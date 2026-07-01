# AssetDocument Object Field Schema Dispatcher Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a shared object field schema utility and migrate AnimSequence `Preview` / `Playback` plus WidgetBlueprint `Palette` / `EditorOptions` field validation to that utility without making `FAssetDocumentObjectRegionAdapter` own schema config.

**Architecture:** `FAssetDocumentObjectRegionAdapter` remains a lifecycle adapter only: it requires object shape and delegates to hooks. Each migrated profile declares `FAssetDocumentObjectFieldSchema` near its existing validation code and calls `FAssetDocumentObjectFieldSchemaUtils` from the hook or validation function. The utility owns JSON field allow-listing, field type checks, JSON Pointer path escaping, and diagnostic construction; asset-specific hooks keep UE materialization and semantic validation.

**Tech Stack:** Unreal Engine 5.7 C++ plugin code, UE JSON DOM, UE Automation Tests, AssetDocument public region runtime.

---

## Source Specs And Base

- Spec: `docs/superpowers/specs/2026-06-30-asset-document-object-field-schema-dispatcher-migration-design.md`
- Refactor chain: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
- Spec commit: `c089a3e docs: add asset document region refactor chain`
- Implementation branch: `feature/asset-document-object-field-schema-dispatcher-migration`
- Worktree path: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-object-field-schema-dispatcher-migration`

## Non-Negotiable Constraints

- Do not add optional schema config to `FAssetDocumentObjectRegionAdapter`.
- Do not add `FAssetDocumentBodyCapabilityBase` or any inheritance-based capability base.
- Do not change MCP TypeScript contracts.
- Do not migrate graph, tree, timeline, fragment, `ClassDefaults`, AnimMontage, or UBlueprint in this implementation.
- Preserve existing diagnostic codes for migrated profile behavior:
  - AnimSequence unsupported scalar fields keep `UnsupportedAuthoredField`.
  - AnimSequence invalid `RateScale` keeps `InvalidNumericField` or `InvalidRateScale`.
  - AnimSequence invalid `PreviewMesh` asset reference keeps the existing asset reference diagnostics.
  - WidgetBlueprint unknown Palette field keeps `UnknownPaletteField`.
  - WidgetBlueprint invalid Palette category keeps `InvalidPaletteCategory`.
  - WidgetBlueprint unknown EditorOptions field keeps `UnknownEditorOption`.
  - WidgetBlueprint invalid EditorOptions flag keeps `InvalidEditorOption`.
- Every task must start with `TASK_BASE=$(git rev-parse HEAD)` and must commit its own checkpoint before the next task.
- Review scopes must use `TASK_BASE..HEAD`, not `master..HEAD`.

## File Map

- Create: `Source/AssetDocument/Private/Regions/AssetDocumentObjectFieldSchemaUtils.h`
  - Declares `FAssetDocumentObjectFieldSpec`, `FAssetDocumentObjectFieldSchema`, and `FAssetDocumentObjectFieldSchemaUtils`.
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentObjectFieldSchemaUtils.cpp`
  - Implements schema validation, field path construction, duplicate field-spec detection, unknown-field rejection, required-field checks, and type mismatch checks.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`
  - Adds pure utility automation tests. These tests are intentionally in the runtime test file because the utility is part of the public region runtime layer and does not need a real asset.
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
  - Replaces profile-private field allow-list/type validation for `Preview` and `Playback` with schema utility calls while keeping asset semantic parsing in existing hooks.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`
  - Adds/updates tests that prove `Preview` and `Playback` still reject invalid fields/types with stable diagnostics.
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp`
  - Replaces `ValidatePaletteSection` and `ValidateEditorOptionsSection` manual loops with schema utility calls.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`
  - Keeps existing WidgetBlueprint metadata tests green and adds path assertions if existing tests only assert diagnostic code.
- Do not modify by default: `Source/AssetDocument/Private/Regions/AssetDocumentObjectRegionAdapter.h`
- Do not modify by default: `Source/AssetDocument/Private/Regions/AssetDocumentObjectRegionAdapter.cpp`

## Validation Commands

