// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentService.h"
#include "Profiles/BlackboardDataAssetDocumentProfile.h"
#include "Regions/AssetDocumentBlackboardKeySchemaUtils.h"

#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/BlackboardData.h"
#include "Engine/EngineTypes.h"
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

TSharedRef<FJsonObject> MakeBlackboardKeyJson(
	const FString& Name,
	const FString& Type,
	const FString& BaseClass = TEXT(""),
	const FString& Enum = TEXT(""),
	const FString& KeyTypeClass = TEXT(""))
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Name"), Name);
	if (!Type.IsEmpty())
	{
		Json->SetStringField(TEXT("Type"), Type);
	}
	if (!BaseClass.IsEmpty())
	{
		Json->SetStringField(TEXT("BaseClass"), BaseClass);
	}
	if (!Enum.IsEmpty())
	{
		Json->SetStringField(TEXT("Enum"), Enum);
	}
	if (!KeyTypeClass.IsEmpty())
	{
		Json->SetStringField(TEXT("KeyTypeClass"), KeyTypeClass);
	}
	return Json;
}

bool ExpectSingleDiagnostic(
	FAutomationTestBase& Test,
	const FAssetDocumentCapabilityResult& Result,
	const FString& Code,
	const FString& Path)
{
	Test.TestFalse(TEXT("Result failed"), Result.bSuccess);
	if (Result.Diagnostics.Num() != 1)
	{
		Test.AddError(FString::Printf(TEXT("Expected 1 diagnostic, got %d"), Result.Diagnostics.Num()));
		return false;
	}

	Test.TestEqual(TEXT("Diagnostic code"), Result.Diagnostics[0].Code, Code);
	Test.TestEqual(TEXT("Diagnostic path"), Result.Diagnostics[0].Path, Path);
	return true;
}

