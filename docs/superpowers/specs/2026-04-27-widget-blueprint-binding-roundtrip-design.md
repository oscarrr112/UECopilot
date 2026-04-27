# WidgetBlueprint Binding 闭环与校验设计

- **日期**：2026-04-27
- **状态**：Draft（待用户审阅）
- **范围**：修复 WidgetBlueprint binding 的 Extract -> Generate -> Extract 闭环、property/function binding 语义完整性，以及 MCP schema 与实现的一致性。

---

## 1. 目标

WidgetBlueprint generator / extractor 应能可靠保留 UMG property bindings。给定一个已有 WidgetBlueprint，`extract_assets` 输出的 JSON 再传给 `generate_assets` 后，重新提取应保留同等 binding 语义。

本 spec 覆盖：

1. 统一 binding JSON 契约，修复 extractor 输出和 generator 输入不一致。
2. 为 property binding 构造 UE 期望的 `SourcePath` / `MemberGuid` 信息，而不是只写 `SourceProperty`。
3. 在添加 function binding 前校验目标 widget 属性、delegate、函数签名和 pure/const 要求。
4. 更新 MCP WidgetBlueprint schema，让 schema 同时描述 authoring format、extract round-trip format 和兼容输入。
5. 增加自动化验证，覆盖 create、extract round-trip、invalid binding 报错。

## 2. 当前问题

### 2.1 Extract / Generate 形状不闭环

`FWidgetBlueprintGenerator::Extract` 当前把所有 binding 写到顶层：

```json
{
  "Bindings": {
    "ScoreText.Text": {
      "Function": "GetScoreText",
      "Kind": "Function"
    }
  }
}
```

实现证据：

- `WidgetBlueprintGenerator.cpp:1558-1583` 遍历 `Blueprint->Bindings`，用 `WidgetName.PropertyName` 作为 key，然后写入顶层 `Config.Bindings`。
- 创建路径只在 widget node 中读取 `Bindings`：`WidgetBlueprintGenerator.cpp:306-317`。
- `WidgetUpdates` 只在单个 update object 中读取 `Bindings`：`WidgetBlueprintGenerator.cpp:1375-1383`。
- `ConfigureBindings` 把 `Bindings` 的 key 当成目标属性名：`WidgetBlueprintGenerator.cpp:1053-1060`，因此不能直接消费顶层 `Widget.Property` key。

这意味着当前 `Extract -> Generate` 很可能丢失 binding，或者把 `ScoreText.Text` 错当成属性名。

### 2.2 Property binding 信息不完整

当前 property binding 分支只设置：

```cpp
NewBinding.SourceProperty = *Property;
NewBinding.Kind = EBindingKind::Property;
```

但 UE 5.7 的 `FDelegateEditorBinding` 还有：

- `SourcePath`
- `MemberGuid`

编辑器正常添加 binding 时会构造 `FEditorPropertyPath(FieldChain)`，同时为 property / function 填 `MemberGuid`。运行期 `FDelegateRuntimeBinding` 只携带 `SourcePath`，没有 `SourceProperty` 字段。

补充 nuance：UE 编译器在 `SourcePath` 为空时会尝试基于 `SourceProperty` 为简单 self property binding 生成 `__Get...` 函数。因此当前实现不一定在所有简单场景下完全失效，但它只覆盖很窄的顶层 self property 绑定，无法表达嵌套路径、函数路径、稳定 rename 或编辑器正常创建的 binding 结构。

### 2.3 Function binding 校验过弱

当前 function binding 只检查函数名是否存在于 `GeneratedClass` / `ParentClass` / `FunctionGraphs`。找不到函数也只是 warning，仍继续创建 binding。

UE 侧实际要求更严格：

- 目标 widget 上必须存在可绑定的 delegate property，通常是 `PropertyNameDelegate`。
- 绑定函数签名必须与目标 delegate 兼容，或能通过 UMG binder 支持。
- property binding 目标要求函数是 `const` 或 `BlueprintPure`。
- 无效 binding 应在生成阶段给出明确失败，而不是保存一个打开后才报错或运行期无效的资产。

