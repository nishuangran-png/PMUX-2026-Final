#!/usr/bin/env bash
# 对「基准实现 vs 待验证实现」跑一次 EQY 形式等价验证。
#
# 用法：
#   bash scripts/eqy_pair.sh <gold.v> <gate.v> <顶层模块名> [--keep]
#
# 说明：
#   - 两个文件里必须有同名、同端口的顶层模块（EQY 的硬要求）。
#   - 在临时目录里跑，不会污染仓库、也不会撞上「目录已存在」那个报错。
#   - 返回 0 = PASS，1 = FAIL，2 = 出错。
#
# ⚠️ 等价验证一律用 EQY，不要手搓 miter -equiv + sat。
#    实测那套会给出假反例，详见 reports/004 第四节。

set -u

if [ $# -lt 3 ]; then
  echo "用法: bash scripts/eqy_pair.sh <gold.v> <gate.v> <顶层模块名> [--keep]" >&2
  exit 2
fi

abspath() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }

GOLD="$(abspath "$1")"
GATE="$(abspath "$2")"
MOD="$3"
KEEP="${4:-}"

for f in "$GOLD" "$GATE"; do
  [ -f "$f" ] || { echo "找不到文件: $f" >&2; exit 2; }
done

WORK="$(mktemp -d /tmp/eqy_pair.XXXXXX)"
trap '[ -n "${WORK:-}" ] && rm -rf "$WORK"' EXIT

cat > "$WORK/eq.eqy" <<EOF
# 自动生成：$MOD
[gold]
read_verilog $GOLD
prep -top $MOD

[gate]
read_verilog $GATE
prep -top $MOD

[strategy sat]
use sat
EOF

( cd "$WORK" && eqy eq.eqy ) > "$WORK/out.log" 2>&1
RC=$?

if grep -q "DONE (PASS" "$WORK/out.log"; then
  echo "PASS  $MOD"
  [ -n "$KEEP" ] && { echo "  日志保留在 $WORK/out.log"; trap - EXIT; }
  exit 0
elif grep -q "DONE (FAIL" "$WORK/out.log"; then
  echo "FAIL  $MOD（不等价）"
  grep -E "Failed to prove|partition" "$WORK/out.log" | head -5
  [ -n "$KEEP" ] && { echo "  日志保留在 $WORK/out.log"; trap - EXIT; }
  exit 1
else
  echo "ERROR $MOD（EQY 没跑起来，rc=$RC）"
  tail -15 "$WORK/out.log"
  [ -n "$KEEP" ] && { echo "  日志保留在 $WORK/out.log"; trap - EXIT; }
  exit 2
fi
