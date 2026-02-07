// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

/**
 * Generator for UDataTable assets from CSV files
 *
 * JSON Config:
 * {
 *   "AssetType": "DataTable",
 *   "Name": "DT_Heroes",
 *   "Path": "/Game/Data",
 *   "RowStruct": "FHeroStats",
 *   "CSVFilePath": "D:/MyProject/Data/Heroes.csv"
 * }
 */
class ASSETFACTORY_API FDataTableGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("DataTable"); }
	virtual int32 GetPriority() const override { return 20; }

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config) const override;
	virtual TArray<FString> GetRequiredFields() const override;

	//~ Extract functionality
	virtual bool CanExtract(UObject* Asset) const override;
	virtual TSharedPtr<FJsonObject> Extract(UObject* Asset, bool bDiffOnly = false) const override;

private:
	/** Find a UScriptStruct by name that derives from FTableRowBase */
	UScriptStruct* FindRowStruct(const FString& StructName) const;
};
