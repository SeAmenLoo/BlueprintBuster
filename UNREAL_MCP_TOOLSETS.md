# Unreal-MCP Toolsets 能力总览

本文档基于 unreal-mcp 提供的 `list_toolsets` / `describe_toolset` 输出整理，目的在于把“有哪些 toolset”以及“它们能做什么”快速映射到可落地的 Unreal Editor 自动化/资产操作能力。

## 1. 入口工具（unreal-mcp 自身）

unreal-mcp 服务器自身提供 3 个入口工具：

- `list_toolsets`：列出所有可用 toolset（名称 + 简述）
- `describe_toolset(toolset_name)`：返回该 toolset 的详细信息（包含所有 tool 名称、描述、输入 schema）
- `load_toolset(toolset_name)`：把某个 toolset 的 tools 注册为“原生 MCP tools”
  - 注意：加载后的 tools 从“下一轮对话”开始可用（不能在同一轮里 load 后立刻调用）

## 2. 数据与调用约定（通用）

- UObject / UClass / UStruct 等对象引用以 `{ "refPath": "..." }` 形式序列化并在 tool 间传递。
- 多数“查询类”工具会返回 JSON 字符串（例如测试列表、日志、Dataflow 图结构），一般需要调用方自行解析。
- 涉及“创建/删除/重命名/写文件/修改工程范围配置”的工具，通常在描述里明确标注需要用户授权；在自动化脚本中建议增加二次确认策略。

## 3. Toolset 清单（来自 list_toolsets）

以下清单为当前环境下 unreal-mcp 可见的全部 toolset（名称保持原样，便于直接用于 `describe_toolset` / `load_toolset`）。

### 3.1 编辑器状态 / AI 助手上下文 / 日志

- `ToolsetRegistry.EditorAppToolset`
  - 编辑器状态查询与控制：内容浏览器路径、选择集、视口相机、可见 actor、控制台变量、资产/编辑器截图、打开资产编辑器等
  - 典型用途：自动化截图/取证、批量定位资产、视口相机对齐、脚本化选择与聚焦
- `AIAssistant.AIAssistantToolset`
  - 获取工程上下文、AI 助手当前停靠到哪个资产编辑器（以及当前图、选中节点）
  - 典型用途：做“上下文感知”的编辑器自动化（例如只操作当前正在编辑的 Blueprint 图）
- `ToolsetRegistry.LogsToolset`
  - 读取输出日志、查询/设置 log category verbosity、按正则过滤日志
  - 典型用途：自动化测试后抓取错误、调试脚本执行结果、CI/自动化回归采集
- `ToolsetRegistry.AgentSkillToolset`
  - 管理工程内 AgentSkill（列出、读取、创建、更新）
  - 典型用途：把高频工作流固化为可复用 skill（需要显式授权）

### 3.2 自动化测试

- `AutomationTestToolset.AutomationTestToolset`
  - 测试发现、筛选、执行与结果获取（封装 IAutomationControllerManager）
  - 推荐工作流：`DiscoverTests` → `ListTests` → `RunTests` → `GetTestStatus/GetTestResults` → `StopTests`（必要时）

### 3.3 工程/插件体系

- `GameFeaturesToolset.GameFeaturesToolset`
  - 列出 Game Feature Plugins、查找/加载 GameFeatureData、读取 actions、创建 Game Feature Plugin（需授权）
  - 典型用途：自动生成模块化玩法插件骨架、审计某个 GFD 的 actions 配置

### 3.4 玩法系统（GAS / GameplayTags / GameplayCues）

- `GameplayTagsToolset.GameplayTagsToolset`
  - 列表/查询 tag、查找引用、添加/删除/重命名 tag（需授权）
  - 典型用途：标签治理（查孤儿/查引用/批量重构命名）
- `GASToolsets.AttributeSetToolset`
  - 发现 AttributeSet 子类与属性列表
  - 典型用途：自动生成属性文档、校验属性命名约定、做 runtime inspector 的前置枚举
- `GASToolsets.AbilitySystemInspectorToolset`
  - 运行时检查某 Actor 的 ASC：GrantedAbilities、ActiveEffects、ActiveTags、AttributeValues
  - 典型用途：PIE 调试与自动化验证（效果叠加/数值是否符合预期）
