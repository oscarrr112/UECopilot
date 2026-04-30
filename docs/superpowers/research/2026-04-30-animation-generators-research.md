# Animation Generators 研究记录

- 日期：2026-04-30
- 分支：`feature/animation-generators-specs`
- 范围：AnimationBlueprint、BlendSpace、AimOffset、AnimMontage 等动画资产 generator 规划，以及动画图创作语言研究
- 状态：用于规格规划的研究总结；本分支不包含实现

## 目标

下一组 generator 家族应覆盖动画创作资产，并保持对 agent 有用。目标不只是创建一个空的 `UAnimBlueprint`，而是最终生成一个 Animation Blueprint，使其能够表达姿势图、状态机、过渡规则和普通 Blueprint 逻辑，同时仍然使用现有 AssetFactory JSON 入口点。

脑暴阶段得出的关键设计决策是：

- 保持顶层 AssetFactory 输入为 JSON；
- 保持普通 Blueprint 变量与现有 `BlueprintGenerator` 契约兼容；
- 对 AnimGraph 和 StateMachine 创作使用动画专用源码块；
- 尽可能为普通 Blueprint/EventGraph/function 逻辑复用 BSL 概念；
- 在接触 UE 图之前，将所有图源码规范化为内部语义 IR。

## 当前项目基线

相关的现有 generator 模式：

- `IAssetGenerator` 仍是通用接口：`GetAssetType`、`Generate`、`ValidateConfig`、`CanExtract`、`Extract` 和优先级。
- curve 等简单 asset generator 使用带 factory 的 `AssetTools.CreateAsset`，然后标记脏数据并保存。
- `BlueprintGenerator` 已支持通用 Blueprint 变量和 CDO 默认属性。
- `StateTreeGenerator` 是复杂 generator 的最佳模式：生命周期代码保留在主 generator 中，而解析、构建、提取 helper 放在聚焦的子目录里。
- 现有 BSL 将语言解析与 UE 图创建分离：source -> parser -> AST/compiler -> graph writer。Animation graph 工作应保持这种分离。

重要的现有 Blueprint 变量契约：

```json
"Variables": [
  { "Name": "Speed", "Type": "Float", "DefaultValue": "0.0" },
  { "Name": "bIsInAir", "Type": "Bool", "DefaultValue": "false" }
]
```

`AnimationBlueprintGenerator` 应复用这个形状，而不是发明 `Default` 或第二套变量声明格式。提取结果可能会输出 `Boolean` 这样的引擎拼写；生成流程在它们映射到同一个 Blueprint pin/category 时，应同时接受 `Bool` 和 `Boolean` 作为别名。

## BlendSpace 和 AimOffset 发现

BlendSpace 资产应该有自己的 generator，并且独立于 AnimationBlueprint。它们是之后由 Animation Blueprint 引用的动画资产。

创建路径：

```cpp
UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
Factory->TargetSkeleton = Skeleton;
Factory->PreviewSkeletalMesh = PreviewMesh;

UBlendSpace* BlendSpace = Cast<UBlendSpace>(
    AssetTools.CreateAsset(Name, Path, UBlendSpace::StaticClass(), Factory)
);
```

Factory 变体：

- `UBlendSpaceFactoryNew` -> `UBlendSpace`
- `UBlendSpaceFactory1D` -> `UBlendSpace1D`
- `UAimOffsetBlendSpaceFactoryNew` -> `UAimOffsetBlendSpace`
- `UAimOffsetBlendSpaceFactory1D` -> `UAimOffsetBlendSpace1D`

有用的公开 API 和字段：

- `FBlendParameter` 描述坐标轴显示名、最小值/最大值、网格数量、吸附和循环。
- `FBlendSample` 存储 `UAnimSequence* Animation`、`SampleValue`、`RateScale` 和单帧设置。
- `UBlendSpace::AddSample(UAnimSequence*, FVector)` 添加样本。
- `UBlendSpace::ValidateSampleData()` 验证样本数据。
- `UBlendSpace::ResampleData()` 重建内部样本数据。

AimOffset 应与 BlendSpace 共享同一个 generator 和 JSON 形状，但需要更严格的验证。AimOffset 样本必须是 mesh-space rotation offset additive 动画。

推荐的依赖顺序：

```text
Skeleton / SkeletalMesh / AnimSequence -> BlendSpace / AimOffset -> AnimationBlueprint
```

## 广义动画资产 generator 调研

为了避免只规划 Animation Blueprint 和 BlendSpace 后再不断补洞，本轮额外搜索了 UE 5.7 本地源码中的 editor factories、animation editor plugins 和相关 asset classes。结论是：动画 generator 家族应覆盖“动画组合/选择/重定向/图逻辑”这几层，但不应把 DCC 导入类资产误设计成 JSON 手写资产。

### 应规划为一等 generator 的资产

