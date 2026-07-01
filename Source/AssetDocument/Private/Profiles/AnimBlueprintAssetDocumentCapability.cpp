// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimBlueprintAssetDocumentCapability.h"

#include "AssetDocumentBodyRegionDispatcher.h"
#include "AssetDocumentFragmentCompiler.h"
#include "Profiles/AnimBlueprintAssetDocumentProfile.h"
#include "Profiles/BlueprintAssetDocumentCommon.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Regions/AssetDocumentDeferredRegionAdapter.h"
#include "Regions/AssetDocumentAnimGraphRegionAdapter.h"
#include "Regions/AssetDocumentNamedArrayRegionAdapter.h"
#include "Regions/AssetDocumentObjectFieldSchemaUtils.h"
#include "Regions/AssetDocumentObjectRegionAdapter.h"

namespace
{
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

bool IsDeferredGraphFamilyKey(const FString& BodyKey)
{
	return BodyKey == TEXT("AnimGraph")
		|| BodyKey == TEXT("StateMachines")
		|| BodyKey == TEXT("TransitionGraphs")
		|| BodyKey == TEXT("AnimLayers")
		|| BodyKey == TEXT("ParentAssetOverrides");
}

bool IsScalarDeferredValue(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid()
		&& Value->Type != EJson::Null
		&& Value->Type != EJson::Array
		&& Value->Type != EJson::Object;
}

FAssetDocumentCapabilityResult NormalizeDeferredGraphFamilyCompatibility(
	const TSharedRef<FJsonValue>& BodyJson,
	TSharedPtr<FJsonValue>& OutBodyJson)
{
	OutBodyJson = BodyJson;

	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult BodyResult = RequireBodyObject(BodyJson, BodyObject);
	if (!BodyResult.bSuccess)
	{
		return BodyResult;
	}

	bool bNeedsNormalization = false;
	TSharedRef<FJsonObject> NormalizedBodyObject = MakeShared<FJsonObject>(*BodyObject);
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		if (!IsDeferredGraphFamilyKey(Pair.Key))
		{
			continue;
		}

		if (IsScalarDeferredValue(Pair.Value))
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.%s is deferred until the corresponding public adapter lands"), *Pair.Key),
				FString::Printf(TEXT("/Body/%s"), *Pair.Key),
				TEXT("UnsupportedAnimBlueprintRegion"));
		}
		if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> DeferredObject = Pair.Value->AsObject();
			if (DeferredObject.IsValid() && DeferredObject->Values.Num() == 0)
			{
				NormalizedBodyObject->SetArrayField(Pair.Key, TArray<TSharedPtr<FJsonValue>>());
				bNeedsNormalization = true;
				continue;
			}

			return BodyFailure(
				FString::Printf(TEXT("Body.%s is deferred until the corresponding public adapter lands"), *Pair.Key),
				FString::Printf(TEXT("/Body/%s"), *Pair.Key),
				TEXT("UnsupportedAnimBlueprintRegion"));
		}
	}

	if (bNeedsNormalization)
	{
		OutBodyJson = MakeShared<FJsonValueObject>(NormalizedBodyObject);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RemapDispatcherCompatibilityCodes(FAssetDocumentCapabilityResult Result)
{
	if (Result.bSuccess)
	{
		return Result;
	}

	for (FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Code == TEXT("UnknownBodyRegion"))
		{
			Diagnostic.Code = TEXT("UnknownBodyKey");
		}
	}

	return Result;
}

