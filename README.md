# BlueprintBuster

> **Isolated portable Editor plugin for Unreal Engine 5.7+**
> Deep decompilation analysis of Blueprint assets → JSON dump → native C++ code.

---

## Table of Contents

1. [Purpose](#1-purpose)
2. [Key Features](#2-key-features)
3. [Generated C++ Code Standards](#3-generated-c-code-standards)
4. [Quick Start Guide](#4-quick-start-guide)
5. [Plugin Structure](#5-plugin-structure)

---

## 1. Purpose

### The Problem

Working with Blueprint assets in large projects inevitably produces a set of systemic issues.

| Problem | Consequence |
|---|---|
| **Spaghetti logic graphs** | A complex visual scheme of hundreds of nodes cannot be code-reviewed or refactored |
| **Binary `.uasset` format** | Git cannot produce human-readable diffs; branch merges are a manual process |
| **Blueprint VM overhead** | Every tick is executed through an interpreter rather than native machine code |
| **Manual migration to C++** | Enormous time costs, high risk of regressions when manually copying components, variables and graph logic |

### The Solution

**BlueprintBuster** is an isolated portable Editor plugin for Unreal Engine 5.7+ that solves the Blueprint-to-C++ migration task in two sequential steps.

```
Blueprint .uasset
       │
       ▼
[ UE5 Commandlet ]  ←  deep analysis via UE Reflection API
       │                SCS  ·  CDO  ·  Event Graph  ·  Custom Functions
       ▼
  *_dump.json           complete machine-readable asset map
       │
       ▼
[ bp_translator.py ]  ←  Python 3.10+, stdlib only
       │
       ▼
  MyClass.h + MyClass.cpp   ready-to-use native C++ scaffold
```

The plugin **does not modify** any project asset. All operations are read-only via AssetRegistry and Reflection API.

---

## 2. Key Features

### 2.1. Autonomous C++ Commandlet

The plugin runs via `UnrealEditor-Cmd.exe` — without launching the full graphical Editor.
This makes it suitable for embedding in CI/CD pipelines, scripting environments and batch tasks.

- Processes a **single asset** by exact path `/Game/...`
- Processes an **entire directory recursively** via AssetRegistry scan
- Accepts the `-Plugin=` flag — loads the module on the fly without modifying the target project's `.uproject`

### 2.2. Deep Component Parsing (SCS)

Full traversal of the **Simple Construction Script** hierarchy.

- Reconstructs the exact component nesting tree (parent → child)
- Preserves variable names and class types (`UStaticMeshComponent`, `UCapsuleComponent`, etc.)
- Records attachment points (`SetupAttachment`) and socket names
- Marks the root component for correct `RootComponent = ...` in the constructor

### 2.3. Smart CDO Extraction

**Class Default Object** property analysis via UE reflection.

- Exports **only those properties** whose values differ from the parent CDO — no noise
- Resolves `EditAnywhere` / `EditDefaultsOnly` / `BlueprintVisible` specifiers
- Contains **surgical reflection protection**: before accessing each property, an `IsA` ownership check is performed — this completely eliminates Assertion crashes when attempting to read properties belonging exclusively to child Blueprint classes
- Automatically determines the pointer storage type and writes a hint (`Hard` / `Soft` / `Class` / `SoftClass` / `Weak`) — the translator uses it to select the correct smart pointer

### 2.4. Full Graph AST Compilation

Recursive Exec Flow tracing with AST tree construction.

| Graph Type | Entry Point | Support |
|---|---|---|
| Event Graph | `UK2Node_Event` | ✅ |
| Construction Script | `UK2Node_Event` (ConstructionScript) | ✅ |
| Custom Function | `UK2Node_FunctionEntry` | ✅ |
| Custom Function return | `UK2Node_FunctionResult` | ✅ |

**Supported node types:**

| Node | Translator Behaviour |
|---|---|
| `UK2Node_CallFunction` | TODO stub with function name and target class |
| `UK2Node_IfThenElse` | `if/else` with `True` / `False` branches |
| `UK2Node_ExecutionSequence` | Linear execution in pin order |
| `UK2Node_VariableGet/Set` | Comment with variable name |
| `UK2Node_MacroInstance` | TODO stub with macro name |
| Any unknown node | TODO stub with node class name |

**Custom function signatures** are fully reconstructed:
- Input parameters — from output data pins of `UK2Node_FunctionEntry`
- Return values — from input data pins of `UK2Node_FunctionResult`
- Automatic pin type to C++ type conversion (see table below)

| Blueprint Pin Type | C++ Type |
|---|---|
| `bool` | `bool` |
| `byte` | `uint8` |
| `int` | `int32` |
| `int64` | `int64` |
| `float` / `real (float)` | `float` |
| `double` / `real (double)` | `double` |
| `string` | `FString` |
| `name` | `FName` |
| `text` | `FText` |
| `object` | `TObjectPtr<ClassName>` |
| `class` | `TSubclassOf<ClassName>` |
| `softobject` | `TSoftObjectPtr<ClassName>` |
| `softclass` | `TSoftClassPtr<ClassName>` |
| `weakobject` | `TWeakObjectPtr<ClassName>` |
| `struct` | `FStructName` |
| `enum` | `EEnumName` |
| `TArray<T>` | `TArray<T>` |
| `TSet<T>` | `TSet<T>` |
| `TMap<K,V>` | `TMap<K, V>  // TODO: confirm key type` |

### 2.5. Hard Shutdown Fix

When a headless commandlet exits, Unreal Engine may crash during destruction of editor subsystems. BlueprintBuster architecturally bypasses this bug.

After writing the last JSON file, the Commandlet triggers **immediate hardware process termination** via `std::_Exit`, bypassing the buggy garbage collector loops and editor subsystem destructors. The operating system releases resources itself — this is a correct and widely used technique for headless UE utilities.

---

## 3. Generated C++ Code Standards

The Python translator `bp_translator.py` produces code that strictly conforms to the project's internal quality standards. Any deviation from the rules below is considered a translator defect, not a reason to manually fix the template.

### 3.1. Zero Raw Pointers

Raw pointers (`UObject*`, `AActor*`) in generated `.h` files are **completely forbidden**.
The translator selects the wrapper automatically based on the `pointerStorageHint` from the JSON dump:

| Dump Hint | Header Type |
|---|---|
| `Hard` | `TObjectPtr<T>` |
| `Soft` | `TSoftObjectPtr<T>` |
| `Class` | `TSubclassOf<T>` |
| `SoftClass` | `TSoftClassPtr<T>` |
| `Weak` | `TWeakObjectPtr<T>` |

### 3.2. Visual Separation

The structure of each generated class is strictly divided into two visual blocks of 70 characters width:

```cpp
    //************************PROPERTIES************************//
public:
    // ...public UPROPERTY...

protected:
    // ...protected UPROPERTY...

private:
    // ...private fields...

    //************************FUNCTIONS************************//
public:
    AMyClass();
    // ...public UFUNCTION...

protected:
    virtual void BeginPlay() override;
    // ...protected overrides...

private:
    // ...
```

Variables and functions are never mixed in the same block.

### 3.3. Instance Settings Naming

All `UPROPERTY` marked as `EditAnywhere` or `EditInstanceOnly` receive a category with the mandatory contiguous prefix `InstanceSettings`:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InstanceSettings|Dialogue")
float ResponseTimeout = 5.0f;
```

Categories without `InstanceSettings` are only allowed for properties not editable per-instance (`EditDefaultsOnly`, `VisibleAnywhere`).

### 3.4. CPU Optimization — Anti-Tick

Constructors of all generated classes contain an explicit tick disable by default:

```cpp
// Actors:
PrimaryActorTick.bCanEverTick = false;

// ActorComponents:
PrimaryComponentTick.bCanEverTick = false;
```

Tick is enabled **only** if `ReceiveTick` / `Event Tick` node is detected in the Blueprint graph. In that case, the translator adds an inline justification comment next to the line, per TDD-06 §2.6 requirement.

### 3.5. Legacy Restriction

The translator rejects generation of classes whose name contains a forbidden substring, exiting with a diagnostic message and return code `2`. This prevents legacy abbreviations from Blueprint asset names entering the native codebase.

---

## 4. Quick Start Guide

> Full instructions with parameter tables, return codes and manual review checklist — in [`INSTRUCTION.md`](INSTRUCTION.md).

### 4.1. Installation

1. Copy the `BlueprintBuster/` folder into the `Plugins/` directory of the target project:
   ```
   <YourProject>/
     Plugins/
       BlueprintBuster/
         BlueprintBuster.uplugin
         Source/
         Python/
         README.md
         INSTRUCTION.md
   ```

2. Right-click `<YourProject>.uproject` → **Generate Visual Studio project files**.

3. Rebuild the **Development Editor | Win64** configuration.

> If the plugin is used only as a command-line tool and is not needed in the running Editor, steps 2–3 are still mandatory — the module must be compiled. Activating it via `Edit → Plugins` is not required: the `-Plugin=` flag in the command will load it on the fly.

### 4.2. Step 1 — Export Blueprints to JSON

```bat
"<UE_INSTALL>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
    "<YourProject>\<YourProject>.uproject" ^
    -run=BlueprintBuster ^
    -Plugin="<YourProject>\Plugins\BlueprintBuster\BlueprintBuster.uplugin" ^
    -TargetDir="/Game/Blueprints/MySystem" ^
    [-OutputDir="<DumpsDir>"] ^
    -NoUI
```

Parameters:

| Key | Purpose |
|---|---|
| `<UE_INSTALL>` | UE 5.7 installation root: `C:\Program Files\Epic Games\UE_5.7` or path to source build |
| `<YourProject>` | Absolute path to the UE project root |
| `-TargetDir` | Virtual Content Browser path (`/Game/...`) for recursive scanning |
| `-TargetBP` | Alternative to `-TargetDir`: path to a single asset (`/Game/Path/To/BP_Foo`, without `.uasset` or `_C`) |
| `-Target` | Unified target: `/Game/...BP.BP` for a single asset, otherwise treated as a folder |
| `-OutputDir` | Absolute filesystem path where `*_dump.json` files are written (default: `<Project>/Saved/BlueprintBuster/Dumps`) |
| `-MaxDepth` | Maximum graph tracing depth (default: `64`) |

Result: files like `BP_MyActor_dump.json` will appear in `<DumpsDir>`.

### 4.3. Step 2 — Translate JSON to C++

```bat
python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" ^
    "<DumpsDir>\BP_MyActor_dump.json" ^
    -o "<YourProject>\Source\<Module>\Public\MySystem" ^
    --module-api <YOUR_API>
```

Parameters:

| Key | Purpose |
|---|---|
| Positional argument | Path to a single `*_dump.json` |
| `-o` / `--output` | Output directory for `.h` and `.cpp` (created automatically) |
| `--module-api` | Module export macro: `MYGAME_API`, `MYMODULE_API`, etc. (default: `LADOGA_API`) |

Example output on successful translation:

```
  Header → <YourProject>\Source\<Module>\Public\MySystem\AMyActor.h
  Source → <YourProject>\Source\<Module>\Public\MySystem\AMyActor.cpp
  (47 nodes parsed; 3 custom function(s); 2 marked for manual review)
```

### 4.4. Batch Translation (PowerShell)

```powershell
$Translator = "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py"
$OutDir     = "<YourProject>\Source\<Module>\Public\Generated"
$Api        = "<YOUR_API>"

Get-ChildItem -Path "<DumpsDir>" -Filter "*_dump.json" | ForEach-Object {
    python $Translator $_.FullName -o $OutDir --module-api $Api
}
```

### 4.5. One-shot conversion (Blueprint → JSON → C++)

```bat
"<UE_INSTALL>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
    "<YourProject>\<YourProject>.uproject" ^
    -run=BlueprintBusterConvert ^
    -Plugin="<YourProject>\Plugins\BlueprintBuster\BlueprintBuster.uplugin" ^
    -Target="/Game/Blueprints/BP_MyActor.BP_MyActor" ^
    -NoUI
```

### 4.6. Editor menu entry

When enabled, use:
- Tools → BlueprintBuster: Dump Selected Blueprints to JSON
- Tools → BlueprintBuster: Convert Selected Blueprints to C++

---

## 5. Plugin Structure

```
BlueprintBuster/
├── BlueprintBuster.uplugin          # Descriptor: Editor, PostEngineInit, Win64
│
├── Source/
│   └── BlueprintBuster/
│       ├── BlueprintBuster.Build.cs
│       ├── Public/
│       │   ├── BlueprintBuster.h           # Module entry point, log category
│       │   ├── BlueprintBusterTypes.h      # POD dump structures (FBPDumpData etc.)
│       │   ├── BlueprintBusterParsers.h    # SCS / CDO / Graph parser declarations
│       │   └── BlueprintBusterCommandlet.h # UBlueprintBusterCommandlet declaration
│       └── Private/
│           ├── BlueprintBuster.cpp
│           ├── BlueprintBusterParsers.cpp  # SCS, CDO, Event Graph, Function Graph
│           └── BlueprintBusterCommandlet.cpp # CLI, JSON serialisation, AssetRegistry
│
├── Python/
│   └── bp_translator.py            # JSON → .h + .cpp translator (Python 3.10+)
│
├── README.md                       # This file
└── INSTRUCTION.md                  # Detailed instructions, review checklist
```

### C++ Module Dependencies

**Public:** `Core` · `CoreUObject` · `Engine` · `Json` · `JsonUtilities`

**Private:** `UnrealEd` · `BlueprintGraph` · `Kismet` · `KismetCompiler` · `AssetRegistry` · `AssetTools` · `EditorSubsystem` · `Slate` · `SlateCore` · `ToolMenus` · `Projects`

### Environment Requirements

| Component | Minimum Version |
|---|---|
| Unreal Engine | 5.7 |
| Target Platform | Win64 |
| Python (translator) | 3.10 |
| Python External Dependencies | None (stdlib only) |

---

*BlueprintBuster — open-source UE5 plugin. Version: 1.1.*

---
---

# BlueprintBuster

> **Изолированный переносимый Editor-плагин для Unreal Engine 5.7+**
> Глубокий декомпиляционный анализ Blueprint-ассетов → JSON-дамп → нативный C++ код.

---

## Содержание

1. [Назначение инструмента](#1-назначение-инструмента)
2. [Ключевые возможности](#2-ключевые-возможности)
3. [Стандарты генерируемого C++ кода](#3-стандарты-генерируемого-c-кода)
4. [Краткая инструкция по использованию](#4-краткая-инструкция-по-использованию)
5. [Структура плагина](#5-структура-плагина)

---

## 1. Назначение инструмента

### Проблема

Работа с Blueprint-ассетами в больших проектах неизбежно порождает ряд системных проблем.

| Проблема | Последствие |
|---|---|
| **Спагетти-граф логики** | Сложная визуальная схема из сотен нод не поддаётся code review и рефакторингу |
| **Бинарный формат `.uasset`** | Git не умеет строить human-readable diff; слияния ветвей — ручной процесс |
| **Blueprint VM overhead** | Каждый тик исполняется через интерпретатор, а не нативный машинный код |
| **Ручной перенос в C++** | Огромные трудозатраты, высокий риск регрессий при ручном копировании компонентов, переменных и логики графов |

### Решение

**BlueprintBuster** — изолированный переносимый Editor-плагин для Unreal Engine 5.7+, решающий задачу миграции Blueprint-кода в C++ двумя последовательными шагами.

```
Blueprint .uasset
       │
       ▼
[ UE5 Commandlet ]  ←  глубокий анализ через Reflection API UE
       │                SCS  ·  CDO  ·  Event Graph  ·  Custom Functions
       ▼
  *_dump.json           полная машиночитаемая карта ассета
       │
       ▼
[ bp_translator.py ]  ←  Python 3.10+, stdlib only
       │
       ▼
  MyClass.h + MyClass.cpp   готовый нативный C++ каркас
```

Плагин **не модифицирует** ни один ассет проекта. Все операции — read-only через AssetRegistry и Reflection API.

---

## 2. Ключевые возможности

### 2.1. Автономный C++ Commandlet

Плагин работает через `UnrealEditor-Cmd.exe` — без запуска полного графического Editor'а.
Это позволяет встраивать анализ в CI/CD-пайплайны, скриптовые среды и batch-задачи.

- Обрабатывает **один ассет** по точному пути `/Game/...`
- Обрабатывает **всю директорию рекурсивно** через AssetRegistry-скан
- Принимает параметр `-Plugin=` — загружает модуль на лету без изменения `.uproject` целевого проекта

### 2.2. Deep Component Parsing (SCS)

Полный обход иерархии **Simple Construction Script**.

- Воссоздаёт точное дерево вложенности компонентов (родитель → дочерний)
- Сохраняет имена переменных, типы классов (`UStaticMeshComponent`, `UCapsuleComponent` и т. д.)
- Фиксирует точки аттача (`SetupAttachment`) и socket-имена
- Помечает корневой компонент для корректного `RootComponent = ...` в конструкторе

### 2.3. Smart CDO Extraction

Анализ свойств **Class Default Object** через рефлексию UE.

- Выгружает **только те свойства**, значения которых отличаются от родительского CDO — никакого шума
- Разрешает `EditAnywhere` / `EditDefaultsOnly` / `BlueprintVisible` спецификаторы
- Содержит **хирургическую защиту рефлексии**: перед обращением к каждому свойству выполняется проверка `IsA` владельца — это полностью исключает Assertion-краши при попытке прочитать свойства, принадлежащие исключительно дочерним Blueprint-классам
- Автоматически определяет тип хранения указателя и записывает подсказку (`Hard` / `Soft` / `Class` / `SoftClass` / `Weak`) — транслятор использует её для точного выбора smart-pointer'а

### 2.4. Full Graph AST Compilation

Рекурсивная трассировка Exec Flow с построением дерева AST.

| Тип графа | Точка входа | Поддержка |
|---|---|---|
| Event Graph | `UK2Node_Event` | ✅ |
| Construction Script | `UK2Node_Event` (ConstructionScript) | ✅ |
| Custom Function | `UK2Node_FunctionEntry` | ✅ |
| Custom Function return | `UK2Node_FunctionResult` | ✅ |

**Поддерживаемые типы нод:**

| Нода | Поведение транслятора |
|---|---|
| `UK2Node_CallFunction` | TODO-заглушка с именем функции и целевым классом |
| `UK2Node_IfThenElse` | `if/else` с ветками `True` / `False` |
| `UK2Node_ExecutionSequence` | Линейное исполнение в порядке пинов |
| `UK2Node_VariableGet/Set` | Комментарий с именем переменной |
| `UK2Node_MacroInstance` | TODO-заглушка с именем макроса |
| Любая неизвестная нода | TODO-заглушка с именем класса ноды |

**Сигнатуры кастомных функций** восстанавливаются полностью:
- Входные параметры — из output data-пинов `UK2Node_FunctionEntry`
- Возвращаемые значения — из input data-пинов `UK2Node_FunctionResult`
- Автоматическое приведение типов пинов к типам C++ (см. таблицу ниже)

| Тип пина Blueprint | Тип C++ |
|---|---|
| `bool` | `bool` |
| `byte` | `uint8` |
| `int` | `int32` |
| `int64` | `int64` |
| `float` / `real (float)` | `float` |
| `double` / `real (double)` | `double` |
| `string` | `FString` |
| `name` | `FName` |
| `text` | `FText` |
| `object` | `TObjectPtr<ClassName>` |
| `class` | `TSubclassOf<ClassName>` |
| `softobject` | `TSoftObjectPtr<ClassName>` |
| `softclass` | `TSoftClassPtr<ClassName>` |
| `weakobject` | `TWeakObjectPtr<ClassName>` |
| `struct` | `FStructName` |
| `enum` | `EEnumName` |
| `TArray<T>` | `TArray<T>` |
| `TSet<T>` | `TSet<T>` |
| `TMap<K,V>` | `TMap<K, V>  // TODO: confirm key type` |

### 2.5. Hard Shutdown Fix

При завершении headless-командлета Unreal Engine может аварийно завершаться в процессе уничтожения редакторных подсистем. BlueprintBuster архитектурно обходит этот баг.

После записи последнего JSON-файла Commandlet вызывает **мгновенное аппаратное завершение процесса** через `std::_Exit`, минуя забагованные циклы сборщика мусора и деструкторы редакторных подсистем. Ресурсы операционная система освобождает сама — это корректный и широко применяемый приём для headless UE-утилит.

---

## 3. Стандарты генерируемого C++ кода

Python-транслятор `bp_translator.py` производит код, жёстко соответствующий внутренним стандартам качества проекта. Отступление от любого из правил ниже считается дефектом транслятора, а не поводом для ручного исправления шаблона.

### 3.1. Zero Raw Pointers

Сырые указатели (`UObject*`, `AActor*`) в сгенерированных `.h` файлах **полностью запрещены**.
Транслятор выбирает обёртку автоматически по подсказке `pointerStorageHint` из JSON-дампа:

| Хинт из дампа | Тип в хедере |
|---|---|
| `Hard` | `TObjectPtr<T>` |
| `Soft` | `TSoftObjectPtr<T>` |
| `Class` | `TSubclassOf<T>` |
| `SoftClass` | `TSoftClassPtr<T>` |
| `Weak` | `TWeakObjectPtr<T>` |

### 3.2. Visual Separation

Структура каждого сгенерированного класса строго разделена на два визуальных блока шириной 70 символов:

```cpp
    //************************PROPERTIES************************//
public:
    // ...публичные UPROPERTY...

protected:
    // ...защищённые UPROPERTY...

private:
    // ...приватные поля...

    //************************FUNCTIONS************************//
public:
    AMyClass();
    // ...публичные UFUNCTION...

protected:
    virtual void BeginPlay() override;
    // ...защищённые переопределения...

private:
    // ...
```

Переменные и функции никогда не перемешиваются в одном блоке.

### 3.3. Instance Settings Naming

Все `UPROPERTY`, помеченные как `EditAnywhere` или `EditInstanceOnly`, получают категорию с обязательным слитным префиксом `InstanceSettings`:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InstanceSettings|Dialogue")
float ResponseTimeout = 5.0f;
```

Категории без `InstanceSettings` допустимы только для свойств, не редактируемых на уровне (`EditDefaultsOnly`, `VisibleAnywhere`).

### 3.4. CPU Optimization — Anti-Tick

Конструкторы всех генерируемых классов по умолчанию содержат явное отключение тика:

```cpp
// Actors:
PrimaryActorTick.bCanEverTick = false;

// ActorComponents:
PrimaryComponentTick.bCanEverTick = false;
```

Тик включается **только** если в Blueprint-графе обнаружен нод `ReceiveTick` / `Event Tick`. В этом случае транслятор добавляет inline-комментарий-обоснование рядом со строкой, согласно требованию TDD-06 §2.6.

### 3.5. Legacy Restriction

Транслятор отклоняет генерацию классов, в имени которых содержится запрещённый суффикс, завершая работу с диагностическим сообщением и кодом возврата `2`. Это исключает попадание устаревших аббревиатур из имён Blueprint-ассетов в нативную кодовую базу.

---

## 4. Краткая инструкция по использованию

> Полная инструкция с таблицами параметров, кодами возврата и чеклистом ручной доработки — в файле [`INSTRUCTION.md`](INSTRUCTION.md).

### 4.1. Установка

1. Скопируйте папку `BlueprintBuster/` в директорию `Plugins/` целевого проекта:
   ```
   <YourProject>/
     Plugins/
       BlueprintBuster/
         BlueprintBuster.uplugin
         Source/
         Python/
         README.md
         INSTRUCTION.md
   ```

2. Откройте `<YourProject>.uproject` правым кликом → **Generate Visual Studio project files**.

3. Пересоберите конфигурацию **Development Editor | Win64**.

> Если плагин используется только как инструмент командной строки и не нужен в работающем Editor'е, шаги 2–3 всё равно обязательны — модуль должен быть скомпилирован. Активировать его через `Edit → Plugins` не требуется: флаг `-Plugin=` в команде загрузит его на лету.

### 4.2. Шаг 1 — Экспорт Blueprint'ов в JSON

```bat
"<UE_INSTALL>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
    "<YourProject>\<YourProject>.uproject" ^
    -run=BlueprintBuster ^
    -Plugin="<YourProject>\Plugins\BlueprintBuster\BlueprintBuster.uplugin" ^
    -TargetDir="/Game/Blueprints/MySystem" ^
    -OutputDir="<DumpsDir>" ^
    -NoUI
```

Параметры:

| Ключ | Назначение |
|---|---|
| `<UE_INSTALL>` | Корень установки UE 5.7: `C:\Program Files\Epic Games\UE_5.7` или путь к source build |
| `<YourProject>` | Абсолютный путь к корню UE-проекта |
| `-TargetDir` | Виртуальный путь в Content Browser (`/Game/...`) для рекурсивного сканирования |
| `-TargetBP` | Альтернатива `-TargetDir`: путь к одному ассету (`/Game/Path/To/BP_Foo`, без `.uasset` и `_C`) |
| `-OutputDir` | Абсолютный путь файловой системы, куда записываются `*_dump.json` |
| `-MaxDepth` | Максимальная глубина трассировки графа (default: `64`) |

В результате в `<DumpsDir>` появятся файлы вида `BP_MyActor_dump.json`.

### 4.3. Шаг 2 — Трансляция JSON в C++

```bat
python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" ^
    "<DumpsDir>\BP_MyActor_dump.json" ^
    -o "<YourProject>\Source\<Module>\Public\MySystem" ^
    --module-api <YOUR_API>
```

Параметры:

| Ключ | Назначение |
|---|---|
| Позиционный аргумент | Путь к одному `*_dump.json` |
| `-o` / `--output` | Выходной каталог для `.h` и `.cpp` (создаётся автоматически) |
| `--module-api` | Макрос экспорта модуля: `MYGAME_API`, `MYMODULE_API` и т. п. (default: `LADOGA_API`) |

Пример вывода при успешной трансляции:

```
  Header → <YourProject>\Source\<Module>\Public\MySystem\AMyActor.h
  Source → <YourProject>\Source\<Module>\Public\MySystem\AMyActor.cpp
  (47 nodes parsed; 3 custom function(s); 2 marked for manual review)
```

### 4.4. Пакетная трансляция (PowerShell)

```powershell
$Translator = "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py"
$OutDir     = "<YourProject>\Source\<Module>\Public\Generated"
$Api        = "<YOUR_API>"

Get-ChildItem -Path "<DumpsDir>" -Filter "*_dump.json" | ForEach-Object {
    python $Translator $_.FullName -o $OutDir --module-api $Api
}
```

---

## 5. Структура плагина

```
BlueprintBuster/
├── BlueprintBuster.uplugin          # Дескриптор: Editor, PostEngineInit, Win64
│
├── Source/
│   └── BlueprintBuster/
│       ├── BlueprintBuster.Build.cs
│       ├── Public/
│       │   ├── BlueprintBuster.h           # Точка входа модуля, лог-категория
│       │   ├── BlueprintBusterTypes.h      # POD-структуры дампа (FBPDumpData и др.)
│       │   ├── BlueprintBusterParsers.h    # Объявления парсеров SCS / CDO / Graph
│       │   └── BlueprintBusterCommandlet.h # Объявление UBlueprintBusterCommandlet
│       └── Private/
│           ├── BlueprintBuster.cpp
│           ├── BlueprintBusterParsers.cpp  # SCS, CDO, Event Graph, Function Graph
│           └── BlueprintBusterCommandlet.cpp # CLI, JSON-сериализация, AssetRegistry
│
├── Python/
│   └── bp_translator.py            # JSON → .h + .cpp транслятор (Python 3.10+)
│
├── README.md                       # Этот файл
└── INSTRUCTION.md                  # Подробная инструкция, чеклист доработки
```

### Зависимости C++ модуля

**Public:** `Core` · `CoreUObject` · `Engine` · `Json` · `JsonUtilities`

**Private:** `UnrealEd` · `BlueprintGraph` · `Kismet` · `KismetCompiler` · `AssetRegistry` · `AssetTools` · `EditorSubsystem` · `Slate` · `SlateCore` · `ToolMenus` · `Projects`

### Требования к окружению

| Компонент | Минимальная версия |
|---|---|
| Unreal Engine | 5.7 |
| Целевая платформа | Win64 |
| Python (транслятор) | 3.10 |
| Внешние зависимости Python | Отсутствуют (только stdlib) |

---

*BlueprintBuster — open-source плагин для UE5. Версия: 1.1.*
