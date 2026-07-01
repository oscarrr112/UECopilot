# AssetDocument Graph Wrapper Adapter Consolidation Design

日期：2026-06-30

状态：待审核

适用分支：`feature/asset-document-object-field-schema-dispatcher-migration`

## 1. 背景

AssetDocument 当前已经开始从 “每个 asset 一整套 extractor / applier / reducer / adapter” 迁移到 “sidecar + profile + policy + public capability/region runtime + thin asset-specific hook”。

本 spec 是 public region runtime 重构链的第 5 环：`Graph Wrapper Adapter Consolidation`。它接在 object field schema dispatcher、preview apply diff adapter、identity array diff helper 之后，目标是把 graph region 的生命周期接线抽成公共组合层。

当前代码里已经存在两类 graph 逻辑：

- `FWidgetBlueprintGraphRegionAdapter`：实现了 `IAssetDocumentRegionAdapter`，负责把 WidgetBlueprint 的 synthetic graph body 接到 validate / preflight / apply / extract / diff。
- `FUBlueprintGraphRegionAdapter`：不是 region runtime adapter，而是 profile-level graph materializer wrapper，当前主要处理 UBlueprint `UbergraphPages` 的 validate / extract / diff；apply 通过 `ApplyUBlueprintGraphRegions` 暴露。

两者真正重复的部分不是 graph DSL、node identity、pin mapping 或 K2 materialization，而是：

- 从 `FAssetDocumentRegionContext` 转换到 `FAssetDocumentCapabilityContext`。
- 要求 synthetic `/Body` value 是 JSON object。
- 把 region runtime 的 `ValidateRegion` / `PreflightRegion` / `ApplyRegion` / `ExtractRegion` / `DiffRegion` 分发到 profile graph hook。
- 把 extract 产物重新包成 `FJsonValueObject`。
- 让 diagnostic 和 diff entry 继续落在真实 body key path，而不是 synthetic region id。

这些属于公共 graph wrapper lifecycle，不应该继续各 asset profile 自己写一遍。

## 2. 目标

1. 新增可组合公共 wrapper：`FAssetDocumentGraphRegionWrapperAdapter`。
2. 让 WidgetBlueprint graph region adapter 改为使用该公共 wrapper，而不是自己持有整套生命周期实现。
3. 让 UBlueprint graph lifecycle 至少在 validate / apply / extract / diff 接线层使用同一公共 wrapper；UBlueprint staged variable / parent class 语义保留为 asset-specific hook。
4. 保持现有 public JSON schema、diagnostic path、diff path 和 graph materialization 行为不变。
5. 为后续支持 UBlueprint `FunctionGraphs` / `MacroGraphs` 留出入口，但第一版不扩大 UBlueprint graph authoring 能力。

## 3. 非目标

本 spec 明确不做：

- 不重写 graph DSL。
- 不改变 graph node / pin identity 策略。
- 不改变 `FAssetDocumentK2GraphAdapter` 的 parse / preflight / apply / extract 行为。
- 不把 Widget tree 和 Blueprint graph 合并成一个 adapter。
- 不在第一版为 UBlueprint 新增 `FunctionGraphs` / `MacroGraphs` materialization。
- 不把 WidgetBlueprint 的 desired-state scratch preflight 语义迁到 UBlueprint。
- 不把 graph compile、repair、refresh cache、semantic identity 写进公共 wrapper。

## 4. 现状梳理

### 4.1 WidgetBlueprint

`Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.h/.cpp` 里有：

- `FWidgetBlueprintGraphRegionAdapter : public IAssetDocumentRegionAdapter`
- synthetic region id：`Body.WidgetBlueprintGraphRegions`
- synthetic body path：`Body.WidgetBlueprintGraphRegions`
- region json pointer：`/Body`

该 adapter 当前做了两件事：

1. 生命周期 wrapper：
   - `SupportsRegion`
   - `GetSchemaHint`
   - `ValidateRegion`
   - `PreflightRegion`
   - `ApplyRegion`
   - `ExtractRegion`
   - `DiffRegion`

2. profile graph materializer delegation：
   - 默认委托 `FWidgetBlueprintGraphAdapter`
   - 测试可通过 `FWidgetBlueprintRegionAdapterHooks` 覆盖 validate / preflight / apply / extract / diff
   - preflight 可使用 `DesiredStateBlueprint`

