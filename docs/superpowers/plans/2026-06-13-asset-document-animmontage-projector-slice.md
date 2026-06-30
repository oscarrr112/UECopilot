# AssetDocument AnimMontage Projector 切片实现计划

> **给 agentic workers 的要求：** 实现本计划时必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans`。每个 task 按 checkbox 推进，并在 task 完成后 checkpoint commit。

**目标：** 并行实现一个实验性的 AnimMontage 读写闭环切片，覆盖 `ExtractBody / ValidateBody / ApplyBody(Update) / DiffBody`，输出与当前 `FAnimMontageAssetDocumentCapability` 相同的 AssetDocument `Body` 结构，然后用代码量、可复用性、文本结构和 update 复杂度评估这条路线是否值得继续。

**架构：** 保留现有 `FAnimMontageAssetDocumentCapability` 作为 production 路径，不替换、不改行为。新增实验代码放在 `Source/AssetDocument/Private/Projectors` 下，只通过自动化测试和评估报告验证 projector/update planner 是否能减少手写逻辑。这个切片不改 MCP、不改 schema、不改变用户看到的 `.assetdoc.json` 格式。

**技术栈：** Unreal Engine 5.7 C++ editor module、`Source/AssetDocument`、现有 `FAssetDocumentFragmentCompiler` / `FAssetDocumentPropertyAdapter`、UE Automation tests、PowerShell LOC 统计、UBT Development 编译。

---

## 1. 范围

这是一个可废弃的垂直切片，不是平台级重构。

切片覆盖当前 AnimMontage profile 已暴露的同一组 `Body` 字段：

- `Skeleton`
- `PreviewMesh`
- `SlotAnimTracks`
- `CompositeSections`
- `Notifies`
- `NotifyStates`
- `Blend`

本切片不做：

- 不替换现有 `FAnimMontageAssetDocumentCapability`。
- 不修改 MCP tool 或 HTTP route。
- 不修改 `MCP/schemas/AssetDocument.md`。
- 不把 `.utxt` 或 `raw.json` 变成新的 authoring surface。
- 不改现有 AssetDoc 文本结构。
- 不引入新的对外资产类型工具。

成功标准：

1. projector 输出能和当前 production capability 的 extraction 输出对齐。
2. update/apply 路径能对现有 montage 做 partial update，并与 production capability 对同一文档的结果对齐。
3. validate 路径能拒绝至少三类无效文档：类型错误、负时间、引用不存在的 section。
4. diff 路径能报告 update 前后的 changed body paths，并与 production diff 的关键路径对齐。
5. 至少两个 projector/update 单元具备 AnimMontage 之外的复用价值。
6. 最终报告包含 production LOC、slice LOC、可复用代码占比、AnimMontage-specific 代码占比，以及 read-side 与 write-side 的代码量拆分。
7. 最终报告给出明确建议：继续、停止，或只在 graph/tree/timeline 类资产上继续。

---

## 2. 读写边界

`Projector` 不能被假设为可逆。

本切片把读写拆成两条明确路径：

```text
UE asset -> Projector -> AssetDoc Body
AssetDoc Body -> Materializer/Updater -> UE asset
```

原因：

- UE 资产里很多信息不是纯数据映射，写回时需要调用 setter、维护数组顺序、重建对象、处理引用、刷新 editor/runtime 派生状态。
- `Extract` 可以跳过 transient/editor-only 字段，但 `Apply` 必须知道哪些字段可以安全替换、哪些字段要 merge、哪些字段必须保留。
- 同一个 AssetDoc 字段的读侧和写侧约束不完全对称，例如 `CompositeSections.NextSectionName` 读出来只是字符串，写回时必须验证 section 是否存在。
- `Notifies` / `NotifyStates` 读出来是 timeline placement，写回时需要 materialize `UAnimNotify` / `UAnimNotifyState` 对象，并决定如何处理已有 unmanaged notify。

因此本切片必须显式实现反向 load/apply 层：

- `FAssetDocumentRefProjector`：只负责 `UObject* -> AssetRef`。
- `FAssetDocumentRefMaterializer`：负责 `AssetRef -> UObject*`，并校验 expected class。
- `FAssetDocumentStructArrayProjector`：负责 `UStruct array -> JSON array`。
- `FAssetDocumentStructArrayUpdater`：负责 `JSON array -> UStruct array replacement`。
- `FAnimMontageProjectorSlice::ExtractBody`：读侧 facade。
- `FAnimMontageProjectorSlice::ApplyBody`：写侧 facade，内部调用 materializer/updater，并处理 AnimMontage-specific 规则。

最终评估报告必须单独统计 read-side 和 write-side 代码量。如果 write-side 代码量接近或超过当前 production capability，说明 projector 路线没有实际降低复杂度。

---

## 3. 文件结构

新增：

- `Source/AssetDocument/Private/Projectors/AssetDocumentProjectionTypes.h`
  实验 projector 的通用 result、diagnostic、metrics 类型。

- `Source/AssetDocument/Private/Projectors/AssetDocumentProjectionTypes.cpp`
  metrics JSON 序列化和 result helper。

- `Source/AssetDocument/Private/Projectors/AssetDocumentRefProjector.h`
  可复用的 asset reference 投影器。

- `Source/AssetDocument/Private/Projectors/AssetDocumentRefProjector.cpp`
  把 `UObject*` 投影成 AssetDoc `AssetRef` fragment。

- `Source/AssetDocument/Private/Projectors/AssetDocumentRefMaterializer.h`
  可复用的 asset reference 反向 materializer。

- `Source/AssetDocument/Private/Projectors/AssetDocumentRefMaterializer.cpp`
  把 AssetDoc `AssetRef` 解析成 `UObject*`，并校验 expected class。

- `Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayProjector.h`
  可复用的 reflected struct array 投影器。

- `Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayProjector.cpp`
  遍历 struct 字段，投影 primitive 字段，并允许字段 override。

- `Source/AssetDocument/Private/Projectors/AssetDocumentUpdatePlan.h`
  实验 update planner 的通用 operation、diagnostic、metrics 类型。

- `Source/AssetDocument/Private/Projectors/AssetDocumentUpdatePlan.cpp`
  update metrics JSON 序列化和 helper。

- `Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayUpdater.h`
  可复用的 reflected struct array 替换/校验 helper。

- `Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayUpdater.cpp`
  根据 JSON array 构造 struct array，复杂字段通过 callback 处理。

- `Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.h`
  AnimMontage 实验读写 facade。

- `Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.cpp`
  输出当前 AnimMontage `Body` shape，并支持 validate/apply/diff 的实验实现。

- `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageProjectorSliceTests.cpp`
  projector 输出测试、update 测试、diff 测试，以及与 production capability 的对比测试。

- `docs/reports/asset-document-animmontage-projector-slice.md`
  最终评估报告。

尽量不修改：

- `Source/AssetDocument/AssetDocument.Build.cs`
  只有新增 include 真的需要模块依赖时才改。

禁止修改 production 行为：

- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.cpp`
- `MCP/src/index.ts`
- `MCP/schemas/AssetDocument.md`

