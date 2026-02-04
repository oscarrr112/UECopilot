// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/WidgetBlueprintGenerator.h"
#include "AssetFactoryModule.h"

// Blueprint creation
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Widget Blueprint
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"

// Container Widgets
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"

// Basic Widgets
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/Spacer.h"

// Input Widgets
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/ComboBoxString.h"
#include "Widgets/Notifications/SProgressBar.h"  // For EProgressBarFillType

// Styling
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Engine/Texture2D.h"

// Asset handling
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"

// For loading Widget Blueprints
#include "Engine/Blueprint.h"

// Reflection and property helpers
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"

// Static initialization
TMap<FString, UClass*> FWidgetBlueprintGenerator::ShorthandClassMap;
bool FWidgetBlueprintGenerator::bShorthandMapInitialized = false;

void FWidgetBlueprintGenerator::InitializeShorthandMap() const
{
	if (bShorthandMapInitialized)
	{
		return;
	}

	// Container widgets
	ShorthandClassMap.Add(TEXT("CanvasPanel"), UCanvasPanel::StaticClass());
	ShorthandClassMap.Add(TEXT("VerticalBox"), UVerticalBox::StaticClass());
	ShorthandClassMap.Add(TEXT("HorizontalBox"), UHorizontalBox::StaticClass());
	ShorthandClassMap.Add(TEXT("Overlay"), UOverlay::StaticClass());
	ShorthandClassMap.Add(TEXT("ScrollBox"), UScrollBox::StaticClass());
	ShorthandClassMap.Add(TEXT("SizeBox"), USizeBox::StaticClass());
	ShorthandClassMap.Add(TEXT("ScaleBox"), UScaleBox::StaticClass());
	ShorthandClassMap.Add(TEXT("WidgetSwitcher"), UWidgetSwitcher::StaticClass());
	ShorthandClassMap.Add(TEXT("GridPanel"), UGridPanel::StaticClass());

	// Basic widgets
	ShorthandClassMap.Add(TEXT("TextBlock"), UTextBlock::StaticClass());
	ShorthandClassMap.Add(TEXT("RichTextBlock"), URichTextBlock::StaticClass());
	ShorthandClassMap.Add(TEXT("Image"), UImage::StaticClass());
	ShorthandClassMap.Add(TEXT("Button"), UButton::StaticClass());
	ShorthandClassMap.Add(TEXT("Border"), UBorder::StaticClass());
	ShorthandClassMap.Add(TEXT("Spacer"), USpacer::StaticClass());

	// Input widgets
	ShorthandClassMap.Add(TEXT("EditableTextBox"), UEditableTextBox::StaticClass());
	ShorthandClassMap.Add(TEXT("CheckBox"), UCheckBox::StaticClass());
	ShorthandClassMap.Add(TEXT("Slider"), USlider::StaticClass());
	ShorthandClassMap.Add(TEXT("ProgressBar"), UProgressBar::StaticClass());
	ShorthandClassMap.Add(TEXT("ComboBoxString"), UComboBoxString::StaticClass());

	bShorthandMapInitialized = true;
}

FGenerationResult FWidgetBlueprintGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	WidgetNameCounter = 0;

	const bool bExists = DoesAssetExist(Path, Name);

	// Handle action logic
	if (Action == EGenerationAction::Create && bExists)
	{
		return FGenerationResult::MakeSkipped(GetAssetType(), Name, Path, TEXT("Asset already exists"));
	}

	if (Action == EGenerationAction::Update && !bExists)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Asset does not exist for update"));
	}

	// Get parent class (default to UserWidget)
	FString ParentClassName = GetStringField(Config, TEXT("ParentClass"), TEXT("UserWidget"));
	UClass* ParentClass = nullptr;

	if (ParentClassName == TEXT("UserWidget") || ParentClassName == TEXT("UUserWidget"))
	{
		ParentClass = UUserWidget::StaticClass();
	}
	else
	{
		// Try to find custom parent class
		ParentClass = StaticLoadClass(UUserWidget::StaticClass(), nullptr, *ParentClassName);
		if (!ParentClass)
		{
			ParentClass = UUserWidget::StaticClass();
			UE_LOG(LogAssetFactory, Warning, TEXT("Parent class '%s' not found, using UUserWidget"), *ParentClassName);
		}
	}

	UWidgetBlueprint* Blueprint = nullptr;

	if (bExists)
	{
		// Load existing blueprint for update or overwrite
		Blueprint = Cast<UWidgetBlueprint>(LoadExistingAsset(Path, Name));
		if (!Blueprint)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing widget blueprint"));
		}

		// Update ParentClass if it has changed
		if (Blueprint->ParentClass != ParentClass)
		{
			UE_LOG(LogAssetFactory, Log, TEXT("Updating ParentClass for '%s' from '%s' to '%s'"),
				*Name,
				Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("null"),
				ParentClass ? *ParentClass->GetName() : TEXT("null"));
			Blueprint->ParentClass = ParentClass;
		}

		// Clear existing widget tree for rebuild
		if (Blueprint->WidgetTree)
		{
			Blueprint->WidgetTree->RootWidget = nullptr;
		}
	}
	else
	{
		// Create new widget blueprint
		FString FullPath = Path / Name;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/") + FullPath;
		}

		UPackage* Package = CreatePackage(*FullPath);
		if (!Package)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create package"));
		}

		Blueprint = CastChecked<UWidgetBlueprint>(
			FKismetEditorUtilities::CreateBlueprint(
				ParentClass,
				Package,
				*Name,
				BPTYPE_Normal,
				UWidgetBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass()
			)
		);

		if (!Blueprint)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create widget blueprint"));
		}
	}

	// Get root widget configuration
	TSharedPtr<FJsonObject> RootWidgetConfig = GetObjectField(Config, TEXT("RootWidget"));
	if (!RootWidgetConfig.IsValid())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Missing RootWidget in configuration"));
	}

	// Build widget tree
	UWidget* RootWidget = BuildWidgetTree(Blueprint, RootWidgetConfig, nullptr, TEXT("RootWidget"));
	if (!RootWidget)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to build widget tree"));
	}

	// Set root widget
	Blueprint->WidgetTree->RootWidget = RootWidget;

	// Compile first to generate the class
	Blueprint->Modify();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// Apply class default properties if specified (after compilation so GeneratedClass exists)
	TSharedPtr<FJsonObject> ClassDefaultsConfig = GetObjectField(Config, TEXT("ClassDefaults"));
	if (ClassDefaultsConfig.IsValid() && ClassDefaultsConfig->Values.Num() > 0)
	{
		ApplyClassDefaults(Blueprint, ClassDefaultsConfig);
		// Mark dirty and recompile after setting CDO properties
		Blueprint->Modify();
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	Blueprint->MarkPackageDirty();

	UPackage* Package = Blueprint->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Blueprint, *PackageFileName, SaveArgs);

	if (bExists)
	{
		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, Blueprint);
	}
	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, Blueprint);
}

UWidget* FWidgetBlueprintGenerator::BuildWidgetTree(
	UWidgetBlueprint* Blueprint,
	TSharedPtr<FJsonObject> WidgetNode,
	UPanelWidget* Parent,
	const FString& JsonPath)
{
	if (!WidgetNode.IsValid())
	{
		UE_LOG(LogAssetFactory, Error, TEXT("[%s] Invalid widget node"), *JsonPath);
		return nullptr;
	}

	// Get widget type
	FString WidgetType = GetStringField(WidgetNode, TEXT("Type"));
	if (WidgetType.IsEmpty())
	{
		UE_LOG(LogAssetFactory, Error, TEXT("[%s] Missing Type field"), *JsonPath);
		return nullptr;
	}

	// Get widget name
	FString WidgetName = GetStringField(WidgetNode, TEXT("Name"));
	if (WidgetName.IsEmpty())
	{
		WidgetName = GenerateWidgetName(WidgetType);
	}

	// Get properties
	TSharedPtr<FJsonObject> Properties = GetObjectField(WidgetNode, TEXT("Properties"));

	// Create the widget
	UWidget* Widget = CreateWidget(Blueprint, WidgetType, WidgetName, Properties);
	if (!Widget)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("[%s] Failed to create widget of type '%s'"), *JsonPath, *WidgetType);
		return nullptr;
	}

	// Apply style if present
	TSharedPtr<FJsonObject> StyleConfig = GetObjectField(WidgetNode, TEXT("Style"));
	if (StyleConfig.IsValid())
	{
		ApplyStyle(Widget, StyleConfig);
	}

	// Handle variable exposure
	bool bIsVariable = false;
	if (WidgetNode->TryGetBoolField(TEXT("IsVariable"), bIsVariable) && bIsVariable)
	{
		FString VariableName = GetStringField(WidgetNode, TEXT("VariableName"), WidgetName);
		ExposeAsVariable(Widget, VariableName);
	}

	// Handle property bindings
	TSharedPtr<FJsonObject> BindingsConfig = GetObjectField(WidgetNode, TEXT("Bindings"));
	if (BindingsConfig.IsValid())
	{
		// Widget must be a variable to have bindings
		if (!bIsVariable)
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Widget '%s' has Bindings but IsVariable is false. Setting IsVariable to true."), *JsonPath, *WidgetName);
			ExposeAsVariable(Widget, WidgetName);
		}
		ConfigureBindings(Blueprint, Widget, WidgetName, BindingsConfig);
	}

	// Add to parent if present
	if (Parent)
	{
		Parent->AddChild(Widget);

		// Configure slot
		TSharedPtr<FJsonObject> SlotConfig = GetObjectField(WidgetNode, TEXT("Slot"));
		ConfigureSlot(Widget, Parent, SlotConfig);
	}

	// Process children if this is a panel widget
	UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
	const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = GetArrayField(WidgetNode, TEXT("Children"));

	if (ChildrenArray && ChildrenArray->Num() > 0)
	{
		if (!PanelWidget)
		{
			// Special case: Button and Border can have a single child
			UButton* ButtonWidget = Cast<UButton>(Widget);
			UBorder* BorderWidget = Cast<UBorder>(Widget);

			if (ButtonWidget || BorderWidget)
			{
				// Only process first child
				if ((*ChildrenArray)[0]->Type == EJson::Object)
				{
					TSharedPtr<FJsonObject> ChildNode = (*ChildrenArray)[0]->AsObject();
					FString ChildPath = FString::Printf(TEXT("%s.Children[0]"), *JsonPath);

					UWidget* ChildWidget = BuildWidgetTree(Blueprint, ChildNode, nullptr, ChildPath);
					if (ChildWidget)
					{
						if (ButtonWidget)
						{
							// Button uses AddChild through its internal slot
							UPanelWidget* ButtonPanel = Cast<UPanelWidget>(ButtonWidget);
							if (ButtonPanel)
							{
								ButtonPanel->AddChild(ChildWidget);
							}
						}
						else if (BorderWidget)
						{
							BorderWidget->SetContent(ChildWidget);
						}
					}
				}

				if (ChildrenArray->Num() > 1)
				{
					UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Button/Border can only have one child, ignoring extra children"), *JsonPath);
				}
			}
			else
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Widget type '%s' is not a panel and cannot have children"), *JsonPath, *WidgetType);
			}
		}
		else
		{
			// Process all children for panel widgets
			for (int32 i = 0; i < ChildrenArray->Num(); ++i)
			{
				if ((*ChildrenArray)[i]->Type == EJson::Object)
				{
					TSharedPtr<FJsonObject> ChildNode = (*ChildrenArray)[i]->AsObject();
					FString ChildPath = FString::Printf(TEXT("%s.Children[%d]"), *JsonPath, i);

					BuildWidgetTree(Blueprint, ChildNode, PanelWidget, ChildPath);
				}
			}
		}
	}

	return Widget;
}

