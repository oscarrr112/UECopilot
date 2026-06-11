// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "AssetDocumentClassResolver.h"
#include "AssetDocumentLifecycle.h"
#include "AssetDocumentPropertyAdapter.h"
#include "AssetDocumentSidecar.h"

#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"

namespace
{
FString NormalizeValidateFilePath(const FString& FilePath)
{
	if (FilePath.IsEmpty())
	{
		return FString();
	}

	FString NormalizedPath = FilePath;
	FPaths::NormalizeFilename(NormalizedPath);
	if (FPaths::IsRelative(NormalizedPath) && NormalizedPath.StartsWith(TEXT("Content/"), ESearchCase::IgnoreCase))
	{
		NormalizedPath = FPaths::Combine(FPaths::ProjectDir(), NormalizedPath);
	}

	NormalizedPath = FPaths::ConvertRelativePathToFull(NormalizedPath);
	FPaths::NormalizeFilename(NormalizedPath);
	return NormalizedPath;
}

FString NormalizeValidateTarget(const FString& Target)
{
	FString NormalizedTarget = Target;
	FPaths::NormalizeFilename(NormalizedTarget);
	NormalizedTarget.TrimStartAndEndInline();

	FString PackagePath;
	FString ObjectName;
	if (NormalizedTarget.Split(TEXT("."), &PackagePath, &ObjectName))
	{
		NormalizedTarget = PackagePath;
	}

	return NormalizedTarget;
}

bool ValidateApplyTarget(const FString& Target, FString& OutError)
{
	if (Target.IsEmpty())
	{
		OutError = TEXT("Target is required");
		return false;
	}

	if (!Target.StartsWith(TEXT("/Game/")))
	{
		OutError = FString::Printf(TEXT("Target '%s' must be a long package name under /Game"), *Target);
		return false;
	}

	FText PackageNameReason;
	if (!FPackageName::IsValidLongPackageName(Target, false, &PackageNameReason))
	{
		OutError = FString::Printf(TEXT("Target '%s' is not a valid long package name: %s"), *Target, *PackageNameReason.ToString());
		return false;
	}

	if (FPackageName::GetLongPackageAssetName(Target).IsEmpty())
	{
		OutError = FString::Printf(TEXT("Target '%s' must include a non-empty asset name"), *Target);
		return false;
	}

	return true;
}

FString ToObjectPath(const FString& PackageOrObjectPath)
{
	if (PackageOrObjectPath.Contains(TEXT(".")))
	{
		return PackageOrObjectPath;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageOrObjectPath);
	if (AssetName.IsEmpty())
	{
		return PackageOrObjectPath;
	}

	return FString::Printf(TEXT("%s.%s"), *PackageOrObjectPath, *AssetName);
}

UObject* LoadAssetFromPackageOrObjectPath(const FString& PackageOrObjectPath)
{
	if (PackageOrObjectPath.IsEmpty())
	{
		return nullptr;
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *ToObjectPath(PackageOrObjectPath));
	if (!Asset && PackageOrObjectPath.Contains(TEXT(".")))
	{
		Asset = LoadObject<UObject>(nullptr, *PackageOrObjectPath);
	}
	return Asset;
}

FString JsonValueToComparableString(TSharedPtr<FJsonValue> Value)
{
	TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
	Wrapper->SetField(TEXT("value"), Value.IsValid() ? Value : MakeShared<FJsonValueNull>());

	FString JsonText;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);
	return JsonText;
}

void AddNamedValueEntry(TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Name, TSharedPtr<FJsonValue> Value)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Name);
	Entry->SetField(TEXT("value"), Value.IsValid() ? Value : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

void AddChangedEntry(TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Name, TSharedPtr<FJsonValue> Before, TSharedPtr<FJsonValue> After)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Name);
	Entry->SetField(TEXT("before"), Before.IsValid() ? Before : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("after"), After.IsValid() ? After : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

void AddReasonEntry(TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Name, const FString& Code, const FString& Message)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Name);
	Entry->SetStringField(TEXT("code"), Code);
	Entry->SetStringField(TEXT("message"), Message);
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