---

## 4. Task 1：建立 projector/materializer 切片测试壳

**文件：**

- 新增：`Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageProjectorSliceTests.cpp`
- 新增：`Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.h`
- 新增：`Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.cpp`

### Step 1：记录 task base

```powershell
git rev-parse HEAD
```

把输出记为 `TASK_BASE`。本 task 的 review diff range 是 `TASK_BASE..HEAD`。

### Step 2：新增失败测试

测试先只验证 projector 能输出当前 AnimMontage `Body` 的关键字段。

核心 fixture：

```cpp
UAnimMontage* NewTransientMontageForProjectorSlice()
{
	UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage(), NAME_None, RF_Transient);
	Montage->BlendIn.SetBlendTime(0.15f);
	Montage->BlendOut.SetBlendTime(0.25f);

	FSlotAnimationTrack SlotTrack;
	SlotTrack.SlotName = TEXT("DefaultSlot");

	FAnimSegment Segment;
	Segment.StartPos = 0.0f;
	Segment.AnimStartTime = 0.0f;
	Segment.AnimEndTime = 1.0f;
	Segment.AnimPlayRate = 1.0f;
	Segment.LoopingCount = 1;
	SlotTrack.AnimTrack.AnimSegments.Add(Segment);
	Montage->SlotAnimTracks.Add(SlotTrack);

	FCompositeSection Section;
	Section.SectionName = TEXT("Start");
	Section.StartTime = 0.0f;
	Section.NextSectionName = NAME_None;
	Montage->CompositeSections.Add(Section);
	return Montage;
}
```

核心断言：

