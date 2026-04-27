// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/WidgetBlueprintGenerator.h"
#include "AssetFactoryModule.h"
#include "Utils/ClassFinderUtils.h"
#include "Utils/PropertySetterUtils.h"

// Blueprint creation
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"

// Widget Blueprint - only base classes needed
#include "Binding/PropertyBinding.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"

// Styling
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"

// Asset handling
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"

// Reflection and property helpers
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/Field.h"

// EdGraph for function validation
#include "EdGraph/EdGraph.h"

namespace
{
struct FWidgetBindingSpec
{
	FString WidgetName;
	FName TargetProperty;
	EBindingKind Kind = EBindingKind::Function;
	FName FunctionName = NAME_None;
	FName SourceProperty = NAME_None;
	TArray<FString> SourcePathSegments;
};

static FString BindingTargetToString(const FString& WidgetName, FName TargetProperty)
{
	return FString::Printf(TEXT("%s.%s"), *WidgetName, *TargetProperty.ToString());
}

static FString BindingTargetKey(const FString& WidgetName, FName TargetProperty)
{
	return BindingTargetToString(WidgetName, TargetProperty).ToLower();
}

static bool ReadStringArrayField(TSharedPtr<FJsonObject> Object, const TCHAR* FieldName, TArray<FString>& OutValues)
{
	if (!Object.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
	if (!Object->TryGetArrayField(FieldName, ArrayValues) || !ArrayValues)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *ArrayValues)
	{
		FString Segment;
		if (!Value.IsValid() || !Value->TryGetString(Segment) || Segment.IsEmpty())
		{
			return false;
		}
		OutValues.Add(Segment);
	}
	return OutValues.Num() > 0;
}

static bool ParseBindingKind(const FString& KindString, EBindingKind& OutKind)
{
	if (KindString.Equals(TEXT("Property"), ESearchCase::IgnoreCase))
	{
		OutKind = EBindingKind::Property;
		return true;
	}
	if (KindString.Equals(TEXT("Function"), ESearchCase::IgnoreCase))
	{
		OutKind = EBindingKind::Function;
		return true;
	}
	return false;
}

static bool ParseBindingSpec(
	const FString& WidgetName,
	const FString& PropertyName,
	const TSharedPtr<FJsonValue>& BindingValue,
	FWidgetBindingSpec& OutSpec,
	FString& OutError)
{
	if (WidgetName.IsEmpty())
	{
		OutError = TEXT("Binding is missing widget name");
		return false;
	}

	if (PropertyName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Binding for widget '%s' is missing target property"), *WidgetName);
		return false;
	}

	OutSpec = FWidgetBindingSpec();
	OutSpec.WidgetName = WidgetName;
	OutSpec.TargetProperty = FName(*PropertyName);
	const FString BindingTarget = BindingTargetToString(OutSpec.WidgetName, OutSpec.TargetProperty);

	FString SimpleFunctionName;
	if (BindingValue.IsValid() && BindingValue->TryGetString(SimpleFunctionName))
	{
		if (SimpleFunctionName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Binding '%s' has an empty function name"), *BindingTarget);
			return false;
		}
		OutSpec.Kind = EBindingKind::Function;
		OutSpec.FunctionName = FName(*SimpleFunctionName);
		return true;
	}

	if (!BindingValue.IsValid() || BindingValue->Type != EJson::Object)
	{
		OutError = FString::Printf(TEXT("Binding '%s' must be a function string or object"), *BindingTarget);
		return false;
	}

	TSharedPtr<FJsonObject> BindingObj = BindingValue->AsObject();
	if (!BindingObj.IsValid())
	{
		OutError = FString::Printf(TEXT("Binding '%s' must be a function string or object"), *BindingTarget);
		return false;
	}

	bool bHasKind = false;
	FString KindString;
	if (BindingObj->TryGetStringField(TEXT("Kind"), KindString))
	{
		bHasKind = true;
		if (!ParseBindingKind(KindString, OutSpec.Kind))
		{
			OutError = FString::Printf(TEXT("Binding '%s' has invalid Kind '%s'"), *BindingTarget, *KindString);
			return false;
		}
	}

	FString FunctionName;
	if (BindingObj->TryGetStringField(TEXT("Function"), FunctionName))
	{
		if (FunctionName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Binding '%s' has an empty function name"), *BindingTarget);
			return false;
		}
		OutSpec.FunctionName = FName(*FunctionName);
		if (!bHasKind)
		{
			OutSpec.Kind = EBindingKind::Function;
		}
	}

	FString SourceProperty;
	if (BindingObj->TryGetStringField(TEXT("Property"), SourceProperty))
	{
		if (SourceProperty.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Binding '%s' has an empty source property"), *BindingTarget);
			return false;
		}
		OutSpec.SourceProperty = FName(*SourceProperty);
		OutSpec.SourcePathSegments.Add(SourceProperty);
		if (!bHasKind)
		{
			OutSpec.Kind = EBindingKind::Property;
		}
	}

	TArray<FString> SourcePathSegments;
	if (ReadStringArrayField(BindingObj, TEXT("SourcePath"), SourcePathSegments))
	{
		OutSpec.SourcePathSegments = SourcePathSegments;
		if (OutSpec.SourcePathSegments.Num() == 1)
		{
			OutSpec.SourceProperty = FName(*OutSpec.SourcePathSegments[0]);
		}
		if (!bHasKind)
		{
			OutSpec.Kind = EBindingKind::Property;
		}
	}
	else if (BindingObj->HasField(TEXT("SourcePath")))
	{
		OutError = FString::Printf(TEXT("Binding '%s' SourcePath must be a non-empty string array"), *BindingTarget);
		return false;
	}

	if (OutSpec.Kind == EBindingKind::Function && OutSpec.FunctionName.IsNone())
	{
		if (OutSpec.SourcePathSegments.Num() > 0)
		{
			OutSpec.FunctionName = FName(*OutSpec.SourcePathSegments.Last());
		}
		else
		{
			OutError = FString::Printf(TEXT("Binding '%s' is a function binding but has no Function"), *BindingTarget);
			return false;
		}
	}

	if (OutSpec.Kind == EBindingKind::Property && OutSpec.SourceProperty.IsNone() && OutSpec.SourcePathSegments.Num() == 0)
	{
		OutError = FString::Printf(TEXT("Binding '%s' is a property binding but has no Property or SourcePath"), *BindingTarget);
		return false;
	}

	return true;
}

static bool ParseTopLevelBindingKey(const FString& BindingTarget, FString& OutWidgetName, FString& OutPropertyName)
{
	int32 SeparatorIndex = INDEX_NONE;
	if (!BindingTarget.FindLastChar(TEXT('.'), SeparatorIndex) || SeparatorIndex <= 0 || SeparatorIndex >= BindingTarget.Len() - 1)
	{
		return false;
	}

	OutWidgetName = BindingTarget.Left(SeparatorIndex);
	OutPropertyName = BindingTarget.Mid(SeparatorIndex + 1);
	return !OutWidgetName.IsEmpty() && !OutPropertyName.IsEmpty();
}

static FDelegateProperty* ResolveBindingDelegateProperty(UWidget* Widget, FName TargetProperty, bool& bOutIsPropertyDelegate)
{
	bOutIsPropertyDelegate = false;
	if (!Widget || TargetProperty.IsNone())
	{
		return nullptr;
	}

	const FName PropertyDelegateName(*(TargetProperty.ToString() + TEXT("Delegate")));
	if (FDelegateProperty* PropertyDelegate = FindFProperty<FDelegateProperty>(Widget->GetClass(), PropertyDelegateName))
	{
		bOutIsPropertyDelegate = true;
		return PropertyDelegate;
	}

	return FindFProperty<FDelegateProperty>(Widget->GetClass(), TargetProperty);
}

static UClass* ResolveBindingSourceClass(UWidgetBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	if (Blueprint->ParentClass)
	{
		return Blueprint->ParentClass;
	}
	if (Blueprint->GeneratedClass)
	{
		return Blueprint->GeneratedClass;
	}
	return Blueprint->SkeletonGeneratedClass;
}

static FProperty* GetBindableReturnProperty(UFunction* Function)
{
	if (!Function || Function->NumParms != 1)
	{
		return nullptr;
	}
	return Function->GetReturnProperty();
}

static bool HasFunctionBinder(UFunction* Function, UFunction* DelegateSignature)
{
	FProperty* FunctionReturn = GetBindableReturnProperty(Function);
	FProperty* DelegateReturn = GetBindableReturnProperty(DelegateSignature);
	if (!FunctionReturn || !DelegateReturn)
	{
		return false;
	}

	TSubclassOf<UPropertyBinding> Binder = UWidget::FindBinderClassForDestination(DelegateReturn);
	return Binder && Binder->GetDefaultObject<UPropertyBinding>()->IsSupportedSource(FunctionReturn);
}

static UEdGraph* FindFunctionGraph(UWidgetBlueprint* Blueprint, FName FunctionName)
{
	if (!Blueprint || FunctionName.IsNone())
	{
		return nullptr;
	}

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FunctionName)
		{
			return Graph;
		}
	}
	return nullptr;
}

