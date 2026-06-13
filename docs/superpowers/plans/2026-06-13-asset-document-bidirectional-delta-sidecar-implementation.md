# AssetDocument 双向 Delta Sidecar Implementation Plan

> **给 agentic workers 的要求：** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 按 task 执行。本计划使用 checkbox (`- [ ]`) 追踪。

**目标：** 在现有 AssetDocument structured Body / AnimMontage 实现上，落地 policy-driven delta sidecar 的基础设施：`RegionPolicyPreset` / `RegionPolicy`、canonical hashing、`SyncStateStore`、region-level direction/conflict，以及 AnimMontage profile 的首个接入。

**架构：** 保留现有 `FAssetDocumentService`、profile/capability/fragments 和 AnimMontage production capability；新增 policy/sync 基础层作为内部契约，不引入 agent-facing patch DSL。运行时由 profile/semantic capability 展开 `RegionPolicyPreset + overrides` 为完整 `RegionPolicy`，`DefaultReducer` 和 `AuthoritativeApplyAdapter` 保持通用 engine，`SidecarSyncEngine` 使用 sidecar `_meta.sync` 做 region-level direction/conflict。

**技术栈：** UE 5.7 C++ editor module，`Source/AssetDocument`，UE reflection (`FProperty`)，JSON (`FJsonObject`)，现有 `FAssetDocumentService`，Automation tests，UBT Development build，必要时补 MCP TypeScript schema/tool 测试。

---

## 当前上下文

已有生产代码已经支持：

- `FAssetDocumentService` 的 `CreateTemplate` / `Apply` / `Extract` / `Diff` / `InspectProfile` / `GetSchema`
- `IAssetDocumentProfile`、`IAssetDocumentCapability`、fragment compiler 和 sidecar path 解析
- AnimMontage structured `Body` 的 apply / extract / diff / validation
- AnimMontage projector slice 的实验性 capability，但本计划不依赖它作为主路径

本计划要改变的是内部架构边界，不改变 agent 作者的核心心智模型：agent 仍然编辑 AssetDoc sidecar，sidecar 仍然描述持久化差异，不要求 agent 写 patch/op DSL。

---

## 目标行为

### Agent-facing 行为

- agent 读取或编辑 `.assetdoc.json`。
- AssetDoc `Body` 中缺失字段表示“不持久化这个差异”，不是删除命令。
- 删除或清空必须通过 AssetDoc-native 的显式值表达，例如空数组、空对象、`null` 或 schema 声明允许的 sentinel。
- 冲突只暴露两种方向选择：
  - `accept sidecar`：sidecar 赢，apply 到 `.uasset`
  - `accept asset`：`.uasset` 赢，重新生成 sidecar managed region

### Internal 行为

- `EvidenceExtractor` 从 asset/raw/reflected data 拿事实，允许偏底层。
- `DefaultReducer` 用 `RegionPolicy` 判断哪些事实和默认状态不同，只输出有效 delta。
- `SemanticCapability` / profile 声明 `RegionPolicy`，包括 region schema、稳定 key、比较规则、apply strategy、约束。
- `SidecarDeltaCapability` 负责 AssetDoc sidecar 中的 sparse delta 读写和 canonical region hash。
- `SidecarSyncEngine` 使用 last-sync state 判断方向和冲突，不只看当前 sidecar 和当前 asset。
- `AuthoritativeApplyAdapter` 根据 `RegionPolicy` 重建或更新 managed region，具体字段映射和 staged mutation 来自 policy / extension hook。

---

## 文件地图

### 新增文件

- `Source/AssetDocument/Public/AssetDocumentPolicy.h`
  - `EAssetDocumentRegionKind`
  - `EAssetDocumentDefaultSource`
  - `EAssetDocumentReducerMode`
  - `EAssetDocumentApplyMode`
  - `EAssetDocumentOrderRule`
  - `FAssetDocumentComparisonRule`
  - `FAssetDocumentIdentityRule`
  - `FAssetDocumentRegionPolicyPreset`
  - `FAssetDocumentRegionPolicy`
- `Source/AssetDocument/Private/AssetDocumentPolicyRegistry.h`
- `Source/AssetDocument/Private/AssetDocumentPolicyRegistry.cpp`
  - 内置 `RegionPolicyPreset`
  - preset expansion helper
- `Source/AssetDocument/Public/AssetDocumentSyncState.h`
  - `FAssetDocumentRegionSyncState`
  - `FAssetDocumentSyncState`
  - `EAssetDocumentSyncDirection`
  - `FAssetDocumentRegionSyncDecision`
- `Source/AssetDocument/Private/AssetDocumentCanonicalJson.h`
- `Source/AssetDocument/Private/AssetDocumentCanonicalJson.cpp`
  - canonical JSON writer
  - SHA256 hash helper
  - `_Skipped` / extract-only metadata 忽略 helper
- `Source/AssetDocument/Private/AssetDocumentSyncStateStore.h`
- `Source/AssetDocument/Private/AssetDocumentSyncStateStore.cpp`
  - `_meta.sync` load / write / update
  - region-level last-sync state
