import json
import sys
import copy
import datetime
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple


try:
    import unreal
except Exception:
    unreal = None


@dataclass(frozen=True)
class _NodeRef:
    graph_name: str
    node_guid: str
    class_name: str
    title: str


def _pin_direction(pin: Any) -> str:
    try:
        return str(pin.direction)
    except Exception:
        return ""


def _is_exec_pin(pin: Any) -> bool:
    try:
        pt = pin.pin_type
        return str(pt.pin_category) == "exec"
    except Exception:
        return False


def _pin_name(pin: Any) -> str:
    try:
        return str(pin.pin_name)
    except Exception:
        try:
            return str(pin.get_name())
        except Exception:
            return ""


def _node_guid(node: Any) -> str:
    try:
        return str(node.node_guid)
    except Exception:
        try:
            return str(node.get_editor_property("node_guid"))
        except Exception:
            return ""


def _node_class_name(node: Any) -> str:
    try:
        return node.get_class().get_name()
    except Exception:
        try:
            return node.get_class().get_name()
        except Exception:
            return ""


def _node_title(node: Any) -> str:
    try:
        return str(node.get_node_title(unreal.NodeTitleType.ListView))
    except Exception:
        try:
            return str(node.get_name())
        except Exception:
            return ""


def _linked_exec_node_from_pin(pin: Any) -> Optional[Any]:
    if not pin:
        return None
    try:
        linked = list(pin.linked_to or [])
    except Exception:
        linked = []
    if not linked:
        return None
    try:
        return linked[0].owning_node
    except Exception:
        return None


def _find_first_linked_exec_output_pin(node: Any) -> Optional[Any]:
    try:
        pins = list(node.pins or [])
    except Exception:
        return None
    for pin in pins:
        if _pin_direction(pin) == "EGPD_Output" and _is_exec_pin(pin):
            try:
                if pin.linked_to:
                    return pin
            except Exception:
                pass
    return None


def _find_branch_pins(node: Any) -> Tuple[Optional[Any], Optional[Any]]:
    then_pin = None
    else_pin = None
    try:
        pins = list(node.pins or [])
    except Exception:
        return None, None
    for pin in pins:
        if _pin_direction(pin) != "EGPD_Output" or not _is_exec_pin(pin):
            continue
        name = _pin_name(pin)
        lname = name.lower()
        if "then" in lname:
            then_pin = pin
        elif "else" in lname:
            else_pin = pin
    return then_pin, else_pin


def _sequence_output_pins(node: Any) -> List[Any]:
    out: List[Any] = []
    try:
        pins = list(node.pins or [])
    except Exception:
        return out
    for pin in pins:
        if _pin_direction(pin) == "EGPD_Output" and _is_exec_pin(pin):
            try:
                if pin.linked_to:
                    out.append(pin)
            except Exception:
                pass
    def _key(p: Any) -> str:
        return _pin_name(p)
    out.sort(key=_key)
    return out


def _callfunction_name_and_target(node: Any) -> Tuple[str, str]:
    func_name = ""
    target_class_path = ""
    try:
        ref = node.get_editor_property("function_reference")
        try:
            func_name = str(ref.get_member_name())
        except Exception:
            try:
                func_name = str(ref.member_name)
            except Exception:
                func_name = ""
        try:
            cls = ref.get_member_parent_class()
            if cls:
                target_class_path = str(cls.get_path_name())
        except Exception:
            target_class_path = ""
    except Exception:
        pass
    return func_name, target_class_path


def _variable_name(node: Any) -> str:
    try:
        ref = node.get_editor_property("variable_reference")
        try:
            return str(ref.get_member_name())
        except Exception:
            return str(ref.member_name)
    except Exception:
        return _node_title(node)


def _get_macro_graph(node: Any) -> Optional[Any]:
    try:
        g = node.get_macro_graph()
        return g
    except Exception:
        try:
            return node.get_editor_property("macro_graph")
        except Exception:
            return None


def _graph_name(graph: Any) -> str:
    try:
        return str(graph.get_name())
    except Exception:
        return ""


