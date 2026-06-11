# UE 资产分类与统一生成框架梳理

**日期：** 2026-06-11  
**状态：** 研究梳理  
**目标：** 将 UE 中各种资产按生成机制、数据结构和可统一程度分类，为后续 `AssetDocument` / `GenericAsset` 框架设计提供依据。

---

## 1. 背景

当前 AssetFactory 的生成方式主要是按资产类型扩展 generator：

- `DataAssetGenerator`
- `BlueprintGenerator`
- `WidgetBlueprintGenerator`
- `MaterialGenerator`
- `StateTreeGenerator`
- `BehaviorTreeGenerator`
- 动画相关 generator 规划

这种方式能快速覆盖单个资产类型，但随着 `AnimationBlueprint`、`Material`、`Niagara`、`AnimMontage` 等复杂资产进入范围，每个 generator 都会重复处理类似问题：

- 资产创建、加载、更新、保存；
- factory 查找与初始化；
- 普通反射属性设置；
- 子对象、数组、结构化数据修改；
- graph/source block 解析；
- preflight、rollback、diagnostics；
- compile / rebuild / post-edit；
- extract / diff / round-trip；
- MCP schema 暴露。

因此需要先按资产结构分类，明确哪些部分可以用通用框架覆盖，哪些部分需要领域 adapter。

---

## 2. 分类维度

建议不要只按 Content Browser 中的资产类型名称分类，而是按以下维度分类：

| 维度 | 问题 | 对框架的影响 |
| --- | --- | --- |
| 创建方式 | 能否直接 `NewObject`？是否需要 factory？ | 决定 `LifecycleAdapter` |
| 数据形态 | 是否只是 UPROPERTY？是否有 tree / graph / import payload？ | 决定 `ContentAdapter` |
| 修改方式 | 能否直接反射设置？是否需要专用 editor API？ | 决定 patch 策略 |
| 编译/重建 | 是否需要 compile、rebuild、resample、refresh？ | 决定 post-apply hook |
| 提取方式 | 能否反射 diff？是否需要 canonical IR？ | 决定 extract 策略 |
| 失败回滚 | 修改是否会部分污染资产？ | 决定 transaction / preflight |

---

## 3. 资产分类

### 3.1 纯数据资产

#### 特点

- 核心内容基本都是 `UPROPERTY`；
- 没有复杂 graph；
- 没有大型二进制 payload；
- 通常不需要 compile；
- 创建后设置属性并保存即可。

#### 例子

- `UDataAsset` / `UPrimaryDataAsset` 子类；
- 自定义配置 UObject asset；
- `CurveFloat` / `CurveVector` / `CurveLinearColor`；
- `DataTable`；
- `BlackboardData`；
- `InputAction`；
- `InputMappingContext`；
- Gameplay 配置类 asset；
- compression/settings/config asset。

#### 统一方式

```text
Class
+ Factory(optional)
+ Properties
+ Save
```

#### 适配策略

这类资产最适合第一版 `GenericAsset`：

- 通过 `Class` 动态加载资产类；
- 可选通过 `FactoryClass` 创建；
- 使用 `PropertySetterUtils` 设置属性；
- 使用 `ExtractPropertiesToJson` 做 diff-only 提取；
- 拒绝 abstract class、transient-only class、无法保存的对象类型。

#### 示例

```json
{
  "AssetType": "GenericAsset",
  "Name": "DA_EnemyConfig",
  "Path": "/Game/Data",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Action": "CreateOrUpdate",
  "Properties": {
    "Health": 100,
    "MoveSpeed": 450.0,
    "DisplayName": "Enemy"
  }
}
```

---

### 3.2 结构化数据资产

#### 特点

- 不是 graph 资产；
- 核心内容不是简单平铺属性；
- 通常包含 tracks、sections、samples、entries、rows、channels 等结构；
- 部分字段可以反射设置；
- 关键结构通常需要专用 API 或语义化 block；
- 修改后可能需要 validate、refresh、rebuild 或 resample。

