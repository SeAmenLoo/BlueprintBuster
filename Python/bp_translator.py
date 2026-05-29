#!/usr/bin/env python3
# Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
# Plugin: BlueprintBuster
"""
bp_translator.py — converts BlueprintBuster JSON dumps to UE5 C++ header/source pairs.

Usage:
    python bp_translator.py <dump.json> [-o <output_dir>] [--module-api LADOGA_API]

Output:
    <output_dir>/<ClassName>.h
    <output_dir>/<ClassName>.cpp

The generator enforces the project's coding standards (TDD-06 + user override):
  * Headers contain ONLY TObjectPtr / TSoftObjectPtr / TWeakObjectPtr / TSoftClassPtr — no raw pointers.
  * PROPERTIES block precedes FUNCTIONS block; both have //******...****// dividers
    with explicit public/protected/private sections.
  * UPROPERTY categories that are instance-editable are prefixed with "InstanceSettings|".
  * Constructor sets PrimaryComponentTick.bCanEverTick = false unless the graph
    contains a Tick event.
  * Class names use the project name or generic descriptors — the "QC" abbreviation
    is rejected at translation time.
  * Custom Blueprint functions are emitted as proper C++ methods with full
    signatures (UFUNCTION + parameters + return type) and TODO-annotated bodies.

Unsupported graph nodes are surfaced as // TODO comments inside the generated cpp so
the human reviewer never silently loses information.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


# ─── Configuration ────────────────────────────────────────────────────────────

DIVIDER_WIDTH = 70           # //***...***// total width
DEFAULT_MODULE_API = "LADOGA_API"
FORBIDDEN_NAME_SUBSTRINGS = ("QC",)   # rejected per project rule


# ─── Data model (mirrors the JSON the commandlet emits) ───────────────────────

@dataclass
class ComponentInfo:
    variable_name: str
    class_path: str
    class_name: str
    attach_parent: str
    attach_socket: str
    is_root: bool


@dataclass
class PropertyInfo:
    property_name: str
    property_type: str
    inner_type: str
    value: str
    category: str
    pointer_hint: str
    is_instance_editable: bool
    is_blueprint_visible: bool


@dataclass
class GraphNode:
    kind: str
    label: str
    function_name: str = ""
    target_class: str = ""
    unsupported: str = ""
    next: List["GraphNode"] = field(default_factory=list)
    branch_true: List["GraphNode"] = field(default_factory=list)
    branch_false: List["GraphNode"] = field(default_factory=list)


@dataclass
class EventTree:
    graph_name: str
    root: Optional[GraphNode]


@dataclass
class FunctionParameter:
    name: str
    type_name: str                # logical: "bool", "float", "int", "string", "object", ...
    cpp_class_name: str = ""      # for object / class / struct / enum
    is_array: bool = False
    is_map: bool = False
    is_set: bool = False
    is_reference: bool = False
    is_const: bool = False


@dataclass
class CustomFunction:
    function_name: str
    graph_name: str
    is_pure: bool
    is_const: bool
    is_blueprint_callable: bool
    inputs: List[FunctionParameter]
    returns: List[FunctionParameter]
    root: Optional[GraphNode]


@dataclass
class DumpData:
    blueprint_name: str
    blueprint_path: str
    parent_class_path: str
    parent_class_name: str
    is_actor_derived: bool
    components: List[ComponentInfo]
    defaults: List[PropertyInfo]
    event_trees: List[EventTree]
    custom_functions: List[CustomFunction]
    unsupported_count: int
    total_node_count: int


# ─── JSON loading ─────────────────────────────────────────────────────────────

def _parse_node(d: Optional[Dict[str, Any]]) -> Optional[GraphNode]:
    if not d:
        return None

    def _children(key: str) -> List[GraphNode]:
        out: List[GraphNode] = []
        for child in d.get(key, []) or []:
            parsed = _parse_node(child)
            if parsed is not None:
                out.append(parsed)
        return out

    return GraphNode(
        kind=d.get("kind", ""),
        label=d.get("label", ""),
        function_name=d.get("function", ""),
        target_class=d.get("targetClass", ""),
        unsupported=d.get("unsupported", ""),
        next=_children("next"),
        branch_true=_children("true"),
        branch_false=_children("false"),
    )


def _parse_parameter(d: Dict[str, Any]) -> FunctionParameter:
    return FunctionParameter(
        name=d.get("name", ""),
        type_name=d.get("type", ""),
        cpp_class_name=d.get("cppClassName", ""),
        is_array=bool(d.get("isArray", False)),
        is_map=bool(d.get("isMap", False)),
        is_set=bool(d.get("isSet", False)),
        is_reference=bool(d.get("isReference", False)),
        is_const=bool(d.get("isConst", False)),
    )


def load_dump(path: Path) -> DumpData:
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        text = data.decode("utf-8-sig")
    elif data.startswith(b"\xff\xfe") or data.startswith(b"\xfe\xff"):
        text = data.decode("utf-16")
    else:
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            text = data.decode("utf-16")

    raw = json.loads(text)

    components = [
        ComponentInfo(
            variable_name=c.get("variableName", ""),
            class_path=c.get("classPath", ""),
            class_name=c.get("className", ""),
            attach_parent=c.get("attachParentVarName", ""),
            attach_socket=c.get("attachSocketName", ""),
            is_root=bool(c.get("isRoot", False)),
        )
        for c in raw.get("components", [])
    ]

    defaults = [
        PropertyInfo(
            property_name=p.get("propertyName", ""),
            property_type=p.get("propertyType", ""),
            inner_type=p.get("innerTypeName", ""),
            value=p.get("value", ""),
            category=p.get("category", "") or "Default",
            pointer_hint=p.get("pointerStorageHint", "None"),
            is_instance_editable=bool(p.get("isInstanceEditable", False)),
            is_blueprint_visible=bool(p.get("isBlueprintVisible", False)),
        )
        for p in raw.get("defaults", [])
    ]

    trees = [
        EventTree(
            graph_name=t.get("graphName", ""),
            root=_parse_node(t.get("event")),
        )
        for t in raw.get("eventTrees", [])
    ]

    custom_functions = [
        CustomFunction(
            function_name=f.get("functionName", "UnknownFunction"),
            graph_name=f.get("graphName", ""),
            is_pure=bool(f.get("isPure", False)),
            is_const=bool(f.get("isConst", False)),
            is_blueprint_callable=bool(f.get("isBlueprintCallable", True)),
            inputs=[_parse_parameter(p) for p in (f.get("inputs", []) or [])],
            returns=[_parse_parameter(p) for p in (f.get("returns", []) or [])],
            root=_parse_node(f.get("functionRoot")),
        )
        for f in raw.get("customFunctions", [])
    ]

    return DumpData(
        blueprint_name=raw.get("blueprintName", "UnknownBP"),
        blueprint_path=raw.get("blueprintPath", ""),
        parent_class_path=raw.get("parentClassPath", ""),
        parent_class_name=raw.get("parentClassName", "AActor"),
        is_actor_derived=bool(raw.get("isActorDerived", True)),
        components=components,
        defaults=defaults,
        event_trees=trees,
        custom_functions=custom_functions,
        unsupported_count=int(raw.get("unsupportedNodeCount", 0)),
        total_node_count=int(raw.get("totalNodeCount", 0)),
    )


# ─── Naming ────────────────────────────────────────────────────────────────────

def sanitise_class_name(blueprint_name: str, parent_class_name: str) -> str:
    """
    Strips BP_ / WBP_ prefixes and applies the proper UE C++ prefix based on the parent.
    Rejects any name containing forbidden substrings (project rule: no "QC").
    """
    name = blueprint_name
    for prefix in ("BP_", "WBP_", "ABP_", "BPI_"):
        if name.startswith(prefix):
            name = name[len(prefix):]
            break

    parent_upper = parent_class_name or ""
    if parent_upper.startswith("A"):
        prefix = "A"
    elif parent_upper.startswith("U"):
        prefix = "U"
    elif parent_upper.startswith("F"):
        prefix = "F"
    else:
        prefix = "A"

    candidate = f"{prefix}{name}"

    for forbidden in FORBIDDEN_NAME_SUBSTRINGS:
        if forbidden in candidate:
            raise ValueError(
                f"Refusing to generate class '{candidate}' — name contains forbidden "
                f"substring '{forbidden}' (project rule)."
            )

    return candidate


def has_tick_event(dump: DumpData) -> bool:
    """Detects ReceiveTick in any event tree — drives PrimaryComponentTick decision."""
    tick_names = {"ReceiveTick", "Tick"}
    for tree in dump.event_trees:
        if tree.root and tree.root.label in tick_names:
            return True
    return False


# ─── Pointer storage selection ────────────────────────────────────────────────

def pointer_wrap(inner_type: str, hint: str) -> str:
    """
    Returns the correct UE5 smart-pointer wrap for the given inner type and hint.
    Headers MUST NOT contain raw pointers per project rule.
    """
    inner = inner_type or "UObject"
    if hint == "Soft":
        return f"TSoftObjectPtr<{inner}>"
    if hint == "SoftClass":
        return f"TSoftClassPtr<{inner}>"
    if hint == "Class":
        return f"TSubclassOf<{inner}>"
    if hint == "Weak":
        return f"TWeakObjectPtr<{inner}>"
    return f"TObjectPtr<{inner}>"


# ─── C++ type mapping ─────────────────────────────────────────────────────────

PRIMITIVE_TYPE_MAP = {
    "BoolProperty":   "bool",
    "ByteProperty":   "uint8",
    "IntProperty":    "int32",
    "Int64Property":  "int64",
    "FloatProperty":  "float",
    "DoubleProperty": "double",
    "StrProperty":    "FString",
    "NameProperty":   "FName",
    "TextProperty":   "FText",
}

# Maps logical pin types (emitted by the C++ parser) to canonical C++ types.
PIN_TYPE_MAP = {
    "bool":   "bool",
    "byte":   "uint8",
    "int":    "int32",
    "int64":  "int64",
    "float":  "float",
    "double": "double",
    "string": "FString",
    "name":   "FName",
    "text":   "FText",
}


def cpp_type_for_property(prop: PropertyInfo) -> str:
    """Resolves the C++ declaration type for a given dumped property."""
    pt = prop.property_type
    if pt in PRIMITIVE_TYPE_MAP:
        return PRIMITIVE_TYPE_MAP[pt]
    if pt == "EnumProperty":
        return prop.inner_type or "int32"
    if pt in ("ObjectProperty", "SoftObjectProperty",
              "ClassProperty",  "SoftClassProperty",
              "WeakObjectProperty"):
        return pointer_wrap(prop.inner_type, prop.pointer_hint)
    if pt == "StructProperty":
        return prop.inner_type or "FStruct"
    if pt == "ArrayProperty":
        return f"TArray<{prop.inner_type or 'int32'}>"
    if pt == "MapProperty":
        return "TMap<FName, FString>  // MANUAL: replace with the actual key/value types"
    if pt == "SetProperty":
        return f"TSet<{prop.inner_type or 'FName'}>"
    return f"int32  /* TODO: unresolved property type '{pt}' */"


def cpp_default_literal(prop: PropertyInfo) -> Optional[str]:
    """Returns a C++ literal for the property's value, or None if deferred to ctor body."""
    pt = prop.property_type
    val = (prop.value or "").strip()

    if pt == "BoolProperty":
        return "true" if val.lower() in ("true", "1") else "false"
    if pt in ("IntProperty", "Int64Property", "ByteProperty"):
        return val or "0"
    if pt in ("FloatProperty", "DoubleProperty"):
        if not val:
            return "0.0f"
        if not val.endswith("f") and pt == "FloatProperty":
            return f"{val}f"
        return val
    if pt == "StrProperty":
        return f'TEXT("{val}")' if val else 'TEXT("")'
    if pt == "NameProperty":
        return f'TEXT("{val}")' if val else 'NAME_None'
    if pt == "EnumProperty":
        return val if val else None
    return None


