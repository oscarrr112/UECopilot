# BSL 事件反射重构计划

> **执行方式:** 使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务执行。步骤使用 `- [ ]` 语法追踪进度。

**目标:** 将硬编码的 `Event_BeginPlay`/`Event_Tick`/`Event_Custom` 枚举合并为基于反射的统一事件系统 `Event_Auto`/`Event_Custom`/`Event_Native`，自动通过父类反射识别原生事件。

**架构:** 枚举从 3 个事件类型变为 3 个新类型：`Event_Auto`（默认，先通过反射查父类，找到→原生事件，找不到→自定义事件）、`Event_Custom`（强制自定义事件）、`Event_Native`（强制原生事件，找不到则报错）。NodeSpawner 使用 `Blueprint->ParentClass->FindFunctionByName()` 在运行时解析事件，不再依赖硬编码映射表。BSLCompiler 只负责简写名映射（`BeginPlay`→`ReceiveBeginPlay`），然后统一设置 `Event_Auto`。

**技术栈:** UE5 C++（UHT 反射、UK2Node_Event、UK2Node_CustomEvent、UFunction）

---

## 涉及文件总览

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `Public/JSON/BlueprintJSONSchema.h` | 修改 | 枚举值替换 |
| `Private/BSL/BSLCompiler.cpp` | 修改 | CompileEvent 逻辑重写 |
| `Private/Factory/NodeSpawner.cpp` | 修改 | SpawnEventNode 改为反射解析 |
| `Private/JSON/BlueprintJSONParser.cpp` | 修改 | 类型名称映射更新 |
| `Private/BSL/BlueprintDecompiler.cpp` | 无需修改 | 反编译器直接读 EventReference，不受影响 |

所有路径相对于 `Plugins/UECopilot/Source/AssetFactoryAI/`。

---

### 任务 1：更新枚举 — BlueprintJSONSchema.h

**文件:** 修改 `Public/JSON/BlueprintJSONSchema.h:14-19`

- [ ] **步骤 1：替换事件枚举值**

将：
```cpp
// Events
Event_BeginPlay			UMETA(DisplayName = "Event BeginPlay"),
Event_Tick				UMETA(DisplayName = "Event Tick"),
Event_Custom			UMETA(DisplayName = "Custom Event"),
Event_NativeOverride	UMETA(DisplayName = "Native Override Event"),
Event_Input				UMETA(DisplayName = "Input Event"),
```

替换为：
```cpp
// Events
Event_Auto				UMETA(DisplayName = "Event Auto"),
Event_Custom			UMETA(DisplayName = "Custom Event"),
Event_Native			UMETA(DisplayName = "Native Event"),
Event_Input				UMETA(DisplayName = "Input Event"),
```

**设计说明：**
- `Event_Auto`：默认模式。NodeSpawner 通过反射在父类上查找函数，找到→`UK2Node_Event`（原生），找不到→`UK2Node_CustomEvent`（自定义）。
- `Event_Custom`：强制创建 `UK2Node_CustomEvent`，用于用户自定义事件如 `OnDoorOpened`。
- `Event_Native`：强制创建 `UK2Node_Event`，函数在父类上找不到时直接返回错误。

---

### 任务 2：更新 BSL 编译器 — 事件名解析

**文件:** 修改 `Private/BSL/BSLCompiler.cpp:224-243`

- [ ] **步骤 1：替换 CompileEvent 逻辑**

编译器已有 `ResolvedParentClass` 成员。将硬编码的 if/else 链替换为简写名映射 + 统一 `Event_Auto`。

