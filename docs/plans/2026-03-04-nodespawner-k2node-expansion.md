# NodeSpawner K2Node 扩展计划

> 日期: 2026-03-04
> 状态: 待实现
> 前置提交: `84851cb` (fix(bsl): resolve 8 compiler bugs + function creation order)

## 1. 背景

BSL (Blueprint Script Language) 编译器通过 `BSLCompiler` 将 BSL AST 转为 `FBlueprintData`，再由 `AIBlueprintFactory` + `NodeSpawner` 生成实际的 UE 蓝图节点。

当前 NodeSpawner 注册了 **39 个 EBlueprintNodeType** 枚举值，映射到 **9 个 Spawn 函数**，底层使用 **16 种 K2Node 类**。覆盖了基本变量、函数调用、流程控制、数学/比较/逻辑、数组、Cast、MakeStruct 等场景。

本文档规划缺失的 K2Node 支持，按优先级分 4 批实现。

## 2. 当前状态

### 2.1 已支持的 K2Node (16 种)

| K2Node 类 | 对应 Spawn 函数 | 覆盖的 NodeType |
|-----------|----------------|----------------|
| UK2Node_Event | SpawnEventNode | Event_BeginPlay, Event_Tick, Event_Input |
| UK2Node_CustomEvent | SpawnEventNode | Event_Custom |
| UK2Node_CallFunction | SpawnFunctionCallNode, SpawnMathNode, SpawnComparisonNode, SpawnLogicNode, SpawnArrayNode | CallFunction, PureFunction, Math_*, Compare_*, Logic_*, Array_* |
| UK2Node_IfThenElse | SpawnFlowControlNode | Flow_Branch |
| UK2Node_ExecutionSequence | SpawnFlowControlNode | Flow_Sequence |
| UK2Node_SwitchInteger | SpawnFlowControlNode | Flow_Switch |
| UK2Node_MacroInstance | SpawnFlowControlNode | Flow_ForLoop, Flow_ForEachLoop, Flow_WhileLoop, Flow_DoOnce, Flow_Gate, Flow_Delay |
| UK2Node_VariableGet | SpawnVariableNode | Variable_Get, Variable_GetLocal |
| UK2Node_VariableSet | SpawnVariableNode | Variable_Set, Variable_SetLocal |
| UK2Node_DynamicCast | SpawnCastNode | Cast |
| UK2Node_MakeStruct | SpawnMakeStructNode | MakeStruct |
| UK2Node_MakeArray | (内部使用) | 数组字面量 |
| UK2Node_CommutativeAssociativeBinaryOperator | (内部使用) | 数学运算 |
| UK2Node_FunctionEntry | SpawnReturnNode (查找) | 函数入口 |
| UK2Node_FunctionResult | SpawnReturnNode | Return |
| UK2Node_ForEachElementInEnum | (include 但未直接使用) | - |

### 2.2 已定义但未实现的 EBlueprintNodeType

| NodeType | 状态 |
|----------|------|
| BreakStruct | 枚举已定义，无 Spawn 实现 |
| Delegate_Bind | 枚举已定义，无 Spawn 实现 |
| Delegate_Unbind | 枚举已定义，无 Spawn 实现 |
| Delegate_Execute | 枚举已定义，无 Spawn 实现 |
| Literal | 枚举已定义，无 Spawn 实现 |
| Comment | 枚举已定义，无 Spawn 实现 |
| Reroute | 枚举已定义，无 Spawn 实现 |

### 2.3 BSL 语言当前能力

**数据类型** (EType): Void, Bool, Int, Float, String, Name, Text, Vector, Rotator, Transform, Object, Class, Array

**表达式** (EExpressionType): 字面量(Bool/Int/Float/String/Vector/Rotator), Variable, FunctionCall, MemberAccess, ArrayAccess, BinaryOp, UnaryOp, Self, Cast, StructLiteral

**语句** (EStatementType): VariableDecl, Assignment, MultiAssignment, ArraySet, If, While, For, ForEach, Return, Break, Continue, ExpressionStmt, Block, Switch, RawNode

### 2.4 关键文件路径

```
Source/AssetFactoryAI/
├── Public/
│   ├── BSL/
│   │   ├── BSLTypes.h          # AST 类型定义 (EType, EExpressionType, EStatementType 等)
│   │   ├── BSLLexer.h          # 词法分析器
│   │   ├── BSLParser.h         # 语法解析器 (Token -> AST)
│   │   └── BSLCompiler.h       # 编译器 (AST -> FBlueprintData)
│   ├── JSON/
│   │   └── BlueprintJSONSchema.h  # FBlueprintData 数据结构 (EBlueprintNodeType 枚举)
│   └── Factory/
│       └── NodeSpawner.h       # 节点生成器头文件
├── Private/
│   ├── BSL/
│   │   ├── BSLTypes.cpp
│   │   ├── BSLLexer.cpp
│   │   ├── BSLParser.cpp
│   │   └── BSLCompiler.cpp     # 编译器实现 (~2200 行)
│   └── Factory/
│       ├── NodeSpawner.cpp      # 节点生成器实现 (~1150 行)
│       └── AIBlueprintFactory.cpp # 蓝图工厂 (CreateBlueprint, ModifyBlueprint)
```

