# StateTree Generator Property Bindings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 AssetFactory StateTree generator 增加普通 property bindings、property function bindings、extract/round-trip、MCP schema 与真实 Editor/MCP 冒烟。

**Architecture:** 新增独立的 StateTree binding layer，负责 JSON spec、state/node/parameter/context 索引、`FPropertyBindingPath` 解析、普通 binding 与 property function binding 写入。现有 `StateTreeStateBuilder` 只在完整 editor structure 生成后调用 binding builder，`StateTreeExtract` 只调用 binding extract helper 输出 canonical JSON。

**Tech Stack:** Unreal Engine 5.7、StateTree editor/runtime modules、PropertyBindingUtils、AssetFactory C++ generator、MCP TypeScript schema/docs、真实 GUI Editor smoke。

---

## 当前分支上下文

- Worktree：`/Volumes/Mac/GameDev/ProjectRPG/.worktrees/AssetFactory-statetree-property-bindings`
- Branch：`feature/statetree-property-bindings`
- Base commit：`2a36994 docs: document StateTree parameters`
- Design commit：`9c234fa docs: design StateTree property bindings`
- 不要运行 `RunUAT BuildPlugin`。本会话验证只用 `npm --prefix MCP run build` 和 ProjectRPG 正常编译。
- GUI smoke 使用主工程 `/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject`，真实图形编辑器，不用 `UnrealEditor-Cmd`、`-NullRHI` 或 headless。

## 文件地图

Create:

- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingTypes.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingResolver.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingResolver.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingBuilder.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingBuilder.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.cpp`
- `TestData/ST_Bindings_Ordinary.json`
- `TestData/ST_Bindings_Function.json`
- `TestData/ST_Bindings_Invalid_UnknownSource.json`
- `TestData/ST_Bindings_Invalid_UnknownTarget.json`
- `TestData/ST_Bindings_Invalid_BadPath.json`
- `TestData/ST_Bindings_Invalid_DuplicateTarget.json`
- `TestData/ST_Bindings_Invalid_BadFunctionType.json`
- `TestData/ST_Bindings_Invalid_TypeMismatch.json`
- `docs/superpowers/verification/statetree_binding_roundtrip_check.py`

Modify:

- `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStructureTypes.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeLinkResolver.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeLinkResolver.cpp`
- `MCP/schemas/StateTree.md`

Engine APIs to keep open while implementing:

- `/Users/pengao/UnrealEngine/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorData.h`
- `/Users/pengao/UnrealEngine/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorPropertyBindings.h`
- `/Users/pengao/UnrealEngine/Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreePropertyBindings.h`
- `/Users/pengao/UnrealEngine/Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreePropertyFunctionBase.h`
- `/Users/pengao/UnrealEngine/Engine/Plugins/Runtime/PropertyBindingUtils/Source/PropertyBindingUtils/Public/PropertyBindingPath.h`
- `/Users/pengao/UnrealEngine/Engine/Plugins/Runtime/StateTree/Source/StateTreeTestSuite/Private/StateTreeBindingTest.cpp`

## Task 1: 解析 binding JSON types

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingTypes.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStructureTypes.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`

- [ ] **Step 1: 写 binding spec types**

Create `StateTreeBindingTypes.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

enum class EAFStateTreeBindingEndpointKind : uint8
{
	RootParameter,
	StateParameter,
	Context,
	Evaluator,
	GlobalTask,
	Task,
	EnterCondition,
	TransitionCondition,
	Consideration,
	Function,
	Node,
};

enum class EAFStateTreeBindingDataSection : uint8
{
	Node,
	Instance,
	ExecutionRuntimeData,
};

struct FAFStateTreeBindingPathSegmentSpec
{
	FString Name;
	int32 ArrayIndex = INDEX_NONE;
	FGuid Guid;
	bool bHasGuid = false;
	FString InstanceStruct;
	FString Access;
};

struct FAFStateTreeBindingEndpointSpec
{
	EAFStateTreeBindingEndpointKind Kind = EAFStateTreeBindingEndpointKind::Node;
	EAFStateTreeBindingDataSection Section = EAFStateTreeBindingDataSection::Instance;
	FString State;
	FString Transition;
	FString Node;
	FString Name;
	FString Class;
	TArray<FAFStateTreeBindingPathSegmentSpec> Path;
};

struct FAFStateTreeBindingFunctionSpec;

struct FAFStateTreeBindingFunctionInputSpec
{
	FAFStateTreeBindingEndpointSpec Source;
	TSharedPtr<FAFStateTreeBindingFunctionSpec> Function;
	bool bHasSource = false;
	bool bHasFunction = false;
};

struct FAFStateTreeBindingFunctionSpec
{
	FString Type;
	TArray<FAFStateTreeBindingPathSegmentSpec> OutputPath;
	TMap<FString, FAFStateTreeBindingFunctionInputSpec> Inputs;
};

struct FAFStateTreeBindingSpec
{
	FString Id;
	FAFStateTreeBindingEndpointSpec Source;
	FAFStateTreeBindingEndpointSpec Target;
	FAFStateTreeBindingFunctionSpec Function;
	bool bHasSource = false;
	bool bHasTarget = false;
	bool bHasFunction = false;
	int32 SourceIndex = INDEX_NONE;
};
```

- [ ] **Step 2: 增加 parser declarations**

Append declarations in `StateTreeJsonTypes.h` namespace `UE::AssetFactory::StateTree`:

```cpp
	bool ParseBindingSpecsFromConfig(
		const TSharedPtr<FJsonObject>& Config,
		TArray<FAFStateTreeBindingSpec>& OutSpecs,
		FString& OutError);
```

