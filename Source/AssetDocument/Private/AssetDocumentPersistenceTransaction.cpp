// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentPersistenceTransaction.h"

#include "AssetDocumentAtomicFile.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

FAssetDocumentPersistenceTransaction::FAssetDocumentPersistenceTransaction(
	const FString& InPackageName,
	const FString& InCanonicalPackageFilename)
	: PackageName(InPackageName)
	, CanonicalPackageFilename(FPaths::ConvertRelativePathToFull(InCanonicalPackageFilename))
{
	FPaths::NormalizeFilename(CanonicalPackageFilename);
	StagingDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("AssetDocument"),
		TEXT("PersistenceStaging"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FPaths::NormalizeFilename(StagingDirectory);
	StagedHeaderFilename = FPaths::Combine(
		StagingDirectory,
		FPaths::GetCleanFilename(CanonicalPackageFilename));
	FPaths::NormalizeFilename(StagedHeaderFilename);
}

FAssetDocumentPersistenceTransaction::~FAssetDocumentPersistenceTransaction()
{
	if (!bCommitted && InstalledFileIndices.Num() > 0)
	{
		TArray<FString> IgnoredRollbackErrors;
		RollbackInstalledPackage(IgnoredRollbackErrors);
	}
	CleanupStaging();
}

bool FAssetDocumentPersistenceTransaction::StagePackage(
	UPackage* Package,
	UObject* Asset,
	FString& OutError)
{
	OutError.Reset();
	if (!Package || !Asset)
	{
		OutError = TEXT("Persistence.Stage.Validate: package and asset are required");
		return false;
	}
	if (Package->GetName() != PackageName)
	{
		OutError = FString::Printf(
			TEXT("Persistence.Stage.Validate: package '%s' does not match expected package '%s'"),
			*Package->GetName(),
			*PackageName);
		return false;
	}

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.MakeDirectory(*StagingDirectory, true))
	{
		OutError = FString::Printf(
			TEXT("Persistence.Stage.Directory: failed to create staging directory '%s'"),
			*StagingDirectory);
		return false;
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	// Staging is an implementation detail: it must neither change LoadedPath nor
	// clear dirty state before the whole AssetDocument transaction commits.
	SaveArgs.SaveFlags = SAVE_FromAutosave | SAVE_KeepDirty;
	SaveArgs.bSlowTask = false;
	if (!UPackage::SavePackage(Package, Asset, *StagedHeaderFilename, SaveArgs))
	{
		OutError = FString::Printf(
			TEXT("Persistence.Stage.SavePackage: failed to serialize package '%s' to staging"),
			*PackageName);
		return false;
	}

	return DiscoverStagedOutputs(OutError);
}

bool FAssetDocumentPersistenceTransaction::DiscoverStagedOutputs(FString& OutError)
{
	OutError.Reset();
	Outputs.Reset();
	const FString CanonicalDirectory = FPaths::GetPath(CanonicalPackageFilename);
	IFileManager::Get().IterateDirectory(
		*StagingDirectory,
		[this, &CanonicalDirectory](const TCHAR* Path, bool bIsDirectory)
		{
			if (!bIsDirectory)
			{
				FOutputFile& Output = Outputs.AddDefaulted_GetRef();
				Output.StagedFilename = Path;
				FPaths::NormalizeFilename(Output.StagedFilename);
				Output.CanonicalFilename = FPaths::Combine(
					CanonicalDirectory,
					FPaths::GetCleanFilename(Output.StagedFilename));
				FPaths::NormalizeFilename(Output.CanonicalFilename);
			}
			return true;
		});

	Outputs.Sort([](const FOutputFile& A, const FOutputFile& B)
	{
		return A.StagedFilename < B.StagedFilename;
	});
	const int32 HeaderIndex = Outputs.IndexOfByPredicate([this](const FOutputFile& Output)
	{
		return Output.StagedFilename == StagedHeaderFilename;
	});
	if (HeaderIndex == INDEX_NONE)
	{
		OutError = FString::Printf(
			TEXT("Persistence.Stage.Output: staged header '%s' was not produced"),
			*StagedHeaderFilename);
		return false;
	}
	if (HeaderIndex != 0)
	{
		Outputs.Swap(0, HeaderIndex);
	}
	return true;
}

bool FAssetDocumentPersistenceTransaction::CaptureOriginal(
	FOutputFile& Output,
	FString& OutError)
{
	OutError.Reset();
	IFileManager& FileManager = IFileManager::Get();
	Output.bOriginalExisted = FileManager.FileExists(*Output.CanonicalFilename);
	Output.OriginalBytes.Reset();
	if (Output.bOriginalExisted
		&& !FFileHelper::LoadFileToArray(Output.OriginalBytes, *Output.CanonicalFilename))
	{
		OutError = FString::Printf(
			TEXT("Persistence.Backup.Read: failed to read existing package output '%s'"),
			*Output.CanonicalFilename);
		return false;
	}
	return true;
}

bool FAssetDocumentPersistenceTransaction::InstallStagedPackage(FString& OutError)
{
	OutError.Reset();
	if (Outputs.Num() == 0)
	{
		OutError = TEXT("Persistence.Install.Validate: no staged package outputs are available");
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(CanonicalPackageFilename), true))
	{
		OutError = FString::Printf(
			TEXT("Persistence.Install.Directory: failed to create canonical directory for '%s'"),
			*CanonicalPackageFilename);
		return false;
	}

	InstalledFileIndices.Reset();
	for (int32 Index = 0; Index < Outputs.Num(); ++Index)
	{
		FOutputFile& Output = Outputs[Index];
		if (!CaptureOriginal(Output, OutError))
		{
			return false;
		}

		TArray64<uint8> StagedBytes;
		if (!FFileHelper::LoadFileToArray(StagedBytes, *Output.StagedFilename))
		{
			OutError = FString::Printf(
				TEXT("Persistence.Install.ReadStage: failed to read staged package output '%s'"),
				*Output.StagedFilename);
			return false;
		}

		FString AtomicError;
		if (!FAssetDocumentAtomicFile::WriteBytesAtomically(
				Output.CanonicalFilename,
				StagedBytes,
				AtomicError))
		{
			OutError = FString::Printf(
				TEXT("Persistence.Install.AtomicReplace: failed to install '%s': %s"),
				*Output.CanonicalFilename,
				*AtomicError);
			return false;
		}
		InstalledFileIndices.Add(Index);
	}
	return true;
}

