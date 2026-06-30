// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimMontageAssetDocumentProfile.h"

#include "AssetDocumentPolicyRegistry.h"

#include "Animation/AnimMontage.h"
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

UClass* FAnimMontageAssetDocumentProfile::GetExactClass() const
{
	return UAnimMontage::StaticClass();
}

TSharedRef<FJsonObject> FAnimMontageAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected CDO-diff properties"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FAnimMontageAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(TEXT("Skeleton"), MakeShared<FJsonValueNull>());
	Body->SetField(TEXT("PreviewMesh"), MakeShared<FJsonValueNull>());
	Body->SetObjectField(TEXT("References"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("Preview"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("Sync"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("RootMotion"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("Metadata"), MakeEmptyArray());
	Body->SetObjectField(TEXT("SectionMetadata"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("TimeStretch"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("Curves"), MakeEmptyArray());
	Body->SetArrayField(TEXT("SlotAnimTracks"), MakeEmptyArray());
	Body->SetArrayField(TEXT("CompositeSections"), MakeEmptyArray());
	Body->SetArrayField(TEXT("Notifies"), MakeEmptyArray());
	Body->SetArrayField(TEXT("NotifyStates"), MakeEmptyArray());
	Body->SetObjectField(TEXT("Blend"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimMontage"));
	Template->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FAnimMontageAssetDocumentProfile::GetBodyKeys() const
{
	return FAnimMontageAssetDocumentCapability::GetCanonicalBodyKeys();
}

const IAssetDocumentCapability* FAnimMontageAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : FAnimMontageAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}

	return nullptr;
}

TArray<FAssetDocumentRegionPolicy> FAnimMontageAssetDocumentProfile::GetRegionPolicies() const
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(13);

	FAssetDocumentRegionPolicy Policy;
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Blend"), EAssetDocumentRegionKind::Object, {}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.References"),
		EAssetDocumentRegionKind::Object,
		{TEXT("Skeleton")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.Preview"),
		EAssetDocumentRegionKind::Object,
		{TEXT("PreviewMesh"), TEXT("PreviewBasePose")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.Sync"),
		EAssetDocumentRegionKind::Object,
		{TEXT("SyncGroup"), TEXT("SyncSlotIndex")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.RootMotion"),
		EAssetDocumentRegionKind::Object,
		{TEXT("bEnableRootMotionTranslation"), TEXT("bEnableRootMotionRotation"), TEXT("RootMotionRootLock")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("ManagedRegion"),
		TEXT("Body.Metadata"),
		EAssetDocumentRegionKind::Array,
		{TEXT("MetaData")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("ManagedRegion"),
		TEXT("Body.SectionMetadata"),
		EAssetDocumentRegionKind::Object,
		{TEXT("CompositeSections")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("DefaultDiff"),
		TEXT("Body.TimeStretch"),
		EAssetDocumentRegionKind::Object,
		{TEXT("TimeStretchCurve"), TEXT("TimeStretchCurveName")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("ManagedRegion"),
		TEXT("Body.Curves"),
		EAssetDocumentRegionKind::Array,
		{TEXT("RawCurveData")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.SlotAnimTracks"), EAssetDocumentRegionKind::Array, {TEXT("SlotAnimTracks")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.CompositeSections"), EAssetDocumentRegionKind::Timeline, {TEXT("CompositeSections")}, Policy))
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

	return Policies;
}
