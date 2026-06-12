// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFragment.h"

#include "Utils/PropertySetterUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Class.h"

namespace
{
UScriptStruct* ResolveScriptStruct(const FString& StructName)
{
	FString NormalizedStructName = StructName;
	NormalizedStructName.TrimStartAndEndInline();
	if (NormalizedStructName.IsEmpty())
	{
		return nullptr;
	}

	if (UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *NormalizedStructName))
	{
		return Struct;
	}

	return FindObject<UScriptStruct>(nullptr, *NormalizedStructName);
}

class FAssetDocumentStructValueFragmentAdapter final : public IAssetDocumentFragmentAdapter
{
public:
	virtual FName GetKind() const override
	{
		return TEXT("StructValue");
	}

	virtual bool SupportsContext(const FAssetDocumentFragmentContext& Context) const override
	{
		return true;
	}

	virtual FAssetDocumentFragmentResult Validate(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FString StructName;
		if (!FragmentJson->TryGetStringField(TEXT("Struct"), StructName) || StructName.TrimStartAndEnd().IsEmpty())
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("StructValue field 'Struct' is required."), Context.JsonPath, TEXT("missing-structvalue-struct"));
		}

		const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
		if (!FragmentJson->TryGetObjectField(TEXT("Properties"), PropertiesPtr) || !PropertiesPtr || !PropertiesPtr->IsValid())
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("StructValue field 'Properties' is required."), Context.JsonPath, TEXT("missing-structvalue-properties"));
		}

		return FAssetDocumentFragmentResult::Success();
	}

	virtual FAssetDocumentFragmentResult Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FString StructName;
		if (!FragmentJson->TryGetStringField(TEXT("Struct"), StructName) || StructName.TrimStartAndEnd().IsEmpty())
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("StructValue field 'Struct' is required."), Context.JsonPath, TEXT("missing-structvalue-struct"));
		}

		const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
		if (!FragmentJson->TryGetObjectField(TEXT("Properties"), PropertiesPtr) || !PropertiesPtr || !PropertiesPtr->IsValid())
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("StructValue field 'Properties' is required."), Context.JsonPath, TEXT("missing-structvalue-properties"));
		}
		TSharedPtr<FJsonObject> Properties = *PropertiesPtr;

		UScriptStruct* Struct = ResolveScriptStruct(StructName);
		if (!Struct)
		{
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("Failed to resolve struct '%s'."), *StructName),
				Context.JsonPath,
				TEXT("structvalue-resolve-failed"));
		}

		if (Context.ExpectedStruct && Struct != Context.ExpectedStruct)
		{
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("Struct '%s' does not match expected struct '%s'."), *Struct->GetName(), *Context.ExpectedStruct->GetName()),
				Context.JsonPath,
				TEXT("structvalue-struct-mismatch"));
		}

		FAssetDocumentFragmentResult Result = FAssetDocumentFragmentResult::Success();
		Result.StructType = Struct;
		Result.StructBytes.SetNumUninitialized(Struct->GetStructureSize());
		Struct->InitializeStruct(Result.StructBytes.GetData());

		if (!FPropertySetterUtils::SetStructFromJson(Struct, Result.StructBytes.GetData(), MakeShared<FJsonValueObject>(Properties)))
		{
			Struct->DestroyStruct(Result.StructBytes.GetData());
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("Failed to apply properties to struct '%s'."), *Struct->GetName()),
				Context.JsonPath,
				TEXT("structvalue-apply-failed"));
		}

		Result.Value = MakeShared<FJsonValueObject>(Properties);
		return Result;
	}

	virtual FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const override
	{
		if (!Context.StructType || !Context.StructValue)
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("StructValue extraction requires StructType and StructValue."), Context.JsonPath, TEXT("structvalue-extract-missing-value"));
		}

		OutFragmentJson->SetStringField(TEXT("Kind"), GetKind().ToString());
		OutFragmentJson->SetStringField(TEXT("Struct"), Context.StructType->GetPathName());
		return FAssetDocumentFragmentResult::Success();
	}
};
}

TSharedRef<IAssetDocumentFragmentAdapter> CreateAssetDocumentStructValueFragmentAdapter()
{
	return MakeShared<FAssetDocumentStructValueFragmentAdapter>();
}
