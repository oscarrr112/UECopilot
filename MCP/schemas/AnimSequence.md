# AnimSequence Generator Schema

Defines the minimal/patch generator contract for Unreal animation sequence assets.

This contract is intended for creating or patching AnimSequence asset metadata, playback settings, curves, notifies, notify states, sync markers, and reflected properties. It does not support raw animation data import or authoring.

## Supported Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `AssetType` | string | Yes | Must be `"AnimSequence"` |
| `Name` | string | Yes | Asset name |
| `Path` | string | Yes | Content path, for example `"/Game/Animations"` |
| `Action` | string | No | `"Create"`, `"Update"`, or `"CreateOrUpdate"` |
| `Skeleton` | string | Yes (Create) / No (Update) | Skeleton asset path or loadable object reference |
| `PreviewMesh` | string | No | Skeletal mesh asset path used as the animation preview mesh |
| `FrameRate` | object | No | Display/sample frame rate, for example `{ "Numerator": 30, "Denominator": 1 }` |
| `NumberOfFrames` | number | No | Sequence frame count for minimal asset setup |
| `RateScale` | number | No | Playback rate multiplier |
| `Properties` | object | No | Additional AnimSequence properties set through reflection |
| `FloatCurves` | array | No | Float animation curve definitions |
| `Notifies` | array | No | Animation notify events |
| `NotifyStates` | array | No | Animation notify state ranges |
| `SyncMarkers` | array | No | Named sync marker entries |

## Non-Goals

The AnimSequence minimal/patch generator does not support raw animation import, compressed track data, FBX import, Interchange import, skeletal mesh creation, or production skeleton generation.

Use Unreal's import pipeline for source animation data. This MCP contract only describes structured metadata and patchable editor-facing fields.

## FloatCurves

`FloatCurves` contains named float curves. Each curve may include reflected curve flags and key data.

```json
{
  "FloatCurves": [
    {
      "Name": "Speed",
      "Keys": [
        { "Time": 0.0, "Value": 0.0 },
        { "Time": 0.5, "Value": 320.0 },
        { "Time": 1.0, "Value": 0.0 }
      ]
    }
  ]
}
```

## Notifies

`Notifies` describes point events on the sequence timeline.

```json
{
  "Notifies": [
    { "Name": "Footstep", "Time": 0.1, "TrackIndex": 0 }
  ]
}
```

## NotifyStates

`NotifyStates` describes ranged events with a start time and duration.

```json
{
  "NotifyStates": [
    { "Name": "Window", "Time": 0.15, "Duration": 0.1, "TrackIndex": 0 }
  ]
}
```

## SyncMarkers

`SyncMarkers` describes named synchronization points.

```json
{
  "SyncMarkers": [
    { "Name": "LeftFootDown", "Time": 0.2 },
    { "Name": "RightFootDown", "Time": 0.7 }
  ]
}
```

## Minimal Example

```json
{
  "AssetType": "AnimSequence",
  "Name": "AS_Idle_Minimal",
  "Path": "/Game/Animations",
  "Skeleton": "/Game/Characters/Hero/SKEL_Hero.SKEL_Hero",
  "FrameRate": { "Numerator": 30, "Denominator": 1 },
  "NumberOfFrames": 1
}
```

## Patch Example

```json
{
  "AssetType": "AnimSequence",
  "Action": "Update",
  "Name": "AS_Attack",
  "Path": "/Game/Animations",
  "PreviewMesh": "/Game/Characters/Hero/SK_Hero.SK_Hero",
  "RateScale": 1.15,
  "Properties": {
    "bEnableRootMotion": true
  },
  "FloatCurves": [
    {
      "Name": "AttackAlpha",
      "Keys": [
        { "Time": 0.0, "Value": 0.0 },
        { "Time": 0.3, "Value": 1.0 },
        { "Time": 0.8, "Value": 0.0 }
      ]
    }
  ],
  "Notifies": [
    { "Name": "Swing", "Time": 0.25, "TrackIndex": 0 }
  ],
  "NotifyStates": [
    { "Name": "DamageWindow", "Time": 0.3, "Duration": 0.2, "TrackIndex": 0 }
  ],
  "SyncMarkers": [
    { "Name": "Impact", "Time": 0.42 }
  ]
}
```
