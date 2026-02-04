// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/ConversationContext.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"

UAIConversationContext::UAIConversationContext()
{
}

UAIConversationContext* UAIConversationContext::Create(UObject* Outer, const FString& InSystemPrompt)
{
	UAIConversationContext* Context = NewObject<UAIConversationContext>(Outer);
	Context->SystemPrompt = InSystemPrompt;
	return Context;
}

void UAIConversationContext::ClearHistory()
{
	History.Empty();
}

void UAIConversationContext::AddUserMessage(const FString& Content)
{
	History.Add(FChatMessage(EChatMessageRole::User, Content));
	TrimHistory();
}

void UAIConversationContext::AddAssistantMessage(const FString& Content)
{
	History.Add(FChatMessage(EChatMessageRole::Assistant, Content));
	TrimHistory();
}

void UAIConversationContext::SetSystemPrompt(const FString& Prompt)
{
	SystemPrompt = Prompt;
}

FString UAIConversationContext::GetSystemPrompt() const
{
	return SystemPrompt;
}

TArray<FChatMessage> UAIConversationContext::GetMessages() const
{
	TArray<FChatMessage> Messages;

	// Build full system prompt with project context
	FString FullSystemPrompt = SystemPrompt;
	if (!ProjectContext.IsEmpty())
	{
		FullSystemPrompt += TEXT("\n\n## Project Context\n") + ProjectContext;
	}

	if (!FullSystemPrompt.IsEmpty())
	{
		Messages.Add(FChatMessage(EChatMessageRole::System, FullSystemPrompt));
	}

	Messages.Append(History);

	return Messages;
}

int32 UAIConversationContext::GetMessageCount() const
{
	return History.Num();
}

void UAIConversationContext::InjectProjectContext(const FString& Context)
{
	ProjectContext = Context;
}

void UAIConversationContext::SetMaxHistoryLength(int32 MaxLength)
{
	MaxHistoryLength = FMath::Max(0, MaxLength);
	TrimHistory();
}

void UAIConversationContext::TrimHistory()
{
	if (MaxHistoryLength > 0 && History.Num() > MaxHistoryLength)
	{
		int32 RemoveCount = History.Num() - MaxHistoryLength;
		History.RemoveAt(0, RemoveCount);
	}
}

// =====================================================
// UProjectContextCollector Implementation
// =====================================================

FString UProjectContextCollector::CollectContext()
{
	FString Context;

	// Get list of blueprints (limit to 20 to avoid huge context)
	TArray<FString> Blueprints = GetProjectBlueprints();

	if (Blueprints.Num() > 0)
	{
		Context += TEXT("### Available Blueprints:\n");
		int32 MaxBlueprints = FMath::Min(Blueprints.Num(), 20);
		for (int32 i = 0; i < MaxBlueprints; i++)
		{
			Context += FString::Printf(TEXT("- %s\n"), *Blueprints[i]);
		}
		if (Blueprints.Num() > 20)
		{
			Context += FString::Printf(TEXT("... and %d more\n"), Blueprints.Num() - 20);
		}
	}

	// Add common UE classes for reference
	Context += TEXT("\n### Common Base Classes:\n");
	Context += TEXT("- Actor, Pawn, Character, PlayerController, GameModeBase, ActorComponent, SceneComponent\n");

	return Context;
}

TArray<FString> UProjectContextCollector::GetProjectBlueprints()
{
	TArray<FString> Result;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AssetDataList);

	for (const FAssetData& AssetData : AssetDataList)
	{
		// Only include game content, not engine/plugin content
		FString PackagePath = AssetData.PackagePath.ToString();
		if (PackagePath.StartsWith(TEXT("/Game/")))
		{
			Result.Add(AssetData.AssetName.ToString());
		}
	}

	return Result;
}

FString UProjectContextCollector::GetClassHierarchy(UClass* Class)
{
	if (!Class)
	{
		return TEXT("");
	}

	FString Hierarchy;
	UClass* CurrentClass = Class;

	while (CurrentClass)
	{
		if (!Hierarchy.IsEmpty())
		{
			Hierarchy += TEXT(" -> ");
		}
		Hierarchy += CurrentClass->GetName();
		CurrentClass = CurrentClass->GetSuperClass();
	}

	return Hierarchy;
}

TArray<FString> UProjectContextCollector::GetClassFunctions(UClass* Class)
{
	TArray<FString> Functions;

	if (!Class)
	{
		return Functions;
	}

	for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		UFunction* Function = *It;
		if (Function)
		{
			FString FuncSignature = Function->GetName();

			// Add parameter info
			FString Params;
			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
			{
				FProperty* Param = *ParamIt;
				if (Param->HasAnyPropertyFlags(CPF_Parm))
				{
					if (!Params.IsEmpty())
					{
						Params += TEXT(", ");
					}
					Params += Param->GetCPPType() + TEXT(" ") + Param->GetName();
				}
			}

			FuncSignature += TEXT("(") + Params + TEXT(")");
			Functions.Add(FuncSignature);
		}
	}

	return Functions;
}

FString UProjectContextCollector::GetBlueprintVariables(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return TEXT("");
	}

	FString Variables;

	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		FString TypeName = Var.VarType.PinCategory.ToString();
		if (Var.VarType.PinSubCategoryObject.IsValid())
		{
			TypeName = Var.VarType.PinSubCategoryObject->GetName();
		}
		Variables += FString::Printf(TEXT("- %s: %s\n"),
			*Var.VarName.ToString(),
			*TypeName);
	}

	return Variables;
}

FString UProjectContextCollector::GetBlueprintSummary(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return TEXT("");
	}

	FString Summary;

	Summary += FString::Printf(TEXT("Blueprint: %s\n"), *Blueprint->GetName());
	Summary += FString::Printf(TEXT("Parent Class: %s\n"),
		Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("None"));

	// Variables
	Summary += TEXT("\nVariables:\n");
	Summary += GetBlueprintVariables(Blueprint);

	// Functions
	Summary += TEXT("\nFunctions:\n");
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph)
		{
			Summary += FString::Printf(TEXT("- %s\n"), *Graph->GetName());
		}
	}

	// Event Graphs
	Summary += TEXT("\nEvent Graphs:\n");
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph)
		{
			Summary += FString::Printf(TEXT("- %s\n"), *Graph->GetName());
		}
	}

	return Summary;
}
