// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/AnimSequenceGenerator.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FrameRate.h"

FString FAnimSequenceGenerator::BuildLongPackageName(const FString& Path, const FString& Name) const
{
	FString FullPath = Path / Name;
	if (!FullPath.StartsWith(TEXT("/")))
	{
		FullPath = TEXT("/") + FullPath;
	}
	return FullPath;
}

USkeleton* FAnimSequenceGenerator::LoadSkeleton(const FString& SkeletonPath) const
{
	return SkeletonPath.IsEmpty() ? nullptr : LoadObject<USkeleton>(nullptr, *SkeletonPath);
}

USkeletalMesh* FAnimSequenceGenerator::LoadPreviewMesh(const FString& PreviewMeshPath) const
{
	return PreviewMeshPath.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *PreviewMeshPath);
}

bool FAnimSequenceGenerator::IsPreviewMeshCompatible(USkeletalMesh* PreviewMesh, USkeleton* Skeleton) const
{
	return !PreviewMesh || !Skeleton || PreviewMesh->GetSkeleton() == Skeleton;
}

FFrameRate FAnimSequenceGenerator::ParseFrameRate(TSharedPtr<FJsonObject> Config) const
{
	TSharedPtr<FJsonObject> FrameRateObject = GetObjectField(Config, TEXT("FrameRate"));
	if (!FrameRateObject.IsValid())
	{
		return FFrameRate(30, 1);
	}

	double Numerator = 30.0;
	double Denominator = 1.0;
	FrameRateObject->TryGetNumberField(TEXT("Numerator"), Numerator);
	FrameRateObject->TryGetNumberField(TEXT("Denominator"), Denominator);
	return FFrameRate(FMath::Max(1, static_cast<int32>(Numerator)), FMath::Max(1, static_cast<int32>(Denominator)));
}

int32 FAnimSequenceGenerator::ParseNumberOfFrames(TSharedPtr<FJsonObject> Config) const
{
	if (!Config.IsValid())
	{
		return 1;
	}

	double NumberOfFrames = 1.0;
	Config->TryGetNumberField(TEXT("NumberOfFrames"), NumberOfFrames);
	return FMath::Max(1, static_cast<int32>(NumberOfFrames));
}

FGenerationResult FAnimSequenceGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	if (!Config.IsValid())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Invalid configuration object"));
	}

	const bool bExists = DoesAssetExist(Path, Name);
	if (Action == EGenerationAction::Create && bExists)
	{
		return FGenerationResult::MakeSkipped(GetAssetType(), Name, Path, TEXT("Asset already exists"));
	}
	if (Action == EGenerationAction::Update && !bExists)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Asset does not exist for update"));
	}

	FString Error;
	UAnimSequence* AnimSequence = bExists ? Cast<UAnimSequence>(LoadExistingAsset(Path, Name)) : CreateAnimSequence(Name, Path, Config, Error);
	if (!AnimSequence)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error.IsEmpty() ? TEXT("Failed to create or load AnimSequence") : Error);
	}

	if (!ApplyPatch(AnimSequence, Config, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	if (!SaveAnimSequence(AnimSequence, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	return bExists
		? FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, AnimSequence)
		: FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, AnimSequence);
}

TOptional<FString> FAnimSequenceGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action) const
{
	(void)Action;

	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}
	return TOptional<FString>();
}

TArray<FString> FAnimSequenceGenerator::GetRequiredFields() const
{
	return { TEXT("Skeleton") };
}

bool FAnimSequenceGenerator::CanExtract(UObject* Asset) const
{
	return Asset && Asset->IsA<UAnimSequence>();
}

TSharedPtr<FJsonObject> FAnimSequenceGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	(void)bDiffOnly;

	const UAnimSequence* AnimSequence = Cast<UAnimSequence>(Asset);
	if (!AnimSequence)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();
	Config->SetStringField(TEXT("Skeleton"), AnimSequence->GetSkeleton() ? AnimSequence->GetSkeleton()->GetPathName() : TEXT(""));
	Config->SetNumberField(TEXT("NumberOfFrames"), AnimSequence->GetNumberOfSampledKeys());
	return Config;
}

UAnimSequence* FAnimSequenceGenerator::CreateAnimSequence(
	const FString& Name,
	const FString& Path,
	TSharedPtr<FJsonObject> Config,
	FString& OutError) const
{
	(void)Name;
	(void)Path;
	(void)Config;

	OutError = TEXT("CreateAnimSequence is implemented in Task 3");
	return nullptr;
}

bool FAnimSequenceGenerator::ApplyPatch(UAnimSequence* AnimSequence, TSharedPtr<FJsonObject> Config, FString& OutError) const
{
	(void)AnimSequence;
	(void)Config;

	OutError = TEXT("AnimSequence patching is implemented in Task 3");
	return false;
}

bool FAnimSequenceGenerator::SaveAnimSequence(UAnimSequence* AnimSequence, FString& OutError) const
{
	(void)AnimSequence;

	OutError = TEXT("SaveAnimSequence is implemented in Task 3");
	return false;
}
