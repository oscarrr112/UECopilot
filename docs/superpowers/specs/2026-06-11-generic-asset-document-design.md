# Generic Asset Document 设计

**日期**：2026-06-11  
**状态**：草稿（待用户审阅）  
**分支**：`feature/asset-document-generic-asset`  
**范围**：第一版通用 UObject/DataAsset 资产描述与属性 patch，不包含图资产、树资产、导入资产或结构化资产实现。

---

## 1. 背景

AssetFactory 当前主要按 `AssetType` 扩展独立 generator。这个模式已经覆盖了 `DataAsset`、`Blueprint`、`WidgetBlueprint`、`Material`、`StateTree`、`BehaviorTree` 等类型，但随着 `AnimationBlueprint`、`AnimMontage`、`Niagara`、`MaterialGraph` 这类资产进入规划，每个 generator 都会重复处理资产生命周期、属性设置、验证、保存、提取和 MCP 暴露。

本设计的目标是先建立一个较低层的通用资产描述入口，用来验证：

- 对于核心内容只是 reflected properties 的资产，不需要为每个资产类型写一个完整 generator；
- 通过动态 class loading 和现有 `PropertySetterUtils`，可以生成当前没有专门 generator 的数据资产；
- 后续的结构化资产、Blueprint、Widget、Material、Niagara、AnimationBlueprint 可以逐步作为薄 adapter 接入，而不是继续增长为巨大的独立 generator。

配套研究文档见：

- `docs/superpowers/research/2026-06-11-ue-asset-taxonomy-and-unified-framework.md`

---

## 2. 目标

第一版只实现 `GenericAsset`：

- 新增 `AssetType: "GenericAsset"`。
- 通过 `Class` 动态解析 UObject 类。
- 支持创建、更新、创建或更新。
- 支持普通 UObject/DataAsset asset 的 package 创建与保存。
- 支持 `Properties` 反射 patch，复用现有 `FPropertySetterUtils`。
- 支持 typed property format 和现有 untyped format。
- 支持提取为 generator-readable JSON。
- 支持 `diffOnly` 提取，只输出相对 CDO 或父默认值变化的属性。
- 验证一个当前没有专门 generator 的资产类也能通过 `GenericAsset` 生成。
- MCP 暴露 schema，让 agent 可以发现和使用这个通用入口。

---

## 3. 非目标

第一版明确不做：

- 不实现图资产生成，例如 MaterialGraph、NiagaraGraph、AnimGraph、Blueprint EventGraph。
- 不实现 WidgetTree。
- 不实现 `AnimMontage`、`BlendSpace`、`LevelSequence` 等结构化资产的专用 sections/tracks/samples block。
- 不实现 import/reimport pipeline。
- 不替换现有 generator。
- 不从 JSON 手写大型二进制 payload。
- 不增加按具体资产类名分支的大型 `if/else` 或 `switch/case`。
- 不尝试自动修改 UE CDO 并保持外部 sidecar 与 `.uasset` 持续热同步。

`FactoryClass` / `Factory` 字段可以在 schema 中作为后续扩展说明，但第一版实现可以不支持或只做显式拒绝，避免半成品语义。

---

## 4. 用户契约

### 4.1 最小创建

```json
{
  "AssetType": "GenericAsset",
  "Name": "DA_GenericEnemy",
  "Path": "/Game/Data",
  "Action": "CreateOrUpdate",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Properties": {
    "Health": 100,
    "MoveSpeed": 450.0
  }
}
```

### 4.2 typed properties

Typed property 的 `type` 只允许基础类型 token，不允许携带 subtype。也就是说，输入中不允许出现 `Object:StaticMesh`、`Class:Pawn`、`Struct:Foo`、`Enum:Bar` 这类写法。

需要目标类型时，生成器必须从目标 `FProperty` 反射推断：

- `FObjectPropertyBase` 提供期望 object class；
- `FClassProperty` / `FSoftClassProperty` 提供期望 base class；
- `FStructProperty` 提供 struct 类型；
- `FEnumProperty` / enum-backed `FByteProperty` 提供 enum 类型；
- `FArrayProperty` / `FMapProperty` / `FSetProperty` 提供容器元素类型。

这样 JSON 只表达“值”，不重复声明 UE 已经知道的 subtype，避免输入 schema 和实际 UPROPERTY 类型产生冲突。

