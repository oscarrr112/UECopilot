# WidgetBlueprint Binding Round-Trip Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make WidgetBlueprint property/function bindings round-trip through `extract_assets -> generate_assets -> extract_assets` while validating UMG binding targets before saving assets.

**Architecture:** Keep WidgetBlueprint bindings authored and extracted in widget-level `Bindings` as the canonical JSON format. Add a normalization layer that also accepts legacy top-level `Bindings: { "Widget.Property": ... }`, then build `FDelegateEditorBinding` through UE reflection so `SourcePath`, `SourceProperty`, `MemberGuid`, function signatures, and pure/const requirements are validated before save.

**Tech Stack:** Unreal Engine 5.7 editor C++, UMG/UMGEditor `FDelegateEditorBinding`, AssetFactory MCP `generate_assets` / `extract_assets`, UE Python verification scripts, Markdown MCP schema docs.

---

## Coordination Notes

- Work in the Git repo at `E:/GameDev/PluginsWarehouse/Plugins/UECopilot`.
- Do not modify unrelated untracked files already present in `git status`.
- The repository instruction says not to commit unless the user explicitly asks. Each task therefore ends with a checkpoint command instead of `git commit`.
- If the user explicitly requests commits during execution, use the commit messages listed in each checkpoint.

## Source Spec

- `docs/superpowers/specs/2026-04-27-widget-blueprint-binding-roundtrip-design.md`

## File Map

- Modify `Source/AssetFactory/Public/Generators/WidgetBlueprintGenerator.h`
  - Change binding-related helper signatures so failures can return to `Generate`.
  - Add top-level binding compatibility helper declaration.
  - Add extractor overload that accepts per-widget binding JSON.

- Modify `Source/AssetFactory/Private/Generators/WidgetBlueprintGenerator.cpp`
  - Add binding normalization structs in an anonymous namespace.
  - Parse widget-level and top-level binding shapes into a single internal spec.
  - Resolve target widget delegate properties through reflection.
  - Build property bindings with `FEditorPropertyPath`, `SourceProperty`, and `MemberGuid`.
  - Build function bindings with target delegate, signature, and pure/const checks.
  - Make extractor emit widget-level canonical `Bindings`.

- Modify `Source/AssetFactory/Public/Test/TestUserWidget.h`
  - Add deliberately invalid binding functions used by MCP negative tests.

- Modify `Source/AssetFactory/Private/Test/TestUserWidget.cpp`
  - Implement the new test functions.

- Modify `MCP/schemas/WidgetBlueprint.md`
  - Document canonical widget-level bindings.
  - Document top-level compatibility input.
  - Document `Property`, `SourcePath`, `MemberGuid`, and function validation rules.

- Create `docs/superpowers/verification/widget_binding_roundtrip_check.py`
  - UE Python diagnostics for generated WidgetBlueprint binding internals.

## Task 1: Add Verification Fixtures And Red Checks

**Files:**
- Modify: `Source/AssetFactory/Public/Test/TestUserWidget.h`
- Modify: `Source/AssetFactory/Private/Test/TestUserWidget.cpp`
- Create: `docs/superpowers/verification/widget_binding_roundtrip_check.py`

- [ ] **Step 1: Add negative-test functions to `TestUserWidget.h`**

Insert these declarations after `IsTextEnabled() const;`:

```cpp
	/** Invalid for Text binding: returns an unsupported object type */
	UFUNCTION(BlueprintCallable, Category = "Bindings")
	UObject* GetObjectForText() const;

	/** Invalid for property delegates: not const and not BlueprintPure */
	UFUNCTION(BlueprintCallable, Category = "Bindings")
	FText GetImpureDisplayText();

	/** Invalid for property delegates: has an input parameter */
	UFUNCTION(BlueprintCallable, Category = "Bindings")
	FText GetTextWithParameter(int32 Value) const;
```

- [ ] **Step 2: Implement the negative-test functions in `TestUserWidget.cpp`**

Append these definitions:

```cpp
UObject* UTestUserWidget::GetObjectForText() const
{
	return nullptr;
}

FText UTestUserWidget::GetImpureDisplayText()
{
	return DisplayText;
}

FText UTestUserWidget::GetTextWithParameter(int32 Value) const
{
	return FText::AsNumber(Value);
}
```

- [ ] **Step 3: Create `docs/superpowers/verification/widget_binding_roundtrip_check.py`**

Create the file with this content:

```python
import json
import unreal

ASSET_PATHS = [
    "/Game/UECopilotTests/WidgetBindings/WBP_BindingRoundTrip_Function.WBP_BindingRoundTrip_Function",
    "/Game/UECopilotTests/WidgetBindings/WBP_BindingRoundTrip_Property.WBP_BindingRoundTrip_Property",
    "/Game/UECopilotTests/WidgetBindings/WBP_BindingRoundTrip_TopLevel.WBP_BindingRoundTrip_TopLevel",
]


def _get_editor_property(obj, *names):
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception:
            continue
    return None


def _name(value):
    return str(value) if value is not None else ""


def _source_path_segments(binding):
    source_path = _get_editor_property(binding, "source_path", "SourcePath")
    if source_path is None:
        return []
    segments = _get_editor_property(source_path, "segments", "Segments") or []
    names = []
    for segment in segments:
        member_name = _get_editor_property(segment, "member_name", "MemberName")
        names.append(_name(member_name))
    return [name for name in names if name and name != "None"]


def _binding_to_dict(binding):
    return {
        "objectName": _name(_get_editor_property(binding, "object_name", "ObjectName")),
        "propertyName": _name(_get_editor_property(binding, "property_name", "PropertyName")),
        "functionName": _name(_get_editor_property(binding, "function_name", "FunctionName")),
        "sourceProperty": _name(_get_editor_property(binding, "source_property", "SourceProperty")),
        "kind": _name(_get_editor_property(binding, "kind", "Kind")),
        "sourcePath": _source_path_segments(binding),
    }


result = {"assets": {}, "errors": []}

for asset_path in ASSET_PATHS:
    asset = unreal.load_asset(asset_path)
    if asset is None:
        result["errors"].append(f"Missing asset: {asset_path}")
        continue

    bindings = _get_editor_property(asset, "bindings", "Bindings") or []
    binding_dicts = [_binding_to_dict(binding) for binding in bindings]
    result["assets"][asset_path] = binding_dicts

    if not binding_dicts:
        result["errors"].append(f"No bindings found: {asset_path}")

property_asset = ASSET_PATHS[1]
property_bindings = result["assets"].get(property_asset, [])
if property_bindings:
    text_binding = next((binding for binding in property_bindings if binding["propertyName"] == "Text"), None)
    if text_binding is None:
        result["errors"].append("Property fixture is missing Text binding")
    elif not text_binding["sourcePath"]:
        result["errors"].append("Property fixture Text binding has an empty SourcePath")

top_level_asset = ASSET_PATHS[2]
top_level_bindings = result["assets"].get(top_level_asset, [])
if top_level_bindings:
    if not any(binding["objectName"] == "LegacyText" and binding["propertyName"] == "Text" for binding in top_level_bindings):
        result["errors"].append("Top-level compatibility fixture did not create LegacyText.Text binding")

if result["errors"]:
    raise Exception(json.dumps(result, ensure_ascii=False))

print(json.dumps(result, ensure_ascii=False))
```

