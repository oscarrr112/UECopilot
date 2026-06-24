# AssetDocument Public Region Runtime 设计

日期：2026-06-24

状态：正式规格，待用户审阅

分支：`docs/asset-document-public-region-runtime-spec`

基线：`feature/asset-document-structured-capabilities-spec` at `f8d6b8f`

范围：为 AssetDocument structured capabilities 增加 public region runtime 设计，抽出现有 `AnimMontage`、`AnimSequence`、`UBlueprint`、`WidgetBlueprint` profile 中重复的 region lifecycle、diff、canonicalization、authoritative apply 和 utility 逻辑。本文不实现新 asset profile，不改变 AssetDocument MCP tool contract。

---

## 1. 背景

AssetDocument 当前已经形成了正确的大方向：

- `FAssetDocumentService` 统一处理 `validate`、`apply`、`extract`、`diff`、`apply-file`。
- `IAssetDocumentProfile` 提供 exact-class profile、`Body` keys、schema/template、region policies。
- `IAssetDocumentCapability` 提供 profile/capability 级 `Validate`、`Preflight`、`Apply`、`Extract`、`Diff`。
- `IAssetDocumentFragmentAdapter` 提供 `AssetRef`、`ClassRef`、`StructValue`、`EmbeddedObject` 等 fragment 级公共能力。
- `FAssetDocumentRegionPolicy` 已经表达 `RegionKind`、`DefaultSource`、`ReducerMode`、`ApplyMode`、identity/comparison rules、managed UE paths、extract-only fields、canonicalizer hook。
- `FAssetDocumentRegionCanonicalizer`、`FAssetDocumentSidecarDelta`、`FAssetDocumentSidecarSyncEngine` 已经承担 region hash、canonicalization 和 sync decision 的一部分职责。

但 structured asset profile 的实现已经出现明显重复：

- 多个 profile 自己实现 `IsKnownBodyKey`。
- 多个 profile 自己实现 `RequireObjectValue`、`RequireArrayValue`、`BodyFailure`。
- 多个 profile 自己实现 `JsonValueToComparableString` 和 `AddBodyDiffEntry`。
- 多个 profile 自己实现 `ValidateBodyObject` 和 unknown region rejection。
- 多个 profile 自己实现 `Extract current -> Apply desired to transient copy -> Extract preview -> compare` 的 diff 模式。
- WidgetBlueprint 内部 adapter 已经有 `Validate / Preflight / Apply / Extract / Diff` 形态，但它们仍是 profile-private adapter，不是 public region capability。

这说明现有架构抽象停在了 `Profile`、`Capability`、`Policy`、`Fragment` 四个点，中间缺少一个 public region runtime。继续增加 asset profile 会把重复逻辑继续扩散到每个 asset class，违背 AssetDocument 的设计目标。

---

## 2. 设计目标

1. 将 AssetDocument 的复用维度从 asset class 拉回 region capability。
2. 保留 sparse sidecar / default diff / authoritative managed region 语义。
3. 让 profile 只声明 body regions、region policies、adapter 绑定和 asset-specific hooks。
4. 将通用 region lifecycle 统一为 `ValidateRegion`、`PreflightRegion`、`ApplyRegion`、`ExtractRegion`、`DiffRegion`。
5. 统一 region shape validation、JSON path diagnostics、diff entry 结构、canonical comparison、explicit empty/delete semantics。
6. 复用现有 `FAssetDocumentRegionPolicy`，不新增一套并行 policy DSL。
7. 复用现有 `IAssetDocumentFragmentAdapter`，不把 fragment 编译逻辑复制进 region adapter。
8. 保持 `FAssetDocumentSidecarSyncEngine` 只做 hash/state decision，不引入 asset-class switch。
9. 保持 `FAssetDocumentCanonicalJson` 是 asset-agnostic JSON canonical serializer，不塞入 profile 语义。
10. 允许 asset-specific code 存在，但只保留 UE API materializer、extractor hook、post-apply repair、compile/rebuild 和特殊 semantic identity。

---

## 3. 非目标

本 spec 不做：

