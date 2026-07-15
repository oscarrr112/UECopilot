# BehaviorTree / BlackboardData deferred-fields 结论

**日期**：2026-07-15
**状态**：无 authored deferred fields

本生产切片没有已确认 authored 但延期的字段。`UBehaviorTree`、`UBlackboardData`、BT graph、composite-decorator graph、节点实例、Blackboard entry/key type、布局与注释的 authored 表面必须在本切片完成 apply/extract/diff/save-reload。

以下内容不是延期项，而是有源码证据的非 authored 边界：

- Behavior Tree runtime mirror：`RootNode`、Children、Services、RootDecorators、DecoratorOps；
- execution/runtime state：ExecutionIndex、MemoryOffset、TreeDepth、ParentNode、instance flags；
- injected subtree decorator preview、subtree version/path cache；
- graph 的自动生成 pin 细节；AssetDocument 只管理逻辑连接；
- debugger、breakpoint、错误显示和运行计数；
- `LastEditedDocuments`、视口/选择等 session 或 per-user 状态；
- Blackboard `ParentKeys`、FirstKeyID、numeric KeyID、同步缓存；
- selector 的 AllowedTypes、SelectedKeyType、SelectedKeyID、`bNoneIsAllowedValue` class policy；
- `FValueOrBlackboardKey_*` 的 hidden cached Key ID；公开 key name 与 literal/default 仍是当前切片 authored 表面，Blackboard/key order 变化时缓存必须失效并重解；
- deprecated `UBlackboardKeyType_NativeEnum` 新建输入；旧资产只允许 extract diagnostic/migration evidence；
- CommentDepth、NodesUnderComment 等 derived cache；
- 被引用 Blackboard、subtree、class、enum、struct、object/class default referent 的内部内容。

这些边界分别通过 schema rejection、extract omission、standard rebuild 和 save-reload 自动化固定，不作为未来补字段的借口。若实现期间发现新的稳定 authored 字段，必须先更新 surface inventory 与主设计并在当前切片实现，不能追加到本文件。

仅含 `Body.BlackboardAsset` 的 sparse `Update` 也不是 deferred lifecycle：它只对已存在 BT 做 overlay，并必须验证 retained tree。enum decorator 的 retained enum/value mapping 若因 Blackboard 替换失效，必须在同一请求显式重写 Tree property，否则拒绝；实现不得把该语义缺口记为 deferred。