#### 例子

- `AnimMontage`；
- `AnimComposite`；
- `BlendSpace` / `AimOffset`；
- `PoseAsset`；
- `MirrorDataTable`；
- `LevelSequence`；
- `ChooserTable`；
- `PoseSearchSchema` / `PoseSearchDatabase`；
- 部分 `PhysicsAsset`。

#### 统一方式

```text
LifecycleAdapter
+ Properties
+ StructuredBlocks
+ Validate
+ PostApply / Rebuild
+ Save
```

#### 适配策略

这类资产应该进入统一框架，但不能只靠通用属性 patch。

框架应提供：

- 通用 lifecycle；
- 通用 `Properties` patch；
- 资产专用 `StructuredBlockAdapter`；
- post-apply hook；
- extract hook。

每个资产的专用代码应尽量薄，只处理普通反射无法表达的结构。

#### AnimMontage 示例

`AnimMontage` 适合作为第二阶段验证对象。

它不是图资产，但也不是纯数据资产。它需要处理：

- montage factory；
- skeleton / preview mesh / source animation；
- slots；
- sections；
- segments；
- notifies；
- branching points；
- montage rebuild / validation。

可能的文档形状：

```json
{
  "AssetType": "AssetDocument",
  "Name": "AM_Attack",
  "Path": "/Game/Animation",
  "Class": "/Script/Engine.AnimMontage",
  "Factory": {
    "Class": "/Script/UnrealEd.AnimMontageFactory",
    "Properties": {
      "TargetSkeleton": "/Game/Characters/SK_Enemy_Skeleton.SK_Enemy_Skeleton",
      "SourceAnimation": "/Game/Animation/A_Attack.A_Attack"
    }
  },
  "Properties": {
    "BlendIn": {
      "BlendTime": 0.2
    }
  },
  "StructuredBlocks": [
    {
      "Name": "Montage",
      "Sections": [
        { "Name": "Start", "StartTime": 0.0 },
        { "Name": "Hit", "StartTime": 0.35 },
        { "Name": "End", "StartTime": 0.7 }
      ],
      "Slots": [
        {
          "Name": "DefaultSlot",
          "Segments": [
            {
              "Animation": "/Game/Animation/A_Attack.A_Attack",
              "StartTime": 0.0,
              "EndTime": 1.0,
              "PlayRate": 1.0
            }
          ]
        }
      ]
    }
  ]
}
```

#### 结论

`AnimMontage` 可以用这个统一思路做，但需要一个薄的 montage adapter。它适合验证框架能覆盖“非纯数据但非图”的资产。

---

### 3.3 类生成型资产

#### 特点

- 会生成 `GeneratedClass`；
- 有 CDO 默认值；
- 资产本体和生成出来的 class 是两层；
- 通常需要 compile；
- 默认值可能要写入 CDO、component template 或 generated class；
- 图逻辑可以作为可选 domain block。

#### 例子

- 普通 `Blueprint`；
- `WidgetBlueprint`；
- `AnimationBlueprint`；
- `ControlRigBlueprint`；
- GameplayAbility Blueprint；
- GameplayEffect Blueprint；
- BTTask / BTService / BTDecorator Blueprint；
- AnimLayerInterface。

#### 统一方式

```text
BlueprintLifecycleAdapter
+ Variables
+ Interfaces
+ Components / Subobjects
+ CDO DefaultProperties
+ Optional Domain Blocks
+ Compile
+ Save
```

#### 适配策略

这类资产不应被当成普通 UObject 直接 patch。

统一框架可以覆盖：

- parent class；
- interfaces；
- variables；
- component templates；
- CDO default properties；
- compile/save；
- extract diff。

但图逻辑要交给 domain adapter：

- Blueprint EventGraph / FunctionGraph -> `BSLFragment`；
- AnimationBlueprint AnimGraph -> `AnimGraphDSL`；
- AnimationBlueprint StateMachine -> `AnimStateMachineDSL`；
- ControlRig graph -> 后续 RigVM DSL / IR。

