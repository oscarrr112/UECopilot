// Copyright ProjectRPG. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetDocumentService.h"
#include "Profiles/AnimBlueprintAssetDocumentProfile.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"

namespace
{
bool HasBodyKey(const TArray<FName>& BodyKeys, const TCHAR* Name)
{
	return BodyKeys.Contains(FName(Name));
}

TSharedRef<FJsonValue> MakeEmptyBodyValue()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithNullRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(RegionName, MakeShared<FJsonValueNull>());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithEmptyArrayRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(RegionName, TArray<TSharedPtr<FJsonValue>>());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithEmptyObjectRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(RegionName, MakeShared<FJsonObject>());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithNonEmptyDeferredRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> AuthoredNode = MakeShared<FJsonObject>();
	AuthoredNode->SetStringField(TEXT("Node"), TEXT("Bad"));
	TArray<TSharedPtr<FJsonValue>> RegionEntries;
	RegionEntries.Add(MakeShared<FJsonValueObject>(AuthoredNode));
	Body->SetArrayField(RegionName, MoveTemp(RegionEntries));
	return MakeShared<FJsonValueObject>(Body);
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

TArray<TSharedPtr<FJsonValue>> MakeLegacyAnimGraphArray()
{
	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Name"), TEXT("AnimGraph"));
	Graph->SetArrayField(TEXT("Nodes"), {});
	return {MakeShared<FJsonValueObject>(Graph)};
}

TSharedRef<FJsonValue> MakeBodyWithCanonicalAnimGraph()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("AnimGraph"), MakeCanonicalAnimGraphObject());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithLegacyAnimGraphArray()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("AnimGraph"), MakeLegacyAnimGraphArray());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithUnsupportedAnimGraphNode()
{
	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), TEXT("IdlePlayer"));
	Node->SetStringField(TEXT("Kind"), TEXT("SequencePlayer"));
	Node->SetStringField(TEXT("Class"), TEXT("/Script/AnimGraph.AnimGraphNode_DoesNotExist"));

	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Id"), TEXT("AnimGraph"));
	Graph->SetStringField(TEXT("Kind"), TEXT("AnimGraph"));
	Graph->SetField(TEXT("Owner"), MakeShared<FJsonValueNull>());
	Graph->SetArrayField(TEXT("Nodes"), {MakeShared<FJsonValueObject>(Node)});
	Graph->SetArrayField(TEXT("Links"), {});
	Graph->SetArrayField(TEXT("Subgraphs"), {});

	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), {MakeShared<FJsonValueObject>(Graph)});

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("AnimGraph"), Region);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithAnimGraphSubgraph()
{
	TSharedRef<FJsonObject> Region = MakeCanonicalAnimGraphObject();
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	Region->TryGetArrayField(TEXT("Graphs"), Graphs);
	TSharedPtr<FJsonObject> RootGraph = Graphs && Graphs->Num() == 1 && (*Graphs)[0].IsValid()
		? (*Graphs)[0]->AsObject()
		: nullptr;

	TSharedRef<FJsonObject> Subgraph = MakeShared<FJsonObject>();
	Subgraph->SetStringField(TEXT("Id"), TEXT("NestedPose"));
	Subgraph->SetStringField(TEXT("Kind"), TEXT("StatePose"));
	Subgraph->SetField(TEXT("Owner"), MakeShared<FJsonValueNull>());
	Subgraph->SetArrayField(TEXT("Nodes"), {});
	Subgraph->SetArrayField(TEXT("Links"), {});
	Subgraph->SetArrayField(TEXT("Subgraphs"), {});
	if (RootGraph.IsValid())
	{
		RootGraph->SetArrayField(TEXT("Subgraphs"), {MakeShared<FJsonValueObject>(Subgraph)});
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("AnimGraph"), Region);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedPtr<FJsonObject> MakeStateMachineState(const TCHAR* Id)
{
	TSharedPtr<FJsonObject> State = MakeShared<FJsonObject>();
	State->SetStringField(TEXT("Id"), Id);
	return State;
}

TSharedPtr<FJsonObject> MakeStateMachineTransition(const TCHAR* Id, const TCHAR* From, const TCHAR* To)
{
	TSharedPtr<FJsonObject> Transition = MakeShared<FJsonObject>();
	Transition->SetStringField(TEXT("Id"), Id);
	Transition->SetStringField(TEXT("From"), From);
	Transition->SetStringField(TEXT("To"), To);
	Transition->SetStringField(TEXT("Rule"), Id);
	return Transition;
}

TSharedPtr<FJsonObject> MakeStateMachine(
	const TCHAR* Name,
	std::initializer_list<TSharedPtr<FJsonObject>> States,
	std::initializer_list<TSharedPtr<FJsonObject>> Transitions)
{
	TSharedPtr<FJsonObject> Machine = MakeShared<FJsonObject>();
	Machine->SetStringField(TEXT("Name"), Name);
	Machine->SetStringField(TEXT("EntryState"), States.size() > 0 ? (*States.begin())->GetStringField(TEXT("Id")) : FString());

	TArray<TSharedPtr<FJsonValue>> StateValues;
	for (const TSharedPtr<FJsonObject>& State : States)
	{
		StateValues.Add(MakeShared<FJsonValueObject>(State.ToSharedRef()));
	}
	Machine->SetArrayField(TEXT("States"), StateValues);

	TArray<TSharedPtr<FJsonValue>> TransitionValues;
	for (const TSharedPtr<FJsonObject>& Transition : Transitions)
	{
		TransitionValues.Add(MakeShared<FJsonValueObject>(Transition.ToSharedRef()));
	}
	Machine->SetArrayField(TEXT("Transitions"), TransitionValues);
	return Machine;
}

TArray<TSharedPtr<FJsonValue>> MakeStateMachineArray(std::initializer_list<TSharedPtr<FJsonObject>> Machines)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedPtr<FJsonObject>& Machine : Machines)
	{
		Values.Add(MakeShared<FJsonValueObject>(Machine.ToSharedRef()));
	}
	return Values;
}

TSharedPtr<FJsonObject> MakeTransitionGraph(const TCHAR* StateMachine, const TCHAR* Transition, bool bWithUnsupportedNode = false)
{
	TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("StateMachine"), StateMachine);
	Graph->SetStringField(TEXT("Transition"), Transition);

	TArray<TSharedPtr<FJsonValue>> Nodes;
	if (bWithUnsupportedNode)
	{
		TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("Id"), TEXT("CanEnter"));
		Node->SetStringField(TEXT("Kind"), TEXT("BoolLiteral"));
		Nodes.Add(MakeShared<FJsonValueObject>(Node));
	}
	Graph->SetArrayField(TEXT("Nodes"), Nodes);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetField(TEXT("Node"), MakeShared<FJsonValueNull>());
	Result->SetStringField(TEXT("Pin"), TEXT("CanEnterTransition"));
	Graph->SetObjectField(TEXT("Result"), Result);
	return Graph;
}

TArray<TSharedPtr<FJsonValue>> MakeTransitionGraphArray(std::initializer_list<TSharedPtr<FJsonObject>> Graphs)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedPtr<FJsonObject>& Graph : Graphs)
	{
		Values.Add(MakeShared<FJsonValueObject>(Graph.ToSharedRef()));
	}
	return Values;
}

TSharedRef<FJsonValue> MakeBodyWithStateMachines(std::initializer_list<TSharedPtr<FJsonObject>> Machines)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("StateMachines"), MakeStateMachineArray(Machines));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithTransitionGraphs(std::initializer_list<TSharedPtr<FJsonObject>> Graphs)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("TransitionGraphs"), MakeTransitionGraphArray(Graphs));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithStateMachineAndTransitionGraph()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(
		TEXT("StateMachines"),
		MakeStateMachineArray({
			MakeStateMachine(
				TEXT("Locomotion"),
				{MakeStateMachineState(TEXT("Idle")), MakeStateMachineState(TEXT("Run"))},
				{MakeStateMachineTransition(TEXT("IdleToRun"), TEXT("Idle"), TEXT("Run"))})}));
	Body->SetArrayField(
		TEXT("TransitionGraphs"),
		MakeTransitionGraphArray({MakeTransitionGraph(TEXT("Locomotion"), TEXT("IdleToRun"))}));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedPtr<FJsonObject> MakeParentAssetOverride(const FString& ParentNodeGuid, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Override = MakeShared<FJsonObject>();
	Override->SetStringField(TEXT("ParentNodeGuid"), ParentNodeGuid);
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), AssetPath);
	Override->SetObjectField(TEXT("NewAsset"), AssetRef);
	return Override;
}

TArray<TSharedPtr<FJsonValue>> MakeParentAssetOverrideArray(std::initializer_list<TSharedPtr<FJsonObject>> Overrides)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedPtr<FJsonObject>& Override : Overrides)
	{
		Values.Add(MakeShared<FJsonValueObject>(Override.ToSharedRef()));
	}
	return Values;
}

