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

	TSharedPtr<FJsonObject> ExtractBinding(const FBindingExtractContext& Context, const FPropertyBindingBinding& Binding)
	{
#if WITH_EDITOR
		if (Binding.GetPropertyFunctionNode().IsValid())
		{
			return nullptr;
		}
#endif

		TSharedPtr<FJsonObject> BindingJson = MakeShared<FJsonObject>();
		BindingJson->SetObjectField(TEXT("source"), ExtractEndpoint(Context, Binding.GetSourcePath()));
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
	EditorBindings->ForEachBinding([&Context, &Result](const FPropertyBindingBinding& Binding)
	{
		TSharedPtr<FJsonObject> BindingJson = ExtractBinding(Context, Binding);
		if (BindingJson.IsValid())
		{
			Result.Add(MakeShared<FJsonValueObject>(BindingJson));
		}
	});

	return Result;
}
