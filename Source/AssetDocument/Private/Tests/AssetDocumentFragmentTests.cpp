// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFragment.h"

#include "AssetDocumentFragmentCompiler.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
class FTestFragmentAdapter final : public IAssetDocumentFragmentAdapter
{
public:
	virtual FName GetKind() const override
	{
		return TEXT("TestKind");
	}

	virtual bool SupportsContext(const FAssetDocumentFragmentContext& Context) const override
	{
		return true;
	}

	virtual FAssetDocumentFragmentResult Validate(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		return FAssetDocumentFragmentResult::Success();
	}

	virtual FAssetDocumentFragmentResult Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FAssetDocumentFragmentResult Result = FAssetDocumentFragmentResult::Success();
		Result.Value = MakeShared<FJsonValueString>(TEXT("compiled"));
		return Result;
	}

	virtual FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const override
	{
		OutFragmentJson->SetStringField(TEXT("Kind"), GetKind().ToString());
		OutFragmentJson->SetStringField(TEXT("Role"), Context.Role);
		return FAssetDocumentFragmentResult::Success();
	}
};

TSharedRef<FJsonObject> MakeFragment(const TCHAR* Kind)
{
	TSharedRef<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), Kind);
	return Fragment;
}

bool HasDiagnosticCode(const FAssetDocumentFragmentResult& Result, const TCHAR* Code)
{
	return Result.Diagnostics.ContainsByPredicate([Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == Code;
	});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentFragmentCompilerDispatchTest,
	"AssetFactory.AssetDocument.Fragments.Dispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentFragmentCompilerDispatchTest::RunTest(const FString& Parameters)
{
	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterAdapter(MakeShared<FTestFragmentAdapter>());

	FAssetDocumentFragmentContext Context;
	Context.JsonPath = TEXT("/Body/Test");

	TSharedRef<FJsonObject> KnownFragment = MakeShared<FJsonObject>();
	KnownFragment->SetStringField(TEXT("Kind"), TEXT("TestKind"));

	const FAssetDocumentFragmentResult KnownResult = Compiler.Compile(KnownFragment, Context);
	TestTrue(TEXT("Known fragment kind compiles"), KnownResult.bSuccess);
	TestTrue(TEXT("Known fragment kind produces a JSON value"), KnownResult.Value.IsValid());

	TSharedRef<FJsonObject> MissingFragment = MakeShared<FJsonObject>();
	MissingFragment->SetStringField(TEXT("Kind"), TEXT("MissingKind"));

	const FAssetDocumentFragmentResult MissingResult = Compiler.Compile(MissingFragment, Context);
	TestFalse(TEXT("Missing fragment kind fails"), MissingResult.bSuccess);
	TestTrue(TEXT("Missing fragment kind reports at least one diagnostic"), MissingResult.Diagnostics.Num() > 0);
	if (MissingResult.Diagnostics.Num() > 0)
	{
		TestEqual(TEXT("Missing fragment diagnostic uses the fragment path"), MissingResult.Diagnostics[0].Path, FString(TEXT("/Body/Test")));
	}

	FAssetDocumentFragmentExtractContext ExtractContext;
	ExtractContext.Kind = TEXT("TestKind");
	ExtractContext.JsonPath = TEXT("/Body/Test");
	ExtractContext.Role = TEXT("SemanticRole");

	TSharedRef<FJsonObject> ExtractedFragment = MakeShared<FJsonObject>();
	const FAssetDocumentFragmentResult ExtractResult = Compiler.Extract(ExtractContext, ExtractedFragment);
	TestTrue(TEXT("Known fragment role extracts"), ExtractResult.bSuccess);
	TestEqual(TEXT("Extract writes fragment Kind"), ExtractedFragment->GetStringField(TEXT("Kind")), FString(TEXT("TestKind")));
	TestEqual(TEXT("Extract preserves semantic role separately from kind"), ExtractedFragment->GetStringField(TEXT("Role")), FString(TEXT("SemanticRole")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBuiltInFragmentAdaptersTest,
	"AssetFactory.AssetDocument.Fragments.BuiltInAdapters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBuiltInFragmentAdaptersTest::RunTest(const FString& Parameters)
{
	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	FAssetDocumentFragmentContext Context;
	Context.JsonPath = TEXT("/Body/Test");

	TSharedRef<FJsonObject> AssetRef = MakeFragment(TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton"));
	Context.ExpectedBaseClass = USkeleton::StaticClass();

	const FAssetDocumentFragmentResult AssetRefResult = Compiler.Compile(AssetRef, Context);
	TestTrue(TEXT("AssetRef loads an asset that matches ExpectedBaseClass"), AssetRefResult.bSuccess);
	TestTrue(TEXT("AssetRef returns the loaded asset object"), IsValid(AssetRefResult.Object));
	if (AssetRefResult.Object)
	{
		TestTrue(TEXT("AssetRef object is a skeleton"), AssetRefResult.Object->IsA(USkeleton::StaticClass()));
	}
	TestTrue(TEXT("AssetRef returns the original path as Value"), AssetRefResult.Value.IsValid() && AssetRefResult.Value->AsString().Contains(TEXT("TutorialTPP_Skeleton")));

	TSharedRef<FJsonObject> ClassRef = MakeFragment(TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimMontage"));
	Context.ExpectedBaseClass = UObject::StaticClass();

	const FAssetDocumentFragmentResult ClassRefResult = Compiler.Compile(ClassRef, Context);
	TestTrue(TEXT("ClassRef resolves a class that matches ExpectedBaseClass"), ClassRefResult.bSuccess);
	TestEqual(TEXT("ClassRef returns AnimMontage class"), ClassRefResult.Class, UAnimMontage::StaticClass());

	FAssetDocumentFragmentExtractContext ClassExtractContext;
	ClassExtractContext.Kind = TEXT("ClassRef");
	ClassExtractContext.ValueObject = UAnimMontage::StaticClass();
	ClassExtractContext.JsonPath = TEXT("/Body/ClassExtract");
	TSharedRef<FJsonObject> ExtractedClassRef = MakeShared<FJsonObject>();
	const FAssetDocumentFragmentResult ClassExtractResult = Compiler.Extract(ClassExtractContext, ExtractedClassRef);
	TestTrue(TEXT("ClassRef extracts a UClass value"), ClassExtractResult.bSuccess);
	TestEqual(TEXT("ClassRef extracts the class value path"), ExtractedClassRef->GetStringField(TEXT("Class")), FString(TEXT("/Script/Engine.AnimMontage")));

	TSharedRef<FJsonObject> StructValue = MakeFragment(TEXT("StructValue"));
	StructValue->SetStringField(TEXT("Struct"), TEXT("/Script/CoreUObject.Vector"));
	TSharedRef<FJsonObject> VectorProperties = MakeShared<FJsonObject>();
	VectorProperties->SetNumberField(TEXT("X"), 1.25);
	VectorProperties->SetNumberField(TEXT("Y"), -2.5);
	VectorProperties->SetNumberField(TEXT("Z"), 3.75);
	StructValue->SetObjectField(TEXT("Properties"), VectorProperties);
	Context.ExpectedBaseClass = nullptr;
	Context.ExpectedStruct = TBaseStructure<FVector>::Get();

	const FAssetDocumentFragmentResult StructValueResult = Compiler.Compile(StructValue, Context);
	TestTrue(TEXT("StructValue compiles a reflected FVector"), StructValueResult.bSuccess);
	TestEqual(TEXT("StructValue returns the expected struct type"), StructValueResult.StructType, TBaseStructure<FVector>::Get());
	TestEqual(TEXT("StructValue returns bytes sized for the struct"), StructValueResult.StructBytes.Num(), TBaseStructure<FVector>::Get()->GetStructureSize());
	if (StructValueResult.StructBytes.Num() == TBaseStructure<FVector>::Get()->GetStructureSize())
	{
		const FVector* Vector = reinterpret_cast<const FVector*>(StructValueResult.StructBytes.GetData());
		TestEqual(TEXT("StructValue sets X"), Vector->X, 1.25);
		TestEqual(TEXT("StructValue sets Y"), Vector->Y, -2.5);
		TestEqual(TEXT("StructValue sets Z"), Vector->Z, 3.75);
	}

	FVector ExtractVector(4.0, 5.5, -6.25);
	FAssetDocumentFragmentExtractContext StructExtractContext;
	StructExtractContext.Kind = TEXT("StructValue");
	StructExtractContext.StructType = TBaseStructure<FVector>::Get();
	StructExtractContext.StructValue = &ExtractVector;
	StructExtractContext.JsonPath = TEXT("/Body/StructExtract");
	TSharedRef<FJsonObject> ExtractedStructValue = MakeShared<FJsonObject>();
	const FAssetDocumentFragmentResult StructExtractResult = Compiler.Extract(StructExtractContext, ExtractedStructValue);
	TestTrue(TEXT("StructValue extracts a struct value"), StructExtractResult.bSuccess);
	const TSharedPtr<FJsonObject>* ExtractedPropertiesPtr = nullptr;
	TestTrue(TEXT("StructValue extract writes Properties"), ExtractedStructValue->TryGetObjectField(TEXT("Properties"), ExtractedPropertiesPtr) && ExtractedPropertiesPtr && ExtractedPropertiesPtr->IsValid());
	if (ExtractedPropertiesPtr && ExtractedPropertiesPtr->IsValid())
	{
		TestEqual(TEXT("StructValue extract writes X"), (*ExtractedPropertiesPtr)->GetNumberField(TEXT("X")), 4.0);
		TestEqual(TEXT("StructValue extract writes Y"), (*ExtractedPropertiesPtr)->GetNumberField(TEXT("Y")), 5.5);
		TestEqual(TEXT("StructValue extract writes Z"), (*ExtractedPropertiesPtr)->GetNumberField(TEXT("Z")), -6.25);
	}

	TSharedRef<FJsonObject> UnsupportedStructValue = MakeFragment(TEXT("StructValue"));
	UnsupportedStructValue->SetStringField(TEXT("Struct"), TEXT("/Script/CoreUObject.SoftObjectPath"));
	UnsupportedStructValue->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Context.ExpectedStruct = FSoftObjectPath::StaticStruct();
	const FAssetDocumentFragmentResult UnsupportedStructResult = Compiler.Compile(UnsupportedStructValue, Context);
	TestFalse(TEXT("StructValue rejects structs that need explicit destruction"), UnsupportedStructResult.bSuccess);
	TestTrue(TEXT("StructValue unsupported lifecycle reports code"), HasDiagnosticCode(UnsupportedStructResult, TEXT("structvalue-unsupported-lifecycle")));

	TSharedRef<FJsonObject> EmbeddedObject = MakeFragment(TEXT("EmbeddedObject"));
	EmbeddedObject->SetStringField(TEXT("Class"), TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"));
	Context.ExpectedStruct = nullptr;
	Context.ExpectedBaseClass = UAnimNotifyState::StaticClass();
	Context.Outer = GetTransientPackage();

	const FAssetDocumentFragmentResult EmbeddedObjectResult = Compiler.Compile(EmbeddedObject, Context);
	TestTrue(TEXT("EmbeddedObject creates an object using the supplied Outer"), EmbeddedObjectResult.bSuccess);
	TestTrue(TEXT("EmbeddedObject returns an object"), IsValid(EmbeddedObjectResult.Object));
	if (EmbeddedObjectResult.Object)
	{
		TestTrue(TEXT("EmbeddedObject object is an AnimNotifyState"), EmbeddedObjectResult.Object->IsA(UAnimNotifyState::StaticClass()));
		TestTrue(TEXT("EmbeddedObject uses the context Outer"), EmbeddedObjectResult.Object->GetOuter() == GetTransientPackage());
	}

	TSharedRef<FJsonObject> BadEmbeddedObject = MakeFragment(TEXT("EmbeddedObject"));
	BadEmbeddedObject->SetStringField(TEXT("Class"), TEXT("/Script/AssetFactory.AssetFactoryNamedAnimNotifyState"));
	TSharedRef<FJsonObject> BadProperties = MakeShared<FJsonObject>();
	BadProperties->SetStringField(TEXT("DefinitelyNotAProperty"), TEXT("bad"));
	BadEmbeddedObject->SetObjectField(TEXT("Properties"), BadProperties);
	Context.ExpectedBaseClass = UAnimNotifyState::StaticClass();
	Context.Outer = GetTransientPackage();

	const FAssetDocumentFragmentResult BadEmbeddedObjectResult = Compiler.Compile(BadEmbeddedObject, Context);
	TestFalse(TEXT("EmbeddedObject rejects bad properties before creation"), BadEmbeddedObjectResult.bSuccess);
	TestTrue(TEXT("EmbeddedObject bad property failure reports preflight code"), HasDiagnosticCode(BadEmbeddedObjectResult, TEXT("embeddedobject-preflight-failed")));
	TestNull(TEXT("EmbeddedObject bad property failure does not return an object"), BadEmbeddedObjectResult.Object);

	TSharedPtr<FJsonObject> Definitions = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> AttackAnimDefinition = MakeFragment(TEXT("AssetRef"));
	AttackAnimDefinition->SetStringField(TEXT("Path"), TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton"));
	Definitions->SetObjectField(TEXT("AttackAnim"), AttackAnimDefinition);

	TSharedRef<FJsonObject> DefinitionRef = MakeFragment(TEXT("DefinitionRef"));
	DefinitionRef->SetStringField(TEXT("Id"), TEXT("AttackAnim"));
	Context.ExpectedBaseClass = USkeleton::StaticClass();
	Context.Outer = nullptr;
	Context.Definitions = &Definitions;
	Context.JsonPath = TEXT("/Body/DefinitionRef");

	const FAssetDocumentFragmentResult DefinitionRefResult = Compiler.Compile(DefinitionRef, Context);
	TestTrue(TEXT("DefinitionRef expands a named definition through the compiler"), DefinitionRefResult.bSuccess);
	TestTrue(TEXT("DefinitionRef returns the expanded asset object"), IsValid(DefinitionRefResult.Object));
	if (DefinitionRefResult.Object)
	{
		TestTrue(TEXT("DefinitionRef expanded object is a skeleton"), DefinitionRefResult.Object->IsA(USkeleton::StaticClass()));
	}

	TSharedPtr<FJsonObject> CyclicDefinitions = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> A = MakeFragment(TEXT("DefinitionRef"));
	A->SetStringField(TEXT("Id"), TEXT("B"));
	TSharedRef<FJsonObject> B = MakeFragment(TEXT("DefinitionRef"));
	B->SetStringField(TEXT("Id"), TEXT("A"));
	CyclicDefinitions->SetObjectField(TEXT("A"), A);
	CyclicDefinitions->SetObjectField(TEXT("B"), B);

	TSharedRef<FJsonObject> CyclicDefinitionRef = MakeFragment(TEXT("DefinitionRef"));
	CyclicDefinitionRef->SetStringField(TEXT("Id"), TEXT("A"));
	Context.ExpectedBaseClass = nullptr;
	Context.Definitions = &CyclicDefinitions;
	Context.DefinitionStack.Reset();
	Context.JsonPath = TEXT("/Body/Cycle");

	const FAssetDocumentFragmentResult CyclicResult = Compiler.Compile(CyclicDefinitionRef, Context);
	TestFalse(TEXT("DefinitionRef rejects cycles"), CyclicResult.bSuccess);
	TestTrue(TEXT("DefinitionRef cycle failure reports diagnostics"), CyclicResult.Diagnostics.Num() > 0);
	TestTrue(TEXT("DefinitionRef cycle failure reports code"), HasDiagnosticCode(CyclicResult, TEXT("definitionref-cycle")));
	if (CyclicResult.Diagnostics.Num() > 0)
	{
		TestTrue(TEXT("DefinitionRef cycle path contains definition chain"), CyclicResult.Diagnostics[0].Path.Contains(TEXT("/Definitions/A")) && CyclicResult.Diagnostics[0].Path.Contains(TEXT("/Definitions/B")));
	}

	return true;
}

#endif
