// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/WidgetBlueprintAssetDocumentCapability.h"

#include "AssetDocumentPropertyAdapter.h"
#include "Profiles/WidgetBlueprintAnimationAdapter.h"
#include "Profiles/WidgetBlueprintBindingAdapter.h"
#include "Profiles/WidgetBlueprintGraphAdapter.h"
#include "Profiles/WidgetBlueprintTreeAdapter.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
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

bool IsGraphBodyKey(const FString& BodyKey)
{
	return BodyKey == TEXT("UbergraphPages")
		|| BodyKey == TEXT("FunctionGraphs")
		|| BodyKey == TEXT("MacroGraphs");
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

FAssetDocumentCapabilityResult ValidatePaletteSection(const TSharedPtr<FJsonObject>& Palette)
{
	if (!Palette.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Palette->Values)
	{
		if (Pair.Key != TEXT("Category"))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unknown Body.Palette field '%s'"), *Pair.Key),
				FString::Printf(TEXT("/Body/Palette/%s"), *Pair.Key),
				TEXT("UnknownPaletteField"));
		}
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String)
		{
			return BodyFailure(TEXT("Body.Palette.Category must be a string"), TEXT("/Body/Palette/Category"), TEXT("InvalidPaletteCategory"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateEditorOptionsSection(const TSharedPtr<FJsonObject>& EditorOptions)
{
	if (!EditorOptions.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : EditorOptions->Values)
	{
		if (Pair.Key != TEXT("bCanCallInitializedWithoutPlayerContext"))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unknown Body.EditorOptions field '%s'"), *Pair.Key),
				FString::Printf(TEXT("/Body/EditorOptions/%s"), *Pair.Key),
				TEXT("UnknownEditorOption"));
		}
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Boolean)
		{
			return BodyFailure(
				TEXT("Body.EditorOptions.bCanCallInitializedWithoutPlayerContext must be a boolean"),
				TEXT("/Body/EditorOptions/bCanCallInitializedWithoutPlayerContext"),
				TEXT("InvalidEditorOption"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
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

struct FScopedRootedObject
{
	explicit FScopedRootedObject(UObject* InObject)
		: Object(InObject)
	{
		if (Object)
		{
			Object->AddToRoot();
		}
	}

	~FScopedRootedObject()
	{
		if (Object && Object->IsRooted())
		{
			Object->RemoveFromRoot();
		}
	}

	UObject* Object = nullptr;
};

FAssetDocumentCapabilityResult PreflightClassDefaultsForParent(
	UWidgetBlueprint* WidgetBlueprint,
	UClass* DesiredParentClass,
	const TSharedPtr<FJsonObject>& ClassDefaults)
{
	if (!ClassDefaults.IsValid() || ClassDefaults->Values.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (!WidgetBlueprint)
	{
		return BodyFailure(TEXT("Body.ClassDefaults requires a WidgetBlueprint asset"), TEXT("/Body/ClassDefaults"), TEXT("UnsupportedAsset"));
	}

	if (WidgetBlueprint->ParentClass.Get() == DesiredParentClass)
	{
		return PreflightClassDefaults(WidgetBlueprint, ClassDefaults);
	}

	const FName ValidationName = MakeUniqueObjectName(
		GetTransientPackage(),
		UWidgetBlueprint::StaticClass(),
		TEXT("AssetDocumentWidgetBlueprintClassDefaultsPreflight"));
	UWidgetBlueprint* ValidationBlueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		DesiredParentClass,
		GetTransientPackage(),
		ValidationName,
		BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(),
		UWidgetBlueprintGeneratedClass::StaticClass(),
		TEXT("AssetDocumentWidgetBlueprintClassDefaultsPreflight")));
	if (!ValidationBlueprint)
	{
		return BodyFailure(TEXT("Failed to create transient WidgetBlueprint for ClassDefaults preflight"), TEXT("/Body/ClassDefaults"), TEXT("CreateValidationBlueprintFailed"));
	}

	FScopedRootedObject RootedValidationBlueprint(ValidationBlueprint);
	FKismetEditorUtilities::CompileBlueprint(ValidationBlueprint);
	if (ValidationBlueprint->Status == BS_Error)
	{
		return BodyFailure(TEXT("Failed to compile transient WidgetBlueprint for ClassDefaults preflight"), TEXT("/Body/ClassDefaults"), TEXT("WidgetBlueprintCompileFailed"));
	}

	return PreflightClassDefaults(ValidationBlueprint, ClassDefaults);
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

FAssetDocumentCapabilityResult ValidateWidgetVariableGuidKeys(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject,
	const TMap<FName, FGuid>& Guids)
{
	if (Guids.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSet<FName> SourceVariableNames;
	if (const TSharedPtr<FJsonValue>* WidgetTreeValue = BodyObject->Values.Find(TEXT("WidgetTree")))
	{
		const FAssetDocumentCapabilityResult WidgetNamesResult =
			FWidgetBlueprintTreeAdapter::CollectVariableWidgetNames(WidgetTreeValue ? *WidgetTreeValue : nullptr, SourceVariableNames);
		if (!WidgetNamesResult.bSuccess)
		{
			return WidgetNamesResult;
		}
	}
	else
	{
		CollectPublicWidgetVariableNames(Cast<UWidgetBlueprint>(Context.Asset), SourceVariableNames);
	}

	TSet<FGuid> SeenGuids;
	for (const TPair<FName, FGuid>& Pair : Guids)
	{
		if (!SourceVariableNames.Contains(Pair.Key))
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.WidgetVariableGuids.%s does not match a public variable widget or animation"), *Pair.Key.ToString()),
				FString::Printf(TEXT("/Body/WidgetVariableGuids/%s"), *Pair.Key.ToString()),
				TEXT("UnknownWidgetVariableGuid"));
		}
		if (SeenGuids.Contains(Pair.Value))
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.WidgetVariableGuids.%s duplicates another widget variable GUID"), *Pair.Key.ToString()),
				FString::Printf(TEXT("/Body/WidgetVariableGuids/%s"), *Pair.Key.ToString()),
				TEXT("DuplicateWidgetVariableGuid"));
		}
		SeenGuids.Add(Pair.Value);
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

struct FWidgetBlueprintVariableSpec
{
	FName Name;
	FEdGraphPinType Type;
	FString DefaultValue;
	TOptional<FString> Category;
	TOptional<FString> Tooltip;
};

FAssetDocumentCapabilityResult ReadPinType(const TSharedPtr<FJsonObject>& TypeObject, const FString& Path, FEdGraphPinType& OutPinType)
{
	OutPinType.ResetToDefaults();
	if (!TypeObject.IsValid())
	{
		return BodyFailure(TEXT("Variable Type must be an object"), Path, TEXT("InvalidVariableType"));
	}

	FString PinCategory;
	if (!TypeObject->TryGetStringField(TEXT("PinCategory"), PinCategory) || PinCategory.IsEmpty())
	{
		return BodyFailure(TEXT("Variable Type.PinCategory is required"), Path / TEXT("PinCategory"), TEXT("MissingPinCategory"));
	}

	auto SetScalar = [&OutPinType](FName Category)
	{
		OutPinType.PinCategory = Category;
		OutPinType.PinSubCategory = NAME_None;
	};

	if (PinCategory == UEdGraphSchema_K2::PC_Boolean.ToString())
	{
		SetScalar(UEdGraphSchema_K2::PC_Boolean);
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Byte.ToString())
	{
		SetScalar(UEdGraphSchema_K2::PC_Byte);
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Int.ToString())
	{
		SetScalar(UEdGraphSchema_K2::PC_Int);
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Int64.ToString())
	{
		SetScalar(UEdGraphSchema_K2::PC_Int64);
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Name.ToString())
	{
		SetScalar(UEdGraphSchema_K2::PC_Name);
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_String.ToString())
	{
		SetScalar(UEdGraphSchema_K2::PC_String);
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Text.ToString())
	{
		SetScalar(UEdGraphSchema_K2::PC_Text);
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Real.ToString())
	{
		FString PinSubCategory;
		if (!TypeObject->TryGetStringField(TEXT("PinSubCategory"), PinSubCategory) || PinSubCategory.IsEmpty())
		{
			return BodyFailure(TEXT("real variable Type.PinSubCategory must be float or double"), Path / TEXT("PinSubCategory"), TEXT("MissingRealPinSubCategory"));
		}
		if (PinSubCategory == UEdGraphSchema_K2::PC_Float.ToString())
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		}
		else if (PinSubCategory == UEdGraphSchema_K2::PC_Double.ToString())
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
		else
		{
			return BodyFailure(TEXT("real variable Type.PinSubCategory must be float or double"), Path / TEXT("PinSubCategory"), TEXT("UnsupportedRealPinSubCategory"));
		}
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Object.ToString() || PinCategory == UEdGraphSchema_K2::PC_Class.ToString())
	{
		FString ObjectClassPath;
		if (!TypeObject->TryGetStringField(TEXT("PinSubCategoryObject"), ObjectClassPath) || ObjectClassPath.IsEmpty())
		{
			return BodyFailure(TEXT("object/class variable Type.PinSubCategoryObject is required"), Path / TEXT("PinSubCategoryObject"), TEXT("MissingPinSubCategoryObject"));
		}

		UClass* ObjectClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ObjectClassPath);
		if (!ObjectClass)
		{
			return BodyFailure(
				FString::Printf(TEXT("Failed to resolve PinSubCategoryObject '%s'"), *ObjectClassPath),
				Path / TEXT("PinSubCategoryObject"),
				TEXT("UnresolvedPinSubCategoryObject"));
		}

		OutPinType.PinCategory = PinCategory == UEdGraphSchema_K2::PC_Object.ToString()
			? UEdGraphSchema_K2::PC_Object
			: UEdGraphSchema_K2::PC_Class;
		OutPinType.PinSubCategory = NAME_None;
		OutPinType.PinSubCategoryObject = ObjectClass;
	}
	else
	{
		return BodyFailure(
			FString::Printf(TEXT("Unsupported variable PinCategory '%s'"), *PinCategory),
			Path / TEXT("PinCategory"),
			TEXT("UnsupportedPinCategory"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseVariableSpecs(
	const TSharedPtr<FJsonObject>& BodyObject,
	TArray<FWidgetBlueprintVariableSpec>& OutVariables)
{
	OutVariables.Reset();
	const TSharedPtr<FJsonValue>* VariablesValue = BodyObject.IsValid() ? BodyObject->Values.Find(TEXT("Variables")) : nullptr;
	if (!VariablesValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (!VariablesValue->IsValid() || (*VariablesValue)->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if ((*VariablesValue)->Type != EJson::Array)
	{
		return BodyFailure(TEXT("Body.Variables must be an array when authored"), TEXT("/Body/Variables"), TEXT("InvalidBodySectionType"));
	}

	TSet<FName> SeenNames;
	const TArray<TSharedPtr<FJsonValue>>& Variables = (*VariablesValue)->AsArray();
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

		FWidgetBlueprintVariableSpec Spec;
		Spec.Name = FName(*Name);
		if (SeenNames.Contains(Spec.Name))
		{
			return BodyFailure(
				FString::Printf(TEXT("Duplicate Body.Variables Name '%s'"), *Name),
				Path / TEXT("Name"),
				TEXT("DuplicateVariableName"));
		}
		SeenNames.Add(Spec.Name);

		const TSharedPtr<FJsonObject>* TypeObject = nullptr;
		if (!VariableObject->TryGetObjectField(TEXT("Type"), TypeObject) || !TypeObject || !TypeObject->IsValid())
		{
			return BodyFailure(TEXT("Variable Type object is required"), Path / TEXT("Type"), TEXT("MissingVariableType"));
		}

		const FAssetDocumentCapabilityResult TypeResult = ReadPinType(*TypeObject, Path / TEXT("Type"), Spec.Type);
		if (!TypeResult.bSuccess)
		{
			return TypeResult;
		}

		VariableObject->TryGetStringField(TEXT("DefaultValue"), Spec.DefaultValue);

		FString Category;
		if (VariableObject->TryGetStringField(TEXT("Category"), Category))
		{
			Spec.Category = Category;
		}

		FString Tooltip;
		if (VariableObject->TryGetStringField(TEXT("Tooltip"), Tooltip))
		{
			Spec.Tooltip = Tooltip;
		}

		OutVariables.Add(MoveTemp(Spec));
	}

	return FAssetDocumentCapabilityResult::Success();
}

void CollectVariableSpecNames(const TArray<FWidgetBlueprintVariableSpec>& Variables, TSet<FName>& OutVariableNames)
{
	OutVariableNames.Reset();
	for (const FWidgetBlueprintVariableSpec& Variable : Variables)
	{
		OutVariableNames.Add(Variable.Name);
	}
}

FAssetDocumentCapabilityResult ValidateVariableWidgetNameConflicts(const TSharedRef<FJsonObject>& BodyObject)
{
	TArray<FWidgetBlueprintVariableSpec> Variables;
	const FAssetDocumentCapabilityResult VariablesResult = ParseVariableSpecs(BodyObject, Variables);
	if (!VariablesResult.bSuccess)
	{
		return VariablesResult;
	}

	TSet<FName> ExplicitVariableNames;
	CollectVariableSpecNames(Variables, ExplicitVariableNames);
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

FBPVariableDescription* FindNewVariable(UBlueprint* Blueprint, FName Name)
{
	return Blueprint ? Blueprint->NewVariables.FindByPredicate([Name](const FBPVariableDescription& Variable)
	{
		return Variable.VarName == Name;
	}) : nullptr;
}

const FBPVariableDescription* FindNewVariable(const UBlueprint* Blueprint, FName Name)
{
	return Blueprint ? Blueprint->NewVariables.FindByPredicate([Name](const FBPVariableDescription& Variable)
	{
		return Variable.VarName == Name;
	}) : nullptr;
}

void ApplyVariableMetadata(UBlueprint* Blueprint, const FWidgetBlueprintVariableSpec& Spec)
{
	if (FBPVariableDescription* Variable = FindNewVariable(Blueprint, Spec.Name))
	{
		if (Spec.Category.IsSet())
		{
			FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, Spec.Name, nullptr, FText::FromString(Spec.Category.GetValue()), true);
		}
		else
		{
			Variable->Category = FText::GetEmpty();
		}
		if (Spec.Tooltip.IsSet())
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Spec.Name, nullptr, FBlueprintMetadata::MD_Tooltip, Spec.Tooltip.GetValue());
		}
		else
		{
			Variable->RemoveMetaData(FBlueprintMetadata::MD_Tooltip);
		}
	}
}

bool ParentClassHasPropertyNamed(const UClass* ParentClass, FName Name)
{
	if (!ParentClass || Name.IsNone())
	{
		return false;
	}

	for (TFieldIterator<FProperty> PropertyIt(ParentClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		if (PropertyIt->GetFName() == Name)
		{
			return true;
		}
	}
	return false;
}

FAssetDocumentCapabilityResult ValidateVariablesAgainstParentClass(UClass* ParentClass, const TArray<FWidgetBlueprintVariableSpec>& Variables)
{
	for (const FWidgetBlueprintVariableSpec& Variable : Variables)
	{
		if (ParentClassHasPropertyNamed(ParentClass, Variable.Name))
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.Variables Name '%s' conflicts with parent class '%s'"), *Variable.Name.ToString(), ParentClass ? *ParentClass->GetPathName() : TEXT("")),
				FString::Printf(TEXT("/Body/Variables/%s"), *Variable.Name.ToString()),
				TEXT("ParentVariableNameConflict"));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyVariables(
	UBlueprint* Blueprint,
	const TArray<FWidgetBlueprintVariableSpec>& Variables,
	const TSet<FName>& ProtectedNames,
	bool& bOutChanged)
{
	bOutChanged = false;
	TSet<FName> DesiredNames;
	for (const FWidgetBlueprintVariableSpec& Spec : Variables)
	{
		DesiredNames.Add(Spec.Name);
	}

	TArray<FName> ExistingNames;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		ExistingNames.Add(Variable.VarName);
	}

	for (const FName& ExistingName : ExistingNames)
	{
		if (!DesiredNames.Contains(ExistingName) && !ProtectedNames.Contains(ExistingName))
		{
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, ExistingName);
			bOutChanged = true;
		}
	}

	for (const FWidgetBlueprintVariableSpec& Spec : Variables)
	{
		const FBPVariableDescription* Existing = FindNewVariable(Blueprint, Spec.Name);
		if (Existing && Existing->VarType != Spec.Type)
		{
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Spec.Name);
			Existing = nullptr;
			bOutChanged = true;
		}

		if (!Existing)
		{
			if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Spec.Name, Spec.Type))
			{
				return BodyFailure(
					FString::Printf(TEXT("Failed to add Blueprint variable '%s'"), *Spec.Name.ToString()),
					TEXT("/Body/Variables"),
					TEXT("AddVariableFailed"));
			}
			bOutChanged = true;
		}

		if (FBPVariableDescription* Mutable = FindNewVariable(Blueprint, Spec.Name))
		{
			const FString PreviousDefault = Mutable->DefaultValue;
			const FString PreviousCategory = Mutable->Category.ToString();
			const FString PreviousTooltip = Mutable->HasMetaData(FBlueprintMetadata::MD_Tooltip)
				? Mutable->GetMetaData(FBlueprintMetadata::MD_Tooltip)
				: FString();
			ApplyVariableMetadata(Blueprint, Spec);
			if (PreviousDefault != Spec.DefaultValue
				|| (Spec.Category.IsSet() && PreviousCategory != Spec.Category.GetValue())
				|| (!Spec.Category.IsSet() && !PreviousCategory.IsEmpty())
				|| (Spec.Tooltip.IsSet() && PreviousTooltip != Spec.Tooltip.GetValue())
				|| (!Spec.Tooltip.IsSet() && !PreviousTooltip.IsEmpty()))
			{
				bOutChanged = true;
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

TSharedRef<FJsonObject> PinTypeToJsonObject(const FEdGraphPinType& PinType)
{
	TSharedRef<FJsonObject> TypeObject = MakeShared<FJsonObject>();
	TypeObject->SetStringField(TEXT("PinCategory"), PinType.PinCategory.ToString());
	if (!PinType.PinSubCategory.IsNone())
	{
		TypeObject->SetStringField(TEXT("PinSubCategory"), PinType.PinSubCategory.ToString());
	}
	if (UObject* SubCategoryObject = PinType.PinSubCategoryObject.Get())
	{
		TypeObject->SetStringField(TEXT("PinSubCategoryObject"), SubCategoryObject->GetPathName());
	}
	return TypeObject;
}

bool IsSupportedAuthoredPinType(const FEdGraphPinType& PinType)
{
	if (PinType.IsContainer() || PinType.bIsReference || PinType.bIsWeakPointer || PinType.bIsConst)
	{
		return false;
	}

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_Int
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_Int64
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_Name
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_String
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_Text
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		return PinType.PinSubCategory.IsNone() && !PinType.PinSubCategoryObject.IsValid();
	}

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		return (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float || PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
			&& !PinType.PinSubCategoryObject.IsValid();
	}

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object || PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
	{
		return PinType.PinSubCategory.IsNone() && Cast<UClass>(PinType.PinSubCategoryObject.Get()) != nullptr;
	}

	return false;
}

bool AuthoredPinTypesDiffer(const FEdGraphPinType& Current, const FEdGraphPinType& Desired)
{
	if (!IsSupportedAuthoredPinType(Current) || !IsSupportedAuthoredPinType(Desired))
	{
		return true;
	}

	if (Current.PinCategory != Desired.PinCategory || Current.PinSubCategory != Desired.PinSubCategory)
	{
		return true;
	}

	return Current.PinSubCategoryObject.Get() != Desired.PinSubCategoryObject.Get();
}

bool AuthoredDefaultValuesDiffer(const FEdGraphPinType& PinType, const FString& Current, const FString& Desired)
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		double CurrentNumber = 0.0;
		double DesiredNumber = 0.0;
		if (LexTryParseString(CurrentNumber, *Current) && LexTryParseString(DesiredNumber, *Desired))
		{
			return !FMath::IsNearlyEqual(CurrentNumber, DesiredNumber);
		}
	}

	return Current != Desired;
}

FString ResolveVariableDefaultValue(const UBlueprint* Blueprint, const FBPVariableDescription& Variable)
{
	if (Blueprint && Blueprint->GeneratedClass)
	{
		UObject* GeneratedCDO = Blueprint->GeneratedClass->GetDefaultObject(false);
		FProperty* Property = GeneratedCDO ? FindFProperty<FProperty>(GeneratedCDO->GetClass(), Variable.VarName) : nullptr;
		if (GeneratedCDO && Property)
		{
			FString Value;
			FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<const uint8*>(GeneratedCDO), Value, GeneratedCDO, PPF_SerializedAsImportText);
			return Value;
		}
	}

	return Variable.DefaultValue;
}

void CollectPublicWidgetVariableNamesFromBlueprint(const UBlueprint* Blueprint, TSet<FName>& OutNames)
{
	OutNames.Reset();
	if (const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint))
	{
		CollectPublicWidgetVariableNames(WidgetBlueprint, OutNames);
	}
}

bool IsOrdinaryAuthoredVariable(const UBlueprint* Blueprint, const FBPVariableDescription& Variable)
{
	TSet<FName> PublicWidgetVariableNames;
	CollectPublicWidgetVariableNamesFromBlueprint(Blueprint, PublicWidgetVariableNames);
	return !PublicWidgetVariableNames.Contains(Variable.VarName) && IsSupportedAuthoredPinType(Variable.VarType);
}

TSharedRef<FJsonObject> VariableToJsonObject(const FBPVariableDescription& Variable, const UBlueprint* Blueprint = nullptr)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Name"), Variable.VarName.ToString());
	Object->SetObjectField(TEXT("Type"), PinTypeToJsonObject(Variable.VarType));
	Object->SetStringField(TEXT("DefaultValue"), ResolveVariableDefaultValue(Blueprint, Variable));
	if (!Variable.Category.IsEmpty())
	{
		Object->SetStringField(TEXT("Category"), Variable.Category.ToString());
	}
	if (Variable.HasMetaData(FBlueprintMetadata::MD_Tooltip))
	{
		Object->SetStringField(TEXT("Tooltip"), Variable.GetMetaData(FBlueprintMetadata::MD_Tooltip));
	}
	return Object;
}

