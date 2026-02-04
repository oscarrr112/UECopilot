// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SBSLTestWindow.h"
#include "BSL/BSLCompiler.h"
#include "JSON/BlueprintJSONSchema.h"
#include "Factory/AIBlueprintFactory.h"
#include "Engine/Blueprint.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "SBSLTestWindow"

void SBSLTestWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// Title
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Title", "BSL Compiler Test"))
			.TextStyle(FAppStyle::Get(), "LargeText")
		]

		// Test case buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			BuildTestCaseButtons()
		]

		// Main content - code input and output
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)

			// Code input
			+ SSplitter::Slot()
			.Value(0.6f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CodeInputLabel", "BSL Code:"))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBox)
					.MinDesiredHeight(200.0f)
					[
						SAssignNew(CodeInputBox, SMultiLineEditableTextBox)
						.Style(FAppStyle::Get(), "Log.TextBox")
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
						.HintText(LOCTEXT("CodeInputHint", "Enter BSL code here or click a test case button..."))
					]
				]
			]

			// Output area
			+ SSplitter::Slot()
			.Value(0.4f)
			[
				BuildOutputArea()
			]
		]

		// Action buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CompileButton", "Compile"))
				.OnClicked(this, &SBSLTestWindow::OnCompileClicked)
				.ToolTipText(LOCTEXT("CompileTooltip", "Compile BSL code to blueprint data"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ApplyButton", "Apply"))
				.OnClicked(this, &SBSLTestWindow::OnApplyClicked)
				.ToolTipText(LOCTEXT("ApplyTooltip", "Create blueprint from compiled data"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ClearButton", "Clear"))
				.OnClicked(this, &SBSLTestWindow::OnClearClicked)
			]
		]
	];

	// Load simple test by default
	SetBSLCode(GetSimpleTestCode());
}

TSharedRef<SWidget> SBSLTestWindow::BuildTestCaseButtons()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TestCasesLabel", "Test Cases:"))
			.TextStyle(FAppStyle::Get(), "NormalText")
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("SimpleTest", "Simple"))
			.OnClicked(this, &SBSLTestWindow::OnSimpleTestClicked)
			.ToolTipText(LOCTEXT("SimpleTestTooltip", "BeginPlay + PrintString"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("MediumTest", "Medium"))
			.OnClicked(this, &SBSLTestWindow::OnMediumTestClicked)
			.ToolTipText(LOCTEXT("MediumTestTooltip", "Variables + Conditions"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ComplexTest", "Complex"))
			.OnClicked(this, &SBSLTestWindow::OnComplexTestClicked)
			.ToolTipText(LOCTEXT("ComplexTestTooltip", "Functions + Multiple Outputs"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("WholeTest", "Whole"))
			.OnClicked(this, &SBSLTestWindow::OnWholeTestClicked)
			.ToolTipText(LOCTEXT("WholeTestTooltip", "All syntax features combined"))
		];
}

TSharedRef<SWidget> SBSLTestWindow::BuildOutputArea()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OutputLabel", "Output:"))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBox)
			.MinDesiredHeight(150.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(OutputBox, SMultiLineEditableTextBox)
					.Style(FAppStyle::Get(), "Log.TextBox")
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
					.IsReadOnly(true)
				]
			]
		];
}

FReply SBSLTestWindow::OnSimpleTestClicked()
{
	SetBSLCode(GetSimpleTestCode());
	ClearOutput();
	AppendOutput(TEXT("Loaded: Simple Test (BeginPlay + PrintString)\n"));
	return FReply::Handled();
}

FReply SBSLTestWindow::OnMediumTestClicked()
{
	SetBSLCode(GetMediumTestCode());
	ClearOutput();
	AppendOutput(TEXT("Loaded: Medium Test (Variables + Conditions)\n"));
	return FReply::Handled();
}

