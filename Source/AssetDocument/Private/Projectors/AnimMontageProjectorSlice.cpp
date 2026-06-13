#include "Projectors/AnimMontageProjectorSlice.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Projectors/AssetDocumentProjectionTypes.h"
#include "Projectors/AssetDocumentRefProjector.h"

namespace
{
void SetObjectOrNullField(const TSharedRef<FJsonObject>& Object, const FString& FieldName, const TSharedPtr<FJsonObject>& FieldObject)
{
	if (FieldObject.IsValid())
	{
		Object->SetObjectField(FieldName, FieldObject);
		return;
	}

	Object->SetField(FieldName, MakeShared<FJsonValueNull>());
}

FAnimMontageProjectorSliceResult ToSliceFailure(const FAssetDocumentProjectionResult& ProjectionResult)
{
	return FAnimMontageProjectorSliceResult::Failure(ProjectionResult.Message);
}
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSliceResult::Success()
{
	FAnimMontageProjectorSliceResult Result;
	Result.bSuccess = true;
	return Result;
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSliceResult::Failure(const FString& InMessage)
{
	FAnimMontageProjectorSliceResult Result;
	Result.bSuccess = false;
	Result.Message = InMessage;
	return Result;
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::ExtractBody(const UAnimMontage& Montage, TSharedRef<FJsonObject> ProjectedBody) const
{
	FAssetDocumentProjectionMetrics Metrics;
	const FAssetDocumentRefProjector RefProjector;

	TSharedPtr<FJsonObject> SkeletonRef;
	FAssetDocumentProjectionResult ProjectionResult = RefProjector.ProjectAssetRef(
		Montage.GetSkeleton(),
		USkeleton::StaticClass(),
		TEXT("/Body/Skeleton"),
		SkeletonRef);
	if (!ProjectionResult.bSuccess)
	{
		return ToSliceFailure(ProjectionResult);
	}
	SetObjectOrNullField(ProjectedBody, TEXT("Skeleton"), SkeletonRef);
	++Metrics.ReusableProjectedFields;

	TSharedPtr<FJsonObject> PreviewMeshRef;
	ProjectionResult = RefProjector.ProjectAssetRef(
		Montage.GetPreviewMesh(),
		USkeletalMesh::StaticClass(),
		TEXT("/Body/PreviewMesh"),
		PreviewMeshRef);
	if (!ProjectionResult.bSuccess)
	{
		return ToSliceFailure(ProjectionResult);
	}
	SetObjectOrNullField(ProjectedBody, TEXT("PreviewMesh"), PreviewMeshRef);
	++Metrics.ReusableProjectedFields;

	TArray<TSharedPtr<FJsonValue>> SlotAnimTracks;
	for (int32 SlotIndex = 0; SlotIndex < Montage.SlotAnimTracks.Num(); ++SlotIndex)
	{
		const FSlotAnimationTrack& SlotAnimTrack = Montage.SlotAnimTracks[SlotIndex];
		TSharedRef<FJsonObject> SlotObject = MakeShared<FJsonObject>();
		SlotObject->SetStringField(TEXT("SlotName"), SlotAnimTrack.SlotName.ToString());
		++Metrics.AssetSpecificFields;

		TSharedRef<FJsonObject> AnimTrackObject = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> AnimSegments;
		for (int32 SegmentIndex = 0; SegmentIndex < SlotAnimTrack.AnimTrack.AnimSegments.Num(); ++SegmentIndex)
		{
			const FAnimSegment& Segment = SlotAnimTrack.AnimTrack.AnimSegments[SegmentIndex];
			TSharedRef<FJsonObject> SegmentObject = MakeShared<FJsonObject>();

			TSharedPtr<FJsonObject> AnimReferenceRef;
			ProjectionResult = RefProjector.ProjectAssetRef(
				Segment.GetAnimReference(),
				UAnimSequenceBase::StaticClass(),
				FString::Printf(TEXT("/Body/SlotAnimTracks/%d/AnimTrack/AnimSegments/%d/AnimReference"), SlotIndex, SegmentIndex),
				AnimReferenceRef);
			if (!ProjectionResult.bSuccess)
			{
				return ToSliceFailure(ProjectionResult);
			}
			SetObjectOrNullField(SegmentObject, TEXT("AnimReference"), AnimReferenceRef);
			++Metrics.ReusableProjectedFields;

			SegmentObject->SetNumberField(TEXT("StartPos"), Segment.StartPos);
			SegmentObject->SetNumberField(TEXT("AnimStartTime"), Segment.AnimStartTime);
			SegmentObject->SetNumberField(TEXT("AnimEndTime"), Segment.AnimEndTime);
			SegmentObject->SetNumberField(TEXT("AnimPlayRate"), Segment.AnimPlayRate);
			SegmentObject->SetNumberField(TEXT("LoopingCount"), Segment.LoopingCount);
			Metrics.AssetSpecificFields += 5;

			AnimSegments.Add(MakeShared<FJsonValueObject>(SegmentObject));
		}
		AnimTrackObject->SetArrayField(TEXT("AnimSegments"), AnimSegments);
		SlotObject->SetObjectField(TEXT("AnimTrack"), AnimTrackObject);
		Metrics.AssetSpecificFields += 2;

		SlotAnimTracks.Add(MakeShared<FJsonValueObject>(SlotObject));
	}
	ProjectedBody->SetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks);

	TArray<TSharedPtr<FJsonValue>> CompositeSections;
	for (const FCompositeSection& Section : Montage.CompositeSections)
	{
		TSharedRef<FJsonObject> SectionObject = MakeShared<FJsonObject>();
		SectionObject->SetStringField(TEXT("SectionName"), Section.SectionName.ToString());
		SectionObject->SetNumberField(TEXT("LinkableTime"), Section.GetTime());
		Metrics.AssetSpecificFields += 2;
		if (!Section.NextSectionName.IsNone())
		{
			SectionObject->SetStringField(TEXT("NextSectionName"), Section.NextSectionName.ToString());
			++Metrics.AssetSpecificFields;
		}
		CompositeSections.Add(MakeShared<FJsonValueObject>(SectionObject));
	}
	ProjectedBody->SetArrayField(TEXT("CompositeSections"), CompositeSections);

	ProjectedBody->SetArrayField(TEXT("Notifies"), TArray<TSharedPtr<FJsonValue>>());
	ProjectedBody->SetArrayField(TEXT("NotifyStates"), TArray<TSharedPtr<FJsonValue>>());
	Metrics.SkippedFields += 2;

	TSharedRef<FJsonObject> Blend = MakeShared<FJsonObject>();
	Blend->SetNumberField(TEXT("BlendInTime"), Montage.GetDefaultBlendInTime());
	Blend->SetNumberField(TEXT("BlendOutTime"), Montage.GetDefaultBlendOutTime());
	ProjectedBody->SetObjectField(TEXT("Blend"), Blend);
	Metrics.AssetSpecificFields += 2;

	ProjectedBody->SetObjectField(TEXT("_ProjectionMetrics"), Metrics.ToJson());

	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::ValidateBody(const UAnimMontage& Montage, const TSharedRef<FJsonObject>& ProjectedBody) const
{
	(void)Montage;
	(void)ProjectedBody;
	return FAnimMontageProjectorSliceResult::Failure(TEXT("AnimMontage projector validation is not implemented."));
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::ApplyBody(UAnimMontage& Montage, const TSharedRef<FJsonObject>& ProjectedBody) const
{
	(void)Montage;
	(void)ProjectedBody;
	return FAnimMontageProjectorSliceResult::Failure(TEXT("AnimMontage projector apply is not implemented."));
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::DiffBody(const UAnimMontage& Montage, const TSharedRef<FJsonObject>& ProjectedBody, TArray<FString>& OutChangedPaths) const
{
	(void)Montage;
	(void)ProjectedBody;
	(void)OutChangedPaths;
	return FAnimMontageProjectorSliceResult::Failure(TEXT("AnimMontage projector diff is not implemented."));
}
