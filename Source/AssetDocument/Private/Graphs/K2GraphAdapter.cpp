// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/K2GraphAdapter.h"

#include "Graphs/K2NodeAdapters/CallFunctionNodeAdapter.h"
#include "Graphs/K2NodeAdapters/EventNodeAdapter.h"
#include "Graphs/K2NodeAdapters/SelfNodeAdapter.h"
#include "Graphs/K2NodeAdapters/VariableGetSetNodeAdapter.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

namespace
{
FString GetClassPath(const UClass* Class)
{
	return Class ? Class->GetPathName() : FString();
}

FString SanitizeSidecarId(const FString& Value, const FString& Fallback)
{
	FString Result = Value.IsEmpty() ? Fallback : Value;
	for (int32 Index = 0; Index < Result.Len(); ++Index)
	{
		const TCHAR Character = Result[Index];
		if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-'))
		{
			Result[Index] = TEXT('_');
		}
	}

	if (Result.IsEmpty() || (!FChar::IsAlpha(Result[0]) && Result[0] != TEXT('_')))
	{
		Result = FString(TEXT("Node_")) + Result;
	}
	return Result;
}

FString MakeUniqueId(const FString& BaseId, TSet<FString>& UsedIds)
{
	FString Candidate = SanitizeSidecarId(BaseId, TEXT("Node"));
	if (!UsedIds.Contains(Candidate))
	{
		UsedIds.Add(Candidate);
		return Candidate;
	}

	for (int32 Suffix = 2;; ++Suffix)
	{
		const FString Suffixed = FString::Printf(TEXT("%s_%d"), *Candidate, Suffix);
		if (!UsedIds.Contains(Suffixed))
		{
			UsedIds.Add(Suffixed);
			return Suffixed;
		}
	}
}

FString MakeNodeBaseId(const UEdGraphNode* Node)
{
	if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		return EventNode->GetFunctionName().ToString();
	}
	if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		return CallNode->GetFunctionName().ToString();
	}
	if (const UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
	{
		return FString::Printf(TEXT("Get_%s"), *VariableGet->GetVarNameString());
	}
	if (const UK2Node_VariableSet* VariableSet = Cast<UK2Node_VariableSet>(Node))
	{
		return FString::Printf(TEXT("Set_%s"), *VariableSet->GetVarNameString());
	}
	if (Node && Node->IsA<UK2Node_Self>())
	{
		return TEXT("Self");
	}
	return Node ? Node->GetName() : FString(TEXT("Node"));
}

TSharedRef<FJsonObject> MakePositionObject(const UEdGraphNode* Node)
{
	TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), Node ? Node->NodePosX : 0);
	Position->SetNumberField(TEXT("Y"), Node ? Node->NodePosY : 0);
	return Position;
}

TSharedRef<FJsonObject> MakeSkippedNodeObject(const UEdGraph* Graph, const UEdGraphNode* Node)
{
	TSharedRef<FJsonObject> Skipped = MakeShared<FJsonObject>();
	Skipped->SetStringField(TEXT("Reason"), TEXT("UnsupportedGraphNodeClass"));
	Skipped->SetStringField(TEXT("Graph"), Graph ? Graph->GetName() : FString());
	Skipped->SetStringField(TEXT("Class"), Node ? GetClassPath(Node->GetClass()) : FString());
	Skipped->SetStringField(TEXT("NodeTitle"), Node ? Node->GetNodeTitle(ENodeTitleType::ListView).ToString() : FString());
	Skipped->SetStringField(TEXT("NodeGuid"), Node ? Node->NodeGuid.ToString(EGuidFormats::Digits) : FString());
	return Skipped;
}

bool IsSupportedPinName(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return false;
	}
	const FString PinName = Pin->PinName.ToString();
	return !PinName.IsEmpty() && SanitizeSidecarId(PinName, TEXT("")) == PinName;
}

void AddSupportedLinks(
	const TArray<UEdGraphNode*>& GraphNodes,
	const TMap<const UEdGraphNode*, FString>& NodeIds,
	FAssetDocumentGraphSpec& GraphSpec)
{
	TSet<FString> LinkKeys;
	for (UEdGraphNode* Node : GraphNodes)
	{
		const FString* FromNodeId = NodeIds.Find(Node);
		if (!FromNodeId)
		{
			continue;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || !IsSupportedPinName(Pin))
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				const FString* ToNodeId = NodeIds.Find(LinkedNode);
				if (!ToNodeId || !IsSupportedPinName(LinkedPin))
				{
					continue;
				}

				FAssetDocumentLinkSpec Link;
				Link.From.Node = *FromNodeId;
				Link.From.Pin = Pin->PinName.ToString();
				Link.To.Node = *ToNodeId;
				Link.To.Pin = LinkedPin->PinName.ToString();
				if (!LinkKeys.Contains(Link.ToKey()))
				{
					LinkKeys.Add(Link.ToKey());
					GraphSpec.Links.Add(MoveTemp(Link));
				}
			}
		}
	}
}
}

