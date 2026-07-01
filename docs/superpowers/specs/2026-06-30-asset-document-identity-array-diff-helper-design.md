# AssetDocument Identity Array Diff Helper 设计规格

日期：2026-06-30

状态：正式规格。本文定义 `AssetDocument Public Region Runtime` 重构链第 4 环。进入实现前必须另写 implementation plan，并按 task checkpoint commit 推进。

## 1. 背景

前置重构已经完成两类公共能力：

- object field schema utility：统一 object region 内字段白名单、类型、required/optional、unknown field diagnostic。
- preview apply diff adapter：统一 validate desired、duplicate preview asset、apply、extract、canonical compare、diff entry 构造。

下一类重复集中在 identity array diff：多个 profile 都需要把 current array 和 desired array 按语义 identity 对齐，然后为每个元素生成稳定 diff path 和状态。

当前代表性重复包括：

- `UBlueprint` `Variables`：按 variable name 对齐，current-only 为 `extra`，desired-only 为 `missing`，字段语义变化为 `changed`。
- `UBlueprint` `ImplementedInterfaces`：按 interface class path 对齐。
- `UBlueprint` `Components`：按 component key 对齐，同时存在 native alias canonical key。
- `WidgetBlueprint` `ImplementedInterfaces`：与 `UBlueprint` 同形态重复。
- `WidgetBlueprint` `Variables`：当前先做整 region semantic compare，后续可升级为 per-element identity diff。
- `AnimSequence` `NotifyTracks`：已经通过 named-array adapter 管 identity validation，但 diff 仍依赖 profile pilot hook 和 preview apply diff。

这说明需要抽的是“identity 对齐和 diff entry 生成”这一层，而不是再新增一个理解 Blueprint、WidgetBlueprint 或 AnimSequence 的大 adapter。

## 2. 设计目标

- 提供公共 helper，统一 identity array 的 diff scaffold。
- Profile 只声明如何从 current / desired 元素取 identity、如何构造 path、如何判断元素是否 changed、如何生成 current / desired JSON value。
- Helper 负责 current-only、desired-only、both-present 的遍历顺序、seen set、entry status、change、稳定 path 和 JSON cloning。
- 保留资产语义在 hook 中：component alias、interface class loading、variable type/default compare、notify track order policy 都不进入公共层。
- 第一版至少迁移一个 Blueprint profile 和一个非 Blueprint 或第二 profile 的同形态切片，证明 helper 不被单一资产绑死。

## 3. 非目标

- 不替代 `FAssetDocumentNamedArrayRegionAdapter`。现有 named-array adapter 继续负责 array<object> shape validation、identity field/alias validation、apply/extract hooks 和 canonical extraction。
- 不处理 timeline placement。time、duration、track existence、section linking、marker rebuild 属于第 7 环。
- 不处理 graph/tree node identity。Graph node semantic id、pin mapping、graph materializer 属于 graph wrapper 环。
- 不把 component alias、owner class identity、graph node semantic id、notify semantic key写死进 helper。
- 不引入 profile 继承基类。
- 不在 helper 内直接读写 `UBlueprint`、`UWidgetBlueprint`、`UAnimSequence` 或任何具体 UObject 类型。
- 不改变 MCP schema、sidecar 格式或 public JSON contract。

## 4. 公共接口草案

第一版建议放在 private region runtime 层：

- `Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.cpp`

候选类型：

```cpp
struct FAssetDocumentIdentityArrayDiffElement
{
	FString Identity;
	FString PathToken;
	TSharedPtr<FJsonValue> Value;
};

struct FAssetDocumentIdentityArrayDiffEntryContext
{
	FString Identity;
	FString Path;
	TSharedPtr<FJsonValue> CurrentValue;
	TSharedPtr<FJsonValue> DesiredValue;
	bool bHasCurrent = false;
	bool bHasDesired = false;
};

struct FAssetDocumentIdentityArrayDiffOptions
{
	FString RegionPath;
	FString ExtraChange = TEXT("extra");
	FString MissingChange = TEXT("missing");
	FString ChangedChange = TEXT("changed");
	bool bEmitUnchanged = true;
};

struct FAssetDocumentIdentityArrayDiffHooks
{
	TFunction<bool(const FAssetDocumentIdentityArrayDiffEntryContext&)> AreElementsEqual;
	TFunction<FString(const FAssetDocumentIdentityArrayDiffEntryContext&)> MakePath;
	TFunction<FString(const FAssetDocumentIdentityArrayDiffEntryContext&)> MakeChange;
};
```

候选 helper：

