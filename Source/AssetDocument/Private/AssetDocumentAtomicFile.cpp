// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentAtomicFile.h"

#include "Containers/StringConv.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Templates/Atomic.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_MAC || PLATFORM_LINUX
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#endif

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
TAtomic<uint8> GNextAtomicFileFailurePoint(
	static_cast<uint8>(EAssetDocumentAtomicFileFailurePoint::None));

bool ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint FailurePoint)
{
	uint8 Expected = static_cast<uint8>(FailurePoint);
	return GNextAtomicFileFailurePoint.CompareExchange(
		Expected,
		static_cast<uint8>(EAssetDocumentAtomicFileFailurePoint::None));
}
#endif

#if PLATFORM_WINDOWS
bool NormalizeWin32AbsolutePath(const FString& AbsolutePath, FString& OutWin32Path)
{
	OutWin32Path = AbsolutePath;
	FPaths::NormalizeFilename(OutWin32Path);
	if (!FPaths::CollapseRelativeDirectories(OutWin32Path, true))
	{
		return false;
	}
	FPaths::MakePlatformFilename(OutWin32Path);

	if (OutWin32Path.StartsWith(TEXT("\\\\?\\")))
	{
		return true;
	}
	if (OutWin32Path.StartsWith(TEXT("\\\\.\\")))
	{
		return false;
	}
	if (OutWin32Path.StartsWith(TEXT("\\\\")))
	{
		OutWin32Path = FString(TEXT("\\\\?\\UNC\\")) + OutWin32Path.Mid(2);
		return true;
	}
	if (OutWin32Path.Len() >= 3
		&& FChar::IsAlpha(OutWin32Path[0])
		&& OutWin32Path[1] == TEXT(':')
		&& OutWin32Path[2] == TEXT('\\'))
	{
		OutWin32Path = FString(TEXT("\\\\?\\")) + OutWin32Path;
		return true;
	}
	return false;
}

bool WriteAll(HANDLE Handle, const uint8* Data, int64 ByteCount, const FString& DestinationPath, FString& OutError)
{
	int64 Offset = 0;
	while (Offset < ByteCount)
	{
		const DWORD ChunkSize = static_cast<DWORD>(
			FMath::Min<int64>(ByteCount - Offset, static_cast<int64>(MAX_uint32)));
		DWORD WrittenByteCount = 0;
		if (::WriteFile(
				Handle,
				Data + Offset,
				ChunkSize,
				&WrittenByteCount,
				nullptr) == 0)
		{
			const uint32 WriteError = static_cast<uint32>(::GetLastError());
			OutError = FString::Printf(
				TEXT("AtomicFile.Write: WriteFile failed with error %u after %lld of %lld bytes for '%s'"),
				WriteError,
				Offset,
				ByteCount,
				*DestinationPath);
			return false;
		}
		if (WrittenByteCount == 0)
		{
			OutError = FString::Printf(
				TEXT("AtomicFile.Write: WriteFile made no progress after %lld of %lld bytes for '%s'"),
				Offset,
				ByteCount,
				*DestinationPath);
			return false;
		}
		Offset += static_cast<int64>(WrittenByteCount);
	}
	return true;
}
#elif PLATFORM_MAC || PLATFORM_LINUX
bool WriteAll(int32 FileDescriptor, const uint8* Data, int64 ByteCount, const FString& DestinationPath, FString& OutError)
{
	int64 Offset = 0;
	while (Offset < ByteCount)
	{
		const size_t ChunkSize = static_cast<size_t>(
			FMath::Min<int64>(ByteCount - Offset, static_cast<int64>(SSIZE_MAX)));
		const ssize_t WrittenByteCount = ::write(FileDescriptor, Data + Offset, ChunkSize);
		if (WrittenByteCount < 0)
		{
			const int32 WriteError = errno;
			if (WriteError == EINTR)
			{
				continue;
			}
			OutError = FString::Printf(
				TEXT("AtomicFile.Write: POSIX write failed with errno=%d after %lld of %lld bytes for '%s'"),
				WriteError,
				Offset,
				ByteCount,
				*DestinationPath);
			return false;
		}
		if (WrittenByteCount == 0)
		{
			OutError = FString::Printf(
				TEXT("AtomicFile.Write: POSIX write made no progress after %lld of %lld bytes for '%s'"),
				Offset,
				ByteCount,
				*DestinationPath);
			return false;
		}
		Offset += static_cast<int64>(WrittenByteCount);
	}
	return true;
}

