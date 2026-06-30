# AssetDocument Fragment Array Region Adapter Design

日期：2026-07-01

状态：待审核

适用分支：`feature/asset-document-object-field-schema-dispatcher-migration`

## 1. 背景

AssetDocument public region runtime 已经完成 object field schema dispatcher、preview apply diff adapter、identity array diff helper、graph wrapper adapter 等公共层。下一环是 fragment object array 的公共 adapter。

当前 AnimSequence 和 AnimMontage 都有 fragment object array 形态：

- AnimSequence `Metadata`
- AnimSequence `AssetUserData`
- AnimMontage `Metadata`
- AnimMontage `SectionMetadata` 中每个 section 对应的 metadata array

这些区域重复了相似的生命周期逻辑：

- Body key 必须是 JSON array。
- array entry 必须是 JSON object。
- diagnostic path 使用 `/Body/<Region>/<Index>`。
- validate 阶段只检查 fragment shape 或 fragment reference。
- apply 阶段需要 compile fragment，再由 profile 挂到具体 UE asset。
- extract 阶段从现有 UObject/struct/value 生成 fragment JSON array。
- diff 阶段通常是 canonical JSON compare 或 preview-apply-diff 结果。
- explicit empty array 表示 authoritative clear/replace。

真正不能公共化的是：

- `FAssetDocumentFragmentCompiler` 的具体使用方式。
- expected base class / struct / role。
- compiled fragment 是否必须产生 UObject。
- UObject outer、rename、managed prefix、rollback。
- asset-specific 写回 API，例如 `UAnimSequence::AddMetaData`、AssetUserData reflection、Montage section metadata。
- apply 后 cache repair、track rebuild、package dirty。

因此本 spec 的核心是：公共 adapter 只统一 array region lifecycle；fragment compiler、fragment materialization 和 UE ownership 通过组合 hooks 接入。

## 2. 目标

1. 新增 `FAssetDocumentFragmentArrayRegionAdapter`，实现 `IAssetDocumentRegionAdapter`。
2. 新增 `FAssetDocumentFragmentArrayHooks`，以组合方式接入 profile-specific fragment 逻辑。
3. 统一 fragment array 的 validate / preflight / apply / extract / diff 生命周期。
4. 统一 JSON array/object shape validation、entry path、empty array、delete/replace 行为。
5. 第一版迁移 AnimSequence `Metadata` 和 `AssetUserData`。
6. 第一版迁移 AnimMontage `Metadata`；AnimMontage `AssetUserData` 仅在确认现有 profile 已暴露 `Body.AssetUserData` 时迁移。
7. 第一版让 AnimMontage `SectionMetadata` 的 per-section array 复用 fragment array utility，但不把 section map 语义塞进基础 adapter。
8. 保持 public AssetDocument body schema 和 fragment JSON schema 不变。

## 3. 非目标

本 spec 明确不做：

- 不修改 `FAssetDocumentFragmentCompiler` 的 adapter dispatch 规则。
- 不把 fragment compiler 合并进 region adapter。
- 不让公共 adapter 直接创建、rename、attach、remove 任何 UObject。
- 不让公共 adapter 知道 `UAnimMetaData`、`UAssetUserData`、`UAnimSequence`、`UAnimMontage`。
- 不处理 timeline placement key、notify time/duration/track identity。
- 不统一 AnimSequence notifies / notify states 里的 timeline placement。
- 不把 AnimMontage `SectionMetadata` 的 section-name validation 做进 base array adapter。
- 不改变 `Definitions`、`EmbeddedObject`、`DefinitionRef`、`AssetRef` 等 fragment kind 的 public JSON shape。
- 不改变 managed object naming prefix。
- 不把 diff 策略从 existing preview-apply-diff 强行改为 identity diff。

## 4. 现状梳理

### 4.1 AnimSequence

`AnimSequenceAssetDocumentCapability.cpp` 目前包含：