static UFunction* ResolveBindingFunction(UWidgetBlueprint* Blueprint, FName FunctionName, FGuid& OutMemberGuid)
{
	OutMemberGuid.Invalidate();
	if (!Blueprint || FunctionName.IsNone())
	{
		return nullptr;
	}

	UClass* ClassesToCheck[] =
	{
		Blueprint->ParentClass,
		Blueprint->SkeletonGeneratedClass,
		Blueprint->GeneratedClass
	};

	for (UClass* ClassToCheck : ClassesToCheck)
	{
		if (!ClassToCheck)
		{
			continue;
		}

		if (UFunction* Function = ClassToCheck->FindFunctionByName(FunctionName, EIncludeSuperFlag::IncludeSuper))
		{
			if (UClass* OwnerClass = Function->GetOwnerClass())
			{
				UBlueprint::GetGuidFromClassByFieldName<UFunction>(OwnerClass, Function->GetFName(), OutMemberGuid);
			}
			return Function;
		}
	}

	if (UEdGraph* Graph = FindFunctionGraph(Blueprint, FunctionName))
	{
		OutMemberGuid = Graph->GraphGuid;
	}
	return nullptr;
}

static UStruct* ResolveNextBindingContainer(FProperty* Property)
{
	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		return ObjectProperty->PropertyClass;
	}
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		return StructProperty->Struct;
	}
	return nullptr;
}

static void TrySetMemberGuid(FProperty* Property, FGuid& OutMemberGuid)
{
	OutMemberGuid.Invalidate();
	if (Property)
	{
		if (UClass* OwnerClass = Property->GetOwnerClass())
		{
			UBlueprint::GetGuidFromClassByFieldName<FProperty>(OwnerClass, Property->GetFName(), OutMemberGuid);
		}
	}
}

static void TrySetMemberGuid(UFunction* Function, FGuid& OutMemberGuid)
{
	OutMemberGuid.Invalidate();
	if (Function)
	{
		if (UClass* OwnerClass = Function->GetOwnerClass())
		{
			UBlueprint::GetGuidFromClassByFieldName<UFunction>(OwnerClass, Function->GetFName(), OutMemberGuid);
		}
	}
}

static bool ResolvePropertyBindingChain(
	UClass* SourceClass,
	const TArray<FString>& SourcePathSegments,
	TArray<FFieldVariant>& OutChain,
	FName& OutSourceProperty,
	FGuid& OutMemberGuid,
	FText& OutError)
{
	OutChain.Reset();
	OutSourceProperty = NAME_None;
	OutMemberGuid.Invalidate();

	if (!SourceClass)
	{
		OutError = FText::FromString(TEXT("Unable to resolve binding source class"));
		return false;
	}
	if (SourcePathSegments.Num() == 0)
	{
		OutError = FText::FromString(TEXT("Property binding SourcePath is empty"));
		return false;
	}

	UStruct* CurrentStruct = SourceClass;
	for (int32 SegmentIndex = 0; SegmentIndex < SourcePathSegments.Num(); ++SegmentIndex)
	{
		const FString& SegmentString = SourcePathSegments[SegmentIndex];
		if (SegmentString.IsEmpty())
		{
			OutError = FText::FromString(FString::Printf(TEXT("SourcePath segment %d is empty"), SegmentIndex));
			return false;
		}

		const FName SegmentName(*SegmentString);
		const bool bHasRemainingSegments = SegmentIndex < SourcePathSegments.Num() - 1;

		if (FProperty* Property = FindFProperty<FProperty>(CurrentStruct, SegmentName))
		{
			OutChain.Add(FFieldVariant(Property));
			TrySetMemberGuid(Property, OutMemberGuid);

			if (bHasRemainingSegments)
			{
				UStruct* NextStruct = ResolveNextBindingContainer(Property);
				if (!NextStruct)
				{
					OutError = FText::FromString(FString::Printf(
						TEXT("SourcePath segment '%s' on '%s' does not expose an object or struct for remaining path"),
						*SegmentString,
						*CurrentStruct->GetName()));
					return false;
				}
				CurrentStruct = NextStruct;
			}
			else
			{
				OutSourceProperty = Property->GetFName();
			}
			continue;
		}

		UFunction* Function = nullptr;
		if (UClass* CurrentClass = Cast<UClass>(CurrentStruct))
		{
			Function = CurrentClass->FindFunctionByName(SegmentName, EIncludeSuperFlag::IncludeSuper);
		}

		if (Function)
		{
			FProperty* ReturnProperty = GetBindableReturnProperty(Function);
			if (!ReturnProperty)
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("SourcePath function segment '%s' on '%s' must take no parameters and return one value"),
					*SegmentString,
					*CurrentStruct->GetName()));
				return false;
			}

			if (!Function->HasAnyFunctionFlags(FUNC_Const | FUNC_BlueprintPure))
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("SourcePath function segment '%s' on '%s' must be const or BlueprintPure"),
					*SegmentString,
					*CurrentStruct->GetName()));
				return false;
			}

			OutChain.Add(FFieldVariant(Function));
			TrySetMemberGuid(Function, OutMemberGuid);

			if (bHasRemainingSegments)
			{
				UStruct* NextStruct = ResolveNextBindingContainer(ReturnProperty);
				if (!NextStruct)
				{
					OutError = FText::FromString(FString::Printf(
						TEXT("SourcePath function segment '%s' on '%s' returns a value that cannot expose remaining path"),
						*SegmentString,
						*CurrentStruct->GetName()));
					return false;
				}
				CurrentStruct = NextStruct;
			}
			continue;
		}

		OutError = FText::FromString(FString::Printf(
			TEXT("SourcePath segment '%s' was not found on '%s'"),
			*SegmentString,
			*CurrentStruct->GetName()));
		return false;
	}

	return true;
}

static bool ResolveFunctionBindingChain(
	UClass* SourceClass,
	const TArray<FString>& SourcePathSegments,
	TArray<FFieldVariant>& OutChain,
	UFunction*& OutFunction,
	FGuid& OutMemberGuid,
	FText& OutError)
{
	OutChain.Reset();
	OutFunction = nullptr;
	OutMemberGuid.Invalidate();

	if (!SourceClass)
	{
		OutError = FText::FromString(TEXT("Unable to resolve binding source class"));
		return false;
	}
	if (SourcePathSegments.Num() == 0)
	{
		OutError = FText::FromString(TEXT("Function binding SourcePath is empty"));
		return false;
	}

	UStruct* CurrentStruct = SourceClass;
	for (int32 SegmentIndex = 0; SegmentIndex < SourcePathSegments.Num(); ++SegmentIndex)
	{
		const FString& SegmentString = SourcePathSegments[SegmentIndex];
		if (SegmentString.IsEmpty())
		{
			OutError = FText::FromString(FString::Printf(TEXT("SourcePath segment %d is empty"), SegmentIndex));
			return false;
		}

		const FName SegmentName(*SegmentString);
		const bool bIsLastSegment = SegmentIndex == SourcePathSegments.Num() - 1;

		UFunction* Function = nullptr;
		if (UClass* CurrentClass = Cast<UClass>(CurrentStruct))
		{
			Function = CurrentClass->FindFunctionByName(SegmentName, EIncludeSuperFlag::IncludeSuper);
		}

		if (Function)
		{
			FProperty* ReturnProperty = GetBindableReturnProperty(Function);
			if (!ReturnProperty)
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("SourcePath function segment '%s' on '%s' must take no parameters and return one value"),
					*SegmentString,
					*CurrentStruct->GetName()));
				return false;
			}

			if (!Function->HasAnyFunctionFlags(FUNC_Const | FUNC_BlueprintPure))
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("SourcePath function segment '%s' on '%s' must be const or BlueprintPure"),
					*SegmentString,
					*CurrentStruct->GetName()));
				return false;
			}

			OutChain.Add(FFieldVariant(Function));
			TrySetMemberGuid(Function, OutMemberGuid);

			if (bIsLastSegment)
			{
				OutFunction = Function;
				return true;
			}

			UStruct* NextStruct = ResolveNextBindingContainer(ReturnProperty);
			if (!NextStruct)
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("SourcePath function segment '%s' on '%s' returns a value that cannot expose remaining path"),
					*SegmentString,
					*CurrentStruct->GetName()));
				return false;
			}
			CurrentStruct = NextStruct;
			continue;
		}

		FProperty* Property = FindFProperty<FProperty>(CurrentStruct, SegmentName);
		if (Property)
		{
			if (bIsLastSegment)
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("Function binding SourcePath must end in a function, but '%s' on '%s' is a property"),
					*SegmentString,
					*CurrentStruct->GetName()));
				return false;
			}

			OutChain.Add(FFieldVariant(Property));
			TrySetMemberGuid(Property, OutMemberGuid);

			UStruct* NextStruct = ResolveNextBindingContainer(Property);
			if (!NextStruct)
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("SourcePath segment '%s' on '%s' does not expose an object or struct for remaining path"),
					*SegmentString,
					*CurrentStruct->GetName()));
				return false;
			}
			CurrentStruct = NextStruct;
			continue;
		}

		OutError = FText::FromString(FString::Printf(
			TEXT("SourcePath segment '%s' was not found on '%s'"),
			*SegmentString,
			*CurrentStruct->GetName()));
		return false;
	}

	return OutFunction != nullptr;
}

