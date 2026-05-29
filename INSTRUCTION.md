# BlueprintBuster — Instructions

**Purpose:** isolated Editor plugin for UE 5.7.4 that dumps the structure of Blueprint assets (SCS, CDO, execution graphs) to JSON, after which the external Python script `bp_translator.py` translates the JSON into a C++ class scaffold (`.h` + `.cpp`).

The plugin does not modify project content and does not run at Runtime. All operations are read-only via AssetRegistry and reflection.

---

## 1. Plugin Installation

The plugin is fully self-contained and can be moved to any UE 5.7+ project by simple copy-paste.

### 1.1. Copying

1. Copy the `BlueprintBuster/` directory in its entirety into the `Plugins/` folder of the target project:

   ```
   <YourProject>/
     Plugins/
       BlueprintBuster/
         BlueprintBuster.uplugin
         Source/
         Python/
         INSTRUCTION.md
   ```

   If the `Plugins/` folder does not exist — create it at the same level as `Content/` and `Source/`.

2. Delete (if present) the cached folders `Binaries/`, `Intermediate/`, `DerivedDataCache/` at the project root — this forces a clean recompilation.

### 1.2. Activation and Build

1. Right-click `<YourProject>.uproject` → **Generate Visual Studio project files**.
2. Open the `.sln`, select the **Development Editor | Win64** configuration and rebuild the project (`Build → Build Solution`).
3. Launch the Editor. Open **Edit → Plugins → "Editor" section**, ensure **BlueprintBuster** is enabled. By default the plugin is marked `EnabledByDefault=false`, so on first launch you may need to check the box and restart the Editor.

### 1.3. Verifying Installation

The following line should appear in the Editor log on startup:

```
LogBlueprintBuster: BlueprintBuster module started.
```

If it does not appear — verify that the `.uplugin` was copied correctly and that the module compiled successfully (see `Saved/Logs/` of the target project).

---

## 2. Running the Commandlet

The plugin operates in headless mode via `UnrealEditor-Cmd.exe`. The Editor MUST NOT be running in parallel — it will hold the DDC lock.

All examples use the `-Plugin=` flag with a **direct absolute path** to `.uplugin`. This allows running the Commandlet even if the plugin is not activated in the target project's `.uproject` — UE will load it on the fly.

### 2.1. General Syntax

```
"<UE_INSTALL>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
    "<AbsolutePathToUProject>" ^
    -run=BlueprintBuster ^
    -Plugin="<AbsolutePathToBlueprintBuster.uplugin>" ^
    [-Target="/Game/Path/To/BP_Foo.BP_Foo" | -TargetBP="/Game/Path/To/BP_Foo"  | -TargetDir="/Game/Path"] ^
    [-OutputDir="<AbsolutePathToDumpsDir>"] ^
    [-MaxDepth=64] ^
    [-Verbose] ^
    -NoUI
```

Parameters:

| Key | Required | Purpose |
| --- | --- | --- |
| `-Plugin=` | **required** | absolute path to `BlueprintBuster.uplugin` (lets UE load the module without editing `.uproject`) |
| `-Target=` | optional | unified target; if it contains a dot (`/Game/...BP.BP`) it is treated as a single Blueprint object path, otherwise as a folder |
| `-TargetBP=` | mutually exclusive with `-TargetDir` | path to a single Blueprint as `/Game/...` (WITHOUT extension and WITHOUT `_C` suffix) |
| `-TargetDir=` | mutually exclusive with `-TargetBP` | root folder for recursive Blueprint search |
| `-OutputDir=` | optional | absolute filesystem path where `*_dump.json` files are saved (default: `<Project>/Saved/BlueprintBuster/Dumps`) |
| `-MaxDepth=` | optional, default 64 | maximum graph traversal depth |
| `-Verbose` | optional | verbose log per Blueprint |
| `-NoUI` | recommended | disables all Editor UI windows — required for headless runs |

### 2.2. Example: single asset

Dump `Content/Blueprints/Player/BP_Player.uasset` from project `<YourProject>\<YourProject>.uproject`, plugin resides in `Plugins\BlueprintBuster\` of the same project:

```
"<UE_INSTALL>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<YourProject>\<YourProject>.uproject" -run=BlueprintBuster -Plugin="<YourProject>\Plugins\BlueprintBuster\BlueprintBuster.uplugin" -TargetBP="/Game/Blueprints/Player/BP_Player" -OutputDir="<DumpsDir>" -NoUI
```

Result — file `<DumpsDir>\BP_Player_dump.json`.

### 2.3. Example: entire folder recursively

Parsing an entire dialogue system:

```
"<UE_INSTALL>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<YourProject>\<YourProject>.uproject" -run=BlueprintBuster -Plugin="<YourProject>\Plugins\BlueprintBuster\BlueprintBuster.uplugin" -TargetDir="/Game/Blueprints/Systems/Dialogue" -OutputDir="<DumpsDir>" -NoUI
```

At the end of a run the log will show a summary:

```
LogBlueprintBuster: === BlueprintBuster finished: 4 blueprint(s) dumped ===
```

### 2.4. Path Reference

- `UnrealEditor-Cmd.exe` lives in the **engine**, not the project: the path depends on how UE 5.7 is installed.
  - Epic Launcher (standard): `C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe`
  - Source build (non-standard location): `<UE_SOURCE_ROOT>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe`
- The `.uproject` path is **absolute**, always the first positional argument after `.exe`.
- The `-Plugin=` path points to the `.uplugin` file itself, not the plugin directory.
- Paths in `-TargetBP=` / `-TargetDir=` use the virtual UE convention (`/Game/...`); `-OutputDir=` uses a real filesystem path.

Placeholders used in examples:

| Placeholder | What to substitute |
| --- | --- |
| `<UE_INSTALL>` | UE 5.7 installation root (`C:\Program Files\Epic Games\UE_5.7` or `<UE_SOURCE_ROOT>`) |
| `<YourProject>` | absolute path to the root of the target UE project |
| `<DumpsDir>` | any absolute path where `*_dump.json` files will be saved |

### 2.5. Return Codes

| Code | Meaning |
| --- | --- |
| `0` | all assets processed successfully |
| `1` | invalid arguments (missing `-TargetBP/-TargetDir` or `-OutputDir`) |
| `2` | one or more assets could not be parsed — see log |

In CI it is recommended to check the return code and parse `LogBlueprintBuster` lines.

### 2.6. Common Errors

- **`Asset not found`** — path must start with `/Game/`, without `.uasset`, without `_C`.
- **`OutputDir is required`** — the parameter is mandatory; relative paths are not supported.
- **`Failed to acquire DDC lock`** — the Editor is already running. Close it before launching the Commandlet.
- **`Plugin '...' not found`** — the path in `-Plugin=` is incorrect or the `.uplugin` is corrupted. Verify the file exists and its content is valid JSON.
- **`Could not find module 'BlueprintBuster'`** — the plugin has not been built. Run `Generate Visual Studio project files` and rebuild the `Development Editor | Win64` configuration.

---

## 3. One-shot conversion (Blueprint → JSON → C++)

This commandlet runs the JSON dump and then calls the bundled `Python/bp_translator.py` for each dumped blueprint.

```
"<UE_INSTALL>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
    "<AbsolutePathToUProject>" ^
    -run=BlueprintBusterConvert ^
    -Plugin="<AbsolutePathToBlueprintBuster.uplugin>" ^
    -Target="/Game/Blueprints/BP_Foo.BP_Foo" ^
    [-DumpDir="<AbsolutePathToJsonDir>"] ^
    [-CppDir="<AbsolutePathToCppDir>"] ^
    [-Python="python"] ^
    [-ModuleAPI="YOURMODULE_API"] ^
    [-MaxDepth=64] ^
    -NoUI