- [ ] **Step 4: Run UBT to compile the test function additions**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: exit code `0`.

- [ ] **Step 5: Red-check current round-trip breakage**

Start the editor if it is not running:

```powershell
Start-Process -FilePath "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject"
```

Wait for MCP `health_check` to return success.

Use MCP `generate_assets` with this payload:

```json
{
  "assets": [
    {
      "AssetType": "WidgetBlueprint",
      "Name": "WBP_BindingRoundTrip_Function",
      "Path": "/Game/UECopilotTests/WidgetBindings",
      "Action": "CreateOrUpdate",
      "ParentClass": "TestUserWidget",
      "RootWidget": {
        "Type": "CanvasPanel",
        "Name": "Root",
        "Children": [
          {
            "Type": "TextBlock",
            "Name": "ScoreText",
            "Bindings": {
              "Text": "GetDisplayText"
            }
          }
        ]
      }
    }
  ]
}
```

Then use MCP `extract_assets` on `/Game/UECopilotTests/WidgetBindings/WBP_BindingRoundTrip_Function`.

Expected before the fix: extracted JSON contains top-level `Bindings` and does not contain `RootWidget.Children[0].Bindings.Text`. This confirms the red state.

- [ ] **Step 6: Checkpoint**

Run:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors. Changed files are the two `TestUserWidget` files and the new verification script.

Suggested commit message if the user explicitly asks for commits:

```text
test(widget): add binding round-trip verification fixtures
```

## Task 2: Add Binding Error Propagation To WidgetBlueprintGenerator

**Files:**
- Modify: `Source/AssetFactory/Public/Generators/WidgetBlueprintGenerator.h`
- Modify: `Source/AssetFactory/Private/Generators/WidgetBlueprintGenerator.cpp`

- [ ] **Step 1: Update helper signatures in `WidgetBlueprintGenerator.h`**

Replace the existing declarations with these signatures:

```cpp
	UWidget* BuildWidgetTree(UWidgetBlueprint* Blueprint, TSharedPtr<FJsonObject> WidgetNode, UPanelWidget* Parent, const FString& JsonPath, FString* OutError = nullptr);

	bool ProcessWidgetUpdates(UWidgetBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>* UpdatesArray, FString& OutError);

	bool ConfigureBindings(UWidgetBlueprint* Blueprint, UWidget* Widget, const FString& WidgetName, TSharedPtr<FJsonObject> BindingsConfig, FString& OutError);

	bool ConfigureTopLevelBindings(UWidgetBlueprint* Blueprint, TSharedPtr<FJsonObject> TopLevelBindingsConfig, FString& OutError);

	TSharedPtr<FJsonObject> ExtractWidgetTree(UWidget* Widget, const TMap<FString, TSharedPtr<FJsonObject>>* BindingsByWidget = nullptr) const;
```

- [ ] **Step 2: Thread `OutError` through `Generate` create/update paths**

In `Generate`, create one local error string before tree operations:

```cpp
	FString BindingError;
```

For existing asset `RootWidget` rebuild, change the build call to:

```cpp
			UWidget* RootWidget = BuildWidgetTree(Blueprint, RootWidgetConfig, nullptr, TEXT("RootWidget"), &BindingError);
			if (!RootWidget)
			{
				const FString Reason = BindingError.IsEmpty() ? TEXT("Failed to build widget tree") : BindingError;
				return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Reason);
			}
```

For `WidgetUpdates`, change the call to:

```cpp
			if (!ProcessWidgetUpdates(Blueprint, WidgetUpdatesArray, BindingError))
			{
				const FString Reason = BindingError.IsEmpty() ? TEXT("Failed to process WidgetUpdates") : BindingError;
				return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Reason);
			}
```

For new asset create, change the build call to:

```cpp
		UWidget* RootWidget = BuildWidgetTree(Blueprint, RootWidgetConfig, nullptr, TEXT("RootWidget"), &BindingError);
		if (!RootWidget)
		{
			const FString Reason = BindingError.IsEmpty() ? TEXT("Failed to build widget tree") : BindingError;
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Reason);
		}
```

- [ ] **Step 3: Add top-level compatibility binding application in `Generate`**

After the create/update tree path has completed and before the first `CompileBlueprint`, add:

```cpp
	TSharedPtr<FJsonObject> TopLevelBindingsConfig = GetObjectField(Config, TEXT("Bindings"));
	if (TopLevelBindingsConfig.IsValid())
	{
		if (!ConfigureTopLevelBindings(Blueprint, TopLevelBindingsConfig, BindingError))
		{
			const FString Reason = BindingError.IsEmpty() ? TEXT("Failed to configure top-level WidgetBlueprint bindings") : BindingError;
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Reason);
		}
	}
```

- [ ] **Step 4: Update `BuildWidgetTree` to propagate binding failures**

Change the function definition signature to match the header. Replace the current binding block with:

```cpp
	TSharedPtr<FJsonObject> BindingsConfig = GetObjectField(WidgetNode, TEXT("Bindings"));
	if (BindingsConfig.IsValid())
	{
		if (!bIsVariable)
		{
			UE_LOG(LogAssetFactory, Log, TEXT("[%s] Widget '%s' has Bindings, auto-setting IsVariable to true."), *JsonPath, *WidgetName);
			ExposeAsVariable(Widget, WidgetName);
		}

		FString LocalBindingError;
		if (!ConfigureBindings(Blueprint, Widget, WidgetName, BindingsConfig, LocalBindingError))
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("[%s] %s"), *JsonPath, *LocalBindingError);
			}
			return nullptr;
		}
	}
```

When recursively building child widgets, pass `OutError` into child calls:

```cpp
UWidget* ChildWidget = BuildWidgetTree(Blueprint, ChildObj, PanelWidget, ChildPath, OutError);
if (!ChildWidget)
{
	return nullptr;
}
```

