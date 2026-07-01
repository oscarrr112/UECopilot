// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UObject/SoftObjectPtr.h"

struct FAssetDocumentBlackboardKeySpec
{
	FName Name;
	FString Type;
	TSoftClassPtr<UBlackboardKeyType> KeyTypeClass;
	TSoftClassPtr<UObject> BaseClass;
	TSoftObjectPtr<UObject> Enum;
	FString Description;
	bool bInstanceSynced = false;
	TSharedPtr<FJsonObject> CanonicalJson;
};

struct FAssetDocumentBlackboardKeyLookupEntry
{
	FName Name;
	UClass* KeyTypeClass = nullptr;
	UClass* BaseClass = nullptr;
	UObject* EnumObject = nullptr;
	bool bInherited = false;
};

class FAssetDocumentBlackboardKeySchemaUtils
{
public:
	static FAssetDocumentCapabilityResult ParseKey(
		const TSharedRef<FJsonObject>& Json,
		const FString& Path,
		FAssetDocumentBlackboardKeySpec& OutSpec);

	static FAssetDocumentCapabilityResult ResolveKeyTypeClass(
		const FAssetDocumentBlackboardKeySpec& Spec,
		UClass*& OutClass,
		FString& OutCanonicalType);

	static FAssetDocumentCapabilityResult ResolveKeyTypeClass(
		const FAssetDocumentBlackboardKeySpec& Spec,
		const FString& Path,
		UClass*& OutClass,
		FString& OutCanonicalType);

	static FAssetDocumentCapabilityResult ValidateKeySpec(
		const FAssetDocumentBlackboardKeySpec& Spec,
		const FString& Path);

	static FAssetDocumentCapabilityResult ValidateUniqueLocalKeys(
		const TArray<FAssetDocumentBlackboardKeySpec>& Specs,
		const FString& KeysPath);

	static FAssetDocumentCapabilityResult BuildLookup(
		const UBlackboardData* Blackboard,
		TMap<FName, FAssetDocumentBlackboardKeyLookupEntry>& OutLookup);

	static bool FindKeyInLookup(
		const TMap<FName, FAssetDocumentBlackboardKeyLookupEntry>& Lookup,
		FName KeyName,
		FAssetDocumentBlackboardKeyLookupEntry& OutEntry);

	static TSharedRef<FJsonObject> ExtractKey(const FBlackboardEntry& Entry);
};
