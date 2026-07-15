// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentRegionCanonicalizer.h"

#include "AssetDocumentCanonicalJson.h"
#include "Graphs/AssetDocumentGraphParser.h"

#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "AnimationUtils.h"
#include "Misc/SecureHash.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
TSharedPtr<FJsonValue> CloneJsonValuePreservingShape(const TSharedPtr<FJsonValue>& Value);

TSharedRef<FJsonObject> CloneJsonObjectPreservingShape(const TSharedRef<FJsonObject>& Object)
{
	TSharedRef<FJsonObject> Clone = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		Clone->SetField(Pair.Key, CloneJsonValuePreservingShape(Pair.Value));
	}
	return Clone;
}

TSharedPtr<FJsonValue> CloneJsonValuePreservingShape(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type == EJson::Null || Value->Type == EJson::None)
	{
		return MakeShared<FJsonValueNull>();
	}

	switch (Value->Type)
	{
	case EJson::String:
		return MakeShared<FJsonValueString>(Value->AsString());
	case EJson::Number:
		return MakeShared<FJsonValueNumber>(Value->AsNumber());
	case EJson::Boolean:
		return MakeShared<FJsonValueBoolean>(Value->AsBool());
	case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> ClonedArray;
			for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
			{
				ClonedArray.Add(CloneJsonValuePreservingShape(Entry));
			}
			return MakeShared<FJsonValueArray>(MoveTemp(ClonedArray));
		}
	case EJson::Object:
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			if (Object.IsValid())
			{
				return MakeShared<FJsonValueObject>(CloneJsonObjectPreservingShape(Object.ToSharedRef()));
			}
			return MakeShared<FJsonValueNull>();
		}
	default:
		return MakeShared<FJsonValueNull>();
	}
}

class FAssetDocumentIdentityRegionCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(RegionValue, Context.Policy);
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext&,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return CloneJsonValuePreservingShape(RegionValue);
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetIdentityStrategy()
{
	static FAssetDocumentIdentityRegionCanonicalizationStrategy Strategy;
	return Strategy;
}

bool IsGeneratedObjectIdentityField(const FString& FieldName)
{
	return FieldName == TEXT("ObjectPath")
		|| FieldName == TEXT("ObjectName")
		|| FieldName == TEXT("Outer")
		|| FieldName == TEXT("Package")
		|| FieldName == TEXT("ClassGeneratedBy")
		|| FieldName == TEXT("SkeletonGeneratedClass");
}

bool IsGeneratedObjectIdentityValue(const FString& Value)
{
	if (Value.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	return Value.Contains(TEXT("/Engine/Transient"), ESearchCase::IgnoreCase)
		|| Value.Contains(TEXT("TransientPackage"), ESearchCase::IgnoreCase)
		|| Value.Contains(TEXT("REINST_"), ESearchCase::CaseSensitive)
		|| Value.Contains(TEXT("SKEL_"), ESearchCase::CaseSensitive)
		|| Value.Contains(TEXT("TRASHCLASS_"), ESearchCase::CaseSensitive)
		|| Value.Contains(TEXT("PLACEHOLDER-CLASS"), ESearchCase::CaseSensitive);
}

FString NormalizeGeneratedObjectIdentityValue(const FString& FieldName, const FString& Value)
{
	if (!IsGeneratedObjectIdentityField(FieldName) || !IsGeneratedObjectIdentityValue(Value))
	{
		return Value;
	}

	return FString::Printf(TEXT("<generated-object-identity:%s>"), *FieldName);
}

bool IsAuthoredPropertiesSubtree(const FString& FieldName)
{
	return FieldName == TEXT("Properties");
}

void NormalizeGeneratedObjectPathFields(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (!Object.IsValid())
		{
			return;
		}

		TArray<FString> FieldNames;
		Object->Values.GenerateKeyArray(FieldNames);
		for (const FString& FieldName : FieldNames)
		{
			if (IsAuthoredPropertiesSubtree(FieldName))
			{
				continue;
			}

			TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
			if (!FieldValue || !FieldValue->IsValid())
			{
				continue;
			}

			if ((*FieldValue)->Type == EJson::String)
			{
				const FString NormalizedValue = NormalizeGeneratedObjectIdentityValue(FieldName, (*FieldValue)->AsString());
				if (NormalizedValue != (*FieldValue)->AsString())
				{
					Object->SetStringField(FieldName, NormalizedValue);
				}
				continue;
			}

			NormalizeGeneratedObjectPathFields(*FieldValue);
		}
		return;
	}

	if (Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
		{
			NormalizeGeneratedObjectPathFields(Entry);
		}
	}
}

bool IsGeneratedDiagnosticContainerField(const FString& FieldName)
{
	return FieldName.StartsWith(TEXT("_"))
		|| FieldName == TEXT("GeneratedDiagnostics")
		|| FieldName == TEXT("UnsupportedGraphDiagnostics")
		|| FieldName == TEXT("ValidationDiagnostics")
		|| FieldName == TEXT("ProjectionMetrics");
}

bool IsEmptyJsonContainer(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		return Value->AsArray().Num() == 0;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		return Object.IsValid() && Object->Values.Num() == 0;
	}

	return false;
}