# ─── Function parameter type resolution ───────────────────────────────────────

def cpp_type_for_parameter(param: FunctionParameter) -> str:
    """
    Resolves the C++ declaration type for a Blueprint function parameter.
    Headers stay free of raw pointers — object types are wrapped in TObjectPtr
    family per project rule.
    """
    base = _cpp_base_type_for_pin(param)

    # Apply container wrappers (mutually exclusive in BP — array/set/map).
    if param.is_array:
        wrapped = f"TArray<{base}>"
    elif param.is_set:
        wrapped = f"TSet<{base}>"
    elif param.is_map:
        # Maps are not fully described by a single param (no key type carried);
        # the human reviewer must finalise the key type.
        wrapped = f"TMap<FName, {base}>  /* TODO: confirm key type */"
    else:
        wrapped = base

    # Const + reference qualifiers.
    qualified = wrapped
    if param.is_const and (param.is_reference or _is_complex_type(param)):
        qualified = f"const {qualified}"
    if param.is_reference:
        qualified = f"{qualified}&"

    return qualified


def _cpp_base_type_for_pin(param: FunctionParameter) -> str:
    """Picks the raw C++ type for a single pin, ignoring container/ref qualifiers."""
    t = param.type_name

    if t in PIN_TYPE_MAP:
        return PIN_TYPE_MAP[t]

    if t == "object":
        # Hard reference in header → TObjectPtr per project rule.
        return f"TObjectPtr<{param.cpp_class_name or 'UObject'}>"
    if t == "class":
        return f"TSubclassOf<{param.cpp_class_name or 'UObject'}>"
    if t == "softobject":
        return f"TSoftObjectPtr<{param.cpp_class_name or 'UObject'}>"
    if t == "softclass":
        return f"TSoftClassPtr<{param.cpp_class_name or 'UObject'}>"
    if t == "weakobject":
        return f"TWeakObjectPtr<{param.cpp_class_name or 'UObject'}>"
    if t == "interface":
        return f"TScriptInterface<I{param.cpp_class_name or 'Interface'}>"
    if t == "struct":
        return param.cpp_class_name or "FStruct"
    if t == "enum":
        return param.cpp_class_name or "int32"

    # Unknown pin category — emit a placeholder and let the reviewer fix it.
    return f"int32 /* TODO: unresolved pin type '{t}' */"


