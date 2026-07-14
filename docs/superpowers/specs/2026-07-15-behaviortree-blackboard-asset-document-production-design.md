# BehaviorTree / BlackboardData AssetDocument 生产设计

**日期**：2026-07-15
**状态**：已批准执行（用户 production goal）
**权威基线**：`codex/asset-document-structured-capabilities@da7b1c8`
**实现分支**：`codex/finish-bt-bb-assetdocument`

## 1. 目标和完成定义

为精确类 `UBehaviorTree` 与 `UBlackboardData` 提供 production-ready AssetDocument profile，完整覆盖 template、schema、validate、apply、inspect、extract、diff、sidecar/writeback、canonicalization、保存重载和 HTTP/MCP 入口。完成必须同时满足：

- 两个 profile 的 managed authored 表面无缺口；
- 所有 invalid input 在 mutate 前被拒绝，或在后续不可避免失败时原子回滚；
- 新建与已有资产均通过 save → unload/reload → extract/diff；
- 默认 Unity Build、分别的 BT/BB focused automation、完整 `AssetFactory.AssetDocument`、MCP tests 与 8562 real HTTP smoke 全部通过；
- spec、surface inventory、deferred-fields、implementation plan、legacy audit 与 benchmark 同当前 HEAD 一致；
- worktree clean，所有结果提交。

完整字段分类以 `asset-document-surface-inventory/2026-07-15-behaviortree-blackboard.md` 为准。

## 2. 权威数据模型

### 2.1 Behavior Tree

`BTGraph + graph-node NodeGuid + wrapper-owned NodeInstance + graph topology + deterministic coordinates` 是 authored source-of-truth。`UBehaviorTree::RootNode` 及 runtime arrays 是由 `UBehaviorTreeGraph::UpdateAsset()` 产生的编译镜像。

AssetDocument 不同时维护 graph 和 runtime tree 两份真源。apply 顺序是：解析 semantic projection → 构建/更新 graph → 写 NodeInstance properties → 建立连接和 decorator bound graphs → 应用 editor layout/comments → 标准 graph rebuild → structural validation → save/reload verification。

### 2.2 Blackboard

`Parent + ordered local Keys + entry metadata + instanced KeyType class/properties` 是 authored source-of-truth。local key 数组严格保序；name 是稳定 identity，numeric ID 是 derived。

### 2.3 引用所有权

AssetDocument 只写引用本身，不递归修改 Blackboard parent、subtree BehaviorTree、node/key class definition、enum、struct、object/class defaults 所指向的资产。所有引用在 mutate 前完成 load/class/compatibility validation。

## 3. Canonical 文档形状

### 3.1 BlackboardData

```json
{
  "SchemaVersion": 1,
  "AssetType": "BlackboardData",
  "Target": "/Game/AI/BB_Enemy",
  "Action": "CreateOrUpdate",
  "Properties": {},
  "Body": {
    "Parent": "/Game/AI/BB_Base.BB_Base",
    "Keys": [
      {
        "Name": "TargetActor",
        "Description": "Current hostile target",
        "Category": "Targeting",
        "bInstanceSynced": false,
        "KeyTypeClass": "/Script/AIModule.BlackboardKeyType_Object",
        "KeyTypeProperties": {
          "BaseClass": "/Script/Engine.Actor",
          "DefaultValue": null
        }
      }
    ]
  }
}
```

`Keys` 是 ordered identity array：diff 以 Name 对齐内容，但 move/reorder 是语义变化，canonical writer 不排序。内建短 `Type` token 只可作为 template convenience，canonical extract 一律输出 `KeyTypeClass + KeyTypeProperties`。

### 3.2 BehaviorTree

```json
{
  "SchemaVersion": 1,
  "AssetType": "BehaviorTree",
  "Target": "/Game/AI/BT_Enemy",
  "Action": "CreateOrUpdate",
  "Body": {
    "BlackboardAsset": "/Game/AI/BB_Enemy.BB_Enemy",
    "Tree": {
      "GraphGuid": "D67AA99C4F1F4415A1352AB83EE1B6F1",
      "Root": {
        "Id": "FB9B930F4CC0D767B15B2CB63BE88154",
        "Class": "/Script/AIModule.BTComposite_Selector",
        "Properties": { "NodeName": "Choose action" },
        "Decorators": [],
        "Services": [],
        "Children": [],
        "Editor": {
          "Position": { "X": 0, "Y": 0 },
          "NodeComment": "",
          "bCommentBubblePinned": false,
          "bCommentBubbleVisible": false
        }
      },
      "Comments": []
    }
  }
}
```

规则：

- `Id` 是 canonical 32-hex `NodeGuid`，绝不复用 `NodeName` 或 UObject name；
- `Class` 是 concrete NodeInstance class；wrapper 由类别动态解析；
- `Children[]` 的顺序是执行语义；其 position 必须产生相同 X/Y 顺序；
- layout 缺省时按 `Children[]` 生成确定坐标，显式 layout 冲突或坐标并列时拒绝；
- selector property canonical 为 `{ "Key": "Name" }`；filter metadata 在 schema/inspect 中只读展示；
- composite decorator 以 nested expression graph 表达，持有稳定 GUID、logic/test topology、属性和 editor state；runtime ops 不出现在文档中；
- ordinary decorators、services、root decorators 保持数组顺序；
- Comments 完整表达持久 Comment Box 字段。

## 4. 动态类与反射属性

profile 不维护 task/decorator/service/key type class whitelist。解析流程必须：