- [ ] **Step 5: Update `ProcessWidgetUpdates` to return bool**

Change the definition signature to:

```cpp
bool FWidgetBlueprintGenerator::ProcessWidgetUpdates(
	UWidgetBlueprint* Blueprint,
	const TArray<TSharedPtr<FJsonValue>>* UpdatesArray,
	FString& OutError)
```

Replace the early null return with:

```cpp
	if (!Blueprint || !UpdatesArray)
	{
		OutError = TEXT("Invalid WidgetUpdates context");
		return false;
	}
```

At the end of the function return success:

```cpp
	return true;
```

Inside the update binding block, replace the current `ConfigureBindings` call with:

```cpp
				FString LocalBindingError;
				if (!ConfigureBindings(Blueprint, Widget, WidgetName, Bindings, LocalBindingError))
				{
					OutError = FString::Printf(TEXT("[%s] %s"), *JsonPath, *LocalBindingError);
					return false;
				}
```

For Add actions that call `BuildWidgetTree`, pass `&OutError` and return false if it fails.

- [ ] **Step 6: Convert `ConfigureBindings` signature**

Change the definition signature to:

```cpp
bool FWidgetBlueprintGenerator::ConfigureBindings(UWidgetBlueprint* Blueprint, UWidget* Widget, const FString& WidgetName, TSharedPtr<FJsonObject> BindingsConfig, FString& OutError)
```

Replace the current null guard with:

```cpp
	if (!Blueprint || !Widget || !BindingsConfig.IsValid())
	{
		OutError = TEXT("Invalid binding configuration context");
		return false;
	}
```

At the end of the function, return `true`.

- [ ] **Step 7: Compile**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: exit code `0`. If compilation fails, fix only signature mismatches from this task.

- [ ] **Step 8: Checkpoint**

Run:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors.

Suggested commit message if the user explicitly asks for commits:

```text
refactor(widget): propagate binding configuration failures
```

## Task 3: Normalize Widget-Level And Top-Level Binding Input

**Files:**
- Modify: `Source/AssetFactory/Private/Generators/WidgetBlueprintGenerator.cpp`

- [ ] **Step 1: Add binding normalization structs near the top of `WidgetBlueprintGenerator.cpp`**

After the include block, add:

```cpp
namespace
{
struct FWidgetBindingSpec
{
	FString WidgetName;
	FName TargetProperty;
	EBindingKind Kind = EBindingKind::Function;
	FName FunctionName = NAME_None;
	FName SourceProperty = NAME_None;
	TArray<FString> SourcePathSegments;
};

static FString BindingTargetToString(const FString& WidgetName, FName TargetProperty)
{
	return FString::Printf(TEXT("%s.%s"), *WidgetName, *TargetProperty.ToString());
}

static bool ReadStringArrayField(TSharedPtr<FJsonObject> Object, const TCHAR* FieldName, TArray<FString>& OutValues)
{
	if (!Object.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
	if (!Object->TryGetArrayField(FieldName, ArrayValues) || !ArrayValues)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *ArrayValues)
	{
		FString Segment;
		if (!Value.IsValid() || !Value->TryGetString(Segment) || Segment.IsEmpty())
		{
			return false;
		}
		OutValues.Add(Segment);
	}
	return OutValues.Num() > 0;
}

static bool ParseBindingKind(const FString& KindString, EBindingKind& OutKind)
{
	if (KindString.Equals(TEXT("Property"), ESearchCase::IgnoreCase))
	{
		OutKind = EBindingKind::Property;
		return true;
	}
	if (KindString.Equals(TEXT("Function"), ESearchCase::IgnoreCase))
	{
		OutKind = EBindingKind::Function;
		return true;
	}
	return false;
}
}
```

- [ ] **Step 2: Add JSON-to-spec parser in the same anonymous namespace**

Add:

```cpp
static bool ParseBindingSpec(
	const FString& WidgetName,
	const FString& PropertyName,
	const TSharedPtr<FJsonValue>& BindingValue,
	FWidgetBindingSpec& OutSpec,
	FString& OutError)
{
	if (WidgetName.IsEmpty())
	{
		OutError = TEXT("Binding is missing widget name");
		return false;
	}

	if (PropertyName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Binding for widget '%s' is missing target property"), *WidgetName);
		return false;
	}

	OutSpec = FWidgetBindingSpec();
	OutSpec.WidgetName = WidgetName;
	OutSpec.TargetProperty = FName(*PropertyName);

	FString SimpleFunctionName;
	if (BindingValue.IsValid() && BindingValue->TryGetString(SimpleFunctionName))
	{
		if (SimpleFunctionName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Binding '%s.%s' has an empty function name"), *WidgetName, *PropertyName);
			return false;
		}
		OutSpec.Kind = EBindingKind::Function;
		OutSpec.FunctionName = FName(*SimpleFunctionName);
		return true;
	}

	if (!BindingValue.IsValid() || BindingValue->Type != EJson::Object)
	{
		OutError = FString::Printf(TEXT("Binding '%s.%s' must be a function string or object"), *WidgetName, *PropertyName);
		return false;
	}

	TSharedPtr<FJsonObject> BindingObj = BindingValue->AsObject();

	FString KindString;
	if (BindingObj->TryGetStringField(TEXT("Kind"), KindString))
	{
		if (!ParseBindingKind(KindString, OutSpec.Kind))
		{
			OutError = FString::Printf(TEXT("Binding '%s.%s' has invalid Kind '%s'"), *WidgetName, *PropertyName, *KindString);
			return false;
		}
	}

	FString FunctionName;
	if (BindingObj->TryGetStringField(TEXT("Function"), FunctionName))
	{
		OutSpec.FunctionName = FName(*FunctionName);
		if (!BindingObj->HasField(TEXT("Kind")))
		{
			OutSpec.Kind = EBindingKind::Function;
		}
	}

	FString SourceProperty;
	if (BindingObj->TryGetStringField(TEXT("Property"), SourceProperty))
	{
		OutSpec.SourceProperty = FName(*SourceProperty);
		OutSpec.SourcePathSegments.Add(SourceProperty);
		if (!BindingObj->HasField(TEXT("Kind")))
		{
			OutSpec.Kind = EBindingKind::Property;
		}
	}

	TArray<FString> SourcePathSegments;
	if (ReadStringArrayField(BindingObj, TEXT("SourcePath"), SourcePathSegments))
	{
		OutSpec.SourcePathSegments = SourcePathSegments;
		if (!BindingObj->HasField(TEXT("Kind")))
		{
			OutSpec.Kind = EBindingKind::Property;
		}
		if (OutSpec.SourcePathSegments.Num() == 1)
		{
			OutSpec.SourceProperty = FName(*OutSpec.SourcePathSegments[0]);
		}
	}

	if (OutSpec.Kind == EBindingKind::Function && OutSpec.FunctionName.IsNone())
	{
		OutError = FString::Printf(TEXT("Function binding '%s.%s' is missing Function"), *WidgetName, *PropertyName);
		return false;
	}

	if (OutSpec.Kind == EBindingKind::Property && OutSpec.SourcePathSegments.Num() == 0)
	{
		OutError = FString::Printf(TEXT("Property binding '%s.%s' is missing Property or SourcePath"), *WidgetName, *PropertyName);
		return false;
	}

	return true;
}
```

