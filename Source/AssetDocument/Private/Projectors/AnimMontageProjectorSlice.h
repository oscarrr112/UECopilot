#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UAnimMontage;

struct FAnimMontageProjectorSliceResult
{
	bool bSuccess = false;
	FString Message;
	TArray<FString> ChangedPaths;

	static FAnimMontageProjectorSliceResult Success();
	static FAnimMontageProjectorSliceResult Failure(const FString& InMessage);
};

class FAnimMontageProjectorSlice
{
public:
	FAnimMontageProjectorSliceResult ExtractBody(const UAnimMontage& Montage, TSharedRef<FJsonObject> ProjectedBody) const;
	FAnimMontageProjectorSliceResult ValidateBody(const UAnimMontage& Montage, const TSharedRef<FJsonObject>& ProjectedBody) const;
	FAnimMontageProjectorSliceResult ApplyBody(UAnimMontage& Montage, const TSharedRef<FJsonObject>& ProjectedBody) const;
	FAnimMontageProjectorSliceResult DiffBody(const UAnimMontage& Montage, const TSharedRef<FJsonObject>& ProjectedBody, TArray<FString>& OutChangedPaths) const;
};
