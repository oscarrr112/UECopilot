// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentModule.h"
#include "AssetDocumentService.h"
#include "Profiles/UBlueprintAssetDocumentCapability.h"
#include "Profiles/UBlueprintAssetDocumentProfile.h"

#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
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

TSharedRef<FJsonObject> MakeActorParentClassRef()
{
	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Actor"));
	return ParentClass;
}

TSharedRef<FJsonObject> MakeClassRef(const FString& ClassPath)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), ClassPath);
	return ClassRef;
}

TSharedRef<FJsonObject> MakeFloatPinType()
{
	TSharedRef<FJsonObject> Type = MakeShared<FJsonObject>();
	Type->SetStringField(TEXT("PinCategory"), TEXT("real"));
	Type->SetStringField(TEXT("PinSubCategory"), TEXT("float"));
	return Type;
}

TSharedPtr<FJsonObject> MakeFloatVariable(const TCHAR* Name, const TCHAR* DefaultValue, const TCHAR* Category = nullptr, const TCHAR* Tooltip = nullptr)
{
	TSharedPtr<FJsonObject> Variable = MakeShared<FJsonObject>();
	Variable->SetStringField(TEXT("Name"), Name);
	Variable->SetObjectField(TEXT("Type"), MakeFloatPinType());
	Variable->SetStringField(TEXT("DefaultValue"), DefaultValue);
	if (Category)
	{
		Variable->SetStringField(TEXT("Category"), Category);
	}
	if (Tooltip)
	{
		Variable->SetStringField(TEXT("Tooltip"), Tooltip);
	}
	return Variable;
}

TArray<TSharedPtr<FJsonValue>> MakeVariableArray(std::initializer_list<TSharedPtr<FJsonObject>> Variables)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const TSharedPtr<FJsonObject>& Variable : Variables)
	{
		Result.Add(MakeShared<FJsonValueObject>(Variable));
	}
	return Result;
}

TSharedPtr<FJsonObject> MakeUBlueprintDocument(
	const FString& Target,
	const FString& ParentClassPath,
	const TOptional<TArray<TSharedPtr<FJsonValue>>>& Variables,
	const TOptional<TArray<TSharedPtr<FJsonValue>>>& ImplementedInterfaces)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Blueprint"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeClassRef(ParentClassPath));
	if (ImplementedInterfaces.IsSet())
	{
		Body->SetArrayField(TEXT("ImplementedInterfaces"), ImplementedInterfaces.GetValue());
	}
	if (Variables.IsSet())
	{
		Body->SetArrayField(TEXT("Variables"), Variables.GetValue());
	}
	Body->SetArrayField(TEXT("Components"), {});
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("UbergraphPages"), {});
	Body->SetArrayField(TEXT("FunctionGraphs"), {});
	Body->SetArrayField(TEXT("MacroGraphs"), {});
	Body->SetArrayField(TEXT("Timelines"), {});
	Document->SetObjectField(TEXT("Body"), Body);
	return Document;
}

TSharedPtr<FJsonObject> LoadBlueprintDocumentBody(TSharedPtr<FJsonObject> Document)
{
	const TSharedPtr<FJsonObject>* Body = nullptr;
	return Document.IsValid() && Document->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid()
		? *Body
		: nullptr;
}

UBlueprint* LoadBlueprintForTarget(const FString& Target)
{
	return LoadObject<UBlueprint>(nullptr, *FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target)));
}