UClass* FWidgetBlueprintGenerator::FindWidgetClass(const FString& TypeString) const
{
	InitializeShorthandMap();

	// First check shorthand map
	if (UClass* const* Found = ShorthandClassMap.Find(TypeString))
	{
		return *Found;
	}

	// Try to load as class path
	if (TypeString.StartsWith(TEXT("/Script/")))
	{
		UClass* LoadedClass = StaticLoadClass(UWidget::StaticClass(), nullptr, *TypeString);
		if (LoadedClass)
		{
			return LoadedClass;
		}
	}

	// Try common module paths
	const FString ModulesToSearch[] = {
		TEXT("/Script/UMG"),
		FString::Printf(TEXT("/Script/%s"), FApp::GetProjectName()),
		TEXT("/Script/Engine"),
	};

	for (const FString& ModulePath : ModulesToSearch)
	{
		FString FullPath = FString::Printf(TEXT("%s.U%s"), *ModulePath, *TypeString);
		UClass* FoundClass = StaticLoadClass(UWidget::StaticClass(), nullptr, *FullPath);
		if (FoundClass)
		{
			return FoundClass;
		}

		// Try without U prefix
		FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *TypeString);
		FoundClass = StaticLoadClass(UWidget::StaticClass(), nullptr, *FullPath);
		if (FoundClass)
		{
			return FoundClass;
		}
	}

	return nullptr;
}

UWidget* FWidgetBlueprintGenerator::CreateWidget(
	UWidgetBlueprint* Blueprint,
	const FString& WidgetType,
	const FString& WidgetName,
	TSharedPtr<FJsonObject> Properties)
{
	InitializeShorthandMap();

	// Handle UserWidget type with WidgetClass property (for instantiating other Widget Blueprints)
	if (WidgetType == TEXT("UserWidget") || WidgetType == TEXT("WidgetBlueprint"))
	{
		FString WidgetClassPath;
		if (Properties.IsValid() && Properties->TryGetStringField(TEXT("WidgetClass"), WidgetClassPath))
		{
			return CreateWidgetFromBlueprint(Blueprint, WidgetClassPath, WidgetName, Properties);
		}
		else
		{
			UE_LOG(LogAssetFactory, Error, TEXT("UserWidget type requires 'WidgetClass' property specifying the Widget Blueprint path"));
			return nullptr;
		}
	}

	// Use shorthand creators for known types
	if (WidgetType == TEXT("CanvasPanel")) return CreateCanvasPanel(Blueprint, WidgetName);
	if (WidgetType == TEXT("VerticalBox")) return CreateVerticalBox(Blueprint, WidgetName);
	if (WidgetType == TEXT("HorizontalBox")) return CreateHorizontalBox(Blueprint, WidgetName);
	if (WidgetType == TEXT("Overlay")) return CreateOverlay(Blueprint, WidgetName);
	if (WidgetType == TEXT("ScrollBox")) return CreateScrollBox(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("SizeBox")) return CreateSizeBox(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("ScaleBox")) return CreateScaleBox(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("WidgetSwitcher")) return CreateWidgetSwitcher(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("GridPanel")) return CreateGridPanel(Blueprint, WidgetName);
	if (WidgetType == TEXT("TextBlock")) return CreateTextBlock(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("RichTextBlock")) return CreateRichTextBlock(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("Image")) return CreateImage(Blueprint, WidgetName);
	if (WidgetType == TEXT("Button")) return CreateButton(Blueprint, WidgetName);
	if (WidgetType == TEXT("Border")) return CreateBorder(Blueprint, WidgetName);
	if (WidgetType == TEXT("Spacer")) return CreateSpacer(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("EditableTextBox")) return CreateEditableTextBox(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("CheckBox")) return CreateCheckBox(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("Slider")) return CreateSlider(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("ProgressBar")) return CreateProgressBar(Blueprint, WidgetName, Properties);
	if (WidgetType == TEXT("ComboBoxString")) return CreateComboBoxString(Blueprint, WidgetName, Properties);

	// Try dynamic class loading for unknown types
	UClass* WidgetClass = FindWidgetClass(WidgetType);
	if (WidgetClass)
	{
		return CreateCustomWidget(Blueprint, WidgetClass, WidgetName, Properties);
	}

	UE_LOG(LogAssetFactory, Error, TEXT("Unknown widget type: %s"), *WidgetType);
	return nullptr;
}

//~ Container Widget Creators

UWidget* FWidgetBlueprintGenerator::CreateCanvasPanel(UWidgetBlueprint* Blueprint, const FString& Name)
{
	return Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), *Name);
}

UWidget* FWidgetBlueprintGenerator::CreateVerticalBox(UWidgetBlueprint* Blueprint, const FString& Name)
{
	return Blueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), *Name);
}

UWidget* FWidgetBlueprintGenerator::CreateHorizontalBox(UWidgetBlueprint* Blueprint, const FString& Name)
{
	return Blueprint->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *Name);
}

UWidget* FWidgetBlueprintGenerator::CreateOverlay(UWidgetBlueprint* Blueprint, const FString& Name)
{
	return Blueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *Name);
}

UWidget* FWidgetBlueprintGenerator::CreateScrollBox(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	UScrollBox* ScrollBox = Blueprint->WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		FString Orientation;
		if (Properties->TryGetStringField(TEXT("Orientation"), Orientation))
		{
			if (Orientation == TEXT("Vertical"))
			{
				ScrollBox->SetOrientation(EOrientation::Orient_Vertical);
			}
			else if (Orientation == TEXT("Horizontal"))
			{
				ScrollBox->SetOrientation(EOrientation::Orient_Horizontal);
			}
		}
	}

	return ScrollBox;
}

UWidget* FWidgetBlueprintGenerator::CreateSizeBox(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	USizeBox* SizeBox = Blueprint->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		// SizeBox properties use setter methods that also set the bOverride flags
		// We handle these explicitly for correct behavior
		double Value = 0;
		if (Properties->TryGetNumberField(TEXT("WidthOverride"), Value))
		{
			SizeBox->SetWidthOverride(static_cast<float>(Value));
		}
		if (Properties->TryGetNumberField(TEXT("HeightOverride"), Value))
		{
			SizeBox->SetHeightOverride(static_cast<float>(Value));
		}
		if (Properties->TryGetNumberField(TEXT("MinDesiredWidth"), Value))
		{
			SizeBox->SetMinDesiredWidth(static_cast<float>(Value));
		}
		if (Properties->TryGetNumberField(TEXT("MinDesiredHeight"), Value))
		{
			SizeBox->SetMinDesiredHeight(static_cast<float>(Value));
		}
		if (Properties->TryGetNumberField(TEXT("MaxDesiredWidth"), Value))
		{
			SizeBox->SetMaxDesiredWidth(static_cast<float>(Value));
		}
		if (Properties->TryGetNumberField(TEXT("MaxDesiredHeight"), Value))
		{
			SizeBox->SetMaxDesiredHeight(static_cast<float>(Value));
		}
		if (Properties->TryGetNumberField(TEXT("MinAspectRatio"), Value))
		{
			SizeBox->SetMinAspectRatio(static_cast<float>(Value));
		}
		if (Properties->TryGetNumberField(TEXT("MaxAspectRatio"), Value))
		{
			SizeBox->SetMaxAspectRatio(static_cast<float>(Value));
		}
	}

	return SizeBox;
}

UWidget* FWidgetBlueprintGenerator::CreateScaleBox(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	UScaleBox* ScaleBox = Blueprint->WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		// Use generic reflection for all properties (Stretch enum will be handled automatically)
		SetPropertiesViaReflection(ScaleBox, Properties);
	}

	return ScaleBox;
}

