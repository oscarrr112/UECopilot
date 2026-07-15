// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "AssetDocumentClassResolver.h"
#include "AssetDocumentApplyTransaction.h"
#include "AssetDocumentCanonicalJson.h"
#include "AssetDocumentEditorSync.h"
#include "AssetDocumentLifecycle.h"
#include "AssetDocumentManagedPropertyPartition.h"
#include "AssetDocumentPersistenceTransaction.h"
#include "AssetDocumentPolicyRegistry.h"
#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentPropertyAdapter.h"
#include "AssetDocumentSidecar.h"
#include "AssetDocumentSidecarDelta.h"
#include "AssetDocumentServiceTestHooks.h"
#include "AssetDocumentSyncStateStore.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "DiffUtils.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include <initializer_list>

namespace
{
FString NormalizeValidateFilePath(const FString& FilePath)
{
	if (FilePath.IsEmpty())
	{
		return FString();
	}

	FString NormalizedPath = FilePath;
	FPaths::NormalizeFilename(NormalizedPath);
	if (FPaths::IsRelative(NormalizedPath) && NormalizedPath.StartsWith(TEXT("Content/"), ESearchCase::IgnoreCase))
	{
		NormalizedPath = FPaths::Combine(FPaths::ProjectDir(), NormalizedPath);
	}

	NormalizedPath = FPaths::ConvertRelativePathToFull(NormalizedPath);
	FPaths::NormalizeFilename(NormalizedPath);
	return NormalizedPath;
}

FString NormalizeValidateTarget(const FString& Target)
{
	FString NormalizedTarget = Target;
	FPaths::NormalizeFilename(NormalizedTarget);
	NormalizedTarget.TrimStartAndEndInline();

	FString PackagePath;
	FString ObjectName;
	if (NormalizedTarget.Split(TEXT("."), &PackagePath, &ObjectName))
	{
		NormalizedTarget = PackagePath;
	}

	return NormalizedTarget;
}

bool ValidateApplyTarget(const FString& Target, FString& OutError)
{
	if (Target.IsEmpty())
	{
		OutError = TEXT("Target is required");
		return false;
	}

	if (!Target.StartsWith(TEXT("/Game/")))
	{
		OutError = FString::Printf(TEXT("Target '%s' must be a long package name under /Game"), *Target);
		return false;
	}

	FText PackageNameReason;
	if (!FPackageName::IsValidLongPackageName(Target, false, &PackageNameReason))
	{
		OutError = FString::Printf(TEXT("Target '%s' is not a valid long package name: %s"), *Target, *PackageNameReason.ToString());
		return false;
	}

	if (FPackageName::GetLongPackageAssetName(Target).IsEmpty())
	{
		OutError = FString::Printf(TEXT("Target '%s' must include a non-empty asset name"), *Target);
		return false;
	}

	return true;
}

FString ToObjectPath(const FString& PackageOrObjectPath)
{
	if (PackageOrObjectPath.Contains(TEXT(".")))
	{
		return PackageOrObjectPath;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageOrObjectPath);
	if (AssetName.IsEmpty())
	{
		return PackageOrObjectPath;
	}

	return FString::Printf(TEXT("%s.%s"), *PackageOrObjectPath, *AssetName);
}

UObject* LoadAssetFromPackageOrObjectPath(const FString& PackageOrObjectPath)
{
	if (PackageOrObjectPath.IsEmpty())
	{
		return nullptr;
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *ToObjectPath(PackageOrObjectPath));
	if (!Asset && PackageOrObjectPath.Contains(TEXT(".")))
	{
		Asset = LoadObject<UObject>(nullptr, *PackageOrObjectPath);
	}
	return Asset;
}

TSharedPtr<FJsonObject> MakeDiffDocument(TSharedPtr<FJsonObject> Document)
{
	if (!Document.IsValid())
	{
		return Document;
	}

	const TSharedPtr<FJsonObject>* BodyObject = nullptr;
	if (!Document->TryGetObjectField(TEXT("Body"), BodyObject) || !BodyObject || !BodyObject->IsValid() || !(*BodyObject)->HasField(TEXT("_Skipped")))
	{
		return Document;
	}

	TSharedPtr<FJsonObject> DiffDocument = MakeShared<FJsonObject>();
	DiffDocument->Values = Document->Values;

	TSharedPtr<FJsonObject> DiffBody = MakeShared<FJsonObject>();
	DiffBody->Values = (*BodyObject)->Values;
	DiffBody->RemoveField(TEXT("_Skipped"));
	DiffDocument->SetObjectField(TEXT("Body"), DiffBody);
	return DiffDocument;
}

struct FAssetDocumentResolvedTarget
{
	UObject* Asset = nullptr;
	UClass* Class = nullptr;
	FString Target;
	FString AssetPath;
};

FAssetDocumentResult ResolveClassOrAssetTarget(const FString& ClassOrAsset, const FString& OperationName, FAssetDocumentResolvedTarget& OutTarget)
{
	if (ClassOrAsset.IsEmpty())
	{
		return FAssetDocumentResult::Failure(FString::Printf(TEXT("%s requires a class name or asset path"), *OperationName));
	}

	OutTarget.Target = ClassOrAsset;
	if (ClassOrAsset.StartsWith(TEXT("/Game/")) && ClassOrAsset.EndsWith(TEXT("_C")))
	{
		FString ClassError;
		if (FAssetDocumentClassResolver::ResolveClass(ClassOrAsset, OutTarget.Class, ClassError))
		{
			return FAssetDocumentResult::Success(TEXT("Resolved generated class target"));
		}
	}

	if (ClassOrAsset.StartsWith(TEXT("/Game/")))
	{
		OutTarget.Asset = LoadAssetFromPackageOrObjectPath(ClassOrAsset);
		if (!OutTarget.Asset)
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to load asset '%s'"), *ClassOrAsset));
			Result.Target = ClassOrAsset;
			Result.AssetPath = ToObjectPath(ClassOrAsset);
			return Result;
		}

		OutTarget.Class = OutTarget.Asset->GetClass();
		OutTarget.Target = NormalizeValidateTarget(ClassOrAsset);
		OutTarget.AssetPath = OutTarget.Asset->GetPathName();
		return FAssetDocumentResult::Success(TEXT("Resolved class or asset target"));
	}

	FString Error;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassOrAsset, OutTarget.Class, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = ClassOrAsset;
		return Result;
	}

	return FAssetDocumentResult::Success(TEXT("Resolved class or asset target"));
}

FAssetDocumentResult ResolveClassTarget(const FString& ClassName, UClass*& OutClass)
{
	if (ClassName.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("CreateTemplate requires a class"));
	}

	FString Error;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassName, OutClass, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = ClassName;
		return Result;
	}

	return FAssetDocumentResult::Success(TEXT("Resolved class target"));
}

TArray<TSharedPtr<FJsonValue>> ServiceMakeStringArray(std::initializer_list<const TCHAR*> Values)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const TCHAR* Value : Values)
	{
		Result.Add(MakeShared<FJsonValueString>(Value));
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> MakeFragmentKindArray()
{
	return ServiceMakeStringArray({
		TEXT("AssetRef"),
		TEXT("ClassRef"),
		TEXT("StructValue"),
		TEXT("EmbeddedObject"),
		TEXT("DefinitionRef"),
	});
}

TArray<TSharedPtr<FJsonValue>> MakeRegionPolicyArray(const TArray<FAssetDocumentRegionPolicy>& Policies)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const FAssetDocumentRegionPolicy& Policy : Policies)
	{
		Result.Add(MakeShared<FJsonValueObject>(FAssetDocumentPolicyRegistry::ExportPolicyToJson(Policy)));
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> MakeRegionPolicyPresetArray()
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const FAssetDocumentRegionPolicyPreset& Preset : FAssetDocumentPolicyRegistry::GetBuiltinPresets())
	{
		Result.Add(MakeShared<FJsonValueObject>(FAssetDocumentPolicyRegistry::ExportPresetToJson(Preset)));
	}
	return Result;
}

TSharedRef<FJsonObject> MakeGenericDocumentShape()
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected CDO-diff properties"));
	Shape->SetObjectField(TEXT("Body"), MakeShared<FJsonObject>());
	return Shape;
}

struct FAssetDocumentProfileResolution
{
	UClass* Class = nullptr;
	TSharedPtr<IAssetDocumentProfile> ExactProfile;
};

FAssetDocumentProfileResolution ResolveAssetDocumentProfile(const FAssetDocumentProfileRegistry& Registry, UClass* Class)
{
	FAssetDocumentProfileResolution Resolution;
	Resolution.Class = Class;
	Resolution.ExactProfile = Registry.FindForClass(Class);
	return Resolution;
}

TSharedPtr<FJsonValue> CloneJsonValuePreservingFields(const TSharedPtr<FJsonValue>& Value);

TSharedRef<FJsonObject> CloneJsonObjectPreservingFields(const TSharedRef<FJsonObject>& Object)
{
	TSharedRef<FJsonObject> Clone = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		Clone->SetField(Pair.Key, CloneJsonValuePreservingFields(Pair.Value));
	}
	return Clone;
}

TSharedPtr<FJsonValue> CloneJsonValuePreservingFields(const TSharedPtr<FJsonValue>& Value)
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
				ClonedArray.Add(CloneJsonValuePreservingFields(Entry));
			}
			return MakeShared<FJsonValueArray>(MoveTemp(ClonedArray));
		}
	case EJson::Object:
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			if (Object.IsValid())
			{
				return MakeShared<FJsonValueObject>(CloneJsonObjectPreservingFields(Object.ToSharedRef()));
			}
			return MakeShared<FJsonValueNull>();
		}
	default:
		return MakeShared<FJsonValueNull>();
	}
}

bool ExtractAssetBodyEvidenceDocument(
	UObject* Asset,
	const TSharedRef<FJsonObject>& SidecarDocument,
	const FString& SourceDocumentPath,
	const FAssetDocumentProfileResolution& ProfileResolution,
	TSharedRef<FJsonObject>& OutEvidenceDocument,
	FString& OutError)
{
	OutError.Reset();
	if (!Asset)
	{
		OutError = TEXT("Asset is required");
		return false;
	}
	if (!ProfileResolution.ExactProfile.IsValid())
	{
		OutError = FString::Printf(TEXT("No exact AssetDocument profile for '%s'"), *Asset->GetClass()->GetPathName());
		return false;
	}

	const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile->ResolveBodyAdapter(TEXT("Body"));
	if (!BodyAdapter)
	{
		OutError = TEXT("Exact AssetDocument profile has no Body adapter");
		return false;
	}

	FString Target;
	SidecarDocument->TryGetStringField(TEXT("Target"), Target);
	Target = NormalizeValidateTarget(Target);
	if (Target.IsEmpty())
	{
		Target = Asset->GetPathName();
	}

	TSharedRef<FJsonObject> EvidenceBody = MakeShared<FJsonObject>();
	FAssetDocumentCapabilityContext CapabilityContext;
	CapabilityContext.Asset = Asset;
	CapabilityContext.AssetClass = Asset->GetClass();
	CapabilityContext.TargetAssetPath = Target;
	CapabilityContext.SourceDocumentPath = SourceDocumentPath;

	const FAssetDocumentCapabilityResult ExtractResult = BodyAdapter->Extract(CapabilityContext, EvidenceBody);
	if (!ExtractResult.bSuccess)
	{
		OutError = ExtractResult.Message.IsEmpty() ? TEXT("Failed to extract asset evidence") : ExtractResult.Message;
		return false;
	}

	OutEvidenceDocument->SetObjectField(TEXT("Body"), EvidenceBody);
	return true;
}

enum class EAssetDocumentApplyFileSyncStateBuildResult : uint8
{
	Updated,
	NotApplicable,
};

