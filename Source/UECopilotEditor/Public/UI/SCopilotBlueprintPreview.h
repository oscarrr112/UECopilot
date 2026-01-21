// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "JSON/BlueprintJSONSchema.h"

class UBlueprint;
class SGraphPanel;
class UEdGraph;

/**
 * Node highlight info for preview
 */
struct FNodeHighlightInfo
{
	enum class EHighlightType
	{
		New,
		Modified,
		Deleted
	};

	FString NodeId;
	EHighlightType Type;
};

/**
 * Copilot Blueprint Preview Widget
 * Shows a read-only preview of generated blueprint graphs
 */
class UECOPILOTEDITOR_API SCopilotBlueprintPreview : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCopilotBlueprintPreview)
		: _Blueprint(nullptr)
		{}
		SLATE_ARGUMENT(UBlueprint*, Blueprint)
	SLATE_END_ARGS()

	/** Construct this widget */
	void Construct(const FArguments& InArgs);

	/** Set the blueprint to preview */
	void SetBlueprint(UBlueprint* InBlueprint);

	/** Set highlighted nodes */
	void SetHighlightedNodes(const TArray<FNodeHighlightInfo>& Highlights);

	/** Clear the preview */
	void ClearPreview();

	/** Get the current blueprint */
	UBlueprint* GetBlueprint() const { return CurrentBlueprint; }

	/** Select a specific graph to view */
	void SelectGraph(UEdGraph* Graph);

private:
	/** Build the graph selector dropdown */
	TSharedRef<SWidget> BuildGraphSelector();

	/** Build the graph view */
	TSharedRef<SWidget> BuildGraphView();

	/** Handle graph selection changed */
	void OnGraphSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);

	/** Get available graphs */
	TArray<TSharedPtr<FString>> GetAvailableGraphs() const;

	/** Update the graph view */
	void UpdateGraphView();

private:
	/** Current blueprint being previewed */
	UPROPERTY()
	UBlueprint* CurrentBlueprint = nullptr;

	/** Current graph being displayed */
	UPROPERTY()
	UEdGraph* CurrentGraph = nullptr;

	/** Node highlights */
	TArray<FNodeHighlightInfo> NodeHighlights;

	/** Graph panel widget */
	TSharedPtr<SGraphPanel> GraphPanel;

	/** Graph selector combo box */
	TSharedPtr<SWidget> GraphSelector;

	/** Currently selected graph name */
	TSharedPtr<FString> CurrentGraphName;

	/** Available graph names */
	TArray<TSharedPtr<FString>> AvailableGraphNames;
};
