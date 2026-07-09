// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentGraphDiff.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Graphs/AssetDocumentGraphDefinitionResolver.h"

namespace
{
FString JoinPath(const FString& BasePath, const FString& Segment)
{
	const FString EscapedSegment = FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Segment);
	if (BasePath.IsEmpty())
	{
		return FString::Printf(TEXT("/%s"), *EscapedSegment);
	}
	return FString::Printf(TEXT("%s/%s"), *BasePath, *EscapedSegment);
}

FString LinkPathToken(const FAssetDocumentLinkSpec& Link)
{
	return FString::Printf(
		TEXT("%s:%s->%s:%s"),
		*Link.From.Node,
		*Link.From.Pin,
		*Link.To.Node,
		*Link.To.Pin);
}

void AddEntry(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& Path,
	const FString& Status,
	const TSharedPtr<FJsonValue>& Desired,
	const TSharedPtr<FJsonValue>& Current,
	const FString& Message = FString())
{
	FAssetDocumentGraphDiffEntry Entry;
	Entry.Path = Path;
	Entry.Status = Status;
	Entry.Desired = AssetDocumentGraphJson::CloneJsonValue(Desired);
	Entry.Current = AssetDocumentGraphJson::CloneJsonValue(Current);
	Entry.Message = Message;
	Entries.Add(MoveTemp(Entry));
}

void AddComparisonEntry(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& Path,
	const TSharedPtr<FJsonValue>& Desired,
	const TSharedPtr<FJsonValue>& Current)
{
	AddEntry(
		Entries,
		Path,
		AssetDocumentGraphJson::AreJsonValuesEqual(Desired, Current) ? TEXT("unchanged") : TEXT("changed"),
		Desired,
		Current);
}

TMap<FString, const FAssetDocumentGraphSpec*> MapGraphsByName(const TArray<FAssetDocumentGraphSpec>& Graphs)
{
	TMap<FString, const FAssetDocumentGraphSpec*> GraphMap;
	for (const FAssetDocumentGraphSpec& Graph : Graphs)
	{
		GraphMap.Add(Graph.Name, &Graph);
	}
	return GraphMap;
}

TMap<FString, const FAssetDocumentGraphSpec*> MapGraphsById(const TArray<FAssetDocumentGraphSpec>& Graphs)
{
	TMap<FString, const FAssetDocumentGraphSpec*> GraphMap;
	for (const FAssetDocumentGraphSpec& Graph : Graphs)
	{
		GraphMap.Add(Graph.Id, &Graph);
	}
	return GraphMap;
}

TMap<FString, const FAssetDocumentNodeSpec*> MapNodesById(const FAssetDocumentGraphSpec& Graph)
{
	TMap<FString, const FAssetDocumentNodeSpec*> NodeMap;
	for (const FAssetDocumentNodeSpec& Node : Graph.Nodes)
	{
		NodeMap.Add(Node.Id, &Node);
	}
	return NodeMap;
}

TMap<FString, const FAssetDocumentPinOverrideSpec*> MapPinsById(const FAssetDocumentNodeSpec& Node)
{
	TMap<FString, const FAssetDocumentPinOverrideSpec*> PinMap;
	for (const FAssetDocumentPinOverrideSpec& Pin : Node.PinOverrides)
	{
		PinMap.Add(Pin.Pin, &Pin);
	}
	return PinMap;
}

TMap<FString, const FAssetDocumentLinkSpec*> MapLinksByPathToken(const FAssetDocumentGraphSpec& Graph)
{
	TMap<FString, const FAssetDocumentLinkSpec*> LinkMap;
	for (const FAssetDocumentLinkSpec& Link : Graph.Links)
	{
		LinkMap.Add(LinkPathToken(Link), &Link);
	}
	return LinkMap;
}

TArray<FString> SortedUnionKeys(const TArray<FString>& LeftKeys, const TArray<FString>& RightKeys)
{
	TSet<FString> KeySet;
	for (const FString& Key : LeftKeys)
	{
		KeySet.Add(Key);
	}
	for (const FString& Key : RightKeys)
	{
		KeySet.Add(Key);
	}

	TArray<FString> Keys = KeySet.Array();
	Keys.Sort();
	return Keys;
}

