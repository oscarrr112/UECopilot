// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimMontageNotifyPlacementAdapter.h"

#include "AssetDocumentFragmentCompiler.h"
#include "AssetDocumentPropertyAdapter.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"

namespace
{
FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult FragmentFailure(const FAssetDocumentFragmentResult& FragmentResult)
{
	FAssetDocumentCapabilityResult Result = FAssetDocumentCapabilityResult::Failure(FragmentResult.Message);
	Result.Diagnostics = FragmentResult.Diagnostics;
	return Result;
}

FString PlacementPath(const TCHAR* SectionName, int32 Index)
{
	return FString::Printf(TEXT("/Body/%s[%d]"), SectionName, Index);
}

FString PlacementFieldPath(const TCHAR* SectionName, int32 Index, const TCHAR* FieldName)
{
	return FString::Printf(TEXT("/Body/%s[%d]/%s"), SectionName, Index, FieldName);
}

FAssetDocumentCapabilityResult RequireObjectValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Expected a JSON object"), Path, TEXT("InvalidNotifyPlacement"));
	}

	OutObject = Value->AsObject();
	if (!OutObject.IsValid())
	{
		return BodyFailure(TEXT("Expected a JSON object"), Path, TEXT("InvalidNotifyPlacement"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadRequiredNonNegativeNumber(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	double& OutValue)
{
	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName);
	if (!Value || !Value->IsValid() || (*Value)->Type != EJson::Number)
	{
		return BodyFailure(FString::Printf(TEXT("%s must be a number"), FieldName), Path, TEXT("InvalidNumericField"));
	}

	OutValue = (*Value)->AsNumber();
	if (OutValue < 0.0)
	{
		return BodyFailure(FString::Printf(TEXT("%s must be non-negative"), FieldName), Path, TEXT("InvalidTime"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadPositiveNumber(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	double& OutValue)
{
	const FAssetDocumentCapabilityResult Result = ReadRequiredNonNegativeNumber(Object, FieldName, Path, OutValue);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (OutValue <= 0.0)
	{
		return BodyFailure(FString::Printf(TEXT("%s must be greater than zero"), FieldName), Path, TEXT("InvalidNotifyStateDuration"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadOptionalTrackIndex(const TSharedRef<FJsonObject>& Object, const FString& Path, int32& OutTrackIndex)
{
	OutTrackIndex = 0;

	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(TEXT("TrackIndex"));
	if (!Value)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (!Value->IsValid() || (*Value)->Type != EJson::Number)
	{
		return BodyFailure(TEXT("TrackIndex must be a non-negative integer"), Path, TEXT("InvalidTrackIndex"));
	}

	const double NumberValue = (*Value)->AsNumber();
	const double RoundedValue = FMath::RoundToDouble(NumberValue);
	if (NumberValue < 0.0 || !FMath::IsNearlyEqual(NumberValue, RoundedValue))
	{
		return BodyFailure(TEXT("TrackIndex must be a non-negative integer"), Path, TEXT("InvalidTrackIndex"));
	}

	OutTrackIndex = static_cast<int32>(RoundedValue);
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

UClass* FindLoadedConcreteChildClass(UClass* BaseClass)
{
	TArray<UClass*> Candidates;
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* CandidateClass = *ClassIt;
		if (!CandidateClass || CandidateClass == BaseClass)
		{
			continue;
		}
		if (CandidateClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		if (CandidateClass->IsChildOf(BaseClass))
		{
			Candidates.Add(CandidateClass);
		}
	}

	Candidates.Sort([](const UClass& Left, const UClass& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});

	return Candidates.Num() > 0 ? Candidates[0] : nullptr;
}

FAssetDocumentCapabilityResult ValidateFragmentClass(
	const TSharedRef<FJsonObject>& ObjectFragment,
	UClass* ExpectedBaseClass,
	const FString& Path)
{
	FString Kind;
	if (!ObjectFragment->TryGetStringField(TEXT("Kind"), Kind) || Kind.TrimStartAndEnd().IsEmpty())
	{
		return BodyFailure(TEXT("Fragment Kind must be a string."), Path, TEXT("missing-fragment-kind"));
	}

	if (Kind != TEXT("EmbeddedObject") && Kind != TEXT("ClassRef"))
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FString ClassName;
	if (!ObjectFragment->TryGetStringField(TEXT("Class"), ClassName) || ClassName.TrimStartAndEnd().IsEmpty())
	{
		return BodyFailure(TEXT("Object fragment field 'Class' is required."), Path, TEXT("missing-object-class"));
	}

	UClass* ResolvedClass = nullptr;
	FString Error;
	if (!ResolveClassAllowAbstract(ClassName, ResolvedClass, Error))
	{
		return BodyFailure(Error, Path, TEXT("object-class-resolve-failed"));
	}

	if (ExpectedBaseClass && !ResolvedClass->IsChildOf(ExpectedBaseClass))
	{
		return BodyFailure(
			FString::Printf(TEXT("Class '%s' is not a child of '%s'."), *ResolvedClass->GetName(), *ExpectedBaseClass->GetName()),
			Path,
			TEXT("embeddedobject-base-class-mismatch"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

bool TryCreateExpectedBaseDefaultObject(
	const TSharedRef<FJsonObject>& ObjectFragment,
	UClass* ExpectedBaseClass,
	UObject* Outer,
	UObject*& OutObject)
{
	OutObject = nullptr;

	FString ClassName;
	if (!ObjectFragment->TryGetStringField(TEXT("Class"), ClassName) || ClassName.TrimStartAndEnd().IsEmpty())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (ObjectFragment->TryGetObjectField(TEXT("Properties"), PropertiesPtr) && PropertiesPtr && PropertiesPtr->IsValid() && (*PropertiesPtr)->Values.Num() > 0)
	{
		return false;
	}

	UClass* ResolvedClass = nullptr;
	FString Error;
	if (!ResolveClassAllowAbstract(ClassName, ResolvedClass, Error))
	{
		return false;
	}

	if (!ExpectedBaseClass || ResolvedClass != ExpectedBaseClass || !ResolvedClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return false;
	}

	UClass* ConcreteClass = FindLoadedConcreteChildClass(ResolvedClass);
	if (!ConcreteClass)
	{
		return false;
	}

	OutObject = NewObject<UObject>(Outer, ConcreteClass, NAME_None, RF_Transactional);
	return OutObject != nullptr;
}

FAssetDocumentCapabilityResult ValidatePlacementArray(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject,
	const TCHAR* SectionName,
	bool bState,
	UClass* ExpectedBaseClass)
{
	const TSharedPtr<FJsonValue>* SectionValue = BodyObject->Values.Find(SectionName);
	if (!SectionValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>& Placements = (*SectionValue)->AsArray();
	for (int32 Index = 0; Index < Placements.Num(); ++Index)
	{
		const FString BasePath = PlacementPath(SectionName, Index);
		TSharedPtr<FJsonObject> PlacementObject;
		FAssetDocumentCapabilityResult Result = RequireObjectValue(Placements[Index], BasePath, PlacementObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		double Time = 0.0;
		Result = ReadRequiredNonNegativeNumber(PlacementObject.ToSharedRef(), TEXT("Time"), BasePath / TEXT("Time"), Time);
		if (!Result.bSuccess)
		{
			return Result;
		}

		int32 TrackIndex = 0;
		Result = ReadOptionalTrackIndex(PlacementObject.ToSharedRef(), BasePath / TEXT("TrackIndex"), TrackIndex);
		if (!Result.bSuccess)
		{
			return Result;
		}

		if (bState)
		{
			double Duration = 0.0;
			Result = ReadPositiveNumber(PlacementObject.ToSharedRef(), TEXT("Duration"), PlacementFieldPath(SectionName, Index, TEXT("Duration")), Duration);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}

		const TSharedPtr<FJsonValue>* ObjectValue = PlacementObject->Values.Find(TEXT("Object"));
		if (!ObjectValue)
		{
			return BodyFailure(TEXT("Notify placement requires Object fragment"), BasePath / TEXT("Object"), TEXT("MissingNotifyObject"));
		}

		TSharedPtr<FJsonObject> ObjectFragment;
		Result = RequireObjectValue(*ObjectValue, BasePath / TEXT("Object"), ObjectFragment);
		if (!Result.bSuccess)
		{
			return Result;
		}

		FAssetDocumentFragmentContext FragmentContext;
		FragmentContext.OwnerAsset = Context.Asset;
		FragmentContext.ExpectedBaseClass = ExpectedBaseClass;
		FragmentContext.Definitions = Context.Definitions;
		FragmentContext.JsonPath = BasePath / TEXT("Object");

		const FAssetDocumentFragmentResult FragmentResult = Compiler.Validate(ObjectFragment.ToSharedRef(), FragmentContext);
		if (!FragmentResult.bSuccess)
		{
			return FragmentFailure(FragmentResult);
		}

		Result = ValidateFragmentClass(ObjectFragment.ToSharedRef(), ExpectedBaseClass, BasePath / TEXT("Object"));
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
	const TSharedPtr<FJsonValue>* SectionValue = BodyObject->Values.Find(SectionName);
	if (!SectionValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>& Placements = (*SectionValue)->AsArray();
	for (int32 Index = 0; Index < Placements.Num(); ++Index)
	{
		const FString BasePath = PlacementPath(SectionName, Index);
		TSharedPtr<FJsonObject> PlacementObject;
		FAssetDocumentCapabilityResult Result = RequireObjectValue(Placements[Index], BasePath, PlacementObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		double Time = 0.0;
		Result = ReadRequiredNonNegativeNumber(PlacementObject.ToSharedRef(), TEXT("Time"), BasePath / TEXT("Time"), Time);
		if (!Result.bSuccess)
		{
			return Result;
		}

		int32 TrackIndex = 0;
		Result = ReadOptionalTrackIndex(PlacementObject.ToSharedRef(), BasePath / TEXT("TrackIndex"), TrackIndex);
		if (!Result.bSuccess)
		{
			return Result;
		}

		double Duration = 0.0;
		if (bState)
		{
			Result = ReadPositiveNumber(PlacementObject.ToSharedRef(), TEXT("Duration"), PlacementFieldPath(SectionName, Index, TEXT("Duration")), Duration);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}

		const TSharedPtr<FJsonValue>* ObjectValue = PlacementObject->Values.Find(TEXT("Object"));
		if (!ObjectValue)
		{
			return BodyFailure(TEXT("Notify placement requires Object fragment"), BasePath / TEXT("Object"), TEXT("MissingNotifyObject"));
		}

		TSharedPtr<FJsonObject> ObjectFragment;
		Result = RequireObjectValue(*ObjectValue, BasePath / TEXT("Object"), ObjectFragment);
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
			UObject* DefaultObject = nullptr;
			if (!TryCreateExpectedBaseDefaultObject(ObjectFragment.ToSharedRef(), ExpectedBaseClass, Montage, DefaultObject))
			{
				return FragmentFailure(FragmentResult);
			}

			FragmentResult = FAssetDocumentFragmentResult::Success();
			FragmentResult.Object = DefaultObject;
		}

		UObject* NotifyObject = FragmentResult.Object;
		if (!NotifyObject || !NotifyObject->IsA(ExpectedBaseClass))
		{
			return BodyFailure(TEXT("Object fragment did not resolve to the expected notify class"), BasePath / TEXT("Object"), TEXT("InvalidNotifyObject"));
		}

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
			NotifyEvent.NotifyName = NotifyObject->GetClass()->GetFName();
			NotifyEvent.SetDuration(static_cast<float>(Duration));
			NotifyEvent.RefreshEndTriggerOffset(Montage->CalculateOffsetForNotify(NotifyTime + static_cast<float>(Duration)));
		}
		else
		{
			NotifyEvent.Notify = Cast<UAnimNotify>(NotifyObject);
			NotifyEvent.NotifyName = NotifyObject->GetClass()->GetFName();
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
		return FragmentFailure(FragmentResult);
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
}

FAssetDocumentCapabilityResult FAnimMontageNotifyPlacementAdapter::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject) const
{
	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	FAssetDocumentCapabilityResult Result = ValidatePlacementArray(Compiler, Context, BodyObject, TEXT("Notifies"), false, UAnimNotify::StaticClass());
	if (!Result.bSuccess)
	{
		return Result;
	}

	return ValidatePlacementArray(Compiler, Context, BodyObject, TEXT("NotifyStates"), true, UAnimNotifyState::StaticClass());
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
		return BodyFailure(TEXT("AnimMontage notify extraction requires an asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	TArray<TSharedPtr<FJsonValue>> Notifies;
	TArray<TSharedPtr<FJsonValue>> NotifyStates;
	int32 NotifyIndex = 0;
	int32 NotifyStateIndex = 0;

	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (Event.Notify)
		{
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
	OutBodyJson->SetObjectField(TEXT("_Skipped"), Skipped);

	return FAssetDocumentCapabilityResult::Success();
}