TSharedRef<FJsonObject> VariableSpecToJsonObject(const FWidgetBlueprintVariableSpec& Variable)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Name"), Variable.Name.ToString());
	Object->SetObjectField(TEXT("Type"), PinTypeToJsonObject(Variable.Type));
	Object->SetStringField(TEXT("DefaultValue"), Variable.DefaultValue);
	if (Variable.Category.IsSet())
	{
		Object->SetStringField(TEXT("Category"), Variable.Category.GetValue());
	}
	if (Variable.Tooltip.IsSet())
	{
		Object->SetStringField(TEXT("Tooltip"), Variable.Tooltip.GetValue());
	}
	return Object;
}

TArray<TSharedPtr<FJsonValue>> ExtractVariableArray(const UBlueprint* Blueprint)
{
	TArray<const FBPVariableDescription*> Variables;
	if (Blueprint)
	{
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (IsOrdinaryAuthoredVariable(Blueprint, Variable))
			{
				Variables.Add(&Variable);
			}
		}
	}
	Variables.Sort([](const FBPVariableDescription& Left, const FBPVariableDescription& Right)
	{
		return Left.VarName.ToString() < Right.VarName.ToString();
	});

	TArray<TSharedPtr<FJsonValue>> Result;
	for (const FBPVariableDescription* Variable : Variables)
	{
		Result.Add(MakeShared<FJsonValueObject>(VariableToJsonObject(*Variable, Blueprint)));
	}
	return Result;
}

