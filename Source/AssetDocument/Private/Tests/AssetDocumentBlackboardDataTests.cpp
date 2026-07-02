// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentService.h"
#include "AssetDocumentSidecar.h"
#include "Profiles/BlackboardDataAssetDocumentProfile.h"
#include "Regions/AssetDocumentBlackboardKeySchemaUtils.h"

#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_NativeEnum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/BlackboardData.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"

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
	const FString& KeyTypeClass = TEXT(""),
	const FString& Description = TEXT(""))
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
	if (!Description.IsEmpty())
	{
		Json->SetStringField(TEXT("Description"), Description);
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

TArray<FAssetDocumentBlackboardKeySpec> ParseKeysForTest(
	FAutomationTestBase& Test,
	const TArray<TSharedRef<FJsonObject>>& Keys)
{
	TArray<FAssetDocumentBlackboardKeySpec> Specs;
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			Keys[Index],
			FString::Printf(TEXT("/Body/Keys/%d"), Index),
			Spec);
		Test.TestTrue(FString::Printf(TEXT("Key %d parses"), Index), ParseResult.bSuccess);
		Specs.Add(Spec);
	}
	return Specs;
}

FString MakeObjectPathFromTarget(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

UBlackboardData* LoadBlackboardForTarget(const FString& Target)
{
	return LoadObject<UBlackboardData>(nullptr, *MakeObjectPathFromTarget(Target));
}

TSharedPtr<FJsonObject> MakeAssetRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	Fragment->SetStringField(TEXT("Path"), Path);
	return Fragment;
}

TSharedPtr<FJsonObject> MakeBlackboardDataDocument(const FString& Target, TSharedPtr<FJsonObject> Body)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BlackboardData"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Body"), Body);
	return Document;
}

TSharedPtr<FJsonObject> MakeBlackboardDataBody(TSharedPtr<FJsonObject> Parent, TArray<TSharedRef<FJsonObject>> Keys)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	if (Parent.IsValid())
	{
		Body->SetObjectField(TEXT("Parent"), Parent);
	}
	else
	{
		Body->SetField(TEXT("Parent"), MakeShared<FJsonValueNull>());
	}

	TArray<TSharedPtr<FJsonValue>> KeyValues;
	KeyValues.Reserve(Keys.Num());
	for (const TSharedRef<FJsonObject>& Key : Keys)
	{
		KeyValues.Add(MakeShared<FJsonValueObject>(Key));
	}
	Body->SetArrayField(TEXT("Keys"), MoveTemp(KeyValues));
	return Body;
}

FAssetDocumentApplyRequest MakeApplyRequest(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	return Request;
}

bool ResultHasDiagnostic(const FAssetDocumentResult& Result, const FString& ExpectedCode, const FString& ExpectedPath)
{
	return Result.Diagnostics.ContainsByPredicate([&ExpectedCode, &ExpectedPath](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == ExpectedCode && Diagnostic.Path == ExpectedPath;
	});
}

TSharedPtr<FJsonObject> GetExtractedBody(const FAssetDocumentResult& ExtractResult)
{
	if (!ExtractResult.Payload.IsValid())
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject>* Body = nullptr;
	if (ExtractResult.Payload->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid())
	{
		return *Body;
	}
	return nullptr;
}

bool DiffPayloadHasNoChangedOrFailedEntries(const TSharedPtr<FJsonObject>& Payload)
{
	const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Failed = nullptr;
	return Payload.IsValid()
		&& Payload->TryGetArrayField(TEXT("changed"), Changed)
		&& Payload->TryGetArrayField(TEXT("failed"), Failed)
		&& Changed
		&& Failed
		&& Changed->Num() == 0
		&& Failed->Num() == 0;
}

