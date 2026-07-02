// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentTreeRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
constexpr const TCHAR* TreeOperationField = TEXT("Operation");

FString RegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FString AppendPath(const FString& BasePath, const FString& Token)
{
	return FString::Printf(TEXT("%s/%s"), *BasePath, *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Token));
}

FString AppendPath(const FString& BasePath, const int32 Index)
{
	return FString::Printf(TEXT("%s/%d"), *BasePath, Index);
}

bool IsObjectRegion(const FAssetDocumentRegionContext& Context)
{
	return !Context.Policy || Context.Policy->RegionKind == EAssetDocumentRegionKind::Object;
}

FAssetDocumentCapabilityResult Failure(
	const FString& Path,
	const FString& Code,
	const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(Path, Code, Message);
}

FAssetDocumentCapabilityResult MissingHookFailure(
	const FAssetDocumentRegionContext& Context,
	const FString& Code,
	const FString& Operation)
{
	return Failure(
		RegionPath(Context),
		Code,
		FString::Printf(TEXT("Tree region %s requires an explicit %s hook"), *Context.BodyPath, *Operation));
}

FAssetDocumentCapabilityResult RequireObjectField(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	const FString& Code,
	TSharedPtr<FJsonObject>& OutObject)
{
	const TSharedPtr<FJsonValue> FieldValue = Object->TryGetField(FieldName);
	if (!FieldValue.IsValid() || FieldValue->Type != EJson::Object)
	{
		return Failure(Path, Code, FString::Printf(TEXT("%s must be a JSON object"), *FieldName));
	}

	OutObject = FieldValue->AsObject();
	if (!OutObject.IsValid())
	{
		return Failure(Path, Code, FString::Printf(TEXT("%s must be a JSON object"), *FieldName));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateOptionalStringField(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& ObjectPath,
	const FString& Code)
{
	const TSharedPtr<FJsonValue> FieldValue = Object->TryGetField(FieldName);
	if (FieldValue.IsValid() && FieldValue->Type != EJson::String)
	{
		return Failure(
			AppendPath(ObjectPath, FieldName),
			Code,
			FString::Printf(TEXT("%s must be a string when present"), *FieldName));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateOptionalObjectField(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& ObjectPath,
	const FString& Code)
{
	const TSharedPtr<FJsonValue> FieldValue = Object->TryGetField(FieldName);
	if (FieldValue.IsValid() && FieldValue->Type != EJson::Object)
	{
		return Failure(
			AppendPath(ObjectPath, FieldName),
			Code,
			FString::Printf(TEXT("%s must be a JSON object when present"), *FieldName));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateOptionalNumberField(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& ObjectPath,
	const FString& Code)
{
	const TSharedPtr<FJsonValue> FieldValue = Object->TryGetField(FieldName);
	if (FieldValue.IsValid() && FieldValue->Type != EJson::Number)
	{
		return Failure(
			AppendPath(ObjectPath, FieldName),
			Code,
			FString::Printf(TEXT("%s must be a number when present"), *FieldName));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateKnownFields(
	const TSharedRef<FJsonObject>& Object,
	const FString& ObjectPath,
	const TArray<FString>& AllowedFields)
{
	TSet<FString> AllowedFieldSet;
	AllowedFieldSet.Reserve(AllowedFields.Num());
	for (const FString& FieldName : AllowedFields)
	{
		AllowedFieldSet.Add(FieldName);
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
	{
		if (!AllowedFieldSet.Contains(Field.Key))
		{
			return Failure(
				AppendPath(ObjectPath, Field.Key),
				TEXT("UnknownField"),
				FString::Printf(TEXT("Unknown tree field %s"), *Field.Key));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RequireArrayField(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	const FString& Code,
	TArray<TSharedPtr<FJsonValue>>& OutArray)
{
	const TSharedPtr<FJsonValue> FieldValue = Object->TryGetField(FieldName);
	if (!FieldValue.IsValid() || FieldValue->Type != EJson::Array)
	{
		return Failure(Path, Code, FString::Printf(TEXT("%s must be a JSON array"), *FieldName));
	}

	OutArray = FieldValue->AsArray();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateOptionalArrayField(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	const FString& Code,
	TArray<TSharedPtr<FJsonValue>>& OutArray)
{
	const TSharedPtr<FJsonValue> FieldValue = Object->TryGetField(FieldName);
	if (!FieldValue.IsValid())
	{
		OutArray.Reset();
		return FAssetDocumentCapabilityResult::Success();
	}
	return RequireArrayField(Object, FieldName, Path, Code, OutArray);
}

FAssetDocumentCapabilityResult ReadIdentity(
	const FAssetDocumentTreeRegionAdapterConfig& Config,
	const TSharedRef<FJsonObject>& Object,
	const FString& ObjectPath,
	FString& OutId)
{
	return FAssetDocumentJsonRegionUtils::RequireStringField(
		Object,
		Config.IdField,
		AppendPath(ObjectPath, Config.IdField),
		OutId,
		TEXT("MissingTreeNodeId"));
}

FAssetDocumentCapabilityResult AddIdentity(
	const FString& Id,
	const FString& SemanticPath,
	const FString& DuplicateDiagnosticPath,
	TMap<FString, FString>& OutSemanticPaths)
{
	if (OutSemanticPaths.Contains(Id))
	{
		return Failure(
			DuplicateDiagnosticPath,
			TEXT("DuplicateTreeNodeId"),
			FString::Printf(TEXT("Duplicate tree node id %s"), *Id));
	}

	OutSemanticPaths.Add(Id, SemanticPath);
	return FAssetDocumentCapabilityResult::Success();
}

FString MakeConfiguredChildEdgePath(
	const FAssetDocumentTreeRegionAdapterConfig& Config,
	const FAssetDocumentRegionContext& Context,
	const FString& ParentId,
	const FString& ChildId)
{
	return AppendPath(AppendPath(FAssetDocumentTreeRegionAdapter::MakeNodePath(Context, ParentId), Config.ChildrenField), ChildId);
}

FString MakeConfiguredDecoratorPath(
	const FAssetDocumentTreeRegionAdapterConfig& Config,
	const FAssetDocumentRegionContext& Context,
	const FString& ParentId,
	const FString& ChildId,
	const FString& DecoratorId)
{
	return AppendPath(AppendPath(MakeConfiguredChildEdgePath(Config, Context, ParentId, ChildId), Config.DecoratorsField), DecoratorId);
}

FString MakeConfiguredServicePath(
	const FAssetDocumentTreeRegionAdapterConfig& Config,
	const FAssetDocumentRegionContext& Context,
	const FString& OwnerId,
	const FString& ServiceId)
{
	return AppendPath(AppendPath(FAssetDocumentTreeRegionAdapter::MakeNodePath(Context, OwnerId), Config.ServicesField), ServiceId);
}

bool IsSupportedDecoratorLogicOperation(const FString& Operation)
{
	return Operation == TEXT("Test")
		|| Operation == TEXT("And")
		|| Operation == TEXT("Or")
		|| Operation == TEXT("Not");
}

FAssetDocumentCapabilityResult ValidateDecoratorLogicArray(
	const FAssetDocumentTreeRegionAdapterConfig& Config,
	const TSharedRef<FJsonObject>& OwnerObject,
	const FString& OwnerAuthoredPath,
	const FString& LogicFieldName)
{
	TArray<TSharedPtr<FJsonValue>> LogicValues;
	const FString LogicPath = AppendPath(OwnerAuthoredPath, LogicFieldName);
	FAssetDocumentCapabilityResult Result = ValidateOptionalArrayField(
		OwnerObject,
		LogicFieldName,
		LogicPath,
		TEXT("InvalidTreeDecoratorLogic"),
		LogicValues);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (int32 Index = 0; Index < LogicValues.Num(); ++Index)
	{
		const FString EntryPath = AppendPath(LogicPath, Index);
		TSharedPtr<FJsonObject> LogicObject;
		Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(LogicValues[Index], EntryPath, LogicObject);
		if (!Result.bSuccess)
		{
			return Failure(EntryPath, TEXT("InvalidTreeDecoratorLogic"), TEXT("Decorator logic entries must be objects"));
		}

		Result = ValidateKnownFields(LogicObject.ToSharedRef(), EntryPath, {TreeOperationField, Config.DecoratorLogicNumberField});
		if (!Result.bSuccess)
		{
			return Result;
		}

		Result = ValidateOptionalNumberField(
			LogicObject.ToSharedRef(),
			Config.DecoratorLogicNumberField,
			EntryPath,
			TEXT("InvalidTreeDecoratorLogicNumber"));
		if (!Result.bSuccess)
		{
			return Result;
		}

		FString Operation;
		Result = FAssetDocumentJsonRegionUtils::RequireStringField(
			LogicObject,
			TreeOperationField,
			AppendPath(EntryPath, TreeOperationField),
			Operation,
			TEXT("InvalidTreeDecoratorLogicOperation"));
		if (!Result.bSuccess)
		{
			return Result;
		}

		if (!IsSupportedDecoratorLogicOperation(Operation))
		{
			return Failure(
				AppendPath(EntryPath, TreeOperationField),
				TEXT("InvalidTreeDecoratorLogicOperation"),
				FString::Printf(TEXT("Unsupported decorator logic operation %s"), *Operation));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateIdentityArray(
	const FAssetDocumentTreeRegionAdapterConfig& Config,
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& OwnerObject,
	const FString& FieldName,
	const FString& FieldPath,
	const FString& InvalidArrayCode,
	const TFunction<FString(const FString&)>& MakeSemanticPath,
	TMap<FString, FString>& OutSemanticPaths)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	FAssetDocumentCapabilityResult Result =
		ValidateOptionalArrayField(OwnerObject, FieldName, FieldPath, InvalidArrayCode, Values);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const FString EntryPath = AppendPath(FieldPath, Index);
		TSharedPtr<FJsonObject> EntryObject;
		Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Values[Index], EntryPath, EntryObject);
		if (!Result.bSuccess)
		{
			return Failure(EntryPath, InvalidArrayCode, FString::Printf(TEXT("%s entries must be objects"), *FieldName));
		}

		Result = ValidateKnownFields(EntryObject.ToSharedRef(), EntryPath, {Config.IdField, Config.ClassField, Config.PropertiesField});
		if (!Result.bSuccess)
		{
			return Result;
		}

		FString EntryId;
		Result = ReadIdentity(Config, EntryObject.ToSharedRef(), EntryPath, EntryId);
		if (!Result.bSuccess)
		{
			return Result;
		}

		Result = ValidateOptionalStringField(EntryObject.ToSharedRef(), Config.ClassField, EntryPath, TEXT("InvalidTreeNodeClass"));
		if (!Result.bSuccess)
		{
			return Result;
		}

		Result = ValidateOptionalObjectField(EntryObject.ToSharedRef(), Config.PropertiesField, EntryPath, TEXT("InvalidTreeProperties"));
		if (!Result.bSuccess)
		{
			return Result;
		}

		Result = AddIdentity(
			EntryId,
			MakeSemanticPath(EntryId),
			AppendPath(EntryPath, Config.IdField),
			OutSemanticPaths);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateNode(
	const FAssetDocumentTreeRegionAdapterConfig& Config,
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& NodeObject,
	const FString& NodeJsonPath,
	TMap<FString, FString>& OutSemanticPaths,
	FString& OutNodeId)
{
	FAssetDocumentCapabilityResult Result = ReadIdentity(Config, NodeObject, NodeJsonPath, OutNodeId);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateKnownFields(
		NodeObject,
		NodeJsonPath,
		{
			Config.IdField,
			Config.ClassField,
			Config.PropertiesField,
			Config.ChildrenField,
			Config.ServicesField,
			Config.DecoratorsField,
			Config.DecoratorLogicField,
		});
	if (!Result.bSuccess)
	{
		return Result;
	}

	const FString NodeSemanticPath = FAssetDocumentTreeRegionAdapter::MakeNodePath(Context, OutNodeId);
	Result = ValidateOptionalStringField(NodeObject, Config.ClassField, NodeJsonPath, TEXT("InvalidTreeNodeClass"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateOptionalObjectField(NodeObject, Config.PropertiesField, NodeJsonPath, TEXT("InvalidTreeProperties"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = AddIdentity(
		OutNodeId,
		NodeSemanticPath,
		AppendPath(NodeJsonPath, Config.IdField),
		OutSemanticPaths);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateIdentityArray(
		Config,
		Context,
		NodeObject,
		Config.ServicesField,
		AppendPath(NodeJsonPath, Config.ServicesField),
		TEXT("InvalidTreeServices"),
		[&Config, &Context, &OutNodeId](const FString& ServiceId)
		{
			return MakeConfiguredServicePath(Config, Context, OutNodeId, ServiceId);
		},
		OutSemanticPaths);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateIdentityArray(
		Config,
		Context,
		NodeObject,
		Config.DecoratorsField,
		AppendPath(NodeJsonPath, Config.DecoratorsField),
		TEXT("InvalidTreeDecorators"),
		[&Config, &NodeSemanticPath](const FString& DecoratorId)
		{
			return AppendPath(AppendPath(NodeSemanticPath, Config.DecoratorsField), DecoratorId);
		},
		OutSemanticPaths);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateDecoratorLogicArray(Config, NodeObject, NodeJsonPath, Config.DecoratorLogicField);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> Children;
	Result = ValidateOptionalArrayField(
		NodeObject,
		Config.ChildrenField,
		AppendPath(NodeJsonPath, Config.ChildrenField),
		TEXT("InvalidTreeChildren"),
		Children);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const FString EdgePathBeforeChildId = AppendPath(AppendPath(NodeJsonPath, Config.ChildrenField), Index);
		TSharedPtr<FJsonObject> EdgeObject;
		Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Children[Index], EdgePathBeforeChildId, EdgeObject);
		if (!Result.bSuccess)
		{
			return Failure(EdgePathBeforeChildId, TEXT("InvalidTreeChildEdge"), TEXT("Tree child edge entries must be objects"));
		}

		Result = ValidateKnownFields(
			EdgeObject.ToSharedRef(),
			EdgePathBeforeChildId,
			{Config.ChildField, Config.DecoratorsField, Config.DecoratorLogicField});
		if (!Result.bSuccess)
		{
			return Result;
		}

		TSharedPtr<FJsonObject> ChildObject;
		Result = RequireObjectField(
			EdgeObject.ToSharedRef(),
			Config.ChildField,
			AppendPath(EdgePathBeforeChildId, Config.ChildField),
			TEXT("InvalidTreeChildEdge"),
			ChildObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		FString ChildId;
		Result = ValidateNode(
			Config,
			Context,
			ChildObject.ToSharedRef(),
			AppendPath(EdgePathBeforeChildId, Config.ChildField),
			OutSemanticPaths,
			ChildId);
		if (!Result.bSuccess)
		{
			return Result;
		}

		const FString EdgeSemanticPath = MakeConfiguredChildEdgePath(Config, Context, OutNodeId, ChildId);
		Result = ValidateIdentityArray(
			Config,
			Context,
			EdgeObject.ToSharedRef(),
			Config.DecoratorsField,
			AppendPath(EdgePathBeforeChildId, Config.DecoratorsField),
			TEXT("InvalidTreeDecorators"),
			[&Config, &Context, &OutNodeId, &ChildId](const FString& DecoratorId)
			{
				return MakeConfiguredDecoratorPath(Config, Context, OutNodeId, ChildId, DecoratorId);
			},
			OutSemanticPaths);
		if (!Result.bSuccess)
		{
			return Result;
		}

		Result = ValidateDecoratorLogicArray(Config, EdgeObject.ToSharedRef(), EdgePathBeforeChildId, Config.DecoratorLogicField);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}
}

FAssetDocumentTreeRegionAdapter::FAssetDocumentTreeRegionAdapter()
	: FAssetDocumentTreeRegionAdapter(FAssetDocumentTreeRegionAdapterConfig(), {})
{
}

FAssetDocumentTreeRegionAdapter::FAssetDocumentTreeRegionAdapter(
	FAssetDocumentTreeRegionAdapterConfig InConfig,
	FAssetDocumentTreeRegionAdapterHooks InHooks)
	: Config(MoveTemp(InConfig))
	, Hooks(MoveTemp(InHooks))
{
	if (Config.Name.IsNone())
	{
		Config.Name = DefaultAdapterName();
	}
	if (Config.RootField.IsEmpty())
	{
		Config.RootField = TEXT("Root");
	}
	if (Config.IdField.IsEmpty())
	{
		Config.IdField = TEXT("Id");
	}
	if (Config.ClassField.IsEmpty())
	{
		Config.ClassField = TEXT("Class");
	}
	if (Config.ChildrenField.IsEmpty())
	{
		Config.ChildrenField = TEXT("Children");
	}
	if (Config.ChildField.IsEmpty())
	{
		Config.ChildField = TEXT("Child");
	}
	if (Config.DecoratorsField.IsEmpty())
	{
		Config.DecoratorsField = TEXT("Decorators");
	}
	if (Config.DecoratorLogicField.IsEmpty())
	{
		Config.DecoratorLogicField = TEXT("DecoratorLogic");
	}
	if (Config.RootDecoratorsField.IsEmpty())
	{
		Config.RootDecoratorsField = TEXT("RootDecorators");
	}
	if (Config.RootDecoratorLogicField.IsEmpty())
	{
		Config.RootDecoratorLogicField = TEXT("RootDecoratorLogic");
	}
	if (Config.ServicesField.IsEmpty())
	{
		Config.ServicesField = TEXT("Services");
	}
	if (Config.PropertiesField.IsEmpty())
	{
		Config.PropertiesField = TEXT("Properties");
	}
	if (Config.DecoratorLogicNumberField.IsEmpty())
	{
		Config.DecoratorLogicNumberField = TEXT("Number");
	}
}

FName FAssetDocumentTreeRegionAdapter::DefaultAdapterName()
{
	return TEXT("AssetDocumentTreeRegionAdapter");
}

FString FAssetDocumentTreeRegionAdapter::MakeNodePath(
	const FAssetDocumentRegionContext& Context,
	const FString& NodeId)
{
	return AppendPath(RegionPath(Context), NodeId);
}

FString FAssetDocumentTreeRegionAdapter::MakeChildEdgePath(
	const FAssetDocumentRegionContext& Context,
	const FString& ParentId,
	const FString& ChildId) const
{
	return MakeConfiguredChildEdgePath(Config, Context, ParentId, ChildId);
}

FString FAssetDocumentTreeRegionAdapter::MakeDecoratorPath(
	const FAssetDocumentRegionContext& Context,
	const FString& ParentId,
	const FString& ChildId,
	const FString& DecoratorId) const
{
	return MakeConfiguredDecoratorPath(Config, Context, ParentId, ChildId, DecoratorId);
}

FString FAssetDocumentTreeRegionAdapter::MakeServicePath(
	const FAssetDocumentRegionContext& Context,
	const FString& OwnerId,
	const FString& ServiceId) const
{
	return MakeConfiguredServicePath(Config, Context, OwnerId, ServiceId);
}

FName FAssetDocumentTreeRegionAdapter::GetName() const
{
	return Config.Name;
}

bool FAssetDocumentTreeRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return IsObjectRegion(Context);
}

TSharedRef<FJsonObject> FAssetDocumentTreeRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext& Context) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Adapter"), GetName().ToString());
	Schema->SetStringField(TEXT("Shape"), TEXT("object<tree>"));
	Schema->SetStringField(TEXT("BodyPath"), Context.BodyPath);
	Schema->SetStringField(TEXT("RootField"), Config.RootField);
	Schema->SetStringField(TEXT("IdField"), Config.IdField);
	Schema->SetStringField(TEXT("ClassField"), Config.ClassField);
	Schema->SetStringField(TEXT("ChildrenField"), Config.ChildrenField);
	Schema->SetStringField(TEXT("ChildField"), Config.ChildField);
	Schema->SetStringField(TEXT("DecoratorsField"), Config.DecoratorsField);
	Schema->SetStringField(TEXT("DecoratorLogicField"), Config.DecoratorLogicField);
	Schema->SetStringField(TEXT("RootDecoratorsField"), Config.RootDecoratorsField);
	Schema->SetStringField(TEXT("RootDecoratorLogicField"), Config.RootDecoratorLogicField);
	Schema->SetStringField(TEXT("ServicesField"), Config.ServicesField);
	Schema->SetStringField(TEXT("PropertiesField"), Config.PropertiesField);
	Schema->SetStringField(TEXT("DecoratorLogicNumberField"), Config.DecoratorLogicNumberField);
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentTreeRegionAdapter::ParseTree(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& Value,
	TSharedPtr<FJsonObject>& OutTree) const
{
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, RegionPath(Context), OutTree);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TMap<FString, FString> SemanticPaths;
	Result = CollectSemanticPaths(Context, OutTree.ToSharedRef(), SemanticPaths);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Parsed tree region"));
}

FAssetDocumentCapabilityResult FAssetDocumentTreeRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	TSharedPtr<FJsonObject> Tree;
	FAssetDocumentCapabilityResult Result = ParseTree(Context, DesiredValue, Tree);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Hooks.ValidateTree)
	{
		return Hooks.ValidateTree(Context, Tree.ToSharedRef());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated tree region"));
}

FAssetDocumentCapabilityResult FAssetDocumentTreeRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;

	TSharedPtr<FJsonObject> Tree;
	FAssetDocumentCapabilityResult Result = ParseTree(Context, DesiredValue, Tree);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Hooks.ValidateTree)
	{
		Result = Hooks.ValidateTree(Context, Tree.ToSharedRef());
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (Hooks.ApplyTree)
	{
		return Hooks.ApplyTree(Context, Tree.ToSharedRef(), bOutChanged);
	}

	return MissingHookFailure(Context, TEXT("MissingRegionApplyHook"), TEXT("apply"));
}

FAssetDocumentCapabilityResult FAssetDocumentTreeRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	OutCurrentValue.Reset();
	if (!Hooks.ExtractTree)
	{
		return MissingHookFailure(Context, TEXT("MissingRegionExtractHook"), TEXT("extract"));
	}

	TSharedRef<FJsonObject> CurrentTree = MakeShared<FJsonObject>();
	FAssetDocumentCapabilityResult Result = Hooks.ExtractTree(Context, CurrentTree);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TMap<FString, FString> SemanticPaths;
	Result = CollectSemanticPaths(Context, CurrentTree, SemanticPaths);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutCurrentValue = MakeShared<FJsonValueObject>(CurrentTree);
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted tree region"));
}

FAssetDocumentCapabilityResult FAssetDocumentTreeRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TSharedPtr<FJsonObject> Tree;
	FAssetDocumentCapabilityResult Result = ParseTree(Context, DesiredValue, Tree);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Hooks.ValidateTree)
	{
		Result = Hooks.ValidateTree(Context, Tree.ToSharedRef());
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (Hooks.DiffTree)
	{
		return Hooks.DiffTree(Context, Tree.ToSharedRef(), OutDiffEntries);
	}

	return MissingHookFailure(Context, TEXT("MissingRegionDiffHook"), TEXT("diff"));
}

FAssetDocumentCapabilityResult FAssetDocumentTreeRegionAdapter::CollectSemanticPaths(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Tree,
	TMap<FString, FString>& OutSemanticPaths) const
{
	OutSemanticPaths.Reset();

	TSharedPtr<FJsonObject> RootObject;
	FAssetDocumentCapabilityResult Result = RequireObjectField(
		Tree,
		Config.RootField,
		AppendPath(RegionPath(Context), Config.RootField),
		TEXT("MissingTreeRoot"),
		RootObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateKnownFields(Tree, RegionPath(Context), {Config.RootField, Config.RootDecoratorsField, Config.RootDecoratorLogicField});
	if (!Result.bSuccess)
	{
		return Result;
	}

	FString RootId;
	Result = ValidateNode(
		Config,
		Context,
		RootObject.ToSharedRef(),
		AppendPath(RegionPath(Context), Config.RootField),
		OutSemanticPaths,
		RootId);
	if (!Result.bSuccess)
	{
		return Result;
	}

	const FString RootDecoratorsSemanticBasePath = AppendPath(RegionPath(Context), Config.RootDecoratorsField);
	Result = ValidateIdentityArray(
		Config,
		Context,
		Tree,
		Config.RootDecoratorsField,
		AppendPath(RegionPath(Context), Config.RootDecoratorsField),
		TEXT("InvalidTreeDecorators"),
		[&RootDecoratorsSemanticBasePath](const FString& DecoratorId)
		{
			return AppendPath(RootDecoratorsSemanticBasePath, DecoratorId);
		},
		OutSemanticPaths);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return ValidateDecoratorLogicArray(Config, Tree, RegionPath(Context), Config.RootDecoratorLogicField);
}
