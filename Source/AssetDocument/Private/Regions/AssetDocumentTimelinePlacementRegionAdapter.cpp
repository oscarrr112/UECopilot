// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentTimelinePlacementRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
FAssetDocumentCapabilityResult TimelinePlacementFailure(
	const FString& Path,
	const FString& Code,
	const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(Path, Code, Message);
}

FAssetDocumentCapabilityResult TimelinePlacementUnsupportedLifecycleFailure(
	const FAssetDocumentTimelinePlacementRegionConfig& Config,
	const FAssetDocumentRegionContext& Context,
	const FString& Path,
	const TCHAR* Lifecycle)
{
	return TimelinePlacementFailure(
		Path,
		TEXT("UnsupportedTimelinePlacementLifecycle"),
		FString::Printf(
			TEXT("Timeline placement region %s requires an explicit %s hook"),
			*Context.RegionId.ToString(),
			Lifecycle));
}

bool TimelinePlacementIsFiniteNumber(const double Value)
{
	return FMath::IsFinite(Value);
}

bool TimelinePlacementIsIntegerNumber(const double Value)
{
	return TimelinePlacementIsFiniteNumber(Value) && FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value));
}

bool TimelinePlacementTryReadNumberField(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	TOptional<double>& OutNumber,
	FString& OutFailureCode,
	FString& OutFailureMessage)
{
	const TSharedPtr<FJsonValue> FieldValue = Object->TryGetField(FieldName);
	if (!FieldValue.IsValid())
	{
		return true;
	}
	if (FieldValue->Type != EJson::Number)
	{
		OutFailureCode = TEXT("InvalidTimelinePlacementNumber");
		OutFailureMessage = FString::Printf(TEXT("%s must be a finite number"), *FieldName);
		return false;
	}

	const double Number = FieldValue->AsNumber();
	if (!TimelinePlacementIsFiniteNumber(Number))
	{
		OutFailureCode = TEXT("InvalidTimelinePlacementNumber");
		OutFailureMessage = FString::Printf(TEXT("%s must be a finite number"), *FieldName);
		return false;
	}

	OutNumber = Number;
	return true;
}

enum class ETimelinePlacementStringReadResult
{
	Missing,
	InvalidType,
	Valid
};

ETimelinePlacementStringReadResult TimelinePlacementTryReadStringField(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	TOptional<FString>& OutString,
	const bool bTrim = true)
{
	const TSharedPtr<FJsonValue> FieldValue = Object->TryGetField(FieldName);
	if (!FieldValue.IsValid())
	{
		return ETimelinePlacementStringReadResult::Missing;
	}
	if (FieldValue->Type != EJson::String)
	{
		return ETimelinePlacementStringReadResult::InvalidType;
	}

	FString StringValue = FieldValue->AsString();
	if (bTrim)
	{
		StringValue = StringValue.TrimStartAndEnd();
	}
	OutString = MoveTemp(StringValue);
	return ETimelinePlacementStringReadResult::Valid;
}