- [ ] **Step 3: Add top-level key parser**

Add:

```cpp
static bool ParseTopLevelBindingKey(const FString& BindingKey, FString& OutWidgetName, FString& OutPropertyName)
{
	int32 DotIndex = INDEX_NONE;
	if (!BindingKey.FindLastChar(TEXT('.'), DotIndex) || DotIndex <= 0 || DotIndex >= BindingKey.Len() - 1)
	{
		return false;
	}

	OutWidgetName = BindingKey.Left(DotIndex);
	OutPropertyName = BindingKey.Mid(DotIndex + 1);
	return !OutWidgetName.IsEmpty() && !OutPropertyName.IsEmpty();
}
```

- [ ] **Step 4: Rewrite the start of `ConfigureBindings` to parse specs**

Inside `ConfigureBindings`, keep `ActualWidgetName`, then replace the per-pair binding value parsing block with:

```cpp
		FWidgetBindingSpec Spec;
		FString ParseError;
		if (!ParseBindingSpec(ActualWidgetName, PropertyName, BindingValue, Spec, ParseError))
		{
			OutError = ParseError;
			return false;
		}

		FDelegateEditorBinding NewBinding;
		NewBinding.ObjectName = ActualWidgetName;
		NewBinding.PropertyName = Spec.TargetProperty;
```

Leave the existing validation/add/update logic temporarily in place until Task 4 replaces it.

- [ ] **Step 5: Implement `ConfigureTopLevelBindings`**

Add this method near `ConfigureBindings`:

```cpp
bool FWidgetBlueprintGenerator::ConfigureTopLevelBindings(UWidgetBlueprint* Blueprint, TSharedPtr<FJsonObject> TopLevelBindingsConfig, FString& OutError)
{
	if (!Blueprint || !Blueprint->WidgetTree || !TopLevelBindingsConfig.IsValid())
	{
		OutError = TEXT("Invalid top-level binding configuration context");
		return false;
	}

	for (const auto& Pair : TopLevelBindingsConfig->Values)
	{
		FString WidgetName;
		FString PropertyName;
		if (!ParseTopLevelBindingKey(Pair.Key, WidgetName, PropertyName))
		{
			OutError = FString::Printf(TEXT("Top-level binding key '%s' must use 'WidgetName.PropertyName'"), *Pair.Key);
			return false;
		}

		UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
		if (!Widget)
		{
			OutError = FString::Printf(TEXT("Top-level binding target widget '%s' was not found"), *WidgetName);
			return false;
		}

		TSharedPtr<FJsonObject> SingleBindingObject = MakeShared<FJsonObject>();
		SingleBindingObject->SetField(PropertyName, Pair.Value);

		if (!Widget->bIsVariable)
		{
			ExposeAsVariable(Widget, WidgetName);
		}

		FString LocalError;
		if (!ConfigureBindings(Blueprint, Widget, WidgetName, SingleBindingObject, LocalError))
		{
			OutError = FString::Printf(TEXT("Top-level binding '%s': %s"), *Pair.Key, *LocalError);
			return false;
		}
	}

	return true;
}
```

- [ ] **Step 6: Compile**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: exit code `0`.

- [ ] **Step 7: Checkpoint**

Run:

```powershell
git diff --check
git status --short
```

Suggested commit message if the user explicitly asks for commits:

```text
feat(widget): normalize widget binding input formats
```

## Task 4: Build Valid UE Function And Property Bindings

**Files:**
- Modify: `Source/AssetFactory/Private/Generators/WidgetBlueprintGenerator.cpp`

- [ ] **Step 1: Add the binding-specific includes**

Add these includes with the UMG/editor includes:

```cpp
#include "Binding/PropertyBinding.h"
#include "UObject/Field.h"
```

- [ ] **Step 2: Add reflected target delegate resolver**

In the anonymous namespace, add:

```cpp
static FDelegateProperty* ResolveBindingDelegateProperty(UWidget* Widget, FName TargetProperty, bool& bOutIsPropertyDelegate)
{
	bOutIsPropertyDelegate = false;
	if (!Widget || TargetProperty.IsNone())
	{
		return nullptr;
	}

	const FName PropertyDelegateName(*(TargetProperty.ToString() + TEXT("Delegate")));
	if (FDelegateProperty* PropertyDelegate = FindFProperty<FDelegateProperty>(Widget->GetClass(), PropertyDelegateName))
	{
		bOutIsPropertyDelegate = true;
		return PropertyDelegate;
	}

	return FindFProperty<FDelegateProperty>(Widget->GetClass(), TargetProperty);
}
```

- [ ] **Step 3: Add source class resolver**

Add:

```cpp
static UClass* ResolveBindingSourceClass(UWidgetBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return nullptr;
	}
	if (Blueprint->SkeletonGeneratedClass)
	{
		return Blueprint->SkeletonGeneratedClass;
	}
	if (Blueprint->GeneratedClass)
	{
		return Blueprint->GeneratedClass;
	}
	return Blueprint->ParentClass;
}
```

- [ ] **Step 4: Add binder compatibility helper**

Add:

```cpp
static bool HasFunctionBinder(UFunction* Function, UFunction* DelegateSignature)
{
	if (!Function || !DelegateSignature || Function->NumParms != 1 || DelegateSignature->NumParms != 1)
	{
		return false;
	}

	FProperty* FunctionReturn = Function->GetReturnProperty();
	FProperty* DelegateReturn = DelegateSignature->GetReturnProperty();
	if (!FunctionReturn || !DelegateReturn)
	{
		return false;
	}

	TSubclassOf<UPropertyBinding> Binder = UWidget::FindBinderClassForDestination(DelegateReturn);
	return Binder && Binder->GetDefaultObject<UPropertyBinding>()->IsSupportedSource(FunctionReturn);
}
```

- [ ] **Step 5: Add function resolver**

Add:

