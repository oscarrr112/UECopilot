// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentService.h"
#include "Profiles/BehaviorTreeAssetDocumentProfile.h"

#include "BehaviorTree/BehaviorTree.h"
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
	FAssetDocumentBehaviorTreeProfileShapeTest,
	"AssetFactory.AssetDocument.BehaviorTree.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeProfileShapeTest::RunTest(const FString&)
{
	TSharedPtr<IAssetDocumentProfile> RegisteredProfile = FAssetDocumentService::GetProfileRegistry().FindForClass(UBehaviorTree::StaticClass());
	TestTrue(TEXT("BehaviorTree profile is registered"), RegisteredProfile.IsValid());
	if (!RegisteredProfile.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Exact class is UBehaviorTree"), RegisteredProfile->GetExactClass(), UBehaviorTree::StaticClass());

	FAssetDocumentTemplateContext TemplateContext;
	TemplateContext.Target = TEXT("/Game/AssetDocumentTests/BT_ProfileShape");
	TemplateContext.ClassPath = TEXT("/Script/AIModule.BehaviorTree");
	const TSharedRef<FJsonObject> Template = RegisteredProfile->CreateTemplate(TemplateContext);
	TestEqual(TEXT("Template class is BehaviorTree"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/AIModule.BehaviorTree")));

	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template contains Body"), Template->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid());
	if (Body && Body->IsValid())
	{
		TestTrue(TEXT("Body contains Blackboard"), (*Body)->HasField(TEXT("Blackboard")));
		TestTrue(TEXT("Body contains Tree"), (*Body)->HasField(TEXT("Tree")));
		TestTrue(TEXT("Body contains EditorLayout"), (*Body)->HasField(TEXT("EditorLayout")));
		TestFalse(TEXT("Body does not contain BlackboardInline"), (*Body)->HasField(TEXT("BlackboardInline")));

		const TSharedPtr<FJsonObject>* Tree = nullptr;
		TestTrue(TEXT("Tree is an object"), (*Body)->TryGetObjectField(TEXT("Tree"), Tree) && Tree && Tree->IsValid());
		if (Tree && Tree->IsValid())
		{
			TestTrue(TEXT("Tree contains RootDecorators"), (*Tree)->HasField(TEXT("RootDecorators")));
			TestTrue(TEXT("Tree contains RootDecoratorLogic"), (*Tree)->HasField(TEXT("RootDecoratorLogic")));
			TestTrue(TEXT("Tree contains Root"), (*Tree)->HasField(TEXT("Root")));
		}
	}

	const TArray<FName> BodyKeys = RegisteredProfile->GetBodyKeys();
	TestTrue(TEXT("Blackboard body key is registered"), BodyKeys.Contains(TEXT("Blackboard")));
	TestTrue(TEXT("Tree body key is registered"), BodyKeys.Contains(TEXT("Tree")));
	TestTrue(TEXT("EditorLayout body key is registered"), BodyKeys.Contains(TEXT("EditorLayout")));
	TestFalse(TEXT("BlackboardInline body key is not registered"), BodyKeys.Contains(TEXT("BlackboardInline")));
	TestNotNull(TEXT("Body root resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Body")));
	TestNotNull(TEXT("Blackboard resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Blackboard")));
	TestNotNull(TEXT("Tree resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Tree")));
	TestNotNull(TEXT("EditorLayout resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("EditorLayout")));
	TestNull(TEXT("BlackboardInline does not resolve adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("BlackboardInline")));

	const TArray<FAssetDocumentRegionPolicy> Policies = RegisteredProfile->GetRegionPolicies();
	TestNotNull(TEXT("Policy includes Body.Blackboard"), FindPolicyByRegionId(Policies, TEXT("Body.Blackboard")));
	TestNotNull(TEXT("Policy includes Body.Tree"), FindPolicyByRegionId(Policies, TEXT("Body.Tree")));
	TestNotNull(TEXT("Policy includes Body.EditorLayout"), FindPolicyByRegionId(Policies, TEXT("Body.EditorLayout")));
	return true;
}

#endif