FAssetDocumentCapabilityResult ValidateParentClassObject(const FAssetDocumentRegionContext&, const TSharedRef<FJsonObject>& ParentClass)
{
	FString Kind;
	if (!ParentClass->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("ClassRef"))
	{
		return BodyFailure(TEXT("Body.ParentClass.Kind must be ClassRef"), TEXT("/Body/ParentClass/Kind"), TEXT("InvalidParentClassKind"));
	}

	FString ClassPath;
	if (!ParentClass->TryGetStringField(TEXT("Class"), ClassPath) || ClassPath.TrimStartAndEnd().IsEmpty())
	{
		return BodyFailure(TEXT("Body.ParentClass.Class is required"), TEXT("/Body/ParentClass/Class"), TEXT("MissingParentClass"));
	}

	UClass* ParentClassObject = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
	if (!ParentClassObject)
	{
		return BodyFailure(
			FString::Printf(TEXT("Failed to resolve Body.ParentClass.Class '%s'"), *ClassPath),
			TEXT("/Body/ParentClass/Class"),
			TEXT("UnresolvedParentClass"));
	}

	if (!ParentClassObject->IsChildOf(UAnimInstance::StaticClass()))
	{
		return BodyFailure(
			FString::Printf(TEXT("Body.ParentClass.Class '%s' must inherit from UAnimInstance"), *ParentClassObject->GetName()),
			TEXT("/Body/ParentClass/Class"),
			TEXT("InvalidAnimBlueprintParentClass"));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint ParentClass"));
}

FAssetDocumentCapabilityResult ResolveParentClassObject(const TSharedRef<FJsonObject>& ParentClass, UClass*& OutParentClass)
{
	OutParentClass = nullptr;
	const FAssetDocumentCapabilityResult ValidateResult = ValidateParentClassObject(FAssetDocumentRegionContext(), ParentClass);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	FString ClassPath;
	ParentClass->TryGetStringField(TEXT("Class"), ClassPath);
	OutParentClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
	return FAssetDocumentCapabilityResult::Success(TEXT("Resolved AnimBlueprint ParentClass"));
}

FAssetDocumentCapabilityResult ResolveAssetRefValue(
	const TSharedPtr<FJsonValue>& Value,
	UClass* ExpectedClass,
	const FString& Path,
	UObject*& OutObject)
{
	OutObject = nullptr;
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("AssetRef omitted"));
	}
	if (Value->Type != EJson::Object)
	{
		return BodyFailure(FString::Printf(TEXT("%s must be an AssetRef object or null"), *Path), Path, TEXT("InvalidAssetRef"));
	}

	const TSharedPtr<FJsonObject> AssetRef = Value->AsObject();
	if (!AssetRef.IsValid())
	{
		return BodyFailure(FString::Printf(TEXT("%s must be an AssetRef object or null"), *Path), Path, TEXT("InvalidAssetRef"));
	}

	FString Kind;
	if (!AssetRef->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("AssetRef"))
	{
		return BodyFailure(FString::Printf(TEXT("%s.Kind must be AssetRef"), *Path), Path / TEXT("Kind"), TEXT("InvalidAssetRefKind"));
	}

	FString AssetPath;
	if (!AssetRef->TryGetStringField(TEXT("Path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
	{
		if (!AssetRef->TryGetStringField(TEXT("Asset"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
		{
			return BodyFailure(FString::Printf(TEXT("%s.Path is required"), *Path), Path / TEXT("Path"), TEXT("MissingAssetRefPath"));
		}
	}

	OutObject = StaticLoadObject(ExpectedClass, nullptr, *AssetPath);
	if (!OutObject || !OutObject->IsA(ExpectedClass))
	{
		return BodyFailure(
			FString::Printf(TEXT("%s '%s' did not resolve to %s"), *Path, *AssetPath, ExpectedClass ? *ExpectedClass->GetName() : TEXT("object")),
			Path,
			TEXT("UnresolvedAssetRef"));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Resolved AssetRef"));
}

TSharedPtr<FJsonValue> MakeAssetRefValue(const UObject* Object)
{
	if (!Object)
	{
		return MakeShared<FJsonValueNull>();
	}

	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), Object->GetPathName());
	return MakeShared<FJsonValueObject>(AssetRef);
}

TSharedRef<FJsonObject> MakeClassRefObject(const UClass* Class)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), Class ? Class->GetPathName() : FString(TEXT("/Script/Engine.AnimInstance")));
	return ClassRef;
}

bool TryReadOptionalBool(const TSharedRef<FJsonObject>& Object, const TCHAR* FieldName, bool& OutValue)
{
	return Object->TryGetBoolField(FieldName, OutValue);
}

EPreviewAnimationBlueprintApplicationMethod ParsePreviewApplicationMethod(const FString& Method)
{
	return Method == TEXT("LinkedAnimGraph")
		? EPreviewAnimationBlueprintApplicationMethod::LinkedAnimGraph
		: EPreviewAnimationBlueprintApplicationMethod::LinkedLayers;
}

FString PreviewApplicationMethodToString(EPreviewAnimationBlueprintApplicationMethod Method)
{
	return Method == EPreviewAnimationBlueprintApplicationMethod::LinkedAnimGraph
		? FString(TEXT("LinkedAnimGraph"))
		: FString(TEXT("LinkedLayers"));
}

void SyncGeneratedClassTargetSkeleton(UAnimBlueprint* AnimBlueprint)
{
	if (!AnimBlueprint)
	{
		return;
	}
	if (UAnimBlueprintGeneratedClass* GeneratedClass = Cast<UAnimBlueprintGeneratedClass>(AnimBlueprint->GeneratedClass))
	{
		GeneratedClass->TargetSkeleton = AnimBlueprint->TargetSkeleton;
	}
	if (UAnimBlueprintGeneratedClass* SkeletonClass = Cast<UAnimBlueprintGeneratedClass>(AnimBlueprint->SkeletonGeneratedClass))
	{
		SkeletonClass->TargetSkeleton = AnimBlueprint->TargetSkeleton;
	}
}

FString NormalizeSyncGroupIdentity(const FString& Identity)
{
	return FName(*Identity).ToString().ToLower();
}

FString SyncGroupPath(const FString& Name)
{
	return FString::Printf(
		TEXT("/Body/SyncGroups/%s"),
		*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Name));
}

FAssetDocumentCapabilityResult ValidateSyncGroupElement(
	const FAssetDocumentRegionContext&,
	const TSharedRef<FJsonObject>& Element,
	int32 Index)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Element->Values)
	{
		if (Pair.Key == TEXT("Name"))
		{
			continue;
		}
		if (Pair.Key == TEXT("Color"))
		{
			return BodyFailure(
				TEXT("Body.SyncGroups Color is deferred until stable color serialization is defined"),
				FString::Printf(TEXT("/Body/SyncGroups/%d/Color"), Index),
				TEXT("UnsupportedSyncGroupColor"));
		}

		return BodyFailure(
			FString::Printf(TEXT("Unknown SyncGroups field '%s'"), *Pair.Key),
			FString::Printf(TEXT("/Body/SyncGroups/%d/%s"), Index, *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Pair.Key)),
			TEXT("UnknownSyncGroupField"));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint SyncGroup"));
}

TSharedRef<FJsonObject> MakeSyncGroupObject(const FName& Name)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Name"), Name.ToString());
	return Object;
}

TMap<FString, FLinearColor> CollectExistingSyncGroupColors(const UAnimBlueprint* AnimBlueprint)
{
	TMap<FString, FLinearColor> Colors;
	if (!AnimBlueprint)
	{
		return Colors;
	}

	for (const FAnimGroupInfo& Group : AnimBlueprint->Groups)
	{
		Colors.Add(NormalizeSyncGroupIdentity(Group.Name.ToString()), Group.Color);
	}
	return Colors;
}

