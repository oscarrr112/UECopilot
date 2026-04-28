// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeLinkResolver.h"

#include "StateTreeEditorNode.h"
#include "StateTreeState.h"

namespace
{
	FString LeafNameFromPath(const FString& CanonicalPath)
	{
		FString Left;
		FString Right;
		if (CanonicalPath.Split(TEXT("/"), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return Right;
		}
		return CanonicalPath;
	}

	FString JoinStatePaths(const FAFStateTreeStateIndex& Index, const TArray<UStateTreeState*>& States)
	{
		TArray<FString> Paths;
		for (const UStateTreeState* State : States)
		{
			if (const FString* Path = Index.PathByState.Find(State))
			{
				Paths.Add(*Path);
			}
		}
		Paths.Sort();
		return FString::Join(Paths, TEXT(", "));
	}
}

bool UE::AssetFactory::StateTree::RegisterStateReference(
	FAFStateTreeStateIndex& Index,
	const FString& Id,
	const FString& CanonicalPath,
	UStateTreeState& State,
	FString& OutError)
{
	if (CanonicalPath.IsEmpty())
	{
		OutError = TEXT("StateTree state canonical path must be non-empty");
		return false;
	}
	if (Index.ByPath.Contains(CanonicalPath))
	{
		OutError = FString::Printf(TEXT("Duplicate StateTree state path '%s'"), *CanonicalPath);
		return false;
	}
	if (!Id.IsEmpty() && Index.ById.Contains(Id))
	{
		OutError = FString::Printf(TEXT("Duplicate StateTree state id '%s'"), *Id);
		return false;
	}

	Index.ByPath.Add(CanonicalPath, &State);
	if (State.ID.IsValid())
	{
		if (Index.ByGuid.Contains(State.ID))
		{
			OutError = FString::Printf(
				TEXT("Duplicate StateTree state GUID '%s'"),
				*State.ID.ToString(EGuidFormats::DigitsWithHyphensLower));
			return false;
		}
		Index.ByGuid.Add(State.ID, &State);
	}
	Index.PathByState.Add(&State, CanonicalPath);
	Index.ByName.FindOrAdd(LeafNameFromPath(CanonicalPath)).Add(&State);
	if (!Id.IsEmpty())
	{
		Index.ById.Add(Id, &State);
	}

	return true;
}

bool UE::AssetFactory::StateTree::RegisterNodeAlias(
	FAFStateTreeStateIndex& Index,
	const FString& Id,
	const FStateTreeEditorNode& Node,
	FString& OutError)
{
	if (Id.IsEmpty())
	{
		return true;
	}

	FGuid ParsedGuid;
	if (FGuid::Parse(Id, ParsedGuid))
	{
		return true;
	}

	if (!Node.ID.IsValid())
	{
		OutError = FString::Printf(TEXT("StateTree node id '%s' produced an invalid GUID"), *Id);
		return false;
	}

	if (const FString* ExistingAlias = Index.NodeAliasByGuid.Find(Node.ID))
	{
		if (*ExistingAlias != Id)
		{
			OutError = FString::Printf(
				TEXT("StateTree node GUID '%s' has conflicting aliases '%s' and '%s'"),
				*Node.ID.ToString(EGuidFormats::DigitsWithHyphensLower),
				**ExistingAlias,
				*Id);
			return false;
		}
		return true;
	}

	Index.NodeAliasByGuid.Add(Node.ID, Id);
	return true;
}

bool UE::AssetFactory::StateTree::ResolveStateReference(
	const FAFStateTreeStateIndex& Index,
	const FString& Reference,
	UStateTreeState*& OutState,
	FString& OutError)
{
	OutState = nullptr;
	if (Reference.IsEmpty())
	{
		OutError = TEXT("StateTree state reference must be non-empty");
		return false;
	}

	FGuid ParsedGuid;
	if (FGuid::Parse(Reference, ParsedGuid))
	{
		if (UStateTreeState* const* StateByGuid = Index.ByGuid.Find(ParsedGuid))
		{
			OutState = *StateByGuid;
			return true;
		}
	}

	if (UStateTreeState* const* StateById = Index.ById.Find(Reference))
	{
		OutState = *StateById;
		return true;
	}
	if (UStateTreeState* const* StateByPath = Index.ByPath.Find(Reference))
	{
		OutState = *StateByPath;
		return true;
	}
	if (const TArray<UStateTreeState*>* StatesByName = Index.ByName.Find(Reference))
	{
		if (StatesByName->Num() == 1)
		{
			OutState = (*StatesByName)[0];
			return true;
		}
		OutError = FString::Printf(
			TEXT("StateTree state reference '%s' is ambiguous; matches: %s"),
			*Reference,
			*JoinStatePaths(Index, *StatesByName));
		return false;
	}

	OutError = FString::Printf(TEXT("StateTree state reference '%s' was not found"), *Reference);
	return false;
}

FStateTreeStateLink UE::AssetFactory::StateTree::MakeStateLink(EStateTreeTransitionType Type, const UStateTreeState* State)
{
	if (Type == EStateTreeTransitionType::GotoState && State)
	{
		return State->GetLinkToState();
	}
	return FStateTreeStateLink(Type);
}
