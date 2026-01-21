// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SCopilotBlueprintDiff.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "SCopilotBlueprintDiff"

void SCopilotBlueprintDiff::Construct(const FArguments& InArgs)
{
	OriginalBlueprint = InArgs._OriginalBlueprint;
	GeneratedBlueprint = InArgs._GeneratedBlueprint;
	OnAccepted = InArgs._OnAccepted;
	OnRejected = InArgs._OnRejected;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Header
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			BuildHeader()
		]

		// Diff list
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			BuildDiffList()
		]

		// Button bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			BuildButtonBar()
		]
	];

	if (OriginalBlueprint && GeneratedBlueprint)
	{
		CalculateDiff();
	}
}

void SCopilotBlueprintDiff::SetBlueprints(UBlueprint* Original, UBlueprint* Generated)
{
	OriginalBlueprint = Original;
	GeneratedBlueprint = Generated;

	CalculateDiff();
	RefreshDiff();
}

TArray<FBlueprintDiffEntry> SCopilotBlueprintDiff::GetSelectedChanges() const
{
	TArray<FBlueprintDiffEntry> Selected;

	for (const FBlueprintDiffEntry& Entry : DiffEntries)
	{
		if (Entry.bSelected)
		{
			Selected.Add(Entry);
		}
	}

	return Selected;
}

void SCopilotBlueprintDiff::RefreshDiff()
{
	if (!DiffListBox.IsValid())
	{
		return;
	}

	DiffListBox->ClearChildren();

	for (FBlueprintDiffEntry& Entry : DiffEntries)
	{
		DiffListBox->AddSlot()
			.AutoHeight()
			.Padding(2.0f)
			[
				CreateDiffEntryWidget(Entry)
			];
	}
}

TSharedRef<SWidget> SCopilotBlueprintDiff::BuildHeader()
{
	return SNew(SVerticalBox)

		// Title
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DiffTitle", "Blueprint Changes"))
			.TextStyle(FAppStyle::Get(), "NormalText.Important")
		]

		// Summary
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(this, &SCopilotBlueprintDiff::GetSummaryText)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]

		// Selection buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SelectAll", "Select All"))
				.OnClicked(this, &SCopilotBlueprintDiff::OnSelectAllClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("DeselectAll", "Deselect All"))
				.OnClicked(this, &SCopilotBlueprintDiff::OnDeselectAllClicked)
			]
		];
}

TSharedRef<SWidget> SCopilotBlueprintDiff::BuildDiffList()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(DiffListBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SCopilotBlueprintDiff::BuildButtonBar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		]

		// Reject button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("RejectButton", "Cancel"))
			.OnClicked(this, &SCopilotBlueprintDiff::OnRejectClicked)
		]

		// Accept button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("AcceptButton", "Apply Selected Changes"))
			.OnClicked(this, &SCopilotBlueprintDiff::OnAcceptClicked)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
		];
}

TSharedRef<SWidget> SCopilotBlueprintDiff::CreateDiffEntryWidget(FBlueprintDiffEntry& Entry)
{
	FLinearColor TypeColor;
	FString TypeLabel;

	switch (Entry.Type)
	{
	case EDiffEntryType::Added:
		TypeColor = FLinearColor::Green;
		TypeLabel = TEXT("+");
		break;
	case EDiffEntryType::Removed:
		TypeColor = FLinearColor::Red;
		TypeLabel = TEXT("-");
		break;
	case EDiffEntryType::Modified:
		TypeColor = FLinearColor::Yellow;
		TypeLabel = TEXT("~");
		break;
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)

			// Checkbox
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([&Entry]() { return Entry.bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([&Entry](ECheckBoxState NewState) { Entry.bSelected = NewState == ECheckBoxState::Checked; })
			]

			// Type indicator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderBackgroundColor(TypeColor)
				.Padding(FMargin(6.0f, 2.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TypeLabel))
					.ColorAndOpacity(FLinearColor::White)
				]
			]

			// Category
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("[%s]"), *Entry.Category)))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			// Name and description
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Entry.Name))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Entry.Description))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Visibility(Entry.Description.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]
			]
		];
}

void SCopilotBlueprintDiff::CalculateDiff()
{
	DiffEntries.Empty();

	if (!OriginalBlueprint && GeneratedBlueprint)
	{
		// Everything is new
		CompareVariables();
		CompareGraphs();
	}
	else if (OriginalBlueprint && GeneratedBlueprint)
	{
		// Compare both
		CompareVariables();
		CompareGraphs();
	}
}