EAssetDocumentApplyFileSyncStateBuildResult AssetDocumentPersistenceBuildApplyFileSyncState(
	const FString& SidecarFilePath,
	UObject* AppliedAsset,
	const FAssetDocumentResult& ApplyResult,
	TSharedPtr<FJsonObject>& OutUpdatedDocument,
	FString& OutSkipReason)
{
	OutUpdatedDocument.Reset();
	OutSkipReason.Reset();
	if (SidecarFilePath.IsEmpty() || !AppliedAsset)
	{
		OutSkipReason = TEXT("Missing sidecar path or applied asset");
		return EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
	}

	TSharedPtr<FJsonObject> SourceDocument;
	FString Error;
	if (!FAssetDocumentSidecar::LoadJsonFile(SidecarFilePath, SourceDocument, Error) || !SourceDocument.IsValid())
	{
		OutSkipReason = Error.IsEmpty() ? TEXT("Failed to reload source sidecar") : Error;
		return EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
	}

	const FAssetDocumentProfileResolution ProfileResolution =
		ResolveAssetDocumentProfile(FAssetDocumentService::GetProfileRegistry(), AppliedAsset->GetClass());
	if (!ProfileResolution.ExactProfile.IsValid())
	{
		OutSkipReason = FString::Printf(TEXT("No exact AssetDocument profile for '%s'"), *AppliedAsset->GetClass()->GetPathName());
		return EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
	}

	const TArray<FAssetDocumentRegionPolicy> RegionPolicies = ProfileResolution.ExactProfile->GetRegionPolicies();
	if (RegionPolicies.Num() == 0)
	{
		OutSkipReason = TEXT("Exact AssetDocument profile has no region policies");
		return EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
	}

	const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile->ResolveBodyAdapter(TEXT("Body"));
	if (!BodyAdapter)
	{
		OutSkipReason = TEXT("Exact AssetDocument profile has no Body adapter");
		return EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
	}

	FString Target;
	SourceDocument->TryGetStringField(TEXT("Target"), Target);
	Target = NormalizeValidateTarget(Target);
	if (Target.IsEmpty())
	{
		Target = ApplyResult.Target;
	}

	TSharedRef<FJsonObject> EvidenceDocument = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> EvidenceBody = MakeShared<FJsonObject>();
	FAssetDocumentCapabilityContext CapabilityContext;
	CapabilityContext.Asset = AppliedAsset;
	CapabilityContext.AssetClass = AppliedAsset->GetClass();
	CapabilityContext.TargetAssetPath = Target;
	CapabilityContext.SourceDocumentPath = SidecarFilePath;

	const FAssetDocumentCapabilityResult ExtractResult = BodyAdapter->Extract(CapabilityContext, EvidenceBody);
	if (!ExtractResult.bSuccess)
	{
		OutSkipReason = ExtractResult.Message.IsEmpty() ? TEXT("Failed to extract post-apply asset evidence") : ExtractResult.Message;
		return EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
	}
	EvidenceDocument->SetObjectField(TEXT("Body"), EvidenceBody);

	FAssetDocumentSyncState SyncState;
	FString SyncError;
	if (!FAssetDocumentSyncStateStore::LoadFromDocumentJson(SourceDocument.ToSharedRef(), SyncState, SyncError))
	{
		OutSkipReason = SyncError.IsEmpty() ? TEXT("Malformed existing sync state") : SyncError;
		return EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
	}
	SyncState.AssetObjectPath = AppliedAsset->GetPathName();
	SyncState.UpdatedAtUtc = FDateTime::UtcNow().ToIso8601();

	bool bUpdatedAnyRegion = false;
	for (const FAssetDocumentRegionPolicy& Policy : RegionPolicies)
	{
		const FString SidecarHash = FAssetDocumentSidecarDelta::HashSidecarRegion(
			SourceDocument.ToSharedRef(),
			Policy,
			EAssetDocumentRegionCanonicalizeSource::SidecarAuthored,
			AppliedAsset->GetClass());
		if (SidecarHash.IsEmpty())
		{
			continue;
		}

		const FString AssetEvidenceHash = FAssetDocumentSidecarDelta::HashSidecarRegion(
			EvidenceDocument,
			Policy,
			EAssetDocumentRegionCanonicalizeSource::AssetEvidence,
			AppliedAsset->GetClass());
		if (AssetEvidenceHash.IsEmpty())
		{
			OutSkipReason = FString::Printf(TEXT("Post-apply asset evidence hash is empty for region '%s'"), *Policy.RegionId.ToString());
			return EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
		}
		if (SidecarHash != AssetEvidenceHash)
		{
			OutSkipReason = FString::Printf(TEXT("Post-apply asset evidence hash differs for region '%s'"), *Policy.RegionId.ToString());
			return EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
		}

		FAssetDocumentRegionSyncState RegionState;
		RegionState.SidecarHash = SidecarHash;
		RegionState.AssetEvidenceHash = AssetEvidenceHash;
		RegionState.LastSyncedAtUtc = SyncState.UpdatedAtUtc;
		FAssetDocumentSyncStateStore::UpdateRegionState(SyncState, Policy.RegionId, RegionState);
		bUpdatedAnyRegion = true;
	}

	if (!bUpdatedAnyRegion)
	{
		OutSkipReason = TEXT("No writable sync regions were found");
		return EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
	}

	FAssetDocumentSyncStateStore::WriteToDocumentJson(SourceDocument.ToSharedRef(), SyncState);
	OutUpdatedDocument = MoveTemp(SourceDocument);
	return EAssetDocumentApplyFileSyncStateBuildResult::Updated;
}

TSharedRef<FJsonObject> MakeGenericProfilePayload(const FAssetDocumentProfileResolution& Resolution)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("Class"), Resolution.Class ? Resolution.Class->GetPathName() : FString());
	Payload->SetObjectField(TEXT("DocumentShape"), MakeGenericDocumentShape());
	Payload->SetArrayField(TEXT("BodySections"), TArray<TSharedPtr<FJsonValue>>());
	Payload->SetArrayField(TEXT("FragmentKinds"), MakeFragmentKindArray());
	Payload->SetArrayField(TEXT("InternalAdapters"), TArray<TSharedPtr<FJsonValue>>());
	Payload->SetArrayField(TEXT("RegionPolicies"), TArray<TSharedPtr<FJsonValue>>());
	return Payload;
}

TSharedRef<FJsonObject> MakeExactProfilePayload(const FAssetDocumentProfileResolution& Resolution)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("Class"), Resolution.Class ? Resolution.Class->GetPathName() : FString());
	Payload->SetObjectField(TEXT("DocumentShape"), Resolution.ExactProfile->GetDocumentShape());

	TArray<TSharedPtr<FJsonValue>> BodySections;
	for (const FName& BodyKey : Resolution.ExactProfile->GetBodyKeys())
	{
		BodySections.Add(MakeShared<FJsonValueString>(BodyKey.ToString()));
	}
	Payload->SetArrayField(TEXT("BodySections"), BodySections);
	Payload->SetArrayField(TEXT("FragmentKinds"), MakeFragmentKindArray());

	TArray<TSharedPtr<FJsonValue>> InternalAdapters;
	TSet<FName> SeenAdapterNames;
	auto AddInternalAdapterNames = [&InternalAdapters, &SeenAdapterNames](const IAssetDocumentCapability* BodyAdapter)
	{
		if (!BodyAdapter)
		{
			return;
		}

		for (const FName& AdapterName : BodyAdapter->GetInternalAdapterNames())
		{
			if (!SeenAdapterNames.Contains(AdapterName))
			{
				SeenAdapterNames.Add(AdapterName);
				InternalAdapters.Add(MakeShared<FJsonValueString>(AdapterName.ToString()));
			}
		}
	};

	if (const IAssetDocumentCapability* BodyAdapter = Resolution.ExactProfile->ResolveBodyAdapter(TEXT("Body")))
	{
		AddInternalAdapterNames(BodyAdapter);
	}
	for (const FName& BodyKey : Resolution.ExactProfile->GetBodyKeys())
	{
		AddInternalAdapterNames(Resolution.ExactProfile->ResolveBodyAdapter(BodyKey));
	}
	Payload->SetArrayField(TEXT("InternalAdapters"), InternalAdapters);
	Payload->SetArrayField(TEXT("RegionPolicies"), MakeRegionPolicyArray(Resolution.ExactProfile->GetRegionPolicies()));
	return Payload;
}

TArray<TSharedPtr<FJsonValue>> MakeRegisteredProfileArray(const FAssetDocumentProfileRegistry& Registry)
{
	TArray<TSharedPtr<FJsonValue>> RegisteredProfiles;
	for (const TSharedRef<IAssetDocumentProfile>& Profile : Registry.GetAllProfiles())
	{
		UClass* ExactClass = Profile->GetExactClass();
		if (!ExactClass)
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Class"), ExactClass->GetPathName());

		TArray<TSharedPtr<FJsonValue>> BodySections;
		for (const FName& BodyKey : Profile->GetBodyKeys())
		{
			BodySections.Add(MakeShared<FJsonValueString>(BodyKey.ToString()));
		}
		Entry->SetArrayField(TEXT("BodySections"), BodySections);
		Entry->SetArrayField(TEXT("RegionPolicies"), MakeRegionPolicyArray(Profile->GetRegionPolicies()));
		RegisteredProfiles.Add(MakeShared<FJsonValueObject>(Entry));
	}
	return RegisteredProfiles;
}

FString JsonValueToComparableString(TSharedPtr<FJsonValue> Value)
{
	TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
	Wrapper->SetField(TEXT("value"), Value.IsValid() ? Value : MakeShared<FJsonValueNull>());

	FString JsonText;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);
	return JsonText;
}

void AddNamedValueEntry(TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Name, TSharedPtr<FJsonValue> Value)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Name);
	Entry->SetStringField(TEXT("path"), FString::Printf(TEXT("/Properties/%s"), *Name));
	Entry->SetStringField(TEXT("status"), TEXT("unchanged"));
	Entry->SetField(TEXT("value"), Value.IsValid() ? Value : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

void AddChangedEntry(TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Name, TSharedPtr<FJsonValue> Before, TSharedPtr<FJsonValue> After)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Name);
	Entry->SetStringField(TEXT("path"), FString::Printf(TEXT("/Properties/%s"), *Name));
	Entry->SetStringField(TEXT("status"), TEXT("changed"));
	Entry->SetField(TEXT("before"), Before.IsValid() ? Before : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("after"), After.IsValid() ? After : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

void AddReasonEntry(TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Name, const FString& Code, const FString& Message)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Name);
	Entry->SetStringField(TEXT("path"), FString::Printf(TEXT("/Properties/%s"), *Name));
	Entry->SetStringField(TEXT("code"), Code);
	Entry->SetStringField(TEXT("message"), Message);
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

void AddPathReasonEntry(TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Path, const FString& Status, const FString& Code, const FString& Message)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), Path);
	Entry->SetStringField(TEXT("status"), Status);
	Entry->SetStringField(TEXT("code"), Code);
	Entry->SetStringField(TEXT("message"), Message);
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

void RouteCapabilityDiffEntries(
	const TArray<TSharedPtr<FJsonValue>>& Entries,
	TArray<TSharedPtr<FJsonValue>>& Changed,
	TArray<TSharedPtr<FJsonValue>>& Unchanged,
	TArray<TSharedPtr<FJsonValue>>& Skipped,
	TArray<TSharedPtr<FJsonValue>>& Failed)
{
	for (const TSharedPtr<FJsonValue>& EntryValue : Entries)
	{
		const TSharedPtr<FJsonObject> EntryObject = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		if (!EntryObject.IsValid())
		{
			Failed.Add(EntryValue.IsValid() ? EntryValue : MakeShared<FJsonValueNull>());
			continue;
		}

		FString Status;
		EntryObject->TryGetStringField(TEXT("status"), Status);
		if (Status == TEXT("changed"))
		{
			Changed.Add(EntryValue);
		}
		else if (Status == TEXT("unchanged"))
		{
			Unchanged.Add(EntryValue);
		}
		else if (Status == TEXT("skipped"))
		{
			Skipped.Add(EntryValue);
		}
		else
		{
			Failed.Add(EntryValue);
		}
	}
}

void AssetDocumentPersistenceAddDiagnostic(
	TArray<FAssetDocumentDiagnostic>& Diagnostics,
	const FString& Code,
	const FString& Message,
	const FString& Path = TEXT("/Apply/Persistence"))
{
	FAssetDocumentDiagnostic Diagnostic;
	Diagnostic.Path = Path;
	Diagnostic.Code = Code;
	Diagnostic.Message = Message;
	Diagnostics.Add(MoveTemp(Diagnostic));
}

bool AssetDocumentPersistenceRequiresStrictFreshVerification(UClass* AssetClass)
{
	return AssetClass == UBehaviorTree::StaticClass()
		|| AssetClass == UBlackboardData::StaticClass();
}

