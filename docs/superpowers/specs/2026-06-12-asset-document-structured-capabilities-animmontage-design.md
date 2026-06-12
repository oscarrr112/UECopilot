# AssetDocument Body/Profile + AnimMontage Pilot 设计

日期：2026-06-12

状态：Draft，待确认后进入 implementation plan

基线：`c7e123bfc59212597c0be89094271cc4bb98a273`

分支：`feature/asset-document-structured-capabilities-spec`

---

## 1. 背景

第一阶段已经把 AssetDocument 的基础能力落地：

- `.assetdoc.json` sidecar 可以跟随 `uasset` 放在同一目录；
- AssetDocument 可以通过 CDO diff 表达 reflected property patch；
- C++ 侧已经有 compiler、sidecar service、file watcher、validate、diff、extract、apply-file；
- MCP 侧已经有统一入口，不再需要每个资产都先暴露一个完整 generator。

这个阶段要回答下一层问题：

```text
tracks、sections、segments、samples、entries、rows、channels 这类结构化内容，
如何进入同一个 AssetDocument 框架，而不是回到每类资产一个重 generator？
```

核心判断：

- 反射属性仍然走 `Properties`；
- 文档公开层的结构化语义走 `Body`；
- compiler 内部用可注册的 profile/capability adapter 处理 `Body`；
- AssetDocument compiler 是编排器；
- 每个 adapter 是薄能力组件，不拥有 sidecar、HTTP、MCP、资产生命周期等通用复杂度。

`AnimMontage` 是第二阶段最合适的试点。它不是纯数据资产，也不是图资产；它有明确的 `SlotAnimTracks`、`FAnimSegment`、`CompositeSections`、notify/branching point 等结构，能验证框架能否覆盖“非纯数据但非图”的资产。

---

## 2. 目标

### 2.1 功能目标

本阶段实现 AssetDocument 的结构化能力层：

1. 在 AssetDocument 文档层新增 `Body` block，并把 capability map 降为 compiler 内部实现细节。
2. C++ 侧新增 capability registry 和 capability adapter 接口。
3. `AssetDocumentCompiler` 根据 `Class + Body keys` 动态调度 profile/capability adapters。
4. 使用 `UAnimMontage` profile 作为第一个 structured body 试点。
5. 支持通过 `.assetdoc.json` sidecar 创建或更新一个可打开、可保存、可 diff 的 `UAnimMontage`；extract 是辅助能力，不是主 workflow。
6. 保持 sidecar watcher 的工作方式不变：保存 `.assetdoc.json` 后自动 apply 到目标 asset。
7. MCP 能按 asset/class 返回 AssetDocument profile，并能生成 sidecar template。

### 2.2 架构目标

本阶段不是新增一个 `AnimMontageGenerator`，也不是恢复“每个 asset type 一个 OOP generator”的路线。

目标架构是：

```text
AssetDocument
  -> AssetDocumentCompiler
    -> DefaultObjectLifecycleAdapter
    -> PropertyPatchAdapter
    -> AssetDocumentProfileRegistry
      -> AnimMontageProfile
    -> AssetDocumentCapabilityRegistry
      -> AnimMontageCapability
      -> future: BlendSpaceCapability
      -> future: DataTableRowsCapability
      -> future: WidgetTreeCapability
      -> future: MaterialGraphCapability
```

这更接近 COM / capability composition：

- profile/capability 通过注册表发现；
- compiler 不知道具体 `AnimMontage` 字段；
- adapter 可以声明支持的 asset class、schema、apply/extract/diff/validate 行为；
- 新资产能力优先新增 adapter，而不是扩展一个越来越大的 generator 基类。

### 2.3 非目标

本阶段不做：

- Animation Blueprint / AnimGraph / StateMachine；
- MaterialGraph、Niagara、Cascade 图语言；
- LevelSequence、MovieScene channel 体系；
- 原始动画 keyframe / raw animation track 写入；
- 完整 notify blueprint authoring；
- 运行时 montage playback 验证；
- 迁移或删除现有 generator registry；
- 兼容旧的 `generate_assets` generator 扩展方式。

---

## 3. AssetDocument 格式扩展