```cpp
static UFunction* ResolveBindingFunction(UWidgetBlueprint* Blueprint, FName FunctionName, FGuid& OutMemberGuid)
{
	OutMemberGuid.Invalidate();
	if (!Blueprint || FunctionName.IsNone())
	{
		return nullptr;
	}

	TArray<UClass*> ClassesToCheck;
	if (Blueprint->GeneratedClass)
	{
		ClassesToCheck.Add(Blueprint->GeneratedClass);
	}
	if (Blueprint->SkeletonGeneratedClass)
	{
		ClassesToCheck.Add(Blueprint->SkeletonGeneratedClass);
	}
	if (Blueprint->ParentClass)
	{
		ClassesToCheck.Add(Blueprint->ParentClass);
	}

	for (UClass* ClassToCheck : ClassesToCheck)
	{
		if (UFunction* Function = ClassToCheck->FindFunctionByName(FunctionName, EIncludeSuperFlag::IncludeSuper))
		{
			UBlueprint::GetGuidFromClassByFieldName<UFunction>(Function->GetOwnerClass(), Function->GetFName(), OutMemberGuid);
			return Function;
		}
	}

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FunctionName)
		{
			OutMemberGuid = Graph->GraphGuid;
			return nullptr;
		}
	}

	return nullptr;
}
```

During implementation, if Blueprint graph-only functions need strict signature inspection, resolve their generated `UFunction` after compilation and fail if the compile log reports invalid binding. Native and inherited functions must be strictly validated before adding the binding.

- [ ] **Step 6: Add source path resolver**

Add:

```cpp
static bool ResolvePropertyBindingChain(UClass* SourceClass, const TArray<FString>& SourcePathSegments, TArray<FFieldVariant>& OutChain, FName& OutSourceProperty, FGuid& OutMemberGuid, FText& OutError)
{
	if (!SourceClass)
	{
		OutError = FText::FromString(TEXT("Unable to resolve binding source class"));
		return false;
	}

	if (SourcePathSegments.Num() == 0)
	{
		OutError = FText::FromString(TEXT("Property binding SourcePath is empty"));
		return false;
	}

	UStruct* CurrentStruct = SourceClass;
	OutSourceProperty = NAME_None;
	OutMemberGuid.Invalidate();

	for (int32 Index = 0; Index < SourcePathSegments.Num(); ++Index)
	{
		const FString& Segment = SourcePathSegments[Index];
		if (!CurrentStruct || Segment.IsEmpty())
		{
			OutError = FText::FromString(FString::Printf(TEXT("Invalid SourcePath segment at index %d"), Index));
			return false;
		}

		if (FProperty* Property = FindFProperty<FProperty>(CurrentStruct, FName(*Segment)))
		{
			OutChain.Add(FFieldVariant(Property));
			OutSourceProperty = Property->GetFName();
			if (UClass* OwnerClass = Property->GetOwnerClass())
			{
				UBlueprint::GetGuidFromClassByFieldName<FProperty>(OwnerClass, Property->GetFName(), OutMemberGuid);
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				CurrentStruct = StructProperty->Struct;
			}
			else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				CurrentStruct = ObjectProperty->PropertyClass;
			}
			else
			{
				CurrentStruct = nullptr;
			}
			continue;
		}

		if (UClass* CurrentClass = Cast<UClass>(CurrentStruct))
		{
			if (UFunction* Function = CurrentClass->FindFunctionByName(FName(*Segment), EIncludeSuperFlag::IncludeSuper))
			{
				OutChain.Add(FFieldVariant(Function));
				UBlueprint::GetGuidFromClassByFieldName<UFunction>(Function->GetOwnerClass(), Function->GetFName(), OutMemberGuid);

				FProperty* ReturnProperty = Function->GetReturnProperty();
				if (FStructProperty* StructReturn = CastField<FStructProperty>(ReturnProperty))
				{
					CurrentStruct = StructReturn->Struct;
				}
				else if (FObjectPropertyBase* ObjectReturn = CastField<FObjectPropertyBase>(ReturnProperty))
				{
					CurrentStruct = ObjectReturn->PropertyClass;
				}
				else
				{
					CurrentStruct = nullptr;
				}
				continue;
			}
		}

		OutError = FText::FromString(FString::Printf(TEXT("SourcePath segment '%s' was not found on '%s'"), *Segment, *CurrentStruct->GetName()));
		return false;
	}

	return OutChain.Num() > 0;
}
```

- [ ] **Step 7: Add binding builder helpers**

Add:

```cpp
static bool BuildFunctionBinding(UWidgetBlueprint* Blueprint, UWidget* Widget, const FWidgetBindingSpec& Spec, FDelegateEditorBinding& OutBinding, FText& OutError)
{
	bool bIsPropertyDelegate = false;
	FDelegateProperty* DelegateProperty = ResolveBindingDelegateProperty(Widget, Spec.TargetProperty, bIsPropertyDelegate);
	if (!DelegateProperty)
	{
		OutError = FText::FromString(FString::Printf(TEXT("Target '%s' has no bindable delegate for property '%s'"), *Spec.WidgetName, *Spec.TargetProperty.ToString()));
		return false;
	}

	FGuid MemberGuid;
	UFunction* Function = ResolveBindingFunction(Blueprint, Spec.FunctionName, MemberGuid);
	if (!Function)
	{
		OutError = FText::FromString(FString::Printf(TEXT("Binding function '%s' was not found"), *Spec.FunctionName.ToString()));
		return false;
	}

	const uint64 SignatureFlags = UFunction::GetDefaultIgnoredSignatureCompatibilityFlags() | CPF_ReturnParm;
	if (!Function->IsSignatureCompatibleWith(DelegateProperty->SignatureFunction, SignatureFlags) && !HasFunctionBinder(Function, DelegateProperty->SignatureFunction))
	{
		OutError = FText::FromString(FString::Printf(TEXT("Binding function '%s' signature does not match '%s'"), *Spec.FunctionName.ToString(), *DelegateProperty->GetName()));
		return false;
	}

	if (bIsPropertyDelegate && !Function->HasAnyFunctionFlags(FUNC_Const | FUNC_BlueprintPure))
	{
		OutError = FText::FromString(FString::Printf(TEXT("Binding function '%s' must be const or BlueprintPure for property binding"), *Spec.FunctionName.ToString()));
		return false;
	}

	OutBinding.ObjectName = Widget->GetName();
	OutBinding.PropertyName = Spec.TargetProperty;
	OutBinding.FunctionName = Spec.FunctionName;
	OutBinding.MemberGuid = MemberGuid;
	OutBinding.Kind = EBindingKind::Function;
	return true;
}

static bool BuildPropertyBinding(UWidgetBlueprint* Blueprint, UWidget* Widget, const FWidgetBindingSpec& Spec, FDelegateEditorBinding& OutBinding, FText& OutError)
{
	bool bIsPropertyDelegate = false;
	FDelegateProperty* DelegateProperty = ResolveBindingDelegateProperty(Widget, Spec.TargetProperty, bIsPropertyDelegate);
	if (!DelegateProperty)
	{
		OutError = FText::FromString(FString::Printf(TEXT("Target '%s' has no bindable delegate for property '%s'"), *Spec.WidgetName, *Spec.TargetProperty.ToString()));
		return false;
	}

	TArray<FFieldVariant> BindingChain;
	FName SourcePropertyName = NAME_None;
	FGuid MemberGuid;
	if (!ResolvePropertyBindingChain(ResolveBindingSourceClass(Blueprint), Spec.SourcePathSegments, BindingChain, SourcePropertyName, MemberGuid, OutError))
	{
		return false;
	}

	FEditorPropertyPath SourcePath(BindingChain);
	FText ValidationError;
	if (!SourcePath.Validate(DelegateProperty, ValidationError))
	{
		OutError = ValidationError;
		return false;
	}

	OutBinding.ObjectName = Widget->GetName();
	OutBinding.PropertyName = Spec.TargetProperty;
	OutBinding.SourceProperty = SourcePropertyName;
	OutBinding.SourcePath = SourcePath;
	OutBinding.MemberGuid = MemberGuid;
	OutBinding.Kind = EBindingKind::Property;
	return true;
}
```

