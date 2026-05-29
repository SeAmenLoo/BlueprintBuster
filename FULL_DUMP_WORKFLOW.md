# BlueprintBuster “FullDump JSON” 工具化流程（无 LLM）

目标：

- 生成 **兼容 BlueprintBuster 现有 dump schema** 的 JSON（`bp_translator.py` 可直接读取）。
- 在 dump 阶段尽可能把蓝图图结构降级/展开成可翻译的节点集合（至少展开 `UK2Node_MacroInstance`）。
- `json -> c++` 阶段不依赖 mcp；一旦 dump 内仍包含 Unsupported/MacroInstance，翻译器直接失败（不输出 TODO 注释）。

## 1. 现有 schema（Translator 读取字段）

`bp_translator.py` 当前读取的顶层字段：

- `blueprintName`
- `blueprintPath`
- `parentClassPath`
- `parentClassName`
- `isActorDerived`
- `components`
- `defaults`
- `eventTrees`
- `customFunctions`
- `unsupportedNodeCount`
- `totalNodeCount`

其中 `eventTrees[].event` 的节点结构要求：

- `kind`：`Event` / `CallFunction` / `Branch` / `Sequence` / `VariableGet` / `VariableSet` / `FunctionEntry`（等）
- `label`
- `function`（可选）
- `targetClass`（可选）
- `unsupported`（仅用于错误信息；不允许最终产物出现 `kind=Unsupported` 或 `kind=MacroInstance`）
- `next` / `true` / `false` 子数组

## 2. 总体策略（推荐）

把“dump 生成”分为两层：

1. **BaseDump（现有 BlueprintBuster Commandlet）**：负责组件、默认值、父类等静态信息，并给出一个初始 `eventTrees`（可能含 Unsupported/MacroInstance）。
2. **FullDumpPatch（Editor + unreal-mcp 驱动）**：在编辑器上下文里读取图的真实结构，执行确定性规则，生成“已展开/已降级”的 `eventTrees`，并回写到 base dump，得到最终 FullDump JSON。

这样能最大化复用 BlueprintBuster 现有能力，同时把“宏展开/复杂节点解析”放到 Editor 上下文完成。

## 3. FullDumpPatch 的输入输出

### 3.1 输入

- BaseDump JSON 文件路径（工程目录内）
- 目标蓝图资产路径（例如 `/Game/Blueprints/BP_Test.BP_Test`）

### 3.2 输出

- FullDump JSON：字段保持兼容，仅替换/覆盖：
  - `eventTrees`
  - `unsupportedNodeCount`
  - `totalNodeCount`
- 可选：输出一个 `fullDumpReport` 字段（不被 translator 消费），用于记录：
  - 展开了哪些宏（宏路径、递归深度）
  - 对哪些节点做了降级（规则名）
  - 如果失败：失败节点 class、guid、graph、pin 结构摘要

## 4. mcp 侧“无 LLM”执行方式

### 4.1 需要的 toolset

在 unreal-mcp 中加载：

- `toolset_registry.toolsets.core.programmatic.ProgrammaticToolset`
- `toolset_registry.toolsets.core.asset.AssetTools`

### 4.2 执行模式

通过 `ProgrammaticToolset.execute_tool_script` 执行一段固定 Python（不做任何推理/对话决策），脚本：

1. 读取 BaseDump JSON（磁盘）
2. `unreal.EditorAssetLibrary.load_asset()` 加载 Blueprint
3. 遍历 graphs / nodes / pins，按规则重建 eventTrees
4. 写回 FullDump JSON（磁盘）
5. 返回 `{ "ok": true, "output_path": "...", "stats": {...} }`

该脚本本质是“确定性导出器”，mcp 只是远程调用与数据回传通道。

当前仓库内提供的单蓝图 Patch 脚本：

- [full_dump_patch.py](file:///e:/Projects/UE/VRGame/git/PluginExample/Plugins/BlueprintBuster/Python/full_dump_patch.py)

备份策略（默认启用）：

- Patch 脚本会先在内容目录下创建蓝图备份（默认目录 `/Game/BlueprintBusterBackups`），并在备份资产上执行任何可能改图的操作。
- 可通过 `--no-backup` 禁用；或用 `--backup-root=/Game/YourFolder` 指定目录。

## 5. 规则层（需要覆盖的关键节点）

### 5.1 MacroInstance（UK2Node_MacroInstance）

目标：最终 FullDump JSON 中 **不出现** `kind=MacroInstance`。

推荐最低实现：

- 取得 `MacroNode->GetMacroGraph()`（宏图）
- 找到宏入口/出口 tunnel（`UK2Node_Tunnel`）
- 建立映射：
  - MacroInstance 外部 pins ↔ tunnel pins（优先按 PinName，再按 PinId/Guid 回退）
- 从入口 tunnel 的 exec 输出进入宏体，生成一段可被现有 translator 消费的 node chain：
  - `CallFunction` / `Branch` / `Sequence` / `VariableGet/Set`
- 递归限制：
  - 维护宏图路径栈（`/Game/...` + GraphName），遇到重复直接失败（循环检测）

如果宏体包含仍不可降级的节点：

- 直接失败（生成器返回错误并停止，不产出 C++）

### 5.2 其它未支持节点

策略：不输出 TODO，不“带结构的占位注释”，而是：

- 在 FullDumpPatch 阶段就把它们降级为可翻译集合；做不到就 **失败并输出报告**。

## 6. 翻译阶段（json -> c++）

`bp_translator.py` 已调整为：

- 一旦遇到 `kind=MacroInstance` 或 `kind=Unsupported`，直接抛异常并退出
- 不会在生成的 C++ 中留下“unsupported TODO 注释”

文件位置：

- [bp_translator.py](file:///e:/Projects/UE/VRGame/git/PluginExample/Plugins/BlueprintBuster/Python/bp_translator.py)

## 7. 单蓝图执行（建议落地命令序）

建议把单蓝图流程做成 3 个固定步骤，方便后续扩展到批量：

1. 生成 BaseDump（现有 BlueprintBuster Convert/Dump 逻辑）
2. 在 Editor 内执行 FullDumpPatch（mcp）生成 `patch.json`
3. 用 [merge_dump.py](file:///e:/Projects/UE/VRGame/git/PluginExample/Plugins/BlueprintBuster/Python/merge_dump.py) 合并得到 FullDump JSON
4. 运行 `bp_translator.py` 生成 C++

后续扩展批量时，只需要在第 1/2 步把“输入蓝图列表”改为遍历目录或资产过滤即可。

## 8. 与现有 Commandlet 集成

BlueprintBuster 的 `DumpBlueprintToJsonFile` 已增加 `bFailOnUnsupported` 参数，`BlueprintBusterCommandlet` / `BlueprintBusterConvertCommandlet` 支持 `-FullDump` 开关：

- 未指定 `-FullDump`：行为与以前一致（dump 中可能包含 unsupportedNodeCount）
- 指定 `-FullDump`：dump 文件仍会写出，但只要存在不支持节点就返回失败（转换流程中止，不会生成含 TODO 的 C++）
