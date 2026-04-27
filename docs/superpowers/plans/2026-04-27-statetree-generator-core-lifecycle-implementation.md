# StateTree Generator Core Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the first StateTree generator slice: create/update/extract a minimal `UStateTree` asset through editor data initialization and official StateTree compilation.

**Architecture:** Add `FStateTreeGenerator` to AssetFactory with a narrow lifecycle boundary: resolve schema, create or load `UStateTree`, initialize/editor-validate `UStateTreeEditorData`, compile via `UStateTreeEditingSubsystem::CompileStateTree`, save only on success, and extract a skeleton from `EditorData`. MCP exposure is part of this slice so Codex can discover `StateTree` through `get_generator_schema`.

**Tech Stack:** UE 5.7 C++ editor module, `StateTreeModule`, `StateTreeEditorModule`, `GameplayStateTreeModule`, AssetFactory `IAssetGenerator`, MCP TypeScript schema docs, UBT Development build, MCP editor verification.

---

## Preflight Notes

- Git repository root is `E:/GameDev/PluginsWarehouse/Plugins/UECopilot`.
- Do not commit unless the user explicitly asks. Use review checkpoints instead of automatic commits.
- Existing untracked files may be present. Do not modify unrelated files.
- This plan implements spec `docs/superpowers/specs/2026-04-27-statetree-generator-core-lifecycle-design.md`.
- This plan does not implement tasks, transitions, parameters, property bags, or property bindings.

---

## File Structure

Create:

- `Source/AssetFactory/Public/Generators/StateTreeGenerator.h`
  Declares `FStateTreeGenerator`, schema resolution, compile/finalize helpers, and extract helpers.

- `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`
  Implements generation lifecycle, schema validation, compile log formatting, saving, and skeleton extract.

- `MCP/schemas/StateTree.md`
  Documents only the core lifecycle JSON contract.

- `TestData/ST_Core_Minimal.json`
  Minimal valid StateTree fixture using `StateTreeComponentSchema`.

- `TestData/ST_Core_AIComponentSchema.json`
  Valid StateTree fixture using `StateTreeAIComponentSchema` and schema properties.

- `TestData/ST_Core_InvalidSchema.json`
  Invalid fixture for validation and MCP negative testing.

Modify:

- `Source/AssetFactory/Private/AssetFactoryModule.cpp`
  Include and register `FStateTreeGenerator`.

- `Source/AssetFactory/AssetFactory.Build.cs`
  Add StateTree and property binding module dependencies.

- `AssetFactory.uplugin`
  Enable `StateTree` and `GameplayStateTree` plugin dependencies.

- `MCP/src/index.ts`
  Add `StateTree` to schema fallback text, `generate_assets` description, and `get_generator_schema` enum/description.

- `MCP/dist/index.js` and `MCP/dist/index.d.ts`
  Regenerate with `npm run build` after TypeScript source changes.

---

## Task 1: Add Fixtures And MCP Schema Draft

**Files:**
- Create: `TestData/ST_Core_Minimal.json`
- Create: `TestData/ST_Core_AIComponentSchema.json`
- Create: `TestData/ST_Core_InvalidSchema.json`
- Create: `MCP/schemas/StateTree.md`

- [ ] **Step 1: Add minimal valid fixture**

Create `TestData/ST_Core_Minimal.json`:

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Core_Minimal",
  "Path": "/Game/Generated/StateTree",
  "Action": "CreateOrUpdate",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema"
}
```

- [ ] **Step 2: Add AI schema fixture**

Create `TestData/ST_Core_AIComponentSchema.json`:

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Core_AIComponentSchema",
  "Path": "/Game/Generated/StateTree",
  "Action": "CreateOrUpdate",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeAIComponentSchema",
  "SchemaProperties": {
    "ContextActorClass": "/Script/Engine.Pawn",
    "AIControllerClass": "/Script/AIModule.AIController"
  }
}
```

- [ ] **Step 3: Add invalid schema fixture**

