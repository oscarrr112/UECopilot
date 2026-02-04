// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UBlueprint;

DECLARE_DELEGATE(FOnDiffAccepted);
DECLARE_DELEGATE(FOnDiffRejected);

/**
 * Diff entry type
 */
UENUM()
enum class EDiffEntryType : uint8
{
	Added,
	Removed,
	Modified
};

/**
 * Single diff entry
 */
struct FBlueprintDiffEntry
{
	EDiffEntryType Type;
	FString Category; // "Variable", "Function", "Node", etc.
	FString Name;
	FString Description;
	bool bSelected = true;
};

/**
 * Copilot Blueprint Diff Widget
 * Shows differences between original and generated blueprints
 */
class ASSETFACTORYEDITOR_API SAIBlueprintDiff : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAIBlueprintDiff)
		: _OriginalBlueprint(nullptr)
		, _GeneratedBlueprint(nullptr)
		{}
		SLATE_ARGUMENT(UBlueprint*, OriginalBlueprint)
		SLATE_ARGUMENT(UBlueprint*, GeneratedBlueprint)
		SLATE_EVENT(FOnDiffAccepted, OnAccepted)
		SLATE_EVENT(FOnDiffRejected, OnRejected)
	SLATE_END_ARGS()

	/** Construct this widget */
	void Construct(const FArguments& InArgs);

	/** Set blueprints to compare */
	void SetBlueprints(UBlueprint* Original, UBlueprint* Generated);

	/** Get selected changes */
	TArray<FBlueprintDiffEntry> GetSelectedChanges() const;

	/** Refresh the diff view */
	void RefreshDiff();

private:
	/** Build the header */
	TSharedRef<SWidget> BuildHeader();

	/** Build the diff list */
	TSharedRef<SWidget> BuildDiffList();

	/** Build the button bar */
	TSharedRef<SWidget> BuildButtonBar();

	/** Create a diff entry widget */
	TSharedRef<SWidget> CreateDiffEntryWidget(FBlueprintDiffEntry& Entry);

	/** Calculate differences between blueprints */
	void CalculateDiff();

	/** Compare variables */
	void CompareVariables();

	/** Compare graphs */
	void CompareGraphs();

	/** Handle accept button */
	FReply OnAcceptClicked();

	/** Handle reject button */
	FReply OnRejectClicked();

	/** Handle select all */
	FReply OnSelectAllClicked();

	/** Handle deselect all */
	FReply OnDeselectAllClicked();

	/** Get summary text */
	FText GetSummaryText() const;

private:
	/** Original blueprint */
	UPROPERTY()
	UBlueprint* OriginalBlueprint = nullptr;

	/** Generated blueprint */
	UPROPERTY()
	UBlueprint* GeneratedBlueprint = nullptr;

	/** Diff entries */
	TArray<FBlueprintDiffEntry> DiffEntries;

	/** Diff list container */
	TSharedPtr<SVerticalBox> DiffListBox;

	/** Callbacks */
	FOnDiffAccepted OnAccepted;
	FOnDiffRejected OnRejected;
};