def _is_complex_type(param: FunctionParameter) -> bool:
    """True for non-primitive types that should be passed by const-ref by convention."""
    return param.type_name in (
        "string", "name", "text",
        "object", "class", "softobject", "softclass", "weakobject",
        "struct", "interface",
    ) or param.is_array or param.is_set or param.is_map


# ─── Function signature helpers ───────────────────────────────────────────────

def _has_single_return(func: CustomFunction) -> bool:
    """True if function has exactly one return parameter — emit as C++ return type."""
    return len(func.returns) == 1


def _function_return_type(func: CustomFunction) -> str:
    """Resolves the C++ return type for a function. void / single return / out-params."""
    if not func.returns:
        return "void"
    if _has_single_return(func):
        # Single return → use as actual return type (strip ref/const — return-by-value).
        only = func.returns[0]
        # Build a clean by-value variant: no reference, drop const for primitives.
        clone = FunctionParameter(
            name=only.name, type_name=only.type_name,
            cpp_class_name=only.cpp_class_name,
            is_array=only.is_array, is_map=only.is_map, is_set=only.is_set,
            is_reference=False,
            is_const=False,
        )
        return cpp_type_for_parameter(clone)
    # Multiple returns → void + out-params handled in signature builder.
    return "void"


def _function_signature_params(func: CustomFunction) -> List[str]:
    """Renders the parameter list for the function signature."""
    rendered: List[str] = []
    for p in func.inputs:
        rendered.append(f"{cpp_type_for_parameter(p)} {p.name}")
    if not _has_single_return(func) and func.returns:
        # Multiple returns become out parameters by reference.
        for r in func.returns:
            ref = FunctionParameter(
                name=r.name, type_name=r.type_name,
                cpp_class_name=r.cpp_class_name,
                is_array=r.is_array, is_map=r.is_map, is_set=r.is_set,
                is_reference=True,
                is_const=False,
            )
            rendered.append(f"{cpp_type_for_parameter(ref)} Out{r.name}")
    return rendered


