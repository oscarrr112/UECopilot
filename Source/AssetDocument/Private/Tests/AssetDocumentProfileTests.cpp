// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfile.h"

#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentService.h"
#include "TestDataAsset.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const TSharedPtr<FJsonObject> FindObjectByStringField(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& FieldName, const FString& ExpectedValue)
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

void TestNoAgentFacingOperationFields(FAutomationTestBase* Test, const FString& Context, const TSharedPtr<FJsonObject>& Object)
{
	if (!Test || !Object.IsValid())
	{
		return;
	}

	for (const TCHAR* FieldName : {TEXT("patch"), TEXT("op"), TEXT("Patch"), TEXT("Op"), TEXT("Operations")})
	{
		Test->TestFalse(FString::Printf(TEXT("%s does not expose %s field"), *Context, FieldName), Object->HasField(FieldName));
	}
}

class FTestAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	virtual UClass* GetExactClass() const override
	{
		return UTestDataAsset::StaticClass();
	}

	virtual TSharedRef<FJsonObject> GetDocumentShape() const override
	{
		TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
		Shape->SetStringField(TEXT("Name"), TEXT("TestShape"));
		return Shape;
	}

	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override
	{
		TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
		Template->SetStringField(TEXT("Target"), Context.Target);
		Template->SetStringField(TEXT("Class"), Context.ClassPath);
		Template->SetBoolField(TEXT("ExactProfileTemplate"), true);
		return Template;
	}

	virtual TArray<FName> GetBodyKeys() const override
	{
		return {TEXT("TestBody")};
	}

	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override
	{
		return nullptr;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentProfileRegistryTest,
	"AssetFactory.AssetDocument.Profile.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentProfileRegistryTest::RunTest(const FString& Parameters)
{
	FAssetDocumentProfileRegistry Registry;
	Registry.Register(MakeShared<FTestAssetDocumentProfile>());

	TestNotNull(TEXT("Registry finds a profile for the exact registered class"), Registry.FindForClass(UTestDataAsset::StaticClass()).Get());
	TestNull(TEXT("Registry does not return profiles for unregistered classes"), Registry.FindForClass(UPackage::StaticClass()).Get());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGenericInspectProfileTest,
	"AssetFactory.AssetDocument.Profile.GenericInspect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGenericInspectProfileTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentService Service;
	FAssetDocumentProfileRequest Request;
	Request.ClassOrAsset = TEXT("/Script/AssetFactory.TestActorBase");

	const FAssetDocumentResult Result = Service.InspectProfile(Request);
	TestTrue(TEXT("InspectProfile succeeds for reflected TestActorBase class"), Result.IsSuccess());
	TestTrue(TEXT("InspectProfile returns a payload"), Result.Payload.IsValid());

	if (!Result.Payload.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Profile reports canonical class path"), Result.Payload->GetStringField(TEXT("Class")), FString(TEXT("/Script/AssetFactory.TestActorBase")));

	const TSharedPtr<FJsonObject>* DocumentShape = nullptr;
	TestTrue(TEXT("Profile includes DocumentShape"), Result.Payload->TryGetObjectField(TEXT("DocumentShape"), DocumentShape));
	if (DocumentShape && DocumentShape->IsValid())
	{
		TestEqual(TEXT("Definitions shape is generic map"), (*DocumentShape)->GetStringField(TEXT("Definitions")), FString(TEXT("map<string, Fragment>")));
		TestEqual(TEXT("Properties shape describes reflected CDO-diff properties"), (*DocumentShape)->GetStringField(TEXT("Properties")), FString(TEXT("reflected CDO-diff properties")));

		const TSharedPtr<FJsonObject>* BodyShape = nullptr;
		TestTrue(TEXT("Body shape is an object"), (*DocumentShape)->TryGetObjectField(TEXT("Body"), BodyShape));
		if (BodyShape && BodyShape->IsValid())
		{
			TestEqual(TEXT("Generic Body shape has no entries"), (*BodyShape)->Values.Num(), 0);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* BodySections = nullptr;
	TestTrue(TEXT("Profile includes BodySections"), Result.Payload->TryGetArrayField(TEXT("BodySections"), BodySections));
	if (BodySections)
	{
		TestEqual(TEXT("Generic profile has no BodySections"), BodySections->Num(), 0);
	}

	const TArray<TSharedPtr<FJsonValue>>* FragmentKinds = nullptr;
	TestTrue(TEXT("Profile includes FragmentKinds"), Result.Payload->TryGetArrayField(TEXT("FragmentKinds"), FragmentKinds));
	if (FragmentKinds)
	{
		TestEqual(TEXT("Generic profile exposes five fragment kinds"), FragmentKinds->Num(), 5);
	}

	const TArray<TSharedPtr<FJsonValue>>* RegionPolicies = nullptr;
	TestTrue(TEXT("Generic profile includes RegionPolicies"), Result.Payload->TryGetArrayField(TEXT("RegionPolicies"), RegionPolicies));
	if (RegionPolicies)
	{
		TestEqual(TEXT("Generic profile has no exact region policies"), RegionPolicies->Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGenericExtractDoesNotForceSyncRegionsTest,
	"AssetFactory.AssetDocument.Profile.GenericExtractDoesNotForceSyncRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGenericExtractDoesNotForceSyncRegionsTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentService Service;
	FAssetDocumentExtractRequest Request;
	Request.AssetPath = TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial");
	Request.bDiffOnly = true;
	Request.bIncludeAllWritable = false;

	const FAssetDocumentResult Result = Service.Extract(Request);
	TestTrue(TEXT("Generic extract succeeds for engine material"), Result.IsSuccess());
	TestTrue(TEXT("Generic extract returns payload"), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Meta = nullptr;
	if (Result.Payload->TryGetObjectField(TEXT("_meta"), Meta) && Meta && Meta->IsValid())
	{
		const TSharedPtr<FJsonObject>* Sync = nullptr;
		if ((*Meta)->TryGetObjectField(TEXT("sync"), Sync) && Sync && Sync->IsValid())
		{
			const TSharedPtr<FJsonObject>* Regions = nullptr;
			TestFalse(TEXT("Generic extract does not force sync regions"), (*Sync)->TryGetObjectField(TEXT("regions"), Regions) && Regions && Regions->IsValid() && (*Regions)->Values.Num() > 0);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGenericCreateTemplateTest,
	"AssetFactory.AssetDocument.Profile.GenericTemplate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGenericCreateTemplateTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentService Service;
	FAssetDocumentTemplateRequest Request;
	Request.Class = TEXT("/Script/AssetFactory.TestActorBase");
	Request.Target = TEXT("/Game/AssetDocumentTests/DA_Template.DA_Template");

	const FAssetDocumentResult Result = Service.CreateTemplate(Request);
	TestTrue(TEXT("CreateTemplate succeeds for reflected TestActorBase class"), Result.IsSuccess());
	TestTrue(TEXT("CreateTemplate returns a payload"), Result.Payload.IsValid());

	if (!Result.Payload.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Template schema version is canonical"), static_cast<int32>(Result.Payload->GetNumberField(TEXT("SchemaVersion"))), 1);
	TestEqual(TEXT("Template target is normalized to package path"), Result.Payload->GetStringField(TEXT("Target")), FString(TEXT("/Game/AssetDocumentTests/DA_Template")));
	TestEqual(TEXT("Result target is normalized to package path"), Result.Target, FString(TEXT("/Game/AssetDocumentTests/DA_Template")));
	TestEqual(TEXT("Template class is canonical"), Result.Payload->GetStringField(TEXT("Class")), FString(TEXT("/Script/AssetFactory.TestActorBase")));
	TestEqual(TEXT("Template action is CreateOrUpdate"), Result.Payload->GetStringField(TEXT("Action")), FString(TEXT("CreateOrUpdate")));
	TestFalse(TEXT("Generic template does not include AssetType"), Result.Payload->HasField(TEXT("AssetType")));

	const TSharedPtr<FJsonObject>* Definitions = nullptr;
	TestTrue(TEXT("Template includes Definitions object"), Result.Payload->TryGetObjectField(TEXT("Definitions"), Definitions));
	if (Definitions && Definitions->IsValid())
	{
		TestEqual(TEXT("Template Definitions starts empty"), (*Definitions)->Values.Num(), 0);
	}

	const TSharedPtr<FJsonObject>* Properties = nullptr;
	TestTrue(TEXT("Template includes Properties object"), Result.Payload->TryGetObjectField(TEXT("Properties"), Properties));
	if (Properties && Properties->IsValid())
	{
		TestEqual(TEXT("Template Properties starts empty"), (*Properties)->Values.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGenericCreateTemplateRejectsInvalidTargetTest,
	"AssetFactory.AssetDocument.Profile.GenericTemplateRejectsInvalidTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGenericCreateTemplateRejectsInvalidTargetTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentService Service;
	FAssetDocumentTemplateRequest Request;
	Request.Class = TEXT("/Script/AssetFactory.TestDataAsset");
	Request.Target = TEXT("AssetDocumentTests/DA_InvalidTarget");

	const FAssetDocumentResult Result = Service.CreateTemplate(Request);
	TestFalse(TEXT("CreateTemplate rejects non-/Game target"), Result.IsSuccess());
	TestTrue(TEXT("CreateTemplate reports long package name error"), Result.Message.Contains(TEXT("must be a long package name under /Game")));
	TestEqual(TEXT("Invalid result target is normalized request target"), Result.Target, Request.Target);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentExactProfileServiceTest,
	"AssetFactory.AssetDocument.Profile.ExactProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentExactProfileServiceTest::RunTest(const FString& Parameters)
{
	TSharedRef<FTestAssetDocumentProfile> TestProfile = MakeShared<FTestAssetDocumentProfile>();
	FAssetDocumentService::GetProfileRegistry().Register(TestProfile);

	TestEqual(TEXT("Default profile region policies are empty"), TestProfile->GetRegionPolicies().Num(), 0);
	FAssetDocumentRegionPolicy MissingPolicy;
	TestFalse(TEXT("Default profile lookup returns false"), TestProfile->GetRegionPolicy(TEXT("Body.TestBody"), MissingPolicy));

	const FAssetDocumentService Service;
	FAssetDocumentProfileRequest ProfileRequest;
	ProfileRequest.ClassOrAsset = TEXT("/Script/AssetFactory.TestDataAsset");

	const FAssetDocumentResult ProfileResult = Service.InspectProfile(ProfileRequest);
	TestTrue(TEXT("InspectProfile succeeds for exact registered profile"), ProfileResult.IsSuccess());
	TestTrue(TEXT("InspectProfile exact profile returns payload"), ProfileResult.Payload.IsValid());
	if (!ProfileResult.Payload.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* DocumentShape = nullptr;
	TestTrue(TEXT("Exact profile exposes its document shape"), ProfileResult.Payload->TryGetObjectField(TEXT("DocumentShape"), DocumentShape));
	if (DocumentShape && DocumentShape->IsValid())
	{
		TestEqual(TEXT("Exact profile document shape is used"), (*DocumentShape)->GetStringField(TEXT("Name")), FString(TEXT("TestShape")));
	}

	const TArray<TSharedPtr<FJsonValue>>* BodySections = nullptr;
	TestTrue(TEXT("Exact profile includes BodySections"), ProfileResult.Payload->TryGetArrayField(TEXT("BodySections"), BodySections));
	if (BodySections)
	{
		TestEqual(TEXT("Exact profile body section count"), BodySections->Num(), 1);
		TestEqual(TEXT("Exact profile body section name"), (*BodySections)[0]->AsString(), FString(TEXT("TestBody")));
	}

	FAssetDocumentTemplateRequest TemplateRequest;
	TemplateRequest.Class = TEXT("/Script/AssetFactory.TestDataAsset");
	TemplateRequest.Target = TEXT("/Game/AssetDocumentTests/DA_Exact.DA_Exact");
	const FAssetDocumentResult TemplateResult = Service.CreateTemplate(TemplateRequest);
	TestTrue(TEXT("CreateTemplate succeeds for exact registered profile"), TemplateResult.IsSuccess());
	TestTrue(TEXT("CreateTemplate exact profile returns payload"), TemplateResult.Payload.IsValid());
	if (TemplateResult.Payload.IsValid())
	{
		TestEqual(TEXT("Exact template receives normalized target"), TemplateResult.Payload->GetStringField(TEXT("Target")), FString(TEXT("/Game/AssetDocumentTests/DA_Exact")));
		TestEqual(TEXT("Exact template receives canonical class"), TemplateResult.Payload->GetStringField(TEXT("Class")), FString(TEXT("/Script/AssetFactory.TestDataAsset")));
		TestTrue(TEXT("Exact template path was used"), TemplateResult.Payload->GetBoolField(TEXT("ExactProfileTemplate")));
	}

	const FAssetDocumentResult SchemaResult = Service.GetSchema();
	TestTrue(TEXT("GetSchema succeeds with registered profile"), SchemaResult.IsSuccess());
	TestTrue(TEXT("GetSchema returns payload with registered profile"), SchemaResult.Payload.IsValid());
	if (SchemaResult.Payload.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* RegisteredProfiles = nullptr;
		TestTrue(TEXT("Schema includes registered_profiles"), SchemaResult.Payload->TryGetArrayField(TEXT("registered_profiles"), RegisteredProfiles));
		if (RegisteredProfiles)
		{
			bool bFoundObjectProfile = false;
			for (const TSharedPtr<FJsonValue>& Entry : *RegisteredProfiles)
			{
				const TSharedPtr<FJsonObject> EntryObject = Entry.IsValid() ? Entry->AsObject() : nullptr;
				if (EntryObject.IsValid())
				{
					FString ClassPath;
					if (EntryObject->TryGetStringField(TEXT("Class"), ClassPath) && ClassPath == TEXT("/Script/AssetFactory.TestDataAsset"))
					{
						const TArray<TSharedPtr<FJsonValue>>* RegionPolicies = nullptr;
						TestTrue(TEXT("Schema registered profile includes RegionPolicies"), EntryObject->TryGetArrayField(TEXT("RegionPolicies"), RegionPolicies));
						if (RegionPolicies)
						{
							TestEqual(TEXT("Schema exact TestDataAsset profile has empty region policies"), RegionPolicies->Num(), 0);
						}
						bFoundObjectProfile = true;
						break;
					}
				}
			}
			TestTrue(TEXT("Schema registered_profiles includes exact TestDataAsset profile"), bFoundObjectProfile);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGenericSchemaTest,
	"AssetFactory.AssetDocument.Profile.Schema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGenericSchemaTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentService Service;

	const FAssetDocumentResult Result = Service.GetSchema();
	TestTrue(TEXT("GetSchema succeeds"), Result.IsSuccess());
	TestTrue(TEXT("GetSchema returns a payload"), Result.Payload.IsValid());

	if (!Result.Payload.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Schema version is 1"), static_cast<int32>(Result.Payload->GetNumberField(TEXT("schema_version"))), 1);

	const TArray<TSharedPtr<FJsonValue>>* RegionPolicyPresets = nullptr;
	TestTrue(TEXT("Schema includes RegionPolicyPresets"), Result.Payload->TryGetArrayField(TEXT("RegionPolicyPresets"), RegionPolicyPresets));
	if (RegionPolicyPresets)
	{
		const TSharedPtr<FJsonObject> DefaultDiffPreset = FindObjectByStringField(*RegionPolicyPresets, TEXT("PresetName"), TEXT("DefaultDiff"));
		TestTrue(TEXT("Schema includes DefaultDiff policy preset"), DefaultDiffPreset.IsValid());
		if (DefaultDiffPreset.IsValid())
		{
			const TSharedPtr<FJsonObject>* Defaults = nullptr;
			TestTrue(TEXT("DefaultDiff preset includes Defaults"), DefaultDiffPreset->TryGetObjectField(TEXT("Defaults"), Defaults));
			if (Defaults && Defaults->IsValid())
			{
				TestEqual(TEXT("DefaultDiff preset reducer is exported"), (*Defaults)->GetStringField(TEXT("ReducerMode")), FString(TEXT("DefaultDiff")));
				TestEqual(TEXT("DefaultDiff preset apply mode is exported"), (*Defaults)->GetStringField(TEXT("ApplyMode")), FString(TEXT("SetProperty")));
				TestNoAgentFacingOperationFields(this, TEXT("DefaultDiff preset"), *Defaults);
			}
		}

		TestTrue(TEXT("Schema includes ManagedRegion policy preset"), FindObjectByStringField(*RegionPolicyPresets, TEXT("PresetName"), TEXT("ManagedRegion")).IsValid());
		TestTrue(TEXT("Schema includes ExtensionHook policy preset"), FindObjectByStringField(*RegionPolicyPresets, TEXT("PresetName"), TEXT("ExtensionHook")).IsValid());
	}

	const TSharedPtr<FJsonObject>* FieldNaming = nullptr;
	TestTrue(TEXT("Schema includes field_naming"), Result.Payload->TryGetObjectField(TEXT("field_naming"), FieldNaming));
	if (FieldNaming && FieldNaming->IsValid())
	{
		TestTrue(TEXT("Schema bans abbreviations"), (*FieldNaming)->GetBoolField(TEXT("ban_abbreviations")));
		TestTrue(TEXT("Schema uses UE stable field names"), (*FieldNaming)->GetBoolField(TEXT("use_ue_stable_field_names")));
	}

	return true;
}

#endif
