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

bool IsGeneratedObjectIdentityField(const FString& FieldName)
{
	return FieldName == TEXT("ObjectPath")
		|| FieldName == TEXT("ObjectName")
		|| FieldName == TEXT("Outer")
		|| FieldName == TEXT("Package")
		|| FieldName == TEXT("ClassGeneratedBy")
		|| FieldName == TEXT("SkeletonGeneratedClass");
}

bool IsGeneratedObjectIdentityValue(const FString& Value)
{
	if (Value.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	return Value.Contains(TEXT("/Engine/Transient"), ESearchCase::IgnoreCase)
		|| Value.Contains(TEXT("TransientPackage"), ESearchCase::IgnoreCase)
		|| Value.Contains(TEXT("REINST_"), ESearchCase::CaseSensitive)
		|| Value.Contains(TEXT("SKEL_"), ESearchCase::CaseSensitive)
		|| Value.Contains(TEXT("TRASHCLASS_"), ESearchCase::CaseSensitive)
		|| Value.Contains(TEXT("PLACEHOLDER-CLASS"), ESearchCase::CaseSensitive);
}

FString NormalizeGeneratedObjectIdentityValue(const FString& FieldName, const FString& Value)
{
	if (!IsGeneratedObjectIdentityField(FieldName) || !IsGeneratedObjectIdentityValue(Value))
	{
		return Value;
	}

	return FString::Printf(TEXT("<generated-object-identity:%s>"), *FieldName);
}

bool IsAuthoredPropertiesSubtree(const FString& FieldName)
{
	return FieldName == TEXT("Properties");
}

void NormalizeGeneratedObjectPathFields(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (!Object.IsValid())
		{
			return;
		}

		TArray<FString> FieldNames;
		Object->Values.GenerateKeyArray(FieldNames);
		for (const FString& FieldName : FieldNames)
		{
			if (IsAuthoredPropertiesSubtree(FieldName))
			{
				continue;
			}

			TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
			if (!FieldValue || !FieldValue->IsValid())
			{
				continue;
			}

			if ((*FieldValue)->Type == EJson::String)
			{
				const FString NormalizedValue = NormalizeGeneratedObjectIdentityValue(FieldName, (*FieldValue)->AsString());
				if (NormalizedValue != (*FieldValue)->AsString())
				{
					Object->SetStringField(FieldName, NormalizedValue);
				}
				continue;
			}

			NormalizeGeneratedObjectPathFields(*FieldValue);
		}
		return;
	}

	if (Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
		{
			NormalizeGeneratedObjectPathFields(Entry);
		}
	}
}

bool IsGeneratedDiagnosticContainerField(const FString& FieldName)
{
	return FieldName.StartsWith(TEXT("_"))
		|| FieldName == TEXT("GeneratedDiagnostics")
		|| FieldName == TEXT("UnsupportedGraphDiagnostics")
		|| FieldName == TEXT("ValidationDiagnostics")
		|| FieldName == TEXT("ProjectionMetrics");
}

bool IsEmptyJsonContainer(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		return Value->AsArray().Num() == 0;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		return Object.IsValid() && Object->Values.Num() == 0;
	}

	return false;
}

void NormalizeEmptyGeneratedContainers(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return;
	}

	if (Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
		{
			NormalizeEmptyGeneratedContainers(Entry);
		}
		return;
	}

	if (Value->Type != EJson::Object)
	{
		return;
	}

	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	if (!Object.IsValid())
	{
		return;
	}

	TArray<FString> FieldNames;
	Object->Values.GenerateKeyArray(FieldNames);

	TArray<FString> FieldsToRemove;
	for (const FString& FieldName : FieldNames)
	{
		if (IsAuthoredPropertiesSubtree(FieldName))
		{
			continue;
		}

		TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
		if (!FieldValue || !FieldValue->IsValid())
		{
			continue;
		}

		NormalizeEmptyGeneratedContainers(*FieldValue);
		if (IsGeneratedDiagnosticContainerField(FieldName) && IsEmptyJsonContainer(*FieldValue))
		{
			FieldsToRemove.Add(FieldName);
		}
	}

	for (const FString& FieldName : FieldsToRemove)
	{
		Object->RemoveField(FieldName);
	}
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

class FAssetDocumentAnimSequencePostApplyCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		TSharedPtr<FJsonValue> CanonicalValue = GetIdentityStrategy().CanonicalizeForHash(Context, RegionValue);
		NormalizeGeneratedObjectPathFields(CanonicalValue);
		NormalizeEmptyGeneratedContainers(CanonicalValue);
		return CanonicalValue;
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return GetIdentityStrategy().CanonicalizeForSidecarWriteback(Context, RegionValue);
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetAnimSequencePostApplyStrategy()
{
	static FAssetDocumentAnimSequencePostApplyCanonicalizationStrategy Strategy;
	return Strategy;
}

const TMap<FName, const IAssetDocumentRegionCanonicalizationStrategy*>& GetBuiltinCanonicalizerStrategies()
{
	static const TMap<FName, const IAssetDocumentRegionCanonicalizationStrategy*> Strategies = {
		{FName(TEXT("AnimSequencePostApply")), &GetAnimSequencePostApplyStrategy()},
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
