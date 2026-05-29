# 📋 BlueprintBuster — 未支持节点类型完整清单

> 本文档详细列举了BlueprintBuster目前**未支持**的Blueprint节点类型，分析其使用场景、转换难度、以及完整的实现方案。

**文档版本：** 1.1  
**最后更新：** 2026-05-29  
**覆盖范围：** UE5.7+ 所有常用K2Node节点类型

---

## 目录

1. [快速概览](#快速概览)
2. [完全未支持的节点](#完全未支持的节点)
3. [部分支持的节点](#部分支持的节点)
4. [实现优先级](#实现优先级)
5. [详细实现方案](#详细实现方案)

---

## 快速概览

### 支持状态统计

| 状态 | 数量 | 完成度 |
|------|------|--------|
| ✅ 完全支持 | 7 | 100% |
| 🟡 部分支持 | 3 | 30-60% |
| ❌ 完全未支持 | **28+** | 0% |
| **总计** | **38+** | **~30%** |

---

## 完全未支持的节点

### 第一类：数据处理节点（11个）

#### 1. **UK2Node_Switch** — Switch/Select语句
- **用途：** 多分支条件判断（类似C++ switch）
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** TODO
- **转换难度：** 🟢 低
- **预计工时：** 2-3天

**问题：** 需识别switch条件表达式和各分支目标

**示例：**
```blueprint
Switch (DialogueState)
  → Case "Idle": ...
  → Case "Active": ...
  → Case "Complete": ...
  → Default: ...
```

**目标C++代码：**
```cpp
switch (DialogueState) {
    case 0: // Idle
        // TODO: implement Idle branch
        break;
    case 1: // Active
        // TODO: implement Active branch
        break;
    // ...
}
```

---

#### 2. **UK2Node_MultiGate** — Multi-Gate/Flip-Flop
- **用途：** 顺序执行多个分支（round-robin）
- **使用频率：** ⭐⭐ 中等偏低
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 3-4天

**问题：** 需维护内部状态（当前执行分支索引）

**示例：**
```blueprint
MultiGate (Num=3)
  → Output 0: ...
  → Output 1: ...
  → Output 2: ...
  [每次调用循环到下一个输出]
```

**目标C++代码：**
```cpp
static int32 MultiGateIndex = 0;
switch (MultiGateIndex % 3) {
    case 0: /* ... */ break;
    case 1: /* ... */ break;
    case 2: /* ... */ break;
}
MultiGateIndex++;
```

---

#### 3. **UK2Node_Select** — Select (Pin)/三元条件
- **用途：** 条件数据选择（类似C++三元运算符）
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** TODO
- **转换难度：** 🟢 低
- **预计工时：** 1-2天

**问题：** 数据节点，非执行节点；需特殊处理

**示例：**
```blueprint
Select
  Condition: bIsActive
  True: 100
  False: 0
  → Output: int
```

**目标C++代码：**
```cpp
int32 Value = bIsActive ? 100 : 0;
```

---

#### 4. **UK2Node_ForLoop** — For Loop
- **用途：** 循环执行（固定次数）
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 3-4天

**问题：** 需处理Loop Body、Loop Complete、Index输出

**示例：**
```blueprint
ForLoop
  First Index: 0
  Last Index: 10
  → LoopBody (with Index)
  → LoopComplete
```

**目标C++代码：**
```cpp
for (int32 Index = 0; Index <= 10; ++Index) {
    // Loop body here
    // TODO: implement loop logic with Index
}
// Loop complete
```

---

#### 5. **UK2Node_ForLoopWithBreak** — For Loop with Break
- **用途：** 可中断的循环
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 3-4天

**问题：** 需识别Break条件

---

#### 6. **UK2Node_WhileLoop** — While Loop
- **用途：** 条件循环
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 3-4天

**问题：** 需识别循环条件表达式

---

#### 7. **UK2Node_DoN** — Do N Times
- **用途：** 触发N次后执行分支
- **使用频率：** ⭐⭐ 中等偏低
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 2-3天

**问题：** 需维护计数器状态

---

#### 8. **UK2Node_Timeline** — Timeline
- **用途：** 动画曲线时间轴
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** TODO
- **转换难度：** 🔴 高
- **预计工时：** 7-10天

**问题：** Timeline是完整的子资源（曲线、轨道、时间标记），需深度导出

**替代方案：** 使用 `FTimerManager` 或 `Tween` 库

---

#### 9. **UK2Node_Delay** — Delay
- **用途：** 延迟执行
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** TODO
- **转换难度：** 🟢 低
- **预计工时：** 1-2天

**问题：** 简单替换为 `GetWorld()->GetTimerManager().SetTimer()`

**示例：**
```blueprint
Delay (2.0 seconds)
```

**目标C++代码：**
```cpp
GetWorld()->GetTimerManager().SetTimer(
    DelayHandle,
    this,
    &AMyClass::OnDelayComplete,
    2.0f,
    false
);
```

---

#### 10. **UK2Node_SpawnActor** — Spawn Actor
- **用途：** 运行时生成Actor
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** 部分TODO
- **转换难度：** 🟢 低
- **预计工时：** 1-2天

**问题：** 需识别Actor类、Transform、Owner参数

**示例：**
```blueprint
SpawnActor
  Class: BP_Enemy
  Transform: SpawnTransform
  Owner: this
```

**目标C++代码：**
```cpp
if (GetWorld()) {
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    AEnemy* NewEnemy = GetWorld()->SpawnActor<AEnemy>(
        ABP_Enemy::StaticClass(),
        SpawnTransform
    );
    // TODO: verify spawn parameters
}
```

---

#### 11. **UK2Node_DynamicCast** — Cast (动态转换)
- **用途：** 运行时类型检查与转换
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** 部分TODO
- **转换难度：** 🟢 低
- **预计工时：** 1-2天

**问题：** 需识别目标类型、输入对象

**示例：**
```blueprint
Cast to Character
  Object: ActorRef
  → Cast (Character output)
  → Failed (null output)
```

**目标C++代码：**
```cpp
if (ACharacter* CastedCharacter = Cast<ACharacter>(ActorRef)) {
    // Use CastedCharacter
} else {
    // Cast failed
}
```

---

### 第二类：函数/调用节点（8个）

#### 12. **UK2Node_ConstructObjectFromClass** — Construct Object from Class
- **用途：** 从Class对象构造UObject实例
- **使用频率：** ⭐⭐ 中等偏低
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 2-3天

**问题：** 需识别类型参数、构造参数

---

#### 13. **UK2Node_CreateDelegate** — Create Event/Create Delegate
- **用途：** 创建委托/事件绑定
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** TODO
- **转换难度：** 🔴 高
- **预计工时：** 5-7天

**问题：** 委托系统复杂，需映射到 `BindDynamic`、`BindUFunction` 等

**替代方案：** 生成带TODO的委托绑定框架

---

#### 14. **UK2Node_CallDelegate** — Call Delegate
- **用途：** 执行委托
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 2-3天

**问题：** 需识别委托变量、参数

---

#### 15. **UK2Node_CallArrayFunction** — Array操作节点
- **用途：** Array的Add、Remove、Clear等操作
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 2-3天

**问题：** 需识别具体的Array函数（Add、Remove、Find等）

**示例：**
```blueprint
Array Add (to array, new item)
```

**目标C++代码：**
```cpp
MyArray.Add(NewItem);  // or emplace for complex types
```

---

#### 16. **UK2Node_CallMapFunction** — Map操作节点
- **用途：** Map的Add、Remove、Find等操作
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 2-3天

**问题：** 需识别具体的Map函数

---

#### 17. **UK2Node_CallSetFunction** — Set操作节点
- **用途：** Set的Add、Remove、Contains等操作
- **使用频率：** ⭐⭐ 中等偏低
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 2-3天

---

#### 18. **UK2Node_GetClassDefaults** — Get Class Defaults
- **用途：** 获取类的CDO属性
- **使用频率：** ⭐⭐ 中等偏低
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 2-3天

**问题：** 需识别类引用、属性名

---

#### 19. **UK2Node_GetEnumValue** — Get Enum by Index (虽然通常可工作)
- **用途：** 从Enum获取值
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** 部分自动
- **转换难度：** 🟢 低
- **预计工时：** 1天

---

### 第三类：事件/消息节点（5个）

#### 20. **UK2Node_CustomEvent** — Custom Event
- **用途：** 自定义事件（可从其他地方调用）
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** TODO
- **转换难度：** 🟡 中
- **预计工时：** 2-3天

**问题：** 需为每个Custom Event生成UFUNCTION，记录参数

---

#### 21. **UK2Node_CallParentFunction** — Call Parent Function
- **用途：** 调用父类同名函数
- **使用频率：** ⭐⭐ 中等偏低
- **生成代码：** TODO
- **转换难度：** 🟢 低
- **预计工时：** 1-2天

**问题：** 简单替换为 `Super::FunctionName()`

---

#### 22. **UK2Node_InputActionEvent** — Input Action Event
- **用途：** 响应输入系统事件
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** TODO
- **转换难度：** 🔴 高
- **预计工时：** 5-7天

**问题：** 需整合Enhanced Input System，映射InputAction资源

---

#### 23. **UK2Node_InputKeyEvent** — Input Key Event
- **用途：** 响应按键输入
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** TODO
- **转换难度：** 🔴 高
- **预计工时：** 5-7天

**问题：** 需映射到 `SetupPlayerInputComponent()`

---

#### 24. **UK2Node_InputActionValue** — Get Input Action Value
- **用途：** 获取输入Action的值（模拟摇杆等）
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** TODO
- **转换难度：** 🔴 高
- **预计工时：** 5-7天

---

### 第四类：Reroute/工具节点（7个）

#### 25. **UK2Node_Reroute** — Reroute Node
- **用途：** 重新路由执行流（美化布局）
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** 无需代码（透传）
- **转换难度：** 🟢 低
- **预计工时：** 1天

**问题：** 应直接透传，不生成代码

**解决：** 在遍历时跳过此节点

---

#### 26. **UK2Node_Comment** — Comment
- **用途：** 注释框
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** 无需代码
- **转换难度：** 🟢 低
- **预计工时：** <1天

**问题：** 应直接忽略

---

#### 27. **UK2Node_Knot** — Knot (数据路由)
- **用途：** 数据节点的重新路由
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** 无需代码
- **转换难度：** 🟢 低
- **预计工时：** <1天

---

#### 28. **UK2Node_BreakStruct/MakeStruct** — Break/Make Struct
- **用途：** Struct拆解/合成
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** 部分自动
- **转换难度：** 🟡 中
- **预计工时：** 2-3天

**问题：** 需识别Struct类型、成员

**示例：**
```blueprint
Make Transform
  Location: FVector(0, 0, 0)
  Rotation: FRotator(0, 0, 0)
  Scale: FVector(1, 1, 1)
  → Transform output
```

**目标C++代码：**
```cpp
FTransform Transform(
    FRotator(0, 0, 0).Quaternion(),
    FVector(0, 0, 0),
    FVector(1, 1, 1)
);
```

---

#### 29. **UK2Node_BreakArray** — Break Array Element
- **用途：** 数组索引访问
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** 部分自动
- **转换难度：** 🟢 低
- **预计工时：** 1-2天

**问题：** 简单替换为 `Array[Index]`

---

#### 30. **UK2Node_Tunnel** — Tunnel Pins (在宏中)
- **用途：** 宏的输入输出引脚
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** 需与Macro展开配合
- **转换难度：** 🔴 高
- **预计工时：** 宏展开的一部分

---

### 第五类：渲染/物理/特殊节点（8个）

#### 31. **UK2Node_GetActorBounds** — Get Actor Bounds
- **用途：** 获取Actor碰撞体积
- **使用频率：** ⭐⭐ 中等偏低
- **生成代码：** 部分自动
- **转换难度：** 🟢 低
- **预计工时：** 1天

**问题：** 简单调用 `GetActorBounds()`

---

#### 32. **UK2Node_GetRenderedPrimitives** — Get Scene Captures
- **用途：** 获取渲染的Primitive组件
- **使用频率：** ⭐ 罕见
- **生成代码：** TODO
- **转换难度：** 🔴 高
- **预计工时：** 7-10天

**问题：** 涉及渲染系统深度集成

---

#### 33. **UK2Node_ExecuteConsoleCommand** — Execute Console Command
- **用途：** 执行控制台命令
- **使用频率：** ⭐⭐ 中等偏低
- **生成代码：** TODO
- **转换难度：** 🟢 低
- **预计工时：** 1-2天

**问题：** 调用 `GEngine->Exec()`

---

#### 34. **UK2Node_PrintString** — Print String (Should be auto-handled)
- **用途：** 打印到屏幕
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** 部分自动
- **转换难度：** 🟢 低
- **预计工时：** 1天

**问题：** 应映射到 `GEngine->AddOnScreenDebugMessage()` 或 `UE_LOG()`

---

#### 35. **UK2Node_DrawDebugLine** — Draw Debug X (Line/Box/Circle等)
- **用途：** 调试绘制
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** 部分自动
- **转换难度：** 🟢 低
- **预计工时：** 1-2天

**问题：** 映射到 `DrawDebugLine()` 等API

---

#### 36. **UK2Node_WaitLatentAction** — Wait Latent Action (基类)
- **用途：** 异步操作等待（Load Async等）
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** TODO
- **转换难度：** 🔴 高
- **预计工时：** 7-10天

**问题：** 需深度理解Latent Action机制，映射到异步API

---

#### 37. **UK2Node_LoadAsset** — Load Asset Async
- **用途：** 异步加载资源
- **使用频率：** ⭐⭐⭐⭐ 很高
- **生成代码：** TODO
- **转换难度：** 🔴 高
- **预计工时：** 5-7天

**问题：** 需映射到 `StreamableManager` 或 `FStreamableHandle`

**示例：**
```blueprint
Load Asset Async
  Asset: "/Game/Meshes/MyMesh"
  → OnLoaded (with loaded asset)
  → OnFailed
```

**目标C++代码：**
```cpp
IStreamingManager::Get().GetStreamingEngine()->GetStreamableManager().RequestAsyncLoad(
    FSoftObjectPath(AssetPath),
    [this](const FString& Path, TSharedPtr<IStreamable> Asset, EAsyncIOPriority Priority)
    {
        if (Asset.IsValid()) {
            // OnLoaded
        } else {
            // OnFailed
        }
    }
);
```

---

#### 38. **UK2Node_AsyncAction** — Async Action (基类)
- **用途：** 异步操作
- **使用频率：** ⭐⭐⭐ 中高
- **生成代码：** TODO
- **转换难度：** 🔴 高
- **预计工时：** 7-10天

**问题：** 各种AsyncAction有不同的签名和行为

---

---

## 部分支持的节点

这些节点虽然在代码中有处理，但生成的C++代码仍需人工补全。

### 1. **UK2Node_CallFunction** — 函数调用

**当前状态：** ✅ 识别 + 📝 生成TODO框架  
**完成度：** 60%

**当前生成：**
```cpp
// Call: MyFunction
// TODO: implement call to MyFunction on ACharacter.
```

**改进后应生成：**
```cpp
// Call: MyFunction()
if (IsValid(this)) {
    MyFunction(); // TODO: verify parameters
}
```

**需完成的工作：**
- ✅ 提取函数名和目标类
- ✅ 识别是否为自身调用
- ❌ 提取调用参数
- ❌ 生成参数列表
- ❌ 处理返回值

---

### 2. **UK2Node_MacroInstance** — 宏实例

**当前状态：** ❌ 标记为Unsupported + 📝 生成TODO  
**完成度：** 10%

**当前生成：**
```cpp
// TODO: macro 'MyMacro' must be expanded manually.
```

**改进后应生成：**
```cpp
// Macro: MyMacro (expanded)
// [宏内的所有节点被递归展开]
```

**需完成的工作：**
- ❌ 递归访问宏内的Graph
- ❌ 展开宏的Tunnel入口
- ❌ 参数映射
- ❌ 循环检测（防止宏调用宏的无限递归）

---

### 3. **UK2Node_VariableGet/Set** — 变量访问

**当前状态：** ✅ 识别 + 📝 生成注释  
**完成度：** 40%

**当前生成：**
```cpp
// VariableGet: MyVariable
// VariableSet: MyVariable
```

**改进后应生成：**
```cpp
int32 Value = MyVariable;  // Get
MyVariable = NewValue;      // Set
```

**需完成的工作：**
- ✅ 识别变量名
- ❌ 识别变量类型
- ❌ 生成赋值表达式
- ❌ 处理数据流

---

---

## 实现优先级

### 🚀 第一梯队（第1-2周）— 高频 + 低难度

| 节点 | 类型 | 难度 | 工时 | 使用频率 | 收益 |
|------|------|------|------|---------|------|
| UK2Node_Delay | 执行 | 🟢 低 | 1-2天 | ⭐⭐⭐⭐ | 高 |
| UK2Node_DynamicCast | 数据 | 🟢 低 | 1-2天 | ⭐⭐⭐ | 中高 |
| UK2Node_Select | 数据 | 🟢 低 | 1-2天 | ⭐⭐⭐⭐ | 高 |
| UK2Node_SpawnActor | 执行 | 🟢 低 | 1-2天 | ⭐⭐⭐⭐ | 高 |
| UK2Node_PrintString | 工具 | 🟢 低 | 1天 | ⭐⭐⭐ | 中 |
| UK2Node_ForLoop | 执行 | 🟡 中 | 3-4天 | ⭐⭐⭐⭐ | 高 |
| UK2Node_Switch | 数据 | 🟡 中 | 2-3天 | ⭐⭐⭐⭐ | 高 |
| UK2Node_CallArrayFunction | 执行 | 🟡 中 | 2-3天 | ⭐⭐⭐⭐ | 高 |

**累计工时：** 14-20天  
**预期完成度提升：** ~35%

---

### 🟡 第二梯队（第3-4周）— 中频 + 中难度

| 节点 | 类型 | 难度 | 工时 | 使用频率 | 收益 |
|------|------|------|------|---------|------|
| UK2Node_CallDelegate | 执行 | 🟡 中 | 2-3天 | ⭐⭐⭐ | 中 |
| UK2Node_BreakStruct/MakeStruct | 工具 | 🟡 中 | 2-3天 | ⭐⭐⭐⭐ | 高 |
| UK2Node_CustomEvent | 事件 | 🟡 中 | 2-3天 | ⭐⭐⭐ | 中高 |
| UK2Node_CallMapFunction | 执行 | 🟡 中 | 2-3天 | ⭐⭐⭐ | 中 |
| UK2Node_DoN | 执行 | 🟡 中 | 2-3天 | ⭐⭐ | 低 |
| UK2Node_MultiGate | 执行 | 🟡 中 | 3-4天 | ⭐⭐ | 低 |
| UK2Node_DrawDebugLine | 工具 | 🟢 低 | 1-2天 | ⭐⭐⭐ | 中 |
| UK2Node_ExecuteConsoleCommand | 工具 | 🟢 低 | 1-2天 | ⭐⭐ | 低 |

**累计工时：** 16-23天  
**预期完成度提升：** ~25%  
**总体完成度：** ~60%

---

### 🔴 第三梯队（第5-6周）— 低频 或 高难度

| 节点 | 类型 | 难度 | 工时 | 使用频率 | 收益 |
|------|------|------|------|---------|------|
| UK2Node_LoadAsset | 执行 | 🔴 高 | 5-7天 | ⭐⭐⭐⭐ | 高 |
| UK2Node_InputActionEvent | 事件 | 🔴 高 | 5-7天 | ⭐⭐⭐⭐ | 高 |
| UK2Node_InputKeyEvent | 事件 | 🔴 高 | 5-7天 | ⭐⭐⭐⭐ | 高 |
| UK2Node_Timeline | 执行 | 🔴 高 | 7-10天 | ⭐⭐⭐ | 中 |
| UK2Node_CreateDelegate | 执行 | 🔴 高 | 5-7天 | ⭐⭐⭐ | 中 |
| UK2Node_WaitLatentAction | 执行 | 🔴 高 | 7-10天 | ⭐⭐⭐ | 中 |
| UK2Node_AsyncAction | 执行 | 🔴 高 | 7-10天 | ⭐⭐⭐ | 中 |

**累计工时：** 42-58天  
**预期完成度提升：** ~25%  
**总体完成度：** ~85%

---

### ⚪ 第四梯队（优化阶段）— 边界情况

| 节点 | 类型 | 难度 | 工时 | 收益 |
|------|------|------|------|------|
| Reroute/Knot/Comment | 工具 | 🟢 低 | <1天 | 低 |
| GetRenderedPrimitives | 特殊 | 🔴 高 | 7-10天 | 极低 |
| 其他罕见节点 | 混合 | 混合 | 变量 | 低 |

---

---

## 详细实现方案

### 方案1：UK2Node_Delay — 延迟执行

#### C++侧改进（BlueprintBusterParsers.cpp）

```cpp
// 在 TraceNode() 中添加
if (const UK2Node_CallFunction* DelayNode = Cast<UK2Node_CallFunction>(InNode))
{
    // 检测是否为Delay节点
    if (DelayNode->FunctionReference.GetMemberName() == FName("Delay"))
    {
        Info->NodeKind  = TEXT("Delay");
        Info->NodeLabel = TEXT("Delay");
        
        // 提取延迟时间参数
        for (UEdGraphPin* Pin : DelayNode->Pins)
        {
            if (Pin && Pin->GetName() == "Duration" && Pin->Direction == EGPD_Input)
            {
                if (Pin->LinkedTo.Num() > 0)
                {
                    // 从连接获取
                    Info->FunctionParameterCount++;
                }
                else if (!Pin->DefaultValue.IsEmpty())
                {
                    Info->NodeLabel = FString::Printf(TEXT("Delay (%.2fs)"), 
                        FCString::Atof(*Pin->DefaultValue));
                }
            }
        }
        return Info;
    }
}
```

#### Python侧代码生成（bp_translator.py）

```python
def _emit_node_chain(root: GraphNode, indent: int) -> List[str]:
    def emit_node(node: GraphNode, depth: int) -> None:
        local_pad = "    " * depth
        
        if node.kind == "Delay":
            duration = node.label.split("(")[-1].rstrip("s)") if "(" in node.label else "1.0"
            out.append(f"{local_pad}// Delay: {node.label}")
            out.append(
                f"{local_pad}GetWorld()->GetTimerManager().SetTimer("
            )
            out.append(
                f"{local_pad}    DelayHandle_{depth},"
            )
            out.append(
                f"{local_pad}    this,"
            )
            out.append(
                f"{local_pad}    &{node.class_name}::OnDelayComplete_{depth},"
            )
            out.append(
                f"{local_pad}    {duration}f,"
            )
            out.append(
                f"{local_pad}    false  // TODO: set to true for looping"
            )
            out.append(
                f"{local_pad});"
            )
```

#### 生成的.h文件

```cpp
private:
    FTimerHandle DelayHandle_1;
    
    void OnDelayComplete_1();
```

#### 生成的.cpp文件

```cpp
void AMyActor::OnDelayComplete_1()
{
    // TODO: implement logic after delay
}
```

---

### 方案2：UK2Node_ForLoop — 循环

#### C++侧改进

```cpp
if (const UK2Node_CallFunction* ForLoopNode = Cast<UK2Node_CallFunction>(InNode))
{
    if (ForLoopNode->FunctionReference.GetMemberName() == FName("ForLoop"))
    {
        Info->NodeKind  = TEXT("ForLoop");
        Info->NodeLabel = TEXT("ForLoop");
        
        // 提取First Index, Last Index
        int32 FirstIdx = 0, LastIdx = 10;
        for (UEdGraphPin* Pin : ForLoopNode->Pins)
        {
            if (Pin->GetName() == "First Index" && !Pin->DefaultValue.IsEmpty())
                FirstIdx = FCString::Atoi(*Pin->DefaultValue);
            if (Pin->GetName() == "Last Index" && !Pin->DefaultValue.IsEmpty())
                LastIdx = FCString::Atoi(*Pin->DefaultValue);
        }
        
        Info->LoopFirstIndex = FirstIdx;
        Info->LoopLastIndex = LastIdx;
        
        // 追踪Loop Body执行链
        for (UEdGraphPin* Pin : ForLoopNode->Pins)
        {
            if (Pin->GetName() == "Loop Body" && Pin->Direction == EGPD_Output)
            {
                if (const UEdGraphNode* LoopBody = GetLinkedExecNode(Pin))
                {
                    TraceLinearChain(LoopBody, Depth + 1, MaxDepth, VisitedThisChain,
                                   Info->LoopBody, OutTotalCount, OutUnsupportedCount);
                }
            }
        }
        
        return Info;
    }
}
```

#### Python侧代码生成

```python
if node.kind == "ForLoop":
    first = node.loop_first_index if hasattr(node, 'loop_first_index') else 0
    last = node.loop_last_index if hasattr(node, 'loop_last_index') else 10
    
    out.append(f"{local_pad}for (int32 Index = {first}; Index <= {last}; ++Index)")
    out.append(f"{local_pad}{{")
    
    # 递归生成Loop Body
    if node.next:
        for child in node.next:
            emit_node(child, depth + 1)
    else:
        out.append(f"{local_pad}    // TODO: implement loop body")
    
    out.append(f"{local_pad}}}")
    out.append("")
    out.append(f"{local_pad}// Loop complete")
```

#### 生成的C++代码

```cpp
for (int32 Index = 0; Index <= 10; ++Index)
{
    // Loop body
    // TODO: implement loop logic with Index
}
// Loop complete
```

---

### 方案3：UK2Node_MacroInstance — 宏展开

#### C++侧递归展开

```cpp
static void ExpandMacroGraph(const UK2Node_MacroInstance* MacroNode,
                            int32 Depth,
                            int32 MaxDepth,
                            TSet<const UEdGraphNode*>& VisitedThisChain,
                            TArray<TSharedPtr<FBPGraphNodeInfo>>& OutExpanded,
                            int32& OutTotalCount,
                            int32& OutUnsupportedCount)
{
    if (!MacroNode || Depth >= MaxDepth)
        return;

    UEdGraph* MacroGraph = MacroNode->GetMacroGraph();
    if (!IsValid(MacroGraph))
        return;

    // 防止无限循环：宏不应该调用自己
    if (VisitedThisChain.Contains(MacroNode))
        return;
    
    VisitedThisChain.Add(MacroNode);

    // 查找宏的执行入口（Tunnel FunctionEntry）
    UK2Node_Tunnel* EntryTunnel = nullptr;
    for (UEdGraphNode* Node : MacroGraph->Nodes)
    {
        if (UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node))
        {
            if (Tunnel->IsA<UK2Node_FunctionEntry>())
            {
                EntryTunnel = Tunnel;
                break;
            }
        }
    }

    if (!EntryTunnel)
        return;

    // 从Tunnel的Then引脚开始追踪
    for (UEdGraphPin* Pin : EntryTunnel->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output &&
            Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
        {
            if (const UEdGraphNode* FirstBodyNode = GetLinkedExecNode(Pin))
            {
                TraceLinearChain(FirstBodyNode, Depth + 1, MaxDepth, VisitedThisChain,
                               OutExpanded, OutTotalCount, OutUnsupportedCount);
            }
            break;
        }
    }
}

// 在 TraceNode 中修改Macro处理
if (const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(InNode))
{
    Info->NodeKind  = TEXT("MacroInstance");
    Info->NodeLabel = MacroNode->GetMacroGraph()
        ? MacroNode->GetMacroGraph()->GetName()
        : TEXT("UnknownMacro");

    UEdGraph* MacroGraph = MacroNode->GetMacroGraph();
    if (IsValid(MacroGraph))
    {
        // 尝试展开宏内容
        TArray<TSharedPtr<FBPGraphNodeInfo>> ExpandedNodes;
        ExpandMacroGraph(MacroNode, Depth, MaxDepth, VisitedThisChain,
                        ExpandedNodes, OutTotalCount, OutUnsupportedCount);
        
        if (ExpandedNodes.Num() > 0)
        {
            // 成功展开
            Info->Next = ExpandedNodes;
            Info->UnsupportedReason = "";  // 不是unsupported了
        }
        else
        {
            // 宏为空或展开失败
            Info->UnsupportedReason = TEXT("Macro body is empty or circular");
            ++OutUnsupportedCount;
        }
    }
    else
    {
        Info->UnsupportedReason = TEXT("Macro graph not found");
        ++OutUnsupportedCount;
    }
    
    return Info;
}
```

#### Python侧展开处理

```python
def _emit_node_chain(root: GraphNode, indent: int) -> List[str]:
    def emit_node(node: GraphNode, depth: int) -> None:
        local_pad = "    " * depth
        
        if node.kind == "MacroInstance":
            if node.unsupported:
                # 宏无法展开
                out.append(
                    f"{local_pad}// ❌ Macro '{node.label}' failed to expand: {node.unsupported}"
                )
            else:
                # 宏成功展开
                out.append(f"{local_pad}// Macro: {node.label} ▼ (expanded)")
                # 递归生成宏体
                for child in node.next:
                    emit_node(child, depth)
                out.append(f"{local_pad}// Macro: {node.label} ▲ (end)")
                return  # 不再处理node.next（已处理过了）
```

#### 生成的C++代码

```cpp
// Macro: MakeDamageInfo ▼ (expanded)
// VariableSet: DamageAmount
DamageAmount = 50.0f;
// Call: ApplyDamage
ApplyDamage(); // TODO: verify parameters
// Macro: MakeDamageInfo ▲ (end)
```

---

### 方案4：UK2Node_Switch — Switch语句

#### C++侧信息提取

```cpp
// 检测Switch节点（需要在Kismet/K2Node中查找）
// UE5中可能是 UK2Node_Switch 或通过CallFunction模拟
if (const UK2Node_CallFunction* SwitchNode = Cast<UK2Node_CallFunction>(InNode))
{
    if (SwitchNode->FunctionReference.GetMemberName().ToString().Contains("Switch"))
    {
        Info->NodeKind = TEXT("Switch");
        Info->NodeLabel = TEXT("Switch");
        
        // 提取Switch的输入引脚（条件值）
        for (UEdGraphPin* Pin : SwitchNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Input &&
                Pin->GetName() == "Selection")
            {
                if (!Pin->DefaultValue.IsEmpty())
                {
                    Info->SwitchValue = Pin->DefaultValue;
                }
            }
        }
        
        // 提取所有Case分支
        for (UEdGraphPin* Pin : SwitchNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output &&
                Pin->GetName().Contains("Case"))
            {
                FString CaseName = Pin->GetName();
                if (const UEdGraphNode* CaseNode = GetLinkedExecNode(Pin))
                {
                    TArray<TSharedPtr<FBPGraphNodeInfo>> CaseChain;
                    TraceLinearChain(CaseNode, Depth + 1, MaxDepth, VisitedThisChain,
                                   CaseChain, OutTotalCount, OutUnsupportedCount);
                    Info->SwitchCases.Add({CaseName, CaseChain});
                }
            }
        }
        
        // 提取Default分支
        for (UEdGraphPin* Pin : SwitchNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output &&
                Pin->GetName() == "Default")
            {
                if (const UEdGraphNode* DefaultNode = GetLinkedExecNode(Pin))
                {
                    TraceLinearChain(DefaultNode, Depth + 1, MaxDepth, VisitedThisChain,
                                   Info->SwitchDefault, OutTotalCount, OutUnsupportedCount);
                }
            }
        }
        
        return Info;
    }
}
```

#### Python侧代码生成

```python
if node.kind == "Switch":
    out.append(f"{local_pad}// Switch statement")
    out.append(f"{local_pad}switch (/* TODO: switch value */)")
    out.append(f"{local_pad}{{")
    
    if hasattr(node, 'switch_cases') and node.switch_cases:
        for case_name, case_nodes in node.switch_cases:
            case_label = case_name.replace("Case_", "")
            out.append(f"{local_pad}    case {case_label}:")
            
            # 递归生成case体
            for child in case_nodes:
                emit_node(child, depth + 2)
            
            out.append(f"{local_pad}        break;")
    
    # Default分支
    if hasattr(node, 'switch_default') and node.switch_default:
        out.append(f"{local_pad}    default:")
        for child in node.switch_default:
            emit_node(child, depth + 2)
        out.append(f"{local_pad}        break;")
    
    out.append(f"{local_pad}}}")
```

#### 生成的C++代码

```cpp
// Switch statement
switch (/* TODO: switch value */)
{
    case 0:
        // VariableSet: DialogueState
        DialogueState = 1;
        break;
    case 1:
        // Call: PlayDialogue
        PlayDialogue(); // TODO: verify parameters
        break;
    default:
        // Call: HandleDefault
        HandleDefault(); // TODO: verify parameters
        break;
}
```

---

## 实现检查清单

### 第一阶段检查清单（Week 1-2）

- [ ] UK2Node_Delay 实现完成
- [ ] UK2Node_DynamicCast 实现完成
- [ ] UK2Node_Select 实现完成
- [ ] UK2Node_SpawnActor 实现完成
- [ ] UK2Node_ForLoop 实现完成
- [ ] UK2Node_Switch 实现完成
- [ ] 单元测试覆盖（至少50个测试用例）
- [ ] 文档更新：README.md 表格更新
- [ ] 集成测试通过

### 第二阶段检查清单（Week 3-4）

- [ ] UK2Node_CallDelegate 实现完成
- [ ] UK2Node_BreakStruct/MakeStruct 实现完成
- [ ] UK2Node_CustomEvent 实现完成
- [ ] UK2Node_MacroInstance 展开实现完成
- [ ] 回归测试通过（所有第一阶段节点仍可工作）
- [ ] 性能基准测试（处理大型Blueprint的时间）

### 第三阶段检查清单（Week 5-6）

- [ ] UK2Node_LoadAsset 实现完成
- [ ] UK2Node_InputActionEvent/InputKeyEvent 实现完成
- [ ] 异步节点处理能力
- [ ] 最终集成测试
- [ ] README/INSTRUCTION 完整更新

---

## 辅助工具建议

### 1. 节点诊断工具

创建Python脚本，扫描所有JSON dumpfile并统计未支持节点：

```python
# analyze_unsupported.py
import json
import sys
from collections import defaultdict

def analyze_dump(dump_path):
    with open(dump_path, 'r') as f:
        dump = json.load(f)
    
    unsupported = defaultdict(list)
    
    def collect_unsupported(node, path=""):
        if node.get("kind") == "Unsupported":
            node_type = node.get("label", "Unknown")
            reason = node.get("unsupported", "")
            unsupported[node_type].append({
                "path": path,
                "reason": reason
            })
        
        for child in node.get("next", []) + node.get("true", []) + node.get("false", []):
            collect_unsupported(child, path + " > " + node.get("label", "?"))
    
    for tree in dump.get("eventTrees", []):
        if tree.get("event"):
            collect_unsupported(tree["event"], f"Event:{tree.get('graphName')}")
    
    for func in dump.get("customFunctions", []):
        if func.get("functionRoot"):
            collect_unsupported(func["functionRoot"], f"Function:{func.get('functionName')}")
    
    return unsupported

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python analyze_unsupported.py <dump_file>")
        sys.exit(1)
    
    unsupported = analyze_dump(sys.argv[1])
    
    print(f"\nFound {sum(len(v) for v in unsupported.values())} unsupported nodes")
    for node_type, instances in sorted(unsupported.items()):
        print(f"\n{node_type}: {len(instances)} instance(s)")
        for inst in instances[:3]:  # 显示前3个
            print(f"  - {inst['path']}")
            if inst['reason']:
                print(f"    Reason: {inst['reason']}")
```

### 2. 代码覆盖率跟踪

添加到CI/CD中：

```bash
# 统计支持的节点类型占比
python -m pytest tests/ --cov=bp_translator --cov-report=term-missing
```

---

## 参考资源

### UE5官方文档

- [K2Node API Reference](https://docs.unrealengine.com/5.0/en-US/API/)
- [Blueprint Scripting Documentation](https://docs.unrealengine.com/5.0/en-US/BlueprintAPI/)

### 相关论文/博客

- UE5 Blueprint优化最佳实践
- C++与Blueprint的性能对比分析

---

## 贡献指南

如需添加新节点支持：

1. 在本文档中新增节点条目
2. 创建专用PR，标题格式：`[Feature] Add support for UK2Node_XXX`
3. 包含：
   - C++侧实现（BlueprintBusterParsers.cpp）
   - Python侧代码生成（bp_translator.py）
   - 至少3个测试用例
   - README.md 更新

---

**Last Updated:** 2026-05-29  
**Status:** Active Development  
**Maintainer:** SeAmenLoo  
