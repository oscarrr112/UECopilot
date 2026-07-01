# BehaviorTree + BlackboardData AssetDocument Design

日期：2026-07-02

状态：待审核（正式 spec）

适用分支：`feature/asset-document-behaviortree-blackboard-spec`

基线：`12ea6ae87c37227cb73f65dd848b2a79c8ac2ee8`

## 1. 背景

AssetDocument 已经完成 public region runtime、Blueprint / WidgetBlueprint / animation-family profiles，以及 `UAnimBlueprint` 这类复杂 asset 的 exact profile 接入。下一环选择 `UBehaviorTree` 与 `UBlackboardData`，目标是定义新的 AssetDocument contract，并验证以下公共能力能否进入 AssetDocument runtime：

```text
sidecar + profile + policy + public region runtime + tree/key adapters + thin asset-specific hook
```

`UBehaviorTree` 的 authoring 语义天然依赖 `UBlackboardData`：

- BT asset 引用一个 Blackboard asset。
- BT task / decorator / service 经常包含 `FBlackboardKeySelector`。
- subtree BT 需要验证 parent / child blackboard compatibility。
- BT node diff path 不能依赖 UE transient editor graph index。

因此本 spec 把 BehaviorTree 与 BlackboardData 放入同一个完整 AssetDocument 目标中，但实现必须拆成 milestones。BlackboardData 是 first-class AssetDocument profile，不是 BT 的 inline 私有配置。

## 2. 设计结论

本 spec 定义 combined BT+BB AssetDocument 目标：

- 新增 exact-class profile：
  - `/Script/AIModule.BlackboardData` / `UBlackboardData::StaticClass()`
  - `/Script/AIModule.BehaviorTree` / `UBehaviorTree::StaticClass()`
- `UBehaviorTree` 只通过 `Body.Blackboard` 引用 `UBlackboardData`：
  - 形态：`{"Kind":"AssetRef","Path":"/Game/AI/BB_Enemy.BB_Enemy"}`
  - 第一版不支持 `Body.BlackboardInline`。
  - 如果需要同时创建 BB 和 BT，应由两个 AssetDocument sidecar 或 Definitions / workflow order 管理，不允许 BT capability 内创建私有 inline blackboard。
- BlackboardData profile 先落 `Body.Parent` 与 `Body.Keys`，并抽公共 key schema / lookup utility 供 BT 复用。
- BehaviorTree profile 使用 public tree adapter 管理 `Body.Tree`，BT capability 只负责：
  - lifecycle create/load
  - BlackboardAsset reference materialization
  - UE node object creation / reflection property apply hook
  - editor graph rebuild / post-apply repair
  - cross-region validation orchestration
- BT tree parser、blackboard key validation、identity diff、JSON Pointer diagnostic 不得写成 `FBehaviorTreeAssetDocumentCapability` 私有巨型 parser。

设计分层不是：

1. **BT-only profile**：只把 `BlackboardAsset` 当字符串引用，节点里的 key selector 由 BT 私有 helper 校验。这个方案短期快，但会阻断 BlackboardData / StateTree / AI asset 复用。
2. **Inline blackboard profile**：BT body 里直接嵌入 `BlackboardInline` 并由 BT apply 创建 BB。这个方案会混淆 asset ownership，导致 sidecar source-of-truth 不清楚。
3. **推荐方案**：一个 combined spec，一个 master implementation plan，milestones 先落 BlackboardData key profile / utility，再落 BehaviorTree lifecycle / tree adapter / BT-BB validation。

## 3. 目标

1. 让 `BlackboardData` 与 `BehaviorTree` 出现在 AssetDocument registered profiles、template、inspect、validate、apply、extract、diff 的常规通路中。
2. 定义 BlackboardData 的稳定 key authoring surface，支持 parent inheritance、key type、base class / enum metadata、stable identity 和 semantic diff。
3. 定义 BehaviorTree 的完整 stable tree authoring surface，支持 root/composite/task/decorator/service、root decorators、edge decorators、decorator logic、node properties 和 editor layout 的 validate/apply/extract/diff。
4. 抽出可复用的 blackboard key utility，供 BT key selector validation 使用，后续也可服务 StateTree 或其它 AI asset。
5. 抽出 public tree region adapter / helper，避免每个 tree-like asset 都写自己的 extractor/applier/reducer/diff helper。
6. 定义 BT editor layout 的独立 region，使视觉排布作为同一 spec / plan 的后续 milestone 完成，而不是混入 semantic tree。
7. 对暂不支持的 BT node class、decorator/service property、editor layout field、debug/cache field 返回 exact diagnostic 或明确 excluded，不静默吞掉。
8. 用 BB focused automation、BT focused automation、BT+BB integrated roundtrip、full AssetDocument automation、MCP tests 和 smoke runner 验证真实 asset contract。

