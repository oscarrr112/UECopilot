// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenerationTypes.generated.h"

/**
 * Status of asset generation
 */
UENUM(BlueprintType)
enum class EGenerationStatus : uint8
{
	Success    UMETA(DisplayName = "Success"),
	Updated    UMETA(DisplayName = "Updated"),
	Skipped    UMETA(DisplayName = "Skipped"),
	Failed     UMETA(DisplayName = "Failed")
};

/**
 * Action to perform on asset
 */
UENUM(BlueprintType)
enum class EGenerationAction : uint8
{
	Create          UMETA(DisplayName = "Create"),
	Update          UMETA(DisplayName = "Update"),
	CreateOrUpdate  UMETA(DisplayName = "CreateOrUpdate")
};

/**
 * Result of a single asset generation
 */
USTRUCT(BlueprintType)
struct ASSETFACTORY_API FGenerationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	FString AssetType;

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	FString AssetName;

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	EGenerationStatus Status = EGenerationStatus::Failed;

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	TObjectPtr<UObject> GeneratedAsset = nullptr;

	// Helper methods
	bool IsSuccess() const { return Status == EGenerationStatus::Success || Status == EGenerationStatus::Updated; }
	FString GetFullPath() const { return AssetPath / AssetName; }

	// Factory methods
	static FGenerationResult MakeSuccess(const FString& InAssetType, const FString& InName, const FString& InPath, UObject* Asset, const FString& InMessage = TEXT(""))
	{
		FGenerationResult Result;
		Result.AssetType = InAssetType;
		Result.AssetName = InName;
		Result.AssetPath = InPath;
		Result.Status = EGenerationStatus::Success;
		Result.Message = InMessage.IsEmpty() ? TEXT("Created successfully") : InMessage;
		Result.GeneratedAsset = Asset;
		return Result;
	}

	static FGenerationResult MakeUpdated(const FString& InAssetType, const FString& InName, const FString& InPath, UObject* Asset, const FString& InMessage = TEXT(""))
	{
		FGenerationResult Result;
		Result.AssetType = InAssetType;
		Result.AssetName = InName;
		Result.AssetPath = InPath;
		Result.Status = EGenerationStatus::Updated;
		Result.Message = InMessage.IsEmpty() ? TEXT("Updated successfully") : InMessage;
		Result.GeneratedAsset = Asset;
		return Result;
	}

	static FGenerationResult MakeSkipped(const FString& InAssetType, const FString& InName, const FString& InPath, const FString& InMessage)
	{
		FGenerationResult Result;
		Result.AssetType = InAssetType;
		Result.AssetName = InName;
		Result.AssetPath = InPath;
		Result.Status = EGenerationStatus::Skipped;
		Result.Message = InMessage;
		return Result;
	}

	static FGenerationResult MakeFailed(const FString& InAssetType, const FString& InName, const FString& InPath, const FString& InMessage)
	{
		FGenerationResult Result;
		Result.AssetType = InAssetType;
		Result.AssetName = InName;
		Result.AssetPath = InPath;
		Result.Status = EGenerationStatus::Failed;
		Result.Message = InMessage;
		return Result;
	}
};

/**
 * Report of batch asset generation
 */
USTRUCT(BlueprintType)
struct ASSETFACTORY_API FGenerationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	TArray<FGenerationResult> Results;

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	int32 TotalCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	int32 SuccessCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	int32 UpdatedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	int32 SkippedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AssetFactory")
	int32 FailedCount = 0;

	void AddResult(const FGenerationResult& Result)
	{
		Results.Add(Result);
		TotalCount++;

		switch (Result.Status)
		{
		case EGenerationStatus::Success:
			SuccessCount++;
			break;
		case EGenerationStatus::Updated:
			UpdatedCount++;
			break;
		case EGenerationStatus::Skipped:
			SkippedCount++;
			break;
		case EGenerationStatus::Failed:
			FailedCount++;
			break;
		}
	}

	FString GetSummary() const
	{
		return FString::Printf(TEXT("Total: %d | Success: %d | Updated: %d | Skipped: %d | Failed: %d"),
			TotalCount, SuccessCount, UpdatedCount, SkippedCount, FailedCount);
	}

	bool HasFailures() const { return FailedCount > 0; }
	bool IsAllSuccess() const { return FailedCount == 0 && SkippedCount == 0; }
};
