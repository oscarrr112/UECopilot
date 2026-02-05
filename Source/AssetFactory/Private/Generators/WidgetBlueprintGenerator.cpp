// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/WidgetBlueprintGenerator.h"
#include "AssetFactoryModule.h"
#include "Utils/ClassFinderUtils.h"
#include "Utils/PropertySetterUtils.h"

// Blueprint creation
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Widget Blueprint - only base classes needed
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"

// Styling (needed for parse helpers)
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Engine/Texture2D.h"

// Asset handling
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"

// Reflection and property helpers
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"

// EdGraph for function validation
#include "EdGraph/EdGraph.h"

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
		// Try to find custom parent class using ClassFinderUtils (searches multiple modules)
		ParentClass = FClassFinderUtils::FindClassByName(ParentClassName, UUserWidget::StaticClass(), true);
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

		// Clear existing widget tree for rebuild (following UE's DeleteWidgets pattern)
		if (Blueprint->WidgetTree)
		{
			Blueprint->WidgetTree->Modify();
			Blueprint->Modify();

			TArray<UWidget*> AllWidgets;
			Blueprint->WidgetTree->GetAllWidgets(AllWidgets);
			Blueprint->WidgetTree->RootWidget = nullptr;

			for (UWidget* Widget : AllWidgets)
			{
				if (Widget)
				{
					const FName WidgetName = Widget->GetFName();

					// Remove associated bindings
					for (int32 i = Blueprint->Bindings.Num() - 1; i >= 0; --i)
					{
						if (Blueprint->Bindings[i].ObjectName == Widget->GetName())
						{
							Blueprint->Bindings.RemoveAt(i);
						}
					}

					// Remove from parent
					if (UPanelWidget* Parent = Widget->GetParent())
					{
						Parent->Modify();
					}
					Widget->Modify();

					// Remove from WidgetTree
					Blueprint->WidgetTree->RemoveWidget(Widget);

					// Remove variable nodes if it was a variable
					if (Widget->bIsVariable)
					{
						FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, WidgetName);
					}

					// Rename to transient package
					Widget->Rename(nullptr, GetTransientPackage());

					// Notify Blueprint that variable was removed
					Blueprint->OnVariableRemoved(WidgetName);
				}
			}
		}

		// Clear bindings as they reference old widgets
		Blueprint->Bindings.Empty();
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

	// Check Action field (default: CreateOrUpdate)
	FString ActionStr = GetStringField(WidgetNode, TEXT("Action"));

	// Get widget name
	FString WidgetName = GetStringField(WidgetNode, TEXT("Name"));

	// Handle Remove action
	if (ActionStr.Equals(TEXT("Remove"), ESearchCase::IgnoreCase))
	{
		if (WidgetName.IsEmpty())
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Widget missing 'Name' field for removal"), *JsonPath);
			return nullptr;
		}

		// Find and remove the widget by name (following UE's DeleteWidgets pattern)
		UWidget* WidgetToRemove = Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
		if (WidgetToRemove)
		{
			Blueprint->WidgetTree->Modify();
			Blueprint->Modify();

			// Helper lambda to properly delete a widget and its children
			TFunction<void(UWidget*)> DeleteWidgetRecursive = [&DeleteWidgetRecursive, &Blueprint](UWidget* Widget)
			{
				if (!Widget) return;

				const FName WidgetFName = Widget->GetFName();

				// Remove associated bindings
				for (int32 i = Blueprint->Bindings.Num() - 1; i >= 0; --i)
				{
					if (Blueprint->Bindings[i].ObjectName == Widget->GetName())
					{
						Blueprint->Bindings.RemoveAt(i);
					}
				}

				// Process children first if it's a panel
				if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
				{
					TArray<UWidget*> Children;
					for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
					{
						Children.Add(Panel->GetChildAt(i));
					}
					for (UWidget* Child : Children)
					{
						DeleteWidgetRecursive(Child);
					}
				}

				// Modify parent
				if (UPanelWidget* Parent = Widget->GetParent())
				{
					Parent->Modify();
				}
				Widget->Modify();

				// Remove from WidgetTree
				Blueprint->WidgetTree->RemoveWidget(Widget);

				// Remove variable nodes if it was a variable
				if (Widget->bIsVariable)
				{
					FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, WidgetFName);
				}

				// Rename to transient package
				Widget->Rename(nullptr, GetTransientPackage());

				// Notify Blueprint that variable was removed
				Blueprint->OnVariableRemoved(WidgetFName);
			};

			// Recursively delete
			DeleteWidgetRecursive(WidgetToRemove);

			UE_LOG(LogAssetFactory, Log, TEXT("[%s] Removed widget '%s'"), *JsonPath, *WidgetName);
		}
		else
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Widget '%s' not found for removal"), *JsonPath, *WidgetName);
		}
		return nullptr;
	}

	// Get widget type
	FString WidgetType = GetStringField(WidgetNode, TEXT("Type"));
	if (WidgetType.IsEmpty())
	{
		UE_LOG(LogAssetFactory, Error, TEXT("[%s] Missing Type field"), *JsonPath);
		return nullptr;
	}

	// Generate name if not provided
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
			UE_LOG(LogAssetFactory, Log, TEXT("[%s] Widget '%s' has Bindings, auto-setting IsVariable to true."), *JsonPath, *WidgetName);
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
			// Check if widget is a ContentWidget (can have a single child) via reflection
			// ContentWidget has a SetContent method - check if GetContentSlot exists (returns UPanelSlot*)
			UClass* WidgetClass = Widget->GetClass();
			UFunction* SetContentFunc = WidgetClass->FindFunctionByName(TEXT("SetContent"));
			UFunction* GetContentSlotFunc = WidgetClass->FindFunctionByName(TEXT("GetContentSlot"));

			// If widget has SetContent or is a PanelWidget (even if Cast failed), handle as single-child container
			if (SetContentFunc || GetContentSlotFunc || WidgetClass->IsChildOf(UPanelWidget::StaticClass()))
			{
				// Only process first child for single-child containers
				if ((*ChildrenArray)[0]->Type == EJson::Object)
				{
					TSharedPtr<FJsonObject> ChildNode = (*ChildrenArray)[0]->AsObject();
					FString ChildPath = FString::Printf(TEXT("%s.Children[0]"), *JsonPath);

					UWidget* ChildWidget = BuildWidgetTree(Blueprint, ChildNode, nullptr, ChildPath);
					if (ChildWidget)
					{
						// Try to add child - first check if it's a PanelWidget
						UPanelWidget* WidgetAsPanel = Cast<UPanelWidget>(Widget);
						if (WidgetAsPanel)
						{
							WidgetAsPanel->AddChild(ChildWidget);
						}
						else if (SetContentFunc)
						{
							// Call SetContent via reflection
							struct { UWidget* Content; } Params;
							Params.Content = ChildWidget;
							Widget->ProcessEvent(SetContentFunc, &Params);
						}
					}
				}

				if (ChildrenArray->Num() > 1)
				{
					UE_LOG(LogAssetFactory, Warning, TEXT("[%s] ContentWidget can only have one child, ignoring extra children"), *JsonPath);
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
	// Use dynamic class lookup - supports "TextBlock", "UTextBlock", full paths, etc.
	return FClassFinderUtils::FindWidgetClass(TypeString);
}

UWidget* FWidgetBlueprintGenerator::CreateWidget(
	UWidgetBlueprint* Blueprint,
	const FString& WidgetType,
	const FString& WidgetName,
	TSharedPtr<FJsonObject> Properties)
{
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

	// Dynamic class lookup for all widget types
	UClass* WidgetClass = FindWidgetClass(WidgetType);
	if (!WidgetClass)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Widget class not found: %s"), *WidgetType);
		return nullptr;
	}

	if (!WidgetClass->IsChildOf(UWidget::StaticClass()))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Class '%s' is not a UWidget subclass"), *WidgetType);
		return nullptr;
	}

	// Create widget using WidgetTree's ConstructWidget (dynamic, no hardcoded types)
	UWidget* Widget = Blueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, *WidgetName);
	if (!Widget)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Failed to construct widget '%s' of type '%s'"), *WidgetName, *WidgetType);
		return nullptr;
	}

	// Set properties via reflection if provided
	if (Properties.IsValid())
	{
		SetPropertiesViaReflection(Widget, Properties);
	}

	return Widget;
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

