// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "UObject/WeakObjectPtr.h"

enum class EAssetDocumentGraphFieldTrait : uint8
{
	Raw,
	AssetRef,
	ClassRef,
	Name,
	SlotName,
	SyncGroupName,
	CachedPoseName,
	LayerName,
	BoneName,
	CurveName,
	GameplayTag,
	Enum,
	Color,
	Vector
};

enum class EAssetDocumentGraphFieldApplyStage : uint8
{
	Validate,
	IdentityAndPins,
	Fields
};

struct FAssetDocumentGraphFieldRuleContext
{
	FString JsonPath;
	FString FieldPath;
	FString OwnerGraphKind;
	TWeakObjectPtr<UClass> RequiredObjectClass;
	TWeakObjectPtr<UClass> RequiredClassClass;
};

struct FAssetDocumentGraphFieldRuleResult
{
	bool bSuccess = true;
	FString Code;
	FString Path;
	FString Message;

	static FAssetDocumentGraphFieldRuleResult Success();
	static FAssetDocumentGraphFieldRuleResult Failure(
		const FString& InCode,
		const FString& InPath,
		const FString& InMessage);
};

class FAssetDocumentGraphFieldRules
{
public:
	static FAssetDocumentGraphFieldRuleResult ValidateTraitShape(
		const FAssetDocumentGraphFieldRuleContext& Context,
		EAssetDocumentGraphFieldTrait Trait,
		const TSharedPtr<FJsonValue>& Value);

	static bool ShouldOmitDefaultField(
		const TSharedPtr<FJsonValue>& Value,
		const TSharedPtr<FJsonValue>& DefaultValue,
		bool bFieldAffectsIdentityOrPins);

	static TArray<EAssetDocumentGraphFieldApplyStage> GetStagedApplyOrder();
	static FString MakeJsonPathToken(const FString& Token);
};