Create `TestData/ST_Core_InvalidSchema.json`:

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Core_InvalidSchema",
  "Path": "/Game/Generated/StateTree",
  "Action": "Create",
  "SchemaClass": "/Script/Engine.Actor"
}
```

- [ ] **Step 4: Add StateTree MCP schema doc**

Create `MCP/schemas/StateTree.md`:

````markdown
# StateTree Generator Schema

Creates UE StateTree assets using editor data and the official StateTree compiler.

This schema currently covers the core lifecycle slice only: schema selection, schema properties, compile, save, and skeleton extraction. Tasks, transitions, parameters, property bags, and property bindings are covered by future StateTree specs.

## Top-Level Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `AssetType` | string | Yes | Must be `"StateTree"` |
| `Name` | string | Yes | Asset name |
| `Path` | string | Yes | Content path, for example `"/Game/AI"` |
| `Action` | string | No | `"Create"`, `"Update"`, or `"CreateOrUpdate"` |
| `SchemaClass` | string | Yes | `UStateTreeSchema` subclass path or exact class name |
| `SchemaProperties` | object | No | Properties applied to the schema instance via reflection |

## Minimal Example

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Enemy",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema"
}
```

## AI Component Schema Example

```json
{
  "AssetType": "StateTree",
  "Name": "ST_EnemyAI",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeAIComponentSchema",
  "SchemaProperties": {
    "ContextActorClass": "/Script/Engine.Pawn",
    "AIControllerClass": "/Script/AIModule.AIController"
  }
}
```

## Current Limitations

- `SubTrees` are extracted as a skeleton but are not yet accepted as input.
- Tasks, evaluators, conditions, considerations, transitions, parameters, and bindings are not part of this lifecycle slice.
- `Update` cannot change `SchemaClass`; recreate the asset if the schema class must change.
````

- [ ] **Step 5: Review checkpoint**

Run:

```powershell
git status --short
```

Expected: the three `TestData/ST_Core_*.json` files and `MCP/schemas/StateTree.md` are new. No production code has changed in this task.

---

## Task 2: Add Generator Header And Module Dependencies

**Files:**
- Create: `Source/AssetFactory/Public/Generators/StateTreeGenerator.h`
- Modify: `Source/AssetFactory/AssetFactory.Build.cs`
- Modify: `AssetFactory.uplugin`

- [ ] **Step 1: Add generator header**

Create `Source/AssetFactory/Public/Generators/StateTreeGenerator.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

class UStateTree;
class UStateTreeEditorData;
class UStateTreeSchema;
struct FStateTreeCompilerLog;

/**
 * Core lifecycle generator for UStateTree assets.
 *
 * This first slice creates editor data, compiles through the official compiler,
 * and extracts a skeleton. Node construction, transitions, parameters, and
 * bindings are intentionally handled by future specs.
 */
class ASSETFACTORY_API FStateTreeGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("StateTree"); }
	virtual int32 GetPriority() const override { return 40; }

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action = EGenerationAction::Create) const override;
	virtual TArray<FString> GetRequiredFields() const override { return { TEXT("SchemaClass") }; }

	virtual bool CanExtract(UObject* Asset) const override;
	virtual TSharedPtr<FJsonObject> Extract(UObject* Asset, bool bDiffOnly = false) const override;

private:
	UClass* ResolveSchemaClass(const FString& SchemaClassName) const;
	TOptional<FString> ValidateSchemaClass(const FString& SchemaClassName) const;
	TOptional<FString> ValidateUpdateSchema(UStateTree* ExistingTree, UClass* RequestedSchemaClass) const;

	UStateTree* CreateStateTreeAsset(const FString& Name, UPackage* Package, UClass* SchemaClass) const;
	UStateTreeEditorData* GetEditorData(UStateTree* StateTree) const;
	UStateTreeSchema* GetEditorSchemaInstance(UStateTree* StateTree) const;

	bool ApplySchemaProperties(UStateTreeSchema* Schema, TSharedPtr<FJsonObject> Config, FString& OutError) const;
	bool CompileStateTree(UStateTree* StateTree, FString& OutError) const;
	FString FormatCompilerLog(const FStateTreeCompilerLog& Log) const;
	bool SaveStateTreePackage(UStateTree* StateTree, UPackage* Package, const FString& Name, const FString& Path, FString& OutError) const;

	TSharedPtr<FJsonObject> ExtractSchemaProperties(const UStateTreeSchema* Schema, bool bDiffOnly) const;
	TArray<TSharedPtr<FJsonValue>> ExtractSubTreesSkeleton(const UStateTreeEditorData* EditorData) const;
};
```

- [ ] **Step 2: Add Build.cs dependencies**

In `Source/AssetFactory/AssetFactory.Build.cs`, add these strings to `PrivateDependencyModuleNames` near the existing editor/gameplay dependencies:

