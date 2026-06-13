// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentPolicyRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
const TCHAR* ToString(EAssetDocumentRegionKind Value)
{
	switch (Value)
	{
	case EAssetDocumentRegionKind::Scalar:
		return TEXT("Scalar");
	case EAssetDocumentRegionKind::Object:
		return TEXT("Object");
	case EAssetDocumentRegionKind::Array:
		return TEXT("Array");
	case EAssetDocumentRegionKind::Timeline:
		return TEXT("Timeline");
	case EAssetDocumentRegionKind::Graph:
		return TEXT("Graph");
	default:
		return TEXT("Object");
	}
}

const TCHAR* ToString(EAssetDocumentDefaultSource Value)
{
	switch (Value)
	{
	case EAssetDocumentDefaultSource::CDO:
		return TEXT("CDO");
	case EAssetDocumentDefaultSource::EmptyTemplate:
		return TEXT("EmptyTemplate");
	case EAssetDocumentDefaultSource::CurrentAssetBaseline:
		return TEXT("CurrentAssetBaseline");
	case EAssetDocumentDefaultSource::ProfileDeclared:
		return TEXT("ProfileDeclared");
	default:
		return TEXT("ProfileDeclared");
	}
}

const TCHAR* ToString(EAssetDocumentReducerMode Value)
{
	switch (Value)
	{
	case EAssetDocumentReducerMode::DefaultDiff:
		return TEXT("DefaultDiff");
	case EAssetDocumentReducerMode::ManagedRegion:
		return TEXT("ManagedRegion");
	default:
		return TEXT("DefaultDiff");
	}
}

const TCHAR* ToString(EAssetDocumentApplyMode Value)
{
	switch (Value)
	{
	case EAssetDocumentApplyMode::SetProperty:
		return TEXT("SetProperty");
	case EAssetDocumentApplyMode::RebuildArrayRegion:
		return TEXT("RebuildArrayRegion");
	case EAssetDocumentApplyMode::ExtensionHook:
		return TEXT("ExtensionHook");
	default:
		return TEXT("SetProperty");
	}
}

TArray<TSharedPtr<FJsonValue>> MakeStringArray(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		Result.Add(MakeShared<FJsonValueString>(Value));
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> MakeStringArray(const TSet<FString>& Values)
{
	TArray<FString> SortedValues = Values.Array();
	SortedValues.Sort();
	return MakeStringArray(SortedValues);
}

TSharedRef<FJsonObject> IdentityRuleToJson(const FAssetDocumentIdentityRule& Rule)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	if (!Rule.FieldPath.IsEmpty())
	{
		Json->SetStringField(TEXT("FieldPath"), Rule.FieldPath);
	}
	if (!Rule.UePropertyPath.IsEmpty())
	{
		Json->SetStringField(TEXT("UePropertyPath"), Rule.UePropertyPath);
	}
	if (!Rule.StableKeyField.IsEmpty())
	{
		Json->SetStringField(TEXT("StableKeyField"), Rule.StableKeyField);
	}
	return Json;
}

TSharedRef<FJsonObject> ComparisonRuleToJson(const FAssetDocumentComparisonRule& Rule)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	if (!Rule.FieldPath.IsEmpty())
	{
		Json->SetStringField(TEXT("FieldPath"), Rule.FieldPath);
	}
	if (!Rule.ComparatorName.IsEmpty())
	{
		Json->SetStringField(TEXT("ComparatorName"), Rule.ComparatorName);
	}
	if (Rule.bIgnoreOrder)
	{
		Json->SetBoolField(TEXT("IgnoreOrder"), true);
	}
	return Json;
}