//~ Slot Configuration (Dynamic via reflection)

void FWidgetBlueprintGenerator::ConfigureSlot(UWidget* Widget, UPanelWidget* Parent, TSharedPtr<FJsonObject> SlotConfig)
{
	if (!Widget || !Parent || !Widget->Slot)
	{
		return;
	}

	UPanelSlot* Slot = Widget->Slot;
	UClass* SlotClass = Slot->GetClass();

	// Apply common slot properties first
	ConfigureCommonSlotProperties(Slot, SlotConfig);

	// Additional slot-specific properties via reflection
	if (SlotConfig.IsValid())
	{
		// Handle Anchors (FAnchors struct) - common on canvas slots
		if (SlotConfig->HasTypedField<EJson::Object>(TEXT("Anchors")))
		{
			FProperty* AnchorsProp = SlotClass->FindPropertyByName(TEXT("Anchors"));
			if (!AnchorsProp)
			{
				// Also try LayoutData.Anchors for some slot types
				AnchorsProp = SlotClass->FindPropertyByName(TEXT("LayoutData"));
			}

			if (AnchorsProp)
			{
				TSharedPtr<FJsonObject> AnchorsConfig = SlotConfig->GetObjectField(TEXT("Anchors"));
				FAnchors Anchors = ParseAnchors(AnchorsConfig);

				// Use reflection to set the anchors
				if (FStructProperty* StructProp = CastField<FStructProperty>(AnchorsProp))
				{
					if (StructProp->Struct->GetFName() == TEXT("Anchors"))
					{
						void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(Slot);
						*static_cast<FAnchors*>(ValuePtr) = Anchors;
					}
				}
			}
		}

		// Handle Offsets (FMargin struct) - common on canvas slots
		if (SlotConfig->HasField(TEXT("Offsets")))
		{
			FProperty* OffsetsProp = SlotClass->FindPropertyByName(TEXT("Offsets"));
			if (OffsetsProp)
			{
				FMargin Offsets = ParseMargins(SlotConfig->TryGetField(TEXT("Offsets")));
				if (FStructProperty* StructProp = CastField<FStructProperty>(OffsetsProp))
				{
					void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(Slot);
					*static_cast<FMargin*>(ValuePtr) = Offsets;
				}
			}
		}

		// Handle Alignment (FVector2D) - common on canvas slots
		const TArray<TSharedPtr<FJsonValue>>* AlignmentArray = nullptr;
		if (SlotConfig->TryGetArrayField(TEXT("Alignment"), AlignmentArray) && AlignmentArray->Num() >= 2)
		{
			FProperty* AlignmentProp = SlotClass->FindPropertyByName(TEXT("Alignment"));
			if (AlignmentProp)
			{
				FVector2D Alignment = ParseVector2D(*AlignmentArray);
				if (FStructProperty* StructProp = CastField<FStructProperty>(AlignmentProp))
				{
					void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(Slot);
					*static_cast<FVector2D*>(ValuePtr) = Alignment;
				}
			}
		}

		// Handle SizeToContent/AutoSize (bool) - common on canvas slots
		bool bAutoSize = false;
		if (SlotConfig->TryGetBoolField(TEXT("SizeToContent"), bAutoSize) ||
			SlotConfig->TryGetBoolField(TEXT("AutoSize"), bAutoSize))
		{
			FProperty* AutoSizeProp = SlotClass->FindPropertyByName(TEXT("bAutoSize"));
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(AutoSizeProp))
			{
				void* ValuePtr = BoolProp->ContainerPtrToValuePtr<void>(Slot);
				BoolProp->SetPropertyValue(ValuePtr, bAutoSize);
			}
		}

		// Handle Size/SlotSize (FSlateChildSize) - for box slots
		if (SlotConfig->HasField(TEXT("Size")))
		{
			TSharedPtr<FJsonValue> SizeValue = SlotConfig->TryGetField(TEXT("Size"));
			FString SizeStr;

			// Parse size rule
			if (SizeValue->TryGetString(SizeStr))
			{
				// String format: "Fill" or "Auto"
			}
			else if (SizeValue->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> SizeObj = SizeValue->AsObject();
				SizeObj->TryGetStringField(TEXT("SizeRule"), SizeStr);
			}

			if (!SizeStr.IsEmpty())
			{
				// Find Size or SlotSize property
				FProperty* SizeProp = SlotClass->FindPropertyByName(TEXT("Size"));
				if (!SizeProp)
				{
					SizeProp = SlotClass->FindPropertyByName(TEXT("SlotSize"));
				}

				if (FStructProperty* StructProp = CastField<FStructProperty>(SizeProp))
				{
					// FSlateChildSize has SizeRule (enum) and Value (float)
					void* SizePtr = StructProp->ContainerPtrToValuePtr<void>(Slot);
					UScriptStruct* SizeStruct = StructProp->Struct;

					// Find SizeRule property within the struct
					FProperty* SizeRuleProp = SizeStruct->FindPropertyByName(TEXT("SizeRule"));
					if (FByteProperty* EnumProp = CastField<FByteProperty>(SizeRuleProp))
					{
						void* RulePtr = EnumProp->ContainerPtrToValuePtr<void>(SizePtr);
						ESlateSizeRule::Type Rule = ParseSizeRule(SizeStr);
						EnumProp->SetIntPropertyValue(RulePtr, static_cast<int64>(Rule));
					}
					else if (FEnumProperty* EnumProp2 = CastField<FEnumProperty>(SizeRuleProp))
					{
						void* RulePtr = EnumProp2->ContainerPtrToValuePtr<void>(SizePtr);
						ESlateSizeRule::Type Rule = ParseSizeRule(SizeStr);
						EnumProp2->GetUnderlyingProperty()->SetIntPropertyValue(RulePtr, static_cast<int64>(Rule));
					}
				}
			}
		}

		// Handle Row/Column for grid slots (via reflection)
		double Row = 0;
		if (SlotConfig->TryGetNumberField(TEXT("Row"), Row))
		{
			FProperty* RowProp = SlotClass->FindPropertyByName(TEXT("Row"));
			if (FIntProperty* IntProp = CastField<FIntProperty>(RowProp))
			{
				void* ValuePtr = IntProp->ContainerPtrToValuePtr<void>(Slot);
				IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(Row));
			}
		}

		double Column = 0;
		if (SlotConfig->TryGetNumberField(TEXT("Column"), Column))
		{
			FProperty* ColProp = SlotClass->FindPropertyByName(TEXT("Column"));
			if (FIntProperty* IntProp = CastField<FIntProperty>(ColProp))
			{
				void* ValuePtr = IntProp->ContainerPtrToValuePtr<void>(Slot);
				IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(Column));
			}
		}

		// Handle RowSpan/ColumnSpan for grid slots
		double RowSpan = 0;
		if (SlotConfig->TryGetNumberField(TEXT("RowSpan"), RowSpan))
		{
			FProperty* RowSpanProp = SlotClass->FindPropertyByName(TEXT("RowSpan"));
			if (FIntProperty* IntProp = CastField<FIntProperty>(RowSpanProp))
			{
				void* ValuePtr = IntProp->ContainerPtrToValuePtr<void>(Slot);
				IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(RowSpan));
			}
		}

		double ColumnSpan = 0;
		if (SlotConfig->TryGetNumberField(TEXT("ColumnSpan"), ColumnSpan))
		{
			FProperty* ColSpanProp = SlotClass->FindPropertyByName(TEXT("ColumnSpan"));
			if (FIntProperty* IntProp = CastField<FIntProperty>(ColSpanProp))
			{
				void* ValuePtr = IntProp->ContainerPtrToValuePtr<void>(Slot);
				IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(ColumnSpan));
			}
		}
	}
}