void NormalizeEmptyGeneratedContainers(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return;
	}

	if (Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
		{
			NormalizeEmptyGeneratedContainers(Entry);
		}
		return;
	}

	if (Value->Type != EJson::Object)
	{
		return;
	}

	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	if (!Object.IsValid())
	{
		return;
	}

	TArray<FString> FieldNames;
	Object->Values.GenerateKeyArray(FieldNames);

	TArray<FString> FieldsToRemove;
	for (const FString& FieldName : FieldNames)
	{
		if (IsAuthoredPropertiesSubtree(FieldName))
		{
			continue;
		}

		TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
		if (!FieldValue || !FieldValue->IsValid())
		{
			continue;
		}

		NormalizeEmptyGeneratedContainers(*FieldValue);
		if (IsGeneratedDiagnosticContainerField(FieldName) && IsEmptyJsonContainer(*FieldValue))
		{
			FieldsToRemove.Add(FieldName);
		}
	}

	for (const FString& FieldName : FieldsToRemove)
	{
		Object->RemoveField(FieldName);
	}
}

bool IsBehaviorTreeGeneratedEmptyArrayField(const FString& FieldName, const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if ((FieldName == TEXT("Services")
		|| FieldName == TEXT("Children")
		|| FieldName == TEXT("Decorators")
		|| FieldName == TEXT("DecoratorLogic")
		|| FieldName == TEXT("RootDecorators")
		|| FieldName == TEXT("RootDecoratorLogic"))
		&& Value->Type == EJson::Array
		&& Value->AsArray().Num() == 0)
	{
		return true;
	}

	return false;
}

void RemoveBehaviorTreeDefaultStringField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* FieldName,
	const TCHAR* DefaultValue)
{
	FString Value;
	if (Object.IsValid()
		&& Object->TryGetStringField(FieldName, Value)
		&& Value == DefaultValue)
	{
		Object->RemoveField(FieldName);
	}
}

void RemoveBehaviorTreeDefaultBoolField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* FieldName,
	bool bDefaultValue)
{
	bool bValue = false;
	if (Object.IsValid()
		&& Object->TryGetBoolField(FieldName, bValue)
		&& bValue == bDefaultValue)
	{
		Object->RemoveField(FieldName);
	}
}

void RemoveBehaviorTreeDefaultNumberField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* FieldName,
	double DefaultValue)
{
	double Value = 0.0;
	if (Object.IsValid()
		&& Object->TryGetNumberField(FieldName, Value)
		&& Value == DefaultValue)
	{
		Object->RemoveField(FieldName);
	}
}

void NormalizeBehaviorTreeDefaultVector(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* FieldName,
	const TCHAR* FirstFieldName,
	double FirstDefault,
	const TCHAR* SecondFieldName,
	double SecondDefault)
{
	const TSharedPtr<FJsonObject>* Vector = nullptr;
	if (!Object.IsValid()
		|| !Object->TryGetObjectField(FieldName, Vector)
		|| !Vector
		|| !Vector->IsValid())
	{
		return;
	}

	RemoveBehaviorTreeDefaultNumberField(*Vector, FirstFieldName, FirstDefault);
	RemoveBehaviorTreeDefaultNumberField(*Vector, SecondFieldName, SecondDefault);
	if ((*Vector)->Values.Num() == 0)
	{
		Object->RemoveField(FieldName);
	}
}

void NormalizeBehaviorTreeEditorDefaults(const TSharedPtr<FJsonObject>& Editor)
{
	RemoveBehaviorTreeDefaultStringField(Editor, TEXT("NodeComment"), TEXT(""));
	RemoveBehaviorTreeDefaultBoolField(Editor, TEXT("bCommentBubblePinned"), false);
	RemoveBehaviorTreeDefaultBoolField(Editor, TEXT("bCommentBubbleVisible"), false);
}

void NormalizeBehaviorTreeCommentColor(const TSharedPtr<FJsonObject>& Comment)
{
	const TSharedPtr<FJsonObject>* Color = nullptr;
	if (!Comment.IsValid()
		|| !Comment->TryGetObjectField(TEXT("Color"), Color)
		|| !Color
		|| !Color->IsValid())
	{
		return;
	}

	for (const TCHAR* Channel : {TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A")})
	{
		double AuthoredValue = 0.0;
		if (!(*Color)->TryGetNumberField(Channel, AuthoredValue))
		{
			continue;
		}

		const float StoredValue = static_cast<float>(AuthoredValue);
		if (StoredValue == 1.0f)
		{
			(*Color)->RemoveField(Channel);
		}
		else
		{
			(*Color)->SetNumberField(Channel, StoredValue);
		}
	}
	if ((*Color)->Values.Num() == 0)
	{
		Comment->RemoveField(TEXT("Color"));
	}
}

void NormalizeBehaviorTreeCommentDefaults(const TSharedPtr<FJsonObject>& Comment)
{
	RemoveBehaviorTreeDefaultStringField(Comment, TEXT("Text"), TEXT(""));
	NormalizeBehaviorTreeDefaultVector(Comment, TEXT("Position"), TEXT("X"), 0.0, TEXT("Y"), 0.0);
	NormalizeBehaviorTreeDefaultVector(Comment, TEXT("Size"), TEXT("Width"), 400.0, TEXT("Height"), 100.0);
	NormalizeBehaviorTreeCommentColor(Comment);
	RemoveBehaviorTreeDefaultNumberField(Comment, TEXT("CommentDepth"), -1.0);
	RemoveBehaviorTreeDefaultNumberField(Comment, TEXT("FontSize"), 18.0);
	RemoveBehaviorTreeDefaultStringField(Comment, TEXT("MoveMode"), TEXT("GroupMovement"));
	RemoveBehaviorTreeDefaultStringField(Comment, TEXT("NodeDetails"), TEXT(""));
	RemoveBehaviorTreeDefaultBoolField(Comment, TEXT("bCommentBubblePinned"), true);
	RemoveBehaviorTreeDefaultBoolField(Comment, TEXT("bCommentBubbleVisible"), true);
	RemoveBehaviorTreeDefaultBoolField(Comment, TEXT("bCommentBubbleVisible_InDetailsPanel"), true);
	RemoveBehaviorTreeDefaultBoolField(Comment, TEXT("bColorCommentBubble"), false);
}

void NormalizeBehaviorTreePostApplyValue(
	const TSharedPtr<FJsonValue>& Value,
	bool bInsideAuthoredProperties = false)
{
	if (!Value.IsValid())
	{
		return;
	}

	if (Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
		{
			NormalizeBehaviorTreePostApplyValue(Entry, bInsideAuthoredProperties);
		}
		return;
	}

	if (Value->Type != EJson::Object)
	{
		return;
	}

	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	if (!Object.IsValid())
	{
		return;
	}

	TArray<FString> FieldNames;
	Object->Values.GenerateKeyArray(FieldNames);

	TArray<FString> FieldsToRemove;
	for (const FString& FieldName : FieldNames)
	{
		TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
		if (!FieldValue || !FieldValue->IsValid())
		{
			continue;
		}

		if (!bInsideAuthoredProperties
			&& FieldName == TEXT("Editor")
			&& (*FieldValue)->Type == EJson::Object)
		{
			NormalizeBehaviorTreeEditorDefaults((*FieldValue)->AsObject());
		}
		else if (!bInsideAuthoredProperties
			&& FieldName == TEXT("Comments")
			&& (*FieldValue)->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& CommentValue : (*FieldValue)->AsArray())
			{
				if (CommentValue.IsValid() && CommentValue->Type == EJson::Object)
				{
					NormalizeBehaviorTreeCommentDefaults(CommentValue->AsObject());
				}
			}
		}

		NormalizeBehaviorTreePostApplyValue(
			*FieldValue,
			bInsideAuthoredProperties || IsAuthoredPropertiesSubtree(FieldName));
		if (!bInsideAuthoredProperties
			&& IsBehaviorTreeGeneratedEmptyArrayField(FieldName, *FieldValue))
		{
			FieldsToRemove.Add(FieldName);
		}
	}

	for (const FString& FieldName : FieldsToRemove)
	{
		Object->RemoveField(FieldName);
	}
}

double NormalizeEditorLayoutNumber(double Value)
{
	if (FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value), 0.000001))
	{
		return FMath::RoundToDouble(Value);
	}
	return FMath::RoundToDouble(Value * 1000000.0) / 1000000.0;
}