```

Notes:
- `-DumpDir` defaults to `<Project>/Saved/BlueprintBuster/Dumps`.
- `-CppDir` defaults to `<Project>/Saved/BlueprintBuster/Cpp`.
- `-Python` defaults to `python` (must be available in PATH).
- `-ModuleAPI` defaults to `<PROJECTNAME>_API`.

---

## 4. Editor menu entry

When the plugin is enabled, open the Editor and use:
- **Tools → BlueprintBuster: Dump Selected Blueprints to JSON**
- **Tools → BlueprintBuster: Convert Selected Blueprints to C++**

Both actions operate on the current Content Browser selection.

## 3. Running the Python Translator

The script `Python/bp_translator.py` is a separate step, independent of UE. Requires Python 3.10+ (`dataclasses` and `typing` are used).

### 3.1. Prerequisites

```
python --version       # must be >= 3.10
```

Dependencies are stdlib-only — no `pip install` required. The script can be invoked by absolute path from any directory — no `cd` needed.

### 3.2. Syntax

```
python <path-to>\bp_translator.py "<input.json>" -o "<output_dir>" [--module-api <API_MACRO>]
```

| Key | Required | Purpose |
| --- | --- | --- |
| `<input.json>` | **required**, positional | path to a single `*_dump.json` generated by the Commandlet |
| `-o` / `--output` | optional | directory where `.h` and `.cpp` are written (default: current `cwd`). Created automatically. |
| `--module-api` | optional | module export macro (default: `LADOGA_API`). For other projects use the appropriate one, e.g. `MYGAME_API`. |

The script always overwrites existing `.h` / `.cpp` without confirmation — ensure the output directory does not match already-edited code.

### 3.3. Example: single file

```
python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" "<DumpsDir>\BP_Player_dump.json" -o "<YourProject>\Source\<Module>\Public\Player" --module-api <YOUR_API>
```

Produces:

```
<YourProject>\Source\<Module>\Public\Player\APlayer.h
<YourProject>\Source\<Module>\Public\Player\APlayer.cpp
```

The class name is derived from the Blueprint name: prefixes `BP_/WBP_/ABP_/BPI_` are stripped, and `A` or `U` is added depending on the parent class. The substring `QC` in names is FORBIDDEN — if triggered, the script will abort with an error and prompt you to rename the source Blueprint.

### 3.4. Example: batch translation of a system

After the Commandlet has dumped the Blueprints of a specific folder to `<DumpsDir>`, translate them file-by-file into the target Source directory:

```bat
:: Translate the main component
python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" "<DumpsDir>\AC_MySystem_dump.json" -o "<YourProject>\Source\<Module>\Public\MySystem" --module-api <YOUR_API>

:: Translate the helper
python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" "<DumpsDir>\BP_MySystemHelper_dump.json" -o "<YourProject>\Source\<Module>\Public\MySystem" --module-api <YOUR_API>

:: Translate the trigger
python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" "<DumpsDir>\A_MySystemTrigger_dump.json" -o "<YourProject>\Source\<Module>\Public\MySystem" --module-api <YOUR_API>
```

Each invocation prints two lines `Header → ...` / `Source → ...` and a summary (`<N> nodes parsed; <M> custom function(s); <K> marked for manual review`).

Placeholders:

| Placeholder | What to substitute |
| --- | --- |
| `<YourProject>` | absolute path to the project root |
| `<DumpsDir>` | directory where the Commandlet wrote the dumps |
| `<Module>` | C++ module name (`MyGame`, `MyGameRuntime`, etc.) |
| `<YOUR_API>` | module export macro (`MYGAME_API`, `MYMODULE_API`) |

### 3.5. Example: batch via PowerShell

To translate an entire dumps folder in one command:

```powershell
Get-ChildItem -Path "<DumpsDir>" -Filter "*_dump.json" |
    ForEach-Object {
        python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" `
            $_.FullName `
            -o "<YourProject>\Source\<Module>\Public\Generated" `
            --module-api <YOUR_API>
    }
```

Bash / Git Bash:

```bash
for f in "<DumpsDir>"/*_dump.json; do
    python "<YourProject>/Plugins/BlueprintBuster/Python/bp_translator.py" \
        "$f" \
        -o "<YourProject>/Source/<Module>/Public/Generated" \
        --module-api <YOUR_API>
done
```

### 3.6. What is generated

In `.h`:
- `UCLASS()` with the correct parent and prefix (`A`/`U`) and the supplied `--module-api` macro.
- Forward-declarations of all classes mentioned in components, CDO and function signatures.
- Block `//******PROPERTIES******//` (public → protected → private), containing:
  - `UPROPERTY()` SCS components as `TObjectPtr<T>`;
  - CDO variables with category `InstanceSettings|<Group>` (only those differing from the parent CDO);
  - pointers wrapped per translator hint: `TObjectPtr` / `TSoftObjectPtr` / `TSubclassOf` / `TSoftClassPtr` / `TWeakObjectPtr`.
- Block `//******FUNCTIONS******//` (public → protected → private):
  - constructor;
  - stubs for `BeginPlay`, `Tick` (if the graph requires it), custom events;
  - **custom Blueprint function declarations** with correct signatures:
    - `UFUNCTION(BlueprintCallable[, BlueprintPure], Category = "BlueprintBuster|Generated")`;
    - parameters converted to C++ types (`bool→bool`, `float→float`, `int→int32`, `string→FString`, `object→TObjectPtr<T>`, `class→TSubclassOf<T>`, containers as `TArray/TSet/TMap`);
    - single return value → return-by-value; multiple → `void` + out-parameters `Out<Name>` by reference;
    - `bIsConst` / `bIsPure` / `bIsBlueprintCallable` flags forwarded from BP.

