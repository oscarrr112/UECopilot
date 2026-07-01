# AssetDocument Region Runtime Test Fixture And Maintenance Sweep Design

日期：2026-07-01

状态：待审核

适用基线：`feature/asset-document-object-field-schema-dispatcher-migration`

## 1. 背景

AssetDocument public region runtime 重构链已经完成了主要生产侧公共层：

- `FAssetDocumentBodyRegionDispatcher`
- `IAssetDocumentRegionAdapter`
- `FAssetDocumentRegionRuntime`
- `FAssetDocumentJsonRegionUtils`
- deferred / object / named-array / preview-apply-diff / identity-array / graph-wrapper / fragment-array / timeline-placement 等公共 adapter 或 helper

现在剩下的是第 8 环：`Region Runtime Test Fixture`，以及收尾性质的维护规则回扫。

当前 `AssetDocumentRegionRuntimeTests.cpp` 已经积累了大量重复测试样板：

- JSON object/array value builder。
- region policy、binding、context builder。
- dispatcher construction helper。
- diff entry path lookup。
- diagnostic path/code assertion。

这些重复不是 production 行为问题，但会让后续新增 public adapter tests 更慢、更容易漏掉 path/code 断言。第 8 环的目标是把这部分测试样板抽成一个小 fixture，同时把维护文档更新到“后续新增资产/profile 必须继续沿 public runtime + thin hook 的方向走”。

## 2. 设计结论

新增一个 test-only、header-only 的 fixture：

- `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h`
- namespace：`AssetDocumentRegionRuntimeTest`
- 只在 `WITH_DEV_AUTOMATION_TESTS` 下可见

fixture 只承接测试样板：

- JSON value builders
- `FAssetDocumentRegionContext` builder
- region policy / binding helper
- dispatcher construction helper
- diff entry lookup helper
- exact diagnostic path/code assertion helper

fixture 不承接任何资产语义，不依赖 profile，不触碰 production adapter。

第一版只迁移代表性 runtime tests：

- deferred region tests
- object region diagnostics
- named array diagnostics
- dispatcher context construction 示例
- preview apply diff failure diagnostics
- timeline placement invalid shape diagnostics

第一版不迁移所有 runtime tests。高价值语义断言仍保留在测试体中，尤其是 identity、canonical order、timeline duplicate 顺序、track resolver、fragment materialization boundary 等。

## 3. 目标

1. 降低新增 public adapter tests 的 context/bootstrap 样板。
2. 统一 diagnostic path/code assertion 的写法。
3. 保持 exact path/code 断言，不允许 fixture 把失败断言变成模糊匹配。
4. 让新 public adapter tests 能清楚区分：
   - public runtime / adapter utility behavior
   - profile-specific UE materialization behavior
   - MCP / apply-file / editor integration behavior
5. 更新重构链、new asset guide、deferred/canonicalization 维护文档，让后续 session 有明确约束。

## 4. 非目标

1. 不修改 production adapter、profile、service、MCP schema 或 TypeScript。
2. 不把 fixture 做成通用测试框架。
3. 不替代 profile-level automation tests。
4. 不替代真实 UE asset smoke、save/load、compile/rebuild/cache repair 验证。
5. 不把所有 `AssetDocumentRegionRuntimeTests.cpp` helper 一次性迁出。
6. 不隐藏 adapter utility 的核心语义断言。
7. 不改变任何 existing diagnostic path/code。

## 5. Fixture 边界

### 5.1 允许进入 fixture 的内容

允许放入 fixture 的能力必须满足三个条件：

1. 与具体 asset class 无关。
2. 只服务 `AssetDocumentRegionRuntimeTests.cpp` 或后续 public region runtime tests。
3. 不改变断言语义，只减少重复样板。

第一版允许函数：

- `MakeObjectValue`
- `MakeArrayValue`
- `MakeObjectRef`
- `MakeBodyWithField`
- `MakePolicy`
- `MakeDeferredPolicy`
- `MakeBinding`
- `MakeRuntimeContext`
- `FAssetDocumentRegionRuntimeTestContextBuilder`
- `MakeDispatcher`
- `GetDiffEntryPath`
- `FindDiffEntryByPath`
- `TestDiagnostic`

### 5.2 不允许进入 fixture 的内容

以下内容必须留在具体测试或更贴近具体 adapter 的 helper 中：

- `FTestRegionAdapter` 这类为某组 dispatcher tests 服务的 fake adapter，除非第二组测试也需要同一形态。
- `MakeIdentityValue`、`GetIdentityCount` 这类 identity-array 专用 helper。
- `MakeTestFragment` 这类 fragment-array 专用 helper。
- timeline-specific config、track resolver、duplicate key hook。
- profile-specific UObject construction 或 editor subsystem calls。
- 自动读取 automation report 或启动 Unreal Editor 的命令封装。

## 6. Diagnostic Helper 规则

`TestDiagnostic` 只做 exact path/code assertion：

```cpp
TestDiagnostic(this, TEXT("Non-array"), Result, TEXT("/Body/NotifyTracks"), TEXT("InvalidBodySectionType"));
```

它不得：

- 只检查 `Result.bSuccess == false`。
- 接受 regex、contains、starts-with 或多个候选 code。
- 自动吞掉 missing diagnostic。
- 自动修正 JSON Pointer escaping。
- 将 expected path/code 从 policy 或 region id 推导出来。

fixture 的职责是减少重复，不是替测试做判断。

## 7. Migration Scope

第一版迁移代表性用例，不追求全文件替换。

### 7.1 Deferred region

迁移目标：

- non-empty array diagnostic
- non-empty object diagnostic

保留目标：