TSharedRef<FJsonValue> MakeBodyWithParentAssetOverrides(std::initializer_list<TSharedPtr<FJsonObject>> Overrides)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("ParentAssetOverrides"), MakeParentAssetOverrideArray(Overrides));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithNonEmptyDeferredObjectRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> RegionObject = MakeShared<FJsonObject>();
	RegionObject->SetStringField(TEXT("Node"), TEXT("Bad"));
	Body->SetObjectField(RegionName, RegionObject);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithScalarDeferredRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(RegionName, TEXT("unsupported"));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithUnknownKey(const TCHAR* UnknownKey = TEXT("UnexpectedGraph"))
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(UnknownKey, TEXT("unsupported"));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonObject> MakeClassRef(const FString& ClassPath)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), ClassPath);
	return ClassRef;
}

TSharedRef<FJsonObject> MakeAssetRef(const FString& AssetPath)
{
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), AssetPath);
	return AssetRef;
}

TSharedRef<FJsonValue> MakeBodyWithMissingParentClass()
{
	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), ParentClass);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithParentClass(const FString& ClassPath)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeClassRef(ClassPath));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithTemplateAndTargetSkeleton()
{
	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetBoolField(TEXT("bIsTemplate"), true);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Template"), Template);
	Body->SetObjectField(
		TEXT("TargetSkeleton"),
		MakeAssetRef(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton")));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithTargetSkeleton(
	const FString& TargetSkeletonPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton"))
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("TargetSkeleton"), MakeAssetRef(TargetSkeletonPath));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithNonTemplateMissingTargetSkeleton()
{
	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetBoolField(TEXT("bIsTemplate"), false);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Template"), Template);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithTargetSkeletonAndPreviewApplicationMethod(const FString& Method)
{
	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), Method);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(
		TEXT("TargetSkeleton"),
		MakeAssetRef(TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton")));
	Body->SetObjectField(TEXT("Preview"), Preview);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithSparseDefaultCoreObjects()
{
	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetBoolField(TEXT("bIsTemplate"), false);

	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), TEXT("LinkedLayers"));

	TSharedRef<FJsonObject> Optimization = MakeShared<FJsonObject>();
	Optimization->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), true);
	Optimization->SetBoolField(TEXT("bWarnAboutBlueprintUsage"), false);
	Optimization->SetBoolField(TEXT("bEnableLinkedAnimLayerInstanceSharing"), false);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Template"), Template);
	Body->SetObjectField(TEXT("Preview"), Preview);
	Body->SetObjectField(TEXT("Optimization"), Optimization);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithSparseChangedPreview()
{
	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), TEXT("LinkedAnimGraph"));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Preview"), Preview);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithMismatchedPreviewSkeleton()
{
	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetObjectField(
		TEXT("PreviewSkeletalMesh"),
		MakeAssetRef(TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh")));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(
		TEXT("TargetSkeleton"),
		MakeAssetRef(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton")));
	Body->SetObjectField(TEXT("Preview"), Preview);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithPreviewApplicationMethod(const FString& Method)
{
	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), Method);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Preview"), Preview);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithUnknownOptimizationField()
{
	TSharedRef<FJsonObject> Optimization = MakeShared<FJsonObject>();
	Optimization->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), true);
	Optimization->SetBoolField(TEXT("bUnknownOptimizationFlag"), true);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Optimization"), Optimization);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonObject> MakeAnimBlueprintApplyDocument(
	const FString& Target,
	const FString& ParentClassPath = TEXT("/Script/Engine.AnimInstance"),
	const FString& TargetSkeletonPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton"),
	const FString& PreviewMeshPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh"),
	bool bIsTemplate = false)
{
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimBlueprint"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeClassRef(ParentClassPath));
	if (bIsTemplate)
	{
		Body->SetField(TEXT("TargetSkeleton"), MakeShared<FJsonValueNull>());
	}
	else
	{
		Body->SetObjectField(TEXT("TargetSkeleton"), MakeAssetRef(TargetSkeletonPath));
	}

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetBoolField(TEXT("bIsTemplate"), bIsTemplate);
	Body->SetObjectField(TEXT("Template"), Template);

	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetObjectField(TEXT("PreviewSkeletalMesh"), MakeAssetRef(PreviewMeshPath));
	Preview->SetField(TEXT("PreviewAnimationBlueprint"), MakeShared<FJsonValueNull>());
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), TEXT("LinkedLayers"));
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintTag"), TEXT(""));
	Body->SetObjectField(TEXT("Preview"), Preview);

	TSharedRef<FJsonObject> Optimization = MakeShared<FJsonObject>();
	Optimization->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), true);
	Optimization->SetBoolField(TEXT("bWarnAboutBlueprintUsage"), false);
	Optimization->SetBoolField(TEXT("bEnableLinkedAnimLayerInstanceSharing"), false);
	Body->SetObjectField(TEXT("Optimization"), Optimization);

	Body->SetArrayField(TEXT("SyncGroups"), {});
	Body->SetArrayField(TEXT("ImplementedInterfaces"), {});
	Body->SetArrayField(TEXT("Variables"), {});
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("UbergraphPages"), {});
	Body->SetObjectField(TEXT("AnimGraph"), MakeCanonicalAnimGraphObject());
	Body->SetArrayField(TEXT("StateMachines"), {});
	Body->SetArrayField(TEXT("TransitionGraphs"), {});
	Body->SetArrayField(TEXT("AnimLayers"), {});
	Body->SetArrayField(TEXT("ParentAssetOverrides"), {});
	Document->SetObjectField(TEXT("Body"), Body);
	return Document;
}

TArray<TSharedPtr<FJsonValue>> MakeSyncGroupArray(std::initializer_list<const TCHAR*> Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TCHAR* Name : Names)
	{
		TSharedRef<FJsonObject> Group = MakeShared<FJsonObject>();
		Group->SetStringField(TEXT("Name"), Name);
		Values.Add(MakeShared<FJsonValueObject>(Group));
	}
	return Values;
}

void SetSyncGroups(const TSharedRef<FJsonObject>& Document, std::initializer_list<const TCHAR*> Names)
{
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	Body->SetArrayField(TEXT("SyncGroups"), MakeSyncGroupArray(Names));
}

TSharedPtr<FJsonObject> MakeFloatVariable(const TCHAR* Name, const TCHAR* DefaultValue)
{
	TSharedPtr<FJsonObject> Variable = MakeShared<FJsonObject>();
	Variable->SetStringField(TEXT("Name"), Name);
	TSharedPtr<FJsonObject> Type = MakeShared<FJsonObject>();
	Type->SetStringField(TEXT("PinCategory"), TEXT("real"));
	Type->SetStringField(TEXT("PinSubCategory"), TEXT("float"));
	Variable->SetObjectField(TEXT("Type"), Type);
	Variable->SetStringField(TEXT("DefaultValue"), DefaultValue);
	return Variable;
}

TArray<TSharedPtr<FJsonValue>> MakeVariableArray(std::initializer_list<TSharedPtr<FJsonObject>> Variables)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedPtr<FJsonObject>& Variable : Variables)
	{
		Values.Add(MakeShared<FJsonValueObject>(Variable.ToSharedRef()));
	}
	return Values;
}

TSharedPtr<FJsonObject> MakeImplementedInterface(const FString& InterfacePath)
{
	TSharedPtr<FJsonObject> InterfaceEntry = MakeShared<FJsonObject>();
	InterfaceEntry->SetObjectField(TEXT("Interface"), MakeClassRef(InterfacePath));
	return InterfaceEntry;
}

void SetImplementedInterfaces(const TSharedRef<FJsonObject>& Document, std::initializer_list<TSharedPtr<FJsonObject>> Interfaces)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedPtr<FJsonObject>& Interface : Interfaces)
	{
		Values.Add(MakeShared<FJsonValueObject>(Interface.ToSharedRef()));
	}
	Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("ImplementedInterfaces"), Values);
}

bool HasBlueprintVariable(const UBlueprint* Blueprint, FName Name)
{
	return Blueprint && Blueprint->NewVariables.ContainsByPredicate([Name](const FBPVariableDescription& Variable)
	{
		return Variable.VarName == Name;
	});
}

TSharedRef<FJsonValue> MakeBodyWithSyncGroups(std::initializer_list<const TCHAR*> Names)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("SyncGroups"), MakeSyncGroupArray(Names));
	return MakeShared<FJsonValueObject>(Body);
}

