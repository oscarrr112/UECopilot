# BSL 扩展实现计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 完善 BSL 编译器，使其能够覆盖大部分日常蓝图开发场景。

**Architecture:** BSL 文本 → Lexer/Parser（已有）→ AST → Compiler（本计划修复）→ FBlueprintData JSON IR → NodeSpawner → UEdGraph。
Tier 1 修复现有编译器缺口（AST 已有，Compiler 未实现）；Tier 2 新增语言特性（改 AST + Parser + Compiler）；Tier 3 逃生舱；Tier 4 反向通路。

**Tech Stack:** UE 5.7 C++，UE 反射系统 (TFieldIterator<FProperty>)，K2Node 宏节点 (K2Node_MacroInstance)。

---

## 测试方法（通用）

每个任务完成后的验证步骤：
1. UBT 编译：`"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload`
2. 打开编辑器，点击 **UECopilot > BSL Test Window**
3. 在 Input 框粘贴测试 BSL，点击 Compile，检查 Output JSON 中节点类型和 pin 名是否正确
4. 或者：使用 `apply_blueprint_change` MCP 工具直接测试

---

## Task 1：函数 Pin 名称反射解析（替换 Arg0/Arg1）

**Files:**
- Modify: `Source/AssetFactoryAI/Public/BSL/BSLCompiler.h`（新增私有成员函数声明）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（新增实现，修改两处调用点）

**背景：** 当前在 `CompileExpression::FunctionCall`（约 L720-750）和 `CompileStatement::ExpressionStmt`（约 L481-512）中，所有函数参数都用 `Arg0`, `Arg1`……作为 pin 名。实际 UE 蓝图中 pin 名是函数参数名（如 `PrintString` 的参数是 `InString`，不是 `Arg0`）。

**Step 1: 在 BSLCompiler.h 末尾（L115 `IsFunctionOutputParameter` 之后）添加声明**

```cpp
/** 通过 UE 反射解析函数参数 pin 名列表，失败则返回空数组 */
static bool TryResolveParamNames(const FString& FunctionRef, TArray<FString>& OutNames);
```

**Step 2: 在 BSLCompiler.cpp 末尾（L1137 之后）添加实现**

```cpp
bool FCompiler::TryResolveParamNames(const FString& FunctionRef, TArray<FString>& OutNames)
{
    OutNames.Empty();

    // 尝试直接路径查找（e.g. "/Script/Engine.KismetSystemLibrary:PrintString"）
    UFunction* Func = FindObject<UFunction>(ANY_PACKAGE, *FunctionRef);

    // 如果直接查找失败，遍历常见库类
    if (!Func)
    {
        TArray<UClass*> CandidateClasses = {
            FindObject<UClass>(ANY_PACKAGE, TEXT("/Script/Engine.KismetSystemLibrary")),
            FindObject<UClass>(ANY_PACKAGE, TEXT("/Script/Engine.KismetMathLibrary")),
            FindObject<UClass>(ANY_PACKAGE, TEXT("/Script/Engine.GameplayStatics")),
        };
        for (UClass* Cls : CandidateClasses)
        {
            if (Cls)
            {
                Func = Cls->FindFunctionByName(*FunctionRef);
                if (Func) break;
            }
        }
    }

    if (!Func) return false;

    // 遍历参数（跳过 ReturnParm 和 OutParm，只取输入参数）
    for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
    {
        if (!(It->PropertyFlags & CPF_ReturnParm) && !(It->PropertyFlags & CPF_OutParm))
        {
            OutNames.Add(It->GetName());
        }
    }
    return OutNames.Num() > 0;
}
```

**Step 3: 修改 `CompileStatement::ExpressionStmt` 中的参数 pin 名（L480-512）**

将 L481-512 整个 `for (const TSharedPtr<FExpression>& Arg : Stmt.Expression->Arguments)` 循环替换为：

```cpp
// 尝试用反射解析 pin 名
TArray<FString> ParamNames;
bool bHasReflectedNames = TryResolveParamNames(Stmt.Expression->Name, ParamNames);

int32 ArgIndex = 0;
for (const TSharedPtr<FExpression>& Arg : Stmt.Expression->Arguments)
{
    if (!Arg) continue;

    FString PinName = (bHasReflectedNames && ParamNames.IsValidIndex(ArgIndex))
        ? ParamNames[ArgIndex]
        : FString::Printf(TEXT("Arg%d"), ArgIndex);

    FString ArgNodeId;
    FString ArgPinName = CompileExpression(*Arg, OutNodes, ArgNodeId);

    FBlueprintPinData ArgPin;
    ArgPin.Name = PinName;
    ArgPin.Direction = EBlueprintPinDirection::Input;

    if (!ArgNodeId.IsEmpty())
    {
        FBlueprintPinConnection ArgConn;
        ArgConn.SourceNodeId = ArgNodeId;
        ArgConn.SourcePinName = ArgPinName;
        ArgPin.Connections.Add(ArgConn);
    }
    else if (Arg->Type == EExpressionType::Literal_String)
    {
        ArgPin.DefaultValue = Arg->StringValue;
    }
    else if (Arg->Type == EExpressionType::Literal_Int)
    {
        ArgPin.DefaultValue = FString::FromInt(Arg->IntValue);
    }
    else if (Arg->Type == EExpressionType::Literal_Float)
    {
        ArgPin.DefaultValue = FString::SanitizeFloat(Arg->FloatValue);
    }
    else if (Arg->Type == EExpressionType::Literal_Bool)
    {
        ArgPin.DefaultValue = Arg->BoolValue ? TEXT("true") : TEXT("false");
    }

    CallNode.Pins.Add(ArgPin);
    ArgIndex++;
}
```

**Step 4: 修改 `CompileExpression::FunctionCall` 中的参数 pin 名（L720-750）**

同样将 L720-750 的 ArgIndex 循环替换为使用 `TryResolveParamNames` 的版本（逻辑同上）。

**Step 5: 编译并验证**

测试 BSL：
```bsl
blueprint BP_Test extends Actor {
  event BeginPlay {
    PrintString("Hello World")
  }
}
```
预期：Output JSON 中 CallFunction 节点的参数 pin 名为 `InString`，而非 `Arg0`。

**Step 6: Commit**
```bash
cd "E:/GameDev/PluginsWarehouse/Plugins/UECopilot"
git add Source/AssetFactoryAI/Public/BSL/BSLCompiler.h Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): use UE reflection to resolve function parameter pin names"
```

---

## Task 2：While 循环编译

**Files:**
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（在 `CompileStatement` 的 `default` case 之前新增 `case EStatementType::While:`）

**背景：** While 循环 AST 节点已有（`Stmt.Condition` + `Stmt.LoopBody`），NodeSpawner 已支持 `Flow_WhileLoop`，但 Compiler `CompileStatement` switch（L643 default）未处理。

UE 蓝图中 While 循环宏节点的 pin 名：
- 输入 exec: `execute`
- 输入条件：`Condition`
- 输出（循环体）：`LoopBody`
- 输出（完成）：`Completed`

**Step 1: 在 L583 (`case EStatementType::Return:` 之前) 或 L641 (`default:` 之前) 添加**

```cpp
case EStatementType::While:
{
    // 创建 WhileLoop 宏节点
    FBlueprintNodeData WhileNode;
    WhileNode.NodeId = GenerateNodeId(TEXT("while"));
    WhileNode.NodeType = EBlueprintNodeType::Flow_WhileLoop;
    WhileNode.Position = {400.0f, 0.0f};

    // 连接 execute
    if (!InOutLastExecNodeId.IsEmpty())
    {
        FBlueprintPinData ExecPin;
        ExecPin.Name = TEXT("execute");
        ExecPin.Direction = EBlueprintPinDirection::Input;
        FBlueprintPinConnection ExecConn;
        ExecConn.SourceNodeId = InOutLastExecNodeId;
        ExecConn.SourcePinName = InOutLastExecPinName;
        ExecPin.Connections.Add(ExecConn);
        WhileNode.Pins.Add(ExecPin);
    }

    // 编译条件
    if (Stmt.Condition.IsValid())
    {
        FString CondNodeId;
        FString CondPinName = CompileExpression(*Stmt.Condition, OutNodes, CondNodeId);
        if (!CondNodeId.IsEmpty())
        {
            FBlueprintPinData CondPin;
            CondPin.Name = TEXT("Condition");
            CondPin.Direction = EBlueprintPinDirection::Input;
            FBlueprintPinConnection CondConn;
            CondConn.SourceNodeId = CondNodeId;
            CondConn.SourcePinName = CondPinName;
            CondPin.Connections.Add(CondConn);
            WhileNode.Pins.Add(CondPin);
        }
    }

    OutNodes.Add(WhileNode);

    // 编译循环体（从 LoopBody 出 exec 开始）
    FString BodyLastNodeId = WhileNode.NodeId;
    FString BodyLastPinName = TEXT("LoopBody");
    CompileStatements(Stmt.LoopBody, OutNodes, BodyLastNodeId, BodyLastPinName);

    // While 之后的语句接在 Completed 上
    InOutLastExecNodeId = WhileNode.NodeId;
    InOutLastExecPinName = TEXT("Completed");
    return true;
}
```

**Step 2: 测试**

```bsl
blueprint BP_WhileTest extends Actor {
  var Counter: int = 0
  event BeginPlay {
    while (Counter < 10) {
      Counter = Counter + 1
    }
  }
}
```
预期：JSON IR 包含 `Flow_WhileLoop` 节点，有 `Condition` pin 连接到 `<` 比较节点，`LoopBody` 连接后续 Set 节点。

**Step 3: Commit**
```bash
git add Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): implement while loop compilation"
```

---

## Task 3：For 循环编译