`FWidgetBlueprintGraphAdapter` 自身负责真实 graph region：

- `UbergraphPages`
- `FunctionGraphs`
- `MacroGraphs`

它内部调用 `FAssetDocumentK2GraphAdapter`，并负责 Widget graph diff path rewrite。

### 4.2 UBlueprint

`Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.h/.cpp` 里有：

- `FUBlueprintGraphRegionAdapter`
- `ApplyUBlueprintGraphRegions`

当前行为：

- `ValidateRegions` 验证 `UbergraphPages`。
- `ExtractRegions` 写出 `UbergraphPages`。
- `DiffRegions` 对比 `UbergraphPages`。
- `ApplyUBlueprintGraphRegions` 应用 `UbergraphPages`。
- `FunctionGraphs` / `MacroGraphs` 仍按当前 profile 策略保持 deferred / empty / unsupported。

UBlueprint capability 还存在 staged validation：

- `ValidateGraphRegionsWithStagedVariables`
- parent class / variable specs 会先 staged，再验证 graph 引用。

这部分是 UBlueprint 的 asset-specific semantic hook，不应被公共 wrapper 吞掉。

## 5. 设计原则

### 5.1 组合优先

`FAssetDocumentGraphRegionWrapperAdapter` 不作为具体 graph adapter 的基类使用，也不要求 WidgetBlueprint 或 UBlueprint 通过继承覆盖虚函数。

它应该是一个可配置对象：

- 配置 region identity、schema hint、body pointer、support predicate。
- 通过 hooks 接入 profile-specific graph materializer。
- profile capability 可以直接实例化它，也可以保留一个很薄的 factory 函数。

### 5.2 公共 wrapper 只管生命周期

公共 wrapper 可以知道：

- region runtime 接口。
- synthetic body region 的 JSON object require。
- capability context 转换。
- hook 调用和错误传播。
- apply changed flag 的默认处理。
- extract value packaging。

公共 wrapper 不可以知道：

- `UbergraphPages` / `FunctionGraphs` / `MacroGraphs` 的解析规则。
- graph node semantic id。
- pin mapping。
- K2 graph materialization。
- WidgetBlueprint 或 UBlueprint 的具体类型语义。
- compile / repair / cache refresh。

### 5.3 真实 path 由 graph hook 负责

公共 wrapper 不应把错误路径统一改成 synthetic path。

例如：

- WidgetBlueprint graph diagnostic 仍应落在 `/Body/UbergraphPages`、`/Body/FunctionGraphs`、`/Body/MacroGraphs`。
- UBlueprint graph diagnostic 仍应落在 `/Body/UbergraphPages`。
- diff entry path 继续由 `FWidgetBlueprintGraphAdapter` / `FUBlueprintGraphRegionAdapter` 生成。

公共 wrapper 只在 body 本身不是 object 时返回 synthetic body path 的 shape error。

## 6. Public Runtime API

新增私有 runtime 文件：

- `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.cpp`

第一版保持在 `Private/Regions`，因为它是 profile implementation helper，不立刻承诺外部插件 API。后续如果第三个 profile 需要直接复用，可再评估移动到 `Public`。

建议接口：

```cpp
struct FAssetDocumentGraphRegionWrapperConfig
{
	FName AdapterName;
	FString RegionId;
	FString BodyPath;
	FString JsonPointer = TEXT("/Body");
	FText SchemaLabel;
};

struct FAssetDocumentGraphRegionWrapperHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject)> Validate;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject)> Preflight;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject,
		bool& bOutChanged)> Apply;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentCapabilityContext& Context,
		TSharedRef<FJsonObject>& OutBodyObject)> Extract;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)> Diff;
};

class FAssetDocumentGraphRegionWrapperAdapter final : public IAssetDocumentRegionAdapter
{
public:
	FAssetDocumentGraphRegionWrapperAdapter(
		FAssetDocumentGraphRegionWrapperConfig InConfig,
		FAssetDocumentGraphRegionWrapperHooks InHooks);

	// IAssetDocumentRegionAdapter
};
```

实现约束：