## 4. 非目标

本 spec 不允许：

- 不新增 `create_behavior_tree`、`update_blackboard` 等专用 MCP tool。
- 不从非 AssetDocument payload 反推 schema；本 spec 只定义新的 AssetDocument body contract。
- 不支持 `Body.BlackboardInline`；BT 引用 BB 必须走 AssetRef。
- 不在 BT capability 中维护一套私有 blackboard key parser。
- 不把 BT editor graph layout 混入 `Body.Tree`。布局必须走独立 `Body.EditorLayout` region，并通过 tree node `Id` 关联语义节点。
- 不把 GraphNode GUID 当作 AssetDocument identity；GraphNode GUID 只能作为 UE editor rebuild 的内部细节。
- 不把 debug execution state、runtime instance memory 写入 sidecar。
- 不让 `Properties` 管理 `BlackboardAsset`、`RootNode`、`RootDecorators`、`RootDecoratorOps`、`Services`、`Decorators`、`DecoratorOps`、`Children`、`BTGraph` 等已由 `Body.*` 管理或明确排除的字段。
- 不通过硬编码节点类 switch 扩展行为；节点 class resolution 必须使用动态 class loading / reflection。
- 不在 `FAssetDocumentCanonicalJson` 中加入 BT/BB domain-specific normalization。

## 5. UE Surface Inventory

### 5.1 BlackboardData managed authored data

| UE surface | AssetDocument region | Handling |
| --- | --- | --- |
| `UBlackboardData::Parent` | `Body.Parent` | AssetRef<UBlackboardData> or null |
| `UBlackboardData::Keys` | `Body.Keys` | named array region，identity 为 `Name` |
| `FBlackboardEntry::EntryName` | `Body.Keys[].Name` | stable key identity，case-sensitive FName display preserved |
| `FBlackboardEntry::KeyType` | `Body.Keys[].Type` / `Body.Keys[].KeyTypeClass` | public key type schema utility |
| `FBlackboardEntry::bInstanceSynced` | `Body.Keys[].bInstanceSynced` | optional bool，default false |
| `UBlackboardKeyType_Object::BaseClass` | `Body.Keys[].BaseClass` | ClassRef when type is Object |
| `UBlackboardKeyType_Class::BaseClass` | `Body.Keys[].BaseClass` | ClassRef when type is Class |
| `UBlackboardKeyType_Enum::EnumType` / `EnumName` | `Body.Keys[].Enum` | AssetRef/Class-like enum reference, exact representation defined by key utility |

### 5.2 BehaviorTree managed authored data

| UE surface | AssetDocument region | Handling |
| --- | --- | --- |
| `UBehaviorTree::BlackboardAsset` | `Body.Blackboard` | AssetRef<UBlackboardData>，required for non-trivial BT |
| `UBehaviorTree::RootNode` | `Body.Tree.Root` | public tree adapter root |
| `UBehaviorTree::RootDecorators` | `Body.Tree.RootDecorators[]` | root-level subtree decorators |
| `UBehaviorTree::RootDecoratorOps` | `Body.Tree.RootDecoratorLogic[]` | root-level decorator logic operations |
| `UBTCompositeNode::Children` | `Body.Tree.*.Children[]` | `FBTCompositeChild` edge binding，order is authored semantic order |
| `UBTCompositeNode::Services` | `Body.Tree.*.Services[]` | service child region under owning composite |
| `FBTCompositeChild::ChildComposite` / `ChildTask` | `Body.Tree.*.Children[].Child` | exactly one child node per edge |
| `FBTCompositeChild::Decorators` | `Body.Tree.*.Children[].Decorators[]` | decorator child region on edge / child binding |
| `FBTCompositeChild::DecoratorOps` | `Body.Tree.*.Children[].DecoratorLogic[]` | AND / OR / NOT / Test decorator expression |
| `UBTTaskNode` | `Body.Tree.*` | leaf node kind |
| `UBTNode` editable properties | `Body.Tree.*.Properties` | reflected property object，schema by node class reflection |
| `FBlackboardKeySelector` properties | `Body.Tree.*.Properties.<Field>` | key selector object, validated against `Body.Blackboard` |
| subtree references such as `UBTTask_RunBehavior` | `Body.Tree.*.Properties.<Field>` | AssetRef<UBehaviorTree>, validate blackboard compatibility |

