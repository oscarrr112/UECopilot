// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/WidgetBlueprintAssetDocumentCapability.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Dom/JsonValue.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "WidgetBlueprint.h"

namespace
{
bool IsKnownBodyKey(const FString& BodyKey)
{
	for (const FName& KnownBodyKey : FWidgetBlueprintAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (KnownBodyKey.ToString() == BodyKey)
		{
			return true;
		}
	}
	return false;
}

FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult RequireBodyObject(const TSharedRef<FJsonValue>& BodyJson, TSharedPtr<FJsonObject>& OutBody)
{
	if (BodyJson->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	OutBody = BodyJson->AsObject();
	if (!OutBody.IsValid())
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RequireEmptyArray(const TSharedPtr<FJsonValue>& Value, const FString& Path, const FString& BodyKey)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (Value->Type != EJson::Array)
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s must be an array when authored"), *BodyKey),
			Path,
			TEXT("InvalidBodySectionType"));
	}

	if (Value->AsArray().Num() > 0)
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s is not supported yet for non-empty WidgetBlueprint documents"), *BodyKey),
			Path,
			TEXT("UnsupportedWidgetBlueprintRegion"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RequireEmptyObject(const TSharedPtr<FJsonValue>& Value, const FString& Path, const FString& BodyKey)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (Value->Type != EJson::Object)
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s must be an object when authored"), *BodyKey),
			Path,
			TEXT("InvalidBodySectionType"));
	}

	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	if (!Object.IsValid())
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s must be an object when authored"), *BodyKey),
			Path,
			TEXT("InvalidBodySectionType"));
	}

	if (Object->Values.Num() > 0)
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s is not supported yet for non-empty WidgetBlueprint documents"), *BodyKey),
			Path,
			TEXT("UnsupportedWidgetBlueprintRegion"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ResolveUserWidgetParentClass(const TSharedPtr<FJsonValue>& Value, UClass*& OutParentClass)
{
	OutParentClass = nullptr;

	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body.ParentClass must be a ClassRef object"), TEXT("/Body/ParentClass"), TEXT("InvalidParentClass"));
	}

	const TSharedPtr<FJsonObject> ParentClass = Value->AsObject();
	if (!ParentClass.IsValid())
	{
		return BodyFailure(TEXT("Body.ParentClass must be a ClassRef object"), TEXT("/Body/ParentClass"), TEXT("InvalidParentClass"));
	}

	FString Kind;
	if (!ParentClass->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("ClassRef"))
	{
		return BodyFailure(TEXT("Body.ParentClass.Kind must be ClassRef"), TEXT("/Body/ParentClass/Kind"), TEXT("InvalidParentClassKind"));
	}

	FString ClassPath;
	if (!ParentClass->TryGetStringField(TEXT("Class"), ClassPath) || ClassPath.IsEmpty())
	{
		return BodyFailure(TEXT("Body.ParentClass.Class is required"), TEXT("/Body/ParentClass/Class"), TEXT("MissingParentClass"));
	}

	OutParentClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
	if (!OutParentClass)
	{
		return BodyFailure(
			FString::Printf(TEXT("Failed to resolve Body.ParentClass.Class '%s'"), *ClassPath),
			TEXT("/Body/ParentClass/Class"),
			TEXT("UnresolvedParentClass"));
	}

	if (!OutParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.ParentClass.Class '%s' is not a UUserWidget subclass"), *OutParentClass->GetName()),
			TEXT("/Body/ParentClass/Class"),
			TEXT("InvalidParentClass"));
	}

	if (OutParentClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.ParentClass.Class '%s' is abstract"), *OutParentClass->GetName()),
			TEXT("/Body/ParentClass/Class"),
			TEXT("AbstractParentClass"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

TSharedRef<FJsonObject> MakeClassRef(UClass* Class)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), Class ? Class->GetPathName() : FString(TEXT("/Script/UMG.UserWidget")));
	return ClassRef;
}

TSharedRef<FJsonObject> MakeDefaultWidgetTree()
{
	TSharedRef<FJsonObject> WidgetTree = MakeShared<FJsonObject>();
	WidgetTree->SetField(TEXT("RootWidget"), MakeShared<FJsonValueNull>());
	WidgetTree->SetObjectField(TEXT("NamedSlotBindings"), MakeShared<FJsonObject>());
	return WidgetTree;
}