#### 示例

```json
{
  "AssetType": "AssetDocument",
  "Name": "BP_Enemy",
  "Path": "/Game/Blueprints",
  "Class": "/Script/Engine.Blueprint",
  "ParentClass": "/Script/Engine.Character",
  "Variables": [
    { "Name": "Health", "Type": "Float", "DefaultValue": "100.0" }
  ],
  "DefaultProperties": {
    "MaxWalkSpeed": 450.0
  },
  "Domains": [
    {
      "Name": "BlueprintGraph",
      "Language": "BSLFragment",
      "Graph": "EventGraph",
      "Source": "event BeginPlay { PrintString(\"Enemy ready\") }"
    }
  ]
}
```

---

### 3.4 树型资产

#### 特点

- 内容是 tree，而不是平铺属性或 graph；
- 节点有 parent / children；
- 每个节点仍然可以用反射设置属性；
- 常常和 Blueprint lifecycle 绑定；
- 可能需要 compile 或 rebuild。

#### 例子

- `WidgetBlueprint` 的 WidgetTree；
- UMG widget hierarchy；
- widget slot；
- widget style；
- widget bindings；
- widget animations 的部分结构。

#### 统一方式

```text
LifecycleAdapter
+ TreeDomainAdapter
+ Node Properties
+ Slot Properties
+ Bindings
+ Compile / Save
```

#### 适配策略

Widget 适合接入统一框架，因为已有动态化基础：

- slot 属性可以通过反射设置；
- style 属性可以通过反射设置；
- widget class 可以动态查找；
- 单子容器可通过函数检测。

未来可以把 WidgetBlueprint 表达成：

```json
{
  "AssetType": "AssetDocument",
  "Name": "WBP_StatusPanel",
  "Path": "/Game/UI",
  "Class": "/Script/UMGEditor.WidgetBlueprint",
  "Domains": [
    {
      "Name": "WidgetTree",
      "Language": "WidgetTreeJson",
      "Tree": {
        "Type": "CanvasPanel",
        "Name": "Root",
        "Children": [
          {
            "Type": "TextBlock",
            "Name": "HealthText",
            "Properties": {
              "Text": "100"
            }
          }
        ]
      }
    },
    {
      "Name": "BlueprintGraph",
      "Language": "BSLFragment",
      "Graph": "EventGraph",
      "Source": "event Construct { }"
    }
  ]
}
```

---

### 3.5 图资产

#### 特点

- 有 nodes、pins、edges、subgraphs；
- 通常需要 compile / rebuild；
- 直接暴露 UE pin-level JSON 不稳定；
- 更适合用领域脚本语言或 canonical graph IR；
- 需要 raw escape hatch 兜底。

#### 例子

- `Material`；
- `NiagaraSystem` / `NiagaraEmitter`；
- Blueprint EventGraph / FunctionGraph；
- AnimationBlueprint AnimGraph / StateMachine；
- `BehaviorTree`；
- `StateTree`；
- `ControlRig` RigVM graph；
- `DeformerGraph`；
- `Metasound`。

#### 统一方式

```text
LifecycleAdapter
+ Domain Source Block
+ Parser
+ Canonical IR
+ Semantic Validation
+ GraphBuilder
+ Compile / Rebuild
+ Extract
```

#### 适配策略

图资产应该共享管线，而不是共享同一种图语义。

例如：

```json
{
  "Domains": [
    {
      "Name": "MaterialGraph",
      "Language": "MaterialDSL",
      "Source": "color = param_color(\"Tint\", #66ccff)\noutput { EmissiveColor = color }"
    },
    {
      "Name": "BlueprintGraph",
      "Language": "BSLFragment",
      "Graph": "EventGraph",
      "Source": "event BeginPlay { }"
    }
  ]
}
```

每个 domain adapter 负责：

- 解析自己的语言；
- 输出 canonical IR；
- 做语义验证；
- 使用 UE lifecycle/schema/editor API 构图；
- 提取为 canonical IR 或可读 DSL。

