#!/usr/bin/env bash
# 批量测量：对 rtl/ 下所有用例跑多组综合流程，输出单元数对照表。
#
# 用途：回答「上游默认流程到底还剩多少优化空间」。
# 用法：
#   source tools/oss-cad-suite/environment
#   bash scripts/measure.sh
#
# 版本以 YOSYS_VERSION 为准（脚本头部会打印实际版本）；换版本必须整体重跑。

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CASES=(
  "single/pmux_basic"
  "single/pmux_onehot"
  "single/pmux_priority"
  "single/mux_tree_8to1"
  "mixed/pmux_in_alu"
  "mixed/pmux_nested"
)

# 每个变体：标签|命令模板（FILE=源文件，MOD=模块名）
VARIANTS=(
  "默认 synth|read_verilog FILE; synth -top MOD"
  "+pmuxtree|read_verilog FILE; proc; opt; pmuxtree; synth -top MOD"
  "synth -noabc|read_verilog FILE; synth -top MOD -noabc"
  "+pmuxtree -noabc|read_verilog FILE; proc; opt; pmuxtree; synth -top MOD -noabc"
  "synth -lut 4|read_verilog FILE; synth -top MOD -lut 4"
  "+pmuxtree -lut 4|read_verilog FILE; proc; opt; pmuxtree; synth -top MOD -lut 4"
  "-noabc +muxcover|read_verilog FILE; synth -top MOD -noabc; muxcover -mux4 -mux8 -mux16; opt_clean"
  "+pmuxtree -noabc +muxcover|read_verilog FILE; proc; opt; pmuxtree; synth -top MOD -noabc; muxcover -mux4 -mux8 -mux16; opt_clean"
)

cellcount() {
  yosys -Q -p "$1; stat" 2>/dev/null \
    | awk '$2 == "cells" && $1 ~ /^[0-9]+$/ { n = $1 } END { print (n == "" ? "ERR" : n) }'
}

printf '%-20s' "用例"
for v in "${VARIANTS[@]}"; do printf '%14s' "${v%%|*}"; done
printf '\n'

for c in "${CASES[@]}"; do
  file="rtl/$c.v"
  mod="$(basename "$c")"
  printf '%-20s' "$mod"
  for v in "${VARIANTS[@]}"; do
    cmd="${v#*|}"
    cmd="${cmd//FILE/$file}"
    cmd="${cmd//MOD/$mod}"
    printf '%14s' "$(cellcount "$cmd")"
  done
  printf '\n'
done
