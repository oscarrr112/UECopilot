# StateTree Generator 参数与 PropertyBag Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 StateTree generator 增加 root/state parameters、PropertyBag schema/value round-trip、state overrides、linked subtree/linked asset 参数同步与真实 Editor/MCP 冒烟。

**Architecture:** 新增一个 `StateTreePropertyBagAdapter` 作为唯一 PropertyBag 适配层，负责 typed JSON 到 `FInstancedPropertyBag` schema/value 的双向转换。`StateTreeStateBuilder` 只解析和编排 root/state/linked parameter sections，`StateTreeExtract` 只调用 adapter 输出 JSON，避免把 PropertyBag 细节散进已有结构/transition 代码。

**Tech Stack:** Unreal Engine 5.7、AssetFactory C++、StateTree editor/runtime modules、StructUtils `FInstancedPropertyBag`、`FPropertySetterUtils`、MCP schema/fixtures、真实 GUI Editor MCP smoke。

---

## 当前分支上下文

- Worktree：`/Volumes/Mac/GameDev/ProjectRPG/.worktrees/AssetFactory-statetree-parameters-propertybags`
- Branch：`feature/statetree-parameters-propertybags`
- Base commit：`bed8084 docs: add StateTree parameters property bag spec`
- 不要在 `/Volumes/Mac/GameDev/ProjectRPG/Plugins/AssetFactory` 里直接实现。本路径只用于最后把 worktree 结果挂回 ProjectRPG 做真实 GUI smoke。

## 文件地图

Create:

- `Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.cpp`
- `TestData/ST_Parameters_Basic.json`
- `TestData/ST_Parameters_Complex.json`
- `TestData/ST_Parameters_LinkedTarget.json`
- `TestData/ST_Parameters_LinkedReferencer.json`
- `TestData/ST_Parameters_Invalid_UnknownType.json`
- `TestData/ST_Parameters_Invalid_BadGuid.json`
- `TestData/ST_Parameters_Invalid_BadValue.json`
- `TestData/ST_Parameters_Invalid_LinkedOverrideUnknown.json`
- `TestData/ST_Parameters_Invalid_LinkedOverrideTypeMismatch.json`

Modify:

- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStructureTypes.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.h`
- `MCP/schemas/StateTree.md`
- `MCP/src/index.ts`：先运行 `rg -n "StateTree|RootParameters|parameters" MCP/src/index.ts`；只有该文件包含 StateTree schema 摘要或字段白名单时才修改对应文本。

Engine APIs to verify and use:

- `/Users/pengao/UnrealEngine/Engine/Source/Runtime/CoreUObject/Public/StructUtils/PropertyBag.h`
- `/Users/pengao/UnrealEngine/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeState.h`
- `/Users/pengao/UnrealEngine/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorData.h`
- `/Users/pengao/UnrealEngine/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Private/StateTreeEditingSubsystem.cpp`

Known UE 5.7 facts from the initial probe:

- `FPropertyBagPropertyDesc` has public `FGuid ID`, `FName Name`, `EPropertyBagPropertyType ValueType`, `FPropertyBagContainerTypes ContainerTypes`, and `ValueTypeObject`.
- `FInstancedPropertyBag` supports `AddProperties()`, `FindPropertyDescByName()`, `FindPropertyDescByID()`, `GetMutableValue()`, `MigrateToNewBagInstanceWithOverrides()`, array refs, and set refs.
- `EPropertyBagContainerType` supports `None`, `Array`, `Set`; there is no `Map` container type in this UE 5.7 header.
- `FStateTreeStateParameters` contains `Parameters`, `PropertyOverrides`, `UpdateParametersFromLinkedSubtree()`, `SetParametersPropertyOverridden()`, and `GetDefaultParameters()`.
- `UStateTreeEditorData` owns `RootParameterPropertyBag` and `RootParametersGuid`.

## Task 1: 固定参数 JSON 模型与基础解析

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.h`
- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStructureTypes.h`

- [ ] **Step 1: 写 adapter public types**

Create `StateTreePropertyBagAdapter.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "StructUtils/PropertyBag.h"

struct FAFStateTreeParameterSpec
{
	FString Name;
	FString Type;
	FGuid ID;
	bool bHasExplicitID = false;
	bool bHasValue = false;
	bool bOverridden = false;
	TSharedPtr<FJsonValue> Value;
};

struct FAFStateTreeParameterBagSpec
{
	TArray<FAFStateTreeParameterSpec> Parameters;
};

namespace UE::AssetFactory::StateTree
{
	bool ParseParameterBagSpec(
		const TSharedPtr<FJsonObject>& ParametersObject,
		const FString& ScopeLabel,
		FAFStateTreeParameterBagSpec& OutSpec,
		FString& OutError);

	bool ApplyParameterBagSpec(
		FInstancedPropertyBag& Bag,
		const FAFStateTreeParameterBagSpec& Spec,
		const FString& AssetPath,
		const FString& ScopeLabel,
		FString& OutError);

