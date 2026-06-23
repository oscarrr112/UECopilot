// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/WidgetBlueprintTreeAdapter.h"

#include "AssetDocumentPropertyAdapter.h"
#include "Utils/ClassFinderUtils.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "WidgetBlueprint.h"

namespace
{
struct FWidgetBlueprintNodeSpec
{
	FName Name;
	FString ClassPath;
	UClass* WidgetClass = nullptr;
	bool bIsVariable = false;
	FString VariableName;
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> Slot = MakeShared<FJsonObject>();
	TArray<FWidgetBlueprintNodeSpec> Children;
};

struct FWidgetBlueprintNamedSlotSpec
{
	FName SlotName;
	FWidgetBlueprintNodeSpec Widget;
};

struct FWidgetBlueprintTreeSpec
{
	bool bHasRootWidget = false;
	FWidgetBlueprintNodeSpec RootWidget;
	TArray<FWidgetBlueprintNamedSlotSpec> NamedSlotBindings;
};

FAssetDocumentCapabilityResult TreeFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FString EscapePathToken(const FString& Token)
{
	return Token.Replace(TEXT("~"), TEXT("~0")).Replace(TEXT("/"), TEXT("~1"));
}

FString JsonValueToComparableString(TSharedPtr<FJsonValue> Value)
{
	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Value.IsValid() ? Value.ToSharedRef() : MakeShared<FJsonValueNull>(), TEXT(""), Writer);
	return JsonText;
}

void AddDiffEntry(
	TArray<TSharedPtr<FJsonValue>>& Entries,
	const FString& Path,
	const FString& Status,
	TSharedPtr<FJsonValue> Current,
	TSharedPtr<FJsonValue> Desired)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), Path);
	Entry->SetStringField(TEXT("status"), Status);
	Entry->SetField(TEXT("current"), Current.IsValid() ? Current : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("desired"), Desired.IsValid() ? Desired : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

TSharedPtr<FJsonValue> CloneJsonValue(TSharedPtr<FJsonValue> Value)
{
	if (!Value.IsValid())
	{
		return MakeShared<FJsonValueNull>();
	}
	return Value;
}

void AddRecursiveDiffEntries(
	TArray<TSharedPtr<FJsonValue>>& Entries,
	const FString& Path,
	TSharedPtr<FJsonValue> Current,
	TSharedPtr<FJsonValue> Desired)
{
	if (JsonValueToComparableString(Current) == JsonValueToComparableString(Desired))
	{
		AddDiffEntry(Entries, Path, TEXT("unchanged"), CloneJsonValue(Current), CloneJsonValue(Desired));
		return;
	}

	if (Current.IsValid() && Desired.IsValid() && Current->Type == EJson::Object && Desired->Type == EJson::Object)
	{
		TSet<FString> Keys;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Current->AsObject()->Values)
		{
			Keys.Add(Pair.Key);
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Desired->AsObject()->Values)
		{
			Keys.Add(Pair.Key);
		}

		TArray<FString> SortedKeys = Keys.Array();
		SortedKeys.Sort();
		for (const FString& Key : SortedKeys)
		{
			const TSharedPtr<FJsonValue>* CurrentChild = Current->AsObject()->Values.Find(Key);
			const TSharedPtr<FJsonValue>* DesiredChild = Desired->AsObject()->Values.Find(Key);
			AddRecursiveDiffEntries(
				Entries,
				FString::Printf(TEXT("%s/%s"), *Path, *EscapePathToken(Key)),
				CurrentChild ? *CurrentChild : MakeShared<FJsonValueNull>(),
				DesiredChild ? *DesiredChild : MakeShared<FJsonValueNull>());
		}
		return;
	}

	if (Current.IsValid() && Desired.IsValid() && Current->Type == EJson::Array && Desired->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& CurrentArray = Current->AsArray();
		const TArray<TSharedPtr<FJsonValue>>& DesiredArray = Desired->AsArray();
		const int32 MaxCount = FMath::Max(CurrentArray.Num(), DesiredArray.Num());
		for (int32 Index = 0; Index < MaxCount; ++Index)
		{
			AddRecursiveDiffEntries(
				Entries,
				FString::Printf(TEXT("%s/%d"), *Path, Index),
				CurrentArray.IsValidIndex(Index) ? CurrentArray[Index] : MakeShared<FJsonValueNull>(),
				DesiredArray.IsValidIndex(Index) ? DesiredArray[Index] : MakeShared<FJsonValueNull>());
		}
		return;
	}

	AddDiffEntry(Entries, Path, TEXT("changed"), CloneJsonValue(Current), CloneJsonValue(Desired));
}

