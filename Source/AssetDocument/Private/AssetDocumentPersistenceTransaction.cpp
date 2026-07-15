// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentPersistenceTransaction.h"

#include "AssetDocumentAtomicFile.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "Misc/Paths.h"
#include "UObject/Linker.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"

namespace
{
const TArray<FString>& AssetDocumentPersistencePackageExtensions()
{
	static const TArray<FString> Extensions{
		TEXT("uasset"),
		TEXT("uexp"),
		TEXT("ubulk"),
		TEXT("uptnl")};
	return Extensions;
}

TSet<UObject*> AssetDocumentPersistenceCollectPackageObjects(UPackage* Package)
{
	TArray<UObject*> Objects;
	if (Package)
	{
		GetObjectsWithOuter(Package, Objects, true);
	}
	return TSet<UObject*>(Objects);
}
}

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
	OriginalPackageFlags = static_cast<uint32>(Package->GetPackageFlags());

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
	const FString CanonicalBaseName = FPaths::GetBaseFilename(CanonicalPackageFilename);
	TMap<FString, FString> StagedByExtension;
	IFileManager::Get().IterateDirectory(
		*StagingDirectory,
		[this, &CanonicalBaseName, &StagedByExtension](const TCHAR* Path, bool bIsDirectory)
		{
			FString StagedFilename(Path);
			FPaths::NormalizeFilename(StagedFilename);
			const FString Extension = FPaths::GetExtension(StagedFilename, false).ToLower();
			if (!bIsDirectory
				&& FPaths::GetBaseFilename(StagedFilename) == CanonicalBaseName
				&& AssetDocumentPersistencePackageExtensions().Contains(Extension))
			{
				StagedByExtension.Add(Extension, MoveTemp(StagedFilename));
			}
			return true;
		});

	for (const FString& Extension : AssetDocumentPersistencePackageExtensions())
	{
		FOutputFile& Output = Outputs.AddDefaulted_GetRef();
		Output.CanonicalFilename = FPaths::Combine(
			CanonicalDirectory,
			FString::Printf(TEXT("%s.%s"), *CanonicalBaseName, *Extension));
		FPaths::NormalizeFilename(Output.CanonicalFilename);
		if (const FString* StagedFilename = StagedByExtension.Find(Extension))
		{
			Output.StagedFilename = *StagedFilename;
		}
		else
		{
			Output.bDeleteCanonicalOnInstall = IFileManager::Get().FileExists(*Output.CanonicalFilename);
		}
	}

	if (Outputs.Num() == 0 || Outputs[0].StagedFilename != StagedHeaderFilename)
	{
		OutError = FString::Printf(
			TEXT("Persistence.Stage.Output: staged header '%s' was not produced"),
			*StagedHeaderFilename);
		return false;
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
		if (Output.StagedFilename.IsEmpty() && !Output.bDeleteCanonicalOnInstall)
		{
			continue;
		}
		if (!CaptureOriginal(Output, OutError))
		{
			return false;
		}

		if (Output.bDeleteCanonicalOnInstall)
		{
			InstalledFileIndices.Add(Index);
			if (!IFileManager::Get().Delete(*Output.CanonicalFilename, false, true))
			{
				OutError = FString::Printf(
					TEXT("Persistence.Install.DeleteStale: failed to delete stale package output '%s'"),
					*Output.CanonicalFilename);
				return false;
			}
			FString DirectoryFlushError;
			if (!FAssetDocumentAtomicFile::FlushParentDirectory(
					Output.CanonicalFilename,
					DirectoryFlushError))
			{
				OutError = FString::Printf(
					TEXT("Persistence.Install.DeleteStale: failed to durably delete '%s': %s"),
					*Output.CanonicalFilename,
					*DirectoryFlushError);
				return false;
			}
			continue;
		}

		TArray64<uint8> StagedBytes;
		if (!FFileHelper::LoadFileToArray(StagedBytes, *Output.StagedFilename))
		{
			OutError = FString::Printf(
				TEXT("Persistence.Install.ReadStage: failed to read staged package output '%s'"),
				*Output.StagedFilename);
			return false;
		}

		// Register before replacement so an internally rolled-back atomic failure is
		// still covered by the outer package transaction.
		InstalledFileIndices.Add(Index);
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
		else if (IFileManager::Get().FileExists(*Output.CanonicalFilename))
		{
			if (!IFileManager::Get().Delete(*Output.CanonicalFilename, false, true))
			{
				bSuccess = false;
				OutErrors.Add(FString::Printf(
					TEXT("Persistence.Rollback.Delete: failed to remove newly installed '%s'"),
					*Output.CanonicalFilename));
			}
			else
			{
				FString DirectoryFlushError;
				if (!FAssetDocumentAtomicFile::FlushParentDirectory(
						Output.CanonicalFilename,
						DirectoryFlushError))
				{
					bSuccess = false;
					OutErrors.Add(FString::Printf(
						TEXT("Persistence.Rollback.Delete: failed to durably remove '%s': %s"),
						*Output.CanonicalFilename,
						*DirectoryFlushError));
				}
			}
		}
	}
	InstalledFileIndices.Reset();
	return bSuccess;
}

bool FAssetDocumentPersistenceTransaction::RefreshCanonicalPackageMetadata(
	UPackage* Package,
	FString& OutError)
{
	OutError.Reset();
	if (!Package || Package->GetName() != PackageName)
	{
		OutError = TEXT("Persistence.Metadata.Validate: live package does not match the transaction package");
		return false;
	}

	const TSet<UObject*> ObjectsBefore = AssetDocumentPersistenceCollectPackageObjects(Package);
	const FPackagePath CanonicalPath = FPackagePath::FromLocalPath(CanonicalPackageFilename);
	ResetLoaders(Package);
	if (!GetPackageLinker(Package, CanonicalPath, LOAD_NoWarn | LOAD_Quiet, nullptr))
	{
		OutError = FString::Printf(
			TEXT("Persistence.Metadata.Linker: failed to read canonical summary from '%s'"),
			*CanonicalPackageFilename);
		return false;
	}
	Package->SetLoadedPath(CanonicalPath);
	Package->ClearPackageFlags(PKG_NewlyCreated);

	const TSet<UObject*> ObjectsAfter = AssetDocumentPersistenceCollectPackageObjects(Package);
	if (!ObjectsBefore.Includes(ObjectsAfter) || !ObjectsAfter.Includes(ObjectsBefore))
	{
		OutError = TEXT("Persistence.Metadata.Identity: canonical metadata refresh changed live package UObject identity");
		return false;
	}
	return true;
}

void FAssetDocumentPersistenceTransaction::BroadcastCanonicalPackageSaved(UPackage* Package) const
{
	if (!Package)
	{
		return;
	}
	FObjectSaveContextData SaveContext(Package, nullptr, *CanonicalPackageFilename, SAVE_None);
	SaveContext.OriginalPackageFlags = OriginalPackageFlags;
	SaveContext.Object = nullptr;
	SaveContext.ObjectSaveContextPhase = EObjectSaveContextPhase::PostSave;
	SaveContext.bSaveSucceeded = true;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	UPackage::PackageSavedEvent.Broadcast(CanonicalPackageFilename, Package);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
	UPackage::PackageSavedWithContextEvent.Broadcast(
		CanonicalPackageFilename,
		Package,
		FObjectPostSaveContext(SaveContext));
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