Use the validation host pattern required by this repo. If the worktree plugin is not already mounted in a validation host, create a temporary host project under `C:/Users/HP/.config/superpowers/validation-hosts/asset-document-object-field-schema` with a junction from `Plugins/UECopilot` to the current worktree. Do not replace the main project plugin.

Fast checks:

```powershell
git diff --check
```

UBT:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Preferred worktree validation host UBT, if a temporary host is prepared:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Automation filters:

```text
AssetFactory.AssetDocument.RegionRuntime
AssetFactory.AssetDocument.AnimSequence
AssetFactory.AssetDocument.WidgetBlueprint.Metadata
```

## Task 1: Add Object Field Schema Utility

**Files:**
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentObjectFieldSchemaUtils.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentObjectFieldSchemaUtils.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: only pre-existing unrelated untracked files in the main repo, or a clean isolated worktree.

- [ ] **Step 2: Write failing utility tests**

Append tests to `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp` after the helper functions and before `#endif`. Include the new header at the top:

```cpp
#include "Regions/AssetDocumentObjectFieldSchemaUtils.h"
```

Add tests with these exact behavior checks:

```cpp
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
```

- [ ] **Step 3: Run the focused test and confirm it fails to compile**

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Expected: compile fails because `Regions/AssetDocumentObjectFieldSchemaUtils.h` does not exist.

- [ ] **Step 4: Add the utility header**

Create `Source/AssetDocument/Private/Regions/AssetDocumentObjectFieldSchemaUtils.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

struct FAssetDocumentObjectFieldSpec
{
	FString Name;
	EJson Type = EJson::None;
	bool bRequired = false;
	FString MissingCode;
	FString TypeMismatchCode;
	FString TypeMismatchMessage;
};

struct FAssetDocumentObjectFieldSchema
{
	TArray<FAssetDocumentObjectFieldSpec> Fields;
	bool bRejectUnknownFields = true;
	FString UnknownFieldCode;
	FString UnknownFieldMessageFormat;
};

struct FAssetDocumentObjectFieldSchemaUtils
{
	static FAssetDocumentCapabilityResult ValidateObjectFields(
		const FAssetDocumentRegionContext& Context,
		const TSharedRef<FJsonObject>& Object,
		const FAssetDocumentObjectFieldSchema& Schema);

	static FString MakeFieldPath(
		const FAssetDocumentRegionContext& Context,
		const FString& FieldName);
};
```

- [ ] **Step 5: Add the utility implementation**

Create `Source/AssetDocument/Private/Regions/AssetDocumentObjectFieldSchemaUtils.cpp`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentObjectFieldSchemaUtils.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
FString RegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FString JsonTypeName(const EJson Type)
{
	switch (Type)
	{
	case EJson::None:
		return TEXT("none");
	case EJson::Null:
		return TEXT("null");
	case EJson::String:
		return TEXT("string");
	case EJson::Number:
		return TEXT("number");
	case EJson::Boolean:
		return TEXT("boolean");
	case EJson::Array:
		return TEXT("array");
	case EJson::Object:
		return TEXT("object");
	default:
		return TEXT("value");
	}
}

FString UnknownFieldMessage(const FAssetDocumentObjectFieldSchema& Schema, const FString& FieldName)
{
	if (!Schema.UnknownFieldMessageFormat.IsEmpty())
	{
		return FString::Printf(*Schema.UnknownFieldMessageFormat, *FieldName);
	}
	return FString::Printf(TEXT("Unknown object field '%s'"), *FieldName);
}

FString TypeMismatchMessage(
	const FAssetDocumentObjectFieldSpec& Field,
	const FAssetDocumentRegionContext& Context)
{
	if (!Field.TypeMismatchMessage.IsEmpty())
	{
		return Field.TypeMismatchMessage;
	}
	return FString::Printf(TEXT("%s.%s must be a %s"), *Context.BodyPath, *Field.Name, *JsonTypeName(Field.Type));
}
}

FString FAssetDocumentObjectFieldSchemaUtils::MakeFieldPath(
	const FAssetDocumentRegionContext& Context,
	const FString& FieldName)
{
	return FString::Printf(
		TEXT("%s/%s"),
		*RegionPath(Context),
		*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(FieldName));
}

