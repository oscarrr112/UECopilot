// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/K2GraphAdapter.h"

#include "Graphs/K2NodeAdapters/CallFunctionNodeAdapter.h"
#include "Graphs/K2NodeAdapters/EventNodeAdapter.h"
#include "Graphs/K2NodeAdapters/SelfNodeAdapter.h"
#include "Graphs/K2NodeAdapters/VariableGetSetNodeAdapter.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr const TCHAR* UbergraphPagesPath = TEXT("/Body/UbergraphPages");

FString GetClassPath(const UClass* Class)
{
	return Class ? Class->GetPathName() : FString();
}

FAssetDocumentCapabilityResult GraphFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FString JoinPath(const FString& Left, const FString& Right)
{
	return Left.IsEmpty() ? Right : Left / Right;
}

FString GraphPath(const FAssetDocumentGraphSpec& Graph)
{
	return JoinPath(UbergraphPagesPath, Graph.Name);
}

FString NodePath(const FAssetDocumentGraphSpec& Graph, const FAssetDocumentNodeSpec& Node)
{
	return JoinPath(JoinPath(GraphPath(Graph), TEXT("Nodes")), Node.Id);
}

FString LinkPath(const FAssetDocumentGraphSpec& Graph, const FAssetDocumentLinkSpec& Link)
{
	return FString::Printf(
		TEXT("%s/Links/%s:%s->%s:%s"),
		*GraphPath(Graph),
		*Link.From.Node,
		*Link.From.Pin,
		*Link.To.Node,
		*Link.To.Pin);
}

UClass* ResolveClass(const FString& ClassPath, UClass* RequiredBaseClass)
{
	UClass* Class = ClassPath.IsEmpty() ? nullptr : StaticLoadClass(RequiredBaseClass ? RequiredBaseClass : UObject::StaticClass(), nullptr, *ClassPath);
	return Class && (!RequiredBaseClass || Class->IsChildOf(RequiredBaseClass)) ? Class : nullptr;
}

bool TryReadMemberRef(const TSharedPtr<FJsonObject>& Member, FString& OutOwnerClass, FString& OutName)
{
	if (!Member.IsValid())
	{
		return false;
	}

	FString Kind;
	return Member->TryGetStringField(TEXT("Kind"), Kind)
		&& Kind == TEXT("MemberRef")
		&& Member->TryGetStringField(TEXT("OwnerClass"), OutOwnerClass)
		&& !OutOwnerClass.IsEmpty()
		&& Member->TryGetStringField(TEXT("Name"), OutName)
		&& !OutName.IsEmpty();
}

UClass* ResolveMemberOwnerClass(const UBlueprint* Blueprint, const FString& OwnerClassPath)
{
	if (OwnerClassPath == TEXT("Self"))
	{
		if (Blueprint && Blueprint->GeneratedClass)
		{
			return Blueprint->GeneratedClass;
		}
		return Blueprint ? Blueprint->ParentClass.Get() : nullptr;
	}
	return ResolveClass(OwnerClassPath, UObject::StaticClass());
}

UFunction* ResolveMemberFunction(const UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Member)
{
	FString OwnerClassPath;
	FString FunctionName;
	if (!TryReadMemberRef(Member, OwnerClassPath, FunctionName))
	{
		return nullptr;
	}

	UClass* OwnerClass = ResolveMemberOwnerClass(Blueprint, OwnerClassPath);
	return OwnerClass ? OwnerClass->FindFunctionByName(FName(*FunctionName)) : nullptr;
}

FProperty* ResolveMemberProperty(const UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Member)
{
	FString OwnerClassPath;
	FString PropertyName;
	if (!TryReadMemberRef(Member, OwnerClassPath, PropertyName))
	{
		return nullptr;
	}

	if (OwnerClassPath == TEXT("Self") && Blueprint)
	{
		const FName VariableName(*PropertyName);
		if (Blueprint->NewVariables.ContainsByPredicate([VariableName](const FBPVariableDescription& Variable)
		{
			return Variable.VarName == VariableName;
		}))
		{
			return FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass : Blueprint->GeneratedClass, VariableName);
		}
	}

	UClass* OwnerClass = ResolveMemberOwnerClass(Blueprint, OwnerClassPath);
	return OwnerClass ? FindFProperty<FProperty>(OwnerClass, FName(*PropertyName)) : nullptr;
}