```cpp
FAnimMontageProjectorSlice Projector;
TSharedRef<FJsonObject> ProjectedBody = MakeShared<FJsonObject>();
const FAnimMontageProjectorSliceResult Result = Projector.ExtractBody(*Montage, ProjectedBody);

TestTrue(TEXT("Projector extraction succeeds"), Result.bSuccess);
TestTrue(TEXT("Projector emits SlotAnimTracks"), ProjectedBody->HasField(TEXT("SlotAnimTracks")));
TestTrue(TEXT("Projector emits CompositeSections"), ProjectedBody->HasField(TEXT("CompositeSections")));
TestTrue(TEXT("Projector emits Notifies"), ProjectedBody->HasField(TEXT("Notifies")));
TestTrue(TEXT("Projector emits NotifyStates"), ProjectedBody->HasField(TEXT("NotifyStates")));
TestTrue(TEXT("Projector emits Blend"), ProjectedBody->HasField(TEXT("Blend")));
```

### Step 3：新增最小 projector 壳

`AnimMontageProjectorSlice.h`：

```cpp
#pragma once

#include "CoreMinimal.h"

class UAnimMontage;
class FJsonObject;

struct FAnimMontageProjectorSliceResult
{
	bool bSuccess = false;
	FString Message;
	TArray<FString> ChangedPaths;

	static FAnimMontageProjectorSliceResult Success();
	static FAnimMontageProjectorSliceResult Failure(const FString& InMessage);
};

class FAnimMontageProjectorSlice
{
public:
	FAnimMontageProjectorSliceResult ExtractBody(const UAnimMontage& Montage, TSharedRef<FJsonObject> OutBody) const;
	FAnimMontageProjectorSliceResult ValidateBody(const UAnimMontage& Montage, const TSharedRef<FJsonObject>& Body) const;
	FAnimMontageProjectorSliceResult ApplyBody(UAnimMontage& Montage, const TSharedRef<FJsonObject>& Body) const;
	FAnimMontageProjectorSliceResult DiffBody(const UAnimMontage& Montage, const TSharedRef<FJsonObject>& DesiredBody, TArray<FString>& OutChangedPaths) const;
};
```

`AnimMontageProjectorSlice.cpp` 先返回失败，确保测试是红的：

```cpp
#include "Projectors/AnimMontageProjectorSlice.h"

#include "Dom/JsonObject.h"

FAnimMontageProjectorSliceResult FAnimMontageProjectorSliceResult::Success()
{
	FAnimMontageProjectorSliceResult Result;
	Result.bSuccess = true;
	return Result;
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSliceResult::Failure(const FString& InMessage)
{
	FAnimMontageProjectorSliceResult Result;
	Result.Message = InMessage;
	return Result;
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::ExtractBody(const UAnimMontage& Montage, TSharedRef<FJsonObject> OutBody) const
{
	return FAnimMontageProjectorSliceResult::Failure(TEXT("AnimMontage projector slice is not implemented"));
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::ValidateBody(const UAnimMontage& Montage, const TSharedRef<FJsonObject>& Body) const
{
	return FAnimMontageProjectorSliceResult::Failure(TEXT("AnimMontage projector slice validation is not implemented"));
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::ApplyBody(UAnimMontage& Montage, const TSharedRef<FJsonObject>& Body) const
{
	return FAnimMontageProjectorSliceResult::Failure(TEXT("AnimMontage projector slice apply is not implemented"));
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::DiffBody(const UAnimMontage& Montage, const TSharedRef<FJsonObject>& DesiredBody, TArray<FString>& OutChangedPaths) const
{
	return FAnimMontageProjectorSliceResult::Failure(TEXT("AnimMontage projector slice diff is not implemented"));
}
```

### Step 4：编译并确认测试失败原因正确

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

预期：编译通过。

运行 automation：

```text
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.ExtractsCurrentBodyShape
```

预期：测试失败，失败点是 `Projector extraction succeeds`。

### Step 5：提交 checkpoint

```powershell
git add Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageProjectorSliceTests.cpp Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.*
git commit -m "test: add anim montage projector slice harness"
```

---

## 5. Task 2：新增可复用 projector/materializer/update primitives

**文件：**

- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentProjectionTypes.h`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentProjectionTypes.cpp`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentRefProjector.h`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentRefProjector.cpp`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentRefMaterializer.h`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentRefMaterializer.cpp`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayProjector.h`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayProjector.cpp`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentUpdatePlan.h`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentUpdatePlan.cpp`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayUpdater.h`
- 新增：`Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayUpdater.cpp`

### Step 1：记录 task base

```powershell
git rev-parse HEAD
```

### Step 2：新增 projection result / metrics

核心类型：

```cpp
struct FAssetDocumentProjectionMetrics
{
	int32 AssetSpecificFields = 0;
	int32 ReusableProjectedFields = 0;
	int32 SkippedFields = 0;