```json
{
  "AssetType": "GenericAsset",
  "Name": "DA_TypedEnemy",
  "Path": "/Game/Data",
  "Class": "TestDataAsset",
  "Properties": {
    "Health": {
      "type": "Int",
      "value": 100
    },
    "DisplayName": {
      "type": "Text",
      "value": "Enemy"
    },
    "SpawnOffset": {
      "type": "FVector",
      "value": [10.0, 20.0, 30.0]
    }
  }
}
```

Object、Class、Enum 等值也使用无 subtype 的 `type`：

```json
{
  "Properties": {
    "Icon": {
      "type": "Object",
      "value": "/Game/UI/T_Icon.T_Icon"
    },
    "EnemyClass": {
      "type": "Class",
      "value": "/Game/Blueprints/BP_Enemy.BP_Enemy_C"
    },
    "CollisionChannel": {
      "type": "Enum",
      "value": "ECC_Pawn"
    }
  }
}
```

这些值是否合法，必须由目标属性的反射类型决定，而不是由 `type` 字符串里的 subtype 决定。

### 4.3 更新已有资产

```json
{
  "AssetType": "GenericAsset",
  "Name": "DA_GenericEnemy",
  "Path": "/Game/Data",
  "Action": "Update",
  "Properties": {
    "MoveSpeed": 600.0
  }
}
```

更新时 `Class` 可选。如果提供 `Class`，生成器应验证它与已有资产 class 兼容；如果不提供，则使用已有资产 class。

### 4.4 提取

`extract_assets` 对 GenericAsset 支持：

```json
{
  "Class": "TestDataAsset",
  "Properties": {
    "Health": 100,
    "MoveSpeed": 450.0
  }
}
```

如果 `diffOnly = true`，只输出相对 class CDO 不同的 editable properties。

---

## 5. 设计

### 5.1 Generator 位置

新增 C++ generator：

```text
Source/AssetFactory/Public/Generators/GenericAssetGenerator.h
Source/AssetFactory/Private/Generators/GenericAssetGenerator.cpp
```

注册方式沿用现有 `AssetGeneratorRegistry`：

```text
GetAssetType() -> "GenericAsset"
```

短期保留现有 `DataAssetGenerator`。`GenericAsset` 是新入口，不改变现有 `DataAsset` 行为。

### 5.2 创建策略

第一版只支持直接创建 UObject asset：

```text
CreatePackage(Path / Name)
NewObject<UObject>(Package, ResolvedClass, Name, RF_Public | RF_Standalone)
FAssetRegistryModule::AssetCreated(Asset)
Apply Properties
SavePackage
```

创建前必须验证：

- `Class` 非空；
- class 能通过动态方式解析；
- class 不是 abstract；
- class 不是 deprecated/newer-version-only 之类明显不可实例化类型；
- class 是 `UObject` 子类；
- class 不是 `UClass`、`UPackage`、`UWorld` 等第一版明确排除的类型；
- class default object 可用；
- class 可以作为 asset 保存。

如果发现某些 UObject class 不能安全用 `NewObject` 作为资产创建，第一版应返回明确错误，而不是扩大硬编码例外列表。

### 5.3 Class 解析

解析顺序应优先复用项目已有动态查找工具：

```text
FClassFinderUtils
StaticLoadClass
FindObject / LoadClass fallback
```

允许输入：

- 完整 class path，例如 `/Script/AssetFactory.TestDataAsset`；
- Blueprint generated class path；
- 短类名，例如 `TestDataAsset`，使用现有 class finder 策略。

不允许为具体类维护静态白名单。

### 5.4 属性 patch

`Properties` 处理复用 `FPropertySetterUtils`：

- 如果属性值是 `{ "type": "...", "value": ... }`，使用 typed property path。
- 否则使用现有 untyped JSON path。
- 支持 nested struct、array、map、set、object reference、class reference、soft reference，取决于 `PropertySetterUtils` 当前能力。
- 第一版 typed `type` 不允许 subtype；`type` 中出现 `:` 应作为 validation error。
- Object、Class、Struct、Enum、Array、Map、Set 的具体目标类型必须从目标 `FProperty` 推断。
- 若现有 `PropertySetterUtils` typed API 要求 subtype，`GenericAsset` implementation plan 应增加一个 target-property-aware adapter，而不是把 subtype 暴露给用户。

第一版应先做 preflight：

```text
Duplicate transient object
Apply properties to duplicate
Only if duplicate succeeds, apply to real asset
```

这样可以减少半写入风险。若某些属性 setter 依赖真实 package/asset context，implementation plan 中需要记录并决定是否按属性类型跳过 duplicate preflight。

### 5.5 更新策略

