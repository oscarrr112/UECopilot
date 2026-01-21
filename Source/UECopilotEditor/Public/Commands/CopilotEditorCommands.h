// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

/**
 * UE Copilot Editor Commands
 */
class FCopilotEditorCommands : public TCommands<FCopilotEditorCommands>
{
public:
	FCopilotEditorCommands()
		: TCommands<FCopilotEditorCommands>(
			TEXT("UECopilot"),
			NSLOCTEXT("Contexts", "UECopilot", "UE Copilot"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	/** Initialize commands */
	virtual void RegisterCommands() override;

public:
	/** Opens the AI chat window */
	TSharedPtr<FUICommandInfo> OpenChatWindow;

	/** Opens the settings dialog */
	TSharedPtr<FUICommandInfo> OpenSettings;

	/** Generate blueprint from selection */
	TSharedPtr<FUICommandInfo> GenerateBlueprint;

	/** Explain selected blueprint */
	TSharedPtr<FUICommandInfo> ExplainBlueprint;

	/** Modify selected blueprint */
	TSharedPtr<FUICommandInfo> ModifyBlueprint;
};