bool HasBlueprintVariable(const UBlueprint* Blueprint, FName Name)
{
	return Blueprint && Blueprint->NewVariables.ContainsByPredicate([Name](const FBPVariableDescription& Variable)
	{
		return Variable.VarName == Name;
	});
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

TSharedPtr<FJsonObject> FindDiffEntryByPath(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& ExpectedPath)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		FString Path;
		if (Object.IsValid() && Object->TryGetStringField(TEXT("path"), Path) && Path == ExpectedPath)
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
	TestTrue(TEXT("Template includes Definitions"), Template->HasTypedField<EJson::Object>(TEXT("Definitions")));
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
	ValidEmptyBody->SetObjectField(TEXT("ParentClass"), MakeActorParentClassRef());
	ValidEmptyBody->SetArrayField(TEXT("UbergraphPages"), {});
	ValidEmptyBody->SetArrayField(TEXT("FunctionGraphs"), {});
	ValidEmptyBody->SetArrayField(TEXT("MacroGraphs"), {});
	ValidEmptyBody->SetArrayField(TEXT("Timelines"), {});
	TestTrue(TEXT("Empty protected regions pass validation"), Capability.Validate(CapabilityContext, MakeBodyValue(ValidEmptyBody)).bSuccess);

	TSharedRef<FJsonObject> UnknownBody = MakeShared<FJsonObject>();
	UnknownBody->SetObjectField(TEXT("ParentClass"), MakeActorParentClassRef());
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
		Body->SetObjectField(TEXT("ParentClass"), MakeActorParentClassRef());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintCreateTest,
	"AssetFactory.AssetDocument.UBlueprint.Create",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintCreateTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_Create_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

	auto MakeCreateDocument = [](const FString& InTarget, const FString& ParentClassPath)
	{
		TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
		Document->SetNumberField(TEXT("SchemaVersion"), 1);
		Document->SetStringField(TEXT("Target"), InTarget);
		Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Blueprint"));
		Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
		Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
		Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

		TSharedPtr<FJsonObject> ParentClass = MakeShared<FJsonObject>();
		ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
		ParentClass->SetStringField(TEXT("Class"), ParentClassPath);

		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetObjectField(TEXT("ParentClass"), ParentClass);
		Body->SetArrayField(TEXT("ImplementedInterfaces"), {});
		Body->SetArrayField(TEXT("Variables"), {});
		Body->SetArrayField(TEXT("Components"), {});
		Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
		Body->SetArrayField(TEXT("UbergraphPages"), {});
		Body->SetArrayField(TEXT("FunctionGraphs"), {});
		Body->SetArrayField(TEXT("MacroGraphs"), {});
		Body->SetArrayField(TEXT("Timelines"), {});
		Document->SetObjectField(TEXT("Body"), Body);
		return Document;
	};

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = MakeCreateDocument(Target, TEXT("/Script/Engine.Actor"));
	Request.bSaveAsset = false;

	const FAssetDocumentResult Result = Service.Apply(Request);
	if (!Result.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Apply failed: %s"), *Result.Message));
	}
	TestTrue(TEXT("Blueprint apply succeeds"), Result.IsSuccess());

	UObject* Created = LoadObject<UObject>(nullptr, *FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target)));
	UBlueprint* Blueprint = Cast<UBlueprint>(Created);
	TestNotNull(TEXT("Created asset is UBlueprint"), Blueprint);
	if (Blueprint)
	{
		TestEqual(TEXT("Parent class is Actor"), Blueprint->ParentClass.Get(), AActor::StaticClass());
	}

	const FString PawnTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_CreatePawn_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentApplyRequest PawnRequest;
	PawnRequest.Document = MakeCreateDocument(PawnTarget, TEXT("/Script/Engine.Pawn"));
	PawnRequest.bSaveAsset = false;

	const FAssetDocumentResult PawnResult = Service.Apply(PawnRequest);
	if (!PawnResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Pawn apply failed: %s"), *PawnResult.Message));
	}
	TestTrue(TEXT("Blueprint apply with Pawn parent succeeds"), PawnResult.IsSuccess());

	UObject* CreatedPawnBlueprint = LoadObject<UObject>(nullptr, *FString::Printf(TEXT("%s.%s"), *PawnTarget, *FPackageName::GetLongPackageAssetName(PawnTarget)));
	UBlueprint* PawnBlueprint = Cast<UBlueprint>(CreatedPawnBlueprint);
	TestNotNull(TEXT("Created Pawn-parent asset is UBlueprint"), PawnBlueprint);
	if (PawnBlueprint)
	{
		TestEqual(TEXT("Parent class is Pawn"), PawnBlueprint->ParentClass.Get(), APawn::StaticClass());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintRequiresParentClassTest,
	"AssetFactory.AssetDocument.UBlueprint.RequiresParentClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintRequiresParentClassTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_MissingParent_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Blueprint"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("ImplementedInterfaces"), {});
	Body->SetArrayField(TEXT("Variables"), {});
	Body->SetArrayField(TEXT("Components"), {});
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("UbergraphPages"), {});
	Body->SetArrayField(TEXT("FunctionGraphs"), {});
	Body->SetArrayField(TEXT("MacroGraphs"), {});
	Body->SetArrayField(TEXT("Timelines"), {});
	Document->SetObjectField(TEXT("Body"), Body);

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;

	const FAssetDocumentResult Result = Service.Apply(Request);
	TestFalse(TEXT("Blueprint apply without ParentClass fails"), Result.IsSuccess());
	TestTrue(TEXT("Failure mentions ParentClass"), Result.Message.Contains(TEXT("ParentClass")));

	UObject* Created = FindObject<UObject>(nullptr, *FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target)));
	TestNull(TEXT("Missing ParentClass does not create a Blueprint asset"), Created);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintVariablesAuthoritativeTest,
	"AssetFactory.AssetDocument.UBlueprint.VariablesAuthoritative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintVariablesAuthoritativeTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_Vars_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

	auto MakeDocument = [&Target](bool bIncludeStamina)
	{
		TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
		Document->SetNumberField(TEXT("SchemaVersion"), 1);
		Document->SetStringField(TEXT("Target"), Target);
		Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Blueprint"));
		Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
		Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
		Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

		TSharedPtr<FJsonObject> ParentClass = MakeShared<FJsonObject>();
		ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
		ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Actor"));

		TArray<TSharedPtr<FJsonValue>> Variables;
		auto AddFloatVariable = [&Variables](const TCHAR* Name, const TCHAR* DefaultValue)
		{
			TSharedPtr<FJsonObject> Type = MakeShared<FJsonObject>();
			Type->SetStringField(TEXT("PinCategory"), TEXT("real"));
			Type->SetStringField(TEXT("PinSubCategory"), TEXT("float"));

			TSharedPtr<FJsonObject> Variable = MakeShared<FJsonObject>();
			Variable->SetStringField(TEXT("Name"), Name);
			Variable->SetObjectField(TEXT("Type"), Type);
			Variable->SetStringField(TEXT("DefaultValue"), DefaultValue);
			Variables.Add(MakeShared<FJsonValueObject>(Variable));
		};

		AddFloatVariable(TEXT("Health"), TEXT("100.0"));
		if (bIncludeStamina)
		{
			AddFloatVariable(TEXT("Stamina"), TEXT("50.0"));
		}

		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetObjectField(TEXT("ParentClass"), ParentClass);
		Body->SetArrayField(TEXT("ImplementedInterfaces"), {});
		Body->SetArrayField(TEXT("Variables"), Variables);
		Body->SetArrayField(TEXT("Components"), {});
		Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
		Body->SetArrayField(TEXT("UbergraphPages"), {});
		Body->SetArrayField(TEXT("FunctionGraphs"), {});
		Body->SetArrayField(TEXT("MacroGraphs"), {});
		Body->SetArrayField(TEXT("Timelines"), {});
		Document->SetObjectField(TEXT("Body"), Body);
		return Document;
	};

	FAssetDocumentService Service;

	FAssetDocumentApplyRequest FirstRequest;
	FirstRequest.Document = MakeDocument(true);
	FirstRequest.bSaveAsset = false;
	const FAssetDocumentResult FirstResult = Service.Apply(FirstRequest);
	if (!FirstResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Initial apply failed: %s"), *FirstResult.Message));
	}
	TestTrue(TEXT("Initial variable apply succeeds"), FirstResult.IsSuccess());

	FAssetDocumentApplyRequest SecondRequest;
	SecondRequest.Document = MakeDocument(false);
	SecondRequest.bSaveAsset = false;
	const FAssetDocumentResult SecondResult = Service.Apply(SecondRequest);
	if (!SecondResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Second apply failed: %s"), *SecondResult.Message));
	}
	TestTrue(TEXT("Second variable apply succeeds"), SecondResult.IsSuccess());

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target)));
	TestNotNull(TEXT("Blueprint exists"), Blueprint);
	if (Blueprint)
	{
		TestTrue(TEXT("Health remains"), Blueprint->NewVariables.ContainsByPredicate([](const FBPVariableDescription& Variable)
		{
			return Variable.VarName == TEXT("Health");
		}));
		TestFalse(TEXT("Stamina was removed"), Blueprint->NewVariables.ContainsByPredicate([](const FBPVariableDescription& Variable)
		{
			return Variable.VarName == TEXT("Stamina");
		}));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintInvalidAuthoritativeArraysTest,
	"AssetFactory.AssetDocument.UBlueprint.InvalidAuthoritativeArrays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintInvalidAuthoritativeArraysTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_InvalidArrays_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentService Service;

	auto ApplyValidHealth = [&]()
	{
		FAssetDocumentApplyRequest Request;
		Request.Document = MakeUBlueprintDocument(
			Target,
			TEXT("/Script/Engine.Actor"),
			MakeVariableArray({MakeFloatVariable(TEXT("Health"), TEXT("100.0"))}),
			TArray<TSharedPtr<FJsonValue>>{});
		Request.bSaveAsset = false;
		const FAssetDocumentResult Result = Service.Apply(Request);
		TestTrue(TEXT("Valid Health apply succeeds"), Result.IsSuccess());
	};

	ApplyValidHealth();

	TArray<TSharedPtr<FJsonValue>> InvalidVariables;
	InvalidVariables.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
	InvalidVariables.Add(MakeShared<FJsonValueString>(TEXT("not an array")));
	InvalidVariables.Add(MakeShared<FJsonValueNull>());

	for (const TSharedPtr<FJsonValue>& InvalidValue : InvalidVariables)
	{
		ApplyValidHealth();

		TSharedPtr<FJsonObject> Document = MakeUBlueprintDocument(
			Target,
			TEXT("/Script/Engine.Actor"),
			MakeVariableArray({MakeFloatVariable(TEXT("Health"), TEXT("100.0"))}),
			TArray<TSharedPtr<FJsonValue>>{});
		TSharedPtr<FJsonObject> Body = LoadBlueprintDocumentBody(Document);
		check(Body.IsValid());
		Body->SetField(TEXT("Variables"), InvalidValue);

		FAssetDocumentApplyRequest Request;
		Request.Document = Document;
		Request.bSaveAsset = false;
		const FAssetDocumentResult Result = Service.Apply(Request);
		TestFalse(TEXT("Invalid Variables field fails apply"), Result.IsSuccess());

		UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
		TestTrue(TEXT("Health remains after invalid Variables field"), HasBlueprintVariable(Blueprint, TEXT("Health")));
	}

	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> InterfaceObjectBody = MakeShared<FJsonObject>();
	InterfaceObjectBody->SetObjectField(TEXT("ParentClass"), MakeActorParentClassRef());
	InterfaceObjectBody->SetObjectField(TEXT("ImplementedInterfaces"), MakeShared<FJsonObject>());
	TestFalse(TEXT("ImplementedInterfaces object fails validation"), Capability.Validate(Context, MakeBodyValue(InterfaceObjectBody)).bSuccess);

	TSharedRef<FJsonObject> InterfaceStringBody = MakeShared<FJsonObject>();
	InterfaceStringBody->SetObjectField(TEXT("ParentClass"), MakeActorParentClassRef());
	InterfaceStringBody->SetStringField(TEXT("ImplementedInterfaces"), TEXT("not an array"));
	TestFalse(TEXT("ImplementedInterfaces string fails validation"), Capability.Validate(Context, MakeBodyValue(InterfaceStringBody)).bSuccess);

	TSharedRef<FJsonObject> InterfaceNullBody = MakeShared<FJsonObject>();
	InterfaceNullBody->SetObjectField(TEXT("ParentClass"), MakeActorParentClassRef());
	InterfaceNullBody->SetField(TEXT("ImplementedInterfaces"), MakeShared<FJsonValueNull>());
	TestFalse(TEXT("ImplementedInterfaces null fails validation"), Capability.Validate(Context, MakeBodyValue(InterfaceNullBody)).bSuccess);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintParentChangeRequiresAuthoritativeRegionsTest,
	"AssetFactory.AssetDocument.UBlueprint.ParentChangeRequiresAuthoritativeRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintParentChangeRequiresAuthoritativeRegionsTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_ParentGuard_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentService Service;

	FAssetDocumentApplyRequest CreateRequest;
	CreateRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		MakeVariableArray({MakeFloatVariable(TEXT("Health"), TEXT("100.0"))}),
		TArray<TSharedPtr<FJsonValue>>{});
	CreateRequest.bSaveAsset = false;
	TestTrue(TEXT("Initial actor parent apply succeeds"), Service.Apply(CreateRequest).IsSuccess());

	FAssetDocumentApplyRequest ParentOnlyRequest;
	ParentOnlyRequest.Document = MakeUBlueprintDocument(Target, TEXT("/Script/Engine.Pawn"), TOptional<TArray<TSharedPtr<FJsonValue>>>(), TOptional<TArray<TSharedPtr<FJsonValue>>>());
	ParentOnlyRequest.bSaveAsset = false;
	const FAssetDocumentResult ParentOnlyResult = Service.Apply(ParentOnlyRequest);
	TestFalse(TEXT("Parent change without Variables and ImplementedInterfaces fails"), ParentOnlyResult.IsSuccess());

	UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
	TestNotNull(TEXT("Blueprint still exists"), Blueprint);
	if (Blueprint)
	{
		TestEqual(TEXT("Parent remains Actor"), Blueprint->ParentClass.Get(), AActor::StaticClass());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintApplyFailureRollsBackTest,
	"AssetFactory.AssetDocument.UBlueprint.ApplyFailureRollsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintApplyFailureRollsBackTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_Rollback_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentService Service;

	FAssetDocumentApplyRequest CreateRequest;
	CreateRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		MakeVariableArray({MakeFloatVariable(TEXT("Health"), TEXT("100.0"))}),
		TArray<TSharedPtr<FJsonValue>>{});
	CreateRequest.bSaveAsset = false;
	TestTrue(TEXT("Initial rollback fixture apply succeeds"), Service.Apply(CreateRequest).IsSuccess());

	TSharedPtr<FJsonObject> ConflictingVariable = MakeFloatVariable(TEXT("Controller"), TEXT("0.0"));
	FAssetDocumentApplyRequest BadRequest;
	BadRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Pawn"),
		MakeVariableArray({ConflictingVariable}),
		TArray<TSharedPtr<FJsonValue>>{});
	BadRequest.bSaveAsset = false;
	const FAssetDocumentResult BadResult = Service.Apply(BadRequest);
	TestFalse(TEXT("Conflicting variable apply fails"), BadResult.IsSuccess());

	UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
	TestNotNull(TEXT("Blueprint still exists after rollback failure"), Blueprint);
	if (Blueprint)
	{
		TestEqual(TEXT("Parent rolled back to Actor"), Blueprint->ParentClass.Get(), AActor::StaticClass());
		TestTrue(TEXT("Health remains after rollback failure"), HasBlueprintVariable(Blueprint, TEXT("Health")));
		TestFalse(TEXT("Controller was not added after rollback failure"), HasBlueprintVariable(Blueprint, TEXT("Controller")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintVariableMetadataAuthoritativeTest,
	"AssetFactory.AssetDocument.UBlueprint.VariableMetadataAuthoritative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintVariableMetadataAuthoritativeTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_Metadata_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentService Service;

	FAssetDocumentApplyRequest FirstRequest;
	FirstRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		MakeVariableArray({MakeFloatVariable(TEXT("Health"), TEXT("100.0"), TEXT("Stats"), TEXT("Hit points"))}),
		TArray<TSharedPtr<FJsonValue>>{});
	FirstRequest.bSaveAsset = false;
	TestTrue(TEXT("Initial metadata apply succeeds"), Service.Apply(FirstRequest).IsSuccess());

	FAssetDocumentApplyRequest SecondRequest;
	SecondRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		MakeVariableArray({MakeFloatVariable(TEXT("Health"), TEXT("100.0"))}),
		TArray<TSharedPtr<FJsonValue>>{});
	SecondRequest.bSaveAsset = false;
	TestTrue(TEXT("Metadata-clearing apply succeeds"), Service.Apply(SecondRequest).IsSuccess());

	UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
	TestNotNull(TEXT("Metadata Blueprint exists"), Blueprint);
	if (Blueprint)
	{
		const FBPVariableDescription* Health = Blueprint->NewVariables.FindByPredicate([](const FBPVariableDescription& Variable)
		{
			return Variable.VarName == TEXT("Health");
		});
		TestNotNull(TEXT("Health variable exists"), Health);
		if (Health)
		{
			TestTrue(TEXT("Category is cleared"), Health->Category.IsEmpty());
			TestFalse(TEXT("Tooltip metadata is cleared"), Health->HasMetaData(FBlueprintMetadata::MD_Tooltip));
		}

		const FUBlueprintAssetDocumentCapability Capability;
		FAssetDocumentCapabilityContext DiffContext;
		DiffContext.Asset = Blueprint;
		DiffContext.AssetClass = UBlueprint::StaticClass();

		TArray<TSharedPtr<FJsonValue>> DiffEntries;
		TSharedPtr<FJsonObject> DesiredDocument = MakeUBlueprintDocument(
			Target,
			TEXT("/Script/Engine.Actor"),
			MakeVariableArray({MakeFloatVariable(TEXT("Health"), TEXT("100.0"))}),
			TArray<TSharedPtr<FJsonValue>>{});
		TSharedPtr<FJsonObject> DesiredBody = LoadBlueprintDocumentBody(DesiredDocument);
		check(DesiredBody.IsValid());
		const FAssetDocumentCapabilityResult DiffResult = Capability.Diff(DiffContext, MakeBodyValue(DesiredBody.ToSharedRef()), DiffEntries);
		TestTrue(TEXT("Metadata-cleared diff succeeds"), DiffResult.bSuccess);
		if (TSharedPtr<FJsonObject> Entry = FindDiffEntryByPath(DiffEntries, TEXT("/Body/Variables/Health")))
		{
			FString EntryJson;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&EntryJson);
			FJsonSerializer::Serialize(Entry.ToSharedRef(), Writer);
			TestEqual(*FString::Printf(TEXT("Metadata-cleared variable diff is unchanged: %s"), *EntryJson), Entry->GetStringField(TEXT("status")), FString(TEXT("unchanged")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintDiffExplicitRegionsTest,
	"AssetFactory.AssetDocument.UBlueprint.DiffExplicitRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintDiffExplicitRegionsTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = NewObject<UBlueprint>(GetTransientPackage(), UBlueprint::StaticClass());
	Blueprint->ParentClass = AActor::StaticClass();
	FBPVariableDescription Health;
	Health.VarName = TEXT("Health");
	Health.VarGuid = FGuid::NewGuid();
	Health.VarType.PinCategory = UEdGraphSchema_K2::PC_Real;
	Health.VarType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	Health.DefaultValue = TEXT("100.0");
	Blueprint->NewVariables.Add(Health);

	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.Asset = Blueprint;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> ParentOnlyBody = MakeShared<FJsonObject>();
	ParentOnlyBody->SetObjectField(TEXT("ParentClass"), MakeActorParentClassRef());
	TArray<TSharedPtr<FJsonValue>> ParentOnlyDiffEntries;
	const FAssetDocumentCapabilityResult ParentOnlyDiffResult = Capability.Diff(Context, MakeBodyValue(ParentOnlyBody), ParentOnlyDiffEntries);
	TestTrue(TEXT("Parent-only diff succeeds"), ParentOnlyDiffResult.bSuccess);
	TestNull(TEXT("Omitted Variables region produces no variable diff entry"), FindDiffEntryByPath(ParentOnlyDiffEntries, TEXT("/Body/Variables/Health")).Get());

	TSharedPtr<FJsonObject> InterfaceEntry = MakeShared<FJsonObject>();
	InterfaceEntry->SetObjectField(TEXT("Interface"), MakeClassRef(TEXT("/Script/Engine.ActorSoundParameterInterface")));
	TArray<TSharedPtr<FJsonValue>> DesiredInterfaces;
	DesiredInterfaces.Add(MakeShared<FJsonValueObject>(InterfaceEntry));

	TSharedRef<FJsonObject> InterfaceBody = MakeShared<FJsonObject>();
	InterfaceBody->SetObjectField(TEXT("ParentClass"), MakeActorParentClassRef());
	InterfaceBody->SetArrayField(TEXT("ImplementedInterfaces"), DesiredInterfaces);
	TArray<TSharedPtr<FJsonValue>> InterfaceDiffEntries;
	const FAssetDocumentCapabilityResult InterfaceDiffResult = Capability.Diff(Context, MakeBodyValue(InterfaceBody), InterfaceDiffEntries);
	TestTrue(TEXT("Interface diff succeeds"), InterfaceDiffResult.bSuccess);
	TSharedPtr<FJsonObject> InterfaceDiff = FindDiffEntryByPath(InterfaceDiffEntries, TEXT("/Body/ImplementedInterfaces//Script/Engine.ActorSoundParameterInterface"));
	TestTrue(TEXT("Missing desired interface is reported"), InterfaceDiff.IsValid());
	if (InterfaceDiff.IsValid())
	{
		TestEqual(TEXT("Missing interface diff is changed"), InterfaceDiff->GetStringField(TEXT("status")), FString(TEXT("changed")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintExtractSkipsUnsupportedPinTypesTest,
	"AssetFactory.AssetDocument.UBlueprint.ExtractSkipsUnsupportedPinTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintExtractSkipsUnsupportedPinTypesTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = NewObject<UBlueprint>(GetTransientPackage(), UBlueprint::StaticClass());
	Blueprint->ParentClass = AActor::StaticClass();

	FBPVariableDescription Scores;
	Scores.VarName = TEXT("Scores");
	Scores.VarGuid = FGuid::NewGuid();
	Scores.VarType.PinCategory = UEdGraphSchema_K2::PC_Int;
	Scores.VarType.ContainerType = EPinContainerType::Array;
	Blueprint->NewVariables.Add(Scores);

	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.Asset = Blueprint;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
	TestTrue(TEXT("Extract succeeds with unsupported variable pin type"), ExtractResult.bSuccess);

	const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
	TestTrue(TEXT("Extract writes Variables array"), ExtractedBody->TryGetArrayField(TEXT("Variables"), Variables));
	TestEqual(TEXT("Unsupported variable is not emitted as simplified Type"), Variables ? Variables->Num() : -1, 0);

	const TSharedPtr<FJsonObject>* Skipped = nullptr;
	TestTrue(TEXT("Extract records skipped diagnostics"), ExtractedBody->TryGetObjectField(TEXT("_Skipped"), Skipped));
	if (Skipped && Skipped->IsValid())
	{
		TestTrue(TEXT("Skipped diagnostics include Variables"), (*Skipped)->HasField(TEXT("Variables")));
	}
	return true;
}

#endif
