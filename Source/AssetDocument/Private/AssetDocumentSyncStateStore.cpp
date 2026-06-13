// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentSyncStateStore.h"

#include "Dom/JsonValue.h"

namespace
{
constexpr int32 DefaultSyncSchemaVersion = 1;
constexpr int32 DefaultPolicyVersion = 1;
constexpr int32 AssetDocumentMetaVersion = 1;

bool TryReadOptionalString(const TSharedRef<FJsonObject>& Object, const FString& FieldName, FString& OutValue, FString& OutError)
{
	if (!Object->HasField(FieldName))
	{
		return true;
	}

	if (!Object->TryGetStringField(FieldName, OutValue))
	{
		OutError = FString::Printf(TEXT("Sync field '%s' must be a string."), *FieldName);
		return false;
	}

	return true;
}

bool TryReadOptionalInt(const TSharedRef<FJsonObject>& Object, const FString& FieldName, int32& OutValue, FString& OutError)
{
	if (!Object->HasField(FieldName))
	{
		return true;
	}

	double NumberValue = 0.0;
	if (!Object->TryGetNumberField(FieldName, NumberValue) || !FMath::IsNearlyEqual(NumberValue, FMath::RoundToDouble(NumberValue)))
	{
		OutError = FString::Printf(TEXT("Sync field '%s' must be an integer."), *FieldName);
		return false;
	}
	if (NumberValue < static_cast<double>(MIN_int32) || NumberValue > static_cast<double>(MAX_int32))
	{
		OutError = FString::Printf(TEXT("Sync field '%s' is outside int32 range."), *FieldName);
		return false;
	}

	OutValue = static_cast<int32>(NumberValue);
	return true;
}

bool TryGetOptionalObject(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	TSharedPtr<FJsonObject>& OutObject,
	FString& OutError)
{
	OutObject.Reset();
	if (!Object->HasField(FieldName))
	{
		return true;
	}

	const TSharedPtr<FJsonObject>* FoundObject = nullptr;
	if (!Object->TryGetObjectField(FieldName, FoundObject) || !FoundObject || !FoundObject->IsValid())
	{
		OutError = FString::Printf(TEXT("Sync field '%s' must be an object."), *FieldName);
		return false;
	}

	OutObject = *FoundObject;
	return true;
}

bool ReadRegionState(const FString& RegionId, const TSharedRef<FJsonObject>& RegionObject, FAssetDocumentRegionSyncState& OutState, FString& OutError)
{
	OutState = FAssetDocumentRegionSyncState();
	if (!TryReadOptionalInt(RegionObject, TEXT("policyVersion"), OutState.PolicyVersion, OutError))
	{
		OutError = FString::Printf(TEXT("Region '%s': %s"), *RegionId, *OutError);
		return false;
	}

	if (!TryReadOptionalString(RegionObject, TEXT("sidecarHash"), OutState.SidecarHash, OutError)
		|| !TryReadOptionalString(RegionObject, TEXT("assetEvidenceHash"), OutState.AssetEvidenceHash, OutError)
		|| !TryReadOptionalString(RegionObject, TEXT("lastSyncedAtUtc"), OutState.LastSyncedAtUtc, OutError))
	{
		OutError = FString::Printf(TEXT("Region '%s': %s"), *RegionId, *OutError);
		return false;
	}

	return true;
}

TSharedRef<FJsonObject> GetOrCreateObjectField(const TSharedRef<FJsonObject>& Object, const FString& FieldName)
{
	const TSharedPtr<FJsonObject>* ExistingObject = nullptr;
	if (Object->TryGetObjectField(FieldName, ExistingObject) && ExistingObject && ExistingObject->IsValid())
	{
		return ExistingObject->ToSharedRef();
	}

	TSharedRef<FJsonObject> NewObject = MakeShared<FJsonObject>();
	Object->SetObjectField(FieldName, NewObject);
	return NewObject;
}

TSharedRef<FJsonObject> MakeRegionJson(const FAssetDocumentRegionSyncState& RegionState)
{
	TSharedRef<FJsonObject> RegionObject = MakeShared<FJsonObject>();
	RegionObject->SetNumberField(TEXT("policyVersion"), RegionState.PolicyVersion);
	RegionObject->SetStringField(TEXT("sidecarHash"), RegionState.SidecarHash);
	RegionObject->SetStringField(TEXT("assetEvidenceHash"), RegionState.AssetEvidenceHash);
	RegionObject->SetStringField(TEXT("lastSyncedAtUtc"), RegionState.LastSyncedAtUtc);
	return RegionObject;
}
}