bool AssetDocumentPersistenceUnloadFreshPackage(UPackage* Package, FString& OutError)
{
	if (!Package)
	{
		return true;
	}
	TArray<UPackage*> Packages{Package};
	UPackageTools::FUnloadPackageParams Params(Packages);
	Params.bUnloadDirtyPackages = true;
	Params.bResetTransBuffer = false;
	if (!UPackageTools::UnloadPackages(Params))
	{
		OutError = Params.OutErrorMessage.IsEmpty()
			? FString::Printf(TEXT("Failed to unload fresh verification package '%s'"), *Package->GetName())
			: Params.OutErrorMessage.ToString();
		return false;
	}
	return true;
}

bool AssetDocumentPersistenceVerifyFreshStagedDocument(
	const FString& StagedHeaderFilename,
	const FString& CanonicalHeaderFilename,
	const FString& Target,
	UClass* ExpectedClass,
	const TSharedPtr<FJsonObject>& AuthoredDocument,
	TArray<FAssetDocumentDiagnostic>& OutDiagnostics,
	FString& OutError)
{
	OutError.Reset();
	const FPackagePath StagedPackagePath = FPackagePath::FromLocalPath(StagedHeaderFilename);
	const FPackagePath CanonicalPackagePath = FPackagePath::FromLocalPath(CanonicalHeaderFilename);
	UPackage* FreshPackage = DiffUtils::LoadPackageForDiff(StagedPackagePath, CanonicalPackagePath);
	if (!FreshPackage)
	{
		OutError = FString::Printf(
			TEXT("Failed to fresh-load staged package '%s' for AssetDocument verification"),
			*StagedHeaderFilename);
		AssetDocumentPersistenceAddDiagnostic(
			OutDiagnostics,
			TEXT("AssetDocumentFreshReloadFailed"),
			OutError);
		return false;
	}

#if WITH_DEV_AUTOMATION_TESTS
	FAssetDocumentServiceTestHooks::ConsumePersistenceCallback(
		EAssetDocumentServicePersistencePhase::AfterFreshReloadBeforeVerification);
	FAssetDocumentDiagnostic ForcedFreshReloadFailure;
	if (FAssetDocumentServiceTestHooks::ConsumePersistenceFailure(
		EAssetDocumentServicePersistencePhase::AfterFreshReloadBeforeVerification,
		ForcedFreshReloadFailure))
	{
		OutDiagnostics.Add(ForcedFreshReloadFailure);
		OutError = ForcedFreshReloadFailure.Message;
		FString UnloadError;
		AssetDocumentPersistenceUnloadFreshPackage(FreshPackage, UnloadError);
		return false;
	}
#endif

	bool bVerified = true;
	auto FailVerification = [&OutDiagnostics, &OutError, &bVerified](
		const FString& Code,
		const FString& Message,
		const FString& Path)
	{
		bVerified = false;
		if (OutError.IsEmpty())
		{
			OutError = Message;
		}
		AssetDocumentPersistenceAddDiagnostic(OutDiagnostics, Code, Message, Path);
	};

	const FString AssetName = FPackageName::GetLongPackageAssetName(Target);
	UObject* FreshAsset = FindObject<UObject>(FreshPackage, *AssetName);
	if (!FreshAsset)
	{
		FreshAsset = FreshPackage->FindAssetInPackage();
	}
	if (!FreshAsset)
	{
		FailVerification(
			TEXT("AssetDocumentFreshReloadMissingAsset"),
			FString::Printf(TEXT("Fresh package '%s' does not contain asset '%s'"), *FreshPackage->GetName(), *AssetName),
			TEXT("/Target"));
	}
	else if (FreshAsset->GetClass() != ExpectedClass)
	{
		FailVerification(
			TEXT("AssetDocumentFreshReloadClassMismatch"),
			FString::Printf(
				TEXT("Fresh asset class '%s' does not exactly match expected class '%s'"),
				*FreshAsset->GetClass()->GetPathName(),
				*GetPathNameSafe(ExpectedClass)),
			TEXT("/Class"));
	}

	if (FreshAsset && FreshAsset->GetClass() == ExpectedClass)
	{
		const FAssetDocumentProfileResolution ProfileResolution =
			ResolveAssetDocumentProfile(FAssetDocumentService::GetProfileRegistry(), FreshAsset->GetClass());
		const TSet<FName> ManagedPropertyNames =
			FAssetDocumentManagedPropertyPartition::CollectTopLevelPropertyNames(ProfileResolution.ExactProfile.Get());
		const TSharedPtr<FJsonObject> ExtractedProperties =
			FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(
				FreshAsset,
				false,
				&ManagedPropertyNames);
		if (!ExtractedProperties.IsValid())
		{
			FailVerification(
				TEXT("AssetDocumentFreshExtractFailed"),
				TEXT("Fresh asset reflected Properties could not be canonically extracted"),
				TEXT("/Properties"));
		}

		TSharedPtr<FJsonObject> DiffDocument = MakeDiffDocument(AuthoredDocument);
		const TSharedPtr<FJsonObject>* AuthoredProperties = nullptr;
		if (DiffDocument.IsValid()
			&& DiffDocument->TryGetObjectField(TEXT("Properties"), AuthoredProperties)
			&& AuthoredProperties
			&& AuthoredProperties->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*AuthoredProperties)->Values)
			{
				FProperty* Property = FindFProperty<FProperty>(FreshAsset->GetClass(), *Pair.Key);
				if (!Property)
				{
					FailVerification(
						TEXT("AssetDocumentFreshDiffMissingProperty"),
						FString::Printf(TEXT("Fresh asset is missing authored property '%s'"), *Pair.Key),
						FString::Printf(TEXT("/Properties/%s"), *Pair.Key));
					continue;
				}

				const void* BeforeValuePtr = Property->ContainerPtrToValuePtr<void>(FreshAsset);
				const TSharedPtr<FJsonValue> BeforeValue =
					FAssetDocumentPropertyAdapter::ExtractPropertyValue(Property, BeforeValuePtr);
				UObject* PreviewAsset = DuplicateObject<UObject>(FreshAsset, GetTransientPackage());
				TSharedPtr<FJsonObject> SingleProperty = MakeShared<FJsonObject>();
				SingleProperty->SetField(Pair.Key, Pair.Value);
				const FAssetDocumentPropertyApplyResult ApplyResult = PreviewAsset
					? FAssetDocumentPropertyAdapter::ApplyProperties(PreviewAsset, SingleProperty)
					: FAssetDocumentPropertyApplyResult();
				const void* AfterValuePtr = PreviewAsset
					? Property->ContainerPtrToValuePtr<void>(PreviewAsset)
					: nullptr;
				const TSharedPtr<FJsonValue> AfterValue = AfterValuePtr
					? FAssetDocumentPropertyAdapter::ExtractPropertyValue(Property, AfterValuePtr)
					: nullptr;
				if (!BeforeValue.IsValid()
					|| !PreviewAsset
					|| !ApplyResult.bSuccess
					|| !AfterValue.IsValid())
				{
					FailVerification(
						TEXT("AssetDocumentFreshDiffPropertyFailed"),
						ApplyResult.Message.IsEmpty()
							? FString::Printf(TEXT("Fresh diff failed for authored property '%s'"), *Pair.Key)
							: ApplyResult.Message,
						FString::Printf(TEXT("/Properties/%s"), *Pair.Key));
				}
				else if (JsonValueToComparableString(BeforeValue) != JsonValueToComparableString(AfterValue))
				{
					FailVerification(
						TEXT("AssetDocumentFreshDiffChanged"),
						FString::Printf(TEXT("Fresh diff reports authored property '%s' as changed"), *Pair.Key),
						FString::Printf(TEXT("/Properties/%s"), *Pair.Key));
				}
			}
		}

		const TSharedPtr<FJsonValue>* BodyValue = DiffDocument.IsValid()
			? DiffDocument->Values.Find(TEXT("Body"))
			: nullptr;
		if (BodyValue && BodyValue->IsValid())
		{
			const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile.IsValid()
				? ProfileResolution.ExactProfile->ResolveBodyAdapter(TEXT("Body"))
				: nullptr;
			if (!BodyAdapter)
			{
				FailVerification(
					TEXT("AssetDocumentFreshExtractMissingBodyAdapter"),
					TEXT("Fresh verification requires an exact Body adapter"),
					TEXT("/Body"));
			}
			else
			{
				TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
				FAssetDocumentCapabilityContext CapabilityContext;
				CapabilityContext.Asset = FreshAsset;
				CapabilityContext.AssetClass = FreshAsset->GetClass();
				CapabilityContext.TargetAssetPath = Target;
				const TSharedPtr<FJsonObject>* Definitions = nullptr;
				DiffDocument->TryGetObjectField(TEXT("Definitions"), Definitions);
				CapabilityContext.Definitions = Definitions;
				const FAssetDocumentCapabilityResult ExtractResult = BodyAdapter->Extract(
					CapabilityContext,
					ExtractedBody);
				if (!ExtractResult.bSuccess)
				{
					FailVerification(
						TEXT("AssetDocumentFreshExtractFailed"),
						ExtractResult.Message.IsEmpty()
							? TEXT("Fresh Body canonical extraction failed")
							: ExtractResult.Message,
						TEXT("/Body"));
				}

				CapabilityContext.bIsDryRun = true;
				TArray<TSharedPtr<FJsonValue>> BodyDiffEntries;
				const FAssetDocumentCapabilityResult DiffResult = BodyAdapter->Diff(
					CapabilityContext,
					BodyValue->ToSharedRef(),
					BodyDiffEntries);
				if (!DiffResult.bSuccess)
				{
					FailVerification(
						TEXT("AssetDocumentFreshDiffFailed"),
						DiffResult.Message.IsEmpty() ? TEXT("Fresh Body diff failed") : DiffResult.Message,
						TEXT("/Body"));
				}
				for (const TSharedPtr<FJsonValue>& EntryValue : BodyDiffEntries)
				{
					const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
					FString Status;
					FString Path = TEXT("/Body");
					if (Entry.IsValid())
					{
						Entry->TryGetStringField(TEXT("status"), Status);
						Entry->TryGetStringField(TEXT("path"), Path);
					}
					if (!Entry.IsValid() || Status != TEXT("unchanged"))
					{
						FailVerification(
							TEXT("AssetDocumentFreshDiffNonEmpty"),
							FString::Printf(
								TEXT("Fresh Body diff contains non-empty status '%s' at '%s'"),
								*Status,
								*Path),
							Path);
					}
				}
			}
		}
	}

	FString UnloadError;
	if (!AssetDocumentPersistenceUnloadFreshPackage(FreshPackage, UnloadError))
	{
		FailVerification(
			TEXT("AssetDocumentFreshReloadCleanupFailed"),
			UnloadError,
			TEXT("/Apply/Persistence"));
	}
	return bVerified;
}

FAssetDocumentResult MakeCapabilityValidationFailure(const FAssetDocumentCapabilityResult& CapabilityResult, const FString& Target, const FString& NormalizedFilePath)
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(CapabilityResult.Message);
	Result.Target = Target;
	Result.SidecarFilePath = NormalizedFilePath;
	Result.Diagnostics = CapabilityResult.Diagnostics;
	if (CapabilityResult.Payload.IsValid())
	{
		Result.Payload = CapabilityResult.Payload;
	}
	return Result;
}