FAssetDocumentResult ValidateGenericAssetDocument(TSharedPtr<FJsonObject> Document, const FString& NormalizedFilePath, bool bPreflightProperties)
{
	if (!Document.IsValid())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Validate requires a JSON document or sidecar file path"));
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	FString Target;
	auto MakeFailure = [&Target, &NormalizedFilePath](const FString& Message)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Message);
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	};

	double SchemaVersion = 0.0;
	if (!Document->TryGetNumberField(TEXT("SchemaVersion"), SchemaVersion) || SchemaVersion != 1.0)
	{
		return MakeFailure(TEXT("SchemaVersion must be 1"));
	}

	FString AssetType;
	if (!Document->TryGetStringField(TEXT("AssetType"), AssetType) || AssetType != TEXT("GenericAsset"))
	{
		return MakeFailure(TEXT("AssetType must be GenericAsset"));
	}

	if (!Document->TryGetStringField(TEXT("Target"), Target) || Target.IsEmpty())
	{
		return MakeFailure(TEXT("Target is required"));
	}
	Target = NormalizeValidateTarget(Target);

	FString Error;
	if (!ValidateApplyTarget(Target, Error))
	{
		return MakeFailure(Error);
	}

	if (!FAssetDocumentSidecar::ValidateTargetMatchesSidecar(NormalizedFilePath, Document, Error))
	{
		return MakeFailure(Error);
	}

	FString ClassName;
	if (!Document->TryGetStringField(TEXT("Class"), ClassName) || ClassName.IsEmpty())
	{
		return MakeFailure(TEXT("Class is required"));
	}

	FString ActionName;
	if (!Document->TryGetStringField(TEXT("Action"), ActionName) || ActionName.IsEmpty())
	{
		return MakeFailure(TEXT("Action is required"));
	}

	EAssetDocumentLifecycleAction Action;
	if (!FAssetDocumentLifecycle::TryParseAction(ActionName, Action, Error))
	{
		return MakeFailure(Error);
	}

	UClass* ResolvedClass = nullptr;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, Error))
	{
		return MakeFailure(Error);
	}

	TSharedPtr<FJsonObject> Properties;
	if (Document->HasField(TEXT("Properties")))
	{
		const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
		if (!Document->TryGetObjectField(TEXT("Properties"), PropertiesPtr) || !PropertiesPtr)
		{
			return MakeFailure(TEXT("Properties must be a JSON object"));
		}
		Properties = *PropertiesPtr;
	}

	if (bPreflightProperties)
	{
		FAssetDocumentPropertyApplyResult PreflightResult = FAssetDocumentPropertyAdapter::PreflightProperties(ResolvedClass, Properties);
		if (!PreflightResult.bSuccess)
		{
			FAssetDocumentResult Result = MakeFailure(PreflightResult.Message);
			Result.Diagnostics = PreflightResult.Diagnostics;
			return Result;
		}
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument is valid"));
	Result.Target = Target;
	Result.SidecarFilePath = NormalizedFilePath;
	Result.Payload = MakeShared<FJsonObject>();
	Result.Payload->SetStringField(TEXT("target"), Target);
	Result.Payload->SetStringField(TEXT("sidecar_file_path"), NormalizedFilePath);
	return Result;
}
}

FAssetDocumentResult FAssetDocumentResult::Success(const FString& InMessage)
{
	FAssetDocumentResult Result;
	Result.Status = EAssetDocumentResultStatus::Success;
	Result.Message = InMessage;
	return Result;
}

FAssetDocumentResult FAssetDocumentResult::Failure(const FString& InMessage)
{
	FAssetDocumentResult Result;
	Result.Status = EAssetDocumentResultStatus::Failed;
	Result.Message = InMessage;
	return Result;
}

TSharedPtr<FJsonObject> FAssetDocumentResult::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("success"), IsSuccess());
	Json->SetStringField(TEXT("message"), Message);
	Json->SetStringField(TEXT("target"), Target);
	Json->SetStringField(TEXT("asset_path"), AssetPath);
	Json->SetStringField(TEXT("sidecar_file_path"), SidecarFilePath);
	Json->SetBoolField(TEXT("saved_asset"), bSavedAsset);
	Json->SetBoolField(TEXT("wrote_sidecar"), bWroteSidecar);

	TArray<TSharedPtr<FJsonValue>> DiagnosticValues;
	for (const FAssetDocumentDiagnostic& Diagnostic : Diagnostics)
	{
		TSharedPtr<FJsonObject> DiagnosticJson = MakeShared<FJsonObject>();
		DiagnosticJson->SetStringField(TEXT("path"), Diagnostic.Path);
		DiagnosticJson->SetStringField(TEXT("code"), Diagnostic.Code);
		DiagnosticJson->SetStringField(TEXT("message"), Diagnostic.Message);
		DiagnosticValues.Add(MakeShared<FJsonValueObject>(DiagnosticJson));
	}
	Json->SetArrayField(TEXT("diagnostics"), DiagnosticValues);

	if (Payload.IsValid())
	{
		Json->SetObjectField(TEXT("payload"), Payload);
	}

	return Json;
}

