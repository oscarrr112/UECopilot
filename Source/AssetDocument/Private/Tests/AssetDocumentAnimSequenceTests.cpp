// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentModule.h"
#include "AssetDocumentService.h"
#include "Profiles/AnimSequenceAssetDocumentProfile.h"

#include "Animation/AnimSequence.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
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
		TestFalse(TEXT("Preflight rejects authored Body while apply is unsupported"), PreflightResult.bSuccess);
		TestEqual(TEXT("Preflight reports unsupported operation"), PreflightResult.Diagnostics.Num() > 0 ? PreflightResult.Diagnostics[0].Code : FString(), FString(TEXT("UnsupportedOperation")));

		FAssetDocumentCapabilityContext ExtractContext;
		ExtractContext.Asset = NewObject<UAnimSequence>(GetTransientPackage());
		ExtractContext.AssetClass = UAnimSequence::StaticClass();
		TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
		const FAssetDocumentCapabilityResult ExtractResult = BodyAdapter->Extract(ExtractContext, ExtractedBody);
		TestTrue(TEXT("Extract succeeds with diagnostic-only skipped metadata"), ExtractResult.bSuccess);
		const TSharedPtr<FJsonObject>* SkippedObject = nullptr;
		TestTrue(TEXT("Extract writes _Skipped diagnostic object"), ExtractedBody->TryGetObjectField(TEXT("_Skipped"), SkippedObject));
		if (SkippedObject && SkippedObject->IsValid())
		{
			TestEqual(TEXT("_Skipped diagnostic code"), (*SkippedObject)->GetStringField(TEXT("Code")), FString(TEXT("AnimSequenceBodyExtractUnsupported")));
		}
	}

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
