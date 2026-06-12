// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfile.h"

#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentService.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
class FTestAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	virtual UClass* GetExactClass() const override
	{
		return UObject::StaticClass();
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

	TestNotNull(TEXT("Registry finds a profile for the exact registered class"), Registry.FindForClass(UObject::StaticClass()).Get());
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
	Request.ClassOrAsset = TEXT("/Script/AssetFactory.TestDataAsset");

	const FAssetDocumentResult Result = Service.InspectProfile(Request);
	TestTrue(TEXT("InspectProfile succeeds for reflected TestDataAsset class"), Result.IsSuccess());
	TestTrue(TEXT("InspectProfile returns a payload"), Result.Payload.IsValid());

	if (!Result.Payload.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Profile reports canonical class path"), Result.Payload->GetStringField(TEXT("Class")), FString(TEXT("/Script/AssetFactory.TestDataAsset")));

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
	Request.Class = TEXT("/Script/AssetFactory.TestDataAsset");
	Request.Target = TEXT("/Game/AssetDocumentTests/DA_Template");

	const FAssetDocumentResult Result = Service.CreateTemplate(Request);
	TestTrue(TEXT("CreateTemplate succeeds for reflected TestDataAsset class"), Result.IsSuccess());
	TestTrue(TEXT("CreateTemplate returns a payload"), Result.Payload.IsValid());

	if (!Result.Payload.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Template schema version is canonical"), static_cast<int32>(Result.Payload->GetNumberField(TEXT("SchemaVersion"))), 1);
	TestEqual(TEXT("Template target is preserved"), Result.Payload->GetStringField(TEXT("Target")), Request.Target);
	TestEqual(TEXT("Template class is canonical"), Result.Payload->GetStringField(TEXT("Class")), FString(TEXT("/Script/AssetFactory.TestDataAsset")));
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