FAssetDocumentCapabilityResult ApplySyncGroups(
	FAssetDocumentRegionContext& Context,
	const TArray<TSharedRef<FJsonObject>>& Elements,
	bool& bOutChanged)
{
	bOutChanged = false;
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	if (!AnimBlueprint)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint SyncGroups"));
	}

	const TMap<FString, FLinearColor> ExistingColors = CollectExistingSyncGroupColors(AnimBlueprint);
	TArray<FAnimGroupInfo> DesiredGroups;
	DesiredGroups.Reserve(Elements.Num());
	for (const TSharedRef<FJsonObject>& Element : Elements)
	{
		FString Name;
		Element->TryGetStringField(TEXT("Name"), Name);

		FAnimGroupInfo Group;
		Group.Name = FName(*Name);
		if (const FLinearColor* ExistingColor = ExistingColors.Find(NormalizeSyncGroupIdentity(Name)))
		{
			Group.Color = *ExistingColor;
		}
		DesiredGroups.Add(Group);
	}

	if (AnimBlueprint->Groups.Num() == DesiredGroups.Num())
	{
		bool bSame = true;
		for (int32 Index = 0; Index < DesiredGroups.Num(); ++Index)
		{
			if (AnimBlueprint->Groups[Index].Name != DesiredGroups[Index].Name)
			{
				bSame = false;
				break;
			}
		}
		if (bSame)
		{
			return FAssetDocumentCapabilityResult::Success(TEXT("AnimBlueprint SyncGroups already match"));
		}
	}

	AnimBlueprint->Groups = MoveTemp(DesiredGroups);
	bOutChanged = true;
	return FAssetDocumentCapabilityResult::Success(TEXT("Applied AnimBlueprint SyncGroups"));
}

FAssetDocumentCapabilityResult ExtractSyncGroups(
	const FAssetDocumentRegionContext& Context,
	TArray<TSharedRef<FJsonObject>>& OutElements)
{
	OutElements.Reset();
	const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	if (!AnimBlueprint)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted empty AnimBlueprint SyncGroups"));
	}

	for (const FAnimGroupInfo& Group : AnimBlueprint->Groups)
	{
		OutElements.Add(MakeSyncGroupObject(Group.Name));
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimBlueprint SyncGroups"));
}

FAssetDocumentCapabilityResult DiffSyncGroups(
	const FAssetDocumentRegionContext& Context,
	const TArray<TSharedRef<FJsonObject>>& DesiredElements,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	TArray<TSharedRef<FJsonObject>> CurrentElements;
	FAssetDocumentCapabilityResult Result = ExtractSyncGroups(Context, CurrentElements);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TMap<FString, TSharedRef<FJsonObject>> CurrentByIdentity;
	TMap<FString, FString> CurrentNamesByIdentity;
	for (const TSharedRef<FJsonObject>& Current : CurrentElements)
	{
		FString Name;
		Current->TryGetStringField(TEXT("Name"), Name);
		const FString Normalized = NormalizeSyncGroupIdentity(Name);
		CurrentByIdentity.Add(Normalized, Current);
		CurrentNamesByIdentity.Add(Normalized, Name);
	}

	TSet<FString> DesiredIdentities;
	for (const TSharedRef<FJsonObject>& Desired : DesiredElements)
	{
		FString Name;
		Desired->TryGetStringField(TEXT("Name"), Name);
		const FString Normalized = NormalizeSyncGroupIdentity(Name);
		DesiredIdentities.Add(Normalized);

		const TSharedRef<FJsonObject>* Current = CurrentByIdentity.Find(Normalized);
		if (!Current)
		{
			FAssetDocumentJsonRegionUtils::AddDiffEntry(
				OutDiffEntries,
				SyncGroupPath(Name),
				TEXT("added"),
				MakeShared<FJsonValueNull>(),
				MakeShared<FJsonValueObject>(Desired));
			continue;
		}

		const TSharedPtr<FJsonValue> CurrentValue = MakeShared<FJsonValueObject>(*Current);
		const TSharedPtr<FJsonValue> DesiredValue = MakeShared<FJsonValueObject>(Desired);
		if (FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentValue)
			!= FAssetDocumentJsonRegionUtils::JsonValueToComparableString(DesiredValue))
		{
			FAssetDocumentJsonRegionUtils::AddDiffEntry(
				OutDiffEntries,
				SyncGroupPath(Name),
				TEXT("changed"),
				CurrentValue,
				DesiredValue);
		}
	}

	for (const TPair<FString, TSharedRef<FJsonObject>>& Pair : CurrentByIdentity)
	{
		if (DesiredIdentities.Contains(Pair.Key))
		{
			continue;
		}

		const FString PathName = CurrentNamesByIdentity.FindRef(Pair.Key);
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			SyncGroupPath(PathName),
			TEXT("removed"),
			MakeShared<FJsonValueObject>(Pair.Value),
			MakeShared<FJsonValueNull>());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed AnimBlueprint SyncGroups"));
}