FString GetObjectFieldStringOrEmpty(const TSharedPtr<FJsonValue>& Value, const FString& FieldName)
{
	const TSharedPtr<FJsonObject> Object = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	if (!Object.IsValid())
	{
		return FString();
	}
	FString FieldValue;
	Object->TryGetStringField(FieldName, FieldValue);
	return FieldValue;
}

void SortObjectArrayByField(const TSharedPtr<FJsonObject>& Object, const FString& ArrayField, const FString& SortField)
{
	const TArray<TSharedPtr<FJsonValue>>* ExistingArray = nullptr;
	if (!Object.IsValid() || !Object->TryGetArrayField(ArrayField, ExistingArray) || !ExistingArray)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> SortedArray = *ExistingArray;
	SortedArray.Sort([&SortField](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		return GetObjectFieldStringOrEmpty(Left, SortField) < GetObjectFieldStringOrEmpty(Right, SortField);
	});
	Object->SetArrayField(ArrayField, MoveTemp(SortedArray));
}

void NormalizeEditorLayoutValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return;
	}

	if (Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
		{
			NormalizeEditorLayoutValue(Entry);
		}
		return;
	}

	if (Value->Type != EJson::Object)
	{
		return;
	}

	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	if (!Object.IsValid())
	{
		return;
	}

	TArray<FString> FieldNames;
	Object->Values.GenerateKeyArray(FieldNames);
	for (const FString& FieldName : FieldNames)
	{
		TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
		if (!FieldValue || !FieldValue->IsValid())
		{
			continue;
		}

		if ((*FieldValue)->Type == EJson::Number)
		{
			Object->SetNumberField(FieldName, NormalizeEditorLayoutNumber((*FieldValue)->AsNumber()));
			continue;
		}

		NormalizeEditorLayoutValue(*FieldValue);
	}

	SortObjectArrayByField(Object, TEXT("Nodes"), TEXT("NodeId"));
	SortObjectArrayByField(Object, TEXT("Comments"), TEXT("Id"));
}

FString GetManagedPropertyLeafName(const FString& PropertyPath)
{
	FString LeafName = PropertyPath;
	int32 DotIndex = INDEX_NONE;
	if (LeafName.FindLastChar(TEXT('.'), DotIndex))
	{
		LeafName.RightChopInline(DotIndex + 1, EAllowShrinking::No);
	}
	return LeafName;
}

FProperty* FindManagedProperty(UClass* AssetClass, const FString& PropertyPath)
{
	if (!AssetClass)
	{
		return nullptr;
	}

	const FString PropertyName = GetManagedPropertyLeafName(PropertyPath);
	if (PropertyName.IsEmpty())
	{
		return nullptr;
	}

	return FindFProperty<FProperty>(AssetClass, FName(*PropertyName));
}

const UObject* GetProjectDefaultAnimationObject(const FObjectPropertyBase* ObjectProperty)
{
	if (!ObjectProperty || !ObjectProperty->PropertyClass)
	{
		return nullptr;
	}

	const UObject* BoneCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
	if (BoneCompressionSettings && BoneCompressionSettings->IsA(ObjectProperty->PropertyClass))
	{
		return BoneCompressionSettings;
	}

	const UObject* CurveCompressionSettings = FAnimationUtils::GetDefaultAnimationCurveCompressionSettings();
	if (CurveCompressionSettings && CurveCompressionSettings->IsA(ObjectProperty->PropertyClass))
	{
		return CurveCompressionSettings;
	}

	return nullptr;
}