bool FullSync(int32 FileDescriptor, int32& OutSyncError)
{
#if PLATFORM_MAC
	if (::fcntl(FileDescriptor, F_FULLFSYNC) == 0)
	{
		OutSyncError = 0;
		return true;
	}
#endif

	int32 SyncResult = 0;
	do
	{
		SyncResult = ::fsync(FileDescriptor);
	}
	while (SyncResult < 0 && errno == EINTR);
	OutSyncError = SyncResult == 0 ? 0 : errno;
	return SyncResult == 0;
}

bool AssetDocumentAtomicFileSyncParentDirectory(const FString& Directory, int32& OutSyncError)
{
	FTCHARToUTF8 NativeDirectory(*Directory);
	int32 DirectoryDescriptor = -1;
	do
	{
		DirectoryDescriptor = ::open(NativeDirectory.Get(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	}
	while (DirectoryDescriptor == -1 && errno == EINTR);
	if (DirectoryDescriptor == -1)
	{
		OutSyncError = errno;
		return false;
	}

	int32 SyncResult = 0;
	do
	{
		SyncResult = ::fsync(DirectoryDescriptor);
	}
	while (SyncResult < 0 && errno == EINTR);
	OutSyncError = SyncResult == 0 ? 0 : errno;
	int32 CloseResult = 0;
	do
	{
		CloseResult = ::close(DirectoryDescriptor);
	}
	while (CloseResult < 0 && errno == EINTR);
	if (SyncResult == 0 && CloseResult != 0)
	{
		OutSyncError = errno;
		return false;
	}
	return SyncResult == 0;
}
#endif
}

bool FAssetDocumentAtomicFile::WriteBytesAtomically(
	const FString& DestinationPath,
	TArrayView64<const uint8> Bytes,
	FString& OutError)
{
	OutError.Reset();
	if (DestinationPath.IsEmpty())
	{
		OutError = TEXT("AtomicFile.Validate: destination path is empty");
		return false;
	}

	FString NormalizedDestinationPath = FPaths::ConvertRelativePathToFull(DestinationPath);
	FPaths::NormalizeFilename(NormalizedDestinationPath);
	const FString DestinationDirectory = FPaths::GetPath(NormalizedDestinationPath);
	const FString DestinationFilename = FPaths::GetCleanFilename(NormalizedDestinationPath);
	const FString TemporaryPath = FPaths::Combine(
		DestinationDirectory,
		FString::Printf(
			TEXT(".%s.%s.tmp"),
			*DestinationFilename,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));

#if PLATFORM_WINDOWS
	FString NativeDestinationPath;
	FString NativeTemporaryPath;
	if (!NormalizeWin32AbsolutePath(NormalizedDestinationPath, NativeDestinationPath)
		|| !NormalizeWin32AbsolutePath(TemporaryPath, NativeTemporaryPath))
	{
		OutError = FString::Printf(
			TEXT("AtomicFile.Validate: path cannot be normalized to an absolute Win32 file path for '%s'"),
			*NormalizedDestinationPath);
		return false;
	}

	HANDLE TemporaryHandle = INVALID_HANDLE_VALUE;
	bool bOwnsTemporaryFile = false;
	bool bCommitted = false;
	auto CloseTemporaryHandle = [&TemporaryHandle]()
	{
		if (TemporaryHandle == INVALID_HANDLE_VALUE)
		{
			return static_cast<uint32>(ERROR_SUCCESS);
		}
		const HANDLE HandleToClose = TemporaryHandle;
		TemporaryHandle = INVALID_HANDLE_VALUE;
		if (::CloseHandle(HandleToClose) != 0)
		{
			return static_cast<uint32>(ERROR_SUCCESS);
		}
		return static_cast<uint32>(::GetLastError());
	};
	auto CleanupTemporaryFile = [&CloseTemporaryHandle, &bOwnsTemporaryFile, &NativeTemporaryPath, &TemporaryPath](FString* Error)
	{
		const uint32 CloseError = CloseTemporaryHandle();
		if (CloseError != ERROR_SUCCESS && Error != nullptr)
		{
			*Error += FString::Printf(
				TEXT("; AtomicFile.Cleanup: CloseHandle failed with error %u"),
				CloseError);
		}
		if (!bOwnsTemporaryFile)
		{
			return;
		}
		if (::DeleteFileW(*NativeTemporaryPath) == 0)
		{
			const uint32 DeleteError = static_cast<uint32>(::GetLastError());
			if (DeleteError != ERROR_FILE_NOT_FOUND
				&& DeleteError != ERROR_PATH_NOT_FOUND
				&& Error != nullptr)
			{
				*Error += FString::Printf(
					TEXT("; AtomicFile.Cleanup: DeleteFileW failed with error %u for temporary file '%s'"),
					DeleteError,
					*TemporaryPath);
			}
		}
		bOwnsTemporaryFile = false;
	};
	ON_SCOPE_EXIT
	{
		if (!bCommitted)
		{
			CleanupTemporaryFile(nullptr);
		}
	};
	auto FailWithCleanup = [&CleanupTemporaryFile, &OutError](FString&& Error)
	{
		CleanupTemporaryFile(&Error);
		OutError = MoveTemp(Error);
		return false;
	};

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint::Open))
	{
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Open: injected open failure for '%s'"),
			*NormalizedDestinationPath));
	}