- `ValidateManagedObjectFragmentShape`
- `CompileManagedObjectFragments`
- `ParseManagedObjectFragmentArray`
- `MoveManagedObjectsToSequence`
- `ReplaceManagedAssetUserDataByReflection`
- `ExtractManagedMetadata`
- `ExtractManagedAssetUserData`

`Metadata` 和 `AssetUserData` 的重复点：

- 都从 Body array 读取 fragment objects。
- 都对 fragment object 做 shape validation。
- apply 时都 compile 成 managed UObject。
- apply 时都 move/rename 到 AnimSequence outer。
- extract 时都过滤 managed-prefix object 并输出 fragment array。

差异点：

- `Metadata` expected base class 是 `UAnimMetaData`，写回使用 `Sequence->RemoveMetaData` / `Sequence->AddMetaData`。
- `AssetUserData` expected base class 是 `UAssetUserData`，写回使用 reflection 替换 `UAnimationAsset.AssetUserData`。
- managed prefix 和 duplicate key diagnostic 不同。
- rollback 和 replacement 仍是 AnimSequence-specific ownership hook。

### 4.2 AnimMontage

`AnimMontageAssetDocumentCapability.cpp` 目前包含：

- `CompileMetadataArray`
- `MoveMetadataArrayToMontage`
- `ValidateMetadataArrayShape`
- `ValidateMetadataObjectProducingFragment`
- `ValidateMetadataArray`
- `ParseMetadataRegions`
- `ExtractMetadataObject`

`Metadata` 和 `SectionMetadata` 的重复点：

- array entry 必须是 object。
- fragment 必须是 `EmbeddedObject` 或 `DefinitionRef`。
- expected object base class 是 `UAnimMetaData`。
- apply 时 compile 成 metadata UObject。
- extract 时输出 fragment JSON。

差异点：

- `Metadata` 是 `/Body/Metadata` 的单个 array。
- `SectionMetadata` 是 `/Body/SectionMetadata/<SectionName>` 下的 array map。
- `SectionMetadata` 需要先验证 section target 是否存在。
- Montage metadata outer move 和 section assignment 是 profile-specific hook。

AnimMontage `AssetUserData` 若当前实现中仍未和 AnimSequence 完全等价，第一版 spec 允许只建立 adapter contract 和 migration entry，implementation plan 需要先确认现有 capability 的实际支持程度，再决定是否同 task 迁移。

## 5. 设计原则

### 5.1 组合优先

`FAssetDocumentFragmentArrayRegionAdapter` 不作为 profile adapter 的基类，也不通过继承暴露 override 点。

它由 config + hooks 构造：

- config 描述 region id、body path、json pointer、adapter name。
- hooks 描述 profile 如何 validate/compile/apply/extract/diff fragments。

公共 adapter 不保存 profile asset state，不创建 UObject，不知道 UE 类型。

### 5.2 Adapter 只管 array lifecycle

公共 adapter 可以负责：

- `SupportsRegion`
- `GetSchemaHint`
- desired value 必须是 JSON array
- 每个 entry 必须是 JSON object
- entry path 构造：`/Body/<Region>/<Index>`
- validate hook dispatch
- preflight hook dispatch
- apply hook dispatch
- extract hook dispatch
- diff hook dispatch 或默认 canonical JSON diff
- empty array 作为 explicit replace/clear 输入传递给 hook
- missing hook 的明确 failure

公共 adapter 不可以负责：

- fragment kind 白名单。
- definition ref resolution。
- compile 成 UObject/struct/value。
- duplicate explicit object name 判断。
- managed object rename / rollback。
- asset user data reflection。
- montage section lookup。
- post-apply repair。

### 5.3 SectionMetadata 不污染基础 adapter

AnimMontage `SectionMetadata` 是 map of arrays，不是单个 body array region。

第一版处理原则：

- `FAssetDocumentFragmentArrayRegionAdapter` 只处理一个 concrete array region。
- `SectionMetadata` profile hook 负责遍历 section map 和 section target validation。
- 每个 section array 可以复用公共 utility，例如 `ValidateFragmentArrayShape` / `CompileFragmentArray` / `ExtractFragmentArray`，并使用 `/Body/SectionMetadata/<SectionName>/<Index>` path。
- 不新增一个过早泛化的 `MapOfFragmentArraysAdapter`，除非后续第二个 asset 也出现同形态。

