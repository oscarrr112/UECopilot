# Generic Asset Document 设计

**日期**：2026-06-11  
**状态**：草稿（待用户审阅）  
**分支**：`feature/asset-document-generic-asset`  
**范围**：第一版同目录 sidecar AssetDocument、通用 UObject/DataAsset 资产描述、属性 patch、以及资产移动/重命名/复制/删除 hook。不包含图资产、树资产、导入资产或结构化资产实现。

---

## 1. 背景

AssetFactory 当前主要按 `AssetType` 扩展独立 generator。这个模式已经覆盖了 `DataAsset`、`Blueprint`、`WidgetBlueprint`、`Material`、`StateTree`、`BehaviorTree` 等类型，但随着 `AnimationBlueprint`、`AnimMontage`、`Niagara`、`MaterialGraph` 这类资产进入规划，每个 generator 都会重复处理资产生命周期、属性设置、验证、保存、提取和 MCP 暴露。

本设计的目标是先建立一个较低层的通用资产描述入口，用来验证：

- 对于核心内容只是 reflected properties 的资产，不需要为每个资产类型写一个完整 generator；
- 通过动态 class loading 和现有 `PropertySetterUtils`，可以生成当前没有专门 generator 的数据资产；
- 每个受管理 `.uasset` 可以拥有一个同目录 `.assetdoc.json` sidecar，并由该 sidecar 作为 source-of-truth；
- UE Editor 中的资产移动、重命名、复制、删除应同步处理 sidecar，让 AssetDocument 真正“跟着 uasset 跑”；
- 后续的结构化资产、Blueprint、Widget、Material、Niagara、AnimationBlueprint 可以逐步作为薄 adapter 接入，而不是继续增长为巨大的独立 generator。

配套研究文档见：

- `docs/superpowers/research/2026-06-11-ue-asset-taxonomy-and-unified-framework.md`

---

## 2. 目标

第一版只实现 `GenericAsset`：

- 新增 `AssetType: "GenericAsset"`。
- 新增同目录 sidecar 约定：`<AssetName>.uasset` 对应 `<AssetName>.assetdoc.json`。
- `AssetDocument` 是 source-of-truth，`.uasset` 是 materialized output。
- 通过 `Class` 动态解析 UObject 类。
- 支持创建、更新、创建或更新。
- 支持从 sidecar `.assetdoc.json` apply/generate 到目标 `.uasset`。
- 支持生成成功后写入或更新 sidecar。
- 支持从 sidecar 文件路径推导目标资产路径。
- 支持资产移动、重命名、复制、删除时同步 sidecar 的 editor hook。
- 支持普通 UObject/DataAsset asset 的 package 创建与保存。
- 支持 `Properties` 反射 patch，复用现有 `FPropertySetterUtils`。
- 支持 typed property format 和现有 untyped format。
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
- 不把 `extract_assets` 作为第一版核心链路；已有 `.uasset` 迁移到 AssetDocument 可以作为后续 best-effort adoption 能力。

`FactoryClass` / `Factory` 字段可以在 schema 中作为后续扩展说明，但第一版实现可以不支持或只做显式拒绝，避免半成品语义。

---

## 4. 用户契约

### 4.1 Sidecar 文件约定

每个受 AssetDocument 管理的 `.uasset` 使用同目录 sidecar：

```text
Content/Data/DA_GenericEnemy.uasset
Content/Data/DA_GenericEnemy.assetdoc.json
```

sidecar 路径推导目标资产路径：

```text
Content/Data/DA_GenericEnemy.assetdoc.json
-> /Game/Data/DA_GenericEnemy
```

sidecar 内可以省略 `Name`、`Path` 和 `Target`：

```json
{
  "SchemaVersion": 1,
  "AssetType": "GenericAsset",
  "Action": "CreateOrUpdate",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Properties": {
    "Health": 100,
    "MoveSpeed": 450.0
  }
}
```

