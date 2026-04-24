// Copyright ProjectRPG. All Rights Reserved.

#include "AssetFactoryNestedDefaultsVerificationCommandlet.h"

#include "AssetFactoryModule.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

UAssetFactoryNestedDefaultsVerificationCommandlet::UAssetFactoryNestedDefaultsVerificationCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UAssetFactoryNestedDefaultsVerificationCommandlet::Main(const FString& Params)
{
	FString AssetPath;
	if (!ParseParameters(Params, AssetPath))
	{
		PrintHelp();
		return 1;
	}

	FString ObjectPath = AssetPath;
	if (!ObjectPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPackageName::GetShortName(ObjectPath);
		ObjectPath = FString::Printf(TEXT("%s.%s"), *ObjectPath, *AssetName);
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ObjectPath));
	if (!Blueprint)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Blueprint not found: %s"), *ObjectPath);
		return 2;
	}

	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (!GeneratedClass)
	{
		GeneratedClass = LoadObject<UClass>(nullptr, *(ObjectPath + TEXT("_C")));
	}
	if (!GeneratedClass)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Generated class not found for Blueprint: %s"), *ObjectPath);
		return 3;
	}

	UObject* CDO = GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("CDO not found for class: %s"), *GeneratedClass->GetName());
		return 4;
	}

	FStructProperty* ConfigProperty = CastField<FStructProperty>(GeneratedClass->FindPropertyByName(TEXT("Config")));
	if (!ConfigProperty)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Config property not found on class: %s"), *GeneratedClass->GetName());
		return 5;
	}

	void* ConfigPtr = ConfigProperty->ContainerPtrToValuePtr<void>(CDO);
	FArrayProperty* EntriesProperty = CastField<FArrayProperty>(ConfigProperty->Struct->FindPropertyByName(TEXT("Entries")));
	if (!EntriesProperty)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Config.Entries property not found"));
		return 6;
	}

	void* EntriesPtr = EntriesProperty->ContainerPtrToValuePtr<void>(ConfigPtr);
	FScriptArrayHelper EntriesHelper(EntriesProperty, EntriesPtr);
	if (EntriesHelper.Num() != 1)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Expected Config.Entries.Num() == 1, got %d"), EntriesHelper.Num());
		return 7;
	}

	FStructProperty* EntryStructProperty = CastField<FStructProperty>(EntriesProperty->Inner);
	if (!EntryStructProperty)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Config.Entries inner property is not a struct"));
		return 8;
	}

	void* EntryPtr = EntriesHelper.GetRawPtr(0);
	FNameProperty* NameProperty = CastField<FNameProperty>(EntryStructProperty->Struct->FindPropertyByName(TEXT("Name")));
	FNumericProperty* ValueProperty = CastField<FNumericProperty>(EntryStructProperty->Struct->FindPropertyByName(TEXT("Value")));
	if (!NameProperty || !ValueProperty)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Config.Entries[0].Name or Value property not found"));
		return 9;
	}

	const FName ActualName = NameProperty->GetPropertyValue(NameProperty->ContainerPtrToValuePtr<void>(EntryPtr));
	const double ActualValue = ValueProperty->GetFloatingPointPropertyValue(ValueProperty->ContainerPtrToValuePtr<void>(EntryPtr));

	if (ActualName != FName(TEXT("TestEntry")))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Expected Config.Entries[0].Name == TestEntry, got %s"), *ActualName.ToString());
		return 10;
	}

	if (!FMath::IsNearlyEqual(ActualValue, 100.0, 0.001))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Expected Config.Entries[0].Value == 100.0, got %f"), ActualValue);
		return 11;
	}

	UE_LOG(LogAssetFactory, Display, TEXT("Nested Blueprint DefaultProperties persisted after reload"));
	return 0;
}

bool UAssetFactoryNestedDefaultsVerificationCommandlet::ParseParameters(const FString& Params, FString& OutAssetPath) const
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
	else if (Tokens.Num() > 0)
	{
		OutAssetPath = Tokens[0];
	}

	return !OutAssetPath.IsEmpty();
}

void UAssetFactoryNestedDefaultsVerificationCommandlet::PrintHelp() const
{
	UE_LOG(LogAssetFactory, Display, TEXT("AssetFactoryNestedDefaultsVerification commandlet"));
	UE_LOG(LogAssetFactory, Display, TEXT("Usage: UnrealEditor-Cmd.exe <Project> -run=AssetFactoryNestedDefaultsVerification -asset=/Game/Test/Blueprints/BP_AF_NestedDefaults"));
}