### 5.3 BehaviorTree editor layout authored data

| UE surface | AssetDocument region | Handling |
| --- | --- | --- |
| `UBehaviorTreeGraphNode` position | `Body.EditorLayout.Nodes[].Position` | optional editor layout region，identity 为 semantic node `Id` |
| graph comment boxes | `Body.EditorLayout.Comments[]` | optional authored presentation, identity 为 `Id` |
| comment bounds/text/color | `Body.EditorLayout.Comments[]` fields | editor presentation only, never BT runtime semantics |
| graph view metadata if stable and authored | `Body.EditorLayout.Graph` | optional object, supported fields must be explicitly listed |

### 5.4 Excluded / derived / runtime data

| UE surface | Reason |
| --- | --- |
| `UBehaviorTree::BTGraph` / `UBehaviorTreeGraph` node topology | editor visualization derived from runtime tree; rebuilt after semantic tree apply |
| `ExecutionIndex`, `TreeDepth`, runtime instance memory | derived/runtime data |
| debugger breakpoints, active node state, search data | debug/transient evidence |
| generated node display labels if derivable from class/properties | derived display data |
| unknown custom node internal caches | excluded unless reflected editable property proves authored ownership |

## 6. Body Schema

### 6.1 BlackboardData schema

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/AI/BB_Enemy",
  "Class": "/Script/AIModule.BlackboardData",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {},
  "Body": {
    "Parent": null,
    "Keys": [
      {
        "Name": "TargetActor",
        "Type": "Object",
        "BaseClass": {
          "Kind": "ClassRef",
          "Class": "/Script/Engine.Actor"
        },
        "bInstanceSynced": false
      },
      {
        "Name": "HasTarget",
        "Type": "Bool"
      },
      {
        "Name": "MoveLocation",
        "Type": "Vector"
      }
    ]
  }
}
```

Rules:

- `Body.Parent` is `AssetRef<UBlackboardData> | null`.
- `Body.Keys` is a managed named array. Missing `Body.Keys` means no authored key delta; when present, it is source-of-truth for local keys owned by this BlackboardData asset.
- Parent keys are visible to BT validation but are not re-authored in child `Body.Keys`.
- Duplicate key names within the same BlackboardData are rejected at `/Body/Keys/<Name>`.
- If a local key shadows a parent key, first implementation must reject it unless UE behavior is explicitly verified and documented.
- `Type` supports known aliases (`Bool`, `Int`, `Float`, `String`, `Name`, `Vector`, `Rotator`, `Object`, `Class`, `Enum`) and may also support explicit `KeyTypeClass` for custom `UBlackboardKeyType` subclasses.
- `Object` and `Class` keys require `BaseClass`.
- `Enum` keys require an enum reference.

### 6.2 BehaviorTree schema

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/AI/BT_Enemy",
  "Class": "/Script/AIModule.BehaviorTree",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {},
  "Body": {
    "Blackboard": {
      "Kind": "AssetRef",
      "Path": "/Game/AI/BB_Enemy.BB_Enemy"
    },
    "Tree": {
      "RootDecorators": [],
      "RootDecoratorLogic": [],
      "Root": {
        "Id": "RootSelector",
        "Class": "/Script/AIModule.BTComposite_Selector",
        "Properties": {},
        "Services": [],
        "Children": [
          {
            "Child": {
              "Id": "MoveToTarget",
              "Class": "/Script/AIModule.BTTask_MoveTo",
              "Properties": {
                "BlackboardKey": {
                  "Key": "TargetActor"
                }
              }
            },
            "Decorators": [
              {
                "Id": "HasTargetDecorator",
                "Class": "/Script/AIModule.BTDecorator_Blackboard",
                "Properties": {
                  "BlackboardKey": {
                    "Key": "HasTarget"
                  }
                }
              }
            ],
            "DecoratorLogic": [
              {
                "Operation": "Test",
                "Number": 0
              }
            ]
          }
        ]
      }
    },
    "EditorLayout": {
      "Nodes": [
        {
          "NodeId": "RootSelector",
          "Position": {
            "X": 0,
            "Y": 0
          }
        },
        {
          "NodeId": "MoveToTarget",
          "Position": {
            "X": 0,
            "Y": 220
          }
        }
      ],
      "Comments": []
    }
  }
}
```

