# UBlueprint Graph Regions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为普通 `/Script/Engine.Blueprint` 的 AssetDocument 增加结构化 graph sidecar 支持，第一轮完成共享 `GraphCore` 与 `Body.UbergraphPages` Tier 1 apply/extract/diff。

**Architecture:** `GraphCore` 只负责 JSON 形状、identity、canonicalization、diagnostics、`Definitions` 解析和 staged orchestration；UE/K2 生命周期放在 `K2GraphAdapter` 与小型 node adapters 中。实现不新增 `BlueprintGenerator`，不走 BSL/旧 generator 路径，不在 core 或 region 主流程中维护 function/property/project class 硬编码清单。

**Tech Stack:** Unreal Engine 5.7 editor module C++、`UBlueprint`、`UEdGraph`、`UEdGraphSchema_K2`、`FBlueprintEditorUtils`、`FKismetEditorUtilities`、AssetDocument capability/profile APIs、UE automation tests、`C:/AVH1` validation host、MCP schema/docs。

---

## Implementation Context

**Spec:** `docs/superpowers/specs/2026-06-19-ublueprint-graph-regions-design.md`

**Parent UBlueprint plan:** `docs/superpowers/plans/2026-06-19-ublueprint-asset-document-implementation.md`

**Implementation branch:** `feature/asset-document-ublueprint-impl`

**Implementation worktree:** `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-ublueprint-impl`

**Validation host:** `C:/AVH1`，其 `Plugins/UECopilot` 应指向本 worktree，避免主项目同名插件遮蔽验证结果。

**Execution strategy:** 每个实现 task 由独立 subagent 执行，主线在 task 之间做 diff review、必要修复和 checkpoint commit。每个 task 开始记录 `TASK_BASE=HEAD`；review 只审 `TASK_BASE..HEAD`；验证通过后提交 checkpoint。

**First implementation scope:** 只支持 `Body.UbergraphPages` 的 Tier 1 K2 节点：`K2Node_Event`、`K2Node_CallFunction`、`K2Node_VariableGet`、`K2Node_VariableSet`、`K2Node_Self`。`FunctionGraphs`、`MacroGraphs`、`Timelines` 保持受保护或明确 unsupported，并作为后续 step 使用同一 `GraphCore` 扩展。

## Core Constraints

- 不新增 `BlueprintGenerator`，不把旧 `BlueprintGenerator` / BSL 作为 AssetDocument apply 路径。
- `GraphCore` 不 include 具体 `K2Node_*` 头文件，不按 node/function/property 名称写大 `switch`。
- node 支持通过 `NodeAdapterRegistry` 与 resolved `UClass`/reflected capability 扩展；adapter 只在 UE 生命周期需要时 include 最小 node class headers。
- `K2Node_CallFunction` 不允许 function whitelist；通过 `MemberRef` 解析 `UFunction`，不支持的 pin/lifecycle pattern 返回 `UnsupportedGraphFunction` 或更窄诊断。
- unsupported fallback 必须包含 `Code`、`Path`、`Class`、`Capability`、`Member`、`Reason`、`SuggestedAction`，不得输出 raw UE graph dump。
- `NodeSpec.Id` 是 graph 内稳定、唯一、agent-friendly 的 sidecar identity，不要求人工手写友好。
- graph region 是 authoritative：sidecar 缺失 graph/node/link/pin default 表示删除或 reset 到 baseline。
- docs/report 使用中文；code identifiers 和 schema keys 保持英文。

## File Structure

新增 graph core 文件：

- `Source/AssetDocument/Private/Graphs/AssetDocumentGraphTypes.h/.cpp`
  - 定义 `FAssetDocumentGraphSpec`、`FAssetDocumentNodeSpec`、`FAssetDocumentPinOverrideSpec`、`FAssetDocumentLinkSpec`、`FAssetDocumentMemberRef`、diagnostic payload 和 canonical sort helpers。
- `Source/AssetDocument/Private/Graphs/AssetDocumentGraphParser.h/.cpp`
  - 解析/验证 graph regions、expanded/compact link endpoint、unknown field、duplicate ids、id regex、schema path。
- `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDefinitionResolver.h/.cpp`
  - 解析 `Definitions`、`DefinitionRef`、inline ref canonicalization、cycle/unresolved diagnostics。