#### Graph Domain Adapter 例子

| Domain | Language | Canonical IR | Builder |
| --- | --- | --- | --- |
| BlueprintGraph | `BSLFragment` | Blueprint graph AST / JSON | K2 graph writer |
| MaterialGraph | `MaterialDSL` | Material expression graph IR | Material expression builder |
| AnimGraph | `AnimGraphDSL` | Pose graph IR | Animation graph builder |
| AnimStateMachine | `AnimStateMachineDSL` | State machine IR | Animation state machine builder |
| NiagaraGraph | `NiagaraDSL` | Niagara stack / graph IR | Niagara editor API adapter |
| StateTree | JSON / future DSL | StateTree spec structs | StateTree builder |

---

### 3.6 导入型 / 资源型资产

#### 特点

- 核心数据来自外部文件；
- 有大型二进制 payload；
- 应该走 UE import / reimport pipeline；
- JSON 不适合手写完整内容；
- 适合描述 import source、import options 和 post-import patch。

#### 例子

- `Texture2D`；
- `StaticMesh`；
- `SkeletalMesh`；
- `SoundWave`；
- `AnimSequence`；
- Font；
- Media assets；
- Groom；
- Alembic；
- USD 相关资产。

#### 统一方式

```text
ImportSource
+ ImportOptions
+ Import / Reimport
+ PostImportProperties
+ MetadataPatch
+ Save
```

#### 适配策略

导入型资产不应伪装成普通 JSON 手写资产。

例如 `AnimSequence`：

- 不建议从 JSON 手写完整 raw/compressed bone tracks；
- 可以支持 source file import；
- 可以 patch notifies；
- 可以 patch curves；
- 可以 patch sync markers；
- 可以 patch metadata；
- 可以验证 skeleton compatibility。

#### 示例

```json
{
  "AssetType": "AssetDocument",
  "Name": "A_Attack",
  "Path": "/Game/Animation",
  "Import": {
    "Source": "D:/Assets/Attack.fbx",
    "FactoryClass": "/Script/UnrealEd.FbxFactory",
    "Options": {
      "Skeleton": "/Game/Characters/SK_Enemy_Skeleton.SK_Enemy_Skeleton",
      "ImportAnimations": true
    }
  },
  "PostImport": {
    "Properties": {
      "RateScale": 1.0
    },
    "Notifies": [
      {
        "Name": "Hit",
        "Time": 0.35
      }
    ]
  }
}
```

---

### 3.7 世界 / 关卡型资产

#### 特点

- 内容是 World / Level / Actor graph；
- 强依赖 editor world 状态；
- 涉及 streaming、partition、actor placement；
- 保存和加载语义特殊；
- 不适合放进第一版 `GenericAsset`。

#### 例子

- `World`；
- `Level`；
- World Partition 数据；
- DataLayer asset；
- Packed Level Actor；
- MapBuildDataRegistry。

#### 统一方式

```text
WorldDocument
+ LevelPatch
+ ActorPlacement
+ ActorProperties
+ SaveWorld
```

#### 适配策略

建议后续单独设计 `WorldDocument` / `LevelPatch`，不要阻塞资产生成框架第一版。

---

## 4. 统一框架草案

### 4.1 顶层结构

```text
AssetDocument
  Metadata
  Lifecycle
  Properties
  StructuredBlocks
  TreeBlocks
  Domains
  Import
  PostProcess
```

### 4.2 模块分层

```text
AssetDocumentCompiler
  |
  |-- AssetDocumentParser
  |-- AssetLifecycleAdapterRegistry
  |     |-- DefaultObjectAssetLifecycle
  |     |-- BlueprintLifecycle
  |     |-- WidgetBlueprintLifecycle
  |     |-- MaterialLifecycle
  |     |-- ImportLifecycle
  |
  |-- ContentAdapterRegistry
  |     |-- PropertyPatchAdapter
  |     |-- StructuredBlockAdapter
  |     |-- TreeDomainAdapter
  |     |-- GraphDomainAdapter
  |     |-- ImportAdapter
  |
  |-- Diagnostics
  |-- Preflight
  |-- Transaction / Rollback
  |-- Extract
```

