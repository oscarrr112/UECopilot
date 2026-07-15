// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AssetDocumentApplyPreflightTests
{
struct FProfileCase
{
	const TCHAR* Label;
	const TCHAR* ClassPath;
	const TCHAR* InvalidBodyKey;
};

const FProfileCase ProfileCases[] = {
	{TEXT("BehaviorTree"), TEXT("/Script/AIModule.BehaviorTree"), TEXT("Tree")},
	{TEXT("BlackboardData"), TEXT("/Script/AIModule.BlackboardData"), TEXT("Keys")},
};

struct FBlueprintCreationCase
{
	const TCHAR* Label;
	const TCHAR* ClassPath;
	const TCHAR* MissingBodyMessage;
	const TCHAR* MissingParentClassMessage;
};

const FBlueprintCreationCase BlueprintCreationCases[] = {
	{
		TEXT("UBlueprint"),
		TEXT("/Script/Engine.Blueprint"),
		TEXT("UBlueprint creation requires Body.ParentClass"),
		TEXT("UBlueprint creation requires Body.ParentClass"),
	},
	{
		TEXT("WidgetBlueprint"),
		TEXT("/Script/UMGEditor.WidgetBlueprint"),
		TEXT("WidgetBlueprint creation requires Body.ParentClass"),
		TEXT("WidgetBlueprint creation requires Body.ParentClass"),
	},
	{
		TEXT("AnimBlueprint"),
		TEXT("/Script/Engine.AnimBlueprint"),
		TEXT("AnimBlueprint creation requires Body"),
		TEXT("AnimBlueprint creation requires Body.ParentClass"),
	},
};

FString MakeUniqueTarget(const TCHAR* AssetStem)
{
	return FString::Printf(
		TEXT("/Game/AssetDocumentTests/Task7b1/%s_%s"),
		AssetStem,
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
}

FString ToObjectPath(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

FString ToPackageFileName(const FString& Target)
{
	return FPackageName::LongPackageNameToFilename(Target, FPackageName::GetAssetPackageExtension());
}

TSharedRef<FJsonObject> MakeAssetRef(const FString& ObjectPath)
{
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), ObjectPath);
	return AssetRef;
}

void CleanupAsset(const FString& Target)
{
	const FString ObjectPath = ToObjectPath(Target);
	const FString PackageFileName = ToPackageFileName(Target);
	UObject* ExistingAsset = FindObject<UObject>(nullptr, *ObjectPath);
	if (!ExistingAsset && IFileManager::Get().FileExists(*PackageFileName))
	{
		ExistingAsset = LoadObject<UObject>(nullptr, *ObjectPath);
	}

	if (ExistingAsset)
	{
		TArray<UObject*> ObjectsToDelete{ExistingAsset};
		ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
	}

	IFileManager::Get().Delete(*PackageFileName, false, true);
}

TSharedPtr<FJsonObject> MakeTemplateDocument(
	FAutomationTestBase& Test,
	const FAssetDocumentService& Service,
	const FString& Target,
	const FString& ClassPath,
	const FString& Action)
{
	FAssetDocumentTemplateRequest Request;
	Request.Class = ClassPath;
	Request.Target = Target;
	const FAssetDocumentResult Result = Service.CreateTemplate(Request);
	Test.TestTrue(*FString::Printf(TEXT("CreateTemplate succeeds for %s"), *ClassPath), Result.IsSuccess());
	Test.TestTrue(*FString::Printf(TEXT("CreateTemplate returns a document for %s"), *ClassPath), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return nullptr;
	}

	Result.Payload->SetStringField(TEXT("Action"), Action);
	return Result.Payload;
}

bool InvalidateKnownBodyRegion(
	FAutomationTestBase& Test,
	const TSharedPtr<FJsonObject>& Document,
	const FString& BodyKey)
{
	const TSharedPtr<FJsonObject>* Body = nullptr;
	const bool bHasBody = Document.IsValid()
		&& Document->TryGetObjectField(TEXT("Body"), Body)
		&& Body
		&& Body->IsValid();
	Test.TestTrue(*FString::Printf(TEXT("Template contains Body for invalid %s preflight"), *BodyKey), bHasBody);
	if (!bHasBody)
	{
		return false;
	}

	Test.TestTrue(*FString::Printf(TEXT("Template contains registered Body.%s region"), *BodyKey), (*Body)->HasField(BodyKey));
	(*Body)->SetStringField(BodyKey, TEXT("invalid-region-shape"));
	return true;
}

class FScopedAssetAddedCounter
{
public:
	explicit FScopedAssetAddedCounter(const FString& InExpectedObjectPath)
		: AssetRegistry(FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get())
		, ExpectedObjectPath(InExpectedObjectPath)
	{
		Handle = AssetRegistry.OnAssetAdded().AddLambda([this](const FAssetData& AssetData)
		{
			if (AssetData.GetObjectPathString() == ExpectedObjectPath)
			{
				++MatchingAssetAddedCount;
			}
		});
	}

	~FScopedAssetAddedCounter()
	{
		AssetRegistry.OnAssetAdded().Remove(Handle);
	}

	int32 GetMatchingAssetAddedCount() const
	{
		return MatchingAssetAddedCount;
	}

private:
	IAssetRegistry& AssetRegistry;
	FString ExpectedObjectPath;
	FDelegateHandle Handle;
	int32 MatchingAssetAddedCount = 0;
};

bool RegistryContainsObjectPath(const FString& ObjectPath)
{
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	return AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid();
}

struct FSurfaceResults
{
	FAssetDocumentResult Validate;
	FAssetDocumentResult Diff;
	FAssetDocumentResult Apply;
};

FSurfaceResults RunPublicSurfaces(FAssetDocumentService& Service, const TSharedPtr<FJsonObject>& Document)
{
	FSurfaceResults Results;

	FAssetDocumentValidateRequest ValidateRequest;
	ValidateRequest.Document = Document;
	Results.Validate = Service.Validate(ValidateRequest);

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	Results.Diff = Service.Diff(DiffRequest);

	FAssetDocumentApplyRequest ApplyRequest;
	ApplyRequest.Document = Document;
	ApplyRequest.bSaveAsset = false;
	Results.Apply = Service.Apply(ApplyRequest);
	return Results;
}

void ExpectSurfaceFailureMessageParity(
	FAutomationTestBase& Test,
	const FString& CaseLabel,
	const FSurfaceResults& Results,
	const FString& ExpectedMessage)
{
	Test.TestFalse(*FString::Printf(TEXT("%s Validate fails"), *CaseLabel), Results.Validate.IsSuccess());
	Test.TestFalse(*FString::Printf(TEXT("%s Diff fails"), *CaseLabel), Results.Diff.IsSuccess());
	Test.TestFalse(*FString::Printf(TEXT("%s Apply fails"), *CaseLabel), Results.Apply.IsSuccess());
	Test.TestEqual(*FString::Printf(TEXT("%s Validate message is canonical"), *CaseLabel), Results.Validate.Message, ExpectedMessage);
	Test.TestEqual(*FString::Printf(TEXT("%s Diff message matches Validate"), *CaseLabel), Results.Diff.Message, Results.Validate.Message);
	Test.TestEqual(*FString::Printf(TEXT("%s Apply message matches Validate"), *CaseLabel), Results.Apply.Message, Results.Validate.Message);
}

void ExpectNoMaterializedTarget(
	FAutomationTestBase& Test,
	const FString& CaseLabel,
	const FString& Target,
	const FScopedAssetAddedCounter& AssetAddedCounter)
{
	const FString ObjectPath = ToObjectPath(Target);
	const FString PackageFileName = ToPackageFileName(Target);
	Test.TestEqual(
		*FString::Printf(TEXT("%s never announces a real asset"), *CaseLabel),
		AssetAddedCounter.GetMatchingAssetAddedCount(),
		0);
	Test.TestNull(*FString::Printf(TEXT("%s leaves no real package"), *CaseLabel), FindPackage(nullptr, *Target));
	Test.TestNull(*FString::Printf(TEXT("%s leaves no real asset"), *CaseLabel), FindObject<UObject>(nullptr, *ObjectPath));
	Test.TestFalse(*FString::Printf(TEXT("%s leaves no AssetRegistry entry"), *CaseLabel), RegistryContainsObjectPath(ObjectPath));
	Test.TestFalse(
		*FString::Printf(TEXT("%s writes no uasset"), *CaseLabel),
		IFileManager::Get().FileExists(*PackageFileName));
}

int32 CountOwnedObjects(UObject* Asset)
{
	TArray<UObject*> OwnedObjects;
	if (Asset)
	{
		GetObjectsWithOuter(Asset, OwnedObjects, true);
	}
	return OwnedObjects.Num();
}

bool ResultHasBodyDiagnostic(const FAssetDocumentResult& Result)
{
	return Result.Diagnostics.ContainsByPredicate([](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == TEXT("/Body") || Diagnostic.Path.StartsWith(TEXT("/Body/"));
	});
}

void ExpectLifecycleFailure(
	FAutomationTestBase& Test,
	const FString& CaseLabel,
	const FString& Surface,
	const FAssetDocumentResult& Result,
	const FString& ExpectedMessage)
{
	Test.TestFalse(*FString::Printf(TEXT("%s %s fails"), *CaseLabel, *Surface), Result.IsSuccess());
	Test.TestEqual(
		*FString::Printf(TEXT("%s %s reports the lifecycle failure before Body preflight"), *CaseLabel, *Surface),
		Result.Message,
		ExpectedMessage);
	Test.TestFalse(
		*FString::Printf(TEXT("%s %s does not expose a lower-priority Body diagnostic"), *CaseLabel, *Surface),
		ResultHasBodyDiagnostic(Result));
}

void ExpectLifecycleParity(
	FAutomationTestBase& Test,
	const FString& CaseLabel,
	FAssetDocumentService& Service,
	const TSharedPtr<FJsonObject>& Document,
	const FString& ExpectedMessage)
{
	FAssetDocumentValidateRequest ValidateRequest;
	ValidateRequest.Document = Document;
	const FAssetDocumentResult ValidateResult = Service.Validate(ValidateRequest);

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);

	FAssetDocumentApplyRequest ApplyRequest;
	ApplyRequest.Document = Document;
	ApplyRequest.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(ApplyRequest);

	ExpectLifecycleFailure(Test, CaseLabel, TEXT("Validate"), ValidateResult, ExpectedMessage);
	ExpectLifecycleFailure(Test, CaseLabel, TEXT("Diff"), DiffResult, ExpectedMessage);
	ExpectLifecycleFailure(Test, CaseLabel, TEXT("Apply"), ApplyResult, ExpectedMessage);
	Test.TestEqual(
		*FString::Printf(TEXT("%s Diff message matches Validate"), *CaseLabel),
		DiffResult.Message,
		ValidateResult.Message);
	Test.TestEqual(
		*FString::Printf(TEXT("%s Apply message matches Validate"), *CaseLabel),
		ApplyResult.Message,
		ValidateResult.Message);
}