```csharp
// StateTree generation
"StateTreeModule",
"StateTreeEditorModule",
"StateTreeDeveloper",
"GameplayStateTreeModule",
"StructUtils",
"StructUtilsEditor",
"PropertyBindingUtils",
"PropertyBindingUtilsEditor",
```

- [ ] **Step 3: Enable required engine plugins**

In `AssetFactory.uplugin`, add these entries inside the `"Plugins"` array after `PythonScriptPlugin`:

```json
{
  "Name": "StateTree",
  "Enabled": true
},
{
  "Name": "GameplayStateTree",
  "Enabled": true
}
```

Keep existing plugin entries unchanged.

- [ ] **Step 4: Review checkpoint**

Run:

```powershell
git diff -- Source/AssetFactory/Public/Generators/StateTreeGenerator.h Source/AssetFactory/AssetFactory.Build.cs AssetFactory.uplugin
```

Expected: only the new header, new module dependency strings, and two plugin dependency entries are shown.

---

## Task 3: Implement Generator Lifecycle

**Files:**
- Create: `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`

- [ ] **Step 1: Add includes and local helpers**

Create `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp` with this opening section:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTreeGenerator.h"

#include "AssetFactoryModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/StateTreeFactory.h"
#include "Misc/PackageName.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "Utils/ClassFinderUtils.h"
#include "Utils/PropertySetterUtils.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FString NormalizeObjectPath(const FString& AssetPath)
	{
		FString NormalizedPath = AssetPath;
		if (!NormalizedPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPaths::GetBaseFilename(NormalizedPath);
			if (!AssetName.IsEmpty())
			{
				NormalizedPath += TEXT(".") + AssetName;
			}
		}
		return NormalizedPath;
	}

	FString BuildLongPackageName(const FString& Path, const FString& Name)
	{
		FString FullPath = Path / Name;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/") + FullPath;
		}
		return FullPath;
	}
}
```

- [ ] **Step 2: Implement class resolution and validation**

Add these methods:

```cpp
UClass* FStateTreeGenerator::ResolveSchemaClass(const FString& SchemaClassName) const
{
	if (SchemaClassName.IsEmpty())
	{
		return nullptr;
	}

	UClass* SchemaClass = nullptr;
	if (SchemaClassName.StartsWith(TEXT("/")))
	{
		SchemaClass = LoadClass<UStateTreeSchema>(nullptr, *SchemaClassName);
		if (!SchemaClass)
		{
			SchemaClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *SchemaClassName));
		}
	}
	else
	{
		SchemaClass = FClassFinderUtils::FindClassByName(SchemaClassName, UStateTreeSchema::StaticClass());
	}

	return SchemaClass && SchemaClass->IsChildOf(UStateTreeSchema::StaticClass()) ? SchemaClass : nullptr;
}

TOptional<FString> FStateTreeGenerator::ValidateSchemaClass(const FString& SchemaClassName) const
{
	if (SchemaClassName.IsEmpty())
	{
		return FString(TEXT("Missing or empty required field 'SchemaClass'"));
	}

	UClass* SchemaClass = ResolveSchemaClass(SchemaClassName);
	if (!SchemaClass)
	{
		return FString::Printf(TEXT("Unknown StateTree SchemaClass: %s"), *SchemaClassName);
	}

	if (!SchemaClass->IsChildOf(UStateTreeSchema::StaticClass()))
	{
		return FString::Printf(TEXT("SchemaClass '%s' is not a UStateTreeSchema subclass"), *SchemaClassName);
	}

	if (SchemaClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return FString::Printf(TEXT("SchemaClass '%s' is abstract and cannot be instantiated"), *SchemaClassName);
	}

	return TOptional<FString>();
}

