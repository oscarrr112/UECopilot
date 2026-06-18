// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentModule.h"
#include "AssetDocumentService.h"
#include "Profiles/AnimSequenceAssetDocumentProfile.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Animation/Skeleton.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const TCHAR* TestSkeletonPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton");
const TCHAR* TestPreviewMeshPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP");

TSharedPtr<FJsonObject> FindObjectByStringField(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& FieldName, const FString& ExpectedValue)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			continue;
		}

		TSharedPtr<FJsonObject> Object = Value->AsObject();
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

bool JsonArrayContainsString(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& ExpectedValue)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (Value.IsValid() && Value->Type == EJson::String && Value->AsString() == ExpectedValue)
		{
			return true;
		}
	}

	return false;
}

const FAssetDocumentRegionPolicy* FindPolicyByRegionId(const TArray<FAssetDocumentRegionPolicy>& Policies, FName RegionId)
{
	for (const FAssetDocumentRegionPolicy& Policy : Policies)
	{
		if (Policy.RegionId == RegionId)
		{
			return &Policy;
		}
	}

	return nullptr;
}

bool PolicyContainsManagedPath(const FAssetDocumentRegionPolicy* Policy, const FString& ExpectedPath)
{
	return Policy && Policy->ManagedUePropertyPaths.Contains(ExpectedPath);
}

TSharedRef<FJsonObject> MakeAssetRef(const FString& Path)
{
	TSharedRef<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	Fragment->SetStringField(TEXT("Path"), Path);
	return Fragment;
}

TSharedRef<FJsonValue> MakeBodyValue(const TSharedRef<FJsonObject>& Body)
{
	return StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(Body));
}

TSharedRef<FJsonObject> MakeScalarRegionsBody(const FString& PreviewMeshPath)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();

	TSharedRef<FJsonObject> References = MakeShared<FJsonObject>();
	References->SetObjectField(TEXT("Skeleton"), MakeAssetRef(TestSkeletonPath));
	References->SetStringField(TEXT("RetargetSource"), TEXT("Default"));
	Body->SetObjectField(TEXT("References"), References);

	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetObjectField(TEXT("PreviewMesh"), MakeAssetRef(PreviewMeshPath));
	Body->SetObjectField(TEXT("Preview"), Preview);

	TSharedRef<FJsonObject> Playback = MakeShared<FJsonObject>();
	Playback->SetNumberField(TEXT("RateScale"), 1.75);
	Body->SetObjectField(TEXT("Playback"), Playback);

	TSharedRef<FJsonObject> Additive = MakeShared<FJsonObject>();
	Additive->SetStringField(TEXT("AdditiveAnimType"), TEXT("AAT_LocalSpaceBase"));
	Additive->SetStringField(TEXT("RefPoseType"), TEXT("ABPT_LocalAnimFrame"));
	Additive->SetNumberField(TEXT("RefFrameIndex"), 0);
	Additive->SetField(TEXT("RefPoseSeq"), MakeShared<FJsonValueNull>());
	Body->SetObjectField(TEXT("Additive"), Additive);

	TSharedRef<FJsonObject> RootMotion = MakeShared<FJsonObject>();
	RootMotion->SetBoolField(TEXT("bEnableRootMotion"), true);
	RootMotion->SetStringField(TEXT("RootMotionRootLock"), TEXT("Zero"));
	RootMotion->SetBoolField(TEXT("bForceRootLock"), true);
	RootMotion->SetBoolField(TEXT("bUseNormalizedRootMotionScale"), true);
	Body->SetObjectField(TEXT("RootMotion"), RootMotion);

	TSharedRef<FJsonObject> Compression = MakeShared<FJsonObject>();
	Compression->SetNumberField(TEXT("CompressionErrorThresholdScale"), 0.42);
	Compression->SetBoolField(TEXT("bDoNotOverrideCompression"), true);
	Body->SetObjectField(TEXT("Compression"), Compression);

	return Body;
}

UAnimSequence* CreateTransientSequence(const TCHAR* Name)
{
	UAnimSequence* Sequence = NewObject<UAnimSequence>(GetTransientPackage(), Name, RF_Transient);
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, TestSkeletonPath);
	if (Sequence && Skeleton)
	{
		Sequence->SetSkeleton(Skeleton);
	}
	return Sequence;
}

FAssetDocumentCapabilityContext MakeSequenceContext(UAnimSequence* Sequence)
{
	FAssetDocumentCapabilityContext Context;
	Context.Asset = Sequence;
	Context.AssetClass = UAnimSequence::StaticClass();
	return Context;
}

TSharedPtr<FJsonObject> GetRequiredObject(const TSharedRef<FJsonObject>& Object, const TCHAR* FieldName)
{
	const TSharedPtr<FJsonObject>* FieldObject = nullptr;
	Object->TryGetObjectField(FieldName, FieldObject);
	return FieldObject ? *FieldObject : nullptr;
}