void SCopilotBlueprintDiff::CompareVariables()
{
	if (!GeneratedBlueprint)
	{
		return;
	}

	TSet<FName> OriginalVars;
	if (OriginalBlueprint)
	{
		for (const FBPVariableDescription& Var : OriginalBlueprint->NewVariables)
		{
			OriginalVars.Add(Var.VarName);
		}
	}

	for (const FBPVariableDescription& Var : GeneratedBlueprint->NewVariables)
	{
		FBlueprintDiffEntry Entry;
		Entry.Category = TEXT("Variable");
		Entry.Name = Var.VarName.ToString();
		Entry.Description = FString::Printf(TEXT("Type: %s"), *Var.VarType.ToString());

		if (OriginalVars.Contains(Var.VarName))
		{
			// Check if modified
			Entry.Type = EDiffEntryType::Modified;
			Entry.Description = TEXT("Variable definition changed");
		}
		else
		{
			Entry.Type = EDiffEntryType::Added;
		}

		DiffEntries.Add(Entry);
		OriginalVars.Remove(Var.VarName);
	}

	// Remaining original vars were removed
	for (const FName& VarName : OriginalVars)
	{
		FBlueprintDiffEntry Entry;
		Entry.Type = EDiffEntryType::Removed;
		Entry.Category = TEXT("Variable");
		Entry.Name = VarName.ToString();

		DiffEntries.Add(Entry);
	}
}

void SCopilotBlueprintDiff::CompareGraphs()
{
	if (!GeneratedBlueprint)
	{
		return;
	}

	TSet<FString> OriginalGraphs;
	if (OriginalBlueprint)
	{
		for (UEdGraph* Graph : OriginalBlueprint->FunctionGraphs)
		{
			if (Graph)
			{
				OriginalGraphs.Add(Graph->GetName());
			}
		}
	}

	for (UEdGraph* Graph : GeneratedBlueprint->FunctionGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		FBlueprintDiffEntry Entry;
		Entry.Category = TEXT("Function");
		Entry.Name = Graph->GetName();
		Entry.Description = FString::Printf(TEXT("%d nodes"), Graph->Nodes.Num());

		if (OriginalGraphs.Contains(Graph->GetName()))
		{
			Entry.Type = EDiffEntryType::Modified;
			OriginalGraphs.Remove(Graph->GetName());
		}
		else
		{
			Entry.Type = EDiffEntryType::Added;
		}

		DiffEntries.Add(Entry);
	}

	// Add event graph changes
	for (UEdGraph* Graph : GeneratedBlueprint->UbergraphPages)
	{
		if (!Graph)
		{
			continue;
		}

		FBlueprintDiffEntry Entry;
		Entry.Type = EDiffEntryType::Modified; // Event graphs are usually modified
		Entry.Category = TEXT("Event Graph");
		Entry.Name = Graph->GetName();
		Entry.Description = FString::Printf(TEXT("%d nodes"), Graph->Nodes.Num());

		DiffEntries.Add(Entry);
	}
}

FReply SCopilotBlueprintDiff::OnAcceptClicked()
{
	OnAccepted.ExecuteIfBound();
	return FReply::Handled();
}

FReply SCopilotBlueprintDiff::OnRejectClicked()
{
	OnRejected.ExecuteIfBound();
	return FReply::Handled();
}

FReply SCopilotBlueprintDiff::OnSelectAllClicked()
{
	for (FBlueprintDiffEntry& Entry : DiffEntries)
	{
		Entry.bSelected = true;
	}
	RefreshDiff();
	return FReply::Handled();
}

FReply SCopilotBlueprintDiff::OnDeselectAllClicked()
{
	for (FBlueprintDiffEntry& Entry : DiffEntries)
	{
		Entry.bSelected = false;
	}
	RefreshDiff();
	return FReply::Handled();
}

FText SCopilotBlueprintDiff::GetSummaryText() const
{
	int32 Added = 0;
	int32 Modified = 0;
	int32 Removed = 0;
	int32 Selected = 0;

	for (const FBlueprintDiffEntry& Entry : DiffEntries)
	{
		switch (Entry.Type)
		{
		case EDiffEntryType::Added: Added++; break;
		case EDiffEntryType::Modified: Modified++; break;
		case EDiffEntryType::Removed: Removed++; break;
		}

		if (Entry.bSelected)
		{
			Selected++;
		}
	}

	return FText::Format(
		LOCTEXT("DiffSummaryFormat", "{0} additions, {1} modifications, {2} removals ({3} selected)"),
		FText::AsNumber(Added),
		FText::AsNumber(Modified),
		FText::AsNumber(Removed),
		FText::AsNumber(Selected)
	);
}

#undef LOCTEXT_NAMESPACE