Also include:

```cpp
#include "Generators/StateTree/StateTreeBindingTypes.h"
```

- [ ] **Step 3: 实现 parser helpers**

In `StateTreeJsonTypes.h`, add inline parsing helpers near existing JSON helper functions:

```cpp
	inline bool TryParseBindingEndpointKind(const FString& Value, EAFStateTreeBindingEndpointKind& OutKind)
	{
		if (Value.Equals(TEXT("rootParameter"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("root_parameter"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::RootParameter;
			return true;
		}
		if (Value.Equals(TEXT("stateParameter"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("state_parameter"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::StateParameter;
			return true;
		}
		if (Value.Equals(TEXT("context"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Context;
			return true;
		}
		if (Value.Equals(TEXT("evaluator"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Evaluator;
			return true;
		}
		if (Value.Equals(TEXT("globalTask"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("global_task"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::GlobalTask;
			return true;
		}
		if (Value.Equals(TEXT("task"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Task;
			return true;
		}
		if (Value.Equals(TEXT("enterCondition"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("enter_condition"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::EnterCondition;
			return true;
		}
		if (Value.Equals(TEXT("transitionCondition"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("transition_condition"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::TransitionCondition;
			return true;
		}
		if (Value.Equals(TEXT("consideration"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Consideration;
			return true;
		}
		if (Value.Equals(TEXT("function"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Function;
			return true;
		}
		if (Value.Equals(TEXT("node"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Node;
			return true;
		}
		return false;
	}

	inline bool TryParseBindingDataSection(const FString& Value, EAFStateTreeBindingDataSection& OutSection)
	{
		if (Value.IsEmpty() || Value.Equals(TEXT("instance"), ESearchCase::IgnoreCase))
		{
			OutSection = EAFStateTreeBindingDataSection::Instance;
			return true;
		}
		if (Value.Equals(TEXT("node"), ESearchCase::IgnoreCase))
		{
			OutSection = EAFStateTreeBindingDataSection::Node;
			return true;
		}
		if (Value.Equals(TEXT("executionRuntimeData"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("execution_runtime_data"), ESearchCase::IgnoreCase))
		{
			OutSection = EAFStateTreeBindingDataSection::ExecutionRuntimeData;
			return true;
		}
		return false;
	}
```

- [ ] **Step 4: 实现 path parser**

Add this helper in `StateTreeJsonTypes.h`:

```cpp
	inline bool ParseBindingPathSegments(
		const TArray<TSharedPtr<FJsonValue>>& PathValues,
		const FString& Label,
		TArray<FAFStateTreeBindingPathSegmentSpec>& OutPath,
		FString& OutError)
	{
		OutPath.Reset();
		for (int32 Index = 0; Index < PathValues.Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = PathValues[Index];
			FAFStateTreeBindingPathSegmentSpec Segment;
			if (!Value.IsValid())
			{
				OutError = FString::Printf(TEXT("StateTree binding %s path segment %d is null"), *Label, Index);
				return false;
			}
			if (Value->Type == EJson::String)
			{
				Segment.Name = Value->AsString();
			}
			else if (Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject>* SegmentObject = nullptr;
				if (!Value->TryGetObject(SegmentObject) || !SegmentObject || !SegmentObject->IsValid())
				{
					OutError = FString::Printf(TEXT("StateTree binding %s path segment %d must be an object"), *Label, Index);
					return false;
				}
				(*SegmentObject)->TryGetStringField(TEXT("name"), Segment.Name);
				(*SegmentObject)->TryGetStringField(TEXT("Name"), Segment.Name);
				double ArrayIndex = static_cast<double>(INDEX_NONE);
				if ((*SegmentObject)->TryGetNumberField(TEXT("arrayIndex"), ArrayIndex) || (*SegmentObject)->TryGetNumberField(TEXT("ArrayIndex"), ArrayIndex))
				{
					Segment.ArrayIndex = static_cast<int32>(ArrayIndex);
				}
				FString GuidString;
				if ((*SegmentObject)->TryGetStringField(TEXT("guid"), GuidString) || (*SegmentObject)->TryGetStringField(TEXT("Guid"), GuidString))
				{
					if (!FGuid::Parse(GuidString, Segment.Guid))
					{
						OutError = FString::Printf(TEXT("StateTree binding %s path segment %d has invalid guid '%s'"), *Label, Index, *GuidString);
						return false;
					}
					Segment.bHasGuid = true;
				}
				(*SegmentObject)->TryGetStringField(TEXT("instanceStruct"), Segment.InstanceStruct);
				(*SegmentObject)->TryGetStringField(TEXT("InstanceStruct"), Segment.InstanceStruct);
				(*SegmentObject)->TryGetStringField(TEXT("access"), Segment.Access);
				(*SegmentObject)->TryGetStringField(TEXT("Access"), Segment.Access);
			}
			else
			{
				OutError = FString::Printf(TEXT("StateTree binding %s path segment %d must be a string or object"), *Label, Index);
				return false;
			}
			if (Segment.Name.IsEmpty())
			{
				OutError = FString::Printf(TEXT("StateTree binding %s path segment %d is missing name"), *Label, Index);
				return false;
			}
			OutPath.Add(MoveTemp(Segment));
		}
		return true;
	}
```

- [ ] **Step 5: 实现 endpoint/function/bindings parser**

Add parser functions in `StateTreeJsonTypes.h` after the path helper. They must:

- Read `kind`, `state`, `transition`, `node`, `name`, `class`, `section`, `path`.
- Accept `Path`/`path`, `Source`/`source`, `Target`/`target`, `Function`/`function`, `Output`/`output`, `Inputs`/`inputs`.
- Reject endpoint without `kind`.
- Reject endpoint path that is not an array.
- For ordinary binding, require source and target.
- For function binding, require target and function object.
- For function input, require exactly one of `source` or nested `function`, and recursively parse nested `function` into `FAFStateTreeBindingFunctionInputSpec::Function`.

Use diagnostics shaped like:

```cpp
OutError = FString::Printf(TEXT("StateTree binding[%d] is missing required field 'target'"), BindingIndex);
```

- [ ] **Step 6: 允许 ValidateConfig 接收 bindings**

Modify `FStateTreeGenerator::ValidateConfig()`:

```cpp
	if ((Config->HasField(TEXT("Bindings")) && !Config->HasTypedField<EJson::Array>(TEXT("Bindings")))
		|| (Config->HasField(TEXT("bindings")) && !Config->HasTypedField<EJson::Array>(TEXT("bindings"))))
	{
		return FString(TEXT("StateTree Bindings must be an array"));
	}
```

Remove the current hard rejection:

```cpp
if (Config->HasField(TEXT("Bindings")))
{
	return FString(TEXT("Bindings input is not supported by the StateTree core lifecycle spec"));
}
```

- [ ] **Step 7: Build to catch parser/header errors**

Run:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected: `Result: Succeeded`.

- [ ] **Step 8: Commit parser skeleton**

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingTypes.h Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp
git commit -m "feat: parse StateTree binding specs"
```

## Task 2: 建立 binding resolver 索引

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingResolver.h`
- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingResolver.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeLinkResolver.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeLinkResolver.cpp`

- [ ] **Step 1: 扩展 state index 支持 GUID lookup**

Add to `FAFStateTreeStateIndex` in `StateTreeLinkResolver.h`:

```cpp
	TMap<FGuid, UStateTreeState*> ByGuid;
```

In `RegisterStateReference()`, after `ByPath`:

```cpp
	if (State.ID.IsValid())
	{
		Index.ByGuid.Add(State.ID, &State);
	}
```

In `ResolveStateReference()`, before id/path lookup:

```cpp
	FGuid ParsedGuid;
	if (FGuid::Parse(Reference, ParsedGuid))
	{
		if (UStateTreeState* const* StateByGuid = Index.ByGuid.Find(ParsedGuid))
		{
			OutState = *StateByGuid;
			return true;
		}
	}
```

- [ ] **Step 2: 写 resolver public API**

Create `StateTreeBindingResolver.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Generators/StateTree/StateTreeBindingTypes.h"
#include "Generators/StateTree/StateTreeLinkResolver.h"
#include "PropertyBindingPath.h"

class UStateTreeEditorData;
class UStateTreeState;
struct FStateTreeEditorNode;

namespace UE::AssetFactory::StateTree
{
	struct FAFStateTreeBindingIndex
	{
		const UStateTreeEditorData* EditorData = nullptr;
		const FAFStateTreeStateIndex* StateIndex = nullptr;
		TMap<FGuid, const FStateTreeEditorNode*> NodeByGuid;
		TMap<FString, const FStateTreeEditorNode*> GlobalNodeById;
		TMap<FString, const FStateTreeEditorNode*> GlobalTaskById;
		TMap<FString, const FStateTreeEditorNode*> EvaluatorById;
		TMap<const UStateTreeState*, TMap<FString, const FStateTreeEditorNode*>> StateNodeById;
	};

	bool BuildBindingIndex(
		const UStateTreeEditorData& EditorData,
		const FAFStateTreeStateIndex& StateIndex,
		FAFStateTreeBindingIndex& OutIndex,
		FString& OutError);

	bool ResolveBindingEndpointPath(
		const FAFStateTreeBindingIndex& Index,
		const FAFStateTreeBindingEndpointSpec& Endpoint,
		FPropertyBindingPath& OutPath,
		FString& OutError);