- `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDiff.h/.cpp`
  - 比较 canonical graph specs，产出 graph/node/pin/link diff entries。
- `Source/AssetDocument/Private/Graphs/AssetDocumentNodeAdapter.h/.cpp`
  - 定义 `IAssetDocumentNodeAdapter`、adapter registry、unsupported fallback diagnostic builder。

新增 UE/K2 graph adapter 文件：

- `Source/AssetDocument/Private/Graphs/K2GraphAdapter.h/.cpp`
  - 负责 `UEdGraph` create/find/rebuild、node staging、pin allocation、link validation、compile/rollback integration。
- `Source/AssetDocument/Private/Graphs/K2NodeAdapters/EventNodeAdapter.h/.cpp`
  - `UK2Node_Event` 的 reflected event binding/extract。
- `Source/AssetDocument/Private/Graphs/K2NodeAdapters/CallFunctionNodeAdapter.h/.cpp`
  - `UK2Node_CallFunction` 的 `UFunction` resolution、`SetFromFunction`、pin support diagnostics。
- `Source/AssetDocument/Private/Graphs/K2NodeAdapters/VariableGetSetNodeAdapter.h/.cpp`
  - `UK2Node_VariableGet` / `UK2Node_VariableSet` 的 `FProperty` / Blueprint variable resolution。
- `Source/AssetDocument/Private/Graphs/K2NodeAdapters/SelfNodeAdapter.h/.cpp`
  - `UK2Node_Self` 的 creation/extract。

接入 UBlueprint profile/capability：

- `Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.h/.cpp`
  - 把 `Body.UbergraphPages` 连接到 `GraphCore` + `K2GraphAdapter`，并隔离 UBlueprint-specific apply/extract/diff。
- `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.h/.cpp`
  - 仅增加 graph adapter hook、apply ordering 和 protected-region 状态变更，不塞入 node/pin/link 实现。
- `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp`
  - 如需要，更新 schema hint 或 region policy details。
- `Source/AssetDocument/AssetDocument.Build.cs`
  - 仅在新增 headers/API 确实需要时补充 `Kismet` / `KismetCompiler` 等依赖。

测试与文档：

- `Source/AssetDocument/Private/Tests/AssetDocumentGraphCoreTests.cpp`
  - 纯 parser/definition/canonical/diagnostic tests，不依赖具体 K2 node class。
- `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp`
  - UBlueprint EventGraph apply/extract/diff/unsupported fallback tests。
- `MCP/schemas/AssetDocument.md`
  - 文档化 graph region schema 和当前 Tier 1 支持范围。
- `docs/superpowers/specs/asset-document-deferred-fields/2026-06-19-ublueprint.md`
  - 缩窄已经完成的 graph 延期项，保留 Function/Macro/Timeline 限制。
- `docs/superpowers/verification/asset_document_ublueprint_graph_http_smoke.py`
  - 外部 HTTP apply-file/extract/diff smoke。
- `docs/reports/asset-document-ublueprint-graph-regions-report.md`
  - 最终中文验证报告。

---

### Task 1: GraphCore Types, Parser, And Diagnostics

**Files:**
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphTypes.h`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphTypes.cpp`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphParser.h`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphParser.cpp`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentNodeAdapter.h`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentNodeAdapter.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentGraphCoreTests.cpp`

- [x] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

Expected: clean status and non-empty `TASK_BASE`.

- [x] **Step 2: Add failing GraphCore parser tests**

Create `AssetDocumentGraphCoreTests.cpp` with automation tests named:

- `AssetFactory.AssetDocument.GraphCore.ParseValidEventGraph`
- `AssetFactory.AssetDocument.GraphCore.RejectDuplicateGraphNames`
- `AssetFactory.AssetDocument.GraphCore.RejectDuplicateNodeIds`
- `AssetFactory.AssetDocument.GraphCore.RejectInvalidNodeId`
- `AssetFactory.AssetDocument.GraphCore.RejectDuplicateLinks`
- `AssetFactory.AssetDocument.GraphCore.AcceptCompactLinkInputAsSugar`
- `AssetFactory.AssetDocument.GraphCore.RejectUnknownGraphField`

Minimum valid input used by `ParseValidEventGraph`:

```json
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "BeginPlay",
      "Class": "/Script/BlueprintGraph.K2Node_Event",
      "Member": {
        "Kind": "MemberRef",
        "OwnerClass": "/Script/Engine.Actor",
        "Name": "ReceiveBeginPlay"
      },
      "Position": { "X": 0, "Y": 0 }
    }
  ],
  "Links": []
}
```

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.GraphCore;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/GraphCoreTask1"
```

Expected: compile or tests fail because GraphCore APIs do not exist.

- [x] **Step 3: Implement minimal GraphCore model and parser**

Implement:

- `FAssetDocumentGraphParseOptions`
- `FAssetDocumentGraphDiagnostic`
- `FAssetDocumentGraphEndpoint`
- `FAssetDocumentLinkSpec`
- `FAssetDocumentMemberRef`
- `FAssetDocumentPinOverrideSpec`
- `FAssetDocumentNodeSpec`
- `FAssetDocumentGraphSpec`
- `FAssetDocumentGraphParseResult`
- `FAssetDocumentGraphParser::ParseGraphArray`
- `FAssetDocumentGraphParser::ParseSingleGraph`
- `FAssetDocumentGraphParser::WriteCanonicalGraphArray`

Rules:

- accepted graph fields: `Name`、`Schema`、`GraphGuid`、`Category`、`Description`、`Signature`、`Nodes`、`Links`;
- accepted node fields: `Id`、`NodeGuid`、`Class`、`Capability`、`Member`、`PinOverrides`、`Position`、`Comment`;
- accepted pin override fields: `Pin`、`Direction`、`Type`、`DefaultValue`、`DefaultObject`、`DefaultTextValue`、`Hidden`、`AdvancedView`;
- node ids and pin ids use `^[A-Za-z_][A-Za-z0-9_-]*$`;
- graph names are unique inside a region;
- node ids are unique inside a graph;
- duplicate expanded links fail with `DuplicateGraphLink`;
- compact endpoint sugar parses only when `Node.Pin` has exactly one dot and both tokens match the id regex;
- canonical serializer always writes expanded link object shape.

- [x] **Step 4: Add adapter registry interface without K2 inventory**

In `AssetDocumentNodeAdapter.*`, define:

- `FAssetDocumentNodeAdapterContext`
- `FAssetDocumentUnsupportedNodeDiagnostic`
- `IAssetDocumentNodeAdapter`
- `FAssetDocumentNodeAdapterRegistry`

Task 1 registry only needs register/find by resolved class path or `UClass*`; it must not include K2 node adapter implementations yet.

- [x] **Step 5: Run GraphCore tests**

Run the Task 1 UBT and automation commands again.

Expected: `AssetFactory.AssetDocument.GraphCore` passes.

- [x] **Step 6: Commit Task 1**

Run:

```powershell
git add Source/AssetDocument/Private/Graphs/AssetDocumentGraphTypes.* Source/AssetDocument/Private/Graphs/AssetDocumentGraphParser.* Source/AssetDocument/Private/Graphs/AssetDocumentNodeAdapter.* Source/AssetDocument/Private/Tests/AssetDocumentGraphCoreTests.cpp
git commit -m "feat(assetdoc): add graph core parser"
```

---

### Task 2: Definitions Resolution And Canonical Graph Diff

**Files:**
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDefinitionResolver.h`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDefinitionResolver.cpp`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDiff.h`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDiff.cpp`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphTypes.h/.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentGraphCoreTests.cpp`

- [x] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [x] **Step 2: Add failing definition and diff tests**

Add automation tests:

- `AssetFactory.AssetDocument.GraphCore.ResolveDefinitionRefs`
- `AssetFactory.AssetDocument.GraphCore.RejectCircularDefinitionRefs`
- `AssetFactory.AssetDocument.GraphCore.RejectUnresolvedDefinitionRef`
- `AssetFactory.AssetDocument.GraphCore.CompareInlineAndDefinitionRefAsEqual`
- `AssetFactory.AssetDocument.GraphCore.ReportMissingExtraChangedGraphDiffs`

Use `Definitions` input:

```json
{
  "Func.KismetSystemLibrary.PrintString": {
    "Kind": "MemberRef",
    "OwnerClass": "/Script/Engine.KismetSystemLibrary",
    "Name": "PrintString"
  }
}
```

Expected diagnostics:

- `CircularDefinitionReference`
- `UnresolvedDefinitionReference`
- `UnknownDefinitionKind`