FAssetDocumentResult FAssetDocumentService::Apply(const FAssetDocumentApplyRequest& Request)
{
	if (!Request.Document.IsValid())
	{
		return FAssetDocumentResult::Failure(TEXT("Apply requires a JSON document"));
	}

	double SchemaVersion = 0.0;
	if (!Request.Document->TryGetNumberField(TEXT("SchemaVersion"), SchemaVersion) || SchemaVersion != 1.0)
	{
		return FAssetDocumentResult::Failure(TEXT("SchemaVersion must be 1"));
	}

	FString AssetType;
	if (!Request.Document->TryGetStringField(TEXT("AssetType"), AssetType) || AssetType != TEXT("GenericAsset"))
	{
		return FAssetDocumentResult::Failure(TEXT("AssetType must be GenericAsset"));
	}

	FString Target;
	if (!Request.Document->TryGetStringField(TEXT("Target"), Target) || Target.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("Target is required"));
	}
	Target = NormalizeValidateTarget(Target);

	FString Error;
	if (!ValidateApplyTarget(Target, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Target;
		return Result;
	}

	FString ClassName;
	if (!Request.Document->TryGetStringField(TEXT("Class"), ClassName) || ClassName.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Class is required"));
		Result.Target = Target;
		return Result;
	}

	FString ActionName;
	if (!Request.Document->TryGetStringField(TEXT("Action"), ActionName) || ActionName.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Action is required"));
		Result.Target = Target;
		return Result;
	}

	EAssetDocumentLifecycleAction Action;
	if (!FAssetDocumentLifecycle::TryParseAction(ActionName, Action, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Target;
		return Result;
	}

	UClass* ResolvedClass = nullptr;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Target;
		return Result;
	}

	TSharedPtr<FJsonObject> Properties;
	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (Request.Document->TryGetObjectField(TEXT("Properties"), PropertiesPtr) && PropertiesPtr)
	{
		Properties = *PropertiesPtr;
	}

	FAssetDocumentPropertyApplyResult PreflightResult = FAssetDocumentPropertyAdapter::PreflightProperties(ResolvedClass, Properties);
	if (!PreflightResult.bSuccess)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(PreflightResult.Message);
		Result.Target = Target;
		Result.Diagnostics = PreflightResult.Diagnostics;
		return Result;
	}

	FAssetDocumentLifecycleResult LifecycleResult = FAssetDocumentLifecycle::CreateOrLoad(Target, ResolvedClass, Action);
	if (!LifecycleResult.Asset)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(LifecycleResult.Error);
		Result.Target = Target;
		Result.AssetPath = LifecycleResult.ObjectPath;
		return Result;
	}

	FAssetDocumentPropertyApplyResult PropertyResult = FAssetDocumentPropertyAdapter::ApplyProperties(LifecycleResult.Asset, Properties);
	if (!PropertyResult.bSuccess)
	{
		FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(PropertyResult.Message);
		Result.Target = Target;
		Result.AssetPath = LifecycleResult.ObjectPath;
		Result.Diagnostics = PropertyResult.Diagnostics;
		return Result;
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument applied"));
	Result.Target = Target;
	Result.AssetPath = LifecycleResult.ObjectPath;
	Result.Diagnostics = PropertyResult.Diagnostics;
	Result.bWroteSidecar = false;

	if (Request.bSaveAsset)
	{
		LifecycleResult.Asset->MarkPackageDirty();
		UPackage* Package = LifecycleResult.Asset->GetOutermost();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, LifecycleResult.Asset, *PackageFileName, SaveArgs);
		if (!bSaved)
		{
			FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
			Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to save asset package '%s'"), *Package->GetName()));
			Result.Target = Target;
			Result.AssetPath = LifecycleResult.ObjectPath;
			Result.Diagnostics = PropertyResult.Diagnostics;
			Result.bSavedAsset = false;
			Result.bWroteSidecar = false;
			return Result;
		}
		Result.bSavedAsset = true;
	}

	return Result;
}