FAssetDocumentCapabilityResult ValidateDefaultWidgetTree(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (Value->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body.WidgetTree must be an object when authored"), TEXT("/Body/WidgetTree"), TEXT("InvalidBodySectionType"));
	}

	const TSharedPtr<FJsonObject> WidgetTree = Value->AsObject();
	if (!WidgetTree.IsValid())
	{
		return BodyFailure(TEXT("Body.WidgetTree must be an object when authored"), TEXT("/Body/WidgetTree"), TEXT("InvalidBodySectionType"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : WidgetTree->Values)
	{
		if (Pair.Key == TEXT("RootWidget"))
		{
			if (Pair.Value.IsValid() && Pair.Value->Type != EJson::Null)
			{
				return BodyFailure(TEXT("Body.WidgetTree.RootWidget is not supported yet when non-null"), TEXT("/Body/WidgetTree/RootWidget"), TEXT("UnsupportedWidgetBlueprintRegion"));
			}
			continue;
		}

		if (Pair.Key == TEXT("NamedSlotBindings"))
		{
			if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
			{
				return BodyFailure(TEXT("Body.WidgetTree.NamedSlotBindings must be an object"), TEXT("/Body/WidgetTree/NamedSlotBindings"), TEXT("InvalidBodySectionType"));
			}
			const TSharedPtr<FJsonObject> NamedSlotBindings = Pair.Value->AsObject();
			if (NamedSlotBindings.IsValid() && NamedSlotBindings->Values.Num() > 0)
			{
				return BodyFailure(TEXT("Body.WidgetTree.NamedSlotBindings is not supported yet when non-empty"), TEXT("/Body/WidgetTree/NamedSlotBindings"), TEXT("UnsupportedWidgetBlueprintRegion"));
			}
			continue;
		}

		return BodyFailure(
			FString::Printf(TEXT("Unknown Body.WidgetTree key '%s'"), *Pair.Key),
			FString::Printf(TEXT("/Body/WidgetTree/%s"), *Pair.Key),
			TEXT("UnknownBodyKey"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FString JsonValueToComparableString(TSharedPtr<FJsonValue> Value)
{
	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Value.IsValid() ? Value.ToSharedRef() : MakeShared<FJsonValueNull>(), TEXT(""), Writer);
	return JsonText;
}

void AddBodyDiffEntry(
	TArray<TSharedPtr<FJsonValue>>& Entries,
	const FString& Path,
	const FString& Status,
	TSharedPtr<FJsonValue> Current,
	TSharedPtr<FJsonValue> Desired)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), Path);
	Entry->SetStringField(TEXT("status"), Status);
	Entry->SetField(TEXT("current"), Current.IsValid() ? Current : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("desired"), Desired.IsValid() ? Desired : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}
}

const TArray<FName>& FWidgetBlueprintAssetDocumentCapability::GetCanonicalBodyKeys()
{
	static const TArray<FName> Keys = {
		TEXT("ParentClass"),
		TEXT("ImplementedInterfaces"),
		TEXT("Variables"),
		TEXT("ClassDefaults"),
		TEXT("WidgetTree"),
		TEXT("Bindings"),
		TEXT("Animations"),
		TEXT("UbergraphPages"),
		TEXT("FunctionGraphs"),
		TEXT("MacroGraphs"),
		TEXT("Palette"),
		TEXT("EditorOptions"),
		TEXT("WidgetVariableGuids"),
	};
	return Keys;
}

FName FWidgetBlueprintAssetDocumentCapability::GetName() const
{
	return TEXT("WidgetBlueprintBody");
}

TArray<FName> FWidgetBlueprintAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {TEXT("WidgetBlueprintBody"), TEXT("WidgetBlueprintEmptyAssetContract")};
}

int32 FWidgetBlueprintAssetDocumentCapability::GetApplyOrder() const
{
	return 60;
}

bool FWidgetBlueprintAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && Asset->GetClass() == UWidgetBlueprint::StaticClass();
}

bool FWidgetBlueprintAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UWidgetBlueprint::StaticClass();
}

