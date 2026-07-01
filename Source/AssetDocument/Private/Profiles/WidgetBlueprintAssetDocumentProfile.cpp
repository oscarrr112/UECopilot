// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/WidgetBlueprintAssetDocumentProfile.h"

#include "AssetDocumentPolicyRegistry.h"

#include "Dom/JsonValue.h"
#include "WidgetBlueprint.h"

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
}
}

UClass* FWidgetBlueprintAssetDocumentProfile::GetExactClass() const
{
	return UWidgetBlueprint::StaticClass();
}

TSharedRef<FJsonObject> FWidgetBlueprintAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected UWidgetBlueprint properties not owned by Body regions"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FWidgetBlueprintAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/UMG.UserWidget"));

	TSharedRef<FJsonObject> WidgetTree = MakeShared<FJsonObject>();
	WidgetTree->SetField(TEXT("RootWidget"), MakeShared<FJsonValueNull>());
	WidgetTree->SetObjectField(TEXT("NamedSlotBindings"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), ParentClass);
	Body->SetArrayField(TEXT("ImplementedInterfaces"), MakeEmptyArray());
	Body->SetArrayField(TEXT("Variables"), MakeEmptyArray());
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("WidgetTree"), WidgetTree);
	Body->SetArrayField(TEXT("Bindings"), MakeEmptyArray());
	Body->SetArrayField(TEXT("Animations"), MakeEmptyArray());
	Body->SetArrayField(TEXT("UbergraphPages"), MakeEmptyArray());
	Body->SetArrayField(TEXT("FunctionGraphs"), MakeEmptyArray());
	Body->SetArrayField(TEXT("MacroGraphs"), MakeEmptyArray());
	Body->SetObjectField(TEXT("Palette"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("EditorOptions"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("WidgetVariableGuids"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/UMGEditor.WidgetBlueprint"));
	Template->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FWidgetBlueprintAssetDocumentProfile::GetBodyKeys() const
{
	return FWidgetBlueprintAssetDocumentCapability::GetCanonicalBodyKeys();
}

const IAssetDocumentCapability* FWidgetBlueprintAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : FWidgetBlueprintAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}

	return nullptr;
}

TArray<FAssetDocumentRegionPolicy> FWidgetBlueprintAssetDocumentProfile::GetRegionPolicies() const
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(13);

	FAssetDocumentRegionPolicy Policy;
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.ParentClass"), EAssetDocumentRegionKind::Object, {TEXT("ParentClass")}, Policy))
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
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.WidgetTree"), EAssetDocumentRegionKind::Object, {TEXT("WidgetTree.RootWidget"), TEXT("WidgetTree.NamedSlotBindings")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Bindings"), EAssetDocumentRegionKind::Array, {TEXT("Bindings")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Animations"), EAssetDocumentRegionKind::Timeline, {TEXT("Animations")}, Policy, TEXT("WidgetBlueprintAnimations")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.UbergraphPages"), EAssetDocumentRegionKind::Graph, {TEXT("UbergraphPages")}, Policy, TEXT("UBlueprintGraph")))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.FunctionGraphs"), EAssetDocumentRegionKind::Graph, {TEXT("FunctionGraphs")}, Policy, TEXT("UBlueprintGraph")))
	{
		MarkDeferredRegionPolicy(Policy);
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.MacroGraphs"), EAssetDocumentRegionKind::Graph, {TEXT("MacroGraphs")}, Policy, TEXT("UBlueprintGraph")))
	{
		MarkDeferredRegionPolicy(Policy);
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Palette"), EAssetDocumentRegionKind::Object, {TEXT("PaletteCategory")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.EditorOptions"), EAssetDocumentRegionKind::Object, {TEXT("bCanCallInitializedWithoutPlayerContext")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.WidgetVariableGuids"), EAssetDocumentRegionKind::Object, {TEXT("WidgetVariableNameToGuidMap")}, Policy, TEXT("WidgetBlueprintWidgetVariableGuids")))
	{
		Policies.Add(Policy);
	}

	return Policies;
}