将 224-243 行替换为：
```cpp
bool FCompiler::CompileEvent(const FFunction& Event, FBlueprintGraphData& OutGraph)
{
	// 创建事件节点
	FBlueprintNodeData EventNode;
	EventNode.NodeId = GenerateNodeId(TEXT("event"));

	// 简写名映射：BSL 简写 → UE 原生函数名
	static const TMap<FString, FString> ShorthandMap = {
		{TEXT("BeginPlay"),       TEXT("ReceiveBeginPlay")},
		{TEXT("Tick"),            TEXT("ReceiveTick")},
		{TEXT("BeginOverlap"),   TEXT("ReceiveActorBeginOverlap")},
		{TEXT("EndOverlap"),     TEXT("ReceiveActorEndOverlap")},
		{TEXT("Destroyed"),      TEXT("ReceiveDestroyed")},
		{TEXT("AnyDamage"),      TEXT("ReceiveAnyDamage")},
	};

	// 解析事件名：应用简写映射，存储供 NodeSpawner 使用
	if (const FString* MappedName = ShorthandMap.Find(Event.Name))
	{
		EventNode.EventName = *MappedName;
	}
	else
	{
		EventNode.EventName = Event.Name;
	}

	// 所有事件统一使用 Event_Auto — NodeSpawner 通过反射解析
	EventNode.NodeType = EBlueprintNodeType::Event_Auto;
```

245-254 行（Position 设置和 CompileStatements 调用）保持不变。

**设计要点：** 简写映射只是 BSL 的语法糖（写 `BeginPlay` 而非 `ReceiveBeginPlay`）。用户也可以直接写完整原生名称（如 `event ReceiveActorBeginOverlap()`）。

---

### 任务 3：更新 NodeSpawner — 基于反射的事件生成

**文件:** 修改 `Private/Factory/NodeSpawner.cpp`
- 删除 `GetActorEventNames()` 函数（123-133 行）
- 更新处理器注册（268-273 行）
- 重写 `SpawnEventNode` 函数（360-450+ 行）

- [ ] **步骤 1：删除 GetActorEventNames**

删除 123-133 行的 `GetActorEventNames()` 函数。反射取代了静态映射表。

- [ ] **步骤 2：更新处理器注册**

将 268-273 行：
```cpp
RegisterManyHandlers(Handlers, {
    EBlueprintNodeType::Event_BeginPlay,
    EBlueprintNodeType::Event_Tick,
    EBlueprintNodeType::Event_Custom,
    EBlueprintNodeType::Event_Input,
}, &UNodeSpawner::SpawnEventNode);
```

替换为：
```cpp
RegisterManyHandlers(Handlers, {
    EBlueprintNodeType::Event_Auto,
    EBlueprintNodeType::Event_Custom,
    EBlueprintNodeType::Event_Native,
    EBlueprintNodeType::Event_Input,
}, &UNodeSpawner::SpawnEventNode);
```

- [ ] **步骤 3：重写 SpawnEventNode**

用以下代码替换整个 `SpawnEventNode` 函数（360 行起）：

