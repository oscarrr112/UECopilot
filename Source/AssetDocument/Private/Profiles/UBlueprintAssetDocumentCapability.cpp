// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/UBlueprintAssetDocumentCapability.h"

#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
bool IsKnownBodyKey(const FString& BodyKey)
{
	for (const FName& KnownBodyKey : FUBlueprintAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (KnownBodyKey.ToString() == BodyKey)
		{
			return true;
		}
	}
	return false;
}

bool IsProtectedRegion(const FString& BodyKey)
{
	return BodyKey == TEXT("UbergraphPages")
		|| BodyKey == TEXT("FunctionGraphs")
		|| BodyKey == TEXT("MacroGraphs")
		|| BodyKey == TEXT("Timelines");
}

FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult RequireObjectValue(const TSharedRef<FJsonValue>& Value, const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
	if (Value->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body must be a JSON object"), Path, TEXT("InvalidBodyType"));
	}

	OutObject = Value->AsObject();
	if (!OutObject.IsValid())
	{
		return BodyFailure(TEXT("Body must be a JSON object"), Path, TEXT("InvalidBodyType"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RequireArrayOrNullValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, const FString& BodyKey)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (Value->Type != EJson::Array)
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s must be an array when authored"), *BodyKey),
			Path,
			TEXT("InvalidBodySectionType"));
	}

	if (Value->AsArray().Num() > 0)
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.%s is an unsupported UBlueprint graph/timeline region and cannot be non-empty yet"), *BodyKey),
			Path,
			TEXT("UnsupportedUBlueprintRegion"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ResolveParentClass(const TSharedPtr<FJsonValue>& Value, UClass*& OutParentClass)
{
	OutParentClass = nullptr;

	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body.ParentClass must be a ClassRef object"), TEXT("/Body/ParentClass"), TEXT("InvalidParentClass"));
	}

	const TSharedPtr<FJsonObject> ParentClass = Value->AsObject();
	if (!ParentClass.IsValid())
	{
		return BodyFailure(TEXT("Body.ParentClass must be a ClassRef object"), TEXT("/Body/ParentClass"), TEXT("InvalidParentClass"));
	}

	FString Kind;
	if (!ParentClass->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("ClassRef"))
	{
		return BodyFailure(TEXT("Body.ParentClass.Kind must be ClassRef"), TEXT("/Body/ParentClass/Kind"), TEXT("InvalidParentClassKind"));
	}

	FString ClassPath;
	if (!ParentClass->TryGetStringField(TEXT("Class"), ClassPath) || ClassPath.IsEmpty())
	{
		return BodyFailure(TEXT("Body.ParentClass.Class is required"), TEXT("/Body/ParentClass/Class"), TEXT("MissingParentClass"));
	}

	OutParentClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
	if (!OutParentClass)
	{
		return BodyFailure(
			FString::Printf(TEXT("Failed to resolve Body.ParentClass.Class '%s'"), *ClassPath),
			TEXT("/Body/ParentClass/Class"),
			TEXT("UnresolvedParentClass"));
	}

	if (!OutParentClass->IsChildOf(UObject::StaticClass()))
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.ParentClass.Class '%s' is not a UObject class"), *OutParentClass->GetName()),
			TEXT("/Body/ParentClass/Class"),
			TEXT("InvalidParentClass"));
	}

	if (OutParentClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.ParentClass.Class '%s' is abstract"), *OutParentClass->GetName()),
			TEXT("/Body/ParentClass/Class"),
			TEXT("AbstractParentClass"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateParentClass(const TSharedPtr<FJsonValue>& Value)
{
	UClass* ParentClass = nullptr;
	return ResolveParentClass(Value, ParentClass);
}
}

const TArray<FName>& FUBlueprintAssetDocumentCapability::GetCanonicalBodyKeys()
{
	static const TArray<FName> Keys = {
		TEXT("ParentClass"),
		TEXT("ImplementedInterfaces"),
		TEXT("Variables"),
		TEXT("Components"),
		TEXT("ClassDefaults"),
		TEXT("UbergraphPages"),
		TEXT("FunctionGraphs"),
		TEXT("MacroGraphs"),
		TEXT("Timelines"),
	};
	return Keys;
}

FName FUBlueprintAssetDocumentCapability::GetName() const
{
	return TEXT("UBlueprintBody");
}

TArray<FName> FUBlueprintAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {TEXT("UBlueprintBody"), TEXT("UBlueprintAuthoritativeRegions")};
}

int32 FUBlueprintAssetDocumentCapability::GetApplyOrder() const
{
	return 60;
}

bool FUBlueprintAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && Asset->GetClass() == UBlueprint::StaticClass();
}

bool FUBlueprintAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UBlueprint::StaticClass();
}