TSharedRef<FJsonObject> FWidgetBlueprintAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("ParentClass"), TEXT("ClassRef<UUserWidget>"));
	Schema->SetStringField(TEXT("ImplementedInterfaces"), TEXT("array empty until WidgetBlueprint interface adapter lands"));
	Schema->SetStringField(TEXT("Variables"), TEXT("array empty until WidgetBlueprint variable adapter lands"));
	Schema->SetStringField(TEXT("ClassDefaults"), TEXT("object empty until WidgetBlueprint CDO defaults adapter lands"));
	Schema->SetStringField(TEXT("WidgetTree"), TEXT("object {RootWidget:null, NamedSlotBindings:{}} until WidgetTree adapter lands"));
	Schema->SetStringField(TEXT("Bindings"), TEXT("array empty until WidgetBlueprint binding adapter lands"));
	Schema->SetStringField(TEXT("Animations"), TEXT("array empty until WidgetBlueprint animation adapter lands"));
	Schema->SetStringField(TEXT("UbergraphPages"), TEXT("array empty until WidgetBlueprint graph adapter lands"));
	Schema->SetStringField(TEXT("FunctionGraphs"), TEXT("array empty until WidgetBlueprint graph adapter lands"));
	Schema->SetStringField(TEXT("MacroGraphs"), TEXT("array empty until WidgetBlueprint graph adapter lands"));
	Schema->SetStringField(TEXT("Palette"), TEXT("object empty until WidgetBlueprint metadata adapter lands"));
	Schema->SetStringField(TEXT("EditorOptions"), TEXT("object empty until WidgetBlueprint metadata adapter lands"));
	Schema->SetStringField(TEXT("WidgetVariableGuids"), TEXT("object empty until WidgetBlueprint metadata adapter lands"));
	return Schema;
}

FAssetDocumentCapabilityResult FWidgetBlueprintAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = RequireBodyObject(BodyJson, BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}
	return ValidateBodyObject(Context, BodyObject.ToSharedRef());
}

FAssetDocumentCapabilityResult FWidgetBlueprintAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	return Validate(Context, BodyJson);
}