In `.cpp`:
- Constructor: `PrimaryComponentTick.bCanEverTick = false;` (or `PrimaryActorTick.bCanEverTick = false;` for AActor) — enabled MANUALLY if `Event Tick` is found in the graph.
- `CreateDefaultSubobject<T>(TEXT("Name"))` + `SetupAttachment()` for the full SCS hierarchy.
- `BeginPlay`/`Tick`/event scaffolds with `// TODO:` blocks in their bodies describing graph nodes.
- **Custom function bodies**: `// Inputs:` / `// Returns:` documentation comments, node body (via `_emit_node_chain`), final `return <default>; // TODO: replace with computed result` for single-return functions or a TODO block for out-parameters.

### 3.7. Translator behaviour on non-standard dumps

- **Empty graph** — `.cpp` contains no function bodies, only the constructor. This is valid.
- **Graph too deep** (Commandlet `MaxDepth` exceeded) — `.cpp` will contain `// TODO: graph truncated at depth N — review original Blueprint`.
- **Unknown node types** — replaced with `// TODO: unsupported node <ClassName> — implement manually`. Their count in the dump accumulates in `unsupported_count`.
- **Function without `UK2Node_FunctionResult`** — return type = `void`, body is translated fully.
- **Pin of unknown category** — parameter gets type `int32 /* TODO: unresolved pin type '<cat>' */`, fix manually.

---

## 4. Manual Review Checklist for Generated C++ Code

The translator covers ~70–80% of typical Blueprint nodes. Before committing generated code, **always** go through this list. Every item is a potential regression if skipped.

### 4.1. Structure and Conventions

- [ ] File name = class name without `A/U/F/E/I` prefix (`MyPlayer.h`, NOT `AMyPlayer.h`).
- [ ] `.h` has NO raw pointers (`UClass*`, `AActor*`) in `UPROPERTY` — only wrappers (`TObjectPtr`, `TSoftObjectPtr`, `TSubclassOf`, `TSoftClassPtr`, `TWeakObjectPtr`).
- [ ] `//******PROPERTIES******//` and `//******FUNCTIONS******//` blocks are present; dividers are exactly 70 characters wide.
- [ ] All `UPROPERTY(EditAnywhere/EditDefaultsOnly)` without `BlueprintReadWrite` have a category starting with `InstanceSettings|` (no spaces).
- [ ] Class/struct/variable names contain no `QC` substring (forbidden by project policy).
- [ ] Logging via `UE_LOG(LogLadoga..., ...)`, not `UE_LOG(LogTemp, ...)`.

### 4.2. Tick and Performance

