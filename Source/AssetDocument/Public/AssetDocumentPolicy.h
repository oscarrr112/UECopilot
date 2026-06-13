// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EAssetDocumentRegionKind : uint8
{
	Scalar,
	Object,
	Array,
	Timeline,
	Graph
};

enum class EAssetDocumentDefaultSource : uint8
{
	CDO,
	EmptyTemplate,
	CurrentAssetBaseline,
	ProfileDeclared
};

enum class EAssetDocumentReducerMode : uint8
{
	DefaultDiff,
	ManagedRegion
};

enum class EAssetDocumentApplyMode : uint8
{
	SetProperty,
	RebuildArrayRegion,
	ExtensionHook
};

struct ASSETDOCUMENT_API FAssetDocumentIdentityRule
{
	FString FieldPath;
	FString UePropertyPath;
	FString StableKeyField;
};

struct ASSETDOCUMENT_API FAssetDocumentComparisonRule
{
	FString FieldPath;
	FString ComparatorName;
	bool bIgnoreOrder = false;
};

struct ASSETDOCUMENT_API FAssetDocumentRegionPolicy
{
	FName RegionId;
	FString BodyPath;
	EAssetDocumentRegionKind RegionKind = EAssetDocumentRegionKind::Object;
	EAssetDocumentDefaultSource DefaultSource = EAssetDocumentDefaultSource::ProfileDeclared;
	EAssetDocumentReducerMode ReducerMode = EAssetDocumentReducerMode::DefaultDiff;
	EAssetDocumentApplyMode ApplyMode = EAssetDocumentApplyMode::SetProperty;
	TArray<FAssetDocumentIdentityRule> IdentityRules;
	TArray<FAssetDocumentComparisonRule> ComparisonRules;
	TArray<FString> ManagedUePropertyPaths;
	TSet<FString> ExtractOnlyFields;
	TSet<FString> ExplicitDeleteValues;
	TOptional<FName> ExtensionHookName;
};

struct ASSETDOCUMENT_API FAssetDocumentRegionPolicyPreset
{
	FName PresetName;
	int32 PolicyVersion = 1;
	FAssetDocumentRegionPolicy Defaults;
};

struct ASSETDOCUMENT_API FAssetDocumentRegionPolicyOverride
{
	TOptional<FName> RegionId;
	TOptional<FString> BodyPath;
	TOptional<EAssetDocumentRegionKind> RegionKind;
	TOptional<EAssetDocumentDefaultSource> DefaultSource;
	TOptional<EAssetDocumentReducerMode> ReducerMode;
	TOptional<EAssetDocumentApplyMode> ApplyMode;
	TOptional<TArray<FAssetDocumentIdentityRule>> IdentityRules;
	TOptional<TArray<FAssetDocumentComparisonRule>> ComparisonRules;
	TOptional<TArray<FString>> ManagedUePropertyPaths;
	TOptional<TSet<FString>> ExtractOnlyFields;
	TOptional<TSet<FString>> ExplicitDeleteValues;
	TOptional<FName> ExtensionHookName;
};
