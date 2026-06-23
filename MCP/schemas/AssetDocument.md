# AssetDocument Schema

AssetDocument is the generic asset sidecar format for reflected Unreal assets. It is not a `generate_assets` payload and it does not encode generator-specific subtypes in the `type` field. MCP tools forward documents to the `/assetfactory/assetdocument/*` HTTP routes; reflection, class lookup, property coercion, validation, and apply behavior live in the Unreal plugin.

## Structured Template Shape

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/Data/DA_Test",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {},
  "Body": {}
}
```

This is the canonical shape returned by `create_asset_document_template`. It is generic and profile-driven; it does not include `AssetType`.

Structured template fields:

- `SchemaVersion`: Use `1`.
- `Target`: The canonical asset package path the document owns, such as `/Game/Data/DA_Test`.
- `Class`: A native path or class name that the Unreal route resolves dynamically.
- `Action`: Use `Create`, `Update`, or `CreateOrUpdate`.
- `Definitions`: A map of reusable fragments keyed by stable names.
- `Properties`: Reflected asset properties to set.
- `Body`: Optional profile-specific structured sections. Use `inspect_asset_document_profile` to discover the sections for the target class.

## Definitions

`Definitions` is a document-local map of reusable fragments. Each key is a stable author-chosen identifier, and each value is a fragment object. Body sections and property values may refer to entries with `DefinitionRef` instead of repeating the same fragment.

Example:

```json
{
  "Definitions": {
    "WalkAnim": {
      "Kind": "AssetRef",
      "Path": "/Game/Animations/AS_Walk.AS_Walk"
    }
  },
  "Body": {
    "SlotAnimTracks": [
      {
        "SlotName": "DefaultSlot",
        "AnimTrack": {
          "AnimSegments": [
            {
              "AnimReference": {
                "Kind": "DefinitionRef",
                "Id": "WalkAnim"
              }
            }
          ]
        }
      }
    ]
  }
}
```

## Fragment Kinds

Supported generic fragment kinds:

- `AssetRef`: References an existing asset by path.
- `ClassRef`: References a class by native path, blueprint class path, or resolvable class name.
- `StructValue`: Carries a reflected struct payload.
- `EmbeddedObject`: Creates or updates an object owned by the asset being authored.
- `DefinitionRef`: References an entry in `Definitions` by `Id`.

Fragment validation and compilation are handled by Unreal-side adapters. MCP tools do not hardcode asset-specific fragment behavior.

## Profile And Template Workflow

Use the generic profile/template workflow for authoring:

1. Call `inspect_asset_document_profile` with a class path or asset path.
2. Read `DocumentShape`, `BodySections`, `FragmentKinds`, and `InternalAdapters` from the profile response.
3. Call `create_asset_document_template` with the class and target path.
4. Fill `Properties`, `Definitions`, and profile-specific `Body` sections.
5. Use `validate_asset_document` and `diff_asset_document` before `apply_asset_document` or `apply_asset_document_file`.

`extract_asset_document` is auxiliary. It is useful for inspecting an existing asset or creating a draft, but it is not required for authoring a new AssetDocument. Extracted `_Skipped` metadata is diagnostic/extract-only and must not be authored back into `Body`.

## Profile Policy Metadata

Profile and schema inspection expose policy metadata so tooling can understand the semantic ownership of structured `Body` regions without inventing an authoring language.

- `inspect_asset_document_profile` returns `RegionPolicies`. For a generic reflected profile with no exact profile policy, this is a valid empty array. For an exact profile such as `/Script/Engine.AnimMontage`, each entry describes a structured region such as `Body.Blend`, `Body.SlotAnimTracks`, or `Body.Notifies`.
- `get_asset_document_schema` returns top-level `RegionPolicyPresets` for built-in policy defaults such as `DefaultDiff`, `ManagedRegion`, and `ExtensionHook`.
- `get_asset_document_schema` also includes `RegionPolicies` on entries in `registered_profiles`, so consumers can discover exact-profile policy summaries without calling profile inspection for every class first.

Policy entries are metadata and semantic declarations only. They are not patch/op instructions, not an operations list, and not a patch/op DSL. Agents should still author normal AssetDocument `Properties`, `Definitions`, and `Body` fields, then use validate and diff before apply.

Stable policy fields include:

- `RegionId`: Stable dotted region id, such as `Body.Blend`.
- `BodyPath`: Body-relative path owned by the policy.
- `RegionKind`: Region shape, such as `Object`, `Array`, or `Timeline`.
- `DefaultSource`: Source used for default comparison, such as `CDO`.
- `ReducerMode`: Summary of how diff/default reduction is interpreted, such as `DefaultDiff` or `ManagedRegion`.
- `ApplyMode`: Summary of how the region is applied, such as `SetProperty`, `RebuildArrayRegion`, or `ExtensionHook`.
- `ManagedUePropertyPaths`: UE property paths managed by the region, when applicable.
- `ExtensionHookName`: Named extension hook for hook-backed policies, when applicable.

## Body Naming Rule

`Body` keys must use canonical Unreal/profile field names. Do not introduce abbreviations or short aliases.

Use names such as `SlotAnimTracks`, `AnimSegments`, `AnimReference`, and `CompositeSections`. Do not use abbreviated keys such as `Slots`, `Segments`, `Animation`, or `Sections`.

Unknown `Body` keys and extract-only keys such as `_Skipped` are rejected during validation for profile-owned bodies.

## UAnimMontage Profile

The `/Script/Engine.AnimMontage` profile owns the following `Body` keys:

- `Skeleton`: `null` or `AssetRef<USkeleton>`.
- `PreviewMesh`: `null` or `AssetRef<USkeletalMesh>`.
- `SlotAnimTracks`: array of slot animation tracks.
- `CompositeSections`: array of montage composite sections.
- `Notifies`: array of anim notify placements.
- `NotifyStates`: array of anim notify state placements.
- `Blend`: object with blend timing fields.

`SlotAnimTracks` entries use `SlotName` and `AnimTrack.AnimSegments`. Segment animation references use the canonical `AnimReference` field. Notify and notify-state placements use fragment objects so notify classes and embedded notify objects can be resolved dynamically.

The profile's `InternalAdapters` include `AnimMontageBody` and `AnimMontageNotifyPlacementAdapter`.

## UAnimSequence Profile

The `/Script/Engine.AnimSequence` profile is post-import only. It manages authored state on an existing `UAnimSequence`; it does not create raw animation tracks, author import settings, replace reimport workflows, or serialize compressed animation output. Use the generic AssetDocument MCP tools with profile inspection; no AnimSequence-specific MCP tool is required.

The profile owns these canonical `Body` keys:

- `References`: object for `Skeleton`, `RetargetSource`, and `RetargetSourceAsset` references/configuration.
- `Preview`: object for preview-only fields such as `PreviewMesh`.
- `Playback`: object for authored playback fields such as `RateScale`; derived length/sample fields are extract-only diagnostics.
- `Additive`: object for `AdditiveAnimType`, `RefPoseType`, `RefFrameIndex`, and `RefPoseSeq`.
- `RootMotion`: object for root motion settings, not root motion track data.
- `Compression`: object for compression configuration references/scalars, not compressed output.
- `Curves`: sparse add/update patches for sequence-owned float curves; deletion/clear is deferred.
- `Notifies`: array for point notify placements.
- `NotifyStates`: array for ranged notify-state placements.
- `NotifyTracks`: array for notify track names/order.
- `SyncMarkers`: array for authored sync markers.
- `Metadata`: array of `UAnimMetaData` embedded object fragments.
- `AssetUserData`: array of `UAssetUserData` embedded object fragments.

Excluded or diagnostic-only fields include `Import`, `RawTracks`, `CompressedData`, derived playback length/sample/frame-rate fields, referenced asset internals, thumbnails, and transient editor state. Authored `_Skipped` metadata is rejected; `_Skipped` is reserved for extracted diagnostics.

For post-import smoke and sidecar workflows, use a real existing sequence such as `/Game/AssetDocumentSmoke/AS_PostImportSidecarSmoke` with sidecar `C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.assetdoc.json`. If old names such as `RawTracks`, `Import`, or `CompressedData` appear, replace them with supported post-import sections (`References`, `Preview`, `Playback`, `Additive`, `RootMotion`, `Compression`, `Curves`, `Notifies`, `NotifyStates`, `NotifyTracks`, `SyncMarkers`, `Metadata`, or `AssetUserData`) or leave the raw/import/compressed data out of the sidecar entirely.

## UBlueprint Profile

The `/Script/Engine.Blueprint` profile represents ordinary `UBlueprint` assets. It does not cover `UWidgetBlueprint`, `UAnimBlueprint`, or specialized Blueprint-derived assets, and it does not use the legacy Blueprint generator path.

The profile owns these canonical `Body` keys:

- `ParentClass`: `ClassRef` for the generated class parent.
- `ImplementedInterfaces`: authoritative array of implemented interface class refs.
- `Variables`: authoritative array of member variables using `FEdGraphPinType`-shaped `Type`.
- `Components`: authoritative component tree and inherited/native component override declarations keyed by `{Name, OwnerClass}`.
- `ClassDefaults`: generated CDO default value deltas.
- `UbergraphPages`: authoritative Tier 1 EventGraph region using canonical graph specs. It currently supports reflected K2 nodes for event graphs and round-trips through validate, apply, extract, and diff.
- `FunctionGraphs`, `MacroGraphs`, and `Timelines`: reserved full-surface regions that still reject non-empty values until their graph/timeline-specific contracts land.

Missing entries in implemented UBlueprint regions delete or reset the corresponding Blueprint authoring surface; they do not preserve current `.uasset` state. Missing `Variables` entries remove member variables. Missing owned `Components` entries remove owned SCS component nodes. Missing inherited component entries remove inherited SCS override templates. Missing native component entries reset supported native component property overrides to the parent CDO baseline. Missing `ClassDefaults` entries reset supported generated CDO default deltas to the parent CDO baseline. Missing `UbergraphPages` graphs, nodes, links, or pin defaults remove or reset the corresponding managed EventGraph surface.

### UBlueprint Graph Regions

`Body.UbergraphPages` is an array of canonical `GraphSpec` objects. The current implementation is Tier 1 for ordinary Blueprint event graphs; `FunctionGraphs`, `MacroGraphs`, and `Timelines` remain explicit unsupported/deferred regions.

Canonical `GraphSpec` fields:

- `Name`: Required graph name, such as `EventGraph`. Names are unique within `UbergraphPages`.
- `Schema`: Required graph schema class path. Tier 1 uses `/Script/BlueprintGraph.EdGraphSchema_K2`.
- `GraphGuid`: Optional stable graph GUID when extraction can preserve one.
- `Category`: Optional graph category metadata.
- `Description`: Optional graph description metadata.
- `Signature`: Optional future signature object; reserved for Function/Macro graph work.
- `Nodes`: Required array of `NodeSpec` objects.
- `Links`: Required array of canonical expanded `LinkSpec` objects.

Canonical `NodeSpec` fields:

- `Id`: Required graph-local stable identity. It must match `^[A-Za-z_][A-Za-z0-9_-]*$` and is the identity used by links and diffs.
- `NodeGuid`: Optional extracted or author-provided UE node GUID. Apply may ignore a stale GUID when it points at a semantically different node.
- `Class`: Required node class path, such as `/Script/BlueprintGraph.K2Node_CallFunction`.
- `Capability`: Optional adapter capability hint. Tier 1 usually leaves this empty and resolves through class/member reflection.
- `Member`: Optional graph member reference. Event, call-function, and variable nodes use `MemberRef`; `K2Node_Self` does not.
- `PinOverrides`: Optional sparse array of pin default overrides. Each entry may include `Pin`, `Direction`, `Type`, `DefaultValue`, `DefaultObject`, `DefaultTextValue`, `Hidden`, and `AdvancedView`.
- `Position`: Optional `{ "X": number, "Y": number }` authoring layout metadata.
- `Comment`: Optional node comment text.

Canonical `LinkSpec` output always uses expanded endpoint objects:

```json
{
  "From": { "Node": "BeginPlay", "Pin": "then" },
  "To": { "Node": "Print", "Pin": "execute" }
}
```

Compact link input sugar is accepted only as authoring input when both endpoints are strings of the form `NodeId.PinId` and each token matches the graph id rules:

```json
{ "From": "BeginPlay.then", "To": "Print.execute" }
```

Extraction and canonical serialization emit the expanded object shape, not compact strings.

Graph-relevant `Definitions` kinds:

- `ClassRef`: Reusable class reference for graph schemas or reflected member owners when a profile accepts it.
- `AssetRef`: Reusable asset reference for graph-owned defaults that point at assets.
- `MemberRef`: Reusable member reference object with `OwnerClass` and `Name`; used by event, function, and variable node specs.
- `PinType`: Reusable reflected pin type shape for future typed pin authoring.
- `Literal`: Reusable literal value fragment for pin defaults or future graph metadata.
- `DefinitionRef`: Inline reference to an entry in `Definitions`; graph diff resolves equivalent inline and referenced values before comparison.

Tier 1 supported node classes:

- `/Script/BlueprintGraph.K2Node_Event`
- `/Script/BlueprintGraph.K2Node_CallFunction`
- `/Script/BlueprintGraph.K2Node_VariableGet`
- `/Script/BlueprintGraph.K2Node_VariableSet`
- `/Script/BlueprintGraph.K2Node_Self`

Unsupported graph fallback diagnostics are structured and actionable instead of raw UE graph dumps. They include:

- `Code`: Stable diagnostic code, such as `UnsupportedGraphNodeClass`, `UnsupportedGraphFunction`, or `UnresolvedGraphMemberReference`.
- `Path`: AssetDocument JSON path, such as `/Body/UbergraphPages/0/Nodes/0`.
- `Class`: Node class path when available.
- `Capability`: Capability hint when provided.
- `Member`: Member reference object when available.
- `Reason`: Human-readable reason.
- `SuggestedAction`: Next action, such as adding a thin node adapter or removing the node from the managed graph.

Unsupported existing graph contents are surfaced through extract-only `_Skipped.Graphs` evidence and diff `skipped` entries; they are not silently ignored and should not be authored back into `Body`.

Component entries use:

- `Scope`: `OwnedSCS`, `Inherited`, or `Native`.
- `Key`: object with `Name` and `OwnerClass`. `OwnerClass` is `Self` for Blueprint-owned SCS nodes, a generated parent class path for inherited SCS nodes, or the native owner class path for native default subobjects.
- `Class`: component class path.
- `AttachTo`: optional `{Name, OwnerClass}` key for `OwnedSCS` scene components.
- `Root`: optional boolean for `OwnedSCS` scene components.
- `Properties`: reflected component default value deltas.

Inherited and native component property overrides are supported. Inherited/native `AttachTo` and `Root` overrides are intentionally rejected with `UnsupportedInheritedComponentAttachRoot` until that capability has a separate UE-backed contract.

Example:

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/AssetDocumentSmoke/BP_BlueprintSidecarSmoke",
  "Class": "/Script/Engine.Blueprint",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {},
  "Body": {
    "ParentClass": {
      "Kind": "ClassRef",
      "Class": "/Script/Engine.Actor"
    },
    "ImplementedInterfaces": [],
    "Variables": [
      {
        "Name": "Health",
        "Type": {
          "PinCategory": "real",
          "PinSubCategory": "float"
        },
        "DefaultValue": "100.0"
      }
    ],
    "Components": [
      {
        "Scope": "OwnedSCS",
        "Key": {
          "Name": "Sensor",
          "OwnerClass": "Self"
        },
        "Class": "/Script/Engine.SphereComponent",
        "AttachTo": {
          "Name": "DefaultSceneRoot",
          "OwnerClass": "Self"
        },
        "Properties": {
          "SphereRadius": 500.0
        }
      }
    ],
    "ClassDefaults": {},
    "UbergraphPages": [],
    "FunctionGraphs": [],
    "MacroGraphs": [],
    "Timelines": []
  }
}
```