```cpp
class FAssetDocumentIdentityArrayDiffHelper
{
public:
	static FAssetDocumentCapabilityResult Diff(
		const FAssetDocumentIdentityArrayDiffOptions& Options,
		const TArray<FAssetDocumentIdentityArrayDiffElement>& CurrentElements,
		const TArray<FAssetDocumentIdentityArrayDiffElement>& DesiredElements,
		const FAssetDocumentIdentityArrayDiffHooks& Hooks,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries);
};
```

Implementation plan 可以调整命名和形参，但必须保持以下约束：

- 通过组合 hooks 注入语义，不通过继承。
- Helper 输入是已 materialize 的 JSON/value element，不负责 parse UE 对象。
- Helper 输出使用现有 `FAssetDocumentJsonRegionUtils::AddDiffEntry` 或同等公共 diff entry helper。
- Helper 不接收 asset class enum，不按 region name 写全局 `switch`。

## 5. Identity 和 Path 规则

公共 helper 应区分三种字符串：

- `Identity`：用于 current / desired 对齐的 normalized key。它可以是 `Health`、interface class path、component canonical key 等。
- `PathToken`：默认用于 JSON Pointer path 的 token，必须经过 `EscapeJsonPointerToken`。
- `Path`：最终 diff path。默认可由 `RegionPath + "/" + escaped PathToken` 生成，但 profile 可用 hook 完全覆盖，例如 component path `"/Body/Components/<Owner>:<Name>"`。

默认行为：

- current 和 desired 都存在且 equal：`status = "unchanged"`，change 为空。
- current 和 desired 都存在但不 equal：`status = "changed"`，change 默认为 `"changed"`。
- current-only：`status = "changed"`，change 默认为 `"extra"`，desired value 为 null。
- desired-only：`status = "changed"`，change 默认为 `"missing"`，current value 为 null。

顺序要求：

- 先按 current authored/extracted 顺序输出 current-present entries。
- 再按 desired authored order 输出未 seen 的 desired-only entries。
- 不默认排序。需要 canonical sort 的 region 应在 profile 或 named-array adapter 中显式处理。

## 6. 与现有公共层的关系

### `FAssetDocumentNamedArrayRegionAdapter`

现有 named-array adapter 已经做：

- desired array shape validation。
- object element validation。
- identity field / alias 读取。
- missing / duplicate identity diagnostic。
- apply order 与 extract canonicalization。

Identity diff helper 不重复这些职责。它可以被 named-array adapter 的 `Hooks.DiffElements` 使用，也可以被 profile-local diff 直接使用。

后续如需把 helper 接入 named-array adapter，应该通过 `DiffElements` hook 或轻量 config 注入，不能让 named-array adapter 知道具体资产语义。

### `FAssetDocumentPreviewApplyDiffAdapter`

Preview apply diff adapter 仍负责 current/preview body 的生成。Identity diff helper 可以用于 preview diff 的 per-key hook 内，把 `CurrentValue` / `DesiredValue` 中的 array 进一步拆成 per-element entries。

第一版不要求所有 preview apply diff region 立刻输出 per-element entries。只有已有 per-element diff 或明显重复的区域才迁移。

### `FAssetDocumentJsonRegionUtils`

Helper 应复用现有 JSON utility：

- JSON Pointer token escaping。
- diff entry 构造。
- canonical JSON compare 或 value clone。

如果现有 utility 缺少 deep clone / comparable compare 入口，implementation plan 应优先复用已经存在的 public region utility，而不是在 profile 中继续复制。

## 7. 第一版迁移范围

建议第一版只做小而真实的迁移：

### Task A：公共 helper 和 runtime tests

新增 helper，覆盖：

- unchanged entry。
- changed entry。
- current-only extra。
- desired-only missing。
- custom path hook。
- custom equality hook。
- path token escaping。
- duplicate identity 输入的处理策略。

Duplicate 策略建议第一版保守处理：

- Helper 假设输入已经完成 duplicate validation。
- 如果 helper 自身发现重复 identity，返回 `DuplicateIdentityArrayDiffIdentity` failure，path 指向 region path。
- 具体 duplicate diagnostic path 仍应由 profile / named-array validation 负责。

### Task B：迁移 `ImplementedInterfaces` diff

优先迁移 `UBlueprint` 和 `WidgetBlueprint` 的 `ImplementedInterfaces`，因为它们形态最接近：

- identity：interface class path。
- current value：`MakeInterfaceDiffValue(CurrentInterface.Interface)`。
- desired value：`InterfaceToJsonObject(DesiredInterface.InterfaceClass)` 或 `MakeInterfaceDiffValue` 统一后的 value。
- path：`/Body/ImplementedInterfaces/<ClassPath>`。

验收点：

- current-only interface 仍是 changed/extra 或现有 profile 可接受的等价 change。
- desired-only interface 仍是 changed/missing 或现有 profile 可接受的等价 change。
- unchanged interface path 不变。
- UBlueprint 与 WidgetBlueprint 共享同一 helper，不复制对齐循环。