- `Source/AssetDocument/Private/AssetDocumentSidecarDelta.h`
- `Source/AssetDocument/Private/AssetDocumentSidecarDelta.cpp`
  - region value lookup
  - region delta hash
  - region managed-state helpers
- `Source/AssetDocument/Private/AssetDocumentSidecarSyncEngine.h`
- `Source/AssetDocument/Private/AssetDocumentSidecarSyncEngine.cpp`
  - direction/conflict matrix
  - `accept sidecar` / `accept asset` decision application result
- `Source/AssetDocument/Private/Tests/AssetDocumentPolicyTests.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentCanonicalJsonTests.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentSyncStateTests.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentSidecarSyncEngineTests.cpp`

### 修改文件

- `Source/AssetDocument/Public/AssetDocumentProfile.h`
  - 给 `IAssetDocumentProfile` 增加非纯虚的 `GetRegionPolicies()` / `GetRegionPolicy()` 默认实现
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.h`
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp`
  - 声明 AnimMontage 的 region policies
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.h`
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
  - 保持 production apply/extract/diff 行为，必要时暴露 helper 给 reducer/policy 测试使用
- `Source/AssetDocument/Private/AssetDocumentService.cpp`
  - `InspectProfile` / `GetSchema` 暴露 policy/preset 摘要
  - `Extract` 初始化 `_meta.sync`
  - `ApplyFile` 成功 apply 后更新 sidecar `_meta.sync`
- `Source/AssetDocument/Private/AssetDocumentSidecar.h`
- `Source/AssetDocument/Private/AssetDocumentSidecar.cpp`
  - 复用现有 sidecar path 和 write guard，不把 sync state 写入 `.uasset`
- `Source/AssetDocument/Private/AssetDocumentEditorSync.h`
- `Source/AssetDocument/Private/AssetDocumentEditorSync.cpp`
  - 只接入方向判断所需的 suppression/guard 点，不改变已有 rename/move/delete 行为
- `Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp`
  - profile schema/inspect 增加 `RegionPolicies` 断言
- `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
  - AnimMontage policy、extract `_meta.sync`、apply 后 sync state 更新断言
- `Source/AssetDocument/AssetDocument.Build.cs`
  - 如 SHA helper 需要新增模块依赖，在这里加最小依赖

---

## 数据结构草案

### `FAssetDocumentRegionPolicy`

```cpp
enum class EAssetDocumentRegionKind : uint8
{
    Scalar,
    Object,
    Array,
    Timeline,
    Graph
};

enum class EAssetDocumentDefaultSource : uint8
{
    CDO,
    EmptyTemplate,
    CurrentAssetBaseline,
    ProfileDeclared
};

enum class EAssetDocumentReducerMode : uint8
{
    DefaultDiff,
    ManagedRegion
};

enum class EAssetDocumentApplyMode : uint8
{
    SetProperty,
    RebuildArrayRegion,
    ExtensionHook
};

struct FAssetDocumentRegionPolicy
{
    FName RegionId;
    FString BodyPath;
    EAssetDocumentRegionKind RegionKind = EAssetDocumentRegionKind::Object;
    EAssetDocumentDefaultSource DefaultSource = EAssetDocumentDefaultSource::ProfileDeclared;
    EAssetDocumentReducerMode ReducerMode = EAssetDocumentReducerMode::DefaultDiff;
    EAssetDocumentApplyMode ApplyMode = EAssetDocumentApplyMode::SetProperty;
    TArray<FAssetDocumentIdentityRule> IdentityRules;
    TArray<FAssetDocumentComparisonRule> ComparisonRules;
    TArray<FString> ManagedUePropertyPaths;
    TSet<FString> ExtractOnlyFields;
    TSet<FString> ExplicitDeleteValues;
    TOptional<FName> ExtensionHookName;
};
```

### `_meta.sync`

```json
{
  "_meta": {
    "assetDocumentVersion": 1,
    "sync": {
      "schemaVersion": 1,
      "assetObjectPath": "/Game/AssetDocumentTest/M_Test",
      "assetPackageGuid": "package-guid-or-empty",
      "updatedAtUtc": "2026-06-13T15:30:00Z",
      "regions": {
        "Body.Blend": {
          "policyVersion": 1,
          "sidecarHash": "sha256:...",
          "assetEvidenceHash": "sha256:...",
          "lastSyncedAtUtc": "2026-06-13T15:30:00Z"
        }
      }
    }
  }
}
```

约束：

- v1 默认只写入 sidecar `_meta.sync`。
- 不写 `.uasset` metadata，不增加 UE asset 内的 marker。
- runtime 内存只做短生命周期 cache，不能作为唯一事实来源。
- companion state file 不在本计划范围内实现。

---

## Task 1：Policy substrate 与 preset expansion

### 目标

建立 policy/preset 的基础类型和内置 preset registry。这个 task 不接入 service，只保证静态 policy 能被构造、展开、序列化成 profile/schema payload。

### 文件

