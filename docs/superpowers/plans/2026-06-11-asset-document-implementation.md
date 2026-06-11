# AssetDocument Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone C++ AssetDocument pipeline that can apply, inspect, extract, validate, diff, and auto-apply `.assetdoc.json` sidecars for reflected UObject/DataAsset-style assets.

**Architecture:** Add a new `AssetDocument` editor module with `FAssetDocumentService` as the stable facade. `AssetFactory` exposes a thin HTTP route extension point; `AssetDocument` depends on existing `AssetFactory` reflection utilities and registers its own routes without adding a generator or duplicating reflection/type logic in MCP.

**Tech Stack:** Unreal Engine 5.7 editor module C++, UE reflection (`FProperty`), `FPropertySetterUtils`, `FClassFinderUtils`, `AssetRegistry`, `DirectoryWatcher`, existing `AssetFactoryHttpServer`, MCP TypeScript server, UE automation tests.

---

## Implementation Context

**Worktree:** `C:/Users/HP/.config/superpowers/worktrees/UECopilot/asset-document-generic-asset`

**Branch:** `feature/asset-document-generic-asset`

**Spec:** `docs/superpowers/specs/2026-06-11-generic-asset-document-design.md`

**Current spec checkpoint:** `0bb8976 docs: add asset document file watcher`

**Core constraints:**

- Do not add `GenericAssetGenerator`.
- Do not add `GenericAsset` to `AssetGeneratorRegistry`.
- Do not route AssetDocument through `generate_assets`.
- Do not implement class/property logic in MCP TypeScript.
- `Target` is required in sidecar files and must match the sidecar path.
- `Validate`, `Inspect`, `Extract`, and `Diff` must be read-only.
- `Apply` / `ApplyFile` are the only write entry points.

## File Structure

Create a focused module:

- `Source/AssetDocument/AssetDocument.Build.cs`: module dependencies.
- `Source/AssetDocument/Public/AssetDocumentModule.h`: module interface and log category.
- `Source/AssetDocument/Private/AssetDocumentModule.cpp`: module startup/shutdown, service lifetime, watcher lifetime.
- `Source/AssetDocument/Public/AssetDocumentTypes.h`: request/result/diagnostic structs.
- `Source/AssetDocument/Public/AssetDocumentService.h`: facade API.
- `Source/AssetDocument/Private/AssetDocumentService.cpp`: orchestration for apply, apply-file, inspect, extract, validate, diff.
- `Source/AssetDocument/Private/AssetDocumentJson.h/.cpp`: parse/serialize JSON document and result objects.
- `Source/AssetDocument/Private/AssetDocumentSidecar.h/.cpp`: sidecar path resolution, `Target` validation, read/write.
- `Source/AssetDocument/Private/AssetDocumentClassResolver.h/.cpp`: dynamic class resolution using existing helpers and UE loading fallback.
- `Source/AssetDocument/Private/AssetDocumentLifecycle.h/.cpp`: create/load/save UObject assets.
- `Source/AssetDocument/Private/AssetDocumentPropertyAdapter.h/.cpp`: reflected property apply, inspect, extract, diff.
- `Source/AssetDocument/Private/AssetDocumentFileWatcher.h/.cpp`: editor-only file watcher with debounce/suppression.
- `Source/AssetDocument/Private/Tests/AssetDocumentServiceTests.cpp`: automation tests for C++ service.

Modify existing integration points:

- `AssetFactory.uplugin`: add `AssetDocument` editor module.
- `Source/AssetFactory/Public/AssetFactoryHttpServer.h`: add a generic external route registration API.
- `Source/AssetFactory/Private/AssetFactoryHttpServer.cpp`: bind and unbind externally registered routes.
- `Source/AssetDocument/Private/AssetDocumentHttpRoutes.h/.cpp`: register `/assetfactory/assetdocument/*` routes and forward to `FAssetDocumentService`.
- `MCP/src/index.ts`: add MCP tools that call `/assetfactory/assetdocument/*`.
- `MCP/src/broker/toolCatalog.ts`: expose AssetDocument tools.
- `MCP/schemas/AssetDocument.md`: tool and document schema.
- `MCP/dist/*`: update generated build output after `npm run build`.

