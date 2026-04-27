// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTreeGenerator.h"

#include "AssetFactoryModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Generators/StateTree/StateTreeExtract.h"
#include "Generators/StateTree/StateTreeStateBuilder.h"
#include "StateTreeFactory.h"
#include "Misc/PackageName.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StateTreeTasksStatus.h"
#include "Utils/ClassFinderUtils.h"
#include "Utils/PropertySetterUtils.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FString NormalizeObjectPath(const FString& AssetPath)
	{
		FString NormalizedPath = AssetPath;
		if (!NormalizedPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPaths::GetBaseFilename(NormalizedPath);
			if (!AssetName.IsEmpty())
			{
				NormalizedPath += TEXT(".") + AssetName;
			}
		}
		return NormalizedPath;
	}

	FString BuildLongPackageName(const FString& Path, const FString& Name)
	{
		FString FullPath = Path / Name;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/") + FullPath;
		}
		return FullPath;
	}
}

UClass* FStateTreeGenerator::ResolveSchemaClass(const FString& SchemaClassName) const
{
	if (SchemaClassName.IsEmpty())
	{
		return nullptr;
	}

	UClass* SchemaClass = nullptr;
	if (SchemaClassName.StartsWith(TEXT("/")))
	{
		SchemaClass = LoadClass<UStateTreeSchema>(nullptr, *SchemaClassName);
		if (!SchemaClass)
		{
			SchemaClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *SchemaClassName));
		}
		if (!SchemaClass)
		{
			const FString NormalizedSchemaClassPath = NormalizeObjectPath(SchemaClassName);
			if (!NormalizedSchemaClassPath.Equals(SchemaClassName, ESearchCase::CaseSensitive))
			{
				SchemaClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *NormalizedSchemaClassPath));
			}
		}
	}
	else
	{
		SchemaClass = FClassFinderUtils::FindClassByName(SchemaClassName, UStateTreeSchema::StaticClass());
	}

	return SchemaClass && SchemaClass->IsChildOf(UStateTreeSchema::StaticClass()) ? SchemaClass : nullptr;
}

TOptional<FString> FStateTreeGenerator::ValidateSchemaClass(const FString& SchemaClassName) const
{
	if (SchemaClassName.IsEmpty())
	{
		return FString(TEXT("Missing or empty required field 'SchemaClass'"));
	}

	UClass* SchemaClass = ResolveSchemaClass(SchemaClassName);
	if (!SchemaClass)
	{
		return FString::Printf(TEXT("Unknown StateTree SchemaClass: %s"), *SchemaClassName);
	}

	if (!SchemaClass->IsChildOf(UStateTreeSchema::StaticClass()))
	{
		return FString::Printf(TEXT("SchemaClass '%s' is not a UStateTreeSchema subclass"), *SchemaClassName);
	}

	if (SchemaClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return FString::Printf(TEXT("SchemaClass '%s' is abstract and cannot be instantiated"), *SchemaClassName);
	}

	return TOptional<FString>();
}

TOptional<FString> FStateTreeGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	FString AssetType;
	if (!Config->TryGetStringField(TEXT("AssetType"), AssetType) || !AssetType.Equals(TEXT("StateTree"), ESearchCase::CaseSensitive))
	{
		return FString(TEXT("AssetType must be 'StateTree'"));
	}

	FString SchemaClassName;
	Config->TryGetStringField(TEXT("SchemaClass"), SchemaClassName);
	if (TOptional<FString> SchemaError = ValidateSchemaClass(SchemaClassName))
	{
		return SchemaError;
	}

	if (Config->HasField(TEXT("SchemaProperties")) && !Config->HasTypedField<EJson::Object>(TEXT("SchemaProperties")))
	{
		return FString(TEXT("StateTree SchemaProperties must be a JSON object"));
	}

	if ((Config->HasField(TEXT("SubTrees")) && !Config->HasTypedField<EJson::Array>(TEXT("SubTrees")))
		|| (Config->HasField(TEXT("subTrees")) && !Config->HasTypedField<EJson::Array>(TEXT("subTrees"))))
	{
		return FString(TEXT("StateTree SubTrees must be an array"));
	}

	if ((Config->HasField(TEXT("Evaluators")) && !Config->HasTypedField<EJson::Array>(TEXT("Evaluators")))
		|| (Config->HasField(TEXT("evaluators")) && !Config->HasTypedField<EJson::Array>(TEXT("evaluators"))))
	{
		return FString(TEXT("StateTree Evaluators must be an array"));
	}

	if ((Config->HasField(TEXT("GlobalTasks")) && !Config->HasTypedField<EJson::Array>(TEXT("GlobalTasks")))
		|| (Config->HasField(TEXT("globalTasks")) && !Config->HasTypedField<EJson::Array>(TEXT("globalTasks"))))
	{
		return FString(TEXT("StateTree GlobalTasks must be an array"));
	}

	if (Config->HasField(TEXT("Bindings")))
	{
		return FString(TEXT("Bindings input is not supported by the StateTree core lifecycle spec"));
	}

	return TOptional<FString>();
}

