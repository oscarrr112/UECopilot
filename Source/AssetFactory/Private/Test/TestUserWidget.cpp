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

UObject* UTestUserWidget::GetObjectForText() const
{
	return nullptr;
}

FText UTestUserWidget::GetImpureDisplayText()
{
	return DisplayText;
}

FText UTestUserWidget::GetTextWithParameter(int32 Value) const
{
	return FText::AsNumber(Value);
}