`Create`：

- 资产已存在时 skipped。
- 资产不存在时创建。

`Update`：

- 资产不存在时 failed。
- 加载已有资产。
- 如果提供 `Class`，验证现有资产 `IsA(ResolvedClass)` 或 class 精确匹配。第一版推荐要求精确匹配，避免误把 parent class 当成可改 class。
- 只修改 JSON 中出现的属性。

`CreateOrUpdate`：

- 资产存在则按 update。
- 不存在则按 create。

### 5.6 保存策略

沿用现有 generator 风格：

```text
Asset->MarkPackageDirty()
Package->FullyLoad()
Package->SetDirtyFlag(true)
UPackage::SavePackage(...)
```

保存失败必须返回 `FGenerationResult::MakeFailed`。

### 5.7 提取策略

`CanExtract`：

- 第一版可提取 UObject asset；
- 排除已有专门 generator 明确处理的高风险类型，例如 `UBlueprint`、`UWidgetBlueprint`、`UMaterial`、`UWorld`；
- 不要用大规模类名列表维护排除项，优先按基类/资产形态排除。

`Extract`：

- 输出 `Class`；
- 输出 `Properties`；
- `diffOnly` 使用 `FPropertySetterUtils::ExtractPropertiesToJson(Asset, true, true)`；
- 可按需要过滤 `UObject` 基类噪声字段；
- 不提取 graph、tree、import 或 structured block。

如果某个资产已经有更专门 generator，应优先让专门 generator 提取，避免 `GenericAsset` 抢占。

---

## 6. 薄 adapter 原则

虽然第一版不实现结构化资产 adapter，但本 spec 明确后续方向。

### 6.1 目标

```text
通用框架负责资产生命周期和反射属性
薄 adapter 只处理反射无法安全保证语义一致的部分
```

例如 `AnimMontage`：

- `BlendIn`、普通布尔/数值字段可以走 `Properties`；
- sections、slots、segments、notifies 需要 montage structured adapter；
- adapter 负责调用 UE API、刷新、验证和 rebuild。

### 6.2 避免 if/else 扩散

后续 adapter 不应写成大型字段分支：

```cpp
if (Field == "Sections") { ... }
else if (Field == "Slots") { ... }
else if (Field == "Notifies") { ... }
```

推荐注册表驱动：

```cpp
Handlers.Add(TEXT("Sections"), &ApplySections);
Handlers.Add(TEXT("Slots"), &ApplySlots);
Handlers.Add(TEXT("Notifies"), &ApplyNotifies);
Handlers.Add(TEXT("BranchingPoints"), &ApplyBranchingPoints);
```

框架统一调度：

```text
for each structured block:
  adapter = Registry.Find(Block.Name)
  adapter.Apply(Block)
```

### 6.3 raw escape hatch

后续图/结构化领域可以保留 raw escape hatch：

```json
{
  "Kind": "RawObject",
  "Class": "/Script/SomeModule.SomeType",
  "Properties": {}
}
```

但第一版 `GenericAsset` 不需要实现它。

---

## 7. MCP 暴露

### 7.1 schema

新增：

```text
MCP/schemas/GenericAsset.md
```

schema 需要说明：

- 适合纯 UObject/DataAsset 风格资产；
- 不适合图、树、导入和结构化资产；
- `Class` 解析规则；
- `Properties` untyped 和 typed 格式；
- typed `type` 禁止 subtype，目标类型由 UPROPERTY 反射推断；
- create/update/create-or-update 行为；
- extract/diffOnly 行为；
- 常见错误。

### 7.2 tool 枚举

将 `GenericAsset` 添加到：

- `generate_assets` 描述；
- `get_generator_schema` enum；
- schema missing fallback available-types 文案；
- `MCP/dist` 构建产物。

如果 MCP 侧已有静态 asset type 列表散落，implementation plan 应优先收敛为单一常量。

---

## 8. 验证方案

### 8.1 C++ 单元/自动化测试

添加 focused tests 覆盖：

- 无效 class 失败；
- abstract class 失败；
- 创建 `TestDataAsset` 或新增测试 DataAsset 子类成功；
- 更新单个属性不覆盖未出现字段；
- typed property 成功；
- 类型不匹配失败且不保存半成品；
- extract diff-only 只包含修改字段。

如果现有 test framework 对 editor asset 创建成本较高，可以先加 smoke script，但 implementation plan 必须说明原因。

### 8.2 UBT

使用项目推荐 Development 编译：