1. 解析 canonical native/Blueprint/Angelscript/project class path；
2. 拒绝 missing、abstract、deprecated-for-new-input 或继承族错误的 class；
3. 通过 UE schema 选择正确 graph wrapper；
4. 枚举目标实例安全可编辑属性并生成 schema/template hints；
5. 通用 staged property runtime 处理 scalar、enum、name/string/text、class/object/soft references、struct、array/map/set 和 `FInstancedStruct`；
6. `Transient`、runtime/cache/debug、禁用实例编辑的字段不进入 authored runtime；
7. unknown/unsupported authored value 产生精确 JSON Pointer error，不能静默忽略。

Blackboard selector 是特例：只写 `SelectedKeyName`；AllowedTypes 和 None policy 从 concrete node constructor/CDO 得到并只用于验证与只读 inspection。

## 5. Apply pipeline 与原子性

### 5.1 Preflight

在任何真实资产 mutate 前完成 document/profile/unknown-field validation、class/reference load、graph topology、GUID uniqueness、layout/order、decorator expression、Blackboard inheritance/key/default、selector 和 subtree compatibility 检查。需要对象实例才能验证的属性先在 transient package 的 staging graph/key/node objects 上应用。

### 5.2 Commit

- 新建：在 staging 成功后创建真实 package/asset，materialize graph/keys，rebuild，validate，save。
- 更新：在写入前捕获完整 authored snapshot，包括 graph/subgraphs/nodes/subobjects/GUID/links/layout/comments、NodeInstance properties、Blackboard entries/key objects、资产 properties/references 和 canonical extract。
- 所有 apply、rebuild、compile、save 或 post-save verification 失败都恢复 snapshot；新对象重命名到 transient package 并清理，旧 subobject ownership 恢复。
- 写 sidecar/hash/writeback 必须在 uasset save/reload 成功之后；sidecar 写失败不能留下声称成功的部分状态。

### 5.3 Save/reload verification

成功返回前必须完成 package save、asset registry/package unload 或等价 fresh load、重新 extract、canonical equality/expected diff、引擎结构验证。只验证内存对象不算成功。

## 6. Canonicalization 与 diff

- object fields 按公开稳定规则输出；identity maps 可排序。
- Blackboard `Keys`、BT children/decorators/services 和 decorator logic operand order 保持 authored/semantic order。
- graph/node GUID canonical 为 uppercase 32-hex；class/object references 使用 canonical object path。
- float、color、vector/rotator、enum、name/text、`FInstancedStruct` 复用统一 canonical property representation。
- derived/runtime/cache/session 字段不进入 extract，因此不产生 diff。
- diff path 使用 identity：`/Body/Keys/TargetActor/...`、`/Body/Tree/Nodes/<Guid>/...`；reorder 明确输出 move/order semantic change。
- repeated apply 与 extract→apply→extract 必须得到空 semantic diff。

## 7. Schema、template 与 diagnostics

两个 exact-class profile 都必须支持九个公共入口：template、schema、validate、apply、inspect、extract、diff、sidecar apply、writeback/preview-diff 组合入口。Schema/template 必须公开：

- canonical shape、required/optional fields、dynamic class/reference format；
- selector filter metadata 为 read-only class policy；
- semantic order 与 layout 的耦合；
- source-proven exclusions；
- unknown/derived field rejection；
- create/update/create-or-update lifecycle。

diagnostic 包含 error code、message 和精确 JSON Pointer；同一输入在 validate、preview 和 apply preflight 得到一致结果。

## 8. 测试策略

按 RED→GREEN 编写，至少覆盖：

- profile/template/schema/exact-class/action/unknown field；
- 所有内建 Blackboard KeyType default/metadata、Struct `FInstancedStruct`、custom key type；
- key order、duplicate、parent shadow/cycle、empty/null/deprecated；
- native/Blueprint/Angelscript/project BT nodes 和 custom editable properties；
- base task/composite/decorator/service fields、selectors 与 filter rejection；
- main tree topology/order/layout/GUID/comments；
- composite decorator expression graph、cycle/arity/dangling；
- static/dynamic subtree compatibility；
- create/update atomicity including late property, rebuild, save and verification failure；
- canonical extract/diff/idempotency/save-reload；
- standard UE rebuild 与 derived mirror consistency；
- MCP protocol 和真实 8562 HTTP 的 BT/BB 独立资产 smoke。

测试数量由覆盖矩阵决定，不以旧分支现有数量为上限。

## 9. 候选分支策略

`origin/feature/asset-document-behaviortree-blackboard-impl` 仅作为候选实现资产。允许保留经过当前合同和测试证明正确的提交；错误边界必须改写或替换，包括把 runtime tree 当真源、以 `NodeName` 为 ID、可写 selector filters、Blackboard keys canonical sort、不完整 KeyType/Comment surface、只做内存回滚和缺少 save-reload verification。最终 legacy audit 逐提交记录 integrated/adapted/rewritten/replaced/discarded，不以“可三方合并”代替生产验收。

## 10. 验收命令

```sh
"/Volumes/External/Unreal/Engines/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh" AssetFactorySandboxEditor Mac Development -Project="/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/AssetFactorySandbox.uproject" -WaitMutex

"/Volumes/External/Unreal/Engines/UnrealEngine/Engine/Binaries/Mac/UnrealEditor-Cmd" "/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/AssetFactorySandbox.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.BehaviorTree;Quit" -TestExit="Automation Test Queue Empty"

"/Volumes/External/Unreal/Engines/UnrealEngine/Engine/Binaries/Mac/UnrealEditor-Cmd" "/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/AssetFactorySandbox.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.BlackboardData;Quit" -TestExit="Automation Test Queue Empty"

cd MCP && npm test
```

随后以独立 Editor 进程监听 `127.0.0.1:8562`，使用真实 HTTP/MCP 请求创建、更新、extract、diff 并重载两个互相引用但路径独立的 BT/BB smoke assets。