static bool BuildFunctionBinding(
	UWidgetBlueprint* Blueprint,
	UWidget* Widget,
	const FWidgetBindingSpec& Spec,
	FDelegateEditorBinding& OutBinding,
	FText& OutError)
{
	bool bIsPropertyDelegate = false;
	FDelegateProperty* DelegateProperty = ResolveBindingDelegateProperty(Widget, Spec.TargetProperty, bIsPropertyDelegate);
	if (!DelegateProperty || !DelegateProperty->SignatureFunction)
	{
		OutError = FText::FromString(FString::Printf(
			TEXT("Binding '%s' target does not expose a bindable delegate"),
			*BindingTargetToString(Spec.WidgetName, Spec.TargetProperty)));
		return false;
	}

	FGuid MemberGuid;
	UFunction* Function = nullptr;
	TArray<FFieldVariant> FunctionBindingChain;
	if (Spec.SourcePathSegments.Num() > 0)
	{
		UClass* SourceClass = ResolveBindingSourceClass(Blueprint);
		if (!ResolveFunctionBindingChain(SourceClass, Spec.SourcePathSegments, FunctionBindingChain, Function, MemberGuid, OutError))
		{
			return false;
		}
	}
	else
	{
		Function = ResolveBindingFunction(Blueprint, Spec.FunctionName, MemberGuid);
		if (Function)
		{
			FunctionBindingChain.Add(FFieldVariant(Function));
		}
	}

	if (!Function)
	{
		const FString GraphOnlySuffix = MemberGuid.IsValid()
			? TEXT(" Graph-only functions must be compiled before strict binding validation can create them.")
			: TEXT("");
		OutError = FText::FromString(FString::Printf(
			TEXT("Binding '%s' function '%s' was not found in the generated, skeleton, or parent class hierarchy.%s"),
			*BindingTargetToString(Spec.WidgetName, Spec.TargetProperty),
			*Spec.FunctionName.ToString(),
			*GraphOnlySuffix));
		return false;
	}

	const auto IgnoredSignatureFlags = UFunction::GetDefaultIgnoredSignatureCompatibilityFlags() | CPF_ReturnParm;
	const bool bSignatureCompatible = Function->IsSignatureCompatibleWith(DelegateProperty->SignatureFunction, IgnoredSignatureFlags);
	const bool bHasPropertyPathBinder = HasFunctionBinder(Function, DelegateProperty->SignatureFunction);
	if (!bSignatureCompatible && !bHasPropertyPathBinder)
	{
		OutError = FText::FromString(FString::Printf(
			TEXT("Binding '%s' function '%s' signature is not compatible with delegate '%s'"),
			*BindingTargetToString(Spec.WidgetName, Spec.TargetProperty),
			*Spec.FunctionName.ToString(),
			*DelegateProperty->GetName()));
		return false;
	}

	if (bIsPropertyDelegate && !Function->HasAnyFunctionFlags(FUNC_Const | FUNC_BlueprintPure))
	{
		OutError = FText::FromString(FString::Printf(
			TEXT("Binding '%s' function '%s' must be const or BlueprintPure for property delegate binding"),
			*BindingTargetToString(Spec.WidgetName, Spec.TargetProperty),
			*Spec.FunctionName.ToString()));
		return false;
	}

	OutBinding.ObjectName = Spec.WidgetName;
	OutBinding.PropertyName = Spec.TargetProperty;
	OutBinding.FunctionName = Function->GetFName();
	OutBinding.SourcePath = FEditorPropertyPath(FunctionBindingChain);
	OutBinding.MemberGuid = MemberGuid;
	OutBinding.Kind = EBindingKind::Function;
	return true;
}

static bool BuildPropertyBinding(
	UWidgetBlueprint* Blueprint,
	UWidget* Widget,
	const FWidgetBindingSpec& Spec,
	FDelegateEditorBinding& OutBinding,
	FText& OutError)
{
	bool bIsPropertyDelegate = false;
	FDelegateProperty* DelegateProperty = ResolveBindingDelegateProperty(Widget, Spec.TargetProperty, bIsPropertyDelegate);
	if (!DelegateProperty || !DelegateProperty->SignatureFunction)
	{
		OutError = FText::FromString(FString::Printf(
			TEXT("Binding '%s' target does not expose a bindable delegate"),
			*BindingTargetToString(Spec.WidgetName, Spec.TargetProperty)));
		return false;
	}

	UClass* SourceClass = ResolveBindingSourceClass(Blueprint);
	TArray<FFieldVariant> BindingChain;
	FName SourceProperty;
	FGuid MemberGuid;
	if (!ResolvePropertyBindingChain(SourceClass, Spec.SourcePathSegments, BindingChain, SourceProperty, MemberGuid, OutError))
	{
		return false;
	}

	FEditorPropertyPath SourcePath(BindingChain);
	FText ValidationError;
	if (!SourcePath.Validate(DelegateProperty, ValidationError))
	{
		OutError = ValidationError;
		return false;
	}

	OutBinding.ObjectName = Spec.WidgetName;
	OutBinding.PropertyName = Spec.TargetProperty;
	OutBinding.SourceProperty = SourceProperty;
	OutBinding.SourcePath = SourcePath;
	OutBinding.MemberGuid = MemberGuid;
	OutBinding.Kind = EBindingKind::Property;
	return true;
}

static TArray<FString> ExtractSourcePathSegments(const FDelegateEditorBinding& Binding)
{
	TArray<FString> Segments;
	for (const auto& Segment : Binding.SourcePath.Segments)
	{
		const FName MemberName = Segment.GetMemberName();
		if (!MemberName.IsNone())
		{
			Segments.Add(MemberName.ToString());
		}
	}
	return Segments;
}

static TSharedPtr<FJsonObject> BindingToJsonObject(const FDelegateEditorBinding& Binding)
{
	TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();

	if (Binding.Kind == EBindingKind::Function)
	{
		FName FunctionName = Binding.FunctionName;
		if (FunctionName.IsNone())
		{
			const TArray<FString> SourcePathSegments = ExtractSourcePathSegments(Binding);
			if (SourcePathSegments.Num() > 0)
			{
				FunctionName = FName(*SourcePathSegments.Last());
			}
		}

		if (FunctionName.IsNone())
		{
			return nullptr;
		}

		BindingObj->SetStringField(TEXT("Function"), FunctionName.ToString());
		BindingObj->SetStringField(TEXT("Kind"), TEXT("Function"));
		const TArray<FString> SourcePathSegments = ExtractSourcePathSegments(Binding);
		if (SourcePathSegments.Num() > 1)
		{
			TArray<TSharedPtr<FJsonValue>> SourcePathJson;
			for (const FString& SourcePathSegment : SourcePathSegments)
			{
				SourcePathJson.Add(MakeShared<FJsonValueString>(SourcePathSegment));
			}
			BindingObj->SetArrayField(TEXT("SourcePath"), SourcePathJson);
		}
		return BindingObj;
	}

	BindingObj->SetStringField(TEXT("Kind"), TEXT("Property"));

	const TArray<FString> SourcePathSegments = ExtractSourcePathSegments(Binding);
	if (SourcePathSegments.Num() == 1)
	{
		BindingObj->SetStringField(TEXT("Property"), SourcePathSegments[0]);
		return BindingObj;
	}

	if (SourcePathSegments.Num() > 1)
	{
		TArray<TSharedPtr<FJsonValue>> SourcePathJson;
		for (const FString& SourcePathSegment : SourcePathSegments)
		{
			SourcePathJson.Add(MakeShared<FJsonValueString>(SourcePathSegment));
		}
		BindingObj->SetArrayField(TEXT("SourcePath"), SourcePathJson);
		return BindingObj;
	}

	if (!Binding.SourceProperty.IsNone())
	{
		BindingObj->SetStringField(TEXT("Property"), Binding.SourceProperty.ToString());
		return BindingObj;
	}

	return nullptr;
}