FAssetDocumentCapabilityResult TimelinePlacementValidateTime(
	const FAssetDocumentTimelinePlacementRegionConfig& Config,
	const FAssetDocumentTimelineRange* Range,
	const FAssetDocumentTimelinePlacementEntry& Entry)
{
	if (!Entry.Time.IsSet())
	{
		if (Config.bRequireTime)
		{
			return TimelinePlacementFailure(
				FAssetDocumentTimelinePlacementUtils::MakeFieldPath(Entry.JsonPointer, Config.TimeFieldName),
				TEXT("MissingTimelinePlacementTime"),
				FString::Printf(TEXT("%s is required"), *Config.TimeFieldName));
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Timeline placement time is optional"));
	}

	const double Time = Entry.Time.GetValue();
	if (Time < 0.0 || (!Config.bAllowZeroTime && FMath::IsNearlyZero(Time)))
	{
		return TimelinePlacementFailure(
			FAssetDocumentTimelinePlacementUtils::MakeFieldPath(Entry.JsonPointer, Config.TimeFieldName),
			TEXT("InvalidTimelinePlacementTime"),
			FString::Printf(TEXT("%s is outside the allowed timeline range"), *Config.TimeFieldName));
	}

	if (Range)
	{
		if (Time < Range->MinTime || (Range->bHasMaxTime && Time > Range->MaxTime))
		{
			return TimelinePlacementFailure(
				FAssetDocumentTimelinePlacementUtils::MakeFieldPath(Entry.JsonPointer, Config.TimeFieldName),
				TEXT("InvalidTimelinePlacementTime"),
				FString::Printf(TEXT("%s is outside the allowed timeline range"), *Config.TimeFieldName));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Timeline placement time is valid"));
}

FAssetDocumentCapabilityResult TimelinePlacementValidateDuration(
	const FAssetDocumentTimelinePlacementRegionConfig& Config,
	const FAssetDocumentTimelineRange* Range,
	const FAssetDocumentTimelinePlacementEntry& Entry)
{
	if (!Config.bHasDuration)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Timeline placement has no duration"));
	}

	const FString DurationPath =
		FAssetDocumentTimelinePlacementUtils::MakeFieldPath(Entry.JsonPointer, Config.DurationFieldName);
	if (!Entry.Duration.IsSet())
	{
		if (Config.bRequireDuration)
		{
			return TimelinePlacementFailure(
				DurationPath,
				TEXT("MissingTimelinePlacementDuration"),
				FString::Printf(TEXT("%s is required"), *Config.DurationFieldName));
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Timeline placement duration is optional"));
	}

	const double Duration = Entry.Duration.GetValue();
	if (Duration < 0.0 || (Config.bRequirePositiveDuration && FMath::IsNearlyZero(Duration)))
	{
		return TimelinePlacementFailure(
			DurationPath,
			TEXT("InvalidTimelinePlacementDuration"),
			FString::Printf(TEXT("%s must be positive"), *Config.DurationFieldName));
	}

	if (Config.bValidateEndTime && Entry.Time.IsSet() && Range && Range->bHasMaxTime)
	{
		const double EndTime = Entry.Time.GetValue() + Duration;
		if (EndTime > Range->MaxTime)
		{
			return TimelinePlacementFailure(
				DurationPath,
				TEXT("InvalidTimelinePlacementEndTime"),
				FString::Printf(TEXT("%s ends outside the allowed timeline range"), *Config.DurationFieldName));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Timeline placement duration is valid"));
}

FAssetDocumentCapabilityResult TimelinePlacementValidateName(
	const FAssetDocumentTimelinePlacementRegionConfig& Config,
	const FAssetDocumentTimelinePlacementEntry& Entry)
{
	if (!Config.bHasName)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Timeline placement has no name"));
	}

	if (!Entry.Name.IsSet() || Entry.Name.GetValue().IsEmpty())
	{
		if (Config.bRequireName)
		{
			return TimelinePlacementFailure(
				FAssetDocumentTimelinePlacementUtils::MakeFieldPath(Entry.JsonPointer, Config.NameFieldName),
				TEXT("MissingTimelinePlacementName"),
				FString::Printf(TEXT("%s is required"), *Config.NameFieldName));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Timeline placement name is valid"));
}

FAssetDocumentCapabilityResult TimelinePlacementResolveTrack(
	const FAssetDocumentTimelinePlacementRegionConfig& Config,
	FAssetDocumentTimelinePlacementEntry& Entry)
{
	if (!Config.bHasTrackIdentity)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Timeline placement has no track identity"));
	}

	if (Config.bRequireTrackIdentity && !Entry.TrackName.IsSet() && !Entry.TrackIndex.IsSet())
	{
		const FString FieldName = !Config.TrackNameFieldName.IsEmpty()
			? Config.TrackNameFieldName
			: Config.TrackIndexFieldName;
		return TimelinePlacementFailure(
			FAssetDocumentTimelinePlacementUtils::MakeFieldPath(Entry.JsonPointer, FieldName),
			TEXT("MissingTimelinePlacementTrackIdentity"),
			TEXT("Timeline placement requires track identity"));
	}

	if (!Config.TrackResolver.Resolve)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Timeline placement track resolver is optional"));
	}

	FAssetDocumentTimelineTrackResolveRequest Request;
	Request.RegionId = Config.RegionId;
	Request.EntryIndex = Entry.Index;
	Request.JsonPointer = Entry.JsonPointer;
	Request.TrackName = Entry.TrackName;
	Request.TrackIndex = Entry.TrackIndex;

	const FAssetDocumentTimelineTrackResolveResult ResolveResult = Config.TrackResolver.Resolve(Request);
	if (!ResolveResult.Error.bSuccess)
	{
		return ResolveResult.Error;
	}

	if (ResolveResult.bResolved)
	{
		Entry.ResolvedTrackIndex = ResolveResult.TrackIndex;
		Entry.CanonicalTrackName = ResolveResult.CanonicalTrackName;
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Resolved timeline placement track"));
}

FAssetDocumentCapabilityResult TimelinePlacementValidateEntriesForLifecycle(
	const FAssetDocumentTimelinePlacementRegionConfig& Config,
	const FAssetDocumentTimelinePlacementHooks& Hooks,
	const FAssetDocumentRegionContext& Context,
	TArray<FAssetDocumentTimelinePlacementEntry>& Entries,
	const FString& RegionPath,
	const TCHAR* Lifecycle,
	const bool bRequireValidate)
{
	if (!Hooks.Validate)
	{
		return bRequireValidate
			? TimelinePlacementUnsupportedLifecycleFailure(Config, Context, RegionPath, Lifecycle)
			: FAssetDocumentCapabilityResult::Success(TEXT("No timeline placement validate hook"));
	}

	return Hooks.Validate(Context, Entries);
}
}

FString FAssetDocumentTimelinePlacementUtils::MakeEntryPath(const FString& BasePath, const int32 Index)
{
	return FString::Printf(TEXT("%s/%d"), *BasePath, Index);
}

FString FAssetDocumentTimelinePlacementUtils::MakeFieldPath(
	const FString& EntryPath,
	const FString& FieldName)
{
	return FString::Printf(
		TEXT("%s/%s"),
		*EntryPath,
		*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(FieldName));
}

FString FAssetDocumentTimelinePlacementUtils::CanonicalizeTimeForKey(const double Value)
{
	return FString::Printf(TEXT("%.17g"), Value);
}

FString FAssetDocumentTimelinePlacementUtils::BuildDefaultDuplicateKey(
	const FAssetDocumentTimelinePlacementRegionConfig& Config,
	const FAssetDocumentTimelinePlacementEntry& Entry)
{
	TArray<FString> Pieces;
	if (Config.bHasName && Entry.Name.IsSet())
	{
		Pieces.Add(FString::Printf(TEXT("Name=%s"), *Entry.Name.GetValue()));
	}
	if (Config.bRequireTime && Entry.Time.IsSet())
	{
		Pieces.Add(FString::Printf(TEXT("Time=%s"), *CanonicalizeTimeForKey(Entry.Time.GetValue())));
	}
	if (Config.bHasDuration && Entry.Duration.IsSet())
	{
		Pieces.Add(FString::Printf(TEXT("Duration=%s"), *CanonicalizeTimeForKey(Entry.Duration.GetValue())));
	}
	if (Config.bHasTrackIdentity)
	{
		if (Entry.ResolvedTrackIndex.IsSet())
		{
			Pieces.Add(FString::Printf(TEXT("ResolvedTrackIndex=%d"), Entry.ResolvedTrackIndex.GetValue()));
		}
		else if (Entry.TrackIndex.IsSet())
		{
			Pieces.Add(FString::Printf(TEXT("TrackIndex=%d"), Entry.TrackIndex.GetValue()));
		}
		if (!Entry.CanonicalTrackName.IsEmpty())
		{
			Pieces.Add(FString::Printf(TEXT("CanonicalTrackName=%s"), *Entry.CanonicalTrackName));
		}
		else if (Entry.TrackName.IsSet())
		{
			Pieces.Add(FString::Printf(TEXT("TrackName=%s"), *Entry.TrackName.GetValue()));
		}
	}

	if (Pieces.IsEmpty())
	{
		Pieces.Add(FString::Printf(TEXT("Index=%d"), Entry.Index));
	}
	return FString::Join(Pieces, TEXT("|"));
}

FAssetDocumentCapabilityResult FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries(
	const TSharedPtr<FJsonValue>& Value,
	const FAssetDocumentTimelinePlacementRegionConfig& Config,
	const FString& BasePath,
	const FAssetDocumentTimelineRange* Range,
	TArray<FAssetDocumentTimelinePlacementEntry>& OutEntries,
	const FAssetDocumentTimelinePlacementHooks* Hooks)
{
	OutEntries.Reset();

	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return TimelinePlacementFailure(
			BasePath,
			TEXT("InvalidTimelinePlacementRegionType"),
			TEXT("Expected a JSON array for timeline placement region"));
	}

	const TArray<TSharedPtr<FJsonValue>> Values = Value->AsArray();
	OutEntries.Reserve(Values.Num());
	TSet<FString> SeenDuplicateKeys;
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const FString EntryPath = MakeEntryPath(BasePath, Index);
		const TSharedPtr<FJsonValue>& EntryValue = Values[Index];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			return TimelinePlacementFailure(
				EntryPath,
				TEXT("InvalidTimelinePlacementEntryType"),
				TEXT("Expected a JSON object for timeline placement entry"));
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		if (!EntryObject.IsValid())
		{
			return TimelinePlacementFailure(
				EntryPath,
				TEXT("InvalidTimelinePlacementEntryType"),
				TEXT("Expected a JSON object for timeline placement entry"));
		}

		FAssetDocumentTimelinePlacementEntry Entry;
		Entry.Index = Index;
		Entry.JsonPointer = EntryPath;
		Entry.EntryObject = EntryObject;

		FString FailureCode;
		FString FailureMessage;
		if (!Config.TimeFieldName.IsEmpty())
		{
			if (!TimelinePlacementTryReadNumberField(
				EntryObject.ToSharedRef(),
				Config.TimeFieldName,
				Entry.Time,
				FailureCode,
				FailureMessage))
			{
				return TimelinePlacementFailure(MakeFieldPath(EntryPath, Config.TimeFieldName), FailureCode, FailureMessage);
			}
		}

		if (Config.bHasDuration && !Config.DurationFieldName.IsEmpty())
		{
			if (!TimelinePlacementTryReadNumberField(
				EntryObject.ToSharedRef(),
				Config.DurationFieldName,
				Entry.Duration,
				FailureCode,
				FailureMessage))
			{
				const FString Code = FailureCode == TEXT("InvalidTimelinePlacementNumber")
					&& EntryObject->TryGetField(Config.DurationFieldName).IsValid()
					&& EntryObject->TryGetField(Config.DurationFieldName)->Type != EJson::Number
					? FString(TEXT("InvalidTimelinePlacementDuration"))
					: FailureCode;
				return TimelinePlacementFailure(MakeFieldPath(EntryPath, Config.DurationFieldName), Code, FailureMessage);
			}
		}

		if (Config.bHasName && !Config.NameFieldName.IsEmpty())
		{
			const ETimelinePlacementStringReadResult NameReadResult =
				TimelinePlacementTryReadStringField(EntryObject.ToSharedRef(), Config.NameFieldName, Entry.Name);
			if (NameReadResult == ETimelinePlacementStringReadResult::InvalidType)
			{
				return TimelinePlacementFailure(
					MakeFieldPath(EntryPath, Config.NameFieldName),
					TEXT("InvalidTimelinePlacementName"),
					FString::Printf(TEXT("%s must be a string"), *Config.NameFieldName));
			}
		}

		if (Config.bHasTrackIdentity)
		{
			if (!Config.TrackNameFieldName.IsEmpty())
			{
				const ETimelinePlacementStringReadResult TrackNameReadResult =
					TimelinePlacementTryReadStringField(EntryObject.ToSharedRef(), Config.TrackNameFieldName, Entry.TrackName);
				if (TrackNameReadResult == ETimelinePlacementStringReadResult::InvalidType)
				{
					return TimelinePlacementFailure(
						MakeFieldPath(EntryPath, Config.TrackNameFieldName),
						TEXT("InvalidTimelinePlacementTrackName"),
						FString::Printf(TEXT("%s must be a string"), *Config.TrackNameFieldName));
				}
			}

			if (!Config.TrackIndexFieldName.IsEmpty())
			{
				TOptional<double> TrackIndexNumber;
				if (!TimelinePlacementTryReadNumberField(
					EntryObject.ToSharedRef(),
					Config.TrackIndexFieldName,
					TrackIndexNumber,
					FailureCode,
					FailureMessage))
				{
					return TimelinePlacementFailure(MakeFieldPath(EntryPath, Config.TrackIndexFieldName), FailureCode, FailureMessage);
				}
				if (TrackIndexNumber.IsSet())
				{
					if (!TimelinePlacementIsIntegerNumber(TrackIndexNumber.GetValue())
						|| TrackIndexNumber.GetValue() < 0.0
						|| TrackIndexNumber.GetValue() > static_cast<double>(MAX_int32))
					{
						return TimelinePlacementFailure(
							MakeFieldPath(EntryPath, Config.TrackIndexFieldName),
							TEXT("InvalidTimelinePlacementTrackIndex"),
							FString::Printf(TEXT("%s must be a non-negative integer"), *Config.TrackIndexFieldName));
					}
					Entry.TrackIndex = static_cast<int32>(FMath::RoundToDouble(TrackIndexNumber.GetValue()));
				}
			}
		}

		FAssetDocumentCapabilityResult Result = TimelinePlacementValidateTime(Config, Range, Entry);
		if (!Result.bSuccess)
		{
			return Result;
		}

		Result = TimelinePlacementValidateDuration(Config, Range, Entry);
		if (!Result.bSuccess)
		{
			return Result;
		}

		Result = TimelinePlacementValidateName(Config, Entry);
		if (!Result.bSuccess)
		{
			return Result;
		}

		Result = TimelinePlacementResolveTrack(Config, Entry);
		if (!Result.bSuccess)
		{
			return Result;
		}

		Entry.DuplicateKey = Hooks && Hooks->BuildDuplicateKey
			? Hooks->BuildDuplicateKey(Entry)
			: BuildDefaultDuplicateKey(Config, Entry);
		if (SeenDuplicateKeys.Contains(Entry.DuplicateKey))
		{
			return TimelinePlacementFailure(
				EntryPath,
				TEXT("DuplicateTimelinePlacementKey"),
				FString::Printf(TEXT("Duplicate timeline placement key %s"), *Entry.DuplicateKey));
		}
		SeenDuplicateKeys.Add(Entry.DuplicateKey);

		OutEntries.Add(MoveTemp(Entry));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Parsed timeline placement region"));
}

TSharedRef<FJsonValue> FAssetDocumentTimelinePlacementUtils::MakeArrayValue(
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

FAssetDocumentTimelinePlacementRegionAdapter::FAssetDocumentTimelinePlacementRegionAdapter(
	FAssetDocumentTimelinePlacementRegionConfig InConfig,
	FAssetDocumentTimelinePlacementHooks InHooks)
	: Config(MoveTemp(InConfig))
	, Hooks(MoveTemp(InHooks))
{
}

FName FAssetDocumentTimelinePlacementRegionAdapter::GetName() const
{
	return Config.AdapterName;
}

bool FAssetDocumentTimelinePlacementRegionAdapter::SupportsRegion(
	const FAssetDocumentRegionContext& Context) const
{
	return (!Config.RegionId.IsNone() && Context.RegionId == Config.RegionId)
		|| (!Config.BodyPath.IsEmpty() && Context.BodyPath == Config.BodyPath)
		|| (!Config.JsonPointer.IsEmpty() && Context.JsonPointer == Config.JsonPointer);
}

TSharedRef<FJsonObject> FAssetDocumentTimelinePlacementRegionAdapter::GetSchemaHint(
	const FAssetDocumentRegionContext&) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Adapter"), GetName().ToString());
	if (!Config.SchemaLabel.IsEmpty())
	{
		Schema->SetStringField(TEXT("Label"), Config.SchemaLabel);
	}
	Schema->SetStringField(TEXT("Kind"), TEXT("TimelinePlacement"));
	Schema->SetStringField(TEXT("Shape"), TEXT("array<object>"));
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentTimelinePlacementRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	TArray<FAssetDocumentTimelinePlacementEntry> Entries;
	FAssetDocumentCapabilityResult Result = ParseEntries(Context, DesiredValue, Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Hooks.Validate)
	{
		return Hooks.Validate(Context, Entries);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated timeline placement region"));
}

FAssetDocumentCapabilityResult FAssetDocumentTimelinePlacementRegionAdapter::PreflightRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	TArray<FAssetDocumentTimelinePlacementEntry> Entries;
	FAssetDocumentCapabilityResult Result = ParseEntries(Context, DesiredValue, Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = TimelinePlacementValidateEntriesForLifecycle(Config, Hooks, Context, Entries, RegionPath(Context), TEXT("validate"), false);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!Hooks.Preflight)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Timeline placement preflight no-op"));
	}

	return Hooks.Preflight(Context, Entries);
}

FAssetDocumentCapabilityResult FAssetDocumentTimelinePlacementRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;

	TArray<FAssetDocumentTimelinePlacementEntry> Entries;
	FAssetDocumentCapabilityResult Result = ParseEntries(Context, DesiredValue, Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = TimelinePlacementValidateEntriesForLifecycle(Config, Hooks, Context, Entries, RegionPath(Context), TEXT("validate"), false);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!Hooks.Apply)
	{
		return TimelinePlacementUnsupportedLifecycleFailure(Config, Context, RegionPath(Context), TEXT("apply"));
	}

	return Hooks.Apply(Context, Entries, bOutChanged);
}

FAssetDocumentCapabilityResult FAssetDocumentTimelinePlacementRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	OutCurrentValue.Reset();
	if (!Hooks.Extract)
	{
		return TimelinePlacementUnsupportedLifecycleFailure(Config, Context, RegionPath(Context), TEXT("extract"));
	}

	TArray<TSharedRef<FJsonObject>> Entries;
	const FAssetDocumentCapabilityResult Result = Hooks.Extract(Context, Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutCurrentValue = FAssetDocumentTimelinePlacementUtils::MakeArrayValue(Entries);
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted timeline placement region"));
}

FAssetDocumentCapabilityResult FAssetDocumentTimelinePlacementRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TArray<FAssetDocumentTimelinePlacementEntry> DesiredEntries;
	FAssetDocumentCapabilityResult Result = ParseEntries(Context, DesiredValue, DesiredEntries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = TimelinePlacementValidateEntriesForLifecycle(Config, Hooks, Context, DesiredEntries, RegionPath(Context), TEXT("validate"), false);
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
		return TimelinePlacementUnsupportedLifecycleFailure(Config, Context, RegionPath(Context), TEXT("diff"));
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
			RegionPath(Context),
			TEXT("changed"),
			CurrentValue,
			DesiredValue);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed timeline placement region"));
}

FString FAssetDocumentTimelinePlacementRegionAdapter::RegionPath(
	const FAssetDocumentRegionContext& Context) const
{
	if (!Config.JsonPointer.IsEmpty())
	{
		return Config.JsonPointer;
	}
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FAssetDocumentCapabilityResult FAssetDocumentTimelinePlacementRegionAdapter::ParseEntries(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<FAssetDocumentTimelinePlacementEntry>& OutEntries) const
{
	TOptional<FAssetDocumentTimelineRange> Range;
	if (Hooks.GetTimelineRange)
	{
		Range = Hooks.GetTimelineRange(Context);
	}

	return FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries(
		DesiredValue,
		Config,
		RegionPath(Context),
		Range.IsSet() ? &Range.GetValue() : nullptr,
		OutEntries,
		&Hooks);
}