- `GASToolsets.GameplayCueToolset`
  - 管理 GameplayCue tags、查 notify 资产、查无 notify 的 cue、执行 cue 预览、创建 cue notify 资产（部分需授权）

### 3.5 Niagara（特效）

- `NiagaraToolsets.NiagaraToolset_Info`
  - Niagara 枚举信息、资产发现路径等“信息导航”
- `NiagaraToolsets.NiagaraToolset_Component`
  - 面向运行时 NiagaraComponent：设置系统、设置/读取 user variables（对组件实例做覆写）
- `NiagaraToolsets.NiagaraToolset_Blueprint`
  - Niagara 与 Blueprint 集成：把系统封装成可复用的 Blueprint FX Actor（偏内容生产）
- `NiagaraToolsets.NiagaraToolset_System`
  - 面向编辑器的 NiagaraSystem 深度编辑：System/Emitter/Module 创建与修改、拓扑与 schema 探索、属性读写

### 3.6 物理与骨骼

- `PhysicsToolsets.PhysicsAssetToolset`
  - PhysicsAsset 创建/管理（碰撞体、约束等）
- `toolset_registry.toolsets.core.skeletal_mesh.SkeletalMeshTools`
  - SkeletalMesh 检查与修改：材质、骨骼层级、Socket 等

### 3.7 Slate / UMG（编辑器 UI 自动化与 UMG 内容生产）

- `SlateInspectorToolset.SlateInspectorToolset`
  - Playwright 风格的 Slate UI 自动化：窗口管理、Observe/Snapshot、Click/Type/Hover/Drag、截图、WaitFor 等
  - 推荐工作流：`Windows(list)` → `Observe(ref)` → `Snapshot(ref)` → 基于 ref 执行 `Click/Type/...` → `Unobserve`
- `UMGToolSet.UMGToolSet`
  - UMG WidgetBlueprint 结构化编辑：列出/创建/移动/删除/重命名 widget、设置 named slot、列出可用 widget class 等
  - 强约束工作流：对每个 widget/slot 的属性读写必须配合 `ObjectTools.list_properties` → `get_properties` → `set_properties`

### 3.8 WorldConditions / StateTree / Conversation / BehaviorTree（资产检查类）

- `WorldConditionsToolset.WorldConditionTools`
  - 以“人类可读文本”形式解释 WorldCondition query/condition 结构
- `state_tree_toolset.toolsets.state_tree.StateTreeTools`
  - StateTree 资产检查（ST）
- `conversation_toolset.toolsets.conversation.ConversationTools`
  - Conversation Graph（对话数据库）资产检查
- `aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools`
  - BehaviorTree（BT）资产检查

### 3.9 动画（Sequencer / Control Rig）

- `animation_toolset.toolsets.controlrig.ControlRigTools`
  - ControlRig 资产创建与编辑：层级、图、node/pin 操作等
- `animation_toolset.toolsets.sequencer.SequencerTools`
  - Sequencer 核心：序列生命周期、播放控制、属性、绑定、轨道/段、组织结构、事件轨、锁定等
- `animation_toolset.toolsets.keyframing.SequencerKeyframingTools`
  - 关键帧：通道 key 增删查、插值、Curve Editor、选择管理等
- `animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools`
  - Sequencer 内 Control Rig 动画：控制值读写/打 key、烘焙、space switching、镜像、layer 管理等
- `animation_toolset.toolsets.outliner.SequencerOutlinerTools`
  - Outliner 结构检查与状态控制（mute/solo/lock/pin 等）
- `animation_toolset.toolsets.conditions.SequencerConditionTools`
  - 轨道/段 runtime conditions（平台/导演蓝图/组条件等）
- `animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools`
  - 自定义绑定类型管理（possessable/spawnable/custom 的转换与查询等）
- `animation_toolset.toolsets.import_export.SequencerImportExportTools`
  - Sequencer 动画导入导出（FBX、AnimSequence、联动更新等）

### 3.10 ToolsetRegistry Core（资产、对象、场景、材质等通用基础能力）

