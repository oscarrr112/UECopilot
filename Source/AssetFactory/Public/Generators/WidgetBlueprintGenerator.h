// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"
#include "Widgets/Layout/Anchors.h"
#include "Layout/Margin.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Framework/Text/TextLayout.h"
#include "Components/SlateWrapperTypes.h"

class UWidgetBlueprint;
class UWidget;
class UPanelWidget;
class UWidgetTree;

/**
 * Generator for Widget Blueprint assets from JSON configuration
 *
 * Supports:
 * - Container widgets: CanvasPanel, VerticalBox, HorizontalBox, Overlay, ScrollBox, SizeBox, ScaleBox, WidgetSwitcher, GridPanel
 * - Basic widgets: TextBlock, RichTextBlock, Image, Button, Border, Spacer
 * - Input widgets: EditableTextBox, CheckBox, Slider, ProgressBar, ComboBoxString
 * - Custom widgets: Any UWidget subclass via class path (e.g., "/Script/ProjectRPG.MyWidget")
 * - Widget Blueprint instances: Use Type "UserWidget" with "WidgetClass" property pointing to another WBP
 * - Property Bindings: Bind widget properties to functions defined in parent class
 *
 * Property Binding Example (Simple - bind to function):
 * {
 *   "Type": "ProgressBar",
 *   "Name": "HealthBar",
 *   "IsVariable": true,
 *   "Bindings": {
 *     "Percent": "GetHealthPercent"
 *   }
 * }
 *
 * Property Binding Example (Full):
 * {
 *   "Type": "ProgressBar",
 *   "Name": "HealthBar",
 *   "IsVariable": true,
 *   "Bindings": {
 *     "Percent": {
 *       "Function": "GetHealthPercent",
 *       "Kind": "Function"
 *     },
 *     "FillColorAndOpacity": {
 *       "Property": "HealthBarColor",
 *       "Kind": "Property"
 *     }
 *   }
 * }
 *
 * UserWidget Instance Example:
 * {
 *   "Type": "UserWidget",
 *   "Name": "MySubWidget",
 *   "IsVariable": true,
 *   "Properties": {
 *     "WidgetClass": "/Game/UI/Combat/WBP_SkillPoint"
 *   }
 * }
 *
 * ClassDefaults Example (set Blueprint class default properties):
 * {
 *   "AssetType": "WidgetBlueprint",
 *   "Name": "WBP_CostDot",
 *   "ParentClass": "/Script/ProjectRPG.CostDotWidget",
 *   "ClassDefaults": {
 *     "ProgressMaterial": "/Game/UI/Materials/Combat/M_UI_CircularProgress",
 *     "AvailableColor": [0.2, 0.8, 0.2, 1.0],
 *     "CostDotClass": "/Game/UI/Combat/WBP_CostDot",
 *     "ElementIcons": {
 *       "Fire": "/Game/UI/Textures/Combat/element_fire",
 *       "Water": "/Game/UI/Textures/Combat/element_water"
 *     }
 *   },
 *   "RootWidget": { ... }
 * }
 *
 * JSON Config Example:
 * {
 *   "AssetType": "WidgetBlueprint",
 *   "Name": "WBP_MainMenu",
 *   "Path": "/Game/UI/Menus",
 *   "ParentClass": "UserWidget",
 *   "RootWidget": {
 *     "Type": "CanvasPanel",
 *     "Name": "RootCanvas",
 *     "Children": [
 *       {
 *         "Type": "Button",
 *         "Name": "PlayButton",
 *         "IsVariable": true,
 *         "Slot": {
 *           "Anchors": { "Min": [0.5, 0.5], "Max": [0.5, 0.5] },
 *           "Alignment": [0.5, 0.5]
 *         },
 *         "Children": [
 *           { "Type": "TextBlock", "Properties": { "Text": "Play" } }
 *         ]
 *       }
 *     ]
 *   }
 * }
 */
class ASSETFACTORY_API FWidgetBlueprintGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("WidgetBlueprint"); }
	virtual int32 GetPriority() const override { return 150; }

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