**Files:**
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`

**背景：** `for i in 0..10 {}` — AST 字段：`Stmt.LoopVariable`（循环变量名）、`Stmt.LoopStart`、`Stmt.LoopEnd`、`Stmt.LoopBody`。NodeSpawner 已支持 `Flow_ForLoop`。

UE ForLoop 宏 pin 名：
- 输入 exec: `execute`
- 输入：`FirstIndex`（int）、`LastIndex`（int）
- 输出 exec（每轮）：`LoopBody`
- 输出 exec（完成）：`Completed`
- 输出数据：`Index`（当前循环变量）

**Step 1: 在 While case 后紧接着添加**

```cpp
case EStatementType::For:
{
    FBlueprintNodeData ForNode;
    ForNode.NodeId = GenerateNodeId(TEXT("for"));
    ForNode.NodeType = EBlueprintNodeType::Flow_ForLoop;
    ForNode.Position = {400.0f, 0.0f};

    // 连接 execute
    if (!InOutLastExecNodeId.IsEmpty())
    {
        FBlueprintPinData ExecPin;
        ExecPin.Name = TEXT("execute");
        ExecPin.Direction = EBlueprintPinDirection::Input;
        FBlueprintPinConnection Conn;
        Conn.SourceNodeId = InOutLastExecNodeId;
        Conn.SourcePinName = InOutLastExecPinName;
        ExecPin.Connections.Add(Conn);
        ForNode.Pins.Add(ExecPin);
    }

    // 编译 FirstIndex
    if (Stmt.LoopStart.IsValid())
    {
        FString StartNodeId;
        FString StartPinName = CompileExpression(*Stmt.LoopStart, OutNodes, StartNodeId);
        FBlueprintPinData FirstPin;
        FirstPin.Name = TEXT("FirstIndex");
        FirstPin.Direction = EBlueprintPinDirection::Input;
        if (!StartNodeId.IsEmpty())
        {
            FBlueprintPinConnection Conn;
            Conn.SourceNodeId = StartNodeId;
            Conn.SourcePinName = StartPinName;
            FirstPin.Connections.Add(Conn);
        }
        else if (Stmt.LoopStart->Type == EExpressionType::Literal_Int)
        {
            FirstPin.DefaultValue = FString::FromInt(Stmt.LoopStart->IntValue);
        }
        ForNode.Pins.Add(FirstPin);
    }

    // 编译 LastIndex
    if (Stmt.LoopEnd.IsValid())
    {
        FString EndNodeId;
        FString EndPinName = CompileExpression(*Stmt.LoopEnd, OutNodes, EndNodeId);
        FBlueprintPinData LastPin;
        LastPin.Name = TEXT("LastIndex");
        LastPin.Direction = EBlueprintPinDirection::Input;
        if (!EndNodeId.IsEmpty())
        {
            FBlueprintPinConnection Conn;
            Conn.SourceNodeId = EndNodeId;
            Conn.SourcePinName = EndPinName;
            LastPin.Connections.Add(Conn);
        }
        else if (Stmt.LoopEnd->Type == EExpressionType::Literal_Int)
        {
            LastPin.DefaultValue = FString::FromInt(Stmt.LoopEnd->IntValue);
        }
        ForNode.Pins.Add(LastPin);
    }

    OutNodes.Add(ForNode);

    // 将循环变量 i 映射到 ForNode 的 Index 输出 pin
    VariableNodeMap.Add(Stmt.LoopVariable, ForNode.NodeId);

    // 编译循环体
    FString BodyLastNodeId = ForNode.NodeId;
    FString BodyLastPinName = TEXT("LoopBody");
    CompileStatements(Stmt.LoopBody, OutNodes, BodyLastNodeId, BodyLastPinName);

    InOutLastExecNodeId = ForNode.NodeId;
    InOutLastExecPinName = TEXT("Completed");
    return true;
}
```

**Step 2: 测试**

```bsl
blueprint BP_ForTest extends Actor {
  event BeginPlay {
    for i in 0..5 {
      PrintString("Iter")
    }
  }
}
```
预期：JSON IR 包含 `Flow_ForLoop` 节点，`FirstIndex=0`，`LastIndex=5`。

**Step 3: Commit**
```bash
git add Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): implement for loop compilation"
```

---

## Task 4：ForEach 循环编译

**Files:**
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`

**背景：** `foreach item in Enemies {}` — AST：`Stmt.LoopVariable`（元素变量名）、`Stmt.LoopCollection`（数组表达式）、`Stmt.LoopBody`。NodeSpawner 已支持 `Flow_ForEachLoop`。

UE ForEachLoop 宏 pin 名：
- 输入 exec: `execute`
- 输入：`Array`（数组）
- 输出 exec（每轮）：`LoopBody`
- 输出 exec（完成）：`Completed`
- 输出数据：`ArrayElement`（当前元素）、`ArrayIndex`（当前索引）

**Step 1: 在 For case 后添加**

```cpp
case EStatementType::ForEach:
{
    FBlueprintNodeData ForEachNode;
    ForEachNode.NodeId = GenerateNodeId(TEXT("foreach"));
    ForEachNode.NodeType = EBlueprintNodeType::Flow_ForEachLoop;
    ForEachNode.Position = {400.0f, 0.0f};

    // 连接 execute
    if (!InOutLastExecNodeId.IsEmpty())
    {
        FBlueprintPinData ExecPin;
        ExecPin.Name = TEXT("execute");
        ExecPin.Direction = EBlueprintPinDirection::Input;
        FBlueprintPinConnection Conn;
        Conn.SourceNodeId = InOutLastExecNodeId;
        Conn.SourcePinName = InOutLastExecPinName;
        ExecPin.Connections.Add(Conn);
        ForEachNode.Pins.Add(ExecPin);
    }

    // 编译集合表达式，连接到 Array pin
    if (Stmt.LoopCollection.IsValid())
    {
        FString CollNodeId;
        FString CollPinName = CompileExpression(*Stmt.LoopCollection, OutNodes, CollNodeId);
        if (!CollNodeId.IsEmpty())
        {
            FBlueprintPinData ArrPin;
            ArrPin.Name = TEXT("Array");
            ArrPin.Direction = EBlueprintPinDirection::Input;
            FBlueprintPinConnection Conn;
            Conn.SourceNodeId = CollNodeId;
            Conn.SourcePinName = CollPinName;
            ArrPin.Connections.Add(Conn);
            ForEachNode.Pins.Add(ArrPin);
        }
    }

    OutNodes.Add(ForEachNode);

    // 将循环变量映射到 ForEachNode 的 ArrayElement 输出 pin
    VariableNodeMap.Add(Stmt.LoopVariable, ForEachNode.NodeId);

    // 编译循环体（在循环体中引用 Stmt.LoopVariable 时，从 ForEachNode.ArrayElement 获取）
    // 注意：VariableNodeMap 存的是 NodeId，pin 名需额外处理
    // 这里使用一个约定：当 Variable 的 Name 与 LoopVariable 匹配且 NodeId == ForEachNode.NodeId 时，
    // CompileExpression::Variable 应使用 "ArrayElement" 作为 pin 名。
    // 实现方式：添加 LoopVarPinMap 字典（Task 5 一并扩展）。
    // 目前先存入 VariableNodeMap（pin 名由 CompileExpression::Variable 查到 fn_entry 时改用变量名自身，
    // 对于非 fn_entry 节点，默认 pin 名即变量名——这里需要单独处理）。

    FString BodyLastNodeId = ForEachNode.NodeId;
    FString BodyLastPinName = TEXT("LoopBody");
    CompileStatements(Stmt.LoopBody, OutNodes, BodyLastNodeId, BodyLastPinName);

    InOutLastExecNodeId = ForEachNode.NodeId;
    InOutLastExecPinName = TEXT("Completed");
    return true;
}
```

**注意：** 循环变量的输出 pin 名是 `ArrayElement`，但 `VariableNodeMap` 目前只存 NodeId、不存 pin 名。需要在 Task 5（局部变量）中同步扩展为 `VariableNodeMap`→`TPair<NodeId, PinName>`（或新增一个 `LoopVarPinMap`）。

**Step 2: 测试**

```bsl
blueprint BP_ForEachTest extends Actor {
  var Enemies: Array<Actor>
  event BeginPlay {
    foreach enemy in Enemies {
      PrintString("found")
    }
  }
}
```

**Step 3: Commit**
```bash
git add Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): implement foreach loop compilation"
```

---

## Task 5：局部变量声明 + 循环变量 Pin 名修复

**Files:**
- Modify: `Source/AssetFactoryAI/Public/BSL/BSLCompiler.h`（扩展 VariableNodeMap 存 pin 名）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（多处修改）

**背景：** 局部变量 `var x: int = 0`（L326-333）目前只打 Warning。需要：
1. 将局部变量注册到一个 map（name → 初始值节点）
2. 在 `CompileExpression::Variable` 中能正确找到局部变量的值

同时，Task 4 引入了循环变量 pin 名的问题，一并修复。

**Step 1: 修改 BSLCompiler.h — 将 VariableNodeMap 改为存 TPair<NodeId, PinName>**

将 L109 的 `TMap<FString, FString> VariableNodeMap;` 改为：
```cpp
// 变量名 → (提供值的节点ID, 输出 pin 名)
TMap<FString, TPair<FString, FString>> VariableNodeMap;
```

**Step 2: 更新所有 VariableNodeMap 的使用点**

全局搜索 `VariableNodeMap` 并修改：
- 写入点（原 `VariableNodeMap.Add(Name, NodeId)` → `VariableNodeMap.Add(Name, {NodeId, PinName})`）
- 读取点（原 `if (FString* MappedNodeId = VariableNodeMap.Find(Expr.Name))` → 用 `TPair` 读 Key/Value）

具体修改位置：
- `CompileFunction`：L185 `VariableNodeMap.Add(Input.Name, TEXT("fn_entry"))` → `VariableNodeMap.Add(Input.Name, {TEXT("fn_entry"), Input.Name})`
- `CompileExpression::Variable`（L686-693）：
  ```cpp
  if (TPair<FString, FString>* Mapped = VariableNodeMap.Find(Expr.Name))
  {
      OutNodeId = Mapped->Key;
      return Mapped->Value;
  }
  ```
- `CompileStatement::For`（Task 3 新增）：`VariableNodeMap.Add(Stmt.LoopVariable, {ForNode.NodeId, TEXT("Index")})`
- `CompileStatement::ForEach`（Task 4 新增）：`VariableNodeMap.Add(Stmt.LoopVariable, {ForEachNode.NodeId, TEXT("ArrayElement")})`

**Step 3: 实现局部变量 case（替换 L326-333 的 Warning）**

```cpp
case EStatementType::VariableDecl:
{
    // 如果有初始值，编译它并注册到 VariableNodeMap（后续引用此变量时使用该值）
    if (Stmt.DeclaredVariable.DefaultValue.IsValid())
    {
        FString ValNodeId;
        FString ValPinName = CompileExpression(*Stmt.DeclaredVariable.DefaultValue, OutNodes, ValNodeId);

        if (!ValNodeId.IsEmpty())
        {
            // 将变量名映射到提供初始值的节点
            VariableNodeMap.Add(Stmt.DeclaredVariable.Name, {ValNodeId, ValPinName});
        }
        else
        {
            // 字面量初始值：为了让后续引用能拿到值，创建一个 Variable_Get 节点（如果是蓝图变量）
            // 或直接在 VariableNodeMap 中存一个 "literal" 标记
            // 简单起见：如果是局部变量初始赋值为字面量，暂时只记录警告
            Warning(FString::Printf(TEXT("Local var '%s' initialized with literal - use blueprint variable for persistence"), *Stmt.DeclaredVariable.Name));
        }
    }
    return true;
}
```