FAssetDocumentNamedArrayRegionAdapter MakeSyncGroupsRegionAdapter()
{
	FAssetDocumentNamedArrayRegionAdapterConfig Config;
	Config.Name = FAnimBlueprintAssetDocumentProfile::SyncGroupsRegionAdapterName();
	Config.IdentityField = TEXT("Name");
	Config.DuplicateIdentityCode = TEXT("DuplicateSyncGroupName");
	Config.NormalizeIdentity = [](const FString& Identity)
	{
		return NormalizeSyncGroupIdentity(Identity);
	};
	Config.bCanonicalizeByIdentity = false;
	Config.bPreserveAuthoredApplyOrder = true;

	FAssetDocumentNamedArrayRegionAdapterHooks Hooks;
	Hooks.ValidateElement = [](const FAssetDocumentRegionContext& Context, const TSharedRef<FJsonObject>& Element, int32 Index)
	{
		return ValidateSyncGroupElement(Context, Element, Index);
	};
	Hooks.ApplyElements = [](FAssetDocumentRegionContext& Context, const TArray<TSharedRef<FJsonObject>>& Elements, bool& bOutChanged)
	{
		return ApplySyncGroups(Context, Elements, bOutChanged);
	};
	Hooks.ExtractElements = [](const FAssetDocumentRegionContext& Context, TArray<TSharedRef<FJsonObject>>& OutElements)
	{
		return ExtractSyncGroups(Context, OutElements);
	};
	Hooks.DiffElements = [](const FAssetDocumentRegionContext& Context, const TArray<TSharedRef<FJsonObject>>& DesiredElements, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		return DiffSyncGroups(Context, DesiredElements, OutDiffEntries);
	};

	return FAssetDocumentNamedArrayRegionAdapter(MoveTemp(Config), MoveTemp(Hooks));
}

FAssetDocumentObjectFieldSchema MakeTemplateSchema()
{
	FAssetDocumentObjectFieldSchema Schema;
	Schema.Fields.Add({
		TEXT("bIsTemplate"),
		EJson::Boolean,
		false,
		TEXT("MissingTemplateFlag"),
		TEXT("InvalidTemplateFlag"),
		TEXT("Body.Template.bIsTemplate must be a boolean")
	});
	return Schema;
}

FAssetDocumentObjectFieldSchema MakePreviewSchema()
{
	FAssetDocumentObjectFieldSchema Schema;
	Schema.Fields.Add({
		TEXT("PreviewSkeletalMesh"),
		EJson::Object,
		false,
		TEXT("MissingPreviewSkeletalMesh"),
		TEXT("InvalidPreviewSkeletalMesh"),
		TEXT("Body.Preview.PreviewSkeletalMesh must be an AssetRef object or null"),
		true
	});
	Schema.Fields.Add({
		TEXT("PreviewAnimationBlueprint"),
		EJson::Object,
		false,
		TEXT("MissingPreviewAnimationBlueprint"),
		TEXT("InvalidPreviewAnimationBlueprint"),
		TEXT("Body.Preview.PreviewAnimationBlueprint must be an AssetRef object or null"),
		true
	});
	Schema.Fields.Add({
		TEXT("PreviewAnimationBlueprintApplicationMethod"),
		EJson::String,
		false,
		TEXT("MissingPreviewAnimationBlueprintApplicationMethod"),
		TEXT("InvalidPreviewAnimationBlueprintApplicationMethod"),
		TEXT("Body.Preview.PreviewAnimationBlueprintApplicationMethod must be a string")
	});
	Schema.Fields.Add({
		TEXT("PreviewAnimationBlueprintTag"),
		EJson::String,
		false,
		TEXT("MissingPreviewAnimationBlueprintTag"),
		TEXT("InvalidPreviewAnimationBlueprintTag"),
		TEXT("Body.Preview.PreviewAnimationBlueprintTag must be a string")
	});
	return Schema;
}

FAssetDocumentObjectFieldSchema MakeOptimizationSchema()
{
	FAssetDocumentObjectFieldSchema Schema;
	Schema.Fields.Add({
		TEXT("bUseMultiThreadedAnimationUpdate"),
		EJson::Boolean,
		false,
		TEXT("MissingOptimizationFlag"),
		TEXT("InvalidOptimizationFlag"),
		TEXT("Body.Optimization.bUseMultiThreadedAnimationUpdate must be a boolean")
	});
	Schema.Fields.Add({
		TEXT("bWarnAboutBlueprintUsage"),
		EJson::Boolean,
		false,
		TEXT("MissingOptimizationFlag"),
		TEXT("InvalidOptimizationFlag"),
		TEXT("Body.Optimization.bWarnAboutBlueprintUsage must be a boolean")
	});
	Schema.Fields.Add({
		TEXT("bEnableLinkedAnimLayerInstanceSharing"),
		EJson::Boolean,
		false,
		TEXT("MissingOptimizationFlag"),
		TEXT("InvalidOptimizationFlag"),
		TEXT("Body.Optimization.bEnableLinkedAnimLayerInstanceSharing must be a boolean")
	});
	return Schema;
}

FAssetDocumentCapabilityResult ValidatePreviewApplicationMethod(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Preview)
{
	FString Method;
	if (!Preview->TryGetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), Method))
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (Method == TEXT("LinkedLayers") || Method == TEXT("LinkedAnimGraph"))
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	return BodyFailure(
		FString::Printf(TEXT("Unsupported PreviewAnimationBlueprintApplicationMethod '%s'"), *Method),
		FAssetDocumentObjectFieldSchemaUtils::MakeFieldPath(Context, TEXT("PreviewAnimationBlueprintApplicationMethod")),
		TEXT("InvalidPreviewAnimationBlueprintApplicationMethod"));
}