TSharedPtr<FJsonValue> CanonicalDesiredVariablesValue(const TArray<FWidgetBlueprintVariableSpec>& Variables)
{
	TArray<const FWidgetBlueprintVariableSpec*> SortedVariables;
	for (const FWidgetBlueprintVariableSpec& Variable : Variables)
	{
		SortedVariables.Add(&Variable);
	}
	SortedVariables.Sort([](const FWidgetBlueprintVariableSpec& Left, const FWidgetBlueprintVariableSpec& Right)
	{
		return Left.Name.ToString() < Right.Name.ToString();
	});

	TArray<TSharedPtr<FJsonValue>> Result;
	for (const FWidgetBlueprintVariableSpec* Variable : SortedVariables)
	{
		Result.Add(MakeShared<FJsonValueObject>(VariableSpecToJsonObject(*Variable)));
	}
	return MakeShared<FJsonValueArray>(Result);
}

bool VariablesSemanticallyDiffer(const UBlueprint* Blueprint, const TArray<FWidgetBlueprintVariableSpec>& DesiredVariables)
{
	TMap<FName, const FWidgetBlueprintVariableSpec*> DesiredByName;
	for (const FWidgetBlueprintVariableSpec& DesiredVariable : DesiredVariables)
	{
		DesiredByName.Add(DesiredVariable.Name, &DesiredVariable);
	}

	int32 CurrentOrdinaryCount = 0;
	if (Blueprint)
	{
		for (const FBPVariableDescription& CurrentVariable : Blueprint->NewVariables)
		{
			if (!IsOrdinaryAuthoredVariable(Blueprint, CurrentVariable))
			{
				continue;
			}

			++CurrentOrdinaryCount;
			const FWidgetBlueprintVariableSpec* const* DesiredVariablePtr = DesiredByName.Find(CurrentVariable.VarName);
			if (!DesiredVariablePtr || !*DesiredVariablePtr)
			{
				return true;
			}
			const FWidgetBlueprintVariableSpec& DesiredVariable = **DesiredVariablePtr;
			if (AuthoredPinTypesDiffer(CurrentVariable.VarType, DesiredVariable.Type))
			{
				return true;
			}
			if (AuthoredDefaultValuesDiffer(CurrentVariable.VarType, ResolveVariableDefaultValue(Blueprint, CurrentVariable), DesiredVariable.DefaultValue))
			{
				return true;
			}

			const FString CurrentCategory = CurrentVariable.Category.ToString();
			const FString DesiredCategory = DesiredVariable.Category.IsSet() ? DesiredVariable.Category.GetValue() : FString();
			if (CurrentCategory != DesiredCategory)
			{
				return true;
			}

			const FString CurrentTooltip = CurrentVariable.HasMetaData(FBlueprintMetadata::MD_Tooltip)
				? CurrentVariable.GetMetaData(FBlueprintMetadata::MD_Tooltip)
				: FString();
			const FString DesiredTooltip = DesiredVariable.Tooltip.IsSet() ? DesiredVariable.Tooltip.GetValue() : FString();
			if (CurrentTooltip != DesiredTooltip)
			{
				return true;
			}
		}
	}

	return CurrentOrdinaryCount != DesiredVariables.Num();
}