FAssetDocumentCapabilityResult DetectDuplicateLocalKeysForTest(const TArray<TSharedRef<FJsonObject>>& Keys)
{
	TSet<FName> SeenNames;
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			Keys[Index],
			FString::Printf(TEXT("/Body/Keys/%d"), Index),
			Spec);
		if (!ParseResult.bSuccess)
		{
			return ParseResult;
		}

		if (SeenNames.Contains(Spec.Name))
		{
			return FAssetDocumentCapabilityResult::Failure(
				FString::Printf(TEXT("Duplicate blackboard key '%s'"), *Spec.Name.ToString()),
				FString::Printf(TEXT("/Body/Keys/%d/Name"), Index),
				TEXT("DuplicateBlackboardKey"));
		}
		SeenNames.Add(Spec.Name);
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("No duplicate keys"));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardKeyAliasResolutionTest,
	"AssetFactory.AssetDocument.BlackboardData.Keys.AliasResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardKeyAliasResolutionTest::RunTest(const FString&)
{
	struct FAliasExpectation
	{
		FString Alias;
		UClass* ExpectedClass = nullptr;
		FString ExpectedCanonicalType;
	};

	const TArray<FAliasExpectation> Expectations = {
		{TEXT("Bool"), UBlackboardKeyType_Bool::StaticClass(), TEXT("Bool")},
		{TEXT("Int"), UBlackboardKeyType_Int::StaticClass(), TEXT("Int")},
		{TEXT("Float"), UBlackboardKeyType_Float::StaticClass(), TEXT("Float")},
		{TEXT("String"), UBlackboardKeyType_String::StaticClass(), TEXT("String")},
		{TEXT("Name"), UBlackboardKeyType_Name::StaticClass(), TEXT("Name")},
		{TEXT("Vector"), UBlackboardKeyType_Vector::StaticClass(), TEXT("Vector")},
		{TEXT("Rotator"), UBlackboardKeyType_Rotator::StaticClass(), TEXT("Rotator")},
	};

	for (const FAliasExpectation& Expectation : Expectations)
	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			MakeBlackboardKeyJson(Expectation.Alias + TEXT("Key"), Expectation.Alias),
			FString::Printf(TEXT("/Body/Keys/%sKey"), *Expectation.Alias),
			Spec);
		TestTrue(FString::Printf(TEXT("%s parses"), *Expectation.Alias), ParseResult.bSuccess);

		UClass* ResolvedClass = nullptr;
		FString CanonicalType;
		const FAssetDocumentCapabilityResult ResolveResult =
			FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyTypeClass(Spec, ResolvedClass, CanonicalType);
		TestTrue(FString::Printf(TEXT("%s resolves"), *Expectation.Alias), ResolveResult.bSuccess);
		TestEqual(FString::Printf(TEXT("%s class"), *Expectation.Alias), ResolvedClass, Expectation.ExpectedClass);
		TestEqual(FString::Printf(TEXT("%s canonical type"), *Expectation.Alias), CanonicalType, Expectation.ExpectedCanonicalType);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardKeyRequiredReferenceTest,
	"AssetFactory.AssetDocument.BlackboardData.Keys.RequiredReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardKeyRequiredReferenceTest::RunTest(const FString&)
{
	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			MakeBlackboardKeyJson(TEXT("Target"), TEXT("Object")),
			TEXT("/Body/Keys/Target"),
			Spec);
		TestTrue(TEXT("Object key parses before validation"), ParseResult.bSuccess);
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(Spec, TEXT("/Body/Keys/Target")),
			TEXT("MissingBlackboardKeyBaseClass"),
			TEXT("/Body/Keys/Target/BaseClass"));
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			MakeBlackboardKeyJson(TEXT("ChosenClass"), TEXT("Class")),
			TEXT("/Body/Keys/ChosenClass"),
			Spec);
		TestTrue(TEXT("Class key parses before validation"), ParseResult.bSuccess);
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(Spec, TEXT("/Body/Keys/ChosenClass")),
			TEXT("MissingBlackboardKeyBaseClass"),
			TEXT("/Body/Keys/ChosenClass/BaseClass"));
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			MakeBlackboardKeyJson(TEXT("Mode"), TEXT("Enum")),
			TEXT("/Body/Keys/Mode"),
			Spec);
		TestTrue(TEXT("Enum key parses before validation"), ParseResult.bSuccess);
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(Spec, TEXT("/Body/Keys/Mode")),
			TEXT("MissingBlackboardKeyEnum"),
			TEXT("/Body/Keys/Mode/Enum"));
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			MakeBlackboardKeyJson(TEXT("Broken"), TEXT("Bogus")),
			TEXT("/Body/Keys/Broken"),
			Spec);
		TestTrue(TEXT("Unknown type parses before resolution"), ParseResult.bSuccess);

		UClass* ResolvedClass = nullptr;
		FString CanonicalType;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyTypeClass(Spec, ResolvedClass, CanonicalType),
			TEXT("InvalidBlackboardKeyType"),
			TEXT("/Body/Keys/Broken/Type"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardKeyExplicitClassTest,
	"AssetFactory.AssetDocument.BlackboardData.Keys.ExplicitKeyTypeClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardKeyExplicitClassTest::RunTest(const FString&)
{
	FAssetDocumentBlackboardKeySpec Spec;
	const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
		MakeBlackboardKeyJson(
			TEXT("ExplicitName"),
			TEXT(""),
			TEXT(""),
			TEXT(""),
			TEXT("/Script/AIModule.BlackboardKeyType_Name")),
		TEXT("/Body/Keys/ExplicitName"),
		Spec);
	TestTrue(TEXT("Explicit KeyTypeClass parses"), ParseResult.bSuccess);

	UClass* ResolvedClass = nullptr;
	FString CanonicalType;
	const FAssetDocumentCapabilityResult ResolveResult =
		FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyTypeClass(Spec, ResolvedClass, CanonicalType);
	TestTrue(TEXT("Explicit KeyTypeClass resolves"), ResolveResult.bSuccess);
	TestEqual(TEXT("Explicit class is Name key type"), ResolvedClass, UBlackboardKeyType_Name::StaticClass());
	TestEqual(TEXT("Explicit canonical type uses loaded class"), CanonicalType, FString(TEXT("Name")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardKeyDuplicateLocalNamesTest,
	"AssetFactory.AssetDocument.BlackboardData.Keys.DuplicateLocalNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardKeyDuplicateLocalNamesTest::RunTest(const FString&)
{
	const TArray<TSharedRef<FJsonObject>> Keys = {
		MakeBlackboardKeyJson(TEXT("Target"), TEXT("Bool")),
		MakeBlackboardKeyJson(TEXT("Target"), TEXT("Int")),
	};
	ExpectSingleDiagnostic(
		*this,
		DetectDuplicateLocalKeysForTest(Keys),
		TEXT("DuplicateBlackboardKey"),
		TEXT("/Body/Keys/1/Name"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardKeyParentLookupTest,
	"AssetFactory.AssetDocument.BlackboardData.Keys.ParentLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardKeyParentLookupTest::RunTest(const FString&)
{
	UBlackboardData* Parent = NewObject<UBlackboardData>(GetTransientPackage(), TEXT("BB_ParentLookupParent"));
	UBlackboardData* Child = NewObject<UBlackboardData>(GetTransientPackage(), TEXT("BB_ParentLookupChild"));
	Child->Parent = Parent;

	FBlackboardEntry ParentEntry;
	ParentEntry.EntryName = TEXT("InheritedTarget");
	ParentEntry.KeyType = NewObject<UBlackboardKeyType_Bool>(Parent);
	Parent->Keys.Add(ParentEntry);

	FBlackboardEntry ParentOverrideEntry;
	ParentOverrideEntry.EntryName = TEXT("SharedName");
	ParentOverrideEntry.KeyType = NewObject<UBlackboardKeyType_Bool>(Parent);
	Parent->Keys.Add(ParentOverrideEntry);

	FBlackboardEntry ChildEntry;
	ChildEntry.EntryName = TEXT("LocalCount");
	ChildEntry.KeyType = NewObject<UBlackboardKeyType_Int>(Child);
	Child->Keys.Add(ChildEntry);

	FBlackboardEntry ChildOverrideEntry;
	ChildOverrideEntry.EntryName = TEXT("SharedName");
	ChildOverrideEntry.KeyType = NewObject<UBlackboardKeyType_Int>(Child);
	Child->Keys.Add(ChildOverrideEntry);

	TMap<FName, FAssetDocumentBlackboardKeyLookupEntry> Lookup;
	const FAssetDocumentCapabilityResult LookupResult = FAssetDocumentBlackboardKeySchemaUtils::BuildLookup(Child, Lookup);
	TestTrue(TEXT("Lookup builds"), LookupResult.bSuccess);

	FAssetDocumentBlackboardKeyLookupEntry FoundInherited;
	TestTrue(TEXT("Finds inherited key"), FAssetDocumentBlackboardKeySchemaUtils::FindKeyInLookup(Lookup, TEXT("InheritedTarget"), FoundInherited));
	TestTrue(TEXT("Inherited key marked inherited"), FoundInherited.bInherited);
	TestEqual(TEXT("Inherited key type"), FoundInherited.KeyTypeClass, UBlackboardKeyType_Bool::StaticClass());

	FAssetDocumentBlackboardKeyLookupEntry FoundLocal;
	TestTrue(TEXT("Finds local key"), FAssetDocumentBlackboardKeySchemaUtils::FindKeyInLookup(Lookup, TEXT("LocalCount"), FoundLocal));
	TestFalse(TEXT("Local key is not inherited"), FoundLocal.bInherited);
	TestEqual(TEXT("Local key type"), FoundLocal.KeyTypeClass, UBlackboardKeyType_Int::StaticClass());

	FAssetDocumentBlackboardKeyLookupEntry FoundOverride;
	TestTrue(TEXT("Finds override key"), FAssetDocumentBlackboardKeySchemaUtils::FindKeyInLookup(Lookup, TEXT("SharedName"), FoundOverride));
	TestFalse(TEXT("Local key overrides inherited key"), FoundOverride.bInherited);
	TestEqual(TEXT("Override key type"), FoundOverride.KeyTypeClass, UBlackboardKeyType_Int::StaticClass());

	const FBlackboardEntry* LocalEntry = Child->Keys.FindByPredicate([](const FBlackboardEntry& Entry)
	{
		return Entry.EntryName == TEXT("LocalCount");
	});
	TestNotNull(TEXT("Local child key exists"), LocalEntry);
	if (!LocalEntry)
	{
		return false;
	}

	const TSharedRef<FJsonObject> ExtractedLocal = FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(*LocalEntry);
	TestEqual(TEXT("Extracted local key name"), ExtractedLocal->GetStringField(TEXT("Name")), FString(TEXT("LocalCount")));
	TestEqual(TEXT("Extracted local key type"), ExtractedLocal->GetStringField(TEXT("Type")), FString(TEXT("Int")));
	TestFalse(TEXT("Single key extraction does not include inherited name"), ExtractedLocal->GetStringField(TEXT("Name")) == TEXT("InheritedTarget"));

	return true;
}

#endif