**Step 4: 测试**

```bsl
blueprint BP_LocalVar extends Actor {
  event BeginPlay {
    var damage: int = 50
    var health: int = 100
    var result: int = health - damage
    PrintString("OK")
  }
}
```

**Step 5: Commit**
```bash
git add Source/AssetFactoryAI/Public/BSL/BSLCompiler.h Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): implement local variable declarations and fix loop variable pin names"
```

---

## Task 6：Cast 表达式编译

**Files:**
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（在 `CompileExpression` switch 的 `default` 前添加 `case EExpressionType::Cast:`）

**背景：** `Cast<PlayerCharacter>(actor)` — AST：`Expr.CastType`（目标类型）、`Expr.Left`（被 cast 的对象）。NodeSpawner 已支持 `EBlueprintNodeType::Cast`。

UE Cast 节点 pin 名：
- 输入：`Object`（被 cast 对象）
- 输出 exec（成功）：`then`（或 `CastSucceeded`）
- 输出 exec（失败）：`CastFailed`
- 输出数据：`As<ClassName>`（e.g. `AsPlayerCharacter`）

**Step 1: 在 `CompileExpression` 的 `default:` 之前添加**

```cpp
case EExpressionType::Cast:
{
    FBlueprintNodeData CastNode;
    CastNode.NodeId = GenerateNodeId(TEXT("cast"));
    CastNode.NodeType = EBlueprintNodeType::Cast;
    CastNode.CastClass = Expr.CastType.SubType;  // e.g. "PlayerCharacter"
    CastNode.Position = {300.0f, 100.0f};

    // 编译被 cast 的对象
    if (Expr.Left.IsValid())
    {
        FString ObjNodeId;
        FString ObjPinName = CompileExpression(*Expr.Left, OutNodes, ObjNodeId);
        if (!ObjNodeId.IsEmpty())
        {
            FBlueprintPinData ObjPin;
            ObjPin.Name = TEXT("Object");
            ObjPin.Direction = EBlueprintPinDirection::Input;
            FBlueprintPinConnection Conn;
            Conn.SourceNodeId = ObjNodeId;
            Conn.SourcePinName = ObjPinName;
            ObjPin.Connections.Add(Conn);
            CastNode.Pins.Add(ObjPin);
        }
    }

    OutNodes.Add(CastNode);
    OutNodeId = CastNode.NodeId;
    // Cast 节点的成功输出 pin 名为 "As<ClassName>"
    return FString::Printf(TEXT("As%s"), *Expr.CastType.SubType);
}
```

**注意：** `FBlueprintNodeData` 中可能没有 `CastClass` 字段，需确认 `BlueprintJSONSchema.h` 中的结构。如果没有，可以用 `NodeData.VariableName` 临时存放（或添加专用字段）。

**Step 2: 确认 BlueprintJSONSchema.h 中 FBlueprintNodeData 是否有 CastClass 字段**

查阅 `Source/AssetFactoryAI/Public/JSON/BlueprintJSONSchema.h`，找到 `FBlueprintNodeData` 结构体定义，确认有无专用字段。如没有，使用 `VariableName` 存放 cast 目标类名（NodeSpawner 应已读取此字段）。

**Step 3: 测试**

```bsl
blueprint BP_CastTest extends Actor {
  event BeginPlay {
    var actor: Actor
    var pc: PlayerCharacter = Cast<PlayerCharacter>(actor)
  }
}
```

**Step 4: Commit**
```bash
git add Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): implement Cast expression compilation"
```

---

## Task 7：Self 表达式编译

**Files:**
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（在 `CompileExpression` switch 中添加 `case EExpressionType::Self:`）

**背景：** `self` 关键字 — 在 Blueprint 中对应 `K2Node_Self`，输出 pin 名为 `self`。

**Step 1: 在 Cast case 之前添加**

```cpp
case EExpressionType::Self:
{
    FBlueprintNodeData SelfNode;
    SelfNode.NodeId = GenerateNodeId(TEXT("self"));
    SelfNode.NodeType = EBlueprintNodeType::Self;
    SelfNode.Position = {200.0f, 100.0f};

    OutNodes.Add(SelfNode);
    OutNodeId = SelfNode.NodeId;
    return TEXT("self");
}
```

**注意：** 需确认 `BlueprintJSONSchema.h` 中是否有 `EBlueprintNodeType::Self`。如果没有，可以用 `PureFunction` 加 `FunctionReference = "Self"` 代替，NodeSpawner 会映射到 K2Node_Self。

**Step 2: 测试**

```bsl
blueprint BP_SelfTest extends Actor {
  event BeginPlay {
    var me: Actor = self
  }
}
```

**Step 3: Commit**
```bash
git add Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): implement Self expression compilation"
```

---

## Task 8：多返回值赋值

**Files:**
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（在 `CompileStatement` switch 中添加 `case EStatementType::MultiAssignment:`）

**背景：** `(a, b) = GetMinMax(arr)` — AST：`Stmt.MultiAssignTargets`（变量名列表）、`Stmt.AssignValue`（函数调用表达式）。

**Step 1: 在 `Assignment` case 之后添加**

```cpp
case EStatementType::MultiAssignment:
{
    // 先编译右侧的函数调用
    if (!Stmt.AssignValue.IsValid()) return true;

    // 将函数调用作为非 pure 的语句节点编译（需要有 exec pin 才能多返回）
    // 如果是函数调用，创建 CallFunction 节点
    if (Stmt.AssignValue->Type == EExpressionType::FunctionCall)
    {
        FBlueprintNodeData CallNode;
        CallNode.NodeId = GenerateNodeId(TEXT("call_multi"));
        CallNode.NodeType = EBlueprintNodeType::CallFunction;
        CallNode.FunctionReference = Stmt.AssignValue->Name;
        CallNode.Position = {400.0f, 0.0f};

        // 连接 execute
        if (!InOutLastExecNodeId.IsEmpty())
        {
            FBlueprintPinData ExecPin;
            ExecPin.Name = TEXT("execute");
            ExecPin.Direction = EBlueprintPinDirection::Input;
            FBlueprintPinConnection Conn;
            Conn.SourceNodeId = InOutLastExecNodeId;
            Conn.SourcePinName = InOutLastExecPinName;
            ExecPin.Connections.Add(Conn);
            CallNode.Pins.Add(ExecPin);
        }

        // 编译函数参数（用反射 pin 名）
        TArray<FString> ParamNames;
        bool bHasNames = TryResolveParamNames(Stmt.AssignValue->Name, ParamNames);
        int32 ArgIndex = 0;
        for (const TSharedPtr<FExpression>& Arg : Stmt.AssignValue->Arguments)
        {
            if (!Arg) continue;
            FString PinName = (bHasNames && ParamNames.IsValidIndex(ArgIndex))
                ? ParamNames[ArgIndex] : FString::Printf(TEXT("Arg%d"), ArgIndex);
            FString ArgNodeId;
            FString ArgPinName = CompileExpression(*Arg, OutNodes, ArgNodeId);
            FBlueprintPinData ArgPin;
            ArgPin.Name = PinName;
            ArgPin.Direction = EBlueprintPinDirection::Input;
            if (!ArgNodeId.IsEmpty())
            {
                FBlueprintPinConnection Conn;
                Conn.SourceNodeId = ArgNodeId;
                Conn.SourcePinName = ArgPinName;
                ArgPin.Connections.Add(Conn);
            }
            CallNode.Pins.Add(ArgPin);
            ArgIndex++;
        }

        OutNodes.Add(CallNode);

        // 将每个赋值目标映射到 CallNode 的对应输出 pin
        // UE 函数多返回值的 pin 名通常与参数名相同（out param）
        // 通过反射解析 out param 名
        TArray<FString> OutParamNames;
        if (UFunction* Func = FindObject<UFunction>(ANY_PACKAGE, *Stmt.AssignValue->Name))
        {
            for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
            {
                if ((It->PropertyFlags & CPF_OutParm) && !(It->PropertyFlags & CPF_ReturnParm))
                {
                    OutParamNames.Add(It->GetName());
                }
            }
        }

        for (int32 i = 0; i < Stmt.MultiAssignTargets.Num(); i++)
        {
            FString TargetName = Stmt.MultiAssignTargets[i];
            FString OutPinName = OutParamNames.IsValidIndex(i) ? OutParamNames[i] : FString::Printf(TEXT("Out%d"), i);
            VariableNodeMap.Add(TargetName, {CallNode.NodeId, OutPinName});
        }

        InOutLastExecNodeId = CallNode.NodeId;
        InOutLastExecPinName = TEXT("then");
    }
    return true;
}
```

**Step 2: 测试**

```bsl
blueprint BP_MultiReturn extends Actor {
  event BeginPlay {
    var min: float
    var max: float
    (min, max) = GetMinMax(10.0)
    PrintString("OK")
  }
}
```

**Step 3: Commit**
```bash
git add Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): implement multi-return value assignment"
```

---

## Task 9：取反运算（UnaryOp::Negate）

**Files:**
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（修改 `CompileExpression::UnaryOp` case，L901-933）

**背景：** `-x` 当前只有 `break`（L932），没有实现。在 Blueprint 中取反通过 `Multiply(x, -1)` 实现。

**Step 1: 将 L931-932 (`// Negate would need...` / `break;`) 替换为**