```bash
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

如果在独立 worktree 中验证插件生效，需按项目约定使用临时 host project，避免主项目同名插件遮蔽 worktree 改动。

### 8.3 MCP / Editor smoke

使用 MCP `generate_assets`：

1. 创建一个没有专门 generator 的测试 DataAsset 子类。
2. 提取资产，验证 `Class` 和 `Properties`。
3. 更新单个属性。
4. 再次提取，验证未指定属性保持不变。
5. negative case 验证错误清晰。

### 8.4 文档验证

- `get_generator_schema(GenericAsset)` 返回 schema。
- schema 示例与 generator 行为一致。
- `git diff --check` 通过。

---

## 9. 风险与缓解

### 9.1 UObject class 不一定都是可保存 asset

风险：很多 UObject 类可实例化，但不适合作为 Content Browser asset。

缓解：

- 第一版明确拒绝已知高风险基类形态，例如 `UWorld`、`UBlueprint`、`UMaterial`；
- 创建失败时返回清晰错误；
- 不维护“支持类白名单”；
- 后续通过 lifecycle adapter 支持特殊资产。

### 9.2 反射 setter 可能半写入

风险：多个属性中前几个成功、后一个失败，导致资产部分改变。

缓解：

- 先在 transient duplicate 上 preflight；
- 失败时不保存真实资产；
- 真实 apply 失败时返回 failed，并避免保存。

### 9.3 GenericAsset 抢占专门 generator 的 extract

风险：`extract_assets` 对 `Blueprint`、`Material` 等资产误用 GenericAsset，输出低质量 JSON。

缓解：

- `CanExtract` 排除高风险形态；
- 保持 registry 提取优先级，让专门 generator 先匹配；
- `GenericAsset` 作为 fallback，优先级较低。

### 9.4 过早扩展成大平台

风险：第一版把 factory、structured blocks、graph、import 都塞进去，导致实现失焦。

缓解：

- 第一版只做通用数据资产；
- 其他能力只在文档中预留；
- 后续每类能力单独 spec 和 implementation plan。

---

## 10. 验收标准

- `GenericAsset` generator 注册成功。
- `generate_assets` 支持 `AssetType: "GenericAsset"`。
- `get_generator_schema(GenericAsset)` 可用。
- 能创建至少一个当前无专门 generator 的 UObject/DataAsset 风格测试资产。
- 能更新已有 GenericAsset 的单个属性。
- 能用 typed property 设置基础类型、文本、向量、对象引用、class 引用或 enum 中的至少三类。
- typed `type` 中出现 subtype 时返回可读 validation error，例如拒绝 `Object:StaticMesh`。
- Object/Class/Enum 等 typed values 根据目标属性反射类型验证，不依赖用户提供 subtype。
- 无效 class、abstract class、属性不存在、类型不匹配都返回可读错误。
- 失败的 property patch 不保存半写入资产。
- `extract_assets` 对 GenericAsset 返回 `Class` 和 `Properties`。
- `diffOnly` 提取只输出非默认 editable properties。
- 不引入按具体资产类名扩展行为的大型 switch/case。
- UBT Development 编译通过。
- 至少一个真实 Editor/MCP smoke 验证通过，或明确记录环境限制。

---

## 11. 后续路线

第一版完成后，建议按以下顺序扩展：

1. **Structured Asset Adapter Spec**：使用 `AnimMontage` 或 `BlendSpace` 验证薄 adapter。
2. **Blueprint Lifecycle Adapter Spec**：把 Blueprint CDO/default properties 接入通用框架。
3. **WidgetTree Domain Spec**：将 WidgetTree 作为 tree domain 接入。
4. **MaterialGraph Domain Spec**：新增 Material DSL -> canonical IR -> expression builder。
5. **AnimationBlueprint Domain Specs**：复用同一套 `Domains[]` 管线接入 AnimGraph、StateMachine 和 BSLFragment。

这些后续工作都不应修改第一版 `GenericAsset` 的核心契约，而应通过 lifecycle/content/domain adapter 增量扩展。

---

## 12. 自审记录

- 本 spec 第一版范围明确，不包含图资产和结构化资产实现。
- 已保留后续 thin adapter 路线，但没有把它放入第一版验收。
- 已明确动态 class loading 和反射 patch 是核心方向。
- 已明确避免大型 if/else、switch/case 和静态类型列表。
- 已明确 GenericAsset 不应抢占 Blueprint/Material/Widget 等专门 generator 的提取。
- 已包含 MCP、UBT、Editor smoke 和 negative validation 验收。