**引擎 K2Node 源码**: `E:\Epic Games\UE_5.7\Engine\Source\Editor\BlueprintGraph\Classes\K2Node_*.h`

---

## 3. 实现计划

### 3.1 P0: BreakStruct (Task #20)

**目标**: 拆解结构体（如 HitResult.Location, HitResult.Actor 等）

**引擎参考**: `K2Node_BreakStruct.h`

**需要修改的文件**:

1. **NodeSpawner.cpp** - 新增 `SpawnBreakStructNode`
   - 类似 `SpawnMakeStructNode`，但方向相反
   - 输入: 结构体 pin (Input)
   - 输出: 各成员 pin (Output)
   - 需要 `UK2Node_BreakStruct::SetStruct()` 设置结构体类型

2. **NodeSpawner.cpp** - 在 `GetNodeSpawnHandlers` 中注册 `BreakStruct -> SpawnBreakStructNode`

3. **BSLCompiler.cpp** - 成员访问表达式编译
   - 当 `MemberAccess` 的对象是结构体类型时，生成 BreakStruct 节点
   - 例如 `hitResult.Location` -> BreakStruct(HitResult) 的 Location 输出 pin
   - 需要在 `CompileExpression` 的 `MemberAccess` case 中添加结构体检测

4. **BSLTypes.h** - 可能不需要改动（MemberAccess 表达式已存在）

**关键检查点**:
- [ ] 确认 `UK2Node_BreakStruct` 的 pin 命名规则（是用属性名还是 DisplayName？）
- [ ] 确认 `BreakStruct` 是否需要 `bMadeAfterOverridePinRemoval` 标志
- [ ] 对于嵌套结构体（如 `transform.Location.X`），是否需要链式 BreakStruct

**测试 BSL**:
```
blueprint BP_Test_BreakStruct extends Actor {
  event BeginPlay {
    var hit: HitResult
    var loc: Vector = hit.Location
    var actor: Actor = hit.Actor
    PrintString(loc.X)
  }
}
```

---

### 3.2 P0: Delegate/事件委托系统 (Task #21)

**目标**: 支持事件派发器绑定、解绑、触发

**引擎参考**: `K2Node_AddDelegate.h`, `K2Node_RemoveDelegate.h`, `K2Node_CallDelegate.h`, `K2Node_CreateDelegate.h`, `K2Node_ComponentBoundEvent.h`

**需要修改的文件**:

1. **BSLTypes.h** - 新增 AST 节点
   - `EStatementType::DelegateBind` - 委托绑定语句
   - `EStatementType::DelegateUnbind` - 委托解绑语句
   - `EExpressionType::DelegateCall` - 委托调用表达式
   - `FStatement` 中添加委托相关字段：DelegateName, EventName, TargetFunction

2. **BSLLexer.h/cpp** - 新增关键词
   - `bind`, `unbind`, `dispatch` (或复用已有语法)

3. **BSLParser.cpp** - 新增解析规则
   - `bind ComponentName.EventName to FunctionName`
   - `unbind ComponentName.EventName from FunctionName`
   - `dispatch EventName(args)`
   - 或者用方法调用语法: `OnClicked.Bind(HandleClick)`

4. **BSLCompiler.cpp** - 新增委托编译
   - 生成 `Delegate_Bind/Unbind/Execute` NodeType

5. **NodeSpawner.cpp** - 新增 `SpawnDelegateNode`
   - Delegate_Bind -> `UK2Node_AddDelegate`
   - Delegate_Unbind -> `UK2Node_RemoveDelegate`
   - Delegate_Execute -> `UK2Node_CallDelegate`

6. **BlueprintJSONSchema.h** - 已有 `Delegate_Bind/Unbind/Execute` 枚举

**BSL 语法设计建议** (两种方案):

方案 A - 关键词风格:
```
event BeginPlay {
  bind Mesh.OnComponentHit to HandleHit
  dispatch OnDamaged(50.0)
}
```

方案 B - 方法调用风格:
```
event BeginPlay {
  Mesh.OnComponentHit.Bind(HandleHit)
  OnDamaged.Broadcast(50.0)
}
```

**建议选择方案 B**：与 UE 蓝图的视觉风格更一致，且不需要新增关键词。

