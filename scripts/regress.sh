#!/usr/bin/env bash
# 回归测试：对全部用例跑「基准 → 优化 → 等价验证 → 性能」，一屏出结果。
#
# 用法：
#   source tools/oss-cad-suite/environment
#   bash scripts/regress.sh                    # 只跑基准（插件不存在时）
#   PMUX_OPT_PLUGIN=build/pmux_opt.so bash scripts/regress.sh
#
# 环境变量：
#   PMUX_OPT_PLUGIN  插件 .so 路径（默认 build/pmux_opt.so）
#   PMUX_OPT_CMD     插件里的命令名（默认 pmux_opt）
#   PMUX_KEEP_TMP    设为 1 则保留中间文件，便于查错
#
# 输出四项：基准单元数 / 优化后单元数 / 降幅 / 等价性。
# 最后一行给合计降幅——这才是官方口径下真正要看的数字。

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PLUGIN="${PMUX_OPT_PLUGIN:-$ROOT/build/pmux_opt.so}"
OPT_CMD="${PMUX_OPT_CMD:-pmux_opt}"

CASES=(
  "single/pmux_basic:pmux_basic"
  "single/pmux_onehot:pmux_onehot"
  "single/pmux_priority:pmux_priority"
  "single/mux_tree_8to1:mux_tree_8to1"
  "mixed/pmux_in_alu:pmux_in_alu"
  "mixed/pmux_nested:pmux_nested"
  "realistic/fsm_ctrl:fsm_ctrl"
  "realistic/cpu_datapath:cpu_datapath"
  "scale/scale_case8:scale_case8"
  "scale/scale_case16:scale_case16"
  "scale/scale_sparse32:scale_sparse32"
  "scale/scale_multi6:scale_multi6"
  "scale/scale_alu:scale_alu"
  "scale/scale_nested:scale_nested"
  "scale/scale_case64:scale_case64"
)
# 说明：scale/ 下的用例由 scripts/gen_cases.py 生成（已有产物直接可用）；
#       fuzz/ 下的随机用例用 scripts/fuzz_equiv.py 单独跑（数量/种子可调）。

# 也可以用「用例清单文件」跑别的集合：
#   bash scripts/regress.sh cases.txt
# 文件每行一条：rtl/ 下的相对路径（不含 .v）:顶层模块名，# 开头为注释。
if [ $# -ge 1 ] && [ -f "$1" ]; then
  CASES=()
  while IFS= read -r line; do
    case "$line" in ''|'#'*) continue;; esac
    CASES+=("$line")
  done < "$1"
fi
MAP_MODE="${PMUX_MAP:-mux_keep}"
EQY_TIMEOUT="${EQY_TIMEOUT:-600}"
# macOS 自带没有 timeout/gtimeout，用 perl 的 alarm 顶上（alarm 会跨 exec 保留）。
EQY_RUN=(perl -e 'alarm shift; exec @ARGV' -- "$EQY_TIMEOUT")

WORK="$(mktemp -d /tmp/pmux_regress.XXXXXX)"
if [ "${PMUX_KEEP_TMP:-}" = "1" ]; then
  echo "中间文件目录: $WORK"
else
  trap '[ -n "${WORK:-}" ] && rm -rf "$WORK"' EXIT
fi

# ⚠️ awk 必须要求第一列是纯数字：yosys 日志里有
#    "Optimizing cells in module ..." 这种行，$2 也是 "cells"，
#    不加数字判断会把 "Optimizing" 当成单元数。
cellcount() {
  "${YOSYS[@]}" -p "$1; stat" 2>/dev/null \
    | awk '$2 == "cells" && $1 ~ /^[0-9]+$/ { n = $1 } END { print (n == "" ? "ERR" : n) }'
}

# 用 /usr/bin/time -l 量耗时与内存峰值（macOS：内存单位是字节）
measure() {
  local cmd="$1" log="$2"
  /usr/bin/time -l "${YOSYS[@]}" -p "$cmd" > /dev/null 2> "$log"
  local real mem
  real="$(awk '/ real /{print $1}' "$log" | head -1)"
  mem="$(awk '/maximum resident set size/{print $1}' "$log" | head -1)"
  echo "$real $mem"
}

YOSYS=(yosys -Q)
HAVE_PLUGIN=0
if [ -f "$PLUGIN" ]; then
  HAVE_PLUGIN=1
  YOSYS+=( -m "$PLUGIN" )
  echo "插件: $PLUGIN"
else
  echo "插件不存在（$PLUGIN）——只跑基准与等价验证，不跑优化。"
fi
echo

printf '%-16s %8s %8s %9s %8s %9s %8s\n' "用例" "基准" "优化后" "降幅" "耗时s" "内存MB" "等价"
printf '%.0s─' {1..80}; echo

