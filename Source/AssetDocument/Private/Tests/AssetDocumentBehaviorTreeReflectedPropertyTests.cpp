// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentReflectedPropertyUtils.h"

#include "AssetDocumentCanonicalJson.h"
#include "Tests/AssetDocumentReflectedPropertyTestTypes.h"

#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_SetKeyValue.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AssetDocumentBehaviorTreeReflectedPropertyTests
{
bool HasDiagnostic(
	const FAssetDocumentCapabilityResult& Result,
	const FString& Path,
	const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

TSharedRef<FJsonObject> MakeInstancedStructJson(UScriptStruct* Struct, const TSharedRef<FJsonObject>& Properties)
{
	TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
	Value->SetStringField(TEXT("Struct"), Struct->GetPathName());
	Value->SetObjectField(TEXT("Properties"), Properties);
	return Value;
}

TSharedRef<FJsonObject> MakeNestedValueJson(
	const FString& Identity,
	const FString& PreservedText,
	const FString& SelectedKey,
	const FString& NestedEntryName)
{
	TSharedRef<FJsonObject> Selector = MakeShared<FJsonObject>();
	Selector->SetStringField(TEXT("Key"), SelectedKey);

	TSharedRef<FJsonObject> NestedProperties = MakeShared<FJsonObject>();
	NestedProperties->SetStringField(TEXT("EntryName"), NestedEntryName);
	NestedProperties->SetBoolField(TEXT("bInstanceSynced"), true);

	TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
	Value->SetStringField(TEXT("Identity"), Identity);
	Value->SetStringField(TEXT("PreservedText"), PreservedText);
	Value->SetObjectField(TEXT("Selector"), Selector);
	Value->SetObjectField(TEXT("NestedValue"), MakeInstancedStructJson(FBlackboardEntry::StaticStruct(), NestedProperties));
	return Value;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeInstancedStructPropertyTest,
	"AssetFactory.AssetDocument.BehaviorTree.ReflectedPropertyRuntime.InstancedStructRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeInstancedStructPropertyTest::RunTest(const FString&)
{
	UBlackboardKeyType_Struct* KeyType = NewObject<UBlackboardKeyType_Struct>(GetTransientPackage());
	TestNotNull(TEXT("Struct key type exists"), KeyType);
	if (!KeyType)
	{
		return false;
	}

	TSharedRef<FJsonObject> ValueProperties = MakeShared<FJsonObject>();
	ValueProperties->SetStringField(TEXT("EntryName"), TEXT("TargetActor"));
	ValueProperties->SetBoolField(TEXT("bInstanceSynced"), true);

	TSharedRef<FJsonObject> InstancedStructValue = MakeShared<FJsonObject>();
	InstancedStructValue->SetStringField(TEXT("Struct"), FBlackboardEntry::StaticStruct()->GetPathName());
	InstancedStructValue->SetObjectField(TEXT("Properties"), ValueProperties);

	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("DefaultValue"), InstancedStructValue);

	const FAssetDocumentCapabilityResult ValidateResult =
		FAssetDocumentReflectedPropertyUtils::ValidateProperties(KeyType, Properties, TEXT("/Properties"));
	TestTrue(TEXT("FInstancedStruct canonical object validates"), ValidateResult.bSuccess);
	if (!ValidateResult.bSuccess)
	{
		return false;
	}

	const FAssetDocumentCapabilityResult ApplyResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(KeyType, Properties, TEXT("/Properties"));
	TestTrue(TEXT("FInstancedStruct canonical object applies"), ApplyResult.bSuccess);
	if (!ApplyResult.bSuccess)
	{
		return false;
	}

	TestTrue(TEXT("Selected struct type persists"), KeyType->DefaultValue.GetScriptStruct() == FBlackboardEntry::StaticStruct());
	const FBlackboardEntry* AppliedEntry = KeyType->DefaultValue.GetPtr<FBlackboardEntry>();
	TestNotNull(TEXT("Applied struct data exists"), AppliedEntry);
	if (AppliedEntry)
	{
		TestEqual(TEXT("Instanced struct name persists"), AppliedEntry->EntryName, FName(TEXT("TargetActor")));
		TestTrue(TEXT("Instanced struct boolean persists"), AppliedEntry->bInstanceSynced != 0);
	}

	TSharedRef<FJsonObject> ExtractedProperties = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult =
		FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(KeyType, ExtractedProperties, TEXT("/Properties"));
	TestTrue(TEXT("FInstancedStruct extracts"), ExtractResult.bSuccess);

	const TSharedPtr<FJsonObject>* ExtractedValue = nullptr;
	TestTrue(
		TEXT("FInstancedStruct extracts as canonical object"),
		ExtractResult.bSuccess
			&& ExtractedProperties->TryGetObjectField(TEXT("DefaultValue"), ExtractedValue)
			&& ExtractedValue
			&& ExtractedValue->IsValid());
	if (ExtractedValue && ExtractedValue->IsValid())
	{
		TestEqual(
			TEXT("Extracted struct path is canonical"),
			(*ExtractedValue)->GetStringField(TEXT("Struct")),
			FBlackboardEntry::StaticStruct()->GetPathName());
		const TSharedPtr<FJsonObject>* ExtractedValueProperties = nullptr;
		if ((*ExtractedValue)->TryGetObjectField(TEXT("Properties"), ExtractedValueProperties)
			&& ExtractedValueProperties
			&& ExtractedValueProperties->IsValid())
		{
			TestEqual(TEXT("Extracted struct name roundtrips"), (*ExtractedValueProperties)->GetStringField(TEXT("EntryName")), FString(TEXT("TargetActor")));
			TestTrue(TEXT("Extracted struct boolean roundtrips"), (*ExtractedValueProperties)->GetBoolField(TEXT("bInstanceSynced")));
		}
		else
		{
			AddError(TEXT("Extracted FInstancedStruct is missing Properties"));
		}
	}

	TSharedRef<FJsonObject> ResetProperties = MakeShared<FJsonObject>();
	ResetProperties->SetField(TEXT("DefaultValue"), MakeShared<FJsonValueNull>());
	const FAssetDocumentCapabilityResult ResetResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(KeyType, ResetProperties, TEXT("/Properties"));
	TestTrue(TEXT("Null resets FInstancedStruct"), ResetResult.bSuccess);
	TestFalse(TEXT("Reset FInstancedStruct is empty"), KeyType->DefaultValue.IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeSelectorAuthoredBoundaryTest,
	"AssetFactory.AssetDocument.BehaviorTree.ReflectedPropertyRuntime.SelectorAuthoredBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeSelectorAuthoredBoundaryTest::RunTest(const FString&)
{
	UBTTask_MoveTo* MoveToTask = NewObject<UBTTask_MoveTo>(GetTransientPackage());
	TestNotNull(TEXT("MoveTo task exists"), MoveToTask);
	if (!MoveToTask)
	{
		return false;
	}

	TSharedRef<FJsonObject> Selector = MakeShared<FJsonObject>();
	Selector->SetStringField(TEXT("Key"), TEXT("TargetActor"));
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("BlackboardKey"), Selector);
	TestTrue(
		TEXT("Canonical selector Key validates"),
		FAssetDocumentReflectedPropertyUtils::ValidateProperties(MoveToTask, Properties, TEXT("/Properties")).bSuccess);

	static const TArray<FString> DerivedFields =
	{
		TEXT("SelectedKeyName"),
		TEXT("AllowedTypes"),
		TEXT("SelectedKeyType"),
		TEXT("SelectedKeyID"),
		TEXT("bNoneIsAllowedValue")
	};

	for (const FString& Field : DerivedFields)
	{
		TSharedRef<FJsonObject> InvalidSelector = MakeShared<FJsonObject>();
		InvalidSelector->SetStringField(TEXT("Key"), TEXT("TargetActor"));
		if (Field == TEXT("AllowedTypes"))
		{
			InvalidSelector->SetArrayField(Field, {});
		}
		else if (Field == TEXT("bNoneIsAllowedValue"))
		{
			InvalidSelector->SetBoolField(Field, true);
		}
		else if (Field == TEXT("SelectedKeyID"))
		{
			InvalidSelector->SetNumberField(Field, 0);
		}
		else
		{
			InvalidSelector->SetStringField(Field, TEXT("Derived"));
		}

		TSharedRef<FJsonObject> InvalidProperties = MakeShared<FJsonObject>();
		InvalidProperties->SetObjectField(TEXT("BlackboardKey"), InvalidSelector);
		const FAssetDocumentCapabilityResult Result =
			FAssetDocumentReflectedPropertyUtils::ValidateProperties(MoveToTask, InvalidProperties, TEXT("/Properties"));
		TestFalse(*FString::Printf(TEXT("Derived selector field %s is rejected"), *Field), Result.bSuccess);
		TestTrue(
			*FString::Printf(TEXT("Derived selector field %s reports exact boundary"), *Field),
			AssetDocumentBehaviorTreeReflectedPropertyTests::HasDiagnostic(
				Result,
				FString::Printf(TEXT("/Properties/BlackboardKey/%s"), *Field),
				TEXT("NonAuthoredProperty")));
	}

	TestTrue(
		TEXT("Canonical selector Key applies"),
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(MoveToTask, Properties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedProperties = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult =
		FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(MoveToTask, ExtractedProperties, TEXT("/Properties"));
	TestTrue(TEXT("Selector extracts"), ExtractResult.bSuccess);

	const TSharedPtr<FJsonObject>* ExtractedSelector = nullptr;
	if (ExtractResult.bSuccess
		&& ExtractedProperties->TryGetObjectField(TEXT("BlackboardKey"), ExtractedSelector)
		&& ExtractedSelector
		&& ExtractedSelector->IsValid())
	{
		TestEqual(TEXT("Selector extract contains only authored Key"), (*ExtractedSelector)->Values.Num(), 1);
		TestEqual(TEXT("Selector extract keeps selected key"), (*ExtractedSelector)->GetStringField(TEXT("Key")), FString(TEXT("TargetActor")));
	}
	else
	{
		AddError(TEXT("Extracted properties are missing BlackboardKey"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeInstancedStructConstraintAndAtomicityTest,
	"AssetFactory.AssetDocument.BehaviorTree.ReflectedPropertyRuntime.InstancedStructConstraintsAndAtomicity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeInstancedStructConstraintAndAtomicityTest::RunTest(const FString&)
{
	using namespace AssetDocumentBehaviorTreeReflectedPropertyTests;

	UAssetDocumentReflectedPropertyTestObject* Object =
		NewObject<UAssetDocumentReflectedPropertyTestObject>(GetTransientPackage());
	TestNotNull(TEXT("reflected-property test object exists"), Object);
	if (!Object)
	{
		return false;
	}

	TSharedRef<FJsonObject> CompatiblePayload = MakeShared<FJsonObject>();
	CompatiblePayload->SetStringField(TEXT("EntryName"), TEXT("Compatible"));
	CompatiblePayload->SetBoolField(TEXT("bInstanceSynced"), false);
	TSharedRef<FJsonObject> CompatibleProperties = MakeShared<FJsonObject>();
	CompatibleProperties->SetObjectField(
		TEXT("ConstrainedValue"),
		MakeInstancedStructJson(FBlackboardEntry::StaticStruct(), CompatiblePayload));
	TestTrue(
		TEXT("BaseStruct-compatible value applies"),
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, CompatibleProperties, TEXT("/Properties")).bSuccess);

	TSharedRef<FJsonObject> IncompatibleProperties = MakeShared<FJsonObject>();
	IncompatibleProperties->SetObjectField(
		TEXT("ConstrainedValue"),
		MakeInstancedStructJson(TBaseStructure<FLinearColor>::Get(), MakeShared<FJsonObject>()));
	const FAssetDocumentCapabilityResult IncompatibleResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, IncompatibleProperties, TEXT("/Properties"));
	TestFalse(TEXT("BaseStruct-incompatible value is rejected"), IncompatibleResult.bSuccess);
	TestTrue(
		TEXT("BaseStruct-incompatible value reports exact diagnostic"),
		HasDiagnostic(IncompatibleResult, TEXT("/Properties/ConstrainedValue/Struct"), TEXT("IncompatibleStructType")));

	TSharedRef<FJsonObject> InvalidConstraintProperties = MakeShared<FJsonObject>();
	InvalidConstraintProperties->SetObjectField(
		TEXT("InvalidConstraintValue"),
		MakeInstancedStructJson(FBlackboardEntry::StaticStruct(), MakeShared<FJsonObject>()));
	AddExpectedError(TEXT("DoesNotExist"), EAutomationExpectedErrorFlags::Contains, 1);
	const FAssetDocumentCapabilityResult InvalidConstraintResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, InvalidConstraintProperties, TEXT("/Properties"));
	TestFalse(TEXT("unresolvable BaseStruct metadata is rejected"), InvalidConstraintResult.bSuccess);
	TestTrue(
		TEXT("unresolvable BaseStruct reports exact diagnostic"),
		HasDiagnostic(InvalidConstraintResult, TEXT("/Properties/InvalidConstraintValue/Struct"), TEXT("InvalidStructConstraint")));

	TSharedRef<FJsonObject> InitialDynamicPayload =
		MakeNestedValueJson(TEXT("Initial"), TEXT("PreserveMe"), TEXT("InitialKey"), TEXT("NestedInitial"));
	TSharedRef<FJsonObject> InitialProperties = MakeShared<FJsonObject>();
	InitialProperties->SetObjectField(
		TEXT("DynamicValue"),
		MakeInstancedStructJson(FAssetDocumentReflectedPropertyNestedTestValue::StaticStruct(), InitialDynamicPayload));
	TestTrue(
		TEXT("dynamic payload with nested custom structs applies"),
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, InitialProperties, TEXT("/Properties")).bSuccess);

	TSharedRef<FJsonObject> SparsePayload = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> SparseSelector = MakeShared<FJsonObject>();
	SparseSelector->SetStringField(TEXT("Key"), TEXT("SparseKey"));
	SparsePayload->SetObjectField(TEXT("Selector"), SparseSelector);
	TSharedRef<FJsonObject> SparseProperties = MakeShared<FJsonObject>();
	SparseProperties->SetObjectField(
		TEXT("DynamicValue"),
		MakeInstancedStructJson(FAssetDocumentReflectedPropertyNestedTestValue::StaticStruct(), SparsePayload));
	TestTrue(
		TEXT("same-type sparse dynamic payload applies"),
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, SparseProperties, TEXT("/Properties")).bSuccess);

	const FAssetDocumentReflectedPropertyNestedTestValue* SparseValue =
		Object->DynamicValue.GetPtr<FAssetDocumentReflectedPropertyNestedTestValue>();
	TestNotNull(TEXT("sparse dynamic payload remains typed"), SparseValue);
	if (SparseValue)
	{
		TestEqual(TEXT("sparse update preserves omitted identity"), SparseValue->Identity, FName(TEXT("Initial")));
		TestEqual(TEXT("sparse update preserves omitted string"), SparseValue->PreservedText, FString(TEXT("PreserveMe")));
		TestEqual(TEXT("sparse update changes nested selector"), SparseValue->Selector.SelectedKeyName, FName(TEXT("SparseKey")));
		TestTrue(TEXT("sparse update preserves nested FInstancedStruct"), SparseValue->NestedValue.IsValid());
	}

	TSharedRef<FJsonObject> InvalidLatePayload = MakeShared<FJsonObject>();
	InvalidLatePayload->SetStringField(TEXT("PreservedText"), TEXT("MustNotCommit"));
	TSharedRef<FJsonObject> InvalidSelector = MakeShared<FJsonObject>();
	InvalidSelector->SetStringField(TEXT("Key"), TEXT("Bad"));
	InvalidSelector->SetNumberField(TEXT("SelectedKeyID"), 7);
	InvalidLatePayload->SetObjectField(TEXT("Selector"), InvalidSelector);
	TSharedRef<FJsonObject> InvalidLateProperties = MakeShared<FJsonObject>();
	InvalidLateProperties->SetObjectField(
		TEXT("DynamicValue"),
		MakeInstancedStructJson(FAssetDocumentReflectedPropertyNestedTestValue::StaticStruct(), InvalidLatePayload));
	const FAssetDocumentCapabilityResult InvalidLateResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, InvalidLateProperties, TEXT("/Properties"));
	TestFalse(TEXT("late nested failure rejects whole update"), InvalidLateResult.bSuccess);
	SparseValue = Object->DynamicValue.GetPtr<FAssetDocumentReflectedPropertyNestedTestValue>();
	TestNotNull(TEXT("late failure leaves destination typed"), SparseValue);
	if (SparseValue)
	{
		TestEqual(TEXT("late failure leaves string unchanged"), SparseValue->PreservedText, FString(TEXT("PreserveMe")));
		TestEqual(TEXT("late failure leaves selector unchanged"), SparseValue->Selector.SelectedKeyName, FName(TEXT("SparseKey")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeCustomStructContainerRoundTripTest,
	"AssetFactory.AssetDocument.BehaviorTree.ReflectedPropertyRuntime.CustomStructContainersRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeCustomStructContainerRoundTripTest::RunTest(const FString&)
{
	using namespace AssetDocumentBehaviorTreeReflectedPropertyTests;

	UAssetDocumentReflectedPropertyTestObject* Source =
		NewObject<UAssetDocumentReflectedPropertyTestObject>(GetTransientPackage());
	UAssetDocumentReflectedPropertyTestObject* Destination =
		NewObject<UAssetDocumentReflectedPropertyTestObject>(GetTransientPackage());
	TestNotNull(TEXT("container source exists"), Source);
	TestNotNull(TEXT("container destination exists"), Destination);
	if (!Source || !Destination)
	{
		return false;
	}

	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ArrayValues;
	ArrayValues.Add(MakeShared<FJsonValueObject>(MakeNestedValueJson(TEXT("ArrayA"), TEXT("ArrayText"), TEXT("ArrayKey"), TEXT("ArrayNested"))));
	Properties->SetArrayField(TEXT("ArrayValues"), MoveTemp(ArrayValues));

	TSharedRef<FJsonObject> MapValues = MakeShared<FJsonObject>();
	MapValues->SetObjectField(TEXT("MapA"), MakeNestedValueJson(TEXT("MapA"), TEXT("MapText"), TEXT("MapKey"), TEXT("MapNested")));
	Properties->SetObjectField(TEXT("MapValues"), MapValues);

	TArray<TSharedPtr<FJsonValue>> SetValues;
	SetValues.Add(MakeShared<FJsonValueObject>(MakeNestedValueJson(TEXT("SetA"), TEXT("SetText"), TEXT("SetKey"), TEXT("SetNested"))));
	Properties->SetArrayField(TEXT("SetValues"), MoveTemp(SetValues));

	const FAssetDocumentCapabilityResult ApplyResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Source, Properties, TEXT("/Properties"));
	TestTrue(TEXT("array/map/set containing nested custom structs apply"), ApplyResult.bSuccess);
	if (!ApplyResult.bSuccess)
	{
		return false;
	}

	TestEqual(TEXT("custom array count"), Source->ArrayValues.Num(), 1);
	TestEqual(TEXT("custom map count"), Source->MapValues.Num(), 1);
	TestEqual(TEXT("custom set count"), Source->SetValues.Num(), 1);
	if (Source->ArrayValues.Num() == 1)
	{
		TestEqual(TEXT("array selector applies"), Source->ArrayValues[0].Selector.SelectedKeyName, FName(TEXT("ArrayKey")));
		TestTrue(TEXT("array nested FInstancedStruct applies"), Source->ArrayValues[0].NestedValue.IsValid());
	}

	TSharedRef<FJsonObject> Extracted = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult =
		FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(Source, Extracted, TEXT("/Properties"));
	TestTrue(TEXT("custom containers extract"), ExtractResult.bSuccess);
	if (!ExtractResult.bSuccess)
	{
		return false;
	}

	const FString FirstCanonical = FAssetDocumentCanonicalJson::WriteCanonicalJson(
		MakeShared<FJsonValueObject>(Extracted));
	const FAssetDocumentCapabilityResult ReapplyResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Destination, Extracted, TEXT("/Properties"));
	TestTrue(TEXT("extracted custom containers reapply"), ReapplyResult.bSuccess);
	if (!ReapplyResult.bSuccess)
	{
		return false;
	}

	TSharedRef<FJsonObject> Reextracted = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ReextractResult =
		FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(Destination, Reextracted, TEXT("/Properties"));
	TestTrue(TEXT("reapplied custom containers extract"), ReextractResult.bSuccess);
	if (ReextractResult.bSuccess)
	{
		TestEqual(
			TEXT("custom container extract-apply-extract is canonical and idempotent"),
			FAssetDocumentCanonicalJson::WriteCanonicalJson(MakeShared<FJsonValueObject>(Reextracted)),
			FirstCanonical);
	}

	TSharedRef<FJsonObject> CollidingMap = MakeShared<FJsonObject>();
	CollidingMap->SetObjectField(
		TEXT("1"),
		MakeNestedValueJson(TEXT("MapByteA"), TEXT("First"), TEXT("FirstKey"), TEXT("FirstNested")));
	CollidingMap->SetObjectField(
		TEXT("01"),
		MakeNestedValueJson(TEXT("MapByteB"), TEXT("Second"), TEXT("SecondKey"), TEXT("SecondNested")));
	TSharedRef<FJsonObject> CollidingMapProperties = MakeShared<FJsonObject>();
	CollidingMapProperties->SetObjectField(TEXT("ByteMapValues"), CollidingMap);
	const FAssetDocumentCapabilityResult CollidingMapResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Source, CollidingMapProperties, TEXT("/Properties"));
	TestFalse(TEXT("semantic duplicate map keys are rejected"), CollidingMapResult.bSuccess);
	TestTrue(
		TEXT("semantic duplicate map keys report exact diagnostic"),
		HasDiagnostic(CollidingMapResult, TEXT("/Properties/ByteMapValues/1"), TEXT("DuplicateMapKey"))
			|| HasDiagnostic(CollidingMapResult, TEXT("/Properties/ByteMapValues/01"), TEXT("DuplicateMapKey")));
	TestEqual(TEXT("duplicate map failure preserves original map"), Source->ByteMapValues.Num(), 0);
	TestEqual(TEXT("duplicate map failure preserves unrelated map"), Source->MapValues.Num(), 1);
	TestTrue(TEXT("duplicate map failure preserves unrelated map key"), Source->MapValues.Contains(TEXT("MapA")));

	TArray<TSharedPtr<FJsonValue>> DuplicateSetValues;
	DuplicateSetValues.Add(MakeShared<FJsonValueObject>(
		MakeNestedValueJson(TEXT("Duplicate"), TEXT("First"), TEXT("FirstKey"), TEXT("FirstNested"))));
	DuplicateSetValues.Add(MakeShared<FJsonValueObject>(
		MakeNestedValueJson(TEXT("Duplicate"), TEXT("Second"), TEXT("SecondKey"), TEXT("SecondNested"))));
	TSharedRef<FJsonObject> DuplicateSetProperties = MakeShared<FJsonObject>();
	DuplicateSetProperties->SetArrayField(TEXT("SetValues"), MoveTemp(DuplicateSetValues));
	const FAssetDocumentCapabilityResult DuplicateSetResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Source, DuplicateSetProperties, TEXT("/Properties"));
	TestFalse(TEXT("duplicate set elements are rejected"), DuplicateSetResult.bSuccess);
	TestTrue(
		TEXT("duplicate set elements report exact diagnostic"),
		HasDiagnostic(DuplicateSetResult, TEXT("/Properties/SetValues/1"), TEXT("DuplicateSetElement")));
	TestEqual(TEXT("duplicate set failure preserves original set"), Source->SetValues.Num(), 1);
	FAssetDocumentReflectedPropertyNestedTestValue ExpectedSetValue;
	ExpectedSetValue.Identity = TEXT("SetA");
	TestTrue(
		TEXT("duplicate set failure preserves original identity"),
		Source->SetValues.Contains(ExpectedSetValue));

	TSharedRef<FJsonObject> InvalidByteMap = MakeShared<FJsonObject>();
	InvalidByteMap->SetObjectField(
		TEXT("12garbage"),
		MakeNestedValueJson(TEXT("Byte"), TEXT("Invalid"), TEXT("ByteKey"), TEXT("ByteNested")));
	TSharedRef<FJsonObject> InvalidByteMapProperties = MakeShared<FJsonObject>();
	InvalidByteMapProperties->SetObjectField(TEXT("ByteMapValues"), InvalidByteMap);
	const FAssetDocumentCapabilityResult InvalidByteMapResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Source, InvalidByteMapProperties, TEXT("/Properties"));
	TestFalse(TEXT("byte map key requires a strict in-range integer"), InvalidByteMapResult.bSuccess);
	TestTrue(
		TEXT("invalid byte map key reports exact diagnostic"),
		HasDiagnostic(InvalidByteMapResult, TEXT("/Properties/ByteMapValues/12garbage"), TEXT("InvalidMapKey")));
	TestEqual(TEXT("invalid byte map failure preserves original map"), Source->ByteMapValues.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeNativeClassReferenceTest,
	"AssetFactory.AssetDocument.BehaviorTree.ReflectedPropertyRuntime.NativeClassReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeNativeClassReferenceTest::RunTest(const FString&)
{
	UBTTask_SetKeyValueClass* Task = NewObject<UBTTask_SetKeyValueClass>(GetTransientPackage());
	TestNotNull(TEXT("native ClassRef test task exists"), Task);
	if (!Task)
	{
		return false;
	}

	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Path"), AActor::StaticClass()->GetPathName());
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("BaseClass"), ClassRef);

	const FAssetDocumentCapabilityResult ApplyResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Task, Properties, TEXT("/Properties"));
	TestTrue(TEXT("native ClassRef applies without generated-class probing"), ApplyResult.bSuccess);

	FClassProperty* BaseClassProperty = FindFProperty<FClassProperty>(Task->GetClass(), TEXT("BaseClass"));
	TestNotNull(TEXT("native ClassRef property exists"), BaseClassProperty);
	if (BaseClassProperty)
	{
		TestEqual(
			TEXT("native ClassRef resolves the exact script class"),
			Cast<UClass>(BaseClassProperty->GetObjectPropertyValue_InContainer(Task)),
			AActor::StaticClass());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreePlainContainerValidationAndAtomicityTest,
	"AssetFactory.AssetDocument.BehaviorTree.ReflectedPropertyRuntime.PlainContainerValidationAndAtomicity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreePlainContainerValidationAndAtomicityTest::RunTest(const FString&)
{
	using namespace AssetDocumentBehaviorTreeReflectedPropertyTests;

	UAssetDocumentReflectedPropertyTestObject* Object =
		NewObject<UAssetDocumentReflectedPropertyTestObject>(GetTransientPackage());
	TestNotNull(TEXT("plain-container test object exists"), Object);
	if (!Object)
	{
		return false;
	}

	Object->PlainByteMapValues.Add(7, 7.0f);
	Object->PlainNameSetValues.Add(FName(TEXT("Sentinel")));

	auto TestOriginalMapPreserved = [this, Object](const TCHAR* Context)
	{
		TestEqual(*FString::Printf(TEXT("%s preserves map count"), Context), Object->PlainByteMapValues.Num(), 1);
		const float* OriginalValue = Object->PlainByteMapValues.Find(7);
		TestNotNull(*FString::Printf(TEXT("%s preserves map key"), Context), OriginalValue);
		if (OriginalValue)
		{
			TestEqual(*FString::Printf(TEXT("%s preserves map value"), Context), *OriginalValue, 7.0f);
		}
	};

	auto TestOriginalSetPreserved = [this, Object](const TCHAR* Context)
	{
		TestEqual(*FString::Printf(TEXT("%s preserves set count"), Context), Object->PlainNameSetValues.Num(), 1);
		TestTrue(
			*FString::Printf(TEXT("%s preserves set element"), Context),
			Object->PlainNameSetValues.Contains(FName(TEXT("Sentinel"))));
	};

	TSharedRef<FJsonObject> DuplicateMap = MakeShared<FJsonObject>();
	DuplicateMap->SetNumberField(TEXT("1"), 1.0);
	DuplicateMap->SetNumberField(TEXT("01"), 2.0);
	TSharedRef<FJsonObject> DuplicateMapProperties = MakeShared<FJsonObject>();
	DuplicateMapProperties->SetObjectField(TEXT("PlainByteMapValues"), DuplicateMap);
	const FAssetDocumentCapabilityResult DuplicateMapResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, DuplicateMapProperties, TEXT("/Properties"));
	TestFalse(TEXT("plain byte map rejects semantic duplicate keys"), DuplicateMapResult.bSuccess);
	TestTrue(
		TEXT("plain byte map duplicate reports exact diagnostic"),
		HasDiagnostic(DuplicateMapResult, TEXT("/Properties/PlainByteMapValues/1"), TEXT("DuplicateMapKey"))
			|| HasDiagnostic(DuplicateMapResult, TEXT("/Properties/PlainByteMapValues/01"), TEXT("DuplicateMapKey")));
	TestOriginalMapPreserved(TEXT("duplicate map failure"));

	TSharedRef<FJsonObject> InvalidMap = MakeShared<FJsonObject>();
	InvalidMap->SetNumberField(TEXT("12garbage"), 12.0);
	TSharedRef<FJsonObject> InvalidMapProperties = MakeShared<FJsonObject>();
	InvalidMapProperties->SetObjectField(TEXT("PlainByteMapValues"), InvalidMap);
	const FAssetDocumentCapabilityResult InvalidMapResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, InvalidMapProperties, TEXT("/Properties"));
	TestFalse(TEXT("plain byte map rejects non-canonical numeric keys"), InvalidMapResult.bSuccess);
	TestTrue(
		TEXT("plain byte map invalid key reports exact diagnostic"),
		HasDiagnostic(InvalidMapResult, TEXT("/Properties/PlainByteMapValues/12garbage"), TEXT("InvalidMapKey")));
	TestOriginalMapPreserved(TEXT("invalid map failure"));

	TSharedRef<FJsonObject> LateFailingMap = MakeShared<FJsonObject>();
	LateFailingMap->SetNumberField(TEXT("1"), 1.0);
	LateFailingMap->SetStringField(TEXT("2"), TEXT("NotANumber"));
	TSharedRef<FJsonObject> LateFailingMapProperties = MakeShared<FJsonObject>();
	LateFailingMapProperties->SetObjectField(TEXT("PlainByteMapValues"), LateFailingMap);
	const FAssetDocumentCapabilityResult LateMapResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, LateFailingMapProperties, TEXT("/Properties"));
	TestFalse(TEXT("plain byte map rejects a late invalid value"), LateMapResult.bSuccess);
	TestTrue(
		TEXT("plain byte map late failure reports the value path"),
		HasDiagnostic(LateMapResult, TEXT("/Properties/PlainByteMapValues/2"), TEXT("InvalidPropertyValue")));
	TestOriginalMapPreserved(TEXT("late map failure"));

	TArray<TSharedPtr<FJsonValue>> DuplicateSet;
	DuplicateSet.Add(MakeShared<FJsonValueString>(TEXT("Duplicate")));
	DuplicateSet.Add(MakeShared<FJsonValueString>(TEXT("Duplicate")));
	TSharedRef<FJsonObject> DuplicateSetProperties = MakeShared<FJsonObject>();
	DuplicateSetProperties->SetArrayField(TEXT("PlainNameSetValues"), MoveTemp(DuplicateSet));
	const FAssetDocumentCapabilityResult DuplicateSetResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, DuplicateSetProperties, TEXT("/Properties"));
	TestFalse(TEXT("plain name set rejects duplicate elements"), DuplicateSetResult.bSuccess);
	TestTrue(
		TEXT("plain name set duplicate reports exact diagnostic"),
		HasDiagnostic(DuplicateSetResult, TEXT("/Properties/PlainNameSetValues/1"), TEXT("DuplicateSetElement")));
	TestOriginalSetPreserved(TEXT("duplicate set failure"));

	TArray<TSharedPtr<FJsonValue>> LateFailingSet;
	LateFailingSet.Add(MakeShared<FJsonValueString>(TEXT("Valid")));
	LateFailingSet.Add(MakeShared<FJsonValueNull>());
	TSharedRef<FJsonObject> LateFailingSetProperties = MakeShared<FJsonObject>();
	LateFailingSetProperties->SetArrayField(TEXT("PlainNameSetValues"), MoveTemp(LateFailingSet));
	const FAssetDocumentCapabilityResult LateSetResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Object, LateFailingSetProperties, TEXT("/Properties"));
	TestFalse(TEXT("plain name set rejects a late invalid element"), LateSetResult.bSuccess);
	TestTrue(
		TEXT("plain name set late failure reports the element path"),
		HasDiagnostic(LateSetResult, TEXT("/Properties/PlainNameSetValues/1"), TEXT("InvalidPropertyValue")));
	TestOriginalSetPreserved(TEXT("late set failure"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