TOptional<FString> FStateTreeGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	FString AssetType;
	if (!Config->TryGetStringField(TEXT("AssetType"), AssetType) || !AssetType.Equals(TEXT("StateTree"), ESearchCase::CaseSensitive))
	{
		return FString(TEXT("AssetType must be 'StateTree'"));
	}

	FString SchemaClassName;
	Config->TryGetStringField(TEXT("SchemaClass"), SchemaClassName);
	if (TOptional<FString> SchemaError = ValidateSchemaClass(SchemaClassName))
	{
		return SchemaError;
	}

	if (Config->HasField(TEXT("SubTrees")))
	{
		return FString(TEXT("SubTrees input is not supported by the StateTree core lifecycle spec"));
	}

	if (Config->HasField(TEXT("Bindings")))
	{
		return FString(TEXT("Bindings input is not supported by the StateTree core lifecycle spec"));
	}

	return TOptional<FString>();
}
```

- [ ] **Step 3: Implement editor data helpers**

Add:

```cpp
UStateTreeEditorData* FStateTreeGenerator::GetEditorData(UStateTree* StateTree) const
{
	if (!StateTree)
	{
		return nullptr;
	}
	return Cast<UStateTreeEditorData>(StateTree->EditorData);
}

UStateTreeSchema* FStateTreeGenerator::GetEditorSchemaInstance(UStateTree* StateTree) const
{
	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	return EditorData ? EditorData->Schema : nullptr;
}

UStateTree* FStateTreeGenerator::CreateStateTreeAsset(const FString& Name, UPackage* Package, UClass* SchemaClass) const
{
	if (!Package || !SchemaClass)
	{
		return nullptr;
	}

	UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
	Factory->SetSchemaClass(SchemaClass);

	return Cast<UStateTree>(Factory->FactoryCreateNew(
		UStateTree::StaticClass(),
		Package,
		*Name,
		RF_Public | RF_Standalone | RF_Transactional,
		nullptr,
		GWarn));
}
```

- [ ] **Step 4: Implement compile, schema property, and save helpers**

Add:

```cpp
bool FStateTreeGenerator::ApplySchemaProperties(UStateTreeSchema* Schema, TSharedPtr<FJsonObject> Config, FString& OutError) const
{
	if (!Schema || !Config.IsValid())
	{
		return true;
	}

	TSharedPtr<FJsonObject> SchemaProperties = GetObjectField(Config, TEXT("SchemaProperties"));
	if (!SchemaProperties.IsValid())
	{
		return true;
	}

	if (!FPropertySetterUtils::SetPropertiesFromJson(Schema, SchemaProperties))
	{
		OutError = TEXT("Failed to apply SchemaProperties");
		return false;
	}

	return true;
}

FString FStateTreeGenerator::FormatCompilerLog(const FStateTreeCompilerLog& Log) const
{
	TArray<TSharedRef<FTokenizedMessage>> Messages = Log.ToTokenizedMessages();
	if (Messages.Num() == 0)
	{
		return TEXT("StateTree compiler failed without diagnostic messages");
	}

	TArray<FString> Parts;
	for (const TSharedRef<FTokenizedMessage>& Message : Messages)
	{
		Parts.Add(Message->ToText().ToString());
	}
	return FString::Join(Parts, TEXT("; "));
}

bool FStateTreeGenerator::CompileStateTree(UStateTree* StateTree, FString& OutError) const
{
	if (!StateTree)
	{
		OutError = TEXT("StateTree is null");
		return false;
	}

	FStateTreeCompilerLog Log;
	if (!UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log))
	{
		OutError = FormatCompilerLog(Log);
		return false;
	}

	return true;
}