- 新增 `Source/AssetDocument/Public/AssetDocumentPolicy.h`
- 新增 `Source/AssetDocument/Private/AssetDocumentPolicyRegistry.h`
- 新增 `Source/AssetDocument/Private/AssetDocumentPolicyRegistry.cpp`
- 新增 `Source/AssetDocument/Private/Tests/AssetDocumentPolicyTests.cpp`

### 实现步骤

- [ ] 定义 policy enums 和 structs。
- [ ] 增加 `FAssetDocumentPolicyRegistry::GetBuiltinPreset(FName PresetName, FAssetDocumentRegionPolicyPreset& OutPreset)`。
- [ ] 增加 `FAssetDocumentPolicyRegistry::ExpandPreset(...)`，支持：
  - preset defaults
  - per-region override
  - `RegionId`
  - `BodyPath`
  - `ManagedUePropertyPaths`
  - `ExtensionHookName`
- [ ] 增加 JSON export helper，供 `InspectProfile` / `GetSchema` 使用。

### 测试

新增 automation：

- `AssetDocument.Policy.PresetExpansion`
- `AssetDocument.Policy.JsonExport`

断言：

- 同一 preset + override 展开结果稳定。
- `RegionId`、`BodyPath`、`ApplyMode`、`ReducerMode` 被正确覆盖。
- JSON payload 不包含空的内部临时字段。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

如果在 worktree host 中验证，替换为临时 host `.uproject` 路径。

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Public/AssetDocumentPolicy.h Source/AssetDocument/Private/AssetDocumentPolicyRegistry.h Source/AssetDocument/Private/AssetDocumentPolicyRegistry.cpp Source/AssetDocument/Private/Tests/AssetDocumentPolicyTests.cpp
git commit -m "feat(assetdoc): add region policy substrate"
```

---

## Task 2：Profile API 与 AnimMontage RegionPolicy 接入

### 目标

让 profile 能声明 region policies，并让 AnimMontage 使用 generic policy 思路描述已有 structured `Body` 的关键 region。

### 文件

- 修改 `Source/AssetDocument/Public/AssetDocumentProfile.h`
- 修改 `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.h`
- 修改 `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp`
- 修改 `Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp`
- 修改 `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

### 实现步骤

- [ ] 在 `IAssetDocumentProfile` 增加默认实现：

```cpp
virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const
{
    return {};
}

virtual bool GetRegionPolicy(FName RegionId, FAssetDocumentRegionPolicy& OutPolicy) const
{
    for (const FAssetDocumentRegionPolicy& Policy : GetRegionPolicies())
    {
        if (Policy.RegionId == RegionId)
        {
            OutPolicy = Policy;
            return true;
        }
    }
    return false;
}
```

- [ ] 在 AnimMontage profile 声明 region：
  - `Body.Blend`
  - `Body.SlotAnimTracks`
  - `Body.CompositeSections`
  - `Body.Notifies`
  - `Body.NotifyStates`
- [ ] 对数组/timeline region 使用 policy preset + overrides，避免把每类 asset 写成专门 reducer/adapter。
- [ ] 保持现有 `GetBodyKeys()` 和 `ResolveBodyAdapter()` 行为不变。

### 测试

扩展 automation：

- `AssetDocument.Profile.ExactProfile`
- `AssetDocument.AnimMontage.InspectProfileIncludesStructuredBody`

新增断言：

- `GetRegionPolicies()` 至少包含上述五个 region。
- `Body.Notifies` / `Body.NotifyStates` 的 `ApplyMode` 是 managed array/timeline 语义。
- `Body.Blend` 的 policy 是 scalar/object default-diff 语义。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Public/AssetDocumentProfile.h Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.h Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "feat(assetdoc): expose anim montage region policies"
```

---

## Task 3：Profile/schema payload 暴露 policy 摘要

### 目标

让 agent/tooling 能看到结构能力的语义声明，但仍然不要求 agent 写 patch/op。

### 文件

- 修改 `Source/AssetDocument/Private/AssetDocumentService.cpp`
- 修改 `Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp`
- 修改 `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
- 视 MCP 当前 schema 输出情况修改 `MCP/src/index.ts` 或相关测试

### 实现步骤

- [ ] 在 `InspectProfile` payload 增加 `RegionPolicies`。
- [ ] 在 `GetSchema` payload 增加 `RegionPolicyPresets` 和 registered profile 的 policy summary。
- [ ] policy payload 使用稳定字段名：
  - `RegionId`
  - `BodyPath`
  - `RegionKind`
  - `DefaultSource`
  - `ReducerMode`
  - `ApplyMode`
  - `ManagedUePropertyPaths`
  - `ExtensionHookName`
- [ ] 不暴露 C++ 内部 helper class 名称。

### 测试

扩展 automation：

- `AssetDocument.Service.SchemaIncludesProfiles`
- `AssetDocument.Profile.GenericInspectProfile`
- `AssetDocument.AnimMontage.InspectProfileIncludesStructuredBody`

断言：