TArray<FString> SortedJsonObjectKeys(const TSharedPtr<FJsonObject>& Object)
{
	TArray<FString> Keys;
	if (Object.IsValid())
	{
		Object->Values.GetKeys(Keys);
		Keys.Sort();
	}
	return Keys;
}

template <typename ValueType>
TArray<FString> SortedMapKeys(const TMap<FString, ValueType>& Map)
{
	TArray<FString> Keys;
	Map.GetKeys(Keys);
	Keys.Sort();
	return Keys;
}

TSharedPtr<FJsonValue> MakeJsonObjectValue(const TSharedPtr<FJsonObject>& Object)
{
	const TSharedPtr<FJsonObject> Clone = AssetDocumentGraphJson::CloneJsonObject(Object);
	if (!Clone.IsValid())
	{
		return nullptr;
	}
	return MakeShared<FJsonValueObject>(Clone.ToSharedRef());
}

void CompareObjectAsWhole(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& Path,
	const TSharedPtr<FJsonObject>& DesiredObject,
	const TSharedPtr<FJsonObject>& CurrentObject,
	const FString& Message = FString())
{
	if (!DesiredObject.IsValid() && !CurrentObject.IsValid())
	{
		return;
	}

	const FString Status = !DesiredObject.IsValid()
		? TEXT("extra")
		: !CurrentObject.IsValid()
			? TEXT("missing")
			: AssetDocumentGraphJson::AreJsonObjectsEqual(DesiredObject, CurrentObject)
				? TEXT("unchanged")
				: TEXT("changed");
	AddEntry(Entries, Path, Status, MakeJsonObjectValue(DesiredObject), MakeJsonObjectValue(CurrentObject), Message);
}

void CompareObjectFields(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& ObjectPath,
	const TSharedPtr<FJsonObject>& DesiredObject,
	const TSharedPtr<FJsonObject>& CurrentObject)
{
	if (!DesiredObject.IsValid() || !CurrentObject.IsValid())
	{
		CompareObjectAsWhole(Entries, ObjectPath, DesiredObject, CurrentObject);
		return;
	}

	const TArray<FString> Keys = SortedUnionKeys(SortedJsonObjectKeys(DesiredObject), SortedJsonObjectKeys(CurrentObject));
	for (const FString& Key : Keys)
	{
		const TSharedPtr<FJsonValue>* DesiredValue = DesiredObject->Values.Find(Key);
		const TSharedPtr<FJsonValue>* CurrentValue = CurrentObject->Values.Find(Key);
		const FString FieldPath = JoinPath(ObjectPath, Key);
		if (!DesiredValue)
		{
			AddEntry(Entries, FieldPath, TEXT("extra"), nullptr, AssetDocumentGraphJson::CloneJsonValue(*CurrentValue));
		}
		else if (!CurrentValue)
		{
			AddEntry(Entries, FieldPath, TEXT("missing"), AssetDocumentGraphJson::CloneJsonValue(*DesiredValue), nullptr);
		}
		else
		{
			AddComparisonEntry(Entries, FieldPath, *DesiredValue, *CurrentValue);
		}
	}
}

void CompareOptionalValue(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& Path,
	const TSharedPtr<FJsonValue>& Desired,
	const TSharedPtr<FJsonValue>& Current)
{
	if (!Desired.IsValid() && !Current.IsValid())
	{
		return;
	}
	AddComparisonEntry(Entries, Path, Desired, Current);
}