	TSharedRef<FJsonObject> ToJson() const;
};

struct FAssetDocumentProjectionResult
{
	bool bSuccess = true;
	FString Message;
	TArray<FAssetDocumentProjectionDiagnostic> Diagnostics;
	FAssetDocumentProjectionMetrics Metrics;

	static FAssetDocumentProjectionResult Success();
	static FAssetDocumentProjectionResult Failure(const FString& Message, const FString& Path, const FString& Code);
};
```

### Step 3：新增 `FAssetDocumentRefProjector`

用途：把 `UObject*` 投影成当前 AssetDoc fragment 风格的 `AssetRef`。

输出结构：

```json
{
  "Kind": "AssetRef",
  "Path": "/Game/...",
  "Class": "/Script/..."
}
```

核心逻辑：

```cpp
FAssetDocumentProjectionResult FAssetDocumentRefProjector::ProjectAssetRef(
	const UObject* Object,
	const UClass* ExpectedClass,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutJson) const
{
	if (!Object)
	{
		OutJson.Reset();
		return FAssetDocumentProjectionResult::Success();
	}

	if (ExpectedClass && !Object->IsA(ExpectedClass))
	{
		return FAssetDocumentProjectionResult::Failure(
			FString::Printf(TEXT("Expected asset reference compatible with '%s', got '%s'"), *ExpectedClass->GetPathName(), *Object->GetClass()->GetPathName()),
			Path,
			TEXT("InvalidAssetRefClass"));
	}

	OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	OutJson->SetStringField(TEXT("Path"), FSoftObjectPath(Object).ToString());
	OutJson->SetStringField(TEXT("Class"), Object->GetClass()->GetPathName());
	return FAssetDocumentProjectionResult::Success();
}
```

### Step 4：新增 `FAssetDocumentRefMaterializer`

用途：把 AssetDoc 中的引用 fragment 反向解析成 UE 对象。这个逻辑不能由 `FAssetDocumentRefProjector` 自动反推，因为写回需要 load、class 校验和错误诊断。

核心接口：

```cpp
class FAssetDocumentRefMaterializer
{
public:
	FAssetDocumentUpdateResult ResolveAssetRef(
		const TSharedRef<FJsonObject>& RefJson,
		const UClass* ExpectedClass,
		const FString& Path,
		UObject*& OutObject) const;
};
```

支持范围：

- `{"Kind":"AssetRef","Path":"...","Class":"..."}`
- null value 表示清空引用。

本切片暂不要求 `DefinitionRef` 完整展开；如果 update 测试需要它，必须把 definition resolution 的代码量单独计入 write-side。

### Step 5：新增 `FAssetDocumentStructArrayProjector`

用途：投影 `FSlotAnimationTrack`、`FCompositeSection`、未来的 table rows、curve keys、timeline entries 等 struct array。

设计约束：

- 默认只处理 primitive 字段：`FName`、`FString`、number、bool。
- 复杂字段由 caller 通过 override callback 处理。
- 不在这里写 AnimMontage-specific 字段名判断。

核心接口：

```cpp
using FAssetDocumentStructFieldOverride = TFunction<bool(
	const FString& FieldName,
	FProperty* Field,
	const void* ValuePtr,
	TSharedRef<FJsonObject> OutObject)>;

class FAssetDocumentStructArrayProjector
{
public:
	FAssetDocumentProjectionResult ProjectStructArray(
		const TArray<const void*>& Items,
		UStruct* StructType,
		const FString& Path,
		const TSet<FString>& IncludedFields,
		const FAssetDocumentStructFieldOverride& FieldOverride,
		TArray<TSharedPtr<FJsonValue>>& OutArray) const;
};
```

### Step 6：新增 update plan / struct array updater

`AssetDocumentUpdatePlan.h` 的核心类型：

```cpp
#pragma once

#include "CoreMinimal.h"

class FJsonObject;

struct FAssetDocumentUpdateOperation
{
	FString Path;
	FString Kind;
};

struct FAssetDocumentUpdateMetrics
{
	int32 ReusableOperations = 0;
	int32 AssetSpecificOperations = 0;
	int32 ValidationFailures = 0;

	TSharedRef<FJsonObject> ToJson() const;
};