	TSharedPtr<FJsonObject> ExtractParameterBag(
		const FInstancedPropertyBag& Bag,
		bool bDiffOnly);
}
```

- [ ] **Step 2: 扩展 state spec**

Add fields to `FAFStateTreeStateSpec` in `StateTreeStructureTypes.h`:

```cpp
	FAFStateTreeParameterBagSpec Parameters;
	FAFStateTreeParameterBagSpec ParameterOverrides;
```

Also include the adapter header:

```cpp
#include "Generators/StateTree/StateTreePropertyBagAdapter.h"
```

- [ ] **Step 3: 实现参数 entry parser**

In `StateTreePropertyBagAdapter.cpp`, implement `ParseParameterBagSpec()` with these exact validation rules:

```cpp
bool UE::AssetFactory::StateTree::ParseParameterBagSpec(
	const TSharedPtr<FJsonObject>& ParametersObject,
	const FString& ScopeLabel,
	FAFStateTreeParameterBagSpec& OutSpec,
	FString& OutError)
{
	OutSpec.Parameters.Reset();
	if (!ParametersObject.IsValid())
	{
		return true;
	}

	TSet<FString> Names;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ParametersObject->Values)
	{
		if (Pair.Key.IsEmpty() || Names.Contains(Pair.Key))
		{
			OutError = FString::Printf(TEXT("StateTree parameter scope '%s' has duplicate or empty parameter name '%s'"), *ScopeLabel, *Pair.Key);
			return false;
		}
		Names.Add(Pair.Key);

		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(EntryObject) || !EntryObject || !EntryObject->IsValid())
		{
			OutError = FString::Printf(TEXT("StateTree parameter '%s.%s' must be an object"), *ScopeLabel, *Pair.Key);
			return false;
		}

		FAFStateTreeParameterSpec Spec;
		Spec.Name = Pair.Key;
		(*EntryObject)->TryGetStringField(TEXT("type"), Spec.Type);
		(*EntryObject)->TryGetStringField(TEXT("Type"), Spec.Type);
		if (Spec.Type.IsEmpty())
		{
			OutError = FString::Printf(TEXT("StateTree parameter '%s.%s' is missing required field 'type'"), *ScopeLabel, *Pair.Key);
			return false;
		}

		FString GuidString;
		if ((*EntryObject)->TryGetStringField(TEXT("id"), GuidString) || (*EntryObject)->TryGetStringField(TEXT("ID"), GuidString))
		{
			if (!FGuid::Parse(GuidString, Spec.ID))
			{
				OutError = FString::Printf(TEXT("StateTree parameter '%s.%s' has invalid GUID '%s'"), *ScopeLabel, *Pair.Key, *GuidString);
				return false;
			}
			Spec.bHasExplicitID = true;
		}

		Spec.bHasValue = (*EntryObject)->TryGetField(TEXT("value"), Spec.Value) || (*EntryObject)->TryGetField(TEXT("Value"), Spec.Value);
		(*EntryObject)->TryGetBoolField(TEXT("overridden"), Spec.bOverridden);
		(*EntryObject)->TryGetBoolField(TEXT("Overridden"), Spec.bOverridden);

		OutSpec.Parameters.Add(MoveTemp(Spec));
	}

	return true;
}
```

- [ ] **Step 4: Parse root/state parameter sections**

In `StateTreeStateBuilder.cpp`, parse root parameters before building nodes:

```cpp
TSharedPtr<FJsonObject> RootParametersObject;
FAFStateTreeParameterBagSpec RootParametersSpec;
if (UE::AssetFactory::StateTree::StructureJson::TryGetObjectField(Config, TEXT("rootParameters"), TEXT("RootParameters"), RootParametersObject))
{
	if (!UE::AssetFactory::StateTree::ParseParameterBagSpec(RootParametersObject, TEXT("root"), RootParametersSpec, OutError))
	{
		return false;
	}
}
```

In `ParseStateSpec()`, parse:

```cpp
TSharedPtr<FJsonObject> ParametersObject;
if (StructureJson::TryGetObjectField(StateObject, TEXT("parameters"), TEXT("Parameters"), ParametersObject))
{
	if (!ParseParameterBagSpec(ParametersObject, OutSpec.CanonicalPath, OutSpec.Parameters, OutError))
	{
		return false;
	}
}

TSharedPtr<FJsonObject> OverridesObject;
if (StructureJson::TryGetObjectField(StateObject, TEXT("parameterOverrides"), TEXT("ParameterOverrides"), OverridesObject))
{
	if (!ParseParameterBagSpec(OverridesObject, OutSpec.CanonicalPath + TEXT(".overrides"), OutSpec.ParameterOverrides, OutError))
	{
		return false;
	}
	for (FAFStateTreeParameterSpec& OverrideSpec : OutSpec.ParameterOverrides.Parameters)
	{
		OverrideSpec.bOverridden = true;
	}
}
```

- [ ] **Step 5: Build to verify parser compiles**

Run from ProjectRPG root after mounting/pointing this worktree as the plugin source:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected: compile succeeds. If it fails on missing include cycles, move `FAFStateTreeParameterSpec` declarations into `StateTreeStructureTypes.h` and keep adapter declarations independent.

- [ ] **Step 6: Commit Task 1**

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.h Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.cpp Source/AssetFactory/Private/Generators/StateTree/StateTreeStructureTypes.h Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp
git commit -m "feat: parse StateTree parameter specs"
```