static TSharedPtr<FJsonObject> BindingToDiagnosticJsonObject(const FDelegateEditorBinding& Binding)
{
	TSharedPtr<FJsonObject> DiagnosticObj = MakeShared<FJsonObject>();
	DiagnosticObj->SetStringField(TEXT("Widget"), Binding.ObjectName);
	DiagnosticObj->SetStringField(TEXT("Property"), Binding.PropertyName.ToString());
	DiagnosticObj->SetStringField(TEXT("Kind"), Binding.Kind == EBindingKind::Function ? TEXT("Function") : TEXT("Property"));

	if (!Binding.FunctionName.IsNone())
	{
		DiagnosticObj->SetStringField(TEXT("Function"), Binding.FunctionName.ToString());
	}
	if (!Binding.SourceProperty.IsNone())
	{
		DiagnosticObj->SetStringField(TEXT("SourceProperty"), Binding.SourceProperty.ToString());
	}

	const TArray<FString> SourcePathSegments = ExtractSourcePathSegments(Binding);
	TArray<TSharedPtr<FJsonValue>> SourcePathJson;
	for (const FString& SourcePathSegment : SourcePathSegments)
	{
		SourcePathJson.Add(MakeShared<FJsonValueString>(SourcePathSegment));
	}
	DiagnosticObj->SetArrayField(TEXT("SourcePath"), SourcePathJson);
	DiagnosticObj->SetBoolField(TEXT("MemberGuidValid"), Binding.MemberGuid.IsValid());
	if (Binding.MemberGuid.IsValid())
	{
		DiagnosticObj->SetStringField(TEXT("MemberGuid"), Binding.MemberGuid.ToString(EGuidFormats::DigitsWithHyphens));
	}

	return DiagnosticObj;
}

static bool CompileBlueprintChecked(UWidgetBlueprint* Blueprint, FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Cannot compile a null WidgetBlueprint");
		return false;
	}

	FCompilerResultsLog CompileResults;
	CompileResults.SetSilentMode(true);
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompileResults);

	if (CompileResults.NumErrors == 0 && Blueprint->Status != BS_Error)
	{
		return true;
	}

	FString ErrorMessage;
	for (const TSharedRef<FTokenizedMessage>& Message : CompileResults.Messages)
	{
		if (Message->GetSeverity() == EMessageSeverity::Error)
		{
			if (!ErrorMessage.IsEmpty())
			{
				ErrorMessage += TEXT("; ");
			}
			ErrorMessage += Message->ToText().ToString();
		}
	}

	if (ErrorMessage.IsEmpty())
	{
		ErrorMessage = FString::Printf(TEXT("WidgetBlueprint compile failed with status %d and %d compiler error(s)"),
			static_cast<int32>(Blueprint->Status),
			CompileResults.NumErrors);
	}

	OutError = ErrorMessage;
	return false;
}

static UWidgetTree* DuplicateWidgetTree(UWidgetTree* SourceTree, UObject* Outer, const TCHAR* BaseName)
{
	if (!SourceTree || !Outer)
	{
		return nullptr;
	}

	const FName DuplicateName = MakeUniqueObjectName(Outer, UWidgetTree::StaticClass(), BaseName);
	return DuplicateObject<UWidgetTree>(SourceTree, Outer, DuplicateName);
}

static UObject* DuplicateClassDefaultObject(UClass* SourceClass, UObject* Outer, const TCHAR* BaseName)
{
	if (!SourceClass || !Outer)
	{
		return nullptr;
	}

	UObject* SourceCDO = SourceClass->GetDefaultObject(false);
	if (!SourceCDO)
	{
		return nullptr;
	}

	const FName DuplicateName = MakeUniqueObjectName(Outer, SourceCDO->GetClass(), BaseName);
	return DuplicateObject<UObject>(SourceCDO, Outer, DuplicateName);
}

static void CopyDefaultObjectValues(UObject* DestinationCDO, UObject* SourceCDO)
{
	if (!DestinationCDO || !SourceCDO)
	{
		return;
	}

	for (TFieldIterator<FProperty> SourceIt(SourceCDO->GetClass()); SourceIt; ++SourceIt)
	{
		FProperty* SourceProperty = *SourceIt;
		if (!SourceProperty || SourceProperty->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient))
		{
			continue;
		}

		FProperty* DestinationProperty = FindFProperty<FProperty>(DestinationCDO->GetClass(), SourceProperty->GetFName());
		if (!DestinationProperty || !DestinationProperty->SameType(SourceProperty))
		{
			continue;
		}

		void* SourceValue = SourceProperty->ContainerPtrToValuePtr<void>(SourceCDO);
		void* DestinationValue = DestinationProperty->ContainerPtrToValuePtr<void>(DestinationCDO);
		DestinationProperty->CopyCompleteValue(DestinationValue, SourceValue);
	}
}

static void MoveWidgetTreeToTransient(UWidgetTree* WidgetTree)
{
	if (WidgetTree)
	{
		WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}
}