FReply SBSLTestWindow::OnComplexTestClicked()
{
	SetBSLCode(GetComplexTestCode());
	ClearOutput();
	AppendOutput(TEXT("Loaded: Complex Test (Functions + Multiple Outputs)\n"));
	return FReply::Handled();
}

FReply SBSLTestWindow::OnWholeTestClicked()
{
	SetBSLCode(GetWholeTestCode());
	ClearOutput();
	AppendOutput(TEXT("Loaded: Whole Test (All syntax features combined)\n"));
	return FReply::Handled();
}

FReply SBSLTestWindow::OnCompileClicked()
{
	CompileCurrentCode();
	return FReply::Handled();
}

FReply SBSLTestWindow::OnApplyClicked()
{
	if (!bHasCompiledData)
	{
		AppendOutput(TEXT("[Error] No compiled data. Please compile first.\n"));
		return FReply::Handled();
	}

	AppendOutput(TEXT("\n--- Applying Blueprint ---\n"));

	// Use the static CreateBlueprint function
	FString PackagePath = TEXT("/Game/Blueprints");
	FBlueprintGenerationResult Result = UAIBlueprintFactory::CreateBlueprint(
		CompiledBlueprintData, PackagePath, true);

	if (Result.bSuccess)
	{
		FString BlueprintPath = Result.Blueprint ? Result.Blueprint->GetPathName() : TEXT("Unknown");
		AppendOutput(FString::Printf(TEXT("[Success] Blueprint created: %s\n"), *BlueprintPath));
		AppendOutput(FString::Printf(TEXT("  Nodes created: %d\n"), Result.CreatedNodes.Num()));
	}
	else
	{
		AppendOutput(TEXT("[Failed] Blueprint generation failed\n"));
		if (!Result.ErrorMessage.IsEmpty())
		{
			AppendOutput(FString::Printf(TEXT("  Error: %s\n"), *Result.ErrorMessage));
		}
	}

	for (const FString& Warning : Result.Warnings)
	{
		AppendOutput(FString::Printf(TEXT("  Warning: %s\n"), *Warning));
	}

	return FReply::Handled();
}

FReply SBSLTestWindow::OnClearClicked()
{
	ClearOutput();
	bHasCompiledData = false;
	return FReply::Handled();
}

void SBSLTestWindow::SetBSLCode(const FString& Code)
{
	if (CodeInputBox.IsValid())
	{
		CodeInputBox->SetText(FText::FromString(Code));
	}
	bHasCompiledData = false;
}

void SBSLTestWindow::AppendOutput(const FString& Text)
{
	if (OutputBox.IsValid())
	{
		FString Current = OutputBox->GetText().ToString();
		OutputBox->SetText(FText::FromString(Current + Text));
	}
}

void SBSLTestWindow::ClearOutput()
{
	if (OutputBox.IsValid())
	{
		OutputBox->SetText(FText::GetEmpty());
	}
}