- 不新增 `USkeleton`、`BlendSpace`、`DataTable` 或任何新 asset class profile。
- 不把所有 region 类型一次性重写为完全泛化实现。
- 不把 graph、tree、timeline、import asset 都塞进一个万能 adapter。
- 不删除现有 `AnimMontage`、`AnimSequence`、`UBlueprint`、`WidgetBlueprint` profile。
- 不改变 MCP public tools；仍使用 `validate_asset_document`、`diff_asset_document`、`extract_asset_document`、`apply_asset_document` 和 `apply_asset_document_file`。
- 不改变 `.assetdoc.json` 顶层 shape。
- 不把 sidecar 变成完整 `.uasset` mirror。
- 不用 class-name switch 或静态 asset-type list 扩展行为。
- 不把 AssetDocument 以外的资产生成路线纳入设计边界。

---

## 4. 当前代码问题

### 4.1 Capability 粒度过粗

`IAssetDocumentCapability` 当前方法直接接收整个 `Body` JSON：

```cpp
virtual FAssetDocumentCapabilityResult Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const = 0;
virtual FAssetDocumentCapabilityResult Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) = 0;
virtual FAssetDocumentCapabilityResult Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const = 0;
virtual FAssetDocumentCapabilityResult Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const = 0;
```

这适合作为 profile-level facade，但不适合作为长期复用单元。`Body.Variables`、`Body.Components`、`Body.Curves`、`Body.Notifies`、`Body.WidgetTree`、`Body.Bindings`、`Body.Animations`、`Body.UbergraphPages` 需要独立 region lifecycle。

### 4.2 RegionPolicy 没有对应 runtime

`FAssetDocumentRegionPolicy` 已经拥有足够描述能力：

- `RegionId`
- `BodyPath`
- `RegionKind`
- `DefaultSource`
- `ReducerMode`
- `ApplyMode`
- `IdentityRules`
- `ComparisonRules`
- `ManagedUePropertyPaths`
- `ExtractOnlyFields`
- `ExplicitDeleteValues`
- `ExtensionHookName`
- `CanonicalizerHookName`

但当前 profile 多数仍手写 region validate/apply/extract/diff，policy 更多用于 sync hash 和 inspection，并没有统一驱动 region runtime。

### 4.3 Profile-private adapter 已经暗示公共层

WidgetBlueprint 已经存在多个私有 adapter：

- `FWidgetBlueprintTreeAdapter`
- `FWidgetBlueprintBindingAdapter`
- `FWidgetBlueprintAnimationAdapter`
- `FWidgetBlueprintGraphAdapter`

它们都拥有接近 public region adapter 的方法形态，但接口各不相同，参数绑定具体 asset class，diff/canonical/diagnostic 规则各自实现。UBlueprint 和 AnimSequence 也有同类重复逻辑，只是没有抽成独立 class。

### 4.4 Diff 语义分散

当前 diff 逻辑至少有三类：

- JSON comparable string 对比。
- apply desired 到 transient copy，再 extract preview 对比。
- asset/domain specific semantic diff，例如 Blueprint components、WidgetTree、Graph regions。

这些都应该由 public region runtime 统一调度，profile 只声明 region 采用哪种 diff strategy。

---

## 5. 推荐架构

新增三个核心公共层：

1. `FAssetDocumentRegionContext`
2. `IAssetDocumentRegionAdapter`
3. `FAssetDocumentRegionRuntime`

再新增两个辅助层：

1. `FAssetDocumentBodyRegionDispatcher`
2. `FAssetDocumentRegionRegistry`

整体关系：

```text
FAssetDocumentService
  -> IAssetDocumentProfile
      -> Asset-specific IAssetDocumentCapability facade
          -> owns FAssetDocumentBodyRegionDispatcher
          -> FAssetDocumentRegionRuntime
              -> IAssetDocumentRegionAdapter
                  -> FragmentCompiler / PropertyAdapter / GraphAdapter / Asset-specific hook
              -> RegionPolicy
              -> RegionCanonicalizer
              -> SidecarDelta
```

---

## 6. Public Region Context

新增：

```cpp
struct FAssetDocumentRegionContext
{
    UObject* Asset = nullptr;
    UClass* AssetClass = nullptr;
    FString TargetAssetPath;
    FString SourceDocumentPath;
    const TSharedPtr<FJsonObject>* Definitions = nullptr;
    FAssetDocumentResult* Result = nullptr;
    bool bIsDryRun = false;

    const IAssetDocumentProfile* Profile = nullptr;
    const FAssetDocumentRegionPolicy* Policy = nullptr;
    FName RegionId;
    FString BodyPath;
    FString JsonPointer;
};
```