static bool SaveWidgetBlueprintPackage(UWidgetBlueprint* Blueprint, FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Cannot save a null WidgetBlueprint");
		return false;
	}

	UPackage* Package = Blueprint->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("WidgetBlueprint has no package to save");
		return false;
	}

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Blueprint, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save WidgetBlueprint package '%s'"), *Package->GetName());
		return false;
	}

	return true;
}
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

	UWidgetBlueprint* Blueprint = nullptr;
	UWidgetBlueprint* ExistingBlueprint = nullptr;
	FString BindingError;
	TSet<FString> WidgetLevelBindingTargets;

	if (bExists)
	{
		// Load existing blueprint for update or overwrite
		ExistingBlueprint = Cast<UWidgetBlueprint>(LoadExistingAsset(Path, Name));
		if (!ExistingBlueprint)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing widget blueprint"));
		}

		const FName CandidateName = MakeUniqueObjectName(GetTransientPackage(), UWidgetBlueprint::StaticClass(), *FString::Printf(TEXT("%s_GenerateCandidate"), *Name));
		Blueprint = DuplicateObject<UWidgetBlueprint>(ExistingBlueprint, GetTransientPackage(), CandidateName);
		if (!Blueprint)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create transient widget blueprint candidate"));
		}

		// Update ParentClass only if explicitly specified in Config
		if (Config->HasField(TEXT("ParentClass")))
		{
			UClass* ParentClass = ResolveParentClass(GetStringField(Config, TEXT("ParentClass"), TEXT("UserWidget")));
			if (Blueprint->ParentClass != ParentClass)
			{
				UE_LOG(LogAssetFactory, Log, TEXT("Updating ParentClass for '%s' from '%s' to '%s'"),
					*Name,
					Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("null"),
					ParentClass ? *ParentClass->GetName() : TEXT("null"));
				Blueprint->ParentClass = ParentClass;
			}
		}

		// Three mutually exclusive widget tree operation paths:
		TSharedPtr<FJsonObject> RootWidgetConfig = GetObjectField(Config, TEXT("RootWidget"));
		const TArray<TSharedPtr<FJsonValue>>* WidgetUpdatesArray = GetArrayField(Config, TEXT("WidgetUpdates"));

		if (RootWidgetConfig.IsValid())
		{
			// Path A: RootWidget provided → clear and fully rebuild widget tree (existing behavior)
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
						const FName WidgetFName = Widget->GetFName();

						for (int32 i = Blueprint->Bindings.Num() - 1; i >= 0; --i)
						{
							if (Blueprint->Bindings[i].ObjectName == Widget->GetName())
							{
								Blueprint->Bindings.RemoveAt(i);
							}
						}

						if (UPanelWidget* WidgetParent = Widget->GetParent())
						{
							WidgetParent->Modify();
						}
						Widget->Modify();
						Blueprint->WidgetTree->RemoveWidget(Widget);

						if (Widget->bIsVariable)
						{
							FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, WidgetFName);
						}

						Widget->Rename(nullptr, GetTransientPackage());
						Blueprint->OnVariableRemoved(WidgetFName);
					}
				}
			}
			Blueprint->Bindings.Empty();

			// Build new widget tree
			UWidget* RootWidget = BuildWidgetTree(Blueprint, RootWidgetConfig, nullptr, TEXT("RootWidget"), &BindingError, &WidgetLevelBindingTargets);
			if (!RootWidget)
			{
				const FString Reason = BindingError.IsEmpty() ? TEXT("Failed to build widget tree") : BindingError;
				return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Reason);
			}
			Blueprint->WidgetTree->RootWidget = RootWidget;
		}
		else if (WidgetUpdatesArray)
		{
			// Path B: WidgetUpdates provided → element-level operations
			if (!ProcessWidgetUpdates(Blueprint, WidgetUpdatesArray, BindingError, &WidgetLevelBindingTargets))
			{
				const FString Reason = BindingError.IsEmpty() ? TEXT("Failed to process WidgetUpdates") : BindingError;
				return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Reason);
			}
		}
		// Path C: Neither provided → widget tree stays unchanged
	}
	else
	{
		// Create new widget blueprint
		UClass* ParentClass = ResolveParentClass(GetStringField(Config, TEXT("ParentClass"), TEXT("UserWidget")));

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

		// New blueprint requires RootWidget
		TSharedPtr<FJsonObject> RootWidgetConfig = GetObjectField(Config, TEXT("RootWidget"));
		if (!RootWidgetConfig.IsValid())
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Missing RootWidget in configuration"));
		}

		UWidget* RootWidget = BuildWidgetTree(Blueprint, RootWidgetConfig, nullptr, TEXT("RootWidget"), &BindingError, &WidgetLevelBindingTargets);
		if (!RootWidget)
		{
			const FString Reason = BindingError.IsEmpty() ? TEXT("Failed to build widget tree") : BindingError;
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Reason);
		}
		Blueprint->WidgetTree->RootWidget = RootWidget;
	}

	TSharedPtr<FJsonObject> TopLevelBindingsConfig = GetObjectField(Config, TEXT("Bindings"));
	if (TopLevelBindingsConfig.IsValid())
	{
		if (!ConfigureTopLevelBindings(Blueprint, TopLevelBindingsConfig, WidgetLevelBindingTargets, BindingError))
		{
			const FString Reason = BindingError.IsEmpty() ? TEXT("Failed to configure top-level WidgetBlueprint bindings") : BindingError;
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Reason);
		}
	}

	// Compile first to generate the class
	Blueprint->Modify();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FString CompileError;
	if (!CompileBlueprintChecked(Blueprint, CompileError))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, FString::Printf(TEXT("WidgetBlueprint compile failed: %s"), *CompileError));
	}

	// Apply class default properties if specified (after compilation so GeneratedClass exists)
	TSharedPtr<FJsonObject> ClassDefaultsConfig = GetObjectField(Config, TEXT("ClassDefaults"));
	if (ClassDefaultsConfig.IsValid() && ClassDefaultsConfig->Values.Num() > 0)
	{
		ApplyClassDefaults(Blueprint, ClassDefaultsConfig);
		// Mark dirty and recompile after setting CDO properties
		Blueprint->Modify();
		if (!CompileBlueprintChecked(Blueprint, CompileError))
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, FString::Printf(TEXT("WidgetBlueprint compile failed after applying ClassDefaults: %s"), *CompileError));
		}
	}

	if (bExists && ExistingBlueprint)
	{
		UWidgetTree* SnapshotTree = DuplicateWidgetTree(ExistingBlueprint->WidgetTree, GetTransientPackage(), TEXT("WidgetTreeRollbackSnapshot"));
		const TArray<FDelegateEditorBinding> SnapshotBindings = ExistingBlueprint->Bindings;
		UClass* SnapshotParentClass = ExistingBlueprint->ParentClass;
		UObject* SnapshotClassDefaultObject = DuplicateClassDefaultObject(ExistingBlueprint->GeneratedClass, GetTransientPackage(), TEXT("ClassDefaultRollbackSnapshot"));

		auto RestoreExistingBlueprint = [&](const FString& FailureReason)
		{
			MoveWidgetTreeToTransient(ExistingBlueprint->WidgetTree);
			ExistingBlueprint->WidgetTree = DuplicateWidgetTree(SnapshotTree, ExistingBlueprint, TEXT("WidgetTreeRestored"));
			ExistingBlueprint->Bindings = SnapshotBindings;
			ExistingBlueprint->ParentClass = SnapshotParentClass;
			ExistingBlueprint->Modify();
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ExistingBlueprint);

			FString RestoreCompileError;
			if (!CompileBlueprintChecked(ExistingBlueprint, RestoreCompileError))
			{
				return FString::Printf(TEXT("%s; failed to restore original WidgetBlueprint compile state: %s"),
					*FailureReason,
					*RestoreCompileError);
			}

			if (SnapshotClassDefaultObject && ExistingBlueprint->GeneratedClass)
			{
				CopyDefaultObjectValues(ExistingBlueprint->GeneratedClass->GetDefaultObject(), SnapshotClassDefaultObject);
			}

			return FailureReason;
		};

		ExistingBlueprint->Modify();
		MoveWidgetTreeToTransient(ExistingBlueprint->WidgetTree);
		ExistingBlueprint->WidgetTree = DuplicateWidgetTree(Blueprint->WidgetTree, ExistingBlueprint, TEXT("WidgetTree"));
		ExistingBlueprint->Bindings = Blueprint->Bindings;
		ExistingBlueprint->ParentClass = Blueprint->ParentClass;

		ExistingBlueprint->Modify();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ExistingBlueprint);
		if (!CompileBlueprintChecked(ExistingBlueprint, CompileError))
		{
			const FString FailureReason = FString::Printf(TEXT("WidgetBlueprint compile failed while committing update: %s"), *CompileError);
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, RestoreExistingBlueprint(FailureReason));
		}

		if (ClassDefaultsConfig.IsValid() && ClassDefaultsConfig->Values.Num() > 0)
		{
			ApplyClassDefaults(ExistingBlueprint, ClassDefaultsConfig);
			ExistingBlueprint->Modify();
			if (!CompileBlueprintChecked(ExistingBlueprint, CompileError))
			{
				const FString FailureReason = FString::Printf(TEXT("WidgetBlueprint compile failed after committing ClassDefaults: %s"), *CompileError);
				return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, RestoreExistingBlueprint(FailureReason));
			}
		}

		ExistingBlueprint->MarkPackageDirty();
		FString SaveError;
		if (!SaveWidgetBlueprintPackage(ExistingBlueprint, SaveError))
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, RestoreExistingBlueprint(SaveError));
		}

		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, ExistingBlueprint);
	}

	Blueprint->MarkPackageDirty();
	FString SaveError;
	if (!SaveWidgetBlueprintPackage(Blueprint, SaveError))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, SaveError);
	}

	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, Blueprint);
}