TOT_A=0
TOT_B=0
FAILED=0

for entry in "${CASES[@]}"; do
  SRC="rtl/${entry%%:*}.v"
  MOD="${entry##*:}"

  # 映射模式（见 reports/008，四种口径的数字不可混着比）：
  #   default   官方 synth 内置 ABC：门级 cell 数，赛题 baseline 的口径
  #   mux_keep  面积导向 + 保留 mux 结构：本项目的推荐流程（默认）
  #   gate_area 面积导向但会打散 mux 结构：反面对照
  #   lut_area  LUT 映射：FPGA 实现口径
  case "$MAP_MODE" in
    default)   SYNTH_TAIL="synth -top $MOD" ;;
    mux_keep)  SYNTH_TAIL="synth -noabc -top $MOD; abc -script $ROOT/scripts/abc_mux_keep.abc" ;;
    gate_area) SYNTH_TAIL="synth -noabc -top $MOD; abc -script $ROOT/scripts/abc_gate_area.abc" ;;
    lut_area)  SYNTH_TAIL="synth -noabc -top $MOD; abc -script $ROOT/scripts/abc_lut_area.abc" ;;
    *) echo "未知 MAP_MODE: $MAP_MODE（可选 default/mux_keep/gate_area/lut_area）" >&2; exit 2 ;;
  esac

  BASE="$(cellcount "read_verilog $SRC; $SYNTH_TAIL; opt_clean -purge")"
  if ! [[ "$BASE" =~ ^[0-9]+$ ]]; then
    echo "❌ $MOD 的基准单元数解析失败：$BASE" >&2
    exit 1
  fi
  TOT_A=$((TOT_A + BASE))

  if [ "$HAVE_PLUGIN" = "0" ]; then
    printf '%-16s %8s %8s %9s %8s %9s %8s\n' "$MOD" "$BASE" "-" "-" "-" "-" "-"
    continue
  fi

  IL="$WORK/$MOD.il"
  FLOW="read_verilog $SRC; proc; opt; $OPT_CMD; $SYNTH_TAIL; opt_clean -purge; write_rtlil $IL"
  read -r RC_RT RC_MEM < <(measure "$FLOW" "$WORK/$MOD.time")

  OPT="$(cellcount "read_verilog $SRC; proc; opt; $OPT_CMD; $SYNTH_TAIL; opt_clean -purge")"
  if ! [[ "$OPT" =~ ^[0-9]+$ ]]; then
    echo "❌ $MOD 的优化后单元数解析失败：$OPT" >&2
    exit 1
  fi
  TOT_B=$((TOT_B + OPT))

  # 官方口径的等价验证：原始 RTL vs 优化后的门级网表
  EQDIR="$WORK/eq_$MOD"
  mkdir -p "$EQDIR"
  # gate 侧是门级网表，prep 会按名字比对内部信号；用 -flatten 只留端口行为
  # （否则优化掉的中间 wire 会被当成等价点，见 reports/007）。
  cat > "$EQDIR/eq.eqy" <<EOF
[gold]
read_verilog $ROOT/$SRC
prep -top $MOD

[gate]
read_rtlil $IL
prep -top $MOD

[strategy sat]
use sat
EOF
  if ( cd "$EQDIR" && "${EQY_RUN[@]}" eqy eq.eqy ) > "$EQDIR/out.log" 2>&1; then
    EQ="PASS"
  else
    EQ="FAIL"
    FAILED=$((FAILED + 1))
  fi

  MEM_MB="$(awk -v m="$RC_MEM" 'BEGIN{ if (m=="") print "-"; else printf "%.0f", m/1048576 }')"
  printf '%-16s %8s %8s %9s %8s %9s %8s\n' \
    "$MOD" "$BASE" "$OPT" \
    "$(python3 -c "print('%+.1f%%' % (($OPT-$BASE)/$BASE*100))")" \
    "$RC_RT" "$MEM_MB" "$EQ"
done

printf '%.0s─' {1..80}; echo
if [ "$HAVE_PLUGIN" = "1" ] && [ "$TOT_A" -gt 0 ]; then
  printf '%-16s %8s %8s %9s\n' "合计" "$TOT_A" "$TOT_B" \
    "$(python3 -c "print('%+.1f%%' % (($TOT_B-$TOT_A)/$TOT_A*100))")"
  echo
  echo "官方满分线 20%，及格线 10%（对比默认 synth 的单元总数）。"
else
  printf '%-16s %8s\n' "合计（基准）" "$TOT_A"
fi

if [ "$FAILED" -gt 0 ]; then
  echo
  echo "⚠️  有 $FAILED 个用例不等价 —— 按赛题规则这是「整个作品作废」级别的问题，优先修。"
  exit 1
fi
exit 0