UWidget* FWidgetBlueprintGenerator::CreateWidgetSwitcher(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	UWidgetSwitcher* Switcher = Blueprint->WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		double ActiveIndex = 0;
		if (Properties->TryGetNumberField(TEXT("ActiveWidgetIndex"), ActiveIndex))
		{
			Switcher->SetActiveWidgetIndex(static_cast<int32>(ActiveIndex));
		}
	}

	return Switcher;
}

UWidget* FWidgetBlueprintGenerator::CreateGridPanel(UWidgetBlueprint* Blueprint, const FString& Name)
{
	return Blueprint->WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass(), *Name);
}

//~ Basic Widget Creators

UWidget* FWidgetBlueprintGenerator::CreateTextBlock(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	UTextBlock* TextBlock = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		// Use generic reflection for all properties
		SetPropertiesViaReflection(TextBlock, Properties);
	}

	return TextBlock;
}

UWidget* FWidgetBlueprintGenerator::CreateRichTextBlock(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	URichTextBlock* RichText = Blueprint->WidgetTree->ConstructWidget<URichTextBlock>(URichTextBlock::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		FString Text;
		if (Properties->TryGetStringField(TEXT("Text"), Text))
		{
			RichText->SetText(FText::FromString(Text));
		}
	}

	return RichText;
}

UWidget* FWidgetBlueprintGenerator::CreateImage(UWidgetBlueprint* Blueprint, const FString& Name)
{
	return Blueprint->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *Name);
}

UWidget* FWidgetBlueprintGenerator::CreateButton(UWidgetBlueprint* Blueprint, const FString& Name)
{
	return Blueprint->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *Name);
}

UWidget* FWidgetBlueprintGenerator::CreateBorder(UWidgetBlueprint* Blueprint, const FString& Name)
{
	return Blueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *Name);
}

UWidget* FWidgetBlueprintGenerator::CreateSpacer(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	USpacer* Spacer = Blueprint->WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* SizeArray = nullptr;
		if (Properties->TryGetArrayField(TEXT("Size"), SizeArray) && SizeArray->Num() >= 2)
		{
			FVector2D Size = ParseVector2D(*SizeArray);
			Spacer->SetSize(Size);
		}
	}

	return Spacer;
}

//~ Input Widget Creators

UWidget* FWidgetBlueprintGenerator::CreateEditableTextBox(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	UEditableTextBox* TextBox = Blueprint->WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		FString Text;
		if (Properties->TryGetStringField(TEXT("Text"), Text))
		{
			TextBox->SetText(FText::FromString(Text));
		}

		FString HintText;
		if (Properties->TryGetStringField(TEXT("HintText"), HintText))
		{
			TextBox->SetHintText(FText::FromString(HintText));
		}

		bool bIsPassword = false;
		if (Properties->TryGetBoolField(TEXT("IsPassword"), bIsPassword))
		{
			TextBox->SetIsPassword(bIsPassword);
		}
	}

	return TextBox;
}

UWidget* FWidgetBlueprintGenerator::CreateCheckBox(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	UCheckBox* CheckBox = Blueprint->WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		bool bIsChecked = false;
		if (Properties->TryGetBoolField(TEXT("IsChecked"), bIsChecked))
		{
			CheckBox->SetIsChecked(bIsChecked);
		}
	}

	return CheckBox;
}

UWidget* FWidgetBlueprintGenerator::CreateSlider(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	USlider* Slider = Blueprint->WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		double Value = 0;
		if (Properties->TryGetNumberField(TEXT("Value"), Value))
		{
			Slider->SetValue(static_cast<float>(Value));
		}

		double MinValue = 0;
		if (Properties->TryGetNumberField(TEXT("MinValue"), MinValue))
		{
			Slider->SetMinValue(static_cast<float>(MinValue));
		}

		double MaxValue = 1;
		if (Properties->TryGetNumberField(TEXT("MaxValue"), MaxValue))
		{
			Slider->SetMaxValue(static_cast<float>(MaxValue));
		}
	}

	return Slider;
}

UWidget* FWidgetBlueprintGenerator::CreateProgressBar(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	UProgressBar* ProgressBar = Blueprint->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		// Map common aliases to actual property names
		TSharedPtr<FJsonObject> MappedProperties = MakeShared<FJsonObject>();
		for (const auto& Pair : Properties->Values)
		{
			// "FillType" -> "BarFillType" alias
			if (Pair.Key.Equals(TEXT("FillType"), ESearchCase::IgnoreCase))
			{
				MappedProperties->SetField(TEXT("BarFillType"), Pair.Value);
			}
			else
			{
				MappedProperties->SetField(Pair.Key, Pair.Value);
			}
		}

		// Use generic reflection for all properties
		SetPropertiesViaReflection(ProgressBar, MappedProperties);
	}

	return ProgressBar;
}

UWidget* FWidgetBlueprintGenerator::CreateComboBoxString(UWidgetBlueprint* Blueprint, const FString& Name, TSharedPtr<FJsonObject> Properties)
{
	UComboBoxString* ComboBox = Blueprint->WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), *Name);

	if (Properties.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* OptionsArray = nullptr;
		if (Properties->TryGetArrayField(TEXT("Options"), OptionsArray))
		{
			for (const TSharedPtr<FJsonValue>& Option : *OptionsArray)
			{
				FString OptionStr;
				if (Option->TryGetString(OptionStr))
				{
					ComboBox->AddOption(OptionStr);
				}
			}
		}

		FString SelectedOption;
		if (Properties->TryGetStringField(TEXT("SelectedOption"), SelectedOption))
		{
			ComboBox->SetSelectedOption(SelectedOption);
		}
	}

	return ComboBox;
}

//~ Widget Blueprint Instance Creator

UWidget* FWidgetBlueprintGenerator::CreateWidgetFromBlueprint(
	UWidgetBlueprint* Blueprint,
	const FString& WidgetBlueprintPath,
	const FString& Name,
	TSharedPtr<FJsonObject> Properties)
{
	// Create a copy of Properties without WidgetClass (it's not an actual widget property)
	TSharedPtr<FJsonObject> FilteredProperties;
	if (Properties.IsValid())
	{
		FilteredProperties = MakeShared<FJsonObject>();
		for (const auto& Pair : Properties->Values)
		{
			if (Pair.Key != TEXT("WidgetClass"))
			{
				FilteredProperties->SetField(Pair.Key, Pair.Value);
			}
		}
	}

	// Load the Widget Blueprint asset
	UWidgetBlueprint* SourceBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *WidgetBlueprintPath);
	if (!SourceBlueprint)
	{
		// Try with _C suffix for Blueprint Generated Class
		FString ClassPath = WidgetBlueprintPath;
		if (!ClassPath.EndsWith(TEXT("_C")))
		{
			ClassPath += TEXT("_C");
		}

		UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, *ClassPath);
		if (WidgetClass)
		{
			UWidget* Widget = Blueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, *Name);
			if (Widget && FilteredProperties.IsValid() && FilteredProperties->Values.Num() > 0)
			{
				SetPropertiesViaReflection(Widget, FilteredProperties);
			}
			return Widget;
		}

		UE_LOG(LogAssetFactory, Error, TEXT("Failed to load Widget Blueprint: %s"), *WidgetBlueprintPath);
		return nullptr;
	}

	// Get the generated class from the Blueprint
	UClass* GeneratedClass = SourceBlueprint->GeneratedClass;
	if (!GeneratedClass)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Widget Blueprint '%s' has no generated class"), *WidgetBlueprintPath);
		return nullptr;
	}

	// Create the widget instance
	UWidget* Widget = Blueprint->WidgetTree->ConstructWidget<UWidget>(GeneratedClass, *Name);
	if (Widget && FilteredProperties.IsValid() && FilteredProperties->Values.Num() > 0)
	{
		SetPropertiesViaReflection(Widget, FilteredProperties);
	}

	return Widget;
}

//~ Custom Widget Creator

UWidget* FWidgetBlueprintGenerator::CreateCustomWidget(
	UWidgetBlueprint* Blueprint,
	UClass* WidgetClass,
	const FString& Name,
	TSharedPtr<FJsonObject> Properties)
{
	if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass()))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Invalid widget class for custom widget: %s"), *Name);
		return nullptr;
	}

	UWidget* Widget = Blueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, *Name);

	if (Widget && Properties.IsValid())
	{
		SetPropertiesViaReflection(Widget, Properties);
	}

	return Widget;
}

//~ Slot Configuration

void FWidgetBlueprintGenerator::ConfigureSlot(UWidget* Widget, UPanelWidget* Parent, TSharedPtr<FJsonObject> SlotConfig)
{
	if (!Widget || !Parent || !Widget->Slot)
	{
		return;
	}

	// Canvas Panel Slot
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
	{
		ConfigureCanvasSlot(CanvasSlot, SlotConfig);
		return;
	}

	// Vertical Box Slot
	if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
	{
		ConfigureVerticalBoxSlot(VBoxSlot, SlotConfig);
		return;
	}

	// Horizontal Box Slot
	if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
	{
		ConfigureHorizontalBoxSlot(HBoxSlot, SlotConfig);
		return;
	}

	// Overlay Slot
	if (UOverlaySlot* OvSlot = Cast<UOverlaySlot>(Widget->Slot))
	{
		ConfigureOverlaySlot(OvSlot, SlotConfig);
		return;
	}

	// Scroll Box Slot
	if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(Widget->Slot))
	{
		ConfigureScrollBoxSlot(ScrollSlot, SlotConfig);
		return;
	}

	// Grid Slot
	if (UGridSlot* GridSlot = Cast<UGridSlot>(Widget->Slot))
	{
		ConfigureGridSlot(GridSlot, SlotConfig);
		return;
	}
}

