// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimBlueprintAssetDocumentProfile.h"

#include "AssetDocumentPolicyRegistry.h"

#include "Animation/AnimBlueprint.h"
#include "Dom/JsonValue.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> MakeEmptyArray()
{
	return TArray<TSharedPtr<FJsonValue>>();
}

TSharedRef<FJsonObject> MakeCanonicalAnimGraphObject()
{
	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Id"), TEXT("AnimGraph"));
	Graph->SetStringField(TEXT("Kind"), TEXT("AnimGraph"));
	Graph->SetField(TEXT("Owner"), MakeShared<FJsonValueNull>());
	Graph->SetArrayField(TEXT("Nodes"), {});
	Graph->SetArrayField(TEXT("Links"), {});
	Graph->SetArrayField(TEXT("Subgraphs"), {});

	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), {MakeShared<FJsonValueObject>(Graph)});
	return Region;
}

TSharedRef<FJsonObject> MakeEmptyGraphRegionObject()
{
	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), MakeEmptyArray());
	return Region;
}

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

void MarkDeferredRegionPolicy(FAssetDocumentRegionPolicy& Policy)
{
	Policy.ExplicitDeleteValues.Add(FAssetDocumentExplicitDeleteValues::Null());
	Policy.ExplicitDeleteValues.Add(FAssetDocumentExplicitDeleteValues::EmptyArray());
	Policy.ExplicitDeleteValues.Add(FAssetDocumentExplicitDeleteValues::EmptyObject());
}

TSharedRef<FJsonObject> MakeClassRef(const FString& ClassPath)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), ClassPath);
	return ClassRef;
}
}

FName FAnimBlueprintAssetDocumentProfile::ObjectRegionAdapterName()
{
	return TEXT("AnimBlueprintObjectRegionAdapter");
}

FName FAnimBlueprintAssetDocumentProfile::TargetSkeletonRegionAdapterName()
{
	return TEXT("AnimBlueprintTargetSkeletonRegionAdapter");
}

FName FAnimBlueprintAssetDocumentProfile::SyncGroupsRegionAdapterName()
{
	return TEXT("AnimBlueprintSyncGroupsNamedArrayRegionAdapter");
}

FName FAnimBlueprintAssetDocumentProfile::BlueprintCommonRegionAdapterName()
{
	return TEXT("AnimBlueprintBlueprintCommonRegionAdapter");
}

FName FAnimBlueprintAssetDocumentProfile::AnimGraphRegionAdapterName()
{
	return TEXT("AnimBlueprintAnimGraphRegionAdapter");
}

FName FAnimBlueprintAssetDocumentProfile::StateMachineRegionAdapterName()
{
	return TEXT("AnimBlueprintStateMachineRegionAdapter");
}

FName FAnimBlueprintAssetDocumentProfile::AnimLayerRegionAdapterName()
{
	return TEXT("AnimBlueprintAnimLayerRegionAdapter");
}

FName FAnimBlueprintAssetDocumentProfile::ParentAssetOverrideRegionAdapterName()
{
	return TEXT("AnimBlueprintParentAssetOverrideRegionAdapter");
}

FName FAnimBlueprintAssetDocumentProfile::DeferredRegionAdapterName()
{
	return TEXT("AnimBlueprintDeferredRegionAdapter");
}

TArray<FAssetDocumentRegionBinding> FAnimBlueprintAssetDocumentProfile::MakeRegionBindings()
{
	return {
		{TEXT("ParentClass"), TEXT("Body.ParentClass"), ObjectRegionAdapterName(), 10, false},
		{TEXT("TargetSkeleton"), TEXT("Body.TargetSkeleton"), TargetSkeletonRegionAdapterName(), 20, false},
		{TEXT("Template"), TEXT("Body.Template"), ObjectRegionAdapterName(), 30, false},
		{TEXT("Preview"), TEXT("Body.Preview"), ObjectRegionAdapterName(), 40, false},
		{TEXT("Optimization"), TEXT("Body.Optimization"), ObjectRegionAdapterName(), 50, false},
		{TEXT("SyncGroups"), TEXT("Body.SyncGroups"), SyncGroupsRegionAdapterName(), 60, false},
		{TEXT("ImplementedInterfaces"), TEXT("Body.ImplementedInterfaces"), BlueprintCommonRegionAdapterName(), 70, false},
		{TEXT("Variables"), TEXT("Body.Variables"), BlueprintCommonRegionAdapterName(), 80, false},
		{TEXT("ClassDefaults"), TEXT("Body.ClassDefaults"), BlueprintCommonRegionAdapterName(), 90, false},
		{TEXT("UbergraphPages"), TEXT("Body.UbergraphPages"), BlueprintCommonRegionAdapterName(), 100, false},
		{TEXT("FunctionGraphs"), TEXT("Body.FunctionGraphs"), BlueprintCommonRegionAdapterName(), 110, false},
		{TEXT("MacroGraphs"), TEXT("Body.MacroGraphs"), BlueprintCommonRegionAdapterName(), 120, false},
		{TEXT("AnimGraph"), TEXT("Body.AnimGraph"), AnimGraphRegionAdapterName(), 200, false},
		{TEXT("StateMachines"), TEXT("Body.StateMachines"), StateMachineRegionAdapterName(), 210, false},
		{TEXT("TransitionGraphs"), TEXT("Body.TransitionGraphs"), StateMachineRegionAdapterName(), 220, false},
		{TEXT("AnimLayers"), TEXT("Body.AnimLayers"), AnimLayerRegionAdapterName(), 230, false},
		{TEXT("ParentAssetOverrides"), TEXT("Body.ParentAssetOverrides"), ParentAssetOverrideRegionAdapterName(), 240, false},
	};
}