```cpp
else  // EUnaryOp::Negate
{
    // Blueprint 中没有专用的取反节点，用 Multiply * (-1) 实现
    FBlueprintNodeData MulNode;
    MulNode.NodeId = GenerateNodeId(TEXT("negate"));
    MulNode.NodeType = EBlueprintNodeType::Math_Multiply;
    MulNode.OperandType = TEXT("DoubleDouble");
    MulNode.Position = {300.0f, 50.0f};

    // A pin = operand
    if (Expr.Left.IsValid())
    {
        FString OpNodeId;
        FString OpPinName = CompileExpression(*Expr.Left, OutNodes, OpNodeId);
        FBlueprintPinData APin;
        APin.Name = TEXT("A");
        APin.Direction = EBlueprintPinDirection::Input;
        if (!OpNodeId.IsEmpty())
        {
            FBlueprintPinConnection Conn;
            Conn.SourceNodeId = OpNodeId;
            Conn.SourcePinName = OpPinName;
            APin.Connections.Add(Conn);
        }
        else if (Expr.Left->Type == EExpressionType::Literal_Int)
        {
            APin.DefaultValue = FString::SanitizeFloat(static_cast<double>(Expr.Left->IntValue));
        }
        else if (Expr.Left->Type == EExpressionType::Literal_Float)
        {
            APin.DefaultValue = FString::SanitizeFloat(Expr.Left->FloatValue);
        }
        MulNode.Pins.Add(APin);
    }

    // B pin = -1
    FBlueprintPinData BPin;
    BPin.Name = TEXT("B");
    BPin.Direction = EBlueprintPinDirection::Input;
    BPin.DefaultValue = TEXT("-1.0");
    MulNode.Pins.Add(BPin);

    OutNodes.Add(MulNode);
    OutNodeId = MulNode.NodeId;
    return TEXT("ReturnValue");
}
```

**Step 2: 测试**

```bsl
blueprint BP_NegateTest extends Actor {
  var Speed: float = 100.0
  event BeginPlay {
    var neg: float = -Speed
  }
}
```
预期：JSON IR 包含 `Math_Multiply` 节点，B pin DefaultValue = "-1.0"。

**Step 3: Commit**
```bash
git add Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): implement unary negate via Multiply * -1"
```

---

## Task 10：方法调用 Parser 修复（支持 self.Func(args) 语法）

**Files:**
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLParser.cpp`（修改 `ParsePostfix` 中 call 分支，约 L643-652）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（修改 `CompileStatement::ExpressionStmt` 和 `CompileExpression::FunctionCall`，处理 `Expr.Object`）

**背景：** 当前 `ParsePostfix` 中当 `Expr` 是 MemberAccess 后遇到 `(args)` 时，会创建一个 `FunctionCall`，但 `Call->Name = Expr->Name`（对 MemberAccess 来说是空字符串）。正确行为：`Call->Name = Expr->MemberName`，`Call->Object = Expr->Object`。

**Step 1: 修改 ParseParser.cpp `ParsePostfix` 中 call 分支（L643-652）**

将：
```cpp
TSharedPtr<FExpression> Call = MakeShared<FExpression>(EExpressionType::FunctionCall);
Call->Name = Expr->Name;
Call->Arguments = Args;
Expr = Call;
```
改为：
```cpp
TSharedPtr<FExpression> Call = MakeShared<FExpression>(EExpressionType::FunctionCall);
if (Expr->Type == EExpressionType::MemberAccess)
{
    // 方法调用：obj.method(args)
    Call->Name = Expr->MemberName;   // "SetLocation"
    Call->Object = Expr->Object;     // 目标对象（如 self.Mesh 表达式）
}
else
{
    // 普通函数调用：FunctionName(args)
    Call->Name = Expr->Name;
}
Call->Arguments = Args;
Expr = Call;
```

**Step 2: 修改 Compiler 处理带 Object 的 FunctionCall**

在 `CompileStatement::ExpressionStmt`（以及 `CompileExpression::FunctionCall`）中，在创建 `CallNode` 后、编译参数之前，先检查 `Stmt.Expression->Object`：

```cpp
// 如果有 Target 对象（方法调用），编译 Object 并加入 Target pin
if (Stmt.Expression->Object.IsValid())
{
    FString TargetNodeId;
    FString TargetPinName = CompileExpression(*Stmt.Expression->Object, OutNodes, TargetNodeId);
    if (!TargetNodeId.IsEmpty())
    {
        FBlueprintPinData TargetPin;
        TargetPin.Name = TEXT("self");  // UE 方法调用的 Target pin 统一叫 "self"
        TargetPin.Direction = EBlueprintPinDirection::Input;
        FBlueprintPinConnection Conn;
        Conn.SourceNodeId = TargetNodeId;
        Conn.SourcePinName = TargetPinName;
        TargetPin.Connections.Add(Conn);
        CallNode.Pins.Add(TargetPin);
    }
}
```

**Step 3: 测试**

```bsl
blueprint BP_MethodTest extends Actor {
  event BeginPlay {
    self.SetActorHiddenInGame(true)
    self.DestroyActor()
  }
}
```
预期：`self` 表达式生成 Self 节点，SetActorHiddenInGame 调用节点有 Target 连接到 Self。

**Step 4: Commit**
```bash
git add Source/AssetFactoryAI/Private/BSL/BSLParser.cpp Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): fix method call parsing - preserve target object in FunctionCall"
```

---

## Task 11：Switch/case 语句（新增 AST + Parser + Compiler）

**Files:**
- Modify: `Source/AssetFactoryAI/Public/BSL/BSLTypes.h`（新增 FSwitchCase、扩展 EStatementType、扩展 FStatement）
- Modify: `Source/AssetFactoryAI/Public/BSL/BSLLexer.h`（新增 Switch/Case/Default token）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLLexer.cpp`（注册关键字）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLParser.cpp`（实现 ParseSwitch）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（实现 Switch 编译）

**Step 1: 修改 BSLTypes.h — 新增 AST 类型**

在 `EStatementType` 末尾（L218 `Block` 之后）添加：
```cpp
Switch,         // switch (x) { case v: {} default: {} }
RawNode,        // @node("Timeline", {...})
```

在 `FStatement` struct 末尾（L258 `Column` 之前）添加：
```cpp
// Switch 相关
struct FSwitchCase {
    TSharedPtr<FExpression> Value;  // nullptr = default case
    TArray<TSharedPtr<FStatement>> Body;
};
TArray<FSwitchCase> SwitchCases;

// RawNode 相关
FString RawNodeType;
TSharedPtr<FJsonObject> RawNodeParams;
```

**注意：** `FSwitchCase` 是嵌套 struct，应定义在 `FStatement` 外、BSL namespace 内。

**Step 2: 修改 BSLLexer.h — 新增 token**

在 `ETokenType` 枚举的 While 附近添加：
```cpp
Switch,
Case,
Default,
```

**Step 3: 修改 BSLLexer.cpp — 注册关键字**

在关键字 map（约 L452）添加：
```cpp
{TEXT("switch"), ETokenType::Switch},
{TEXT("case"), ETokenType::Case},
{TEXT("default"), ETokenType::Default},
```

**Step 4: 修改 BSLParser.cpp — 实现 ParseSwitch**

在 `ParseStatement` 中添加分支：
```cpp
if (Match(ETokenType::Switch))
    return ParseSwitch();
```

新增 `ParseSwitch` 函数实现：
```cpp
TSharedPtr<FStatement> FParser::ParseSwitch()
{
    TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::Switch);

    Consume(ETokenType::LeftParen, TEXT("Expected '(' after 'switch'"));
    Stmt->Condition = ParseExpression();
    Consume(ETokenType::RightParen, TEXT("Expected ')' after switch condition"));
    Consume(ETokenType::LeftBrace, TEXT("Expected '{' to start switch body"));

    while (!Check(ETokenType::RightBrace) && !IsAtEnd())
    {
        FStatement::FSwitchCase SwitchCase;

        if (Match(ETokenType::Case))
        {
            SwitchCase.Value = ParseExpression();
            Consume(ETokenType::Colon, TEXT("Expected ':' after case value"));
        }
        else if (Match(ETokenType::Default))
        {
            SwitchCase.Value = nullptr;  // nullptr = default
            Consume(ETokenType::Colon, TEXT("Expected ':' after 'default'"));
        }
        else break;

        if (Check(ETokenType::LeftBrace))
            SwitchCase.Body = ParseBlock();

        Stmt->SwitchCases.Add(SwitchCase);
    }

    Consume(ETokenType::RightBrace, TEXT("Expected '}' to end switch"));
    return Stmt;
}
```

**注意：** BSLParser.h 中需同步声明 `ParseSwitch()`，且 `FSwitchCase` 的 `Body` 需与 FStatement 的 Body 字段类型一致（`TArray<TSharedPtr<FStatement>>`）。

**Step 5: 修改 BSLCompiler.cpp — 编译 Switch**

```cpp
case EStatementType::Switch:
{
    // Switch 对应 Flow_Switch 节点
    // 由于 UE Switch 节点因类型不同（SwitchInteger/SwitchEnum/SwitchString）有不同 pin 布局，
    // 这里用 Flow_Switch 并通过 SwitchCases 数量自动生成 case pin 名 (Case_0, Case_1 ...)
    FBlueprintNodeData SwitchNode;
    SwitchNode.NodeId = GenerateNodeId(TEXT("switch"));
    SwitchNode.NodeType = EBlueprintNodeType::Flow_Switch;
    SwitchNode.Position = {400.0f, 0.0f};

    // 连接 execute
    if (!InOutLastExecNodeId.IsEmpty())
    {
        FBlueprintPinData ExecPin;
        ExecPin.Name = TEXT("execute");
        ExecPin.Direction = EBlueprintPinDirection::Input;
        FBlueprintPinConnection Conn;
        Conn.SourceNodeId = InOutLastExecNodeId;
        Conn.SourcePinName = InOutLastExecPinName;
        ExecPin.Connections.Add(Conn);
        SwitchNode.Pins.Add(ExecPin);
    }

    // 连接 switch 对象（Selection pin）
    if (Stmt.Condition.IsValid())
    {
        FString SelNodeId;
        FString SelPinName = CompileExpression(*Stmt.Condition, OutNodes, SelNodeId);
        if (!SelNodeId.IsEmpty())
        {
            FBlueprintPinData SelPin;
            SelPin.Name = TEXT("Selection");
            SelPin.Direction = EBlueprintPinDirection::Input;
            FBlueprintPinConnection Conn;
            Conn.SourceNodeId = SelNodeId;
            Conn.SourcePinName = SelPinName;
            SelPin.Connections.Add(Conn);
            SwitchNode.Pins.Add(SelPin);
        }
    }

    OutNodes.Add(SwitchNode);

    // 编译每个 case 分支
    int32 CaseIndex = 0;
    for (const FStatement::FSwitchCase& SwitchCase : Stmt.SwitchCases)
    {
        FString CasePinName = SwitchCase.Value.IsValid()
            ? FString::Printf(TEXT("Case_%d"), CaseIndex)
            : TEXT("Default");

        FString CaseLastNodeId = SwitchNode.NodeId;
        FString CaseLastPinName = CasePinName;
        CompileStatements(SwitchCase.Body, OutNodes, CaseLastNodeId, CaseLastPinName);

        if (SwitchCase.Value.IsValid()) CaseIndex++;
    }

    InOutLastExecNodeId.Empty();
    InOutLastExecPinName.Empty();
    return true;
}
```

**Step 6: 测试**

```bsl
blueprint BP_SwitchTest extends Actor {
  var Level: int = 1
  event BeginPlay {
    switch (Level) {
      case 1: { PrintString("Level 1") }
      case 2: { PrintString("Level 2") }
      default: { PrintString("Other") }
    }
  }
}
```

**Step 7: Commit**
```bash
git add Source/AssetFactoryAI/Public/BSL/BSLTypes.h Source/AssetFactoryAI/Public/BSL/BSLLexer.h \
        Source/AssetFactoryAI/Private/BSL/BSLLexer.cpp Source/AssetFactoryAI/Private/BSL/BSLParser.cpp \
        Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): add switch/case statement support"
