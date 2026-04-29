# StateTree Binding Rich Paths And Function Diagnostics Design

## Context

StateTree binding generation and extraction now cover ordinary bindings, property functions, GUID node references, and final smoke verification. The final verification spec deliberately left three binding edges deferred:

- Editor-authored `instanceStruct` / `access` path metadata is extract-only. Generator input still rejects those path segments.
- Property function input target paths are generation-limited to a single property name because `inputs` is currently a JSON object keyed by input name.
- Extraction avoids recursive property function graphs and skips unsupported multi-segment function inputs, but it does not emit rich diagnostics that explain what was skipped.

This spec removes the silent-skip and single-segment limits by making property function inputs explicit path entries, teaching generation to consume authorable rich path segment metadata, and adding extraction diagnostics for unsupported or cyclic function graphs. If UE does not expose an authoring API for a metadata field such as `access`, the field becomes a stable, tested negative contract rather than a silent ignore or vague future note.

## Goals

- Define a canonical JSON shape for property function inputs that supports multi-segment target paths and rich path segment metadata.
- Generate bindings from authorable `instanceStruct` / `access` path segment metadata when Unreal's property binding path APIs can validate and preserve the segment; otherwise produce stable, specific generation errors.
- Extract function input target paths without silently dropping multi-segment inputs.
- Emit machine-readable extraction diagnostics for unsupported function inputs, cyclic function graphs, and other skipped function graph edges.
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
- `instanceStruct` and `access` are accepted by the JSON parser.
- The generator should apply `instanceStruct` when Unreal exposes a stable setter or constructor path for the segment. If `access` is not authorable through available UE APIs, generation should fail with a clear error naming the segment and the unsupported field rather than silently ignoring it.

The implementation should prefer one small adapter around `FPropertyBindingPathSegment` construction so endpoint paths and function input target paths use identical behavior.

## Extraction Design

Extraction emits canonical function `inputs` arrays. For every binding whose target struct ID belongs to a property function node:

- If the target path can be represented, emit an input entry with `target` equal to `ExtractPathSegments(InputBinding.GetTargetPath())`.
- If the source is another property function, emit nested `function`.
- If the source is a normal endpoint, emit `source`.

Extraction no longer skips multi-segment function input target paths simply because generation used to require one segment.

The extractor should still protect against recursive graphs. Instead of silently returning a partial function with missing inputs, it should add a diagnostic entry to the extracted asset.

## Diagnostics Contract

Extracted StateTree JSON may include a top-level `diagnostics` object:

```json
"diagnostics": {
  "bindings": [
    {
      "severity": "warning",
      "code": "StateTree.Binding.FunctionCycle",
      "message": "Skipped cyclic property function input graph at binding 'duration-from-function'.",
      "bindingId": "duration-from-function",
      "path": "bindings[0].function.inputs[1]"
    }
  ]
}
```

Rules:

- Diagnostics are extraction-only metadata.
- `severity` is `"warning"` for skipped representable-but-unsupported graph edges and `"error"` only when extraction cannot produce a usable binding representation.
- `code` is stable and machine-readable.
- `message` is human-readable.
- `bindingId` is included when known.
- `path` points to the extracted JSON location when known.

Initial diagnostic codes:

- `StateTree.Binding.FunctionCycle`: recursive property function graph detected.
- `StateTree.Binding.UnsupportedFunctionInputTarget`: input target path cannot be represented.
- `StateTree.Binding.UnsupportedFunctionInputSource`: input source endpoint cannot be represented.
- `StateTree.Binding.UnsupportedPathMetadata`: rich path metadata is present but cannot be generated or round-tripped safely.

Round-trip comparison tools should ignore `diagnostics` by default when comparing generated JSON, but dedicated diagnostics tests should assert exact expected diagnostic codes.

## Fixtures And Smoke Coverage

Add fixtures:

- Positive: property function input with explicit single-segment array input targets.
- Positive: property function input with multi-segment target path.
- Positive: binding path with `instanceStruct` metadata when UE generation can author it.
- Negative: unsupported `access` or unsupported rich path metadata when generation cannot safely author it.
- Extraction-only: synthetic or editor-authored asset that produces `StateTree.Binding.FunctionCycle` or unsupported function diagnostics.

The final smoke manifest should grow only after the feature has focused tests. The final smoke should include:

- Generation success for canonical `inputs[]`.
- Round-trip stability for multi-segment function input targets.
- Negative validation for unsupported rich metadata.
- Summary/evidence that diagnostics are emitted rather than silent skip.

## Testing Strategy

Static and MCP tests:

- Add JSON parser tests for canonical `inputs[]`, legacy object input normalization, missing `target`, empty `target`, and source/function exclusivity.
- Add generation negative tests for malformed target paths and unsupported metadata.
- Extend round-trip checks so canonical `inputs[]` order is stable and diagnostics do not cause false mismatches.

Editor/MCP smoke:

- Use the existing `smoke:statetree-final` runner pattern after focused tests pass.
- Add one visual/openable asset that uses canonical function inputs.
- Add one negative fixture proving unsupported metadata fails with a stable error.
- Add extraction diagnostics evidence for cyclic or unsupported function graph edges.

## Migration And Documentation

Update docs and evidence notes so new fixtures use canonical `inputs[]`. Keep legacy object parsing only as a parser compatibility bridge during development; it should not appear in extracted output or newly-authored fixtures.

When this spec is complete and smoked, remove the deferred limits block added to ProjectRPG `AGENTS.md` if all three deferred items are resolved. If any item remains impossible because UE does not expose a stable authoring API, narrow the `AGENTS.md` note to the remaining unsupported field and include the reason.

## Open Risks

- UE may not expose a public setter for `access` metadata on `FPropertyBindingPathSegment`. If so, `access` should remain a documented negative case rather than being silently accepted.
- Building a real cyclic property function graph through generation may be impossible because generation itself is recursive and acyclic. Diagnostics may need a targeted extractor unit test or a deliberately editor-authored fixture.
- Canonical `inputs[]` changes fixture readability; tests should keep fixture names and diagnostics precise so failures remain easy to debug.