要求：

- `RegionId` 使用 policy 的完整 region id，例如 `Body.Curves`。
- `BodyPath` 使用 dotted path，例如 `Body.Curves`。
- `JsonPointer` 使用 diagnostics 需要的 JSON Pointer，例如 `/Body/Curves`。
- `Definitions` 继续复用现有 fragment/compiler 定义解析。
- `bIsDryRun` 必须传递到 region adapter，禁止 preflight 真实修改 asset。

---

## 7. Public Region Adapter Interface

新增：

```cpp
class ASSETDOCUMENT_API IAssetDocumentRegionAdapter
{
public:
    virtual ~IAssetDocumentRegionAdapter() = default;

    virtual FName GetName() const = 0;
    virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const = 0;
    virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext& Context) const = 0;

    virtual FAssetDocumentCapabilityResult ValidateRegion(
        const FAssetDocumentRegionContext& Context,
        const TSharedPtr<FJsonValue>& DesiredValue) const = 0;

    virtual FAssetDocumentCapabilityResult PreflightRegion(
        FAssetDocumentRegionContext& Context,
        const TSharedPtr<FJsonValue>& DesiredValue) const;

    virtual FAssetDocumentCapabilityResult ApplyRegion(
        FAssetDocumentRegionContext& Context,
        const TSharedPtr<FJsonValue>& DesiredValue,
        bool& bOutChanged) = 0;

    virtual FAssetDocumentCapabilityResult ExtractRegion(
        const FAssetDocumentRegionContext& Context,
        TSharedPtr<FJsonValue>& OutCurrentValue) const = 0;

    virtual FAssetDocumentCapabilityResult DiffRegion(
        const FAssetDocumentRegionContext& Context,
        const TSharedPtr<FJsonValue>& DesiredValue,
        TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const;
};
```

默认行为：

- `PreflightRegion()` 默认调用 `ValidateRegion()`。
- `DiffRegion()` 默认使用 region runtime 的 configured diff strategy。
- adapter 可以 override `DiffRegion()`，但必须输出统一 diff entry shape。
- adapter 不负责 `Body` object unknown key 检查；这由 body region dispatcher 统一完成。
- adapter 不直接读写 sidecar 文件；只处理当前 region value。

接口继承只用于 `IAssetDocumentRegionAdapter` 这类纯抽象能力边界。具体 adapter 应优先通过组合持有 parser、materializer、extractor、diff strategy、post-apply hook，不应通过多层 `Base` class 继承扩展行为。

---

## 8. Region Runtime

新增：

```cpp
class FAssetDocumentRegionRuntime
{
public:
    static FAssetDocumentCapabilityResult Validate(
        const FAssetDocumentRegionContext& Context,
        const TSharedPtr<FJsonValue>& DesiredValue,
        const IAssetDocumentRegionAdapter& Adapter);

    static FAssetDocumentCapabilityResult Preflight(
        FAssetDocumentRegionContext& Context,
        const TSharedPtr<FJsonValue>& DesiredValue,
        const IAssetDocumentRegionAdapter& Adapter);

    static FAssetDocumentCapabilityResult Apply(
        FAssetDocumentRegionContext& Context,
        const TSharedPtr<FJsonValue>& DesiredValue,
        const IAssetDocumentRegionAdapter& Adapter,
        bool& bOutChanged);

    static FAssetDocumentCapabilityResult Extract(
        const FAssetDocumentRegionContext& Context,
        const IAssetDocumentRegionAdapter& Adapter,
        TSharedPtr<FJsonValue>& OutCurrentValue);

    static FAssetDocumentCapabilityResult Diff(
        const FAssetDocumentRegionContext& Context,
        const TSharedPtr<FJsonValue>& DesiredValue,
        const IAssetDocumentRegionAdapter& Adapter,
        TArray<TSharedPtr<FJsonValue>>& OutDiffEntries);
};
```

职责：