FAssetDocumentCapabilityResult ValidateCoreObjectRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Object)
{
	if (Context.RegionId == TEXT("Body.ParentClass"))
	{
		return ValidateParentClassObject(Context, Object);
	}
	if (Context.RegionId == TEXT("Body.Template"))
	{
		return FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(Context, Object, MakeTemplateSchema());
	}
	if (Context.RegionId == TEXT("Body.Preview"))
	{
		FAssetDocumentCapabilityResult Result =
			FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(Context, Object, MakePreviewSchema());
		if (!Result.bSuccess)
		{
			return Result;
		}
		return ValidatePreviewApplicationMethod(Context, Object);
	}
	if (Context.RegionId == TEXT("Body.Optimization"))
	{
		return FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(Context, Object, MakeOptimizationSchema());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint object region"));
}

FAssetDocumentCapabilityResult ApplyCoreObjectRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Object,
	bool& bOutChanged)
{
	bOutChanged = false;
	FAssetDocumentCapabilityResult Result = ValidateCoreObjectRegion(Context, Object);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	if (!AnimBlueprint)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint object region"));
	}

	if (Context.RegionId == TEXT("Body.ParentClass"))
	{
		UClass* ParentClass = nullptr;
		Result = ResolveParentClassObject(Object, ParentClass);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (ParentClass && AnimBlueprint->ParentClass.Get() != ParentClass)
		{
			AnimBlueprint->ParentClass = ParentClass;
			bOutChanged = true;
		}
	}
	else if (Context.RegionId == TEXT("Body.Template"))
	{
		bool bIsTemplate = false;
		if (TryReadOptionalBool(Object, TEXT("bIsTemplate"), bIsTemplate) && AnimBlueprint->bIsTemplate != bIsTemplate)
		{
			AnimBlueprint->bIsTemplate = bIsTemplate;
			if (bIsTemplate && AnimBlueprint->TargetSkeleton)
			{
				AnimBlueprint->TargetSkeleton = nullptr;
				SyncGeneratedClassTargetSkeleton(AnimBlueprint);
			}
			bOutChanged = true;
		}
	}
	else if (Context.RegionId == TEXT("Body.Preview"))
	{
		UObject* PreviewMeshObject = nullptr;
		Result = ResolveAssetRefValue(
			Object->TryGetField(TEXT("PreviewSkeletalMesh")),
			USkeletalMesh::StaticClass(),
			TEXT("/Body/Preview/PreviewSkeletalMesh"),
			PreviewMeshObject);
		if (!Result.bSuccess)
		{
			return Result;
		}
		USkeletalMesh* PreviewMesh = Cast<USkeletalMesh>(PreviewMeshObject);
		if (Object->HasField(TEXT("PreviewSkeletalMesh")) && AnimBlueprint->GetPreviewMesh() != PreviewMesh)
		{
			AnimBlueprint->SetPreviewMesh(PreviewMesh, true);
			bOutChanged = true;
		}

		UObject* PreviewAnimBlueprintObject = nullptr;
		Result = ResolveAssetRefValue(
			Object->TryGetField(TEXT("PreviewAnimationBlueprint")),
			UAnimBlueprint::StaticClass(),
			TEXT("/Body/Preview/PreviewAnimationBlueprint"),
			PreviewAnimBlueprintObject);
		if (!Result.bSuccess)
		{
			return Result;
		}
		UAnimBlueprint* PreviewAnimBlueprint = Cast<UAnimBlueprint>(PreviewAnimBlueprintObject);
		if (Object->HasField(TEXT("PreviewAnimationBlueprint")) && AnimBlueprint->GetPreviewAnimationBlueprint() != PreviewAnimBlueprint)
		{
			AnimBlueprint->SetPreviewAnimationBlueprint(PreviewAnimBlueprint);
			bOutChanged = true;
		}

		FString Method;
		if (Object->TryGetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), Method))
		{
			const EPreviewAnimationBlueprintApplicationMethod ParsedMethod = ParsePreviewApplicationMethod(Method);
			if (AnimBlueprint->GetPreviewAnimationBlueprintApplicationMethod() != ParsedMethod)
			{
				AnimBlueprint->SetPreviewAnimationBlueprintApplicationMethod(ParsedMethod);
				bOutChanged = true;
			}
		}

		FString Tag;
		if (Object->TryGetStringField(TEXT("PreviewAnimationBlueprintTag"), Tag))
		{
			const FName ParsedTag(*Tag);
			if (AnimBlueprint->GetPreviewAnimationBlueprintTag() != ParsedTag)
			{
				AnimBlueprint->SetPreviewAnimationBlueprintTag(ParsedTag);
				bOutChanged = true;
			}
		}
	}
	else if (Context.RegionId == TEXT("Body.Optimization"))
	{
		bool bBoolValue = false;
		if (TryReadOptionalBool(Object, TEXT("bUseMultiThreadedAnimationUpdate"), bBoolValue)
			&& AnimBlueprint->bUseMultiThreadedAnimationUpdate != bBoolValue)
		{
			AnimBlueprint->bUseMultiThreadedAnimationUpdate = bBoolValue;
			bOutChanged = true;
		}
		if (TryReadOptionalBool(Object, TEXT("bWarnAboutBlueprintUsage"), bBoolValue)
			&& AnimBlueprint->bWarnAboutBlueprintUsage != bBoolValue)
		{
			AnimBlueprint->bWarnAboutBlueprintUsage = bBoolValue;
			bOutChanged = true;
		}
		if (TryReadOptionalBool(Object, TEXT("bEnableLinkedAnimLayerInstanceSharing"), bBoolValue)
			&& AnimBlueprint->bEnableLinkedAnimLayerInstanceSharing != bBoolValue)
		{
			AnimBlueprint->bEnableLinkedAnimLayerInstanceSharing = bBoolValue;
			bOutChanged = true;
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Applied AnimBlueprint object region"));
}

TSharedRef<FJsonObject> MakeDefaultParentClassObject()
{
	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimInstance"));
	return ParentClass;
}

FAssetDocumentCapabilityResult ExtractCoreObjectRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedRef<FJsonObject>& OutObject)
{
	const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	if (Context.RegionId == TEXT("Body.ParentClass"))
	{
		OutObject = AnimBlueprint ? MakeClassRefObject(AnimBlueprint->ParentClass.Get()) : MakeDefaultParentClassObject();
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted default AnimBlueprint ParentClass"));
	}
	if (Context.RegionId == TEXT("Body.Template"))
	{
		OutObject->SetBoolField(TEXT("bIsTemplate"), AnimBlueprint ? AnimBlueprint->bIsTemplate : false);
	}
	else if (Context.RegionId == TEXT("Body.Preview"))
	{
		OutObject->SetField(TEXT("PreviewSkeletalMesh"), MakeAssetRefValue(AnimBlueprint ? AnimBlueprint->GetPreviewMesh() : nullptr));
		OutObject->SetField(TEXT("PreviewAnimationBlueprint"), MakeAssetRefValue(AnimBlueprint ? AnimBlueprint->GetPreviewAnimationBlueprint() : nullptr));
		OutObject->SetStringField(
			TEXT("PreviewAnimationBlueprintApplicationMethod"),
			AnimBlueprint
				? PreviewApplicationMethodToString(AnimBlueprint->GetPreviewAnimationBlueprintApplicationMethod())
				: FString(TEXT("LinkedLayers")));
		OutObject->SetStringField(
			TEXT("PreviewAnimationBlueprintTag"),
			AnimBlueprint ? AnimBlueprint->GetPreviewAnimationBlueprintTag().ToString() : FString());
	}
	else if (Context.RegionId == TEXT("Body.Optimization"))
	{
		OutObject->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), AnimBlueprint ? AnimBlueprint->bUseMultiThreadedAnimationUpdate : true);
		OutObject->SetBoolField(TEXT("bWarnAboutBlueprintUsage"), AnimBlueprint ? AnimBlueprint->bWarnAboutBlueprintUsage : false);
		OutObject->SetBoolField(TEXT("bEnableLinkedAnimLayerInstanceSharing"), AnimBlueprint ? AnimBlueprint->bEnableLinkedAnimLayerInstanceSharing : false);
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimBlueprint object region default"));
}