如果 sidecar 显式声明 `Target`，必须与 sidecar 文件路径推导出的目标资产一致：

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/Data/DA_GenericEnemy",
  "AssetType": "GenericAsset",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Properties": {}
}
```

不一致时应返回 validation error，而不是隐式选择其中一个。

### 4.2 Inline 最小创建

第一版不要求兼容现有 `generate_assets` 入口。AssetDocument 应提供新的 MCP/HTTP 命令，直接接收 inline AssetDocument，便于测试和一次性生成：

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

生成成功后，如果请求包含 `WriteSidecar: true` 或 apply-sidecar 命令使用 sidecar 文件作为输入，应把最终 AssetDocument 写到目标 `.assetdoc.json`。

### 4.3 typed properties

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

### 4.4 更新已有资产

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

### 4.5 Sidecar hook 行为

第一版应注册 editor hook，使 sidecar 与 `.uasset` 一起移动：

| 资产操作 | sidecar 行为 |
| --- | --- |
| Rename | 将 `<OldName>.assetdoc.json` 重命名为 `<NewName>.assetdoc.json`，并更新可选 `Target` |
| Move | 将 sidecar 移到新目录，文件名不变，并更新可选 `Target` |
| Duplicate | 复制 sidecar 到新资产旁边，并更新可选 `Target` |
| Delete | 默认将 sidecar 移到同目录 `_DeletedAssetDocs` 或删除，具体实现计划固定；第一版至少不能留下误指向现有资产的 sidecar |

hook 必须只处理受管理资产：

- 资产旁边存在同名 `.assetdoc.json`；
- 或资产 metadata 标记为 AssetDocument-managed；
- 或操作来自 AssetDocument apply/generate。

如果 hook 无法安全同步 sidecar，应记录明确 warning，并尽量不阻止 UE 自身资产操作。implementation plan 需要确定错误上报位置。

---

## 5. 设计

### 5.1 Module 与组件化编排

第一版新增独立 Editor module，而不是在现有 `AssetGeneratorRegistry` 中注册 `GenericAssetGenerator`：

```text
Source/AssetDocument/AssetDocument.Build.cs
Source/AssetDocument/Public/AssetDocumentModule.h
Source/AssetDocument/Private/AssetDocumentModule.cpp
```

`AssetDocument` module 是新的资产文档编译与应用层。现有 `AssetFactory` HTTP/MCP 层可以新增命令来调用它，但 `AssetDocument` 不依赖 `AssetFactory` 的 generator registry，也不需要伪装成一个 generator。

这个设计更接近组件装配器/COM 风格，而不是“每个 AssetType 一个 OOP generator 子类”：

```text
AssetDocumentCompiler
  -> SidecarSource / InlineSource
  -> DocumentValidator
  -> AssetLifecycleAdapter
  -> PropertyPatchAdapter
  -> SavePackageAdapter
  -> EditorSidecarSyncService
