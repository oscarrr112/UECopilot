// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/WidgetBlueprintAssetDocumentCapability.h"

#include "AssetDocumentPropertyAdapter.h"
#include "Profiles/WidgetBlueprintTreeAdapter.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Dom/JsonValue.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
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

bool IsLegalWidgetBlueprintParentClass(const UClass* ParentClass)
{
	return ParentClass
		&& ParentClass->IsChildOf(UUserWidget::StaticClass())
		&& (!ParentClass->HasAnyClassFlags(CLASS_Abstract) || ParentClass == UUserWidget::StaticClass());
}

FGuid MakeDeterministicWidgetVariableGuid(const FString& TargetAssetPath, const FName& VariableName)
{
	return FGuid::NewDeterministicGuid(FString::Printf(
		TEXT("%s|WidgetVariableGuids|%s"),
		*TargetAssetPath,
		*VariableName.ToString()));
}

void CollectPublicWidgetVariableNames(const UWidgetBlueprint* WidgetBlueprint, TSet<FName>& OutVariables)
{
	OutVariables.Reset();
#if WITH_EDITORONLY_DATA
	if (!WidgetBlueprint)
	{
		return;
	}

	if (WidgetBlueprint->WidgetTree)
	{
		WidgetBlueprint->WidgetTree->ForEachWidget([&OutVariables](UWidget* Widget)
		{
			if (Widget && Widget->bIsVariable)
			{
				OutVariables.Add(Widget->GetFName());
			}
		});
	}
	for (UWidgetAnimation* Animation : WidgetBlueprint->Animations)
	{
		if (Animation)
		{
			OutVariables.Add(Animation->GetFName());
		}
	}
#endif
}

void CollectCompilerWidgetVariableNames(const UWidgetBlueprint* WidgetBlueprint, TSet<FName>& OutVariables)
{
	OutVariables.Reset();
#if WITH_EDITORONLY_DATA
	if (!WidgetBlueprint)
	{
		return;
	}

	if (WidgetBlueprint->WidgetTree)
	{
		WidgetBlueprint->WidgetTree->ForEachWidget([&OutVariables](UWidget* Widget)
		{
			if (Widget)
			{
				OutVariables.Add(Widget->GetFName());
			}
		});
	}
	for (UWidgetAnimation* Animation : WidgetBlueprint->Animations)
	{
		if (Animation)
		{
			OutVariables.Add(Animation->GetFName());
		}
	}
#endif
}