FAssetDocumentObjectRegionAdapter MakeCoreObjectRegionAdapter()
{
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ValidateObject = [](const FAssetDocumentRegionContext& Context, const TSharedRef<FJsonObject>& Object)
	{
		return ValidateCoreObjectRegion(Context, Object);
	};
	Hooks.ApplyObject = [](FAssetDocumentRegionContext& Context, const TSharedRef<FJsonObject>& Object, bool& bOutChanged)
	{
		return ApplyCoreObjectRegion(Context, Object, bOutChanged);
	};
	Hooks.ExtractObject = [](const FAssetDocumentRegionContext& Context, TSharedRef<FJsonObject>& OutObject)
	{
		return ExtractCoreObjectRegion(Context, OutObject);
	};
	Hooks.DiffObject = [](const FAssetDocumentRegionContext& Context, const TSharedRef<FJsonObject>& Object, TArray<TSharedPtr<FJsonValue>>&)
	{
		return ValidateCoreObjectRegion(Context, Object);
	};
	return FAssetDocumentObjectRegionAdapter(FAnimBlueprintAssetDocumentProfile::ObjectRegionAdapterName(), MoveTemp(Hooks));
}

class FAnimBlueprintTargetSkeletonRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	virtual FName GetName() const override
	{
		return FAnimBlueprintAssetDocumentProfile::TargetSkeletonRegionAdapterName();
	}

	virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const override
	{
		return Context.RegionId == TEXT("Body.TargetSkeleton");
	}

	virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext&) const override
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("Adapter"), GetName().ToString());
		Schema->SetStringField(TEXT("Shape"), TEXT("AssetRef<USkeleton>|null"));
		return Schema;
	}

	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override
	{
		if (!DesiredValue.IsValid() || DesiredValue->Type == EJson::Null)
		{
			return FAssetDocumentCapabilityResult::Success(TEXT("TargetSkeleton omitted"));
		}
		if (DesiredValue->Type != EJson::Object)
		{
			return BodyFailure(TEXT("Body.TargetSkeleton must be an AssetRef object or null"), Context.JsonPointer, TEXT("InvalidTargetSkeleton"));
		}

		TSharedPtr<FJsonObject> AssetRef = DesiredValue->AsObject();
		if (!AssetRef.IsValid())
		{
			return BodyFailure(TEXT("Body.TargetSkeleton must be an AssetRef object or null"), Context.JsonPointer, TEXT("InvalidTargetSkeleton"));
		}

		FString Kind;
		if (!AssetRef->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("AssetRef"))
		{
			return BodyFailure(TEXT("Body.TargetSkeleton.Kind must be AssetRef"), Context.JsonPointer / TEXT("Kind"), TEXT("InvalidTargetSkeletonKind"));
		}

		TSharedRef<FJsonObject> NormalizedAssetRef = MakeShared<FJsonObject>(*AssetRef);
		FString Path;
		if (!NormalizedAssetRef->TryGetStringField(TEXT("Path"), Path) || Path.TrimStartAndEnd().IsEmpty())
		{
			FString Asset;
			if (NormalizedAssetRef->TryGetStringField(TEXT("Asset"), Asset) && !Asset.TrimStartAndEnd().IsEmpty())
			{
				NormalizedAssetRef->SetStringField(TEXT("Path"), Asset);
			}
			else
			{
				return BodyFailure(TEXT("Body.TargetSkeleton.Path is required"), Context.JsonPointer / TEXT("Path"), TEXT("MissingTargetSkeleton"));
			}
		}

		FAssetDocumentFragmentCompiler Compiler;
		Compiler.RegisterBuiltInAdapters();
		FAssetDocumentFragmentContext FragmentContext;
		FragmentContext.OwnerAsset = Context.Asset;
		FragmentContext.ExpectedBaseClass = USkeleton::StaticClass();
		FragmentContext.Definitions = Context.Definitions;
		FragmentContext.JsonPath = Context.JsonPointer;
		FragmentContext.Role = TEXT("TargetSkeleton");
		const FAssetDocumentFragmentResult FragmentResult = Compiler.Compile(NormalizedAssetRef, FragmentContext);
		if (!FragmentResult.bSuccess)
		{
			return BodyFailure(
				FragmentResult.Message.IsEmpty() ? TEXT("Body.TargetSkeleton must resolve to a USkeleton asset") : FragmentResult.Message,
				Context.JsonPointer,
				TEXT("InvalidTargetSkeleton"));
		}

		return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint TargetSkeleton"));
	}

	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) override
	{
		bOutChanged = false;
		FAssetDocumentCapabilityResult Result = ValidateRegion(Context, DesiredValue);
		if (!Result.bSuccess)
		{
			return Result;
		}

		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
		if (!AnimBlueprint)
		{
			return Result;
		}

		UObject* ResolvedObject = nullptr;
		Result = ResolveAssetRefValue(DesiredValue, USkeleton::StaticClass(), Context.JsonPointer, ResolvedObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		USkeleton* DesiredSkeleton = Cast<USkeleton>(ResolvedObject);
		if (AnimBlueprint->TargetSkeleton.Get() != DesiredSkeleton)
		{
			AnimBlueprint->TargetSkeleton = DesiredSkeleton;
			SyncGeneratedClassTargetSkeleton(AnimBlueprint);
			bOutChanged = true;
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Applied AnimBlueprint TargetSkeleton"));
	}

	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext& Context,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override
	{
		const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
		OutCurrentValue = MakeAssetRefValue(AnimBlueprint ? AnimBlueprint->TargetSkeleton.Get() : nullptr);
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted default AnimBlueprint TargetSkeleton"));
	}

	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>&) const override
	{
		return ValidateRegion(Context, DesiredValue);
	}
};

