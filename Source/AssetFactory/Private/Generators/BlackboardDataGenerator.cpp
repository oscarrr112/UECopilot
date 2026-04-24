// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/BlackboardDataGenerator.h"

#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

FGenerationResult FBlackboardDataGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	if (!Config.IsValid())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Invalid configuration object"));
	}

	const bool bExists = DoesAssetExist(Path, Name);

	if (Action == EGenerationAction::Create && bExists)
	{
		return FGenerationResult::MakeSkipped(GetAssetType(), Name, Path, TEXT("Asset already exists"));
	}

	if (Action == EGenerationAction::Update && !bExists)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Asset does not exist for update"));
	}

	const auto LoadBlackboardAsset = [](const FString& AssetPath) -> UBlackboardData*
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}

		FString NormalizedPath = AssetPath;
		if (!NormalizedPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPaths::GetBaseFilename(NormalizedPath);
			if (!AssetName.IsEmpty())
			{
				NormalizedPath += TEXT(".") + AssetName;
			}
		}

		return LoadObject<UBlackboardData>(nullptr, *NormalizedPath);
	};

	const auto ResolveClassReference = [](const FString& ClassRef) -> UClass*
	{
		if (ClassRef.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* LoadedClass = LoadClass<UObject>(nullptr, *ClassRef))
		{
			return LoadedClass;
		}

		return StaticLoadClass(UObject::StaticClass(), nullptr, *ClassRef);
	};

	const auto ResolveEnumReference = [](const FString& EnumRef) -> UEnum*
	{
		if (EnumRef.IsEmpty())
		{
			return nullptr;
		}

		if (UEnum* LoadedEnum = LoadObject<UEnum>(nullptr, *EnumRef))
		{
			return LoadedEnum;
		}

		FString SearchName = EnumRef;
		if (!SearchName.StartsWith(TEXT("E")))
		{
			SearchName = TEXT("E") + SearchName;
		}

		for (TObjectIterator<UEnum> It; It; ++It)
		{
			if (It->GetName() == EnumRef || It->GetName() == SearchName || It->GetPathName() == EnumRef)
			{
				return *It;
			}
		}

		return nullptr;
	};

	UBlackboardData* Blackboard = nullptr;
	UPackage* Package = nullptr;

	if (bExists)
	{
		Blackboard = Cast<UBlackboardData>(LoadExistingAsset(Path, Name));
		if (!Blackboard)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing blackboard asset"));
		}

		Package = Blackboard->GetOutermost();
	}
	else
	{
		FString FullPath = Path / Name;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/") + FullPath;
		}

		Package = CreatePackage(*FullPath);
		if (!Package)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create package"));
		}

		Blackboard = NewObject<UBlackboardData>(Package, UBlackboardData::StaticClass(), *Name, RF_Public | RF_Standalone);
		if (!Blackboard)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create blackboard asset"));
		}

		FAssetRegistryModule::AssetCreated(Blackboard);
	}

	UBlackboardData* ParentBlackboard = nullptr;
	FString ParentPath;
	if (Config->TryGetStringField(TEXT("Parent"), ParentPath) && !ParentPath.IsEmpty())
	{
		ParentBlackboard = LoadBlackboardAsset(ParentPath);
		if (!ParentBlackboard)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
				FString::Printf(TEXT("Failed to load Parent blackboard '%s'"), *ParentPath));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
	if (!Config->TryGetArrayField(TEXT("Keys"), KeysArray) || !KeysArray || KeysArray->Num() == 0)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("'Keys' must be a non-empty array"));
	}

	TArray<FBlackboardEntry> PendingKeys;
	PendingKeys.Reserve(KeysArray->Num());

	for (const TSharedPtr<FJsonValue>& KeyValue : *KeysArray)
	{
		const TSharedPtr<FJsonObject> KeyObject = KeyValue.IsValid() ? KeyValue->AsObject() : nullptr;
		if (!KeyObject.IsValid())
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Encountered invalid key configuration"));
		}

		FBlackboardEntry Entry = BuildEntry(KeyObject);

		FString TypeName;
		KeyObject->TryGetStringField(TEXT("Type"), TypeName);

		UClass* KeyTypeClass = ResolveKeyTypeClass(TypeName);
		if (!KeyTypeClass)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
				FString::Printf(TEXT("Unsupported key type '%s'"), *TypeName));
		}

		UBlackboardKeyType* KeyType = NewObject<UBlackboardKeyType>(Blackboard, KeyTypeClass, NAME_None, RF_Transactional);
		if (!KeyType)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
				FString::Printf(TEXT("Failed to create key type '%s'"), *TypeName));
		}

		if (UBlackboardKeyType_Object* ObjectKeyType = Cast<UBlackboardKeyType_Object>(KeyType))
		{
			FString BaseClassName;
			KeyObject->TryGetStringField(TEXT("BaseClass"), BaseClassName);
			ObjectKeyType->BaseClass = ResolveClassReference(BaseClassName);
			if (!ObjectKeyType->BaseClass)
			{
				return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
					FString::Printf(TEXT("Failed to load BaseClass '%s' for Object key '%s'"), *BaseClassName, *Entry.EntryName.ToString()));
			}
		}
		else if (UBlackboardKeyType_Class* ClassKeyType = Cast<UBlackboardKeyType_Class>(KeyType))
		{
			FString BaseClassName;
			KeyObject->TryGetStringField(TEXT("BaseClass"), BaseClassName);
			ClassKeyType->BaseClass = ResolveClassReference(BaseClassName);
			if (!ClassKeyType->BaseClass)
			{
				return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
					FString::Printf(TEXT("Failed to load BaseClass '%s' for Class key '%s'"), *BaseClassName, *Entry.EntryName.ToString()));
			}
		}
		else if (UBlackboardKeyType_Enum* EnumKeyType = Cast<UBlackboardKeyType_Enum>(KeyType))
		{
			FString EnumName;
			KeyObject->TryGetStringField(TEXT("EnumName"), EnumName);
			EnumKeyType->EnumType = ResolveEnumReference(EnumName);
			EnumKeyType->EnumName = EnumName;
			if (!EnumKeyType->EnumType)
			{
				return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
					FString::Printf(TEXT("Failed to resolve EnumName '%s' for Enum key '%s'"), *EnumName, *Entry.EntryName.ToString()));
			}
		}

		Entry.KeyType = KeyType;
		PendingKeys.Add(MoveTemp(Entry));
	}

	Blackboard->Modify();
	Blackboard->Parent = ParentBlackboard;
	Blackboard->Keys = MoveTemp(PendingKeys);

	Blackboard->MarkPackageDirty();

	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

	if (!UPackage::SavePackage(Package, Blackboard, *PackageFileName, SaveArgs))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to save blackboard package"));
	}

	if (bExists)
	{
		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, Blackboard);
	}

	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, Blackboard);
}

