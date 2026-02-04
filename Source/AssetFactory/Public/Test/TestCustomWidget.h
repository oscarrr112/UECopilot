// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "TestCustomWidget.generated.h"

/**
 * Test custom widget for validating dynamic class loading in WidgetBlueprintGenerator
 *
 * Example JSON:
 * {
 *   "Type": "/Script/AssetFactory.TestCustomWidget",
 *   "Name": "MyCustomWidget",
 *   "Properties": {
 *     "CustomText": "Hello World",
 *     "CustomValue": 42,
 *     "bCustomEnabled": true
 *   }
 * }
 */
UCLASS()
class ASSETFACTORY_API UTestCustomWidget : public UWidget
{
	GENERATED_BODY()

public:
	UTestCustomWidget(const FObjectInitializer& ObjectInitializer);

	//~ UWidget Interface
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	//~ UWidget Interface
	virtual TSharedRef<SWidget> RebuildWidget() override;

public:
	/** Custom text property that can be set via JSON */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom")
	FString CustomText;

	/** Custom integer value property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom")
	int32 CustomValue;

	/** Custom boolean flag property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom")
	bool bCustomEnabled;

	/** Custom color property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom")
	FLinearColor CustomColor;

protected:
	/** The Slate widget instance */
	TSharedPtr<class SBorder> MyBorder;
};