UStateTreeEditorData* FStateTreeGenerator::GetEditorData(UStateTree* StateTree) const
{
	if (!StateTree)
	{
		return nullptr;
	}
	return Cast<UStateTreeEditorData>(StateTree->EditorData);
}

UStateTreeSchema* FStateTreeGenerator::GetEditorSchemaInstance(UStateTree* StateTree) const
{
	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	return EditorData ? EditorData->Schema : nullptr;
}

UStateTree* FStateTreeGenerator::CreateStateTreeAsset(const FString& Name, UPackage* Package, UClass* SchemaClass) const
{
	if (!Package || !SchemaClass)
	{
		return nullptr;
	}

	UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
	Factory->SetSchemaClass(SchemaClass);

	return Cast<UStateTree>(Factory->FactoryCreateNew(
		UStateTree::StaticClass(),
		Package,
		*Name,
		RF_Public | RF_Standalone | RF_Transactional,
		nullptr,
		GWarn));
}

bool FStateTreeGenerator::ApplySchemaProperties(UStateTreeSchema* Schema, TSharedPtr<FJsonObject> Config, FString& OutError) const
{
	if (!Schema || !Config.IsValid())
	{
		return true;
	}

	TSharedPtr<FJsonObject> SchemaProperties = GetObjectField(Config, TEXT("SchemaProperties"));
	if (!SchemaProperties.IsValid())
	{
		return true;
	}

	if (!FPropertySetterUtils::SetPropertiesFromJson(Schema, SchemaProperties))
	{
		OutError = TEXT("Failed to apply SchemaProperties");
		return false;
	}

	return true;
}

FString FStateTreeGenerator::FormatCompilerLog(const FStateTreeCompilerLog& Log) const
{
	TArray<TSharedRef<FTokenizedMessage>> Messages = Log.ToTokenizedMessages();
	if (Messages.Num() == 0)
	{
		return TEXT("StateTree compiler failed without diagnostic messages");
	}

	TArray<FString> Parts;
	for (const TSharedRef<FTokenizedMessage>& Message : Messages)
	{
		Parts.Add(Message->ToText().ToString());
	}
	return FString::Join(Parts, TEXT("; "));
}

bool FStateTreeGenerator::CompileStateTree(UStateTree* StateTree, FString& OutError) const
{
	if (!StateTree)
	{
		OutError = TEXT("StateTree is null");
		return false;
	}

	FStateTreeCompilerLog Log;
	if (!UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log))
	{
		OutError = FormatCompilerLog(Log);
		return false;
	}

	return true;
}

bool FStateTreeGenerator::SaveStateTreePackage(UStateTree* StateTree, UPackage* Package, const FString& Name, const FString& Path, FString& OutError) const
{
	if (!StateTree || !Package)
	{
		OutError = TEXT("Failed to resolve StateTree package");
		return false;
	}

	StateTree->PostEditChange();
	StateTree->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, StateTree, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save StateTree package for %s/%s"), *Path, *Name);
		return false;
	}

	return true;
}