bool IsDefaultObjectAssetRef(
	const TSharedPtr<FJsonValue>& FieldValue,
	UClass* AssetClass,
	const FString& PropertyPath)
{
	if (!AssetClass || !FieldValue.IsValid() || FieldValue->Type != EJson::Object)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> FieldObject = FieldValue->AsObject();
	if (!FieldObject.IsValid())
	{
		return false;
	}

	if (FieldObject->Values.Num() != 2
		|| !FieldObject->HasField(TEXT("Kind"))
		|| !FieldObject->HasField(TEXT("Path")))
	{
		return false;
	}

	FString Kind;
	FString Path;
	if (!FieldObject->TryGetStringField(TEXT("Kind"), Kind)
		|| !Kind.Equals(TEXT("AssetRef"), ESearchCase::CaseSensitive)
		|| !FieldObject->TryGetStringField(TEXT("Path"), Path)
		|| Path.IsEmpty())
	{
		return false;
	}

	const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(FindManagedProperty(AssetClass, PropertyPath));
	const UObject* DefaultObject = AssetClass->GetDefaultObject();
	if (!ObjectProperty || !DefaultObject)
	{
		return false;
	}

	const UObject* DefaultValue = ObjectProperty->GetObjectPropertyValue_InContainer(DefaultObject);
	if (DefaultValue && Path == DefaultValue->GetPathName())
	{
		return true;
	}

	if (const UObject* ProjectDefault = GetProjectDefaultAnimationObject(ObjectProperty))
	{
		return Path == ProjectDefault->GetPathName();
	}

	return false;
}

void NormalizeManagedNumericFields(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	if (!Context.Policy || !Context.AssetClass || !Value.IsValid() || Value->Type != EJson::Object)
	{
		return;
	}

	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	if (!Object.IsValid())
	{
		return;
	}

	for (const FString& PropertyPath : Context.Policy->ManagedUePropertyPaths)
	{
		const FString FieldName = GetManagedPropertyLeafName(PropertyPath);
		TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
		if (!FieldValue || !FieldValue->IsValid() || (*FieldValue)->Type != EJson::Number)
		{
			continue;
		}

		if (CastField<FFloatProperty>(FindManagedProperty(Context.AssetClass, PropertyPath)))
		{
			Object->SetNumberField(FieldName, static_cast<float>((*FieldValue)->AsNumber()));
		}
	}
}

void NormalizeDefaultObjectReferenceFields(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	if (!Context.Policy || !Context.AssetClass || !Value.IsValid() || Value->Type != EJson::Object)
	{
		return;
	}

	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	if (!Object.IsValid())
	{
		return;
	}

	TArray<FString> FieldsToRemove;
	for (const FString& PropertyPath : Context.Policy->ManagedUePropertyPaths)
	{
		const FString FieldName = GetManagedPropertyLeafName(PropertyPath);
		const TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
		if (FieldValue && IsDefaultObjectAssetRef(*FieldValue, Context.AssetClass, PropertyPath))
		{
			FieldsToRemove.Add(FieldName);
		}
	}

	for (const FString& FieldName : FieldsToRemove)
	{
		Object->RemoveField(FieldName);
	}
}

bool IsAnimCurveObject(const TSharedPtr<FJsonObject>& Object)
{
	return Object.IsValid()
		&& Object->HasTypedField<EJson::String>(TEXT("Name"))
		&& Object->HasTypedField<EJson::String>(TEXT("CurveType"))
		&& Object->HasTypedField<EJson::Array>(TEXT("Keys"));
}

FString NormalizeRichCurveInterpMode(FString InterpMode)
{
	if (InterpMode.StartsWith(TEXT("RCIM_"), ESearchCase::CaseSensitive))
	{
		InterpMode.RightChopInline(5, EAllowShrinking::No);
	}
	return InterpMode;
}

void NormalizeAnimCurveKeyObject(const TSharedPtr<FJsonObject>& KeyObject)
{
	if (!KeyObject.IsValid())
	{
		return;
	}

	FString InterpMode;
	if (KeyObject->TryGetStringField(TEXT("InterpMode"), InterpMode))
	{
		KeyObject->SetStringField(TEXT("InterpMode"), NormalizeRichCurveInterpMode(InterpMode));
	}

	for (const TCHAR* NumericField : {TEXT("Time"), TEXT("Value"), TEXT("ArriveTangent"), TEXT("LeaveTangent"), TEXT("TangentWeight")})
	{
		double Number = 0.0;
		if (KeyObject->TryGetNumberField(NumericField, Number))
		{
			KeyObject->SetNumberField(NumericField, static_cast<float>(Number));
		}
	}
}

void NormalizeAnimCurveObject(const TSharedPtr<FJsonObject>& CurveObject)
{
	if (!IsAnimCurveObject(CurveObject))
	{
		return;
	}

	FString Name;
	if (CurveObject->TryGetStringField(TEXT("Name"), Name))
	{
		CurveObject->SetStringField(TEXT("Name"), Name.ToLower());
	}

	const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
	if (CurveObject->TryGetArrayField(TEXT("Keys"), Keys) && Keys)
	{
		for (const TSharedPtr<FJsonValue>& KeyValue : *Keys)
		{
			if (KeyValue.IsValid() && KeyValue->Type == EJson::Object)
			{
				NormalizeAnimCurveKeyObject(KeyValue->AsObject());
			}
		}
	}
}