void ComparePins(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& NodePath,
	const FAssetDocumentNodeSpec& DesiredNode,
	const FAssetDocumentNodeSpec& CurrentNode)
{
	const TMap<FString, const FAssetDocumentPinOverrideSpec*> DesiredPins = MapPinsById(DesiredNode);
	const TMap<FString, const FAssetDocumentPinOverrideSpec*> CurrentPins = MapPinsById(CurrentNode);
	const TArray<FString> PinKeys = SortedUnionKeys(SortedMapKeys(DesiredPins), SortedMapKeys(CurrentPins));

	for (const FString& PinKey : PinKeys)
	{
		const FAssetDocumentPinOverrideSpec* const* DesiredPin = DesiredPins.Find(PinKey);
		const FAssetDocumentPinOverrideSpec* const* CurrentPin = CurrentPins.Find(PinKey);
		const FString PinPath = JoinPath(JoinPath(NodePath, TEXT("PinOverrides")), PinKey);
		if (!DesiredPin)
		{
			AddEntry(
				Entries,
				PinPath,
				TEXT("extra"),
				nullptr,
				MakeShared<FJsonValueObject>((*CurrentPin)->ToJsonObject()));
		}
		else if (!CurrentPin)
		{
			AddEntry(
				Entries,
				PinPath,
				TEXT("missing"),
				MakeShared<FJsonValueObject>((*DesiredPin)->ToJsonObject()),
				nullptr);
		}
		else
		{
			AddComparisonEntry(
				Entries,
				PinPath,
				MakeShared<FJsonValueObject>((*DesiredPin)->ToJsonObject()),
				MakeShared<FJsonValueObject>((*CurrentPin)->ToJsonObject()));
		}
	}
}

void CompareNodeDetails(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& NodePath,
	const FAssetDocumentNodeSpec& DesiredNode,
	const FAssetDocumentNodeSpec& CurrentNode)
{
	AddComparisonEntry(
		Entries,
		NodePath,
		MakeShared<FJsonValueObject>(DesiredNode.ToJsonObject()),
		MakeShared<FJsonValueObject>(CurrentNode.ToJsonObject()));
	CompareObjectFields(Entries, JoinPath(NodePath, TEXT("Fields")), DesiredNode.Fields, CurrentNode.Fields);
	CompareObjectFields(Entries, JoinPath(NodePath, TEXT("Pins")), DesiredNode.Pins, CurrentNode.Pins);
	CompareObjectAsWhole(
		Entries,
		JoinPath(NodePath, TEXT("Position")),
		DesiredNode.Position,
		CurrentNode.Position,
		TEXT("layout"));
	CompareObjectFields(Entries, JoinPath(NodePath, TEXT("Spawner")), DesiredNode.Spawner, CurrentNode.Spawner);
	CompareObjectFields(Entries, JoinPath(NodePath, TEXT("SubgraphRefs")), DesiredNode.SubgraphRefs, CurrentNode.SubgraphRefs);
	CompareObjectFields(Entries, JoinPath(NodePath, TEXT("Evidence")), DesiredNode.Evidence, CurrentNode.Evidence);
}

void CompareLinks(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& GraphPath,
	const FAssetDocumentGraphSpec& DesiredGraph,
	const FAssetDocumentGraphSpec& CurrentGraph)
{
	const TMap<FString, const FAssetDocumentLinkSpec*> DesiredLinks = MapLinksByPathToken(DesiredGraph);
	const TMap<FString, const FAssetDocumentLinkSpec*> CurrentLinks = MapLinksByPathToken(CurrentGraph);
	const TArray<FString> LinkKeys = SortedUnionKeys(SortedMapKeys(DesiredLinks), SortedMapKeys(CurrentLinks));

	for (const FString& LinkKey : LinkKeys)
	{
		const FAssetDocumentLinkSpec* const* DesiredLink = DesiredLinks.Find(LinkKey);
		const FAssetDocumentLinkSpec* const* CurrentLink = CurrentLinks.Find(LinkKey);
		const FString LinkPath = JoinPath(JoinPath(GraphPath, TEXT("Links")), LinkKey);
		if (!DesiredLink)
		{
			AddEntry(
				Entries,
				LinkPath,
				TEXT("extra"),
				nullptr,
				MakeShared<FJsonValueObject>((*CurrentLink)->ToJsonObject()));
		}
		else if (!CurrentLink)
		{
			AddEntry(
				Entries,
				LinkPath,
				TEXT("missing"),
				MakeShared<FJsonValueObject>((*DesiredLink)->ToJsonObject()),
				nullptr);
		}
		else
		{
			AddComparisonEntry(
				Entries,
				LinkPath,
				MakeShared<FJsonValueObject>((*DesiredLink)->ToJsonObject()),
				MakeShared<FJsonValueObject>((*CurrentLink)->ToJsonObject()));
		}
	}
}

