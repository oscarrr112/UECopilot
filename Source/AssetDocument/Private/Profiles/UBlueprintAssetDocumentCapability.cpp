// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/UBlueprintAssetDocumentCapability.h"

#include "AssetDocumentPropertyAdapter.h"
#include "Profiles/UBlueprintGraphRegionAdapter.h"

#include "Utils/PropertySetterUtils.h"

#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

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
	return BodyKey == TEXT("FunctionGraphs")
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

FAssetDocumentCapabilityResult RequireArrayValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, const FString& BodyKey, const TArray<TSharedPtr<FJsonValue>>*& OutArray)
{
	OutArray = nullptr;
	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s must be an array when authored"), *BodyKey),
			Path,
			TEXT("InvalidBodySectionType"));
	}

	OutArray = &Value->AsArray();
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

struct FUBlueprintComponentKey
{
	FName Name;
	FString OwnerClass;
};

struct FUBlueprintComponentSpec
{
	FUBlueprintComponentKey Key;
	FString Scope;
	UClass* ComponentClass = nullptr;
	TOptional<FUBlueprintComponentKey> AttachTo;
	bool bRoot = false;
	TSharedPtr<FJsonObject> Properties;
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
	const TSharedPtr<FJsonValue>* VariablesValue = BodyObject->Values.Find(TEXT("Variables"));
	if (!VariablesValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
	const FAssetDocumentCapabilityResult ArrayResult = RequireArrayValue(*VariablesValue, TEXT("/Body/Variables"), TEXT("Variables"), Variables);
	if (!ArrayResult.bSuccess)
	{
		return ArrayResult;
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
	const TSharedPtr<FJsonValue>* InterfacesValue = BodyObject->Values.Find(TEXT("ImplementedInterfaces"));
	if (!InterfacesValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>* Interfaces = nullptr;
	const FAssetDocumentCapabilityResult ArrayResult = RequireArrayValue(*InterfacesValue, TEXT("/Body/ImplementedInterfaces"), TEXT("ImplementedInterfaces"), Interfaces);
	if (!ArrayResult.bSuccess)
	{
		return ArrayResult;
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

FString ComponentKeyToString(const FUBlueprintComponentKey& Key)
{
	return FString::Printf(TEXT("%s:%s"), *Key.OwnerClass, *Key.Name.ToString());
}

FString ComponentPath(const FUBlueprintComponentKey& Key)
{
	return FString::Printf(TEXT("/Body/Components/%s"), *ComponentKeyToString(Key));
}

bool IsSelfComponentKey(const FUBlueprintComponentKey& Key)
{
	return Key.OwnerClass == TEXT("Self");
}

bool IsDefaultSceneRootKey(const FUBlueprintComponentKey& Key)
{
	return IsSelfComponentKey(Key) && Key.Name == USceneComponent::GetDefaultSceneRootVariableName();
}

bool IsInheritedOrNativeComponentScope(const FString& Scope)
{
	return Scope == TEXT("Inherited") || Scope == TEXT("Native");
}

bool HasAuthoredProperties(const FUBlueprintComponentSpec& Spec)
{
	return Spec.Properties.IsValid() && Spec.Properties->Values.Num() > 0;
}

FAssetDocumentCapabilityResult ReadComponentKey(const TSharedPtr<FJsonObject>& Object, const FString& Path, FUBlueprintComponentKey& OutKey)
{
	OutKey = FUBlueprintComponentKey();
	if (!Object.IsValid())
	{
		return BodyFailure(TEXT("Component Key must be an object"), Path, TEXT("InvalidComponentKey"));
	}

	FString Name;
	if (!Object->TryGetStringField(TEXT("Name"), Name) || Name.IsEmpty())
	{
		return BodyFailure(TEXT("Component Key.Name is required"), Path / TEXT("Name"), TEXT("MissingComponentKeyName"));
	}

	FString OwnerClass;
	if (!Object->TryGetStringField(TEXT("OwnerClass"), OwnerClass) || OwnerClass.IsEmpty())
	{
		return BodyFailure(TEXT("Component Key.OwnerClass is required"), Path / TEXT("OwnerClass"), TEXT("MissingComponentKeyOwnerClass"));
	}

	OutKey.Name = FName(*Name);
	OutKey.OwnerClass = OwnerClass;
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ComponentPropertyFailure(const FAssetDocumentPropertyApplyResult& PropertyResult, const FString& Path)
{
	if (PropertyResult.Diagnostics.Num() > 0)
	{
		const FAssetDocumentDiagnostic& Diagnostic = PropertyResult.Diagnostics[0];
		const FString DiagnosticPath = Diagnostic.Path.IsEmpty() ? Path : Path / Diagnostic.Path;
		return BodyFailure(Diagnostic.Message, DiagnosticPath, Diagnostic.Code);
	}
	return BodyFailure(PropertyResult.Message, Path, TEXT("InvalidComponentProperties"));
}

FAssetDocumentPropertyApplyResult ApplyPropertiesDirectNoDuplicate(UObject* Object, TSharedPtr<FJsonObject> Properties)
{
	FAssetDocumentPropertyApplyResult Result;
	if (!Object)
	{
		Result.Message = TEXT("Object is required");
		return Result;
	}
	if (!Properties.IsValid() || Properties->Values.Num() == 0)
	{
		Result.bSuccess = true;
		Result.Message = TEXT("No properties to apply");
		return Result;
	}

	bool bAllSucceeded = true;
	Object->Modify();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
	{
		FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), *Pair.Key);
		if (!Property)
		{
			FAssetDocumentDiagnostic Diagnostic;
			Diagnostic.Path = FString::Printf(TEXT("Properties.%s"), *Pair.Key);
			Diagnostic.Code = TEXT("UnknownProperty");
			Diagnostic.Message = FString::Printf(TEXT("Property '%s' does not exist"), *Pair.Key);
			Result.Diagnostics.Add(Diagnostic);
			bAllSucceeded = false;
			continue;
		}

		const FString NonWritableReason = FAssetDocumentPropertyAdapter::GetNonWritableReason(Property);
		if (!NonWritableReason.IsEmpty())
		{
			FAssetDocumentDiagnostic Diagnostic;
			Diagnostic.Path = FString::Printf(TEXT("Properties.%s"), *Pair.Key);
			Diagnostic.Code = TEXT("NonWritable");
			Diagnostic.Message = FString::Printf(TEXT("Property '%s' is not writable: %s"), *Pair.Key, *NonWritableReason);
			Result.Diagnostics.Add(Diagnostic);
			bAllSucceeded = false;
			continue;
		}

		TSharedPtr<FJsonValue> ValueToApply = Pair.Value;
		if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Object)
		{
			TSharedPtr<FJsonObject> TypedObject = Pair.Value->AsObject();
			if (TypedObject.IsValid() && TypedObject->HasField(TEXT("type")) && TypedObject->HasField(TEXT("value")))
			{
				ValueToApply = TypedObject->TryGetField(TEXT("value"));
			}
		}

		if (!FPropertySetterUtils::SetPropertyFromJson(Object, Property, ValueToApply))
		{
			FAssetDocumentDiagnostic Diagnostic;
			Diagnostic.Path = FString::Printf(TEXT("Properties.%s"), *Pair.Key);
			Diagnostic.Code = TEXT("SetPropertyFailed");
			Diagnostic.Message = FString::Printf(TEXT("Failed to set property '%s'"), *Pair.Key);
			Result.Diagnostics.Add(Diagnostic);
			bAllSucceeded = false;
		}
	}

	Result.bSuccess = bAllSucceeded;
	Result.Message = bAllSucceeded ? TEXT("Properties applied") : TEXT("Property apply failed");
	return Result;
}

FAssetDocumentPropertyApplyResult PreflightPropertiesOnObject(UObject* Object, TSharedPtr<FJsonObject> Properties)
{
	FAssetDocumentPropertyApplyResult Result;
	if (!Object)
	{
		Result.Message = TEXT("Object is required");
		return Result;
	}
	if (!Properties.IsValid() || Properties->Values.Num() == 0)
	{
		Result.bSuccess = true;
		Result.Message = TEXT("No properties to preflight");
		return Result;
	}

	UObject* PreflightObject = DuplicateObject<UObject>(Object, GetTransientPackage());
	if (!PreflightObject)
	{
		Result.Message = TEXT("Failed to create transient property preflight object");
		return Result;
	}

	return ApplyPropertiesDirectNoDuplicate(PreflightObject, Properties);
}

FAssetDocumentCapabilityResult ParseComponentSpecs(const TSharedPtr<FJsonObject>& BodyObject, TArray<FUBlueprintComponentSpec>& OutComponents)
{
	OutComponents.Reset();
	const TSharedPtr<FJsonValue>* ComponentsValue = BodyObject->Values.Find(TEXT("Components"));
	if (!ComponentsValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
	const FAssetDocumentCapabilityResult ArrayResult = RequireArrayValue(*ComponentsValue, TEXT("/Body/Components"), TEXT("Components"), Components);
	if (!ArrayResult.bSuccess)
	{
		return ArrayResult;
	}

	TSet<FString> SeenKeys;
	for (int32 Index = 0; Index < Components->Num(); ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Body/Components/%d"), Index);
		const TSharedPtr<FJsonObject> ComponentObject = (*Components)[Index].IsValid() ? (*Components)[Index]->AsObject() : nullptr;
		if (!ComponentObject.IsValid())
		{
			return BodyFailure(TEXT("Body.Components entries must be objects"), Path, TEXT("InvalidComponent"));
		}

		FUBlueprintComponentSpec Spec;
		const TSharedPtr<FJsonObject>* KeyObject = nullptr;
		if (!ComponentObject->TryGetObjectField(TEXT("Key"), KeyObject) || !KeyObject || !KeyObject->IsValid())
		{
			return BodyFailure(TEXT("Component Key object is required"), Path / TEXT("Key"), TEXT("MissingComponentKey"));
		}

		const FAssetDocumentCapabilityResult KeyResult = ReadComponentKey(*KeyObject, Path / TEXT("Key"), Spec.Key);
		if (!KeyResult.bSuccess)
		{
			return KeyResult;
		}

		const FString KeyString = ComponentKeyToString(Spec.Key);
		if (SeenKeys.Contains(KeyString))
		{
			return BodyFailure(
				FString::Printf(TEXT("Duplicate Body.Components Key '%s'"), *KeyString),
				Path / TEXT("Key"),
				TEXT("DuplicateComponentKey"));
		}
		SeenKeys.Add(KeyString);

		if (!ComponentObject->TryGetStringField(TEXT("Scope"), Spec.Scope) || Spec.Scope.IsEmpty())
		{
			return BodyFailure(TEXT("Component Scope is required"), Path / TEXT("Scope"), TEXT("MissingComponentScope"));
		}
		if (Spec.Scope != TEXT("OwnedSCS") && Spec.Scope != TEXT("Inherited") && Spec.Scope != TEXT("Native"))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unsupported component Scope '%s'"), *Spec.Scope),
				Path / TEXT("Scope"),
				TEXT("UnsupportedComponentScope"));
		}
		if (Spec.Scope == TEXT("OwnedSCS") && !IsSelfComponentKey(Spec.Key))
		{
			return BodyFailure(TEXT("OwnedSCS components require Key.OwnerClass == Self"), Path / TEXT("Key/OwnerClass"), TEXT("InvalidOwnedSCSOwnerClass"));
		}
		if (Spec.Scope == TEXT("OwnedSCS") && IsDefaultSceneRootKey(Spec.Key))
		{
			return BodyFailure(TEXT("DefaultSceneRoot is reserved for the generated default scene root"), Path / TEXT("Key/Name"), TEXT("ProtectedComponentName"));
		}

		FString ClassPath;
		if (!ComponentObject->TryGetStringField(TEXT("Class"), ClassPath) || ClassPath.IsEmpty())
		{
			return BodyFailure(TEXT("Component Class is required"), Path / TEXT("Class"), TEXT("MissingComponentClass"));
		}
		Spec.ComponentClass = StaticLoadClass(UActorComponent::StaticClass(), nullptr, *ClassPath);
		if (!Spec.ComponentClass || !Spec.ComponentClass->IsChildOf(UActorComponent::StaticClass()))
		{
			return BodyFailure(
				FString::Printf(TEXT("Component Class '%s' must resolve to a UActorComponent subclass"), *ClassPath),
				Path / TEXT("Class"),
				TEXT("InvalidComponentClass"));
		}

		const TSharedPtr<FJsonObject>* AttachToObject = nullptr;
		if (ComponentObject->TryGetObjectField(TEXT("AttachTo"), AttachToObject))
		{
			FUBlueprintComponentKey AttachToKey;
			const FAssetDocumentCapabilityResult AttachResult = ReadComponentKey(*AttachToObject, Path / TEXT("AttachTo"), AttachToKey);
			if (!AttachResult.bSuccess)
			{
				return AttachResult;
			}
			Spec.AttachTo = AttachToKey;
		}

		bool bRoot = false;
		if (ComponentObject->TryGetBoolField(TEXT("Root"), bRoot))
		{
			Spec.bRoot = bRoot;
		}
		if (IsInheritedOrNativeComponentScope(Spec.Scope) && (Spec.AttachTo.IsSet() || Spec.bRoot))
		{
			return BodyFailure(
				TEXT("Inherited and Native component AttachTo/Root overrides are not supported yet"),
				ComponentPath(Spec.Key) / (Spec.AttachTo.IsSet() ? TEXT("AttachTo") : TEXT("Root")),
				TEXT("UnsupportedInheritedComponentAttachRoot"));
		}
		if (Spec.bRoot && !Spec.ComponentClass->IsChildOf(USceneComponent::StaticClass()))
		{
			return BodyFailure(TEXT("Root components must be USceneComponent subclasses"), Path / TEXT("Root"), TEXT("InvalidRootComponentClass"));
		}

		const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
		if (ComponentObject->TryGetObjectField(TEXT("Properties"), PropertiesObject) && PropertiesObject && PropertiesObject->IsValid())
		{
			Spec.Properties = *PropertiesObject;
		}
		else
		{
			Spec.Properties = MakeShared<FJsonObject>();
		}

		OutComponents.Add(MoveTemp(Spec));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult PreflightComponentProperties(const TArray<FUBlueprintComponentSpec>& Components)
{
	for (const FUBlueprintComponentSpec& Component : Components)
	{
		if (!Component.Properties.IsValid() || Component.Properties->Values.Num() == 0)
		{
			continue;
		}

		const FAssetDocumentPropertyApplyResult PreflightResult =
			FAssetDocumentPropertyAdapter::PreflightProperties(Component.ComponentClass, Component.Properties);
		if (!PreflightResult.bSuccess)
		{
			return ComponentPropertyFailure(PreflightResult, ComponentPath(Component.Key) / TEXT("Properties"));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

bool HasOwnedSCSComponent(const TArray<FUBlueprintComponentSpec>& Components)
{
	return Components.ContainsByPredicate([](const FUBlueprintComponentSpec& Spec)
	{
		return Spec.Scope == TEXT("OwnedSCS");
	});
}

bool HasInheritedOrNativeComponent(const TArray<FUBlueprintComponentSpec>& Components)
{
	return Components.ContainsByPredicate([](const FUBlueprintComponentSpec& Spec)
	{
		return IsInheritedOrNativeComponentScope(Spec.Scope);
	});
}

void CollectOwnedSCSComponents(
	const TArray<FUBlueprintComponentSpec>& Specs,
	TArray<const FUBlueprintComponentSpec*>& OutOwnedSpecs,
	TMap<FString, const FUBlueprintComponentSpec*>& OutOwnedByKey)
{
	OutOwnedSpecs.Reset();
	OutOwnedByKey.Reset();
	for (const FUBlueprintComponentSpec& Spec : Specs)
	{
		if (Spec.Scope == TEXT("OwnedSCS"))
		{
			OutOwnedSpecs.Add(&Spec);
			OutOwnedByKey.Add(ComponentKeyToString(Spec.Key), &Spec);
		}
	}
}

void CollectInheritedComponents(
	const TArray<FUBlueprintComponentSpec>& Specs,
	TArray<const FUBlueprintComponentSpec*>& OutInheritedSpecs)
{
	OutInheritedSpecs.Reset();
	for (const FUBlueprintComponentSpec& Spec : Specs)
	{
		if (Spec.Scope == TEXT("Inherited") && HasAuthoredProperties(Spec))
		{
			OutInheritedSpecs.Add(&Spec);
		}
	}
}

void CollectNativeComponents(
	const TArray<FUBlueprintComponentSpec>& Specs,
	TArray<const FUBlueprintComponentSpec*>& OutNativeSpecs,
	TSet<FString>& OutNativeKeys)
{
	OutNativeSpecs.Reset();
	OutNativeKeys.Reset();
	for (const FUBlueprintComponentSpec& Spec : Specs)
	{
		if (Spec.Scope == TEXT("Native") && HasAuthoredProperties(Spec))
		{
			OutNativeSpecs.Add(&Spec);
			OutNativeKeys.Add(ComponentKeyToString(Spec.Key));
		}
	}
}

USCS_Node* FindSCSNodeByKey(USimpleConstructionScript* SCS, const FUBlueprintComponentKey& Key);

FAssetDocumentCapabilityResult PreflightOwnedSCSStructure(
	const UBlueprint* Blueprint,
	const TArray<const FUBlueprintComponentSpec*>& DesiredOwnedSpecs,
	const TMap<FString, const FUBlueprintComponentSpec*>& DesiredOwnedByKey)
{
	int32 RootCount = 0;
	TMap<FString, FString> DesiredAttachParentByChild;

	for (const FUBlueprintComponentSpec* SpecPtr : DesiredOwnedSpecs)
	{
		const FUBlueprintComponentSpec& Spec = *SpecPtr;
		const FString SpecKeyString = ComponentKeyToString(Spec.Key);
		if (Spec.bRoot)
		{
			++RootCount;
			if (RootCount > 1)
			{
				return BodyFailure(TEXT("Only one OwnedSCS component can declare Root:true"), ComponentPath(Spec.Key) / TEXT("Root"), TEXT("MultipleRootComponents"));
			}
			if (Spec.AttachTo.IsSet())
			{
				return BodyFailure(TEXT("Root components cannot also declare AttachTo"), ComponentPath(Spec.Key) / TEXT("AttachTo"), TEXT("RootComponentCannotAttach"));
			}
		}

		if (!Spec.AttachTo.IsSet())
		{
			continue;
		}

		if (!Spec.ComponentClass || !Spec.ComponentClass->IsChildOf(USceneComponent::StaticClass()))
		{
			return BodyFailure(TEXT("Only scene components can use AttachTo"), ComponentPath(Spec.Key) / TEXT("AttachTo"), TEXT("InvalidComponentAttach"));
		}

		const FUBlueprintComponentKey AttachToKey = Spec.AttachTo.GetValue();
		const FString AttachToKeyString = ComponentKeyToString(AttachToKey);
		if (AttachToKeyString == SpecKeyString)
		{
			return BodyFailure(TEXT("Component cannot attach to itself"), ComponentPath(Spec.Key) / TEXT("AttachTo"), TEXT("InvalidComponentAttach"));
		}

		if (const FUBlueprintComponentSpec* const* DesiredParent = DesiredOwnedByKey.Find(AttachToKeyString))
		{
			const FUBlueprintComponentSpec* ParentSpec = *DesiredParent;
			if (!ParentSpec->ComponentClass || !ParentSpec->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
			{
				return BodyFailure(TEXT("AttachTo target must be a scene component"), ComponentPath(Spec.Key) / TEXT("AttachTo"), TEXT("InvalidAttachParent"));
			}
			DesiredAttachParentByChild.Add(SpecKeyString, AttachToKeyString);
			continue;
		}

		if (IsDefaultSceneRootKey(AttachToKey))
		{
			continue;
		}

		if (IsSelfComponentKey(AttachToKey))
		{
			return BodyFailure(
				FString::Printf(TEXT("AttachTo component '%s' is not present in the authoritative OwnedSCS component set"), *AttachToKeyString),
				ComponentPath(Spec.Key) / TEXT("AttachTo"),
				TEXT("AttachParentRemovedByAuthoritativeRegion"));
		}

		USCS_Node* ExistingParent = Blueprint && Blueprint->SimpleConstructionScript
			? FindSCSNodeByKey(Blueprint->SimpleConstructionScript, AttachToKey)
			: nullptr;
		if (!ExistingParent)
		{
			return BodyFailure(
				FString::Printf(TEXT("AttachTo component '%s' was not found"), *AttachToKeyString),
				ComponentPath(Spec.Key) / TEXT("AttachTo"),
				TEXT("MissingAttachParent"));
		}
		if (!ExistingParent->ComponentClass || !ExistingParent->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
		{
			return BodyFailure(TEXT("AttachTo target must be a scene component"), ComponentPath(Spec.Key) / TEXT("AttachTo"), TEXT("InvalidAttachParent"));
		}
	}

	for (const FUBlueprintComponentSpec* SpecPtr : DesiredOwnedSpecs)
	{
		const FUBlueprintComponentSpec& Spec = *SpecPtr;
		const FString StartKey = ComponentKeyToString(Spec.Key);
		TSet<FString> SeenKeys;
		FString CurrentKey = StartKey;
		while (const FString* ParentKey = DesiredAttachParentByChild.Find(CurrentKey))
		{
			if (*ParentKey == StartKey || SeenKeys.Contains(*ParentKey))
			{
				return BodyFailure(TEXT("OwnedSCS AttachTo declarations cannot form a cycle"), ComponentPath(Spec.Key) / TEXT("AttachTo"), TEXT("ComponentAttachCycle"));
			}
			SeenKeys.Add(CurrentKey);
			CurrentKey = *ParentKey;
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

void ApplyVariableMetadata(UBlueprint* Blueprint, const FUBlueprintVariableSpec& Spec)
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

FAssetDocumentCapabilityResult ValidateVariablesAgainstParentClass(UClass* ParentClass, const TArray<FUBlueprintVariableSpec>& Variables)
{
	for (const FUBlueprintVariableSpec& Variable : Variables)
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
			if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Spec.Name, Spec.Type))
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

USimpleConstructionScript* EnsureSimpleConstructionScript(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	if (!Blueprint->SimpleConstructionScript)
	{
		Blueprint->Modify();
		Blueprint->SimpleConstructionScript = NewObject<USimpleConstructionScript>(Blueprint);
	}

	if (Blueprint->SimpleConstructionScript)
	{
		Blueprint->SimpleConstructionScript->ValidateSceneRootNodes();
	}
	return Blueprint->SimpleConstructionScript;
}

USCS_Node* FindSCSNodeByKey(USimpleConstructionScript* SCS, const FUBlueprintComponentKey& Key)
{
	if (!SCS || !IsSelfComponentKey(Key))
	{
		return nullptr;
	}
	return SCS->FindSCSNode(Key.Name);
}

bool IsDefaultSceneRootNode(const USCS_Node* Node, const USimpleConstructionScript* SCS)
{
	return Node
		&& SCS
		&& Node == SCS->GetDefaultSceneRootNode()
		&& Node->GetVariableName() == USceneComponent::GetDefaultSceneRootVariableName();
}

void DetachNodeFromCurrentParent(USimpleConstructionScript* SCS, USCS_Node* Node)
{
	if (!SCS || !Node)
	{
		return;
	}
	SCS->RemoveNode(Node, false);
}

FAssetDocumentCapabilityResult AttachOwnedNode(USimpleConstructionScript* SCS, USCS_Node* Node, const FUBlueprintComponentSpec& Spec)
{
	if (!SCS || !Node)
	{
		return BodyFailure(TEXT("Missing SCS node while applying owned component"), ComponentPath(Spec.Key), TEXT("MissingSCSNode"));
	}

	if (Spec.AttachTo.IsSet())
	{
		if (!Node->ComponentClass || !Node->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
		{
			return BodyFailure(TEXT("Only scene components can use AttachTo"), ComponentPath(Spec.Key) / TEXT("AttachTo"), TEXT("InvalidComponentAttach"));
		}

		USCS_Node* ParentNode = FindSCSNodeByKey(SCS, Spec.AttachTo.GetValue());
		if (!ParentNode)
		{
			return BodyFailure(
				FString::Printf(TEXT("AttachTo component '%s' was not found"), *ComponentKeyToString(Spec.AttachTo.GetValue())),
				ComponentPath(Spec.Key) / TEXT("AttachTo"),
				TEXT("MissingAttachParent"));
		}
		if (ParentNode == Node)
		{
			return BodyFailure(TEXT("Component cannot attach to itself"), ComponentPath(Spec.Key) / TEXT("AttachTo"), TEXT("InvalidComponentAttach"));
		}
		if (!ParentNode->ComponentClass || !ParentNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
		{
			return BodyFailure(TEXT("AttachTo target must be a scene component"), ComponentPath(Spec.Key) / TEXT("AttachTo"), TEXT("InvalidAttachParent"));
		}

		DetachNodeFromCurrentParent(SCS, Node);
		ParentNode->AddChildNode(Node);
		return FAssetDocumentCapabilityResult::Success();
	}

	DetachNodeFromCurrentParent(SCS, Node);
	SCS->AddNode(Node);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyOwnedSCSComponents(UBlueprint* Blueprint, const TArray<FUBlueprintComponentSpec>& Specs, bool& bOutChanged)
{
	bOutChanged = false;
	TArray<const FUBlueprintComponentSpec*> DesiredOwnedSpecs;
	TMap<FString, const FUBlueprintComponentSpec*> DesiredOwnedByKey;
	CollectOwnedSCSComponents(Specs, DesiredOwnedSpecs, DesiredOwnedByKey);
	bOutChanged = DesiredOwnedSpecs.Num() > 0;
	if (DesiredOwnedSpecs.Num() > 0 && (!Blueprint || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass())))
	{
		return BodyFailure(TEXT("OwnedSCS components require an Actor-derived Blueprint parent class"), TEXT("/Body/Components"), TEXT("OwnedSCSRequiresActorParent"));
	}

	const FAssetDocumentCapabilityResult StructureResult = PreflightOwnedSCSStructure(Blueprint, DesiredOwnedSpecs, DesiredOwnedByKey);
	if (!StructureResult.bSuccess)
	{
		return StructureResult;
	}

	USimpleConstructionScript* SCS = nullptr;
	if (DesiredOwnedSpecs.Num() > 0)
	{
		SCS = EnsureSimpleConstructionScript(Blueprint);
	}
	else if (Blueprint)
	{
		SCS = Blueprint->SimpleConstructionScript.Get();
	}
	if (!SCS && DesiredOwnedSpecs.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (!SCS)
	{
		return BodyFailure(TEXT("Failed to create SimpleConstructionScript for UBlueprint"), TEXT("/Body/Components"), TEXT("MissingSimpleConstructionScript"));
	}

	TArray<USCS_Node*> ExistingNodes = SCS->GetAllNodes();
	for (USCS_Node* Node : ExistingNodes)
	{
		if (!Node || IsDefaultSceneRootNode(Node, SCS))
		{
			continue;
		}

		FUBlueprintComponentKey ExistingKey;
		ExistingKey.Name = Node->GetVariableName();
		ExistingKey.OwnerClass = TEXT("Self");
		if (!DesiredOwnedByKey.Contains(ComponentKeyToString(ExistingKey)))
		{
			SCS->RemoveNode(Node, false);
			bOutChanged = true;
		}
	}

	TMap<FString, USCS_Node*> NodesByKey;
	for (const FUBlueprintComponentSpec* SpecPtr : DesiredOwnedSpecs)
	{
		const FUBlueprintComponentSpec& Spec = *SpecPtr;
		const FString SpecKeyString = ComponentKeyToString(Spec.Key);
		USCS_Node* Node = FindSCSNodeByKey(SCS, Spec.Key);
		if (!Node)
		{
			Node = SCS->CreateNode(Spec.ComponentClass, Spec.Key.Name);
		}
		if (!Node)
		{
			return BodyFailure(
				FString::Printf(TEXT("Failed to create owned SCS component '%s'"), *ComponentKeyToString(Spec.Key)),
				ComponentPath(Spec.Key),
				TEXT("CreateSCSNodeFailed"));
		}
		if (Node->ComponentClass.Get() != Spec.ComponentClass)
		{
			SCS->RemoveNode(Node, false);
			Node = SCS->CreateNode(Spec.ComponentClass, Spec.Key.Name);
			if (!Node)
			{
				return BodyFailure(
					FString::Printf(TEXT("Failed to recreate owned SCS component '%s'"), *ComponentKeyToString(Spec.Key)),
					ComponentPath(Spec.Key),
					TEXT("CreateSCSNodeFailed"));
			}
		}
		NodesByKey.Add(SpecKeyString, Node);
	}

	for (const FUBlueprintComponentSpec* SpecPtr : DesiredOwnedSpecs)
	{
		const FUBlueprintComponentSpec& Spec = *SpecPtr;
		USCS_Node* Node = NodesByKey.FindRef(ComponentKeyToString(Spec.Key));
		if (Spec.bRoot)
		{
			DetachNodeFromCurrentParent(SCS, Node);
			SCS->AddNode(Node);
			continue;
		}

		const FAssetDocumentCapabilityResult AttachResult = AttachOwnedNode(SCS, Node, Spec);
		if (!AttachResult.bSuccess)
		{
			return AttachResult;
		}
	}

	for (const FUBlueprintComponentSpec* SpecPtr : DesiredOwnedSpecs)
	{
		const FUBlueprintComponentSpec& Spec = *SpecPtr;
		USCS_Node* Node = NodesByKey.FindRef(ComponentKeyToString(Spec.Key));
		if (!Node || !Node->ComponentTemplate)
		{
			return BodyFailure(
				FString::Printf(TEXT("Owned SCS component '%s' has no component template"), *ComponentKeyToString(Spec.Key)),
				ComponentPath(Spec.Key),
				TEXT("MissingComponentTemplate"));
		}

		const FAssetDocumentPropertyApplyResult PropertyResult =
			FAssetDocumentPropertyAdapter::ApplyProperties(Node->ComponentTemplate, Spec.Properties);
		if (!PropertyResult.bSuccess)
		{
			return ComponentPropertyFailure(PropertyResult, ComponentPath(Spec.Key) / TEXT("Properties"));
		}
	}

	SCS->ValidateSceneRootNodes();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ResolveInheritedComponentKey(
	UBlueprint* Blueprint,
	const FUBlueprintComponentSpec& Spec,
	FComponentKey& OutKey,
	UActorComponent*& OutOriginalTemplate)
{
	OutKey = FComponentKey();
	OutOriginalTemplate = nullptr;
	if (!Blueprint)
	{
		return BodyFailure(TEXT("Blueprint is required to resolve inherited component key"), ComponentPath(Spec.Key), TEXT("MissingBlueprint"));
	}
	if (IsSelfComponentKey(Spec.Key))
	{
		return BodyFailure(TEXT("Inherited components require Key.OwnerClass to name the parent generated class"), ComponentPath(Spec.Key) / TEXT("Key/OwnerClass"), TEXT("InvalidInheritedComponentOwnerClass"));
	}

	UClass* OwnerClass = StaticLoadClass(UObject::StaticClass(), nullptr, *Spec.Key.OwnerClass);
	UBlueprintGeneratedClass* OwnerGeneratedClass = Cast<UBlueprintGeneratedClass>(OwnerClass);
	if (!OwnerGeneratedClass)
	{
		return BodyFailure(
			FString::Printf(TEXT("Inherited component OwnerClass '%s' must resolve to a Blueprint generated class"), *Spec.Key.OwnerClass),
			ComponentPath(Spec.Key) / TEXT("Key/OwnerClass"),
			TEXT("UnresolvedInheritedComponentOwnerClass"));
	}
	if (Blueprint->GeneratedClass && !Blueprint->GeneratedClass->IsChildOf(OwnerGeneratedClass))
	{
		return BodyFailure(
			FString::Printf(TEXT("Inherited component OwnerClass '%s' is not in the Blueprint parent chain"), *Spec.Key.OwnerClass),
			ComponentPath(Spec.Key) / TEXT("Key/OwnerClass"),
			TEXT("InvalidInheritedComponentOwnerClass"));
	}
	if (!OwnerGeneratedClass->SimpleConstructionScript)
	{
		return BodyFailure(
			FString::Printf(TEXT("Inherited component OwnerClass '%s' has no SimpleConstructionScript"), *Spec.Key.OwnerClass),
			ComponentPath(Spec.Key) / TEXT("Key/OwnerClass"),
			TEXT("MissingInheritedComponentSCS"));
	}

	USCS_Node* InheritedNode = OwnerGeneratedClass->SimpleConstructionScript->FindSCSNode(Spec.Key.Name);
	if (!InheritedNode || !InheritedNode->ComponentTemplate)
	{
		return BodyFailure(
			FString::Printf(TEXT("Inherited component '%s' was not found on '%s'"), *Spec.Key.Name.ToString(), *Spec.Key.OwnerClass),
			ComponentPath(Spec.Key),
			TEXT("MissingInheritedComponent"));
	}
	if (Spec.ComponentClass && !InheritedNode->ComponentTemplate->IsA(Spec.ComponentClass))
	{
		return BodyFailure(
			FString::Printf(TEXT("Inherited component '%s' class '%s' is not compatible with declared class '%s'"),
				*Spec.Key.Name.ToString(),
				*GetClassPath(InheritedNode->ComponentTemplate->GetClass()),
				*GetClassPath(Spec.ComponentClass)),
			ComponentPath(Spec.Key) / TEXT("Class"),
			TEXT("ComponentClassMismatch"));
	}

	OutKey = FComponentKey(InheritedNode);
	if (!OutKey.IsValid())
	{
		return BodyFailure(
			FString::Printf(TEXT("Failed to construct inherited component key for '%s'"), *ComponentKeyToString(Spec.Key)),
			ComponentPath(Spec.Key),
			TEXT("InvalidInheritedComponentKey"));
	}
	OutOriginalTemplate = InheritedNode->ComponentTemplate;
	return FAssetDocumentCapabilityResult::Success();
}

bool ComponentKeyMatchesAny(const FComponentKey& Key, const TArray<FComponentKey>& DesiredKeys)
{
	for (const FComponentKey& DesiredKey : DesiredKeys)
	{
		if (Key.Match(DesiredKey))
		{
			return true;
		}
	}
	return false;
}

FAssetDocumentCapabilityResult ApplyInheritedComponentOverrides(UBlueprint* Blueprint, const TArray<FUBlueprintComponentSpec>& Specs, bool& bOutChanged)
{
	bOutChanged = false;
	TArray<const FUBlueprintComponentSpec*> DesiredInheritedSpecs;
	CollectInheritedComponents(Specs, DesiredInheritedSpecs);

	TArray<FComponentKey> DesiredKeys;
	struct FResolvedInheritedSpec
	{
		const FUBlueprintComponentSpec* Spec = nullptr;
		FComponentKey Key;
	};
	TArray<FResolvedInheritedSpec> ResolvedSpecs;

	for (const FUBlueprintComponentSpec* SpecPtr : DesiredInheritedSpecs)
	{
		FComponentKey ResolvedKey;
		UActorComponent* OriginalTemplate = nullptr;
		const FAssetDocumentCapabilityResult ResolveResult = ResolveInheritedComponentKey(Blueprint, *SpecPtr, ResolvedKey, OriginalTemplate);
		if (!ResolveResult.bSuccess)
		{
			return ResolveResult;
		}

		DesiredKeys.Add(ResolvedKey);
		FResolvedInheritedSpec Resolved;
		Resolved.Spec = SpecPtr;
		Resolved.Key = ResolvedKey;
		ResolvedSpecs.Add(MoveTemp(Resolved));
	}

	UInheritableComponentHandler* ExistingHandler = Blueprint ? Blueprint->GetInheritableComponentHandler(false) : nullptr;
	if (ExistingHandler)
	{
		TArray<FComponentKey> KeysToRemove;
		for (auto RecordIt = ExistingHandler->CreateRecordIterator(); RecordIt; ++RecordIt)
		{
			const FComponentKey ExistingKey = RecordIt->ComponentKey;
			if (ExistingKey.IsSCSKey() && !ComponentKeyMatchesAny(ExistingKey, DesiredKeys))
			{
				KeysToRemove.Add(ExistingKey);
			}
		}

		for (const FComponentKey& KeyToRemove : KeysToRemove)
		{
			ExistingHandler->Modify();
			ExistingHandler->RemoveOverridenComponentTemplate(KeyToRemove);
			bOutChanged = true;
		}
	}

	if (ResolvedSpecs.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	UInheritableComponentHandler* Handler = Blueprint->GetInheritableComponentHandler(true);
	if (!Handler)
	{
		return BodyFailure(TEXT("Failed to create inheritable component handler"), TEXT("/Body/Components"), TEXT("MissingInheritableComponentHandler"));
	}

	for (const FResolvedInheritedSpec& Resolved : ResolvedSpecs)
	{
		const FUBlueprintComponentSpec& Spec = *Resolved.Spec;
		Handler->Modify();
		UActorComponent* OverrideTemplate = Handler->CreateOverridenComponentTemplate(Resolved.Key);
		if (!OverrideTemplate)
		{
			return BodyFailure(
				FString::Printf(TEXT("Failed to create inherited component override template for '%s'"), *ComponentKeyToString(Spec.Key)),
				ComponentPath(Spec.Key),
				TEXT("CreateInheritedOverrideTemplateFailed"));
		}

		const FAssetDocumentPropertyApplyResult PropertyResult =
			ApplyPropertiesDirectNoDuplicate(OverrideTemplate, Spec.Properties);
		if (!PropertyResult.bSuccess)
		{
			return ComponentPropertyFailure(PropertyResult, ComponentPath(Spec.Key) / TEXT("Properties"));
		}
		bOutChanged = true;
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

FAssetDocumentCapabilityResult ResetWritablePropertiesFromBaseline(UObject* Target, UObject* Baseline, const FString& Path)
{
	if (!Target || !Baseline)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	Target->Modify();
	for (TFieldIterator<FProperty> PropertyIt(Target->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!FAssetDocumentPropertyAdapter::IsWritableProperty(Property))
		{
			continue;
		}

		FProperty* BaselineProperty = FindFProperty<FProperty>(Baseline->GetClass(), Property->GetFName());
		if (!BaselineProperty || !BaselineProperty->SameType(Property))
		{
			continue;
		}

		void* TargetValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
		const void* BaselineValuePtr = BaselineProperty->ContainerPtrToValuePtr<void>(Baseline);
		if (!Property->Identical(TargetValuePtr, BaselineValuePtr))
		{
			Property->CopyCompleteValue(TargetValuePtr, BaselineValuePtr);
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

void CollectActorComponentDefaultSubobjects(UObject* Object, TArray<UActorComponent*>& OutComponents)
{
	OutComponents.Reset();
	if (!Object)
	{
		return;
	}

	TArray<UObject*> DefaultSubobjects;
	Object->GetDefaultSubobjects(DefaultSubobjects);
	for (UObject* DefaultSubobject : DefaultSubobjects)
	{
		if (UActorComponent* Component = Cast<UActorComponent>(DefaultSubobject))
		{
			OutComponents.Add(Component);
		}
	}
}

UActorComponent* FindComponentByObjectName(UObject* Owner, FName ComponentName, UClass* ComponentClass, bool& bOutAmbiguous)
{
	bOutAmbiguous = false;
	TArray<UActorComponent*> Components;
	CollectActorComponentDefaultSubobjects(Owner, Components);

	UActorComponent* Match = nullptr;
	for (UActorComponent* Component : Components)
	{
		if (!Component || Component->GetFName() != ComponentName)
		{
			continue;
		}
		if (ComponentClass && !Component->IsA(ComponentClass))
		{
			continue;
		}
		if (Match)
		{
			bOutAmbiguous = true;
			return nullptr;
		}
		Match = Component;
	}
	return Match;
}

UActorComponent* FindComponentByAliasProperty(UObject* Owner, FName AliasName, UClass* ComponentClass)
{
	if (!Owner)
	{
		return nullptr;
	}

	FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), AliasName);
	if (!Property)
	{
		return nullptr;
	}

	UObject* Value = Property->GetObjectPropertyValue_InContainer(Owner);
	UActorComponent* Component = Cast<UActorComponent>(Value);
	if (!Component || (ComponentClass && !Component->IsA(ComponentClass)))
	{
		return nullptr;
	}
	return Component;
}

FName FindAliasPropertyNameForComponent(UObject* Owner, UActorComponent* Component)
{
	if (!Owner || !Component)
	{
		return NAME_None;
	}

	for (TFieldIterator<FObjectPropertyBase> PropertyIt(Owner->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FObjectPropertyBase* Property = *PropertyIt;
		UObject* Value = Property->GetObjectPropertyValue_InContainer(Owner);
		if (Value == Component)
		{
			return Property->GetFName();
		}
	}
	return NAME_None;
}

UClass* FindNativeOwnerClassForComponent(UClass* StartClass, UActorComponent* Component)
{
	if (!StartClass || !Component)
	{
		return nullptr;
	}

	for (UClass* CandidateClass = StartClass; CandidateClass; CandidateClass = CandidateClass->GetSuperClass())
	{
		if (Cast<UBlueprintGeneratedClass>(CandidateClass))
		{
			continue;
		}

		UObject* CandidateCDO = CandidateClass->GetDefaultObject(false);
		bool bAmbiguous = false;
		UActorComponent* CandidateComponent = FindComponentByObjectName(CandidateCDO, Component->GetFName(), Component->GetClass(), bAmbiguous);
		if (CandidateComponent && !bAmbiguous)
		{
			return CandidateClass;
		}
	}
	return nullptr;
}

FAssetDocumentCapabilityResult ResolveNativeComponent(
	UBlueprint* Blueprint,
	const FUBlueprintComponentSpec& Spec,
	UActorComponent*& OutGeneratedComponent,
	UActorComponent*& OutBaselineComponent)
{
	OutGeneratedComponent = nullptr;
	OutBaselineComponent = nullptr;
	UBlueprintGeneratedClass* GeneratedClass = Blueprint ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass) : nullptr;
	UObject* GeneratedCDO = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
	UClass* ParentClass = GeneratedClass ? GeneratedClass->GetSuperClass() : nullptr;
	UObject* ParentCDO = ParentClass ? ParentClass->GetDefaultObject(false) : nullptr;
	if (!GeneratedClass || !GeneratedCDO || !ParentCDO)
	{
		return BodyFailure(TEXT("Native component overrides require a compiled Blueprint generated class"), ComponentPath(Spec.Key), TEXT("MissingGeneratedCDO"));
	}
	if (IsSelfComponentKey(Spec.Key))
	{
		return BodyFailure(TEXT("Native components require Key.OwnerClass to name the native owner class"), ComponentPath(Spec.Key) / TEXT("Key/OwnerClass"), TEXT("InvalidNativeComponentOwnerClass"));
	}

	UClass* OwnerClass = StaticLoadClass(UObject::StaticClass(), nullptr, *Spec.Key.OwnerClass);
	if (!OwnerClass || !OwnerClass->IsChildOf(AActor::StaticClass()))
	{
		return BodyFailure(
			FString::Printf(TEXT("Native component OwnerClass '%s' must resolve to an Actor class"), *Spec.Key.OwnerClass),
			ComponentPath(Spec.Key) / TEXT("Key/OwnerClass"),
			TEXT("UnresolvedNativeComponentOwnerClass"));
	}
	if (!GeneratedClass->IsChildOf(OwnerClass))
	{
		return BodyFailure(
			FString::Printf(TEXT("Native component OwnerClass '%s' is not in the Blueprint parent chain"), *Spec.Key.OwnerClass),
			ComponentPath(Spec.Key) / TEXT("Key/OwnerClass"),
			TEXT("InvalidNativeComponentOwnerClass"));
	}

	OutGeneratedComponent = FindComponentByAliasProperty(GeneratedCDO, Spec.Key.Name, Spec.ComponentClass);
	OutBaselineComponent = FindComponentByAliasProperty(ParentCDO, Spec.Key.Name, Spec.ComponentClass);
	if (!OutGeneratedComponent)
	{
		bool bAmbiguous = false;
		OutGeneratedComponent = FindComponentByObjectName(GeneratedCDO, Spec.Key.Name, Spec.ComponentClass, bAmbiguous);
		if (bAmbiguous)
		{
			return BodyFailure(
				FString::Printf(TEXT("Native component key '%s' matched multiple generated CDO component templates"), *ComponentKeyToString(Spec.Key)),
				ComponentPath(Spec.Key),
				TEXT("AmbiguousNativeComponent"));
		}
		if (OutGeneratedComponent)
		{
			bool bBaselineAmbiguous = false;
			OutBaselineComponent = FindComponentByObjectName(ParentCDO, OutGeneratedComponent->GetFName(), Spec.ComponentClass, bBaselineAmbiguous);
			if (bBaselineAmbiguous)
			{
				return BodyFailure(
					FString::Printf(TEXT("Native component key '%s' matched multiple parent CDO component templates"), *ComponentKeyToString(Spec.Key)),
					ComponentPath(Spec.Key),
					TEXT("AmbiguousNativeComponent"));
			}
		}
	}

	if (!OutGeneratedComponent)
	{
		return BodyFailure(
			FString::Printf(TEXT("Native component '%s' was not found on generated CDO"), *ComponentKeyToString(Spec.Key)),
			ComponentPath(Spec.Key),
			TEXT("MissingNativeComponent"));
	}
	if (!OutBaselineComponent)
	{
		return BodyFailure(
			FString::Printf(TEXT("Native component '%s' baseline was not found on parent CDO"), *ComponentKeyToString(Spec.Key)),
			ComponentPath(Spec.Key),
			TEXT("MissingNativeComponentBaseline"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyNativeComponentOverrides(UBlueprint* Blueprint, const TArray<FUBlueprintComponentSpec>& Specs, bool& bOutChanged)
{
	bOutChanged = false;
	TArray<const FUBlueprintComponentSpec*> DesiredNativeSpecs;
	TSet<FString> DesiredNativeKeys;
	CollectNativeComponents(Specs, DesiredNativeSpecs, DesiredNativeKeys);

	UBlueprintGeneratedClass* GeneratedClass = Blueprint ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass) : nullptr;
	UObject* GeneratedCDO = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
	UClass* ParentClass = GeneratedClass ? GeneratedClass->GetSuperClass() : nullptr;
	UObject* ParentCDO = ParentClass ? ParentClass->GetDefaultObject(false) : nullptr;
	if (!GeneratedCDO || !ParentCDO)
	{
		return DesiredNativeSpecs.Num() == 0
			? FAssetDocumentCapabilityResult::Success()
			: BodyFailure(TEXT("Native component overrides require a compiled Blueprint generated class"), TEXT("/Body/Components"), TEXT("MissingGeneratedCDO"));
	}

	TArray<UActorComponent*> GeneratedComponents;
	CollectActorComponentDefaultSubobjects(GeneratedCDO, GeneratedComponents);
	for (UActorComponent* GeneratedComponent : GeneratedComponents)
	{
		if (!GeneratedComponent)
		{
			continue;
		}

		FUBlueprintComponentKey ObjectNameKey;
		ObjectNameKey.Name = GeneratedComponent->GetFName();
		ObjectNameKey.OwnerClass = ParentClass ? GetClassPath(ParentClass) : FString();
		if (DesiredNativeKeys.Contains(ComponentKeyToString(ObjectNameKey)))
		{
			continue;
		}

		bool bAmbiguous = false;
		UActorComponent* BaselineComponent = FindComponentByObjectName(ParentCDO, GeneratedComponent->GetFName(), GeneratedComponent->GetClass(), bAmbiguous);
		if (BaselineComponent && !bAmbiguous)
		{
			const FAssetDocumentCapabilityResult ResetResult =
				ResetWritablePropertiesFromBaseline(GeneratedComponent, BaselineComponent, ComponentPath(ObjectNameKey) / TEXT("Properties"));
			if (!ResetResult.bSuccess)
			{
				return ResetResult;
			}
		}
	}

	for (const FUBlueprintComponentSpec* SpecPtr : DesiredNativeSpecs)
	{
		UActorComponent* GeneratedComponent = nullptr;
		UActorComponent* BaselineComponent = nullptr;
		const FAssetDocumentCapabilityResult ResolveResult = ResolveNativeComponent(Blueprint, *SpecPtr, GeneratedComponent, BaselineComponent);
		if (!ResolveResult.bSuccess)
		{
			return ResolveResult;
		}

		const FAssetDocumentCapabilityResult ResetResult =
			ResetWritablePropertiesFromBaseline(GeneratedComponent, BaselineComponent, ComponentPath(SpecPtr->Key) / TEXT("Properties"));
		if (!ResetResult.bSuccess)
		{
			return ResetResult;
		}

		const FAssetDocumentPropertyApplyResult PropertyResult =
			FAssetDocumentPropertyAdapter::ApplyProperties(GeneratedComponent, SpecPtr->Properties);
		if (!PropertyResult.bSuccess)
		{
			return ComponentPropertyFailure(PropertyResult, ComponentPath(SpecPtr->Key) / TEXT("Properties"));
		}
		bOutChanged = true;
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult PreflightInheritedNativeComponents(UBlueprint* Blueprint, const TArray<FUBlueprintComponentSpec>& Specs)
{
	for (const FUBlueprintComponentSpec& Spec : Specs)
	{
		if (!HasAuthoredProperties(Spec))
		{
			continue;
		}
		if (Spec.Scope == TEXT("Inherited"))
		{
			FComponentKey ResolvedKey;
			UActorComponent* OriginalTemplate = nullptr;
		const FAssetDocumentCapabilityResult ResolveResult = ResolveInheritedComponentKey(Blueprint, Spec, ResolvedKey, OriginalTemplate);
		if (!ResolveResult.bSuccess)
		{
			return ResolveResult;
		}
		const FAssetDocumentPropertyApplyResult PropertyResult = PreflightPropertiesOnObject(OriginalTemplate, Spec.Properties);
		if (!PropertyResult.bSuccess)
		{
			return ComponentPropertyFailure(PropertyResult, ComponentPath(Spec.Key) / TEXT("Properties"));
		}
	}
	else if (Spec.Scope == TEXT("Native"))
	{
		UActorComponent* GeneratedComponent = nullptr;
		UActorComponent* BaselineComponent = nullptr;
			const FAssetDocumentCapabilityResult ResolveResult = ResolveNativeComponent(Blueprint, Spec, GeneratedComponent, BaselineComponent);
		if (!ResolveResult.bSuccess)
		{
			return ResolveResult;
		}
		const FAssetDocumentPropertyApplyResult PropertyResult = PreflightPropertiesOnObject(GeneratedComponent, Spec.Properties);
		if (!PropertyResult.bSuccess)
		{
			return ComponentPropertyFailure(PropertyResult, ComponentPath(Spec.Key) / TEXT("Properties"));
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

TSharedRef<FJsonObject> ComponentKeyToJsonObject(const FUBlueprintComponentKey& Key)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Name"), Key.Name.ToString());
	Object->SetStringField(TEXT("OwnerClass"), Key.OwnerClass);
	return Object;
}

TSharedRef<FJsonObject> ComponentSpecToJsonObject(const FUBlueprintComponentSpec& Component)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetObjectField(TEXT("Key"), ComponentKeyToJsonObject(Component.Key));
	Object->SetStringField(TEXT("Scope"), Component.Scope);
	Object->SetStringField(TEXT("Class"), GetClassPath(Component.ComponentClass));
	if (Component.AttachTo.IsSet())
	{
		Object->SetObjectField(TEXT("AttachTo"), ComponentKeyToJsonObject(Component.AttachTo.GetValue()));
	}
	Object->SetBoolField(TEXT("Root"), Component.bRoot);
	Object->SetObjectField(TEXT("Properties"), Component.Properties.IsValid() ? Component.Properties : MakeShared<FJsonObject>());
	return Object;
}

TSharedRef<FJsonObject> ComponentNodeToJsonObject(const UBlueprint* Blueprint, const USCS_Node* Node)
{
	FUBlueprintComponentKey Key;
	Key.Name = Node ? Node->GetVariableName() : NAME_None;
	Key.OwnerClass = TEXT("Self");

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetObjectField(TEXT("Key"), ComponentKeyToJsonObject(Key));
	Object->SetStringField(TEXT("Scope"), TEXT("OwnedSCS"));
	Object->SetStringField(TEXT("Class"), GetClassPath(Node ? Node->ComponentClass.Get() : nullptr));

	USCS_Node* ParentNode = Node && Node->GetSCS() ? Node->GetSCS()->FindParentNode(const_cast<USCS_Node*>(Node)) : nullptr;
	if (ParentNode)
	{
		FUBlueprintComponentKey ParentKey;
		ParentKey.Name = ParentNode->GetVariableName();
		ParentKey.OwnerClass = TEXT("Self");
		Object->SetObjectField(TEXT("AttachTo"), ComponentKeyToJsonObject(ParentKey));
	}
	else if (Node && !Node->ParentComponentOrVariableName.IsNone())
	{
		FUBlueprintComponentKey ParentKey;
		ParentKey.Name = Node->ParentComponentOrVariableName;
		ParentKey.OwnerClass = TEXT("Self");
		Object->SetObjectField(TEXT("AttachTo"), ComponentKeyToJsonObject(ParentKey));
	}

	Object->SetBoolField(TEXT("Root"), Node ? Node->IsRootNode() : false);
	UObject* Template = Node ? Node->ComponentTemplate.Get() : nullptr;
	Object->SetObjectField(
		TEXT("Properties"),
		Template ? FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(Template, true) : MakeShared<FJsonObject>());
	return Object;
}

FString JsonValueToComparableString(const TSharedPtr<FJsonValue>& Value)
{
	FString Result;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
	FJsonSerializer::Serialize(Value, TEXT(""), Writer);
	return Result;
}

bool JsonValuesDiffer(const TSharedPtr<FJsonValue>& Current, const TSharedPtr<FJsonValue>& Desired)
{
	return JsonValueToComparableString(Current) != JsonValueToComparableString(Desired);
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

void AddSkippedVariableEvidence(TSharedRef<FJsonObject>& OutBodyJson, const FString& VariableName, const FString& Reason)
{
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

	TSharedPtr<FJsonObject> VariablesDiagnostic;
	const TSharedPtr<FJsonObject>* ExistingVariables = nullptr;
	if (Skipped->TryGetObjectField(TEXT("Variables"), ExistingVariables) && ExistingVariables && ExistingVariables->IsValid())
	{
		VariablesDiagnostic = *ExistingVariables;
	}
	else
	{
		VariablesDiagnostic = MakeShared<FJsonObject>();
		Skipped->SetObjectField(TEXT("Variables"), VariablesDiagnostic);
	}

	TSharedPtr<FJsonObject> VariableDiagnostic = MakeShared<FJsonObject>();
	VariableDiagnostic->SetStringField(TEXT("Reason"), Reason);
	VariablesDiagnostic->SetObjectField(VariableName, VariableDiagnostic);
}

void RestoreBlueprintState(
	UBlueprint* Blueprint,
	UClass* PreviousParentClass,
	const TArray<FBPInterfaceDescription>& PreviousInterfaces,
	const TArray<FBPVariableDescription>& PreviousVariables)
{
	if (!Blueprint)
	{
		return;
	}

	Blueprint->Modify();
	Blueprint->ParentClass = PreviousParentClass;
	Blueprint->ImplementedInterfaces = PreviousInterfaces;
	Blueprint->NewVariables = PreviousVariables;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
}

FAssetDocumentCapabilityResult ApplyVariableDefaultsToGeneratedClass(UBlueprint* Blueprint, const TArray<FUBlueprintVariableSpec>& Variables)
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
	for (const FUBlueprintVariableSpec& Variable : Variables)
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
		const FUBlueprintVariableSpec& Variable = Variables[Index];
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

	const FAssetDocumentPropertyApplyResult PreflightResult =
		FAssetDocumentPropertyAdapter::PreflightProperties(GeneratedClass, ClassDefaults);
	if (!PreflightResult.bSuccess)
	{
		return ClassDefaultPropertyFailure(PreflightResult);
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

int32 CountUnsupportedWritableDifferences(UObject* Object, UObject* Baseline, const UBlueprint* Blueprint = nullptr)
{
	if (!Object || !Baseline)
	{
		return 0;
	}

	int32 Count = 0;
	for (TFieldIterator<FProperty> PropertyIt(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!FAssetDocumentPropertyAdapter::IsWritableProperty(Property)
			|| IsSupportedClassDefaultProperty(Property)
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
		if (!Property->Identical(CurrentValuePtr, BaselineValuePtr))
		{
			++Count;
		}
	}
	return Count;
}

TSharedPtr<FJsonObject> InheritedOverrideToJsonObject(const FComponentKey& Key, UActorComponent* Template)
{
	if (!Key.IsSCSKey() || !Template)
	{
		return nullptr;
	}

	FUBlueprintComponentKey PublicKey;
	PublicKey.Name = Key.GetSCSVariableName();
	PublicKey.OwnerClass = GetClassPath(Key.GetComponentOwner());

	UActorComponent* OriginalTemplate = Key.GetOriginalTemplate();
	TSharedPtr<FJsonObject> ComponentObject = MakeShared<FJsonObject>();
	ComponentObject->SetObjectField(TEXT("Key"), ComponentKeyToJsonObject(PublicKey));
	ComponentObject->SetStringField(TEXT("Scope"), TEXT("Inherited"));
	ComponentObject->SetStringField(TEXT("Class"), GetClassPath(Template->GetClass()));
	ComponentObject->SetBoolField(TEXT("Root"), false);
	ComponentObject->SetObjectField(TEXT("Properties"), ExtractWritablePropertiesComparedToBaseline(Template, OriginalTemplate));
	return ComponentObject;
}

TSharedPtr<FJsonObject> NativeComponentOverrideToJsonObject(
	UObject* GeneratedCDO,
	UObject* ParentCDO,
	UActorComponent* GeneratedComponent,
	UClass* NativeOwnerClass)
{
	if (!GeneratedCDO || !ParentCDO || !GeneratedComponent || !NativeOwnerClass)
	{
		return nullptr;
	}

	bool bAmbiguous = false;
	UActorComponent* BaselineComponent = FindComponentByObjectName(ParentCDO, GeneratedComponent->GetFName(), GeneratedComponent->GetClass(), bAmbiguous);
	if (!BaselineComponent || bAmbiguous)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Properties = ExtractWritablePropertiesComparedToBaseline(GeneratedComponent, BaselineComponent);
	if (!Properties.IsValid() || Properties->Values.Num() == 0)
	{
		return nullptr;
	}

	FUBlueprintComponentKey PublicKey;
	const FName AliasName = FindAliasPropertyNameForComponent(GeneratedCDO, GeneratedComponent);
	PublicKey.Name = AliasName.IsNone() ? GeneratedComponent->GetFName() : AliasName;
	PublicKey.OwnerClass = GetClassPath(NativeOwnerClass);

	TSharedPtr<FJsonObject> ComponentObject = MakeShared<FJsonObject>();
	ComponentObject->SetObjectField(TEXT("Key"), ComponentKeyToJsonObject(PublicKey));
	ComponentObject->SetStringField(TEXT("Scope"), TEXT("Native"));
	ComponentObject->SetStringField(TEXT("Class"), GetClassPath(GeneratedComponent->GetClass()));
	ComponentObject->SetBoolField(TEXT("Root"), false);
	ComponentObject->SetObjectField(TEXT("Properties"), Properties);
	return ComponentObject;
}

FAssetDocumentCapabilityResult RestoreAndReturnFailure(
	UBlueprint* Blueprint,
	UClass* PreviousParentClass,
	const TArray<FBPInterfaceDescription>& PreviousInterfaces,
	const TArray<FBPVariableDescription>& PreviousVariables,
	const FAssetDocumentCapabilityResult& FailureResult)
{
	RestoreBlueprintState(Blueprint, PreviousParentClass, PreviousInterfaces, PreviousVariables);
	return FailureResult;
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
	return {TEXT("UBlueprintBody"), TEXT("UBlueprintAuthoritativeRegions"), TEXT("UBlueprintGraphRegionAdapter")};
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
	Schema->SetStringField(TEXT("UbergraphPages"), TEXT("array<GraphSpec> validated by GraphCore; apply adapters deferred"));
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

	TArray<FUBlueprintComponentSpec> ParsedComponents;
	const FAssetDocumentCapabilityResult ComponentParseResult = ParseComponentSpecs(BodyObject, ParsedComponents);
	if (!ComponentParseResult.bSuccess)
	{
		return ComponentParseResult;
	}

	bool bHasClassDefaultsRegion = false;
	TSharedPtr<FJsonObject> ParsedClassDefaults;
	const FAssetDocumentCapabilityResult ClassDefaultsParseResult = ParseClassDefaults(BodyObject, bHasClassDefaultsRegion, ParsedClassDefaults);
	if (!ClassDefaultsParseResult.bSuccess)
	{
		return ClassDefaultsParseResult;
	}
	if (!ParsedClassDefaults.IsValid())
	{
		ParsedClassDefaults = MakeShared<FJsonObject>();
	}

	constexpr bool bHasVariablesRegion = true;
	constexpr bool bHasInterfacesRegion = true;
	constexpr bool bHasComponentsRegion = true;
	constexpr bool bHasClassDefaultsRegionForApply = true;
	const UClass* EffectiveParentClass = ParsedParentClass ? ParsedParentClass : Blueprint->ParentClass.Get();
	const bool bParentChangesExistingBlueprint = ParsedParentClass && Blueprint->ParentClass.Get() != ParsedParentClass;

	if (bHasVariablesRegion)
	{
		const FAssetDocumentCapabilityResult ParentVariableResult = ValidateVariablesAgainstParentClass(const_cast<UClass*>(EffectiveParentClass), ParsedVariables);
		if (!ParentVariableResult.bSuccess)
		{
			return ParentVariableResult;
		}
	}

	if (bHasComponentsRegion && HasOwnedSCSComponent(ParsedComponents) && (!EffectiveParentClass || !EffectiveParentClass->IsChildOf(AActor::StaticClass())))
	{
		return BodyFailure(TEXT("OwnedSCS components require an Actor-derived Blueprint parent class"), TEXT("/Body/Components"), TEXT("OwnedSCSRequiresActorParent"));
	}

	if (bHasComponentsRegion)
	{
		const FAssetDocumentCapabilityResult ComponentPropertiesResult = PreflightComponentProperties(ParsedComponents);
		if (!ComponentPropertiesResult.bSuccess)
		{
			return ComponentPropertiesResult;
		}

		if (!bParentChangesExistingBlueprint)
		{
			const FAssetDocumentCapabilityResult InheritedNativePreflightResult = PreflightInheritedNativeComponents(Blueprint, ParsedComponents);
			if (!InheritedNativePreflightResult.bSuccess)
			{
				return InheritedNativePreflightResult;
			}
		}
	}

	if (bHasClassDefaultsRegionForApply && !bParentChangesExistingBlueprint)
	{
		const FAssetDocumentCapabilityResult ClassDefaultsPreflightResult = PreflightClassDefaults(Blueprint, ParsedClassDefaults);
		if (!ClassDefaultsPreflightResult.bSuccess)
		{
			return ClassDefaultsPreflightResult;
		}
	}

	bool bChanged = false;
	UClass* PreviousParentClass = Blueprint->ParentClass.Get();
	const TArray<FBPInterfaceDescription> PreviousInterfaces = Blueprint->ImplementedInterfaces;
	const TArray<FBPVariableDescription> PreviousVariables = Blueprint->NewVariables;
	if (ParsedParentClass && PreviousParentClass != ParsedParentClass)
	{
		Blueprint->Modify();
		Blueprint->ParentClass = ParsedParentClass;
		bChanged = true;
	}

	if (bHasInterfacesRegion)
	{
		bool bInterfacesChanged = false;
		const FAssetDocumentCapabilityResult InterfaceApplyResult = ApplyInterfaces(Blueprint, ParsedInterfaces, bInterfacesChanged);
		if (!InterfaceApplyResult.bSuccess)
		{
			return RestoreAndReturnFailure(Blueprint, PreviousParentClass, PreviousInterfaces, PreviousVariables, InterfaceApplyResult);
		}
		bChanged |= bInterfacesChanged;
	}

	if (bHasVariablesRegion)
	{
		bool bVariablesChanged = false;
		const FAssetDocumentCapabilityResult VariableApplyResult = ApplyVariables(Blueprint, ParsedVariables, bVariablesChanged);
		if (!VariableApplyResult.bSuccess)
		{
			return RestoreAndReturnFailure(Blueprint, PreviousParentClass, PreviousInterfaces, PreviousVariables, VariableApplyResult);
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
			return RestoreAndReturnFailure(
				Blueprint,
				PreviousParentClass,
				PreviousInterfaces,
				PreviousVariables,
				BodyFailure(TEXT("Failed to compile UBlueprint after applying authoritative Body regions"), TEXT("/Body"), TEXT("BlueprintCompileFailed")));
		}

		if (bHasVariablesRegion)
		{
			const FAssetDocumentCapabilityResult DefaultsResult = ApplyVariableDefaultsToGeneratedClass(Blueprint, ParsedVariables);
			if (!DefaultsResult.bSuccess)
			{
				return RestoreAndReturnFailure(Blueprint, PreviousParentClass, PreviousInterfaces, PreviousVariables, DefaultsResult);
			}
		}
	}

	if (bHasComponentsRegion)
	{
		const FAssetDocumentCapabilityResult InheritedNativePreflightResult = PreflightInheritedNativeComponents(Blueprint, ParsedComponents);
		if (!InheritedNativePreflightResult.bSuccess)
		{
			return RestoreAndReturnFailure(Blueprint, PreviousParentClass, PreviousInterfaces, PreviousVariables, InheritedNativePreflightResult);
		}
	}

	if (bHasClassDefaultsRegionForApply)
	{
		const FAssetDocumentCapabilityResult ClassDefaultsPreflightResult = PreflightClassDefaults(Blueprint, ParsedClassDefaults);
		if (!ClassDefaultsPreflightResult.bSuccess)
		{
			return RestoreAndReturnFailure(Blueprint, PreviousParentClass, PreviousInterfaces, PreviousVariables, ClassDefaultsPreflightResult);
		}
	}

	if (bHasComponentsRegion)
	{
		bool bOwnedSCSChanged = false;
		const FAssetDocumentCapabilityResult ComponentApplyResult = ApplyOwnedSCSComponents(Blueprint, ParsedComponents, bOwnedSCSChanged);
		if (!ComponentApplyResult.bSuccess)
		{
			return RestoreAndReturnFailure(Blueprint, PreviousParentClass, PreviousInterfaces, PreviousVariables, ComponentApplyResult);
		}

		if (bOwnedSCSChanged)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			if (Blueprint->Status == BS_Error)
			{
				return RestoreAndReturnFailure(
					Blueprint,
					PreviousParentClass,
					PreviousInterfaces,
					PreviousVariables,
					BodyFailure(TEXT("Failed to compile UBlueprint after applying owned SCS components"), TEXT("/Body/Components"), TEXT("BlueprintCompileFailed")));
			}
		}
	}

	if (bHasComponentsRegion)
	{
		bool bInheritedChanged = false;
		const FAssetDocumentCapabilityResult InheritedApplyResult = ApplyInheritedComponentOverrides(Blueprint, ParsedComponents, bInheritedChanged);
		if (!InheritedApplyResult.bSuccess)
		{
			return RestoreAndReturnFailure(Blueprint, PreviousParentClass, PreviousInterfaces, PreviousVariables, InheritedApplyResult);
		}

		bool bNativeChanged = false;
		const FAssetDocumentCapabilityResult NativeApplyResult = ApplyNativeComponentOverrides(Blueprint, ParsedComponents, bNativeChanged);
		if (!NativeApplyResult.bSuccess)
		{
			return RestoreAndReturnFailure(Blueprint, PreviousParentClass, PreviousInterfaces, PreviousVariables, NativeApplyResult);
		}
	}

	if (bHasClassDefaultsRegionForApply)
	{
		const FAssetDocumentCapabilityResult ClassDefaultsApplyResult = ApplyClassDefaults(Blueprint, ParsedClassDefaults);
		if (!ClassDefaultsApplyResult.bSuccess)
		{
			return RestoreAndReturnFailure(Blueprint, PreviousParentClass, PreviousInterfaces, PreviousVariables, ClassDefaultsApplyResult);
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
			if (!IsSupportedAuthoredPinType(Variable.VarType))
			{
				AddSkippedVariableEvidence(OutBodyJson, Variable.VarName.ToString(), TEXT("UnsupportedPinType"));
				continue;
			}
			Variables.Add(MakeShared<FJsonValueObject>(VariableToJsonObject(Variable, Blueprint)));
		}
	}
	OutBodyJson->SetArrayField(TEXT("Variables"), Variables);

	TArray<TSharedPtr<FJsonValue>> Components;
	if (Blueprint && Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node || IsDefaultSceneRootNode(Node, Blueprint->SimpleConstructionScript))
			{
				continue;
			}
			Components.Add(MakeShared<FJsonValueObject>(ComponentNodeToJsonObject(Blueprint, Node)));
		}
	}
	if (Blueprint)
	{
		if (UInheritableComponentHandler* Handler = const_cast<UBlueprint*>(Blueprint)->GetInheritableComponentHandler(false))
		{
			for (auto RecordIt = Handler->CreateRecordIterator(); RecordIt; ++RecordIt)
			{
				TSharedPtr<FJsonObject> ComponentObject = InheritedOverrideToJsonObject(RecordIt->ComponentKey, RecordIt->ComponentTemplate);
				if (ComponentObject.IsValid())
				{
					Components.Add(MakeShared<FJsonValueObject>(ComponentObject));
				}
			}
		}

		UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
		UObject* GeneratedCDO = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
		UObject* ParentCDO = GeneratedClass && GeneratedClass->GetSuperClass()
			? GeneratedClass->GetSuperClass()->GetDefaultObject(false)
			: nullptr;
		TArray<UActorComponent*> GeneratedComponents;
		CollectActorComponentDefaultSubobjects(GeneratedCDO, GeneratedComponents);
		for (UActorComponent* GeneratedComponent : GeneratedComponents)
		{
			UClass* NativeOwnerClass = FindNativeOwnerClassForComponent(GeneratedClass ? GeneratedClass->GetSuperClass() : nullptr, GeneratedComponent);
			TSharedPtr<FJsonObject> ComponentObject =
				NativeComponentOverrideToJsonObject(GeneratedCDO, ParentCDO, GeneratedComponent, NativeOwnerClass);
			if (ComponentObject.IsValid())
			{
				Components.Add(MakeShared<FJsonValueObject>(ComponentObject));
			}
		}
	}
	OutBodyJson->SetArrayField(TEXT("Components"), Components);
	if (Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->GetSuperClass())
	{
		UObject* GeneratedCDO = Blueprint->GeneratedClass->GetDefaultObject(false);
		UObject* ParentCDO = Blueprint->GeneratedClass->GetSuperClass()->GetDefaultObject(false);
		OutBodyJson->SetObjectField(TEXT("ClassDefaults"), ExtractWritablePropertiesComparedToBaseline(GeneratedCDO, ParentCDO, Blueprint));
		AddSkippedUnsupportedEvidence(OutBodyJson, TEXT("ClassDefaults"), CountUnsupportedWritableDifferences(GeneratedCDO, ParentCDO, Blueprint));
	}
	else
	{
		OutBodyJson->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	}
	const FUBlueprintGraphRegionAdapter GraphRegionAdapter;
	const FAssetDocumentCapabilityResult GraphExtractResult = GraphRegionAdapter.ExtractRegions(Context, OutBodyJson);
	if (!GraphExtractResult.bSuccess)
	{
		return GraphExtractResult;
	}
	OutBodyJson->SetArrayField(TEXT("FunctionGraphs"), {});
	OutBodyJson->SetArrayField(TEXT("MacroGraphs"), {});
	OutBodyJson->SetArrayField(TEXT("Timelines"), {});

	if (Blueprint)
	{
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

	const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		return BodyFailure(TEXT("UBlueprint body diff requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	{
		TArray<FUBlueprintVariableSpec> DesiredVariables;
		const FAssetDocumentCapabilityResult VariableParseResult = ParseVariableSpecs(DesiredBody, DesiredVariables);
		if (!VariableParseResult.bSuccess)
		{
			return VariableParseResult;
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
					MakeVariableDiffValue(Blueprint, CurrentVariable),
					MakeShared<FJsonValueNull>(),
					TEXT("extra"));
				continue;
			}

			const bool bTypeChanged = AuthoredPinTypesDiffer(CurrentVariable.VarType, DesiredVariable->Type);
			const FString CurrentDefaultValue = ResolveVariableDefaultValue(Blueprint, CurrentVariable);
			const bool bDefaultChanged = AuthoredDefaultValuesDiffer(CurrentVariable.VarType, CurrentDefaultValue, DesiredVariable->DefaultValue);
			const FString DesiredCategory = DesiredVariable->Category.IsSet() ? DesiredVariable->Category.GetValue() : FString();
			const bool bCategoryChanged = CurrentVariable.Category.ToString() != DesiredCategory;
			const FString CurrentTooltip = CurrentVariable.HasMetaData(FBlueprintMetadata::MD_Tooltip)
				? CurrentVariable.GetMetaData(FBlueprintMetadata::MD_Tooltip)
				: FString();
			const FString DesiredTooltip = DesiredVariable->Tooltip.IsSet() ? DesiredVariable->Tooltip.GetValue() : FString();
			const bool bTooltipChanged = CurrentTooltip != DesiredTooltip;

			AddBodyDiffEntry(
				OutDiffEntries,
				Path,
				(bTypeChanged || bDefaultChanged || bCategoryChanged || bTooltipChanged) ? TEXT("changed") : TEXT("unchanged"),
				MakeVariableDiffValue(Blueprint, CurrentVariable),
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
	}

	{
		TArray<FUBlueprintInterfaceSpec> DesiredInterfaces;
		const FAssetDocumentCapabilityResult InterfaceParseResult = ParseInterfaceSpecs(DesiredBody, DesiredInterfaces);
		if (!InterfaceParseResult.bSuccess)
		{
			return InterfaceParseResult;
		}

		TMap<FString, UClass*> DesiredByPath;
		for (const FUBlueprintInterfaceSpec& DesiredInterface : DesiredInterfaces)
		{
			DesiredByPath.Add(GetClassPath(DesiredInterface.InterfaceClass), DesiredInterface.InterfaceClass);
		}

		TSet<FString> SeenCurrentInterfaces;
		for (const FBPInterfaceDescription& CurrentInterface : Blueprint->ImplementedInterfaces)
		{
			if (!CurrentInterface.Interface)
			{
				continue;
			}
			const FString CurrentPath = GetClassPath(CurrentInterface.Interface);
			SeenCurrentInterfaces.Add(CurrentPath);
			UClass* const* DesiredInterface = DesiredByPath.Find(CurrentPath);
			AddBodyDiffEntry(
				OutDiffEntries,
				FString::Printf(TEXT("/Body/ImplementedInterfaces/%s"), *CurrentPath),
				DesiredInterface ? TEXT("unchanged") : TEXT("changed"),
				MakeInterfaceDiffValue(CurrentInterface.Interface),
				DesiredInterface ? MakeInterfaceDiffValue(*DesiredInterface) : MakeInterfaceDiffValue(nullptr),
				DesiredInterface ? FString() : TEXT("extra"));
		}

		for (const FUBlueprintInterfaceSpec& DesiredInterface : DesiredInterfaces)
		{
			const FString DesiredPath = GetClassPath(DesiredInterface.InterfaceClass);
			if (!SeenCurrentInterfaces.Contains(DesiredPath))
			{
				AddBodyDiffEntry(
					OutDiffEntries,
					FString::Printf(TEXT("/Body/ImplementedInterfaces/%s"), *DesiredPath),
					TEXT("changed"),
					MakeShared<FJsonValueNull>(),
					MakeShared<FJsonValueObject>(InterfaceToJsonObject(DesiredInterface.InterfaceClass)),
					TEXT("missing"));
			}
		}
	}

	{
		TArray<FUBlueprintComponentSpec> DesiredComponents;
		const FAssetDocumentCapabilityResult ComponentParseResult = ParseComponentSpecs(DesiredBody, DesiredComponents);
		if (!ComponentParseResult.bSuccess)
		{
			return ComponentParseResult;
		}

		struct FDesiredComponentDiffSpec
		{
			TSharedPtr<FJsonObject> Value;
			FString OriginalKeyString;
		};
		TMap<FString, FDesiredComponentDiffSpec> DesiredByKey;
		for (const FUBlueprintComponentSpec& Component : DesiredComponents)
		{
			FDesiredComponentDiffSpec Desired;
			Desired.Value = ComponentSpecToJsonObject(Component);
			Desired.OriginalKeyString = ComponentKeyToString(Component.Key);
			DesiredByKey.Add(ComponentKeyToString(Component.Key), Desired);
			if (Component.Scope == TEXT("Native"))
			{
				UActorComponent* GeneratedComponent = nullptr;
				UActorComponent* BaselineComponent = nullptr;
				if (ResolveNativeComponent(const_cast<UBlueprint*>(Blueprint), Component, GeneratedComponent, BaselineComponent).bSuccess && GeneratedComponent)
				{
					FUBlueprintComponentKey CanonicalKey = Component.Key;
					UObject* GeneratedCDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
					const FName AliasName = FindAliasPropertyNameForComponent(GeneratedCDO, GeneratedComponent);
					CanonicalKey.Name = AliasName.IsNone() ? GeneratedComponent->GetFName() : AliasName;
					FUBlueprintComponentSpec CanonicalComponent = Component;
					CanonicalComponent.Key = CanonicalKey;
					FDesiredComponentDiffSpec CanonicalDesired;
					CanonicalDesired.Value = ComponentSpecToJsonObject(CanonicalComponent);
					CanonicalDesired.OriginalKeyString = ComponentKeyToString(Component.Key);
					DesiredByKey.Add(ComponentKeyToString(CanonicalKey), CanonicalDesired);
				}
			}
		}

		TSet<FString> SeenDesiredComponents;
		auto AddCurrentComponentDiff = [&](const TSharedPtr<FJsonObject>& CurrentObject)
		{
			if (!CurrentObject.IsValid())
			{
				return;
			}

			const TSharedPtr<FJsonObject>* KeyObject = nullptr;
			FUBlueprintComponentKey CurrentKey;
			if (!CurrentObject->TryGetObjectField(TEXT("Key"), KeyObject) || !KeyObject || !KeyObject->IsValid())
			{
				return;
			}
			if (!ReadComponentKey(*KeyObject, TEXT("/Body/Components/Current/Key"), CurrentKey).bSuccess)
			{
				return;
			}

			const FString CurrentKeyString = ComponentKeyToString(CurrentKey);
			const FDesiredComponentDiffSpec* DesiredComponent = DesiredByKey.Find(CurrentKeyString);
			if (DesiredComponent)
			{
				SeenDesiredComponents.Add(DesiredComponent->OriginalKeyString);
			}

			const TSharedPtr<FJsonValue> CurrentValue = MakeShared<FJsonValueObject>(CurrentObject.ToSharedRef());
			const TSharedPtr<FJsonValue> DesiredValue = DesiredComponent
				? TSharedPtr<FJsonValue>(MakeShared<FJsonValueObject>(DesiredComponent->Value.ToSharedRef()))
				: TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>());
			const bool bChanged = !DesiredComponent || JsonValuesDiffer(CurrentValue, DesiredValue);
			AddBodyDiffEntry(
				OutDiffEntries,
				ComponentPath(CurrentKey),
				bChanged ? TEXT("changed") : TEXT("unchanged"),
				CurrentValue,
				DesiredValue,
				DesiredComponent ? (bChanged ? TEXT("changed") : FString()) : TEXT("extra"));
		};

		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* CurrentNode : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (!CurrentNode || IsDefaultSceneRootNode(CurrentNode, Blueprint->SimpleConstructionScript))
				{
					continue;
				}

				AddCurrentComponentDiff(ComponentNodeToJsonObject(Blueprint, CurrentNode));
			}
		}

		if (UInheritableComponentHandler* Handler = const_cast<UBlueprint*>(Blueprint)->GetInheritableComponentHandler(false))
		{
			for (auto RecordIt = Handler->CreateRecordIterator(); RecordIt; ++RecordIt)
			{
				AddCurrentComponentDiff(InheritedOverrideToJsonObject(RecordIt->ComponentKey, RecordIt->ComponentTemplate));
			}
		}

		UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
		UObject* GeneratedCDO = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
		UObject* ParentCDO = GeneratedClass && GeneratedClass->GetSuperClass()
			? GeneratedClass->GetSuperClass()->GetDefaultObject(false)
			: nullptr;
		TArray<UActorComponent*> GeneratedComponents;
		CollectActorComponentDefaultSubobjects(GeneratedCDO, GeneratedComponents);
		for (UActorComponent* GeneratedComponent : GeneratedComponents)
		{
			UClass* NativeOwnerClass = FindNativeOwnerClassForComponent(GeneratedClass ? GeneratedClass->GetSuperClass() : nullptr, GeneratedComponent);
			AddCurrentComponentDiff(NativeComponentOverrideToJsonObject(GeneratedCDO, ParentCDO, GeneratedComponent, NativeOwnerClass));
		}

		for (const FUBlueprintComponentSpec& DesiredComponent : DesiredComponents)
		{
			if (!SeenDesiredComponents.Contains(ComponentKeyToString(DesiredComponent.Key)))
			{
				AddBodyDiffEntry(
					OutDiffEntries,
					ComponentPath(DesiredComponent.Key),
					TEXT("changed"),
					MakeShared<FJsonValueNull>(),
					MakeShared<FJsonValueObject>(ComponentSpecToJsonObject(DesiredComponent)),
					TEXT("missing"));
			}
		}
	}

	{
		bool bHasClassDefaultsRegion = false;
		TSharedPtr<FJsonObject> DesiredClassDefaults;
		const FAssetDocumentCapabilityResult ClassDefaultsParseResult = ParseClassDefaults(DesiredBody, bHasClassDefaultsRegion, DesiredClassDefaults);
		if (!ClassDefaultsParseResult.bSuccess)
		{
			return ClassDefaultsParseResult;
		}
		if (!DesiredClassDefaults.IsValid())
		{
			DesiredClassDefaults = MakeShared<FJsonObject>();
		}

		UObject* GeneratedCDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
		UObject* ParentCDO = Blueprint->GeneratedClass && Blueprint->GeneratedClass->GetSuperClass()
			? Blueprint->GeneratedClass->GetSuperClass()->GetDefaultObject(false)
			: nullptr;
		TSharedPtr<FJsonObject> CurrentClassDefaults = ExtractWritablePropertiesComparedToBaseline(GeneratedCDO, ParentCDO, Blueprint);
		TSet<FString> SeenClassDefaultNames;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& CurrentPair : CurrentClassDefaults->Values)
		{
			SeenClassDefaultNames.Add(CurrentPair.Key);
			const TSharedPtr<FJsonValue>* DesiredValue = DesiredClassDefaults->Values.Find(CurrentPair.Key);
			const bool bChanged = !DesiredValue || JsonValuesDiffer(CurrentPair.Value, *DesiredValue);
			AddBodyDiffEntry(
				OutDiffEntries,
				FString::Printf(TEXT("/Body/ClassDefaults/%s"), *CurrentPair.Key),
				bChanged ? TEXT("changed") : TEXT("unchanged"),
				CurrentPair.Value,
				DesiredValue ? *DesiredValue : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>()),
				DesiredValue ? (bChanged ? TEXT("changed") : FString()) : TEXT("extra"));
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& DesiredPair : DesiredClassDefaults->Values)
		{
			if (!SeenClassDefaultNames.Contains(DesiredPair.Key))
			{
				AddBodyDiffEntry(
					OutDiffEntries,
					FString::Printf(TEXT("/Body/ClassDefaults/%s"), *DesiredPair.Key),
					TEXT("changed"),
					MakeShared<FJsonValueNull>(),
					DesiredPair.Value,
					TEXT("missing"));
			}
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

	const FUBlueprintGraphRegionAdapter GraphRegionAdapter;
	const FAssetDocumentCapabilityResult GraphRegionResult = GraphRegionAdapter.ValidateRegions(Context, BodyObject);
	if (!GraphRegionResult.bSuccess)
	{
		return GraphRegionResult;
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

	TArray<FUBlueprintComponentSpec> ComponentSpecs;
	const FAssetDocumentCapabilityResult ComponentResult = ParseComponentSpecs(BodyObject, ComponentSpecs);
	if (!ComponentResult.bSuccess)
	{
		return ComponentResult;
	}

	bool bHasClassDefaultsRegion = false;
	TSharedPtr<FJsonObject> ClassDefaults;
	const FAssetDocumentCapabilityResult ClassDefaultsResult = ParseClassDefaults(BodyObject, bHasClassDefaultsRegion, ClassDefaults);
	if (!ClassDefaultsResult.bSuccess)
	{
		return ClassDefaultsResult;
	}

	if (HasOwnedSCSComponent(ComponentSpecs))
	{
		UClass* ParentClass = nullptr;
		const TSharedPtr<FJsonValue>* ParentClassValue = BodyObject->Values.Find(TEXT("ParentClass"));
		if (ParentClassValue)
		{
			const FAssetDocumentCapabilityResult ParentClassResult = ResolveParentClass(*ParentClassValue, ParentClass);
			if (!ParentClassResult.bSuccess)
			{
				return ParentClassResult;
			}
		}
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset))
		{
			ParentClass = ParentClass ? ParentClass : Blueprint->ParentClass.Get();
		}
		if (!ParentClass || !ParentClass->IsChildOf(AActor::StaticClass()))
		{
			return BodyFailure(TEXT("OwnedSCS components require an Actor-derived Blueprint parent class"), TEXT("/Body/Components"), TEXT("OwnedSCSRequiresActorParent"));
		}
	}

	const FAssetDocumentCapabilityResult ComponentPropertiesResult = PreflightComponentProperties(ComponentSpecs);
	if (!ComponentPropertiesResult.bSuccess)
	{
		return ComponentPropertiesResult;
	}

	UBlueprint* ValidationBlueprint = const_cast<UBlueprint*>(Cast<UBlueprint>(Context.Asset));
	bool bParentChangesExistingBlueprint = false;
	if (ValidationBlueprint)
	{
		UClass* DesiredParentClass = nullptr;
		const TSharedPtr<FJsonValue>* ParentClassValue = BodyObject->Values.Find(TEXT("ParentClass"));
		if (ParentClassValue)
		{
			const FAssetDocumentCapabilityResult ParentClassResult = ResolveParentClass(*ParentClassValue, DesiredParentClass);
			if (!ParentClassResult.bSuccess)
			{
				return ParentClassResult;
			}
		}
		bParentChangesExistingBlueprint = DesiredParentClass && ValidationBlueprint->ParentClass.Get() != DesiredParentClass;
	}

	if (ValidationBlueprint && !bParentChangesExistingBlueprint)
	{
		const FAssetDocumentCapabilityResult InheritedNativePreflightResult = PreflightInheritedNativeComponents(ValidationBlueprint, ComponentSpecs);
		if (!InheritedNativePreflightResult.bSuccess)
		{
			return InheritedNativePreflightResult;
		}
	}

	if (bHasClassDefaultsRegion && !bParentChangesExistingBlueprint)
	{
		if (ValidationBlueprint)
		{
			const FAssetDocumentCapabilityResult ClassDefaultsPreflightResult = PreflightClassDefaults(ValidationBlueprint, ClassDefaults);
			if (!ClassDefaultsPreflightResult.bSuccess)
			{
				return ClassDefaultsPreflightResult;
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated UBlueprint Body scaffold"));
}
