// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentAnimStateMachineRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

#include "Dom/JsonValue.h"

namespace
{
constexpr const TCHAR* StateMachinesRegionId = TEXT("Body.StateMachines");
constexpr const TCHAR* TransitionGraphsRegionId = TEXT("Body.TransitionGraphs");
constexpr const TCHAR* TransitionResultPin = TEXT("CanEnterTransition");

FAssetDocumentCapabilityResult Failure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FString Escape(const FString& Token)
{
	return FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Token);
}

FString NormalizeIdentity(const FString& Identity)
{
	return FName(*Identity).ToString().ToLower();
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

TSharedPtr<FJsonValue> MakeEmptyArrayValue()
{
	return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>());
}

bool TryReadRequiredString(
	const TSharedPtr<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	const FString& MissingCode,
	const FString& InvalidCode,
	FString& OutValue,
	FAssetDocumentCapabilityResult& OutResult)
{
	if (!Object.IsValid() || !Object->TryGetStringField(FieldName, OutValue))
	{
		OutResult = Failure(Path, MissingCode, FString::Printf(TEXT("%s is required"), *Path));
		return false;
	}
	OutValue = OutValue.TrimStartAndEnd();
	if (OutValue.IsEmpty())
	{
		OutResult = Failure(Path, InvalidCode, FString::Printf(TEXT("%s must not be empty"), *Path));
		return false;
	}
	return true;
}