void SBSLTestWindow::CompileCurrentCode()
{
	if (!CodeInputBox.IsValid())
	{
		return;
	}

	FString Code = CodeInputBox->GetText().ToString();
	if (Code.IsEmpty())
	{
		AppendOutput(TEXT("[Error] No code to compile.\n"));
		return;
	}

	AppendOutput(TEXT("\n--- Compiling BSL ---\n"));
	AppendOutput(FString::Printf(TEXT("Code length: %d characters\n"), Code.Len()));

	BSL::FCompileResult Result = BSL::FCompiler::Compile(Code);

	if (Result.bSuccess)
	{
		AppendOutput(TEXT("[Success] Compilation successful!\n"));
		AppendOutput(FString::Printf(TEXT("  Blueprint name: %s\n"), *Result.BlueprintData.Name));
		AppendOutput(FString::Printf(TEXT("  Parent class: %s\n"), *Result.BlueprintData.ParentClass));
		AppendOutput(FString::Printf(TEXT("  Variables: %d\n"), Result.BlueprintData.Variables.Num()));
		AppendOutput(FString::Printf(TEXT("  Event graphs: %d\n"), Result.BlueprintData.EventGraphs.Num()));
		AppendOutput(FString::Printf(TEXT("  Functions: %d\n"), Result.BlueprintData.Functions.Num()));

		// Show details of event graphs
		for (const FBlueprintGraphData& Graph : Result.BlueprintData.EventGraphs)
		{
			AppendOutput(FString::Printf(TEXT("  [EventGraph] %s: %d nodes\n"), *Graph.Name, Graph.Nodes.Num()));
			for (const FBlueprintNodeData& Node : Graph.Nodes)
			{
				AppendOutput(FString::Printf(TEXT("    - %s (type=%d)\n"), *Node.NodeId, static_cast<int32>(Node.NodeType)));
			}
		}

		// Store for apply
		CompiledBlueprintData = Result.BlueprintData;
		bHasCompiledData = true;
	}
	else
	{
		AppendOutput(TEXT("[Failed] Compilation failed\n"));
		bHasCompiledData = false;
	}

	for (const FString& Error : Result.Errors)
	{
		AppendOutput(FString::Printf(TEXT("  Error: %s\n"), *Error));
	}

	for (const FString& Warning : Result.Warnings)
	{
		AppendOutput(FString::Printf(TEXT("  Warning: %s\n"), *Warning));
	}
}

FString SBSLTestWindow::GetSimpleTestCode()
{
	return TEXT(R"(blueprint BP_HelloWorld extends Actor {
  event BeginPlay {
    PrintString("Hello World")
  }
})");
}

FString SBSLTestWindow::GetMediumTestCode()
{
	return TEXT(R"(blueprint BP_Counter extends Actor {
  var Counter: int = 0
  var MaxCount: int = 10

  event BeginPlay {
    PrintString("Counter Started")
  }

  event Tick {
    if (Counter < MaxCount) {
      Counter = Counter + 1
    }
  }
})");
}

FString SBSLTestWindow::GetComplexTestCode()
{
	return TEXT(R"(blueprint BP_Calculator extends Actor {
  var Value: float = 0.0

  event BeginPlay {
    Value = 100.0
    PrintString("Calculator Ready")
  }

  function Calculate(A: float, B: float) -> (Sum: float, Product: float) {
    Sum = A + B
    Product = A * B
  }
})");
}

FString SBSLTestWindow::GetWholeTestCode()
{
	return TEXT(R"(blueprint BP_WholeTest extends Actor {
  // Variables of different types
  var Health: int = 100
  var Speed: float = 5.5
  var PlayerName: string = "Player1"
  var IsAlive: bool = true
  var Score: int = 0

  // BeginPlay event - initialization
  event BeginPlay {
    PrintString("Game Started!")
    PrintString(PlayerName)

    // Conditional logic
    if (IsAlive) {
      PrintString("Player is alive")
      Score = Score + 10
    } else {
      PrintString("Player is dead")
    }

    // Nested condition
    if (Health > 50) {
      if (Speed > 3.0) {
        PrintString("Healthy and Fast")
      }
    }
  }

  // Tick event - game loop
  event Tick {
    if (IsAlive) {
      // Arithmetic operations
      Score = Score + 1
      Health = Health - 1

      // Comparison with logical operators
      if (Health <= 0) {
        IsAlive = false
        PrintString("Game Over")
      }
    }
  }

  // Function with single output
  function GetDoubleHealth(Multiplier: float) -> (Result: float) {
    Result = Health * Multiplier
  }

  // Function with multiple inputs and outputs
  function CalculateStats(BaseHealth: int, BaseSpeed: float) -> (TotalHealth: int, TotalSpeed: float) {
    TotalHealth = BaseHealth + Health
    TotalSpeed = BaseSpeed + Speed
  }

  // Pure calculation function
  function AddNumbers(A: float, B: float) -> (Sum: float) {
    Sum = A + B
  }
})");
}

#undef LOCTEXT_NAMESPACE