### Task C：迁移 `UBlueprint Variables` 或 `Components` 中较小一段

如果 Task B 后风险仍可控，再迁移一个更有语义的 Blueprint slice：

- 首选 `UBlueprint Variables`：identity 是 variable name，equality hook 保留 `AuthoredPinTypesDiffer`、default value、category、tooltip 等现有语义。
- 备选 `UBlueprint Components`：identity 是 component key，但 native alias canonical key 复杂，适合作为第二版或单独 task。

第一版建议先迁移 `Variables`，暂不迁移 `Components`。`Components` 需要 canonical key alias 和 native/inherited/owned component source，容易把 helper 拉向 Blueprint 专用。

## 8. 明确暂缓范围

以下内容不进入第一版：

- `UBlueprint Components` 的 native alias canonical key 生成。
- `AnimSequence Notifies`、`NotifyStates`、`SyncMarkers` timeline placement diff。
- `AnimMontage CompositeSections`、`SlotAnimTracks` timeline/track diff。
- Widget animation track diff。
- Graph node / pin diff。
- Fragment arrays，如 `Metadata`、`AssetUserData`、`SectionMetadata`。

这些区域可以在 helper 稳定后作为后续迁移候选，但不能让第一版横跨太多 shape。

## 9. 行为兼容要求

迁移后必须保持：

- diff path 稳定，不从 `/Body/Variables/Health` 退化成 `/Body/Variables/0`。
- status 仍为现有消费者理解的 `changed` / `unchanged`。
- change 字段如现有 profile 使用 `extra` / `missing` / `changed`，必须保留或在 spec review 中明确批准变更。
- current / desired value shape 不变。
- parse / validate failure 的 diagnostic path 不变；helper 不应该接管 parse failure。
- omitted region 与 explicit empty region 的语义不变。

## 10. 测试要求

公共 helper tests：

- 放在 `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp` 或更合适的 region runtime test 文件。
- 覆盖 add/remove/change/unchanged、custom path、escape、duplicate failure。
- 断言 diff entry 的 path/status/change/current/desired。

Profile tests：

- `AssetDocumentUBlueprintTests.cpp` 应覆盖 `Variables` 或 `ImplementedInterfaces` 的 per-element diff path。
- `WidgetBlueprint` 对应测试应覆盖 `ImplementedInterfaces` 迁移后的 path/status。
- 如果某 profile 旧测试只检查整 region changed，需要补 per-element path assertion。

验证：

- `git diff --check`。
- UBT validation host 编译。
- focused automation：`AssetFactory.AssetDocument.UBlueprint`、`AssetFactory.AssetDocument.WidgetBlueprint`。
- full `AssetFactory.AssetDocument` automation 在 final review 前运行。

## 11. Review 要求

Task review 只审当前 task 的 `TASK_BASE..HEAD`。Final review 只审本 spec 的 `SPEC_BASE..HEAD`。

Reviewer 重点：

- Helper 是否只处理 identity diff scaffold，没有资产分支。
- `UBlueprint` 与 `WidgetBlueprint` 是否真的共享 helper。
- path/status/change/value shape 是否兼容。
- 是否把 component alias、variable type compare、interface class loading 等语义留在 profile hook。
- 是否没有绕过 existing validation。

## 12. 终止条件

出现以下情况必须停止实现，回到 spec 讨论：

- Helper 内出现 `UBlueprint`、`UWidgetBlueprint`、`UAnimSequence` 等具体资产类型。
- Helper 内出现按 `Variables`、`ImplementedInterfaces`、`Components` 等 region name 的全局分支。
- 为了复用 helper 要求 profile 改成继承某个 base capability。
- 迁移 `Components` 时需要把 native alias 或 owner class resolution 放进 helper。
- 迁移导致 diff path 从 semantic path 退化为 array index path。
- 第一版 task 同时改 Blueprint、Widget、AnimSequence、AnimMontage、timeline 多个 shape，无法隔离 review。

## 13. 完成标准

本环完成时应满足：

- 存在公共 `FAssetDocumentIdentityArrayDiffHelper` 或等价命名的 helper。
- 至少两个 profile 或两个独立 region 使用同一 helper，其中至少一个来自 Blueprint/WidgetBlueprint 的 `ImplementedInterfaces` 同形态重复。
- Helper 没有资产类型依赖、没有 region name switch、没有继承要求。
- 迁移区域的 diff path/status/change/current/desired 兼容旧行为。
- 对 public helper 和迁移 profile 都有自动化覆盖。
- 下一环 `Graph Wrapper Adapter Consolidation` 可以在不重新解决 identity diff scaffold 的前提下继续推进。
