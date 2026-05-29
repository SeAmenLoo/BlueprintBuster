# FullDumpPatch Lowering 规则（S2）

目标：在生成 FullDump JSON 时，通过确定性规则把蓝图中的高层节点“降级/展开”为 BlueprintBuster `bp_translator.py` 可消费的节点集合；若无法降级则直接失败并输出 `fullDumpReport`（不生成含 TODO 的 C++）。

## 1. 可消费节点集合（当前 Translator 认可）

- `Event`
- `CallFunction`
- `Branch`
- `Sequence`
- `VariableGet`
- `VariableSet`
- `FunctionEntry`（仅作为无输出占位/终止节点使用；不会生成代码）

## 2. S2 范围（来自 UNSUPPORTED_NODES.md）

优先覆盖以下节点族（按出现频率与实现价值排序）：

- `K2Node_MacroInstance`
- `K2Node_CallFunction`（参数与返回值结构）
- `K2Node_Switch*`
- `K2Node_Select`
- `K2Node_ForLoop*` / `K2Node_WhileLoop`
- `K2Node_Delay` / `K2Node_Timeline`
- `K2Node_DynamicCast`
- `K2Node_SpawnActor*`
- `K2Node_MultiGate` / `K2Node_DoN`

## 3. 总体实现路线

### 路线 A（优先尝试）：调用引擎“Expand Node / Compile Lowering”

如果 Unreal Python / Editor API 提供“把节点展开成更底层节点”的接口，则采用：

1. 复制目标图到临时图（避免污染原蓝图）
2. 对目标节点执行 Expand（宏、循环、switch、delay 等）
3. 在展开后的图上执行 exec-chain 提取，生成 `eventTrees`

优点：覆盖面大、可跟随引擎版本演进；缺点：依赖 Editor API 可用性与稳定性。

### 路线 B（兜底）：纯图遍历 + 规则降级

当无法直接 Expand 时，按节点语义做最小降级：

- 输出可消费节点集合
- 数据流（条件/参数）暂不强行求值，必要时在 CallFunction 参数层面补充结构字段供后续翻译器使用
- 无法保证语义等价的节点直接失败

## 4. 节点级规则（计划与当前状态）

### 4.1 MacroInstance（进行中）

目标：消灭 `MacroInstance`，把宏体 exec-flow 内联到 `eventTrees`。

最小规则：

- 通过宏图的入口 `K2Node_Tunnel` 找到首个 exec 输出
- 在宏图内沿 exec 链提取可消费节点
- 将宏体末端与外部 next exec 相连
- 宏递归/互相引用检测：`macroStack` 去重

当前实现位置：

- Dump 阶段（C++ parser）：[BlueprintBusterParsers.cpp](file:///e:/Projects/UE/VRGame/git/PluginExample/Plugins/BlueprintBuster/Source/BlueprintBuster/Private/BlueprintBusterParsers.cpp)

### 4.2 CallFunction 参数/返回值（进行中）

目标：为 `json -> c++` 提供足够信息做到“无 TODO 的可编译调用”：

- 输入 pin 列表：名称、类型、是否连接、默认值
- 返回值 pin 与 out 参数 pin 列表
- 连接来源（linked pin → owning node）用于后续数据流求值/表达式生成

当前已实现（最小可编译调用）：

- Dump JSON 增量字段（兼容现有 schema）：
  - `targetClassName`、`targetExpr`、`args[]`（`{name, expr}`）
  - `condition`（Branch）
  - `valueExpr`（VariableSet）
- 若无法解析表达式：节点会被标记为 `Unsupported`，配合 `-FullDump` 直接失败，不生成含占位文本的 C++。

### 4.3 Switch / Select（计划）

优先尝试路线 A（Expand）。若不可用，则失败并报告，直到实现明确可验证的降级规则。

### 4.4 ForLoop / WhileLoop / DoN / MultiGate（计划）

这类节点通常涉及隐式状态与控制流图，优先走路线 A。

### 4.5 Delay / Timeline（计划）

如果 Expand 结果能稳定表达为“CallFunction + continuation”，则在 dump 阶段允许输出；否则失败并报告（因为 C++ 语义等价实现需要明确 Latent/Timer 方案）。

### 4.6 DynamicCast / SpawnActor（计划）

可用路线 B：

- DynamicCast → `Cast<>()` + 分支
- SpawnActor → `GetWorld()->SpawnActor<>()`（需参数与 class/path 信息）

但仍依赖参数结构导出（见 4.2）。

## 5. 失败与报告

任何节点无法降级时：

- FullDumpPatch 返回失败（非 0）
- `out_patch.json` 仍会写出，并包含 `fullDumpReport.errors[]`
- `bp_translator.py` 不会生成 C++（或上层流程直接中止）