bool IsTemplateFlagTrue(const TSharedRef<FJsonObject>& BodyObject)
{
	const TSharedPtr<FJsonObject>* Template = nullptr;
	if (!BodyObject->TryGetObjectField(TEXT("Template"), Template) || !Template || !Template->IsValid())
	{
		return false;
	}

	bool bIsTemplate = false;
	return (*Template)->TryGetBoolField(TEXT("bIsTemplate"), bIsTemplate)
		&& bIsTemplate;
}

bool HasNonNullTargetSkeleton(const TSharedRef<FJsonObject>& BodyObject)
{
	const TSharedPtr<FJsonValue> TargetSkeleton = BodyObject->TryGetField(TEXT("TargetSkeleton"));
	return TargetSkeleton.IsValid() && TargetSkeleton->Type != EJson::Null;
}

FAssetDocumentCapabilityResult ValidateAnimBlueprintCrossRegion(
	const FAssetDocumentCapabilityContext&,
	const TSharedRef<FJsonObject>& BodyObject)
{
	if (IsTemplateFlagTrue(BodyObject) && HasNonNullTargetSkeleton(BodyObject))
	{
		return BodyFailure(
			TEXT("Template AnimationBlueprints cannot author TargetSkeleton"),
			TEXT("/Body/TargetSkeleton"),
			TEXT("InvalidTemplateSkeleton"));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint cross-region constraints"));
}

FAssetDocumentCapabilityResult RepairAnimBlueprintAfterApply(
	FAssetDocumentCapabilityContext& Context,
	const TSet<FName>& AppliedRegions)
{
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	if (!AnimBlueprint || AppliedRegions.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("No AnimBlueprint repair needed"));
	}

	SyncGeneratedClassTargetSkeleton(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (AnimBlueprint->Status == BS_Error)
	{
		return BodyFailure(TEXT("Failed to compile AnimBlueprint after applying Body regions"), TEXT("/Body"), TEXT("AnimBlueprintCompileFailed"));
	}

	AnimBlueprint->MarkPackageDirty();
	return FAssetDocumentCapabilityResult::Success(TEXT("Repaired AnimBlueprint after applying Body regions"));
}

FAssetDocumentCapabilityResult DispatchWithDispatcher(
	TFunctionRef<FAssetDocumentCapabilityResult(const FAssetDocumentBodyRegionDispatcher&)> Dispatch)
{
	FAssetDocumentObjectRegionAdapter ObjectAdapter = MakeCoreObjectRegionAdapter();
	FAnimBlueprintTargetSkeletonRegionAdapter TargetSkeletonAdapter;
	FAssetDocumentNamedArrayRegionAdapter SyncGroupsAdapter = MakeSyncGroupsRegionAdapter();
	FBlueprintAssetDocumentCommonRegionAdapter BlueprintCommonAdapter(
		FAnimBlueprintAssetDocumentProfile::BlueprintCommonRegionAdapterName());
	FAssetDocumentAnimGraphRegionAdapter AnimGraphAdapter(
		FAnimBlueprintAssetDocumentProfile::AnimGraphRegionAdapterName());
	FAssetDocumentDeferredRegionAdapter DeferredAdapter(
		FAnimBlueprintAssetDocumentProfile::DeferredRegionAdapterName(),
		TEXT("UnsupportedAnimBlueprintRegion"),
		TEXT(""));

	TMap<FName, IAssetDocumentRegionAdapter*> Adapters;
	Adapters.Add(ObjectAdapter.GetName(), &ObjectAdapter);
	Adapters.Add(TargetSkeletonAdapter.GetName(), &TargetSkeletonAdapter);
	Adapters.Add(SyncGroupsAdapter.GetName(), &SyncGroupsAdapter);
	Adapters.Add(BlueprintCommonAdapter.GetName(), &BlueprintCommonAdapter);
	Adapters.Add(AnimGraphAdapter.GetName(), &AnimGraphAdapter);
	Adapters.Add(DeferredAdapter.GetName(), &DeferredAdapter);

	FAssetDocumentBodyRegionDispatcherHooks Hooks;
	Hooks.ValidateCrossRegion = [](const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject)
	{
		return ValidateAnimBlueprintCrossRegion(Context, BodyObject);
	};
	Hooks.PostApplyRepair = [](FAssetDocumentCapabilityContext& Context, const TSet<FName>& AppliedRegions)
	{
		return RepairAnimBlueprintAfterApply(Context, AppliedRegions);
	};

	const FAssetDocumentBodyRegionDispatcher Dispatcher(
		FAnimBlueprintAssetDocumentProfile::MakeRegionBindings(),
		FAnimBlueprintAssetDocumentProfile().GetRegionPolicies(),
		Adapters,
		MoveTemp(Hooks));
	return Dispatch(Dispatcher);
}

FAssetDocumentCapabilityResult ValidateContext(const FAssetDocumentCapabilityContext& Context)
{
	if (Context.Asset && Context.Asset->GetClass() != UAnimBlueprint::StaticClass())
	{
		return BodyFailure(TEXT("AnimBlueprint body validation requires exact UAnimBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}
	if (!Context.Asset && Context.AssetClass && Context.AssetClass != UAnimBlueprint::StaticClass())
	{
		return BodyFailure(TEXT("AnimBlueprint body validation requires exact UAnimBlueprint class"), TEXT("/Class"), TEXT("UnsupportedClass"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateWithDispatcher(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& BodyJson)
{
	FAssetDocumentCapabilityResult Result = ValidateContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedPtr<FJsonValue> DispatcherBodyJson;
	Result = NormalizeDeferredGraphFamilyCompatibility(BodyJson, DispatcherBodyJson);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return RemapDispatcherCompatibilityCodes(DispatchWithDispatcher(
		[&Context, &DispatcherBodyJson](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ValidateBody(Context, DispatcherBodyJson.ToSharedRef());
		}));
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
		FAnimBlueprintAssetDocumentProfile::ObjectRegionAdapterName(),
		FAnimBlueprintAssetDocumentProfile::TargetSkeletonRegionAdapterName(),
		FAnimBlueprintAssetDocumentProfile::SyncGroupsRegionAdapterName(),
		FAnimBlueprintAssetDocumentProfile::BlueprintCommonRegionAdapterName(),
		FAnimBlueprintAssetDocumentProfile::AnimGraphRegionAdapterName(),
		FAnimBlueprintAssetDocumentProfile::DeferredRegionAdapterName(),
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
	Schema->SetStringField(TEXT("AnimGraph"), TEXT("root-only graph pilot: array<{Name:'AnimGraph', Nodes:[], OutputPose:{Node:null, Pin:'Result'}}>"));
	Schema->SetStringField(TEXT("StateMachines"), TEXT("deferred empty graph/tree region until state-machine adapter lands"));
	Schema->SetStringField(TEXT("TransitionGraphs"), TEXT("deferred empty graph region until transition graph adapter lands"));
	Schema->SetStringField(TEXT("AnimLayers"), TEXT("deferred empty graph/array region until anim layer adapter lands"));
	Schema->SetStringField(TEXT("ParentAssetOverrides"), TEXT("deferred empty identity-array region until AnimGraph identity lands"));
	return Schema;
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	return ValidateWithDispatcher(Context, BodyJson);
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	FAssetDocumentCapabilityResult Result = ValidateContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TSharedPtr<FJsonValue> DispatcherBodyJson;
	Result = NormalizeDeferredGraphFamilyCompatibility(BodyJson, DispatcherBodyJson);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return RemapDispatcherCompatibilityCodes(DispatchWithDispatcher(
		[&Context, &DispatcherBodyJson](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.PreflightBody(Context, DispatcherBodyJson.ToSharedRef());
		}));
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	FAssetDocumentCapabilityResult Result = ValidateContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TSharedPtr<FJsonValue> DispatcherBodyJson;
	Result = NormalizeDeferredGraphFamilyCompatibility(BodyJson, DispatcherBodyJson);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TSet<FName> AppliedRegions;
	return RemapDispatcherCompatibilityCodes(DispatchWithDispatcher(
		[&Context, &DispatcherBodyJson, &AppliedRegions](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ApplyBody(Context, DispatcherBodyJson.ToSharedRef(), AppliedRegions);
		}));
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	const FAssetDocumentCapabilityResult Result = ValidateContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return RemapDispatcherCompatibilityCodes(DispatchWithDispatcher(
		[&Context, &OutBodyJson](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ExtractBody(Context, OutBodyJson);
		}));
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	FAssetDocumentCapabilityResult Result = ValidateContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TSharedPtr<FJsonValue> DispatcherBodyJson;
	Result = NormalizeDeferredGraphFamilyCompatibility(DesiredJson, DispatcherBodyJson);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return RemapDispatcherCompatibilityCodes(DispatchWithDispatcher(
		[&Context, &DispatcherBodyJson, &OutDiffEntries](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.DiffBody(Context, DispatcherBodyJson.ToSharedRef(), OutDiffEntries);
		}));
}