void NormalizeAnimCurveArrayForHash(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>>& Array = const_cast<TArray<TSharedPtr<FJsonValue>>&>(Value->AsArray());
	if (Array.Num() == 0)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& Entry : Array)
	{
		if (!Entry.IsValid() || Entry->Type != EJson::Object || !IsAnimCurveObject(Entry->AsObject()))
		{
			return;
		}
	}

	for (const TSharedPtr<FJsonValue>& Entry : Array)
	{
		NormalizeAnimCurveObject(Entry->AsObject());
	}

	Array.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		const TSharedPtr<FJsonObject> LeftObject = Left.IsValid() && Left->Type == EJson::Object ? Left->AsObject() : nullptr;
		const TSharedPtr<FJsonObject> RightObject = Right.IsValid() && Right->Type == EJson::Object ? Right->AsObject() : nullptr;
		const FString LeftName = LeftObject.IsValid() ? LeftObject->GetStringField(TEXT("Name")) : FString();
		const FString RightName = RightObject.IsValid() ? RightObject->GetStringField(TEXT("Name")) : FString();
		return LeftName < RightName;
	});
}

bool IsAnimTimelineFloatField(const FString& FieldName)
{
	return FieldName == TEXT("Time")
		|| FieldName == TEXT("Duration");
}

bool IsPropertiesSubtree(const FString& FieldName)
{
	return FieldName == TEXT("Properties");
}

void NormalizeAnimTimelineNumericFields(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return;
	}

	if (Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
		{
			NormalizeAnimTimelineNumericFields(Entry);
		}
		return;
	}

	if (Value->Type != EJson::Object)
	{
		return;
	}

	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	if (!Object.IsValid())
	{
		return;
	}

	TArray<FString> FieldNames;
	Object->Values.GenerateKeyArray(FieldNames);
	for (const FString& FieldName : FieldNames)
	{
		TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
		if (!FieldValue || !FieldValue->IsValid())
		{
			continue;
		}

		if (IsAnimTimelineFloatField(FieldName) && (*FieldValue)->Type == EJson::Number)
		{
			Object->SetNumberField(FieldName, static_cast<float>((*FieldValue)->AsNumber()));
			continue;
		}

		if (IsPropertiesSubtree(FieldName))
		{
			continue;
		}

		NormalizeAnimTimelineNumericFields(*FieldValue);
	}
}

FString MakeCanonicalObjectSortKey(const TSharedPtr<FJsonObject>& Object)
{
	if (!Object.IsValid())
	{
		return FString();
	}

	return FAssetDocumentCanonicalJson::WriteCanonicalJson(
		MakeShared<FJsonValueObject>(CloneJsonObjectPreservingShape(Object.ToSharedRef())),
		nullptr);
}

void NormalizeAnimTimelineArrayOrder(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>>& Array = const_cast<TArray<TSharedPtr<FJsonValue>>&>(Value->AsArray());
	if (Array.Num() == 0)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& Entry : Array)
	{
		if (!Entry.IsValid()
			|| Entry->Type != EJson::Object
			|| !Entry->AsObject().IsValid()
			|| !Entry->AsObject()->HasTypedField<EJson::String>(TEXT("Name"))
			|| !Entry->AsObject()->HasTypedField<EJson::Number>(TEXT("Time")))
		{
			return;
		}
	}

	Array.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		const TSharedPtr<FJsonObject> LeftObject = Left.IsValid() && Left->Type == EJson::Object ? Left->AsObject() : nullptr;
		const TSharedPtr<FJsonObject> RightObject = Right.IsValid() && Right->Type == EJson::Object ? Right->AsObject() : nullptr;
		const double LeftTime = LeftObject.IsValid() ? LeftObject->GetNumberField(TEXT("Time")) : 0.0;
		const double RightTime = RightObject.IsValid() ? RightObject->GetNumberField(TEXT("Time")) : 0.0;
		if (LeftTime != RightTime)
		{
			return LeftTime < RightTime;
		}

		const FString LeftName = LeftObject.IsValid() ? LeftObject->GetStringField(TEXT("Name")) : FString();
		const FString RightName = RightObject.IsValid() ? RightObject->GetStringField(TEXT("Name")) : FString();
		if (LeftName != RightName)
		{
			return LeftName < RightName;
		}

		return MakeCanonicalObjectSortKey(LeftObject) < MakeCanonicalObjectSortKey(RightObject);
	});
}

bool IsAnimCurveRegion(const FAssetDocumentRegionPolicy* Policy)
{
	return Policy
		&& (Policy->BodyPath == TEXT("Body.Curves")
			|| Policy->RegionId == FName(TEXT("Body.Curves")));
}

bool IsAnimTimelineRegion(const FAssetDocumentRegionPolicy* Policy)
{
	return Policy && Policy->RegionKind == EAssetDocumentRegionKind::Timeline;
}

FString MakeGraphNodeSemanticKey(const FAssetDocumentNodeSpec& Node)
{
	FString MemberJson;
	if (Node.Member.IsValid())
	{
		const TSharedRef<FJsonObject> MemberForHash = CloneJsonObjectPreservingShape(Node.Member.ToSharedRef());
		MemberForHash->RemoveField(TEXT("Guid"));
		MemberJson = FAssetDocumentCanonicalJson::WriteCanonicalJson(
			MakeShared<FJsonValueObject>(MemberForHash),
			nullptr);
	}

	return FString::Printf(TEXT("%s|%s"), *Node.Class, *MemberJson);
}

FString MakeSemanticNodeId(const FString& SemanticKey)
{
	const FTCHARToUTF8 Utf8SemanticKey(*SemanticKey);
	const FSHAHash Hash = FSHA1::HashBuffer(
		Utf8SemanticKey.Get(),
		static_cast<uint64>(Utf8SemanticKey.Length()));
	return FString::Printf(TEXT("semantic_%s"), *Hash.ToString().Left(16).ToLower());
}

