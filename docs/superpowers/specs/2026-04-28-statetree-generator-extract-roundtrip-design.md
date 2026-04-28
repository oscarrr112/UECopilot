# StateTree Generator Extract + Round-trip Design

**日期**：2026-04-28  
**状态**：Draft（待用户审阅）  
**子 Spec**：Spec 6  
**依赖**：Spec 1-5（core lifecycle、dynamic nodes、structure/transitions、parameters/property bags、property bindings）

---

## 1. 背景

StateTree generator 的前五个子 spec 已经分别实现了创建、动态节点、结构/transition、parameters/property bags、property bindings/function bindings。每个子 spec 也顺手补了局部 extraction，但目前缺少一个统一的“资产级 round-trip”保护层。

Spec 6 的目标不是再新增 StateTree 编辑器语义，而是把已有 generator/extractor 收束成长期可回归的闭环：

```text
Generate fixture -> Extract JSON #1 -> Generate from extracted JSON -> Extract JSON #2 -> Compare normalized semantics
```

这个闭环要覆盖所有现有 positive StateTree fixtures，并新增一个综合 fixture 来验证多能力组合场景。

---

## 2. 目标

- 覆盖所有现有 StateTree positive fixtures 的 round-trip：
	- `ST_Core_Minimal`
	- `ST_Core_AIComponentSchema`
	- `ST_Dynamic_Delay_Minimal`
	- `ST_Dynamic_DebugText_WithInstance`
	- `ST_Dynamic_Condition_CompareInt`
	- `ST_Structure_Transitions`
	- `ST_Structure_LinkedSubtree`
	- `ST_Structure_LinkedAsset_Target`
	- `ST_Structure_LinkedAsset_Referencer`
	- `ST_Parameters_Basic`
	- `ST_Parameters_Complex`
	- `ST_Parameters_LinkedTarget`
	- `ST_Parameters_LinkedReferencer`
	- `ST_Bindings_Ordinary`
	- `ST_Bindings_Function`
- 新增一个综合 fixture，组合 root parameters、state parameters、nested states、transition condition、ordinary binding、property function binding。
- 提供一个统一 verifier，用于比较 `Extract #1` 与 `Extract #2` 的稳定语义。
- 保留更严格的专项 checker，用来锁紧 state path、parameter identity、binding path/function graph 等容易回归的位置。
- 继续通过真实 GUI Editor + MCP 做最终 smoke，至少打开综合 fixture 给用户验收。
- 确认 invalid fixtures 的失败诊断没有被 Spec 6 的兼容修复破坏。

---

## 3. 非目标

- 不新增新的 StateTree 节点类型支持。
- 不扩展 property bag 的类型矩阵；`Map`、整数/byte/double/enum 等限制继续按 Spec 4 文档说明处理。
- 不实现 editor-authored-only binding metadata 的生成支持，例如 `instanceStruct` / `access`。
- 不要求 byte-level JSON 完全一致；UE/editor 生成的 hash、字段顺序、自然 GUID 表达差异不应导致 round-trip 失败。
- 不把 screenshot/UI smoke 套件化；截图与批量 editor smoke 留给 Spec 8。

---

## 4. Fixture 策略

### 4.1 现有 fixture 全覆盖

Verifier 要能按依赖顺序处理现有 fixtures：

1. 先生成独立 target asset：
	- `ST_Structure_LinkedAsset_Target`
	- `ST_Parameters_LinkedTarget`
2. 再生成 referencer：
	- `ST_Structure_LinkedAsset_Referencer`
	- `ST_Parameters_LinkedReferencer`
3. 其余 positive fixtures 可独立生成。

每个 fixture 都独立执行 `Generate -> Extract -> Generate -> Extract -> Compare`。失败报告必须包含 fixture 名、阶段、源/目标临时 JSON 路径，以及 first meaningful mismatch。

### 4.2 综合 fixture

新增 `TestData/ST_RoundTrip_Comprehensive.json`。