#endif

	TemporaryHandle = ::CreateFileW(
		*NativeTemporaryPath,
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (TemporaryHandle == INVALID_HANDLE_VALUE)
	{
		const uint32 OpenError = static_cast<uint32>(::GetLastError());
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Open: CreateFileW(CREATE_NEW) failed with error %u for '%s'"),
			OpenError,
			*NormalizedDestinationPath));
	}
	bOwnsTemporaryFile = true;

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint::PartialWrite))
	{
		if (Bytes.Num() > 1)
		{
			const int64 PartialByteCount = FMath::Clamp<int64>(Bytes.Num() / 2, 1, Bytes.Num() - 1);
			FString PartialWriteError;
			if (!WriteAll(TemporaryHandle, Bytes.GetData(), PartialByteCount, NormalizedDestinationPath, PartialWriteError))
			{
				return FailWithCleanup(MoveTemp(PartialWriteError));
			}
		}
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Write: injected partial write failure for '%s'"),
			*NormalizedDestinationPath));
	}
#endif

	FString WriteError;
	if (!WriteAll(TemporaryHandle, Bytes.GetData(), Bytes.Num(), NormalizedDestinationPath, WriteError))
	{
		return FailWithCleanup(MoveTemp(WriteError));
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint::Flush))
	{
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Flush: injected full-flush failure for '%s'"),
			*NormalizedDestinationPath));
	}
#endif
	if (::FlushFileBuffers(TemporaryHandle) == 0)
	{
		const uint32 FlushError = static_cast<uint32>(::GetLastError());
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Flush: FlushFileBuffers failed with error %u for '%s'"),
			FlushError,
			*NormalizedDestinationPath));
	}
	const uint32 CloseError = CloseTemporaryHandle();
	if (CloseError != ERROR_SUCCESS)
	{
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Flush: CloseHandle failed with error %u for '%s'"),
			CloseError,
			*NormalizedDestinationPath));
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint::Rename))
	{
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Rename: injected rename failure for '%s'"),
			*NormalizedDestinationPath));
	}
#endif

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint::DirectoryFlush))
	{
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.DirectoryFlush: injected directory-flush failure for '%s'"),
			*NormalizedDestinationPath));
	}
#endif
	if (::MoveFileExW(
			*NativeTemporaryPath,
			*NativeDestinationPath,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
	{
		const uint32 RenameError = static_cast<uint32>(::GetLastError());
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Rename: MoveFileExW failed with error %u for '%s'"),
			RenameError,
			*NormalizedDestinationPath));
	}

	bOwnsTemporaryFile = false;
	bCommitted = true;
	return true;

