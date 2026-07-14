// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "AssetDocumentSidecar.h"
#include "AssetDocumentSidecarDelta.h"
#include "AssetDocumentSidecarSyncEngine.h"
#include "AssetDocumentSyncStateStore.h"
#include "AssetFactoryNamedAnimNotifyState.h"
#include "Profiles/AnimMontageAssetDocumentProfile.h"
#include "Profiles/AnimMontageAssetDocumentCapability.h"
#include "Profiles/AnimMontageNotifyPlacementAdapter.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimMetaData.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Generators/AnimSequenceGenerator.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectIterator.h"
#include "UObject/SavePackage.h"

FAssetDocumentResult RegenerateSidecarRegionsFromAsset(
	UObject* Asset,
	const TSharedRef<FJsonObject>& SidecarDocument,
	const TArray<FName>& RegionIds,
	const FString& SourceDocumentPath);

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const TCHAR* TestSkeletonPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton");
const TCHAR* TestPreviewMeshPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP");
const TCHAR* TestAnimPath = TEXT("/Game/Generated/Animation");
const TCHAR* TestConcreteNotifyClassPath = TEXT("/Script/Engine.AnimNotify_PlaySound");
const FName TestManagedNotifyName(TEXT("AssetDocument.Notify"));
const FName TestManagedNotifyStateName(TEXT("AssetDocument.NotifyState"));
const TCHAR* TestManagedNotifyObjectPrefix = TEXT("AssetDocumentManaged_Notify_");
const TCHAR* TestManagedNotifyStateObjectPrefix = TEXT("AssetDocumentManaged_NotifyState_");

TSharedPtr<FJsonObject> MakeMontageDocument(const FString& Target)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimMontage"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Body"), MakeShared<FJsonObject>());
	return Document;
}

TSharedPtr<FJsonObject> AnimMontageMakeAssetRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	Fragment->SetStringField(TEXT("Path"), Path);
	return Fragment;
}

TSharedPtr<FJsonObject> MakeReorderedAssetRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Path"), Path);
	Fragment->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	return Fragment;
}

TSharedPtr<FJsonObject> MakeDefinitionRef(const FString& Id)
{
	TSharedPtr<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), TEXT("DefinitionRef"));
	Fragment->SetStringField(TEXT("Id"), Id);
	return Fragment;
}

TSharedPtr<FJsonObject> AnimMontageMakeClassRef(const FString& ClassPath)
{
	TSharedPtr<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	Fragment->SetStringField(TEXT("Class"), ClassPath);
	return Fragment;
}

TSharedPtr<FJsonObject> MakeEmbeddedObjectRef(const FString& ClassPath)
{
	TSharedPtr<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), TEXT("EmbeddedObject"));
	Fragment->SetStringField(TEXT("Class"), ClassPath);
	return Fragment;
}

TSharedPtr<FJsonObject> MakeEmbeddedObjectRef(const FString& ClassPath, TSharedPtr<FJsonObject> Properties)
{
	TSharedPtr<FJsonObject> Fragment = MakeEmbeddedObjectRef(ClassPath);
	Fragment->SetObjectField(TEXT("Properties"), Properties);
	return Fragment;
}

struct FAnimMetaDataClassFixture
{
	FString ClassPath;
	FString ObjectPath;
	FString PackageFileName;

	bool IsValid() const
	{
		return !ClassPath.IsEmpty();
	}

	void Cleanup() const
	{
		if (!ObjectPath.IsEmpty())
		{
			UObject* ExistingAsset = FindObject<UObject>(nullptr, *ObjectPath);
			if (!ExistingAsset && !PackageFileName.IsEmpty() && IFileManager::Get().FileExists(*PackageFileName))
			{
				ExistingAsset = LoadObject<UObject>(nullptr, *ObjectPath);
			}

			if (ExistingAsset)
			{
				TArray<UObject*> ObjectsToDelete;
				ObjectsToDelete.Add(ExistingAsset);
				ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
			}
		}

		if (!PackageFileName.IsEmpty() && IFileManager::Get().FileExists(*PackageFileName))
		{
			IFileManager::Get().Delete(*PackageFileName, false, true);
		}
	}
};

FAnimMetaDataClassFixture CreateAnimMetaDataClassFixture()
{
	FAnimMetaDataClassFixture Fixture;
	const FString AssetName = FString::Printf(TEXT("BP_AnimMetaData_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString PackageName = FString::Printf(TEXT("/Game/AssetDocumentTests/%s"), *AssetName);
	Fixture.ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
	Fixture.PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return Fixture;
	}

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		UAnimMetaData::StaticClass(),
		Package,
		*AssetName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());
	if (!Blueprint)
	{
		Fixture.Cleanup();
		return Fixture;
	}

	Blueprint->bGenerateAbstractClass = false;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FAssetRegistryModule::AssetCreated(Blueprint);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Blueprint, *Fixture.PackageFileName, SaveArgs))
	{
		Fixture.Cleanup();
		return Fixture;
	}

	if (Blueprint->GeneratedClass && !Blueprint->GeneratedClass->HasAnyClassFlags(CLASS_Abstract))
	{
		Fixture.ClassPath = Blueprint->GeneratedClass->GetPathName();
	}
	return Fixture;
}

TSharedPtr<FJsonObject> MakeAnimMetaDataRef(const FString& ClassPath)
{
	return MakeEmbeddedObjectRef(ClassPath, MakeShared<FJsonObject>());
}

TSharedPtr<FJsonObject> MakeMontageTimeStretchCurve()
{
	TSharedPtr<FJsonObject> Curve = MakeShared<FJsonObject>();
	Curve->SetStringField(TEXT("Name"), TEXT("MontageTimeStretchCurve"));

	TArray<TSharedPtr<FJsonValue>> Flags;
	Flags.Add(MakeShared<FJsonValueString>(TEXT("Default")));
	Curve->SetArrayField(TEXT("Flags"), Flags);

	TArray<TSharedPtr<FJsonValue>> Keys;
	auto AddKey = [&Keys](double Time, double Value)
	{
		TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
		Key->SetNumberField(TEXT("Time"), Time);
		Key->SetNumberField(TEXT("Value"), Value);
		Keys.Add(MakeShared<FJsonValueObject>(Key));
	};
	AddKey(1.0, 0.0);
	AddKey(0.0, 0.0);
	AddKey(0.5, 1.0);
	Curve->SetArrayField(TEXT("Keys"), Keys);

	return Curve;
}

TSharedPtr<FJsonObject> MakeSortedMontageTimeStretchCurve()
{
	TSharedPtr<FJsonObject> Curve = MakeShared<FJsonObject>();
	Curve->SetStringField(TEXT("Name"), TEXT("MontageTimeStretchCurve"));

	TArray<TSharedPtr<FJsonValue>> Flags;
	Flags.Add(MakeShared<FJsonValueString>(TEXT("Default")));
	Curve->SetArrayField(TEXT("Flags"), Flags);

	TArray<TSharedPtr<FJsonValue>> Keys;
	auto AddKey = [&Keys](double Time, double Value)
	{
		TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
		Key->SetNumberField(TEXT("Time"), Time);
		Key->SetNumberField(TEXT("Value"), Value);
		Keys.Add(MakeShared<FJsonValueObject>(Key));
	};
	AddKey(0.0, 0.0);
	AddKey(0.5, 1.0);
	AddKey(1.0, 0.0);
	Curve->SetArrayField(TEXT("Keys"), Keys);

	return Curve;
}

void SetCurvesAndTimeStretch(TSharedPtr<FJsonObject> Document)
{
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	TArray<TSharedPtr<FJsonValue>> Curves;
	Curves.Add(MakeShared<FJsonValueObject>(MakeMontageTimeStretchCurve()));
	Body->SetArrayField(TEXT("Curves"), Curves);

	TSharedPtr<FJsonObject> TimeStretch = MakeShared<FJsonObject>();
	TimeStretch->SetStringField(TEXT("TimeStretchCurveName"), TEXT("MontageTimeStretchCurve"));
	TimeStretch->SetNumberField(TEXT("SamplingRate"), 30.0);
	TimeStretch->SetNumberField(TEXT("CurveValueMinPrecision"), 0.02);
	Body->SetObjectField(TEXT("TimeStretch"), TimeStretch);
}

FString MakeUniqueTestAssetName(const TCHAR* Prefix)
{
	return FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
}

FString MakeUniqueMontageTarget(const TCHAR* Prefix)
{
	return FString::Printf(TEXT("/Game/AssetDocumentTests/%s"), *MakeUniqueTestAssetName(Prefix));
}

FString MakeObjectPathFromTarget(const FString& Target)
{
	const FString AssetName = FPackageName::GetLongPackageAssetName(Target);
	return FString::Printf(TEXT("%s.%s"), *Target, *AssetName);
}

FString MakeAnimSequenceObjectPath(const FString& AssetName)
{
	return FString::Printf(TEXT("%s/%s.%s"), TestAnimPath, *AssetName, *AssetName);
}

UAnimSequenceBase* CreateAnimSequenceFixture()
{
	FAnimSequenceGenerator Generator;
	const FString AssetName = MakeUniqueTestAssetName(TEXT("AS_AssetDocumentMontage"));

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();
	Config->SetStringField(TEXT("Skeleton"), TestSkeletonPath);
	Config->SetStringField(TEXT("PreviewMesh"), TestPreviewMeshPath);

	TSharedPtr<FJsonObject> FrameRate = MakeShared<FJsonObject>();
	FrameRate->SetNumberField(TEXT("Numerator"), 30);
	FrameRate->SetNumberField(TEXT("Denominator"), 1);
	Config->SetObjectField(TEXT("FrameRate"), FrameRate);
	Config->SetNumberField(TEXT("NumberOfFrames"), 12);

	const FGenerationResult Result = Generator.Generate(AssetName, TestAnimPath, EGenerationAction::CreateOrUpdate, Config);
	if (!Result.IsSuccess())
	{
		return nullptr;
	}

	UAnimSequenceBase* AnimSequence = Cast<UAnimSequenceBase>(Result.GeneratedAsset);
	return AnimSequence ? AnimSequence : LoadObject<UAnimSequenceBase>(nullptr, *MakeAnimSequenceObjectPath(AssetName));
}

TSharedPtr<FJsonObject> MakeStructuredMontageDocument(const FString& Target, const FString& AnimReferencePath)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(Target);
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	Body->SetObjectField(TEXT("Skeleton"), AnimMontageMakeAssetRef(TestSkeletonPath));
	Body->SetObjectField(TEXT("PreviewMesh"), AnimMontageMakeAssetRef(TestPreviewMeshPath));

	TSharedPtr<FJsonObject> Segment = MakeShared<FJsonObject>();
	Segment->SetObjectField(TEXT("AnimReference"), AnimMontageMakeAssetRef(AnimReferencePath));
	Segment->SetNumberField(TEXT("StartPos"), 0.0);
	Segment->SetNumberField(TEXT("AnimStartTime"), 0.0);
	Segment->SetNumberField(TEXT("AnimEndTime"), 0.25);
	Segment->SetNumberField(TEXT("AnimPlayRate"), 1.0);
	Segment->SetNumberField(TEXT("LoopingCount"), 1.0);

	TArray<TSharedPtr<FJsonValue>> AnimSegments;
	AnimSegments.Add(MakeShared<FJsonValueObject>(Segment));

	TSharedPtr<FJsonObject> AnimTrack = MakeShared<FJsonObject>();
	AnimTrack->SetArrayField(TEXT("AnimSegments"), AnimSegments);

	TSharedPtr<FJsonObject> SlotAnimTrack = MakeShared<FJsonObject>();
	SlotAnimTrack->SetStringField(TEXT("SlotName"), TEXT("DefaultSlot"));
	SlotAnimTrack->SetObjectField(TEXT("AnimTrack"), AnimTrack);

	TArray<TSharedPtr<FJsonValue>> SlotAnimTracks;
	SlotAnimTracks.Add(MakeShared<FJsonValueObject>(SlotAnimTrack));
	Body->SetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks);

	TSharedPtr<FJsonObject> StartSection = MakeShared<FJsonObject>();
	StartSection->SetStringField(TEXT("SectionName"), TEXT("Start"));
	StartSection->SetNumberField(TEXT("LinkableTime"), 0.0);
	StartSection->SetStringField(TEXT("NextSectionName"), TEXT("End"));

	TSharedPtr<FJsonObject> EndSection = MakeShared<FJsonObject>();
	EndSection->SetStringField(TEXT("SectionName"), TEXT("End"));
	EndSection->SetNumberField(TEXT("LinkableTime"), 0.25);

	TArray<TSharedPtr<FJsonValue>> CompositeSections;
	CompositeSections.Add(MakeShared<FJsonValueObject>(StartSection));
	CompositeSections.Add(MakeShared<FJsonValueObject>(EndSection));
	Body->SetArrayField(TEXT("CompositeSections"), CompositeSections);

	TSharedPtr<FJsonObject> Blend = MakeShared<FJsonObject>();
	Blend->SetNumberField(TEXT("BlendInTime"), 0.1);
	Blend->SetNumberField(TEXT("BlendOutTime"), 0.2);
	Body->SetObjectField(TEXT("Blend"), Blend);

	return Document;
}

TSharedPtr<FJsonObject> MakeDefinitionRefDocument(const FString& Target, const FString& AnimReferencePath, const FString& DefinitionId, TSharedPtr<FJsonObject> Definition)
{
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimReferencePath);
	Document->GetObjectField(TEXT("Definitions"))->SetObjectField(DefinitionId, Definition);
	TArray<TSharedPtr<FJsonValue>> MetadataValues;
	MetadataValues.Add(MakeShared<FJsonValueObject>(MakeDefinitionRef(DefinitionId)));
	Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Metadata"), MetadataValues);
	return Document;
}

TSharedPtr<FJsonObject> MakeReorderedStructuredMontageDocument(const FString& Target, const FString& AnimReferencePath)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(Target);
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	Body->SetObjectField(TEXT("Skeleton"), MakeReorderedAssetRef(TestSkeletonPath));
	Body->SetObjectField(TEXT("PreviewMesh"), MakeReorderedAssetRef(TestPreviewMeshPath));

	TSharedPtr<FJsonObject> Segment = MakeShared<FJsonObject>();
	Segment->SetNumberField(TEXT("LoopingCount"), 1.0);
	Segment->SetNumberField(TEXT("AnimPlayRate"), 1.0);
	Segment->SetNumberField(TEXT("AnimEndTime"), 0.25);
	Segment->SetNumberField(TEXT("AnimStartTime"), 0.0);
	Segment->SetNumberField(TEXT("StartPos"), 0.0);
	Segment->SetObjectField(TEXT("AnimReference"), MakeReorderedAssetRef(AnimReferencePath));

	TArray<TSharedPtr<FJsonValue>> AnimSegments;
	AnimSegments.Add(MakeShared<FJsonValueObject>(Segment));

	TSharedPtr<FJsonObject> AnimTrack = MakeShared<FJsonObject>();
	AnimTrack->SetArrayField(TEXT("AnimSegments"), AnimSegments);

	TSharedPtr<FJsonObject> SlotAnimTrack = MakeShared<FJsonObject>();
	SlotAnimTrack->SetObjectField(TEXT("AnimTrack"), AnimTrack);
	SlotAnimTrack->SetStringField(TEXT("SlotName"), TEXT("DefaultSlot"));

	TArray<TSharedPtr<FJsonValue>> SlotAnimTracks;
	SlotAnimTracks.Add(MakeShared<FJsonValueObject>(SlotAnimTrack));
	Body->SetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks);

	TSharedPtr<FJsonObject> StartSection = MakeShared<FJsonObject>();
	StartSection->SetStringField(TEXT("NextSectionName"), TEXT("End"));
	StartSection->SetNumberField(TEXT("LinkableTime"), 0.0);
	StartSection->SetStringField(TEXT("SectionName"), TEXT("Start"));

	TSharedPtr<FJsonObject> EndSection = MakeShared<FJsonObject>();
	EndSection->SetNumberField(TEXT("LinkableTime"), 0.25);
	EndSection->SetStringField(TEXT("SectionName"), TEXT("End"));

	TArray<TSharedPtr<FJsonValue>> CompositeSections;
	CompositeSections.Add(MakeShared<FJsonValueObject>(StartSection));
	CompositeSections.Add(MakeShared<FJsonValueObject>(EndSection));
	Body->SetArrayField(TEXT("CompositeSections"), CompositeSections);

	TSharedPtr<FJsonObject> Blend = MakeShared<FJsonObject>();
	Blend->SetNumberField(TEXT("BlendOutTime"), 0.2);
	Blend->SetNumberField(TEXT("BlendInTime"), 0.1);
	Body->SetObjectField(TEXT("Blend"), Blend);

	return Document;
}

TSharedPtr<FJsonObject> GetFirstSegment(TSharedPtr<FJsonObject> Document)
{
	const TArray<TSharedPtr<FJsonValue>>& SlotAnimTracks = Document->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("SlotAnimTracks"));
	TSharedPtr<FJsonObject> SlotAnimTrack = SlotAnimTracks[0]->AsObject();
	const TArray<TSharedPtr<FJsonValue>>& AnimSegments = SlotAnimTrack->GetObjectField(TEXT("AnimTrack"))->GetArrayField(TEXT("AnimSegments"));
	return AnimSegments[0]->AsObject();
}

TSharedPtr<FJsonObject> GetCompositeSection(TSharedPtr<FJsonObject> Document, int32 Index)
{
	const TArray<TSharedPtr<FJsonValue>>& CompositeSections = Document->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("CompositeSections"));
	return CompositeSections[Index]->AsObject();
}

void SetScalarRegions(TSharedPtr<FJsonObject> Document, const FString& PreviewBasePosePath)
{
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));

	TSharedPtr<FJsonObject> References = MakeShared<FJsonObject>();
	References->SetObjectField(TEXT("Skeleton"), AnimMontageMakeAssetRef(TestSkeletonPath));
	Body->SetObjectField(TEXT("References"), References);

	TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetObjectField(TEXT("PreviewMesh"), AnimMontageMakeAssetRef(TestPreviewMeshPath));
	Preview->SetObjectField(TEXT("PreviewBasePose"), AnimMontageMakeAssetRef(PreviewBasePosePath));
	Body->SetObjectField(TEXT("Preview"), Preview);

	TSharedPtr<FJsonObject> Sync = MakeShared<FJsonObject>();
	Sync->SetStringField(TEXT("SyncGroup"), TEXT("AssetDocSync"));
	Sync->SetNumberField(TEXT("SyncSlotIndex"), 0);
	Body->SetObjectField(TEXT("Sync"), Sync);

	TSharedPtr<FJsonObject> RootMotion = MakeShared<FJsonObject>();
	RootMotion->SetBoolField(TEXT("bEnableRootMotionTranslation"), true);
	RootMotion->SetBoolField(TEXT("bEnableRootMotionRotation"), true);
	RootMotion->SetStringField(TEXT("RootMotionRootLock"), TEXT("Zero"));
	Body->SetObjectField(TEXT("RootMotion"), RootMotion);
}

TSharedPtr<FJsonObject> MakeSyncOnlyMontageDocument(const FString& Target, const FString& SyncGroup, double SyncSlotIndex)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(Target);
	TSharedPtr<FJsonObject> Sync = MakeShared<FJsonObject>();
	Sync->SetStringField(TEXT("SyncGroup"), SyncGroup);
	Sync->SetNumberField(TEXT("SyncSlotIndex"), SyncSlotIndex);
	Document->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("Sync"), Sync);
	return Document;
}

TSharedPtr<FJsonObject> MakeNotifyPlacement(double Time, TSharedPtr<FJsonObject> Object)
{
	TSharedPtr<FJsonObject> Placement = MakeShared<FJsonObject>();
	Placement->SetNumberField(TEXT("Time"), Time);
	Placement->SetObjectField(TEXT("Object"), Object);
	return Placement;
}

TSharedPtr<FJsonObject> MakeNotifyStatePlacement(double Time, double Duration, TSharedPtr<FJsonObject> Object)
{
	TSharedPtr<FJsonObject> Placement = MakeNotifyPlacement(Time, Object);
	Placement->SetNumberField(TEXT("Duration"), Duration);
	return Placement;
}

void SetSingleManagedNotify(TSharedPtr<FJsonObject> Body)
{
	TArray<TSharedPtr<FJsonValue>> Notifies;
	Notifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(0.10, MakeEmbeddedObjectRef(TestConcreteNotifyClassPath))));
	Body->SetArrayField(TEXT("Notifies"), Notifies);
}

void SetSingleManagedNotifyState(TSharedPtr<FJsonObject> Body)
{
	TArray<TSharedPtr<FJsonValue>> NotifyStates;
	NotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
		0.12,
		0.05,
		MakeEmbeddedObjectRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"), MakeShared<FJsonObject>()))));
	Body->SetArrayField(TEXT("NotifyStates"), NotifyStates);
}

bool JsonArrayContainsString(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& Expected)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (Value.IsValid() && Value->AsString() == Expected)
		{
			return true;
		}
	}
	return false;
}

bool LoadSidecarJson(FAutomationTestBase* Test, const FString& SidecarPath, TSharedPtr<FJsonObject>& OutDocument)
{
	FString Error;
	const bool bLoaded = FAssetDocumentSidecar::LoadJsonFile(SidecarPath, OutDocument, Error);
	Test->TestTrue(FString::Printf(TEXT("Loads sidecar JSON '%s'"), *SidecarPath), bLoaded);
	if (!bLoaded)
	{
		Test->AddError(Error);
	}
	return bLoaded;
}

bool WriteSidecarJson(FAutomationTestBase* Test, const FString& SidecarPath, const TSharedPtr<FJsonObject>& Document)
{
	FString Error;
	const bool bWrote = FAssetDocumentSidecar::WriteJsonFile(SidecarPath, Document, Error);
	Test->TestTrue(FString::Printf(TEXT("Writes sidecar JSON '%s'"), *SidecarPath), bWrote);
	if (!bWrote)
	{
		Test->AddError(Error);
	}
	return bWrote;
}

FString JsonObjectToCompactString(const TSharedPtr<FJsonObject>& Object)
{
	if (!Object.IsValid())
	{
		return TEXT("<invalid>");
	}

	FString JsonText;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
	return JsonText;
}

const TSharedPtr<FJsonObject>* FindSyncRegions(TSharedPtr<FJsonObject> Document)
{
	const TSharedPtr<FJsonObject>* Meta = nullptr;
	if (!Document.IsValid() || !Document->TryGetObjectField(TEXT("_meta"), Meta) || !Meta || !Meta->IsValid())
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject>* Sync = nullptr;
	if (!(*Meta)->TryGetObjectField(TEXT("sync"), Sync) || !Sync || !Sync->IsValid())
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject>* Regions = nullptr;
	if (!(*Sync)->TryGetObjectField(TEXT("regions"), Regions) || !Regions || !Regions->IsValid())
	{
		return nullptr;
	}
	return Regions;
}

bool HasSyncRegions(TSharedPtr<FJsonObject> Document)
{
	return FindSyncRegions(Document) != nullptr;
}

void ExpectApplyFileSyncRegion(
	FAutomationTestBase* Test,
	const TSharedRef<FJsonObject>& AppliedSidecarDocument,
	const TSharedPtr<FJsonObject>& SyncRegions,
	const FAssetDocumentRegionPolicy& Policy)
{
	const TSharedPtr<FJsonObject>* RegionObject = nullptr;
	Test->TestTrue(
		FString::Printf(TEXT("ApplyFile sync includes %s"), *Policy.RegionId.ToString()),
		SyncRegions->TryGetObjectField(Policy.RegionId.ToString(), RegionObject));
	if (!RegionObject || !RegionObject->IsValid())
	{
		return;
	}

	const FString SidecarHash = (*RegionObject)->GetStringField(TEXT("sidecarHash"));
	const FString AssetEvidenceHash = (*RegionObject)->GetStringField(TEXT("assetEvidenceHash"));
	const FString LastSyncedAtUtc = (*RegionObject)->GetStringField(TEXT("lastSyncedAtUtc"));
	const FString ExpectedSidecarHash = FAssetDocumentSidecarDelta::HashSidecarRegion(AppliedSidecarDocument, Policy);
	Test->TestFalse(FString::Printf(TEXT("%s sidecar hash is initialized after ApplyFile"), *Policy.RegionId.ToString()), SidecarHash.IsEmpty());
	Test->TestFalse(FString::Printf(TEXT("%s asset evidence hash is initialized after ApplyFile"), *Policy.RegionId.ToString()), AssetEvidenceHash.IsEmpty());
	Test->TestFalse(FString::Printf(TEXT("%s lastSyncedAtUtc is initialized after ApplyFile"), *Policy.RegionId.ToString()), LastSyncedAtUtc.IsEmpty());
	Test->TestEqual(FString::Printf(TEXT("%s sidecar hash matches source sidecar"), *Policy.RegionId.ToString()), SidecarHash, ExpectedSidecarHash);
	Test->TestEqual(FString::Printf(TEXT("%s asset evidence hash matches applied sidecar"), *Policy.RegionId.ToString()), AssetEvidenceHash, SidecarHash);
}

bool JsonArrayContainsPathStatus(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& ExpectedPath, const FString& ExpectedStatus)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Object.IsValid())
		{
			continue;
		}

		FString Path;
		FString Status;
		if (Object->TryGetStringField(TEXT("path"), Path)
			&& Object->TryGetStringField(TEXT("status"), Status)
			&& Path == ExpectedPath
			&& Status == ExpectedStatus)
		{
			return true;
		}
	}
	return false;
}

bool JsonArrayContainsPath(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& ExpectedPath)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Object.IsValid())
		{
			continue;
		}

		FString Path;
		if (Object->TryGetStringField(TEXT("path"), Path) && Path == ExpectedPath)
		{
			return true;
		}
	}
	return false;
}

bool HasExpectedBodySections(const TArray<TSharedPtr<FJsonValue>>& BodySections)
{
	return JsonArrayContainsString(BodySections, TEXT("Skeleton"))
		&& JsonArrayContainsString(BodySections, TEXT("PreviewMesh"))
		&& JsonArrayContainsString(BodySections, TEXT("References"))
		&& JsonArrayContainsString(BodySections, TEXT("Preview"))
		&& JsonArrayContainsString(BodySections, TEXT("Sync"))
		&& JsonArrayContainsString(BodySections, TEXT("RootMotion"))
		&& JsonArrayContainsString(BodySections, TEXT("Metadata"))
		&& JsonArrayContainsString(BodySections, TEXT("SectionMetadata"))
		&& JsonArrayContainsString(BodySections, TEXT("TimeStretch"))
		&& JsonArrayContainsString(BodySections, TEXT("Curves"))
		&& JsonArrayContainsString(BodySections, TEXT("SlotAnimTracks"))
		&& JsonArrayContainsString(BodySections, TEXT("CompositeSections"))
		&& JsonArrayContainsString(BodySections, TEXT("Notifies"))
		&& JsonArrayContainsString(BodySections, TEXT("NotifyStates"))
		&& JsonArrayContainsString(BodySections, TEXT("Blend"));
}

bool HasExpectedFragmentKinds(const TArray<TSharedPtr<FJsonValue>>& FragmentKinds)
{
	return JsonArrayContainsString(FragmentKinds, TEXT("AssetRef"))
		&& JsonArrayContainsString(FragmentKinds, TEXT("ClassRef"))
		&& JsonArrayContainsString(FragmentKinds, TEXT("StructValue"))
		&& JsonArrayContainsString(FragmentKinds, TEXT("EmbeddedObject"))
		&& JsonArrayContainsString(FragmentKinds, TEXT("DefinitionRef"));
}

