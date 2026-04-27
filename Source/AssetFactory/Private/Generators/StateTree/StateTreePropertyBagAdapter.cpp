// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreePropertyBagAdapter.h"

#include "Utils/ClassFinderUtils.h"
#include "Utils/PropertySetterUtils.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FAFResolvedParameterType
	{
		EPropertyBagPropertyType ValueType = EPropertyBagPropertyType::None;
		const UObject* ValueTypeObject = nullptr;
	};

	FGuid MakeParameterGuid(const FString& ScopeLabel, const FString& Name)
	{
		return FGuid::NewDeterministicGuid(FString::Printf(TEXT("AssetFactory.StateTree.Parameter.%s.%s"), *ScopeLabel, *Name));
	}

	bool SplitReferenceType(const FString& TypeString, FString& OutBaseType, FString& OutClassName)
	{
		if (!TypeString.Split(TEXT(":"), &OutBaseType, &OutClassName))
		{
			return false;
		}
		OutBaseType.TrimStartAndEndInline();
		OutClassName.TrimStartAndEndInline();
		return !OutBaseType.IsEmpty() && !OutClassName.IsEmpty();
	}

	bool ResolveParameterType(
		const FAFStateTreeParameterSpec& ParameterSpec,
		const FString& ScopeLabel,
		FAFResolvedParameterType& OutType,
		FString& OutError)
	{
		FString TypeString = ParameterSpec.Type;
		TypeString.TrimStartAndEndInline();

		if (TypeString.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
		{
			OutType.ValueType = EPropertyBagPropertyType::Bool;
			return true;
		}
		if (TypeString.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
		{
			OutType.ValueType = EPropertyBagPropertyType::Float;
			return true;
		}
		if (TypeString.Equals(TEXT("String"), ESearchCase::IgnoreCase))
		{
			OutType.ValueType = EPropertyBagPropertyType::String;
			return true;
		}
		if (TypeString.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
		{
			OutType.ValueType = EPropertyBagPropertyType::Name;
			return true;
		}
		if (TypeString.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
		{
			OutType.ValueType = EPropertyBagPropertyType::Text;
			return true;
		}

		FString BaseType;
		FString ClassName;
		if (SplitReferenceType(TypeString, BaseType, ClassName))
		{
			if (BaseType.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
			{
				OutType.ValueType = EPropertyBagPropertyType::Object;
			}
			else if (BaseType.Equals(TEXT("SoftObject"), ESearchCase::IgnoreCase))
			{
				OutType.ValueType = EPropertyBagPropertyType::SoftObject;
			}
			else if (BaseType.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
			{
				OutType.ValueType = EPropertyBagPropertyType::Class;
			}
			else if (BaseType.Equals(TEXT("SoftClass"), ESearchCase::IgnoreCase))
			{
				OutType.ValueType = EPropertyBagPropertyType::SoftClass;
			}

			if (OutType.ValueType != EPropertyBagPropertyType::None)
			{
				UClass* ResolvedClass = FClassFinderUtils::FindClassByName(ClassName, UObject::StaticClass(), false);
				if (!ResolvedClass)
				{
					OutError = FString::Printf(
						TEXT("Unknown StateTree parameter class for '%s.%s' type '%s'"),
						*ScopeLabel,
						*ParameterSpec.Name,
						*ParameterSpec.Type);
					return false;
				}
				OutType.ValueTypeObject = ResolvedClass;
				return true;
			}
		}

		OutError = FString::Printf(
			TEXT("Unknown StateTree parameter type for '%s.%s': '%s'"),
			*ScopeLabel,
			*ParameterSpec.Name,
			*ParameterSpec.Type);
		return false;
	}
}

bool UE::AssetFactory::StateTree::ParseParameterBagSpec(
	const TSharedPtr<FJsonObject>& ParametersObject,
	const FString& ScopeLabel,
	FAFStateTreeParameterBagSpec& OutSpec,
	FString& OutError,
	bool bRequireType)
{
	OutSpec.Parameters.Reset();
	OutSpec.bSpecified = false;
	if (!ParametersObject.IsValid())
	{
		return true;
	}
	OutSpec.bSpecified = true;

	TSet<FString> Names;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ParametersObject->Values)
	{
		if (Pair.Key.IsEmpty() || Names.Contains(Pair.Key))
		{
			OutError = FString::Printf(TEXT("StateTree parameter scope '%s' has duplicate or empty parameter name '%s'"), *ScopeLabel, *Pair.Key);
			return false;
		}
		Names.Add(Pair.Key);

		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(EntryObject) || !EntryObject || !EntryObject->IsValid())
		{
			OutError = FString::Printf(TEXT("StateTree parameter '%s.%s' must be an object"), *ScopeLabel, *Pair.Key);
			return false;
		}

		FAFStateTreeParameterSpec Spec;
		Spec.Name = Pair.Key;
		(*EntryObject)->TryGetStringField(TEXT("type"), Spec.Type);
		(*EntryObject)->TryGetStringField(TEXT("Type"), Spec.Type);
		if (bRequireType && Spec.Type.IsEmpty())
		{
			OutError = FString::Printf(TEXT("StateTree parameter '%s.%s' is missing required field 'type'"), *ScopeLabel, *Pair.Key);
			return false;
		}

		TSharedPtr<FJsonValue> IDValue = (*EntryObject)->TryGetField(TEXT("id"));
		if (!IDValue.IsValid())
		{
			IDValue = (*EntryObject)->TryGetField(TEXT("ID"));
		}
		if (IDValue.IsValid())
		{
			if (IDValue->Type != EJson::String)
			{
				OutError = FString::Printf(TEXT("StateTree parameter '%s.%s' field 'id' must be a string GUID"), *ScopeLabel, *Pair.Key);
				return false;
			}

			const FString GuidString = IDValue->AsString();
			if (!FGuid::Parse(GuidString, Spec.ID))
			{
				OutError = FString::Printf(TEXT("StateTree parameter '%s.%s' has invalid GUID '%s'"), *ScopeLabel, *Pair.Key, *GuidString);
				return false;
			}
			Spec.bHasExplicitID = true;
		}

		Spec.Value = (*EntryObject)->TryGetField(TEXT("value"));
		if (!Spec.Value.IsValid())
		{
			Spec.Value = (*EntryObject)->TryGetField(TEXT("Value"));
		}
		Spec.bHasValue = Spec.Value.IsValid();
		(*EntryObject)->TryGetBoolField(TEXT("overridden"), Spec.bOverridden);
		(*EntryObject)->TryGetBoolField(TEXT("Overridden"), Spec.bOverridden);

		OutSpec.Parameters.Add(MoveTemp(Spec));
	}

	return true;
}

bool UE::AssetFactory::StateTree::ApplyParameterBagSpec(
	FInstancedPropertyBag& Bag,
	const FAFStateTreeParameterBagSpec& Spec,
	const FString& AssetPath,
	const FString& ScopeLabel,
	FString& OutError)
{
	(void)AssetPath;

	TArray<FPropertyBagPropertyDesc> Descs;
	Descs.Reserve(Spec.Parameters.Num());
	for (const FAFStateTreeParameterSpec& ParameterSpec : Spec.Parameters)
	{
		FAFResolvedParameterType ResolvedType;
		if (!ResolveParameterType(ParameterSpec, ScopeLabel, ResolvedType, OutError))
		{
			return false;
		}

		FPropertyBagPropertyDesc& Desc = Descs.Add_GetRef(FPropertyBagPropertyDesc(
			FName(*ParameterSpec.Name),
			ResolvedType.ValueType,
			ResolvedType.ValueTypeObject));
		Desc.ID = ParameterSpec.bHasExplicitID ? ParameterSpec.ID : MakeParameterGuid(ScopeLabel, ParameterSpec.Name);
	}

	Bag.Reset();
	if (Descs.IsEmpty())
	{
		return true;
	}

	const EPropertyBagAlterationResult AddResult = Bag.AddProperties(Descs);
	if (AddResult != EPropertyBagAlterationResult::Success)
	{
		OutError = FString::Printf(
			TEXT("Failed to update StateTree parameter bag schema for scope '%s' (result=%d)"),
			*ScopeLabel,
			static_cast<int32>(AddResult));
		return false;
	}

	const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
	if (!BagStruct)
	{
		OutError = FString::Printf(TEXT("StateTree parameter bag schema for scope '%s' was not created"), *ScopeLabel);
		return false;
	}

	FStructView MutableValue = Bag.GetMutableValue();
	void* BagMemory = MutableValue.GetMemory();
	if (!BagMemory)
	{
		OutError = FString::Printf(TEXT("StateTree parameter bag memory for scope '%s' was not created"), *ScopeLabel);
		return false;
	}

	TSharedPtr<FJsonObject> ValuesObject = MakeShared<FJsonObject>();
	for (const FAFStateTreeParameterSpec& ParameterSpec : Spec.Parameters)
	{
		if (!ParameterSpec.bHasValue)
		{
			continue;
		}
		ValuesObject->SetField(ParameterSpec.Name, ParameterSpec.Value);
	}

	if (ValuesObject->Values.Num() > 0
		&& !FPropertySetterUtils::SetStructFromJson(const_cast<UPropertyBag*>(BagStruct), BagMemory, MakeShared<FJsonValueObject>(ValuesObject)))
	{
		OutError = FString::Printf(TEXT("Failed to set StateTree parameter defaults for scope '%s'"), *ScopeLabel);
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> UE::AssetFactory::StateTree::ExtractParameterBag(
	const FInstancedPropertyBag& Bag,
	bool bDiffOnly)
{
	(void)Bag;
	(void)bDiffOnly;
	return MakeShared<FJsonObject>();
}
