// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimSequenceAssetDocumentProfile.h"

#include "AssetDocumentPolicyRegistry.h"

#include "Animation/AnimSequence.h"
#include "Dom/JsonValue.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> MakeEmptyArray()
{
	return TArray<TSharedPtr<FJsonValue>>();
}

bool MakeRegionPolicy(
	FName PresetName,
	FName RegionId,
	EAssetDocumentRegionKind RegionKind,
	TArray<FString> ManagedUePropertyPaths,
	FAssetDocumentRegionPolicy& OutPolicy)
{
	FAssetDocumentRegionPolicyPreset Preset;
	if (!FAssetDocumentPolicyRegistry::GetBuiltinPreset(PresetName, Preset))
	{
		return false;
	}

	FAssetDocumentRegionPolicyOverride Override;
	Override.RegionId = RegionId;
	Override.BodyPath = RegionId.ToString();
	Override.RegionKind = RegionKind;
	if (ManagedUePropertyPaths.Num() > 0)
	{
		Override.ManagedUePropertyPaths = MoveTemp(ManagedUePropertyPaths);
	}
	return FAssetDocumentPolicyRegistry::ExpandPreset(Preset, Override, OutPolicy);
}
}

UClass* FAnimSequenceAssetDocumentProfile::GetExactClass() const
{
	return UAnimSequence::StaticClass();
}

TSharedRef<FJsonObject> FAnimSequenceAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected CDO-diff properties"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FAnimSequenceAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("References"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("Preview"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("Playback"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("Additive"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("RootMotion"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("Compression"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("Curves"), MakeEmptyArray());
	Body->SetArrayField(TEXT("Notifies"), MakeEmptyArray());
	Body->SetArrayField(TEXT("NotifyStates"), MakeEmptyArray());
	Body->SetArrayField(TEXT("NotifyTracks"), MakeEmptyArray());
	Body->SetArrayField(TEXT("SyncMarkers"), MakeEmptyArray());
	Body->SetArrayField(TEXT("Metadata"), MakeEmptyArray());
	Body->SetArrayField(TEXT("AssetUserData"), MakeEmptyArray());

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimSequence"));
	Template->SetStringField(TEXT("Action"), TEXT("Update"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FAnimSequenceAssetDocumentProfile::GetBodyKeys() const
{
	return FAnimSequenceAssetDocumentCapability::GetCanonicalBodyKeys();
}

const IAssetDocumentCapability* FAnimSequenceAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : FAnimSequenceAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}

	return nullptr;
}

TArray<FAssetDocumentRegionPolicy> FAnimSequenceAssetDocumentProfile::GetRegionPolicies() const
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(13);

	FAssetDocumentRegionPolicy Policy;
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.References"),
		EAssetDocumentRegionKind::Object,
		{TEXT("Skeleton"), TEXT("RetargetSource"), TEXT("RetargetSourceAsset")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.Preview"),
		EAssetDocumentRegionKind::Object,
		{TEXT("PreviewSkeletalMesh"), TEXT("PreviewPoseAsset")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.Playback"),
		EAssetDocumentRegionKind::Object,
		{TEXT("RateScale")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.Additive"),
		EAssetDocumentRegionKind::Object,
		{TEXT("AdditiveAnimType"), TEXT("RefPoseType"), TEXT("RefFrameIndex"), TEXT("RefPoseSeq")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.RootMotion"),
		EAssetDocumentRegionKind::Object,
		{TEXT("bEnableRootMotion"), TEXT("RootMotionRootLock"), TEXT("bForceRootLock"), TEXT("bUseNormalizedRootMotionScale")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.Compression"),
		EAssetDocumentRegionKind::Object,
		{TEXT("CompressionErrorThresholdScale"), TEXT("BoneCompressionSettings"), TEXT("CurveCompressionSettings"), TEXT("bDoNotOverrideCompression")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Curves"), EAssetDocumentRegionKind::Array, {TEXT("RawCurveData")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Notifies"), EAssetDocumentRegionKind::Timeline, {TEXT("Notifies")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.NotifyStates"), EAssetDocumentRegionKind::Timeline, {TEXT("Notifies")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.NotifyTracks"), EAssetDocumentRegionKind::Array, {TEXT("AnimNotifyTracks")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.SyncMarkers"), EAssetDocumentRegionKind::Timeline, {TEXT("AuthoredSyncMarkers")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Metadata"), EAssetDocumentRegionKind::Array, {TEXT("MetaData")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.AssetUserData"), EAssetDocumentRegionKind::Array, {TEXT("AssetUserData")}, Policy))
	{
		Policies.Add(Policy);
	}

	return Policies;
}