| 资产 | 主要证据 | 规划结论 |
| --- | --- | --- |
| `AnimMontage` | `AnimMontageFactory` 支持 `TargetSkeleton`、`SourceAnimation`、`PreviewSkeletalMesh`，创建后设置 skeleton 和 preview mesh。 | 必须规划。它是 slot、section、notify、branching point 和 runtime playback 的核心资产，且会反向影响 AnimGraph 的 slot node 规划。 |
| `AnimComposite` | `AnimCompositeFactory` 可从 `SourceAnimation` 创建并填充 `AnimationTrack`。 | 必须规划。它比 Montage 简单，是 sequence 组合和时间线片段 generator 的低风险入口。 |
| `PoseAsset` | `PoseAssetFactory` 要求 `SourceAnimation`，调用 `CreatePoseFromAnimation` 并设置 skeleton。 | 必须规划。它服务 pose driver、pose library、facial/pose matching 等后续节点。 |
| `MirrorDataTable` | `MirrorDataTableFactory` 使用 skeleton、find/replace expressions 和 `FMirrorTableRow`。 | 必须规划。它是动画镜像、IK/retarget authoring 的基础数据资产。 |
| `IKRig` / `IKRetargeter` | `IKRigDefinitionFactory` 和 `IKRetargetFactory` 位于 IKRig editor plugin。 | 必须规划，但应放在核心组合资产之后。它们依赖 skeleton/preview mesh，契约会比 Montage 更结构化。 |
| `PoseSearchSchema` / `PoseSearchDatabase` | PoseSearch editor plugin 提供 schema/database factories。 | 应规划为后续 specs。它们是 motion matching / pose search 工作流的核心，通常引用大量 sequences 和 schema channels。 |
| `ChooserTable` | Chooser plugin 有 `ChooserFactory`。 | 应规划为后续 specs。它是 animation selection / gameplay-driven choice 的数据层，适合 agent 生成。 |
| `ControlRigBlueprint` | ControlRig editor plugin 提供 `ControlRigBlueprintFactory`，并创建 root graph。 | 应规划，但第一阶段只做 asset lifecycle 和基础 metadata；完整 RigVM graph authoring 风险较高，应另拆 DSL/IR 研究。 |
| `AnimLayerInterface` | `UAnimLayerInterfaceFactory` 与 Animation Blueprint factory 同文件族。 | 应作为 AnimationBlueprint family 的子规格，而不是完全独立的第一批 generator。 |

### 应规划为边界型或 patch 型能力的资产

| 资产 | 规划方式 | 原因 |
| --- | --- | --- |
| `AnimSequence` | 规划为受限 generator/patcher：创建最小 fixture、设置 skeleton/preview mesh、notifies、curves、metadata、sync markers；不承诺从 JSON 手写压缩后的骨骼关键帧。 | 许多下游资产需要 sequence fixtures，但完整 raw animation import/压缩链条更像 DCC/import pipeline，不适合作为第一版 JSON authoring。 |
| `Skeleton` | 规划为引用/验证边界，必要时增加 `SkeletonPatch` 类能力。 | 从零创建可用 skeleton 通常依赖 skeletal mesh/import 数据。generator 更应该验证 skeleton 兼容性并读取 CDO/metadata。 |
| `SkeletalMesh` | 不纳入当前 animation generator 核心。 | 它是 import-heavy 资产，应属于 mesh/import pipeline，不适合在 animation generator 内手写。 |
| `PhysicsAsset` | 可作为后续辅助 generator。 | 它与 SkeletalMesh 强相关，对 animation preview 和 ragdoll 有价值，但不是 AnimGraph/Montage 的直接前置。 |
| `AnimBoneCompressionSettings` / `AnimCurveCompressionSettings` | 可作为低风险配置资产补充。 | factory 存在，契约简单，但对 agent 创作动画行为的价值低于 Montage/IK/PoseSearch。 |
| `AnimStreamable` | 暂列后续。 | factory 基于 `SourceAnimation` 调用 `InitFrom`，更像派生/优化资产。 |
| `AnimationModifier` | 规划为 operation/pipeline step，而不是普通 asset generator。 | 它通常作用于 existing animation assets，适合作为“对 AnimSequence 执行修改”的命令式能力。 |
| `AnimationSharingSetup` | 后续规划。 | 对大规模角色共享动画有价值，但应用场景更窄。 |

### 暂不作为当前路线图目标

- 纯 JSON 创建完整 `SkeletalMesh`。
- 从零手写完整 `Skeleton` hierarchy、retarget base pose 和 bind pose 数据。
- 从 JSON 直接描述完整 `AnimSequence` raw/compressed keyframe 数据并替代导入管线。
- 第一阶段实现完整 `ControlRig` RigVM 图语言。
- 第一阶段覆盖 DeformerGraph、AnimNext、MetaHuman 专用资产或第三方插件动画资产。

这些项目不是永远不做，而是不应阻塞当前“功能完备的 animation generator family”。完整路线图可以先覆盖 agent 最常需要生成和组合的动画创作资产，再把 import-heavy 和 experimental systems 作为独立研究分支。