FAssetDocumentCapabilityResult FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Object,
	const FAssetDocumentObjectFieldSchema& Schema)
{
	TMap<FString, FAssetDocumentObjectFieldSpec> FieldsByName;
	for (const FAssetDocumentObjectFieldSpec& Field : Schema.Fields)
	{
		if (Field.Name.IsEmpty() || Field.Type == EJson::None)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				RegionPath(Context),
				TEXT("InvalidObjectFieldSchema"),
				TEXT("Object field schema contains an invalid field spec"));
		}
		if (FieldsByName.Contains(Field.Name))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeFieldPath(Context, Field.Name),
				TEXT("InvalidObjectFieldSchema"),
				FString::Printf(TEXT("Object field schema contains duplicate field '%s'"), *Field.Name));
		}
		FieldsByName.Add(Field.Name, Field);
	}

	if (Schema.bRejectUnknownFields)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
		{
			if (!FieldsByName.Contains(Pair.Key))
			{
				return FAssetDocumentJsonRegionUtils::Failure(
					MakeFieldPath(Context, Pair.Key),
					Schema.UnknownFieldCode.IsEmpty() ? TEXT("UnknownObjectField") : Schema.UnknownFieldCode,
					UnknownFieldMessage(Schema, Pair.Key));
			}
		}
	}

	for (const FAssetDocumentObjectFieldSpec& Field : Schema.Fields)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field.Name);
		if (!Value.IsValid())
		{
			if (Field.bRequired)
			{
				return FAssetDocumentJsonRegionUtils::Failure(
					MakeFieldPath(Context, Field.Name),
					Field.MissingCode.IsEmpty() ? TEXT("MissingObjectField") : Field.MissingCode,
					FString::Printf(TEXT("%s.%s is required"), *Context.BodyPath, *Field.Name));
			}
			continue;
		}
		if (Value->Type != Field.Type)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeFieldPath(Context, Field.Name),
				Field.TypeMismatchCode.IsEmpty() ? TEXT("InvalidObjectFieldType") : Field.TypeMismatchCode,
				TypeMismatchMessage(Field, Context));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated object fields"));
}
```

- [ ] **Step 6: Run utility tests**

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Expected: compile succeeds.

If an editor automation runner is available, run:

```text
AssetFactory.AssetDocument.RegionRuntime.ObjectFieldSchema
```

Expected: all new object field schema tests pass.

- [ ] **Step 7: Commit Task 1**

```powershell
git diff --check
git add Source/AssetDocument/Private/Regions/AssetDocumentObjectFieldSchemaUtils.h Source/AssetDocument/Private/Regions/AssetDocumentObjectFieldSchemaUtils.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat: add asset document object field schema utility"
```

Expected: one checkpoint commit containing only the utility and utility tests.

## Task 2: Migrate AnimSequence Preview Field Schema

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree after Task 1 commit.

- [ ] **Step 2: Add a failing Preview schema regression test**

In `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`, extend the existing scalar validation test that already checks invalid Preview and Playback cases. Add a case that proves unknown Preview fields still fail at `/Body/Preview/<Field>` with `UnsupportedAuthoredField`:

```cpp
	TSharedRef<FJsonObject> UnknownPreviewFieldBody = MakePlaybackRateBody(4.6);
	TSharedRef<FJsonObject> UnknownPreview = MakeShared<FJsonObject>();
	UnknownPreview->SetStringField(TEXT("PreviewMesh/Bad~Field"), TEXT("unexpected"));
	UnknownPreviewFieldBody->SetObjectField(TEXT("Preview"), UnknownPreview);
	const FAssetDocumentCapabilityResult UnknownPreviewResult =
		Capability.Validate(Context, MakeBodyValue(UnknownPreviewFieldBody));
	TestFalse(TEXT("Validate rejects unknown Body.Preview field"), UnknownPreviewResult.bSuccess);
	TestTrue(
		TEXT("Unknown Body.Preview field diagnostic uses escaped field path"),
		HasDiagnostic(UnknownPreviewResult, TEXT("/Body/Preview/PreviewMesh~1Bad~0Field"), TEXT("UnsupportedAuthoredField")));
