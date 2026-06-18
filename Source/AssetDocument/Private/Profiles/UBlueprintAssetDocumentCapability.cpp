// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/UBlueprintAssetDocumentCapability.h"

#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
bool IsKnownBodyKey(const FString& BodyKey)
{
	for (const FName& KnownBodyKey : FUBlueprintAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (KnownBodyKey.ToString() == BodyKey)
		{
			return true;
		}
	}
	return false;
}

bool IsProtectedRegion(const FString& BodyKey)
{
	return BodyKey == TEXT("UbergraphPages")
		|| BodyKey == TEXT("FunctionGraphs")
		|| BodyKey == TEXT("MacroGraphs")
		|| BodyKey == TEXT("Timelines");
}

FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult RequireObjectValue(const TSharedRef<FJsonValue>& Value, const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
	if (Value->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body must be a JSON object"), Path, TEXT("InvalidBodyType"));
	}

	OutObject = Value->AsObject();
	if (!OutObject.IsValid())
	{
		return BodyFailure(TEXT("Body must be a JSON object"), Path, TEXT("InvalidBodyType"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RequireArrayOrNullValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, const FString& BodyKey)
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
			FString::Printf(TEXT("Body.%s is an unsupported UBlueprint graph/timeline region and cannot be non-empty yet"), *BodyKey),
			Path,
			TEXT("UnsupportedUBlueprintRegion"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ResolveParentClass(const TSharedPtr<FJsonValue>& Value, UClass*& OutParentClass)
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

	if (!OutParentClass->IsChildOf(UObject::StaticClass()))
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.ParentClass.Class '%s' is not a UObject class"), *OutParentClass->GetName()),
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

FAssetDocumentCapabilityResult ValidateParentClass(const TSharedPtr<FJsonValue>& Value)
{
	UClass* ParentClass = nullptr;
	return ResolveParentClass(Value, ParentClass);
}

struct FUBlueprintVariableSpec
{
	FName Name;
	FEdGraphPinType Type;
	FString DefaultValue;
	TOptional<FString> Category;
	TOptional<FString> Tooltip;
};

struct FUBlueprintInterfaceSpec
{
	UClass* InterfaceClass = nullptr;
};

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

FAssetDocumentCapabilityResult ReadBodyObject(const TSharedRef<FJsonValue>& BodyJson, TSharedPtr<FJsonObject>& OutBody)
{
	return RequireObjectValue(BodyJson, TEXT("/Body"), OutBody);
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

FAssetDocumentCapabilityResult ParseVariableSpecs(const TSharedPtr<FJsonObject>& BodyObject, TArray<FUBlueprintVariableSpec>& OutVariables)
{
	OutVariables.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
	if (!BodyObject->TryGetArrayField(TEXT("Variables"), Variables))
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSet<FName> SeenNames;
	for (int32 Index = 0; Index < Variables->Num(); ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Body/Variables/%d"), Index);
		const TSharedPtr<FJsonObject> VariableObject = (*Variables)[Index].IsValid() ? (*Variables)[Index]->AsObject() : nullptr;
		if (!VariableObject.IsValid())
		{
			return BodyFailure(TEXT("Body.Variables entries must be objects"), Path, TEXT("InvalidVariable"));
		}

		FString Name;
		if (!VariableObject->TryGetStringField(TEXT("Name"), Name) || Name.IsEmpty())
		{
			return BodyFailure(TEXT("Variable Name is required"), Path / TEXT("Name"), TEXT("MissingVariableName"));
		}

		FUBlueprintVariableSpec Spec;
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

FAssetDocumentCapabilityResult ParseInterfaceSpecs(const TSharedPtr<FJsonObject>& BodyObject, TArray<FUBlueprintInterfaceSpec>& OutInterfaces)
{
	OutInterfaces.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Interfaces = nullptr;
	if (!BodyObject->TryGetArrayField(TEXT("ImplementedInterfaces"), Interfaces))
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSet<UClass*> SeenInterfaces;
	for (int32 Index = 0; Index < Interfaces->Num(); ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Body/ImplementedInterfaces/%d"), Index);
		const TSharedPtr<FJsonObject> InterfaceObject = (*Interfaces)[Index].IsValid() ? (*Interfaces)[Index]->AsObject() : nullptr;
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

		FUBlueprintInterfaceSpec Spec;
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

void ApplyVariableMetadata(UBlueprint* Blueprint, const FUBlueprintVariableSpec& Spec)
{
	if (FBPVariableDescription* Variable = FindNewVariable(Blueprint, Spec.Name))
	{
		Variable->DefaultValue = Spec.DefaultValue;
		if (Spec.Category.IsSet())
		{
			FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, Spec.Name, nullptr, FText::FromString(Spec.Category.GetValue()), true);
		}
		if (Spec.Tooltip.IsSet())
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Spec.Name, nullptr, FBlueprintMetadata::MD_Tooltip, Spec.Tooltip.GetValue());
		}
	}
}

FAssetDocumentCapabilityResult ApplyVariables(UBlueprint* Blueprint, const TArray<FUBlueprintVariableSpec>& Variables, bool& bOutChanged)
{
	bOutChanged = false;
	TSet<FName> DesiredNames;
	for (const FUBlueprintVariableSpec& Spec : Variables)
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

	for (const FUBlueprintVariableSpec& Spec : Variables)
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
			if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Spec.Name, Spec.Type, Spec.DefaultValue))
			{
				return BodyFailure(
					FString::Printf(TEXT("Failed to add Blueprint variable '%s'"), *Spec.Name.ToString()),
					TEXT("/Body/Variables"),
					TEXT("AddVariableFailed"));
			}
			bOutChanged = true;
		}

		FBPVariableDescription* Mutable = FindNewVariable(Blueprint, Spec.Name);
		if (Mutable)
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

FAssetDocumentCapabilityResult ApplyInterfaces(UBlueprint* Blueprint, const TArray<FUBlueprintInterfaceSpec>& Interfaces, bool& bOutChanged)
{
	bOutChanged = false;
	TSet<UClass*> DesiredInterfaces;
	for (const FUBlueprintInterfaceSpec& Spec : Interfaces)
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

	for (const FUBlueprintInterfaceSpec& Spec : Interfaces)
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

TSharedRef<FJsonObject> VariableToJsonObject(const FBPVariableDescription& Variable)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Name"), Variable.VarName.ToString());
	Object->SetObjectField(TEXT("Type"), PinTypeToJsonObject(Variable.VarType));
	Object->SetStringField(TEXT("DefaultValue"), Variable.DefaultValue);
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

TSharedRef<FJsonObject> VariableSpecToJsonObject(const FUBlueprintVariableSpec& Variable)
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

void AddBodyDiffEntry(
	TArray<TSharedPtr<FJsonValue>>& Entries,
	const FString& Path,
	const FString& Status,
	TSharedPtr<FJsonValue> Current,
	TSharedPtr<FJsonValue> Desired,
	const FString& Change = FString())
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), Path);
	Entry->SetStringField(TEXT("status"), Status);
	if (!Change.IsEmpty())
	{
		Entry->SetStringField(TEXT("change"), Change);
	}
	Entry->SetField(TEXT("current"), Current.IsValid() ? Current : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("desired"), Desired.IsValid() ? Desired : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

void AddSkippedUnsupportedEvidence(TSharedRef<FJsonObject>& OutBodyJson, const FString& Region, int32 Count)
{
	if (Count <= 0)
	{
		return;
	}

	TSharedPtr<FJsonObject> Skipped;
	const TSharedPtr<FJsonObject>* ExistingSkipped = nullptr;
	if (OutBodyJson->TryGetObjectField(TEXT("_Skipped"), ExistingSkipped) && ExistingSkipped && ExistingSkipped->IsValid())
	{
		Skipped = *ExistingSkipped;
	}
	else
	{
		Skipped = MakeShared<FJsonObject>();
		OutBodyJson->SetObjectField(TEXT("_Skipped"), Skipped);
	}

	TSharedPtr<FJsonObject> RegionDiagnostic = MakeShared<FJsonObject>();
	RegionDiagnostic->SetStringField(TEXT("Reason"), TEXT("UnsupportedUBlueprintRegion"));
	RegionDiagnostic->SetNumberField(TEXT("Count"), Count);
	Skipped->SetObjectField(Region, RegionDiagnostic);
}
}

const TArray<FName>& FUBlueprintAssetDocumentCapability::GetCanonicalBodyKeys()
{
	static const TArray<FName> Keys = {
		TEXT("ParentClass"),
		TEXT("ImplementedInterfaces"),
		TEXT("Variables"),
		TEXT("Components"),
		TEXT("ClassDefaults"),
		TEXT("UbergraphPages"),
		TEXT("FunctionGraphs"),
		TEXT("MacroGraphs"),
		TEXT("Timelines"),
	};
	return Keys;
}

FName FUBlueprintAssetDocumentCapability::GetName() const
{
	return TEXT("UBlueprintBody");
}

TArray<FName> FUBlueprintAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {TEXT("UBlueprintBody"), TEXT("UBlueprintAuthoritativeRegions")};
}

int32 FUBlueprintAssetDocumentCapability::GetApplyOrder() const
{
	return 60;
}

bool FUBlueprintAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && Asset->GetClass() == UBlueprint::StaticClass();
}

bool FUBlueprintAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UBlueprint::StaticClass();
}

