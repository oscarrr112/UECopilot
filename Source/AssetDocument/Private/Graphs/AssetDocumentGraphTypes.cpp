// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentGraphTypes.h"

namespace
{
void SetOptionalString(TSharedRef<FJsonObject> Object, const TCHAR* Field, const FString& Value)
{
	if (!Value.IsEmpty())
	{
		Object->SetStringField(Field, Value);
	}
}
}

FString FAssetDocumentGraphEndpoint::ToKey() const
{
	return FString::Printf(TEXT("%s.%s"), *Node, *Pin);
}

TSharedRef<FJsonObject> FAssetDocumentGraphEndpoint::ToJsonObject() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Node"), Node);
	Object->SetStringField(TEXT("Pin"), Pin);
	return Object;
}

FString FAssetDocumentLinkSpec::ToKey() const
{
	return FString::Printf(TEXT("%s->%s"), *From.ToKey(), *To.ToKey());
}

TSharedRef<FJsonObject> FAssetDocumentLinkSpec::ToJsonObject() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetObjectField(TEXT("From"), From.ToJsonObject());
	Object->SetObjectField(TEXT("To"), To.ToJsonObject());
	return Object;
}

TSharedRef<FJsonObject> FAssetDocumentMemberRef::ToJsonObject() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Kind"), Kind);
	SetOptionalString(Object, TEXT("OwnerClass"), OwnerClass);
	SetOptionalString(Object, TEXT("Name"), Name);
	SetOptionalString(Object, TEXT("Guid"), Guid);
	if (bHasSelfContext)
	{
		Object->SetBoolField(TEXT("SelfContext"), bSelfContext);
	}
	return Object;
}

TSharedRef<FJsonObject> FAssetDocumentPinOverrideSpec::ToJsonObject() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Pin"), Pin);
	SetOptionalString(Object, TEXT("Direction"), Direction);
	if (Type.IsValid())
	{
		Object->SetField(TEXT("Type"), AssetDocumentGraphJson::CloneJsonValue(Type));
	}
	if (DefaultValue.IsValid())
	{
		Object->SetField(TEXT("DefaultValue"), AssetDocumentGraphJson::CloneJsonValue(DefaultValue));
	}
	if (DefaultObject.IsValid())
	{
		Object->SetField(TEXT("DefaultObject"), AssetDocumentGraphJson::CloneJsonValue(DefaultObject));
	}
	if (DefaultTextValue.IsValid())
	{
		Object->SetField(TEXT("DefaultTextValue"), AssetDocumentGraphJson::CloneJsonValue(DefaultTextValue));
	}
	if (Hidden.IsSet())
	{
		Object->SetBoolField(TEXT("Hidden"), Hidden.GetValue());
	}
	if (AdvancedView.IsSet())
	{
		Object->SetBoolField(TEXT("AdvancedView"), AdvancedView.GetValue());
	}
	return Object;
}

TSharedRef<FJsonObject> FAssetDocumentNodeSpec::ToJsonObject() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Id"), Id);
	SetOptionalString(Object, TEXT("NodeGuid"), NodeGuid);
	Object->SetStringField(TEXT("Class"), Class);
	SetOptionalString(Object, TEXT("Capability"), Capability);
	if (Member.IsValid())
	{
		Object->SetObjectField(TEXT("Member"), AssetDocumentGraphJson::CloneJsonObject(Member));
	}
	if (!PinOverrides.IsEmpty())
	{
		TArray<FAssetDocumentPinOverrideSpec> SortedPinOverrides = PinOverrides;
		SortedPinOverrides.Sort([](const FAssetDocumentPinOverrideSpec& Left, const FAssetDocumentPinOverrideSpec& Right)
		{
			return Left.Pin < Right.Pin;
		});

		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FAssetDocumentPinOverrideSpec& PinOverride : SortedPinOverrides)
		{
			Values.Add(MakeShared<FJsonValueObject>(PinOverride.ToJsonObject()));
		}
		Object->SetArrayField(TEXT("PinOverrides"), MoveTemp(Values));
	}
	if (Position.IsValid())
	{
		Object->SetObjectField(TEXT("Position"), AssetDocumentGraphJson::CloneJsonObject(Position));
	}
	if (bHasComment)
	{
		Object->SetStringField(TEXT("Comment"), Comment);
	}
	return Object;
}