- 统一调用 adapter。
- 统一处理 explicit empty/delete value。
- 统一调用 `FAssetDocumentRegionCanonicalizer`。
- 统一输出 diff entry。
- 统一处理 `ReducerMode` 和 `ApplyMode` 的默认行为。
- 统一把 adapter diagnostics prefix 到 region `JsonPointer`。
- 统一执行 `Extract current -> Apply desired to transient copy -> Extract preview -> compare` 策略。

禁止：

- 禁止在 runtime 中加入 asset class switch。
- 禁止 runtime 直接包含 `AnimMontage`、`AnimSequence`、`UBlueprint`、`WidgetBlueprint` 头文件。
- 禁止 runtime 修改 `FAssetDocumentSidecarSyncEngine` 的 decision 规则。

---

## 9. Body Region Dispatcher

新增：

```cpp
struct FAssetDocumentBodyRegionDispatcherHooks
{
    TFunction<FAssetDocumentCapabilityResult(
        const FAssetDocumentCapabilityContext&,
        const TSharedRef<FJsonObject>&)> ValidateCrossRegion;

    TFunction<FAssetDocumentCapabilityResult(
        FAssetDocumentCapabilityContext&,
        const TSet<FName>&)> PostApplyRepair;
};

class FAssetDocumentBodyRegionDispatcher
{
public:
    FAssetDocumentCapabilityResult ValidateBody(
        const FAssetDocumentCapabilityContext& Context,
        const TSharedRef<FJsonObject>& BodyObject) const;

    FAssetDocumentCapabilityResult PreflightBody(
        FAssetDocumentCapabilityContext& Context,
        const TSharedRef<FJsonObject>& BodyObject) const;

    FAssetDocumentCapabilityResult ApplyBody(
        FAssetDocumentCapabilityContext& Context,
        const TSharedRef<FJsonObject>& BodyObject,
        TSet<FName>& OutAppliedRegions) const;

    FAssetDocumentCapabilityResult ExtractBody(
        const FAssetDocumentCapabilityContext& Context,
        TSharedRef<FJsonObject>& OutBodyObject) const;

    FAssetDocumentCapabilityResult DiffBody(
        const FAssetDocumentCapabilityContext& Context,
        const TSharedRef<FJsonObject>& DesiredBody,
        TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const;
};
```

`FAssetDocumentBodyRegionDispatcher` 负责：

- `Body` 必须是 JSON object。
- unknown body key rejection。
- required body key validation。
- body key 到 region adapter 的分发。
- 按 apply order 执行 regions。
- 统一调用 `ValidateBodyCrossRegion()`。
- 统一调用 `PostApplyRepair()`。
- 统一 `Extract()` 时按 canonical body key 顺序写出。
- 统一 `Diff()` 时遍历 authored desired regions，并对 missing managed regions 使用 policy/default strategy。

profile-specific capability 仍然直接实现现有 `IAssetDocumentCapability`，但内部组合一个 dispatcher：

```cpp
class FAnimSequenceAssetDocumentCapability : public IAssetDocumentCapability
{
private:
    FAssetDocumentBodyRegionDispatcher BodyDispatcher;
};
```

profile-specific capability 只保留：

- exact asset/class support。
- 构造 `RegionBindings` / `RegionPolicies` / adapter map。
- cross-region validation hook。
- post-apply repair hook。

这样保留当前 `IAssetDocumentCapability` public contract，同时避免新增一个继承基类迫使所有 profile 进入同一继承树。

---

## 10. Region Registry

新增：

```cpp
class FAssetDocumentRegionRegistry
{
public:
    void Register(TSharedRef<IAssetDocumentRegionAdapter> Adapter);
    const IAssetDocumentRegionAdapter* FindByName(FName AdapterName) const;
    const IAssetDocumentRegionAdapter* FindForPolicy(const FAssetDocumentRegionPolicy& Policy) const;
};
```

第一版可以不做全局 mutable singleton。推荐先由 profile/capability 持有 adapter map，接口形状向 registry 靠拢：

```cpp
TMap<FName, TSharedRef<IAssetDocumentRegionAdapter>> RegionAdapters;
```

后续当多个 profile 确实共享 adapter 实例时，再提升为 module-level registry。

---

## 11. 第一批公共 Region Adapter

### 11.1 `FAssetDocumentDeferredRegionAdapter`

用途：

