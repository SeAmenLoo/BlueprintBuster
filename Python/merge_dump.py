import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        print("Usage: merge_dump.py <base_dump.json> <patch.json> <out.json>", file=sys.stderr)
        return 2

    base_path = Path(sys.argv[1])
    patch_path = Path(sys.argv[2])
    out_path = Path(sys.argv[3])

    base = json.loads(base_path.read_text(encoding="utf-8"))
    patch = json.loads(patch_path.read_text(encoding="utf-8"))

    for k in ("eventTrees", "unsupportedNodeCount", "totalNodeCount", "fullDumpReport"):
        if k in patch:
            base[k] = patch[k]

    out_path.write_text(json.dumps(base, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

