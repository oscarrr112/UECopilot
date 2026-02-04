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

protected:
	/** Parse value type from string */
	EInputActionValueType ParseValueType(const FString& TypeString) const;

	/** Create trigger from name */
	UInputTrigger* CreateTrigger(UInputAction* Outer, const FString& TriggerName) const;

	/** Create modifier from name */
	UInputModifier* CreateModifier(UInputAction* Outer, const FString& ModifierName) const;
};
