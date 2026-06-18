// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"
#include "AssetDocumentProfileRegistry.h"
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
		TestTrue(FString::Printf(TEXT("Region policy %s exists"), *RegionId.ToString()), Policies.ContainsByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
		{
			return Policy.RegionId == RegionId;
		}));
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
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimSequenceRegisteredProfileSchemaTest,
	"AssetFactory.AssetDocument.AnimSequence.RegisteredProfileSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimSequenceRegisteredProfileSchemaTest::RunTest(const FString&)
{
	FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FAnimSequenceAssetDocumentProfile>());
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