void FWidgetBlueprintGenerator::ConfigureCommonSlotProperties(UPanelSlot* Slot, TSharedPtr<FJsonObject> SlotConfig)
{
	if (!Slot || !SlotConfig.IsValid())
	{
		return;
	}

	UClass* SlotClass = Slot->GetClass();

	// Handle Padding (FMargin) - exists on most slot types
	if (SlotConfig->HasField(TEXT("Padding")))
	{
		FProperty* PaddingProp = SlotClass->FindPropertyByName(TEXT("Padding"));
		if (FStructProperty* StructProp = CastField<FStructProperty>(PaddingProp))
		{
			FMargin Padding = ParseMargins(SlotConfig->TryGetField(TEXT("Padding")));
			void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(Slot);
			*static_cast<FMargin*>(ValuePtr) = Padding;
		}
	}

	// Handle HorizontalAlignment (supports both "HAlign" and "HorizontalAlignment")
	FString HAlign;
	if (SlotConfig->TryGetStringField(TEXT("HAlign"), HAlign) ||
		SlotConfig->TryGetStringField(TEXT("HorizontalAlignment"), HAlign))
	{
		FProperty* HAlignProp = SlotClass->FindPropertyByName(TEXT("HorizontalAlignment"));
		if (FByteProperty* EnumProp = CastField<FByteProperty>(HAlignProp))
		{
			void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Slot);
			EHorizontalAlignment Alignment = ParseHorizontalAlignment(HAlign);
			EnumProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(Alignment));
		}
		else if (FEnumProperty* EnumProp2 = CastField<FEnumProperty>(HAlignProp))
		{
			void* ValuePtr = EnumProp2->ContainerPtrToValuePtr<void>(Slot);
			EHorizontalAlignment Alignment = ParseHorizontalAlignment(HAlign);
			EnumProp2->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, static_cast<int64>(Alignment));
		}
	}

	// Handle VerticalAlignment (supports both "VAlign" and "VerticalAlignment")
	FString VAlign;
	if (SlotConfig->TryGetStringField(TEXT("VAlign"), VAlign) ||
		SlotConfig->TryGetStringField(TEXT("VerticalAlignment"), VAlign))
	{
		FProperty* VAlignProp = SlotClass->FindPropertyByName(TEXT("VerticalAlignment"));
		if (FByteProperty* EnumProp = CastField<FByteProperty>(VAlignProp))
		{
			void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Slot);
			EVerticalAlignment Alignment = ParseVerticalAlignment(VAlign);
			EnumProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(Alignment));
		}
		else if (FEnumProperty* EnumProp2 = CastField<FEnumProperty>(VAlignProp))
		{
			void* ValuePtr = EnumProp2->ContainerPtrToValuePtr<void>(Slot);
			EVerticalAlignment Alignment = ParseVerticalAlignment(VAlign);
			EnumProp2->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, static_cast<int64>(Alignment));
		}
	}
}