Godot 的 `.tscn/.tres` 不是让用户写一个 “capability map”。它在文件头或 entry heading 中声明当前资源/节点类型，然后在该类型下面写属性；外部和内部资源通过 `ExtResource("id")` / `SubResource("id")` 显式引用。官方文档也说明，等于默认值的属性不会被保存。参考：[Godot TSCN file format](https://docs.godotengine.org/en/4.4/contributing/development/file_formats/tscn.html)。

AssetDocument 采用类似原则：

```text
Class 决定 Body schema
Body 写当前 Class 的结构化内容
Definitions 写可复用 fragment
Properties 写 CDO/default diff 属性
capability map 只存在于 compiler 内部
```

### 3.1 顶层字段

在第一阶段格式基础上新增 `Definitions` 和 `Body`：

```json
{
  "SchemaVersion": 1,
  "Action": "CreateOrUpdate",
  "Target": "/Game/Anim/AM_Attack",
  "Class": "/Script/Engine.AnimMontage",
  "Definitions": {
    "AttackAnim": {
      "Kind": "AssetRef",
      "Path": "/Game/Anim/A_Attack.A_Attack",
      "Class": "/Script/Engine.AnimSequence"
    },
    "HitNotify": {
      "Kind": "EmbeddedObject",
      "Class": "/Script/Game.MyAttackNotify",
      "Properties": {
        "Damage": 10
      }
    }
  },
  "Properties": {
    "RateScale": 1.0
  },
  "Body": {
      "Skeleton": {
        "Kind": "AssetRef",
        "Path": "/Game/Characters/SK_Mannequin_Skeleton.SK_Mannequin_Skeleton",
        "Class": "/Script/Engine.Skeleton"
      },
      "PreviewMesh": {
        "Kind": "AssetRef",
        "Path": "/Game/Characters/SK_Mannequin.SK_Mannequin",
        "Class": "/Script/Engine.SkeletalMesh"
      },
      "Slots": [
        {
          "Name": "DefaultSlot",
          "Segments": [
            {
              "Animation": {
                "Kind": "DefinitionRef",
                "Id": "AttackAnim"
              }
            }
          ]
        }
      ],
      "Notifies": [
        {
          "Time": 0.35,
          "Object": {
            "Kind": "DefinitionRef",
            "Id": "HitNotify"
          }
        }
      ]
  }
}
```

`Body` 是当前 `Class` 的结构化内容 object：

- 文档作者不需要写 capability name；
- `Class` 决定可用的 body sections；
- compiler 通过 AssetDocument profile 把 body keys 动态分发给内部 capability/adapter；
- 未被 profile 声明的 body key 是 validation error。

`Definitions` 是文档内部 fragment 表：

- key 是文档内局部 ID；
- value 必须是显式 `Kind` 的 fragment；
- Body 内可以通过 `DefinitionRef` 引用；
- `Definitions` 不拥有 UE package 生命周期，只是 AssetDocument 内部复用和 identity 语义；
- compiler 必须检测循环引用，例如 `A -> B -> A`。

### 3.2 `Class` 字段

本阶段延续第一阶段约束：

- `Class` 必须是完整反射路径，例如 `/Script/Engine.AnimMontage`；
- 不支持 subtype fallback；
- 如果 asset 已存在，`Class` 必须与实际 asset class 一致，或者是完全相同 class 的 redirect 后结果；
- adapter 不负责猜 class。

这避免了 “AnimMontage 或任意子类都可以” 带来的 CDO、factory、序列化语义差异。

### 3.3 Target 显式声明

`Target` 继续显式声明，sidecar 文件路径不作为唯一 source of truth。

规则：

1. `.assetdoc.json` 必须有 `Target`。
2. 如果是 sidecar apply，`Target` 必须与 sidecar 路径推导出的 asset path 一致。
3. 不一致时 validate/apply 均失败。

这样牺牲一点冗余，换来可读性、可移动性和安全检查。

### 3.4 Fragment kinds

本阶段不只实现 `EmbeddedObject`。第一版 fragment substrate 支持以下 `Kind`：

#### AssetRef

引用已有外部 UE asset。

```json
{
  "Kind": "AssetRef",
  "Path": "/Game/Anim/A_Attack.A_Attack",
  "Class": "/Script/Engine.AnimSequence"
}
```

规则：

- `Path` 必须是完整 asset path；
- `Class` 必须是完整反射路径；
- placement/capability 可以提供 expected base class；
- validate 时必须 load 并校验 class 兼容性。

#### ClassRef

引用 UE class。

```json
{
  "Kind": "ClassRef",
  "Class": "/Script/Game.MyAttackNotify"
}
```

规则：

- `Class` 必须是完整反射路径；
- fragment context 可以要求 expected base class；
- 不实例化对象。

#### StructValue

描述一个 struct value。

```json
{
  "Kind": "StructValue",
  "Struct": "/Script/CoreUObject.Vector",
  "Properties": {
    "X": 1.0,
    "Y": 2.0,
    "Z": 3.0
  }
}
```

规则：

- `Struct` 必须是完整反射路径；
- `Properties` 使用通用 property setter；
- 适合后续曲线 key、颜色、向量、transform、范围等值类型。

#### EmbeddedObject

描述内嵌 UObject。

```json
{
  "Kind": "EmbeddedObject",
  "Class": "/Script/Game.MyAttackNotify",
  "Properties": {
    "Damage": 10
  }
}
```

规则：

- `Class` 必须是完整反射路径；
- fragment context 必须提供 `Outer` 和 expected base class；
- 对象创建、属性设置、extract、diff 走 fragment adapter；
- 不允许 capability 直接 new 或硬编码具体 UObject class。

#### DefinitionRef

引用 `Definitions` 中的 fragment。

```json
{
  "Kind": "DefinitionRef",
  "Id": "AttackAnim"
}
```

规则：

- `Id` 必须存在于 `Definitions`；
- 被引用 definition 自己也必须有 `Kind`；
- compiler 展开时保留 reference path，便于 diff/error reporting；
- validate 必须检测循环引用；
- 同一个 `EmbeddedObject` definition 被多个 placement 引用时，默认语义是“同一份 definition 生成多个 placement-local object instance”，除非后续 schema 显式加入 shared instance 语义。

### 3.5 Body block 示例

第一版 `UAnimMontage` body 建议格式：

```json
{
  "Body": {
      "Skeleton": {
        "Kind": "AssetRef",
        "Path": "/Game/Characters/SK_Mannequin_Skeleton.SK_Mannequin_Skeleton",
        "Class": "/Script/Engine.Skeleton"
      },
      "PreviewMesh": {
        "Kind": "AssetRef",
        "Path": "/Game/Characters/SK_Mannequin.SK_Mannequin",
        "Class": "/Script/Engine.SkeletalMesh"
      },
      "Slots": [
        {
          "Name": "DefaultSlot",
          "Segments": [
            {
              "Animation": {
                "Kind": "AssetRef",
                "Path": "/Game/Anim/A_Attack.A_Attack",
                "Class": "/Script/Engine.AnimSequence"
              },
              "StartPos": 0.0,
              "AnimStartTime": 0.0,
              "AnimEndTime": 0.8,
              "AnimPlayRate": 1.0,
              "LoopingCount": 1
            }
          ]
        }
      ],
      "Sections": [
        {
          "Name": "Start",
          "Time": 0.0,
          "NextSection": "End"
        },
        {
          "Name": "End",
          "Time": 0.8
        }
      ],
      "Blend": {
        "BlendInTime": 0.25,
        "BlendOutTime": 0.25
      },
      "Notifies": [
        {
          "Time": 0.35,
          "Object": {
            "Kind": "EmbeddedObject",
            "Class": "/Script/Game.MyAttackNotify",
            "Properties": {
              "Damage": 10
            }
          }
        }
      ],
      "NotifyStates": [
        {
          "Time": 0.25,
          "Duration": 0.35,
          "Object": {
            "Kind": "EmbeddedObject",
            "Class": "/Script/Game.MyAttackWindowNotifyState",
            "Properties": {}
          }
        }
      ]
  }
}
```

说明：

- JSON 字段使用面向用户的语义名；
- adapter 内部映射到 UE 结构：
  - `Skeleton` -> fragment compiler with `ExpectedBaseClass=USkeleton`;
  - `PreviewMesh` -> fragment compiler with `ExpectedBaseClass=USkeletalMesh`;
  - `Slots[]` -> `UAnimMontage::SlotAnimTracks`;
  - `Segments[]` -> `FAnimTrack::AnimSegments`;
  - segment 字段 -> `FAnimSegment`;
  - `Animation` -> fragment compiler with `ExpectedBaseClass=UAnimSequenceBase`;
  - `Sections[]` -> `UAnimMontage::CompositeSections`;
  - `NextSection` -> `FCompositeSection::NextSectionName`;
  - `Notifies[].Object` -> fragment compiler with `ExpectedBaseClass=UAnimNotify`;
  - `NotifyStates[].Object` -> fragment compiler with `ExpectedBaseClass=UAnimNotifyState`;
- `Time` 使用 section linkable time，而不是直接写 deprecated editor-only `StartTime_DEPRECATED`。

---

## 4. Compiler / Profile / Capability 架构

### 4.1 编排流程

`AssetDocumentCompiler` 的 apply 流程扩展为：

```text
1. Parse document
2. Validate common fields
3. Validate Definitions and fragment graph
4. Resolve sidecar target consistency
5. Create or load UObject
6. Validate class exact match
7. Apply reflected Properties through PropertyPatchAdapter
8. For each Body entry in deterministic order:
     adapter = Profile.ResolveBodyAdapter(BodyKey)
     adapter.Validate(Context, JsonValue)
     adapter.Apply(Context, JsonValue)
      if adapter encounters a fragment:
        FragmentCompiler.Compile(FragmentJson, FragmentContext)
9. Mark dirty / post edit / save package
10. Return structured result
```

关键点：

- compiler 不写 `if (Name == "AnimMontage")`；
- unknown body key 是 validation error；
- body key 不属于当前 Class profile 是 validation error；
- `Validate` 不能修改 asset；
- `Apply` 只能修改自己声明负责的结构化区域；
- body adapter 不直接创建 fragment 对象，只向 `AssetDocumentFragmentCompiler` 提交显式 `Kind` fragment 和上下文。

### 4.2 Profile 接口

Profile 是文档公开层和内部 adapter 的分界。

```cpp
class IAssetDocumentProfile
{
public:
	virtual ~IAssetDocumentProfile() = default;

	virtual UClass* GetExactClass() const = 0;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const = 0;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const = 0;
	virtual TArray<FName> GetBodyKeys() const = 0;

	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const = 0;
};
```

规则：

- Agent/MCP 只看到 profile、template、`Body` schema；
- profile 内部可以把多个 body key 映射到同一个 capability；
- `Body` key 到 capability/adapter 的映射由 profile 动态解析；
- profile 不直接写 asset，只负责 schema、template、routing。

`AnimMontageProfile` 示例：

```text
Class=/Script/Engine.AnimMontage
Body.Skeleton     -> AnimMontageCapability
Body.PreviewMesh  -> AnimMontageCapability
Body.Slots        -> AnimMontageCapability
Body.Sections     -> AnimMontageCapability
Body.Notifies     -> AnimMontageCapability / AnimMontageNotifyTimelinePlacementAdapter
Body.NotifyStates -> AnimMontageCapability / AnimMontageNotifyTimelinePlacementAdapter
Body.Blend        -> AnimMontageCapability
```

### 4.3 Capability 接口

建议 C++ 接口：

```cpp
class IAssetDocumentCapability
{
public:
	virtual ~IAssetDocumentCapability() = default;

	virtual FName GetName() const = 0;
	virtual int32 GetApplyOrder() const = 0;
	virtual bool SupportsAsset(const UObject* Asset) const = 0;
	virtual bool SupportsClass(const UClass* AssetClass) const = 0;

	virtual TSharedRef<FJsonObject> GetSchemaHint() const = 0;

	virtual FAssetDocumentCapabilityResult Validate(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonValue>& CapabilityJson) const = 0;

	virtual FAssetDocumentCapabilityResult Apply(
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonValue>& CapabilityJson) = 0;

	virtual FAssetDocumentCapabilityResult Extract(
		const FAssetDocumentCapabilityContext& Context,
		TSharedRef<FJsonObject>& OutCapabilityJson) const = 0;

	virtual FAssetDocumentCapabilityResult Diff(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonValue>& DesiredJson,
		TArray<FAssetDocumentDiffEntry>& OutDiff) const = 0;
};
```

接口可以在实现时拆小，但语义边界要保持：

- `Validate`：语法、引用、asset class、范围检查；
- `Apply`：真实写入；
- `Extract`：从 asset 反向生成辅助 `Body` block；
- `Diff`：比较 desired JSON 与当前 asset 结构；
- `GetSchemaHint`：提供 MCP/inspect 可读 schema。

### 4.4 Context

`FAssetDocumentCapabilityContext` 至少包含：

```cpp
struct FAssetDocumentCapabilityContext
{
	UObject* Asset = nullptr;
	UClass* AssetClass = nullptr;
	FString TargetAssetPath;
	FString SourceDocumentPath;
	bool bIsDryRun = false;
	FAssetDocumentDiagnosticSink* Diagnostics = nullptr;
};
```

后续可增加：

- package path；
- transaction/undo scope；
- resolved reference cache；
- apply mode；
- editor refresh hooks。

### 4.5 Registry

Profile registry 职责：

- 通过 exact `UClass` 查找 profile；
- 为 MCP schema/template/inspect 提供公开文档形状；
- 不直接 apply asset。

建议：

```cpp
class FAssetDocumentProfileRegistry
{
public:
	void Register(TSharedRef<IAssetDocumentProfile> Profile);
	const IAssetDocumentProfile* FindForClass(UClass* AssetClass) const;
};
```

Capability registry 职责：

- 通过 `FName` 查找 capability；
- 返回所有已注册 capability；
- 根据 asset/class 返回 supported capability list；
- 保证 deterministic apply order；
- 不理解 capability 内部字段。

建议：

```cpp
class FAssetDocumentCapabilityRegistry
{
public:
	void Register(TSharedRef<IAssetDocumentCapability> Capability);
	const IAssetDocumentCapability* Find(FName Name) const;
	TArray<const IAssetDocumentCapability*> GetSupportedForClass(UClass* Class) const;
};
```

注册时机可以是模块 startup：

```cpp
void FAssetFactoryModule::StartupModule()
{
	AssetDocumentProfileRegistry.Register(MakeShared<FAnimMontageAssetDocumentProfile>());
	AssetDocumentCapabilityRegistry.Register(MakeShared<FAnimMontageAssetDocumentCapability>());
}
```

后续如果拆模块，可以让各模块自行注册能力。

### 4.6 Fragment compiler

Body adapter / internal capability 处理结构化领域时，不应静态持有一串子 adapter。例如不允许写成：

```text
AnimMontageCapability owns NotifyTimelineAdapter owns AnimNotifyObjectAdapter
```

正确规则是：

```text
adapter 可以静态注册
compiler 必须动态装配
```

第一版新增 `AssetDocumentFragmentCompiler`：

```cpp
class FAssetDocumentFragmentCompiler
{
public:
	FAssetDocumentFragmentResult Validate(
		const TSharedRef<FJsonObject>& FragmentJson,
		const FAssetDocumentFragmentContext& Context) const;

	FAssetDocumentFragmentResult Compile(
		const TSharedRef<FJsonObject>& FragmentJson,
		const FAssetDocumentFragmentContext& Context) const;

	FAssetDocumentFragmentResult Extract(
		const FAssetDocumentFragmentExtractContext& Context,
		TSharedRef<FJsonObject>& OutFragmentJson) const;
};
```

`FAssetDocumentFragmentContext` 至少包含：

```cpp
struct FAssetDocumentFragmentContext
{
	UObject* OwnerAsset = nullptr;
	UObject* Outer = nullptr;
	UClass* ExpectedBaseClass = nullptr;
	UScriptStruct* ExpectedStruct = nullptr;
	FString JsonPath;
	FString Role;
};
```

fragment compiler 通过 `Kind` 找 adapter：

```text
Kind=AssetRef        -> AssetRefFragmentAdapter
Kind=ClassRef        -> ClassRefFragmentAdapter
Kind=StructValue     -> StructValueFragmentAdapter
Kind=EmbeddedObject  -> EmbeddedObjectFragmentAdapter
Kind=DefinitionRef   -> DefinitionRefFragmentAdapter
```

建议接口：

```cpp
class IAssetDocumentFragmentAdapter
{
public:
	virtual ~IAssetDocumentFragmentAdapter() = default;

	virtual FName GetKind() const = 0;
	virtual bool SupportsContext(const FAssetDocumentFragmentContext& Context) const = 0;

	virtual FAssetDocumentFragmentResult Validate(
		const TSharedRef<FJsonObject>& FragmentJson,
		const FAssetDocumentFragmentContext& Context) const = 0;

	virtual FAssetDocumentFragmentResult Compile(
		const TSharedRef<FJsonObject>& FragmentJson,
		const FAssetDocumentFragmentContext& Context) const = 0;

	virtual FAssetDocumentFragmentResult Extract(
		const FAssetDocumentFragmentExtractContext& Context,
		TSharedRef<FJsonObject>& OutFragmentJson) const = 0;
};
```

`DefinitionRefFragmentAdapter` 只负责：

- 在 `Definitions` 中查找 `Id`；
- 检测循环引用；
- 把 referenced fragment 交回 `AssetDocumentFragmentCompiler`；
- 保留原始 reference path，便于 error/diff。

它不直接解释 `AssetRef` / `EmbeddedObject` / `StructValue`。

#### Placement adapter 与 fragment compiler

placement adapter 只负责容器位置，不负责对象语义。

例如 montage notify：

```text
AnimMontageCapability
  -> AnimMontageNotifyTimelinePlacementAdapter
      - parse Time / Duration
      - create or update FAnimNotifyEvent placement
      - call FragmentCompiler with ExpectedBaseClass=UAnimNotify or UAnimNotifyState
      - attach compiled UObject to FAnimNotifyEvent
```

这样后续 `AnimSequence` 或 `AnimComposite` 支持 notify 时，只新增自己的 placement adapter，并复用同一个 fragment compiler 和 object fragment adapters。

禁止的形态：

```text
AnimMontageCapability -> AnimNotifyCapability
AnimSequenceCapability -> AnimNotifyCapability
MaterialCapability -> NiagaraCapability
```

允许的形态：

```text
BodyAdapter/ContainerCapability -> PlacementAdapter -> FragmentCompiler -> FragmentAdapterRegistry
```

---

## 5. AnimMontage Capability

### 5.1 支持范围

第一版支持：

- 创建或更新 `UAnimMontage`；
- 设置 Skeleton；
- 设置 PreviewMesh；
- 设置 slot tracks；
- 设置 animation segments；
- 设置 composite sections；
- 设置 section next link；
- 设置基础 blend time；
- 创建 montage timeline 内嵌的 `UAnimNotify` / `UAnimNotifyState` 对象，并通过反射设置它们的属性；
- extract/diff 上述内容。

第一版暂不支持或仅 inspect 的字段必须记录到 deferred fields 目录：

```text
docs/superpowers/specs/asset-document-deferred-fields/
```

规则：

- 每个 capability 维护一份 deferred fields 文件；
- spec 里如果写“暂不支持”“仅 inspect”“skipped”，必须同步把字段、原因、清理条件写进 deferred fields 文件；
- 后续实现或 review 不允许只靠正文记忆追踪未完成项。

AnimMontage 第一版 deferred fields：

- branching point；
- marker sync；
- root motion advanced settings；
- metadata object authoring；
- montage editor UI layout。

Notify / NotifyState 不是独立 deferred 项。第一版可以支持“完整对象创建”，但边界是：

- 支持在 `AnimMontage` capability 内创建 montage timeline 上的 notify event；
- 支持 `Object.Kind=EmbeddedObject` 或 `Object.Kind=DefinitionRef`；
- 支持 `Object.Class` 使用完整反射路径；
- 支持 `Object.Properties` 使用通用 property patch 设置 notify 对象字段；
- 不在本阶段抽象独立的 `AnimNotify` / `AnimNotifyState` capability；
- 不在本阶段理解每种 AN/ANS 的领域语义。

示例：

```json
{
  "Notifies": [
    {
      "Time": 0.35,
      "Object": {
        "Kind": "EmbeddedObject",
        "Class": "/Script/Game.MyAttackNotify",
        "Properties": {}
      }
    }
  ],
  "NotifyStates": [
    {
      "Time": 0.25,
      "Duration": 0.35,
      "Object": {
        "Kind": "EmbeddedObject",
        "Class": "/Script/Game.MyAttackWindowNotifyState",
        "Properties": {}
      }
    }
  ]
}
```

这不需要新增一套 AN capability。Montage 只负责 timeline placement，notify 对象自身由 fragment compiler 动态编译。

```text
AnimMontageCapability
  -> AnimMontageNotifyTimelinePlacementAdapter
    -> AssetDocumentFragmentCompiler
      -> EmbeddedObjectFragmentAdapter
      -> DefinitionRefFragmentAdapter
```

其中：

- `AnimMontageNotifyTimelinePlacementAdapter` 只负责把 notify event / notify state event 放进 Montage timeline；
- `AssetDocumentFragmentCompiler` 根据 `Object.Kind` 动态装配 fragment adapter；
- 后续如果新增独立 `AnimNotify` / `AnimNotifyState` capability，应复用同一套 class/properties schema，而不是发明第二套 notify object 表达；
- Montage 文档里的 `Notifies` / `NotifyStates` 应能迁移到未来 AN/ANS capability 的 richer schema。

### 5.2 UE 结构映射

UE 5.7 相关结构：

- `UAnimMontage::SlotAnimTracks`
- `FSlotAnimationTrack::SlotName`
- `FSlotAnimationTrack::AnimTrack`
- `FAnimTrack::AnimSegments`
- `FAnimSegment::SetAnimReference`
- `FAnimSegment::StartPos`
- `FAnimSegment::AnimStartTime`
- `FAnimSegment::AnimEndTime`
- `FAnimSegment::AnimPlayRate`
- `FAnimSegment::LoopingCount`
- `UAnimMontage::CompositeSections`
- `FCompositeSection::SectionName`
- `FCompositeSection::NextSectionName`
- `FAnimNotifyEvent`
- `UAnimNotify`
- `UAnimNotifyState`

实现时优先使用 UE API：

- `SetAnimReference` 设置 segment 动画引用；
- linkable element 的公开方法设置 time；
- montage 提供的 section/link helper 如果存在，应优先使用；
- notify / notify state 对象创建必须通过 fragment compiler，并由 placement context 限制基类为 `UAnimNotify` 或 `UAnimNotifyState`；
- raw array mutation 只允许在 adapter 内部小范围使用，并且必须配套 post edit/rebuild/validation。

### 5.3 引用校验

adapter 必须校验：

- `Skeleton` 必须能通过 fragment compiler 解析为 `USkeleton`；
- `PreviewMesh` 如果提供，必须能通过 fragment compiler 解析为 `USkeletalMesh`；
- `Animation` 必须能通过 fragment compiler 解析为 `UAnimSequenceBase`；
- animation 与 skeleton 兼容；
- section name 非空且唯一；
- segment 时间范围合法；
- `AnimPlayRate` 非 0；
- `LoopingCount` 大于 0；
- `NextSection` 如果提供，必须指向已有 section。
- `Notifies[].Object` 必须能通过 fragment compiler 解析为 `UAnimNotify` 实例；
- `NotifyStates[].Object` 必须能通过 fragment compiler 解析为 `UAnimNotifyState` 实例；
- notify / notify state 的 `Object.Properties` 必须能被通用 property setter 写入；
- notify state `Duration` 必须大于 0。

错误需要带 JSON path，例如：

```text
/Body/Slots[0]/Segments[1]/Animation
/Body/Sections[2]/NextSection
/Body/NotifyStates[0]/Object/Class
```

### 5.4 Apply 策略

P0 采用 replace-owned-block 策略：

- `Slots` 如果出现，则替换全部 slot tracks；
- `Sections` 如果出现，则替换全部 composite sections；
- `Notifies` 如果出现，则替换 AnimMontage body adapter 管理的全部 notify events；
- `NotifyStates` 如果出现，则替换 AnimMontage body adapter 管理的全部 notify state events；
- `Blend` 如果出现，则只更新声明字段；
- 未声明的 Body 子块保持不变。

理由：

- slot/section 是数组结构，partial patch 需要 identity、merge、delete 语义；
- 第一版先保证 deterministic；
- 后续可以扩展 `Mode: "Patch"` 或 per-item `Action`。

示例：

```json
{
  "Body": {
    "Slots": [],
    "Sections": []
  }
}
```

表示显式清空 slots/sections。

如果 `Slots` 字段不存在，则不修改现有 slots。
如果 `Notifies` / `NotifyStates` 字段不存在，则不修改现有 notify timeline。

### 5.5 Extract 策略（辅助能力）

extract 默认输出与当前 asset 结构一致的 `Body` block，但它是辅助入口，不是 Agent 主 workflow：

```json
{
  "Body": {
      "Skeleton": {
        "Kind": "AssetRef",
        "Path": "...",
        "Class": "/Script/Engine.Skeleton"
      },
      "PreviewMesh": {
        "Kind": "AssetRef",
        "Path": "...",
        "Class": "/Script/Engine.SkeletalMesh"
      },
      "Slots": [
        {
          "Name": "DefaultSlot",
          "Segments": [
            {
              "Animation": {
                "Kind": "AssetRef",
                "Path": "...",
                "Class": "/Script/Engine.AnimSequence"
              },
              "StartPos": 0.0,
              "AnimStartTime": 0.0,
              "AnimEndTime": 0.8,
              "AnimPlayRate": 1.0,
              "LoopingCount": 1
            }
          ]
        }
      ],
      "Sections": [],
      "Notifies": [],
      "NotifyStates": []
  }
}
```

extract 不需要输出 unsupported/unknown internal data，但要在 result 中记录 skipped entries，并同步维护 deferred fields 文件。例如：

```json
{
  "Path": "/Body/BranchingPoints",
  "Reason": "Branching point authoring is deferred; tracked in asset-document-deferred-fields/2026-06-12-animmontage.md"
}
```

### 5.6 Diff 策略

diff 输出与第一阶段 property diff 风格一致：

- `changed`
- `unchanged`
- `missing`
- `extra`
- `unsupported`
- `invalid`

数组元素 path 使用 index：

```text
/Body/Slots[0]/Segments[0]/AnimEndTime
```

对于 replace-owned-block，diff 可以先按完整结构比较，不需要实现智能 move detection。

---

## 6. MCP / HTTP 能力

第一版不新增一批 AnimMontage 专用 MCP tool。

继续复用 AssetDocument 入口：

- `get_asset_document_schema`
- `inspect_asset_document_profile`
- `create_asset_document_template`
- `inspect_asset_document_target`
- `validate_asset_document`
- `apply_asset_document`
- `apply_asset_document_file`
- `extract_asset_document`
- `diff_asset_document`

主 workflow 是：

```text
inspect profile -> create sidecar template -> edit .assetdoc.json -> save -> watcher auto apply
```

`extract_asset_document` 是辅助能力：

- 从已有 `uasset` 回填初始 sidecar；
- debug；
- diff；
- 迁移旧资产；
- 检查当前 `uasset` 与 sidecar 的差异。

Agent 不应该依赖 extract 才知道怎么写文档；它应该先拿 profile/template。

### 6.1 schema

`get_asset_document_schema` 返回：

- common AssetDocument schema；
- common fragment schema；
- `Definitions` / `Body` / `Properties` 规则；
- registered profile summary；
- 每个 profile 支持的 class path。

### 6.2 profile

`inspect_asset_document_profile` 对 `UAnimMontage` 返回：

```json
{
  "Class": "/Script/Engine.AnimMontage",
  "DocumentShape": {
    "Definitions": "map<string, Fragment>",
    "Properties": "reflected CDO-diff properties",
    "Body": {
      "Skeleton": "AssetRef<USkeleton>",
      "PreviewMesh": "AssetRef<USkeletalMesh>",
      "Slots": "array",
      "Sections": "array",
      "Notifies": "array",
      "NotifyStates": "array",
      "Blend": "object"
    }
  },
  "BodySections": [
    "Skeleton",
    "PreviewMesh",
    "Slots",
    "Sections",
    "Notifies",
    "NotifyStates",
    "Blend"
  ],
  "FragmentKinds": [
    "AssetRef",
    "ClassRef",
    "StructValue",
    "EmbeddedObject",
    "DefinitionRef"
  ],
  "InternalAdapters": [
    {
      "Name": "AnimMontageCapability",
      "BodySections": ["Skeleton", "PreviewMesh", "Slots", "Sections", "Notifies", "NotifyStates", "Blend"]
    }
  ]
}
```

对于尚未创建的目标，可以根据 document `Class` inspect profile。

### 6.3 template

`create_asset_document_template` 输入 `Class`、`Target` 和可选 intent，输出 canonical sidecar 起稿：

```json
{
  "SchemaVersion": 1,
  "Action": "CreateOrUpdate",
  "Target": "/Game/Anim/AM_Attack",
  "Class": "/Script/Engine.AnimMontage",
  "Definitions": {},
  "Properties": {},
  "Body": {
    "Skeleton": null,
    "PreviewMesh": null,
    "Slots": [],
    "Sections": [],
    "Notifies": [],
    "NotifyStates": [],
    "Blend": {}
  }
}
```

Template 输出的是可编辑 sidecar，不自动 apply。

### 6.4 validate/apply/diff/extract

HTTP/MCP 层不理解 `UAnimMontage` body 字段，只透传给 compiler/profile。

这条规则很重要：否则复杂度会从 C++ generator 转移到 MCP wrapper，等于换了地方继续硬编码。

---

## 7. 与现有 generator 的关系

本阶段不新增 `AnimMontageGenerator`。

如果实现需要创建 `UAnimMontage`，应优先放在 lifecycle/factory adapter 层：

```text
DefaultObjectLifecycleAdapter
  -> direct NewObject/CreatePackage path for simple classes
  -> optional factory path for classes that require editor factory
```

`AnimMontage` 可能需要 `UAnimMontageFactory`、`TargetSkeleton`、`SourceAnimation`、`PreviewSkeletalMesh`。具体实现要在 implementation plan 阶段确认 UE 5.7 factory API。

允许新增：

- `AssetDocumentProfileRegistry` 和 `AnimMontageProfile`；
- `AnimMontageLifecycleCapability` 或 `FactoryCreatePolicy`；
- `AnimMontageAssetDocumentCapability`；
- `AssetDocumentFragmentCompiler` 和 `AssetDocumentFragmentAdapterRegistry`；
- `AssetRef` / `ClassRef` / `StructValue` / `EmbeddedObject` / `DefinitionRef` fragment adapters；
- small helper for loading animation references。

不允许新增一个承载完整业务的：

```text
AnimMontageGenerator : IAssetGenerator
```

如果必须通过现有 registry 暴露，也只能是很薄的 facade，并且不作为本阶段首选入口。

---

## 8. 文件与模块建议

建议新增或扩展：

```text
Source/AssetFactory/Public/AssetDocument/
  AssetDocumentProfile.h
  AssetDocumentProfileRegistry.h
  AssetDocumentCapability.h
  AssetDocumentCapabilityRegistry.h
  AssetDocumentFragmentCompiler.h
  AssetDocumentFragmentAdapter.h
  AssetDocumentFragmentAdapterRegistry.h

Source/AssetFactory/Private/AssetDocument/
  AssetDocumentProfileRegistry.cpp
  AssetDocumentCapabilityRegistry.cpp
  AssetDocumentCompiler.cpp
  AssetDocumentFragmentCompiler.cpp
  AssetDocumentFragmentAdapterRegistry.cpp
  AssetDocumentSchemaService.cpp
  AssetDocumentTemplateService.cpp

Source/AssetFactory/Private/AssetDocument/Profiles/
  AnimMontageAssetDocumentProfile.h
  AnimMontageAssetDocumentProfile.cpp

Source/AssetFactory/Private/AssetDocument/Fragments/
  AssetRefFragmentAdapter.cpp
  ClassRefFragmentAdapter.cpp
  StructValueFragmentAdapter.cpp
  EmbeddedObjectFragmentAdapter.cpp
  DefinitionRefFragmentAdapter.cpp

Source/AssetFactory/Private/AssetDocument/Capabilities/
  AnimMontageAssetDocumentCapability.h
  AnimMontageAssetDocumentCapability.cpp
  AnimMontageNotifyTimelinePlacementAdapter.h
  AnimMontageNotifyTimelinePlacementAdapter.cpp

docs/superpowers/specs/asset-document-deferred-fields/
  2026-06-12-animmontage.md
```

如果 `Private/AssetDocument/Capabilities` 目录较重，可以先放 private；待 capability 生态稳定后再公开接口。

模块依赖可能需要确认：

- `Engine`
- `UnrealEd`
- `AssetTools`
- animation editor/factory 相关模块

implementation plan 需要通过 UBT 验证具体依赖，不在 spec 中提前硬编码所有 include。

---

## 9. 测试计划

### 9.1 C++ Automation

新增 `AssetFactory.AssetDocument.Profile` / `AssetFactory.AssetDocument.Fragments` / `AssetFactory.AssetDocument.Body` 测试组：

1. fragment registry 能注册并发现 `AssetRef`、`ClassRef`、`StructValue`、`EmbeddedObject`、`DefinitionRef` adapters；
2. `DefinitionRef` 能展开到 `Definitions` 中的 fragment；
3. `DefinitionRef` 循环引用 validate 失败；
4. `AssetRef` 能校验 expected base class；
5. `ClassRef` 能校验 expected base class；
6. `StructValue` 能通过反射写入 struct properties；
7. `EmbeddedObject` 能使用指定 outer 创建 UObject 并应用 properties；
8. profile registry 能通过 `/Script/Engine.AnimMontage` 返回 `AnimMontageProfile`；
9. `create_asset_document_template` 能生成 canonical `Body` sidecar；
10. capability registry 能注册并发现内部 `AnimMontage` capability；
11. unknown body key validate 失败；
12. body key 与 class profile 不匹配时 validate 失败；
13. validate 不修改资产；
14. apply 可以创建 montage；
15. apply 可以更新 slots/sections；
16. diff 能识别 changed/unchanged；
17. extract 能输出 `Body`；
18. sidecar watcher 保存后能自动 apply montage document；
19. invalid animation/skeleton/section link/fragment ref 返回带 JSON path 的错误。

### 9.2 测试资产

需要一个稳定 fixture：

- `USkeleton`
- `UAnimSequenceBase` 或 `UAnimSequence`
- 可选 `USkeletalMesh`

优先方案：

- 在 automation 测试内生成最小 animation fixture；
- 如果 UE API 生成成本过高，则使用已有测试 fixture asset；
- fixture 创建逻辑必须属于测试，不进入 production generator。

### 9.3 MCP / TypeScript

MCP 测试需要覆盖：

- schema/profile 包含 `UAnimMontage` body sections；
- template 生成 canonical sidecar；
- validate bad body key error；
- inspect 返回 class profile；
- apply/extract/diff request/response contract 不破坏现有 AssetDocument 工具。

### 9.4 验证命令

实现完成后必须验证：

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

对于 worktree 插件，仍按项目约定使用临时 validation host project，避免主项目同名插件压制 worktree 改动。

还需要：

- Editor automation test；
- MCP `npm test`；
- MCP `npm run build`；
- live editor HTTP smoke：
  - write `.assetdoc.json`;
  - watcher auto apply；
  - readback/diff confirms result。

---

## 10. 验收标准

本阶段完成时应满足：

1. 不新增重型 `AnimMontageGenerator`。
2. `AssetDocumentCompiler` 不包含 `AnimMontage` 字段级 if/else。
3. profile registry 可以返回 `UAnimMontage` 的 `Body` schema。
4. `create_asset_document_template` 可以生成 canonical sidecar。
5. `.assetdoc.json` 可以创建一个 `UAnimMontage`。
6. `.assetdoc.json` 可以更新 montage slots、segments、sections。
7. 保存 sidecar 文件后，watcher 可以自动 apply 到目标 `uasset`。
8. `validate_asset_document` 对坏引用、坏 section link、class mismatch 给出明确错误。
9. `extract_asset_document` 能输出 `Body`，但它是辅助能力，不是主 workflow。
10. `diff_asset_document` 能比较 `Body` block。
11. notify / notify state 可以用完整 class path 创建内嵌对象，并通过反射设置属性。
12. `Definitions` + `DefinitionRef` 可以被 Body 内字段复用。
13. `AssetRef`、`ClassRef`、`StructValue`、`EmbeddedObject`、`DefinitionRef` 都通过 `AssetDocumentFragmentCompiler` 动态分发，不靠 capability 手写解析。
14. UBT、automation、MCP tests、live smoke 均通过。

---

## 11. 风险与决策点

### 11.1 Montage 创建路径

风险：

- `UAnimMontage` 可能需要 `UAnimMontageFactory` 才能正确初始化 skeleton/source animation；
- direct `NewObject` 创建后可能缺少 editor-only 初始化。

决策：

- implementation plan 第一项必须 research UE 5.7 factory API；
- 如果 factory 是必要条件，lifecycle adapter 提供 class/factory create policy；
- 不把 factory 逻辑塞进 capability apply。

### 11.2 Section time 写入

风险：

- `FCompositeSection::StartTime_DEPRECATED` 不能作为真实写入目标；
- section time 存在于 `FAnimLinkableElement` linkable data。

决策：

- implementation 时必须使用公开方法或经验证的 UE editor helper；
- 若只能 raw mutation，要配套 post edit、refresh 和 automation 验证。

### 11.3 Notify 范围

风险：

- notify event、notify state、branching point 语义复杂；
- 完整 AN/ANS 领域能力会牵涉每种 notify class 的专用语义；
- 内嵌 UObject 创建如果不走 fragment compiler，容易在 Montage adapter 内产生硬编码。

决策：

- P0 支持 timeline 内嵌 `UAnimNotify` / `UAnimNotifyState` 对象创建；
- P0 不实现独立 `AnimNotify` / `AnimNotifyState` capability；
- P0 不理解 AN/ANS 领域语义，只做 class path、实例化、反射属性 patch、timeline 挂接；
- P0 使用 `EmbeddedObject` / `DefinitionRef` fragment 表达 notify 对象；
- branching point 仍 deferred，并记录到 deferred fields 文件。

### 11.4 Partial patch

风险：

- slots/sections 的 merge/delete/move 语义复杂。

决策：

- P0 使用 replace-owned-block；
- 后续再引入 per-item action。

---

## 12. 后续扩展方向

这个 spec 完成后，下一批 structured capability 可以按类似方式接入：

| Capability | 目标资产 | 结构类型 | 备注 |
| --- | --- | --- | --- |
| `AnimComposite` | `UAnimComposite` | animation track / segments | 比 Montage 更简单，可复用 segment helper |
| `BlendSpace` | `UBlendSpace` | samples / axis | 验证 samples 类资产 |
| `DataTableRows` | `UDataTable` | rows | 适合替代专用 datatable row update |
| `CurveKeys` | `UCurveFloat` / `UCurveVector` | keys/channels | 验证 channel-like 数据 |
| `WidgetTree` | `UWidgetBlueprint` | tree / slots / style | 迁移 WidgetBlueprintGenerator 的动态经验 |
| `MaterialGraph` | `UMaterial` | graph nodes/links | 可以先以 JSON workflow 表达 |
| `NiagaraGraph` | Niagara assets | graph/modules | 需要更独立的图 DSL 或 workflow JSON |
| `AnimNotifyCapability` | `UAnimNotify` / `UAnimNotifyState` | class / properties / domain semantics | 后续独立 AN/ANS 能力应复用 fragment schema，不通过 capability-to-capability bridge 接入 |

图资产不应该直接塞进 `Properties`。它们应该是 capability 下的 graph/workflow block，由图 adapter 负责解释。

---

## 13. 推荐实施拆分

implementation plan 建议拆成：

1. **Fragment substrate**
   - `AssetDocumentFragmentCompiler`、fragment registry、`AssetRef`、`ClassRef`、`StructValue`、`EmbeddedObject`、`DefinitionRef`。
2. **Capability substrate**
   - interface、registry、compiler dispatch、schema hint、unknown capability validation、capability 调用 fragment compiler 的上下文协议。
3. **AnimMontage research + lifecycle**
   - 确认 UE 5.7 创建路径，完成 create/load/save。
4. **AnimMontage apply**
   - skeleton、preview mesh、slots、segments、sections、blend、notify timeline placement adapter、fragment-driven notify object creation。
5. **Profile/template/diff/inspect/extract**
   - class profile、sidecar template、fragment schema hints、diff entries、辅助 extract。
6. **Sidecar watcher + MCP smoke**
   - 保存 `.assetdoc.json` 自动更新 montage。
7. **Review and hardening**
   - error paths、no mutation on validate、UBT/automation/MCP/live smoke。

每个 task 仍然按项目规则记录 `TASK_BASE=HEAD`，完成后 checkpoint commit。