```

---

## Task 12：Struct 字面量（Vector / Rotator / Transform）

**Files:**
- Modify: `Source/AssetFactoryAI/Public/BSL/BSLTypes.h`（新增 `EExpressionType::StructLiteral`，扩展 FExpression）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLParser.cpp`（在 ParsePrimary 中识别 Vector/Rotator/Transform 构造）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（编译 StructLiteral 为 MakeStruct 节点）

**Step 1: 修改 BSLTypes.h**

在 `EExpressionType` 枚举末尾添加：
```cpp
StructLiteral,  // Vector(x, y, z) / Rotator(p, y, r) / Transform(...)
```

在 `FExpression` struct 中添加字段（在 `CastType` 之后）：
```cpp
// StructLiteral
FString StructTypeName;   // "Vector", "Rotator", "Transform"
TArray<TSharedPtr<FExpression>> StructFields;  // 构造参数列表
```

**Step 2: 修改 BSLParser.cpp ParsePrimary**

在 `ParsePrimary` 中，识别 `Vector(...)` / `Rotator(...)` / `Transform(...)` 语法：

```cpp
// 检测 struct 字面量：Vector(...) / Rotator(...) / Transform(...)
if (Check(ETokenType::Identifier))
{
    FString IdName = Peek().Value;
    if (IdName == TEXT("Vector") || IdName == TEXT("Rotator") || IdName == TEXT("Transform"))
    {
        // 向前看一个 token，看是否是 (
        // 注意：需要保存 token 位置，避免误消费普通 Identifier
        int32 SavedPos = Current;
        Advance();  // 消费 Identifier
        if (Check(ETokenType::LeftParen))
        {
            Advance();  // 消费 (
            TSharedPtr<FExpression> StructExpr = MakeShared<FExpression>(EExpressionType::StructLiteral);
            StructExpr->StructTypeName = IdName;
            StructExpr->StructFields = ParseArguments();
            Consume(ETokenType::RightParen, TEXT("Expected ')' after struct fields"));
            return StructExpr;
        }
        // 回退：不是 struct 字面量，当作普通 Identifier 处理
        Current = SavedPos;
    }
}
```

**注意：** 需在 `FParser` 类中暴露 `Current` 成员（或通过 Backtrack 机制实现）。

**Step 3: 修改 BSLCompiler.cpp — 编译 StructLiteral**

```cpp
case EExpressionType::StructLiteral:
{
    FBlueprintNodeData MakeNode;
    MakeNode.NodeId = GenerateNodeId(TEXT("make_struct"));
    MakeNode.NodeType = EBlueprintNodeType::MakeStruct;
    MakeNode.StructType = Expr.StructTypeName;  // "Vector", "Rotator", "Transform"
    MakeNode.Position = {300.0f, 100.0f};

    // Vector 的 pin 名：X, Y, Z
    // Rotator 的 pin 名：Pitch, Yaw, Roll
    // Transform 的 pin 名：Translation, Rotation, Scale3D（各自是 Vector/Rotator）
    TArray<FString> FieldNames;
    if (Expr.StructTypeName == TEXT("Vector"))
        FieldNames = {TEXT("X"), TEXT("Y"), TEXT("Z")};
    else if (Expr.StructTypeName == TEXT("Rotator"))
        FieldNames = {TEXT("Pitch"), TEXT("Yaw"), TEXT("Roll")};
    else if (Expr.StructTypeName == TEXT("Transform"))
        FieldNames = {TEXT("Translation"), TEXT("Rotation"), TEXT("Scale3D")};

    for (int32 i = 0; i < Expr.StructFields.Num(); i++)
    {
        FString FieldPinName = FieldNames.IsValidIndex(i) ? FieldNames[i] : FString::Printf(TEXT("Field%d"), i);
        const TSharedPtr<FExpression>& FieldExpr = Expr.StructFields[i];
        if (!FieldExpr) continue;

        FString FieldNodeId;
        FString FieldPinOut = CompileExpression(*FieldExpr, OutNodes, FieldNodeId);

        FBlueprintPinData FieldPin;
        FieldPin.Name = FieldPinName;
        FieldPin.Direction = EBlueprintPinDirection::Input;
        if (!FieldNodeId.IsEmpty())
        {
            FBlueprintPinConnection Conn;
            Conn.SourceNodeId = FieldNodeId;
            Conn.SourcePinName = FieldPinOut;
            FieldPin.Connections.Add(Conn);
        }
        else if (FieldExpr->Type == EExpressionType::Literal_Float)
        {
            FieldPin.DefaultValue = FString::SanitizeFloat(FieldExpr->FloatValue);
        }
        else if (FieldExpr->Type == EExpressionType::Literal_Int)
        {
            FieldPin.DefaultValue = FString::SanitizeFloat(static_cast<double>(FieldExpr->IntValue));
        }
        MakeNode.Pins.Add(FieldPin);
    }

    OutNodes.Add(MakeNode);
    OutNodeId = MakeNode.NodeId;
    return TEXT("ReturnValue");
}
```

**注意：** `FBlueprintNodeData` 可能没有 `StructType` 字段，需确认 `BlueprintJSONSchema.h`。如没有，用 `VariableName` 临时存放。

**Step 4: 测试**

```bsl
blueprint BP_StructTest extends Actor {
  event BeginPlay {
    var loc: Vector = Vector(100.0, 0.0, 50.0)
    var rot: Rotator = Rotator(0.0, 90.0, 0.0)
  }
}
```

**Step 5: Commit**
```bash
git add Source/AssetFactoryAI/Public/BSL/BSLTypes.h Source/AssetFactoryAI/Private/BSL/BSLParser.cpp \
        Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): add struct literal support (Vector, Rotator, Transform)"
```

---

## Task 13：组件链式调用（self.Mesh.SetRelativeLocation(v)）

**Files:**
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（扩展 `CompileExpression::MemberAccess`，处理组件引用链）

**背景：** `self.Mesh.SetRelativeLocation(v)` 解析后为：
- `FunctionCall { Name: "SetRelativeLocation", Object: MemberAccess(Self, "Mesh"), Arguments: [v] }`
（在 Task 10 修复 parser 后）

编译策略：
1. `MemberAccess(Self, "Mesh")` → 生成 `GetComponent_Mesh` 纯函数节点（或直接在函数调用时处理）
2. `FunctionCall { Name: "SetRelativeLocation", Object: 上面的节点 }` → 生成 CallFunction 节点，Target pin 连接到 GetComponent 输出

**Step 1: 修改 `CompileExpression::MemberAccess` case（L935-963）**

将当前实现替换为更通用的版本：
```cpp
case EExpressionType::MemberAccess:
{
    // obj.member 作为表达式使用（不是方法调用）
    // 对 self.ComponentName 这类访问，生成 GetComponent 或 GetProperty 节点
    FBlueprintNodeData GetNode;
    GetNode.NodeId = GenerateNodeId(TEXT("get_member"));
    GetNode.NodeType = EBlueprintNodeType::PureFunction;
    // 约定命名：GetMeshComponent、GetSkeletalMeshComponent 等
    // 实际通过 FindFunctionByName 动态查找 Getter
    GetNode.FunctionReference = FString::Printf(TEXT("Get%s"), *Expr.MemberName);
    GetNode.Position = {200.0f, 100.0f};

    if (Expr.Object.IsValid())
    {
        FString ObjNodeId;
        FString ObjPinName = CompileExpression(*Expr.Object, OutNodes, ObjNodeId);
        if (!ObjNodeId.IsEmpty())
        {
            FBlueprintPinData TargetPin;
            TargetPin.Name = TEXT("self");
            TargetPin.Direction = EBlueprintPinDirection::Input;
            FBlueprintPinConnection Conn;
            Conn.SourceNodeId = ObjNodeId;
            Conn.SourcePinName = ObjPinName;
            TargetPin.Connections.Add(Conn);
            GetNode.Pins.Add(TargetPin);
        }
    }

    OutNodes.Add(GetNode);
    OutNodeId = GetNode.NodeId;
    return TEXT("ReturnValue");
}
```

**Step 2: Task 10 已经修复了 FunctionCall 带 Object 的情况**，这里只需确认 `CompileExpression::FunctionCall` 中正确处理了 `Expr.Object`（同 `CompileStatement::ExpressionStmt` 中 Target pin 的逻辑）。

**Step 3: 测试**

```bsl
blueprint BP_ComponentTest extends Actor {
  event BeginPlay {
    self.SetActorLocation(Vector(0.0, 0.0, 100.0))
  }
}
```

**Step 4: Commit**
```bash
git add Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): support component member access chains"
```

---

## Task 14：数组操作

**Files:**
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLParser.cpp`（将 `arr.Add(x)` 等识别为数组方法调用）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（将数组方法映射到正确的 Array_* 节点类型）

**背景：** 数组方法在 BSL 中写成 `arr.Add(x)`, `arr.Length()`, `arr[0]`。解析规则：
- `arr.Add(x)` → FunctionCall { Name: "Add", Object: Variable("arr") }（Task 10 修复后）
- `arr[0]` → ArrayAccess { Object: Variable("arr"), Index: 0 }（BSL 已有 `ArrayAccess` 类型）

**Step 1: 修改 `CompileStatement::ExpressionStmt` — 识别数组方法并映射到正确节点类型**

在创建 `CallNode` 之前，检查函数名是否是数组方法：

```cpp
// 检查是否是数组方法调用
static const TMap<FString, EBlueprintNodeType> ArrayMethodMap = {
    {TEXT("Add"),     EBlueprintNodeType::Array_Add},
    {TEXT("Remove"),  EBlueprintNodeType::Array_Remove},
    {TEXT("Length"),  EBlueprintNodeType::Array_Length},  // 若 Array_Length 已在 schema 中
};