def _function_uproperty_macro(func: CustomFunction) -> str:
    """Builds the UFUNCTION() spec line for the given custom function."""
    parts: List[str] = []
    if func.is_blueprint_callable:
        parts.append("BlueprintCallable")
    if func.is_pure:
        parts.append("BlueprintPure")
    parts.append('Category = "BlueprintBuster|Generated"')
    return "UFUNCTION(" + ", ".join(parts) + ")"


def _function_default_return_literal(func: CustomFunction) -> str:
    """Returns the C++ literal used in the auto-generated `return X;` line."""
    if not _has_single_return(func):
        return ""
    only = func.returns[0]
    if only.type_name == "bool":
        return "false"
    if only.type_name in ("byte", "int", "int64", "float", "double"):
        return "0" if only.type_name in ("byte", "int", "int64") else "0.0f"
    if only.type_name in ("string", "name", "text"):
        # FString / FName / FText all default-construct meaningfully.
        return "{}"
    if only.type_name in ("object", "softobject", "weakobject", "class", "softclass"):
        return "nullptr"
    if only.type_name == "struct":
        return f"{only.cpp_class_name or 'FStruct'}{{}}"
    if only.type_name == "enum":
        return f"static_cast<{only.cpp_class_name or 'int32'}>(0)"
    return "{}"