FAssetDocumentResult CheckBodyAdapters(
	const TSharedPtr<FJsonObject>& Document,
	const FAssetDocumentProfileResolution& ProfileResolution,
	UObject* ExistingAsset,
	UClass* ResolvedClass,
	const FString& Target,
	const FString& NormalizedFilePath,
	const TSharedPtr<FJsonObject>* DefinitionsPtr,
	bool bRunPreflight,
	TSharedPtr<FJsonValue>* OutBodyValue = nullptr,
	TArray<const IAssetDocumentCapability*>* OutBodyAdapters = nullptr)
{
	if (OutBodyValue)
	{
		OutBodyValue->Reset();
	}
	if (OutBodyAdapters)
	{
		OutBodyAdapters->Reset();
	}

	if (!Document->HasField(TEXT("Body")))
	{
		return FAssetDocumentResult::Success(TEXT("AssetDocument has no Body to check"));
	}

	const TSharedPtr<FJsonValue> BodyValue = Document->TryGetField(TEXT("Body"));
	if (!BodyValue.IsValid())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Body is required when present"));
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	const TSharedPtr<FJsonObject> BodyObject = BodyValue->AsObject();
	if (!BodyObject.IsValid())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Body must be a JSON object"));
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedFilePath;
		FAssetDocumentDiagnostic Diagnostic;
		Diagnostic.Path = TEXT("/Body");
		Diagnostic.Code = TEXT("InvalidBodyType");
		Diagnostic.Message = Result.Message;
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
		return Result;
	}

	if (!ProfileResolution.ExactProfile.IsValid())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(
			FString::Printf(TEXT("Body is not supported for class '%s'"), *ResolvedClass->GetPathName()));
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedFilePath;
		FAssetDocumentDiagnostic Diagnostic;
		Diagnostic.Path = TEXT("/Body");
		Diagnostic.Code = TEXT("MissingProfile");
		Diagnostic.Message = Result.Message;
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
		return Result;
	}

	TArray<const IAssetDocumentCapability*> BodyAdapters;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile->ResolveBodyAdapter(FName(*Pair.Key));
		if (!BodyAdapter)
		{
			// Let the profile's whole-Body adapter preserve its canonical unknown-key
			// diagnostic instead of converting it into a service-level adapter error.
			BodyAdapter = ProfileResolution.ExactProfile->ResolveBodyAdapter(TEXT("Body"));
		}
		if (!BodyAdapter)
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(
				FString::Printf(
					TEXT("Profile for class '%s' does not provide Body adapter for '%s'"),
					*ResolvedClass->GetPathName(),
					*Pair.Key));
			Result.Target = Target;
			Result.SidecarFilePath = NormalizedFilePath;
			FAssetDocumentDiagnostic Diagnostic;
			Diagnostic.Path = FString::Printf(TEXT("/Body/%s"), *Pair.Key);
			Diagnostic.Code = TEXT("MissingBodyAdapter");
			Diagnostic.Message = Result.Message;
			Result.Diagnostics.Add(MoveTemp(Diagnostic));
			return Result;
		}
		BodyAdapters.AddUnique(BodyAdapter);
	}

	if (BodyAdapters.Num() == 0)
	{
		if (const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile->ResolveBodyAdapter(TEXT("Body")))
		{
			BodyAdapters.Add(BodyAdapter);
		}
	}

	if (BodyAdapters.Num() == 0)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(
			FString::Printf(TEXT("Profile for class '%s' does not provide Body validation"), *ResolvedClass->GetPathName()));
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedFilePath;
		FAssetDocumentDiagnostic Diagnostic;
		Diagnostic.Path = TEXT("/Body");
		Diagnostic.Code = TEXT("MissingBodyAdapter");
		Diagnostic.Message = Result.Message;
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
		return Result;
	}

	for (const IAssetDocumentCapability* BodyAdapter : BodyAdapters)
	{
		FAssetDocumentCapabilityContext CapabilityContext;
		CapabilityContext.Asset = ExistingAsset;
		CapabilityContext.AssetClass = ResolvedClass;
		CapabilityContext.TargetAssetPath = Target;
		CapabilityContext.SourceDocumentPath = NormalizedFilePath;
		CapabilityContext.Definitions = DefinitionsPtr;
		CapabilityContext.bIsDryRun = true;

		const FAssetDocumentCapabilityResult CapabilityResult = bRunPreflight
			? BodyAdapter->Preflight(CapabilityContext, BodyValue.ToSharedRef())
			: BodyAdapter->Validate(CapabilityContext, BodyValue.ToSharedRef());
		if (!CapabilityResult.bSuccess)
		{
			return MakeCapabilityValidationFailure(CapabilityResult, Target, NormalizedFilePath);
		}
	}

	if (OutBodyValue)
	{
		*OutBodyValue = BodyValue;
	}
	if (OutBodyAdapters)
	{
		*OutBodyAdapters = MoveTemp(BodyAdapters);
	}
	return FAssetDocumentResult::Success(bRunPreflight
		? TEXT("AssetDocument Body preflight succeeded")
		: TEXT("AssetDocument Body validation succeeded"));
}

class FScopedAssetDocumentStagedObject
{
public:
	~FScopedAssetDocumentStagedObject()
	{
		Asset.Reset();
		FAssetDocumentLifecycle::DiscardTransientPreview(LifecycleResult);
	}

	UObject* Get() const
	{
		return Asset.Get();
	}

	void Reset(UObject* InAsset)
	{
		LifecycleResult = {};
		LifecycleResult.Asset = InAsset;
		Asset.Reset(InAsset);
	}

	void Reset(const FAssetDocumentLifecycleResult& InLifecycleResult)
	{
		LifecycleResult = InLifecycleResult;
		Asset.Reset(InLifecycleResult.Asset);
	}

private:
	TStrongObjectPtr<UObject> Asset;
	FAssetDocumentLifecycleResult LifecycleResult;
};

FAssetDocumentResult StageCompleteApplyDocument(
	const TSharedPtr<FJsonObject>& Document,
	UObject* ExistingAsset,
	UClass* ResolvedClass,
	const FString& Target,
	const FString& NormalizedFilePath,
	const TSharedPtr<FJsonObject>* DefinitionsPtr,
	const TSharedPtr<FJsonObject>& Properties,
	const TSharedPtr<FJsonValue>& BodyValue,
	const TArray<const IAssetDocumentCapability*>& BodyAdapters)
{
	FScopedAssetDocumentStagedObject Staged;
	if (ExistingAsset)
	{
		const FName PreviewName = MakeUniqueObjectName(
			GetTransientPackage(),
			ExistingAsset->GetClass(),
			ExistingAsset->GetFName());
		Staged.Reset(DuplicateObject<UObject>(
			ExistingAsset,
			GetTransientPackage(),
			PreviewName));
		if (Staged.Get())
		{
			Staged.Get()->ClearFlags(RF_Public | RF_Standalone);
			Staged.Get()->SetFlags(RF_Transient);
		}
	}
	else
	{
		FAssetDocumentLifecycleResult PreviewResult =
			FAssetDocumentLifecycle::CreateTransientPreview(Target, ResolvedClass, Document);
		Staged.Reset(PreviewResult);
		if (!PreviewResult.Error.IsEmpty())
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(PreviewResult.Error);
			Result.Target = Target;
			Result.AssetPath = PreviewResult.ObjectPath;
			Result.SidecarFilePath = NormalizedFilePath;
			return Result;
		}
	}

	if (!Staged.Get())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(
			TEXT("Failed to create complete-document transient staging asset"));
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	const FAssetDocumentPropertyApplyResult PropertyResult =
		FAssetDocumentPropertyAdapter::ApplyProperties(Staged.Get(), Properties);
	if (!PropertyResult.bSuccess)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(PropertyResult.Message);
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedFilePath;
		Result.Diagnostics = PropertyResult.Diagnostics;
		return Result;
	}

	if (Document->HasField(TEXT("Body")))
	{
		for (const IAssetDocumentCapability* BodyAdapter : BodyAdapters)
		{
			FAssetDocumentCapabilityContext CapabilityContext;
			CapabilityContext.Asset = Staged.Get();
			CapabilityContext.AssetClass = ResolvedClass;
			CapabilityContext.TargetAssetPath = Target;
			CapabilityContext.SourceDocumentPath = NormalizedFilePath;
			CapabilityContext.Definitions = DefinitionsPtr;
			CapabilityContext.bIsDryRun = false;

			FAssetDocumentCapabilityResult CapabilityResult =
				const_cast<IAssetDocumentCapability*>(BodyAdapter)->Apply(
					CapabilityContext,
					BodyValue.ToSharedRef());
			if (!CapabilityResult.bSuccess)
			{
				return MakeCapabilityValidationFailure(CapabilityResult, Target, NormalizedFilePath);
			}

			TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
			CapabilityResult = BodyAdapter->Extract(CapabilityContext, ExtractedBody);
			if (!CapabilityResult.bSuccess)
			{
				return MakeCapabilityValidationFailure(CapabilityResult, Target, NormalizedFilePath);
			}
		}
	}

	return FAssetDocumentResult::Success(TEXT("Complete AssetDocument staged and verified"));
}

void RollbackAssetDocumentApply(
	FAssetDocumentApplyTransaction& Transaction,
	FAssetDocumentResult& Result)
{
	TArray<FAssetDocumentDiagnostic> RollbackDiagnostics;
	Transaction.Rollback(RollbackDiagnostics);
	Result.Diagnostics.Append(RollbackDiagnostics);
}

void EnsureAssetDocumentLifecyclePrimaryDiagnostic(FAssetDocumentResult& Result)
{
	if (Result.Diagnostics.Num() > 0)
	{
		return;
	}

	FAssetDocumentDiagnostic Diagnostic;
	Diagnostic.Path = TEXT("/Target");
	Diagnostic.Code = TEXT("AssetDocumentLifecycleFailed");
	Diagnostic.Message = Result.Message;
	Result.Diagnostics.Add(MoveTemp(Diagnostic));
}

FAssetDocumentResult ValidateGenericAssetDocument(TSharedPtr<FJsonObject> Document, const FString& NormalizedFilePath, bool bPreflightProperties)
{
	if (!Document.IsValid())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Validate requires a JSON document or sidecar file path"));
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	FString Target;
	auto MakeFailure = [&Target, &NormalizedFilePath](const FString& Message)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Message);
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	};

	double SchemaVersion = 0.0;
	if (!Document->TryGetNumberField(TEXT("SchemaVersion"), SchemaVersion) || SchemaVersion != 1.0)
	{
		return MakeFailure(TEXT("SchemaVersion must be 1"));
	}

	const bool bHasStructuredShape = Document->HasField(TEXT("Body")) || Document->HasField(TEXT("Definitions"));
	FString AssetType;
	if (Document->TryGetStringField(TEXT("AssetType"), AssetType))
	{
		if (AssetType != TEXT("GenericAsset"))
		{
			return MakeFailure(TEXT("AssetType must be GenericAsset"));
		}
	}
	else if (!bHasStructuredShape)
	{
		return MakeFailure(TEXT("AssetType must be GenericAsset"));
	}

	if (!Document->TryGetStringField(TEXT("Target"), Target) || Target.IsEmpty())
	{
		return MakeFailure(TEXT("Target is required"));
	}
	Target = NormalizeValidateTarget(Target);

	FString Error;
	if (!ValidateApplyTarget(Target, Error))
	{
		return MakeFailure(Error);
	}

	if (!FAssetDocumentSidecar::ValidateTargetMatchesSidecar(NormalizedFilePath, Document, Error))
	{
		return MakeFailure(Error);
	}

	FString ClassName;
	if (!Document->TryGetStringField(TEXT("Class"), ClassName) || ClassName.IsEmpty())
	{
		return MakeFailure(TEXT("Class is required"));
	}

	FString ActionName;
	if (!Document->TryGetStringField(TEXT("Action"), ActionName) || ActionName.IsEmpty())
	{
		return MakeFailure(TEXT("Action is required"));
	}

	EAssetDocumentLifecycleAction Action;
	if (!FAssetDocumentLifecycle::TryParseAction(ActionName, Action, Error))
	{
		return MakeFailure(Error);
	}

	UClass* ResolvedClass = nullptr;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, Error))
	{
		return MakeFailure(Error);
	}

	const TSharedPtr<FJsonObject>* DefinitionsPtr = nullptr;
	if (Document->HasField(TEXT("Definitions")))
	{
		if (!Document->TryGetObjectField(TEXT("Definitions"), DefinitionsPtr) || !DefinitionsPtr)
		{
			return MakeFailure(TEXT("Definitions must be a JSON object"));
		}
	}

	TSharedPtr<FJsonObject> Properties;
	if (Document->HasField(TEXT("Properties")))
	{
		const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
		if (!Document->TryGetObjectField(TEXT("Properties"), PropertiesPtr) || !PropertiesPtr)
		{
			return MakeFailure(TEXT("Properties must be a JSON object"));
		}
		Properties = *PropertiesPtr;
	}

	const FAssetDocumentLifecycleResult LifecycleResult = FAssetDocumentLifecycle::Resolve(Target, ResolvedClass, Action);
	if (!LifecycleResult.Error.IsEmpty())
	{
		FAssetDocumentResult Result = MakeFailure(LifecycleResult.Error);
		Result.AssetPath = LifecycleResult.ObjectPath;
		return Result;
	}
	if (!LifecycleResult.Asset && !FAssetDocumentLifecycle::ValidateCreateDocument(ResolvedClass, Document, Error))
	{
		FAssetDocumentResult Result = MakeFailure(Error);
		Result.AssetPath = LifecycleResult.ObjectPath;
		return Result;
	}

	const FAssetDocumentProfileResolution ProfileResolution =
		ResolveAssetDocumentProfile(FAssetDocumentService::GetProfileRegistry(), ResolvedClass);
	const FAssetDocumentCapabilityResult ManagedPropertyResult =
		FAssetDocumentManagedPropertyPartition::ValidateTopLevelProperties(ProfileResolution.ExactProfile.Get(), Properties);
	if (!ManagedPropertyResult.bSuccess)
	{
		return MakeCapabilityValidationFailure(ManagedPropertyResult, Target, NormalizedFilePath);
	}

	if (bPreflightProperties)
	{
		FAssetDocumentPropertyApplyResult PreflightResult = FAssetDocumentPropertyAdapter::PreflightProperties(ResolvedClass, Properties);
		if (!PreflightResult.bSuccess)
		{
			FAssetDocumentResult Result = MakeFailure(PreflightResult.Message);
			Result.Diagnostics = PreflightResult.Diagnostics;
			return Result;
		}
	}

	const FAssetDocumentResult BodyResult = CheckBodyAdapters(
		Document,
		ProfileResolution,
		LifecycleResult.Asset,
		ResolvedClass,
		Target,
		NormalizedFilePath,
		DefinitionsPtr,
		false);
	if (!BodyResult.IsSuccess())
	{
		return BodyResult;
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument is valid"));
	Result.Target = Target;
	Result.SidecarFilePath = NormalizedFilePath;
	Result.Payload = MakeShared<FJsonObject>();
	Result.Payload->SetStringField(TEXT("target"), Target);
	Result.Payload->SetStringField(TEXT("sidecar_file_path"), NormalizedFilePath);
	return Result;
}
}