FAssetDocumentResult FAssetDocumentService::ApplyFile(const FAssetDocumentApplyFileRequest& Request)
{
	const FString NormalizedFilePath = NormalizeValidateFilePath(Request.FilePath);
	if (NormalizedFilePath.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("ApplyFile requires a sidecar file path"));
	}

	TSharedPtr<FJsonObject> Document;
	FString Error;
	if (!FAssetDocumentSidecar::LoadJsonFile(NormalizedFilePath, Document, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	if (!FAssetDocumentSidecar::ValidateTargetMatchesSidecar(NormalizedFilePath, Document, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.SidecarFilePath = NormalizedFilePath;

		FString Target;
		if (Document.IsValid() && Document->TryGetStringField(TEXT("Target"), Target))
		{
			Result.Target = NormalizeValidateTarget(Target);
		}
		return Result;
	}

	FAssetDocumentApplyRequest ApplyRequest;
	ApplyRequest.Document = Document;
	ApplyRequest.bSaveAsset = Request.bSaveAsset;
	ApplyRequest.bWriteSidecar = false;

	FAssetDocumentResult Result = Apply(ApplyRequest);
	Result.SidecarFilePath = NormalizedFilePath;

	if (!Result.Payload.IsValid())
	{
		Result.Payload = MakeShared<FJsonObject>();
	}
	Result.Payload->SetStringField(TEXT("sidecar_file_path"), NormalizedFilePath);
	Result.Payload->SetBoolField(TEXT("triggered_by_watcher"), Request.bTriggeredByWatcher);

	return Result;
}

FAssetDocumentResult FAssetDocumentService::Inspect(const FAssetDocumentInspectRequest& Request) const
{
	if (Request.ClassOrAsset.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("Inspect requires a class name or asset path"));
	}

	UObject* Asset = nullptr;
	UClass* Class = nullptr;
	FString Target = Request.ClassOrAsset;
	FString Error;

	if (Request.ClassOrAsset.StartsWith(TEXT("/Game/")))
	{
		Asset = LoadAssetFromPackageOrObjectPath(Request.ClassOrAsset);
		if (!Asset)
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to load asset '%s'"), *Request.ClassOrAsset));
			Result.Target = Request.ClassOrAsset;
			Result.AssetPath = ToObjectPath(Request.ClassOrAsset);
			return Result;
		}
		Class = Asset->GetClass();
		Target = NormalizeValidateTarget(Request.ClassOrAsset);
	}
	else if (!FAssetDocumentClassResolver::ResolveClass(Request.ClassOrAsset, Class, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Request.ClassOrAsset;
		return Result;
	}

	TSharedPtr<FJsonObject> Payload = FAssetDocumentPropertyAdapter::InspectProperties(Class, Asset);
	if (!Payload.IsValid())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Failed to inspect reflected properties"));
		Result.Target = Target;
		if (Asset)
		{
			Result.AssetPath = Asset->GetPathName();
		}
		return Result;
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument inspected"));
	Result.Target = Target;
	if (Asset)
	{
		Result.AssetPath = Asset->GetPathName();
	}
	Result.Payload = Payload;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Extract(const FAssetDocumentExtractRequest& Request) const
{
	if (Request.AssetPath.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("Extract requires an asset path"));
	}

	UObject* Asset = LoadAssetFromPackageOrObjectPath(Request.AssetPath);
	const FString Target = NormalizeValidateTarget(Request.AssetPath);
	if (!Asset)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to load asset '%s'"), *Request.AssetPath));
		Result.Target = Target;
		Result.AssetPath = ToObjectPath(Request.AssetPath);
		return Result;
	}

	const bool bSkipDefaults = Request.bDiffOnly && !Request.bIncludeAllWritable;
	TSharedPtr<FJsonObject> Properties = FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(Asset, bSkipDefaults);
	if (!Properties.IsValid())
	{
		Properties = MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("AssetType"), TEXT("GenericAsset"));
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), Asset->GetClass()->GetPathName());
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Properties"), Properties);

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument extracted"));
	Result.Target = Target;
	Result.AssetPath = Asset->GetPathName();
	Result.Payload = Document;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Validate(const FAssetDocumentValidateRequest& Request) const
{
	const FString NormalizedFilePath = NormalizeValidateFilePath(Request.FilePath);
	TSharedPtr<FJsonObject> Document = Request.Document;

	if (!Document.IsValid() && NormalizedFilePath.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Validate requires a JSON document or sidecar file path"));
		return Result;
	}

	if (!Document.IsValid())
	{
		FString Error;
		if (!FAssetDocumentSidecar::LoadJsonFile(NormalizedFilePath, Document, Error))
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
			Result.SidecarFilePath = NormalizedFilePath;
			return Result;
		}
	}

	return ValidateGenericAssetDocument(Document, NormalizedFilePath, true);
}