//~ Style Application (Dynamic via reflection)

void FWidgetBlueprintGenerator::ApplyStyle(UWidget* Widget, TSharedPtr<FJsonObject> StyleConfig)
{
	if (!Widget || !StyleConfig.IsValid())
	{
		return;
	}

	UClass* WidgetClass = Widget->GetClass();

	// Handle Color/ColorAndOpacity - common style property
	if (StyleConfig->HasField(TEXT("Color")))
	{
		FLinearColor Color = ParseColor(StyleConfig->TryGetField(TEXT("Color")));

		// Try ColorAndOpacity (FSlateColor) first - used by TextBlock
		FProperty* ColorProp = WidgetClass->FindPropertyByName(TEXT("ColorAndOpacity"));
		if (FStructProperty* StructProp = CastField<FStructProperty>(ColorProp))
		{
			void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(Widget);
			if (StructProp->Struct->GetFName() == TEXT("SlateColor"))
			{
				*static_cast<FSlateColor*>(ValuePtr) = FSlateColor(Color);
			}
			else if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
			{
				*static_cast<FLinearColor*>(ValuePtr) = Color;
			}
		}
		// Try BrushColor for Border widgets
		else
		{
			FProperty* BrushColorProp = WidgetClass->FindPropertyByName(TEXT("BrushColor"));
			if (FStructProperty* BrushColorStruct = CastField<FStructProperty>(BrushColorProp))
			{
				void* ValuePtr = BrushColorStruct->ContainerPtrToValuePtr<void>(Widget);
				*static_cast<FLinearColor*>(ValuePtr) = Color;
			}
		}
	}

	// Handle Brush - for Image, Border, and similar widgets
	if (StyleConfig->HasTypedField<EJson::Object>(TEXT("Brush")))
	{
		TSharedPtr<FJsonObject> BrushConfig = StyleConfig->GetObjectField(TEXT("Brush"));
		if (BrushConfig.IsValid())
		{
			FSlateBrush Brush = ParseBrush(BrushConfig);

			// Find Brush property
			FProperty* BrushProp = WidgetClass->FindPropertyByName(TEXT("Brush"));
			// Also try Background for Border widgets
			if (!BrushProp)
			{
				BrushProp = WidgetClass->FindPropertyByName(TEXT("Background"));
			}

			if (FStructProperty* StructProp = CastField<FStructProperty>(BrushProp))
			{
				void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(Widget);
				*static_cast<FSlateBrush*>(ValuePtr) = Brush;
			}
		}
	}

	// Handle Font - for TextBlock and similar widgets
	if (StyleConfig->HasTypedField<EJson::Object>(TEXT("Font")))
	{
		TSharedPtr<FJsonObject> FontConfig = StyleConfig->GetObjectField(TEXT("Font"));
		if (FontConfig.IsValid())
		{
			FProperty* FontProp = WidgetClass->FindPropertyByName(TEXT("Font"));
			if (FStructProperty* StructProp = CastField<FStructProperty>(FontProp))
			{
				void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(Widget);
				FSlateFontInfo* FontPtr = static_cast<FSlateFontInfo*>(ValuePtr);

				double Size = 0;
				if (FontConfig->TryGetNumberField(TEXT("Size"), Size))
				{
					FontPtr->Size = static_cast<int32>(Size);
				}

				// Font family would need asset loading - handled separately if needed
			}
		}
	}

	// Handle any additional style properties via generic reflection
	// This allows styles to set arbitrary widget properties
	for (const auto& Pair : StyleConfig->Values)
	{
		// Skip already-handled properties
		if (Pair.Key == TEXT("Color") || Pair.Key == TEXT("Brush") || Pair.Key == TEXT("Font"))
		{
			continue;
		}

		// Try to set as a widget property via reflection
		FProperty* Property = WidgetClass->FindPropertyByName(*Pair.Key);
		if (Property)
		{
			FPropertySetterUtils::SetPropertyFromJson(Widget, Property, Pair.Value);
		}
	}
}