	TArray<FPropertyBindingPathSegment> MakeBindingPathSegments(
		const TArray<FAFStateTreeBindingPathSegmentSpec>& SegmentSpecs,
		FString& OutError);
}
```

- [ ] **Step 3: 实现 node index**

In `StateTreeBindingResolver.cpp`, implement recursive collection:

```cpp
static void RegisterNode(
	FAFStateTreeBindingIndex& Index,
	const FStateTreeEditorNode& Node,
	const FString& StableId,
	const UStateTreeState* OwnerState,
	TMap<FString, const FStateTreeEditorNode*>* ScopedById)
{
	if (Node.ID.IsValid())
	{
		Index.NodeByGuid.Add(Node.ID, &Node);
	}
	if (!StableId.IsEmpty() && ScopedById)
	{
		ScopedById->Add(StableId, &Node);
	}
}
```

Use node GUID string as fallback stable id because generated nodes currently do not retain original string id after build:

```cpp
const FString GuidId = Node.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
```

Register:

- `EditorData.Evaluators` into `EvaluatorById` and `GlobalNodeById`.
- `EditorData.GlobalTasks` into `GlobalTaskById` and `GlobalNodeById`.
- `State.Tasks`, `State.SingleTask`, `State.EnterConditions`, `State.Considerations`, and `Transition.Conditions` into per-state map and `NodeByGuid`.

- [ ] **Step 4: 实现 segment conversion**

Implement `MakeBindingPathSegments()`:

```cpp
TArray<FPropertyBindingPathSegment> UE::AssetFactory::StateTree::MakeBindingPathSegments(
	const TArray<FAFStateTreeBindingPathSegmentSpec>& SegmentSpecs,
	FString& OutError)
{
	TArray<FPropertyBindingPathSegment> Segments;
	for (const FAFStateTreeBindingPathSegmentSpec& Spec : SegmentSpecs)
	{
		if (Spec.Name.IsEmpty())
		{
			OutError = TEXT("StateTree binding path segment name must be non-empty");
			return {};
		}
		FPropertyBindingPathSegment Segment(FName(*Spec.Name), Spec.ArrayIndex);
#if WITH_EDITORONLY_DATA
		if (Spec.bHasGuid)
		{
			Segment.SetPropertyGuid(Spec.Guid);
		}
#endif
		Segments.Add(Segment);
	}
	return Segments;
}
```

Leave `instanceStruct/access` unsupported for generation in this task. If those fields are non-empty, return:

```cpp
OutError = FString::Printf(TEXT("StateTree binding path segment '%s' instanceStruct/access is not supported for generation yet"), *Spec.Name);
```

- [ ] **Step 5: 实现 root/state parameter endpoint**

Resolve:

```cpp
FPropertyBindingPath(Index.EditorData->GetRootParametersGuid(), Segments)
FPropertyBindingPath(State.Parameters.ID, Segments)
```

Rules:

- `rootParameter` must have non-empty path.
- `stateParameter` must have `state`.
- If the first segment matches a PropertyBag desc, set its property GUID from `FPropertyBagPropertyDesc::ID`.
- If input segment includes GUID, require it equals desc ID.
- Use `Path.UpdateSegments(Bag.GetPropertyBagStruct(), &Error)` and fail on false.

- [ ] **Step 6: 实现 node endpoint**

Resolve `node`, `task`, `globalTask`, `evaluator`, `enterCondition`, `transitionCondition`, `consideration`:

- Find state when endpoint requires `state`.
- Find node by GUID string first, then scoped id.
- Convert `section`:
	- `node` -> `Node.Node.GetScriptStruct()`
	- `instance` -> `Node.Instance.GetScriptStruct()` or `Node.InstanceObject->GetClass()`
	- `executionRuntimeData` -> `Node.ExecutionRuntimeData.GetScriptStruct()` or `Node.ExecutionRuntimeDataObject->GetClass()`
- Base GUID is always `Node.ID`.
- Build `FPropertyBindingPath(Node.ID, Segments)`.
- Validate path against the selected struct/class.

Diagnostic example:

```cpp
OutError = FString::Printf(TEXT("StateTree binding target node '%s' in state '%s' was not found"), *Endpoint.Node, *Endpoint.State);
```

- [ ] **Step 7: 实现 context endpoint minimum**

For 5A, support context through `EditorData.GetAllStructDescs()` if available in UE 5.7. If the exact helper name differs, use the public API on `UStateTreeEditorData` / schema that returns bindable structs.

Match by:

- `Endpoint.Name` against `FStateTreeBindableStructDesc::Name`.
- `Endpoint.Class` against `Desc.Struct->GetPathName()`.

Resolve `FPropertyBindingPath(Desc.ID, Segments)` and validate against `Desc.Struct`.

If no public API exposes bindable structs, implement a narrow error for context and proceed with parameter/node sources in 5A:

```cpp
OutError = TEXT("StateTree context binding source is not available through the UE 5.7 editor API used by AssetFactory");
```

Then Task 7 must keep context out of required positive smoke and document the limitation.

- [ ] **Step 8: Build resolver**

Run the normal project build. Expected: `Result: Succeeded`.

- [ ] **Step 9: Commit resolver**

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingResolver.* Source/AssetFactory/Private/Generators/StateTree/StateTreeLinkResolver.*
git commit -m "feat: resolve StateTree binding endpoints"
```

## Task 3: 写入普通 property bindings

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingBuilder.h`
- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingBuilder.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`

- [ ] **Step 1: 写 builder API**

Create `StateTreeBindingBuilder.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Generators/StateTree/StateTreeLinkResolver.h"

class UStateTreeEditorData;

namespace UE::AssetFactory::StateTree
{
	bool ApplyPropertyBindings(
		UStateTreeEditorData& EditorData,
		const FAFStateTreeStateIndex& StateIndex,
		const TSharedPtr<FJsonObject>& Config,
		FString& OutError);
}
```

- [ ] **Step 2: 实现普通 binding builder**

Create `StateTreeBindingBuilder.cpp` with:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeBindingBuilder.h"

#include "Generators/StateTree/StateTreeBindingResolver.h"
#include "Generators/StateTree/StateTreeJsonTypes.h"
#include "StateTreeEditorData.h"