EBlueprintNodeType NodeType = EBlueprintNodeType::CallFunction;
if (Stmt.Expression->Object.IsValid())
{
    if (const EBlueprintNodeType* ArrayType = ArrayMethodMap.Find(Stmt.Expression->Name))
    {
        NodeType = *ArrayType;
    }
}

FBlueprintNodeData CallNode;
CallNode.NodeId = GenerateNodeId(TEXT("call"));
CallNode.NodeType = NodeType;
CallNode.FunctionReference = Stmt.Expression->Name;
// ...（其余 Target pin + 参数编译逻辑与 Task 10 相同）
```

**Step 2: 实现 `CompileExpression::ArrayAccess`**

```cpp
case EExpressionType::ArrayAccess:
{
    // arr[index] 对应 Array_Get 纯函数节点
    FBlueprintNodeData GetNode;
    GetNode.NodeId = GenerateNodeId(TEXT("arr_get"));
    GetNode.NodeType = EBlueprintNodeType::Array_Get;
    GetNode.Position = {300.0f, 100.0f};

    // Array pin
    if (Expr.Object.IsValid())
    {
        FString ArrNodeId;
        FString ArrPinName = CompileExpression(*Expr.Object, OutNodes, ArrNodeId);
        if (!ArrNodeId.IsEmpty())
        {
            FBlueprintPinData ArrPin;
            ArrPin.Name = TEXT("TargetArray");
            ArrPin.Direction = EBlueprintPinDirection::Input;
            FBlueprintPinConnection Conn;
            Conn.SourceNodeId = ArrNodeId;
            Conn.SourcePinName = ArrPinName;
            ArrPin.Connections.Add(Conn);
            GetNode.Pins.Add(ArrPin);
        }
    }

    // Index pin
    if (Expr.Right.IsValid())  // 注意：ArrayAccess 的 index 存在 Right 还是另一个字段？
    {                           // 查看 BSLTypes.h：FExpression 没有专用 index 字段
        // 根据 Parser 实现确认 index 存在哪个字段（可能是 Arguments[0]）
        // 查 BSLParser.cpp ParsePostfix 中 index 分支的实现
    }

    OutNodes.Add(GetNode);
    OutNodeId = GetNode.NodeId;
    return TEXT("Item");  // Array_Get 的输出 pin 名
}
```

**注意：** 需先查阅 `BSLParser.cpp` 中 ArrayAccess 的 index 存在哪个字段（`Right`、`Arguments[0]` 还是其他）。

**Step 3: 测试**

```bsl
blueprint BP_ArrayTest extends Actor {
  var Items: Array<Actor>
  event BeginPlay {
    Items.Add(self)
    var count: int = Items.Length()
    var first: Actor = Items[0]
  }
}
```

**Step 4: Commit**
```bash
git add Source/AssetFactoryAI/Private/BSL/BSLParser.cpp Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp
git commit -m "feat(bsl): add array operation support (Add, Remove, Length, Get)"
```

---

## Task 15：@node 逃生舱语法

**Files:**
- Modify: `Source/AssetFactoryAI/Public/BSL/BSLLexer.h`（新增 `At` token）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLLexer.cpp`（识别 `@` 字符）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLParser.cpp`（实现 ParseRawNode）
- Modify: `Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp`（编译 RawNode → 透传给 NodeSpawner）

**背景：** `@node("Timeline", { TrackName: "FadeIn", Length: 1.5 })` 允许直接透传任意节点类型给 NodeSpawner，绕过 BSL 语法限制。

**Step 1: 修改 BSLLexer.h — 新增 At token**

在 ETokenType 枚举添加：
```cpp
At,    // @
```

**Step 2: 修改 BSLLexer.cpp — 识别 @ 字符**

在 Lexer 的字符扫描主循环添加：
```cpp
case '@': AddToken(ETokenType::At); break;
```

**Step 3: 修改 BSLParser.cpp — 实现 ParseRawNode**

在 `ParseStatement` 中，当遇到 `@` 时调用 `ParseRawNode()`：
```cpp
if (Match(ETokenType::At))
{
    // @node("NodeType", { json params })
    Consume(ETokenType::Identifier, TEXT("Expected 'node' after '@'"));  // consume "node"
    Consume(ETokenType::LeftParen, TEXT("Expected '(' after '@node'"));
    FToken NodeTypeToken = Consume(ETokenType::StringLiteral, TEXT("Expected node type string"));

    TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::RawNode);
    Stmt->RawNodeType = NodeTypeToken.Value;

    // 可选：解析 JSON 参数对象
    if (Match(ETokenType::Comma))
    {
        // 将后续内容读取为原始字符串直到 )
        // 简化实现：手动收集 token 直到配对的 )
        FString ParamsText;
        int32 Depth = 1;
        while (!IsAtEnd() && Depth > 0)
        {
            FToken T = Advance();
            if (T.Type == ETokenType::LeftParen) Depth++;
            else if (T.Type == ETokenType::RightParen) { Depth--; if (Depth == 0) break; }
            ParamsText += T.Value + TEXT(" ");
        }
        // 解析 ParamsText 为 FJsonObject
        TSharedPtr<FJsonObject> JsonParams;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsText);
        if (FJsonSerializer::Deserialize(Reader, JsonParams))
            Stmt->RawNodeParams = JsonParams;
    }
    else
    {
        Consume(ETokenType::RightParen, TEXT("Expected ')' to close @node"));
    }

    return Stmt;
}
```

**Step 4: 修改 BSLCompiler.cpp — 编译 RawNode**

```cpp
case EStatementType::RawNode:
{
    // 透传给 NodeSpawner：直接用 RawNodeType 作为 NodeType string
    // 需要将字符串 NodeType 映射到 EBlueprintNodeType，或者使用 Custom/Unknown 节点类型
    FBlueprintNodeData RawNode;
    RawNode.NodeId = GenerateNodeId(TEXT("raw"));
    RawNode.NodeType = EBlueprintNodeType::Unknown;  // NodeSpawner 需要能处理 Unknown + CustomNodeType
    RawNode.CustomNodeType = Stmt.RawNodeType;       // 需在 FBlueprintNodeData 中添加此字段
    RawNode.Position = {400.0f, 0.0f};

    // 连接 execute
    if (!InOutLastExecNodeId.IsEmpty())
    {
        FBlueprintPinData ExecPin;
        ExecPin.Name = TEXT("execute");
        ExecPin.Direction = EBlueprintPinDirection::Input;
        FBlueprintPinConnection Conn;
        Conn.SourceNodeId = InOutLastExecNodeId;
        Conn.SourcePinName = InOutLastExecPinName;
        ExecPin.Connections.Add(Conn);
        RawNode.Pins.Add(ExecPin);
    }

    // 将 RawNodeParams 的字段转换为 pin 的 DefaultValue
    if (Stmt.RawNodeParams.IsValid())
    {
        for (auto& Pair : Stmt.RawNodeParams->Values)
        {
            FString ValueStr;
            if (Pair.Value->TryGetString(ValueStr))
            {
                FBlueprintPinData ParamPin;
                ParamPin.Name = Pair.Key;
                ParamPin.Direction = EBlueprintPinDirection::Input;
                ParamPin.DefaultValue = ValueStr;
                RawNode.Pins.Add(ParamPin);
            }
        }
    }

    OutNodes.Add(RawNode);
    InOutLastExecNodeId = RawNode.NodeId;
    InOutLastExecPinName = TEXT("then");
    return true;
}
```

**注意：** 需要在 `FBlueprintNodeData`（`BlueprintJSONSchema.h`）中添加 `FString CustomNodeType` 字段，并在 NodeSpawner 中处理。

**Step 5: Commit**
```bash
git add Source/AssetFactoryAI/Public/BSL/BSLTypes.h Source/AssetFactoryAI/Public/BSL/BSLLexer.h \
        Source/AssetFactoryAI/Private/BSL/BSLLexer.cpp Source/AssetFactoryAI/Private/BSL/BSLParser.cpp \
        Source/AssetFactoryAI/Private/BSL/BSLCompiler.cpp Source/AssetFactoryAI/Public/JSON/BlueprintJSONSchema.h
git commit -m "feat(bsl): add @node escape hatch syntax for arbitrary Blueprint nodes"
```

---

## Task 16：BSLEmitter（AST → BSL 文本）

**Files:**
- Create: `Source/AssetFactoryAI/Public/BSL/BSLEmitter.h`
- Create: `Source/AssetFactoryAI/Private/BSL/BSLEmitter.cpp`

**背景：** BSLEmitter 接受 `BSL::FBlueprint` AST，输出 BSL 文本字符串。这是反向通路（Tier 4）的第一步。

**Step 1: 创建 BSLEmitter.h**

```cpp
#pragma once
#include "CoreMinimal.h"
#include "BSL/BSLTypes.h"

namespace BSL
{
class ASSETFACTORYAI_API FEmitter
{
public:
    /** 将 AST 序列化为 BSL 源代码字符串 */
    static FString Emit(const FBlueprint& Blueprint);

private:
    FEmitter();

    FString EmitBlueprint(const FBlueprint& BP);
    FString EmitFunction(const FFunction& Func, int32 Indent);
    FString EmitStatement(const FStatement& Stmt, int32 Indent);
    FString EmitExpression(const FExpression& Expr);
    FString EmitType(const FTypeInfo& Type);

    FString IndentStr(int32 Level) const;
    FString Result;
};
} // namespace BSL
```

**Step 2: 创建 BSLEmitter.cpp — 实现核心逻辑**

```cpp
#include "BSL/BSLEmitter.h"