### 2.4 MCP schema 与实现不一致

`MCP/schemas/WidgetBlueprint.md` 只描述 widget-level `Bindings`，没有列出 extractor 当前顶层 `Bindings` 格式，也没有说明 property binding 的 `SourcePath` / `MemberGuid` 语义和 function binding 的校验条件。

## 3. 非目标

- 不重写 WidgetBlueprintGenerator 的 widget tree、slot、style 动态化逻辑。
- 不引入针对具体 widget 类型或具体属性名的硬编码 switch / case。
- 不要求手写所有 UE binder 类型列表。
- 不在本 spec 中迁移到 UE FieldNotification / MVVM 绑定系统。
- 不改变 `WidgetUpdates` 的增量更新主语义。
- 不在本次设计中提交实现代码；实现前仍需要对应 implementation plan。

## 4. 推荐方案

采用 **widget-level canonical format + top-level compatibility input**。

### 4.1 Canonical authoring / extract 格式

规范格式是 widget 节点内的 `Bindings`：

```json
{
  "Type": "TextBlock",
  "Name": "ScoreText",
  "Bindings": {
    "Text": {
      "Kind": "Function",
      "Function": "GetScoreText"
    }
  }
}
```

Extractor 应默认把 binding 写回对应 widget node。这样 `extract_assets` 的输出可以直接作为 `generate_assets` 输入。

### 4.2 兼容读取顶层格式

Generator 应兼容读取旧的顶层 `Bindings`：

```json
{
  "Bindings": {
    "ScoreText.Text": {
      "Kind": "Function",
      "Function": "GetScoreText"
    }
  }
}
```

兼容策略：

- `WidgetName.PropertyName` 中最后一个 `.` 前为 widget name，最后一段为 target property。
- 生成器在 widget tree 构建或 update 操作完成后解析顶层 bindings。
- 找不到 widget 时返回明确错误或 validation failure。
- 如果同一个 widget/property 同时出现在 widget-level 和 top-level，widget-level 配置优先，顶层配置产生 warning 或被覆盖，具体行为需在 implementation plan 中固定并测试。

### 4.3 不输出双份 binding

Extractor 不应同时输出顶层和 widget-level 两份 binding。双份输出会制造冲突来源，也会让调用方误以为两份都需要维护。

## 5. Binding JSON 设计

### 5.1 Function binding

推荐完整格式：

```json
{
  "Kind": "Function",
  "Function": "GetDisplayText"
}
```

保留简单格式：

```json
"Text": "GetDisplayText"
```

简单格式等价于 `Kind: "Function"`。

### 5.2 Property binding

推荐格式：

```json
{
  "Kind": "Property",
  "Property": "DisplayText"
}
```

扩展路径格式：

```json
{
  "Kind": "Property",
  "SourcePath": ["ViewModel", "DisplayText"]
}
```

规则：

- `Property` 是 `SourcePath` 的简写，等价于单段 path。
- `SourcePath` 支持 property / pure function chain，但实现必须通过 UE 反射解析每一段，而不是字符串拼接。
- 提取时优先输出 `SourcePath`；如果只有单段 property，可输出简写 `Property` 以保持 schema 友好。
- `MemberGuid` 不作为用户必填输入；生成器应在能解析到 reflected member 时自动填充。
- 如果输入显式提供 `MemberGuid`，只能作为 extract round-trip metadata 使用；实现仍必须用当前 class 反射重新验证名称和路径。

## 6. C++ 设计

### 6.1 Binding 归一化模型

在 `WidgetBlueprintGenerator.cpp` 中引入小型内部结构，先把 JSON 解析为统一模型：

```cpp
struct FWidgetBindingSpec
{
	FString WidgetName;
	FName TargetProperty;
	EBindingKind Kind;
	FString FunctionName;
	TArray<FString> SourcePathSegments;
	FString SourceProperty;
};
```

职责：

- 从 widget-level `Bindings` 解析时补入当前 widget name。
- 从顶层 `Bindings` 解析时拆出 widget name 和 target property。
- 在写入 `Blueprint->Bindings` 前统一校验。