def _trace_exec_chain(
    graph: Any,
    start_node: Any,
    max_depth: int,
    macro_stack: List[str],
    report_errors: List[Dict[str, Any]],
    *,
    in_macro: bool = False,
) -> Dict[str, Any]:
    visited: Set[str] = set()

    def _fail(reason: str, node: Optional[Any]) -> None:
        ref = None
        if node is not None:
            ref = {
                "graph": _graph_name(graph),
                "nodeGuid": _node_guid(node),
                "nodeClass": _node_class_name(node),
                "nodeTitle": _node_title(node),
            }
        report_errors.append({"reason": reason, "node": ref, "macroStack": list(macro_stack)})
        raise RuntimeError(reason)

    def _emit_node(node: Any, depth: int) -> Dict[str, Any]:
        if node is None:
            _fail("Null node in execution chain", None)
        if depth >= max_depth:
            _fail(f"MaxDepth exceeded ({max_depth})", node)

        guid = _node_guid(node)
        if guid and guid in visited:
            _fail("Cycle detected in execution chain", node)
        if guid:
            visited.add(guid)

        cls = _node_class_name(node)

        if "K2Node_Tunnel" in cls:
            if in_macro:
                return {"kind": "FunctionEntry", "label": "MacroTunnel", "next": []}
            _fail("Tunnel node encountered outside macro expansion", node)

        if "K2Node_Event" in cls or cls == "K2Node_Event":
            out = {"kind": "Event", "label": _node_title(node), "next": []}
            pin = _find_first_linked_exec_output_pin(node)
            nxt = _linked_exec_node_from_pin(pin) if pin else None
            if nxt:
                out["next"].append(_emit_node(nxt, depth + 1))
            return out

        if "K2Node_CallFunction" in cls:
            fn, target = _callfunction_name_and_target(node)
            out = {"kind": "CallFunction", "label": fn or _node_title(node), "function": fn, "targetClass": target, "next": []}
            pin = _find_first_linked_exec_output_pin(node)
            nxt = _linked_exec_node_from_pin(pin) if pin else None
            if nxt:
                out["next"].append(_emit_node(nxt, depth + 1))
            return out

        if "K2Node_IfThenElse" in cls:
            then_pin, else_pin = _find_branch_pins(node)
            out = {"kind": "Branch", "label": "Branch", "true": [], "false": [], "next": []}
            tnode = _linked_exec_node_from_pin(then_pin) if then_pin else None
            enode = _linked_exec_node_from_pin(else_pin) if else_pin else None
            if tnode:
                out["true"].append(_emit_node(tnode, depth + 1))
            if enode:
                out["false"].append(_emit_node(enode, depth + 1))
            return out

        if "K2Node_ExecutionSequence" in cls:
            out = {"kind": "Sequence", "label": "Sequence", "next": []}
            for pin in _sequence_output_pins(node):
                nxt = _linked_exec_node_from_pin(pin)
                if nxt:
                    out["next"].append(_emit_node(nxt, depth + 1))
            return out

        if "K2Node_VariableGet" in cls:
            out = {"kind": "VariableGet", "label": _variable_name(node), "next": []}
            pin = _find_first_linked_exec_output_pin(node)
            nxt = _linked_exec_node_from_pin(pin) if pin else None
            if nxt:
                out["next"].append(_emit_node(nxt, depth + 1))
            return out

        if "K2Node_VariableSet" in cls:
            out = {"kind": "VariableSet", "label": _variable_name(node), "next": []}
            pin = _find_first_linked_exec_output_pin(node)
            nxt = _linked_exec_node_from_pin(pin) if pin else None
            if nxt:
                out["next"].append(_emit_node(nxt, depth + 1))
            return out

        if "K2Node_MacroInstance" in cls:
            macro_graph = _get_macro_graph(node)
            if not macro_graph:
                _fail("MacroInstance has no macro graph", node)
            macro_id = f"{_graph_name(macro_graph)}@{getattr(macro_graph, 'get_path_name', lambda: '')()}"
            if macro_id in macro_stack:
                _fail("Macro recursion detected", node)
            macro_stack.append(macro_id)
            expanded_root, expanded_tail_next = _expand_macro_exec(macro_graph, max_depth, macro_stack, report_errors)
            macro_stack.pop()
            pin = _find_first_linked_exec_output_pin(node)
            after_macro = _linked_exec_node_from_pin(pin) if pin else None
            if expanded_tail_next is not None:
                _fail("Macro expansion returned unexpected tail marker", node)
            if after_macro:
                _append_to_leaves(expanded_root, _emit_node(after_macro, depth + 1))
            return expanded_root

        lowering_hints = [
            "K2Node_Switch",
            "K2Node_Select",
            "K2Node_MultiGate",
            "K2Node_ForLoop",
            "K2Node_ForLoopWithBreak",
            "K2Node_WhileLoop",
            "K2Node_DoOnceMultiInput",
            "K2Node_DoN",
            "K2Node_Timeline",
            "K2Node_Delay",
            "K2Node_SpawnActor",
            "K2Node_DynamicCast",
        ]
        for hint in lowering_hints:
            if hint in cls:
                _fail(f"Lowering rule not implemented for {hint}", node)

        _fail("Unsupported node class in execution chain", node)
        raise RuntimeError("unreachable")

    return _emit_node(start_node, 0)