# ─── Code generation ──────────────────────────────────────────────────────────

def divider(label: str) -> str:
    """Builds //******LABEL******// padded to DIVIDER_WIDTH chars."""
    inner = f"//{'*' * 6}{label}{'*' * 6}//"
    if len(inner) >= DIVIDER_WIDTH:
        return inner
    extra = DIVIDER_WIDTH - len(inner)
    left = extra // 2
    right = extra - left
    return f"//{'*' * (6 + left)}{label}{'*' * (6 + right)}//"


def emit_header(
    dump: DumpData,
    class_name: str,
    module_api: str,
    has_tick: bool,
) -> str:
    parent = dump.parent_class_name or "AActor"
    base_for_include = parent[1:] if parent[:1] in "AU" else parent

    # ── Collect property declarations by access level ────────────────────────
    public_props:    List[str] = []
    protected_props: List[str] = []

    for comp in dump.components:
        category = "InstanceSettings|Components"
        comp_type = pointer_wrap(comp.class_name or "USceneComponent", "Hard")
        protected_props.append(
            f"    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, "
            f"Category = \"{category}\")\n"
            f"    {comp_type} {comp.variable_name};"
        )

    for prop in dump.defaults:
        cpp_type = cpp_type_for_property(prop)
        default_literal = cpp_default_literal(prop)

        spec_parts: List[str] = []
        if prop.is_instance_editable:
            spec_parts.append("EditAnywhere")
        else:
            spec_parts.append("EditDefaultsOnly")
        if prop.is_blueprint_visible:
            spec_parts.append("BlueprintReadWrite")

        category = prop.category or "Default"
        if prop.is_instance_editable and not category.startswith("InstanceSettings"):
            category = f"InstanceSettings|{category}"
        spec_parts.append(f'Category = "{category}"')
        spec_line = "UPROPERTY(" + ", ".join(spec_parts) + ")"

        decl_line = f"    {cpp_type} {prop.property_name}"
        if default_literal is not None:
            decl_line += f" = {default_literal}"
        decl_line += ";"

        block = f"    {spec_line}\n{decl_line}"
        if prop.is_instance_editable:
            public_props.append(block)
        else:
            protected_props.append(block)

    # ── Event functions ──────────────────────────────────────────────────────
    event_funcs: List[Tuple[str, str]] = []
    for tree in dump.event_trees:
        if not tree.root:
            continue
        ev_name = tree.root.label or "UnknownEvent"
        cpp_name = _event_to_cpp_name(ev_name)
        if cpp_name:
            event_funcs.append(
                (
                    f"    virtual void {cpp_name}();",
                    f"Generated from BP event {ev_name} (graph: {tree.graph_name})",
                )
            )

    # ── Custom function declarations ─────────────────────────────────────────
    custom_func_decls: List[str] = []
    for func in dump.custom_functions:
        ret = _function_return_type(func)
        params = _function_signature_params(func)
        param_text = ", ".join(params)
        const_suffix = " const" if func.is_const else ""
        block = (
            f"    // Generated from BP function {func.function_name} "
            f"(graph: {func.graph_name}). Body in .cpp — review TODOs.\n"
            f"    {_function_uproperty_macro(func)}\n"
            f"    {ret} {func.function_name}({param_text}){const_suffix};"
        )
        custom_func_decls.append(block)

    # Forward declarations from properties + components + function params.
    fwd_set: set = set()
    for comp in dump.components:
        if comp.class_name:
            fwd_set.add(comp.class_name)
    for prop in dump.defaults:
        if prop.inner_type and prop.property_type in (
            "ObjectProperty", "SoftObjectProperty",
            "ClassProperty",  "SoftClassProperty",
            "WeakObjectProperty",
        ):
            fwd_set.add(prop.inner_type)
    for func in dump.custom_functions:
        for p in list(func.inputs) + list(func.returns):
            if p.type_name in ("object", "class", "softobject", "softclass",
                                "weakobject", "interface") and p.cpp_class_name:
                fwd_set.add(p.cpp_class_name)

    lines: List[str] = []
    lines += [
        "#pragma once",
        "",
        "#include \"CoreMinimal.h\"",
        f"#include \"GameFramework/{base_for_include}.h\""
        if dump.is_actor_derived
        else f"#include \"{base_for_include}.h\"",
        f"#include \"{class_name}.generated.h\"",
        "",
    ]

    for fwd in sorted(fwd_set):
        # Skip primitives that slipped in.
        if fwd in ("int32", "FString", "FName", "FText", "bool", "float"):
            continue
        lines.append(f"class {fwd};")
    if fwd_set:
        lines.append("")

    lines += [
        "UCLASS()",
        f"class {module_api} {class_name} : public {parent}",
        "{",
        "    GENERATED_BODY()",
        "",
        f"    {divider('PROPERTIES')}",
        "public:",
    ]
    lines.append("\n\n".join(public_props) if public_props else "    // (no public properties)")
    lines += [
        "",
        "protected:",
    ]
    lines.append("\n\n".join(protected_props) if protected_props else "    // (no protected properties)")
    lines += [
        "",
        "private:",
        "    // No private properties were dumped — add cached state here.",
        "",
        f"    {divider('FUNCTIONS')}",
        "public:",
        f"    {class_name}();",
    ]
    if event_funcs:
        lines.append("")
        for sig, brief in event_funcs:
            lines.append(f"    // {brief}")
            lines.append(sig)
    if custom_func_decls:
        lines.append("")
        for decl in custom_func_decls:
            lines.append(decl)
            lines.append("")
    lines += [
        "protected:",
    ]
    if dump.is_actor_derived:
        lines.append("    virtual void BeginPlay() override;")
        if has_tick:
            lines.append("    virtual void Tick(float DeltaSeconds) override;")
    lines += [
        "",
        "private:",
        "};",
        "",
    ]

    return "\n".join(lines)


