// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/WidgetBlueprintBindingAdapter.h"

#include "Blueprint/WidgetTree.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Field.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
struct FParsedWidgetBinding
{
	FName Widget;
	FName Property;
	EBindingKind Kind = EBindingKind::Function;
	FName Function;
	TArray<FName> SourcePath;
	FGuid MemberGuid;
	FString JsonPath;
};

FAssetDocumentCapabilityResult BindingFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FString EscapePathToken(const FString& Token)
{
	return Token.Replace(TEXT("~"), TEXT("~0")).Replace(TEXT("/"), TEXT("~1"));
}

FString BindingIdentityKey(const FName& Widget, const FName& Property)
{
	return FString::Printf(TEXT("%s.%s"), *Widget.ToString(), *Property.ToString()).ToLower();
}

FString BindingIdentityKey(const FDelegateEditorBinding& Binding)
{
	return BindingIdentityKey(FName(*Binding.ObjectName), Binding.PropertyName);
}

bool ParseBindingKind(const FString& KindString, EBindingKind& OutKind)
{
	if (KindString.Equals(TEXT("Function"), ESearchCase::IgnoreCase))
	{
		OutKind = EBindingKind::Function;
		return true;
	}
	if (KindString.Equals(TEXT("Property"), ESearchCase::IgnoreCase))
	{
		OutKind = EBindingKind::Property;
		return true;
	}
	return false;
}

