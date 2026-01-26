// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "JSON/BlueprintJSONSchema.h"

class SMultiLineEditableTextBox;

/**
 * BSL Compiler Test Window
 * For testing BSL compilation without AI
 */
class SBSLTestWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBSLTestWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// UI Builders
	TSharedRef<SWidget> BuildTestCaseButtons();
	TSharedRef<SWidget> BuildOutputArea();

	// Button handlers
	FReply OnSimpleTestClicked();
	FReply OnMediumTestClicked();
	FReply OnComplexTestClicked();
	FReply OnWholeTestClicked();
	FReply OnLoopTestClicked();
	FReply OnBreakContinueTestClicked();
	FReply OnArrayTestClicked();
	FReply OnCompileClicked();
	FReply OnApplyClicked();
	FReply OnClearClicked();

	// Helper functions
	void SetBSLCode(const FString& Code);
	void AppendOutput(const FString& Text);
	void ClearOutput();
	void CompileCurrentCode();

	// Test case code
	static FString GetSimpleTestCode();
	static FString GetMediumTestCode();
	static FString GetComplexTestCode();
	static FString GetWholeTestCode();
	static FString GetLoopTestCode();
	static FString GetBreakContinueTestCode();
	static FString GetArrayTestCode();

private:
	TSharedPtr<SMultiLineEditableTextBox> CodeInputBox;
	TSharedPtr<SMultiLineEditableTextBox> OutputBox;

	// Store compiled blueprint data for apply
	bool bHasCompiledData = false;
	FBlueprintData CompiledBlueprintData;
};