- [x] **Step 3: Implement definition resolver**

Implement support for these definition kinds:

- `ClassRef`
- `AssetRef`
- `MemberRef`
- `PinType`
- `Literal`

Rules:

- definition ids match `^[A-Za-z_][A-Za-z0-9_.:-]*$`;
- resolver detects cycles before mutation;
- inline refs and equivalent `DefinitionRef` resolve to the same canonical JSON for semantic comparison;
- extractor style is not changed here: no opportunistic hoisting into `Definitions`.

- [x] **Step 4: Implement graph diff helper**

Implement comparison for:

- graph missing/extra/changed under `/Body/UbergraphPages/<GraphName>`;
- node missing/extra/changed under `/Body/UbergraphPages/<GraphName>/Nodes/<NodeId>`;
- pin override missing/extra/changed under `/Body/UbergraphPages/<GraphName>/Nodes/<NodeId>/PinOverrides/<PinId>`;
- link missing/extra under `/Body/UbergraphPages/<GraphName>/Links/<FromNode>:<FromPin>-><ToNode>:<ToPin>`;
- unsupported status passthrough when extract reports incomplete evidence.

- [x] **Step 5: Run GraphCore tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.GraphCore;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/GraphCoreTask2"
```

Expected: all `GraphCore` tests pass.

- [x] **Step 6: Commit Task 2**

Run:

```powershell
git add Source/AssetDocument/Private/Graphs Source/AssetDocument/Private/Tests/AssetDocumentGraphCoreTests.cpp
git commit -m "feat(assetdoc): resolve graph definitions"
```

---

### Task 3: UBlueprint Graph Region Hook And Unsupported Fallback

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.h`
- Create: `Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.h`
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp` only if existing helper extraction is needed.

- [x] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [x] **Step 2: Add failing UBlueprint graph validation tests**

Create tests:

- `AssetFactory.AssetDocument.UBlueprint.GraphValidation.AcceptsTier1ShapeBeforeApply`
- `AssetFactory.AssetDocument.UBlueprint.GraphValidation.RejectsUnsupportedFunctionGraphs`
- `AssetFactory.AssetDocument.UBlueprint.GraphValidation.RejectsUnsupportedMacroGraphs`
- `AssetFactory.AssetDocument.UBlueprint.GraphValidation.RejectsTimelinesUntilImplemented`
- `AssetFactory.AssetDocument.UBlueprint.GraphValidation.UnsupportedNodeHasActionableDiagnostic`
- `AssetFactory.AssetDocument.UBlueprint.GraphValidation.UnsupportedFunctionHasActionableDiagnostic`

Expected fallback JSON object fields:

```json
{
  "Code": "UnsupportedGraphNodeClass",
  "Path": "/Body/UbergraphPages/0/Nodes/0",
  "Class": "/Script/BlueprintGraph.K2Node_IfThenElse",
  "Capability": "",
  "Member": null,
  "Reason": "node class has no registered AssetDocument adapter in the current tier",
  "SuggestedAction": "add a thin node adapter for this class or remove the node from the managed graph"
}
```

- [x] **Step 3: Implement `UBlueprintGraphRegionAdapter` validation hook**

Rules:

- `Body.UbergraphPages` may be non-empty only if every graph parses through `GraphCore`;
- `FunctionGraphs`、`MacroGraphs`、`Timelines` remain rejected when non-empty, with their existing protection tests updated only if the message/code changes intentionally;
- unsupported `UbergraphPages` node class fails with `UnsupportedGraphNodeClass`;
- unsupported `K2Node_CallFunction` pattern fails with `UnsupportedGraphFunction` or a narrower code;
- diagnostics must include `Reason` and `SuggestedAction`.

- [x] **Step 4: Wire capability without moving graph logic into the big capability**

`FUBlueprintAssetDocumentCapability` should delegate graph validation/extract/diff/apply to `FUBlueprintGraphRegionAdapter`. It should keep existing variables/components/class-defaults behavior unchanged.

- [x] **Step 5: Run UBlueprint graph validation tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint.GraphValidation;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintGraphValidation"
```

Expected: graph validation tests pass and existing `AssetFactory.AssetDocument.UBlueprint.UnsupportedGraphProtection` still passes for regions not implemented in this task.