FAssetDocumentResult RegenerateSidecarRegionsFromAsset(
	UObject* Asset,
	const TSharedRef<FJsonObject>& SidecarDocument,
	const TArray<FName>& RegionIds,
	const FString& SourceDocumentPath)
{
	if (!Asset)
	{
		return FAssetDocumentResult::Failure(TEXT("RegenerateSidecarRegionsFromAsset requires an asset"));
	}

	if (RegionIds.Num() == 0)
	{
		return FAssetDocumentResult::Failure(TEXT("RegenerateSidecarRegionsFromAsset requires at least one region id"));
	}

	const FAssetDocumentProfileResolution ProfileResolution =
		ResolveAssetDocumentProfile(FAssetDocumentService::GetProfileRegistry(), Asset->GetClass());
	if (!ProfileResolution.ExactProfile.IsValid())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(
			FString::Printf(TEXT("No exact AssetDocument profile for '%s'"), *Asset->GetClass()->GetPathName()));
		Result.AssetPath = Asset->GetPathName();
		return Result;
	}

	TArray<FAssetDocumentRegionPolicy> RequestedPolicies;
	RequestedPolicies.Reserve(RegionIds.Num());
	for (const FName& RegionId : RegionIds)
	{
		FAssetDocumentRegionPolicy Policy;
		if (!ProfileResolution.ExactProfile->GetRegionPolicy(RegionId, Policy))
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(
				FString::Printf(TEXT("Requested region '%s' is not declared by profile '%s'"), *RegionId.ToString(), *Asset->GetClass()->GetPathName()));
			Result.AssetPath = Asset->GetPathName();
			return Result;
		}

		RequestedPolicies.Add(MoveTemp(Policy));
	}

	TSharedRef<FJsonObject> EvidenceDocument = MakeShared<FJsonObject>();
	FString Error;
	if (!ExtractAssetBodyEvidenceDocument(Asset, SidecarDocument, SourceDocumentPath, ProfileResolution, EvidenceDocument, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.AssetPath = Asset->GetPathName();
		return Result;
	}

	FAssetDocumentSyncState SyncState;
	if (!FAssetDocumentSyncStateStore::LoadFromDocumentJson(SidecarDocument, SyncState, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error.IsEmpty() ? TEXT("Malformed existing sync state") : Error);
		Result.AssetPath = Asset->GetPathName();
		return Result;
	}

	TSharedRef<FJsonObject> StagedDocument = CloneJsonObjectPreservingFields(SidecarDocument);
	SyncState.AssetObjectPath = Asset->GetPathName();
	SyncState.UpdatedAtUtc = FDateTime::UtcNow().ToIso8601();

	TArray<TSharedPtr<FJsonValue>> RegeneratedRegionIds;
	for (const FAssetDocumentRegionPolicy& Policy : RequestedPolicies)
	{
		const FAssetDocumentSidecarRegionValue EvidenceRegion = FAssetDocumentSidecarDelta::FindRegionValue(EvidenceDocument, Policy);
		if (EvidenceRegion.State == EAssetDocumentSidecarRegionState::Unset)
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(
				FString::Printf(TEXT("Asset evidence for requested region '%s' is missing"), *Policy.RegionId.ToString()));
			Result.AssetPath = Asset->GetPathName();
			return Result;
		}

		const FString AssetEvidenceHash = FAssetDocumentSidecarDelta::HashSidecarRegion(
			EvidenceDocument,
			Policy,
			EAssetDocumentRegionCanonicalizeSource::AssetEvidence,
			Asset->GetClass());
		if (AssetEvidenceHash.IsEmpty())
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(
				FString::Printf(TEXT("Asset evidence hash is empty for requested region '%s'"), *Policy.RegionId.ToString()));
			Result.AssetPath = Asset->GetPathName();
			return Result;
		}

		const TSharedPtr<FJsonValue> RegionValue = FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(EvidenceRegion.Value, &Policy);
		if (!FAssetDocumentSidecarDelta::SetRegionValue(StagedDocument, Policy, RegionValue, Error))
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error.IsEmpty()
				? FString::Printf(TEXT("Failed to set requested region '%s'"), *Policy.RegionId.ToString())
				: Error);
			Result.AssetPath = Asset->GetPathName();
			return Result;
		}

		const FString SidecarHash = FAssetDocumentSidecarDelta::HashSidecarRegion(
			StagedDocument,
			Policy,
			EAssetDocumentRegionCanonicalizeSource::SidecarAuthored,
			Asset->GetClass());
		if (SidecarHash.IsEmpty())
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(
				FString::Printf(TEXT("Regenerated sidecar hash is empty for requested region '%s'"), *Policy.RegionId.ToString()));
			Result.AssetPath = Asset->GetPathName();
			return Result;
		}
		if (SidecarHash != AssetEvidenceHash)
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(
				FString::Printf(TEXT("Regenerated sidecar hash differs from asset evidence hash for requested region '%s'"), *Policy.RegionId.ToString()));
			Result.AssetPath = Asset->GetPathName();
			return Result;
		}

		FAssetDocumentRegionSyncState RegionState;
		RegionState.SidecarHash = SidecarHash;
		RegionState.AssetEvidenceHash = AssetEvidenceHash;
		RegionState.LastSyncedAtUtc = SyncState.UpdatedAtUtc;
		FAssetDocumentSyncStateStore::UpdateRegionState(SyncState, Policy.RegionId, RegionState);
		RegeneratedRegionIds.Add(MakeShared<FJsonValueString>(Policy.RegionId.ToString()));
	}

	FAssetDocumentSyncStateStore::WriteToDocumentJson(StagedDocument, SyncState);
	SidecarDocument->Values = StagedDocument->Values;

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument sidecar regions regenerated from asset"));
	Result.Target = NormalizeValidateTarget(Asset->GetPathName());
	Result.AssetPath = Asset->GetPathName();
	Result.SidecarFilePath = SourceDocumentPath;
	Result.Payload = MakeShared<FJsonObject>();
	Result.Payload->SetArrayField(TEXT("regenerated_regions"), MoveTemp(RegeneratedRegionIds));
	return Result;
}

FAssetDocumentResult FAssetDocumentResult::Success(const FString& InMessage)
{
	FAssetDocumentResult Result;
	Result.Status = EAssetDocumentResultStatus::Success;
	Result.Message = InMessage;
	return Result;
}

FAssetDocumentResult FAssetDocumentResult::Failure(const FString& InMessage)
{
	FAssetDocumentResult Result;
	Result.Status = EAssetDocumentResultStatus::Failed;
	Result.Message = InMessage;
	return Result;
}

TSharedPtr<FJsonObject> FAssetDocumentResult::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("success"), IsSuccess());
	Json->SetStringField(TEXT("message"), Message);
	Json->SetStringField(TEXT("target"), Target);
	Json->SetStringField(TEXT("asset_path"), AssetPath);
	Json->SetStringField(TEXT("sidecar_file_path"), SidecarFilePath);
	Json->SetBoolField(TEXT("saved_asset"), bSavedAsset);
	Json->SetBoolField(TEXT("wrote_sidecar"), bWroteSidecar);

	TArray<TSharedPtr<FJsonValue>> DiagnosticValues;
	for (const FAssetDocumentDiagnostic& Diagnostic : Diagnostics)
	{
		TSharedPtr<FJsonObject> DiagnosticJson = MakeShared<FJsonObject>();
		DiagnosticJson->SetStringField(TEXT("path"), Diagnostic.Path);
		DiagnosticJson->SetStringField(TEXT("code"), Diagnostic.Code);
		DiagnosticJson->SetStringField(TEXT("message"), Diagnostic.Message);
		DiagnosticValues.Add(MakeShared<FJsonValueObject>(DiagnosticJson));
	}
	Json->SetArrayField(TEXT("diagnostics"), DiagnosticValues);

	if (Payload.IsValid())
	{
		Json->SetObjectField(TEXT("payload"), Payload);
	}

	return Json;
}

FAssetDocumentProfileRegistry& FAssetDocumentService::GetProfileRegistry()
{
	static FAssetDocumentProfileRegistry Registry;
	return Registry;
}