bool UE::AssetFactory::StateTree::ApplyPropertyBindings(
	UStateTreeEditorData& EditorData,
	const FAFStateTreeStateIndex& StateIndex,
	const TSharedPtr<FJsonObject>& Config,
	FString& OutError)
{
	TArray<FAFStateTreeBindingSpec> Specs;
	if (!ParseBindingSpecsFromConfig(Config, Specs, OutError))
	{
		return false;
	}
	if (Specs.IsEmpty())
	{
		return true;
	}

	FAFStateTreeBindingIndex Index;
	if (!BuildBindingIndex(EditorData, StateIndex, Index, OutError))
	{
		return false;
	}

	TSet<FString> TargetKeys;
	for (const FAFStateTreeBindingSpec& Spec : Specs)
	{
		if (Spec.bHasFunction)
		{
			continue;
		}

		FPropertyBindingPath SourcePath;
		FPropertyBindingPath TargetPath;
		if (!ResolveBindingEndpointPath(Index, Spec.Source, SourcePath, OutError)
			|| !ResolveBindingEndpointPath(Index, Spec.Target, TargetPath, OutError))
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] '%s' failed to resolve: %s"), Spec.SourceIndex, *Spec.Id, *OutError);
			return false;
		}

		const FString TargetKey = TargetPath.GetStructID().ToString(EGuidFormats::DigitsWithHyphensLower) + TEXT(":") + TargetPath.ToString();
		if (TargetKeys.Contains(TargetKey))
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] '%s' targets duplicate path '%s'"), Spec.SourceIndex, *Spec.Id, *TargetPath.ToString());
			return false;
		}
		TargetKeys.Add(TargetKey);

		EditorData.AddPropertyBinding(SourcePath, TargetPath);
	}

	return true;
}
```

- [ ] **Step 3: 接到 ApplyStateTreeConfig**

In `StateTreeStateBuilder.cpp`, include:

```cpp
#include "Generators/StateTree/StateTreeBindingBuilder.h"
```

At the end of `ApplyStateTreeConfig()`, after transition/link finalization and before `return true`, call:

```cpp
	if (!UE::AssetFactory::StateTree::ApplyPropertyBindings(EditorData, Index, Config, OutError))
	{
		return false;
	}
```

- [ ] **Step 4: 写 ordinary fixture**

Create `TestData/ST_Bindings_Ordinary.json`:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Bindings_Ordinary",
	"Path": "/Game/AFSmoke",
	"Action": "CreateOrUpdate",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"rootParameters": {
		"DelaySeconds": { "type": "Float", "value": 0.35 }
	},
	"SubTrees": [
		{
			"id": "root",
			"name": "Root",
			"type": "State",
			"children": [
				{
					"id": "idle",
					"name": "Idle",
					"type": "State",
					"tasks": [
						{
							"id": "delay-task",
							"kind": "task",
							"type": "/Script/StateTreeModule.StateTreeDelayTask",
							"instance": {
								"properties": {
									"Duration": 0.1
								}
							}
						}
					]
				}
			]
		}
	],
	"bindings": [
		{
			"id": "duration-from-root-parameter",
			"source": {
				"kind": "rootParameter",
				"path": ["DelaySeconds"]
			},
			"target": {
				"kind": "task",
				"state": "Root/Idle",
				"node": "delay-task",
				"section": "instance",
				"path": ["Duration"]
			}
		}
	]
}
```

If `StateTreeDelayTask` instance property is not named `Duration` in UE 5.7, inspect the struct and replace with the actual float duration field before committing.

- [ ] **Step 5: Build and run MCP build**

Run:

```bash
npm --prefix MCP run build
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected: both succeed.

- [ ] **Step 6: Commit ordinary builder**

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingBuilder.* Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp TestData/ST_Bindings_Ordinary.json
git commit -m "feat: add StateTree property bindings"
```

## Task 4: 普通 binding extract

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.h`
- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`

- [ ] **Step 1: 写 extract API**

Create `StateTreeBindingExtract.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class UStateTreeEditorData;

namespace UE::AssetFactory::StateTree
{
	TArray<TSharedPtr<FJsonValue>> ExtractPropertyBindings(const UStateTreeEditorData* EditorData);
}
```

- [ ] **Step 2: 实现 segment extraction**

In `StateTreeBindingExtract.cpp`, implement:

```cpp
static TSharedPtr<FJsonValue> ExtractPathSegment(const FPropertyBindingPathSegment& Segment)
{
	const bool bNeedsObject =
		Segment.GetArrayIndex() != INDEX_NONE
#if WITH_EDITORONLY_DATA
		|| Segment.GetPropertyGuid().IsValid()
#endif
		|| Segment.GetInstanceStruct() != nullptr;
	if (!bNeedsObject)
	{
		return MakeShared<FJsonValueString>(Segment.GetName().ToString());
	}

	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("name"), Segment.GetName().ToString());
	if (Segment.GetArrayIndex() != INDEX_NONE)
	{
		Object->SetNumberField(TEXT("arrayIndex"), Segment.GetArrayIndex());
	}
#if WITH_EDITORONLY_DATA
	if (Segment.GetPropertyGuid().IsValid())
	{
		Object->SetStringField(TEXT("guid"), Segment.GetPropertyGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
	}
#endif
	if (const UStruct* InstanceStruct = Segment.GetInstanceStruct())
	{
		Object->SetStringField(TEXT("instanceStruct"), InstanceStruct->GetPathName());
	}
	return MakeShared<FJsonValueObject>(Object);
}
```

- [ ] **Step 3: 输出 generic endpoint**

For 5A, output conservative endpoints:

```json
{ "kind": "node", "node": "<guid>", "section": "instance", "path": [...] }
{ "kind": "rootParameter", "path": [...] }
```

Use `FPropertyBindingPath::GetStructID()` to identify root parameter by `EditorData->GetRootParametersGuid()`; otherwise default to node GUID. This is enough for `Generate -> Extract -> Generate`.

If a source/target struct ID cannot be classified, emit:

```json
{ "kind": "node", "node": "<guid>", "section": "instance", "path": [...] }
```

and let generation fail only if it truly cannot resolve.

- [ ] **Step 4: Wire top-level extract**

In `StateTreeGenerator.cpp`, include binding extract and add after root parameters:

