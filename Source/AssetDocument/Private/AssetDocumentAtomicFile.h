// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
enum class EAssetDocumentAtomicFileFailurePoint : uint8
{
	None,
	Open,
	PartialWrite,
	Flush,
	Rename,
	DirectoryFlush,
};
#endif

class FAssetDocumentAtomicFile
{
public:
	static bool WriteBytesAtomically(
		const FString& DestinationPath,
		TArrayView64<const uint8> Bytes,
		FString& OutError);

#if WITH_DEV_AUTOMATION_TESTS
	static void FailNextWriteAtForTest(EAssetDocumentAtomicFileFailurePoint FailurePoint);
	static void ResetFailureForTest();
#endif
};
