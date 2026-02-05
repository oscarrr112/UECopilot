// Copyright ProjectRPG. All Rights Reserved.

#include "Test/TestUserWidget.h"

FText UTestUserWidget::GetDisplayText() const
{
	return DisplayText;
}

ESlateVisibility UTestUserWidget::GetTextVisibility() const
{
	return bShowText ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
}

FLinearColor UTestUserWidget::GetTextColor() const
{
	return TextColor;
}

float UTestUserWidget::GetTextOpacity() const
{
	return TextOpacity;
}

bool UTestUserWidget::IsTextEnabled() const
{
	return bTextEnabled;
}
