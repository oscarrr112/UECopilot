// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimMontageNotifyPlacementAdapter.h"

#include "AssetDocumentFragmentCompiler.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentPropertyAdapter.h"
#include "Regions/AssetDocumentTimelinePlacementRegionAdapter.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectGlobals.h"

#include <cmath>

namespace
{
const FName ManagedNotifyName(TEXT("AssetDocument.Notify"));
const FName ManagedNotifyStateName(TEXT("AssetDocument.NotifyState"));
const TCHAR* ManagedNotifyObjectPrefix = TEXT("AssetDocumentManaged_Notify_");
const TCHAR* ManagedNotifyStateObjectPrefix = TEXT("AssetDocumentManaged_NotifyState_");

FAssetDocumentCapabilityResult NotifyBodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult NotifyFragmentFailure(const FAssetDocumentFragmentResult& FragmentResult)
{
	FAssetDocumentCapabilityResult Result = FAssetDocumentCapabilityResult::Failure(FragmentResult.Message);
	Result.Diagnostics = FragmentResult.Diagnostics;
	return Result;
}

bool HasReservedManagedObjectName(const UObject* Object, const TCHAR* Prefix)
{
	return Object && Object->GetName().StartsWith(Prefix);
}

bool HasExpectedOuter(const UObject* Object, const UAnimMontage* Montage)
{
	return !Montage || (Object && Object->GetOuter() == Montage);
}

void MarkManagedNotifyObject(UObject* NotifyObject, UAnimMontage* Montage, bool bState)
{
	if (!NotifyObject || !Montage)
	{
		return;
	}

	const TCHAR* Prefix = bState ? ManagedNotifyStateObjectPrefix : ManagedNotifyObjectPrefix;
	const FName ManagedObjectName = MakeUniqueObjectName(Montage, NotifyObject->GetClass(), Prefix);
	NotifyObject->Rename(*ManagedObjectName.ToString(), Montage, REN_DontCreateRedirectors | REN_NonTransactional);
}

FString PlacementPath(const TCHAR* SectionName, int32 Index)
{
	return FString::Printf(TEXT("/Body/%s[%d]"), SectionName, Index);
}

FString PlacementFieldPath(const TCHAR* SectionName, int32 Index, const TCHAR* FieldName)
{
	return FString::Printf(TEXT("/Body/%s[%d]/%s"), SectionName, Index, FieldName);
}

FAssetDocumentCapabilityResult NotifyRequireObjectValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return NotifyBodyFailure(TEXT("Expected a JSON object"), Path, TEXT("InvalidNotifyPlacement"));
	}

	OutObject = Value->AsObject();
	if (!OutObject.IsValid())
	{
		return NotifyBodyFailure(TEXT("Expected a JSON object"), Path, TEXT("InvalidNotifyPlacement"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FString TimelinePathToMontagePlacementPath(const FString& Path, const TCHAR* SectionName)
{
	const FString Prefix = FString::Printf(TEXT("/Body/%s/"), SectionName);
	if (!Path.StartsWith(Prefix))
	{
		return Path;
	}

	const FString Suffix = Path.RightChop(Prefix.Len());
	int32 SlashIndex = INDEX_NONE;
	const bool bHasSlash = Suffix.FindChar(TEXT('/'), SlashIndex);
	const FString IndexString = bHasSlash ? Suffix.Left(SlashIndex) : Suffix;
	if (IndexString.IsEmpty() || !IndexString.IsNumeric())
	{
		return Path;
	}

	const FString Rest = bHasSlash ? Suffix.RightChop(SlashIndex) : FString();
	return FString::Printf(TEXT("/Body/%s[%s]%s"), SectionName, *IndexString, *Rest);
}

FAssetDocumentCapabilityResult MapTimelinePlacementFailure(
	const FAssetDocumentCapabilityResult& Result,
	const TCHAR* SectionName)
{
	if (Result.bSuccess || Result.Diagnostics.IsEmpty())
	{
		return Result;
	}

	const FAssetDocumentDiagnostic& Diagnostic = Result.Diagnostics[0];
	FString Path = TimelinePathToMontagePlacementPath(Diagnostic.Path, SectionName);
	FString Code = Diagnostic.Code;
	if (Code == TEXT("InvalidTimelinePlacementRegionType"))
	{
		Code = TEXT("InvalidBodySectionType");
	}
	else if (Code == TEXT("InvalidTimelinePlacementEntryType"))
	{
		Code = TEXT("InvalidNotifyPlacement");
	}
	else if (Code == TEXT("InvalidTimelinePlacementNumber")
		|| Code == TEXT("MissingTimelinePlacementTime")
		|| Code == TEXT("MissingTimelinePlacementDuration"))
	{
		Code = Path.EndsWith(TEXT("/TrackIndex"))
			? FString(TEXT("InvalidTrackIndex"))
			: FString(TEXT("InvalidNumericField"));
	}
	else if (Code == TEXT("InvalidTimelinePlacementTime"))
	{
		Code = TEXT("InvalidTime");
	}
	else if (Code == TEXT("InvalidTimelinePlacementDuration"))
	{
		Code = Result.Message.Contains(TEXT("positive"))
			? FString(TEXT("InvalidNotifyStateDuration"))
			: FString(TEXT("InvalidNumericField"));
	}
	else if (Code == TEXT("InvalidTimelinePlacementEndTime"))
	{
		Code = TEXT("InvalidNotifyStateDuration");
	}
	else if (Code == TEXT("InvalidTimelinePlacementTrackIndex"))
	{
		Code = TEXT("InvalidTrackIndex");
	}
	else if (Code == TEXT("DuplicateTimelinePlacementKey"))
	{
		Code = FCString::Strcmp(SectionName, TEXT("Notifies")) == 0
			? FString(TEXT("DuplicateNotifyPlacementKey"))
			: FString(TEXT("DuplicateNotifyStatePlacementKey"));
	}

	return NotifyBodyFailure(Result.Message, Path, Code);
}

FAssetDocumentCapabilityResult ValidatePlacementFloatSafety(
	const TSharedPtr<FJsonValue>& SectionValue,
	const TCHAR* SectionName,
	const TArray<const TCHAR*>& NumericFieldNames)
{
	if (!SectionValue.IsValid() || SectionValue->Type != EJson::Array)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>& Values = SectionValue->AsArray();
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& EntryValue = Values[Index];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		if (!EntryObject.IsValid())
		{
			continue;
		}

		for (const TCHAR* FieldName : NumericFieldNames)
		{
			const TSharedPtr<FJsonValue> NumberValue = EntryObject->TryGetField(FieldName);
			if (!NumberValue.IsValid() || NumberValue->Type != EJson::Number)
			{
				continue;
			}

			const double Number = NumberValue->AsNumber();
			if (!std::isfinite(Number) || Number < -static_cast<double>(MAX_flt) || Number > static_cast<double>(MAX_flt))
			{
				return NotifyBodyFailure(
					FString::Printf(TEXT("%s must fit in a float"), FieldName),
					PlacementFieldPath(SectionName, Index, FieldName),
					TEXT("InvalidNumericField"));
			}
			const float FloatNumber = static_cast<float>(Number);
			if (!FMath::IsFinite(FloatNumber))
			{
				return NotifyBodyFailure(
					FString::Printf(TEXT("%s must fit in a finite float"), FieldName),
					PlacementFieldPath(SectionName, Index, FieldName),
					TEXT("InvalidNumericField"));
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentTimelinePlacementRegionConfig MakePlacementConfig(const TCHAR* SectionName, bool bState)
{
	FAssetDocumentTimelinePlacementRegionConfig Config;
	Config.AdapterName = bState
		? FName(TEXT("AnimMontageNotifyStatesTimelinePlacement"))
		: FName(TEXT("AnimMontageNotifiesTimelinePlacement"));
	Config.RegionId = bState ? FName(TEXT("Body.NotifyStates")) : FName(TEXT("Body.Notifies"));
	Config.BodyPath = SectionName;
	Config.JsonPointer = FString::Printf(TEXT("/Body/%s"), SectionName);
	Config.TimeFieldName = TEXT("Time");
	Config.TrackIndexFieldName = TEXT("TrackIndex");
	Config.bHasTrackIdentity = true;
	if (bState)
	{
		Config.DurationFieldName = TEXT("Duration");
		Config.bHasDuration = true;
		Config.bRequireDuration = true;
		Config.bRequirePositiveDuration = true;
		Config.bValidateEndTime = true;
	}
	return Config;
}

FString ReadPlacementObjectIdentity(const FAssetDocumentTimelinePlacementEntry& Entry)
{
	const TSharedPtr<FJsonValue> ObjectValue = Entry.EntryObject.IsValid()
		? Entry.EntryObject->TryGetField(TEXT("Object"))
		: nullptr;
	if (!ObjectValue.IsValid())
	{
		return TEXT("Object=<missing>");
	}

	return FString::Printf(
		TEXT("Object=%s"),
		*FAssetDocumentJsonRegionUtils::JsonValueToComparableString(ObjectValue));
}

FAssetDocumentTimelinePlacementHooks MakePlacementHooks(const TCHAR* SectionName, bool bState)
{
	FAssetDocumentTimelinePlacementHooks Hooks;
	const FString RegionName(SectionName);
	Hooks.BuildDuplicateKey = [RegionName, bState](const FAssetDocumentTimelinePlacementEntry& Entry)
	{
		TArray<FString> Pieces;
		Pieces.Add(FString::Printf(TEXT("Region=%s"), *RegionName));
		Pieces.Add(FString::Printf(
			TEXT("Time=%s"),
			Entry.Time.IsSet()
				? *FAssetDocumentTimelinePlacementUtils::CanonicalizeTimeForKey(Entry.Time.GetValue())
				: TEXT("<missing>")));
		if (bState)
		{
			Pieces.Add(FString::Printf(
				TEXT("Duration=%s"),
				Entry.Duration.IsSet()
					? *FAssetDocumentTimelinePlacementUtils::CanonicalizeTimeForKey(Entry.Duration.GetValue())
					: TEXT("<missing>")));
		}
		Pieces.Add(FString::Printf(
			TEXT("TrackIndex=%d"),
			Entry.TrackIndex.IsSet() ? Entry.TrackIndex.GetValue() : 0));
		Pieces.Add(ReadPlacementObjectIdentity(Entry));
		return FString::Join(Pieces, TEXT("|"));
	};
	return Hooks;
}

FAssetDocumentCapabilityResult ParsePlacementEntries(
	const TSharedRef<FJsonObject>& BodyObject,
	const TCHAR* SectionName,
	bool bState,
	TArray<FAssetDocumentTimelinePlacementEntry>& OutEntries)
{
	OutEntries.Reset();

	const TSharedPtr<FJsonValue>* SectionValue = BodyObject->Values.Find(SectionName);
	if (!SectionValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const FAssetDocumentCapabilityResult FloatSafetyResult = ValidatePlacementFloatSafety(
		*SectionValue,
		SectionName,
		bState ? TArray<const TCHAR*>{ TEXT("Time"), TEXT("Duration") } : TArray<const TCHAR*>{ TEXT("Time") });
	if (!FloatSafetyResult.bSuccess)
	{
		return FloatSafetyResult;
	}

	FAssetDocumentTimelinePlacementRegionConfig Config = MakePlacementConfig(SectionName, bState);
	FAssetDocumentTimelinePlacementHooks Hooks = MakePlacementHooks(SectionName, bState);
	const FAssetDocumentCapabilityResult Result = FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries(
		*SectionValue,
		Config,
		Config.JsonPointer,
		nullptr,
		OutEntries,
		&Hooks);
	if (!Result.bSuccess)
	{
		return MapTimelinePlacementFailure(Result, SectionName);
	}

	return FAssetDocumentCapabilityResult::Success();
}

bool ResolveClassAllowAbstract(const FString& ClassName, UClass*& OutClass, FString& OutError)
{
	OutClass = FindObject<UClass>(nullptr, *ClassName);
	if (!OutClass)
	{
		OutClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassName);
	}

	if (!OutClass)
	{
		OutError = FString::Printf(TEXT("Failed to resolve class '%s'."), *ClassName);
		return false;
	}

	return true;
}

FAssetDocumentCapabilityResult ValidateEmbeddedObjectClass(
	const TSharedRef<FJsonObject>& ObjectFragment,
	UClass* ExpectedBaseClass,
	const FString& Path)
{
	FString ClassName;
	if (!ObjectFragment->TryGetStringField(TEXT("Class"), ClassName) || ClassName.TrimStartAndEnd().IsEmpty())
	{
		return NotifyBodyFailure(TEXT("EmbeddedObject field 'Class' is required."), Path, TEXT("missing-embeddedobject-class"));
	}

	UClass* ResolvedClass = nullptr;
	FString Error;
	if (!ResolveClassAllowAbstract(ClassName, ResolvedClass, Error))
	{
		return NotifyBodyFailure(Error, Path, TEXT("object-class-resolve-failed"));
	}

	if (ExpectedBaseClass && !ResolvedClass->IsChildOf(ExpectedBaseClass))
	{
		return NotifyBodyFailure(
			FString::Printf(TEXT("Class '%s' is not a child of '%s'."), *ResolvedClass->GetName(), *ExpectedBaseClass->GetName()),
			Path,
			TEXT("embeddedobject-base-class-mismatch"));
	}

	if (ResolvedClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return NotifyBodyFailure(
			FString::Printf(TEXT("Class '%s' is abstract and cannot be used as an AnimMontage notify object."), *ResolvedClass->GetName()),
			Path,
			TEXT("AbstractNotifyClass"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateObjectProducingFragment(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& ObjectFragment,
	UClass* ExpectedBaseClass,
	const FString& Path,
	TSet<FString>& DefinitionStack)
{
	FString Kind;
	if (!ObjectFragment->TryGetStringField(TEXT("Kind"), Kind) || Kind.TrimStartAndEnd().IsEmpty())
	{
		return NotifyBodyFailure(TEXT("Fragment Kind must be a string."), Path, TEXT("missing-fragment-kind"));
	}

	if (Kind == TEXT("EmbeddedObject"))
	{
		return ValidateEmbeddedObjectClass(ObjectFragment, ExpectedBaseClass, Path);
	}

	if (Kind == TEXT("DefinitionRef"))
	{
		FString Id;
		if (!ObjectFragment->TryGetStringField(TEXT("Id"), Id) || Id.TrimStartAndEnd().IsEmpty())
		{
			return NotifyBodyFailure(TEXT("DefinitionRef field 'Id' is required."), Path, TEXT("missing-definitionref-id"));
		}

		if (!Context.Definitions || !Context.Definitions->IsValid())
		{
			return NotifyBodyFailure(TEXT("DefinitionRef requires Definitions."), Path, TEXT("definitionref-missing-definitions"));
		}

		if (DefinitionStack.Contains(Id))
		{
			return NotifyBodyFailure(FString::Printf(TEXT("DefinitionRef cycle detected at '%s'."), *Id), Path, TEXT("definitionref-cycle"));
		}

		const TSharedPtr<FJsonObject>* DefinitionJson = nullptr;
		if (!(*Context.Definitions)->TryGetObjectField(Id, DefinitionJson) || !DefinitionJson || !DefinitionJson->IsValid())
		{
			return NotifyBodyFailure(FString::Printf(TEXT("Definition '%s' was not found."), *Id), Path, TEXT("definitionref-missing-id"));
		}

		DefinitionStack.Add(Id);
		const FAssetDocumentCapabilityResult Result = ValidateObjectProducingFragment(Context, DefinitionJson->ToSharedRef(), ExpectedBaseClass, Path, DefinitionStack);
		DefinitionStack.Remove(Id);
		return Result;
	}

	return NotifyBodyFailure(TEXT("Notify placement Object must be an EmbeddedObject or DefinitionRef to an EmbeddedObject."), Path, TEXT("InvalidNotifyObjectFragment"));
}

FAssetDocumentCapabilityResult ValidatePlacementArray(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject,
	const TCHAR* SectionName,
	bool bState,
	UClass* ExpectedBaseClass)
{
	TArray<FAssetDocumentTimelinePlacementEntry> Placements;
	FAssetDocumentCapabilityResult Result = ParsePlacementEntries(BodyObject, SectionName, bState, Placements);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FAssetDocumentTimelinePlacementEntry& Placement : Placements)
	{
		const FString BasePath = PlacementPath(SectionName, Placement.Index);
		const TSharedPtr<FJsonValue>* ObjectValue = Placement.EntryObject->Values.Find(TEXT("Object"));
		if (!ObjectValue)
		{
			return NotifyBodyFailure(TEXT("Notify placement requires Object fragment"), BasePath / TEXT("Object"), TEXT("MissingNotifyObject"));
		}

		TSharedPtr<FJsonObject> ObjectFragment;
		Result = NotifyRequireObjectValue(*ObjectValue, BasePath / TEXT("Object"), ObjectFragment);
		if (!Result.bSuccess)
		{
			return Result;
		}

		TSet<FString> DefinitionStack;
		Result = ValidateObjectProducingFragment(Context, ObjectFragment.ToSharedRef(), ExpectedBaseClass, BasePath / TEXT("Object"), DefinitionStack);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult CompilePlacementArray(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& BodyObject,
	const TCHAR* SectionName,
	bool bState,
	UClass* ExpectedBaseClass,
	TArray<FAnimNotifyEvent>& OutEvents)
{
	TArray<FAssetDocumentTimelinePlacementEntry> Placements;
	FAssetDocumentCapabilityResult Result = ParsePlacementEntries(BodyObject, SectionName, bState, Placements);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FAssetDocumentTimelinePlacementEntry& Placement : Placements)
	{
		const FString BasePath = PlacementPath(SectionName, Placement.Index);
		const TSharedPtr<FJsonValue>* ObjectValue = Placement.EntryObject->Values.Find(TEXT("Object"));
		if (!ObjectValue)
		{
			return NotifyBodyFailure(TEXT("Notify placement requires Object fragment"), BasePath / TEXT("Object"), TEXT("MissingNotifyObject"));
		}

		TSharedPtr<FJsonObject> ObjectFragment;
		Result = NotifyRequireObjectValue(*ObjectValue, BasePath / TEXT("Object"), ObjectFragment);
		if (!Result.bSuccess)
		{
			return Result;
		}

		TSet<FString> DefinitionStack;
		Result = ValidateObjectProducingFragment(Context, ObjectFragment.ToSharedRef(), ExpectedBaseClass, BasePath / TEXT("Object"), DefinitionStack);
		if (!Result.bSuccess)
		{
			return Result;
		}

		FAssetDocumentFragmentContext FragmentContext;
		FragmentContext.OwnerAsset = Montage;
		FragmentContext.Outer = Montage;
		FragmentContext.ExpectedBaseClass = ExpectedBaseClass;
		FragmentContext.Definitions = Context.Definitions;
		FragmentContext.JsonPath = BasePath / TEXT("Object");

		FAssetDocumentFragmentResult FragmentResult = Compiler.Compile(ObjectFragment.ToSharedRef(), FragmentContext);
		if (!FragmentResult.bSuccess)
		{
			return NotifyFragmentFailure(FragmentResult);
		}

		UObject* NotifyObject = FragmentResult.Object;
		if (!NotifyObject || !NotifyObject->IsA(ExpectedBaseClass))
		{
			return NotifyBodyFailure(TEXT("Object fragment did not resolve to the expected notify class"), BasePath / TEXT("Object"), TEXT("InvalidNotifyObject"));
		}
		MarkManagedNotifyObject(NotifyObject, Montage, bState);

		const double Time = Placement.Time.GetValue();
		const int32 TrackIndex = Placement.TrackIndex.IsSet() ? Placement.TrackIndex.GetValue() : 0;
		const double Duration = Placement.Duration.IsSet() ? Placement.Duration.GetValue() : 0.0;
		const float NotifyTime = static_cast<float>(Time);
		FAnimNotifyEvent NotifyEvent;
		NotifyEvent.TrackIndex = TrackIndex;
		NotifyEvent.SetTime(NotifyTime);
		NotifyEvent.RefreshTriggerOffset(Montage->CalculateOffsetForNotify(NotifyTime));
#if WITH_EDITORONLY_DATA
		NotifyEvent.Guid = FGuid::NewGuid();
#endif

		if (bState)
		{
			NotifyEvent.NotifyStateClass = Cast<UAnimNotifyState>(NotifyObject);
			NotifyEvent.NotifyName = ManagedNotifyStateName;
			NotifyEvent.SetDuration(static_cast<float>(Duration));
			NotifyEvent.RefreshEndTriggerOffset(Montage->CalculateOffsetForNotify(NotifyTime + static_cast<float>(Duration)));
		}
		else
		{
			NotifyEvent.Notify = Cast<UAnimNotify>(NotifyObject);
			NotifyEvent.NotifyName = ManagedNotifyName;
		}

		OutEvents.Add(NotifyEvent);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractEmbeddedObject(
	const FAssetDocumentFragmentCompiler& Compiler,
	UObject* OwnerAsset,
	UObject* ValueObject,
	const FString& JsonPath,
	TSharedRef<FJsonObject>& OutFragment)
{
	FAssetDocumentFragmentExtractContext ExtractContext;
	ExtractContext.OwnerAsset = OwnerAsset;
	ExtractContext.ValueObject = ValueObject;
	ExtractContext.Kind = TEXT("EmbeddedObject");
	ExtractContext.JsonPath = JsonPath;

	const FAssetDocumentFragmentResult FragmentResult = Compiler.Extract(ExtractContext, OutFragment);
	if (!FragmentResult.bSuccess)
	{
		return NotifyFragmentFailure(FragmentResult);
	}

	if (TSharedPtr<FJsonObject> Properties = FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(ValueObject, true))
	{
		OutFragment->SetObjectField(TEXT("Properties"), Properties);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractPlacement(
	const FAssetDocumentFragmentCompiler& Compiler,
	const UAnimMontage* Montage,
	const FAnimNotifyEvent& Event,
	bool bState,
	int32 Index,
	TSharedRef<FJsonObject>& OutPlacement)
{
	UObject* NotifyObject = bState ? static_cast<UObject*>(Event.NotifyStateClass) : static_cast<UObject*>(Event.Notify);
	if (!NotifyObject)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	OutPlacement->SetNumberField(TEXT("Time"), Event.GetTime());
	if (Event.TrackIndex != 0)
	{
		OutPlacement->SetNumberField(TEXT("TrackIndex"), Event.TrackIndex);
	}

	if (bState)
	{
		OutPlacement->SetNumberField(TEXT("Duration"), Event.GetDuration());
	}

	TSharedRef<FJsonObject> ObjectFragment = MakeShared<FJsonObject>();
	const TCHAR* SectionName = bState ? TEXT("NotifyStates") : TEXT("Notifies");
	const FAssetDocumentCapabilityResult Result = ExtractEmbeddedObject(
		Compiler,
		const_cast<UAnimMontage*>(Montage),
		NotifyObject,
		PlacementFieldPath(SectionName, Index, TEXT("Object")),
		ObjectFragment);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutPlacement->SetObjectField(TEXT("Object"), ObjectFragment);
	return FAssetDocumentCapabilityResult::Success();
}

TSharedRef<FJsonObject> MakeSkippedNotifyMetadata(const FAnimNotifyEvent& Event, bool bState)
{
	TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
	const UObject* NotifyObject = bState ? static_cast<const UObject*>(Event.NotifyStateClass) : static_cast<const UObject*>(Event.Notify);
	Metadata->SetStringField(TEXT("Reason"), bState ? TEXT("unmanaged-notify-state") : TEXT("unmanaged-notify"));
	Metadata->SetNumberField(TEXT("Time"), Event.GetTime());
	Metadata->SetNumberField(TEXT("TrackIndex"), Event.TrackIndex);
	if (bState)
	{
		Metadata->SetNumberField(TEXT("Duration"), Event.GetDuration());
	}
	if (NotifyObject)
	{
		Metadata->SetStringField(TEXT("Class"), NotifyObject->GetClass()->GetPathName());
		Metadata->SetStringField(TEXT("ObjectName"), NotifyObject->GetName());
	}
	if (!Event.NotifyName.IsNone())
	{
		Metadata->SetStringField(TEXT("NotifyName"), Event.NotifyName.ToString());
	}
	return Metadata;
}
}

FAssetDocumentCapabilityResult FAnimMontageNotifyPlacementAdapter::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject) const
{
	FAssetDocumentCapabilityResult Result = ValidatePlacementArray(Context, BodyObject, TEXT("Notifies"), false, UAnimNotify::StaticClass());
	if (!Result.bSuccess)
	{
		return Result;
	}

	return ValidatePlacementArray(Context, BodyObject, TEXT("NotifyStates"), true, UAnimNotifyState::StaticClass());
}

bool FAnimMontageNotifyPlacementAdapter::IsManagedNotifyEvent(const FAnimNotifyEvent& Event, const UAnimMontage* Montage)
{
	return Event.Notify
		&& Event.NotifyName == ManagedNotifyName
		&& HasReservedManagedObjectName(Event.Notify, ManagedNotifyObjectPrefix)
		&& HasExpectedOuter(Event.Notify, Montage);
}

bool FAnimMontageNotifyPlacementAdapter::IsManagedNotifyStateEvent(const FAnimNotifyEvent& Event, const UAnimMontage* Montage)
{
	return Event.NotifyStateClass
		&& Event.NotifyName == ManagedNotifyStateName
		&& HasReservedManagedObjectName(Event.NotifyStateClass, ManagedNotifyStateObjectPrefix)
		&& HasExpectedOuter(Event.NotifyStateClass, Montage);
}

FAssetDocumentCapabilityResult FAnimMontageNotifyPlacementAdapter::Compile(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& BodyObject,
	FAnimMontageNotifyPlacementResult& OutResult) const
{
	if (BodyObject->HasField(TEXT("Notifies")))
	{
		OutResult.bHasNotifies = true;
		FAssetDocumentCapabilityResult Result = CompilePlacementArray(Compiler, Context, Montage, BodyObject, TEXT("Notifies"), false, UAnimNotify::StaticClass(), OutResult.Notifies);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (BodyObject->HasField(TEXT("NotifyStates")))
	{
		OutResult.bHasNotifyStates = true;
		FAssetDocumentCapabilityResult Result = CompilePlacementArray(Compiler, Context, Montage, BodyObject, TEXT("NotifyStates"), true, UAnimNotifyState::StaticClass(), OutResult.NotifyStates);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAnimMontageNotifyPlacementAdapter::Extract(
	const FAssetDocumentFragmentCompiler& Compiler,
	const UAnimMontage* Montage,
	TSharedRef<FJsonObject>& OutBodyJson) const
{
	if (!Montage)
	{
		return NotifyBodyFailure(TEXT("AnimMontage notify extraction requires an asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	TArray<TSharedPtr<FJsonValue>> Notifies;
	TArray<TSharedPtr<FJsonValue>> NotifyStates;
	TArray<TSharedPtr<FJsonValue>> SkippedNotifies;
	TArray<TSharedPtr<FJsonValue>> SkippedNotifyStates;
	int32 NotifyIndex = 0;
	int32 NotifyStateIndex = 0;

	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (Event.Notify)
		{
			if (!IsManagedNotifyEvent(Event, Montage))
			{
				SkippedNotifies.Add(MakeShared<FJsonValueObject>(MakeSkippedNotifyMetadata(Event, false)));
				continue;
			}

			TSharedRef<FJsonObject> Placement = MakeShared<FJsonObject>();
			const FAssetDocumentCapabilityResult Result = ExtractPlacement(Compiler, Montage, Event, false, NotifyIndex, Placement);
			if (!Result.bSuccess)
			{
				return Result;
			}
			Notifies.Add(MakeShared<FJsonValueObject>(Placement));
			++NotifyIndex;
		}
		else if (Event.NotifyStateClass)
		{
			if (!IsManagedNotifyStateEvent(Event, Montage))
			{
				SkippedNotifyStates.Add(MakeShared<FJsonValueObject>(MakeSkippedNotifyMetadata(Event, true)));
				continue;
			}

			TSharedRef<FJsonObject> Placement = MakeShared<FJsonObject>();
			const FAssetDocumentCapabilityResult Result = ExtractPlacement(Compiler, Montage, Event, true, NotifyStateIndex, Placement);
			if (!Result.bSuccess)
			{
				return Result;
			}
			NotifyStates.Add(MakeShared<FJsonValueObject>(Placement));
			++NotifyStateIndex;
		}
	}

	OutBodyJson->SetArrayField(TEXT("Notifies"), Notifies);
	OutBodyJson->SetArrayField(TEXT("NotifyStates"), NotifyStates);

	TSharedRef<FJsonObject> Skipped = MakeShared<FJsonObject>();
	Skipped->SetStringField(TEXT("BranchingPoints"), TEXT("deferred"));
	Skipped->SetNumberField(TEXT("UnmanagedNotifies"), SkippedNotifies.Num());
	Skipped->SetNumberField(TEXT("UnmanagedNotifyStates"), SkippedNotifyStates.Num());
	Skipped->SetArrayField(TEXT("SkippedNotifies"), SkippedNotifies);
	Skipped->SetArrayField(TEXT("SkippedNotifyStates"), SkippedNotifyStates);
	OutBodyJson->SetObjectField(TEXT("_Skipped"), Skipped);

	return FAssetDocumentCapabilityResult::Success();
}