```

第一版只注册最小能力组件：

- `JsonAssetDocumentSource`：读取 inline JSON 或 `.assetdoc.json`；
- `GenericUObjectLifecycleAdapter`：处理可直接 `NewObject` 的 UObject/DataAsset 风格资产；
- `ReflectionPropertyPatchAdapter`：通过 `FProperty` 设置 CDO/对象属性；
- `PackageSaveAdapter`：保存 package；
- `EditorSidecarSyncService`：处理 rename/move/duplicate/delete hook。

后续支持 AnimMontage、Widget、MaterialGraph、Niagara、ABP 时，应优先新增或替换 capability component，而不是把逻辑塞回一个大型 `GenericAssetGenerator`。如果旧 `generate_assets` 未来需要桥接 AssetDocument，也应该只是调用 `AssetDocument` module 的 facade，不承载业务复杂度。

### 5.2 Sidecar 解析与写入

新增小型 sidecar helper，放在 `Source/AssetDocument/Private/Sidecar/` 或等价目录：

```text
ResolveSidecarPath(ObjectPath) -> FilePath
ResolveObjectPathFromSidecar(FilePath) -> ObjectPath
LoadAssetDocument(FilePath) -> JsonObject
WriteAssetDocument(FilePath, JsonObject)
ValidateTargetMatchesSidecar(FilePath, Document)
```

路径规则：

```text
/Game/Data/DA_GenericEnemy
-> <Project>/Content/Data/DA_GenericEnemy.assetdoc.json
```

sidecar 必须使用 UTF-8 JSON。写入时保持稳定字段顺序，方便 git diff。

第一版不需要实现复杂 formatting/preserve comments。JSON 不支持注释，后续如果需要人工注释，可以单独讨论 JSONC/YAML frontend，但 UE 侧 canonical sidecar 先保持 JSON。

### 5.3 创建策略

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

### 5.4 Class 解析

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

### 5.5 属性 patch

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

### 5.6 更新策略

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

### 5.7 保存与 sidecar 写入策略

沿用现有 generator 风格：

```text
Asset->MarkPackageDirty()
Package->FullyLoad()
Package->SetDirtyFlag(true)
UPackage::SavePackage(...)
```

保存失败必须返回 `FGenerationResult::MakeFailed`。

sidecar 写入策略：

- apply/generate 成功后再写 sidecar，避免记录失败状态；
- 如果输入来自 sidecar 文件，成功后可以规范化写回同一文件；
- 如果输入来自 inline JSON 且 `WriteSidecar` 为 true，则写入目标 sidecar；
- 如果 `.uasset` 保存成功但 sidecar 写入失败，结果应视为 failed 或 partial failed。implementation plan 需要固定是否回滚 `.uasset`，第一版推荐返回 failed 并记录人工修复路径。

### 5.8 Editor sidecar hooks

第一版需要实现 sidecar 跟随 hook。候选 UE 事件：

- asset rename/move：监听 asset registry 或 editor asset subsystem 的 rename/renamed 事件；
- asset duplicate：监听 asset added/imported 后结合 duplication context，或在可用 editor delegate 中处理 duplicate；
- asset delete：监听 asset pre-delete/deleted 事件。

implementation plan 需要先确认 UE 5.7 中最稳定的 delegate/API。hook 实现原则：

- 只处理同目录同名 sidecar；
- 不扫描全项目做昂贵匹配；
- 避免递归触发；
- 不因为 sidecar 文件操作失败而破坏 UE 资产操作；
- 对 duplicate/move/rename 更新 `Target` 字段，如果存在；
- 不自动修改 `Properties`。

### 5.9 Extract / adoption 策略

第一版不把 `extract_assets` 作为核心验收。AssetDocument 是 source-of-truth，不要求从 `.uasset` 完美反向生成 sidecar。

后续可以新增 best-effort adoption：

```text
Existing .uasset -> Draft .assetdoc.json
```

用途是迁移旧资产、debug 和 smoke test，不是主链路。

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

## 7. MCP / HTTP 暴露

### 7.1 schema

新增：

```text
MCP/schemas/AssetDocument.md
```

schema 需要说明：

- 第一版支持 `AssetType: "GenericAsset"` 的通用 UObject/DataAsset 风格文档；
- 适合纯 UObject/DataAsset 风格资产；
- 不适合图、树、导入和结构化资产；
- sidecar 命名：`<AssetName>.assetdoc.json`；
- sidecar 路径如何推导目标 `/Game/...` 资产；
- AssetDocument 是 source-of-truth，`.uasset` 是 materialized output；
- editor hook 会同步 rename/move/duplicate/delete；
- `Class` 解析规则；
- `Properties` untyped 和 typed 格式；
- typed `type` 禁止 subtype，目标类型由 UPROPERTY 反射推断；
- create/update/create-or-update 行为；
- 常见错误。

### 7.2 新工具

新增独立 MCP/HTTP 入口，而不是把 `GenericAsset` 加入 `generate_assets`：

```text
apply_asset_document(document)
apply_asset_document_file(file_path)
get_asset_document_schema()
```

`apply_asset_document` 接收 inline JSON。`apply_asset_document_file` 从 `.assetdoc.json` 路径读取文档，并从 sidecar 文件位置推导目标 `/Game/...` 路径。`get_asset_document_schema` 返回 AssetDocument schema，而不是复用 `get_generator_schema`。

HTTP 层可以复用现有 AssetFactory server，但路由语义应保持独立，例如：

```text
POST /assetdocument/apply
POST /assetdocument/apply-file
GET  /assetdocument/schema
```

如果 MCP 侧已有静态 asset type 列表散落，本 spec 不要求把 AssetDocument 接入那些列表。implementation plan 可以顺手收敛公共 HTTP/MCP helper，但不要为了兼容旧 generator 枚举扩大第一版范围。

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
- 从同一个 sidecar 重复 apply 后结果稳定；
- 显式 `Target` 与 sidecar 路径不一致时失败。

如果现有 test framework 对 editor asset 创建成本较高，可以先加 smoke script，但 implementation plan 必须说明原因。

### 8.2 UBT

使用项目推荐 Development 编译：

```bash
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

