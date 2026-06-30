// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentSidecarDelta.h"

#include "Dom/JsonObject.h"

namespace
{
bool ParseBodyPath(const FString& BodyPath, TArray<FString>& OutSegments, FString* OutError = nullptr)
{
	OutSegments.Reset();

	if (BodyPath.IsEmpty())
	{
		if (OutError)
		{
			*OutError = TEXT("Region BodyPath is empty.");
		}
		return false;
	}

	if (BodyPath.StartsWith(TEXT("$")))
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Region BodyPath '%s' must be a dotted path, not JSONPath syntax."), *BodyPath);
		}
		return false;
	}

	BodyPath.ParseIntoArray(OutSegments, TEXT("."), false);
	for (const FString& Segment : OutSegments)
	{
		if (Segment.IsEmpty())
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("Region BodyPath '%s' contains an empty path segment."), *BodyPath);
			}
			return false;
		}
	}

	if (OutSegments.Num() == 0)
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Region BodyPath '%s' has no path segments."), *BodyPath);
		}
		return false;
	}

	return true;
}

bool IsEmptyJsonObject(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Object && Value->AsObject().IsValid() && Value->AsObject()->Values.Num() == 0;
}

bool IsEmptyJsonArray(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Array && Value->AsArray().Num() == 0;
}

bool IsJsonNull(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && (Value->Type == EJson::Null || Value->Type == EJson::None);
}
}

FAssetDocumentSidecarRegionValue FAssetDocumentSidecarDelta::FindRegionValue(
	const TSharedRef<FJsonObject>& DocumentJson,
	const FAssetDocumentRegionPolicy& Policy)
{
	TArray<FString> Segments;
	if (!ParseBodyPath(Policy.BodyPath, Segments))
	{
		return {};
	}

	TSharedPtr<FJsonObject> CurrentObject = DocumentJson;
	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FString& Segment = Segments[SegmentIndex];
		const TSharedPtr<FJsonValue>* FieldValue = CurrentObject->Values.Find(Segment);
		if (!FieldValue)
		{
			return {};
		}

		if (SegmentIndex == Segments.Num() - 1)
		{
			FAssetDocumentSidecarRegionValue Result;
			Result.Value = *FieldValue;
			Result.State = IsExplicitEmptyRegion(Result.Value, Policy)
				? EAssetDocumentSidecarRegionState::ExplicitEmpty
				: EAssetDocumentSidecarRegionState::Present;
			return Result;
		}

		if (!FieldValue->IsValid() || (*FieldValue)->Type != EJson::Object)
		{
			return {};
		}

		CurrentObject = (*FieldValue)->AsObject();
		if (!CurrentObject.IsValid())
		{
			return {};
		}
	}

	return {};
}

bool FAssetDocumentSidecarDelta::SetRegionValue(
	const TSharedRef<FJsonObject>& DocumentJson,
	const FAssetDocumentRegionPolicy& Policy,
	const TSharedPtr<FJsonValue>& Value,
	FString& OutError)
{
	OutError.Reset();

	if (!Value.IsValid())
	{
		OutError = TEXT("Region value is invalid.");
		return false;
	}

	TArray<FString> Segments;
	if (!ParseBodyPath(Policy.BodyPath, Segments, &OutError))
	{
		return false;
	}

	TSharedPtr<FJsonObject> CurrentObject = DocumentJson;
	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num() - 1; ++SegmentIndex)
	{
		const FString& Segment = Segments[SegmentIndex];
		const TSharedPtr<FJsonValue>* FieldValue = CurrentObject->Values.Find(Segment);
		if (!FieldValue)
		{
			TSharedRef<FJsonObject> NewObject = MakeShared<FJsonObject>();
			CurrentObject->SetObjectField(Segment, NewObject);
			CurrentObject = NewObject;
			continue;
		}

		if (!FieldValue->IsValid() || (*FieldValue)->Type != EJson::Object)
		{
			OutError = FString::Printf(TEXT("Region BodyPath '%s' segment '%s' is not an object."), *Policy.BodyPath, *Segment);
			return false;
		}

		CurrentObject = (*FieldValue)->AsObject();
		if (!CurrentObject.IsValid())
		{
			OutError = FString::Printf(TEXT("Region BodyPath '%s' segment '%s' is not a valid object."), *Policy.BodyPath, *Segment);
			return false;
		}
	}

	CurrentObject->SetField(Segments.Last(), Value);
	return true;
}

FString FAssetDocumentSidecarDelta::HashSidecarRegion(
	const TSharedRef<FJsonObject>& DocumentJson,
	const FAssetDocumentRegionPolicy& Policy)
{
	return HashSidecarRegion(DocumentJson, Policy, EAssetDocumentRegionCanonicalizeSource::SidecarAuthored);
}

FString FAssetDocumentSidecarDelta::HashSidecarRegion(
	const TSharedRef<FJsonObject>& DocumentJson,
	const FAssetDocumentRegionPolicy& Policy,
	EAssetDocumentRegionCanonicalizeSource Source,
	UClass* AssetClass)
{
	const FAssetDocumentSidecarRegionValue Region = FindRegionValue(DocumentJson, Policy);
	if (Region.State == EAssetDocumentSidecarRegionState::Unset)
	{
		return FString();
	}

	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = Source;
	Context.AssetClass = AssetClass;
	return FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, Region.Value);
}

bool FAssetDocumentSidecarDelta::IsExplicitEmptyRegion(
	const TSharedPtr<FJsonValue>& Value,
	const FAssetDocumentRegionPolicy& Policy)
{
	if (IsEmptyJsonArray(Value)
		&& (Policy.ApplyMode == EAssetDocumentApplyMode::RebuildArrayRegion
			|| Policy.ExplicitDeleteValues.Contains(FAssetDocumentExplicitDeleteValues::EmptyArray())))
	{
		return true;
	}

	if (IsEmptyJsonObject(Value) && Policy.ExplicitDeleteValues.Contains(FAssetDocumentExplicitDeleteValues::EmptyObject()))
	{
		return true;
	}

	if (IsJsonNull(Value) && Policy.ExplicitDeleteValues.Contains(FAssetDocumentExplicitDeleteValues::Null()))
	{
		return true;
	}

	return false;
}
