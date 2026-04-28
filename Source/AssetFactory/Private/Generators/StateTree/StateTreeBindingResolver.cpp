// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeBindingResolver.h"

#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeState.h"
#include "StructUtils/PropertyBag.h"

namespace
{
	using namespace UE::AssetFactory::StateTree;

	FString MakeStableNodeId(const FStateTreeEditorNode& Node)
	{
		return Node.ID.IsValid() ? Node.ID.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
	}

	FString MakeGuidKey(const FGuid& Guid)
	{
		return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
	}

	FString MakeTransitionReferenceKey(const FString& Reference)
	{
		FGuid ParsedGuid;
		if (FGuid::Parse(Reference, ParsedGuid))
		{
			return MakeGuidKey(ParsedGuid);
		}
		return MakeGuidKey(FGuid::NewDeterministicGuid(TEXT("AssetFactory.StateTree.Transition.") + Reference));
	}

	bool RegisterNodeIdentity(
		FAFStateTreeBindingIndex& Index,
		const FStateTreeEditorNode& Node,
		const TCHAR* Label,
		FString& OutError)
	{
		if (Node.ID.IsValid())
		{
			if (const FStateTreeEditorNode* const* ExistingNode = Index.NodeByGuid.Find(Node.ID))
			{
				if (*ExistingNode != &Node)
				{
					OutError = FString::Printf(
						TEXT("Duplicate StateTree binding %s node GUID '%s'"),
						Label,
						*Node.ID.ToString(EGuidFormats::DigitsWithHyphensLower));
					return false;
				}
				return true;
			}
			Index.NodeByGuid.Add(Node.ID, &Node);
		}
		return true;
	}

	bool AddNodeReference(
		TMap<FString, const FStateTreeEditorNode*>& ScopedById,
		const FString& StableId,
		const FStateTreeEditorNode& Node,
		const TCHAR* Label,
		FString& OutError)
	{
		if (StableId.IsEmpty())
		{
			return true;
		}

		if (const FStateTreeEditorNode* const* ExistingNode = ScopedById.Find(StableId))
		{
			if (*ExistingNode != &Node)
			{
				OutError = FString::Printf(TEXT("Duplicate StateTree binding %s node reference '%s'"), Label, *StableId);
				return false;
			}
			return true;
		}

		ScopedById.Add(StableId, &Node);
		return true;
	}

	bool RegisterNode(
		FAFStateTreeBindingIndex& Index,
		const FStateTreeEditorNode& Node,
		const TCHAR* Label,
		TMap<FString, const FStateTreeEditorNode*>* ScopedById,
		FString& OutError)
	{
		if (!RegisterNodeIdentity(Index, Node, Label, OutError))
		{
			return false;
		}

		if (ScopedById)
		{
			return AddNodeReference(*ScopedById, MakeStableNodeId(Node), Node, Label, OutError);
		}
		return true;
	}

	bool RegisterGlobalNode(
		FAFStateTreeBindingIndex& Index,
		const FStateTreeEditorNode& Node,
		TMap<FString, const FStateTreeEditorNode*>& TypedById,
		const TCHAR* Label,
		FString& OutError)
	{
		const FString StableId = MakeStableNodeId(Node);
		if (!RegisterNode(Index, Node, Label, &TypedById, OutError))
		{
			return false;
		}
		return AddNodeReference(Index.GlobalNodeById, StableId, Node, TEXT("global"), OutError);
	}

