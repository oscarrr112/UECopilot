# AssetDocument AnimMontage Projector 切片评估报告

## 结论

建议：只在 graph、tree、timeline 这类结构化资产上继续 projector/materializer 路线；不要把 AnimMontage 作为“通用 projector 能显著减少手写逻辑”的正例继续扩张。

本切片证明 read-side projector 可以保持 AssetDoc `Body` 文本结构不变，并能和当前 production extraction 对齐；但 Task 5 的关键观察也很明确：Projector 不能自动反向处理，Apply 必须走单独的 staged materializer/update path。为了避免失败半写入，staging 与 pre-resolve 是必要成本，不是临时实现细节。AnimMontage-specific write-side 代码增长显著，说明对 AnimMontage 这种写回规则密集的资产，projector 路线没有明显降低总体复杂度。

## 测试范围

本报告基于当前 worktree 文件内容与计划中的文件集合统计。记录到的 `HEAD` 与用户给定 base 相同，都是 `3c46798720ede814d38cee66ae80108100333a28`，因此 `3c46798..HEAD` 当前没有额外代码 diff；报告仍按当前 worktree 中已存在的 production 与 projector slice 文件做 LOC 和结构评估。

计划要求的目标测试覆盖包括：

- `AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.ExtractsCurrentBodyShape`
- `AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.MatchesProductionExtraction`
- `AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.UpdatesExistingMontage`
- `AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.MatchesProductionApplyUpdate`
- `AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.DiffReportsUpdatePaths`
- `AssetFactory.AssetDocument.AnimMontage`

实际测试文件还包含 invalid array、invalid blend、invalid section、invalid reference no-mutation、notify no-mutation 等验证点。这些测试对报告结论很重要：失败路径不会修改 montage，靠的是先 validate、再 staged load/materialize、最后统一写入。

本 task 是报告 task，未重新运行 UBT 或 Editor automation；Task 7 仍需要做完整编译与自动化验证。

## 代码量统计

统计口径：使用计划给出的 PowerShell 脚本，以 `Get-Content | Measure-Object -Line` 统计物理行数，包含空行、include、namespace、声明和测试辅助代码，不按有效代码行过滤。

| 分类 | LOC | 占 slice 总量 | 说明 |
| --- | ---: | ---: | --- |
| production | 1728 | 不适用 | 当前 `FAnimMontageAssetDocumentCapability` 与 notify placement adapter |
| sliceReusable | 408 | 30.2% | projection result、ref projector/materializer、struct array projector/updater、update plan |
| sliceSpecific | 941 | 69.8% | `FAnimMontageProjectorSlice` facade 与 AnimMontage-specific validate/stage/apply/diff |
| slice total | 1349 | 100.0% | reusable + specific |

文件明细：