- `InspectProfile` 中 AnimMontage 包含 `RegionPolicies`。
- `GetSchema` 中包含内置 presets 摘要。
- generic profile 没有 exact policy 时仍然返回合法空列表。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

如果修改 MCP：

```powershell
Set-Location E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec/MCP
npm test
```

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Private/AssetDocumentService.cpp Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp MCP/src/index.ts
git commit -m "feat(assetdoc): publish region policy metadata"
```

如果 `MCP/src/index.ts` 未修改，不把它加入 commit。

---

## Task 4：Canonical JSON hashing 与 extract-only metadata 过滤

### 目标

为 sidecar/asset region 计算稳定 hash。这个 hash 是 last-sync state 的基础，必须不受 JSON object key 顺序影响，并忽略 extract-only `_Skipped` 这类诊断字段。

### 文件

- 新增 `Source/AssetDocument/Private/AssetDocumentCanonicalJson.h`
- 新增 `Source/AssetDocument/Private/AssetDocumentCanonicalJson.cpp`
- 新增 `Source/AssetDocument/Private/Tests/AssetDocumentCanonicalJsonTests.cpp`
- 如需 SHA 模块，修改 `Source/AssetDocument/AssetDocument.Build.cs`

### 实现步骤

- [ ] 实现 canonical JSON writer：
  - object key 按字典序排序
  - array 保留顺序
  - string 使用 JSON escaping
  - number 使用稳定字符串格式
  - bool/null 使用 JSON 标准字面值
- [ ] 实现 `HashJsonValue(const TSharedPtr<FJsonValue>& Value, const FAssetDocumentRegionPolicy* Policy)`。
- [ ] 实现 `CloneWithoutExtractOnlyFields(...)`。
- [ ] 默认忽略字段：
  - `_Skipped`
  - `_meta`
  - policy 中声明的 `ExtractOnlyFields`

### 测试

新增 automation：

- `AssetDocument.CanonicalJson.ObjectOrderStable`
- `AssetDocument.CanonicalJson.ArrayOrderMatters`
- `AssetDocument.CanonicalJson.ExtractOnlyFieldsIgnored`

断言：

- `{ "A": 1, "B": 2 }` 与 `{ "B": 2, "A": 1 }` hash 一致。
- `[1, 2]` 与 `[2, 1]` hash 不一致。
- 增删 `_Skipped` 不改变 managed region hash。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Private/AssetDocumentCanonicalJson.h Source/AssetDocument/Private/AssetDocumentCanonicalJson.cpp Source/AssetDocument/Private/Tests/AssetDocumentCanonicalJsonTests.cpp Source/AssetDocument/AssetDocument.Build.cs
git commit -m "feat(assetdoc): add canonical region hashing"
```

如果 `AssetDocument.Build.cs` 未修改，不把它加入 commit。

---

## Task 5：SidecarDeltaCapability 基础 helper

### 目标

提供 AssetDoc-native sparse delta 的 region 读写 helper。它不是 agent-facing patch DSL，只负责从 sidecar JSON 中定位 region、计算 region hash、判断显式清空值。

### 文件

- 新增 `Source/AssetDocument/Private/AssetDocumentSidecarDelta.h`
- 新增 `Source/AssetDocument/Private/AssetDocumentSidecarDelta.cpp`
- 新增或扩展 `Source/AssetDocument/Private/Tests/AssetDocumentSyncStateTests.cpp`

### 实现步骤

- [ ] 实现 `FindRegionValue(DocumentJson, Policy)`：
  - 支持 `BodyPath` 如 `Body.Blend`
  - 支持 object/array/scalar region
- [ ] 实现 `SetRegionValue(DocumentJson, Policy, Value)`。
- [ ] 实现 `HashSidecarRegion(DocumentJson, Policy)`。
- [ ] 实现 `IsExplicitEmptyRegion(Value, Policy)`：
  - empty array 表示 managed array 清空
  - empty object/null 的语义由 policy 控制
- [ ] 缺失 region 返回 `Unset` 状态，不视为删除。

### 测试

新增或扩展 automation：

- `AssetDocument.SidecarDelta.MissingRegionMeansUnset`
- `AssetDocument.SidecarDelta.EmptyArrayIsExplicit`
- `AssetDocument.SidecarDelta.HashIgnoresSkipped`

断言：

