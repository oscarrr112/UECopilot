// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"
#include "EdGraph/EdGraphPin.h"

/**
 * Generator for Blueprint classes
 *
 * JSON Config:
 * {
 *   "AssetType": "Blueprint",
 *   "Name": "BP_Enemy",
 *   "Path": "/Game/Blueprints",
 *   "ParentClass": "Character",      // Actor, Pawn, Character, PlayerController, etc.
 *   "Interfaces": ["IDamageable"],   // Optional: interfaces to implement
 *   "Variables": [                   // Optional: blueprint variables
 *     {"Name": "Health", "Type": "Float", "DefaultValue": "100.0"},
 *     {"Name": "bIsAlive", "Type": "Boolean", "DefaultValue": "true"}
 *   ],
 *   "Components": [                  // Optional: components (Actor blueprints only)
 *     {"Name": "Mesh", "Class": "StaticMeshComponent", "bIsRoot": true,
 *      "Properties": {"StaticMesh": "/Game/Meshes/SM_Cube"}},
 *     {"Name": "Collision", "Class": "BoxComponent", "AttachTo": "Mesh",
 *      "Properties": {"BoxExtent": [50, 50, 50]}}
 *   ],
 *   "DefaultProperties": {           // Optional: CDO default values
 *     "MaxHealth": 100.0
 *   }
 * }
 */
class ASSETFACTORY_API FBlueprintGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("Blueprint"); }
	virtual int32 GetPriority() const override { return 200; } // Lowest priority - processed last

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action = EGenerationAction::Create) const override;
	virtual TArray<FString> GetRequiredFields() const override;

	//~ Extract functionality
	virtual bool CanExtract(UObject* Asset) const override;
	virtual TSharedPtr<FJsonObject> Extract(UObject* Asset, bool bDiffOnly = false) const override;

protected:
	/** Set default properties on CDO using utility class. Returns false if any property failed. */
	bool SetDefaultProperties(UBlueprint* Blueprint, TSharedPtr<FJsonObject> Properties);

	/** Add variables to blueprint */
	void AddVariables(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>* VariablesArray);

	/** Add components to blueprint (Actor-based only) */
	void AddComponents(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>* ComponentsArray);

	/** Map type string to FEdGraphPinType */
	FEdGraphPinType GetPinTypeFromString(const FString& TypeString);

	//~ Extract Helpers
	/** Extract components from SCS */
	TArray<TSharedPtr<FJsonValue>> ExtractComponents(UBlueprint* Blueprint) const;

	/** Extract variables */
	TArray<TSharedPtr<FJsonValue>> ExtractVariables(UBlueprint* Blueprint) const;

	/** Convert FEdGraphPinType to type string */
	FString PinTypeToString(const FEdGraphPinType& PinType) const;

	/** Extract component properties via reflection */
	TSharedPtr<FJsonObject> ExtractComponentProperties(UActorComponent* Component, bool bDiffOnly) const;
};