void FWidgetBlueprintGenerator::ConfigureCanvasSlot(UCanvasPanelSlot* Slot, TSharedPtr<FJsonObject> SlotConfig)
{
	if (!Slot)
	{
		return;
	}

	// Default values
	FAnchors Anchors(0.0f, 0.0f, 0.0f, 0.0f);
	FMargin Offsets(0.0f, 0.0f, 100.0f, 100.0f);
	FVector2D Alignment(0.0f, 0.0f);
	bool bSizeToContent = false;

	if (SlotConfig.IsValid())
	{
		// Parse anchors
		if (SlotConfig->HasTypedField<EJson::Object>(TEXT("Anchors")))
		{
			TSharedPtr<FJsonObject> AnchorsConfig = SlotConfig->GetObjectField(TEXT("Anchors"));
			if (AnchorsConfig.IsValid())
			{
				Anchors = ParseAnchors(AnchorsConfig);
			}
		}

		// Parse offsets
		if (SlotConfig->HasField(TEXT("Offsets")))
		{
			Offsets = ParseMargins(SlotConfig->TryGetField(TEXT("Offsets")));
		}

		// Parse alignment
		const TArray<TSharedPtr<FJsonValue>>* AlignmentArray = nullptr;
		if (SlotConfig->TryGetArrayField(TEXT("Alignment"), AlignmentArray) && AlignmentArray->Num() >= 2)
		{
			Alignment = ParseVector2D(*AlignmentArray);
		}

		// Parse size to content
		SlotConfig->TryGetBoolField(TEXT("SizeToContent"), bSizeToContent);
	}

	Slot->SetAnchors(Anchors);
	Slot->SetOffsets(Offsets);
	Slot->SetAlignment(Alignment);
	Slot->SetAutoSize(bSizeToContent);
}

void FWidgetBlueprintGenerator::ConfigureVerticalBoxSlot(UVerticalBoxSlot* Slot, TSharedPtr<FJsonObject> SlotConfig)
{
	if (!Slot)
	{
		return;
	}

	if (SlotConfig.IsValid())
	{
		// Padding
		if (SlotConfig->HasField(TEXT("Padding")))
		{
			FMargin Padding = ParseMargins(SlotConfig->TryGetField(TEXT("Padding")));
			Slot->SetPadding(Padding);
		}

		// Size - supports both string format "Fill" and object format { "SizeRule": "Fill" }
		if (SlotConfig->HasField(TEXT("Size")))
		{
			TSharedPtr<FJsonValue> SizeValue = SlotConfig->TryGetField(TEXT("Size"));
			if (SizeValue->Type == EJson::String)
			{
				FString SizeStr = SizeValue->AsString();
				Slot->SetSize(ParseSizeRule(SizeStr));
			}
			else if (SizeValue->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> SizeObj = SizeValue->AsObject();
				FString SizeRule;
				if (SizeObj->TryGetStringField(TEXT("SizeRule"), SizeRule))
				{
					Slot->SetSize(ParseSizeRule(SizeRule));
				}
			}
		}

		// Horizontal Alignment
		FString HAlign;
		if (SlotConfig->TryGetStringField(TEXT("HAlign"), HAlign))
		{
			Slot->SetHorizontalAlignment(ParseHorizontalAlignment(HAlign));
		}

		// Vertical Alignment
		FString VAlign;
		if (SlotConfig->TryGetStringField(TEXT("VAlign"), VAlign))
		{
			Slot->SetVerticalAlignment(ParseVerticalAlignment(VAlign));
		}
	}
}

void FWidgetBlueprintGenerator::ConfigureHorizontalBoxSlot(UHorizontalBoxSlot* Slot, TSharedPtr<FJsonObject> SlotConfig)
{
	if (!Slot)
	{
		return;
	}

	if (SlotConfig.IsValid())
	{
		// Padding
		if (SlotConfig->HasField(TEXT("Padding")))
		{
			FMargin Padding = ParseMargins(SlotConfig->TryGetField(TEXT("Padding")));
			Slot->SetPadding(Padding);
		}

		// Size - supports both string format "Fill" and object format { "SizeRule": "Fill" }
		if (SlotConfig->HasField(TEXT("Size")))
		{
			TSharedPtr<FJsonValue> SizeValue = SlotConfig->TryGetField(TEXT("Size"));
			if (SizeValue->Type == EJson::String)
			{
				FString SizeStr = SizeValue->AsString();
				Slot->SetSize(ParseSizeRule(SizeStr));
			}
			else if (SizeValue->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> SizeObj = SizeValue->AsObject();
				FString SizeRule;
				if (SizeObj->TryGetStringField(TEXT("SizeRule"), SizeRule))
				{
					Slot->SetSize(ParseSizeRule(SizeRule));
				}
			}
		}

		// Horizontal Alignment
		FString HAlign;
		if (SlotConfig->TryGetStringField(TEXT("HAlign"), HAlign))
		{
			Slot->SetHorizontalAlignment(ParseHorizontalAlignment(HAlign));
		}

		// Vertical Alignment
		FString VAlign;
		if (SlotConfig->TryGetStringField(TEXT("VAlign"), VAlign))
		{
			Slot->SetVerticalAlignment(ParseVerticalAlignment(VAlign));
		}
	}
}

void FWidgetBlueprintGenerator::ConfigureOverlaySlot(UOverlaySlot* Slot, TSharedPtr<FJsonObject> SlotConfig)
{
	if (!Slot)
	{
		return;
	}

	if (SlotConfig.IsValid())
	{
		// Padding
		if (SlotConfig->HasField(TEXT("Padding")))
		{
			FMargin Padding = ParseMargins(SlotConfig->TryGetField(TEXT("Padding")));
			Slot->SetPadding(Padding);
		}

		// Horizontal Alignment
		FString HAlign;
		if (SlotConfig->TryGetStringField(TEXT("HAlign"), HAlign))
		{
			Slot->SetHorizontalAlignment(ParseHorizontalAlignment(HAlign));
		}

		// Vertical Alignment
		FString VAlign;
		if (SlotConfig->TryGetStringField(TEXT("VAlign"), VAlign))
		{
			Slot->SetVerticalAlignment(ParseVerticalAlignment(VAlign));
		}
	}
}

void FWidgetBlueprintGenerator::ConfigureScrollBoxSlot(UScrollBoxSlot* Slot, TSharedPtr<FJsonObject> SlotConfig)
{
	if (!Slot)
	{
		return;
	}

	if (SlotConfig.IsValid())
	{
		// Padding
		if (SlotConfig->HasField(TEXT("Padding")))
		{
			FMargin Padding = ParseMargins(SlotConfig->TryGetField(TEXT("Padding")));
			Slot->SetPadding(Padding);
		}

		// Horizontal Alignment
		FString HAlign;
		if (SlotConfig->TryGetStringField(TEXT("HAlign"), HAlign))
		{
			Slot->SetHorizontalAlignment(ParseHorizontalAlignment(HAlign));
		}
	}
}

void FWidgetBlueprintGenerator::ConfigureGridSlot(UGridSlot* Slot, TSharedPtr<FJsonObject> SlotConfig)
{
	if (!Slot)
	{
		return;
	}

	if (SlotConfig.IsValid())
	{
		// Row
		double Row = 0;
		if (SlotConfig->TryGetNumberField(TEXT("Row"), Row))
		{
			Slot->SetRow(static_cast<int32>(Row));
		}

		// Column
		double Column = 0;
		if (SlotConfig->TryGetNumberField(TEXT("Column"), Column))
		{
			Slot->SetColumn(static_cast<int32>(Column));
		}

		// Row Span
		double RowSpan = 1;
		if (SlotConfig->TryGetNumberField(TEXT("RowSpan"), RowSpan))
		{
			Slot->SetRowSpan(static_cast<int32>(RowSpan));
		}

		// Column Span
		double ColumnSpan = 1;
		if (SlotConfig->TryGetNumberField(TEXT("ColumnSpan"), ColumnSpan))
		{
			Slot->SetColumnSpan(static_cast<int32>(ColumnSpan));
		}

		// Padding
		if (SlotConfig->HasField(TEXT("Padding")))
		{
			FMargin Padding = ParseMargins(SlotConfig->TryGetField(TEXT("Padding")));
			Slot->SetPadding(Padding);
		}

		// Horizontal Alignment
		FString HAlign;
		if (SlotConfig->TryGetStringField(TEXT("HAlign"), HAlign))
		{
			Slot->SetHorizontalAlignment(ParseHorizontalAlignment(HAlign));
		}

		// Vertical Alignment
		FString VAlign;
		if (SlotConfig->TryGetStringField(TEXT("VAlign"), VAlign))
		{
			Slot->SetVerticalAlignment(ParseVerticalAlignment(VAlign));
		}
	}
}

//~ Style Application

void FWidgetBlueprintGenerator::ApplyStyle(UWidget* Widget, TSharedPtr<FJsonObject> StyleConfig)
{
	if (!Widget || !StyleConfig.IsValid())
	{
		return;
	}

	// TextBlock styling
	if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
	{
		ApplyTextBlockStyle(TextBlock, StyleConfig);
		return;
	}

	// Image styling
	if (UImage* Image = Cast<UImage>(Widget))
	{
		ApplyImageStyle(Image, StyleConfig);
		return;
	}

	// Border styling
	if (UBorder* Border = Cast<UBorder>(Widget))
	{
		ApplyBorderStyle(Border, StyleConfig);
		return;
	}
}