## 6. Public Runtime API

新增私有 runtime 文件：

- `Source/AssetDocument/Private/Regions/AssetDocumentFragmentArrayRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentFragmentArrayRegionAdapter.cpp`

第一版保持在 `Private/Regions`，原因与 graph wrapper 一致：它是 profile implementation helper，暂不承诺外部插件 API。

建议接口：

```cpp
struct FAssetDocumentFragmentArrayRegionConfig
{
	FName AdapterName;
	FName RegionId;
	FString BodyPath;
	FString JsonPointer;
	FString SchemaLabel;
};

struct FAssetDocumentFragmentArrayEntry
{
	int32 Index = INDEX_NONE;
	FString JsonPointer;
	TSharedRef<FJsonObject> FragmentObject;
};

struct FAssetDocumentFragmentArrayHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext& Context,
		const TArray<FAssetDocumentFragmentArrayEntry>& Entries)> Validate;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext& Context,
		const TArray<FAssetDocumentFragmentArrayEntry>& Entries)> Preflight;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext& Context,
		const TArray<FAssetDocumentFragmentArrayEntry>& Entries,
		bool& bOutChanged)> Apply;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext& Context,
		TArray<TSharedRef<FJsonObject>>& OutEntries)> Extract;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext& Context,
		const TArray<FAssetDocumentFragmentArrayEntry>& DesiredEntries,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)> Diff;
};

class FAssetDocumentFragmentArrayRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	FAssetDocumentFragmentArrayRegionAdapter(
		FAssetDocumentFragmentArrayRegionConfig InConfig,
		FAssetDocumentFragmentArrayHooks InHooks);

	// IAssetDocumentRegionAdapter
};
```

## 7. Runtime Behavior

### 7.1 SupportsRegion

`SupportsRegion` 匹配：

- `Context.RegionId == Config.RegionId`
- 或 `Context.BodyPath == Config.BodyPath`
- 或 `Context.JsonPointer == Config.JsonPointer`

第一版要求 `Config.JsonPointer` 是 concrete array pointer，例如 `/Body/Metadata`。`SectionMetadata` 的 nested pointer 不直接通过 `SupportsRegion` 匹配基础 adapter。

### 7.2 Validate / Preflight / Apply

`ValidateRegion`、`PreflightRegion`、`ApplyRegion` 都先把 desired value 解析成 `TArray<FAssetDocumentFragmentArrayEntry>`：

- desired value missing 由 region runtime policy 决定是否调用 adapter。
- desired value 非 array：failure code `InvalidFragmentArrayRegionType`。
- entry 非 object 或 object payload invalid：failure code `InvalidFragmentArrayEntryType`。
- entry path 是 `Config.JsonPointer / Index`。

解析成功后：

- `Validate` hook 未设置则 failure `UnsupportedFragmentArrayLifecycle`。
- `Preflight` hook 未设置则 success no-op，除非 config 要求 preflight mandatory。
- `Apply` hook 未设置则 failure `UnsupportedFragmentArrayLifecycle`。
- `Apply` 在 shape failure 时必须设置 `bOutChanged = false`。

`Preflight` 默认 no-op 是允许的，因为很多 fragment array 的真实验证已经发生在 validate 或 apply preview 里；implementation plan 需要测试该行为。

### 7.3 Extract

`ExtractRegion` 调用 `Hooks.Extract` 得到 `TArray<TSharedRef<FJsonObject>>`。

公共 adapter 负责：

- 把 entries 包装成 JSON array。
- 如果 hook failure，原样传播 diagnostics。
- 如果 hook 未设置，返回 `UnsupportedFragmentArrayLifecycle`。

公共 adapter 不做：

- fragment extraction。
- object filtering。
- managed prefix 判断。