def _append_to_leaves(root: Dict[str, Any], nxt: Dict[str, Any]) -> None:
    kind = root.get("kind", "")
    if kind == "Branch":
        t = root.get("true", [])
        f = root.get("false", [])
        if not t and not f:
            root.setdefault("next", []).append(copy.deepcopy(nxt))
            return
        for child in t:
            _append_to_leaves(child, nxt)
        for child in f:
            _append_to_leaves(child, nxt)
        return

    kids = root.get("next", [])
    if not kids:
        root.setdefault("next", []).append(copy.deepcopy(nxt))
        return
    for child in kids:
        _append_to_leaves(child, nxt)


def _expand_macro_exec(
    macro_graph: Any,
    max_depth: int,
    macro_stack: List[str],
    report_errors: List[Dict[str, Any]],
) -> Tuple[Dict[str, Any], Optional[Any]]:
    nodes = []
    try:
        nodes = list(macro_graph.nodes or [])
    except Exception:
        nodes = []

    tunnels = [n for n in nodes if "K2Node_Tunnel" in _node_class_name(n)]
    if not tunnels:
        report_errors.append({"reason": "MacroGraph has no tunnel nodes", "macroStack": list(macro_stack)})
        raise RuntimeError("MacroGraph has no tunnel nodes")

    entry = None
    for t in tunnels:
        try:
            pins = list(t.pins or [])
        except Exception:
            pins = []
        has_exec_out = any(_pin_direction(p) == "EGPD_Output" and _is_exec_pin(p) for p in pins)
        has_exec_in = any(_pin_direction(p) == "EGPD_Input" and _is_exec_pin(p) for p in pins)
        if has_exec_out and not has_exec_in:
            entry = t
            break
    if entry is None:
        entry = tunnels[0]

    entry_pin = _find_first_linked_exec_output_pin(entry)
    start = _linked_exec_node_from_pin(entry_pin) if entry_pin else None
    if start is None:
        report_errors.append({"reason": "Macro entry tunnel has no linked exec output", "macroStack": list(macro_stack)})
        raise RuntimeError("Macro entry tunnel has no linked exec output")

    root = _trace_exec_chain(macro_graph, start, max_depth, macro_stack, report_errors, in_macro=True)
    return root, None