//~ Reflection-based Property Setting

// Helper to detect if properties use the new typed format
static bool IsTypedPropertiesFormat(TSharedPtr<FJsonObject> Properties)
{
	if (!Properties.IsValid())
	{
		return false;
	}
	for (const auto& Pair : Properties->Values)
	{
		const TSharedPtr<FJsonObject>* PropObj;
		if (Pair.Value->TryGetObject(PropObj) && (*PropObj)->HasField(TEXT("type")))
		{
			return true;
		}
		break; // Only check first property
	}
	return false;
}

void FWidgetBlueprintGenerator::SetPropertiesViaReflection(UWidget* Widget, TSharedPtr<FJsonObject> Properties)
{
	if (!Widget || !Properties.IsValid())
	{
		return;
	}

	// Use the shared utility class for property setting (supports both formats)
	if (IsTypedPropertiesFormat(Properties))
	{
		FPropertySetterUtils::SetTypedPropertiesFromJson(Widget, Properties);
	}
	else
	{
		FPropertySetterUtils::SetPropertiesFromJson(Widget, Properties);
	}
}

void FWidgetBlueprintGenerator::SetObjectPropertiesViaReflection(UObject* Object, TSharedPtr<FJsonObject> Properties)
{
	if (!Object || !Properties.IsValid())
	{
		return;
	}

	// Use the shared utility class for property setting (supports both formats)
	if (IsTypedPropertiesFormat(Properties))
	{
		FPropertySetterUtils::SetTypedPropertiesFromJson(Object, Properties);
	}
	else
	{
		FPropertySetterUtils::SetPropertiesFromJson(Object, Properties);
	}
}

bool FWidgetBlueprintGenerator::SetPropertyValueFromJson(UObject* Object, FProperty* Property, void* ValuePtr, TSharedPtr<FJsonValue> JsonValue)
{
	// Delegate to the shared utility class
	if (Object)
	{
		return FPropertySetterUtils::SetPropertyFromJson(Object, Property, JsonValue);
	}

	// For struct fields without a containing object, handle struct property directly
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return FPropertySetterUtils::SetStructPropertyFromJson(StructProp, ValuePtr, JsonValue);
	}

	return false;
}

bool FWidgetBlueprintGenerator::SetStructPropertyFromJson(FStructProperty* StructProp, void* ValuePtr, TSharedPtr<FJsonValue> JsonValue)
{
	// Delegate to the shared utility class
	return FPropertySetterUtils::SetStructPropertyFromJson(StructProp, ValuePtr, JsonValue);
}

bool FWidgetBlueprintGenerator::SetObjectReferenceFromPath(FObjectPropertyBase* ObjProp, void* ValuePtr, const FString& ObjectPath)
{
	// Delegate to the shared utility class
	return FPropertySetterUtils::SetObjectReferenceFromPath(ObjProp, ValuePtr, ObjectPath);
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

	// Array format [R, G, B, A]
	const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
	if (ColorValue->TryGetArray(ColorArray))
	{
		if (ColorArray->Num() >= 3)
		{
			double R = 1, G = 1, B = 1, A = 1;
			(*ColorArray)[0]->TryGetNumber(R);
			(*ColorArray)[1]->TryGetNumber(G);
			(*ColorArray)[2]->TryGetNumber(B);
			if (ColorArray->Num() >= 4)
			{
				(*ColorArray)[3]->TryGetNumber(A);
			}
			return FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
		}
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

		// Validate that the bound function exists in the class hierarchy (including C++ base classes)
		if (NewBinding.Kind == EBindingKind::Function && !NewBinding.FunctionName.IsNone())
		{
			bool bFunctionFound = false;

			// Try to find the function in the generated class hierarchy
			UClass* ClassToCheck = Blueprint->GeneratedClass;
			if (!ClassToCheck)
			{
				// If not compiled yet, use the parent class
				ClassToCheck = Blueprint->ParentClass;
			}

			if (ClassToCheck)
			{
				// FindFunctionByName searches the entire class hierarchy including C++ base classes
				UFunction* FoundFunction = ClassToCheck->FindFunctionByName(NewBinding.FunctionName);
				if (FoundFunction)
				{
					bFunctionFound = true;
				}
			}

			// Also check Blueprint function graphs (functions defined in this Blueprint but not yet compiled)
			if (!bFunctionFound)
			{
				for (UEdGraph* Graph : Blueprint->FunctionGraphs)
				{
					if (Graph && Graph->GetFName() == NewBinding.FunctionName)
					{
						bFunctionFound = true;
						break;
					}
				}
			}

			if (!bFunctionFound)
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Binding function '%s' not found in class hierarchy for widget '%s.%s'. Binding will be created but may not work at runtime."),
					*NewBinding.FunctionName.ToString(), *ActualWidgetName, *PropertyName);
				// Continue to create the binding anyway - it might be added later or the user knows what they're doing
			}
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
	// Delegate to the shared utility class
	return FPropertySetterUtils::SetSoftObjectProperty(CDO, Property, AssetPath);
}