void FWidgetBlueprintGenerator::ApplyTextBlockStyle(UTextBlock* TextBlock, TSharedPtr<FJsonObject> StyleConfig)
{
	if (!TextBlock || !StyleConfig.IsValid())
	{
		return;
	}

	// Color
	if (StyleConfig->HasField(TEXT("Color")))
	{
		FLinearColor Color = ParseColor(StyleConfig->TryGetField(TEXT("Color")));
		TextBlock->SetColorAndOpacity(FSlateColor(Color));
	}

	// Font
	if (StyleConfig->HasTypedField<EJson::Object>(TEXT("Font")))
	{
		TSharedPtr<FJsonObject> FontConfig = StyleConfig->GetObjectField(TEXT("Font"));
		if (FontConfig.IsValid())
		{
			FSlateFontInfo Font = TextBlock->GetFont();

			double Size = 0;
			if (FontConfig->TryGetNumberField(TEXT("Size"), Size))
			{
				Font.Size = static_cast<int32>(Size);
			}

			FString Family;
			if (FontConfig->TryGetStringField(TEXT("Family"), Family))
			{
				// Font family handling - would need font asset loading
				// For now, just use the default font with specified size
			}

			TextBlock->SetFont(Font);
		}
	}
}

void FWidgetBlueprintGenerator::ApplyImageStyle(UImage* Image, TSharedPtr<FJsonObject> StyleConfig)
{
	if (!Image || !StyleConfig.IsValid())
	{
		return;
	}

	if (StyleConfig->HasTypedField<EJson::Object>(TEXT("Brush")))
	{
		TSharedPtr<FJsonObject> BrushConfig = StyleConfig->GetObjectField(TEXT("Brush"));
		if (BrushConfig.IsValid())
		{
			FSlateBrush Brush = ParseBrush(BrushConfig);
			Image->SetBrush(Brush);
		}
	}

	// Direct color/tint
	if (StyleConfig->HasField(TEXT("Color")))
	{
		FLinearColor Color = ParseColor(StyleConfig->TryGetField(TEXT("Color")));
		Image->SetColorAndOpacity(Color);
	}
}

void FWidgetBlueprintGenerator::ApplyBorderStyle(UBorder* Border, TSharedPtr<FJsonObject> StyleConfig)
{
	if (!Border || !StyleConfig.IsValid())
	{
		return;
	}

	if (StyleConfig->HasTypedField<EJson::Object>(TEXT("Brush")))
	{
		TSharedPtr<FJsonObject> BrushConfig = StyleConfig->GetObjectField(TEXT("Brush"));
		if (BrushConfig.IsValid())
		{
			FSlateBrush Brush = ParseBrush(BrushConfig);
			Border->SetBrush(Brush);
		}
	}

	// Background color
	if (StyleConfig->HasField(TEXT("Color")))
	{
		FLinearColor Color = ParseColor(StyleConfig->TryGetField(TEXT("Color")));
		Border->SetBrushColor(Color);
	}
}

//~ Reflection-based Property Setting

void FWidgetBlueprintGenerator::SetPropertiesViaReflection(UWidget* Widget, TSharedPtr<FJsonObject> Properties)
{
	if (!Widget || !Properties.IsValid())
	{
		return;
	}

	SetObjectPropertiesViaReflection(Widget, Properties);
}

void FWidgetBlueprintGenerator::SetObjectPropertiesViaReflection(UObject* Object, TSharedPtr<FJsonObject> Properties)
{
	if (!Object || !Properties.IsValid())
	{
		return;
	}

	UClass* ObjectClass = Object->GetClass();

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropertyName = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Pair.Value;

		FProperty* Property = ObjectClass->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s' not found on class '%s'"), *PropertyName, *ObjectClass->GetName());
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		SetPropertyValueFromJson(Object, Property, ValuePtr, JsonValue);
	}
}

bool FWidgetBlueprintGenerator::SetPropertyValueFromJson(UObject* Object, FProperty* Property, void* ValuePtr, TSharedPtr<FJsonValue> JsonValue)
{
	if (!Property || !ValuePtr || !JsonValue.IsValid())
	{
		return false;
	}

	// Handle numeric types (int, float, double, etc.)
	if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		double Value = 0.0;
		if (JsonValue->TryGetNumber(Value))
		{
			if (NumericProp->IsFloatingPoint())
			{
				NumericProp->SetFloatingPointPropertyValue(ValuePtr, Value);
			}
			else
			{
				NumericProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value));
			}
			return true;
		}
	}
	// Handle bool
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool Value = false;
		if (JsonValue->TryGetBool(Value))
		{
			BoolProp->SetPropertyValue(ValuePtr, Value);
			return true;
		}
	}
	// Handle FString
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			StrProp->SetPropertyValue(ValuePtr, Value);
			return true;
		}
	}
	// Handle FName
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			NameProp->SetPropertyValue(ValuePtr, FName(*Value));
			return true;
		}
	}
	// Handle FText
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			TextProp->SetPropertyValue(ValuePtr, FText::FromString(Value));
			return true;
		}
	}
	// Handle Enum (both FEnumProperty and FByteProperty with enum)
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FString EnumValueStr;
		if (JsonValue->TryGetString(EnumValueStr))
		{
			UEnum* Enum = EnumProp->GetEnum();
			int64 EnumValue = Enum->GetValueByNameString(EnumValueStr);
			if (EnumValue == INDEX_NONE)
			{
				// Try with enum prefix
				EnumValue = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + EnumValueStr);
			}
			if (EnumValue != INDEX_NONE)
			{
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
				return true;
			}
			else
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Invalid enum value '%s' for property '%s'"), *EnumValueStr, *Property->GetName());
			}
		}
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (UEnum* Enum = ByteProp->Enum)
		{
			FString EnumValueStr;
			if (JsonValue->TryGetString(EnumValueStr))
			{
				int64 EnumValue = Enum->GetValueByNameString(EnumValueStr);
				if (EnumValue == INDEX_NONE)
				{
					EnumValue = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + EnumValueStr);
				}
				if (EnumValue != INDEX_NONE)
				{
					ByteProp->SetIntPropertyValue(ValuePtr, EnumValue);
					return true;
				}
			}
		}
		else
		{
			// Plain byte, treat as number
			double Value = 0.0;
			if (JsonValue->TryGetNumber(Value))
			{
				ByteProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value));
				return true;
			}
		}
	}
	// Handle Struct types
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return SetStructPropertyFromJson(StructProp, ValuePtr, JsonValue);
	}
	// Handle Object references (UObject*, TSoftObjectPtr, TSubclassOf)
	else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		FString ObjectPath;
		if (JsonValue->TryGetString(ObjectPath))
		{
			return SetObjectReferenceFromPath(ObjProp, ValuePtr, ObjectPath);
		}
	}
	// Handle Soft Object Ptr
	else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		FString ObjectPath;
		if (JsonValue->TryGetString(ObjectPath))
		{
			return SetSoftObjectProperty(Object, SoftObjProp, ObjectPath);
		}
	}
	// Handle TSubclassOf
	else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		FString ClassPath;
		if (JsonValue->TryGetString(ClassPath))
		{
			return SetClassProperty(Object, ClassProp, ClassPath);
		}
	}
	// Handle TArray
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues;
		if (JsonValue->TryGetArray(ArrayValues))
		{
			return SetArrayProperty(Object, ArrayProp, *ArrayValues);
		}
	}
	// Handle TMap
	else if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		const TSharedPtr<FJsonObject>* MapObject;
		if (JsonValue->TryGetObject(MapObject))
		{
			return SetMapProperty(Object, MapProp, *MapObject);
		}
	}

	return false;
}

bool FWidgetBlueprintGenerator::SetStructPropertyFromJson(FStructProperty* StructProp, void* ValuePtr, TSharedPtr<FJsonValue> JsonValue)
{
	UScriptStruct* Struct = StructProp->Struct;

	// FLinearColor - [R, G, B, A] array or {R, G, B, A} object
	if (Struct == TBaseStructure<FLinearColor>::Get())
	{
		FLinearColor Color = ParseColor(JsonValue);
		*static_cast<FLinearColor*>(ValuePtr) = Color;
		return true;
	}
	// FColor
	else if (Struct == TBaseStructure<FColor>::Get())
	{
		FLinearColor Color = ParseColor(JsonValue);
		*static_cast<FColor*>(ValuePtr) = Color.ToFColor(true);
		return true;
	}
	// FVector2D - [X, Y] array
	else if (Struct == TBaseStructure<FVector2D>::Get())
	{
		const TArray<TSharedPtr<FJsonValue>>* Array;
		if (JsonValue->TryGetArray(Array) && Array->Num() >= 2)
		{
			*static_cast<FVector2D*>(ValuePtr) = ParseVector2D(*Array);
			return true;
		}
	}
	// FVector - [X, Y, Z] array
	else if (Struct == TBaseStructure<FVector>::Get())
	{
		const TArray<TSharedPtr<FJsonValue>>* Array;
		if (JsonValue->TryGetArray(Array) && Array->Num() >= 3)
		{
			FVector& Vec = *static_cast<FVector*>(ValuePtr);
			Vec.X = (*Array)[0]->AsNumber();
			Vec.Y = (*Array)[1]->AsNumber();
			Vec.Z = (*Array)[2]->AsNumber();
			return true;
		}
	}
	// FMargin - number, [H, V], [L, T, R, B], or {Left, Top, Right, Bottom}
	else if (Struct == TBaseStructure<FMargin>::Get())
	{
		*static_cast<FMargin*>(ValuePtr) = ParseMargins(JsonValue);
		return true;
	}
	// FSlateColor
	else if (Struct->GetFName() == TEXT("SlateColor"))
	{
		FLinearColor Color = ParseColor(JsonValue);
		FSlateColor* SlateColor = static_cast<FSlateColor*>(ValuePtr);
		*SlateColor = FSlateColor(Color);
		return true;
	}
	// FSlateFontInfo
	else if (Struct->GetFName() == TEXT("SlateFontInfo"))
	{
		const TSharedPtr<FJsonObject>* FontObj;
		if (JsonValue->TryGetObject(FontObj))
		{
			*static_cast<FSlateFontInfo*>(ValuePtr) = ParseFont(*FontObj);
			return true;
		}
	}
	// FSlateBrush
	else if (Struct->GetFName() == TEXT("SlateBrush"))
	{
		const TSharedPtr<FJsonObject>* BrushObj;
		if (JsonValue->TryGetObject(BrushObj))
		{
			*static_cast<FSlateBrush*>(ValuePtr) = ParseBrush(*BrushObj);
			return true;
		}
	}
	// FAnchors
	else if (Struct->GetFName() == TEXT("Anchors"))
	{
		const TSharedPtr<FJsonObject>* AnchorsObj;
		if (JsonValue->TryGetObject(AnchorsObj))
		{
			*static_cast<FAnchors*>(ValuePtr) = ParseAnchors(*AnchorsObj);
			return true;
		}
	}
	// Generic struct - try to set fields recursively
	else
	{
		const TSharedPtr<FJsonObject>* StructObj;
		if (JsonValue->TryGetObject(StructObj))
		{
			for (const auto& Pair : (*StructObj)->Values)
			{
				FProperty* FieldProp = Struct->FindPropertyByName(*Pair.Key);
				if (FieldProp)
				{
					void* FieldPtr = FieldProp->ContainerPtrToValuePtr<void>(ValuePtr);
					SetPropertyValueFromJson(nullptr, FieldProp, FieldPtr, Pair.Value);
				}
			}
			return true;
		}
	}

	return false;
}

