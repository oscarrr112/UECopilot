// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentFragmentArrayRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
FString RegionPath(const FAssetDocumentFragmentArrayRegionConfig& Config, const FAssetDocumentRegionContext& Context)
{
	if (!Config.JsonPointer.IsEmpty())
	{
		return Config.JsonPointer;
	}
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FAssetDocumentCapabilityResult FragmentArrayFailure(
	const FString& Path,
	const FString& Code,
	const FString& Message)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult UnsupportedLifecycleFailure(
	const FAssetDocumentFragmentArrayRegionConfig& Config,
	const FAssetDocumentRegionContext& Context,
	const TCHAR* Lifecycle)
{
	return FragmentArrayFailure(
		RegionPath(Config, Context),
		TEXT("UnsupportedFragmentArrayLifecycle"),
		FString::Printf(
			TEXT("Fragment array region %s requires an explicit %s hook"),
			*Context.RegionId.ToString(),
			Lifecycle));
}
}

FString FAssetDocumentFragmentArrayUtils::MakeEntryPath(const FString& BasePath, const int32 Index)
{
	return FString::Printf(TEXT("%s/%d"), *BasePath, Index);
}

FAssetDocumentCapabilityResult FAssetDocumentFragmentArrayUtils::ParseObjectEntries(
	const TSharedPtr<FJsonValue>& Value,
	const FString& BasePath,
	TArray<FAssetDocumentFragmentArrayEntry>& OutEntries)
{
	OutEntries.Reset();

	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return FragmentArrayFailure(
			BasePath,
			TEXT("InvalidFragmentArrayRegionType"),
			TEXT("Expected a JSON array for fragment array region"));
	}

	const TArray<TSharedPtr<FJsonValue>> Values = Value->AsArray();
	OutEntries.Reserve(Values.Num());
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const FString EntryPath = MakeEntryPath(BasePath, Index);
		const TSharedPtr<FJsonValue>& EntryValue = Values[Index];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			return FragmentArrayFailure(
				EntryPath,
				TEXT("InvalidFragmentArrayEntryType"),
				TEXT("Expected a JSON object for fragment array entry"));
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		if (!EntryObject.IsValid())
		{
			return FragmentArrayFailure(
				EntryPath,
				TEXT("InvalidFragmentArrayEntryType"),
				TEXT("Expected a JSON object for fragment array entry"));
		}

		OutEntries.Emplace(Index, EntryPath, EntryObject.ToSharedRef());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Parsed fragment array region"));
}

TSharedRef<FJsonValue> FAssetDocumentFragmentArrayUtils::MakeArrayValue(
	const TArray<TSharedRef<FJsonObject>>& Entries)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Entries.Num());
	for (const TSharedRef<FJsonObject>& Entry : Entries)
	{
		Values.Add(MakeShared<FJsonValueObject>(Entry));
	}
	return MakeShared<FJsonValueArray>(MoveTemp(Values));
}

FAssetDocumentFragmentArrayRegionAdapter::FAssetDocumentFragmentArrayRegionAdapter(
	FAssetDocumentFragmentArrayRegionConfig InConfig,
	FAssetDocumentFragmentArrayHooks InHooks)
	: Config(MoveTemp(InConfig))
	, Hooks(MoveTemp(InHooks))
{
}

FName FAssetDocumentFragmentArrayRegionAdapter::GetName() const
{
	return Config.AdapterName;
}

bool FAssetDocumentFragmentArrayRegionAdapter::SupportsRegion(
	const FAssetDocumentRegionContext& Context) const
{
	return (!Config.RegionId.IsNone() && Context.RegionId == Config.RegionId)
		|| (!Config.BodyPath.IsEmpty() && Context.BodyPath == Config.BodyPath)
		|| (!Config.JsonPointer.IsEmpty() && Context.JsonPointer == Config.JsonPointer);
}

TSharedRef<FJsonObject> FAssetDocumentFragmentArrayRegionAdapter::GetSchemaHint(
	const FAssetDocumentRegionContext&) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Adapter"), GetName().ToString());
	if (!Config.SchemaLabel.IsEmpty())
	{
		Schema->SetStringField(TEXT("Label"), Config.SchemaLabel);
	}
	Schema->SetStringField(TEXT("Kind"), TEXT("FragmentArray"));
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentFragmentArrayRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	TArray<FAssetDocumentFragmentArrayEntry> Entries;
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentFragmentArrayUtils::ParseObjectEntries(DesiredValue, RegionPath(Config, Context), Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!Hooks.Validate)
	{
		return UnsupportedLifecycleFailure(Config, Context, TEXT("validate"));
	}

	return Hooks.Validate(Context, Entries);
}

FAssetDocumentCapabilityResult FAssetDocumentFragmentArrayRegionAdapter::PreflightRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	TArray<FAssetDocumentFragmentArrayEntry> Entries;
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentFragmentArrayUtils::ParseObjectEntries(DesiredValue, RegionPath(Config, Context), Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!Hooks.Preflight)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Fragment array preflight no-op"));
	}

	return Hooks.Preflight(Context, Entries);
}

FAssetDocumentCapabilityResult FAssetDocumentFragmentArrayRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;

	TArray<FAssetDocumentFragmentArrayEntry> Entries;
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentFragmentArrayUtils::ParseObjectEntries(DesiredValue, RegionPath(Config, Context), Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!Hooks.Apply)
	{
		return UnsupportedLifecycleFailure(Config, Context, TEXT("apply"));
	}

	return Hooks.Apply(Context, Entries, bOutChanged);
}

FAssetDocumentCapabilityResult FAssetDocumentFragmentArrayRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	OutCurrentValue.Reset();
	if (!Hooks.Extract)
	{
		return UnsupportedLifecycleFailure(Config, Context, TEXT("extract"));
	}

	TArray<TSharedRef<FJsonObject>> Entries;
	const FAssetDocumentCapabilityResult Result = Hooks.Extract(Context, Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutCurrentValue = FAssetDocumentFragmentArrayUtils::MakeArrayValue(Entries);
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted fragment array region"));
}

FAssetDocumentCapabilityResult FAssetDocumentFragmentArrayRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TArray<FAssetDocumentFragmentArrayEntry> DesiredEntries;
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentFragmentArrayUtils::ParseObjectEntries(DesiredValue, RegionPath(Config, Context), DesiredEntries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Hooks.Diff)
	{
		return Hooks.Diff(Context, DesiredEntries, OutDiffEntries);
	}

	if (!Config.bEnableDefaultDiff || !Hooks.Extract)
	{
		return UnsupportedLifecycleFailure(Config, Context, TEXT("diff"));
	}

	TSharedPtr<FJsonValue> CurrentValue;
	Result = ExtractRegion(Context, CurrentValue);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentValue) !=
		FAssetDocumentJsonRegionUtils::JsonValueToComparableString(DesiredValue))
	{
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			RegionPath(Config, Context),
			TEXT("changed"),
			CurrentValue,
			DesiredValue);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed fragment array region"));
}