这些 toolset 往往是“其他高层 toolset”的底座，建议优先熟悉：

- `toolset_registry.toolsets.core.object.ObjectTools`
  - 查询类与实例、列出/读取/写入属性、搜索子类（很多系统要求先 list_properties 再 set）
- `toolset_registry.toolsets.core.asset.AssetTools`
  - 资产发现/加载/保存/复制/移动/删除、依赖/引用、读写工程目录内文本文件
- `toolset_registry.toolsets.core.scene.SceneTools`
  - 关卡加载、放置/删除 actor、outliner folder 管理、world trace
- `toolset_registry.toolsets.core.actor.ActorTools`
  - actor transform/label/tag、组件增删查、父子组件关系、bounds
- `toolset_registry.toolsets.core.blueprint.BlueprintTools`
  - Blueprint 结构与图编辑（变量、pin 连接、dispatcher、重设父类等）
- `toolset_registry.toolsets.core.material.MaterialTools`
  - Material 资产创建、表达式节点增删查、连线、参数组、layout、recompile
- `toolset_registry.toolsets.core.material_instance.MaterialInstanceTools`
  - MaterialInstanceConstant 创建与修改（参数覆写等）
- `toolset_registry.toolsets.core.static_mesh.StaticMeshTools`
  - StaticMesh 检查与修改（LOD、材质、构建设置等）
- `toolset_registry.toolsets.core.texture.TextureTools`
  - Texture 相关操作（导入后调整、参数修改等）
- `toolset_registry.toolsets.core.data_table.DataTableTools`
  - DataTable 读写、结构管理（适合做配置表自动化）
- `toolset_registry.toolsets.core.curve_table.CurveTableTools`
  - CurveTable 创建与编辑
- `toolset_registry.toolsets.core.data_asset.DataAssetTools`
  - DataAsset 创建与编辑（面向 UPrimaryDataAsset / UObject 配置）
- `toolset_registry.toolsets.core.string_table.StringTableTools`
  - StringTable 创建与编辑（本地化/文本资源）
- `toolset_registry.toolsets.core.primitive.PrimitiveTools`
  - 给 actor 添加基础几何组件（快速搭建场景占位）
- `toolset_registry.toolsets.core.programmatic.ProgrammaticToolset`
  - 通过受限 Python 脚本批量调用 toolset API（减少往返、适合批处理）

## 4. 能力组合建议（面向落地场景）

### 4.1 “批量内容修改”最小组合

- 发现/加载资产：`AssetTools.find_assets` + `AssetTools.load_asset`
- 查询/修改属性：`ObjectTools.list_properties` + `ObjectTools.get_properties` + `ObjectTools.set_properties`
- 保存：`AssetTools.save_assets`

### 4.2 “关卡批处理”组合

- 放置/查找/删除 actor：`SceneTools.add_to_scene_from_asset` / `SceneTools.find_actors` / `SceneTools.remove_from_scene`
- 变换与组件：`ActorTools.set_actor_transform` / `ActorTools.add_component`
- 视口辅助：`ToolsetRegistry.EditorAppToolset.FocusOnActors` / `GetCameraTransform` / `SetCameraTransform`

### 4.3 “UI（UMG）内容生产”组合

- 结构操作：`UMGToolSet.UMGToolSet.*`
- 属性读写：`ObjectTools.*`
- 如果需要自动点编辑器 UI：叠加 `SlateInspectorToolset.SlateInspectorToolset` 做 UI 驱动（例如打开设计器、切换面板等）

### 4.4 “自动化验证”组合

- 执行测试：`AutomationTestToolset.AutomationTestToolset.*`
- 抓日志：`ToolsetRegistry.LogsToolset.GetLogEntries`
- UI 自动化（需要时）：`SlateInspectorToolset.SlateInspectorToolset.*`

## 5. 如何获取更细粒度的 tool 列表与 schema

当你需要某个 toolset 的完整工具列表、每个工具的输入输出字段时：

1. 调用 `describe_toolset(toolset_name)` 获取该 toolset 的 tools 与 JSON schema
2. 如果你希望把 tools 注册成“可直接调用的 MCP 工具”，再调用 `load_toolset(toolset_name)`（下一轮开始可用）