TSharedPtr<FJsonObject> FindDiffEntryByPath(const TArray<TSharedPtr<FJsonValue>>& Entries, const FString& ExpectedPath)
{
	for (const TSharedPtr<FJsonValue>& EntryValue : Entries)
	{
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = EntryValue->AsObject();
		if (!Entry.IsValid())
		{
			continue;
		}

		FString Path;
		if (Entry->TryGetStringField(TEXT("path"), Path) && Path == ExpectedPath)
		{
			return Entry;
		}
	}

	return nullptr;
}

bool HasDiagnostic(const FAssetDocumentCapabilityResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

TSharedRef<FJsonObject> MakePlaybackRateBody(double RateScale)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Playback = MakeShared<FJsonObject>();
	Playback->SetNumberField(TEXT("RateScale"), RateScale);
	Body->SetObjectField(TEXT("Playback"), Playback);
	return Body;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimSequenceProfileShapeTest,
	"AssetFactory.AssetDocument.AnimSequence.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimSequenceProfileShapeTest::RunTest(const FString&)
{
	const FAnimSequenceAssetDocumentProfile Profile;
	TestEqual(TEXT("Exact class is UAnimSequence"), Profile.GetExactClass(), UAnimSequence::StaticClass());

	const TArray<FName> BodyKeys = Profile.GetBodyKeys();
	const TArray<FName> ExpectedKeys = {
		TEXT("References"),
		TEXT("Preview"),
		TEXT("Playback"),
		TEXT("Additive"),
		TEXT("RootMotion"),
		TEXT("Compression"),
		TEXT("Curves"),
		TEXT("Notifies"),
		TEXT("NotifyStates"),
		TEXT("NotifyTracks"),
		TEXT("SyncMarkers"),
		TEXT("Metadata"),
		TEXT("AssetUserData"),
	};
	for (const FName& ExpectedKey : ExpectedKeys)
	{
		TestTrue(FString::Printf(TEXT("Body key %s exists"), *ExpectedKey.ToString()), BodyKeys.Contains(ExpectedKey));
		TestNotNull(FString::Printf(TEXT("Body key %s resolves adapter"), *ExpectedKey.ToString()), Profile.ResolveBodyAdapter(ExpectedKey));
	}
	TestNotNull(TEXT("Body root resolves adapter"), Profile.ResolveBodyAdapter(TEXT("Body")));

	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	for (const FName& ExpectedKey : ExpectedKeys)
	{
		const FName RegionId(*FString::Printf(TEXT("Body.%s"), *ExpectedKey.ToString()));
		TestNotNull(FString::Printf(TEXT("Region policy %s exists"), *RegionId.ToString()), FindPolicyByRegionId(Policies, RegionId));
	}

	const FAssetDocumentRegionPolicy* PlaybackPolicy = FindPolicyByRegionId(Policies, TEXT("Body.Playback"));
	if (PlaybackPolicy)
	{
		TestTrue(TEXT("Body.Playback uses DefaultDiff reducer"), PlaybackPolicy->ReducerMode == EAssetDocumentReducerMode::DefaultDiff);
		TestTrue(TEXT("Body.Playback uses SetProperty apply mode"), PlaybackPolicy->ApplyMode == EAssetDocumentApplyMode::SetProperty);
		TestTrue(TEXT("Body.Playback owns RateScale"), PolicyContainsManagedPath(PlaybackPolicy, TEXT("RateScale")));
	}

	const FAssetDocumentRegionPolicy* CurvesPolicy = FindPolicyByRegionId(Policies, TEXT("Body.Curves"));
	if (CurvesPolicy)
	{
		TestTrue(TEXT("Body.Curves uses ManagedRegion reducer"), CurvesPolicy->ReducerMode == EAssetDocumentReducerMode::ManagedRegion);
		TestTrue(TEXT("Body.Curves uses rebuild-array apply mode"), CurvesPolicy->ApplyMode == EAssetDocumentApplyMode::RebuildArrayRegion);
		TestTrue(TEXT("Body.Curves owns RawCurveData"), PolicyContainsManagedPath(CurvesPolicy, TEXT("RawCurveData")));
	}

	const FAssetDocumentRegionPolicy* NotifiesPolicy = FindPolicyByRegionId(Policies, TEXT("Body.Notifies"));
	if (NotifiesPolicy)
	{
		TestTrue(TEXT("Body.Notifies intentionally owns Notifies"), PolicyContainsManagedPath(NotifiesPolicy, TEXT("Notifies")));
	}

	const FAssetDocumentRegionPolicy* NotifyStatesPolicy = FindPolicyByRegionId(Policies, TEXT("Body.NotifyStates"));
	if (NotifyStatesPolicy)
	{
		TestTrue(TEXT("Body.NotifyStates intentionally owns Notifies"), PolicyContainsManagedPath(NotifyStatesPolicy, TEXT("Notifies")));
	}

	const IAssetDocumentCapability* BodyAdapter = Profile.ResolveBodyAdapter(TEXT("Body"));
	if (BodyAdapter)
	{
		FAssetDocumentCapabilityContext Context;

		TSharedRef<FJsonObject> UnknownBody = MakeShared<FJsonObject>();
		UnknownBody->SetObjectField(TEXT("UnexpectedSection"), MakeShared<FJsonObject>());
		const FAssetDocumentCapabilityResult UnknownResult = BodyAdapter->Validate(Context, MakeShared<FJsonValueObject>(UnknownBody));
		TestFalse(TEXT("Validate rejects unknown Body key"), UnknownResult.bSuccess);

		TSharedRef<FJsonObject> SkippedBody = MakeShared<FJsonObject>();
		SkippedBody->SetObjectField(TEXT("_Skipped"), MakeShared<FJsonObject>());
		const FAssetDocumentCapabilityResult SkippedResult = BodyAdapter->Validate(Context, MakeShared<FJsonValueObject>(SkippedBody));
		TestFalse(TEXT("Validate rejects authored _Skipped Body key"), SkippedResult.bSuccess);

		TSharedRef<FJsonObject> ValidEmptyBody = MakeShared<FJsonObject>();
		const TSharedRef<FJsonValue> ValidEmptyBodyJson = MakeShared<FJsonValueObject>(ValidEmptyBody);
		const FAssetDocumentCapabilityResult ValidateResult = BodyAdapter->Validate(Context, ValidEmptyBodyJson);
		TestTrue(TEXT("Validate accepts empty authored Body shape"), ValidateResult.bSuccess);

		FAssetDocumentCapabilityContext PreflightContext;
		const FAssetDocumentCapabilityResult PreflightResult = BodyAdapter->Preflight(PreflightContext, ValidEmptyBodyJson);
		TestTrue(TEXT("Preflight accepts empty supported Body shape"), PreflightResult.bSuccess);

		FAssetDocumentCapabilityContext ExtractContext;
		ExtractContext.Asset = NewObject<UAnimSequence>(GetTransientPackage());
		ExtractContext.AssetClass = UAnimSequence::StaticClass();
		TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
		const FAssetDocumentCapabilityResult ExtractResult = BodyAdapter->Extract(ExtractContext, ExtractedBody);
		TestTrue(TEXT("Extract succeeds with supported scalar Body regions"), ExtractResult.bSuccess);
		TestTrue(TEXT("Extract writes References object"), ExtractedBody->HasTypedField<EJson::Object>(TEXT("References")));
		TestTrue(TEXT("Extract writes Playback object"), ExtractedBody->HasTypedField<EJson::Object>(TEXT("Playback")));
		TestTrue(TEXT("Extract writes RootMotion object"), ExtractedBody->HasTypedField<EJson::Object>(TEXT("RootMotion")));
		TestTrue(TEXT("Extract writes Compression object"), ExtractedBody->HasTypedField<EJson::Object>(TEXT("Compression")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimSequenceScalarRegionsTest,
	"AssetFactory.AssetDocument.AnimSequence.ScalarRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimSequenceScalarRegionsTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("No Movie Scene found for SequencerDataModel"), EAutomationExpectedErrorFlags::Contains, 52);
	AddExpectedError(TEXT("Unable to find Control Rig Section"), EAutomationExpectedErrorFlags::Contains, 2);

	FAnimSequenceAssetDocumentCapability Capability;
	UAnimSequence* Sequence = CreateTransientSequence(TEXT("AssetDocumentAnimSequenceScalarRegions"));
	USkeletalMesh* PreviewMesh = LoadObject<USkeletalMesh>(nullptr, TestPreviewMeshPath);
	TestNotNull(TEXT("Fixture creates transient AnimSequence"), Sequence);
	TestNotNull(TEXT("Tutorial skeleton fixture is available"), Sequence ? Sequence->GetSkeleton() : nullptr);
	TestNotNull(TEXT("Tutorial preview mesh fixture is available"), PreviewMesh);
	if (!Sequence || !Sequence->GetSkeleton() || !PreviewMesh)
	{
		return true;
	}

	Sequence->RateScale = 1.0f;
	Sequence->AdditiveAnimType = AAT_None;
	Sequence->RefPoseType = ABPT_None;
	Sequence->RefFrameIndex = 0;
	Sequence->bEnableRootMotion = false;
	Sequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
	Sequence->bForceRootLock = false;
	Sequence->bUseNormalizedRootMotionScale = false;
	Sequence->CompressionErrorThresholdScale = 1.0f;
	Sequence->bDoNotOverrideCompression = false;
	Sequence->SetPreviewMesh(nullptr, false);

	FAssetDocumentCapabilityContext Context = MakeSequenceContext(Sequence);
	TSharedRef<FJsonObject> InitialExtract = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult InitialExtractResult = Capability.Extract(Context, InitialExtract);
	TestTrue(TEXT("Extract succeeds for scalar/reference regions"), InitialExtractResult.bSuccess);
	TestTrue(TEXT("Extract emits References"), InitialExtract->HasTypedField<EJson::Object>(TEXT("References")));
	TestTrue(TEXT("Extract emits Playback"), InitialExtract->HasTypedField<EJson::Object>(TEXT("Playback")));
	const TSharedPtr<FJsonObject> InitialPlayback = GetRequiredObject(InitialExtract, TEXT("Playback"));
	TestFalse(TEXT("Extract does not expose derived PlayLength as authored Playback field"), InitialPlayback.IsValid() && InitialPlayback->HasField(TEXT("PlayLength")));

	const FAssetDocumentCapabilityResult ApplyResult = Capability.Apply(Context, MakeBodyValue(MakeScalarRegionsBody(TestPreviewMeshPath)));
	TestTrue(TEXT("Apply succeeds after full scalar parse"), ApplyResult.bSuccess);
	TestEqual(TEXT("Apply updates Preview.PreviewMesh"), Sequence->GetPreviewMesh(), PreviewMesh);
	TestEqual(TEXT("Apply updates Playback.RateScale"), Sequence->RateScale, 1.75f);
	TestEqual(TEXT("Apply updates Additive.AdditiveAnimType"), static_cast<EAdditiveAnimationType>(Sequence->AdditiveAnimType), AAT_LocalSpaceBase);
	TestEqual(TEXT("Apply updates Additive.RefPoseType"), static_cast<EAdditiveBasePoseType>(Sequence->RefPoseType), ABPT_LocalAnimFrame);
	TestEqual(TEXT("Apply updates Additive.RefFrameIndex"), Sequence->RefFrameIndex, 0);
	TestNull(TEXT("Apply accepts null Additive.RefPoseSeq"), Sequence->RefPoseSeq);
	TestTrue(TEXT("Apply updates RootMotion.bEnableRootMotion"), Sequence->bEnableRootMotion);
	TestEqual(TEXT("Apply updates RootMotion.RootMotionRootLock"), static_cast<ERootMotionRootLock::Type>(Sequence->RootMotionRootLock), ERootMotionRootLock::Zero);
	TestTrue(TEXT("Apply updates RootMotion.bForceRootLock"), Sequence->bForceRootLock);
	TestTrue(TEXT("Apply updates RootMotion.bUseNormalizedRootMotionScale"), Sequence->bUseNormalizedRootMotionScale);
	TestEqual(TEXT("Apply updates Compression.CompressionErrorThresholdScale"), Sequence->CompressionErrorThresholdScale, 0.42f);
	TestTrue(TEXT("Apply updates Compression.bDoNotOverrideCompression"), !!Sequence->bDoNotOverrideCompression);

	TSharedRef<FJsonObject> Extracted = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, Extracted);
	TestTrue(TEXT("Extract succeeds after apply"), ExtractResult.bSuccess);
	const TSharedPtr<FJsonObject> ExtractedReferences = GetRequiredObject(Extracted, TEXT("References"));
	const TSharedPtr<FJsonObject> ExtractedPreview = GetRequiredObject(Extracted, TEXT("Preview"));
	const TSharedPtr<FJsonObject> ExtractedPlayback = GetRequiredObject(Extracted, TEXT("Playback"));
	const TSharedPtr<FJsonObject> ExtractedAdditive = GetRequiredObject(Extracted, TEXT("Additive"));
	const TSharedPtr<FJsonObject> ExtractedRootMotion = GetRequiredObject(Extracted, TEXT("RootMotion"));
	const TSharedPtr<FJsonObject> ExtractedCompression = GetRequiredObject(Extracted, TEXT("Compression"));
	TestTrue(TEXT("Extract outputs References.Skeleton"), ExtractedReferences.IsValid() && ExtractedReferences->HasTypedField<EJson::Object>(TEXT("Skeleton")));
	TestTrue(TEXT("Extract outputs References.RetargetSource"), ExtractedReferences.IsValid() && ExtractedReferences->HasTypedField<EJson::String>(TEXT("RetargetSource")));
	TestTrue(TEXT("Extract outputs Preview.PreviewMesh"), ExtractedPreview.IsValid() && ExtractedPreview->HasTypedField<EJson::Object>(TEXT("PreviewMesh")));
	if (ExtractedPlayback.IsValid())
	{
		TestEqual(TEXT("Extract outputs Playback.RateScale"), ExtractedPlayback->GetNumberField(TEXT("RateScale")), 1.75);
	}
	if (ExtractedAdditive.IsValid())
	{
		TestEqual(TEXT("Extract outputs Additive.AdditiveAnimType"), ExtractedAdditive->GetStringField(TEXT("AdditiveAnimType")), FString(TEXT("AAT_LocalSpaceBase")));
	}
	if (ExtractedRootMotion.IsValid())
	{
		TestEqual(TEXT("Extract outputs RootMotion.RootMotionRootLock"), ExtractedRootMotion->GetStringField(TEXT("RootMotionRootLock")), FString(TEXT("Zero")));
	}
	if (ExtractedCompression.IsValid())
	{
		TestTrue(TEXT("Extract outputs Compression.bDoNotOverrideCompression"), ExtractedCompression->GetBoolField(TEXT("bDoNotOverrideCompression")));
	}

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult = Capability.Diff(Context, MakeBodyValue(MakePlaybackRateBody(2.0)), DiffEntries);
	TestTrue(TEXT("Diff succeeds for authored scalar regions"), DiffResult.bSuccess);
	const TSharedPtr<FJsonObject> PlaybackDiff = FindDiffEntryByPath(DiffEntries, TEXT("/Body/Playback"));
	TestTrue(TEXT("Diff reports Playback path"), PlaybackDiff.IsValid());
	if (PlaybackDiff.IsValid())
	{
		TestEqual(TEXT("Diff marks Playback changed before apply"), PlaybackDiff->GetStringField(TEXT("status")), FString(TEXT("changed")));
		TSharedPtr<FJsonObject> CurrentPlayback = PlaybackDiff->GetObjectField(TEXT("current"));
		TSharedPtr<FJsonObject> DesiredPlayback = PlaybackDiff->GetObjectField(TEXT("desired"));
		TestTrue(TEXT("Diff current Playback is object"), CurrentPlayback.IsValid());
		TestTrue(TEXT("Diff desired Playback is object"), DesiredPlayback.IsValid());
		if (CurrentPlayback.IsValid() && DesiredPlayback.IsValid())
		{
			TestEqual(TEXT("Diff current Playback.RateScale is before value"), CurrentPlayback->GetNumberField(TEXT("RateScale")), 1.75);
			TestEqual(TEXT("Diff desired Playback.RateScale is authored value"), DesiredPlayback->GetNumberField(TEXT("RateScale")), 2.0);
		}
	}

	const FAssetDocumentCapabilityResult ApplyPlaybackForDiffResult = Capability.Apply(Context, MakeBodyValue(MakePlaybackRateBody(2.0)));
	TestTrue(TEXT("Apply playback-only body for unchanged diff check"), ApplyPlaybackForDiffResult.bSuccess);
	DiffEntries.Reset();
	const FAssetDocumentCapabilityResult UnchangedDiffResult = Capability.Diff(Context, MakeBodyValue(MakePlaybackRateBody(2.0)), DiffEntries);
	TestTrue(TEXT("Diff succeeds after authored value already applied"), UnchangedDiffResult.bSuccess);
	const TSharedPtr<FJsonObject> UnchangedPlaybackDiff = FindDiffEntryByPath(DiffEntries, TEXT("/Body/Playback"));
	TestTrue(TEXT("Diff reports Playback path after apply"), UnchangedPlaybackDiff.IsValid());
	if (UnchangedPlaybackDiff.IsValid())
	{
		TestEqual(TEXT("Diff marks Playback unchanged after apply"), UnchangedPlaybackDiff->GetStringField(TEXT("status")), FString(TEXT("unchanged")));
	}

	const TArray<FString> UnsupportedTopLevelFields = {
		TEXT("Import"),
		TEXT("RawTracks"),
		TEXT("CompressedData"),
	};
	for (const FString& FieldName : UnsupportedTopLevelFields)
	{
		TSharedRef<FJsonObject> InvalidBody = MakeShared<FJsonObject>();
		InvalidBody->SetObjectField(FieldName, MakeShared<FJsonObject>());
		const FAssetDocumentCapabilityResult InvalidResult = Capability.Validate(Context, MakeBodyValue(InvalidBody));
		TestFalse(FString::Printf(TEXT("Validate rejects Body.%s"), *FieldName), InvalidResult.bSuccess);
	}

	const TArray<FString> NotImplementedRegions = {
		TEXT("Curves"),
		TEXT("Metadata"),
	};
	for (const FString& RegionName : NotImplementedRegions)
	{
		TSharedRef<FJsonObject> InvalidBody = MakePlaybackRateBody(3.0);
		InvalidBody->SetArrayField(RegionName, TArray<TSharedPtr<FJsonValue>>());
		const FString RegionPath = FString::Printf(TEXT("/Body/%s"), *RegionName);
		const FAssetDocumentCapabilityResult ValidateUnsupportedRegionResult = Capability.Validate(Context, MakeBodyValue(InvalidBody));
		TestFalse(FString::Printf(TEXT("Validate rejects not-yet-implemented Body.%s"), *RegionName), ValidateUnsupportedRegionResult.bSuccess);
		TestTrue(FString::Printf(TEXT("Validate diagnostic points at Body.%s"), *RegionName), HasDiagnostic(ValidateUnsupportedRegionResult, RegionPath, TEXT("NotImplementedBodyRegion")));
		const FAssetDocumentCapabilityResult PreflightUnsupportedRegionResult = Capability.Preflight(Context, MakeBodyValue(InvalidBody));
		TestFalse(FString::Printf(TEXT("Preflight rejects not-yet-implemented Body.%s"), *RegionName), PreflightUnsupportedRegionResult.bSuccess);
		const float RateScaleBeforeUnsupportedRegion = Sequence->RateScale;
		const FAssetDocumentCapabilityResult ApplyUnsupportedRegionResult = Capability.Apply(Context, MakeBodyValue(InvalidBody));
		TestFalse(FString::Printf(TEXT("Apply rejects not-yet-implemented Body.%s"), *RegionName), ApplyUnsupportedRegionResult.bSuccess);
		TestEqual(FString::Printf(TEXT("Not-yet-implemented Body.%s does not partially mutate RateScale"), *RegionName), Sequence->RateScale, RateScaleBeforeUnsupportedRegion);
	}

	TSharedRef<FJsonObject> UnsupportedDiffBody = MakePlaybackRateBody(3.0);
	UnsupportedDiffBody->SetArrayField(TEXT("Notifies"), TArray<TSharedPtr<FJsonValue>>());
	TArray<TSharedPtr<FJsonValue>> UnsupportedDiffEntries;
	const FAssetDocumentCapabilityResult UnsupportedDiffResult = Capability.Diff(Context, MakeBodyValue(UnsupportedDiffBody), UnsupportedDiffEntries);
	TestFalse(TEXT("Diff rejects not-yet-implemented Body.Notifies"), UnsupportedDiffResult.bSuccess);
	TestTrue(TEXT("Diff diagnostic points at Body.Notifies"), HasDiagnostic(UnsupportedDiffResult, TEXT("/Body/Notifies"), TEXT("NotImplementedBodyRegion")));

	const TArray<TPair<FString, FString>> RejectedImplementedFields = {
		TPair<FString, FString>(TEXT("Playback"), TEXT("FrameRate")),
		TPair<FString, FString>(TEXT("Compression"), TEXT("VariableFrameStrippingSettings")),
		TPair<FString, FString>(TEXT("References"), TEXT("RetargetSourceAssetReferencePose")),
		TPair<FString, FString>(TEXT("RootMotion"), TEXT("bEnableRootMotoin")),
	};
	for (const TPair<FString, FString>& RejectedField : RejectedImplementedFields)
	{
		TSharedRef<FJsonObject> InvalidBody = MakePlaybackRateBody(3.25);
		TSharedRef<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetStringField(RejectedField.Value, TEXT("unexpected"));
		InvalidBody->SetObjectField(RejectedField.Key, Section);
		const FString FieldPath = FString::Printf(TEXT("/Body/%s/%s"), *RejectedField.Key, *RejectedField.Value);
		const FAssetDocumentCapabilityResult InvalidFieldValidateResult = Capability.Validate(Context, MakeBodyValue(InvalidBody));
		TestFalse(FString::Printf(TEXT("Validate rejects Body.%s.%s"), *RejectedField.Key, *RejectedField.Value), InvalidFieldValidateResult.bSuccess);
		TestTrue(FString::Printf(TEXT("Validate diagnostic points at Body.%s.%s"), *RejectedField.Key, *RejectedField.Value), HasDiagnostic(InvalidFieldValidateResult, FieldPath, TEXT("UnsupportedAuthoredField")));
		const float RateScaleBeforeInvalidField = Sequence->RateScale;
		const FAssetDocumentCapabilityResult InvalidFieldApplyResult = Capability.Apply(Context, MakeBodyValue(InvalidBody));
		TestFalse(FString::Printf(TEXT("Apply rejects Body.%s.%s"), *RejectedField.Key, *RejectedField.Value), InvalidFieldApplyResult.bSuccess);
		TestEqual(FString::Printf(TEXT("Rejected Body.%s.%s does not partially mutate RateScale"), *RejectedField.Key, *RejectedField.Value), Sequence->RateScale, RateScaleBeforeInvalidField);
	}

	TSharedRef<FJsonObject> InvalidPlaybackBody = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> InvalidPlayback = MakeShared<FJsonObject>();
	InvalidPlayback->SetNumberField(TEXT("PlayLength"), 3.0);
	InvalidPlaybackBody->SetObjectField(TEXT("Playback"), InvalidPlayback);
	TestFalse(TEXT("Validate rejects Body.Playback.PlayLength"), Capability.Validate(Context, MakeBodyValue(InvalidPlaybackBody)).bSuccess);
	InvalidPlayback->RemoveField(TEXT("PlayLength"));
	InvalidPlayback->SetNumberField(TEXT("NumberOfSampledKeys"), 30);
	TestFalse(TEXT("Validate rejects Body.Playback.NumberOfSampledKeys"), Capability.Validate(Context, MakeBodyValue(InvalidPlaybackBody)).bSuccess);
	InvalidPlayback->RemoveField(TEXT("NumberOfSampledKeys"));
	InvalidPlayback->SetNumberField(TEXT("SamplingFrameRate"), 30);
	TestFalse(TEXT("Validate rejects Body.Playback.SamplingFrameRate"), Capability.Validate(Context, MakeBodyValue(InvalidPlaybackBody)).bSuccess);

	TSharedRef<FJsonObject> NullSkeletonBody = MakePlaybackRateBody(4.0);
	TSharedRef<FJsonObject> NullSkeletonReferences = MakeShared<FJsonObject>();
	NullSkeletonReferences->SetField(TEXT("Skeleton"), MakeShared<FJsonValueNull>());
	NullSkeletonBody->SetObjectField(TEXT("References"), NullSkeletonReferences);
	const float RateScaleBeforeNullSkeleton = Sequence->RateScale;
	const FAssetDocumentCapabilityResult NullSkeletonApplyResult = Capability.Apply(Context, MakeBodyValue(NullSkeletonBody));
	TestFalse(TEXT("Apply rejects authored References.Skeleton null"), NullSkeletonApplyResult.bSuccess);
	TestTrue(TEXT("Skeleton null diagnostic points at References.Skeleton"), HasDiagnostic(NullSkeletonApplyResult, TEXT("/Body/References/Skeleton"), TEXT("NullNotAllowed")));
	TestEqual(TEXT("Skeleton null does not partially mutate RateScale"), Sequence->RateScale, RateScaleBeforeNullSkeleton);

	TSharedRef<FJsonObject> InvalidRefFrameBody = MakePlaybackRateBody(4.25);
	TSharedRef<FJsonObject> InvalidRefFrameAdditive = MakeShared<FJsonObject>();
	InvalidRefFrameAdditive->SetNumberField(TEXT("RefFrameIndex"), 999999);
	InvalidRefFrameBody->SetObjectField(TEXT("Additive"), InvalidRefFrameAdditive);
	const float RateScaleBeforeInvalidRefFrame = Sequence->RateScale;
	const FAssetDocumentCapabilityResult InvalidRefFrameApplyResult = Capability.Apply(Context, MakeBodyValue(InvalidRefFrameBody));
	TestFalse(TEXT("Apply rejects out-of-range Additive.RefFrameIndex"), InvalidRefFrameApplyResult.bSuccess);
	TestTrue(TEXT("RefFrameIndex diagnostic points at Additive.RefFrameIndex"), HasDiagnostic(InvalidRefFrameApplyResult, TEXT("/Body/Additive/RefFrameIndex"), TEXT("InvalidRefFrameIndex")));
	TestEqual(TEXT("Invalid RefFrameIndex does not partially mutate RateScale"), Sequence->RateScale, RateScaleBeforeInvalidRefFrame);

	UAnimSequence* IncompatiblePreviewSequence = NewObject<UAnimSequence>(GetTransientPackage(), TEXT("AssetDocumentAnimSequenceIncompatiblePreview"), RF_Transient);
	IncompatiblePreviewSequence->SetSkeleton(NewObject<USkeleton>(GetTransientPackage(), TEXT("AssetDocumentAnimSequenceOtherSkeleton"), RF_Transient));
	FAssetDocumentCapabilityContext IncompatiblePreviewContext = MakeSequenceContext(IncompatiblePreviewSequence);
	TSharedRef<FJsonObject> IncompatiblePreviewBody = MakePlaybackRateBody(4.5);
	TSharedRef<FJsonObject> IncompatiblePreview = MakeShared<FJsonObject>();
	IncompatiblePreview->SetObjectField(TEXT("PreviewMesh"), MakeAssetRef(TestPreviewMeshPath));
	IncompatiblePreviewBody->SetObjectField(TEXT("Preview"), IncompatiblePreview);
	const FAssetDocumentCapabilityResult IncompatiblePreviewApplyResult = Capability.Apply(IncompatiblePreviewContext, MakeBodyValue(IncompatiblePreviewBody));
	TestFalse(TEXT("Apply rejects incompatible Preview.PreviewMesh skeleton"), IncompatiblePreviewApplyResult.bSuccess);
	TestTrue(TEXT("PreviewMesh skeleton diagnostic points at Preview.PreviewMesh"), HasDiagnostic(IncompatiblePreviewApplyResult, TEXT("/Body/Preview/PreviewMesh"), TEXT("PreviewMeshSkeletonMismatch")));
	TestEqual(TEXT("Incompatible PreviewMesh does not partially mutate RateScale"), IncompatiblePreviewSequence->RateScale, 1.0f);
	TestNull(TEXT("Incompatible PreviewMesh does not apply preview mesh"), IncompatiblePreviewSequence->GetPreviewMesh());

	const float RateScaleBeforeInvalid = Sequence->RateScale;
	const bool bEnableRootMotionBeforeInvalid = Sequence->bEnableRootMotion;
	TSharedRef<FJsonObject> InvalidBody = MakeScalarRegionsBody(TestPreviewMeshPath);
	InvalidBody->GetObjectField(TEXT("Playback"))->SetNumberField(TEXT("RateScale"), 2.25);
	InvalidBody->GetObjectField(TEXT("Preview"))->SetStringField(TEXT("PreviewMesh"), TEXT("not an asset ref object"));
	const FAssetDocumentCapabilityResult InvalidTypeApplyResult = Capability.Apply(Context, MakeBodyValue(InvalidBody));
	TestFalse(TEXT("Apply rejects invalid reference value type"), InvalidTypeApplyResult.bSuccess);
	TestEqual(TEXT("Invalid value type does not partially mutate RateScale"), Sequence->RateScale, RateScaleBeforeInvalid);
	TestEqual(TEXT("Invalid value type does not partially mutate root motion"), Sequence->bEnableRootMotion, bEnableRootMotionBeforeInvalid);

	TSharedRef<FJsonObject> MissingRefBody = MakeScalarRegionsBody(TEXT("/Game/AssetDocumentTests/MissingMesh.MissingMesh"));
	MissingRefBody->GetObjectField(TEXT("Playback"))->SetNumberField(TEXT("RateScale"), 2.5);
	const FAssetDocumentCapabilityResult MissingRefApplyResult = Capability.Apply(Context, MakeBodyValue(MissingRefBody));
	TestFalse(TEXT("Apply rejects invalid asset reference"), MissingRefApplyResult.bSuccess);
	TestEqual(TEXT("Invalid asset reference does not partially mutate RateScale"), Sequence->RateScale, RateScaleBeforeInvalid);

	TSharedRef<FJsonObject> InvalidClassBody = MakeScalarRegionsBody(TestPreviewMeshPath);
	InvalidClassBody->GetObjectField(TEXT("Playback"))->SetNumberField(TEXT("RateScale"), 2.75);
	InvalidClassBody->GetObjectField(TEXT("Preview"))->SetObjectField(TEXT("PreviewMesh"), MakeAssetRef(TestSkeletonPath));
	const FAssetDocumentCapabilityResult InvalidClassApplyResult = Capability.Apply(Context, MakeBodyValue(InvalidClassBody));
	TestFalse(TEXT("Apply rejects asset reference that resolves to wrong class"), InvalidClassApplyResult.bSuccess);
	TestEqual(TEXT("Invalid reference class does not partially mutate RateScale"), Sequence->RateScale, RateScaleBeforeInvalid);

	TSharedRef<FJsonObject> InvalidNumberBody = MakeScalarRegionsBody(TestPreviewMeshPath);
	InvalidNumberBody->GetObjectField(TEXT("Playback"))->SetStringField(TEXT("RateScale"), TEXT("fast"));
	TestFalse(TEXT("Apply rejects invalid numeric value"), Capability.Apply(Context, MakeBodyValue(InvalidNumberBody)).bSuccess);
	TestEqual(TEXT("Invalid numeric value does not partially mutate RateScale"), Sequence->RateScale, RateScaleBeforeInvalid);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimSequenceRegisteredProfileSchemaTest,
	"AssetFactory.AssetDocument.AnimSequence.RegisteredProfileSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimSequenceRegisteredProfileSchemaTest::RunTest(const FString&)
{
	FAssetDocumentModule::Get();
	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.GetSchema();
	TestTrue(TEXT("Schema succeeds"), Result.IsSuccess());
	TestTrue(TEXT("Schema has payload"), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* RegisteredProfiles = nullptr;
	TestTrue(TEXT("Schema includes registered_profiles"), Result.Payload->TryGetArrayField(TEXT("registered_profiles"), RegisteredProfiles));
	if (!RegisteredProfiles)
	{
		return true;
	}

	TSharedPtr<FJsonObject> AnimSequenceEntry = FindObjectByStringField(*RegisteredProfiles, TEXT("Class"), TEXT("/Script/Engine.AnimSequence"));
	TestTrue(TEXT("Registered profiles include AnimSequence"), AnimSequenceEntry.IsValid());
	if (AnimSequenceEntry.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* BodySections = nullptr;
		TestTrue(TEXT("AnimSequence entry includes BodySections"), AnimSequenceEntry->TryGetArrayField(TEXT("BodySections"), BodySections));
		if (BodySections)
		{
			TestTrue(TEXT("AnimSequence BodySections include References"), JsonArrayContainsString(*BodySections, TEXT("References")));
			TestTrue(TEXT("AnimSequence BodySections include AssetUserData"), JsonArrayContainsString(*BodySections, TEXT("AssetUserData")));
			TestFalse(TEXT("AnimSequence BodySections omit extract-only _Skipped"), JsonArrayContainsString(*BodySections, TEXT("_Skipped")));
		}

		const TArray<TSharedPtr<FJsonValue>>* RegionPolicies = nullptr;
		TestTrue(TEXT("AnimSequence entry includes RegionPolicies"), AnimSequenceEntry->TryGetArrayField(TEXT("RegionPolicies"), RegionPolicies));
		if (RegionPolicies)
		{
			TestTrue(TEXT("AnimSequence RegionPolicies include Body.Curves"), FindObjectByStringField(*RegionPolicies, TEXT("RegionId"), TEXT("Body.Curves")).IsValid());
			TestTrue(TEXT("AnimSequence RegionPolicies include Body.SyncMarkers"), FindObjectByStringField(*RegionPolicies, TEXT("RegionId"), TEXT("Body.SyncMarkers")).IsValid());
		}
	}

	return true;
}

#endif