## Task 2: 实现 PropertyBag type mapping 与 4A 基础写入

**Files:**

- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`

- [ ] **Step 1: Add type descriptor parser**

Add a local helper in `StateTreePropertyBagAdapter.cpp`:

```cpp
namespace
{
	struct FAFPropertyBagTypeDesc
	{
		EPropertyBagPropertyType ValueType = EPropertyBagPropertyType::None;
		FPropertyBagContainerTypes Containers;
		TObjectPtr<const UObject> ValueTypeObject = nullptr;
	};

	bool ResolveClassObject(const FString& TypeName, UClass* BaseClass, const UObject*& OutObject)
	{
		OutObject = nullptr;
		if (TypeName.IsEmpty())
		{
			OutObject = BaseClass;
			return true;
		}
		UClass* Class = LoadClass<UObject>(nullptr, *TypeName);
		if (!Class)
		{
			Class = FindObject<UClass>(nullptr, *TypeName);
		}
		if (!Class || !Class->IsChildOf(BaseClass))
		{
			return false;
		}
		OutObject = Class;
		return true;
	}
}
```

- [ ] **Step 2: Map simple and reference types**

Implement `ParsePropertyBagType()` with explicit mappings:

```cpp
bool ParsePropertyBagType(const FString& TypeString, FAFPropertyBagTypeDesc& OutDesc, FString& OutError)
{
	const FParsedTypeInfo Parsed = FPropertySetterUtils::ParseTypeString(TypeString);
	const FString BaseType = Parsed.BaseType;

	if (BaseType.Equals(TEXT("Bool"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::Bool; return true; }
	if (BaseType.Equals(TEXT("Byte"), ESearchCase::IgnoreCase) || BaseType.Equals(TEXT("UInt8"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::Byte; return true; }
	if (BaseType.Equals(TEXT("Int"), ESearchCase::IgnoreCase) || BaseType.Equals(TEXT("Int32"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::Int32; return true; }
	if (BaseType.Equals(TEXT("Int64"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::Int64; return true; }
	if (BaseType.Equals(TEXT("UInt32"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::UInt32; return true; }
	if (BaseType.Equals(TEXT("UInt64"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::UInt64; return true; }
	if (BaseType.Equals(TEXT("Float"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::Float; return true; }
	if (BaseType.Equals(TEXT("Double"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::Double; return true; }
	if (BaseType.Equals(TEXT("Name"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::Name; return true; }
	if (BaseType.Equals(TEXT("String"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::String; return true; }
	if (BaseType.Equals(TEXT("Text"), ESearchCase::IgnoreCase)) { OutDesc.ValueType = EPropertyBagPropertyType::Text; return true; }

	if (BaseType.Equals(TEXT("Enum"), ESearchCase::IgnoreCase))
	{
		UEnum* Enum = FPropertySetterUtils::FindEnumByName(Parsed.SubType);
		if (!Enum)
		{
			OutError = FString::Printf(TEXT("Unknown StateTree parameter enum type '%s'"), *Parsed.SubType);
			return false;
		}
		OutDesc.ValueType = EPropertyBagPropertyType::Enum;
		OutDesc.ValueTypeObject = Enum;
		return true;
	}

	if (BaseType.Equals(TEXT("Struct"), ESearchCase::IgnoreCase) || BaseType.StartsWith(TEXT("F")))
	{
		const FString StructName = BaseType.Equals(TEXT("Struct"), ESearchCase::IgnoreCase) ? Parsed.SubType : BaseType;
		UScriptStruct* Struct = FPropertySetterUtils::FindStructByName(StructName);
		if (!Struct)
		{
			OutError = FString::Printf(TEXT("Unknown StateTree parameter struct type '%s'"), *StructName);
			return false;
		}
		OutDesc.ValueType = EPropertyBagPropertyType::Struct;
		OutDesc.ValueTypeObject = Struct;
		return true;
	}

	// Object/Class/SoftObject/SoftClass blocks use ResolveClassObject().
	OutError = FString::Printf(TEXT("Unknown StateTree parameter type '%s'"), *TypeString);
	return false;
}
```

Add the four reference blocks:

```cpp
if (BaseType.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
{
	const UObject* TypeObject = nullptr;
	if (!ResolveClassObject(Parsed.SubType, UObject::StaticClass(), TypeObject))
	{
		OutError = FString::Printf(TEXT("Unknown StateTree parameter object class '%s'"), *Parsed.SubType);
		return false;
	}
	OutDesc.ValueType = EPropertyBagPropertyType::Object;
	OutDesc.ValueTypeObject = TypeObject;
	return true;
}
```

Repeat for `SoftObject`, `Class`, and `SoftClass`; class types should default to `UObject::StaticClass()` as the metaclass when subtype is empty.

- [ ] **Step 3: Add deterministic ID helper**

```cpp
FGuid ResolveParameterGuid(
	const FInstancedPropertyBag& Bag,
	const FAFStateTreeParameterSpec& Spec,
	const FString& AssetPath,
	const FString& ScopeLabel)
{
	if (Spec.bHasExplicitID)
	{
		return Spec.ID;
	}
	if (const FPropertyBagPropertyDesc* ExistingDesc = Bag.FindPropertyDescByName(FName(*Spec.Name)))
	{
		if (ExistingDesc->ID.IsValid())
		{
			return ExistingDesc->ID;
		}
	}
	return FGuid::NewDeterministicGuid(FString::Printf(
		TEXT("AssetFactory.StateTree.Parameter.%s.%s.%s.%s"),
		*AssetPath,
		*ScopeLabel,
		*Spec.Name,
		*Spec.Type));
}
```

- [ ] **Step 4: Add or update bag descriptors**

Implement the descriptor batch in `ApplyParameterBagSpec()`:

```cpp
TArray<FPropertyBagPropertyDesc> Descs;
for (const FAFStateTreeParameterSpec& Param : Spec.Parameters)
{
	FAFPropertyBagTypeDesc TypeDesc;
	if (!ParsePropertyBagType(Param.Type, TypeDesc, OutError))
	{
		OutError = FString::Printf(TEXT("StateTree parameter '%s.%s': %s"), *ScopeLabel, *Param.Name, *OutError);
		return false;
	}

	FPropertyBagPropertyDesc Desc(FName(*Param.Name), TypeDesc.Containers, TypeDesc.ValueType, const_cast<UObject*>(TypeDesc.ValueTypeObject.Get()), CPF_Edit);
	Desc.ID = ResolveParameterGuid(Bag, Param, AssetPath, ScopeLabel);
	Descs.Add(MoveTemp(Desc));
}

const EPropertyBagAlterationResult Result = Bag.AddProperties(Descs, true);
if (Result != EPropertyBagAlterationResult::Success)
{
	OutError = FString::Printf(TEXT("StateTree parameter scope '%s' failed to update PropertyBag schema"), *ScopeLabel);
	return false;
}
```

- [ ] **Step 5: Set values through reflected properties**

After `AddProperties()`, set each `value`:

```cpp
FStructView MutableValue = Bag.GetMutableValue();
for (const FAFStateTreeParameterSpec& Param : Spec.Parameters)
{
	if (!Param.bHasValue)
	{
		continue;
	}
	const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(FName(*Param.Name));
	if (!Desc || !Desc->CachedProperty)
	{
		OutError = FString::Printf(TEXT("StateTree parameter '%s.%s' has no reflected property after schema update"), *ScopeLabel, *Param.Name);
		return false;
	}
	void* ValuePtr = Desc->CachedProperty->ContainerPtrToValuePtr<void>(MutableValue.GetMemory());
	if (!FPropertySetterUtils::SetPropertyValueFromJson(const_cast<FProperty*>(Desc->CachedProperty), ValuePtr, Param.Value, nullptr, ScopeLabel + TEXT(".") + Param.Name))
	{
		OutError = FString::Printf(TEXT("StateTree parameter '%s.%s' value does not match type '%s'"), *ScopeLabel, *Param.Name, *Param.Type);
		return false;
	}
}
```

- [ ] **Step 6: Integrate root and state parameters**

In `ApplyStateTreeConfig()`, after parsing `RootParametersSpec`, apply it before evaluator/global/state construction:

```cpp
if (!UE::AssetFactory::StateTree::ApplyParameterBagSpec(EditorData.RootParameterPropertyBag, RootParametersSpec, EditorData.GetTypedOuter<UPackage>()->GetPathName(), TEXT("root"), OutError))
{
	return false;
}
```

In `BuildStateRecursive()`, after `ApplyStateProperties()`:

```cpp
if (!UE::AssetFactory::StateTree::ApplyParameterBagSpec(State.Parameters.Parameters, Spec.Parameters, EditorData.GetTypedOuter<UPackage>()->GetPathName(), Spec.CanonicalPath, OutError))
{
	return false;
}
```

- [ ] **Step 7: Create 4A basic fixtures**

Create `TestData/ST_Parameters_Basic.json`:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Parameters_Basic",
	"Path": "/Game/AFSmoke",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"RootParameters": {
		"MoveSpeed": { "type": "Float", "value": 600.0 },
		"DisplayName": { "type": "Text", "value": "Guard" },
		"TargetActor": { "type": "Object:/Script/Engine.Actor", "value": "" },
		"TargetClass": { "type": "Class:/Script/Engine.Actor", "value": "/Script/Engine.Actor" }
	},
	"SubTrees": [
		{
			"name": "Root",
			"children": [
				{
					"name": "Idle",
					"parameters": {
						"IdleDuration": { "type": "Float", "value": 0.25 },
						"bLoop": { "type": "Bool", "value": true }
					}
				}
			]
		}
	]
}
```

- [ ] **Step 8: Create 4A invalid fixtures**

Create `ST_Parameters_Invalid_UnknownType.json` with:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Parameters_Invalid_UnknownType",
	"Path": "/Game/AFSmoke",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"RootParameters": {
		"Bad": { "type": "DefinitelyNotAType", "value": 1 }
	},
	"SubTrees": [{ "name": "Root" }]
}
```

Create `ST_Parameters_Invalid_BadGuid.json` with `id: "not-a-guid"`.

Create `ST_Parameters_Invalid_BadValue.json` with `type: "Float"` and `value: "not numeric"`.

- [ ] **Step 9: Verify 4A**

Run:

```bash
npm --prefix MCP run build
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected:

- MCP build exits 0.
- UBT exits 0 without triggering full engine rebuild beyond normal plugin/project actions.

Then run GUI editor/MCP smoke:

```bash
open -a "/Users/pengao/UnrealEngine/Engine/Binaries/Mac/UnrealEditor.app" --args "/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject"
```

Use MCP `generate_assets` for `ST_Parameters_Basic.json`, open the generated StateTree in GUI, and visually confirm root/state parameters are visible.

- [ ] **Step 10: Commit Task 2**

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.* Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp TestData/ST_Parameters_Basic.json TestData/ST_Parameters_Invalid_*.json
git commit -m "feat: add StateTree basic parameters"
```

## Task 3: 实现参数 extract round-trip

**Files:**

- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.h`

- [ ] **Step 1: Add type string extraction**

Implement helper:

```cpp
FString MakeTypeString(const FPropertyBagPropertyDesc& Desc)
{
	FString Base;
	switch (Desc.ValueType)
	{
	case EPropertyBagPropertyType::Bool: Base = TEXT("Bool"); break;
	case EPropertyBagPropertyType::Byte: Base = Desc.ValueTypeObject ? FString::Printf(TEXT("Enum:%s"), *Desc.ValueTypeObject->GetPathName()) : TEXT("Byte"); break;
	case EPropertyBagPropertyType::Int32: Base = TEXT("Int32"); break;
	case EPropertyBagPropertyType::Int64: Base = TEXT("Int64"); break;
	case EPropertyBagPropertyType::UInt32: Base = TEXT("UInt32"); break;
	case EPropertyBagPropertyType::UInt64: Base = TEXT("UInt64"); break;
	case EPropertyBagPropertyType::Float: Base = TEXT("Float"); break;
	case EPropertyBagPropertyType::Double: Base = TEXT("Double"); break;
	case EPropertyBagPropertyType::Name: Base = TEXT("Name"); break;
	case EPropertyBagPropertyType::String: Base = TEXT("String"); break;
	case EPropertyBagPropertyType::Text: Base = TEXT("Text"); break;
	case EPropertyBagPropertyType::Enum: Base = FString::Printf(TEXT("Enum:%s"), *GetPathNameSafe(Desc.ValueTypeObject)); break;
	case EPropertyBagPropertyType::Struct: Base = FString::Printf(TEXT("Struct:%s"), *GetPathNameSafe(Desc.ValueTypeObject)); break;
	case EPropertyBagPropertyType::Object: Base = FString::Printf(TEXT("Object:%s"), *GetPathNameSafe(Desc.ValueTypeObject)); break;
	case EPropertyBagPropertyType::SoftObject: Base = FString::Printf(TEXT("SoftObject:%s"), *GetPathNameSafe(Desc.ValueTypeObject)); break;
	case EPropertyBagPropertyType::Class: Base = FString::Printf(TEXT("Class:%s"), *GetPathNameSafe(Desc.ValueTypeObject)); break;
	case EPropertyBagPropertyType::SoftClass: Base = FString::Printf(TEXT("SoftClass:%s"), *GetPathNameSafe(Desc.ValueTypeObject)); break;
	default: Base = TEXT("None"); break;
	}
	for (int32 Index = Desc.ContainerTypes.Num() - 1; Index >= 0; --Index)
	{
		const EPropertyBagContainerType Container = Desc.ContainerTypes[Index];
		if (Container == EPropertyBagContainerType::Array)
		{
			Base = TEXT("Array:") + Base;
		}
		else if (Container == EPropertyBagContainerType::Set)
		{
			Base = TEXT("Set:") + Base;
		}
	}
	return Base;
}
```

- [ ] **Step 2: Implement ExtractParameterBag**

```cpp
TSharedPtr<FJsonObject> UE::AssetFactory::StateTree::ExtractParameterBag(const FInstancedPropertyBag& Bag, bool bDiffOnly)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
	if (!BagStruct)
	{
		return Result;
	}

	const FConstStructView Value = Bag.GetValue();
	for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
	{
		if (!Desc.CachedProperty)
		{
			continue;
		}
		const void* ValuePtr = Desc.CachedProperty->ContainerPtrToValuePtr<void>(Value.GetMemory());
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), Desc.ID.ToString(EGuidFormats::DigitsWithHyphensLower));
		Entry->SetStringField(TEXT("type"), MakeTypeString(Desc));
		if (TSharedPtr<FJsonValue> JsonValue = FPropertySetterUtils::ExtractPropertyToJson(const_cast<FProperty*>(Desc.CachedProperty), ValuePtr))
		{
			Entry->SetField(TEXT("value"), JsonValue);
		}
		Result->SetObjectField(Desc.Name.ToString(), Entry);
	}
	return Result;
}
```

- [ ] **Step 3: Wire root extract**

In `FStateTreeGenerator::Extract()` in `StateTreeGenerator.cpp`, inside the existing `if (EditorData)` block after schema extraction and before `SubTrees`, add:

```cpp
TSharedPtr<FJsonObject> RootParameters = UE::AssetFactory::StateTree::ExtractParameterBag(EditorData->GetRootParametersPropertyBag(), bDiffOnly);
if (RootParameters.IsValid() && RootParameters->Values.Num() > 0)
{
	OutJson->SetObjectField(TEXT("RootParameters"), RootParameters);
}
```

- [ ] **Step 4: Wire state extract**

In `ExtractState()`:

```cpp
TSharedPtr<FJsonObject> Parameters = UE::AssetFactory::StateTree::ExtractParameterBag(State.Parameters.Parameters, bDiffOnly);
if (Parameters.IsValid() && Parameters->Values.Num() > 0)
{
	StateJson->SetObjectField(TEXT("parameters"), Parameters);
}
```

- [ ] **Step 5: Verify extract round-trip**

Generate `ST_Parameters_Basic`, call `extract_assets`, and verify output contains:

- `RootParameters.MoveSpeed.id`
- `RootParameters.MoveSpeed.type == "Float"`
- `RootParameters.MoveSpeed.value == 600`
- `SubTrees[0].children[0].parameters.IdleDuration.type == "Float"`

Then regenerate the extracted JSON under a new name:

```json
{
	"Name": "ST_Parameters_Basic_RoundTrip",
	"Path": "/Game/AFSmoke"
}
```

Expected: generation succeeds and extracted IDs remain stable.

- [ ] **Step 6: Commit Task 3**

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.* Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.*
git commit -m "feat: extract StateTree parameters"
```

## Task 4: 支持复杂类型与容器冒烟 4B

**Files:**

- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.cpp`
- Create: `TestData/ST_Parameters_Complex.json`

- [ ] **Step 1: Add Array and Set type mapping**

Extend `ParsePropertyBagType()`:

```cpp
if (BaseType.Equals(TEXT("Array"), ESearchCase::IgnoreCase) || BaseType.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
{
	FAFPropertyBagTypeDesc InnerDesc;
	if (!ParsePropertyBagType(Parsed.SubType, InnerDesc, OutError))
	{
		return false;
	}
	OutDesc = InnerDesc;
	OutDesc.Containers.Add(BaseType.Equals(TEXT("Array"), ESearchCase::IgnoreCase)
		? EPropertyBagContainerType::Array
		: EPropertyBagContainerType::Set);
	return true;
}
```

Add explicit `Map` rejection because UE 5.7 `EPropertyBagContainerType` does not expose `Map`:

```cpp
if (BaseType.Equals(TEXT("Map"), ESearchCase::IgnoreCase))
{
	OutError = TEXT("StateTree PropertyBag parameters do not support Map containers in UE 5.7");
	return false;
}
```

- [ ] **Step 2: Verify container value setting**

Keep using `FPropertySetterUtils::SetPropertyValueFromJson()` after descriptor creation. It already supports `FArrayProperty` and `FSetProperty` when a reflected property exists. If UE creates a bag property that is not `FArrayProperty`/`FSetProperty`, fail with:

```cpp
OutError = FString::Printf(TEXT("StateTree parameter '%s.%s' created unsupported reflected property '%s'"), *ScopeLabel, *Param.Name, *Desc->CachedProperty->GetClass()->GetName());
return false;
```

- [ ] **Step 3: Create complex fixture**

Create `TestData/ST_Parameters_Complex.json`:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Parameters_Complex",
	"Path": "/Game/AFSmoke",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"RootParameters": {
		"SpawnOffset": {
			"type": "Struct:/Script/CoreUObject.Vector",
			"value": { "X": 10, "Y": 20, "Z": 30 }
		},
		"Waypoints": {
			"type": "Array:Struct:/Script/CoreUObject.Vector",
			"value": [
				{ "X": 0, "Y": 0, "Z": 0 },
				{ "X": 300, "Y": 0, "Z": 0 }
			]
		},
		"Names": {
			"type": "Set:Name",
			"value": ["Alpha", "Beta"]
		}
	},
	"SubTrees": [{ "name": "Root" }]
}
```

- [ ] **Step 4: Create Map negative fixture**

Create `ST_Parameters_Invalid_MapUnsupported.json`:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Parameters_Invalid_MapUnsupported",
	"Path": "/Game/AFSmoke",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"RootParameters": {
		"BadMap": { "type": "Map:String:Int32", "value": { "A": 1 } }
	},
	"SubTrees": [{ "name": "Root" }]
}
```

- [ ] **Step 5: Verify 4B**

Run MCP `generate_assets` for `ST_Parameters_Complex.json`.

Expected:

- Asset generation succeeds.
- GUI editor shows `SpawnOffset`, `Waypoints`, and `Names`.
- Extract emits `Struct:/Script/CoreUObject.Vector`, `Array:Struct:/Script/CoreUObject.Vector`, and `Set:Name`.

Run `generate_assets` for `ST_Parameters_Invalid_MapUnsupported.json`.

Expected: request fails with `do not support Map containers in UE 5.7`.

- [ ] **Step 6: Commit Task 4**

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.cpp TestData/ST_Parameters_Complex.json TestData/ST_Parameters_Invalid_MapUnsupported.json
git commit -m "feat: support StateTree complex parameters"
```

## Task 5: 支持 overrides 与 linked 参数同步 4C

**Files:**

- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp`
- Create linked fixtures listed in file map.

- [ ] **Step 1: Add override apply helper**

Add declaration:

```cpp
bool ApplyParameterOverrides(
	FStateTreeStateParameters& Parameters,
	const FAFStateTreeParameterBagSpec& OverrideSpec,
	const FString& ScopeLabel,
	FString& OutError);
```

Implementation:

```cpp
bool UE::AssetFactory::StateTree::ApplyParameterOverrides(
	FStateTreeStateParameters& Parameters,
	const FAFStateTreeParameterBagSpec& OverrideSpec,
	const FString& ScopeLabel,
	FString& OutError)
{
	for (const FAFStateTreeParameterSpec& Override : OverrideSpec.Parameters)
	{
		const FPropertyBagPropertyDesc* Desc = Parameters.Parameters.FindPropertyDescByName(FName(*Override.Name));
		if (!Desc)
		{
			OutError = FString::Printf(TEXT("StateTree parameter override '%s.%s' references unknown parameter"), *ScopeLabel, *Override.Name);
			return false;
		}
		if (Override.bHasValue)
		{
			FStructView Value = Parameters.Parameters.GetMutableValue();
			void* ValuePtr = Desc->CachedProperty->ContainerPtrToValuePtr<void>(Value.GetMemory());
			if (!FPropertySetterUtils::SetPropertyValueFromJson(const_cast<FProperty*>(Desc->CachedProperty), ValuePtr, Override.Value, nullptr, ScopeLabel + TEXT(".") + Override.Name))
			{
				OutError = FString::Printf(TEXT("StateTree parameter override '%s.%s' value does not match existing parameter type"), *ScopeLabel, *Override.Name);
				return false;
			}
		}
		Parameters.SetParametersPropertyOverridden(Desc->ID, Override.bOverridden || Override.bHasValue);
	}
	return true;
}
```

- [ ] **Step 2: Apply ordinary state overrides**

In `BuildStateRecursive()`, after applying `Spec.Parameters`, call:

```cpp
if (!UE::AssetFactory::StateTree::ApplyParameterOverrides(State.Parameters, Spec.ParameterOverrides, Spec.CanonicalPath, OutError))
{
	return false;
}
```

- [ ] **Step 3: Sync linked state parameters before overrides**

In `FinalizeStateRecursive()`, after `ApplyLinkedState()` and before transitions:

```cpp
if (Spec.Type == EStateTreeStateType::Linked || Spec.Type == EStateTreeStateType::LinkedAsset)
{
	State.Parameters.UpdateParametersFromLinkedSubtree();
	if (!UE::AssetFactory::StateTree::ApplyParameterOverrides(State.Parameters, Spec.ParameterOverrides, Spec.CanonicalPath, OutError))
	{
		return false;
	}
}
```

If `UpdateParametersFromLinkedSubtree()` fails to sync linked asset in practice, mirror `StateTreeEditingSubsystem.cpp` behavior around its `UpdateLinkedStateParameters` lambda and keep the call in one adapter/state-builder helper.

- [ ] **Step 4: Extract override flags**

Update `ExtractParameterBag()` or add `ExtractParameterOverrides()` so state extraction can mark overridden properties:

```cpp
if (State.Parameters.IsParametersPropertyOverridden(Desc.ID))
{
	Entry->SetBoolField(TEXT("overridden"), true);
}
```

For linked states, emit overridden entries under `parameterOverrides` when `State.Type` is `Linked` or `LinkedAsset`; emit ordinary state-local entries under `parameters` for non-linked states. In both cases, mark each overridden entry with `overridden: true`.

- [ ] **Step 5: Create linked fixtures**

Create `ST_Parameters_LinkedTarget.json`:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Parameters_LinkedTarget",
	"Path": "/Game/AFSmoke",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"RootParameters": {
		"LinkedSpeed": { "type": "Float", "value": 200.0 }
	},
	"SubTrees": [{ "name": "Root" }]
}
```

Create `ST_Parameters_LinkedReferencer.json`:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Parameters_LinkedReferencer",
	"Path": "/Game/AFSmoke",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"SubTrees": [
		{
			"name": "Root",
			"children": [
				{
					"name": "UseLinkedAsset",
					"type": "LinkedAsset",
					"linkedAsset": "/Game/AFSmoke/ST_Parameters_LinkedTarget.ST_Parameters_LinkedTarget",
					"parameterOverrides": {
						"LinkedSpeed": { "type": "Float", "value": 350.0, "overridden": true }
					}
				}
			]
		}
	]
}
```

Also create invalid linked override fixtures:

- `ST_Parameters_Invalid_LinkedOverrideUnknown.json` uses `"MissingParam"`.
- `ST_Parameters_Invalid_LinkedOverrideTypeMismatch.json` uses `"LinkedSpeed": { "type": "Float", "value": "fast" }`.

- [ ] **Step 6: Verify 4C**

Generate linked target first, then referencer.

Expected:

- Target generation succeeds.
- Referencer generation succeeds.
- GUI editor shows linked asset state parameter override.
- Extract for referencer includes `LinkedSpeed`, its original ID from linked target schema, and value `350.0`.

Run invalid fixtures.

Expected:

- Unknown override fails with `references unknown parameter`.
- Type mismatch fails with `value does not match existing parameter type`.

- [ ] **Step 7: Commit Task 5**

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreePropertyBagAdapter.* Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp TestData/ST_Parameters_Linked*.json TestData/ST_Parameters_Invalid_Linked*.json
git commit -m "feat: add StateTree parameter overrides"
```

## Task 6: 文档、MCP schema 与最终真实冒烟

**Files:**

- Modify: `MCP/schemas/StateTree.md`
- Modify: `MCP/src/index.ts` only when `rg -n "StateTree|RootParameters|parameters" MCP/src/index.ts` shows StateTree schema text that must mention parameters.
- Modify: `docs/superpowers/specs/2026-04-27-statetree-generator-spec-map.md` only if it tracks status.

- [ ] **Step 1: Update MCP StateTree docs**

Add sections to `MCP/schemas/StateTree.md`:

```markdown
## Parameters

`RootParameters` and state `parameters` use the same typed property shape as Blueprint generator properties:

```json
{
	"RootParameters": {
		"MoveSpeed": { "type": "Float", "value": 600.0 },
		"SpawnOffset": { "type": "Struct:/Script/CoreUObject.Vector", "value": { "X": 0, "Y": 0, "Z": 80 } }
	}
}
```

Supported parameter types: `Bool`, `Byte`, `Int32`, `Int64`, `UInt32`, `UInt64`, `Float`, `Double`, `Name`, `String`, `Text`, `Enum:<EnumName>`, `Struct:<StructName>`, `Object:<ClassName>`, `SoftObject:<ClassName>`, `Class:<ClassName>`, `SoftClass:<ClassName>`, `Array:<ElementType>`, `Set:<ElementType>`.

`Map` is rejected for StateTree PropertyBag parameters on UE 5.7 because `EPropertyBagContainerType` does not expose a map container.
```
```

- [ ] **Step 2: Build MCP**

Run:

```bash
npm --prefix MCP run build
```

Expected: exit 0.

- [ ] **Step 3: Full UBT build**

Run:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected: exit 0.

- [ ] **Step 4: Real GUI/MCP smoke**

Use the same GUI editor path as previous specs. Generate and extract these fixtures:

- `ST_Parameters_Basic.json`
- `ST_Parameters_Complex.json`
- `ST_Parameters_LinkedTarget.json`
- `ST_Parameters_LinkedReferencer.json`

For each successful fixture, collect:

- `generate_assets` response success.
- `extract_assets` response containing expected parameter names and IDs.
- GUI screenshot or user-visible confirmation that parameters appear in the StateTree editor details panel.

Run invalid fixtures and collect stable failure snippets:

- `ST_Parameters_Invalid_UnknownType.json`
- `ST_Parameters_Invalid_BadGuid.json`
- `ST_Parameters_Invalid_BadValue.json`
- `ST_Parameters_Invalid_MapUnsupported.json`
- `ST_Parameters_Invalid_LinkedOverrideUnknown.json`
- `ST_Parameters_Invalid_LinkedOverrideTypeMismatch.json`

- [ ] **Step 5: Final status review**

Run:

```bash
git status --short
git log --oneline --decorate -5
```

Expected:

- Only intentional source/docs/fixtures are modified.
- No generated `Content/AFSmoke` or ProjectRPG assets are staged in AssetFactory.

- [ ] **Step 6: Commit docs/schema/final fixtures**

```bash
git add MCP/schemas/StateTree.md MCP/src/index.ts docs/superpowers/specs/2026-04-27-statetree-generator-spec-map.md TestData/ST_Parameters_*.json
git commit -m "docs: document StateTree parameters"
```

If `MCP/src/index.ts` or spec-map did not change after the `rg` check, omit those paths from `git add`.

## Implementation Completion Checklist

- [ ] `npm --prefix MCP run build` passes.
- [ ] ProjectRPGEditor UBT build passes.
- [ ] Real GUI editor launches from the normal app path, not `UnrealEditor-Cmd`, not `-NullRHI`.
- [ ] MCP `generate_assets` succeeds for 4A, 4B, and 4C positive fixtures.
- [ ] MCP `extract_assets` shows parameter `id/type/value` round-trip.
- [ ] GUI editor visibly shows root/state parameters.
- [ ] Invalid fixtures fail with stable, readable errors.
- [ ] No smoke-only assets are committed into AssetFactory.
- [ ] Branch has small commits for parser, basic generation, extraction, complex types, overrides, and docs.