UClass* FAnimBlueprintAssetDocumentProfile::GetExactClass() const
{
	return UAnimBlueprint::StaticClass();
}

TSharedRef<FJsonObject> FAnimBlueprintAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected UAnimBlueprint properties not owned by Body regions"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FAnimBlueprintAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeClassRef(TEXT("/Script/Engine.AnimInstance")));
	Body->SetField(TEXT("TargetSkeleton"), MakeShared<FJsonValueNull>());

	TSharedRef<FJsonObject> TemplateRegion = MakeShared<FJsonObject>();
	TemplateRegion->SetBoolField(TEXT("bIsTemplate"), false);
	Body->SetObjectField(TEXT("Template"), TemplateRegion);

	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetField(TEXT("PreviewSkeletalMesh"), MakeShared<FJsonValueNull>());
	Preview->SetField(TEXT("PreviewAnimationBlueprint"), MakeShared<FJsonValueNull>());
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), TEXT("LinkedLayers"));
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintTag"), TEXT(""));
	Body->SetObjectField(TEXT("Preview"), Preview);

	TSharedRef<FJsonObject> Optimization = MakeShared<FJsonObject>();
	Optimization->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), true);
	Optimization->SetBoolField(TEXT("bWarnAboutBlueprintUsage"), false);
	Optimization->SetBoolField(TEXT("bEnableLinkedAnimLayerInstanceSharing"), false);
	Body->SetObjectField(TEXT("Optimization"), Optimization);

	Body->SetArrayField(TEXT("SyncGroups"), MakeEmptyArray());
	Body->SetArrayField(TEXT("ImplementedInterfaces"), MakeEmptyArray());
	Body->SetArrayField(TEXT("Variables"), MakeEmptyArray());
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("UbergraphPages"), MakeEmptyArray());
	Body->SetArrayField(TEXT("FunctionGraphs"), MakeEmptyArray());
	Body->SetArrayField(TEXT("MacroGraphs"), MakeEmptyArray());
	Body->SetObjectField(TEXT("AnimGraph"), MakeCanonicalAnimGraphObject());
	Body->SetObjectField(TEXT("StateMachines"), MakeEmptyGraphRegionObject());
	Body->SetArrayField(TEXT("TransitionGraphs"), MakeEmptyArray());
	Body->SetObjectField(TEXT("AnimLayers"), MakeEmptyGraphRegionObject());
	Body->SetArrayField(TEXT("ParentAssetOverrides"), MakeEmptyArray());

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimBlueprint"));
	Template->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FAnimBlueprintAssetDocumentProfile::GetBodyKeys() const
{
	return FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys();
}

const IAssetDocumentCapability* FAnimBlueprintAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}

	return nullptr;
}

TArray<FAssetDocumentRegionPolicy> FAnimBlueprintAssetDocumentProfile::GetRegionPolicies() const
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(15);

	FAssetDocumentRegionPolicy Policy;
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.ParentClass"), EAssetDocumentRegionKind::Object, {TEXT("ParentClass")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.TargetSkeleton"), EAssetDocumentRegionKind::Object, {TEXT("TargetSkeleton")}, Policy))
	{
		Policy.ExplicitDeleteValues.Add(FAssetDocumentExplicitDeleteValues::Null());
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Template"), EAssetDocumentRegionKind::Object, {TEXT("bIsTemplate")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Preview"), EAssetDocumentRegionKind::Object, {TEXT("PreviewSkeletalMesh"), TEXT("PreviewAnimationBlueprint"), TEXT("PreviewAnimationBlueprintApplicationMethod"), TEXT("PreviewAnimationBlueprintTag")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Optimization"), EAssetDocumentRegionKind::Object, {TEXT("bUseMultiThreadedAnimationUpdate"), TEXT("bWarnAboutBlueprintUsage"), TEXT("bEnableLinkedAnimLayerInstanceSharing")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.SyncGroups"), EAssetDocumentRegionKind::Array, {TEXT("Groups")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.ImplementedInterfaces"), EAssetDocumentRegionKind::Array, {TEXT("ImplementedInterfaces")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Variables"), EAssetDocumentRegionKind::Array, {TEXT("NewVariables")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.ClassDefaults"), EAssetDocumentRegionKind::Object, {}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.UbergraphPages"), EAssetDocumentRegionKind::Graph, {TEXT("UbergraphPages")}, Policy, TEXT("UBlueprintGraph")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.FunctionGraphs"), EAssetDocumentRegionKind::Graph, {TEXT("FunctionGraphs")}, Policy, TEXT("UBlueprintGraph")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.MacroGraphs"), EAssetDocumentRegionKind::Graph, {TEXT("MacroGraphs")}, Policy, TEXT("UBlueprintGraph")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.AnimGraph"), EAssetDocumentRegionKind::Graph, {TEXT("AnimGraph")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.StateMachines"), EAssetDocumentRegionKind::Graph, {TEXT("StateMachines")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.TransitionGraphs"), EAssetDocumentRegionKind::Graph, {TEXT("TransitionGraphs")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.AnimLayers"), EAssetDocumentRegionKind::Graph, {TEXT("AnimLayers")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.ParentAssetOverrides"), EAssetDocumentRegionKind::Array, {TEXT("ParentAssetOverrides")}, Policy))
	{
		Policies.Add(Policy);
	}

	return Policies;
}
