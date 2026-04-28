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
	};

	FString GuidToString(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	FString StatePath(const FString& ParentPath, const UStateTreeState& State)
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
		const FString CurrentPath = StatePath(ParentPath, State);
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
#if WITH_EDITORONLY_DATA
		const FGuid PropertyGuid = Segment.GetPropertyGuid();
#endif

		if (ArrayIndex == INDEX_NONE
			&& InstanceStruct == nullptr
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

	FString ExtractFunctionInputName(const FPropertyBindingPath& InputPath)
	{
		if (InputPath.NumSegments() == 1)
		{
			return InputPath.GetSegment(0).GetName().ToString();
		}
		return InputPath.ToString();
	}

	TSharedPtr<FJsonObject> ExtractFunctionSpec(
		const FBindingExtractContext& Context,
		const FPropertyBindingBinding& Binding,
		TSet<FGuid>& VisitedFunctionIds);

	TSharedPtr<FJsonObject> ExtractFunctionInputSpec(
		const FBindingExtractContext& Context,
		const FPropertyBindingBinding& Binding,
		TSet<FGuid>& VisitedFunctionIds)
	{
		TSharedPtr<FJsonObject> InputJson = MakeShared<FJsonObject>();
		if (GetPropertyFunctionEditorNode(Binding))
		{
			InputJson->SetObjectField(TEXT("function"), ExtractFunctionSpec(Context, Binding, VisitedFunctionIds));
		}
		else
		{
			InputJson->SetObjectField(TEXT("source"), ExtractEndpoint(Context, Binding.GetSourcePath()));
		}
		return InputJson;
	}

	TSharedPtr<FJsonObject> ExtractFunctionSpec(
		const FBindingExtractContext& Context,
		const FPropertyBindingBinding& Binding,
		TSet<FGuid>& VisitedFunctionIds)
	{
		TSharedPtr<FJsonObject> FunctionJson = MakeShared<FJsonObject>();
		const FStateTreeEditorNode* EditorNode = GetPropertyFunctionEditorNode(Binding);
		if (!EditorNode || !EditorNode->Node.IsValid())
		{
			FunctionJson->SetStringField(TEXT("type"), TEXT(""));
			FunctionJson->SetArrayField(TEXT("output"), ExtractPathSegments(Binding.GetSourcePath()));
			FunctionJson->SetObjectField(TEXT("inputs"), MakeShared<FJsonObject>());
			return FunctionJson;
		}

		if (const UScriptStruct* FunctionStruct = EditorNode->Node.GetScriptStruct())
		{
			FunctionJson->SetStringField(TEXT("type"), FunctionStruct->GetPathName());
		}
		FunctionJson->SetArrayField(TEXT("output"), ExtractPathSegments(Binding.GetSourcePath()));

		TSharedPtr<FJsonObject> InputsJson = MakeShared<FJsonObject>();
		const FGuid FunctionNodeId = EditorNode->ID.IsValid() ? EditorNode->ID : Binding.GetSourcePath().GetStructID();
		if (!VisitedFunctionIds.Contains(FunctionNodeId))
		{
			VisitedFunctionIds.Add(FunctionNodeId);

			TArray<const FPropertyBindingBinding*> InputBindings;
			Context.BindingsByTargetStructId.MultiFind(FunctionNodeId, InputBindings);
			InputBindings.Sort([](const FPropertyBindingBinding* A, const FPropertyBindingBinding* B)
			{
				return A && B ? A->GetTargetPath().ToString() < B->GetTargetPath().ToString() : A != nullptr;
			});

			for (const FPropertyBindingBinding* InputBinding : InputBindings)
			{
				if (!InputBinding)
				{
					continue;
				}
				const FString InputName = ExtractFunctionInputName(InputBinding->GetTargetPath());
				InputsJson->SetObjectField(InputName, ExtractFunctionInputSpec(Context, *InputBinding, VisitedFunctionIds));
			}

			VisitedFunctionIds.Remove(FunctionNodeId);
		}

		FunctionJson->SetObjectField(TEXT("inputs"), InputsJson);
		return FunctionJson;
	}

	TSharedPtr<FJsonObject> ExtractBinding(const FBindingExtractContext& Context, const FPropertyBindingBinding& Binding)
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
			BindingJson->SetObjectField(TEXT("function"), ExtractFunctionSpec(Context, Binding, VisitedFunctionIds));
		}
		else
		{
			BindingJson->SetObjectField(TEXT("source"), ExtractEndpoint(Context, Binding.GetSourcePath()));
		}
		BindingJson->SetObjectField(TEXT("target"), ExtractEndpoint(Context, Binding.GetTargetPath()));
		return BindingJson;
	}
}

TArray<TSharedPtr<FJsonValue>> UE::AssetFactory::StateTree::ExtractPropertyBindings(const UStateTreeEditorData* EditorData)
{
	TArray<TSharedPtr<FJsonValue>> Result;
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

	EditorBindings->ForEachBinding([&ContextWithBindings, &Result](const FPropertyBindingBinding& Binding)
	{
		TSharedPtr<FJsonObject> BindingJson = ExtractBinding(ContextWithBindings, Binding);
		if (BindingJson.IsValid())
		{
			Result.Add(MakeShared<FJsonValueObject>(BindingJson));
		}
	});

	return Result;
}
