// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/UBlueprintGraphRegionAdapter.h"

#include "Graphs/AssetDocumentGraphDefinitionResolver.h"
#include "Graphs/AssetDocumentGraphParser.h"
#include "Graphs/AssetDocumentNodeAdapter.h"

#include "Dom/JsonValue.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr const TCHAR* UbergraphPagesPath = TEXT("/Body/UbergraphPages");
constexpr const TCHAR* K2NodeCallFunctionClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");

FAssetDocumentCapabilityResult GraphFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult InvalidRegionTypeFailure(const FString& RegionName)
{
	return GraphFailure(
		FString::Printf(TEXT("Body.%s must be an array when authored"), *RegionName),
		FString::Printf(TEXT("/Body/%s"), *RegionName),
		TEXT("InvalidGraphRegionType"));
}

FAssetDocumentCapabilityResult GraphDiagnosticFailure(const FAssetDocumentGraphDiagnostic& Diagnostic)
{
	return GraphFailure(Diagnostic.Message, Diagnostic.Path, Diagnostic.Code);
}

TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Object)
{
	return AssetDocumentGraphJson::CloneJsonObject(Object);
}

FString NodePath(int32 GraphIndex, int32 NodeIndex)
{
	return FString::Printf(TEXT("%s/%d/Nodes/%d"), UbergraphPagesPath, GraphIndex, NodeIndex);
}

UClass* ResolveClass(const FString& ClassPath)
{
	return ClassPath.IsEmpty() ? nullptr : StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
}

bool TryResolveMemberFunction(const TSharedPtr<FJsonObject>& Member)
{
	if (!Member.IsValid())
	{
		return false;
	}

	FString Kind;
	if (!Member->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("MemberRef"))
	{
		return false;
	}

	FString OwnerClassPath;
	FString FunctionName;
	if (!Member->TryGetStringField(TEXT("OwnerClass"), OwnerClassPath)
		|| !Member->TryGetStringField(TEXT("Name"), FunctionName)
		|| OwnerClassPath.IsEmpty()
		|| FunctionName.IsEmpty()
		|| OwnerClassPath == TEXT("Self"))
	{
		return false;
	}

	UClass* OwnerClass = ResolveClass(OwnerClassPath);
	return OwnerClass && OwnerClass->FindFunctionByName(FName(*FunctionName)) != nullptr;
}

FAssetDocumentUnsupportedNodeDiagnostic MakeUnsupportedNodeDiagnostic(
	const FAssetDocumentNodeSpec& Node,
	const FString& Path,
	const FString& Code,
	const FString& Reason,
	const FString& SuggestedAction)
{
	FAssetDocumentUnsupportedNodeDiagnostic Diagnostic;
	Diagnostic.Code = Code;
	Diagnostic.Path = Path;
	Diagnostic.Class = Node.Class;
	Diagnostic.Capability = Node.Capability;
	Diagnostic.Member = CloneJsonObject(Node.Member);
	Diagnostic.Reason = Reason;
	Diagnostic.SuggestedAction = SuggestedAction;
	return Diagnostic;
}

FAssetDocumentCapabilityResult UnsupportedGraphFailure(const FAssetDocumentUnsupportedNodeDiagnostic& Unsupported)
{
	FAssetDocumentCapabilityResult Result = FAssetDocumentCapabilityResult::Failure(
		FString::Printf(TEXT("%s at %s: %s"), *Unsupported.Code, *Unsupported.Path, *Unsupported.Reason),
		Unsupported.Path,
		Unsupported.Code);

	TArray<TSharedPtr<FJsonValue>> UnsupportedDiagnostics;
	UnsupportedDiagnostics.Add(MakeShared<FJsonValueObject>(Unsupported.ToJsonObject()));
	Result.Payload = MakeShared<FJsonObject>();
	Result.Payload->SetArrayField(TEXT("UnsupportedGraphDiagnostics"), MoveTemp(UnsupportedDiagnostics));
	return Result;
}

FAssetDocumentCapabilityResult ValidateUbergraphPages(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (Value->Type != EJson::Array)
	{
		return InvalidRegionTypeFailure(TEXT("UbergraphPages"));
	}

	const TArray<TSharedPtr<FJsonValue>>& GraphValues = Value->AsArray();
	if (GraphValues.IsEmpty())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentGraphParseOptions ParseOptions;
	ParseOptions.Path = UbergraphPagesPath;
	FAssetDocumentGraphParseResult ParseResult = FAssetDocumentGraphParser::ParseGraphArray(GraphValues, ParseOptions);
	if (!ParseResult.IsValid())
	{
		return GraphDiagnosticFailure(ParseResult.Diagnostics[0]);
	}

	FAssetDocumentGraphDefinitionResolveOptions ResolveOptions;
	ResolveOptions.GraphsPath = UbergraphPagesPath;
	const TSharedPtr<FJsonObject> Definitions = Context.Definitions ? *Context.Definitions : nullptr;
	FAssetDocumentGraphDefinitionResolveResult ResolveResult =
		FAssetDocumentGraphDefinitionResolver::ResolveGraphArray(ParseResult.Graphs, Definitions, ResolveOptions);
	if (!ResolveResult.IsValid())
	{
		return GraphDiagnosticFailure(ResolveResult.Diagnostics[0]);
	}

	const FAssetDocumentNodeAdapterRegistry CurrentTierRegistry;
	for (int32 GraphIndex = 0; GraphIndex < ResolveResult.Graphs.Num(); ++GraphIndex)
	{
		const FAssetDocumentGraphSpec& Graph = ResolveResult.Graphs[GraphIndex];
		for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
		{
			const FAssetDocumentNodeSpec& Node = Graph.Nodes[NodeIndex];
			const FString Path = NodePath(GraphIndex, NodeIndex);
			UClass* NodeClass = ResolveClass(Node.Class);
			if (!NodeClass)
			{
				return GraphFailure(
					FString::Printf(TEXT("Failed to resolve graph node class '%s'"), *Node.Class),
					Path / TEXT("Class"),
					TEXT("UnresolvedGraphNodeClass"));
			}

			if (CurrentTierRegistry.FindAdapter(NodeClass).IsValid())
			{
				continue;
			}

			if (Node.Class == K2NodeCallFunctionClassPath && TryResolveMemberFunction(Node.Member))
			{
				return UnsupportedGraphFailure(MakeUnsupportedNodeDiagnostic(
					Node,
					Path,
					TEXT("UnsupportedGraphFunction"),
					TEXT("reflected function resolves, but the current tier has no function adapter for graph apply validation"),
					TEXT("add a thin K2Node_CallFunction adapter for this function pattern or remove the function node from the managed graph")));
			}

			return UnsupportedGraphFailure(MakeUnsupportedNodeDiagnostic(
				Node,
				Path,
				TEXT("UnsupportedGraphNodeClass"),
				TEXT("node class has no registered AssetDocument adapter in the current tier"),
				TEXT("add a thin node adapter for this class or remove the node from the managed graph")));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}
}

FAssetDocumentCapabilityResult FUBlueprintGraphRegionAdapter::ValidateRegions(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject) const
{
	const TSharedPtr<FJsonValue>* UbergraphPagesValue = BodyObject->Values.Find(TEXT("UbergraphPages"));
	return ValidateUbergraphPages(Context, UbergraphPagesValue ? *UbergraphPagesValue : TSharedPtr<FJsonValue>());
}
