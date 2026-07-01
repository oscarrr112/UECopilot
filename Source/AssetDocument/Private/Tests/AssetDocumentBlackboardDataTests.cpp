// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentService.h"
#include "Profiles/BlackboardDataAssetDocumentProfile.h"

#include "BehaviorTree/BlackboardData.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const FAssetDocumentRegionPolicy* FindPolicyByRegionId(const TArray<FAssetDocumentRegionPolicy>& Policies, FName RegionId)
{
	return Policies.FindByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
	{
		return Policy.RegionId == RegionId;
	});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataProfileShapeTest,
	"AssetFactory.AssetDocument.BlackboardData.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataProfileShapeTest::RunTest(const FString&)
{
	TSharedPtr<IAssetDocumentProfile> RegisteredProfile = FAssetDocumentService::GetProfileRegistry().FindForClass(UBlackboardData::StaticClass());
	TestTrue(TEXT("BlackboardData profile is registered"), RegisteredProfile.IsValid());
	if (!RegisteredProfile.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Exact class is UBlackboardData"), RegisteredProfile->GetExactClass(), UBlackboardData::StaticClass());

	FAssetDocumentTemplateContext TemplateContext;
	TemplateContext.Target = TEXT("/Game/AssetDocumentTests/BB_ProfileShape");
	TemplateContext.ClassPath = TEXT("/Script/AIModule.BlackboardData");
	const TSharedRef<FJsonObject> Template = RegisteredProfile->CreateTemplate(TemplateContext);
	TestEqual(TEXT("Template class is BlackboardData"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/AIModule.BlackboardData")));

	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template contains Body"), Template->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid());
	if (Body && Body->IsValid())
	{
		TestTrue(TEXT("Body contains Parent"), (*Body)->HasField(TEXT("Parent")));
		TestTrue(TEXT("Body contains Keys"), (*Body)->HasField(TEXT("Keys")));
	}

	const TArray<FName> BodyKeys = RegisteredProfile->GetBodyKeys();
	TestTrue(TEXT("Parent body key is registered"), BodyKeys.Contains(TEXT("Parent")));
	TestTrue(TEXT("Keys body key is registered"), BodyKeys.Contains(TEXT("Keys")));
	TestNotNull(TEXT("Body root resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Body")));
	TestNotNull(TEXT("Parent resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Parent")));
	TestNotNull(TEXT("Keys resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Keys")));

	const TArray<FAssetDocumentRegionPolicy> Policies = RegisteredProfile->GetRegionPolicies();
	TestNotNull(TEXT("Policy includes Body.Parent"), FindPolicyByRegionId(Policies, TEXT("Body.Parent")));
	TestNotNull(TEXT("Policy includes Body.Keys"), FindPolicyByRegionId(Policies, TEXT("Body.Keys")));
	return true;
}

#endif