---

### Task 1: Module Skeleton And Public API

**Files:**
- Modify: `AssetFactory.uplugin`
- Create: `Source/AssetDocument/AssetDocument.Build.cs`
- Create: `Source/AssetDocument/Public/AssetDocumentModule.h`
- Create: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Create: `Source/AssetDocument/Public/AssetDocumentTypes.h`
- Create: `Source/AssetDocument/Public/AssetDocumentService.h`
- Create: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Test: UBT compile

- [ ] **Step 1: Add the module to `AssetFactory.uplugin`**

Add this module entry after `AssetFactory`, so `AssetDocument` can reuse `AssetFactory` utilities and the HTTP route extension point:

```json
{
	"Name": "AssetDocument",
	"Type": "Editor",
	"LoadingPhase": "Default"
}
```

- [ ] **Step 2: Create `Source/AssetDocument/AssetDocument.Build.cs`**

```csharp
// Copyright ProjectRPG. All Rights Reserved.

using UnrealBuildTool;

public class AssetDocument : ModuleRules
{
	public AssetDocument(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities",
			"AssetFactory"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetRegistry",
			"AssetTools",
			"DirectoryWatcher",
			"Projects"
		});
	}
}
```

- [ ] **Step 3: Create `AssetDocumentTypes.h` with stable request/result contracts**

Use these names exactly; later tasks fill helper fields as needed without renaming the facade contract.

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

enum class EAssetDocumentResultStatus : uint8
{
	Success,
	Failed,
	PartialFailed
};

struct ASSETDOCUMENT_API FAssetDocumentDiagnostic
{
	FString Path;
	FString Code;
	FString Message;
};

struct ASSETDOCUMENT_API FAssetDocumentApplyRequest
{
	TSharedPtr<FJsonObject> Document;
	bool bWriteSidecar = false;
	bool bSaveAsset = true;
};

struct ASSETDOCUMENT_API FAssetDocumentApplyFileRequest
{
	FString FilePath;
	bool bSaveAsset = true;
	bool bAllowSidecarRewrite = true;
	bool bTriggeredByWatcher = false;
};

struct ASSETDOCUMENT_API FAssetDocumentInspectRequest
{
	FString ClassOrAsset;
};

struct ASSETDOCUMENT_API FAssetDocumentExtractRequest
{
	FString AssetPath;
	bool bDiffOnly = true;
	bool bIncludeAllWritable = false;
};

struct ASSETDOCUMENT_API FAssetDocumentValidateRequest
{
	TSharedPtr<FJsonObject> Document;
	FString FilePath;
};

struct ASSETDOCUMENT_API FAssetDocumentDiffRequest
{
	TSharedPtr<FJsonObject> Document;
	FString FilePath;
};

struct ASSETDOCUMENT_API FAssetDocumentResult
{
	EAssetDocumentResultStatus Status = EAssetDocumentResultStatus::Failed;
	FString Message;
	FString Target;
	FString SidecarFilePath;
	FString AssetPath;
	bool bSavedAsset = false;
	bool bWroteSidecar = false;
	TArray<FAssetDocumentDiagnostic> Diagnostics;
	TSharedPtr<FJsonObject> Payload;

	static FAssetDocumentResult Success(const FString& InMessage);
	static FAssetDocumentResult Failure(const FString& InMessage);
	bool IsSuccess() const { return Status == EAssetDocumentResultStatus::Success; }
	TSharedPtr<FJsonObject> ToJson() const;
};
```

- [ ] **Step 4: Create `AssetDocumentService.h`**

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDocumentTypes.h"

class ASSETDOCUMENT_API FAssetDocumentService
{
public:
	FAssetDocumentResult Apply(const FAssetDocumentApplyRequest& Request);
	FAssetDocumentResult ApplyFile(const FAssetDocumentApplyFileRequest& Request);
	FAssetDocumentResult Inspect(const FAssetDocumentInspectRequest& Request) const;
	FAssetDocumentResult Extract(const FAssetDocumentExtractRequest& Request) const;
	FAssetDocumentResult Validate(const FAssetDocumentValidateRequest& Request) const;
	FAssetDocumentResult Diff(const FAssetDocumentDiffRequest& Request) const;
};
```