UWidget* FWidgetBlueprintGenerator::BuildWidgetTree(
	UWidgetBlueprint* Blueprint,
	TSharedPtr<FJsonObject> WidgetNode,
	UPanelWidget* Parent,
	const FString& JsonPath,
	FString* OutError,
	TSet<FString>* OutWidgetLevelBindingTargets)
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

		UWidget* WidgetToRemove = Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
		if (WidgetToRemove)
		{
			RemoveWidget(Blueprint, WidgetToRemove);
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
		FString LocalBindingError;
		if (!ConfigureBindings(Blueprint, Widget, WidgetName, BindingsConfig, LocalBindingError, OutWidgetLevelBindingTargets))
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("[%s] %s"), *JsonPath, *LocalBindingError);
			}
			return nullptr;
		}
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

					FString ChildError;
					UWidget* ChildWidget = BuildWidgetTree(Blueprint, ChildNode, nullptr, ChildPath, &ChildError, OutWidgetLevelBindingTargets);
					if (!ChildWidget)
					{
						if (!ChildError.IsEmpty())
						{
							if (OutError)
							{
								*OutError = ChildError;
							}
							return nullptr;
						}
					}

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

					FString ChildError;
					UWidget* ChildWidget = BuildWidgetTree(Blueprint, ChildNode, PanelWidget, ChildPath, &ChildError, OutWidgetLevelBindingTargets);
					if (!ChildWidget)
					{
						if (!ChildError.IsEmpty())
						{
							if (OutError)
							{
								*OutError = ChildError;
							}
							return nullptr;
						}
						continue;
					}
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

	// Additional slot-specific properties
	if (SlotConfig.IsValid())
	{
		// CanvasPanelSlot uses FAnchorData (LayoutData) which contains Anchors, Offsets, Alignment
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			// Handle Anchors
			if (SlotConfig->HasTypedField<EJson::Object>(TEXT("Anchors")))
			{
				TSharedPtr<FJsonObject> AnchorsConfig = SlotConfig->GetObjectField(TEXT("Anchors"));
				FAnchors Anchors = FPropertySetterUtils::ParseStruct<FAnchors>(AnchorsConfig);
				CanvasSlot->SetAnchors(Anchors);
			}

			// Handle Offsets
			if (SlotConfig->HasField(TEXT("Offsets")))
			{
				FMargin Offsets = FPropertySetterUtils::ParseStruct<FMargin>(SlotConfig->TryGetField(TEXT("Offsets")));
				CanvasSlot->SetOffsets(Offsets);
			}

			// Handle Alignment
			const TArray<TSharedPtr<FJsonValue>>* AlignmentArray = nullptr;
			if (SlotConfig->TryGetArrayField(TEXT("Alignment"), AlignmentArray) && AlignmentArray->Num() >= 2)
			{
				FVector2D Alignment = FPropertySetterUtils::ParseStruct<FVector2D>(*AlignmentArray);
				CanvasSlot->SetAlignment(Alignment);
			}

			// Handle Position (convenience: sets Offsets.Left and Offsets.Top)
			const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;
			if (SlotConfig->TryGetArrayField(TEXT("Position"), PositionArray) && PositionArray->Num() >= 2)
			{
				FVector2D Position = FPropertySetterUtils::ParseStruct<FVector2D>(*PositionArray);
				CanvasSlot->SetPosition(Position);
			}

			// Handle Size (convenience: sets Offsets.Right and Offsets.Bottom)
			const TArray<TSharedPtr<FJsonValue>>* SizeArray = nullptr;
			if (SlotConfig->TryGetArrayField(TEXT("Size"), SizeArray) && SizeArray->Num() >= 2)
			{
				FVector2D Size = FPropertySetterUtils::ParseStruct<FVector2D>(*SizeArray);
				CanvasSlot->SetSize(Size);
			}

			// Handle SizeToContent/AutoSize
			bool bAutoSize = false;
			if (SlotConfig->TryGetBoolField(TEXT("SizeToContent"), bAutoSize) ||
				SlotConfig->TryGetBoolField(TEXT("AutoSize"), bAutoSize))
			{
				CanvasSlot->SetAutoSize(bAutoSize);
			}

			// Handle ZOrder
			double ZOrder = 0;
			if (SlotConfig->TryGetNumberField(TEXT("ZOrder"), ZOrder))
			{
				CanvasSlot->SetZOrder(static_cast<int32>(ZOrder));
			}
		}
		else
		{
			// Non-canvas slots: use reflection for slot-specific properties
			// Handle SizeToContent/AutoSize (bool)
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
						void* SizePtr = StructProp->ContainerPtrToValuePtr<void>(Slot);
						UScriptStruct* SizeStruct = StructProp->Struct;

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
			FMargin Padding = FPropertySetterUtils::ParseStruct<FMargin>(SlotConfig->TryGetField(TEXT("Padding")));
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
		FLinearColor Color = FPropertySetterUtils::ParseStruct<FLinearColor>(StyleConfig->TryGetField(TEXT("Color")));

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
			FSlateBrush Brush = FPropertySetterUtils::ParseStruct<FSlateBrush>(BrushConfig);

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
	// Delegates to PropertySetterUtils which handles Size + reflection fallback for all other fields
	if (StyleConfig->HasTypedField<EJson::Object>(TEXT("Font")))
	{
		TSharedPtr<FJsonObject> FontConfig = StyleConfig->GetObjectField(TEXT("Font"));
		if (FontConfig.IsValid())
		{
			FProperty* FontProp = WidgetClass->FindPropertyByName(TEXT("Font"));
			if (FontProp)
			{
				TSharedRef<FJsonValueObject> FontJsonValue = MakeShared<FJsonValueObject>(FontConfig);
				FPropertySetterUtils::SetPropertyFromJson(Widget, FontProp, FontJsonValue);
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

UClass* FWidgetBlueprintGenerator::ResolveParentClass(const FString& ParentClassName) const
{
	if (ParentClassName == TEXT("UserWidget") || ParentClassName == TEXT("UUserWidget"))
	{
		return UUserWidget::StaticClass();
	}

	UClass* ParentClass = FClassFinderUtils::FindClassByName(ParentClassName, UUserWidget::StaticClass(), true);
	if (!ParentClass)
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Parent class '%s' not found, using UUserWidget"), *ParentClassName);
		ParentClass = UUserWidget::StaticClass();
	}
	return ParentClass;
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

bool FWidgetBlueprintGenerator::ConfigureBindings(
	UWidgetBlueprint* Blueprint,
	UWidget* Widget,
	const FString& WidgetName,
	TSharedPtr<FJsonObject> BindingsConfig,
	FString& OutError,
	TSet<FString>* OutWidgetLevelBindingTargets)
{
	if (!Blueprint || !Widget || !BindingsConfig.IsValid())
	{
		OutError = TEXT("Invalid binding configuration context");
		return false;
	}

	// Use the actual widget name from the object, not the passed-in name
	// This ensures the binding references the correct widget in the tree
	const FString ActualWidgetName = Widget->GetName();

	for (const auto& Pair : BindingsConfig->Values)
	{
		const FString& PropertyName = Pair.Key;
		const TSharedPtr<FJsonValue>& BindingValue = Pair.Value;

		FWidgetBindingSpec Spec;
		FString ParseError;
		if (!ParseBindingSpec(ActualWidgetName, PropertyName, BindingValue, Spec, ParseError))
		{
			OutError = ParseError;
			return false;
		}

		FDelegateEditorBinding NewBinding;
		FText BindingValidationError;
		if (Spec.Kind == EBindingKind::Function)
		{
			if (!BuildFunctionBinding(Blueprint, Widget, Spec, NewBinding, BindingValidationError))
			{
				OutError = BindingValidationError.ToString();
				return false;
			}
		}
		else
		{
			if (!BuildPropertyBinding(Blueprint, Widget, Spec, NewBinding, BindingValidationError))
			{
				OutError = BindingValidationError.ToString();
				return false;
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
					*Spec.WidgetName, *NewBinding.PropertyName.ToString(),
					NewBinding.Kind == EBindingKind::Function ? *NewBinding.FunctionName.ToString() : *NewBinding.SourceProperty.ToString());
				break;
			}
		}

		if (!bFound)
		{
			Blueprint->Bindings.Add(NewBinding);
			UE_LOG(LogAssetFactory, Log, TEXT("Added binding: %s.%s -> %s"),
				*Spec.WidgetName, *NewBinding.PropertyName.ToString(),
				NewBinding.Kind == EBindingKind::Function ? *NewBinding.FunctionName.ToString() : *NewBinding.SourceProperty.ToString());
		}

		if (OutWidgetLevelBindingTargets)
		{
			OutWidgetLevelBindingTargets->Add(BindingTargetKey(NewBinding.ObjectName, NewBinding.PropertyName));
		}
	}

	return true;
}

bool FWidgetBlueprintGenerator::ConfigureTopLevelBindings(
	UWidgetBlueprint* Blueprint,
	TSharedPtr<FJsonObject> TopLevelBindingsConfig,
	const TSet<FString>& WidgetLevelBindingTargets,
	FString& OutError)
{
	if (!Blueprint || !Blueprint->WidgetTree || !TopLevelBindingsConfig.IsValid())
	{
		OutError = TEXT("Invalid top-level binding configuration context");
		return false;
	}

	for (const auto& Pair : TopLevelBindingsConfig->Values)
	{
		const FString& BindingTarget = Pair.Key;
		FString WidgetName;
		FString PropertyName;
		if (!ParseTopLevelBindingKey(BindingTarget, WidgetName, PropertyName))
		{
			OutError = FString::Printf(TEXT("Top-level binding key '%s' must use Widget.Property format"), *BindingTarget);
			return false;
		}

		UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
		if (!Widget)
		{
			OutError = FString::Printf(TEXT("Top-level binding target widget '%s' was not found"), *WidgetName);
			return false;
		}

		const FString CanonicalTarget = BindingTargetKey(WidgetName, FName(*PropertyName));
		if (WidgetLevelBindingTargets.Contains(CanonicalTarget))
		{
			UE_LOG(LogAssetFactory, Warning,
				TEXT("Skipping top-level compatibility binding '%s' because a widget-level binding already defines the same target"),
				*BindingTarget);
			continue;
		}

		if (!Widget->bIsVariable)
		{
			ExposeAsVariable(Widget, WidgetName);
		}

		TSharedPtr<FJsonObject> WidgetBindings = MakeShared<FJsonObject>();
		WidgetBindings->SetField(PropertyName, Pair.Value);

		FString LocalBindingError;
		if (!ConfigureBindings(Blueprint, Widget, WidgetName, WidgetBindings, LocalBindingError, nullptr))
		{
			OutError = FString::Printf(TEXT("[%s] %s"), *BindingTarget, *LocalBindingError);
			return false;
		}
	}

	return true;
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

		if (FPropertySetterUtils::SetPropertyFromJson(CDO, Property, JsonValue))
		{
			UE_LOG(LogAssetFactory, Log, TEXT("Set ClassDefault: %s"), *PropertyName);
		}
		else
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Failed to set ClassDefault: %s"), *PropertyName);
		}
	}
}

void FWidgetBlueprintGenerator::RemoveWidget(UWidgetBlueprint* Blueprint, UWidget* Widget)
{
	if (!Blueprint || !Widget) return;

	Blueprint->WidgetTree->Modify();
	Blueprint->Modify();

	// Recursive lambda to delete widget and all its children
	TFunction<void(UWidget*)> DeleteWidgetRecursive = [&DeleteWidgetRecursive, &Blueprint](UWidget* W)
	{
		if (!W) return;

		const FName WidgetFName = W->GetFName();

		// Remove associated bindings
		for (int32 i = Blueprint->Bindings.Num() - 1; i >= 0; --i)
		{
			if (Blueprint->Bindings[i].ObjectName == W->GetName())
			{
				Blueprint->Bindings.RemoveAt(i);
			}
		}

		// Process children first if it's a panel
		if (UPanelWidget* Panel = Cast<UPanelWidget>(W))
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
		if (UPanelWidget* WidgetParent = W->GetParent())
		{
			WidgetParent->Modify();
		}
		W->Modify();

		// Remove from WidgetTree
		Blueprint->WidgetTree->RemoveWidget(W);

		// Remove variable nodes if it was a variable
		if (W->bIsVariable)
		{
			FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, WidgetFName);
		}

		// Rename to transient package
		W->Rename(nullptr, GetTransientPackage());

		// Notify Blueprint that variable was removed
		Blueprint->OnVariableRemoved(WidgetFName);
	};

	DeleteWidgetRecursive(Widget);
}

