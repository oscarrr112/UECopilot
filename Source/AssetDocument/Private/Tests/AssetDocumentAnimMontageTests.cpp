// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "AssetFactoryNamedAnimNotifyState.h"
#include "Profiles/AnimMontageAssetDocumentProfile.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Engine/SkeletalMesh.h"
#include "Generators/AnimSequenceGenerator.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"

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

TSharedPtr<FJsonObject> MakeAssetRef(const FString& Path)
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

TSharedPtr<FJsonObject> MakeClassRef(const FString& ClassPath)
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
	Body->SetObjectField(TEXT("Skeleton"), MakeAssetRef(TestSkeletonPath));
	Body->SetObjectField(TEXT("PreviewMesh"), MakeAssetRef(TestPreviewMeshPath));

	TSharedPtr<FJsonObject> Segment = MakeShared<FJsonObject>();
	Segment->SetObjectField(TEXT("AnimReference"), MakeAssetRef(AnimReferencePath));
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

int32 CountNotifyEventsWithObjectPrefix(const UAnimMontage* Montage, const FString& ObjectPrefix)
{
	int32 Count = 0;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		const UObject* NotifyObject = Event.Notify ? static_cast<const UObject*>(Event.Notify) : static_cast<const UObject*>(Event.NotifyStateClass);
		if (NotifyObject && NotifyObject->GetName().StartsWith(ObjectPrefix))
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

bool HasDiagnostic(const FAssetDocumentResult& Result, const FString& Path, const FString& Code)
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
		HasDiagnostic(Result, ExpectedPath, ExpectedCode));
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
		const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
		TestTrue(TEXT("Extract includes Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody));
		if (ExtractedBody && ExtractedBody->IsValid())
		{
			TestTrue(TEXT("Extract includes Skeleton"), (*ExtractedBody)->HasTypedField<EJson::Object>(TEXT("Skeleton")));
			TestTrue(TEXT("Extract includes PreviewMesh"), (*ExtractedBody)->HasTypedField<EJson::Object>(TEXT("PreviewMesh")));

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
	}

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
	TestEqual(TEXT("Managed notify is replaced without duplicate"), CountNotifyEventsWithObjectPrefix(Montage, TestManagedNotifyObjectPrefix), 1);
	TestEqual(TEXT("Managed notify state is replaced without duplicate"), CountNotifyEventsWithObjectPrefix(Montage, TestManagedNotifyStateObjectPrefix), 1);
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
	TestEqual(TEXT("Repeated apply keeps one managed notify"), CountNotifyEventsWithObjectPrefix(Montage, TestManagedNotifyObjectPrefix), 1);
	TestEqual(TEXT("Repeated apply keeps one managed notify state"), CountNotifyEventsWithObjectPrefix(Montage, TestManagedNotifyStateObjectPrefix), 1);

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
	TestEqual(TEXT("Reapply keeps one managed notify"), CountNotifyEventsWithObjectPrefix(Montage, TestManagedNotifyObjectPrefix), 1);
	TestEqual(TEXT("Reapply keeps one managed notify state"), CountNotifyEventsWithObjectPrefix(Montage, TestManagedNotifyStateObjectPrefix), 1);

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
			MakeClassRef(TestConcreteNotifyClassPath))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Notifies"), Notifies);
	}, TEXT("/Body/Notifies[0]/Object"), TEXT("InvalidNotifyObjectFragment"));
	bAllCasesPassed &= ExpectInvalidValidate(this, TEXT("NotifyState direct ClassRef"), AnimReferencePath, [](TSharedPtr<FJsonObject> Document)
	{
		TArray<TSharedPtr<FJsonValue>> NotifyStates;
		NotifyStates.Add(MakeShared<FJsonValueObject>(MakeNotifyStatePlacement(
			0.12,
			0.05,
			MakeClassRef(TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState")))));
		Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("NotifyStates"), NotifyStates);
	}, TEXT("/Body/NotifyStates[0]/Object"), TEXT("InvalidNotifyObjectFragment"));

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
	TestTrue(TEXT("Invalid patch reports AnimEndTime diagnostic"), HasDiagnostic(InvalidResult, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/AnimEndTime"), TEXT("InvalidAnimEndTime")));

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
	TestTrue(TEXT("Invalid patch reports NextSectionName diagnostic"), HasDiagnostic(InvalidResult, TEXT("/Body/CompositeSections/0/NextSectionName"), TEXT("InvalidNextSectionName")));

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
	TestTrue(TEXT("Invalid new apply reports AnimPlayRate diagnostic"), HasDiagnostic(Result, TEXT("/Body/SlotAnimTracks/0/AnimTrack/AnimSegments/0/AnimPlayRate"), TEXT("InvalidAnimPlayRate")));
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
	Definitions->SetObjectField(TEXT("SkeletonAsset"), MakeAssetRef(TestSkeletonPath));
	Definitions->SetObjectField(TEXT("PreviewMeshAsset"), MakeAssetRef(TestPreviewMeshPath));
	Definitions->SetObjectField(TEXT("AnimAsset"), MakeAssetRef(AnimSequence->GetPathName()));

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
	Definitions->SetObjectField(TEXT("SkeletonAsset"), MakeAssetRef(TestSkeletonPath));
	Definitions->SetObjectField(TEXT("PreviewMeshAsset"), MakeAssetRef(TestPreviewMeshPath));
	Definitions->SetObjectField(TEXT("AnimAsset"), MakeAssetRef(AnimSequence->GetPathName()));

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
			TestFalse(TEXT("Body.Blend policy does not expose patch field"), BlendPolicyJson->HasField(TEXT("patch")));
			TestFalse(TEXT("Body.Blend policy does not expose op field"), BlendPolicyJson->HasField(TEXT("op")));
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
	TestTrue(TEXT("Validate reports MissingProfile at Body"), HasDiagnostic(Result, TEXT("/Body"), TEXT("MissingProfile")));

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
	TestTrue(TEXT("Validate reports InvalidBodyType at Body"), HasDiagnostic(Result, TEXT("/Body"), TEXT("InvalidBodyType")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsInvalidBodySectionTypesTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsInvalidBodySectionTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsInvalidBodySectionTypesTest::RunTest(const FString& Parameters)
{
	const TArray<FString> ArraySections = {
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
		TestTrue(FString::Printf(TEXT("Validate reports InvalidBodySectionType for %s"), *Section), HasDiagnostic(Result, FString::Printf(TEXT("/Body/%s"), *Section), TEXT("InvalidBodySectionType")));
	}

	for (const FString& Section : {FString(TEXT("Skeleton")), FString(TEXT("PreviewMesh"))})
	{
		TSharedPtr<FJsonObject> Document = MakeMontageDocument(FString::Printf(TEXT("/Game/AssetDocumentTests/AM_Invalid_%s"), *Section));
		Document->GetObjectField(TEXT("Body"))->SetStringField(Section, TEXT("not an object"));

		const FAssetDocumentResult Result = ValidateDocument(Document);
		TestFalse(FString::Printf(TEXT("Validate rejects string %s"), *Section), Result.IsSuccess());
		TestTrue(FString::Printf(TEXT("Validate reports InvalidBodySectionType for %s"), *Section), HasDiagnostic(Result, FString::Printf(TEXT("/Body/%s"), *Section), TEXT("InvalidBodySectionType")));
	}

	{
		TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_InvalidBlend"));
		Document->GetObjectField(TEXT("Body"))->SetStringField(TEXT("Blend"), TEXT("not an object"));

		const FAssetDocumentResult Result = ValidateDocument(Document);
		TestFalse(TEXT("Validate rejects non-object Blend"), Result.IsSuccess());
		TestTrue(TEXT("Validate reports InvalidBodySectionType for Blend"), HasDiagnostic(Result, TEXT("/Body/Blend"), TEXT("InvalidBodySectionType")));
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
					TestFalse(TEXT("Schema registered policy does not expose patch field"), NotifyStatesPolicyJson->HasField(TEXT("patch")));
					TestFalse(TEXT("Schema registered policy does not expose op field"), NotifyStatesPolicyJson->HasField(TEXT("op")));
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
