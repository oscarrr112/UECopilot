// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commands/AssetFactoryCommands.h"

#define LOCTEXT_NAMESPACE "CopilotEditorCommands"

void FAssetFactoryCommands::RegisterCommands()
{
	UI_COMMAND(
		OpenChatWindow,
		"Open Copilot",
		"Open the UE Copilot AI chat window",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::P)
	);

	UI_COMMAND(
		OpenSettings,
		"Copilot Settings",
		"Open UE Copilot settings",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		GenerateBlueprint,
		"Generate Blueprint",
		"Generate a new blueprint using AI",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::G)
	);

	UI_COMMAND(
		ExplainBlueprint,
		"Explain Blueprint",
		"Get an AI explanation of the selected blueprint",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::E)
	);

	UI_COMMAND(
		ModifyBlueprint,
		"Modify Blueprint",
		"Modify the selected blueprint using AI",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::M)
	);
}

#undef LOCTEXT_NAMESPACE
