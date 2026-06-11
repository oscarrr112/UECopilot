// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDocumentTypes.h"

class FJsonObject;

struct FAssetDocumentPropertyApplyResult
{
	bool bSuccess = false;
	FString Message;
	TArray<FAssetDocumentDiagnostic> Diagnostics;
};

class FAssetDocumentPropertyAdapter
{
public:
	static FAssetDocumentPropertyApplyResult ApplyProperties(UObject* Asset, TSharedPtr<FJsonObject> Properties);

private:
	static bool ApplyPropertiesDirect(UObject* Asset, TSharedPtr<FJsonObject> Properties, TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	static bool ApplySingleProperty(UObject* Asset, const FString& PropertyName, TSharedPtr<FJsonValue> JsonValue, TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	static bool TryGetTypedValue(TSharedPtr<FJsonValue> JsonValue, FString& OutType, TSharedPtr<FJsonValue>& OutValue, bool& bOutIsTyped, FString& OutError);
	static void AddDiagnostic(TArray<FAssetDocumentDiagnostic>& Diagnostics, const FString& PropertyName, const FString& Code, const FString& Message);
};
