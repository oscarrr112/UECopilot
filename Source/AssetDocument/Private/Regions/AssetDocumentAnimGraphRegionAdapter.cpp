// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentAnimGraphRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

#include "Dom/JsonValue.h"

namespace
{
constexpr const TCHAR* AnimGraphPath = TEXT("/Body/AnimGraph");
constexpr const TCHAR* CanonicalGraphName = TEXT("AnimGraph");
constexpr const TCHAR* CanonicalOutputPin = TEXT("Result");

FAssetDocumentCapabilityResult Failure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

TSharedRef<FJsonObject> MakeCanonicalGraphObject()
{
	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Name"), CanonicalGraphName);
	Graph->SetArrayField(TEXT("Nodes"), {});

	TSharedRef<FJsonObject> OutputPose = MakeShared<FJsonObject>();
	OutputPose->SetField(TEXT("Node"), MakeShared<FJsonValueNull>());
	OutputPose->SetStringField(TEXT("Pin"), CanonicalOutputPin);
	Graph->SetObjectField(TEXT("OutputPose"), OutputPose);
	return Graph;
}

TSharedPtr<FJsonValue> MakeCanonicalValue()
{
	TArray<TSharedPtr<FJsonValue>> Graphs;
	Graphs.Add(MakeShared<FJsonValueObject>(MakeCanonicalGraphObject()));
	return MakeShared<FJsonValueArray>(MoveTemp(Graphs));
}

bool IsEmptyCompatibilityValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return true;
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

FAssetDocumentCapabilityResult ValidateOutputPose(const TSharedPtr<FJsonObject>& OutputPose)
{
	if (!OutputPose.IsValid())
	{
		return Failure(
			TEXT("/Body/AnimGraph/AnimGraph/OutputPose"),
			TEXT("InvalidAnimGraphOutputPose"),
			TEXT("Body.AnimGraph root-only pilot requires OutputPose object"));
	}

	const TSharedPtr<FJsonValue>* NodeValue = OutputPose->Values.Find(TEXT("Node"));
	if (!NodeValue || !NodeValue->IsValid() || (*NodeValue)->Type != EJson::Null)
	{
		return Failure(
			TEXT("/Body/AnimGraph/AnimGraph/OutputPose/Node"),
			TEXT("UnsupportedAnimGraphOutputSource"),
			TEXT("Body.AnimGraph root-only pilot only supports null OutputPose.Node"));
	}

	FString Pin;
	if (!OutputPose->TryGetStringField(TEXT("Pin"), Pin) || Pin != CanonicalOutputPin)
	{
		return Failure(
			TEXT("/Body/AnimGraph/AnimGraph/OutputPose/Pin"),
			TEXT("UnsupportedAnimGraphOutputPin"),
			TEXT("Body.AnimGraph root-only pilot only supports OutputPose.Pin='Result'"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : OutputPose->Values)
	{
		if (Pair.Key != TEXT("Node") && Pair.Key != TEXT("Pin"))
		{
			return Failure(
				FString::Printf(TEXT("/Body/AnimGraph/AnimGraph/OutputPose/%s"), *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Pair.Key)),
				TEXT("UnknownAnimGraphField"),
				FString::Printf(TEXT("Unknown Body.AnimGraph OutputPose field '%s'"), *Pair.Key));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateCanonicalRootGraph(const TSharedPtr<FJsonObject>& Graph)
{
	if (!Graph.IsValid())
	{
		return Failure(
			TEXT("/Body/AnimGraph/0"),
			TEXT("InvalidAnimGraph"),
			TEXT("Body.AnimGraph entries must be graph objects"));
	}

	FString Name;
	if (!Graph->TryGetStringField(TEXT("Name"), Name) || Name != CanonicalGraphName)
	{
		return Failure(
			TEXT("/Body/AnimGraph/0/Name"),
			TEXT("InvalidAnimGraphName"),
			TEXT("Body.AnimGraph pilot only supports Name='AnimGraph'"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
	{
		return Failure(
			TEXT("/Body/AnimGraph/AnimGraph/Nodes"),
			TEXT("InvalidAnimGraphNodes"),
			TEXT("Body.AnimGraph root-only pilot requires Nodes array"));
	}
	if (Nodes->Num() > 0)
	{
		return Failure(
			TEXT("/Body/AnimGraph/AnimGraph/Nodes/0"),
			TEXT("UnsupportedAnimGraphNode"),
			TEXT("Body.AnimGraph pilot does not support authored pose nodes yet"));
	}

	const TSharedPtr<FJsonObject>* OutputPose = nullptr;
	if (!Graph->TryGetObjectField(TEXT("OutputPose"), OutputPose) || !OutputPose || !OutputPose->IsValid())
	{
		return Failure(
			TEXT("/Body/AnimGraph/AnimGraph/OutputPose"),
			TEXT("InvalidAnimGraphOutputPose"),
			TEXT("Body.AnimGraph root-only pilot requires OutputPose object"));
	}

	const FAssetDocumentCapabilityResult OutputResult = ValidateOutputPose(*OutputPose);
	if (!OutputResult.bSuccess)
	{
		return OutputResult;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Graph->Values)
	{
		if (Pair.Key != TEXT("Name") && Pair.Key != TEXT("Nodes") && Pair.Key != TEXT("OutputPose"))
		{
			return Failure(
				FString::Printf(TEXT("/Body/AnimGraph/AnimGraph/%s"), *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Pair.Key)),
				TEXT("UnknownAnimGraphField"),
				FString::Printf(TEXT("Unknown Body.AnimGraph graph field '%s'"), *Pair.Key));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateAnimGraphValue(const TSharedPtr<FJsonValue>& DesiredValue)
{
	if (IsEmptyCompatibilityValue(DesiredValue))
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (DesiredValue->Type != EJson::Array)
	{
		return Failure(
			AnimGraphPath,
			TEXT("InvalidAnimGraphRegionType"),
			TEXT("Body.AnimGraph must be null, empty object, empty array, or canonical root-only graph array"));
	}

	const TArray<TSharedPtr<FJsonValue>>& Graphs = DesiredValue->AsArray();
	if (Graphs.Num() != 1)
	{
		return Failure(
			AnimGraphPath,
			TEXT("InvalidAnimGraphCount"),
			TEXT("Body.AnimGraph pilot supports exactly one AnimGraph graph object"));
	}

	const TSharedPtr<FJsonObject> Graph = Graphs[0].IsValid() ? Graphs[0]->AsObject() : nullptr;
	return ValidateCanonicalRootGraph(Graph);
}

TSharedPtr<FJsonValue> CanonicalizeDesiredValue(const TSharedPtr<FJsonValue>& DesiredValue)
{
	if (IsEmptyCompatibilityValue(DesiredValue))
	{
		return MakeCanonicalValue();
	}
	return DesiredValue;
}
}

FAssetDocumentAnimGraphRegionAdapter::FAssetDocumentAnimGraphRegionAdapter(FName InAdapterName)
	: AdapterName(InAdapterName)
{
}

FName FAssetDocumentAnimGraphRegionAdapter::GetName() const
{
	return AdapterName;
}

bool FAssetDocumentAnimGraphRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return Context.RegionId == TEXT("Body.AnimGraph");
}

TSharedRef<FJsonObject> FAssetDocumentAnimGraphRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Kind"), TEXT("AnimGraphRootOnlyPilot"));
	Schema->SetStringField(TEXT("Shape"), TEXT("array<{Name:'AnimGraph', Nodes:[], OutputPose:{Node:null, Pin:'Result'}}>"));
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentAnimGraphRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext&,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return ValidateAnimGraphValue(DesiredValue);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimGraphRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext&,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;
	return ValidateAnimGraphValue(DesiredValue);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimGraphRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext&,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	OutCurrentValue = MakeCanonicalValue();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentAnimGraphRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext&,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	const FAssetDocumentCapabilityResult ValidateResult = ValidateAnimGraphValue(DesiredValue);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	const TSharedPtr<FJsonValue> Current = MakeCanonicalValue();
	const TSharedPtr<FJsonValue> Desired = CanonicalizeDesiredValue(DesiredValue);
	const bool bChanged =
		FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Current)
		!= FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Desired);

	FAssetDocumentJsonRegionUtils::AddDiffEntry(
		OutDiffEntries,
		TEXT("/Body/AnimGraph/AnimGraph"),
		bChanged ? TEXT("changed") : TEXT("unchanged"),
		Current,
		Desired);

	return FAssetDocumentCapabilityResult::Success();
}
