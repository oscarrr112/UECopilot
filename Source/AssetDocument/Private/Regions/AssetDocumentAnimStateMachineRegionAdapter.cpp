// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentAnimStateMachineRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentPropertyAdapter.h"
#include "Animation/AnimBlueprint.h"
#include "AnimationGraph.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "EdGraphSchema_K2.h"
#include "Graphs/AssetDocumentAnimationGraphNodeActionProvider.h"
#include "Graphs/AssetDocumentAnimationGraphRuntime.h"
#include "Graphs/AssetDocumentGraphDiff.h"
#include "Graphs/AssetDocumentGraphParser.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"

namespace
{
constexpr const TCHAR* StateMachinesRegionId = TEXT("Body.StateMachines");
constexpr const TCHAR* TransitionGraphsRegionId = TEXT("Body.TransitionGraphs");
constexpr const TCHAR* TransitionResultPin = TEXT("CanEnterTransition");

FAssetDocumentCapabilityResult Failure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FString Escape(const FString& Token)
{
	return FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Token);
}

FString NormalizeIdentity(const FString& Identity)
{
	return FName(*Identity).ToString().ToLower();
}

bool IsEmptyCompatibilityValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return true;
	}
	if (Value->Type == EJson::Array)
	{
		return Value->AsArray().Num() == 0;
	}
	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		return Object.IsValid() && Object->Values.Num() == 0;
	}
	return false;
}

TSharedPtr<FJsonValue> MakeEmptyArrayValue()
{
	return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>());
}

TSharedPtr<FJsonValue> MakeEmptyStateMachinesValue()
{
	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), {});
	return MakeShared<FJsonValueObject>(Region);
}

FAssetDocumentCapabilityResult FromGraphDiagnostics(const TArray<FAssetDocumentGraphDiagnostic>& Diagnostics)
{
	if (Diagnostics.IsEmpty())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentCapabilityResult Result;
	Result.bSuccess = false;
	Result.Message = Diagnostics[0].Message;
	for (const FAssetDocumentGraphDiagnostic& GraphDiagnostic : Diagnostics)
	{
		FAssetDocumentDiagnostic Diagnostic;
		Diagnostic.Path = GraphDiagnostic.Path;
		Diagnostic.Code = GraphDiagnostic.Code;
		Diagnostic.Message = GraphDiagnostic.Message;
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
	}
	return Result;
}

FAssetDocumentCapabilityResult ParseStateMachinesRegion(
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<FAssetDocumentGraphSpec>& OutGraphs)
{
	if (!DesiredValue.IsValid() || DesiredValue->Type != EJson::Object)
	{
		return Failure(
			TEXT("/Body/StateMachines"),
			TEXT("InvalidStateMachinesRegionType"),
			TEXT("Body.StateMachines must be an object with a Graphs array."));
	}

	const TSharedPtr<FJsonObject> RegionObject = DesiredValue->AsObject();
	if (!RegionObject.IsValid())
	{
		return Failure(
			TEXT("/Body/StateMachines"),
			TEXT("InvalidStateMachinesRegionType"),
			TEXT("Body.StateMachines must be an object with a Graphs array."));
	}

	FAssetDocumentGraphParseOptions Options;
	Options.Path = TEXT("/Body/StateMachines");
	const FAssetDocumentGraphParseResult ParseResult =
		FAssetDocumentGraphParser::ParseGraphRegion(RegionObject.ToSharedRef(), Options);
	if (!ParseResult.IsValid())
	{
		return FromGraphDiagnostics(ParseResult.Diagnostics);
	}

	TSet<FString> MachineIds;
	for (const FAssetDocumentGraphSpec& Graph : ParseResult.Graphs)
	{
		if (Graph.Kind != TEXT("StateMachine"))
		{
			return Failure(
				FString::Printf(TEXT("/Body/StateMachines/Graphs/%s/Kind"), *Escape(Graph.Id)),
				TEXT("InvalidStateMachineGraphKind"),
				TEXT("Body.StateMachines root graphs must use Kind='StateMachine'."));
		}
		const FString NormalizedId = NormalizeIdentity(Graph.Id);
		if (MachineIds.Contains(NormalizedId))
		{
			return Failure(
				FString::Printf(TEXT("/Body/StateMachines/Graphs/%s/Id"), *Escape(Graph.Id)),
				TEXT("DuplicateStateMachineName"),
				FString::Printf(TEXT("Duplicate StateMachine graph identity '%s'."), *Graph.Id));
		}
		MachineIds.Add(NormalizedId);
	}

	OutGraphs = ParseResult.Graphs;
	return FAssetDocumentCapabilityResult::Success();
}

UAnimBlueprint* ResolveAnimBlueprint(const FAssetDocumentRegionContext& Context)
{
	return Cast<UAnimBlueprint>(Context.Asset);
}

UAnimationGraph* FindRootAnimGraph(UAnimBlueprint* AnimBlueprint)
{
	if (!AnimBlueprint)
	{
		return nullptr;
	}
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		UAnimationGraph* AnimGraph = Cast<UAnimationGraph>(Graph);
		if (AnimGraph && AnimGraph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
		{
			return AnimGraph;
		}
	}
	return Cast<UAnimationGraph>(
		FindObject<UEdGraph>(AnimBlueprint, *UEdGraphSchema_K2::GN_AnimGraph.ToString()));
}

FVector2f ReadGraphPosition(const TSharedPtr<FJsonObject>& Position, float DefaultX, float DefaultY)
{
	double X = DefaultX;
	double Y = DefaultY;
	if (Position.IsValid())
	{
		Position->TryGetNumberField(TEXT("X"), X);
		Position->TryGetNumberField(TEXT("Y"), Y);
	}
	return FVector2f(static_cast<float>(X), static_cast<float>(Y));
}

bool TryReadRequiredString(
	const TSharedPtr<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	const FString& MissingCode,
	const FString& InvalidCode,
	FString& OutValue,
	FAssetDocumentCapabilityResult& OutResult)
{
	if (!Object.IsValid() || !Object->TryGetStringField(FieldName, OutValue))
	{
		OutResult = Failure(Path, MissingCode, FString::Printf(TEXT("%s is required"), *Path));
		return false;
	}
	OutValue = OutValue.TrimStartAndEnd();
	if (OutValue.IsEmpty())
	{
		OutResult = Failure(Path, InvalidCode, FString::Printf(TEXT("%s must not be empty"), *Path));
		return false;
	}
	return true;
}