FAssetDocumentResult FAssetDocumentService::Diff(const FAssetDocumentDiffRequest& Request) const
{
	const FString NormalizedFilePath = NormalizeValidateFilePath(Request.FilePath);
	TSharedPtr<FJsonObject> Document = Request.Document;

	if (!Document.IsValid() && NormalizedFilePath.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("Diff requires a JSON document or sidecar file path"));
	}

	if (!Document.IsValid())
	{
		FString Error;
		if (!FAssetDocumentSidecar::LoadJsonFile(NormalizedFilePath, Document, Error))
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
			Result.SidecarFilePath = NormalizedFilePath;
			return Result;
		}
	}

	const FAssetDocumentResult ValidateResult = ValidateGenericAssetDocument(Document, NormalizedFilePath, false);
	if (!ValidateResult.IsSuccess())
	{
		return ValidateResult;
	}

	FString Target;
	Document->TryGetStringField(TEXT("Target"), Target);
	Target = NormalizeValidateTarget(Target);
	if (!NormalizedFilePath.IsEmpty())
	{
		Target = FAssetDocumentSidecar::ResolveObjectPathFromSidecar(NormalizedFilePath);
	}

	UObject* Asset = LoadAssetFromPackageOrObjectPath(Target);
	if (!Asset)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to load asset '%s'"), *Target));
		Result.Target = Target;
		Result.AssetPath = ToObjectPath(Target);
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	TSharedPtr<FJsonObject> Properties;
	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (Document->TryGetObjectField(TEXT("Properties"), PropertiesPtr) && PropertiesPtr)
	{
		Properties = *PropertiesPtr;
	}
	if (!Properties.IsValid())
	{
		Properties = MakeShared<FJsonObject>();
	}

	TArray<TSharedPtr<FJsonValue>> Changed;
	TArray<TSharedPtr<FJsonValue>> Unchanged;
	TArray<TSharedPtr<FJsonValue>> Skipped;
	TArray<TSharedPtr<FJsonValue>> Failed;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
	{
		FProperty* Property = FindFProperty<FProperty>(Asset->GetClass(), *Pair.Key);
		if (!Property)
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("UnknownProperty"), FString::Printf(TEXT("Property '%s' does not exist"), *Pair.Key));
			continue;
		}

		const FString NonWritableReason = FAssetDocumentPropertyAdapter::GetNonWritableReason(Property);
		if (!NonWritableReason.IsEmpty())
		{
			AddReasonEntry(Skipped, Pair.Key, TEXT("NonWritable"), FString::Printf(TEXT("Property '%s' is not writable: %s"), *Pair.Key, *NonWritableReason));
			continue;
		}

		const void* CurrentValuePtr = Property->ContainerPtrToValuePtr<void>(Asset);
		TSharedPtr<FJsonValue> BeforeValue = FAssetDocumentPropertyAdapter::ExtractPropertyValue(Property, CurrentValuePtr);
		if (!BeforeValue.IsValid())
		{
			AddReasonEntry(Skipped, Pair.Key, TEXT("UnsupportedSerialization"), FString::Printf(TEXT("Property '%s' cannot be serialized"), *Pair.Key));
			continue;
		}

		UObject* PreviewAsset = DuplicateObject<UObject>(Asset, GetTransientPackage());
		if (!PreviewAsset)
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("DuplicateFailed"), FString::Printf(TEXT("Failed to duplicate asset while diffing '%s'"), *Pair.Key));
			continue;
		}

		TSharedPtr<FJsonObject> SingleProperty = MakeShared<FJsonObject>();
		SingleProperty->SetField(Pair.Key, Pair.Value);
		FAssetDocumentPropertyApplyResult ApplyResult = FAssetDocumentPropertyAdapter::ApplyProperties(PreviewAsset, SingleProperty);
		if (!ApplyResult.bSuccess)
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("TypeValidationFailed"), ApplyResult.Message);
			continue;
		}

		const void* AfterValuePtr = Property->ContainerPtrToValuePtr<void>(PreviewAsset);
		TSharedPtr<FJsonValue> AfterValue = FAssetDocumentPropertyAdapter::ExtractPropertyValue(Property, AfterValuePtr);
		if (!AfterValue.IsValid())
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("AfterSerializationFailed"), FString::Printf(TEXT("Property '%s' could not be serialized after applying document value"), *Pair.Key));
			continue;
		}

		if (JsonValueToComparableString(BeforeValue) == JsonValueToComparableString(AfterValue))
		{
			AddNamedValueEntry(Unchanged, Pair.Key, BeforeValue);
		}
		else
		{
			AddChangedEntry(Changed, Pair.Key, BeforeValue, AfterValue);
		}
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("changed"), Changed);
	Payload->SetArrayField(TEXT("unchanged"), Unchanged);
	Payload->SetArrayField(TEXT("skipped"), Skipped);
	Payload->SetArrayField(TEXT("failed"), Failed);

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument diffed"));
	Result.Target = Target;
	Result.AssetPath = Asset->GetPathName();
	Result.SidecarFilePath = NormalizedFilePath;
	Result.Payload = Payload;
	return Result;
}