- `SupportsRegion` 默认匹配 `RegionId` 或 `BodyPath`，并要求 `JsonPointer` 匹配。
- `GetSchemaHint` 返回 adapter name / schema label，不声明具体 graph key schema。
- `ValidateRegion`、`PreflightRegion`、`ApplyRegion`、`DiffRegion` 要求 `DesiredValue` 是 object，再调用对应 hook。
- `ExtractRegion` 创建空 object，调用 extract hook，成功后返回 `FJsonValueObject`。
- 如果某个 lifecycle hook 未配置：
  - validate / preflight / apply / diff 返回明确 unsupported failure。
  - extract 返回明确 unsupported failure。
  - 不允许静默成功，避免 capability 误以为 graph region 已处理。

## 7. WidgetBlueprint 迁移

WidgetBlueprint 第一版迁移目标：

- 保留 `FWidgetBlueprintGraphRegionAdapter` 这个现有类名，避免一次性改动 capability bridge 和测试入口。
- 把该类改成 thin adapter/factory：内部持有或临时构造 `FAssetDocumentGraphRegionWrapperAdapter`。
- `FWidgetBlueprintRegionAdapterHooks` 继续可用于测试覆盖。
- `DesiredStateBlueprint` 继续只影响 preflight hook。

迁移后的默认 hooks：

- `Validate` -> `FWidgetBlueprintGraphAdapter().ValidateRegions(...)`
- `Preflight` -> `FWidgetBlueprintGraphAdapter().PreflightRegions(..., DesiredStateBlueprint ? DesiredStateBlueprint : Cast<UBlueprint>(Context.Asset))`
- `Apply` -> `FWidgetBlueprintGraphAdapter().ApplyRegions(...)`
- `Extract` -> `FWidgetBlueprintGraphAdapter().ExtractRegions(...)`
- `Diff` -> `FWidgetBlueprintGraphAdapter().DiffRegions(...)`

兼容要求：

- `Body.WidgetBlueprintGraphRegions` 仍然只是 synthetic runtime region，不出现在 public document body。
- `UbergraphPages` / `FunctionGraphs` / `MacroGraphs` 的 public JSON shape 不变。
- Widget graph diff path rewrite 行为不变。
- Scratch preflight 仍使用 desired-state blueprint。

## 8. UBlueprint 迁移

UBlueprint 第一版迁移目标：

- 新增 UBlueprint graph synthetic runtime binding，例如 `Body.UBlueprintGraphRegions`，仅用于 capability 内部生命周期分发。
- 该 synthetic region 的 `DesiredValue` 仍是整个 `/Body` object。
- 使用 `FAssetDocumentGraphRegionWrapperAdapter` 统一 validate / apply / extract / diff 接线。
- staged parent class / variable validation 继续留在 `ValidateGraphRegionsWithStagedVariables` 或同等 UBlueprint hook 中。

建议 hooks：

- `Validate`：
  - 默认调用 `FUBlueprintGraphRegionAdapter().ValidateRegions(...)`。
  - staged validation 路径继续由 UBlueprint capability 在调用 wrapper 前后组合，不下沉到公共 wrapper。
- `Apply`：
  - 调用 `ApplyUBlueprintGraphRegions(...)`。
- `Extract`：
  - 调用 `FUBlueprintGraphRegionAdapter().ExtractRegions(...)`。
- `Diff`：
  - 调用 `FUBlueprintGraphRegionAdapter().DiffRegions(...)`。

`Preflight` 第一版可采用保守策略：

- 如果现有 UBlueprint apply 流程没有独立 graph preflight lifecycle，则不强行新增公开行为。
- wrapper hook 可返回当前 capability 认可的 no-op success，或由 UBlueprint capability 暂不通过 runtime 调用 graph preflight。
- implementation plan 必须先确认当前 `Preflight` call graph，再选择两者之一。

兼容要求：

- UBlueprint `FunctionGraphs` / `MacroGraphs` 第一版仍不新增 authoring 支持。
- `UbergraphPages` 的 validate / apply / extract / diff 输出不变。
- staged variable / parent class validation 顺序不变。
- compile / repair 仍由 UBlueprint capability 的 apply 后处理负责。

## 9. 测试要求

### 9.1 公共 wrapper 单元测试

新增或扩展 region runtime 测试，覆盖：