bool FWidgetBlueprintGenerator::ProcessWidgetUpdates(
	UWidgetBlueprint* Blueprint,
	const TArray<TSharedPtr<FJsonValue>>* UpdatesArray,
	FString& OutError,
	TSet<FString>* OutWidgetLevelBindingTargets)
{
	if (!Blueprint || !UpdatesArray)
	{
		OutError = TEXT("Invalid WidgetUpdates context");
		return false;
	}

	for (int32 i = 0; i < UpdatesArray->Num(); ++i)
	{
		TSharedPtr<FJsonObject> UpdateObj = (*UpdatesArray)[i]->AsObject();
		if (!UpdateObj.IsValid())
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("WidgetUpdates[%d]: not a valid JSON object"), i);
			continue;
		}

		FString WidgetName = GetStringField(UpdateObj, TEXT("Name"));
		FString UpdateAction = GetStringField(UpdateObj, TEXT("Action"), TEXT("Update"));
		FString JsonPath = FString::Printf(TEXT("WidgetUpdates[%d]"), i);

		if (WidgetName.IsEmpty() && !UpdateAction.Equals(TEXT("Add"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Missing 'Name' field"), *JsonPath);
			continue;
		}

		if (UpdateAction.Equals(TEXT("Remove"), ESearchCase::IgnoreCase))
		{
			UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
			if (Widget)
			{
				RemoveWidget(Blueprint, Widget);
				UE_LOG(LogAssetFactory, Log, TEXT("[%s] Removed widget '%s'"), *JsonPath, *WidgetName);
			}
			else
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Widget '%s' not found for removal"), *JsonPath, *WidgetName);
			}
			continue;
		}

		if (UpdateAction.Equals(TEXT("Update"), ESearchCase::IgnoreCase))
		{
			UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
			if (!Widget)
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Widget '%s' not found for update"), *JsonPath, *WidgetName);
				continue;
			}

			// Apply Style
			TSharedPtr<FJsonObject> StyleConfig = GetObjectField(UpdateObj, TEXT("Style"));
			if (StyleConfig.IsValid())
			{
				ApplyStyle(Widget, StyleConfig);
			}

			// Apply Properties
			TSharedPtr<FJsonObject> Props = GetObjectField(UpdateObj, TEXT("Properties"));
			if (Props.IsValid())
			{
				SetPropertiesViaReflection(Widget, Props);
			}

			// Apply Slot
			TSharedPtr<FJsonObject> SlotConfig = GetObjectField(UpdateObj, TEXT("Slot"));
			if (SlotConfig.IsValid() && Widget->GetParent())
			{
				ConfigureSlot(Widget, Widget->GetParent(), SlotConfig);
			}

			// Handle IsVariable
			bool bIsVariable = false;
			if (UpdateObj->TryGetBoolField(TEXT("IsVariable"), bIsVariable) && bIsVariable)
			{
				ExposeAsVariable(Widget, WidgetName);
			}

			// Handle Bindings
			TSharedPtr<FJsonObject> Bindings = GetObjectField(UpdateObj, TEXT("Bindings"));
			if (Bindings.IsValid())
			{
				if (!Widget->bIsVariable)
				{
					ExposeAsVariable(Widget, WidgetName);
				}
				FString LocalBindingError;
				if (!ConfigureBindings(Blueprint, Widget, WidgetName, Bindings, LocalBindingError, OutWidgetLevelBindingTargets))
				{
					OutError = FString::Printf(TEXT("[%s] %s"), *JsonPath, *LocalBindingError);
					return false;
				}
			}

			UE_LOG(LogAssetFactory, Log, TEXT("[%s] Updated widget '%s'"), *JsonPath, *WidgetName);
			continue;
		}

		if (UpdateAction.Equals(TEXT("Add"), ESearchCase::IgnoreCase))
		{
			FString ParentName = GetStringField(UpdateObj, TEXT("Parent"));
			if (ParentName.IsEmpty())
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Missing 'Parent' field for Add action"), *JsonPath);
				continue;
			}

			UWidget* ParentWidget = Blueprint->WidgetTree->FindWidget(FName(*ParentName));
			UPanelWidget* Panel = Cast<UPanelWidget>(ParentWidget);
			if (!Panel)
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Parent widget '%s' not found or not a panel"), *JsonPath, *ParentName);
				continue;
			}

			// Get the Widget config (nested under "Widget" key)
			TSharedPtr<FJsonObject> WidgetConfig = GetObjectField(UpdateObj, TEXT("Widget"));
			if (!WidgetConfig.IsValid())
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Missing 'Widget' field for Add action"), *JsonPath);
				continue;
			}
			FString AddError;
			if (!BuildWidgetTree(Blueprint, WidgetConfig, Panel, JsonPath, &AddError, OutWidgetLevelBindingTargets))
			{
				if (!AddError.IsEmpty())
				{
					OutError = AddError;
					return false;
				}
				continue;
			}
			UE_LOG(LogAssetFactory, Log, TEXT("[%s] Added widget to parent '%s'"), *JsonPath, *ParentName);
			continue;
		}

		if (UpdateAction.Equals(TEXT("Move"), ESearchCase::IgnoreCase))
		{
			UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
			if (!Widget)
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Widget '%s' not found for Move"), *JsonPath, *WidgetName);
				continue;
			}

			FString NewParentName = GetStringField(UpdateObj, TEXT("NewParent"));
			if (NewParentName.IsEmpty())
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Missing 'NewParent' field for Move action"), *JsonPath);
				continue;
			}

			UWidget* NewParentWidget = Blueprint->WidgetTree->FindWidget(FName(*NewParentName));
			UPanelWidget* NewPanel = Cast<UPanelWidget>(NewParentWidget);
			if (!NewPanel)
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("[%s] NewParent '%s' not found or not a panel"), *JsonPath, *NewParentName);
				continue;
			}

			// Detach from current parent (widget stays in WidgetTree, all properties preserved)
			UPanelWidget* OldParent = Widget->GetParent();
			if (OldParent)
			{
				OldParent->RemoveChild(Widget);
			}

			// Attach to new parent
			NewPanel->AddChild(Widget);

			// Apply new slot config if provided
			TSharedPtr<FJsonObject> NewSlotConfig = GetObjectField(UpdateObj, TEXT("NewSlot"));
			if (NewSlotConfig.IsValid())
			{
				ConfigureSlot(Widget, NewPanel, NewSlotConfig);
			}

			UE_LOG(LogAssetFactory, Log, TEXT("[%s] Moved widget '%s' from '%s' to '%s'"),
				*JsonPath, *WidgetName,
				OldParent ? *OldParent->GetName() : TEXT("(none)"),
				*NewParentName);
			continue;
		}

		UE_LOG(LogAssetFactory, Warning, TEXT("[%s] Unknown action '%s'"), *JsonPath, *UpdateAction);
	}

	return true;
}

