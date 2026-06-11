// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFileWatcher.h"

#include "AssetDocumentEditorSync.h"
#include "AssetDocumentModule.h"
#include "AssetDocumentService.h"
#include "AssetDocumentSidecar.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "DirectoryWatcherModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
FString NormalizeFilePath(const FString& FilePath)
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

FString NormalizeWatchedFilePath(const FString& FilePath, const FString& WatchDirectory)
{
	FString NormalizedPath = FilePath;
	FPaths::NormalizeFilename(NormalizedPath);
	if (FPaths::IsRelative(NormalizedPath) && !WatchDirectory.IsEmpty())
	{
		NormalizedPath = FPaths::Combine(WatchDirectory, NormalizedPath);
	}
	return NormalizeFilePath(NormalizedPath);
}

FString NormalizeContentDir()
{
	FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FPaths::NormalizeDirectoryName(ContentDir);
	return ContentDir;
}

FString NormalizeTarget(const FString& Target)
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

void ApplySidecarNow(const TSharedPtr<FAssetDocumentService>& Service, const FString& FilePath)
{
	if (!Service.IsValid() || FAssetDocumentEditorSync::IsSidecarWriteSuppressed(FilePath))
	{
		return;
	}

	const FString Target = FAssetDocumentSidecar::ResolveObjectPathFromSidecar(FilePath);
	FString Warning;
	if (FAssetDocumentFileWatcher::ShouldSkipAutoApplyForDirtyPackage(Target, Warning))
	{
		UE_LOG(LogAssetDocument, Warning, TEXT("%s"), *Warning);
		return;
	}

	FAssetDocumentApplyFileRequest Request;
	Request.FilePath = FilePath;
	Request.bSaveAsset = true;
	Request.bAllowSidecarRewrite = false;
	Request.bTriggeredByWatcher = true;

	const FAssetDocumentResult Result = Service->ApplyFile(Request);
	if (Result.IsSuccess())
	{
		UE_LOG(LogAssetDocument, Log, TEXT("Auto-applied AssetDocument sidecar '%s'"), *FilePath);
	}
	else
	{
		UE_LOG(LogAssetDocument, Warning, TEXT("Failed to auto-apply AssetDocument sidecar '%s': %s"), *FilePath, *Result.Message);
	}
}
}

FAssetDocumentFileWatcher::FAssetDocumentFileWatcher(const TSharedRef<FAssetDocumentService>& InService)
	: Service(InService)
{
}

FAssetDocumentFileWatcher::~FAssetDocumentFileWatcher()
{
	Unregister();
}

void FAssetDocumentFileWatcher::Register()
{
	if (bRegistered)
	{
		return;
	}

	WatchDirectory = NormalizeContentDir();
	if (WatchDirectory.IsEmpty())
	{
		UE_LOG(LogAssetDocument, Warning, TEXT("AssetDocument file watcher could not resolve project Content directory"));
		return;
	}

	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();
	if (!DirectoryWatcher)
	{
		UE_LOG(LogAssetDocument, Warning, TEXT("DirectoryWatcher module returned no watcher"));
		return;
	}

	if (!DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
		WatchDirectory,
		IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FAssetDocumentFileWatcher::HandleDirectoryChanged),
		WatchHandle,
		0))
	{
		UE_LOG(LogAssetDocument, Warning, TEXT("Failed to register AssetDocument file watcher for '%s'"), *WatchDirectory);
		return;
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FAssetDocumentFileWatcher::Tick), 0.25f);
	bRegistered = true;
	UE_LOG(LogAssetDocument, Log, TEXT("AssetDocument file watcher registered for '%s'"), *WatchDirectory);
}

void FAssetDocumentFileWatcher::Unregister()
{
	if (!bRegistered)
	{
		return;
	}

	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	if (WatchHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("DirectoryWatcher")))
	{
		FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::GetModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		if (IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get())
		{
			DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(WatchDirectory, WatchHandle);
		}
		WatchHandle.Reset();
	}

	{
		FScopeLock Lock(&PendingFilesLock);
		PendingFiles.Reset();
	}

	bRegistered = false;
}