- 管理已声明但 deferred 的 empty array/object region。
- 替代多个 profile 中的 `RequireEmptyArray`、`RequireEmptyObject`。

行为：

- `null` 或缺失表示未声明。
- empty array/object 允许。
- non-empty 返回明确 `UnsupportedRegion` diagnostic。
- extract 输出 empty/default value 或 `_Skipped` evidence，由 policy 决定。

适用：

- `Body.FunctionGraphs`
- `Body.MacroGraphs`
- `Body.Timelines`
- 其它已声明但尚未支持 authoring 的 regions。

### 11.2 `FAssetDocumentObjectRegionAdapter`

用途：

- 管理简单 object region。
- 支持 shape validation、fragment field validation、canonical diff。

适用：

- `Body.References`
- `Body.Preview`
- `Body.RootMotion`
- `Body.Playback`
- `Body.Additive`
- `Body.Compression`
- `Body.Blend`
- `Body.EditorOptions`
- `Body.Palette`

限制：

- 只处理 JSON object lifecycle。
- UE 写入仍通过 field binding 或 specialized hook 实现。
- 不负责 graph/tree/timeline materialization。

### 11.3 `FAssetDocumentNamedArrayRegionAdapter`

用途：

- 管理带稳定 identity 的 array region。

核心配置：

```cpp
struct FAssetDocumentNamedArrayRegionOptions
{
    FString ElementNameField = TEXT("Name");
    bool bRequireUniqueIdentity = true;
    bool bSortForCanonicalHash = true;
    bool bUseAuthoredOrderForApply = false;
};
```

适用：

- `Body.Variables`
- `Body.ImplementedInterfaces`
- `Body.Components`
- `Body.Curves`
- `Body.Notifies`
- `Body.NotifyStates`
- `Body.NotifyTracks`
- `Body.SyncMarkers`
- `Body.Bindings`
- `Body.Animations`

限制：

- adapter 只管理 array parsing、identity、canonical ordering、diff scaffolding。
- 具体 element parse/materialize/extract 交给 element handler。

### 11.4 `FAssetDocumentPreviewApplyDiffRegionAdapter`

用途：

- 抽出常见 diff 策略：extract current，复制 transient asset，apply desired，extract preview，canonical compare。

适用：

- 初期用于 `AnimMontage`、`AnimSequence` 这类 apply 已相对完整但 diff 重复的 profile。

限制：

- 需要 adapter 提供 safe transient duplicate strategy。
- 不适合会触发不可逆 editor global state 的 region。

### 11.5 `FAssetDocumentGraphRegionAdapterWrapper`

用途：

- 通过组合包装现有 graph core，不重写 graph parser/node adapters。
- 统一 graph region validate/extract/diff 入口。

适用：

- `UBlueprint` graph regions。
- `WidgetBlueprint` graph regions。
- 后续 `AnimBlueprint`、`Material`、`Niagara` graph regions。

限制：

- 第一版只包装现有 `FUBlueprintGraphRegionAdapter` 和 `FWidgetBlueprintGraphAdapter` 形态。
- 不把 Material/Niagara graph 一次性纳入实现。
- wrapper 不应作为 graph adapter 继承基类；后续 Material/Niagara 也应通过组合注入 node adapter、schema resolver、materializer 和 diff strategy。

### 11.6 暂不公共化的 adapter

第一版不抽成完全公共 adapter：

- `FWidgetBlueprintTreeAdapter`
- `FWidgetBlueprintAnimationAdapter`
- `FAnimMontageNotifyPlacementAdapter`

这些可以先通过 wrapper 接入 `IAssetDocumentRegionAdapter`，保留内部实现。等第二个 tree/timeline/notify-like asset 需要复用时，再抽深层公共结构。

---

## 12. Authoritative Apply 语义

public region runtime 必须保留现有 AssetDocument 语义：

- sidecar 是 source-of-truth delta。
- managed region 缺失不等于 one-shot patch skip。
- 对 managed region，缺失表示没有持久化差异；sidecar-to-asset sync 应回到 default/baseline，然后应用 authored region。
- 对未 managed 的 asset 数据，sidecar 缺失不能重置。
- explicit empty values 由 `Policy.ExplicitDeleteValues` 控制。

第一版 runtime 行为：

