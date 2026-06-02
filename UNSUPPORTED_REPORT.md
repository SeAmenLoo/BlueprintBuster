# BlueprintBuster Unsupported 处理记录

此文档用于持续维护 BlueprintBuster 在 FullDump / 转换过程中遇到的 Unsupported 节点与原因，并记录可落地的解决方案（实现建议或已实现状态）。每次新增 Unsupported 时，将对应条目补充到“通用问题清单”或新增“案例记录”。

## Dump.json 中的 Unsupported 表现
- `unsupportedNodeCount` > 0
- `eventTrees/customFunctions` 中出现 `kind: "Unsupported"`
  - `label`：节点（或函数）名称
  - `unsupported`：失败原因（应当尽量做到确定性、可定位）

## 通用问题清单（原因 → 解决方案）

### 1) 委托绑定：`K2Node_AddDelegate`
- 典型原因
  - `AddDelegate bound node is not CreateDelegate`
- 解决方案
  - Dump 解析阶段：允许 delegate pin 连接到更多“可生成 handler 的节点类型”，除 `UK2Node_CreateDelegate` 外，至少补齐：
    - `UK2Node_Event`（其 `OutputDelegate` 输出 pin 可直接用于绑定）：handler 名可取 `GetFunctionName()` 或自定义事件的 `CustomFunctionName`
  - 生成阶段：保持 `Target->Delegate.AddDynamic(this, &{ClassName}::{Handler})`，并对非 `this` handler object 明确 Unsupported（fail-fast）

### 2) Pin 默认值为资源对象：`DefaultObject` / 资源路径
- 典型原因
  - `Pin has asset default object '/Game/.../Asset.Asset' which is not supported`
- 解决方案
  - 表达式解析（pin→C++）阶段支持资源默认对象：
    - 生成 `LoadObject<T>(nullptr, TEXT("/Game/.../Asset.Asset"))` 或 `StaticLoadObject(...)` 形式的表达式
  - 若目标类型可推导（Object/Class pin 的 `PinSubCategoryObject`），用该类型作为模板参数 `T`

### 3) impure `CallFunction` 的 ReturnValue 被当作表达式使用
- 典型原因
  - `VariableSet value cannot be resolved: Pure CallFunction cannot be inlined: CallFunction is not pure`
- 解决方案
  - Dump lowering 需要支持“语句级”的返回值承接（而不是强制把 RHS 变成表达式）：
    - 方案 A：在 `CallFunction` 节点记录一个 `returnValueTemp`（或直接记录“下一节点是对某变量的赋值”），Translator 输出 `Var = Func(...);`
    - 方案 B：当 `VariableSet` 的 ValuePin 链接到前序 impure CallFunction 的 ReturnValue 时，将 `VariableSet` 直接降级为对该 CallFunction 的“赋值语句”输出（跳过单独的 ValueExpr 解析）

### 4) `K2Node_VariableSet` 的输出 pin 参与表达式
- 典型原因
  - `Output pin node class K2Node_VariableSet is not supported for expression lowering`
- 解决方案
  - 表达式解析阶段为 `UK2Node_VariableSet` 增加特判：
    - 当使用其 “Output_Get” 输出 pin 时，直接解析为该变量名（等价于 Get）

### 5) Unsupported 原因信息为空 / Struct 等默认值不支持
- 典型原因
  - `Pure CallFunction argument 'X' cannot be resolved:`（原因为空）
- 解决方案
  - 默认值解析失败时必须填充 `OutFailureReason`（例如：`Pin default value category 'Struct' is not supported`），保证 dump 可定位
  - 对常见 Struct（如 `FVector`/`FLinearColor`/`FRotator`/`FTransform`）补齐：
    - 由 `MakeStruct/BreakStruct` 或等价纯函数节点生成的表达式 lowering

### 6) FlipFlop 的数据输出 pin（例如 `IsA`）参与表达式
- 典型原因
  - `Output pin node class K2Node_MacroInstance is not supported for expression lowering`
- 解决方案
  - 在表达式解析阶段为 `FlipFlop` 宏实例补齐 bool 输出 pin 的 lowering：
    - `IsA` → `(!bFlipFlop_<GuidDigits>)`

### 7) “Validated Get / IsValid” 变体的 `VariableGet` 没有被降级为判空分支
- 典型表现
  - 原蓝图存在 `Is Valid / Is Not Valid` 执行分支，但生成 C++ 直接使用对象（例如直接 `Trigger->...`）
- 解决方案
  - Dump 解析阶段识别带 Exec pins 的 `UK2Node_VariableGet`，降级为：
    - `kind: Branch`
    - `condition: IsValid(<VarName>)`
    - `true/false` 分支分别接 `Then/Else` exec 链

## 案例记录

### BP_Light（/Game/DemoTemplate/Blueprint/BP_Light.BP_Light）
- dump：`BB/BP_Light_dump.json`
- 当前 Unsupported 节点（dump.json 中的 `eventTrees`）
  - `AddDelegate`
    - 原因：`AddDelegate bound node is not CreateDelegate`
    - 对应解决：见“1) 委托绑定”
  - `CreateDynamicMaterialInstance`
    - 原因：`Parent` pin 默认值是资源对象 `/Game/DemoTemplate/Blueprint/M_Light.M_Light`
    - 对应解决：见“2) Pin 默认值为资源对象”
  - `MID_LIght`（VariableSet）
    - 原因：RHS 来自 impure `CreateDynamicMaterialInstance` 的 ReturnValue，无法 inline 为表达式
    - 对应解决：见“3) impure CallFunction 的 ReturnValue 被当作表达式使用”
  - `SetMaterial`
    - 原因：`Material` 参数从 `K2Node_VariableSet` 的输出 pin 取值，表达式解析不支持
    - 对应解决：见“4) K2Node_VariableSet 的输出 pin 参与表达式”
  - `SetVectorParameterValue`
    - 原因：`bPickA` 来自 `FlipFlop` 的 bool 输出 pin（MacroInstance），表达式解析不支持
    - 对应解决：见“6) FlipFlop 的数据输出 pin（例如 IsA）参与表达式”