如果在独立 worktree 中验证插件生效，需按项目约定使用临时 host project，避免主项目同名插件遮蔽 worktree 改动。

### 8.3 MCP / Editor smoke

使用 MCP `apply_asset_document` / `apply_asset_document_file`：

1. 创建一个没有专门 generator 的测试 DataAsset 子类。
2. 写入同目录 `.assetdoc.json`。
3. 更新 sidecar 中单个属性并重新 apply。
4. 通过 UE 读取资产验证未指定属性保持不变。
5. 重命名/移动/复制/删除受管理资产，验证 sidecar 跟随行为。
6. negative case 验证错误清晰。

### 8.4 文档验证

- `get_asset_document_schema()` 返回 schema。
- schema 示例与 AssetDocument compiler 行为一致。
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

### 9.3 Sidecar 与 uasset 可能失去同步

风险：用户通过 UE Editor 移动、重命名、复制、删除资产时，sidecar 没有跟随，导致文档指向错误资产。

缓解：

- 第一版就实现 editor hook；
- hook 只处理同名 sidecar 或 managed metadata；
- hook 操作失败时给出 warning；
- schema 明确 AssetDocument-managed 资产应通过 sidecar/apply 工作流维护。

### 9.4 过早扩展成大平台

风险：第一版把 factory、structured blocks、graph、import 都塞进去，导致实现失焦。

缓解：

- 第一版只做通用数据资产；
- 其他能力只在文档中预留；
- 后续每类能力单独 spec 和 implementation plan。

---

## 10. 验收标准

- `AssetDocument` Editor module 在插件中注册并加载成功。
- `AssetDocument` module 不依赖 `AssetGeneratorRegistry`，也不注册 `GenericAssetGenerator`。
- MCP/HTTP 提供 `apply_asset_document` / `apply_asset_document_file` / `get_asset_document_schema` 等价能力。
- 支持同目录 sidecar：`<AssetName>.assetdoc.json`。
- 支持从 sidecar 路径推导目标资产路径。
- 显式 `Target` 与 sidecar 路径不一致时失败。
- apply/generate 成功后能写入或更新 sidecar。
- 受管理资产 rename/move 时 sidecar 跟随并更新可选 `Target`。
- 受管理资产 duplicate 时 sidecar 被复制并更新可选 `Target`。
- 受管理资产 delete 时 sidecar 被删除或移入实现计划指定的位置，不留下误指向现有资产的 sidecar。
- 能创建至少一个当前无专门 generator 的 UObject/DataAsset 风格测试资产。
- 能更新已有 GenericAsset 的单个属性。
- 能用 typed property 设置基础类型、文本、向量、对象引用、class 引用或 enum 中的至少三类。
- typed `type` 中出现 subtype 时返回可读 validation error，例如拒绝 `Object:StaticMesh`。
- Object/Class/Enum 等 typed values 根据目标属性反射类型验证，不依赖用户提供 subtype。
- 无效 class、abstract class、属性不存在、类型不匹配都返回可读错误。
- 失败的 property patch 不保存半写入资产。
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
- 已明确 AssetDocument sidecar 是 source-of-truth，`.uasset` 是 materialized output。
- 已把 sidecar rename/move/duplicate/delete hook 纳入第一版范围。
- 已保留后续 thin adapter 路线，但没有把它放入第一版验收。
- 已明确动态 class loading 和反射 patch 是核心方向。
- 已明确避免大型 if/else、switch/case 和静态类型列表。
- 已明确 extract/adoption 不是第一版核心链路。
- 已包含 MCP、UBT、Editor smoke 和 negative validation 验收。
