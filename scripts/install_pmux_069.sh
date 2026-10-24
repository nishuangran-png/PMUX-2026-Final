#!/usr/bin/env bash
# 把仓库内的 pmux_opt v2 源码安装到干净的 Yosys 0.69 源码树。
#
# 用法：
#   bash scripts/install_pmux_069.sh [YOSYS_SRC_ROOT]
#
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEFAULT_SRC="$ROOT/tools/build/official-v069-opt"
[ -d "$DEFAULT_SRC" ] || DEFAULT_SRC="$ROOT/tools/build/yosys-069-src"

# 默认安装到官网 v0.69 的优化源码树（若尚未创建则回退到旧工作树）。脚本会：
#   1. 校验源码树是 Yosys 0.69；
#   2. 复制 passes/pmux/；
#   3. 只在尚未集成时应用 passes/CMakeLists.txt 与 synth_intel.cc 的补丁；
#   4. 已集成树会同步最新的 pmux_opt.cc，并校验 LUT 折叠接线不可缺失。
#
# 安装只包含 PMUX/ROM 算法及其必要的 synth_intel 接线，不修改 ABC9 输入顺序。
#
# 安装后编译：
#   cmake -S <YOSYS_SRC_ROOT> -B <YOSYS_SRC_ROOT>/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build <YOSYS_SRC_ROOT>/build --target yosys -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
set -euo pipefail

SRC_ROOT="${1:-$DEFAULT_SRC}"
SRC_ROOT="$(cd "$SRC_ROOT" && pwd -P)"
OFFICIAL_COMMIT="9f75ca1f9834a39a863915b5dae0c7b1e33533bc"
PATCH="$ROOT/patches/yosys-069/pmux_opt-integration.patch"
PASS_SRC="$ROOT/src/yosys-069/passes/pmux"
PASS_DST="$SRC_ROOT/passes/pmux"
CMAKE_FILE="$SRC_ROOT/passes/CMakeLists.txt"
SYNTH_FILE="$SRC_ROOT/techlibs/intel/synth_intel.cc"
INTEL_CMAKE_FILE="$SRC_ROOT/techlibs/intel/CMakeLists.txt"
FFMAP_SRC="$ROOT/src/yosys-069/techlibs/intel/common/ff_map_pn1.v"
FFMAP_DST="$SRC_ROOT/techlibs/intel/common/ff_map_pn1.v"

die() {
  echo "错误：$*" >&2
  exit 1
}

[ -f "$PASS_SRC/pmux_opt.cc" ] || die "缺少 $PASS_SRC/pmux_opt.cc"
[ -f "$PASS_SRC/CMakeLists.txt" ] || die "缺少 $PASS_SRC/CMakeLists.txt"
[ -f "$FFMAP_SRC" ] || die "缺少 $FFMAP_SRC"
[ -f "$PATCH" ] || die "缺少 $PATCH"
[ -f "$CMAKE_FILE" ] || die "不是 Yosys 源码树：$SRC_ROOT"
[ -f "$SYNTH_FILE" ] || die "不是 Yosys 源码树：$SRC_ROOT"
[ -f "$INTEL_CMAKE_FILE" ] || die "缺少 Intel 资源清单：$INTEL_CMAKE_FILE"

git_root="$(git -C "$SRC_ROOT" rev-parse --show-toplevel 2>/dev/null || true)"
if [ -n "$git_root" ]; then
  git_root="$(cd "$git_root" && pwd -P)"
fi

recorded_commit="$(tr -d '[:space:]' < "$SRC_ROOT/.gitcommit" 2>/dev/null || true)"
head_commit=""
if [ "$git_root" = "$SRC_ROOT" ]; then
  head_commit="$(git -C "$SRC_ROOT" rev-parse HEAD 2>/dev/null || true)"
fi
if [ "${PMUX_ALLOW_VERSION_MISMATCH:-0}" != "1" ]; then
  if [ "$recorded_commit" != "$OFFICIAL_COMMIT" ]; then
    die "源码树 .gitcommit=${recorded_commit:-缺失}，要求官方 v0.69 commit $OFFICIAL_COMMIT（如确需覆盖，设置 PMUX_ALLOW_VERSION_MISMATCH=1）"
  fi
  if [ -n "$head_commit" ] && [ "$head_commit" != "$OFFICIAL_COMMIT" ]; then
    die "源码树 HEAD=$head_commit，要求官方 v0.69 commit $OFFICIAL_COMMIT（如确需覆盖，设置 PMUX_ALLOW_VERSION_MISMATCH=1）"
  fi
fi