bool FWidgetBlueprintGenerator::SetClassProperty(UObject* CDO, FClassProperty* Property, const FString& ClassPath)
{
	// Delegate to the shared utility class
	return FPropertySetterUtils::SetClassProperty(CDO, Property, ClassPath);
}

bool FWidgetBlueprintGenerator::SetMapProperty(UObject* CDO, FMapProperty* Property, TSharedPtr<FJsonObject> MapConfig)
{
	// Delegate to the shared utility class
	return FPropertySetterUtils::SetMapProperty(CDO, Property, MapConfig);
}

bool FWidgetBlueprintGenerator::SetArrayProperty(UObject* CDO, FArrayProperty* Property, const TArray<TSharedPtr<FJsonValue>>& ArrayValues)
{
	// Delegate to the shared utility class
	return FPropertySetterUtils::SetArrayProperty(CDO, Property, ArrayValues);
}

TOptional<FString> FWidgetBlueprintGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	if (!Config->HasField(TEXT("RootWidget")))
	{
		return FString(TEXT("Missing required field 'RootWidget'"));
	}

	TSharedPtr<FJsonObject> RootWidget = Config->GetObjectField(TEXT("RootWidget"));
	if (!RootWidget.IsValid())
	{
		return FString(TEXT("'RootWidget' must be a valid object"));
	}

	if (!RootWidget->HasField(TEXT("Type")))
	{
		return FString(TEXT("RootWidget must have a 'Type' field"));
	}

	return TOptional<FString>();
}

TArray<FString> FWidgetBlueprintGenerator::GetRequiredFields() const
{
	return { TEXT("RootWidget") };
}

//~ Extract Implementation

bool FWidgetBlueprintGenerator::CanExtract(UObject* Asset) const
{
	return Asset && Asset->IsA<UWidgetBlueprint>();
}

TSharedPtr<FJsonObject> FWidgetBlueprintGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Asset);
	if (!Blueprint)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	// ParentClass
	if (Blueprint->ParentClass && Blueprint->ParentClass != UUserWidget::StaticClass())
	{
		Config->SetStringField(TEXT("ParentClass"), Blueprint->ParentClass->GetPathName());
	}

	// RootWidget
	if (Blueprint->WidgetTree && Blueprint->WidgetTree->RootWidget)
	{
		TSharedPtr<FJsonObject> RootWidgetJson = ExtractWidgetTree(Blueprint->WidgetTree->RootWidget);
		if (RootWidgetJson.IsValid())
		{
			Config->SetObjectField(TEXT("RootWidget"), RootWidgetJson);
		}
	}

	// Bindings
	if (Blueprint->Bindings.Num() > 0)
	{
		TSharedPtr<FJsonObject> BindingsJson = MakeShared<FJsonObject>();
		for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
		{
			TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
			if (Binding.Kind == EBindingKind::Function)
			{
				BindingObj->SetStringField(TEXT("Function"), Binding.FunctionName.ToString());
				BindingObj->SetStringField(TEXT("Kind"), TEXT("Function"));
			}
			else
			{
				BindingObj->SetStringField(TEXT("Property"), Binding.SourceProperty.ToString());
				BindingObj->SetStringField(TEXT("Kind"), TEXT("Property"));
			}

			// Key format: "WidgetName.PropertyName"
			FString BindingKey = FString::Printf(TEXT("%s.%s"), *Binding.ObjectName, *Binding.PropertyName.ToString());
			BindingsJson->SetObjectField(BindingKey, BindingObj);
		}
		if (BindingsJson->Values.Num() > 0)
		{
			Config->SetObjectField(TEXT("Bindings"), BindingsJson);
		}
	}

	return Config;
}

TSharedPtr<FJsonObject> FWidgetBlueprintGenerator::ExtractWidgetTree(UWidget* Widget) const
{
	if (!Widget)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> WidgetJson = MakeShared<FJsonObject>();

	// Type - get the class name without U prefix
	FString ClassName = Widget->GetClass()->GetName();
	if (ClassName.StartsWith(TEXT("U")))
	{
		ClassName = ClassName.Mid(1);
	}
	WidgetJson->SetStringField(TEXT("Type"), ClassName);

	// Name
	WidgetJson->SetStringField(TEXT("Name"), Widget->GetName());

	// IsVariable
	if (Widget->bIsVariable)
	{
		WidgetJson->SetBoolField(TEXT("IsVariable"), true);
	}

	// Properties - extract common widget properties
	TSharedPtr<FJsonObject> PropertiesJson = ExtractWidgetProperties(Widget, true);
	if (PropertiesJson.IsValid() && PropertiesJson->Values.Num() > 0)
	{
		WidgetJson->SetObjectField(TEXT("Properties"), PropertiesJson);
	}

	// Slot - if widget has a slot (is child of a panel)
	if (Widget->Slot)
	{
		TSharedPtr<FJsonObject> SlotJson = ExtractSlotConfig(Widget->Slot);
		if (SlotJson.IsValid() && SlotJson->Values.Num() > 0)
		{
			WidgetJson->SetObjectField(TEXT("Slot"), SlotJson);
		}
	}

	// Children - if this is a panel widget
	UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
	if (PanelWidget && PanelWidget->GetChildrenCount() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ChildrenArray;
		for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
		{
			UWidget* ChildWidget = PanelWidget->GetChildAt(i);
			if (ChildWidget)
			{
				TSharedPtr<FJsonObject> ChildJson = ExtractWidgetTree(ChildWidget);
				if (ChildJson.IsValid())
				{
					ChildrenArray.Add(MakeShared<FJsonValueObject>(ChildJson));
				}
			}
		}
		if (ChildrenArray.Num() > 0)
		{
			WidgetJson->SetArrayField(TEXT("Children"), ChildrenArray);
		}
	}

	return WidgetJson;
}

