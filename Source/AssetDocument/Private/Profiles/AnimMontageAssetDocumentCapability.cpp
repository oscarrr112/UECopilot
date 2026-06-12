// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimMontageAssetDocumentCapability.h"

#include "Animation/AnimMontage.h"
#include "Dom/JsonValue.h"

namespace
{
const TSet<FString>& GetAnimMontageBodyKeys()
{
	static const TSet<FString> Keys = {
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
	return FAssetDocumentCapabilityResult::Success(TEXT("AnimMontage body apply is deferred"));
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	return FAssetDocumentCapabilityResult::Success(TEXT("AnimMontage body extract is deferred"));
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

		if (!GetAnimMontageBodyKeys().Contains(Pair.Key))
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
