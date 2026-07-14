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
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/EngineTypes.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectIterator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const FAssetDocumentRegionPolicy* BlackboardTestFindPolicyByRegionId(const TArray<FAssetDocumentRegionPolicy>& Policies, FName RegionId)
{
	return Policies.FindByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
	{
		return Policy.RegionId == RegionId;
	});
}

TSharedRef<FJsonObject> BlackboardTestMakeBlackboardKeyJson(
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

FString BlackboardTestMakeObjectPathFromTarget(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

UBlackboardData* LoadBlackboardForTarget(const FString& Target)
{
	return LoadObject<UBlackboardData>(nullptr, *BlackboardTestMakeObjectPathFromTarget(Target));
}

bool BlackboardTestIsEngineDerivedSelfKey(const FBlackboardEntry& Entry)
{
	const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Entry.KeyType);
	return Entry.EntryName == FBlackboard::KeySelf
		&& ObjectKey
		&& ObjectKey->GetClass() == UBlackboardKeyType_Object::StaticClass()
		&& ObjectKey->BaseClass == AActor::StaticClass()
		&& ObjectKey->DefaultValue == nullptr
		&& Entry.EntryDescription.IsEmpty()
		&& Entry.EntryCategory.IsNone()
		&& !Entry.bInstanceSynced;
}

TArray<const FBlackboardEntry*> BlackboardTestGetAuthoredKeys(const UBlackboardData* Blackboard)
{
	TArray<const FBlackboardEntry*> AuthoredKeys;
	if (!Blackboard)
	{
		return AuthoredKeys;
	}

	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		if (!BlackboardTestIsEngineDerivedSelfKey(Entry))
		{
			AuthoredKeys.Add(&Entry);
		}
	}
	return AuthoredKeys;
}

TSharedRef<FJsonObject> BlackboardTestExtractKeyChecked(
	FAutomationTestBase& Test,
	const FBlackboardEntry& Entry,
	const FString& Path)
{
	TSharedPtr<FJsonObject> Extracted;
	const FAssetDocumentCapabilityResult Result = FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(
		Entry,
		Path,
		Extracted);
	Test.TestTrue(FString::Printf(TEXT("Key %s extracts"), *Entry.EntryName.ToString()), Result.bSuccess);
	if (!Result.bSuccess)
	{
		for (const FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
		{
			Test.AddError(FString::Printf(TEXT("%s at %s: %s"), *Diagnostic.Code, *Diagnostic.Path, *Diagnostic.Message));
		}
	}
	return Extracted.IsValid() ? Extracted.ToSharedRef() : MakeShared<FJsonObject>();
}

TSharedPtr<FJsonObject> BlackboardTestMakeAssetRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	Fragment->SetStringField(TEXT("Path"), Path);
	return Fragment;
}

TSharedPtr<FJsonObject> BlackboardTestMakeClassRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	Fragment->SetStringField(TEXT("Path"), Path);
	return Fragment;
}

TSharedRef<FJsonObject> BlackboardTestMakeCanonicalKey(
	const FString& Name,
	UClass* KeyTypeClass,
	const TSharedRef<FJsonObject>& KeyTypeProperties)
{
	TSharedRef<FJsonObject> Key = MakeShared<FJsonObject>();
	Key->SetStringField(TEXT("Name"), Name);
	Key->SetObjectField(TEXT("KeyTypeClass"), BlackboardTestMakeClassRef(KeyTypeClass->GetPathName()));
	Key->SetObjectField(TEXT("KeyTypeProperties"), KeyTypeProperties);
	return Key;
}

TSharedRef<FJsonObject> BlackboardTestMakeStructValue(
	UScriptStruct* Struct,
	const TSharedRef<FJsonObject>& Properties)
{
	TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
	Value->SetStringField(TEXT("Struct"), Struct->GetPathName());
	Value->SetObjectField(TEXT("Properties"), Properties);
	return Value;
}

TSharedPtr<FJsonObject> BlackboardTestGetKeyTypeProperties(const TSharedPtr<FJsonObject>& Key)
{
	const TSharedPtr<FJsonObject>* Properties = nullptr;
	return Key.IsValid() && Key->TryGetObjectField(TEXT("KeyTypeProperties"), Properties) && Properties && Properties->IsValid()
		? *Properties
		: nullptr;
}

UClass* BlackboardTestCreateCustomKeyTypeClass(FAutomationTestBase& Test, const FString& AssetName)
{
	const FString PackageName = FString::Printf(TEXT("/Game/AssetDocumentTests/%s"), *AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	Test.TestNotNull(TEXT("Custom key type package is created"), Package);
	if (!Package)
	{
		return nullptr;
	}

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		UBlackboardKeyType_Int::StaticClass(),
		Package,
		FName(*AssetName),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("AssetDocumentBlackboardDataTests")));
	Test.TestNotNull(TEXT("Custom key type Blueprint is created"), Blueprint);
	if (!Blueprint)
	{
		return nullptr;
	}

	FEdGraphPinType StringType;
	StringType.PinCategory = UEdGraphSchema_K2::PC_String;
	Test.TestTrue(
		TEXT("CustomLabel reflected property is added"),
		FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("CustomLabel"), StringType, TEXT("")));

	FEdGraphPinType FloatType;
	FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
	FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	Test.TestTrue(
		TEXT("CustomWeight reflected property is added"),
		FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("CustomWeight"), FloatType, TEXT("0.0")));

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Test.TestNotNull(TEXT("Custom key type Blueprint generated class exists"), Blueprint->GeneratedClass.Get());
	return Blueprint->GeneratedClass;
}

TSharedPtr<FJsonObject> BlackboardTestMakeBlackboardDataDocument(const FString& Target, TSharedPtr<FJsonObject> Body)
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

TSharedPtr<FJsonObject> BlackboardTestMakeBlackboardDataBody(TSharedPtr<FJsonObject> Parent, TArray<TSharedRef<FJsonObject>> Keys)
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

FAssetDocumentApplyRequest BlackboardTestMakeApplyRequest(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	return Request;
}

bool BlackboardTestResultHasDiagnostic(const FAssetDocumentResult& Result, const FString& ExpectedCode, const FString& ExpectedPath)
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

bool BlackboardTestDiffPayloadHasNoChangedOrFailedEntries(const TSharedPtr<FJsonObject>& Payload)
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

bool BlackboardTestWriteSidecarJson(FAutomationTestBase* Test, const FString& SidecarPath, const TSharedPtr<FJsonObject>& Document)
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

bool BlackboardTestLoadSidecarJson(FAutomationTestBase* Test, const FString& SidecarPath, TSharedPtr<FJsonObject>& OutDocument)
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

bool BlackboardTestExpectSyncRegions(
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
	TSharedPtr<FJsonObject> Document = BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(nullptr, {KeyJson}));

	FAssetDocumentService Service;
	const FAssetDocumentResult FirstApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(Document));
	Test.TestTrue(TEXT("Initial blackboard apply succeeds"), FirstApplyResult.IsSuccess());
	if (!FirstApplyResult.IsSuccess())
	{
		Test.AddError(FirstApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	Test.TestNotNull(TEXT("Blackboard loads after initial apply"), Blackboard);
	TArray<const FBlackboardEntry*> AuthoredKeys = BlackboardTestGetAuthoredKeys(Blackboard);
	if (AuthoredKeys.Num() != 1)
	{
		return false;
	}

	UBlackboardKeyType* FirstKeyType = AuthoredKeys[0]->KeyType;
	const int32 FirstChildCount = CountBlackboardKeyTypeChildren(Blackboard);

	const FAssetDocumentResult SecondApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(Document));
	Test.TestTrue(TEXT("Repeated identical blackboard apply succeeds"), SecondApplyResult.IsSuccess());
	if (!SecondApplyResult.IsSuccess())
	{
		Test.AddError(SecondApplyResult.Message);
		return false;
	}

	AuthoredKeys = BlackboardTestGetAuthoredKeys(Blackboard);
	Test.TestEqual(TEXT("Repeated apply preserves authored key count"), AuthoredKeys.Num(), 1);
	if (AuthoredKeys.Num() == 1)
	{
		Test.TestTrue(TEXT("Repeated apply preserves key type object"), AuthoredKeys[0]->KeyType == FirstKeyType);
	}
	Test.TestEqual(TEXT("Repeated apply does not create extra key type children"), CountBlackboardKeyTypeChildren(Blackboard), FirstChildCount);
	return true;
}