- `Body.Notifies` 缺失时，helper 返回 unset。
- `Body.Notifies: []` 返回显式空数组。
- 同一个 region 带 `_Skipped` 和不带 `_Skipped` hash 一致。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Private/AssetDocumentSidecarDelta.h Source/AssetDocument/Private/AssetDocumentSidecarDelta.cpp Source/AssetDocument/Private/Tests/AssetDocumentSyncStateTests.cpp
git commit -m "feat(assetdoc): add sparse sidecar delta helpers"
```

---

## Task 6：SyncStateStore 持久化 `_meta.sync`

### 目标

把 last-sync state 持久化到 sidecar `_meta.sync`，使 sync direction 能判断“谁变了”。这个 task 不写 `.uasset` metadata，也不创建 companion state file。

### 文件

- 新增 `Source/AssetDocument/Public/AssetDocumentSyncState.h`
- 新增 `Source/AssetDocument/Private/AssetDocumentSyncStateStore.h`
- 新增 `Source/AssetDocument/Private/AssetDocumentSyncStateStore.cpp`
- 新增 `Source/AssetDocument/Private/Tests/AssetDocumentSyncStateTests.cpp`
- 修改 `Source/AssetDocument/Private/AssetDocumentSidecar.cpp`

### 实现步骤

- [ ] 定义 sync state 结构：
  - `SchemaVersion`
  - `AssetObjectPath`
  - `AssetPackageGuid`
  - `UpdatedAtUtc`
  - `Regions`
- [ ] 实现 `LoadFromDocumentJson(...)`。
- [ ] 实现 `WriteToDocumentJson(...)`。
- [ ] 实现 `UpdateRegionState(...)`。
- [ ] 确保 `_meta.sync` 不参与 region hash。
- [ ] 确保 `FAssetDocumentSidecar::ValidateTargetMatchesSidecar` 继续只用 `Target` 判断 sidecar 所属资产。

### 测试

新增 automation：

- `AssetDocument.SyncState.LoadEmpty`
- `AssetDocument.SyncState.RoundTripMeta`
- `AssetDocument.SyncState.MetaIgnoredByHash`

断言：

- 无 `_meta.sync` 的旧 sidecar 可以被读取。
- 写入后 JSON 结构稳定。
- 改变 `_meta.sync.updatedAtUtc` 不改变任何 region hash。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Public/AssetDocumentSyncState.h Source/AssetDocument/Private/AssetDocumentSyncStateStore.h Source/AssetDocument/Private/AssetDocumentSyncStateStore.cpp Source/AssetDocument/Private/AssetDocumentSidecar.cpp Source/AssetDocument/Private/Tests/AssetDocumentSyncStateTests.cpp
git commit -m "feat(assetdoc): persist sidecar sync state"
```

---

## Task 7：SidecarSyncEngine direction/conflict matrix

### 目标

实现纯逻辑 sync engine，用当前 sidecar region hash、当前 asset evidence hash、last-sync region state 判断方向和冲突。

### 文件

- 新增 `Source/AssetDocument/Private/AssetDocumentSidecarSyncEngine.h`
- 新增 `Source/AssetDocument/Private/AssetDocumentSidecarSyncEngine.cpp`
- 新增 `Source/AssetDocument/Private/Tests/AssetDocumentSidecarSyncEngineTests.cpp`

### 决策矩阵

| sidecar changed | asset changed | decision |
| --- | --- | --- |
| false | false | `NoChange` |
| true | false | `ApplySidecarToAsset` |
| false | true | `RegenerateSidecarRegion` |
| true | true | `Conflict` |

如果没有 last-sync state：

- 已有 sidecar region 且 asset evidence 可计算：返回 `NeedsInitialBaseline`，由调用者选择初始化或提示冲突策略。
- 空 sidecar + asset evidence：返回 `RegenerateSidecarRegion`。

### 实现步骤

- [ ] 定义 `EAssetDocumentSyncDirection`：
  - `NoChange`
  - `ApplySidecarToAsset`
  - `RegenerateSidecarRegion`
  - `Conflict`
  - `NeedsInitialBaseline`
- [ ] 实现 `DecideRegion(...)`。
- [ ] 实现 `DecideDocument(...)` 聚合多个 region。
- [ ] 实现 `ApplyResolution(Decision, AcceptSidecar|AcceptAsset)` 的结果结构，只返回动作，不直接写文件或 asset。

### 测试

新增 automation：

- `AssetDocument.SidecarSync.NoChange`
- `AssetDocument.SidecarSync.SidecarOnlyChange`
- `AssetDocument.SidecarSync.AssetOnlyChange`
- `AssetDocument.SidecarSync.Conflict`
- `AssetDocument.SidecarSync.InitialBaseline`
- `AssetDocument.SidecarSync.AcceptSidecar`
- `AssetDocument.SidecarSync.AcceptAsset`

断言：

- 所有矩阵路径稳定。
- conflict 不自动 merge。
- `accept sidecar` 和 `accept asset` 只产生明确动作。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Private/AssetDocumentSidecarSyncEngine.h Source/AssetDocument/Private/AssetDocumentSidecarSyncEngine.cpp Source/AssetDocument/Private/Tests/AssetDocumentSidecarSyncEngineTests.cpp
git commit -m "feat(assetdoc): add sidecar sync decision engine"
```

---

## Task 8：Extract 集成 `_meta.sync`

### 目标

当 `Extract` 生成 AssetDoc 时，为 managed region 初始化 sync metadata。这样从 `.uasset` 生成 sidecar 后，后续双向 sync 有 last-sync baseline。

### 文件

- 修改 `Source/AssetDocument/Private/AssetDocumentService.cpp`
- 修改 `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
- 修改 `Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp`

### 实现步骤

