// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IDirectoryWatcher.h"

class FAssetDocumentService;

struct FAssetDocumentWatchedFileState
{
	int64 FileSize = INDEX_NONE;
	FDateTime Timestamp;

	bool operator==(const FAssetDocumentWatchedFileState& Other) const
	{
		return FileSize == Other.FileSize && Timestamp == Other.Timestamp;
	}

	bool operator!=(const FAssetDocumentWatchedFileState& Other) const
	{
		return !(*this == Other);
	}
};

class FAssetDocumentFileWatcher
{
public:
	explicit FAssetDocumentFileWatcher(const TSharedRef<FAssetDocumentService>& InService);
	~FAssetDocumentFileWatcher();

	void Register();
	void Unregister();

	static bool IsAssetDocumentSidecarPath(const FString& FilePath);
	static bool TryReadFileState(const FString& FilePath, FAssetDocumentWatchedFileState& OutState);
	static bool IsStableFileState(const FString& FilePath, const FAssetDocumentWatchedFileState& PreviousState);
	static bool ShouldSkipAutoApplyForDirtyPackage(const FString& Target, FString& OutWarning);

private:
	struct FPendingSidecar
	{
		FAssetDocumentWatchedFileState LastState;
		FDateTime LastObservedUtc;
	};

	void HandleDirectoryChanged(const TArray<FFileChangeData>& FileChanges);
	void QueueFileChange(const FString& FilePath);
	bool Tick(float DeltaSeconds);
	void ApplySidecarOnGameThread(const FString& FilePath);

	TSharedPtr<FAssetDocumentService> Service;
	FString WatchDirectory;
	FDelegateHandle WatchHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	TMap<FString, FPendingSidecar> PendingFiles;
	FCriticalSection PendingFilesLock;
	bool bRegistered = false;
	FTimespan DebounceInterval = FTimespan::FromMilliseconds(500);
};