### 6.2 解析目标 widget 与 delegate

添加 helper：

```cpp
UWidget* ResolveBindingTargetWidget(UWidgetBlueprint* Blueprint, const FString& WidgetName);
FDelegateProperty* ResolveBindingDelegateProperty(UWidget* Widget, FName TargetProperty, bool& bIsPropertyDelegate);
```

解析规则：

- 目标 widget 必须存在。
- property binding 优先查找 `TargetProperty + "Delegate"`。
- event binding 可查找同名 delegate，但本 spec 主要面向 property bindings。
- 不根据具体 widget class 写 switch；全部通过反射查找 `FDelegateProperty`。

### 6.3 Property binding 构造

添加 helper：

```cpp
bool BuildPropertyBinding(
	UWidgetBlueprint* Blueprint,
	const FWidgetBindingSpec& Spec,
	FDelegateProperty* TargetDelegate,
	FDelegateEditorBinding& OutBinding,
	FText& OutError);
```

行为：

- 以 `Blueprint->SkeletonGeneratedClass` / `GeneratedClass` / `ParentClass` 为起点解析 `SourcePath`。
- 对每段 property / function 使用 UE 反射获得 `FFieldVariant` chain。
- 用 `FEditorPropertyPath(FieldChain)` 填 `OutBinding.SourcePath`。
- 单段 property 同时填 `SourceProperty`，保持编辑器显示和 legacy 简写兼容。
- 通过 `SourcePath.Validate(TargetDelegate, OutError)` 或等价规则验证类型兼容。
- 为最后一个 resolved member 填 `MemberGuid`；找不到 GUID 时允许 native member 继续使用名称，但必须通过反射解析成功。

### 6.4 Function binding 构造

添加 helper：

```cpp
bool BuildFunctionBinding(
	UWidgetBlueprint* Blueprint,
	const FWidgetBindingSpec& Spec,
	FDelegateProperty* TargetDelegate,
	bool bIsPropertyDelegate,
	FDelegateEditorBinding& OutBinding,
	FText& OutError);
```

行为：

- 在 `GeneratedClass` / `SkeletonGeneratedClass` / `ParentClass` / Blueprint function graphs 中解析函数。
- 函数必须存在；不存在时失败。
- 函数签名必须与 target delegate 兼容，或满足 UE binder 支持路径。
- 如果目标是 property delegate，函数必须 `FUNC_Const` 或 `FUNC_BlueprintPure`。
- 对 Blueprint function graph 尽量填 `MemberGuid`，以支持重命名稳定性。

### 6.5 编译结果检查

`FKismetEditorUtilities::CompileBlueprint(Blueprint)` 后应检查编译结果。如果 binding validation 失败，generator 应返回失败结果，而不是保存不稳定资产。

实现计划需要确认项目现有生成器如何表达失败结果和日志收集；保持现有 `FGenerationResult::MakeFailed` 风格。

## 7. Extractor 设计

`ExtractWidgetTree` 当前递归输出 widget node。需要让 extractor 能把 `Blueprint->Bindings` 映射回对应 widget node：

1. 在 `Extract` 开始时按 `ObjectName` 建立 binding map。
2. 调用递归 `ExtractWidgetTree` 时传入 binding map，或新增内部 helper。
3. 每个 widget node 输出自己的 `Bindings`。
4. 输出 property binding 时保留 `Kind`，并根据 `SourcePath` 选择 `Property` 或 `SourcePath`。
5. 输出 function binding 时输出 `Function` 和 `Kind`。

如果遇到 binding 的 `ObjectName` 找不到对应 widget：

- 不把它静默丢掉。
- 在顶层输出 `UnresolvedBindings` 或记录 warning；具体字段名在 implementation plan 中固定。
- 正常资产不应出现该字段。

## 8. Schema 与文档

更新 `MCP/schemas/WidgetBlueprint.md`：

- 顶层字段表新增 `Bindings`，标记为 compatibility input，不推荐作为新 authoring 格式。
- `Bindings Format` 分成：
  - Widget-level canonical format
  - Top-level compatibility format
  - Extract round-trip behavior
