// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreePropertyBagAdapter.h"

bool UE::AssetFactory::StateTree::ParseParameterBagSpec(
	const TSharedPtr<FJsonObject>& ParametersObject,
	const FString& ScopeLabel,
	FAFStateTreeParameterBagSpec& OutSpec,
	FString& OutError,
	bool bRequireType)
{
	OutSpec.Parameters.Reset();
	if (!ParametersObject.IsValid())
	{
		return true;
	}

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
	(void)Bag;
	(void)Spec;
	(void)AssetPath;
	(void)ScopeLabel;
	(void)OutError;
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