### 4.3 统一执行流程

```text
1. Parse AssetDocument
2. Resolve class / factory / target asset
3. Select LifecycleAdapter
4. Preflight:
   - class exists
   - factory exists
   - dependencies load
   - properties valid
   - structured blocks valid
5. Create or load asset
6. Apply:
   - properties
   - structured blocks
   - tree blocks
   - graph domains
   - import patches
7. PostApply:
   - validate
   - rebuild
   - compile
   - resample
8. Save
9. Extract / return diagnostics
```

---

## 5. Adapter 设计原则

### 5.1 LifecycleAdapter

负责资产生命周期：

```text
CanHandle(Document)
Create(Document)
Load(Document)
PreApply(Asset, Document)
PostApply(Asset, Document)
CompileOrRebuild(Asset, Document)
Save(Asset, Document)
Extract(Asset)
```

示例：

- `DefaultObjectAssetLifecycle`
- `BlueprintLifecycleAdapter`
- `MaterialLifecycleAdapter`
- `AnimMontageLifecycleAdapter`
- `ImportLifecycleAdapter`

### 5.2 PropertyPatchAdapter

负责通用反射属性：

```text
Properties -> FPropertySetterUtils
```

要求：

- 支持 typed property format；
- 支持 nested structs；
- 支持 arrays/maps/sets；
- 支持 object/class/soft references；
- 支持 diff-only extract；
- 设置前尽量 preflight，避免半写入。

### 5.3 StructuredBlockAdapter

负责非图结构：

```text
StructuredBlocks[] -> asset-specific structured changes
```

适合：

- montage sections；
- blendspace samples；
- level sequence tracks；
- chooser rows；
- pose search channels。

### 5.4 GraphDomainAdapter

负责图语言：

```text
Language + Source -> Canonical IR -> GraphBuilder
```

要求：

- 不直接暴露 UE pin-level JSON 作为主要创作契约；
- 支持 raw escape hatch；
- parser error 必须包含 block path、line、column；
- builder 必须使用 UE lifecycle/schema/editor API；
- extract 优先输出 canonical IR。

### 5.5 ImportAdapter

负责导入型资产：

```text
Source file + options -> import/reimport -> post-import patch
```

要求：

- 不手写大型二进制 payload；
- import options 可序列化；
- post-import patch 复用 `PropertyPatchAdapter`；
- import failure 不应污染目标资产。

---

## 6. 与现有 generator 的关系

### 6.1 短期

保留现有 generator，不立即替换。

第一版新增 `GenericAsset` / `AssetDocument`，用于验证通用能力。

### 6.2 中期

把简单 generator 逐步改薄：

- `DataAssetGenerator` 可以变成 `GenericAsset` 的 thin wrapper；
- `Curve*` 可考虑走通用属性 + 小 adapter；
- `InputAction` / `InputMappingContext` 可评估是否降级为 structured adapter；
- `BlueprintGenerator` 的 CDO/default properties 可以复用统一 patch engine。

### 6.3 长期

复杂 generator 变成 lifecycle + domain adapter：

- `WidgetBlueprintGenerator` -> Blueprint lifecycle + WidgetTree adapter；
- `MaterialGenerator` -> Material lifecycle + MaterialGraph adapter；
- `AnimationBlueprintGenerator` -> Blueprint lifecycle + AnimGraph/StateMachine/BSL adapters；
- `NiagaraGenerator` -> Niagara lifecycle + Niagara domain adapter。

---

## 7. 推荐落地顺序

### V1: Generic Data Asset

目标：证明没有专用 generator 的资产也能通过通用机制创建、更新、提取。

范围：

- `AssetType: "GenericAsset"`；
- `Class` 动态加载；
- 可选 `FactoryClass` 字段先预留；
- `Properties` patch；
- `Create` / `Update` / `CreateOrUpdate`；
- diff-only extract；
- negative validation。

