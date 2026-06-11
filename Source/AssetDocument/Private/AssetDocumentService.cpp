// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "AssetDocumentClassResolver.h"
#include "AssetDocumentLifecycle.h"
#include "AssetDocumentPropertyAdapter.h"
#include "AssetDocumentSidecar.h"

#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
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
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetDocument ApplyFile is not implemented"));
	Result.Target = Request.FilePath;
	Result.SidecarFilePath = Request.FilePath;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Inspect(const FAssetDocumentInspectRequest& Request) const
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetDocument Inspect is not implemented"));
	Result.Target = Request.ClassOrAsset;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Extract(const FAssetDocumentExtractRequest& Request) const
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetDocument Extract is not implemented"));
	Result.Target = Request.AssetPath;
	Result.AssetPath = Request.AssetPath;
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

	double SchemaVersion = 0.0;
	if (!Document->TryGetNumberField(TEXT("SchemaVersion"), SchemaVersion) || SchemaVersion != 1.0)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("SchemaVersion must be 1"));
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	FString AssetType;
	if (!Document->TryGetStringField(TEXT("AssetType"), AssetType) || AssetType != TEXT("GenericAsset"))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetType must be GenericAsset"));
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	FString Error;
	if (!FAssetDocumentSidecar::ValidateTargetMatchesSidecar(NormalizedFilePath, Document, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	FString Target;
	Document->TryGetStringField(TEXT("Target"), Target);
	Target = NormalizeValidateTarget(Target);

	if (!NormalizedFilePath.IsEmpty())
	{
		Target = FAssetDocumentSidecar::ResolveObjectPathFromSidecar(NormalizedFilePath);
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument is valid"));
	Result.Target = Target;
	Result.SidecarFilePath = NormalizedFilePath;

	Result.Payload = MakeShared<FJsonObject>();
	Result.Payload->SetStringField(TEXT("target"), Target);
	Result.Payload->SetStringField(TEXT("sidecar_file_path"), NormalizedFilePath);
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Diff(const FAssetDocumentDiffRequest& Request) const
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetDocument Diff is not implemented"));
	Result.Target = Request.FilePath;
	Result.SidecarFilePath = Request.FilePath;
	return Result;
}
