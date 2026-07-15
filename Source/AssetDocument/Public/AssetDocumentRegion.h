// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct ASSETDOCUMENT_API FAssetDocumentRegionContext
{
	UObject* Asset = nullptr;
	UClass* AssetClass = nullptr;
	FString TargetAssetPath;
	FString SourceDocumentPath;
	const TSharedPtr<FJsonObject>* Definitions = nullptr;
	/** Whole desired Body for operations whose region semantics depend on sibling regions. */
	TSharedPtr<FJsonObject> DesiredBody;
	FAssetDocumentResult* Result = nullptr;
	bool bIsDryRun = false;

	const IAssetDocumentProfile* Profile = nullptr;
	const FAssetDocumentRegionPolicy* Policy = nullptr;
	FName RegionId;
	FString BodyPath;
	FString JsonPointer;
};

struct ASSETDOCUMENT_API FAssetDocumentRegionBinding
{
	FName BodyKey;
	FName RegionId;
	FName AdapterName;
	int32 ApplyOrder = 0;
	bool bRequired = false;
};

class ASSETDOCUMENT_API IAssetDocumentRegionAdapter
{
public:
	virtual ~IAssetDocumentRegionAdapter() = default;

	virtual FName GetName() const = 0;
	virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const = 0;
	virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext& Context) const = 0;

	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const = 0;

	virtual FAssetDocumentCapabilityResult PreflightRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const
	{
		return ValidateRegion(Context, DesiredValue);
	}

	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) = 0;

	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext& Context,
		TSharedPtr<FJsonValue>& OutCurrentValue) const = 0;

	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const = 0;
};
