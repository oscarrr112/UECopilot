// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentModule.h"
#include "AssetDocumentService.h"
#include "Profiles/UBlueprintAssetDocumentCapability.h"
#include "Profiles/UBlueprintAssetDocumentProfile.h"

#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Components/SphereComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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
	const TOptional<TArray<TSharedPtr<FJsonValue>>>& ImplementedInterfaces,
	const TOptional<TArray<TSharedPtr<FJsonValue>>>& Components = TOptional<TArray<TSharedPtr<FJsonValue>>>(),
	TSharedPtr<FJsonObject> ClassDefaults = nullptr)
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
	Body->SetArrayField(TEXT("Components"), Components.IsSet() ? Components.GetValue() : TArray<TSharedPtr<FJsonValue>>{});
	Body->SetObjectField(TEXT("ClassDefaults"), ClassDefaults.IsValid() ? ClassDefaults : MakeShared<FJsonObject>());
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

TSharedPtr<FJsonObject> MakeComponentKey(const TCHAR* Name, const TCHAR* OwnerClass = TEXT("Self"))
{
	TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
	Key->SetStringField(TEXT("Name"), Name);
	Key->SetStringField(TEXT("OwnerClass"), OwnerClass);
	return Key;
}

TSharedPtr<FJsonObject> MakeOwnedSphereComponent(const TCHAR* Name, double SphereRadius)
{
	TSharedPtr<FJsonObject> Component = MakeShared<FJsonObject>();
	Component->SetObjectField(TEXT("Key"), MakeComponentKey(Name));
	Component->SetStringField(TEXT("Scope"), TEXT("OwnedSCS"));
	Component->SetStringField(TEXT("Class"), TEXT("/Script/Engine.SphereComponent"));
	Component->SetObjectField(TEXT("AttachTo"), MakeComponentKey(TEXT("DefaultSceneRoot")));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("SphereRadius"), SphereRadius);
	Component->SetObjectField(TEXT("Properties"), Properties);
	return Component;
}

TSharedPtr<FJsonObject> MakeOwnedSceneComponent(const TCHAR* Name)
{
	TSharedPtr<FJsonObject> Component = MakeShared<FJsonObject>();
	Component->SetObjectField(TEXT("Key"), MakeComponentKey(Name));
	Component->SetStringField(TEXT("Scope"), TEXT("OwnedSCS"));
	Component->SetStringField(TEXT("Class"), TEXT("/Script/Engine.SceneComponent"));
	Component->SetObjectField(TEXT("AttachTo"), MakeComponentKey(TEXT("DefaultSceneRoot")));
	Component->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	return Component;
}

TSharedPtr<FJsonObject> MakeReferencedComponent(const TCHAR* Scope, const TCHAR* Name, const TCHAR* OwnerClass)
{
	TSharedPtr<FJsonObject> Component = MakeShared<FJsonObject>();
	Component->SetObjectField(TEXT("Key"), MakeComponentKey(Name, OwnerClass));
	Component->SetStringField(TEXT("Scope"), Scope);
	Component->SetStringField(TEXT("Class"), TEXT("/Script/Engine.SceneComponent"));
	Component->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	return Component;
}

TArray<TSharedPtr<FJsonValue>> MakeComponentArray(std::initializer_list<TSharedPtr<FJsonObject>> Components)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const TSharedPtr<FJsonObject>& Component : Components)
	{
		Result.Add(MakeShared<FJsonValueObject>(Component));
	}
	return Result;
}

USCS_Node* FindSCSNodeByVariableName(const UBlueprint* Blueprint, FName VariableName)
{
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return nullptr;
	}

	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName() == VariableName)
		{
			return Node;
		}
	}
	return nullptr;
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

bool ResultHasDiagnosticCode(const FAssetDocumentCapabilityResult& Result, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == Code;
	});
}

bool ResultHasDiagnostic(const FAssetDocumentResult& Result, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == Code;
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

TSharedPtr<FJsonObject> FindComponentByScopeAndKey(
	const TArray<TSharedPtr<FJsonValue>>& Values,
	const FString& Scope,
	const FString& Name,
	const FString& OwnerClass)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Object.IsValid())
		{
			continue;
		}

		FString ActualScope;
		if (!Object->TryGetStringField(TEXT("Scope"), ActualScope) || ActualScope != Scope)
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* Key = nullptr;
		if (!Object->TryGetObjectField(TEXT("Key"), Key) || !Key || !Key->IsValid())
		{
			continue;
		}

		FString ActualName;
		FString ActualOwnerClass;
		if ((*Key)->TryGetStringField(TEXT("Name"), ActualName)
			&& (*Key)->TryGetStringField(TEXT("OwnerClass"), ActualOwnerClass)
			&& ActualName == Name
			&& ActualOwnerClass == OwnerClass)
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