void CompareNodes(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& GraphPath,
	const FAssetDocumentGraphSpec& DesiredGraph,
	const FAssetDocumentGraphSpec& CurrentGraph)
{
	const TMap<FString, const FAssetDocumentNodeSpec*> DesiredNodes = MapNodesById(DesiredGraph);
	const TMap<FString, const FAssetDocumentNodeSpec*> CurrentNodes = MapNodesById(CurrentGraph);
	const TArray<FString> NodeKeys = SortedUnionKeys(SortedMapKeys(DesiredNodes), SortedMapKeys(CurrentNodes));

	for (const FString& NodeKey : NodeKeys)
	{
		const FAssetDocumentNodeSpec* const* DesiredNode = DesiredNodes.Find(NodeKey);
		const FAssetDocumentNodeSpec* const* CurrentNode = CurrentNodes.Find(NodeKey);
		const FString NodePath = JoinPath(JoinPath(GraphPath, TEXT("Nodes")), NodeKey);
		if (!DesiredNode)
		{
			AddEntry(
				Entries,
				NodePath,
				TEXT("extra"),
				nullptr,
				MakeShared<FJsonValueObject>((*CurrentNode)->ToJsonObject()));
		}
		else if (!CurrentNode)
		{
			AddEntry(
				Entries,
				NodePath,
				TEXT("missing"),
				MakeShared<FJsonValueObject>((*DesiredNode)->ToJsonObject()),
				nullptr);
		}
		else
		{
			CompareNodeDetails(Entries, NodePath, **DesiredNode, **CurrentNode);
			ComparePins(Entries, NodePath, **DesiredNode, **CurrentNode);
		}
	}
}

void CompareGraphListById(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& GraphsPath,
	const TArray<FAssetDocumentGraphSpec>& DesiredGraphs,
	const TArray<FAssetDocumentGraphSpec>& CurrentGraphs);

void CompareGraphDetails(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& GraphPath,
	const FAssetDocumentGraphSpec& DesiredGraph,
	const FAssetDocumentGraphSpec& CurrentGraph)
{
	AddComparisonEntry(
		Entries,
		GraphPath,
		MakeShared<FJsonValueObject>(DesiredGraph.ToJsonObject()),
		MakeShared<FJsonValueObject>(CurrentGraph.ToJsonObject()));
	CompareObjectFields(Entries, JoinPath(GraphPath, TEXT("Owner")), DesiredGraph.Owner, CurrentGraph.Owner);
	if (DesiredGraph.OwnerNodeId != CurrentGraph.OwnerNodeId)
	{
		AddComparisonEntry(
			Entries,
			JoinPath(GraphPath, TEXT("OwnerNodeId")),
			MakeShared<FJsonValueString>(DesiredGraph.OwnerNodeId),
			MakeShared<FJsonValueString>(CurrentGraph.OwnerNodeId));
	}
	if (DesiredGraph.OwnerPin != CurrentGraph.OwnerPin)
	{
		AddComparisonEntry(
			Entries,
			JoinPath(GraphPath, TEXT("OwnerPin")),
			MakeShared<FJsonValueString>(DesiredGraph.OwnerPin),
			MakeShared<FJsonValueString>(CurrentGraph.OwnerPin));
	}
	CompareOptionalValue(Entries, JoinPath(GraphPath, TEXT("EntryPins")), DesiredGraph.EntryPins, CurrentGraph.EntryPins);
	CompareOptionalValue(Entries, JoinPath(GraphPath, TEXT("ResultPins")), DesiredGraph.ResultPins, CurrentGraph.ResultPins);
	CompareObjectAsWhole(
		Entries,
		JoinPath(GraphPath, TEXT("Position")),
		DesiredGraph.Position,
		CurrentGraph.Position,
		TEXT("layout"));
	CompareOptionalValue(Entries, JoinPath(GraphPath, TEXT("Metadata")), DesiredGraph.Metadata, CurrentGraph.Metadata);
	CompareOptionalValue(Entries, JoinPath(GraphPath, TEXT("Diagnostics")), DesiredGraph.Diagnostics, CurrentGraph.Diagnostics);
	CompareOptionalValue(Entries, JoinPath(GraphPath, TEXT("Skipped")), DesiredGraph.Skipped, CurrentGraph.Skipped);
	CompareOptionalValue(Entries, JoinPath(GraphPath, TEXT("_Skipped")), DesiredGraph.UnderscoreSkipped, CurrentGraph.UnderscoreSkipped);
	CompareObjectFields(Entries, JoinPath(GraphPath, TEXT("Evidence")), DesiredGraph.Evidence, CurrentGraph.Evidence);
	CompareNodes(Entries, GraphPath, DesiredGraph, CurrentGraph);
	CompareLinks(Entries, GraphPath, DesiredGraph, CurrentGraph);
	CompareGraphListById(
		Entries,
		JoinPath(GraphPath, TEXT("Subgraphs")),
		DesiredGraph.Subgraphs,
		CurrentGraph.Subgraphs);
}

