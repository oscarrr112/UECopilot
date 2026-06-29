// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentBodyRegionDispatcher.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentRegionRuntime.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
TSharedPtr<FJsonValue> MakeObjectValue(TSharedRef<FJsonObject> Object)
{
	return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> MakeArrayValue(TArray<TSharedPtr<FJsonValue>> Values)
{
	return MakeShared<FJsonValueArray>(MoveTemp(Values));
}

TSharedRef<FJsonValue> MakeObjectRef(TSharedRef<FJsonObject> Object)
{
	return MakeShared<FJsonValueObject>(Object);
}

TSharedRef<FJsonObject> MakeBodyWithField(const FString& FieldName, const TSharedPtr<FJsonValue>& FieldValue)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(FieldName, FieldValue);
	return Body;
}

FAssetDocumentRegionPolicy MakePolicy(const FName RegionId, const FString& BodyPath)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = RegionId;
	Policy.BodyPath = BodyPath;
	return Policy;
}

FAssetDocumentRegionBinding MakeBinding(
	const FName BodyKey,
	const FName RegionId,
	const FName AdapterName,
	const int32 ApplyOrder = 0,
	const bool bRequired = false)
{
	FAssetDocumentRegionBinding Binding;
	Binding.BodyKey = BodyKey;
	Binding.RegionId = RegionId;
	Binding.AdapterName = AdapterName;
	Binding.ApplyOrder = ApplyOrder;
	Binding.bRequired = bRequired;
	return Binding;
}

struct FTestRegionAdapter : IAssetDocumentRegionAdapter
{
	explicit FTestRegionAdapter(FName InName)
		: Name(InName)
	{
	}

	FName Name;
	bool bSupportsRegion = true;
	bool bApplyChanged = true;
	bool bValidateSucceeds = true;
	bool bApplySucceeds = true;
	TSharedPtr<FJsonValue> CurrentValue = MakeShared<FJsonValueString>(TEXT("Current"));
	TArray<FName>* ApplyLog = nullptr;
	mutable int32 ValidateCalls = 0;
	mutable int32 PreflightCalls = 0;
	mutable int32 ApplyCalls = 0;
	mutable int32 ExtractCalls = 0;
	mutable int32 DiffCalls = 0;
	mutable FString LastJsonPointer;

	FName GetName() const override
	{
		return Name;
	}

	bool SupportsRegion(const FAssetDocumentRegionContext& Context) const override
	{
		LastJsonPointer = Context.JsonPointer;
		return bSupportsRegion;
	}

	TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext&) const override
	{
		return MakeShared<FJsonObject>();
	}

	FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>&) const override
	{
		++ValidateCalls;
		LastJsonPointer = Context.JsonPointer;
		if (!bValidateSucceeds)
		{
			return FAssetDocumentCapabilityResult::Failure(TEXT("validate failed"), Context.JsonPointer, TEXT("AdapterValidateFailed"));
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("validated"));
	}

	FAssetDocumentCapabilityResult PreflightRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override
	{
		++PreflightCalls;
		return IAssetDocumentRegionAdapter::PreflightRegion(Context, DesiredValue);
	}

	FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>&,
		bool& bOutChanged) override
	{
		++ApplyCalls;
		LastJsonPointer = Context.JsonPointer;
		if (ApplyLog)
		{
			ApplyLog->Add(Context.RegionId);
		}
		if (!bApplySucceeds)
		{
			return FAssetDocumentCapabilityResult::Failure(TEXT("apply failed"), Context.JsonPointer, TEXT("AdapterApplyFailed"));
		}
		bOutChanged = bApplyChanged;
		return FAssetDocumentCapabilityResult::Success(TEXT("applied"));
	}

	FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext& Context,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override
	{
		++ExtractCalls;
		LastJsonPointer = Context.JsonPointer;
		OutCurrentValue = CurrentValue;
		return FAssetDocumentCapabilityResult::Success(TEXT("extracted"));
	}

	FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>&,
		TArray<TSharedPtr<FJsonValue>>&) const override
	{
		++DiffCalls;
		LastJsonPointer = Context.JsonPointer;
		return FAssetDocumentCapabilityResult::Success(TEXT("diffed"));
	}
};