**关键检查点**:
- [ ] `UK2Node_AddDelegate` 需要哪些特殊 pin（DelegateReference, Target 等）
- [ ] `K2Node_ComponentBoundEvent` vs `K2Node_AddDelegate` 的使用场景区别
- [ ] 多播委托 (Multicast Delegate) vs 单播委托的处理差异

**测试 BSL**:
```
blueprint BP_Test_Delegate extends Actor {
  var Mesh: StaticMeshComponent

  function HandleHit(HitComp: PrimitiveComponent, OtherActor: Actor) {
    PrintString("Hit!")
  }

  event BeginPlay {
    Mesh.OnComponentHit.Bind(HandleHit)
  }
}
```

---

### 3.3 P1: SpawnActorFromClass (Task #22)

**目标**: 运行时生成 Actor

**引擎参考**: `K2Node_SpawnActorFromClass.h`

**需要修改的文件**:

1. **EBlueprintNodeType** - 新增 `SpawnActor` 枚举值
2. **NodeSpawner.cpp** - 新增 `SpawnActorNode`
   - 使用 `UK2Node_SpawnActorFromClass`
   - 特殊: 有 template pin（暴露生成的 Actor 属性）
   - 需要 `SetClassInputPin()` 或类似方法设置目标类
3. **BSLCompiler.cpp** - 识别 `SpawnActor` 函数调用并生成专用节点
4. **BSLTypes.h** - 可能不需要改动（通过 FunctionCall 表达式处理）

**BSL 语法**: 用函数调用风格
```
var enemy: Actor = SpawnActor(BP_Enemy, location, rotation)
```

**关键检查点**:
- [ ] template pin 如何创建和连接
- [ ] `SpawnActorFromClass` vs `SpawnActor` (GameplayStatics) 的区别
- [ ] 返回值类型是否随 Class 参数变化

---

### 3.4 P1: Timeline (Task #23)

**目标**: 支持 Timeline 节点

**引擎参考**: `K2Node_Timeline.h`

**复杂度**: 高 - Timeline 不是简单的节点，涉及 UTimelineComponent + CurveFloat 资产。

**需要修改的文件**:

1. **EBlueprintNodeType** - 新增 `Timeline` 枚举值
2. **NodeSpawner.cpp** - 新增 `SpawnTimelineNode`
   - 创建 `UK2Node_Timeline`
   - 需要在 Blueprint 上创建 `UTimelineTemplate`
   - 关联 CurveFloat/CurveVector 资产
3. **BSLTypes.h** - 可能需要新的语句类型 `TimelineDecl`
4. **BSLParser/Compiler** - Timeline 声明块解析和编译

**BSL 语法设计**:
```
blueprint BP_Door extends Actor {
  timeline DoorTimeline {
    curve DoorAlpha: CurveFloat = "/Game/Curves/CF_DoorOpen"
    length: 2.0
    looping: false
  }

  event BeginPlay {
    DoorTimeline.PlayFromStart()
  }

  event DoorTimeline.Update(Alpha: float) {
    var newLoc: Vector = Lerp(ClosedPos, OpenPos, Alpha)
    SetActorLocation(newLoc)
  }

  event DoorTimeline.Finished {
    PrintString("Door opened")
  }
}
```

**关键检查点**:
- [ ] `UTimelineTemplate` 如何在 Blueprint 上创建
- [ ] Timeline 的 Update/Finished 输出 pin 如何与事件连接
- [ ] CurveFloat 资产引用方式
- [ ] 考虑是否先通过 `@node("Timeline", {...})` RawNode 支持，后续再做原生语法

---

### 3.5 P1: SetFieldsInStruct (Task #24)

**目标**: 修改已有结构体的部分字段

**引擎参考**: `K2Node_SetFieldsInStruct.h`

**需要修改的文件**:

1. **EBlueprintNodeType** - 新增 `SetFieldsInStruct` 枚举值
2. **NodeSpawner.cpp** - 新增 `SpawnSetFieldsNode`
   - 输入: 结构体 + 各字段值
   - 输出: 修改后的结构体
3. **BSLCompiler.cpp** - 识别结构体字段赋值并生成 SetFields 节点
   - `myVector.X = 100` -> SetFieldsInStruct

**BSL 语法**: 用赋值语法
```
var pos: Vector = GetActorLocation()
pos.X = 100.0
pos.Z = pos.Z + 50.0
SetActorLocation(pos)
```

**关键检查点**:
- [ ] `SetFieldsInStruct` 的 pin 命名规则
- [ ] 与 MakeStruct 的区别处理（编译器需区分"创建新结构体" vs "修改已有结构体"）
- [ ] 链式修改是否需要多个 SetFieldsInStruct 节点

---

### 3.6 P2: Select + SwitchString/Enum (Task #25)

**目标**: 扩展条件选择和 Switch 变体

**引擎参考**: `K2Node_Select.h`, `K2Node_SwitchString.h`, `K2Node_SwitchEnum.h`

