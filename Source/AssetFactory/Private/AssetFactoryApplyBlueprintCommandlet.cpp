// Copyright ProjectRPG. All Rights Reserved.

#include "AssetFactoryApplyBlueprintCommandlet.h"

#include "Factory/AIBlueprintFactory.h"
#include "JSON/BlueprintJSONParser.h"

#include "AssetFactoryModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"

UAssetFactoryApplyBlueprintCommandlet::UAssetFactoryApplyBlueprintCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UAssetFactoryApplyBlueprintCommandlet::Main(const FString& Params)
{
	FString AssetPath;
	FString JsonPath;
	bool bMerge = true;
	bool bSaveAsset = true;
	if (!ParseParameters(Params, AssetPath, JsonPath, bMerge, bSaveAsset))
	{
		PrintHelp();
		return 1;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Failed to read JSON file: %s"), *JsonPath);
		return 2;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath));
	if (!Blueprint && !AssetPath.Contains(TEXT(".")))
	{
		const FString Name = FPackageName::GetShortName(AssetPath);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *Name);
		Blueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ObjectPath));
	}
	if (!Blueprint)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Blueprint not found: %s"), *AssetPath);
		return 3;
	}

	const FBlueprintParseResult ParseResult = UBlueprintJSONParser::ParseBlueprintJSON(JsonText);
	if (!ParseResult.bSuccess)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("ParseBlueprintJSON failed: %s"), *ParseResult.ErrorMessage);
		return 4;
	}

	FBlueprintGenerationResult ApplyResult = UAIBlueprintFactory::ModifyBlueprint(Blueprint, ParseResult.BlueprintData, bMerge);
	if (!ApplyResult.bSuccess)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("ModifyBlueprint failed: %s"), *ApplyResult.ErrorMessage);
		return 5;
	}

	Blueprint->MarkPackageDirty();

	bool bSaved = false;
	if (bSaveAsset)
	{
		UPackage* Package = Blueprint->GetOutermost();
		if (Package)
		{
			const FString PackageName = Package->GetName();
			const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			FSavePackageResultStruct SaveResult = UPackage::Save(Package, Blueprint, *PackageFileName, SaveArgs);
			bSaved = SaveResult.Result == ESavePackageResult::Success;
		}
	}

	TSharedRef<FJsonObject> Output = MakeShared<FJsonObject>();
	Output->SetBoolField(TEXT("success"), true);
	Output->SetStringField(TEXT("asset_path"), AssetPath);
	Output->SetBoolField(TEXT("saved"), bSaved);
	Output->SetBoolField(TEXT("merge"), bMerge);

	TArray<TSharedPtr<FJsonValue>> WarningValues;
	for (const FString& Warning : ApplyResult.Warnings)
	{
		WarningValues.Add(MakeShared<FJsonValueString>(Warning));
	}
	Output->SetArrayField(TEXT("warnings"), WarningValues);

	FString OutputJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
	FJsonSerializer::Serialize(Output, Writer);
	UE_LOG(LogAssetFactory, Display, TEXT("AF_APPLY_RESULT: %s"), *OutputJson);
	return 0;
}

bool UAssetFactoryApplyBlueprintCommandlet::ParseParameters(
	const FString& Params,
	FString& OutAssetPath,
	FString& OutJsonPath,
	bool& bOutMerge,
	bool& bOutSaveAsset)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	if (Switches.Contains(TEXT("help")) || Switches.Contains(TEXT("h")) || Switches.Contains(TEXT("?")))
	{
		return false;
	}

	if (ParamMap.Contains(TEXT("asset")))
	{
		OutAssetPath = ParamMap[TEXT("asset")];
	}
	else if (ParamMap.Contains(TEXT("asset_path")))
	{
		OutAssetPath = ParamMap[TEXT("asset_path")];
	}

	if (ParamMap.Contains(TEXT("json")))
	{
		OutJsonPath = ParamMap[TEXT("json")];
	}
	else if (ParamMap.Contains(TEXT("json_path")))
	{
		OutJsonPath = ParamMap[TEXT("json_path")];
	}

	if (OutAssetPath.IsEmpty() || OutJsonPath.IsEmpty())
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Missing required params: -asset and -json"));
		return false;
	}

	if (FPaths::IsRelative(OutJsonPath))
	{
		OutJsonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), OutJsonPath));
	}
	else
	{
		OutJsonPath = FPaths::ConvertRelativePathToFull(OutJsonPath);
	}

	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*OutJsonPath))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("JSON file not found: %s"), *OutJsonPath);
		return false;
	}

	const bool bNoMerge = Switches.Contains(TEXT("nomerge"));
	const bool bNoSave = Switches.Contains(TEXT("nosave"));
	bOutMerge = !bNoMerge;
	bOutSaveAsset = !bNoSave;
	return true;
}

void UAssetFactoryApplyBlueprintCommandlet::PrintHelp() const
{
	UE_LOG(LogAssetFactory, Display, TEXT("AssetFactoryApplyBlueprint commandlet"));
	UE_LOG(LogAssetFactory, Display, TEXT("Usage: UnrealEditor-Cmd.exe <Project> -run=AssetFactoryApplyBlueprint -asset=/Game/BP_X.BP_X -json=C:/temp/change.json [-nomerge] [-nosave]"));
}
