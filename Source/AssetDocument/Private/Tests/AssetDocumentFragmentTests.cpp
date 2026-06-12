// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFragment.h"

#include "AssetDocumentFragmentCompiler.h"

#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

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
		return FAssetDocumentFragmentResult::Success();
	}
};
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
	ExtractContext.JsonPath = TEXT("/Body/Test");
	ExtractContext.Role = TEXT("TestKind");

	TSharedRef<FJsonObject> ExtractedFragment = MakeShared<FJsonObject>();
	const FAssetDocumentFragmentResult ExtractResult = Compiler.Extract(ExtractContext, ExtractedFragment);
	TestTrue(TEXT("Known fragment role extracts"), ExtractResult.bSuccess);
	TestEqual(TEXT("Extract writes fragment Kind"), ExtractedFragment->GetStringField(TEXT("Kind")), FString(TEXT("TestKind")));

	return true;
}

#endif