def _event_to_cpp_name(event_label: str) -> Optional[str]:
    """Maps BP event names to their C++ override names where applicable."""
    if event_label == "ReceiveBeginPlay":
        return None
    if event_label == "ReceiveTick":
        return None
    if event_label == "ReceiveEndPlay":
        return None
    return event_label or None


def emit_source(
    dump: DumpData,
    class_name: str,
    has_tick: bool,
) -> str:
    lines: List[str] = []
    lines += [
        f"#include \"{class_name}.h\"",
    ]

    # Component includes (best-effort).
    include_set: List[str] = []
    for comp in dump.components:
        if comp.class_name == "USceneComponent":
            include_set.append("Components/SceneComponent.h")
        elif comp.class_name == "UStaticMeshComponent":
            include_set.append("Components/StaticMeshComponent.h")
        elif comp.class_name == "USkeletalMeshComponent":
            include_set.append("Components/SkeletalMeshComponent.h")
        elif comp.class_name == "UCapsuleComponent":
            include_set.append("Components/CapsuleComponent.h")
        elif comp.class_name == "UBoxComponent":
            include_set.append("Components/BoxComponent.h")
        elif comp.class_name == "USphereComponent":
            include_set.append("Components/SphereComponent.h")
        elif comp.class_name == "UAudioComponent":
            include_set.append("Components/AudioComponent.h")
    for inc in sorted(set(include_set)):
        lines.append(f"#include \"{inc}\"")
    lines.append("")

    # ── Constructor ──────────────────────────────────────────────────────────
    lines += [
        f"{class_name}::{class_name}()",
        "{",
        "    // Tick disabled by project rule (TDD-06). Re-enable explicitly if profiling demands it.",
    ]
    if dump.is_actor_derived:
        if has_tick:
            lines.append("    PrimaryActorTick.bCanEverTick = true;  // BP graph contained ReceiveTick.")
        else:
            lines.append("    PrimaryActorTick.bCanEverTick = false;")
    else:
        if has_tick:
            lines.append("    PrimaryComponentTick.bCanEverTick = true;  // BP graph contained ReceiveTick.")
        else:
            lines.append("    PrimaryComponentTick.bCanEverTick = false;")

    if dump.components:
        lines.append("")
        lines.append("    // ── Components ────────────────────────────────────────────────────")
        root_var: Optional[str] = None
        for comp in dump.components:
            ctor = (
                f'    {comp.variable_name} = CreateDefaultSubobject<{comp.class_name}>'
                f'(TEXT("{comp.variable_name}"));'
            )
            lines.append(ctor)
            if comp.is_root and root_var is None:
                root_var = comp.variable_name
                lines.append(f"    RootComponent = {comp.variable_name};")
        for comp in dump.components:
            if not comp.is_root and comp.attach_parent:
                socket = (
                    f', FName(TEXT("{comp.attach_socket}"))'
                    if comp.attach_socket
                    else ""
                )
                lines.append(
                    f"    {comp.variable_name}->SetupAttachment("
                    f"{comp.attach_parent}{socket});"
                )

    deferred_defaults = [
        d for d in dump.defaults
        if cpp_default_literal(d) is None and d.value
    ]
    if deferred_defaults:
        lines.append("")
        lines.append("    // ── CDO defaults (deferred from header) ───────────────────────────")
        for d in deferred_defaults:
            lines.append(
                f'    // TODO: assign default for {d.property_name} '
                f'(type {d.property_type}, raw value: "{d.value[:80]}").'
            )

    lines += [
        "}",
        "",
    ]

    # ── BeginPlay / Tick / event functions ───────────────────────────────────
    if dump.is_actor_derived:
        lines += [
            f"void {class_name}::BeginPlay()",
            "{",
            "    Super::BeginPlay();",
        ]
        bp_tree = _find_event_tree(dump, "ReceiveBeginPlay")
        if bp_tree:
            lines += _emit_node_chain(bp_tree, indent=1)
        lines += ["}", ""]

        if has_tick:
            tick_tree = _find_event_tree(dump, "ReceiveTick")
            lines += [
                f"void {class_name}::Tick(float DeltaSeconds)",
                "{",
                "    Super::Tick(DeltaSeconds);",
            ]
            if tick_tree:
                lines += _emit_node_chain(tick_tree, indent=1)
            lines += ["}", ""]

    for tree in dump.event_trees:
        if not tree.root:
            continue
        if tree.root.label in ("ReceiveBeginPlay", "ReceiveTick", "ReceiveEndPlay"):
            continue
        cpp_name = _event_to_cpp_name(tree.root.label)
        if not cpp_name:
            continue
        lines += [
            f"void {class_name}::{cpp_name}()",
            "{",
        ]
        lines += _emit_node_chain(tree.root, indent=1)
        lines += ["}", ""]

    # ── Custom Blueprint functions ───────────────────────────────────────────
    for func in dump.custom_functions:
        ret = _function_return_type(func)
        params = _function_signature_params(func)
        param_text = ", ".join(params)
        const_suffix = " const" if func.is_const else ""

        lines.append(
            f"{ret} {class_name}::{func.function_name}({param_text}){const_suffix}"
        )
        lines.append("{")

        # Inputs documentation block.
        if func.inputs:
            lines.append("    // Inputs:")
            for p in func.inputs:
                lines.append(f"    //   - {p.name}: {p.type_name}"
                             f"{(' ' + p.cpp_class_name) if p.cpp_class_name else ''}")
        if func.returns:
            lines.append("    // Returns:")
            for r in func.returns:
                lines.append(f"    //   - {r.name}: {r.type_name}"
                             f"{(' ' + r.cpp_class_name) if r.cpp_class_name else ''}")

        if func.root:
            lines += _emit_node_chain(func.root, indent=1)
        else:
            lines.append("    // (empty BP function body)")

        # Out-parameter defaults / single-return fallthrough.
        if not _has_single_return(func) and func.returns:
            lines.append("")
            lines.append("    // TODO: populate Out* parameters before returning.")
        elif _has_single_return(func):
            lines.append("")
            lines.append(
                f"    return {_function_default_return_literal(func)}; "
                f"// TODO: replace with computed result"
            )

        lines += ["}", ""]

    return "\n".join(lines)


