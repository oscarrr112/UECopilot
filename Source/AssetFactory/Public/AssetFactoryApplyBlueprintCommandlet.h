// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AssetFactoryApplyBlueprintCommandlet.generated.h"

/**
 * Apply blueprint logic JSON to an existing Blueprint asset.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe <Project.uproject> -run=AssetFactoryApplyBlueprint -asset=/Game/BP_X.BP_X -json=C:/temp/change.json -merge -save
 */
UCLASS()
class ASSETFACTORY_API UAssetFactoryApplyBlueprintCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAssetFactoryApplyBlueprintCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	bool ParseParameters(
		const FString& Params,
		FString& OutAssetPath,
		FString& OutJsonPath,
		bool& bOutMerge,
		bool& bOutSaveAsset);

	void PrintHelp() const;
};