```cpp
FNodeSpawnResult UNodeSpawner::SpawnEventNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
    FNodeSpawnResult Result;
    UK2Node* Node = nullptr;
    FName EventFunctionName = FName(*NodeData.EventName);

    // --- 复用检查：事件已存在则直接返回 ---
    for (UEdGraphNode* ExistingNode : Graph->Nodes)
    {
        if (UK2Node_Event* ExistingEvent = Cast<UK2Node_Event>(ExistingNode))
        {
            if (ExistingEvent->EventReference.GetMemberName() == EventFunctionName)
            {
                UE_LOG(LogAssetFactoryAI, Log, TEXT("复用已有原生事件: %s"), *EventFunctionName.ToString());
                Result.bSuccess = true;
                Result.Node = ExistingEvent;
                return Result;
            }
        }
        if (UK2Node_CustomEvent* ExistingCustom = Cast<UK2Node_CustomEvent>(ExistingNode))
        {
            if (ExistingCustom->CustomFunctionName == EventFunctionName)
            {
                UE_LOG(LogAssetFactoryAI, Log, TEXT("复用已有自定义事件: %s"), *EventFunctionName.ToString());
                Result.bSuccess = true;
                Result.Node = ExistingCustom;
                return Result;
            }
        }
    }

    // --- 反射解析：在父类上查找函数 ---
    UClass* ParentClass = Blueprint->ParentClass;
    UFunction* NativeFunc = ParentClass ? ParentClass->FindFunctionByName(EventFunctionName) : nullptr;

    switch (NodeData.NodeType)
    {
    case EBlueprintNodeType::Event_Auto:
    {
        if (NativeFunc)
        {
            // 父类上找到 → 生成原生事件覆盖节点
            UK2Node_Event* EventNode = CreateNode<UK2Node_Event>(Graph);
            EventNode->EventReference.SetExternalMember(EventFunctionName, ParentClass);
            EventNode->bOverrideFunction = true;
            EventNode->AllocateDefaultPins();
            Node = EventNode;
        }
        else
        {
            // 父类上找不到 → 回退为自定义事件
            UK2Node_CustomEvent* CustomEvent = CreateNode<UK2Node_CustomEvent>(Graph);
            CustomEvent->CustomFunctionName = EventFunctionName;
            CustomEvent->AllocateDefaultPins();
            Node = CustomEvent;
        }
        break;
    }

    case EBlueprintNodeType::Event_Native:
    {
        if (!NativeFunc)
        {
            Result.ErrorMessage = FString::Printf(
                TEXT("原生事件 '%s' 在父类 '%s' 上未找到，请检查事件名或使用 Event_Auto"),
                *EventFunctionName.ToString(),
                ParentClass ? *ParentClass->GetName() : TEXT("nullptr"));
            return Result;
        }
        UK2Node_Event* EventNode = CreateNode<UK2Node_Event>(Graph);
        EventNode->EventReference.SetExternalMember(EventFunctionName, ParentClass);
        EventNode->bOverrideFunction = true;
        EventNode->AllocateDefaultPins();
        Node = EventNode;
        break;
    }

    case EBlueprintNodeType::Event_Custom:
    {
        UK2Node_CustomEvent* CustomEvent = CreateNode<UK2Node_CustomEvent>(Graph);
        CustomEvent->CustomFunctionName = EventFunctionName;
        CustomEvent->AllocateDefaultPins();
        Node = CustomEvent;
        break;
    }

    default:
        break;
    }

    if (Node)
    {
        SetNodePosition(Node, NodeData.Position);
        Result.bSuccess = true;
        Result.Node = Node;
    }
    else
    {
        Result.ErrorMessage = FString::Printf(TEXT("生成事件节点失败: %s"), *NodeData.EventName);
    }
    return Result;
}
```

**核心设计：**
- `Event_Auto`：`FindFunctionByName` 查父类。找到→`UK2Node_Event`，找不到→`UK2Node_CustomEvent`。
- `Event_Native`：同样查反射，但找不到直接报错。
- `Event_Custom`：始终 `UK2Node_CustomEvent`，不走反射。

---

### 任务 4：更新 BlueprintJSONParser — 类型名映射

**文件:** 修改 `Private/JSON/BlueprintJSONParser.cpp`

- [ ] **步骤 1：更新 InferNodeType（61-72 行）**

将：
```cpp
if (EventKey.Contains(TEXT("beginplay")) || ...)
{
    return EBlueprintNodeType::Event_BeginPlay;
}
if (EventKey.Contains(TEXT("tick")) || ...)
{
    return EBlueprintNodeType::Event_Tick;
}
return EBlueprintNodeType::Event_Custom;
```

替换为：
```cpp
return EBlueprintNodeType::Event_Auto;
```

所有事件走 `Event_Auto`，NodeSpawner 通过反射解析。

- [ ] **步骤 2：更新字符串→枚举映射（163-168 行）**

将：
```cpp
AddEntry(TEXT("Event_BeginPlay"), EBlueprintNodeType::Event_BeginPlay);
AddEntry(TEXT("BeginPlay"), EBlueprintNodeType::Event_BeginPlay);
AddEntry(TEXT("Event_Tick"), EBlueprintNodeType::Event_Tick);
AddEntry(TEXT("Tick"), EBlueprintNodeType::Event_Tick);
AddEntry(TEXT("Event_Custom"), EBlueprintNodeType::Event_Custom);
AddEntry(TEXT("CustomEvent"), EBlueprintNodeType::Event_Custom);
```

