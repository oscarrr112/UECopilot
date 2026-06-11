// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentJson.h"

#include "Misc/FileHelper.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool FAssetDocumentJson::LoadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJson, FString& OutError)
{
	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to read JSON file '%s'"), *FilePath);
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
	if (!FJsonSerializer::Deserialize(Reader, OutJson) || !OutJson.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to parse JSON file '%s'"), *FilePath);
		return false;
	}

	return true;
}

bool FAssetDocumentJson::WriteJsonFile(const FString& FilePath, const TSharedPtr<FJsonObject>& Json, FString& OutError)
{
	if (!Json.IsValid())
	{
		OutError = TEXT("Cannot write an invalid JSON object");
		return false;
	}

	FString Contents;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Contents);
	if (!FJsonSerializer::Serialize(Json.ToSharedRef(), Writer))
	{
		OutError = FString::Printf(TEXT("Failed to serialize JSON file '%s'"), *FilePath);
		return false;
	}

	if (!FFileHelper::SaveStringToFile(Contents, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Failed to write JSON file '%s'"), *FilePath);
		return false;
	}

	return true;
}
