// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimSequenceAssetDocumentProfile.h"

#include "AssetDocumentPolicyRegistry.h"

#include "Animation/AnimSequence.h"
#include "Dom/JsonValue.h"

namespace
{
bool MakeRegionPolicy(
	FName PresetName,
	FName RegionId,
	EAssetDocumentRegionKind RegionKind,
	TArray<FString> ManagedUePropertyPaths,
	FAssetDocumentRegionPolicy& OutPolicy,
	FName CanonicalizerHookName = NAME_None)
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
	if (!CanonicalizerHookName.IsNone())
	{
		Override.CanonicalizerHookName = CanonicalizerHookName;
	}
	return FAssetDocumentPolicyRegistry::ExpandPreset(Preset, Override, OutPolicy);
}
}

UClass* FAnimSequenceAssetDocumentProfile::GetExactClass() const
{
	return UAnimSequence::StaticClass();
}

FName FAnimSequenceAssetDocumentProfile::PilotObjectRegionAdapterName()
{
	return TEXT("AnimSequenceObjectRegionAdapter");
}

FName FAnimSequenceAssetDocumentProfile::PreviewObjectRegionAdapterName()
{
	return TEXT("AnimSequencePreviewObjectRegionAdapter");
}

FName FAnimSequenceAssetDocumentProfile::PlaybackObjectRegionAdapterName()
{
	return TEXT("AnimSequencePlaybackObjectRegionAdapter");
}

FName FAnimSequenceAssetDocumentProfile::NotifyTracksNamedArrayRegionAdapterName()
{
	return TEXT("AnimSequenceNotifyTracksNamedArrayRegionAdapter");
}

FAssetDocumentNamedArrayRegionAdapterConfig FAnimSequenceAssetDocumentProfile::MakeNotifyTracksNamedArrayConfig()
{
	FAssetDocumentNamedArrayRegionAdapterConfig Config;
	Config.Name = NotifyTracksNamedArrayRegionAdapterName();
	Config.IdentityField = TEXT("TrackName");
	Config.IdentityAliases = {TEXT("Name")};
	Config.MissingIdentityCode = TEXT("InvalidStringField");
	Config.DuplicateIdentityCode = TEXT("DuplicateNotifyTrackName");
	Config.NormalizeIdentity = [](const FString& Identity)
	{
		return FName(*Identity).ToString().ToLower();
	};
	Config.bCanonicalizeByIdentity = false;
	Config.bPreserveAuthoredApplyOrder = true;
	return Config;
}

TArray<FString> FAnimSequenceAssetDocumentProfile::MakeNotifyTracksIdentityFieldNames()
{
	const FAssetDocumentNamedArrayRegionAdapterConfig Config = MakeNotifyTracksNamedArrayConfig();
	TArray<FString> FieldNames;
	FieldNames.Reserve(1 + Config.IdentityAliases.Num());
	FieldNames.Add(Config.IdentityField);
	FieldNames.Append(Config.IdentityAliases);
	return FieldNames;
}

FString FAnimSequenceAssetDocumentProfile::MakeNotifyTracksIdentityJsonPointer(int32 Index)
{
	const FAssetDocumentNamedArrayRegionAdapterConfig Config = MakeNotifyTracksNamedArrayConfig();
	return FString::Printf(TEXT("/Body/NotifyTracks/%d/%s"), Index, *Config.IdentityField);
}

TArray<FAssetDocumentRegionBinding> FAnimSequenceAssetDocumentProfile::MakePilotRegionBindings()
{
	return {
		{TEXT("Preview"), TEXT("Body.Preview"), PilotObjectRegionAdapterName(), 10, false},
		{TEXT("Playback"), TEXT("Body.Playback"), PilotObjectRegionAdapterName(), 20, false},
		{TEXT("NotifyTracks"), TEXT("Body.NotifyTracks"), NotifyTracksNamedArrayRegionAdapterName(), 30, false},
	};
}

TArray<FAssetDocumentRegionPolicy> FAnimSequenceAssetDocumentProfile::MakePilotRegionPolicies()
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(3);

	FAssetDocumentRegionPolicy Policy;
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.Preview"),
		EAssetDocumentRegionKind::Object,
		{TEXT("PreviewSkeletalMesh")},
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
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.NotifyTracks"), EAssetDocumentRegionKind::Array, {TEXT("AnimNotifyTracks")}, Policy, TEXT("AnimSequencePostApply")))
	{
		Policies.Add(Policy);
	}

	return Policies;
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
	Policies.Append(MakePilotRegionPolicies());
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
		TEXT("Body.Additive"),
		EAssetDocumentRegionKind::Object,
		{TEXT("AdditiveAnimType"), TEXT("RefPoseType"), TEXT("RefFrameIndex"), TEXT("RefPoseSeq")},
		Policy,
		TEXT("AnimSequencePostApply")))
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
		Policy,
		TEXT("AnimSequencePostApply")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Curves"), EAssetDocumentRegionKind::Array, {TEXT("RawCurveData")}, Policy, TEXT("AnimSequencePostApply")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Notifies"), EAssetDocumentRegionKind::Timeline, {TEXT("Notifies")}, Policy, TEXT("AnimSequencePostApply")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.NotifyStates"), EAssetDocumentRegionKind::Timeline, {TEXT("Notifies")}, Policy, TEXT("AnimSequencePostApply")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.SyncMarkers"), EAssetDocumentRegionKind::Timeline, {TEXT("AuthoredSyncMarkers")}, Policy, TEXT("AnimSequencePostApply")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Metadata"), EAssetDocumentRegionKind::Array, {TEXT("MetaData")}, Policy, TEXT("AnimSequencePostApply")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.AssetUserData"), EAssetDocumentRegionKind::Array, {TEXT("AssetUserData")}, Policy, TEXT("AnimSequencePostApply")))
	{
		Policies.Add(Policy);
	}

	return Policies;
}