```

Run compile or the focused automation. Expected before implementation: this may already pass through the old private helper. Keep it because it protects diagnostic compatibility during migration.

- [ ] **Step 3: Include the utility**

In `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`, add:

```cpp
#include "Regions/AssetDocumentObjectFieldSchemaUtils.h"
```

near the other `Regions/...` includes.

- [ ] **Step 4: Add a helper for AnimSequence object field schema validation**

Near existing `RejectUnknownObjectFields`, add this helper:

```cpp
FAssetDocumentCapabilityResult ValidateAnimSequenceObjectFieldSchema(
	const TSharedRef<FJsonObject>& BodyObject,
	const TCHAR* SectionName,
	const FAssetDocumentObjectFieldSchema& Schema)
{
	const TSharedPtr<FJsonObject>* SectionObject = nullptr;
	if (!BodyObject->TryGetObjectField(SectionName, SectionObject) || !SectionObject || !SectionObject->IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentRegionContext RegionContext;
	RegionContext.RegionId = *FString::Printf(TEXT("Body.%s"), SectionName);
	RegionContext.BodyPath = FString::Printf(TEXT("Body.%s"), SectionName);
	RegionContext.JsonPointer = FString::Printf(TEXT("/Body/%s"), SectionName);
	return FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(
		RegionContext,
		(*SectionObject).ToSharedRef(),
		Schema);
}
```

- [ ] **Step 5: Replace Preview unknown field validation with schema utility**

In `ValidateBodyObjectShape`, replace:

```cpp
RejectUnknownObjectFields(BodyObject, TEXT("Preview"), { TEXT("PreviewMesh") }),
```

with:

```cpp
ValidateAnimSequenceObjectFieldSchema(
	BodyObject,
	TEXT("Preview"),
	FAssetDocumentObjectFieldSchema{
		{
			{TEXT("PreviewMesh"), EJson::Object, false, TEXT("MissingPreviewMesh"), TEXT("InvalidObjectReference")},
		},
		true,
		TEXT("UnsupportedAuthoredField"),
		TEXT("Body.Preview.%s is not supported by the AnimSequence Task 2 scalar capability")
	}),
```

If the compiler rejects aggregate initialization because of UE or MSVC constraints, use a local `MakeAnimSequencePreviewSchema()` function returning `FAssetDocumentObjectFieldSchema` instead. Preserve the same field list, codes, and message format.

- [ ] **Step 6: Run focused validation**

```powershell
git diff --check
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Run automation filter:

```text
AssetFactory.AssetDocument.AnimSequence
```

Expected: AnimSequence tests pass; unknown Preview path remains escaped and diagnostic code remains `UnsupportedAuthoredField`.

- [ ] **Step 7: Commit Task 2**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "refactor: migrate anim sequence preview field schema"
```

Expected: one checkpoint commit containing only AnimSequence Preview schema migration and test updates.

## Task 3: Migrate AnimSequence Playback Field Schema

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree after Task 2 commit.

- [ ] **Step 2: Add Playback schema regression tests**

In `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`, extend the existing invalid scalar validation section with:

```cpp
	TSharedRef<FJsonObject> UnknownPlaybackFieldBody = MakePlaybackRateBody(4.7);
	UnknownPlaybackFieldBody->GetObjectField(TEXT("Playback"))->SetStringField(TEXT("RateScale/Bad~Field"), TEXT("unexpected"));
	const FAssetDocumentCapabilityResult UnknownPlaybackResult =
		Capability.Validate(Context, MakeBodyValue(UnknownPlaybackFieldBody));
	TestFalse(TEXT("Validate rejects unknown Body.Playback field"), UnknownPlaybackResult.bSuccess);
	TestTrue(
		TEXT("Unknown Body.Playback field diagnostic uses escaped field path"),
		HasDiagnostic(UnknownPlaybackResult, TEXT("/Body/Playback/RateScale~1Bad~0Field"), TEXT("UnsupportedAuthoredField")));

	TSharedRef<FJsonObject> InvalidPlaybackRateTypeBody = MakePlaybackRateBody(4.8);
	InvalidPlaybackRateTypeBody->GetObjectField(TEXT("Playback"))->SetStringField(TEXT("RateScale"), TEXT("fast"));
	const FAssetDocumentCapabilityResult InvalidPlaybackRateTypeResult =
		Capability.Validate(Context, MakeBodyValue(InvalidPlaybackRateTypeBody));
	TestFalse(TEXT("Validate rejects non-number Body.Playback.RateScale"), InvalidPlaybackRateTypeResult.bSuccess);
	TestTrue(
		TEXT("Invalid Body.Playback.RateScale diagnostic is stable"),
		HasDiagnostic(InvalidPlaybackRateTypeResult, TEXT("/Body/Playback/RateScale"), TEXT("InvalidNumericField")));
```

Run focused validation. Expected before migration: these should already pass or fail only on path escaping; after migration both must pass.

- [ ] **Step 3: Replace Playback unknown/type validation with schema utility**

In `ValidateBodyObjectShape`, replace:

```cpp
RejectUnknownObjectFields(BodyObject, TEXT("Playback"), { TEXT("RateScale") }),
```

with:

```cpp
ValidateAnimSequenceObjectFieldSchema(
	BodyObject,
	TEXT("Playback"),
	FAssetDocumentObjectFieldSchema{
		{
			{TEXT("RateScale"), EJson::Number, false, TEXT("MissingRateScale"), TEXT("InvalidNumericField"), TEXT("RateScale must be a number")},
		},
		true,
		TEXT("UnsupportedAuthoredField"),
		TEXT("Body.Playback.%s is not supported by the AnimSequence Task 2 scalar capability")
	}),
```

Keep `RejectUnsupportedAuthoredFields` before schema validation so derived extract-only fields such as `PlayLength`, `NumberOfSampledKeys`, and `SamplingFrameRate` still return `UnsupportedAuthoredField` with their existing message.

- [ ] **Step 4: Remove duplicate Playback type validation only if it becomes redundant**

`ParseAnimSequencePlaybackRegion` currently calls `ReadOptionalNumber` for `RateScale`. Keep it unless removing it is proven by tests to preserve all `Apply`, `Validate`, and `Diff` behavior. The safer first implementation keeps it as semantic parser validation; schema validation is the shared front door.

- [ ] **Step 5: Run focused validation**

```powershell
git diff --check
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Run automation filter:

```text
AssetFactory.AssetDocument.AnimSequence
```

Expected: AnimSequence tests pass, including existing scalar diff/apply tests and new Playback diagnostics.

- [ ] **Step 6: Commit Task 3**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "refactor: migrate anim sequence playback field schema"
```

Expected: one checkpoint commit containing only AnimSequence Playback schema migration and test updates.

## Task 4: Migrate WidgetBlueprint Palette And EditorOptions

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree after Task 3 commit.

- [ ] **Step 2: Strengthen WidgetBlueprint diagnostics tests**

In `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`, locate `FAssetDocumentWidgetBlueprintMetadataRejectsInvalidPaletteEditorOptionsTest`. Keep its code assertions and add path assertions next to each diagnostic code assertion:

```cpp
TestTrue(TEXT("Unknown Palette diagnostic path is reported"), ResultHasDiagnostic(PaletteUnknownResult, TEXT("/Body/Palette/Unexpected"), TEXT("UnknownPaletteField")));
TestTrue(TEXT("Invalid Palette.Category diagnostic path is reported"), ResultHasDiagnostic(PaletteTypeResult, TEXT("/Body/Palette/Category"), TEXT("InvalidPaletteCategory")));
TestTrue(TEXT("Invalid EditorOptions diagnostic path is reported"), ResultHasDiagnostic(EditorTypeResult, TEXT("/Body/EditorOptions/bCanCallInitializedWithoutPlayerContext"), TEXT("InvalidEditorOption")));
```

If the file does not have `ResultHasDiagnostic`, add a local helper near `ResultHasDiagnosticCode`:

```cpp
bool ResultHasDiagnostic(const FAssetDocumentResult& Result, const FString& Path, const FString& Code)
{
	for (const FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Path == Path && Diagnostic.Code == Code)
		{
			return true;
		}
	}
	return false;
}
```

- [ ] **Step 3: Include the utility**

In `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp`, add:

```cpp
#include "Regions/AssetDocumentObjectFieldSchemaUtils.h"
```

near the other `Regions/...` includes.

- [ ] **Step 4: Replace Palette validation loop with schema utility**

Replace the body of `ValidatePaletteSection` with:

```cpp
	if (!Palette.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentRegionContext RegionContext;
	RegionContext.RegionId = TEXT("Body.Palette");
	RegionContext.BodyPath = TEXT("Body.Palette");
	RegionContext.JsonPointer = TEXT("/Body/Palette");

	FAssetDocumentObjectFieldSchema Schema;
	Schema.UnknownFieldCode = TEXT("UnknownPaletteField");
	Schema.UnknownFieldMessageFormat = TEXT("Unknown Body.Palette field '%s'");
	Schema.Fields.Add({
		TEXT("Category"),
		EJson::String,
		false,
		TEXT("MissingPaletteCategory"),
		TEXT("InvalidPaletteCategory"),
		TEXT("Body.Palette.Category must be a string")
	});
	return FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(
		RegionContext,
		Palette.ToSharedRef(),
		Schema);
```

- [ ] **Step 5: Replace EditorOptions validation loop with schema utility**

Replace the body of `ValidateEditorOptionsSection` with:

```cpp
	if (!EditorOptions.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentRegionContext RegionContext;
	RegionContext.RegionId = TEXT("Body.EditorOptions");
	RegionContext.BodyPath = TEXT("Body.EditorOptions");
	RegionContext.JsonPointer = TEXT("/Body/EditorOptions");

	FAssetDocumentObjectFieldSchema Schema;
	Schema.UnknownFieldCode = TEXT("UnknownEditorOption");
	Schema.UnknownFieldMessageFormat = TEXT("Unknown Body.EditorOptions field '%s'");
	Schema.Fields.Add({
		TEXT("bCanCallInitializedWithoutPlayerContext"),
		EJson::Boolean,
		false,
		TEXT("MissingEditorOption"),
		TEXT("InvalidEditorOption"),
		TEXT("Body.EditorOptions.bCanCallInitializedWithoutPlayerContext must be a boolean")
	});
	return FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(
		RegionContext,
		EditorOptions.ToSharedRef(),
		Schema);
```

- [ ] **Step 6: Run focused validation**

```powershell
git diff --check
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Run automation filter:

```text
AssetFactory.AssetDocument.WidgetBlueprint.Metadata
```

Expected: WidgetBlueprint metadata tests pass and diagnostics remain stable.

- [ ] **Step 7: Commit Task 4**

```powershell
git add Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp
git commit -m "refactor: migrate widget blueprint metadata field schema"
```

Expected: one checkpoint commit containing only WidgetBlueprint Palette/EditorOptions schema migration and test updates.

## Final Verification

- [ ] **Step 1: Confirm adapter boundary**

```powershell
rg -n "optional schema|ObjectRegionAdapter.*Schema|Schema.*ObjectRegionAdapter|FAssetDocumentObjectRegionAdapter.*FieldSchema" Source/AssetDocument/Private/Regions Source/AssetDocument/Private/Profiles
```

Expected: no result showing schema config in `FAssetDocumentObjectRegionAdapter`. Mentions inside profile hooks or tests are acceptable.

- [ ] **Step 2: Run full checks**

```powershell
git diff --check
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Run automation filters:

```text
AssetFactory.AssetDocument.RegionRuntime
AssetFactory.AssetDocument.AnimSequence
AssetFactory.AssetDocument.WidgetBlueprint.Metadata
```

If time allows before final review, run:

```text
AssetFactory.AssetDocument
```

- [ ] **Step 3: Final review scope**

Use:

```powershell
git log --oneline c089a3e..HEAD
git diff --stat c089a3e..HEAD
```

Expected: plan commit plus four implementation commits. Review only this spec slice and do not include unrelated untracked files.

## Subagent Execution Notes

- Implement one task at a time.
- Do not dispatch multiple implementation subagents in parallel because Tasks 2 and 3 both modify `AnimSequenceAssetDocumentCapability.cpp` and `AssetDocumentAnimSequenceTests.cpp`.
- Each implementer subagent owns only the files listed in its task.
- After each implementation task, run a spec-compliance review over `TASK_BASE..HEAD`, then a code-quality review over the same range.
- If a reviewer finds issues, send the same implementer a fix request and re-review the same diff range.
- Do not start Task 4 until Task 3 is committed and reviewed.