bool DiffPayloadHasChangedPath(const TSharedPtr<FJsonObject>& Payload, const FString& ExpectedPath)
{
	const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
	if (!Payload.IsValid() || !Payload->TryGetArrayField(TEXT("changed"), Changed) || !Changed)
	{
		return false;
	}

	return Changed->ContainsByPredicate([&ExpectedPath](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() && EntryValue->Type == EJson::Object
			? EntryValue->AsObject()
			: nullptr;
		FString Path;
		return Entry.IsValid()
			&& Entry->TryGetStringField(TEXT("path"), Path)
			&& Path == ExpectedPath;
	});
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

bool ExpectSyncRegions(
	FAutomationTestBase* Test,
	const TSharedPtr<FJsonObject>& Document,
	const TArray<FString>& ExpectedRegionIds)
{
	const TSharedPtr<FJsonObject>* Meta = nullptr;
	const TSharedPtr<FJsonObject>* Sync = nullptr;
	const TSharedPtr<FJsonObject>* Regions = nullptr;
	Test->TestTrue(TEXT("ApplyFile sidecar includes _meta"), Document.IsValid() && Document->TryGetObjectField(TEXT("_meta"), Meta));
	Test->TestTrue(TEXT("ApplyFile sidecar includes _meta.sync"), Meta && Meta->IsValid() && (*Meta)->TryGetObjectField(TEXT("sync"), Sync));
	Test->TestTrue(TEXT("ApplyFile sidecar includes _meta.sync.regions"), Sync && Sync->IsValid() && (*Sync)->TryGetObjectField(TEXT("regions"), Regions));
	if (!Regions || !Regions->IsValid())
	{
		return false;
	}

	for (const FString& RegionId : ExpectedRegionIds)
	{
		const TSharedPtr<FJsonObject>* Region = nullptr;
		Test->TestTrue(FString::Printf(TEXT("Sync state includes %s"), *RegionId), (*Regions)->TryGetObjectField(RegionId, Region));
		if (!Region || !Region->IsValid())
		{
			continue;
		}

		FString SidecarHash;
		FString AssetEvidenceHash;
		Test->TestTrue(FString::Printf(TEXT("%s sidecarHash exists"), *RegionId), (*Region)->TryGetStringField(TEXT("sidecarHash"), SidecarHash));
		Test->TestTrue(FString::Printf(TEXT("%s assetEvidenceHash exists"), *RegionId), (*Region)->TryGetStringField(TEXT("assetEvidenceHash"), AssetEvidenceHash));
		Test->TestFalse(FString::Printf(TEXT("%s sidecarHash is non-empty"), *RegionId), SidecarHash.IsEmpty());
		Test->TestFalse(FString::Printf(TEXT("%s assetEvidenceHash is non-empty"), *RegionId), AssetEvidenceHash.IsEmpty());
		Test->TestEqual(FString::Printf(TEXT("%s sync hashes match"), *RegionId), AssetEvidenceHash, SidecarHash);
	}
	return true;
}

int32 CountBlackboardKeyTypeChildren(UBlackboardData* Blackboard)
{
	int32 Count = 0;
	ForEachObjectWithOuter(Blackboard, [&Count](UObject* Object)
	{
		if (Object && Object->IsA<UBlackboardKeyType>())
		{
			++Count;
		}
	}, true);
	return Count;
}

bool ExpectRepeatedApplyPreservesSingleKeyObject(
	FAutomationTestBase& Test,
	const FString& Target,
	const TSharedRef<FJsonObject>& KeyJson)
{
	TSharedPtr<FJsonObject> Document = MakeBlackboardDataDocument(
		Target,
		MakeBlackboardDataBody(nullptr, {KeyJson}));

	FAssetDocumentService Service;
	const FAssetDocumentResult FirstApplyResult = Service.Apply(MakeApplyRequest(Document));
	Test.TestTrue(TEXT("Initial blackboard apply succeeds"), FirstApplyResult.IsSuccess());
	if (!FirstApplyResult.IsSuccess())
	{
		Test.AddError(FirstApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	Test.TestNotNull(TEXT("Blackboard loads after initial apply"), Blackboard);
	if (!Blackboard || Blackboard->Keys.Num() != 1)
	{
		return false;
	}

	UBlackboardKeyType* FirstKeyType = Blackboard->Keys[0].KeyType;
	const int32 FirstChildCount = CountBlackboardKeyTypeChildren(Blackboard);

	const FAssetDocumentResult SecondApplyResult = Service.Apply(MakeApplyRequest(Document));
	Test.TestTrue(TEXT("Repeated identical blackboard apply succeeds"), SecondApplyResult.IsSuccess());
	if (!SecondApplyResult.IsSuccess())
	{
		Test.AddError(SecondApplyResult.Message);
		return false;
	}

	Test.TestEqual(TEXT("Repeated apply preserves key count"), Blackboard->Keys.Num(), 1);
	if (Blackboard->Keys.Num() == 1)
	{
		Test.TestTrue(TEXT("Repeated apply preserves key type object"), Blackboard->Keys[0].KeyType == FirstKeyType);
	}
	Test.TestEqual(TEXT("Repeated apply does not create extra key type children"), CountBlackboardKeyTypeChildren(Blackboard), FirstChildCount);
	return true;
}

bool ExpectApplyThenDiffUnchangedForSingleKey(
	FAutomationTestBase& Test,
	const FString& Target,
	const TSharedRef<FJsonObject>& KeyJson)
{
	TSharedPtr<FJsonObject> Document = MakeBlackboardDataDocument(
		Target,
		MakeBlackboardDataBody(nullptr, {KeyJson}));

	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(Document));
	Test.TestTrue(TEXT("Initial blackboard apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		Test.AddError(ApplyResult.Message);
		return false;
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	Test.TestTrue(TEXT("Diff against same authored document succeeds"), DiffResult.IsSuccess());
	if (!DiffResult.IsSuccess())
	{
		Test.AddError(DiffResult.Message);
		return false;
	}

	Test.TestTrue(TEXT("Diff against same authored document has no changed or failed entries"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
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
			FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyTypeClass(
				Spec,
				FString::Printf(TEXT("/Body/Keys/%sKey"), *Expectation.Alias),
				ResolvedClass,
				CanonicalType);
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
		TSharedRef<FJsonObject> MissingNameJson = MakeShared<FJsonObject>();
		MissingNameJson->SetStringField(TEXT("Type"), TEXT("Bool"));
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(MissingNameJson, TEXT("/Body/Keys/0"), Spec),
			TEXT("MissingBlackboardKeyName"),
			TEXT("/Body/Keys/0/Name"));
	}

	{
		TSharedRef<FJsonObject> InvalidNameJson = MakeShared<FJsonObject>();
		InvalidNameJson->SetNumberField(TEXT("Name"), 42.0);
		InvalidNameJson->SetStringField(TEXT("Type"), TEXT("Bool"));
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(InvalidNameJson, TEXT("/Body/Keys/1"), Spec),
			TEXT("InvalidBlackboardKeyName"),
			TEXT("/Body/Keys/1/Name"));
	}

	{
		TSharedRef<FJsonObject> EmptyNameJson = MakeBlackboardKeyJson(TEXT(""), TEXT("Bool"));
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(EmptyNameJson, TEXT("/Body/Keys/2"), Spec),
			TEXT("InvalidBlackboardKeyName"),
			TEXT("/Body/Keys/2/Name"));
	}

	{
		TSharedRef<FJsonObject> InvalidDescriptionJson = MakeBlackboardKeyJson(TEXT("BadDescription"), TEXT("Bool"));
		InvalidDescriptionJson->SetNumberField(TEXT("Description"), 42.0);
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(InvalidDescriptionJson, TEXT("/Body/Keys/BadDescription"), Spec),
			TEXT("InvalidBlackboardKeyDescription"),
			TEXT("/Body/Keys/BadDescription/Description"));
	}

	{
		TSharedRef<FJsonObject> InvalidInstanceSyncedJson = MakeBlackboardKeyJson(TEXT("BadSync"), TEXT("Bool"));
		InvalidInstanceSyncedJson->SetStringField(TEXT("bInstanceSynced"), TEXT("true"));
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(InvalidInstanceSyncedJson, TEXT("/Body/Keys/BadSync"), Spec),
			TEXT("InvalidBlackboardKeyInstanceSynced"),
			TEXT("/Body/Keys/BadSync/bInstanceSynced"));
	}

	{
		TSharedRef<FJsonObject> InvalidBaseClassJson = MakeBlackboardKeyJson(TEXT("BadBaseClass"), TEXT("Object"));
		InvalidBaseClassJson->SetBoolField(TEXT("BaseClass"), true);
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(InvalidBaseClassJson, TEXT("/Body/Keys/BadBaseClass"), Spec),
			TEXT("InvalidBlackboardKeyBaseClass"),
			TEXT("/Body/Keys/BadBaseClass/BaseClass"));
	}

	{
		TSharedRef<FJsonObject> InvalidTypeJson = MakeShared<FJsonObject>();
		InvalidTypeJson->SetStringField(TEXT("Name"), TEXT("BadType"));
		InvalidTypeJson->SetNumberField(TEXT("Type"), 42.0);
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(InvalidTypeJson, TEXT("/Body/Keys/BadType"), Spec),
			TEXT("InvalidBlackboardKeyType"),
			TEXT("/Body/Keys/BadType/Type"));
	}

	{
		TSharedRef<FJsonObject> InvalidKeyTypeClassJson = MakeShared<FJsonObject>();
		InvalidKeyTypeClassJson->SetStringField(TEXT("Name"), TEXT("BadKeyTypeClass"));
		InvalidKeyTypeClassJson->SetBoolField(TEXT("KeyTypeClass"), true);
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(InvalidKeyTypeClassJson, TEXT("/Body/Keys/BadKeyTypeClass"), Spec),
			TEXT("InvalidBlackboardKeyType"),
			TEXT("/Body/Keys/BadKeyTypeClass/KeyTypeClass"));
	}

	{
		TSharedRef<FJsonObject> InvalidEnumJson = MakeBlackboardKeyJson(TEXT("BadEnum"), TEXT("Enum"));
		InvalidEnumJson->SetNumberField(TEXT("Enum"), 42.0);
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(InvalidEnumJson, TEXT("/Body/Keys/BadEnum"), Spec),
			TEXT("InvalidBlackboardKeyEnum"),
			TEXT("/Body/Keys/BadEnum/Enum"));
	}

	{
		TSharedRef<FJsonObject> UnknownFieldJson = MakeBlackboardKeyJson(TEXT("Typo"), TEXT("Bool"));
		UnknownFieldJson->SetStringField(TEXT("Typo/Field"), TEXT("Oops"));
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(UnknownFieldJson, TEXT("/Body/Keys/Typo"), Spec),
			TEXT("UnknownBlackboardKeyField"),
			TEXT("/Body/Keys/Typo/Typo~1Field"));
	}

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
			MakeBlackboardKeyJson(
				TEXT("NativeMode"),
				TEXT(""),
				TEXT(""),
				TEXT(""),
				TEXT("/Script/AIModule.BlackboardKeyType_NativeEnum")),
			TEXT("/Body/Keys/NativeMode"),
			Spec);
		TestTrue(TEXT("NativeEnum key parses before validation"), ParseResult.bSuccess);
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(Spec, TEXT("/Body/Keys/NativeMode")),
			TEXT("MissingBlackboardKeyEnum"),
			TEXT("/Body/Keys/NativeMode/Enum"));
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
			FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyTypeClass(Spec, TEXT("/Body/Keys/Broken"), ResolvedClass, CanonicalType),
			TEXT("InvalidBlackboardKeyType"),
			TEXT("/Body/Keys/Broken/Type"));
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			MakeBlackboardKeyJson(TEXT("BrokenCustomPath"), TEXT("Bogus")),
			TEXT("/Custom/Keys/BrokenCustomPath"),
			Spec);
		TestTrue(TEXT("Unknown type parses before path-aware validation"), ParseResult.bSuccess);

		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(Spec, TEXT("/Custom/Keys/BrokenCustomPath")),
			TEXT("InvalidBlackboardKeyType"),
			TEXT("/Custom/Keys/BrokenCustomPath/Type"));
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
		FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyTypeClass(Spec, TEXT("/Body/Keys/ExplicitName"), ResolvedClass, CanonicalType);
	TestTrue(TEXT("Explicit KeyTypeClass resolves"), ResolveResult.bSuccess);
	TestEqual(TEXT("Explicit class is Name key type"), ResolvedClass, UBlackboardKeyType_Name::StaticClass());
	TestEqual(TEXT("Explicit canonical type uses loaded class"), CanonicalType, FString(TEXT("Name")));

	FAssetDocumentBlackboardKeySpec MatchingSpec;
	const FAssetDocumentCapabilityResult MatchingParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
		MakeBlackboardKeyJson(
			TEXT("MatchingExplicitBool"),
			TEXT("Bool"),
			TEXT(""),
			TEXT(""),
			TEXT("/Script/AIModule.BlackboardKeyType_Bool")),
		TEXT("/Body/Keys/MatchingExplicitBool"),
		MatchingSpec);
	TestTrue(TEXT("Matching Type and KeyTypeClass parses"), MatchingParseResult.bSuccess);
	TestTrue(
		TEXT("Matching Type and KeyTypeClass validates"),
		FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(MatchingSpec, TEXT("/Body/Keys/MatchingExplicitBool")).bSuccess);

	FAssetDocumentBlackboardKeySpec ConflictingSpec;
	const FAssetDocumentCapabilityResult ConflictingParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
		MakeBlackboardKeyJson(
			TEXT("ConflictingExplicitBool"),
			TEXT("Bool"),
			TEXT(""),
			TEXT(""),
			TEXT("/Script/AIModule.BlackboardKeyType_Int")),
		TEXT("/Body/Keys/ConflictingExplicitBool"),
		ConflictingSpec);
	TestTrue(TEXT("Conflicting Type and KeyTypeClass parses"), ConflictingParseResult.bSuccess);
	ExpectSingleDiagnostic(
		*this,
		FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(ConflictingSpec, TEXT("/Body/Keys/ConflictingExplicitBool")),
		TEXT("ConflictingBlackboardKeyType"),
		TEXT("/Body/Keys/ConflictingExplicitBool/KeyTypeClass"));

	FAssetDocumentBlackboardKeySpec LoadableSpec;
	const FAssetDocumentCapabilityResult LoadableParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
		MakeBlackboardKeyJson(
			TEXT("ExplicitNativeEnum"),
			TEXT(""),
			TEXT(""),
			TEXT(""),
			TEXT("/Script/AIModule.BlackboardKeyType_NativeEnum")),
		TEXT("/Body/Keys/ExplicitNativeEnum"),
		LoadableSpec);
	TestTrue(TEXT("Loadable explicit KeyTypeClass parses"), LoadableParseResult.bSuccess);

	UClass* LoadableResolvedClass = nullptr;
	FString LoadableCanonicalType;
	const FAssetDocumentCapabilityResult LoadableResolveResult =
		FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyTypeClass(LoadableSpec, TEXT("/Body/Keys/ExplicitNativeEnum"), LoadableResolvedClass, LoadableCanonicalType);
	TestTrue(TEXT("Loadable explicit KeyTypeClass resolves"), LoadableResolveResult.bSuccess);
	TestEqual(TEXT("Loadable explicit class is NativeEnum key type"), LoadableResolvedClass, UBlackboardKeyType_NativeEnum::StaticClass());
	TestEqual(TEXT("Loadable explicit canonical type uses loaded class"), LoadableCanonicalType, FString(TEXT("NativeEnum")));

	FAssetDocumentBlackboardKeySpec InvalidSpec;
	const FAssetDocumentCapabilityResult InvalidParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
		MakeBlackboardKeyJson(
			TEXT("InvalidExplicit"),
			TEXT(""),
			TEXT(""),
			TEXT(""),
			TEXT("/Script/Engine.Actor")),
		TEXT("/Body/Keys/InvalidExplicit"),
		InvalidSpec);
	TestTrue(TEXT("Invalid explicit KeyTypeClass parses"), InvalidParseResult.bSuccess);

	UClass* InvalidResolvedClass = nullptr;
	FString InvalidCanonicalType;
	ExpectSingleDiagnostic(
		*this,
		FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyTypeClass(InvalidSpec, TEXT("/Body/Keys/InvalidExplicit"), InvalidResolvedClass, InvalidCanonicalType),
		TEXT("InvalidBlackboardKeyType"),
		TEXT("/Body/Keys/InvalidExplicit/KeyTypeClass"));

	ExpectSingleDiagnostic(
		*this,
		FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(InvalidSpec, TEXT("/Custom/Keys/InvalidExplicit")),
		TEXT("InvalidBlackboardKeyType"),
		TEXT("/Custom/Keys/InvalidExplicit/KeyTypeClass"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardKeyResolvedMetadataTest,
	"AssetFactory.AssetDocument.BlackboardData.Keys.ResolvedMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardKeyResolvedMetadataTest::RunTest(const FString&)
{
	const FString ActorClassPath = AActor::StaticClass()->GetPathName();
	const FString EnumPath = StaticEnum<EBasicKeyOperation::Type>()->GetPathName();

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			MakeBlackboardKeyJson(TEXT("Target"), TEXT("Object"), ActorClassPath),
			TEXT("/Body/Keys/Target"),
			Spec);
		TestTrue(TEXT("Object metadata key parses"), ParseResult.bSuccess);

		FAssetDocumentBlackboardResolvedKeyMetadata Metadata;
		const FAssetDocumentCapabilityResult MetadataResult =
			FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyMetadata(Spec, TEXT("/Body/Keys/Target"), Metadata);
		TestTrue(TEXT("Object metadata resolves"), MetadataResult.bSuccess);
		TestEqual(TEXT("Object metadata key class"), Metadata.KeyTypeClass, UBlackboardKeyType_Object::StaticClass());
		TestEqual(TEXT("Object metadata canonical type"), Metadata.CanonicalType, FString(TEXT("Object")));
		TestEqual(TEXT("Object metadata base class"), Metadata.BaseClass, AActor::StaticClass());
		TestNull(TEXT("Object metadata enum object"), Metadata.EnumObject);
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			MakeBlackboardKeyJson(TEXT("ChosenClass"), TEXT("Class"), ActorClassPath),
			TEXT("/Body/Keys/ChosenClass"),
			Spec);
		TestTrue(TEXT("Class metadata key parses"), ParseResult.bSuccess);

		FAssetDocumentBlackboardResolvedKeyMetadata Metadata;
		const FAssetDocumentCapabilityResult MetadataResult =
			FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyMetadata(Spec, TEXT("/Body/Keys/ChosenClass"), Metadata);
		TestTrue(TEXT("Class metadata resolves"), MetadataResult.bSuccess);
		TestEqual(TEXT("Class metadata key class"), Metadata.KeyTypeClass, UBlackboardKeyType_Class::StaticClass());
		TestEqual(TEXT("Class metadata base class"), Metadata.BaseClass, AActor::StaticClass());
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			MakeBlackboardKeyJson(TEXT("Mode"), TEXT("Enum"), TEXT(""), EnumPath),
			TEXT("/Body/Keys/Mode"),
			Spec);
		TestTrue(TEXT("Enum metadata key parses"), ParseResult.bSuccess);

		FAssetDocumentBlackboardResolvedKeyMetadata Metadata;
		const FAssetDocumentCapabilityResult MetadataResult =
			FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyMetadata(Spec, TEXT("/Body/Keys/Mode"), Metadata);
		TestTrue(TEXT("Enum metadata resolves"), MetadataResult.bSuccess);
		TestEqual(TEXT("Enum metadata key class"), Metadata.KeyTypeClass, UBlackboardKeyType_Enum::StaticClass());
		TestEqual(TEXT("Enum metadata enum object"), Metadata.EnumObject, static_cast<UObject*>(StaticEnum<EBasicKeyOperation::Type>()));
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			MakeBlackboardKeyJson(
				TEXT("NativeMode"),
				TEXT(""),
				TEXT(""),
				EnumPath,
				TEXT("/Script/AIModule.BlackboardKeyType_NativeEnum")),
			TEXT("/Body/Keys/NativeMode"),
			Spec);
		TestTrue(TEXT("NativeEnum metadata key parses"), ParseResult.bSuccess);
		TestTrue(
			TEXT("NativeEnum explicit key validates with Enum"),
			FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(Spec, TEXT("/Body/Keys/NativeMode")).bSuccess);

		FAssetDocumentBlackboardResolvedKeyMetadata Metadata;
		const FAssetDocumentCapabilityResult MetadataResult =
			FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyMetadata(Spec, TEXT("/Body/Keys/NativeMode"), Metadata);
		TestTrue(TEXT("NativeEnum metadata resolves"), MetadataResult.bSuccess);
		TestEqual(TEXT("NativeEnum metadata key class"), Metadata.KeyTypeClass, UBlackboardKeyType_NativeEnum::StaticClass());
		TestEqual(TEXT("NativeEnum metadata canonical type"), Metadata.CanonicalType, FString(TEXT("NativeEnum")));
		TestEqual(TEXT("NativeEnum metadata enum object"), Metadata.EnumObject, static_cast<UObject*>(StaticEnum<EBasicKeyOperation::Type>()));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardKeyMetadataTest,
	"AssetFactory.AssetDocument.BlackboardData.Keys.Metadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardKeyMetadataTest::RunTest(const FString&)
{
	FAssetDocumentBlackboardKeySpec Spec;
	const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
		MakeBlackboardKeyJson(
			TEXT("Target"),
			TEXT("Bool"),
			TEXT(""),
			TEXT(""),
			TEXT(""),
			TEXT("Primary target actor")),
		TEXT("/Body/Keys/Target"),
		Spec);
	TestTrue(TEXT("Key with Description parses"), ParseResult.bSuccess);
	TestEqual(TEXT("Spec preserves Description"), Spec.Description, FString(TEXT("Primary target actor")));
	TestTrue(TEXT("Canonical JSON is valid"), Spec.CanonicalJson.IsValid());
	if (Spec.CanonicalJson.IsValid())
	{
		TestEqual(
			TEXT("Canonical JSON preserves Description"),
			Spec.CanonicalJson->GetStringField(TEXT("Description")),
			FString(TEXT("Primary target actor")));
	}

	FBlackboardEntry Entry;
	Entry.EntryName = TEXT("Target");
	Entry.KeyType = NewObject<UBlackboardKeyType_Bool>(GetTransientPackage());
	Entry.EntryDescription = TEXT("Primary target actor");
	const TSharedRef<FJsonObject> Extracted = FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(Entry);
	TestEqual(
		TEXT("Extracted key preserves Description"),
		Extracted->GetStringField(TEXT("Description")),
		FString(TEXT("Primary target actor")));

	UBlackboardKeyType_NativeEnum* NativeEnumKey = NewObject<UBlackboardKeyType_NativeEnum>(GetTransientPackage());
	NativeEnumKey->EnumType = StaticEnum<EBasicKeyOperation::Type>();
	NativeEnumKey->EnumName = NativeEnumKey->EnumType ? NativeEnumKey->EnumType->GetPathName() : TEXT("");

	FBlackboardEntry NativeEnumEntry;
	NativeEnumEntry.EntryName = TEXT("NativeMode");
	NativeEnumEntry.KeyType = NativeEnumKey;
	const TSharedRef<FJsonObject> ExtractedNativeEnum = FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(NativeEnumEntry);
	TestEqual(TEXT("Extracted NativeEnum key type class"), ExtractedNativeEnum->GetStringField(TEXT("KeyTypeClass")), FString(TEXT("/Script/AIModule.BlackboardKeyType_NativeEnum")));
	TestEqual(TEXT("Extracted NativeEnum preserves Enum"), ExtractedNativeEnum->GetStringField(TEXT("Enum")), NativeEnumKey->EnumName);

	FAssetDocumentBlackboardKeySpec ExtractedNativeEnumSpec;
	const FAssetDocumentCapabilityResult ExtractedNativeEnumParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
		ExtractedNativeEnum,
		TEXT("/Body/Keys/NativeMode"),
		ExtractedNativeEnumSpec);
	TestTrue(TEXT("Extracted NativeEnum parses"), ExtractedNativeEnumParseResult.bSuccess);
	TestTrue(
		TEXT("Extracted NativeEnum validates"),
		FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(ExtractedNativeEnumSpec, TEXT("/Body/Keys/NativeMode")).bSuccess);

	FBlackboardEntry StructEntry;
	StructEntry.EntryName = TEXT("Payload");
	StructEntry.KeyType = NewObject<UBlackboardKeyType_Struct>(GetTransientPackage());
	const TSharedRef<FJsonObject> ExtractedStruct = FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(StructEntry);
	TestFalse(TEXT("Extracted Struct does not use alias Type"), ExtractedStruct->HasField(TEXT("Type")));
	TestEqual(
		TEXT("Extracted Struct key type class"),
		ExtractedStruct->GetStringField(TEXT("KeyTypeClass")),
		FString(TEXT("/Script/AIModule.BlackboardKeyType_Struct")));

	FAssetDocumentBlackboardKeySpec ExtractedStructSpec;
	const FAssetDocumentCapabilityResult ExtractedStructParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
		ExtractedStruct,
		TEXT("/Body/Keys/Payload"),
		ExtractedStructSpec);
	TestTrue(TEXT("Extracted Struct parses"), ExtractedStructParseResult.bSuccess);
	TestTrue(
		TEXT("Extracted Struct validates"),
		FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(ExtractedStructSpec, TEXT("/Body/Keys/Payload")).bSuccess);

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
	const TArray<FAssetDocumentBlackboardKeySpec> Specs = ParseKeysForTest(*this, Keys);
	ExpectSingleDiagnostic(
		*this,
		FAssetDocumentBlackboardKeySchemaUtils::ValidateUniqueLocalKeys(Specs, TEXT("/Body/Keys")),
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

	UBlackboardData* CycleA = NewObject<UBlackboardData>(GetTransientPackage(), TEXT("BB_CycleA"));
	UBlackboardData* CycleB = NewObject<UBlackboardData>(GetTransientPackage(), TEXT("BB_CycleB"));
	CycleA->Parent = CycleB;
	CycleB->Parent = CycleA;

	TMap<FName, FAssetDocumentBlackboardKeyLookupEntry> CycleLookup;
	ExpectSingleDiagnostic(
		*this,
		FAssetDocumentBlackboardKeySchemaUtils::BuildLookup(CycleA, CycleLookup),
		TEXT("BlackboardParentCycle"),
		TEXT("/Body/Parent"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataParentInheritanceTest,
	"AssetFactory.AssetDocument.BlackboardData.ParentInheritance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataParentInheritanceTest::RunTest(const FString&)
{
	const FString ParentTarget = TEXT("/Game/AssetDocumentTests/BB_AD_ParentInheritance_Parent");
	const FString ChildTarget = TEXT("/Game/AssetDocumentTests/BB_AD_ParentInheritance");

	FAssetDocumentService Service;
	const FAssetDocumentResult ParentApplyResult = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		ParentTarget,
		MakeBlackboardDataBody(nullptr, {MakeBlackboardKeyJson(TEXT("InheritedTarget"), TEXT("Bool"))}))));
	TestTrue(TEXT("Parent blackboard apply succeeds"), ParentApplyResult.IsSuccess());
	if (!ParentApplyResult.IsSuccess())
	{
		AddError(ParentApplyResult.Message);
		return false;
	}

	const FAssetDocumentResult ChildApplyResult = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		ChildTarget,
		MakeBlackboardDataBody(
			MakeAssetRef(ParentTarget),
			{MakeBlackboardKeyJson(TEXT("LocalCount"), TEXT("Int"))}))));
	TestTrue(TEXT("Child blackboard apply succeeds"), ChildApplyResult.IsSuccess());
	if (!ChildApplyResult.IsSuccess())
	{
		AddError(ChildApplyResult.Message);
		return false;
	}

	UBlackboardData* ChildBlackboard = LoadBlackboardForTarget(ChildTarget);
	TestNotNull(TEXT("Child blackboard loads"), ChildBlackboard);
	if (!ChildBlackboard)
	{
		return false;
	}
	TestTrue(TEXT("Child parent is applied"), ChildBlackboard->Parent.Get() == LoadBlackboardForTarget(ParentTarget));

	TMap<FName, FAssetDocumentBlackboardKeyLookupEntry> Lookup;
	const FAssetDocumentCapabilityResult LookupResult = FAssetDocumentBlackboardKeySchemaUtils::BuildLookup(ChildBlackboard, Lookup);
	TestTrue(TEXT("Profile-applied parent lookup builds"), LookupResult.bSuccess);

	FAssetDocumentBlackboardKeyLookupEntry InheritedEntry;
	TestTrue(TEXT("Lookup sees inherited key through parent"), FAssetDocumentBlackboardKeySchemaUtils::FindKeyInLookup(Lookup, TEXT("InheritedTarget"), InheritedEntry));
	TestTrue(TEXT("Inherited key is marked inherited"), InheritedEntry.bInherited);

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = ChildTarget;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Child extract succeeds"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedKeys = nullptr;
	TestTrue(TEXT("Extracted child body has Keys"), ExtractedBody.IsValid() && ExtractedBody->TryGetArrayField(TEXT("Keys"), ExtractedKeys));
	TestEqual(TEXT("Child extraction includes local keys only"), ExtractedKeys ? ExtractedKeys->Num() : -1, 1);
	if (ExtractedKeys && ExtractedKeys->Num() == 1 && (*ExtractedKeys)[0].IsValid() && (*ExtractedKeys)[0]->Type == EJson::Object)
	{
		TestEqual(TEXT("Extracted local key name"), (*ExtractedKeys)[0]->AsObject()->GetStringField(TEXT("Name")), FString(TEXT("LocalCount")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataApplyExtractDiffTest,
	"AssetFactory.AssetDocument.BlackboardData.ApplyExtractDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataApplyExtractDiffTest::RunTest(const FString&)
{
	const FString ParentTarget = TEXT("/Game/AssetDocumentTests/BB_AD_ApplyExtractDiff_Parent");
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_ApplyExtractDiff");
	FAssetDocumentService Service;
	const FAssetDocumentResult ParentApplyResult = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		ParentTarget,
		MakeBlackboardDataBody(nullptr, {MakeBlackboardKeyJson(TEXT("InheritedTeam"), TEXT("Name"))}))));
	TestTrue(TEXT("Roundtrip parent blackboard apply succeeds"), ParentApplyResult.IsSuccess());
	if (!ParentApplyResult.IsSuccess())
	{
		AddError(ParentApplyResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> Body = MakeBlackboardDataBody(MakeAssetRef(ParentTarget), {
		MakeBlackboardKeyJson(TEXT("Target"), TEXT("Object"), AActor::StaticClass()->GetPathName(), TEXT(""), TEXT(""), TEXT("Current target")),
		MakeBlackboardKeyJson(TEXT("IsVisible"), TEXT("Bool")),
	});
	Body->GetArrayField(TEXT("Keys"))[1]->AsObject()->SetBoolField(TEXT("bInstanceSynced"), true);

	TSharedPtr<FJsonObject> Document = MakeBlackboardDataDocument(Target, Body);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(Document));
	TestTrue(TEXT("Blackboard apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	TestNotNull(TEXT("Blackboard loads"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}
	TestEqual(TEXT("Blackboard has authored key count"), Blackboard->Keys.Num(), 2);
	if (Blackboard->Keys.Num() == 2)
	{
		TestEqual(TEXT("Authored key order is preserved"), Blackboard->Keys[0].EntryName, FName(TEXT("Target")));
		const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Blackboard->Keys[0].KeyType);
		TestNotNull(TEXT("Target key is Object key type"), ObjectKey);
		if (ObjectKey)
		{
			TestTrue(TEXT("Object key BaseClass is applied"), ObjectKey->BaseClass.Get() == AActor::StaticClass());
		}
		TestEqual(TEXT("Description is applied"), Blackboard->Keys[0].EntryDescription, FString(TEXT("Current target")));
		TestTrue(TEXT("bInstanceSynced is applied"), Blackboard->Keys[1].bInstanceSynced);
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Blackboard extract succeeds"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TSharedPtr<FJsonObject>* ExtractedParent = nullptr;
	TestTrue(TEXT("Extracted body has Parent"), ExtractedBody.IsValid() && ExtractedBody->TryGetObjectField(TEXT("Parent"), ExtractedParent));
	if (ExtractedParent && ExtractedParent->IsValid())
	{
		TestEqual(TEXT("Extracted Parent is AssetRef"), (*ExtractedParent)->GetStringField(TEXT("Kind")), FString(TEXT("AssetRef")));
		TestEqual(TEXT("Extracted Parent path is canonical object path"), (*ExtractedParent)->GetStringField(TEXT("Path")), MakeObjectPathFromTarget(ParentTarget));
	}
	const TArray<TSharedPtr<FJsonValue>>* ExtractedKeys = nullptr;
	TestTrue(TEXT("Extracted body has Keys"), ExtractedBody.IsValid() && ExtractedBody->TryGetArrayField(TEXT("Keys"), ExtractedKeys));
	TestEqual(TEXT("Extracted key count"), ExtractedKeys ? ExtractedKeys->Num() : -1, 2);

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Blackboard diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("Apply/extract diff is unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataRepeatedApplyPreservesKeyObjectsTest,
	"AssetFactory.AssetDocument.BlackboardData.RepeatedApplyPreservesKeyObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataRepeatedApplyPreservesKeyObjectsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_RepeatedApplyPreservesKeyObjects");
	TSharedPtr<FJsonObject> Body = MakeBlackboardDataBody(nullptr, {
		MakeBlackboardKeyJson(TEXT("Target"), TEXT("Bool")),
		MakeBlackboardKeyJson(TEXT("Count"), TEXT("Int")),
	});
	TSharedPtr<FJsonObject> Document = MakeBlackboardDataDocument(Target, Body);

	FAssetDocumentService Service;
	const FAssetDocumentResult FirstApplyResult = Service.Apply(MakeApplyRequest(Document));
	TestTrue(TEXT("Initial blackboard apply succeeds"), FirstApplyResult.IsSuccess());
	if (!FirstApplyResult.IsSuccess())
	{
		AddError(FirstApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	TestNotNull(TEXT("Blackboard loads after initial apply"), Blackboard);
	if (!Blackboard || Blackboard->Keys.Num() != 2)
	{
		return false;
	}

	UBlackboardKeyType* FirstTargetKeyType = Blackboard->Keys[0].KeyType;
	UBlackboardKeyType* FirstCountKeyType = Blackboard->Keys[1].KeyType;
	const int32 FirstChildCount = CountBlackboardKeyTypeChildren(Blackboard);

	const FAssetDocumentResult SecondApplyResult = Service.Apply(MakeApplyRequest(Document));
	TestTrue(TEXT("Repeated identical blackboard apply succeeds"), SecondApplyResult.IsSuccess());
	if (!SecondApplyResult.IsSuccess())
	{
		AddError(SecondApplyResult.Message);
		return false;
	}

	TestEqual(TEXT("Repeated apply preserves key count"), Blackboard->Keys.Num(), 2);
	if (Blackboard->Keys.Num() == 2)
	{
		TestTrue(TEXT("Repeated apply preserves first key type object"), Blackboard->Keys[0].KeyType == FirstTargetKeyType);
		TestTrue(TEXT("Repeated apply preserves second key type object"), Blackboard->Keys[1].KeyType == FirstCountKeyType);
	}
	TestEqual(TEXT("Repeated apply does not create extra key type children"), CountBlackboardKeyTypeChildren(Blackboard), FirstChildCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataRepeatedApplyPreservesExplicitKeyTypeClassTest,
	"AssetFactory.AssetDocument.BlackboardData.RepeatedApplyPreservesExplicitKeyTypeClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataRepeatedApplyPreservesExplicitKeyTypeClassTest::RunTest(const FString&)
{
	return ExpectRepeatedApplyPreservesSingleKeyObject(
		*this,
		TEXT("/Game/AssetDocumentTests/BB_AD_RepeatedApplyPreservesExplicitKeyTypeClass"),
		MakeBlackboardKeyJson(
			TEXT("ExplicitName"),
			TEXT(""),
			TEXT(""),
			TEXT(""),
			TEXT("/Script/AIModule.BlackboardKeyType_Name")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataRepeatedApplyPreservesMatchingTypeAndKeyTypeClassTest,
	"AssetFactory.AssetDocument.BlackboardData.RepeatedApplyPreservesMatchingTypeAndKeyTypeClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataRepeatedApplyPreservesMatchingTypeAndKeyTypeClassTest::RunTest(const FString&)
{
	return ExpectRepeatedApplyPreservesSingleKeyObject(
		*this,
		TEXT("/Game/AssetDocumentTests/BB_AD_RepeatedApplyPreservesMatchingTypeAndKeyTypeClass"),
		MakeBlackboardKeyJson(
			TEXT("ExplicitName"),
			TEXT("Name"),
			TEXT(""),
			TEXT(""),
			TEXT("/Script/AIModule.BlackboardKeyType_Name")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataDiffUnchangedForExplicitKeyTypeClassTest,
	"AssetFactory.AssetDocument.BlackboardData.DiffUnchangedForExplicitKeyTypeClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataDiffUnchangedForExplicitKeyTypeClassTest::RunTest(const FString&)
{
	return ExpectApplyThenDiffUnchangedForSingleKey(
		*this,
		TEXT("/Game/AssetDocumentTests/BB_AD_DiffUnchangedForExplicitKeyTypeClass"),
		MakeBlackboardKeyJson(
			TEXT("ExplicitName"),
			TEXT(""),
			TEXT(""),
			TEXT(""),
			TEXT("/Script/AIModule.BlackboardKeyType_Name")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataDiffUnchangedForMatchingTypeAndKeyTypeClassTest,
	"AssetFactory.AssetDocument.BlackboardData.DiffUnchangedForMatchingTypeAndKeyTypeClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataDiffUnchangedForMatchingTypeAndKeyTypeClassTest::RunTest(const FString&)
{
	return ExpectApplyThenDiffUnchangedForSingleKey(
		*this,
		TEXT("/Game/AssetDocumentTests/BB_AD_DiffUnchangedForMatchingTypeAndKeyTypeClass"),
		MakeBlackboardKeyJson(
			TEXT("ExplicitName"),
			TEXT("Name"),
			TEXT(""),
			TEXT(""),
			TEXT("/Script/AIModule.BlackboardKeyType_Name")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataDryRunDoesNotMaterializeKeysTest,
	"AssetFactory.AssetDocument.BlackboardData.DryRunDoesNotMaterializeKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataDryRunDoesNotMaterializeKeysTest::RunTest(const FString&)
{
	UBlackboardData* Blackboard = NewObject<UBlackboardData>(GetTransientPackage(), TEXT("BB_AD_DryRunDoesNotMaterializeKeys"));
	TestNotNull(TEXT("Transient blackboard creates"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}

	TSharedPtr<FJsonObject> Body = MakeBlackboardDataBody(nullptr, {
		MakeBlackboardKeyJson(TEXT("DryRunKey"), TEXT("Bool")),
	});

	FBlackboardDataAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.Asset = Blackboard;
	Context.AssetClass = UBlackboardData::StaticClass();
	Context.TargetAssetPath = TEXT("/Game/AssetDocumentTests/BB_AD_DryRunDoesNotMaterializeKeys");
	Context.bIsDryRun = true;

	const int32 KeyCountBefore = Blackboard->Keys.Num();
	const int32 ChildCountBefore = CountBlackboardKeyTypeChildren(Blackboard);
	const FAssetDocumentCapabilityResult Result = Capability.Apply(Context, MakeShared<FJsonValueObject>(Body.ToSharedRef()));

	TestTrue(TEXT("Dry-run apply succeeds"), Result.bSuccess);
	TestEqual(TEXT("Dry-run does not mutate Keys"), Blackboard->Keys.Num(), KeyCountBefore);
	TestEqual(TEXT("Dry-run does not create key type children"), CountBlackboardKeyTypeChildren(Blackboard), ChildCountBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataDiffReportsKeyReorderTest,
	"AssetFactory.AssetDocument.BlackboardData.DiffReportsKeyReorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataDiffReportsKeyReorderTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_DiffReportsKeyReorder");
	TSharedPtr<FJsonObject> InitialBody = MakeBlackboardDataBody(nullptr, {
		MakeBlackboardKeyJson(TEXT("A"), TEXT("Bool")),
		MakeBlackboardKeyJson(TEXT("B"), TEXT("Int")),
	});

	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(Target, InitialBody)));
	TestTrue(TEXT("Initial ordered apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> ReorderedBody = MakeBlackboardDataBody(nullptr, {
		MakeBlackboardKeyJson(TEXT("B"), TEXT("Int")),
		MakeBlackboardKeyJson(TEXT("A"), TEXT("Bool")),
	});
	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = MakeBlackboardDataDocument(Target, ReorderedBody);
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);

	TestTrue(TEXT("Reorder diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("Reorder diff reports Body.Keys changed"), DiffPayloadHasChangedPath(DiffResult.Payload, TEXT("/Body/Keys")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataRejectsParentSelfCycleTest,
	"AssetFactory.AssetDocument.BlackboardData.RejectsParentSelfCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataRejectsParentSelfCycleTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_RejectsParentSelfCycle");
	FAssetDocumentService Service;
	const FAssetDocumentResult InitialResult = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		Target,
		MakeBlackboardDataBody(nullptr, {MakeBlackboardKeyJson(TEXT("Local"), TEXT("Bool"))}))));
	TestTrue(TEXT("Initial self-cycle fixture apply succeeds"), InitialResult.IsSuccess());
	if (!InitialResult.IsSuccess())
	{
		AddError(InitialResult.Message);
		return false;
	}

	const FAssetDocumentResult SelfParentResult = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		Target,
		MakeBlackboardDataBody(MakeAssetRef(Target), {MakeBlackboardKeyJson(TEXT("Local"), TEXT("Bool"))}))));

	TestFalse(TEXT("Self-parent apply fails"), SelfParentResult.IsSuccess());
	TestTrue(TEXT("Self-parent reports cycle diagnostic"), ResultHasDiagnostic(SelfParentResult, TEXT("BlackboardParentCycle"), TEXT("/Body/Parent")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataRejectsParentChainCycleTest,
	"AssetFactory.AssetDocument.BlackboardData.RejectsParentChainCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataRejectsParentChainCycleTest::RunTest(const FString&)
{
	const FString ParentTarget = TEXT("/Game/AssetDocumentTests/BB_AD_RejectsParentChainCycle_Parent");
	const FString ChildTarget = TEXT("/Game/AssetDocumentTests/BB_AD_RejectsParentChainCycle");
	FAssetDocumentService Service;

	const FAssetDocumentResult ParentResult = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		ParentTarget,
		MakeBlackboardDataBody(nullptr, {MakeBlackboardKeyJson(TEXT("ParentKey"), TEXT("Bool"))}))));
	TestTrue(TEXT("Parent fixture apply succeeds"), ParentResult.IsSuccess());
	if (!ParentResult.IsSuccess())
	{
		AddError(ParentResult.Message);
		return false;
	}

	const FAssetDocumentResult ChildResult = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		ChildTarget,
		MakeBlackboardDataBody(MakeAssetRef(ParentTarget), {MakeBlackboardKeyJson(TEXT("ChildKey"), TEXT("Int"))}))));
	TestTrue(TEXT("Child fixture apply succeeds"), ChildResult.IsSuccess());
	if (!ChildResult.IsSuccess())
	{
		AddError(ChildResult.Message);
		return false;
	}

	const FAssetDocumentResult CycleResult = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		ParentTarget,
		MakeBlackboardDataBody(MakeAssetRef(ChildTarget), {MakeBlackboardKeyJson(TEXT("ParentKey"), TEXT("Bool"))}))));

	TestFalse(TEXT("Parent-chain cycle apply fails"), CycleResult.IsSuccess());
	TestTrue(TEXT("Parent-chain cycle reports diagnostic"), ResultHasDiagnostic(CycleResult, TEXT("BlackboardParentCycle"), TEXT("/Body/Parent")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataRejectsDuplicateKeysTest,
	"AssetFactory.AssetDocument.BlackboardData.RejectsDuplicateKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataRejectsDuplicateKeysTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_RejectsDuplicateKeys");
	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		Target,
		MakeBlackboardDataBody(nullptr, {
			MakeBlackboardKeyJson(TEXT("Target"), TEXT("Bool")),
			MakeBlackboardKeyJson(TEXT("Target"), TEXT("Int")),
		}))));
	TestFalse(TEXT("Duplicate blackboard key apply fails"), Result.IsSuccess());
	TestTrue(TEXT("Duplicate failure has exact diagnostic"), ResultHasDiagnostic(Result, TEXT("DuplicateBlackboardKey"), TEXT("/Body/Keys/1/Name")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataRejectsInvalidKeyTypeTest,
	"AssetFactory.AssetDocument.BlackboardData.RejectsInvalidKeyType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataRejectsInvalidKeyTypeTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_RejectsInvalidKeyType");
	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		Target,
		MakeBlackboardDataBody(nullptr, {MakeBlackboardKeyJson(TEXT("Broken"), TEXT("Bogus"))}))));
	TestFalse(TEXT("Invalid blackboard key type apply fails"), Result.IsSuccess());
	TestTrue(TEXT("Invalid type failure has exact diagnostic"), ResultHasDiagnostic(Result, TEXT("InvalidBlackboardKeyType"), TEXT("/Body/Keys/Broken/Type")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataApplyFileCanonicalWritebackTest,
	"AssetFactory.AssetDocument.BlackboardData.ApplyFileCanonicalWriteback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataApplyFileCanonicalWritebackTest::RunTest(const FString&)
{
	const FString ParentTarget = TEXT("/Game/AssetDocumentTests/BB_AD_Task10_ApplyFileWriteback_Parent");
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_Task10_ApplyFileWriteback");
	FAssetDocumentService Service;
	const FAssetDocumentResult ParentApplyResult = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		ParentTarget,
		MakeBlackboardDataBody(nullptr, {
			MakeBlackboardKeyJson(TEXT("InheritedTarget"), TEXT("Object"), AActor::StaticClass()->GetPathName()),
			MakeBlackboardKeyJson(TEXT("InheritedGate"), TEXT("Bool"),
				TEXT(""),
				TEXT(""),
				TEXT(""),
				TEXT("Inherited condition")),
		}))));
	TestTrue(TEXT("ApplyFile parent blackboard apply succeeds"), ParentApplyResult.IsSuccess());
	if (!ParentApplyResult.IsSuccess())
	{
		AddError(ParentApplyResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> Body = MakeBlackboardDataBody(MakeAssetRef(MakeObjectPathFromTarget(ParentTarget)), {
		MakeBlackboardKeyJson(TEXT("TargetActor"), TEXT("Object"), AActor::StaticClass()->GetPathName(), TEXT(""), TEXT(""), TEXT("Local target")),
		MakeBlackboardKeyJson(TEXT("MoveLocation"), TEXT("Vector")),
		MakeBlackboardKeyJson(TEXT("AlertName"), TEXT("Name")),
	});
	Body->GetArrayField(TEXT("Keys"))[2]->AsObject()->SetBoolField(TEXT("bInstanceSynced"), true);
	TSharedPtr<FJsonObject> Document = MakeBlackboardDataDocument(Target, Body);
	const FString SidecarPath = FAssetDocumentSidecar::ResolveSidecarPathFromObjectPath(Target);
	if (!WriteSidecarJson(this, SidecarPath, Document))
	{
		return false;
	}

	FAssetDocumentValidateRequest ValidateRequest;
	ValidateRequest.FilePath = SidecarPath;
	const FAssetDocumentResult ValidateResult = Service.Validate(ValidateRequest);
	TestTrue(TEXT("BlackboardData ApplyFile sidecar validates"), ValidateResult.IsSuccess());
	if (!ValidateResult.IsSuccess())
	{
		AddError(ValidateResult.Message);
		return false;
	}

	FAssetDocumentApplyFileRequest ApplyFileRequest;
	ApplyFileRequest.FilePath = SidecarPath;
	ApplyFileRequest.bSaveAsset = true;
	const FAssetDocumentResult ApplyFileResult = Service.ApplyFile(ApplyFileRequest);
	TestTrue(TEXT("BlackboardData ApplyFile succeeds"), ApplyFileResult.IsSuccess());
	TestTrue(TEXT("BlackboardData ApplyFile writes sidecar sync state"), ApplyFileResult.bWroteSidecar);
	if (ApplyFileResult.Payload.IsValid())
	{
		FString SkipReason;
		if (ApplyFileResult.Payload->TryGetStringField(TEXT("sidecar_sync_update_skip_reason"), SkipReason))
		{
			AddError(FString::Printf(TEXT("BlackboardData sidecar sync update skip reason: %s"), *SkipReason));
		}
		TestFalse(TEXT("BlackboardData ApplyFile does not skip sync update"), ApplyFileResult.Payload->HasField(TEXT("sidecar_sync_update_skipped")));
	}
	if (!ApplyFileResult.IsSuccess())
	{
		AddError(ApplyFileResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	TestNotNull(TEXT("ApplyFile-created blackboard loads"), Blackboard);
	if (Blackboard)
	{
		TestTrue(TEXT("ApplyFile applies parent"), Blackboard->Parent.Get() == LoadBlackboardForTarget(ParentTarget));
		TestEqual(TEXT("ApplyFile applies local key count"), Blackboard->Keys.Num(), 3);
	}

	TSharedPtr<FJsonObject> ReloadedSidecar;
	if (!LoadSidecarJson(this, SidecarPath, ReloadedSidecar))
	{
		return false;
	}
	ExpectSyncRegions(this, ReloadedSidecar, {
		TEXT("Body.Parent"),
		TEXT("Body.Keys"),
	});

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.FilePath = SidecarPath;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("BlackboardData ApplyFile diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("BlackboardData ApplyFile diff has no changed or failed entries"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

#endif