namespace BSL
{

FString FEmitter::Emit(const FBlueprint& Blueprint)
{
    FEmitter Emitter;
    return Emitter.EmitBlueprint(Blueprint);
}

FString FEmitter::IndentStr(int32 Level) const
{
    FString Indent;
    for (int32 i = 0; i < Level; i++) Indent += TEXT("  ");
    return Indent;
}

FString FEmitter::EmitBlueprint(const FBlueprint& BP)
{
    FString Out = FString::Printf(TEXT("blueprint %s extends %s {\n"), *BP.Name, *BP.ParentClass);

    for (const FVariable& Var : BP.Variables)
    {
        FString Default = Var.DefaultValue.IsValid()
            ? FString::Printf(TEXT(" = %s"), *EmitExpression(*Var.DefaultValue))
            : TEXT("");
        Out += FString::Printf(TEXT("  var %s: %s%s\n"), *Var.Name, *EmitType(Var.Type), *Default);
    }

    if (BP.Variables.Num() > 0 && BP.Functions.Num() > 0) Out += TEXT("\n");

    for (const FFunction& Func : BP.Functions)
        Out += EmitFunction(Func, 1) + TEXT("\n");

    Out += TEXT("}\n");
    return Out;
}

FString FEmitter::EmitFunction(const FFunction& Func, int32 Indent)
{
    FString I = IndentStr(Indent);
    FString Header;

    if (Func.bIsEvent)
    {
        Header = FString::Printf(TEXT("%sevent %s"), *I, *Func.Name);
    }
    else
    {
        FString Params;
        for (int32 i = 0; i < Func.Inputs.Num(); i++)
        {
            if (i > 0) Params += TEXT(", ");
            Params += Func.Inputs[i].Name + TEXT(": ") + EmitType(Func.Inputs[i].Type);
        }
        FString Returns;
        if (Func.Outputs.Num() > 0)
        {
            for (int32 i = 0; i < Func.Outputs.Num(); i++)
            {
                if (i > 0) Returns += TEXT(", ");
                Returns += Func.Outputs[i].Name + TEXT(": ") + EmitType(Func.Outputs[i].Type);
            }
            Returns = FString::Printf(TEXT(" -> (%s)"), *Returns);
        }
        Header = FString::Printf(TEXT("%sfunction %s(%s)%s"), *I, *Func.Name, *Params, *Returns);
    }

    FString Body = TEXT(" {\n");
    for (const TSharedPtr<FStatement>& Stmt : Func.Body)
        if (Stmt) Body += EmitStatement(*Stmt, Indent + 1);
    Body += I + TEXT("}");

    return Header + Body;
}

FString FEmitter::EmitStatement(const FStatement& Stmt, int32 Indent)
{
    FString I = IndentStr(Indent);
    switch (Stmt.Type)
    {
    case EStatementType::VariableDecl:
    {
        FString Default = Stmt.DeclaredVariable.DefaultValue.IsValid()
            ? TEXT(" = ") + EmitExpression(*Stmt.DeclaredVariable.DefaultValue) : TEXT("");
        return FString::Printf(TEXT("%svar %s: %s%s\n"), *I, *Stmt.DeclaredVariable.Name,
                               *EmitType(Stmt.DeclaredVariable.Type), *Default);
    }
    case EStatementType::Assignment:
        return FString::Printf(TEXT("%s%s = %s\n"), *I, *Stmt.AssignTarget,
                               *EmitExpression(*Stmt.AssignValue));
    case EStatementType::ExpressionStmt:
        return I + EmitExpression(*Stmt.Expression) + TEXT("\n");
    case EStatementType::If:
    {
        FString Out = FString::Printf(TEXT("%sif (%s) {\n"), *I, *EmitExpression(*Stmt.Condition));
        for (const TSharedPtr<FStatement>& S : Stmt.ThenBody)
            if (S) Out += EmitStatement(*S, Indent + 1);
        Out += I + TEXT("}");
        if (Stmt.ElseBody.Num() > 0)
        {
            Out += TEXT(" else {\n");
            for (const TSharedPtr<FStatement>& S : Stmt.ElseBody)
                if (S) Out += EmitStatement(*S, Indent + 1);
            Out += I + TEXT("}");
        }
        return Out + TEXT("\n");
    }
    case EStatementType::While:
    {
        FString Out = FString::Printf(TEXT("%swhile (%s) {\n"), *I, *EmitExpression(*Stmt.Condition));
        for (const TSharedPtr<FStatement>& S : Stmt.LoopBody)
            if (S) Out += EmitStatement(*S, Indent + 1);
        return Out + I + TEXT("}\n");
    }
    case EStatementType::For:
    {
        FString Out = FString::Printf(TEXT("%sfor %s in %s..%s {\n"), *I, *Stmt.LoopVariable,
                                      *EmitExpression(*Stmt.LoopStart), *EmitExpression(*Stmt.LoopEnd));
        for (const TSharedPtr<FStatement>& S : Stmt.LoopBody)
            if (S) Out += EmitStatement(*S, Indent + 1);
        return Out + I + TEXT("}\n");
    }
    case EStatementType::ForEach:
    {
        FString Out = FString::Printf(TEXT("%sforeach %s in %s {\n"), *I, *Stmt.LoopVariable,
                                      *EmitExpression(*Stmt.LoopCollection));
        for (const TSharedPtr<FStatement>& S : Stmt.LoopBody)
            if (S) Out += EmitStatement(*S, Indent + 1);
        return Out + I + TEXT("}\n");
    }
    case EStatementType::Return:
    {
        if (Stmt.ReturnValues.Num() == 0) return I + TEXT("return\n");
        if (Stmt.ReturnValues.Num() == 1) return I + TEXT("return ") + EmitExpression(*Stmt.ReturnValues[0]) + TEXT("\n");
        TArray<FString> Parts;
        for (const TSharedPtr<FExpression>& V : Stmt.ReturnValues)
            if (V) Parts.Add(EmitExpression(*V));
        return I + TEXT("return (") + FString::Join(Parts, TEXT(", ")) + TEXT(")\n");
    }
    case EStatementType::Break:    return I + TEXT("break\n");
    case EStatementType::Continue: return I + TEXT("continue\n");
    default: return I + TEXT("// <unsupported statement>\n");
    }
}

FString FEmitter::EmitExpression(const FExpression& Expr)
{
    switch (Expr.Type)
    {
    case EExpressionType::Literal_Bool:   return Expr.BoolValue ? TEXT("true") : TEXT("false");
    case EExpressionType::Literal_Int:    return FString::FromInt(Expr.IntValue);
    case EExpressionType::Literal_Float:  return FString::SanitizeFloat(Expr.FloatValue);
    case EExpressionType::Literal_String: return TEXT("\"") + Expr.StringValue + TEXT("\"");
    case EExpressionType::Variable:       return Expr.Name;
    case EExpressionType::Self:           return TEXT("self");
    case EExpressionType::MemberAccess:
        return EmitExpression(*Expr.Object) + TEXT(".") + Expr.MemberName;
    case EExpressionType::FunctionCall:
    {
        TArray<FString> Args;
        for (const TSharedPtr<FExpression>& Arg : Expr.Arguments)
            if (Arg) Args.Add(EmitExpression(*Arg));
        FString ArgStr = FString::Join(Args, TEXT(", "));
        if (Expr.Object.IsValid())
            return EmitExpression(*Expr.Object) + TEXT(".") + Expr.Name + TEXT("(") + ArgStr + TEXT(")");
        return Expr.Name + TEXT("(") + ArgStr + TEXT(")");
    }
    case EExpressionType::BinaryOp:
    {
        static const TMap<EBinaryOp, FString> OpMap = {
            {EBinaryOp::Add, TEXT("+")}, {EBinaryOp::Subtract, TEXT("-")},
            {EBinaryOp::Multiply, TEXT("*")}, {EBinaryOp::Divide, TEXT("/")},
            {EBinaryOp::Equal, TEXT("==")}, {EBinaryOp::NotEqual, TEXT("!=")},
            {EBinaryOp::Less, TEXT("<")}, {EBinaryOp::LessEqual, TEXT("<=")},
            {EBinaryOp::Greater, TEXT(">")}, {EBinaryOp::GreaterEqual, TEXT(">=")},
            {EBinaryOp::And, TEXT("&&")}, {EBinaryOp::Or, TEXT("||")},
        };
        FString Op = OpMap.Contains(Expr.BinaryOp) ? *OpMap.Find(Expr.BinaryOp) : TEXT("?");
        return EmitExpression(*Expr.Left) + TEXT(" ") + Op + TEXT(" ") + EmitExpression(*Expr.Right);
    }
    case EExpressionType::UnaryOp:
        return (Expr.UnaryOp == EUnaryOp::Negate ? TEXT("-") : TEXT("!")) + EmitExpression(*Expr.Left);
    case EExpressionType::Cast:
        return FString::Printf(TEXT("Cast<%s>(%s)"), *Expr.CastType.SubType, *EmitExpression(*Expr.Left));
    case EExpressionType::ArrayAccess:
        return EmitExpression(*Expr.Object) + TEXT("[") + EmitExpression(*Expr.Right) + TEXT("]");
    default: return TEXT("/* unknown */");
    }
}

FString FEmitter::EmitType(const FTypeInfo& Type)
{
    switch (Type.Type)
    {
    case EType::Bool:   return TEXT("bool");
    case EType::Int:    return TEXT("int");
    case EType::Float:  return TEXT("float");
    case EType::String: return TEXT("string");
    case EType::Vector: return TEXT("Vector");
    case EType::Rotator:return TEXT("Rotator");
    case EType::Object: return Type.SubType.IsEmpty() ? TEXT("Object") : Type.SubType;
    case EType::Array:  return FString::Printf(TEXT("Array<%s>"), *Type.SubType);
    default:            return TEXT("unknown");
    }
}

} // namespace BSL
```

**Step 3: 在 AssetFactoryAI.Build.cs 确认 BSLEmitter.cpp 会被编译（自动，无需修改）**

**Step 4: Commit**
```bash
git add Source/AssetFactoryAI/Public/BSL/BSLEmitter.h Source/AssetFactoryAI/Private/BSL/BSLEmitter.cpp
git commit -m "feat(bsl): add BSLEmitter for AST to BSL text serialization"
```

---

## Task 17：BlueprintDecompiler（UEdGraph → BSL AST）

**文件：**
- Create: `Source/AssetFactoryAI/Public/BSL/BlueprintDecompiler.h`
- Create: `Source/AssetFactoryAI/Private/BSL/BlueprintDecompiler.cpp`

**背景：** 将现有 UBlueprint 的 UEdGraph 逆向还原为 BSL AST，再通过 BSLEmitter 生成 BSL 文本。

**核心算法：**
1. 从 Event/FunctionEntry 节点出发，沿 exec pin 追踪执行流
2. 线性执行链 → 顺序语句列表
3. `K2Node_IfThenElse` → `FStatement(If)`
4. ForLoop/WhileLoop 宏 → `FStatement(For/While)`
5. 数据流节点（无 exec pin）→ 递归构建 `FExpression` 树
6. 无法识别的节点 → `FStatement(RawNode)` 逃生舱

**Step 1: 创建 BlueprintDecompiler.h**

```cpp
#pragma once
#include "CoreMinimal.h"
#include "BSL/BSLTypes.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