- [ ] `Extract` 在 profile 有 region policies 时：
  - 先按现有逻辑生成 `Body`
  - 对每个 region 计算 extracted region hash
  - 写 `_meta.sync.regions[RegionId].sidecarHash`
  - 写 `_meta.sync.regions[RegionId].assetEvidenceHash`
- [ ] `sidecarHash` 与 `assetEvidenceHash` 初始相同。
- [ ] `_Skipped` 不影响 hash。
- [ ] 没有 exact profile 的 generic asset 不强制写 region state。
- [ ] `Validate` 接受 `_meta` 顶层字段。

### 测试

扩展 automation：

- `AssetDocument.AnimMontage.ExtractsCurrentBody`
- `AssetDocument.AnimMontage.ExtractedBodyDoesNotAuthorSkippedMetadata`

新增断言：

- extract 结果包含 `_meta.sync.schemaVersion`。
- `Body.Blend` / `Body.SlotAnimTracks` / `Body.CompositeSections` 至少有 sync region state。
- 手动增删 `_Skipped` 后 region hash 不变。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Private/AssetDocumentService.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp
git commit -m "feat(assetdoc): initialize sync state on extract"
```

---

## Task 9：ApplyFile 成功后更新 sidecar sync state

### 目标

当 sidecar apply 到 asset 成功后，更新 sidecar `_meta.sync`，让下一轮 direction 判断知道 sidecar 和 asset 已经同步。

### 文件

- 修改 `Source/AssetDocument/Private/AssetDocumentService.cpp`
- 修改 `Source/AssetDocument/Private/AssetDocumentEditorSync.cpp`
- 修改 `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
- 可能扩展 `Source/AssetDocument/Private/Tests/AssetDocumentEditorSyncTests.cpp`

### 实现步骤

- [ ] `ApplyFile` 成功后读取 source sidecar JSON。
- [ ] 根据 source doc path / target asset / profile policies 重新计算已应用 region 的 `sidecarHash`。
- [ ] 从应用后的 asset evidence 计算 `assetEvidenceHash`。
- [ ] 更新 `_meta.sync` 并写回 source sidecar。
- [ ] 写回 sidecar 时使用现有 `FScopedSidecarWrite` / suppression guard，避免触发自循环。
- [ ] dry run 不更新 sidecar。
- [ ] 失败 apply 不更新 sidecar。

### 测试

扩展 automation：

- `AssetDocument.AnimMontage.AppliesStructuredBody`
- `AssetDocument.EditorSync.SuppressesOwnWrites`

新增断言：

- `ApplyFile` 成功后 sidecar `_meta.sync.regions` 更新。
- dry run 不写 sidecar。
- apply 失败不写 sidecar。
- 自己写 `_meta.sync` 不触发重复 apply。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Private/AssetDocumentService.cpp Source/AssetDocument/Private/AssetDocumentEditorSync.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentEditorSyncTests.cpp
git commit -m "feat(assetdoc): update sync state after sidecar apply"
```

---

## Task 10：Region-level asset-to-sidecar regeneration path

### 目标

当 `.uasset` 改变且 sidecar region 未改变时，可以按 policy 重新生成 managed region 的 sidecar delta，并更新 `_meta.sync`。这对应 conflict resolution 中的 `accept asset`。

### 文件

- 修改 `Source/AssetDocument/Private/AssetDocumentService.cpp`
- 修改 `Source/AssetDocument/Private/AssetDocumentSidecarSyncEngine.cpp`
- 修改 `Source/AssetDocument/Private/AssetDocumentSidecarDelta.cpp`
- 修改 `Source/AssetDocument/Private/Tests/AssetDocumentSidecarSyncEngineTests.cpp`
- 修改 `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

### 实现步骤

- [ ] 增加 internal helper `RegenerateSidecarRegionsFromAsset(...)`。
- [ ] 复用现有 capability `Extract` 得到 full body evidence。
- [ ] 使用 `RegionPolicy` 只替换 requested managed region，不重写无关 sidecar 字段。
- [ ] 对 `accept asset` 更新：
  - sidecar region value
  - `sidecarHash`
  - `assetEvidenceHash`
  - `lastSyncedAtUtc`
- [ ] 不改 unmanaged sidecar fields。

### 测试

新增或扩展 automation：

- `AssetDocument.SidecarSync.AcceptAssetRegeneratesManagedRegion`
- `AssetDocument.SidecarSync.AcceptAssetPreservesUnmanagedFields`

断言：

