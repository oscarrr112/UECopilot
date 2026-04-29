// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeBindingExtract.h"

#include "Dom/JsonObject.h"
#include "PropertyBindingBinding.h"
#include "PropertyBindingPath.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeState.h"

namespace
{
	struct FNodeEndpointReference
	{
		FString StatePath;
	};

	struct FContextEndpointReference
	{
		FString Name;
		FString ClassPath;
	};

	struct FBindingExtractContext
	{
		const UStateTreeEditorData* EditorData = nullptr;
		TMap<FGuid, FString> StatePathByParameterId;
		TMap<FGuid, FNodeEndpointReference> NodeById;
		TMap<FGuid, FContextEndpointReference> ContextById;
		TMultiMap<FGuid, const FPropertyBindingBinding*> BindingsByTargetStructId;
		TSet<FGuid> PropertyFunctionNodeIds;
		TArray<UE::AssetFactory::StateTree::FAFStateTreeBindingDiagnostic> Diagnostics;
	};

	FString GuidToString(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	FString MakeBindingTargetKey(const FPropertyBindingPath& TargetPath)
	{
		return FString::Printf(
			TEXT("%s:%s"),
			*GuidToString(TargetPath.GetStructID()),
			*TargetPath.ToString());
	}

	void AddBindingDiagnostic(
		FBindingExtractContext& Context,
		const FString& Code,
		const FString& JsonPath,
		const FPropertyBindingPath& BindingTarget,
		const FString& Message)
	{
		UE::AssetFactory::StateTree::FAFStateTreeBindingDiagnostic Diagnostic;
		Diagnostic.Code = Code;
		Diagnostic.Severity = TEXT("warning");
		Diagnostic.Path = JsonPath;
		Diagnostic.BindingTarget = MakeBindingTargetKey(BindingTarget);
		Diagnostic.Message = Message;
		Context.Diagnostics.Add(MoveTemp(Diagnostic));
	}

	FString BindingStatePath(const FString& ParentPath, const UStateTreeState& State)
	{
		const FString Name = State.Name.ToString();
		return ParentPath.IsEmpty() ? Name : ParentPath + TEXT("/") + Name;
	}

	void AddNodeReferences(
		const TArray<FStateTreeEditorNode>& Nodes,
		const FString& StatePath,
		FBindingExtractContext& Context)
	{
		for (const FStateTreeEditorNode& Node : Nodes)
		{
			if (Node.ID.IsValid())
			{
				Context.NodeById.Add(Node.ID, FNodeEndpointReference{StatePath});
			}
		}
	}

	void AddNodeReference(
		const FStateTreeEditorNode& Node,
		const FString& StatePath,
		FBindingExtractContext& Context)
	{
		if (Node.ID.IsValid())
		{
			Context.NodeById.Add(Node.ID, FNodeEndpointReference{StatePath});
		}
	}

	void CollectStateReferences(
		const UStateTreeState& State,
		const FString& ParentPath,
		FBindingExtractContext& Context)
	{
		const FString CurrentPath = BindingStatePath(ParentPath, State);
		if (State.Parameters.ID.IsValid())
		{
			Context.StatePathByParameterId.Add(State.Parameters.ID, CurrentPath);
		}

		AddNodeReferences(State.Tasks, CurrentPath, Context);
		AddNodeReference(State.SingleTask, CurrentPath, Context);
		AddNodeReferences(State.EnterConditions, CurrentPath, Context);
		AddNodeReferences(State.Considerations, CurrentPath, Context);
		for (const FStateTreeTransition& Transition : State.Transitions)
		{
			AddNodeReferences(Transition.Conditions, CurrentPath, Context);
		}

		for (const TObjectPtr<UStateTreeState>& Child : State.Children)
		{
			if (Child)
			{
				CollectStateReferences(*Child, CurrentPath, Context);
			}
		}
	}

	FBindingExtractContext BuildExtractContext(const UStateTreeEditorData& EditorData)
	{
		FBindingExtractContext Context;
		Context.EditorData = &EditorData;

		AddNodeReferences(EditorData.Evaluators, FString(), Context);
		AddNodeReferences(EditorData.GlobalTasks, FString(), Context);
		for (const TObjectPtr<UStateTreeState>& SubTree : EditorData.SubTrees)
		{
			if (SubTree)
			{
				CollectStateReferences(*SubTree, FString(), Context);
			}
		}

		EditorData.VisitAllNodes(
			[&Context](const UStateTreeState*, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView)
			{
				if (Desc.DataSource == EStateTreeBindableStructSource::Context
					&& Desc.ID.IsValid()
					&& Desc.Struct)
				{
					Context.ContextById.Add(Desc.ID, FContextEndpointReference{Desc.Name.ToString(), Desc.Struct->GetPathName()});
				}
				return EStateTreeVisitor::Continue;
			});

		return Context;
	}

	TSharedPtr<FJsonValue> ExtractPathSegment(const FPropertyBindingPathSegment& Segment)
	{
		const FString Name = Segment.GetName().ToString();
		const int32 ArrayIndex = Segment.GetArrayIndex();
		const UStruct* InstanceStruct = Segment.GetInstanceStruct();
		const EPropertyBindingPropertyAccessType AccessType = Segment.GetInstancedStructAccessType();
#if WITH_EDITORONLY_DATA
		const FGuid PropertyGuid = Segment.GetPropertyGuid();
#endif

		if (ArrayIndex == INDEX_NONE
			&& InstanceStruct == nullptr
			&& AccessType == EPropertyBindingPropertyAccessType::Unset
#if WITH_EDITORONLY_DATA
			&& !PropertyGuid.IsValid()
#endif
			)
		{
			return MakeShared<FJsonValueString>(Name);
		}

		TSharedPtr<FJsonObject> SegmentJson = MakeShared<FJsonObject>();
		SegmentJson->SetStringField(TEXT("name"), Name);
		if (ArrayIndex != INDEX_NONE)
		{
			SegmentJson->SetNumberField(TEXT("arrayIndex"), ArrayIndex);
		}
#if WITH_EDITORONLY_DATA
		if (PropertyGuid.IsValid())
		{
			SegmentJson->SetStringField(TEXT("guid"), GuidToString(PropertyGuid));
		}
#endif
		if (InstanceStruct)
		{
			SegmentJson->SetStringField(TEXT("instanceStruct"), InstanceStruct->GetPathName());
		}
		if (AccessType != EPropertyBindingPropertyAccessType::Unset)
		{
			if (const UEnum* AccessEnum = StaticEnum<EPropertyBindingPropertyAccessType>())
			{
				SegmentJson->SetStringField(TEXT("access"), AccessEnum->GetNameStringByValue(static_cast<int64>(AccessType)));
			}
		}
		return MakeShared<FJsonValueObject>(SegmentJson);
	}

	TArray<TSharedPtr<FJsonValue>> ExtractPathSegments(const FPropertyBindingPath& Path)
	{
		TArray<TSharedPtr<FJsonValue>> SegmentValues;
		for (const FPropertyBindingPathSegment& Segment : Path.GetSegments())
		{
			SegmentValues.Add(ExtractPathSegment(Segment));
		}
		return SegmentValues;
	}

	TSharedPtr<FJsonObject> ExtractEndpoint(const FBindingExtractContext& Context, const FPropertyBindingPath& Path)
	{
		TSharedPtr<FJsonObject> EndpointJson = MakeShared<FJsonObject>();
		const FGuid StructID = Path.GetStructID();
		if (Context.EditorData && StructID == Context.EditorData->GetRootParametersGuid())
		{
			EndpointJson->SetStringField(TEXT("kind"), TEXT("rootParameter"));
		}
		else if (const FString* StatePath = Context.StatePathByParameterId.Find(StructID))
		{
			EndpointJson->SetStringField(TEXT("kind"), TEXT("stateParameter"));
			EndpointJson->SetStringField(TEXT("state"), *StatePath);
		}
		else if (const FContextEndpointReference* ContextRef = Context.ContextById.Find(StructID))
		{
			EndpointJson->SetStringField(TEXT("kind"), TEXT("context"));
			if (!ContextRef->Name.IsEmpty())
			{
				EndpointJson->SetStringField(TEXT("name"), ContextRef->Name);
			}
			if (!ContextRef->ClassPath.IsEmpty())
			{
				EndpointJson->SetStringField(TEXT("class"), ContextRef->ClassPath);
			}
		}
		else
		{
			EndpointJson->SetStringField(TEXT("kind"), TEXT("node"));
			EndpointJson->SetStringField(TEXT("node"), GuidToString(StructID));
			if (const FNodeEndpointReference* NodeRef = Context.NodeById.Find(StructID))
			{
				if (!NodeRef->StatePath.IsEmpty())
				{
					EndpointJson->SetStringField(TEXT("state"), NodeRef->StatePath);
				}
			}
			EndpointJson->SetStringField(TEXT("section"), TEXT("instance"));
		}
		EndpointJson->SetArrayField(TEXT("path"), ExtractPathSegments(Path));
		return EndpointJson;
	}

	const FStateTreeEditorNode* GetPropertyFunctionEditorNode(const FPropertyBindingBinding& Binding)
	{
#if WITH_EDITOR
		if (Binding.GetPropertyFunctionNode().IsValid())
		{
			return Binding.GetPropertyFunctionNode().GetPtr<const FStateTreeEditorNode>();
		}
#endif
		return nullptr;
	}

	TSharedPtr<FJsonObject> ExtractFunctionSpec(
		FBindingExtractContext& Context,
		const FPropertyBindingBinding& Binding,
		TSet<FGuid>& VisitedFunctionIds,
		const FString& JsonPath);

	TSharedPtr<FJsonObject> ExtractFunctionInputSpec(
		FBindingExtractContext& Context,
		const FPropertyBindingBinding& Binding,
		TSet<FGuid>& VisitedFunctionIds,
		const FString& JsonPath)
	{
		TSharedPtr<FJsonObject> InputJson = MakeShared<FJsonObject>();
		InputJson->SetArrayField(TEXT("target"), ExtractPathSegments(Binding.GetTargetPath()));
		if (GetPropertyFunctionEditorNode(Binding))
		{
			InputJson->SetObjectField(TEXT("function"), ExtractFunctionSpec(Context, Binding, VisitedFunctionIds, JsonPath + TEXT(".function")));
		}
		else
		{
			InputJson->SetObjectField(TEXT("source"), ExtractEndpoint(Context, Binding.GetSourcePath()));
		}
		return InputJson;
	}

	TSharedPtr<FJsonObject> ExtractFunctionSpec(
		FBindingExtractContext& Context,
		const FPropertyBindingBinding& Binding,
		TSet<FGuid>& VisitedFunctionIds,
		const FString& JsonPath)
	{
		TSharedPtr<FJsonObject> FunctionJson = MakeShared<FJsonObject>();
		const FStateTreeEditorNode* EditorNode = GetPropertyFunctionEditorNode(Binding);
		if (!EditorNode || !EditorNode->Node.IsValid())
		{
			FunctionJson->SetStringField(TEXT("type"), TEXT(""));
			FunctionJson->SetArrayField(TEXT("output"), ExtractPathSegments(Binding.GetSourcePath()));
			FunctionJson->SetArrayField(TEXT("inputs"), TArray<TSharedPtr<FJsonValue>>{});
			return FunctionJson;
		}

		if (const UScriptStruct* FunctionStruct = EditorNode->Node.GetScriptStruct())
		{
			FunctionJson->SetStringField(TEXT("type"), FunctionStruct->GetPathName());
		}
		FunctionJson->SetArrayField(TEXT("output"), ExtractPathSegments(Binding.GetSourcePath()));

		TArray<TSharedPtr<FJsonValue>> InputsJson;
		const FGuid FunctionNodeId = EditorNode->ID.IsValid() ? EditorNode->ID : Binding.GetSourcePath().GetStructID();
		if (VisitedFunctionIds.Contains(FunctionNodeId))
		{
			AddBindingDiagnostic(
				Context,
				TEXT("StateTree.Binding.FunctionCycle"),
				JsonPath,
				Binding.GetTargetPath(),
				TEXT("Skipped nested StateTree property function input because the graph references an already visited function node."));
			FunctionJson->SetArrayField(TEXT("inputs"), InputsJson);
			return FunctionJson;
		}

		VisitedFunctionIds.Add(FunctionNodeId);

		TArray<const FPropertyBindingBinding*> InputBindings;
		Context.BindingsByTargetStructId.MultiFind(FunctionNodeId, InputBindings);
		InputBindings.Sort([](const FPropertyBindingBinding& A, const FPropertyBindingBinding& B)
		{
			return A.GetTargetPath().ToString() < B.GetTargetPath().ToString();
		});

		for (const FPropertyBindingBinding* InputBinding : InputBindings)
		{
			if (!InputBinding)
			{
				continue;
			}
			const int32 InputIndex = InputsJson.Num();
			TSharedPtr<FJsonObject> InputJson = ExtractFunctionInputSpec(
				Context,
				*InputBinding,
				VisitedFunctionIds,
				FString::Printf(TEXT("%s.inputs[%d]"), *JsonPath, InputIndex));
			InputsJson.Add(MakeShared<FJsonValueObject>(InputJson));
		}

		VisitedFunctionIds.Remove(FunctionNodeId);

		FunctionJson->SetArrayField(TEXT("inputs"), InputsJson);
		return FunctionJson;
	}

	TSharedPtr<FJsonObject> ExtractBinding(FBindingExtractContext& Context, const FPropertyBindingBinding& Binding, const FString& JsonPath)
	{
		const FGuid TargetStructId = Binding.GetTargetPath().GetStructID();
		if (Context.PropertyFunctionNodeIds.Contains(TargetStructId))
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> BindingJson = MakeShared<FJsonObject>();
		if (GetPropertyFunctionEditorNode(Binding))
		{
			TSet<FGuid> VisitedFunctionIds;
			BindingJson->SetObjectField(TEXT("function"), ExtractFunctionSpec(Context, Binding, VisitedFunctionIds, JsonPath + TEXT(".function")));
		}
		else
		{
			BindingJson->SetObjectField(TEXT("source"), ExtractEndpoint(Context, Binding.GetSourcePath()));
		}
		BindingJson->SetObjectField(TEXT("target"), ExtractEndpoint(Context, Binding.GetTargetPath()));
		return BindingJson;
	}
}

UE::AssetFactory::StateTree::FAFStateTreeBindingExtractionResult UE::AssetFactory::StateTree::ExtractPropertyBindings(const UStateTreeEditorData* EditorData)
{
	FAFStateTreeBindingExtractionResult Result;
	if (!EditorData)
	{
		return Result;
	}

	const FStateTreeEditorPropertyBindings* EditorBindings = EditorData->GetPropertyEditorBindings();
	if (!EditorBindings)
	{
		return Result;
	}

	const FBindingExtractContext Context = BuildExtractContext(*EditorData);
	FBindingExtractContext ContextWithBindings = Context;
	EditorBindings->ForEachBinding([&ContextWithBindings](const FPropertyBindingBinding& Binding)
	{
		ContextWithBindings.BindingsByTargetStructId.Add(Binding.GetTargetPath().GetStructID(), &Binding);
		if (const FStateTreeEditorNode* EditorNode = GetPropertyFunctionEditorNode(Binding))
		{
			const FGuid FunctionNodeId = EditorNode->ID.IsValid() ? EditorNode->ID : Binding.GetSourcePath().GetStructID();
			ContextWithBindings.PropertyFunctionNodeIds.Add(FunctionNodeId);
		}
	});

	TArray<const FPropertyBindingBinding*> TopLevelBindings;
	EditorBindings->ForEachBinding([&ContextWithBindings, &TopLevelBindings](const FPropertyBindingBinding& Binding)
	{
		if (!ContextWithBindings.PropertyFunctionNodeIds.Contains(Binding.GetTargetPath().GetStructID()))
		{
			TopLevelBindings.Add(&Binding);
		}
	});
	TopLevelBindings.Sort([](const FPropertyBindingBinding& A, const FPropertyBindingBinding& B)
	{
		return MakeBindingTargetKey(A.GetTargetPath()) < MakeBindingTargetKey(B.GetTargetPath());
	});

	for (const FPropertyBindingBinding* Binding : TopLevelBindings)
	{
		if (!Binding)
		{
			continue;
		}
		const int32 BindingIndex = Result.Bindings.Num();
		TSharedPtr<FJsonObject> BindingJson = ExtractBinding(
			ContextWithBindings,
			*Binding,
			FString::Printf(TEXT("$.bindings[%d]"), BindingIndex));
		if (BindingJson.IsValid())
		{
			Result.Bindings.Add(MakeShared<FJsonValueObject>(BindingJson));
		}
	}

	Result.Diagnostics = MoveTemp(ContextWithBindings.Diagnostics);
	return Result;
}
