# StateTree Binding Rich Paths And Function Diagnostics Design

## Context

StateTree binding generation and extraction now cover ordinary bindings, property functions, GUID node references, and final smoke verification. The final verification spec deliberately left three binding edges deferred:

- Editor-authored `instanceStruct` / `access` path metadata needed an authorable generator input contract.
- Property function input target paths are generation-limited to a single property name because `inputs` is currently a JSON object keyed by input name.
- Extraction avoids recursive property function graphs, but it did not emit a stable diagnostic for cyclic nested function inputs.

This spec removes the single-segment limit by making property function inputs explicit path entries, teaches generation to consume authorable rich path segment metadata, and adds extraction diagnostics for cyclic nested property function graphs. Unsupported function input target/source/path metadata diagnostics remain future diagnostics/polish, not part of the current final verification blocker completion scope.

## Goals

- Define a canonical JSON shape for property function inputs that supports multi-segment target paths and rich path segment metadata.
- Generate bindings from authorable `instanceStruct` / `access` path segment metadata, and produce stable generation errors for malformed or unknown metadata.
- Extract function input target paths without silently dropping multi-segment inputs.
- Emit a machine-readable extraction diagnostic for cyclic nested property function input graphs.
- Record unsupported extraction diagnostics as future diagnostics/polish work.
- Preserve round-trip stability for ordinary bindings, function bindings, GUID node IDs, and the final smoke fixture.

## Non-Goals

- Do not add new StateTree property function types.
- Do not change ordinary non-function binding endpoint JSON except for accepting richer path segments that already appear in extraction.
- Do not make diagnostics a generation input contract. Diagnostics are extraction metadata only.
- Do not attempt to author arbitrary editor-only graphs that Unreal cannot validate through `FPropertyBindingPath`.

## Canonical JSON Contract

Property function `inputs` becomes an array. Each input declares an explicit `target` path into the property function's instance data and exactly one value source: `source` or nested `function`.

```json
"function": {
  "type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
  "output": ["Result"],
  "inputs": [
    {
      "target": ["Left"],
      "source": { "kind": "rootParameter", "path": ["BaseDelay"] }
    },
    {
      "target": [
        { "name": "Nested", "instanceStruct": "/Script/StateTreeModule.SomeInstanceData" },
        "Value"
      ],
      "function": {
        "type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
        "output": ["Result"],
        "inputs": [
          {
            "target": ["Left"],
            "source": { "kind": "rootParameter", "path": ["BaseDelay"] }
          }
        ]
      }
    }
  ]
}
```

Rules:

- `inputs` canonical output is always an array.
- `target` is required and must contain at least one path segment.
- `target` uses the same segment grammar as endpoint `path`: string segments for simple properties, object segments for `name`, `guid`, `arrayIndex`, `instanceStruct`, and `access`.
- Each input must define exactly one of `source` or `function`.
- Existing object-shaped `inputs` may be accepted as legacy input during the transition, but extraction and new fixtures emit only the array form.
- Legacy object-shaped `inputs` must normalize to the same internal model as array entries with `target: [key]`.

## Generation Design

`FAFStateTreeBindingFunctionSpec::Inputs` should become an ordered array of input specs rather than a map keyed by name. Each input stores:

- `TargetPath`: `TArray<FAFStateTreeBindingPathSegmentSpec>`
- `Source` or nested `Function`
- `SourceIndex` / label data for precise diagnostics

Generation resolves target paths by applying `MakeBindingPathSegments()` to each input's `target`, then validating that path against the property function instance struct. This removes the current single-key restriction and allows nested paths into function instance data.

For rich path segment metadata:

- `guid` continues to call `SetPropertyGuid()` under editor-only data.
- `arrayIndex` continues to map into `FPropertyBindingPathSegment`.
- `instanceStruct` and `access` are accepted by the JSON parser and applied to generated path segments.
- Invalid `access` values and unknown `instanceStruct` paths fail with clear generation errors rather than being silently ignored.

The implementation should prefer one small adapter around `FPropertyBindingPathSegment` construction so endpoint paths and function input target paths use identical behavior.

## Extraction Design

Extraction emits canonical function `inputs` arrays. For every binding whose target struct ID belongs to a property function node:

- If the target path can be represented, emit an input entry with `target` equal to `ExtractPathSegments(InputBinding.GetTargetPath())`.
- If the source is another property function, emit nested `function`.
- If the source is a normal endpoint, emit `source`.

Extraction no longer skips multi-segment function input target paths simply because generation used to require one segment.