cmake_hits="$(grep -cF 'add_subdirectory(pmux)' "$CMAKE_FILE" || true)"
synth_hits="$(grep -cF 'run("pmux_opt");' "$SYNTH_FILE" || true)"
deferred_marker_hits="$(grep -cF 'cell->name.begins_with("$pmux_deferred_lut")' "$SYNTH_FILE" || true)"
deferred_select_hits="$(grep -cF 'select -set pmux_deferred_luts */c:*pmux_deferred_lut*' "$SYNTH_FILE" || true)"
deferred_techmap_hits="$(grep -cF 'techmap -map +/techmap.v * @pmux_deferred_luts %d' "$SYNTH_FILE" || true)"
fold_luts_hits="$(grep -cF 'run("pmux_opt -fold_luts");' "$SYNTH_FILE" || true)"
intel_cmake_hits="$(grep -cF 'common/ff_map_pn1.v' "$INTEL_CMAKE_FILE" || true)"
pn1_legalize_hits="$(grep -cF 'dfflegalize -cell $_DFFE_PN0P_ 01 -cell $_DFFE_PN1P_ 01' "$SYNTH_FILE" || true)"
pn1_techmap_hits="$(grep -cF 'techmap -map +/intel/common/ff_map.v -map +/intel/common/ff_map_pn1.v' "$SYNTH_FILE" || true)"

if [ "$cmake_hits" = "1" ] && [ "$synth_hits" = "1" ] && \
   [ "$deferred_marker_hits" = "1" ] && [ "$deferred_select_hits" = "1" ] && \
   [ "$deferred_techmap_hits" = "1" ] && \
   [ "$fold_luts_hits" = "1" ] && [ "$intel_cmake_hits" = "1" ] && \
   [ "$pn1_legalize_hits" = "1" ] && [ "$pn1_techmap_hits" = "1" ]; then
  mkdir -p "$PASS_DST"
  mkdir -p "$(dirname "$FFMAP_DST")"
  cp -p "$PASS_SRC/CMakeLists.txt" "$PASS_DST/CMakeLists.txt"
  cp -p "$PASS_SRC/pmux_opt.cc" "$PASS_DST/pmux_opt.cc"
  cp -p "$FFMAP_SRC" "$FFMAP_DST"
  echo "pmux_opt 已集成：$SRC_ROOT"
  exit 0
fi
if [ "$cmake_hits" != "0" ] || [ "$synth_hits" != "0" ] || \
   [ "$deferred_marker_hits" != "0" ] || [ "$deferred_select_hits" != "0" ] || \
   [ "$deferred_techmap_hits" != "0" ] || \
   [ "$fold_luts_hits" != "0" ] || [ "$intel_cmake_hits" != "0" ] || \
   [ "$pn1_legalize_hits" != "0" ] || [ "$pn1_techmap_hits" != "0" ]; then
  die "集成状态不完整（CMake=$cmake_hits，pass=$synth_hits，marker=$deferred_marker_hits，select=$deferred_select_hits，techmap=$deferred_techmap_hits，fold_luts=$fold_luts_hits，intel_cmake=$intel_cmake_hits，pn1_legalize=$pn1_legalize_hits，pn1_techmap=$pn1_techmap_hits），请使用干净的 Yosys 0.69 源码树"
fi

if [ "$git_root" = "$SRC_ROOT" ]; then
  git -C "$SRC_ROOT" apply --check "$PATCH" || die "补丁无法干净应用到 $SRC_ROOT"
  mkdir -p "$PASS_DST"
  mkdir -p "$(dirname "$FFMAP_DST")"
  cp -p "$PASS_SRC/CMakeLists.txt" "$PASS_DST/CMakeLists.txt"
  cp -p "$PASS_SRC/pmux_opt.cc" "$PASS_DST/pmux_opt.cc"
  cp -p "$FFMAP_SRC" "$FFMAP_DST"
  git -C "$SRC_ROOT" apply "$PATCH"
else
  patch -p1 -d "$SRC_ROOT" --dry-run < "$PATCH" >/dev/null || die "补丁无法干净应用到 $SRC_ROOT"
  mkdir -p "$PASS_DST"
  mkdir -p "$(dirname "$FFMAP_DST")"
  cp -p "$PASS_SRC/CMakeLists.txt" "$PASS_DST/CMakeLists.txt"
  cp -p "$PASS_SRC/pmux_opt.cc" "$PASS_DST/pmux_opt.cc"
  cp -p "$FFMAP_SRC" "$FFMAP_DST"
  patch -p1 -d "$SRC_ROOT" < "$PATCH" >/dev/null
fi

echo "安装完成：$SRC_ROOT"
echo "下一步：cmake -S \"$SRC_ROOT\" -B \"$SRC_ROOT/build\" -DCMAKE_BUILD_TYPE=Release"
echo "        cmake --build \"$SRC_ROOT/build\" --target yosys -j8"