void SyncWidgetTreeVariableGuidsForCompile(
	UWidgetBlueprint* WidgetBlueprint,
	const FString& TargetAssetPath,
	const TMap<FName, FGuid>& DesiredGuids)
{
#if WITH_EDITORONLY_DATA
	if (!WidgetBlueprint)
	{
		return;
	}

	TSet<FName> SourceVariables;
	CollectCompilerWidgetVariableNames(WidgetBlueprint, SourceVariables);
	WidgetBlueprint->Modify();
	WidgetBlueprint->WidgetVariableNameToGuidMap.Empty();

	TSet<FGuid> UsedGuids;
	TArray<FName> SortedVariables = SourceVariables.Array();
	SortedVariables.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	for (const FName& SourceVariable : SortedVariables)
	{
		FGuid VariableGuid = DesiredGuids.FindRef(SourceVariable);
		if (!VariableGuid.IsValid())
		{
			VariableGuid = MakeDeterministicWidgetVariableGuid(TargetAssetPath, SourceVariable);
		}
		if (!VariableGuid.IsValid())
		{
			VariableGuid = MakeDeterministicWidgetVariableGuid(TargetAssetPath, SourceVariable);
		}
		if (UsedGuids.Contains(VariableGuid))
		{
			VariableGuid = MakeDeterministicWidgetVariableGuid(FString::Printf(TEXT("%s|duplicate"), *TargetAssetPath), SourceVariable);
		}

		UsedGuids.Add(VariableGuid);
		WidgetBlueprint->WidgetVariableNameToGuidMap.Add(SourceVariable, VariableGuid);
	}
#endif
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

FAssetDocumentCapabilityResult RequireObjectSection(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	const FString& BodyKey,
	TSharedPtr<FJsonObject>& OutObject)
{
	OutObject.Reset();
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		OutObject = MakeShared<FJsonObject>();
		return FAssetDocumentCapabilityResult::Success();
	}

	if (Value->Type != EJson::Object)
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s must be an object when authored"), *BodyKey),
			Path,
			TEXT("InvalidBodySectionType"));
	}

	OutObject = Value->AsObject();
	if (!OutObject.IsValid())
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s must be an object when authored"), *BodyKey),
			Path,
			TEXT("InvalidBodySectionType"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseObjectSection(
	const TSharedPtr<FJsonObject>& BodyObject,
	const FString& BodyKey,
	TSharedPtr<FJsonObject>& OutObject)
{
	const TSharedPtr<FJsonValue>* Value = BodyObject.IsValid() ? BodyObject->Values.Find(BodyKey) : nullptr;
	return RequireObjectSection(Value ? *Value : nullptr, FString::Printf(TEXT("/Body/%s"), *BodyKey), BodyKey, OutObject);
}

bool IsSupportedClassDefaultProperty(FProperty* Property)
{
	return CastField<FBoolProperty>(Property)
		|| CastField<FEnumProperty>(Property)
		|| CastField<FByteProperty>(Property)
		|| CastField<FNumericProperty>(Property)
		|| CastField<FStrProperty>(Property)
		|| CastField<FNameProperty>(Property)
		|| CastField<FTextProperty>(Property)
		|| CastField<FObjectPropertyBase>(Property)
		|| CastField<FSoftObjectProperty>(Property)
		|| CastField<FClassProperty>(Property)
		|| CastField<FSoftClassProperty>(Property);
}

FAssetDocumentCapabilityResult ClassDefaultPropertyFailure(const FAssetDocumentPropertyApplyResult& PropertyResult)
{
	if (PropertyResult.Diagnostics.Num() > 0)
	{
		const FAssetDocumentDiagnostic& Diagnostic = PropertyResult.Diagnostics[0];
		const FString DiagnosticPath = Diagnostic.Path.IsEmpty()
			? FString(TEXT("/Body/ClassDefaults"))
			: FString(TEXT("/Body/ClassDefaults/")) + Diagnostic.Path.Replace(TEXT("Properties."), TEXT(""));
		return BodyFailure(Diagnostic.Message, DiagnosticPath, Diagnostic.Code);
	}
	return BodyFailure(PropertyResult.Message, TEXT("/Body/ClassDefaults"), TEXT("InvalidClassDefaults"));
}

FAssetDocumentCapabilityResult PreflightClassDefaults(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonObject>& ClassDefaults)
{
	if (!ClassDefaults.IsValid() || ClassDefaults->Values.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	UClass* GeneratedClass = WidgetBlueprint ? WidgetBlueprint->GeneratedClass : nullptr;
	if (!GeneratedClass)
	{
		return BodyFailure(TEXT("Body.ClassDefaults requires a compiled WidgetBlueprint generated class"), TEXT("/Body/ClassDefaults"), TEXT("MissingGeneratedClass"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ClassDefaults->Values)
	{
		FProperty* Property = FindFProperty<FProperty>(GeneratedClass, *Pair.Key);
		if (!Property)
		{
			return BodyFailure(
				FString::Printf(TEXT("Class default property '%s' does not exist"), *Pair.Key),
				FString::Printf(TEXT("/Body/ClassDefaults/%s"), *Pair.Key),
				TEXT("UnknownProperty"));
		}
		if (!FAssetDocumentPropertyAdapter::IsWritableProperty(Property))
		{
			return BodyFailure(
				FString::Printf(TEXT("Class default property '%s' is not writable: %s"), *Pair.Key, *FAssetDocumentPropertyAdapter::GetNonWritableReason(Property)),
				FString::Printf(TEXT("/Body/ClassDefaults/%s"), *Pair.Key),
				TEXT("NonWritable"));
		}
		if (!IsSupportedClassDefaultProperty(Property))
		{
			return BodyFailure(
				FString::Printf(TEXT("Class default property '%s' uses unsupported type '%s'"), *Pair.Key, *FAssetDocumentPropertyAdapter::GetTypeToken(Property)),
				FString::Printf(TEXT("/Body/ClassDefaults/%s"), *Pair.Key),
				TEXT("UnsupportedClassDefaultValueType"));
		}
	}

	const FAssetDocumentPropertyApplyResult PreflightResult =
		FAssetDocumentPropertyAdapter::PreflightProperties(GeneratedClass, ClassDefaults);
	if (!PreflightResult.bSuccess)
	{
		return ClassDefaultPropertyFailure(PreflightResult);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyClassDefaults(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonObject>& ClassDefaults)
{
	UClass* GeneratedClass = WidgetBlueprint ? WidgetBlueprint->GeneratedClass : nullptr;
	UObject* GeneratedCDO = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
	UClass* ParentClass = GeneratedClass ? GeneratedClass->GetSuperClass() : nullptr;
	UObject* ParentCDO = ParentClass ? ParentClass->GetDefaultObject(false) : nullptr;
	if (!GeneratedCDO || !ParentCDO)
	{
		return BodyFailure(TEXT("Body.ClassDefaults requires generated and parent CDOs"), TEXT("/Body/ClassDefaults"), TEXT("MissingGeneratedCDO"));
	}

	GeneratedCDO->Modify();
	for (TFieldIterator<FProperty> PropertyIt(GeneratedClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!FAssetDocumentPropertyAdapter::IsWritableProperty(Property) || !IsSupportedClassDefaultProperty(Property))
		{
			continue;
		}
		if (ClassDefaults.IsValid() && ClassDefaults->HasField(Property->GetName()))
		{
			continue;
		}

		FProperty* ParentProperty = FindFProperty<FProperty>(ParentClass, Property->GetFName());
		if (!ParentProperty || !ParentProperty->SameType(Property))
		{
			continue;
		}

		void* GeneratedValuePtr = Property->ContainerPtrToValuePtr<void>(GeneratedCDO);
		const void* ParentValuePtr = ParentProperty->ContainerPtrToValuePtr<void>(ParentCDO);
		if (!Property->Identical(GeneratedValuePtr, ParentValuePtr))
		{
			Property->CopyCompleteValue(GeneratedValuePtr, ParentValuePtr);
		}
	}

	const FAssetDocumentPropertyApplyResult PropertyResult =
		FAssetDocumentPropertyAdapter::ApplyProperties(GeneratedCDO, ClassDefaults);
	if (!PropertyResult.bSuccess)
	{
		return ClassDefaultPropertyFailure(PropertyResult);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
	return FAssetDocumentCapabilityResult::Success();
}

TSharedPtr<FJsonObject> ExtractClassDefaults(const UWidgetBlueprint* WidgetBlueprint)
{
	TSharedPtr<FJsonObject> PropertiesJson = MakeShared<FJsonObject>();
	UClass* GeneratedClass = WidgetBlueprint ? WidgetBlueprint->GeneratedClass : nullptr;
	UObject* GeneratedCDO = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
	UClass* ParentClass = GeneratedClass ? GeneratedClass->GetSuperClass() : nullptr;
	UObject* ParentCDO = ParentClass ? ParentClass->GetDefaultObject(false) : nullptr;
	if (!GeneratedCDO || !ParentCDO)
	{
		return PropertiesJson;
	}

	for (TFieldIterator<FProperty> PropertyIt(GeneratedClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!FAssetDocumentPropertyAdapter::IsWritableProperty(Property) || !IsSupportedClassDefaultProperty(Property))
		{
			continue;
		}

		FProperty* BaselineProperty = FindFProperty<FProperty>(ParentClass, Property->GetFName());
		if (!BaselineProperty || !BaselineProperty->SameType(Property))
		{
			continue;
		}

		const void* CurrentValuePtr = Property->ContainerPtrToValuePtr<void>(GeneratedCDO);
		const void* BaselineValuePtr = BaselineProperty->ContainerPtrToValuePtr<void>(ParentCDO);
		if (Property->Identical(CurrentValuePtr, BaselineValuePtr))
		{
			continue;
		}

		TSharedPtr<FJsonValue> JsonValue = FAssetDocumentPropertyAdapter::ExtractPropertyValue(Property, CurrentValuePtr);
		if (JsonValue.IsValid())
		{
			PropertiesJson->SetField(Property->GetName(), JsonValue);
		}
	}

	return PropertiesJson;
}

FAssetDocumentCapabilityResult ParseWidgetVariableGuids(
	const TSharedPtr<FJsonObject>& WidgetVariableGuids,
	TMap<FName, FGuid>& OutGuids)
{
	OutGuids.Reset();
	if (!WidgetVariableGuids.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : WidgetVariableGuids->Values)
	{
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String)
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.WidgetVariableGuids.%s must be a GUID string"), *Pair.Key),
				FString::Printf(TEXT("/Body/WidgetVariableGuids/%s"), *Pair.Key),
				TEXT("InvalidWidgetVariableGuid"));
		}

		FGuid Guid;
		if (!FGuid::Parse(Pair.Value->AsString(), Guid) || !Guid.IsValid())
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.WidgetVariableGuids.%s is not a valid GUID"), *Pair.Key),
				FString::Printf(TEXT("/Body/WidgetVariableGuids/%s"), *Pair.Key),
				TEXT("InvalidWidgetVariableGuid"));
		}
		OutGuids.Add(FName(*Pair.Key), Guid);
	}

	return FAssetDocumentCapabilityResult::Success();
}

TSharedRef<FJsonObject> BuildWidgetVariableGuidsJson(
	const UWidgetBlueprint* WidgetBlueprint,
	const FString& TargetAssetPath,
	const TMap<FName, FGuid>* DesiredGuids = nullptr)
{
	TSet<FName> SourceVariables;
	CollectPublicWidgetVariableNames(WidgetBlueprint, SourceVariables);

	TArray<FName> SortedVariables = SourceVariables.Array();
	SortedVariables.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const FName& VariableName : SortedVariables)
	{
		FGuid Guid = DesiredGuids ? DesiredGuids->FindRef(VariableName) : FGuid();
		if (!Guid.IsValid() && WidgetBlueprint)
		{
			Guid = WidgetBlueprint->WidgetVariableNameToGuidMap.FindRef(VariableName);
		}
		if (!Guid.IsValid())
		{
			Guid = MakeDeterministicWidgetVariableGuid(TargetAssetPath, VariableName);
		}
		Json->SetStringField(VariableName.ToString(), Guid.ToString(EGuidFormats::DigitsWithHyphensLower));
	}
	return Json;
}

FAssetDocumentCapabilityResult CollectExplicitVariableNames(
	const TSharedPtr<FJsonValue>& VariablesValue,
	TSet<FName>& OutVariableNames)
{
	OutVariableNames.Reset();
	if (!VariablesValue.IsValid() || VariablesValue->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (VariablesValue->Type != EJson::Array)
	{
		return BodyFailure(TEXT("Body.Variables must be an array when authored"), TEXT("/Body/Variables"), TEXT("InvalidBodySectionType"));
	}

	TSet<FName> SeenNames;
	const TArray<TSharedPtr<FJsonValue>>& Variables = VariablesValue->AsArray();
	for (int32 Index = 0; Index < Variables.Num(); ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Body/Variables/%d"), Index);
		const TSharedPtr<FJsonObject> VariableObject = Variables[Index].IsValid() ? Variables[Index]->AsObject() : nullptr;
		if (!VariableObject.IsValid())
		{
			return BodyFailure(TEXT("Body.Variables entries must be objects"), Path, TEXT("InvalidVariable"));
		}

		FString Name;
		if (!VariableObject->TryGetStringField(TEXT("Name"), Name) || Name.IsEmpty())
		{
			return BodyFailure(TEXT("Variable Name is required"), Path / TEXT("Name"), TEXT("MissingVariableName"));
		}

		const FName VariableName(*Name);
		if (SeenNames.Contains(VariableName))
		{
			return BodyFailure(
				FString::Printf(TEXT("Duplicate Body.Variables Name '%s'"), *Name),
				Path / TEXT("Name"),
				TEXT("DuplicateVariableName"));
		}
		SeenNames.Add(VariableName);
		OutVariableNames.Add(VariableName);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateVariableWidgetNameConflicts(const TSharedRef<FJsonObject>& BodyObject)
{
	TSet<FName> ExplicitVariableNames;
	const TSharedPtr<FJsonValue>* VariablesValue = BodyObject->Values.Find(TEXT("Variables"));
	const FAssetDocumentCapabilityResult VariablesResult =
		CollectExplicitVariableNames(VariablesValue ? *VariablesValue : nullptr, ExplicitVariableNames);
	if (!VariablesResult.bSuccess)
	{
		return VariablesResult;
	}

	if (ExplicitVariableNames.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSet<FName> VariableWidgetNames;
	const TSharedPtr<FJsonValue>* WidgetTreeValue = BodyObject->Values.Find(TEXT("WidgetTree"));
	const FAssetDocumentCapabilityResult WidgetNamesResult =
		FWidgetBlueprintTreeAdapter::CollectVariableWidgetNames(WidgetTreeValue ? *WidgetTreeValue : nullptr, VariableWidgetNames);
	if (!WidgetNamesResult.bSuccess)
	{
		return WidgetNamesResult;
	}

	for (const FName& VariableName : ExplicitVariableNames)
	{
		if (VariableWidgetNames.Contains(VariableName))
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.Variables Name '%s' conflicts with a variable widget of the same name"), *VariableName.ToString()),
				FString::Printf(TEXT("/Body/Variables/%s"), *VariableName.ToString()),
				TEXT("VariableWidgetNameConflict"));
		}
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

	if (!IsLegalWidgetBlueprintParentClass(OutParentClass))
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.ParentClass.Class '%s' is abstract"), *OutParentClass->GetName()),
			TEXT("/Body/ParentClass/Class"),
			TEXT("AbstractParentClass"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

void AddSkippedEvidence(TSharedRef<FJsonObject>& OutBodyJson, const FString& Path, const FString& Message)
{
	const TSharedPtr<FJsonObject>* ExistingSkipped = nullptr;
	TSharedPtr<FJsonObject> Skipped;
	if (OutBodyJson->TryGetObjectField(TEXT("_Skipped"), ExistingSkipped) && ExistingSkipped && ExistingSkipped->IsValid())
	{
		Skipped = *ExistingSkipped;
	}
	if (!Skipped.IsValid())
	{
		Skipped = MakeShared<FJsonObject>();
		OutBodyJson->SetObjectField(TEXT("_Skipped"), Skipped);
	}

	TArray<TSharedPtr<FJsonValue>> Entries;
	const TArray<TSharedPtr<FJsonValue>>* ExistingEntries = nullptr;
	if (Skipped->TryGetArrayField(TEXT("UnsupportedWidgetBlueprintRegions"), ExistingEntries) && ExistingEntries)
	{
		Entries = *ExistingEntries;
	}

	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("Code"), TEXT("UnsupportedWidgetBlueprintRegion"));
	Entry->SetStringField(TEXT("Path"), Path);
	Entry->SetStringField(TEXT("Message"), Message);
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
	Skipped->SetArrayField(TEXT("UnsupportedWidgetBlueprintRegions"), Entries);
}

struct FUnsupportedCurrentRegion
{
	FString Path;
	FString Message;
};

void CollectUnsupportedCurrentRegions(const UWidgetBlueprint* WidgetBlueprint, TArray<FUnsupportedCurrentRegion>& OutRegions)
{
	OutRegions.Reset();
	if (!WidgetBlueprint)
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	if (WidgetBlueprint->Bindings.Num() > 0)
	{
		OutRegions.Add({
			TEXT("/Body/Bindings"),
			TEXT("Existing WidgetBlueprint has non-empty Bindings that the current AssetDocument adapter cannot safely apply or diff")
		});
	}
	if (WidgetBlueprint->Animations.Num() > 0)
	{
		OutRegions.Add({
			TEXT("/Body/Animations"),
			TEXT("Existing WidgetBlueprint has non-empty Animations that the current AssetDocument adapter cannot safely apply or diff")
		});
	}
#endif
}

FAssetDocumentCapabilityResult FailOnUnsupportedCurrentRegions(const UWidgetBlueprint* WidgetBlueprint)
{
	TArray<FUnsupportedCurrentRegion> UnsupportedRegions;
	CollectUnsupportedCurrentRegions(WidgetBlueprint, UnsupportedRegions);
	if (UnsupportedRegions.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentCapabilityResult Result = FAssetDocumentCapabilityResult::Failure(
		TEXT("Existing WidgetBlueprint contains unsupported non-empty regions"),
		UnsupportedRegions[0].Path,
		TEXT("UnsupportedWidgetBlueprintRegion"));
	Result.Diagnostics.Reset();
	for (const FUnsupportedCurrentRegion& UnsupportedRegion : UnsupportedRegions)
	{
		FAssetDocumentDiagnostic Diagnostic;
		Diagnostic.Path = UnsupportedRegion.Path;
		Diagnostic.Code = TEXT("UnsupportedWidgetBlueprintRegion");
		Diagnostic.Message = UnsupportedRegion.Message;
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
	}
	return Result;
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
	return FWidgetBlueprintTreeAdapter::MakeDefaultWidgetTree();
}

FAssetDocumentCapabilityResult ValidateDefaultWidgetTree(const TSharedPtr<FJsonValue>& Value)
{
	return FWidgetBlueprintTreeAdapter::Validate(Value);
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
	Schema->SetStringField(TEXT("Variables"), TEXT("array of explicit Blueprint variables; names must not conflict with variable widgets"));
	Schema->SetStringField(TEXT("ClassDefaults"), TEXT("object of reflected generated CDO default differences"));
	Schema->SetStringField(TEXT("WidgetTree"), TEXT("object {RootWidget:WidgetNode|null, NamedSlotBindings:map<string, WidgetNode>}"));
	Schema->SetStringField(TEXT("Bindings"), TEXT("array empty until WidgetBlueprint binding adapter lands"));
	Schema->SetStringField(TEXT("Animations"), TEXT("array empty until WidgetBlueprint animation adapter lands"));
	Schema->SetStringField(TEXT("UbergraphPages"), TEXT("array empty until WidgetBlueprint graph adapter lands"));
	Schema->SetStringField(TEXT("FunctionGraphs"), TEXT("array empty until WidgetBlueprint graph adapter lands"));
	Schema->SetStringField(TEXT("MacroGraphs"), TEXT("array empty until WidgetBlueprint graph adapter lands"));
	Schema->SetStringField(TEXT("Palette"), TEXT("object {Category:string}"));
	Schema->SetStringField(TEXT("EditorOptions"), TEXT("object {bCanCallInitializedWithoutPlayerContext:bool}"));
	Schema->SetStringField(TEXT("WidgetVariableGuids"), TEXT("object map variable name to GUID string"));
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

	const TSharedPtr<FJsonValue>* WidgetTreeValue = BodyObject->Values.Find(TEXT("WidgetTree"));
	const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Context.Asset);
	const FAssetDocumentCapabilityResult WidgetTreeResult =
		FWidgetBlueprintTreeAdapter::Preflight(WidgetBlueprint, WidgetTreeValue ? *WidgetTreeValue : nullptr);
	if (!WidgetTreeResult.bSuccess)
	{
		return WidgetTreeResult;
	}

	TSharedPtr<FJsonObject> ClassDefaults;
	const FAssetDocumentCapabilityResult ClassDefaultsResult =
		ParseObjectSection(BodyObject, TEXT("ClassDefaults"), ClassDefaults);
	if (!ClassDefaultsResult.bSuccess)
	{
		return ClassDefaultsResult;
	}
	const FAssetDocumentCapabilityResult ClassDefaultsPreflightResult =
		PreflightClassDefaults(Cast<UWidgetBlueprint>(Context.Asset), ClassDefaults);
	if (!ClassDefaultsPreflightResult.bSuccess)
	{
		return ClassDefaultsPreflightResult;
	}

	TSharedPtr<FJsonObject> WidgetVariableGuids;
	const FAssetDocumentCapabilityResult WidgetVariableGuidsResult =
		ParseObjectSection(BodyObject, TEXT("WidgetVariableGuids"), WidgetVariableGuids);
	if (!WidgetVariableGuidsResult.bSuccess)
	{
		return WidgetVariableGuidsResult;
	}
	TMap<FName, FGuid> ParsedGuids;
	const FAssetDocumentCapabilityResult GuidParseResult = ParseWidgetVariableGuids(WidgetVariableGuids, ParsedGuids);
	if (!GuidParseResult.bSuccess)
	{
		return GuidParseResult;
	}

	return FAssetDocumentCapabilityResult::Success();
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

	const FAssetDocumentCapabilityResult CurrentStateResult = FailOnUnsupportedCurrentRegions(WidgetBlueprint);
	if (!CurrentStateResult.bSuccess)
	{
		return CurrentStateResult;
	}

	TSharedPtr<FJsonObject> ClassDefaults;
	const FAssetDocumentCapabilityResult ClassDefaultsParseResult =
		ParseObjectSection(BodyObject, TEXT("ClassDefaults"), ClassDefaults);
	if (!ClassDefaultsParseResult.bSuccess)
	{
		return ClassDefaultsParseResult;
	}

	TSharedPtr<FJsonObject> Palette;
	const FAssetDocumentCapabilityResult PaletteParseResult =
		ParseObjectSection(BodyObject, TEXT("Palette"), Palette);
	if (!PaletteParseResult.bSuccess)
	{
		return PaletteParseResult;
	}

	TSharedPtr<FJsonObject> EditorOptions;
	const FAssetDocumentCapabilityResult EditorOptionsParseResult =
		ParseObjectSection(BodyObject, TEXT("EditorOptions"), EditorOptions);
	if (!EditorOptionsParseResult.bSuccess)
	{
		return EditorOptionsParseResult;
	}

	TSharedPtr<FJsonObject> WidgetVariableGuids;
	const FAssetDocumentCapabilityResult WidgetVariableGuidsParseResult =
		ParseObjectSection(BodyObject, TEXT("WidgetVariableGuids"), WidgetVariableGuids);
	if (!WidgetVariableGuidsParseResult.bSuccess)
	{
		return WidgetVariableGuidsParseResult;
	}
	TMap<FName, FGuid> DesiredGuids;
	const FAssetDocumentCapabilityResult DesiredGuidsResult = ParseWidgetVariableGuids(WidgetVariableGuids, DesiredGuids);
	if (!DesiredGuidsResult.bSuccess)
	{
		return DesiredGuidsResult;
	}

	const TSharedPtr<FJsonValue>* WidgetTreeValue = BodyObject->Values.Find(TEXT("WidgetTree"));
	bool bChanged = false;
	bool bWidgetTreeChanged = false;
	const FAssetDocumentCapabilityResult WidgetTreeResult =
		FWidgetBlueprintTreeAdapter::Apply(WidgetBlueprint, WidgetTreeValue ? *WidgetTreeValue : nullptr, &bWidgetTreeChanged);
	if (!WidgetTreeResult.bSuccess)
	{
		return WidgetTreeResult;
	}
	bChanged |= bWidgetTreeChanged;

	if (WidgetBlueprint->ParentClass.Get() != ParentClass)
	{
		WidgetBlueprint->Modify();
		WidgetBlueprint->ParentClass = ParentClass;
		bChanged = true;
	}

	if (bChanged)
	{
		SyncWidgetTreeVariableGuidsForCompile(WidgetBlueprint, Context.TargetAssetPath, DesiredGuids);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
		if (WidgetBlueprint->Status == BS_Error)
		{
			return BodyFailure(TEXT("Failed to compile WidgetBlueprint after applying Body contract"), TEXT("/Body"), TEXT("WidgetBlueprintCompileFailed"));
		}
	}
	else
	{
		SyncWidgetTreeVariableGuidsForCompile(WidgetBlueprint, Context.TargetAssetPath, DesiredGuids);
	}

	const FAssetDocumentCapabilityResult ClassDefaultsPreflightResult = PreflightClassDefaults(WidgetBlueprint, ClassDefaults);
	if (!ClassDefaultsPreflightResult.bSuccess)
	{
		return ClassDefaultsPreflightResult;
	}
	const FAssetDocumentCapabilityResult ClassDefaultsApplyResult = ApplyClassDefaults(WidgetBlueprint, ClassDefaults);
	if (!ClassDefaultsApplyResult.bSuccess)
	{
		return ClassDefaultsApplyResult;
	}

#if WITH_EDITORONLY_DATA
	if (Palette.IsValid())
	{
		FString Category;
		if (Palette->TryGetStringField(TEXT("Category"), Category))
		{
			WidgetBlueprint->Modify();
			WidgetBlueprint->PaletteCategory = Category;
			if (UUserWidget* GeneratedCDO = WidgetBlueprint->GeneratedClass
				? Cast<UUserWidget>(WidgetBlueprint->GeneratedClass->GetDefaultObject(false))
				: nullptr)
			{
				GeneratedCDO->Modify();
				GeneratedCDO->PaletteCategory = FText::FromString(Category);
			}
			FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
		}
	}

	if (EditorOptions.IsValid())
	{
		bool bCanCallInitializedWithoutPlayerContext = false;
		if (EditorOptions->TryGetBoolField(TEXT("bCanCallInitializedWithoutPlayerContext"), bCanCallInitializedWithoutPlayerContext))
		{
			WidgetBlueprint->Modify();
			WidgetBlueprint->bCanCallInitializedWithoutPlayerContext = bCanCallInitializedWithoutPlayerContext;
			if (UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WidgetBlueprint->GeneratedClass))
			{
				GeneratedClass->bCanCallInitializedWithoutPlayerContext = bCanCallInitializedWithoutPlayerContext;
			}
			FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
		}
	}
#endif

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
	OutBodyJson->SetObjectField(TEXT("ClassDefaults"), ExtractClassDefaults(WidgetBlueprint));
	TSharedRef<FJsonObject> WidgetTreeJson = MakeDefaultWidgetTree();
	const FAssetDocumentCapabilityResult WidgetTreeResult = FWidgetBlueprintTreeAdapter::Extract(WidgetBlueprint, WidgetTreeJson);
	if (!WidgetTreeResult.bSuccess)
	{
		return WidgetTreeResult;
	}
	OutBodyJson->SetObjectField(TEXT("WidgetTree"), WidgetTreeJson);
	OutBodyJson->SetArrayField(TEXT("Bindings"), {});
	OutBodyJson->SetArrayField(TEXT("Animations"), {});
	OutBodyJson->SetArrayField(TEXT("UbergraphPages"), {});
	OutBodyJson->SetArrayField(TEXT("FunctionGraphs"), {});
	OutBodyJson->SetArrayField(TEXT("MacroGraphs"), {});
	TSharedRef<FJsonObject> Palette = MakeShared<FJsonObject>();
#if WITH_EDITORONLY_DATA
	if (WidgetBlueprint && !WidgetBlueprint->PaletteCategory.IsEmpty())
	{
		Palette->SetStringField(TEXT("Category"), WidgetBlueprint->PaletteCategory);
	}
#endif
	OutBodyJson->SetObjectField(TEXT("Palette"), Palette);

	TSharedRef<FJsonObject> EditorOptions = MakeShared<FJsonObject>();
#if WITH_EDITORONLY_DATA
	if (WidgetBlueprint && WidgetBlueprint->bCanCallInitializedWithoutPlayerContext)
	{
		EditorOptions->SetBoolField(
			TEXT("bCanCallInitializedWithoutPlayerContext"),
			WidgetBlueprint->bCanCallInitializedWithoutPlayerContext);
	}
#endif
	OutBodyJson->SetObjectField(TEXT("EditorOptions"), EditorOptions);
	OutBodyJson->SetObjectField(TEXT("WidgetVariableGuids"), BuildWidgetVariableGuidsJson(WidgetBlueprint, Context.TargetAssetPath));

	TArray<FUnsupportedCurrentRegion> UnsupportedRegions;
	CollectUnsupportedCurrentRegions(WidgetBlueprint, UnsupportedRegions);
	for (const FUnsupportedCurrentRegion& UnsupportedRegion : UnsupportedRegions)
	{
		AddSkippedEvidence(OutBodyJson, UnsupportedRegion.Path, UnsupportedRegion.Message);
	}

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
		TSharedPtr<FJsonValue> CurrentValue = Current ? *Current : MakeShared<FJsonValueNull>();
		TSharedPtr<FJsonValue> DesiredValue = Desired ? *Desired : MakeShared<FJsonValueNull>();
		if (const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Context.Asset))
		{
			TArray<FUnsupportedCurrentRegion> UnsupportedRegions;
			CollectUnsupportedCurrentRegions(WidgetBlueprint, UnsupportedRegions);
			for (const FUnsupportedCurrentRegion& UnsupportedRegion : UnsupportedRegions)
			{
				if (UnsupportedRegion.Path == FString::Printf(TEXT("/Body/%s"), *BodyKey))
				{
					TSharedRef<FJsonObject> UnsupportedEvidence = MakeShared<FJsonObject>();
					UnsupportedEvidence->SetStringField(TEXT("Code"), TEXT("UnsupportedWidgetBlueprintRegion"));
					UnsupportedEvidence->SetStringField(TEXT("Message"), UnsupportedRegion.Message);
					CurrentValue = MakeShared<FJsonValueObject>(UnsupportedEvidence);
					break;
				}
			}
		}
		if (BodyKey == TEXT("WidgetTree"))
		{
			const TSharedPtr<FJsonValue>* DesiredWidgetTree = DesiredBody->Values.Find(TEXT("WidgetTree"));
			const FAssetDocumentCapabilityResult WidgetTreeDiffResult = FWidgetBlueprintTreeAdapter::Diff(
				Cast<UWidgetBlueprint>(Context.Asset),
				DesiredWidgetTree ? *DesiredWidgetTree : nullptr,
				OutDiffEntries);
			if (!WidgetTreeDiffResult.bSuccess)
			{
				return WidgetTreeDiffResult;
			}
		}
		else
		{
			if (BodyKey == TEXT("WidgetVariableGuids"))
			{
				TSharedPtr<FJsonObject> DesiredGuidsObject;
				if (DesiredValue.IsValid() && DesiredValue->Type == EJson::Object)
				{
					DesiredGuidsObject = DesiredValue->AsObject();
				}
				TMap<FName, FGuid> DesiredGuids;
				const FAssetDocumentCapabilityResult DesiredGuidsParseResult = ParseWidgetVariableGuids(DesiredGuidsObject, DesiredGuids);
				if (!DesiredGuidsParseResult.bSuccess)
				{
					return DesiredGuidsParseResult;
				}
				if (const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Context.Asset))
				{
					CurrentValue = MakeShared<FJsonValueObject>(BuildWidgetVariableGuidsJson(WidgetBlueprint, Context.TargetAssetPath));
					TSharedRef<FJsonObject> CanonicalDesiredGuids = BuildWidgetVariableGuidsJson(WidgetBlueprint, Context.TargetAssetPath, &DesiredGuids);
					DesiredValue = MakeShared<FJsonValueObject>(CanonicalDesiredGuids);
				}
			}
			const FString Status = JsonValueToComparableString(CurrentValue) == JsonValueToComparableString(DesiredValue)
				? TEXT("unchanged")
				: TEXT("changed");
			AddBodyDiffEntry(OutDiffEntries, FString::Printf(TEXT("/Body/%s"), *BodyKey), Status, CurrentValue, DesiredValue);
		}
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
			TSharedPtr<FJsonObject> Object;
			const FAssetDocumentCapabilityResult ObjectResult = RequireObjectSection(Pair.Value, FString::Printf(TEXT("/Body/%s"), *Pair.Key), Pair.Key, Object);
			if (!ObjectResult.bSuccess)
			{
				return ObjectResult;
			}
			if (Pair.Key == TEXT("WidgetVariableGuids"))
			{
				TMap<FName, FGuid> ParsedGuids;
				const FAssetDocumentCapabilityResult GuidParseResult = ParseWidgetVariableGuids(Object, ParsedGuids);
				if (!GuidParseResult.bSuccess)
				{
					return GuidParseResult;
				}
			}
		}
		else if (Pair.Key == TEXT("Variables"))
		{
			TSet<FName> VariableNames;
			const FAssetDocumentCapabilityResult VariableParseResult = CollectExplicitVariableNames(Pair.Value, VariableNames);
			if (!VariableParseResult.bSuccess)
			{
				return VariableParseResult;
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

	const FAssetDocumentCapabilityResult VariableConflictResult = ValidateVariableWidgetNameConflicts(BodyObject);
	if (!VariableConflictResult.bSuccess)
	{
		return VariableConflictResult;
	}

	if (const TSharedPtr<FJsonValue>* VariablesValue = BodyObject->Values.Find(TEXT("Variables")))
	{
		if (VariablesValue->IsValid() && (*VariablesValue)->Type == EJson::Array && (*VariablesValue)->AsArray().Num() > 0)
		{
			return BodyFailure(
				TEXT("Body.Variables is not supported yet for non-conflicting WidgetBlueprint variables"),
				TEXT("/Body/Variables"),
				TEXT("UnsupportedWidgetBlueprintRegion"));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated WidgetBlueprint Body scaffold"));
}