bool FAssetDocumentFileWatcher::IsAssetDocumentSidecarPath(const FString& FilePath)
{
	const FString NormalizedPath = NormalizeFilePath(FilePath);
	if (!NormalizedPath.EndsWith(TEXT(".assetdoc.json"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	FString ContentRoot = NormalizeContentDir();
	if (!ContentRoot.EndsWith(TEXT("/")))
	{
		ContentRoot += TEXT("/");
	}
	return NormalizedPath.StartsWith(ContentRoot, ESearchCase::IgnoreCase);
}

void FAssetDocumentFileWatcher::CollectAssetDocumentSidecarsUnderDirectory(const FString& Directory, TArray<FString>& OutFilePaths)
{
	OutFilePaths.Reset();

	FString NormalizedDirectory = NormalizeFilePath(Directory);
	FPaths::NormalizeDirectoryName(NormalizedDirectory);
	if (NormalizedDirectory.IsEmpty() || !IFileManager::Get().DirectoryExists(*NormalizedDirectory))
	{
		return;
	}

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFilesRecursive(FoundFiles, *NormalizedDirectory, TEXT("*.assetdoc.json"), true, false);

	for (FString& FoundFile : FoundFiles)
	{
		FoundFile = NormalizeFilePath(FoundFile);
		if (IsAssetDocumentSidecarPath(FoundFile))
		{
			OutFilePaths.Add(FoundFile);
		}
	}

	OutFilePaths.Sort();
}

bool FAssetDocumentFileWatcher::TryReadFileState(const FString& FilePath, FAssetDocumentWatchedFileState& OutState)
{
	const FString NormalizedPath = NormalizeFilePath(FilePath);
	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.FileExists(*NormalizedPath))
	{
		return false;
	}

	OutState.FileSize = FileManager.FileSize(*NormalizedPath);
	OutState.Timestamp = FileManager.GetTimeStamp(*NormalizedPath);
	return OutState.FileSize >= 0 && OutState.Timestamp != FDateTime::MinValue();
}

bool FAssetDocumentFileWatcher::IsStableFileState(const FString& FilePath, const FAssetDocumentWatchedFileState& PreviousState)
{
	FAssetDocumentWatchedFileState CurrentState;
	return TryReadFileState(FilePath, CurrentState) && CurrentState == PreviousState;
}

bool FAssetDocumentFileWatcher::ShouldSkipAutoApplyForDirtyPackage(const FString& Target, FString& OutWarning)
{
	const FString NormalizedTarget = NormalizeTarget(Target);
	if (NormalizedTarget.IsEmpty())
	{
		return false;
	}

	UPackage* Package = FindPackage(nullptr, *NormalizedTarget);
	if (!Package || !Package->IsDirty())
	{
		return false;
	}

	OutWarning = FString::Printf(
		TEXT("Skipped AssetDocument auto apply for '%s' because the target package is dirty; save or revert the asset before editing the sidecar."),
		*NormalizedTarget);
	return true;
}

void FAssetDocumentFileWatcher::HandleDirectoryChanged(const TArray<FFileChangeData>& FileChanges)
{
	for (const FFileChangeData& FileChange : FileChanges)
	{
		if (FileChange.Action == FFileChangeData::FCA_RescanRequired)
		{
			TArray<FString> SidecarPaths;
			CollectAssetDocumentSidecarsUnderDirectory(WatchDirectory, SidecarPaths);
			for (const FString& SidecarPath : SidecarPaths)
			{
				QueueFileChange(SidecarPath);
			}
			continue;
		}

		if (FileChange.Action != FFileChangeData::FCA_Added && FileChange.Action != FFileChangeData::FCA_Modified)
		{
			continue;
		}

		QueueFileChange(NormalizeWatchedFilePath(FileChange.Filename, WatchDirectory));
	}
}

void FAssetDocumentFileWatcher::QueueFileChange(const FString& FilePath)
{
	const FString NormalizedPath = NormalizeFilePath(FilePath);
	if (!IsAssetDocumentSidecarPath(NormalizedPath) || FAssetDocumentEditorSync::IsSidecarWriteSuppressed(NormalizedPath))
	{
		return;
	}

	FAssetDocumentWatchedFileState State;
	if (!TryReadFileState(NormalizedPath, State))
	{
		return;
	}

	FScopeLock Lock(&PendingFilesLock);
	FPendingSidecar& Pending = PendingFiles.FindOrAdd(NormalizedPath);
	Pending.LastState = State;
	Pending.LastObservedUtc = FDateTime::UtcNow();
}

bool FAssetDocumentFileWatcher::Tick(float DeltaSeconds)
{
	TArray<FString> ReadyFiles;
	const FDateTime Now = FDateTime::UtcNow();

	{
		FScopeLock Lock(&PendingFilesLock);
		for (TMap<FString, FPendingSidecar>::TIterator It(PendingFiles); It; ++It)
		{
			const FString& FilePath = It.Key();
			FPendingSidecar& Pending = It.Value();

			if (FAssetDocumentEditorSync::IsSidecarWriteSuppressed(FilePath))
			{
				It.RemoveCurrent();
				continue;
			}

			FAssetDocumentWatchedFileState CurrentState;
			if (!TryReadFileState(FilePath, CurrentState))
			{
				It.RemoveCurrent();
				continue;
			}

			if (CurrentState != Pending.LastState)
			{
				Pending.LastState = CurrentState;
				Pending.LastObservedUtc = Now;
				continue;
			}

			if (Now - Pending.LastObservedUtc < DebounceInterval)
			{
				continue;
			}

			ReadyFiles.Add(FilePath);
			It.RemoveCurrent();
		}
	}

	for (const FString& FilePath : ReadyFiles)
	{
		ApplySidecarOnGameThread(FilePath);
	}

	return bRegistered;
}

void FAssetDocumentFileWatcher::ApplySidecarOnGameThread(const FString& FilePath)
{
	if (!IsInGameThread())
	{
		TSharedPtr<FAssetDocumentService> ServiceCopy = Service;
		AsyncTask(ENamedThreads::GameThread, [ServiceCopy, FilePath]()
		{
			ApplySidecarNow(ServiceCopy, FilePath);
		});
		return;
	}

	ApplySidecarNow(Service, FilePath);
}