- 明确 property binding 支持 `Property` 简写和 `SourcePath`。
- 明确 function binding 的签名和 pure/const 要求。
- 明确 binding 会自动 `IsVariable: true`。
- 明确无效 binding 应失败并返回具体错误，而不是 silent warning。

如果其他 schema hint 或 prompt 文档引用 Widget binding，也应同步更新。

## 9. 验证方案

### 9.1 UBT 编译

使用项目推荐命令：

```bash
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### 9.2 MCP 自动化验证

使用 MCP 工具创建和提取测试资产：

1. 创建 `WBP_BindingRoundTrip_Function`，父类使用已有 `TestUserWidget`。
2. 为 `TextBlock.Text` 绑定 `GetDisplayText`。
3. 提取资产，确认 binding 位于 `RootWidget.Children[].Bindings.Text`。
4. 用提取 JSON 重新生成新资产。
5. 再次提取，确认 binding 语义保持一致。

Property binding 场景：

1. 创建 `WBP_BindingRoundTrip_Property`。
2. 将 `TextBlock.Text` 绑定到 `DisplayText` property。
3. 通过提取确认输出 `Kind: "Property"`，并保留 `Property` 或等价 `SourcePath`。
4. 重新生成后确认 UE asset 中 `FDelegateEditorBinding.SourcePath` 有效。

兼容顶层输入场景：

1. 用顶层 `Bindings: { "MyText.Text": ... }` 创建或更新资产。
2. 确认生成成功。
3. 提取时确认输出回 widget-level canonical format。

无效配置场景：

- 目标 widget 不存在时失败。
- 目标 property 没有可绑定 delegate 时失败。
- 函数不存在时失败。
- 函数返回类型不匹配时失败。
- property delegate 绑定到非 pure/const 函数时失败。

### 9.3 编辑器 sanity check

在编辑器中打开生成的 WidgetBlueprint：

- Details 面板显示绑定存在。
- 编译 WidgetBlueprint 不报 binding signature / pure / target missing 错误。
- 保存并重新提取后 binding 仍存在。

## 10. 风险与缓解

### SourcePath 构造依赖 editor-only 类型

`FEditorPropertyPath` 来自 UMGEditor。当前 generator 已运行在编辑器插件上下文中，可以使用该类型，但实现必须保持 module dependency 明确，不引入 runtime-only 构建问题。

### Blueprint graph function 与 native function GUID 差异

native function / property 可能没有稳定 GUID。实现应把 GUID 作为增强字段，而不是唯一定位手段；名称解析仍必须通过反射校验。

### 兼容顶层输入的冲突规则

同时出现 widget-level 和 top-level binding 时需要确定优先级。推荐 widget-level 优先，因为它是 canonical authoring format，且更贴近 widget tree 所属关系。

### 编译失败信息收集

如果现有 compile helper 不暴露详细 `FCompilerResultsLog`，实现可能需要补一个局部收集路径。计划阶段应先确认可用 API，再决定最小侵入实现。

## 11. 推荐实现顺序

1. 为当前不闭环行为补 failing test / MCP verification script。
2. 引入 binding spec 解析层，统一 widget-level 与顶层 compatibility 输入。
3. 实现目标 widget / delegate 反射解析。
4. 实现 function binding 严格校验。
5. 实现 property binding `SourcePath` / `MemberGuid` 构造。
6. 改造 extractor，把 binding 写回 widget node。
7. 更新 `MCP/schemas/WidgetBlueprint.md`。
8. 跑 UBT 编译和 MCP round-trip 验证。

## 12. 自审记录

- 本 spec 聚焦 WidgetBlueprint binding，不扩展无关 widget tree 功能。
- 已明确当前 `SourceProperty` 不是完全无效，但不足以作为长期正确实现。
- 已给出 Extract -> Generate -> Extract 的 canonical 闭环格式。
- 已保留顶层 `Bindings` 兼容读取，避免破坏已有提取 JSON 或外部调用方。
- 已要求动态反射解析，避免按 widget 类型或属性名硬编码。
- 未包含 production code 变更；下一步需要写 implementation plan 后再实现。