Rules:

- `Body.Blackboard` is required when `Body.Tree` contains any node with blackboard key selectors or subtree compatibility checks.
- `Body.BlackboardInline` is not a valid key. It must return `UnknownBodyKey` or `UnsupportedBehaviorTreeRegion` if explicitly declared by policy during migration.
- `Body.Tree.Root` is required for authored tree apply.
- `Body.Tree.RootDecorators[]` and `Body.Tree.RootDecoratorLogic[]` map to `UBehaviorTree::RootDecorators` and `RootDecoratorOps`; they are required fields and may be empty arrays.
- Every node, root decorator, edge decorator, and service must have stable `Id` unique within the tree.
- `Class` must resolve dynamically to a non-abstract `UBTNode` subclass compatible with the node position:
  - root / internal composite: `UBTCompositeNode`
  - leaf task: `UBTTaskNode`
  - decorator: `UBTDecorator`
  - service: `UBTService`
- All loadable non-abstract compatible subclasses are in scope, including project-defined BT node classes. The implementation must not whitelist only built-in AIModule nodes.
- `Properties` is a reflected property object. Unsupported property type must return exact diagnostic, not be silently ignored.
- `Children[]` maps to `FBTCompositeChild`; each child entry must contain exactly one `Child` object. The child class decides whether UE stores it in `ChildComposite` or `ChildTask`.
- `Children[].Decorators[]` maps to `FBTCompositeChild::Decorators`.
- `Children[].DecoratorLogic[]` maps to `FBTCompositeChild::DecoratorOps`.
- `RootDecoratorLogic[]` and `DecoratorLogic[]` operations must use the UE `EBTDecoratorLogic` vocabulary: `Test`, `And`, `Or`, `Not`. `Invalid` is rejected in authored documents.
- decorator logic `Number` must be validated against the decorator expression shape. Empty decorators require empty decorator logic.
- `Services` are child arrays with their own stable `Id`.
- child order under `Children` is semantic and preserved.
- `Body.EditorLayout` is optional but in-scope for this spec. Missing `EditorLayout` means the implementation may use UE rebuild / auto layout; present `EditorLayout` is source-of-truth for supported editor presentation fields.
- `Body.EditorLayout.Nodes[].NodeId` must reference an existing semantic node, decorator, or service `Id` from `Body.Tree`. Layout cannot create, delete, or reorder BT nodes.
- `Body.EditorLayout` must never be used to validate runtime BT semantics. Semantic validation must come from `Body.Blackboard` and `Body.Tree`.
- Unsupported editor layout fields must fail validation with exact path/code rather than being silently ignored.

## 7. Region Policy

### 7.1 BlackboardData policies

| RegionId | Kind | Managed UE surface | Adapter / hook |
| --- | --- | --- | --- |
| `Body.Parent` | object/scalar ref | `UBlackboardData::Parent` | object/ref adapter + BB parent hook |
| `Body.Keys` | array | `UBlackboardData::Keys` | blackboard key named-array adapter |

### 7.2 BehaviorTree policies

| RegionId | Kind | Managed UE surface | Adapter / hook |
| --- | --- | --- | --- |
| `Body.Blackboard` | object/scalar ref | `UBehaviorTree::BlackboardAsset` | object/ref adapter + BT blackboard hook |
| `Body.Tree` | tree | `RootNode`, `RootDecorators`, `RootDecoratorOps`, `Children`, `Decorators`, `DecoratorOps`, `Services` | public tree adapter + BT node materializer |
| `Body.EditorLayout` | object | `UBehaviorTreeGraph` presentation fields | editor layout adapter + BT graph hook |

Policies must name managed UE surfaces so inspect/template output can explain ownership. `Properties` must reject writes for fields already owned by `Body.Blackboard`, `Body.Tree`, or `Body.EditorLayout`.

## 8. Public Runtime Composition

### 8.1 Required public utilities / adapters

Implementation should introduce or reuse these public-ish components:

1. `FAssetDocumentBlackboardKeySchemaUtils`
   - parse key JSON
   - resolve key type class dynamically
   - canonicalize key type representation
   - extract key type metadata
   - lookup keys through parent chain
   - validate key existence and type compatibility

2. `FAssetDocumentBlackboardKeyRegionAdapter`
   - named array adapter for `Body.Keys`
   - identity: `Name`
   - validate/apply/extract/diff local keys
   - duplicate local key rejection
   - parent shadowing policy