TSharedRef<FJsonObject> FUBlueprintAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("ParentClass"), TEXT("ClassRef"));
	Schema->SetStringField(TEXT("ImplementedInterfaces"), TEXT("array<{Interface: ClassRef}>"));
	Schema->SetStringField(TEXT("Variables"), TEXT("array<{Name, Type, DefaultValue, Flags, Category, Tooltip}>"));
	Schema->SetStringField(TEXT("Components"), TEXT("array<{Key:{Name,OwnerClass}, Scope, Class, AttachTo, Root, Properties}>"));
	Schema->SetStringField(TEXT("ClassDefaults"), TEXT("object"));
	Schema->SetStringField(TEXT("UbergraphPages"), TEXT("array unsupported until graph region implementation"));
	Schema->SetStringField(TEXT("FunctionGraphs"), TEXT("array unsupported until graph region implementation"));
	Schema->SetStringField(TEXT("MacroGraphs"), TEXT("array unsupported until graph region implementation"));
	Schema->SetStringField(TEXT("Timelines"), TEXT("array unsupported until timeline region implementation"));
	return Schema;
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = RequireObjectValue(BodyJson, TEXT("/Body"), BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}
	return ValidateBodyObject(Context, BodyObject.ToSharedRef());
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	return Validate(Context, BodyJson);
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	const FAssetDocumentCapabilityResult ValidateResult = Validate(Context, BodyJson);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		return BodyFailure(TEXT("UBlueprint body apply requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = RequireObjectValue(BodyJson, TEXT("/Body"), BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	if (const TSharedPtr<FJsonValue>* ParentClassValue = BodyObject->Values.Find(TEXT("ParentClass")))
	{
		UClass* ParentClass = nullptr;
		const FAssetDocumentCapabilityResult ParentClassResult = ResolveParentClass(*ParentClassValue, ParentClass);
		if (!ParentClassResult.bSuccess)
		{
			return ParentClassResult;
		}

		if (Blueprint->ParentClass.Get() != ParentClass)
		{
			Blueprint->Modify();
			Blueprint->ParentClass = ParentClass;
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Applied UBlueprint Body scaffold"));
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	if (Context.Asset && !SupportsAsset(Context.Asset))
	{
		return BodyFailure(TEXT("UBlueprint body extract requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}
	if (!Context.Asset && Context.AssetClass && !SupportsClass(Context.AssetClass))
	{
		return BodyFailure(TEXT("UBlueprint body extract requires exact UBlueprint class"), TEXT("/Class"), TEXT("UnsupportedClass"));
	}

	OutBodyJson->SetObjectField(TEXT("ParentClass"), MakeShared<FJsonObject>());
	OutBodyJson->SetArrayField(TEXT("ImplementedInterfaces"), {});
	OutBodyJson->SetArrayField(TEXT("Variables"), {});
	OutBodyJson->SetArrayField(TEXT("Components"), {});
	OutBodyJson->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	OutBodyJson->SetArrayField(TEXT("UbergraphPages"), {});
	OutBodyJson->SetArrayField(TEXT("FunctionGraphs"), {});
	OutBodyJson->SetArrayField(TEXT("MacroGraphs"), {});
	OutBodyJson->SetArrayField(TEXT("Timelines"), {});
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted UBlueprint Body scaffold"));
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	return Validate(Context, DesiredJson);
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::ValidateBodyObject(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject) const
{
	if (Context.Asset && !SupportsAsset(Context.Asset))
	{
		return BodyFailure(TEXT("UBlueprint body validation requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}
	if (!Context.Asset && Context.AssetClass && !SupportsClass(Context.AssetClass))
	{
		return BodyFailure(TEXT("UBlueprint body validation requires exact UBlueprint class"), TEXT("/Class"), TEXT("UnsupportedClass"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		if (!IsKnownBodyKey(Pair.Key))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unknown UBlueprint Body key '%s'"), *Pair.Key),
				FString::Printf(TEXT("/Body/%s"), *Pair.Key),
				TEXT("UnknownBodyKey"));
		}

		if (IsProtectedRegion(Pair.Key))
		{
			const FAssetDocumentCapabilityResult ProtectedResult =
				RequireArrayOrNullValue(Pair.Value, FString::Printf(TEXT("/Body/%s"), *Pair.Key), Pair.Key);
			if (!ProtectedResult.bSuccess)
			{
				return ProtectedResult;
			}
		}

		if (Pair.Key == TEXT("ParentClass"))
		{
			const FAssetDocumentCapabilityResult ParentClassResult = ValidateParentClass(Pair.Value);
			if (!ParentClassResult.bSuccess)
			{
				return ParentClassResult;
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated UBlueprint Body scaffold"));
}