struct FAssetDocumentUpdateResult
{
	bool bSuccess = true;
	FString Message;
	TArray<FString> ChangedPaths;
	FAssetDocumentUpdateMetrics Metrics;

	static FAssetDocumentUpdateResult Success();
	static FAssetDocumentUpdateResult Failure(const FString& Message);
};
```

`AssetDocumentStructArrayUpdater.h` 的核心接口：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Projectors/AssetDocumentUpdatePlan.h"

class FJsonObject;
class FJsonValue;
class UScriptStruct;

using FAssetDocumentStructFieldApplyOverride = TFunction<bool(
	const FString& FieldName,
	const TSharedPtr<FJsonValue>& JsonValue,
	void* StructValuePtr,
	FString& OutError)>;

class FAssetDocumentStructArrayUpdater
{
public:
	FAssetDocumentUpdateResult ReplaceStructArray(
		const TArray<TSharedPtr<FJsonValue>>& SourceArray,
		UScriptStruct* StructType,
		const FString& Path,
		const FAssetDocumentStructFieldApplyOverride& FieldOverride,
		TFunctionRef<void(void*)> AddStructValue) const;
};
```

实现约束：

- updater 只负责“JSON array -> struct array replacement”的通用机械部分。
- primitive 字段可以通过 `FPropertySetterUtils::SetPropertyFromJson` 写入。
- AnimMontage 的 `AnimTrack.AnimSegments`、`NextSectionName` 校验、notify object 创建不写进 updater，只通过 callback 或 AnimMontage-specific 层处理。
- `ChangedPaths` 使用 AssetDoc JSON Pointer，例如 `/Body/Blend/BlendInTime`、`/Body/CompositeSections`。
- updater 不是 projector 的反向自动运行结果；它是单独的 materialization/write path。

### Step 7：编译

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

预期：编译通过。此时 Task 1 的 projector 测试仍可能失败，因为 AnimMontage facade 还没接入 primitives。

### Step 8：提交 checkpoint

```powershell
git add Source/AssetDocument/Private/Projectors/AssetDocumentProjectionTypes.* Source/AssetDocument/Private/Projectors/AssetDocumentRefProjector.* Source/AssetDocument/Private/Projectors/AssetDocumentRefMaterializer.* Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayProjector.* Source/AssetDocument/Private/Projectors/AssetDocumentUpdatePlan.* Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayUpdater.*
git commit -m "feat: add experimental asset document projection and update primitives"
```

---

## 6. Task 3：用 projector 输出 AnimMontage Body

**文件：**

- 修改：`Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.h`
- 修改：`Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.cpp`
- 修改：`Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageProjectorSliceTests.cpp`

### Step 1：记录 task base

```powershell
git rev-parse HEAD
```

### Step 2：实现全部当前公开 Body key

`FAnimMontageProjectorSlice::ExtractBody` 需要输出：

```json
{
  "Skeleton": null,
  "PreviewMesh": null,
  "SlotAnimTracks": [],
  "CompositeSections": [],
  "Notifies": [],
  "NotifyStates": [],
  "Blend": {
    "BlendInTime": 0.15,
    "BlendOutTime": 0.25
  },
  "_ProjectionMetrics": {
    "AssetSpecificFields": 0,
    "ReusableProjectedFields": 0,
    "SkippedFields": 0
  }
}
```

实现要求：

- `Skeleton`、`PreviewMesh` 通过 `FAssetDocumentRefProjector` 投影。
- `SlotAnimTracks` 使用 reusable struct/array projector 加少量 AnimMontage-specific override。
- `CompositeSections` 使用 reusable struct/array projector。
- `Notifies`、`NotifyStates` 先输出空数组；若 fixture 里有 managed notify，再补对齐逻辑。
- `Blend` 可以先用 AnimMontage-specific 小段逻辑，因为 `BlendIn/BlendOut` 是 class-specific field。
- `_ProjectionMetrics` 只允许出现在实验 projector 输出，不允许进入 production capability。

### Step 3：加强测试断言

新增断言：