### 7.4 Diff

第一版提供两种模式：

1. hook-provided diff：
   - 如果 `Hooks.Diff` 存在，adapter 调用它。
   - profile 可以继续使用 preview-apply-diff 或自定义 semantic diff。

2. default canonical diff：
   - 如果 `Hooks.Diff` 不存在，但 `Hooks.Extract` 存在，adapter 可以 extract current array，并对 current vs desired 做 canonical JSON compare。
   - 输出单条 region-level diff entry：path `Config.JsonPointer`，status `changed` / `unchanged`。

为避免误伤现有 behavior，第一版迁移 AnimSequence/AnimMontage 时推荐继续使用 existing preview-apply-diff 的 body diff 机制；公共 adapter default diff 只在 unit tests 或明确低风险 region 使用。

## 8. Fragment Utility

除了 region adapter，第一版还应提供一组小型 utility，供 `SectionMetadata` 和 profile hooks 使用：

- `FAssetDocumentFragmentArrayUtils::ParseObjectEntries`
- `FAssetDocumentFragmentArrayUtils::ValidateEntriesWithCompiler`
- `FAssetDocumentFragmentArrayUtils::CompileObjectEntries`
- `FAssetDocumentFragmentArrayUtils::ExtractObjectEntries`
- `FAssetDocumentFragmentArrayUtils::MakeEntryPath`

这些 utility 仍在 `Private/Regions` 或 `Private` 下，不进入 public API。

utility 可以接受 profile 提供的 callback：

- `MakeFragmentContext(Index, JsonPointer)`
- `OnCompiledFragment(Index, FragmentResult)`
- `MakeExtractContext(Index, UObject*)`

但 utility 不应知道 concrete UE asset class。

## 9. Migration Scope

### 9.1 Task 1: Adapter And Tests

新增 adapter 和 utility，使用 fake hooks 测试：

- supports region by id/body path/json pointer。
- rejects non-array desired value。
- rejects non-object entry。
- validate receives entry index/path/object。
- apply empty array calls hook and can clear。
- apply shape failure resets changed。
- extract wraps hook entries into JSON array。
- missing mandatory hooks fail clearly。
- hook failure diagnostics propagate unchanged。
- default canonical diff emits stable path/status if enabled。

### 9.2 Task 2: AnimSequence Metadata

迁移 `Body.Metadata`：

- validate array shape through adapter。
- compile/materialize remains in AnimSequence hook.
- managed prefix, explicit name, duplicate key, move/rollback remain AnimSequence-specific.
- apply empty array removes existing managed metadata.
- extract still filters managed metadata only.
- public JSON shape unchanged.

### 9.3 Task 3: AnimSequence AssetUserData

迁移 `Body.AssetUserData`：

- expected base class remains `UAssetUserData`。
- reflection replacement remains AnimSequence-specific hook。
- managed prefix and explicit name handling remain unchanged。
- apply empty array clears existing managed AssetUserData only。
- extract still filters managed AssetUserData only。

### 9.4 Task 4: AnimMontage Metadata

迁移 `Body.Metadata`：

- expected base class remains `UAnimMetaData`。
- `EmbeddedObject` / `DefinitionRef` allowlist remains montage hook/compiler validation。
- outer move to montage remains hook。
- apply empty array clears montage metadata behavior unchanged。
- extract output unchanged。

### 9.5 Task 5: AnimMontage AssetUserData

Implementation plan must first verify current AnimMontage `AssetUserData` support.

If current profile already supports it:

- migrate it like AnimSequence `AssetUserData`。

If current profile does not support it:

- do not add new public capability in this spec。
- document it as deferred in refactor-chain notes。
- do not invent new authoring behavior under a refactor spec。

### 9.6 Task 6: AnimMontage SectionMetadata Utility Reuse

`SectionMetadata` 仍然是 AnimMontage capability 拥有的 map region。

迁移目标：

