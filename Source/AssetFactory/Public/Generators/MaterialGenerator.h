// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

class UMaterial;
class UMaterialExpressionParameter;

/**
 * Generator for Material assets from JSON configuration
 *
 * Supports predefined material templates for common UI effects:
 * - CircularProgress: Radial fill effect for progress indicators
 * - CooldownSweep: Clock-like sweep for cooldown displays
 * - GradientFill: Linear or radial gradient fills
 * - GlowPulse: Animated glow effect
 *
 * JSON Config Example:
 * {
 *   "AssetType": "Material",
 *   "Name": "M_UI_CircularProgress",
 *   "Path": "/Game/UI/Materials",
 *   "Template": "CircularProgress",
 *   "Domain": "UI",
 *   "BlendMode": "Translucent",
 *   "Parameters": {
 *     "Progress": 0.5,
 *     "Color": [1, 0.8, 0.2, 1],
 *     "BackgroundColor": [0.1, 0.1, 0.1, 0.5]
 *   }
 * }
 */
class ASSETFACTORY_API FMaterialGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("Material"); }
	virtual int32 GetPriority() const override { return 50; } // Before widgets that might use materials

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config) const override;
	virtual TArray<FString> GetRequiredFields() const override;

protected:
	// Template builders (work on existing material, can be used for create or update)
	bool BuildCircularProgressMaterial(UMaterial* Material, TSharedPtr<FJsonObject> Params);
	bool BuildCooldownSweepMaterial(UMaterial* Material, TSharedPtr<FJsonObject> Params);
	bool BuildGradientFillMaterial(UMaterial* Material, TSharedPtr<FJsonObject> Params);
	bool BuildHealthBarFillMaterial(UMaterial* Material, TSharedPtr<FJsonObject> Params);

	// Helpers
	void SetupUIMaterialDefaults(UMaterial* Material);
	void ClearMaterialExpressions(UMaterial* Material);
	void CompileAndSaveMaterial(UMaterial* Material);

	// Parse helpers
	FLinearColor ParseColor(TSharedPtr<FJsonValue> ColorValue) const;
	EMaterialDomain ParseDomain(const FString& DomainString) const;
	EBlendMode ParseBlendMode(const FString& BlendString) const;
};