3. `FAssetDocumentTreeRegionAdapter`
   - generic tree lifecycle adapter shape
   - stable node identity and semantic diff path
   - recursive validate/apply/extract/diff hooks
   - child edge bindings for `Children[].Child`
   - child arrays for `RootDecorators`, `Children[].Decorators`, and `Services`
   - decorator logic arrays for `RootDecoratorLogic` and `Children[].DecoratorLogic`

4. `FAssetDocumentBehaviorTreeNodeMaterializer`
   - BT-specific hook used by tree adapter
   - dynamic node class resolution
   - reflected property apply/extract
   - `FBlackboardKeySelector` conversion through key utility
   - subtree BT AssetRef validation

5. `FAssetDocumentEditorLayoutRegionAdapter`
   - optional editor presentation adapter keyed by semantic node ids
   - validate layout references after `Body.Tree` ids are known
   - apply/extract/diff supported position/comment fields
   - keep layout diff independent from runtime tree semantic diff

These components must be composition-based. Do not add an inheritance base capability for BT/BB profiles.

### 8.2 Profile capabilities

Expected profile classes:

- `FBlackboardDataAssetDocumentProfile`
- `FBlackboardDataAssetDocumentCapability`
- `FBehaviorTreeAssetDocumentProfile`
- `FBehaviorTreeAssetDocumentCapability`

Both capabilities should use `FAssetDocumentBodyRegionDispatcher`. They may wire profile-specific hooks, but must not duplicate dispatcher behavior:

- Body object validation
- known key rejection
- adapter dispatch
- JSON Pointer escaping
- canonical JSON compare
- diff entry construction

## 9. Lifecycle

### 9.1 BlackboardData lifecycle

Create/update:

- Create or load target `UBlackboardData`.
- Resolve `Body.Parent` AssetRef if present.
- Apply `Body.Keys` as local key source-of-truth.
- Recreate `UBlackboardKeyType` instances under the Blackboard asset.
- Mark package dirty and notify asset registry as needed.

Extract:

- Extract `Parent` as AssetRef or null.
- Extract only local `Keys`, not inherited parent keys.
- Key order should preserve UE authored order unless canonicalization by identity is explicitly chosen.

Diff:

- Parent diff at `/Body/Parent`.
- Local key diff at `/Body/Keys/<Name>`.
- Parent-inherited key lookup may be used for BT validation, but inherited keys are not reported as local key additions.

### 9.2 BehaviorTree lifecycle

Create/update:

- Create or load target `UBehaviorTree`.
- Resolve `Body.Blackboard` AssetRef.
- Apply `Body.Tree` using the public tree adapter and BT materializer.
- Apply `Body.Tree.RootDecorators` / `RootDecoratorLogic`.
- Apply composite `Children[]` as `FBTCompositeChild` edge bindings, including `ChildComposite` / `ChildTask`, `Decorators`, and `DecoratorOps`.
- Rebuild editor graph / refresh tree after structural changes.
- Apply `Body.EditorLayout` after editor graph rebuild when the region is present.
- If `Body.EditorLayout` is absent, use UE rebuild / auto layout behavior for editor graph presentation.
- Validate no half-applied tree remains after failed preflight. Use staged preview/apply or rollback strategy when needed.

Extract:

- Extract `Blackboard` as AssetRef.
- Extract runtime authored tree from `RootNode`, not from editor graph layout.
- Extract root decorators / root decorator logic from `RootDecorators` and `RootDecoratorOps`.
- Extract each composite child as one edge binding with `Child`, `Decorators`, and `DecoratorLogic`.
- Extract stable node `Id`. If existing UE nodes lack authored identity, implementation must define deterministic identity generation and document collision behavior.
- Extract supported editor layout fields into `Body.EditorLayout` after the semantic tree ids are known.
- Layout extraction must map editor graph nodes back to semantic `Id`; it must not expose transient graph node object paths as identity.

Diff:

- Blackboard diff at `/Body/Blackboard`.
- Tree node diff at `/Body/Tree/<NodeId>`.
- Root decorator diff at `/Body/Tree/RootDecorators/<Id>`.
- Root decorator logic diff at `/Body/Tree/RootDecoratorLogic/<Index>` because UE logic op order is part of the decorator expression.
- Child additions/removals use semantic child node id path, not transient UE graph index.
- Child edge decorator diff at `/Body/Tree/<ParentId>/Children/<ChildId>/Decorators/<Id>`.
- Child edge decorator logic diff at `/Body/Tree/<ParentId>/Children/<ChildId>/DecoratorLogic/<Index>`.
- Layout diff at `/Body/EditorLayout/Nodes/<NodeId>` and `/Body/EditorLayout/Comments/<CommentId>`.
- Layout-only changes must not appear as semantic `Body.Tree` changes.

## 10. BT + BB Cross-Region Validation

BehaviorTree validation must compose BlackboardData key utility:

- `Body.Blackboard` must resolve to `UBlackboardData`.
- `FBlackboardKeySelector` fields in node `Properties` must reference existing keys from:
  - the referenced blackboard local keys
  - its parent chain
- key selector type compatibility must be validated when UE exposes allowed key filters or property metadata.
- node classes with BT asset references must validate referenced subtree blackboard compatibility:
  - child BT with no blackboard is allowed only if UE runtime permits it.
  - child BT with a blackboard must be related to parent blackboard according to verified UE compatibility behavior.
- errors must point to the authored property path, for example:
  - `/Body/Tree/MoveToTarget/Properties/BlackboardKey/Key`
  - `/Body/Tree/RunSubtree/Properties/BehaviorAsset`
- `Body.EditorLayout` validation must run after tree identity validation so dangling layout entries can report the missing semantic id.

Validation must not mutate the real asset.

## 11. Node Identity And Diff Paths

Required identity rules:

- Node `Id` is required for every authored tree node, decorator, and service.
- `Id` must be unique within the owning BehaviorTree.
- The same `Id` cannot appear as both task and decorator/service.
- Child edge identity is the referenced child node `Id`; there is no separate edge id unless future UE evidence proves multiple edges can target the same node instance.
- IDs are case-sensitive for display but duplicate detection should use `FName` normalization unless implementation proves UE treats them case-sensitive.
- Semantic diff paths:
  - root node: `/Body/Tree/<Id>`
  - child node: `/Body/Tree/<Id>`
  - root decorator: `/Body/Tree/RootDecorators/<Id>`
  - root decorator logic: `/Body/Tree/RootDecoratorLogic/<Index>`
  - edge decorator: `/Body/Tree/<OwnerId>/Children/<ChildId>/Decorators/<Id>`
  - edge decorator logic: `/Body/Tree/<OwnerId>/Children/<ChildId>/DecoratorLogic/<Index>`
  - service: `/Body/Tree/<OwnerId>/Services/<Id>`
  - property: `/Body/Tree/<Id>/Properties/<PropertyName>`
  - editor layout node: `/Body/EditorLayout/Nodes/<NodeId>`
  - editor layout comment: `/Body/EditorLayout/Comments/<CommentId>`

Array index paths are allowed only for malformed JSON before identity can be read.

## 12. Property Reflection Rules

BT node `Properties` should use existing dynamic style:

- resolve node class dynamically with `ClassFinderUtils`, `StaticLoadClass`, or equivalent runtime class loading.
- set and extract reflected properties through `PropertySetterUtils`, the existing AssetDocument reflected property runtime, or a focused public property helper if the current runtime lacks extract/diff support.
- do not include every possible BT node header just to support common nodes.
- `FBlackboardKeySelector` needs a dedicated conversion utility because it is semantic, not plain scalar.
- Asset references inside node properties should use AssetRef shape where possible.
- BT capability must not maintain a node-class or property-name whitelist for common nodes. It must support the complete authored editable reflected property surface that the shared AssetDocument property runtime can represent.
- If implementation discovers a reflected authored property kind that the shared runtime cannot yet represent, the plan must expand the shared runtime in the same BT+BB implementation chain or explicitly prove that the field is runtime/debug/cache/editor-derived and excluded from AssetDocument authorship.
- property failures must be exact path/code diagnostics. They are not allowed to become silent partial apply/extract behavior.

First implementation must support complete BT semantic roundtrip for authored editable node properties, including task/composite/decorator/service properties reachable through reflection and `FBlackboardKeySelector`.

## 13. Deferred And Excluded Boundaries

The following boundaries must be documented in `docs/superpowers/specs/asset-document-deferred-fields/` during implementation:

