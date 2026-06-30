// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentBodyRegionDispatcher.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentRegionRuntime.h"
#include "Regions/AssetDocumentDeferredRegionAdapter.h"
#include "Regions/AssetDocumentNamedArrayRegionAdapter.h"
#include "Regions/AssetDocumentObjectFieldSchemaUtils.h"
#include "Regions/AssetDocumentObjectRegionAdapter.h"
#include "Regions/AssetDocumentWidgetBlueprintRegionWrappers.h"

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

FAssetDocumentRegionPolicy MakeDeferredPolicy(
	const FName RegionId,
	const FString& BodyPath,
	const EAssetDocumentRegionKind RegionKind)
{
	FAssetDocumentRegionPolicy Policy = MakePolicy(RegionId, BodyPath);
	Policy.RegionKind = RegionKind;
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
	FAssetDocumentRegionRuntimeWidgetWrapperDelegatesValidateTest,
	"AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper.DelegatesValidate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeWidgetWrapperDelegatesValidateTest::RunTest(const FString& Parameters)
{
	int32 ValidateCalls = 0;
	FString SeenPointer;
	TSharedPtr<FJsonValue> SeenDesired;
	FWidgetBlueprintRegionAdapterHooks Hooks;
	Hooks.Validate = [&ValidateCalls, &SeenPointer, &SeenDesired](
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue)
	{
		++ValidateCalls;
		SeenPointer = Context.JsonPointer;
		SeenDesired = DesiredValue;
		return FAssetDocumentCapabilityResult::Success(TEXT("validated widget region"));
	};
	FWidgetBlueprintTreeRegionAdapter Adapter(MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.WidgetTree"), TEXT("Body.WidgetTree"));
	const FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.WidgetTree"), TEXT("/Body/WidgetTree"), &Policy);
	const TSharedPtr<FJsonValue> Desired = MakeObjectValue(MakeShared<FJsonObject>());

	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Validate(Context, Desired, Adapter);

	TestTrue(TEXT("Validate succeeds through wrapper"), Result.bSuccess);
	TestEqual(TEXT("Validate hook is called once"), ValidateCalls, 1);
	TestEqual(TEXT("Validate receives region pointer"), SeenPointer, FString(TEXT("/Body/WidgetTree")));
	TestTrue(TEXT("Validate receives desired value"), SeenDesired == Desired);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeWidgetWrapperDelegatesPreflightTest,
	"AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper.DelegatesPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeWidgetWrapperDelegatesPreflightTest::RunTest(const FString& Parameters)
{
	int32 PreflightCalls = 0;
	UObject* SentinelAsset = reinterpret_cast<UObject*>(0x1234);
	FWidgetBlueprintRegionAdapterHooks Hooks;
	Hooks.Preflight = [&PreflightCalls, &SentinelAsset](
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>&)
	{
		++PreflightCalls;
		return Context.Asset == SentinelAsset
			? FAssetDocumentCapabilityResult::Success(TEXT("preflighted widget region"))
			: FAssetDocumentCapabilityResult::Failure(TEXT("missing context asset"), Context.JsonPointer, TEXT("MissingContextAsset"));
	};
	FWidgetBlueprintBindingRegionAdapter Adapter(MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.Bindings"), TEXT("Body.Bindings"));
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.Bindings"), TEXT("/Body/Bindings"), &Policy);
	Context.Asset = SentinelAsset;

	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Preflight(
		Context,
		MakeArrayValue({}),
		Adapter);

	TestTrue(TEXT("Preflight succeeds through wrapper"), Result.bSuccess);
	TestEqual(TEXT("Preflight hook is called once"), PreflightCalls, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeWidgetWrapperDelegatesExtractTest,
	"AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper.DelegatesExtract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeWidgetWrapperDelegatesExtractTest::RunTest(const FString& Parameters)
{
	int32 ExtractCalls = 0;
	FWidgetBlueprintRegionAdapterHooks Hooks;
	Hooks.Extract = [&ExtractCalls](
		const FAssetDocumentRegionContext&,
		TSharedPtr<FJsonValue>& OutCurrentValue)
	{
		++ExtractCalls;
		OutCurrentValue = MakeArrayValue({MakeShared<FJsonValueString>(TEXT("Animation"))});
		return FAssetDocumentCapabilityResult::Success(TEXT("extracted widget region"));
	};
	FWidgetBlueprintAnimationRegionAdapter Adapter(MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.Animations"), TEXT("Body.Animations"));
	const FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.Animations"), TEXT("/Body/Animations"), &Policy);

	TSharedPtr<FJsonValue> Extracted;
	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Extract(Context, Adapter, Extracted);

	TestTrue(TEXT("Extract succeeds through wrapper"), Result.bSuccess);
	TestEqual(TEXT("Extract hook is called once"), ExtractCalls, 1);
	TestTrue(TEXT("Extract returns hook value"), Extracted.IsValid() && Extracted->Type == EJson::Array);
	TestEqual(TEXT("Extracted array contains hook value"), Extracted.IsValid() ? Extracted->AsArray().Num() : 0, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeWidgetWrapperDelegatesDiffTest,
	"AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper.DelegatesDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeWidgetWrapperDelegatesDiffTest::RunTest(const FString& Parameters)
{
	int32 DiffCalls = 0;
	FWidgetBlueprintRegionAdapterHooks Hooks;
	Hooks.Diff = [&DiffCalls](
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		++DiffCalls;
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			Context.JsonPointer,
			TEXT("changed"),
			MakeShared<FJsonValueNull>(),
			DesiredValue);
		return FAssetDocumentCapabilityResult::Success(TEXT("diffed widget graph region"));
	};
	FWidgetBlueprintGraphRegionAdapter Adapter(MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.WidgetBlueprintGraphRegions"), TEXT("Body.WidgetBlueprintGraphRegions"));
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.WidgetBlueprintGraphRegions"), TEXT("/Body"), &Policy);
	Context.BodyPath = TEXT("Body.WidgetBlueprintGraphRegions");

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Diff(
		Context,
		MakeObjectValue(MakeShared<FJsonObject>()),
		Adapter,
		DiffEntries);

	TestTrue(TEXT("Diff succeeds through wrapper"), Result.bSuccess);
	TestEqual(TEXT("Diff hook is called once"), DiffCalls, 1);
	TestEqual(TEXT("Diff entry is produced by hook"), DiffEntries.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeWidgetWrapperRejectsNonSyntheticGraphRegionTest,
	"AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper.RejectsNonSyntheticGraphRegion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeWidgetWrapperRejectsNonSyntheticGraphRegionTest::RunTest(const FString& Parameters)
{
	FWidgetBlueprintGraphRegionAdapter Adapter;

	const FAssetDocumentRegionPolicy WrongBodyPolicy = MakePolicy(TEXT("Body.WidgetBlueprintGraphs"), TEXT("Body.WidgetBlueprintGraphs"));
	const FAssetDocumentRegionContext WrongBodyContext = MakeRuntimeContext(TEXT("Body.WidgetBlueprintGraphs"), TEXT("/Body"), &WrongBodyPolicy);
	TestFalse(TEXT("Graph wrapper rejects wrong body-level region id"), Adapter.SupportsRegion(WrongBodyContext));

	const TArray<FString> SingleGraphKeys = {
		TEXT("UbergraphPages"),
		TEXT("FunctionGraphs"),
		TEXT("MacroGraphs"),
	};
	for (const FString& SingleGraphKey : SingleGraphKeys)
	{
		const FString RegionId = FString::Printf(TEXT("Body.%s"), *SingleGraphKey);
		const FAssetDocumentRegionPolicy SingleGraphKeyPolicy = MakePolicy(*RegionId, *RegionId);
		const FAssetDocumentRegionContext SingleGraphKeyContext = MakeRuntimeContext(
			*RegionId,
			*FString::Printf(TEXT("/Body/%s"), *SingleGraphKey),
			&SingleGraphKeyPolicy);
		TestFalse(
			FString::Printf(TEXT("Graph wrapper rejects individual graph body key %s"), *SingleGraphKey),
			Adapter.SupportsRegion(SingleGraphKeyContext));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeWidgetWrapperBodyLevelGraphLifecycleTest,
	"AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper.BodyLevelGraphLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeWidgetWrapperBodyLevelGraphLifecycleTest::RunTest(const FString& Parameters)
{
	int32 ValidateCalls = 0;
	int32 PreflightCalls = 0;
	int32 ApplyCalls = 0;
	int32 ExtractCalls = 0;
	int32 DiffCalls = 0;
	FWidgetBlueprintRegionAdapterHooks Hooks;
	Hooks.Validate = [&ValidateCalls](const FAssetDocumentRegionContext& Context, const TSharedPtr<FJsonValue>& DesiredValue)
	{
		++ValidateCalls;
		return Context.JsonPointer == TEXT("/Body") && DesiredValue.IsValid() && DesiredValue->Type == EJson::Object
			? FAssetDocumentCapabilityResult::Success(TEXT("validated body-level graph wrapper"))
			: FAssetDocumentCapabilityResult::Failure(TEXT("graph wrapper requires body object context"), Context.JsonPointer, TEXT("InvalidGraphWrapperContext"));
	};
	Hooks.Preflight = [&PreflightCalls](FAssetDocumentRegionContext& Context, const TSharedPtr<FJsonValue>& DesiredValue)
	{
		++PreflightCalls;
		return Context.RegionId == TEXT("Body.WidgetBlueprintGraphRegions") && DesiredValue.IsValid() && DesiredValue->Type == EJson::Object
			? FAssetDocumentCapabilityResult::Success(TEXT("preflighted body-level graph wrapper"))
			: FAssetDocumentCapabilityResult::Failure(TEXT("graph wrapper requires synthetic region id"), Context.JsonPointer, TEXT("InvalidGraphWrapperContext"));
	};
	Hooks.Apply = [&ApplyCalls](
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged)
	{
		++ApplyCalls;
		bOutChanged = true;
		return Context.BodyPath == TEXT("Body.WidgetBlueprintGraphRegions") && DesiredValue.IsValid() && DesiredValue->Type == EJson::Object
			? FAssetDocumentCapabilityResult::Success(TEXT("applied body-level graph wrapper"))
			: FAssetDocumentCapabilityResult::Failure(TEXT("graph wrapper requires synthetic body path"), Context.JsonPointer, TEXT("InvalidGraphWrapperContext"));
	};
	Hooks.Extract = [&ExtractCalls](const FAssetDocumentRegionContext& Context, TSharedPtr<FJsonValue>& OutCurrentValue)
	{
		++ExtractCalls;
		TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
		BodyObject->SetArrayField(TEXT("UbergraphPages"), {});
		BodyObject->SetArrayField(TEXT("FunctionGraphs"), {});
		BodyObject->SetArrayField(TEXT("MacroGraphs"), {});
		OutCurrentValue = MakeShared<FJsonValueObject>(BodyObject);
		return Context.JsonPointer == TEXT("/Body")
			? FAssetDocumentCapabilityResult::Success(TEXT("extracted body-level graph wrapper"))
			: FAssetDocumentCapabilityResult::Failure(TEXT("graph wrapper extracts at body pointer"), Context.JsonPointer, TEXT("InvalidGraphWrapperContext"));
	};
	Hooks.Diff = [&DiffCalls](
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		++DiffCalls;
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			TEXT("/Body/UbergraphPages"),
			TEXT("changed"),
			MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>()),
			DesiredValue);
		return Context.JsonPointer == TEXT("/Body")
			? FAssetDocumentCapabilityResult::Success(TEXT("diffed body-level graph wrapper"))
			: FAssetDocumentCapabilityResult::Failure(TEXT("graph wrapper diffs at body pointer"), Context.JsonPointer, TEXT("InvalidGraphWrapperContext"));
	};

	FWidgetBlueprintGraphRegionAdapter Adapter(MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.WidgetBlueprintGraphRegions"), TEXT("Body.WidgetBlueprintGraphRegions"));
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.WidgetBlueprintGraphRegions"), TEXT("/Body"), &Policy);
	Context.BodyPath = TEXT("Body.WidgetBlueprintGraphRegions");
	TSharedRef<FJsonObject> DesiredBody = MakeShared<FJsonObject>();
	DesiredBody->SetArrayField(TEXT("UbergraphPages"), {});
	const TSharedPtr<FJsonValue> DesiredValue = MakeObjectValue(DesiredBody);

	TestTrue(TEXT("Graph wrapper supports synthetic body-level region"), Adapter.SupportsRegion(Context));
	TestTrue(TEXT("Validate dispatches body-level graph wrapper"), FAssetDocumentRegionRuntime::Validate(Context, DesiredValue, Adapter).bSuccess);
	TestTrue(TEXT("Preflight dispatches body-level graph wrapper"), FAssetDocumentRegionRuntime::Preflight(Context, DesiredValue, Adapter).bSuccess);
	bool bChanged = false;
	TestTrue(TEXT("Apply dispatches body-level graph wrapper"), FAssetDocumentRegionRuntime::Apply(Context, DesiredValue, Adapter, bChanged).bSuccess);
	TestTrue(TEXT("Apply returns graph wrapper changed flag"), bChanged);
	TSharedPtr<FJsonValue> ExtractedValue;
	TestTrue(TEXT("Extract dispatches body-level graph wrapper"), FAssetDocumentRegionRuntime::Extract(Context, Adapter, ExtractedValue).bSuccess);
	TestTrue(TEXT("Extract keeps graph body object shape"), ExtractedValue.IsValid() && ExtractedValue->Type == EJson::Object);
	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	TestTrue(TEXT("Diff dispatches body-level graph wrapper"), FAssetDocumentRegionRuntime::Diff(Context, DesiredValue, Adapter, DiffEntries).bSuccess);
	TestEqual(TEXT("Validate hook count"), ValidateCalls, 1);
	TestEqual(TEXT("Preflight hook count"), PreflightCalls, 1);
	TestEqual(TEXT("Apply hook count"), ApplyCalls, 1);
	TestEqual(TEXT("Extract hook count"), ExtractCalls, 1);
	TestEqual(TEXT("Diff hook count"), DiffCalls, 1);
	TestEqual(TEXT("Diff entries from graph wrapper"), DiffEntries.Num(), 1);
	return true;
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
	FAssetDocumentRegionRuntimeDeferredRegionAcceptsEmptyArrayTest,
	"AssetFactory.AssetDocument.RegionRuntime.DeferredRegion.AcceptsEmptyArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeDeferredRegionAcceptsEmptyArrayTest::RunTest(const FString& Parameters)
{
	FAssetDocumentDeferredRegionAdapter Adapter;
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("Body.FunctionGraphs"), TEXT("Body.FunctionGraphs"), EAssetDocumentRegionKind::Graph);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("Body.FunctionGraphs"), TEXT("/Body/FunctionGraphs"), &Policy);

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Validate(Context, MakeArrayValue({}), Adapter);

	TestTrue(TEXT("Deferred empty array succeeds"), Result.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeDeferredRegionAcceptsEmptyObjectTest,
	"AssetFactory.AssetDocument.RegionRuntime.DeferredRegion.AcceptsEmptyObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeDeferredRegionAcceptsEmptyObjectTest::RunTest(const FString& Parameters)
{
	FAssetDocumentDeferredRegionAdapter Adapter;
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("Body.Preview"), TEXT("Body.Preview"), EAssetDocumentRegionKind::Object);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("Body.Preview"), TEXT("/Body/Preview"), &Policy);

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Validate(Context, MakeObjectValue(MakeShared<FJsonObject>()), Adapter);

	TestTrue(TEXT("Deferred empty object succeeds"), Result.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeDeferredRegionRejectsNonEmptyArrayTest,
	"AssetFactory.AssetDocument.RegionRuntime.DeferredRegion.RejectsNonEmptyArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeDeferredRegionRejectsNonEmptyArrayTest::RunTest(const FString& Parameters)
{
	FAssetDocumentDeferredRegionAdapter Adapter;
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("Body.FunctionGraphs"), TEXT("Body.FunctionGraphs"), EAssetDocumentRegionKind::Graph);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("Body.FunctionGraphs"), TEXT("/Body/FunctionGraphs"), &Policy);

	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Validate(
		Context,
		MakeArrayValue({MakeShared<FJsonValueString>(TEXT("Graph"))}),
		Adapter);

	TestFalse(TEXT("Deferred non-empty array fails"), Result.bSuccess);
	TestEqual(
		TEXT("Non-empty array reports unsupported deferred region"),
		Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(),
		FString(TEXT("UnsupportedRegion")));
	TestEqual(
		TEXT("Non-empty array diagnostic uses region path"),
		Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(),
		FString(TEXT("/Body/FunctionGraphs")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeDeferredRegionRejectsNonEmptyObjectTest,
	"AssetFactory.AssetDocument.RegionRuntime.DeferredRegion.RejectsNonEmptyObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeDeferredRegionRejectsNonEmptyObjectTest::RunTest(const FString& Parameters)
{
	FAssetDocumentDeferredRegionAdapter Adapter;
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("Body.Preview"), TEXT("Body.Preview"), EAssetDocumentRegionKind::Object);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("Body.Preview"), TEXT("/Body/Preview"), &Policy);
	TSharedRef<FJsonObject> NonEmptyObject = MakeShared<FJsonObject>();
	NonEmptyObject->SetStringField(TEXT("Name"), TEXT("Preview"));

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Validate(Context, MakeObjectValue(NonEmptyObject), Adapter);

	TestFalse(TEXT("Deferred non-empty object fails"), Result.bSuccess);
	TestEqual(
		TEXT("Non-empty object reports unsupported deferred region"),
		Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(),
		FString(TEXT("UnsupportedRegion")));
	TestEqual(
		TEXT("Non-empty object diagnostic uses region path"),
		Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(),
		FString(TEXT("/Body/Preview")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeDeferredRegionExtractsDeclaredDefaultTest,
	"AssetFactory.AssetDocument.RegionRuntime.DeferredRegion.ExtractsDeclaredDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeDeferredRegionExtractsDeclaredDefaultTest::RunTest(const FString& Parameters)
{
	FAssetDocumentDeferredRegionAdapter Adapter;
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("Body.FunctionGraphs"), TEXT("Body.FunctionGraphs"), EAssetDocumentRegionKind::Graph);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("Body.FunctionGraphs"), TEXT("/Body/FunctionGraphs"), &Policy);

	TSharedPtr<FJsonValue> ExtractedValue;
	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Extract(Context, Adapter, ExtractedValue);

	TestTrue(TEXT("Deferred extract succeeds"), Result.bSuccess);
	TestTrue(TEXT("Deferred graph default is array"), ExtractedValue.IsValid() && ExtractedValue->Type == EJson::Array);
	TestEqual(TEXT("Deferred graph default is empty"), ExtractedValue.IsValid() ? ExtractedValue->AsArray().Num() : -1, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeObjectRegionRequiresObjectTest,
	"AssetFactory.AssetDocument.RegionRuntime.ObjectRegion.RequiresObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeObjectRegionRequiresObjectTest::RunTest(const FString& Parameters)
{
	FAssetDocumentObjectRegionAdapter Adapter;
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("Preview"), TEXT("Body.Preview"), EAssetDocumentRegionKind::Object);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("Preview"), TEXT("/Body/Preview"), &Policy);

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Validate(Context, MakeShared<FJsonValueString>(TEXT("not-object")), Adapter);

	TestFalse(TEXT("Object adapter rejects non-object values"), Result.bSuccess);
	TestEqual(TEXT("Non-object diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/Preview")));
	TestEqual(TEXT("Non-object diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidBodySectionType")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeObjectRegionCallsValidationHookTest,
	"AssetFactory.AssetDocument.RegionRuntime.ObjectRegion.CallsValidationHook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeObjectRegionCallsValidationHookTest::RunTest(const FString& Parameters)
{
	int32 ValidationCalls = 0;
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ValidateObject = [&ValidationCalls](const FAssetDocumentRegionContext& Context, const TSharedRef<FJsonObject>& Object)
	{
		++ValidationCalls;
		return Object->HasField(TEXT("PreviewMesh"))
			? FAssetDocumentCapabilityResult::Success(TEXT("object hook ok"))
			: FAssetDocumentJsonRegionUtils::Failure(Context.JsonPointer / TEXT("PreviewMesh"), TEXT("MissingPreviewMesh"), TEXT("PreviewMesh is required"));
	};
	FAssetDocumentObjectRegionAdapter Adapter(TEXT("ObjectAdapter"), MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("Preview"), TEXT("Body.Preview"), EAssetDocumentRegionKind::Object);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("Preview"), TEXT("/Body/Preview"), &Policy);
	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetObjectField(TEXT("PreviewMesh"), MakeShared<FJsonObject>());

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Validate(Context, MakeObjectValue(Preview), Adapter);

	TestTrue(TEXT("Object validation hook succeeds"), Result.bSuccess);
	TestEqual(TEXT("Validation hook is called once"), ValidationCalls, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeObjectRegionExtractsViaHookTest,
	"AssetFactory.AssetDocument.RegionRuntime.ObjectRegion.ExtractsViaHook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeObjectRegionExtractsViaHookTest::RunTest(const FString& Parameters)
{
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ExtractObject = [](const FAssetDocumentRegionContext&, TSharedRef<FJsonObject>& OutObject)
	{
		OutObject->SetNumberField(TEXT("RateScale"), 1.25);
		return FAssetDocumentCapabilityResult::Success(TEXT("extracted object"));
	};
	FAssetDocumentObjectRegionAdapter Adapter(TEXT("ObjectAdapter"), MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("Playback"), TEXT("Body.Playback"), EAssetDocumentRegionKind::Object);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("Playback"), TEXT("/Body/Playback"), &Policy);

	TSharedPtr<FJsonValue> ExtractedValue;
	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Extract(Context, Adapter, ExtractedValue);

	TestTrue(TEXT("Object extract hook succeeds"), Result.bSuccess);
	TestTrue(TEXT("Extracted value is an object"), ExtractedValue.IsValid() && ExtractedValue->Type == EJson::Object);
	TestEqual(TEXT("Extracted object field is returned"), ExtractedValue->AsObject()->GetNumberField(TEXT("RateScale")), 1.25);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeObjectRegionDiffsCanonicalJsonTest,
	"AssetFactory.AssetDocument.RegionRuntime.ObjectRegion.DiffsCanonicalJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeObjectRegionDiffsCanonicalJsonTest::RunTest(const FString& Parameters)
{
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ExtractObject = [](const FAssetDocumentRegionContext&, TSharedRef<FJsonObject>& OutObject)
	{
		OutObject->SetNumberField(TEXT("B"), 2.0);
		OutObject->SetNumberField(TEXT("A"), 1.0);
		return FAssetDocumentCapabilityResult::Success(TEXT("extracted object"));
	};
	Hooks.DiffObject = [](const FAssetDocumentRegionContext& Context, const TSharedRef<FJsonObject>& DesiredObject, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		TSharedRef<FJsonObject> CurrentObject = MakeShared<FJsonObject>();
		CurrentObject->SetNumberField(TEXT("B"), 2.0);
		CurrentObject->SetNumberField(TEXT("A"), 1.0);
		const TSharedPtr<FJsonValue> CurrentValue = MakeShared<FJsonValueObject>(CurrentObject);
		const TSharedPtr<FJsonValue> DesiredValue = MakeShared<FJsonValueObject>(DesiredObject);
		if (FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentValue) !=
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(DesiredValue))
		{
			FAssetDocumentJsonRegionUtils::AddDiffEntry(
				OutDiffEntries,
				Context.JsonPointer,
				TEXT("changed"),
				CurrentValue,
				DesiredValue);
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("diffed object"));
	};
	FAssetDocumentObjectRegionAdapter Adapter(TEXT("ObjectAdapter"), MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("Playback"), TEXT("Body.Playback"), EAssetDocumentRegionKind::Object);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("Playback"), TEXT("/Body/Playback"), &Policy);

	TSharedRef<FJsonObject> SameDesired = MakeShared<FJsonObject>();
	SameDesired->SetNumberField(TEXT("A"), 1.0);
	SameDesired->SetNumberField(TEXT("B"), 2.0);
	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Diff(Context, MakeObjectValue(SameDesired), Adapter, DiffEntries);
	TestTrue(TEXT("Canonical object diff succeeds"), Result.bSuccess);
	TestEqual(TEXT("Field insertion order does not produce diff"), DiffEntries.Num(), 0);

	TSharedRef<FJsonObject> ChangedDesired = MakeShared<FJsonObject>();
	ChangedDesired->SetNumberField(TEXT("A"), 1.0);
	ChangedDesired->SetNumberField(TEXT("B"), 3.0);
	Result = FAssetDocumentRegionRuntime::Diff(Context, MakeObjectValue(ChangedDesired), Adapter, DiffEntries);
	TestTrue(TEXT("Changed object diff succeeds"), Result.bSuccess);
	TestEqual(TEXT("Changed canonical object emits one diff"), DiffEntries.Num(), 1);
	TestEqual(TEXT("Changed object diff path"), DiffEntries.Num() > 0 ? DiffEntries[0]->AsObject()->GetStringField(TEXT("path")) : FString(), FString(TEXT("/Body/Playback")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeObjectRegionMissingLifecycleHooksFailFastTest,
	"AssetFactory.AssetDocument.RegionRuntime.ObjectRegion.MissingLifecycleHooksFailFast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeObjectRegionMissingLifecycleHooksFailFastTest::RunTest(const FString& Parameters)
{
	FAssetDocumentObjectRegionAdapter Adapter(TEXT("ObjectAdapter"));
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("Playback"), TEXT("Body.Playback"), EAssetDocumentRegionKind::Object);
	FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("Playback"), TEXT("/Body/Playback"), &Policy);
	TSharedRef<FJsonObject> Desired = MakeShared<FJsonObject>();

	bool bChanged = true;
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Apply(Context, MakeObjectValue(Desired), Adapter, bChanged);
	TestFalse(TEXT("Object apply without hook fails"), Result.bSuccess);
	TestEqual(TEXT("Object missing apply hook diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("MissingRegionApplyHook")));
	TestFalse(TEXT("Object missing apply hook leaves changed false"), bChanged);

	TSharedPtr<FJsonValue> ExtractedValue;
	Result = FAssetDocumentRegionRuntime::Extract(Context, Adapter, ExtractedValue);
	TestFalse(TEXT("Object extract without hook fails"), Result.bSuccess);
	TestEqual(TEXT("Object missing extract hook diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("MissingRegionExtractHook")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	Result = FAssetDocumentRegionRuntime::Diff(Context, MakeObjectValue(Desired), Adapter, DiffEntries);
	TestFalse(TEXT("Object diff without hook fails"), Result.bSuccess);
	TestEqual(TEXT("Object missing diff hook diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("MissingRegionDiffHook")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeNamedArrayRequiresArrayTest,
	"AssetFactory.AssetDocument.RegionRuntime.NamedArray.RequiresArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeNamedArrayRequiresArrayTest::RunTest(const FString& Parameters)
{
	FAssetDocumentNamedArrayRegionAdapter Adapter;
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("NotifyTracks"), TEXT("Body.NotifyTracks"), EAssetDocumentRegionKind::Array);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("NotifyTracks"), TEXT("/Body/NotifyTracks"), &Policy);

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Validate(Context, MakeObjectValue(MakeShared<FJsonObject>()), Adapter);

	TestFalse(TEXT("Named array adapter rejects non-array values"), Result.bSuccess);
	TestEqual(TEXT("Non-array diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidBodySectionType")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeNamedArrayRequiresUniqueIdentityTest,
	"AssetFactory.AssetDocument.RegionRuntime.NamedArray.RequiresUniqueIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeNamedArrayRequiresUniqueIdentityTest::RunTest(const FString& Parameters)
{
	FAssetDocumentNamedArrayRegionAdapter Adapter;
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("NotifyTracks"), TEXT("Body.NotifyTracks"), EAssetDocumentRegionKind::Array);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("NotifyTracks"), TEXT("/Body/NotifyTracks"), &Policy);
	TSharedRef<FJsonObject> First = MakeShared<FJsonObject>();
	First->SetStringField(TEXT("Name"), TEXT("Footstep"));
	TSharedRef<FJsonObject> Second = MakeShared<FJsonObject>();
	Second->SetStringField(TEXT("Name"), TEXT("Footstep"));

	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Validate(
		Context,
		MakeArrayValue({MakeObjectValue(First), MakeObjectValue(Second)}),
		Adapter);

	TestFalse(TEXT("Duplicate named array identity fails"), Result.bSuccess);
	TestEqual(TEXT("Duplicate identity diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("DuplicateNamedArrayIdentity")));
	TestEqual(TEXT("Duplicate identity diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/NotifyTracks/1/Name")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeNamedArrayRejectsNormalizedDuplicateIdentityTest,
	"AssetFactory.AssetDocument.RegionRuntime.NamedArray.RejectsNormalizedDuplicateIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeNamedArrayRejectsNormalizedDuplicateIdentityTest::RunTest(const FString& Parameters)
{
	FAssetDocumentNamedArrayRegionAdapterConfig Config;
	Config.IdentityField = TEXT("Name");
	Config.NormalizeIdentity = [](const FString& Identity)
	{
		return Identity.ToLower();
	};
	FAssetDocumentNamedArrayRegionAdapter Adapter(Config);
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("NotifyTracks"), TEXT("Body.NotifyTracks"), EAssetDocumentRegionKind::Array);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("NotifyTracks"), TEXT("/Body/NotifyTracks"), &Policy);
	TSharedRef<FJsonObject> First = MakeShared<FJsonObject>();
	First->SetStringField(TEXT("Name"), TEXT("Default"));
	TSharedRef<FJsonObject> Second = MakeShared<FJsonObject>();
	Second->SetStringField(TEXT("Name"), TEXT("default"));

	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Validate(
		Context,
		MakeArrayValue({MakeObjectValue(First), MakeObjectValue(Second)}),
		Adapter);

	TestFalse(TEXT("Normalized duplicate named array identity fails"), Result.bSuccess);
	TestEqual(TEXT("Normalized duplicate identity diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("DuplicateNamedArrayIdentity")));
	TestEqual(TEXT("Normalized duplicate identity diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/NotifyTracks/1/Name")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeNamedArrayRejectsMissingIdentityTest,
	"AssetFactory.AssetDocument.RegionRuntime.NamedArray.RejectsMissingIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeNamedArrayRejectsMissingIdentityTest::RunTest(const FString& Parameters)
{
	FAssetDocumentNamedArrayRegionAdapter Adapter;
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("NotifyTracks"), TEXT("Body.NotifyTracks"), EAssetDocumentRegionKind::Array);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("NotifyTracks"), TEXT("/Body/NotifyTracks"), &Policy);
	TSharedRef<FJsonObject> Track = MakeShared<FJsonObject>();
	Track->SetNumberField(TEXT("Index"), 0.0);

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Validate(Context, MakeArrayValue({MakeObjectValue(Track)}), Adapter);

	TestFalse(TEXT("Missing identity field fails"), Result.bSuccess);
	TestEqual(TEXT("Missing identity diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("MissingNamedArrayIdentity")));
	TestEqual(TEXT("Missing identity diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/NotifyTracks/0/Name")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeNamedArrayCanonicalizesByIdentityTest,
	"AssetFactory.AssetDocument.RegionRuntime.NamedArray.CanonicalizesByIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeNamedArrayCanonicalizesByIdentityTest::RunTest(const FString& Parameters)
{
	FAssetDocumentNamedArrayRegionAdapterHooks Hooks;
	Hooks.ExtractElements = [](const FAssetDocumentRegionContext&, TArray<TSharedRef<FJsonObject>>& OutElements)
	{
		TSharedRef<FJsonObject> Beta = MakeShared<FJsonObject>();
		Beta->SetStringField(TEXT("Name"), TEXT("Beta"));
		TSharedRef<FJsonObject> Alpha = MakeShared<FJsonObject>();
		Alpha->SetStringField(TEXT("Name"), TEXT("Alpha"));
		OutElements = {Beta, Alpha};
		return FAssetDocumentCapabilityResult::Success(TEXT("extracted elements"));
	};
	FAssetDocumentNamedArrayRegionAdapter Adapter(TEXT("NamedArrayAdapter"), TEXT("Name"), MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("NotifyTracks"), TEXT("Body.NotifyTracks"), EAssetDocumentRegionKind::Array);
	const FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("NotifyTracks"), TEXT("/Body/NotifyTracks"), &Policy);

	TSharedPtr<FJsonValue> ExtractedValue;
	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Extract(Context, Adapter, ExtractedValue);

	TestTrue(TEXT("Canonical named array extract succeeds"), Result.bSuccess);
	TestTrue(TEXT("Canonical extract returns array"), ExtractedValue.IsValid() && ExtractedValue->Type == EJson::Array);
	if (ExtractedValue.IsValid() && ExtractedValue->Type == EJson::Array && ExtractedValue->AsArray().Num() == 2)
	{
		TestEqual(TEXT("Canonical extract sorts first identity"), ExtractedValue->AsArray()[0]->AsObject()->GetStringField(TEXT("Name")), FString(TEXT("Alpha")));
		TestEqual(TEXT("Canonical extract sorts second identity"), ExtractedValue->AsArray()[1]->AsObject()->GetStringField(TEXT("Name")), FString(TEXT("Beta")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeNamedArrayPreservesAuthoredApplyOrderWhenConfiguredTest,
	"AssetFactory.AssetDocument.RegionRuntime.NamedArray.PreservesAuthoredApplyOrderWhenConfigured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeNamedArrayPreservesAuthoredApplyOrderWhenConfiguredTest::RunTest(const FString& Parameters)
{
	TArray<FString> AppliedOrder;
	FAssetDocumentNamedArrayRegionAdapterHooks Hooks;
	Hooks.ApplyElements = [&AppliedOrder](FAssetDocumentRegionContext&, const TArray<TSharedRef<FJsonObject>>& Elements, bool& bOutChanged)
	{
		for (const TSharedRef<FJsonObject>& Element : Elements)
		{
			AppliedOrder.Add(Element->GetStringField(TEXT("Name")));
		}
		bOutChanged = true;
		return FAssetDocumentCapabilityResult::Success(TEXT("applied elements"));
	};
	FAssetDocumentNamedArrayRegionAdapterConfig Config;
	Config.Name = TEXT("NamedArrayAdapter");
	Config.IdentityField = TEXT("Name");
	Config.bPreserveAuthoredApplyOrder = true;
	FAssetDocumentNamedArrayRegionAdapter Adapter(Config, MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("NotifyTracks"), TEXT("Body.NotifyTracks"), EAssetDocumentRegionKind::Array);
	FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("NotifyTracks"), TEXT("/Body/NotifyTracks"), &Policy);
	TSharedRef<FJsonObject> BetaDesired = MakeShared<FJsonObject>();
	BetaDesired->SetStringField(TEXT("Name"), TEXT("Beta"));
	TSharedRef<FJsonObject> AlphaDesired = MakeShared<FJsonObject>();
	AlphaDesired->SetStringField(TEXT("Name"), TEXT("Alpha"));

	bool bChanged = false;
	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Apply(
		Context,
		MakeArrayValue({MakeObjectValue(BetaDesired), MakeObjectValue(AlphaDesired)}),
		Adapter,
		bChanged);

	TestTrue(TEXT("Named array apply succeeds"), Result.bSuccess);
	TestTrue(TEXT("Named array apply reports change"), bChanged);
	TestEqual(TEXT("Two elements are applied"), AppliedOrder.Num(), 2);
	if (AppliedOrder.Num() < 2)
	{
		return false;
	}
	TestEqual(TEXT("First authored element is applied first"), AppliedOrder[0], FString(TEXT("Beta")));
	TestEqual(TEXT("Second authored element is applied second"), AppliedOrder[1], FString(TEXT("Alpha")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeNamedArrayNoopApplyCanReturnUnchangedTest,
	"AssetFactory.AssetDocument.RegionRuntime.NamedArray.NoopApplyCanReturnUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeNamedArrayNoopApplyCanReturnUnchangedTest::RunTest(const FString& Parameters)
{
	FAssetDocumentNamedArrayRegionAdapterHooks Hooks;
	Hooks.ApplyElements = [](FAssetDocumentRegionContext&, const TArray<TSharedRef<FJsonObject>>&, bool& bOutChanged)
	{
		bOutChanged = false;
		return FAssetDocumentCapabilityResult::Success(TEXT("no-op apply"));
	};
	FAssetDocumentNamedArrayRegionAdapter Adapter(TEXT("NamedArrayAdapter"), TEXT("Name"), MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("NotifyTracks"), TEXT("Body.NotifyTracks"), EAssetDocumentRegionKind::Array);
	FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("NotifyTracks"), TEXT("/Body/NotifyTracks"), &Policy);
	TSharedRef<FJsonObject> Track = MakeShared<FJsonObject>();
	Track->SetStringField(TEXT("Name"), TEXT("Default"));

	bool bChanged = true;
	const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Apply(
		Context,
		MakeArrayValue({MakeObjectValue(Track)}),
		Adapter,
		bChanged);

	TestTrue(TEXT("No-op named array apply succeeds"), Result.bSuccess);
	TestFalse(TEXT("No-op named array apply reports unchanged"), bChanged);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeNamedArrayMissingLifecycleHooksFailFastTest,
	"AssetFactory.AssetDocument.RegionRuntime.NamedArray.MissingLifecycleHooksFailFast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeNamedArrayMissingLifecycleHooksFailFastTest::RunTest(const FString& Parameters)
{
	FAssetDocumentNamedArrayRegionAdapter Adapter(TEXT("NamedArrayAdapter"), TEXT("Name"));
	const FAssetDocumentRegionPolicy Policy =
		MakeDeferredPolicy(TEXT("NotifyTracks"), TEXT("Body.NotifyTracks"), EAssetDocumentRegionKind::Array);
	FAssetDocumentRegionContext Context =
		MakeRuntimeContext(TEXT("NotifyTracks"), TEXT("/Body/NotifyTracks"), &Policy);
	TSharedRef<FJsonObject> Track = MakeShared<FJsonObject>();
	Track->SetStringField(TEXT("Name"), TEXT("Default"));
	const TSharedPtr<FJsonValue> Desired = MakeArrayValue({MakeObjectValue(Track)});

	bool bChanged = true;
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentRegionRuntime::Apply(Context, Desired, Adapter, bChanged);
	TestFalse(TEXT("Named array apply without hook fails"), Result.bSuccess);
	TestEqual(TEXT("Named array missing apply hook diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("MissingRegionApplyHook")));
	TestFalse(TEXT("Named array missing apply hook leaves changed false"), bChanged);

	TSharedPtr<FJsonValue> ExtractedValue;
	Result = FAssetDocumentRegionRuntime::Extract(Context, Adapter, ExtractedValue);
	TestFalse(TEXT("Named array extract without hook fails"), Result.bSuccess);
	TestEqual(TEXT("Named array missing extract hook diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("MissingRegionExtractHook")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	Result = FAssetDocumentRegionRuntime::Diff(Context, Desired, Adapter, DiffEntries);
	TestFalse(TEXT("Named array diff without hook fails"), Result.bSuccess);
	TestEqual(TEXT("Named array missing diff hook diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("MissingRegionDiffHook")));
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
	FAssetDocumentRegionRuntimeBodyDispatcherRejectsCaseMismatchedBodyKeyTest,
	"AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsCaseMismatchedBodyKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeBodyDispatcherRejectsCaseMismatchedBodyKeyTest::RunTest(const FString& Parameters)
{
	FTestRegionAdapter Adapter(TEXT("Fake"));
	const FAssetDocumentBodyRegionDispatcher Dispatcher = MakeDispatcher(
		{MakeBinding(TEXT("Preview"), TEXT("Preview"), TEXT("Fake"))},
		{MakePolicy(TEXT("Preview"), TEXT("Body.Preview"))},
		{&Adapter});

	const FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result = Dispatcher.ValidateBody(
		Context,
		MakeObjectRef(MakeBodyWithField(TEXT("preview"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()))));

	TestFalse(TEXT("Case-mismatched Body key is rejected"), Result.bSuccess);
	TestEqual(TEXT("Case-mismatched key diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/preview")));
	TestEqual(TEXT("Case-mismatched key diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("UnknownBodyRegion")));
	TestEqual(TEXT("Case-mismatched key is not silently ignored"), Adapter.ValidateCalls, 0);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeObjectFieldSchemaAcceptsValidObjectTest,
	"AssetFactory.AssetDocument.RegionRuntime.ObjectFieldSchema.AcceptsValidObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeObjectFieldSchemaAcceptsValidObjectTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.Palette"), TEXT("/Body/Palette"));
	Context.BodyPath = TEXT("Body.Palette");
	FAssetDocumentObjectFieldSchema Schema;
	Schema.UnknownFieldCode = TEXT("UnknownPaletteField");
	Schema.Fields.Add({TEXT("Category"), EJson::String, false, TEXT("MissingPaletteCategory"), TEXT("InvalidPaletteCategory")});
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Category"), TEXT("AssetDocument"));

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(Context, Object, Schema);

	TestTrue(TEXT("Valid object field schema succeeds"), Result.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeObjectFieldSchemaAcceptsAllowedNullTest,
	"AssetFactory.AssetDocument.RegionRuntime.ObjectFieldSchema.AcceptsAllowedNull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeObjectFieldSchemaAcceptsAllowedNullTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.Preview"), TEXT("/Body/Preview"));
	Context.BodyPath = TEXT("Body.Preview");
	FAssetDocumentObjectFieldSchema Schema;
	Schema.UnknownFieldCode = TEXT("UnsupportedAuthoredField");
	Schema.Fields.Add({
		TEXT("PreviewMesh"),
		EJson::Object,
		false,
		TEXT("MissingPreviewMesh"),
		TEXT("InvalidObjectReference"),
		FString(),
		true,
	});
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetField(TEXT("PreviewMesh"), MakeShared<FJsonValueNull>());

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(Context, Object, Schema);

	TestTrue(TEXT("Allowed null object field schema succeeds"), Result.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeObjectFieldSchemaRejectsUnknownFieldTest,
	"AssetFactory.AssetDocument.RegionRuntime.ObjectFieldSchema.RejectsUnknownField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeObjectFieldSchemaRejectsUnknownFieldTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.Palette"), TEXT("/Body/Palette"));
	Context.BodyPath = TEXT("Body.Palette");
	FAssetDocumentObjectFieldSchema Schema;
	Schema.UnknownFieldCode = TEXT("UnknownPaletteField");
	Schema.UnknownFieldMessageFormat = TEXT("Unknown Body.Palette field '%s'");
	Schema.Fields.Add({TEXT("Category"), EJson::String, false, TEXT("MissingPaletteCategory"), TEXT("InvalidPaletteCategory")});
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Category/With~Escape"), TEXT("invalid"));

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(Context, Object, Schema);

	TestFalse(TEXT("Unknown field schema fails"), Result.bSuccess);
	TestEqual(TEXT("Unknown field diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("UnknownPaletteField")));
	TestEqual(TEXT("Unknown field path escapes JSON Pointer tokens"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/Palette/Category~1With~0Escape")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeObjectFieldSchemaRejectsWrongTypeTest,
	"AssetFactory.AssetDocument.RegionRuntime.ObjectFieldSchema.RejectsWrongType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeObjectFieldSchemaRejectsWrongTypeTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.EditorOptions"), TEXT("/Body/EditorOptions"));
	Context.BodyPath = TEXT("Body.EditorOptions");
	FAssetDocumentObjectFieldSchema Schema;
	Schema.UnknownFieldCode = TEXT("UnknownEditorOption");
	Schema.Fields.Add({
		TEXT("bCanCallInitializedWithoutPlayerContext"),
		EJson::Boolean,
		false,
		TEXT("MissingEditorOption"),
		TEXT("InvalidEditorOption"),
		TEXT("Body.EditorOptions.bCanCallInitializedWithoutPlayerContext must be a boolean")
	});
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("bCanCallInitializedWithoutPlayerContext"), TEXT("true"));

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(Context, Object, Schema);

	TestFalse(TEXT("Wrong field type schema fails"), Result.bSuccess);
	TestEqual(TEXT("Wrong type diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidEditorOption")));
	TestEqual(TEXT("Wrong type diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/EditorOptions/bCanCallInitializedWithoutPlayerContext")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeObjectFieldSchemaRejectsMissingRequiredFieldTest,
	"AssetFactory.AssetDocument.RegionRuntime.ObjectFieldSchema.RejectsMissingRequiredField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeObjectFieldSchemaRejectsMissingRequiredFieldTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.Required"), TEXT("/Body/Required"));
	Context.BodyPath = TEXT("Body.Required");
	FAssetDocumentObjectFieldSchema Schema;
	Schema.UnknownFieldCode = TEXT("UnknownRequiredField");
	Schema.Fields.Add({TEXT("Name"), EJson::String, true, TEXT("MissingRequiredName"), TEXT("InvalidRequiredName")});
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();

	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(Context, Object, Schema);

	TestFalse(TEXT("Missing required field schema fails"), Result.bSuccess);
	TestEqual(TEXT("Missing field diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("MissingRequiredName")));
	TestEqual(TEXT("Missing field diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body/Required/Name")));
	return true;
}

#endif