```cpp
const TArray<TSharedPtr<FJsonValue>>* SlotAnimTracks = nullptr;
TestTrue(TEXT("SlotAnimTracks is an array"), ProjectedBody->TryGetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks));
TestEqual(TEXT("One slot track is emitted"), SlotAnimTracks ? SlotAnimTracks->Num() : 0, 1);

const TSharedPtr<FJsonObject>* Blend = nullptr;
TestTrue(TEXT("Blend object exists"), ProjectedBody->TryGetObjectField(TEXT("Blend"), Blend));
if (Blend && Blend->IsValid())
{
	TestEqual(TEXT("BlendInTime is projected"), (*Blend)->GetNumberField(TEXT("BlendInTime")), 0.15);
	TestEqual(TEXT("BlendOutTime is projected"), (*Blend)->GetNumberField(TEXT("BlendOutTime")), 0.25);
}

const TSharedPtr<FJsonObject>* Metrics = nullptr;
TestTrue(TEXT("Projection metrics exist"), ProjectedBody->TryGetObjectField(TEXT("_ProjectionMetrics"), Metrics));
```

### Step 4：编译并运行测试

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

运行：

```text
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.ExtractsCurrentBodyShape
```

预期：通过。

### Step 5：提交 checkpoint

```powershell
git add Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.* Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageProjectorSliceTests.cpp
git commit -m "feat: project anim montage body through experimental slice"
```

---

## 7. Task 4：与 production extraction 做等价对比

**文件：**

- 修改：`Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageProjectorSliceTests.cpp`

### Step 1：记录 task base

```powershell
git rev-parse HEAD
```

### Step 2：新增 normalization helper

实验字段不能参与 production 对比：

```cpp
TSharedRef<FJsonObject> CloneBodyWithoutExperimentFields(const TSharedRef<FJsonObject>& Body)
{
	TSharedRef<FJsonObject> Clone = MakeShared<FJsonObject>();
	Clone->Values = Body->Values;
	Clone->RemoveField(TEXT("_ProjectionMetrics"));
	Clone->RemoveField(TEXT("_Skipped"));
	return Clone;
}
```

### Step 3：新增 production 对比测试

测试路径：

```text
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.MatchesProductionExtraction
```

测试逻辑：

```cpp
FAssetDocumentCapabilityContext Context;
Context.Asset = Montage;
Context.AssetClass = UAnimMontage::StaticClass();

FAnimMontageAssetDocumentCapability ProductionCapability;
TSharedRef<FJsonObject> ProductionBody = MakeShared<FJsonObject>();
const FAssetDocumentCapabilityResult ProductionResult = ProductionCapability.Extract(Context, ProductionBody);
TestTrue(TEXT("Production extraction succeeds"), ProductionResult.bSuccess);

FAnimMontageProjectorSlice Projector;
TSharedRef<FJsonObject> ProjectedBody = MakeShared<FJsonObject>();
const FAnimMontageProjectorSliceResult ProjectorResult = Projector.ExtractBody(*Montage, ProjectedBody);
TestTrue(TEXT("Projector extraction succeeds"), ProjectorResult.bSuccess);

const TSharedRef<FJsonObject> NormalizedProduction = CloneBodyWithoutExperimentFields(ProductionBody);
const TSharedRef<FJsonObject> NormalizedProjected = CloneBodyWithoutExperimentFields(ProjectedBody);
TestTrue(TEXT("Projector output matches production body for representative fixture"), JsonObjectsEqualForSlice(NormalizedProduction, NormalizedProjected));
```

需要 include：

```cpp
#include "Profiles/AnimMontageAssetDocumentCapability.h"
```

### Step 4：编译并运行两个 projector 测试

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

运行：

```text
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.ExtractsCurrentBodyShape
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.MatchesProductionExtraction
```

预期：两个测试都通过。

如果 production 输出比 projector 多出当前 fixture 真正支持的字段，应补 projector 或调整 fixture，而不是放宽断言。

### Step 5：提交 checkpoint

```powershell
git add Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageProjectorSliceTests.cpp
git commit -m "test: compare anim montage projector slice with production extraction"
```

---

## 8. Task 5：实现并验证 materialize / update / apply / diff 闭环

**文件：**

- 修改：`Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.h`
- 修改：`Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.cpp`
- 修改：`Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageProjectorSliceTests.cpp`

### Step 1：记录 task base

```powershell
git rev-parse HEAD
```

### Step 2：实现 `ValidateBody`

`ValidateBody` 至少覆盖这些规则：

- `SlotAnimTracks` 存在时必须是 array。
- `CompositeSections` 存在时必须是 array。
- `Notifies` 存在时必须是 array。
- `NotifyStates` 存在时必须是 array。
- `Blend` 存在时必须是 object。
- `Blend.BlendInTime` 和 `Blend.BlendOutTime` 必须是非负 number。
- `CompositeSections[].SectionName` 不能为空。
- `CompositeSections[].NextSectionName` 如果非空，必须引用同一文档中的 section name。

