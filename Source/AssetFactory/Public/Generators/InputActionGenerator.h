// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"
#include "InputAction.h"

/**
 * Generator for UInputAction (Enhanced Input)
 *
 * JSON Config:
 * {
 *   "AssetType": "InputAction",
 *   "Name": "IA_Attack",
 *   "Path": "/Game/Input/Actions",
 *   "ValueType": "Boolean",              // Boolean, Axis1D, Axis2D, Axis3D
 *   "Triggers": ["Pressed", "Hold"],     // Optional: Down, Pressed, Released, Hold, Tap, Pulse
 *   "Modifiers": ["Negate", "DeadZone"]  // Optional: Negate, Swizzle, Scalar, DeadZone, Smooth
 * }
 */
class ASSETFACTORY_API FInputActionGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("InputAction"); }
	virtual int32 GetPriority() const override { return 0; } // Highest priority - processed first

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action = EGenerationAction::Create) const override;

	//~ Extract functionality
	virtual bool CanExtract(UObject* Asset) const override;
	virtual TSharedPtr<FJsonObject> Extract(UObject* Asset, bool bDiffOnly = false) const override;

protected:
	/** Parse value type from string */
	EInputActionValueType ParseValueType(const FString& TypeString) const;
	FString ValueTypeToString(EInputActionValueType ValueType) const;

	/** Create trigger from name */
	UInputTrigger* CreateTrigger(UInputAction* Outer, const FString& TriggerName) const;
	FString TriggerToString(UInputTrigger* Trigger) const;

	/** Create modifier from name */
	UInputModifier* CreateModifier(UInputAction* Outer, const FString& ModifierName) const;
	FString ModifierToString(UInputModifier* Modifier) const;
};