void ExpectUnknownBodyRegionFailure(
	FAutomationTestBase& Test,
	const FString& CaseLabel,
	const FString& Surface,
	const FAssetDocumentResult& Result)
{
	Test.TestFalse(*FString::Printf(TEXT("%s %s rejects an unknown Body region"), *CaseLabel, *Surface), Result.IsSuccess());
	Test.TestTrue(
		*FString::Printf(TEXT("%s %s reports the canonical unknown-region diagnostic"), *CaseLabel, *Surface),
		Result.Diagnostics.ContainsByPredicate([](const FAssetDocumentDiagnostic& Diagnostic)
		{
			return Diagnostic.Path == TEXT("/Body/UnknownRegion")
				&& Diagnostic.Code == TEXT("UnknownBodyKey");
		}));
}

bool RunUnknownBodyRegionCase(FAutomationTestBase& Test, const FProfileCase& ProfileCase)
{
	const FString Target = MakeUniqueTarget(*FString::Printf(TEXT("%s_UnknownRegion"), ProfileCase.Label));
	const FString ObjectPath = ToObjectPath(Target);
	const FString PackageFileName = ToPackageFileName(Target);
	CleanupAsset(Target);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeTemplateDocument(
		Test,
		Service,
		Target,
		ProfileCase.ClassPath,
		TEXT("CreateOrUpdate"));
	if (!Document.IsValid())
	{
		return false;
	}

	TSharedRef<FJsonObject> UnknownBody = MakeShared<FJsonObject>();
	UnknownBody->SetObjectField(TEXT("UnknownRegion"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Body"), UnknownBody);

	FScopedAssetAddedCounter AssetAddedCounter(ObjectPath);

	FAssetDocumentValidateRequest ValidateRequest;
	ValidateRequest.Document = Document;
	const FAssetDocumentResult ValidateResult = Service.Validate(ValidateRequest);

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);

	FAssetDocumentApplyRequest ApplyRequest;
	ApplyRequest.Document = Document;
	ApplyRequest.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(ApplyRequest);

	const FString CaseLabel(ProfileCase.Label);
	ExpectUnknownBodyRegionFailure(Test, CaseLabel, TEXT("Validate"), ValidateResult);
	ExpectUnknownBodyRegionFailure(Test, CaseLabel, TEXT("Diff"), DiffResult);
	ExpectUnknownBodyRegionFailure(Test, CaseLabel, TEXT("Apply"), ApplyResult);
	Test.TestEqual(
		*FString::Printf(TEXT("%s Diff unknown-region message matches Validate"), ProfileCase.Label),
		DiffResult.Message,
		ValidateResult.Message);
	Test.TestEqual(
		*FString::Printf(TEXT("%s Apply unknown-region message matches Validate"), ProfileCase.Label),
		ApplyResult.Message,
		ValidateResult.Message);

	Test.TestEqual(
		*FString::Printf(TEXT("%s unknown-region surfaces never announce a real asset"), ProfileCase.Label),
		AssetAddedCounter.GetMatchingAssetAddedCount(),
		0);
	Test.TestNull(
		*FString::Printf(TEXT("%s unknown-region surfaces leave no real package"), ProfileCase.Label),
		FindPackage(nullptr, *Target));
	Test.TestNull(
		*FString::Printf(TEXT("%s unknown-region surfaces leave no real asset"), ProfileCase.Label),
		FindObject<UObject>(nullptr, *ObjectPath));
	Test.TestFalse(
		*FString::Printf(TEXT("%s unknown-region surfaces leave no AssetRegistry entry"), ProfileCase.Label),
		RegistryContainsObjectPath(ObjectPath));
	Test.TestFalse(
		*FString::Printf(TEXT("%s unknown-region surfaces write no uasset"), ProfileCase.Label),
		IFileManager::Get().FileExists(*PackageFileName));

	CleanupAsset(Target);
	return true;
}

bool RunBlueprintCreationPrerequisiteCase(
	FAutomationTestBase& Test,
	const FBlueprintCreationCase& CreationCase,
	bool bRemoveWholeBody)
{
	const FString Variant = bRemoveWholeBody ? TEXT("MissingBody") : TEXT("MissingParentClass");
	const FString CaseLabel = FString::Printf(TEXT("%s %s"), CreationCase.Label, *Variant);
	const FString Target = MakeUniqueTarget(*FString::Printf(TEXT("%s_%s"), CreationCase.Label, *Variant));
	CleanupAsset(Target);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeTemplateDocument(
		Test,
		Service,
		Target,
		CreationCase.ClassPath,
		TEXT("Create"));
	if (!Document.IsValid())
	{
		return false;
	}

	if (bRemoveWholeBody)
	{
		Document->RemoveField(TEXT("Body"));
	}
	else
	{
		const TSharedPtr<FJsonObject>* Body = nullptr;
		if (!Document->TryGetObjectField(TEXT("Body"), Body) || !Body || !Body->IsValid())
		{
			Test.AddError(FString::Printf(TEXT("%s template has no Body"), CreationCase.Label));
			return false;
		}
		(*Body)->RemoveField(TEXT("ParentClass"));
	}

	FScopedAssetAddedCounter AssetAddedCounter(ToObjectPath(Target));
	const FSurfaceResults Results = RunPublicSurfaces(Service, Document);
	ExpectSurfaceFailureMessageParity(
		Test,
		CaseLabel,
		Results,
		bRemoveWholeBody ? CreationCase.MissingBodyMessage : CreationCase.MissingParentClassMessage);
	ExpectNoMaterializedTarget(Test, CaseLabel, Target, AssetAddedCounter);

	CleanupAsset(Target);
	return true;
}

bool RunCreateExistingInvalidPropertiesCase(FAutomationTestBase& Test, const FProfileCase& ProfileCase)
{
	const FString CaseLabel = FString::Printf(TEXT("%s Create existing with invalid Properties"), ProfileCase.Label);
	const FString Target = MakeUniqueTarget(*FString::Printf(TEXT("%s_CreateExistingInvalidProperties"), ProfileCase.Label));
	const FString ObjectPath = ToObjectPath(Target);
	CleanupAsset(Target);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> FixtureDocument = MakeTemplateDocument(
		Test,
		Service,
		Target,
		ProfileCase.ClassPath,
		TEXT("Create"));
	FAssetDocumentApplyRequest FixtureRequest;
	FixtureRequest.Document = FixtureDocument;
	FixtureRequest.bSaveAsset = false;
	const FAssetDocumentResult FixtureResult = Service.Apply(FixtureRequest);
	Test.TestTrue(*FString::Printf(TEXT("%s fixture apply succeeds"), *CaseLabel), FixtureResult.IsSuccess());
	if (!FixtureResult.IsSuccess())
	{
		CleanupAsset(Target);
		return false;
	}

	UObject* ExistingAsset = FindObject<UObject>(nullptr, *ObjectPath);
	UPackage* ExistingPackage = ExistingAsset ? ExistingAsset->GetOutermost() : nullptr;
	Test.TestNotNull(*FString::Printf(TEXT("%s fixture asset exists"), *CaseLabel), ExistingAsset);
	Test.TestNotNull(*FString::Printf(TEXT("%s fixture package exists"), *CaseLabel), ExistingPackage);
	if (!ExistingAsset || !ExistingPackage)
	{
		CleanupAsset(Target);
		return false;
	}

	ExistingPackage->ClearDirtyFlag();
	const int32 OwnedObjectCountBefore = CountOwnedObjects(ExistingAsset);
	FScopedAssetAddedCounter AssetAddedCounter(ObjectPath);

	TSharedPtr<FJsonObject> InvalidDocument = MakeTemplateDocument(
		Test,
		Service,
		Target,
		ProfileCase.ClassPath,
		TEXT("Create"));
	InvalidDocument->GetObjectField(TEXT("Properties"))->SetStringField(
		TEXT("DefinitelyNotARealProperty"),
		TEXT("invalid"));

	const FSurfaceResults Results = RunPublicSurfaces(Service, InvalidDocument);
	ExpectSurfaceFailureMessageParity(
		Test,
		CaseLabel,
		Results,
		FString::Printf(TEXT("Create failed because target '%s' already exists"), *Target));
	Test.TestEqual(
		*FString::Printf(TEXT("%s read-only lookup announces no new asset"), *CaseLabel),
		AssetAddedCounter.GetMatchingAssetAddedCount(),
		0);
	Test.TestFalse(*FString::Printf(TEXT("%s read-only lookup leaves the package clean"), *CaseLabel), ExistingPackage->IsDirty());
	Test.TestEqual(
		*FString::Printf(TEXT("%s read-only lookup creates no owned objects"), *CaseLabel),
		CountOwnedObjects(ExistingAsset),
		OwnedObjectCountBefore);

	CleanupAsset(Target);
	return true;
}

bool RunAnimMontageInvalidReferenceCase(FAutomationTestBase& Test)
{
	const FString CaseLabel(TEXT("AnimMontage wrong-class Skeleton creation preflight"));
	const FString Target = MakeUniqueTarget(TEXT("AnimMontage_InvalidSkeleton"));
	const FString WrongClassSkeletonPath(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	CleanupAsset(Target);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeTemplateDocument(
		Test,
		Service,
		Target,
		TEXT("/Script/Engine.AnimMontage"),
		TEXT("Create"));
	if (!Document.IsValid())
	{
		return false;
	}
	Document->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("Skeleton"), MakeAssetRef(WrongClassSkeletonPath));

	FScopedAssetAddedCounter AssetAddedCounter(ToObjectPath(Target));
	FAssetDocumentApplyRequest ApplyRequest;
	ApplyRequest.Document = Document;
	ApplyRequest.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(ApplyRequest);
	Test.TestFalse(TEXT("AnimMontage wrong-class Skeleton is rejected before creation"), ApplyResult.IsSuccess());
	Test.TestTrue(
		TEXT("AnimMontage wrong-class Skeleton failure is attributed to Body.Skeleton"),
		ApplyResult.Diagnostics.ContainsByPredicate([](const FAssetDocumentDiagnostic& Diagnostic)
		{
			return Diagnostic.Path == TEXT("/Body/Skeleton");
		}));
	ExpectNoMaterializedTarget(Test, CaseLabel, Target, AssetAddedCounter);

	CleanupAsset(Target);
	return true;
}

bool RunAnimMontageMissingTimeStretchCurveCase(FAutomationTestBase& Test)
{
	const FString CaseLabel(TEXT("AnimMontage missing TimeStretch curve creation preflight"));
	const FString Target = MakeUniqueTarget(TEXT("AnimMontage_MissingTimeStretchCurve"));
	CleanupAsset(Target);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeTemplateDocument(
		Test,
		Service,
		Target,
		TEXT("/Script/Engine.AnimMontage"),
		TEXT("Create"));
	if (!Document.IsValid())
	{
		return false;
	}

	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	Body->RemoveField(TEXT("Curves"));
	Body->GetObjectField(TEXT("TimeStretch"))->SetStringField(TEXT("TimeStretchCurveName"), TEXT("MissingCurve"));

	FScopedAssetAddedCounter AssetAddedCounter(ToObjectPath(Target));
	const FSurfaceResults Results = RunPublicSurfaces(Service, Document);
	ExpectSurfaceFailureMessageParity(
		Test,
		CaseLabel,
		Results,
		TEXT("TimeStretchCurveName must reference a montage-owned float curve"));

	auto HasExactDiagnostic = [](const FAssetDocumentResult& Result)
	{
		return Result.Diagnostics.ContainsByPredicate([](const FAssetDocumentDiagnostic& Diagnostic)
		{
			return Diagnostic.Path == TEXT("/Body/TimeStretch/TimeStretchCurveName")
				&& Diagnostic.Code == TEXT("MissingTimeStretchCurve");
		});
	};
	Test.TestTrue(TEXT("Validate reports exact MissingTimeStretchCurve diagnostic"), HasExactDiagnostic(Results.Validate));
	Test.TestTrue(TEXT("Diff reports exact MissingTimeStretchCurve diagnostic"), HasExactDiagnostic(Results.Diff));
	Test.TestTrue(TEXT("Apply reports exact MissingTimeStretchCurve diagnostic"), HasExactDiagnostic(Results.Apply));
	ExpectNoMaterializedTarget(Test, CaseLabel, Target, AssetAddedCounter);

	CleanupAsset(Target);
	return true;
}

bool RunDiskExistingCreateCase(FAutomationTestBase& Test)
{
	const FString CaseLabel(TEXT("Create disk-existing target"));
	const FString Target = MakeUniqueTarget(TEXT("DiskExisting"));
	const FString ObjectPath = ToObjectPath(Target);
	const FString PackageFileName = ToPackageFileName(Target);
	const FString SentinelContents(TEXT("Task7b1 disk-existing sentinel"));
	CleanupAsset(Target);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeTemplateDocument(
		Test,
		Service,
		Target,
		ProfileCases[0].ClassPath,
		TEXT("Create"));
	if (!Document.IsValid())
	{
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFileName), true);
	const bool bWroteSentinel = FFileHelper::SaveStringToFile(SentinelContents, *PackageFileName);
	Test.TestTrue(TEXT("Disk-existing fixture writes an unloaded package sentinel"), bWroteSentinel);
	Test.TestNull(TEXT("Disk-existing fixture starts without a loaded package"), FindPackage(nullptr, *Target));
	if (!bWroteSentinel)
	{
		return false;
	}

	FScopedAssetAddedCounter AssetAddedCounter(ObjectPath);
	const FSurfaceResults Results = RunPublicSurfaces(Service, Document);
	ExpectSurfaceFailureMessageParity(
		Test,
		CaseLabel,
		Results,
		FString::Printf(TEXT("Create failed because target '%s' already exists"), *Target));

	FString ContentsAfter;
	Test.TestTrue(TEXT("Disk-existing sentinel remains readable"), FFileHelper::LoadFileToString(ContentsAfter, *PackageFileName));
	Test.TestEqual(TEXT("Disk-existing sentinel remains unchanged"), ContentsAfter, SentinelContents);
	Test.TestNull(TEXT("Create disk-existing lookup does not load a package"), FindPackage(nullptr, *Target));
	Test.TestNull(TEXT("Create disk-existing lookup creates no asset"), FindObject<UObject>(nullptr, *ObjectPath));
	Test.TestFalse(TEXT("Create disk-existing lookup registers no asset"), RegistryContainsObjectPath(ObjectPath));
	Test.TestEqual(TEXT("Create disk-existing lookup announces no asset"), AssetAddedCounter.GetMatchingAssetAddedCount(), 0);

	IFileManager::Get().Delete(*PackageFileName, false, true);
	return true;
}