FAssetDocumentNodeAdapterRegistry FAssetDocumentK2GraphAdapter::CreateTier1NodeAdapterRegistry()
{
	FAssetDocumentNodeAdapterRegistry Registry;
	Registry.RegisterAdapter(TEXT("/Script/BlueprintGraph.K2Node_Event"), MakeShared<FAssetDocumentK2EventNodeAdapter>());
	Registry.RegisterAdapter(TEXT("/Script/BlueprintGraph.K2Node_CallFunction"), MakeShared<FAssetDocumentK2CallFunctionNodeAdapter>());
	Registry.RegisterAdapter(TEXT("/Script/BlueprintGraph.K2Node_VariableGet"), MakeShared<FAssetDocumentK2VariableGetNodeAdapter>());
	Registry.RegisterAdapter(TEXT("/Script/BlueprintGraph.K2Node_VariableSet"), MakeShared<FAssetDocumentK2VariableSetNodeAdapter>());
	Registry.RegisterAdapter(TEXT("/Script/BlueprintGraph.K2Node_Self"), MakeShared<FAssetDocumentK2SelfNodeAdapter>());
	return Registry;
}

FAssetDocumentK2GraphExtractResult FAssetDocumentK2GraphAdapter::ExtractUbergraphPages(const UBlueprint* Blueprint) const
{
	FAssetDocumentK2GraphExtractResult Result;
	if (!Blueprint)
	{
		return Result;
	}

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph)
		{
			continue;
		}

		FAssetDocumentGraphSpec GraphSpec;
		GraphSpec.Name = Graph->GetName();
		GraphSpec.Schema = Graph->Schema ? Graph->Schema->GetPathName() : FString(TEXT("/Script/BlueprintGraph.EdGraphSchema_K2"));
		GraphSpec.GraphGuid = Graph->GraphGuid.ToString(EGuidFormats::Digits);

		TSet<FString> UsedIds;
		TMap<const UEdGraphNode*, FString> NodeIds;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			FAssetDocumentNodeSpec NodeSpec;
			if (!ExtractNode(Blueprint, Node, NodeSpec))
			{
				Result.SkippedNodes.Add(MakeShared<FJsonValueObject>(MakeSkippedNodeObject(Graph, Node)));
				continue;
			}

			NodeSpec.Id = MakeUniqueId(MakeNodeBaseId(Node), UsedIds);
			NodeSpec.Class = GetClassPath(Node->GetClass());
			NodeSpec.NodeGuid = Node->NodeGuid.ToString(EGuidFormats::Digits);
			NodeSpec.Position = MakePositionObject(Node);
			NodeIds.Add(Node, NodeSpec.Id);
			GraphSpec.Nodes.Add(MoveTemp(NodeSpec));
		}

		AddSupportedLinks(Graph->Nodes, NodeIds, GraphSpec);
		if (!GraphSpec.Nodes.IsEmpty() || !GraphSpec.Links.IsEmpty())
		{
			Result.Graphs.Add(MoveTemp(GraphSpec));
		}
	}

	return Result;
}

bool FAssetDocumentK2GraphAdapter::ExtractNode(const UBlueprint* Blueprint, const UEdGraphNode* Node, FAssetDocumentNodeSpec& OutNode) const
{
	if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		return FAssetDocumentK2EventNodeAdapter().ExtractNode(Blueprint, EventNode, OutNode);
	}
	if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		return FAssetDocumentK2CallFunctionNodeAdapter().ExtractNode(Blueprint, CallNode, OutNode);
	}
	if (const UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
	{
		return FAssetDocumentK2VariableGetNodeAdapter().ExtractNode(Blueprint, VariableGet, OutNode);
	}
	if (const UK2Node_VariableSet* VariableSet = Cast<UK2Node_VariableSet>(Node))
	{
		return FAssetDocumentK2VariableSetNodeAdapter().ExtractNode(Blueprint, VariableSet, OutNode);
	}
	if (const UK2Node_Self* SelfNode = Cast<UK2Node_Self>(Node))
	{
		return FAssetDocumentK2SelfNodeAdapter().ExtractNode(Blueprint, SelfNode, OutNode);
	}
	return false;
}