If `uint64` does not match the UE 5.7 signature flags type at compile time, use the exact type inferred from `UFunction::GetDefaultIgnoredSignatureCompatibilityFlags()`.

- [ ] **Step 8: Replace old validation inside `ConfigureBindings`**

After parsing `Spec`, replace the old function-name-only validation block with:

```cpp
		FText BindingValidationError;
		if (Spec.Kind == EBindingKind::Function)
		{
			if (!BuildFunctionBinding(Blueprint, Widget, Spec, NewBinding, BindingValidationError))
			{
				OutError = BindingValidationError.ToString();
				return false;
			}
		}
		else
		{
			if (!BuildPropertyBinding(Blueprint, Widget, Spec, NewBinding, BindingValidationError))
			{
				OutError = BindingValidationError.ToString();
				return false;
			}
		}
```

Keep the existing add/update loop, but use `NewBinding.PropertyName` when comparing and logging.

- [ ] **Step 9: Compile**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: exit code `0`.

- [ ] **Step 10: Run MCP negative cases**

Use MCP `generate_assets` for each invalid case. Each should return failure and include a specific error.

Missing function:

```json
{
  "assets": [
    {
      "AssetType": "WidgetBlueprint",
      "Name": "WBP_Binding_Invalid_MissingFunction",
      "Path": "/Game/UECopilotTests/WidgetBindings",
      "Action": "CreateOrUpdate",
      "ParentClass": "TestUserWidget",
      "RootWidget": {
        "Type": "CanvasPanel",
        "Name": "Root",
        "Children": [
          {
            "Type": "TextBlock",
            "Name": "InvalidText",
            "Bindings": {
              "Text": "NoSuchBindingFunction"
            }
          }
        ]
      }
    }
  ]
}
```

Wrong return type:

```json
{
  "assets": [
    {
      "AssetType": "WidgetBlueprint",
      "Name": "WBP_Binding_Invalid_ReturnType",
      "Path": "/Game/UECopilotTests/WidgetBindings",
      "Action": "CreateOrUpdate",
      "ParentClass": "TestUserWidget",
      "RootWidget": {
        "Type": "CanvasPanel",
        "Name": "Root",
        "Children": [
          {
            "Type": "TextBlock",
            "Name": "InvalidText",
            "Bindings": {
              "Text": "GetObjectForText"
            }
          }
        ]
      }
    }
  ]
}
```

Non-pure property delegate function:

```json
{
  "assets": [
    {
      "AssetType": "WidgetBlueprint",
      "Name": "WBP_Binding_Invalid_Impure",
      "Path": "/Game/UECopilotTests/WidgetBindings",
      "Action": "CreateOrUpdate",
      "ParentClass": "TestUserWidget",
      "RootWidget": {
        "Type": "CanvasPanel",
        "Name": "Root",
        "Children": [
          {
            "Type": "TextBlock",
            "Name": "InvalidText",
            "Bindings": {
              "Text": "GetImpureDisplayText"
            }
          }
        ]
      }
    }
  ]
}
```

- [ ] **Step 11: Checkpoint**

Run:

```powershell
git diff --check
git status --short
```

Suggested commit message if the user explicitly asks for commits:

```text
fix(widget): build validated UMG delegate bindings
```

## Task 5: Emit Canonical Widget-Level Bindings From Extract

**Files:**
- Modify: `Source/AssetFactory/Public/Generators/WidgetBlueprintGenerator.h`
- Modify: `Source/AssetFactory/Private/Generators/WidgetBlueprintGenerator.cpp`

- [ ] **Step 1: Add binding JSON extraction helpers**

In the anonymous namespace in `WidgetBlueprintGenerator.cpp`, add:

```cpp
static TArray<FString> ExtractSourcePathSegments(const FDelegateEditorBinding& Binding)
{
	TArray<FString> Segments;
	for (const FEditorPropertyPathSegment& Segment : Binding.SourcePath.Segments)
	{
		const FName MemberName = Segment.GetMemberName();
		if (!MemberName.IsNone())
		{
			Segments.Add(MemberName.ToString());
		}
	}
	return Segments;
}

static TSharedPtr<FJsonObject> BindingToJsonObject(const FDelegateEditorBinding& Binding)
{
	TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
	if (Binding.Kind == EBindingKind::Function)
	{
		BindingObj->SetStringField(TEXT("Kind"), TEXT("Function"));
		BindingObj->SetStringField(TEXT("Function"), Binding.FunctionName.ToString());
		return BindingObj;
	}

	BindingObj->SetStringField(TEXT("Kind"), TEXT("Property"));
	TArray<FString> SourcePathSegments = ExtractSourcePathSegments(Binding);
	if (SourcePathSegments.Num() == 1)
	{
		BindingObj->SetStringField(TEXT("Property"), SourcePathSegments[0]);
	}
	else if (SourcePathSegments.Num() > 1)
	{
		TArray<TSharedPtr<FJsonValue>> SegmentValues;
		for (const FString& Segment : SourcePathSegments)
		{
			SegmentValues.Add(MakeShared<FJsonValueString>(Segment));
		}
		BindingObj->SetArrayField(TEXT("SourcePath"), SegmentValues);
	}
	else
	{
		BindingObj->SetStringField(TEXT("Property"), Binding.SourceProperty.ToString());
	}
	return BindingObj;
}
```

