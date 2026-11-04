#!/usr/bin/env python3
"""Prove equivalence for the frozen flow using the bundled Yosys miter."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()

    root = Path(args.root).resolve()
    out_dir = Path(args.out_dir).resolve()
    work = out_dir / "work"
    out_dir.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)

    helper_path = Path(__file__).with_name("final_eqy.py")
    spec = importlib.util.spec_from_file_location("final_eqy", helper_path)
    assert spec is not None and spec.loader is not None
    fe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fe)

    baseline = root / "baseline" / "yosys.exe"
    candidate = root / "candidate" / "yosys.exe"
    cell_model = root / "source" / "final" / "dffeas_formal.v"
    env = fe.li.child_env()
    env["PATH"] = str(root / "candidate") + ";" + env.get("PATH", "")
    env["ABC"] = str(root / "candidate" / "yosys-abc.exe")

    rows = []
    logs = []
    for case in fe.cases(root):
        top = case["top"]
        case_work = work / top
        case_work.mkdir(parents=True, exist_ok=True)
        gold = case_work / f"{top}_gold.il"
        gate = case_work / f"{top}_gate.il"
        gold_luts = fe.make_lut_netlist(
            baseline, case["source"], top,
            case_work / f"{top}_gold_raw.il", gold,
            args.timeout, env, custom_script=False,
        )
        gate_luts = fe.make_lut_netlist(
            candidate, case["source"], top,
            case_work / f"{top}_gate_raw.il", gate,
            args.timeout, env, custom_script=True,
        )
        status, log = fe.run_whole_miter(
            top, gold, gate, case_work, baseline, env, args.timeout, cell_model
        )
        row = {
            "suite": case["suite"],
            "name": case["name"],
            "top": top,
            "gold_luts": gold_luts,
            "gate_luts": gate_luts,
            "status": status,
        }
        rows.append(row)
        logs.append(
            f"===== {top} =====\n"
            f"status={status} luts={gold_luts}->{gate_luts}\n{log}\n"
        )
        print(f"{top}: {status} luts={gold_luts}->{gate_luts}", flush=True)

    passed = all(row["status"] == "PASS" for row in rows)
    payload = {
        "method": "Yosys whole-design equivalence after ABC9 and before map_cells",
        "cases": rows,
        "pass": passed,
    }
    (out_dir / "result.json").write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    (out_dir / "equiv-all.log").write_text("\n".join(logs), encoding="utf-8")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