```cpp
TArray<TSharedPtr<FJsonValue>> Bindings = UE::AssetFactory::StateTree::ExtractPropertyBindings(EditorData);
if (!Bindings.IsEmpty())
{
	OutJson->SetArrayField(TEXT("bindings"), Bindings);
}
```

- [ ] **Step 5: Build**

Run normal project build. Expected: `Result: Succeeded`.

- [ ] **Step 6: Commit ordinary extract**

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.* Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp
git commit -m "feat: extract StateTree property bindings"
```

## Task 5: Property function binding

**Files:**

- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingBuilder.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingResolver.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.cpp`
- Create: `TestData/ST_Bindings_Function.json`
- Create: `TestData/ST_Bindings_Invalid_BadFunctionType.json`

- [ ] **Step 1: Add function struct resolver**

In `StateTreeBindingBuilder.cpp`, add:

```cpp
#include "StateTreePropertyFunctionBase.h"
#include "Generators/StateTree/StateTreeClassResolver.h"
```

Implement:

```cpp
static UScriptStruct* ResolvePropertyFunctionStruct(const FString& TypeName, FString& OutError)
{
	UScriptStruct* Struct = UE::AssetFactory::StateTree::ResolveScriptStruct(TypeName);
	if (!Struct)
	{
		OutError = FString::Printf(TEXT("Unknown StateTree property function type '%s'"), *TypeName);
		return nullptr;
	}
	if (!Struct->IsChildOf(FStateTreePropertyFunctionBase::StaticStruct()))
	{
		OutError = FString::Printf(TEXT("StateTree property function type '%s' is not a FStateTreePropertyFunctionBase"), *TypeName);
		return nullptr;
	}
	return Struct;
}
```

- [ ] **Step 2: Add recursive function application**

In `StateTreeBindingBuilder.cpp`, add a helper:

```cpp
static bool ApplyFunctionBinding(
	UStateTreeEditorData& EditorData,
	const UE::AssetFactory::StateTree::FAFStateTreeBindingIndex& Index,
	const FAFStateTreeBindingFunctionSpec& FunctionSpec,
	const FPropertyBindingPath& TargetPath,
	FPropertyBindingPath& OutFunctionOutputPath,
	FString& OutError)
{
	UScriptStruct* FunctionStruct = ResolvePropertyFunctionStruct(FunctionSpec.Type, OutError);
	if (!FunctionStruct)
	{
		return false;
	}

	TArray<FPropertyBindingPathSegment> OutputSegments = UE::AssetFactory::StateTree::MakeBindingPathSegments(FunctionSpec.OutputPath, OutError);
	if (!OutError.IsEmpty())
	{
		return false;
	}

	OutFunctionOutputPath = EditorData.EditorBindings.AddFunctionBinding(FunctionStruct, OutputSegments, TargetPath);
	const FGuid FunctionNodeID = OutFunctionOutputPath.GetStructID();

	for (const TPair<FString, FAFStateTreeBindingFunctionInputSpec>& Pair : FunctionSpec.Inputs)
	{
		FPropertyBindingPath InputTargetPath(FunctionNodeID, FName(*Pair.Key));
		FPropertyBindingPath InputSourcePath;
		if (Pair.Value.bHasSource)
		{
			if (!UE::AssetFactory::StateTree::ResolveBindingEndpointPath(Index, Pair.Value.Source, InputSourcePath, OutError))
			{
				return false;
			}
		}
		else
		{
			if (!Pair.Value.Function.IsValid())
			{
				OutError = FString::Printf(TEXT("Nested StateTree property function input '%s' is missing function data"), *Pair.Key);
				return false;
			}
			if (!ApplyFunctionBinding(EditorData, Index, *Pair.Value.Function, InputTargetPath, InputSourcePath, OutError))
			{
				OutError = FString::Printf(TEXT("Nested StateTree property function input '%s' failed: %s"), *Pair.Key, *OutError);
				return false;
			}
			continue;
		}
		EditorData.AddPropertyBinding(InputSourcePath, InputTargetPath);
	}

	return true;
}
```

- [ ] **Step 3: Add nested function fixture check**

After the single function fixture passes, create a temporary local JSON based on `ST_Bindings_Function.json` with a nested `function` under input `Left`: inner add computes `BaseDelay + BonusDelay`, outer add adds `BonusDelay` again. Generate it through MCP during smoke and verify it saves. Do not keep this as a committed fixture unless the JSON remains compact enough to be useful; the committed acceptance fixture is the single function case.

- [ ] **Step 4: Process function specs**

In `ApplyPropertyBindings()`, before ordinary `continue`, handle:

```cpp
if (Spec.bHasFunction)
{
	FPropertyBindingPath TargetPath;
	if (!ResolveBindingEndpointPath(Index, Spec.Target, TargetPath, OutError))
	{
		OutError = FString::Printf(TEXT("StateTree binding[%d] '%s' function target failed to resolve: %s"), Spec.SourceIndex, *Spec.Id, *OutError);
		return false;
	}
	const FString TargetKey = TargetPath.GetStructID().ToString(EGuidFormats::DigitsWithHyphensLower) + TEXT(":") + TargetPath.ToString();
	if (TargetKeys.Contains(TargetKey))
	{
		OutError = FString::Printf(TEXT("StateTree binding[%d] '%s' targets duplicate path '%s'"), Spec.SourceIndex, *Spec.Id, *TargetPath.ToString());
		return false;
	}
	TargetKeys.Add(TargetKey);

	FPropertyBindingPath FunctionOutputPath;
	if (!ApplyFunctionBinding(EditorData, Index, Spec.Function, TargetPath, FunctionOutputPath, OutError))
	{
		OutError = FString::Printf(TEXT("StateTree binding[%d] '%s' function failed: %s"), Spec.SourceIndex, *Spec.Id, *OutError);
		return false;
	}
	continue;
}
```