TSharedRef<FJsonObject> FUBlueprintAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("ParentClass"), TEXT("ClassRef"));
	Schema->SetStringField(TEXT("ImplementedInterfaces"), TEXT("array<{Interface: ClassRef}>"));
	Schema->SetStringField(TEXT("Variables"), TEXT("array<{Name, Type, DefaultValue, Flags, Category, Tooltip}>"));
	Schema->SetStringField(TEXT("Components"), TEXT("array<{Key:{Name,OwnerClass}, Scope, Class, AttachTo, Root, Properties}>"));
	Schema->SetStringField(TEXT("ClassDefaults"), TEXT("object"));
	Schema->SetStringField(TEXT("UbergraphPages"), TEXT("array unsupported until graph region implementation"));
	Schema->SetStringField(TEXT("FunctionGraphs"), TEXT("array unsupported until graph region implementation"));
	Schema->SetStringField(TEXT("MacroGraphs"), TEXT("array unsupported until graph region implementation"));
	Schema->SetStringField(TEXT("Timelines"), TEXT("array unsupported until timeline region implementation"));
	return Schema;
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = ReadBodyObject(BodyJson, BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}
	return ValidateBodyObject(Context, BodyObject.ToSharedRef());
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	return Validate(Context, BodyJson);
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		return BodyFailure(TEXT("UBlueprint body apply requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = ReadBodyObject(BodyJson, BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	const FAssetDocumentCapabilityResult ValidateResult = ValidateBodyObject(Context, BodyObject.ToSharedRef());
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	UClass* ParsedParentClass = nullptr;
	if (const TSharedPtr<FJsonValue>* ParentClassValue = BodyObject->Values.Find(TEXT("ParentClass")))
	{
		const FAssetDocumentCapabilityResult ParentClassResult = ResolveParentClass(*ParentClassValue, ParsedParentClass);
		if (!ParentClassResult.bSuccess)
		{
			return ParentClassResult;
		}
	}

	TArray<FUBlueprintInterfaceSpec> ParsedInterfaces;
	const FAssetDocumentCapabilityResult InterfaceParseResult = ParseInterfaceSpecs(BodyObject, ParsedInterfaces);
	if (!InterfaceParseResult.bSuccess)
	{
		return InterfaceParseResult;
	}

	TArray<FUBlueprintVariableSpec> ParsedVariables;
	const FAssetDocumentCapabilityResult VariableParseResult = ParseVariableSpecs(BodyObject, ParsedVariables);
	if (!VariableParseResult.bSuccess)
	{
		return VariableParseResult;
	}

	bool bChanged = false;
	UClass* PreviousParentClass = Blueprint->ParentClass.Get();
	if (ParsedParentClass && PreviousParentClass != ParsedParentClass)
	{
		Blueprint->Modify();
		Blueprint->ParentClass = ParsedParentClass;
		bChanged = true;
	}

	if (BodyObject->HasField(TEXT("ImplementedInterfaces")))
	{
		bool bInterfacesChanged = false;
		const FAssetDocumentCapabilityResult InterfaceApplyResult = ApplyInterfaces(Blueprint, ParsedInterfaces, bInterfacesChanged);
		if (!InterfaceApplyResult.bSuccess)
		{
			return InterfaceApplyResult;
		}
		bChanged |= bInterfacesChanged;
	}

	if (BodyObject->HasField(TEXT("Variables")))
	{
		bool bVariablesChanged = false;
		const FAssetDocumentCapabilityResult VariableApplyResult = ApplyVariables(Blueprint, ParsedVariables, bVariablesChanged);
		if (!VariableApplyResult.bSuccess)
		{
			return VariableApplyResult;
		}
		bChanged |= bVariablesChanged;
	}

	if (bChanged)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			if (PreviousParentClass && Blueprint->ParentClass.Get() != PreviousParentClass)
			{
				Blueprint->ParentClass = PreviousParentClass;
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
			}
			return BodyFailure(TEXT("Failed to compile UBlueprint after applying authoritative Body regions"), TEXT("/Body"), TEXT("BlueprintCompileFailed"));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Applied UBlueprint Body"));
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	if (Context.Asset && !SupportsAsset(Context.Asset))
	{
		return BodyFailure(TEXT("UBlueprint body extract requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}
	if (!Context.Asset && Context.AssetClass && !SupportsClass(Context.AssetClass))
	{
		return BodyFailure(TEXT("UBlueprint body extract requires exact UBlueprint class"), TEXT("/Class"), TEXT("UnsupportedClass"));
	}

	const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (Blueprint && Blueprint->ParentClass)
	{
		OutBodyJson->SetObjectField(TEXT("ParentClass"), MakeClassRef(Blueprint->ParentClass.Get()));
	}
	else
	{
		OutBodyJson->SetObjectField(TEXT("ParentClass"), MakeShared<FJsonObject>());
	}

	TArray<TSharedPtr<FJsonValue>> Interfaces;
	if (Blueprint)
	{
		for (const FBPInterfaceDescription& InterfaceDescription : Blueprint->ImplementedInterfaces)
		{
			if (!InterfaceDescription.Interface)
			{
				continue;
			}
			TSharedPtr<FJsonObject> InterfaceObject = MakeShared<FJsonObject>();
			InterfaceObject->SetObjectField(TEXT("Interface"), MakeClassRef(InterfaceDescription.Interface));
			Interfaces.Add(MakeShared<FJsonValueObject>(InterfaceObject));
		}
	}
	OutBodyJson->SetArrayField(TEXT("ImplementedInterfaces"), Interfaces);

	TArray<TSharedPtr<FJsonValue>> Variables;
	if (Blueprint)
	{
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			Variables.Add(MakeShared<FJsonValueObject>(VariableToJsonObject(Variable)));
		}
	}
	OutBodyJson->SetArrayField(TEXT("Variables"), Variables);
	OutBodyJson->SetArrayField(TEXT("Components"), {});
	OutBodyJson->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	OutBodyJson->SetArrayField(TEXT("UbergraphPages"), {});
	OutBodyJson->SetArrayField(TEXT("FunctionGraphs"), {});
	OutBodyJson->SetArrayField(TEXT("MacroGraphs"), {});
	OutBodyJson->SetArrayField(TEXT("Timelines"), {});

	if (Blueprint)
	{
		AddSkippedUnsupportedEvidence(OutBodyJson, TEXT("UbergraphPages"), Blueprint->UbergraphPages.Num());
		AddSkippedUnsupportedEvidence(OutBodyJson, TEXT("FunctionGraphs"), Blueprint->FunctionGraphs.Num());
		AddSkippedUnsupportedEvidence(OutBodyJson, TEXT("MacroGraphs"), Blueprint->MacroGraphs.Num());
		AddSkippedUnsupportedEvidence(OutBodyJson, TEXT("Timelines"), Blueprint->Timelines.Num());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted UBlueprint Body"));
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TSharedPtr<FJsonObject> DesiredBody;
	const FAssetDocumentCapabilityResult BodyResult = ReadBodyObject(DesiredJson, DesiredBody);
	if (!BodyResult.bSuccess)
	{
		return BodyResult;
	}

	const FAssetDocumentCapabilityResult ValidateResult = ValidateBodyObject(Context, DesiredBody.ToSharedRef());
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	TArray<FUBlueprintVariableSpec> DesiredVariables;
	const FAssetDocumentCapabilityResult VariableParseResult = ParseVariableSpecs(DesiredBody, DesiredVariables);
	if (!VariableParseResult.bSuccess)
	{
		return VariableParseResult;
	}

	const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		return BodyFailure(TEXT("UBlueprint body diff requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	TMap<FName, FUBlueprintVariableSpec> DesiredByName;
	for (const FUBlueprintVariableSpec& Variable : DesiredVariables)
	{
		DesiredByName.Add(Variable.Name, Variable);
	}

	TSet<FName> SeenCurrent;
	for (const FBPVariableDescription& CurrentVariable : Blueprint->NewVariables)
	{
		SeenCurrent.Add(CurrentVariable.VarName);
		const FUBlueprintVariableSpec* DesiredVariable = DesiredByName.Find(CurrentVariable.VarName);
		const FString Path = FString::Printf(TEXT("/Body/Variables/%s"), *CurrentVariable.VarName.ToString());
		if (!DesiredVariable)
		{
			AddBodyDiffEntry(
				OutDiffEntries,
				Path,
				TEXT("changed"),
				MakeShared<FJsonValueObject>(VariableToJsonObject(CurrentVariable)),
				MakeShared<FJsonValueNull>(),
				TEXT("extra"));
			continue;
		}

		const bool bTypeChanged = CurrentVariable.VarType != DesiredVariable->Type;
		const bool bDefaultChanged = CurrentVariable.DefaultValue != DesiredVariable->DefaultValue;
		const bool bCategoryChanged = DesiredVariable->Category.IsSet() && CurrentVariable.Category.ToString() != DesiredVariable->Category.GetValue();
		const FString CurrentTooltip = CurrentVariable.HasMetaData(FBlueprintMetadata::MD_Tooltip)
			? CurrentVariable.GetMetaData(FBlueprintMetadata::MD_Tooltip)
			: FString();
		const bool bTooltipChanged = DesiredVariable->Tooltip.IsSet() && CurrentTooltip != DesiredVariable->Tooltip.GetValue();

		AddBodyDiffEntry(
			OutDiffEntries,
			Path,
			(bTypeChanged || bDefaultChanged || bCategoryChanged || bTooltipChanged) ? TEXT("changed") : TEXT("unchanged"),
			MakeShared<FJsonValueObject>(VariableToJsonObject(CurrentVariable)),
			MakeShared<FJsonValueObject>(VariableSpecToJsonObject(*DesiredVariable)),
			(bTypeChanged || bDefaultChanged || bCategoryChanged || bTooltipChanged) ? TEXT("changed") : FString());
	}

	for (const FUBlueprintVariableSpec& DesiredVariable : DesiredVariables)
	{
		if (!SeenCurrent.Contains(DesiredVariable.Name))
		{
			AddBodyDiffEntry(
				OutDiffEntries,
				FString::Printf(TEXT("/Body/Variables/%s"), *DesiredVariable.Name.ToString()),
				TEXT("changed"),
				MakeShared<FJsonValueNull>(),
				MakeShared<FJsonValueObject>(VariableSpecToJsonObject(DesiredVariable)),
				TEXT("missing"));
		}
	}

	if (const TSharedPtr<FJsonValue>* DesiredParentClassValue = DesiredBody->Values.Find(TEXT("ParentClass")))
	{
		TSharedPtr<FJsonObject> DesiredParentObject = (*DesiredParentClassValue).IsValid() ? (*DesiredParentClassValue)->AsObject() : nullptr;
		UClass* DesiredParentClass = nullptr;
		const FAssetDocumentCapabilityResult ParentClassResult = ReadClassRef(DesiredParentObject, TEXT("/Body/ParentClass"), DesiredParentClass);
		if (!ParentClassResult.bSuccess)
		{
			return ParentClassResult;
		}
		AddBodyDiffEntry(
			OutDiffEntries,
			TEXT("/Body/ParentClass"),
			Blueprint->ParentClass.Get() == DesiredParentClass ? TEXT("unchanged") : TEXT("changed"),
			MakeShared<FJsonValueObject>(MakeClassRef(Blueprint->ParentClass.Get())),
			MakeShared<FJsonValueObject>(MakeClassRef(DesiredParentClass)));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("UBlueprint Body diffed"));
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::ValidateBodyObject(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject) const
{
	if (Context.Asset && !SupportsAsset(Context.Asset))
	{
		return BodyFailure(TEXT("UBlueprint body validation requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}
	if (!Context.Asset && Context.AssetClass && !SupportsClass(Context.AssetClass))
	{
		return BodyFailure(TEXT("UBlueprint body validation requires exact UBlueprint class"), TEXT("/Class"), TEXT("UnsupportedClass"));
	}

	if (!BodyObject->HasField(TEXT("ParentClass")))
	{
		return BodyFailure(TEXT("Body.ParentClass is required for UBlueprint documents"), TEXT("/Body/ParentClass"), TEXT("MissingParentClass"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		if (!IsKnownBodyKey(Pair.Key))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unknown UBlueprint Body key '%s'"), *Pair.Key),
				FString::Printf(TEXT("/Body/%s"), *Pair.Key),
				TEXT("UnknownBodyKey"));
		}

		if (IsProtectedRegion(Pair.Key))
		{
			const FAssetDocumentCapabilityResult ProtectedResult =
				RequireArrayOrNullValue(Pair.Value, FString::Printf(TEXT("/Body/%s"), *Pair.Key), Pair.Key);
			if (!ProtectedResult.bSuccess)
			{
				return ProtectedResult;
			}
		}

		if (Pair.Key == TEXT("ParentClass"))
		{
			const FAssetDocumentCapabilityResult ParentClassResult = ValidateParentClass(Pair.Value);
			if (!ParentClassResult.bSuccess)
			{
				return ParentClassResult;
			}
		}
	}

	TArray<FUBlueprintInterfaceSpec> InterfaceSpecs;
	const FAssetDocumentCapabilityResult InterfaceResult = ParseInterfaceSpecs(BodyObject, InterfaceSpecs);
	if (!InterfaceResult.bSuccess)
	{
		return InterfaceResult;
	}

	TArray<FUBlueprintVariableSpec> VariableSpecs;
	const FAssetDocumentCapabilityResult VariableResult = ParseVariableSpecs(BodyObject, VariableSpecs);
	if (!VariableResult.bSuccess)
	{
		return VariableResult;
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated UBlueprint Body scaffold"));
}