void NormalizeGraphSchemaForHash(FAssetDocumentGraphSpec& Graph)
{
	if (Graph.Schema == TEXT("/Script/UMGEditor.WidgetGraphSchema"))
	{
		Graph.Schema = TEXT("/Script/BlueprintGraph.EdGraphSchema_K2");
	}
}

void NormalizeGraphNodeMemberForHash(FAssetDocumentNodeSpec& Node)
{
	if (Node.Member.IsValid())
	{
		Node.Member->RemoveField(TEXT("Guid"));
	}
}

void RewriteGraphNodeIdsForHash(FAssetDocumentGraphSpec& Graph)
{
	TMap<FString, int32> SemanticKeyCounts;
	TMap<FString, FString> OldNodeIdToSemanticId;
	TMap<FString, int32> FinalIdCounts;

	for (const FAssetDocumentNodeSpec& Node : Graph.Nodes)
	{
		const FString SemanticKey = MakeGraphNodeSemanticKey(Node);
		SemanticKeyCounts.FindOrAdd(SemanticKey)++;
	}

	for (const FAssetDocumentNodeSpec& Node : Graph.Nodes)
	{
		const FString SemanticKey = MakeGraphNodeSemanticKey(Node);
		const FString FinalId = SemanticKeyCounts.FindRef(SemanticKey) == 1
			? MakeSemanticNodeId(SemanticKey)
			: Node.Id;
		FinalIdCounts.FindOrAdd(FinalId)++;
	}

	for (FAssetDocumentNodeSpec& Node : Graph.Nodes)
	{
		const FString SemanticKey = MakeGraphNodeSemanticKey(Node);
		if (SemanticKeyCounts.FindRef(SemanticKey) != 1)
		{
			continue;
		}

		const FString SemanticId = MakeSemanticNodeId(SemanticKey);
		if (FinalIdCounts.FindRef(SemanticId) != 1)
		{
			continue;
		}

		OldNodeIdToSemanticId.Add(Node.Id, SemanticId);
		Node.Id = SemanticId;
	}

	for (FAssetDocumentLinkSpec& Link : Graph.Links)
	{
		if (const FString* FromNodeId = OldNodeIdToSemanticId.Find(Link.From.Node))
		{
			Link.From.Node = *FromNodeId;
		}
		if (const FString* ToNodeId = OldNodeIdToSemanticId.Find(Link.To.Node))
		{
			Link.To.Node = *ToNodeId;
		}
	}
}

TSharedPtr<FJsonValue> CanonicalizeGraphArrayForHash(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	if (!RegionValue.IsValid() || RegionValue->Type != EJson::Array)
	{
		return GetIdentityStrategy().CanonicalizeForHash(Context, RegionValue);
	}

	FAssetDocumentGraphParseOptions ParseOptions;
	ParseOptions.Path = TEXT("");
	FAssetDocumentGraphParseResult ParseResult = FAssetDocumentGraphParser::ParseGraphArray(RegionValue->AsArray(), ParseOptions);
	if (!ParseResult.IsValid())
	{
		return GetIdentityStrategy().CanonicalizeForHash(Context, RegionValue);
	}

	for (FAssetDocumentGraphSpec& Graph : ParseResult.Graphs)
	{
		NormalizeGraphSchemaForHash(Graph);
		Graph.GraphGuid.Reset();
		for (FAssetDocumentNodeSpec& Node : Graph.Nodes)
		{
			Node.NodeGuid.Reset();
			Node.Capability.Reset();
			NormalizeGraphNodeMemberForHash(Node);
		}
		RewriteGraphNodeIdsForHash(Graph);
	}

	return FAssetDocumentGraphParser::WriteCanonicalGraphArray(ParseResult.Graphs);
}

class FAssetDocumentUBlueprintGraphCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return CanonicalizeGraphArrayForHash(Context, RegionValue);
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return GetIdentityStrategy().CanonicalizeForSidecarWriteback(Context, RegionValue);
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetUBlueprintGraphStrategy()
{
	static FAssetDocumentUBlueprintGraphCanonicalizationStrategy Strategy;
	return Strategy;
}

TSharedPtr<FJsonValue> SortObjectByFieldName(const TSharedPtr<FJsonValue>& RegionValue)
{
	if (!RegionValue.IsValid() || RegionValue->Type != EJson::Object)
	{
		return CloneJsonValuePreservingShape(RegionValue);
	}

	const TSharedPtr<FJsonObject> SourceObject = RegionValue->AsObject();
	if (!SourceObject.IsValid())
	{
		return MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
	}

	TArray<FString> Keys;
	SourceObject->Values.GetKeys(Keys);
	Keys.Sort();

	TSharedRef<FJsonObject> SortedObject = MakeShared<FJsonObject>();
	for (const FString& Key : Keys)
	{
		const TSharedPtr<FJsonValue>* Value = SourceObject->Values.Find(Key);
		SortedObject->SetField(Key, Value ? CloneJsonValuePreservingShape(*Value) : MakeShared<FJsonValueNull>());
	}
	return MakeShared<FJsonValueObject>(SortedObject);
}

class FAssetDocumentWidgetBlueprintWidgetVariableGuidsCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return SortObjectByFieldName(FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(RegionValue, Context.Policy));
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext&,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return SortObjectByFieldName(RegionValue);
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetWidgetBlueprintWidgetVariableGuidsStrategy()
{
	static FAssetDocumentWidgetBlueprintWidgetVariableGuidsCanonicalizationStrategy Strategy;
	return Strategy;
}