## WidgetBlueprint Profile

The `/Script/UMGEditor.WidgetBlueprint` profile represents exact `UWidgetBlueprint` assets through AssetDocument only. It does not use the legacy `WidgetBlueprint` generator schema, does not accept `AssetType: "WidgetBlueprint"`, and does not require a WidgetBlueprint-specific MCP tool.

The profile owns these canonical `Body` keys:

- `ParentClass`: required `ClassRef` resolving to a `UUserWidget` subclass. The default is `/Script/UMG.UserWidget`.
- `ImplementedInterfaces`: authoritative array of implemented interface refs. Task 1 accepts only empty arrays until the WidgetBlueprint interface adapter lands.
- `Variables`: authoritative array for non-widget Blueprint variables. Task 1 accepts only empty arrays.
- `ClassDefaults`: generated `UUserWidget` CDO default deltas. Task 1 accepts only an empty object.
- `WidgetTree`: object for `RootWidget` and `NamedSlotBindings`. Task 1 accepts `{ "RootWidget": null, "NamedSlotBindings": {} }` or an empty object.
- `Bindings`: authoritative `UWidgetBlueprint::Bindings` array. Task 1 accepts only empty arrays.
- `Animations`: authoritative package-owned `UWidgetAnimation` array. Task 1 accepts only empty arrays.
- `UbergraphPages`, `FunctionGraphs`, and `MacroGraphs`: WidgetBlueprint graph regions using the UBlueprint graph model. Task 1 accepts only empty arrays and exposes `UBlueprintGraph` policy hooks for the graph regions.
- `Palette`: stable palette metadata. Task 1 accepts only an empty object.
- `EditorOptions`: stable WidgetBlueprint editor/runtime behavior flags. Task 1 accepts only an empty object.
- `WidgetVariableGuids`: stable widget/animation variable GUID map. Task 1 accepts only an empty object.

