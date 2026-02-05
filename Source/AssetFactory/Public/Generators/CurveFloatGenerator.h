// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

/**
 * Generator for UCurveFloat
 *
 * JSON Config:
 * {
 *   "AssetType": "CurveFloat",
 *   "Name": "Curve_DamageScale",
 *   "Path": "/Game/Data/Curves",
 *   "Keys": [
 *     { "Time": 0.0, "Value": 1.0, "InterpMode": "Linear" },
 *     { "Time": 0.5, "Value": 1.5, "InterpMode": "Cubic" },
 *     { "Time": 1.0, "Value": 2.0 }
 *   ]
 * }
 */
class ASSETFACTORY_API FCurveFloatGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("CurveFloat"); }
	virtual int32 GetPriority() const override { return 10; }

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	//~ Extract functionality
	virtual bool CanExtract(UObject* Asset) const override;
	virtual TSharedPtr<FJsonObject> Extract(UObject* Asset, bool bDiffOnly = false) const override;

protected:
	/** Parse interpolation mode from string */
	ERichCurveInterpMode ParseInterpMode(const FString& ModeString) const;

	/** Convert interpolation mode to string */
	FString InterpModeToString(ERichCurveInterpMode Mode) const;
};