bool FWidgetBlueprintGenerator::SetObjectReferenceFromPath(FObjectPropertyBase* ObjProp, void* ValuePtr, const FString& ObjectPath)
{
	if (ObjectPath.IsEmpty())
	{
		ObjProp->SetObjectPropertyValue(ValuePtr, nullptr);
		return true;
	}

	UObject* LoadedObject = StaticLoadObject(ObjProp->PropertyClass, nullptr, *ObjectPath);
	if (LoadedObject)
	{
		ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObject);
		return true;
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load object: %s"), *ObjectPath);
	return false;
}

//~ Variable Exposure

void FWidgetBlueprintGenerator::ExposeAsVariable(UWidget* Widget, const FString& VariableName)
{
	if (!Widget)
	{
		return;
	}

	Widget->bIsVariable = true;

	// Rename widget if variable name is different
	if (!VariableName.IsEmpty() && VariableName != Widget->GetName())
	{
		Widget->Rename(*VariableName);
	}
}

//~ Parse Helpers

FAnchors FWidgetBlueprintGenerator::ParseAnchors(TSharedPtr<FJsonObject> AnchorsConfig) const
{
	FAnchors Result;

	if (!AnchorsConfig.IsValid())
	{
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* MinArray = nullptr;
	if (AnchorsConfig->TryGetArrayField(TEXT("Min"), MinArray) && MinArray->Num() >= 2)
	{
		Result.Minimum = ParseVector2D(*MinArray);
	}

	const TArray<TSharedPtr<FJsonValue>>* MaxArray = nullptr;
	if (AnchorsConfig->TryGetArrayField(TEXT("Max"), MaxArray) && MaxArray->Num() >= 2)
	{
		Result.Maximum = ParseVector2D(*MaxArray);
	}

	return Result;
}

FMargin FWidgetBlueprintGenerator::ParseMargins(TSharedPtr<FJsonValue> MarginsValue) const
{
	FMargin Result(0.0f);

	if (!MarginsValue.IsValid())
	{
		return Result;
	}

	// Single number - uniform margin
	double UniformValue = 0;
	if (MarginsValue->TryGetNumber(UniformValue))
	{
		return FMargin(static_cast<float>(UniformValue));
	}

	// Array format [Left, Top, Right, Bottom]
	const TArray<TSharedPtr<FJsonValue>>* MarginsArray = nullptr;
	if (MarginsValue->TryGetArray(MarginsArray) && MarginsArray->Num() >= 4)
	{
		double Left = 0, Top = 0, Right = 0, Bottom = 0;
		(*MarginsArray)[0]->TryGetNumber(Left);
		(*MarginsArray)[1]->TryGetNumber(Top);
		(*MarginsArray)[2]->TryGetNumber(Right);
		(*MarginsArray)[3]->TryGetNumber(Bottom);
		return FMargin(static_cast<float>(Left), static_cast<float>(Top), static_cast<float>(Right), static_cast<float>(Bottom));
	}

	// Object format {Left, Top, Right, Bottom}
	const TSharedPtr<FJsonObject>* MarginsObject = nullptr;
	if (MarginsValue->TryGetObject(MarginsObject))
	{
		double Left = 0, Top = 0, Right = 0, Bottom = 0;
		(*MarginsObject)->TryGetNumberField(TEXT("Left"), Left);
		(*MarginsObject)->TryGetNumberField(TEXT("Top"), Top);
		(*MarginsObject)->TryGetNumberField(TEXT("Right"), Right);
		(*MarginsObject)->TryGetNumberField(TEXT("Bottom"), Bottom);
		return FMargin(static_cast<float>(Left), static_cast<float>(Top), static_cast<float>(Right), static_cast<float>(Bottom));
	}

	return Result;
}

FVector2D FWidgetBlueprintGenerator::ParseVector2D(const TArray<TSharedPtr<FJsonValue>>& Array) const
{
	FVector2D Result(0.0f, 0.0f);

	if (Array.Num() >= 2)
	{
		double X = 0, Y = 0;
		Array[0]->TryGetNumber(X);
		Array[1]->TryGetNumber(Y);
		Result = FVector2D(static_cast<float>(X), static_cast<float>(Y));
	}

	return Result;
}

FLinearColor FWidgetBlueprintGenerator::ParseColor(TSharedPtr<FJsonValue> ColorValue) const
{
	FLinearColor Result = FLinearColor::White;

	if (!ColorValue.IsValid())
	{
		return Result;
	}

	// String format - hex or named
	FString ColorStr;
	if (ColorValue->TryGetString(ColorStr))
	{
		// Hex format
		if (ColorStr.StartsWith(TEXT("#")))
		{
			FColor ParsedColor = FColor::FromHex(ColorStr);
			return FLinearColor(ParsedColor);
		}

		// Named colors
		if (ColorStr == TEXT("White")) return FLinearColor::White;
		if (ColorStr == TEXT("Black")) return FLinearColor::Black;
		if (ColorStr == TEXT("Red")) return FLinearColor::Red;
		if (ColorStr == TEXT("Green")) return FLinearColor::Green;
		if (ColorStr == TEXT("Blue")) return FLinearColor::Blue;
		if (ColorStr == TEXT("Yellow")) return FLinearColor::Yellow;
		if (ColorStr == TEXT("Transparent")) return FLinearColor::Transparent;
	}

	// Object format {R, G, B, A}
	const TSharedPtr<FJsonObject>* ColorObject = nullptr;
	if (ColorValue->TryGetObject(ColorObject))
	{
		double R = 1, G = 1, B = 1, A = 1;
		(*ColorObject)->TryGetNumberField(TEXT("R"), R);
		(*ColorObject)->TryGetNumberField(TEXT("G"), G);
		(*ColorObject)->TryGetNumberField(TEXT("B"), B);
		(*ColorObject)->TryGetNumberField(TEXT("A"), A);
		return FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
	}

	return Result;
}

FSlateFontInfo FWidgetBlueprintGenerator::ParseFont(TSharedPtr<FJsonObject> FontConfig) const
{
	FSlateFontInfo Font;

	if (FontConfig.IsValid())
	{
		double Size = 12;
		if (FontConfig->TryGetNumberField(TEXT("Size"), Size))
		{
			Font.Size = static_cast<int32>(Size);
		}
	}

	return Font;
}

FSlateBrush FWidgetBlueprintGenerator::ParseBrush(TSharedPtr<FJsonObject> BrushConfig) const
{
	FSlateBrush Brush;

	if (!BrushConfig.IsValid())
	{
		return Brush;
	}

	// Load image texture
	FString ImagePath;
	if (BrushConfig->TryGetStringField(TEXT("Image"), ImagePath))
	{
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ImagePath);
		if (Texture)
		{
			Brush.SetResourceObject(Texture);
		}
		else
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load texture: %s"), *ImagePath);
		}
	}

	// Tint color
	if (BrushConfig->HasField(TEXT("Tint")))
	{
		FLinearColor Tint = ParseColor(BrushConfig->TryGetField(TEXT("Tint")));
		Brush.TintColor = FSlateColor(Tint);
	}

	// Draw type
	FString DrawAs;
	if (BrushConfig->TryGetStringField(TEXT("DrawAs"), DrawAs))
	{
		if (DrawAs == TEXT("Box")) Brush.DrawAs = ESlateBrushDrawType::Box;
		else if (DrawAs == TEXT("Image")) Brush.DrawAs = ESlateBrushDrawType::Image;
		else if (DrawAs == TEXT("Border")) Brush.DrawAs = ESlateBrushDrawType::Border;
		else if (DrawAs == TEXT("NoDrawType")) Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
	}

	return Brush;
}

