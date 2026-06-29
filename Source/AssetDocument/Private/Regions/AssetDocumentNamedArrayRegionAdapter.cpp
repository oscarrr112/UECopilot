// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentNamedArrayRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
FString RegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FString FieldPath(const FAssetDocumentRegionContext& Context, const int32 Index, const FString& FieldName)
{
	return FString::Printf(
		TEXT("%s/%d/%s"),
		*RegionPath(Context),
		Index,
		*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(FieldName));
}

bool IsArrayRegion(const FAssetDocumentRegionContext& Context)
{
	return !Context.Policy
		|| Context.Policy->RegionKind == EAssetDocumentRegionKind::Array
		|| Context.Policy->RegionKind == EAssetDocumentRegionKind::Timeline;
}

FAssetDocumentCapabilityResult Failure(
	const FAssetDocumentRegionContext& Context,
	const int32 Index,
	const FString& FieldName,
	const FString& Code,
	const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(FieldPath(Context, Index, FieldName), Code, Message);
}
}

FAssetDocumentNamedArrayRegionAdapter::FAssetDocumentNamedArrayRegionAdapter()
	: FAssetDocumentNamedArrayRegionAdapter(FAssetDocumentNamedArrayRegionAdapterConfig(), {})
{
}

FAssetDocumentNamedArrayRegionAdapter::FAssetDocumentNamedArrayRegionAdapter(
	FName InName,
	FString InIdentityField,
	FAssetDocumentNamedArrayRegionAdapterHooks InHooks)
	: Config()
	, Hooks(MoveTemp(InHooks))
{
	Config.Name = InName;
	Config.IdentityField = MoveTemp(InIdentityField);
}

FAssetDocumentNamedArrayRegionAdapter::FAssetDocumentNamedArrayRegionAdapter(
	FAssetDocumentNamedArrayRegionAdapterConfig InConfig,
	FAssetDocumentNamedArrayRegionAdapterHooks InHooks)
	: Config(MoveTemp(InConfig))
	, Hooks(MoveTemp(InHooks))
{
	if (Config.Name.IsNone())
	{
		Config.Name = DefaultAdapterName();
	}
	if (Config.IdentityField.IsEmpty())
	{
		Config.IdentityField = TEXT("Name");
	}
}

FName FAssetDocumentNamedArrayRegionAdapter::DefaultAdapterName()
{
	return TEXT("AssetDocumentNamedArrayRegionAdapter");
}

FName FAssetDocumentNamedArrayRegionAdapter::GetName() const
{
	return Config.Name;
}

bool FAssetDocumentNamedArrayRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return IsArrayRegion(Context);
}

TSharedRef<FJsonObject> FAssetDocumentNamedArrayRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext& Context) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Adapter"), GetName().ToString());
	Schema->SetStringField(TEXT("Shape"), TEXT("array<object>"));
	Schema->SetStringField(TEXT("IdentityField"), Config.IdentityField);
	if (!Config.IdentityAliases.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> AliasValues;
		for (const FString& Alias : Config.IdentityAliases)
		{
			AliasValues.Add(MakeShared<FJsonValueString>(Alias));
		}
		Schema->SetArrayField(TEXT("IdentityAliases"), MoveTemp(AliasValues));
	}
	Schema->SetStringField(TEXT("BodyPath"), Context.BodyPath);
	Schema->SetBoolField(TEXT("CanonicalizeByIdentity"), Config.bCanonicalizeByIdentity);
	Schema->SetBoolField(TEXT("PreserveAuthoredApplyOrder"), Config.bPreserveAuthoredApplyOrder);
	return Schema;
}

bool FAssetDocumentNamedArrayRegionAdapter::TryReadIdentity(
	const TSharedRef<FJsonObject>& Element,
	FString& OutIdentity,
	FString& OutIdentityField) const
{
	TArray<FString> CandidateFields;
	CandidateFields.Add(Config.IdentityField);
	CandidateFields.Append(Config.IdentityAliases);

	for (const FString& CandidateField : CandidateFields)
	{
		const TSharedPtr<FJsonValue> IdentityValue = Element->TryGetField(CandidateField);
		if (!IdentityValue.IsValid() || IdentityValue->Type != EJson::String)
		{
			continue;
		}

		const FString Identity = IdentityValue->AsString().TrimStartAndEnd();
		if (!Identity.IsEmpty())
		{
			OutIdentity = Identity;
			OutIdentityField = CandidateField;
			return true;
		}
	}

	OutIdentity.Reset();
	OutIdentityField = Config.IdentityField;
	return false;
}