def _build_event_trees(
    blueprint: Any,
    max_depth: int,
) -> Tuple[List[Dict[str, Any]], int, int, Dict[str, Any]]:
    if unreal is None:
        raise RuntimeError("This script must run inside Unreal Python environment")

    graphs: List[Any] = []
    try:
        graphs.extend(list(blueprint.ubergraph_pages or []))
    except Exception:
        pass
    try:
        graphs.extend(list(blueprint.function_graphs or []))
    except Exception:
        pass

    report_errors: List[Dict[str, Any]] = []
    total_nodes = 0
    unsupported_nodes = 0
    out: List[Dict[str, Any]] = []

    for graph in graphs:
        if graph is None:
            continue
        gname = _graph_name(graph)
        try:
            graph_nodes = list(graph.nodes or [])
        except Exception:
            graph_nodes = []
        for node in graph_nodes:
            total_nodes += 1
            if node is None:
                continue
            cls = _node_class_name(node)
            if "K2Node_Event" not in cls:
                continue
            try:
                ev = _trace_exec_chain(graph, node, max_depth, [], report_errors)
            except Exception:
                unsupported_nodes += 1
                continue
            out.append({"graphName": gname, "event": ev})

    report = {
        "ok": unsupported_nodes == 0 and not report_errors,
        "unsupportedCount": unsupported_nodes,
        "totalNodeCount": total_nodes,
        "errors": report_errors,
    }
    return out, unsupported_nodes, total_nodes, report


def main() -> int:
    if len(sys.argv) < 4:
        print(
            "Usage: full_dump_patch.py <blueprint_asset_path> <base_dump.json> <out_patch.json> "
            "[max_depth] [--no-backup] [--backup-root=/Game/BlueprintBusterBackups]",
            file=sys.stderr,
        )
        return 2

    blueprint_asset_path = sys.argv[1]
    base_dump_path = Path(sys.argv[2])
    out_patch_path = Path(sys.argv[3])
    max_depth = 512
    for arg in sys.argv[4:]:
        if arg.isdigit():
            max_depth = int(arg)

    enable_backup = True
    backup_root = "/Game/BlueprintBusterBackups"
    for arg in sys.argv[4:]:
        if arg == "--no-backup":
            enable_backup = False
        elif arg.startswith("--backup-root="):
            backup_root = arg.split("=", 1)[1].strip() or backup_root

    if unreal is None:
        print("ERROR: Unreal Python environment is required", file=sys.stderr)
        return 1

    if not base_dump_path.exists():
        print(f"ERROR: base dump not found: {base_dump_path}", file=sys.stderr)
        return 1

    bp = unreal.EditorAssetLibrary.load_asset(blueprint_asset_path)
    if not bp:
        print(f"ERROR: cannot load blueprint: {blueprint_asset_path}", file=sys.stderr)
        return 1

    original_bp = bp
    backup_path = ""
    if enable_backup:
        name = blueprint_asset_path.split("/")[-1]
        if "." in name:
            name = name.split(".", 1)[0]
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        backup_name = f"{name}_BBBackup_{ts}"
        try:
            unreal.EditorAssetLibrary.make_directory(backup_root)
        except Exception:
            pass
        try:
            tools = unreal.AssetToolsHelpers.get_asset_tools()
            bp = tools.duplicate_asset(backup_name, backup_root, bp)
        except Exception:
            bp = None
        if not bp:
            print(f"ERROR: cannot create backup blueprint under {backup_root}", file=sys.stderr)
            return 1
        try:
            backup_path = str(bp.get_path_name())
        except Exception:
            backup_path = ""

    try:
        unreal.KismetEditorUtilities.compile_blueprint(bp)
    except Exception:
        pass

    trees, unsupported_count, total_count, report = _build_event_trees(bp, max_depth)

    report["sourceBlueprint"] = blueprint_asset_path
    report["backupBlueprint"] = backup_path
    try:
        report["originalLoaded"] = str(original_bp.get_path_name())
    except Exception:
        report["originalLoaded"] = ""

    patch = {
        "eventTrees": trees,
        "unsupportedNodeCount": unsupported_count,
        "totalNodeCount": total_count,
        "fullDumpReport": report,
    }

    out_patch_path.write_text(json.dumps(patch, ensure_ascii=False, indent=2), encoding="utf-8")

    if not report.get("ok", False):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
