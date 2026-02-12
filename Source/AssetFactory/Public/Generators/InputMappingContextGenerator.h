// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

/**
 * Generator for UInputMappingContext (Enhanced Input)
 *
 * JSON Config:
 * {
 *   "AssetType": "InputMappingContext",
 *   "Name": "IMC_Default",
 *   "Path": "/Game/Input",
 *   "Mappings": [
 *     {
 *       "Action": "/Game/Input/Actions/IA_Move",
 *       "Key": "W",
 *       "Triggers": ["Pressed"],     // Optional
 *       "Modifiers": ["SwizzleAxis"] // Optional
 *     },
 *     {
 *       "Action": "/Game/Input/Actions/IA_Attack",
 *       "Key": "LeftMouseButton"
 *     }
 *   ]
 * }
 */
class ASSETFACTORY_API FInputMappingContextGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("InputMappingContext"); }
	virtual int32 GetPriority() const override { return 100; } // Lower priority - processed after InputAction

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
	/** Parse key name to FKey */
	FKey ParseKey(const FString& KeyName) const;

	/** Create trigger from name */
	class UInputTrigger* CreateTrigger(UObject* Outer, const FString& TriggerName) const;
	FString TriggerToString(class UInputTrigger* Trigger) const;

	/** Create modifier from name */
	class UInputModifier* CreateModifier(UObject* Outer, const FString& ModifierName) const;
	FString ModifierToString(class UInputModifier* Modifier) const;
};