FAssetDocumentResult FAssetDocumentService::Apply(const FAssetDocumentApplyRequest& Request)
{
	if (!Request.Document.IsValid())
	{
		return FAssetDocumentResult::Failure(TEXT("Apply requires a JSON document"));
	}

	const FString NormalizedSourceDocumentPath = NormalizeValidateFilePath(Request.SourceDocumentPath);

	double SchemaVersion = 0.0;
	if (!Request.Document->TryGetNumberField(TEXT("SchemaVersion"), SchemaVersion) || SchemaVersion != 1.0)
	{
		return FAssetDocumentResult::Failure(TEXT("SchemaVersion must be 1"));
	}

	const bool bHasStructuredShape = Request.Document->HasField(TEXT("Body")) || Request.Document->HasField(TEXT("Definitions"));
	FString AssetType;
	if (Request.Document->TryGetStringField(TEXT("AssetType"), AssetType))
	{
		if (AssetType != TEXT("GenericAsset"))
		{
			return FAssetDocumentResult::Failure(TEXT("AssetType must be GenericAsset"));
		}
	}
	else if (!bHasStructuredShape)
	{
		return FAssetDocumentResult::Failure(TEXT("AssetType must be GenericAsset"));
	}

	FString Target;
	if (!Request.Document->TryGetStringField(TEXT("Target"), Target) || Target.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("Target is required"));
	}
	Target = NormalizeValidateTarget(Target);

	FString Error;
	if (!ValidateApplyTarget(Target, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Target;
		return Result;
	}

	FString ClassName;
	if (!Request.Document->TryGetStringField(TEXT("Class"), ClassName) || ClassName.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Class is required"));
		Result.Target = Target;
		return Result;
	}

	FString ActionName;
	if (!Request.Document->TryGetStringField(TEXT("Action"), ActionName) || ActionName.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Action is required"));
		Result.Target = Target;
		return Result;
	}

	EAssetDocumentLifecycleAction Action;
	if (!FAssetDocumentLifecycle::TryParseAction(ActionName, Action, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Target;
		return Result;
	}

	UClass* ResolvedClass = nullptr;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Target;
		return Result;
	}

	const TSharedPtr<FJsonObject>* DefinitionsPtr = nullptr;
	if (Request.Document->HasField(TEXT("Definitions")))
	{
		if (!Request.Document->TryGetObjectField(TEXT("Definitions"), DefinitionsPtr) || !DefinitionsPtr)
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Definitions must be a JSON object"));
			Result.Target = Target;
			Result.SidecarFilePath = NormalizedSourceDocumentPath;
			return Result;
		}
	}

	TSharedPtr<FJsonObject> Properties;
	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (Request.Document->TryGetObjectField(TEXT("Properties"), PropertiesPtr) && PropertiesPtr)
	{
		Properties = *PropertiesPtr;
	}

	const FAssetDocumentLifecycleResult LifecyclePreflight = FAssetDocumentLifecycle::Resolve(Target, ResolvedClass, Action);
	if (!LifecyclePreflight.Error.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(LifecyclePreflight.Error);
		Result.Target = Target;
		Result.AssetPath = LifecyclePreflight.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		return Result;
	}
	if (!LifecyclePreflight.Asset && !FAssetDocumentLifecycle::ValidateCreateDocument(ResolvedClass, Request.Document, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Target;
		Result.AssetPath = LifecyclePreflight.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		return Result;
	}
	const FAssetDocumentProfileResolution ProfileResolution =
		ResolveAssetDocumentProfile(FAssetDocumentService::GetProfileRegistry(), ResolvedClass);
	const FAssetDocumentCapabilityResult ManagedPropertyResult =
		FAssetDocumentManagedPropertyPartition::ValidateTopLevelProperties(ProfileResolution.ExactProfile.Get(), Properties);
	if (!ManagedPropertyResult.bSuccess)
	{
		return MakeCapabilityValidationFailure(ManagedPropertyResult, Target, NormalizedSourceDocumentPath);
	}

	FAssetDocumentPropertyApplyResult PreflightResult = FAssetDocumentPropertyAdapter::PreflightProperties(ResolvedClass, Properties);
	if (!PreflightResult.bSuccess)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(PreflightResult.Message);
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		Result.Diagnostics = PreflightResult.Diagnostics;
		return Result;
	}

	TSharedPtr<FJsonValue> BodyValue;
	TArray<const IAssetDocumentCapability*> BodyAdapters;
	FAssetDocumentResult BodyPreflightResult = CheckBodyAdapters(
		Request.Document,
		ProfileResolution,
		LifecyclePreflight.Asset,
		ResolvedClass,
		Target,
		NormalizedSourceDocumentPath,
		DefinitionsPtr,
		true,
		&BodyValue,
		&BodyAdapters);
	if (!BodyPreflightResult.IsSuccess())
	{
		BodyPreflightResult.AssetPath = LifecyclePreflight.ObjectPath;
		return BodyPreflightResult;
	}

	FAssetDocumentResult StagingResult = StageCompleteApplyDocument(
		Request.Document,
		LifecyclePreflight.Asset,
		ResolvedClass,
		Target,
		NormalizedSourceDocumentPath,
		DefinitionsPtr,
		Properties,
		BodyValue,
		BodyAdapters);
	if (!StagingResult.IsSuccess())
	{
		if (StagingResult.AssetPath.IsEmpty())
		{
			StagingResult.AssetPath = LifecyclePreflight.ObjectPath;
		}
		return StagingResult;
	}

#if WITH_DEV_AUTOMATION_TESTS
	FAssetDocumentDiagnostic ForcedPhaseDiagnostic;
	uint64 ForcedFailureGeneration = 0;
	if (FAssetDocumentServiceTestHooks::ConsumeApplyFailure(
		EAssetDocumentServiceApplyPhase::AfterStagedDocument,
		ForcedPhaseDiagnostic,
		&ForcedFailureGeneration))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(ForcedPhaseDiagnostic.Message);
		Result.Target = Target;
		Result.AssetPath = LifecyclePreflight.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		Result.Diagnostics.Add(MoveTemp(ForcedPhaseDiagnostic));
		return Result;
	}
#endif

	if (Request.bSaveAsset
		&& LifecyclePreflight.Asset
		&& LifecyclePreflight.Asset->GetOutermost()
		&& LifecyclePreflight.Asset->GetOutermost()->IsDirty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(
			FString::Printf(
				TEXT("Existing asset '%s' has unsaved package changes; transactional Apply fails closed"),
				*LifecyclePreflight.ObjectPath));
		Result.Target = Target;
		Result.AssetPath = LifecyclePreflight.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		FAssetDocumentDiagnostic Diagnostic;
		Diagnostic.Path = TEXT("/Target");
		Diagnostic.Code = TEXT("DirtyExistingAssetTransactionUnsupported");
		Diagnostic.Message = Result.Message;
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
		return Result;
	}

	const TSet<FName> ManagedPropertyNames =
		FAssetDocumentManagedPropertyPartition::CollectTopLevelPropertyNames(ProfileResolution.ExactProfile.Get());
	FAssetDocumentApplyTransaction Transaction(LifecyclePreflight.Asset, ManagedPropertyNames);

	FAssetDocumentLifecycleResult LifecycleResult = FAssetDocumentLifecycle::CreateOrLoad(Target, ResolvedClass, Action, Request.Document);
	Transaction.AttachLifecycleResult(LifecycleResult);
	if (!LifecycleResult.Error.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(LifecycleResult.Error);
		Result.Target = Target;
		Result.AssetPath = LifecycleResult.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		Result.Diagnostics = LifecycleResult.Diagnostics;
		EnsureAssetDocumentLifecyclePrimaryDiagnostic(Result);
		RollbackAssetDocumentApply(Transaction, Result);
		return Result;
	}

	if (!LifecycleResult.Asset)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(
			LifecycleResult.Error.IsEmpty() ? TEXT("Asset lifecycle did not resolve or create an asset") : LifecycleResult.Error);
		Result.Target = Target;
		Result.AssetPath = LifecycleResult.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		Result.Diagnostics = LifecycleResult.Diagnostics;
		EnsureAssetDocumentLifecyclePrimaryDiagnostic(Result);
		RollbackAssetDocumentApply(Transaction, Result);
		return Result;
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (LifecycleResult.bCreated
		&& FAssetDocumentServiceTestHooks::ConsumeApplyFailure(
			EAssetDocumentServiceApplyPhase::AfterNewLiveMaterialize,
			ForcedPhaseDiagnostic,
			&ForcedFailureGeneration))
	{
		Transaction.BindForcedFailureGeneration(ForcedFailureGeneration);
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(ForcedPhaseDiagnostic.Message);
		Result.Target = Target;
		Result.AssetPath = LifecycleResult.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		Result.Diagnostics.Add(MoveTemp(ForcedPhaseDiagnostic));
		RollbackAssetDocumentApply(Transaction, Result);
		return Result;
	}
#endif

	FAssetDocumentPropertyApplyResult PropertyResult = FAssetDocumentPropertyAdapter::ApplyProperties(LifecycleResult.Asset, Properties);
	if (!PropertyResult.bSuccess)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(PropertyResult.Message);
		Result.Target = Target;
		Result.AssetPath = LifecycleResult.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		Result.Diagnostics = PropertyResult.Diagnostics;
		RollbackAssetDocumentApply(Transaction, Result);
		return Result;
	}

	TArray<FAssetDocumentDiagnostic> Diagnostics = PropertyResult.Diagnostics;
#if WITH_DEV_AUTOMATION_TESTS
	if (FAssetDocumentServiceTestHooks::ConsumeApplyFailure(
		EAssetDocumentServiceApplyPhase::AfterLiveProperties,
		ForcedPhaseDiagnostic,
		&ForcedFailureGeneration))
	{
		Transaction.BindForcedFailureGeneration(ForcedFailureGeneration);
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(ForcedPhaseDiagnostic.Message);
		Result.Target = Target;
		Result.AssetPath = LifecycleResult.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		Result.Diagnostics = Diagnostics;
		Result.Diagnostics.Add(MoveTemp(ForcedPhaseDiagnostic));
		RollbackAssetDocumentApply(Transaction, Result);
		return Result;
	}
