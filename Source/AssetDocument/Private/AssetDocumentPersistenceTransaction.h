// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;
class UPackage;

/**
 * Owns the on-disk portion of one AssetDocument apply.
 *
 * SavePackage writes into a unique /Temp-mounted staging directory first. The
 * staged package can therefore be loaded for diff without replacing the live
 * package, and canonical package files are only installed after verification.
 */
class FAssetDocumentPersistenceTransaction
{
public:
	FAssetDocumentPersistenceTransaction(
		const FString& InPackageName,
		const FString& InCanonicalPackageFilename);
	~FAssetDocumentPersistenceTransaction();

	bool StagePackage(UPackage* Package, UObject* Asset, FString& OutError);
	bool InstallStagedPackage(FString& OutError);
	bool RollbackInstalledPackage(TArray<FString>& OutErrors);
	void Commit();

	const FString& GetStagedHeaderFilename() const { return StagedHeaderFilename; }
	const FString& GetCanonicalHeaderFilename() const { return CanonicalPackageFilename; }
	bool HasInstalledFiles() const { return InstalledFileIndices.Num() > 0; }

private:
	struct FOutputFile
	{
		FString StagedFilename;
		FString CanonicalFilename;
		bool bOriginalExisted = false;
		TArray64<uint8> OriginalBytes;
	};

	bool DiscoverStagedOutputs(FString& OutError);
	bool CaptureOriginal(FOutputFile& Output, FString& OutError);
	void CleanupStaging();

	FString PackageName;
	FString CanonicalPackageFilename;
	FString StagingDirectory;
	FString StagedHeaderFilename;
	TArray<FOutputFile> Outputs;
	TArray<int32> InstalledFileIndices;
	bool bCommitted = false;
};
