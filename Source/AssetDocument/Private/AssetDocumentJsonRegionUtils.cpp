// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentJsonRegionUtils.h"

#include "AssetDocumentCanonicalJson.h"

FAssetDocumentCapabilityResult FAssetDocumentJsonRegionUtils::Failure(
	const FString& Path,
	const FString& Code,
	const FString& Message)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult FAssetDocumentJsonRegionUtils::RequireObjectValue(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutObject)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return Failure(Path, TEXT("InvalidBodySectionType"), TEXT("Expected a JSON object"));
	}

	OutObject = Value->AsObject();
	if (!OutObject.IsValid())
	{
		return Failure(Path, TEXT("InvalidBodySectionType"), TEXT("Expected a JSON object"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentJsonRegionUtils::RequireArrayValue(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	const TArray<TSharedPtr<FJsonValue>>*& OutArray)
{
	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return Failure(Path, TEXT("InvalidBodySectionType"), TEXT("Expected a JSON array"));
	}

	OutArray = &Value->AsArray();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentJsonRegionUtils::RequireStringField(
	const TSharedPtr<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	FString& OutString,
	const FString& Code)
{
	if (!Object.IsValid() || !Object->TryGetStringField(FieldName, OutString) || OutString.IsEmpty())
	{
		return Failure(Path, Code, FString::Printf(TEXT("%s must be a non-empty string"), *FieldName));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentJsonRegionUtils::RequireNumberField(
	const TSharedPtr<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	double& OutNumber,
	const FString& Code)
{
	if (!Object.IsValid() || !Object->TryGetNumberField(FieldName, OutNumber))
	{
		return Failure(Path, Code, FString::Printf(TEXT("%s must be a number"), *FieldName));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentJsonRegionUtils::RequireBoolField(
	const TSharedPtr<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	bool& OutBool,
	const FString& Code)
{
	if (!Object.IsValid() || !Object->TryGetBoolField(FieldName, OutBool))
	{
		return Failure(Path, Code, FString::Printf(TEXT("%s must be a boolean"), *FieldName));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FString FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(const FString& Token)
{
	return Token.Replace(TEXT("~"), TEXT("~0")).Replace(TEXT("/"), TEXT("~1"));
}

FString FAssetDocumentJsonRegionUtils::MakeBodyPath(const FString& BodyKey)
{
	return FString::Printf(TEXT("/Body/%s"), *EscapeJsonPointerToken(BodyKey));
}

FString FAssetDocumentJsonRegionUtils::MakeBodyArrayItemPath(const FString& BodyKey, int32 Index)
{
	return FString::Printf(TEXT("%s/%d"), *MakeBodyPath(BodyKey), Index);
}

FString FAssetDocumentJsonRegionUtils::JsonValueToComparableString(const TSharedPtr<FJsonValue>& Value)
{
	return FAssetDocumentCanonicalJson::WriteCanonicalJson(Value.IsValid() ? Value : MakeShared<FJsonValueNull>());
}

void FAssetDocumentJsonRegionUtils::AddDiffEntry(
	TArray<TSharedPtr<FJsonValue>>& Entries,
	const FString& Path,
	const FString& Status,
	TSharedPtr<FJsonValue> Current,
	TSharedPtr<FJsonValue> Desired)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), Path);
	Entry->SetStringField(TEXT("status"), Status);
	Entry->SetField(TEXT("current"), Current.IsValid() ? Current : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("desired"), Desired.IsValid() ? Desired : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}