FAssetDocumentCapabilityResult MakeMissingMemberFailure(const FAssetDocumentGraphSpec& Graph, const FAssetDocumentNodeSpec& Node)
{
	return GraphFailure(
		FString::Printf(TEXT("Graph node '%s' requires a reflected MemberRef"), *Node.Id),
		JoinPath(NodePath(Graph, Node), TEXT("Member")),
		TEXT("MissingGraphMemberReference"));
}

FAssetDocumentCapabilityResult MakeUnresolvedMemberFailure(const FAssetDocumentGraphSpec& Graph, const FAssetDocumentNodeSpec& Node)
{
	return GraphFailure(
		FString::Printf(TEXT("Graph node '%s' MemberRef could not be resolved"), *Node.Id),
		JoinPath(NodePath(Graph, Node), TEXT("Member")),
		TEXT("UnresolvedGraphMemberReference"));
}

bool ConfigureNodeFromSpec(UBlueprint* Blueprint, UEdGraphNode* Node, const FAssetDocumentGraphSpec& GraphSpec, const FAssetDocumentNodeSpec& NodeSpec, FAssetDocumentCapabilityResult& OutFailure)
{
	if (!Node)
	{
		OutFailure = GraphFailure(
			FString::Printf(TEXT("Graph node '%s' could not be created"), *NodeSpec.Id),
			NodePath(GraphSpec, NodeSpec),
			TEXT("UnresolvedGraphNodeClass"));
		return false;
	}

	Node->Modify();
	Node->NodePosX = 0;
	Node->NodePosY = 0;
	if (NodeSpec.Position.IsValid())
	{
		double X = 0.0;
		double Y = 0.0;
		NodeSpec.Position->TryGetNumberField(TEXT("X"), X);
		NodeSpec.Position->TryGetNumberField(TEXT("Y"), Y);
		Node->NodePosX = static_cast<int32>(X);
		Node->NodePosY = static_cast<int32>(Y);
	}
	Node->NodeComment = NodeSpec.bHasComment ? NodeSpec.Comment : FString();

	if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		FString OwnerClassPath;
		FString FunctionName;
		if (!TryReadMemberRef(NodeSpec.Member, OwnerClassPath, FunctionName))
		{
			OutFailure = MakeMissingMemberFailure(GraphSpec, NodeSpec);
			return false;
		}
		UClass* OwnerClass = ResolveMemberOwnerClass(Blueprint, OwnerClassPath);
		UFunction* Function = OwnerClass ? OwnerClass->FindFunctionByName(FName(*FunctionName)) : nullptr;
		if (!OwnerClass || !Function)
		{
			OutFailure = MakeUnresolvedMemberFailure(GraphSpec, NodeSpec);
			return false;
		}

		EventNode->EventReference.SetExternalMember(FName(*FunctionName), OwnerClass);
		EventNode->bOverrideFunction = true;
	}
	else if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		UFunction* Function = ResolveMemberFunction(Blueprint, NodeSpec.Member);
		if (!Function)
		{
			OutFailure = NodeSpec.Member.IsValid()
				? MakeUnresolvedMemberFailure(GraphSpec, NodeSpec)
				: MakeMissingMemberFailure(GraphSpec, NodeSpec);
			return false;
		}

		CallNode->SetFromFunction(Function);
		CallNode->FunctionReference.SetFromField<UFunction>(Function, false);
	}
	else if (UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
	{
		FString OwnerClassPath;
		FString PropertyName;
		if (!TryReadMemberRef(NodeSpec.Member, OwnerClassPath, PropertyName))
		{
			OutFailure = MakeMissingMemberFailure(GraphSpec, NodeSpec);
			return false;
		}
		if (!ResolveMemberProperty(Blueprint, NodeSpec.Member))
		{
			OutFailure = MakeUnresolvedMemberFailure(GraphSpec, NodeSpec);
			return false;
		}
		if (OwnerClassPath == TEXT("Self"))
		{
			VariableGet->VariableReference.SetSelfMember(FName(*PropertyName));
		}
		else
		{
			UClass* OwnerClass = ResolveMemberOwnerClass(Blueprint, OwnerClassPath);
			VariableGet->VariableReference.SetExternalMember(FName(*PropertyName), OwnerClass);
		}
	}
	else if (UK2Node_VariableSet* VariableSet = Cast<UK2Node_VariableSet>(Node))
	{
		FString OwnerClassPath;
		FString PropertyName;
		if (!TryReadMemberRef(NodeSpec.Member, OwnerClassPath, PropertyName))
		{
			OutFailure = MakeMissingMemberFailure(GraphSpec, NodeSpec);
			return false;
		}
		if (!ResolveMemberProperty(Blueprint, NodeSpec.Member))
		{
			OutFailure = MakeUnresolvedMemberFailure(GraphSpec, NodeSpec);
			return false;
		}
		if (OwnerClassPath == TEXT("Self"))
		{
			VariableSet->VariableReference.SetSelfMember(FName(*PropertyName));
		}
		else
		{
			UClass* OwnerClass = ResolveMemberOwnerClass(Blueprint, OwnerClassPath);
			VariableSet->VariableReference.SetExternalMember(FName(*PropertyName), OwnerClass);
		}
	}
	else if (!Node->IsA<UK2Node_Self>())
	{
		OutFailure = GraphFailure(
			FString::Printf(TEXT("Graph node class '%s' is not supported by Tier 1 K2 apply"), *NodeSpec.Class),
			NodePath(GraphSpec, NodeSpec),
			TEXT("UnsupportedGraphNodeClass"));
		return false;
	}

	if (Node->Pins.IsEmpty())
	{
		Node->AllocateDefaultPins();
	}
	else
	{
		Node->ReconstructNode();
	}
	return true;
}