验收：

- 创建一个当前没有专用 generator 的 `UDataAsset` 子类；
- 更新已有资产的单个属性；
- 提取只包含相对默认值变化；
- 无效 class、abstract class、属性不存在、类型不匹配时返回可读错误；
- 不引入每类资产的硬编码 switch。

---

### V2: Structured Asset

目标：证明框架可以覆盖非纯数据但非图资产。

推荐对象：

- `AnimMontage`；
- 或 `BlendSpace`。

范围：

- lifecycle adapter；
- factory properties；
- structured block；
- post-apply validate/rebuild；
- extract structured block。

验收：

- 生成可打开的 `AnimMontage` 或 `BlendSpace`；
- 普通属性走 `Properties`；
- 专用结构走 `StructuredBlocks`；
- 无效 animation/skeleton 时 preflight 失败。

---

### V3: Class Asset

目标：把 Blueprint 默认值能力接入统一框架。

范围：

- Blueprint lifecycle；
- variables；
- interfaces；
- CDO `DefaultProperties`；
- component template properties；
- compile/save。

验收：

- 创建普通 Blueprint；
- 更新 CDO 默认值；
- component template 属性可反射设置；
- extract 可返回稳定 diff。

---

### V4: Tree / Simple Graph

目标：接入 WidgetTree、BSL、MaterialGraph。

范围：

- `Domains[]`；
- `Language + Source` block；
- canonical IR；
- diagnostics；
- graph/tree builder。

验收：

- WidgetTree 可通过 domain block 生成；
- BSLFragment 可写入 EventGraph；
- MaterialDSL 可生成最小材质图。

---

### V5: Complex Graph Assets

目标：接入 ABP、Niagara、ControlRig。

范围：

- AnimGraphDSL；
- AnimStateMachineDSL；
- NiagaraDSL / canonical stack IR；
- ControlRig lifecycle；
- raw node escape hatch；
- robust compile diagnostics。

验收：

- ABP 不再作为独立大 generator 从零实现；
- 复用同一套 `Domains[]`、diagnostics、preflight、extract；
- 新 graph domain 只需要实现自己的 adapter。

---

## 8. 对 AnimMontage 的判断

`AnimMontage` 可以用这个思路做，而且是很好的第二阶段验证对象。

它的定位：

```text
结构化数据资产
而不是纯数据资产
也不是图资产
```

通用框架能覆盖：

- class/factory 创建；
- dependency loading；
- 普通属性 patch；
- save；
- diagnostics；
- extract；
- MCP 入口。

需要 montage adapter 覆盖：

- skeleton/source animation 初始化；
- slot track；
- composite sections；
- animation segments；
- notifies；
- branching points；
- montage rebuild / validation。

因此 `AnimMontage` 不应该回到“完整独立 generator”的老路，而应该作为：

```text
AssetDocument + AnimMontageLifecycleAdapter + MontageStructuredBlockAdapter
```

---

## 9. 初步结论

统一框架不应该试图用一种格式直接表达所有 UE 资产细节。

更合理的目标是：

```text
统一资产生命周期
统一属性 patch
统一 source block / domain adapter 管线
统一 diagnostics
统一 extract / diff / round-trip
```

不同资产的差异保留在轻量 adapter 中：

- 纯数据资产：几乎不需要 adapter；
- 结构化资产：需要 thin structured adapter；
- 类生成资产：需要 lifecycle adapter；
- 树型资产：需要 tree adapter；
- 图资产：需要 domain graph adapter；
- 导入资产：需要 import adapter；
- 世界资产：后续独立 world document。

第一版应从 `GenericAsset` 开始，不碰图资产。

推荐第一阶段目标：

```text
GenericAsset
+ 通用 Class 加载
+ 通用 UObject/DataAsset 创建
+ 通用 Properties patch
+ diff-only extract
+ negative validation
```

随后用 `AnimMontage` 或 `BlendSpace` 验证框架能自然扩展到结构化资产。

