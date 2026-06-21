// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentRegionCanonicalizer.h"

#include "AssetDocumentCanonicalJson.h"
#include "Graphs/AssetDocumentGraphParser.h"

#include "Misc/SecureHash.h"

namespace
{
class FAssetDocumentIdentityRegionCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(RegionValue, Context.Policy);
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext&,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(RegionValue);
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetIdentityStrategy()
{
	static FAssetDocumentIdentityRegionCanonicalizationStrategy Strategy;
	return Strategy;
}

FString MakeGraphNodeSemanticKey(const FAssetDocumentNodeSpec& Node)
{
	FString MemberJson;
	if (Node.Member.IsValid())
	{
		MemberJson = FAssetDocumentCanonicalJson::WriteCanonicalJson(
			MakeShared<FJsonValueObject>(Node.Member.ToSharedRef()),
			nullptr);
	}

	return FString::Printf(TEXT("%s|%s"), *Node.Class, *MemberJson);
}

FString MakeSemanticNodeId(const FString& SemanticKey)
{
	const FTCHARToUTF8 Utf8SemanticKey(*SemanticKey);
	const FSHAHash Hash = FSHA1::HashBuffer(
		Utf8SemanticKey.Get(),
		static_cast<uint64>(Utf8SemanticKey.Length()));
	return FString::Printf(TEXT("semantic_%s"), *Hash.ToString().Left(16).ToLower());
}

void RewriteGraphNodeIdsForHash(FAssetDocumentGraphSpec& Graph)
{
	TMap<FString, int32> SemanticKeyCounts;
	TMap<FString, FString> OldNodeIdToSemanticId;
	TMap<FString, int32> FinalIdCounts;

	for (const FAssetDocumentNodeSpec& Node : Graph.Nodes)
	{
		const FString SemanticKey = MakeGraphNodeSemanticKey(Node);
		SemanticKeyCounts.FindOrAdd(SemanticKey)++;
	}

	for (const FAssetDocumentNodeSpec& Node : Graph.Nodes)
	{
		const FString SemanticKey = MakeGraphNodeSemanticKey(Node);
		const FString FinalId = SemanticKeyCounts.FindRef(SemanticKey) == 1
			? MakeSemanticNodeId(SemanticKey)
			: Node.Id;
		FinalIdCounts.FindOrAdd(FinalId)++;
	}

	for (FAssetDocumentNodeSpec& Node : Graph.Nodes)
	{
		const FString SemanticKey = MakeGraphNodeSemanticKey(Node);
		if (SemanticKeyCounts.FindRef(SemanticKey) != 1)
		{
			continue;
		}

		const FString SemanticId = MakeSemanticNodeId(SemanticKey);
		if (FinalIdCounts.FindRef(SemanticId) != 1)
		{
			continue;
		}

		OldNodeIdToSemanticId.Add(Node.Id, SemanticId);
		Node.Id = SemanticId;
	}

	for (FAssetDocumentLinkSpec& Link : Graph.Links)
	{
		if (const FString* FromNodeId = OldNodeIdToSemanticId.Find(Link.From.Node))
		{
			Link.From.Node = *FromNodeId;
		}
		if (const FString* ToNodeId = OldNodeIdToSemanticId.Find(Link.To.Node))
		{
			Link.To.Node = *ToNodeId;
		}
	}
}

TSharedPtr<FJsonValue> CanonicalizeGraphArrayForHash(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	if (!RegionValue.IsValid() || RegionValue->Type != EJson::Array)
	{
		return GetIdentityStrategy().CanonicalizeForHash(Context, RegionValue);
	}

	FAssetDocumentGraphParseOptions ParseOptions;
	ParseOptions.Path = TEXT("");
	FAssetDocumentGraphParseResult ParseResult = FAssetDocumentGraphParser::ParseGraphArray(RegionValue->AsArray(), ParseOptions);
	if (!ParseResult.IsValid())
	{
		return GetIdentityStrategy().CanonicalizeForHash(Context, RegionValue);
	}

	for (FAssetDocumentGraphSpec& Graph : ParseResult.Graphs)
	{
		Graph.GraphGuid.Reset();
		for (FAssetDocumentNodeSpec& Node : Graph.Nodes)
		{
			Node.NodeGuid.Reset();
			Node.Capability.Reset();
		}
		RewriteGraphNodeIdsForHash(Graph);
	}

	return FAssetDocumentGraphParser::WriteCanonicalGraphArray(ParseResult.Graphs);
}

class FAssetDocumentUBlueprintGraphCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return CanonicalizeGraphArrayForHash(Context, RegionValue);
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return GetIdentityStrategy().CanonicalizeForSidecarWriteback(Context, RegionValue);
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetUBlueprintGraphStrategy()
{
	static FAssetDocumentUBlueprintGraphCanonicalizationStrategy Strategy;
	return Strategy;
}

const TMap<FName, const IAssetDocumentRegionCanonicalizationStrategy*>& GetBuiltinCanonicalizerStrategies()
{
	static const TMap<FName, const IAssetDocumentRegionCanonicalizationStrategy*> Strategies = {
		{FName(TEXT("UBlueprintGraph")), &GetUBlueprintGraphStrategy()},
	};
	return Strategies;
}

const IAssetDocumentRegionCanonicalizationStrategy& ResolveStrategy(
	const FAssetDocumentRegionCanonicalizeContext& Context)
{
	const FName HookName = Context.Policy ? Context.Policy->CanonicalizerHookName : NAME_None;
	if (const IAssetDocumentRegionCanonicalizationStrategy* const* Strategy = GetBuiltinCanonicalizerStrategies().Find(HookName))
	{
		return **Strategy;
	}
	return GetIdentityStrategy();
}
}

TSharedPtr<FJsonValue> FAssetDocumentRegionCanonicalizer::CanonicalizeForHash(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return ResolveStrategy(Context).CanonicalizeForHash(Context, RegionValue);
}

TSharedPtr<FJsonValue> FAssetDocumentRegionCanonicalizer::CanonicalizeForSidecarWriteback(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return ResolveStrategy(Context).CanonicalizeForSidecarWriteback(Context, RegionValue);
}

FString FAssetDocumentRegionCanonicalizer::HashRegionValue(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return FAssetDocumentCanonicalJson::HashJsonValue(CanonicalizeForHash(Context, RegionValue), nullptr);
}