def _find_event_tree(dump: DumpData, event_label: str) -> Optional[GraphNode]:
    for t in dump.event_trees:
        if t.root and t.root.label == event_label:
            return t.root
    return None


def _emit_node_chain(root: GraphNode, indent: int) -> List[str]:
    """Emits the linear chain (root + root.next + branches)."""
    pad = "    " * indent
    out: List[str] = []

    def emit_node(node: GraphNode, depth: int) -> None:
        local_pad = "    " * depth
        if node.kind == "CallFunction":
            out.append(f"{local_pad}// Call: {node.function_name}")
            out.append(
                f"{local_pad}// TODO: implement call to {node.function_name}"
                f"{f' on {node.target_class}' if node.target_class else ''}."
            )
        elif node.kind == "Branch":
            out.append(f"{local_pad}// Branch — condition pin not resolved by translator.")
            out.append(f"{local_pad}if (/* TODO: resolve condition */ true)")
            out.append(f"{local_pad}{{")
            for child in node.branch_true:
                emit_node(child, depth + 1)
            out.append(f"{local_pad}}}")
            if node.branch_false:
                out.append(f"{local_pad}else")
                out.append(f"{local_pad}{{")
                for child in node.branch_false:
                    emit_node(child, depth + 1)
                out.append(f"{local_pad}}}")
        elif node.kind == "Sequence":
            out.append(f"{local_pad}// Sequence — flattened to linear execution.")
            for child in node.next:
                emit_node(child, depth)
            return
        elif node.kind in ("VariableGet", "VariableSet"):
            out.append(f"{local_pad}// {node.kind}: {node.label}")
        elif node.kind == "Event":
            pass
        elif node.kind == "FunctionEntry":
            # Function body root — no code, body is in node.next.
            pass
        elif node.kind == "MacroInstance":
            out.append(
                f"{local_pad}// TODO: macro '{node.label}' must be expanded manually "
                f"({node.unsupported})."
            )
        elif node.kind == "Unsupported":
            out.append(
                f"{local_pad}// TODO: unsupported node '{node.label}' — "
                f"{node.unsupported}"
            )
        else:
            out.append(f"{local_pad}// {node.kind}: {node.label}")

        for child in node.next:
            emit_node(child, depth)

    emit_node(root, indent)
    if not out:
        out.append(f"{pad}// (empty BP graph for this event)")
    return out