FAssetDocumentCapabilityResult RequireStringField(
	const TSharedPtr<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	const FString& Code,
	FString& OutValue)
{
	if (!Object.IsValid() || !Object->TryGetStringField(FieldName, OutValue) || OutValue.IsEmpty())
	{
		return BindingFailure(
			FString::Printf(TEXT("Body.Bindings entry requires non-empty %s"), *FieldName),
			Path,
			Code);
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseSourcePath(
	const TSharedPtr<FJsonObject>& BindingObject,
	const FString& BindingPath,
	TArray<FName>& OutPath)
{
	OutPath.Reset();

	const TArray<TSharedPtr<FJsonValue>>* SourcePathArray = nullptr;
	if (BindingObject->TryGetArrayField(TEXT("SourcePath"), SourcePathArray) && SourcePathArray)
	{
		if (SourcePathArray->Num() == 0)
		{
			return BindingFailure(TEXT("Body.Bindings SourcePath must contain at least one segment"), BindingPath / TEXT("SourcePath"), TEXT("InvalidBindingSourcePath"));
		}

		for (int32 SegmentIndex = 0; SegmentIndex < SourcePathArray->Num(); ++SegmentIndex)
		{
			FString Segment;
			if (!(*SourcePathArray)[SegmentIndex].IsValid()
				|| !(*SourcePathArray)[SegmentIndex]->TryGetString(Segment)
				|| Segment.IsEmpty())
			{
				return BindingFailure(
					TEXT("Body.Bindings SourcePath segments must be non-empty strings"),
					FString::Printf(TEXT("%s/SourcePath/%d"), *BindingPath, SegmentIndex),
					TEXT("InvalidBindingSourcePath"));
			}
			OutPath.Add(FName(*Segment));
		}
		return FAssetDocumentCapabilityResult::Success();
	}

	FString SourceProperty;
	if (BindingObject->TryGetStringField(TEXT("SourceProperty"), SourceProperty)
		|| BindingObject->TryGetStringField(TEXT("PropertyPath"), SourceProperty))
	{
		if (SourceProperty.IsEmpty())
		{
			return BindingFailure(TEXT("Body.Bindings source property must be non-empty"), BindingPath, TEXT("InvalidBindingSourcePath"));
		}
		OutPath.Add(FName(*SourceProperty));
		return FAssetDocumentCapabilityResult::Success();
	}

	return BindingFailure(TEXT("Property bindings require SourcePath"), BindingPath, TEXT("MissingBindingSourcePath"));
}

FAssetDocumentCapabilityResult ParseBindingSpecs(
	const TSharedPtr<FJsonValue>& BindingsJson,
	TArray<FParsedWidgetBinding>& OutSpecs)
{
	OutSpecs.Reset();
	if (!BindingsJson.IsValid() || BindingsJson->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (BindingsJson->Type != EJson::Array)
	{
		return BindingFailure(TEXT("Body.Bindings must be an array"), TEXT("/Body/Bindings"), TEXT("InvalidBodySectionType"));
	}

	const TArray<TSharedPtr<FJsonValue>>& Bindings = BindingsJson->AsArray();
	TSet<FString> SeenIdentities;
	for (int32 BindingIndex = 0; BindingIndex < Bindings.Num(); ++BindingIndex)
	{
		const FString BindingPath = FString::Printf(TEXT("/Body/Bindings/%d"), BindingIndex);
		if (!Bindings[BindingIndex].IsValid() || Bindings[BindingIndex]->Type != EJson::Object)
		{
			return BindingFailure(TEXT("Body.Bindings entries must be objects"), BindingPath, TEXT("InvalidBindingEntry"));
		}

		const TSharedPtr<FJsonObject> BindingObject = Bindings[BindingIndex]->AsObject();
		FParsedWidgetBinding Spec;
		Spec.JsonPath = BindingPath;
		FString WidgetName;
		FString PropertyName;
		FString KindString;
		FAssetDocumentCapabilityResult Result = RequireStringField(BindingObject, TEXT("Widget"), BindingPath / TEXT("Widget"), TEXT("MissingBindingWidgetName"), WidgetName);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = RequireStringField(BindingObject, TEXT("Property"), BindingPath / TEXT("Property"), TEXT("MissingBindingProperty"), PropertyName);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = RequireStringField(BindingObject, TEXT("Kind"), BindingPath / TEXT("Kind"), TEXT("MissingBindingKind"), KindString);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (!ParseBindingKind(KindString, Spec.Kind))
		{
			return BindingFailure(
				FString::Printf(TEXT("Body.Bindings Kind '%s' is not supported"), *KindString),
				BindingPath / TEXT("Kind"),
				TEXT("InvalidBindingKind"));
		}

		Spec.Widget = FName(*WidgetName);
		Spec.Property = FName(*PropertyName);
		const FString IdentityKey = BindingIdentityKey(Spec.Widget, Spec.Property);
		if (SeenIdentities.Contains(IdentityKey))
		{
			return BindingFailure(
				FString::Printf(TEXT("Duplicate binding target '%s.%s'"), *WidgetName, *PropertyName),
				BindingPath,
				TEXT("DuplicateBindingTarget"));
		}
		SeenIdentities.Add(IdentityKey);

		FString MemberGuidString;
		if (BindingObject->TryGetStringField(TEXT("MemberGuid"), MemberGuidString) && !MemberGuidString.IsEmpty())
		{
			if (!FGuid::Parse(MemberGuidString, Spec.MemberGuid) || !Spec.MemberGuid.IsValid())
			{
				return BindingFailure(TEXT("Body.Bindings MemberGuid must be a valid GUID string"), BindingPath / TEXT("MemberGuid"), TEXT("InvalidBindingMemberGuid"));
			}
		}

		if (Spec.Kind == EBindingKind::Function)
		{
			FString FunctionName;
			Result = RequireStringField(BindingObject, TEXT("Function"), BindingPath / TEXT("Function"), TEXT("MissingBindingFunction"), FunctionName);
			if (!Result.bSuccess)
			{
				return Result;
			}
			Spec.Function = FName(*FunctionName);
		}
		else
		{
			Result = ParseSourcePath(BindingObject, BindingPath, Spec.SourcePath);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}

		OutSpecs.Add(MoveTemp(Spec));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FDelegateProperty* ResolveBindingDelegateProperty(UWidget* Widget, FName TargetProperty)
{
	if (!Widget)
	{
		return nullptr;
	}

	FDelegateProperty* BindableProperty = FindFProperty<FDelegateProperty>(
		Widget->GetClass(),
		FName(*(TargetProperty.ToString() + TEXT("Delegate"))));
	return BindableProperty ? BindableProperty : FindFProperty<FDelegateProperty>(Widget->GetClass(), TargetProperty);
}

bool RequiresPureBindingFunction(UWidget* Widget, FName TargetProperty)
{
	return Widget
		&& FindFProperty<FDelegateProperty>(Widget->GetClass(), FName(*(TargetProperty.ToString() + TEXT("Delegate")))) != nullptr;
}

UFunction* ResolveFunction(UWidgetBlueprint* WidgetBlueprint, FName FunctionName)
{
	if (!WidgetBlueprint || FunctionName.IsNone())
	{
		return nullptr;
	}
	if (WidgetBlueprint->GeneratedClass)
	{
		if (UFunction* Function = WidgetBlueprint->GeneratedClass->FindFunctionByName(FunctionName, EIncludeSuperFlag::IncludeSuper))
		{
			return Function;
		}
	}
	if (WidgetBlueprint->SkeletonGeneratedClass)
	{
		if (UFunction* Function = WidgetBlueprint->SkeletonGeneratedClass->FindFunctionByName(FunctionName, EIncludeSuperFlag::IncludeSuper))
		{
			return Function;
		}
	}
	if (WidgetBlueprint->ParentClass)
	{
		return WidgetBlueprint->ParentClass->FindFunctionByName(FunctionName, EIncludeSuperFlag::IncludeSuper);
	}
	return nullptr;
}

UStruct* ResolveNextContainer(FProperty* Property)
{
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		return StructProperty->Struct;
	}
	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		return ObjectProperty->PropertyClass;
	}
	return nullptr;
}

FAssetDocumentCapabilityResult ResolvePropertyPath(
	UWidgetBlueprint* WidgetBlueprint,
	const FParsedWidgetBinding& Spec,
	TArray<FFieldVariant>& OutChain)
{
	OutChain.Reset();
	UStruct* CurrentStruct = WidgetBlueprint ? WidgetBlueprint->GeneratedClass : nullptr;
	if (!CurrentStruct)
	{
		return BindingFailure(TEXT("WidgetBlueprint generated class is required for property binding"), TEXT("/Body/Bindings"), TEXT("MissingGeneratedClass"));
	}

	for (int32 SegmentIndex = 0; SegmentIndex < Spec.SourcePath.Num(); ++SegmentIndex)
	{
		FProperty* Property = FindFProperty<FProperty>(CurrentStruct, Spec.SourcePath[SegmentIndex]);
		if (!Property)
		{
			return BindingFailure(
				FString::Printf(TEXT("Binding source path segment '%s' does not exist"), *Spec.SourcePath[SegmentIndex].ToString()),
				FString::Printf(TEXT("%s/SourcePath/%d"), *Spec.JsonPath, SegmentIndex),
				TEXT("InvalidBindingSourcePath"));
		}
		OutChain.Add(FFieldVariant(Property));
		if (SegmentIndex < Spec.SourcePath.Num() - 1)
		{
			CurrentStruct = ResolveNextContainer(Property);
			if (!CurrentStruct)
			{
				return BindingFailure(
					FString::Printf(TEXT("Binding source path segment '%s' cannot contain child segments"), *Spec.SourcePath[SegmentIndex].ToString()),
					FString::Printf(TEXT("%s/SourcePath/%d"), *Spec.JsonPath, SegmentIndex),
					TEXT("InvalidBindingSourcePath"));
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult BuildEditorBinding(
	UWidgetBlueprint* WidgetBlueprint,
	const FParsedWidgetBinding& Spec,
	FDelegateEditorBinding& OutBinding)
{
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return BindingFailure(TEXT("WidgetBlueprint WidgetTree is required for bindings"), Spec.JsonPath, TEXT("MissingWidgetTree"));
	}

	UWidget* TargetWidget = WidgetBlueprint->WidgetTree->FindWidget(Spec.Widget);
	if (!TargetWidget)
	{
		return BindingFailure(
			FString::Printf(TEXT("Binding target widget '%s' does not exist"), *Spec.Widget.ToString()),
			Spec.JsonPath / TEXT("Widget"),
			TEXT("MissingBindingWidget"));
	}

	FDelegateProperty* DelegateProperty = ResolveBindingDelegateProperty(TargetWidget, Spec.Property);
	if (!DelegateProperty)
	{
		return BindingFailure(
			FString::Printf(TEXT("Binding target property '%s.%s' has no supported delegate"), *Spec.Widget.ToString(), *Spec.Property.ToString()),
			Spec.JsonPath / TEXT("Property"),
			TEXT("MissingBindingDelegate"));
	}

	OutBinding = FDelegateEditorBinding();
	OutBinding.ObjectName = Spec.Widget.ToString();
	OutBinding.PropertyName = Spec.Property;
	OutBinding.Kind = Spec.Kind;
	OutBinding.MemberGuid = Spec.MemberGuid;

	if (Spec.Kind == EBindingKind::Function)
	{
		UFunction* Function = ResolveFunction(WidgetBlueprint, Spec.Function);
		if (!Function)
		{
			return BindingFailure(
				FString::Printf(TEXT("Binding function '%s' does not exist"), *Spec.Function.ToString()),
				Spec.JsonPath / TEXT("Function"),
				TEXT("MissingBindingFunction"));
		}
		if (Spec.MemberGuid.IsValid())
		{
			FName FunctionNameFromGuid = NAME_None;
			if (WidgetBlueprint->SkeletonGeneratedClass)
			{
				FunctionNameFromGuid = UBlueprint::GetFieldNameFromClassByGuid<UFunction>(WidgetBlueprint->SkeletonGeneratedClass, Spec.MemberGuid);
			}
			if (FunctionNameFromGuid.IsNone() && WidgetBlueprint->GeneratedClass)
			{
				FunctionNameFromGuid = UBlueprint::GetFieldNameFromClassByGuid<UFunction>(WidgetBlueprint->GeneratedClass, Spec.MemberGuid);
			}
			if (FunctionNameFromGuid.IsNone() || FunctionNameFromGuid != Spec.Function)
			{
				return BindingFailure(
					FString::Printf(TEXT("Binding MemberGuid does not resolve to function '%s'"), *Spec.Function.ToString()),
					Spec.JsonPath / TEXT("MemberGuid"),
					TEXT("MismatchedBindingMemberGuid"));
			}
		}

		OutBinding.FunctionName = Spec.Function;
		if (!Function->IsSignatureCompatibleWith(
				DelegateProperty->SignatureFunction,
				UFunction::GetDefaultIgnoredSignatureCompatibilityFlags() | CPF_ReturnParm)
			|| (RequiresPureBindingFunction(TargetWidget, Spec.Property)
				&& !Function->HasAnyFunctionFlags(FUNC_Const | FUNC_BlueprintPure)))
		{
			return BindingFailure(
				FString::Printf(TEXT("Binding function '%s' is not compatible with '%s.%s'"), *Spec.Function.ToString(), *Spec.Widget.ToString(), *Spec.Property.ToString()),
				Spec.JsonPath / TEXT("Function"),
				TEXT("InvalidBindingFunctionSignature"));
		}
	}
	else
	{
		TArray<FFieldVariant> SourceChain;
		const FAssetDocumentCapabilityResult SourcePathResult = ResolvePropertyPath(WidgetBlueprint, Spec, SourceChain);
		if (!SourcePathResult.bSuccess)
		{
			return SourcePathResult;
		}

		OutBinding.SourcePath = FEditorPropertyPath(SourceChain);
		FText ValidationError;
		if (!OutBinding.SourcePath.Validate(DelegateProperty, ValidationError))
		{
			return BindingFailure(
				FString::Printf(TEXT("Binding source path is not compatible with '%s.%s': %s"), *Spec.Widget.ToString(), *Spec.Property.ToString(), *ValidationError.ToString()),
				Spec.JsonPath / TEXT("SourcePath"),
				TEXT("InvalidBindingSourcePath"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

void SortEditorBindings(TArray<FDelegateEditorBinding>& Bindings)
{
	Bindings.Sort([](const FDelegateEditorBinding& Left, const FDelegateEditorBinding& Right)
	{
		return BindingIdentityKey(Left) < BindingIdentityKey(Right);
	});
}

TSharedPtr<FJsonObject> BindingToJson(const FDelegateEditorBinding& Binding)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Widget"), Binding.ObjectName);
	Json->SetStringField(TEXT("Property"), Binding.PropertyName.ToString());
	Json->SetStringField(TEXT("Kind"), Binding.Kind == EBindingKind::Function ? TEXT("Function") : TEXT("Property"));
	if (Binding.Kind == EBindingKind::Function)
	{
		Json->SetStringField(TEXT("Function"), Binding.FunctionName.ToString());
		if (Binding.MemberGuid.IsValid())
		{
			Json->SetStringField(TEXT("MemberGuid"), Binding.MemberGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
		}
	}
	else
	{
		TArray<TSharedPtr<FJsonValue>> SourcePath;
		for (const FEditorPropertyPathSegment& Segment : Binding.SourcePath.Segments)
		{
			SourcePath.Add(MakeShared<FJsonValueString>(Segment.GetMemberName().ToString()));
		}
		Json->SetArrayField(TEXT("SourcePath"), SourcePath);
	}
	return Json;
}

TSharedPtr<FJsonValue> BindingsToJsonValue(const TArray<FDelegateEditorBinding>& Bindings)
{
	TArray<FDelegateEditorBinding> SortedBindings = Bindings;
	SortEditorBindings(SortedBindings);

	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FDelegateEditorBinding& Binding : SortedBindings)
	{
		Values.Add(MakeShared<FJsonValueObject>(BindingToJson(Binding)));
	}
	return MakeShared<FJsonValueArray>(Values);
}

FString WidgetBindingJsonValueToComparableString(const TSharedPtr<FJsonValue>& Value)
{
	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Value.IsValid() ? Value.ToSharedRef() : MakeShared<FJsonValueNull>(), TEXT(""), Writer);
	return JsonText;
}

FAssetDocumentCapabilityResult BuildDesiredBindings(
	UWidgetBlueprint* WidgetBlueprint,
	const TSharedPtr<FJsonValue>& BindingsJson,
	TArray<FDelegateEditorBinding>& OutBindings)
{
	OutBindings.Reset();

	TArray<FParsedWidgetBinding> Specs;
	FAssetDocumentCapabilityResult ParseResult = ParseBindingSpecs(BindingsJson, Specs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}

	for (const FParsedWidgetBinding& Spec : Specs)
	{
		FDelegateEditorBinding Binding;
		const FAssetDocumentCapabilityResult BuildResult = BuildEditorBinding(WidgetBlueprint, Spec, Binding);
		if (!BuildResult.bSuccess)
		{
			return BuildResult;
		}
		OutBindings.Add(MoveTemp(Binding));
	}
	SortEditorBindings(OutBindings);
	return FAssetDocumentCapabilityResult::Success();
}
}

FAssetDocumentCapabilityResult FWidgetBlueprintBindingAdapter::Validate(const TSharedPtr<FJsonValue>& BindingsJson)
{
	TArray<FParsedWidgetBinding> Specs;
	return ParseBindingSpecs(BindingsJson, Specs);
}

FAssetDocumentCapabilityResult FWidgetBlueprintBindingAdapter::Preflight(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& BindingsJson)
{
	TArray<FDelegateEditorBinding> DesiredBindings;
	const FAssetDocumentCapabilityResult BuildResult = BuildDesiredBindings(WidgetBlueprint, BindingsJson, DesiredBindings);
	if (!BuildResult.bSuccess)
	{
		return BuildResult;
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Preflighted WidgetBlueprint Bindings"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintBindingAdapter::Apply(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& BindingsJson, bool* bOutChanged)
{
	if (bOutChanged)
	{
		*bOutChanged = false;
	}

	TArray<FDelegateEditorBinding> DesiredBindings;
	const FAssetDocumentCapabilityResult BuildResult = BuildDesiredBindings(WidgetBlueprint, BindingsJson, DesiredBindings);
	if (!BuildResult.bSuccess)
	{
		return BuildResult;
	}

#if WITH_EDITORONLY_DATA
	const FString CurrentComparable = WidgetBindingJsonValueToComparableString(BindingsToJsonValue(WidgetBlueprint ? WidgetBlueprint->Bindings : TArray<FDelegateEditorBinding>()));
	const FString DesiredComparable = WidgetBindingJsonValueToComparableString(BindingsToJsonValue(DesiredBindings));
	if (CurrentComparable != DesiredComparable)
	{
		if (!WidgetBlueprint)
		{
			return BindingFailure(TEXT("WidgetBlueprint is required for applying bindings"), TEXT("/Body/Bindings"), TEXT("UnsupportedAsset"));
		}
		WidgetBlueprint->Modify();
		WidgetBlueprint->Bindings = MoveTemp(DesiredBindings);
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
		if (bOutChanged)
		{
			*bOutChanged = true;
		}
	}
#endif

	return FAssetDocumentCapabilityResult::Success(TEXT("Applied WidgetBlueprint Bindings"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintBindingAdapter::Extract(const UWidgetBlueprint* WidgetBlueprint, TArray<TSharedPtr<FJsonValue>>& OutBindings)
{
	OutBindings.Reset();
#if WITH_EDITORONLY_DATA
	TArray<FDelegateEditorBinding> Bindings = WidgetBlueprint ? WidgetBlueprint->Bindings : TArray<FDelegateEditorBinding>();
	SortEditorBindings(Bindings);
	for (const FDelegateEditorBinding& Binding : Bindings)
	{
		OutBindings.Add(MakeShared<FJsonValueObject>(BindingToJson(Binding)));
	}
#endif
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted WidgetBlueprint Bindings"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintBindingAdapter::CanonicalizeDesired(const TSharedPtr<FJsonValue>& BindingsJson, TSharedPtr<FJsonValue>& OutCanonicalJson)
{
	TArray<FParsedWidgetBinding> Specs;
	const FAssetDocumentCapabilityResult ParseResult = ParseBindingSpecs(BindingsJson, Specs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}

	TArray<TSharedPtr<FJsonValue>> Values;
	Specs.Sort([](const FParsedWidgetBinding& Left, const FParsedWidgetBinding& Right)
	{
		return BindingIdentityKey(Left.Widget, Left.Property) < BindingIdentityKey(Right.Widget, Right.Property);
	});
	for (const FParsedWidgetBinding& Spec : Specs)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("Widget"), Spec.Widget.ToString());
		Json->SetStringField(TEXT("Property"), Spec.Property.ToString());
		Json->SetStringField(TEXT("Kind"), Spec.Kind == EBindingKind::Function ? TEXT("Function") : TEXT("Property"));
		if (Spec.Kind == EBindingKind::Function)
		{
			Json->SetStringField(TEXT("Function"), Spec.Function.ToString());
			if (Spec.MemberGuid.IsValid())
			{
				Json->SetStringField(TEXT("MemberGuid"), Spec.MemberGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			}
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> SourcePath;
			for (const FName& Segment : Spec.SourcePath)
			{
				SourcePath.Add(MakeShared<FJsonValueString>(Segment.ToString()));
			}
			Json->SetArrayField(TEXT("SourcePath"), SourcePath);
		}
		Values.Add(MakeShared<FJsonValueObject>(Json));
	}
	OutCanonicalJson = MakeShared<FJsonValueArray>(Values);
	return FAssetDocumentCapabilityResult::Success();
}