bool RunInvalidCreateCase(FAutomationTestBase& Test, const FProfileCase& ProfileCase)
{
	const FString Target = MakeUniqueTarget(*FString::Printf(TEXT("%s_InvalidPreflight"), ProfileCase.Label));
	const FString ObjectPath = ToObjectPath(Target);
	const FString PackageFileName = ToPackageFileName(Target);
	CleanupAsset(Target);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> InvalidDocument = MakeTemplateDocument(
		Test,
		Service,
		Target,
		ProfileCase.ClassPath,
		TEXT("Create"));
	if (!InvalidateKnownBodyRegion(Test, InvalidDocument, ProfileCase.InvalidBodyKey))
	{
		CleanupAsset(Target);
		return false;
	}

	FScopedAssetAddedCounter AssetAddedCounter(ObjectPath);
	FAssetDocumentApplyRequest InvalidRequest;
	InvalidRequest.Document = InvalidDocument;
	InvalidRequest.bSaveAsset = false;
	const FAssetDocumentResult InvalidResult = Service.Apply(InvalidRequest);

	Test.TestFalse(
		*FString::Printf(TEXT("%s invalid Body preflight fails"), ProfileCase.Label),
		InvalidResult.IsSuccess());
	Test.TestTrue(
		*FString::Printf(TEXT("%s failure comes from Body/profile preflight"), ProfileCase.Label),
		ResultHasBodyDiagnostic(InvalidResult));
	Test.TestEqual(
		*FString::Printf(TEXT("%s invalid preflight never announces a real asset to AssetRegistry"), ProfileCase.Label),
		AssetAddedCounter.GetMatchingAssetAddedCount(),
		0);
	Test.TestNull(
		*FString::Printf(TEXT("%s invalid preflight leaves no real package"), ProfileCase.Label),
		FindPackage(nullptr, *Target));
	Test.TestNull(
		*FString::Printf(TEXT("%s invalid preflight leaves no real asset"), ProfileCase.Label),
		FindObject<UObject>(nullptr, *ObjectPath));
	Test.TestFalse(
		*FString::Printf(TEXT("%s invalid preflight leaves no AssetRegistry entry"), ProfileCase.Label),
		RegistryContainsObjectPath(ObjectPath));
	Test.TestFalse(
		*FString::Printf(TEXT("%s invalid preflight writes no uasset"), ProfileCase.Label),
		IFileManager::Get().FileExists(*PackageFileName));

	TSharedPtr<FJsonObject> ValidDocument = MakeTemplateDocument(
		Test,
		Service,
		Target,
		ProfileCase.ClassPath,
		TEXT("Create"));
	FAssetDocumentApplyRequest RetryRequest;
	RetryRequest.Document = ValidDocument;
	RetryRequest.bSaveAsset = false;
	const FAssetDocumentResult RetryResult = Service.Apply(RetryRequest);
	Test.TestTrue(
		*FString::Printf(TEXT("%s valid retry succeeds after rejected preflight"), ProfileCase.Label),
		RetryResult.IsSuccess());
	Test.TestNotNull(
		*FString::Printf(TEXT("%s valid retry creates the requested asset"), ProfileCase.Label),
		FindObject<UObject>(nullptr, *ObjectPath));

	CleanupAsset(Target);
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyPreflightBehaviorTreeInvalidBodyTest,
	"AssetFactory.AssetDocument.ApplyPreflight.BehaviorTree.InvalidBodyDoesNotCreate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyPreflightBehaviorTreeInvalidBodyTest::RunTest(const FString& Parameters)
{
	return AssetDocumentApplyPreflightTests::RunInvalidCreateCase(
		*this,
		AssetDocumentApplyPreflightTests::ProfileCases[0]);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyPreflightBlackboardDataInvalidBodyTest,
	"AssetFactory.AssetDocument.ApplyPreflight.BlackboardData.InvalidBodyDoesNotCreate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyPreflightBlackboardDataInvalidBodyTest::RunTest(const FString& Parameters)
{
	return AssetDocumentApplyPreflightTests::RunInvalidCreateCase(
		*this,
		AssetDocumentApplyPreflightTests::ProfileCases[1]);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyPreflightUnknownBodyRegionTest,
	"AssetFactory.AssetDocument.ApplyPreflight.UnknownBodyRegion.SurfaceParityDoesNotCreate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyPreflightUnknownBodyRegionTest::RunTest(const FString& Parameters)
{
	using namespace AssetDocumentApplyPreflightTests;

	bool bSuccess = true;
	for (const FProfileCase& ProfileCase : ProfileCases)
	{
		bSuccess &= RunUnknownBodyRegionCase(*this, ProfileCase);
	}
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyPreflightBlueprintCreationPrerequisitesTest,
	"AssetFactory.AssetDocument.ApplyPreflight.CreationPrerequisites.BlueprintFamilies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyPreflightBlueprintCreationPrerequisitesTest::RunTest(const FString& Parameters)
{
	using namespace AssetDocumentApplyPreflightTests;

	bool bSuccess = true;
	for (const FBlueprintCreationCase& CreationCase : BlueprintCreationCases)
	{
		bSuccess &= RunBlueprintCreationPrerequisiteCase(*this, CreationCase, true);
		bSuccess &= RunBlueprintCreationPrerequisiteCase(*this, CreationCase, false);
	}
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyPreflightAnimMontageFragmentResolutionTest,
	"AssetFactory.AssetDocument.ApplyPreflight.AnimMontage.InvalidFragmentDoesNotCreate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyPreflightAnimMontageFragmentResolutionTest::RunTest(const FString& Parameters)
{
	return AssetDocumentApplyPreflightTests::RunAnimMontageInvalidReferenceCase(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyPreflightAnimMontageMissingTimeStretchCurveTest,
	"AssetFactory.AssetDocument.ApplyPreflight.AnimMontage.MissingTimeStretchCurveDoesNotCreate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyPreflightAnimMontageMissingTimeStretchCurveTest::RunTest(const FString& Parameters)
{
	return AssetDocumentApplyPreflightTests::RunAnimMontageMissingTimeStretchCurveCase(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyPreflightLifecyclePropertyPrecedenceTest,
	"AssetFactory.AssetDocument.ApplyPreflight.Lifecycle.PrecedesInvalidPropertiesWithSurfaceParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyPreflightLifecyclePropertyPrecedenceTest::RunTest(const FString& Parameters)
{
	using namespace AssetDocumentApplyPreflightTests;

	bool bSuccess = true;
	for (const FProfileCase& ProfileCase : ProfileCases)
	{
		bSuccess &= RunCreateExistingInvalidPropertiesCase(*this, ProfileCase);
	}
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyPreflightDiskExistingCreateTest,
	"AssetFactory.AssetDocument.ApplyPreflight.Lifecycle.DiskExistingCreateStaysUnloadedAndClean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyPreflightDiskExistingCreateTest::RunTest(const FString& Parameters)
{
	return AssetDocumentApplyPreflightTests::RunDiskExistingCreateCase(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyPreflightLifecyclePrecedenceTest,
	"AssetFactory.AssetDocument.ApplyPreflight.Lifecycle.PrecedesBodyPreflightWithSurfaceParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyPreflightLifecyclePrecedenceTest::RunTest(const FString& Parameters)
{
	using namespace AssetDocumentApplyPreflightTests;

	FAssetDocumentService Service;
	for (const FProfileCase& ProfileCase : ProfileCases)
	{
		const FString ExistingTarget = MakeUniqueTarget(*FString::Printf(TEXT("%s_CreateExisting"), ProfileCase.Label));
		CleanupAsset(ExistingTarget);

		TSharedPtr<FJsonObject> FixtureDocument = MakeTemplateDocument(
			*this,
			Service,
			ExistingTarget,
			ProfileCase.ClassPath,
			TEXT("Create"));
		FAssetDocumentApplyRequest FixtureRequest;
		FixtureRequest.Document = FixtureDocument;
		FixtureRequest.bSaveAsset = false;
		const FAssetDocumentResult FixtureResult = Service.Apply(FixtureRequest);
		TestTrue(
			*FString::Printf(TEXT("%s existing-target fixture is created"), ProfileCase.Label),
			FixtureResult.IsSuccess());

		TSharedPtr<FJsonObject> CreateExistingDocument = MakeTemplateDocument(
			*this,
			Service,
			ExistingTarget,
			ProfileCase.ClassPath,
			TEXT("Create"));
		if (InvalidateKnownBodyRegion(*this, CreateExistingDocument, ProfileCase.InvalidBodyKey))
		{
			ExpectLifecycleParity(
				*this,
				FString::Printf(TEXT("%s Create existing"), ProfileCase.Label),
				Service,
				CreateExistingDocument,
				FString::Printf(TEXT("Create failed because target '%s' already exists"), *ExistingTarget));
		}
		CleanupAsset(ExistingTarget);

		const FString MissingTarget = MakeUniqueTarget(*FString::Printf(TEXT("%s_UpdateMissing"), ProfileCase.Label));
		CleanupAsset(MissingTarget);
		TSharedPtr<FJsonObject> UpdateMissingDocument = MakeTemplateDocument(
			*this,
			Service,
			MissingTarget,
			ProfileCase.ClassPath,
			TEXT("Update"));
		if (InvalidateKnownBodyRegion(*this, UpdateMissingDocument, ProfileCase.InvalidBodyKey))
		{
			ExpectLifecycleParity(
				*this,
				FString::Printf(TEXT("%s Update missing"), ProfileCase.Label),
				Service,
				UpdateMissingDocument,
				FString::Printf(TEXT("Update failed because target '%s' does not exist"), *MissingTarget));
		}
		CleanupAsset(MissingTarget);
	}

	const FString ClassMismatchTarget = MakeUniqueTarget(TEXT("ClassMismatch"));
	CleanupAsset(ClassMismatchTarget);
	TSharedPtr<FJsonObject> BlackboardFixture = MakeTemplateDocument(
		*this,
		Service,
		ClassMismatchTarget,
		ProfileCases[1].ClassPath,
		TEXT("Create"));
	FAssetDocumentApplyRequest BlackboardFixtureRequest;
	BlackboardFixtureRequest.Document = BlackboardFixture;
	BlackboardFixtureRequest.bSaveAsset = false;
	const FAssetDocumentResult BlackboardFixtureResult = Service.Apply(BlackboardFixtureRequest);
	TestTrue(TEXT("Class-mismatch BlackboardData fixture is created"), BlackboardFixtureResult.IsSuccess());

	TSharedPtr<FJsonObject> ClassMismatchDocument = MakeTemplateDocument(
		*this,
		Service,
		ClassMismatchTarget,
		ProfileCases[0].ClassPath,
		TEXT("CreateOrUpdate"));
	if (InvalidateKnownBodyRegion(*this, ClassMismatchDocument, ProfileCases[0].InvalidBodyKey))
	{
		ExpectLifecycleParity(
			*this,
			TEXT("Existing class mismatch"),
			Service,
			ClassMismatchDocument,
			FString::Printf(
				TEXT("Existing asset '%s' is not an exact UBehaviorTree asset"),
				*ToObjectPath(ClassMismatchTarget)));
	}
	CleanupAsset(ClassMismatchTarget);

	return true;
}

#endif
