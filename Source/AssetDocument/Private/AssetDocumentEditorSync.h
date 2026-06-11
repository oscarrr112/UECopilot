// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class FAssetDocumentEditorSync
{
public:
	class FScopedSidecarWrite
	{
	public:
		explicit FScopedSidecarWrite(const TArray<FString>& InFilePaths);
		explicit FScopedSidecarWrite(const FString& InFilePath);
		~FScopedSidecarWrite();

		FScopedSidecarWrite(const FScopedSidecarWrite&) = delete;
		FScopedSidecarWrite& operator=(const FScopedSidecarWrite&) = delete;

	private:
		TArray<FString> FilePaths;
	};

	~FAssetDocumentEditorSync();

	void Register();
	void Unregister();

	static bool IsSidecarWriteSuppressed(const FString& FilePath);
	static FString InferDuplicateSourceObjectPath(const FString& NewObjectPath);

	bool SyncSidecarForRenameOrMove(const FString& OldObjectPath, const FString& NewObjectPath) const;
	bool SyncSidecarForDuplicate(const FString& SourceObjectPath, const FString& NewObjectPath) const;
	bool ArchiveSidecarForDeletedAsset(const FString& ObjectPath, FString* OutArchivePath = nullptr) const;

private:
	void HandleAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
	void HandleAssetAdded(const FAssetData& AssetData);
	void HandleAssetRemoved(const FAssetData& AssetData);

	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	bool bRegistered = false;
};
