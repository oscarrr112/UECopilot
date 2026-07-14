// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentReflectedPropertyUtils.h"

#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