- [x] **Step 6: Commit Task 3**

Run:

```powershell
git add Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.* Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.* Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp
git commit -m "feat(assetdoc): validate ublueprint graph regions"
```

---

### Task 4: K2 Tier 1 Node Adapters And Extract

**Files:**
- Create: `Source/AssetDocument/Private/Graphs/K2GraphAdapter.h`
- Create: `Source/AssetDocument/Private/Graphs/K2GraphAdapter.cpp`
- Create: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/EventNodeAdapter.h`
- Create: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/EventNodeAdapter.cpp`
- Create: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/CallFunctionNodeAdapter.h`
- Create: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/CallFunctionNodeAdapter.cpp`
- Create: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/VariableGetSetNodeAdapter.h`
- Create: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/VariableGetSetNodeAdapter.cpp`
- Create: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/SelfNodeAdapter.h`
- Create: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/SelfNodeAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp`
- Modify: `Source/AssetDocument/AssetDocument.Build.cs` only if compile proves a missing module dependency.

- [x] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [x] **Step 2: Add failing extract tests**

Add tests:

- `AssetFactory.AssetDocument.UBlueprint.GraphExtract.ExtractsBeginPlayPrintString`
- `AssetFactory.AssetDocument.UBlueprint.GraphExtract.ExtractsVariableGetSetAndSelf`
- `AssetFactory.AssetDocument.UBlueprint.GraphExtract.SkipsUnsupportedExistingNodes`
- `AssetFactory.AssetDocument.UBlueprint.GraphExtract.KeepsPinOverridesSparse`

Create test Blueprints using UE APIs, not by importing a raw graph dump.

- [x] **Step 3: Implement registry registration for Tier 1 adapters**

Register adapters for resolved node classes:

- `/Script/BlueprintGraph.K2Node_Event`
- `/Script/BlueprintGraph.K2Node_CallFunction`
- `/Script/BlueprintGraph.K2Node_VariableGet`
- `/Script/BlueprintGraph.K2Node_VariableSet`
- `/Script/BlueprintGraph.K2Node_Self`

This list lives only in UBlueprint/K2 adapter registration, not in `GraphCore`.

- [x] **Step 4: Implement extract for Tier 1 adapters**

Adapter extract rules:

- Event: output `Class`、stable `Id`、`MemberRef`、`Position`、optional `NodeGuid`;
- CallFunction: output reflected `MemberRef`; sparse input pin defaults only when non-baseline and not linked;
- Variable get/set: resolve Blueprint variables, parent `FProperty`, and component variables through reflection/staged evidence;
- Self: no `MemberRef`;
- all links serialize as expanded `LinkSpec` objects;
- unsupported existing nodes are reported in `_Skipped.Graphs` evidence and diff status `unsupported`.

- [x] **Step 5: Run extract tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint.GraphExtract;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintGraphExtract"
```

Expected: focused graph extract tests pass.

- [x] **Step 6: Commit Task 4**

Run:

```powershell
git add Source/AssetDocument/Private/Graphs Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.* Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp Source/AssetDocument/AssetDocument.Build.cs
git commit -m "feat(assetdoc): extract ublueprint event graphs"
```

---

### Task 5: UBlueprint `UbergraphPages` Diff

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDiff.*`
- Modify: `Source/AssetDocument/Private/Graphs/K2GraphAdapter.*`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [ ] **Step 2: Add failing graph diff tests**

Add tests:

- `AssetFactory.AssetDocument.UBlueprint.GraphDiff.UnchangedAfterExtract`
- `AssetFactory.AssetDocument.UBlueprint.GraphDiff.ReportsMissingNode`
- `AssetFactory.AssetDocument.UBlueprint.GraphDiff.ReportsExtraNode`
- `AssetFactory.AssetDocument.UBlueprint.GraphDiff.ReportsChangedPinDefault`
- `AssetFactory.AssetDocument.UBlueprint.GraphDiff.ReportsMissingLink`
- `AssetFactory.AssetDocument.UBlueprint.GraphDiff.TreatsDefinitionRefAndInlineMemberRefAsEqual`

- [ ] **Step 3: Implement UBlueprint graph diff integration**

Rules:

- Desired graph parses through `GraphCore`;
- Current graph comes from Tier 1 extract;
- `DefinitionRef` resolves before comparison;
- unsupported existing nodes produce `unsupported` entries rather than lossy diffs;
- missing sidecar `UbergraphPages` means empty authoritative graph user content.