bool FAssetDocumentSyncStateStore::LoadFromDocumentJson(
	const TSharedRef<FJsonObject>& DocumentJson,
	FAssetDocumentSyncState& OutState,
	FString& OutError)
{
	OutState = FAssetDocumentSyncState();
	OutError.Reset();

	TSharedPtr<FJsonObject> MetaObject;
	const TSharedPtr<FJsonObject>* FoundMetaObject = nullptr;
	if (!DocumentJson->TryGetObjectField(TEXT("_meta"), FoundMetaObject) || !FoundMetaObject || !FoundMetaObject->IsValid())
	{
		return true;
	}
	MetaObject = *FoundMetaObject;

	TSharedPtr<FJsonObject> SyncObject;
	if (!TryGetOptionalObject(MetaObject.ToSharedRef(), TEXT("sync"), SyncObject, OutError))
	{
		return false;
	}

	if (!SyncObject.IsValid())
	{
		return true;
	}

	if (!TryReadOptionalInt(SyncObject.ToSharedRef(), TEXT("schemaVersion"), OutState.SchemaVersion, OutError)
		|| !TryReadOptionalString(SyncObject.ToSharedRef(), TEXT("assetObjectPath"), OutState.AssetObjectPath, OutError)
		|| !TryReadOptionalString(SyncObject.ToSharedRef(), TEXT("assetPackageGuid"), OutState.AssetPackageGuid, OutError)
		|| !TryReadOptionalString(SyncObject.ToSharedRef(), TEXT("updatedAtUtc"), OutState.UpdatedAtUtc, OutError))
	{
		return false;
	}

	TSharedPtr<FJsonObject> RegionsObject;
	if (!TryGetOptionalObject(SyncObject.ToSharedRef(), TEXT("regions"), RegionsObject, OutError))
	{
		return false;
	}

	if (!RegionsObject.IsValid())
	{
		return true;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RegionsObject->Values)
	{
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object || !Pair.Value->AsObject().IsValid())
		{
			OutError = FString::Printf(TEXT("Sync region '%s' must be an object."), *Pair.Key);
			return false;
		}

		FAssetDocumentRegionSyncState RegionState;
		if (!ReadRegionState(Pair.Key, Pair.Value->AsObject().ToSharedRef(), RegionState, OutError))
		{
			return false;
		}

		OutState.Regions.Add(Pair.Key, RegionState);
	}

	return true;
}

void FAssetDocumentSyncStateStore::WriteToDocumentJson(
	const TSharedRef<FJsonObject>& DocumentJson,
	const FAssetDocumentSyncState& State)
{
	TSharedRef<FJsonObject> MetaObject = GetOrCreateObjectField(DocumentJson, TEXT("_meta"));
	TSharedRef<FJsonObject> SyncObject = MakeShared<FJsonObject>();

	MetaObject->SetNumberField(TEXT("assetDocumentVersion"), AssetDocumentMetaVersion);

	SyncObject->SetNumberField(TEXT("schemaVersion"), State.SchemaVersion);
	SyncObject->SetStringField(TEXT("assetObjectPath"), State.AssetObjectPath);
	SyncObject->SetStringField(TEXT("assetPackageGuid"), State.AssetPackageGuid);
	SyncObject->SetStringField(TEXT("updatedAtUtc"), State.UpdatedAtUtc);

	TSharedRef<FJsonObject> RegionsObject = MakeShared<FJsonObject>();
	TArray<FString> RegionIds;
	State.Regions.GetKeys(RegionIds);
	RegionIds.Sort();

	for (const FString& RegionId : RegionIds)
	{
		if (const FAssetDocumentRegionSyncState* RegionState = State.Regions.Find(RegionId))
		{
			RegionsObject->SetObjectField(RegionId, MakeRegionJson(*RegionState));
		}
	}
	SyncObject->SetObjectField(TEXT("regions"), RegionsObject);

	MetaObject->SetObjectField(TEXT("sync"), SyncObject);
}

void FAssetDocumentSyncStateStore::UpdateRegionState(
	FAssetDocumentSyncState& State,
	const FString& RegionId,
	const FAssetDocumentRegionSyncState& RegionState)
{
	if (RegionId.IsEmpty())
	{
		return;
	}

	FAssetDocumentRegionSyncState NormalizedRegionState = RegionState;
	if (NormalizedRegionState.PolicyVersion <= 0)
	{
		NormalizedRegionState.PolicyVersion = DefaultPolicyVersion;
	}

	if (State.SchemaVersion <= 0)
	{
		State.SchemaVersion = DefaultSyncSchemaVersion;
	}

	State.Regions.Add(RegionId, NormalizedRegionState);
}

void FAssetDocumentSyncStateStore::UpdateRegionState(
	FAssetDocumentSyncState& State,
	FName RegionId,
	const FAssetDocumentRegionSyncState& RegionState)
{
	UpdateRegionState(State, RegionId.IsNone() ? FString() : RegionId.ToString(), RegionState);
}

void FAssetDocumentSyncStateStore::UpdateRegionState(
	FAssetDocumentSyncState& State,
	const TCHAR* RegionId,
	const FAssetDocumentRegionSyncState& RegionState)
{
	UpdateRegionState(State, FString(RegionId ? RegionId : TEXT("")), RegionState);
}