# ─── Entry point ──────────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Translate BlueprintBuster JSON dumps into UE5 C++ class pairs."
    )
    ap.add_argument("dump", type=Path, help="Path to <BlueprintName>_dump.json")
    ap.add_argument(
        "-o", "--output", type=Path, default=Path.cwd(),
        help="Output directory (default: cwd)."
    )
    ap.add_argument(
        "--module-api", default=DEFAULT_MODULE_API,
        help=f"Module API macro (default: {DEFAULT_MODULE_API})."
    )
    args = ap.parse_args()

    if not args.dump.is_file():
        print(f"ERROR: dump file not found: {args.dump}", file=sys.stderr)
        return 1

    dump = load_dump(args.dump)

    try:
        class_name = sanitise_class_name(dump.blueprint_name, dump.parent_class_name)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    has_tick = has_tick_event(dump)

    args.output.mkdir(parents=True, exist_ok=True)
    header_path = args.output / f"{class_name}.h"
    source_path = args.output / f"{class_name}.cpp"

    header_path.write_text(emit_header(dump, class_name, args.module_api, has_tick),
                           encoding="utf-8", newline="\n")
    source_path.write_text(emit_source(dump, class_name, has_tick),
                           encoding="utf-8", newline="\n")

    print(f"  Header → {header_path}")
    print(f"  Source → {source_path}")
    print(f"  ({dump.total_node_count} nodes parsed; "
          f"{len(dump.custom_functions)} custom function(s); "
          f"{dump.unsupported_count} marked for manual review)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