综合 fixture 应保持单资产为主，不依赖新的外部 target。它组合已支持能力：

- `SchemaClass`: `StateTreeComponentSchema`。
- Root parameters：
	- `BaseDelay` float
	- `BonusDelay` float
	- `DebugLabel` string/text 中已支持的一种
- State hierarchy：
	- `Root`
	- `Idle`
	- `Attack`
	- `Recover`
- Tasks：
	- `StateTreeDelayTask`，instance `Duration` 由 function binding 计算。
	- `StateTreeDebugTextTask` 或当前项目已验证可生成的 debug text task，用于覆盖 instance properties。
- Transition：
	- `Idle -> Attack`，带 transition condition。
	- `Attack -> Recover`，使用 `OnStateFailed` 或 `OnStateCompleted` 中已验证的一种。
- Bindings：
	- ordinary binding：root parameter 到 task instance property。
	- function binding：`BaseDelay + BonusDelay -> DelayTask.Duration`。
- State parameter：
	- 至少一个 state-local parameter，覆盖 state parameter extraction。

如果某个组合在 UE compiler 层天然冲突，优先降低综合 fixture 的复杂度，不在 Spec 6 中新增 generator 语义绕过 compiler。

---

## 5. Normalization 与比较策略

Spec 6 使用分层比较。

### 5.1 通用 normalizer

所有 extracted JSON 比较前先做通用规整：

- 删除 `Compiled.lastCompiledEditorDataHash`。
- 删除空数组与空对象噪声，仅限 generator 不区分缺失与空值的字段。
- 统一字段大小写到 canonical generator spelling，例如 `RootParameters`、`SubTrees`、`GlobalTasks`。
- 对数组中天然无序的集合排序，例如 root parameter object keys、binding list。
- 数字采用容差比较，默认 `1e-4`。
- object/class path 统一为长路径字符串，不比较 editor display name。

### 5.2 State/transition checker

严格比较：

- state readable path。
- state stable id/GUID。
- state type、selection behavior、tasks completion、enabled/tag/description/custom tick rate。
- transition trigger/type/priority/enabled/delay/required event。
- `GotoState` target 的 readable path 或可解析 GUID 等价性。
- transition condition node 的 `kind/type/instance` 可生成字段。

### 5.3 Parameter checker

严格比较：

- parameter scope：root、state、linked override。
- parameter name。
- parameter type。
- parameter GUID/id。
- default value。
- linked override 的目标 parameter GUID 与 value。

比较器允许 extracted path segment 以 `{ name, guid }` 形式出现，也允许 generator fixture 只写 `name`；normalizer 要根据 extracted parameter table 归一到 canonical `{scope, name, guid}`。

### 5.4 Binding checker

严格比较：

- ordinary binding source endpoint。
- target endpoint。
- target section：`node`、`instance`、`executionRuntimeData`。
- target path。
- property function binding type。
- function output path。
- function input map。
- nested function graph（如果 fixture 使用）。

Normalizer 要把以下等价表达统一：

- author id 与 deterministic GUID node id。
- binding path segment 的 string form 与 `{ name, guid }` object form。
- root parameter source 的 name-only form 与 GUID form。

不支持生成的 editor-authored metadata 要么被 extractor 省略，要么被 documentation 标为 extraction-only；不能输出会导致 generator 拒绝的字段，除非该字段有明确 limitation。

### 5.5 Node checker

比较以下可生成字段：

- `kind`
- `type`
- `node.properties`
- `instance.properties`
- `executionRuntimeData.properties`
- expression metadata：conditions/considerations 的 indent 与 operand

不比较 transient editor-only UI 状态、compiler compact handles、runtime baked data。

---

## 6. Verifier 设计

新增脚本建议路径：

```text
docs/superpowers/verification/statetree_roundtrip_check.py
```

脚本输入：