测试新增三个无效输入：

```json
{ "SlotAnimTracks": "bad" }
```

```json
{ "Blend": { "BlendInTime": -1.0 } }
```

```json
{
  "CompositeSections": [
    { "SectionName": "Start", "StartTime": 0.0, "NextSectionName": "Missing" }
  ]
}
```

每个输入都必须让 `ValidateBody` 返回失败，并给出非空 `Message`。

### Step 3：实现 `ApplyBody` 作为 materialize/update 路径

`ApplyBody` 是本切片最重要的部分。它表示“把 AssetDoc Body load/materialize 回已有 AnimMontage 并做更新”，不是只生成新文档，也不是 projector 自动反向运行。

支持的 partial update：

```json
{
  "Blend": {
    "BlendInTime": 0.3,
    "BlendOutTime": 0.4
  },
  "CompositeSections": [
    {
      "SectionName": "Start",
      "StartTime": 0.0,
      "NextSectionName": "Loop"
    },
    {
      "SectionName": "Loop",
      "StartTime": 0.5,
      "NextSectionName": ""
    }
  ]
}
```

行为要求：

- 只更新文档里出现的 body key。
- 未出现的 body key 保持现有 montage 内容不变。
- `Blend` 只更新出现的字段。
- asset/class reference 字段必须通过 `FAssetDocumentRefMaterializer` 解析，不能直接复用 projector 逻辑。
- `CompositeSections` 出现时整体替换 `Montage->CompositeSections`。
- `SlotAnimTracks` 出现时整体替换 `Montage->SlotAnimTracks`，但 AnimSegment 内的 `AnimReference` 可以为 null。
- `Notifies` / `NotifyStates` 本切片可以先只支持空数组清理 managed notify；如果要支持 embedded notify，需要在报告里单独记录 asset-specific 代码量。
- `ChangedPaths` 记录被更新的 body path。
- Apply 结束后需要调用必要的 UE dirty/refresh 逻辑；如果本切片因 transient fixture 不调用，需要在报告里明确说明 production asset 写回仍需要额外 lifecycle 处理。

新增测试：

```text
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.UpdatesExistingMontage
```

断言：

- 初始 montage 有一个 section，apply 后有两个 section。
- `BlendInTime` 从 `0.15` 变成 `0.3`。
- `BlendOutTime` 从 `0.25` 变成 `0.4`。
- `ChangedPaths` 包含 `/Body/Blend` 和 `/Body/CompositeSections`。

### Step 4：与 production Apply 做结果对比

新增测试：

```text
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.MatchesProductionApplyUpdate
```

测试逻辑：

1. 创建两个相同的 transient montage：`ProductionMontage` 和 `SliceMontage`。
2. 构造同一份 desired `Body`，包含 `Blend` 和 `CompositeSections` partial update。
3. 对 `ProductionMontage` 调用 `FAnimMontageAssetDocumentCapability::Apply`。
4. 对 `SliceMontage` 调用 `FAnimMontageProjectorSlice::ApplyBody`。
5. 分别用 production capability `Extract` 得到两个 body。
6. 去掉 `_Skipped` 和 `_ProjectionMetrics`。
7. JSON 文本比较必须相等。

如果 production apply 需要完整 `Body` 而不是 partial body，则测试应显式记录：

```cpp
// Production capability expects the full Body object; this test expands the partial update into a full comparable Body before applying.
```

不能因为 production 与 slice 对 partial update 的契约不同而静默放宽断言。

### Step 5：实现 `DiffBody`

`DiffBody` 可以用“extract current -> apply desired to duplicate/transient copy -> extract preview -> compare normalized JSON”的方式实现，先不追求最优性能。

要求：

- desired body 改 `Blend` 时，返回 `/Body/Blend`。
- desired body 改 `CompositeSections` 时，返回 `/Body/CompositeSections`。
- desired body 与 current 相同时，`OutChangedPaths` 为空。

新增测试：

```text
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.DiffReportsUpdatePaths
```

断言：

- 修改 blend 时 changed paths 包含 `/Body/Blend`。
- 修改 section 时 changed paths 包含 `/Body/CompositeSections`。

### Step 6：编译并运行 update/diff 测试

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

运行：

```text
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.UpdatesExistingMontage
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.MatchesProductionApplyUpdate
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.DiffReportsUpdatePaths
```