bool HasDiagnostic(const FAssetDocumentCapabilityResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

bool HasDiagnostic(const FAssetDocumentResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

bool HasDiffPath(const TArray<TSharedPtr<FJsonValue>>& DiffEntries, const FString& ExpectedPath)
{
	return DiffEntries.ContainsByPredicate([&ExpectedPath](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		return Entry.IsValid() && Entry->TryGetStringField(TEXT("path"), Path) && Path == ExpectedPath;
	});
}

bool HasDiffNullField(const TArray<TSharedPtr<FJsonValue>>& DiffEntries, const FString& ExpectedPath, const FString& FieldName)
{
	return DiffEntries.ContainsByPredicate([&ExpectedPath, &FieldName](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		const TSharedPtr<FJsonValue> FieldValue = Entry.IsValid() ? Entry->TryGetField(FieldName) : nullptr;
		return Entry.IsValid()
			&& Entry->TryGetStringField(TEXT("path"), Path)
			&& Path == ExpectedPath
			&& FieldValue.IsValid()
			&& FieldValue->Type == EJson::Null;
	});
}

bool HasNumericSyncGroupDiffPath(const TArray<TSharedPtr<FJsonValue>>& DiffEntries)
{
	return DiffEntries.ContainsByPredicate([](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		return Entry.IsValid()
			&& Entry->TryGetStringField(TEXT("path"), Path)
			&& Path.StartsWith(TEXT("/Body/SyncGroups/"))
			&& Path.Len() > FString(TEXT("/Body/SyncGroups/")).Len()
			&& FChar::IsDigit(Path[FString(TEXT("/Body/SyncGroups/")).Len()]);
	});
}

bool HasArrayFieldCount(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, int32 ExpectedCount)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	return Object.IsValid() && Object->TryGetArrayField(FieldName, Values) && Values && Values->Num() == ExpectedCount;
}

bool HasCanonicalAnimGraphObject(const TSharedPtr<FJsonObject>& Body)
{
	const TSharedPtr<FJsonObject>* AnimGraph = nullptr;
	if (!Body.IsValid() || !Body->TryGetObjectField(TEXT("AnimGraph"), AnimGraph) || !AnimGraph || !AnimGraph->IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!(*AnimGraph)->TryGetArrayField(TEXT("Graphs"), Graphs) || !Graphs || Graphs->Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> RootGraph = (*Graphs)[0].IsValid() ? (*Graphs)[0]->AsObject() : nullptr;
	FString Id;
	FString Kind;
	return RootGraph.IsValid()
		&& RootGraph->TryGetStringField(TEXT("Id"), Id)
		&& RootGraph->TryGetStringField(TEXT("Kind"), Kind)
		&& Id == TEXT("AnimGraph")
		&& Kind == TEXT("AnimGraph");
}

const FAssetDocumentRegionPolicy* FindPolicy(const TArray<FAssetDocumentRegionPolicy>& Policies, const TCHAR* RegionId)
{
	return Policies.FindByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
	{
		return Policy.RegionId == FName(RegionId);
	});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintProfileShapeTest,
	"AssetFactory.AssetDocument.AnimBlueprint.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintProfileShapeTest::RunTest(const FString&)
{
	const FAnimBlueprintAssetDocumentProfile Profile;
	TestEqual(TEXT("Exact class is UAnimBlueprint"), Profile.GetExactClass(), UAnimBlueprint::StaticClass());

	FAssetDocumentTemplateContext TemplateContext;
	TemplateContext.Target = TEXT("/Game/AssetDocumentSmoke/ABP_AssetDocumentSmoke");
	const TSharedRef<FJsonObject> Template = Profile.CreateTemplate(TemplateContext);
	TestEqual(TEXT("Template class is AnimBlueprint"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/Engine.AnimBlueprint")));

	const TSharedPtr<FJsonObject> Body = Template->GetObjectField(TEXT("Body"));
	TestTrue(TEXT("Template includes Body object"), Body.IsValid());
	if (Body.IsValid())
	{
		const TArray<FName> ExpectedTemplateBodyKeys = FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys();
		TestEqual(TEXT("Template Body only includes canonical keys"), Body->Values.Num(), ExpectedTemplateBodyKeys.Num());
		for (const FName& ExpectedKey : ExpectedTemplateBodyKeys)
		{
			TestTrue(
				FString::Printf(TEXT("Template includes Body.%s"), *ExpectedKey.ToString()),
				Body->HasField(ExpectedKey.ToString()));
		}
	}

	const TArray<FName> BodyKeys = Profile.GetBodyKeys();
	TestTrue(TEXT("Body keys include ParentClass"), HasBodyKey(BodyKeys, TEXT("ParentClass")));
	TestTrue(TEXT("Body keys include TargetSkeleton"), HasBodyKey(BodyKeys, TEXT("TargetSkeleton")));
	TestTrue(TEXT("Body keys include Template"), HasBodyKey(BodyKeys, TEXT("Template")));
	TestTrue(TEXT("Body keys include Preview"), HasBodyKey(BodyKeys, TEXT("Preview")));
	TestTrue(TEXT("Body keys include Optimization"), HasBodyKey(BodyKeys, TEXT("Optimization")));
	TestTrue(TEXT("Body keys include SyncGroups"), HasBodyKey(BodyKeys, TEXT("SyncGroups")));
	TestTrue(TEXT("Body keys include ImplementedInterfaces"), HasBodyKey(BodyKeys, TEXT("ImplementedInterfaces")));
	TestTrue(TEXT("Body keys include Variables"), HasBodyKey(BodyKeys, TEXT("Variables")));
	TestTrue(TEXT("Body keys include ClassDefaults"), HasBodyKey(BodyKeys, TEXT("ClassDefaults")));
	TestTrue(TEXT("Body keys include UbergraphPages"), HasBodyKey(BodyKeys, TEXT("UbergraphPages")));
	TestTrue(TEXT("Body keys include AnimGraph"), HasBodyKey(BodyKeys, TEXT("AnimGraph")));
	TestTrue(TEXT("Body keys include StateMachines"), HasBodyKey(BodyKeys, TEXT("StateMachines")));
	TestTrue(TEXT("Body keys include TransitionGraphs"), HasBodyKey(BodyKeys, TEXT("TransitionGraphs")));
	TestTrue(TEXT("Body keys include AnimLayers"), HasBodyKey(BodyKeys, TEXT("AnimLayers")));
	TestTrue(TEXT("Body keys include ParentAssetOverrides"), HasBodyKey(BodyKeys, TEXT("ParentAssetOverrides")));
	TestTrue(TEXT("Body root resolves adapter"), Profile.ResolveBodyAdapter(TEXT("Body")) != nullptr);
	TestTrue(TEXT("ParentClass resolves adapter"), Profile.ResolveBodyAdapter(TEXT("ParentClass")) != nullptr);

	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	TestNotNull(TEXT("Policy includes Body.ParentClass"), FindPolicy(Policies, TEXT("Body.ParentClass")));
	TestNotNull(TEXT("Policy includes Body.TargetSkeleton"), FindPolicy(Policies, TEXT("Body.TargetSkeleton")));
	TestNotNull(TEXT("Policy includes Body.Template"), FindPolicy(Policies, TEXT("Body.Template")));
	TestNotNull(TEXT("Policy includes Body.Preview"), FindPolicy(Policies, TEXT("Body.Preview")));
	TestNotNull(TEXT("Policy includes Body.Optimization"), FindPolicy(Policies, TEXT("Body.Optimization")));
	TestNotNull(TEXT("Policy includes Body.SyncGroups"), FindPolicy(Policies, TEXT("Body.SyncGroups")));
	TestNotNull(TEXT("Policy includes Body.ImplementedInterfaces"), FindPolicy(Policies, TEXT("Body.ImplementedInterfaces")));
	TestNotNull(TEXT("Policy includes Body.Variables"), FindPolicy(Policies, TEXT("Body.Variables")));
	TestNotNull(TEXT("Policy includes Body.ClassDefaults"), FindPolicy(Policies, TEXT("Body.ClassDefaults")));
	TestNotNull(TEXT("Policy includes Body.UbergraphPages"), FindPolicy(Policies, TEXT("Body.UbergraphPages")));
	const FAssetDocumentRegionPolicy* AnimGraphPolicy = FindPolicy(Policies, TEXT("Body.AnimGraph"));
	const FAssetDocumentRegionPolicy* StateMachinesPolicy = FindPolicy(Policies, TEXT("Body.StateMachines"));
	const FAssetDocumentRegionPolicy* TransitionGraphsPolicy = FindPolicy(Policies, TEXT("Body.TransitionGraphs"));
	const FAssetDocumentRegionPolicy* AnimLayersPolicy = FindPolicy(Policies, TEXT("Body.AnimLayers"));
	const FAssetDocumentRegionPolicy* ParentAssetOverridesPolicy = FindPolicy(Policies, TEXT("Body.ParentAssetOverrides"));
	TestNotNull(TEXT("Policy includes Body.AnimGraph"), AnimGraphPolicy);
	TestNotNull(TEXT("Policy includes Body.StateMachines"), StateMachinesPolicy);
	TestNotNull(TEXT("Policy includes Body.TransitionGraphs"), TransitionGraphsPolicy);
	TestNotNull(TEXT("Policy includes Body.AnimLayers"), AnimLayersPolicy);
	TestNotNull(TEXT("Policy includes Body.ParentAssetOverrides"), ParentAssetOverridesPolicy);
	if (AnimGraphPolicy)
	{
		TestEqual(TEXT("AnimGraph policy is no longer deferred/null-gated"), AnimGraphPolicy->ExplicitDeleteValues.Num(), 0);
	}
	if (StateMachinesPolicy)
	{
		TestEqual(TEXT("StateMachines policy is no longer deferred/null-gated"), StateMachinesPolicy->ExplicitDeleteValues.Num(), 0);
	}
	if (TransitionGraphsPolicy)
	{
		TestEqual(TEXT("TransitionGraphs policy is no longer deferred/null-gated"), TransitionGraphsPolicy->ExplicitDeleteValues.Num(), 0);
	}
	if (AnimLayersPolicy)
	{
		TestTrue(TEXT("AnimLayers policy is deferred/null-gated"), AnimLayersPolicy->ExplicitDeleteValues.Num() > 0);
	}
	if (ParentAssetOverridesPolicy)
	{
		TestEqual(TEXT("ParentAssetOverrides policy is no longer deferred/null-gated"), ParentAssetOverridesPolicy->ExplicitDeleteValues.Num(), 0);
	}

	const IAssetDocumentCapability* BodyAdapter = Profile.ResolveBodyAdapter(TEXT("Body"));
	TestNotNull(TEXT("Body adapter resolves for validation"), BodyAdapter);
	if (BodyAdapter)
	{
		FAssetDocumentCapabilityContext Context;
		Context.AssetClass = UAnimBlueprint::StaticClass();
		TestTrue(TEXT("Empty Body validates"), BodyAdapter->Validate(Context, MakeEmptyBodyValue()).bSuccess);

		for (const TCHAR* DeferredKey : {
			TEXT("AnimLayers"),
		})
		{
			const FAssetDocumentCapabilityResult Result = BodyAdapter->Validate(Context, MakeBodyWithNonEmptyDeferredRegion(DeferredKey));
			TestFalse(FString::Printf(TEXT("Body.%s rejects non-empty deferred content"), DeferredKey), Result.bSuccess);
			TestTrue(FString::Printf(TEXT("Body.%s reports a diagnostic"), DeferredKey), Result.Diagnostics.Num() > 0);
			if (Result.Diagnostics.Num() > 0)
			{
				TestEqual(
					FString::Printf(TEXT("Body.%s diagnostic path"), DeferredKey),
					Result.Diagnostics[0].Path,
					FString::Printf(TEXT("/Body/%s"), DeferredKey));
				TestEqual(
					FString::Printf(TEXT("Body.%s diagnostic code"), DeferredKey),
					Result.Diagnostics[0].Code,
					FString(TEXT("UnsupportedAnimBlueprintRegion")));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintDeferredGraphGatesTest,
	"AssetFactory.AssetDocument.AnimBlueprint.DeferredGraphGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintDeferredGraphGatesTest::RunTest(const FString&)
{
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	for (const TCHAR* DeferredKey : {
		TEXT("AnimLayers"),
	})
	{
		TestTrue(
			FString::Printf(TEXT("Body.%s accepts null while deferred"), DeferredKey),
			Capability.Validate(Context, MakeBodyWithNullRegion(DeferredKey)).bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s accepts empty array while deferred"), DeferredKey),
			Capability.Validate(Context, MakeBodyWithEmptyArrayRegion(DeferredKey)).bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s accepts empty object while deferred"), DeferredKey),
			Capability.Validate(Context, MakeBodyWithEmptyObjectRegion(DeferredKey)).bSuccess);

		const FAssetDocumentCapabilityResult NonEmptyResult =
			Capability.Validate(Context, MakeBodyWithNonEmptyDeferredRegion(DeferredKey));
		const FString ExpectedPath = FString::Printf(TEXT("/Body/%s"), DeferredKey);
		TestFalse(
			FString::Printf(TEXT("Body.%s rejects non-empty authored value while deferred"), DeferredKey),
			NonEmptyResult.bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s reports UnsupportedAnimBlueprintRegion at exact path"), DeferredKey),
			HasDiagnostic(NonEmptyResult, ExpectedPath, TEXT("UnsupportedAnimBlueprintRegion")));

		const FAssetDocumentCapabilityResult NonEmptyObjectResult =
			Capability.Validate(Context, MakeBodyWithNonEmptyDeferredObjectRegion(DeferredKey));
		TestFalse(
			FString::Printf(TEXT("Body.%s rejects non-empty object while deferred"), DeferredKey),
			NonEmptyObjectResult.bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s reports UnsupportedAnimBlueprintRegion for non-empty object"), DeferredKey),
			HasDiagnostic(NonEmptyObjectResult, ExpectedPath, TEXT("UnsupportedAnimBlueprintRegion")));

		const FAssetDocumentCapabilityResult ScalarResult =
			Capability.Validate(Context, MakeBodyWithScalarDeferredRegion(DeferredKey));
		TestFalse(
			FString::Printf(TEXT("Body.%s rejects scalar value while deferred"), DeferredKey),
			ScalarResult.bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s reports UnsupportedAnimBlueprintRegion for scalar"), DeferredKey),
			HasDiagnostic(ScalarResult, ExpectedPath, TEXT("UnsupportedAnimBlueprintRegion")));
	}

	const FAssetDocumentCapabilityResult UnknownKeyResult = Capability.Validate(Context, MakeBodyWithUnknownKey());
	TestFalse(TEXT("Unknown Body key rejects"), UnknownKeyResult.bSuccess);
	TestTrue(
		TEXT("Unknown Body key reports UnknownBodyKey"),
		HasDiagnostic(UnknownKeyResult, TEXT("/Body/UnexpectedGraph"), TEXT("UnknownBodyKey")));
	for (const TCHAR* UnknownBlueprintGraphKey : {TEXT("FunctionGraphs"), TEXT("MacroGraphs")})
	{
		const FAssetDocumentCapabilityResult UnknownBlueprintGraphResult =
			Capability.Validate(Context, MakeBodyWithUnknownKey(UnknownBlueprintGraphKey));
		TestFalse(
			FString::Printf(TEXT("Body.%s remains outside the ABP profile"), UnknownBlueprintGraphKey),
			UnknownBlueprintGraphResult.bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s reports UnknownBodyKey"), UnknownBlueprintGraphKey),
			HasDiagnostic(
				UnknownBlueprintGraphResult,
				FString::Printf(TEXT("/Body/%s"), UnknownBlueprintGraphKey),
				TEXT("UnknownBodyKey")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintAnimGraphTest,
	"AssetFactory.AssetDocument.AnimBlueprint.AnimGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintAnimGraphTest::RunTest(const FString&)
{
	FAssetDocumentCapabilityContext ValidationContext;
	ValidationContext.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	TestTrue(TEXT("Canonical recursive AnimGraph validates"), Capability.Validate(ValidationContext, MakeBodyWithCanonicalAnimGraph()).bSuccess);

	const FAssetDocumentCapabilityResult LegacyArrayResult =
		Capability.Validate(ValidationContext, MakeBodyWithLegacyAnimGraphArray());
	TestFalse(TEXT("Legacy AnimGraph array rejects under recursive schema"), LegacyArrayResult.bSuccess);
	TestTrue(
		TEXT("Legacy array diagnostic uses AnimGraph region path"),
		HasDiagnostic(LegacyArrayResult, TEXT("/Body/AnimGraph"), TEXT("InvalidAnimGraphRegionType")));

	const FAssetDocumentCapabilityResult UnsupportedNodeResult =
		Capability.Validate(ValidationContext, MakeBodyWithUnsupportedAnimGraphNode());
	TestFalse(TEXT("Unsupported AnimGraph node rejects"), UnsupportedNodeResult.bSuccess);
	TestTrue(
		TEXT("Unsupported node diagnostic uses semantic AnimGraph path"),
		HasDiagnostic(
			UnsupportedNodeResult,
			TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Class"),
			TEXT("UnspawnableGraphNodeClass")));

	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_AnimGraph_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
	FAssetDocumentService Service;

	const FString BadTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_AnimGraph_Bad_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	TSharedRef<FJsonObject> BadDocument = MakeAnimBlueprintApplyDocument(BadTarget);
	BadDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("AnimGraph"),
		MakeBodyWithUnsupportedAnimGraphNode()->AsObject()->GetObjectField(TEXT("AnimGraph")));
	FAssetDocumentApplyRequest BadRequest;
	BadRequest.Document = BadDocument;
	BadRequest.bSaveAsset = false;
	const FAssetDocumentResult BadApplyResult = Service.Apply(BadRequest);
	TestFalse(TEXT("Unsupported AnimGraph node apply rejects before mutation"), BadApplyResult.IsSuccess());
	TestTrue(
		TEXT("Unsupported apply diagnostic keeps Body.AnimGraph semantic path"),
		HasDiagnostic(
			BadApplyResult,
			TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Class"),
			TEXT("UnspawnableGraphNodeClass")));

	TSharedRef<FJsonObject> Document = MakeAnimBlueprintApplyDocument(Target);
	Document->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("AnimGraph"), MakeCanonicalAnimGraphObject());

	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(Request);
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimGraph recursive apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Recursive AnimGraph apply succeeds"), ApplyResult.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created AnimBlueprint loads"), AnimBlueprint);
	FAssetDocumentCapabilityContext Context;
	Context.Asset = AnimBlueprint;
	Context.AssetClass = UAnimBlueprint::StaticClass();

	TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
	TestTrue(TEXT("AnimGraph extract succeeds"), ExtractResult.bSuccess);
	TestTrue(TEXT("Extract includes canonical recursive AnimGraph"), HasCanonicalAnimGraphObject(ExtractedBody));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult =
		Capability.Diff(Context, MakeBodyWithCanonicalAnimGraph(), DiffEntries);
	TestTrue(TEXT("AnimGraph diff succeeds"), DiffResult.bSuccess);
	TestTrue(TEXT("AnimGraph diff uses semantic graph path"), HasDiffPath(DiffEntries, TEXT("/Body/AnimGraph/Graphs/AnimGraph")));

	DiffEntries.Reset();
	const FAssetDocumentCapabilityResult SubgraphDiffResult =
		Capability.Diff(Context, MakeBodyWithAnimGraphSubgraph(), DiffEntries);
	TestTrue(TEXT("AnimGraph subgraph diff succeeds"), SubgraphDiffResult.bSuccess);
	TestTrue(
		TEXT("AnimGraph subgraph diff uses semantic subgraph path"),
		HasDiffPath(DiffEntries, TEXT("/Body/AnimGraph/Graphs/AnimGraph/Subgraphs/NestedPose")));
	TestTrue(
		TEXT("AnimGraph subgraph missing current is serialized as null"),
		HasDiffNullField(DiffEntries, TEXT("/Body/AnimGraph/Graphs/AnimGraph/Subgraphs/NestedPose"), TEXT("current")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintStateMachinesTest,
	"AssetFactory.AssetDocument.AnimBlueprint.StateMachines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintStateMachinesTest::RunTest(const FString&)
{
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	TestTrue(
		TEXT("StateMachines accepts empty compatibility value"),
		Capability.Validate(Context, MakeBodyWithEmptyArrayRegion(TEXT("StateMachines"))).bSuccess);

	const FAssetDocumentCapabilityResult AuthoredStateMachineResult =
		Capability.Validate(
			Context,
			MakeBodyWithStateMachines({
				MakeStateMachine(
					TEXT("Locomotion"),
					{MakeStateMachineState(TEXT("Idle")), MakeStateMachineState(TEXT("Run"))},
					{MakeStateMachineTransition(TEXT("IdleToRun"), TEXT("Idle"), TEXT("Run"))})}));
	TestFalse(TEXT("StateMachines rejects non-empty authored identity data until materialization exists"), AuthoredStateMachineResult.bSuccess);
	TestTrue(
		TEXT("StateMachines non-empty diagnostic uses region path"),
		HasDiagnostic(AuthoredStateMachineResult, TEXT("/Body/StateMachines"), TEXT("UnsupportedAnimBlueprintRegion")));

	const FAssetDocumentCapabilityResult DuplicateStateResult =
		Capability.Validate(
			Context,
			MakeBodyWithStateMachines({
				MakeStateMachine(
					TEXT("Locomotion"),
					{MakeStateMachineState(TEXT("Idle")), MakeStateMachineState(TEXT("idle"))},
					{})}));
	TestFalse(TEXT("StateMachines still rejects duplicate authored input through deferred boundary"), DuplicateStateResult.bSuccess);
	TestTrue(
		TEXT("Duplicate state is not silently accepted"),
		HasDiagnostic(DuplicateStateResult, TEXT("/Body/StateMachines"), TEXT("UnsupportedAnimBlueprintRegion")));

	const FAssetDocumentCapabilityResult UnknownEndpointResult =
		Capability.Validate(
			Context,
			MakeBodyWithStateMachines({
				MakeStateMachine(
					TEXT("Locomotion"),
					{MakeStateMachineState(TEXT("Idle"))},
					{MakeStateMachineTransition(TEXT("IdleToRun"), TEXT("Idle"), TEXT("Run"))})}));
	TestFalse(TEXT("StateMachines rejects transitions while materialization is deferred"), UnknownEndpointResult.bSuccess);
	TestTrue(
		TEXT("Unknown endpoint is not silently accepted"),
		HasDiagnostic(UnknownEndpointResult, TEXT("/Body/StateMachines"), TEXT("UnsupportedAnimBlueprintRegion")));

	TestTrue(
		TEXT("TransitionGraphs accepts empty compatibility value"),
		Capability.Validate(Context, MakeBodyWithEmptyArrayRegion(TEXT("TransitionGraphs"))).bSuccess);

	const FAssetDocumentCapabilityResult AuthoredTransitionGraphResult =
		Capability.Validate(Context, MakeBodyWithTransitionGraphs({MakeTransitionGraph(TEXT("Locomotion"), TEXT("IdleToRun"))}));
	TestFalse(TEXT("TransitionGraphs rejects non-empty root-only data until materialization exists"), AuthoredTransitionGraphResult.bSuccess);
	TestTrue(
		TEXT("TransitionGraphs non-empty diagnostic uses region path"),
		HasDiagnostic(AuthoredTransitionGraphResult, TEXT("/Body/TransitionGraphs"), TEXT("UnsupportedAnimBlueprintRegion")));

	const FAssetDocumentCapabilityResult DuplicateTransitionGraphResult =
		Capability.Validate(
			Context,
			MakeBodyWithTransitionGraphs({
				MakeTransitionGraph(TEXT("Locomotion"), TEXT("IdleToRun")),
				MakeTransitionGraph(TEXT("locomotion"), TEXT("idletorun"))}));
	TestFalse(TEXT("TransitionGraphs rejects duplicate authored input through deferred boundary"), DuplicateTransitionGraphResult.bSuccess);
	TestTrue(
		TEXT("Duplicate transition graph is not silently accepted"),
		HasDiagnostic(DuplicateTransitionGraphResult, TEXT("/Body/TransitionGraphs"), TEXT("UnsupportedAnimBlueprintRegion")));

	const FAssetDocumentCapabilityResult UnsupportedRuleNodeResult =
		Capability.Validate(
			Context,
			MakeBodyWithTransitionGraphs({MakeTransitionGraph(TEXT("Locomotion"), TEXT("IdleToRun"), true)}));
	TestFalse(TEXT("TransitionGraphs rejects authored rule nodes while materialization is deferred"), UnsupportedRuleNodeResult.bSuccess);
	TestTrue(
		TEXT("Unsupported transition graph node is not silently accepted"),
		HasDiagnostic(
			UnsupportedRuleNodeResult,
			TEXT("/Body/TransitionGraphs"),
			TEXT("UnsupportedAnimBlueprintRegion")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult =
		Capability.Diff(Context, MakeBodyWithEmptyArrayRegion(TEXT("StateMachines")), DiffEntries);
	TestTrue(TEXT("StateMachines and TransitionGraphs diff succeeds"), DiffResult.bSuccess);
	TestEqual(TEXT("Empty StateMachines diff has no authored entries"), DiffEntries.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintAnimLayersAndParentAssetOverridesTest,
	"AssetFactory.AssetDocument.AnimBlueprint.AnimLayersAndParentAssetOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintAnimLayersAndParentAssetOverridesTest::RunTest(const FString&)
{
	const FString AnimationAssetPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Idle.Tutorial_Idle");
	const FString ParentGuidA = TEXT("01234567-89ab-cdef-0123-456789abcdef");
	const FString ParentGuidB = TEXT("11111111-2222-3333-4444-555555555555");

	FAssetDocumentCapabilityContext ValidationContext;
	ValidationContext.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	const FAssetDocumentCapabilityResult AnimLayersResult =
		Capability.Validate(ValidationContext, MakeBodyWithNonEmptyDeferredRegion(TEXT("AnimLayers")));
	TestFalse(TEXT("AnimLayers remains outside regular AnimBlueprint exact-profile authoring"), AnimLayersResult.bSuccess);
	TestTrue(
		TEXT("AnimLayers boundary reports deferred diagnostic"),
		HasDiagnostic(AnimLayersResult, TEXT("/Body/AnimLayers"), TEXT("UnsupportedAnimBlueprintRegion")));

	TestTrue(
		TEXT("ParentAssetOverrides validates stable guid identity array"),
		Capability.Validate(
			ValidationContext,
			MakeBodyWithParentAssetOverrides({MakeParentAssetOverride(ParentGuidA, AnimationAssetPath)})).bSuccess);

	const FAssetDocumentCapabilityResult DuplicateGuidResult =
		Capability.Validate(
			ValidationContext,
			MakeBodyWithParentAssetOverrides({
				MakeParentAssetOverride(ParentGuidA, AnimationAssetPath),
				MakeParentAssetOverride(ParentGuidA, AnimationAssetPath)}));
	TestFalse(TEXT("ParentAssetOverrides rejects duplicate ParentNodeGuid"), DuplicateGuidResult.bSuccess);
	TestTrue(
		TEXT("Duplicate ParentNodeGuid diagnostic uses authored index"),
		HasDiagnostic(DuplicateGuidResult, TEXT("/Body/ParentAssetOverrides/1/ParentNodeGuid"), TEXT("DuplicateParentAssetOverrideGuid")));

	TSharedPtr<FJsonObject> MissingPathOverride = MakeParentAssetOverride(ParentGuidA, AnimationAssetPath);
	const TSharedPtr<FJsonObject>* MissingPathAssetRef = nullptr;
	if (MissingPathOverride->TryGetObjectField(TEXT("NewAsset"), MissingPathAssetRef) && MissingPathAssetRef && MissingPathAssetRef->IsValid())
	{
		(*MissingPathAssetRef)->RemoveField(TEXT("Path"));
	}
	const FAssetDocumentCapabilityResult MissingAssetPathResult =
		Capability.Validate(ValidationContext, MakeBodyWithParentAssetOverrides({MissingPathOverride}));
	TestFalse(TEXT("ParentAssetOverrides rejects missing animation asset path"), MissingAssetPathResult.bSuccess);
	TestTrue(
		TEXT("Missing asset path diagnostic uses guid identity path"),
		HasDiagnostic(
			MissingAssetPathResult,
			TEXT("/Body/ParentAssetOverrides/01234567-89ab-cdef-0123-456789abcdef/NewAsset/Path"),
			TEXT("MissingParentAssetOverrideAssetPath")));

	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_Overrides_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
	TSharedRef<FJsonObject> Document = MakeAnimBlueprintApplyDocument(Target);
	Document->GetObjectField(TEXT("Body"))->SetArrayField(
		TEXT("ParentAssetOverrides"),
		MakeParentAssetOverrideArray({MakeParentAssetOverride(ParentGuidA, AnimationAssetPath)}));

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(Request);
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("ParentAssetOverrides apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("ParentAssetOverrides apply succeeds"), ApplyResult.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created AnimBlueprint loads"), AnimBlueprint);
	UAnimationAsset* ExpectedAsset = LoadObject<UAnimationAsset>(nullptr, *AnimationAssetPath);
	TestNotNull(TEXT("Expected animation asset loads"), ExpectedAsset);
	if (AnimBlueprint && ExpectedAsset)
	{
		TestEqual(TEXT("One parent asset override applied"), AnimBlueprint->ParentAssetOverrides.Num(), 1);
		if (AnimBlueprint->ParentAssetOverrides.Num() == 1)
		{
			TestEqual(TEXT("ParentNodeGuid is preserved"), AnimBlueprint->ParentAssetOverrides[0].ParentNodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower), ParentGuidA);
			TestEqual(TEXT("NewAsset is applied"), AnimBlueprint->ParentAssetOverrides[0].NewAsset.Get(), ExpectedAsset);
		}
	}

	FAssetDocumentCapabilityContext Context;
	Context.Asset = AnimBlueprint;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
	TestTrue(TEXT("ParentAssetOverrides extract succeeds"), ExtractResult.bSuccess);
	TestTrue(TEXT("Extract includes one parent asset override"), HasArrayFieldCount(ExtractedBody, TEXT("ParentAssetOverrides"), 1));
	const TArray<TSharedPtr<FJsonValue>>* ExtractedOverrides = nullptr;
	if (ExtractedBody->TryGetArrayField(TEXT("ParentAssetOverrides"), ExtractedOverrides) && ExtractedOverrides && ExtractedOverrides->Num() == 1)
	{
		const TSharedPtr<FJsonObject> ExtractedOverride = (*ExtractedOverrides)[0]->AsObject();
		TestTrue(TEXT("Extracted parent override is object"), ExtractedOverride.IsValid());
		if (ExtractedOverride.IsValid())
		{
			FString ExtractedGuid;
			TestTrue(TEXT("Extracted ParentNodeGuid is present"), ExtractedOverride->TryGetStringField(TEXT("ParentNodeGuid"), ExtractedGuid));
			TestEqual(TEXT("Extracted ParentNodeGuid shape is stable"), ExtractedGuid, ParentGuidA);
			const TSharedPtr<FJsonObject>* ExtractedAssetRef = nullptr;
			TestTrue(TEXT("Extracted NewAsset is object"), ExtractedOverride->TryGetObjectField(TEXT("NewAsset"), ExtractedAssetRef));
			if (ExtractedAssetRef && ExtractedAssetRef->IsValid())
			{
				FString ExtractedKind;
				FString ExtractedPath;
				TestTrue(TEXT("Extracted NewAsset.Kind is present"), (*ExtractedAssetRef)->TryGetStringField(TEXT("Kind"), ExtractedKind));
				TestEqual(TEXT("Extracted NewAsset.Kind is AssetRef"), ExtractedKind, FString(TEXT("AssetRef")));
				TestTrue(TEXT("Extracted NewAsset.Path is present"), (*ExtractedAssetRef)->TryGetStringField(TEXT("Path"), ExtractedPath));
				if (ExpectedAsset)
				{
					TestEqual(TEXT("Extracted NewAsset.Path is stable"), ExtractedPath, ExpectedAsset->GetPathName());
				}
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult =
		Capability.Diff(
			Context,
			MakeBodyWithParentAssetOverrides({
				MakeParentAssetOverride(ParentGuidA, AnimationAssetPath),
				MakeParentAssetOverride(ParentGuidB, AnimationAssetPath)}),
			DiffEntries);
	TestTrue(TEXT("ParentAssetOverrides diff succeeds"), DiffResult.bSuccess);
	TestTrue(
		TEXT("ParentAssetOverrides diff uses guid identity path"),
		HasDiffPath(DiffEntries, TEXT("/Body/ParentAssetOverrides/11111111-2222-3333-4444-555555555555")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintCoreObjectRegionsTest,
	"AssetFactory.AssetDocument.AnimBlueprint.CoreObjectRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintCoreObjectRegionsTest::RunTest(const FString&)
{
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	const FAssetDocumentCapabilityResult MissingParentClassResult =
		Capability.Validate(Context, MakeBodyWithMissingParentClass());
	TestFalse(TEXT("ParentClass missing Class rejects"), MissingParentClassResult.bSuccess);
	TestTrue(
		TEXT("ParentClass missing Class reports MissingParentClass"),
		HasDiagnostic(MissingParentClassResult, TEXT("/Body/ParentClass/Class"), TEXT("MissingParentClass")));

	const FAssetDocumentCapabilityResult InvalidParentClassResult =
		Capability.Validate(Context, MakeBodyWithParentClass(TEXT("/Script/Engine.Actor")));
	TestFalse(TEXT("ParentClass must be an AnimInstance child"), InvalidParentClassResult.bSuccess);
	TestTrue(
		TEXT("ParentClass reports InvalidAnimBlueprintParentClass"),
		HasDiagnostic(InvalidParentClassResult, TEXT("/Body/ParentClass/Class"), TEXT("InvalidAnimBlueprintParentClass")));

	const FAssetDocumentCapabilityResult InvalidTemplateSkeletonResult =
		Capability.Validate(Context, MakeBodyWithTemplateAndTargetSkeleton());
	TestFalse(TEXT("Template AnimBlueprint cannot author TargetSkeleton"), InvalidTemplateSkeletonResult.bSuccess);
	TestTrue(
		TEXT("Template skeleton conflict reports InvalidTemplateSkeleton"),
		HasDiagnostic(InvalidTemplateSkeletonResult, TEXT("/Body/TargetSkeleton"), TEXT("InvalidTemplateSkeleton"))
			|| HasDiagnostic(InvalidTemplateSkeletonResult, TEXT("/Body/Template/bIsTemplate"), TEXT("InvalidTemplateSkeleton")));

	const FAssetDocumentCapabilityResult MissingCreateSkeletonResult =
		Capability.Validate(Context, MakeBodyWithNonTemplateMissingTargetSkeleton());
	TestFalse(TEXT("Non-template AnimBlueprint create requires TargetSkeleton"), MissingCreateSkeletonResult.bSuccess);
	TestTrue(
		TEXT("Missing create skeleton diagnostic uses TargetSkeleton path"),
		HasDiagnostic(MissingCreateSkeletonResult, TEXT("/Body/TargetSkeleton"), TEXT("MissingTargetSkeleton")));

	const FAssetDocumentCapabilityResult MismatchedPreviewSkeletonResult =
		Capability.Validate(Context, MakeBodyWithMismatchedPreviewSkeleton());
	TestFalse(TEXT("Preview skeletal mesh skeleton must match TargetSkeleton"), MismatchedPreviewSkeletonResult.bSuccess);
	TestTrue(
		TEXT("Preview skeleton mismatch diagnostic uses PreviewSkeletalMesh path"),
		HasDiagnostic(
			MismatchedPreviewSkeletonResult,
			TEXT("/Body/Preview/PreviewSkeletalMesh"),
			TEXT("MismatchedPreviewSkeletalMeshSkeleton")));

	const FAssetDocumentCapabilityResult InvalidPreviewMethodResult =
		Capability.Validate(Context, MakeBodyWithPreviewApplicationMethod(TEXT("Bogus")));
	TestFalse(TEXT("Preview rejects unknown application method"), InvalidPreviewMethodResult.bSuccess);
	TestTrue(
		TEXT("Preview method reports InvalidPreviewAnimationBlueprintApplicationMethod"),
		HasDiagnostic(
			InvalidPreviewMethodResult,
			TEXT("/Body/Preview/PreviewAnimationBlueprintApplicationMethod"),
			TEXT("InvalidPreviewAnimationBlueprintApplicationMethod")));

	const FAssetDocumentCapabilityResult UnknownOptimizationFieldResult =
		Capability.Validate(Context, MakeBodyWithUnknownOptimizationField());
	TestFalse(TEXT("Optimization rejects unknown object fields"), UnknownOptimizationFieldResult.bSuccess);
	TestTrue(
		TEXT("Optimization unknown field reports schema diagnostic"),
		HasDiagnostic(UnknownOptimizationFieldResult, TEXT("/Body/Optimization/bUnknownOptimizationFlag"), TEXT("UnknownObjectField")));

	const FAssetDocumentCapabilityResult UnknownBodyKeyResult = Capability.Validate(Context, MakeBodyWithUnknownKey());
	TestFalse(TEXT("Dispatcher still rejects unknown Body key"), UnknownBodyKeyResult.bSuccess);
	TestTrue(
		TEXT("Dispatcher preserves UnknownBodyKey code"),
		HasDiagnostic(UnknownBodyKeyResult, TEXT("/Body/UnexpectedGraph"), TEXT("UnknownBodyKey")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult ObjectDiffResult =
		Capability.Diff(Context, MakeBodyWithTargetSkeletonAndPreviewApplicationMethod(TEXT("LinkedAnimGraph")), DiffEntries);
	TestTrue(TEXT("Preview object diff succeeds"), ObjectDiffResult.bSuccess);
	TestTrue(TEXT("Preview object diff reports region path"), HasDiffPath(DiffEntries, TEXT("/Body/Preview")));

	FAssetDocumentCapabilityContext SparseDiffContext;
	SparseDiffContext.Asset = NewObject<UAnimBlueprint>(GetTransientPackage());
	SparseDiffContext.AssetClass = UAnimBlueprint::StaticClass();

	DiffEntries.Reset();
	const FAssetDocumentCapabilityResult SparseNoChangeDiffResult =
		Capability.Diff(SparseDiffContext, MakeBodyWithSparseDefaultCoreObjects(), DiffEntries);
	TestTrue(TEXT("Sparse default object diff succeeds"), SparseNoChangeDiffResult.bSuccess);
	TestEqual(TEXT("Sparse default object diff does not report omitted defaults"), DiffEntries.Num(), 0);

	DiffEntries.Reset();
	const FAssetDocumentCapabilityResult SparseChangedDiffResult =
		Capability.Diff(SparseDiffContext, MakeBodyWithSparseChangedPreview(), DiffEntries);
	TestTrue(TEXT("Sparse changed object diff succeeds"), SparseChangedDiffResult.bSuccess);
	TestTrue(TEXT("Sparse changed object diff reports Preview"), HasDiffPath(DiffEntries, TEXT("/Body/Preview")));

	DiffEntries.Reset();
	const FAssetDocumentCapabilityResult SkeletonDiffResult =
		Capability.Diff(
			Context,
			MakeBodyWithTargetSkeleton(),
			DiffEntries);
	TestTrue(TEXT("TargetSkeleton diff succeeds"), SkeletonDiffResult.bSuccess);
	TestTrue(TEXT("TargetSkeleton diff reports region path"), HasDiffPath(DiffEntries, TEXT("/Body/TargetSkeleton")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintCreateUpdateLifecycleTest,
	"AssetFactory.AssetDocument.AnimBlueprint.CreateUpdateLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintCreateUpdateLifecycleTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_Lifecycle_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
	const FString SkeletonPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton");
	const FString PreviewMeshPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh");

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = MakeAnimBlueprintApplyDocument(Target, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, false);
	Request.bSaveAsset = false;

	const FAssetDocumentResult Result = Service.Apply(Request);
	if (!Result.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint apply failed: %s"), *Result.Message));
	}
	TestTrue(TEXT("AnimBlueprint apply succeeds"), Result.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created asset is UAnimBlueprint"), AnimBlueprint);
	USkeleton* ExpectedSkeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	USkeletalMesh* ExpectedPreviewMesh = LoadObject<USkeletalMesh>(nullptr, *PreviewMeshPath);
	TestNotNull(TEXT("Expected skeleton asset loads"), ExpectedSkeleton);
	TestNotNull(TEXT("Expected preview mesh asset loads"), ExpectedPreviewMesh);
	if (AnimBlueprint)
	{
		TestEqual(TEXT("Parent class is AnimInstance"), AnimBlueprint->ParentClass.Get(), UAnimInstance::StaticClass());
		TestFalse(TEXT("Created AnimBlueprint is not a template"), AnimBlueprint->bIsTemplate);
		TestEqual(TEXT("TargetSkeleton is authored skeleton"), AnimBlueprint->TargetSkeleton.Get(), ExpectedSkeleton);
		TestEqual(TEXT("Preview mesh is authored mesh"), AnimBlueprint->GetPreviewMesh(), ExpectedPreviewMesh);
	}

	FAssetDocumentApplyRequest TemplateUpdateRequest;
	TemplateUpdateRequest.Document = MakeAnimBlueprintApplyDocument(Target, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, true);
	TemplateUpdateRequest.bSaveAsset = false;
	const FAssetDocumentResult TemplateUpdateResult = Service.Apply(TemplateUpdateRequest);
	if (!TemplateUpdateResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint template update failed: %s"), *TemplateUpdateResult.Message));
	}
	TestTrue(TEXT("AnimBlueprint update succeeds"), TemplateUpdateResult.IsSuccess());
	if (AnimBlueprint)
	{
		TestTrue(TEXT("Updated AnimBlueprint is template"), AnimBlueprint->bIsTemplate);
		TestNull(TEXT("Template update clears TargetSkeleton"), AnimBlueprint->TargetSkeleton.Get());
	}

	const FString BadTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_Invalid_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentApplyRequest BadRequest;
	BadRequest.Document = MakeAnimBlueprintApplyDocument(BadTarget, TEXT("/Script/Engine.Actor"), SkeletonPath, PreviewMeshPath, false);
	BadRequest.bSaveAsset = false;
	const FAssetDocumentResult BadResult = Service.Apply(BadRequest);
	TestFalse(TEXT("Invalid AnimBlueprint create preflight fails"), BadResult.IsSuccess());

	UObject* BadAsset = FindObject<UObject>(
		nullptr,
		*FString::Printf(TEXT("%s.%s"), *BadTarget, *FPackageName::GetLongPackageAssetName(BadTarget)));
	TestNull(TEXT("Invalid create does not leave a loadable asset"), BadAsset);

	const FString MissingSkeletonTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_MissingSkeleton_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentApplyRequest MissingSkeletonRequest;
	MissingSkeletonRequest.Document = MakeAnimBlueprintApplyDocument(MissingSkeletonTarget, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, false);
	MissingSkeletonRequest.Document->GetObjectField(TEXT("Body"))->RemoveField(TEXT("TargetSkeleton"));
	MissingSkeletonRequest.bSaveAsset = false;
	const FAssetDocumentResult MissingSkeletonResult = Service.Apply(MissingSkeletonRequest);
	TestFalse(TEXT("Service create rejects non-template AnimBlueprint without TargetSkeleton"), MissingSkeletonResult.IsSuccess());
	TestTrue(
		TEXT("Missing skeleton create failure mentions TargetSkeleton"),
		MissingSkeletonResult.Message.Contains(TEXT("TargetSkeleton")) || MissingSkeletonResult.Message.Contains(TEXT("MissingTargetSkeleton")));
	UObject* MissingSkeletonAsset = FindObject<UObject>(
		nullptr,
		*FString::Printf(TEXT("%s.%s"), *MissingSkeletonTarget, *FPackageName::GetLongPackageAssetName(MissingSkeletonTarget)));
	TestNull(TEXT("Missing skeleton create does not leave a loadable asset"), MissingSkeletonAsset);

	const FString MismatchedPreviewTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_UpdatePreview_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString MismatchedPreviewMeshPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP");
	FAssetDocumentApplyRequest PreviewBaseRequest;
	PreviewBaseRequest.Document = MakeAnimBlueprintApplyDocument(MismatchedPreviewTarget, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, false);
	PreviewBaseRequest.bSaveAsset = false;
	const FAssetDocumentResult PreviewBaseResult = Service.Apply(PreviewBaseRequest);
	if (!PreviewBaseResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint preview mismatch base apply failed: %s"), *PreviewBaseResult.Message));
	}
	TestTrue(TEXT("Preview mismatch base AnimBlueprint apply succeeds"), PreviewBaseResult.IsSuccess());

	TSharedRef<FJsonObject> PreviewUpdateDocument = MakeAnimBlueprintApplyDocument(MismatchedPreviewTarget, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, false);
	TSharedPtr<FJsonObject> PreviewUpdateBody = PreviewUpdateDocument->GetObjectField(TEXT("Body"));
	PreviewUpdateBody->Values.Empty();
	TSharedRef<FJsonObject> PreviewUpdate = MakeShared<FJsonObject>();
	PreviewUpdate->SetObjectField(TEXT("PreviewSkeletalMesh"), MakeAssetRef(MismatchedPreviewMeshPath));
	PreviewUpdateBody->SetObjectField(TEXT("Preview"), PreviewUpdate);

	FAssetDocumentApplyRequest PreviewUpdateRequest;
	PreviewUpdateRequest.Document = PreviewUpdateDocument;
	PreviewUpdateRequest.bSaveAsset = false;
	const FAssetDocumentResult PreviewUpdateResult = Service.Apply(PreviewUpdateRequest);
	TestFalse(TEXT("Update-only PreviewSkeletalMesh rejects skeleton mismatch against existing TargetSkeleton"), PreviewUpdateResult.IsSuccess());
	TestTrue(
		TEXT("Update-only preview mismatch reports PreviewSkeletalMesh diagnostic"),
		PreviewUpdateResult.Diagnostics.ContainsByPredicate([](const FAssetDocumentDiagnostic& Diagnostic)
		{
			return Diagnostic.Path == TEXT("/Body/Preview/PreviewSkeletalMesh")
				&& Diagnostic.Code == TEXT("MismatchedPreviewSkeletalMeshSkeleton");
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintBlueprintCommonRegionsTest,
	"AssetFactory.AssetDocument.AnimBlueprint.BlueprintCommonRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintBlueprintCommonRegionsTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_Common_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
	const FString InterfacePath = TEXT("/Script/Engine.ActorSoundParameterInterface");

	TSharedRef<FJsonObject> Document = MakeAnimBlueprintApplyDocument(Target);
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	SetImplementedInterfaces(Document, {MakeImplementedInterface(InterfacePath)});
	Body->SetArrayField(TEXT("Variables"), MakeVariableArray({MakeFloatVariable(TEXT("Speed"), TEXT("123.0"))}));
	Body->GetObjectField(TEXT("ClassDefaults"))->SetBoolField(TEXT("bUseMainInstanceMontageEvaluationData"), true);
	Body->SetArrayField(TEXT("UbergraphPages"), {});

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(Request);
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint common region apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Blueprint common regions apply succeeds"), ApplyResult.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created AnimBlueprint loads"), AnimBlueprint);
	UClass* InterfaceClass = LoadObject<UClass>(nullptr, *InterfacePath);
	TestNotNull(TEXT("Interface class loads"), InterfaceClass);
	if (AnimBlueprint && InterfaceClass)
	{
		TestTrue(
			TEXT("AnimBlueprint implements authored interface"),
			AnimBlueprint->ImplementedInterfaces.ContainsByPredicate([InterfaceClass](const FBPInterfaceDescription& Interface)
			{
				return Interface.Interface == InterfaceClass;
			}));
		TestTrue(TEXT("AnimBlueprint has authored variable"), HasBlueprintVariable(AnimBlueprint, TEXT("Speed")));

		UObject* GeneratedCDO = AnimBlueprint->GeneratedClass ? AnimBlueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
		FBoolProperty* MontageDataProperty = GeneratedCDO
			? FindFProperty<FBoolProperty>(GeneratedCDO->GetClass(), TEXT("bUseMainInstanceMontageEvaluationData"))
			: nullptr;
		TestNotNull(TEXT("ClassDefaults property resolves on generated CDO"), MontageDataProperty);
		if (GeneratedCDO && MontageDataProperty)
		{
			TestTrue(TEXT("ClassDefaults bool is applied"), MontageDataProperty->GetPropertyValue_InContainer(GeneratedCDO));
		}
	}

	FAssetDocumentCapabilityContext Context;
	Context.Asset = AnimBlueprint;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;
	TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
	TestTrue(TEXT("Extract succeeds"), ExtractResult.bSuccess);
	TestTrue(TEXT("Extract includes one implemented interface"), HasArrayFieldCount(ExtractedBody, TEXT("ImplementedInterfaces"), 1));
	TestTrue(TEXT("Extract includes one variable"), HasArrayFieldCount(ExtractedBody, TEXT("Variables"), 1));
	TestTrue(TEXT("Extract includes empty UbergraphPages"), HasArrayFieldCount(ExtractedBody, TEXT("UbergraphPages"), 0));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult =
		Capability.Diff(Context, MakeShared<FJsonValueObject>(Body.ToSharedRef()), DiffEntries);
	TestTrue(TEXT("Diff succeeds for Blueprint common regions"), DiffResult.bSuccess);
	TestTrue(TEXT("Diff reports implemented interface semantic path"), HasDiffPath(DiffEntries, FString::Printf(TEXT("/Body/ImplementedInterfaces/%s"), *InterfacePath)));
	TestTrue(TEXT("Diff reports variable semantic path"), HasDiffPath(DiffEntries, TEXT("/Body/Variables/Speed")));

	FAssetDocumentCapabilityContext ValidationContext;
	ValidationContext.AssetClass = UAnimBlueprint::StaticClass();
	TSharedRef<FJsonObject> InvalidInterfaceBody = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> InvalidInterfaces;
	InvalidInterfaces.Add(MakeShared<FJsonValueObject>(MakeImplementedInterface(TEXT("/Script/Engine.Actor")).ToSharedRef()));
	InvalidInterfaceBody->SetArrayField(TEXT("ImplementedInterfaces"), InvalidInterfaces);
	const FAssetDocumentCapabilityResult InvalidInterfaceResult =
		Capability.Validate(ValidationContext, MakeShared<FJsonValueObject>(InvalidInterfaceBody));
	TestFalse(TEXT("ImplementedInterfaces validates interface classes"), InvalidInterfaceResult.bSuccess);
	TestTrue(
		TEXT("Invalid interface reports exact common diagnostic"),
		HasDiagnostic(InvalidInterfaceResult, TEXT("/Body/ImplementedInterfaces/0/Interface/Class"), TEXT("InvalidInterfaceClass")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintSyncGroupsTest,
	"AssetFactory.AssetDocument.AnimBlueprint.SyncGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintSyncGroupsTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_SyncGroups_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));

	TSharedRef<FJsonObject> Document = MakeAnimBlueprintApplyDocument(Target);
	SetSyncGroups(Document, {TEXT("Locomotion"), TEXT("UpperBody")});

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	const FAssetDocumentResult Result = Service.Apply(Request);
	if (!Result.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint SyncGroups apply failed: %s"), *Result.Message));
	}
	TestTrue(TEXT("SyncGroups apply succeeds"), Result.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created AnimBlueprint loads"), AnimBlueprint);
	if (AnimBlueprint)
	{
		TestEqual(TEXT("Two sync groups applied"), AnimBlueprint->Groups.Num(), 2);
		if (AnimBlueprint->Groups.Num() == 2)
		{
			TestEqual(TEXT("First sync group preserves authored order"), AnimBlueprint->Groups[0].Name, FName(TEXT("Locomotion")));
			TestEqual(TEXT("Second sync group preserves authored order"), AnimBlueprint->Groups[1].Name, FName(TEXT("UpperBody")));
		}
	}

	FAssetDocumentCapabilityContext Context;
	Context.Asset = AnimBlueprint;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;
	TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
	TestTrue(TEXT("Extract succeeds"), ExtractResult.bSuccess);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedGroups = nullptr;
	TestTrue(TEXT("Extract includes SyncGroups array"), ExtractedBody->TryGetArrayField(TEXT("SyncGroups"), ExtractedGroups));
	if (ExtractedGroups)
	{
		TestEqual(TEXT("Extracted SyncGroups count"), ExtractedGroups->Num(), 2);
	}

	FAssetDocumentCapabilityContext ValidationContext;
	ValidationContext.AssetClass = UAnimBlueprint::StaticClass();
	const FAssetDocumentCapabilityResult DuplicateResult =
		Capability.Validate(ValidationContext, MakeBodyWithSyncGroups({TEXT("Locomotion"), TEXT("locomotion")}));
	TestFalse(TEXT("Duplicate sync group names reject case-insensitively"), DuplicateResult.bSuccess);
	TestTrue(
		TEXT("Duplicate sync group reports exact index diagnostic"),
		HasDiagnostic(DuplicateResult, TEXT("/Body/SyncGroups/1/Name"), TEXT("DuplicateSyncGroupName")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult =
		Capability.Diff(Context, MakeBodyWithSyncGroups({TEXT("Locomotion"), TEXT("AimOffset")}), DiffEntries);
	TestTrue(TEXT("SyncGroups diff succeeds"), DiffResult.bSuccess);
	TestTrue(TEXT("Diff uses stable added identity path"), HasDiffPath(DiffEntries, TEXT("/Body/SyncGroups/AimOffset")));
	TestTrue(TEXT("Diff uses stable removed identity path"), HasDiffPath(DiffEntries, TEXT("/Body/SyncGroups/UpperBody")));
	TestFalse(TEXT("Diff does not use numeric SyncGroups paths"), HasNumericSyncGroupDiffPath(DiffEntries));

	return true;
}

#endif