FAssetDocumentCapabilityResult FWidgetBlueprintAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Context.Asset);
	if (!WidgetBlueprint)
	{
		return BodyFailure(TEXT("WidgetBlueprint body apply requires exact UWidgetBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = RequireBodyObject(BodyJson, BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	const FAssetDocumentCapabilityResult ValidateResult = ValidateBodyObject(Context, BodyObject.ToSharedRef());
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	UClass* ParentClass = nullptr;
	const FAssetDocumentCapabilityResult ParentClassResult = ResolveUserWidgetParentClass(BodyObject->Values.FindChecked(TEXT("ParentClass")), ParentClass);
	if (!ParentClassResult.bSuccess)
	{
		return ParentClassResult;
	}

	bool bChanged = false;
	if (WidgetBlueprint->ParentClass.Get() != ParentClass)
	{
		WidgetBlueprint->Modify();
		WidgetBlueprint->ParentClass = ParentClass;
		bChanged = true;
	}

#if WITH_EDITORONLY_DATA
	if (WidgetBlueprint->WidgetTree)
	{
		WidgetBlueprint->WidgetTree->Modify();
		if (WidgetBlueprint->WidgetTree->RootWidget)
		{
			WidgetBlueprint->WidgetTree->RootWidget = nullptr;
			bChanged = true;
		}
		if (WidgetBlueprint->WidgetTree->NamedSlotBindings.Num() > 0)
		{
			WidgetBlueprint->WidgetTree->NamedSlotBindings.Empty();
			bChanged = true;
		}
	}
	if (WidgetBlueprint->Bindings.Num() > 0)
	{
		WidgetBlueprint->Modify();
		WidgetBlueprint->Bindings.Empty();
		bChanged = true;
	}
	if (WidgetBlueprint->Animations.Num() > 0)
	{
		WidgetBlueprint->Modify();
		WidgetBlueprint->Animations.Empty();
		bChanged = true;
	}
#endif

	if (bChanged)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
		if (WidgetBlueprint->Status == BS_Error)
		{
			return BodyFailure(TEXT("Failed to compile WidgetBlueprint after applying empty Body contract"), TEXT("/Body"), TEXT("WidgetBlueprintCompileFailed"));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Applied WidgetBlueprint Body"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	if (Context.Asset && !SupportsAsset(Context.Asset))
	{
		return BodyFailure(TEXT("WidgetBlueprint body extract requires exact UWidgetBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}
	if (!Context.Asset && Context.AssetClass && !SupportsClass(Context.AssetClass))
	{
		return BodyFailure(TEXT("WidgetBlueprint body extract requires exact UWidgetBlueprint class"), TEXT("/Class"), TEXT("UnsupportedClass"));
	}

	const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Context.Asset);
	OutBodyJson->SetObjectField(TEXT("ParentClass"), MakeClassRef(WidgetBlueprint && WidgetBlueprint->ParentClass ? WidgetBlueprint->ParentClass.Get() : UUserWidget::StaticClass()));
	OutBodyJson->SetArrayField(TEXT("ImplementedInterfaces"), {});
	OutBodyJson->SetArrayField(TEXT("Variables"), {});
	OutBodyJson->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	OutBodyJson->SetObjectField(TEXT("WidgetTree"), MakeDefaultWidgetTree());
	OutBodyJson->SetArrayField(TEXT("Bindings"), {});
	OutBodyJson->SetArrayField(TEXT("Animations"), {});
	OutBodyJson->SetArrayField(TEXT("UbergraphPages"), {});
	OutBodyJson->SetArrayField(TEXT("FunctionGraphs"), {});
	OutBodyJson->SetArrayField(TEXT("MacroGraphs"), {});
	OutBodyJson->SetObjectField(TEXT("Palette"), MakeShared<FJsonObject>());
	OutBodyJson->SetObjectField(TEXT("EditorOptions"), MakeShared<FJsonObject>());
	OutBodyJson->SetObjectField(TEXT("WidgetVariableGuids"), MakeShared<FJsonObject>());

	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted WidgetBlueprint Body"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TSharedPtr<FJsonObject> DesiredBody;
	const FAssetDocumentCapabilityResult ObjectResult = RequireBodyObject(DesiredJson, DesiredBody);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	const FAssetDocumentCapabilityResult ValidateResult = ValidateBodyObject(Context, DesiredBody.ToSharedRef());
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	TSharedRef<FJsonObject> CurrentBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Extract(Context, CurrentBody);
	if (!ExtractResult.bSuccess)
	{
		return ExtractResult;
	}

	for (const FName& BodyKeyName : GetCanonicalBodyKeys())
	{
		const FString BodyKey = BodyKeyName.ToString();
		const TSharedPtr<FJsonValue>* Current = CurrentBody->Values.Find(BodyKey);
		const TSharedPtr<FJsonValue>* Desired = DesiredBody->Values.Find(BodyKey);
		const TSharedPtr<FJsonValue> CurrentValue = Current ? *Current : MakeShared<FJsonValueNull>();
		const TSharedPtr<FJsonValue> DesiredValue = Desired ? *Desired : MakeShared<FJsonValueNull>();
		const FString Status = JsonValueToComparableString(CurrentValue) == JsonValueToComparableString(DesiredValue)
			? TEXT("unchanged")
			: TEXT("changed");
		AddBodyDiffEntry(OutDiffEntries, FString::Printf(TEXT("/Body/%s"), *BodyKey), Status, CurrentValue, DesiredValue);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("WidgetBlueprint Body diffed"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintAssetDocumentCapability::ValidateBodyObject(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject) const
{
	if (Context.Asset && !SupportsAsset(Context.Asset))
	{
		return BodyFailure(TEXT("WidgetBlueprint body validation requires exact UWidgetBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}
	if (!Context.Asset && Context.AssetClass && !SupportsClass(Context.AssetClass))
	{
		return BodyFailure(TEXT("WidgetBlueprint body validation requires exact UWidgetBlueprint class"), TEXT("/Class"), TEXT("UnsupportedClass"));
	}

	if (!BodyObject->HasField(TEXT("ParentClass")))
	{
		return BodyFailure(TEXT("Body.ParentClass is required for WidgetBlueprint documents"), TEXT("/Body/ParentClass"), TEXT("MissingParentClass"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		if (!IsKnownBodyKey(Pair.Key))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unknown WidgetBlueprint Body key '%s'"), *Pair.Key),
				FString::Printf(TEXT("/Body/%s"), *Pair.Key),
				TEXT("UnknownBodyKey"));
		}

		if (Pair.Key == TEXT("ParentClass"))
		{
			UClass* ParentClass = nullptr;
			const FAssetDocumentCapabilityResult ParentClassResult = ResolveUserWidgetParentClass(Pair.Value, ParentClass);
			if (!ParentClassResult.bSuccess)
			{
				return ParentClassResult;
			}
		}
		else if (Pair.Key == TEXT("WidgetTree"))
		{
			const FAssetDocumentCapabilityResult WidgetTreeResult = ValidateDefaultWidgetTree(Pair.Value);
			if (!WidgetTreeResult.bSuccess)
			{
				return WidgetTreeResult;
			}
		}
		else if (Pair.Key == TEXT("ClassDefaults") || Pair.Key == TEXT("Palette") || Pair.Key == TEXT("EditorOptions") || Pair.Key == TEXT("WidgetVariableGuids"))
		{
			const FAssetDocumentCapabilityResult ObjectResult = RequireEmptyObject(Pair.Value, FString::Printf(TEXT("/Body/%s"), *Pair.Key), Pair.Key);
			if (!ObjectResult.bSuccess)
			{
				return ObjectResult;
			}
		}
		else
		{
			const FAssetDocumentCapabilityResult ArrayResult = RequireEmptyArray(Pair.Value, FString::Printf(TEXT("/Body/%s"), *Pair.Key), Pair.Key);
			if (!ArrayResult.bSuccess)
			{
				return ArrayResult;
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated WidgetBlueprint Body scaffold"));
}