FAssetDocumentCapabilityResult FAssetDocumentNamedArrayRegionAdapter::ParseElements(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& Value,
	TArray<TSharedRef<FJsonObject>>& OutElements) const
{
	TArray<TSharedPtr<FJsonValue>> Values;
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentJsonRegionUtils::RequireArrayValue(Value, RegionPath(Context), Values);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSet<FString> SeenIdentities;
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		TSharedPtr<FJsonObject> ElementObject;
		Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(
			Values[Index],
			FString::Printf(TEXT("%s/%d"), *RegionPath(Context), Index),
			ElementObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		FString Identity;
		FString IdentityField;
		if (!TryReadIdentity(ElementObject.ToSharedRef(), Identity, IdentityField))
		{
			return Failure(
				Context,
				Index,
				Config.IdentityField,
				Config.MissingIdentityCode,
				FString::Printf(TEXT("%s is required for named array elements"), *Config.IdentityField));
		}

		if (SeenIdentities.Contains(Identity))
		{
			return Failure(
				Context,
				Index,
				IdentityField,
				Config.DuplicateIdentityCode,
				FString::Printf(TEXT("Duplicate named array identity %s"), *Identity));
		}
		SeenIdentities.Add(Identity);

		if (Hooks.ValidateElement)
		{
			Result = Hooks.ValidateElement(Context, ElementObject.ToSharedRef(), Index);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}

		OutElements.Add(ElementObject.ToSharedRef());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Parsed named array region"));
}

TArray<TSharedRef<FJsonObject>> FAssetDocumentNamedArrayRegionAdapter::SortElementsForCanonicalOrder(
	TArray<TSharedRef<FJsonObject>> Elements) const
{
	Elements.Sort(
		[this](const TSharedRef<FJsonObject>& Left, const TSharedRef<FJsonObject>& Right)
		{
			FString LeftIdentity;
			FString LeftField;
			FString RightIdentity;
			FString RightField;
			TryReadIdentity(Left, LeftIdentity, LeftField);
			TryReadIdentity(Right, RightIdentity, RightField);
			if (LeftIdentity != RightIdentity)
			{
				return LeftIdentity < RightIdentity;
			}
			return FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeShared<FJsonValueObject>(Left)) <
				FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeShared<FJsonValueObject>(Right));
		});
	return Elements;
}

TSharedPtr<FJsonValue> FAssetDocumentNamedArrayRegionAdapter::MakeArrayValueFromElements(
	TArray<TSharedRef<FJsonObject>> Elements,
	const bool bCanonicalize) const
{
	if (bCanonicalize)
	{
		Elements = SortElementsForCanonicalOrder(MoveTemp(Elements));
	}

	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Elements.Num());
	for (const TSharedRef<FJsonObject>& Element : Elements)
	{
		Values.Add(MakeShared<FJsonValueObject>(Element));
	}
	return MakeShared<FJsonValueArray>(MoveTemp(Values));
}

FAssetDocumentCapabilityResult FAssetDocumentNamedArrayRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	TArray<TSharedRef<FJsonObject>> Elements;
	return ParseElements(Context, DesiredValue, Elements);
}

FAssetDocumentCapabilityResult FAssetDocumentNamedArrayRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;

	TArray<TSharedRef<FJsonObject>> Elements;
	FAssetDocumentCapabilityResult Result = ParseElements(Context, DesiredValue, Elements);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!Config.bPreserveAuthoredApplyOrder)
	{
		Elements = SortElementsForCanonicalOrder(MoveTemp(Elements));
	}

	if (Hooks.ApplyElements)
	{
		return Hooks.ApplyElements(Context, Elements, bOutChanged);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Named array region has no apply hook"));
}

FAssetDocumentCapabilityResult FAssetDocumentNamedArrayRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	TArray<TSharedRef<FJsonObject>> Elements;
	if (Hooks.ExtractElements)
	{
		const FAssetDocumentCapabilityResult Result = Hooks.ExtractElements(Context, Elements);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	OutCurrentValue = MakeArrayValueFromElements(MoveTemp(Elements), Config.bCanonicalizeByIdentity);
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted named array region"));
}

FAssetDocumentCapabilityResult FAssetDocumentNamedArrayRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TArray<TSharedRef<FJsonObject>> DesiredElements;
	FAssetDocumentCapabilityResult Result = ParseElements(Context, DesiredValue, DesiredElements);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Hooks.DiffElements)
	{
		return Hooks.DiffElements(Context, DesiredElements, OutDiffEntries);
	}

	TSharedPtr<FJsonValue> CurrentValue;
	Result = ExtractRegion(Context, CurrentValue);
	if (!Result.bSuccess)
	{
		return Result;
	}

	const TSharedPtr<FJsonValue> CanonicalDesired =
		MakeArrayValueFromElements(MoveTemp(DesiredElements), Config.bCanonicalizeByIdentity);

	if (FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentValue) !=
		FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CanonicalDesired))
	{
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			RegionPath(Context),
			TEXT("changed"),
			CurrentValue,
			DesiredValue);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed named array region"));
}