TOptional<FString> FBlackboardDataGenerator::ValidateConfig(
	TSharedPtr<FJsonObject> Config,
	EGenerationAction Action) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	const auto LoadBlackboardAsset = [](const FString& AssetPath) -> UBlackboardData*
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}

		FString NormalizedPath = AssetPath;
		if (!NormalizedPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPaths::GetBaseFilename(NormalizedPath);
			if (!AssetName.IsEmpty())
			{
				NormalizedPath += TEXT(".") + AssetName;
			}
		}

		return LoadObject<UBlackboardData>(nullptr, *NormalizedPath);
	};

	const auto ResolveClassReference = [](const FString& ClassRef) -> UClass*
	{
		if (ClassRef.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* LoadedClass = LoadClass<UObject>(nullptr, *ClassRef))
		{
			return LoadedClass;
		}

		return StaticLoadClass(UObject::StaticClass(), nullptr, *ClassRef);
	};

	const auto ResolveEnumReference = [](const FString& EnumRef) -> UEnum*
	{
		if (EnumRef.IsEmpty())
		{
			return nullptr;
		}

		if (UEnum* LoadedEnum = LoadObject<UEnum>(nullptr, *EnumRef))
		{
			return LoadedEnum;
		}

		FString SearchName = EnumRef;
		if (!SearchName.StartsWith(TEXT("E")))
		{
			SearchName = TEXT("E") + SearchName;
		}

		for (TObjectIterator<UEnum> It; It; ++It)
		{
			if (It->GetName() == EnumRef || It->GetName() == SearchName || It->GetPathName() == EnumRef)
			{
				return *It;
			}
		}

		return nullptr;
	};

	FString ParentPath;
	if (Config->TryGetStringField(TEXT("Parent"), ParentPath) && !ParentPath.IsEmpty())
	{
		if (!LoadBlackboardAsset(ParentPath))
		{
			return FString::Printf(TEXT("Failed to load Parent blackboard '%s'"), *ParentPath);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
	if (!Config->TryGetArrayField(TEXT("Keys"), KeysArray) || !KeysArray || KeysArray->Num() == 0)
	{
		return FString(TEXT("'Keys' must be a non-empty array"));
	}

	TSet<FString> SeenNames;

	for (int32 Index = 0; Index < KeysArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& KeyValue = (*KeysArray)[Index];
		if (!KeyValue.IsValid() || KeyValue->Type != EJson::Object)
		{
			return FString::Printf(TEXT("Keys[%d] must be an object"), Index);
		}

		const TSharedPtr<FJsonObject> KeyObject = KeyValue->AsObject();
		if (!KeyObject.IsValid())
		{
			return FString::Printf(TEXT("Keys[%d] must be an object"), Index);
		}

		FString KeyName;
		if (!KeyObject->TryGetStringField(TEXT("Name"), KeyName) || KeyName.IsEmpty())
		{
			return FString::Printf(TEXT("Keys[%d].Name must be a non-empty string"), Index);
		}

		if (SeenNames.Contains(KeyName))
		{
			return FString::Printf(TEXT("Duplicate blackboard key name '%s'"), *KeyName);
		}
		SeenNames.Add(KeyName);

		FString TypeName;
		if (!KeyObject->TryGetStringField(TEXT("Type"), TypeName) || TypeName.IsEmpty())
		{
			return FString::Printf(TEXT("Keys[%d] ('%s') is missing a valid Type"), Index, *KeyName);
		}

		if (!ResolveKeyTypeClass(TypeName))
		{
			return FString::Printf(TEXT("Keys[%d] ('%s') has unsupported Type '%s'"), Index, *KeyName, *TypeName);
		}

		if (TypeName.Equals(TEXT("Object"), ESearchCase::IgnoreCase) ||
			TypeName.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
		{
			FString BaseClassName;
			if (!KeyObject->TryGetStringField(TEXT("BaseClass"), BaseClassName) || BaseClassName.IsEmpty())
			{
				return FString::Printf(TEXT("Keys[%d] ('%s') requires a non-empty BaseClass"), Index, *KeyName);
			}

			if (!ResolveClassReference(BaseClassName))
			{
				return FString::Printf(TEXT("Keys[%d] ('%s') failed to load BaseClass '%s'"), Index, *KeyName, *BaseClassName);
			}
		}

		if (TypeName.Equals(TEXT("Enum"), ESearchCase::IgnoreCase))
		{
			FString EnumName;
			if (!KeyObject->TryGetStringField(TEXT("EnumName"), EnumName) || EnumName.IsEmpty())
			{
				return FString::Printf(TEXT("Keys[%d] ('%s') requires a non-empty EnumName"), Index, *KeyName);
			}

			if (!ResolveEnumReference(EnumName))
			{
				return FString::Printf(TEXT("Keys[%d] ('%s') failed to resolve EnumName '%s'"), Index, *KeyName, *EnumName);
			}
		}
	}

	return TOptional<FString>();
}

bool FBlackboardDataGenerator::CanExtract(UObject* Asset) const
{
	return Asset && Asset->IsA<UBlackboardData>();
}

TSharedPtr<FJsonObject> FBlackboardDataGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	(void)bDiffOnly;

	const UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset);
	if (!Blackboard)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	if (Blackboard->Parent)
	{
		Config->SetStringField(TEXT("Parent"), Blackboard->Parent->GetPathName());
	}

	TArray<TSharedPtr<FJsonValue>> KeysArray;
	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		if (!Entry.KeyType)
		{
			continue;
		}

		const FString TypeName = KeyTypeClassToName(Entry.KeyType->GetClass());
		if (TypeName.IsEmpty())
		{
			continue;
		}

		TSharedPtr<FJsonObject> KeyObject = MakeShared<FJsonObject>();
		KeyObject->SetStringField(TEXT("Name"), Entry.EntryName.ToString());
		KeyObject->SetStringField(TEXT("Type"), TypeName);

		if (const UBlackboardKeyType_Object* ObjectKeyType = Cast<UBlackboardKeyType_Object>(Entry.KeyType))
		{
			if (ObjectKeyType->BaseClass)
			{
				KeyObject->SetStringField(TEXT("BaseClass"), ObjectKeyType->BaseClass->GetPathName());
			}
		}
		else if (const UBlackboardKeyType_Class* ClassKeyType = Cast<UBlackboardKeyType_Class>(Entry.KeyType))
		{
			if (ClassKeyType->BaseClass)
			{
				KeyObject->SetStringField(TEXT("BaseClass"), ClassKeyType->BaseClass->GetPathName());
			}
		}
		else if (const UBlackboardKeyType_Enum* EnumKeyType = Cast<UBlackboardKeyType_Enum>(Entry.KeyType))
		{
			if (EnumKeyType->EnumType)
			{
				KeyObject->SetStringField(TEXT("EnumName"), EnumKeyType->EnumType->GetPathName());
			}
			else if (!EnumKeyType->EnumName.IsEmpty())
			{
				KeyObject->SetStringField(TEXT("EnumName"), EnumKeyType->EnumName);
			}
		}

		if (Entry.bInstanceSynced)
		{
			KeyObject->SetBoolField(TEXT("bInstanceSynced"), true);
		}

		if (!Entry.EntryDescription.IsEmpty())
		{
			KeyObject->SetStringField(TEXT("Description"), Entry.EntryDescription);
		}

		KeysArray.Add(MakeShared<FJsonValueObject>(KeyObject));
	}

	Config->SetArrayField(TEXT("Keys"), KeysArray);
	return Config;
}