TOptional<FString> FWidgetBlueprintGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	if (Action == EGenerationAction::Update)
	{
		// Update: RootWidget and WidgetUpdates are both optional
		// If RootWidget is provided, require explicit RebuildTree confirmation to prevent accidental full tree destruction
		if (Config->HasTypedField<EJson::Object>(TEXT("RootWidget")))
		{
			bool bRebuildTree = false;
			Config->TryGetBoolField(TEXT("RebuildTree"), bRebuildTree);
			if (!bRebuildTree)
			{
				return FString(TEXT(
					"Action 'Update' with 'RootWidget' will DESTROY the entire existing widget tree and rebuild from scratch. "
					"Use 'WidgetUpdates' array for safe incremental changes (add/update/remove individual widgets), "
					"or set '\"RebuildTree\": true' to confirm full rebuild."
				));
			}

			TSharedPtr<FJsonObject> RootWidget = Config->GetObjectField(TEXT("RootWidget"));
			if (!RootWidget->HasField(TEXT("Type")))
			{
				return FString(TEXT("RootWidget must have a 'Type' field"));
			}
		}
		return TOptional<FString>();
	}

	// Create: RootWidget is required
	if (!Config->HasTypedField<EJson::Object>(TEXT("RootWidget")))
	{
		return FString(TEXT("'RootWidget' must be a JSON object"));
	}

	TSharedPtr<FJsonObject> RootWidget = Config->GetObjectField(TEXT("RootWidget"));

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

	TMap<FString, TSharedPtr<FJsonObject>> BindingsByWidget;
	TArray<TSharedPtr<FJsonValue>> BindingDiagnostics;
	TArray<TSharedPtr<FJsonValue>> ExtractWarnings;

	auto AddExtractWarning = [&ExtractWarnings](const FString& Warning)
	{
		ExtractWarnings.Add(MakeShared<FJsonValueString>(Warning));
	};

	for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
	{
		if (Binding.ObjectName.IsEmpty() || Binding.PropertyName.IsNone())
		{
			const FString Warning = FString::Printf(TEXT("Skipping malformed binding during extract: ObjectName='%s', PropertyName='%s'"),
				*Binding.ObjectName,
				*Binding.PropertyName.ToString());
			UE_LOG(LogAssetFactory, Warning, TEXT("%s"), *Warning);
			AddExtractWarning(Warning);
			continue;
		}

		BindingDiagnostics.Add(MakeShared<FJsonValueObject>(BindingToDiagnosticJsonObject(Binding)));

		if (!Blueprint->WidgetTree || !Blueprint->WidgetTree->FindWidget(FName(*Binding.ObjectName)))
		{
			const FString Warning = FString::Printf(TEXT("Skipping unresolved binding during extract: target widget '%s' for property '%s' was not found"),
				*Binding.ObjectName,
				*Binding.PropertyName.ToString());
			UE_LOG(LogAssetFactory, Warning, TEXT("%s"), *Warning);
			AddExtractWarning(Warning);
			continue;
		}

		TSharedPtr<FJsonObject> BindingObj = BindingToJsonObject(Binding);
		if (!BindingObj.IsValid())
		{
			const FString Warning = FString::Printf(TEXT("Skipping unsupported binding during extract: %s.%s"),
				*Binding.ObjectName,
				*Binding.PropertyName.ToString());
			UE_LOG(LogAssetFactory, Warning, TEXT("%s"), *Warning);
			AddExtractWarning(Warning);
			continue;
		}

		TSharedPtr<FJsonObject>* ExistingWidgetBindings = BindingsByWidget.Find(Binding.ObjectName);
		if (!ExistingWidgetBindings)
		{
			BindingsByWidget.Add(Binding.ObjectName, MakeShared<FJsonObject>());
			ExistingWidgetBindings = BindingsByWidget.Find(Binding.ObjectName);
		}

		if (ExistingWidgetBindings && ExistingWidgetBindings->IsValid())
		{
			(*ExistingWidgetBindings)->SetObjectField(Binding.PropertyName.ToString(), BindingObj);
		}
	}

	// RootWidget
	if (Blueprint->WidgetTree && Blueprint->WidgetTree->RootWidget)
	{
		TSharedPtr<FJsonObject> RootWidgetJson = ExtractWidgetTree(Blueprint->WidgetTree->RootWidget, &BindingsByWidget);
		if (RootWidgetJson.IsValid())
		{
			Config->SetObjectField(TEXT("RootWidget"), RootWidgetJson);
		}
	}

	if (BindingDiagnostics.Num() > 0)
	{
		Config->SetArrayField(TEXT("BindingDiagnostics"), BindingDiagnostics);
	}
	if (ExtractWarnings.Num() > 0)
	{
		Config->SetArrayField(TEXT("Warnings"), ExtractWarnings);
	}

	return Config;
}

TSharedPtr<FJsonObject> FWidgetBlueprintGenerator::ExtractWidgetTree(UWidget* Widget, const TMap<FString, TSharedPtr<FJsonObject>>* BindingsByWidget) const
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

	if (BindingsByWidget)
	{
		if (const TSharedPtr<FJsonObject>* WidgetBindings = BindingsByWidget->Find(Widget->GetName()))
		{
			if (WidgetBindings->IsValid() && (*WidgetBindings)->Values.Num() > 0)
			{
				WidgetJson->SetObjectField(TEXT("Bindings"), *WidgetBindings);
				WidgetJson->SetBoolField(TEXT("IsVariable"), true);
			}
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
				TSharedPtr<FJsonObject> ChildJson = ExtractWidgetTree(ChildWidget, BindingsByWidget);
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

	// CanvasPanelSlot: extract from LayoutData (FAnchorData)
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		FAnchorData Layout = CanvasSlot->GetLayout();

		// Anchors
		TSharedPtr<FJsonObject> AnchorsJson = MakeShared<FJsonObject>();
		AnchorsJson->SetArrayField(TEXT("Min"), {
			MakeShared<FJsonValueNumber>(Layout.Anchors.Minimum.X),
			MakeShared<FJsonValueNumber>(Layout.Anchors.Minimum.Y)
		});
		AnchorsJson->SetArrayField(TEXT("Max"), {
			MakeShared<FJsonValueNumber>(Layout.Anchors.Maximum.X),
			MakeShared<FJsonValueNumber>(Layout.Anchors.Maximum.Y)
		});
		SlotJson->SetObjectField(TEXT("Anchors"), AnchorsJson);

		// Offsets
		SlotJson->SetField(TEXT("Offsets"), MarginToJson(Layout.Offsets));

		// Alignment (only if non-zero)
		if (!Layout.Alignment.IsZero())
		{
			SlotJson->SetField(TEXT("Alignment"), Vector2DToJson(Layout.Alignment));
		}

		// AutoSize
		if (CanvasSlot->GetAutoSize())
		{
			SlotJson->SetBoolField(TEXT("SizeToContent"), true);
		}

		// ZOrder
		int32 ZOrder = CanvasSlot->GetZOrder();
		if (ZOrder != 0)
		{
			SlotJson->SetNumberField(TEXT("ZOrder"), ZOrder);
		}
	}
	else
	{
		// Non-canvas slots: use reflection

		// Padding (for most slot types)
		if (FStructProperty* PaddingProp = CastField<FStructProperty>(SlotClass->FindPropertyByName(TEXT("Padding"))))
		{
			void* ValuePtr = PaddingProp->ContainerPtrToValuePtr<void>(Slot);
			FMargin* Padding = static_cast<FMargin*>(ValuePtr);
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