EHorizontalAlignment FWidgetBlueprintGenerator::ParseHorizontalAlignment(const FString& AlignString) const
{
	if (AlignString == TEXT("Left")) return HAlign_Left;
	if (AlignString == TEXT("Center")) return HAlign_Center;
	if (AlignString == TEXT("Right")) return HAlign_Right;
	if (AlignString == TEXT("Fill")) return HAlign_Fill;
	return HAlign_Fill;
}

EVerticalAlignment FWidgetBlueprintGenerator::ParseVerticalAlignment(const FString& AlignString) const
{
	if (AlignString == TEXT("Top")) return VAlign_Top;
	if (AlignString == TEXT("Center")) return VAlign_Center;
	if (AlignString == TEXT("Bottom")) return VAlign_Bottom;
	if (AlignString == TEXT("Fill")) return VAlign_Fill;
	return VAlign_Fill;
}

ETextJustify::Type FWidgetBlueprintGenerator::ParseTextJustify(const FString& JustifyString) const
{
	if (JustifyString == TEXT("Left")) return ETextJustify::Left;
	if (JustifyString == TEXT("Center")) return ETextJustify::Center;
	if (JustifyString == TEXT("Right")) return ETextJustify::Right;
	return ETextJustify::Left;
}

ESlateSizeRule::Type FWidgetBlueprintGenerator::ParseSizeRule(const FString& SizeString) const
{
	if (SizeString == TEXT("Auto")) return ESlateSizeRule::Automatic;
	if (SizeString == TEXT("Fill")) return ESlateSizeRule::Fill;
	return ESlateSizeRule::Automatic;
}

FString FWidgetBlueprintGenerator::GenerateWidgetName(const FString& Prefix) const
{
	return FString::Printf(TEXT("%s_%d"), *Prefix, ++WidgetNameCounter);
}

bool FWidgetBlueprintGenerator::IsPanelWidget(UClass* WidgetClass) const
{
	return WidgetClass && WidgetClass->IsChildOf(UPanelWidget::StaticClass());
}

//~ Property Bindings

void FWidgetBlueprintGenerator::ConfigureBindings(UWidgetBlueprint* Blueprint, UWidget* Widget, const FString& WidgetName, TSharedPtr<FJsonObject> BindingsConfig)
{
	if (!Blueprint || !Widget || !BindingsConfig.IsValid())
	{
		return;
	}

	// Use the actual widget name from the object, not the passed-in name
	// This ensures the binding references the correct widget in the tree
	const FString ActualWidgetName = Widget->GetName();

	for (const auto& Pair : BindingsConfig->Values)
	{
		const FString& PropertyName = Pair.Key;
		const TSharedPtr<FJsonValue>& BindingValue = Pair.Value;

		FDelegateEditorBinding NewBinding;
		NewBinding.ObjectName = ActualWidgetName;
		NewBinding.PropertyName = *PropertyName;

		// Simple format: "PropertyName": "FunctionName"
		FString FunctionName;
		if (BindingValue->TryGetString(FunctionName))
		{
			NewBinding.FunctionName = *FunctionName;
			NewBinding.Kind = EBindingKind::Function;
		}
		// Full format: "PropertyName": { "Function": "...", "Kind": "..." }
		else if (BindingValue->Type == EJson::Object)
		{
			TSharedPtr<FJsonObject> BindingObj = BindingValue->AsObject();

			FString Function;
			if (BindingObj->TryGetStringField(TEXT("Function"), Function))
			{
				NewBinding.FunctionName = *Function;
				NewBinding.Kind = EBindingKind::Function;
			}

			FString Property;
			if (BindingObj->TryGetStringField(TEXT("Property"), Property))
			{
				NewBinding.SourceProperty = *Property;
				NewBinding.Kind = EBindingKind::Property;
			}

			// Override Kind if explicitly specified
			FString KindStr;
			if (BindingObj->TryGetStringField(TEXT("Kind"), KindStr))
			{
				if (KindStr == TEXT("Property"))
				{
					NewBinding.Kind = EBindingKind::Property;
				}
				else if (KindStr == TEXT("Function"))
				{
					NewBinding.Kind = EBindingKind::Function;
				}
			}
		}
		else
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Invalid binding format for property '%s' on widget '%s'"), *PropertyName, *ActualWidgetName);
			continue;
		}

		// Check if binding already exists and update it, otherwise add new
		bool bFound = false;
		for (FDelegateEditorBinding& ExistingBinding : Blueprint->Bindings)
		{
			if (ExistingBinding.ObjectName == NewBinding.ObjectName && ExistingBinding.PropertyName == NewBinding.PropertyName)
			{
				ExistingBinding = NewBinding;
				bFound = true;
				UE_LOG(LogAssetFactory, Log, TEXT("Updated binding: %s.%s -> %s"),
					*ActualWidgetName, *PropertyName,
					NewBinding.Kind == EBindingKind::Function ? *NewBinding.FunctionName.ToString() : *NewBinding.SourceProperty.ToString());
				break;
			}
		}

		if (!bFound)
		{
			Blueprint->Bindings.Add(NewBinding);
			UE_LOG(LogAssetFactory, Log, TEXT("Added binding: %s.%s -> %s"),
				*ActualWidgetName, *PropertyName,
				NewBinding.Kind == EBindingKind::Function ? *NewBinding.FunctionName.ToString() : *NewBinding.SourceProperty.ToString());
		}
	}
}

//~ Class Default Properties

void FWidgetBlueprintGenerator::ApplyClassDefaults(UWidgetBlueprint* Blueprint, TSharedPtr<FJsonObject> ClassDefaultsConfig)
{
	if (!Blueprint || !ClassDefaultsConfig.IsValid())
	{
		return;
	}

	// Get the CDO (Class Default Object) from the generated class
	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (!GeneratedClass)
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Blueprint has no generated class, cannot apply ClassDefaults"));
		return;
	}

	UObject* CDO = GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Failed to get CDO for class '%s'"), *GeneratedClass->GetName());
		return;
	}

	UE_LOG(LogAssetFactory, Log, TEXT("Applying ClassDefaults to '%s'"), *GeneratedClass->GetName());

	// Iterate through all properties in the JSON
	for (const auto& Pair : ClassDefaultsConfig->Values)
	{
		const FString& PropertyName = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Pair.Value;

		// Find the property on the class
		FProperty* Property = GeneratedClass->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s' not found on class '%s'"), *PropertyName, *GeneratedClass->GetName());
			continue;
		}

		// Check if property is editable
		if (!Property->HasAnyPropertyFlags(CPF_Edit))
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s' is not editable"), *PropertyName);
			continue;
		}

		if (SetCDOProperty(CDO, Property, JsonValue))
		{
			UE_LOG(LogAssetFactory, Log, TEXT("Set ClassDefault: %s"), *PropertyName);
		}
		else
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Failed to set ClassDefault: %s"), *PropertyName);
		}
	}
}

bool FWidgetBlueprintGenerator::SetCDOProperty(UObject* CDO, FProperty* Property, TSharedPtr<FJsonValue> JsonValue)
{
	if (!CDO || !Property || !JsonValue.IsValid())
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);

	// Handle TSoftObjectPtr (materials, textures)
	if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		FString AssetPath;
		if (JsonValue->TryGetString(AssetPath))
		{
			return SetSoftObjectProperty(CDO, SoftObjProp, AssetPath);
		}
		return false;
	}

	// Handle TSubclassOf
	if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		FString ClassPath;
		if (JsonValue->TryGetString(ClassPath))
		{
			return SetClassProperty(CDO, ClassProp, ClassPath);
		}
		return false;
	}

	// Handle TMap
	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		if (JsonValue->Type == EJson::Object)
		{
			return SetMapProperty(CDO, MapProp, JsonValue->AsObject());
		}
		return false;
	}

	// Handle TArray
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
		if (JsonValue->TryGetArray(ArrayValues))
		{
			return SetArrayProperty(CDO, ArrayProp, *ArrayValues);
		}
		return false;
	}

	// Handle numeric types
	if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		double Value = 0.0;
		if (JsonValue->TryGetNumber(Value))
		{
			if (NumericProp->IsFloatingPoint())
			{
				NumericProp->SetFloatingPointPropertyValue(ValuePtr, Value);
			}
			else
			{
				NumericProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value));
			}
			return true;
		}
		return false;
	}

	// Handle bool
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool Value = false;
		if (JsonValue->TryGetBool(Value))
		{
			BoolProp->SetPropertyValue(ValuePtr, Value);
			return true;
		}
		return false;
	}

	// Handle FString
	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			StrProp->SetPropertyValue(ValuePtr, Value);
			return true;
		}
		return false;
	}

	// Handle FText
	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			TextProp->SetPropertyValue(ValuePtr, FText::FromString(Value));
			return true;
		}
		return false;
	}

	// Handle FName
	if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			NameProp->SetPropertyValue(ValuePtr, FName(*Value));
			return true;
		}
		return false;
	}

	// Handle struct types
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		// FLinearColor
		if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
		{
			FLinearColor Color = ParseColor(JsonValue);
			*static_cast<FLinearColor*>(ValuePtr) = Color;
			return true;
		}

		// FColor
		if (StructProp->Struct == TBaseStructure<FColor>::Get())
		{
			FLinearColor Color = ParseColor(JsonValue);
			*static_cast<FColor*>(ValuePtr) = Color.ToFColor(true);
			return true;
		}

		// FVector2D
		if (StructProp->Struct == TBaseStructure<FVector2D>::Get())
		{
			const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
			if (JsonValue->TryGetArray(ArrayValues) && ArrayValues->Num() >= 2)
			{
				*static_cast<FVector2D*>(ValuePtr) = ParseVector2D(*ArrayValues);
				return true;
			}
			return false;
		}

		// FSoftObjectPath
		if (StructProp->Struct == TBaseStructure<FSoftObjectPath>::Get())
		{
			FString PathStr;
			if (JsonValue->TryGetString(PathStr))
			{
				*static_cast<FSoftObjectPath*>(ValuePtr) = FSoftObjectPath(PathStr);
				return true;
			}
			return false;
		}
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Unsupported property type for '%s': %s"), *Property->GetName(), *Property->GetClass()->GetName());
	return false;
}

