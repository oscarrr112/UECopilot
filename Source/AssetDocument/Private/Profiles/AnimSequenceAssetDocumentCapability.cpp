// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimSequenceAssetDocumentCapability.h"

#include "Animation/AnimSequence.h"
#include "Dom/JsonValue.h"

namespace
{
bool IsKnownBodyKey(const FString& BodyKey)
{
	for (const FName& KnownBodyKey : FAnimSequenceAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (KnownBodyKey.ToString() == BodyKey)
		{
			return true;
		}
	}
	return false;
}

bool IsObject(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Object;
}

bool IsArray(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Array;
}

FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult UnsupportedTask1Operation(const TCHAR* Operation)
{
	return BodyFailure(
		FString::Printf(TEXT("AnimSequence Body %s is not implemented in Task 1; this checkpoint only registers the profile, schema, validation shape, and region policies"), Operation),
		TEXT("/Body"),
		TEXT("UnsupportedOperation"));
}
}

const TArray<FName>& FAnimSequenceAssetDocumentCapability::GetCanonicalBodyKeys()
{
	static const TArray<FName> BodyKeys = {
		TEXT("References"),
		TEXT("Preview"),
		TEXT("Playback"),
		TEXT("Additive"),
		TEXT("RootMotion"),
		TEXT("Compression"),
		TEXT("Curves"),
		TEXT("Notifies"),
		TEXT("NotifyStates"),
		TEXT("NotifyTracks"),
		TEXT("SyncMarkers"),
		TEXT("Metadata"),
		TEXT("AssetUserData"),
	};
	return BodyKeys;
}

FName FAnimSequenceAssetDocumentCapability::GetName() const
{
	return TEXT("AnimSequenceBody");
}

TArray<FName> FAnimSequenceAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {GetName()};
}

int32 FAnimSequenceAssetDocumentCapability::GetApplyOrder() const
{
	return 100;
}

bool FAnimSequenceAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && Asset->IsA<UAnimSequence>();
}

bool FAnimSequenceAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UAnimSequence::StaticClass();
}

TSharedRef<FJsonObject> FAnimSequenceAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Hint = MakeShared<FJsonObject>();
	Hint->SetStringField(TEXT("References"), TEXT("object: Skeleton, RetargetSource, RetargetSourceAsset"));
	Hint->SetStringField(TEXT("Preview"), TEXT("object: PreviewMesh, PreviewPoseAsset"));
	Hint->SetStringField(TEXT("Playback"), TEXT("object: RateScale; derived length/sample fields are extract-only"));
	Hint->SetStringField(TEXT("Additive"), TEXT("object: AdditiveAnimType, RefPoseType, RefFrameIndex, RefPoseSeq"));
	Hint->SetStringField(TEXT("RootMotion"), TEXT("object: bEnableRootMotion, RootMotionRootLock, bForceRootLock, bUseNormalizedRootMotionScale"));
	Hint->SetStringField(TEXT("Compression"), TEXT("object: CompressionErrorThresholdScale, BoneCompressionSettings, CurveCompressionSettings, bDoNotOverrideCompression"));
	Hint->SetStringField(TEXT("Curves"), TEXT("array: sequence-owned float curves"));
	Hint->SetStringField(TEXT("Notifies"), TEXT("array: point notify placements"));
	Hint->SetStringField(TEXT("NotifyStates"), TEXT("array: ranged notify-state placements"));
	Hint->SetStringField(TEXT("NotifyTracks"), TEXT("array: notify track names/order"));
	Hint->SetStringField(TEXT("SyncMarkers"), TEXT("array: authored sync marker timeline"));
	Hint->SetStringField(TEXT("Metadata"), TEXT("array: UAnimMetaData embedded object fragments"));
	Hint->SetStringField(TEXT("AssetUserData"), TEXT("array: UAssetUserData embedded object fragments"));
	return Hint;
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>& BodyJson) const
{
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

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	return Validate(Context, BodyJson);
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&)
{
	return UnsupportedTask1Operation(TEXT("apply"));
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext&, TSharedRef<FJsonObject>&) const
{
	return UnsupportedTask1Operation(TEXT("extract"));
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&, TArray<TSharedPtr<FJsonValue>>&) const
{
	return UnsupportedTask1Operation(TEXT("diff"));
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::ValidateBodyObject(const TSharedRef<FJsonObject>& BodyObject) const
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		const FString Path = FString::Printf(TEXT("/Body/%s"), *Pair.Key);
		if (Pair.Key == TEXT("_Skipped"))
		{
			return BodyFailure(TEXT("Body._Skipped is extract-only diagnostic metadata and cannot be authored"), Path, TEXT("ExtractOnlyBodyKey"));
		}
		if (!IsKnownBodyKey(Pair.Key))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unknown AnimSequence Body key '%s'"), *Pair.Key),
				Path,
				TEXT("UnknownBodyKey"));
		}
	}

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

	const TArray<FAssetDocumentCapabilityResult> Results = {
		RequireObject(TEXT("References")),
		RequireObject(TEXT("Preview")),
		RequireObject(TEXT("Playback")),
		RequireObject(TEXT("Additive")),
		RequireObject(TEXT("RootMotion")),
		RequireObject(TEXT("Compression")),
		RequireArray(TEXT("Curves")),
		RequireArray(TEXT("Notifies")),
		RequireArray(TEXT("NotifyStates")),
		RequireArray(TEXT("NotifyTracks")),
		RequireArray(TEXT("SyncMarkers")),
		RequireArray(TEXT("Metadata")),
		RequireArray(TEXT("AssetUserData")),
	};

	for (const FAssetDocumentCapabilityResult& Result : Results)
	{
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("AnimSequence Body is valid"));
}
