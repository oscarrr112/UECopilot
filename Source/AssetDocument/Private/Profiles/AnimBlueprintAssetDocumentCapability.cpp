// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimBlueprintAssetDocumentCapability.h"

#include "Animation/AnimBlueprint.h"
#include "Dom/JsonValue.h"
#include "Regions/AssetDocumentDeferredRegionAdapter.h"

namespace
{
bool IsKnownBodyKey(const FString& BodyKey)
{
	for (const FName& KnownBodyKey : FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (KnownBodyKey.ToString() == BodyKey)
		{
			return true;
		}
	}
	return false;
}

bool IsDeferredGraphFamilyKey(const FString& BodyKey)
{
	return BodyKey == TEXT("AnimGraph")
		|| BodyKey == TEXT("StateMachines")
		|| BodyKey == TEXT("TransitionGraphs")
		|| BodyKey == TEXT("AnimLayers")
		|| BodyKey == TEXT("ParentAssetOverrides");
}

bool IsEmptyDeferredValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return true;
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

FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult RequireBodyObject(const TSharedRef<FJsonValue>& BodyJson, TSharedPtr<FJsonObject>& OutBodyObject)
{
	if (BodyJson->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	OutBodyObject = BodyJson->AsObject();
	if (!OutBodyObject.IsValid())
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	return FAssetDocumentCapabilityResult::Success();
}
}

const TArray<FName>& FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys()
{
	static const TArray<FName> Keys = {
		TEXT("ParentClass"),
		TEXT("TargetSkeleton"),
		TEXT("Template"),
		TEXT("Preview"),
		TEXT("Optimization"),
		TEXT("SyncGroups"),
		TEXT("ImplementedInterfaces"),
		TEXT("Variables"),
		TEXT("ClassDefaults"),
		TEXT("UbergraphPages"),
		TEXT("AnimGraph"),
		TEXT("StateMachines"),
		TEXT("TransitionGraphs"),
		TEXT("AnimLayers"),
		TEXT("ParentAssetOverrides"),
	};
	return Keys;
}

FName FAnimBlueprintAssetDocumentCapability::GetName() const
{
	return TEXT("AnimBlueprintBody");
}

TArray<FName> FAnimBlueprintAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {
		GetName(),
		TEXT("AnimBlueprintObjectRegionAdapter"),
		TEXT("AnimBlueprintSyncGroupsNamedArrayRegionAdapter"),
		TEXT("AnimBlueprintBlueprintCommonAdapter"),
		TEXT("AnimBlueprintDeferredRegionAdapter"),
		FAssetDocumentDeferredRegionAdapter::DefaultAdapterName(),
	};
}

int32 FAnimBlueprintAssetDocumentCapability::GetApplyOrder() const
{
	return 60;
}

bool FAnimBlueprintAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && Asset->GetClass() == UAnimBlueprint::StaticClass();
}

bool FAnimBlueprintAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UAnimBlueprint::StaticClass();
}

TSharedRef<FJsonObject> FAnimBlueprintAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("ParentClass"), TEXT("ClassRef<UAnimInstance>"));
	Schema->SetStringField(TEXT("TargetSkeleton"), TEXT("AssetRef<USkeleton>|null"));
	Schema->SetStringField(TEXT("Template"), TEXT("object {bIsTemplate:bool}"));
	Schema->SetStringField(TEXT("Preview"), TEXT("object {PreviewSkeletalMesh, PreviewAnimationBlueprint, PreviewAnimationBlueprintApplicationMethod, PreviewAnimationBlueprintTag}"));
	Schema->SetStringField(TEXT("Optimization"), TEXT("object {bUseMultiThreadedAnimationUpdate, bWarnAboutBlueprintUsage, bEnableLinkedAnimLayerInstanceSharing}"));
	Schema->SetStringField(TEXT("SyncGroups"), TEXT("array of FAnimGroupInfo entries keyed by Name"));
	Schema->SetStringField(TEXT("ImplementedInterfaces"), TEXT("Blueprint common array region"));
	Schema->SetStringField(TEXT("Variables"), TEXT("Blueprint common identity-array region"));
	Schema->SetStringField(TEXT("ClassDefaults"), TEXT("Blueprint common generated CDO default-diff object"));
	Schema->SetStringField(TEXT("UbergraphPages"), TEXT("Blueprint common K2 graph wrapper region"));
	Schema->SetStringField(TEXT("AnimGraph"), TEXT("deferred empty graph region until animation graph adapter lands"));
	Schema->SetStringField(TEXT("StateMachines"), TEXT("deferred empty graph/tree region until state-machine adapter lands"));
	Schema->SetStringField(TEXT("TransitionGraphs"), TEXT("deferred empty graph region until transition graph adapter lands"));
	Schema->SetStringField(TEXT("AnimLayers"), TEXT("deferred empty graph/array region until anim layer adapter lands"));
	Schema->SetStringField(TEXT("ParentAssetOverrides"), TEXT("deferred empty identity-array region until AnimGraph identity lands"));
	return Schema;
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	if (Context.Asset && !SupportsAsset(Context.Asset))
	{
		return BodyFailure(TEXT("AnimBlueprint body validation requires exact UAnimBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}
	if (!Context.Asset && Context.AssetClass && !SupportsClass(Context.AssetClass))
	{
		return BodyFailure(TEXT("AnimBlueprint body validation requires exact UAnimBlueprint class"), TEXT("/Class"), TEXT("UnsupportedClass"));
	}

	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = RequireBodyObject(BodyJson, BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		if (!IsKnownBodyKey(Pair.Key))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unknown AnimBlueprint Body key '%s'"), *Pair.Key),
				FString::Printf(TEXT("/Body/%s"), *Pair.Key),
				TEXT("UnknownBodyKey"));
		}

		if (IsDeferredGraphFamilyKey(Pair.Key) && !IsEmptyDeferredValue(Pair.Value))
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.%s is deferred until the corresponding public adapter lands"), *Pair.Key),
				FString::Printf(TEXT("/Body/%s"), *Pair.Key),
				TEXT("UnsupportedAnimBlueprintRegion"));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint Body scaffold"));
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	return Validate(Context, BodyJson);
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	return Validate(Context, BodyJson);
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext&, TSharedRef<FJsonObject>& OutBodyJson) const
{
	for (const FName& BodyKey : GetCanonicalBodyKeys())
	{
		const FString BodyKeyString = BodyKey.ToString();
		if (BodyKey == TEXT("ParentClass") || BodyKey == TEXT("Template") || BodyKey == TEXT("Preview") || BodyKey == TEXT("Optimization") || BodyKey == TEXT("ClassDefaults"))
		{
			OutBodyJson->SetObjectField(BodyKeyString, MakeShared<FJsonObject>());
		}
		else if (BodyKey == TEXT("TargetSkeleton"))
		{
			OutBodyJson->SetField(BodyKeyString, MakeShared<FJsonValueNull>());
		}
		else
		{
			OutBodyJson->SetArrayField(BodyKeyString, TArray<TSharedPtr<FJsonValue>>());
		}
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimBlueprint Body scaffold"));
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>&) const
{
	return Validate(Context, DesiredJson);
}