- [ ] **Step 5: Add function fixture**

Create `TestData/ST_Bindings_Function.json`:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Bindings_Function",
	"Path": "/Game/AFSmoke",
	"Action": "CreateOrUpdate",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"rootParameters": {
		"BaseDelay": { "type": "Float", "value": 0.2 },
		"BonusDelay": { "type": "Float", "value": 0.15 }
	},
	"SubTrees": [
		{
			"id": "root",
			"name": "Root",
			"type": "State",
			"children": [
				{
					"id": "idle",
					"name": "Idle",
					"type": "State",
					"tasks": [
						{
							"id": "delay-task",
							"kind": "task",
							"type": "/Script/StateTreeModule.StateTreeDelayTask",
							"instance": {
								"properties": {
									"Duration": 0.1
								}
							}
						}
					]
				}
			]
		}
	],
	"bindings": [
		{
			"id": "duration-from-add-function",
			"target": {
				"kind": "task",
				"state": "Root/Idle",
				"node": "delay-task",
				"section": "instance",
				"path": ["Duration"]
			},
			"function": {
				"type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
				"output": ["Result"],
				"inputs": {
					"Left": { "source": { "kind": "rootParameter", "path": ["BaseDelay"] } },
					"Right": { "source": { "kind": "rootParameter", "path": ["BonusDelay"] } }
				}
			}
		}
	]
}
```

If UE 5.7 names the float add struct differently, inspect `StateTreeFloatPropertyFunctions.h` and update the fixture/type.

- [ ] **Step 6: Add bad function fixture**

Create `TestData/ST_Bindings_Invalid_BadFunctionType.json` by copying the function fixture and replacing function type with:

```json
"type": "/Script/StateTreeModule.StateTreeDelayTask"
```

Expected MCP error includes `not a FStateTreePropertyFunctionBase`.

- [ ] **Step 7: Build**

Run normal project build. Expected: `Result: Succeeded`.

- [ ] **Step 8: Commit function bindings**

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingBuilder.cpp Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.cpp TestData/ST_Bindings_Function.json TestData/ST_Bindings_Invalid_BadFunctionType.json
git commit -m "feat: add StateTree property function bindings"
```

## Task 6: Invalid fixtures and diagnostics

**Files:**

- Create invalid JSON fixtures listed in the file map.
- Modify resolver/builder/parser files as diagnostics require.

- [ ] **Step 1: Unknown source fixture**

Create `TestData/ST_Bindings_Invalid_UnknownSource.json` by copying ordinary fixture and changing source path to:

```json
"path": ["MissingParameter"]
```

Expected HTTP/MCP error includes `MissingParameter`.

- [ ] **Step 2: Unknown target fixture**

Create `TestData/ST_Bindings_Invalid_UnknownTarget.json` by copying ordinary fixture and changing target node to:

```json
"node": "missing-task"
```

Expected error includes `missing-task`.

- [ ] **Step 3: Bad path fixture**

Create `TestData/ST_Bindings_Invalid_BadPath.json` by copying ordinary fixture and changing target path to:

```json
"path": ["MissingDuration"]
```

Expected error includes `MissingDuration`.

- [ ] **Step 4: Duplicate target fixture**

Create `TestData/ST_Bindings_Invalid_DuplicateTarget.json` by copying ordinary fixture and adding a second binding with the same target and different source/id.

Expected error includes `duplicate path`.

- [ ] **Step 5: Type mismatch fixture**

Create `TestData/ST_Bindings_Invalid_TypeMismatch.json` by adding a root string parameter:

```json
"DebugLabel": { "type": "String", "value": "bad" }
```

and binding it to delay task `Duration`.

Expected compile error includes copy/type incompatibility. If UE compiler message is terse, wrap compile failure in `FStateTreeGenerator::CompileStateTree()` with `StateTree compiler failed after applying bindings: ...` only when `bindings` exists in config.

- [ ] **Step 6: Run invalids manually through MCP once Editor is up**

Use existing MCP `generate_assets` flow. Each invalid fixture must return failure/HTTP 400 and include the expected snippet.

- [ ] **Step 7: Commit diagnostics fixtures**

```bash
git add TestData/ST_Bindings_Invalid_*.json Source/AssetFactory/Private/Generators/StateTree Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp
git commit -m "test: add StateTree binding diagnostics fixtures"
```

## Task 7: MCP schema docs

**Files:**

- Modify: `MCP/schemas/StateTree.md`

- [ ] **Step 1: Replace old binding limitation sentence**

In the opening paragraph, replace:

```md
Property bindings and property function bindings are supported through the top-level `bindings` array.
```

- [ ] **Step 2: Add bindings field**

In the top-level field table, add:

```md
| `bindings` | array | No | Property bindings and property function bindings |
```

- [ ] **Step 3: Add Binding section**

Add a `## Bindings` section after `## Node Contract` with ordinary and function JSON examples from `ST_Bindings_Ordinary.json` and `ST_Bindings_Function.json`.

Document:

- `source`, `target`, `function`.
- source/target `kind` values.
- `section` values.
- path segment string/object shape.
- no string DSL support.

- [ ] **Step 4: Build MCP**

Run:

```bash
npm --prefix MCP run build
```

Expected: TypeScript build succeeds.

- [ ] **Step 5: Commit schema**

```bash
git add MCP/schemas/StateTree.md
git commit -m "docs: document StateTree bindings schema"
```

