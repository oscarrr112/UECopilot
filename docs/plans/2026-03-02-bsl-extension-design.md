# BSL 扩展设计文档

**日期**：2026-03-02
**状态**：已审批，待实现

---

## 背景与目标

BSL（Blueprint Script Language）是 UECopilot 中 AI 生成 Blueprint 的中间语言。
当前架构中存在两条路径：JSON schema 路和 BSL 路。
**目标：以 BSL 作为唯一 AI 接口，JSON 只作为内部 IR。**

原因：
- AI 生成代码语言的稳定性远高于生成 JSON 图结构
- BSL 更紧凑，token 消耗少约 50%
- 支持未来双向通路（正向编译 + 反向反编译已有 Blueprint）

---

## 整体架构

```
AI → BSL 文本
        ↓ Lexer + Parser
     BSL AST (FBlueprint)
        ↓ Compiler（含 UE 反射查询 pin 名称）
     FBlueprintData（JSON IR，内部使用）
        ↓ NodeSpawner / AIBlueprintFactory
     UEdGraph（实际 Blueprint 节点图）

     反向通路（新增）：
     UEdGraph → BlueprintDecompiler → BSL AST → BSLEmitter → BSL 文本
```

---

## 函数 Pin 名称解析

**决策：运行时使用 UE 反射动态解析。**

编译 `FunctionCall` 表达式时：
1. 用 `FindObject<UFunction>` 或 `FindFunctionByName` 查找目标函数
2. 遍历 `UFunction::PropertyLink` 获取参数名列表
3. 按位置将 BSL 参数映射到实际 pin 名（而非 `Arg0`/`Arg1`）
4. `self.Func(args)` → 自动生成 `Target` pin 连接到 self
5. 静态函数不需要 Target pin

---

## 功能范围（按优先级）

### Tier 1 — 修复现有缺口（语法已有，编译器未实现）

| 功能 | BSL 语法 | 目标节点 | 当前状态 |
|------|---------|---------|---------|
| While 循环 | `while (cond) {}` | `Flow_WhileLoop` | 解析有，编译 TODO |
| For 循环 | `for i in 0..10 {}` | `Flow_ForLoop` | 解析有，编译 TODO |
| ForEach 循环 | `foreach item in arr {}` | `Flow_ForEachLoop` | 解析有，编译 TODO |
| 函数内局部变量 | `var x: int = 0` | 函数图本地变量 | TODO 注释 |
| Cast 表达式 | `Cast<PlayerCharacter>(actor)` | `Cast` 节点 | 解析有，编译无 |
| Self 表达式 | `self` 关键字 | self 引用 | 解析有，编译无 |
| 多返回值赋值 | `(a, b) = Func()` | 多 pin 连接 | 解析有，编译无 |
| 取反运算 | `-x` | `Multiply(x, -1)` | 注释 "not implemented" |
| 函数 pin 名称 | 所有函数调用 | 正确 pin 名 | 全部 Arg0/Arg1（错误）|

### Tier 2 — 新增语言特性（需要改语法 + 改编译器）

**Switch/case：**
```bsl
switch (Level) {
  case 1: { SpawnEnemies(5) }
  case 2: { SpawnEnemies(10) }
  default: { SpawnEnemies(20) }
}
```
→ `Flow_Switch` / `SwitchInteger` / `SwitchEnum` 节点

**Struct 字面量：**
```bsl
var loc: Vector = Vector(100.0, 0.0, 50.0)
var rot: Rotator = Rotator(0.0, 90.0, 0.0)
var tf: Transform = Transform(Vector(0,0,0), Rotator(0,0,0), Vector(1,1,1))
```
→ `MakeStruct` 节点

**组件方法链（最高频使用）：**
```bsl
self.Mesh.SetRelativeLocation(Vector(0, 0, 100))
self.Mesh.SetVisibility(true)
var comp: StaticMeshComponent = self.Mesh
```
→ 先生成 GetComponent 节点，再将其 ReturnValue 作为 Target pin

**数组操作：**
```bsl
Enemies.Add(newEnemy)
Enemies.Remove(oldEnemy)
var count: int = Enemies.Length()
var first: Actor = Enemies[0]
Enemies[0] = replacedActor
```
→ `Array_Add`、`Array_Remove`、`Array_Length`、`Array_Get`、`Array_Set` 节点

### Tier 3 — 逃生舱语法

用于 Timeline、Delegate 等无法用 BSL 结构化表达的节点：

```bsl
event BeginPlay {
  PrintString("Start")

  // Timeline 等复杂节点直接透传给 NodeSpawner
  @node("Timeline", { TrackName: "FadeIn", Length: 1.5 })

  PrintString("End")
}
```

AST 新增 `EStatementType::RawNode`，携带节点类型字符串和原始参数 JSON。

### Tier 4 — 反向通路（读取已有 Blueprint）

**BSLEmitter**（新增 `BSLEmitter.h/cpp`）：
- 输入：`BSL::FBlueprint` AST
- 输出：BSL 文本字符串
- 递归遍历 AST 节点，输出对应 BSL 语法
- 处理缩进、换行、操作符优先级

**BlueprintDecompiler**（新增 `BlueprintDecompiler.h/cpp`）：
- 输入：`UBlueprint*`
- 输出：`BSL::FBlueprint` AST
- 核心算法：
  1. 从 Event/FunctionEntry 节点出发，沿 exec pin 追踪执行流
  2. 线性执行链 → 顺序语句列表
  3. `K2Node_IfThenElse` → `FStatement(If)`
  4. ForLoop/ForEach macro → `FStatement(For/ForEach)`
  5. 数据流节点（非 exec）→ 递归构建 `FExpression` 树
  6. 无法识别的节点 → `FStatement(RawNode)` 逃生舱

**新增 HTTP 端点 / MCP 工具**：`extract_blueprint_as_bsl`

---

## 需要新增/修改的 AST 类型

```cpp
// BSLTypes.h 新增

enum class EStatementType : uint8 {
  // 现有...
  Switch,        // switch (x) { case ... }
  RawNode,       // @node("Timeline", {...})
};

struct FSwitchCase {
  TSharedPtr<FExpression> Value;  // nullptr = default
  TArray<TSharedPtr<FStatement>> Body;
};

// FStatement 新增字段：
TArray<FSwitchCase> SwitchCases;      // for Switch
FString RawNodeType;                   // for RawNode
TSharedPtr<FJsonObject> RawNodeParams; // for RawNode

// FExpression 新增类型：
// EExpressionType::StructLiteral  → struct 字面量
// FString StructTypeName;
// TArray<TSharedPtr<FExpression>> StructFields;
```

---

## 不在本次范围内

- Delegate 绑定/解绑（低频，可用 `@node` 逃生舱）
- Timeline 轨道曲线编辑（需要手动在编辑器操作）
- Interface 函数调用（后续 Tier 5）
- 语法风格改为 TypeScript/C# 风格（可选 polish，不是当务之急）

---

## 实现顺序建议

1. **Tier 1 第一批**：函数 pin 反射解析 + While/For/ForEach 循环编译
2. **Tier 1 第二批**：局部变量、Cast、Self、多返回赋值、取反
3. **Tier 2**：Struct 字面量 → 组件链式调用 → Switch/case → 数组操作
4. **Tier 3**：`@node` 逃生舱语法
5. **Tier 4**：BSLEmitter → BlueprintDecompiler → MCP 端点
