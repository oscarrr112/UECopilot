// Copyright ProjectRPG. All Rights Reserved.

#include "Test/TestCustomWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "TestCustomWidget"

UTestCustomWidget::UTestCustomWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CustomText(TEXT("Default Custom Text"))
	, CustomValue(0)
	, bCustomEnabled(true)
	, CustomColor(FLinearColor(0.2f, 0.4f, 0.8f, 1.0f))
{
}

void UTestCustomWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyBorder.Reset();
}

TSharedRef<SWidget> UTestCustomWidget::RebuildWidget()
{
	MyBorder = SNew(SBorder)
		.BorderBackgroundColor(CustomColor)
		.Padding(FMargin(10.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Custom Widget: %s"), *CustomText)))
				.ColorAndOpacity(FSlateColor(FLinearColor::White))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 5.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Value: %d | Enabled: %s"),
					CustomValue,
					bCustomEnabled ? TEXT("Yes") : TEXT("No"))))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
			]
		];

	return MyBorder.ToSharedRef();
}

#if WITH_EDITOR
const FText UTestCustomWidget::GetPaletteCategory()
{
	return LOCTEXT("CustomWidgets", "Custom Widgets");
}
#endif

#undef LOCTEXT_NAMESPACE