void CompareGraphListById(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& GraphsPath,
	const TArray<FAssetDocumentGraphSpec>& DesiredGraphs,
	const TArray<FAssetDocumentGraphSpec>& CurrentGraphs)
{
	const TMap<FString, const FAssetDocumentGraphSpec*> DesiredGraphMap = MapGraphsById(DesiredGraphs);
	const TMap<FString, const FAssetDocumentGraphSpec*> CurrentGraphMap = MapGraphsById(CurrentGraphs);
	const TArray<FString> GraphKeys = SortedUnionKeys(SortedMapKeys(DesiredGraphMap), SortedMapKeys(CurrentGraphMap));

	for (const FString& GraphKey : GraphKeys)
	{
		const FAssetDocumentGraphSpec* const* DesiredGraph = DesiredGraphMap.Find(GraphKey);
		const FAssetDocumentGraphSpec* const* CurrentGraph = CurrentGraphMap.Find(GraphKey);
		const FString GraphPath = JoinPath(GraphsPath, GraphKey);
		if (!DesiredGraph)
		{
			AddEntry(
				Entries,
				GraphPath,
				TEXT("extra"),
				nullptr,
				MakeShared<FJsonValueObject>((*CurrentGraph)->ToJsonObject()));
		}
		else if (!CurrentGraph)
		{
			AddEntry(
				Entries,
				GraphPath,
				TEXT("missing"),
				MakeShared<FJsonValueObject>((*DesiredGraph)->ToJsonObject()),
				nullptr);
		}
		else
		{
			CompareGraphDetails(Entries, GraphPath, **DesiredGraph, **CurrentGraph);
		}
	}
}

void AddResolveDiagnosticsAsUnsupported(
	TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const TArray<FAssetDocumentGraphDiagnostic>& Diagnostics)
{
	for (const FAssetDocumentGraphDiagnostic& Diagnostic : Diagnostics)
	{
		Entries.Add(FAssetDocumentGraphDiff::MakeUnsupported(
			Diagnostic.Path,
			FString::Printf(TEXT("%s: %s"), *Diagnostic.Code, *Diagnostic.Message)));
	}
}
}

TSharedRef<FJsonObject> FAssetDocumentGraphDiffEntry::ToJsonObject() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Path"), Path);
	Object->SetStringField(TEXT("Status"), Status);
	if (!Message.IsEmpty())
	{
		Object->SetStringField(TEXT("Message"), Message);
	}
	if (Desired.IsValid())
	{
		Object->SetField(TEXT("Desired"), AssetDocumentGraphJson::CloneJsonValue(Desired));
	}
	if (Current.IsValid())
	{
		Object->SetField(TEXT("Current"), AssetDocumentGraphJson::CloneJsonValue(Current));
	}
	return Object;
}