Non-empty regions whose adapters are scheduled for later WidgetBlueprint implementation tasks fail validation or apply with `UnsupportedWidgetBlueprintRegion`; they are not silently preserved. Unknown `Body` keys are rejected.

Minimal empty WidgetBlueprint sidecar:

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/UI/WBP_Empty",
  "Class": "/Script/UMGEditor.WidgetBlueprint",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {},
  "Body": {
    "ParentClass": {
      "Kind": "ClassRef",
      "Class": "/Script/UMG.UserWidget"
    },
    "ImplementedInterfaces": [],
    "Variables": [],
    "ClassDefaults": {},
    "WidgetTree": {
      "RootWidget": null,
      "NamedSlotBindings": {}
    },
    "Bindings": [],
    "Animations": [],
    "UbergraphPages": [],
    "FunctionGraphs": [],
    "MacroGraphs": [],
    "Palette": {},
    "EditorOptions": {},
    "WidgetVariableGuids": {}
  }
}
```

## Legacy Reflected Apply Shape

The current reflected apply, validate, diff, extract, and sidecar file routes still use the legacy GenericAsset sidecar shape:

```json
{
  "SchemaVersion": 1,
  "AssetType": "GenericAsset",
  "Target": "/Game/Data/DA_Test",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Action": "CreateOrUpdate",
  "Properties": {
    "TestString": "hello",
    "TestInt": 42,
    "TypedFloat": {
      "type": "float",
      "value": 3.5
    }
  }
}
```

Required fields for legacy reflected apply/validate/diff paths:

- `SchemaVersion`: Use `1`.
- `AssetType`: Use `GenericAsset` for these legacy reflected paths.
- `Target`: Required in sidecar files. It is the canonical asset path the sidecar owns, such as `/Game/Data/DA_Test`.
- `Class`: Required for validation, diff, and apply. It can be a native path or class name that the Unreal route resolves dynamically.
- `Action`: Required for validation, diff, and apply. Use `Create`, `Update`, or `CreateOrUpdate`.

Common apply fields:

- `Properties`: Reflected asset properties to set.

Inline convenience:

- `Name` and `Path` may be used as convenience fields for inline documents only when the Unreal route/service receiving the document accepts them.
- MCP forwards the raw document to Unreal and does not synthesize `Target` from `Name` and `Path`.
- Sidecar files and portable examples should include explicit `Target`; `Name` and `Path` are convenience, not a replacement for the required sidecar `Target`.

## Properties

`Properties` supports untyped and typed forms:

```json
{
  "Properties": {
    "TestString": "plain untyped value",
    "TestInt": 12,
    "TypedName": {
      "type": "Name",
      "value": "CreatedName"
    }
  }
}
```

Untyped values are raw JSON values. Typed values use an object with:

- `type`: The property type hint, such as `Name`, `Text`, `float`, or another reflected type accepted by the Unreal route.
- `value`: The value to apply.

Rule: no subtype in `type`. For arrays, maps, sets, structs, objects, soft references, and class references, keep subtype information in the value shape accepted by the route rather than inventing `type` strings like `Array<int>` or `Object:SomeClass`.

## MCP Tools

- `get_asset_document_schema`: Reads this schema document from `MCP/schemas/AssetDocument.md`.
- `inspect_asset_document_target`: Calls `GET /assetfactory/assetdocument/inspect?class_or_asset=...` to inspect writable reflected properties for a class or asset.
- `inspect_asset_document_profile`: Calls `GET /assetfactory/assetdocument/profile?class_or_asset=...` to inspect the generic AssetDocument profile for a class or asset.
- `create_asset_document_template`: Calls `POST /assetfactory/assetdocument/template` with `Class` and `Target` to create a canonical generic AssetDocument template.
- `extract_asset_document`: Calls `POST /assetfactory/assetdocument/extract` and returns an AssetDocument draft for an existing asset.
- `validate_asset_document`: Calls `POST /assetfactory/assetdocument/validate` for exactly one inline document or sidecar file path.
- `diff_asset_document`: Calls `POST /assetfactory/assetdocument/diff` to compare exactly one inline document or sidecar file against the current asset state.
- `apply_asset_document`: Calls `POST /assetfactory/assetdocument/apply` with an inline AssetDocument JSON object.
- `apply_asset_document_file`: Calls `POST /assetfactory/assetdocument/apply-file` with a sidecar file path. The sidecar `Target` must match the path-derived `/Game` asset target; use `validate_asset_document` or `diff_asset_document` first when you need a preflight check.

AssetDocument MCP tools are generic. Do not add asset-specific AssetDocument tools; use profile inspection and reflected schema data to discover capabilities for a class or asset.

## Tool Arguments

Apply inline using the legacy reflected shape:

```json
{
  "document": {
    "SchemaVersion": 1,
    "AssetType": "GenericAsset",
    "Target": "/Game/Data/DA_Test",
    "Class": "TestDataAsset",
    "Action": "CreateOrUpdate",
    "Properties": {
      "TestString": "hello"
    }
  }
}
```

Apply sidecar file:

```json
{
  "file_path": "E:/GameDev/Project/Content/Data/DA_Test.assetdoc.json",
  "save_asset": true
}
```

The sidecar file itself should include `Target`:

```json
{
  "SchemaVersion": 1,
  "AssetType": "GenericAsset",
  "Target": "/Game/Data/DA_Test",
  "Class": "TestDataAsset",
  "Action": "CreateOrUpdate",
  "Properties": {
    "TestString": "hello"
  }
}
```

Inspect:

```json
{
  "class_or_asset": "/Script/AssetFactory.TestDataAsset"
}
```

Inspect profile:

```json
{
  "class_or_asset": "/Script/AssetFactory.TestDataAsset"
}
```

Create template:

```json
{
  "class": "/Script/AssetFactory.TestDataAsset",
  "target": "/Game/Data/DA_Test"
}
```

Example template result:

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/Data/DA_Test",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {}
}
```

Extract:

```json
{
  "asset_path": "/Game/Data/DA_Test",
  "diff_only": true,
  "include_all_writable": false
}
```

Validate or diff inline using the legacy reflected shape:

```json
{
  "document": {
    "SchemaVersion": 1,
    "AssetType": "GenericAsset",
    "Target": "/Game/Data/DA_Test",
    "Class": "TestDataAsset",
    "Action": "CreateOrUpdate",
    "Properties": {}
  }
}
```

Validate or diff sidecar:

```json
{
  "file_path": "E:/GameDev/Project/Content/Data/DA_Test.assetdoc.json"
}
```

## File Watcher Behavior

The Unreal-side file watcher owns sidecar synchronization. When a watched AssetDocument sidecar changes on disk, the editor route may apply or validate the sidecar according to the watcher implementation and should mark watcher-triggered requests internally. MCP tools do not implement watcher behavior; use `apply_asset_document_file`, `validate_asset_document`, or `diff_asset_document` to explicitly exercise the same HTTP-side sidecar paths.
