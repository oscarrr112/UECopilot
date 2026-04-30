// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

class UAnimSequence;
class USkeletalMesh;
class USkeleton;
enum ERichCurveInterpMode : int;
struct FFrameRate;

class ASSETFACTORY_API FAnimSequenceGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("AnimSequence"); }
	virtual int32 GetPriority() const override { return 20; }

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action = EGenerationAction::Create) const override;
	virtual TArray<FString> GetRequiredFields() const override;
	virtual bool CanExtract(UObject* Asset) const override;
	virtual TSharedPtr<FJsonObject> Extract(UObject* Asset, bool bDiffOnly = false) const override;

private:
	FString BuildLongPackageName(const FString& Path, const FString& Name) const;
	USkeleton* LoadSkeleton(const FString& SkeletonPath) const;
	USkeletalMesh* LoadPreviewMesh(const FString& PreviewMeshPath) const;
	bool IsPreviewMeshCompatible(USkeletalMesh* PreviewMesh, USkeleton* Skeleton) const;
	FFrameRate ParseFrameRate(TSharedPtr<FJsonObject> Config) const;
	int32 ParseNumberOfFrames(TSharedPtr<FJsonObject> Config) const;
	UAnimSequence* CreateAnimSequence(const FString& Name, const FString& Path, TSharedPtr<FJsonObject> Config, FString& OutError) const;
	bool ApplyPatch(UAnimSequence* AnimSequence, TSharedPtr<FJsonObject> Config, FString& OutError) const;
	bool ValidateFloatCurves(const TArray<TSharedPtr<FJsonValue>>& FloatCurves, FString& OutError) const;
	bool ApplyFloatCurves(UAnimSequence* AnimSequence, const TArray<TSharedPtr<FJsonValue>>& FloatCurves, FString& OutError) const;
	ERichCurveInterpMode ParseInterpMode(const FString& InterpMode) const;
	FString InterpModeToString(ERichCurveInterpMode InterpMode) const;
	bool SaveAnimSequence(UAnimSequence* AnimSequence, FString& OutError) const;
};