**Select 节点**:
- 类似三元运算符
- BSL 语法: `var result = condition ? valueA : valueB` (需要新增三元表达式)
- 或通过函数: `var result = Select(condition, valueA, valueB)`

**SwitchString**:
- BSL 中 switch 语句的字符串版本
- 编译器根据表达式类型自动选择 SwitchInteger/SwitchString/SwitchEnum
- 需要在 `CompileStatement` 的 Switch case 中添加类型检测

**SwitchEnum**:
- 类型为 Enum 时使用 `UK2Node_SwitchEnum`
- BSL 可能需要新增 Enum 类型支持 (`EType::Enum`)

**需要修改的文件**:
1. **EBlueprintNodeType** - 新增 `Select`, `Flow_SwitchString`, `Flow_SwitchEnum`
2. **NodeSpawner.cpp** - SpawnSelectNode, 扩展 SpawnFlowControlNode
3. **BSLTypes.h** - 可能新增 `EExpressionType::Ternary`
4. **BSLParser.cpp** - 解析三元表达式 `? :`
5. **BSLCompiler.cpp** - Switch 类型自动推断

---

### 3.7 P2: FormatText (Task #26)

**目标**: 支持文本格式化节点

**引擎参考**: `K2Node_FormatText.h`

**特殊点**: pin 数量根据格式字符串中的 `{placeholder}` 动态生成

**BSL 语法设计**:
```
var msg: string = format("Player {Name} has {HP} HP", Name=playerName, HP=health)
```
或字符串插值:
```
var msg: string = f"Player {playerName} has {health} HP"
```

**需要修改的文件**:
1. **EBlueprintNodeType** - 新增 `FormatText`
2. **NodeSpawner.cpp** - SpawnFormatTextNode
   - 解析格式字符串，动态创建 pin
3. **BSLTypes.h** - 可能新增 `EExpressionType::FormatString`
4. **BSLParser/Compiler** - 格式化表达式解析

---

### 3.8 P3: ConstructObject + LoadAsset (Task #27)

**目标**: 运行时对象创建和异步资源加载

**引擎参考**: `K2Node_ConstructObjectFromClass.h`, `K2Node_LoadAsset.h`

**ConstructObject**:
```
var widget: UserWidget = construct(MyWidgetClass)
```

**LoadAsset** (异步):
```
loadAsync("/Game/Textures/T_Icon", OnLoaded)
```
注意: LoadAsset 是 latent 节点，需要特殊的执行流处理。

---

## 4. 实现顺序和依赖关系

```
P0 (可并行)              P1 (依赖 P0)           P2 (依赖 P1)         P3 (依赖 P2)
#20 BreakStruct      ─┐
                      ├─→ #22 SpawnActor   ─┐
#21 Delegate         ─┤   #23 Timeline     ├─→ #25 Select/Switch ─┐
                      ├─→ #24 SetFields   ─┤   #26 FormatText   ─┼─→ #27 Construct/Load
                      └                    └                     └
```

## 5. 每个 Task 的标准工作流

遵循 MEMORY.md 中的强制规则：**实现 → Spec 审查 → 代码质量审查 → 修复 → 修复后再次审查 → 提交**

每个 Task 的具体步骤：

1. **阅读引擎 K2Node 源码** - 确认 pin 命名规则、特殊行为
   - 路径: `E:\Epic Games\UE_5.7\Engine\Source\Editor\BlueprintGraph\Classes\K2Node_XYZ.h`
2. **实现 NodeSpawner** - 添加 Spawn 函数和 Handler 注册
3. **实现 BSL 编译器** - 如需新语法，按 Lexer -> Parser -> Compiler 顺序
4. **编译验证** - UBT 编译
5. **功能测试** - 启动编辑器，用 `apply_blueprint_as_bsl` MCP 工具测试
6. **代码审查** - 使用 superpowers:requesting-code-review
7. **修复 + 再审查**
8. **提交**

## 6. 已知 Bug (待修复)

| Bug | 描述 | 优先级 |
|-----|------|--------|
| Task #19 | BlueprintGenerator::Extract() 使用错误的 CDO（父类 CDO 而非蓝图生成类 CDO），访问蓝图自定义变量时崩溃 | 独立修复，不阻塞本计划 |

## 7. 编译和测试命令

```bash
# 编译
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" \
  PluginsWarehouseEditor Win64 Development \
  "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload

# 启动编辑器 (后台)
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" \
  "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject"

# 等待 HTTP Server 就绪
curl -s http://localhost:8559/assetfactory/health

# MCP 测试工具
# - generate_assets: 创建蓝图
# - apply_blueprint_as_bsl: 应用 BSL 代码
# - extract_blueprint_as_bsl: 提取为 BSL (反编译)
# - extract_blueprint_graph: 提取节点图 JSON
```