	bool RegisterStateNodeArray(
		FAFStateTreeBindingIndex& Index,
		TArray<FStateTreeEditorNode> const& Nodes,
		TMap<FString, const FStateTreeEditorNode*>& ScopedById,
		TMap<FString, const FStateTreeEditorNode*>& TypedById,
		const TCHAR* Label,
		FString& OutError)
	{
		for (const FStateTreeEditorNode& Node : Nodes)
		{
			if (!RegisterNode(Index, Node, Label, &ScopedById, OutError)
				|| !AddNodeReference(TypedById, MakeStableNodeId(Node), Node, Label, OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool RegisterStateNodes(
		FAFStateTreeBindingIndex& Index,
		const UStateTreeState& State,
		FString& OutError)
	{
		TMap<FString, const FStateTreeEditorNode*>& ScopedById = Index.StateNodeById.FindOrAdd(&State);
		TMap<FString, const FStateTreeEditorNode*>& TaskById = Index.StateTaskById.FindOrAdd(&State);
		TMap<FString, const FStateTreeEditorNode*>& EnterConditionById = Index.StateEnterConditionById.FindOrAdd(&State);
		TMap<FString, const FStateTreeEditorNode*>& ConsiderationById = Index.StateConsiderationById.FindOrAdd(&State);
		if (!RegisterStateNodeArray(Index, State.Tasks, ScopedById, TaskById, TEXT("state task"), OutError)
			|| !RegisterNode(Index, State.SingleTask, TEXT("single task"), &ScopedById, OutError)
			|| !AddNodeReference(TaskById, MakeStableNodeId(State.SingleTask), State.SingleTask, TEXT("single task"), OutError)
			|| !RegisterStateNodeArray(Index, State.EnterConditions, ScopedById, EnterConditionById, TEXT("enter condition"), OutError)
			|| !RegisterStateNodeArray(Index, State.Considerations, ScopedById, ConsiderationById, TEXT("consideration"), OutError))
		{
			return false;
		}

		TMap<FString, TMap<FString, const FStateTreeEditorNode*>>& TransitionConditionByTransitionId = Index.StateTransitionConditionByTransitionId.FindOrAdd(&State);
		for (const FStateTreeTransition& Transition : State.Transitions)
		{
			if (Transition.Conditions.IsEmpty())
			{
				continue;
			}

			const FString TransitionKey = MakeGuidKey(Transition.ID);
			if (TransitionKey.IsEmpty())
			{
				OutError = TEXT("StateTree transition with binding-visible conditions has an invalid GUID");
				return false;
			}
			if (TransitionConditionByTransitionId.Contains(TransitionKey))
			{
				OutError = FString::Printf(TEXT("Duplicate StateTree binding transition GUID '%s'"), *TransitionKey);
				return false;
			}

			TMap<FString, const FStateTreeEditorNode*>& TransitionConditionById = TransitionConditionByTransitionId.Add(TransitionKey);
			if (!RegisterStateNodeArray(Index, Transition.Conditions, ScopedById, TransitionConditionById, TEXT("transition condition"), OutError))
			{
				return false;
			}
		}

		for (const TObjectPtr<UStateTreeState>& Child : State.Children)
		{
			if (Child)
			{
				if (!RegisterStateNodes(Index, *Child, OutError))
				{
					return false;
				}
			}
		}
		return true;
	}

	const FPropertyBagPropertyDesc* FindPropertyBagDesc(
		const FInstancedPropertyBag& Bag,
		const FName Name)
	{
		const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
		if (!BagStruct)
		{
			return nullptr;
		}

		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			if (Desc.Name == Name)
			{
				return &Desc;
			}
		}
		return nullptr;
	}

	bool ApplyFirstPropertyBagSegmentGuid(
		const FInstancedPropertyBag& Bag,
		FPropertyBindingPath& Path,
		FString& OutError)
	{
		if (Path.NumSegments() == 0)
		{
			return true;
		}

		FPropertyBindingPathSegment& FirstSegment = Path.GetMutableSegments()[0];
		const FPropertyBagPropertyDesc* Desc = FindPropertyBagDesc(Bag, FirstSegment.GetName());
		if (!Desc)
		{
			return true;
		}

#if WITH_EDITORONLY_DATA
		const FGuid ExistingGuid = FirstSegment.GetPropertyGuid();
		if (ExistingGuid.IsValid() && ExistingGuid != Desc->ID)
		{
			OutError = FString::Printf(
				TEXT("StateTree binding path segment '%s' GUID '%s' does not match property bag desc GUID '%s'"),
				*FirstSegment.GetName().ToString(),
				*ExistingGuid.ToString(EGuidFormats::DigitsWithHyphensLower),
				*Desc->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
			return false;
		}
		FirstSegment.SetPropertyGuid(Desc->ID);
#endif
		return true;
	}

	bool ValidatePathAgainstStruct(
		FPropertyBindingPath& Path,
		const UStruct* Struct,
		FString& OutError)
	{
		if (!Struct)
		{
			OutError = TEXT("StateTree binding endpoint has no struct/class to validate against");
			return false;
		}

		FString Error;
		if (!Path.UpdateSegments(Struct, &Error))
		{
			OutError = Error.IsEmpty()
				? FString::Printf(TEXT("StateTree binding path '%s' could not be resolved against '%s'"), *Path.ToString(), *Struct->GetPathName())
				: Error;
			return false;
		}
		return true;
	}

	bool ValidatePathAgainstBag(
		const FInstancedPropertyBag& Bag,
		FPropertyBindingPath& Path,
		FString& OutError)
	{
		if (!ApplyFirstPropertyBagSegmentGuid(Bag, Path, OutError))
		{
			return false;
		}
		return ValidatePathAgainstStruct(Path, Bag.GetPropertyBagStruct(), OutError);
	}

	bool ResolveState(
		const FAFStateTreeBindingIndex& Index,
		const FString& Reference,
		UStateTreeState const*& OutState,
		FString& OutError)
	{
		OutState = nullptr;
		if (!Index.StateIndex)
		{
			OutError = TEXT("StateTree binding index has no state index");
			return false;
		}

		UStateTreeState* MutableState = nullptr;
		if (!UE::AssetFactory::StateTree::ResolveStateReference(*Index.StateIndex, Reference, MutableState, OutError))
		{
			return false;
		}
		OutState = MutableState;
		return true;
	}

	const FStateTreeEditorNode* FindNodeByReference(
		const FString& Reference,
		const TMap<FString, const FStateTreeEditorNode*>* ScopedById)
	{
		if (!ScopedById)
		{
			return nullptr;
		}

		FGuid ParsedGuid;
		if (FGuid::Parse(Reference, ParsedGuid))
		{
			if (const FStateTreeEditorNode* const* NodeByGuid = ScopedById->Find(MakeGuidKey(ParsedGuid)))
			{
				return *NodeByGuid;
			}
		}

		if (const FStateTreeEditorNode* const* NodeById = ScopedById->Find(Reference))
		{
			return *NodeById;
		}
		return nullptr;
	}

	bool ResolveTransitionConditionNode(
		const FAFStateTreeBindingIndex& Index,
		const FAFStateTreeBindingEndpointSpec& Endpoint,
		const UStateTreeState& State,
		const FStateTreeEditorNode*& OutNode,
		FString& OutError)
	{
		OutNode = nullptr;

		const TMap<FString, TMap<FString, const FStateTreeEditorNode*>>* ConditionsByTransition = Index.StateTransitionConditionByTransitionId.Find(&State);
		if (!ConditionsByTransition)
		{
			return true;
		}

		if (!Endpoint.Transition.IsEmpty())
		{
			const FString TransitionKey = MakeTransitionReferenceKey(Endpoint.Transition);
			const TMap<FString, const FStateTreeEditorNode*>* Conditions = ConditionsByTransition->Find(TransitionKey);
			if (!Conditions)
			{
				OutError = FString::Printf(
					TEXT("StateTree binding transition '%s' in state '%s' was not found"),
					*Endpoint.Transition,
					*Endpoint.State);
				return false;
			}

			OutNode = FindNodeByReference(Endpoint.Node, Conditions);
			return true;
		}

		TArray<FString> MatchingTransitions;
		for (const TPair<FString, TMap<FString, const FStateTreeEditorNode*>>& Pair : *ConditionsByTransition)
		{
			if (const FStateTreeEditorNode* Candidate = FindNodeByReference(Endpoint.Node, &Pair.Value))
			{
				OutNode = Candidate;
				MatchingTransitions.Add(Pair.Key);
			}
		}

		if (MatchingTransitions.Num() > 1)
		{
			MatchingTransitions.Sort();
			OutError = FString::Printf(
				TEXT("StateTree binding transition condition node '%s' in state '%s' is ambiguous; specify 'transition'. Matches: %s"),
				*Endpoint.Node,
				*Endpoint.State,
				*FString::Join(MatchingTransitions, TEXT(", ")));
			return false;
		}

		return true;
	}

	const UStruct* GetNodeSectionStruct(
		const FStateTreeEditorNode& Node,
		const EAFStateTreeBindingDataSection Section)
	{
		switch (Section)
		{
		case EAFStateTreeBindingDataSection::Node:
			return Node.Node.GetScriptStruct();
		case EAFStateTreeBindingDataSection::Instance:
			return Node.InstanceObject ? static_cast<const UStruct*>(Node.InstanceObject->GetClass()) : static_cast<const UStruct*>(Node.Instance.GetScriptStruct());
		case EAFStateTreeBindingDataSection::ExecutionRuntimeData:
			return Node.ExecutionRuntimeDataObject ? static_cast<const UStruct*>(Node.ExecutionRuntimeDataObject->GetClass()) : static_cast<const UStruct*>(Node.ExecutionRuntimeData.GetScriptStruct());
		default:
			return nullptr;
		}
	}

	bool ResolveParameterEndpoint(
		const FAFStateTreeBindingIndex& Index,
		const FAFStateTreeBindingEndpointSpec& Endpoint,
		FPropertyBindingPath& OutPath,
		FString& OutError)
	{
		if (!Index.EditorData)
		{
			OutError = TEXT("StateTree binding index has no editor data");
			return false;
		}

		const bool bRootParameter = Endpoint.Kind == EAFStateTreeBindingEndpointKind::RootParameter;
		if (bRootParameter && Endpoint.Path.IsEmpty())
		{
			OutError = TEXT("StateTree rootParameter binding endpoint must have a non-empty path");
			return false;
		}

		TArray<FPropertyBindingPathSegment> Segments = MakeBindingPathSegments(Endpoint.Path, OutError);
		if (!OutError.IsEmpty())
		{
			return false;
		}

		const FInstancedPropertyBag* Bag = nullptr;
		FGuid StructID;
		if (bRootParameter)
		{
			Bag = &Index.EditorData->GetRootParametersPropertyBag();
			StructID = Index.EditorData->GetRootParametersGuid();
		}
		else
		{
			if (Endpoint.State.IsEmpty())
			{
				OutError = TEXT("StateTree stateParameter binding endpoint must specify 'state'");
				return false;
			}

			const UStateTreeState* State = nullptr;
			if (!ResolveState(Index, Endpoint.State, State, OutError))
			{
				return false;
			}
			Bag = &State->Parameters.Parameters;
			StructID = State->Parameters.ID;
		}

		OutPath = FPropertyBindingPath(StructID, Segments);
		return ValidatePathAgainstBag(*Bag, OutPath, OutError);
	}

	bool ResolveNodeEndpoint(
		const FAFStateTreeBindingIndex& Index,
		const FAFStateTreeBindingEndpointSpec& Endpoint,
		FPropertyBindingPath& OutPath,
		FString& OutError)
	{
		if (Endpoint.Node.IsEmpty())
		{
			OutError = TEXT("StateTree binding node endpoint must specify 'node'");
			return false;
		}

		const FStateTreeEditorNode* Node = nullptr;
		switch (Endpoint.Kind)
		{
		case EAFStateTreeBindingEndpointKind::Evaluator:
			Node = FindNodeByReference(Endpoint.Node, &Index.EvaluatorById);
			break;
		case EAFStateTreeBindingEndpointKind::GlobalTask:
			Node = FindNodeByReference(Endpoint.Node, &Index.GlobalTaskById);
			break;
		case EAFStateTreeBindingEndpointKind::Node:
			if (!Endpoint.State.IsEmpty())
			{
				const UStateTreeState* State = nullptr;
				if (!ResolveState(Index, Endpoint.State, State, OutError))
				{
					return false;
				}
				Node = FindNodeByReference(Endpoint.Node, Index.StateNodeById.Find(State));
			}
			if (!Node)
			{
				Node = FindNodeByReference(Endpoint.Node, &Index.GlobalNodeById);
			}
			break;
		case EAFStateTreeBindingEndpointKind::Task:
		case EAFStateTreeBindingEndpointKind::EnterCondition:
		case EAFStateTreeBindingEndpointKind::Consideration:
		{
			if (Endpoint.State.IsEmpty())
			{
				OutError = TEXT("StateTree binding node endpoint must specify 'state'");
				return false;
			}

			const UStateTreeState* State = nullptr;
			if (!ResolveState(Index, Endpoint.State, State, OutError))
			{
				return false;
			}
			const TMap<FString, const FStateTreeEditorNode*>* ScopedById = nullptr;
			if (Endpoint.Kind == EAFStateTreeBindingEndpointKind::Task)
			{
				ScopedById = Index.StateTaskById.Find(State);
			}
			else if (Endpoint.Kind == EAFStateTreeBindingEndpointKind::EnterCondition)
			{
				ScopedById = Index.StateEnterConditionById.Find(State);
			}
			else
			{
				ScopedById = Index.StateConsiderationById.Find(State);
			}
			Node = FindNodeByReference(Endpoint.Node, ScopedById);
			break;
		}
		case EAFStateTreeBindingEndpointKind::TransitionCondition:
		{
			if (Endpoint.State.IsEmpty())
			{
				OutError = TEXT("StateTree binding transition condition endpoint must specify 'state'");
				return false;
			}

			const UStateTreeState* State = nullptr;
			if (!ResolveState(Index, Endpoint.State, State, OutError))
			{
				return false;
			}
			if (!ResolveTransitionConditionNode(Index, Endpoint, *State, Node, OutError))
			{
				return false;
			}
			break;
		}
		default:
			OutError = TEXT("Unsupported StateTree binding node endpoint kind");
			return false;
		}

		if (!Node)
		{
			OutError = Endpoint.State.IsEmpty()
				? FString::Printf(TEXT("StateTree binding target node '%s' was not found"), *Endpoint.Node)
				: FString::Printf(TEXT("StateTree binding target node '%s' in state '%s' was not found"), *Endpoint.Node, *Endpoint.State);
			return false;
		}

		const UStruct* Struct = GetNodeSectionStruct(*Node, Endpoint.Section);
		if (!Struct)
		{
			OutError = FString::Printf(TEXT("StateTree binding node '%s' section has no struct/class"), *Endpoint.Node);
			return false;
		}

		TArray<FPropertyBindingPathSegment> Segments = MakeBindingPathSegments(Endpoint.Path, OutError);
		if (!OutError.IsEmpty())
		{
			return false;
		}

		OutPath = FPropertyBindingPath(Node->ID, Segments);
		return ValidatePathAgainstStruct(OutPath, Struct, OutError);
	}

	bool ResolveContextEndpoint(
		const FAFStateTreeBindingIndex& Index,
		const FAFStateTreeBindingEndpointSpec& Endpoint,
		FPropertyBindingPath& OutPath,
		FString& OutError)
	{
		if (!Index.EditorData)
		{
			OutError = TEXT("StateTree binding index has no editor data");
			return false;
		}
		if (Endpoint.Name.IsEmpty() && Endpoint.Class.IsEmpty())
		{
			OutError = TEXT("StateTree context binding endpoint must specify 'name' or 'class'");
			return false;
		}

		FStateTreeBindableStructDesc MatchedDesc;
		TArray<FString> MatchingDescriptions;
		Index.EditorData->VisitAllNodes(
			[&Endpoint, &MatchedDesc, &MatchingDescriptions](const UStateTreeState*, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView)
			{
				if (Desc.DataSource != EStateTreeBindableStructSource::Context || !Desc.Struct)
				{
					return EStateTreeVisitor::Continue;
				}

				const bool bNameMatches = Endpoint.Name.IsEmpty() || Endpoint.Name == Desc.Name.ToString();
				const bool bClassMatches = Endpoint.Class.IsEmpty() || Endpoint.Class == Desc.Struct->GetPathName();
				if (bNameMatches && bClassMatches)
				{
					if (MatchingDescriptions.IsEmpty())
					{
						MatchedDesc = Desc;
					}
					MatchingDescriptions.Add(FString::Printf(TEXT("%s (%s)"), *Desc.Name.ToString(), *Desc.Struct->GetPathName()));
				}
				return EStateTreeVisitor::Continue;
			});

		if (MatchingDescriptions.IsEmpty())
		{
			OutError = Endpoint.Name.IsEmpty()
				? FString::Printf(TEXT("StateTree context binding source class '%s' was not found"), *Endpoint.Class)
				: FString::Printf(TEXT("StateTree context binding source '%s' was not found"), *Endpoint.Name);
			return false;
		}
		if (MatchingDescriptions.Num() > 1)
		{
			MatchingDescriptions.Sort();
			OutError = Endpoint.Name.IsEmpty()
				? FString::Printf(
					TEXT("StateTree context binding source class '%s' is ambiguous; matches: %s"),
					*Endpoint.Class,
					*FString::Join(MatchingDescriptions, TEXT(", ")))
				: FString::Printf(
					TEXT("StateTree context binding source '%s' is ambiguous; matches: %s"),
					*Endpoint.Name,
					*FString::Join(MatchingDescriptions, TEXT(", ")));
			return false;
		}

		TArray<FPropertyBindingPathSegment> Segments = MakeBindingPathSegments(Endpoint.Path, OutError);
		if (!OutError.IsEmpty())
		{
			return false;
		}

		OutPath = FPropertyBindingPath(MatchedDesc.ID, Segments);
		return ValidatePathAgainstStruct(OutPath, MatchedDesc.Struct, OutError);
	}
}

bool UE::AssetFactory::StateTree::BuildBindingIndex(
	const UStateTreeEditorData& EditorData,
	const FAFStateTreeStateIndex& StateIndex,
	FAFStateTreeBindingIndex& OutIndex,
	FString& OutError)
{
	OutError.Reset();
	OutIndex = FAFStateTreeBindingIndex();
	OutIndex.EditorData = &EditorData;
	OutIndex.StateIndex = &StateIndex;

	for (const FStateTreeEditorNode& Evaluator : EditorData.Evaluators)
	{
		if (!RegisterGlobalNode(OutIndex, Evaluator, OutIndex.EvaluatorById, TEXT("evaluator"), OutError))
		{
			return false;
		}
	}
	for (const FStateTreeEditorNode& GlobalTask : EditorData.GlobalTasks)
	{
		if (!RegisterGlobalNode(OutIndex, GlobalTask, OutIndex.GlobalTaskById, TEXT("global task"), OutError))
		{
			return false;
		}
	}
	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData.SubTrees)
	{
		if (SubTree)
		{
			if (!RegisterStateNodes(OutIndex, *SubTree, OutError))
			{
				return false;
			}
		}
	}

	return true;
}

bool UE::AssetFactory::StateTree::ResolveBindingEndpointPath(
	const FAFStateTreeBindingIndex& Index,
	const FAFStateTreeBindingEndpointSpec& Endpoint,
	FPropertyBindingPath& OutPath,
	FString& OutError)
{
	OutError.Reset();
	OutPath.Reset();

	switch (Endpoint.Kind)
	{
	case EAFStateTreeBindingEndpointKind::RootParameter:
	case EAFStateTreeBindingEndpointKind::StateParameter:
		return ResolveParameterEndpoint(Index, Endpoint, OutPath, OutError);
	case EAFStateTreeBindingEndpointKind::Context:
		return ResolveContextEndpoint(Index, Endpoint, OutPath, OutError);
	case EAFStateTreeBindingEndpointKind::Evaluator:
	case EAFStateTreeBindingEndpointKind::GlobalTask:
	case EAFStateTreeBindingEndpointKind::Task:
	case EAFStateTreeBindingEndpointKind::EnterCondition:
	case EAFStateTreeBindingEndpointKind::TransitionCondition:
	case EAFStateTreeBindingEndpointKind::Consideration:
	case EAFStateTreeBindingEndpointKind::Node:
		return ResolveNodeEndpoint(Index, Endpoint, OutPath, OutError);
	case EAFStateTreeBindingEndpointKind::Function:
		OutError = TEXT("StateTree binding function endpoints are not resolved by the endpoint resolver");
		return false;
	default:
		OutError = TEXT("Unsupported StateTree binding endpoint kind");
		return false;
	}
}

TArray<FPropertyBindingPathSegment> UE::AssetFactory::StateTree::MakeBindingPathSegments(
	const TArray<FAFStateTreeBindingPathSegmentSpec>& SegmentSpecs,
	FString& OutError)
{
	OutError.Reset();
	TArray<FPropertyBindingPathSegment> Segments;
	for (const FAFStateTreeBindingPathSegmentSpec& Spec : SegmentSpecs)
	{
		if (Spec.Name.IsEmpty())
		{
			OutError = TEXT("StateTree binding path segment name must be non-empty");
			return {};
		}
		if (!Spec.InstanceStruct.IsEmpty() || !Spec.Access.IsEmpty())
		{
			OutError = FString::Printf(TEXT("StateTree binding path segment '%s' instanceStruct/access is not supported for generation yet"), *Spec.Name);
			return {};
		}

		FPropertyBindingPathSegment Segment(FName(*Spec.Name), Spec.ArrayIndex);
#if WITH_EDITORONLY_DATA
		if (Spec.bHasGuid)
		{
			Segment.SetPropertyGuid(Spec.Guid);
		}
#endif
		Segments.Add(Segment);
	}
	return Segments;
}
