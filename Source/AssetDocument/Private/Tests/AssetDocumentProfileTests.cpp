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

#endif