- `SupportsRegion` 匹配 `RegionId`。
- `SupportsRegion` 匹配 `BodyPath`。
- `SupportsRegion` 拒绝非 `/Body` json pointer。
- validate 对非 object desired value 返回 shape failure。
- validate 调用 hook 并传播 failure。
- preflight 调用 hook，允许 hook 修改 capability context。
- apply 调用 hook 并传播 `bOutChanged`。
- apply 在 body shape failure 时将 `bOutChanged` 置为 false。
- extract 调用 hook 并返回 object value。
- diff 调用 hook 并追加 diff entries。
- 未配置 hook 返回 unsupported failure，而不是成功。

### 9.2 WidgetBlueprint 回归测试

覆盖现有 Widget graph 行为：

- `UbergraphPages` / `FunctionGraphs` / `MacroGraphs` validate 仍走原 graph adapter。
- graph diff path 仍是 `/Body/<GraphRegion>`。
- desired-state scratch preflight 行为不变。
- existing WidgetBlueprint graph automation test 全部通过。

### 9.3 UBlueprint 回归测试

覆盖现有 UBlueprint graph 行为：

- `UbergraphPages` validate path 不变。
- `UbergraphPages` apply 仍能 materialize graph。
- `UbergraphPages` extract payload 不变。
- `UbergraphPages` diff entry path 不变。
- staged variable / parent class validation 对 graph references 的效果不变。
- `FunctionGraphs` / `MacroGraphs` 不因为 wrapper 迁移变成可写。

## 10. 验证要求

implementation plan 必须包含：

1. C++ 单元/automation 测试。
2. `git diff --check`。
3. UBT 编译。
4. AssetDocument 相关 automation test。
5. 至少一次 reviewer 只审该 task 的 `TASK_BASE..HEAD`。

推荐 UBT：

```powershell
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

推荐 automation host：

```powershell
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests <AssetDocumentGraphTests>;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

具体 test filter 由 implementation plan 根据现有 test 名称确认。

## 11. 实施切分建议

本 spec 不直接进入生产代码修改；实现前需要另写 implementation plan。建议任务切分：

1. `Task 1 - Common Graph Wrapper`
   - 新增 `FAssetDocumentGraphRegionWrapperAdapter`。
   - 添加 fake-hook 单元测试。
   - 不迁移任何 profile。

2. `Task 2 - WidgetBlueprint Migration`
   - 让 `FWidgetBlueprintGraphRegionAdapter` 委托公共 wrapper。
   - 保留现有 public class name 和 test hooks。
   - 跑 WidgetBlueprint graph 回归。

3. `Task 3 - UBlueprint Migration`
   - 增加 UBlueprint synthetic graph runtime binding。
   - validate / apply / extract / diff 接线迁移到公共 wrapper。
   - staged validation 保持 asset-specific hook。
   - 跑 UBlueprint graph 回归。

4. `Task 4 - Review Cleanup`
   - 删除重复 lifecycle helper。
   - 确认 graph materializer 仍独立可测。
   - 更新 refactor-chain 文档中第 5 环状态。

每个 task 结束后需要 checkpoint commit，不带 dirty diff 进入下一 task。

## 12. 完成标准

本 spec 完成后应满足：

- `FAssetDocumentGraphRegionWrapperAdapter` 是 graph region lifecycle 的唯一公共 wrapper。
- WidgetBlueprint 与 UBlueprint 至少在 validate / apply / extract / diff 生命周期接线上共享该 wrapper。
- graph materialization 仍由 `FWidgetBlueprintGraphAdapter` / `FUBlueprintGraphRegionAdapter` / `FAssetDocumentK2GraphAdapter` 负责。
- public AssetDocument body schema 不变。
- diagnostic path 和 diff path 不回退到 synthetic region id。
- UBlueprint `FunctionGraphs` / `MacroGraphs` 行为不变。
- 所有新增行为有自动化测试或明确 regression test 覆盖。

## 13. 后续升级触发条件

出现以下任一情况时，应继续抽下一层公共 graph region 能力，而不是继续堆 profile-specific code：

- 第三个 profile 需要 synthetic graph body wrapper。
- UBlueprint 开始支持 `FunctionGraphs` / `MacroGraphs`，且实现形态与 WidgetBlueprint graph region list 接近。
- 多个 profile 重复实现 graph diff path rewrite。
- 多个 profile 重复维护 graph region list 与 `EAssetDocumentK2GraphRegion` 的映射。
- graph preflight desired-state / scratch-state 语义开始在两个以上 profile 复用。

这些触发条件应进入新的 spec 或 implementation plan，而不是夹在本次迁移里顺手完成。