#endif
	if (Request.Document->HasField(TEXT("Body")))
	{
		for (const IAssetDocumentCapability* BodyAdapter : BodyAdapters)
		{
			FAssetDocumentCapabilityContext CapabilityContext;
			CapabilityContext.Asset = LifecycleResult.Asset;
			CapabilityContext.AssetClass = ResolvedClass;
			CapabilityContext.TargetAssetPath = Target;
			CapabilityContext.SourceDocumentPath = NormalizedSourceDocumentPath;
			CapabilityContext.Definitions = DefinitionsPtr;
			CapabilityContext.bIsDryRun = false;

			FAssetDocumentCapabilityResult CapabilityResult = const_cast<IAssetDocumentCapability*>(BodyAdapter)->Apply(CapabilityContext, BodyValue.ToSharedRef());
			if (!CapabilityResult.bSuccess)
			{
				FAssetDocumentResult Result = MakeCapabilityValidationFailure(CapabilityResult, Target, NormalizedSourceDocumentPath);
				Result.AssetPath = LifecycleResult.ObjectPath;
				Result.Diagnostics.Insert(Diagnostics, 0);
				RollbackAssetDocumentApply(Transaction, Result);
				return Result;
			}

			Diagnostics.Append(CapabilityResult.Diagnostics);
		}

#if WITH_DEV_AUTOMATION_TESTS
		if (LifecycleResult.bCreated
			&& FAssetDocumentServiceTestHooks::ConsumeApplyFailure(
				EAssetDocumentServiceApplyPhase::AfterNewLiveBody,
				ForcedPhaseDiagnostic,
				&ForcedFailureGeneration))
		{
			Transaction.BindForcedFailureGeneration(ForcedFailureGeneration);
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(ForcedPhaseDiagnostic.Message);
			Result.Target = Target;
			Result.AssetPath = LifecycleResult.ObjectPath;
			Result.SidecarFilePath = NormalizedSourceDocumentPath;
			Result.Diagnostics = Diagnostics;
			Result.Diagnostics.Add(MoveTemp(ForcedPhaseDiagnostic));
			RollbackAssetDocumentApply(Transaction, Result);
			return Result;
		}
#endif
	}
	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument applied"));
	TStrongObjectPtr<UObject> LiveAssetGuard(LifecycleResult.Asset);
	TStrongObjectPtr<UPackage> LivePackageGuard(LifecycleResult.Asset->GetOutermost());
	Result.Target = Target;
	Result.AssetPath = LifecycleResult.ObjectPath;
	Result.SidecarFilePath = NormalizedSourceDocumentPath;
	Result.Diagnostics = Diagnostics;
	Result.bWroteSidecar = false;
	Result.bSavedAsset = false;

	TSharedPtr<FJsonObject> UpdatedSidecarDocument;
	FString SidecarSyncUpdateSkipReason;
	EAssetDocumentApplyFileSyncStateBuildResult SidecarBuildResult = EAssetDocumentApplyFileSyncStateBuildResult::NotApplicable;
	if (Request.bWriteSidecar && Request.bSaveAsset)
	{
		SidecarBuildResult = AssetDocumentPersistenceBuildApplyFileSyncState(
			NormalizedSourceDocumentPath,
			LifecycleResult.Asset,
			Result,
			UpdatedSidecarDocument,
			SidecarSyncUpdateSkipReason);
	}
	else if (Request.bWriteSidecar && !Request.bSaveAsset)
	{
		SidecarSyncUpdateSkipReason = TEXT("Sidecar sync rewrite requires a durable asset save");
	}

	TUniquePtr<FAssetDocumentPersistenceTransaction> PersistenceTransaction;
	auto RollbackPersistence = [&Result, &PersistenceTransaction]()
	{
		if (!PersistenceTransaction || !PersistenceTransaction->HasInstalledFiles())
		{
			return;
		}
		TArray<FString> RollbackErrors;
		PersistenceTransaction->RollbackInstalledPackage(RollbackErrors);
		for (const FString& RollbackError : RollbackErrors)
		{
			AssetDocumentPersistenceAddDiagnostic(
				Result.Diagnostics,
				TEXT("AssetDocumentPersistenceRollbackFailed"),
				RollbackError);
		}
	};
	auto FailPersistence = [
		&Result,
		&Transaction,
		&RollbackPersistence,
		&Target,
		&NormalizedSourceDocumentPath,
		&LifecycleResult](
		const FString& Message,
		const FString& Code,
		const TOptional<FAssetDocumentDiagnostic>& PrimaryDiagnostic = TOptional<FAssetDocumentDiagnostic>()) mutable
	{
		Result.Status = EAssetDocumentResultStatus::Failed;
		Result.Message = Message;
		Result.Target = Target;
		Result.AssetPath = LifecycleResult.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		Result.bSavedAsset = false;
		Result.bWroteSidecar = false;
		if (PrimaryDiagnostic.IsSet())
		{
			Result.Diagnostics.Add(PrimaryDiagnostic.GetValue());
		}
		else
		{
			AssetDocumentPersistenceAddDiagnostic(Result.Diagnostics, Code, Message);
		}
		RollbackPersistence();
		RollbackAssetDocumentApply(Transaction, Result);
		return Result;
	};

	if (Request.bWriteSidecar
		&& Request.bSaveAsset
		&& AssetDocumentPersistenceRequiresStrictFreshVerification(ResolvedClass)
		&& (SidecarBuildResult != EAssetDocumentApplyFileSyncStateBuildResult::Updated
			|| !UpdatedSidecarDocument.IsValid()))
	{
		const FString SidecarPreparationError = SidecarSyncUpdateSkipReason.IsEmpty()
			? TEXT("Strict AssetDocument sidecar sync state could not be prepared")
			: SidecarSyncUpdateSkipReason;
		return FailPersistence(
			SidecarPreparationError,
			TEXT("AssetDocumentStrictSidecarSyncPreparationFailed"));
	}

	if (Request.bSaveAsset)
	{
		LifecycleResult.Asset->MarkPackageDirty();
		UPackage* Package = LifecycleResult.Asset->GetOutermost();
		if (!Package)
		{
			return FailPersistence(
				TEXT("AssetDocument persistence could not resolve the asset package"),
				TEXT("AssetDocumentPersistenceMissingPackage"));
		}
		const FString PackageName = Package->GetName();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

#if WITH_DEV_AUTOMATION_TESTS
		FAssetDocumentServiceTestHooks::ConsumePersistenceCallback(
			EAssetDocumentServicePersistencePhase::BeforePackageStageSave);
		FAssetDocumentDiagnostic ForcedPersistenceFailure;
		if (FAssetDocumentServiceTestHooks::ConsumePersistenceFailure(
			EAssetDocumentServicePersistencePhase::BeforePackageStageSave,
			ForcedPersistenceFailure))
		{
			return FailPersistence(
				ForcedPersistenceFailure.Message,
				ForcedPersistenceFailure.Code,
				ForcedPersistenceFailure);
		}
#endif

		PersistenceTransaction = MakeUnique<FAssetDocumentPersistenceTransaction>(
			PackageName,
			PackageFileName);
		FString PersistenceError;
		if (!PersistenceTransaction->StagePackage(
				Package,
				LifecycleResult.Asset,
				PersistenceError))
		{
			return FailPersistence(
				PersistenceError,
				TEXT("AssetDocumentPackageStageSaveFailed"));
		}

		if (AssetDocumentPersistenceRequiresStrictFreshVerification(ResolvedClass))
		{
			TArray<FAssetDocumentDiagnostic> VerificationDiagnostics;
			if (!AssetDocumentPersistenceVerifyFreshStagedDocument(
					PersistenceTransaction->GetStagedHeaderFilename(),
					PersistenceTransaction->GetCanonicalHeaderFilename(),
					Target,
					ResolvedClass,
					Request.Document,
					VerificationDiagnostics,
					PersistenceError))
			{
				Result.Diagnostics.Append(VerificationDiagnostics);
				return FailPersistence(
					PersistenceError.IsEmpty()
						? TEXT("Fresh staged AssetDocument verification failed")
						: PersistenceError,
					TEXT("AssetDocumentFreshVerificationFailed"));
			}
		}

		if (!PersistenceTransaction->InstallStagedPackage(PersistenceError))
		{
			return FailPersistence(
				PersistenceError,
				TEXT("AssetDocumentPackageInstallFailed"));
		}

#if WITH_DEV_AUTOMATION_TESTS
		FAssetDocumentServiceTestHooks::ConsumePersistenceCallback(
			EAssetDocumentServicePersistencePhase::AfterPackageInstall);
		if (FAssetDocumentServiceTestHooks::ConsumePersistenceFailure(
			EAssetDocumentServicePersistencePhase::AfterPackageInstall,
			ForcedPersistenceFailure))
		{
			return FailPersistence(
				ForcedPersistenceFailure.Message,
				ForcedPersistenceFailure.Code,
				ForcedPersistenceFailure);
		}
#endif
		Result.bSavedAsset = true;
	}

	if (Request.bWriteSidecar
		&& Request.bSaveAsset
		&& SidecarBuildResult == EAssetDocumentApplyFileSyncStateBuildResult::Updated
		&& UpdatedSidecarDocument.IsValid())
	{
#if WITH_DEV_AUTOMATION_TESTS
		FAssetDocumentServiceTestHooks::ConsumePersistenceCallback(
			EAssetDocumentServicePersistencePhase::BeforeSidecarWrite);
		FAssetDocumentDiagnostic ForcedSidecarFailure;
		if (FAssetDocumentServiceTestHooks::ConsumePersistenceFailure(
			EAssetDocumentServicePersistencePhase::BeforeSidecarWrite,
			ForcedSidecarFailure))
		{
			return FailPersistence(
				ForcedSidecarFailure.Message,
				ForcedSidecarFailure.Code,
				ForcedSidecarFailure);
		}
#endif
		FString SidecarWriteError;
		FAssetDocumentEditorSync::FScopedSidecarWrite Guard(NormalizedSourceDocumentPath);
		if (!FAssetDocumentSidecar::WriteJsonFile(
				NormalizedSourceDocumentPath,
				UpdatedSidecarDocument,
				SidecarWriteError))
		{
			return FailPersistence(
				SidecarWriteError.IsEmpty()
					? TEXT("Failed to atomically write AssetDocument sidecar sync state")
					: SidecarWriteError,
				TEXT("AssetDocumentSidecarAtomicWriteFailed"));
		}
		Result.bWroteSidecar = true;
	}

	if (Result.bSavedAsset && LifecycleResult.Asset && LifecycleResult.Asset->GetOutermost())
	{
		LifecycleResult.Asset->GetOutermost()->SetDirtyFlag(false);
	}
	if (!SidecarSyncUpdateSkipReason.IsEmpty())
	{
		if (!Result.Payload.IsValid())
		{
			Result.Payload = MakeShared<FJsonObject>();
		}
		Result.Payload->SetBoolField(TEXT("sidecar_sync_update_skipped"), true);
		Result.Payload->SetStringField(
			TEXT("sidecar_sync_update_skip_reason"),
			SidecarSyncUpdateSkipReason);
	}
	if (PersistenceTransaction)
	{
		PersistenceTransaction->Commit();
	}
	Transaction.Commit();
	return Result;
}

