# StateTree Rich Binding Paths Deferred Polish

- Minor: `StateTreeBindingTypes.h` 的 `SourceLabel` 目前主要是未来 diagnostics/debug label，当前未被输出使用。
- Minor: `TryParseBindingPathAccess` 目前接受整个 `EPropertyBindingPropertyAccessType` enum；如果 schema/docs 只想承诺 `StructInstance`、`ObjectInstance`、`Unset`，后续可以收窄或加注释。
- Important follow-up: 当前 extraction diagnostics 只承诺 cyclic nested property function input graph 的 `StateTree.Binding.FunctionCycle` warning；unsupported binding graph diagnostics 以及 `FunctionCycle` 之外更丰富的 binding diagnostics 仍是 future diagnostics/polish，不能作为当前 final verification blocker 的已完成范围。
