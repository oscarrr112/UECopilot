// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/BlueprintAssetDocumentCommon.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentPropertyAdapter.h"
#include "Profiles/UBlueprintGraphRegionAdapter.h"
#include "Regions/AssetDocumentIdentityArrayDiffHelper.h"

#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

FAssetDocumentCapabilityResult ApplyUBlueprintGraphRegions(
	FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& DesiredBody,
	bool& bOutChanged);

namespace
{
FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

bool IsCommonRegion(FName RegionId)
{
	return RegionId == TEXT("Body.ImplementedInterfaces")
		|| RegionId == TEXT("Body.Variables")
		|| RegionId == TEXT("Body.ClassDefaults")
		|| RegionId == TEXT("Body.UbergraphPages");
}

FString RegionBodyKey(const FAssetDocumentRegionContext& Context)
{
	const FString RegionId = Context.RegionId.ToString();
	FString Left;
	FString Right;
	return RegionId.Split(TEXT("."), &Left, &Right) ? Right : RegionId;
}

TSharedRef<FJsonObject> MakeBodyObject(const FString& BodyKey, const TSharedPtr<FJsonValue>& Value)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(BodyKey, Value.IsValid() ? Value : MakeShared<FJsonValueNull>());
	return Body;
}

FAssetDocumentRegionContext MakeUBlueprintGraphRegionContext(const FAssetDocumentRegionContext& Context)
{
	FAssetDocumentRegionContext GraphContext = Context;
	GraphContext.RegionId = TEXT("Body.UBlueprintGraphRegions");
	GraphContext.BodyPath = TEXT("Body.UBlueprintGraphRegions");
	GraphContext.JsonPointer = TEXT("/Body");
	return GraphContext;
}

FAssetDocumentCapabilityContext MakeCapabilityContext(const FAssetDocumentRegionContext& Context)
{
	FAssetDocumentCapabilityContext CapabilityContext;
	CapabilityContext.Asset = Context.Asset;
	CapabilityContext.AssetClass = Context.AssetClass;
	CapabilityContext.TargetAssetPath = Context.TargetAssetPath;
	CapabilityContext.SourceDocumentPath = Context.SourceDocumentPath;
	CapabilityContext.Definitions = Context.Definitions;
	CapabilityContext.Result = Context.Result;
	CapabilityContext.bIsDryRun = Context.bIsDryRun;
	return CapabilityContext;
}

FAssetDocumentCapabilityResult RequireArrayValue(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	const FString& BodyKey,
	TArray<TSharedPtr<FJsonValue>>& OutArray)
{
	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s must be an array when authored"), *BodyKey),
			Path,
			TEXT("InvalidBodySectionType"));
	}

	OutArray = Value->AsArray();
	return FAssetDocumentCapabilityResult::Success();
}

FString GetClassPath(const UClass* Class)
{
	return Class ? Class->GetPathName() : FString();
}

TSharedRef<FJsonObject> MakeClassRef(UClass* Class)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), GetClassPath(Class));
	return ClassRef;
}