| Sidecar 状态 | Managed region apply 行为 |
| --- | --- |
| region absent | 使用 `DefaultSource` 重建 default/baseline |
| region present with value | apply authored value |
| region present explicit empty | 按 `ExplicitDeleteValues` 清空或删除 |
| region present null but null not explicit delete | validation error |

`DefaultSource` 解释：

| DefaultSource | 含义 |
| --- | --- |
| `CDO` | 从 class default object 或 parent baseline 还原 |
| `EmptyTemplate` | 使用 empty object/array/scalar default |
| `CurrentAssetBaseline` | 使用当前 asset 中非 managed baseline，避免误删外部状态 |
| `ProfileDeclared` | 使用 profile/adapter 提供的 default region value |

---

## 13. Reducer 语义

public region runtime 需要把 reducer 从 profile-private code 中抽出来，但第一版只实现已有语义的公共化。

### 13.1 `DefaultDiff`

流程：

1. extract asset evidence region。
2. canonicalize evidence region for hash。
3. compare with default/baseline region。
4. 如果等于 default/baseline，sidecar 可以 omit。
5. 如果不同，写出 canonical authored region。

### 13.2 `ManagedRegion`

流程：

1. region 是权威 managed surface。
2. extract 输出当前 managed surface 的 canonical representation。
3. sidecar 缺失时 apply 使用 default/baseline 重建该 managed surface。
4. diff 必须能指出 sidecar 与当前 asset 的 semantic difference。

### 13.3 后续扩展

后续可以增加：

- `SparseNamedArrayDiff`
- `GraphSemanticDiff`
- `TimelineSemanticDiff`

这些不是第一版必需项。第一版只提供 extension hook 和 strategy slot。

---

## 14. Diff Entry 统一格式

所有 region diff 必须输出统一 JSON object：

```json
{
  "path": "/Body/Variables/MyVar",
  "status": "changed",
  "current": {},
  "desired": {}
}
```

字段：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `path` | 是 | JSON Pointer |
| `status` | 是 | `changed`、`unchanged`、`skipped`、`failed` |
| `current` | 否 | 当前 asset evidence |
| `desired` | 否 | sidecar desired value |
| `code` | 否 | 诊断 code |
| `message` | 否 | 可读说明 |

禁止各 profile 自己发明 diff entry shape。

---

## 15. JSON Utility 公共化

新增 `FAssetDocumentJsonRegionUtils`，从 profile-private code 抽出：

- `Failure(Path, Code, Message)`
- `RequireObjectValue(Value, Path, OutObject)`
- `RequireArrayValue(Value, Path, OutArray)`
- `RequireStringField(Object, FieldName, Path, OutString)`
- `RequireNumberField(Object, FieldName, Path, OutNumber)`
- `RequireBoolField(Object, FieldName, Path, OutBool)`
- `JsonValueToComparableString(Value, Policy)`
- `AddDiffEntry(Entries, Path, Status, Current, Desired)`
- `EscapeJsonPointerToken(Token)`
- `MakeBodyPath(BodyKey)`
- `MakeBodyArrayItemPath(BodyKey, Index)`

要求：

- diagnostics path 必须统一使用 JSON Pointer。
- helper 不得包含 asset class 逻辑。
- helper 不得调用 UE editor APIs。

---

## 16. Profile 声明方式

profile 应从“一个 capability 手写全部 region”转为“声明 region table”。

示例：

```cpp
struct FAssetDocumentRegionBinding
{
    FName BodyKey;
    FName RegionId;
    FName AdapterName;
    int32 ApplyOrder = 0;
    bool bRequired = false;
};
```

profile 或 body capability 提供：

```cpp
virtual TArray<FAssetDocumentRegionBinding> GetRegionBindings() const;
virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;
```

`ResolveBodyAdapter(FName BodyKey)` 在兼容期仍返回 `Body` facade capability。新的 facade 内部按 `RegionBindings` 分发。

兼容要求：

- `inspect_asset_document_profile` 仍显示原有 `BodySections`。
- `InternalAdapters` 可额外显示 region adapter names。
- `RegionPolicies` 输出必须保持稳定。

---

## 17. Asset-specific Hook 边界

允许保留 asset-specific code 的位置：