bool FStateTreeGenerator::SaveStateTreePackage(UStateTree* StateTree, UPackage* Package, const FString& Name, const FString& Path, FString& OutError) const
{
	if (!StateTree || !Package)
	{
		OutError = TEXT("Failed to resolve StateTree package");
		return false;
	}

	StateTree->PostEditChange();
	StateTree->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, StateTree, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save StateTree package for %s/%s"), *Path, *Name);
		return false;
	}

	return true;
}
```

- [ ] **Step 5: Implement Generate**

Add:

```cpp
FGenerationResult FStateTreeGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	if (!Config.IsValid())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Invalid configuration object"));
	}

	const bool bExists = DoesAssetExist(Path, Name);
	if (Action == EGenerationAction::Create && bExists)
	{
		return FGenerationResult::MakeSkipped(GetAssetType(), Name, Path, TEXT("Asset already exists"));
	}

	if (Action == EGenerationAction::Update && !bExists)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Asset does not exist for update"));
	}

	FString SchemaClassName;
	Config->TryGetStringField(TEXT("SchemaClass"), SchemaClassName);
	UClass* SchemaClass = ResolveSchemaClass(SchemaClassName);
	if (!SchemaClass)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, FString::Printf(TEXT("Unknown StateTree SchemaClass: %s"), *SchemaClassName));
	}

	UStateTree* StateTree = nullptr;
	UPackage* Package = nullptr;

	if (bExists)
	{
		StateTree = Cast<UStateTree>(LoadExistingAsset(Path, Name));
		if (!StateTree)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load StateTree for update"));
		}
		Package = StateTree->GetOutermost();
		if (TOptional<FString> SchemaError = ValidateUpdateSchema(StateTree, SchemaClass))
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, SchemaError.GetValue());
		}
	}
	else
	{
		Package = CreatePackage(*BuildLongPackageName(Path, Name));
		if (!Package)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create StateTree package"));
		}

		StateTree = CreateStateTreeAsset(Name, Package, SchemaClass);
		if (!StateTree)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create StateTree asset"));
		}
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData || !EditorData->Schema || !EditorData->EditorSchema)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("StateTree editor data was not initialized"));
	}

	FString Error;
	if (!ApplySchemaProperties(EditorData->Schema, Config, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	if (!CompileStateTree(StateTree, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	if (!bExists)
	{
		FAssetRegistryModule::AssetCreated(StateTree);
	}

	if (!SaveStateTreePackage(StateTree, Package, Name, Path, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	return bExists
		? FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, StateTree)
		: FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, StateTree);
}
```

- [ ] **Step 6: Implement update schema guard**

Add:

```cpp
TOptional<FString> FStateTreeGenerator::ValidateUpdateSchema(UStateTree* ExistingTree, UClass* RequestedSchemaClass) const
{
	if (!ExistingTree || !RequestedSchemaClass)
	{
		return FString(TEXT("StateTree update schema validation failed"));
	}

	UStateTreeSchema* ExistingSchema = GetEditorSchemaInstance(ExistingTree);
	if (!ExistingSchema)
	{
		return FString(TEXT("Existing StateTree has no editor schema instance"));
	}

	if (ExistingSchema->GetClass() != RequestedSchemaClass)
	{
		return FString(TEXT("StateTree Update cannot change SchemaClass in core lifecycle spec. Recreate the asset or use a future migration spec."));
	}

	return TOptional<FString>();
}
```

- [ ] **Step 7: Compile checkpoint**

Run UBT:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: compile may fail because the generator is not registered and extract methods are still missing. Any failure should be limited to missing `FStateTreeGenerator` method definitions. If module names cannot resolve, verify `StateTree` and `GameplayStateTree` plugin entries from Task 2.

---

## Task 4: Implement Extract Skeleton

**Files:**
- Modify: `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`

- [ ] **Step 1: Add extraction methods**

Append:

```cpp
bool FStateTreeGenerator::CanExtract(UObject* Asset) const
{
	return Asset && Asset->IsA<UStateTree>();
}

TSharedPtr<FJsonObject> FStateTreeGenerator::ExtractSchemaProperties(const UStateTreeSchema* Schema, bool bDiffOnly) const
{
	if (!Schema)
	{
		return nullptr;
	}

	return FPropertySetterUtils::ExtractPropertiesToJson(const_cast<UStateTreeSchema*>(Schema), true, bDiffOnly);
}

TArray<TSharedPtr<FJsonValue>> FStateTreeGenerator::ExtractSubTreesSkeleton(const UStateTreeEditorData* EditorData) const
{
	TArray<TSharedPtr<FJsonValue>> Result;
	if (!EditorData)
	{
		return Result;
	}

	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
	{
		if (!SubTree)
		{
			continue;
		}

		TSharedPtr<FJsonObject> StateJson = MakeShared<FJsonObject>();
		StateJson->SetStringField(TEXT("name"), SubTree->Name.ToString());
		StateJson->SetStringField(TEXT("type"), StaticEnum<EStateTreeStateType>()->GetNameStringByValue(static_cast<int64>(SubTree->Type)));
		StateJson->SetStringField(TEXT("id"), SubTree->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
		Result.Add(MakeShared<FJsonValueObject>(StateJson));
	}

	return Result;
}

TSharedPtr<FJsonObject> FStateTreeGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	UStateTree* StateTree = Cast<UStateTree>(Asset);
	if (!StateTree)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("AssetType"), TEXT("StateTree"));
	OutJson->SetStringField(TEXT("Name"), StateTree->GetName());

	if (UPackage* Package = StateTree->GetOutermost())
	{
		OutJson->SetStringField(TEXT("Path"), FPackageName::GetLongPackagePath(Package->GetName()));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	UStateTreeSchema* Schema = EditorData ? EditorData->Schema : nullptr;
	if (Schema)
	{
		OutJson->SetStringField(TEXT("SchemaClass"), Schema->GetClass()->GetPathName());

		TSharedPtr<FJsonObject> SchemaProps = ExtractSchemaProperties(Schema, bDiffOnly);
		if (SchemaProps.IsValid() && SchemaProps->Values.Num() > 0)
		{
			OutJson->SetObjectField(TEXT("SchemaProperties"), SchemaProps);
		}
	}

	TArray<TSharedPtr<FJsonValue>> SubTrees = ExtractSubTreesSkeleton(EditorData);
	if (SubTrees.Num() > 0)
	{
		OutJson->SetArrayField(TEXT("SubTrees"), SubTrees);
	}

	TSharedPtr<FJsonObject> CompiledJson = MakeShared<FJsonObject>();
	CompiledJson->SetNumberField(TEXT("lastCompiledEditorDataHash"), static_cast<double>(StateTree->LastCompiledEditorDataHash));
	OutJson->SetObjectField(TEXT("Compiled"), CompiledJson);

	return OutJson;
}
```

- [ ] **Step 2: Compile checkpoint**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: remaining failures, if any, should point to include/module issues or exact API mismatches. Fix only the StateTree generator files and module dependency list.

---

## Task 5: Register Generator

**Files:**
- Modify: `Source/AssetFactory/Private/AssetFactoryModule.cpp`

- [ ] **Step 1: Add include**

Add near the other generator includes:

```cpp
#include "Generators/StateTreeGenerator.h"
```

- [ ] **Step 2: Register generator**

In `FAssetFactoryModule::RegisterGenerators()`, add after behavior tree registration:

```cpp
Registry.RegisterGenerator(MakeShared<FStateTreeGenerator>());  // Priority 40: StateTree after BT/BB/tags, before generic assets
```

- [ ] **Step 3: Compile checkpoint**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: UBT succeeds before MCP work begins.

---

## Task 6: Expose StateTree In MCP

**Files:**
- Modify: `MCP/src/index.ts`
- Regenerate: `MCP/dist/index.js`
- Regenerate: `MCP/dist/index.d.ts`

- [ ] **Step 1: Update schema fallback text**

In `MCP/src/index.ts`, update `loadSchema()` fallback string so the available types include `StateTree`:

```ts
return `Schema not found for asset type: ${assetType}. Available types: Blueprint, WidgetBlueprint, StateTree, Material, DataAsset, DataTable, CurveFloat, CurveVector, InputAction, InputMappingContext, GameplayTag`;
```

- [ ] **Step 2: Update generate_assets description**

In the `generate_assets` tool description, add `StateTree` to the supported type list:

```ts
"Generate Unreal Engine assets from JSON configuration. Supports Blueprint (any parent class: Actor, Character, GameplayEffect, GameplayAbility, AnimInstance, BTTaskNode, and related subclasses), WidgetBlueprint, StateTree, DataAsset, DataTable, Material, CurveFloat, CurveVector, InputAction, InputMappingContext, GameplayTag. Supports Create, Update (field-level patch: only JSON-present fields are modified, missing fields are preserved), and CreateOrUpdate actions. IMPORTANT: Call get_generator_schema first to get the correct JSON field names and formats for the asset type you want to generate. IMPORTANT for WidgetBlueprint Update: use 'WidgetUpdates' array for safe incremental changes; do NOT use 'RootWidget' in Update mode unless you intend to destroy and fully rebuild the widget tree (requires '\"RebuildTree\": true')."
```

Also update the nested `AssetType.description` string to include `StateTree`.

- [ ] **Step 3: Update get_generator_schema enum**

Add `StateTree` to the description and enum:

```ts
description:
  "The asset type to get schema for: Blueprint, WidgetBlueprint, StateTree, Material, DataAsset, DataTable, CurveFloat, CurveVector, InputAction, InputMappingContext, GameplayTag",
enum: [
  "Blueprint",
  "WidgetBlueprint",
  "StateTree",
  "Material",
  "DataAsset",
  "DataTable",
  "CurveFloat",
  "CurveVector",
  "InputAction",
  "InputMappingContext",
  "GameplayTag",
],
```

- [ ] **Step 4: Build MCP**

Run:

```powershell
Push-Location MCP
npm run build
Pop-Location
```

Expected: TypeScript build succeeds and updates `MCP/dist/index.js` plus `MCP/dist/index.d.ts` if emitted.

- [ ] **Step 5: Verify generated dist contains StateTree**

Run:

```powershell
Select-String -Path MCP\dist\index.js -Pattern 'StateTree'
```

Expected: matches in fallback text and tool schema strings.

---

## Task 7: Editor And MCP Verification

**Files:**
- No planned code edits unless verification exposes defects in files owned by earlier tasks.

- [ ] **Step 1: Run final UBT compile**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: build succeeds.

- [ ] **Step 2: Start editor if needed**

If the editor is not already running, start it in the background:

```powershell
Start-Process -FilePath "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject"
```

- [ ] **Step 3: Wait for MCP HTTP server**

Use MCP `health_check`.

Expected result:

```json
{ "status": "ok" }
```

or an equivalent success response.

- [ ] **Step 4: Verify generator schema discovery**

Use MCP `get_generator_schema`:

```json
{
  "asset_type": "StateTree"
}
```

Expected: response includes `SchemaClass`, `SchemaProperties`, and the minimal StateTree examples from `MCP/schemas/StateTree.md`.

- [ ] **Step 5: Generate minimal fixture**

Use MCP `generate_assets`:

```json
{
  "assets": [
    {
      "AssetType": "StateTree",
      "Name": "ST_Core_Minimal",
      "Path": "/Game/Generated/StateTree",
      "Action": "CreateOrUpdate",
      "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema"
    }
  ]
}
```

Expected: success or updated status. Asset path `/Game/Generated/StateTree/ST_Core_Minimal` exists.

- [ ] **Step 6: Extract generated asset**

Use MCP `extract_assets`:

```json
{
  "assets": ["/Game/Generated/StateTree/ST_Core_Minimal"],
  "diffOnly": true
}
```

Expected: JSON contains:

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Core_Minimal",
  "Path": "/Game/Generated/StateTree",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
  "SubTrees": [
    {
      "name": "Root",
      "type": "State"
    }
  ],
  "Compiled": {
    "lastCompiledEditorDataHash": 1
  }
}
```

The hash value only needs to be non-zero.

- [ ] **Step 7: Verify AI schema fixture**

Use MCP `generate_assets` with `TestData/ST_Core_AIComponentSchema.json` values.

Expected: success and extract output includes `SchemaClass` ending with `StateTreeAIComponentSchema`.

- [ ] **Step 8: Verify invalid schema fails cleanly**

Use MCP `generate_assets` with `TestData/ST_Core_InvalidSchema.json`.

Expected: failed result with message:

```text
SchemaClass '/Script/Engine.Actor' is not a UStateTreeSchema subclass
```

or:

```text
Unknown StateTree SchemaClass: /Script/Engine.Actor
```

Either is acceptable if no asset is saved and the editor does not crash.

- [ ] **Step 9: Confirm no half-created invalid asset**

Use MCP `extract_assets`:

```json
{
  "assets": ["/Game/Generated/StateTree/ST_Core_InvalidSchema"]
}
```

Expected: extraction fails or reports asset not found.

---

## Task 8: Final Review And Cleanup

**Files:**
- Review only unless verification defects require edits.

- [ ] **Step 1: Inspect changed files**

Run:

```powershell
git status --short
git diff --stat
```

Expected: changed files match the File Structure section. Unrelated existing untracked files remain untouched.

- [ ] **Step 2: Scan for accidental unsupported fields**

Run:

```powershell
Select-String -Path Source\AssetFactory\Private\Generators\StateTreeGenerator.cpp -Pattern 'Task|Transition|Binding|Parameter|Evaluator|Condition|Consideration'
```

Expected: matches only appear in error strings or comments explaining unsupported core lifecycle fields, not implementation logic for those features.

- [ ] **Step 3: Re-run MCP build**

Run:

```powershell
Push-Location MCP
npm run build
Pop-Location
```

Expected: build succeeds.

- [ ] **Step 4: Re-run UBT build**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: build succeeds.

- [ ] **Step 5: Prepare final implementation summary**

Summarize:

- Generator files created.
- MCP schema exposure added.
- Fixtures added.
- UBT result.
- MCP generate/extract result.
- Known non-goals: nodes, transitions, parameters, bindings.

Do not commit unless the user explicitly asks.