protected:
	//~ Widget Tree Building
	/** Recursively build widget tree from JSON */
	UWidget* BuildWidgetTree(UWidgetBlueprint* Blueprint, TSharedPtr<FJsonObject> WidgetNode, UPanelWidget* Parent, const FString& JsonPath);

	/** Create a widget instance based on type */
	UWidget* CreateWidget(UWidgetBlueprint* Blueprint, const FString& WidgetType, const FString& WidgetName, TSharedPtr<FJsonObject> Config);

	/** Find widget class from shorthand or class path */
	UClass* FindWidgetClass(const FString& TypeString) const;

	//~ Container Widget Creators
	UWidget* CreateCanvasPanel(UWidgetBlueprint* Blueprint, const FString& Name);
	UWidget* CreateVerticalBox(UWidgetBlueprint* Blueprint, const FString& Name);
	UWidget* CreateHorizontalBox(UWidgetBlueprint* Blueprint, const FString& Name);
	UWidget* CreateOverlay(UWidgetBlueprint* Blueprint, const FString& Name);
	UWidget* CreateScrollBox(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);
	UWidget* CreateSizeBox(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);
	UWidget* CreateScaleBox(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);
	UWidget* CreateWidgetSwitcher(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);
	UWidget* CreateGridPanel(UWidgetBlueprint* Blueprint, const FString& Name);

	//~ Basic Widget Creators
	UWidget* CreateTextBlock(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);
	UWidget* CreateRichTextBlock(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);
	UWidget* CreateImage(UWidgetBlueprint* Blueprint, const FString& Name);
	UWidget* CreateButton(UWidgetBlueprint* Blueprint, const FString& Name);
	UWidget* CreateBorder(UWidgetBlueprint* Blueprint, const FString& Name);
	UWidget* CreateSpacer(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);

	//~ Input Widget Creators
	UWidget* CreateEditableTextBox(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);
	UWidget* CreateCheckBox(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);
	UWidget* CreateSlider(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);
	UWidget* CreateProgressBar(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);
	UWidget* CreateComboBoxString(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties);

	//~ Widget Blueprint Instance Creator
	/** Create a widget by loading and instantiating another Widget Blueprint */
	UWidget* CreateWidgetFromBlueprint(UWidgetBlueprint* Blueprint, const FString& WidgetBlueprintPath, const FString& Name, TSharedPtr<FJsonObject> Properties);

	//~ Custom Widget Creator
	UWidget* CreateCustomWidget(UWidgetBlueprint* Blueprint, UClass* WidgetClass, const FString& Name, TSharedPtr<FJsonObject> Properties);

	//~ Slot Configuration
	void ConfigureSlot(UWidget* Widget, UPanelWidget* Parent, TSharedPtr<FJsonObject> SlotConfig);
	void ConfigureCanvasSlot(class UCanvasPanelSlot* Slot, TSharedPtr<FJsonObject> SlotConfig);
	void ConfigureVerticalBoxSlot(class UVerticalBoxSlot* Slot, TSharedPtr<FJsonObject> SlotConfig);
	void ConfigureHorizontalBoxSlot(class UHorizontalBoxSlot* Slot, TSharedPtr<FJsonObject> SlotConfig);
	void ConfigureOverlaySlot(class UOverlaySlot* Slot, TSharedPtr<FJsonObject> SlotConfig);
	void ConfigureScrollBoxSlot(class UScrollBoxSlot* Slot, TSharedPtr<FJsonObject> SlotConfig);
	void ConfigureGridSlot(class UGridSlot* Slot, TSharedPtr<FJsonObject> SlotConfig);

	//~ Style Application
	void ApplyStyle(UWidget* Widget, TSharedPtr<FJsonObject> StyleConfig);
	void ApplyTextBlockStyle(class UTextBlock* TextBlock, TSharedPtr<FJsonObject> StyleConfig);
	void ApplyImageStyle(class UImage* Image, TSharedPtr<FJsonObject> StyleConfig);
	void ApplyBorderStyle(class UBorder* Border, TSharedPtr<FJsonObject> StyleConfig);

	//~ Reflection-based Property Setting (Universal)
	/** Set properties on a widget via reflection - handles all property types */
	void SetPropertiesViaReflection(UWidget* Widget, TSharedPtr<FJsonObject> Properties);

	/** Set properties on any UObject via reflection */
	void SetObjectPropertiesViaReflection(UObject* Object, TSharedPtr<FJsonObject> Properties);

	/** Set a single property value from JSON - handles all types including enums, structs, containers */
	bool SetPropertyValueFromJson(UObject* Object, FProperty* Property, void* ValuePtr, TSharedPtr<FJsonValue> JsonValue);

	/** Set struct property from JSON */
	bool SetStructPropertyFromJson(FStructProperty* StructProp, void* ValuePtr, TSharedPtr<FJsonValue> JsonValue);

	/** Set object reference from path string */
	bool SetObjectReferenceFromPath(FObjectPropertyBase* ObjProp, void* ValuePtr, const FString& ObjectPath);

	//~ Variable Exposure
	void ExposeAsVariable(UWidget* Widget, const FString& VariableName);

	//~ Property Bindings
	/** Configure property bindings for a widget from JSON */
	void ConfigureBindings(UWidgetBlueprint* Blueprint, UWidget* Widget, const FString& WidgetName, TSharedPtr<FJsonObject> BindingsConfig);

	//~ Class Default Properties
	/** Apply class default properties to the Blueprint's CDO */
	void ApplyClassDefaults(UWidgetBlueprint* Blueprint, TSharedPtr<FJsonObject> ClassDefaultsConfig);

	/** Set a single property on the CDO via reflection */
	bool SetCDOProperty(UObject* CDO, FProperty* Property, TSharedPtr<FJsonValue> JsonValue);

	/** Handle TSoftObjectPtr properties */
	bool SetSoftObjectProperty(UObject* CDO, FSoftObjectProperty* Property, const FString& AssetPath);

	/** Handle TSubclassOf properties */
	bool SetClassProperty(UObject* CDO, FClassProperty* Property, const FString& ClassPath);

	/** Handle TMap properties */
	bool SetMapProperty(UObject* CDO, FMapProperty* Property, TSharedPtr<FJsonObject> MapConfig);

	/** Handle TArray properties */
	bool SetArrayProperty(UObject* CDO, FArrayProperty* Property, const TArray<TSharedPtr<FJsonValue>>& ArrayValues);

	//~ Parse Helpers
	FAnchors ParseAnchors(TSharedPtr<FJsonObject> AnchorsConfig) const;
	FMargin ParseMargins(TSharedPtr<FJsonValue> MarginsValue) const;
	FVector2D ParseVector2D(const TArray<TSharedPtr<FJsonValue>>& Array) const;
	FLinearColor ParseColor(TSharedPtr<FJsonValue> ColorValue) const;
	FSlateFontInfo ParseFont(TSharedPtr<FJsonObject> FontConfig) const;
	FSlateBrush ParseBrush(TSharedPtr<FJsonObject> BrushConfig) const;
	EHorizontalAlignment ParseHorizontalAlignment(const FString& AlignString) const;
	EVerticalAlignment ParseVerticalAlignment(const FString& AlignString) const;
	ETextJustify::Type ParseTextJustify(const FString& JustifyString) const;
	ESlateSizeRule::Type ParseSizeRule(const FString& SizeString) const;

	//~ Utility
	FString GenerateWidgetName(const FString& Prefix) const;
	bool IsPanelWidget(UClass* WidgetClass) const;

private:
	/** Counter for generating unique widget names */
	mutable int32 WidgetNameCounter = 0;

	/** Shorthand to class mapping */
	static TMap<FString, UClass*> ShorthandClassMap;
	static bool bShorthandMapInitialized;
	void InitializeShorthandMap() const;
};