替换为：
```cpp
AddEntry(TEXT("Event_Auto"), EBlueprintNodeType::Event_Auto);
AddEntry(TEXT("Event_BeginPlay"), EBlueprintNodeType::Event_Auto);  // 向后兼容
AddEntry(TEXT("BeginPlay"), EBlueprintNodeType::Event_Auto);        // 向后兼容
AddEntry(TEXT("Event_Tick"), EBlueprintNodeType::Event_Auto);       // 向后兼容
AddEntry(TEXT("Tick"), EBlueprintNodeType::Event_Auto);             // 向后兼容
AddEntry(TEXT("Event_Custom"), EBlueprintNodeType::Event_Custom);
AddEntry(TEXT("CustomEvent"), EBlueprintNodeType::Event_Custom);
AddEntry(TEXT("Event_Native"), EBlueprintNodeType::Event_Native);
```

- [ ] **步骤 3：更新 K2Node_Event 映射（276 行）**

将：
```cpp
AddEntry(TEXT("K2Node_Event"), EBlueprintNodeType::Event_Custom);
```

替换为：
```cpp
AddEntry(TEXT("K2Node_Event"), EBlueprintNodeType::Event_Auto);
```

- [ ] **步骤 4：更新枚举→字符串映射（1257-1259 行）**

将：
```cpp
{EBlueprintNodeType::Event_BeginPlay, TEXT("BeginPlay")},
{EBlueprintNodeType::Event_Tick, TEXT("Tick")},
{EBlueprintNodeType::Event_Custom, TEXT("CustomEvent")},
```

替换为：
```cpp
{EBlueprintNodeType::Event_Auto, TEXT("Event")},
{EBlueprintNodeType::Event_Custom, TEXT("CustomEvent")},
{EBlueprintNodeType::Event_Native, TEXT("NativeEvent")},
```

---

### 任务 5：编译、测试、验证

- [ ] **步骤 1：编译插件**

关闭 UE 编辑器，运行编译。预期：0 错误。

- [ ] **步骤 2：启动编辑器测试 BSL**

使用 MCP `apply_blueprint_as_bsl` 测试以下 BSL：

```bsl
blueprint BP_DoorTest extends Actor {
  event BeginPlay() {
    PrintString("初始化完成", true, 2.0)
  }
  event ReceiveActorBeginOverlap(OtherActor: Actor) {
    PrintString("重叠开始!", true, 2.0)
  }
  event ReceiveActorEndOverlap(OtherActor: Actor) {
    PrintString("重叠结束", true, 2.0)
  }
  event Tick(DeltaSeconds: float) {
    PrintString("每帧执行", true, 2.0)
  }
  event MyCustomEvent() {
    PrintString("自定义事件!", true, 2.0)
  }
}
```

预期结果：
- `BeginPlay` → 通过简写映射→`ReceiveBeginPlay`→反射找到→原生事件节点
- `ReceiveActorBeginOverlap` → 直接反射找到→原生事件节点
- `ReceiveActorEndOverlap` → 直接反射找到→原生事件节点
- `Tick` → 通过简写映射→`ReceiveTick`→反射找到→原生事件节点
- `MyCustomEvent` → 反射找不到→回退为自定义事件节点
- **无编译器警告**（不再出现 "name conflicts with native function"）

- [ ] **步骤 3：验证反编译往返**

使用 `extract_blueprint_as_bsl` 提取测试蓝图。反编译器（BlueprintDecompiler.cpp:130）直接读取 `EventReference.GetMemberName()`，不受本次重构影响，无需修改。

- [ ] **步骤 4：提交**

```bash
git add Plugins/UECopilot/Source/AssetFactoryAI/
git commit -m "refactor: 统一基于反射的 BSL 事件系统 (Event_Auto/Custom/Native)"
```