## Task 8: Round-trip verifier

**Files:**

- Create: `docs/superpowers/verification/statetree_binding_roundtrip_check.py`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.cpp` as needed.

- [ ] **Step 1: Write verifier script**

Create `statetree_binding_roundtrip_check.py`:

```python
#!/usr/bin/env python3
import json
import sys
from pathlib import Path

def stable_binding(binding):
	b = dict(binding)
	b.pop("id", None)
	return b

def load(path):
	with Path(path).open("r", encoding="utf-8") as f:
		data = json.load(f)
	return data.get("bindings", data.get("Bindings", []))

def main():
	if len(sys.argv) != 3:
		print("usage: statetree_binding_roundtrip_check.py extract_a.json extract_b.json", file=sys.stderr)
		return 2
	a = sorted((stable_binding(x) for x in load(sys.argv[1])), key=lambda x: json.dumps(x, sort_keys=True))
	b = sorted((stable_binding(x) for x in load(sys.argv[2])), key=lambda x: json.dumps(x, sort_keys=True))
	if a != b:
		print("StateTree binding round-trip mismatch", file=sys.stderr)
		print(json.dumps({"left": a, "right": b}, indent=2, sort_keys=True), file=sys.stderr)
		return 1
	print(f"StateTree binding round-trip stable: {len(a)} bindings")
	return 0

if __name__ == "__main__":
	raise SystemExit(main())
```

- [ ] **Step 2: Use verifier in smoke**

After generating and extracting `ST_Bindings_Ordinary` twice, run:

```bash
python3 docs/superpowers/verification/statetree_binding_roundtrip_check.py /tmp/ST_Bindings_Ordinary.extract1.json /tmp/ST_Bindings_Ordinary.extract2.json
```

Expected: `StateTree binding round-trip stable: 1 bindings`.

- [ ] **Step 3: Commit verifier**

```bash
git add docs/superpowers/verification/statetree_binding_roundtrip_check.py Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.cpp
git commit -m "test: verify StateTree binding round trip"
```

## Task 9: Final build and smoke

**Files:**

- No planned code changes unless smoke reveals defects.

- [ ] **Step 1: Clean status**

Run:

```bash
git status --short
```

Expected: clean or only intentional smoke-generated assets outside this AssetFactory worktree.

- [ ] **Step 2: MCP build**

Run:

```bash
npm --prefix MCP run build
```

Expected: succeeds.

- [ ] **Step 3: Normal project compile**

Run from ProjectRPG root, not the AssetFactory worktree:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected: `Result: Succeeded`.

- [ ] **Step 4: Launch real GUI editor**

Use the same launch method that worked for previous StateTree smoke. Do not add `-NullRHI`; do not use `UnrealEditor-Cmd`.

Expected: editor opens graphically.

- [ ] **Step 5: Positive MCP smoke**

Run `generate_assets` for:

- `TestData/ST_Bindings_Ordinary.json`
- `TestData/ST_Bindings_Function.json`

Expected:

- HTTP/MCP success.
- Assets appear under `/Game/AFSmoke`.
- GUI details show binding indicator on delay task duration.
- Function fixture shows property function binding and remains after save/reopen.

- [ ] **Step 6: Invalid MCP smoke**

Run invalid fixtures:

- `ST_Bindings_Invalid_UnknownSource`
- `ST_Bindings_Invalid_UnknownTarget`
- `ST_Bindings_Invalid_BadPath`
- `ST_Bindings_Invalid_DuplicateTarget`
- `ST_Bindings_Invalid_BadFunctionType`
- `ST_Bindings_Invalid_TypeMismatch`

Expected: each fails with HTTP 400/failure and expected readable snippet.

- [ ] **Step 7: Extract round-trip smoke**

Extract `ST_Bindings_Ordinary` to `/tmp/ST_Bindings_Ordinary.extract1.json`, generate from it, extract again to `/tmp/ST_Bindings_Ordinary.extract2.json`, then run the verifier.

Expected:

```text
StateTree binding round-trip stable: 1 bindings
```

- [ ] **Step 8: Commit smoke fixes if any**

If smoke required fixes:

```bash
git add Source/AssetFactory/Private/Generators/StateTree Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp TestData docs/superpowers/verification MCP/schemas/StateTree.md
git commit -m "fix: stabilize StateTree binding smoke"
```

If no fixes were needed, do not create an empty commit.

## Task 10: Integration finish

**Files:**

- ProjectRPG root submodule pointer after AssetFactory branch is merged/pushed.

- [ ] **Step 1: Review commit history**

Run:

```bash
git log --oneline origin/master..HEAD
```

Expected: design plus implementation commits.

- [ ] **Step 2: Merge/push AssetFactory branch per project workflow**

From AssetFactory worktree:

```bash
git switch master
git merge --ff-only feature/statetree-property-bindings
git push origin master
```

If `master` is still checked out in another worktree, use that master worktree for the merge or remove the stale worktree after confirming it is no longer needed.

- [ ] **Step 3: Update ProjectRPG submodule pointer**

From `/Volumes/Mac/GameDev/ProjectRPG`:

```bash
git add Plugins/AssetFactory
git commit -m "Update AssetFactory StateTree bindings"
git push origin master
```

- [ ] **Step 4: Final report**

Report:

- AssetFactory final commit hash.
- ProjectRPG submodule pointer commit hash.
- Build evidence: MCP build and ProjectRPGEditor normal compile.
- GUI smoke evidence: assets opened and what the details panel showed.
- Invalid fixture evidence: each expected failure snippet.
- Round-trip verifier output.
