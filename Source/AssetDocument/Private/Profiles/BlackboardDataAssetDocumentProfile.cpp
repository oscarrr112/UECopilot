// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/BlackboardDataAssetDocumentProfile.h"

#include "AssetDocumentPolicyRegistry.h"

#include "BehaviorTree/BlackboardData.h"
#include "Dom/JsonValue.h"

namespace
{
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

FAssetDocumentCapabilityResult NotImplementedResult(const FString& Operation)
{
	return FAssetDocumentCapabilityResult::Failure(
		FString::Printf(TEXT("BlackboardData AssetDocument %s is not implemented in this checkpoint"), *Operation),
		TEXT("/Body"),
		TEXT("NotImplemented"));
}
}

TArray<FName> FBlackboardDataAssetDocumentCapability::GetCanonicalBodyKeys()
{
	return {
		TEXT("Parent"),
		TEXT("Keys"),
	};
}

FName FBlackboardDataAssetDocumentCapability::GetName() const
{
	return TEXT("BlackboardDataBodyCapability");
}

TArray<FName> FBlackboardDataAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {
		FBlackboardDataAssetDocumentProfile::ParentRegionAdapterName(),
		FBlackboardDataAssetDocumentProfile::KeysRegionAdapterName(),
	};
}

int32 FBlackboardDataAssetDocumentCapability::GetApplyOrder() const
{
	return 100;
}

bool FBlackboardDataAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && SupportsClass(Asset->GetClass());
}

bool FBlackboardDataAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UBlackboardData::StaticClass();
}

TSharedRef<FJsonObject> FBlackboardDataAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Parent"), TEXT("AssetRef<UBlackboardData> | null"));
	Schema->SetStringField(TEXT("Keys"), TEXT("array<BlackboardKey>"));
	return Schema;
}

FAssetDocumentCapabilityResult FBlackboardDataAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&) const
{
	return NotImplementedResult(TEXT("validation"));
}

FAssetDocumentCapabilityResult FBlackboardDataAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&)
{
	return NotImplementedResult(TEXT("apply"));
}

FAssetDocumentCapabilityResult FBlackboardDataAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext&, TSharedRef<FJsonObject>&) const
{
	return NotImplementedResult(TEXT("extract"));
}

FAssetDocumentCapabilityResult FBlackboardDataAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&, TArray<TSharedPtr<FJsonValue>>&) const
{
	return NotImplementedResult(TEXT("diff"));
}

FName FBlackboardDataAssetDocumentProfile::ParentRegionAdapterName()
{
	return TEXT("BlackboardDataParentRegionAdapter");
}

FName FBlackboardDataAssetDocumentProfile::KeysRegionAdapterName()
{
	return TEXT("BlackboardDataKeysRegionAdapter");
}

TArray<FAssetDocumentRegionBinding> FBlackboardDataAssetDocumentProfile::MakeRegionBindings()
{
	return {
		{TEXT("Parent"), TEXT("Body.Parent"), ParentRegionAdapterName(), 10, false},
		{TEXT("Keys"), TEXT("Body.Keys"), KeysRegionAdapterName(), 20, false},
	};
}

UClass* FBlackboardDataAssetDocumentProfile::GetExactClass() const
{
	return UBlackboardData::StaticClass();
}

TSharedRef<FJsonObject> FBlackboardDataAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected UBlackboardData properties not owned by Body regions"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FBlackboardDataAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(TEXT("Parent"), MakeShared<FJsonValueNull>());
	Body->SetArrayField(TEXT("Keys"), TArray<TSharedPtr<FJsonValue>>());

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BlackboardData"));
	Template->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FBlackboardDataAssetDocumentProfile::GetBodyKeys() const
{
	return FBlackboardDataAssetDocumentCapability::GetCanonicalBodyKeys();
}

const IAssetDocumentCapability* FBlackboardDataAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : FBlackboardDataAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}

	return nullptr;
}

TArray<FAssetDocumentRegionPolicy> FBlackboardDataAssetDocumentProfile::GetRegionPolicies() const
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(2);

	FAssetDocumentRegionPolicy Policy;
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Parent"), EAssetDocumentRegionKind::Object, {TEXT("Parent")}, Policy))
	{
		Policy.ExplicitDeleteValues.Add(FAssetDocumentExplicitDeleteValues::Null());
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Keys"), EAssetDocumentRegionKind::Array, {TEXT("Keys")}, Policy))
	{
		Policies.Add(Policy);
	}

	return Policies;
}
