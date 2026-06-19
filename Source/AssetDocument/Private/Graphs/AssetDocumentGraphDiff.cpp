// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentGraphDiff.h"

#include "Graphs/AssetDocumentGraphDefinitionResolver.h"

namespace
{
FString EscapeJsonPointerToken(FString Token)
{
	Token.ReplaceInline(TEXT("~"), TEXT("~0"));
	Token.ReplaceInline(TEXT("/"), TEXT("~1"));
	return Token;
}

FString JoinPath(const FString& BasePath, const FString& Segment)
{
	const FString EscapedSegment = EscapeJsonPointerToken(Segment);
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

template <typename ValueType>
TArray<FString> SortedMapKeys(const TMap<FString, ValueType>& Map)
{
	TArray<FString> Keys;
	Map.GetKeys(Keys);
	Keys.Sort();
	return Keys;
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
			AddComparisonEntry(
				Entries,
				NodePath,
				MakeShared<FJsonValueObject>((*DesiredNode)->ToJsonObject()),
				MakeShared<FJsonValueObject>((*CurrentNode)->ToJsonObject()));
			ComparePins(Entries, NodePath, **DesiredNode, **CurrentNode);
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

FAssetDocumentGraphDiffEntry FAssetDocumentGraphDiff::MakeUnsupported(const FString& Path, const FString& Message)
{
	FAssetDocumentGraphDiffEntry Entry;
	Entry.Path = Path;
	Entry.Status = TEXT("unsupported");
	Entry.Message = Message;
	return Entry;
}