bool FAssetDocumentPersistenceTransaction::RollbackInstalledPackage(
	TArray<FString>& OutErrors)
{
	bool bSuccess = true;
	for (int32 InstalledIndex = InstalledFileIndices.Num() - 1; InstalledIndex >= 0; --InstalledIndex)
	{
		const FOutputFile& Output = Outputs[InstalledFileIndices[InstalledIndex]];
		if (Output.bOriginalExisted)
		{
			FString AtomicError;
			if (!FAssetDocumentAtomicFile::WriteBytesAtomically(
					Output.CanonicalFilename,
					Output.OriginalBytes,
					AtomicError))
			{
				bSuccess = false;
				OutErrors.Add(FString::Printf(
					TEXT("Persistence.Rollback.Restore: failed to restore '%s': %s"),
					*Output.CanonicalFilename,
					*AtomicError));
			}
		}
		else if (IFileManager::Get().FileExists(*Output.CanonicalFilename)
			&& !IFileManager::Get().Delete(*Output.CanonicalFilename, false, true))
		{
			bSuccess = false;
			OutErrors.Add(FString::Printf(
				TEXT("Persistence.Rollback.Delete: failed to remove newly installed '%s'"),
				*Output.CanonicalFilename));
		}
	}
	InstalledFileIndices.Reset();
	return bSuccess;
}

void FAssetDocumentPersistenceTransaction::Commit()
{
	bCommitted = true;
	InstalledFileIndices.Reset();
	for (FOutputFile& Output : Outputs)
	{
		Output.OriginalBytes.Reset();
	}
	CleanupStaging();
}

void FAssetDocumentPersistenceTransaction::CleanupStaging()
{
	if (!StagingDirectory.IsEmpty())
	{
		IFileManager::Get().DeleteDirectory(*StagingDirectory, false, true);
	}
}