FGenerationResult FStateTreeGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	if (!Config.IsValid())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Invalid configuration object"));
	}

	const bool bExists = DoesAssetExist(Path, Name);
	if (Action == EGenerationAction::Create && bExists)
	{
		return FGenerationResult::MakeSkipped(GetAssetType(), Name, Path, TEXT("Asset already exists"));
	}

	if (Action == EGenerationAction::Update && !bExists)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Asset does not exist for update"));
	}

	FString SchemaClassName;
	Config->TryGetStringField(TEXT("SchemaClass"), SchemaClassName);
	UClass* SchemaClass = ResolveSchemaClass(SchemaClassName);
	if (!SchemaClass)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, FString::Printf(TEXT("Unknown StateTree SchemaClass: %s"), *SchemaClassName));
	}

	UStateTree* StateTree = nullptr;
	UPackage* Package = nullptr;

	if (bExists)
	{
		StateTree = Cast<UStateTree>(LoadExistingAsset(Path, Name));
		if (!StateTree)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load StateTree for update"));
		}
		Package = StateTree->GetOutermost();
		if (TOptional<FString> SchemaError = ValidateUpdateSchema(StateTree, SchemaClass))
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, SchemaError.GetValue());
		}
	}
	else
	{
		Package = CreatePackage(*BuildLongPackageName(Path, Name));
		if (!Package)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create StateTree package"));
		}

		StateTree = CreateStateTreeAsset(Name, Package, SchemaClass);
		if (!StateTree)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create StateTree asset"));
		}
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData || !EditorData->Schema || !EditorData->EditorSchema)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("StateTree editor data was not initialized"));
	}

	FString Error;
	if (!ApplySchemaProperties(EditorData->Schema, Config, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	if (!UE::AssetFactory::StateTree::ApplyStateTreeConfig(*EditorData, Config, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	if (!CompileStateTree(StateTree, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	if (!bExists)
	{
		FAssetRegistryModule::AssetCreated(StateTree);
	}

	if (!SaveStateTreePackage(StateTree, Package, Name, Path, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	return bExists
		? FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, StateTree)
		: FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, StateTree);
}

TOptional<FString> FStateTreeGenerator::ValidateUpdateSchema(UStateTree* ExistingTree, UClass* RequestedSchemaClass) const
{
	if (!ExistingTree || !RequestedSchemaClass)
	{
		return FString(TEXT("StateTree update schema validation failed"));
	}

	UStateTreeSchema* ExistingSchema = GetEditorSchemaInstance(ExistingTree);
	if (!ExistingSchema)
	{
		return FString(TEXT("Existing StateTree has no editor schema instance"));
	}

	if (ExistingSchema->GetClass() != RequestedSchemaClass)
	{
		return FString(TEXT("StateTree Update cannot change SchemaClass in core lifecycle spec. Recreate the asset or use a future migration spec."));
	}

	return TOptional<FString>();
}

bool FStateTreeGenerator::CanExtract(UObject* Asset) const
{
	return Asset && Asset->IsA<UStateTree>();
}

TSharedPtr<FJsonObject> FStateTreeGenerator::ExtractSchemaProperties(const UStateTreeSchema* Schema, bool bDiffOnly) const
{
	if (!Schema)
	{
		return nullptr;
	}

	return FPropertySetterUtils::ExtractPropertiesToJson(const_cast<UStateTreeSchema*>(Schema), true, bDiffOnly);
}

TArray<TSharedPtr<FJsonValue>> FStateTreeGenerator::ExtractSubTreesSkeleton(const UStateTreeEditorData* EditorData) const
{
	TArray<TSharedPtr<FJsonValue>> Result;
	if (!EditorData)
	{
		return Result;
	}

	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
	{
		if (!SubTree)
		{
			continue;
		}

		TSharedPtr<FJsonObject> StateJson = MakeShared<FJsonObject>();
		StateJson->SetStringField(TEXT("name"), SubTree->Name.ToString());
		StateJson->SetStringField(TEXT("type"), StaticEnum<EStateTreeStateType>()->GetNameStringByValue(static_cast<int64>(SubTree->Type)));
		StateJson->SetStringField(TEXT("id"), SubTree->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
		Result.Add(MakeShared<FJsonValueObject>(StateJson));
	}

	return Result;
}

TSharedPtr<FJsonObject> FStateTreeGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	UStateTree* StateTree = Cast<UStateTree>(Asset);
	if (!StateTree)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("AssetType"), TEXT("StateTree"));
	OutJson->SetStringField(TEXT("Name"), StateTree->GetName());

	if (UPackage* Package = StateTree->GetOutermost())
	{
		OutJson->SetStringField(TEXT("Path"), FPackageName::GetLongPackagePath(Package->GetName()));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	UStateTreeSchema* Schema = EditorData ? EditorData->Schema : nullptr;
	if (Schema)
	{
		OutJson->SetStringField(TEXT("SchemaClass"), Schema->GetClass()->GetPathName());

		TSharedPtr<FJsonObject> SchemaProps = ExtractSchemaProperties(Schema, bDiffOnly);
		if (SchemaProps.IsValid() && SchemaProps->Values.Num() > 0)
		{
			OutJson->SetObjectField(TEXT("SchemaProperties"), SchemaProps);
		}
	}

	if (EditorData)
	{
		OutJson->SetArrayField(TEXT("Evaluators"), UE::AssetFactory::StateTree::ExtractEditorNodes(EditorData->Evaluators, TEXT("evaluator"), bDiffOnly));
		OutJson->SetArrayField(TEXT("GlobalTasks"), UE::AssetFactory::StateTree::ExtractEditorNodes(EditorData->GlobalTasks, TEXT("globalTask"), bDiffOnly));
		OutJson->SetStringField(TEXT("GlobalTasksCompletion"), StaticEnum<EStateTreeTaskCompletionType>()->GetNameStringByValue(static_cast<int64>(EditorData->GlobalTasksCompletion)));
	}

	TArray<TSharedPtr<FJsonValue>> SubTrees = UE::AssetFactory::StateTree::ExtractSubTrees(EditorData, bDiffOnly);
	OutJson->SetArrayField(TEXT("SubTrees"), SubTrees);

	TSharedPtr<FJsonObject> CompiledJson = MakeShared<FJsonObject>();
	CompiledJson->SetNumberField(TEXT("lastCompiledEditorDataHash"), static_cast<double>(StateTree->LastCompiledEditorDataHash));
	OutJson->SetObjectField(TEXT("Compiled"), CompiledJson);

	return OutJson;
}