- empty array / empty object acceptance 仍保持直接读写，便于看出 deferred adapter 的 shape 语义。

### 7.2 Object region

迁移目标：

- non-object diagnostic
- missing lifecycle hook diagnostics

保留目标：

- hook invocation、extract、canonical diff 的语义断言仍显式写在测试体。

### 7.3 Named array

迁移目标：

- non-array diagnostic
- duplicate identity diagnostic
- normalized duplicate identity diagnostic
- missing identity diagnostic

保留目标：

- canonicalization、stable identity ordering、diff path 语义仍显式写在测试体。

### 7.4 Dispatcher

迁移目标：

- 至少一个 dispatcher test 使用 `FAssetDocumentRegionRuntimeTestContextBuilder`，作为后续测试参考。

保留目标：

- apply order、unknown key rejection、required key behavior 仍按当前测试结构显式断言。

### 7.5 Preview apply diff

迁移目标：

- invalid preview context diagnostics
- hook failure diagnostics

保留目标：

- preview asset duplication、body mutation isolation、canonical compare behavior 仍显式断言。

### 7.6 Timeline placement

迁移目标：

- invalid shape diagnostics。

保留目标：

- duplicate-before-semantic lifecycle、track resolver、numeric validation、extract/diff stability 仍显式写在 timeline tests 中，不迁入 fixture。

## 8. Documentation Sweep

### 8.1 Refactor chain

`docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md` 需要标记第 8 环实现状态：

- fixture 文件入口。
- 已迁移代表性 runtime tests。
- fixture 只服务 public runtime tests。
- 第三批执行顺序中第 8 环变成已完成，guide/deferred sweep 和下一个新资产 thin hook 仍保留为后续操作约束。

### 8.2 New asset guide

`docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md` 需要新增 public adapter test fixture 规则：

- 新增 public adapter tests 优先复用 fixture。
- exact diagnostic path/code 仍必须断言。
- adapter utility 核心语义必须留在测试体。
- profile-level automation 仍验证 UE materialization、compile/rebuild、cache repair、save/load。

### 8.3 Deferred/canonicalization maintenance doc

`docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md` 需要增加测试证据分层规则：

- public runtime / adapter fixture tests 证明 shared runtime、adapter utility、diagnostic 和 canonical compare。
- profile automation tests 证明 UE materialization、post-apply repair、compile/rebuild、save/load。
- MCP / apply-file smoke 证明协议、sidecar path contract 和 editor integration。

## 9. Execution Model

实现阶段必须新建独立 branch 和 worktree。

建议：

- branch：`feature/asset-document-region-runtime-test-fixture-sweep`
- worktree：`E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-region-runtime-test-fixture-sweep`

任务拆分：

1. 新增 fixture 并迁移 deferred region 代表性 tests。
2. 迁移 object / named-array / dispatcher / preview-apply-diff / timeline invalid shape 代表性 tests。
3. 回扫维护文档。
4. final review + UBT + focused automation。

每个 task 都必须 checkpoint commit。review diff range 使用真实 `TASK_BASE..HEAD`，final review 使用 `SPEC_BASE..HEAD`。

## 10. Verification

最低验证：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -NoHotReload
```

focused automation：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/RegionRuntimeFixtureFinal" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime; Quit"
```

profile smoke：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/RegionRuntimeFixtureAnimSequence" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence; Quit"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/RegionRuntimeFixtureAnimMontage" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage; Quit"
```

每个 report 的接受条件：

- `Failed=0`
- `NotRun=0`

## 11. Review Criteria

spec review 必须确认：

- fixture 范围符合第 8 环，不越界到 production behavior。
- fixture 有真实 migrated users，不是空框架。
- 迁移范围与本 spec 一致，没有全量扫动测试文件。
- 文档回扫覆盖 refactor chain、new asset guide、canonicalization/deferred maintenance。

code quality review 必须确认：

- header-only fixture 不引入 ODR 或 include 顺序风险。
- helper 命名不会与现有 anonymous namespace helper 混淆。
- diagnostic helper 不隐藏 path/code 断言。
- 迁移后测试可读性没有下降。
- 没有修改 production adapter/profile 行为。

## 12. 停止条件

出现以下任一情况，必须停止实现并回到 spec 讨论：

- fixture 需要引用 profile 或 asset class headers。
- fixture 开始封装 automation runner、UBT、Editor process 或 report parsing。
- fixture 需要理解 object/named-array/fragment/timeline 的具体语义。
- 迁移导致 diagnostic path/code 文本改变。
- 单个 task 需要迁移大部分 `AssetDocumentRegionRuntimeTests.cpp`。
- 为了让 fixture 通用而删除具体测试中的语义断言。

## 13. 完成定义

本环完成时必须满足：

- `AssetDocumentRegionRuntimeTestFixture.h` 存在且仅在 tests 中使用。
- 至少 deferred/object/named-array/dispatcher/preview-apply-diff/timeline 各有一个代表性测试使用 fixture。
- exact diagnostic path/code 断言仍可读、可审。
- refactor chain 标记第 8 环已实现。
- new asset guide 明确后续 public adapter tests 优先使用 fixture。
- deferred/canonicalization 文档明确测试证据分层。
- UBT 通过。
- `AssetFactory.AssetDocument.RegionRuntime` automation 通过。
- AnimSequence 与 AnimMontage focused automation 通过。
- final spec review 与 code quality review 均通过。

完成后，public region runtime 重构链可视为进入“新资产 thin hook 验证”阶段：下一步不是继续抽大公共层，而是选择下一个 asset profile，用这套规则证明新增资产不再堆私有 lifecycle。
