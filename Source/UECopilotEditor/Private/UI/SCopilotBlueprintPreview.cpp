// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SCopilotBlueprintPreview.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "GraphEditor.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "SCopilotBlueprintPreview"

void SCopilotBlueprintPreview::Construct(const FArguments& InArgs)
{
	CurrentBlueprint = InArgs._Blueprint;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Graph selector
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			BuildGraphSelector()
		]

		// Graph view
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			BuildGraphView()
		]
	];

	if (CurrentBlueprint)
	{
		UpdateGraphView();
	}
}

void SCopilotBlueprintPreview::SetBlueprint(UBlueprint* InBlueprint)
{
	CurrentBlueprint = InBlueprint;
	CurrentGraph = nullptr;

	// Update available graphs
	AvailableGraphNames = GetAvailableGraphs();

	if (AvailableGraphNames.Num() > 0)
	{
		CurrentGraphName = AvailableGraphNames[0];
	}

	UpdateGraphView();
}

void SCopilotBlueprintPreview::SetHighlightedNodes(const TArray<FNodeHighlightInfo>& Highlights)
{
	NodeHighlights = Highlights;
	// Refresh view to show highlights
	UpdateGraphView();
}

void SCopilotBlueprintPreview::ClearPreview()
{
	CurrentBlueprint = nullptr;
	CurrentGraph = nullptr;
	NodeHighlights.Empty();
	AvailableGraphNames.Empty();
	CurrentGraphName = nullptr;

	UpdateGraphView();
}

void SCopilotBlueprintPreview::SelectGraph(UEdGraph* Graph)
{
	if (Graph && CurrentBlueprint)
	{
		CurrentGraph = Graph;

		// Find matching name
		for (const TSharedPtr<FString>& Name : AvailableGraphNames)
		{
			if (*Name == Graph->GetName())
			{
				CurrentGraphName = Name;
				break;
			}
		}

		UpdateGraphView();
	}
}

TSharedRef<SWidget> SCopilotBlueprintPreview::BuildGraphSelector()
{
	AvailableGraphNames = GetAvailableGraphs();

	if (AvailableGraphNames.Num() > 0)
	{
		CurrentGraphName = AvailableGraphNames[0];
	}

	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("GraphLabel", "Graph:"))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&AvailableGraphNames)
			.InitiallySelectedItem(CurrentGraphName)
			.OnSelectionChanged(this, &SCopilotBlueprintPreview::OnGraphSelectionChanged)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
			{
				return SNew(STextBlock).Text(FText::FromString(*Item));
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return CurrentGraphName.IsValid() ? FText::FromString(*CurrentGraphName) : LOCTEXT("NoGraph", "No Graph");
				})
			]
		];
}

TSharedRef<SWidget> SCopilotBlueprintPreview::BuildGraphView()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(2.0f)
		[
			SNew(SBox)
			.MinDesiredWidth(400.0f)
			.MinDesiredHeight(300.0f)
			[
				SNew(SOverlay)

				// Placeholder when no blueprint
				+ SOverlay::Slot()
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Visibility_Lambda([this]()
					{
						return CurrentBlueprint == nullptr ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoPreview", "No blueprint to preview"))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]

				// Graph panel container
				+ SOverlay::Slot()
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						return CurrentBlueprint != nullptr ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						// The actual graph editor would go here
						// For now, using a placeholder
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								if (CurrentGraph)
								{
									return FText::Format(LOCTEXT("GraphPreviewFormat", "Graph: {0}\nNodes: {1}"),
										FText::FromString(CurrentGraph->GetName()),
										FText::AsNumber(CurrentGraph->Nodes.Num()));
								}
								return LOCTEXT("SelectGraph", "Select a graph to preview");
							})
						]
					]
				]
			]
		];
}

void SCopilotBlueprintPreview::OnGraphSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid() || !CurrentBlueprint)
	{
		return;
	}

	CurrentGraphName = NewSelection;

	// Find the matching graph
	FString SelectedName = *NewSelection;

	// Search in event graphs
	for (UEdGraph* Graph : CurrentBlueprint->UbergraphPages)
	{
		if (Graph && Graph->GetName() == SelectedName)
		{
			CurrentGraph = Graph;
			UpdateGraphView();
			return;
		}
	}

	// Search in function graphs
	for (UEdGraph* Graph : CurrentBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == SelectedName)
		{
			CurrentGraph = Graph;
			UpdateGraphView();
			return;
		}
	}
}

TArray<TSharedPtr<FString>> SCopilotBlueprintPreview::GetAvailableGraphs() const
{
	TArray<TSharedPtr<FString>> Result;

	if (!CurrentBlueprint)
	{
		return Result;
	}

	// Add event graphs
	for (UEdGraph* Graph : CurrentBlueprint->UbergraphPages)
	{
		if (Graph)
		{
			Result.Add(MakeShared<FString>(Graph->GetName()));
		}
	}

	// Add function graphs
	for (UEdGraph* Graph : CurrentBlueprint->FunctionGraphs)
	{
		if (Graph)
		{
			Result.Add(MakeShared<FString>(Graph->GetName()));
		}
	}

	return Result;
}

void SCopilotBlueprintPreview::UpdateGraphView()
{
	// In a full implementation, this would refresh the SGraphPanel
	// For now, the view updates automatically via lambdas
}

#undef LOCTEXT_NAMESPACE