- element materializer，例如 Blueprint variable、component、AnimNotifyEvent、Widget node。
- element extractor，例如从 UE object/struct 转 agent-facing JSON。
- semantic identity，例如 Blueprint component alias、Widget variable GUID、graph semantic node id。
- post-apply repair，例如 Blueprint compile、Widget compile、AnimSequence refresh cache、Montage marker cache repair。
- editor subsystem bridge，例如 graph construction、MovieScene track construction。
- unsupported-current detection，例如 unknown graph node family 或 unsupported animation track family。

不允许保留在 asset-specific code 的内容：

- generic body object validation。
- generic unknown key rejection。
- generic array/object type check。
- generic diff entry construction。
- generic comparable JSON serializer。
- generic region canonical hash/writeback call。
- generic explicit empty/delete handling。
- generic apply order dispatch。

---

## 18. 迁移策略

迁移必须分阶段，不能一次性重写全部 profile。

### 18.1 Task 1：公共 utility 和 region runtime skeleton

新增：

- `Source/AssetDocument/Public/AssetDocumentRegion.h`
- `Source/AssetDocument/Private/AssetDocumentRegionRuntime.h/.cpp`
- `Source/AssetDocument/Private/AssetDocumentJsonRegionUtils.h/.cpp`

验证：

- 新增 unit tests，不触碰现有 profile behavior。
- 所有现有 AssetDocument automation 继续通过。

### 18.2 Task 2：`FAssetDocumentBodyRegionDispatcher`

新增组合式 dispatcher，但不迁移所有 profile。

先创建 test-only fake profile/capability，验证：

- unknown body key rejection。
- required region validation。
- adapter dispatch order。
- missing adapter diagnostics。
- region diagnostics path prefix。

### 18.3 Task 3：迁移 deferred regions

先把 UBlueprint / WidgetBlueprint 的 deferred graph/timeline empty region 校验迁入 `FAssetDocumentDeferredRegionAdapter`。

理由：

- 风险最低。
- 不涉及 UE object mutation。
- 可以证明 profile-private shape validation 可以减少。

### 18.4 Task 4：迁移 AnimSequence object/named-array regions 中的一小组

推荐选择：

- `Body.References`
- `Body.Preview`
- `Body.Playback`
- `Body.NotifyTracks`

理由：

- AnimSequence 当前重复较多。
- 它比 WidgetTree 和 graph 更适合验证公共 `ObjectRegion` 和 `NamedArrayRegion`。

### 18.5 Task 5：包装现有 WidgetBlueprint adapters

不重写内部逻辑，只包成 region adapter：

- `WidgetTree`
- `Bindings`
- `Animations`
- graph regions

目标是统一 region lifecycle 外壳，不追求立即减少所有内部代码。

### 18.6 Task 6：更新新资产扩展指南

更新：

- `docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md`

明确新 asset class 不应默认新增完整 `XxxAssetDocumentCapability` 巨类，而应先选择 public region adapter，再写 asset-specific hooks。

---

## 19. 验收标准

第一版 public region runtime 完成时，必须满足：

1. 新增 public region context、adapter interface、runtime 和 JSON utility。
2. 至少一个现有 profile 的至少两个 regions 通过 runtime 调度。
3. 至少一个 deferred region 使用 `FAssetDocumentDeferredRegionAdapter`。
4. 至少一个 object 或 named-array region 使用 public region adapter。
5. `inspect_asset_document_profile` 能看到 region policies 和 internal adapter names。
6. 现有 `AnimMontage`、`AnimSequence`、`UBlueprint`、`WidgetBlueprint` focused tests 不回退。
7. full `AssetFactory.AssetDocument` automation 不回退。
8. MCP `npm test` 不回退。
9. external HTTP smoke 中 `apply-file -> extract -> diff` 不出现新增 sidecar sync hash mismatch。
10. spec 或 guide 中明确后续新增资产应优先复用 public region adapter。

---

## 20. 测试要求

新增 C++ automation tests：