## AnimationBlueprint 发现

`AnimationBlueprintGenerator` 不应复用通用 `BlueprintGenerator` 作为实现路径。带有 `ParentClass = AnimInstance` 的普通 Blueprint 并不够，因为真正的 `UAnimBlueprint` 需要感知 skeleton 的创建流程和动画编译路径。

官方创建路径：

```cpp
UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
Factory->ParentClass = UAnimInstance::StaticClass();
Factory->TargetSkeleton = Skeleton;
Factory->PreviewSkeletalMesh = PreviewMesh;

UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(
    AssetTools.CreateAsset(Name, Path, UAnimBlueprint::StaticClass(), Factory)
);
```

factory 内部会调用 `FKismetEditorUtilities::CreateBlueprint(..., UAnimBlueprint::StaticClass(), ...)`，然后将 `TargetSkeleton` 写入 blueprint、generated class 和 skeleton generated class。

编译路径：

```cpp
FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
```

AnimGraph module 会为 `UAnimBlueprint` 注册专用 compiler。保存可以使用与现有 generators 相同的 package save 模式，必要时也可以使用 editor save helpers。

创建会自动为 asset 提供默认 EventGraph 和 AnimGraph 脚手架。AnimGraph 图修改必须使用 UE graph schema 和 node lifecycle，而不是手动填充数组。

## AnimGraph 和 StateMachine 复杂性

AnimGraph 和 StateMachine generation 不是平坦节点列表问题。

重要的 UE 行为：

- `UAnimationGraphSchema::CreateDefaultNodesForGraph` 创建 AnimGraph root node。
- `UAnimationGraphSchema::SpawnNodeFromAsset` 处理动画资产节点生成和 skeleton 兼容性，但不匹配时可能静默不创建节点，因此 generator 应预先根据目标 skeleton 验证资产，或确认节点确实已被创建。
- State machine nodes 在 node placement 期间创建嵌套的 `UAnimationStateMachineGraph` 子图和默认 entry nodes。
- State nodes 创建带有 state result nodes 的 `UAnimationStateGraph` 子图。
- Transition nodes 创建带有 transition result nodes 的 `UAnimationTransitionGraph` 子图。
- State machine schema 在连接 states 时可以自动插入 transition nodes。

因此，generator 代码应尽可能调用 schema/actions/node lifecycle APIs。不应把图数组、pins 或子图当作稳定数据手写。

## 语言和契约决策

对于面向 agent 的工具，最佳契约不是单一统一的人类语言。最佳契约是显式、隔离的源码块：

- 用于资产外壳和通用 Blueprint 元数据的顶层 JSON；
- 用于姿势图表达的 `AnimGraph` block；
- 用于状态机表达的 `StateMachines[]` blocks；
- 使用 `BSLFragment` 表达普通 K2/EventGraph/function 逻辑的 `BlueprintGraphs[]` blocks。

这能让错误保持局部化。解析错误可以指向 `AnimGraph.Source`、`StateMachines[0].Source` 或 `BlueprintGraphs[0].Source`。

`BSLFragment` 被有意命名为 fragment 契约。当前 BSL parser 期望完整的 `blueprint ... extends ... { ... }` wrapper，因此 Animation Blueprint integration 应在调用现有 BSL infrastructure 之前包装并重定向这些 blocks，而不是假装目前已经接受裸露的 event snippets。

不建议将 YAML 作为主要设计轴。它改变的是表面语法，但不能解决图语义。如果某个 YAML frontend 将来有用，它应在 UE-side generator 之外编译为同一个 canonical JSON/IR。

UE clipboard text（`FEdGraphUtilities::ExportNodesToText` / `ImportNodesFromText`）对调试或 fallback 导入/导出研究有用，但它太接近内部 UObject text，不适合作为长期创作格式。

## 推荐的规格方向

animation generator 家族应拆分为小而可独立验证的 specs：

- Animation generator spec map 和语言架构。
- 动画资产边界和 `AnimSequence` 受限 patch/minimal fixture contract。
- BlendSpace/AimOffset generator。
- AnimComposite、AnimMontage、PoseAsset 和 MirrorDataTable generator。
- AnimationBlueprint 生命周期 generator。
- Canonical AnimGraph IR 和最小姿势图 builder。
- StateMachine source/IR 和 transition rule builder。
- 面向 Animation Blueprint EventGraph/functions 的 BSLFragment integration。
- 高级 animation nodes 和 raw-node escape hatch。
- IKRig/IKRetargeter generator。
- PoseSearchSchema、PoseSearchDatabase 和 ChooserTable generator。
- ControlRigBlueprint lifecycle generator，以及后续 RigVM 图语言研究。
- Extraction、round-trip、MCP docs 和 fixtures。

这样能让 BlendSpace、Montage 等可独立打开和复用的资产先落地，让 AnimationBlueprint lifecycle 在进入图复杂性之前可验证，并给图语言在实现前留下稳定空间。