| Entry | Stage behavior | Cleanup trigger |
| --- | --- | --- |
| invalid BT node class | validation failure with `/Body/Tree/<Id>/Class` | class path is corrected to a loadable, non-abstract, position-compatible `UBTNode` subclass |
| non-authored reflected property | excluded with documented reason | field is proven authored and represented by shared property runtime |
| unsupported editor layout field | validation failure under `/Body/EditorLayout` | editor layout adapter supports apply/extract/diff |
| unsupported graph comment field | validation failure under `/Body/EditorLayout/Comments/<Id>` | comment field has stable UE storage and tests |
| advanced key selector filters | conservative validation | reliable UE metadata extraction and tests |
| custom `UBlackboardKeyType` metadata | supported only via `KeyTypeClass` plus reflected metadata if implemented | key type metadata adapter |
| subtree blackboard compatibility edge cases | conservative rejection | verified UE runtime behavior and tests |

## 14. Diagnostics

Required diagnostic examples:

| Case | Path | Code |
| --- | --- | --- |
| unknown BB Body key | `/Body/<Key>` | `UnknownBodyKey` |
| duplicate BB key | `/Body/Keys/<Name>` | `DuplicateBlackboardKey` |
| missing BB key name | `/Body/Keys/<Index>/Name` | `MissingBlackboardKeyName` |
| invalid BB key type | `/Body/Keys/<Name>/Type` | `InvalidBlackboardKeyType` |
| missing Object/Class BaseClass | `/Body/Keys/<Name>/BaseClass` | `MissingBlackboardKeyBaseClass` |
| BT Blackboard missing | `/Body/Blackboard` | `MissingBehaviorTreeBlackboard` |
| BT Blackboard unresolved | `/Body/Blackboard` | `UnresolvedBehaviorTreeBlackboard` |
| `BlackboardInline` provided | `/Body/BlackboardInline` | `UnknownBodyKey` or `UnsupportedBehaviorTreeRegion` |
| duplicate BT node id | `/Body/Tree/<Id>` | `DuplicateBehaviorTreeNodeId` |
| invalid node class | `/Body/Tree/<Id>/Class` | `InvalidBehaviorTreeNodeClass` |
| invalid child edge | `/Body/Tree/<ParentId>/Children/<ChildId>` | `InvalidBehaviorTreeChildEdge` |
| duplicate edge decorator id | `/Body/Tree/<ParentId>/Children/<ChildId>/Decorators/<Id>` | `DuplicateBehaviorTreeDecoratorId` |
| invalid decorator logic op | `/Body/Tree/<ParentId>/Children/<ChildId>/DecoratorLogic/<Index>/Operation` | `InvalidBehaviorTreeDecoratorLogic` |
| dangling decorator logic | `/Body/Tree/<ParentId>/Children/<ChildId>/DecoratorLogic/<Index>` | `InvalidBehaviorTreeDecoratorLogicShape` |
| invalid root decorator logic | `/Body/Tree/RootDecoratorLogic/<Index>` | `InvalidBehaviorTreeRootDecoratorLogic` |
| blackboard key missing | `/Body/Tree/<Id>/Properties/<Field>/Key` | `UnknownBlackboardKey` |
| key type mismatch | `/Body/Tree/<Id>/Properties/<Field>/Key` | `IncompatibleBlackboardKeyType` |
| subtree blackboard mismatch | `/Body/Tree/<Id>/Properties/<Field>` | `IncompatibleBehaviorTreeBlackboard` |
| duplicate layout node | `/Body/EditorLayout/Nodes/<NodeId>` | `DuplicateEditorLayoutNode` |
| dangling layout node | `/Body/EditorLayout/Nodes/<NodeId>` | `UnknownEditorLayoutNode` |
| invalid layout position | `/Body/EditorLayout/Nodes/<NodeId>/Position` | `InvalidEditorLayoutPosition` |
| duplicate layout comment | `/Body/EditorLayout/Comments/<Id>` | `DuplicateEditorLayoutComment` |

## 15. Milestones

The implementation plan must execute this combined spec through one branch chain. The plan may stage behavior by milestone, but it must cover the whole BT+BB target in this spec, including `Body.EditorLayout`.

1. **Blackboard key utility and tests**
   - key type resolution/canonicalization
   - parent-chain lookup
   - duplicate and shadowing policy
   - no production profile changes until utility tests exist

2. **BlackboardData AssetDocument profile**
   - profile/template/policies
   - `Body.Parent`
   - `Body.Keys`
   - apply/extract/diff focused automation

3. **Public tree region adapter**
   - generic tree JSON shape
   - identity/diff helper
   - malformed JSON diagnostics
   - fixture-level tests

