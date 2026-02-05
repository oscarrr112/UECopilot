// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

/**
 * UE Copilot Editor Commands
 */
class FAssetFactoryCommands : public TCommands<FAssetFactoryCommands>
{
public:
	FAssetFactoryCommands()
		: TCommands<FAssetFactoryCommands>(
			TEXT("AssetFactoryAI"),
			NSLOCTEXT("Contexts", "AssetFactoryAI", "Asset Factory AI"),
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
