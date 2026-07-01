// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/BehaviorTreeAssetDocumentProfile.h"

#include "AssetDocumentPolicyRegistry.h"

#include "BehaviorTree/BehaviorTree.h"
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
		FString::Printf(TEXT("BehaviorTree AssetDocument %s is not implemented in this checkpoint"), *Operation),
		TEXT("/Body"),
		TEXT("NotImplemented"));
}
}

TArray<FName> FBehaviorTreeAssetDocumentCapability::GetCanonicalBodyKeys()
{
	return {
		TEXT("Blackboard"),
		TEXT("Tree"),
		TEXT("EditorLayout"),
	};
}

FName FBehaviorTreeAssetDocumentCapability::GetName() const
{
	return TEXT("BehaviorTreeBodyCapability");
}

TArray<FName> FBehaviorTreeAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {
		FBehaviorTreeAssetDocumentProfile::BlackboardRegionAdapterName(),
		FBehaviorTreeAssetDocumentProfile::TreeRegionAdapterName(),
		FBehaviorTreeAssetDocumentProfile::EditorLayoutRegionAdapterName(),
	};
}

int32 FBehaviorTreeAssetDocumentCapability::GetApplyOrder() const
{
	return 100;
}

bool FBehaviorTreeAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && SupportsClass(Asset->GetClass());
}

bool FBehaviorTreeAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UBehaviorTree::StaticClass();
}

TSharedRef<FJsonObject> FBehaviorTreeAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetStringField(TEXT("RootDecorators"), TEXT("array<BehaviorTreeDecorator>"));
	Tree->SetStringField(TEXT("RootDecoratorLogic"), TEXT("array<BehaviorTreeDecoratorLogic>"));
	Tree->SetStringField(TEXT("Root"), TEXT("BehaviorTreeNode"));

	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Blackboard"), TEXT("AssetRef<UBlackboardData> | null"));
	Schema->SetObjectField(TEXT("Tree"), Tree);
	Schema->SetStringField(TEXT("EditorLayout"), TEXT("object"));
	return Schema;
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&) const
{
	return NotImplementedResult(TEXT("validation"));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&)
{
	return NotImplementedResult(TEXT("apply"));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext&, TSharedRef<FJsonObject>&) const
{
	return NotImplementedResult(TEXT("extract"));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&, TArray<TSharedPtr<FJsonValue>>&) const
{
	return NotImplementedResult(TEXT("diff"));
}

FName FBehaviorTreeAssetDocumentProfile::BlackboardRegionAdapterName()
{
	return TEXT("BehaviorTreeBlackboardRegionAdapter");
}

FName FBehaviorTreeAssetDocumentProfile::TreeRegionAdapterName()
{
	return TEXT("BehaviorTreeTreeRegionAdapter");
}

FName FBehaviorTreeAssetDocumentProfile::EditorLayoutRegionAdapterName()
{
	return TEXT("BehaviorTreeEditorLayoutRegionAdapter");
}

TArray<FAssetDocumentRegionBinding> FBehaviorTreeAssetDocumentProfile::MakeRegionBindings()
{
	return {
		{TEXT("Blackboard"), TEXT("Body.Blackboard"), BlackboardRegionAdapterName(), 10, false},
		{TEXT("Tree"), TEXT("Body.Tree"), TreeRegionAdapterName(), 20, false},
		{TEXT("EditorLayout"), TEXT("Body.EditorLayout"), EditorLayoutRegionAdapterName(), 30, false},
	};
}

UClass* FBehaviorTreeAssetDocumentProfile::GetExactClass() const
{
	return UBehaviorTree::StaticClass();
}

TSharedRef<FJsonObject> FBehaviorTreeAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected UBehaviorTree properties not owned by Body regions"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FBehaviorTreeAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetObjectField(TEXT("Root"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(TEXT("Blackboard"), MakeShared<FJsonValueNull>());
	Body->SetObjectField(TEXT("Tree"), Tree);
	Body->SetObjectField(TEXT("EditorLayout"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BehaviorTree"));
	Template->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FBehaviorTreeAssetDocumentProfile::GetBodyKeys() const
{
	return FBehaviorTreeAssetDocumentCapability::GetCanonicalBodyKeys();
}

const IAssetDocumentCapability* FBehaviorTreeAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : FBehaviorTreeAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}

	return nullptr;
}

TArray<FAssetDocumentRegionPolicy> FBehaviorTreeAssetDocumentProfile::GetRegionPolicies() const
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(3);

	FAssetDocumentRegionPolicy Policy;
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Blackboard"), EAssetDocumentRegionKind::Object, {TEXT("BlackboardAsset")}, Policy))
	{
		Policy.ExplicitDeleteValues.Add(FAssetDocumentExplicitDeleteValues::Null());
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(
		TEXT("ManagedRegion"),
		TEXT("Body.Tree"),
		EAssetDocumentRegionKind::Graph,
		{TEXT("RootNode"), TEXT("RootDecorators"), TEXT("RootDecoratorOps")},
		Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.EditorLayout"), EAssetDocumentRegionKind::Object, {TEXT("BTGraph")}, Policy))
	{
		Policies.Add(Policy);
	}

	return Policies;
}