FAssetDocumentResult FAssetDocumentService::ApplyFile(const FAssetDocumentApplyFileRequest& Request)
{
	const FString NormalizedFilePath = NormalizeValidateFilePath(Request.FilePath);
	if (NormalizedFilePath.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("ApplyFile requires a sidecar file path"));
	}

	TSharedPtr<FJsonObject> Document;
	FString Error;
	if (!FAssetDocumentSidecar::LoadJsonFile(NormalizedFilePath, Document, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	if (!FAssetDocumentSidecar::ValidateTargetMatchesSidecar(NormalizedFilePath, Document, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.SidecarFilePath = NormalizedFilePath;

		FString Target;
		if (Document.IsValid() && Document->TryGetStringField(TEXT("Target"), Target))
		{
			Result.Target = NormalizeValidateTarget(Target);
		}
		return Result;
	}

	FAssetDocumentApplyRequest ApplyRequest;
	ApplyRequest.Document = Document;
	ApplyRequest.SourceDocumentPath = NormalizedFilePath;
	ApplyRequest.bSaveAsset = Request.bSaveAsset;
	ApplyRequest.bWriteSidecar = Request.bAllowSidecarRewrite && Request.bSaveAsset;

	FAssetDocumentResult Result = Apply(ApplyRequest);
	Result.SidecarFilePath = NormalizedFilePath;

	if (!Result.Payload.IsValid())
	{
		Result.Payload = MakeShared<FJsonObject>();
	}
	Result.Payload->SetStringField(TEXT("sidecar_file_path"), NormalizedFilePath);
	Result.Payload->SetBoolField(TEXT("triggered_by_watcher"), Request.bTriggeredByWatcher);
	if (Result.IsSuccess()
		&& Request.bAllowSidecarRewrite
		&& !Request.bSaveAsset
		&& !Result.bWroteSidecar)
	{
		Result.Payload->SetBoolField(TEXT("sidecar_sync_update_skipped"), true);
		Result.Payload->SetStringField(
			TEXT("sidecar_sync_update_skip_reason"),
			TEXT("Sidecar sync rewrite requires a durable asset save"));
	}

	return Result;
}

FAssetDocumentResult FAssetDocumentService::Inspect(const FAssetDocumentInspectRequest& Request) const
{
	FAssetDocumentResolvedTarget ResolvedTarget;
	FAssetDocumentResult ResolveResult = ResolveClassOrAssetTarget(Request.ClassOrAsset, TEXT("Inspect"), ResolvedTarget);
	if (!ResolveResult.IsSuccess())
	{
		return ResolveResult;
	}

	const FAssetDocumentProfileResolution ProfileResolution = ResolveAssetDocumentProfile(GetProfileRegistry(), ResolvedTarget.Class);
	const TSet<FName> ManagedPropertyNames =
		FAssetDocumentManagedPropertyPartition::CollectTopLevelPropertyNames(ProfileResolution.ExactProfile.Get());
	TSharedPtr<FJsonObject> Payload = FAssetDocumentPropertyAdapter::InspectProperties(
		ResolvedTarget.Class,
		ResolvedTarget.Asset,
		&ManagedPropertyNames);
	if (!Payload.IsValid())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Failed to inspect reflected properties"));
		Result.Target = ResolvedTarget.Target;
		Result.AssetPath = ResolvedTarget.AssetPath;
		return Result;
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument inspected"));
	Result.Target = ResolvedTarget.Target;
	Result.AssetPath = ResolvedTarget.AssetPath;
	Result.Payload = Payload;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::InspectProfile(const FAssetDocumentProfileRequest& Request) const
{
	FAssetDocumentResolvedTarget ResolvedTarget;
	FAssetDocumentResult ResolveResult = ResolveClassOrAssetTarget(Request.ClassOrAsset, TEXT("InspectProfile"), ResolvedTarget);
	if (!ResolveResult.IsSuccess())
	{
		return ResolveResult;
	}

	const FAssetDocumentProfileResolution ProfileResolution = ResolveAssetDocumentProfile(GetProfileRegistry(), ResolvedTarget.Class);
	TSharedPtr<FJsonObject> Payload = ProfileResolution.ExactProfile.IsValid()
		? MakeExactProfilePayload(ProfileResolution)
		: MakeGenericProfilePayload(ProfileResolution);

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument profile inspected"));
	Result.Target = ResolvedTarget.Target;
	Result.AssetPath = ResolvedTarget.AssetPath;
	Result.Payload = Payload;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::CreateTemplate(const FAssetDocumentTemplateRequest& Request) const
{
	const FString NormalizedTarget = NormalizeValidateTarget(Request.Target);
	if (NormalizedTarget.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("CreateTemplate requires a target"));
	}

	FString Error;
	if (!ValidateApplyTarget(NormalizedTarget, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = NormalizedTarget;
		return Result;
	}

	UClass* ResolvedClass = nullptr;
	FAssetDocumentResult ResolveResult = ResolveClassTarget(Request.Class, ResolvedClass);
	if (!ResolveResult.IsSuccess())
	{
		return ResolveResult;
	}

	const FAssetDocumentProfileResolution ProfileResolution = ResolveAssetDocumentProfile(GetProfileRegistry(), ResolvedClass);
	if (ProfileResolution.ExactProfile.IsValid())
	{
		FAssetDocumentTemplateContext Context;
		Context.Target = NormalizedTarget;
		Context.ClassPath = ResolvedClass->GetPathName();

		FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument template created"));
		Result.Target = NormalizedTarget;
		Result.Payload = ProfileResolution.ExactProfile->CreateTemplate(Context);
		return Result;
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("SchemaVersion"), 1);
	Payload->SetStringField(TEXT("Target"), NormalizedTarget);
	Payload->SetStringField(TEXT("Class"), ResolvedClass->GetPathName());
	Payload->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Payload->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Payload->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument template created"));
	Result.Target = NormalizedTarget;
	Result.Payload = Payload;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::GetSchema() const
{
	TSharedPtr<FJsonObject> FieldNaming = MakeShared<FJsonObject>();
	FieldNaming->SetBoolField(TEXT("ban_abbreviations"), true);
	FieldNaming->SetBoolField(TEXT("use_ue_stable_field_names"), true);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("schema_version"), 1);
	Payload->SetStringField(TEXT("asset_type"), TEXT("GenericAsset"));
	Payload->SetArrayField(TEXT("asset_document_tools"), ServiceMakeStringArray({
		TEXT("get_asset_document_schema"),
		TEXT("inspect_asset_document_target"),
		TEXT("inspect_asset_document_profile"),
		TEXT("create_asset_document_template"),
		TEXT("extract_asset_document"),
		TEXT("validate_asset_document"),
		TEXT("diff_asset_document"),
		TEXT("apply_asset_document"),
		TEXT("apply_asset_document_file"),
	}));
	Payload->SetArrayField(TEXT("fragment_kinds"), MakeFragmentKindArray());
	Payload->SetObjectField(TEXT("field_naming"), FieldNaming);
	Payload->SetArrayField(TEXT("RegionPolicyPresets"), MakeRegionPolicyPresetArray());
	Payload->SetArrayField(TEXT("registered_profiles"), MakeRegisteredProfileArray(GetProfileRegistry()));

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument schema described"));
	Result.Payload = Payload;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Extract(const FAssetDocumentExtractRequest& Request) const
{
	if (Request.AssetPath.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("Extract requires an asset path"));
	}

	UObject* Asset = LoadAssetFromPackageOrObjectPath(Request.AssetPath);
	const FString Target = NormalizeValidateTarget(Request.AssetPath);
	if (!Asset)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to load asset '%s'"), *Request.AssetPath));
		Result.Target = Target;
		Result.AssetPath = ToObjectPath(Request.AssetPath);
		return Result;
	}

	const FAssetDocumentProfileResolution ProfileResolution =
		ResolveAssetDocumentProfile(FAssetDocumentService::GetProfileRegistry(), Asset->GetClass());
	const TSet<FName> ManagedPropertyNames =
		FAssetDocumentManagedPropertyPartition::CollectTopLevelPropertyNames(ProfileResolution.ExactProfile.Get());
	const bool bSkipDefaults = Request.bDiffOnly && !Request.bIncludeAllWritable;
	TSharedPtr<FJsonObject> Properties = FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(
		Asset,
		bSkipDefaults,
		&ManagedPropertyNames);
	if (!Properties.IsValid())
	{
		Properties = MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("AssetType"), TEXT("GenericAsset"));
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), Asset->GetClass()->GetPathName());
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Properties"), Properties);

	if (ProfileResolution.ExactProfile.IsValid())
	{
		if (const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile->ResolveBodyAdapter(TEXT("Body")))
		{
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			FAssetDocumentCapabilityContext CapabilityContext;
			CapabilityContext.Asset = Asset;
			CapabilityContext.AssetClass = Asset->GetClass();
			CapabilityContext.TargetAssetPath = Target;

			const FAssetDocumentCapabilityResult CapabilityResult = BodyAdapter->Extract(CapabilityContext, Body);
			if (!CapabilityResult.bSuccess)
			{
				FAssetDocumentResult Result = FAssetDocumentResult::Failure(CapabilityResult.Message);
				Result.Target = Target;
				Result.AssetPath = Asset->GetPathName();
				Result.Diagnostics = CapabilityResult.Diagnostics;
				return Result;
			}

			Document->SetObjectField(TEXT("Body"), Body);
		}

		const TArray<FAssetDocumentRegionPolicy> RegionPolicies = ProfileResolution.ExactProfile->GetRegionPolicies();
		if (RegionPolicies.Num() > 0)
		{
			FAssetDocumentSyncState SyncState;
			SyncState.AssetObjectPath = Asset->GetPathName();
			SyncState.UpdatedAtUtc = FDateTime::UtcNow().ToIso8601();

			for (const FAssetDocumentRegionPolicy& Policy : RegionPolicies)
			{
				const FString RegionHash = FAssetDocumentSidecarDelta::HashSidecarRegion(Document.ToSharedRef(), Policy);
				if (RegionHash.IsEmpty())
				{
					continue;
				}

				FAssetDocumentRegionSyncState RegionState;
				RegionState.SidecarHash = RegionHash;
				RegionState.AssetEvidenceHash = RegionHash;
				RegionState.LastSyncedAtUtc = SyncState.UpdatedAtUtc;
				FAssetDocumentSyncStateStore::UpdateRegionState(SyncState, Policy.RegionId, RegionState);
			}

			if (SyncState.Regions.Num() > 0)
			{
				FAssetDocumentSyncStateStore::WriteToDocumentJson(Document.ToSharedRef(), SyncState);
			}
		}
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument extracted"));
	Result.Target = Target;
	Result.AssetPath = Asset->GetPathName();
	Result.Payload = Document;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Validate(const FAssetDocumentValidateRequest& Request) const
{
	const FString NormalizedFilePath = NormalizeValidateFilePath(Request.FilePath);
	TSharedPtr<FJsonObject> Document = Request.Document;

	if (!Document.IsValid() && NormalizedFilePath.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Validate requires a JSON document or sidecar file path"));
		return Result;
	}

	if (!Document.IsValid())
	{
		FString Error;
		if (!FAssetDocumentSidecar::LoadJsonFile(NormalizedFilePath, Document, Error))
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
			Result.SidecarFilePath = NormalizedFilePath;
			return Result;
		}
	}

	return ValidateGenericAssetDocument(Document, NormalizedFilePath, true);
}

FAssetDocumentResult FAssetDocumentService::Diff(const FAssetDocumentDiffRequest& Request) const
{
	const FString NormalizedFilePath = NormalizeValidateFilePath(Request.FilePath);
	TSharedPtr<FJsonObject> Document = Request.Document;

	if (!Document.IsValid() && NormalizedFilePath.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("Diff requires a JSON document or sidecar file path"));
	}

	if (!Document.IsValid())
	{
		FString Error;
		if (!FAssetDocumentSidecar::LoadJsonFile(NormalizedFilePath, Document, Error))
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
			Result.SidecarFilePath = NormalizedFilePath;
			return Result;
		}
	}

	Document = MakeDiffDocument(Document);

	const FAssetDocumentResult ValidateResult = ValidateGenericAssetDocument(Document, NormalizedFilePath, false);
	if (!ValidateResult.IsSuccess())
	{
		return ValidateResult;
	}

	FString Target;
	Document->TryGetStringField(TEXT("Target"), Target);
	Target = NormalizeValidateTarget(Target);
	if (!NormalizedFilePath.IsEmpty())
	{
		Target = FAssetDocumentSidecar::ResolveObjectPathFromSidecar(NormalizedFilePath);
	}

	UObject* Asset = LoadAssetFromPackageOrObjectPath(Target);
	if (!Asset)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to load asset '%s'"), *Target));
		Result.Target = Target;
		Result.AssetPath = ToObjectPath(Target);
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	TSharedPtr<FJsonObject> Properties;
	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (Document->TryGetObjectField(TEXT("Properties"), PropertiesPtr) && PropertiesPtr)
	{
		Properties = *PropertiesPtr;
	}
	if (!Properties.IsValid())
	{
		Properties = MakeShared<FJsonObject>();
	}

	TArray<TSharedPtr<FJsonValue>> Changed;
	TArray<TSharedPtr<FJsonValue>> Unchanged;
	TArray<TSharedPtr<FJsonValue>> Skipped;
	TArray<TSharedPtr<FJsonValue>> Failed;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
	{
		FProperty* Property = FindFProperty<FProperty>(Asset->GetClass(), *Pair.Key);
		if (!Property)
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("UnknownProperty"), FString::Printf(TEXT("Property '%s' does not exist"), *Pair.Key));
			continue;
		}

		const FString NonWritableReason = FAssetDocumentPropertyAdapter::GetNonWritableReason(Property);
		if (!NonWritableReason.IsEmpty())
		{
			AddReasonEntry(Skipped, Pair.Key, TEXT("NonWritable"), FString::Printf(TEXT("Property '%s' is not writable: %s"), *Pair.Key, *NonWritableReason));
			continue;
		}

		const void* CurrentValuePtr = Property->ContainerPtrToValuePtr<void>(Asset);
		TSharedPtr<FJsonValue> BeforeValue = FAssetDocumentPropertyAdapter::ExtractPropertyValue(Property, CurrentValuePtr);
		if (!BeforeValue.IsValid())
		{
			AddReasonEntry(Skipped, Pair.Key, TEXT("UnsupportedSerialization"), FString::Printf(TEXT("Property '%s' cannot be serialized"), *Pair.Key));
			continue;
		}

		UObject* PreviewAsset = DuplicateObject<UObject>(Asset, GetTransientPackage());
		if (!PreviewAsset)
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("DuplicateFailed"), FString::Printf(TEXT("Failed to duplicate asset while diffing '%s'"), *Pair.Key));
			continue;
		}

		TSharedPtr<FJsonObject> SingleProperty = MakeShared<FJsonObject>();
		SingleProperty->SetField(Pair.Key, Pair.Value);
		FAssetDocumentPropertyApplyResult ApplyResult = FAssetDocumentPropertyAdapter::ApplyProperties(PreviewAsset, SingleProperty);
		if (!ApplyResult.bSuccess)
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("TypeValidationFailed"), ApplyResult.Message);
			continue;
		}

		const void* AfterValuePtr = Property->ContainerPtrToValuePtr<void>(PreviewAsset);
		TSharedPtr<FJsonValue> AfterValue = FAssetDocumentPropertyAdapter::ExtractPropertyValue(Property, AfterValuePtr);
		if (!AfterValue.IsValid())
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("AfterSerializationFailed"), FString::Printf(TEXT("Property '%s' could not be serialized after applying document value"), *Pair.Key));
			continue;
		}

		if (JsonValueToComparableString(BeforeValue) == JsonValueToComparableString(AfterValue))
		{
			AddNamedValueEntry(Unchanged, Pair.Key, BeforeValue);
		}
		else
		{
			AddChangedEntry(Changed, Pair.Key, BeforeValue, AfterValue);
		}
	}

	const TSharedPtr<FJsonValue>* BodyValue = Document->Values.Find(TEXT("Body"));
	if (BodyValue && BodyValue->IsValid())
	{
		const FAssetDocumentProfileResolution ProfileResolution = ResolveAssetDocumentProfile(GetProfileRegistry(), Asset->GetClass());
		const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile.IsValid()
			? ProfileResolution.ExactProfile->ResolveBodyAdapter(TEXT("Body"))
			: nullptr;
		if (!BodyAdapter)
		{
			AddPathReasonEntry(Skipped, TEXT("/Body"), TEXT("skipped"), TEXT("MissingBodyAdapter"), TEXT("No AssetDocument Body adapter is registered for this asset class"));
		}
		else
		{
			const TSharedPtr<FJsonObject>* Definitions = nullptr;
			Document->TryGetObjectField(TEXT("Definitions"), Definitions);

			FAssetDocumentCapabilityContext CapabilityContext;
			CapabilityContext.Asset = Asset;
			CapabilityContext.AssetClass = Asset->GetClass();
			CapabilityContext.TargetAssetPath = Target;
			CapabilityContext.SourceDocumentPath = NormalizedFilePath;
			CapabilityContext.Definitions = Definitions;
			CapabilityContext.bIsDryRun = true;

			TArray<TSharedPtr<FJsonValue>> BodyDiffEntries;
			const FAssetDocumentCapabilityResult BodyDiffResult = BodyAdapter->Diff(CapabilityContext, BodyValue->ToSharedRef(), BodyDiffEntries);
			if (!BodyDiffResult.bSuccess)
			{
				AddPathReasonEntry(Failed, TEXT("/Body"), TEXT("failed"), BodyDiffResult.Diagnostics.Num() > 0 ? BodyDiffResult.Diagnostics[0].Code : TEXT("BodyDiffFailed"), BodyDiffResult.Message);
			}
			else
			{
				RouteCapabilityDiffEntries(BodyDiffEntries, Changed, Unchanged, Skipped, Failed);
			}
		}
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("changed"), Changed);
	Payload->SetArrayField(TEXT("unchanged"), Unchanged);
	Payload->SetArrayField(TEXT("skipped"), Skipped);
	Payload->SetArrayField(TEXT("failed"), Failed);

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument diffed"));
	Result.Target = Target;
	Result.AssetPath = Asset->GetPathName();
	Result.SidecarFilePath = NormalizedFilePath;
	Result.Payload = Payload;
	return Result;
}