double GetObjectNumberFieldOrZero(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
{
	if (!Object.IsValid())
	{
		return 0.0;
	}
	double Value = 0.0;
	Object->TryGetNumberField(FieldName, Value);
	return Value;
}

FString GetObjectStringFieldOrEmpty(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
{
	if (!Object.IsValid())
	{
		return FString();
	}
	FString Value;
	Object->TryGetStringField(FieldName, Value);
	return Value;
}

void SortArrayObjectsByStringField(const TSharedPtr<FJsonObject>& Object, const FString& ArrayField, const FString& SortField)
{
	const TArray<TSharedPtr<FJsonValue>>* ExistingArray = nullptr;
	if (!Object.IsValid() || !Object->TryGetArrayField(ArrayField, ExistingArray) || !ExistingArray)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> SortedArray = *ExistingArray;
	SortedArray.Sort([&SortField](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		const TSharedPtr<FJsonObject> LeftObject = Left.IsValid() && Left->Type == EJson::Object ? Left->AsObject() : nullptr;
		const TSharedPtr<FJsonObject> RightObject = Right.IsValid() && Right->Type == EJson::Object ? Right->AsObject() : nullptr;
		return GetObjectStringFieldOrEmpty(LeftObject, SortField) < GetObjectStringFieldOrEmpty(RightObject, SortField);
	});
	Object->SetArrayField(ArrayField, MoveTemp(SortedArray));
}

void SortAnimationKeys(const TSharedPtr<FJsonObject>& Object)
{
	const TArray<TSharedPtr<FJsonValue>>* ExistingKeys = nullptr;
	if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("Keys"), ExistingKeys) || !ExistingKeys)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> SortedKeys = *ExistingKeys;
	SortedKeys.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		const TSharedPtr<FJsonObject> LeftObject = Left.IsValid() && Left->Type == EJson::Object ? Left->AsObject() : nullptr;
		const TSharedPtr<FJsonObject> RightObject = Right.IsValid() && Right->Type == EJson::Object ? Right->AsObject() : nullptr;
		const double LeftFrame = GetObjectNumberFieldOrZero(LeftObject, TEXT("Frame"));
		const double RightFrame = GetObjectNumberFieldOrZero(RightObject, TEXT("Frame"));
		if (LeftFrame != RightFrame)
		{
			return LeftFrame < RightFrame;
		}
		return MakeCanonicalObjectSortKey(LeftObject) < MakeCanonicalObjectSortKey(RightObject);
	});
	Object->SetArrayField(TEXT("Keys"), MoveTemp(SortedKeys));
}

FString MakeAnimationTrackSortKey(const TSharedPtr<FJsonObject>& Object)
{
	return FString::Printf(
		TEXT("%s|%s|%s"),
		*GetObjectStringFieldOrEmpty(Object, TEXT("Widget")),
		*GetObjectStringFieldOrEmpty(Object, TEXT("Property")),
		*GetObjectStringFieldOrEmpty(Object, TEXT("Type"))).ToLower();
}

void NormalizeWidgetBlueprintAnimationValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>>& Animations = const_cast<TArray<TSharedPtr<FJsonValue>>&>(Value->AsArray());
	for (const TSharedPtr<FJsonValue>& AnimationValue : Animations)
	{
		const TSharedPtr<FJsonObject> AnimationObject = AnimationValue.IsValid() && AnimationValue->Type == EJson::Object ? AnimationValue->AsObject() : nullptr;
		if (!AnimationObject.IsValid())
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* TrackValues = nullptr;
		if (AnimationObject->TryGetArrayField(TEXT("Tracks"), TrackValues) && TrackValues)
		{
			TArray<TSharedPtr<FJsonValue>> SortedTracks = *TrackValues;
			for (const TSharedPtr<FJsonValue>& TrackValue : SortedTracks)
			{
				const TSharedPtr<FJsonObject> TrackObject = TrackValue.IsValid() && TrackValue->Type == EJson::Object ? TrackValue->AsObject() : nullptr;
				if (!TrackObject.IsValid())
				{
					continue;
				}

				SortAnimationKeys(TrackObject);
				const TArray<TSharedPtr<FJsonValue>>* ChannelValues = nullptr;
				if (TrackObject->TryGetArrayField(TEXT("Channels"), ChannelValues) && ChannelValues)
				{
					TArray<TSharedPtr<FJsonValue>> SortedChannels = *ChannelValues;
					for (const TSharedPtr<FJsonValue>& ChannelValue : SortedChannels)
					{
						const TSharedPtr<FJsonObject> ChannelObject = ChannelValue.IsValid() && ChannelValue->Type == EJson::Object ? ChannelValue->AsObject() : nullptr;
						SortAnimationKeys(ChannelObject);
					}
					SortedChannels.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
					{
						const TSharedPtr<FJsonObject> LeftObject = Left.IsValid() && Left->Type == EJson::Object ? Left->AsObject() : nullptr;
						const TSharedPtr<FJsonObject> RightObject = Right.IsValid() && Right->Type == EJson::Object ? Right->AsObject() : nullptr;
						return GetObjectStringFieldOrEmpty(LeftObject, TEXT("Name")) < GetObjectStringFieldOrEmpty(RightObject, TEXT("Name"));
					});
					TrackObject->SetArrayField(TEXT("Channels"), MoveTemp(SortedChannels));
				}
			}
			SortedTracks.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
			{
				const TSharedPtr<FJsonObject> LeftObject = Left.IsValid() && Left->Type == EJson::Object ? Left->AsObject() : nullptr;
				const TSharedPtr<FJsonObject> RightObject = Right.IsValid() && Right->Type == EJson::Object ? Right->AsObject() : nullptr;
				return MakeAnimationTrackSortKey(LeftObject) < MakeAnimationTrackSortKey(RightObject);
			});
			AnimationObject->SetArrayField(TEXT("Tracks"), MoveTemp(SortedTracks));
		}
	}

	Animations.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		const TSharedPtr<FJsonObject> LeftObject = Left.IsValid() && Left->Type == EJson::Object ? Left->AsObject() : nullptr;
		const TSharedPtr<FJsonObject> RightObject = Right.IsValid() && Right->Type == EJson::Object ? Right->AsObject() : nullptr;
		return GetObjectStringFieldOrEmpty(LeftObject, TEXT("Name")) < GetObjectStringFieldOrEmpty(RightObject, TEXT("Name"));
	});
}

