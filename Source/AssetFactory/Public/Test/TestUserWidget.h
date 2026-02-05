// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TestUserWidget.generated.h"

/**
 * Test UserWidget base class for validating Bindings function detection
 *
 * This class provides C++ functions that can be bound to widget properties,
 * allowing us to test that WidgetBlueprintGenerator correctly validates
 * bindings against the entire class hierarchy (including C++ base classes).
 *
 * Example WidgetBlueprint JSON using this as parent:
 * {
 *   "AssetType": "WidgetBlueprint",
 *   "Name": "WBP_TestBindings",
 *   "Path": "/Game/Test",
 *   "ParentClass": "TestUserWidget",
 *   "RootWidget": {
 *     "Type": "CanvasPanel",
 *     "Name": "RootPanel",
 *     "Children": [
 *       {
 *         "Type": "TextBlock",
 *         "Name": "MyText",
 *         "Bindings": {
 *           "Text": "GetDisplayText"
 *         }
 *       }
 *     ]
 *   }
 * }
 */
UCLASS(Blueprintable)
class ASSETFACTORY_API UTestUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ Binding Functions (can be bound to widget properties)

	/** Returns text for binding - demonstrates FText binding */
	UFUNCTION(BlueprintCallable, Category = "Bindings")
	FText GetDisplayText() const;

	/** Returns visibility for binding - demonstrates ESlateVisibility binding */
	UFUNCTION(BlueprintCallable, Category = "Bindings")
	ESlateVisibility GetTextVisibility() const;

	/** Returns color for binding - demonstrates FLinearColor binding */
	UFUNCTION(BlueprintCallable, Category = "Bindings")
	FLinearColor GetTextColor() const;

	/** Returns opacity for binding - demonstrates float binding */
	UFUNCTION(BlueprintCallable, Category = "Bindings")
	float GetTextOpacity() const;

	/** Returns enabled state for binding - demonstrates bool binding */
	UFUNCTION(BlueprintCallable, Category = "Bindings")
	bool IsTextEnabled() const;

public:
	//~ Properties that can affect bindings

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FText DisplayText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	bool bShowText = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FLinearColor TextColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	float TextOpacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	bool bTextEnabled = true;
};