- [ ] **Step 5: Create module class and singleton service access**

`AssetDocumentModule.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAssetDocument, Log, All);

class FAssetDocumentService;

class ASSETDOCUMENT_API FAssetDocumentModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FAssetDocumentModule& Get();
	static bool IsAvailable();

	FAssetDocumentService& GetService();

private:
	TUniquePtr<FAssetDocumentService> Service;
};
```

`AssetDocumentModule.cpp`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentModule.h"
#include "AssetDocumentService.h"

DEFINE_LOG_CATEGORY(LogAssetDocument);

void FAssetDocumentModule::StartupModule()
{
	UE_LOG(LogAssetDocument, Log, TEXT("AssetDocument module starting up"));
	Service = MakeUnique<FAssetDocumentService>();
}

void FAssetDocumentModule::ShutdownModule()
{
	Service.Reset();
	UE_LOG(LogAssetDocument, Log, TEXT("AssetDocument module shut down"));
}

FAssetDocumentModule& FAssetDocumentModule::Get()
{
	return FModuleManager::LoadModuleChecked<FAssetDocumentModule>(TEXT("AssetDocument"));
}

bool FAssetDocumentModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("AssetDocument"));
}

FAssetDocumentService& FAssetDocumentModule::GetService()
{
	check(Service.IsValid());
	return *Service;
}

IMPLEMENT_MODULE(FAssetDocumentModule, AssetDocument)
```

- [ ] **Step 6: Add stub service implementation**

Create `AssetDocumentService.cpp` with methods returning explicit unsupported failures. This confirms module/linking before behavior.

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"
#include "AssetDocumentModule.h"

FAssetDocumentResult FAssetDocumentResult::Success(const FString& InMessage)
{
	FAssetDocumentResult Result;
	Result.Status = EAssetDocumentResultStatus::Success;
	Result.Message = InMessage;
	return Result;
}

FAssetDocumentResult FAssetDocumentResult::Failure(const FString& InMessage)
{
	FAssetDocumentResult Result;
	Result.Status = EAssetDocumentResultStatus::Failed;
	Result.Message = InMessage;
	return Result;
}

TSharedPtr<FJsonObject> FAssetDocumentResult::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("success"), IsSuccess());
	Json->SetStringField(TEXT("message"), Message);
	Json->SetStringField(TEXT("target"), Target);
	Json->SetStringField(TEXT("asset_path"), AssetPath);
	Json->SetStringField(TEXT("sidecar_file_path"), SidecarFilePath);
	Json->SetBoolField(TEXT("saved_asset"), bSavedAsset);
	Json->SetBoolField(TEXT("wrote_sidecar"), bWroteSidecar);
	if (Payload.IsValid())
	{
		Json->SetObjectField(TEXT("payload"), Payload);
	}
	return Json;
}

FAssetDocumentResult FAssetDocumentService::Apply(const FAssetDocumentApplyRequest&)
{
	return FAssetDocumentResult::Failure(TEXT("AssetDocument Apply is not implemented"));
}

FAssetDocumentResult FAssetDocumentService::ApplyFile(const FAssetDocumentApplyFileRequest&)
{
	return FAssetDocumentResult::Failure(TEXT("AssetDocument ApplyFile is not implemented"));
}

FAssetDocumentResult FAssetDocumentService::Inspect(const FAssetDocumentInspectRequest&) const
{
	return FAssetDocumentResult::Failure(TEXT("AssetDocument Inspect is not implemented"));
}

FAssetDocumentResult FAssetDocumentService::Extract(const FAssetDocumentExtractRequest&) const
{
	return FAssetDocumentResult::Failure(TEXT("AssetDocument Extract is not implemented"));
}

FAssetDocumentResult FAssetDocumentService::Validate(const FAssetDocumentValidateRequest&) const
{
	return FAssetDocumentResult::Failure(TEXT("AssetDocument Validate is not implemented"));
}

FAssetDocumentResult FAssetDocumentService::Diff(const FAssetDocumentDiffRequest&) const
{
	return FAssetDocumentResult::Failure(TEXT("AssetDocument Diff is not implemented"));
}
```

