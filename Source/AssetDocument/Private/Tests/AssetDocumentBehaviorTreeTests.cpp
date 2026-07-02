// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentService.h"
#include "Profiles/BehaviorTreeAssetDocumentProfile.h"
#include "Regions/AssetDocumentReflectedPropertyUtils.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "BehaviorTree/Tasks/BTTask_SetKeyValue.h"
#include "BehaviorTree/Tasks/BTTask_WaitBlackboardTime.h"
#include "Animation/NodeMappingContainer.h"
#include "BlueprintEditorSettings.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Sections/MovieSceneCVarSection.h"
#include "TestActorBase.h"
#include "TestDataAsset.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

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

TSharedRef<FJsonObject> MakeObject()
{
	return MakeShared<FJsonObject>();
}

TSharedPtr<FJsonObject> MakeAssetRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
	Ref->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	Ref->SetStringField(TEXT("Path"), Path);
	return Ref;
}

FString MakeObjectPathFromTarget(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

UBlackboardData* MakeExistingBlackboardAsset(const FString& Target)
{
	UPackage* Package = CreatePackage(*Target);
	return NewObject<UBlackboardData>(
		Package,
		*FPackageName::GetLongPackageAssetName(Target),
		RF_Public | RF_Standalone | RF_Transactional);
}

UBehaviorTree* LoadBehaviorTreeForTarget(const FString& Target)
{
	return LoadObject<UBehaviorTree>(nullptr, *MakeObjectPathFromTarget(Target));
}

FAssetDocumentApplyRequest MakeApplyRequest(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	return Request;
}

FAssetDocumentDiffRequest MakeDiffRequest(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentDiffRequest Request;
	Request.Document = Document;
	return Request;
}

bool ResultHasDiagnostic(const FAssetDocumentResult& Result, const FString& ExpectedCode, const FString& ExpectedPath)
{
	return Result.Diagnostics.ContainsByPredicate([&ExpectedCode, &ExpectedPath](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == ExpectedCode && Diagnostic.Path == ExpectedPath;
	});
}

bool DiffPayloadHasEntry(const TSharedPtr<FJsonObject>& Payload, const FString& BucketName, const FString& ExpectedPath)
{
	if (!Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Payload->TryGetArrayField(BucketName, Entries) || !Entries)
	{
		return false;
	}

	return Entries->ContainsByPredicate([&ExpectedPath](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		return Entry.IsValid() && Entry->TryGetStringField(TEXT("path"), Path) && Path == ExpectedPath;
	});
}

TSharedPtr<FJsonObject> MakeBehaviorTreeBody(
	TSharedPtr<FJsonObject> Blackboard,
	TSharedPtr<FJsonObject> Tree = nullptr,
	TSharedPtr<FJsonObject> EditorLayout = nullptr)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	if (Blackboard.IsValid())
	{
		Body->SetObjectField(TEXT("Blackboard"), Blackboard);
	}
	else
	{
		Body->SetField(TEXT("Blackboard"), MakeShared<FJsonValueNull>());
	}

	if (Tree.IsValid())
	{
		Body->SetObjectField(TEXT("Tree"), Tree);
	}
	if (EditorLayout.IsValid())
	{
		Body->SetObjectField(TEXT("EditorLayout"), EditorLayout);
	}
	return Body;
}

TSharedPtr<FJsonObject> MakeEmptyBehaviorTree()
{
	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetObjectField(TEXT("Root"), MakeShared<FJsonObject>());
	return Tree;
}

TSharedPtr<FJsonObject> MakeTreeWithKeySelectorLikeProperty()
{
	TSharedPtr<FJsonObject> Selector = MakeShared<FJsonObject>();
	Selector->SetStringField(TEXT("Key"), TEXT("TargetActor"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("BlackboardKey"), Selector);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("Id"), TEXT("RootWithKey"));
	Root->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTTask_WaitBlackboardTime"));
	Root->SetObjectField(TEXT("Properties"), Properties);

	TSharedPtr<FJsonObject> Tree = MakeEmptyBehaviorTree();
	Tree->SetObjectField(TEXT("Root"), Root);
	return Tree;
}

TSharedPtr<FJsonObject> MakeBehaviorTreeDocument(const FString& Target, TSharedPtr<FJsonObject> Body)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BehaviorTree"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Body"), Body);
	return Document;
}

TSharedPtr<FJsonObject> MakeClassRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
	Ref->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	Ref->SetStringField(TEXT("Path"), Path);
	return Ref;
}