- 只替换目标 region。
- 手写的其他 sidecar 字段保持原值。
- 更新后的 direction 变为 `NoChange`。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Private/AssetDocumentService.cpp Source/AssetDocument/Private/AssetDocumentSidecarSyncEngine.cpp Source/AssetDocument/Private/AssetDocumentSidecarDelta.cpp Source/AssetDocument/Private/Tests/AssetDocumentSidecarSyncEngineTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "feat(assetdoc): regenerate sidecar regions from asset"
```

---

## Task 11：AnimMontage managed region apply alignment

### 目标

把现有 AnimMontage Body apply 行为和 region policy 对齐，明确 managed array region 的重建边界。这个 task 不重写已有 capability，只补齐 policy-driven 入口和测试约束。

### 文件

- 修改 `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- 修改 `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.cpp`
- 修改 `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

### 实现步骤

- [ ] 对 `Body.Notifies` / `Body.NotifyStates` 明确 managed region 行为：
  - sidecar 中缺失：不改现有 asset region
  - sidecar 中空数组：清空 AssetDoc-managed region
  - sidecar 中数组有值：按 policy 重建 managed region
- [ ] 对非 managed UE 内容保持现有保留策略。
- [ ] 对 `CompositeSections` / `SlotAnimTracks` 保持现有 apply 语义，同时记录 policy id 方便 sync hash。
- [ ] 错误信息使用 region id，方便 agent/tooling 定位。

### 测试

扩展 automation：

- `AssetDocument.AnimMontage.ApplyReplacesExistingManagedNotifies`
- `AssetDocument.AnimMontage.AppliesStructuredBody`
- `AssetDocument.AnimMontage.RejectsInvalidBodySemantics`

新增断言：

- 缺失 `Body.Notifies` 时不清空已有 notify。
- `Body.Notifies: []` 时清空 managed notify。
- 非 managed notify 不因 managed region apply 被误删。

### 验证命令

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### Checkpoint

```powershell
git status --short
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "feat(assetdoc): align anim montage apply with region policy"
```

---

## Task 12：端到端验证与文档收口

### 目标

完成 UBT、automation、MCP/schema 验证，并把可验证方式写入计划执行记录。若需要真实 AnimMontage smoke asset，应在临时 host 或明确测试路径中创建，不把不必要资产提交进源码仓库。

### 文件

- 修改本计划文件的执行记录区
- 可能修改 `docs/superpowers/specs/2026-06-13-asset-document-bidirectional-delta-sidecar-goal.md` 的“实现状态”小节

### 验证步骤

- [ ] 确认工作区没有无关 dirty diff：

```powershell
git status --short
```

- [ ] UBT 编译：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

- [ ] 如存在 worktree 插件 shadowing 风险，创建临时 host project 并用 junction 指向当前 worktree plugin，再对 host 运行 UBT。
- [ ] 运行相关 Automation tests：
  - `AssetDocument.Policy.*`
  - `AssetDocument.CanonicalJson.*`
  - `AssetDocument.SyncState.*`
  - `AssetDocument.SidecarDelta.*`
  - `AssetDocument.SidecarSync.*`
  - `AssetDocument.AnimMontage.*`
- [ ] 如 MCP schema/tool 输出有变化，运行：

```powershell
Set-Location E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec/MCP
npm test
```

- [ ] 用真实 AnimMontage smoke asset 验证：
  - extract 得到 `_meta.sync`
  - 修改 sidecar blend/section/notify 后 apply 生效
  - 再 extract 或 inspect direction 后变为同步状态
  - asset 端修改后 `accept asset` 能更新 sidecar managed region
  - sidecar 与 asset 同时修改时报告 conflict，不自动 merge

### 执行记录（2026-06-14）

- `git status --short`：Task 12 开始前工作区无无关 dirty diff。
- UBT 使用 validation host，避免真实项目同名插件 shadowing：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

结果：`Target is up to date`，`Result: Succeeded`。

- MCP/schema 测试：

```powershell
Set-Location E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec/MCP
npm test
```

结果：`node --test dist/broker/*.test.js` 通过，`39` tests passed，`0` failed。

- Automation 使用 validation host：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -NoSound -NoSplash -ExecCmds="Automation RunTests AssetFactory.AssetDocument; Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/Automation/AssetDocumentTask12All"
```

结果：report `C:/AVH1/Saved/Automation/AssetDocumentTask12All/index.json`，`50` succeeded，`6` succeeded with warnings，`0` failed，`0` not run。

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -NoSound -NoSplash -ExecCmds="Automation RunTests AssetDocument; Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/Automation/AssetDocumentTask12Bare"
```

结果：report `C:/AVH1/Saved/Automation/AssetDocumentTask12Bare/index.json`，`85` succeeded，`6` succeeded with warnings，`0` failed，`0` not run。覆盖 `AssetDocument.Policy.*`、`AssetDocument.CanonicalJson.*`、`AssetDocument.SyncState.*`、`AssetDocument.SidecarDelta.*`、`AssetDocument.SidecarSync.*`，并包含 `AssetFactory.AssetDocument.AnimMontage.*`、`AssetFactory.AssetDocument.SidecarSync.*` 等集成测试。

- 真实 AnimMontage smoke asset：
  - 创建/复制的真实资产路径：`/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke`。
  - 磁盘文件：`C:/AVH1/Content/AssetDocumentSmoke/AM_DeltaSidecarSmoke.uasset`。
  - 资产来自 validation host 中已有的 `AM_StructuredBodyDemo` 复制件，class 确认为 `AnimMontage`。
  - HTTP server：后台启动 `UnrealEditor.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -NoSound -NoSplash`，`GET http://127.0.0.1:8559/assetfactory/health` 返回 `status=ok`、`subsystemAvailable=true`。
  - `POST /assetfactory/assetdocument/extract` 对 `/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke` 成功，输出保存到 `C:/AVH1/Saved/AssetDocumentSmoke/AM_DeltaSidecarSmoke.assetdoc.json`。提取 payload 包含 `_meta.sync`，region 包含 `Body.Blend`、`Body.CompositeSections`、`Body.Notifies`、`Body.NotifyStates`、`Body.SlotAnimTracks`。
  - 将 sidecar 放到 uasset 同目录：`C:/AVH1/Content/AssetDocumentSmoke/AM_DeltaSidecarSmoke.assetdoc.json`。删除 extract-only 的 `Body._Skipped` 后，把 `Body.Blend.BlendInTime` 改为 `0.5`，`Body.Blend.BlendOutTime` 改为 `0.25`。
  - `POST /assetfactory/assetdocument/apply-file` 成功：`success=true`、`saved_asset=true`、`wrote_sidecar=true`。
  - 再次 `extract` 确认真实 uasset 当前值为 `BlendInTime=0.5`、`BlendOutTime=0.25`，输出保存到 `C:/AVH1/Saved/AssetDocumentSmoke/AM_DeltaSidecarSmoke.after-exact-apply.assetdoc.json`。
  - `POST /assetfactory/assetdocument/diff` 使用同目录 sidecar 返回 `success=true`，`payload.changed` 为空，结果保存到 `C:/AVH1/Saved/AssetDocumentSmoke/diff-after-exact-apply.json`。
  - 说明：第一次 smoke 使用 `0.35/0.45` 时 apply 到 uasset 成功，但由于 UE float 回读为 `0.349999994/0.449999988`，post-apply evidence hash 与 sidecar canonical hash 不一致，`ApplyFile` 按设计跳过 `_meta.sync` 更新并返回 `sidecar_sync_update_skip_reason="Post-apply asset evidence hash differs for region 'Body.Blend'"`。改用可精确表示的 `0.5/0.25` 后验证了 sync state 成功更新路径。
  - 当前 HTTP routes 未暴露独立的 `accept asset` / conflict resolution endpoint；`accept asset` managed region regenerate 和 conflict/no-merge 行为由 `AssetDocument.SidecarSync.*` 与 `AssetFactory.AssetDocument.SidecarSync.*` automation 覆盖。
  - 验证后已关闭后台 editor 进程，`/assetfactory/health` 不再可达。

### Checkpoint

```powershell
git status --short
git add docs/superpowers/plans/2026-06-13-asset-document-bidirectional-delta-sidecar-implementation.md docs/superpowers/specs/2026-06-13-asset-document-bidirectional-delta-sidecar-goal.md
git commit -m "docs(assetdoc): record delta sidecar verification"
```

如果 spec 文件未修改，不把它加入 commit。

---

## 实现顺序建议

推荐顺序：

1. Task 1-3 建立 policy 声明层和工具可见性。
2. Task 4-7 建立 hash、state、direction/conflict 的纯逻辑层。
3. Task 8-10 接入 extract/apply/regenerate 的 sidecar 生命周期。
4. Task 11 对齐 AnimMontage managed region apply 行为。
5. Task 12 做端到端验证和记录。

Task 1-7 可以由 subagent 分段实现，Task 8-11 涉及 `AssetDocumentService` 和 AnimMontage capability，建议串行推进并在每个 task 后 checkpoint commit。

---

## 风险与约束

- `_meta.sync` 不能参与 canonical region hash，否则每次写入时间戳都会制造自冲突。
- 缺失 region 不能解释为删除，否则 sparse delta 的核心语义会被破坏。
- `accept asset` 只能替换 managed region，不能重写整个 sidecar。
- `accept sidecar` 成功后必须更新 last-sync state，否则下一轮会继续误判为 sidecar changed。
- AnimMontage notify timeline 需要保留非 AssetDoc-managed 内容；managed region 的 identity/marker 规则必须和现有 `AnimMontageNotifyPlacementAdapter` 行为一致。
- 本计划不引入 C++ template 抽象；复用通过 `RegionPolicyPreset`、profile/schema 和 UE reflection 完成。
- 不把 sync state 写进 `.uasset`，避免破坏 UE 原始资产文件。

---

## 完成定义

- 所有新增 policy/sync/canonical/delta/sync-engine automation tests 通过。
- 现有 AssetDocument 和 AnimMontage automation tests 继续通过。
- `Extract` 能生成带 `_meta.sync` 的 AnimMontage AssetDoc。
- `ApplyFile` 成功后能更新 sidecar `_meta.sync`。
- `SidecarSyncEngine` 能区分 no-change、sidecar-only、asset-only、conflict。
- conflict resolution 只提供 `accept sidecar` 和 `accept asset` 两条路径。
- profile/schema payload 中能看到 AnimMontage region policies 和 built-in presets。
- UBT Development build 通过。
- 如果使用真实编辑器/MCP 验证，验证记录包含具体 smoke asset 路径和命令输出摘要。