bool IsUnchangedDiffEntry(const TSharedPtr<FJsonObject>& Entry)
{
	if (!Entry.IsValid())
	{
		return false;
	}

	FString Status;
	return Entry->TryGetStringField(TEXT("status"), Status)
		&& Status == TEXT("unchanged")
		&& !Entry->HasField(TEXT("change"));
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
	FAssetDocumentUBlueprintOwnedSCSComponentsAuthoritativeTest,
	"AssetFactory.AssetDocument.UBlueprint.OwnedSCSComponentsAuthoritative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintOwnedSCSComponentsAuthoritativeTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_Components_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentService Service;

	FAssetDocumentApplyRequest FirstRequest;
	FirstRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{});
	LoadBlueprintDocumentBody(FirstRequest.Document)->SetArrayField(
		TEXT("Components"),
		MakeComponentArray({
			MakeOwnedSphereComponent(TEXT("Sensor"), 500.0),
			MakeOwnedSphereComponent(TEXT("Vision"), 250.0),
		}));
	FirstRequest.bSaveAsset = false;

	const FAssetDocumentResult FirstResult = Service.Apply(FirstRequest);
	if (!FirstResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Initial component apply failed: %s"), *FirstResult.Message));
	}
	TestTrue(TEXT("Initial component apply succeeds"), FirstResult.IsSuccess());

	FAssetDocumentApplyRequest SecondRequest;
	SecondRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{});
	LoadBlueprintDocumentBody(SecondRequest.Document)->SetArrayField(
		TEXT("Components"),
		MakeComponentArray({MakeOwnedSphereComponent(TEXT("Sensor"), 500.0)}));
	SecondRequest.bSaveAsset = false;

	const FAssetDocumentResult SecondResult = Service.Apply(SecondRequest);
	if (!SecondResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Second component apply failed: %s"), *SecondResult.Message));
	}
	TestTrue(TEXT("Second component apply succeeds"), SecondResult.IsSuccess());

	UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
	TestNotNull(TEXT("Blueprint exists"), Blueprint);
	if (Blueprint)
	{
		USCS_Node* Sensor = FindSCSNodeByVariableName(Blueprint, TEXT("Sensor"));
		USCS_Node* Vision = FindSCSNodeByVariableName(Blueprint, TEXT("Vision"));
		USCS_Node* DefaultSceneRoot = FindSCSNodeByVariableName(Blueprint, USceneComponent::GetDefaultSceneRootVariableName());

		TestNotNull(TEXT("Sensor component remains"), Sensor);
		TestNull(TEXT("Vision component was removed"), Vision);
		TestNotNull(TEXT("DefaultSceneRoot exists"), DefaultSceneRoot);
		if (Sensor)
		{
			TestEqual(TEXT("Sensor class is SphereComponent"), Sensor->ComponentClass.Get(), USphereComponent::StaticClass());
			const USphereComponent* SensorTemplate = Cast<USphereComponent>(Sensor->ComponentTemplate);
			TestNotNull(TEXT("Sensor has SphereComponent template"), SensorTemplate);
			if (SensorTemplate)
			{
				TestEqual(TEXT("SphereRadius property was applied"), SensorTemplate->GetUnscaledSphereRadius(), 500.0f);
			}
			TestEqual(TEXT("Sensor attaches to DefaultSceneRoot"), Blueprint->SimpleConstructionScript->FindParentNode(Sensor), DefaultSceneRoot);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintNativeInheritedComponentsDoNotDeleteOwnedSCSTest,
	"AssetFactory.AssetDocument.UBlueprint.NativeInheritedComponentsDoNotDeleteOwnedSCS",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintNativeInheritedComponentsDoNotDeleteOwnedSCSTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_ComponentScopes_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentService Service;

	FAssetDocumentApplyRequest CreateRequest;
	CreateRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{});
	LoadBlueprintDocumentBody(CreateRequest.Document)->SetArrayField(
		TEXT("Components"),
		MakeComponentArray({MakeOwnedSphereComponent(TEXT("Sensor"), 500.0)}));
	CreateRequest.bSaveAsset = false;

	const FAssetDocumentResult CreateResult = Service.Apply(CreateRequest);
	TestTrue(TEXT("Initial owned component apply succeeds"), CreateResult.IsSuccess());

	FAssetDocumentApplyRequest ReferenceOnlyRequest;
	ReferenceOnlyRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{});
	LoadBlueprintDocumentBody(ReferenceOnlyRequest.Document)->SetArrayField(
		TEXT("Components"),
		MakeComponentArray({
			MakeReferencedComponent(TEXT("Native"), TEXT("NativeRoot"), TEXT("/Script/Engine.Actor")),
			MakeReferencedComponent(TEXT("Inherited"), TEXT("InheritedMesh"), TEXT("/Script/Engine.Pawn")),
		}));
	ReferenceOnlyRequest.bSaveAsset = false;

	const FAssetDocumentResult ReferenceOnlyResult = Service.Apply(ReferenceOnlyRequest);
	TestTrue(TEXT("Native/Inherited-only components apply succeeds"), ReferenceOnlyResult.IsSuccess());

	UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
	TestNotNull(TEXT("Blueprint exists"), Blueprint);
	if (Blueprint)
	{
		TestNotNull(TEXT("Existing owned Sensor is preserved"), FindSCSNodeByVariableName(Blueprint, TEXT("Sensor")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintOwnedSCSComponentsStructuralPreflightTest,
	"AssetFactory.AssetDocument.UBlueprint.OwnedSCSComponentsStructuralPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintOwnedSCSComponentsStructuralPreflightTest::RunTest(const FString&)
{
	FAssetDocumentService Service;

	{
		const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_ComponentBadAttach_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		FAssetDocumentApplyRequest CreateRequest;
		CreateRequest.Document = MakeUBlueprintDocument(
			Target,
			TEXT("/Script/Engine.Actor"),
			TArray<TSharedPtr<FJsonValue>>{},
			TArray<TSharedPtr<FJsonValue>>{});
		LoadBlueprintDocumentBody(CreateRequest.Document)->SetArrayField(
			TEXT("Components"),
			MakeComponentArray({MakeOwnedSphereComponent(TEXT("Sensor"), 500.0)}));
		CreateRequest.bSaveAsset = false;
		TestTrue(TEXT("Initial Sensor apply succeeds"), Service.Apply(CreateRequest).IsSuccess());

		TSharedPtr<FJsonObject> BadChild = MakeOwnedSceneComponent(TEXT("BadChild"));
		BadChild->SetObjectField(TEXT("AttachTo"), MakeComponentKey(TEXT("MissingParent")));

		FAssetDocumentApplyRequest BadRequest;
		BadRequest.Document = MakeUBlueprintDocument(
			Target,
			TEXT("/Script/Engine.Actor"),
			TArray<TSharedPtr<FJsonValue>>{},
			TArray<TSharedPtr<FJsonValue>>{});
		LoadBlueprintDocumentBody(BadRequest.Document)->SetArrayField(TEXT("Components"), MakeComponentArray({BadChild}));
		BadRequest.bSaveAsset = false;

		const FAssetDocumentResult BadResult = Service.Apply(BadRequest);
		TestFalse(TEXT("Missing AttachTo parent rejects apply"), BadResult.IsSuccess());

		UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
		TestNotNull(TEXT("Blueprint remains loadable after bad apply"), Blueprint);
		if (Blueprint)
		{
			TestNotNull(TEXT("Existing Sensor survives failed apply"), FindSCSNodeByVariableName(Blueprint, TEXT("Sensor")));
			TestNull(TEXT("BadChild is not left behind after failed apply"), FindSCSNodeByVariableName(Blueprint, TEXT("BadChild")));
		}
	}

	{
		const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_ComponentStaleAttach_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		FAssetDocumentApplyRequest CreateRequest;
		CreateRequest.Document = MakeUBlueprintDocument(
			Target,
			TEXT("/Script/Engine.Actor"),
			TArray<TSharedPtr<FJsonValue>>{},
			TArray<TSharedPtr<FJsonValue>>{});
		LoadBlueprintDocumentBody(CreateRequest.Document)->SetArrayField(
			TEXT("Components"),
			MakeComponentArray({MakeOwnedSphereComponent(TEXT("Sensor"), 500.0)}));
		CreateRequest.bSaveAsset = false;
		TestTrue(TEXT("Initial stale-parent Sensor apply succeeds"), Service.Apply(CreateRequest).IsSuccess());

		TSharedPtr<FJsonObject> BadChild = MakeOwnedSceneComponent(TEXT("BadChild"));
		BadChild->SetObjectField(TEXT("AttachTo"), MakeComponentKey(TEXT("Sensor")));

		FAssetDocumentApplyRequest BadRequest;
		BadRequest.Document = MakeUBlueprintDocument(
			Target,
			TEXT("/Script/Engine.Actor"),
			TArray<TSharedPtr<FJsonValue>>{},
			TArray<TSharedPtr<FJsonValue>>{});
		LoadBlueprintDocumentBody(BadRequest.Document)->SetArrayField(TEXT("Components"), MakeComponentArray({BadChild}));
		BadRequest.bSaveAsset = false;

		const FAssetDocumentResult BadResult = Service.Apply(BadRequest);
		TestFalse(TEXT("AttachTo removed owned parent rejects apply"), BadResult.IsSuccess());

		UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
		TestNotNull(TEXT("Blueprint remains loadable after stale parent apply"), Blueprint);
		if (Blueprint)
		{
			TestNotNull(TEXT("Existing Sensor survives stale parent apply"), FindSCSNodeByVariableName(Blueprint, TEXT("Sensor")));
			TestNull(TEXT("BadChild is not left behind after stale parent apply"), FindSCSNodeByVariableName(Blueprint, TEXT("BadChild")));
		}
	}

	{
		const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_ComponentCycle_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		TSharedPtr<FJsonObject> A = MakeOwnedSceneComponent(TEXT("A"));
		TSharedPtr<FJsonObject> B = MakeOwnedSceneComponent(TEXT("B"));
		A->SetObjectField(TEXT("AttachTo"), MakeComponentKey(TEXT("B")));
		B->SetObjectField(TEXT("AttachTo"), MakeComponentKey(TEXT("A")));

		FAssetDocumentApplyRequest Request;
		Request.Document = MakeUBlueprintDocument(Target, TEXT("/Script/Engine.Actor"), TArray<TSharedPtr<FJsonValue>>{}, TArray<TSharedPtr<FJsonValue>>{});
		LoadBlueprintDocumentBody(Request.Document)->SetArrayField(TEXT("Components"), MakeComponentArray({A, B}));
		Request.bSaveAsset = false;

		const FAssetDocumentResult Result = Service.Apply(Request);
		TestFalse(TEXT("Attach cycle rejects apply"), Result.IsSuccess());
	}

	{
		const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_ComponentProtectedName_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		FAssetDocumentApplyRequest Request;
		Request.Document = MakeUBlueprintDocument(Target, TEXT("/Script/Engine.Actor"), TArray<TSharedPtr<FJsonValue>>{}, TArray<TSharedPtr<FJsonValue>>{});
		LoadBlueprintDocumentBody(Request.Document)->SetArrayField(
			TEXT("Components"),
			MakeComponentArray({MakeOwnedSceneComponent(*USceneComponent::GetDefaultSceneRootVariableName().ToString())}));
		Request.bSaveAsset = false;

		const FAssetDocumentResult Result = Service.Apply(Request);
		TestFalse(TEXT("DefaultSceneRoot protected component name rejects apply"), Result.IsSuccess());
	}

	{
		const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_ComponentRootRules_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		TSharedPtr<FJsonObject> A = MakeOwnedSceneComponent(TEXT("A"));
		TSharedPtr<FJsonObject> B = MakeOwnedSceneComponent(TEXT("B"));
		A->SetBoolField(TEXT("Root"), true);
		B->SetBoolField(TEXT("Root"), true);

		FAssetDocumentApplyRequest MultiRootRequest;
		MultiRootRequest.Document = MakeUBlueprintDocument(Target, TEXT("/Script/Engine.Actor"), TArray<TSharedPtr<FJsonValue>>{}, TArray<TSharedPtr<FJsonValue>>{});
		LoadBlueprintDocumentBody(MultiRootRequest.Document)->SetArrayField(TEXT("Components"), MakeComponentArray({A, B}));
		MultiRootRequest.bSaveAsset = false;
		TestFalse(TEXT("Multiple Root components reject apply"), Service.Apply(MultiRootRequest).IsSuccess());

		TSharedPtr<FJsonObject> RootWithAttach = MakeOwnedSceneComponent(TEXT("RootWithAttach"));
		RootWithAttach->SetBoolField(TEXT("Root"), true);

		FAssetDocumentApplyRequest RootAttachRequest;
		RootAttachRequest.Document = MakeUBlueprintDocument(Target, TEXT("/Script/Engine.Actor"), TArray<TSharedPtr<FJsonValue>>{}, TArray<TSharedPtr<FJsonValue>>{});
		LoadBlueprintDocumentBody(RootAttachRequest.Document)->SetArrayField(TEXT("Components"), MakeComponentArray({RootWithAttach}));
		RootAttachRequest.bSaveAsset = false;
		TestFalse(TEXT("Root with AttachTo rejects apply"), Service.Apply(RootAttachRequest).IsSuccess());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintOwnedSCSComponentsValidationAndDiffTest,
	"AssetFactory.AssetDocument.UBlueprint.OwnedSCSComponentsValidationAndDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintOwnedSCSComponentsValidationAndDiffTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext ValidationContext;
	ValidationContext.AssetClass = UBlueprint::StaticClass();

	TSharedPtr<FJsonObject> InvalidDocument = MakeUBlueprintDocument(
		TEXT("/Game/AssetDocumentTests/BP_AD_InvalidComponentParent"),
		TEXT("/Script/Engine.GameInstance"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{});
	LoadBlueprintDocumentBody(InvalidDocument)->SetArrayField(
		TEXT("Components"),
		MakeComponentArray({MakeOwnedSphereComponent(TEXT("Sensor"), 500.0)}));

	const FAssetDocumentCapabilityResult InvalidResult = Capability.Validate(
		ValidationContext,
		MakeBodyValue(LoadBlueprintDocumentBody(InvalidDocument).ToSharedRef()));
	TestFalse(TEXT("OwnedSCS component with non-Actor parent fails validation"), InvalidResult.bSuccess);
	const bool bHasExpectedInvalidParentDiagnostic = ResultHasDiagnostic(InvalidResult, TEXT("/Body/Components"), TEXT("OwnedSCSRequiresActorParent"));
	if (!bHasExpectedInvalidParentDiagnostic)
	{
		for (const FAssetDocumentDiagnostic& Diagnostic : InvalidResult.Diagnostics)
		{
			AddError(FString::Printf(TEXT("Unexpected non-Actor component diagnostic: Path=%s Code=%s Message=%s"), *Diagnostic.Path, *Diagnostic.Code, *Diagnostic.Message));
		}
	}
	TestTrue(TEXT("Non-Actor component failure is reported at Components"), bHasExpectedInvalidParentDiagnostic);

	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_ComponentDiff_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	TSharedPtr<FJsonObject> Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{});
	LoadBlueprintDocumentBody(Document)->SetArrayField(
		TEXT("Components"),
		MakeComponentArray({MakeOwnedSphereComponent(TEXT("Sensor"), 500.0)}));

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(Request);
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Component diff fixture apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Component diff fixture apply succeeds"), ApplyResult.IsSuccess());

	UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
	TestNotNull(TEXT("Component diff Blueprint exists"), Blueprint);
	if (Blueprint)
	{
		FAssetDocumentCapabilityContext Context;
		Context.Asset = Blueprint;
		Context.AssetClass = UBlueprint::StaticClass();

		TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
		const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
		TestTrue(TEXT("Component extract succeeds"), ExtractResult.bSuccess);

		const TArray<TSharedPtr<FJsonValue>>* ExtractedComponents = nullptr;
		TestTrue(TEXT("Extract includes Components"), ExtractedBody->TryGetArrayField(TEXT("Components"), ExtractedComponents));
		TSharedPtr<FJsonObject> Sensor = ExtractedComponents ? FindJsonObjectByStringField(*ExtractedComponents, TEXT("Scope"), TEXT("OwnedSCS")) : nullptr;
		TestTrue(TEXT("Extract includes an OwnedSCS component"), Sensor.IsValid());
		if (Sensor.IsValid())
		{
			const TSharedPtr<FJsonObject>* Key = nullptr;
			TestTrue(TEXT("Extracted component includes Key"), Sensor->TryGetObjectField(TEXT("Key"), Key));
			if (Key && Key->IsValid())
			{
				TestEqual(TEXT("Extracted component Key.Name is Sensor"), (*Key)->GetStringField(TEXT("Name")), FString(TEXT("Sensor")));
				TestEqual(TEXT("Extracted component Key.OwnerClass is Self"), (*Key)->GetStringField(TEXT("OwnerClass")), FString(TEXT("Self")));
			}
			TestEqual(TEXT("Extracted component class is SphereComponent"), Sensor->GetStringField(TEXT("Class")), FString(TEXT("/Script/Engine.SphereComponent")));
			const TSharedPtr<FJsonObject>* AttachTo = nullptr;
			TestTrue(TEXT("Extracted component includes AttachTo"), Sensor->TryGetObjectField(TEXT("AttachTo"), AttachTo));
			if (AttachTo && AttachTo->IsValid())
			{
				TestEqual(TEXT("Extracted AttachTo.Name is DefaultSceneRoot"), (*AttachTo)->GetStringField(TEXT("Name")), FString(TEXT("DefaultSceneRoot")));
				TestEqual(TEXT("Extracted AttachTo.OwnerClass is Self"), (*AttachTo)->GetStringField(TEXT("OwnerClass")), FString(TEXT("Self")));
			}
			const TSharedPtr<FJsonObject>* Properties = nullptr;
			TestTrue(TEXT("Extracted component includes Properties"), Sensor->TryGetObjectField(TEXT("Properties"), Properties));
			if (Properties && Properties->IsValid())
			{
				TestEqual(TEXT("Extracted SphereRadius is default diff"), (*Properties)->GetNumberField(TEXT("SphereRadius")), 500.0);
			}
		}

		TArray<TSharedPtr<FJsonValue>> DiffEntries;
		const FAssetDocumentCapabilityResult DiffResult = Capability.Diff(Context, MakeBodyValue(LoadBlueprintDocumentBody(Document).ToSharedRef()), DiffEntries);
		TestTrue(TEXT("Component diff succeeds"), DiffResult.bSuccess);
		TSharedPtr<FJsonObject> SensorDiff = FindDiffEntryByPath(DiffEntries, TEXT("/Body/Components/Self:Sensor"));
		TestTrue(TEXT("Component diff includes Sensor"), SensorDiff.IsValid());
		if (SensorDiff.IsValid())
		{
			TestEqual(TEXT("Sensor component diff is unchanged"), SensorDiff->GetStringField(TEXT("status")), FString(TEXT("unchanged")));
		}
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
	FAssetDocumentUBlueprintInheritedSCSOverrideTest,
	"AssetFactory.AssetDocument.UBlueprint.InheritedSCSOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintInheritedSCSOverrideTest::RunTest(const FString&)
{
	const FString ParentTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_InheritedParent_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ChildTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_InheritedChild_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentService Service;

	FAssetDocumentApplyRequest ParentRequest;
	ParentRequest.Document = MakeUBlueprintDocument(
		ParentTarget,
		TEXT("/Script/Engine.Actor"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{},
		MakeComponentArray({MakeOwnedSphereComponent(TEXT("ParentSensor"), 150.0)}));
	ParentRequest.bSaveAsset = false;
	const FAssetDocumentResult ParentResult = Service.Apply(ParentRequest);
	if (!ParentResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Parent apply failed: %s"), *ParentResult.Message));
	}
	TestTrue(TEXT("Parent Blueprint with owned component applies"), ParentResult.IsSuccess());

	UBlueprint* ParentBlueprint = LoadBlueprintForTarget(ParentTarget);
	TestNotNull(TEXT("Parent Blueprint exists"), ParentBlueprint);
	if (!ParentBlueprint || !ParentBlueprint->GeneratedClass)
	{
		return true;
	}

	USCS_Node* ParentSensorNode = FindSCSNodeByVariableName(ParentBlueprint, TEXT("ParentSensor"));
	TestNotNull(TEXT("Parent SCS component exists"), ParentSensorNode);
	if (!ParentSensorNode)
	{
		return true;
	}

	const FString ParentGeneratedClassPath = ParentBlueprint->GeneratedClass->GetPathName();
	TSharedPtr<FJsonObject> InheritedComponent = MakeReferencedComponent(TEXT("Inherited"), TEXT("ParentSensor"), *ParentGeneratedClassPath);
	InheritedComponent->SetStringField(TEXT("Class"), TEXT("/Script/Engine.SphereComponent"));
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("SphereRadius"), 900.0);
	InheritedComponent->SetObjectField(TEXT("Properties"), Properties);

	FAssetDocumentApplyRequest ChildOverrideRequest;
	ChildOverrideRequest.Document = MakeUBlueprintDocument(
		ChildTarget,
		ParentGeneratedClassPath,
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{},
		MakeComponentArray({InheritedComponent}));
	ChildOverrideRequest.bSaveAsset = false;
	const FAssetDocumentResult OverrideResult = Service.Apply(ChildOverrideRequest);
	if (!OverrideResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Inherited override apply failed: %s"), *OverrideResult.Message));
	}
	TestTrue(TEXT("Inherited SCS component override applies"), OverrideResult.IsSuccess());

	UBlueprint* ChildBlueprint = LoadBlueprintForTarget(ChildTarget);
	TestNotNull(TEXT("Child Blueprint exists"), ChildBlueprint);
	if (ChildBlueprint)
	{
		const FComponentKey ParentSensorKey(ParentSensorNode);
		UInheritableComponentHandler* Handler = ChildBlueprint->GetInheritableComponentHandler(false);
		TestNotNull(TEXT("Child has inheritable component handler after override"), Handler);
		UActorComponent* OverrideTemplate = Handler ? Handler->GetOverridenComponentTemplate(ParentSensorKey) : nullptr;
		USphereComponent* SphereOverride = Cast<USphereComponent>(OverrideTemplate);
		TestNotNull(TEXT("Inherited override template is a SphereComponent"), SphereOverride);
		if (SphereOverride)
		{
			TestEqual(TEXT("Inherited SphereRadius override is applied"), SphereOverride->GetUnscaledSphereRadius(), 900.0f);
		}
	}

	if (ChildBlueprint)
	{
		const FUBlueprintAssetDocumentCapability Capability;
		FAssetDocumentCapabilityContext Context;
		Context.Asset = ChildBlueprint;
		Context.AssetClass = UBlueprint::StaticClass();

		TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
		const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
		TestTrue(TEXT("Inherited component extract succeeds"), ExtractResult.bSuccess);
		const TArray<TSharedPtr<FJsonValue>>* ExtractedComponents = nullptr;
		TestTrue(TEXT("Extract includes inherited Components"), ExtractedBody->TryGetArrayField(TEXT("Components"), ExtractedComponents));
		TSharedPtr<FJsonObject> ExtractedInherited = ExtractedComponents
			? FindComponentByScopeAndKey(*ExtractedComponents, TEXT("Inherited"), TEXT("ParentSensor"), ParentGeneratedClassPath)
			: nullptr;
		TestTrue(TEXT("Extract includes inherited override component"), ExtractedInherited.IsValid());

		TArray<TSharedPtr<FJsonValue>> DiffEntries;
		const FAssetDocumentCapabilityResult DiffResult =
			Capability.Diff(Context, MakeBodyValue(LoadBlueprintDocumentBody(ChildOverrideRequest.Document).ToSharedRef()), DiffEntries);
		TestTrue(TEXT("Inherited component diff succeeds"), DiffResult.bSuccess);
		TSharedPtr<FJsonObject> InheritedDiff = FindDiffEntryByPath(DiffEntries, FString::Printf(TEXT("/Body/Components/%s:ParentSensor"), *ParentGeneratedClassPath));
		TestNotNull(TEXT("Inherited component diff includes override path"), InheritedDiff.Get());
		TestTrue(TEXT("Inherited component roundtrip diff is unchanged"), IsUnchangedDiffEntry(InheritedDiff));
	}

	FAssetDocumentApplyRequest ChildResetRequest;
	ChildResetRequest.Document = MakeUBlueprintDocument(
		ChildTarget,
		ParentGeneratedClassPath,
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{});
	ChildResetRequest.bSaveAsset = false;
	const FAssetDocumentResult ResetResult = Service.Apply(ChildResetRequest);
	if (!ResetResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Inherited override reset apply failed: %s"), *ResetResult.Message));
	}
	TestTrue(TEXT("Omitting inherited component clears override"), ResetResult.IsSuccess());

	ChildBlueprint = LoadBlueprintForTarget(ChildTarget);
	if (ChildBlueprint)
	{
		const FComponentKey ParentSensorKey(ParentSensorNode);
		UInheritableComponentHandler* Handler = ChildBlueprint->GetInheritableComponentHandler(false);
		UActorComponent* OverrideTemplate = Handler ? Handler->GetOverridenComponentTemplate(ParentSensorKey) : nullptr;
		TestNull(TEXT("Inherited override template is removed when omitted"), OverrideTemplate);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintNativeComponentOverrideTest,
	"AssetFactory.AssetDocument.UBlueprint.NativeComponentOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintNativeComponentOverrideTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_NativeComponent_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentService Service;

	TSharedPtr<FJsonObject> NativeMovement = MakeReferencedComponent(TEXT("Native"), TEXT("CharacterMovement"), TEXT("/Script/Engine.Character"));
	NativeMovement->SetStringField(TEXT("Class"), TEXT("/Script/Engine.CharacterMovementComponent"));
	TSharedPtr<FJsonObject> MovementProperties = MakeShared<FJsonObject>();
	MovementProperties->SetNumberField(TEXT("MaxWalkSpeed"), 700.0);
	NativeMovement->SetObjectField(TEXT("Properties"), MovementProperties);

	FAssetDocumentApplyRequest OverrideRequest;
	OverrideRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Character"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{},
		MakeComponentArray({NativeMovement}));
	OverrideRequest.bSaveAsset = false;
	const FAssetDocumentResult OverrideResult = Service.Apply(OverrideRequest);
	if (!OverrideResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Native component override apply failed: %s"), *OverrideResult.Message));
	}
	TestTrue(TEXT("Native CharacterMovement override applies"), OverrideResult.IsSuccess());

	UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
	TestNotNull(TEXT("Native component Blueprint exists"), Blueprint);
	if (Blueprint && Blueprint->GeneratedClass)
	{
		ACharacter* CDO = Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
		TestNotNull(TEXT("Generated CDO is Character"), CDO);
		UCharacterMovementComponent* Movement = CDO ? CDO->GetCharacterMovement() : nullptr;
		TestNotNull(TEXT("Generated CDO has CharacterMovement"), Movement);
		if (Movement)
		{
			TestEqual(TEXT("Native MaxWalkSpeed override is applied"), Movement->MaxWalkSpeed, 700.0f);
		}
	}

	if (Blueprint)
	{
		const FUBlueprintAssetDocumentCapability Capability;
		FAssetDocumentCapabilityContext Context;
		Context.Asset = Blueprint;
		Context.AssetClass = UBlueprint::StaticClass();

		TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
		const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
		TestTrue(TEXT("Native component extract succeeds"), ExtractResult.bSuccess);
		const TArray<TSharedPtr<FJsonValue>>* ExtractedComponents = nullptr;
		TestTrue(TEXT("Extract includes native Components"), ExtractedBody->TryGetArrayField(TEXT("Components"), ExtractedComponents));
		TSharedPtr<FJsonObject> ExtractedNative = ExtractedComponents
			? FindComponentByScopeAndKey(*ExtractedComponents, TEXT("Native"), TEXT("CharacterMovement"), TEXT("/Script/Engine.Character"))
			: nullptr;
		TestTrue(TEXT("Extract includes native CharacterMovement override"), ExtractedNative.IsValid());

		TArray<TSharedPtr<FJsonValue>> DiffEntries;
		const FAssetDocumentCapabilityResult DiffResult =
			Capability.Diff(Context, MakeBodyValue(LoadBlueprintDocumentBody(OverrideRequest.Document).ToSharedRef()), DiffEntries);
		TestTrue(TEXT("Native component diff succeeds"), DiffResult.bSuccess);
		TSharedPtr<FJsonObject> NativeDiff = FindDiffEntryByPath(DiffEntries, TEXT("/Body/Components//Script/Engine.Character:CharacterMovement"));
		TestNotNull(TEXT("Native component diff includes alias path"), NativeDiff.Get());
		TestTrue(TEXT("Native component roundtrip diff is unchanged"), IsUnchangedDiffEntry(NativeDiff));

		TSharedPtr<FJsonObject> NativeMovementByObjectName = MakeReferencedComponent(TEXT("Native"), TEXT("CharMoveComp"), TEXT("/Script/Engine.Character"));
		NativeMovementByObjectName->SetStringField(TEXT("Class"), TEXT("/Script/Engine.CharacterMovementComponent"));
		NativeMovementByObjectName->SetObjectField(TEXT("Properties"), MovementProperties);
		TSharedPtr<FJsonObject> ObjectNameDocument = MakeUBlueprintDocument(
			Target,
			TEXT("/Script/Engine.Character"),
			TArray<TSharedPtr<FJsonValue>>{},
			TArray<TSharedPtr<FJsonValue>>{},
			MakeComponentArray({NativeMovementByObjectName}));
		TSharedPtr<FJsonObject> ObjectNameBody = LoadBlueprintDocumentBody(ObjectNameDocument);

		TArray<TSharedPtr<FJsonValue>> ObjectNameDiffEntries;
		const FAssetDocumentCapabilityResult ObjectNameDiffResult = Capability.Diff(Context, MakeBodyValue(ObjectNameBody.ToSharedRef()), ObjectNameDiffEntries);
		TestTrue(TEXT("Native object-name component diff succeeds"), ObjectNameDiffResult.bSuccess);
		TSharedPtr<FJsonObject> ObjectNameNativeDiff =
			FindDiffEntryByPath(ObjectNameDiffEntries, TEXT("/Body/Components//Script/Engine.Character:CharacterMovement"));
		TestNotNull(TEXT("Native object-name diff canonicalizes to alias path"), ObjectNameNativeDiff.Get());
		TestTrue(TEXT("Native object-name desired diff is unchanged"), IsUnchangedDiffEntry(ObjectNameNativeDiff));
	}

	const ACharacter* ParentCDO = GetDefault<ACharacter>();
	const float ParentMaxWalkSpeed = ParentCDO && ParentCDO->GetCharacterMovement()
		? ParentCDO->GetCharacterMovement()->MaxWalkSpeed
		: 600.0f;

	FAssetDocumentApplyRequest ResetRequest;
	ResetRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Character"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{});
	ResetRequest.bSaveAsset = false;
	const FAssetDocumentResult ResetResult = Service.Apply(ResetRequest);
	if (!ResetResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Native component reset apply failed: %s"), *ResetResult.Message));
	}
	TestTrue(TEXT("Omitting native component resets override"), ResetResult.IsSuccess());

	Blueprint = LoadBlueprintForTarget(Target);
	if (Blueprint && Blueprint->GeneratedClass)
	{
		ACharacter* CDO = Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
		UCharacterMovementComponent* Movement = CDO ? CDO->GetCharacterMovement() : nullptr;
		TestNotNull(TEXT("Generated CDO still has CharacterMovement after reset"), Movement);
		if (Movement)
		{
			TestEqual(TEXT("Native MaxWalkSpeed resets to parent CDO baseline"), Movement->MaxWalkSpeed, ParentMaxWalkSpeed);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintInheritedNativeAttachRootUnsupportedTest,
	"AssetFactory.AssetDocument.UBlueprint.InheritedNativeAttachRootUnsupported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintInheritedNativeAttachRootUnsupportedTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedPtr<FJsonObject> NativeWithAttach = MakeReferencedComponent(TEXT("Native"), TEXT("CharacterMovement"), TEXT("/Script/Engine.Character"));
	NativeWithAttach->SetStringField(TEXT("Class"), TEXT("/Script/Engine.CharacterMovementComponent"));
	NativeWithAttach->SetObjectField(TEXT("AttachTo"), MakeComponentKey(TEXT("CapsuleComponent"), TEXT("/Script/Engine.Character")));

	TSharedRef<FJsonObject> NativeBody = MakeShared<FJsonObject>();
	NativeBody->SetObjectField(TEXT("ParentClass"), MakeClassRef(TEXT("/Script/Engine.Character")));
	NativeBody->SetArrayField(TEXT("Components"), MakeComponentArray({NativeWithAttach}));
	const FAssetDocumentCapabilityResult NativeResult = Capability.Validate(Context, MakeBodyValue(NativeBody));
	TestFalse(TEXT("Native component AttachTo fails validation"), NativeResult.bSuccess);
	TestTrue(TEXT("Native AttachTo uses unsupported attach/root diagnostic"), ResultHasDiagnosticCode(NativeResult, TEXT("UnsupportedInheritedComponentAttachRoot")));

	TSharedPtr<FJsonObject> InheritedWithRoot = MakeReferencedComponent(TEXT("Inherited"), TEXT("ParentSensor"), TEXT("/Game/AssetDocumentTests/BP_UnresolvedParent.BP_UnresolvedParent_C"));
	InheritedWithRoot->SetStringField(TEXT("Class"), TEXT("/Script/Engine.SphereComponent"));
	InheritedWithRoot->SetBoolField(TEXT("Root"), true);

	TSharedRef<FJsonObject> InheritedBody = MakeShared<FJsonObject>();
	InheritedBody->SetObjectField(TEXT("ParentClass"), MakeActorParentClassRef());
	InheritedBody->SetArrayField(TEXT("Components"), MakeComponentArray({InheritedWithRoot}));
	const FAssetDocumentCapabilityResult InheritedResult = Capability.Validate(Context, MakeBodyValue(InheritedBody));
	TestFalse(TEXT("Inherited component Root fails validation"), InheritedResult.bSuccess);
	TestTrue(TEXT("Inherited Root uses unsupported attach/root diagnostic"), ResultHasDiagnosticCode(InheritedResult, TEXT("UnsupportedInheritedComponentAttachRoot")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintClassDefaultsAuthoritativeTest,
	"AssetFactory.AssetDocument.UBlueprint.ClassDefaultsAuthoritative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintClassDefaultsAuthoritativeTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_ClassDefaults_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentService Service;

	TSharedPtr<FJsonObject> ClassDefaults = MakeShared<FJsonObject>();
	ClassDefaults->SetNumberField(TEXT("InitialLifeSpan"), 12.5);

	FAssetDocumentApplyRequest ApplyRequest;
	ApplyRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{},
		ClassDefaults);
	ApplyRequest.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(ApplyRequest);
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("ClassDefaults apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("ClassDefaults apply succeeds"), ApplyResult.IsSuccess());

	UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
	TestNotNull(TEXT("ClassDefaults Blueprint exists"), Blueprint);
	if (Blueprint && Blueprint->GeneratedClass)
	{
		AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
		TestNotNull(TEXT("Generated CDO is Actor"), CDO);
		if (CDO)
		{
			TestEqual(TEXT("InitialLifeSpan class default is applied"), CDO->InitialLifeSpan, 12.5f);
		}
	}

	const float ParentInitialLifeSpan = GetDefault<AActor>()->InitialLifeSpan;
	FAssetDocumentApplyRequest ResetRequest;
	ResetRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Actor"),
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{},
		TArray<TSharedPtr<FJsonValue>>{},
		MakeShared<FJsonObject>());
	ResetRequest.bSaveAsset = false;
	const FAssetDocumentResult ResetResult = Service.Apply(ResetRequest);
	if (!ResetResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("ClassDefaults reset failed: %s"), *ResetResult.Message));
	}
	TestTrue(TEXT("Omitted ClassDefaults property resets to parent CDO"), ResetResult.IsSuccess());

	Blueprint = LoadBlueprintForTarget(Target);
	if (Blueprint && Blueprint->GeneratedClass)
	{
		AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
		TestNotNull(TEXT("Generated CDO still exists after reset"), CDO);
		if (CDO)
		{
			TestEqual(TEXT("InitialLifeSpan resets to parent CDO baseline"), CDO->InitialLifeSpan, ParentInitialLifeSpan);
		}
	}

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

	TSharedPtr<FJsonObject> InvalidDefaultVariable = MakeFloatVariable(TEXT("RollbackFloat"), TEXT("not-a-float"));
	FAssetDocumentApplyRequest BadRequest;
	BadRequest.Document = MakeUBlueprintDocument(
		Target,
		TEXT("/Script/Engine.Pawn"),
		MakeVariableArray({InvalidDefaultVariable}),
		TArray<TSharedPtr<FJsonValue>>{});
	BadRequest.bSaveAsset = false;
	const FAssetDocumentResult BadResult = Service.Apply(BadRequest);
	TestFalse(TEXT("Invalid default apply fails after mutation"), BadResult.IsSuccess());
	TestTrue(TEXT("Failure comes from default sync after mutation"), ResultHasDiagnostic(BadResult, TEXT("InvalidVariableDefaultValue")));

	UBlueprint* Blueprint = LoadBlueprintForTarget(Target);
	TestNotNull(TEXT("Blueprint still exists after rollback failure"), Blueprint);
	if (Blueprint)
	{
		TestEqual(TEXT("Parent rolled back to Actor"), Blueprint->ParentClass.Get(), AActor::StaticClass());
		TestTrue(TEXT("Health remains after rollback failure"), HasBlueprintVariable(Blueprint, TEXT("Health")));
		TestFalse(TEXT("RollbackFloat was not added after rollback failure"), HasBlueprintVariable(Blueprint, TEXT("RollbackFloat")));
		TestEqual(TEXT("Implemented interfaces remain empty after rollback failure"), Blueprint->ImplementedInterfaces.Num(), 0);
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
