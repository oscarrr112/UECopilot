// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"

struct FAssetDocumentTimelineRange
{
	double MinTime = 0.0;
	double MaxTime = 0.0;
	bool bHasMaxTime = false;
};

struct FAssetDocumentTimelineTrackResolveRequest
{
	FName RegionId;
	int32 EntryIndex = INDEX_NONE;
	FString JsonPointer;
	TOptional<FString> TrackName;
	TOptional<int32> TrackIndex;
};

struct FAssetDocumentTimelineTrackResolveResult
{
	bool bResolved = false;
	int32 TrackIndex = INDEX_NONE;
	FString CanonicalTrackName;
	FAssetDocumentCapabilityResult Error =
		FAssetDocumentCapabilityResult::Success(TEXT("No track resolution required"));
};

struct FAssetDocumentTimelineTrackResolver
{
	TFunction<FAssetDocumentTimelineTrackResolveResult(const FAssetDocumentTimelineTrackResolveRequest&)> Resolve;
};

struct FAssetDocumentTimelinePlacementRegionConfig
{
	FName AdapterName;
	FName RegionId;
	FString BodyPath;
	FString JsonPointer;
	FString SchemaLabel;

	FString TimeFieldName = TEXT("Time");
	bool bRequireTime = true;
	bool bAllowZeroTime = true;

	FString DurationFieldName;
	bool bHasDuration = false;
	bool bRequireDuration = false;
	bool bRequirePositiveDuration = true;
	bool bValidateEndTime = false;

	FString NameFieldName;
	bool bHasName = false;
	bool bRequireName = false;

	FString TrackNameFieldName;
	FString TrackIndexFieldName;
	bool bHasTrackIdentity = false;
	bool bRequireTrackIdentity = false;

	bool bEnableDefaultDiff = true;
	bool bPreserveProfileDiagnosticPaths = true;

	FAssetDocumentTimelineTrackResolver TrackResolver;
};

struct FAssetDocumentTimelinePlacementEntry
{
	int32 Index = INDEX_NONE;
	FString JsonPointer;
	TSharedPtr<FJsonObject> EntryObject;
	TOptional<double> Time;
	TOptional<double> Duration;
	TOptional<FString> Name;
	TOptional<FString> TrackName;
	TOptional<int32> TrackIndex;
	TOptional<int32> ResolvedTrackIndex;
	FString CanonicalTrackName;
	FString DuplicateKey;
};

struct FAssetDocumentTimelinePlacementHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		TArray<FAssetDocumentTimelinePlacementEntry>&)> Validate;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		TArray<FAssetDocumentTimelinePlacementEntry>&)> Preflight;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		const TArray<FAssetDocumentTimelinePlacementEntry>&,
		bool&)> Apply;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		TArray<TSharedRef<FJsonObject>>&)> Extract;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		const TArray<FAssetDocumentTimelinePlacementEntry>&,
		TArray<TSharedPtr<FJsonValue>>&)> Diff;

	TFunction<FString(const FAssetDocumentTimelinePlacementEntry&)> BuildDuplicateKey;
	TFunction<FAssetDocumentTimelineRange(const FAssetDocumentRegionContext&)> GetTimelineRange;
};

struct FAssetDocumentTimelinePlacementUtils
{
	static FString MakeEntryPath(const FString& BasePath, int32 Index);
	static FString MakeFieldPath(const FString& EntryPath, const FString& FieldName);
	static FString CanonicalizeTimeForKey(double Value);
	static FString BuildDefaultDuplicateKey(const FAssetDocumentTimelinePlacementRegionConfig& Config, const FAssetDocumentTimelinePlacementEntry& Entry);
	static FAssetDocumentCapabilityResult ParsePlacementEntries(
		const TSharedPtr<FJsonValue>& Value,
		const FAssetDocumentTimelinePlacementRegionConfig& Config,
		const FString& BasePath,
		const FAssetDocumentTimelineRange* Range,
		TArray<FAssetDocumentTimelinePlacementEntry>& OutEntries,
		const FAssetDocumentTimelinePlacementHooks* Hooks = nullptr,
		bool bValidateDuplicateKeys = true);
	static FAssetDocumentCapabilityResult ValidateDuplicateKeys(
		const FAssetDocumentTimelinePlacementRegionConfig& Config,
		TArray<FAssetDocumentTimelinePlacementEntry>& Entries,
		const FAssetDocumentTimelinePlacementHooks* Hooks = nullptr);
	static TSharedRef<FJsonValue> MakeArrayValue(const TArray<TSharedRef<FJsonObject>>& Entries);
};

class FAssetDocumentTimelinePlacementRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	FAssetDocumentTimelinePlacementRegionAdapter(
		FAssetDocumentTimelinePlacementRegionConfig InConfig,
		FAssetDocumentTimelinePlacementHooks InHooks);

	virtual FName GetName() const override;
	virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const override;
	virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext& Context) const override;
	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override;
	virtual FAssetDocumentCapabilityResult PreflightRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override;
	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) override;
	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext& Context,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override;
	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override;

private:
	FString RegionPath(const FAssetDocumentRegionContext& Context) const;
	FAssetDocumentCapabilityResult ParseEntries(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<FAssetDocumentTimelinePlacementEntry>& OutEntries) const;

	FAssetDocumentTimelinePlacementRegionConfig Config;
	FAssetDocumentTimelinePlacementHooks Hooks;
};
