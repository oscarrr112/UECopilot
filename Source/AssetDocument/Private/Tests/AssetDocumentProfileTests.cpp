// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfile.h"

#include "AssetDocumentProfileRegistry.h"

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

	virtual FName GetDocumentShape() const override
	{
		return TEXT("TestShape");
	}

	virtual TSharedPtr<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override
	{
		TSharedPtr<FJsonObject> Template = MakeShared<FJsonObject>();
		Template->SetStringField(TEXT("Target"), Context.Target);
		Template->SetStringField(TEXT("Class"), Context.ClassPath);
		return Template;
	}

	virtual TArray<FString> GetBodyKeys() const override
	{
		return {TEXT("TestBody")};
	}

	virtual TSharedPtr<IAssetDocumentCapability> ResolveBodyAdapter(const FString& BodyKey) const override
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

#endif