FAssetDocumentRegionContext MakeRuntimeContext(
	const FName RegionId = TEXT("Preview"),
	const FString& JsonPointer = TEXT("/Body/Preview"),
	const FAssetDocumentRegionPolicy* Policy = nullptr)
{
	FAssetDocumentRegionContext Context;
	Context.RegionId = RegionId;
	Context.BodyPath = FString::Printf(TEXT("Body.%s"), *RegionId.ToString());
	Context.JsonPointer = JsonPointer;
	Context.Policy = Policy;
	return Context;
}

FAssetDocumentBodyRegionDispatcher MakeDispatcher(
	const TArray<FAssetDocumentRegionBinding>& Bindings,
	const TArray<FAssetDocumentRegionPolicy>& Policies,
	const TArray<IAssetDocumentRegionAdapter*>& Adapters,
	FAssetDocumentBodyRegionDispatcherHooks Hooks = {})
{
	TMap<FName, IAssetDocumentRegionAdapter*> AdapterMap;
	for (IAssetDocumentRegionAdapter* Adapter : Adapters)
	{
		AdapterMap.Add(Adapter->GetName(), Adapter);
	}

	return FAssetDocumentBodyRegionDispatcher(
		Bindings,
		Policies,
		AdapterMap,
		MoveTemp(Hooks));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeJsonUtilsRequireObjectTest,
	"AssetFactory.AssetDocument.RegionRuntime.JsonUtils.RequireObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeJsonUtilsRequireObjectTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> OutObject;
	TSharedRef<FJsonObject> SourceObject = MakeShared<FJsonObject>();
	SourceObject->SetStringField(TEXT("Name"), TEXT("Preview"));

	FAssetDocumentCapabilityResult Result =
		FAssetDocumentJsonRegionUtils::RequireObjectValue(MakeObjectValue(SourceObject), TEXT("/Body/Preview"), OutObject);
	TestTrue(TEXT("Object value succeeds"), Result.bSuccess);
	TestTrue(TEXT("Object value is returned"), OutObject == SourceObject);

	Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(
		MakeShared<FJsonValueString>(TEXT("not-object")),
		TEXT("/Body/Preview"),
		OutObject);
	TestFalse(TEXT("String value fails object requirement"), Result.bSuccess);
	TestEqual(TEXT("Failure reports one diagnostic"), Result.Diagnostics.Num(), 1);
	if (Result.Diagnostics.Num() < 1)
	{
		return false;
	}
	TestEqual(TEXT("Failure path uses supplied JSON pointer"), Result.Diagnostics[0].Path, FString(TEXT("/Body/Preview")));
	TestEqual(TEXT("Failure code marks invalid section type"), Result.Diagnostics[0].Code, FString(TEXT("InvalidBodySectionType")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeJsonUtilsRequireArrayTest,
	"AssetFactory.AssetDocument.RegionRuntime.JsonUtils.RequireArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeJsonUtilsRequireArrayTest::RunTest(const FString& Parameters)
{
	TArray<TSharedPtr<FJsonValue>> OutArray;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireArrayValue(
		MakeArrayValue({MakeShared<FJsonValueString>(TEXT("A"))}),
		TEXT("/Body/Curves"),
		OutArray);
	TestTrue(TEXT("Array value succeeds"), Result.bSuccess);
	TestEqual(TEXT("Array values are copied to caller-owned storage"), OutArray.Num(), 1);
	if (OutArray.Num() < 1)
	{
		return false;
	}
	TestEqual(TEXT("Array value remains safe after temporary input"), OutArray[0]->AsString(), FString(TEXT("A")));

	Result = FAssetDocumentJsonRegionUtils::RequireArrayValue(
		MakeObjectValue(MakeShared<FJsonObject>()),
		TEXT("/Body/Curves"),
		OutArray);
	TestFalse(TEXT("Object value fails array requirement"), Result.bSuccess);
	TestEqual(TEXT("Failure reports one diagnostic"), Result.Diagnostics.Num(), 1);
	if (Result.Diagnostics.Num() < 1)
	{
		return false;
	}
	TestEqual(TEXT("Failure path uses supplied JSON pointer"), Result.Diagnostics[0].Path, FString(TEXT("/Body/Curves")));
	TestEqual(TEXT("Failure message describes array requirement"), Result.Message, FString(TEXT("Expected a JSON array")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeJsonUtilsJsonPointerEscapingTest,
	"AssetFactory.AssetDocument.RegionRuntime.JsonUtils.JsonPointerEscaping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeJsonUtilsJsonPointerEscapingTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Escapes JSON pointer slash and tilde tokens"),
		FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(TEXT("Curve/Name~LOD")),
		FString(TEXT("Curve~1Name~0LOD")));
	TestEqual(
		TEXT("Body path escapes body key token"),
		FAssetDocumentJsonRegionUtils::MakeBodyPath(TEXT("Graph/With~Token")),
		FString(TEXT("/Body/Graph~1With~0Token")));
	TestEqual(
		TEXT("Body array item path appends numeric token"),
		FAssetDocumentJsonRegionUtils::MakeBodyArrayItemPath(TEXT("Curves"), 7),
		FString(TEXT("/Body/Curves/7")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeJsonUtilsDiffEntryShapeTest,
	"AssetFactory.AssetDocument.RegionRuntime.JsonUtils.DiffEntryShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeJsonUtilsDiffEntryShapeTest::RunTest(const FString& Parameters)
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	FAssetDocumentJsonRegionUtils::AddDiffEntry(
		Entries,
		TEXT("/Body/Preview"),
		TEXT("changed"),
		MakeShared<FJsonValueString>(TEXT("Before")),
		MakeShared<FJsonValueString>(TEXT("After")));

	TestEqual(TEXT("One diff entry is added"), Entries.Num(), 1);
	if (Entries.Num() < 1)
	{
		return false;
	}
	TestTrue(TEXT("Entry is a JSON object"), Entries[0].IsValid() && Entries[0]->Type == EJson::Object);
	const TSharedPtr<FJsonObject> EntryObject = Entries[0]->AsObject();
	if (!EntryObject.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("Entry path field"), EntryObject->GetStringField(TEXT("path")), FString(TEXT("/Body/Preview")));
	TestEqual(TEXT("Entry status field"), EntryObject->GetStringField(TEXT("status")), FString(TEXT("changed")));
	TestEqual(TEXT("Entry current field"), EntryObject->GetStringField(TEXT("current")), FString(TEXT("Before")));
	TestEqual(TEXT("Entry desired field"), EntryObject->GetStringField(TEXT("desired")), FString(TEXT("After")));

	TSharedRef<FJsonObject> FirstObject = MakeShared<FJsonObject>();
	FirstObject->SetNumberField(TEXT("B"), 2.0);
	FirstObject->SetNumberField(TEXT("A"), 1.0);
	TSharedRef<FJsonObject> SecondObject = MakeShared<FJsonObject>();
	SecondObject->SetNumberField(TEXT("A"), 1.0);
	SecondObject->SetNumberField(TEXT("B"), 2.0);
	TestEqual(
		TEXT("Comparable JSON ignores object field insertion order"),
		FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeObjectValue(FirstObject)),
		FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeObjectValue(SecondObject)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeJsonUtilsFieldFailureTest,
	"AssetFactory.AssetDocument.RegionRuntime.JsonUtils.FieldFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeJsonUtilsFieldFailureTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Name"), TEXT("   "));
	Object->SetStringField(TEXT("Enabled"), TEXT("true"));

	FString OutString;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireStringField(
		Object,
		TEXT("Name"),
		TEXT("/Body/Variables/0/Name"),
		OutString);
	TestFalse(TEXT("Whitespace-only string is rejected"), Result.bSuccess);
	TestEqual(TEXT("Whitespace string failure diagnostic count"), Result.Diagnostics.Num(), 1);
	if (Result.Diagnostics.Num() < 1)
	{
		return false;
	}
	TestEqual(TEXT("Whitespace string failure path"), Result.Diagnostics[0].Path, FString(TEXT("/Body/Variables/0/Name")));

	double OutNumber = 0.0;
	Result = FAssetDocumentJsonRegionUtils::RequireNumberField(
		Object,
		TEXT("Time"),
		TEXT("/Body/Curves/0/Keys/0/Time"),
		OutNumber);
	TestFalse(TEXT("Missing number is rejected"), Result.bSuccess);
	TestEqual(TEXT("Missing number diagnostic count"), Result.Diagnostics.Num(), 1);
	if (Result.Diagnostics.Num() < 1)
	{
		return false;
	}
	TestEqual(TEXT("Missing number diagnostic code"), Result.Diagnostics[0].Code, FString(TEXT("InvalidNumericField")));

	bool bOutBool = false;
	Result = FAssetDocumentJsonRegionUtils::RequireBoolField(
		Object,
		TEXT("Enabled"),
		TEXT("/Body/Options/Enabled"),
		bOutBool);
	TestFalse(TEXT("Wrong bool type is rejected"), Result.bSuccess);
	TestEqual(TEXT("Wrong bool diagnostic count"), Result.Diagnostics.Num(), 1);
	if (Result.Diagnostics.Num() < 1)
	{
		return false;
	}
	TestEqual(TEXT("Wrong bool diagnostic code"), Result.Diagnostics[0].Code, FString(TEXT("InvalidBooleanField")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeDispatchValidateCallsAdapterTest,
	"AssetFactory.AssetDocument.RegionRuntime.Dispatch.ValidateCallsAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeDispatchValidateCallsAdapterTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Preview"), TEXT("Body.Preview"));
	const FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Preview"), TEXT("/Body/Preview"), &Policy);

	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Validate(
		Context,
		MakeShared<FJsonValueString>(TEXT("Desired")),
		Adapter);

	TestTrue(TEXT("Runtime validate succeeds"), Result.bSuccess);
	TestEqual(TEXT("Adapter validate is called once"), Adapter.ValidateCalls, 1);
	TestEqual(TEXT("Adapter receives JSON pointer"), Adapter.LastJsonPointer, FString(TEXT("/Body/Preview")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeDispatchPreflightUsesValidateDefaultTest,
	"AssetFactory.AssetDocument.RegionRuntime.Dispatch.PreflightUsesValidateDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeDispatchPreflightUsesValidateDefaultTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Preview"), TEXT("Body.Preview"));
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Preview"), TEXT("/Body/Preview"), &Policy);

	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Preflight(
		Context,
		MakeShared<FJsonValueString>(TEXT("Desired")),
		Adapter);

	TestTrue(TEXT("Runtime preflight succeeds"), Result.bSuccess);
	TestEqual(TEXT("Adapter preflight override is called once"), Adapter.PreflightCalls, 1);
	TestEqual(TEXT("Default preflight routes to validate"), Adapter.ValidateCalls, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeDispatchApplyReturnsChangedFlagTest,
	"AssetFactory.AssetDocument.RegionRuntime.Dispatch.ApplyReturnsChangedFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeDispatchApplyReturnsChangedFlagTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	Adapter.bApplyChanged = true;
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Preview"), TEXT("Body.Preview"));
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Preview"), TEXT("/Body/Preview"), &Policy);

	bool bChanged = false;
	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Apply(
		Context,
		MakeShared<FJsonValueString>(TEXT("Desired")),
		Adapter,
		bChanged);

	TestTrue(TEXT("Runtime apply succeeds"), Result.bSuccess);
	TestTrue(TEXT("Changed flag is returned from adapter"), bChanged);
	TestEqual(TEXT("Adapter apply is called once"), Adapter.ApplyCalls, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeDispatchDiffAllowsAdapterHandledEmptyResultTest,
	"AssetFactory.AssetDocument.RegionRuntime.Dispatch.DiffAllowsAdapterHandledEmptyResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeDispatchDiffAllowsAdapterHandledEmptyResultTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	Adapter.CurrentValue = MakeShared<FJsonValueString>(TEXT("Current"));
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Display/Name"), TEXT("Body.Display/Name"));
	const FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Display/Name"), TEXT("/Body/Display~1Name"), &Policy);

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Diff(
		Context,
		MakeShared<FJsonValueString>(TEXT("Desired")),
		Adapter,
		DiffEntries);

	TestTrue(TEXT("Runtime diff succeeds"), Result.bSuccess);
	TestEqual(TEXT("Adapter-handled empty diff stays empty"), DiffEntries.Num(), 0);
	TestEqual(TEXT("Runtime does not infer fallback extraction"), Adapter.ExtractCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeDispatchDiffEntryUsesJsonPointerTest,
	"AssetFactory.AssetDocument.RegionRuntime.Dispatch.DiffEntryUsesJsonPointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeDispatchDiffEntryUsesJsonPointerTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	Adapter.CurrentValue = MakeShared<FJsonValueString>(TEXT("Current"));
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Display/Name"), TEXT("Body.Display/Name"));
	const FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Display/Name"), TEXT("/Body/Display~1Name"), &Policy);

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::DiffByCanonicalExtract(
		Context,
		MakeShared<FJsonValueString>(TEXT("Desired")),
		Adapter,
		DiffEntries);

	TestTrue(TEXT("Default runtime diff succeeds"), Result.bSuccess);
	TestEqual(TEXT("Default diff emits one changed entry"), DiffEntries.Num(), 1);
	if (DiffEntries.Num() < 1 || !DiffEntries[0].IsValid() || DiffEntries[0]->Type != EJson::Object)
	{
		return false;
	}
	TestEqual(TEXT("Diff entry path uses region JSON pointer"), DiffEntries[0]->AsObject()->GetStringField(TEXT("path")), FString(TEXT("/Body/Display~1Name")));
	TestEqual(TEXT("Default diff does not call adapter semantic diff hook"), Adapter.DiffCalls, 0);
	TestEqual(TEXT("Runtime extracts current value for default diff"), Adapter.ExtractCalls, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeExplicitEmptyRejectsUnexpectedNullTest,
	"AssetFactory.AssetDocument.RegionRuntime.ExplicitEmpty.RejectsUnexpectedNull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeExplicitEmptyRejectsUnexpectedNullTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Preview"), TEXT("Body.Preview"));
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Preview"), TEXT("/Body/Preview"), &Policy);

	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Validate(
		Context,
		MakeShared<FJsonValueNull>(),
		Adapter);

	TestFalse(TEXT("Unexpected null fails before adapter validation"), Result.bSuccess);
	TestEqual(TEXT("Adapter validate is not called"), Adapter.ValidateCalls, 0);
	TestEqual(TEXT("Unexpected null diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/Preview")));
	TestEqual(TEXT("Unexpected null diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("UnexpectedNullBodySection")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherRejectsNonObjectBodyTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsNonObjectBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRejectsNonObjectBodyTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"))},
		{MakePolicy(TEXT("Preview"), TEXT("Body.Preview"))},
		{&Adapter});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(Context, MakeShared<FJsonValueString>(TEXT("not-object")));

	TestFalse(TEXT("Non-object Body is rejected"), Result.bSuccess);
	TestEqual(TEXT("Diagnostic points at Body"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherRejectsUnknownBodyKeyTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsUnknownBodyKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRejectsUnknownBodyKeyTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"))},
		{MakePolicy(TEXT("Preview"), TEXT("Body.Preview"))},
		{&Adapter});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("Unknown"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()))));

	TestFalse(TEXT("Unknown Body key is rejected"), Result.bSuccess);
	TestEqual(TEXT("Unknown key diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/Unknown")));
	TestEqual(TEXT("Unknown key diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("UnknownBodyRegion")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherRequiresConfiguredRegionTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RequiresConfiguredRegion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRequiresConfiguredRegionTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"), 0, true)},
		{MakePolicy(TEXT("Preview"), TEXT("Body.Preview"))},
		{&Adapter});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(Context, MakeObjectRef(MakeShared<FJsonObject>()));

	TestFalse(TEXT("Missing required region is rejected"), Result.bSuccess);
	TestEqual(TEXT("Required region diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/Preview")));
	TestEqual(TEXT("Required region diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("MissingRequiredBodyRegion")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherRejectsDuplicateBodyKeyConfigTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsDuplicateBodyKeyConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRejectsDuplicateBodyKeyConfigTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{
			MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake")),
			MakeBinding(TEXT("Preview"), TEXT("PreviewCopy"), TEXT("Fake")),
		},
		{
			MakePolicy(TEXT("Preview"), TEXT("Body.Preview")),
			MakePolicy(TEXT("PreviewCopy"), TEXT("Body.Preview")),
		},
		{&Adapter});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("Preview"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()))));

	TestFalse(TEXT("Duplicate BodyKey config is rejected before dispatch"), Result.bSuccess);
	TestEqual(TEXT("Duplicate BodyKey diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidRegionDispatcherConfig")));
	TestEqual(TEXT("Adapter validate is not called"), Adapter.ValidateCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherRejectsDuplicateBindingRegionIdConfigTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsDuplicateBindingRegionIdConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRejectsDuplicateBindingRegionIdConfigTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter FirstAdapter(TEXT("FirstAdapter"));
	FTestRegionAdapter SecondAdapter(TEXT("SecondAdapter"));
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{
			MakeBinding(TEXT("First"), TEXT("SharedRegion"), TEXT("FirstAdapter")),
			MakeBinding(TEXT("Second"), TEXT("SharedRegion"), TEXT("SecondAdapter")),
		},
		{MakePolicy(TEXT("SharedRegion"), TEXT("Body.First"))},
		{&FirstAdapter, &SecondAdapter});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("First"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()))));

	TestFalse(TEXT("Duplicate binding RegionId config is rejected before dispatch"), Result.bSuccess);
	TestEqual(TEXT("Duplicate binding RegionId diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidRegionDispatcherConfig")));
	TestEqual(TEXT("First adapter validate is not called"), FirstAdapter.ValidateCalls, 0);
	TestEqual(TEXT("Second adapter validate is not called"), SecondAdapter.ValidateCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherRejectsDuplicatePolicyRegionIdConfigTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsDuplicatePolicyRegionIdConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRejectsDuplicatePolicyRegionIdConfigTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"))},
		{
			MakePolicy(TEXT("Preview"), TEXT("Body.Preview")),
			MakePolicy(TEXT("Preview"), TEXT("Body.PreviewCopy")),
		},
		{&Adapter});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("Preview"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()))));

	TestFalse(TEXT("Duplicate policy RegionId config is rejected before dispatch"), Result.bSuccess);
	TestEqual(TEXT("Duplicate policy RegionId diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidRegionDispatcherConfig")));
	TestEqual(TEXT("Adapter validate is not called"), Adapter.ValidateCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherRejectsEmptyAdapterNameConfigTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsEmptyAdapterNameConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRejectsEmptyAdapterNameConfigTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), NAME_None)},
		{MakePolicy(TEXT("Preview"), TEXT("Body.Preview"))},
		{&Adapter});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("Preview"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()))));

	TestFalse(TEXT("Empty adapter name config is rejected before dispatch"), Result.bSuccess);
	TestEqual(TEXT("Empty adapter diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidRegionDispatcherConfig")));
	TestEqual(TEXT("Adapter validate is not called"), Adapter.ValidateCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherRejectsMissingAdapterConfigTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsMissingAdapterConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRejectsMissingAdapterConfigTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"))},
		{MakePolicy(TEXT("Preview"), TEXT("Body.Preview"))},
		{});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("Preview"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()))));

	TestFalse(TEXT("Missing adapter config is rejected"), Result.bSuccess);
	TestEqual(TEXT("Missing adapter diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidRegionDispatcherConfig")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherRejectsMissingPolicyConfigTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsMissingPolicyConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRejectsMissingPolicyConfigTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"))},
		{},
		{&Adapter});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("Preview"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()))));

	TestFalse(TEXT("Missing policy config is rejected"), Result.bSuccess);
	TestEqual(TEXT("Missing policy diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidRegionDispatcherConfig")));
	TestEqual(TEXT("Adapter validate is not called"), Adapter.ValidateCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherRejectsUnsupportedAdapterTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsUnsupportedAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRejectsUnsupportedAdapterTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	Adapter.bSupportsRegion = false;
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"))},
		{MakePolicy(TEXT("Preview"), TEXT("Body.Preview"))},
		{&Adapter});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(Context, MakeObjectRef(MakeShared<FJsonObject>()));

	TestFalse(TEXT("Unsupported optional adapter is rejected as invalid config"), Result.bSuccess);
	TestEqual(TEXT("Unsupported adapter diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidRegionDispatcherConfig")));
	TestEqual(TEXT("Adapter validate is not called after config failure"), Adapter.ValidateCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherAppliesInConfiguredOrderTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.AppliesInConfiguredOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherAppliesInConfiguredOrderTest::RunTest(const FString& Parameters)
{
	TArray<FName> ApplyLog;
	FTestRegionAdapter FirstAdapter(TEXT("FirstAdapter"));
	FTestRegionAdapter SecondAdapter(TEXT("SecondAdapter"));
	FirstAdapter.ApplyLog = &ApplyLog;
	SecondAdapter.ApplyLog = &ApplyLog;

	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{
			MakeBinding(TEXT("Second"), TEXT("SecondRegion"), TEXT("SecondAdapter"), 20),
			MakeBinding(TEXT("First"), TEXT("FirstRegion"), TEXT("FirstAdapter"), 10),
		},
		{
			MakePolicy(TEXT("SecondRegion"), TEXT("Body.Second")),
			MakePolicy(TEXT("FirstRegion"), TEXT("Body.First")),
		},
		{&FirstAdapter, &SecondAdapter});

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("Second"), TEXT("B"));
	Body->SetStringField(TEXT("First"), TEXT("A"));

	FAssetDocumentCapabilityContext Context;
	TSet<FName> AppliedRegions;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ApplyBody(Context, MakeObjectRef(Body), AppliedRegions);

	TestTrue(TEXT("Apply succeeds"), Result.bSuccess);
	TestEqual(TEXT("Two regions are applied"), ApplyLog.Num(), 2);
	if (ApplyLog.Num() < 2)
	{
		return false;
	}
	TestEqual(TEXT("First configured order applies first"), ApplyLog[0], FName(TEXT("FirstRegion")));
	TestEqual(TEXT("Second configured order applies second"), ApplyLog[1], FName(TEXT("SecondRegion")));
	TestTrue(TEXT("Applied region set contains FirstRegion"), AppliedRegions.Contains(TEXT("FirstRegion")));
	TestTrue(TEXT("Applied region set contains SecondRegion"), AppliedRegions.Contains(TEXT("SecondRegion")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherCallsCrossRegionHookTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.CallsCrossRegionHook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherCallsCrossRegionHookTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	int32 CrossRegionCalls = 0;
	FAssetDocumentBodyRegionDispatcherHooks Hooks;
	Hooks.ValidateCrossRegion = [&CrossRegionCalls](const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>& BodyObject)
	{
		++CrossRegionCalls;
		return BodyObject->HasField(TEXT("Preview"))
			? FAssetDocumentCapabilityResult::Success(TEXT("cross-region ok"))
			: FAssetDocumentCapabilityResult::Failure(TEXT("missing preview"), TEXT("/Body"), TEXT("CrossRegionFailure"));
	};

	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"))},
		{MakePolicy(TEXT("Preview"), TEXT("Body.Preview"))},
		{&Adapter},
		MoveTemp(Hooks));

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("Preview"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()))));

	TestTrue(TEXT("Validate succeeds"), Result.bSuccess);
	TestEqual(TEXT("Cross-region hook is called once"), CrossRegionCalls, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherDiffValidatesRegionBeforeCrossRegionHookTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.DiffValidatesRegionBeforeCrossRegionHook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherDiffValidatesRegionBeforeCrossRegionHookTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	int32 CrossRegionCalls = 0;
	FAssetDocumentBodyRegionDispatcherHooks Hooks;
	Hooks.ValidateCrossRegion = [&CrossRegionCalls](const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>&)
	{
		++CrossRegionCalls;
		return FAssetDocumentCapabilityResult::Success(TEXT("cross-region ok"));
	};

	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"))},
		{MakePolicy(TEXT("Preview"), TEXT("Body.Preview"))},
		{&Adapter},
		MoveTemp(Hooks));

	const FAssetDocumentCapabilityContext Context;
	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult Result = Dispatcher.DiffBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("Preview"), MakeShared<FJsonValueNull>())),
		DiffEntries);

	TestFalse(TEXT("Invalid region fails diff"), Result.bSuccess);
	TestEqual(TEXT("Cross-region hook is not called before region validation"), CrossRegionCalls, 0);
	TestEqual(TEXT("Unexpected null diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("UnexpectedNullBodySection")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherValidateStopsAfterAdapterFailureTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.ValidateStopsAfterAdapterFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherValidateStopsAfterAdapterFailureTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter FirstAdapter(TEXT("FirstAdapter"));
	FTestRegionAdapter SecondAdapter(TEXT("SecondAdapter"));
	FirstAdapter.bValidateSucceeds = false;
	int32 CrossRegionCalls = 0;
	FAssetDocumentBodyRegionDispatcherHooks Hooks;
	Hooks.ValidateCrossRegion = [&CrossRegionCalls](const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>&)
	{
		++CrossRegionCalls;
		return FAssetDocumentCapabilityResult::Success(TEXT("cross-region ok"));
	};

	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{
			MakeBinding(TEXT("First"), TEXT("FirstRegion"), TEXT("FirstAdapter"), 10),
			MakeBinding(TEXT("Second"), TEXT("SecondRegion"), TEXT("SecondAdapter"), 20),
		},
		{
			MakePolicy(TEXT("FirstRegion"), TEXT("Body.First")),
			MakePolicy(TEXT("SecondRegion"), TEXT("Body.Second")),
		},
		{&FirstAdapter, &SecondAdapter},
		MoveTemp(Hooks));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("First"), TEXT("A"));
	Body->SetStringField(TEXT("Second"), TEXT("B"));

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(Context, MakeObjectRef(Body));

	TestFalse(TEXT("Adapter validate failure propagates"), Result.bSuccess);
	TestEqual(TEXT("First adapter validate is called once"), FirstAdapter.ValidateCalls, 1);
	TestEqual(TEXT("Second adapter is not called after failure"), SecondAdapter.ValidateCalls, 0);
	TestEqual(TEXT("Cross-region hook is not called after failure"), CrossRegionCalls, 0);
	TestEqual(TEXT("Adapter failure diagnostic code propagates"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("AdapterValidateFailed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherApplyStopsAfterAdapterFailureTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.ApplyStopsAfterAdapterFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherApplyStopsAfterAdapterFailureTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter FirstAdapter(TEXT("FirstAdapter"));
	FTestRegionAdapter SecondAdapter(TEXT("SecondAdapter"));
	FirstAdapter.bApplySucceeds = false;
	int32 RepairCalls = 0;
	FAssetDocumentBodyRegionDispatcherHooks Hooks;
	Hooks.PostApplyRepair = [&RepairCalls](FAssetDocumentCapabilityContext&, const TSet<FName>&)
	{
		++RepairCalls;
		return FAssetDocumentCapabilityResult::Success(TEXT("repair ok"));
	};

	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{
			MakeBinding(TEXT("First"), TEXT("FirstRegion"), TEXT("FirstAdapter"), 10),
			MakeBinding(TEXT("Second"), TEXT("SecondRegion"), TEXT("SecondAdapter"), 20),
		},
		{
			MakePolicy(TEXT("FirstRegion"), TEXT("Body.First")),
			MakePolicy(TEXT("SecondRegion"), TEXT("Body.Second")),
		},
		{&FirstAdapter, &SecondAdapter},
		MoveTemp(Hooks));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("First"), TEXT("A"));
	Body->SetStringField(TEXT("Second"), TEXT("B"));

	FAssetDocumentCapabilityContext Context;
	TSet<FName> AppliedRegions;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ApplyBody(Context, MakeObjectRef(Body), AppliedRegions);

	TestFalse(TEXT("Adapter apply failure propagates"), Result.bSuccess);
	TestEqual(TEXT("First adapter apply is called once"), FirstAdapter.ApplyCalls, 1);
	TestEqual(TEXT("Second adapter is not called after failure"), SecondAdapter.ApplyCalls, 0);
	TestEqual(TEXT("Repair hook is not called after failure"), RepairCalls, 0);
	TestEqual(TEXT("Adapter failure diagnostic code propagates"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("AdapterApplyFailed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeBodyDispatcherCallsPostApplyRepairHookTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.CallsPostApplyRepairHook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherCallsPostApplyRepairHookTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	int32 RepairCalls = 0;
	TSet<FName> RepairRegions;
	FAssetDocumentBodyRegionDispatcherHooks Hooks;
	Hooks.PostApplyRepair = [&RepairCalls, &RepairRegions](FAssetDocumentCapabilityContext&, const TSet<FName>& AppliedRegions)
	{
		++RepairCalls;
		RepairRegions = AppliedRegions;
		return FAssetDocumentCapabilityResult::Success(TEXT("repair ok"));
	};

	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"))},
		{MakePolicy(TEXT("Preview"), TEXT("Body.Preview"))},
		{&Adapter},
		MoveTemp(Hooks));

	FAssetDocumentCapabilityContext Context;
	TSet<FName> AppliedRegions;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ApplyBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("Preview"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()))),
		AppliedRegions);

	TestTrue(TEXT("Apply succeeds"), Result.bSuccess);
	TestEqual(TEXT("Repair hook is called once"), RepairCalls, 1);
	TestTrue(TEXT("Repair hook receives applied region"), RepairRegions.Contains(TEXT("Preview")));
	return true;
}

#endif