TSharedPtr<FJsonObject> FWidgetBlueprintGenerator::ExtractSlotConfig(UPanelSlot* Slot) const
{
	if (!Slot)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> SlotJson = MakeShared<FJsonObject>();
	UClass* SlotClass = Slot->GetClass();

	// Anchors (for CanvasPanelSlot)
	if (FStructProperty* AnchorsProp = CastField<FStructProperty>(SlotClass->FindPropertyByName(TEXT("Anchors"))))
	{
		void* ValuePtr = AnchorsProp->ContainerPtrToValuePtr<void>(Slot);
		FAnchors* Anchors = static_cast<FAnchors*>(ValuePtr);

		TSharedPtr<FJsonObject> AnchorsJson = MakeShared<FJsonObject>();
		AnchorsJson->SetArrayField(TEXT("Min"), {
			MakeShared<FJsonValueNumber>(Anchors->Minimum.X),
			MakeShared<FJsonValueNumber>(Anchors->Minimum.Y)
		});
		AnchorsJson->SetArrayField(TEXT("Max"), {
			MakeShared<FJsonValueNumber>(Anchors->Maximum.X),
			MakeShared<FJsonValueNumber>(Anchors->Maximum.Y)
		});
		SlotJson->SetObjectField(TEXT("Anchors"), AnchorsJson);
	}

	// Offsets (for CanvasPanelSlot)
	if (FStructProperty* OffsetsProp = CastField<FStructProperty>(SlotClass->FindPropertyByName(TEXT("Offsets"))))
	{
		void* ValuePtr = OffsetsProp->ContainerPtrToValuePtr<void>(Slot);
		FMargin* Offsets = static_cast<FMargin*>(ValuePtr);
		SlotJson->SetField(TEXT("Offsets"), MarginToJson(*Offsets));
	}

	// Alignment (for CanvasPanelSlot)
	if (FStructProperty* AlignmentProp = CastField<FStructProperty>(SlotClass->FindPropertyByName(TEXT("Alignment"))))
	{
		void* ValuePtr = AlignmentProp->ContainerPtrToValuePtr<void>(Slot);
		FVector2D* Alignment = static_cast<FVector2D*>(ValuePtr);
		SlotJson->SetField(TEXT("Alignment"), Vector2DToJson(*Alignment));
	}

	// Padding (for most slot types)
	if (FStructProperty* PaddingProp = CastField<FStructProperty>(SlotClass->FindPropertyByName(TEXT("Padding"))))
	{
		void* ValuePtr = PaddingProp->ContainerPtrToValuePtr<void>(Slot);
		FMargin* Padding = static_cast<FMargin*>(ValuePtr);
		// Only add if non-zero
		if (Padding->Left != 0 || Padding->Top != 0 || Padding->Right != 0 || Padding->Bottom != 0)
		{
			SlotJson->SetField(TEXT("Padding"), MarginToJson(*Padding));
		}
	}

	// HorizontalAlignment
	if (FProperty* HAlignProp = SlotClass->FindPropertyByName(TEXT("HorizontalAlignment")))
	{
		if (FByteProperty* ByteProp = CastField<FByteProperty>(HAlignProp))
		{
			void* ValuePtr = ByteProp->ContainerPtrToValuePtr<void>(Slot);
			EHorizontalAlignment HAlign = static_cast<EHorizontalAlignment>(ByteProp->GetUnsignedIntPropertyValue(ValuePtr));
			if (HAlign != HAlign_Fill)
			{
				SlotJson->SetStringField(TEXT("HorizontalAlignment"), HorizontalAlignmentToString(HAlign));
			}
		}
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(HAlignProp))
		{
			void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Slot);
			int64 EnumValue = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			EHorizontalAlignment HAlign = static_cast<EHorizontalAlignment>(EnumValue);
			if (HAlign != HAlign_Fill)
			{
				SlotJson->SetStringField(TEXT("HorizontalAlignment"), HorizontalAlignmentToString(HAlign));
			}
		}
	}

	// VerticalAlignment
	if (FProperty* VAlignProp = SlotClass->FindPropertyByName(TEXT("VerticalAlignment")))
	{
		if (FByteProperty* ByteProp = CastField<FByteProperty>(VAlignProp))
		{
			void* ValuePtr = ByteProp->ContainerPtrToValuePtr<void>(Slot);
			EVerticalAlignment VAlign = static_cast<EVerticalAlignment>(ByteProp->GetUnsignedIntPropertyValue(ValuePtr));
			if (VAlign != VAlign_Fill)
			{
				SlotJson->SetStringField(TEXT("VerticalAlignment"), VerticalAlignmentToString(VAlign));
			}
		}
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(VAlignProp))
		{
			void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Slot);
			int64 EnumValue = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			EVerticalAlignment VAlign = static_cast<EVerticalAlignment>(EnumValue);
			if (VAlign != VAlign_Fill)
			{
				SlotJson->SetStringField(TEXT("VerticalAlignment"), VerticalAlignmentToString(VAlign));
			}
		}
	}

	// Row/Column (for GridSlot)
	if (FIntProperty* RowProp = CastField<FIntProperty>(SlotClass->FindPropertyByName(TEXT("Row"))))
	{
		void* ValuePtr = RowProp->ContainerPtrToValuePtr<void>(Slot);
		int32 Row = RowProp->GetPropertyValue(ValuePtr);
		if (Row != 0)
		{
			SlotJson->SetNumberField(TEXT("Row"), Row);
		}
	}
	if (FIntProperty* ColProp = CastField<FIntProperty>(SlotClass->FindPropertyByName(TEXT("Column"))))
	{
		void* ValuePtr = ColProp->ContainerPtrToValuePtr<void>(Slot);
		int32 Column = ColProp->GetPropertyValue(ValuePtr);
		if (Column != 0)
		{
			SlotJson->SetNumberField(TEXT("Column"), Column);
		}
	}

	return SlotJson;
}