#elif PLATFORM_MAC || PLATFORM_LINUX
	FTCHARToUTF8 NativeDestinationPath(*NormalizedDestinationPath);
	FTCHARToUTF8 NativeTemporaryPath(*TemporaryPath);
	const FString BackupPath = FPaths::Combine(
		DestinationDirectory,
		FString::Printf(
			TEXT(".%s.%s.bak"),
			*DestinationFilename,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	FTCHARToUTF8 NativeBackupPath(*BackupPath);
	int32 TemporaryFileDescriptor = -1;
	bool bOwnsTemporaryFile = false;
	bool bOwnsBackupFile = false;
	bool bCommitted = false;
	auto CloseTemporaryFile = [&TemporaryFileDescriptor]()
	{
		if (TemporaryFileDescriptor == -1)
		{
			return 0;
		}
		const int32 FileDescriptorToClose = TemporaryFileDescriptor;
		TemporaryFileDescriptor = -1;
		if (::close(FileDescriptorToClose) == 0)
		{
			return 0;
		}
		return errno;
	};
	auto CleanupTemporaryFile = [
		&CloseTemporaryFile,
		&bOwnsTemporaryFile,
		&NativeTemporaryPath,
		&TemporaryPath,
		&bOwnsBackupFile,
		&NativeBackupPath,
		&BackupPath](FString* Error)
	{
		if (bOwnsTemporaryFile)
		{
			if (::unlink(NativeTemporaryPath.Get()) != 0)
			{
				const int32 DeleteError = errno;
				if (DeleteError != ENOENT && Error != nullptr)
				{
					*Error += FString::Printf(
						TEXT("; AtomicFile.Cleanup: POSIX unlink failed with errno=%d for temporary file '%s'"),
						DeleteError,
						*TemporaryPath);
				}
			}
			bOwnsTemporaryFile = false;
		}
		const int32 CloseError = CloseTemporaryFile();
		if (CloseError != 0 && Error != nullptr)
		{
			*Error += FString::Printf(
				TEXT("; AtomicFile.Cleanup: POSIX close failed with errno=%d"),
				CloseError);
		}
		if (bOwnsBackupFile)
		{
			if (::unlink(NativeBackupPath.Get()) != 0 && errno != ENOENT && Error != nullptr)
			{
				*Error += FString::Printf(
					TEXT("; AtomicFile.Cleanup: POSIX unlink failed with errno=%d for backup file '%s'"),
					errno,
					*BackupPath);
			}
			bOwnsBackupFile = false;
		}
	};
	ON_SCOPE_EXIT
	{
		if (!bCommitted)
		{
			CleanupTemporaryFile(nullptr);
		}
	};
	auto FailWithCleanup = [&CleanupTemporaryFile, &OutError](FString&& Error)
	{
		CleanupTemporaryFile(&Error);
		OutError = MoveTemp(Error);
		return false;
	};

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint::Open))
	{
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Open: injected open failure for '%s'"),
			*NormalizedDestinationPath));
	}
#endif

	do
	{
		TemporaryFileDescriptor = ::open(
			NativeTemporaryPath.Get(),
			O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
			0666);
	}
	while (TemporaryFileDescriptor == -1 && errno == EINTR);
	if (TemporaryFileDescriptor == -1)
	{
		const int32 OpenError = errno;
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Open: POSIX open(O_CREAT|O_EXCL) failed with errno=%d for '%s'"),
			OpenError,
			*NormalizedDestinationPath));
	}
	bOwnsTemporaryFile = true;

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint::PartialWrite))
	{
		if (Bytes.Num() > 1)
		{
			const int64 PartialByteCount = FMath::Clamp<int64>(Bytes.Num() / 2, 1, Bytes.Num() - 1);
			FString PartialWriteError;
			if (!WriteAll(
					TemporaryFileDescriptor,
					Bytes.GetData(),
					PartialByteCount,
					NormalizedDestinationPath,
					PartialWriteError))
			{
				return FailWithCleanup(MoveTemp(PartialWriteError));
			}
		}
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Write: injected partial write failure for '%s'"),
			*NormalizedDestinationPath));
	}
#endif

	FString WriteError;
	if (!WriteAll(
			TemporaryFileDescriptor,
			Bytes.GetData(),
			Bytes.Num(),
			NormalizedDestinationPath,
			WriteError))
	{
		return FailWithCleanup(MoveTemp(WriteError));
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint::Flush))
	{
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Flush: injected full-flush failure for '%s'"),
			*NormalizedDestinationPath));
	}
#endif
	int32 SyncError = 0;
	if (!FullSync(TemporaryFileDescriptor, SyncError))
	{
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Flush: full sync failed with errno=%d for '%s'"),
			SyncError,
			*NormalizedDestinationPath));
	}
	const int32 CloseError = CloseTemporaryFile();
	if (CloseError != 0)
	{
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Flush: POSIX close failed with errno=%d for '%s'"),
			CloseError,
			*NormalizedDestinationPath));
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint::Rename))
	{
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Rename: injected rename failure for '%s'"),
			*NormalizedDestinationPath));
	}