| 分类 | 文件 | LOC |
| --- | --- | ---: |
| production | `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp` | 1106 |
| production | `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.h` | 22 |
| production | `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.cpp` | 570 |
| production | `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.h` | 30 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentProjectionTypes.cpp` | 24 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentProjectionTypes.h` | 25 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentRefProjector.cpp` | 26 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentRefProjector.h` | 9 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentRefMaterializer.cpp` | 37 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentRefMaterializer.h` | 10 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayProjector.cpp` | 97 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayProjector.h` | 21 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentUpdatePlan.cpp` | 24 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentUpdatePlan.h` | 24 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayUpdater.cpp` | 89 |
| sliceReusable | `Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayUpdater.h` | 22 |
| sliceSpecific | `Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.cpp` | 921 |
| sliceSpecific | `Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.h` | 20 |

## Read-side 与 write-side LOC 近似

统计口径：read-side 包含 `RefProjector`、`StructArrayProjector`、projection result 类型，以及 `AnimMontageProjectorSlice::ExtractBody` 和 JSON compare/diff 所需的投影比较辅助。write-side 包含 `RefMaterializer`、`StructArrayUpdater`、update plan 类型，以及 `ValidateBody`、`Stage*`、`ApplyBody`、`DiffBody` 中为了写回、预解析和变更路径服务的逻辑。`AnimMontageProjectorSlice.cpp` 中 namespace helper 很多是混用的，因此这里按函数职责做合理近似，不是逐行审计。

| 方向 | reusable LOC 近似 | AnimMontage-specific LOC 近似 | 合计近似 | 观察 |
| --- | ---: | ---: | ---: | --- |
| read-side projector | 178 | 170 | 348 | 读侧逻辑较薄，asset ref 投影与 primitive struct projection 有复用价值 |
| write-side materializer/updater/apply/diff | 230 | 771 | 1001 | 写侧占 slice 大头，且大部分是 AnimMontage-specific staging、校验、写入与 diff |

write-side 近似占 slice 总量约 74.2%。单独 materializer/update/apply/diff 的成本接近或超过 read-side 的 2.8 倍，说明这个切片的复杂度中心不在“如何读出 JSON”，而在“如何安全地从 JSON 写回 UE 对象”。

## AssetDoc Body JSON 示例

文本结构保持当前 AssetDoc 形状，不要求用户编辑新的 IR：

```json
{
  "Body": {
    "Skeleton": null,
    "PreviewMesh": null,
    "SlotAnimTracks": [],
    "CompositeSections": [],
    "Notifies": [],
    "NotifyStates": [],
    "Blend": {
      "BlendInTime": 0.15,
      "BlendOutTime": 0.25
    }
  }
}
```

实验 projector 还会输出 `_ProjectionMetrics`，但它只属于实验输出与测试比较前的诊断字段，不应进入 production AssetDoc 文本契约。

## 可复用单元分析

有复用价值的单元：

- `FAssetDocumentRefProjector`：把 `UObject*` 投影为 `{ Kind, Path, Class }`，适合 Skeleton、PreviewMesh、AnimReference 等引用字段。
- `FAssetDocumentRefMaterializer`：把 AssetDoc reference 解析为 UE 对象，并做 expected class 校验。它不能从 projector 自动反推，因为写回需要 load、class validation、path-based diagnostic。
- `FAssetDocumentStructArrayProjector`：适合同构 `UStruct` 数组的 primitive 字段投影，通过 override 处理资产特例。
- `FAssetDocumentStructArrayUpdater`：可以替换 struct array，并通过 `FPropertySetterUtils` 处理简单 primitive field。
- `FAssetDocumentUpdateResult` / metrics / changed paths：对 write-side 诊断和 diff 输出有复用意义。

限制也很清楚：

- AnimMontage 的 `SlotAnimTracks -> AnimTrack -> AnimSegments` 并不是简单扁平 struct array；`AnimReference`、`CompositeLength`、`NextSectionName`、blend time、notify 支持边界都需要资产层规则。
- 当前 `FAnimMontageProjectorSlice::ExtractBody` 实际上没有大规模使用 `FAssetDocumentStructArrayProjector`，而是手写 Slot、Segment、Section 的结构，这削弱了 reusable projector 的收益。
- write-side 的 staged object、staged blend、staged montage apply、ValidateSlotAnimTracks、StageSlotAnimTracks 等都是 AnimMontage-specific，难以直接搬到其他资产。

## Read-side vs write-side 分析

Read-side 的结果偏正面：它能输出与 production capability 对齐的 `Body` 字段，保持 `Skeleton`、`PreviewMesh`、`SlotAnimTracks`、`CompositeSections`、`Notifies`、`NotifyStates`、`Blend` 这些用户已见结构。对引用字段和简单 primitive 字段，projector primitive 可以减少重复。

Write-side 的结果偏负面：Apply 不是 projector 的逆运算。需要先 validate JSON shape，再 resolve/load asset refs，再构造 staged montage update，最后统一写回 `SetSkeleton`、`SetPreviewMesh`、`SlotAnimTracks`、`CompositeSections`、`BlendIn/BlendOut` 并记录 changed paths。这个 staging/pre-resolve 流程是为了保证 invalid reference 或 unsupported notify update 不会半写入 montage，属于必要安全成本。

Diff 也不能只靠 production path 直接复用。当前做法是重新 `ExtractBody`，对 desired fields 做 comparable JSON string 比较，再输出 `/Body/...` changed paths。这个逻辑对报告/工具层有价值，但仍是另一套 write-side 辅助代码。

## 文本结构是否变化

没有变化。切片目标就是不改 MCP、不改 schema、不改用户看到的 `.assetdoc.json` 格式。Projector slice 产生的 production-compatible 字段仍是当前 AnimMontage `Body` shape。

唯一额外字段是 `_ProjectionMetrics`，它用于实验诊断和 LOC/复用度观察；测试比较时会 normalize 掉，不应成为正式 AssetDoc 文档结构的一部分。

## update/apply/diff 与 production 对齐情况

Extraction：`MatchesProductionExtraction` 对 representative fixture 做 normalized JSON 比较，证明 read-side 输出可以与 production extraction 对齐。

Apply/update：`MatchesProductionApplyUpdate` 对 production-compatible update body 同时调用 production capability 与 projector slice apply，再提取并比较结果。这个范围内是对齐的。

Diff：`DiffReportsUpdatePaths` 能报告 `/Body/Blend`、`/Body/CompositeSections`、`/Body/SlotAnimTracks`，并能对相同 desired body 返回空 changed paths。它覆盖工具层需要的 changed-path 反馈，但还不是通用 diff 引擎。

边界：Notifies 与 NotifyStates 当前只支持空数组，非空更新会拒绝并保持 montage 不变。因此对 production 已有 notify placement adapter 的完整语义，projector slice 仍未覆盖。

## 反向 load/materialize 额外逻辑

反向写回至少需要这些不能由 projector 自动反推的逻辑：

- AssetRef 解析：`Path` 到 `UObject` 的 resolve/load、失败诊断、expected class 校验。
- null 与 missing 的语义区别：字段缺失表示不更新，字段为 null 表示显式清空引用。
- Slot/segment materialize：构造 `FSlotAnimationTrack`、`FAnimSegment`，解析 optional number，处理 `AnimPlayRate > 0`、`LoopingCount` 正整数等约束。
- Montage 派生状态：根据 segment end position 更新 composite length。
- Composite section 校验：`NextSectionName` 必须指向已有 section。
- Blend 写回：通过 `BlendIn.SetBlendTime` / `BlendOut.SetBlendTime` 写入，而不是普通字段赋值。
- 失败原子性：所有引用和结构先 staging，任何一处失败都不得修改现有 montage。
- Changed paths 与 dirty 标记：只有成功写入后才记录 changed paths 并 `MarkPackageDirty`。
- Unsupported notify 处理：当前非空 Notifies/NotifyStates 需要显式拒绝，避免伪装成成功。

这些逻辑本质上是 write-side materializer/update path，不是 read-side projector 的自然副产品。

## 明确建议

1. 保留 AssetDoc 作为用户工作层，不引入另一套用户可见 semantic IR；projector 只能作为从 raw/utxt/UE object 到 AssetDoc Body 的内部投影/验证工具。
2. 不要以 AnimMontage 为模板继续为每个 UE class 写一套 projector slice。AnimMontage-specific LOC 占 slice 约 69.8%，write-side 近似占 slice 约 74.2%，已经显示出 per-asset interpreter 的风险。
3. 可以继续投资 reusable read-side primitives，特别是 asset refs、primitive struct fields、array projection、diagnostics metrics。
4. write-side 只建议在 graph/tree/timeline 类资产继续试点，因为这些资产通常有稳定节点/边/轨道结构，materializer/updater 的复用收益可能高于 AnimMontage。
5. 对 AnimMontage production 路径，短期建议继续维护现有 capability 和 notify placement adapter；如果未来要吸收本切片成果，应只挑选 `FAssetDocumentRefMaterializer`、changed paths、staging/no-mutation 测试模式等小单元，而不是整体替换。
6. Task 7 必须继续跑 UBT 与 automation，确认 projector slice 测试和现有 AnimMontage 测试都通过后，再把这份报告作为路线判断依据。