FString JsonScalarToString(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FString();
	}
	if (Value->Type == EJson::String)
	{
		return Value->AsString();
	}
	if (Value->Type == EJson::Number)
	{
		return FString::SanitizeFloat(Value->AsNumber());
	}
	if (Value->Type == EJson::Boolean)
	{
		return Value->AsBool() ? TEXT("true") : TEXT("false");
	}
	return FString();
}

void ResetPinDefaultToBaseline(UEdGraphPin* Pin)
{
	if (!Pin
		|| Pin->Direction != EGPD_Input
		|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
		|| Pin->PinName == UEdGraphSchema_K2::PN_Self)
	{
		return;
	}
	Pin->DefaultValue = Pin->AutogeneratedDefaultValue;
	Pin->DefaultObject = nullptr;
	Pin->DefaultTextValue = FText::GetEmpty();
}

FAssetDocumentCapabilityResult ApplyPinDefaults(const FAssetDocumentGraphSpec& GraphSpec, const FAssetDocumentNodeSpec& NodeSpec, UEdGraphNode* Node)
{
	TSet<FString> AuthoredPins;
	for (const FAssetDocumentPinOverrideSpec& PinOverride : NodeSpec.PinOverrides)
	{
		AuthoredPins.Add(PinOverride.Pin);
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && !AuthoredPins.Contains(Pin->PinName.ToString()))
		{
			ResetPinDefaultToBaseline(Pin);
		}
	}

	for (const FAssetDocumentPinOverrideSpec& PinOverride : NodeSpec.PinOverrides)
	{
		UEdGraphPin* Pin = Node->FindPin(FName(*PinOverride.Pin));
		if (!Pin)
		{
			return GraphFailure(
				FString::Printf(TEXT("Graph pin '%s' does not exist on node '%s' after reconstruction"), *PinOverride.Pin, *NodeSpec.Id),
				JoinPath(NodePath(GraphSpec, NodeSpec), TEXT("PinOverrides") / PinOverride.Pin),
				TEXT("InvalidGraphPin"));
		}
		if (Pin->Direction != EGPD_Input)
		{
			return GraphFailure(
				FString::Printf(TEXT("Graph pin '%s' is not an input pin"), *PinOverride.Pin),
				JoinPath(NodePath(GraphSpec, NodeSpec), TEXT("PinOverrides") / PinOverride.Pin),
				TEXT("InvalidGraphPin"));
		}

		ResetPinDefaultToBaseline(Pin);
		if (PinOverride.DefaultValue.IsValid())
		{
			Pin->DefaultValue = JsonScalarToString(PinOverride.DefaultValue);
		}
		if (PinOverride.DefaultObject.IsValid())
		{
			const FString ObjectPath = JsonScalarToString(PinOverride.DefaultObject);
			Pin->DefaultObject = ObjectPath.IsEmpty() ? nullptr : StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
			if (!ObjectPath.IsEmpty() && !Pin->DefaultObject)
			{
				return GraphFailure(
					FString::Printf(TEXT("Graph pin '%s' DefaultObject '%s' could not be loaded"), *PinOverride.Pin, *ObjectPath),
					JoinPath(NodePath(GraphSpec, NodeSpec), TEXT("PinOverrides") / PinOverride.Pin / TEXT("DefaultObject")),
					TEXT("InvalidGraphPinDefault"));
			}
		}
		if (PinOverride.DefaultTextValue.IsValid())
		{
			Pin->DefaultTextValue = FText::FromString(JsonScalarToString(PinOverride.DefaultTextValue));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

UEdGraphPin* FindPinBySpec(const TMap<FString, UEdGraphNode*>& NodesById, const FAssetDocumentGraphEndpoint& Endpoint)
{
	UEdGraphNode* const* Node = NodesById.Find(Endpoint.Node);
	return Node && *Node ? (*Node)->FindPin(FName(*Endpoint.Pin)) : nullptr;
}

FAssetDocumentCapabilityResult ValidateLinks(const FAssetDocumentGraphSpec& GraphSpec, const UEdGraphSchema_K2* Schema, const TMap<FString, UEdGraphNode*>& NodesById)
{
	for (const FAssetDocumentLinkSpec& Link : GraphSpec.Links)
	{
		UEdGraphPin* FromPin = FindPinBySpec(NodesById, Link.From);
		UEdGraphPin* ToPin = FindPinBySpec(NodesById, Link.To);
		if (!FromPin || !ToPin)
		{
			return GraphFailure(
				FString::Printf(TEXT("Graph link endpoint '%s' could not be resolved after node reconstruction"), *Link.ToKey()),
				LinkPath(GraphSpec, Link),
				TEXT("UnresolvedGraphLinkEndpoint"));
		}
		if (FromPin->Direction != EGPD_Output || ToPin->Direction != EGPD_Input)
		{
			return GraphFailure(
				FString::Printf(TEXT("Graph link '%s' must connect output to input"), *Link.ToKey()),
				LinkPath(GraphSpec, Link),
				TEXT("InvalidGraphLinkType"));
		}
		const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
		if (Response.Response == CONNECT_RESPONSE_DISALLOW)
		{
			return GraphFailure(
				FString::Printf(TEXT("Graph schema rejected link '%s': %s"), *Link.ToKey(), *Response.Message.ToString()),
				LinkPath(GraphSpec, Link),
				TEXT("InvalidGraphLinkType"));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult PreflightGraphSpec(UBlueprint* Blueprint, const FAssetDocumentGraphSpec& GraphSpec, UClass*& OutSchemaClass, TMap<FString, UClass*>& OutNodeClasses)
{
	OutSchemaClass = ResolveClass(GraphSpec.Schema, UEdGraphSchema::StaticClass());
	if (!OutSchemaClass || !OutSchemaClass->IsChildOf(UEdGraphSchema_K2::StaticClass()))
	{
		return GraphFailure(
			FString::Printf(TEXT("Graph schema '%s' could not be resolved as EdGraphSchema_K2"), *GraphSpec.Schema),
			JoinPath(GraphPath(GraphSpec), TEXT("Schema")),
			TEXT("InvalidGraphSchema"));
	}

	const FAssetDocumentNodeAdapterRegistry Registry = FAssetDocumentK2GraphAdapter::CreateTier1NodeAdapterRegistry();
	UEdGraph* TempGraph = NewObject<UEdGraph>(Blueprint ? static_cast<UObject*>(Blueprint) : GetTransientPackage(), NAME_None, RF_Transient);
	TempGraph->Schema = OutSchemaClass;
	const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(TempGraph->GetSchema());
	if (!Schema)
	{
		return GraphFailure(TEXT("K2 graph schema could not be initialized"), GraphPath(GraphSpec), TEXT("InvalidGraphSchema"));
	}

	TMap<FString, UEdGraphNode*> TempNodesById;
	for (const FAssetDocumentNodeSpec& NodeSpec : GraphSpec.Nodes)
	{
		UClass* NodeClass = ResolveClass(NodeSpec.Class, UEdGraphNode::StaticClass());
		if (!NodeClass)
		{
			return GraphFailure(
				FString::Printf(TEXT("Graph node class '%s' could not be loaded"), *NodeSpec.Class),
				JoinPath(NodePath(GraphSpec, NodeSpec), TEXT("Class")),
				TEXT("UnresolvedGraphNodeClass"));
		}
		if (!Registry.FindAdapter(NodeClass).IsValid())
		{
			return GraphFailure(
				FString::Printf(TEXT("Graph node class '%s' has no Tier 1 adapter"), *NodeSpec.Class),
				JoinPath(NodePath(GraphSpec, NodeSpec), TEXT("Class")),
				TEXT("UnsupportedGraphNodeClass"));
		}

		UEdGraphNode* TempNode = NewObject<UEdGraphNode>(TempGraph, NodeClass, NAME_None, RF_Transient);
		TempGraph->AddNode(TempNode, false, false);
		FAssetDocumentCapabilityResult ConfigureFailure = FAssetDocumentCapabilityResult::Success();
		if (!ConfigureNodeFromSpec(Blueprint, TempNode, GraphSpec, NodeSpec, ConfigureFailure))
		{
			return ConfigureFailure;
		}
		const FAssetDocumentCapabilityResult PinResult = ApplyPinDefaults(GraphSpec, NodeSpec, TempNode);
		if (!PinResult.bSuccess)
		{
			return PinResult;
		}
		TempNodesById.Add(NodeSpec.Id, TempNode);
		OutNodeClasses.Add(NodeSpec.Id, NodeClass);
	}

	return ValidateLinks(GraphSpec, Schema, TempNodesById);
}

UEdGraph* FindUbergraphPageByName(UBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}
	return nullptr;
}

UEdGraph* FindOrCreateUbergraphPage(UBlueprint* Blueprint, const FAssetDocumentGraphSpec& GraphSpec, UClass* SchemaClass, bool& bOutChanged)
{
	UEdGraph* Graph = FindUbergraphPageByName(Blueprint, GraphSpec.Name);
	if (Graph)
	{
		return Graph;
	}

	Graph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*GraphSpec.Name),
		UEdGraph::StaticClass(),
		SchemaClass);
	FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
	bOutChanged = true;
	return Graph;
}

bool NodeMatchesMemberSpec(const UBlueprint* Blueprint, const UEdGraphNode* Node, const FAssetDocumentNodeSpec& NodeSpec)
{
	if (!Node || Node->GetClass()->GetPathName() != NodeSpec.Class)
	{
		return false;
	}

	if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		FString OwnerClassPath;
		FString FunctionName;
		if (!TryReadMemberRef(NodeSpec.Member, OwnerClassPath, FunctionName))
		{
			return false;
		}
		return EventNode->GetFunctionName() == FName(*FunctionName);
	}
	if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		UFunction* DesiredFunction = ResolveMemberFunction(Blueprint, NodeSpec.Member);
		UFunction* CurrentFunction = CallNode->GetTargetFunction();
		return DesiredFunction && CurrentFunction == DesiredFunction;
	}
	if (const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
	{
		FString OwnerClassPath;
		FString PropertyName;
		return TryReadMemberRef(NodeSpec.Member, OwnerClassPath, PropertyName)
			&& VariableNode->GetVarName() == FName(*PropertyName);
	}
	return Node->IsA<UK2Node_Self>();
}

UEdGraphNode* FindReusableNode(const UBlueprint* Blueprint, UEdGraph* Graph, const FAssetDocumentNodeSpec& NodeSpec, TSet<UEdGraphNode*>& UsedNodes)
{
	if (!NodeSpec.NodeGuid.IsEmpty())
	{
		FGuid DesiredGuid;
		if (FGuid::ParseExact(NodeSpec.NodeGuid, EGuidFormats::Digits, DesiredGuid) || FGuid::Parse(NodeSpec.NodeGuid, DesiredGuid))
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && !UsedNodes.Contains(Node) && Node->NodeGuid == DesiredGuid)
				{
					UsedNodes.Add(Node);
					return Node;
				}
			}
		}
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && !UsedNodes.Contains(Node) && NodeMatchesMemberSpec(Blueprint, Node, NodeSpec))
		{
			UsedNodes.Add(Node);
			return Node;
		}
	}
	return nullptr;
}