TArray<FAssetDocumentGraphDiffEntry> FAssetDocumentGraphDiff::CompareUbergraphPages(
	const TArray<FAssetDocumentGraphSpec>& DesiredGraphs,
	const TArray<FAssetDocumentGraphSpec>& CurrentGraphs,
	const TSharedPtr<FJsonObject>& Definitions)
{
	FAssetDocumentGraphDefinitionResolveOptions ResolveOptions;
	ResolveOptions.GraphsPath = TEXT("/Body/UbergraphPages");
	const FAssetDocumentGraphDefinitionResolveResult DesiredResolveResult =
		FAssetDocumentGraphDefinitionResolver::ResolveGraphArray(DesiredGraphs, Definitions, ResolveOptions);
	const FAssetDocumentGraphDefinitionResolveResult CurrentResolveResult =
		FAssetDocumentGraphDefinitionResolver::ResolveGraphArray(CurrentGraphs, Definitions, ResolveOptions);

	TArray<FAssetDocumentGraphDiffEntry> Entries;
	AddResolveDiagnosticsAsUnsupported(Entries, DesiredResolveResult.Diagnostics);
	AddResolveDiagnosticsAsUnsupported(Entries, CurrentResolveResult.Diagnostics);
	if (!DesiredResolveResult.IsValid() || !CurrentResolveResult.IsValid())
	{
		return Entries;
	}

	const TMap<FString, const FAssetDocumentGraphSpec*> DesiredGraphMap = MapGraphsByName(DesiredResolveResult.Graphs);
	const TMap<FString, const FAssetDocumentGraphSpec*> CurrentGraphMap = MapGraphsByName(CurrentResolveResult.Graphs);
	const TArray<FString> GraphKeys = SortedUnionKeys(SortedMapKeys(DesiredGraphMap), SortedMapKeys(CurrentGraphMap));

	for (const FString& GraphKey : GraphKeys)
	{
		const FAssetDocumentGraphSpec* const* DesiredGraph = DesiredGraphMap.Find(GraphKey);
		const FAssetDocumentGraphSpec* const* CurrentGraph = CurrentGraphMap.Find(GraphKey);
		const FString GraphPath = JoinPath(TEXT("/Body/UbergraphPages"), GraphKey);
		if (!DesiredGraph)
		{
			AddEntry(
				Entries,
				GraphPath,
				TEXT("extra"),
				nullptr,
				MakeShared<FJsonValueObject>((*CurrentGraph)->ToJsonObject()));
		}
		else if (!CurrentGraph)
		{
			AddEntry(
				Entries,
				GraphPath,
				TEXT("missing"),
				MakeShared<FJsonValueObject>((*DesiredGraph)->ToJsonObject()),
				nullptr);
		}
		else
		{
			AddComparisonEntry(
				Entries,
				GraphPath,
				MakeShared<FJsonValueObject>((*DesiredGraph)->ToJsonObject()),
				MakeShared<FJsonValueObject>((*CurrentGraph)->ToJsonObject()));
			CompareNodes(Entries, GraphPath, **DesiredGraph, **CurrentGraph);
			CompareLinks(Entries, GraphPath, **DesiredGraph, **CurrentGraph);
		}
	}

	return Entries;
}

TArray<FAssetDocumentGraphDiffEntry> FAssetDocumentGraphDiff::CompareGraphRegion(
	const TArray<FAssetDocumentGraphSpec>& DesiredGraphs,
	const TArray<FAssetDocumentGraphSpec>& CurrentGraphs,
	const FString& RegionPath)
{
	TArray<FAssetDocumentGraphDiffEntry> Entries;
	CompareGraphListById(Entries, JoinPath(RegionPath, TEXT("Graphs")), DesiredGraphs, CurrentGraphs);
	return Entries;
}

FAssetDocumentGraphDiffEntry FAssetDocumentGraphDiff::MakeUnsupported(const FString& Path, const FString& Message)
{
	FAssetDocumentGraphDiffEntry Entry;
	Entry.Path = Path;
	Entry.Status = TEXT("unsupported");
	Entry.Message = Message;
	return Entry;
}