- [ ] **Step 4: Run graph diff tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint.GraphDiff;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintGraphDiff"
```

Expected: graph diff tests pass.

- [ ] **Step 5: Commit Task 5**

Run:

```powershell
git add Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.cpp Source/AssetDocument/Private/Graphs Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp
git commit -m "feat(assetdoc): diff ublueprint event graphs"
```

---

### Task 6: UBlueprint `UbergraphPages` Apply And Compile Safety

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Graphs/K2GraphAdapter.*`
- Modify: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/*.h/.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [ ] **Step 2: Add failing apply tests**

Add tests:

- `AssetFactory.AssetDocument.UBlueprint.GraphApply.CreatesBeginPlayPrintString`
- `AssetFactory.AssetDocument.UBlueprint.GraphApply.UpdatesPinDefault`
- `AssetFactory.AssetDocument.UBlueprint.GraphApply.DeletesOmittedNodeAndLink`
- `AssetFactory.AssetDocument.UBlueprint.GraphApply.RejectsInvalidLinkBeforeMutation`
- `AssetFactory.AssetDocument.UBlueprint.GraphApply.CompileFailureDoesNotSavePartialGraph`

Use sidecar shape:

```json
{
  "Body": {
    "UbergraphPages": [
      {
        "Name": "EventGraph",
        "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
        "Nodes": [
          {
            "Id": "BeginPlay",
            "Class": "/Script/BlueprintGraph.K2Node_Event",
            "Member": {
              "Kind": "MemberRef",
              "OwnerClass": "/Script/Engine.Actor",
              "Name": "ReceiveBeginPlay"
            },
            "Position": { "X": 0, "Y": 0 }
          },
          {
            "Id": "Print",
            "Class": "/Script/BlueprintGraph.K2Node_CallFunction",
            "Member": {
              "Kind": "MemberRef",
              "OwnerClass": "/Script/Engine.KismetSystemLibrary",
              "Name": "PrintString"
            },
            "PinOverrides": [
              { "Pin": "InString", "DefaultValue": "Hello from AssetDocument" }
            ],
            "Position": { "X": 320, "Y": 0 }
          }
        ],
        "Links": [
          {
            "From": { "Node": "BeginPlay", "Pin": "then" },
            "To": { "Node": "Print", "Pin": "execute" }
          }
        ]
      }
    ]
  }
}
```

- [ ] **Step 3: Implement staged apply**

Apply order inside graph region:

1. parse and resolve all graph specs;
2. resolve graph schema and node classes dynamically;
3. ask registry for adapters;
4. preflight member refs and link endpoints before mutation;
5. create/find `UEdGraph`;
6. create/find nodes by sidecar `Id`;
7. bind members through adapters;
8. call UE pin allocation/reconstruction;
9. apply sparse pin defaults;
10. create schema-validated links;
11. delete omitted nodes/links;
12. compile Blueprint and return `BlueprintCompileFailed` on failure.

Do not save assets on failed compile. If full rollback is not available yet, tests must prove failed preflight prevents mutation for invalid sidecar and compile failures are reported before save.

- [ ] **Step 4: Run graph apply tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint.GraphApply;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintGraphApply"
```

Expected: graph apply tests pass.

- [ ] **Step 5: Run focused UBlueprint regression tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintGraphRegression"
```

Expected: existing UBlueprint variables/components/class-default tests still pass.

- [ ] **Step 6: Commit Task 6**

Run:

```powershell
git add Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.cpp Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Graphs Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp
git commit -m "feat(assetdoc): apply ublueprint event graphs"
```

---

### Task 7: HTTP Smoke, Docs, And Final Review

**Files:**
- Modify: `MCP/schemas/AssetDocument.md`
- Modify: `docs/superpowers/specs/asset-document-deferred-fields/2026-06-19-ublueprint.md`
- Create: `docs/superpowers/verification/asset_document_ublueprint_graph_http_smoke.py`
- Create: `docs/reports/asset-document-ublueprint-graph-regions-report.md`
- Modify: `MCP/src/index.ts` only if profile catalog/schema exposure requires it.

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [ ] **Step 2: Update schema documentation**

Document:

- canonical `GraphSpec` fields;
- `NodeSpec` fields;
- expanded `LinkSpec` as canonical output;
- compact link syntax as input sugar;
- `Definitions` graph-relevant kinds;
- Tier 1 supported node classes;
- unsupported fallback diagnostics;
- remaining unsupported `FunctionGraphs`、`MacroGraphs`、`Timelines` scope.

- [ ] **Step 3: Add external graph smoke script**

Create script that:

- writes `C:/AVH1/Content/AssetDocumentSmoke/BP_GraphSidecarSmoke.assetdoc.json`;
- applies a Blueprint with `BeginPlay -> PrintString`;
- calls `/assetfactory/assetdocument/apply-file`;
- calls `/assetfactory/assetdocument/extract`;
- calls `/assetfactory/assetdocument/diff`;
- asserts extract contains `Body.UbergraphPages[0].Nodes` with `BeginPlay` and `Print`;
- asserts diff has no unexpected changed entries after apply.

- [ ] **Step 4: Run final verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.GraphCore;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/GraphCoreFinal"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintGraphFinal"
Push-Location MCP; npm test; Pop-Location
python docs/superpowers/verification/asset_document_ublueprint_graph_http_smoke.py --base-url http://127.0.0.1:8559
```

Expected:

- UBT succeeds;
- `GraphCore` automation succeeds;
- `UBlueprint` automation succeeds;
- `MCP` tests succeed or only generate known `MCP/dist` churn that is restored before commit;
- HTTP smoke succeeds against live editor server.

- [ ] **Step 5: Write final report**

Report in Chinese:

- branch/worktree;
- implementation commit range;
- implemented graph regions and supported Tier 1 nodes;
- unsupported fallback behavior;
- deferred Function/Macro/Timeline scope;
- verification commands and results;
- HTTP smoke sidecar path and asset path;
- known risks.

- [ ] **Step 6: Dispatch read-only spec/code quality review**

Review only `SPEC_BASE..HEAD` or the task-level range requested by the main agent. Reviewer checks:

- design philosophy: thin core, high reuse, reflection-first;
- no Blueprint generator path;
- no raw UE graph dump fallback;
- unsupported diagnostics are actionable;
- tests cover delete/update/reset authoritative semantics;
- validation host evidence is current.

- [ ] **Step 7: Fix review findings and commit final docs**

Run focused verification again after fixes, then:

```powershell
git add MCP/schemas/AssetDocument.md docs/superpowers/specs/asset-document-deferred-fields/2026-06-19-ublueprint.md docs/superpowers/verification/asset_document_ublueprint_graph_http_smoke.py docs/reports/asset-document-ublueprint-graph-regions-report.md MCP/src/index.ts
git commit -m "docs(assetdoc): document ublueprint graph regions"
```

---

## Task Delegation Notes

派发 subagent 前，主线必须显示：

- worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-ublueprint-impl`
- branch: `feature/asset-document-ublueprint-impl`
- base commit: current `HEAD`
- diff range: `TASK_BASE..HEAD`
- allowed write scope: 当前 task 的 `Files` 列表
- context inheritance: 不继承完整长线程；只给 spec、plan、相关文件路径、acceptance criteria、当前 HEAD

实现类 subagent 需要知道：它不是独自在 codebase 中工作，不能 revert 他人改动；遇到 task 范围外的 dirty diff 应停止并报告。

## Final Acceptance Criteria

- `Body.UbergraphPages` 可以 roundtrip Tier 1 EventGraph：extract、diff、apply 都使用结构化 sidecar。
- BeginPlay -> PrintString smoke graph 可以通过 AssetDocument apply 创建，并能 diff 到 unchanged。
- omitted graph/node/link/pin default 按 authoritative semantics 删除或 reset。
- unsupported graph node/function/pin pattern 给出 actionable fallback diagnostics。
- `FunctionGraphs`、`MacroGraphs`、`Timelines` 仍有明确 unsupported/deferred 表述，不会静默吞掉 sidecar 内容。
- `GraphCore` 没有具体 K2 node includes、function whitelist、property whitelist 或 project-class inventory。
- 不新增 `BlueprintGenerator`、Blueprint-specific MCP tool 或 BSL apply 路径。
- `C:/AVH1` UBT、focused automation、MCP tests、HTTP smoke 的结果写入最终报告。
