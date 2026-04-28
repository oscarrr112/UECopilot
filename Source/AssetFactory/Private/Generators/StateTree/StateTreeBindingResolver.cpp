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

	static void RegisterNode(
		FAFStateTreeBindingIndex& Index,
		const FStateTreeEditorNode& Node,
		const FString& StableId,
		const UStateTreeState* OwnerState,
		TMap<FString, const FStateTreeEditorNode*>* ScopedById)
	{
		(void)OwnerState;
		if (Node.ID.IsValid())
		{
			Index.NodeByGuid.Add(Node.ID, &Node);
		}
		if (!StableId.IsEmpty() && ScopedById)
		{
			ScopedById->Add(StableId, &Node);
		}
	}

	void RegisterGlobalNode(
		FAFStateTreeBindingIndex& Index,
		const FStateTreeEditorNode& Node,
		TMap<FString, const FStateTreeEditorNode*>& TypedById)
	{
		const FString StableId = MakeStableNodeId(Node);
		RegisterNode(Index, Node, StableId, nullptr, &TypedById);
		if (!StableId.IsEmpty())
		{
			Index.GlobalNodeById.Add(StableId, &Node);
		}
	}

	void RegisterStateNodeArray(
		FAFStateTreeBindingIndex& Index,
		const UStateTreeState& State,
		TArray<FStateTreeEditorNode> const& Nodes,
		TMap<FString, const FStateTreeEditorNode*>& ScopedById)
	{
		for (const FStateTreeEditorNode& Node : Nodes)
		{
			RegisterNode(Index, Node, MakeStableNodeId(Node), &State, &ScopedById);
		}
	}

	void RegisterStateNodes(
		FAFStateTreeBindingIndex& Index,
		const UStateTreeState& State)
	{
		TMap<FString, const FStateTreeEditorNode*>& ScopedById = Index.StateNodeById.FindOrAdd(&State);
		RegisterStateNodeArray(Index, State, State.Tasks, ScopedById);
		RegisterNode(Index, State.SingleTask, MakeStableNodeId(State.SingleTask), &State, &ScopedById);
		RegisterStateNodeArray(Index, State, State.EnterConditions, ScopedById);
		RegisterStateNodeArray(Index, State, State.Considerations, ScopedById);
		for (const FStateTreeTransition& Transition : State.Transitions)
		{
			RegisterStateNodeArray(Index, State, Transition.Conditions, ScopedById);
		}

		for (const TObjectPtr<UStateTreeState>& Child : State.Children)
		{
			if (Child)
			{
				RegisterStateNodes(Index, *Child);
			}
		}
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
		const FAFStateTreeBindingIndex& Index,
		const FString& Reference,
		const TMap<FString, const FStateTreeEditorNode*>* ScopedById)
	{
		FGuid ParsedGuid;
		if (FGuid::Parse(Reference, ParsedGuid))
		{
			if (const FStateTreeEditorNode* const* NodeByGuid = Index.NodeByGuid.Find(ParsedGuid))
			{
				return *NodeByGuid;
			}
		}

		if (ScopedById)
		{
			if (const FStateTreeEditorNode* const* NodeById = ScopedById->Find(Reference))
			{
				return *NodeById;
			}
		}
		return nullptr;
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
			Node = FindNodeByReference(Index, Endpoint.Node, &Index.EvaluatorById);
			break;
		case EAFStateTreeBindingEndpointKind::GlobalTask:
			Node = FindNodeByReference(Index, Endpoint.Node, &Index.GlobalTaskById);
			break;
		case EAFStateTreeBindingEndpointKind::Node:
			if (!Endpoint.State.IsEmpty())
			{
				const UStateTreeState* State = nullptr;
				if (!ResolveState(Index, Endpoint.State, State, OutError))
				{
					return false;
				}
				Node = FindNodeByReference(Index, Endpoint.Node, Index.StateNodeById.Find(State));
			}
			if (!Node)
			{
				Node = FindNodeByReference(Index, Endpoint.Node, &Index.GlobalNodeById);
			}
			break;
		case EAFStateTreeBindingEndpointKind::Task:
		case EAFStateTreeBindingEndpointKind::EnterCondition:
		case EAFStateTreeBindingEndpointKind::TransitionCondition:
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
			Node = FindNodeByReference(Index, Endpoint.Node, Index.StateNodeById.Find(State));
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
		bool bFoundMatch = false;
		Index.EditorData->VisitAllNodes(
			[&Endpoint, &MatchedDesc, &bFoundMatch](const UStateTreeState*, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView)
			{
				if (Desc.DataSource != EStateTreeBindableStructSource::Context || !Desc.Struct)
				{
					return EStateTreeVisitor::Continue;
				}

				const bool bNameMatches = Endpoint.Name.IsEmpty() || Endpoint.Name == Desc.Name.ToString();
				const bool bClassMatches = Endpoint.Class.IsEmpty() || Endpoint.Class == Desc.Struct->GetPathName();
				if (bNameMatches && bClassMatches)
				{
					MatchedDesc = Desc;
					bFoundMatch = true;
					return EStateTreeVisitor::Break;
				}
				return EStateTreeVisitor::Continue;
			});

		if (!bFoundMatch)
		{
			OutError = Endpoint.Name.IsEmpty()
				? FString::Printf(TEXT("StateTree context binding source class '%s' was not found"), *Endpoint.Class)
				: FString::Printf(TEXT("StateTree context binding source '%s' was not found"), *Endpoint.Name);
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
		RegisterGlobalNode(OutIndex, Evaluator, OutIndex.EvaluatorById);
	}
	for (const FStateTreeEditorNode& GlobalTask : EditorData.GlobalTasks)
	{
		RegisterGlobalNode(OutIndex, GlobalTask, OutIndex.GlobalTaskById);
	}
	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData.SubTrees)
	{
		if (SubTree)
		{
			RegisterStateNodes(OutIndex, *SubTree);
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
