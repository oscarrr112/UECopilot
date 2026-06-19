// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentGraphDefinitionResolver.h"

namespace
{
bool IsDefinitionId(const FString& Value)
{
	if (Value.IsEmpty())
	{
		return false;
	}

	const TCHAR First = Value[0];
	if (!FChar::IsAlpha(First) && First != TEXT('_'))
	{
		return false;
	}

	for (int32 Index = 1; Index < Value.Len(); ++Index)
	{
		const TCHAR Character = Value[Index];
		if (!FChar::IsAlnum(Character) &&
			Character != TEXT('_') &&
			Character != TEXT('.') &&
			Character != TEXT(':') &&
			Character != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

bool IsKnownDefinitionKind(const FString& Kind)
{
	return Kind == TEXT("DefinitionRef") ||
		Kind == TEXT("ClassRef") ||
		Kind == TEXT("AssetRef") ||
		Kind == TEXT("MemberRef") ||
		Kind == TEXT("PinType") ||
		Kind == TEXT("Literal");
}

FString JoinPath(const FString& BasePath, const FString& Segment)
{
	if (BasePath.IsEmpty())
	{
		return FString::Printf(TEXT("/%s"), *Segment);
	}
	return FString::Printf(TEXT("%s/%s"), *BasePath, *Segment);
}

FString IndexPath(const FString& BasePath, int32 Index)
{
	return JoinPath(BasePath, FString::FromInt(Index));
}

class FResolverContext
{
public:
	FResolverContext(
		const TSharedPtr<FJsonObject>& InDefinitions,
		const FAssetDocumentGraphDefinitionResolveOptions& InOptions,
		FAssetDocumentGraphDefinitionResolveResult& InResult)
		: Definitions(InDefinitions)
		, Options(InOptions)
		, Result(InResult)
	{
	}

	void ValidateDefinitions()
	{
		if (!Definitions.IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Definitions->Values)
		{
			const FString DefinitionPath = JoinPath(Options.DefinitionsPath, Pair.Key);
			if (!IsDefinitionId(Pair.Key))
			{
				Result.AddDiagnostic(
					TEXT("InvalidDefinitionId"),
					DefinitionPath,
					TEXT("Definition id must match ^[A-Za-z_][A-Za-z0-9_.:-]*$."));
				continue;
			}

			const TSharedPtr<FJsonObject> Object = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
			if (!Object.IsValid())
			{
				Result.AddDiagnostic(
					TEXT("UnknownDefinitionKind"),
					DefinitionPath,
					TEXT("Definition must be an object with a supported Kind."));
				continue;
			}

			FString Kind;
			if (!Object->TryGetStringField(TEXT("Kind"), Kind) || !IsKnownDefinitionKind(Kind))
			{
				Result.AddDiagnostic(
					TEXT("UnknownDefinitionKind"),
					JoinPath(DefinitionPath, TEXT("Kind")),
					FString::Printf(TEXT("Unknown definition kind '%s'."), *Kind));
			}
		}
	}

	TSharedPtr<FJsonValue> ResolveValue(
		const TSharedPtr<FJsonValue>& Value,
		const FString& Path,
		TArray<FString>& Stack)
	{
		if (!Value.IsValid())
		{
			return nullptr;
		}

		switch (Value->Type)
		{
		case EJson::Object:
			return MakeShared<FJsonValueObject>(ResolveObject(Value->AsObject(), Path, Stack));
		case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> ResolvedItems;
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				ResolvedItems.Add(ResolveValue(Items[Index], IndexPath(Path, Index), Stack));
			}
			return MakeShared<FJsonValueArray>(MoveTemp(ResolvedItems));
		}
		default:
			return AssetDocumentGraphJson::CloneJsonValue(Value);
		}
	}

	TSharedPtr<FJsonObject> ResolveObject(
		const TSharedPtr<FJsonObject>& Object,
		const FString& Path,
		TArray<FString>& Stack)
	{
		if (!Object.IsValid())
		{
			return nullptr;
		}

		FString Kind;
		if (Object->TryGetStringField(TEXT("Kind"), Kind))
		{
			if (!IsKnownDefinitionKind(Kind))
			{
				Result.AddDiagnostic(
					TEXT("UnknownDefinitionKind"),
					JoinPath(Path, TEXT("Kind")),
					FString::Printf(TEXT("Unknown definition kind '%s'."), *Kind));
			}

			if (Kind == TEXT("DefinitionRef"))
			{
				FString Id;
				if (!Object->TryGetStringField(TEXT("Id"), Id) || Id.IsEmpty())
				{
					Result.AddDiagnostic(
						TEXT("UnresolvedDefinitionReference"),
						JoinPath(Path, TEXT("Id")),
						TEXT("DefinitionRef requires a non-empty Id."));
					return AssetDocumentGraphJson::CloneJsonObject(Object);
				}

				const TSharedPtr<FJsonValue> Resolved = ResolveDefinition(Id, Path, Stack);
				return Resolved.IsValid() && Resolved->Type == EJson::Object
					? Resolved->AsObject()
					: AssetDocumentGraphJson::CloneJsonObject(Object);
			}
		}

		TSharedRef<FJsonObject> Resolved = MakeShared<FJsonObject>();
		TArray<FString> Keys;
		Object->Values.GetKeys(Keys);
		Keys.Sort();
		for (const FString& Key : Keys)
		{
			Resolved->SetField(Key, ResolveValue(Object->Values[Key], JoinPath(Path, Key), Stack));
		}
		return Resolved;
	}

	TSharedPtr<FJsonValue> ResolveDefinition(
		const FString& Id,
		const FString& Path,
		TArray<FString>& Stack)
	{
		if (!Definitions.IsValid())
		{
			Result.AddDiagnostic(
				TEXT("UnresolvedDefinitionReference"),
				Path,
				FString::Printf(TEXT("Definition '%s' could not be resolved."), *Id));
			return nullptr;
		}

		const TSharedPtr<FJsonValue>* DefinitionValue = Definitions->Values.Find(Id);
		if (!DefinitionValue || !DefinitionValue->IsValid())
		{
			Result.AddDiagnostic(
				TEXT("UnresolvedDefinitionReference"),
				Path,
				FString::Printf(TEXT("Definition '%s' could not be resolved."), *Id));
			return nullptr;
		}

		if (Stack.Contains(Id))
		{
			Result.AddDiagnostic(
				TEXT("CircularDefinitionReference"),
				Path,
				FString::Printf(TEXT("Definition '%s' participates in a circular reference."), *Id));
			return nullptr;
		}

		Stack.Add(Id);
		const TSharedPtr<FJsonValue> Resolved = ResolveValue(
			*DefinitionValue,
			JoinPath(Options.DefinitionsPath, Id),
			Stack);
		Stack.Pop(EAllowShrinking::No);
		return Resolved;
	}

private:
	const TSharedPtr<FJsonObject>& Definitions;
	const FAssetDocumentGraphDefinitionResolveOptions& Options;
	FAssetDocumentGraphDefinitionResolveResult& Result;
};

FAssetDocumentGraphSpec ResolveGraph(
	const FAssetDocumentGraphSpec& Graph,
	const FString& GraphPath,
	FResolverContext& Context)
{
	FAssetDocumentGraphSpec Resolved = Graph;
	Resolved.Signature = nullptr;
	if (Graph.Signature.IsValid())
	{
		TArray<FString> Stack;
		Resolved.Signature = Context.ResolveObject(Graph.Signature, JoinPath(GraphPath, TEXT("Signature")), Stack);
	}

	Resolved.Nodes.Reset();
	for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
	{
		const FAssetDocumentNodeSpec& Node = Graph.Nodes[NodeIndex];
		const FString NodePath = IndexPath(JoinPath(GraphPath, TEXT("Nodes")), NodeIndex);
		FAssetDocumentNodeSpec ResolvedNode = Node;
		ResolvedNode.Member = nullptr;
		if (Node.Member.IsValid())
		{
			TArray<FString> Stack;
			ResolvedNode.Member = Context.ResolveObject(Node.Member, JoinPath(NodePath, TEXT("Member")), Stack);
		}
		ResolvedNode.Position = AssetDocumentGraphJson::CloneJsonObject(Node.Position);

		ResolvedNode.PinOverrides.Reset();
		for (int32 PinIndex = 0; PinIndex < Node.PinOverrides.Num(); ++PinIndex)
		{
			const FAssetDocumentPinOverrideSpec& PinOverride = Node.PinOverrides[PinIndex];
			const FString PinPath = IndexPath(JoinPath(NodePath, TEXT("PinOverrides")), PinIndex);
			FAssetDocumentPinOverrideSpec ResolvedPin = PinOverride;
			TArray<FString> Stack;
			ResolvedPin.Type = Context.ResolveValue(PinOverride.Type, JoinPath(PinPath, TEXT("Type")), Stack);
			Stack.Reset();
			ResolvedPin.DefaultValue = Context.ResolveValue(PinOverride.DefaultValue, JoinPath(PinPath, TEXT("DefaultValue")), Stack);
			Stack.Reset();
			ResolvedPin.DefaultObject = Context.ResolveValue(PinOverride.DefaultObject, JoinPath(PinPath, TEXT("DefaultObject")), Stack);
			Stack.Reset();
			ResolvedPin.DefaultTextValue =
				Context.ResolveValue(PinOverride.DefaultTextValue, JoinPath(PinPath, TEXT("DefaultTextValue")), Stack);
			ResolvedNode.PinOverrides.Add(MoveTemp(ResolvedPin));
		}

		Resolved.Nodes.Add(MoveTemp(ResolvedNode));
	}

	return Resolved;
}
}

void FAssetDocumentGraphDefinitionResolveResult::AddDiagnostic(
	const FString& Code,
	const FString& Path,
	const FString& Message)
{
	FAssetDocumentGraphDiagnostic Diagnostic;
	Diagnostic.Code = Code;
	Diagnostic.Path = Path;
	Diagnostic.Message = Message;
	Diagnostics.Add(MoveTemp(Diagnostic));
}

FAssetDocumentGraphDefinitionResolveResult FAssetDocumentGraphDefinitionResolver::ResolveGraphArray(
	const TArray<FAssetDocumentGraphSpec>& Graphs,
	const TSharedPtr<FJsonObject>& Definitions,
	const FAssetDocumentGraphDefinitionResolveOptions& Options)
{
	FAssetDocumentGraphDefinitionResolveResult Result;
	FResolverContext Context(Definitions, Options, Result);
	Context.ValidateDefinitions();

	if (Definitions.IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Definitions->Values)
		{
			TArray<FString> Stack;
			Context.ResolveValue(Pair.Value, JoinPath(Options.DefinitionsPath, Pair.Key), Stack);
		}
	}

	for (int32 Index = 0; Index < Graphs.Num(); ++Index)
	{
		Result.Graphs.Add(ResolveGraph(Graphs[Index], IndexPath(Options.GraphsPath, Index), Context));
	}

	return Result;
}
