#!/usr/bin/env python3
"""Run the unified synthesis command and preserve raw logs for both arms."""

from __future__ import annotations

import csv
import os
from pathlib import Path
import re
import subprocess
import sys


CELL = re.compile(r"^\s+(\d+)\s+(\S+)\s*$")


def main() -> int:
    root = Path(sys.argv[1]).resolve()
    rows = []
    for arm in ("baseline", "candidate"):
        bindir = root / arm
        env = os.environ.copy()
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        env["ABC"] = str(bindir / "yosys-abc.exe")
        env["PYTHONUTF8"] = "1"
        for suite in ("official", "five"):
            for source in sorted((root / "cases" / suite).rglob("*.v")):
                top = source.stem
                command = (
                    f"read_verilog {source.name}; "
                    f"synth_intel -family cycloneiv -top {top}; stat"
                )
                proc = subprocess.run(
                    [str(bindir / "yosys.exe"), "-Q", "-p", command],
                    cwd=source.parent,
                    env=env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                )
                log_dir = root / "logs" / "synthesis" / arm
                log_dir.mkdir(parents=True, exist_ok=True)
                (log_dir / f"{top}.log").write_text(
                    proc.stdout + f"\nEXIT_CODE={proc.returncode}\n",
                    encoding="utf-8",
                )
                if proc.returncode:
                    raise RuntimeError(f"{arm}/{top}: exit {proc.returncode}")
                marker = f"=== {top} ==="
                text = proc.stdout[proc.stdout.rfind(marker):]
                cells = {}
                for line in text.splitlines():
                    match = CELL.match(line)
                    if match:
                        cells[match.group(2)] = int(match.group(1))
                rows.append({
                    "arm": arm,
                    "suite": suite,
                    "top": top,
                    "cycloneiv_lcell_comb": cells.get("cycloneiv_lcell_comb", 0),
                    "$not": cells.get("$not", 0),
                    "dffeas": cells.get("dffeas", 0),
                    "$lut": cells.get("$lut", 0),
                })
    with (root / "logs" / "synthesis" / "stats.csv").open(
        "w", encoding="utf-8", newline=""
    ) as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"collected {len(rows)} synthesis logs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
