// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDocumentTypes.h"

class ASSETDOCUMENT_API FAssetDocumentService
{
public:
	FAssetDocumentResult Apply(const FAssetDocumentApplyRequest& Request);
	FAssetDocumentResult ApplyFile(const FAssetDocumentApplyFileRequest& Request);
	FAssetDocumentResult Inspect(const FAssetDocumentInspectRequest& Request) const;
	FAssetDocumentResult Extract(const FAssetDocumentExtractRequest& Request) const;
	FAssetDocumentResult Validate(const FAssetDocumentValidateRequest& Request) const;
	FAssetDocumentResult Diff(const FAssetDocumentDiffRequest& Request) const;
};