- [ ] Constructor explicitly contains `PrimaryComponentTick.bCanEverTick = false;` (or `PrimaryActorTick.bCanEverTick = false;`).
- [ ] If the original Blueprint contained `Event Tick` and the logic genuinely needs per-frame updates — `bCanEverTick = true` is set and a **justification comment** appears directly above the line (TDD-06 §2.6 requirement).
- [ ] If Tick is actually needed once per second — replace with `FTimerHandle` + `SetTimer` (don't leave Tick enabled).

### 4.3. Custom Blueprint Functions

The translator exports custom functions (Functions tab in the BP editor) with a full signature and body stub. Before committing, verify:

- [ ] **Signature matches** the source BP: function name, number of input/output parameters, their types. Cross-check the JSON block `customFunctions[...]` against `.h`.
- [ ] **Type mapping is correct** for project-specific parameters:
  - BP `Object Reference (X)` → `TObjectPtr<X>` in `.h`;
  - BP `Class Reference (X)` → `TSubclassOf<X>`;
  - BP `Soft Object Reference (X)` → `TSoftObjectPtr<X>`;
  - BP `Soft Class Reference (X)` → `TSoftClassPtr<X>`;
  - BP `Struct (X)` → `FX` (if the name doesn't start with `F` — add prefix manually);
  - BP `Enum (X)` → `X` (no prefix — fix to `EX` if the enum follows project naming convention).
- [ ] **Out-parameters by reference**: when multiple return values exist, the translator makes the function `void` and adds `Out<Name>` parameters. Ensure callers pass references and all Out-parameters are actually assigned before `return`.
- [ ] **Single return type**: for functions with one return value, the translator leaves `return <default>; // TODO: replace with computed result` — replace with the computed result.
- [ ] **`const` methods**: functions marked `Const` in BP get a `const` suffix in C++. Verify no class members are modified in the body; if they are — remove the flag or use `mutable`.
- [ ] **`BlueprintPure`**: Pure functions require no exec pins and no side effects in UE. If the C++ body calls something with side effects — remove `BlueprintPure`, keep only `BlueprintCallable`.
- [ ] **Function category**: auto-generation sets `Category = "BlueprintBuster|Generated"`. Replace with project-specific category (e.g. `Category = "Ladoga|Dialogue"`), otherwise assets will appear noisy in BP.
- [ ] **Function name doesn't conflict** with virtual methods of the parent. If a BP function is named `BeginPlay`, `Tick`, `EndPlay` — rename it.
- [ ] **Function graphs without `UK2Node_FunctionEntry`** are absent from JSON — this is normal; the parser ignores such empty entries.

### 4.4. Complex Graph Nodes

The translator leaves `// TODO:` for the following categories — each TODO must be resolved:

- [ ] **Macro Nodes** (`UK2Node_MacroInstance`) — expand manually into corresponding C++ (`ForLoop`, `WhileLoop`, `IsValid`, `Multi-Branch`, custom macros). The translator only knows of their presence, not their contents.
- [ ] **Custom Macro Libraries** — look up the source macro in the Editor and port the logic.
- [ ] **Delegate bindings** (Bind Event to ..., Assign on ...) — replace with `AddDynamic(this, &ThisClass::OnEvent)` in `BeginPlay` and `RemoveDynamic` in `EndPlay`.
- [ ] **Timeline nodes** — convert to `UCurveFloat` + `FTimeline` or `UMovieSceneSequence`. The translator does not serialise Timelines.
- [ ] **Latent nodes** (`Delay`, `MoveComponentTo`, `Move To Location or Actor`) — replace with `FTimerHandle`, `FLatentActionManager`, `UAITask`.
- [ ] **Cast Nodes** — reconsider architecture: TDD-06 §2.6 forbids Cast chains. If a cast is genuinely needed — use an interface or `GetClass()->IsChildOf()`.
- [ ] **MultiCast Delegate Broadcast** — verify that `AddDynamic` subscriptions are alive at the time of Broadcast.
- [ ] **For Each Loop with Break** — expand into a regular `for` with `break`; the Blueprint node has no direct C++ analogue.
- [ ] **Construct Object from Class / Spawn Actor** — specify a correct `Owner`, `Instigator`, `SpawnParameters.SpawnCollisionHandlingOverride`.
- [ ] **Get All Actors of Class** — critical: verify the call is not in Tick. Replace with a cached list from a subsystem (`UGameplayMessageSubsystem` or the relevant `USubsystem`).

### 4.5. GAS / GameplayTags

- [ ] If the Blueprint worked with `AbilitySystemComponent` — all tags are accessed via `FGameplayTag::RequestGameplayTag(FName("..."), false)` inside a **function** or a static initialiser inside `Initialize()`, **never** at file-scope (triggers `ensure(false)`).
- [ ] Direct attribute writes (Health, Stamina) replaced with `GE_*` or `ApplyGameplayEffectSpecToSelf`.
- [ ] No `SetGlobalTimeDilation` — Hit-Stop via `Montage_SetPlayRate` on the actor.

### 4.6. Network and Save/Load Correctness

- [ ] All `UPROPERTY` marked `SaveGame` in the original BP retain the same specifier + a persistence level comment (`// [RUN]` / `// [PERMANENT]` / `// [SESSION]`).
- [ ] `Replicated`/`ReplicatedUsing` are preserved and `GetLifetimeReplicatedProps` is added.
- [ ] `RPC` markers (`Server`, `Client`, `NetMulticast`, `Reliable/Unreliable`) are transferred manually — the translator does not determine them.

### 4.7. Pointers and Validity

- [ ] `IsValid(...)` is present before every `TObjectPtr` dereference.
- [ ] `TWeakObjectPtr` is dereferenced via `.Get()` with a null check.
- [ ] `TSoftObjectPtr<T>` — async `LoadAsync` via `FStreamableManager`, not synchronous `LoadSynchronous` inside Tick.

### 4.8. Includes and Dependencies

- [ ] `.h` uses forward declarations; full `#include` is in `.cpp`.
- [ ] Include order: `CoreMinimal.h` → Engine → Plugin → Project → `*.generated.h`.
- [ ] Remove `#include "AbilitySystemBlueprintLibrary.h"` and similar if not actually used.
- [ ] `.Build.cs` contains all modules required during manual refinement (`GameplayAbilities`, `EnhancedInput`, `MotionWarping`, etc.).

### 4.9. Compilation and Smoke Test

- [ ] Build passes with **0 warnings** (zero-warnings policy).
- [ ] A Blueprint child class of the generated C++ class is created in the Editor.
- [ ] A level is opened, an Actor is spawned — `BeginPlay` fires, `Output Log` shows no `ensure`/`check`/`Warning`.
- [ ] If the Blueprint had input — verify that Input Action works in C++ via `EnhancedInputComponent`.

### 4.10. Final Sanity Check

- [ ] Diff of generated `.h`/`.cpp` visually matches the original Blueprint: component count in SCS matches, event count in graph matches, **custom function count matches**, no missing variables.
- [ ] Field `unsupported_count` in the source `*_dump.json` equals zero — otherwise revisit all `// TODO: unsupported` entries.
- [ ] Every entry in `customFunctions[]` JSON is reflected in `.h` — each has a declaration and an implementation in `.cpp`.
- [ ] Generated class passes the Code Review checklist (CLAUDE.md §9).

---

## 5. Limitations

- The plugin **does not preserve** values of complex CDO fields (structs, arrays, maps) — they get a default-construct in `.h` and `// TODO: copy value from original BP CDO` in `.cpp`.
- Construction Script body is **not parsed** as a graph — only its presence is recorded in JSON; the body remains outside automation. Custom functions (Functions tab in the BP editor) are fully parsed starting from the plugin version with `customFunctions` support.
- Default values of custom function parameters (BP allows them via "Set defaults" in Details) **are not transferred** — C++ signature parameters have no default values. Add them manually if needed.
- Function metadata (`Tooltip`, `Keywords`, `CompactNodeTitle`) **is not exported** — add manually in the `meta=()` section of `UFUNCTION`.
- Animation (AnimBlueprint State Machines, Blend Spaces) — **not supported**. Convert ABP manually or use specialised UE5 tools (`Anim Node Reference`).
- Widget Blueprints (UMG): SCS will produce a widget tree, graph — events; visual bindings and font materials must be converted manually.

---

## 6. Security and Environment Constraints

- The plugin is read-only with respect to project content: no asset is modified or saved back to `.uasset`.
- All artefacts (JSON, `.h`/`.cpp`) are written **only** to the directory specified in `-OutputDir` / `--output`. No writes to `Content/`.
- No telemetry, network calls or access to external services. All dependencies are UE stdlib and Python stdlib.

---

*Instruction version: 1.2. Changes 1.1 → 1.2: all specific paths replaced with abstract placeholders (`<UE_INSTALL>`, `<YourProject>`, `<DumpsDir>`, `<Module>`, `<YOUR_API>`); placeholder tables added in §2.4 and §3.4; engine version corrected to UE 5.7 in all examples.*

---
---

# BlueprintBuster — Инструкция

**Назначение:** изолированный Editor-плагин для UE 5.7.4, который выгружает структуру Blueprint-ассетов (SCS, CDO, граф исполнения) в JSON, после чего внешний Python-скрипт `bp_translator.py` транслирует JSON в каркас C++ классов (`.h` + `.cpp`).

Плагин не модифицирует контент проекта и не выполняется в Runtime. Все операции — read-only через AssetRegistry и reflection.

---

## 1. Установка плагина

Плагин полностью самодостаточен и переносится в любой UE 5.7+ проект простым копированием.

### 1.1. Копирование

1. Скопируйте директорию `BlueprintBuster/` целиком в папку `Plugins/` целевого проекта:

   ```
   <YourProject>/
     Plugins/
       BlueprintBuster/
         BlueprintBuster.uplugin
         Source/
         Python/
         INSTRUCTION.md
   ```

   Если папки `Plugins/` нет — создайте её на одном уровне с `Content/` и `Source/`.

2. Удалите (если присутствуют) кэшируемые папки `Binaries/`, `Intermediate/`, `DerivedDataCache/` в корне проекта — это форсирует чистую перекомпиляцию.

### 1.2. Активация и сборка

1. Откройте `<YourProject>.uproject` правым кликом → **Generate Visual Studio project files**.
2. Откройте `.sln`, выберите конфигурацию **Development Editor | Win64** и пересоберите проект (`Build → Build Solution`).
3. Запустите Editor. Откройте **Edit → Plugins → раздел "Editor"**, убедитесь что **BlueprintBuster** включён. По умолчанию плагин помечен `EnabledByDefault=false`, поэтому при первом запуске может потребоваться поставить галочку и перезапустить Editor.

### 1.3. Проверка установки

В логе Editor при старте должна появиться строка:

```
LogBlueprintBuster: BlueprintBuster module started.
```

Если её нет — проверьте, что `.uplugin` корректно скопирован, и что модуль успешно собрался (см. `Saved/Logs/` целевого проекта).

---

## 2. Запуск Commandlet

Плагин работает в headless-режиме через `UnrealEditor-Cmd.exe`. Editor НЕ должен быть открыт параллельно — он удержит блокировку DDC.

Все примеры используют флаг `-Plugin=` с **прямым абсолютным путём** к `.uplugin`. Это позволяет запускать Commandlet, даже если плагин не активирован в `.uproject` целевого проекта — UE подгрузит его на лету.

### 2.1. Общий синтаксис

```
"<UE_INSTALL>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
    "<AbsolutePathToUProject>" ^
    -run=BlueprintBuster ^
    -Plugin="<AbsolutePathToBlueprintBuster.uplugin>" ^
    [-TargetBP="/Game/Path/To/BP_Foo"  | -TargetDir="/Game/Path"] ^
    -OutputDir="<AbsolutePathToDumpsDir>" ^
    [-MaxDepth=64] ^
    [-Verbose] ^
    -NoUI
```

Параметры:

| Ключ | Обязательность | Назначение |
| --- | --- | --- |
| `-Plugin=` | **обязателен** | абсолютный путь к `BlueprintBuster.uplugin` (даёт UE загрузить модуль без правки `.uproject`) |
| `-TargetBP=` | взаимоисключающий с `-TargetDir` | путь к одному Blueprint в виде `/Game/...` (БЕЗ расширения и БЕЗ суффикса `_C`) |
| `-TargetDir=` | взаимоисключающий с `-TargetBP` | корневая папка для рекурсивного поиска Blueprint'ов |
| `-OutputDir=` | **обязателен** | абсолютный путь в файловой системе, куда сохраняются `*_dump.json` |
| `-MaxDepth=` | опционально, default 64 | максимальная глубина обхода графа |
| `-Verbose` | опционально | расширенный лог по каждому Blueprint |
| `-NoUI` | рекомендуется | отключает все UI-окна Editor'а — обязательно для headless-прогона |

### 2.2. Пример: один ассет

Выгрузить `Content/Blueprints/Player/BP_Player.uasset` из проекта `<YourProject>\<YourProject>.uproject`, плагин лежит в `Plugins\BlueprintBuster\` того же проекта:

```
"<UE_INSTALL>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<YourProject>\<YourProject>.uproject" -run=BlueprintBuster -Plugin="<YourProject>\Plugins\BlueprintBuster\BlueprintBuster.uplugin" -TargetBP="/Game/Blueprints/Player/BP_Player" -OutputDir="<DumpsDir>" -NoUI
```

Результат — файл `<DumpsDir>\BP_Player_dump.json`.

### 2.3. Пример: вся папка рекурсивно

Парсинг всей системы диалогов:

```
"<UE_INSTALL>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<YourProject>\<YourProject>.uproject" -run=BlueprintBuster -Plugin="<YourProject>\Plugins\BlueprintBuster\BlueprintBuster.uplugin" -TargetDir="/Game/Blueprints/Systems/Dialogue" -OutputDir="<DumpsDir>" -NoUI
```

В конце прогона в логе появится сводка:

```
LogBlueprintBuster: === BlueprintBuster finished: 4 blueprint(s) dumped ===
```

### 2.4. Подсказки по путям

- `UnrealEditor-Cmd.exe` лежит в **движке**, а не в проекте: путь зависит от того, как установлен UE 5.7.
  - Epic Launcher (стандартный): `C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe`
  - Source build (нестандартное расположение): `<UE_SOURCE_ROOT>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe`
- Путь к `.uproject` — **абсолютный**, всегда первым позиционным аргументом после `.exe`.
- Путь в `-Plugin=` указывает именно на файл `.uplugin`, не на директорию плагина.
- Путь в `-TargetBP=` / `-TargetDir=` использует виртуальную UE-конвенцию (`/Game/...`), а в `-OutputDir=` — реальный путь файловой системы.

Плейсхолдеры в примерах:

| Плейсхолдер | Что подставить |
| --- | --- |
| `<UE_INSTALL>` | корень установки UE 5.7 (`C:\Program Files\Epic Games\UE_5.7` или `<UE_SOURCE_ROOT>`) |
| `<YourProject>` | абсолютный путь к корню целевого UE-проекта |
| `<DumpsDir>` | любой абсолютный путь, куда будут сохраняться `*_dump.json` |

### 2.5. Коды возврата

| Код | Значение |
| --- | --- |
| `0` | все ассеты обработаны успешно |
| `1` | неверные аргументы (отсутствует `-TargetBP/-TargetDir` или `-OutputDir`) |
| `2` | один или более ассетов не удалось распарсить — см. лог |

В CI рекомендуется проверять код возврата и парсить `LogBlueprintBuster` строки.

### 2.6. Типовые ошибки

- **`Asset not found`** — путь должен начинаться с `/Game/`, без `.uasset`, без `_C`.
- **`OutputDir is required`** — параметр обязателен, относительные пути не поддерживаются.
- **`Failed to acquire DDC lock`** — Editor уже запущен. Закройте его перед запуском Commandlet.
- **`Plugin '...' not found`** — путь в `-Plugin=` неверный либо `.uplugin` повреждён. Проверьте, что файл существует и его содержимое — валидный JSON.
- **`Could not find module 'BlueprintBuster'`** — плагин не собран. Запустите `Generate Visual Studio project files` и пересоберите конфигурацию `Development Editor | Win64`.

---

## 3. Запуск Python-транслятора

Скрипт `Python/bp_translator.py` — отдельный шаг, не зависит от UE. Требует Python 3.10+ (используются `dataclasses` и `typing`).

### 3.1. Подготовка

```
python --version       # должно быть >= 3.10
```

Зависимости из stdlib — никаких `pip install` не требуется. Скрипт можно вызывать по абсолютному пути из любого каталога — `cd` не требуется.

### 3.2. Синтаксис

```
python <path-to>\bp_translator.py "<input.json>" -o "<output_dir>" [--module-api <API_MACRO>]
```

| Ключ | Обязательность | Назначение |
| --- | --- | --- |
| `<input.json>` | **обязателен**, позиционный | путь к одному `*_dump.json`, сгенерированному Commandlet'ом |
| `-o` / `--output` | опционально | каталог, куда записываются `.h` и `.cpp` (default: текущая `cwd`). Создаётся автоматически. |
| `--module-api` | опционально | макрос экспорта модуля (default: `LADOGA_API`). Для других проектов укажите соответствующий, например `MYGAME_API`. |

Скрипт всегда перезаписывает существующие `.h` / `.cpp` без подтверждения — следите за тем, что выходной каталог не совпадает с уже отредактированным кодом.

### 3.3. Пример: один файл

```
python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" "<DumpsDir>\BP_Player_dump.json" -o "<YourProject>\Source\<Module>\Public\Player" --module-api <YOUR_API>
```

Создаст:

```
<YourProject>\Source\<Module>\Public\Player\APlayer.h
<YourProject>\Source\<Module>\Public\Player\APlayer.cpp
```

Имя класса выводится из имени Blueprint: префиксы `BP_/WBP_/ABP_/BPI_` отрезаются, добавляется `A` или `U` в зависимости от родителя. Подстрока `QC` в именах ЗАПРЕЩЕНА — если триггерится, скрипт прервётся с ошибкой и предложит переименовать исходный Blueprint.

### 3.4. Пример: пакетная трансляция одной системы

После того как Commandlet выгрузил Blueprint'ы конкретной папки в `<DumpsDir>`, транслируем их пофайлово в целевой Source-каталог:

```bat
:: Транслируем основной компонент
python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" "<DumpsDir>\AC_MySystem_dump.json" -o "<YourProject>\Source\<Module>\Public\MySystem" --module-api <YOUR_API>

:: Транслируем вспомогательный Helper
python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" "<DumpsDir>\BP_MySystemHelper_dump.json" -o "<YourProject>\Source\<Module>\Public\MySystem" --module-api <YOUR_API>

:: Транслируем триггер
python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" "<DumpsDir>\A_MySystemTrigger_dump.json" -o "<YourProject>\Source\<Module>\Public\MySystem" --module-api <YOUR_API>
```

Каждый вызов выводит две строки `Header → ...` / `Source → ...` и сводку (`<N> nodes parsed; <M> custom function(s); <K> marked for manual review`).

Плейсхолдеры:

| Плейсхолдер | Что подставить |
| --- | --- |
| `<YourProject>` | абсолютный путь к корню проекта |
| `<DumpsDir>` | каталог, куда записал дампы Commandlet |
| `<Module>` | имя C++-модуля (`MyGame`, `MyGameRuntime`, и т. п.) |
| `<YOUR_API>` | макрос экспорта модуля (`MYGAME_API`, `MYMODULE_API`) |

### 3.5. Пример: batch через PowerShell

Если нужно перевести всю папку дампов одной командой:

```powershell
Get-ChildItem -Path "<DumpsDir>" -Filter "*_dump.json" |
    ForEach-Object {
        python "<YourProject>\Plugins\BlueprintBuster\Python\bp_translator.py" `
            $_.FullName `
            -o "<YourProject>\Source\<Module>\Public\Generated" `
            --module-api <YOUR_API>
    }
```

Bash / Git Bash:

```bash
for f in "<DumpsDir>"/*_dump.json; do
    python "<YourProject>/Plugins/BlueprintBuster/Python/bp_translator.py" \
        "$f" \
        -o "<YourProject>/Source/<Module>/Public/Generated" \
        --module-api <YOUR_API>
done
```

### 3.6. Что генерируется

В `.h`:
- `UCLASS()` с правильным родителем и префиксом (`A`/`U`) и переданным `--module-api` макросом.
- Forward-declarations всех классов, упомянутых в компонентах, CDO и сигнатурах функций.
- Блок `//******PROPERTIES******//` (public → protected → private), в нём:
  - `UPROPERTY()` SCS-компоненты как `TObjectPtr<T>`;
  - переменные CDO с категорией `InstanceSettings|<Group>` (только те, что отличаются от родительского CDO);
  - указатели обёрнуты согласно подсказке транслятора: `TObjectPtr` / `TSoftObjectPtr` / `TSubclassOf` / `TSoftClassPtr` / `TWeakObjectPtr`.
- Блок `//******FUNCTIONS******//` (public → protected → private):
  - конструктор;
  - стабы `BeginPlay`, `Tick` (если граф требует), кастомных событий;
  - **объявления кастомных Blueprint-функций** с корректной сигнатурой:
    - `UFUNCTION(BlueprintCallable[, BlueprintPure], Category = "BlueprintBuster|Generated")`;
    - параметры конвертируются в C++-типы (`bool→bool`, `float→float`, `int→int32`, `string→FString`, `object→TObjectPtr<T>`, `class→TSubclassOf<T>`, контейнеры в `TArray/TSet/TMap`);
    - одно возвращаемое значение → return-by-value; несколько → `void` + out-параметры `Out<Name>` по ссылке;
    - флаги `bIsConst` / `bIsPure` / `bIsBlueprintCallable` пробрасываются из BP.

В `.cpp`:
- Конструктор: `PrimaryComponentTick.bCanEverTick = false;` (или `PrimaryActorTick.bCanEverTick = false;` для AActor) — включается ВРУЧНУЮ, если в графе встречен `Event Tick`.
- `CreateDefaultSubobject<T>(TEXT("Name"))` + `SetupAttachment()` для всей SCS-иерархии.
- Каркасы `BeginPlay`/`Tick`/событий, в теле — `// TODO:` блоки с краткими описаниями узлов из графа.
- **Тела кастомных функций**: документирующие комментарии `// Inputs:` / `// Returns:`, узлы тела (через `_emit_node_chain`), финальный `return <default>; // TODO: replace with computed result` для функций с одним возвратом или TODO-блок для out-параметров.

### 3.7. Поведение скрипта на нестандартных дампах

- **Пустой граф** — `.cpp` функций не содержит, только конструктор. Это валидно.
- **Слишком глубокий граф** (превышен `MaxDepth` в Commandlet'е) — в `.cpp` появится `// TODO: graph truncated at depth N — review original Blueprint`.
- **Узлы неизвестного типа** — заменяются на `// TODO: unsupported node <ClassName> — implement manually`. Их количество в дампе суммируется в `unsupported_count`.
- **Функция без `UK2Node_FunctionResult`** — return-тип = `void`, тело транслируется полностью.
- **Pin неизвестной категории** — параметр получит тип `int32 /* TODO: unresolved pin type '<cat>' */`, поправить вручную.

---

## 4. Чеклист ручной доработки сгенерированного C++ кода

Транслятор покрывает ~70–80% типовых Blueprint-узлов. Перед коммитом сгенерированного кода **обязательно** пройдитесь по списку. Каждый пункт — потенциальная регрессия, если пропущен.

### 4.1. Структура и конвенции

- [ ] Имя файла = имя класса без префикса `A/U/F/E/I` (`LadogaPlayer.h`, НЕ `ALadogaPlayer.h`).
- [ ] В `.h` НЕТ "сырых" указателей (`UClass*`, `AActor*`) в `UPROPERTY` — только обёртки (`TObjectPtr`, `TSoftObjectPtr`, `TSubclassOf`, `TSoftClassPtr`, `TWeakObjectPtr`).
- [ ] Блоки `//******PROPERTIES******//` и `//******FUNCTIONS******//` присутствуют, разделители ровно 70 символов.
- [ ] Все `UPROPERTY(EditAnywhere/EditDefaultsOnly)` без `BlueprintReadWrite` имеют категорию, начинающуюся с `InstanceSettings|` (без пробелов).
- [ ] В именах классов/структур/переменных нет подстроки `QC` (запрещено политикой проекта).
- [ ] Логирование через `UE_LOG(LogLadoga..., ...)`, а не `UE_LOG(LogTemp, ...)`.

### 4.2. Tick и производительность

- [ ] В конструкторе явно стоит `PrimaryComponentTick.bCanEverTick = false;` (или `PrimaryActorTick.bCanEverTick = false;`).
- [ ] Если оригинальный Blueprint содержал `Event Tick`, и логика действительно нужна каждый кадр — `bCanEverTick = true` выставлен и есть **комментарий-обоснование** прямо над строкой (требование TDD-06 §2.6).
- [ ] Если Tick на самом деле нужен раз в секунду — заменить на `FTimerHandle` + `SetTimer` (не оставлять Tick).

### 4.3. Кастомные Blueprint-функции

Транслятор экспортирует кастомные функции (Functions tab в BP редакторе) с полной сигнатурой и стабом тела. Перед коммитом проверить:

- [ ] **Сигнатура совпадает** с исходным BP: имя функции, число входных/выходных параметров, их типы. Сверьте JSON-блок `customFunctions[...]` с `.h`.
- [ ] **Маппинг типов корректен** для специфических параметров проекта:
  - BP `Object Reference (X)` → `TObjectPtr<X>` в `.h`;
  - BP `Class Reference (X)` → `TSubclassOf<X>`;
  - BP `Soft Object Reference (X)` → `TSoftObjectPtr<X>`;
  - BP `Soft Class Reference (X)` → `TSoftClassPtr<X>`;
  - BP `Struct (X)` → `FX` (если имя не начинается с `F` — добавить вручную);
  - BP `Enum (X)` → `X` (без префикса — поправить на `EX` если энам в проекте именуется по конвенции).
- [ ] **Out-параметры по ссылке**: при наличии нескольких возвращаемых значений транслятор делает функцию `void` и добавляет параметры `Out<Name>`. Убедиться, что вызывающий код передаёт ссылку, и что все Out-параметры реально присваиваются перед `return`.
- [ ] **Один return-тип**: для функций с одним возвращаемым значением транслятор оставляет `return <default>; // TODO: replace with computed result` — заменить на вычисление результата.
- [ ] **`const`-методы**: функции, помеченные в BP как `Const`, получают суффикс `const` в C++. Проверить, что внутри тела нет модификации членов класса; если есть — снять флаг или использовать `mutable`.
- [ ] **`BlueprintPure`**: для Pure-функций UE требует отсутствия exec-пинов и побочных эффектов. Если C++-тело вызывает что-то с side effects — снять `BlueprintPure`, оставить только `BlueprintCallable`.
- [ ] **Категория функции**: автогенерация ставит `Category = "BlueprintBuster|Generated"`. Заменить на проектную (`Category = "Ladoga|Dialogue"` и т. п.), иначе ассеты будут визуально шуметь в BP.
- [ ] **Имя функции не конфликтует** с виртуальными методами родителя. Если BP-функция называется `BeginPlay`, `Tick`, `EndPlay` — переименовать.
- [ ] **Function-graphs без `UK2Node_FunctionEntry`** в JSON отсутствуют — это нормально, такие пустышки игнорируются парсером.

### 4.4. Сложные узлы графа

Транслятор оставляет `// TODO:` для следующих категорий — каждое TODO должно быть закрыто:

- [ ] **Macro Nodes** (`UK2Node_MacroInstance`) — раскрыть вручную в соответствующий C++ (`ForLoop`, `WhileLoop`, `IsValid`, `Multi-Branch`, custom macros). Транслятор знает только об их наличии, не о содержимом.
- [ ] **Custom Macro Libraries** проекта — посмотреть исходный макрос в Editor, перенести логику.
- [ ] **Delegate bindings** (Bind Event to ..., Assign on ...) — заменить на `AddDynamic(this, &ThisClass::OnEvent)` в `BeginPlay` и `RemoveDynamic` в `EndPlay`.
- [ ] **Timeline nodes** — конвертировать в `UCurveFloat` + `FTimeline` либо в `UMovieSceneSequence`. Транслятор Timeline'ы не сериализует.
- [ ] **Latent nodes** (`Delay`, `MoveComponentTo`, `Move To Location or Actor`) — заменить на `FTimerHandle`, `FLatentActionManager`, `UAITask`. Узел сохранён как TODO с длительностью.
- [ ] **Cast Nodes** — пересмотреть архитектуру: TDD-06 §2.6 запрещает Cast-цепочки. Если каст реально нужен — использовать интерфейс или `GetClass()->IsChildOf()`.
- [ ] **MultiCast Delegate Broadcast** — проверить, что подписки `AddDynamic` живы на момент Broadcast'а.
- [ ] **For Each Loop с Break** — раскрыть в обычный `for` с `break`; Blueprint-узел в C++ не имеет прямого аналога.
- [ ] **Construct Object from Class / Spawn Actor** — указать корректный `Owner`, `Instigator`, `SpawnParameters.SpawnCollisionHandlingOverride`.
- [ ] **Get All Actors of Class** — критично: проверить, что вызов не в Tick. Заменить на кэшированный список из подсистемы (`UGameplayMessageSubsystem` или соответствующий `USubsystem`).

### 4.5. GAS / GameplayTags

- [ ] Если Blueprint работал с `AbilitySystemComponent` — все теги обращены через `FGameplayTag::RequestGameplayTag(FName("..."), false)` в **функции** или статический инициализатор внутри `Initialize()`, **никогда** на file-scope (триггерит `ensure(false)`).
- [ ] Прямые правки атрибутов (Health, Stamina) заменены на `GE_*` либо `ApplyGameplayEffectSpecToSelf`.
- [ ] Никакого `SetGlobalTimeDilation` — Hit-Stop через `Montage_SetPlayRate` на актёре.

### 4.6. Сетевая и Save/Load корректность

- [ ] Все `UPROPERTY` с пометкой `SaveGame` в оригинальном BP сохранены с тем же спецификатором + комментарий уровня persistence (`// [RUN]` / `// [PERMANENT]` / `// [SESSION]`).
- [ ] `Replicated`/`ReplicatedUsing` сохранены, добавлен `GetLifetimeReplicatedProps`.
- [ ] `RPC` пометки (`Server`, `Client`, `NetMulticast`, `Reliable/Unreliable`) перенесены вручную — транслятор их не определяет.

### 4.7. Указатели и валидность

- [ ] Перед каждым разыменованием `TObjectPtr` стоит `IsValid(...)`.
- [ ] `TWeakObjectPtr` разыменовывается через `.Get()` с проверкой результата.
- [ ] `TSoftObjectPtr<T>` — асинхронный `LoadAsync` через `FStreamableManager`, не синхронный `LoadSynchronous` в Tick.

### 4.8. Includes и зависимости

- [ ] В `.h` — forward declarations; полный `#include` перенесён в `.cpp`.
- [ ] Порядок includes: `CoreMinimal.h` → Engine → Plugin → Project → `*.generated.h`.
- [ ] Удалить `#include "AbilitySystemBlueprintLibrary.h"` и подобные, если фактически не используется.
- [ ] В `.Build.cs` добавлены модули, потребовавшиеся при ручной доработке (`GameplayAbilities`, `EnhancedInput`, `MotionWarping` и т.п.).

### 4.9. Компиляция и smoke test

- [ ] Сборка проходит с **0 warning'ов** (политика zero-warnings).
- [ ] В Editor создан Blueprint-наследник от сгенерированного C++ класса.
- [ ] Открыт уровень, заспавнен Actor — `BeginPlay` отрабатывает, в `Output Log` нет `ensure`/`check`/`Warning`.
- [ ] Если Blueprint имел вход (input) — проверить, что Input Action работает в C++ через `EnhancedInputComponent`.

### 4.10. Финальный sanity check

- [ ] Diff сгенерированных `.h`/`.cpp` визуально соответствует оригинальному Blueprint: количество компонентов в SCS совпадает, количество событий в графе совпадает, **количество кастомных функций совпадает**, нет "потерянных" переменных.
- [ ] Поле `unsupported_count` из исходного `*_dump.json` равно нулю — иначе пройтись по `// TODO: unsupported` ещё раз.
- [ ] Поле `customFunctions[]` из JSON полностью отражено в `.h` — для каждой записи есть объявление, для каждой — реализация в `.cpp`.
- [ ] Сгенерированный класс соответствует Code Review чеклисту (CLAUDE.md §9).

---

## 5. Ограничения

- Плагин **не сохраняет** значения сложных CDO-полей (структуры, массивы, мапы) — для них в `.h` стоит only-default-construct, в `.cpp` — `// TODO: copy value from original BP CDO`.
- Construction Script содержимое **не парсится** как граф — только сам факт его наличия попадает в JSON, тело остаётся за пределами автоматизации. Custom-функции (вкладка Functions в BP-редакторе) парсятся полноценно, начиная с версии плагина с поддержкой `customFunctions`.
- Default-значения параметров кастомных функций (BP допускает их через "Set defaults" в Details) **не переносятся** — в C++ сигнатуре параметры без значений по умолчанию. Проставить вручную при необходимости.
- Метаданные функций (`Tooltip`, `Keywords`, `CompactNodeTitle`) **не экспортируются** — добавляются вручную в `meta=()` секции `UFUNCTION`.
- Анимация (AnimBlueprint State Machines, Blend Spaces) — **не поддерживается**. ABP конвертируйте вручную, либо используйте узкоспециализированные инструменты UE5 (`Anim Node Reference`).
- Widget Blueprints (UMG): SCS даст дерево виджетов, граф — события; визуальные binding'и и материалы шрифтов конвертировать вручную.

---

## 6. Безопасность и ограничения окружения

- Плагин read-only по отношению к контенту проекта: ни один ассет не модифицируется и не сохраняется обратно в `.uasset`.
- Все артефакты (JSON, `.h`/`.cpp`) пишутся **только** в каталог, указанный в `-OutputDir` / `--output`. Никаких записей в `Content/`.
- Никакой телеметрии, сетевых вызовов, обращений к внешним сервисам. Все зависимости — stdlib UE и stdlib Python.

---

*Версия инструкции: 1.2. Изменения от 1.1 → 1.2: все конкретные пути заменены на абстрактные плейсхолдеры (`<UE_INSTALL>`, `<YourProject>`, `<DumpsDir>`, `<Module>`, `<YOUR_API>`); добавлены таблицы плейсхолдеров в §2.4 и §3.4; исправлена версия движка на UE 5.7 во всех примерах.*