bool FWidgetBlueprintGenerator::SetSoftObjectProperty(UObject* CDO, FSoftObjectProperty* Property, const FString& AssetPath)
{
	if (!CDO || !Property || AssetPath.IsEmpty())
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);

	// Normalize the asset path
	FString NormalizedPath = AssetPath;
	if (!NormalizedPath.Contains(TEXT(".")))
	{
		// Add asset name if not present (e.g., "/Game/UI/Mat" -> "/Game/UI/Mat.Mat")
		FString AssetName = FPaths::GetBaseFilename(NormalizedPath);
		NormalizedPath = NormalizedPath + TEXT(".") + AssetName;
	}

	// Set the soft object path via FSoftObjectPtr
	FSoftObjectPath SoftPath(NormalizedPath);
	FSoftObjectPtr SoftPtr(SoftPath);
	Property->SetPropertyValue(ValuePtr, SoftPtr);

	UE_LOG(LogAssetFactory, Log, TEXT("Set TSoftObjectPtr: %s = %s"), *Property->GetName(), *NormalizedPath);
	return true;
}

bool FWidgetBlueprintGenerator::SetClassProperty(UObject* CDO, FClassProperty* Property, const FString& ClassPath)
{
	if (!CDO || !Property || ClassPath.IsEmpty())
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);

	// Try to load the class
	FString FullClassPath = ClassPath;

	// If it's a Widget Blueprint path, try to load the generated class
	if (!FullClassPath.EndsWith(TEXT("_C")))
	{
		FullClassPath += TEXT("_C");
	}

	UClass* LoadedClass = LoadClass<UObject>(nullptr, *FullClassPath);
	if (!LoadedClass)
	{
		// Try without _C suffix (native classes)
		LoadedClass = LoadClass<UObject>(nullptr, *ClassPath);
	}

	if (LoadedClass)
	{
		// Verify the class is compatible with the property's meta class
		UClass* MetaClass = Property->MetaClass;
		if (MetaClass && !LoadedClass->IsChildOf(MetaClass))
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Class '%s' is not a subclass of '%s'"), *LoadedClass->GetName(), *MetaClass->GetName());
			return false;
		}

		Property->SetPropertyValue(ValuePtr, LoadedClass);
		UE_LOG(LogAssetFactory, Log, TEXT("Set TSubclassOf: %s = %s"), *Property->GetName(), *LoadedClass->GetName());
		return true;
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load class: %s"), *ClassPath);
	return false;
}

bool FWidgetBlueprintGenerator::SetMapProperty(UObject* CDO, FMapProperty* Property, TSharedPtr<FJsonObject> MapConfig)
{
	if (!CDO || !Property || !MapConfig.IsValid())
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);
	FScriptMapHelper MapHelper(Property, ValuePtr);

	// Get key and value property types
	FProperty* KeyProp = Property->KeyProp;
	FProperty* ValueProp = Property->ValueProp;

	// Clear existing entries
	MapHelper.EmptyValues();

	for (const auto& Pair : MapConfig->Values)
	{
		const FString& KeyStr = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Pair.Value;

		// Add a new entry
		int32 Index = MapHelper.AddDefaultValue_Invalid_NeedsRehash();

		// Set the key
		void* KeyPtr = MapHelper.GetKeyPtr(Index);

		// Handle enum keys (like ERPGElementType)
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(KeyProp))
		{
			UEnum* Enum = EnumProp->GetEnum();
			int64 EnumValue = Enum->GetValueByNameString(KeyStr);
			if (EnumValue == INDEX_NONE)
			{
				// Try with enum prefix
				FString FullName = Enum->GetName() + TEXT("::") + KeyStr;
				EnumValue = Enum->GetValueByNameString(FullName);
			}
			if (EnumValue != INDEX_NONE)
			{
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(KeyPtr, EnumValue);
			}
			else
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Unknown enum value '%s' for enum '%s'"), *KeyStr, *Enum->GetName());
				MapHelper.RemoveAt(Index);
				continue;
			}
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(KeyProp))
		{
			// Byte enum
			if (UEnum* Enum = ByteProp->Enum)
			{
				int64 EnumValue = Enum->GetValueByNameString(KeyStr);
				if (EnumValue != INDEX_NONE)
				{
					ByteProp->SetIntPropertyValue(KeyPtr, EnumValue);
				}
				else
				{
					MapHelper.RemoveAt(Index);
					continue;
				}
			}
			else
			{
				ByteProp->SetIntPropertyValue(KeyPtr, static_cast<int64>(FCString::Atoi(*KeyStr)));
			}
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(KeyProp))
		{
			StrProp->SetPropertyValue(KeyPtr, KeyStr);
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(KeyProp))
		{
			NameProp->SetPropertyValue(KeyPtr, FName(*KeyStr));
		}
		else
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Unsupported map key type: %s"), *KeyProp->GetClass()->GetName());
			MapHelper.RemoveAt(Index);
			continue;
		}

		// Set the value
		void* ValPtr = MapHelper.GetValuePtr(Index);

		// Handle TSoftObjectPtr values
		if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(ValueProp))
		{
			FString AssetPath;
			if (JsonValue->TryGetString(AssetPath))
			{
				// Normalize path
				if (!AssetPath.Contains(TEXT(".")))
				{
					FString AssetName = FPaths::GetBaseFilename(AssetPath);
					AssetPath = AssetPath + TEXT(".") + AssetName;
				}
				FSoftObjectPath SoftPath(AssetPath);
				FSoftObjectPtr SoftPtr(SoftPath);
				SoftObjProp->SetPropertyValue(ValPtr, SoftPtr);
			}
		}
		// Handle FLinearColor values
		else if (FStructProperty* StructProp = CastField<FStructProperty>(ValueProp))
		{
			if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
			{
				FLinearColor Color = ParseColor(JsonValue);
				*static_cast<FLinearColor*>(ValPtr) = Color;
			}
		}
		// Handle other types
		else
		{
			// Try to use generic property setting
			FString ValueStr;
			double ValueNum = 0;
			if (JsonValue->TryGetString(ValueStr))
			{
				if (FStrProperty* StrValProp = CastField<FStrProperty>(ValueProp))
				{
					StrValProp->SetPropertyValue(ValPtr, ValueStr);
				}
			}
			else if (JsonValue->TryGetNumber(ValueNum))
			{
				if (FNumericProperty* NumProp = CastField<FNumericProperty>(ValueProp))
				{
					if (NumProp->IsFloatingPoint())
					{
						NumProp->SetFloatingPointPropertyValue(ValPtr, ValueNum);
					}
					else
					{
						NumProp->SetIntPropertyValue(ValPtr, static_cast<int64>(ValueNum));
					}
				}
			}
		}
	}

	MapHelper.Rehash();

	UE_LOG(LogAssetFactory, Log, TEXT("Set TMap: %s with %d entries"), *Property->GetName(), MapHelper.Num());
	return true;
}

bool FWidgetBlueprintGenerator::SetArrayProperty(UObject* CDO, FArrayProperty* Property, const TArray<TSharedPtr<FJsonValue>>& ArrayValues)
{
	if (!CDO || !Property)
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);
	FScriptArrayHelper ArrayHelper(Property, ValuePtr);

	FProperty* InnerProp = Property->Inner;

	// Clear existing entries
	ArrayHelper.EmptyValues();

	for (int32 i = 0; i < ArrayValues.Num(); ++i)
	{
		const TSharedPtr<FJsonValue>& JsonValue = ArrayValues[i];

		int32 Index = ArrayHelper.AddValue();
		void* ElementPtr = ArrayHelper.GetRawPtr(Index);

		// Handle different inner property types
		if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(InnerProp))
		{
			FString AssetPath;
			if (JsonValue->TryGetString(AssetPath))
			{
				if (!AssetPath.Contains(TEXT(".")))
				{
					FString AssetName = FPaths::GetBaseFilename(AssetPath);
					AssetPath = AssetPath + TEXT(".") + AssetName;
				}
				FSoftObjectPath SoftPath(AssetPath);
				FSoftObjectPtr SoftPtr(SoftPath);
				SoftObjProp->SetPropertyValue(ElementPtr, SoftPtr);
			}
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(InnerProp))
		{
			FString Value;
			if (JsonValue->TryGetString(Value))
			{
				StrProp->SetPropertyValue(ElementPtr, Value);
			}
		}
		else if (FNumericProperty* NumProp = CastField<FNumericProperty>(InnerProp))
		{
			double Value = 0;
			if (JsonValue->TryGetNumber(Value))
			{
				if (NumProp->IsFloatingPoint())
				{
					NumProp->SetFloatingPointPropertyValue(ElementPtr, Value);
				}
				else
				{
					NumProp->SetIntPropertyValue(ElementPtr, static_cast<int64>(Value));
				}
			}
		}
		else if (FStructProperty* StructProp = CastField<FStructProperty>(InnerProp))
		{
			if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
			{
				*static_cast<FLinearColor*>(ElementPtr) = ParseColor(JsonValue);
			}
		}
	}

	UE_LOG(LogAssetFactory, Log, TEXT("Set TArray: %s with %d elements"), *Property->GetName(), ArrayHelper.Num());
	return true;
}