FAssetDocumentCapabilityResult ApplyVariableDefaultsToGeneratedClass(UBlueprint* Blueprint, const TArray<FWidgetBlueprintVariableSpec>& Variables)
{
	UClass* GeneratedClass = Blueprint ? Blueprint->GeneratedClass : nullptr;
	UObject* GeneratedCDO = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
	if (!GeneratedCDO)
	{
		return BodyFailure(TEXT("Failed to resolve Blueprint generated CDO for variable defaults"), TEXT("/Body/Variables"), TEXT("MissingGeneratedCDO"));
	}

	struct FPreviousDefault
	{
		FProperty* Property = nullptr;
		FString Value;
	};

	TArray<FPreviousDefault> PreviousDefaults;
	for (const FWidgetBlueprintVariableSpec& Variable : Variables)
	{
		FProperty* Property = FindFProperty<FProperty>(GeneratedCDO->GetClass(), Variable.Name);
		if (!Property)
		{
			return BodyFailure(
				FString::Printf(TEXT("Failed to resolve generated property for Blueprint variable '%s'"), *Variable.Name.ToString()),
				FString::Printf(TEXT("/Body/Variables/%s/DefaultValue"), *Variable.Name.ToString()),
				TEXT("MissingGeneratedVariableProperty"));
		}

		FPreviousDefault Previous;
		Previous.Property = Property;
		FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<const uint8*>(GeneratedCDO), Previous.Value, GeneratedCDO, PPF_SerializedAsImportText);
		PreviousDefaults.Add(Previous);
	}

	GeneratedCDO->Modify();
	for (int32 Index = 0; Index < Variables.Num(); ++Index)
	{
		const FWidgetBlueprintVariableSpec& Variable = Variables[Index];
		FProperty* Property = PreviousDefaults[Index].Property;
		if (!FBlueprintEditorUtils::PropertyValueFromString(Property, Variable.DefaultValue, reinterpret_cast<uint8*>(GeneratedCDO), GeneratedCDO, PPF_SerializedAsImportText))
		{
			for (const FPreviousDefault& Previous : PreviousDefaults)
			{
				if (Previous.Property)
				{
					FBlueprintEditorUtils::PropertyValueFromString(Previous.Property, Previous.Value, reinterpret_cast<uint8*>(GeneratedCDO), GeneratedCDO, PPF_SerializedAsImportText);
				}
			}
			return BodyFailure(
				FString::Printf(TEXT("Failed to parse default value '%s' for Blueprint variable '%s'"), *Variable.DefaultValue, *Variable.Name.ToString()),
				FString::Printf(TEXT("/Body/Variables/%s/DefaultValue"), *Variable.Name.ToString()),
				TEXT("InvalidVariableDefaultValue"));
		}

		if (FBPVariableDescription* MutableVariable = FindNewVariable(Blueprint, Variable.Name))
		{
			MutableVariable->DefaultValue = Variable.DefaultValue;
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

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

bool HasBindingEntries(const TSharedPtr<FJsonValue>& BindingsJson)
{
	if (!BindingsJson.IsValid() || BindingsJson->Type == EJson::Null)
	{
		return false;
	}
	return BindingsJson->Type == EJson::Array && BindingsJson->AsArray().Num() > 0;
}

bool HasAnimationEntries(const TSharedPtr<FJsonValue>& AnimationsJson)
{
	if (!AnimationsJson.IsValid() || AnimationsJson->Type == EJson::Null)
	{
		return false;
	}
	return AnimationsJson->Type == EJson::Array && AnimationsJson->AsArray().Num() > 0;
}

struct FDesiredWidgetBlueprintPreflightScratch
{
	~FDesiredWidgetBlueprintPreflightScratch()
	{
		if (Blueprint && Blueprint->IsRooted())
		{
			Blueprint->RemoveFromRoot();
		}
	}

	void Set(UWidgetBlueprint* InBlueprint)
	{
		if (Blueprint && Blueprint->IsRooted())
		{
			Blueprint->RemoveFromRoot();
		}
		Blueprint = InBlueprint;
		if (Blueprint)
		{
			Blueprint->AddToRoot();
		}
	}

	UWidgetBlueprint* Blueprint = nullptr;
};

FAssetDocumentCapabilityResult BuildDesiredWidgetBlueprintPreflightScratch(
	const FString& TargetAssetPath,
	UClass* ParentClass,
	const TSharedPtr<FJsonValue>& WidgetTreeJson,
	const TArray<FWidgetBlueprintVariableSpec>& VariableSpecs,
	const TMap<FName, FGuid>& DesiredGuids,
	const TSharedPtr<FJsonObject>& ClassDefaults,
	const FString& FailurePath,
	FDesiredWidgetBlueprintPreflightScratch& OutScratch)
{
	const FName ValidationName = MakeUniqueObjectName(
		GetTransientPackage(),
		UWidgetBlueprint::StaticClass(),
		TEXT("AssetDocumentWidgetBlueprintDesiredPreflight"));
	UWidgetBlueprint* ValidationBlueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		GetTransientPackage(),
		ValidationName,
		BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(),
		UWidgetBlueprintGeneratedClass::StaticClass(),
		TEXT("AssetDocumentWidgetBlueprintDesiredPreflight")));
	if (!ValidationBlueprint)
	{
		return BodyFailure(TEXT("Failed to create transient WidgetBlueprint for desired-state preflight"), FailurePath, TEXT("CreateValidationBlueprintFailed"));
	}
	OutScratch.Set(ValidationBlueprint);

	bool bScratchWidgetTreeChanged = false;
	const FAssetDocumentCapabilityResult WidgetTreeResult =
		FWidgetBlueprintTreeAdapter::Apply(ValidationBlueprint, WidgetTreeJson, &bScratchWidgetTreeChanged);
	if (!WidgetTreeResult.bSuccess)
	{
		return WidgetTreeResult;
	}

	TSet<FName> ProtectedWidgetVariableNames;
	CollectPublicWidgetVariableNames(ValidationBlueprint, ProtectedWidgetVariableNames);
	bool bScratchVariablesChanged = false;
	const FAssetDocumentCapabilityResult VariableApplyResult =
		ApplyVariables(ValidationBlueprint, VariableSpecs, ProtectedWidgetVariableNames, bScratchVariablesChanged);
	if (!VariableApplyResult.bSuccess)
	{
		return VariableApplyResult;
	}

	SyncWidgetTreeVariableGuidsForCompile(ValidationBlueprint, TargetAssetPath, DesiredGuids);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ValidationBlueprint);
	FKismetEditorUtilities::CompileBlueprint(ValidationBlueprint);
	if (ValidationBlueprint->Status == BS_Error)
	{
		return BodyFailure(TEXT("Failed to compile transient WidgetBlueprint for desired-state preflight"), FailurePath, TEXT("WidgetBlueprintCompileFailed"));
	}

	const FAssetDocumentCapabilityResult VariableDefaultsResult = ApplyVariableDefaultsToGeneratedClass(ValidationBlueprint, VariableSpecs);
	if (!VariableDefaultsResult.bSuccess)
	{
		return VariableDefaultsResult;
	}

	const FAssetDocumentCapabilityResult ClassDefaultsPreflightResult = PreflightClassDefaults(ValidationBlueprint, ClassDefaults);
	if (!ClassDefaultsPreflightResult.bSuccess)
	{
		return ClassDefaultsPreflightResult;
	}
	const FAssetDocumentCapabilityResult ClassDefaultsApplyResult = ApplyClassDefaults(ValidationBlueprint, ClassDefaults);
	if (!ClassDefaultsApplyResult.bSuccess)
	{
		return ClassDefaultsApplyResult;
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult PreflightGraphsAgainstDesiredWidgetBlueprint(
	FAssetDocumentCapabilityContext& SourceContext,
	const FString& TargetAssetPath,
	UClass* ParentClass,
	const TSharedRef<FJsonObject>& DesiredBody,
	const TSharedPtr<FJsonValue>& WidgetTreeJson,
	const TArray<FWidgetBlueprintVariableSpec>& VariableSpecs,
	const TMap<FName, FGuid>& DesiredGuids,
	const TSharedPtr<FJsonObject>& ClassDefaults)
{
	FDesiredWidgetBlueprintPreflightScratch Scratch;
	const FAssetDocumentCapabilityResult ScratchResult = BuildDesiredWidgetBlueprintPreflightScratch(
		TargetAssetPath,
		ParentClass,
		WidgetTreeJson,
		VariableSpecs,
		DesiredGuids,
		ClassDefaults,
		TEXT("/Body"),
		Scratch);
	if (!ScratchResult.bSuccess)
	{
		return ScratchResult;
	}

	return FWidgetBlueprintGraphAdapter().PreflightRegions(SourceContext, DesiredBody, Scratch.Blueprint);
}

FAssetDocumentCapabilityResult PreflightBindingsAgainstDesiredWidgetBlueprint(
	const FAssetDocumentCapabilityContext& SourceContext,
	const FString& TargetAssetPath,
	UClass* ParentClass,
	const TSharedRef<FJsonObject>& DesiredBody,
	const TSharedPtr<FJsonValue>& WidgetTreeJson,
	const TArray<FWidgetBlueprintVariableSpec>& VariableSpecs,
	const TMap<FName, FGuid>& DesiredGuids,
	const TSharedPtr<FJsonObject>& ClassDefaults,
	const TSharedPtr<FJsonValue>& BindingsJson)
{
	const FAssetDocumentCapabilityResult BindingShapeResult = FWidgetBlueprintBindingAdapter::Validate(BindingsJson);
	if (!BindingShapeResult.bSuccess)
	{
		return BindingShapeResult;
	}
	if (!HasBindingEntries(BindingsJson))
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FDesiredWidgetBlueprintPreflightScratch Scratch;
	const FAssetDocumentCapabilityResult ScratchResult = BuildDesiredWidgetBlueprintPreflightScratch(
		TargetAssetPath,
		ParentClass,
		WidgetTreeJson,
		VariableSpecs,
		DesiredGuids,
		ClassDefaults,
		TEXT("/Body/Bindings"),
		Scratch);
	if (!ScratchResult.bSuccess)
	{
		return ScratchResult;
	}

	FAssetDocumentCapabilityContext ScratchContext = SourceContext;
	ScratchContext.Asset = Scratch.Blueprint;
	ScratchContext.AssetClass = UWidgetBlueprint::StaticClass();
	bool bScratchGraphsChanged = false;
	const FAssetDocumentCapabilityResult GraphApplyResult =
		FWidgetBlueprintGraphAdapter().ApplyRegions(ScratchContext, DesiredBody, bScratchGraphsChanged);
	if (!GraphApplyResult.bSuccess)
	{
		return GraphApplyResult;
	}

	return FWidgetBlueprintBindingAdapter::Preflight(Scratch.Blueprint, BindingsJson);
}

FAssetDocumentCapabilityResult PreflightAnimationsAgainstDesiredWidgetBlueprint(
	const FString& TargetAssetPath,
	UClass* ParentClass,
	const TSharedPtr<FJsonValue>& WidgetTreeJson,
	const TArray<FWidgetBlueprintVariableSpec>& VariableSpecs,
	const TMap<FName, FGuid>& DesiredGuids,
	const TSharedPtr<FJsonObject>& ClassDefaults,
	const TSharedPtr<FJsonValue>& AnimationsJson)
{
	const FAssetDocumentCapabilityResult AnimationShapeResult = FWidgetBlueprintAnimationAdapter::Validate(AnimationsJson);
	if (!AnimationShapeResult.bSuccess)
	{
		return AnimationShapeResult;
	}
	if (!HasAnimationEntries(AnimationsJson))
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FDesiredWidgetBlueprintPreflightScratch Scratch;
	const FAssetDocumentCapabilityResult ScratchResult = BuildDesiredWidgetBlueprintPreflightScratch(
		TargetAssetPath,
		ParentClass,
		WidgetTreeJson,
		VariableSpecs,
		DesiredGuids,
		ClassDefaults,
		TEXT("/Body/Animations"),
		Scratch);
	if (!ScratchResult.bSuccess)
	{
		return ScratchResult;
	}

	return FWidgetBlueprintAnimationAdapter::Preflight(Scratch.Blueprint, AnimationsJson);
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
	return {TEXT("WidgetBlueprintBody"), TEXT("WidgetBlueprintTree"), TEXT("WidgetBlueprintBindings"), TEXT("WidgetBlueprintAnimations"), TEXT("WidgetBlueprintEmptyAssetContract")};
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
	Schema->SetStringField(TEXT("Bindings"), TEXT("array of {Widget:string, Property:string, Kind:Function|Property, Function?:string, SourcePath?:string[]}"));
	Schema->SetStringField(TEXT("Animations"), TEXT("array of {Name, FrameRate, PlaybackRange, Tracks:[{Widget, Property, Type:Float|Transform, Keys|Channels}]}"));
	Schema->SetStringField(TEXT("UbergraphPages"), TEXT("array of UBlueprintGraph objects"));
	Schema->SetStringField(TEXT("FunctionGraphs"), TEXT("array of UBlueprintGraph objects"));
	Schema->SetStringField(TEXT("MacroGraphs"), TEXT("array of UBlueprintGraph objects"));
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

	UClass* ParentClass = nullptr;
	const FAssetDocumentCapabilityResult ParentClassResult = ResolveUserWidgetParentClass(BodyObject->Values.FindChecked(TEXT("ParentClass")), ParentClass);
	if (!ParentClassResult.bSuccess)
	{
		return ParentClassResult;
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
		PreflightClassDefaultsForParent(Cast<UWidgetBlueprint>(Context.Asset), ParentClass, ClassDefaults);
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

	TArray<FWidgetBlueprintVariableSpec> VariableSpecs;
	const FAssetDocumentCapabilityResult VariableParseResult = ParseVariableSpecs(BodyObject, VariableSpecs);
	if (!VariableParseResult.bSuccess)
	{
		return VariableParseResult;
	}

	const FAssetDocumentCapabilityResult GraphPreflightResult =
		PreflightGraphsAgainstDesiredWidgetBlueprint(
			Context,
			Context.TargetAssetPath,
			ParentClass,
			BodyObject.ToSharedRef(),
			WidgetTreeValue ? *WidgetTreeValue : nullptr,
			VariableSpecs,
			ParsedGuids,
			ClassDefaults);
	if (!GraphPreflightResult.bSuccess)
	{
		return GraphPreflightResult;
	}

	const TSharedPtr<FJsonValue>* BindingsValue = BodyObject->Values.Find(TEXT("Bindings"));
	const FAssetDocumentCapabilityResult BindingPreflightResult = PreflightBindingsAgainstDesiredWidgetBlueprint(
		Context,
		Context.TargetAssetPath,
		ParentClass,
		BodyObject.ToSharedRef(),
		WidgetTreeValue ? *WidgetTreeValue : nullptr,
		VariableSpecs,
		ParsedGuids,
		ClassDefaults,
		BindingsValue ? *BindingsValue : nullptr);
	if (!BindingPreflightResult.bSuccess)
	{
		return BindingPreflightResult;
	}

	const TSharedPtr<FJsonValue>* AnimationsValue = BodyObject->Values.Find(TEXT("Animations"));
	const FAssetDocumentCapabilityResult AnimationPreflightResult = PreflightAnimationsAgainstDesiredWidgetBlueprint(
		Context.TargetAssetPath,
		ParentClass,
		WidgetTreeValue ? *WidgetTreeValue : nullptr,
		VariableSpecs,
		ParsedGuids,
		ClassDefaults,
		AnimationsValue ? *AnimationsValue : nullptr);
	if (!AnimationPreflightResult.bSuccess)
	{
		return AnimationPreflightResult;
	}

	const FAssetDocumentCapabilityResult CurrentAnimationResult =
		FWidgetBlueprintAnimationAdapter::CheckForUnsupportedCurrentTracks(Cast<UWidgetBlueprint>(Context.Asset));
	if (!CurrentAnimationResult.bSuccess)
	{
		return CurrentAnimationResult;
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
	const FAssetDocumentCapabilityResult CurrentAnimationResult =
		FWidgetBlueprintAnimationAdapter::CheckForUnsupportedCurrentTracks(WidgetBlueprint);
	if (!CurrentAnimationResult.bSuccess)
	{
		return CurrentAnimationResult;
	}

	TSharedPtr<FJsonObject> ClassDefaults;
	const FAssetDocumentCapabilityResult ClassDefaultsParseResult =
		ParseObjectSection(BodyObject, TEXT("ClassDefaults"), ClassDefaults);
	if (!ClassDefaultsParseResult.bSuccess)
	{
		return ClassDefaultsParseResult;
	}
	const FAssetDocumentCapabilityResult TargetClassDefaultsPreflightResult =
		PreflightClassDefaultsForParent(WidgetBlueprint, ParentClass, ClassDefaults);
	if (!TargetClassDefaultsPreflightResult.bSuccess)
	{
		return TargetClassDefaultsPreflightResult;
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

	TArray<FWidgetBlueprintVariableSpec> VariableSpecs;
	const FAssetDocumentCapabilityResult VariableParseResult = ParseVariableSpecs(BodyObject, VariableSpecs);
	if (!VariableParseResult.bSuccess)
	{
		return VariableParseResult;
	}

	const TSharedPtr<FJsonValue>* WidgetTreeValue = BodyObject->Values.Find(TEXT("WidgetTree"));
	const FAssetDocumentCapabilityResult GraphPreflightResult =
		PreflightGraphsAgainstDesiredWidgetBlueprint(
			Context,
			Context.TargetAssetPath,
			ParentClass,
			BodyObject.ToSharedRef(),
			WidgetTreeValue ? *WidgetTreeValue : nullptr,
			VariableSpecs,
			DesiredGuids,
			ClassDefaults);
	if (!GraphPreflightResult.bSuccess)
	{
		return GraphPreflightResult;
	}

	const TSharedPtr<FJsonValue>* BindingsValue = BodyObject->Values.Find(TEXT("Bindings"));
	const FAssetDocumentCapabilityResult BindingPreflightResult = PreflightBindingsAgainstDesiredWidgetBlueprint(
		Context,
		Context.TargetAssetPath,
		ParentClass,
		BodyObject.ToSharedRef(),
		WidgetTreeValue ? *WidgetTreeValue : nullptr,
		VariableSpecs,
		DesiredGuids,
		ClassDefaults,
		BindingsValue ? *BindingsValue : nullptr);
	if (!BindingPreflightResult.bSuccess)
	{
		return BindingPreflightResult;
	}

	const TSharedPtr<FJsonValue>* AnimationsValue = BodyObject->Values.Find(TEXT("Animations"));
	const FAssetDocumentCapabilityResult AnimationPreflightResult = PreflightAnimationsAgainstDesiredWidgetBlueprint(
		Context.TargetAssetPath,
		ParentClass,
		WidgetTreeValue ? *WidgetTreeValue : nullptr,
		VariableSpecs,
		DesiredGuids,
		ClassDefaults,
		AnimationsValue ? *AnimationsValue : nullptr);
	if (!AnimationPreflightResult.bSuccess)
	{
		return AnimationPreflightResult;
	}

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

	TSet<FName> ProtectedWidgetVariableNames;
	CollectPublicWidgetVariableNames(WidgetBlueprint, ProtectedWidgetVariableNames);
	bool bVariablesChanged = false;
	const FAssetDocumentCapabilityResult VariableApplyResult =
		ApplyVariables(WidgetBlueprint, VariableSpecs, ProtectedWidgetVariableNames, bVariablesChanged);
	if (!VariableApplyResult.bSuccess)
	{
		return VariableApplyResult;
	}
	bChanged |= bVariablesChanged;

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

	bool bGraphsChanged = false;
	const FAssetDocumentCapabilityResult GraphApplyResult =
		FWidgetBlueprintGraphAdapter().ApplyRegions(Context, BodyObject.ToSharedRef(), bGraphsChanged);
	if (!GraphApplyResult.bSuccess)
	{
		return GraphApplyResult;
	}
	bChanged |= bGraphsChanged;

	const FAssetDocumentCapabilityResult VariableDefaultsResult = ApplyVariableDefaultsToGeneratedClass(WidgetBlueprint, VariableSpecs);
	if (!VariableDefaultsResult.bSuccess)
	{
		return VariableDefaultsResult;
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

	bool bBindingsChanged = false;
	const FAssetDocumentCapabilityResult BindingsApplyResult =
		FWidgetBlueprintBindingAdapter::Apply(WidgetBlueprint, BindingsValue ? *BindingsValue : nullptr, &bBindingsChanged);
	if (!BindingsApplyResult.bSuccess)
	{
		return BindingsApplyResult;
	}
	if (bBindingsChanged)
	{
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
		if (WidgetBlueprint->Status == BS_Error)
		{
			return BodyFailure(TEXT("Failed to compile WidgetBlueprint after applying Body.Bindings"), TEXT("/Body/Bindings"), TEXT("WidgetBlueprintCompileFailed"));
		}
	}

	bool bAnimationsChanged = false;
	const FAssetDocumentCapabilityResult AnimationsApplyResult =
		FWidgetBlueprintAnimationAdapter::Apply(WidgetBlueprint, AnimationsValue ? *AnimationsValue : nullptr, &bAnimationsChanged);
	if (!AnimationsApplyResult.bSuccess)
	{
		return AnimationsApplyResult;
	}
	if (bAnimationsChanged)
	{
		SyncWidgetTreeVariableGuidsForCompile(WidgetBlueprint, Context.TargetAssetPath, DesiredGuids);
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
		if (WidgetBlueprint->Status == BS_Error)
		{
			return BodyFailure(TEXT("Failed to compile WidgetBlueprint after applying Body.Animations"), TEXT("/Body/Animations"), TEXT("WidgetBlueprintCompileFailed"));
		}
	}

#if WITH_EDITORONLY_DATA
	if (Palette.IsValid())
	{
		FString Category;
		Palette->TryGetStringField(TEXT("Category"), Category);
		WidgetBlueprint->Modify();
		WidgetBlueprint->PaletteCategory = Category;
		if (UUserWidget* GeneratedCDO = WidgetBlueprint->GeneratedClass
			? Cast<UUserWidget>(WidgetBlueprint->GeneratedClass->GetDefaultObject(false))
			: nullptr)
		{
			GeneratedCDO->Modify();
			GeneratedCDO->PaletteCategory = Category.IsEmpty() ? FText::GetEmpty() : FText::FromString(Category);
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
	}

	if (EditorOptions.IsValid())
	{
		bool bCanCallInitializedWithoutPlayerContext = false;
		EditorOptions->TryGetBoolField(TEXT("bCanCallInitializedWithoutPlayerContext"), bCanCallInitializedWithoutPlayerContext);
		WidgetBlueprint->Modify();
		WidgetBlueprint->bCanCallInitializedWithoutPlayerContext = bCanCallInitializedWithoutPlayerContext;
		if (UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WidgetBlueprint->GeneratedClass))
		{
			GeneratedClass->bCanCallInitializedWithoutPlayerContext = bCanCallInitializedWithoutPlayerContext;
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
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
	OutBodyJson->SetArrayField(TEXT("Variables"), ExtractVariableArray(WidgetBlueprint));
	OutBodyJson->SetObjectField(TEXT("ClassDefaults"), ExtractClassDefaults(WidgetBlueprint));
	TSharedRef<FJsonObject> WidgetTreeJson = MakeDefaultWidgetTree();
	const FAssetDocumentCapabilityResult WidgetTreeResult = FWidgetBlueprintTreeAdapter::Extract(WidgetBlueprint, WidgetTreeJson);
	if (!WidgetTreeResult.bSuccess)
	{
		return WidgetTreeResult;
	}
	OutBodyJson->SetObjectField(TEXT("WidgetTree"), WidgetTreeJson);
	TArray<TSharedPtr<FJsonValue>> BindingValues;
	const FAssetDocumentCapabilityResult BindingsResult = FWidgetBlueprintBindingAdapter::Extract(WidgetBlueprint, BindingValues);
	if (!BindingsResult.bSuccess)
	{
		return BindingsResult;
	}
	OutBodyJson->SetArrayField(TEXT("Bindings"), BindingValues);
	TArray<TSharedPtr<FJsonValue>> AnimationValues;
	const FAssetDocumentCapabilityResult AnimationsResult = FWidgetBlueprintAnimationAdapter::Extract(WidgetBlueprint, AnimationValues);
	if (!AnimationsResult.bSuccess)
	{
		return AnimationsResult;
	}
	OutBodyJson->SetArrayField(TEXT("Animations"), AnimationValues);
	const FAssetDocumentCapabilityResult GraphExtractResult =
		FWidgetBlueprintGraphAdapter().ExtractRegions(Context, OutBodyJson);
	if (!GraphExtractResult.bSuccess)
	{
		return GraphExtractResult;
	}
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

	bool bDiffedGraphRegions = false;
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
		else if (BodyKey == TEXT("Bindings"))
		{
			TSharedPtr<FJsonValue> CanonicalDesiredBindings;
			const FAssetDocumentCapabilityResult BindingsDesiredResult = FWidgetBlueprintBindingAdapter::CanonicalizeDesired(
				Desired ? *Desired : nullptr,
				CanonicalDesiredBindings);
			if (!BindingsDesiredResult.bSuccess)
			{
				return BindingsDesiredResult;
			}
			DesiredValue = CanonicalDesiredBindings;
			const FString Status = JsonValueToComparableString(CurrentValue) == JsonValueToComparableString(DesiredValue)
				? TEXT("unchanged")
				: TEXT("changed");
			AddBodyDiffEntry(OutDiffEntries, FString::Printf(TEXT("/Body/%s"), *BodyKey), Status, CurrentValue, DesiredValue);
		}
		else if (BodyKey == TEXT("Animations"))
		{
			const FAssetDocumentCapabilityResult AnimationDiffResult = FWidgetBlueprintAnimationAdapter::Diff(
				Cast<UWidgetBlueprint>(Context.Asset),
				Desired ? *Desired : nullptr,
				OutDiffEntries);
			if (!AnimationDiffResult.bSuccess)
			{
				return AnimationDiffResult;
			}
		}
		else if (IsGraphBodyKey(BodyKey))
		{
			if (!bDiffedGraphRegions)
			{
				const FAssetDocumentCapabilityResult GraphDiffResult =
					FWidgetBlueprintGraphAdapter().DiffRegions(Context, DesiredBody.ToSharedRef(), OutDiffEntries);
				if (!GraphDiffResult.bSuccess)
				{
					return GraphDiffResult;
				}
				bDiffedGraphRegions = true;
			}
			continue;
		}
		else
		{
			if (BodyKey == TEXT("Variables"))
			{
				TArray<FWidgetBlueprintVariableSpec> DesiredVariables;
				const FAssetDocumentCapabilityResult DesiredVariablesResult = ParseVariableSpecs(DesiredBody, DesiredVariables);
				if (!DesiredVariablesResult.bSuccess)
				{
					return DesiredVariablesResult;
				}
				DesiredValue = CanonicalDesiredVariablesValue(DesiredVariables);
				const FString Status = VariablesSemanticallyDiffer(Cast<UBlueprint>(Context.Asset), DesiredVariables)
					? TEXT("changed")
					: TEXT("unchanged");
				AddBodyDiffEntry(OutDiffEntries, FString::Printf(TEXT("/Body/%s"), *BodyKey), Status, CurrentValue, DesiredValue);
				continue;
			}
			else if (BodyKey == TEXT("WidgetVariableGuids"))
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
				const FAssetDocumentCapabilityResult GuidKeyResult = ValidateWidgetVariableGuidKeys(Context, BodyObject, ParsedGuids);
				if (!GuidKeyResult.bSuccess)
				{
					return GuidKeyResult;
				}
			}
			else if (Pair.Key == TEXT("Palette"))
			{
				const FAssetDocumentCapabilityResult PaletteResult = ValidatePaletteSection(Object);
				if (!PaletteResult.bSuccess)
				{
					return PaletteResult;
				}
			}
			else if (Pair.Key == TEXT("EditorOptions"))
			{
				const FAssetDocumentCapabilityResult EditorOptionsResult = ValidateEditorOptionsSection(Object);
				if (!EditorOptionsResult.bSuccess)
				{
					return EditorOptionsResult;
				}
			}
		}
		else if (Pair.Key == TEXT("Variables"))
		{
			TArray<FWidgetBlueprintVariableSpec> Variables;
			const FAssetDocumentCapabilityResult VariableParseResult = ParseVariableSpecs(BodyObject, Variables);
			if (!VariableParseResult.bSuccess)
			{
				return VariableParseResult;
			}
		}
		else if (Pair.Key == TEXT("Bindings"))
		{
			const FAssetDocumentCapabilityResult BindingsResult = FWidgetBlueprintBindingAdapter::Validate(Pair.Value);
			if (!BindingsResult.bSuccess)
			{
				return BindingsResult;
			}
		}
		else if (Pair.Key == TEXT("Animations"))
		{
			const FAssetDocumentCapabilityResult AnimationsResult = FWidgetBlueprintAnimationAdapter::Validate(Pair.Value);
			if (!AnimationsResult.bSuccess)
			{
				return AnimationsResult;
			}
		}
		else if (IsGraphBodyKey(Pair.Key))
		{
			continue;
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

	const FAssetDocumentCapabilityResult GraphValidateResult =
		FWidgetBlueprintGraphAdapter().ValidateRegions(Context, BodyObject);
	if (!GraphValidateResult.bSuccess)
	{
		return GraphValidateResult;
	}

	const FAssetDocumentCapabilityResult VariableConflictResult = ValidateVariableWidgetNameConflicts(BodyObject);
	if (!VariableConflictResult.bSuccess)
	{
		return VariableConflictResult;
	}

	TArray<FWidgetBlueprintVariableSpec> Variables;
	const FAssetDocumentCapabilityResult VariableParseResult = ParseVariableSpecs(BodyObject, Variables);
	if (!VariableParseResult.bSuccess)
	{
		return VariableParseResult;
	}
	UClass* ParentClass = nullptr;
	const FAssetDocumentCapabilityResult ParentClassResult = ResolveUserWidgetParentClass(BodyObject->Values.FindChecked(TEXT("ParentClass")), ParentClass);
	if (!ParentClassResult.bSuccess)
	{
		return ParentClassResult;
	}
	const FAssetDocumentCapabilityResult ParentVariableResult = ValidateVariablesAgainstParentClass(ParentClass, Variables);
	if (!ParentVariableResult.bSuccess)
	{
		return ParentVariableResult;
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated WidgetBlueprint Body scaffold"));
}