namespace BSL
{

struct FDecompileResult
{
    bool bSuccess = false;
    FBlueprint Blueprint;
    TArray<FString> Warnings;
};

class ASSETFACTORYAI_API FBlueprintDecompiler
{
public:
    static FDecompileResult Decompile(UBlueprint* Blueprint);

private:
    FBlueprintDecompiler();

    void DecompileGraph(UEdGraph* Graph, FFunction& OutFunc);
    TArray<TSharedPtr<FStatement>> WalkExecChain(UEdGraphPin* ExecPin);
    TSharedPtr<FStatement> NodeToStatement(UEdGraphNode* Node);
    TSharedPtr<FExpression> PinToExpression(UEdGraphPin* DataPin);

    TSet<UEdGraphNode*> VisitedNodes;
    TArray<FString> Warnings;
};

} // namespace BSL
```

**Step 2: 实现 BlueprintDecompiler.cpp（核心框架）**

由于完整实现较复杂，此处实现核心框架，支持基础场景：

```cpp
#include "BSL/BlueprintDecompiler.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Self.h"
#include "K2Node_MacroInstance.h"

namespace BSL
{

FDecompileResult FBlueprintDecompiler::Decompile(UBlueprint* BP)
{
    FDecompileResult Result;
    if (!BP) return Result;

    Result.Blueprint.Name = BP->GetName();
    if (BP->ParentClass)
        Result.Blueprint.ParentClass = BP->ParentClass->GetName();

    // 反编译每个 EventGraph
    for (UEdGraph* Graph : BP->UbergraphPages)
    {
        // 找到所有 Event 节点，每个是一个独立 Function
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
            {
                FFunction Func;
                Func.bIsEvent = true;
                Func.Name = EventNode->EventReference.GetMemberName().ToString();

                FBlueprintDecompiler Decompiler;
                UEdGraphPin* ThenPin = EventNode->FindPin(TEXT("then"), EGPD_Output);
                if (ThenPin)
                    Func.Body = Decompiler.WalkExecChain(ThenPin);
                Func.Body = Decompiler.WalkExecChain(ThenPin);
                Result.Blueprint.Functions.Add(Func);
            }
        }
    }

    Result.bSuccess = true;
    return Result;
}

TArray<TSharedPtr<FStatement>> FBlueprintDecompiler::WalkExecChain(UEdGraphPin* ExecPin)
{
    TArray<TSharedPtr<FStatement>> Stmts;
    if (!ExecPin || ExecPin->LinkedTo.Num() == 0) return Stmts;

    UEdGraphNode* NextNode = ExecPin->LinkedTo[0]->GetOwningNode();
    while (NextNode && !VisitedNodes.Contains(NextNode))
    {
        VisitedNodes.Add(NextNode);
        TSharedPtr<FStatement> Stmt = NodeToStatement(NextNode);
        if (Stmt) Stmts.Add(Stmt);

        // 找到 "then" exec output pin 继续追踪
        UEdGraphPin* ThenPin = NextNode->FindPin(TEXT("then"), EGPD_Output);
        if (!ThenPin || ThenPin->LinkedTo.Num() == 0) break;
        NextNode = ThenPin->LinkedTo[0]->GetOwningNode();
    }
    return Stmts;
}

TSharedPtr<FStatement> FBlueprintDecompiler::NodeToStatement(UEdGraphNode* Node)
{
    if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
    {
        TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::ExpressionStmt);
        TSharedPtr<FExpression> Call = MakeShared<FExpression>(EExpressionType::FunctionCall);
        Call->Name = CallNode->FunctionReference.GetMemberName().ToString();
        // TODO: 遍历非 exec 输入 pin，构建 Arguments
        Stmt->Expression = Call;
        return Stmt;
    }
    else if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(Node))
    {
        TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::Assignment);
        Stmt->AssignTarget = SetNode->GetVarName().ToString();
        // TODO: 从 value pin 构建 AssignValue
        return Stmt;
    }
    else if (UK2Node_IfThenElse* BranchNode = Cast<UK2Node_IfThenElse>(Node))
    {
        TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::If);
        UEdGraphPin* CondPin = BranchNode->FindPin(TEXT("Condition"), EGPD_Input);
        if (CondPin && CondPin->LinkedTo.Num() > 0)
            Stmt->Condition = PinToExpression(CondPin->LinkedTo[0]);

        UEdGraphPin* ThenPin = BranchNode->FindPin(TEXT("then"), EGPD_Output);
        UEdGraphPin* ElsePin = BranchNode->FindPin(TEXT("else"), EGPD_Output);
        if (ThenPin) Stmt->ThenBody = WalkExecChain(ThenPin);
        if (ElsePin) Stmt->ElseBody = WalkExecChain(ElsePin);
        return Stmt;
    }

    // 无法识别：RawNode 逃生舱
    TSharedPtr<FStatement> RawStmt = MakeShared<FStatement>(EStatementType::RawNode);
    RawStmt->RawNodeType = Node->GetClass()->GetName();
    Warnings.Add(FString::Printf(TEXT("Unrecognized node: %s"), *Node->GetClass()->GetName()));
    return RawStmt;
}

TSharedPtr<FExpression> FBlueprintDecompiler::PinToExpression(UEdGraphPin* DataPin)
{
    if (!DataPin) return nullptr;
    UEdGraphNode* SourceNode = DataPin->GetOwningNode();

    if (UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(SourceNode))
    {
        return FExpression::MakeVariable(GetNode->GetVarName().ToString());
    }
    else if (UK2Node_Self* SelfNode = Cast<UK2Node_Self>(SourceNode))
    {
        return MakeShared<FExpression>(EExpressionType::Self);
    }

    // 字面量（从 pin 的 DefaultValue 读取）
    if (!DataPin->DefaultValue.IsEmpty())
    {
        // 根据 pin 类型解析
        if (DataPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
            return FExpression::MakeInt(FCString::Atoi(*DataPin->DefaultValue));
        if (DataPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Float)
            return FExpression::MakeFloat(FCString::Atof(*DataPin->DefaultValue));
        if (DataPin->PinType.PinCategory == UEdGraphSchema_K2::PC_String)
            return FExpression::MakeString(DataPin->DefaultValue);
        if (DataPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
            return FExpression::MakeBool(DataPin->DefaultValue.ToBool());
    }

    return nullptr;
}

} // namespace BSL
```

**Step 3: Commit**
```bash
git add Source/AssetFactoryAI/Public/BSL/BlueprintDecompiler.h Source/AssetFactoryAI/Private/BSL/BlueprintDecompiler.cpp
git commit -m "feat(bsl): add BlueprintDecompiler for UEdGraph to BSL AST conversion"
```

---

## Task 18：MCP 端点 extract_blueprint_as_bsl

**Files:**
- Modify: `Source/AssetFactory/Private/AssetFactoryHttpServer.cpp`（新增 `/assetfactory/extract_bsl` 路由）
- Modify: `MCP/src/index.ts`（新增 `extract_blueprint_as_bsl` MCP 工具）

**Step 1: 在 HTTP Server 添加路由**

在 `AssetFactoryHttpServer.cpp` 处理 GET 请求的地方（与 `/assetfactory/screenshot` 类似），新增：

```cpp
// GET /assetfactory/extract_bsl?asset=/Game/Blueprints/BP_Player
if (Request.Method == TEXT("GET") && Request.Path.StartsWith(TEXT("/assetfactory/extract_bsl")))
{
    FString AssetPath = // 从 QueryString 解析 asset 参数

    AsyncTask(ENamedThreads::GameThread, [this, AssetPath, Connection]()
    {
        UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AssetPath));
        if (!BP)
        {
            SendJsonResponse(Connection, {{"error", "Blueprint not found: " + AssetPath}});
            return;
        }

        BSL::FDecompileResult DecompileResult = BSL::FBlueprintDecompiler::Decompile(BP);
        if (!DecompileResult.bSuccess)
        {
            SendJsonResponse(Connection, {{"error", "Decompile failed"}});
            return;
        }

        FString BSLText = BSL::FEmitter::Emit(DecompileResult.Blueprint);
        SendJsonResponse(Connection, {
            {"bsl", BSLText},
            {"warnings", DecompileResult.Warnings}
        });
    });
    return;
}
```

**Step 2: 在 MCP TypeScript 添加工具**

在 `MCP/src/index.ts` 中，仿照 `get_viewport_screenshot` 工具，添加：

```typescript
server.tool(
  "extract_blueprint_as_bsl",
  "Decompile an existing Blueprint asset into BSL source code",
  {
    asset_path: z.string().describe("Blueprint asset path, e.g. /Game/Blueprints/BP_Player"),
  },
  async ({ asset_path }) => {
    const serviceUrl = await getServiceUrl();
    const resp = await fetch(`${serviceUrl}/assetfactory/extract_bsl?asset=${encodeURIComponent(asset_path)}`);
    const data = await resp.json();
    if (data.error) throw new Error(data.error);
    return {
      content: [{ type: "text", text: data.bsl }],
    };
  }
);
```

**Step 3: Commit**
```bash
git add Source/AssetFactory/Private/AssetFactoryHttpServer.cpp MCP/src/index.ts
git commit -m "feat: add extract_blueprint_as_bsl MCP tool and HTTP endpoint"
```

---

## 实现顺序总结

| 优先级 | Task | 工作量 |
|--------|------|--------|
| P0 | Task 1 - Pin 反射解析 | ★★ |
| P0 | Task 2 - While 循环 | ★ |
| P0 | Task 3 - For 循环 | ★ |
| P0 | Task 4 - ForEach 循环 | ★ |
| P1 | Task 5 - 局部变量 + VariableNodeMap 重构 | ★★ |
| P1 | Task 6 - Cast | ★ |
| P1 | Task 7 - Self | ★ |
| P1 | Task 8 - MultiAssignment | ★★ |
| P1 | Task 9 - Negate | ★ |
| P1 | Task 10 - Parser 方法调用修复 | ★★ |
| P2 | Task 11 - Switch/case | ★★★ |
| P2 | Task 12 - Struct 字面量 | ★★ |
| P2 | Task 13 - 组件链式调用 | ★ |
| P2 | Task 14 - 数组操作 | ★★ |
| P3 | Task 15 - @node 逃生舱 | ★★★ |
| P4 | Task 16 - BSLEmitter | ★★★ |
| P4 | Task 17 - BlueprintDecompiler | ★★★★ |
| P4 | Task 18 - MCP 端点 | ★★ |
