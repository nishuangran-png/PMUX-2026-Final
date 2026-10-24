#!/usr/bin/env python3
"""Prove holdout LUT netlists equivalent without EQY partitioning."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from eval_holdout import synth_lut, synth_stats  # noqa: E402


DEFAULT_BASELINE = ROOT / "tools/build/yosys-069-baseline-src/build/yosys"
DEFAULT_CANDIDATE = ROOT / "tools/build/official-v069-formal-src/build-formal/yosys"
DEFAULT_MANIFEST = ROOT / "experiments/holdout-20261024/manifest.json"


def prove(binary: Path, gold: Path, gate: Path, top: str, log: Path,
          timeout: int) -> str:
    script = (
        f"read_rtlil {gold}; rename {top} gold; "
        f"read_rtlil {gate}; rename {top} gate; "
        "equiv_make gold gate equiv; hierarchy -top equiv; "
        "equiv_simple -nogroup; equiv_status -assert"
    )
    with log.open("w", encoding="utf-8") as handle:
        proc = subprocess.run(
            [str(binary), "-p", script], stdout=handle,
            stderr=subprocess.STDOUT, timeout=timeout,
        )
    text = log.read_text(errors="replace")
    if proc.returncode != 0:
        tail = "\n".join(text.strip().splitlines()[-20:])
        raise RuntimeError(f"proof failed (exit {proc.returncode})\n{tail}")
    if "Equivalence successfully proven" not in text:
        tail = "\n".join(text.strip().splitlines()[-20:])
        raise RuntimeError(f"proof did not report success\n{tail}")
    return text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--candidate", type=Path, default=DEFAULT_CANDIDATE)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument("--json", type=Path, default=None)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    for path in (args.baseline, args.candidate, args.manifest):
        if not path.exists():
            parser.error(f"missing input: {path}")

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    by_id = {case["id"]: case for case in manifest["cases"]}
    selected = args.cases or [
        "verilog_axi_priority_encoder_128",
        "verilog_axi_priority_encoder_512",
        "verilog_axi_arbiter_64",
    ]
    missing = [case_id for case_id in selected if case_id not in by_id]
    if missing:
        parser.error("unknown case(s): " + ", ".join(missing))

    workdir = Path(tempfile.mkdtemp(prefix="pmux_lut_equiv_"))
    rows = []
    all_pass = True
    print(
        f"{'case':40} {'base':>6} {'cand':>6} {'reduct':>8} "
        f"{'gold_lut':>9} {'gate_lut':>9} {'delta':>7} {'proof_s':>9}  result"
    )
    try:
        for case_id in selected:
            case = by_id[case_id]
            errors = []
            gold_luts = gate_luts = -1
            base_area = cand_area = -1
            reduction = None
            elapsed = None
            status = "FAIL"
            try:
                base_stats = synth_stats(args.baseline, case, args.timeout)
                cand_stats = synth_stats(args.candidate, case, args.timeout)
                base_area = base_stats["logic"]
                cand_area = cand_stats["logic"]
                if base_area:
                    reduction = (base_area - cand_area) / base_area * 100.0
                gold = workdir / f"{case_id}_gold.il"
                gate = workdir / f"{case_id}_gate.il"
                gold_luts = synth_lut(
                    args.baseline, case, gold, workdir, "gold", args.timeout
                )
                gate_luts = synth_lut(
                    args.candidate, case, gate, workdir, "gate", args.timeout
                )
                started = time.monotonic()
                prove(
                    args.candidate, gold, gate, case["top"],
                    workdir / f"{case_id}_equiv.log", args.timeout,
                )
                elapsed = time.monotonic() - started
                status = "PASS"
            except (RuntimeError, subprocess.TimeoutExpired) as exc:
                errors.append(str(exc))
                all_pass = False

            delta = gold_luts - gate_luts if gold_luts >= 0 else None
            print(
                f"{case_id:40} {base_area:6d} {cand_area:6d} "
                f"{reduction if reduction is not None else 0:7.2f}% "
                f"{gold_luts:9d} {gate_luts:9d} "
                f"{delta if delta is not None else 0:7d} "
                f"{elapsed if elapsed is not None else 0:9.2f}  {status}"
            )
            for error in errors:
                for line in error.splitlines()[:20]:
                    print(f"    {line}")
            rows.append({
                "id": case_id,
                "top": case["top"],
                "baseline_logic": base_area,
                "candidate_logic": cand_area,
                "reduction_pct": reduction,
                "gold_luts": gold_luts,
                "gate_luts": gate_luts,
                "delta_luts": delta,
                "proof_s": elapsed,
                "status": status,
                "errors": errors,
            })
    finally:
        if args.keep:
            print(f"workdir: {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps({"cases": rows, "all_pass": all_pass}, indent=2) + "\n",
            encoding="utf-8",
        )
        print(f"JSON -> {args.json}")
    return 0 if all_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