bool FindRegionPolicy(const TArray<FAssetDocumentRegionPolicy>& Policies, FName RegionId, FAssetDocumentRegionPolicy& OutPolicy)
{
	for (const FAssetDocumentRegionPolicy& Policy : Policies)
	{
		if (Policy.RegionId == RegionId)
		{
			OutPolicy = Policy;
			return true;
		}
	}
	return false;
}

const TArray<FName>& GetCompleteAnimMontageSyncRegionIds()
{
	static const TArray<FName> RegionIds = {
		TEXT("Body.References"),
		TEXT("Body.Preview"),
		TEXT("Body.Sync"),
		TEXT("Body.RootMotion"),
		TEXT("Body.Metadata"),
		TEXT("Body.SectionMetadata"),
		TEXT("Body.TimeStretch"),
		TEXT("Body.Curves"),
		TEXT("Body.Blend"),
		TEXT("Body.SlotAnimTracks"),
		TEXT("Body.CompositeSections"),
		TEXT("Body.Notifies"),
		TEXT("Body.NotifyStates"),
	};
	return RegionIds;
}

const TArray<FName>& GetNewCompleteAnimMontageRegionIds()
{
	static const TArray<FName> RegionIds = {
		TEXT("Body.References"),
		TEXT("Body.Preview"),
		TEXT("Body.Sync"),
		TEXT("Body.RootMotion"),
		TEXT("Body.Metadata"),
		TEXT("Body.SectionMetadata"),
		TEXT("Body.TimeStretch"),
		TEXT("Body.Curves"),
	};
	return RegionIds;
}

FString RegionIdToDiffPath(FName RegionId)
{
	return FString(TEXT("/")) + RegionId.ToString().Replace(TEXT("."), TEXT("/"));
}

bool CollectRegionPolicies(
	FAutomationTestBase* Test,
	const FAnimMontageAssetDocumentProfile& Profile,
	const TArray<FName>& RegionIds,
	TArray<FAssetDocumentRegionPolicy>& OutPolicies)
{
	OutPolicies.Reset();
	bool bAllFound = true;
	for (FName RegionId : RegionIds)
	{
		FAssetDocumentRegionPolicy Policy;
		const bool bFound = Test->TestTrue(
			FString::Printf(TEXT("AnimMontage profile has %s policy"), *RegionId.ToString()),
			Profile.GetRegionPolicy(RegionId, Policy));
		if (bFound)
		{
			OutPolicies.Add(Policy);
		}
		bAllFound &= bFound;
	}
	return bAllFound;
}

void TestRegionPolicyContract(
	FAutomationTestBase* Test,
	const FAnimMontageAssetDocumentProfile& Profile,
	FName RegionId,
	EAssetDocumentRegionKind RegionKind,
	EAssetDocumentReducerMode ReducerMode,
	EAssetDocumentApplyMode ApplyMode,
	const TArray<FString>& ExpectedManagedPaths)
{
	FAssetDocumentRegionPolicy RegionPolicy;
	const FString RegionIdString = RegionId.ToString();
	if (!Test->TestTrue(
		FString::Printf(TEXT("AnimMontage profile declares %s policy"), *RegionIdString),
		Profile.GetRegionPolicy(RegionId, RegionPolicy)))
	{
		return;
	}

	Test->TestEqual(
		FString::Printf(TEXT("%s uses expected region kind"), *RegionIdString),
		RegionPolicy.RegionKind,
		RegionKind);
	Test->TestEqual(
		FString::Printf(TEXT("%s uses expected reducer"), *RegionIdString),
		RegionPolicy.ReducerMode,
		ReducerMode);
	Test->TestEqual(
		FString::Printf(TEXT("%s uses expected apply mode"), *RegionIdString),
		RegionPolicy.ApplyMode,
		ApplyMode);

	for (const FString& ExpectedManagedPath : ExpectedManagedPaths)
	{
		Test->TestTrue(
			FString::Printf(TEXT("%s owns %s property"), *RegionIdString, *ExpectedManagedPath),
			RegionPolicy.ManagedUePropertyPaths.Contains(ExpectedManagedPath));
	}
}

TSharedPtr<FJsonObject> FindJsonObjectByStringField(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& FieldName, const FString& ExpectedValue)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Object.IsValid())
		{
			continue;
		}

		FString ActualValue;
		if (Object->TryGetStringField(FieldName, ActualValue) && ActualValue == ExpectedValue)
		{
			return Object;
		}
	}
	return nullptr;
}

void TestNoAgentFacingOperationFields(FAutomationTestBase* Test, const FString& Context, const TSharedPtr<FJsonObject>& Object)
{
	if (!Test || !Object.IsValid())
	{
		return;
	}

	for (const TCHAR* FieldName : {TEXT("patch"), TEXT("op"), TEXT("Patch"), TEXT("Op"), TEXT("Operations")})
	{
		Test->TestFalse(FString::Printf(TEXT("%s does not expose %s field"), *Context, FieldName), Object->HasField(FieldName));
	}
}

int32 CountNotifyEventsByName(const UAnimMontage* Montage, FName NotifyName)
{
	int32 Count = 0;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (Event.NotifyName == NotifyName)
		{
			++Count;
		}
	}
	return Count;
}

int32 CountNotifyEventsByObjectName(const UAnimMontage* Montage, const FString& ObjectName)
{
	int32 Count = 0;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		const UObject* NotifyObject = Event.Notify ? static_cast<const UObject*>(Event.Notify) : static_cast<const UObject*>(Event.NotifyStateClass);
		if (NotifyObject && NotifyObject->GetName() == ObjectName)
		{
			++Count;
		}
	}
	return Count;
}

int32 CountManagedNotifyEvents(const UAnimMontage* Montage)
{
	int32 Count = 0;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (FAnimMontageNotifyPlacementAdapter::IsManagedNotifyEvent(Event, Montage))
		{
			++Count;
		}
	}
	return Count;
}

int32 CountManagedNotifyStateEvents(const UAnimMontage* Montage)
{
	int32 Count = 0;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (FAnimMontageNotifyPlacementAdapter::IsManagedNotifyStateEvent(Event, Montage))
		{
			++Count;
		}
	}
	return Count;
}

UAnimNotify* CreateTestNotify(UAnimMontage* Montage, FName ObjectName = NAME_None)
{
	UClass* NotifyClass = StaticLoadClass(UAnimNotify::StaticClass(), nullptr, TestConcreteNotifyClassPath);
	return NotifyClass ? NewObject<UAnimNotify>(Montage, NotifyClass, ObjectName, RF_Transactional) : nullptr;
}

UAnimNotify* CreateTestNotifyWithOuter(UObject* Outer, FName ObjectName)
{
	UClass* NotifyClass = StaticLoadClass(UAnimNotify::StaticClass(), nullptr, TestConcreteNotifyClassPath);
	return NotifyClass ? NewObject<UAnimNotify>(Outer, NotifyClass, ObjectName, RF_Transactional) : nullptr;
}

void AddNotifyEvent(UAnimMontage* Montage, UAnimNotify* Notify, FName NotifyName, float Time)
{
	FAnimNotifyEvent Event;
	Event.Notify = Notify;
	Event.NotifyName = NotifyName;
	Event.SetTime(Time);
	Event.RefreshTriggerOffset(Montage->CalculateOffsetForNotify(Time));
#if WITH_EDITORONLY_DATA
	Event.Guid = FGuid::NewGuid();
#endif
	Montage->Notifies.Add(Event);
}

void AddNotifyStateEvent(UAnimMontage* Montage, UAnimNotifyState* NotifyState, FName NotifyName, float Time, float Duration)
{
	FAnimNotifyEvent Event;
	Event.NotifyStateClass = NotifyState;
	Event.NotifyName = NotifyName;
	Event.SetTime(Time);
	Event.SetDuration(Duration);
	Event.RefreshTriggerOffset(Montage->CalculateOffsetForNotify(Time));
	Event.RefreshEndTriggerOffset(Montage->CalculateOffsetForNotify(Time + Duration));
#if WITH_EDITORONLY_DATA
	Event.Guid = FGuid::NewGuid();
#endif
	Montage->Notifies.Add(Event);
}

bool AnimMontageHasDiagnostic(const FAssetDocumentResult& Result, const FString& Path, const FString& Code)
{
	for (const FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Path == Path && Diagnostic.Code == Code)
		{
			return true;
		}
	}
	return false;
}

bool HasDiagnosticMessage(const FAssetDocumentResult& Result, const FString& Path, const FString& Code, const FString& MessageFragment)
{
	for (const FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Path == Path && Diagnostic.Code == Code && Diagnostic.Message.Contains(MessageFragment))
		{
			return true;
		}
	}
	return false;
}

int32 CountDirectObjectsWithOuter(const UObject* Outer)
{
	int32 Count = 0;
	ForEachObjectWithOuter(Outer, [&Count](UObject*)
	{
		++Count;
	}, false);
	return Count;
}

FAssetDocumentResult ValidateDocument(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentService Service;
	FAssetDocumentValidateRequest Request;
	Request.Document = Document;
	return Service.Validate(Request);
}

FAssetDocumentResult ApplyDocument(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	return Service.Apply(Request);
}

FAssetDocumentResult DiffDocument(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentService Service;
	FAssetDocumentDiffRequest Request;
	Request.Document = Document;
	return Service.Diff(Request);
}

bool ExpectExtractSyncRegion(
	FAutomationTestBase* Test,
	const TSharedRef<FJsonObject>& Document,
	const TSharedPtr<FJsonObject>& Regions,
	const FAssetDocumentRegionPolicy& Policy)
{
	if (!Test || !Regions.IsValid())
	{
		return false;
	}

	const FString RegionId = Policy.RegionId.ToString();
	const TSharedPtr<FJsonObject>* RegionState = nullptr;
	const bool bHasRegion = Test->TestTrue(
		FString::Printf(TEXT("Extract sync includes %s"), *RegionId),
		Regions->TryGetObjectField(RegionId, RegionState));
	if (!bHasRegion || !RegionState || !RegionState->IsValid())
	{
		return false;
	}

	const FString SidecarHash = (*RegionState)->GetStringField(TEXT("sidecarHash"));
	const FString AssetEvidenceHash = (*RegionState)->GetStringField(TEXT("assetEvidenceHash"));
	const FString LastSyncedAtUtc = (*RegionState)->GetStringField(TEXT("lastSyncedAtUtc"));
	const FString RegionHash = FAssetDocumentSidecarDelta::HashSidecarRegion(Document, Policy);

	Test->TestFalse(FString::Printf(TEXT("%s sidecar hash is initialized"), *RegionId), SidecarHash.IsEmpty());
	Test->TestFalse(FString::Printf(TEXT("%s asset evidence hash is initialized"), *RegionId), AssetEvidenceHash.IsEmpty());
	Test->TestFalse(FString::Printf(TEXT("%s lastSyncedAtUtc is initialized"), *RegionId), LastSyncedAtUtc.IsEmpty());
	Test->TestEqual(FString::Printf(TEXT("%s sidecar hash matches extracted region"), *RegionId), SidecarHash, RegionHash);
	Test->TestEqual(FString::Printf(TEXT("%s asset evidence starts from sidecar hash"), *RegionId), AssetEvidenceHash, SidecarHash);

	return true;
}

bool ExpectSyncRegionMatchesSidecar(
	FAutomationTestBase* Test,
	const TSharedRef<FJsonObject>& Document,
	const FAssetDocumentRegionPolicy& Policy,
	FAssetDocumentRegionSyncState& OutRegionState)
{
	FAssetDocumentSyncState SyncState;
	FString Error;
	const bool bLoaded = Test->TestTrue(
		FString::Printf(TEXT("Loads sync state for %s"), *Policy.RegionId.ToString()),
		FAssetDocumentSyncStateStore::LoadFromDocumentJson(Document, SyncState, Error));
	if (!bLoaded)
	{
		Test->AddError(Error);
		return false;
	}

	const FAssetDocumentRegionSyncState* RegionState = SyncState.Regions.Find(Policy.RegionId.ToString());
	const bool bHasRegion = Test->TestNotNull(
		FString::Printf(TEXT("Sync state includes %s"), *Policy.RegionId.ToString()),
		RegionState);
	if (!bHasRegion || !RegionState)
	{
		return false;
	}

	const FString SidecarHash = FAssetDocumentSidecarDelta::HashSidecarRegion(Document, Policy);
	Test->TestFalse(FString::Printf(TEXT("%s regenerated sidecar hash is non-empty"), *Policy.RegionId.ToString()), SidecarHash.IsEmpty());
	Test->TestEqual(FString::Printf(TEXT("%s sync sidecarHash matches regenerated region"), *Policy.RegionId.ToString()), RegionState->SidecarHash, SidecarHash);
	Test->TestEqual(FString::Printf(TEXT("%s sync assetEvidenceHash matches regenerated sidecar"), *Policy.RegionId.ToString()), RegionState->AssetEvidenceHash, RegionState->SidecarHash);
	Test->TestFalse(FString::Printf(TEXT("%s lastSyncedAtUtc is updated"), *Policy.RegionId.ToString()), RegionState->LastSyncedAtUtc.IsEmpty());

	OutRegionState = *RegionState;
	return true;
}