FAssetDocumentCapabilityResult ReadClassRef(const TSharedPtr<FJsonObject>& Object, const FString& Path, UClass*& OutClass)
{
	OutClass = nullptr;
	if (!Object.IsValid())
	{
		return BodyFailure(TEXT("Expected ClassRef object"), Path, TEXT("InvalidClassRef"));
	}

	FString Kind;
	if (!Object->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("ClassRef"))
	{
		return BodyFailure(TEXT("ClassRef.Kind must be ClassRef"), Path / TEXT("Kind"), TEXT("InvalidClassRefKind"));
	}

	FString ClassPath;
	if (!Object->TryGetStringField(TEXT("Class"), ClassPath) || ClassPath.IsEmpty())
	{
		return BodyFailure(TEXT("ClassRef.Class is required"), Path / TEXT("Class"), TEXT("MissingClassRefClass"));
	}

	OutClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
	if (!OutClass)
	{
		return BodyFailure(
			FString::Printf(TEXT("Failed to resolve class '%s'"), *ClassPath),
			Path / TEXT("Class"),
			TEXT("UnresolvedClassRef"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

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

struct FBlueprintVariableSpec
{
	FName Name;
	FEdGraphPinType Type;
	FString DefaultValue;
	TOptional<FString> Category;
	TOptional<FString> Tooltip;
};

struct FBlueprintInterfaceSpec
{
	UClass* InterfaceClass = nullptr;
};

FAssetDocumentCapabilityResult ParseVariableSpecs(const TSharedPtr<FJsonObject>& BodyObject, TArray<FBlueprintVariableSpec>& OutVariables)
{
	OutVariables.Reset();
	const TSharedPtr<FJsonValue>* VariablesValue = BodyObject->Values.Find(TEXT("Variables"));
	if (!VariablesValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TArray<TSharedPtr<FJsonValue>> Variables;
	const FAssetDocumentCapabilityResult ArrayResult = RequireArrayValue(*VariablesValue, TEXT("/Body/Variables"), TEXT("Variables"), Variables);
	if (!ArrayResult.bSuccess)
	{
		return ArrayResult;
	}

	TSet<FName> SeenNames;
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

		FBlueprintVariableSpec Spec;
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

FAssetDocumentCapabilityResult ParseInterfaceSpecs(const TSharedPtr<FJsonObject>& BodyObject, TArray<FBlueprintInterfaceSpec>& OutInterfaces)
{
	OutInterfaces.Reset();
	const TSharedPtr<FJsonValue>* InterfacesValue = BodyObject->Values.Find(TEXT("ImplementedInterfaces"));
	if (!InterfacesValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TArray<TSharedPtr<FJsonValue>> Interfaces;
	const FAssetDocumentCapabilityResult ArrayResult = RequireArrayValue(*InterfacesValue, TEXT("/Body/ImplementedInterfaces"), TEXT("ImplementedInterfaces"), Interfaces);
	if (!ArrayResult.bSuccess)
	{
		return ArrayResult;
	}

	TSet<UClass*> SeenInterfaces;
	for (int32 Index = 0; Index < Interfaces.Num(); ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Body/ImplementedInterfaces/%d"), Index);
		const TSharedPtr<FJsonObject> InterfaceObject = Interfaces[Index].IsValid() ? Interfaces[Index]->AsObject() : nullptr;
		if (!InterfaceObject.IsValid())
		{
			return BodyFailure(TEXT("Body.ImplementedInterfaces entries must be objects"), Path, TEXT("InvalidImplementedInterface"));
		}

		const TSharedPtr<FJsonObject>* InterfaceRef = nullptr;
		if (!InterfaceObject->TryGetObjectField(TEXT("Interface"), InterfaceRef) || !InterfaceRef || !InterfaceRef->IsValid())
		{
			return BodyFailure(TEXT("ImplementedInterfaces entry requires Interface ClassRef"), Path / TEXT("Interface"), TEXT("MissingInterfaceClassRef"));
		}

		UClass* InterfaceClass = nullptr;
		const FAssetDocumentCapabilityResult ClassResult = ReadClassRef(*InterfaceRef, Path / TEXT("Interface"), InterfaceClass);
		if (!ClassResult.bSuccess)
		{
			return ClassResult;
		}
		if (!InterfaceClass->HasAnyClassFlags(CLASS_Interface))
		{
			return BodyFailure(
				FString::Printf(TEXT("Implemented interface '%s' is not an interface class"), *GetClassPath(InterfaceClass)),
				Path / TEXT("Interface/Class"),
				TEXT("InvalidInterfaceClass"));
		}
		if (SeenInterfaces.Contains(InterfaceClass))
		{
			return BodyFailure(
				FString::Printf(TEXT("Duplicate implemented interface '%s'"), *GetClassPath(InterfaceClass)),
				Path / TEXT("Interface/Class"),
				TEXT("DuplicateInterface"));
		}
		SeenInterfaces.Add(InterfaceClass);

		FBlueprintInterfaceSpec Spec;
		Spec.InterfaceClass = InterfaceClass;
		OutInterfaces.Add(Spec);
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

void ApplyVariableMetadata(UBlueprint* Blueprint, const FBlueprintVariableSpec& Spec)
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
	for (TFieldIterator<FProperty> PropertyIt(ParentClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		if (PropertyIt->GetFName() == Name)
		{
			return true;
		}
	}
	return false;
}

FAssetDocumentCapabilityResult ValidateVariablesAgainstParentClass(UClass* ParentClass, const TArray<FBlueprintVariableSpec>& Variables)
{
	if (!ParentClass)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	for (const FBlueprintVariableSpec& Variable : Variables)
	{
		if (ParentClassHasPropertyNamed(ParentClass, Variable.Name))
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.Variables Name '%s' conflicts with parent class '%s'"), *Variable.Name.ToString(), *GetClassPath(ParentClass)),
				FString::Printf(TEXT("/Body/Variables/%s"), *Variable.Name.ToString()),
				TEXT("ParentVariableNameConflict"));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyVariables(UBlueprint* Blueprint, const TArray<FBlueprintVariableSpec>& Variables, bool& bOutChanged)
{
	bOutChanged = false;
	TSet<FName> DesiredNames;
	for (const FBlueprintVariableSpec& Spec : Variables)
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
		if (!DesiredNames.Contains(ExistingName))
		{
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, ExistingName);
			bOutChanged = true;
		}
	}

	for (const FBlueprintVariableSpec& Spec : Variables)
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
				|| (Spec.Tooltip.IsSet() && PreviousTooltip != Spec.Tooltip.GetValue()))
			{
				bOutChanged = true;
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyInterfaces(UBlueprint* Blueprint, const TArray<FBlueprintInterfaceSpec>& Interfaces, bool& bOutChanged)
{
	bOutChanged = false;
	TSet<UClass*> DesiredInterfaces;
	for (const FBlueprintInterfaceSpec& Spec : Interfaces)
	{
		DesiredInterfaces.Add(Spec.InterfaceClass);
	}

	TArray<UClass*> ExistingInterfaces;
	for (const FBPInterfaceDescription& InterfaceDescription : Blueprint->ImplementedInterfaces)
	{
		if (InterfaceDescription.Interface)
		{
			ExistingInterfaces.Add(InterfaceDescription.Interface);
		}
	}

	for (UClass* ExistingInterface : ExistingInterfaces)
	{
		if (!DesiredInterfaces.Contains(ExistingInterface))
		{
			FBlueprintEditorUtils::RemoveInterface(Blueprint, ExistingInterface->GetClassPathName(), false);
			bOutChanged = true;
		}
	}

	for (const FBlueprintInterfaceSpec& Spec : Interfaces)
	{
		const bool bAlreadyImplemented = Blueprint->ImplementedInterfaces.ContainsByPredicate([&Spec](const FBPInterfaceDescription& InterfaceDescription)
		{
			return InterfaceDescription.Interface == Spec.InterfaceClass;
		});
		if (!bAlreadyImplemented)
		{
			if (!FBlueprintEditorUtils::ImplementNewInterface(Blueprint, Spec.InterfaceClass->GetClassPathName()))
			{
				return BodyFailure(
					FString::Printf(TEXT("Failed to implement Blueprint interface '%s'"), *GetClassPath(Spec.InterfaceClass)),
					TEXT("/Body/ImplementedInterfaces"),
					TEXT("ImplementInterfaceFailed"));
			}
			bOutChanged = true;
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

bool IsBlueprintVariableClassDefaultProperty(const UBlueprint* Blueprint, const FProperty* Property)
{
	return Blueprint && Property && FindNewVariable(Blueprint, Property->GetFName()) != nullptr;
}

FAssetDocumentCapabilityResult ParseClassDefaults(
	const TSharedPtr<FJsonObject>& BodyObject,
	bool& bOutHasClassDefaults,
	TSharedPtr<FJsonObject>& OutClassDefaults)
{
	bOutHasClassDefaults = false;
	OutClassDefaults.Reset();

	const TSharedPtr<FJsonValue>* ClassDefaultsValue = BodyObject->Values.Find(TEXT("ClassDefaults"));
	if (!ClassDefaultsValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	bOutHasClassDefaults = true;
	if (!ClassDefaultsValue->IsValid() || (*ClassDefaultsValue)->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body.ClassDefaults must be an object when authored"), TEXT("/Body/ClassDefaults"), TEXT("InvalidBodySectionType"));
	}

	OutClassDefaults = (*ClassDefaultsValue)->AsObject();
	if (!OutClassDefaults.IsValid())
	{
		return BodyFailure(TEXT("Body.ClassDefaults must be an object when authored"), TEXT("/Body/ClassDefaults"), TEXT("InvalidBodySectionType"));
	}

	return FAssetDocumentCapabilityResult::Success();
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

FAssetDocumentCapabilityResult PreflightClassDefaults(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& ClassDefaults)
{
	if (!ClassDefaults.IsValid() || ClassDefaults->Values.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	UClass* GeneratedClass = Blueprint ? Blueprint->GeneratedClass : nullptr;
	if (!GeneratedClass)
	{
		return BodyFailure(TEXT("Body.ClassDefaults requires a compiled Blueprint generated class"), TEXT("/Body/ClassDefaults"), TEXT("MissingGeneratedClass"));
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
		if (IsBlueprintVariableClassDefaultProperty(Blueprint, Property))
		{
			return BodyFailure(
				FString::Printf(TEXT("Blueprint variable '%s' must be authored through Body.Variables, not Body.ClassDefaults"), *Pair.Key),
				FString::Printf(TEXT("/Body/ClassDefaults/%s"), *Pair.Key),
				TEXT("BlueprintVariableClassDefaultUnsupported"));
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

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyClassDefaults(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& ClassDefaults)
{
	UClass* GeneratedClass = Blueprint ? Blueprint->GeneratedClass : nullptr;
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
		if (!FAssetDocumentPropertyAdapter::IsWritableProperty(Property)
			|| !IsSupportedClassDefaultProperty(Property)
			|| IsBlueprintVariableClassDefaultProperty(Blueprint, Property))
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

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyVariableDefaultsToGeneratedClass(UBlueprint* Blueprint, const TArray<FBlueprintVariableSpec>& Variables)
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
	for (const FBlueprintVariableSpec& Variable : Variables)
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
		const FBlueprintVariableSpec& Variable = Variables[Index];
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
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		return PinType.PinSubCategory.IsNone() && !PinType.PinSubCategoryObject.IsValid();
	}

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
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

TSharedRef<FJsonObject> VariableSpecToJsonObject(const FBlueprintVariableSpec& Variable)
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

FString MakeVariableIdentityKey(const FName Name)
{
	return Name.ToString().ToLower();
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

TSharedPtr<FJsonValue> MakeVariableDiffValue(const UBlueprint* Blueprint, const FBPVariableDescription& Variable)
{
	if (!IsSupportedAuthoredPinType(Variable.VarType))
	{
		return MakeShared<FJsonValueString>(TEXT("UnsupportedPinType"));
	}
	return MakeShared<FJsonValueObject>(VariableToJsonObject(Variable, Blueprint));
}

TSharedRef<FJsonObject> InterfaceToJsonObject(UClass* InterfaceClass)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetObjectField(TEXT("Interface"), MakeClassRef(InterfaceClass));
	return Object;
}

TSharedPtr<FJsonValue> MakeInterfaceDiffValue(UClass* InterfaceClass)
{
	return InterfaceClass
		? TSharedPtr<FJsonValue>(MakeShared<FJsonValueObject>(InterfaceToJsonObject(InterfaceClass)))
		: TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>());
}

TSharedPtr<FJsonObject> ExtractWritablePropertiesComparedToBaseline(UObject* Object, UObject* Baseline, const UBlueprint* Blueprint = nullptr)
{
	TSharedPtr<FJsonObject> PropertiesJson = MakeShared<FJsonObject>();
	if (!Object || !Baseline)
	{
		return PropertiesJson;
	}

	for (TFieldIterator<FProperty> PropertyIt(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!FAssetDocumentPropertyAdapter::IsWritableProperty(Property)
			|| !IsSupportedClassDefaultProperty(Property)
			|| IsBlueprintVariableClassDefaultProperty(Blueprint, Property))
		{
			continue;
		}

		FProperty* BaselineProperty = FindFProperty<FProperty>(Baseline->GetClass(), Property->GetFName());
		if (!BaselineProperty || !BaselineProperty->SameType(Property))
		{
			continue;
		}

		const void* CurrentValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		const void* BaselineValuePtr = BaselineProperty->ContainerPtrToValuePtr<void>(Baseline);
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

FAssetDocumentCapabilityResult ValidateInterfaces(const TSharedPtr<FJsonObject>& Body)
{
	TArray<FBlueprintInterfaceSpec> Interfaces;
	return ParseInterfaceSpecs(Body, Interfaces);
}

FAssetDocumentCapabilityResult ValidateVariables(const FAssetDocumentRegionContext& Context, const TSharedPtr<FJsonObject>& Body)
{
	TArray<FBlueprintVariableSpec> Variables;
	FAssetDocumentCapabilityResult Result = ParseVariableSpecs(Body, Variables);
	if (!Result.bSuccess)
	{
		return Result;
	}

	const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	UClass* ParentClass = Blueprint ? Blueprint->ParentClass.Get() : nullptr;
	return ValidateVariablesAgainstParentClass(ParentClass, Variables);
}

FAssetDocumentCapabilityResult ValidateClassDefaults(const FAssetDocumentRegionContext& Context, const TSharedPtr<FJsonObject>& Body)
{
	bool bHasClassDefaults = false;
	TSharedPtr<FJsonObject> ClassDefaults;
	FAssetDocumentCapabilityResult Result = ParseClassDefaults(Body, bHasClassDefaults, ClassDefaults);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!bHasClassDefaults)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	return PreflightClassDefaults(Cast<UBlueprint>(Context.Asset), ClassDefaults);
}

FAssetDocumentCapabilityResult ValidateGraph(const FAssetDocumentRegionContext& Context, const TSharedPtr<FJsonObject>& Body)
{
	return FUBlueprintGraphRegionAdapter().ValidateRegions(MakeCapabilityContext(Context), Body.ToSharedRef());
}

TSharedPtr<FJsonObject> CurrentClassDefaults(const UBlueprint* Blueprint)
{
	if (Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->GetSuperClass())
	{
		UObject* GeneratedCDO = Blueprint->GeneratedClass->GetDefaultObject(false);
		UObject* ParentCDO = Blueprint->GeneratedClass->GetSuperClass()->GetDefaultObject(false);
		return ExtractWritablePropertiesComparedToBaseline(GeneratedCDO, ParentCDO, Blueprint);
	}
	return MakeShared<FJsonObject>();
}
}

FBlueprintAssetDocumentCommonRegionAdapter::FBlueprintAssetDocumentCommonRegionAdapter(FName InAdapterName)
	: AdapterName(InAdapterName)
{
}

FName FBlueprintAssetDocumentCommonRegionAdapter::GetName() const
{
	return AdapterName;
}

bool FBlueprintAssetDocumentCommonRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return IsCommonRegion(Context.RegionId);
}

TSharedRef<FJsonObject> FBlueprintAssetDocumentCommonRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext& Context) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("RegionId"), Context.RegionId.ToString());
	Schema->SetStringField(TEXT("Adapter"), AdapterName.ToString());
	return Schema;
}

FAssetDocumentCapabilityResult FBlueprintAssetDocumentCommonRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	const FString BodyKey = RegionBodyKey(Context);
	TSharedRef<FJsonObject> Body = MakeBodyObject(BodyKey, DesiredValue);
	if (BodyKey == TEXT("ImplementedInterfaces"))
	{
		return ValidateInterfaces(Body);
	}
	if (BodyKey == TEXT("Variables"))
	{
		return ValidateVariables(Context, Body);
	}
	if (BodyKey == TEXT("ClassDefaults"))
	{
		return ValidateClassDefaults(Context, Body);
	}
	if (BodyKey == TEXT("UbergraphPages"))
	{
		return ValidateGraph(Context, Body);
	}
	return BodyFailure(TEXT("Unsupported Blueprint common region"), Context.JsonPointer, TEXT("UnsupportedBlueprintCommonRegion"));
}

FAssetDocumentCapabilityResult FBlueprintAssetDocumentCommonRegionAdapter::PreflightRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return ValidateRegion(Context, DesiredValue);
}

FAssetDocumentCapabilityResult FBlueprintAssetDocumentCommonRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;
	UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		return BodyFailure(TEXT("Blueprint common region apply requires a UBlueprint asset"), Context.JsonPointer, TEXT("UnsupportedAsset"));
	}

	const FString BodyKey = RegionBodyKey(Context);
	TSharedRef<FJsonObject> Body = MakeBodyObject(BodyKey, DesiredValue);
	if (BodyKey == TEXT("ImplementedInterfaces"))
	{
		TArray<FBlueprintInterfaceSpec> Interfaces;
		FAssetDocumentCapabilityResult Result = ParseInterfaceSpecs(Body, Interfaces);
		if (!Result.bSuccess)
		{
			return Result;
		}
		return ApplyInterfaces(Blueprint, Interfaces, bOutChanged);
	}
	if (BodyKey == TEXT("Variables"))
	{
		TArray<FBlueprintVariableSpec> Variables;
		FAssetDocumentCapabilityResult Result = ParseVariableSpecs(Body, Variables);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ValidateVariablesAgainstParentClass(Blueprint->ParentClass.Get(), Variables);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ApplyVariables(Blueprint, Variables, bOutChanged);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (bOutChanged)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			if (Blueprint->Status == BS_Error)
			{
				return BodyFailure(TEXT("Failed to compile Blueprint after applying variables"), TEXT("/Body/Variables"), TEXT("BlueprintCompileFailed"));
			}
		}
		return ApplyVariableDefaultsToGeneratedClass(Blueprint, Variables);
	}
	if (BodyKey == TEXT("ClassDefaults"))
	{
		bool bHasClassDefaults = false;
		TSharedPtr<FJsonObject> ClassDefaults;
		FAssetDocumentCapabilityResult Result = ParseClassDefaults(Body, bHasClassDefaults, ClassDefaults);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (!ClassDefaults.IsValid())
		{
			ClassDefaults = MakeShared<FJsonObject>();
		}
		Result = PreflightClassDefaults(Blueprint, ClassDefaults);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ApplyClassDefaults(Blueprint, ClassDefaults);
		if (Result.bSuccess)
		{
			bOutChanged = true;
		}
		return Result;
	}
	if (BodyKey == TEXT("UbergraphPages"))
	{
		FAssetDocumentCapabilityContext CapabilityContext = MakeCapabilityContext(Context);
		return ApplyUBlueprintGraphRegions(CapabilityContext, Body, bOutChanged);
	}
	return BodyFailure(TEXT("Unsupported Blueprint common region"), Context.JsonPointer, TEXT("UnsupportedBlueprintCommonRegion"));
}

FAssetDocumentCapabilityResult FBlueprintAssetDocumentCommonRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	const FString BodyKey = RegionBodyKey(Context);
	if (BodyKey == TEXT("ImplementedInterfaces"))
	{
		TArray<TSharedPtr<FJsonValue>> Interfaces;
		if (Blueprint)
		{
			for (const FBPInterfaceDescription& InterfaceDescription : Blueprint->ImplementedInterfaces)
			{
				if (InterfaceDescription.Interface)
				{
					Interfaces.Add(MakeShared<FJsonValueObject>(InterfaceToJsonObject(InterfaceDescription.Interface)));
				}
			}
		}
		OutCurrentValue = MakeShared<FJsonValueArray>(MoveTemp(Interfaces));
		return FAssetDocumentCapabilityResult::Success();
	}
	if (BodyKey == TEXT("Variables"))
	{
		TArray<TSharedPtr<FJsonValue>> Variables;
		if (Blueprint)
		{
			for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
			{
				if (IsSupportedAuthoredPinType(Variable.VarType))
				{
					Variables.Add(MakeShared<FJsonValueObject>(VariableToJsonObject(Variable, Blueprint)));
				}
			}
		}
		OutCurrentValue = MakeShared<FJsonValueArray>(MoveTemp(Variables));
		return FAssetDocumentCapabilityResult::Success();
	}
	if (BodyKey == TEXT("ClassDefaults"))
	{
		OutCurrentValue = MakeShared<FJsonValueObject>(CurrentClassDefaults(Blueprint).ToSharedRef());
		return FAssetDocumentCapabilityResult::Success();
	}
	if (BodyKey == TEXT("UbergraphPages"))
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		const FAssetDocumentCapabilityResult Result =
			FUBlueprintGraphRegionAdapter().ExtractRegions(MakeCapabilityContext(Context), Body);
		if (!Result.bSuccess)
		{
			return Result;
		}
		const TSharedPtr<FJsonValue>* Value = Body->Values.Find(TEXT("UbergraphPages"));
		OutCurrentValue = Value && Value->IsValid() ? *Value : MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>());
		return FAssetDocumentCapabilityResult::Success();
	}
	return BodyFailure(TEXT("Unsupported Blueprint common region"), Context.JsonPointer, TEXT("UnsupportedBlueprintCommonRegion"));
}

FAssetDocumentCapabilityResult FBlueprintAssetDocumentCommonRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		return BodyFailure(TEXT("Blueprint common region diff requires a UBlueprint asset"), Context.JsonPointer, TEXT("UnsupportedAsset"));
	}

	const FString BodyKey = RegionBodyKey(Context);
	TSharedRef<FJsonObject> Body = MakeBodyObject(BodyKey, DesiredValue);
	if (BodyKey == TEXT("UbergraphPages"))
	{
		return FUBlueprintGraphRegionAdapter().DiffRegions(MakeCapabilityContext(Context), Body, OutDiffEntries);
	}
	if (BodyKey == TEXT("Variables"))
	{
		TArray<FBlueprintVariableSpec> DesiredVariables;
		FAssetDocumentCapabilityResult Result = ParseVariableSpecs(Body, DesiredVariables);
		if (!Result.bSuccess)
		{
			return Result;
		}

		TArray<FAssetDocumentIdentityArrayDiffElement> CurrentVariables;
		TMap<FString, const FBPVariableDescription*> CurrentVariablesByIdentity;
		TMap<FString, FString> VariablePathTokensByIdentity;
		for (const FBPVariableDescription& CurrentVariable : Blueprint->NewVariables)
		{
			const FString VariableName = CurrentVariable.VarName.ToString();
			const FString VariableIdentity = MakeVariableIdentityKey(CurrentVariable.VarName);
			CurrentVariablesByIdentity.Add(VariableIdentity, &CurrentVariable);
			VariablePathTokensByIdentity.Add(VariableIdentity, VariableName);
			CurrentVariables.Add({VariableIdentity, VariableName, MakeVariableDiffValue(Blueprint, CurrentVariable)});
		}

		TMap<FString, FBlueprintVariableSpec> DesiredVariableSpecsByIdentity;
		TArray<FAssetDocumentIdentityArrayDiffElement> DesiredVariableElements;
		for (const FBlueprintVariableSpec& DesiredVariable : DesiredVariables)
		{
			const FString VariableName = DesiredVariable.Name.ToString();
			const FString VariableIdentity = MakeVariableIdentityKey(DesiredVariable.Name);
			DesiredVariableSpecsByIdentity.Add(VariableIdentity, DesiredVariable);
			if (!VariablePathTokensByIdentity.Contains(VariableIdentity))
			{
				VariablePathTokensByIdentity.Add(VariableIdentity, VariableName);
			}
			DesiredVariableElements.Add({VariableIdentity, VariableName, MakeShared<FJsonValueObject>(VariableSpecToJsonObject(DesiredVariable))});
		}

		FAssetDocumentIdentityArrayDiffOptions Options;
		Options.RegionPath = TEXT("/Body/Variables");
		FAssetDocumentIdentityArrayDiffHooks Hooks;
		Hooks.MakePath = [&VariablePathTokensByIdentity](const FAssetDocumentIdentityArrayDiffEntryContext& Entry)
		{
			const FString PathToken = VariablePathTokensByIdentity.FindRef(Entry.Identity);
			return FString::Printf(TEXT("/Body/Variables/%s"), PathToken.IsEmpty() ? *Entry.Identity : *PathToken);
		};
		Hooks.AreElementsEqual = [Blueprint, &CurrentVariablesByIdentity, &DesiredVariableSpecsByIdentity](const FAssetDocumentIdentityArrayDiffEntryContext& Entry)
		{
			if (!Entry.bHasCurrent || !Entry.bHasDesired)
			{
				return false;
			}
			const FBPVariableDescription* const* CurrentVariable = CurrentVariablesByIdentity.Find(Entry.Identity);
			const FBlueprintVariableSpec* DesiredVariable = DesiredVariableSpecsByIdentity.Find(Entry.Identity);
			if (!CurrentVariable || !DesiredVariable)
			{
				return false;
			}
			const bool bTypeChanged = AuthoredPinTypesDiffer((*CurrentVariable)->VarType, DesiredVariable->Type);
			const FString CurrentDefaultValue = ResolveVariableDefaultValue(Blueprint, **CurrentVariable);
			const bool bDefaultChanged = AuthoredDefaultValuesDiffer((*CurrentVariable)->VarType, CurrentDefaultValue, DesiredVariable->DefaultValue);
			const FString DesiredCategory = DesiredVariable->Category.IsSet() ? DesiredVariable->Category.GetValue() : FString();
			const bool bCategoryChanged = (*CurrentVariable)->Category.ToString() != DesiredCategory;
			const FString CurrentTooltip = (*CurrentVariable)->HasMetaData(FBlueprintMetadata::MD_Tooltip)
				? (*CurrentVariable)->GetMetaData(FBlueprintMetadata::MD_Tooltip)
				: FString();
			const FString DesiredTooltip = DesiredVariable->Tooltip.IsSet() ? DesiredVariable->Tooltip.GetValue() : FString();
			return !(bTypeChanged || bDefaultChanged || bCategoryChanged || CurrentTooltip != DesiredTooltip);
		};
		return FAssetDocumentIdentityArrayDiffHelper::Diff(Options, CurrentVariables, DesiredVariableElements, Hooks, OutDiffEntries);
	}
	if (BodyKey == TEXT("ImplementedInterfaces"))
	{
		TArray<FBlueprintInterfaceSpec> DesiredInterfaces;
		FAssetDocumentCapabilityResult Result = ParseInterfaceSpecs(Body, DesiredInterfaces);
		if (!Result.bSuccess)
		{
			return Result;
		}

		TArray<FAssetDocumentIdentityArrayDiffElement> CurrentInterfaceElements;
		for (const FBPInterfaceDescription& CurrentInterface : Blueprint->ImplementedInterfaces)
		{
			if (CurrentInterface.Interface)
			{
				const FString CurrentPath = GetClassPath(CurrentInterface.Interface);
				CurrentInterfaceElements.Add({CurrentPath, CurrentPath, MakeInterfaceDiffValue(CurrentInterface.Interface)});
			}
		}

		TArray<FAssetDocumentIdentityArrayDiffElement> DesiredInterfaceElements;
		for (const FBlueprintInterfaceSpec& DesiredInterface : DesiredInterfaces)
		{
			const FString DesiredPath = GetClassPath(DesiredInterface.InterfaceClass);
			DesiredInterfaceElements.Add({DesiredPath, DesiredPath, MakeInterfaceDiffValue(DesiredInterface.InterfaceClass)});
		}

		FAssetDocumentIdentityArrayDiffOptions Options;
		Options.RegionPath = TEXT("/Body/ImplementedInterfaces");
		FAssetDocumentIdentityArrayDiffHooks Hooks;
		Hooks.AreElementsEqual = [](const FAssetDocumentIdentityArrayDiffEntryContext&) { return true; };
		Hooks.MakePath = [](const FAssetDocumentIdentityArrayDiffEntryContext& Entry)
		{
			return FString::Printf(TEXT("/Body/ImplementedInterfaces/%s"), *Entry.Identity);
		};
		return FAssetDocumentIdentityArrayDiffHelper::Diff(Options, CurrentInterfaceElements, DesiredInterfaceElements, Hooks, OutDiffEntries);
	}
	if (BodyKey == TEXT("ClassDefaults"))
	{
		bool bHasClassDefaults = false;
		TSharedPtr<FJsonObject> DesiredClassDefaults;
		FAssetDocumentCapabilityResult Result = ParseClassDefaults(Body, bHasClassDefaults, DesiredClassDefaults);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (!DesiredClassDefaults.IsValid())
		{
			DesiredClassDefaults = MakeShared<FJsonObject>();
		}

		TSharedPtr<FJsonObject> CurrentDefaults = CurrentClassDefaults(Blueprint);
		TSet<FString> SeenNames;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& CurrentPair : CurrentDefaults->Values)
		{
			SeenNames.Add(CurrentPair.Key);
			const TSharedPtr<FJsonValue>* Desired = DesiredClassDefaults->Values.Find(CurrentPair.Key);
			FAssetDocumentJsonRegionUtils::AddDiffEntry(
				OutDiffEntries,
				FString::Printf(TEXT("/Body/ClassDefaults/%s"), *CurrentPair.Key),
				(!Desired || FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentPair.Value) != FAssetDocumentJsonRegionUtils::JsonValueToComparableString(*Desired)) ? TEXT("changed") : TEXT("unchanged"),
				CurrentPair.Value,
				Desired ? *Desired : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>()));
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& DesiredPair : DesiredClassDefaults->Values)
		{
			if (!SeenNames.Contains(DesiredPair.Key))
			{
				FAssetDocumentJsonRegionUtils::AddDiffEntry(
					OutDiffEntries,
					FString::Printf(TEXT("/Body/ClassDefaults/%s"), *DesiredPair.Key),
					TEXT("changed"),
					MakeShared<FJsonValueNull>(),
					DesiredPair.Value);
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	}
	return BodyFailure(TEXT("Unsupported Blueprint common region"), Context.JsonPointer, TEXT("UnsupportedBlueprintCommonRegion"));
}
