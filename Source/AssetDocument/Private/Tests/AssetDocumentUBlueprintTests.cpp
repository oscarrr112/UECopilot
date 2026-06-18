// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentModule.h"
#include "AssetDocumentService.h"
#include "Profiles/UBlueprintAssetDocumentCapability.h"
#include "Profiles/UBlueprintAssetDocumentProfile.h"

#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const FAssetDocumentRegionPolicy* FindPolicyByRegionId(const TArray<FAssetDocumentRegionPolicy>& Policies, FName RegionId)
{
	for (const FAssetDocumentRegionPolicy& Policy : Policies)
	{
		if (Policy.RegionId == RegionId)
		{
			return &Policy;
		}
	}
	return nullptr;
}

TSharedRef<FJsonValue> MakeBodyValue(const TSharedRef<FJsonObject>& Body)
{
	return StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(Body));
}

bool ResultHasDiagnostic(const FAssetDocumentCapabilityResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

TSharedPtr<FJsonObject> FindJsonObjectByStringField(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& FieldName, const FString& ExpectedValue)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		FString ActualValue;
		if (Object.IsValid() && Object->TryGetStringField(FieldName, ActualValue) && ActualValue == ExpectedValue)
		{
			return Object;
		}
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintProfileTest,
	"AssetFactory.AssetDocument.UBlueprint.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintProfileTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentProfile Profile;
	const FUBlueprintAssetDocumentCapability Capability;
	TestEqual(TEXT("Exact class is UBlueprint"), Profile.GetExactClass(), UBlueprint::StaticClass());
	TestTrue(TEXT("SupportsClass accepts exact UBlueprint"), Capability.SupportsClass(UBlueprint::StaticClass()));
	TestFalse(TEXT("SupportsClass rejects UBlueprintGeneratedClass"), Capability.SupportsClass(UBlueprintGeneratedClass::StaticClass()));

	if (UClass* WidgetBlueprintClass = StaticLoadClass(UBlueprint::StaticClass(), nullptr, TEXT("/Script/UMGEditor.WidgetBlueprint")))
	{
		TestFalse(TEXT("SupportsClass rejects UWidgetBlueprint"), Capability.SupportsClass(WidgetBlueprintClass));
	}
	if (UClass* AnimBlueprintClass = StaticLoadClass(UBlueprint::StaticClass(), nullptr, TEXT("/Script/Engine.AnimBlueprint")))
	{
		TestFalse(TEXT("SupportsClass rejects UAnimBlueprint"), Capability.SupportsClass(AnimBlueprintClass));
	}

	UBlueprint* ExactBlueprint = NewObject<UBlueprint>(GetTransientPackage(), UBlueprint::StaticClass());
	TestTrue(TEXT("SupportsAsset accepts exact UBlueprint asset"), Capability.SupportsAsset(ExactBlueprint));

	const TArray<FName> ExpectedBodyKeys = {
		TEXT("ParentClass"),
		TEXT("ImplementedInterfaces"),
		TEXT("Variables"),
		TEXT("Components"),
		TEXT("ClassDefaults"),
		TEXT("UbergraphPages"),
		TEXT("FunctionGraphs"),
		TEXT("MacroGraphs"),
		TEXT("Timelines"),
	};

	const TArray<FName> BodyKeys = Profile.GetBodyKeys();
	for (const FName& ExpectedKey : ExpectedBodyKeys)
	{
		TestTrue(FString::Printf(TEXT("Body key %s exists"), *ExpectedKey.ToString()), BodyKeys.Contains(ExpectedKey));
		TestNotNull(FString::Printf(TEXT("Body key %s resolves adapter"), *ExpectedKey.ToString()), Profile.ResolveBodyAdapter(ExpectedKey));
	}
	TestNotNull(TEXT("Body root resolves adapter"), Profile.ResolveBodyAdapter(TEXT("Body")));

	FAssetDocumentTemplateContext Context;
	Context.Target = TEXT("/Game/AssetDocumentTests/BP_Template");
	Context.ClassPath = TEXT("/Script/Engine.Blueprint");
	const TSharedRef<FJsonObject> Template = Profile.CreateTemplate(Context);

	TestEqual(TEXT("Template Class is Blueprint"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/Engine.Blueprint")));
	TestEqual(TEXT("Template Target is preserved"), Template->GetStringField(TEXT("Target")), Context.Target);
	TestTrue(TEXT("Template includes Action"), Template->HasTypedField<EJson::String>(TEXT("Action")));
	TestTrue(TEXT("Template includes Properties"), Template->HasTypedField<EJson::Object>(TEXT("Properties")));
	TestFalse(TEXT("Template does not use legacy AssetType"), Template->HasField(TEXT("AssetType")));

	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template contains Body"), Template->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid());
	if (Body && Body->IsValid())
	{
		for (const FName& ExpectedKey : ExpectedBodyKeys)
		{
			TestTrue(FString::Printf(TEXT("Template Body contains %s"), *ExpectedKey.ToString()), (*Body)->HasField(ExpectedKey.ToString()));
		}
	}

	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	TestNotNull(TEXT("ParentClass has region policy"), FindPolicyByRegionId(Policies, TEXT("Body.ParentClass")));
	TestNotNull(TEXT("ImplementedInterfaces has region policy"), FindPolicyByRegionId(Policies, TEXT("Body.ImplementedInterfaces")));
	TestNotNull(TEXT("Variables has region policy"), FindPolicyByRegionId(Policies, TEXT("Body.Variables")));
	TestNotNull(TEXT("Components has region policy"), FindPolicyByRegionId(Policies, TEXT("Body.Components")));
	TestNotNull(TEXT("ClassDefaults has region policy"), FindPolicyByRegionId(Policies, TEXT("Body.ClassDefaults")));

	FAssetDocumentModule::Get();
	FAssetDocumentService Service;
	const FAssetDocumentResult SchemaResult = Service.GetSchema();
	TestTrue(TEXT("Schema succeeds after module registration"), SchemaResult.IsSuccess());
	TestTrue(TEXT("Schema returns payload"), SchemaResult.Payload.IsValid());
	if (SchemaResult.Payload.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* RegisteredProfiles = nullptr;
		TestTrue(TEXT("Schema includes registered_profiles"), SchemaResult.Payload->TryGetArrayField(TEXT("registered_profiles"), RegisteredProfiles));
		if (RegisteredProfiles)
		{
			TSharedPtr<FJsonObject> BlueprintProfile = FindJsonObjectByStringField(*RegisteredProfiles, TEXT("Class"), TEXT("/Script/Engine.Blueprint"));
			TestTrue(TEXT("Registered profiles include UBlueprint"), BlueprintProfile.IsValid());
			if (BlueprintProfile.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* BodySections = nullptr;
				TestTrue(TEXT("UBlueprint schema entry includes BodySections"), BlueprintProfile->TryGetArrayField(TEXT("BodySections"), BodySections));
				TestTrue(TEXT("UBlueprint schema entry includes ParentClass"), BodySections && BodySections->ContainsByPredicate([](const TSharedPtr<FJsonValue>& Value)
				{
					return Value.IsValid() && Value->Type == EJson::String && Value->AsString() == TEXT("ParentClass");
				}));
			}
		}
	}

	FAssetDocumentCapabilityContext CapabilityContext;
	CapabilityContext.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> ValidEmptyBody = MakeShared<FJsonObject>();
	ValidEmptyBody->SetArrayField(TEXT("UbergraphPages"), {});
	ValidEmptyBody->SetArrayField(TEXT("FunctionGraphs"), {});
	ValidEmptyBody->SetArrayField(TEXT("MacroGraphs"), {});
	ValidEmptyBody->SetArrayField(TEXT("Timelines"), {});
	TestTrue(TEXT("Empty protected regions pass validation"), Capability.Validate(CapabilityContext, MakeBodyValue(ValidEmptyBody)).bSuccess);

	TSharedRef<FJsonObject> UnknownBody = MakeShared<FJsonObject>();
	UnknownBody->SetObjectField(TEXT("UnexpectedSection"), MakeShared<FJsonObject>());
	const FAssetDocumentCapabilityResult UnknownResult = Capability.Validate(CapabilityContext, MakeBodyValue(UnknownBody));
	TestFalse(TEXT("Unknown Body key fails validation"), UnknownResult.bSuccess);
	TestTrue(TEXT("Unknown Body key diagnostic is precise"), ResultHasDiagnostic(UnknownResult, TEXT("/Body/UnexpectedSection"), TEXT("UnknownBodyKey")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintUnsupportedGraphProtectionTest,
	"AssetFactory.AssetDocument.UBlueprint.UnsupportedGraphProtection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintUnsupportedGraphProtectionTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	const TArray<FString> ProtectedRegions = {
		TEXT("UbergraphPages"),
		TEXT("FunctionGraphs"),
		TEXT("MacroGraphs"),
		TEXT("Timelines"),
	};

	for (const FString& Region : ProtectedRegions)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
		Body->SetArrayField(Region, Values);

		const FAssetDocumentCapabilityResult Result = Capability.Validate(Context, MakeBodyValue(Body));
		TestFalse(FString::Printf(TEXT("Non-empty %s fails validation"), *Region), Result.bSuccess);
		TestTrue(FString::Printf(TEXT("%s failure mentions unsupported region"), *Region), Result.Message.Contains(Region));
		TestTrue(FString::Printf(TEXT("%s diagnostic is precise"), *Region), ResultHasDiagnostic(Result, FString::Printf(TEXT("/Body/%s"), *Region), TEXT("UnsupportedUBlueprintRegion")));
	}

	return true;
}

#endif