| Test | 目的 |
| --- | --- |
| `AssetFactory.AssetDocument.RegionRuntime.Shape` | Body/region shape validation |
| `AssetFactory.AssetDocument.RegionRuntime.Dispatch` | apply order 和 adapter dispatch |
| `AssetFactory.AssetDocument.RegionRuntime.DiffEntry` | diff entry shape 稳定 |
| `AssetFactory.AssetDocument.RegionRuntime.ExplicitEmpty` | explicit empty/delete semantics |
| `AssetFactory.AssetDocument.RegionRuntime.DeferredRegion` | deferred empty/non-empty validation |
| `AssetFactory.AssetDocument.RegionRuntime.NamedArray` | identity、duplicate、canonical order |
| `AssetFactory.AssetDocument.RegionRuntime.CanonicalHash` | region runtime 与 canonicalizer/hash 一致 |

回归测试：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

focused automation：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionRuntime"
```

full automation：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AssetDocument"
```

MCP：

```powershell
Push-Location MCP
npm test
Pop-Location
```

---

## 21. 反模式

实现时必须避免：

- 一个巨大 `UniversalRegionAdapter` 处理所有 graph/tree/timeline/import。
- 在 runtime 中写 `if Class == UWidgetBlueprint`。
- 通过静态 asset class list 决定 adapter。
- 为每个 asset class 新增一套 extractor/applier/reducer/diff helper。
- 让 `RegionPolicy` 只作为文档存在，而不是 runtime 输入。
- 在 `FAssetDocumentCanonicalJson` 中加入 domain semantic normalization。
- 在 `FAssetDocumentSidecarSyncEngine` 中加入 region-specific conflict decision。
- 把 fragment adapter、region adapter、profile capability 三个粒度混成一个接口。
- 为了抽象而重写已经稳定的 WidgetTree/Graph 内部 materializer。

---

## 22. 对后续资产的影响

完成 public region runtime 后，新 asset profile 的默认流程应变为：

1. 做 asset surface inventory。
2. 定义 `Body.*` regions。
3. 为每个 region 选择 public adapter。
4. 只有 public adapter 无法表达 UE materialization/extraction/repair 时，才写 asset-specific hook。
5. 将 hook 注册到 region adapter 或 region binding。
6. 用 RegionPolicy 驱动 reducer、apply mode、identity、comparison、canonicalizer。

例如：

| 后续资产 | 应优先复用 |
| --- | --- |
| `USkeleton` | object region、named array region、fragment refs、post-apply repair hook |
| `BlendSpace` | object region、named array samples、preview apply diff |
| `DataTable` | table/named-row region，后续从 named array 扩展 |
| `BlackboardData` | named key array region |
| `BehaviorTree` | tree region wrapper |
| `LevelSequence` | timeline/track region wrapper |
| `Material` | graph region wrapper，后续组合专门 material node adapter |

---

## 23. Implementation Plan 要求

写 implementation plan 前必须遵守：

- 不在当前 spec 分支直接实现生产代码。
- 从 `feature/asset-document-structured-capabilities-spec` 新建独立 implementation worktree。
- 每个 task 记录 `TASK_BASE=HEAD`。
- 每个 task 完成 focused verification 后 checkpoint commit。
- review 只审对应 `TASK_BASE..HEAD`。

推荐 implementation plan task 切分：

1. Region runtime interfaces and JSON utilities。
2. Body region dispatcher and fake adapter tests。
3. Deferred region adapter migration。
4. Object/named-array region pilot on AnimSequence。
5. WidgetBlueprint adapter wrapper migration。
6. Guide update and full regression。

---

## 24. 成功标准

这个 spec 成功的标志不是新增更多资产，而是减少新资产接入成本：

- 新 asset class 不再默认创建一个几千行 `XxxAssetDocumentCapability.cpp`。
- 新 region 不再重复写 body key validation、array/object require、diff entry、comparable JSON。
- `RegionPolicy` 成为真实 runtime 输入。
- profile 代码主要声明 region 和 hooks。
- asset-specific code 的存在理由可以被归类为 materializer、extractor、semantic identity 或 repair hook。

---

## 25. 用户审阅重点

请重点审阅：

1. 是否同意先抽 region runtime，再做 `USkeleton` 或 `BlendSpace`。
2. 是否同意第一版只迁移低风险 deferred/object/named-array region，不强行重写 graph/tree/timeline 内部。
3. 是否同意 `IAssetDocumentCapability` 保留为 profile-level facade，新增 `IAssetDocumentRegionAdapter` 作为 public region 级接口。
4. 是否同意后续新资产必须先选 public region adapter，只有无法表达时才写 asset-specific hook。