The extractor should still protect against recursive graphs. Instead of silently returning a partial function with missing inputs, it should add a diagnostic entry to the extracted asset.

## Diagnostics Contract

Extracted StateTree JSON may include a top-level `diagnostics` object when a nested property function input graph references an already visited function node:

```json
"diagnostics": {
  "bindings": [
    {
      "severity": "warning",
      "code": "StateTree.Binding.FunctionCycle",
      "message": "Skipped nested StateTree property function input because the graph references an already visited function node.",
      "bindingTarget": "Root/Idle/delay-task.Duration",
      "path": "$.bindings[0].function.inputs[0].function"
    }
  ]
}
```

Rules:

- Diagnostics are extraction-only metadata.
- Current implementation only emits `"warning"` diagnostics for cyclic nested property function input graphs.
- `code` is stable and machine-readable.
- `message` is human-readable.
- `bindingTarget` is included when known.
- `path` points to the extracted JSON location when known.

Current implemented diagnostic codes:

- `StateTree.Binding.FunctionCycle`: recursive property function graph detected.

Unsupported function input target/source/path metadata diagnostics remain future diagnostics/polish work. They are not part of the current final verification blocker completion scope and should not be treated as implemented contract codes until generation or extraction emits them and dedicated checks assert them.

Round-trip comparison tools should ignore `diagnostics` by default when comparing generated JSON, but dedicated diagnostics tests should assert exact expected diagnostic codes.

## Fixtures And Smoke Coverage

Add fixtures:

- Positive: property function input with explicit single-segment array input targets.
- Positive: property function input with multi-segment target path.
- Positive: binding path with `instanceStruct` / `access` metadata.
- Negative generation fixtures for malformed function input targets, bad `access`, and unknown `instanceStruct`.
- Extraction-only: synthetic or editor-authored asset that produces `StateTree.Binding.FunctionCycle`.

The final smoke manifest should grow only after the feature has focused tests. The final smoke should include:

- Generation success for canonical `inputs[]`.
- Round-trip stability for multi-segment function input targets.
- Negative validation for malformed or unknown rich metadata as generation errors.
- Evidence that cyclic nested property function input diagnostics are emitted.
- Normal smoke evidence that generated assets do not emit unexpected diagnostics.

## Testing Strategy

Static and MCP tests:

- Add JSON parser tests for canonical `inputs[]`, legacy object input normalization, missing `target`, empty `target`, and source/function exclusivity.
- Add generation negative tests for malformed target paths, bad `access`, and unknown `instanceStruct`.
- Extend round-trip checks so canonical `inputs[]` order is stable and diagnostics do not cause false mismatches.

Editor/MCP smoke:

- Use the existing `smoke:statetree-final` runner pattern after focused tests pass.
- Add one visual/openable asset that uses canonical function inputs.
- Add generation negative fixtures proving malformed or unknown metadata fails with stable errors.
- Add extraction diagnostics evidence for cyclic nested property function graph edges.
- Assert normal smoke extraction has no unexpected diagnostics.

## Migration And Documentation

Update docs and evidence notes so new fixtures use canonical `inputs[]`. Keep legacy object parsing only as a parser compatibility bridge during development; it should not appear in extracted output or newly-authored fixtures.

When this spec is complete and smoked, remove or narrow the deferred limits block added to ProjectRPG `AGENTS.md` so it only names future diagnostics/polish that remains outside the current final verification blocker scope.

## Open Risks

- Schema currently commits only to `StructInstance`, `ObjectInstance`, and `Unset`, while `TryParseBindingPathAccess` accepts the broader `EPropertyBindingPropertyAccessType` enum; this is tracked as deferred polish.
- Building a real cyclic property function graph through generation may be impossible because generation itself is recursive and acyclic. Diagnostics may need a targeted extractor unit test or a deliberately editor-authored fixture.
- Canonical `inputs[]` changes fixture readability; tests should keep fixture names and diagnostics precise so failures remain easy to debug.

## Implementation Status

- Branch: `feature/statetree-rich-binding-paths`.
- Canonical property function input shape is `function.inputs[]`; each input carries an explicit `target` path array plus exactly one of `source` or nested `function`.
- Legacy object-shaped function input maps remain generation compatibility input only. Extraction and new fixtures emit canonical `inputs[]`.
- Rich path segment metadata is authorable through generator input for `name`, `arrayIndex`, `guid`, `instanceStruct`, and `access`.
- Extraction diagnostics are read-only metadata under `diagnostics.bindings`; they are not authoring input, are ignored by default round-trip comparison, and the current implemented code is `StateTree.Binding.FunctionCycle`.