- [ ] **Step 2: Build per-widget binding JSON map in `Extract`**

Replace the current top-level `Bindings` output block with:

```cpp
	TMap<FString, TSharedPtr<FJsonObject>> BindingsByWidget;
	if (Blueprint->Bindings.Num() > 0)
	{
		for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
		{
			TSharedPtr<FJsonObject>* ExistingBindings = BindingsByWidget.Find(Binding.ObjectName);
			if (!ExistingBindings)
			{
				TSharedPtr<FJsonObject> WidgetBindings = MakeShared<FJsonObject>();
				BindingsByWidget.Add(Binding.ObjectName, WidgetBindings);
				ExistingBindings = BindingsByWidget.Find(Binding.ObjectName);
			}

			if (ExistingBindings && ExistingBindings->IsValid())
			{
				(*ExistingBindings)->SetObjectField(Binding.PropertyName.ToString(), BindingToJsonObject(Binding));
			}
		}
	}
```

Pass the map into root extraction:

```cpp
TSharedPtr<FJsonObject> RootWidgetJson = ExtractWidgetTree(Blueprint->WidgetTree->RootWidget, &BindingsByWidget);
```

- [ ] **Step 3: Add `Bindings` output in `ExtractWidgetTree`**

Change the function definition signature:

```cpp
TSharedPtr<FJsonObject> FWidgetBlueprintGenerator::ExtractWidgetTree(UWidget* Widget, const TMap<FString, TSharedPtr<FJsonObject>>* BindingsByWidget) const
```

After properties/style/slot extraction and before children extraction, add:

```cpp
	if (BindingsByWidget)
	{
		if (const TSharedPtr<FJsonObject>* WidgetBindings = BindingsByWidget->Find(Widget->GetName()))
		{
			if (WidgetBindings->IsValid() && (*WidgetBindings)->Values.Num() > 0)
			{
				WidgetJson->SetObjectField(TEXT("Bindings"), *WidgetBindings);
				WidgetJson->SetBoolField(TEXT("IsVariable"), true);
			}
		}
	}
```

When recursively extracting children, pass `BindingsByWidget`:

```cpp
TSharedPtr<FJsonObject> ChildJson = ExtractWidgetTree(ChildWidget, BindingsByWidget);
```

- [ ] **Step 4: Keep default overload behavior**

Any remaining call to `ExtractWidgetTree(Widget)` should compile because the header default argument passes `nullptr`.

- [ ] **Step 5: Compile**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: exit code `0`.

- [ ] **Step 6: Run MCP positive round-trip cases**

Create the function binding fixture:

```json
{
  "assets": [
    {
      "AssetType": "WidgetBlueprint",
      "Name": "WBP_BindingRoundTrip_Function",
      "Path": "/Game/UECopilotTests/WidgetBindings",
      "Action": "CreateOrUpdate",
      "ParentClass": "TestUserWidget",
      "RootWidget": {
        "Type": "CanvasPanel",
        "Name": "Root",
        "Children": [
          {
            "Type": "TextBlock",
            "Name": "ScoreText",
            "Bindings": {
              "Text": "GetDisplayText"
            }
          }
        ]
      }
    }
  ]
}
```

Create the property binding fixture:

```json
{
  "assets": [
    {
      "AssetType": "WidgetBlueprint",
      "Name": "WBP_BindingRoundTrip_Property",
      "Path": "/Game/UECopilotTests/WidgetBindings",
      "Action": "CreateOrUpdate",
      "ParentClass": "TestUserWidget",
      "RootWidget": {
        "Type": "CanvasPanel",
        "Name": "Root",
        "Children": [
          {
            "Type": "TextBlock",
            "Name": "PropertyText",
            "Bindings": {
              "Text": {
                "Kind": "Property",
                "Property": "DisplayText"
              }
            }
          }
        ]
      }
    }
  ]
}
```

Create the top-level compatibility fixture:

```json
{
  "assets": [
    {
      "AssetType": "WidgetBlueprint",
      "Name": "WBP_BindingRoundTrip_TopLevel",
      "Path": "/Game/UECopilotTests/WidgetBindings",
      "Action": "CreateOrUpdate",
      "ParentClass": "TestUserWidget",
      "RootWidget": {
        "Type": "CanvasPanel",
        "Name": "Root",
        "Children": [
          {
            "Type": "TextBlock",
            "Name": "LegacyText"
          }
        ]
      },
      "Bindings": {
        "LegacyText.Text": {
          "Kind": "Function",
          "Function": "GetDisplayText"
        }
      }
    }
  ]
}
```

For each asset, run MCP `extract_assets`. Expected:

- `RootWidget.Children[*].Bindings` contains the binding.
- Top-level `Bindings` is absent from extracted output.
- Re-generating from extracted JSON and extracting again preserves the same widget-level binding shape.

- [ ] **Step 7: Run UE Python binding diagnostics**

Use MCP `execute_python` with:

```python
exec(open(r"E:/GameDev/PluginsWarehouse/Plugins/UECopilot/docs/superpowers/verification/widget_binding_roundtrip_check.py", "r", encoding="utf-8").read())
```

Expected: prints JSON with no `errors`; property fixture `Text` binding has non-empty `sourcePath`.

- [ ] **Step 8: Checkpoint**

Run:

```powershell
git diff --check
git status --short
```

Suggested commit message if the user explicitly asks for commits:

```text
fix(widget): extract bindings in canonical widget format
```

## Task 6: Update MCP WidgetBlueprint Schema

**Files:**
- Modify: `MCP/schemas/WidgetBlueprint.md`

- [ ] **Step 1: Add top-level compatibility field**

In the top-level field table, add:

```markdown
| `Bindings` | object | No | | Compatibility input for extracted or legacy top-level bindings keyed by `"WidgetName.PropertyName"`. Prefer widget-level `Bindings` for new authoring. Extract now emits widget-level bindings. |
```

- [ ] **Step 2: Replace the existing Bindings Format section**

Replace `## Bindings Format` through the current binding bullets with:

````markdown
## Bindings Format

Widget-level `Bindings` is the canonical authoring and extract format. Place it on the widget node that owns the target property:

```json
{
  "Type": "TextBlock",
  "Name": "ScoreText",
  "Bindings": {
    "Text": "GetDisplayText",
    "Visibility": {
      "Kind": "Function",
      "Function": "GetTextVisibility"
    },
    "ColorAndOpacity": {
      "Kind": "Property",
      "Property": "TextColor"
    }
  }
}
```

