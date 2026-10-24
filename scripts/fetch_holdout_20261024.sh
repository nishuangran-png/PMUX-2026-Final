#!/usr/bin/env bash
# 按固定 commit 获取 2026-10-24 最小泛化封存集。
# 默认写入 tools/holdout-20261024/sources；可传一个目标目录作为第一个参数。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$ROOT/tools/holdout-20261024/sources}"
mkdir -p "$DEST"

fetch_repo() {
  local name="$1"
  local url="$2"
  local commit="$3"
  local dir="$DEST/$name"

  if [[ ! -d "$dir/.git" ]]; then
    git init -q "$dir"
    git -C "$dir" remote add origin "$url"
  fi
  git -C "$dir" fetch -q --depth 1 origin "$commit"
  git -C "$dir" checkout -q --detach FETCH_HEAD
  echo "$name $(git -C "$dir" rev-parse HEAD)"
}

fetch_repo verilog-axi https://github.com/alexforencich/verilog-axi.git \
  516bd5dadc3365b7f9e225d2af8fe0b8d804fe53
fetch_repo picorv32 https://github.com/YosysHQ/picorv32.git \
  ef203c2b0a3fb793280f5114941416c425c5b461

# 该仓库仅用于记录兼容性排除证据，不进入面积/EQY 统计。
fetch_repo scr1 https://github.com/Syntacore/scr1.git \
  ebb5e3551a9d93c0ee95f0b767dd878b8927e702

printf '%s\n' "sources -> $DEST"