#endif

	const bool bDestinationExisted = ::access(NativeDestinationPath.Get(), F_OK) == 0;
	if (bDestinationExisted)
	{
		if (::link(NativeDestinationPath.Get(), NativeBackupPath.Get()) != 0)
		{
			const int32 LinkError = errno;
			return FailWithCleanup(FString::Printf(
				TEXT("AtomicFile.Backup: POSIX link failed with errno=%d for '%s'"),
				LinkError,
				*NormalizedDestinationPath));
		}
		bOwnsBackupFile = true;
	}
	if (::rename(NativeTemporaryPath.Get(), NativeDestinationPath.Get()) != 0)
	{
		const int32 RenameError = errno;
		if (RenameError == EXDEV)
		{
			return FailWithCleanup(FString::Printf(
				TEXT("AtomicFile.Rename: cross-device rename is forbidden (errno=%d) for '%s'"),
				RenameError,
				*NormalizedDestinationPath));
		}
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.Rename: POSIX rename failed with errno=%d for '%s'"),
			RenameError,
			*NormalizedDestinationPath));
	}

	bOwnsTemporaryFile = false;
	auto RollbackCommittedRename = [&]()
	{
		bool bRestored = true;
		if (bOwnsBackupFile)
		{
			bRestored = ::rename(NativeBackupPath.Get(), NativeDestinationPath.Get()) == 0;
			if (bRestored)
			{
				bOwnsBackupFile = false;
			}
			else
			{
				// Preserve the hard-link backup for manual recovery. Cleanup must not
				// delete the only known-good inode after a rollback failure.
				bOwnsBackupFile = false;
			}
		}
		else
		{
			bRestored = ::unlink(NativeDestinationPath.Get()) == 0 || errno == ENOENT;
		}
		int32 RollbackSyncError = 0;
		return bRestored && AssetDocumentAtomicFileSyncParentDirectory(DestinationDirectory, RollbackSyncError);
	};

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeFailurePointForStage(EAssetDocumentAtomicFileFailurePoint::DirectoryFlush))
	{
		const bool bRestored = RollbackCommittedRename();
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.DirectoryFlush: injected directory-flush failure for '%s'%s"),
			*NormalizedDestinationPath,
			bRestored
				? TEXT("")
				: *FString::Printf(TEXT("; rollback failed; recovery backup may remain at '%s'"), *BackupPath)));
	}
#endif
	int32 DirectorySyncError = 0;
	if (!AssetDocumentAtomicFileSyncParentDirectory(DestinationDirectory, DirectorySyncError))
	{
		const bool bRestored = RollbackCommittedRename();
		return FailWithCleanup(FString::Printf(
			TEXT("AtomicFile.DirectoryFlush: parent directory sync failed with errno=%d for '%s'%s"),
			DirectorySyncError,
			*NormalizedDestinationPath,
			bRestored
				? TEXT("")
				: *FString::Printf(TEXT("; rollback failed; recovery backup may remain at '%s'"), *BackupPath)));
	}
	if (bOwnsBackupFile)
	{
		if (::unlink(NativeBackupPath.Get()) == 0 || errno == ENOENT)
		{
			bOwnsBackupFile = false;
			int32 CleanupSyncError = 0;
			AssetDocumentAtomicFileSyncParentDirectory(DestinationDirectory, CleanupSyncError);
		}
	}
	bCommitted = true;
	return true;
#else
	OutError = FString::Printf(
		TEXT("AtomicFile.Open: native atomic replace is unsupported on this platform for '%s'"),
		*NormalizedDestinationPath);
	return false;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void FAssetDocumentAtomicFile::FailNextWriteAtForTest(EAssetDocumentAtomicFileFailurePoint FailurePoint)
{
	GNextAtomicFileFailurePoint.Exchange(static_cast<uint8>(FailurePoint));
}

void FAssetDocumentAtomicFile::ResetFailureForTest()
{
	GNextAtomicFileFailurePoint.Exchange(
		static_cast<uint8>(EAssetDocumentAtomicFileFailurePoint::None));
}
#endif
