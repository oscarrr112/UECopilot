// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

/**
 * Generator for UCurveVector
 *
 * JSON Config:
 * {
 *   "AssetType": "CurveVector",
 *   "Name": "Curve_Position",
 *   "Path": "/Game/Data/Curves",
 *   "Keys": [
 *     { "Time": 0.0, "Value": [0, 0, 0], "InterpMode": "Linear" },
 *     { "Time": 1.0, "Value": [100, 50, 0] }
 *   ]
 * }
 */
class ASSETFACTORY_API FCurveVectorGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("CurveVector"); }
	virtual int32 GetPriority() const override { return 10; }

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

protected:
	ERichCurveInterpMode ParseInterpMode(const FString& ModeString) const;
	FVector ParseVector(const TSharedPtr<FJsonValue>& JsonValue) const;
};