TArray<TSharedPtr<FJsonValue>> MakeIdentityRuleArray(const TArray<FAssetDocumentIdentityRule>& Rules)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(Rules.Num());
	for (const FAssetDocumentIdentityRule& Rule : Rules)
	{
		Result.Add(MakeShared<FJsonValueObject>(IdentityRuleToJson(Rule)));
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> MakeComparisonRuleArray(const TArray<FAssetDocumentComparisonRule>& Rules)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(Rules.Num());
	for (const FAssetDocumentComparisonRule& Rule : Rules)
	{
		Result.Add(MakeShared<FJsonValueObject>(ComparisonRuleToJson(Rule)));
	}
	return Result;
}

FAssetDocumentRegionPolicyPreset MakeDefaultDiffPreset()
{
	FAssetDocumentRegionPolicyPreset Preset;
	Preset.PresetName = TEXT("DefaultDiff");
	Preset.PolicyVersion = 1;
	Preset.Defaults.RegionKind = EAssetDocumentRegionKind::Object;
	Preset.Defaults.DefaultSource = EAssetDocumentDefaultSource::CDO;
	Preset.Defaults.ReducerMode = EAssetDocumentReducerMode::DefaultDiff;
	Preset.Defaults.ApplyMode = EAssetDocumentApplyMode::SetProperty;
	return Preset;
}

FAssetDocumentRegionPolicyPreset MakeManagedRegionPreset()
{
	FAssetDocumentRegionPolicyPreset Preset;
	Preset.PresetName = TEXT("ManagedRegion");
	Preset.PolicyVersion = 1;
	Preset.Defaults.RegionKind = EAssetDocumentRegionKind::Array;
	Preset.Defaults.DefaultSource = EAssetDocumentDefaultSource::ProfileDeclared;
	Preset.Defaults.ReducerMode = EAssetDocumentReducerMode::ManagedRegion;
	Preset.Defaults.ApplyMode = EAssetDocumentApplyMode::RebuildArrayRegion;
	return Preset;
}

FAssetDocumentRegionPolicyPreset MakeExtensionHookPreset()
{
	FAssetDocumentRegionPolicyPreset Preset;
	Preset.PresetName = TEXT("ExtensionHook");
	Preset.PolicyVersion = 1;
	Preset.Defaults.RegionKind = EAssetDocumentRegionKind::Object;
	Preset.Defaults.DefaultSource = EAssetDocumentDefaultSource::ProfileDeclared;
	Preset.Defaults.ReducerMode = EAssetDocumentReducerMode::ManagedRegion;
	Preset.Defaults.ApplyMode = EAssetDocumentApplyMode::ExtensionHook;
	return Preset;
}
}

bool FAssetDocumentPolicyRegistry::GetBuiltinPreset(FName PresetName, FAssetDocumentRegionPolicyPreset& OutPreset)
{
	for (const FAssetDocumentRegionPolicyPreset& Preset : GetBuiltinPresets())
	{
		if (Preset.PresetName == PresetName)
		{
			OutPreset = Preset;
			return true;
		}
	}
	return false;
}

TArray<FAssetDocumentRegionPolicyPreset> FAssetDocumentPolicyRegistry::GetBuiltinPresets()
{
	return {
		MakeDefaultDiffPreset(),
		MakeManagedRegionPreset(),
		MakeExtensionHookPreset()
	};
}