```bash
python3 docs/superpowers/verification/statetree_roundtrip_check.py \
	/tmp/ST_Name.extract1.json \
	/tmp/ST_Name.extract2.json \
	--fixture ST_Name
```

脚本职责：

- 加载两个 extracted configs。
- 运行通用 normalizer。
- 分层调用 state/transition、parameter、binding、node checker。
- 输出稳定摘要：
	- fixture name
	- states count
	- nodes count
	- parameters count
	- bindings count
	- transitions count
- mismatch 时输出最小路径，例如：

```text
StateTree round-trip mismatch: ST_RoundTrip_Comprehensive
path: SubTrees[Root]/children[Attack]/tasks[delay-task]/instance.Duration
left: 0.4000000059
right: 0.25
```

脚本不直接调用 MCP。MCP orchestration 由一次性 smoke 脚本或 shell/node helper 负责，round-trip checker 只做纯 JSON 比较，方便长期复用。

---

## 7. MCP / Editor Smoke

Spec 6 的最终 smoke 使用真实 GUI Editor，不使用 `UnrealEditor-Cmd`、`-NullRHI` 或 headless。启动方式沿用已验证稳定的直接二进制启动：

```bash
"/Users/pengao/UnrealEngine/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
	"/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject"
```

MCP smoke 步骤：

1. `health_check` 返回 ok。
2. 生成全部 positive fixtures。
3. 提取全部 positive fixtures 到 `/tmp/...extract1.json`。
4. 使用 extracted configs 重新生成。
5. 再次提取到 `/tmp/...extract2.json`。
6. 对每个 fixture 跑 `statetree_roundtrip_check.py`。
7. 生成并打开 `/Game/AFSmoke/ST_RoundTrip_Comprehensive`。
8. 运行 invalid fixtures，确认失败诊断仍稳定且没有 crash。

报告 smoke 时必须列出：

- 正常编译 action 数与 `Result: Succeeded`。
- Editor 是 GUI + Metal RHI 的日志证据。
- MCP health。
- round-trip checker 摘要。
- invalid fixture 失败摘要。

---

## 8. Error Handling

- Extracted JSON 如果包含 generator 当前不支持的字段，优先修 extractor，让它不要输出 unsupported generator input。
- 如果字段对 editor-authored assets 有诊断价值但不支持生成，必须在 schema limitation 中明确，且 round-trip fixtures 不依赖该字段。
- Checker mismatch 要返回非零退出码。
- Checker mismatch 不应吞掉后续 fixture；批量 orchestration 应收集所有失败后统一汇报。
- Floating point mismatch 使用容差；非数值字段必须严格比较。

---

## 9. Implementation Notes

推荐实现拆分：

1. 新增综合 fixture。
2. 新增 pure JSON round-trip checker。
3. 新增 orchestration helper 或记录标准 MCP smoke 命令。
4. 跑现有 fixtures，修暴露出来的 extractor/generator 兼容问题。
5. 更新 `MCP/schemas/StateTree.md` 的 extraction/round-trip 说明。
6. 正常编译 ProjectRPGEditor。
7. 真实 GUI Editor + MCP smoke。

Spec 6 允许做小范围 generator/extractor bugfix，但每个 bugfix 都必须由 round-trip failure 驱动，并通过同一个 checker 回归。

---

## 10. Acceptance Criteria

- `ST_RoundTrip_Comprehensive.json` 存在并可生成、编译、提取。
- 所有现有 positive StateTree fixtures 通过 `Generate -> Extract -> Generate -> Extract -> Compare`。
- `statetree_roundtrip_check.py` 能稳定比较至少：
	- states/transitions
	- parameters
	- ordinary bindings
	- property function bindings
	- dynamic nodes
- invalid StateTree fixtures 仍返回可读失败诊断。
- `MCP/schemas/StateTree.md` 说明 round-trip 支持范围与当前 limitations。
- ProjectRPGEditor 正常编译通过。
- 真实 GUI Editor 中可打开综合 fixture，并能看到生成的 StateTree 结构。