Supported binding value forms:

- Simple function binding: `"PropertyName": "FunctionName"`
- Full function binding: `"PropertyName": { "Kind": "Function", "Function": "FunctionName" }`
- Property binding shorthand: `"PropertyName": { "Kind": "Property", "Property": "SourceProperty" }`
- Property binding path: `"PropertyName": { "Kind": "Property", "SourcePath": ["ViewModel", "DisplayText"] }`

Compatibility input is also accepted at the asset config top level:

```json
{
  "Bindings": {
    "ScoreText.Text": {
      "Kind": "Function",
      "Function": "GetDisplayText"
    }
  }
}
```

Top-level binding keys use `"WidgetName.PropertyName"`. This form is accepted for compatibility, but extraction emits widget-level `Bindings`.

Binding validation:

- The target widget is auto-exposed as `IsVariable: true`.
- The target property must have a reflected bindable delegate, usually `PropertyNameDelegate`.
- Function bindings must point to an existing function.
- Function signatures must match the target delegate or be supported by UMG's property binding adapter.
- Property delegate function bindings must be `const` or `BlueprintPure`.
- Property bindings are resolved through UE reflection and stored with `SourcePath`; single-segment paths may be displayed as `Property`.
- Invalid bindings fail generation with a specific error instead of being saved as broken bindings.
````

- [ ] **Step 3: Ensure WidgetUpdates references canonical format**

Under `WidgetUpdates`, keep `Bindings` listed as an update field and add this sentence near that section:

```markdown
`WidgetUpdates[].Bindings` uses the same widget-level binding value format shown in Bindings Format.
```

- [ ] **Step 4: Schema self-check**

Run:

```powershell
Select-String -Path MCP\schemas\WidgetBlueprint.md -Pattern "WidgetName.PropertyName|SourcePath|BlueprintPure|Compatibility input"
```

Expected: each pattern appears at least once.

- [ ] **Step 5: Checkpoint**

Run:

```powershell
git diff --check
git status --short
```

Suggested commit message if the user explicitly asks for commits:

```text
docs(widget): document binding round-trip schema
```

## Task 7: Full Verification And Review Handoff

**Files:**
- No source edits in this task unless verification exposes a defect in prior tasks.

- [ ] **Step 1: Run final UBT compile**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: exit code `0`.

- [ ] **Step 2: Start or reuse Unreal Editor**

Run if the editor is not already running:

```powershell
Start-Process -FilePath "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject"
```

Poll MCP `health_check` until it reports success.

- [ ] **Step 3: Re-run positive MCP fixtures**

Run the three `generate_assets` payloads from Task 5:

- `WBP_BindingRoundTrip_Function`
- `WBP_BindingRoundTrip_Property`
- `WBP_BindingRoundTrip_TopLevel`

Expected: all succeed.

- [ ] **Step 4: Re-run extraction round-trip**

For each positive fixture:

1. Run MCP `extract_assets`.
2. Save the returned config in the working notes.
3. Run MCP `generate_assets` with the returned config and `Action: "CreateOrUpdate"`.
4. Run MCP `extract_assets` again.

Expected:

- Extracted bindings remain under widget nodes.
- Top-level `Bindings` is not emitted.
- Function binding keeps `Kind: "Function"` and `Function`.
- Property binding keeps `Kind: "Property"` and either `Property` or equivalent `SourcePath`.

- [ ] **Step 5: Re-run binding diagnostics**

Run the external verification script while the AssetFactory HTTP server is available:

```powershell
python docs/superpowers/verification/widget_binding_roundtrip_check.py
```

Expected: JSON output contains no `errors` and verifies widget-level bindings plus extracted `BindingDiagnostics.SourcePath`/`MemberGuidValid`.

- [ ] **Step 6: Re-run negative MCP cases**

Run the three invalid payloads from Task 4.

Expected:

- Missing function returns failure containing `not found`.
- Wrong return type returns failure containing `signature` or `unsupported`.
- Non-pure property delegate returns failure containing `const` or `BlueprintPure`.

- [ ] **Step 7: Inspect git diff**

Run:

```powershell
git diff -- Source/AssetFactory/Public/Generators/WidgetBlueprintGenerator.h Source/AssetFactory/Private/Generators/WidgetBlueprintGenerator.cpp Source/AssetFactory/Public/Test/TestUserWidget.h Source/AssetFactory/Private/Test/TestUserWidget.cpp MCP/schemas/WidgetBlueprint.md docs/superpowers/verification/widget_binding_roundtrip_check.py
git diff --check
git status --short
```

Expected:

- Diff only includes planned files.
- No whitespace errors.
- Existing unrelated untracked files remain untouched.

- [ ] **Step 8: Request spec and code quality reviews**

Dispatch two read-only review subagents:

```text
Spec review: Compare implementation against docs/superpowers/specs/2026-04-27-widget-blueprint-binding-roundtrip-design.md and docs/superpowers/plans/2026-04-27-widget-blueprint-binding-roundtrip.md. Check for missing requirements, schema drift, test gaps, and behavior regressions. Do not modify files.
```

```text
Code quality review: Review WidgetBlueprint binding changes in Source/AssetFactory and MCP schema docs. Focus on UE reflection correctness, dynamic-design compliance, error propagation, compile risks, and round-trip edge cases. Do not modify files.
```

- [ ] **Step 9: Apply review fixes**

For each review finding:

1. Reproduce or inspect the cited issue.
2. Make the smallest scoped fix.
3. Re-run the narrow verification for that fix.
4. Re-run final UBT compile and MCP binding diagnostics.

- [ ] **Step 10: Final checkpoint**

Run:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors. Report the changed files and verification evidence to the user. Do not commit unless the user asks.

Suggested commit message if the user explicitly asks for commits:

```text
fix(widget): round-trip and validate UMG bindings
```

## Self-Review

- Spec coverage: Tasks 2-5 implement generator/extractor behavior; Task 6 updates schema; Task 7 covers UBT, MCP positive/negative tests, and review.
- Placeholder scan: The plan avoids open-ended placeholders and includes concrete file paths, code snippets, commands, and expected results.
- Type consistency: The plan consistently uses `FWidgetBindingSpec`, `ConfigureBindings`, `ConfigureTopLevelBindings`, `BuildFunctionBinding`, `BuildPropertyBinding`, and `BindingsByWidget`.
- User repository rules: Commit steps are replaced with checkpoints because this repo requires explicit user approval before committing.