FAssetDocumentCapabilityResult ValidateNoUnknownFields(
	const TSharedPtr<FJsonObject>& Object,
	const TSet<FString>& KnownFields,
	const FString& Path)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (!KnownFields.Contains(Pair.Key))
		{
			return Failure(
				FString::Printf(TEXT("%s/%s"), *Path, *Escape(Pair.Key)),
				TEXT("UnknownAnimStateMachineField"),
				FString::Printf(TEXT("Unknown state-machine field '%s'"), *Pair.Key));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateTransitionResult(
	const TSharedPtr<FJsonObject>& ResultObject,
	const FString& Path)
{
	if (!ResultObject.IsValid())
	{
		return Failure(Path, TEXT("InvalidTransitionGraphResult"), TEXT("Transition graph Result must be an object"));
	}

	const TSharedPtr<FJsonValue>* NodeValue = ResultObject->Values.Find(TEXT("Node"));
	if (!NodeValue || !NodeValue->IsValid() || (*NodeValue)->Type != EJson::Null)
	{
		return Failure(
			Path / TEXT("Node"),
			TEXT("UnsupportedTransitionGraphResultSource"),
			TEXT("Transition graph pilot only supports Result.Node=null"));
	}

	FString Pin;
	if (!ResultObject->TryGetStringField(TEXT("Pin"), Pin) || Pin != TransitionResultPin)
	{
		return Failure(
			Path / TEXT("Pin"),
			TEXT("UnsupportedTransitionGraphResultPin"),
			TEXT("Transition graph pilot only supports Result.Pin='CanEnterTransition'"));
	}

	return ValidateNoUnknownFields(ResultObject, {TEXT("Node"), TEXT("Pin")}, Path);
}

FAssetDocumentCapabilityResult ValidateStateMachinesValue(const TSharedPtr<FJsonValue>& DesiredValue)
{
	if (IsEmptyCompatibilityValue(DesiredValue))
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (DesiredValue->Type != EJson::Array)
	{
		return Failure(TEXT("/Body/StateMachines"), TEXT("InvalidStateMachinesRegionType"), TEXT("Body.StateMachines must be an array"));
	}

	TSet<FString> MachineIdentities;
	const TArray<TSharedPtr<FJsonValue>>& Machines = DesiredValue->AsArray();
	for (int32 MachineIndex = 0; MachineIndex < Machines.Num(); ++MachineIndex)
	{
		const TSharedPtr<FJsonObject> Machine = Machines[MachineIndex].IsValid() ? Machines[MachineIndex]->AsObject() : nullptr;
		if (!Machine.IsValid())
		{
			return Failure(
				FString::Printf(TEXT("/Body/StateMachines/%d"), MachineIndex),
				TEXT("InvalidStateMachine"),
				TEXT("Body.StateMachines entries must be objects"));
		}

		FAssetDocumentCapabilityResult Result = ValidateNoUnknownFields(
			Machine,
			{TEXT("Name"), TEXT("EntryState"), TEXT("States"), TEXT("Transitions")},
			FString::Printf(TEXT("/Body/StateMachines/%d"), MachineIndex));
		if (!Result.bSuccess)
		{
			return Result;
		}

		FString MachineName;
		if (!TryReadRequiredString(
			Machine,
			TEXT("Name"),
			FString::Printf(TEXT("/Body/StateMachines/%d/Name"), MachineIndex),
			TEXT("MissingStateMachineName"),
			TEXT("InvalidStateMachineName"),
			MachineName,
			Result))
		{
			return Result;
		}

		const FString NormalizedMachine = NormalizeIdentity(MachineName);
		if (MachineIdentities.Contains(NormalizedMachine))
		{
			return Failure(
				FString::Printf(TEXT("/Body/StateMachines/%d/Name"), MachineIndex),
				TEXT("DuplicateStateMachineName"),
				FString::Printf(TEXT("Duplicate StateMachines identity '%s'"), *MachineName));
		}
		MachineIdentities.Add(NormalizedMachine);

		const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
		if (!Machine->TryGetArrayField(TEXT("States"), States) || !States)
		{
			return Failure(
				FString::Printf(TEXT("/Body/StateMachines/%s/States"), *Escape(MachineName)),
				TEXT("MissingStateMachineStates"),
				TEXT("State machine requires States array"));
		}

		TSet<FString> StateIdentities;
		for (int32 StateIndex = 0; StateIndex < States->Num(); ++StateIndex)
		{
			const TSharedPtr<FJsonObject> State = (*States)[StateIndex].IsValid() ? (*States)[StateIndex]->AsObject() : nullptr;
			if (!State.IsValid())
			{
				return Failure(
					FString::Printf(TEXT("/Body/StateMachines/%s/States/%d"), *Escape(MachineName), StateIndex),
					TEXT("InvalidStateMachineState"),
					TEXT("State entries must be objects"));
			}

			Result = ValidateNoUnknownFields(
				State,
				{TEXT("Id"), TEXT("Name"), TEXT("Graph")},
				FString::Printf(TEXT("/Body/StateMachines/%s/States/%d"), *Escape(MachineName), StateIndex));
			if (!Result.bSuccess)
			{
				return Result;
			}

			FString StateId;
			if (!TryReadRequiredString(
				State,
				TEXT("Id"),
				FString::Printf(TEXT("/Body/StateMachines/%s/States/%d/Id"), *Escape(MachineName), StateIndex),
				TEXT("MissingStateId"),
				TEXT("InvalidStateId"),
				StateId,
				Result))
			{
				return Result;
			}

			const FString NormalizedState = NormalizeIdentity(StateId);
			if (StateIdentities.Contains(NormalizedState))
			{
				return Failure(
					FString::Printf(TEXT("/Body/StateMachines/%s/States/%d/Id"), *Escape(MachineName), StateIndex),
					TEXT("DuplicateStateId"),
					FString::Printf(TEXT("Duplicate state identity '%s' in StateMachine '%s'"), *StateId, *MachineName));
			}
			StateIdentities.Add(NormalizedState);
		}

		FString EntryState;
		if (Machine->TryGetStringField(TEXT("EntryState"), EntryState)
			&& !EntryState.TrimStartAndEnd().IsEmpty()
			&& !StateIdentities.Contains(NormalizeIdentity(EntryState)))
		{
			return Failure(
				FString::Printf(TEXT("/Body/StateMachines/%s/EntryState"), *Escape(MachineName)),
				TEXT("UnknownEntryState"),
				FString::Printf(TEXT("EntryState '%s' does not match a state identity"), *EntryState));
		}

		const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
		if (!Machine->TryGetArrayField(TEXT("Transitions"), Transitions) || !Transitions)
		{
			continue;
		}

		TSet<FString> TransitionIdentities;
		for (int32 TransitionIndex = 0; TransitionIndex < Transitions->Num(); ++TransitionIndex)
		{
			const TSharedPtr<FJsonObject> Transition = (*Transitions)[TransitionIndex].IsValid() ? (*Transitions)[TransitionIndex]->AsObject() : nullptr;
			if (!Transition.IsValid())
			{
				return Failure(
					FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%d"), *Escape(MachineName), TransitionIndex),
					TEXT("InvalidStateTransition"),
					TEXT("Transition entries must be objects"));
			}

			Result = ValidateNoUnknownFields(
				Transition,
				{TEXT("Id"), TEXT("From"), TEXT("To"), TEXT("Rule")},
				FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%d"), *Escape(MachineName), TransitionIndex));
			if (!Result.bSuccess)
			{
				return Result;
			}

			FString TransitionId;
			if (!TryReadRequiredString(
				Transition,
				TEXT("Id"),
				FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%d/Id"), *Escape(MachineName), TransitionIndex),
				TEXT("MissingTransitionId"),
				TEXT("InvalidTransitionId"),
				TransitionId,
				Result))
			{
				return Result;
			}

			const FString NormalizedTransition = NormalizeIdentity(TransitionId);
			if (TransitionIdentities.Contains(NormalizedTransition))
			{
				return Failure(
					FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%d/Id"), *Escape(MachineName), TransitionIndex),
					TEXT("DuplicateTransitionId"),
					FString::Printf(TEXT("Duplicate transition identity '%s' in StateMachine '%s'"), *TransitionId, *MachineName));
			}
			TransitionIdentities.Add(NormalizedTransition);

			for (const TCHAR* Endpoint : {TEXT("From"), TEXT("To")})
			{
				FString StateRef;
				if (!TryReadRequiredString(
					Transition,
					Endpoint,
					FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%s/%s"), *Escape(MachineName), *Escape(TransitionId), Endpoint),
					FString::Printf(TEXT("MissingTransition%s"), Endpoint),
					FString::Printf(TEXT("InvalidTransition%s"), Endpoint),
					StateRef,
					Result))
				{
					return Result;
				}
				if (!StateIdentities.Contains(NormalizeIdentity(StateRef)))
				{
					return Failure(
						FString::Printf(TEXT("/Body/StateMachines/%s/Transitions/%s/%s"), *Escape(MachineName), *Escape(TransitionId), Endpoint),
						TEXT("UnknownTransitionState"),
						FString::Printf(TEXT("Transition endpoint '%s' does not match a state identity"), *StateRef));
				}
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint state machines"));
}

FAssetDocumentCapabilityResult ValidateTransitionGraphsValue(const TSharedPtr<FJsonValue>& DesiredValue)
{
	if (IsEmptyCompatibilityValue(DesiredValue))
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (DesiredValue->Type != EJson::Array)
	{
		return Failure(TEXT("/Body/TransitionGraphs"), TEXT("InvalidTransitionGraphsRegionType"), TEXT("Body.TransitionGraphs must be an array"));
	}

	TSet<FString> TransitionGraphIdentities;
	const TArray<TSharedPtr<FJsonValue>>& Graphs = DesiredValue->AsArray();
	for (int32 GraphIndex = 0; GraphIndex < Graphs.Num(); ++GraphIndex)
	{
		const TSharedPtr<FJsonObject> Graph = Graphs[GraphIndex].IsValid() ? Graphs[GraphIndex]->AsObject() : nullptr;
		if (!Graph.IsValid())
		{
			return Failure(
				FString::Printf(TEXT("/Body/TransitionGraphs/%d"), GraphIndex),
				TEXT("InvalidTransitionGraph"),
				TEXT("Body.TransitionGraphs entries must be objects"));
		}

		FAssetDocumentCapabilityResult Result = ValidateNoUnknownFields(
			Graph,
			{TEXT("StateMachine"), TEXT("Transition"), TEXT("Nodes"), TEXT("Result")},
			FString::Printf(TEXT("/Body/TransitionGraphs/%d"), GraphIndex));
		if (!Result.bSuccess)
		{
			return Result;
		}

		FString MachineName;
		if (!TryReadRequiredString(
			Graph,
			TEXT("StateMachine"),
			FString::Printf(TEXT("/Body/TransitionGraphs/%d/StateMachine"), GraphIndex),
			TEXT("MissingTransitionGraphStateMachine"),
			TEXT("InvalidTransitionGraphStateMachine"),
			MachineName,
			Result))
		{
			return Result;
		}

		FString TransitionId;
		if (!TryReadRequiredString(
			Graph,
			TEXT("Transition"),
			FString::Printf(TEXT("/Body/TransitionGraphs/%s/%d/Transition"), *Escape(MachineName), GraphIndex),
			TEXT("MissingTransitionGraphTransition"),
			TEXT("InvalidTransitionGraphTransition"),
			TransitionId,
			Result))
		{
			return Result;
		}

		const FString GraphIdentity = NormalizeIdentity(MachineName) / NormalizeIdentity(TransitionId);
		if (TransitionGraphIdentities.Contains(GraphIdentity))
		{
			return Failure(
				FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s"), *Escape(MachineName), *Escape(TransitionId)),
				TEXT("DuplicateTransitionGraph"),
				FString::Printf(TEXT("Duplicate TransitionGraphs identity '%s/%s'"), *MachineName, *TransitionId));
		}
		TransitionGraphIdentities.Add(GraphIdentity);

		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
		{
			return Failure(
				FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s/Nodes"), *Escape(MachineName), *Escape(TransitionId)),
				TEXT("MissingTransitionGraphNodes"),
				TEXT("Transition graph requires Nodes array"));
		}
		if (Nodes->Num() > 0)
		{
			return Failure(
				FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s/Nodes/0"), *Escape(MachineName), *Escape(TransitionId)),
				TEXT("UnsupportedTransitionGraphNode"),
				TEXT("Transition graph pilot does not support authored rule nodes yet"));
		}

		const TSharedPtr<FJsonObject>* ResultObject = nullptr;
		if (!Graph->TryGetObjectField(TEXT("Result"), ResultObject) || !ResultObject || !ResultObject->IsValid())
		{
			return Failure(
				FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s/Result"), *Escape(MachineName), *Escape(TransitionId)),
				TEXT("InvalidTransitionGraphResult"),
				TEXT("Transition graph requires Result object"));
		}

		Result = ValidateTransitionResult(*ResultObject, FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s/Result"), *Escape(MachineName), *Escape(TransitionId)));
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint transition graphs"));
}

FString StateMachinePath(const TSharedPtr<FJsonObject>& Machine, int32 FallbackIndex)
{
	FString Name;
	if (Machine.IsValid() && Machine->TryGetStringField(TEXT("Name"), Name) && !Name.TrimStartAndEnd().IsEmpty())
	{
		return FString::Printf(TEXT("/Body/StateMachines/%s"), *Escape(Name));
	}
	return FString::Printf(TEXT("/Body/StateMachines/%d"), FallbackIndex);
}

FString TransitionGraphPath(const TSharedPtr<FJsonObject>& Graph, int32 FallbackIndex)
{
	FString MachineName;
	FString TransitionId;
	if (Graph.IsValid()
		&& Graph->TryGetStringField(TEXT("StateMachine"), MachineName)
		&& Graph->TryGetStringField(TEXT("Transition"), TransitionId)
		&& !MachineName.TrimStartAndEnd().IsEmpty()
		&& !TransitionId.TrimStartAndEnd().IsEmpty())
	{
		return FString::Printf(TEXT("/Body/TransitionGraphs/%s/%s"), *Escape(MachineName), *Escape(TransitionId));
	}
	return FString::Printf(TEXT("/Body/TransitionGraphs/%d"), FallbackIndex);
}

void AddArrayDiffEntries(
	const TSharedPtr<FJsonValue>& DesiredValue,
	const FString& RegionId,
	TFunctionRef<FString(const TSharedPtr<FJsonObject>&, int32)> MakePath,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	if (IsEmptyCompatibilityValue(DesiredValue))
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>& DesiredArray = DesiredValue->AsArray();
	for (int32 Index = 0; Index < DesiredArray.Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Object = DesiredArray[Index].IsValid() ? DesiredArray[Index]->AsObject() : nullptr;
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			MakePath(Object, Index),
			TEXT("added"),
			MakeEmptyArrayValue(),
			DesiredArray[Index]);
	}

	if (DesiredArray.Num() == 0)
	{
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			FString::Printf(TEXT("/Body/%s"), *RegionId),
			TEXT("unchanged"),
			MakeEmptyArrayValue(),
			MakeEmptyArrayValue());
	}
}
}

FAssetDocumentAnimStateMachineRegionAdapter::FAssetDocumentAnimStateMachineRegionAdapter(FName InAdapterName)
	: AdapterName(InAdapterName)
{
}

FName FAssetDocumentAnimStateMachineRegionAdapter::GetName() const
{
	return AdapterName;
}

bool FAssetDocumentAnimStateMachineRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return Context.RegionId == StateMachinesRegionId || Context.RegionId == TransitionGraphsRegionId;
}

TSharedRef<FJsonObject> FAssetDocumentAnimStateMachineRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Kind"), TEXT("AnimStateMachineIdentityRegion"));
	Schema->SetStringField(TEXT("StateMachines"), TEXT("array<{Name, EntryState?, States:[{Id}], Transitions:[{Id, From, To, Rule?}]}>"));
	Schema->SetStringField(TEXT("TransitionGraphs"), TEXT("array<{StateMachine, Transition, Nodes:[], Result:{Node:null, Pin:'CanEnterTransition'}}>"));
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentAnimStateMachineRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	if (Context.RegionId == StateMachinesRegionId)
	{
		return ValidateStateMachinesValue(DesiredValue);
	}
	if (Context.RegionId == TransitionGraphsRegionId)
	{
		return ValidateTransitionGraphsValue(DesiredValue);
	}
	return Failure(Context.JsonPointer, TEXT("UnsupportedAnimStateMachineRegion"), TEXT("Unsupported state-machine region"));
}

FAssetDocumentCapabilityResult FAssetDocumentAnimStateMachineRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;
	return ValidateRegion(Context, DesiredValue);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimStateMachineRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext&,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	OutCurrentValue = MakeEmptyArrayValue();
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimBlueprint state-machine identity region"));
}

FAssetDocumentCapabilityResult FAssetDocumentAnimStateMachineRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	const FAssetDocumentCapabilityResult ValidateResult = ValidateRegion(Context, DesiredValue);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	if (Context.RegionId == StateMachinesRegionId)
	{
		AddArrayDiffEntries(DesiredValue, TEXT("StateMachines"), StateMachinePath, OutDiffEntries);
	}
	else if (Context.RegionId == TransitionGraphsRegionId)
	{
		AddArrayDiffEntries(DesiredValue, TEXT("TransitionGraphs"), TransitionGraphPath, OutDiffEntries);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed AnimBlueprint state-machine identity region"));
}