UClass* ResolveWidgetClass(const FString& ClassPath)
{
	UClass* WidgetClass = StaticLoadClass(UWidget::StaticClass(), nullptr, *ClassPath);
	if (!WidgetClass)
	{
		WidgetClass = FClassFinderUtils::FindClassByName(ClassPath, UWidget::StaticClass(), true);
	}
	return WidgetClass && WidgetClass->IsChildOf(UWidget::StaticClass()) ? WidgetClass : nullptr;
}

FString NodePath(const FString& ParentPath, const FString& ChildName)
{
	return FString::Printf(TEXT("%s/%s"), *ParentPath, *EscapePathToken(ChildName));
}

FAssetDocumentCapabilityResult ParseWidgetNode(
	const TSharedPtr<FJsonValue>& NodeValue,
	const FString& Path,
	TSet<FName>& UsedNames,
	FWidgetBlueprintNodeSpec& OutNode)
{
	if (!NodeValue.IsValid() || NodeValue->Type != EJson::Object)
	{
		return TreeFailure(TEXT("Widget node must be an object"), Path, TEXT("InvalidWidgetNode"));
	}

	const TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
	if (!NodeObject.IsValid())
	{
		return TreeFailure(TEXT("Widget node must be an object"), Path, TEXT("InvalidWidgetNode"));
	}

	FString Name;
	if (!NodeObject->TryGetStringField(TEXT("Name"), Name) || Name.IsEmpty())
	{
		return TreeFailure(TEXT("Widget node requires non-empty Name"), NodePath(Path, TEXT("Name")), TEXT("MissingWidgetName"));
	}
	OutNode.Name = FName(*Name);
	if (UsedNames.Contains(OutNode.Name))
	{
		return TreeFailure(
			FString::Printf(TEXT("Duplicate widget name '%s'"), *Name),
			NodePath(Path, TEXT("Name")),
			TEXT("DuplicateWidgetName"));
	}
	UsedNames.Add(OutNode.Name);

	if (!NodeObject->TryGetStringField(TEXT("Class"), OutNode.ClassPath) || OutNode.ClassPath.IsEmpty())
	{
		return TreeFailure(TEXT("Widget node requires non-empty Class"), NodePath(Path, TEXT("Class")), TEXT("MissingWidgetClass"));
	}
	OutNode.WidgetClass = ResolveWidgetClass(OutNode.ClassPath);
	if (!OutNode.WidgetClass)
	{
		return TreeFailure(
			FString::Printf(TEXT("Failed to resolve widget class '%s'"), *OutNode.ClassPath),
			NodePath(Path, TEXT("Class")),
			OutNode.ClassPath.StartsWith(TEXT("/Script/")) ? TEXT("InvalidWidgetClass") : TEXT("UnresolvedWidgetClass"));
	}

	NodeObject->TryGetBoolField(TEXT("IsVariable"), OutNode.bIsVariable);
	NodeObject->TryGetStringField(TEXT("VariableName"), OutNode.VariableName);

	if (const TSharedPtr<FJsonValue>* PropertiesValue = NodeObject->Values.Find(TEXT("Properties")))
	{
		if (!PropertiesValue->IsValid() || (*PropertiesValue)->Type != EJson::Object)
		{
			return TreeFailure(TEXT("Widget node Properties must be an object"), NodePath(Path, TEXT("Properties")), TEXT("InvalidWidgetProperties"));
		}
		OutNode.Properties = (*PropertiesValue)->AsObject();
	}

	if (const TSharedPtr<FJsonValue>* SlotValue = NodeObject->Values.Find(TEXT("Slot")))
	{
		if (!SlotValue->IsValid() || (*SlotValue)->Type != EJson::Object)
		{
			return TreeFailure(TEXT("Widget node Slot must be an object"), NodePath(Path, TEXT("Slot")), TEXT("InvalidWidgetSlot"));
		}
		OutNode.Slot = (*SlotValue)->AsObject();
	}

	if (const TSharedPtr<FJsonValue>* ChildrenValue = NodeObject->Values.Find(TEXT("Children")))
	{
		if (!ChildrenValue->IsValid() || (*ChildrenValue)->Type != EJson::Array)
		{
			return TreeFailure(TEXT("Widget node Children must be an array"), NodePath(Path, TEXT("Children")), TEXT("InvalidWidgetChildren"));
		}

		const TArray<TSharedPtr<FJsonValue>>& Children = (*ChildrenValue)->AsArray();
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			FWidgetBlueprintNodeSpec Child;
			const FAssetDocumentCapabilityResult ChildResult = ParseWidgetNode(
				Children[Index],
				FString::Printf(TEXT("%s/Children/%d"), *Path, Index),
				UsedNames,
				Child);
			if (!ChildResult.bSuccess)
			{
				return ChildResult;
			}
			OutNode.Children.Add(MoveTemp(Child));
		}
	}

	const FAssetDocumentPropertyApplyResult PropertyResult =
		FAssetDocumentPropertyAdapter::PreflightProperties(OutNode.WidgetClass, OutNode.Properties);
	if (!PropertyResult.bSuccess)
	{
		FAssetDocumentCapabilityResult Result = TreeFailure(PropertyResult.Message, NodePath(Path, TEXT("Properties")), TEXT("InvalidWidgetProperty"));
		Result.Diagnostics.Reset();
		for (FAssetDocumentDiagnostic Diagnostic : PropertyResult.Diagnostics)
		{
			Diagnostic.Path = FString::Printf(TEXT("%s/%s"), *NodePath(Path, TEXT("Properties")), *Diagnostic.Path);
			Result.Diagnostics.Add(MoveTemp(Diagnostic));
		}
		if (Result.Diagnostics.Num() == 0)
		{
			FAssetDocumentDiagnostic Diagnostic;
			Diagnostic.Path = NodePath(Path, TEXT("Properties"));
			Diagnostic.Code = TEXT("InvalidWidgetProperty");
			Diagnostic.Message = PropertyResult.Message;
			Result.Diagnostics.Add(MoveTemp(Diagnostic));
		}
		return Result;
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseWidgetTree(
	const TSharedPtr<FJsonValue>& WidgetTreeValue,
	FWidgetBlueprintTreeSpec& OutSpec)
{
	TSharedPtr<FJsonObject> WidgetTreeObject;
	if (!WidgetTreeValue.IsValid() || WidgetTreeValue->Type == EJson::Null)
	{
		WidgetTreeObject = FWidgetBlueprintTreeAdapter::MakeDefaultWidgetTree();
	}
	else if (WidgetTreeValue->Type == EJson::Object)
	{
		WidgetTreeObject = WidgetTreeValue->AsObject();
	}
	else
	{
		return TreeFailure(TEXT("Body.WidgetTree must be an object"), TEXT("/Body/WidgetTree"), TEXT("InvalidBodySectionType"));
	}

	if (!WidgetTreeObject.IsValid())
	{
		return TreeFailure(TEXT("Body.WidgetTree must be an object"), TEXT("/Body/WidgetTree"), TEXT("InvalidBodySectionType"));
	}

	TSet<FName> UsedNames;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : WidgetTreeObject->Values)
	{
		if (Pair.Key != TEXT("RootWidget") && Pair.Key != TEXT("NamedSlotBindings"))
		{
			return TreeFailure(
				FString::Printf(TEXT("Unknown Body.WidgetTree key '%s'"), *Pair.Key),
				FString::Printf(TEXT("/Body/WidgetTree/%s"), *EscapePathToken(Pair.Key)),
				TEXT("UnknownBodyKey"));
		}
	}

	if (const TSharedPtr<FJsonValue>* RootValue = WidgetTreeObject->Values.Find(TEXT("RootWidget")))
	{
		if (RootValue->IsValid() && (*RootValue)->Type != EJson::Null)
		{
			OutSpec.bHasRootWidget = true;
			const FAssetDocumentCapabilityResult RootResult = ParseWidgetNode(
				*RootValue,
				TEXT("/Body/WidgetTree/RootWidget"),
				UsedNames,
				OutSpec.RootWidget);
			if (!RootResult.bSuccess)
			{
				return RootResult;
			}
		}
	}

	if (const TSharedPtr<FJsonValue>* NamedSlotsValue = WidgetTreeObject->Values.Find(TEXT("NamedSlotBindings")))
	{
		if (!NamedSlotsValue->IsValid() || (*NamedSlotsValue)->Type != EJson::Object)
		{
			return TreeFailure(TEXT("Body.WidgetTree.NamedSlotBindings must be an object"), TEXT("/Body/WidgetTree/NamedSlotBindings"), TEXT("InvalidBodySectionType"));
		}

		const TSharedPtr<FJsonObject> NamedSlotObject = (*NamedSlotsValue)->AsObject();
		TArray<FString> SlotNames;
		NamedSlotObject->Values.GetKeys(SlotNames);
		SlotNames.Sort();
		for (const FString& SlotName : SlotNames)
		{
			if (SlotName.IsEmpty())
			{
				return TreeFailure(TEXT("NamedSlotBindings slot names must be non-empty"), TEXT("/Body/WidgetTree/NamedSlotBindings"), TEXT("InvalidNamedSlotName"));
			}

			FWidgetBlueprintNamedSlotSpec Binding;
			Binding.SlotName = FName(*SlotName);
			const FAssetDocumentCapabilityResult BindingResult = ParseWidgetNode(
				NamedSlotObject->Values.FindChecked(SlotName),
				FString::Printf(TEXT("/Body/WidgetTree/NamedSlotBindings/%s"), *EscapePathToken(SlotName)),
				UsedNames,
				Binding.Widget);
			if (!BindingResult.bSuccess)
			{
				return BindingResult;
			}
			OutSpec.NamedSlotBindings.Add(MoveTemp(Binding));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyPropertyResultAsCapability(
	const FAssetDocumentPropertyApplyResult& PropertyResult,
	const FString& Path,
	const FString& Code)
{
	FAssetDocumentCapabilityResult Result = TreeFailure(PropertyResult.Message, Path, Code);
	Result.Diagnostics.Reset();
	for (FAssetDocumentDiagnostic Diagnostic : PropertyResult.Diagnostics)
	{
		Diagnostic.Path = FString::Printf(TEXT("%s/%s"), *Path, *Diagnostic.Path);
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
	}
	if (Result.Diagnostics.Num() == 0)
	{
		FAssetDocumentDiagnostic Diagnostic;
		Diagnostic.Path = Path;
		Diagnostic.Code = Code;
		Diagnostic.Message = PropertyResult.Message;
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
	}
	return Result;
}

UWidget* ConstructWidgetFromSpec(UWidgetTree* WidgetTree, const FWidgetBlueprintNodeSpec& Spec)
{
	UWidget* Widget = WidgetTree->ConstructWidget<UWidget>(Spec.WidgetClass, Spec.Name);
	if (Widget)
	{
		Widget->bIsVariable = Spec.bIsVariable;
	}
	return Widget;
}

FAssetDocumentCapabilityResult MaterializeNode(
	UWidgetTree* WidgetTree,
	const FWidgetBlueprintNodeSpec& Spec,
	const FString& Path,
	UWidget*& OutWidget)
{
	OutWidget = ConstructWidgetFromSpec(WidgetTree, Spec);
	if (!OutWidget)
	{
		return TreeFailure(
			FString::Printf(TEXT("Failed to construct widget '%s'"), *Spec.Name.ToString()),
			Path,
			TEXT("ConstructWidgetFailed"));
	}

	const FAssetDocumentPropertyApplyResult WidgetPropertyResult =
		FAssetDocumentPropertyAdapter::ApplyProperties(OutWidget, Spec.Properties);
	if (!WidgetPropertyResult.bSuccess)
	{
		return ApplyPropertyResultAsCapability(WidgetPropertyResult, NodePath(Path, TEXT("Properties")), TEXT("InvalidWidgetProperty"));
	}

	if (Spec.Children.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	UPanelWidget* PanelWidget = Cast<UPanelWidget>(OutWidget);
	if (!PanelWidget)
	{
		return TreeFailure(
			FString::Printf(TEXT("Widget '%s' does not support authored children"), *Spec.Name.ToString()),
			NodePath(Path, TEXT("Children")),
			TEXT("UnsupportedWidgetChildren"));
	}

	for (int32 ChildIndex = 0; ChildIndex < Spec.Children.Num(); ++ChildIndex)
	{
		const FWidgetBlueprintNodeSpec& ChildSpec = Spec.Children[ChildIndex];
		UWidget* ChildWidget = nullptr;
		const FString ChildPath = FString::Printf(TEXT("%s/Children/%d"), *Path, ChildIndex);
		const FAssetDocumentCapabilityResult ChildResult = MaterializeNode(WidgetTree, ChildSpec, ChildPath, ChildWidget);
		if (!ChildResult.bSuccess)
		{
			return ChildResult;
		}

		UPanelSlot* Slot = PanelWidget->AddChild(ChildWidget);
		if (!Slot)
		{
			return TreeFailure(
				FString::Printf(TEXT("Failed to add child widget '%s' to '%s'"), *ChildSpec.Name.ToString(), *Spec.Name.ToString()),
				ChildPath,
				TEXT("AddChildFailed"));
		}

		const FAssetDocumentPropertyApplyResult SlotPropertyResult =
			FAssetDocumentPropertyAdapter::ApplyProperties(Slot, ChildSpec.Slot);
		if (!SlotPropertyResult.bSuccess)
		{
			return ApplyPropertyResultAsCapability(SlotPropertyResult, NodePath(ChildPath, TEXT("Slot")), TEXT("InvalidWidgetSlotProperty"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult MaterializeTree(UWidgetTree* WidgetTree, const FWidgetBlueprintTreeSpec& Spec)
{
	if (!WidgetTree)
	{
		return TreeFailure(TEXT("WidgetBlueprint has no WidgetTree"), TEXT("/Body/WidgetTree"), TEXT("MissingWidgetTree"));
	}

	WidgetTree->Modify();

	TArray<UWidget*> ExistingWidgets;
	WidgetTree->GetAllWidgets(ExistingWidgets);
	for (UWidget* ExistingWidget : ExistingWidgets)
	{
		WidgetTree->RemoveWidget(ExistingWidget);
	}
	WidgetTree->RootWidget = nullptr;
	WidgetTree->NamedSlotBindings.Empty();

	if (Spec.bHasRootWidget)
	{
		UWidget* RootWidget = nullptr;
		const FAssetDocumentCapabilityResult RootResult = MaterializeNode(WidgetTree, Spec.RootWidget, TEXT("/Body/WidgetTree/RootWidget"), RootWidget);
		if (!RootResult.bSuccess)
		{
			return RootResult;
		}
		WidgetTree->RootWidget = RootWidget;
	}

	for (const FWidgetBlueprintNamedSlotSpec& Binding : Spec.NamedSlotBindings)
	{
		UWidget* SlotWidget = nullptr;
		const FString BindingPath = FString::Printf(TEXT("/Body/WidgetTree/NamedSlotBindings/%s"), *EscapePathToken(Binding.SlotName.ToString()));
		const FAssetDocumentCapabilityResult BindingResult = MaterializeNode(WidgetTree, Binding.Widget, BindingPath, SlotWidget);
		if (!BindingResult.bSuccess)
		{
			return BindingResult;
		}
		WidgetTree->NamedSlotBindings.Add(Binding.SlotName, SlotWidget);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult PreflightTreeMaterialization(const FWidgetBlueprintTreeSpec& Spec)
{
	UWidgetTree* PreviewTree = NewObject<UWidgetTree>(GetTransientPackage(), UWidgetTree::StaticClass());
	if (!PreviewTree)
	{
		return TreeFailure(TEXT("Failed to create transient WidgetTree for preflight"), TEXT("/Body/WidgetTree"), TEXT("WidgetTreePreflightFailed"));
	}
	return MaterializeTree(PreviewTree, Spec);
}

TSharedRef<FJsonObject> ExtractNode(UWidget* Widget)
{
	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Name"), Widget ? Widget->GetName() : FString());
	Node->SetStringField(TEXT("Class"), Widget && Widget->GetClass() ? Widget->GetClass()->GetPathName() : FString());
	Node->SetBoolField(TEXT("IsVariable"), Widget && Widget->bIsVariable);
	if (Widget && Widget->bIsVariable)
	{
		Node->SetStringField(TEXT("VariableName"), Widget->GetName());
	}

	TSharedPtr<FJsonObject> Properties = Widget
		? FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(Widget, true)
		: MakeShared<FJsonObject>();
	Node->SetObjectField(TEXT("Properties"), Properties.IsValid() ? Properties : MakeShared<FJsonObject>());

	TSharedPtr<FJsonObject> Slot = Widget && Widget->Slot
		? FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(Widget->Slot, true)
		: MakeShared<FJsonObject>();
	Node->SetObjectField(TEXT("Slot"), Slot.IsValid() ? Slot : MakeShared<FJsonObject>());

	TArray<TSharedPtr<FJsonValue>> Children;
	if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
	{
		for (int32 ChildIndex = 0; ChildIndex < PanelWidget->GetChildrenCount(); ++ChildIndex)
		{
			if (UWidget* Child = PanelWidget->GetChildAt(ChildIndex))
			{
				Children.Add(MakeShared<FJsonValueObject>(ExtractNode(Child)));
			}
		}
	}
	Node->SetArrayField(TEXT("Children"), Children);
	return Node;
}

TSharedRef<FJsonObject> ExtractTreeObject(const UWidgetTree* WidgetTree)
{
	TSharedRef<FJsonObject> WidgetTreeJson = FWidgetBlueprintTreeAdapter::MakeDefaultWidgetTree();
	if (!WidgetTree)
	{
		return WidgetTreeJson;
	}

	if (WidgetTree->RootWidget)
	{
		WidgetTreeJson->SetObjectField(TEXT("RootWidget"), ExtractNode(WidgetTree->RootWidget));
	}

	TArray<FName> SlotNames;
	WidgetTree->NamedSlotBindings.GetKeys(SlotNames);
	SlotNames.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TSharedRef<FJsonObject> NamedSlotBindings = MakeShared<FJsonObject>();
	for (const FName& SlotName : SlotNames)
	{
		if (UWidget* SlotWidget = WidgetTree->NamedSlotBindings.FindRef(SlotName))
		{
			NamedSlotBindings->SetObjectField(SlotName.ToString(), ExtractNode(SlotWidget));
		}
	}
	WidgetTreeJson->SetObjectField(TEXT("NamedSlotBindings"), NamedSlotBindings);
	return WidgetTreeJson;
}
}

TSharedRef<FJsonObject> FWidgetBlueprintTreeAdapter::MakeDefaultWidgetTree()
{
	TSharedRef<FJsonObject> WidgetTree = MakeShared<FJsonObject>();
	WidgetTree->SetField(TEXT("RootWidget"), MakeShared<FJsonValueNull>());
	WidgetTree->SetObjectField(TEXT("NamedSlotBindings"), MakeShared<FJsonObject>());
	return WidgetTree;
}

FAssetDocumentCapabilityResult FWidgetBlueprintTreeAdapter::Validate(const TSharedPtr<FJsonValue>& WidgetTreeJson)
{
	FWidgetBlueprintTreeSpec Spec;
	return ParseWidgetTree(WidgetTreeJson, Spec);
}

FAssetDocumentCapabilityResult FWidgetBlueprintTreeAdapter::Preflight(const UWidgetBlueprint*, const TSharedPtr<FJsonValue>& WidgetTreeJson)
{
	FWidgetBlueprintTreeSpec Spec;
	const FAssetDocumentCapabilityResult ParseResult = ParseWidgetTree(WidgetTreeJson, Spec);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}
	return PreflightTreeMaterialization(Spec);
}

FAssetDocumentCapabilityResult FWidgetBlueprintTreeAdapter::Apply(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& WidgetTreeJson)
{
	if (!WidgetBlueprint)
	{
		return TreeFailure(TEXT("WidgetTree apply requires a WidgetBlueprint asset"), TEXT("/Body/WidgetTree"), TEXT("UnsupportedAsset"));
	}

	FWidgetBlueprintTreeSpec Spec;
	const FAssetDocumentCapabilityResult ParseResult = ParseWidgetTree(WidgetTreeJson, Spec);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}

	const FAssetDocumentCapabilityResult PreflightResult = PreflightTreeMaterialization(Spec);
	if (!PreflightResult.bSuccess)
	{
		return PreflightResult;
	}

	if (!WidgetBlueprint->WidgetTree)
	{
		WidgetBlueprint->WidgetTree = NewObject<UWidgetTree>(WidgetBlueprint, TEXT("WidgetTree"), RF_Transactional);
	}

	return MaterializeTree(WidgetBlueprint->WidgetTree, Spec);
}

FAssetDocumentCapabilityResult FWidgetBlueprintTreeAdapter::Extract(const UWidgetBlueprint* WidgetBlueprint, TSharedRef<FJsonObject>& OutWidgetTreeJson)
{
	OutWidgetTreeJson = ExtractTreeObject(WidgetBlueprint ? WidgetBlueprint->WidgetTree : nullptr);
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted WidgetTree"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintTreeAdapter::Diff(const UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	FWidgetBlueprintTreeSpec Spec;
	const FAssetDocumentCapabilityResult ValidateResult = ParseWidgetTree(DesiredJson, Spec);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	TSharedRef<FJsonObject> CurrentTree = MakeDefaultWidgetTree();
	const FAssetDocumentCapabilityResult ExtractResult = Extract(WidgetBlueprint, CurrentTree);
	if (!ExtractResult.bSuccess)
	{
		return ExtractResult;
	}

	UWidgetTree* DesiredPreviewTree = NewObject<UWidgetTree>(GetTransientPackage(), UWidgetTree::StaticClass());
	if (!DesiredPreviewTree)
	{
		return TreeFailure(TEXT("Failed to create transient WidgetTree for diff"), TEXT("/Body/WidgetTree"), TEXT("WidgetTreeDiffFailed"));
	}
	const FAssetDocumentCapabilityResult DesiredMaterializeResult = MaterializeTree(DesiredPreviewTree, Spec);
	if (!DesiredMaterializeResult.bSuccess)
	{
		return DesiredMaterializeResult;
	}
	TSharedRef<FJsonObject> DesiredCanonicalTree = ExtractTreeObject(DesiredPreviewTree);

	AddRecursiveDiffEntries(
		OutDiffEntries,
		TEXT("/Body/WidgetTree"),
		MakeShared<FJsonValueObject>(CurrentTree),
		MakeShared<FJsonValueObject>(DesiredCanonicalTree));
	return FAssetDocumentCapabilityResult::Success(TEXT("WidgetTree diffed"));
}
