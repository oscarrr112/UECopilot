// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

class UBlackboardData;
struct FBlackboardEntry;

/**
 * Generator for UBlackboardData assets.
 *
 * JSON Config:
 * {
 *   "AssetType": "BlackboardData",
 *   "Name": "BB_Enemy",
 *   "Path": "/Game/AI",
 *   "Parent": "/Game/AI/BB_BaseEnemy",
 *   "Keys": [
 *     { "Name": "TargetActor", "Type": "Object", "BaseClass": "/Script/Engine.Actor" },
 *     { "Name": "HasTarget", "Type": "Bool", "bInstanceSynced": true }
 *   ]
 * }
 *
 * Supported Key Types: Bool / Int / Float / String / Name / Vector / Rotator / Object / Class / Enum
 */
class ASSETFACTORY_API FBlackboardDataGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("BlackboardData"); }
	virtual int32 GetPriority() const override { return 10; }

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action = EGenerationAction::Create) const override;
	virtual TArray<FString> GetRequiredFields() const override { return { TEXT("Keys") }; }

	virtual bool CanExtract(UObject* Asset) const override;
	virtual TSharedPtr<FJsonObject> Extract(UObject* Asset, bool bDiffOnly = false) const override;

private:
	FBlackboardEntry BuildEntry(TSharedPtr<FJsonObject> KeyJson) const;
	UClass* ResolveKeyTypeClass(const FString& TypeName) const;
	FString KeyTypeClassToName(const UClass* KeyTypeClass) const;
};
