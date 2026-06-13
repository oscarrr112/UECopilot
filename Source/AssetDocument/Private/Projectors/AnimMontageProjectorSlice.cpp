#include "Projectors/AnimMontageProjectorSlice.h"

#include "Dom/JsonObject.h"

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
	(void)Montage;
	(void)ProjectedBody;
	return FAnimMontageProjectorSliceResult::Failure(TEXT("AnimMontage projector extract is not implemented."));
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
