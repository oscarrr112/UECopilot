// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/GameplayTagGenerator.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsEditorModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameplayTagGenerator, Log, All);

FGenerationResult FGameplayTagGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	// Parse tag definitions
	const TArray<TSharedPtr<FJsonValue>>* TagsArray = GetArrayField(Config, TEXT("Tags"));
	if (!TagsArray || TagsArray->Num() == 0)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("'Tags' array is empty or missing"));
	}

	TArray<FTagDefinition> NewTags = ParseTagDefinitions(TagsArray);
	if (NewTags.Num() == 0)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("No valid tag definitions found in 'Tags' array"));
	}

	// Validate tag name format
	for (const FTagDefinition& TagDef : NewTags)
	{
		if (TagDef.Tag.IsEmpty())
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Empty tag name found"));
		}
		if (TagDef.Tag.Contains(TEXT(" ")))
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
				FString::Printf(TEXT("Tag name '%s' contains spaces. Use dot notation (e.g., 'Status.Burning')"), *TagDef.Tag));
		}
	}

	// Use the editor module API — handles ini persistence + runtime tag tree refresh
	IGameplayTagsEditorModule& EditorModule = IGameplayTagsEditorModule::Get();

	int32 NewCount = 0;
	int32 ExistingCount = 0;

	for (const FTagDefinition& TagDef : NewTags)
	{
		// Check if tag already exists at runtime
		FGameplayTag ExistingTag = FGameplayTag::RequestGameplayTag(FName(*TagDef.Tag), false);
		if (ExistingTag.IsValid())
		{
			ExistingCount++;
			UE_LOG(LogGameplayTagGenerator, Verbose, TEXT("  Tag already exists: %s"), *TagDef.Tag);
			continue;
		}

		// AddNewGameplayTagToINI writes to ini AND refreshes the tag tree
		if (EditorModule.AddNewGameplayTagToINI(TagDef.Tag, TagDef.DevComment))
		{
			NewCount++;
			UE_LOG(LogGameplayTagGenerator, Log, TEXT("  + %s"), *TagDef.Tag);
		}
		else
		{
			UE_LOG(LogGameplayTagGenerator, Warning, TEXT("  Failed to add tag: %s"), *TagDef.Tag);
		}
	}

	// Build result message
	FString Message;
	if (NewCount > 0 && ExistingCount > 0)
	{
		Message = FString::Printf(TEXT("%d new tags registered, %d already existed"), NewCount, ExistingCount);
	}
	else if (NewCount > 0)
	{
		Message = FString::Printf(TEXT("%d tags registered"), NewCount);
	}
	else
	{
		Message = FString::Printf(TEXT("All %d tags already exist"), ExistingCount);
	}

	UE_LOG(LogGameplayTagGenerator, Log, TEXT("GameplayTag '%s': %s"), *Name, *Message);

	return FGenerationResult::MakeSuccess(GetAssetType(), Name, FPaths::ProjectConfigDir(), nullptr, Message);
}

TOptional<FString> FGameplayTagGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	const TArray<TSharedPtr<FJsonValue>>* TagsArray = GetArrayField(Config, TEXT("Tags"));
	if (!TagsArray || TagsArray->Num() == 0)
	{
		return FString(TEXT("Missing or empty required field 'Tags'"));
	}

	// Validate each tag entry
	for (int32 i = 0; i < TagsArray->Num(); i++)
	{
		const TSharedPtr<FJsonValue>& Value = (*TagsArray)[i];
		if (Value->Type == EJson::String)
		{
			FString TagStr;
			if (!Value->TryGetString(TagStr) || TagStr.IsEmpty())
			{
				return FString::Printf(TEXT("Tags[%d]: empty string"), i);
			}
		}
		else if (Value->Type == EJson::Object)
		{
			TSharedPtr<FJsonObject> TagObj = Value->AsObject();
			FString TagName;
			if (!TagObj->TryGetStringField(TEXT("Tag"), TagName) || TagName.IsEmpty())
			{
				return FString::Printf(TEXT("Tags[%d]: missing or empty 'Tag' field"), i);
			}
		}
		else
		{
			return FString::Printf(TEXT("Tags[%d]: must be a string or object with 'Tag' field"), i);
		}
	}

	return TOptional<FString>(); // Valid
}

TArray<FString> FGameplayTagGenerator::GetRequiredFields() const
{
	return { TEXT("Tags") };
}

// --- Private helpers ---

TArray<FGameplayTagGenerator::FTagDefinition> FGameplayTagGenerator::ParseTagDefinitions(
	const TArray<TSharedPtr<FJsonValue>>* TagsArray) const
{
	TArray<FTagDefinition> Result;
	if (!TagsArray) return Result;

	for (const TSharedPtr<FJsonValue>& Value : *TagsArray)
	{
		FTagDefinition Def;

		if (Value->Type == EJson::String)
		{
			// Simple string format: "Status.Burning"
			Value->TryGetString(Def.Tag);
		}
		else if (Value->Type == EJson::Object)
		{
			// Object format: {"Tag": "Status.Burning", "DevComment": "..."}
			TSharedPtr<FJsonObject> TagObj = Value->AsObject();
			TagObj->TryGetStringField(TEXT("Tag"), Def.Tag);
			TagObj->TryGetStringField(TEXT("DevComment"), Def.DevComment);
		}

		if (!Def.Tag.IsEmpty())
		{
			Result.Add(MoveTemp(Def));
		}
	}

	return Result;
}