FBlackboardEntry FBlackboardDataGenerator::BuildEntry(TSharedPtr<FJsonObject> KeyJson) const
{
	FBlackboardEntry Entry;
	if (!KeyJson.IsValid())
	{
		return Entry;
	}

	FString KeyName;
	if (KeyJson->TryGetStringField(TEXT("Name"), KeyName))
	{
		Entry.EntryName = FName(*KeyName);
	}

	KeyJson->TryGetStringField(TEXT("Description"), Entry.EntryDescription);
	bool bInstanceSynced = false;
	if (KeyJson->TryGetBoolField(TEXT("bInstanceSynced"), bInstanceSynced))
	{
		Entry.bInstanceSynced = bInstanceSynced;
	}

	return Entry;
}

UClass* FBlackboardDataGenerator::ResolveKeyTypeClass(const FString& TypeName) const
{
	if (TypeName.IsEmpty())
	{
		return nullptr;
	}

	if (TypeName.Equals(TEXT("Bool"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Bool::StaticClass();
	if (TypeName.Equals(TEXT("Int"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Int::StaticClass();
	if (TypeName.Equals(TEXT("Float"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Float::StaticClass();
	if (TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_String::StaticClass();
	if (TypeName.Equals(TEXT("Name"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Name::StaticClass();
	if (TypeName.Equals(TEXT("Vector"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Vector::StaticClass();
	if (TypeName.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Rotator::StaticClass();
	if (TypeName.Equals(TEXT("Object"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Object::StaticClass();
	if (TypeName.Equals(TEXT("Class"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Class::StaticClass();
	if (TypeName.Equals(TEXT("Enum"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Enum::StaticClass();

	return nullptr;
}

FString FBlackboardDataGenerator::KeyTypeClassToName(const UClass* KeyTypeClass) const
{
	if (!KeyTypeClass)
	{
		return TEXT("");
	}

	if (KeyTypeClass == UBlackboardKeyType_Bool::StaticClass()) return TEXT("Bool");
	if (KeyTypeClass == UBlackboardKeyType_Int::StaticClass()) return TEXT("Int");
	if (KeyTypeClass == UBlackboardKeyType_Float::StaticClass()) return TEXT("Float");
	if (KeyTypeClass == UBlackboardKeyType_String::StaticClass()) return TEXT("String");
	if (KeyTypeClass == UBlackboardKeyType_Name::StaticClass()) return TEXT("Name");
	if (KeyTypeClass == UBlackboardKeyType_Vector::StaticClass()) return TEXT("Vector");
	if (KeyTypeClass == UBlackboardKeyType_Rotator::StaticClass()) return TEXT("Rotator");
	if (KeyTypeClass == UBlackboardKeyType_Object::StaticClass()) return TEXT("Object");
	if (KeyTypeClass == UBlackboardKeyType_Class::StaticClass()) return TEXT("Class");
	if (KeyTypeClass == UBlackboardKeyType_Enum::StaticClass()) return TEXT("Enum");

	return TEXT("");
}
