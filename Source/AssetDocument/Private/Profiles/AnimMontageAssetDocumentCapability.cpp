// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimMontageAssetDocumentCapability.h"

#include "AssetDocumentFragmentCompiler.h"

#include "Animation/AnimMontage.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"

namespace
{
bool IsKnownBodyKey(const FString& BodyKey)
{
	for (const FName& KnownBodyKey : FAnimMontageAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (KnownBodyKey.ToString() == BodyKey)
		{
			return true;
		}
	}
	return false;
}

FString GetLegacyBodyKeyGuidance(const FString& BodyKey)
{
	if (BodyKey == TEXT("Slots"))
	{
		return TEXT("Use SlotAnimTracks");
	}
	if (BodyKey == TEXT("Segments"))
	{
		return TEXT("Use AnimSegments");
	}
	if (BodyKey == TEXT("Animation"))
	{
		return TEXT("Use AnimReference");
	}
	if (BodyKey == TEXT("Sections"))
	{
		return TEXT("Use CompositeSections");
	}
	return FString();
}

FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

bool IsNullOrObject(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && (Value->Type == EJson::Null || Value->Type == EJson::Object);
}

bool IsArray(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Array;
}

bool IsObject(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Object;
}

FAssetDocumentCapabilityResult FragmentFailure(const FAssetDocumentFragmentResult& FragmentResult)
{
	FAssetDocumentCapabilityResult Result = FAssetDocumentCapabilityResult::Failure(FragmentResult.Message);
	Result.Diagnostics = FragmentResult.Diagnostics;
	return Result;
}

FAssetDocumentCapabilityResult RequireObjectValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Expected a JSON object"), Path, TEXT("InvalidBodySectionType"));
	}

	OutObject = Value->AsObject();
	if (!OutObject.IsValid())
	{
		return BodyFailure(TEXT("Expected a JSON object"), Path, TEXT("InvalidBodySectionType"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RequireArrayValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, const TArray<TSharedPtr<FJsonValue>>*& OutArray)
{
	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return BodyFailure(TEXT("Expected a JSON array"), Path, TEXT("InvalidBodySectionType"));
	}

	OutArray = &Value->AsArray();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult CompileObjectFragment(
	const FAssetDocumentFragmentCompiler& Compiler,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& Fragment,
	UClass* ExpectedBaseClass,
	const FString& JsonPath,
	FAssetDocumentFragmentResult& OutFragmentResult)
{
	FAssetDocumentFragmentContext FragmentContext;
	FragmentContext.OwnerAsset = Montage;
	FragmentContext.Outer = Montage;
	FragmentContext.ExpectedBaseClass = ExpectedBaseClass;
	FragmentContext.JsonPath = JsonPath;

	OutFragmentResult = Compiler.Compile(Fragment, FragmentContext);
	return OutFragmentResult.bSuccess ? FAssetDocumentCapabilityResult::Success() : FragmentFailure(OutFragmentResult);
}

FAssetDocumentCapabilityResult ApplyObjectReference(
	const FAssetDocumentFragmentCompiler& Compiler,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& BodyObject,
	const TCHAR* FieldName,
	UClass* ExpectedBaseClass,
	TFunctionRef<void(UObject*)> ApplyObject)
{
	const TSharedPtr<FJsonValue>* Value = BodyObject->Values.Find(FieldName);
	if (!Value)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (!Value->IsValid() || (*Value)->Type == EJson::Null)
	{
		ApplyObject(nullptr);
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> FragmentObject;
	const FString Path = FString::Printf(TEXT("/Body/%s"), FieldName);
	const FAssetDocumentCapabilityResult ObjectResult = RequireObjectValue(*Value, Path, FragmentObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	FAssetDocumentFragmentResult FragmentResult;
	const FAssetDocumentCapabilityResult CompileResult = CompileObjectFragment(Compiler, Montage, FragmentObject.ToSharedRef(), ExpectedBaseClass, Path, FragmentResult);
	if (!CompileResult.bSuccess)
	{
		return CompileResult;
	}

	ApplyObject(FragmentResult.Object);
	return FAssetDocumentCapabilityResult::Success();
}

bool TryGetNumber(const TSharedRef<FJsonObject>& Object, const TCHAR* FieldName, double& OutValue)
{
	return Object->TryGetNumberField(FieldName, OutValue);
}

FAssetDocumentCapabilityResult ApplySlotAnimTracks(
	const FAssetDocumentFragmentCompiler& Compiler,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& BodyObject)
{
	const TSharedPtr<FJsonValue>* SlotAnimTracksValue = BodyObject->Values.Find(TEXT("SlotAnimTracks"));
	if (!SlotAnimTracksValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>* SlotAnimTracksArray = nullptr;
	FAssetDocumentCapabilityResult ArrayResult = RequireArrayValue(*SlotAnimTracksValue, TEXT("/Body/SlotAnimTracks"), SlotAnimTracksArray);
	if (!ArrayResult.bSuccess)
	{
		return ArrayResult;
	}

	TArray<FSlotAnimationTrack> NewSlotAnimTracks;
	float CompositeLength = 0.0f;

	for (int32 SlotIndex = 0; SlotIndex < SlotAnimTracksArray->Num(); ++SlotIndex)
	{
		const FString SlotPath = FString::Printf(TEXT("/Body/SlotAnimTracks/%d"), SlotIndex);
		TSharedPtr<FJsonObject> SlotObject;
		const FAssetDocumentCapabilityResult SlotObjectResult = RequireObjectValue((*SlotAnimTracksArray)[SlotIndex], SlotPath, SlotObject);
		if (!SlotObjectResult.bSuccess)
		{
			return SlotObjectResult;
		}

		FString SlotNameString;
		if (!SlotObject->TryGetStringField(TEXT("SlotName"), SlotNameString) || SlotNameString.TrimStartAndEnd().IsEmpty())
		{
			return BodyFailure(TEXT("SlotAnimTrack requires SlotName"), SlotPath / TEXT("SlotName"), TEXT("MissingSlotName"));
		}

		const TSharedPtr<FJsonObject>* AnimTrackObject = nullptr;
		if (!SlotObject->TryGetObjectField(TEXT("AnimTrack"), AnimTrackObject) || !AnimTrackObject || !AnimTrackObject->IsValid())
		{
			return BodyFailure(TEXT("SlotAnimTrack requires AnimTrack object"), SlotPath / TEXT("AnimTrack"), TEXT("MissingAnimTrack"));
		}

		const TArray<TSharedPtr<FJsonValue>>* AnimSegmentsArray = nullptr;
		if (!(*AnimTrackObject)->TryGetArrayField(TEXT("AnimSegments"), AnimSegmentsArray) || !AnimSegmentsArray)
		{
			return BodyFailure(TEXT("AnimTrack requires AnimSegments array"), SlotPath / TEXT("AnimTrack/AnimSegments"), TEXT("MissingAnimSegments"));
		}

		FSlotAnimationTrack SlotAnimTrack;
		SlotAnimTrack.SlotName = FName(*SlotNameString);

		for (int32 SegmentIndex = 0; SegmentIndex < AnimSegmentsArray->Num(); ++SegmentIndex)
		{
			const FString SegmentPath = FString::Printf(TEXT("%s/AnimTrack/AnimSegments/%d"), *SlotPath, SegmentIndex);
			TSharedPtr<FJsonObject> SegmentObject;
			const FAssetDocumentCapabilityResult SegmentObjectResult = RequireObjectValue((*AnimSegmentsArray)[SegmentIndex], SegmentPath, SegmentObject);
			if (!SegmentObjectResult.bSuccess)
			{
				return SegmentObjectResult;
			}

			const TSharedPtr<FJsonObject>* AnimReferenceObject = nullptr;
			if (!SegmentObject->TryGetObjectField(TEXT("AnimReference"), AnimReferenceObject) || !AnimReferenceObject || !AnimReferenceObject->IsValid())
			{
				return BodyFailure(TEXT("AnimSegment requires AnimReference fragment"), SegmentPath / TEXT("AnimReference"), TEXT("MissingAnimReference"));
			}

			FAssetDocumentFragmentResult AnimReferenceResult;
			FAssetDocumentCapabilityResult CompileResult = CompileObjectFragment(
				Compiler,
				Montage,
				AnimReferenceObject->ToSharedRef(),
				UAnimSequenceBase::StaticClass(),
				SegmentPath / TEXT("AnimReference"),
				AnimReferenceResult);
			if (!CompileResult.bSuccess)
			{
				return CompileResult;
			}

			UAnimSequenceBase* AnimReference = Cast<UAnimSequenceBase>(AnimReferenceResult.Object);
			if (!AnimReference)
			{
				return BodyFailure(TEXT("AnimReference did not resolve to UAnimSequenceBase"), SegmentPath / TEXT("AnimReference"), TEXT("InvalidAnimReference"));
			}

			FAnimSegment Segment;
			Segment.SetAnimReference(AnimReference);

			double NumberValue = 0.0;
			if (TryGetNumber(SegmentObject.ToSharedRef(), TEXT("StartPos"), NumberValue))
			{
				Segment.StartPos = static_cast<float>(NumberValue);
			}
			if (TryGetNumber(SegmentObject.ToSharedRef(), TEXT("AnimStartTime"), NumberValue))
			{
				Segment.AnimStartTime = static_cast<float>(NumberValue);
			}
			if (TryGetNumber(SegmentObject.ToSharedRef(), TEXT("AnimEndTime"), NumberValue))
			{
				Segment.AnimEndTime = static_cast<float>(NumberValue);
			}
			if (TryGetNumber(SegmentObject.ToSharedRef(), TEXT("AnimPlayRate"), NumberValue))
			{
				Segment.AnimPlayRate = static_cast<float>(NumberValue);
			}
			if (TryGetNumber(SegmentObject.ToSharedRef(), TEXT("LoopingCount"), NumberValue))
			{
				Segment.LoopingCount = static_cast<int32>(NumberValue);
			}

			if (FMath::IsNearlyZero(Segment.AnimPlayRate))
			{
				return BodyFailure(TEXT("AnimPlayRate must not be zero"), SegmentPath / TEXT("AnimPlayRate"), TEXT("InvalidAnimPlayRate"));
			}
			if (Segment.LoopingCount <= 0)
			{
				return BodyFailure(TEXT("LoopingCount must be greater than zero"), SegmentPath / TEXT("LoopingCount"), TEXT("InvalidLoopingCount"));
			}
			if (Segment.AnimEndTime < Segment.AnimStartTime)
			{
				return BodyFailure(TEXT("AnimEndTime must be greater than or equal to AnimStartTime"), SegmentPath / TEXT("AnimEndTime"), TEXT("InvalidAnimEndTime"));
			}

			CompositeLength = FMath::Max(CompositeLength, Segment.GetEndPos());
			SlotAnimTrack.AnimTrack.AnimSegments.Add(Segment);
		}

		NewSlotAnimTracks.Add(SlotAnimTrack);
	}

	Montage->SlotAnimTracks = MoveTemp(NewSlotAnimTracks);
	Montage->SetCompositeLength(CompositeLength);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyCompositeSections(UAnimMontage* Montage, const TSharedRef<FJsonObject>& BodyObject)
{
	const TSharedPtr<FJsonValue>* CompositeSectionsValue = BodyObject->Values.Find(TEXT("CompositeSections"));
	if (!CompositeSectionsValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>* CompositeSectionsArray = nullptr;
	FAssetDocumentCapabilityResult ArrayResult = RequireArrayValue(*CompositeSectionsValue, TEXT("/Body/CompositeSections"), CompositeSectionsArray);
	if (!ArrayResult.bSuccess)
	{
		return ArrayResult;
	}

	TArray<FCompositeSection> NewCompositeSections;
	TSet<FName> SectionNames;

	for (int32 SectionIndex = 0; SectionIndex < CompositeSectionsArray->Num(); ++SectionIndex)
	{
		const FString SectionPath = FString::Printf(TEXT("/Body/CompositeSections/%d"), SectionIndex);
		TSharedPtr<FJsonObject> SectionObject;
		const FAssetDocumentCapabilityResult SectionObjectResult = RequireObjectValue((*CompositeSectionsArray)[SectionIndex], SectionPath, SectionObject);
		if (!SectionObjectResult.bSuccess)
		{
			return SectionObjectResult;
		}

		FString SectionNameString;
		if (!SectionObject->TryGetStringField(TEXT("SectionName"), SectionNameString) || SectionNameString.TrimStartAndEnd().IsEmpty())
		{
			return BodyFailure(TEXT("CompositeSection requires SectionName"), SectionPath / TEXT("SectionName"), TEXT("MissingSectionName"));
		}

		const FName SectionName(*SectionNameString);
		if (SectionNames.Contains(SectionName))
		{
			return BodyFailure(FString::Printf(TEXT("Duplicate CompositeSection '%s'"), *SectionNameString), SectionPath / TEXT("SectionName"), TEXT("DuplicateSectionName"));
		}
		SectionNames.Add(SectionName);

		FCompositeSection Section;
		Section.SectionName = SectionName;
		double LinkableTime = 0.0;
		SectionObject->TryGetNumberField(TEXT("LinkableTime"), LinkableTime);
		Section.Link(Montage, static_cast<float>(LinkableTime), 0);
		Section.SetTime(static_cast<float>(LinkableTime));

		FString NextSectionNameString;
		if (SectionObject->TryGetStringField(TEXT("NextSectionName"), NextSectionNameString) && !NextSectionNameString.TrimStartAndEnd().IsEmpty())
		{
			Section.NextSectionName = FName(*NextSectionNameString);
		}

		NewCompositeSections.Add(Section);
	}

	for (const FCompositeSection& Section : NewCompositeSections)
	{
		if (!Section.NextSectionName.IsNone() && !SectionNames.Contains(Section.NextSectionName))
		{
			return BodyFailure(
				FString::Printf(TEXT("NextSectionName '%s' does not reference an existing CompositeSection"), *Section.NextSectionName.ToString()),
				TEXT("/Body/CompositeSections"),
				TEXT("InvalidNextSectionName"));
		}
	}

	Montage->CompositeSections = MoveTemp(NewCompositeSections);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyBlend(UAnimMontage* Montage, const TSharedRef<FJsonObject>& BodyObject)
{
	const TSharedPtr<FJsonValue>* BlendValue = BodyObject->Values.Find(TEXT("Blend"));
	if (!BlendValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> BlendObject;
	const FAssetDocumentCapabilityResult BlendObjectResult = RequireObjectValue(*BlendValue, TEXT("/Body/Blend"), BlendObject);
	if (!BlendObjectResult.bSuccess)
	{
		return BlendObjectResult;
	}

	double BlendTime = 0.0;
	if (BlendObject->TryGetNumberField(TEXT("BlendInTime"), BlendTime))
	{
		Montage->BlendIn.SetBlendTime(static_cast<float>(BlendTime));
	}
	if (BlendObject->TryGetNumberField(TEXT("BlendOutTime"), BlendTime))
	{
		Montage->BlendOut.SetBlendTime(static_cast<float>(BlendTime));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractAssetRef(
	const FAssetDocumentFragmentCompiler& Compiler,
	UObject* OwnerAsset,
	UObject* ValueObject,
	const FString& JsonPath,
	TSharedRef<FJsonObject>& OutFragment)
{
	FAssetDocumentFragmentExtractContext ExtractContext;
	ExtractContext.OwnerAsset = OwnerAsset;
	ExtractContext.ValueObject = ValueObject;
	ExtractContext.Kind = TEXT("AssetRef");
	ExtractContext.JsonPath = JsonPath;

	const FAssetDocumentFragmentResult FragmentResult = Compiler.Extract(ExtractContext, OutFragment);
	return FragmentResult.bSuccess ? FAssetDocumentCapabilityResult::Success() : FragmentFailure(FragmentResult);
}
}

const TArray<FName>& FAnimMontageAssetDocumentCapability::GetCanonicalBodyKeys()
{
	static const TArray<FName> Keys = {
		TEXT("Skeleton"),
		TEXT("PreviewMesh"),
		TEXT("SlotAnimTracks"),
		TEXT("CompositeSections"),
		TEXT("Notifies"),
		TEXT("NotifyStates"),
		TEXT("Blend"),
	};
	return Keys;
}

FName FAnimMontageAssetDocumentCapability::GetName() const
{
	return TEXT("AnimMontageBody");
}

int32 FAnimMontageAssetDocumentCapability::GetApplyOrder() const
{
	return 0;
}

bool FAnimMontageAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && Asset->IsA<UAnimMontage>();
}

bool FAnimMontageAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UAnimMontage::StaticClass();
}

TSharedRef<FJsonObject> FAnimMontageAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Skeleton"), TEXT("null | AssetRef<USkeleton>"));
	Schema->SetStringField(TEXT("PreviewMesh"), TEXT("null | AssetRef<USkeletalMesh>"));
	Schema->SetStringField(TEXT("SlotAnimTracks"), TEXT("array<SlotAnimTrack>"));
	Schema->SetStringField(TEXT("CompositeSections"), TEXT("array<CompositeSection>"));
	Schema->SetStringField(TEXT("Notifies"), TEXT("array<AnimNotifyPlacement>"));
	Schema->SetStringField(TEXT("NotifyStates"), TEXT("array<AnimNotifyStatePlacement>"));
	Schema->SetStringField(TEXT("Blend"), TEXT("object"));
	return Schema;
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	if (!SupportsClass(Context.AssetClass))
	{
		return BodyFailure(TEXT("AnimMontage body validation requires UAnimMontage class"), TEXT("/Class"), TEXT("UnsupportedClass"));
	}

	if (BodyJson->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	const TSharedPtr<FJsonObject> BodyObject = BodyJson->AsObject();
	if (!BodyObject.IsValid())
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	return ValidateBodyObject(BodyObject.ToSharedRef());
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	UAnimMontage* Montage = Cast<UAnimMontage>(Context.Asset);
	if (!Montage)
	{
		return BodyFailure(TEXT("AnimMontage body apply requires UAnimMontage asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	if (BodyJson->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	const TSharedPtr<FJsonObject> BodyObject = BodyJson->AsObject();
	if (!BodyObject.IsValid())
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	FAssetDocumentCapabilityResult Result = ApplyObjectReference(
		Compiler,
		Montage,
		BodyObject.ToSharedRef(),
		TEXT("Skeleton"),
		USkeleton::StaticClass(),
		[Montage](UObject* Object)
		{
			Montage->SetSkeleton(Cast<USkeleton>(Object));
		});
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ApplyObjectReference(
		Compiler,
		Montage,
		BodyObject.ToSharedRef(),
		TEXT("PreviewMesh"),
		USkeletalMesh::StaticClass(),
		[Montage](UObject* Object)
		{
			Montage->SetPreviewMesh(Cast<USkeletalMesh>(Object), false);
		});
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FAssetDocumentCapabilityResult StepResult : {
		ApplySlotAnimTracks(Compiler, Montage, BodyObject.ToSharedRef()),
		ApplyCompositeSections(Montage, BodyObject.ToSharedRef()),
		ApplyBlend(Montage, BodyObject.ToSharedRef()),
	})
	{
		if (!StepResult.bSuccess)
		{
			return StepResult;
		}
	}

	Montage->MarkPackageDirty();
	return FAssetDocumentCapabilityResult::Success(TEXT("AnimMontage Body applied"));
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	const UAnimMontage* Montage = Cast<UAnimMontage>(Context.Asset);
	if (!Montage)
	{
		return BodyFailure(TEXT("AnimMontage body extract requires UAnimMontage asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	if (USkeleton* Skeleton = Montage->GetSkeleton())
	{
		TSharedRef<FJsonObject> SkeletonRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimMontage*>(Montage), Skeleton, TEXT("/Body/Skeleton"), SkeletonRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutBodyJson->SetObjectField(TEXT("Skeleton"), SkeletonRef);
	}

	if (USkeletalMesh* PreviewMesh = Montage->GetPreviewMesh())
	{
		TSharedRef<FJsonObject> PreviewMeshRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimMontage*>(Montage), PreviewMesh, TEXT("/Body/PreviewMesh"), PreviewMeshRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutBodyJson->SetObjectField(TEXT("PreviewMesh"), PreviewMeshRef);
	}

	TArray<TSharedPtr<FJsonValue>> SlotAnimTracks;
	for (const FSlotAnimationTrack& SlotAnimTrack : Montage->SlotAnimTracks)
	{
		TSharedRef<FJsonObject> SlotObject = MakeShared<FJsonObject>();
		SlotObject->SetStringField(TEXT("SlotName"), SlotAnimTrack.SlotName.ToString());

		TSharedRef<FJsonObject> AnimTrackObject = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> AnimSegments;
		for (const FAnimSegment& Segment : SlotAnimTrack.AnimTrack.AnimSegments)
		{
			TSharedRef<FJsonObject> SegmentObject = MakeShared<FJsonObject>();
			if (UAnimSequenceBase* AnimReference = Segment.GetAnimReference())
			{
				TSharedRef<FJsonObject> AnimReferenceRef = MakeShared<FJsonObject>();
				FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimMontage*>(Montage), AnimReference, TEXT("/Body/SlotAnimTracks/AnimTrack/AnimSegments/AnimReference"), AnimReferenceRef);
				if (!Result.bSuccess)
				{
					return Result;
				}
				SegmentObject->SetObjectField(TEXT("AnimReference"), AnimReferenceRef);
			}
			SegmentObject->SetNumberField(TEXT("StartPos"), Segment.StartPos);
			SegmentObject->SetNumberField(TEXT("AnimStartTime"), Segment.AnimStartTime);
			SegmentObject->SetNumberField(TEXT("AnimEndTime"), Segment.AnimEndTime);
			SegmentObject->SetNumberField(TEXT("AnimPlayRate"), Segment.AnimPlayRate);
			SegmentObject->SetNumberField(TEXT("LoopingCount"), Segment.LoopingCount);
			AnimSegments.Add(MakeShared<FJsonValueObject>(SegmentObject));
		}
		AnimTrackObject->SetArrayField(TEXT("AnimSegments"), AnimSegments);
		SlotObject->SetObjectField(TEXT("AnimTrack"), AnimTrackObject);
		SlotAnimTracks.Add(MakeShared<FJsonValueObject>(SlotObject));
	}
	OutBodyJson->SetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks);

	TArray<TSharedPtr<FJsonValue>> CompositeSections;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		TSharedRef<FJsonObject> SectionObject = MakeShared<FJsonObject>();
		SectionObject->SetStringField(TEXT("SectionName"), Section.SectionName.ToString());
		SectionObject->SetNumberField(TEXT("LinkableTime"), Section.GetTime());
		if (!Section.NextSectionName.IsNone())
		{
			SectionObject->SetStringField(TEXT("NextSectionName"), Section.NextSectionName.ToString());
		}
		CompositeSections.Add(MakeShared<FJsonValueObject>(SectionObject));
	}
	OutBodyJson->SetArrayField(TEXT("CompositeSections"), CompositeSections);

	TSharedRef<FJsonObject> Blend = MakeShared<FJsonObject>();
	Blend->SetNumberField(TEXT("BlendInTime"), Montage->GetDefaultBlendInTime());
	Blend->SetNumberField(TEXT("BlendOutTime"), Montage->GetDefaultBlendOutTime());
	OutBodyJson->SetObjectField(TEXT("Blend"), Blend);

	return FAssetDocumentCapabilityResult::Success(TEXT("AnimMontage Body extracted"));
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	return FAssetDocumentCapabilityResult::Success(TEXT("AnimMontage body diff is deferred"));
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::ValidateBodyObject(const TSharedRef<FJsonObject>& BodyObject) const
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		const FString Path = FString::Printf(TEXT("/Body/%s"), *Pair.Key);
		const FString Guidance = GetLegacyBodyKeyGuidance(Pair.Key);
		if (!Guidance.IsEmpty())
		{
			return BodyFailure(
				FString::Printf(TEXT("Body key '%s' is not supported. %s"), *Pair.Key, *Guidance),
				Path,
				TEXT("DeprecatedBodyKey"));
		}

		if (!IsKnownBodyKey(Pair.Key))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unknown AnimMontage Body key '%s'"), *Pair.Key),
				Path,
				TEXT("UnknownBodyKey"));
		}
	}

	auto RequireNullOrObject = [&BodyObject](const TCHAR* FieldName) -> FAssetDocumentCapabilityResult
	{
		if (const TSharedPtr<FJsonValue>* Value = BodyObject->Values.Find(FieldName))
		{
			if (!IsNullOrObject(*Value))
			{
				const FString Path = FString::Printf(TEXT("/Body/%s"), FieldName);
				return BodyFailure(FString::Printf(TEXT("Body.%s must be null or an object fragment"), FieldName), Path, TEXT("InvalidBodySectionType"));
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	};

	auto RequireArray = [&BodyObject](const TCHAR* FieldName) -> FAssetDocumentCapabilityResult
	{
		if (const TSharedPtr<FJsonValue>* Value = BodyObject->Values.Find(FieldName))
		{
			if (!IsArray(*Value))
			{
				const FString Path = FString::Printf(TEXT("/Body/%s"), FieldName);
				return BodyFailure(FString::Printf(TEXT("Body.%s must be an array"), FieldName), Path, TEXT("InvalidBodySectionType"));
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	};

	auto RequireObject = [&BodyObject](const TCHAR* FieldName) -> FAssetDocumentCapabilityResult
	{
		if (const TSharedPtr<FJsonValue>* Value = BodyObject->Values.Find(FieldName))
		{
			if (!IsObject(*Value))
			{
				const FString Path = FString::Printf(TEXT("/Body/%s"), FieldName);
				return BodyFailure(FString::Printf(TEXT("Body.%s must be an object"), FieldName), Path, TEXT("InvalidBodySectionType"));
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	};

	for (const FAssetDocumentCapabilityResult Result : {
		RequireNullOrObject(TEXT("Skeleton")),
		RequireNullOrObject(TEXT("PreviewMesh")),
		RequireArray(TEXT("SlotAnimTracks")),
		RequireArray(TEXT("CompositeSections")),
		RequireArray(TEXT("Notifies")),
		RequireArray(TEXT("NotifyStates")),
		RequireObject(TEXT("Blend")),
	})
	{
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("AnimMontage Body is valid"));
}