预期：全部通过。

### Step 7：提交 checkpoint

```powershell
git add Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.* Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageProjectorSliceTests.cpp
git commit -m "feat: add anim montage projector update and diff slice"
```

---

## 9. Task 6：产出代码量和文本结构评估报告

**文件：**

- 新增：`docs/reports/asset-document-animmontage-projector-slice.md`

### Step 1：记录 task base

```powershell
git rev-parse HEAD
```

### Step 2：统计 production 与 slice 代码量

```powershell
$production = @(
  'Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp',
  'Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.h',
  'Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.cpp',
  'Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.h'
)
$sliceReusable = @(
  'Source/AssetDocument/Private/Projectors/AssetDocumentProjectionTypes.cpp',
  'Source/AssetDocument/Private/Projectors/AssetDocumentProjectionTypes.h',
  'Source/AssetDocument/Private/Projectors/AssetDocumentRefProjector.cpp',
  'Source/AssetDocument/Private/Projectors/AssetDocumentRefProjector.h',
  'Source/AssetDocument/Private/Projectors/AssetDocumentRefMaterializer.cpp',
  'Source/AssetDocument/Private/Projectors/AssetDocumentRefMaterializer.h',
  'Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayProjector.cpp',
  'Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayProjector.h',
  'Source/AssetDocument/Private/Projectors/AssetDocumentUpdatePlan.cpp',
  'Source/AssetDocument/Private/Projectors/AssetDocumentUpdatePlan.h',
  'Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayUpdater.cpp',
  'Source/AssetDocument/Private/Projectors/AssetDocumentStructArrayUpdater.h'
)
$sliceSpecific = @(
  'Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.cpp',
  'Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.h'
)
foreach ($set in @(@('production',$production), @('sliceReusable',$sliceReusable), @('sliceSpecific',$sliceSpecific))) {
  $name = $set[0]
  $files = $set[1]
  $lines = 0
  foreach ($file in $files) {
    $count = (Get-Content $file | Measure-Object -Line).Lines
    $lines += $count
    "$name`t$file`t$count"
  }
  "$name`tTOTAL`t$lines"
}
```

### Step 3：写中文评估报告

报告必须包含：

- 结论：继续、停止，或只在 graph/tree/timeline 资产上继续。
- 测试范围。
- 代码量表格。
- 可复用单元分析，并区分 read-side projector 与 write-side updater。
- 与当前 AssetDoc 文本结构的对比。
- update/apply/diff 行为是否与 production 对齐。
- 反向 load/materialize 需要多少额外逻辑，哪些逻辑无法由 projector 自动反推。
- 明确建议。

报告中的文本结构示例：

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

### Step 4：占位符扫描

```powershell
rg -n "TBD|TODO|待补|占位|Use the exact|Write one|Write at least" docs/reports/asset-document-animmontage-projector-slice.md
```

预期：无匹配。

### Step 5：提交 checkpoint

```powershell
git add docs/reports/asset-document-animmontage-projector-slice.md
git commit -m "docs: evaluate anim montage projector slice"
```

---

## 10. Task 7：最终验证

### Step 1：UBT 编译

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

预期：通过。

### Step 2：运行目标 automation

```text
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.ExtractsCurrentBodyShape
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.MatchesProductionExtraction
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.UpdatesExistingMontage
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.MatchesProductionApplyUpdate
AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.DiffReportsUpdatePaths
AssetFactory.AssetDocument.AnimMontage
```

预期：projector slice 测试通过，现有 AnimMontage 测试仍通过。

### Step 3：确认 worktree 状态

```powershell
git status --short --branch
```

预期：所有 task commit 后 worktree 干净。

### Step 4：最终总结必须包含

- production LOC total。
- slice LOC total，并拆分 reusable 与 AnimMontage-specific。
- read-side projector LOC 与 write-side updater LOC。
- 输出文本结构是否变化。
- update/apply/diff 是否覆盖，是否与 production 对齐。
- apply 是否需要单独 materializer，以及这部分占总 slice LOC 的比例。
- projector 路线是否值得继续。
- 哪些验证无法运行，以及原因。

---

## 11. Review 边界

每个 task 开始时记录 `TASK_BASE=HEAD`。

- Task review 只审 `TASK_BASE..HEAD`。
- 最终 review 审 `602d8cd..HEAD`，除非执行前 branch base 发生变化。
- subagent 只能改自己 task 的文件列表；如果必须改其他文件，需要先说明原因。