TSharedPtr<FJsonObject> FWidgetBlueprintGenerator::ExtractWidgetProperties(UWidget* Widget, bool bDiffOnly) const
{
	if (!Widget)
	{
		return nullptr;
	}

	// Use the generic property extraction utility
	TSharedPtr<FJsonObject> AllProperties = FPropertySetterUtils::ExtractPropertiesToJson(Widget, true, bDiffOnly);

	if (!AllProperties.IsValid())
	{
		return nullptr;
	}

	// Filter out UWidget base properties that are not useful for JSON config
	TSharedPtr<FJsonObject> FilteredProperties = MakeShared<FJsonObject>();
	UClass* WidgetClass = Widget->GetClass();

	// Properties to exclude (from UWidget base class that are not useful in config)
	static const TSet<FString> ExcludedProperties = {
		TEXT("Slot"), TEXT("bIsVariable"), TEXT("ToolTipText"), TEXT("Cursor"),
		TEXT("Visibility"), TEXT("RenderTransform"), TEXT("RenderTransformPivot"),
		TEXT("bIsEnabled"), TEXT("Navigation"), TEXT("FlowDirectionPreference"),
		TEXT("AccessibleBehavior"), TEXT("AccessibleSummaryBehavior"),
		TEXT("AccessibleText"), TEXT("AccessibleSummaryText")
	};

	for (const auto& Pair : AllProperties->Values)
	{
		// Skip excluded properties
		if (ExcludedProperties.Contains(Pair.Key))
		{
			continue;
		}

		// Skip event delegates (start with "On")
		if (Pair.Key.StartsWith(TEXT("On")))
		{
			continue;
		}

		FProperty* Property = WidgetClass->FindPropertyByName(*Pair.Key);
		if (Property)
		{
			UClass* OwnerClass = Property->GetOwnerClass();
			// Keep properties from the widget's own class or intermediate classes
			// but filter out base UWidget/UVisual properties (except important ones)
			if (OwnerClass != UWidget::StaticClass() && OwnerClass != UObject::StaticClass())
			{
				FilteredProperties->SetField(Pair.Key, Pair.Value);
			}
			// Keep certain important UWidget properties
			else if (OwnerClass == UWidget::StaticClass())
			{
				// These are properties that are commonly configured
				if (Pair.Key == TEXT("ToolTipWidget") || Pair.Key == TEXT("Clipping"))
				{
					FilteredProperties->SetField(Pair.Key, Pair.Value);
				}
			}
		}
	}

	return FilteredProperties;
}

FString FWidgetBlueprintGenerator::HorizontalAlignmentToString(EHorizontalAlignment Alignment) const
{
	switch (Alignment)
	{
	case HAlign_Left: return TEXT("Left");
	case HAlign_Center: return TEXT("Center");
	case HAlign_Right: return TEXT("Right");
	case HAlign_Fill: return TEXT("Fill");
	default: return TEXT("Fill");
	}
}

FString FWidgetBlueprintGenerator::VerticalAlignmentToString(EVerticalAlignment Alignment) const
{
	switch (Alignment)
	{
	case VAlign_Top: return TEXT("Top");
	case VAlign_Center: return TEXT("Center");
	case VAlign_Bottom: return TEXT("Bottom");
	case VAlign_Fill: return TEXT("Fill");
	default: return TEXT("Fill");
	}
}

TSharedPtr<FJsonValue> FWidgetBlueprintGenerator::ColorToJson(const FLinearColor& Color) const
{
	TArray<TSharedPtr<FJsonValue>> ColorArray;
	ColorArray.Add(MakeShared<FJsonValueNumber>(Color.R));
	ColorArray.Add(MakeShared<FJsonValueNumber>(Color.G));
	ColorArray.Add(MakeShared<FJsonValueNumber>(Color.B));
	ColorArray.Add(MakeShared<FJsonValueNumber>(Color.A));
	return MakeShared<FJsonValueArray>(ColorArray);
}

TSharedPtr<FJsonValue> FWidgetBlueprintGenerator::Vector2DToJson(const FVector2D& Vector) const
{
	TArray<TSharedPtr<FJsonValue>> VectorArray;
	VectorArray.Add(MakeShared<FJsonValueNumber>(Vector.X));
	VectorArray.Add(MakeShared<FJsonValueNumber>(Vector.Y));
	return MakeShared<FJsonValueArray>(VectorArray);
}

TSharedPtr<FJsonValue> FWidgetBlueprintGenerator::MarginToJson(const FMargin& Margin) const
{
	TArray<TSharedPtr<FJsonValue>> MarginArray;
	MarginArray.Add(MakeShared<FJsonValueNumber>(Margin.Left));
	MarginArray.Add(MakeShared<FJsonValueNumber>(Margin.Top));
	MarginArray.Add(MakeShared<FJsonValueNumber>(Margin.Right));
	MarginArray.Add(MakeShared<FJsonValueNumber>(Margin.Bottom));
	return MakeShared<FJsonValueArray>(MarginArray);
}
