#!/usr/bin/env bash
# PMUX 优化流程（交付用的一键脚本）：任意 Verilog → 优化后的门级网表 + 单元数。
#
# 用法：
#   bash scripts/pmux_synth.sh <输入.v> <顶层模块> [输出.v]
#
# 环境变量：
#   PMUX_MAP  映射口径：mux_keep（默认）/ default / gate_area / lut_area
#   PMUX_AUTO 是否启用「自动选优」：1（默认）会同时跑「加 pass」和「不加 pass」
#             两条流程，取单元数更小的那份。这样**最坏情况也不会比基准差**。
#             设 0 则只跑加 pass 的那条。
#
# 为什么需要自动选优：代价模型只能估算，而 ABC 映射还会再改变一次结果，
# 随机模糊测试里出现过「改写后反而变差」的用例（见 reports/004-开工摸底与开发链路验证.md）。
# 与其赌模型准，不如两条都跑——千级节点电路一条流程不到 1 秒，双倍开销可接受。
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

IN="${1:?用法: pmux_synth.sh <输入.v> <顶层模块> [输出.v]}"
TOP="${2:?缺少顶层模块名}"
OUT="${3:-}"

MAP_MODE="${PMUX_MAP:-mux_keep}"
case "$MAP_MODE" in
  default)   TAIL="synth -top $TOP" ;;
  mux_keep)  TAIL="synth -noabc -top $TOP; abc -script $ROOT/scripts/abc_mux_keep.abc" ;;
  gate_area) TAIL="synth -noabc -top $TOP; abc -script $ROOT/scripts/abc_gate_area.abc" ;;
  lut_area)  TAIL="synth -noabc -top $TOP; abc -script $ROOT/scripts/abc_lut_area.abc" ;;
  *) echo "未知 PMUX_MAP: $MAP_MODE" >&2; exit 2 ;;
esac

[ -f "$ROOT/build/pmux_opt.so" ] || { echo "插件未编译，先跑 bash plugin/build.sh" >&2; exit 1; }

TMP="$(mktemp -d /tmp/pmux_synth.XXXXXX)"
trap '[ -n "${TMP:-}" ] && rm -rf "$TMP"' EXIT

# 跑一条流程，写入网表并回显单元数（取日志里最后一个 "N cells"）。
run_variant() {
  local use_pass="$1" out="$2" log
  local pre="" plugin=()
  if [ "$use_pass" = "1" ]; then
    pre="proc; opt; pmux_opt;"
    plugin=(-m "$ROOT/build/pmux_opt.so")
  fi
  log="$(yosys -Q ${plugin[@]+"${plugin[@]}"} -p \
    "read_verilog $IN; $pre $TAIL; opt_clean -purge; write_verilog -noattr $out; stat" 2>&1)" || {
      echo "$log" >&2; return 1; }
  echo "$log" | awk '$2 == "cells" && $1 ~ /^[0-9]+$/ { n = $1 } END { print (n == "" ? "ERR" : n) }'
}

if [ "${PMUX_AUTO:-1}" = "1" ]; then
  BASE="$(run_variant 0 "$TMP/base.v")"
  OPT="$(run_variant 1 "$TMP/opt.v")"
  if [ "$BASE" = "ERR" ] || [ "$OPT" = "ERR" ]; then
    echo "流程出错，见上面的 yosys 日志" >&2; exit 1
  fi
  if [ "$OPT" -le "$BASE" ]; then
    CHOSEN="$TMP/opt.v"; PICK="加 pass"
  else
    CHOSEN="$TMP/base.v"; PICK="不加 pass（改写后反而更大，自动回退）"
  fi
  printf '输入: %s  顶层: %s  映射: %s\n' "$IN" "$TOP" "$MAP_MODE"
  printf '基准 %s 单元 → 优化 %s 单元（%s）  采用: %s\n' \
    "$BASE" "$OPT" "$(python3 -c "print('%+.1f%%' % (($OPT-$BASE)/$BASE*100))")" "$PICK"
  if [ -n "$OUT" ]; then
    cp "$CHOSEN" "$OUT"
    echo "网表已写入: $OUT"
  fi
else
  OPT="$(run_variant 1 "$TMP/opt.v")"
  printf '输入: %s  顶层: %s  映射: %s  只跑加 pass 的流程 → %s 单元\n' \
    "$IN" "$TOP" "$MAP_MODE" "$OPT"
  if [ -n "$OUT" ]; then
    cp "$TMP/opt.v" "$OUT"
    echo "网表已写入: $OUT"
  fi
fi