bool ExpectInvalidValidate(
	FAutomationTestBase* Test,
	const FString& CaseName,
	const FString& AnimReferencePath,
	TFunctionRef<void(TSharedPtr<FJsonObject>)> Mutate,
	const FString& ExpectedPath,
	const FString& ExpectedCode)
{
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(MakeUniqueMontageTarget(TEXT("AM_InvalidSemantic")), AnimReferencePath);
	Mutate(Document);

	const FAssetDocumentResult Result = ValidateDocument(Document);
	const bool bRejected = Test->TestFalse(FString::Printf(TEXT("%s: validate rejects invalid Body"), *CaseName), Result.IsSuccess());
	const bool bDiagnostic = Test->TestTrue(
		FString::Printf(TEXT("%s: validate reports expected diagnostic"), *CaseName),
		AnimMontageHasDiagnostic(Result, ExpectedPath, ExpectedCode));
	return bRejected && bDiagnostic;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyStructureTest,
	"AssetFactory.AssetDocument.AnimMontage.Apply.Structure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyStructureTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_Structure"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;

	const FAssetDocumentResult Result = Service.Apply(Request);
	TestTrue(TEXT("Apply succeeds for structured AnimMontage body"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Applied AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	TestNotNull(TEXT("Montage has skeleton"), Montage->GetSkeleton());
	TestNotNull(TEXT("Montage has preview mesh"), Montage->GetPreviewMesh());
	TestEqual(TEXT("Montage has one SlotAnimTrack"), Montage->SlotAnimTracks.Num(), 1);
	if (Montage->SlotAnimTracks.Num() == 1)
	{
		TestEqual(TEXT("SlotAnimTrack name"), Montage->SlotAnimTracks[0].SlotName, FName(TEXT("DefaultSlot")));
		TestEqual(TEXT("SlotAnimTrack has one segment"), Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num(), 1);
		if (Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 1)
		{
			TestNotNull(TEXT("AnimSegment has AnimReference"), Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference().Get());
		}
	}

	TestEqual(TEXT("Montage has two CompositeSections"), Montage->CompositeSections.Num(), 2);
	if (Montage->CompositeSections.Num() >= 2)
	{
		TestEqual(TEXT("First section name"), Montage->CompositeSections[0].SectionName, FName(TEXT("Start")));
		TestEqual(TEXT("First section next section"), Montage->CompositeSections[0].NextSectionName, FName(TEXT("End")));
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds for structured AnimMontage body"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns payload"), ExtractResult.Payload.IsValid());
	if (ExtractResult.Payload.IsValid())
	{
		const TSharedPtr<FJsonObject>* Meta = nullptr;
		TestTrue(TEXT("Extract includes _meta"), ExtractResult.Payload->TryGetObjectField(TEXT("_meta"), Meta));
		const TSharedPtr<FJsonObject>* Sync = nullptr;
		TestTrue(TEXT("Extract includes _meta.sync"), Meta && Meta->IsValid() && (*Meta)->TryGetObjectField(TEXT("sync"), Sync));
		const TSharedPtr<FJsonObject>* SyncRegions = nullptr;
		TestTrue(TEXT("Extract includes _meta.sync.regions"), Sync && Sync->IsValid() && (*Sync)->TryGetObjectField(TEXT("regions"), SyncRegions));
		if (Sync && Sync->IsValid())
		{
			TestEqual(TEXT("Extract sync schema version"), static_cast<int32>((*Sync)->GetNumberField(TEXT("schemaVersion"))), 1);
			TestEqual(TEXT("Extract sync asset object path"), (*Sync)->GetStringField(TEXT("assetObjectPath")), MakeObjectPathFromTarget(Target));
			TestFalse(TEXT("Extract sync updatedAtUtc is initialized"), (*Sync)->GetStringField(TEXT("updatedAtUtc")).IsEmpty());
		}

		FAnimMontageAssetDocumentProfile Profile;
		TArray<FAssetDocumentRegionPolicy> CompleteRegionPolicies;
		CollectRegionPolicies(this, Profile, GetCompleteAnimMontageSyncRegionIds(), CompleteRegionPolicies);
		FAssetDocumentRegionPolicy BlendPolicy;
		TestTrue(TEXT("AnimMontage profile has Body.Blend policy"), Profile.GetRegionPolicy(TEXT("Body.Blend"), BlendPolicy));
		if (SyncRegions && SyncRegions->IsValid())
		{
			for (const FAssetDocumentRegionPolicy& Policy : CompleteRegionPolicies)
			{
				ExpectExtractSyncRegion(this, ExtractResult.Payload.ToSharedRef(), *SyncRegions, Policy);
			}
		}

		const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
		TestTrue(TEXT("Extract includes Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody));
		if (ExtractedBody && ExtractedBody->IsValid())
		{
			const TSharedPtr<FJsonObject>* ExtractedReferences = nullptr;
			TestTrue(TEXT("Extract includes References"), (*ExtractedBody)->TryGetObjectField(TEXT("References"), ExtractedReferences));
			if (ExtractedReferences && ExtractedReferences->IsValid())
			{
				TestTrue(TEXT("Extract includes References.Skeleton"), (*ExtractedReferences)->HasTypedField<EJson::Object>(TEXT("Skeleton")));
			}

			const TSharedPtr<FJsonObject>* ExtractedPreview = nullptr;
			TestTrue(TEXT("Extract includes Preview"), (*ExtractedBody)->TryGetObjectField(TEXT("Preview"), ExtractedPreview));
			if (ExtractedPreview && ExtractedPreview->IsValid())
			{
				TestTrue(TEXT("Extract includes Preview.PreviewMesh"), (*ExtractedPreview)->HasTypedField<EJson::Object>(TEXT("PreviewMesh")));
			}
			TestFalse(TEXT("Extract omits legacy Skeleton"), (*ExtractedBody)->HasField(TEXT("Skeleton")));
			TestFalse(TEXT("Extract omits legacy PreviewMesh"), (*ExtractedBody)->HasField(TEXT("PreviewMesh")));

			const TArray<TSharedPtr<FJsonValue>>* ExtractedSlotAnimTracks = nullptr;
			TestTrue(TEXT("Extract includes SlotAnimTracks"), (*ExtractedBody)->TryGetArrayField(TEXT("SlotAnimTracks"), ExtractedSlotAnimTracks));
			TestTrue(TEXT("Extract has one SlotAnimTrack"), ExtractedSlotAnimTracks && ExtractedSlotAnimTracks->Num() == 1);
			if (ExtractedSlotAnimTracks && ExtractedSlotAnimTracks->Num() == 1)
			{
				const TSharedPtr<FJsonObject> ExtractedSlot = (*ExtractedSlotAnimTracks)[0]->AsObject();
				TestTrue(TEXT("Extracted SlotAnimTrack is object"), ExtractedSlot.IsValid());
				if (ExtractedSlot.IsValid())
				{
					TestEqual(TEXT("Extracted SlotName"), ExtractedSlot->GetStringField(TEXT("SlotName")), FString(TEXT("DefaultSlot")));
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* ExtractedSections = nullptr;
			TestTrue(TEXT("Extract includes CompositeSections"), (*ExtractedBody)->TryGetArrayField(TEXT("CompositeSections"), ExtractedSections));
			TestTrue(TEXT("Extract has two CompositeSections"), ExtractedSections && ExtractedSections->Num() == 2);
			if (ExtractedSections && ExtractedSections->Num() >= 2)
			{
				const TSharedPtr<FJsonObject> ExtractedStartSection = (*ExtractedSections)[0]->AsObject();
				TestTrue(TEXT("Extracted first section is object"), ExtractedStartSection.IsValid());
				if (ExtractedStartSection.IsValid())
				{
					TestEqual(TEXT("Extracted first section name"), ExtractedStartSection->GetStringField(TEXT("SectionName")), FString(TEXT("Start")));
					TestEqual(TEXT("Extracted first section next"), ExtractedStartSection->GetStringField(TEXT("NextSectionName")), FString(TEXT("End")));
				}
			}

			const TSharedPtr<FJsonObject>* ExtractedBlend = nullptr;
			TestTrue(TEXT("Extract includes Blend"), (*ExtractedBody)->TryGetObjectField(TEXT("Blend"), ExtractedBlend));
			if (ExtractedBlend && ExtractedBlend->IsValid())
			{
				TestEqual(TEXT("Extracted BlendInTime"), (*ExtractedBlend)->GetNumberField(TEXT("BlendInTime")), 0.1);
				TestEqual(TEXT("Extracted BlendOutTime"), (*ExtractedBlend)->GetNumberField(TEXT("BlendOutTime")), 0.2);
			}
		}

		if (ExtractedBody && ExtractedBody->IsValid())
		{
			const FString BlendHashBeforeSkipped = FAssetDocumentSidecarDelta::HashSidecarRegion(ExtractResult.Payload.ToSharedRef(), BlendPolicy);
			(*ExtractedBody)->RemoveField(TEXT("_Skipped"));
			TestEqual(TEXT("Body _Skipped does not affect region hash when removed"), FAssetDocumentSidecarDelta::HashSidecarRegion(ExtractResult.Payload.ToSharedRef(), BlendPolicy), BlendHashBeforeSkipped);
			(*ExtractedBody)->SetObjectField(TEXT("_Skipped"), MakeShared<FJsonObject>());
			TestEqual(TEXT("Body _Skipped does not affect region hash when added"), FAssetDocumentSidecarDelta::HashSidecarRegion(ExtractResult.Payload.ToSharedRef(), BlendPolicy), BlendHashBeforeSkipped);
			(*ExtractedBody)->RemoveField(TEXT("_Skipped"));
			TestEqual(TEXT("Body _Skipped does not affect region hash after add/remove"), FAssetDocumentSidecarDelta::HashSidecarRegion(ExtractResult.Payload.ToSharedRef(), BlendPolicy), BlendHashBeforeSkipped);

			FAssetDocumentValidateRequest ValidateRequest;
			ValidateRequest.Document = ExtractResult.Payload;
			const FAssetDocumentResult ValidateResult = Service.Validate(ValidateRequest);
			TestTrue(TEXT("Validate accepts document with _meta.sync"), ValidateResult.IsSuccess());
			if (!ValidateResult.IsSuccess())
			{
				AddError(ValidateResult.Message);
			}

			ExtractResult.Payload->RemoveField(TEXT("_meta"));
			TestEqual(TEXT("Top-level _meta does not affect region hash"), FAssetDocumentSidecarDelta::HashSidecarRegion(ExtractResult.Payload.ToSharedRef(), BlendPolicy), BlendHashBeforeSkipped);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyExtractScalarRegionsTest,
	"AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ScalarRegions.Core",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyExtractScalarRegionsTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequenceBase = CreateAnimSequenceFixture();
	UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimSequenceBase);
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_ScalarRegions"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SetScalarRegions(Document, AnimSequence->GetPathName());

	const FAssetDocumentResult Result = ApplyDocument(Document);
	TestTrue(TEXT("Apply succeeds for scalar regions"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Applied scalar region AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	TestNotNull(TEXT("Scalar regions set skeleton"), Montage->GetSkeleton());
	TestNotNull(TEXT("Scalar regions set preview mesh"), Montage->GetPreviewMesh());
	TestTrue(TEXT("Scalar regions set preview base pose"), Montage->PreviewBasePose.Get() == AnimSequence);
	TestEqual(TEXT("Scalar regions set sync group"), Montage->SyncGroup, FName(TEXT("AssetDocSync")));
	TestEqual(TEXT("Scalar regions set sync slot index"), Montage->SyncSlotIndex, 0);
	TestTrue(TEXT("Scalar regions enable root motion translation"), Montage->bEnableRootMotionTranslation);
	TestTrue(TEXT("Scalar regions enable root motion rotation"), Montage->bEnableRootMotionRotation);
	TestEqual(TEXT("Scalar regions set root motion root lock"), Montage->RootMotionRootLock, ERootMotionRootLock::Zero);

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds for scalar regions"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns scalar region payload"), ExtractResult.Payload.IsValid());
	if (!ExtractResult.IsSuccess() || !ExtractResult.Payload.IsValid())
	{
		AddError(ExtractResult.Message);
		return false;
	}

	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	TestTrue(TEXT("Extract includes Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody));
	if (!ExtractedBody || !ExtractedBody->IsValid())
	{
		return false;
	}

	TestFalse(TEXT("Extract omits legacy Skeleton field"), (*ExtractedBody)->HasField(TEXT("Skeleton")));
	TestFalse(TEXT("Extract omits legacy PreviewMesh field"), (*ExtractedBody)->HasField(TEXT("PreviewMesh")));

	const TSharedPtr<FJsonObject>* References = nullptr;
	TestTrue(TEXT("Extract includes Body.References"), (*ExtractedBody)->TryGetObjectField(TEXT("References"), References));
	if (References && References->IsValid())
	{
		TestTrue(TEXT("Extract includes Body.References.Skeleton"), (*References)->HasTypedField<EJson::Object>(TEXT("Skeleton")));
	}

	const TSharedPtr<FJsonObject>* Preview = nullptr;
	TestTrue(TEXT("Extract includes Body.Preview"), (*ExtractedBody)->TryGetObjectField(TEXT("Preview"), Preview));
	if (Preview && Preview->IsValid())
	{
		TestTrue(TEXT("Extract includes Body.Preview.PreviewMesh"), (*Preview)->HasTypedField<EJson::Object>(TEXT("PreviewMesh")));
		TestTrue(TEXT("Extract includes Body.Preview.PreviewBasePose"), (*Preview)->HasTypedField<EJson::Object>(TEXT("PreviewBasePose")));
	}

	const TSharedPtr<FJsonObject>* Sync = nullptr;
	TestTrue(TEXT("Extract includes Body.Sync"), (*ExtractedBody)->TryGetObjectField(TEXT("Sync"), Sync));
	if (Sync && Sync->IsValid())
	{
		TestEqual(TEXT("Extracted SyncGroup"), (*Sync)->GetStringField(TEXT("SyncGroup")), FString(TEXT("AssetDocSync")));
		TestEqual(TEXT("Extracted SyncSlotIndex"), static_cast<int32>((*Sync)->GetNumberField(TEXT("SyncSlotIndex"))), 0);
	}

	const TSharedPtr<FJsonObject>* RootMotion = nullptr;
	TestTrue(TEXT("Extract includes Body.RootMotion"), (*ExtractedBody)->TryGetObjectField(TEXT("RootMotion"), RootMotion));
	if (RootMotion && RootMotion->IsValid())
	{
		TestTrue(TEXT("Extracted bEnableRootMotionTranslation"), (*RootMotion)->GetBoolField(TEXT("bEnableRootMotionTranslation")));
		TestTrue(TEXT("Extracted bEnableRootMotionRotation"), (*RootMotion)->GetBoolField(TEXT("bEnableRootMotionRotation")));
		TestEqual(TEXT("Extracted RootMotionRootLock"), (*RootMotion)->GetStringField(TEXT("RootMotionRootLock")), FString(TEXT("Zero")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageExpandedBlendTest,
	"AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ExpandedBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageExpandedBlendTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_ExpandedBlend"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> Blend = Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Blend"));
	Blend->SetNumberField(TEXT("BlendInTime"), 0.2);
	Blend->SetNumberField(TEXT("BlendOutTime"), 0.3);
	Blend->SetStringField(TEXT("BlendModeIn"), TEXT("Inertialization"));
	Blend->SetStringField(TEXT("BlendModeOut"), TEXT("Standard"));
	Blend->SetNumberField(TEXT("BlendOutTriggerTime"), 0.15);
	Blend->SetBoolField(TEXT("bEnableAutoBlendOut"), false);

	const FAssetDocumentResult ApplyResult = ApplyDocument(Document);
	TestTrue(TEXT("Apply succeeds for expanded Blend"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Applied expanded Blend AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	TestTrue(TEXT("Expanded Blend sets BlendInTime"), FMath::IsNearlyEqual(Montage->GetDefaultBlendInTime(), 0.2f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Expanded Blend sets BlendOutTime"), FMath::IsNearlyEqual(Montage->GetDefaultBlendOutTime(), 0.3f, KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Expanded Blend sets BlendModeIn"), Montage->BlendModeIn, EMontageBlendMode::Inertialization);
	TestEqual(TEXT("Expanded Blend sets BlendModeOut"), Montage->BlendModeOut, EMontageBlendMode::Standard);
	TestTrue(TEXT("Expanded Blend sets BlendOutTriggerTime"), FMath::IsNearlyEqual(Montage->BlendOutTriggerTime, 0.15f, KINDA_SMALL_NUMBER));
	TestFalse(TEXT("Expanded Blend disables auto blend out"), Montage->bEnableAutoBlendOut);

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds for expanded Blend"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns expanded Blend payload"), ExtractResult.Payload.IsValid());
	if (!ExtractResult.IsSuccess() || !ExtractResult.Payload.IsValid())
	{
		AddError(ExtractResult.Message);
		return false;
	}

	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	TestTrue(TEXT("Extract includes Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody));
	if (!ExtractedBody || !ExtractedBody->IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* ExtractedBlend = nullptr;
	TestTrue(TEXT("Extract includes Body.Blend"), (*ExtractedBody)->TryGetObjectField(TEXT("Blend"), ExtractedBlend));
	if (!ExtractedBlend || !ExtractedBlend->IsValid())
	{
		return false;
	}

	TestTrue(TEXT("Extracted BlendInTime"), FMath::IsNearlyEqual((*ExtractedBlend)->GetNumberField(TEXT("BlendInTime")), 0.2, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Extracted BlendOutTime"), FMath::IsNearlyEqual((*ExtractedBlend)->GetNumberField(TEXT("BlendOutTime")), 0.3, KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Extracted BlendModeIn"), (*ExtractedBlend)->GetStringField(TEXT("BlendModeIn")), FString(TEXT("Inertialization")));
	TestEqual(TEXT("Extracted BlendModeOut"), (*ExtractedBlend)->GetStringField(TEXT("BlendModeOut")), FString(TEXT("Standard")));
	TestTrue(TEXT("Extracted BlendOutTriggerTime"), FMath::IsNearlyEqual((*ExtractedBlend)->GetNumberField(TEXT("BlendOutTriggerTime")), 0.15, KINDA_SMALL_NUMBER));
	TestFalse(TEXT("Extracted bEnableAutoBlendOut"), (*ExtractedBlend)->GetBoolField(TEXT("bEnableAutoBlendOut")));

	const FString AnimReferencePath = AnimSequence->GetPathName();
	bool bAllCasesPassed = true;
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Invalid BlendModeIn"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Blend"))->SetStringField(TEXT("BlendModeIn"), TEXT("Bad"));
	}, TEXT("/Body/Blend/BlendModeIn"), TEXT("InvalidBlendMode"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Invalid BlendOutTriggerTime"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Blend"))->SetNumberField(TEXT("BlendOutTriggerTime"), -2.0);
	}, TEXT("/Body/Blend/BlendOutTriggerTime"), TEXT("InvalidBlendOutTriggerTime"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Invalid bEnableAutoBlendOut"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Blend"))->SetStringField(TEXT("bEnableAutoBlendOut"), TEXT("yes"));
	}, TEXT("/Body/Blend/bEnableAutoBlendOut"), TEXT("InvalidBooleanField"));

	return bAllCasesPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageCurvesAndTimeStretchTest,
	"AssetFactory.AssetDocument.AnimMontage.ApplyExtract.CurvesAndTimeStretch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageCurvesAndTimeStretchTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FName CurveName(TEXT("MontageTimeStretchCurve"));
	const FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);
	if (IAnimationDataModel* SequenceDataModel = AnimSequence->GetDataModel())
	{
		TestNull(TEXT("Fixture AnimSequence starts without montage curve"), SequenceDataModel->FindFloatCurve(CurveId));
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_CurvesAndTimeStretch"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SetCurvesAndTimeStretch(Document);

	const FAssetDocumentResult ApplyResult = ApplyDocument(Document);
	TestTrue(TEXT("Apply succeeds for curves and time stretch"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Applied curves/time stretch AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	const IAnimationDataModel* MontageDataModel = Montage->GetDataModel();
	TestNotNull(TEXT("Montage data model is available"), MontageDataModel);
	const FFloatCurve* MontageCurve = MontageDataModel ? MontageDataModel->FindFloatCurve(CurveId) : nullptr;
	TestNotNull(TEXT("Montage data model has MontageTimeStretchCurve"), MontageCurve);
	if (MontageCurve)
	{
		TArray<float> Times;
		TArray<float> Values;
		MontageCurve->GetKeys(Times, Values);
		TestEqual(TEXT("Montage curve has three keys"), Times.Num(), 3);
		if (Times.Num() == 3 && Values.Num() == 3)
		{
			TestTrue(TEXT("Montage curve key 0 is sorted"), FMath::IsNearlyEqual(Times[0], 0.0f) && FMath::IsNearlyEqual(Values[0], 0.0f));
			TestTrue(TEXT("Montage curve key 1 is sorted"), FMath::IsNearlyEqual(Times[1], 0.5f) && FMath::IsNearlyEqual(Values[1], 1.0f));
			TestTrue(TEXT("Montage curve key 2 is sorted"), FMath::IsNearlyEqual(Times[2], 1.0f) && FMath::IsNearlyEqual(Values[2], 0.0f));
		}
	}
	TestEqual(TEXT("Time stretch curve name is assigned"), Montage->TimeStretchCurveName, CurveName);

	if (IAnimationDataModel* SequenceDataModel = AnimSequence->GetDataModel())
	{
		TestNull(TEXT("Referenced AnimSequence does not receive montage-owned curve"), SequenceDataModel->FindFloatCurve(CurveId));
	}

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds for curves and time stretch"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns curves/time stretch payload"), ExtractResult.Payload.IsValid());
	if (!ExtractResult.IsSuccess() || !ExtractResult.Payload.IsValid())
	{
		AddError(ExtractResult.Message);
		return false;
	}

	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	TestTrue(TEXT("Extract includes Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody));
	if (!ExtractedBody || !ExtractedBody->IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ExtractedCurves = nullptr;
	TestTrue(TEXT("Extract includes Body.Curves"), (*ExtractedBody)->TryGetArrayField(TEXT("Curves"), ExtractedCurves));
	TestTrue(TEXT("Extracted Body.Curves has one curve"), ExtractedCurves && ExtractedCurves->Num() == 1);
	if (ExtractedCurves && ExtractedCurves->Num() == 1)
	{
		const TSharedPtr<FJsonObject> ExtractedCurve = (*ExtractedCurves)[0]->AsObject();
		TestTrue(TEXT("Extracted curve is object"), ExtractedCurve.IsValid());
		if (ExtractedCurve.IsValid())
		{
			TestEqual(TEXT("Extracted curve name"), ExtractedCurve->GetStringField(TEXT("Name")), FString(TEXT("MontageTimeStretchCurve")));
			const TArray<TSharedPtr<FJsonValue>>* ExtractedKeys = nullptr;
			TestTrue(TEXT("Extracted curve includes Keys"), ExtractedCurve->TryGetArrayField(TEXT("Keys"), ExtractedKeys));
			TestTrue(TEXT("Extracted curve has three keys"), ExtractedKeys && ExtractedKeys->Num() == 3);
		}
	}

	const TSharedPtr<FJsonObject>* ExtractedTimeStretch = nullptr;
	TestTrue(TEXT("Extract includes Body.TimeStretch"), (*ExtractedBody)->TryGetObjectField(TEXT("TimeStretch"), ExtractedTimeStretch));
	if (ExtractedTimeStretch && ExtractedTimeStretch->IsValid())
	{
		TestEqual(TEXT("Extracted TimeStretchCurveName"), (*ExtractedTimeStretch)->GetStringField(TEXT("TimeStretchCurveName")), FString(TEXT("MontageTimeStretchCurve")));
		TestTrue(TEXT("Extracted SamplingRate"), FMath::IsNearlyEqual((*ExtractedTimeStretch)->GetNumberField(TEXT("SamplingRate")), 30.0, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Extracted CurveValueMinPrecision"), FMath::IsNearlyEqual((*ExtractedTimeStretch)->GetNumberField(TEXT("CurveValueMinPrecision")), 0.02, KINDA_SMALL_NUMBER));
		TestFalse(TEXT("Extracted TimeStretch omits baked Markers"), (*ExtractedTimeStretch)->HasField(TEXT("Markers")));
		TestFalse(TEXT("Extracted TimeStretch omits baked Sum_dT_i_by_C_i"), (*ExtractedTimeStretch)->HasField(TEXT("Sum_dT_i_by_C_i")));
	}

	TArray<float> OriginalTimes;
	TArray<float> OriginalValues;
	if (MontageCurve)
	{
		MontageCurve->GetKeys(OriginalTimes, OriginalValues);
	}

	const FString AnimReferencePath = AnimSequence->GetPathName();
	bool bAllCasesPassed = true;
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Invalid curve name"), AnimReferencePath, [](TSharedPtr<FJsonObject> InvalidDocument)
	{
		SetCurvesAndTimeStretch(InvalidDocument);
		InvalidDocument->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("Curves"))[0]->AsObject()->SetStringField(TEXT("Name"), TEXT(""));
	}, TEXT("/Body/Curves/0/Name"), TEXT("InvalidCurveName"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Invalid curve keys"), AnimReferencePath, [](TSharedPtr<FJsonObject> InvalidDocument)
	{
		SetCurvesAndTimeStretch(InvalidDocument);
		InvalidDocument->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("Curves"))[0]->AsObject()->SetArrayField(TEXT("Keys"), TArray<TSharedPtr<FJsonValue>>());
	}, TEXT("/Body/Curves/0/Keys"), TEXT("InvalidCurveKeys"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Invalid curve key time"), AnimReferencePath, [](TSharedPtr<FJsonObject> InvalidDocument)
	{
		SetCurvesAndTimeStretch(InvalidDocument);
		InvalidDocument->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("Curves"))[0]->AsObject()->GetArrayField(TEXT("Keys"))[0]->AsObject()->SetNumberField(TEXT("Time"), -1.0);
	}, TEXT("/Body/Curves/0/Keys/0/Time"), TEXT("InvalidCurveKeyTime"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Invalid curve key value"), AnimReferencePath, [](TSharedPtr<FJsonObject> InvalidDocument)
	{
		SetCurvesAndTimeStretch(InvalidDocument);
		InvalidDocument->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("Curves"))[0]->AsObject()->GetArrayField(TEXT("Keys"))[0]->AsObject()->SetStringField(TEXT("Value"), TEXT("fast"));
	}, TEXT("/Body/Curves/0/Keys/0/Value"), TEXT("InvalidCurveKeyValue"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Duplicate curve name"), AnimReferencePath, [](TSharedPtr<FJsonObject> InvalidDocument)
	{
		SetCurvesAndTimeStretch(InvalidDocument);
		TArray<TSharedPtr<FJsonValue>> Curves = InvalidDocument->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("Curves"));
		Curves.Add(MakeShared<FJsonValueObject>(MakeMontageTimeStretchCurve()));
		InvalidDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Curves"), Curves);
	}, TEXT("/Body/Curves/1/Name"), TEXT("DuplicateCurveName"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Unsupported curve flag"), AnimReferencePath, [](TSharedPtr<FJsonObject> InvalidDocument)
	{
		SetCurvesAndTimeStretch(InvalidDocument);
		TSharedPtr<FJsonObject> Curve = InvalidDocument->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("Curves"))[0]->AsObject();
		TArray<TSharedPtr<FJsonValue>> Flags;
		Flags.Add(MakeShared<FJsonValueString>(TEXT("Disabled")));
		Curve->SetArrayField(TEXT("Flags"), Flags);
	}, TEXT("/Body/Curves/0/Flags/0"), TEXT("InvalidCurveFlags"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Missing time stretch curve in provided curves"), AnimReferencePath, [](TSharedPtr<FJsonObject> InvalidDocument)
	{
		SetCurvesAndTimeStretch(InvalidDocument);
		InvalidDocument->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("TimeStretch"))->SetStringField(TEXT("TimeStretchCurveName"), TEXT("MissingCurve"));
	}, TEXT("/Body/TimeStretch/TimeStretchCurveName"), TEXT("MissingTimeStretchCurve"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Invalid time stretch sampling rate"), AnimReferencePath, [](TSharedPtr<FJsonObject> InvalidDocument)
	{
		SetCurvesAndTimeStretch(InvalidDocument);
		InvalidDocument->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("TimeStretch"))->SetNumberField(TEXT("SamplingRate"), 0.0);
	}, TEXT("/Body/TimeStretch/SamplingRate"), TEXT("InvalidTimeStretchSamplingRate"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Invalid time stretch curve precision"), AnimReferencePath, [](TSharedPtr<FJsonObject> InvalidDocument)
	{
		SetCurvesAndTimeStretch(InvalidDocument);
		InvalidDocument->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("TimeStretch"))->SetNumberField(TEXT("CurveValueMinPrecision"), -0.1);
	}, TEXT("/Body/TimeStretch/CurveValueMinPrecision"), TEXT("InvalidTimeStretchCurveValueMinPrecision"));

	TSharedPtr<FJsonObject> DuplicateApplyDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SetCurvesAndTimeStretch(DuplicateApplyDocument);
	TArray<TSharedPtr<FJsonValue>> DuplicateCurves = DuplicateApplyDocument->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("Curves"));
	DuplicateCurves.Add(MakeShared<FJsonValueObject>(MakeMontageTimeStretchCurve()));
	DuplicateApplyDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Curves"), DuplicateCurves);
	const FAssetDocumentResult DuplicateApplyResult = ApplyDocument(DuplicateApplyDocument);
	TestFalse(TEXT("Duplicate curve apply is rejected before production mutation"), DuplicateApplyResult.IsSuccess());
	TestTrue(TEXT("Duplicate curve apply reports DuplicateCurveName"), AnimMontageHasDiagnostic(DuplicateApplyResult, TEXT("/Body/Curves/1/Name"), TEXT("DuplicateCurveName")));
	const FFloatCurve* CurveAfterDuplicateApply = Montage->GetDataModel() ? Montage->GetDataModel()->FindFloatCurve(CurveId) : nullptr;
	TestNotNull(TEXT("Original curve remains after duplicate curve apply failure"), CurveAfterDuplicateApply);
	if (CurveAfterDuplicateApply)
	{
		TArray<float> TimesAfterDuplicate;
		TArray<float> ValuesAfterDuplicate;
		CurveAfterDuplicateApply->GetKeys(TimesAfterDuplicate, ValuesAfterDuplicate);
		TestEqual(TEXT("Original curve key count remains after duplicate curve apply failure"), TimesAfterDuplicate.Num(), OriginalTimes.Num());
		for (int32 KeyIndex = 0; KeyIndex < FMath::Min(TimesAfterDuplicate.Num(), OriginalTimes.Num()); ++KeyIndex)
		{
			TestTrue(TEXT("Original curve times remain after duplicate curve apply failure"), FMath::IsNearlyEqual(TimesAfterDuplicate[KeyIndex], OriginalTimes[KeyIndex]));
			TestTrue(TEXT("Original curve values remain after duplicate curve apply failure"), ValuesAfterDuplicate.IsValidIndex(KeyIndex) && OriginalValues.IsValidIndex(KeyIndex) && FMath::IsNearlyEqual(ValuesAfterDuplicate[KeyIndex], OriginalValues[KeyIndex]));
		}
	}

	TSharedPtr<FJsonObject> MissingCurveApplyDocument = MakeMontageDocument(Target);
	TSharedPtr<FJsonObject> MissingCurveTimeStretch = MakeShared<FJsonObject>();
	MissingCurveTimeStretch->SetStringField(TEXT("TimeStretchCurveName"), TEXT("MissingCurve"));
	MissingCurveApplyDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("TimeStretch"), MissingCurveTimeStretch);
	const FAssetDocumentResult MissingCurveApplyResult = ApplyDocument(MissingCurveApplyDocument);
	TestFalse(TEXT("Missing time stretch curve apply is rejected before production mutation"), MissingCurveApplyResult.IsSuccess());
	TestTrue(TEXT("Missing time stretch curve apply reports MissingTimeStretchCurve"), AnimMontageHasDiagnostic(MissingCurveApplyResult, TEXT("/Body/TimeStretch/TimeStretchCurveName"), TEXT("MissingTimeStretchCurve")));
	TestEqual(TEXT("TimeStretchCurveName remains after missing curve apply failure"), Montage->TimeStretchCurveName, CurveName);
	TestNotNull(TEXT("Original curve remains after missing time stretch curve apply failure"), Montage->GetDataModel() ? Montage->GetDataModel()->FindFloatCurve(CurveId) : nullptr);

	const FName OriginalSyncGroup = Montage->SyncGroup;
	const bool bOriginalRootMotionTranslation = Montage->bEnableRootMotionTranslation;
	const bool bOriginalRootMotionRotation = Montage->bEnableRootMotionRotation;
	const ERootMotionRootLock::Type OriginalRootMotionRootLock = Montage->RootMotionRootLock;
	TSharedPtr<FJsonObject> MissingCurveWithEarlierRegionsDocument = MakeMontageDocument(Target);
	TSharedPtr<FJsonObject> EarlierSync = MakeShared<FJsonObject>();
	EarlierSync->SetStringField(TEXT("SyncGroup"), TEXT("ShouldNotApply"));
	MissingCurveWithEarlierRegionsDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("Sync"), EarlierSync);
	TSharedPtr<FJsonObject> EarlierRootMotion = MakeShared<FJsonObject>();
	EarlierRootMotion->SetBoolField(TEXT("bEnableRootMotionTranslation"), !bOriginalRootMotionTranslation);
	EarlierRootMotion->SetBoolField(TEXT("bEnableRootMotionRotation"), !bOriginalRootMotionRotation);
	EarlierRootMotion->SetStringField(TEXT("RootMotionRootLock"), TEXT("Zero"));
	MissingCurveWithEarlierRegionsDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("RootMotion"), EarlierRootMotion);
	TSharedPtr<FJsonObject> EarlierTimeStretch = MakeShared<FJsonObject>();
	EarlierTimeStretch->SetStringField(TEXT("TimeStretchCurveName"), TEXT("MissingCurve"));
	MissingCurveWithEarlierRegionsDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("TimeStretch"), EarlierTimeStretch);
	const FAssetDocumentResult MissingCurveWithEarlierRegionsResult = ApplyDocument(MissingCurveWithEarlierRegionsDocument);
	TestFalse(TEXT("Missing time stretch curve rejects before earlier region mutation"), MissingCurveWithEarlierRegionsResult.IsSuccess());
	TestTrue(TEXT("Missing time stretch curve with earlier regions reports MissingTimeStretchCurve"), AnimMontageHasDiagnostic(MissingCurveWithEarlierRegionsResult, TEXT("/Body/TimeStretch/TimeStretchCurveName"), TEXT("MissingTimeStretchCurve")));
	TestEqual(TEXT("SyncGroup remains after invalid time stretch preflight"), Montage->SyncGroup, OriginalSyncGroup);
	TestEqual(TEXT("RootMotion translation remains after invalid time stretch preflight"), Montage->bEnableRootMotionTranslation, bOriginalRootMotionTranslation);
	TestEqual(TEXT("RootMotion rotation remains after invalid time stretch preflight"), Montage->bEnableRootMotionRotation, bOriginalRootMotionRotation);
	TestEqual(TEXT("RootMotion lock remains after invalid time stretch preflight"), Montage->RootMotionRootLock, OriginalRootMotionRootLock);

	IAnimationDataController& Controller = Montage->GetController();
	Controller.SetCurveFlags(CurveId, AACF_Disabled, false);
	const FAssetDocumentResult UnsupportedFlagsExtractResult = Service.Extract(ExtractRequest);
	TestFalse(TEXT("Extract rejects unsupported curve flags"), UnsupportedFlagsExtractResult.IsSuccess());
	TestTrue(TEXT("Extract reports UnsupportedCurveFlags"), AnimMontageHasDiagnostic(UnsupportedFlagsExtractResult, TEXT("/Body/Curves/0/Flags"), TEXT("UnsupportedCurveFlags")));

	return bAllCasesPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageMetadataRegionsTest,
	"AssetFactory.AssetDocument.AnimMontage.ApplyExtract.MetadataRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageMetadataRegionsTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_MetadataRegions"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	const FAnimMetaDataClassFixture AnimMetaDataClassFixture = CreateAnimMetaDataClassFixture();
	ON_SCOPE_EXIT
	{
		AnimMetaDataClassFixture.Cleanup();
	};
	const FString AnimMetaDataClassPath = AnimMetaDataClassFixture.ClassPath;
	TestTrue(TEXT("Concrete AnimMetaData fixture class path is available"), AnimMetaDataClassFixture.IsValid());
	if (!AnimMetaDataClassFixture.IsValid())
	{
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> MetadataValues;
	MetadataValues.Add(MakeShared<FJsonValueObject>(MakeAnimMetaDataRef(AnimMetaDataClassPath)));
	Body->SetArrayField(TEXT("Metadata"), MetadataValues);

	TSharedPtr<FJsonObject> SectionMetadata = MakeShared<FJsonObject>();
	SectionMetadata->SetArrayField(TEXT("Start"), MetadataValues);
	TArray<TSharedPtr<FJsonValue>> EmptySectionMetadata;
	SectionMetadata->SetArrayField(TEXT("End"), EmptySectionMetadata);
	Body->SetObjectField(TEXT("SectionMetadata"), SectionMetadata);

	const FAssetDocumentResult ApplyResult = ApplyDocument(Document);
	TestTrue(TEXT("Apply succeeds for metadata regions"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Applied metadata region AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	TestEqual(TEXT("Asset metadata region has one entry"), Montage->GetMetaData().Num(), 1);
	if (Montage->GetMetaData().Num() == 1)
	{
		TestTrue(TEXT("Asset metadata entry is UAnimMetaData"), Montage->GetMetaData()[0]->IsA(UAnimMetaData::StaticClass()));
		TestTrue(TEXT("Asset metadata outer is montage"), Montage->GetMetaData()[0]->GetOuter() == Montage);
	}

	TestTrue(TEXT("Montage has parsed CompositeSections"), Montage->CompositeSections.Num() >= 2);
	if (Montage->CompositeSections.Num() >= 2)
	{
		TestEqual(TEXT("Start section metadata has one entry"), Montage->CompositeSections[0].GetMetaData().Num(), 1);
		if (Montage->CompositeSections[0].GetMetaData().Num() == 1)
		{
			TestTrue(TEXT("Start section metadata entry is UAnimMetaData"), Montage->CompositeSections[0].GetMetaData()[0]->IsA(UAnimMetaData::StaticClass()));
			TestTrue(TEXT("Start section metadata outer is montage"), Montage->CompositeSections[0].GetMetaData()[0]->GetOuter() == Montage);
		}
		TestEqual(TEXT("Empty section metadata clears End section"), Montage->CompositeSections[1].GetMetaData().Num(), 0);
	}
	if (Montage->CompositeSections.Num() < 2)
	{
		return false;
	}

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds for metadata regions"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns metadata region payload"), ExtractResult.Payload.IsValid());
	if (!ExtractResult.IsSuccess() || !ExtractResult.Payload.IsValid())
	{
		AddError(ExtractResult.Message);
		return false;
	}

	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	TestTrue(TEXT("Extract includes Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody));
	if (!ExtractedBody || !ExtractedBody->IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ExtractedMetadata = nullptr;
	TestTrue(TEXT("Extract includes Body.Metadata"), (*ExtractedBody)->TryGetArrayField(TEXT("Metadata"), ExtractedMetadata));
	TestTrue(TEXT("Extracted Body.Metadata has one entry"), ExtractedMetadata && ExtractedMetadata->Num() == 1);
	if (ExtractedMetadata && ExtractedMetadata->Num() == 1)
	{
		const TSharedPtr<FJsonObject> ExtractedObject = (*ExtractedMetadata)[0]->AsObject();
		TestTrue(TEXT("Extracted metadata entry is object"), ExtractedObject.IsValid());
		if (ExtractedObject.IsValid())
		{
			TestEqual(TEXT("Extracted metadata kind"), ExtractedObject->GetStringField(TEXT("Kind")), FString(TEXT("EmbeddedObject")));
			TestEqual(TEXT("Extracted metadata class"), ExtractedObject->GetStringField(TEXT("Class")), AnimMetaDataClassPath);
		}
	}

	const TSharedPtr<FJsonObject>* ExtractedSectionMetadata = nullptr;
	TestTrue(TEXT("Extract includes Body.SectionMetadata"), (*ExtractedBody)->TryGetObjectField(TEXT("SectionMetadata"), ExtractedSectionMetadata));
	if (ExtractedSectionMetadata && ExtractedSectionMetadata->IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ExtractedStartMetadata = nullptr;
		TestTrue(TEXT("Extract includes Start section metadata"), (*ExtractedSectionMetadata)->TryGetArrayField(TEXT("Start"), ExtractedStartMetadata));
		TestTrue(TEXT("Extracted Start metadata has one entry"), ExtractedStartMetadata && ExtractedStartMetadata->Num() == 1);
		const TArray<TSharedPtr<FJsonValue>>* ExtractedEndMetadata = nullptr;
		TestTrue(TEXT("Extract includes empty End section metadata"), (*ExtractedSectionMetadata)->TryGetArrayField(TEXT("End"), ExtractedEndMetadata));
		TestTrue(TEXT("Extracted End metadata remains empty"), ExtractedEndMetadata && ExtractedEndMetadata->IsEmpty());
	}

	TSharedPtr<FJsonObject> UnknownSectionDocument = MakeStructuredMontageDocument(MakeUniqueMontageTarget(TEXT("AM_MetadataUnknownSection")), AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> UnknownSectionBody = UnknownSectionDocument->GetObjectField(TEXT("Body"));
	TSharedPtr<FJsonObject> UnknownSectionMetadata = MakeShared<FJsonObject>();
	UnknownSectionMetadata->SetArrayField(TEXT("Missing"), MetadataValues);
	UnknownSectionBody->SetObjectField(TEXT("SectionMetadata"), UnknownSectionMetadata);

	const FAssetDocumentResult UnknownSectionResult = ValidateDocument(UnknownSectionDocument);
	TestFalse(TEXT("Unknown section metadata target is rejected"), UnknownSectionResult.IsSuccess());
	TestTrue(
		TEXT("Unknown section metadata target reports UnknownSectionMetadataTarget"),
		AnimMontageHasDiagnostic(UnknownSectionResult, TEXT("/Body/SectionMetadata/Missing"), TEXT("UnknownSectionMetadataTarget")));

	const int32 OriginalAssetMetadataCount = Montage->GetMetaData().Num();
	const int32 OriginalStartSectionMetadataCount = Montage->CompositeSections[0].GetMetaData().Num();
	const int32 OriginalEndSectionMetadataCount = Montage->CompositeSections[1].GetMetaData().Num();
	const int32 OriginalDirectObjectCount = CountDirectObjectsWithOuter(Montage);

	TSharedPtr<FJsonObject> InvalidApplyDocument = MakeMontageDocument(Target);
	TSharedPtr<FJsonObject> InvalidApplyBody = InvalidApplyDocument->GetObjectField(TEXT("Body"));
	InvalidApplyBody->SetArrayField(TEXT("Metadata"), MetadataValues);
	TSharedPtr<FJsonObject> InvalidApplySectionMetadata = MakeShared<FJsonObject>();
	InvalidApplySectionMetadata->SetArrayField(TEXT("Missing"), MetadataValues);
	InvalidApplyBody->SetObjectField(TEXT("SectionMetadata"), InvalidApplySectionMetadata);

	const FAssetDocumentResult InvalidApplyResult = ApplyDocument(InvalidApplyDocument);
	TestFalse(TEXT("Unknown section metadata target apply is rejected"), InvalidApplyResult.IsSuccess());
	TestTrue(
		TEXT("Unknown section metadata target apply reports UnknownSectionMetadataTarget"),
		AnimMontageHasDiagnostic(InvalidApplyResult, TEXT("/Body/SectionMetadata/Missing"), TEXT("UnknownSectionMetadataTarget")));
	TestEqual(TEXT("Asset metadata unchanged after failed metadata apply"), Montage->GetMetaData().Num(), OriginalAssetMetadataCount);
	TestEqual(TEXT("Start section metadata unchanged after failed metadata apply"), Montage->CompositeSections[0].GetMetaData().Num(), OriginalStartSectionMetadataCount);
	TestEqual(TEXT("End section metadata unchanged after failed metadata apply"), Montage->CompositeSections[1].GetMetaData().Num(), OriginalEndSectionMetadataCount);
	TestEqual(TEXT("Failed metadata apply does not create direct child objects under production montage"), CountDirectObjectsWithOuter(Montage), OriginalDirectObjectCount);

	TSharedPtr<FJsonObject> MissingClassDocument = MakeStructuredMontageDocument(MakeUniqueMontageTarget(TEXT("AM_MetadataMissingClass")), AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> MissingClassObject = MakeShared<FJsonObject>();
	MissingClassObject->SetStringField(TEXT("Kind"), TEXT("EmbeddedObject"));
	MissingClassObject->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	TArray<TSharedPtr<FJsonValue>> MissingClassMetadata;
	MissingClassMetadata.Add(MakeShared<FJsonValueObject>(MissingClassObject));
	MissingClassDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Metadata"), MissingClassMetadata);
	const FAssetDocumentResult MissingClassResult = ValidateDocument(MissingClassDocument);
	TestFalse(TEXT("Metadata EmbeddedObject missing Class is rejected"), MissingClassResult.IsSuccess());
	TestTrue(TEXT("Metadata EmbeddedObject missing Class reports diagnostic"), AnimMontageHasDiagnostic(MissingClassResult, TEXT("/Body/Metadata/0"), TEXT("missing-embeddedobject-class")));

	TSharedPtr<FJsonObject> MissingDefinitionIdDocument = MakeStructuredMontageDocument(MakeUniqueMontageTarget(TEXT("AM_MetadataMissingDefinitionId")), AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> MissingDefinitionIdObject = MakeShared<FJsonObject>();
	MissingDefinitionIdObject->SetStringField(TEXT("Kind"), TEXT("DefinitionRef"));
	TArray<TSharedPtr<FJsonValue>> MissingDefinitionIdMetadata;
	MissingDefinitionIdMetadata.Add(MakeShared<FJsonValueObject>(MissingDefinitionIdObject));
	MissingDefinitionIdDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Metadata"), MissingDefinitionIdMetadata);
	const FAssetDocumentResult MissingDefinitionIdResult = ValidateDocument(MissingDefinitionIdDocument);
	TestFalse(TEXT("Metadata DefinitionRef missing Id is rejected"), MissingDefinitionIdResult.IsSuccess());
	TestTrue(TEXT("Metadata DefinitionRef missing Id reports diagnostic"), AnimMontageHasDiagnostic(MissingDefinitionIdResult, TEXT("/Body/Metadata/0"), TEXT("missing-definitionref-id")));

	TSharedPtr<FJsonObject> MissingDefinitionDocument = MakeStructuredMontageDocument(MakeUniqueMontageTarget(TEXT("AM_MetadataMissingDefinition")), AnimSequence->GetPathName());
	TArray<TSharedPtr<FJsonValue>> MissingDefinitionMetadata;
	MissingDefinitionMetadata.Add(MakeShared<FJsonValueObject>(MakeDefinitionRef(TEXT("Missing"))));
	MissingDefinitionDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Metadata"), MissingDefinitionMetadata);
	const FAssetDocumentResult MissingDefinitionResult = ValidateDocument(MissingDefinitionDocument);
	TestFalse(TEXT("Metadata DefinitionRef missing target is rejected"), MissingDefinitionResult.IsSuccess());
	TestTrue(TEXT("Metadata DefinitionRef missing target reports diagnostic"), AnimMontageHasDiagnostic(MissingDefinitionResult, TEXT("/Body/Metadata/0"), TEXT("definitionref-missing-id")));

	const FAssetDocumentResult WrongDefinitionBaseResult = ValidateDocument(MakeDefinitionRefDocument(
		MakeUniqueMontageTarget(TEXT("AM_MetadataWrongDefinitionBase")),
		AnimSequence->GetPathName(),
		TEXT("WrongBase"),
		MakeEmbeddedObjectRef(TestConcreteNotifyClassPath)));
	TestFalse(TEXT("Metadata DefinitionRef to non-UAnimMetaData is rejected"), WrongDefinitionBaseResult.IsSuccess());
	TestTrue(TEXT("Metadata DefinitionRef to non-UAnimMetaData reports diagnostic"), AnimMontageHasDiagnostic(WrongDefinitionBaseResult, TEXT("/Body/Metadata/0"), TEXT("embeddedobject-base-class-mismatch")));

	const int32 BeforeCompileFailureAssetMetadataCount = Montage->GetMetaData().Num();
	const int32 BeforeCompileFailureStartSectionMetadataCount = Montage->CompositeSections[0].GetMetaData().Num();
	const int32 BeforeCompileFailureEndSectionMetadataCount = Montage->CompositeSections[1].GetMetaData().Num();
	const int32 BeforeCompileFailureDirectObjectCount = CountDirectObjectsWithOuter(Montage);
	TSharedPtr<FJsonObject> CompileFailureDocument = MakeMontageDocument(Target);
	TArray<TSharedPtr<FJsonValue>> CompileFailureMetadata;
	CompileFailureMetadata.Add(MakeShared<FJsonValueObject>(MakeAnimMetaDataRef(AnimMetaDataClassPath)));
	TSharedPtr<FJsonObject> InvalidPropertyMetadata = MakeAnimMetaDataRef(AnimMetaDataClassPath);
	TSharedPtr<FJsonObject> InvalidMetadataProperties = MakeShared<FJsonObject>();
	InvalidMetadataProperties->SetBoolField(TEXT("DefinitelyMissingMetadataProperty"), true);
	InvalidPropertyMetadata->SetObjectField(TEXT("Properties"), InvalidMetadataProperties);
	CompileFailureMetadata.Add(MakeShared<FJsonValueObject>(InvalidPropertyMetadata));
	CompileFailureDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Metadata"), CompileFailureMetadata);
	const FAssetDocumentResult CompileFailureResult = ApplyDocument(CompileFailureDocument);
	TestFalse(TEXT("Metadata compile failure apply is rejected"), CompileFailureResult.IsSuccess());
	TestTrue(TEXT("Metadata compile failure reports preflight diagnostic"), AnimMontageHasDiagnostic(CompileFailureResult, TEXT("/Body/Metadata/1"), TEXT("embeddedobject-preflight-failed")));
	TestEqual(TEXT("Asset metadata unchanged after metadata compile failure"), Montage->GetMetaData().Num(), BeforeCompileFailureAssetMetadataCount);
	TestEqual(TEXT("Start section metadata unchanged after metadata compile failure"), Montage->CompositeSections[0].GetMetaData().Num(), BeforeCompileFailureStartSectionMetadataCount);
	TestEqual(TEXT("End section metadata unchanged after metadata compile failure"), Montage->CompositeSections[1].GetMetaData().Num(), BeforeCompileFailureEndSectionMetadataCount);
	TestEqual(TEXT("Metadata compile failure does not create direct child objects under production montage"), CountDirectObjectsWithOuter(Montage), BeforeCompileFailureDirectObjectCount);

	TSharedPtr<FJsonObject> BothSectionsDocument = MakeMontageDocument(Target);
	TSharedPtr<FJsonObject> BothSectionMetadata = MakeShared<FJsonObject>();
	BothSectionMetadata->SetArrayField(TEXT("Start"), MetadataValues);
	BothSectionMetadata->SetArrayField(TEXT("End"), MetadataValues);
	BothSectionsDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("SectionMetadata"), BothSectionMetadata);
	const FAssetDocumentResult BothSectionsResult = ApplyDocument(BothSectionsDocument);
	TestTrue(TEXT("Apply can seed metadata on both sections"), BothSectionsResult.IsSuccess());
	if (!BothSectionsResult.IsSuccess())
	{
		AddError(BothSectionsResult.Message);
		return false;
	}
	TestEqual(TEXT("Seeded Start section metadata"), Montage->CompositeSections[0].GetMetaData().Num(), 1);
	TestEqual(TEXT("Seeded End section metadata"), Montage->CompositeSections[1].GetMetaData().Num(), 1);

	TSharedPtr<FJsonObject> StartOnlySectionDocument = MakeMontageDocument(Target);
	TSharedPtr<FJsonObject> StartOnlySectionMetadata = MakeShared<FJsonObject>();
	StartOnlySectionMetadata->SetArrayField(TEXT("Start"), MetadataValues);
	StartOnlySectionDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("SectionMetadata"), StartOnlySectionMetadata);
	const FAssetDocumentResult StartOnlySectionResult = ApplyDocument(StartOnlySectionDocument);
	TestTrue(TEXT("Apply can replace SectionMetadata with only Start listed"), StartOnlySectionResult.IsSuccess());
	if (!StartOnlySectionResult.IsSuccess())
	{
		AddError(StartOnlySectionResult.Message);
		return false;
	}
	TestEqual(TEXT("Start-only replacement keeps Start section metadata"), Montage->CompositeSections[0].GetMetaData().Num(), 1);
	TestEqual(TEXT("Start-only replacement clears unlisted End section metadata"), Montage->CompositeSections[1].GetMetaData().Num(), 0);

	FAssetDocumentExtractRequest StartOnlyExtractRequest;
	StartOnlyExtractRequest.AssetPath = Target;
	StartOnlyExtractRequest.bDiffOnly = false;
	StartOnlyExtractRequest.bIncludeAllWritable = true;
	const FAssetDocumentResult StartOnlyExtractResult = Service.Extract(StartOnlyExtractRequest);
	TestTrue(TEXT("Extract succeeds after Start-only SectionMetadata replacement"), StartOnlyExtractResult.IsSuccess());
	if (StartOnlyExtractResult.IsSuccess() && StartOnlyExtractResult.Payload.IsValid())
	{
		const TSharedPtr<FJsonObject>* StartOnlyExtractedBody = nullptr;
		const TSharedPtr<FJsonObject>* StartOnlyExtractedSectionMetadata = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* StartOnlyExtractedEndMetadata = nullptr;
		TestTrue(TEXT("Extract includes Body after Start-only SectionMetadata replacement"), StartOnlyExtractResult.Payload->TryGetObjectField(TEXT("Body"), StartOnlyExtractedBody));
		TestTrue(TEXT("Extract includes SectionMetadata after Start-only SectionMetadata replacement"), StartOnlyExtractedBody && (*StartOnlyExtractedBody)->TryGetObjectField(TEXT("SectionMetadata"), StartOnlyExtractedSectionMetadata));
		TestTrue(TEXT("Extract includes empty End metadata after Start-only SectionMetadata replacement"), StartOnlyExtractedSectionMetadata && (*StartOnlyExtractedSectionMetadata)->TryGetArrayField(TEXT("End"), StartOnlyExtractedEndMetadata));
		TestTrue(TEXT("Extracted End metadata is empty after Start-only replacement"), StartOnlyExtractedEndMetadata && StartOnlyExtractedEndMetadata->IsEmpty());
	}

	TSharedPtr<FJsonObject> ClearAllSectionDocument = MakeMontageDocument(Target);
	ClearAllSectionDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("SectionMetadata"), MakeShared<FJsonObject>());
	const FAssetDocumentResult ClearAllSectionResult = ApplyDocument(ClearAllSectionDocument);
	TestTrue(TEXT("Empty SectionMetadata object clears all section metadata"), ClearAllSectionResult.IsSuccess());
	if (!ClearAllSectionResult.IsSuccess())
	{
		AddError(ClearAllSectionResult.Message);
		return false;
	}
	TestEqual(TEXT("Empty SectionMetadata clears Start section metadata"), Montage->CompositeSections[0].GetMetaData().Num(), 0);
	TestEqual(TEXT("Empty SectionMetadata clears End section metadata"), Montage->CompositeSections[1].GetMetaData().Num(), 0);

	TSharedPtr<FJsonObject> ReplacementDocument = MakeMontageDocument(Target);
	TArray<TSharedPtr<FJsonValue>> ReplacementMetadata;
	ReplacementMetadata.Add(MakeShared<FJsonValueObject>(MakeAnimMetaDataRef(AnimMetaDataClassPath)));
	ReplacementDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Metadata"), ReplacementMetadata);

	const FAssetDocumentResult ReplacementResult = ApplyDocument(ReplacementDocument);
	TestTrue(TEXT("Metadata replacement apply succeeds"), ReplacementResult.IsSuccess());
	if (!ReplacementResult.IsSuccess())
	{
		AddError(ReplacementResult.Message);
		return false;
	}

	UClass* AnimMetaDataClass = StaticLoadClass(UAnimMetaData::StaticClass(), nullptr, *AnimMetaDataClassPath);
	TestNotNull(TEXT("Concrete AnimMetaData fixture class is loadable"), AnimMetaDataClass);
	if (!AnimMetaDataClass)
	{
		return false;
	}

	UAnimMetaData* ManualMetadata = NewObject<UAnimMetaData>(Montage, AnimMetaDataClass, NAME_None, RF_Transactional);
	TestNotNull(TEXT("Manual metadata fixture can be instantiated"), ManualMetadata);
	if (!ManualMetadata)
	{
		return false;
	}
	Montage->AddMetaData(ManualMetadata);
	TestEqual(TEXT("Manual metadata fixture is present before replacement"), Montage->GetMetaData().Num(), 2);

	const FAssetDocumentResult ReplaceAgainResult = ApplyDocument(ReplacementDocument);
	TestTrue(TEXT("Metadata replacement reapply succeeds"), ReplaceAgainResult.IsSuccess());
	if (!ReplaceAgainResult.IsSuccess())
	{
		AddError(ReplaceAgainResult.Message);
		return false;
	}

	TestEqual(TEXT("Metadata v1 replacement does not preserve unmanaged entries in the same array"), Montage->GetMetaData().Num(), 1);
	if (Montage->GetMetaData().Num() == 1)
	{
		TestTrue(TEXT("Replacement metadata is a new managed entry"), Montage->GetMetaData()[0] != ManualMetadata);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageScalarRegionPrecedenceTest,
	"AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ScalarRegions.Precedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageScalarRegionPrecedenceTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_ScalarPrecedence"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SetScalarRegions(Document, AnimSequence->GetPathName());

	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	Body->SetField(TEXT("Skeleton"), MakeShared<FJsonValueNull>());
	Body->SetField(TEXT("PreviewMesh"), MakeShared<FJsonValueNull>());

	const FAssetDocumentResult Result = ApplyDocument(Document);
	TestTrue(TEXT("Apply succeeds with legacy/nested scalar conflicts"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Precedence AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	TestNotNull(TEXT("Nested References.Skeleton overrides legacy Skeleton"), Montage->GetSkeleton());
	TestNotNull(TEXT("Nested Preview.PreviewMesh overrides legacy PreviewMesh"), Montage->GetPreviewMesh());

	Montage->SyncGroup = FName(TEXT("ManualSync"));
	TSharedPtr<FJsonObject> ClearSyncDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SetScalarRegions(ClearSyncDocument, AnimSequence->GetPathName());
	ClearSyncDocument->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Sync"))->SetStringField(TEXT("SyncGroup"), TEXT(""));
	const FAssetDocumentResult ClearSyncResult = ApplyDocument(ClearSyncDocument);
	TestTrue(TEXT("Apply succeeds with empty SyncGroup clear"), ClearSyncResult.IsSuccess());
	if (!ClearSyncResult.IsSuccess())
	{
		AddError(ClearSyncResult.Message);
		return false;
	}
	TestEqual(TEXT("Empty SyncGroup clears to None"), Montage->SyncGroup, NAME_None);

	TSharedPtr<FJsonObject> SparseSyncDocument = MakeSyncOnlyMontageDocument(Target, TEXT("SparseSync"), 0);
	FAnimMontageAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext PreflightContext;
	PreflightContext.Asset = Montage;
	PreflightContext.AssetClass = UAnimMontage::StaticClass();
	PreflightContext.TargetAssetPath = Target;
	const TSharedPtr<FJsonObject>* SparseDefinitions = nullptr;
	SparseSyncDocument->TryGetObjectField(TEXT("Definitions"), SparseDefinitions);
	PreflightContext.Definitions = SparseDefinitions;
	const FAssetDocumentCapabilityResult PreflightResult = Capability.Preflight(
		PreflightContext,
		MakeShared<FJsonValueObject>(SparseSyncDocument->GetObjectField(TEXT("Body"))));
	TestTrue(TEXT("Sparse Sync-only preflight succeeds against existing SlotAnimTracks"), PreflightResult.bSuccess);
	if (!PreflightResult.bSuccess)
	{
		AddError(PreflightResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> InvalidSparseSyncDocument = MakeSyncOnlyMontageDocument(Target, TEXT("InvalidSparseSync"), 99);
	const FAssetDocumentCapabilityResult InvalidPreflightResult = Capability.Preflight(
		PreflightContext,
		MakeShared<FJsonValueObject>(InvalidSparseSyncDocument->GetObjectField(TEXT("Body"))));
	TestFalse(TEXT("Sparse Sync-only preflight rejects out-of-range SyncSlotIndex"), InvalidPreflightResult.bSuccess);
	bool bSawInvalidSyncSlotIndex = false;
	for (const FAssetDocumentDiagnostic& Diagnostic : InvalidPreflightResult.Diagnostics)
	{
		bSawInvalidSyncSlotIndex |= Diagnostic.Path == TEXT("/Body/Sync/SyncSlotIndex") && Diagnostic.Code == TEXT("InvalidSyncSlotIndex");
	}
	TestTrue(TEXT("Sparse Sync-only preflight reports InvalidSyncSlotIndex"), bSawInvalidSyncSlotIndex);

	Montage->RateScale = 1.0f;
	InvalidSparseSyncDocument->GetObjectField(TEXT("Properties"))->SetNumberField(TEXT("RateScale"), 2.0);
	const FAssetDocumentResult InvalidSparseApplyResult = ApplyDocument(InvalidSparseSyncDocument);
	TestFalse(TEXT("Invalid sparse Sync apply fails before mutating reflected properties"), InvalidSparseApplyResult.IsSuccess());
	TestEqual(TEXT("Invalid sparse Sync apply leaves RateScale unchanged"), Montage->RateScale, 1.0f);

	const FAssetDocumentResult SparseSyncResult = ApplyDocument(SparseSyncDocument);
	TestTrue(TEXT("Sparse Sync-only apply succeeds against existing SlotAnimTracks"), SparseSyncResult.IsSuccess());
	if (!SparseSyncResult.IsSuccess())
	{
		AddError(SparseSyncResult.Message);
		return false;
	}
	TestEqual(TEXT("Sparse Sync-only apply sets SyncGroup"), Montage->SyncGroup, FName(TEXT("SparseSync")));
	TestEqual(TEXT("Sparse Sync-only apply keeps valid SyncSlotIndex"), Montage->SyncSlotIndex, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageScalarRegionValidationTest,
	"AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ScalarRegions.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageScalarRegionValidationTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString AnimReferencePath = AnimSequence->GetPathName();
	bool bAllCasesPassed = true;
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("SyncSlotIndex negative"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		SetScalarRegions(Document, GetFirstSegment(Document)->GetObjectField(TEXT("AnimReference"))->GetStringField(TEXT("Path")));
		Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Sync"))->SetNumberField(TEXT("SyncSlotIndex"), -1);
	}, TEXT("/Body/Sync/SyncSlotIndex"), TEXT("InvalidSyncSlotIndex"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("SyncSlotIndex fractional"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		SetScalarRegions(Document, GetFirstSegment(Document)->GetObjectField(TEXT("AnimReference"))->GetStringField(TEXT("Path")));
		Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Sync"))->SetNumberField(TEXT("SyncSlotIndex"), 1.5);
	}, TEXT("/Body/Sync/SyncSlotIndex"), TEXT("InvalidSyncSlotIndex"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("SyncSlotIndex out of range"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		SetScalarRegions(Document, GetFirstSegment(Document)->GetObjectField(TEXT("AnimReference"))->GetStringField(TEXT("Path")));
		Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Sync"))->SetNumberField(TEXT("SyncSlotIndex"), 1);
	}, TEXT("/Body/Sync/SyncSlotIndex"), TEXT("InvalidSyncSlotIndex"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Invalid root motion lock"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		SetScalarRegions(Document, GetFirstSegment(Document)->GetObjectField(TEXT("AnimReference"))->GetStringField(TEXT("Path")));
		Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("RootMotion"))->SetStringField(TEXT("RootMotionRootLock"), TEXT("Bogus"));
	}, TEXT("/Body/RootMotion/RootMotionRootLock"), TEXT("InvalidRootMotionRootLock"));

	TSharedPtr<FJsonObject> WrongTypeDocument = MakeStructuredMontageDocument(MakeUniqueMontageTarget(TEXT("AM_ScalarWrongType")), AnimReferencePath);
	SetScalarRegions(WrongTypeDocument, TestSkeletonPath);
	const FAssetDocumentResult WrongTypeResult = ApplyDocument(WrongTypeDocument);
	bAllCasesPassed &= TestFalse(TEXT("Apply rejects wrong PreviewBasePose asset type"), WrongTypeResult.IsSuccess());
	bAllCasesPassed &= TestTrue(
		TEXT("Apply reports PreviewBasePose asset type diagnostic"),
		AnimMontageHasDiagnostic(WrongTypeResult, TEXT("/Body/Preview/PreviewBasePose"), TEXT("assetref-base-class-mismatch"))
			|| AnimMontageHasDiagnostic(WrongTypeResult, TEXT("/Body/Preview/PreviewBasePose"), TEXT("InvalidObjectReference")));

	return bAllCasesPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageScalarRegionCollectMarkersTest,
	"AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ScalarRegions.CollectMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageScalarRegionCollectMarkersTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequenceBase = CreateAnimSequenceFixture();
	UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimSequenceBase);
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	FAnimSyncMarker SourceMarker;
	SourceMarker.MarkerName = FName(TEXT("AssetDocMarker"));
	SourceMarker.Time = 0.10f;
	AnimSequence->AuthoredSyncMarkers.Add(SourceMarker);
	AnimSequence->RefreshSyncMarkerDataFromAuthored();

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_ScalarMarkers"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SetScalarRegions(Document, AnimSequence->GetPathName());

	const FAssetDocumentResult Result = ApplyDocument(Document);
	TestTrue(TEXT("Apply succeeds for marker collection scalar regions"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Marker collection AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	TestEqual(TEXT("Apply refreshes montage marker cache"), Montage->MarkerData.AuthoredSyncMarkers.Num(), 1);
	if (Montage->MarkerData.AuthoredSyncMarkers.Num() == 1)
	{
		TestEqual(TEXT("Collected marker name"), Montage->MarkerData.AuthoredSyncMarkers[0].MarkerName, FName(TEXT("AssetDocMarker")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyFileSyncStateTest,
	"AssetFactory.AssetDocument.AnimMontage.ApplyFile.SyncState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyFileSyncStateTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}
	UAnimSequence* PreviewBasePose = Cast<UAnimSequence>(AnimSequence);
	TestNotNull(TEXT("AnimSequence fixture can be used as PreviewBasePose"), PreviewBasePose);
	if (!PreviewBasePose)
	{
		return false;
	}

	FAssetDocumentService Service;
	FAnimMontageAssetDocumentProfile Profile;
	TArray<FAssetDocumentRegionPolicy> CompleteRegionPolicies;
	CollectRegionPolicies(this, Profile, GetCompleteAnimMontageSyncRegionIds(), CompleteRegionPolicies);
	TArray<FAssetDocumentRegionPolicy> NewRegionPolicies;
	CollectRegionPolicies(this, Profile, GetNewCompleteAnimMontageRegionIds(), NewRegionPolicies);

	TArray<FString> SidecarPathsToCleanup;
	ON_SCOPE_EXIT
	{
		for (const FString& Path : SidecarPathsToCleanup)
		{
			IFileManager::Get().Delete(*Path);
			IFileManager::Get().DeleteDirectory(*FPaths::GetPath(Path), false, false);
		}
	};

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_ApplyFileSync"));
	const FString SidecarPath = FAssetDocumentSidecar::ResolveSidecarPathFromObjectPath(Target);
	SidecarPathsToCleanup.Add(SidecarPath);
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SetScalarRegions(Document, PreviewBasePose->GetPathName());
	SetCurvesAndTimeStretch(Document);
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	TArray<TSharedPtr<FJsonValue>> StableCurves;
	StableCurves.Add(MakeShared<FJsonValueObject>(MakeSortedMontageTimeStretchCurve()));
	Body->SetArrayField(TEXT("Curves"), StableCurves);
	TSharedPtr<FJsonObject> SyncBlend = Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Blend"));
	SyncBlend->SetNumberField(TEXT("BlendInTime"), 0.125);
	SyncBlend->SetNumberField(TEXT("BlendOutTime"), 0.25);
	SyncBlend->SetStringField(TEXT("BlendModeIn"), TEXT("Standard"));
	SyncBlend->SetStringField(TEXT("BlendModeOut"), TEXT("Standard"));
	SyncBlend->SetNumberField(TEXT("BlendOutTriggerTime"), -1.0);
	SyncBlend->SetBoolField(TEXT("bEnableAutoBlendOut"), true);
	Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Sync"))->SetStringField(TEXT("SyncGroup"), TEXT("ApplyFileSync"));
	TSharedPtr<FJsonObject> StableNotifyObject = MakeEmbeddedObjectRef(TestConcreteNotifyClassPath, MakeShared<FJsonObject>());
	TArray<TSharedPtr<FJsonValue>> StableNotifies;
	StableNotifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(0.10000000149011612, StableNotifyObject)));
	Body->SetArrayField(TEXT("Notifies"), StableNotifies);

	TArray<TSharedPtr<FJsonValue>> StableNotifyStates;
	StableNotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
		0.11999999731779099,
		0.050000004470348358,
		MakeEmbeddedObjectRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"), MakeShared<FJsonObject>()))));
	Body->SetArrayField(TEXT("NotifyStates"), StableNotifyStates);
	Body->GetObjectField(TEXT("TimeStretch"))->SetNumberField(TEXT("CurveValueMinPrecision"), 0.019999999552965164);

	const FAnimMetaDataClassFixture AnimMetaDataClassFixture = CreateAnimMetaDataClassFixture();
	ON_SCOPE_EXIT
	{
		AnimMetaDataClassFixture.Cleanup();
	};
	const FString AnimMetaDataClassPath = AnimMetaDataClassFixture.ClassPath;
	TestTrue(TEXT("Concrete AnimMetaData fixture class path is available"), AnimMetaDataClassFixture.IsValid());
	if (!AnimMetaDataClassFixture.IsValid())
	{
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> MetadataValues;
	MetadataValues.Add(MakeShared<FJsonValueObject>(MakeEmbeddedObjectRef(AnimMetaDataClassPath)));
	Body->SetArrayField(TEXT("Metadata"), MetadataValues);
	TSharedPtr<FJsonObject> SectionMetadata = MakeShared<FJsonObject>();
	SectionMetadata->SetArrayField(TEXT("Start"), MetadataValues);
	SectionMetadata->SetArrayField(TEXT("End"), TArray<TSharedPtr<FJsonValue>>());
	Body->SetObjectField(TEXT("SectionMetadata"), SectionMetadata);
	Document->RemoveField(TEXT("_meta"));
	if (!WriteSidecarJson(this, SidecarPath, Document))
	{
		return false;
	}

	FAssetDocumentApplyFileRequest Request;
	Request.FilePath = SidecarPath;
	Request.bSaveAsset = false;

	const FAssetDocumentResult Result = Service.ApplyFile(Request);
	TestTrue(TEXT("ApplyFile succeeds for structured AnimMontage sidecar"), Result.IsSuccess());
	if (Result.IsSuccess() && !Result.bWroteSidecar && Result.Payload.IsValid())
	{
		FString SkipReason;
		if (Result.Payload->TryGetStringField(TEXT("sidecar_sync_update_skip_reason"), SkipReason))
		{
			AddError(FString::Printf(TEXT("ApplyFile sync rewrite skipped: %s"), *SkipReason));
			TSharedPtr<FJsonObject> SourceAfterApply;
			FString SourceLoadError;
			if (FAssetDocumentSidecar::LoadJsonFile(SidecarPath, SourceAfterApply, SourceLoadError) && SourceAfterApply.IsValid())
			{
				AddError(FString::Printf(TEXT("ApplyFile source body: %s"), *JsonObjectToCompactString(SourceAfterApply->GetObjectField(TEXT("Body")))));
			}
			FAssetDocumentExtractRequest DiagnosticExtractRequest;
			DiagnosticExtractRequest.AssetPath = Target;
			DiagnosticExtractRequest.bDiffOnly = false;
			DiagnosticExtractRequest.bIncludeAllWritable = true;
			const FAssetDocumentResult DiagnosticExtractResult = Service.Extract(DiagnosticExtractRequest);
			if (DiagnosticExtractResult.Payload.IsValid())
			{
				AddError(FString::Printf(TEXT("ApplyFile evidence body: %s"), *JsonObjectToCompactString(DiagnosticExtractResult.Payload->GetObjectField(TEXT("Body")))));
			}
		}
	}
	TestTrue(TEXT("ApplyFile reports sidecar sync rewrite"), Result.bWroteSidecar);
	if (!Result.IsSuccess())
	{
		AddError(Result.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("ApplyFile-created AnimMontage is loadable"), Montage);
	if (Montage)
	{
		TestNotNull(TEXT("ApplyFile sets skeleton reference region"), Montage->GetSkeleton());
		TestNotNull(TEXT("ApplyFile sets preview mesh region"), Montage->GetPreviewMesh());
		TestTrue(TEXT("ApplyFile sets preview base pose"), Montage->PreviewBasePose.Get() == PreviewBasePose);
		TestEqual(TEXT("ApplyFile sets sync group"), Montage->SyncGroup, FName(TEXT("ApplyFileSync")));
		TestEqual(TEXT("ApplyFile sets sync slot index"), Montage->SyncSlotIndex, 0);
		TestTrue(TEXT("ApplyFile enables root motion translation"), Montage->bEnableRootMotionTranslation);
		TestTrue(TEXT("ApplyFile enables root motion rotation"), Montage->bEnableRootMotionRotation);
		TestEqual(TEXT("ApplyFile sets root motion root lock"), Montage->RootMotionRootLock, ERootMotionRootLock::Zero);
		TestEqual(TEXT("ApplyFile sets asset metadata"), Montage->GetMetaData().Num(), 1);
		TestTrue(TEXT("ApplyFile keeps section metadata target available"), Montage->CompositeSections.Num() >= 1);
		if (Montage->CompositeSections.Num() >= 1)
		{
			TestEqual(TEXT("ApplyFile sets Start section metadata"), Montage->CompositeSections[0].GetMetaData().Num(), 1);
		}
		TestEqual(TEXT("ApplyFile sets time stretch curve name"), Montage->TimeStretchCurveName, FName(TEXT("MontageTimeStretchCurve")));
		if (const IAnimationDataModel* DataModel = Montage->GetDataModel())
		{
			const FFloatCurve* MontageCurve = DataModel->FindFloatCurve(FAnimationCurveIdentifier(FName(TEXT("MontageTimeStretchCurve")), ERawCurveTrackTypes::RCT_Float));
			TestNotNull(TEXT("ApplyFile creates montage-owned float curve"), MontageCurve);
		}
	}

	TSharedPtr<FJsonObject> ReloadedDocument;
	if (LoadSidecarJson(this, SidecarPath, ReloadedDocument))
	{
		const TSharedPtr<FJsonObject>* Meta = nullptr;
		TestTrue(TEXT("ApplyFile sidecar includes _meta"), ReloadedDocument->TryGetObjectField(TEXT("_meta"), Meta));
		const TSharedPtr<FJsonObject>* Sync = nullptr;
		TestTrue(TEXT("ApplyFile sidecar includes _meta.sync"), Meta && Meta->IsValid() && (*Meta)->TryGetObjectField(TEXT("sync"), Sync));
		const TSharedPtr<FJsonObject>* SyncRegions = FindSyncRegions(ReloadedDocument);
		TestTrue(TEXT("ApplyFile sidecar includes _meta.sync.regions"), SyncRegions != nullptr);
		if (Sync && Sync->IsValid())
		{
			TestEqual(TEXT("ApplyFile sync asset object path"), (*Sync)->GetStringField(TEXT("assetObjectPath")), MakeObjectPathFromTarget(Target));
		}
		if (SyncRegions)
		{
			for (const FAssetDocumentRegionPolicy& Policy : CompleteRegionPolicies)
			{
				ExpectApplyFileSyncRegion(this, ReloadedDocument.ToSharedRef(), *SyncRegions, Policy);
			}
		}

		FAssetDocumentExtractRequest ReExtractRequest;
		ReExtractRequest.AssetPath = Target;
		ReExtractRequest.bDiffOnly = false;
		ReExtractRequest.bIncludeAllWritable = true;
		const FAssetDocumentResult ReExtractResult = Service.Extract(ReExtractRequest);
		TestTrue(TEXT("Re-extract succeeds after complete ApplyFile"), ReExtractResult.IsSuccess());
		TestTrue(TEXT("Re-extract returns payload after complete ApplyFile"), ReExtractResult.Payload.IsValid());

		if (ReExtractResult.Payload.IsValid())
		{
			const FAssetDocumentResult DiffResult = DiffDocument(ReExtractResult.Payload);
			TestTrue(TEXT("Diff succeeds after complete ApplyFile re-extract"), DiffResult.IsSuccess());
			TestTrue(TEXT("Diff returns payload after complete ApplyFile re-extract"), DiffResult.Payload.IsValid());
			if (DiffResult.Payload.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
				TestTrue(TEXT("Complete ApplyFile diff payload includes changed array"), DiffResult.Payload->TryGetArrayField(TEXT("changed"), Changed));
				if (Changed)
				{
					for (const FAssetDocumentRegionPolicy& Policy : NewRegionPolicies)
					{
						const FString RegionPath = RegionIdToDiffPath(Policy.RegionId);
						TestFalse(
							FString::Printf(TEXT("Re-extracted complete region %s has no changed diff entry"), *RegionPath),
							JsonArrayContainsPathStatus(*Changed, RegionPath, TEXT("changed")));
					}
				}
			}
		}
	}

	const FString NoRewriteTarget = MakeUniqueMontageTarget(TEXT("AM_ApplyFileNoRewrite"));
	const FString NoRewriteSidecarPath = FAssetDocumentSidecar::ResolveSidecarPathFromObjectPath(NoRewriteTarget);
	SidecarPathsToCleanup.Add(NoRewriteSidecarPath);
	TSharedPtr<FJsonObject> NoRewriteDocument = MakeStructuredMontageDocument(NoRewriteTarget, AnimSequence->GetPathName());
	NoRewriteDocument->RemoveField(TEXT("_meta"));
	if (!WriteSidecarJson(this, NoRewriteSidecarPath, NoRewriteDocument))
	{
		return false;
	}

	FAssetDocumentApplyFileRequest NoRewriteRequest;
	NoRewriteRequest.FilePath = NoRewriteSidecarPath;
	NoRewriteRequest.bSaveAsset = false;
	NoRewriteRequest.bAllowSidecarRewrite = false;
	NoRewriteRequest.bTriggeredByWatcher = true;

	const FAssetDocumentResult NoRewriteResult = Service.ApplyFile(NoRewriteRequest);
	TestTrue(TEXT("ApplyFile succeeds when sidecar rewrite is disabled"), NoRewriteResult.IsSuccess());
	TestFalse(TEXT("ApplyFile does not report sidecar rewrite when disabled"), NoRewriteResult.bWroteSidecar);

	TSharedPtr<FJsonObject> ReloadedNoRewriteDocument;
	if (LoadSidecarJson(this, NoRewriteSidecarPath, ReloadedNoRewriteDocument))
	{
		TestFalse(TEXT("ApplyFile does not write _meta.sync when rewrite is disabled"), HasSyncRegions(ReloadedNoRewriteDocument));
	}

	const FString HashMismatchTarget = MakeUniqueMontageTarget(TEXT("AM_ApplyFileHashMismatch"));
	const FString HashMismatchSidecarPath = FAssetDocumentSidecar::ResolveSidecarPathFromObjectPath(HashMismatchTarget);
	SidecarPathsToCleanup.Add(HashMismatchSidecarPath);
	TSharedPtr<FJsonObject> HashMismatchDocument = MakeStructuredMontageDocument(HashMismatchTarget, AnimSequence->GetPathName());
	HashMismatchDocument->RemoveField(TEXT("_meta"));
	if (!WriteSidecarJson(this, HashMismatchSidecarPath, HashMismatchDocument))
	{
		return false;
	}

	FAssetDocumentApplyFileRequest HashMismatchRequest;
	HashMismatchRequest.FilePath = HashMismatchSidecarPath;
	HashMismatchRequest.bSaveAsset = false;

	const FAssetDocumentResult HashMismatchResult = Service.ApplyFile(HashMismatchRequest);
	TestTrue(TEXT("ApplyFile still succeeds when sync hash update is skipped for mismatched evidence"), HashMismatchResult.IsSuccess());
	TestFalse(TEXT("ApplyFile does not report sidecar rewrite when sync hashes mismatch"), HashMismatchResult.bWroteSidecar);

	TSharedPtr<FJsonObject> ReloadedHashMismatchDocument;
	if (LoadSidecarJson(this, HashMismatchSidecarPath, ReloadedHashMismatchDocument))
	{
		TestFalse(TEXT("ApplyFile does not write _meta.sync when sync hashes mismatch"), HasSyncRegions(ReloadedHashMismatchDocument));
	}

	const FString MalformedSyncTarget = MakeUniqueMontageTarget(TEXT("AM_ApplyFileMalformedSync"));
	const FString MalformedSyncSidecarPath = FAssetDocumentSidecar::ResolveSidecarPathFromObjectPath(MalformedSyncTarget);
	SidecarPathsToCleanup.Add(MalformedSyncSidecarPath);
	TSharedPtr<FJsonObject> MalformedSyncDocument = MakeStructuredMontageDocument(MalformedSyncTarget, AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> MalformedMeta = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> MalformedSync = MakeShared<FJsonObject>();
	MalformedSync->SetStringField(TEXT("schemaVersion"), TEXT("bad"));
	MalformedMeta->SetObjectField(TEXT("sync"), MalformedSync);
	MalformedSyncDocument->SetObjectField(TEXT("_meta"), MalformedMeta);
	TSharedPtr<FJsonObject> MalformedSyncBlend = MalformedSyncDocument->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Blend"));
	MalformedSyncBlend->SetNumberField(TEXT("BlendInTime"), 0.125);
	MalformedSyncBlend->SetNumberField(TEXT("BlendOutTime"), 0.25);
	if (!WriteSidecarJson(this, MalformedSyncSidecarPath, MalformedSyncDocument))
	{
		return false;
	}

	FAssetDocumentApplyFileRequest MalformedSyncRequest;
	MalformedSyncRequest.FilePath = MalformedSyncSidecarPath;
	MalformedSyncRequest.bSaveAsset = false;

	const FAssetDocumentResult MalformedSyncResult = Service.ApplyFile(MalformedSyncRequest);
	TestTrue(TEXT("ApplyFile still succeeds when malformed existing sync prevents sync rewrite"), MalformedSyncResult.IsSuccess());
	TestFalse(TEXT("ApplyFile does not report sidecar rewrite when existing sync is malformed"), MalformedSyncResult.bWroteSidecar);

	TSharedPtr<FJsonObject> ReloadedMalformedSyncDocument;
	if (LoadSidecarJson(this, MalformedSyncSidecarPath, ReloadedMalformedSyncDocument))
	{
		const TSharedPtr<FJsonObject>* ReloadedMeta = nullptr;
		const TSharedPtr<FJsonObject>* ReloadedSync = nullptr;
		TestTrue(TEXT("Malformed sync sidecar keeps _meta"), ReloadedMalformedSyncDocument->TryGetObjectField(TEXT("_meta"), ReloadedMeta));
		TestTrue(TEXT("Malformed sync sidecar keeps _meta.sync"), ReloadedMeta && ReloadedMeta->IsValid() && (*ReloadedMeta)->TryGetObjectField(TEXT("sync"), ReloadedSync));
		if (ReloadedSync && ReloadedSync->IsValid())
		{
			FString SchemaVersion;
			TestTrue(TEXT("Malformed sync schemaVersion remains string"), (*ReloadedSync)->TryGetStringField(TEXT("schemaVersion"), SchemaVersion));
			TestEqual(TEXT("Malformed sync schemaVersion is not normalized"), SchemaVersion, FString(TEXT("bad")));
			TestFalse(TEXT("Malformed sync is not overwritten with regions"), (*ReloadedSync)->HasField(TEXT("regions")));
		}
	}

	const FString MismatchTarget = MakeUniqueMontageTarget(TEXT("AM_ApplyFileMismatch"));
	const FString MismatchSidecarPath = FAssetDocumentSidecar::ResolveSidecarPathFromObjectPath(MismatchTarget);
	SidecarPathsToCleanup.Add(MismatchSidecarPath);
	TSharedPtr<FJsonObject> MismatchDocument = MakeStructuredMontageDocument(MakeUniqueMontageTarget(TEXT("AM_WrongTarget")), AnimSequence->GetPathName());
	MismatchDocument->RemoveField(TEXT("_meta"));
	if (!WriteSidecarJson(this, MismatchSidecarPath, MismatchDocument))
	{
		return false;
	}

	FAssetDocumentApplyFileRequest MismatchRequest;
	MismatchRequest.FilePath = MismatchSidecarPath;
	MismatchRequest.bSaveAsset = false;

	const FAssetDocumentResult MismatchResult = Service.ApplyFile(MismatchRequest);
	TestFalse(TEXT("ApplyFile fails for target mismatch"), MismatchResult.IsSuccess());
	TestFalse(TEXT("Failed ApplyFile does not report sidecar rewrite"), MismatchResult.bWroteSidecar);

	TSharedPtr<FJsonObject> ReloadedMismatchDocument;
	if (LoadSidecarJson(this, MismatchSidecarPath, ReloadedMismatchDocument))
	{
		TestFalse(TEXT("Failed ApplyFile leaves _meta.sync absent"), HasSyncRegions(ReloadedMismatchDocument));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarSyncAcceptAssetRegeneratesManagedRegionTest,
	"AssetFactory.AssetDocument.SidecarSync.AcceptAssetRegeneratesManagedRegion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarSyncAcceptAssetRegeneratesManagedRegionTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_AcceptAssetBlend"));
	const FAssetDocumentResult CreateResult = ApplyDocument(MakeStructuredMontageDocument(Target, AnimSequence->GetPathName()));
	TestTrue(TEXT("Initial AnimMontage apply succeeds"), CreateResult.IsSuccess());
	if (!CreateResult.IsSuccess())
	{
		AddError(CreateResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Created AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds before accept asset"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns baseline sidecar"), ExtractResult.Payload.IsValid());
	if (!ExtractResult.IsSuccess() || !ExtractResult.Payload.IsValid())
	{
		AddError(ExtractResult.Message);
		return false;
	}

	FAnimMontageAssetDocumentProfile Profile;
	FAssetDocumentRegionPolicy BlendPolicy;
	TestTrue(TEXT("AnimMontage profile has Body.Blend policy"), Profile.GetRegionPolicy(TEXT("Body.Blend"), BlendPolicy));
	FAssetDocumentRegionPolicy CompositeSectionsPolicy;
	TestTrue(TEXT("AnimMontage profile has Body.CompositeSections policy"), Profile.GetRegionPolicy(TEXT("Body.CompositeSections"), CompositeSectionsPolicy));

	TSharedPtr<FJsonObject> SidecarDocument = ExtractResult.Payload;
	SidecarDocument->SetStringField(TEXT("ManualTopLevelField"), TEXT("keep-me"));
	SidecarDocument->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	SidecarDocument->GetObjectField(TEXT("Definitions"))->SetObjectField(TEXT("ManualDefinition"), AnimMontageMakeAssetRef(TestSkeletonPath));
	SidecarDocument->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("ManualProperty"), TEXT("preserve"));

	TArray<TSharedPtr<FJsonValue>> AuthoredCompositeSections;
	TSharedPtr<FJsonObject> AuthoredSection = MakeShared<FJsonObject>();
	AuthoredSection->SetStringField(TEXT("SectionName"), TEXT("AuthoredSidecarOnly"));
	AuthoredSection->SetNumberField(TEXT("LinkableTime"), 0.11);
	AuthoredCompositeSections.Add(MakeShared<FJsonValueObject>(AuthoredSection));
	SidecarDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("CompositeSections"), AuthoredCompositeSections);
	const FString CompositeSectionsHashBefore = FAssetDocumentSidecarDelta::HashSidecarRegion(SidecarDocument.ToSharedRef(), CompositeSectionsPolicy);

	Montage->BlendIn.SetBlendTime(0.33f);
	Montage->BlendOut.SetBlendTime(0.44f);

	const FAssetDocumentResult RegenerateResult = RegenerateSidecarRegionsFromAsset(
		Montage,
		SidecarDocument.ToSharedRef(),
		{TEXT("Body.Blend")},
		FString());

	TestTrue(TEXT("Accept asset regeneration succeeds"), RegenerateResult.IsSuccess());
	if (!RegenerateResult.IsSuccess())
	{
		AddError(RegenerateResult.Message);
		return false;
	}

	const TSharedPtr<FJsonObject>* RegeneratedBlend = nullptr;
	TestTrue(TEXT("Regenerated sidecar includes Body.Blend"), SidecarDocument->GetObjectField(TEXT("Body"))->TryGetObjectField(TEXT("Blend"), RegeneratedBlend));
	if (RegeneratedBlend && RegeneratedBlend->IsValid())
	{
		TestTrue(TEXT("BlendInTime comes from changed asset"), FMath::IsNearlyEqual((*RegeneratedBlend)->GetNumberField(TEXT("BlendInTime")), 0.33, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("BlendOutTime comes from changed asset"), FMath::IsNearlyEqual((*RegeneratedBlend)->GetNumberField(TEXT("BlendOutTime")), 0.44, KINDA_SMALL_NUMBER));
	}

	TestEqual(TEXT("Manual top-level field is preserved"), SidecarDocument->GetStringField(TEXT("ManualTopLevelField")), FString(TEXT("keep-me")));
	TestTrue(TEXT("Manual definition is preserved"), SidecarDocument->GetObjectField(TEXT("Definitions"))->HasTypedField<EJson::Object>(TEXT("ManualDefinition")));
	TestEqual(TEXT("Manual Properties field is preserved"), SidecarDocument->GetObjectField(TEXT("Properties"))->GetStringField(TEXT("ManualProperty")), FString(TEXT("preserve")));
	TestEqual(TEXT("Non-target Body.CompositeSections region is preserved"), FAssetDocumentSidecarDelta::HashSidecarRegion(SidecarDocument.ToSharedRef(), CompositeSectionsPolicy), CompositeSectionsHashBefore);

	FAssetDocumentRegionSyncState BlendSyncState;
	if (ExpectSyncRegionMatchesSidecar(this, SidecarDocument.ToSharedRef(), BlendPolicy, BlendSyncState))
	{
		const FString BlendHash = FAssetDocumentSidecarDelta::HashSidecarRegion(SidecarDocument.ToSharedRef(), BlendPolicy);
		const FAssetDocumentRegionSyncDecision Decision = FAssetDocumentSidecarSyncEngine::DecideRegion(
			TEXT("Body.Blend"),
			BlendHash,
			BlendHash,
			&BlendSyncState);
		TestEqual(TEXT("Updated sync state makes Body.Blend NoChange"), Decision.Direction, EAssetDocumentSyncDirection::NoChange);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarSyncAcceptAssetRegeneratesSyncRegionTest,
	"AssetFactory.AssetDocument.SidecarSync.AcceptAssetRegeneratesSyncRegion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarSyncAcceptAssetRegeneratesSyncRegionTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	UAnimSequence* PreviewBasePose = Cast<UAnimSequence>(AnimSequence);
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	TestNotNull(TEXT("AnimSequence fixture can be used as PreviewBasePose"), PreviewBasePose);
	if (!AnimSequence || !PreviewBasePose)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_AcceptAssetSync"));
	TSharedPtr<FJsonObject> InitialDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SetScalarRegions(InitialDocument, PreviewBasePose->GetPathName());
	SetSingleManagedNotify(InitialDocument->GetObjectField(TEXT("Body")));

	const FAssetDocumentResult CreateResult = ApplyDocument(InitialDocument);
	TestTrue(TEXT("Initial AnimMontage apply with Sync succeeds"), CreateResult.IsSuccess());
	if (!CreateResult.IsSuccess())
	{
		AddError(CreateResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Created Sync AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds before Sync accept asset"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns Sync baseline sidecar"), ExtractResult.Payload.IsValid());
	if (!ExtractResult.IsSuccess() || !ExtractResult.Payload.IsValid())
	{
		AddError(ExtractResult.Message);
		return false;
	}

	FAnimMontageAssetDocumentProfile Profile;
	FAssetDocumentRegionPolicy SyncPolicy;
	TestTrue(TEXT("AnimMontage profile has Body.Sync policy"), Profile.GetRegionPolicy(TEXT("Body.Sync"), SyncPolicy));
	FAssetDocumentRegionPolicy NotifiesPolicy;
	TestTrue(TEXT("AnimMontage profile has Body.Notifies policy"), Profile.GetRegionPolicy(TEXT("Body.Notifies"), NotifiesPolicy));
	FAssetDocumentRegionPolicy SlotAnimTracksPolicy;
	TestTrue(TEXT("AnimMontage profile has Body.SlotAnimTracks policy"), Profile.GetRegionPolicy(TEXT("Body.SlotAnimTracks"), SlotAnimTracksPolicy));

	TSharedPtr<FJsonObject> SidecarDocument = ExtractResult.Payload;
	const FString NotifiesHashBefore = FAssetDocumentSidecarDelta::HashSidecarRegion(SidecarDocument.ToSharedRef(), NotifiesPolicy);
	const FString SlotAnimTracksHashBefore = FAssetDocumentSidecarDelta::HashSidecarRegion(SidecarDocument.ToSharedRef(), SlotAnimTracksPolicy);

	Montage->SyncGroup = FName(TEXT("EditorChanged"));

	const FAssetDocumentResult RegenerateResult = RegenerateSidecarRegionsFromAsset(
		Montage,
		SidecarDocument.ToSharedRef(),
		{TEXT("Body.Sync")},
		FString());

	TestTrue(TEXT("Accept asset regeneration succeeds for Body.Sync"), RegenerateResult.IsSuccess());
	if (!RegenerateResult.IsSuccess())
	{
		AddError(RegenerateResult.Message);
		return false;
	}

	const TSharedPtr<FJsonObject>* RegeneratedSync = nullptr;
	TestTrue(TEXT("Regenerated sidecar includes Body.Sync"), SidecarDocument->GetObjectField(TEXT("Body"))->TryGetObjectField(TEXT("Sync"), RegeneratedSync));
	if (RegeneratedSync && RegeneratedSync->IsValid())
	{
		TestEqual(TEXT("Body.Sync.SyncGroup comes from changed asset"), (*RegeneratedSync)->GetStringField(TEXT("SyncGroup")), FString(TEXT("EditorChanged")));
	}
	TestEqual(TEXT("Non-target Body.Notifies region is preserved"), FAssetDocumentSidecarDelta::HashSidecarRegion(SidecarDocument.ToSharedRef(), NotifiesPolicy), NotifiesHashBefore);
	TestEqual(TEXT("Non-target Body.SlotAnimTracks region is preserved"), FAssetDocumentSidecarDelta::HashSidecarRegion(SidecarDocument.ToSharedRef(), SlotAnimTracksPolicy), SlotAnimTracksHashBefore);

	FAssetDocumentRegionSyncState SyncState;
	if (ExpectSyncRegionMatchesSidecar(this, SidecarDocument.ToSharedRef(), SyncPolicy, SyncState))
	{
		const FString SyncHash = FAssetDocumentSidecarDelta::HashSidecarRegion(SidecarDocument.ToSharedRef(), SyncPolicy);
		const FAssetDocumentRegionSyncDecision Decision = FAssetDocumentSidecarSyncEngine::DecideRegion(
			TEXT("Body.Sync"),
			SyncHash,
			SyncHash,
			&SyncState);
		TestEqual(TEXT("Updated sync state makes Body.Sync NoChange"), Decision.Direction, EAssetDocumentSyncDirection::NoChange);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarSyncAcceptAssetPreservesUnmanagedFieldsTest,
	"AssetFactory.AssetDocument.SidecarSync.AcceptAssetPreservesUnmanagedFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarSyncAcceptAssetPreservesUnmanagedFieldsTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_AcceptAssetUnknown"));
	const FAssetDocumentResult CreateResult = ApplyDocument(MakeStructuredMontageDocument(Target, AnimSequence->GetPathName()));
	TestTrue(TEXT("Initial AnimMontage apply succeeds"), CreateResult.IsSuccess());
	if (!CreateResult.IsSuccess())
	{
		AddError(CreateResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Created AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds before unknown accept asset"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns baseline sidecar"), ExtractResult.Payload.IsValid());
	if (!ExtractResult.IsSuccess() || !ExtractResult.Payload.IsValid())
	{
		AddError(ExtractResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> SidecarDocument = ExtractResult.Payload;
	SidecarDocument->SetStringField(TEXT("ManualTopLevelField"), TEXT("keep-me"));

	const FAssetDocumentResult RegenerateResult = RegenerateSidecarRegionsFromAsset(
		Montage,
		SidecarDocument.ToSharedRef(),
		{TEXT("Body.UnknownRegion")},
		FString());

	TestFalse(TEXT("Unknown accept asset region fails"), RegenerateResult.IsSuccess());
	TestTrue(TEXT("Unknown region failure mentions region id"), RegenerateResult.Message.Contains(TEXT("Body.UnknownRegion")));
	TestEqual(TEXT("Failed regeneration preserves manual top-level field"), SidecarDocument->GetStringField(TEXT("ManualTopLevelField")), FString(TEXT("keep-me")));

	SidecarDocument->SetStringField(TEXT("Body"), TEXT("not-an-object"));
	const FAssetDocumentResult NonObjectBodyResult = RegenerateSidecarRegionsFromAsset(
		Montage,
		SidecarDocument.ToSharedRef(),
		{TEXT("Body.Blend")},
		FString());
	TestFalse(TEXT("Non-object Body intermediate path fails"), NonObjectBodyResult.IsSuccess());
	TestTrue(TEXT("Non-object Body failure reports path error"), NonObjectBodyResult.Message.Contains(TEXT("Body")) && NonObjectBodyResult.Message.Contains(TEXT("not an object")));
	TestEqual(TEXT("Failed non-object regeneration preserves Body string"), SidecarDocument->GetStringField(TEXT("Body")), FString(TEXT("not-an-object")));
	TestEqual(TEXT("Failed non-object regeneration preserves manual top-level field"), SidecarDocument->GetStringField(TEXT("ManualTopLevelField")), FString(TEXT("keep-me")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyNotifiesTest,
	"AssetFactory.AssetDocument.AnimMontage.Apply.Notifies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyNotifiesTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_Notifies"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));

	TArray<TSharedPtr<FJsonValue>> Notifies;
	Notifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(
		0.10,
		MakeEmbeddedObjectRef(TestConcreteNotifyClassPath))));
	Body->SetArrayField(TEXT("Notifies"), Notifies);

	TArray<TSharedPtr<FJsonValue>> NotifyStates;
	NotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
		0.12,
		0.05,
		MakeEmbeddedObjectRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"), MakeShared<FJsonObject>()))));
	Body->SetArrayField(TEXT("NotifyStates"), NotifyStates);

	const FAssetDocumentResult Result = ApplyDocument(Document);
	TestTrue(TEXT("Apply succeeds for AnimMontage notifies"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Applied AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	const FAnimNotifyEvent* NotifyEvent = Montage->Notifies.FindByPredicate([](const FAnimNotifyEvent& Event)
	{
		return Event.Notify && Event.Notify->IsA<UAnimNotify>() && Event.NotifyName == TestManagedNotifyName;
	});
	TestNotNull(TEXT("Montage contains UAnimNotify event"), NotifyEvent);

	const FAnimNotifyEvent* NotifyStateEvent = Montage->Notifies.FindByPredicate([](const FAnimNotifyEvent& Event)
	{
		return Event.NotifyStateClass && Event.NotifyStateClass->IsA<UAssetFactoryNamedAnimNotifyState>() && Event.NotifyName == TestManagedNotifyStateName;
	});
	TestNotNull(TEXT("Montage contains named UAnimNotifyState event"), NotifyStateEvent);
	if (NotifyStateEvent)
	{
		TestTrue(TEXT("Notify state duration is positive"), NotifyStateEvent->GetDuration() > 0.0f);
	}

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds for AnimMontage notifies"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns payload"), ExtractResult.Payload.IsValid());
	if (ExtractResult.Payload.IsValid())
	{
		const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
		TestTrue(TEXT("Extract includes Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody));
		if (ExtractedBody && ExtractedBody->IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* ExtractedNotifies = nullptr;
			TestTrue(TEXT("Extract includes Notifies"), (*ExtractedBody)->TryGetArrayField(TEXT("Notifies"), ExtractedNotifies));
			TestTrue(TEXT("Extract has one notify"), ExtractedNotifies && ExtractedNotifies->Num() == 1);
			if (ExtractedNotifies && ExtractedNotifies->Num() == 1)
			{
				const TSharedPtr<FJsonObject> ExtractedNotify = (*ExtractedNotifies)[0]->AsObject();
				TestTrue(TEXT("Extracted notify is object"), ExtractedNotify.IsValid());
				if (ExtractedNotify.IsValid())
				{
					TestEqual(TEXT("Extracted notify time"), ExtractedNotify->GetNumberField(TEXT("Time")), 0.10);
					TestTrue(TEXT("Extracted notify has Object"), ExtractedNotify->HasTypedField<EJson::Object>(TEXT("Object")));
					if (const TSharedPtr<FJsonObject>* ExtractedNotifyObject = nullptr; ExtractedNotify->TryGetObjectField(TEXT("Object"), ExtractedNotifyObject) && ExtractedNotifyObject && ExtractedNotifyObject->IsValid())
					{
						TestEqual(TEXT("Extracted notify class is stable"), (*ExtractedNotifyObject)->GetStringField(TEXT("Class")), FString(TestConcreteNotifyClassPath));
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* ExtractedNotifyStates = nullptr;
			TestTrue(TEXT("Extract includes NotifyStates"), (*ExtractedBody)->TryGetArrayField(TEXT("NotifyStates"), ExtractedNotifyStates));
			TestTrue(TEXT("Extract has one notify state"), ExtractedNotifyStates && ExtractedNotifyStates->Num() == 1);
			if (ExtractedNotifyStates && ExtractedNotifyStates->Num() == 1)
			{
				const TSharedPtr<FJsonObject> ExtractedNotifyState = (*ExtractedNotifyStates)[0]->AsObject();
				TestTrue(TEXT("Extracted notify state is object"), ExtractedNotifyState.IsValid());
				if (ExtractedNotifyState.IsValid())
				{
					TestEqual(TEXT("Extracted notify state time"), ExtractedNotifyState->GetNumberField(TEXT("Time")), 0.12);
					TestTrue(TEXT("Extracted notify state duration is positive"), ExtractedNotifyState->GetNumberField(TEXT("Duration")) > 0.0);
					TestTrue(TEXT("Extracted notify state has Object"), ExtractedNotifyState->HasTypedField<EJson::Object>(TEXT("Object")));
				}
			}

			const TSharedPtr<FJsonObject>* Skipped = nullptr;
			TestTrue(TEXT("Extract includes skipped metadata"), (*ExtractedBody)->TryGetObjectField(TEXT("_Skipped"), Skipped));
			if (Skipped && Skipped->IsValid())
			{
				TestTrue(TEXT("Skipped metadata includes branching point note"), (*Skipped)->HasField(TEXT("BranchingPoints")));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyNotifiesPreservesUnmanagedTest,
	"AssetFactory.AssetDocument.AnimMontage.Apply.NotifiesPreservesUnmanaged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyNotifiesPreservesUnmanagedTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_NotifyOwnership"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));

	TArray<TSharedPtr<FJsonValue>> Notifies;
	Notifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(0.10, MakeEmbeddedObjectRef(TestConcreteNotifyClassPath))));
	Body->SetArrayField(TEXT("Notifies"), Notifies);

	TArray<TSharedPtr<FJsonValue>> NotifyStates;
	NotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
		0.12,
		0.05,
		MakeEmbeddedObjectRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"), MakeShared<FJsonObject>()))));
	Body->SetArrayField(TEXT("NotifyStates"), NotifyStates);

	const FAssetDocumentResult CreateResult = ApplyDocument(MakeStructuredMontageDocument(Target, AnimSequence->GetPathName()));
	TestTrue(TEXT("Initial AnimMontage apply succeeds"), CreateResult.IsSuccess());
	if (!CreateResult.IsSuccess())
	{
		AddError(CreateResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Created AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	AddNotifyEvent(Montage, CreateTestNotify(Montage), FName(TEXT("Manual.Notify")), 0.02f);
	AddNotifyEvent(Montage, CreateTestNotify(Montage, TEXT("ManualNotifyNameCollision")), TestManagedNotifyName, 0.03f);
	AddNotifyStateEvent(Montage, NewObject<UAssetFactoryNamedAnimNotifyState>(Montage, NAME_None, RF_Transactional), FName(TEXT("Manual.NotifyState")), 0.04f, 0.02f);
	AddNotifyStateEvent(Montage, NewObject<UAssetFactoryNamedAnimNotifyState>(Montage, TEXT("ManualNotifyStateNameCollision"), RF_Transactional), TestManagedNotifyStateName, 0.05f, 0.02f);

	const FAssetDocumentResult ApplyResult = ApplyDocument(Document);
	TestTrue(TEXT("Notify ownership apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	TestEqual(TEXT("Unmanaged notify is preserved"), CountNotifyEventsByName(Montage, FName(TEXT("Manual.Notify"))), 1);
	TestEqual(TEXT("Unmanaged notify state is preserved"), CountNotifyEventsByName(Montage, FName(TEXT("Manual.NotifyState"))), 1);
	TestEqual(TEXT("Same-name manual notify is preserved"), CountNotifyEventsByObjectName(Montage, TEXT("ManualNotifyNameCollision")), 1);
	TestEqual(TEXT("Same-name manual notify state is preserved"), CountNotifyEventsByObjectName(Montage, TEXT("ManualNotifyStateNameCollision")), 1);
	TestEqual(TEXT("Managed notify is replaced without duplicate"), CountManagedNotifyEvents(Montage), 1);
	TestEqual(TEXT("Managed notify state is replaced without duplicate"), CountManagedNotifyStateEvents(Montage), 1);
	const int32 CountAfterFirstApply = Montage->Notifies.Num();

	const FAssetDocumentResult ReapplyResult = ApplyDocument(Document);
	TestTrue(TEXT("Repeated notify ownership apply succeeds"), ReapplyResult.IsSuccess());
	if (!ReapplyResult.IsSuccess())
	{
		AddError(ReapplyResult.Message);
		return false;
	}

	TestEqual(TEXT("Repeated apply does not grow notifies"), Montage->Notifies.Num(), CountAfterFirstApply);
	TestEqual(TEXT("Repeated apply keeps unmanaged notify"), CountNotifyEventsByName(Montage, FName(TEXT("Manual.Notify"))), 1);
	TestEqual(TEXT("Repeated apply keeps unmanaged notify state"), CountNotifyEventsByName(Montage, FName(TEXT("Manual.NotifyState"))), 1);
	TestEqual(TEXT("Repeated apply keeps same-name manual notify"), CountNotifyEventsByObjectName(Montage, TEXT("ManualNotifyNameCollision")), 1);
	TestEqual(TEXT("Repeated apply keeps same-name manual notify state"), CountNotifyEventsByObjectName(Montage, TEXT("ManualNotifyStateNameCollision")), 1);
	TestEqual(TEXT("Repeated apply keeps one managed notify"), CountManagedNotifyEvents(Montage), 1);
	TestEqual(TEXT("Repeated apply keeps one managed notify state"), CountManagedNotifyStateEvents(Montage), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyReplacesExistingManagedNotifiesTest,
	"AssetFactory.AssetDocument.AnimMontage.ApplyReplacesExistingManagedNotifies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyReplacesExistingManagedNotifiesTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_NotifyManagedRegion"));
	TSharedPtr<FJsonObject> InitialDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SetSingleManagedNotify(InitialDocument->GetObjectField(TEXT("Body")));
	SetSingleManagedNotifyState(InitialDocument->GetObjectField(TEXT("Body")));

	const FAssetDocumentResult InitialApplyResult = ApplyDocument(InitialDocument);
	TestTrue(TEXT("Initial managed notify apply succeeds"), InitialApplyResult.IsSuccess());
	if (!InitialApplyResult.IsSuccess())
	{
		AddError(InitialApplyResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Applied AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	AddNotifyEvent(Montage, CreateTestNotify(Montage), FName(TEXT("Manual.Notify")), 0.02f);
	AddNotifyStateEvent(Montage, NewObject<UAssetFactoryNamedAnimNotifyState>(Montage, NAME_None, RF_Transactional), FName(TEXT("Manual.NotifyState")), 0.04f, 0.02f);
	const FString WrongNameNotifyObjectName = MakeUniqueTestAssetName(TestManagedNotifyObjectPrefix);
	const FString WrongOuterNotifyObjectName = MakeUniqueTestAssetName(TestManagedNotifyObjectPrefix);
	const FString WrongNameNotifyStateObjectName = MakeUniqueTestAssetName(TestManagedNotifyStateObjectPrefix);
	const FString WrongOuterNotifyStateObjectName = MakeUniqueTestAssetName(TestManagedNotifyStateObjectPrefix);
	AddNotifyEvent(
		Montage,
		CreateTestNotify(Montage, FName(*WrongNameNotifyObjectName)),
		FName(TEXT("Manual.ReservedPrefixNotify")),
		0.06f);
	AddNotifyEvent(
		Montage,
		CreateTestNotifyWithOuter(GetTransientPackage(), FName(*WrongOuterNotifyObjectName)),
		TestManagedNotifyName,
		0.07f);
	AddNotifyStateEvent(
		Montage,
		NewObject<UAssetFactoryNamedAnimNotifyState>(Montage, FName(*WrongNameNotifyStateObjectName), RF_Transactional),
		FName(TEXT("Manual.ReservedPrefixNotifyState")),
		0.08f,
		0.02f);
	AddNotifyStateEvent(
		Montage,
		NewObject<UAssetFactoryNamedAnimNotifyState>(GetTransientPackage(), FName(*WrongOuterNotifyStateObjectName), RF_Transactional),
		TestManagedNotifyStateName,
		0.09f,
		0.02f);
	TestEqual(TEXT("Initial managed notify exists"), CountManagedNotifyEvents(Montage), 1);
	TestEqual(TEXT("Initial managed notify state exists"), CountManagedNotifyStateEvents(Montage), 1);

	TSharedPtr<FJsonObject> MissingNotifyRegionsDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	const FAssetDocumentResult MissingNotifyRegionsResult = ApplyDocument(MissingNotifyRegionsDocument);
	TestTrue(TEXT("Apply succeeds when notify regions are missing"), MissingNotifyRegionsResult.IsSuccess());
	if (!MissingNotifyRegionsResult.IsSuccess())
	{
		AddError(MissingNotifyRegionsResult.Message);
		return false;
	}

	TestEqual(TEXT("Missing Body.Notifies preserves managed notify"), CountManagedNotifyEvents(Montage), 1);
	TestEqual(TEXT("Missing Body.NotifyStates preserves managed notify state"), CountManagedNotifyStateEvents(Montage), 1);
	TestEqual(TEXT("Missing notify regions preserve unmanaged notify"), CountNotifyEventsByName(Montage, FName(TEXT("Manual.Notify"))), 1);
	TestEqual(TEXT("Missing notify regions preserve unmanaged notify state"), CountNotifyEventsByName(Montage, FName(TEXT("Manual.NotifyState"))), 1);

	TSharedPtr<FJsonObject> EmptyNotifiesDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	EmptyNotifiesDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), TArray<TSharedPtr<FJsonValue>>());
	const FAssetDocumentResult EmptyNotifiesResult = ApplyDocument(EmptyNotifiesDocument);
	TestTrue(TEXT("Apply succeeds with empty Body.Notifies"), EmptyNotifiesResult.IsSuccess());
	if (!EmptyNotifiesResult.IsSuccess())
	{
		AddError(EmptyNotifiesResult.Message);
		return false;
	}

	TestEqual(TEXT("Empty Body.Notifies clears managed notify"), CountManagedNotifyEvents(Montage), 0);
	TestEqual(TEXT("Empty Body.Notifies preserves managed notify state"), CountManagedNotifyStateEvents(Montage), 1);
	TestEqual(TEXT("Empty Body.Notifies preserves unmanaged notify"), CountNotifyEventsByName(Montage, FName(TEXT("Manual.Notify"))), 1);
	TestEqual(TEXT("Empty Body.Notifies preserves unmanaged notify state"), CountNotifyEventsByName(Montage, FName(TEXT("Manual.NotifyState"))), 1);
	TestEqual(TEXT("Empty Body.Notifies preserves reserved-prefix notify with wrong NotifyName"), CountNotifyEventsByObjectName(Montage, WrongNameNotifyObjectName), 1);
	TestEqual(TEXT("Empty Body.Notifies preserves reserved-prefix notify with wrong Outer"), CountNotifyEventsByObjectName(Montage, WrongOuterNotifyObjectName), 1);

	TSharedPtr<FJsonObject> RebuildNotifiesDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SetSingleManagedNotify(RebuildNotifiesDocument->GetObjectField(TEXT("Body")));
	const FAssetDocumentResult RebuildNotifiesResult = ApplyDocument(RebuildNotifiesDocument);
	TestTrue(TEXT("Apply rebuilds managed notify region"), RebuildNotifiesResult.IsSuccess());
	if (!RebuildNotifiesResult.IsSuccess())
	{
		AddError(RebuildNotifiesResult.Message);
		return false;
	}

	TestEqual(TEXT("Body.Notifies with values rebuilds managed notify"), CountManagedNotifyEvents(Montage), 1);
	TestEqual(TEXT("Body.Notifies rebuild preserves managed notify state"), CountManagedNotifyStateEvents(Montage), 1);
	TestEqual(TEXT("Body.Notifies rebuild preserves reserved-prefix notify with wrong NotifyName"), CountNotifyEventsByObjectName(Montage, WrongNameNotifyObjectName), 1);
	TestEqual(TEXT("Body.Notifies rebuild preserves reserved-prefix notify with wrong Outer"), CountNotifyEventsByObjectName(Montage, WrongOuterNotifyObjectName), 1);

	TSharedPtr<FJsonObject> EmptyNotifyStatesDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	EmptyNotifyStatesDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("NotifyStates"), TArray<TSharedPtr<FJsonValue>>());
	const FAssetDocumentResult EmptyNotifyStatesResult = ApplyDocument(EmptyNotifyStatesDocument);
	TestTrue(TEXT("Apply succeeds with empty Body.NotifyStates"), EmptyNotifyStatesResult.IsSuccess());
	if (!EmptyNotifyStatesResult.IsSuccess())
	{
		AddError(EmptyNotifyStatesResult.Message);
		return false;
	}

	TestEqual(TEXT("Empty Body.NotifyStates preserves managed notify"), CountManagedNotifyEvents(Montage), 1);
	TestEqual(TEXT("Empty Body.NotifyStates clears managed notify state"), CountManagedNotifyStateEvents(Montage), 0);
	TestEqual(TEXT("Empty Body.NotifyStates preserves unmanaged notify"), CountNotifyEventsByName(Montage, FName(TEXT("Manual.Notify"))), 1);
	TestEqual(TEXT("Empty Body.NotifyStates preserves unmanaged notify state"), CountNotifyEventsByName(Montage, FName(TEXT("Manual.NotifyState"))), 1);
	TestEqual(TEXT("Empty Body.NotifyStates preserves reserved-prefix notify state with wrong NotifyName"), CountNotifyEventsByObjectName(Montage, WrongNameNotifyStateObjectName), 1);
	TestEqual(TEXT("Empty Body.NotifyStates preserves reserved-prefix notify state with wrong Outer"), CountNotifyEventsByObjectName(Montage, WrongOuterNotifyStateObjectName), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageExtractSkipsUnmanagedNotifiesTest,
	"AssetFactory.AssetDocument.AnimMontage.Extract.SkipsUnmanagedNotifies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageExtractSkipsUnmanagedNotifiesTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_ExtractNotifyOwnership"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));

	TArray<TSharedPtr<FJsonValue>> Notifies;
	Notifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(0.10, MakeEmbeddedObjectRef(TestConcreteNotifyClassPath))));
	Body->SetArrayField(TEXT("Notifies"), Notifies);

	TArray<TSharedPtr<FJsonValue>> NotifyStates;
	NotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
		0.12,
		0.05,
		MakeEmbeddedObjectRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"), MakeShared<FJsonObject>()))));
	Body->SetArrayField(TEXT("NotifyStates"), NotifyStates);

	const FAssetDocumentResult ApplyResult = ApplyDocument(Document);
	TestTrue(TEXT("Initial notify apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Applied AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	AddNotifyEvent(Montage, CreateTestNotify(Montage, TEXT("ManualRoundtripNotify")), FName(TEXT("Manual.RoundtripNotify")), 0.02f);
	AddNotifyStateEvent(Montage, NewObject<UAssetFactoryNamedAnimNotifyState>(Montage, TEXT("ManualRoundtripNotifyState"), RF_Transactional), FName(TEXT("Manual.RoundtripNotifyState")), 0.03f, 0.02f);

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds with mixed notify ownership"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns payload"), ExtractResult.Payload.IsValid());
	if (!ExtractResult.IsSuccess() || !ExtractResult.Payload.IsValid())
	{
		AddError(ExtractResult.Message);
		return false;
	}

	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	TestTrue(TEXT("Extract includes Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody));
	if (!ExtractedBody || !ExtractedBody->IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ExtractedNotifies = nullptr;
	TestTrue(TEXT("Extract includes managed Notifies"), (*ExtractedBody)->TryGetArrayField(TEXT("Notifies"), ExtractedNotifies));
	TestTrue(TEXT("Extract outputs only one managed notify"), ExtractedNotifies && ExtractedNotifies->Num() == 1);

	const TArray<TSharedPtr<FJsonValue>>* ExtractedNotifyStates = nullptr;
	TestTrue(TEXT("Extract includes managed NotifyStates"), (*ExtractedBody)->TryGetArrayField(TEXT("NotifyStates"), ExtractedNotifyStates));
	TestTrue(TEXT("Extract outputs only one managed notify state"), ExtractedNotifyStates && ExtractedNotifyStates->Num() == 1);

	const TSharedPtr<FJsonObject>* Skipped = nullptr;
	TestTrue(TEXT("Extract includes skipped metadata"), (*ExtractedBody)->TryGetObjectField(TEXT("_Skipped"), Skipped));
	if (Skipped && Skipped->IsValid())
	{
		TestEqual(TEXT("Skipped unmanaged notify count"), (*Skipped)->GetNumberField(TEXT("UnmanagedNotifies")), 1.0);
		TestEqual(TEXT("Skipped unmanaged notify state count"), (*Skipped)->GetNumberField(TEXT("UnmanagedNotifyStates")), 1.0);
	}

	(*ExtractedBody)->RemoveField(TEXT("_Skipped"));
	const FAssetDocumentResult ReapplyExtractedResult = ApplyDocument(ExtractResult.Payload);
	TestTrue(TEXT("Reapplying extracted document succeeds"), ReapplyExtractedResult.IsSuccess());
	if (!ReapplyExtractedResult.IsSuccess())
	{
		AddError(ReapplyExtractedResult.Message);
		return false;
	}

	TestEqual(TEXT("Reapply preserves unmanaged notify once"), CountNotifyEventsByObjectName(Montage, TEXT("ManualRoundtripNotify")), 1);
	TestEqual(TEXT("Reapply preserves unmanaged notify state once"), CountNotifyEventsByObjectName(Montage, TEXT("ManualRoundtripNotifyState")), 1);
	TestEqual(TEXT("Reapply keeps one managed notify"), CountManagedNotifyEvents(Montage), 1);
	TestEqual(TEXT("Reapply keeps one managed notify state"), CountManagedNotifyStateEvents(Montage), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsSemanticInvalidBodyTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsSemanticInvalidBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsSemanticInvalidBodyTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString AnimReferencePath = AnimSequence->GetPathName();
	bool bAllCasesPassed = true;
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("StartPos string"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetFirstSegment(Document)->SetStringField(TEXT("StartPos"), TEXT("bad"));
	}, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/StartPos"), TEXT("InvalidNumericField"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("StartPos negative"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetFirstSegment(Document)->SetNumberField(TEXT("StartPos"), -0.1);
	}, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/StartPos"), TEXT("InvalidTime"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("AnimStartTime negative"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetFirstSegment(Document)->SetNumberField(TEXT("AnimStartTime"), -0.1);
	}, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/AnimStartTime"), TEXT("InvalidTime"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("AnimEndTime negative"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetFirstSegment(Document)->SetNumberField(TEXT("AnimEndTime"), -0.1);
	}, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/AnimEndTime"), TEXT("InvalidTime"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("AnimEndTime before start"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TSharedPtr<FJsonObject> Segment = GetFirstSegment(Document);
		Segment->SetNumberField(TEXT("AnimStartTime"), 0.2);
		Segment->SetNumberField(TEXT("AnimEndTime"), 0.1);
	}, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/AnimEndTime"), TEXT("InvalidAnimEndTime"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("AnimPlayRate zero"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetFirstSegment(Document)->SetNumberField(TEXT("AnimPlayRate"), 0.0);
	}, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/AnimPlayRate"), TEXT("InvalidAnimPlayRate"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("AnimPlayRate negative"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetFirstSegment(Document)->SetNumberField(TEXT("AnimPlayRate"), -1.0);
	}, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/AnimPlayRate"), TEXT("InvalidAnimPlayRate"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("LoopingCount string"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetFirstSegment(Document)->SetStringField(TEXT("LoopingCount"), TEXT("bad"));
	}, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/LoopingCount"), TEXT("InvalidNumericField"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("LoopingCount fractional"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetFirstSegment(Document)->SetNumberField(TEXT("LoopingCount"), 1.5);
	}, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/LoopingCount"), TEXT("InvalidLoopingCount"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("LoopingCount zero"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetFirstSegment(Document)->SetNumberField(TEXT("LoopingCount"), 0.0);
	}, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/LoopingCount"), TEXT("InvalidLoopingCount"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Duplicate section"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetCompositeSection(Document, 1)->SetStringField(TEXT("SectionName"), TEXT("Start"));
	}, TEXT("/Body/CompositeSections/1/SectionName"), TEXT("DuplicateSectionName"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Empty section"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetCompositeSection(Document, 0)->SetStringField(TEXT("SectionName"), TEXT(""));
	}, TEXT("/Body/CompositeSections/0/SectionName"), TEXT("MissingSectionName"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Blank next section"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetCompositeSection(Document, 0)->SetStringField(TEXT("NextSectionName"), TEXT("   "));
	}, TEXT("/Body/CompositeSections/0/NextSectionName"), TEXT("InvalidNextSectionName"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Missing next section"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		GetCompositeSection(Document, 0)->SetStringField(TEXT("NextSectionName"), TEXT("Missing"));
	}, TEXT("/Body/CompositeSections/0/NextSectionName"), TEXT("InvalidNextSectionName"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("NotifyState zero duration"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> NotifyStates;
		NotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
			0.12,
			0.0,
			MakeEmbeddedObjectRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"), MakeShared<FJsonObject>()))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("NotifyStates"), NotifyStates);
	}, TEXT("/Body/NotifyStates[0]/Duration"), TEXT("InvalidNotifyStateDuration"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Notify string time keeps path style"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		TSharedPtr<FJsonObject> Notify = MakeNotifyPlacement(
			0.10,
			MakeEmbeddedObjectRef(TestConcreteNotifyClassPath));
		Notify->SetStringField(TEXT("Time"), TEXT("bad"));
		Notifies.Add(MakeShared<FJsonValueObject>(Notify));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]/Time"), TEXT("InvalidNumericField"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Notify overflow time keeps numeric diagnostic"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		Notifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(
			1.0e40,
			MakeEmbeddedObjectRef(TestConcreteNotifyClassPath))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]/Time"), TEXT("InvalidNumericField"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("NotifyState overflow duration keeps numeric diagnostic"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> NotifyStates;
		NotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
			0.12,
			1.0e40,
			MakeEmbeddedObjectRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"), MakeShared<FJsonObject>()))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("NotifyStates"), NotifyStates);
	}, TEXT("/Body/NotifyStates[0]/Duration"), TEXT("InvalidNumericField"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Notify TrackIndex string keeps legacy diagnostic"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		TSharedPtr<FJsonObject> Notify = MakeNotifyPlacement(
			0.10,
			MakeEmbeddedObjectRef(TestConcreteNotifyClassPath));
		Notify->SetStringField(TEXT("TrackIndex"), TEXT("bad"));
		Notifies.Add(MakeShared<FJsonValueObject>(Notify));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]/TrackIndex"), TEXT("InvalidTrackIndex"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Notify TrackIndex negative keeps legacy diagnostic"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		TSharedPtr<FJsonObject> Notify = MakeNotifyPlacement(
			0.10,
			MakeEmbeddedObjectRef(TestConcreteNotifyClassPath));
		Notify->SetNumberField(TEXT("TrackIndex"), -1.0);
		Notifies.Add(MakeShared<FJsonValueObject>(Notify));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]/TrackIndex"), TEXT("InvalidTrackIndex"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Notify TrackIndex fractional keeps legacy diagnostic"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		TSharedPtr<FJsonObject> Notify = MakeNotifyPlacement(
			0.10,
			MakeEmbeddedObjectRef(TestConcreteNotifyClassPath));
		Notify->SetNumberField(TEXT("TrackIndex"), 1.5);
		Notifies.Add(MakeShared<FJsonValueObject>(Notify));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]/TrackIndex"), TEXT("InvalidTrackIndex"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Notify TrackIndex near-integer fractional keeps legacy diagnostic"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		TSharedPtr<FJsonObject> Notify = MakeNotifyPlacement(
			0.10,
			MakeEmbeddedObjectRef(TestConcreteNotifyClassPath));
		Notify->SetNumberField(TEXT("TrackIndex"), 1.000000001);
		Notifies.Add(MakeShared<FJsonValueObject>(Notify));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]/TrackIndex"), TEXT("InvalidTrackIndex"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Duplicate notify placement reports duplicate diagnostic"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		Notifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(
			0.10,
			MakeEmbeddedObjectRef(TestConcreteNotifyClassPath))));
		Notifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(
			0.10,
			MakeEmbeddedObjectRef(TestConcreteNotifyClassPath))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[1]"), TEXT("DuplicateNotifyPlacementKey"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Duplicate notify state placement reports duplicate diagnostic"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> NotifyStates;
		NotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
			0.12,
			0.05,
			MakeEmbeddedObjectRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"), MakeShared<FJsonObject>()))));
		NotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
			0.12,
			0.05,
			MakeEmbeddedObjectRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"), MakeShared<FJsonObject>()))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("NotifyStates"), NotifyStates);
	}, TEXT("/Body/NotifyStates[1]"), TEXT("DuplicateNotifyStatePlacementKey"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Notify wrong base class"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		Notifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(
			0.10,
			MakeEmbeddedObjectRef(TEXT("/Script/Engine.AnimNotifyState")))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]/Object"), TEXT("embeddedobject-base-class-mismatch"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Abstract notify class"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		Notifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(
			0.10,
			MakeEmbeddedObjectRef(TEXT("/Script/Engine.AnimNotify")))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]/Object"), TEXT("AbstractNotifyClass"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Notify direct ClassRef"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		Notifies.Add(MakeShared<FJsonValueObject>(MakeNotifyPlacement(
			0.10,
			AnimMontageMakeClassRef(TestConcreteNotifyClassPath))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]/Object"), TEXT("InvalidNotifyObjectFragment"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("NotifyState direct ClassRef"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> NotifyStates;
		NotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
			0.12,
			0.05,
			AnimMontageMakeClassRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState")))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("NotifyStates"), NotifyStates);
	}, TEXT("/Body/NotifyStates[0]/Object"), TEXT("InvalidNotifyObjectFragment"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("Notify non-object placement"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		Notifies.Add(MakeShared<FJsonValueString>(TEXT("bad")));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]"), TEXT("InvalidNotifyPlacement"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("NotifyState non-object placement"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> NotifyStates;
		NotifyStates.Add(MakeShared<FJsonValueString>(TEXT("bad")));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("NotifyStates"), NotifyStates);
	}, TEXT("/Body/NotifyStates[0]"), TEXT("InvalidNotifyPlacement"));

	return bAllCasesPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyFailureDoesNotMutateExistingTest,
	"AssetFactory.AssetDocument.AnimMontage.Apply.FailureDoesNotMutateExisting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyFailureDoesNotMutateExistingTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_NoPartialMutation"));
	const FAssetDocumentResult CreateResult = ApplyDocument(MakeStructuredMontageDocument(Target, AnimSequence->GetPathName()));
	TestTrue(TEXT("Initial AnimMontage apply succeeds"), CreateResult.IsSuccess());
	if (!CreateResult.IsSuccess())
	{
		AddError(CreateResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Created AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	USkeleton* OriginalSkeleton = Montage->GetSkeleton();
	USkeletalMesh* OriginalPreviewMesh = Montage->GetPreviewMesh();
	const int32 OriginalSlotCount = Montage->SlotAnimTracks.Num();
	const int32 OriginalSectionCount = Montage->CompositeSections.Num();
	const float OriginalBlendInTime = Montage->GetDefaultBlendInTime();
	const float OriginalBlendOutTime = Montage->GetDefaultBlendOutTime();
	FFloatProperty* RateScaleProperty = FindFProperty<FFloatProperty>(Montage->GetClass(), TEXT("RateScale"));
	TestNotNull(TEXT("AnimMontage exposes reflected RateScale property"), RateScaleProperty);
	if (!RateScaleProperty)
	{
		return false;
	}
	const float OriginalRateScale = RateScaleProperty->GetPropertyValue_InContainer(Montage);

	TSharedPtr<FJsonObject> InvalidDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	InvalidDocument->GetObjectField(TEXT("Properties"))->SetNumberField(TEXT("RateScale"), OriginalRateScale + 0.5f);
	TSharedPtr<FJsonObject> InvalidBody = InvalidDocument->GetObjectField(TEXT("Body"));
	InvalidBody->SetField(TEXT("Skeleton"), MakeShared<FJsonValueNull>());
	InvalidBody->SetField(TEXT("PreviewMesh"), MakeShared<FJsonValueNull>());
	InvalidBody->GetObjectField(TEXT("Blend"))->SetNumberField(TEXT("BlendInTime"), 0.9);
	InvalidBody->GetObjectField(TEXT("Blend"))->SetNumberField(TEXT("BlendOutTime"), 0.8);
	TSharedPtr<FJsonObject> Segment = GetFirstSegment(InvalidDocument);
	Segment->SetNumberField(TEXT("AnimStartTime"), 0.2);
	Segment->SetNumberField(TEXT("AnimEndTime"), 0.1);

	const FAssetDocumentResult InvalidResult = ApplyDocument(InvalidDocument);
	TestFalse(TEXT("Invalid patch fails"), InvalidResult.IsSuccess());
	TestTrue(TEXT("Invalid patch reports AnimEndTime diagnostic"), AnimMontageHasDiagnostic(InvalidResult, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/AnimEndTime"), TEXT("InvalidAnimEndTime")));

	TestEqual(TEXT("Skeleton is unchanged after failed patch"), Montage->GetSkeleton(), OriginalSkeleton);
	TestEqual(TEXT("Preview mesh is unchanged after failed patch"), Montage->GetPreviewMesh(), OriginalPreviewMesh);
	TestEqual(TEXT("Slot tracks are unchanged after failed patch"), Montage->SlotAnimTracks.Num(), OriginalSlotCount);
	TestEqual(TEXT("Composite sections are unchanged after failed patch"), Montage->CompositeSections.Num(), OriginalSectionCount);
	TestEqual(TEXT("BlendInTime is unchanged after failed patch"), Montage->GetDefaultBlendInTime(), OriginalBlendInTime);
	TestEqual(TEXT("BlendOutTime is unchanged after failed patch"), Montage->GetDefaultBlendOutTime(), OriginalBlendOutTime);
	TestEqual(TEXT("Reflected properties are unchanged after failed Body preflight"), RateScaleProperty->GetPropertyValue_InContainer(Montage), OriginalRateScale);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyPreflightDoesNotCreateEmbeddedObjectsTest,
	"AssetFactory.AssetDocument.AnimMontage.Apply.PreflightDoesNotCreateEmbeddedObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyPreflightDoesNotCreateEmbeddedObjectsTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_PreflightEmbeddedObject"));
	const FAssetDocumentResult CreateResult = ApplyDocument(MakeStructuredMontageDocument(Target, AnimSequence->GetPathName()));
	TestTrue(TEXT("Initial AnimMontage apply succeeds"), CreateResult.IsSuccess());
	if (!CreateResult.IsSuccess())
	{
		AddError(CreateResult.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("Created AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	FFloatProperty* RateScaleProperty = FindFProperty<FFloatProperty>(Montage->GetClass(), TEXT("RateScale"));
	TestNotNull(TEXT("AnimMontage exposes reflected RateScale property"), RateScaleProperty);
	if (!RateScaleProperty)
	{
		return false;
	}

	const int32 OriginalDirectObjectCount = CountDirectObjectsWithOuter(Montage);
	const float OriginalRateScale = RateScaleProperty->GetPropertyValue_InContainer(Montage);
	const int32 OriginalSlotCount = Montage->SlotAnimTracks.Num();
	const int32 OriginalSectionCount = Montage->CompositeSections.Num();

	TSharedPtr<FJsonObject> InvalidDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	InvalidDocument->GetObjectField(TEXT("Properties"))->SetNumberField(TEXT("RateScale"), OriginalRateScale + 0.5f);
	GetFirstSegment(InvalidDocument)->SetObjectField(TEXT("AnimReference"), MakeEmbeddedObjectRef(TEXT("/Script/Engine.AnimComposite")));
	GetCompositeSection(InvalidDocument, 0)->SetStringField(TEXT("NextSectionName"), TEXT("Missing"));

	const FAssetDocumentResult InvalidResult = ApplyDocument(InvalidDocument);
	TestFalse(TEXT("Invalid patch fails"), InvalidResult.IsSuccess());
	TestTrue(TEXT("Invalid patch reports NextSectionName diagnostic"), AnimMontageHasDiagnostic(InvalidResult, TEXT("/Body/CompositeSections/0/NextSectionName"), TEXT("InvalidNextSectionName")));

	TestEqual(TEXT("Preflight does not create direct child objects under production montage"), CountDirectObjectsWithOuter(Montage), OriginalDirectObjectCount);
	TestEqual(TEXT("Reflected property is unchanged after failed Body preflight"), RateScaleProperty->GetPropertyValue_InContainer(Montage), OriginalRateScale);
	TestEqual(TEXT("Slot tracks are unchanged after failed preflight"), Montage->SlotAnimTracks.Num(), OriginalSlotCount);
	TestEqual(TEXT("Composite sections are unchanged after failed preflight"), Montage->CompositeSections.Num(), OriginalSectionCount);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyFailureCleansNewAssetTest,
	"AssetFactory.AssetDocument.AnimMontage.Apply.FailureCleansNewAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyFailureCleansNewAssetTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_FailedCreateCleanup"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	GetFirstSegment(Document)->SetNumberField(TEXT("AnimPlayRate"), 0.0);

	const FAssetDocumentResult Result = ApplyDocument(Document);
	TestFalse(TEXT("Invalid new AnimMontage apply fails"), Result.IsSuccess());
	TestTrue(TEXT("Invalid new apply reports AnimPlayRate diagnostic"), AnimMontageHasDiagnostic(Result, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/AnimPlayRate"), TEXT("InvalidAnimPlayRate")));
	TestNull(TEXT("Failed new AnimMontage apply does not leave a loadable asset"), LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyDefinitionRefBodyTest,
	"AssetFactory.AssetDocument.AnimMontage.Apply.DefinitionRefBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyDefinitionRefBodyTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_DefinitionRefBody"));
	TSharedPtr<FJsonObject> Document = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> Definitions = Document->GetObjectField(TEXT("Definitions"));
	Definitions->SetObjectField(TEXT("SkeletonAsset"), AnimMontageMakeAssetRef(TestSkeletonPath));
	Definitions->SetObjectField(TEXT("PreviewMeshAsset"), AnimMontageMakeAssetRef(TestPreviewMeshPath));
	Definitions->SetObjectField(TEXT("AnimAsset"), AnimMontageMakeAssetRef(AnimSequence->GetPathName()));

	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	Body->SetObjectField(TEXT("Skeleton"), MakeDefinitionRef(TEXT("SkeletonAsset")));
	Body->SetObjectField(TEXT("PreviewMesh"), MakeDefinitionRef(TEXT("PreviewMeshAsset")));
	GetFirstSegment(Document)->SetObjectField(TEXT("AnimReference"), MakeDefinitionRef(TEXT("AnimAsset")));

	const FAssetDocumentResult Result = ApplyDocument(Document);
	TestTrue(TEXT("DefinitionRef Body apply succeeds"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MakeObjectPathFromTarget(Target));
	TestNotNull(TEXT("DefinitionRef AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	TestNotNull(TEXT("DefinitionRef montage has skeleton"), Montage->GetSkeleton());
	TestNotNull(TEXT("DefinitionRef montage has preview mesh"), Montage->GetPreviewMesh());
	TestEqual(TEXT("DefinitionRef montage has one SlotAnimTrack"), Montage->SlotAnimTracks.Num(), 1);
	if (Montage->SlotAnimTracks.Num() == 1 && Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 1)
	{
		TestEqual(TEXT("DefinitionRef segment uses generated animation"), Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference().Get(), AnimSequence);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageDiffTreatsDefinitionRefAsUnchangedTest,
	"AssetFactory.AssetDocument.AnimMontage.DiffTreatsDefinitionRefAsUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageDiffTreatsDefinitionRefAsUnchangedTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_DiffDefinitionRef"));
	const FAssetDocumentResult CreateResult = ApplyDocument(MakeStructuredMontageDocument(Target, AnimSequence->GetPathName()));
	TestTrue(TEXT("Initial AnimMontage apply succeeds"), CreateResult.IsSuccess());
	if (!CreateResult.IsSuccess())
	{
		AddError(CreateResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> DesiredDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> Definitions = DesiredDocument->GetObjectField(TEXT("Definitions"));
	Definitions->SetObjectField(TEXT("SkeletonAsset"), AnimMontageMakeAssetRef(TestSkeletonPath));
	Definitions->SetObjectField(TEXT("PreviewMeshAsset"), AnimMontageMakeAssetRef(TestPreviewMeshPath));
	Definitions->SetObjectField(TEXT("AnimAsset"), AnimMontageMakeAssetRef(AnimSequence->GetPathName()));

	TSharedPtr<FJsonObject> Body = DesiredDocument->GetObjectField(TEXT("Body"));
	Body->SetObjectField(TEXT("Skeleton"), MakeDefinitionRef(TEXT("SkeletonAsset")));
	Body->SetObjectField(TEXT("PreviewMesh"), MakeDefinitionRef(TEXT("PreviewMeshAsset")));
	GetFirstSegment(DesiredDocument)->SetObjectField(TEXT("AnimReference"), MakeDefinitionRef(TEXT("AnimAsset")));

	const FAssetDocumentResult DiffResult = DiffDocument(DesiredDocument);
	TestTrue(TEXT("Diff succeeds for DefinitionRef-equivalent AnimMontage Body"), DiffResult.IsSuccess());
	TestTrue(TEXT("Diff returns a payload"), DiffResult.Payload.IsValid());
	if (!DiffResult.Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
	TestTrue(TEXT("Diff payload includes changed array"), DiffResult.Payload->TryGetArrayField(TEXT("changed"), Changed));
	TestFalse(TEXT("Diff does not report changed SlotAnimTracks for equivalent DefinitionRef"), Changed && JsonArrayContainsPathStatus(*Changed, TEXT("/Body/SlotAnimTracks"), TEXT("changed")));

	const TArray<TSharedPtr<FJsonValue>>* Unchanged = nullptr;
	TestTrue(TEXT("Diff payload includes unchanged array"), DiffResult.Payload->TryGetArrayField(TEXT("unchanged"), Unchanged));
	TestTrue(TEXT("Diff reports unchanged SlotAnimTracks for equivalent DefinitionRef"), Unchanged && JsonArrayContainsPathStatus(*Unchanged, TEXT("/Body/SlotAnimTracks"), TEXT("unchanged")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageDiffIgnoresObjectFieldOrderTest,
	"AssetFactory.AssetDocument.AnimMontage.DiffIgnoresObjectFieldOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageDiffIgnoresObjectFieldOrderTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_DiffFieldOrder"));
	const FAssetDocumentResult CreateResult = ApplyDocument(MakeStructuredMontageDocument(Target, AnimSequence->GetPathName()));
	TestTrue(TEXT("Initial AnimMontage apply succeeds"), CreateResult.IsSuccess());
	if (!CreateResult.IsSuccess())
	{
		AddError(CreateResult.Message);
		return false;
	}

	const FAssetDocumentResult DiffResult = DiffDocument(MakeReorderedStructuredMontageDocument(Target, AnimSequence->GetPathName()));
	TestTrue(TEXT("Diff succeeds for reordered AnimMontage Body"), DiffResult.IsSuccess());
	TestTrue(TEXT("Diff returns a payload"), DiffResult.Payload.IsValid());
	if (!DiffResult.Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
	TestTrue(TEXT("Diff payload includes changed array"), DiffResult.Payload->TryGetArrayField(TEXT("changed"), Changed));
	TestFalse(TEXT("Diff does not report changed SlotAnimTracks for reordered fields"), Changed && JsonArrayContainsPathStatus(*Changed, TEXT("/Body/SlotAnimTracks"), TEXT("changed")));
	TestFalse(TEXT("Diff does not report changed CompositeSections for reordered fields"), Changed && JsonArrayContainsPathStatus(*Changed, TEXT("/Body/CompositeSections"), TEXT("changed")));
	TestFalse(TEXT("Diff does not report changed Blend for reordered fields"), Changed && JsonArrayContainsPathStatus(*Changed, TEXT("/Body/Blend"), TEXT("changed")));

	const TArray<TSharedPtr<FJsonValue>>* Unchanged = nullptr;
	TestTrue(TEXT("Diff payload includes unchanged array"), DiffResult.Payload->TryGetArrayField(TEXT("unchanged"), Unchanged));
	TestTrue(TEXT("Diff reports unchanged SlotAnimTracks for reordered fields"), Unchanged && JsonArrayContainsPathStatus(*Unchanged, TEXT("/Body/SlotAnimTracks"), TEXT("unchanged")));
	TestTrue(TEXT("Diff reports unchanged CompositeSections for reordered fields"), Unchanged && JsonArrayContainsPathStatus(*Unchanged, TEXT("/Body/CompositeSections"), TEXT("unchanged")));
	TestTrue(TEXT("Diff reports unchanged Blend for reordered fields"), Unchanged && JsonArrayContainsPathStatus(*Unchanged, TEXT("/Body/Blend"), TEXT("unchanged")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageDiffReportsChangedBodySectionsTest,
	"AssetFactory.AssetDocument.AnimMontage.DiffReportsChangedBodySections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageDiffReportsChangedBodySectionsTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_DiffBody"));
	const FAssetDocumentResult CreateResult = ApplyDocument(MakeStructuredMontageDocument(Target, AnimSequence->GetPathName()));
	TestTrue(TEXT("Initial AnimMontage apply succeeds"), CreateResult.IsSuccess());
	if (!CreateResult.IsSuccess())
	{
		AddError(CreateResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> DesiredDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	TArray<TSharedPtr<FJsonValue>> SlotAnimTracks = DesiredDocument->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("SlotAnimTracks"));

	TSharedPtr<FJsonObject> ExtraSlot = MakeShared<FJsonObject>();
	ExtraSlot->SetStringField(TEXT("SlotName"), TEXT("UpperBody"));
	TSharedPtr<FJsonObject> ExtraAnimTrack = MakeShared<FJsonObject>();
	ExtraAnimTrack->SetArrayField(TEXT("AnimSegments"), TArray<TSharedPtr<FJsonValue>>());
	ExtraSlot->SetObjectField(TEXT("AnimTrack"), ExtraAnimTrack);
	SlotAnimTracks.Add(MakeShared<FJsonValueObject>(ExtraSlot));
	DesiredDocument->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks);

	const FAssetDocumentResult DiffResult = DiffDocument(DesiredDocument);
	TestTrue(TEXT("Diff succeeds for changed AnimMontage Body"), DiffResult.IsSuccess());
	TestTrue(TEXT("Diff returns a payload"), DiffResult.Payload.IsValid());
	if (!DiffResult.Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
	TestTrue(TEXT("Diff payload includes changed array"), DiffResult.Payload->TryGetArrayField(TEXT("changed"), Changed));
	TestTrue(TEXT("Diff reports changed SlotAnimTracks path"), Changed && JsonArrayContainsPathStatus(*Changed, TEXT("/Body/SlotAnimTracks"), TEXT("changed")));

	const TArray<TSharedPtr<FJsonValue>>* Failed = nullptr;
	TestTrue(TEXT("Diff payload includes failed array"), DiffResult.Payload->TryGetArrayField(TEXT("failed"), Failed));
	TestTrue(TEXT("Diff has no failed Body entries"), Failed && Failed->Num() == 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageDiffIgnoresSkippedMetadataTest,
	"AssetFactory.AssetDocument.AnimMontage.DiffIgnoresSkippedMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageDiffIgnoresSkippedMetadataTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const FString Target = MakeUniqueMontageTarget(TEXT("AM_DiffSkipped"));
	const FAssetDocumentResult CreateResult = ApplyDocument(MakeStructuredMontageDocument(Target, AnimSequence->GetPathName()));
	TestTrue(TEXT("Initial AnimMontage apply succeeds"), CreateResult.IsSuccess());
	if (!CreateResult.IsSuccess())
	{
		AddError(CreateResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> DesiredDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	TSharedPtr<FJsonObject> Skipped = MakeShared<FJsonObject>();
	Skipped->SetNumberField(TEXT("UnmanagedNotifies"), 1.0);
	DesiredDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("_Skipped"), Skipped);

	const FAssetDocumentResult DiffResult = DiffDocument(DesiredDocument);
	TestTrue(TEXT("Diff succeeds with extract-only _Skipped Body metadata"), DiffResult.IsSuccess());
	if (!DiffResult.IsSuccess())
	{
		AddError(DiffResult.Message);
		return false;
	}
	TestTrue(TEXT("Diff returns a payload"), DiffResult.Payload.IsValid());
	if (!DiffResult.Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
	TestTrue(TEXT("Diff payload includes changed array"), DiffResult.Payload->TryGetArrayField(TEXT("changed"), Changed));
	TestFalse(TEXT("Diff does not report changed _Skipped metadata"), Changed && JsonArrayContainsPath(*Changed, TEXT("/Body/_Skipped")));

	const TArray<TSharedPtr<FJsonValue>>* Unchanged = nullptr;
	TestTrue(TEXT("Diff payload includes unchanged array"), DiffResult.Payload->TryGetArrayField(TEXT("unchanged"), Unchanged));
	TestFalse(TEXT("Diff does not report unchanged _Skipped metadata"), Unchanged && JsonArrayContainsPath(*Unchanged, TEXT("/Body/_Skipped")));

	const TArray<TSharedPtr<FJsonValue>>* SkippedEntries = nullptr;
	TestTrue(TEXT("Diff payload includes skipped array"), DiffResult.Payload->TryGetArrayField(TEXT("skipped"), SkippedEntries));
	TestFalse(TEXT("Diff does not report skipped _Skipped metadata"), SkippedEntries && JsonArrayContainsPath(*SkippedEntries, TEXT("/Body/_Skipped")));

	const TArray<TSharedPtr<FJsonValue>>* Failed = nullptr;
	TestTrue(TEXT("Diff payload includes failed array"), DiffResult.Payload->TryGetArrayField(TEXT("failed"), Failed));
	TestFalse(TEXT("Diff does not report failed _Skipped metadata"), Failed && JsonArrayContainsPath(*Failed, TEXT("/Body/_Skipped")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageInspectProfileTest,
	"AssetFactory.AssetDocument.AnimMontage.InspectProfileIncludesStructuredBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageInspectProfileTest::RunTest(const FString& Parameters)
{
	const FAnimMontageAssetDocumentProfile Profile;
	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	TestTrue(TEXT("AnimMontage profile declares at least five region policies"), Policies.Num() >= 5);

	FAssetDocumentRegionPolicy BlendPolicy;
	TestTrue(TEXT("AnimMontage profile declares Body.Blend policy"), Profile.GetRegionPolicy(TEXT("Body.Blend"), BlendPolicy));
	TestEqual(TEXT("Body.Blend path is dotted body path"), BlendPolicy.BodyPath, FString(TEXT("Body.Blend")));
	TestEqual(TEXT("Body.Blend uses object region kind"), BlendPolicy.RegionKind, EAssetDocumentRegionKind::Object);
	TestEqual(TEXT("Body.Blend uses CDO default source"), BlendPolicy.DefaultSource, EAssetDocumentDefaultSource::CDO);
	TestEqual(TEXT("Body.Blend uses default-diff reducer"), BlendPolicy.ReducerMode, EAssetDocumentReducerMode::DefaultDiff);
	TestEqual(TEXT("Body.Blend uses set-property apply mode"), BlendPolicy.ApplyMode, EAssetDocumentApplyMode::SetProperty);

	FAssetDocumentRegionPolicy SlotAnimTracksPolicy;
	TestTrue(TEXT("AnimMontage profile declares Body.SlotAnimTracks policy"), FindRegionPolicy(Policies, TEXT("Body.SlotAnimTracks"), SlotAnimTracksPolicy));
	TestEqual(TEXT("Body.SlotAnimTracks uses array region kind"), SlotAnimTracksPolicy.RegionKind, EAssetDocumentRegionKind::Array);
	TestEqual(TEXT("Body.SlotAnimTracks uses managed reducer"), SlotAnimTracksPolicy.ReducerMode, EAssetDocumentReducerMode::ManagedRegion);
	TestEqual(TEXT("Body.SlotAnimTracks uses managed array apply mode"), SlotAnimTracksPolicy.ApplyMode, EAssetDocumentApplyMode::RebuildArrayRegion);
	TestTrue(TEXT("Body.SlotAnimTracks owns SlotAnimTracks property"), SlotAnimTracksPolicy.ManagedUePropertyPaths.Contains(TEXT("SlotAnimTracks")));

	FAssetDocumentRegionPolicy CompositeSectionsPolicy;
	TestTrue(TEXT("AnimMontage profile declares Body.CompositeSections policy"), FindRegionPolicy(Policies, TEXT("Body.CompositeSections"), CompositeSectionsPolicy));
	TestEqual(TEXT("Body.CompositeSections uses timeline region kind"), CompositeSectionsPolicy.RegionKind, EAssetDocumentRegionKind::Timeline);
	TestEqual(TEXT("Body.CompositeSections uses managed reducer"), CompositeSectionsPolicy.ReducerMode, EAssetDocumentReducerMode::ManagedRegion);
	TestEqual(TEXT("Body.CompositeSections uses managed timeline apply mode"), CompositeSectionsPolicy.ApplyMode, EAssetDocumentApplyMode::RebuildArrayRegion);
	TestTrue(TEXT("Body.CompositeSections owns CompositeSections property"), CompositeSectionsPolicy.ManagedUePropertyPaths.Contains(TEXT("CompositeSections")));

	FAssetDocumentRegionPolicy NotifiesPolicy;
	TestTrue(TEXT("AnimMontage profile declares Body.Notifies policy"), Profile.GetRegionPolicy(TEXT("Body.Notifies"), NotifiesPolicy));
	TestEqual(TEXT("Body.Notifies uses timeline region kind"), NotifiesPolicy.RegionKind, EAssetDocumentRegionKind::Timeline);
	TestEqual(TEXT("Body.Notifies uses managed reducer"), NotifiesPolicy.ReducerMode, EAssetDocumentReducerMode::ManagedRegion);
	TestEqual(TEXT("Body.Notifies uses managed timeline apply mode"), NotifiesPolicy.ApplyMode, EAssetDocumentApplyMode::RebuildArrayRegion);
	TestTrue(TEXT("Body.Notifies owns Notifies property"), NotifiesPolicy.ManagedUePropertyPaths.Contains(TEXT("Notifies")));

	FAssetDocumentRegionPolicy NotifyStatesPolicy;
	TestTrue(TEXT("AnimMontage profile declares Body.NotifyStates policy"), Profile.GetRegionPolicy(TEXT("Body.NotifyStates"), NotifyStatesPolicy));
	TestEqual(TEXT("Body.NotifyStates keeps a distinct region id"), NotifyStatesPolicy.RegionId, FName(TEXT("Body.NotifyStates")));
	TestEqual(TEXT("Body.NotifyStates uses timeline region kind"), NotifyStatesPolicy.RegionKind, EAssetDocumentRegionKind::Timeline);
	TestEqual(TEXT("Body.NotifyStates uses managed reducer"), NotifyStatesPolicy.ReducerMode, EAssetDocumentReducerMode::ManagedRegion);
	TestEqual(TEXT("Body.NotifyStates uses managed timeline apply mode"), NotifyStatesPolicy.ApplyMode, EAssetDocumentApplyMode::RebuildArrayRegion);
	TestTrue(TEXT("Body.NotifyStates owns Notifies property"), NotifyStatesPolicy.ManagedUePropertyPaths.Contains(TEXT("Notifies")));

	TestRegionPolicyContract(
		this,
		Profile,
		TEXT("Body.References"),
		EAssetDocumentRegionKind::Object,
		EAssetDocumentReducerMode::DefaultDiff,
		EAssetDocumentApplyMode::SetProperty,
		{TEXT("Skeleton")});
	TestRegionPolicyContract(
		this,
		Profile,
		TEXT("Body.Preview"),
		EAssetDocumentRegionKind::Object,
		EAssetDocumentReducerMode::DefaultDiff,
		EAssetDocumentApplyMode::SetProperty,
		{TEXT("PreviewMesh"), TEXT("PreviewBasePose")});
	TestRegionPolicyContract(
		this,
		Profile,
		TEXT("Body.Sync"),
		EAssetDocumentRegionKind::Object,
		EAssetDocumentReducerMode::DefaultDiff,
		EAssetDocumentApplyMode::SetProperty,
		{TEXT("SyncGroup"), TEXT("SyncSlotIndex")});
	TestRegionPolicyContract(
		this,
		Profile,
		TEXT("Body.RootMotion"),
		EAssetDocumentRegionKind::Object,
		EAssetDocumentReducerMode::DefaultDiff,
		EAssetDocumentApplyMode::SetProperty,
		{
			TEXT("bEnableRootMotionTranslation"),
			TEXT("bEnableRootMotionRotation"),
			TEXT("RootMotionRootLock"),
		});
	TestRegionPolicyContract(
		this,
		Profile,
		TEXT("Body.Metadata"),
		EAssetDocumentRegionKind::Array,
		EAssetDocumentReducerMode::ManagedRegion,
		EAssetDocumentApplyMode::RebuildArrayRegion,
		{TEXT("MetaData")});
	TestRegionPolicyContract(
		this,
		Profile,
		TEXT("Body.SectionMetadata"),
		EAssetDocumentRegionKind::Object,
		EAssetDocumentReducerMode::ManagedRegion,
		EAssetDocumentApplyMode::RebuildArrayRegion,
		{TEXT("CompositeSections")});
	TestRegionPolicyContract(
		this,
		Profile,
		TEXT("Body.TimeStretch"),
		EAssetDocumentRegionKind::Object,
		EAssetDocumentReducerMode::DefaultDiff,
		EAssetDocumentApplyMode::SetProperty,
		{TEXT("TimeStretchCurve"), TEXT("TimeStretchCurveName")});
	TestRegionPolicyContract(
		this,
		Profile,
		TEXT("Body.Curves"),
		EAssetDocumentRegionKind::Array,
		EAssetDocumentReducerMode::ManagedRegion,
		EAssetDocumentApplyMode::RebuildArrayRegion,
		{TEXT("RawCurveData")});

	const FAssetDocumentService Service;
	FAssetDocumentProfileRequest Request;
	Request.ClassOrAsset = TEXT("/Script/Engine.AnimMontage");

	const FAssetDocumentResult Result = Service.InspectProfile(Request);
	TestTrue(TEXT("InspectProfile succeeds for AnimMontage class"), Result.IsSuccess());
	TestTrue(TEXT("InspectProfile returns a payload"), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* BodySections = nullptr;
	TestTrue(TEXT("AnimMontage profile includes BodySections"), Result.Payload->TryGetArrayField(TEXT("BodySections"), BodySections));
	if (BodySections)
	{
		TestTrue(TEXT("BodySections includes expected AnimMontage keys"), HasExpectedBodySections(*BodySections));
		TestFalse(TEXT("BodySections does not expose extract-only skipped metadata"), JsonArrayContainsString(*BodySections, TEXT("_Skipped")));
	}

	const TArray<TSharedPtr<FJsonValue>>* FragmentKinds = nullptr;
	TestTrue(TEXT("AnimMontage profile includes FragmentKinds"), Result.Payload->TryGetArrayField(TEXT("FragmentKinds"), FragmentKinds));
	if (FragmentKinds)
	{
		TestTrue(TEXT("FragmentKinds includes expected AssetDocument fragment kinds"), HasExpectedFragmentKinds(*FragmentKinds));
	}

	const TArray<TSharedPtr<FJsonValue>>* InternalAdapters = nullptr;
	TestTrue(TEXT("AnimMontage profile includes internal adapters"), Result.Payload->TryGetArrayField(TEXT("InternalAdapters"), InternalAdapters));
	if (InternalAdapters)
	{
		TestTrue(TEXT("InternalAdapters includes AnimMontage body adapter"), JsonArrayContainsString(*InternalAdapters, TEXT("AnimMontageBody")));
		TestTrue(TEXT("InternalAdapters includes AnimMontage notify placement adapter"), JsonArrayContainsString(*InternalAdapters, TEXT("AnimMontageNotifyPlacementAdapter")));
	}

	const TArray<TSharedPtr<FJsonValue>>* RegionPolicies = nullptr;
	TestTrue(TEXT("AnimMontage profile includes RegionPolicies"), Result.Payload->TryGetArrayField(TEXT("RegionPolicies"), RegionPolicies));
	if (RegionPolicies)
	{
		const TSharedPtr<FJsonObject> BlendPolicyJson = FindJsonObjectByStringField(*RegionPolicies, TEXT("RegionId"), TEXT("Body.Blend"));
		TestTrue(TEXT("RegionPolicies includes Body.Blend"), BlendPolicyJson.IsValid());
		if (BlendPolicyJson.IsValid())
		{
			TestEqual(TEXT("Body.Blend policy exports BodyPath"), BlendPolicyJson->GetStringField(TEXT("BodyPath")), FString(TEXT("Body.Blend")));
			TestEqual(TEXT("Body.Blend policy exports RegionKind"), BlendPolicyJson->GetStringField(TEXT("RegionKind")), FString(TEXT("Object")));
			TestEqual(TEXT("Body.Blend policy exports DefaultSource"), BlendPolicyJson->GetStringField(TEXT("DefaultSource")), FString(TEXT("CDO")));
			TestEqual(TEXT("Body.Blend policy exports ReducerMode"), BlendPolicyJson->GetStringField(TEXT("ReducerMode")), FString(TEXT("DefaultDiff")));
			TestEqual(TEXT("Body.Blend policy exports ApplyMode"), BlendPolicyJson->GetStringField(TEXT("ApplyMode")), FString(TEXT("SetProperty")));
			TestNoAgentFacingOperationFields(this, TEXT("Body.Blend policy"), BlendPolicyJson);
		}

		const TSharedPtr<FJsonObject> SlotAnimTracksPolicyJson = FindJsonObjectByStringField(*RegionPolicies, TEXT("RegionId"), TEXT("Body.SlotAnimTracks"));
		TestTrue(TEXT("RegionPolicies includes Body.SlotAnimTracks"), SlotAnimTracksPolicyJson.IsValid());
		if (SlotAnimTracksPolicyJson.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* ManagedPaths = nullptr;
			TestTrue(TEXT("Body.SlotAnimTracks policy exports ManagedUePropertyPaths"), SlotAnimTracksPolicyJson->TryGetArrayField(TEXT("ManagedUePropertyPaths"), ManagedPaths));
			TestTrue(TEXT("Body.SlotAnimTracks policy owns SlotAnimTracks"), ManagedPaths && JsonArrayContainsString(*ManagedPaths, TEXT("SlotAnimTracks")));
		}

		const TSharedPtr<FJsonObject> NotifiesPolicyJson = FindJsonObjectByStringField(*RegionPolicies, TEXT("RegionId"), TEXT("Body.Notifies"));
		TestTrue(TEXT("RegionPolicies includes Body.Notifies"), NotifiesPolicyJson.IsValid());
		if (NotifiesPolicyJson.IsValid())
		{
			TestEqual(TEXT("Body.Notifies policy exports RegionKind"), NotifiesPolicyJson->GetStringField(TEXT("RegionKind")), FString(TEXT("Timeline")));
			TestEqual(TEXT("Body.Notifies policy exports ReducerMode"), NotifiesPolicyJson->GetStringField(TEXT("ReducerMode")), FString(TEXT("ManagedRegion")));
			TestEqual(TEXT("Body.Notifies policy exports ApplyMode"), NotifiesPolicyJson->GetStringField(TEXT("ApplyMode")), FString(TEXT("RebuildArrayRegion")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageCreateTemplateTest,
	"AssetFactory.AssetDocument.AnimMontage.CreateTemplate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageCreateTemplateTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentService Service;
	FAssetDocumentTemplateRequest Request;
	Request.Class = TEXT("/Script/Engine.AnimMontage");
	Request.Target = TEXT("/Game/AssetDocumentTests/AM_Template");

	const FAssetDocumentResult Result = Service.CreateTemplate(Request);
	TestTrue(TEXT("CreateTemplate succeeds for AnimMontage class"), Result.IsSuccess());
	TestTrue(TEXT("CreateTemplate returns a payload"), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return false;
	}

	TestFalse(TEXT("AnimMontage template does not include AssetType"), Result.Payload->HasField(TEXT("AssetType")));
	TestEqual(TEXT("Template class is AnimMontage"), Result.Payload->GetStringField(TEXT("Class")), FString(TEXT("/Script/Engine.AnimMontage")));

	const TSharedPtr<FJsonObject>* Definitions = nullptr;
	TestTrue(TEXT("Template includes Definitions"), Result.Payload->TryGetObjectField(TEXT("Definitions"), Definitions));

	const TSharedPtr<FJsonObject>* Properties = nullptr;
	TestTrue(TEXT("Template includes Properties"), Result.Payload->TryGetObjectField(TEXT("Properties"), Properties));

	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template includes Body"), Result.Payload->TryGetObjectField(TEXT("Body"), Body));
	if (Body && Body->IsValid())
	{
		TestTrue(TEXT("Body includes Skeleton"), (*Body)->HasField(TEXT("Skeleton")));
		TestTrue(TEXT("Body includes PreviewMesh"), (*Body)->HasField(TEXT("PreviewMesh")));
		TestTrue(TEXT("Body initializes Skeleton as null"), (*Body)->HasTypedField<EJson::Null>(TEXT("Skeleton")));
		TestTrue(TEXT("Body initializes PreviewMesh as null"), (*Body)->HasTypedField<EJson::Null>(TEXT("PreviewMesh")));
		TestTrue(TEXT("Body initializes References as object"), (*Body)->HasTypedField<EJson::Object>(TEXT("References")));
		TestTrue(TEXT("Body initializes Preview as object"), (*Body)->HasTypedField<EJson::Object>(TEXT("Preview")));
		TestTrue(TEXT("Body initializes Sync as object"), (*Body)->HasTypedField<EJson::Object>(TEXT("Sync")));
		TestTrue(TEXT("Body initializes RootMotion as object"), (*Body)->HasTypedField<EJson::Object>(TEXT("RootMotion")));
		TestTrue(TEXT("Body initializes Metadata as array"), (*Body)->HasTypedField<EJson::Array>(TEXT("Metadata")));
		TestTrue(TEXT("Body initializes SectionMetadata as object"), (*Body)->HasTypedField<EJson::Object>(TEXT("SectionMetadata")));
		TestTrue(TEXT("Body initializes TimeStretch as object"), (*Body)->HasTypedField<EJson::Object>(TEXT("TimeStretch")));
		TestTrue(TEXT("Body initializes Curves as array"), (*Body)->HasTypedField<EJson::Array>(TEXT("Curves")));
		TestTrue(TEXT("Body includes SlotAnimTracks"), (*Body)->HasField(TEXT("SlotAnimTracks")));
		TestTrue(TEXT("Body includes CompositeSections"), (*Body)->HasField(TEXT("CompositeSections")));
		TestTrue(TEXT("Body includes Notifies"), (*Body)->HasField(TEXT("Notifies")));
		TestTrue(TEXT("Body includes NotifyStates"), (*Body)->HasField(TEXT("NotifyStates")));
		TestTrue(TEXT("Body includes Blend"), (*Body)->HasField(TEXT("Blend")));
		TestFalse(TEXT("Body does not include abbreviated Slots"), (*Body)->HasField(TEXT("Slots")));
		TestFalse(TEXT("Body does not include abbreviated Segments"), (*Body)->HasField(TEXT("Segments")));
		TestFalse(TEXT("Body does not include abbreviated Animation"), (*Body)->HasField(TEXT("Animation")));
		TestFalse(TEXT("Body does not include abbreviated Sections"), (*Body)->HasField(TEXT("Sections")));
		TestFalse(TEXT("Body does not include extract-only skipped metadata"), (*Body)->HasField(TEXT("_Skipped")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageValidateStructuredWithoutAssetTypeTest,
	"AssetFactory.AssetDocument.AnimMontage.ValidateStructuredWithoutAssetType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageValidateStructuredWithoutAssetTypeTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_Valid"));

	const FAssetDocumentResult Result = ValidateDocument(Document);
	TestTrue(TEXT("Structured AnimMontage document without AssetType validates"), Result.IsSuccess());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsUnknownBodyKeyTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsUnknownBodyKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsUnknownBodyKeyTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_Invalid"));
	Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("UnexpectedSection"), TArray<TSharedPtr<FJsonValue>>());

	const FAssetDocumentResult Result = ValidateDocument(Document);
	TestFalse(TEXT("Validate rejects unknown Body key"), Result.IsSuccess());
	TestTrue(TEXT("Validate reports UnknownBodyKey diagnostic"), HasDiagnosticMessage(Result, TEXT("/Body/UnexpectedSection"), TEXT("UnknownBodyKey"), TEXT("Unknown AnimMontage Body key")));

	TSharedPtr<FJsonObject> SkippedDocument = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_InvalidSkipped"));
	SkippedDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("_Skipped"), MakeShared<FJsonObject>());

	const FAssetDocumentResult SkippedResult = ValidateDocument(SkippedDocument);
	TestFalse(TEXT("Validate rejects authored _Skipped Body key"), SkippedResult.IsSuccess());
	TestTrue(TEXT("Validate reports UnknownBodyKey for _Skipped"), HasDiagnosticMessage(SkippedResult, TEXT("/Body/_Skipped"), TEXT("UnknownBodyKey"), TEXT("Unknown AnimMontage Body key")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsAbbreviatedSlotsTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsAbbreviatedSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsAbbreviatedSlotsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_InvalidSlots"));
	Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Slots"), TArray<TSharedPtr<FJsonValue>>());

	const FAssetDocumentResult Result = ValidateDocument(Document);
	TestFalse(TEXT("Validate rejects abbreviated Body.Slots"), Result.IsSuccess());
	TestTrue(TEXT("Validate reports DeprecatedBodyKey diagnostic"), HasDiagnosticMessage(Result, TEXT("/Body/Slots"), TEXT("DeprecatedBodyKey"), TEXT("Use SlotAnimTracks")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsBodyWithoutProfileTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsBodyWithoutProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsBodyWithoutProfileTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/DA_BodyUnsupported"));
	Document->SetStringField(TEXT("Class"), TEXT("/Script/AssetFactory.TestActorBase"));

	const FAssetDocumentResult Result = ValidateDocument(Document);
	TestFalse(TEXT("Validate rejects Body when class has no profile"), Result.IsSuccess());
	TestTrue(TEXT("Validate reports MissingProfile at Body"), AnimMontageHasDiagnostic(Result, TEXT("/Body"), TEXT("MissingProfile")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsNonObjectBodyTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsNonObjectBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsNonObjectBodyTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_InvalidBody"));
	Document->SetStringField(TEXT("Body"), TEXT("not an object"));

	const FAssetDocumentResult Result = ValidateDocument(Document);
	TestFalse(TEXT("Validate rejects non-object Body"), Result.IsSuccess());
	TestTrue(TEXT("Validate reports InvalidBodyType at Body"), AnimMontageHasDiagnostic(Result, TEXT("/Body"), TEXT("InvalidBodyType")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsInvalidBodySectionTypesTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsInvalidBodySectionTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsInvalidBodySectionTypesTest::RunTest(const FString& Parameters)
{
	const TArray<FString> ArraySections = {
		TEXT("Metadata"),
		TEXT("Curves"),
		TEXT("SlotAnimTracks"),
		TEXT("CompositeSections"),
		TEXT("Notifies"),
		TEXT("NotifyStates"),
	};

	for (const FString& Section : ArraySections)
	{
		TSharedPtr<FJsonObject> Document = MakeMontageDocument(FString::Printf(TEXT("/Game/AssetDocumentTests/AM_Invalid_%s"), *Section));
		Document->GetObjectField(TEXT("Body"))->SetStringField(Section, TEXT("not an array"));

		const FAssetDocumentResult Result = ValidateDocument(Document);
		TestFalse(FString::Printf(TEXT("Validate rejects non-array %s"), *Section), Result.IsSuccess());
		TestTrue(FString::Printf(TEXT("Validate reports InvalidBodySectionType for %s"), *Section), AnimMontageHasDiagnostic(Result, FString::Printf(TEXT("/Body/%s"), *Section), TEXT("InvalidBodySectionType")));
	}

	const TArray<FString> ObjectSections = {
		TEXT("References"),
		TEXT("Preview"),
		TEXT("Sync"),
		TEXT("RootMotion"),
		TEXT("SectionMetadata"),
		TEXT("TimeStretch"),
		TEXT("Blend"),
	};

	for (const FString& Section : ObjectSections)
	{
		TSharedPtr<FJsonObject> Document = MakeMontageDocument(FString::Printf(TEXT("/Game/AssetDocumentTests/AM_Invalid_%s"), *Section));
		Document->GetObjectField(TEXT("Body"))->SetStringField(Section, TEXT("not an object"));

		const FAssetDocumentResult Result = ValidateDocument(Document);
		TestFalse(FString::Printf(TEXT("Validate rejects non-object %s"), *Section), Result.IsSuccess());
		TestTrue(FString::Printf(TEXT("Validate reports InvalidBodySectionType for %s"), *Section), AnimMontageHasDiagnostic(Result, FString::Printf(TEXT("/Body/%s"), *Section), TEXT("InvalidBodySectionType")));
	}

	{
		TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_Invalid_SectionMetadataSectionArray"));
		TSharedRef<FJsonObject> SectionMetadata = MakeShared<FJsonObject>();
		SectionMetadata->SetStringField(TEXT("Start"), TEXT("not an array"));
		Document->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("SectionMetadata"), SectionMetadata);

		const FAssetDocumentResult Result = ValidateDocument(Document);
		TestFalse(TEXT("Validate rejects non-array SectionMetadata.Start"), Result.IsSuccess());
		TestTrue(
			TEXT("Validate reports InvalidBodySectionType for non-array SectionMetadata.Start"),
			AnimMontageHasDiagnostic(Result, TEXT("/Body/SectionMetadata/Start"), TEXT("InvalidBodySectionType")));
	}

	{
		TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_Invalid_SectionMetadataEntryObject"));
		TSharedRef<FJsonObject> SectionMetadata = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> InvalidSectionValues;
		InvalidSectionValues.Add(MakeShared<FJsonValueString>(TEXT("not an object")));
		SectionMetadata->SetArrayField(TEXT("Start"), InvalidSectionValues);
		Document->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("SectionMetadata"), SectionMetadata);

		const FAssetDocumentResult Result = ValidateDocument(Document);
		TestFalse(TEXT("Validate rejects non-object SectionMetadata.Start entry"), Result.IsSuccess());
		TestTrue(
			TEXT("Validate reports InvalidBodySectionType for non-object SectionMetadata.Start entry"),
			AnimMontageHasDiagnostic(Result, TEXT("/Body/SectionMetadata/Start/0"), TEXT("InvalidBodySectionType")));
	}

	for (const FString& Section : {FString(TEXT("Skeleton")), FString(TEXT("PreviewMesh"))})
	{
		TSharedPtr<FJsonObject> Document = MakeMontageDocument(FString::Printf(TEXT("/Game/AssetDocumentTests/AM_Invalid_%s"), *Section));
		Document->GetObjectField(TEXT("Body"))->SetStringField(Section, TEXT("not an object"));

		const FAssetDocumentResult Result = ValidateDocument(Document);
		TestFalse(FString::Printf(TEXT("Validate rejects string %s"), *Section), Result.IsSuccess());
		TestTrue(FString::Printf(TEXT("Validate reports InvalidBodySectionType for %s"), *Section), AnimMontageHasDiagnostic(Result, FString::Printf(TEXT("/Body/%s"), *Section), TEXT("InvalidBodySectionType")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRegisteredProfileSchemaTest,
	"AssetFactory.AssetDocument.AnimMontage.RegisteredProfileSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRegisteredProfileSchemaTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.GetSchema();
	TestTrue(TEXT("GetSchema succeeds"), Result.IsSuccess());
	TestTrue(TEXT("GetSchema returns payload"), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* RegisteredProfiles = nullptr;
	TestTrue(TEXT("Schema includes registered_profiles"), Result.Payload->TryGetArrayField(TEXT("registered_profiles"), RegisteredProfiles));
	if (!RegisteredProfiles)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* AssetDocumentTools = nullptr;
	TestTrue(TEXT("Schema includes asset_document_tools"), Result.Payload->TryGetArrayField(TEXT("asset_document_tools"), AssetDocumentTools));
	if (AssetDocumentTools)
	{
		TestTrue(TEXT("Schema includes inspect_asset_document_profile"), JsonArrayContainsString(*AssetDocumentTools, TEXT("inspect_asset_document_profile")));
		TestTrue(TEXT("Schema includes create_asset_document_template"), JsonArrayContainsString(*AssetDocumentTools, TEXT("create_asset_document_template")));
		TestTrue(TEXT("Schema includes diff_asset_document"), JsonArrayContainsString(*AssetDocumentTools, TEXT("diff_asset_document")));
		TestFalse(TEXT("Schema does not include asset-specific AnimMontage inspect tool"), JsonArrayContainsString(*AssetDocumentTools, TEXT("inspect_anim_montage_document")));
		TestFalse(TEXT("Schema does not include asset-specific AnimMontage create tool"), JsonArrayContainsString(*AssetDocumentTools, TEXT("create_anim_montage_document")));
		TestFalse(TEXT("Schema does not include asset-specific AnimMontage diff tool"), JsonArrayContainsString(*AssetDocumentTools, TEXT("diff_anim_montage_document")));
	}

	const TArray<TSharedPtr<FJsonValue>>* RegionPolicyPresets = nullptr;
	TestTrue(TEXT("Schema includes RegionPolicyPresets"), Result.Payload->TryGetArrayField(TEXT("RegionPolicyPresets"), RegionPolicyPresets));
	if (RegionPolicyPresets)
	{
		TestTrue(TEXT("Schema includes DefaultDiff region policy preset"), FindJsonObjectByStringField(*RegionPolicyPresets, TEXT("PresetName"), TEXT("DefaultDiff")).IsValid());
		TestTrue(TEXT("Schema includes ManagedRegion region policy preset"), FindJsonObjectByStringField(*RegionPolicyPresets, TEXT("PresetName"), TEXT("ManagedRegion")).IsValid());
		TestTrue(TEXT("Schema includes ExtensionHook region policy preset"), FindJsonObjectByStringField(*RegionPolicyPresets, TEXT("PresetName"), TEXT("ExtensionHook")).IsValid());
	}

	bool bFoundAnimMontageProfile = false;
	for (const TSharedPtr<FJsonValue>& Entry : *RegisteredProfiles)
	{
		const TSharedPtr<FJsonObject> EntryObject = Entry.IsValid() ? Entry->AsObject() : nullptr;
		if (!EntryObject.IsValid() || EntryObject->GetStringField(TEXT("Class")) != TEXT("/Script/Engine.AnimMontage"))
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* BodySections = nullptr;
		if (EntryObject->TryGetArrayField(TEXT("BodySections"), BodySections) && BodySections && HasExpectedBodySections(*BodySections))
		{
			TestFalse(TEXT("Schema registered profile omits extract-only skipped metadata"), JsonArrayContainsString(*BodySections, TEXT("_Skipped")));

			const TArray<TSharedPtr<FJsonValue>>* RegionPolicies = nullptr;
			TestTrue(TEXT("Schema registered AnimMontage profile includes RegionPolicies"), EntryObject->TryGetArrayField(TEXT("RegionPolicies"), RegionPolicies));
			if (RegionPolicies)
			{
				TestTrue(TEXT("Schema registered AnimMontage profile includes Body.Blend policy"), FindJsonObjectByStringField(*RegionPolicies, TEXT("RegionId"), TEXT("Body.Blend")).IsValid());
				TestTrue(TEXT("Schema registered AnimMontage profile includes Body.Notifies policy"), FindJsonObjectByStringField(*RegionPolicies, TEXT("RegionId"), TEXT("Body.Notifies")).IsValid());

				const TSharedPtr<FJsonObject> NotifyStatesPolicyJson = FindJsonObjectByStringField(*RegionPolicies, TEXT("RegionId"), TEXT("Body.NotifyStates"));
				TestTrue(TEXT("Schema registered AnimMontage profile includes Body.NotifyStates policy"), NotifyStatesPolicyJson.IsValid());
				if (NotifyStatesPolicyJson.IsValid())
				{
					TestNoAgentFacingOperationFields(this, TEXT("Schema registered policy"), NotifyStatesPolicyJson);
				}
			}
			bFoundAnimMontageProfile = true;
			break;
		}
	}

	TestTrue(TEXT("Schema registered_profiles includes AnimMontage body sections"), bFoundAnimMontageProfile);

	return true;
}

#endif