FAssetDocumentCapabilityResult ValidateNoUnknownFields(
	const TSharedPtr<FJsonObject>& Object,
	const TSet<FString>& KnownFields,
	const FString& Path)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (!KnownFields.Contains(Pair.Key))
		{
			return Failure(
				FString::Printf(TEXT("%s/%s"), *Path, *Escape(Pair.Key)),
				TEXT("UnknownAnimStateMachineField"),
				FString::Printf(TEXT("Unknown state-machine field '%s'"), *Pair.Key));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateTransitionResult(
	const TSharedPtr<FJsonObject>& ResultObject,
	const FString& Path)
{
	if (!ResultObject.IsValid())
	{
		return Failure(Path, TEXT("InvalidTransitionGraphResult"), TEXT("Transition graph Result must be an object"));
	}

	const TSharedPtr<FJsonValue>* NodeValue = ResultObject->Values.Find(TEXT("Node"));
	if (!NodeValue || !NodeValue->IsValid() || (*NodeValue)->Type != EJson::Null)
	{
		return Failure(
			Path / TEXT("Node"),
			TEXT("UnsupportedTransitionGraphResultSource"),
			TEXT("Transition graph pilot only supports Result.Node=null"));
	}

	FString Pin;
	if (!ResultObject->TryGetStringField(TEXT("Pin"), Pin) || Pin != TransitionResultPin)
	{
		return Failure(
			Path / TEXT("Pin"),
			TEXT("UnsupportedTransitionGraphResultPin"),
			TEXT("Transition graph pilot only supports Result.Pin='CanEnterTransition'"));
	}

	return ValidateNoUnknownFields(ResultObject, {TEXT("Node"), TEXT("Pin")}, Path);
}

FAssetDocumentCapabilityResult ValidateStateMachinesValue(const TSharedPtr<FJsonValue>& DesiredValue)
{
	if (IsEmptyCompatibilityValue(DesiredValue))
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (DesiredValue->Type != EJson::Array)
	{
		return Failure(TEXT("/Body/StateMachines"), TEXT("InvalidStateMachinesRegionType"), TEXT("Body.StateMachines must be an array"));
	}

	TSet<FString> MachineIdentities;
	const TArray<TSharedPtr<FJsonValue>>& Machines = DesiredValue->AsArray();
	if (Machines.Num() > 0)
	{
		return Failure(
			TEXT("/Body/StateMachines"),
			TEXT("UnsupportedAnimBlueprintRegion"),
			TEXT("Body.StateMachines authoring is deferred until state-machine graph materialization is supported"));
	}
	for (int32 MachineIndex = 0; MachineIndex < Machines.Num(); ++MachineIndex)
	{
		const TSharedPtr<FJsonObject> Machine = Machines[MachineIndex].IsValid() ? Machines[MachineIndex]->AsObject() : nullptr;
		if (!Machine.IsValid())
		{
			return Failure(
				FString::Printf(TEXT("/Body/StateMachines/%d"), MachineIndex),
				TEXT("InvalidStateMachine"),
				TEXT("Body.StateMachines entries must be objects"));
		}

		FAssetDocumentCapabilityResult Result = ValidateNoUnknownFields(
			Machine,
			{TEXT("Name"), TEXT("EntryState"), TEXT("States"), TEXT("Transitions")},
			FString::Printf(TEXT("/Body/StateMachines/%d"), MachineIndex));
		if (!Result.bSuccess)
		{
			return Result;
		}

		FString MachineName;
		if (!TryReadRequiredString(
			Machine,
			TEXT("Name"),
			FString::Printf(TEXT("/Body/StateMachines/%d/Name"), MachineIndex),
			TEXT("MissingStateMachineName"),
			TEXT("InvalidStateMachineName"),
			MachineName,
			Result))
		{
			return Result;
		}

		const FString NormalizedMachine = NormalizeIdentity(MachineName);
		if (MachineIdentities.Contains(NormalizedMachine))
		{
			return Failure(
				FString::Printf(TEXT("/Body/StateMachines/%d/Name"), MachineIndex),
				TEXT("DuplicateStateMachineName"),
				FString::Printf(TEXT("Duplicate StateMachines identity '%s'"), *MachineName));
		}
		MachineIdentities.Add(NormalizedMachine);

		const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
		if (!Machine->TryGetArrayField(TEXT("States"), States) || !States)
		{
			return Failure(
				FString::Printf(TEXT("/Body/StateMachines/%s/States"), *Escape(MachineName)),
				TEXT("MissingStateMachineStates"),
				TEXT("State machine requires States array"));
		}

		TSet<FString> StateIdentities;
		for (int32 StateIndex = 0; StateIndex < States->Num(); ++StateIndex)
		{
			const TSharedPtr<FJsonObject> State = (*States)[StateIndex].IsValid() ? (*States)[StateIndex]->AsObject() : nullptr;
			if (!State.IsValid())
			{
				return Failure(
					FString::Printf(TEXT("/Body/StateMachines/%s/States/%d"), *Escape(MachineName), StateIndex),
					TEXT("InvalidStateMachineState"),
					TEXT("State entries must be objects"));
			}

			Result = ValidateNoUnknownFields(
				State,
				{TEXT("Id"), TEXT("Name"), TEXT("Graph")},
				FString::Printf(TEXT("/Body/StateMachines/%s/States/%d"), *Escape(MachineName), StateIndex));
			if (!Result.bSuccess)
			{
				return Result;
			}

			FString StateId;
			if (!TryReadRequiredString(
				State,
				TEXT("Id"),
				FString::Printf(TEXT("/Body/StateMachines/%s/States/%d/Id"), *Escape(MachineName), StateIndex),
				TEXT("MissingStateId"),
				TEXT("InvalidStateId"),
				StateId,
				Result))
			{
				return Result;
			}

			const FString NormalizedState = NormalizeIdentity(StateId);
			if (StateIdentities.Contains(NormalizedState))
			{
				return Failure(
					FString::Printf(TEXT("/Body/StateMachines/%s/States/%d/Id"), *Escape(MachineName), StateIndex),
					TEXT("DuplicateStateId"),
					FString::Printf(TEXT("Duplicate state identity '%s' in StateMachine '%s'"), *StateId, *MachineName));
			}
			StateIdentities.Add(NormalizedState);
		}

		FString EntryState;
		if (Machine->TryGetStringField(TEXT("EntryState"), EntryState)
			&& !EntryState.TrimStartAndEnd().IsEmpty()
			&& !StateIdentities.Contains(NormalizeIdentity(EntryState)))
		{
			return Failure(
				FString::Printf(TEXT("/Body/StateMachines/%s/EntryState"), *Escape(MachineName)),
				TEXT("UnknownEntryState"),
				FString::Printf(TEXT("EntryState '%s' does not match a state identity"), *EntryState));
		}

		const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
		if (!Machine->TryGetArrayField(TEXT("Transitions"), Transitions) || !Transitions)
		{
			continue;
		}

		TSet<FString> TransitionIdentities;
		for (int32 TransitionIndex = 0; TransitionIndex < Transitions->Num(); ++TransitionIndex)
		{
			const TSharedPtr<FJsonObject> Transition = (*Transitions)[TransitionIndex].IsValid() ? (*Transitions)[TransitionIndex]->AsObject() : nullptr;
			if (!Transition.IsValid())
			{
				return Failure(
					FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%d"), *Escape(MachineName), TransitionIndex),
					TEXT("InvalidStateTransition"),
					TEXT("Transition entries must be objects"));
			}

			Result = ValidateNoUnknownFields(
				Transition,
				{TEXT("Id"), TEXT("From"), TEXT("To"), TEXT("Rule")},
				FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%d"), *Escape(MachineName), TransitionIndex));
			if (!Result.bSuccess)
			{
				return Result;
			}

			FString TransitionId;
			if (!TryReadRequiredString(
				Transition,
				TEXT("Id"),
				FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%d/Id"), *Escape(MachineName), TransitionIndex),
				TEXT("MissingTransitionId"),
				TEXT("InvalidTransitionId"),
				TransitionId,
				Result))
			{
				return Result;
			}

			const FString NormalizedTransition = NormalizeIdentity(TransitionId);
			if (TransitionIdentities.Contains(NormalizedTransition))
			{
				return Failure(
					FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%d/Id"), *Escape(MachineName), TransitionIndex),
					TEXT("DuplicateTransitionId"),
					FString::Printf(TEXT("Duplicate transition identity '%s' in StateMachine '%s'"), *TransitionId, *MachineName));
			}
			TransitionIdentities.Add(NormalizedTransition);

			for (const TCHAR* Endpoint : {TEXT("From"), TEXT("To")})
			{
				FString StateRef;
				if (!TryReadRequiredString(
					Transition,
					Endpoint,
					FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%s/%s"), *Escape(MachineName), *Escape(TransitionId), Endpoint),
					FString::Printf(TEXT("MissingTransition%s"), Endpoint),
					FString::Printf(TEXT("InvalidTransition%s"), Endpoint),
					StateRef,
					Result))
				{
					return Result;
				}
				if (!StateIdentities.Contains(NormalizeIdentity(StateRef)))
				{
					return Failure(
						FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%s/%s"), *Escape(MachineName), *Escape(TransitionId), Endpoint),
						TEXT("UnknownTransitionState"),
						FString::Printf(TEXT("Transition endpoint '%s' does not match a state identity"), *StateRef));
				}
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint state machines"));
}

FAssetDocumentCapabilityResult ValidateTransitionGraphsValue(const TSharedPtr<FJsonValue>& DesiredValue)
{
	if (IsEmptyCompatibilityValue(DesiredValue))
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (DesiredValue->Type != EJson::Array)
	{
		return Failure(TEXT("/Body/TransitionGraphs"), TEXT("InvalidTransitionGraphsRegionType"), TEXT("Body.TransitionGraphs must be an array"));
	}

	TSet<FString> TransitionGraphIdentities;
	const TArray<TSharedPtr<FJsonValue>>& Graphs = DesiredValue->AsArray();
	if (Graphs.Num() > 0)
	{
		return Failure(
			TEXT("/Body/TransitionGraphs"),
			TEXT("UnsupportedAnimBlueprintRegion"),
			TEXT("Body.TransitionGraphs authoring is deferred until transition rule graph materialization is supported"));
	}
	for (int32 GraphIndex = 0; GraphIndex < Graphs.Num(); ++GraphIndex)
	{
		const TSharedPtr<FJsonObject> Graph = Graphs[GraphIndex].IsValid() ? Graphs[GraphIndex]->AsObject() : nullptr;
		if (!Graph.IsValid())
		{
			return Failure(
				FString::Printf(TEXT("/Body/TransitionGraphs/%d"), GraphIndex),
				TEXT("InvalidTransitionGraph"),
				TEXT("Body.TransitionGraphs entries must be objects"));
		}

		FAssetDocumentCapabilityResult Result = ValidateNoUnknownFields(
			Graph,
			{TEXT("StateMachine"), TEXT("Transition"), TEXT("Nodes"), TEXT("Result")},
			FString::Printf(TEXT("/Body/TransitionGraphs/%d"), GraphIndex));
		if (!Result.bSuccess)
		{
			return Result;
		}

		FString MachineName;
		if (!TryReadRequiredString(
			Graph,
			TEXT("StateMachine"),
			FString::Printf(TEXT("/Body/TransitionGraphs/%d/StateMachine"), GraphIndex),
			TEXT("MissingTransitionGraphStateMachine"),
			TEXT("InvalidTransitionGraphStateMachine"),
			MachineName,
			Result))
		{
			return Result;
		}

		FString TransitionId;
		if (!TryReadRequiredString(
			Graph,
			TEXT("Transition"),
			FString::Printf(TEXT("/Body/TransitionGraphs/%s/%d/Transition"), *Escape(MachineName), GraphIndex),
			TEXT("MissingTransitionGraphTransition"),
			TEXT("InvalidTransitionGraphTransition"),
			TransitionId,
			Result))
		{
			return Result;
		}

		const FString GraphIdentity = NormalizeIdentity(MachineName) / NormalizeIdentity(TransitionId);
		if (TransitionGraphIdentities.Contains(GraphIdentity))
		{
			return Failure(
				FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s"), *Escape(MachineName), *Escape(TransitionId)),
				TEXT("DuplicateTransitionGraph"),
				FString::Printf(TEXT("Duplicate TransitionGraphs identity '%s/%s'"), *MachineName, *TransitionId));
		}
		TransitionGraphIdentities.Add(GraphIdentity);

		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
		{
			return Failure(
				FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s/Nodes"), *Escape(MachineName), *Escape(TransitionId)),
				TEXT("MissingTransitionGraphNodes"),
				TEXT("Transition graph requires Nodes array"));
		}
		if (Nodes->Num() > 0)
		{
			return Failure(
				FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s/Nodes/0"), *Escape(MachineName), *Escape(TransitionId)),
				TEXT("UnsupportedTransitionGraphNode"),
				TEXT("Transition graph pilot does not support authored rule nodes yet"));
		}

		const TSharedPtr<FJsonObject>* ResultObject = nullptr;
		if (!Graph->TryGetObjectField(TEXT("Result"), ResultObject) || !ResultObject || !ResultObject->IsValid())
		{
			return Failure(
				FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s/Result"), *Escape(MachineName), *Escape(TransitionId)),
				TEXT("InvalidTransitionGraphResult"),
				TEXT("Transition graph requires Result object"));
		}

		Result = ValidateTransitionResult(*ResultObject, FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s/Result"), *Escape(MachineName), *Escape(TransitionId)));
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint transition graphs"));
}

FString StateMachinePath(const TSharedPtr<FJsonObject>& Machine, int32 FallbackIndex)
{
	FString Name;
	if (Machine.IsValid() && Machine->TryGetStringField(TEXT("Name"), Name) && !Name.TrimStartAndEnd().IsEmpty())
	{
		return FString::Printf(TEXT("/Body/StateMachines/%s"), *Escape(Name));
	}
	return FString::Printf(TEXT("/Body/StateMachines/%d"), FallbackIndex);
}

FString TransitionGraphPath(const TSharedPtr<FJsonObject>& Graph, int32 FallbackIndex)
{
	FString MachineName;
	FString TransitionId;
	if (Graph.IsValid()
		&& Graph->TryGetStringField(TEXT("StateMachine"), MachineName)
		&& Graph->TryGetStringField(TEXT("Transition"), TransitionId)
		&& !MachineName.TrimStartAndEnd().IsEmpty()
		&& !TransitionId.TrimStartAndEnd().IsEmpty())
	{
		return FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s"), *Escape(MachineName), *Escape(TransitionId));
	}
	return FString::Printf(TEXT("/Body/TransitionGraphs/%d"), FallbackIndex);
}

void AddArrayDiffEntries(
	const TSharedPtr<FJsonValue>& DesiredValue,
	const FString& RegionId,
	TFunctionRef<FString(const TSharedPtr<FJsonObject>&, int32)> MakePath,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	if (IsEmptyCompatibilityValue(DesiredValue))
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>& DesiredArray = DesiredValue->AsArray();
	for (int32 Index = 0; Index < DesiredArray.Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Object = DesiredArray[Index].IsValid() ? DesiredArray[Index]->AsObject() : nullptr;
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			MakePath(Object, Index),
			TEXT("added"),
			MakeEmptyArrayValue(),
			DesiredArray[Index]);
	}

	if (DesiredArray.Num() == 0)
	{
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			FString::Printf(TEXT("/Body/%s"), *RegionId),
			TEXT("unchanged"),
			MakeEmptyArrayValue(),
			MakeEmptyArrayValue());
	}
}

FString GraphPath(const FAssetDocumentGraphSpec& GraphSpec)
{
	return FString::Printf(TEXT("/Body/StateMachines/Graphs/%s"), *Escape(GraphSpec.Id));
}

FString NodePath(const FAssetDocumentGraphSpec& GraphSpec, const FAssetDocumentNodeSpec& NodeSpec)
{
	return FString::Printf(TEXT("%s/Nodes/%s"), *GraphPath(GraphSpec), *Escape(NodeSpec.Id));
}

FString LinkPath(const FAssetDocumentGraphSpec& GraphSpec, const FAssetDocumentLinkSpec& Link)
{
	return FString::Printf(TEXT("%s/Links/%s"), *GraphPath(GraphSpec), *Escape(Link.ToKey()));
}

TSharedRef<FJsonObject> MakeOwnerObject(const FString& FieldName, const FString& Value)
{
	TSharedRef<FJsonObject> Owner = MakeShared<FJsonObject>();
	Owner->SetStringField(FieldName, Value);
	return Owner;
}

bool TryReadOwnerField(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FString& FieldName,
	FString& OutValue)
{
	OutValue.Reset();
	if (GraphSpec.Owner.IsValid())
	{
		return GraphSpec.Owner->TryGetStringField(FieldName, OutValue) && !OutValue.TrimStartAndEnd().IsEmpty();
	}
	if (FieldName == TEXT("State") || FieldName == TEXT("Transition"))
	{
		OutValue = GraphSpec.OwnerNodeId;
		return !OutValue.TrimStartAndEnd().IsEmpty();
	}
	return false;
}

FGuid MakeOwnedSubgraphGuid(const FAssetDocumentGraphSpec& StateMachineGraph, const FAssetDocumentGraphSpec& Subgraph)
{
	FString OwnerKey;
	FString OwnerValue;
	if (TryReadOwnerField(Subgraph, TEXT("State"), OwnerValue))
	{
		OwnerKey = TEXT("State");
	}
	else if (TryReadOwnerField(Subgraph, TEXT("Transition"), OwnerValue))
	{
		OwnerKey = TEXT("Transition");
	}
	return FGuid::NewDeterministicGuid(
		FString::Printf(TEXT("AssetDocument.StateMachine.%s.%s.%s.%s"), *StateMachineGraph.Id, *Subgraph.Kind, *OwnerKey, *OwnerValue));
}

FGuid MakeConventionalOwnedSubgraphGuid(
	const FAssetDocumentGraphSpec& StateMachineGraph,
	const FString& Kind,
	const FString& OwnerKey,
	const FString& OwnerValue)
{
	return FGuid::NewDeterministicGuid(
		FString::Printf(TEXT("AssetDocument.StateMachine.%s.%s.%s.%s"), *StateMachineGraph.Id, *Kind, *OwnerKey, *OwnerValue));
}

void StripFrameworkResultSkippedNodes(FAssetDocumentGraphSpec& Graph)
{
	if (!Graph.UnderscoreSkipped.IsValid() || Graph.UnderscoreSkipped->Type != EJson::Object)
	{
		return;
	}

	const TSharedPtr<FJsonObject> Skipped = Graph.UnderscoreSkipped->AsObject();
	if (!Skipped.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (Skipped->TryGetArrayField(TEXT("Nodes"), Nodes) && Nodes)
	{
		TArray<TSharedPtr<FJsonValue>> RemainingNodes;
		for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
		{
			const TSharedPtr<FJsonObject> NodeObject = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			FString ClassPath;
			const bool bFrameworkResultNode = NodeObject.IsValid()
				&& NodeObject->TryGetStringField(TEXT("Class"), ClassPath)
				&& (ClassPath.EndsWith(TEXT(".AnimGraphNode_StateResult"))
					|| ClassPath.EndsWith(TEXT(".AnimGraphNode_TransitionResult")));
			if (!bFrameworkResultNode)
			{
				RemainingNodes.Add(NodeValue);
			}
		}

		if (RemainingNodes.IsEmpty())
		{
			Skipped->RemoveField(TEXT("Nodes"));
		}
		else
		{
			Skipped->SetArrayField(TEXT("Nodes"), MoveTemp(RemainingNodes));
		}
	}

	if (Skipped->Values.IsEmpty())
	{
		Graph.UnderscoreSkipped.Reset();
	}
}

FAssetDocumentCapabilityResult ApplyFields(
	UEdGraphNode* Node,
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec)
{
	if (!NodeSpec.Fields.IsValid() || NodeSpec.Fields->Values.IsEmpty())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const FAssetDocumentPropertyApplyResult Result =
		FAssetDocumentPropertyAdapter::ApplyProperties(Node, NodeSpec.Fields);
	if (Result.bSuccess)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentCapabilityResult FailureResult;
	FailureResult.bSuccess = false;
	FailureResult.Message = Result.Message;
	const FString FieldsPath = NodePath(GraphSpec, NodeSpec) / TEXT("Fields");
	if (Result.Diagnostics.IsEmpty())
	{
		FailureResult.Diagnostics.Add({FieldsPath, TEXT("GraphNodeFieldApplyFailed"), Result.Message});
		return FailureResult;
	}
	for (const FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
	{
		FAssetDocumentDiagnostic Mapped = Diagnostic;
		Mapped.Path = Diagnostic.Path.IsEmpty() ? FieldsPath : FieldsPath / Diagnostic.Path;
		FailureResult.Diagnostics.Add(MoveTemp(Mapped));
	}
	return FailureResult;
}

UAnimGraphNode_StateMachineBase* FindStateMachineNode(UAnimationGraph* RootAnimGraph, const FAssetDocumentGraphSpec& GraphSpec)
{
	if (!RootAnimGraph)
	{
		return nullptr;
	}

	TArray<UAnimGraphNode_StateMachineBase*> Nodes;
	RootAnimGraph->GetNodesOfClass<UAnimGraphNode_StateMachineBase>(Nodes);
	for (UAnimGraphNode_StateMachineBase* Node : Nodes)
	{
		if (!Node || !Node->EditorStateMachineGraph)
		{
			continue;
		}
		if (Node->EditorStateMachineGraph->GetName() == GraphSpec.Name || Node->EditorStateMachineGraph->GetName() == GraphSpec.Id)
		{
			return Node;
		}
	}
	return nullptr;
}

UAnimGraphNode_StateMachineBase* FindOrCreateStateMachineNode(
	UAnimationGraph* RootAnimGraph,
	const FAssetDocumentGraphSpec& GraphSpec,
	bool& bOutChanged)
{
	if (UAnimGraphNode_StateMachineBase* Existing = FindStateMachineNode(RootAnimGraph, GraphSpec))
	{
		return Existing;
	}

	if (!RootAnimGraph)
	{
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_StateMachine> NodeCreator(*RootAnimGraph);
	UAnimGraphNode_StateMachine* StateMachineNode = NodeCreator.CreateNode();
	const FVector2f Location = ReadGraphPosition(GraphSpec.Position, 0.0f, 0.0f);
	StateMachineNode->NodePosX = static_cast<int32>(Location.X);
	StateMachineNode->NodePosY = static_cast<int32>(Location.Y);
	NodeCreator.Finalize();
	StateMachineNode->OnRenameNode(GraphSpec.Name.IsEmpty() ? GraphSpec.Id : GraphSpec.Name);
	if (StateMachineNode->EditorStateMachineGraph)
	{
		FBlueprintEditorUtils::RenameGraph(
			StateMachineNode->EditorStateMachineGraph,
			GraphSpec.Name.IsEmpty() ? GraphSpec.Id : GraphSpec.Name);
	}
	bOutChanged = true;
	return StateMachineNode;
}

bool IsTransitionNodeSpec(const FAssetDocumentNodeSpec& NodeSpec)
{
	return NodeSpec.Kind == TEXT("Transition") || NodeSpec.Class.EndsWith(TEXT(".AnimStateTransitionNode"));
}

bool IsStateNodeSpec(const FAssetDocumentNodeSpec& NodeSpec)
{
	return NodeSpec.Kind.IsEmpty()
		|| NodeSpec.Kind == TEXT("State")
		|| NodeSpec.Class.IsEmpty()
		|| NodeSpec.Class.EndsWith(TEXT(".AnimStateNode"));
}

UAnimStateNodeBase* FindExistingStateMachineNode(UAnimationStateMachineGraph* Graph, const FAssetDocumentNodeSpec& NodeSpec)
{
	if (!Graph)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UAnimStateNodeBase* StateNode = Cast<UAnimStateNodeBase>(Node);
		if (!StateNode)
		{
			continue;
		}
		if (Node->GetName() == NodeSpec.Id || StateNode->GetStateName() == NodeSpec.Id)
		{
			return StateNode;
		}
	}
	return nullptr;
}

FAssetDocumentCapabilityResult MaterializeStateMachineNodes(
	UAnimationStateMachineGraph* StateMachineGraph,
	const FAssetDocumentGraphSpec& GraphSpec,
	TMap<FString, UAnimStateNodeBase*>& OutNodes,
	bool& bOutChanged)
{
	for (const FAssetDocumentNodeSpec& NodeSpec : GraphSpec.Nodes)
	{
		UAnimStateNodeBase* Node = FindExistingStateMachineNode(StateMachineGraph, NodeSpec);
		if (!Node)
		{
			const FVector2f Location = ReadGraphPosition(NodeSpec.Position, 0.0f, 0.0f);
			if (IsTransitionNodeSpec(NodeSpec))
			{
				Node = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateTransitionNode>(
					StateMachineGraph,
					NewObject<UAnimStateTransitionNode>(),
					Location,
					false);
			}
			else if (IsStateNodeSpec(NodeSpec))
			{
				Node = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(
					StateMachineGraph,
					NewObject<UAnimStateNode>(),
					Location,
					false);
			}
			else
			{
				return Failure(
					NodePath(GraphSpec, NodeSpec) / TEXT("Class"),
					TEXT("UnsupportedStateMachineNodeClass"),
					FString::Printf(TEXT("Unsupported state-machine node class '%s'."), *NodeSpec.Class));
			}
			bOutChanged = true;
		}

		if (!Node)
		{
			return Failure(
				NodePath(GraphSpec, NodeSpec) / TEXT("Class"),
				TEXT("StateMachineNodeCreateFailed"),
				FString::Printf(TEXT("State-machine node '%s' could not be created."), *NodeSpec.Id));
		}

		Node->NodePosX = static_cast<int32>(ReadGraphPosition(NodeSpec.Position, Node->NodePosX, Node->NodePosY).X);
		Node->NodePosY = static_cast<int32>(ReadGraphPosition(NodeSpec.Position, Node->NodePosX, Node->NodePosY).Y);
		Node->OnRenameNode(NodeSpec.Id);

		const FAssetDocumentCapabilityResult FieldResult = ApplyFields(Node, GraphSpec, NodeSpec);
		if (!FieldResult.bSuccess)
		{
			return FieldResult;
		}

		OutNodes.Add(NodeSpec.Id, Node);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult MaterializeStateMachineLinks(
	UAnimationStateMachineGraph* StateMachineGraph,
	const FAssetDocumentGraphSpec& GraphSpec,
	const TMap<FString, UAnimStateNodeBase*>& Nodes)
{
	TMap<UAnimStateTransitionNode*, UAnimStateNodeBase*> PreviousByTransition;
	TMap<UAnimStateTransitionNode*, UAnimStateNodeBase*> NextByTransition;

	for (const FAssetDocumentLinkSpec& Link : GraphSpec.Links)
	{
		UAnimStateNodeBase* FromNode = Nodes.FindRef(Link.From.Node);
		UAnimStateNodeBase* ToNode = Nodes.FindRef(Link.To.Node);
		if (!FromNode)
		{
			return Failure(
				LinkPath(GraphSpec, Link) / TEXT("From/Node"),
				TEXT("UnknownStateMachineLinkEndpoint"),
				FString::Printf(TEXT("State-machine link '%s' references unknown source node '%s'."), *Link.ToKey(), *Link.From.Node));
		}
		if (!ToNode)
		{
			return Failure(
				LinkPath(GraphSpec, Link) / TEXT("To/Node"),
				TEXT("UnknownStateMachineLinkEndpoint"),
				FString::Printf(TEXT("State-machine link '%s' references unknown target node '%s'."), *Link.ToKey(), *Link.To.Node));
		}

		UAnimStateTransitionNode* FromTransition = Cast<UAnimStateTransitionNode>(FromNode);
		UAnimStateTransitionNode* ToTransition = Cast<UAnimStateTransitionNode>(ToNode);
		if (!FromTransition && ToTransition)
		{
			PreviousByTransition.Add(ToTransition, FromNode);
		}
		else if (FromTransition && !ToTransition)
		{
			NextByTransition.Add(FromTransition, ToNode);
		}
		else
		{
			return Failure(
				GraphPath(GraphSpec) / TEXT("Links"),
				TEXT("InvalidStateMachineTransitionLink"),
				TEXT("State-machine links must connect State->Transition and Transition->State."));
		}
	}

	for (const TPair<UAnimStateTransitionNode*, UAnimStateNodeBase*>& Pair : PreviousByTransition)
	{
		UAnimStateTransitionNode* Transition = Pair.Key;
		UAnimStateNodeBase* Previous = Pair.Value;
		UAnimStateNodeBase* Next = NextByTransition.FindRef(Transition);
		if (!Next)
		{
			return Failure(
				GraphPath(GraphSpec) / TEXT("Links"),
				TEXT("IncompleteStateMachineTransition"),
				FString::Printf(TEXT("Transition '%s' is missing a target state link."), *Transition->GetName()));
		}
		Transition->CreateConnections(Previous, Next);
	}

	if (const TSharedPtr<FJsonObject> Metadata = GraphSpec.Metadata.IsValid() && GraphSpec.Metadata->Type == EJson::Object
		? GraphSpec.Metadata->AsObject()
		: nullptr)
	{
		FString EntryState;
		if (Metadata->TryGetStringField(TEXT("EntryState"), EntryState))
		{
			UAnimStateNodeBase* EntryNode = Nodes.FindRef(EntryState);
			if (!EntryNode)
			{
				return Failure(
					GraphPath(GraphSpec) / TEXT("Metadata/EntryState"),
					TEXT("UnknownEntryState"),
					FString::Printf(TEXT("EntryState '%s' does not match a state node."), *EntryState));
			}
			if (StateMachineGraph && StateMachineGraph->EntryNode && StateMachineGraph->EntryNode->GetOutputPin() && EntryNode->GetInputPin())
			{
				StateMachineGraph->EntryNode->GetOutputPin()->BreakAllPinLinks(true);
				StateMachineGraph->EntryNode->GetOutputPin()->MakeLinkTo(EntryNode->GetInputPin());
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult EnsureStateResultNode(UAnimationStateGraph* StateGraph, const FString& Path)
{
	if (!StateGraph)
	{
		return Failure(Path, TEXT("MissingStatePoseGraph"), TEXT("StatePose owner does not have an animation state graph."));
	}
	if (StateGraph->GetResultNode())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FGraphNodeCreator<UAnimGraphNode_StateResult> NodeCreator(*StateGraph);
	UAnimGraphNode_StateResult* ResultNode = NodeCreator.CreateNode();
	NodeCreator.Finalize();
	StateGraph->MyResultNode = ResultNode;
	return ResultNode
		? FAssetDocumentCapabilityResult::Success()
		: Failure(Path, TEXT("StateResultNodeCreateFailed"), TEXT("StatePose result node could not be created."));
}

FAssetDocumentCapabilityResult EnsureTransitionResultNode(UAnimationTransitionGraph* TransitionGraph, const FString& Path)
{
	if (!TransitionGraph)
	{
		return Failure(Path, TEXT("MissingTransitionRuleGraph"), TEXT("TransitionRule owner does not have an animation transition graph."));
	}
	if (TransitionGraph->GetResultNode())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FGraphNodeCreator<UAnimGraphNode_TransitionResult> NodeCreator(*TransitionGraph);
	UAnimGraphNode_TransitionResult* ResultNode = NodeCreator.CreateNode();
	NodeCreator.Finalize();
	TransitionGraph->MyResultNode = ResultNode;
	return ResultNode
		? FAssetDocumentCapabilityResult::Success()
		: Failure(Path, TEXT("TransitionResultNodeCreateFailed"), TEXT("TransitionRule result node could not be created."));
}

class FStateMachineOwnedGraphHook final : public IAssetDocumentAnimationGraphStructuralHook
{
public:
	FStateMachineOwnedGraphHook(
		const FAssetDocumentGraphSpec& InStateMachineGraph,
		const TMap<FString, UAnimStateNodeBase*>& InNodes)
		: StateMachineGraph(InStateMachineGraph)
		, Nodes(InNodes)
	{
	}

	virtual FAssetDocumentCapabilityResult LocateOrCreateGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& InOutContext) override
	{
		FString OwnerId;
		if (GraphSpec.Kind == TEXT("StatePose"))
		{
			if (!TryReadOwnerField(GraphSpec, TEXT("State"), OwnerId))
			{
				return Failure(InOutContext.GraphPath / TEXT("Owner/State"), TEXT("MissingStatePoseOwner"), TEXT("StatePose subgraph requires Owner.State."));
			}

			UAnimStateNode* StateNode = Cast<UAnimStateNode>(Nodes.FindRef(OwnerId));
			UAnimationStateGraph* StateGraph = StateNode ? Cast<UAnimationStateGraph>(StateNode->GetBoundGraph()) : nullptr;
			if (!StateGraph)
			{
				return Failure(
					InOutContext.GraphPath / TEXT("Owner/State"),
					TEXT("UnknownStatePoseOwner"),
					FString::Printf(TEXT("StatePose owner state '%s' was not materialized."), *OwnerId));
			}

			InOutContext.Graph = StateGraph;
			StateGraph->Modify();
			StateGraph->GraphGuid = MakeOwnedSubgraphGuid(StateMachineGraph, GraphSpec);
			return EnsureStateResultNode(StateGraph, InOutContext.GraphPath);
		}

		if (GraphSpec.Kind == TEXT("TransitionRule"))
		{
			if (!TryReadOwnerField(GraphSpec, TEXT("Transition"), OwnerId))
			{
				return Failure(InOutContext.GraphPath / TEXT("Owner/Transition"), TEXT("MissingTransitionRuleOwner"), TEXT("TransitionRule subgraph requires Owner.Transition."));
			}

			UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Nodes.FindRef(OwnerId));
			UAnimationTransitionGraph* TransitionGraph = TransitionNode ? Cast<UAnimationTransitionGraph>(TransitionNode->GetBoundGraph()) : nullptr;
			if (!TransitionGraph)
			{
				return Failure(
					InOutContext.GraphPath / TEXT("Owner/Transition"),
					TEXT("UnknownTransitionRuleOwner"),
					FString::Printf(TEXT("TransitionRule owner transition '%s' was not materialized."), *OwnerId));
			}

			InOutContext.Graph = TransitionGraph;
			TransitionGraph->Modify();
			TransitionGraph->GraphGuid = MakeOwnedSubgraphGuid(StateMachineGraph, GraphSpec);
			return EnsureTransitionResultNode(TransitionGraph, InOutContext.GraphPath);
		}

		return Failure(
			InOutContext.GraphPath / TEXT("Kind"),
			TEXT("UnsupportedStateMachineSubgraphKind"),
			FString::Printf(TEXT("Unsupported StateMachine subgraph kind '%s'."), *GraphSpec.Kind));
	}

	virtual FAssetDocumentCapabilityResult RepairAfterApply(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentAnimationGraphContext& Context) override
	{
		if (GraphSpec.Kind == TEXT("StatePose"))
		{
			return EnsureStateResultNode(Cast<UAnimationStateGraph>(Context.Graph), Context.GraphPath);
		}
		if (GraphSpec.Kind == TEXT("TransitionRule"))
		{
			return EnsureTransitionResultNode(Cast<UAnimationTransitionGraph>(Context.Graph), Context.GraphPath);
		}
		return FAssetDocumentCapabilityResult::Success();
	}

private:
	const FAssetDocumentGraphSpec& StateMachineGraph;
	const TMap<FString, UAnimStateNodeBase*>& Nodes;
};

FAssetDocumentCapabilityResult ApplyStateMachineSubgraphs(
	UAnimBlueprint* AnimBlueprint,
	const FAssetDocumentGraphSpec& GraphSpec,
	const TMap<FString, UAnimStateNodeBase*>& Nodes,
	bool& bOutChanged)
{
	if (GraphSpec.Subgraphs.IsEmpty())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FStateMachineOwnedGraphHook Hook(GraphSpec, Nodes);
	const FAssetDocumentAnimationGraphRuntime Runtime(MakeShared<FAssetDocumentAnimationGraphNodeActionProvider>());
	for (const FAssetDocumentGraphSpec& Subgraph : GraphSpec.Subgraphs)
	{
		FAssetDocumentAnimationGraphContext RuntimeContext;
		RuntimeContext.Asset = AnimBlueprint;
		RuntimeContext.Blueprint = AnimBlueprint;
		RuntimeContext.GraphPath = GraphPath(GraphSpec) / TEXT("Subgraphs") / Escape(Subgraph.Id);
		RuntimeContext.GraphKind = Subgraph.Kind;
		const FAssetDocumentCapabilityResult ApplyResult = Runtime.ApplyGraph(Subgraph, RuntimeContext, Hook);
		if (!ApplyResult.bSuccess)
		{
			return ApplyResult;
		}
		bOutChanged = true;
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentGraphSpec ExtractStateMachineGraph(UAnimGraphNode_StateMachineBase* StateMachineNode)
{
	FAssetDocumentGraphSpec Graph;
	UAnimationStateMachineGraph* StateMachineGraph = StateMachineNode ? StateMachineNode->EditorStateMachineGraph : nullptr;
	Graph.Id = StateMachineGraph ? StateMachineGraph->GetName() : FString();
	Graph.Name = Graph.Id;
	Graph.Kind = TEXT("StateMachine");
	Graph.Position = MakeShared<FJsonObject>();
	Graph.Position->SetNumberField(TEXT("X"), StateMachineNode ? StateMachineNode->NodePosX : 0);
	Graph.Position->SetNumberField(TEXT("Y"), StateMachineNode ? StateMachineNode->NodePosY : 0);
	if (!StateMachineGraph)
	{
		return Graph;
	}

	TMap<const UEdGraphNode*, UAnimStateNodeBase*> StateMachineNodesByGraphNode;
	TMap<const UEdGraphNode*, FString> NodeIds;
	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		if (Cast<UAnimStateEntryNode>(Node))
		{
			continue;
		}
		if (UAnimStateNodeBase* StateNode = Cast<UAnimStateNodeBase>(Node))
		{
			FAssetDocumentNodeSpec NodeSpec;
			NodeSpec.Id = StateNode->GetStateName();
			if (NodeSpec.Id.IsEmpty() || NodeSpec.Id == TEXT("BaseState"))
			{
				NodeSpec.Id = StateNode->GetName();
			}
			NodeSpec.Class = StateNode->GetClass()->GetPathName();
			NodeSpec.Kind = Cast<UAnimStateTransitionNode>(StateNode) ? TEXT("Transition") : TEXT("State");
			NodeSpec.Position = MakeShared<FJsonObject>();
			NodeSpec.Position->SetNumberField(TEXT("X"), StateNode->NodePosX);
			NodeSpec.Position->SetNumberField(TEXT("Y"), StateNode->NodePosY);
			NodeIds.Add(StateNode, NodeSpec.Id);
			StateMachineNodesByGraphNode.Add(StateNode, StateNode);
			Graph.Nodes.Add(MoveTemp(NodeSpec));
		}
	}

	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node);
		if (!Transition)
		{
			continue;
		}
		UAnimStateNodeBase* Previous = Transition->GetPreviousState();
		UAnimStateNodeBase* Next = Transition->GetNextState();
		if (Previous && Next)
		{
			FAssetDocumentLinkSpec Incoming;
			Incoming.From.Node = NodeIds.FindRef(Previous);
			Incoming.From.Pin = TEXT("Out");
			Incoming.To.Node = NodeIds.FindRef(Transition);
			Incoming.To.Pin = TEXT("In");
			Graph.Links.Add(Incoming);

			FAssetDocumentLinkSpec Outgoing;
			Outgoing.From.Node = NodeIds.FindRef(Transition);
			Outgoing.From.Pin = TEXT("Out");
			Outgoing.To.Node = NodeIds.FindRef(Next);
			Outgoing.To.Pin = TEXT("In");
			Graph.Links.Add(Outgoing);
		}
	}

	if (StateMachineGraph->EntryNode && StateMachineGraph->EntryNode->GetOutputPin())
	{
		for (UEdGraphPin* LinkedPin : StateMachineGraph->EntryNode->GetOutputPin()->LinkedTo)
		{
			UAnimStateNodeBase* EntryState = LinkedPin ? Cast<UAnimStateNodeBase>(LinkedPin->GetOwningNode()) : nullptr;
			const FString EntryStateId = EntryState ? NodeIds.FindRef(EntryState) : FString();
			if (!EntryStateId.IsEmpty())
			{
				TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
				Metadata->SetStringField(TEXT("EntryState"), EntryStateId);
				Graph.Metadata = MakeShared<FJsonValueObject>(Metadata);
				break;
			}
		}
	}

	const FAssetDocumentAnimationGraphRuntime Runtime;
	UBlueprint* OwnerBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(StateMachineGraph);
	for (const TPair<const UEdGraphNode*, UAnimStateNodeBase*>& Pair : StateMachineNodesByGraphNode)
	{
		UAnimStateNodeBase* StateMachineNodeBase = Pair.Value;
		const FString OwnerId = NodeIds.FindRef(StateMachineNodeBase);
		if (OwnerId.IsEmpty())
		{
			continue;
		}

		if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(StateMachineNodeBase))
		{
			UAnimationTransitionGraph* TransitionGraph = Cast<UAnimationTransitionGraph>(TransitionNode->GetBoundGraph());
			if (!TransitionGraph
				|| TransitionGraph->GraphGuid != MakeConventionalOwnedSubgraphGuid(Graph, TEXT("TransitionRule"), TEXT("Transition"), OwnerId))
			{
				continue;
			}

			EnsureTransitionResultNode(TransitionGraph, GraphPath(Graph) / TEXT("Subgraphs") / Escape(OwnerId + TEXT("Rule")));
			FAssetDocumentAnimationGraphContext RuntimeContext;
			RuntimeContext.Asset = OwnerBlueprint;
			RuntimeContext.Blueprint = OwnerBlueprint;
			RuntimeContext.Graph = TransitionGraph;
			RuntimeContext.GraphKind = TEXT("TransitionRule");
			FAssetDocumentGraphSpec RuleGraph;
			Runtime.ExtractGraph(RuntimeContext, RuleGraph);
			RuleGraph.Id = OwnerId + TEXT("Rule");
			RuleGraph.Kind = TEXT("TransitionRule");
			RuleGraph.Owner = MakeOwnerObject(TEXT("Transition"), OwnerId);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetField(TEXT("Node"), MakeShared<FJsonValueNull>());
			Result->SetStringField(TEXT("Pin"), TransitionResultPin);
			TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
			Metadata->SetObjectField(TEXT("Result"), Result);
			RuleGraph.Metadata = MakeShared<FJsonValueObject>(Metadata);
			StripFrameworkResultSkippedNodes(RuleGraph);
			Graph.Subgraphs.Add(MoveTemp(RuleGraph));
		}
		else if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(StateMachineNodeBase))
		{
			UAnimationStateGraph* StateGraph = Cast<UAnimationStateGraph>(StateNode->GetBoundGraph());
			if (!StateGraph
				|| StateGraph->GraphGuid != MakeConventionalOwnedSubgraphGuid(Graph, TEXT("StatePose"), TEXT("State"), OwnerId))
			{
				continue;
			}

			EnsureStateResultNode(StateGraph, GraphPath(Graph) / TEXT("Subgraphs") / Escape(OwnerId + TEXT("Pose")));
			FAssetDocumentAnimationGraphContext RuntimeContext;
			RuntimeContext.Asset = OwnerBlueprint;
			RuntimeContext.Blueprint = OwnerBlueprint;
			RuntimeContext.Graph = StateGraph;
			RuntimeContext.GraphKind = TEXT("StatePose");
			FAssetDocumentGraphSpec PoseGraph;
			Runtime.ExtractGraph(RuntimeContext, PoseGraph);
			PoseGraph.Id = OwnerId + TEXT("Pose");
			PoseGraph.Kind = TEXT("StatePose");
			PoseGraph.Owner = MakeOwnerObject(TEXT("State"), OwnerId);
			StripFrameworkResultSkippedNodes(PoseGraph);
			Graph.Subgraphs.Add(MoveTemp(PoseGraph));
		}
	}

	return Graph;
}

FAssetDocumentCapabilityResult ApplyStateMachines(
	FAssetDocumentRegionContext& Context,
	const TArray<FAssetDocumentGraphSpec>& Graphs,
	bool& bOutChanged)
{
	UAnimBlueprint* AnimBlueprint = ResolveAnimBlueprint(Context);
	UAnimationGraph* RootAnimGraph = FindRootAnimGraph(AnimBlueprint);
	if (!AnimBlueprint || !RootAnimGraph)
	{
		return Failure(
			TEXT("/Body/StateMachines"),
			TEXT("MissingAnimGraph"),
			TEXT("Body.StateMachines requires an Animation Blueprint with an AnimGraph."));
	}

	for (const FAssetDocumentGraphSpec& GraphSpec : Graphs)
	{
		UAnimGraphNode_StateMachineBase* StateMachineNode =
			FindOrCreateStateMachineNode(RootAnimGraph, GraphSpec, bOutChanged);
		UAnimationStateMachineGraph* StateMachineGraph =
			StateMachineNode ? StateMachineNode->EditorStateMachineGraph : nullptr;
		if (!StateMachineGraph)
		{
			return Failure(
				GraphPath(GraphSpec),
				TEXT("StateMachineGraphCreateFailed"),
				FString::Printf(TEXT("State machine graph '%s' could not be created."), *GraphSpec.Id));
		}

		TMap<FString, UAnimStateNodeBase*> Nodes;
		const FAssetDocumentCapabilityResult NodeResult =
			MaterializeStateMachineNodes(StateMachineGraph, GraphSpec, Nodes, bOutChanged);
		if (!NodeResult.bSuccess)
		{
			return NodeResult;
		}

		const FAssetDocumentCapabilityResult LinkResult =
			MaterializeStateMachineLinks(StateMachineGraph, GraphSpec, Nodes);
		if (!LinkResult.bSuccess)
		{
			return LinkResult;
		}

		const FAssetDocumentCapabilityResult SubgraphResult =
			ApplyStateMachineSubgraphs(AnimBlueprint, GraphSpec, Nodes, bOutChanged);
		if (!SubgraphResult.bSuccess)
		{
			return SubgraphResult;
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Applied AnimBlueprint state machines"));
}

TSharedPtr<FJsonValue> ExtractStateMachinesValue(const FAssetDocumentRegionContext& Context)
{
	TArray<FAssetDocumentGraphSpec> Graphs;
	if (UAnimationGraph* RootAnimGraph = FindRootAnimGraph(ResolveAnimBlueprint(Context)))
	{
		TArray<UAnimGraphNode_StateMachineBase*> Nodes;
		RootAnimGraph->GetNodesOfClass<UAnimGraphNode_StateMachineBase>(Nodes);
		for (UAnimGraphNode_StateMachineBase* Node : Nodes)
		{
			Graphs.Add(ExtractStateMachineGraph(Node));
		}
	}

	return MakeShared<FJsonValueObject>(FAssetDocumentGraphParser::WriteCanonicalGraphRegion(Graphs));
}
}

FAssetDocumentAnimStateMachineRegionAdapter::FAssetDocumentAnimStateMachineRegionAdapter(FName InAdapterName)
	: AdapterName(InAdapterName)
{
}

FName FAssetDocumentAnimStateMachineRegionAdapter::GetName() const
{
	return AdapterName;
}

bool FAssetDocumentAnimStateMachineRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return Context.RegionId == StateMachinesRegionId || Context.RegionId == TransitionGraphsRegionId;
}

TSharedRef<FJsonObject> FAssetDocumentAnimStateMachineRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Kind"), TEXT("AnimStateMachineRecursiveGraphRegion"));
	Schema->SetStringField(TEXT("StateMachines"), TEXT("{Graphs:[{Id,Kind:'StateMachine',Nodes,Links,Subgraphs}]}"));
	Schema->SetStringField(TEXT("TransitionGraphs"), TEXT("deprecated empty-only compatibility region; author transition rules as StateMachines subgraphs"));
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentAnimStateMachineRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	if (Context.RegionId == StateMachinesRegionId)
	{
		TArray<FAssetDocumentGraphSpec> Graphs;
		return ParseStateMachinesRegion(DesiredValue, Graphs);
	}
	if (Context.RegionId == TransitionGraphsRegionId)
	{
		return ValidateTransitionGraphsValue(DesiredValue);
	}
	return Failure(Context.JsonPointer, TEXT("UnsupportedAnimStateMachineRegion"), TEXT("Unsupported state-machine region"));
}

FAssetDocumentCapabilityResult FAssetDocumentAnimStateMachineRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;
	if (Context.RegionId == StateMachinesRegionId)
	{
		TArray<FAssetDocumentGraphSpec> Graphs;
		const FAssetDocumentCapabilityResult ParseResult = ParseStateMachinesRegion(DesiredValue, Graphs);
		if (!ParseResult.bSuccess)
		{
			return ParseResult;
		}
		return ApplyStateMachines(Context, Graphs, bOutChanged);
	}
	return ValidateRegion(Context, DesiredValue);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimStateMachineRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	OutCurrentValue = Context.RegionId == StateMachinesRegionId
		? ExtractStateMachinesValue(Context)
		: MakeEmptyArrayValue();
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimBlueprint state-machine identity region"));
}

FAssetDocumentCapabilityResult FAssetDocumentAnimStateMachineRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	const FAssetDocumentCapabilityResult ValidateResult = ValidateRegion(Context, DesiredValue);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	if (Context.RegionId == StateMachinesRegionId)
	{
		TArray<FAssetDocumentGraphSpec> DesiredGraphs;
		const FAssetDocumentCapabilityResult ParseResult = ParseStateMachinesRegion(DesiredValue, DesiredGraphs);
		if (!ParseResult.bSuccess)
		{
			return ParseResult;
		}

		TSharedPtr<FJsonValue> CurrentValue = ExtractStateMachinesValue(Context);
		const TSharedPtr<FJsonObject> CurrentObject = CurrentValue.IsValid() && CurrentValue->Type == EJson::Object
			? CurrentValue->AsObject()
			: nullptr;
		TArray<FAssetDocumentGraphSpec> CurrentGraphs;
		if (CurrentObject.IsValid())
		{
			FAssetDocumentGraphParseOptions Options;
			Options.Path = TEXT("/Body/StateMachines");
			const FAssetDocumentGraphParseResult CurrentParse =
				FAssetDocumentGraphParser::ParseGraphRegion(CurrentObject.ToSharedRef(), Options);
			CurrentGraphs = CurrentParse.Graphs;
		}

		for (const FAssetDocumentGraphDiffEntry& Entry :
			FAssetDocumentGraphDiff::CompareGraphRegion(DesiredGraphs, CurrentGraphs, TEXT("/Body/StateMachines")))
		{
			TSharedPtr<FJsonObject> DiffObject = MakeShared<FJsonObject>();
			DiffObject->SetStringField(TEXT("path"), Entry.Path);
			DiffObject->SetStringField(TEXT("status"), Entry.Status);
			DiffObject->SetField(TEXT("current"), Entry.Current.IsValid() ? Entry.Current : MakeShared<FJsonValueNull>());
			DiffObject->SetField(TEXT("desired"), Entry.Desired.IsValid() ? Entry.Desired : MakeShared<FJsonValueNull>());
			if (!Entry.Message.IsEmpty())
			{
				DiffObject->SetStringField(TEXT("message"), Entry.Message);
			}
			OutDiffEntries.Add(MakeShared<FJsonValueObject>(DiffObject));
		}
	}
	else if (Context.RegionId == TransitionGraphsRegionId)
	{
		AddArrayDiffEntries(DesiredValue, TEXT("TransitionGraphs"), TransitionGraphPath, OutDiffEntries);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed AnimBlueprint state-machine identity region"));
}