void BreakAllLinksForManagedNodes(const TMap<FString, UEdGraphNode*>& NodesById)
{
	for (const TPair<FString, UEdGraphNode*>& Pair : NodesById)
	{
		if (!Pair.Value)
		{
			continue;
		}
		for (UEdGraphPin* Pin : Pair.Value->Pins)
		{
			if (Pin)
			{
				Pin->BreakAllPinLinks();
			}
		}
	}
}

FAssetDocumentCapabilityResult CreateLinks(const FAssetDocumentGraphSpec& GraphSpec, const UEdGraphSchema_K2* Schema, const TMap<FString, UEdGraphNode*>& NodesById)
{
	for (const FAssetDocumentLinkSpec& Link : GraphSpec.Links)
	{
		UEdGraphPin* FromPin = FindPinBySpec(NodesById, Link.From);
		UEdGraphPin* ToPin = FindPinBySpec(NodesById, Link.To);
		if (!FromPin || !ToPin)
		{
			return GraphFailure(
				FString::Printf(TEXT("Graph link endpoint '%s' could not be resolved after node reconstruction"), *Link.ToKey()),
				LinkPath(GraphSpec, Link),
				TEXT("UnresolvedGraphLinkEndpoint"));
		}
		if (!Schema->TryCreateConnection(FromPin, ToPin))
		{
			return GraphFailure(
				FString::Printf(TEXT("Graph schema rejected link '%s'"), *Link.ToKey()),
				LinkPath(GraphSpec, Link),
				TEXT("InvalidGraphLinkType"));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyGraphsNoCompile(UBlueprint* Blueprint, const TArray<FAssetDocumentGraphSpec>& DesiredGraphs, bool& bOutChanged)
{
	TSet<FString> DesiredGraphNames;
	TMap<FString, UClass*> SchemaClassesByGraph;
	TMap<FString, TMap<FString, UClass*>> NodeClassesByGraph;
	for (const FAssetDocumentGraphSpec& GraphSpec : DesiredGraphs)
	{
		UClass* SchemaClass = nullptr;
		TMap<FString, UClass*> NodeClasses;
		const FAssetDocumentCapabilityResult PreflightResult = PreflightGraphSpec(Blueprint, GraphSpec, SchemaClass, NodeClasses);
		if (!PreflightResult.bSuccess)
		{
			return PreflightResult;
		}
		DesiredGraphNames.Add(GraphSpec.Name);
		SchemaClassesByGraph.Add(GraphSpec.Name, SchemaClass);
		NodeClassesByGraph.Add(GraphSpec.Name, MoveTemp(NodeClasses));
	}

	for (UEdGraph* ExistingGraph : TArray<UEdGraph*>(Blueprint->UbergraphPages))
	{
		if (!ExistingGraph || DesiredGraphNames.Contains(ExistingGraph->GetName()))
		{
			continue;
		}

		if (ExistingGraph == FBlueprintEditorUtils::FindEventGraph(Blueprint))
		{
			for (UEdGraphNode* Node : TArray<UEdGraphNode*>(ExistingGraph->Nodes))
			{
				if (Node)
				{
					Node->DestroyNode();
					bOutChanged = true;
				}
			}
		}
		else
		{
			FBlueprintEditorUtils::RemoveGraph(Blueprint, ExistingGraph, EGraphRemoveFlags::MarkTransient);
			bOutChanged = true;
		}
	}

	for (const FAssetDocumentGraphSpec& GraphSpec : DesiredGraphs)
	{
		UClass* SchemaClass = SchemaClassesByGraph.FindRef(GraphSpec.Name);
		bool bGraphChanged = false;
		UEdGraph* Graph = FindOrCreateUbergraphPage(Blueprint, GraphSpec, SchemaClass, bGraphChanged);
		bOutChanged |= bGraphChanged;
		if (!Graph)
		{
			return GraphFailure(
				FString::Printf(TEXT("Failed to create graph '%s'"), *GraphSpec.Name),
				GraphPath(GraphSpec),
				TEXT("InvalidGraphRegion"));
		}

		Graph->Modify();
		TSet<UEdGraphNode*> UsedExistingNodes;
		TMap<FString, UEdGraphNode*> NodesById;
		const TMap<FString, UClass*>* NodeClasses = NodeClassesByGraph.Find(GraphSpec.Name);
		for (const FAssetDocumentNodeSpec& NodeSpec : GraphSpec.Nodes)
		{
			UEdGraphNode* Node = FindReusableNode(Blueprint, Graph, NodeSpec, UsedExistingNodes);
			if (!Node)
			{
				UClass* NodeClass = NodeClasses ? NodeClasses->FindRef(NodeSpec.Id) : nullptr;
				Node = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
				Graph->AddNode(Node, true, false);
				UsedExistingNodes.Add(Node);
				bOutChanged = true;
			}

			FAssetDocumentCapabilityResult ConfigureFailure = FAssetDocumentCapabilityResult::Success();
			if (!ConfigureNodeFromSpec(Blueprint, Node, GraphSpec, NodeSpec, ConfigureFailure))
			{
				return ConfigureFailure;
			}
			const FAssetDocumentCapabilityResult PinResult = ApplyPinDefaults(GraphSpec, NodeSpec, Node);
			if (!PinResult.bSuccess)
			{
				return PinResult;
			}
			NodesById.Add(NodeSpec.Id, Node);
		}

		for (UEdGraphNode* ExistingNode : TArray<UEdGraphNode*>(Graph->Nodes))
		{
			if (ExistingNode && !UsedExistingNodes.Contains(ExistingNode))
			{
				ExistingNode->DestroyNode();
				bOutChanged = true;
			}
		}

		BreakAllLinksForManagedNodes(NodesById);
		const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
		const FAssetDocumentCapabilityResult LinkResult = CreateLinks(GraphSpec, Schema, NodesById);
		if (!LinkResult.bSuccess)
		{
			return LinkResult;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
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

FAssetDocumentK2GraphApplyResult FAssetDocumentK2GraphAdapter::ApplyUbergraphPages(
	UBlueprint* Blueprint,
	const TArray<FAssetDocumentGraphSpec>& DesiredGraphs) const
{
	FAssetDocumentK2GraphApplyResult ApplyResult;
	if (!Blueprint)
	{
		ApplyResult.Result = GraphFailure(
			TEXT("UBlueprint graph apply requires exact UBlueprint asset"),
			TEXT("/Body"),
			TEXT("UnsupportedAsset"));
		return ApplyResult;
	}

	const FAssetDocumentK2GraphExtractResult Snapshot = ExtractUbergraphPages(Blueprint);
	bool bChanged = false;
	ApplyResult.Result = ApplyGraphsNoCompile(Blueprint, DesiredGraphs, bChanged);
	if (!ApplyResult.Result.bSuccess)
	{
		return ApplyResult;
	}

	if (bChanged || !DesiredGraphs.IsEmpty())
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			bool bRollbackChanged = false;
			ApplyGraphsNoCompile(Blueprint, Snapshot.Graphs, bRollbackChanged);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			ApplyResult.Result = GraphFailure(
				TEXT("Failed to compile UBlueprint after applying graph regions"),
				UbergraphPagesPath,
				TEXT("BlueprintCompileFailed"));
			return ApplyResult;
		}
	}

	ApplyResult.bChanged = bChanged;
	ApplyResult.Result = FAssetDocumentCapabilityResult::Success(TEXT("Applied UBlueprint graph regions"));
	return ApplyResult;
}
