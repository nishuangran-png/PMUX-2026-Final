#!/usr/bin/env python3
"""Formal equivalence for the frozen PMUX final flow using EQY."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess


HELPER = Path(__file__).with_name("li_acceptance.py")
if not HELPER.is_file():
    HELPER = (
        Path(__file__).parent
        / "li-delivery"
        / "fpga2026fpga2026-codex-test4-rom-lowering-20261023"
        / "scripts"
        / "li_acceptance.py"
    )
spec = importlib.util.spec_from_file_location("li_acceptance", HELPER)
li = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(li)


def cases(root: Path):
    official = root / "cases" / "official"
    five = root / "cases" / "five"
    result = [
        {"suite": "official", "name": name, "top": name,
         "source": official / name / f"{name}.v"}
        for name in ("test1", "test2", "test3", "test4")
    ]
    result.extend(
        {"suite": "five", "name": name, "top": name, "source": five / f"{name}.v"}
        for name in (
            "pmux_case01_balanced",
            "pmux_case02_pipeline",
            "pmux_case03_crossbar",
            "pmux_case04_feedback",
            "pmux_case05_nested",
        )
    )
    return result


def make_lut_netlist(binary: Path, source: Path, top: str, raw: Path,
                     final: Path, timeout: int, env: dict[str, str],
                     custom_script: bool) -> int:
    abc = (
        "abc9 -lut 4 -W 300 -script +/intel/cycloneiv/pmux_abc9.abc"
        if custom_script else
        "abc9 -lut 4 -W 300"
    )
    script = (
        f"read_verilog {source.name}; "
        f"synth_intel -family cycloneiv -top {top} -run begin:map_ffs; "
        f"{abc}; clean; opt -fast; clean; "
        f"write_rtlil {raw.as_posix()}"
    )
    li.run_captured([str(binary), "-q", "-p", script], source.parent, env, timeout)
    li.keep_top_module(raw, final, top)
    if not final.exists() or not final.stat().st_size:
        raise RuntimeError(f"{top}: failed to extract top module for EQY")
    return sum(
        line.startswith("  cell $lut")
        for line in final.read_text(encoding="utf-8", errors="replace").splitlines()
    )


def run_whole_miter(top: str, gold: Path, gate: Path, work: Path,
                    baseline: Path, env: dict[str, str], timeout: int,
                    cell_model: Path) -> tuple[str, str]:
    script = (
        f"read_verilog -sv {cell_model.as_posix()}; "
        f"read_rtlil {gold.name}; rename {top} gold; "
        f"read_rtlil {gate.name}; rename {top} gate; "
        "flatten gate; proc; opt -fast; "
        "equiv_make gold gate equiv; hierarchy -top equiv; "
        "proc; async2sync; opt -fast; "
        "setundef -zero -init -undriven; equiv_simple -undef; "
        "equiv_induct -undef; equiv_status -assert"
    )
    proc = subprocess.run(
        [str(baseline), "-Q", "-p", script], cwd=work, env=env,
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        timeout=timeout,
    )
    log = proc.stdout + proc.stderr
    if proc.returncode == 0:
        return "PASS", log
    if "Equivalence check failed" in log:
        return "FAIL", log
    return "ERROR", log


def run_eqy(case: dict, baseline: Path, candidate: Path, eqy: Path,
            work_root: Path, timeout: int, env: dict[str, str],
            cell_model: Path) -> dict:
    top = case["top"]
    work = work_root / top
    work.mkdir(parents=True, exist_ok=True)
    gold = work / f"{top}_gold.il"
    gate = work / f"{top}_gate.il"
    gold_luts = make_lut_netlist(
        baseline, case["source"], top, work / f"{top}_gold_raw.il",
        gold, timeout, env, custom_script=False,
    )
    gate_luts = make_lut_netlist(
        candidate, case["source"], top, work / f"{top}_gate_raw.il",
        gate, timeout, env, custom_script=True,
    )
    config = work / f"{top}.eqy"
    config.write_text(
        f"[gold]\nread_rtlil {gold.name}\nprep -top {top}\n\n"
        f"[gate]\nread_verilog -sv {cell_model.as_posix()}\n"
        f"read_rtlil {gate.name}\nprep -top {top}\n\n"
        "[strategy sat]\nuse sat\ndepth 20\n",
        encoding="utf-8",
    )
    eqy_dir = f"eqy_{top}"
    setup = subprocess.run(
        [str(eqy), "-m", "-d", eqy_dir, "-f", config.name],
        cwd=work, env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=timeout,
    )
    log = "===== EQY setup =====\n" + setup.stdout + setup.stderr
    if setup.returncode:
        partition = "ERROR"
    else:
        make_exe = shutil.which("make", path=env.get("PATH"))
        if make_exe is None:
            partition = "ERROR"
            log += "\nERROR: make executable not found in PATH\n"
        else:
            makefile = work / eqy_dir / "strategies.mk"
            makefile.write_text(
                makefile.read_text(encoding="utf-8").replace(
                    "source run.sh", "source ./run.sh"
                ),
                encoding="utf-8",
            )
            make_run = subprocess.run(
                [make_exe, "-f", "strategies.mk"],
                cwd=work / eqy_dir, env=env, capture_output=True, text=True,
                encoding="utf-8", errors="replace", timeout=timeout,
            )
            log += "\n===== EQY strategies.mk =====\n" + make_run.stdout + make_run.stderr
            status_files = sorted(
                (work / eqy_dir / "strategies").glob("*/*/status")
            )
            statuses = {
                path.read_text(encoding="utf-8", errors="replace").strip()
                for path in status_files
            }
            log += "\n===== partition statuses =====\n" + "\n".join(
                f"{path.parent.parent.name}: "
                f"{path.read_text(encoding='utf-8', errors='replace').strip()}"
                for path in status_files
            ) + "\n"
            if make_run.returncode == 0 and status_files and statuses == {"PASS"}:
                partition = "PASS"
            elif "FAIL" in statuses:
                partition = "FAIL"
            else:
                partition = "ERROR"
    whole = None
    whole_log = ""
    status = partition
    if partition in {"FAIL", "ERROR"}:
        whole, whole_log = run_whole_miter(
            top, gold, gate, work, baseline, env, timeout, cell_model
        )
        if whole == "PASS":
            status = "PASS*"
    return {
        "status": status,
        "eqy_partition": partition,
        "whole_miter": whole,
        "gold_luts": gold_luts,
        "gate_luts": gate_luts,
        "log": log,
        "whole_log": whole_log,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--eqy", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--work-dir", required=True)
    parser.add_argument("--cell-model", required=True)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()

    root = Path(args.root).resolve()
    baseline = Path(args.baseline).resolve()
    candidate = Path(args.candidate).resolve()
    eqy = Path(args.eqy).resolve()
    out_dir = Path(args.out_dir).resolve()
    work_root = Path(args.work_dir).resolve()
    cell_model = Path(args.cell_model).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    work_root.mkdir(parents=True, exist_ok=True)
    env = li.child_env(eqy)
    bin_override = os.environ.get("PMUX_BIN_DIR")
    if bin_override:
        bin_dir = Path(bin_override).resolve()
    else:
        bin_dir = root / "bin"
        if not bin_dir.is_dir():
            bin_dir = root / "runtime" / "bin"
    env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")
    env["ABC"] = str(bin_dir / "yosys-abc.exe")
    rows = []
    logs = []
    for case in cases(root):
        print(f"{case['name']}: EQY", flush=True)
        result = run_eqy(case, baseline, candidate, eqy, work_root,
                         args.timeout, env, cell_model)
        rows.append({**case, "source": str(case["source"]), **{
            key: value for key, value in result.items()
            if key not in {"log", "whole_log"}
        }})
        logs.append(
            f"===== {case['name']} =====\n"
            f"status={result['status']} partition={result['eqy_partition']} "
            f"whole_miter={result['whole_miter']} "
            f"luts={result['gold_luts']}->{result['gate_luts']}\n"
            f"{result['log']}\n{result['whole_log']}\n"
        )
        print(
            f"  result={result['status']} partition={result['eqy_partition']} "
            f"whole={result['whole_miter']} luts={result['gold_luts']}"
            f"->{result['gate_luts']}",
            flush=True,
        )

    payload = {
        "baseline": str(baseline),
        "candidate": str(candidate),
        "candidate_sha256": li.sha256(candidate),
        "eqy": str(eqy),
        "cases": rows,
        "pass": all(row["status"] in {"PASS", "PASS*"} for row in rows),
    }
    (out_dir / "result.json").write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    (out_dir / "eqy-all.log").write_text("\n".join(logs), encoding="utf-8")
    return 0 if payload["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