class FAssetDocumentWidgetBlueprintAnimationsCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		TSharedPtr<FJsonValue> CanonicalValue = FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(RegionValue, Context.Policy);
		NormalizeWidgetBlueprintAnimationValue(CanonicalValue);
		return CanonicalValue;
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext&,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		TSharedPtr<FJsonValue> CanonicalValue = CloneJsonValuePreservingShape(RegionValue);
		NormalizeWidgetBlueprintAnimationValue(CanonicalValue);
		return CanonicalValue;
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetWidgetBlueprintAnimationsStrategy()
{
	static FAssetDocumentWidgetBlueprintAnimationsCanonicalizationStrategy Strategy;
	return Strategy;
}

class FAssetDocumentBehaviorTreePostApplyCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		TSharedPtr<FJsonValue> CanonicalValue = GetIdentityStrategy().CanonicalizeForHash(Context, RegionValue);
		NormalizeBehaviorTreePostApplyValue(CanonicalValue);
		return CanonicalValue;
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext&,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		TSharedPtr<FJsonValue> CanonicalValue = CloneJsonValuePreservingShape(RegionValue);
		NormalizeBehaviorTreePostApplyValue(CanonicalValue);
		return CanonicalValue;
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetBehaviorTreePostApplyStrategy()
{
	static FAssetDocumentBehaviorTreePostApplyCanonicalizationStrategy Strategy;
	return Strategy;
}

class FAssetDocumentEditorLayoutCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		TSharedPtr<FJsonValue> CanonicalValue = GetIdentityStrategy().CanonicalizeForHash(Context, RegionValue);
		NormalizeEditorLayoutValue(CanonicalValue);
		return CanonicalValue;
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext&,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		TSharedPtr<FJsonValue> CanonicalValue = CloneJsonValuePreservingShape(RegionValue);
		NormalizeEditorLayoutValue(CanonicalValue);
		return CanonicalValue;
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetEditorLayoutStrategy()
{
	static FAssetDocumentEditorLayoutCanonicalizationStrategy Strategy;
	return Strategy;
}

class FAssetDocumentAnimSequencePostApplyCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		TSharedPtr<FJsonValue> CanonicalValue = GetIdentityStrategy().CanonicalizeForHash(Context, RegionValue);
		NormalizeGeneratedObjectPathFields(CanonicalValue);
		NormalizeEmptyGeneratedContainers(CanonicalValue);
		NormalizeManagedNumericFields(Context, CanonicalValue);
		NormalizeDefaultObjectReferenceFields(Context, CanonicalValue);
		if (IsAnimCurveRegion(Context.Policy))
		{
			NormalizeAnimCurveArrayForHash(CanonicalValue);
		}
		if (IsAnimTimelineRegion(Context.Policy))
		{
			NormalizeAnimTimelineNumericFields(CanonicalValue);
			NormalizeAnimTimelineArrayOrder(CanonicalValue);
		}
		return CanonicalValue;
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return GetIdentityStrategy().CanonicalizeForSidecarWriteback(Context, RegionValue);
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetAnimSequencePostApplyStrategy()
{
	static FAssetDocumentAnimSequencePostApplyCanonicalizationStrategy Strategy;
	return Strategy;
}

const TMap<FName, const IAssetDocumentRegionCanonicalizationStrategy*>& GetBuiltinCanonicalizerStrategies()
{
	static const TMap<FName, const IAssetDocumentRegionCanonicalizationStrategy*> Strategies = {
		{FName(TEXT("AnimSequencePostApply")), &GetAnimSequencePostApplyStrategy()},
		{FName(TEXT("BehaviorTreePostApply")), &GetBehaviorTreePostApplyStrategy()},
		{FName(TEXT("EditorLayout")), &GetEditorLayoutStrategy()},
		{FName(TEXT("UBlueprintGraph")), &GetUBlueprintGraphStrategy()},
		{FName(TEXT("WidgetBlueprintAnimations")), &GetWidgetBlueprintAnimationsStrategy()},
		{FName(TEXT("WidgetBlueprintWidgetVariableGuids")), &GetWidgetBlueprintWidgetVariableGuidsStrategy()},
	};
	return Strategies;
}

const IAssetDocumentRegionCanonicalizationStrategy& ResolveStrategy(
	const FAssetDocumentRegionCanonicalizeContext& Context)
{
	const FName HookName = Context.Policy ? Context.Policy->CanonicalizerHookName : NAME_None;
	if (const IAssetDocumentRegionCanonicalizationStrategy* const* Strategy = GetBuiltinCanonicalizerStrategies().Find(HookName))
	{
		return **Strategy;
	}
	return GetIdentityStrategy();
}
}

TSharedPtr<FJsonValue> FAssetDocumentRegionCanonicalizer::CanonicalizeForHash(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return ResolveStrategy(Context).CanonicalizeForHash(Context, RegionValue);
}

TSharedPtr<FJsonValue> FAssetDocumentRegionCanonicalizer::CanonicalizeForSidecarWriteback(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return ResolveStrategy(Context).CanonicalizeForSidecarWriteback(Context, RegionValue);
}

FString FAssetDocumentRegionCanonicalizer::HashRegionValue(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return FAssetDocumentCanonicalJson::HashJsonValue(CanonicalizeForHash(Context, RegionValue), nullptr);
}