bool HasDiagnostic(const FAssetDocumentCapabilityResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

TSharedPtr<FJsonObject> GetObjectField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
{
	const TSharedPtr<FJsonObject>* FieldObject = nullptr;
	return Object.IsValid() && Object->TryGetObjectField(FieldName, FieldObject) && FieldObject ? *FieldObject : nullptr;
}

TSharedPtr<FJsonObject> GetObjectFromValue(const TSharedPtr<FJsonValue>& Value)
{
	const TSharedPtr<FJsonObject>* Object = nullptr;
	return Value.IsValid() && Value->TryGetObject(Object) && Object ? *Object : nullptr;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeBlackboardReferenceTest,
	"AssetFactory.AssetDocument.BehaviorTree.BlackboardReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeBlackboardReferenceTest::RunTest(const FString&)
{
	const FString BlackboardTarget = TEXT("/Game/AssetDocumentTests/BB_BT_BlackboardReference");
	const FString BehaviorTreeTarget = TEXT("/Game/AssetDocumentTests/BT_AD_BlackboardReference");
	UBlackboardData* Blackboard = MakeExistingBlackboardAsset(BlackboardTarget);
	TestNotNull(TEXT("Referenced blackboard asset exists"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)), MakeEmptyBehaviorTree(), MakeShared<FJsonObject>());
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(BehaviorTreeTarget, Body)));
	TestTrue(TEXT("BehaviorTree apply with Blackboard AssetRef succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(BehaviorTreeTarget);
	TestNotNull(TEXT("BehaviorTree lifecycle creates an asset"), BehaviorTree);
	if (!BehaviorTree)
	{
		return false;
	}
	TestTrue(TEXT("BehaviorTree has exact class"), BehaviorTree->GetClass() == UBehaviorTree::StaticClass());
	TestTrue(TEXT("BlackboardAsset is assigned"), BehaviorTree->BlackboardAsset == Blackboard);

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = BehaviorTreeTarget;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("BehaviorTree extract succeeds"), ExtractResult.IsSuccess());
	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	const TSharedPtr<FJsonObject>* ExtractedBlackboard = nullptr;
	TestTrue(TEXT("Extracted payload contains Body"), ExtractResult.Payload.IsValid() && ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody) && ExtractedBody && ExtractedBody->IsValid());
	TestTrue(TEXT("Extracted Body contains Blackboard AssetRef"), ExtractedBody && (*ExtractedBody)->TryGetObjectField(TEXT("Blackboard"), ExtractedBlackboard) && ExtractedBlackboard && ExtractedBlackboard->IsValid());
	if (ExtractedBlackboard && ExtractedBlackboard->IsValid())
	{
		TestEqual(TEXT("Extracted Blackboard Kind"), (*ExtractedBlackboard)->GetStringField(TEXT("Kind")), FString(TEXT("AssetRef")));
		TestEqual(TEXT("Extracted Blackboard Path"), (*ExtractedBlackboard)->GetStringField(TEXT("Path")), Blackboard->GetPathName());
	}

	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(BehaviorTreeTarget, Body)));
	TestTrue(TEXT("BehaviorTree diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("Blackboard diff path is stable"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("unchanged"), TEXT("/Body/Blackboard")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeRejectsBlackboardInlineTest,
	"AssetFactory.AssetDocument.BehaviorTree.RejectsBlackboardInline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeRejectsBlackboardInlineTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(nullptr, MakeEmptyBehaviorTree(), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("BlackboardInline"), MakeShared<FJsonObject>());

	FAssetDocumentValidateRequest Request;
	Request.Document = MakeBehaviorTreeDocument(TEXT("/Game/AssetDocumentTests/BT_AD_RejectsBlackboardInline"), Body);
	const FAssetDocumentResult Result = FAssetDocumentService().Validate(Request);
	TestFalse(TEXT("BehaviorTree rejects BlackboardInline"), Result.IsSuccess());
	TestTrue(TEXT("BlackboardInline reports unknown region"), ResultHasDiagnostic(Result, TEXT("UnknownBodyKey"), TEXT("/Body/BlackboardInline")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeRejectsMissingBlackboardForKeySelectorsTest,
	"AssetFactory.AssetDocument.BehaviorTree.RejectsMissingBlackboardForKeySelectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeRejectsMissingBlackboardForKeySelectorsTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(nullptr, MakeTreeWithKeySelectorLikeProperty(), MakeShared<FJsonObject>());

	FAssetDocumentValidateRequest Request;
	Request.Document = MakeBehaviorTreeDocument(TEXT("/Game/AssetDocumentTests/BT_AD_RejectsMissingBlackboardForKeySelectors"), Body);
	const FAssetDocumentResult Result = FAssetDocumentService().Validate(Request);
	TestFalse(TEXT("BehaviorTree rejects key selectors without Blackboard"), Result.IsSuccess());
	TestTrue(TEXT("Missing blackboard diagnostic is stable"), ResultHasDiagnostic(Result, TEXT("MissingBehaviorTreeBlackboard"), TEXT("/Body/Blackboard")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeReflectedPropertiesTest,
	"AssetFactory.AssetDocument.BehaviorTree.ReflectedProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeReflectedPropertiesTest::RunTest(const FString&)
{
	UTestDataAsset* ScalarObject = NewObject<UTestDataAsset>(GetTransientPackage());
	TSharedRef<FJsonObject> ScalarProperties = MakeObject();
	ScalarProperties->SetStringField(TEXT("TestString"), TEXT("Scout"));
	ScalarProperties->SetNumberField(TEXT("TestInt"), 7);
	ScalarProperties->SetNumberField(TEXT("TestFloat"), 2.5);
	ScalarProperties->SetBoolField(TEXT("bTestBool"), true);
	ScalarProperties->SetStringField(TEXT("TestName"), TEXT("PatrolKey"));
	ScalarProperties->SetStringField(TEXT("TestText"), TEXT("Visible text"));

	FAssetDocumentCapabilityResult ScalarApply = FAssetDocumentReflectedPropertyUtils::ApplyProperties(ScalarObject, ScalarProperties, TEXT("/Properties"));
	TestTrue(TEXT("scalar reflected apply succeeds"), ScalarApply.bSuccess);
	TestEqual(TEXT("string applied"), ScalarObject->TestString, FString(TEXT("Scout")));
	TestEqual(TEXT("int applied"), ScalarObject->TestInt, 7);
	TestTrue(TEXT("float applied"), FMath::IsNearlyEqual(ScalarObject->TestFloat, 2.5f));
	TestTrue(TEXT("bool applied"), ScalarObject->bTestBool);
	TestEqual(TEXT("name applied"), ScalarObject->TestName, FName(TEXT("PatrolKey")));
	TestEqual(TEXT("text applied"), ScalarObject->TestText.ToString(), FString(TEXT("Visible text")));

	TSharedRef<FJsonObject> ExtractedScalars = MakeObject();
	FAssetDocumentCapabilityResult ScalarExtract = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ScalarObject, ExtractedScalars, TEXT("/Properties"));
	TestTrue(TEXT("scalar reflected extract succeeds"), ScalarExtract.bSuccess);
	TestEqual(TEXT("string extracted"), ExtractedScalars->GetStringField(TEXT("TestString")), FString(TEXT("Scout")));
	TestEqual(TEXT("name extracted"), ExtractedScalars->GetStringField(TEXT("TestName")), FString(TEXT("PatrolKey")));
	TestEqual(TEXT("text extracted"), ExtractedScalars->GetStringField(TEXT("TestText")), FString(TEXT("Visible text")));

	UBTDecorator_Blackboard* Decorator = NewObject<UBTDecorator_Blackboard>(GetTransientPackage());
	TSharedRef<FJsonObject> EnumProperties = MakeObject();
	EnumProperties->SetStringField(TEXT("FlowAbortMode"), TEXT("Both"));
	FAssetDocumentCapabilityResult EnumApply = FAssetDocumentReflectedPropertyUtils::ApplyProperties(Decorator, EnumProperties, TEXT("/Properties"));
	TestTrue(TEXT("enum reflected apply succeeds"), EnumApply.bSuccess);
	TSharedRef<FJsonObject> ExtractedEnum = MakeObject();
	TestTrue(TEXT("enum reflected extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(Decorator, ExtractedEnum, TEXT("/Properties")).bSuccess);
	TestEqual(TEXT("enum extracted"), ExtractedEnum->GetStringField(TEXT("FlowAbortMode")), FString(TEXT("Both")));

	UBehaviorTree* ReferencedTree = NewObject<UBehaviorTree>(GetTransientPackage(), TEXT("BT_ReflectedPropertyReference"));
	UBTTask_RunBehavior* ObjectRefTask = NewObject<UBTTask_RunBehavior>(GetTransientPackage());
	TSharedRef<FJsonObject> ObjectRefProperties = MakeObject();
	ObjectRefProperties->SetObjectField(TEXT("BehaviorAsset"), MakeAssetRef(ReferencedTree->GetPathName()));
	TestTrue(TEXT("AssetRef object apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(ObjectRefTask, ObjectRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedObjectRef = MakeObject();
	TestTrue(TEXT("AssetRef object extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ObjectRefTask, ExtractedObjectRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedBehaviorAsset = GetObjectField(ExtractedObjectRef, TEXT("BehaviorAsset"));
	TestTrue(TEXT("object reference extracts as AssetRef"), ExtractedBehaviorAsset.IsValid() && ExtractedBehaviorAsset->GetStringField(TEXT("Kind")) == TEXT("AssetRef"));

	TSharedRef<FJsonObject> ExtraAssetRefProperties = MakeObject();
	TSharedPtr<FJsonObject> ExtraAssetRef = MakeAssetRef(ReferencedTree->GetPathName());
	ExtraAssetRef->SetStringField(TEXT("Extra"), TEXT("invalid"));
	ExtraAssetRefProperties->SetObjectField(TEXT("BehaviorAsset"), ExtraAssetRef);
	FAssetDocumentCapabilityResult ExtraAssetRefResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(ObjectRefTask, ExtraAssetRefProperties, TEXT("/Properties"));
	TestFalse(TEXT("AssetRef rejects unknown object fields"), ExtraAssetRefResult.bSuccess);
	TestTrue(TEXT("AssetRef unknown field path/code is exact"), HasDiagnostic(ExtraAssetRefResult, TEXT("/Properties/BehaviorAsset/Extra"), TEXT("UnknownField")));

	UBTTask_RunBehavior* RawObjectRefTask = NewObject<UBTTask_RunBehavior>(GetTransientPackage());
	TSharedRef<FJsonObject> RawObjectRefProperties = MakeObject();
	RawObjectRefProperties->SetStringField(TEXT("BehaviorAsset"), ReferencedTree->GetPathName());
	TestTrue(TEXT("raw object path apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(RawObjectRefTask, RawObjectRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedRawObjectRef = MakeObject();
	TestTrue(TEXT("raw object path extracts canonically"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(RawObjectRefTask, ExtractedRawObjectRef, TEXT("/Properties")).bSuccess);
	TestTrue(TEXT("raw object path canonicalizes to AssetRef"), GetObjectField(ExtractedRawObjectRef, TEXT("BehaviorAsset")).IsValid());

	UBTTask_SetKeyValueClass* ClassRefTask = NewObject<UBTTask_SetKeyValueClass>(GetTransientPackage());
	TSharedRef<FJsonObject> ClassRefProperties = MakeObject();
	ClassRefProperties->SetObjectField(TEXT("BaseClass"), MakeClassRef(TEXT("/Script/Engine.Actor")));
	TestTrue(TEXT("ClassRef apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(ClassRefTask, ClassRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedClassRef = MakeObject();
	TestTrue(TEXT("ClassRef extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ClassRefTask, ExtractedClassRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedBaseClass = GetObjectField(ExtractedClassRef, TEXT("BaseClass"));
	TestTrue(TEXT("class reference extracts as ClassRef"), ExtractedBaseClass.IsValid() && ExtractedBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));

	TSharedRef<FJsonObject> ExtraClassRefProperties = MakeObject();
	TSharedPtr<FJsonObject> ExtraClassRef = MakeClassRef(TEXT("/Script/Engine.Actor"));
	ExtraClassRef->SetStringField(TEXT("Extra"), TEXT("invalid"));
	ExtraClassRefProperties->SetObjectField(TEXT("BaseClass"), ExtraClassRef);
	FAssetDocumentCapabilityResult ExtraClassRefResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(ClassRefTask, ExtraClassRefProperties, TEXT("/Properties"));
	TestFalse(TEXT("ClassRef rejects unknown object fields"), ExtraClassRefResult.bSuccess);
	TestTrue(TEXT("ClassRef unknown field path/code is exact"), HasDiagnostic(ExtraClassRefResult, TEXT("/Properties/BaseClass/Extra"), TEXT("UnknownField")));

	UBTTask_SetKeyValueClass* NestedClassRefTask = NewObject<UBTTask_SetKeyValueClass>(GetTransientPackage());
	TSharedRef<FJsonObject> NestedClassRefProperties = MakeObject();
	TSharedPtr<FJsonObject> NestedValue = MakeShared<FJsonObject>();
	NestedValue->SetStringField(TEXT("DefaultValue"), TEXT("/Script/Engine.Actor"));
	NestedValue->SetStringField(TEXT("BaseClass"), TEXT("/Script/Engine.Actor"));
	NestedClassRefProperties->SetObjectField(TEXT("Value"), NestedValue);
	TestTrue(TEXT("nested struct raw class refs apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedClassRefTask, NestedClassRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedNestedClassRef = MakeObject();
	TestTrue(TEXT("nested struct class refs extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(NestedClassRefTask, ExtractedNestedClassRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedNestedValue = GetObjectField(ExtractedNestedClassRef, TEXT("Value"));
	TSharedPtr<FJsonObject> ExtractedNestedDefaultClass = GetObjectField(ExtractedNestedValue, TEXT("DefaultValue"));
	TSharedPtr<FJsonObject> ExtractedNestedBaseClass = GetObjectField(ExtractedNestedValue, TEXT("BaseClass"));
	TestTrue(TEXT("nested struct default class reference extracts as ClassRef"), ExtractedNestedDefaultClass.IsValid() && ExtractedNestedDefaultClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
	TestTrue(TEXT("nested struct base class reference extracts as ClassRef"), ExtractedNestedBaseClass.IsValid() && ExtractedNestedBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
	UBTTask_SetKeyValueClass* NestedClassRefRoundtripTask = NewObject<UBTTask_SetKeyValueClass>(GetTransientPackage());
	TestTrue(TEXT("nested struct ClassRef object roundtrip apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedClassRefRoundtripTask, ExtractedNestedClassRef, TEXT("/Properties")).bSuccess);

	UBTTask_SetKeyValueObject* NestedObjectRefTask = NewObject<UBTTask_SetKeyValueObject>(GetTransientPackage());
	TSharedRef<FJsonObject> NestedObjectRefProperties = MakeObject();
	TSharedPtr<FJsonObject> NestedObjectValue = MakeShared<FJsonObject>();
	NestedObjectValue->SetStringField(TEXT("DefaultValue"), ReferencedTree->GetPathName());
	NestedObjectValue->SetStringField(TEXT("BaseClass"), UBehaviorTree::StaticClass()->GetPathName());
	NestedObjectRefProperties->SetObjectField(TEXT("Value"), NestedObjectValue);
	TestTrue(TEXT("nested struct raw object refs apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedObjectRefTask, NestedObjectRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedNestedObjectRef = MakeObject();
	TestTrue(TEXT("nested struct object refs extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(NestedObjectRefTask, ExtractedNestedObjectRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedNestedObjectValue = GetObjectField(ExtractedNestedObjectRef, TEXT("Value"));
	TSharedPtr<FJsonObject> ExtractedNestedDefaultObject = GetObjectField(ExtractedNestedObjectValue, TEXT("DefaultValue"));
	TSharedPtr<FJsonObject> ExtractedNestedObjectBaseClass = GetObjectField(ExtractedNestedObjectValue, TEXT("BaseClass"));
	TestTrue(TEXT("nested struct default object reference extracts as AssetRef"), ExtractedNestedDefaultObject.IsValid() && ExtractedNestedDefaultObject->GetStringField(TEXT("Kind")) == TEXT("AssetRef"));
	TestTrue(TEXT("nested struct object base class reference extracts as ClassRef"), ExtractedNestedObjectBaseClass.IsValid() && ExtractedNestedObjectBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
	UBTTask_SetKeyValueObject* NestedObjectRefRoundtripTask = NewObject<UBTTask_SetKeyValueObject>(GetTransientPackage());
	TestTrue(TEXT("nested struct AssetRef object roundtrip apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedObjectRefRoundtripTask, ExtractedNestedObjectRef, TEXT("/Properties")).bSuccess);

	UBTTask_SetKeyValueStruct* StructValueTask = NewObject<UBTTask_SetKeyValueStruct>(GetTransientPackage());
	TSharedRef<FJsonObject> ExtractedStructValueTask = MakeObject();
	FAssetDocumentCapabilityResult InstancedStructExtractResult = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(StructValueTask, ExtractedStructValueTask, TEXT("/Properties"));
	TestFalse(TEXT("FInstancedStruct extraction is explicitly rejected"), InstancedStructExtractResult.bSuccess);
	TestTrue(TEXT("FInstancedStruct extraction path/code is exact"), HasDiagnostic(InstancedStructExtractResult, TEXT("/Properties/Value/DefaultValue"), TEXT("UnsupportedProperty")));

	TSharedRef<FJsonObject> InstancedStructProperties = MakeObject();
	TSharedPtr<FJsonObject> InstancedStructValue = MakeShared<FJsonObject>();
	InstancedStructValue->SetObjectField(TEXT("DefaultValue"), MakeShared<FJsonObject>());
	InstancedStructProperties->SetObjectField(TEXT("Value"), InstancedStructValue);
	FAssetDocumentCapabilityResult InstancedStructValidateResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(StructValueTask, InstancedStructProperties, TEXT("/Properties"));
	TestFalse(TEXT("FInstancedStruct authored input is explicitly rejected"), InstancedStructValidateResult.bSuccess);
	TestTrue(TEXT("FInstancedStruct validation path/code is exact"), HasDiagnostic(InstancedStructValidateResult, TEXT("/Properties/Value/DefaultValue"), TEXT("UnsupportedProperty")));

	UBTTask_WaitBlackboardTime* SelectorTask = NewObject<UBTTask_WaitBlackboardTime>(GetTransientPackage());
	FStructProperty* SelectorProperty = FindFProperty<FStructProperty>(SelectorTask->GetClass(), TEXT("BlackboardKey"));
	TestNotNull(TEXT("BlackboardKey selector property exists"), SelectorProperty);
	if (SelectorProperty)
	{
		TSharedRef<FJsonObject> SelectorJson = MakeObject();
		SelectorJson->SetStringField(TEXT("SelectedKeyName"), TEXT("TargetActor"));
		SelectorJson->SetBoolField(TEXT("bNoneIsAllowedValue"), true);
		void* SelectorPtr = SelectorProperty->ContainerPtrToValuePtr<void>(SelectorTask);
		TestTrue(TEXT("blackboard selector apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, SelectorJson, TEXT("/Properties/BlackboardKey")).bSuccess);
		TSharedPtr<FJsonValue> ExtractedSelectorValue;
		TestTrue(TEXT("blackboard selector extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractBlackboardKeySelector(SelectorProperty, SelectorPtr, ExtractedSelectorValue, TEXT("/Properties/BlackboardKey")).bSuccess);
		TSharedPtr<FJsonObject> ExtractedSelector = ExtractedSelectorValue.IsValid() ? ExtractedSelectorValue->AsObject() : nullptr;
		TestTrue(TEXT("blackboard selector extracts object"), ExtractedSelector.IsValid());
		if (ExtractedSelector.IsValid())
		{
			TestEqual(TEXT("blackboard selector key name"), ExtractedSelector->GetStringField(TEXT("SelectedKeyName")), FString(TEXT("TargetActor")));
			TestTrue(TEXT("blackboard selector none allowed"), ExtractedSelector->GetBoolField(TEXT("bNoneIsAllowedValue")));
			TestFalse(TEXT("blackboard selector skips transient SelectedKeyID"), ExtractedSelector->HasField(TEXT("SelectedKeyID")));
		}

		FBlackboardKeySelector* Selector = static_cast<FBlackboardKeySelector*>(SelectorPtr);
		UBlackboardKeyType_Object* ObjectFilter = NewObject<UBlackboardKeyType_Object>(SelectorTask);
		ObjectFilter->BaseClass = AActor::StaticClass();
		Selector->AllowedTypes = { ObjectFilter };

		TSharedPtr<FJsonValue> ExtractedFilterSelectorValue;
		TestTrue(TEXT("blackboard selector filter extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractBlackboardKeySelector(SelectorProperty, SelectorPtr, ExtractedFilterSelectorValue, TEXT("/Properties/BlackboardKey")).bSuccess);
		TSharedPtr<FJsonObject> ExtractedFilterSelector = GetObjectFromValue(ExtractedFilterSelectorValue);
		const TArray<TSharedPtr<FJsonValue>>* ExtractedAllowedTypes = nullptr;
		TestTrue(TEXT("blackboard selector extracts allowed type filters"), ExtractedFilterSelector.IsValid() && ExtractedFilterSelector->TryGetArrayField(TEXT("AllowedTypes"), ExtractedAllowedTypes) && ExtractedAllowedTypes && ExtractedAllowedTypes->Num() == 1);
		if (ExtractedAllowedTypes && ExtractedAllowedTypes->Num() == 1)
		{
			TSharedPtr<FJsonObject> ExtractedAllowedType = GetObjectFromValue((*ExtractedAllowedTypes)[0]);
			TSharedPtr<FJsonObject> ExtractedAllowedTypeProperties = GetObjectField(ExtractedAllowedType, TEXT("Properties"));
			TSharedPtr<FJsonObject> ExtractedFilterBaseClass = GetObjectField(ExtractedAllowedTypeProperties, TEXT("BaseClass"));
			TestTrue(TEXT("blackboard selector allowed type extracts ClassRef"), ExtractedAllowedType.IsValid() && ExtractedAllowedType->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
			TestTrue(TEXT("blackboard selector allowed type preserves BaseClass"), ExtractedFilterBaseClass.IsValid() && ExtractedFilterBaseClass->GetStringField(TEXT("Path")) == AActor::StaticClass()->GetPathName());

			UBTTask_WaitBlackboardTime* RoundtripSelectorTask = NewObject<UBTTask_WaitBlackboardTime>(GetTransientPackage());
			void* RoundtripSelectorPtr = SelectorProperty->ContainerPtrToValuePtr<void>(RoundtripSelectorTask);
			TestTrue(TEXT("blackboard selector filter apply roundtrip succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, RoundtripSelectorPtr, ExtractedFilterSelector.ToSharedRef(), TEXT("/Properties/BlackboardKey")).bSuccess);
			FBlackboardKeySelector* RoundtripSelector = static_cast<FBlackboardKeySelector*>(RoundtripSelectorPtr);
			UBlackboardKeyType_Object* RoundtripObjectFilter = RoundtripSelector->AllowedTypes.Num() == 1 ? Cast<UBlackboardKeyType_Object>(RoundtripSelector->AllowedTypes[0]) : nullptr;
			TestTrue(TEXT("blackboard selector filter roundtrip restores BaseClass"), RoundtripObjectFilter && RoundtripObjectFilter->BaseClass == AActor::StaticClass());
		}

		TSharedRef<FJsonObject> InvalidFilterSelector = MakeObject();
		TArray<TSharedPtr<FJsonValue>> InvalidFilters;
		InvalidFilters.Add(MakeShared<FJsonValueObject>(MakeClassRef(TEXT("/Script/Engine.Actor"))));
		InvalidFilterSelector->SetArrayField(TEXT("AllowedTypes"), InvalidFilters);
		FAssetDocumentCapabilityResult InvalidFilterResult = FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, InvalidFilterSelector, TEXT("/Properties/BlackboardKey"));
		TestFalse(TEXT("invalid blackboard filter class rejected"), InvalidFilterResult.bSuccess);
		TestTrue(TEXT("invalid blackboard filter class path/code is exact"), HasDiagnostic(InvalidFilterResult, TEXT("/Properties/BlackboardKey/AllowedTypes/0/Path"), TEXT("InvalidClassRef")));

		TSharedRef<FJsonObject> UnknownNestedFilterSelector = MakeObject();
		TSharedPtr<FJsonObject> UnknownFilter = MakeClassRef(UBlackboardKeyType_Object::StaticClass()->GetPathName());
		TSharedPtr<FJsonObject> UnknownFilterProperties = MakeShared<FJsonObject>();
		UnknownFilterProperties->SetStringField(TEXT("NotAKeyTypeProperty"), TEXT("bad"));
		UnknownFilter->SetObjectField(TEXT("Properties"), UnknownFilterProperties);
		TArray<TSharedPtr<FJsonValue>> UnknownFilters;
		UnknownFilters.Add(MakeShared<FJsonValueObject>(UnknownFilter));
		UnknownNestedFilterSelector->SetArrayField(TEXT("AllowedTypes"), UnknownFilters);
		FAssetDocumentCapabilityResult UnknownNestedFilterResult = FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, UnknownNestedFilterSelector, TEXT("/Properties/BlackboardKey"));
		TestFalse(TEXT("unknown blackboard filter property rejected"), UnknownNestedFilterResult.bSuccess);
		TestTrue(TEXT("unknown blackboard filter property path/code is exact"), HasDiagnostic(UnknownNestedFilterResult, TEXT("/Properties/BlackboardKey/AllowedTypes/0/Properties/NotAKeyTypeProperty"), TEXT("UnknownProperty")));

		TSharedRef<FJsonObject> NonAuthoredNestedFilterSelector = MakeObject();
		TSharedPtr<FJsonObject> NonAuthoredFilter = MakeClassRef(UBlackboardKeyType_Enum::StaticClass()->GetPathName());
		TSharedPtr<FJsonObject> NonAuthoredFilterProperties = MakeShared<FJsonObject>();
		NonAuthoredFilterProperties->SetBoolField(TEXT("bIsEnumNameValid"), true);
		NonAuthoredFilter->SetObjectField(TEXT("Properties"), NonAuthoredFilterProperties);
		TArray<TSharedPtr<FJsonValue>> NonAuthoredFilters;
		NonAuthoredFilters.Add(MakeShared<FJsonValueObject>(NonAuthoredFilter));
		NonAuthoredNestedFilterSelector->SetArrayField(TEXT("AllowedTypes"), NonAuthoredFilters);
		FAssetDocumentCapabilityResult NonAuthoredNestedFilterResult = FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, NonAuthoredNestedFilterSelector, TEXT("/Properties/BlackboardKey"));
		TestFalse(TEXT("non-authored blackboard filter property rejected"), NonAuthoredNestedFilterResult.bSuccess);
		TestTrue(TEXT("non-authored blackboard filter property path/code is exact"), HasDiagnostic(NonAuthoredNestedFilterResult, TEXT("/Properties/BlackboardKey/AllowedTypes/0/Properties/bIsEnumNameValid"), TEXT("NonAuthoredProperty")));
	}

	ATestActorBase* ContainerObject = NewObject<ATestActorBase>(GetTransientPackage());
	TSharedRef<FJsonObject> ContainerProperties = MakeObject();
	TSharedPtr<FJsonObject> ConfigJson = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Entries;
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("Name"), TEXT("Damage"));
	Entry->SetNumberField(TEXT("Value"), 12.0);
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
	ConfigJson->SetArrayField(TEXT("Entries"), Entries);
	ContainerProperties->SetObjectField(TEXT("Config"), ConfigJson);
	TestTrue(TEXT("array property apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(ContainerObject, ContainerProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedContainer = MakeObject();
	TestTrue(TEXT("array property extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ContainerObject, ExtractedContainer, TEXT("/Properties")).bSuccess);
	TestTrue(TEXT("array property extracted"), GetObjectField(ExtractedContainer, TEXT("Config")).IsValid());

	UNodeMappingContainer* MapObject = NewObject<UNodeMappingContainer>(GetTransientPackage());
	TSharedRef<FJsonObject> MapProperties = MakeObject();
	TSharedPtr<FJsonObject> SourceToTarget = MakeShared<FJsonObject>();
	SourceToTarget->SetStringField(TEXT("SourceBone"), TEXT("TargetBone"));
	MapProperties->SetObjectField(TEXT("SourceToTarget"), SourceToTarget);
	TestTrue(TEXT("map property apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(MapObject, MapProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedMap = MakeObject();
	TestTrue(TEXT("map property extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(MapObject, ExtractedMap, TEXT("/Properties")).bSuccess);
	TestTrue(TEXT("map property extracted"), GetObjectField(ExtractedMap, TEXT("SourceToTarget")).IsValid());

	UClass* PropertyEditorTestClass = LoadClass<UObject>(nullptr, TEXT("/Script/UnrealEd.PropertyEditorTestObject"));
	TestNotNull(TEXT("PropertyEditorTestObject class exists"), PropertyEditorTestClass);
	if (PropertyEditorTestClass)
	{
		UObject* UnsupportedMapKeyObject = NewObject<UObject>(GetTransientPackage(), PropertyEditorTestClass);
		TSharedRef<FJsonObject> UnsupportedMapKeyProperties = MakeObject();
		TSharedPtr<FJsonObject> UnsupportedMapKeyValue = MakeShared<FJsonObject>();
		UnsupportedMapKeyValue->SetStringField(TEXT("NotAStableKey"), TEXT("Value"));
		UnsupportedMapKeyProperties->SetObjectField(TEXT("LinearColorToStringMap"), UnsupportedMapKeyValue);
		FAssetDocumentCapabilityResult UnsupportedMapKeyResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(UnsupportedMapKeyObject, UnsupportedMapKeyProperties, TEXT("/Properties"));
		TestFalse(TEXT("unsupported map key type rejected"), UnsupportedMapKeyResult.bSuccess);
		TestTrue(TEXT("unsupported map key type path/code is exact"), HasDiagnostic(UnsupportedMapKeyResult, TEXT("/Properties/LinearColorToStringMap"), TEXT("UnsupportedProperty")));
	}

	UBlueprintEditorSettings* SetSettings = NewObject<UBlueprintEditorSettings>(GetTransientPackage());
	TSharedRef<FJsonObject> SetProperties = MakeObject();
	TArray<TSharedPtr<FJsonValue>> SetValues;
	SetValues.Add(MakeShared<FJsonValueString>(TEXT("Zulu")));
	SetValues.Add(MakeShared<FJsonValueString>(TEXT("Alpha")));
	SetProperties->SetArrayField(TEXT("TypePromotionPinDenyList"), SetValues);
	TestTrue(TEXT("set property apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(SetSettings, SetProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedSetProperties = MakeObject();
	TestTrue(TEXT("set property extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(SetSettings, ExtractedSetProperties, TEXT("/Properties")).bSuccess);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedSetValues = nullptr;
	TestTrue(TEXT("set property extracts stable array"), ExtractedSetProperties->TryGetArrayField(TEXT("TypePromotionPinDenyList"), ExtractedSetValues) && ExtractedSetValues && ExtractedSetValues->Num() == 2);
	if (ExtractedSetValues && ExtractedSetValues->Num() == 2)
	{
		FString FirstSetValue;
		FString SecondSetValue;
		(*ExtractedSetValues)[0]->TryGetString(FirstSetValue);
		(*ExtractedSetValues)[1]->TryGetString(SecondSetValue);
		TestEqual(TEXT("set extraction is canonically sorted first"), FirstSetValue, FString(TEXT("Alpha")));
		TestEqual(TEXT("set extraction is canonically sorted second"), SecondSetValue, FString(TEXT("Zulu")));
	}

	TSharedRef<FJsonObject> SameSetDifferentOrder = MakeObject();
	TArray<TSharedPtr<FJsonValue>> ReorderedSetValues;
	ReorderedSetValues.Add(MakeShared<FJsonValueString>(TEXT("Alpha")));
	ReorderedSetValues.Add(MakeShared<FJsonValueString>(TEXT("Zulu")));
	SameSetDifferentOrder->SetArrayField(TEXT("TypePromotionPinDenyList"), ReorderedSetValues);
	TArray<TSharedPtr<FJsonValue>> SetDiffEntries;
	TestTrue(TEXT("set diff succeeds"), FAssetDocumentReflectedPropertyUtils::DiffProperties(SetSettings, SameSetDifferentOrder, TEXT("/Properties"), SetDiffEntries).bSuccess);
	TestEqual(TEXT("set diff ignores element order"), SetDiffEntries.Num(), 0);

	TSharedRef<FJsonObject> UnknownProperties = MakeObject();
	UnknownProperties->SetStringField(TEXT("DoesNotExist"), TEXT("value"));
	FAssetDocumentCapabilityResult UnknownResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(ScalarObject, UnknownProperties, TEXT("/Properties"));
	TestFalse(TEXT("unknown property rejected"), UnknownResult.bSuccess);
	TestTrue(TEXT("unknown property path/code is exact"), HasDiagnostic(UnknownResult, TEXT("/Properties/DoesNotExist"), TEXT("UnknownProperty")));

	TSharedRef<FJsonObject> RuntimeProperties = MakeObject();
	RuntimeProperties->SetStringField(TEXT("ParentNode"), TEXT("invalid"));
	FAssetDocumentCapabilityResult RuntimeResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(Decorator, RuntimeProperties, TEXT("/Properties"));
	TestFalse(TEXT("non-authored property rejected"), RuntimeResult.bSuccess);
	TestTrue(TEXT("non-authored property path/code is exact"), HasDiagnostic(RuntimeResult, TEXT("/Properties/ParentNode"), TEXT("NonAuthoredProperty")));

	UMovieSceneCVarSection* UnsupportedObject = NewObject<UMovieSceneCVarSection>(GetTransientPackage());
	TSharedRef<FJsonObject> UnsupportedProperties = MakeObject();
	TArray<TSharedPtr<FJsonValue>> ConsoleVariableCollections;
	TSharedPtr<FJsonObject> UnsupportedCollection = MakeShared<FJsonObject>();
	UnsupportedCollection->SetField(TEXT("Interface"), MakeShared<FJsonValueNull>());
	ConsoleVariableCollections.Add(MakeShared<FJsonValueObject>(UnsupportedCollection));
	UnsupportedProperties->SetArrayField(TEXT("ConsoleVariableCollections"), ConsoleVariableCollections);
	FAssetDocumentCapabilityResult UnsupportedResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(UnsupportedObject, UnsupportedProperties, TEXT("/Properties"));
	TestFalse(TEXT("unsupported authored property rejected"), UnsupportedResult.bSuccess);
	TestTrue(TEXT("unsupported authored property path/code is exact"), HasDiagnostic(UnsupportedResult, TEXT("/Properties/ConsoleVariableCollections/0/Interface"), TEXT("UnsupportedProperty")));

	UBehaviorTree* TreeObject = NewObject<UBehaviorTree>(GetTransientPackage());
	TSharedRef<FJsonObject> TreeOwnedProperties = MakeObject();
	TreeOwnedProperties->SetField(TEXT("RootNode"), MakeShared<FJsonValueNull>());
	FAssetDocumentCapabilityResult TreeOwnedResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(TreeObject, TreeOwnedProperties, TEXT("/Properties"));
	TestFalse(TEXT("Body.Tree-owned property rejected"), TreeOwnedResult.bSuccess);
	TestTrue(TEXT("Body.Tree-owned property path/code is exact"), HasDiagnostic(TreeOwnedResult, TEXT("/Properties/RootNode"), TEXT("BodyTreeProperty")));

	TSharedRef<FJsonObject> DiffProperties = MakeObject();
	DiffProperties->SetStringField(TEXT("TestString"), TEXT("Changed"));
	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	TestTrue(TEXT("reflected diff succeeds"), FAssetDocumentReflectedPropertyUtils::DiffProperties(ScalarObject, DiffProperties, TEXT("/Properties"), DiffEntries).bSuccess);
	TestTrue(TEXT("reflected diff reports changed entry"), DiffEntries.Num() == 1);

	return true;
}

#endif