- 尽量用 `FAssetDocumentFragmentArrayUtils` 替换 per-section array 的 shape / compile / extract 重复循环。
- section-name validation 继续留在 AnimMontage code。
- section assignment 继续留在 AnimMontage code。
- 保持 path shape `/Body/SectionMetadata/<SectionName>/<Index>`。

如果 utility reuse 引入的间接层比移除的重复更多，implementation 可以保持 `SectionMetadata` 不变，但必须在 task notes 里记录原因。它不应阻塞 base array adapter migration。

## 10. Compatibility Requirements

The implementation must preserve:

- existing Body keys。
- existing fragment JSON schema。
- existing diagnostic paths under `/Body/<Region>/<Index>`。
- existing duplicate explicit-name diagnostics。
- existing managed-object prefix behavior。
- existing extract filtering of managed objects only。
- existing `_Skipped` behavior, if a migrated region contributes skipped evidence。
- existing preview-apply-diff semantics for full body diff。

The implementation must not:

- output synthetic adapter region ids into user-visible diff path。
- change authored empty array semantics。
- clear unmanaged user data or unmanaged metadata。
- attach compiled objects to final asset before profile hook has passed validation。
- leak transient staging objects into the final asset on failure。

## 11. Testing Requirements

### 11.1 Runtime Tests

Add focused tests in `AssetDocumentRegionRuntimeTests.cpp` or a dedicated fragment-array test file:

- shape validation。
- entry path construction。
- hook failure propagation。
- empty array apply。
- extract array wrapping。
- default diff path/status。

### 11.2 AnimSequence Tests

Run and/or add coverage for:

- `Metadata` roundtrip。
- `Metadata` empty array clears managed metadata only。
- `AssetUserData` roundtrip。
- `AssetUserData` empty array clears managed user data only。
- invalid entry type path `/Body/Metadata/0` or `/Body/AssetUserData/0`。
- duplicate explicit name diagnostic unchanged。
- extract ignores unmanaged objects。

### 11.3 AnimMontage Tests

Run and/or add coverage for:

- `Metadata` roundtrip。
- `Metadata` empty array behavior。
- `SectionMetadata` path and section target validation unchanged。
- `SectionMetadata` utility reuse keeps `/Body/SectionMetadata/<SectionName>/<Index>` diagnostics。
- `AssetUserData` only if currently supported。

## 12. Verification Requirements

Implementation plan must require:

1. `git diff --check`
2. UBT on a trusted validation host pointing at the implementation worktree
3. focused automation:
   - `AssetFactory.AssetDocument.RegionRuntime`
   - `AssetFactory.AssetDocument.AnimSequence`
   - `AssetFactory.AssetDocument.AnimMontage`
4. full `AssetFactory.AssetDocument` automation before final review
5. per-task spec review and code quality review over `TASK_BASE..HEAD`
6. final spec review and final code quality review over `SPEC_BASE..HEAD`

## 13. Completion Criteria

This spec is complete when:

- `FAssetDocumentFragmentArrayRegionAdapter` exists and is tested。
- At least AnimSequence `Metadata` and `AssetUserData` use the common fragment array lifecycle。
- AnimMontage `Metadata` uses the common fragment array lifecycle。
- AnimMontage `SectionMetadata` either reuses fragment array utility per section or has an explicit documented deferral reason。
- Fragment compiler and UObject materialization remain profile hooks。
- Public document schema remains unchanged。
- Empty array clear/replace behavior is covered by tests。
- Diagnostic paths remain stable and non-synthetic。
- Refactor-chain document marks ring 6 implemented or partially implemented with exact deferrals。

## 14. 后续升级触发条件

出现以下任一情况时，应写新的 spec，而不是扩大本 spec：

- 第二个 map-of-array region 出现，需要 `MapOfFragmentArraysAdapter`。
- notify / notify-state object fragments 要与 timeline placement 同时迁移。
- fragment arrays 需要 identity diff，而不是 region-level canonical diff。
- fragment compiler 需要 public API changes。
- managed object rollback 需要跨 AnimSequence / AnimMontage 统一。

这些都不是第一版 fragment array adapter 的职责。