- [ ] **Step 7: Run UBT**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: compile succeeds or, when validating the isolated worktree, use the temporary host project described in Task 10.

- [ ] **Step 8: Commit**

```powershell
git add AssetFactory.uplugin Source/AssetDocument
git commit -m "feat: add asset document module skeleton"
```

---

### Task 2: Sidecar Path Resolution And Document Parsing

**Files:**
- Create: `Source/AssetDocument/Private/AssetDocumentSidecar.h`
- Create: `Source/AssetDocument/Private/AssetDocumentSidecar.cpp`
- Create: `Source/AssetDocument/Private/AssetDocumentJson.h`
- Create: `Source/AssetDocument/Private/AssetDocumentJson.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentServiceTests.cpp`

- [ ] **Step 1: Add automation tests for target requirements**

Create `AssetDocumentServiceTests.cpp`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AssetDocumentService.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentMissingTargetTest,
	"AssetFactory.AssetDocument.Sidecar.MissingTargetFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentMissingTargetTest::RunTest(const FString&)
{
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Doc = MakeShared<FJsonObject>();
	Doc->SetNumberField(TEXT("SchemaVersion"), 1);
	Doc->SetStringField(TEXT("AssetType"), TEXT("GenericAsset"));

	FAssetDocumentValidateRequest Request;
	Request.Document = Doc;
	Request.FilePath = FPaths::ProjectContentDir() / TEXT("Data/DA_Test.assetdoc.json");

	const FAssetDocumentResult Result = Service.Validate(Request);
	TestFalse(TEXT("Missing Target fails"), Result.IsSuccess());
	TestTrue(TEXT("Error mentions Target"), Result.Message.Contains(TEXT("Target")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentTargetMismatchTest,
	"AssetFactory.AssetDocument.Sidecar.TargetMismatchFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentTargetMismatchTest::RunTest(const FString&)
{
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Doc = MakeShared<FJsonObject>();
	Doc->SetNumberField(TEXT("SchemaVersion"), 1);
	Doc->SetStringField(TEXT("AssetType"), TEXT("GenericAsset"));
	Doc->SetStringField(TEXT("Target"), TEXT("/Game/Data/DA_Other"));

	FAssetDocumentValidateRequest Request;
	Request.Document = Doc;
	Request.FilePath = FPaths::ProjectContentDir() / TEXT("Data/DA_Test.assetdoc.json");

	const FAssetDocumentResult Result = Service.Validate(Request);
	TestFalse(TEXT("Mismatched Target fails"), Result.IsSuccess());
	TestTrue(TEXT("Error mentions mismatch"), Result.Message.Contains(TEXT("does not match")));
	return true;
}
```

- [ ] **Step 2: Run tests and verify they fail**

Run automation through the temporary validation host when available:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<TempHostProject>.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.Sidecar;Quit" -unattended -nop4 -nosplash -nullrhi -log
```

Expected before implementation: tests fail because `Validate` is still a stub.

- [ ] **Step 3: Implement sidecar helpers**

`AssetDocumentSidecar.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FAssetDocumentSidecar
{
public:
	static FString ResolveObjectPathFromSidecar(const FString& FilePath);
	static FString ResolveSidecarPathFromObjectPath(const FString& ObjectPath);
	static bool LoadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJson, FString& OutError);
	static bool WriteJsonFile(const FString& FilePath, const TSharedPtr<FJsonObject>& Json, FString& OutError);
	static bool ValidateTargetMatchesSidecar(const FString& FilePath, const TSharedPtr<FJsonObject>& Json, FString& OutError);
};
```

`ValidateTargetMatchesSidecar` behavior:

- fail when `FilePath` is non-empty and `Target` is missing;
- compute `/Game/.../<AssetName>` from `<Project>/Content/.../<AssetName>.assetdoc.json`;
- fail when computed path differs from `Target`;
- succeed when no `FilePath` is supplied because inline documents may use `Name` / `Path`.

- [ ] **Step 4: Implement JSON file read/write**

Use `FFileHelper::LoadFileToString`, `TJsonReaderFactory<>::Create`, `FJsonSerializer::Deserialize`, `FJsonSerializer::Serialize`, and `FFileHelper::SaveStringToFile`.

Output JSON is canonical, UTF-8, and pretty printed using `TPrettyJsonPrintPolicy<TCHAR>`.

- [ ] **Step 5: Wire `Validate` to sidecar validation**

In `FAssetDocumentService::Validate`:

- fail when both `Document` and `FilePath` are empty;
- load JSON from file when `Document` is null and `FilePath` is present;
- require `SchemaVersion == 1`;
- require `AssetType == "GenericAsset"`;
- call `ValidateTargetMatchesSidecar`;
- return success with payload containing normalized `target` and `sidecar_file_path`.

- [ ] **Step 6: Run tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<TempHostProject>.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.Sidecar;Quit" -unattended -nop4 -nosplash -nullrhi -log
```

Expected: sidecar target tests pass.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument
git commit -m "feat: validate asset document sidecars"
```

---

### Task 3: Class Resolution, Lifecycle, And Apply

**Files:**
- Create: `Source/AssetDocument/Private/AssetDocumentClassResolver.h/.cpp`
- Create: `Source/AssetDocument/Private/AssetDocumentLifecycle.h/.cpp`
- Create: `Source/AssetDocument/Private/AssetDocumentPropertyAdapter.h/.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentServiceTests.cpp`

- [ ] **Step 1: Add failing apply tests**

Add tests covering:

- invalid class fails;
- abstract class fails;
- create `TestDataAsset` succeeds;
- update one property leaves unspecified properties unchanged;
- typed `type` with subtype such as `Object:StaticMesh` fails.

Use asset paths under `/Game/AssetDocumentTests/`.

- [ ] **Step 2: Run tests and verify failure**

Run automation:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<TempHostProject>.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.Apply;Quit" -unattended -nop4 -nosplash -nullrhi -log
```

Expected: tests fail because apply is not implemented.

- [ ] **Step 3: Implement `FAssetDocumentClassResolver`**

Resolution order:

1. `FClassFinderUtils` for simple names where possible.
2. `StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath)`.
3. `FindObject<UClass>(nullptr, *ClassPath)` fallback.

Reject:

- null class;
- abstract class;
- deprecated/newer-version-only class;
- non-`UObject`;
- `UClass`, `UPackage`, `UWorld`;
- `UBlueprint`, `UMaterial` for first version.

- [ ] **Step 4: Implement lifecycle**

`CreateOrLoad` accepts `Target`, `Class`, and action:

- `Create`: fail if target already exists;
- `Update`: fail if target missing;
- `CreateOrUpdate`: load or create;
- create package with `CreatePackage(*Target)`;
- create asset with `NewObject<UObject>(Package, ResolvedClass, AssetName, RF_Public | RF_Standalone)`;
- call `FAssetRegistryModule::AssetCreated(Asset)` for new assets.

- [ ] **Step 5: Implement property adapter**

Rules:

- detect typed object values by presence of `type` and `value`;
- reject typed `type` strings containing `:` for sidecar v1;
- for typed values, verify property exists and use target `FProperty` for constraints;
- for untyped values, call `FPropertySetterUtils::SetPropertyFromJson`;
- for multiple fields, preflight on a transient duplicate before applying to real asset;
- return per-property diagnostics.

- [ ] **Step 6: Implement save path**

Save only after successful apply:

```cpp
Asset->MarkPackageDirty();
UPackage* Package = Asset->GetOutermost();
const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
FSavePackageArgs SaveArgs;
SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
const bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
```

Return failed when save fails.

- [ ] **Step 7: Run tests**

Expected: apply tests pass.

- [ ] **Step 8: Commit**

```powershell
git add Source/AssetDocument
git commit -m "feat: apply generic asset documents"
```

---

### Task 4: Inspect, Extract, Validate, And Diff

**Files:**
- Modify: `Source/AssetDocument/Private/AssetDocumentPropertyAdapter.h/.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentServiceTests.cpp`

- [ ] **Step 1: Add failing read-side tests**

Tests:

- `Inspect` on `TestDataAsset` returns editable property rows with `name`, `ue_type`, `type_token`, `current_value`, `default_value`, and `writable`.
- `Extract` returns an AssetDocument draft with `SchemaVersion`, `AssetType`, `Target`, `Class`, and diff-only `Properties`.
- `Validate` does not save or dirty packages.
- `Diff` returns `changed`, `unchanged`, `skipped`, and `failed` arrays.

- [ ] **Step 2: Implement type-token mapping**

Implement a small reflection-derived mapper:

- bool -> `Bool`;
- integer -> `Int`;
- float/double -> `Float`;
- string/name/text -> `String` / `Name` / `Text`;
- struct -> struct name such as `FVector`;
- object/soft object -> `Object`;
- class/soft class -> `Class`;
- enum -> `Enum`;
- array/map/set -> `Array` / `Map` / `Set`.

Do not use subtype strings in the `type` token.

- [ ] **Step 3: Implement inspect**

Iterate `TFieldIterator<FProperty>` over the resolved class or loaded asset class. Include editable properties and skipped entries with reasons for transient, deprecated, non-editable, or unsupported serialization.

- [ ] **Step 4: Implement extract**

Use `FPropertySetterUtils::ExtractPropertiesToJson(Object, true, bDiffOnly)` as the first implementation. Wrap output:

```json
{
  "SchemaVersion": 1,
  "AssetType": "GenericAsset",
  "Target": "/Game/AssetDocumentTests/DA_Test",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Properties": {}
}
```

- [ ] **Step 5: Implement diff**

For each document property:

- load current asset;
- serialize current value;
- compare JSON value text;
- record before/after for changed fields;
- record failed entry when property is missing or type validation fails.

- [ ] **Step 6: Run read-side tests**

Expected: all read-side tests pass and leave no dirty packages.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument
git commit -m "feat: inspect and extract asset documents"
```

---

### Task 5: HTTP Routes

**Files:**
- Modify: `Source/AssetFactory/Public/AssetFactoryHttpServer.h`
- Modify: `Source/AssetFactory/Private/AssetFactoryHttpServer.cpp`
- Create: `Source/AssetDocument/Private/AssetDocumentHttpRoutes.h`
- Create: `Source/AssetDocument/Private/AssetDocumentHttpRoutes.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Test: UBT and manual HTTP calls

- [ ] **Step 1: Add an external route extension API to `AssetFactoryHttpServer.h`**

Add public methods and a private storage array. Keep this API generic; do not mention AssetDocument in `AssetFactoryHttpServer`.

```cpp
struct ASSETFACTORY_API FAssetFactoryExternalRoute
{
	FString Path;
	EHttpServerRequestVerbs Verb = EHttpServerRequestVerbs::VERB_GET;
	FHttpRequestHandler Handler;
	FString MethodText;
	FString Description;
	bool bRequiresBody = false;
};

class ASSETFACTORY_API FAssetFactoryHttpServer
{
public:
	static FDelegateHandle RegisterExternalRoute(const FAssetFactoryExternalRoute& Route);
	static void UnregisterExternalRoute(FDelegateHandle Handle);

private:
	static TMap<FDelegateHandle, FAssetFactoryExternalRoute>& GetExternalRoutes();
	void BindExternalRoutes();
	void UnbindExternalRoutes();
	TArray<FHttpRouteHandle> ExternalRouteHandles;
};
```

- [ ] **Step 2: Bind external routes in `AssetFactoryHttpServer.cpp`**

Implementation rules:

- `RegisterExternalRoute` stores the route by handle and, if the server is already running, the next restart binds it; first version does not need live binding into an already running server.
- `Start` calls `BindExternalRoutes()` after binding built-in routes.
- `Stop` calls `UnbindExternalRoutes()` before clearing `HttpRouter`.
- `WriteServiceDiscoveryFile` includes external routes so MCP clients can discover the new endpoints.

- [ ] **Step 3: Create `AssetDocumentHttpRoutes`**

`AssetDocumentHttpRoutes.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

class FAssetDocumentService;

class FAssetDocumentHttpRoutes
{
public:
	explicit FAssetDocumentHttpRoutes(FAssetDocumentService& InService);
	~FAssetDocumentHttpRoutes();

	void Register();
	void Unregister();

private:
	FAssetDocumentService& Service;
	TArray<FDelegateHandle> RouteHandles;
};
```

`AssetDocumentHttpRoutes.cpp` must create handlers for these routes:

- `POST /assetfactory/assetdocument/apply`
- `POST /assetfactory/assetdocument/apply-file`
- `GET /assetfactory/assetdocument/schema`
- `GET /assetfactory/assetdocument/inspect`
- `POST /assetfactory/assetdocument/extract`
- `POST /assetfactory/assetdocument/validate`
- `POST /assetfactory/assetdocument/diff`

- [ ] **Step 4: Forward handlers to C++ service**

Each handler:

- parse JSON body or query parameters;
- call the injected `FAssetDocumentService`;
- return `Result.ToJson()` using `SendJsonResponse`;
- return HTTP 400 for validation failures and 500 only for unexpected server errors.

- [ ] **Step 5: Register routes from `AssetDocumentModule.cpp`**

In `StartupModule`, create `FAssetDocumentHttpRoutes` after creating `FAssetDocumentService` and call `Register()`.

In `ShutdownModule`, call `Unregister()` before destroying the service.

- [ ] **Step 6: Run UBT**

Expected: compile passes.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetFactory Source/AssetDocument
git commit -m "feat: expose asset document http routes"
```

---

### Task 6: MCP Tools And Schema

**Files:**
- Modify: `MCP/src/index.ts`
- Modify: `MCP/src/broker/toolCatalog.ts`
- Create: `MCP/schemas/AssetDocument.md`
- Modify generated files under `MCP/dist` by running build.
- Test: MCP TypeScript tests

- [ ] **Step 1: Add tool definitions**

Add tools:

- `apply_asset_document`
- `apply_asset_document_file`
- `get_asset_document_schema`
- `inspect_asset_document_target`
- `extract_asset_document`
- `validate_asset_document`
- `diff_asset_document`

Each tool calls the matching `/assetfactory/assetdocument/*` HTTP route. Do not implement reflection logic in TypeScript.

- [ ] **Step 2: Add schema file**

`MCP/schemas/AssetDocument.md` must document:

- required sidecar `Target`;
- inline `Name` / `Path` convenience;
- `Properties` typed/untyped forms;
- no subtype in `type`;
- inspect/extract/validate/diff tools;
- file watcher behavior.

- [ ] **Step 3: Update tool catalog**

Add the new tool names to `MCP/src/broker/toolCatalog.ts` and descriptions to the visible catalog.

- [ ] **Step 4: Add MCP tests**

Add or update tests so `ListTools` includes all AssetDocument tools and `get_asset_document_schema` loads `AssetDocument.md`.

- [ ] **Step 5: Build MCP**

Run:

```powershell
Set-Location MCP
npm test
npm run build
Set-Location ..
```

Expected: tests pass and `MCP/dist` updates.

- [ ] **Step 6: Commit**

```powershell
git add MCP
git commit -m "feat: add asset document mcp tools"
```

---

### Task 7: Editor Sidecar Rename/Move/Duplicate/Delete Hooks

**Files:**
- Create: `Source/AssetDocument/Private/AssetDocumentEditorSync.h/.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Test: automation or MCP smoke

- [ ] **Step 1: Add sync service**

Implement a service that handles:

- rename: rename sidecar and update required `Target`;
- move: move sidecar and update required `Target`;
- duplicate: copy sidecar and update required `Target`;
- delete: move sidecar to `_DeletedAssetDocs` or delete it; choose move to `_DeletedAssetDocs` for first version.

- [ ] **Step 2: Register editor delegates**

Use stable UE 5.7 editor/asset delegates found during implementation. Register in module startup and unregister in shutdown.

- [ ] **Step 3: Add recursion guards**

Guard sidecar writes so sync-triggered sidecar updates do not trigger duplicate work in the file watcher.

- [ ] **Step 4: Verify manually with editor automation**

Use MCP `execute_python` or editor automation to:

- create asset and sidecar;
- rename asset;
- move asset;
- duplicate asset;
- delete asset;
- inspect sidecar files and `Target` fields.

- [ ] **Step 5: Commit**

```powershell
git add Source/AssetDocument
git commit -m "feat: sync asset document sidecars"
```

---

### Task 8: File Watcher Auto Apply

**Files:**
- Create: `Source/AssetDocument/Private/AssetDocumentFileWatcher.h/.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Test: editor smoke

- [ ] **Step 1: Implement watcher service**

Use `FDirectoryWatcherModule` / `IDirectoryWatcher`. Watch project `Content/` for `.assetdoc.json` changes.

- [ ] **Step 2: Add debounce and stable-file check**

Maintain a map:

```cpp
TMap<FString, FDateTime> PendingFiles;
TSet<FString> SuppressedFiles;
```

Only call `ApplyFile` after size and timestamp are stable across the debounce interval.

- [ ] **Step 3: Apply on game thread**

Use `AsyncTask(ENamedThreads::GameThread, ...)` before touching UObject/package state.

- [ ] **Step 4: Protect dirty assets**

Before apply, if the target package is dirty from user edits and the watcher did not dirty it, return warning and skip. Do not overwrite.

- [ ] **Step 5: Prevent recursive apply**

When `ApplyFile` rewrites sidecar JSON, add the path to `SuppressedFiles` before write and remove it after the next watcher event or after a short timeout.

- [ ] **Step 6: Smoke test**

In editor:

1. Create sidecar and asset.
2. Modify `.assetdoc.json` externally and save.
3. Verify `.uasset` changes without MCP apply call.
4. Save invalid JSON and verify no `.uasset` write.
5. Dirty target asset manually and verify watcher skips.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument
git commit -m "feat: auto apply asset document sidecars"
```

---

### Task 9: End-To-End Verification

**Files:**
- Optional create: `MCP/scripts/assetdocument_smoke.mjs`
- Modify docs only if verification reveals a usage correction.

- [ ] **Step 1: Create temporary validation host**

Follow project instruction: create host under `C:/Users/HP/.config/superpowers/validation-hosts/...` and junction `Plugins/UECopilot` to this worktree. Do not validate worktree changes through the main `PluginsWarehouse.uproject` if the main project plugin shadows it.

- [ ] **Step 2: Run UBT on host**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" <HostEditorTarget> Win64 Development "-Project=<HostProject>.uproject" -NoHotReload
```

Expected: compile succeeds.

- [ ] **Step 3: Start editor and wait for HTTP**

Start editor in background, then use `health_check` MCP until ready.

- [ ] **Step 4: Run MCP smoke**

Exercise:

- `get_asset_document_schema`
- `inspect_asset_document_target`
- `apply_asset_document`
- `extract_asset_document`
- `validate_asset_document`
- `diff_asset_document`
- `apply_asset_document_file`
- watcher auto apply

- [ ] **Step 5: Read latest UE log if anything fails**

Check latest log under the validation host `Saved/Logs`.

- [ ] **Step 6: Commit smoke script or log notes**

If a reusable script was created:

```powershell
git add MCP/scripts/assetdocument_smoke.mjs
git commit -m "test: add asset document smoke script"
```

If no script was needed, do not create a commit just for logs.

---

## Self-Review

**Spec coverage:**

- Standalone module: Task 1.
- C++ service facade: Task 1 and Task 3.
- Required `Target` and sidecar path validation: Task 2.
- Apply/create/update/save: Task 3.
- Inspect/extract/validate/diff: Task 4.
- HTTP/MCP tools: Tasks 5 and 6.
- No module dependency cycle: Task 5 keeps `AssetDocument` depending on `AssetFactory`; `AssetFactory` exposes generic route extension only and does not depend on `AssetDocument`.
- Sidecar rename/move/duplicate/delete: Task 7.
- File watcher auto apply: Task 8.
- UBT and editor/MCP verification: Task 9.

**Placeholder scan:** No task uses open-ended placeholders. Each task has concrete files, behavior, commands, and commit boundaries.

**Type consistency:** The plan consistently uses `FAssetDocumentService`, `Apply`, `ApplyFile`, `Inspect`, `Extract`, `Validate`, `Diff`, external HTTP route registration, and sidecar `Target`.
