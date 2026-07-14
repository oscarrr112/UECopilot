// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentSidecar.h"

#include "AssetDocumentJson.h"

#include "Misc/Paths.h"

namespace
{
FString SidecarNormalizeFilenameString(const FString& FilePath)
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

FString SidecarNormalizeContentDir()
{
	FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FPaths::NormalizeDirectoryName(ContentDir);
	return ContentDir;
}

FString SidecarNormalizeObjectPath(const FString& ObjectPath)
{
	FString NormalizedPath = ObjectPath;
	FPaths::NormalizeFilename(NormalizedPath);
	NormalizedPath.TrimStartAndEndInline();

	FString PackagePath;
	FString ObjectName;
	if (NormalizedPath.Split(TEXT("."), &PackagePath, &ObjectName))
	{
		NormalizedPath = PackagePath;
	}

	return NormalizedPath;
}
}

FString FAssetDocumentSidecar::ResolveObjectPathFromSidecar(const FString& FilePath)
{
	const FString NormalizedPath = SidecarNormalizeFilenameString(FilePath);
	if (NormalizedPath.IsEmpty())
	{
		return FString();
	}

	const FString ContentDir = SidecarNormalizeContentDir();
	FString ContentRoot = ContentDir;
	if (!ContentRoot.EndsWith(TEXT("/")))
	{
		ContentRoot += TEXT("/");
	}

	if (!NormalizedPath.StartsWith(ContentRoot, ESearchCase::IgnoreCase))
	{
		return FString();
	}

	FString RelativePath = NormalizedPath.RightChop(ContentRoot.Len());
	FPaths::NormalizeFilename(RelativePath);

	FString AssetName = FPaths::GetCleanFilename(RelativePath);
	if (!AssetName.RemoveFromEnd(TEXT(".assetdoc.json"), ESearchCase::IgnoreCase))
	{
		return FString();
	}

	const FString RelativeDirectory = FPaths::GetPath(RelativePath);
	FString ObjectPath = TEXT("/Game");
	if (!RelativeDirectory.IsEmpty())
	{
		ObjectPath /= RelativeDirectory;
	}
	ObjectPath /= AssetName;
	FPaths::NormalizeFilename(ObjectPath);
	return ObjectPath;
}

FString FAssetDocumentSidecar::ResolveSidecarPathFromObjectPath(const FString& ObjectPath)
{
	const FString NormalizedObjectPath = SidecarNormalizeObjectPath(ObjectPath);
	if (!NormalizedObjectPath.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive))
	{
		return FString();
	}

	FString RelativePath = NormalizedObjectPath.RightChop(UE_ARRAY_COUNT(TEXT("/Game/")) - 1);
	RelativePath += TEXT(".assetdoc.json");

	FString FilePath = FPaths::Combine(FPaths::ProjectContentDir(), RelativePath);
	FilePath = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::NormalizeFilename(FilePath);
	return FilePath;
}

bool FAssetDocumentSidecar::LoadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJson, FString& OutError)
{
	return FAssetDocumentJson::LoadJsonFile(SidecarNormalizeFilenameString(FilePath), OutJson, OutError);
}

bool FAssetDocumentSidecar::WriteJsonFile(const FString& FilePath, const TSharedPtr<FJsonObject>& Json, FString& OutError)
{
	return FAssetDocumentJson::WriteJsonFile(SidecarNormalizeFilenameString(FilePath), Json, OutError);
}

bool FAssetDocumentSidecar::ValidateTargetMatchesSidecar(const FString& FilePath, const TSharedPtr<FJsonObject>& Json, FString& OutError)
{
	if (FilePath.IsEmpty())
	{
		return true;
	}

	if (!Json.IsValid())
	{
		OutError = TEXT("Cannot validate sidecar Target for an invalid JSON document");
		return false;
	}

	FString Target;
	if (!Json->TryGetStringField(TEXT("Target"), Target) || Target.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Target is required for sidecar file '%s'"), *SidecarNormalizeFilenameString(FilePath));
		return false;
	}

	const FString ExpectedTarget = ResolveObjectPathFromSidecar(FilePath);
	if (ExpectedTarget.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Unable to resolve expected Target from sidecar file '%s'"), *SidecarNormalizeFilenameString(FilePath));
		return false;
	}

	const FString NormalizedTarget = SidecarNormalizeObjectPath(Target);
	if (!NormalizedTarget.Equals(ExpectedTarget, ESearchCase::CaseSensitive))
	{
		OutError = FString::Printf(
			TEXT("Target '%s' does not match sidecar file '%s'; expected '%s'"),
			*NormalizedTarget,
			*SidecarNormalizeFilenameString(FilePath),
			*ExpectedTarget);
		return false;
	}

	return true;
}