bool FAssetDocumentPolicyRegistry::ExpandPreset(
	const FAssetDocumentRegionPolicyPreset& Preset,
	const FAssetDocumentRegionPolicyOverride& Override,
	FAssetDocumentRegionPolicy& OutPolicy)
{
	if (Preset.PresetName.IsNone())
	{
		return false;
	}

	OutPolicy = Preset.Defaults;
	if (Override.RegionId.IsSet())
	{
		OutPolicy.RegionId = Override.RegionId.GetValue();
	}
	if (Override.BodyPath.IsSet())
	{
		OutPolicy.BodyPath = Override.BodyPath.GetValue();
	}
	if (Override.RegionKind.IsSet())
	{
		OutPolicy.RegionKind = Override.RegionKind.GetValue();
	}
	if (Override.DefaultSource.IsSet())
	{
		OutPolicy.DefaultSource = Override.DefaultSource.GetValue();
	}
	if (Override.ReducerMode.IsSet())
	{
		OutPolicy.ReducerMode = Override.ReducerMode.GetValue();
	}
	if (Override.ApplyMode.IsSet())
	{
		OutPolicy.ApplyMode = Override.ApplyMode.GetValue();
	}
	if (Override.IdentityRules.Num() > 0)
	{
		OutPolicy.IdentityRules = Override.IdentityRules;
	}
	if (Override.ComparisonRules.Num() > 0)
	{
		OutPolicy.ComparisonRules = Override.ComparisonRules;
	}
	if (Override.ManagedUePropertyPaths.Num() > 0)
	{
		OutPolicy.ManagedUePropertyPaths = Override.ManagedUePropertyPaths;
	}
	if (Override.ExtractOnlyFields.Num() > 0)
	{
		OutPolicy.ExtractOnlyFields = Override.ExtractOnlyFields;
	}
	if (Override.ExplicitDeleteValues.Num() > 0)
	{
		OutPolicy.ExplicitDeleteValues = Override.ExplicitDeleteValues;
	}
	if (Override.ExtensionHookName.IsSet())
	{
		OutPolicy.ExtensionHookName = Override.ExtensionHookName.GetValue();
	}

	return !OutPolicy.RegionId.IsNone() && !OutPolicy.BodyPath.IsEmpty();
}

TSharedRef<FJsonObject> FAssetDocumentPolicyRegistry::ExportPolicyToJson(const FAssetDocumentRegionPolicy& Policy)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	if (!Policy.RegionId.IsNone())
	{
		Json->SetStringField(TEXT("RegionId"), Policy.RegionId.ToString());
	}
	if (!Policy.BodyPath.IsEmpty())
	{
		Json->SetStringField(TEXT("BodyPath"), Policy.BodyPath);
	}
	Json->SetStringField(TEXT("RegionKind"), ToString(Policy.RegionKind));
	Json->SetStringField(TEXT("DefaultSource"), ToString(Policy.DefaultSource));
	Json->SetStringField(TEXT("ReducerMode"), ToString(Policy.ReducerMode));
	Json->SetStringField(TEXT("ApplyMode"), ToString(Policy.ApplyMode));

	if (Policy.IdentityRules.Num() > 0)
	{
		Json->SetArrayField(TEXT("IdentityRules"), MakeIdentityRuleArray(Policy.IdentityRules));
	}
	if (Policy.ComparisonRules.Num() > 0)
	{
		Json->SetArrayField(TEXT("ComparisonRules"), MakeComparisonRuleArray(Policy.ComparisonRules));
	}
	if (Policy.ManagedUePropertyPaths.Num() > 0)
	{
		Json->SetArrayField(TEXT("ManagedUePropertyPaths"), MakeStringArray(Policy.ManagedUePropertyPaths));
	}
	if (Policy.ExtractOnlyFields.Num() > 0)
	{
		Json->SetArrayField(TEXT("ExtractOnlyFields"), MakeStringArray(Policy.ExtractOnlyFields));
	}
	if (Policy.ExplicitDeleteValues.Num() > 0)
	{
		Json->SetArrayField(TEXT("ExplicitDeleteValues"), MakeStringArray(Policy.ExplicitDeleteValues));
	}
	if (Policy.ExtensionHookName.IsSet())
	{
		Json->SetStringField(TEXT("ExtensionHookName"), Policy.ExtensionHookName.GetValue().ToString());
	}

	return Json;
}

TSharedRef<FJsonObject> FAssetDocumentPolicyRegistry::ExportPresetToJson(const FAssetDocumentRegionPolicyPreset& Preset)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("PresetName"), Preset.PresetName.ToString());
	Json->SetNumberField(TEXT("PolicyVersion"), Preset.PolicyVersion);
	Json->SetObjectField(TEXT("Defaults"), ExportPolicyToJson(Preset.Defaults));
	return Json;
}