TSharedRef<FJsonObject> FAssetDocumentGraphSpec::ToJsonObject() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Name"), Name);
	Object->SetStringField(TEXT("Schema"), Schema);
	SetOptionalString(Object, TEXT("GraphGuid"), GraphGuid);
	SetOptionalString(Object, TEXT("Category"), Category);
	SetOptionalString(Object, TEXT("Description"), Description);
	if (Signature.IsValid())
	{
		Object->SetObjectField(TEXT("Signature"), AssetDocumentGraphJson::CloneJsonObject(Signature));
	}

	TArray<FAssetDocumentNodeSpec> SortedNodes = Nodes;
	SortedNodes.Sort([](const FAssetDocumentNodeSpec& Left, const FAssetDocumentNodeSpec& Right)
	{
		return Left.Id < Right.Id;
	});

	TArray<TSharedPtr<FJsonValue>> NodeValues;
	for (const FAssetDocumentNodeSpec& Node : SortedNodes)
	{
		NodeValues.Add(MakeShared<FJsonValueObject>(Node.ToJsonObject()));
	}
	Object->SetArrayField(TEXT("Nodes"), MoveTemp(NodeValues));

	TArray<FAssetDocumentLinkSpec> SortedLinks = Links;
	SortedLinks.Sort([](const FAssetDocumentLinkSpec& Left, const FAssetDocumentLinkSpec& Right)
	{
		return Left.ToKey() < Right.ToKey();
	});

	TArray<TSharedPtr<FJsonValue>> LinkValues;
	for (const FAssetDocumentLinkSpec& Link : SortedLinks)
	{
		LinkValues.Add(MakeShared<FJsonValueObject>(Link.ToJsonObject()));
	}
	Object->SetArrayField(TEXT("Links"), MoveTemp(LinkValues));

	return Object;
}

void FAssetDocumentGraphParseResult::AddDiagnostic(const FString& Code, const FString& Path, const FString& Message)
{
	FAssetDocumentGraphDiagnostic Diagnostic;
	Diagnostic.Code = Code;
	Diagnostic.Path = Path;
	Diagnostic.Message = Message;
	Diagnostics.Add(MoveTemp(Diagnostic));
}

namespace AssetDocumentGraphJson
{
TSharedPtr<FJsonValue> CloneJsonValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return nullptr;
	}

	switch (Value->Type)
	{
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		TSharedRef<FJsonObject> Clone = MakeShared<FJsonObject>();
		if (Object.IsValid())
		{
			TArray<FString> Keys;
			Object->Values.GetKeys(Keys);
			Keys.Sort();
			for (const FString& Key : Keys)
			{
				Clone->SetField(Key, CloneJsonValue(Object->Values[Key]));
			}
		}
		return MakeShared<FJsonValueObject>(Clone);
	}
	case EJson::Array:
	{
		TArray<TSharedPtr<FJsonValue>> CloneArray;
		for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
		{
			CloneArray.Add(CloneJsonValue(Item));
		}
		return MakeShared<FJsonValueArray>(MoveTemp(CloneArray));
	}
	case EJson::String:
		return MakeShared<FJsonValueString>(Value->AsString());
	case EJson::Number:
		return MakeShared<FJsonValueNumber>(Value->AsNumber());
	case EJson::Boolean:
		return MakeShared<FJsonValueBoolean>(Value->AsBool());
	case EJson::Null:
	default:
		return MakeShared<FJsonValueNull>();
	}
}

TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Object)
{
	if (!Object.IsValid())
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> Clone = MakeShared<FJsonObject>();
	TArray<FString> Keys;
	Object->Values.GetKeys(Keys);
	Keys.Sort();
	for (const FString& Key : Keys)
	{
		Clone->SetField(Key, CloneJsonValue(Object->Values[Key]));
	}
	return Clone;
}

bool AreJsonValuesEqual(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
{
	if (!Left.IsValid() || !Right.IsValid())
	{
		return !Left.IsValid() && !Right.IsValid();
	}
	if (Left->Type != Right->Type)
	{
		return false;
	}

	switch (Left->Type)
	{
	case EJson::Object:
		return AreJsonObjectsEqual(Left->AsObject(), Right->AsObject());
	case EJson::Array:
	{
		const TArray<TSharedPtr<FJsonValue>>& LeftArray = Left->AsArray();
		const TArray<TSharedPtr<FJsonValue>>& RightArray = Right->AsArray();
		if (LeftArray.Num() != RightArray.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < LeftArray.Num(); ++Index)
		{
			if (!AreJsonValuesEqual(LeftArray[Index], RightArray[Index]))
			{
				return false;
			}
		}
		return true;
	}
	case EJson::String:
		return Left->AsString() == Right->AsString();
	case EJson::Number:
		return FMath::IsNearlyEqual(Left->AsNumber(), Right->AsNumber());
	case EJson::Boolean:
		return Left->AsBool() == Right->AsBool();
	case EJson::Null:
	default:
		return true;
	}
}

bool AreJsonObjectsEqual(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right)
{
	if (!Left.IsValid() || !Right.IsValid())
	{
		return !Left.IsValid() && !Right.IsValid();
	}
	if (Left->Values.Num() != Right->Values.Num())
	{
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Left->Values)
	{
		const TSharedPtr<FJsonValue>* RightValue = Right->Values.Find(Pair.Key);
		if (!RightValue || !AreJsonValuesEqual(Pair.Value, *RightValue))
		{
			return false;
		}
	}
	return true;
}
}
