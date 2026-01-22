# UECopilot TODO

## 已完成功能

### 布局算法改进 (LayoutEngine)
- [x] 使用 Sugiyama 分层算法布局执行流节点
- [x] 数据节点层级从消费者反推（整数层用于 Y 轴计算）
- [x] 数据节点 X 轴使用半层定位（每步退 0.5 层，更紧凑）
- [x] 子图分离（BeginPlay、Tick 等事件分开布局）
- [x] MinimizeCrossings 交叉最小化算法
- [x] 添加 FNodePlacementGrid 重叠检测数据结构

### BSL 编译器改进 (BSLCompiler)
- [x] 每次变量引用创建独立的 Variable Get 节点（不复用，避免长连线）
- [x] 类型转换节点自动插入（Int to Double）
- [x] 函数输出参数正确处理
- [x] If/Else 后续代码使用 Sequence 节点连接
- [x] OperandType 字段支持（区分 IntInt、DoubleDouble 等运算类型）

### 节点生成改进 (NodeSpawner)
- [x] 使用 OperandType 字段选择正确的数学运算函数
- [x] Pin 名称匹配改进（支持位置参数）
- [x] Pin 名称大小写不敏感匹配（execute vs Execute）
- [x] ForLoopWithBreak 宏节点支持

### 布局算法改进 (LayoutEngine) - 续
- [x] 跳过 Break pin 的 back-edge（避免循环依赖导致层级计算错误）

## 待完成功能

### 布局算法
- [x] 引脚顺序与节点Y位置对应（Sequence/Loop等多输出引脚节点）
- [ ] 进一步优化数据节点的 Y 轴位置（减少连线交叉）
- [ ] 考虑节点实际尺寸进行重叠检测
- [ ] 支持手动调整后的位置保持

### BSL 语言支持
- [x] For 循环语句 (`for i in start..end { body }`)
- [x] While 循环语句 (`while (condition) { body }`)
- [x] Break 语句（For 循环使用 ForLoopWithBreak 宏）
- [x] Continue 语句（终止当前迭代执行流）
- [ ] 数组操作
- [ ] 结构体支持
- [ ] 更多内置函数

### 测试
- [ ] 完整测试所有 BSL 测试用例（Simple, Medium, Complex, Whole）
- [ ] 边界情况测试
- [ ] 性能测试（大型蓝图）

## 已知问题

- 复杂蓝图中数据节点位置可能仍需手动微调
- 嵌套 if/else 的布局可能不够理想

## 参考资料

- [Dagre - Directed graph layout for JavaScript](https://github.com/dagrejs/dagre)
- [ELK - Eclipse Layout Kernel](https://eclipse.dev/elk/)
- [Layered graph drawing - Wikipedia](https://en.wikipedia.org/wiki/Layered_graph_drawing)
- [The Sugiyama Method](https://blog.disy.net/sugiyama-method/)