4. **BehaviorTree profile and lifecycle**
   - exact profile/template/policies
   - `Body.Blackboard`
   - create/update/extract/diff are wired for the complete BT semantic tree contract

5. **BT node materialization**
   - root/composite/task/decorator/service apply/extract
   - root decorators and root decorator logic
   - child edge bindings with edge decorators and decorator logic
   - dynamic class loading
   - complete authored editable reflected property roundtrip
   - editor graph rebuild hook

6. **BehaviorTree editor layout region**
   - `Body.EditorLayout` schema/policy
   - node position apply/extract/diff keyed by semantic node `Id`
   - graph comment apply/extract/diff for supported fields
   - auto layout fallback when `Body.EditorLayout` is absent
   - dangling/duplicate layout diagnostics

7. **BT-BB integrated validation**
   - key selector validation
   - subtree BT AssetRef compatibility
   - full BT+BB roundtrip
   - semantic tree roundtrip and layout roundtrip remain separately diffable

8. **Final verification and report**
   - UBT
   - BB focused automation
   - BT focused automation
   - full `AssetFactory.AssetDocument`
   - `npm --prefix MCP test`
   - smoke runner or documented environment blocker

Each milestone must end with a checkpoint commit and a scoped review range. Review prompts must not include unrelated prior thread history.

## 16. Verification Requirements

Minimum focused automation:

- `AssetFactory.AssetDocument.BlackboardData.ProfileShape`
- `AssetFactory.AssetDocument.BlackboardData.Keys`
- `AssetFactory.AssetDocument.BlackboardData.ParentInheritance`
- `AssetFactory.AssetDocument.BehaviorTree.ProfileShape`
- `AssetFactory.AssetDocument.BehaviorTree.BlackboardReference`
- `AssetFactory.AssetDocument.BehaviorTree.Tree`
- `AssetFactory.AssetDocument.BehaviorTree.RootDecorators`
- `AssetFactory.AssetDocument.BehaviorTree.DecoratorLogic`
- `AssetFactory.AssetDocument.BehaviorTree.EditorLayout`
- `AssetFactory.AssetDocument.BehaviorTree.BlackboardKeySelectors`
- `AssetFactory.AssetDocument.BehaviorTree.SubtreeBlackboardCompatibility`

Required command-level verification:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/<bt-bb-host>/AVH1.uproject" -NoHotReload
```

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ReportExportPath=<report>" "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BehaviorTree; Automation RunTests AssetFactory.AssetDocument.BlackboardData; Quit"
```

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ReportExportPath=<report>" "-ExecCmds=Automation RunTests AssetFactory.AssetDocument; Quit"
```

```powershell
npm --prefix MCP test
```

If HTTP/editor smoke is blocked by host listener or editor environment, the report must mark it blocked and not count it as passed.

## 17. Success Criteria

The spec is complete only when:

- BlackboardData and BehaviorTree both have exact AssetDocument profiles.
- BT references BB through AssetRef only.
- BB key lookup and key type compatibility live in shared utility/adapter code, not BT capability private code.
- BT tree validate/apply/extract/diff uses public tree adapter composition.
- BT semantic roundtrip covers root decorators, root decorator logic, composite child edge binding, edge decorators, edge decorator logic, composite services, task/composite/decorator/service properties, subtree references, and key selectors.
- BT reflected property handling is dynamic and complete for authored editable properties; built-in-node whitelists or partial common-property coverage are not accepted as complete.
- invalid node/property/key/decorator-logic/layout cases produce exact path/code diagnostics.
- extract/diff paths are semantic and stable.
- final report records fresh UBT, automation, MCP, and smoke/blocker evidence.

## 18. Long-Term Maintenance Rules

When adding or extending BT/BB-like assets:

- If a region contains named keys, parent key inheritance, or key type compatibility, use `FAssetDocumentBlackboardKeySchemaUtils` or extend it first.
- If a region contains tree nodes, child arrays, decorators/services, or stable node ids, use `FAssetDocumentTreeRegionAdapter` or extend it first.
- Do not add a new asset-specific tree parser after this spec lands unless the tree semantics are proven unrelated to BT tree identity/diff/apply.
- Do not add inline referenced asset authoring inside another asset's body unless a separate spec proves ownership and sync semantics.
- If a second asset needs blackboard-like key selector validation, promote any remaining BT-specific helper into public utility before implementing the second copy.