bool ExpectApplyThenDiffUnchangedForSingleKey(
	FAutomationTestBase& Test,
	const FString& Target,
	const TSharedRef<FJsonObject>& KeyJson)
{
	TSharedPtr<FJsonObject> Document = BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(nullptr, {KeyJson}));

	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(Document));
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

	Test.TestTrue(TEXT("Diff against same authored document has no changed or failed entries"), BlackboardTestDiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
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
	TestNotNull(TEXT("Policy includes Body.Parent"), BlackboardTestFindPolicyByRegionId(Policies, TEXT("Body.Parent")));
	TestNotNull(TEXT("Policy includes Body.Keys"), BlackboardTestFindPolicyByRegionId(Policies, TEXT("Body.Keys")));
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
			BlackboardTestMakeBlackboardKeyJson(Expectation.Alias + TEXT("Key"), Expectation.Alias),
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
		TSharedRef<FJsonObject> EmptyNameJson = BlackboardTestMakeBlackboardKeyJson(TEXT(""), TEXT("Bool"));
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(EmptyNameJson, TEXT("/Body/Keys/2"), Spec),
			TEXT("InvalidBlackboardKeyName"),
			TEXT("/Body/Keys/2/Name"));
	}

	{
		TSharedRef<FJsonObject> InvalidDescriptionJson = BlackboardTestMakeBlackboardKeyJson(TEXT("BadDescription"), TEXT("Bool"));
		InvalidDescriptionJson->SetNumberField(TEXT("Description"), 42.0);
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(InvalidDescriptionJson, TEXT("/Body/Keys/BadDescription"), Spec),
			TEXT("InvalidBlackboardKeyDescription"),
			TEXT("/Body/Keys/BadDescription/Description"));
	}

	{
		TSharedRef<FJsonObject> InvalidInstanceSyncedJson = BlackboardTestMakeBlackboardKeyJson(TEXT("BadSync"), TEXT("Bool"));
		InvalidInstanceSyncedJson->SetStringField(TEXT("bInstanceSynced"), TEXT("true"));
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(InvalidInstanceSyncedJson, TEXT("/Body/Keys/BadSync"), Spec),
			TEXT("InvalidBlackboardKeyInstanceSynced"),
			TEXT("/Body/Keys/BadSync/bInstanceSynced"));
	}

	{
		TSharedRef<FJsonObject> InvalidBaseClassJson = BlackboardTestMakeBlackboardKeyJson(TEXT("BadBaseClass"), TEXT("Object"));
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
		TSharedRef<FJsonObject> InvalidEnumJson = BlackboardTestMakeBlackboardKeyJson(TEXT("BadEnum"), TEXT("Enum"));
		InvalidEnumJson->SetNumberField(TEXT("Enum"), 42.0);
		FAssetDocumentBlackboardKeySpec Spec;
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ParseKey(InvalidEnumJson, TEXT("/Body/Keys/BadEnum"), Spec),
			TEXT("InvalidBlackboardKeyEnum"),
			TEXT("/Body/Keys/BadEnum/Enum"));
	}

	{
		TSharedRef<FJsonObject> UnknownFieldJson = BlackboardTestMakeBlackboardKeyJson(TEXT("Typo"), TEXT("Bool"));
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
			BlackboardTestMakeBlackboardKeyJson(TEXT("Target"), TEXT("Object")),
			TEXT("/Body/Keys/Target"),
			Spec);
		TestTrue(TEXT("Object key parses before validation"), ParseResult.bSuccess);
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(Spec, TEXT("/Body/Keys/Target")),
			TEXT("MissingBlackboardKeyBaseClass"),
			TEXT("/Body/Keys/Target/KeyTypeProperties/BaseClass"));
		TestFalse(TEXT("Object key does not synthesize an authored BaseClass from its CDO default"), Spec.KeyTypeProperties->HasField(TEXT("BaseClass")));
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			BlackboardTestMakeBlackboardKeyJson(TEXT("ChosenClass"), TEXT("Class")),
			TEXT("/Body/Keys/ChosenClass"),
			Spec);
		TestTrue(TEXT("Class key parses before validation"), ParseResult.bSuccess);
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(Spec, TEXT("/Body/Keys/ChosenClass")),
			TEXT("MissingBlackboardKeyBaseClass"),
			TEXT("/Body/Keys/ChosenClass/KeyTypeProperties/BaseClass"));
		TestFalse(TEXT("Class key does not synthesize an authored BaseClass from its CDO default"), Spec.KeyTypeProperties->HasField(TEXT("BaseClass")));
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			BlackboardTestMakeBlackboardKeyJson(TEXT("Mode"), TEXT("Enum")),
			TEXT("/Body/Keys/Mode"),
			Spec);
		TestTrue(TEXT("Enum key parses before validation"), ParseResult.bSuccess);
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(Spec, TEXT("/Body/Keys/Mode")),
			TEXT("MissingBlackboardKeyEnum"),
			TEXT("/Body/Keys/Mode/KeyTypeProperties/EnumType"));
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			BlackboardTestMakeBlackboardKeyJson(
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
			TEXT("DeprecatedBlackboardKeyType"),
			TEXT("/Body/Keys/NativeMode/KeyTypeClass"));
	}

	{
		FAssetDocumentBlackboardKeySpec Spec;
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			BlackboardTestMakeBlackboardKeyJson(TEXT("Broken"), TEXT("Bogus")),
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
			BlackboardTestMakeBlackboardKeyJson(TEXT("BrokenCustomPath"), TEXT("Bogus")),
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
		BlackboardTestMakeBlackboardKeyJson(
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
		BlackboardTestMakeBlackboardKeyJson(
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
		BlackboardTestMakeBlackboardKeyJson(
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
		BlackboardTestMakeBlackboardKeyJson(
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
		BlackboardTestMakeBlackboardKeyJson(
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
			BlackboardTestMakeBlackboardKeyJson(TEXT("Target"), TEXT("Object"), ActorClassPath),
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
			BlackboardTestMakeBlackboardKeyJson(TEXT("ChosenClass"), TEXT("Class"), ActorClassPath),
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
			BlackboardTestMakeBlackboardKeyJson(TEXT("Mode"), TEXT("Enum"), TEXT(""), EnumPath),
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
			BlackboardTestMakeBlackboardKeyJson(
				TEXT("NativeMode"),
				TEXT(""),
				TEXT(""),
				EnumPath,
				TEXT("/Script/AIModule.BlackboardKeyType_NativeEnum")),
			TEXT("/Body/Keys/NativeMode"),
			Spec);
		TestTrue(TEXT("NativeEnum metadata key parses"), ParseResult.bSuccess);
		ExpectSingleDiagnostic(
			*this,
			FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(Spec, TEXT("/Body/Keys/NativeMode")),
			TEXT("DeprecatedBlackboardKeyType"),
			TEXT("/Body/Keys/NativeMode/KeyTypeClass"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardKeyPublicObjectRefsTest,
	"AssetFactory.AssetDocument.BlackboardData.Keys.PublicObjectRefs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardKeyPublicObjectRefsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_PublicObjectRefs");
	const FString EnumPath = StaticEnum<EBasicKeyOperation::Type>()->GetPathName();

	TSharedRef<FJsonObject> ObjectKey = BlackboardTestMakeBlackboardKeyJson(TEXT("TargetActor"), TEXT("Object"));
	ObjectKey->SetObjectField(TEXT("BaseClass"), BlackboardTestMakeClassRef(AActor::StaticClass()->GetPathName()));

	TSharedRef<FJsonObject> EnumKey = BlackboardTestMakeBlackboardKeyJson(TEXT("Mode"), TEXT(""));
	EnumKey->SetObjectField(TEXT("KeyTypeClass"), BlackboardTestMakeClassRef(UBlackboardKeyType_Enum::StaticClass()->GetPathName()));
	EnumKey->SetObjectField(TEXT("Enum"), BlackboardTestMakeAssetRef(EnumPath));

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(nullptr, {ObjectKey, EnumKey}));
	const FAssetDocumentResult ApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(Document));
	TestTrue(TEXT("public object-form blackboard apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	TestNotNull(TEXT("public object-form blackboard loads"), Blackboard);
	const TArray<const FBlackboardEntry*> AuthoredKeys = BlackboardTestGetAuthoredKeys(Blackboard);
	if (AuthoredKeys.Num() == 2)
	{
		const UBlackboardKeyType_Object* ObjectType = Cast<UBlackboardKeyType_Object>(AuthoredKeys[0]->KeyType);
		TestTrue(TEXT("public BaseClass object ref applies"), ObjectType && ObjectType->BaseClass.Get() == AActor::StaticClass());
		const UBlackboardKeyType_Enum* EnumType = Cast<UBlackboardKeyType_Enum>(AuthoredKeys[1]->KeyType);
		TestTrue(TEXT("public KeyTypeClass object ref applies"), EnumType && EnumType->EnumType == StaticEnum<EBasicKeyOperation::Type>());
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("public object-form blackboard extract succeeds"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedKeys = nullptr;
	TestTrue(TEXT("public object-form extract includes Keys"), ExtractedBody.IsValid() && ExtractedBody->TryGetArrayField(TEXT("Keys"), ExtractedKeys));
	if (ExtractedKeys && ExtractedKeys->Num() == 2)
	{
		TSharedPtr<FJsonObject> ExtractedObjectKey = (*ExtractedKeys)[0]->AsObject();
		TSharedPtr<FJsonObject> ExtractedObjectProperties = BlackboardTestGetKeyTypeProperties(ExtractedObjectKey);
		TSharedPtr<FJsonObject> ExtractedBaseClass = ExtractedObjectProperties.IsValid() ? ExtractedObjectProperties->GetObjectField(TEXT("BaseClass")) : nullptr;
		TestTrue(TEXT("extract uses BaseClass ClassRef"), ExtractedBaseClass.IsValid()
			&& ExtractedBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef")
			&& ExtractedBaseClass->GetStringField(TEXT("Path")) == AActor::StaticClass()->GetPathName());

		TSharedPtr<FJsonObject> ExtractedEnumKey = (*ExtractedKeys)[1]->AsObject();
		TSharedPtr<FJsonObject> ExtractedKeyTypeClass = ExtractedEnumKey.IsValid() ? ExtractedEnumKey->GetObjectField(TEXT("KeyTypeClass")) : nullptr;
		TSharedPtr<FJsonObject> ExtractedEnumProperties = BlackboardTestGetKeyTypeProperties(ExtractedEnumKey);
		TSharedPtr<FJsonObject> ExtractedEnum = ExtractedEnumProperties.IsValid() ? ExtractedEnumProperties->GetObjectField(TEXT("EnumType")) : nullptr;
		TestTrue(TEXT("extract uses KeyTypeClass ClassRef"), ExtractedKeyTypeClass.IsValid()
			&& ExtractedKeyTypeClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef")
			&& ExtractedKeyTypeClass->GetStringField(TEXT("Path")) == UBlackboardKeyType_Enum::StaticClass()->GetPathName());
		TestTrue(TEXT("extract uses Enum AssetRef"), ExtractedEnum.IsValid()
			&& ExtractedEnum->GetStringField(TEXT("Kind")) == TEXT("AssetRef")
			&& ExtractedEnum->GetStringField(TEXT("Path")) == EnumPath);
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("public object-form blackboard diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("public object-form blackboard diff is unchanged"), BlackboardTestDiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
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
		BlackboardTestMakeBlackboardKeyJson(
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
	const TSharedRef<FJsonObject> Extracted = BlackboardTestExtractKeyChecked(*this, Entry, TEXT("/Body/Keys/Target"));
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
	const TSharedRef<FJsonObject> ExtractedNativeEnum = BlackboardTestExtractKeyChecked(*this, NativeEnumEntry, TEXT("/Body/Keys/NativeMode"));
	TSharedPtr<FJsonObject> ExtractedNativeEnumKeyTypeClass = ExtractedNativeEnum->GetObjectField(TEXT("KeyTypeClass"));
	TSharedPtr<FJsonObject> ExtractedNativeEnumProperties = BlackboardTestGetKeyTypeProperties(ExtractedNativeEnum);
	TSharedPtr<FJsonObject> ExtractedNativeEnumRef = ExtractedNativeEnumProperties.IsValid()
		? ExtractedNativeEnumProperties->GetObjectField(TEXT("EnumType"))
		: nullptr;
	TestTrue(TEXT("Extracted legacy NativeEnum canonicalizes to Enum ClassRef"), ExtractedNativeEnumKeyTypeClass.IsValid()
		&& ExtractedNativeEnumKeyTypeClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef")
		&& ExtractedNativeEnumKeyTypeClass->GetStringField(TEXT("Path")) == TEXT("/Script/AIModule.BlackboardKeyType_Enum"));
	TestTrue(TEXT("Extracted legacy NativeEnum preserves EnumType as AssetRef"), ExtractedNativeEnumRef.IsValid()
		&& ExtractedNativeEnumRef->GetStringField(TEXT("Kind")) == TEXT("AssetRef")
		&& ExtractedNativeEnumRef->GetStringField(TEXT("Path")) == NativeEnumKey->EnumName);

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
	const TSharedRef<FJsonObject> ExtractedStruct = BlackboardTestExtractKeyChecked(*this, StructEntry, TEXT("/Body/Keys/Payload"));
	TestFalse(TEXT("Extracted Struct does not use alias Type"), ExtractedStruct->HasField(TEXT("Type")));
	TSharedPtr<FJsonObject> ExtractedStructKeyTypeClass = ExtractedStruct->GetObjectField(TEXT("KeyTypeClass"));
	TestTrue(TEXT("Extracted Struct key type class is ClassRef"), ExtractedStructKeyTypeClass.IsValid()
		&& ExtractedStructKeyTypeClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef")
		&& ExtractedStructKeyTypeClass->GetStringField(TEXT("Path")) == TEXT("/Script/AIModule.BlackboardKeyType_Struct"));

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
		BlackboardTestMakeBlackboardKeyJson(TEXT("Target"), TEXT("Bool")),
		BlackboardTestMakeBlackboardKeyJson(TEXT("Target"), TEXT("Int")),
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

	const TSharedRef<FJsonObject> ExtractedLocal = BlackboardTestExtractKeyChecked(*this, *LocalEntry, TEXT("/Body/Keys/LocalCount"));
	TestEqual(TEXT("Extracted local key name"), ExtractedLocal->GetStringField(TEXT("Name")), FString(TEXT("LocalCount")));
	TestEqual(
		TEXT("Extracted local key type"),
		ExtractedLocal->GetObjectField(TEXT("KeyTypeClass"))->GetStringField(TEXT("Path")),
		UBlackboardKeyType_Int::StaticClass()->GetPathName());
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
	const FAssetDocumentResult ParentApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		ParentTarget,
		BlackboardTestMakeBlackboardDataBody(nullptr, {BlackboardTestMakeBlackboardKeyJson(TEXT("InheritedTarget"), TEXT("Bool"))}))));
	TestTrue(TEXT("Parent blackboard apply succeeds"), ParentApplyResult.IsSuccess());
	if (!ParentApplyResult.IsSuccess())
	{
		AddError(ParentApplyResult.Message);
		return false;
	}

	const FAssetDocumentResult ChildApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		ChildTarget,
		BlackboardTestMakeBlackboardDataBody(
			BlackboardTestMakeAssetRef(ParentTarget),
			{BlackboardTestMakeBlackboardKeyJson(TEXT("LocalCount"), TEXT("Int"))}))));
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
	const FAssetDocumentResult ParentApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		ParentTarget,
		BlackboardTestMakeBlackboardDataBody(nullptr, {BlackboardTestMakeBlackboardKeyJson(TEXT("InheritedTeam"), TEXT("Name"))}))));
	TestTrue(TEXT("Roundtrip parent blackboard apply succeeds"), ParentApplyResult.IsSuccess());
	if (!ParentApplyResult.IsSuccess())
	{
		AddError(ParentApplyResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> Body = BlackboardTestMakeBlackboardDataBody(BlackboardTestMakeAssetRef(ParentTarget), {
		BlackboardTestMakeBlackboardKeyJson(TEXT("Target"), TEXT("Object"), AActor::StaticClass()->GetPathName(), TEXT(""), TEXT(""), TEXT("Current target")),
		BlackboardTestMakeBlackboardKeyJson(TEXT("IsVisible"), TEXT("Bool")),
	});
	Body->GetArrayField(TEXT("Keys"))[1]->AsObject()->SetBoolField(TEXT("bInstanceSynced"), true);

	TSharedPtr<FJsonObject> Document = BlackboardTestMakeBlackboardDataDocument(Target, Body);
	const FAssetDocumentResult ApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(Document));
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
		TestEqual(TEXT("Extracted Parent path is canonical object path"), (*ExtractedParent)->GetStringField(TEXT("Path")), BlackboardTestMakeObjectPathFromTarget(ParentTarget));
	}
	const TArray<TSharedPtr<FJsonValue>>* ExtractedKeys = nullptr;
	TestTrue(TEXT("Extracted body has Keys"), ExtractedBody.IsValid() && ExtractedBody->TryGetArrayField(TEXT("Keys"), ExtractedKeys));
	TestEqual(TEXT("Extracted key count"), ExtractedKeys ? ExtractedKeys->Num() : -1, 2);

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Blackboard diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("Apply/extract diff is unchanged"), BlackboardTestDiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataRepeatedApplyPreservesKeyObjectsTest,
	"AssetFactory.AssetDocument.BlackboardData.RepeatedApplyPreservesKeyObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataRepeatedApplyPreservesKeyObjectsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_RepeatedApplyPreservesKeyObjects");
	TSharedPtr<FJsonObject> Body = BlackboardTestMakeBlackboardDataBody(nullptr, {
		BlackboardTestMakeBlackboardKeyJson(TEXT("Target"), TEXT("Bool")),
		BlackboardTestMakeBlackboardKeyJson(TEXT("Count"), TEXT("Int")),
	});
	TSharedPtr<FJsonObject> Document = BlackboardTestMakeBlackboardDataDocument(Target, Body);

	FAssetDocumentService Service;
	const FAssetDocumentResult FirstApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(Document));
	TestTrue(TEXT("Initial blackboard apply succeeds"), FirstApplyResult.IsSuccess());
	if (!FirstApplyResult.IsSuccess())
	{
		AddError(FirstApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	TestNotNull(TEXT("Blackboard loads after initial apply"), Blackboard);
	TArray<const FBlackboardEntry*> AuthoredKeys = BlackboardTestGetAuthoredKeys(Blackboard);
	if (AuthoredKeys.Num() != 2)
	{
		return false;
	}

	UBlackboardKeyType* FirstTargetKeyType = AuthoredKeys[0]->KeyType;
	UBlackboardKeyType* FirstCountKeyType = AuthoredKeys[1]->KeyType;
	const int32 FirstChildCount = CountBlackboardKeyTypeChildren(Blackboard);

	const FAssetDocumentResult SecondApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(Document));
	TestTrue(TEXT("Repeated identical blackboard apply succeeds"), SecondApplyResult.IsSuccess());
	if (!SecondApplyResult.IsSuccess())
	{
		AddError(SecondApplyResult.Message);
		return false;
	}

	AuthoredKeys = BlackboardTestGetAuthoredKeys(Blackboard);
	TestEqual(TEXT("Repeated apply preserves authored key count"), AuthoredKeys.Num(), 2);
	if (AuthoredKeys.Num() == 2)
	{
		TestTrue(TEXT("Repeated apply preserves first key type object"), AuthoredKeys[0]->KeyType == FirstTargetKeyType);
		TestTrue(TEXT("Repeated apply preserves second key type object"), AuthoredKeys[1]->KeyType == FirstCountKeyType);
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
		BlackboardTestMakeBlackboardKeyJson(
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
		BlackboardTestMakeBlackboardKeyJson(
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
		BlackboardTestMakeBlackboardKeyJson(
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
		BlackboardTestMakeBlackboardKeyJson(
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

	TSharedPtr<FJsonObject> Body = BlackboardTestMakeBlackboardDataBody(nullptr, {
		BlackboardTestMakeBlackboardKeyJson(TEXT("DryRunKey"), TEXT("Bool")),
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
	TSharedPtr<FJsonObject> InitialBody = BlackboardTestMakeBlackboardDataBody(nullptr, {
		BlackboardTestMakeBlackboardKeyJson(TEXT("A"), TEXT("Bool")),
		BlackboardTestMakeBlackboardKeyJson(TEXT("B"), TEXT("Int")),
	});

	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(Target, InitialBody)));
	TestTrue(TEXT("Initial ordered apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> ReorderedBody = BlackboardTestMakeBlackboardDataBody(nullptr, {
		BlackboardTestMakeBlackboardKeyJson(TEXT("B"), TEXT("Int")),
		BlackboardTestMakeBlackboardKeyJson(TEXT("A"), TEXT("Bool")),
	});
	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = BlackboardTestMakeBlackboardDataDocument(Target, ReorderedBody);
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
	const FAssetDocumentResult InitialResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(nullptr, {BlackboardTestMakeBlackboardKeyJson(TEXT("Local"), TEXT("Bool"))}))));
	TestTrue(TEXT("Initial self-cycle fixture apply succeeds"), InitialResult.IsSuccess());
	if (!InitialResult.IsSuccess())
	{
		AddError(InitialResult.Message);
		return false;
	}

	const FAssetDocumentResult SelfParentResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(BlackboardTestMakeAssetRef(Target), {BlackboardTestMakeBlackboardKeyJson(TEXT("Local"), TEXT("Bool"))}))));

	TestFalse(TEXT("Self-parent apply fails"), SelfParentResult.IsSuccess());
	TestTrue(TEXT("Self-parent reports cycle diagnostic"), BlackboardTestResultHasDiagnostic(SelfParentResult, TEXT("BlackboardParentCycle"), TEXT("/Body/Parent")));
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

	const FAssetDocumentResult ParentResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		ParentTarget,
		BlackboardTestMakeBlackboardDataBody(nullptr, {BlackboardTestMakeBlackboardKeyJson(TEXT("ParentKey"), TEXT("Bool"))}))));
	TestTrue(TEXT("Parent fixture apply succeeds"), ParentResult.IsSuccess());
	if (!ParentResult.IsSuccess())
	{
		AddError(ParentResult.Message);
		return false;
	}

	const FAssetDocumentResult ChildResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		ChildTarget,
		BlackboardTestMakeBlackboardDataBody(BlackboardTestMakeAssetRef(ParentTarget), {BlackboardTestMakeBlackboardKeyJson(TEXT("ChildKey"), TEXT("Int"))}))));
	TestTrue(TEXT("Child fixture apply succeeds"), ChildResult.IsSuccess());
	if (!ChildResult.IsSuccess())
	{
		AddError(ChildResult.Message);
		return false;
	}

	const FAssetDocumentResult CycleResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		ParentTarget,
		BlackboardTestMakeBlackboardDataBody(BlackboardTestMakeAssetRef(ChildTarget), {BlackboardTestMakeBlackboardKeyJson(TEXT("ParentKey"), TEXT("Bool"))}))));

	TestFalse(TEXT("Parent-chain cycle apply fails"), CycleResult.IsSuccess());
	TestTrue(TEXT("Parent-chain cycle reports diagnostic"), BlackboardTestResultHasDiagnostic(CycleResult, TEXT("BlackboardParentCycle"), TEXT("/Body/Parent")));
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
	const FAssetDocumentResult Result = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(nullptr, {
			BlackboardTestMakeBlackboardKeyJson(TEXT("Target"), TEXT("Bool")),
			BlackboardTestMakeBlackboardKeyJson(TEXT("Target"), TEXT("Int")),
		}))));
	TestFalse(TEXT("Duplicate blackboard key apply fails"), Result.IsSuccess());
	TestTrue(TEXT("Duplicate failure has exact diagnostic"), BlackboardTestResultHasDiagnostic(Result, TEXT("DuplicateBlackboardKey"), TEXT("/Body/Keys/1/Name")));
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
	const FAssetDocumentResult Result = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(nullptr, {BlackboardTestMakeBlackboardKeyJson(TEXT("Broken"), TEXT("Bogus"))}))));
	TestFalse(TEXT("Invalid blackboard key type apply fails"), Result.IsSuccess());
	TestTrue(TEXT("Invalid type failure has exact diagnostic"), BlackboardTestResultHasDiagnostic(Result, TEXT("InvalidBlackboardKeyType"), TEXT("/Body/Keys/Broken/Type")));
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
	const FAssetDocumentResult ParentApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		ParentTarget,
		BlackboardTestMakeBlackboardDataBody(nullptr, {
			[&]()
			{
				TSharedRef<FJsonObject> Key = BlackboardTestMakeBlackboardKeyJson(TEXT("InheritedTarget"), TEXT("Object"));
				Key->SetObjectField(TEXT("BaseClass"), BlackboardTestMakeClassRef(AActor::StaticClass()->GetPathName()));
				return Key;
			}(),
			BlackboardTestMakeBlackboardKeyJson(TEXT("InheritedGate"), TEXT("Bool"),
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

	FBlackboardEntry TargetEntry;
	TargetEntry.EntryName = TEXT("TargetActor");
	TargetEntry.EntryDescription = TEXT("Local target");
	UBlackboardKeyType_Object* TargetType = NewObject<UBlackboardKeyType_Object>(GetTransientPackage());
	TargetType->BaseClass = AActor::StaticClass();
	TargetEntry.KeyType = TargetType;
	FBlackboardEntry MoveEntry;
	MoveEntry.EntryName = TEXT("MoveLocation");
	MoveEntry.KeyType = NewObject<UBlackboardKeyType_Vector>(GetTransientPackage());
	FBlackboardEntry AlertEntry;
	AlertEntry.EntryName = TEXT("AlertName");
	AlertEntry.KeyType = NewObject<UBlackboardKeyType_Name>(GetTransientPackage());
	AlertEntry.bInstanceSynced = true;
	TSharedPtr<FJsonObject> Body = BlackboardTestMakeBlackboardDataBody(
		BlackboardTestMakeAssetRef(BlackboardTestMakeObjectPathFromTarget(ParentTarget)),
		{
			BlackboardTestExtractKeyChecked(*this, TargetEntry, TEXT("/Body/Keys/TargetActor")),
			BlackboardTestExtractKeyChecked(*this, MoveEntry, TEXT("/Body/Keys/MoveLocation")),
			BlackboardTestExtractKeyChecked(*this, AlertEntry, TEXT("/Body/Keys/AlertName")),
		});
	TSharedPtr<FJsonObject> Document = BlackboardTestMakeBlackboardDataDocument(Target, Body);
	const FString SidecarPath = FAssetDocumentSidecar::ResolveSidecarPathFromObjectPath(Target);
	if (!BlackboardTestWriteSidecarJson(this, SidecarPath, Document))
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
	if (!BlackboardTestLoadSidecarJson(this, SidecarPath, ReloadedSidecar))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* ReloadedBodyPtr = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ReloadedKeys = nullptr;
	TestTrue(TEXT("ApplyFile sidecar retains Body"), ReloadedSidecar.IsValid() && ReloadedSidecar->TryGetObjectField(TEXT("Body"), ReloadedBodyPtr));
	TestTrue(TEXT("ApplyFile sidecar retains Keys"), ReloadedBodyPtr && ReloadedBodyPtr->IsValid() && (*ReloadedBodyPtr)->TryGetArrayField(TEXT("Keys"), ReloadedKeys));
	if (ReloadedKeys && ReloadedKeys->Num() > 0)
	{
		TSharedPtr<FJsonObject> ReloadedTargetKey = (*ReloadedKeys)[0]->AsObject();
		TSharedPtr<FJsonObject> ReloadedProperties = BlackboardTestGetKeyTypeProperties(ReloadedTargetKey);
		TSharedPtr<FJsonObject> ReloadedBaseClass = ReloadedProperties.IsValid() ? ReloadedProperties->GetObjectField(TEXT("BaseClass")) : nullptr;
		TestTrue(TEXT("ApplyFile sidecar uses BaseClass ClassRef"), ReloadedBaseClass.IsValid()
			&& ReloadedBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef")
			&& ReloadedBaseClass->GetStringField(TEXT("Path")) == AActor::StaticClass()->GetPathName());
	}
	BlackboardTestExpectSyncRegions(this, ReloadedSidecar, {
		TEXT("Body.Parent"),
		TEXT("Body.Keys"),
	});

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.FilePath = SidecarPath;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("BlackboardData ApplyFile diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("BlackboardData ApplyFile diff has no changed or failed entries"), BlackboardTestDiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataProductionBuiltinDefaultsTest,
	"AssetFactory.AssetDocument.BlackboardData.Production.BuiltinDefaultsAndMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataProductionBuiltinDefaultsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_ProductionBuiltinDefaults");
	UEnum* EnumType = StaticEnum<EBasicKeyOperation::Type>();
	const FString EnumDefault = EnumType->GetNameStringByValue(0);

	TSharedRef<FJsonObject> BoolProperties = MakeShared<FJsonObject>();
	BoolProperties->SetBoolField(TEXT("bDefaultValue"), true);
	TSharedRef<FJsonObject> BoolKey = BlackboardTestMakeCanonicalKey(TEXT("Enabled"), UBlackboardKeyType_Bool::StaticClass(), BoolProperties);
	BoolKey->SetStringField(TEXT("Description"), TEXT("Controls production behavior"));
	BoolKey->SetStringField(TEXT("Category"), TEXT("Production"));
	BoolKey->SetBoolField(TEXT("bInstanceSynced"), true);

	TSharedRef<FJsonObject> IntProperties = MakeShared<FJsonObject>();
	IntProperties->SetNumberField(TEXT("DefaultValue"), 42);
	TSharedRef<FJsonObject> FloatProperties = MakeShared<FJsonObject>();
	FloatProperties->SetNumberField(TEXT("DefaultValue"), 3.5);
	TSharedRef<FJsonObject> NameProperties = MakeShared<FJsonObject>();
	NameProperties->SetStringField(TEXT("DefaultValue"), TEXT("SpawnPoint"));
	TSharedRef<FJsonObject> StringProperties = MakeShared<FJsonObject>();
	StringProperties->SetStringField(TEXT("DefaultValue"), TEXT("Aggressive"));

	TSharedRef<FJsonObject> VectorDefault = MakeShared<FJsonObject>();
	VectorDefault->SetNumberField(TEXT("X"), 1.25);
	VectorDefault->SetNumberField(TEXT("Y"), -2.5);
	VectorDefault->SetNumberField(TEXT("Z"), 3.75);
	TSharedRef<FJsonObject> VectorProperties = MakeShared<FJsonObject>();
	VectorProperties->SetObjectField(TEXT("DefaultValue"), VectorDefault);
	VectorProperties->SetBoolField(TEXT("bUseDefaultValue"), true);

	TSharedRef<FJsonObject> RotatorDefault = MakeShared<FJsonObject>();
	RotatorDefault->SetNumberField(TEXT("Pitch"), 10.0);
	RotatorDefault->SetNumberField(TEXT("Yaw"), 20.0);
	RotatorDefault->SetNumberField(TEXT("Roll"), 30.0);
	TSharedRef<FJsonObject> RotatorProperties = MakeShared<FJsonObject>();
	RotatorProperties->SetObjectField(TEXT("DefaultValue"), RotatorDefault);
	RotatorProperties->SetBoolField(TEXT("bUseDefaultValue"), true);

	TSharedRef<FJsonObject> ObjectProperties = MakeShared<FJsonObject>();
	ObjectProperties->SetObjectField(TEXT("BaseClass"), BlackboardTestMakeClassRef(AActor::StaticClass()->GetPathName()));
	ObjectProperties->SetField(TEXT("DefaultValue"), MakeShared<FJsonValueNull>());
	TSharedRef<FJsonObject> ClassProperties = MakeShared<FJsonObject>();
	ClassProperties->SetObjectField(TEXT("BaseClass"), BlackboardTestMakeClassRef(AActor::StaticClass()->GetPathName()));
	ClassProperties->SetObjectField(TEXT("DefaultValue"), BlackboardTestMakeClassRef(AActor::StaticClass()->GetPathName()));

	TSharedRef<FJsonObject> EnumProperties = MakeShared<FJsonObject>();
	EnumProperties->SetObjectField(TEXT("EnumType"), BlackboardTestMakeAssetRef(EnumType->GetPathName()));
	EnumProperties->SetStringField(TEXT("EnumName"), EnumType->GetPathName());
	EnumProperties->SetStringField(TEXT("DefaultValue"), EnumDefault);

	TArray<TSharedRef<FJsonObject>> Keys = {
		BoolKey,
		BlackboardTestMakeCanonicalKey(TEXT("Count"), UBlackboardKeyType_Int::StaticClass(), IntProperties),
		BlackboardTestMakeCanonicalKey(TEXT("Ratio"), UBlackboardKeyType_Float::StaticClass(), FloatProperties),
		BlackboardTestMakeCanonicalKey(TEXT("Marker"), UBlackboardKeyType_Name::StaticClass(), NameProperties),
		BlackboardTestMakeCanonicalKey(TEXT("Mood"), UBlackboardKeyType_String::StaticClass(), StringProperties),
		BlackboardTestMakeCanonicalKey(TEXT("Location"), UBlackboardKeyType_Vector::StaticClass(), VectorProperties),
		BlackboardTestMakeCanonicalKey(TEXT("Facing"), UBlackboardKeyType_Rotator::StaticClass(), RotatorProperties),
		BlackboardTestMakeCanonicalKey(TEXT("Target"), UBlackboardKeyType_Object::StaticClass(), ObjectProperties),
		BlackboardTestMakeCanonicalKey(TEXT("TargetClass"), UBlackboardKeyType_Class::StaticClass(), ClassProperties),
		BlackboardTestMakeCanonicalKey(TEXT("Mode"), UBlackboardKeyType_Enum::StaticClass(), EnumProperties),
	};

	TSharedPtr<FJsonObject> Document = BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(nullptr, Keys));
	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(Document));
	TestTrue(TEXT("All built-in defaults and metadata apply"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	TestNotNull(TEXT("Production built-in blackboard loads"), Blackboard);
	const TArray<const FBlackboardEntry*> AuthoredKeys = BlackboardTestGetAuthoredKeys(Blackboard);
	if (AuthoredKeys.Num() != Keys.Num())
	{
		return false;
	}

	TestEqual(TEXT("Description applies"), AuthoredKeys[0]->EntryDescription, FString(TEXT("Controls production behavior")));
	TestEqual(TEXT("Category applies"), AuthoredKeys[0]->EntryCategory, FName(TEXT("Production")));
	TestTrue(TEXT("bInstanceSynced applies"), AuthoredKeys[0]->bInstanceSynced != 0);
	TestTrue(TEXT("Bool default applies"), CastChecked<UBlackboardKeyType_Bool>(AuthoredKeys[0]->KeyType)->bDefaultValue);
	TestEqual(TEXT("Int default applies"), CastChecked<UBlackboardKeyType_Int>(AuthoredKeys[1]->KeyType)->DefaultValue, 42);
	TestEqual(TEXT("Float default applies"), CastChecked<UBlackboardKeyType_Float>(AuthoredKeys[2]->KeyType)->DefaultValue, 3.5f);
	TestEqual(TEXT("Name default applies"), CastChecked<UBlackboardKeyType_Name>(AuthoredKeys[3]->KeyType)->DefaultValue, FName(TEXT("SpawnPoint")));
	TestEqual(TEXT("String default applies"), CastChecked<UBlackboardKeyType_String>(AuthoredKeys[4]->KeyType)->DefaultValue, FString(TEXT("Aggressive")));
	TestEqual(TEXT("Vector default applies"), CastChecked<UBlackboardKeyType_Vector>(AuthoredKeys[5]->KeyType)->DefaultValue, FVector(1.25, -2.5, 3.75));
	TestTrue(TEXT("Vector use-default metadata applies"), CastChecked<UBlackboardKeyType_Vector>(AuthoredKeys[5]->KeyType)->bUseDefaultValue);
	TestEqual(TEXT("Rotator default applies"), CastChecked<UBlackboardKeyType_Rotator>(AuthoredKeys[6]->KeyType)->DefaultValue, FRotator(10.0, 20.0, 30.0));
	TestTrue(TEXT("Rotator use-default metadata applies"), CastChecked<UBlackboardKeyType_Rotator>(AuthoredKeys[6]->KeyType)->bUseDefaultValue);
	TestEqual(TEXT("Object BaseClass applies"), CastChecked<UBlackboardKeyType_Object>(AuthoredKeys[7]->KeyType)->BaseClass.Get(), AActor::StaticClass());
	TestNull(TEXT("Object null default applies"), CastChecked<UBlackboardKeyType_Object>(AuthoredKeys[7]->KeyType)->DefaultValue.Get());
	TestEqual(TEXT("Class BaseClass applies"), CastChecked<UBlackboardKeyType_Class>(AuthoredKeys[8]->KeyType)->BaseClass.Get(), AActor::StaticClass());
	TestEqual(TEXT("Class default applies"), CastChecked<UBlackboardKeyType_Class>(AuthoredKeys[8]->KeyType)->DefaultValue.Get(), AActor::StaticClass());
	TestEqual(TEXT("Enum type applies"), CastChecked<UBlackboardKeyType_Enum>(AuthoredKeys[9]->KeyType)->EnumType.Get(), EnumType);
	TestEqual(TEXT("Enum default applies"), CastChecked<UBlackboardKeyType_Enum>(AuthoredKeys[9]->KeyType)->DefaultValue, static_cast<uint8>(0));

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("All built-in defaults extract"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedKeys = nullptr;
	TestTrue(TEXT("Extracted production body has all keys"), ExtractedBody.IsValid() && ExtractedBody->TryGetArrayField(TEXT("Keys"), ExtractedKeys) && ExtractedKeys && ExtractedKeys->Num() == Keys.Num());
	if (ExtractedKeys && ExtractedKeys->Num() == Keys.Num())
	{
		TSharedPtr<FJsonObject> ExtractedBool = (*ExtractedKeys)[0]->AsObject();
		TestFalse(TEXT("Canonical extract never emits short Type"), ExtractedBool->HasField(TEXT("Type")));
		TestTrue(TEXT("Canonical extract emits KeyTypeClass"), ExtractedBool->HasField(TEXT("KeyTypeClass")));
		TestEqual(TEXT("Canonical extract preserves Category"), ExtractedBool->GetStringField(TEXT("Category")), FString(TEXT("Production")));
		TestTrue(TEXT("Canonical extract preserves explicit synchronized state"), ExtractedBool->GetBoolField(TEXT("bInstanceSynced")));
		TSharedPtr<FJsonObject> ExtractedBoolProperties = BlackboardTestGetKeyTypeProperties(ExtractedBool);
		TestTrue(TEXT("Canonical extract includes Bool properties"), ExtractedBoolProperties.IsValid() && ExtractedBoolProperties->GetBoolField(TEXT("bDefaultValue")));
		TSharedPtr<FJsonObject> ExtractedEnumProperties = BlackboardTestGetKeyTypeProperties((*ExtractedKeys)[9]->AsObject());
		TestTrue(TEXT("Canonical extract includes Enum properties"), ExtractedEnumProperties.IsValid()
			&& ExtractedEnumProperties->GetStringField(TEXT("DefaultValue")) == EnumDefault);
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Built-in authored document diffs"), DiffResult.IsSuccess());
	TestTrue(TEXT("Built-in authored document is canonical-idempotent"), BlackboardTestDiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataProductionSelfActorSignatureTest,
	"AssetFactory.AssetDocument.BlackboardData.Production.SelfActorExactSignature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataProductionSelfActorSignatureTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_ProductionSelfActorSignature");
	TSharedRef<FJsonObject> AuthoredProperties = MakeShared<FJsonObject>();
	AuthoredProperties->SetBoolField(TEXT("bDefaultValue"), true);
	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(
		BlackboardTestMakeBlackboardDataDocument(
			Target,
			BlackboardTestMakeBlackboardDataBody(nullptr, {
				BlackboardTestMakeCanonicalKey(TEXT("Authored"), UBlackboardKeyType_Bool::StaticClass(), AuthoredProperties),
			}))));
	TestTrue(TEXT("SelfActor signature fixture applies"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	FBlackboardEntry* SelfEntry = Blackboard ? Blackboard->Keys.FindByPredicate([](const FBlackboardEntry& Entry)
	{
		return Entry.EntryName == FBlackboard::KeySelf;
	}) : nullptr;
	TestNotNull(TEXT("UE persistent SelfActor exists after UpdateParentKeys"), SelfEntry);
	UBlackboardKeyType_Object* SelfObject = SelfEntry ? Cast<UBlackboardKeyType_Object>(SelfEntry->KeyType) : nullptr;
	TestTrue(TEXT("Baseline SelfActor has exact engine-derived signature"), SelfEntry && BlackboardTestIsEngineDerivedSelfKey(*SelfEntry));
	if (!SelfEntry || !SelfObject)
	{
		return false;
	}

	auto VerifyCanonicalKeys = [this, &Service, &Target](const FString& Label, int32 ExpectedCount, bool bExpectSelfActor)
	{
		FAssetDocumentExtractRequest ExtractRequest;
		ExtractRequest.AssetPath = Target;
		const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
		TestTrue(FString::Printf(TEXT("%s extracts"), *Label), ExtractResult.IsSuccess());
		TSharedPtr<FJsonObject> Body = GetExtractedBody(ExtractResult);
		const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
		const bool bHasKeys = Body.IsValid() && Body->TryGetArrayField(TEXT("Keys"), Keys) && Keys;
		TestTrue(FString::Printf(TEXT("%s has canonical Keys"), *Label), bHasKeys);
		TestEqual(FString::Printf(TEXT("%s canonical key count"), *Label), Keys ? Keys->Num() : -1, ExpectedCount);
		bool bHasSelfActor = false;
		if (Keys)
		{
			for (const TSharedPtr<FJsonValue>& KeyValue : *Keys)
			{
				const TSharedPtr<FJsonObject> KeyObject = KeyValue.IsValid() ? KeyValue->AsObject() : nullptr;
				FString Name;
				bHasSelfActor |= KeyObject.IsValid() && KeyObject->TryGetStringField(TEXT("Name"), Name) && Name == FBlackboard::KeySelf.ToString();
			}
		}
		TestEqual(FString::Printf(TEXT("%s SelfActor authored visibility"), *Label), bHasSelfActor, bExpectSelfActor);
	};

	VerifyCanonicalKeys(TEXT("Exact persistent signature"), 1, false);
	auto VerifyMismatch = [&VerifyCanonicalKeys](const FString& Label, TFunctionRef<void(bool)> SetMismatch)
	{
		SetMismatch(true);
		VerifyCanonicalKeys(Label, 2, true);
		SetMismatch(false);
	};
	VerifyMismatch(TEXT("Non-empty description"), [SelfEntry](bool bMismatch)
	{
		SelfEntry->EntryDescription = bMismatch ? TEXT("Authored description") : TEXT("");
	});
	VerifyMismatch(TEXT("Non-empty category"), [SelfEntry](bool bMismatch)
	{
		SelfEntry->EntryCategory = bMismatch ? FName(TEXT("Authored")) : NAME_None;
	});
	VerifyMismatch(TEXT("Synchronized SelfActor"), [SelfEntry](bool bMismatch)
	{
		SelfEntry->bInstanceSynced = bMismatch;
	});
	VerifyMismatch(TEXT("Non-null default"), [SelfObject](bool bMismatch)
	{
		SelfObject->DefaultValue = bMismatch ? AActor::StaticClass()->GetDefaultObject() : nullptr;
	});
	VerifyMismatch(TEXT("Non-default BaseClass"), [SelfObject](bool bMismatch)
	{
		SelfObject->BaseClass = bMismatch ? UObject::StaticClass() : AActor::StaticClass();
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataProductionStructDefaultTest,
	"AssetFactory.AssetDocument.BlackboardData.Production.StructInstancedDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataProductionStructDefaultTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_ProductionStructDefault");
	TSharedRef<FJsonObject> VectorProperties = MakeShared<FJsonObject>();
	VectorProperties->SetNumberField(TEXT("X"), 4.0);
	VectorProperties->SetNumberField(TEXT("Y"), 5.5);
	VectorProperties->SetNumberField(TEXT("Z"), -6.25);
	TSharedRef<FJsonObject> KeyProperties = MakeShared<FJsonObject>();
	KeyProperties->SetObjectField(TEXT("DefaultValue"), BlackboardTestMakeStructValue(TBaseStructure<FVector>::Get(), VectorProperties));
	TSharedRef<FJsonObject> Key = BlackboardTestMakeCanonicalKey(TEXT("Payload"), UBlackboardKeyType_Struct::StaticClass(), KeyProperties);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(nullptr, {Key}));
	const FAssetDocumentResult ApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(Document));
	TestTrue(TEXT("FInstancedStruct default applies"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	const TArray<const FBlackboardEntry*> AuthoredKeys = BlackboardTestGetAuthoredKeys(Blackboard);
	UBlackboardKeyType_Struct* StructKey = AuthoredKeys.Num() == 1
		? Cast<UBlackboardKeyType_Struct>(AuthoredKeys[0]->KeyType)
		: nullptr;
	TestNotNull(TEXT("Struct key materializes"), StructKey);
	TestTrue(TEXT("Struct default is valid"), StructKey && StructKey->DefaultValue.IsValid());
	if (StructKey && StructKey->DefaultValue.IsValid())
	{
		TestTrue(TEXT("Struct default type is FVector"), StructKey->DefaultValue.GetScriptStruct() == TBaseStructure<FVector>::Get());
		TestEqual(TEXT("Struct default data applies"), *reinterpret_cast<const FVector*>(StructKey->DefaultValue.GetMemory()), FVector(4.0, 5.5, -6.25));
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("FInstancedStruct default extracts"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedKeys = nullptr;
	TestTrue(TEXT("Struct extract has one key"), ExtractedBody.IsValid() && ExtractedBody->TryGetArrayField(TEXT("Keys"), ExtractedKeys) && ExtractedKeys && ExtractedKeys->Num() == 1);
	if (ExtractedKeys && ExtractedKeys->Num() == 1)
	{
		TSharedPtr<FJsonObject> ExtractedProperties = BlackboardTestGetKeyTypeProperties((*ExtractedKeys)[0]->AsObject());
		TSharedPtr<FJsonObject> ExtractedDefault = ExtractedProperties.IsValid() ? ExtractedProperties->GetObjectField(TEXT("DefaultValue")) : nullptr;
		TestTrue(TEXT("Struct canonical extract preserves type"), ExtractedDefault.IsValid()
			&& ExtractedDefault->GetStringField(TEXT("Struct")) == TBaseStructure<FVector>::Get()->GetPathName());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataProductionCustomReflectedPropertiesTest,
	"AssetFactory.AssetDocument.BlackboardData.Production.CustomReflectedProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataProductionCustomReflectedPropertiesTest::RunTest(const FString&)
{
	UClass* CustomClass = BlackboardTestCreateCustomKeyTypeClass(*this, TEXT("BP_AD_CustomBlackboardKeyType"));
	if (!CustomClass)
	{
		return false;
	}

	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("DefaultValue"), 77);
	Properties->SetStringField(TEXT("CustomLabel"), TEXT("Roundtrip label"));
	Properties->SetNumberField(TEXT("CustomWeight"), 2.25);
	TSharedRef<FJsonObject> Key = BlackboardTestMakeCanonicalKey(TEXT("Custom"), CustomClass, Properties);
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_ProductionCustomProperties");
	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(
		BlackboardTestMakeBlackboardDataDocument(Target, BlackboardTestMakeBlackboardDataBody(nullptr, {Key}))));
	TestTrue(TEXT("Custom key type reflected properties apply"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	const TArray<const FBlackboardEntry*> AuthoredKeys = BlackboardTestGetAuthoredKeys(Blackboard);
	UObject* KeyType = AuthoredKeys.Num() == 1 ? AuthoredKeys[0]->KeyType.Get() : nullptr;
	TestNotNull(TEXT("Custom key type materializes"), KeyType);
	TestEqual(TEXT("Custom key type class is preserved"), KeyType ? KeyType->GetClass() : nullptr, CustomClass);
	if (KeyType)
	{
		FStrProperty* LabelProperty = FindFProperty<FStrProperty>(CustomClass, TEXT("CustomLabel"));
		FFloatProperty* WeightProperty = FindFProperty<FFloatProperty>(CustomClass, TEXT("CustomWeight"));
		TestNotNull(TEXT("CustomLabel reflected property exists"), LabelProperty);
		TestNotNull(TEXT("CustomWeight reflected property exists"), WeightProperty);
		if (LabelProperty)
		{
			TestEqual(TEXT("CustomLabel applies"), LabelProperty->GetPropertyValue_InContainer(KeyType), FString(TEXT("Roundtrip label")));
		}
		if (WeightProperty)
		{
			TestEqual(TEXT("CustomWeight applies"), WeightProperty->GetPropertyValue_InContainer(KeyType), 2.25f);
		}
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Custom key type reflected properties extract"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedKeys = nullptr;
	if (ExtractedBody.IsValid() && ExtractedBody->TryGetArrayField(TEXT("Keys"), ExtractedKeys) && ExtractedKeys && ExtractedKeys->Num() == 1)
	{
		TSharedPtr<FJsonObject> ExtractedProperties = BlackboardTestGetKeyTypeProperties((*ExtractedKeys)[0]->AsObject());
		TestTrue(TEXT("CustomLabel extracts"), ExtractedProperties.IsValid()
			&& ExtractedProperties->GetStringField(TEXT("CustomLabel")) == TEXT("Roundtrip label"));
		TestTrue(TEXT("CustomWeight extracts"), ExtractedProperties.IsValid()
			&& ExtractedProperties->GetNumberField(TEXT("CustomWeight")) == 2.25);
	}
	else
	{
		AddError(TEXT("Custom reflected property extract is missing its key"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataProductionInvalidInputsTest,
	"AssetFactory.AssetDocument.BlackboardData.Production.InvalidKeyTypesAndDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataProductionInvalidInputsTest::RunTest(const FString&)
{
	FAssetDocumentService Service;
	auto ApplySingle = [&Service](const FString& Suffix, const TSharedRef<FJsonObject>& Key)
	{
		return Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
			FString::Printf(TEXT("/Game/AssetDocumentTests/BB_AD_Invalid_%s"), *Suffix),
			BlackboardTestMakeBlackboardDataBody(nullptr, {Key}))));
	};

	TSharedRef<FJsonObject> NativeEnumProperties = MakeShared<FJsonObject>();
	NativeEnumProperties->SetStringField(TEXT("EnumName"), StaticEnum<EBasicKeyOperation::Type>()->GetPathName());
	TSharedRef<FJsonObject> NativeEnumKey = BlackboardTestMakeCanonicalKey(TEXT("Legacy"), UBlackboardKeyType_NativeEnum::StaticClass(), NativeEnumProperties);
	const FAssetDocumentResult NativeEnumResult = ApplySingle(TEXT("NativeEnum"), NativeEnumKey);
	TestFalse(TEXT("Deprecated NativeEnum is rejected for new input"), NativeEnumResult.IsSuccess());
	TestTrue(TEXT("Deprecated NativeEnum has exact diagnostic"), BlackboardTestResultHasDiagnostic(
		NativeEnumResult,
		TEXT("DeprecatedBlackboardKeyType"),
		TEXT("/Body/Keys/Legacy/KeyTypeClass")));

	TSharedRef<FJsonObject> NullKey = MakeShared<FJsonObject>();
	NullKey->SetStringField(TEXT("Name"), TEXT("NullType"));
	NullKey->SetField(TEXT("KeyTypeClass"), MakeShared<FJsonValueNull>());
	NullKey->SetObjectField(TEXT("KeyTypeProperties"), MakeShared<FJsonObject>());
	const FAssetDocumentResult NullResult = ApplySingle(TEXT("NullType"), NullKey);
	TestFalse(TEXT("Null KeyTypeClass is rejected"), NullResult.IsSuccess());
	TestTrue(TEXT("Null KeyTypeClass has exact diagnostic"), BlackboardTestResultHasDiagnostic(
		NullResult,
		TEXT("NullBlackboardKeyType"),
		TEXT("/Body/Keys/NullType/KeyTypeClass")));

	TSharedRef<FJsonObject> UnknownProperties = MakeShared<FJsonObject>();
	UnknownProperties->SetNumberField(TEXT("RuntimeCache"), 1);
	const FAssetDocumentResult UnknownResult = ApplySingle(
		TEXT("UnknownProperty"),
		BlackboardTestMakeCanonicalKey(TEXT("Unknown"), UBlackboardKeyType_Int::StaticClass(), UnknownProperties));
	TestFalse(TEXT("Unknown key type property is rejected"), UnknownResult.IsSuccess());
	TestTrue(TEXT("Unknown key type property has exact diagnostic"), BlackboardTestResultHasDiagnostic(
		UnknownResult,
		TEXT("UnknownBlackboardKeyTypeProperty"),
		TEXT("/Body/Keys/Unknown/KeyTypeProperties/RuntimeCache")));

	TSharedRef<FJsonObject> InvalidObjectProperties = MakeShared<FJsonObject>();
	InvalidObjectProperties->SetObjectField(TEXT("BaseClass"), BlackboardTestMakeClassRef(AActor::StaticClass()->GetPathName()));
	InvalidObjectProperties->SetObjectField(
		TEXT("DefaultValue"),
		BlackboardTestMakeAssetRef(UTexture2D::StaticClass()->GetDefaultObject()->GetPathName()));
	const FAssetDocumentResult InvalidObjectResult = ApplySingle(
		TEXT("ObjectDefault"),
		BlackboardTestMakeCanonicalKey(TEXT("ObjectDefault"), UBlackboardKeyType_Object::StaticClass(), InvalidObjectProperties));
	TestFalse(TEXT("Object default outside BaseClass is rejected"), InvalidObjectResult.IsSuccess());
	TestTrue(TEXT("Object default diagnostic is exact"), BlackboardTestResultHasDiagnostic(
		InvalidObjectResult,
		TEXT("InvalidBlackboardKeyDefault"),
		TEXT("/Body/Keys/ObjectDefault/KeyTypeProperties/DefaultValue")));

	TSharedRef<FJsonObject> InvalidClassProperties = MakeShared<FJsonObject>();
	InvalidClassProperties->SetObjectField(TEXT("BaseClass"), BlackboardTestMakeClassRef(AActor::StaticClass()->GetPathName()));
	InvalidClassProperties->SetObjectField(TEXT("DefaultValue"), BlackboardTestMakeClassRef(UTexture2D::StaticClass()->GetPathName()));
	const FAssetDocumentResult InvalidClassResult = ApplySingle(
		TEXT("ClassDefault"),
		BlackboardTestMakeCanonicalKey(TEXT("ClassDefault"), UBlackboardKeyType_Class::StaticClass(), InvalidClassProperties));
	TestFalse(TEXT("Class default outside BaseClass is rejected"), InvalidClassResult.IsSuccess());
	TestTrue(TEXT("Class default diagnostic is exact"), BlackboardTestResultHasDiagnostic(
		InvalidClassResult,
		TEXT("InvalidBlackboardKeyDefault"),
		TEXT("/Body/Keys/ClassDefault/KeyTypeProperties/DefaultValue")));

	TSharedRef<FJsonObject> InvalidEnumProperties = MakeShared<FJsonObject>();
	InvalidEnumProperties->SetObjectField(TEXT("EnumType"), BlackboardTestMakeAssetRef(StaticEnum<EBasicKeyOperation::Type>()->GetPathName()));
	InvalidEnumProperties->SetStringField(TEXT("EnumName"), StaticEnum<EBasicKeyOperation::Type>()->GetPathName());
	InvalidEnumProperties->SetStringField(TEXT("DefaultValue"), TEXT("DoesNotExist"));
	const FAssetDocumentResult InvalidEnumResult = ApplySingle(
		TEXT("EnumDefault"),
		BlackboardTestMakeCanonicalKey(TEXT("EnumDefault"), UBlackboardKeyType_Enum::StaticClass(), InvalidEnumProperties));
	TestFalse(TEXT("Unknown enum default is rejected"), InvalidEnumResult.IsSuccess());
	TestTrue(TEXT("Unknown enum default diagnostic is exact"), BlackboardTestResultHasDiagnostic(
		InvalidEnumResult,
		TEXT("InvalidBlackboardKeyDefault"),
		TEXT("/Body/Keys/EnumDefault/KeyTypeProperties/DefaultValue")));

	TSharedRef<FJsonObject> InvalidStructProperties = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> InvalidVectorProperties = MakeShared<FJsonObject>();
	InvalidVectorProperties->SetNumberField(TEXT("MissingAxis"), 1.0);
	InvalidStructProperties->SetObjectField(TEXT("DefaultValue"), BlackboardTestMakeStructValue(TBaseStructure<FVector>::Get(), InvalidVectorProperties));
	const FAssetDocumentResult InvalidStructResult = ApplySingle(
		TEXT("StructDefault"),
		BlackboardTestMakeCanonicalKey(TEXT("StructDefault"), UBlackboardKeyType_Struct::StaticClass(), InvalidStructProperties));
	TestFalse(TEXT("Unknown FInstancedStruct field is rejected"), InvalidStructResult.IsSuccess());
	TestTrue(TEXT("Unknown FInstancedStruct field diagnostic is exact"), BlackboardTestResultHasDiagnostic(
		InvalidStructResult,
		TEXT("UnknownBlackboardStructField"),
		TEXT("/Body/Keys/StructDefault/KeyTypeProperties/DefaultValue/Properties/MissingAxis")));

	const FString MissingBaseRollbackTarget = TEXT("/Game/AssetDocumentTests/BB_AD_Invalid_MissingBaseRollback");
	TSharedRef<FJsonObject> StableProperties = MakeShared<FJsonObject>();
	StableProperties->SetNumberField(TEXT("DefaultValue"), 19);
	const FAssetDocumentResult StableApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(
		BlackboardTestMakeBlackboardDataDocument(
			MissingBaseRollbackTarget,
			BlackboardTestMakeBlackboardDataBody(nullptr, {
				BlackboardTestMakeCanonicalKey(TEXT("Stable"), UBlackboardKeyType_Int::StaticClass(), StableProperties),
			}))));
	TestTrue(TEXT("Missing BaseClass rollback fixture applies"), StableApplyResult.IsSuccess());
	UBlackboardData* StableBlackboard = LoadBlackboardForTarget(MissingBaseRollbackTarget);
	TArray<const FBlackboardEntry*> StableAuthoredKeys = BlackboardTestGetAuthoredKeys(StableBlackboard);
	UBlackboardKeyType* StableKeyObject = StableAuthoredKeys.Num() == 1 ? StableAuthoredKeys[0]->KeyType.Get() : nullptr;
	TestNotNull(TEXT("Missing BaseClass rollback fixture retains its key object"), StableKeyObject);

	TSharedRef<FJsonObject> MissingBaseProperties = MakeShared<FJsonObject>();
	MissingBaseProperties->SetField(TEXT("DefaultValue"), MakeShared<FJsonValueNull>());
	const FAssetDocumentResult MissingBaseResult = Service.Apply(BlackboardTestMakeApplyRequest(
		BlackboardTestMakeBlackboardDataDocument(
			MissingBaseRollbackTarget,
			BlackboardTestMakeBlackboardDataBody(nullptr, {
				BlackboardTestMakeCanonicalKey(TEXT("RequiresBase"), UBlackboardKeyType_Object::StaticClass(), MissingBaseProperties),
			}))));
	TestFalse(TEXT("Object key without explicitly authored BaseClass is rejected"), MissingBaseResult.IsSuccess());
	TestTrue(TEXT("Missing BaseClass diagnostic uses exact authored property path"), BlackboardTestResultHasDiagnostic(
		MissingBaseResult,
		TEXT("MissingBlackboardKeyBaseClass"),
		TEXT("/Body/Keys/RequiresBase/KeyTypeProperties/BaseClass")));
	StableAuthoredKeys = BlackboardTestGetAuthoredKeys(StableBlackboard);
	TestEqual(TEXT("Missing BaseClass rejection preserves authored key count"), StableAuthoredKeys.Num(), 1);
	if (StableAuthoredKeys.Num() == 1)
	{
		TestEqual(TEXT("Missing BaseClass rejection preserves key identity"), StableAuthoredKeys[0]->KeyType.Get(), StableKeyObject);
		TestEqual(TEXT("Missing BaseClass rejection preserves key default"), CastChecked<UBlackboardKeyType_Int>(StableAuthoredKeys[0]->KeyType)->DefaultValue, 19);
	}

	const FString InvalidExtractTarget = TEXT("/Game/AssetDocumentTests/BB_AD_Invalid_ExtractNullType");
	const FAssetDocumentResult EmptyApplyResult = Service.Apply(BlackboardTestMakeApplyRequest(
		BlackboardTestMakeBlackboardDataDocument(
			InvalidExtractTarget,
			BlackboardTestMakeBlackboardDataBody(nullptr, {}))));
	TestTrue(TEXT("Invalid extract fixture applies"), EmptyApplyResult.IsSuccess());
	UBlackboardData* InvalidExtractBlackboard = LoadBlackboardForTarget(InvalidExtractTarget);
	TestNotNull(TEXT("Invalid extract fixture loads"), InvalidExtractBlackboard);
	if (InvalidExtractBlackboard)
	{
		FBlackboardEntry InvalidEntry;
		InvalidEntry.EntryName = TEXT("BrokenExtract");
		InvalidExtractBlackboard->Keys.Add(InvalidEntry);
		FAssetDocumentExtractRequest InvalidExtractRequest;
		InvalidExtractRequest.AssetPath = InvalidExtractTarget;
		const FAssetDocumentResult InvalidExtractResult = Service.Extract(InvalidExtractRequest);
		TestFalse(TEXT("Null existing key type fails production extraction"), InvalidExtractResult.IsSuccess());
		TestTrue(TEXT("Null existing key type extraction diagnostic is propagated"), BlackboardTestResultHasDiagnostic(
			InvalidExtractResult,
			TEXT("NullBlackboardKeyType"),
			TEXT("/Body/Keys/BrokenExtract/KeyTypeClass")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataProductionOrderAndShadowTest,
	"AssetFactory.AssetDocument.BlackboardData.Production.OrderIdentityAndParentShadow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataProductionOrderAndShadowTest::RunTest(const FString&)
{
	FAssetDocumentService Service;
	const FString ParentTarget = TEXT("/Game/AssetDocumentTests/BB_AD_ProductionShadowParent");
	const FAssetDocumentResult ParentResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		ParentTarget,
		BlackboardTestMakeBlackboardDataBody(nullptr, {BlackboardTestMakeBlackboardKeyJson(TEXT("Shared"), TEXT("Bool"))}))));
	TestTrue(TEXT("Parent shadow fixture applies"), ParentResult.IsSuccess());

	const FString ShadowTarget = TEXT("/Game/AssetDocumentTests/BB_AD_ProductionShadowChild");
	const FAssetDocumentResult ShadowResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		ShadowTarget,
		BlackboardTestMakeBlackboardDataBody(
			BlackboardTestMakeAssetRef(ParentTarget),
			{BlackboardTestMakeBlackboardKeyJson(TEXT("Shared"), TEXT("Int"))}))));
	TestFalse(TEXT("Local key cannot shadow its parent chain"), ShadowResult.IsSuccess());
	TestTrue(TEXT("Parent shadow diagnostic uses key identity path"), BlackboardTestResultHasDiagnostic(
		ShadowResult,
		TEXT("BlackboardKeyShadowsParent"),
		TEXT("/Body/Keys/Shared/Name")));
	TestNull(TEXT("Rejected shadow does not leave a new asset"), FindObject<UBlackboardData>(nullptr, *BlackboardTestMakeObjectPathFromTarget(ShadowTarget)));

	const FString OrderedTarget = TEXT("/Game/AssetDocumentTests/BB_AD_ProductionOrderedIdentity");
	TSharedPtr<FJsonObject> OrderedDocument = BlackboardTestMakeBlackboardDataDocument(
		OrderedTarget,
		BlackboardTestMakeBlackboardDataBody(nullptr, {
			BlackboardTestMakeBlackboardKeyJson(TEXT("Third"), TEXT("Bool")),
			BlackboardTestMakeBlackboardKeyJson(TEXT("First"), TEXT("Int")),
			BlackboardTestMakeBlackboardKeyJson(TEXT("Second"), TEXT("Float")),
		}));
	const FAssetDocumentResult OrderedResult = Service.Apply(BlackboardTestMakeApplyRequest(OrderedDocument));
	TestTrue(TEXT("Authored local order applies"), OrderedResult.IsSuccess());
	UBlackboardData* Ordered = LoadBlackboardForTarget(OrderedTarget);
	const TArray<const FBlackboardEntry*> OrderedAuthoredKeys = BlackboardTestGetAuthoredKeys(Ordered);
	if (OrderedAuthoredKeys.Num() == 3)
	{
		TestEqual(TEXT("Local order position 0 is retained"), OrderedAuthoredKeys[0]->EntryName, FName(TEXT("Third")));
		TestEqual(TEXT("Local order position 1 is retained"), OrderedAuthoredKeys[1]->EntryName, FName(TEXT("First")));
		TestEqual(TEXT("Local order position 2 is retained"), OrderedAuthoredKeys[2]->EntryName, FName(TEXT("Second")));
		const int32 FirstAuthoredID = static_cast<int32>(Ordered->GetKeyID(OrderedAuthoredKeys[0]->EntryName));
		TestEqual(TEXT("Derived IDs preserve authored adjacency at 1"), static_cast<int32>(Ordered->GetKeyID(OrderedAuthoredKeys[1]->EntryName)), FirstAuthoredID + 1);
		TestEqual(TEXT("Derived IDs preserve authored adjacency at 2"), static_cast<int32>(Ordered->GetKeyID(OrderedAuthoredKeys[2]->EntryName)), FirstAuthoredID + 2);
	}
	else
	{
		AddError(TEXT("Ordered blackboard did not materialize three keys"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataProductionStagingRollbackTest,
	"AssetFactory.AssetDocument.BlackboardData.Production.StagingRollbackOnLateKeyFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataProductionStagingRollbackTest::RunTest(const FString&)
{
	FAssetDocumentService Service;
	const FString ParentTarget = TEXT("/Game/AssetDocumentTests/BB_AD_RollbackParent");
	TestTrue(TEXT("Rollback parent fixture applies"), Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		ParentTarget,
		BlackboardTestMakeBlackboardDataBody(nullptr, {BlackboardTestMakeBlackboardKeyJson(TEXT("ParentOnly"), TEXT("Bool"))})))).IsSuccess());

	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_ProductionRollback");
	TSharedRef<FJsonObject> InitialProperties = MakeShared<FJsonObject>();
	InitialProperties->SetNumberField(TEXT("DefaultValue"), 7);
	TSharedRef<FJsonObject> InitialKey = BlackboardTestMakeCanonicalKey(TEXT("Stable"), UBlackboardKeyType_Int::StaticClass(), InitialProperties);
	const FAssetDocumentResult InitialResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(nullptr, {InitialKey}))));
	TestTrue(TEXT("Rollback target fixture applies"), InitialResult.IsSuccess());
	UBlackboardData* Blackboard = LoadBlackboardForTarget(Target);
	TArray<const FBlackboardEntry*> AuthoredKeys = BlackboardTestGetAuthoredKeys(Blackboard);
	if (AuthoredKeys.Num() != 1)
	{
		return false;
	}
	UBlackboardKeyType* OriginalKeyObject = AuthoredKeys[0]->KeyType;
	const int32 OriginalDefault = CastChecked<UBlackboardKeyType_Int>(OriginalKeyObject)->DefaultValue;

	TSharedRef<FJsonObject> ChangedProperties = MakeShared<FJsonObject>();
	ChangedProperties->SetNumberField(TEXT("DefaultValue"), 11);
	TSharedRef<FJsonObject> ChangedKey = BlackboardTestMakeCanonicalKey(TEXT("Stable"), UBlackboardKeyType_Int::StaticClass(), ChangedProperties);
	TSharedRef<FJsonObject> BrokenProperties = MakeShared<FJsonObject>();
	BrokenProperties->SetNumberField(TEXT("DefaultValue"), 2);
	BrokenProperties->SetNumberField(TEXT("LateUnknown"), 3);
	TSharedRef<FJsonObject> BrokenKey = BlackboardTestMakeCanonicalKey(TEXT("Broken"), UBlackboardKeyType_Int::StaticClass(), BrokenProperties);
	const FAssetDocumentResult FailedResult = Service.Apply(BlackboardTestMakeApplyRequest(BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(BlackboardTestMakeAssetRef(ParentTarget), {ChangedKey, BrokenKey}))));
	TestFalse(TEXT("Late key staging failure rejects update"), FailedResult.IsSuccess());
	TestTrue(TEXT("Late key staging failure reports exact path"), BlackboardTestResultHasDiagnostic(
		FailedResult,
		TEXT("UnknownBlackboardKeyTypeProperty"),
		TEXT("/Body/Keys/Broken/KeyTypeProperties/LateUnknown")));
	TestNull(TEXT("Parent remains unchanged after staging failure"), Blackboard->Parent.Get());
	AuthoredKeys = BlackboardTestGetAuthoredKeys(Blackboard);
	TestEqual(TEXT("Authored local key count remains unchanged after staging failure"), AuthoredKeys.Num(), 1);
	if (AuthoredKeys.Num() == 1)
	{
		TestEqual(TEXT("Stable key object identity remains unchanged"), AuthoredKeys[0]->KeyType.Get(), OriginalKeyObject);
		TestEqual(TEXT("Stable key default remains unchanged"), CastChecked<UBlackboardKeyType_Int>(AuthoredKeys[0]->KeyType)->DefaultValue, OriginalDefault);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataProductionSaveReloadTest,
	"AssetFactory.AssetDocument.BlackboardData.Production.SaveReloadAuthoredSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBlackboardDataProductionSaveReloadTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BB_AD_ProductionSaveReload");
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("DefaultValue"), TEXT("Saved value"));
	TSharedRef<FJsonObject> Key = BlackboardTestMakeCanonicalKey(TEXT("SavedKey"), UBlackboardKeyType_String::StaticClass(), Properties);
	Key->SetStringField(TEXT("Description"), TEXT("Survives package reload"));
	Key->SetStringField(TEXT("Category"), TEXT("Persistence"));
	Key->SetBoolField(TEXT("bInstanceSynced"), true);
	TSharedPtr<FJsonObject> Document = BlackboardTestMakeBlackboardDataDocument(
		Target,
		BlackboardTestMakeBlackboardDataBody(nullptr, {Key}));

	FAssetDocumentApplyRequest ApplyRequest = BlackboardTestMakeApplyRequest(Document);
	ApplyRequest.bSaveAsset = true;
	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(ApplyRequest);
	TestTrue(TEXT("Complete authored surface saves"), ApplyResult.IsSuccess() && ApplyResult.bSavedAsset);
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBlackboardData* BeforeUnload = LoadBlackboardForTarget(Target);
	UPackage* Package = BeforeUnload ? BeforeUnload->GetOutermost() : nullptr;
	TestNotNull(TEXT("Saved package exists before unload"), Package);
	if (!Package)
	{
		return false;
	}
	TArray<UPackage*> PackagesToUnload = {Package};
	TestTrue(TEXT("Saved Blackboard package unloads"), UPackageTools::UnloadPackages(PackagesToUnload));
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	UBlackboardData* Reloaded = LoadBlackboardForTarget(Target);
	TestNotNull(TEXT("Saved Blackboard fresh-loads"), Reloaded);
	if (!Reloaded)
	{
		return false;
	}
	const FBlackboardEntry* ReloadedKey = Reloaded->Keys.FindByPredicate([](const FBlackboardEntry& Entry)
	{
		return Entry.EntryName == TEXT("SavedKey");
	});
	TestNotNull(TEXT("Fresh reload retains the authored key"), ReloadedKey);
	if (!ReloadedKey)
	{
		return false;
	}
	TestEqual(TEXT("Reload preserves Description"), ReloadedKey->EntryDescription, FString(TEXT("Survives package reload")));
	TestEqual(TEXT("Reload preserves Category"), ReloadedKey->EntryCategory, FName(TEXT("Persistence")));
	TestTrue(TEXT("Reload preserves bInstanceSynced"), ReloadedKey->bInstanceSynced != 0);
	TestEqual(TEXT("Reload preserves key default"), CastChecked<UBlackboardKeyType_String>(ReloadedKey->KeyType)->DefaultValue, FString(TEXT("Saved value")));

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Fresh-loaded Blackboard extracts"), ExtractResult.IsSuccess());
	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Fresh-loaded Blackboard diffs"), DiffResult.IsSuccess());
	TestTrue(TEXT("Fresh-loaded Blackboard canonical diff is empty"), BlackboardTestDiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

#endif
