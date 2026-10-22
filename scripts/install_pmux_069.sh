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
#   3. 只在尚未集成时应用 passes/CMakeLists.txt 与 synth_intel.cc 的补丁。
#
# 安装后编译：
#   cmake -S <YOSYS_SRC_ROOT> -B <YOSYS_SRC_ROOT>/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build <YOSYS_SRC_ROOT>/build --target yosys -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
set -euo pipefail

SRC_ROOT="${1:-$DEFAULT_SRC}"
SRC_ROOT="$(cd "$SRC_ROOT" && pwd -P)"
PATCH="$ROOT/patches/yosys-069/pmux_opt-integration.patch"
NORMALIZE_PATCH="$ROOT/patches/yosys-069/abc9-input-canonicalization.patch"
PASS_SRC="$ROOT/src/yosys-069/passes/pmux"
PASS_DST="$SRC_ROOT/passes/pmux"
CMAKE_FILE="$SRC_ROOT/passes/CMakeLists.txt"
SYNTH_FILE="$SRC_ROOT/techlibs/intel/synth_intel.cc"

die() {
  echo "错误：$*" >&2
  exit 1
}

[ -f "$PASS_SRC/pmux_opt.cc" ] || die "缺少 $PASS_SRC/pmux_opt.cc"
[ -f "$PASS_SRC/CMakeLists.txt" ] || die "缺少 $PASS_SRC/CMakeLists.txt"
[ -f "$PATCH" ] || die "缺少 $PATCH"
[ -f "$NORMALIZE_PATCH" ] || die "缺少 $NORMALIZE_PATCH"
[ -f "$CMAKE_FILE" ] || die "不是 Yosys 源码树：$SRC_ROOT"
[ -f "$SYNTH_FILE" ] || die "不是 Yosys 源码树：$SRC_ROOT"

git_root="$(git -C "$SRC_ROOT" rev-parse --show-toplevel 2>/dev/null || true)"
if [ -n "$git_root" ]; then
  git_root="$(cd "$git_root" && pwd -P)"
fi

if [ "$git_root" = "$SRC_ROOT" ] && tag="$(git -C "$SRC_ROOT" describe --tags --abbrev=0 2>/dev/null)"; then
  if [ "$tag" != "v0.69" ] && [ "${PMUX_ALLOW_VERSION_MISMATCH:-0}" != "1" ]; then
    die "源码树版本是 $tag，不是 v0.69（如确需覆盖，设置 PMUX_ALLOW_VERSION_MISMATCH=1）"
  fi
else
  echo "提示：源码树不是 git 仓库，跳过版本标签检查。" >&2
fi

cmake_hits="$(grep -c '^add_subdirectory(pmux)$' "$CMAKE_FILE" || true)"
synth_hits="$(grep -c 'run("pmux_opt");' "$SYNTH_FILE" || true)"
sort_hits="$(grep -c 'run("sort");' "$SYNTH_FILE" || true)"

if [ "$cmake_hits" = "1" ] && [ "$synth_hits" = "1" ]; then
  mkdir -p "$PASS_DST"
  cp -p "$PASS_SRC/CMakeLists.txt" "$PASS_DST/CMakeLists.txt"
  cp -p "$PASS_SRC/pmux_opt.cc" "$PASS_DST/pmux_opt.cc"
  if [ "$sort_hits" = "0" ]; then
    patch -p1 -d "$SRC_ROOT" --dry-run < "$NORMALIZE_PATCH" >/dev/null || die "ABC9 输入规范化补丁无法应用"
    patch -p1 -d "$SRC_ROOT" < "$NORMALIZE_PATCH" >/dev/null
  fi
  echo "pmux_opt 已集成：$SRC_ROOT"
  exit 0
fi
if [ "$cmake_hits" != "0" ] || [ "$synth_hits" != "0" ]; then
  die "集成状态不完整（CMake 命中 $cmake_hits，synth_intel 命中 $synth_hits），请先检查源码树"
fi

if [ "$git_root" = "$SRC_ROOT" ]; then
  git -C "$SRC_ROOT" apply --check "$PATCH" || die "补丁无法干净应用到 $SRC_ROOT"
  mkdir -p "$PASS_DST"
  cp -p "$PASS_SRC/CMakeLists.txt" "$PASS_DST/CMakeLists.txt"
  cp -p "$PASS_SRC/pmux_opt.cc" "$PASS_DST/pmux_opt.cc"
  git -C "$SRC_ROOT" apply "$PATCH"
else
  patch -p1 -d "$SRC_ROOT" --dry-run < "$PATCH" >/dev/null || die "补丁无法干净应用到 $SRC_ROOT"
  mkdir -p "$PASS_DST"
  cp -p "$PASS_SRC/CMakeLists.txt" "$PASS_DST/CMakeLists.txt"
  cp -p "$PASS_SRC/pmux_opt.cc" "$PASS_DST/pmux_opt.cc"
  patch -p1 -d "$SRC_ROOT" < "$PATCH" >/dev/null
fi

patch -p1 -d "$SRC_ROOT" < "$NORMALIZE_PATCH" >/dev/null

echo "安装完成：$SRC_ROOT"
echo "下一步：cmake -S \"$SRC_ROOT\" -B \"$SRC_ROOT/build\" -DCMAKE_BUILD_TYPE=Release"
echo "        cmake --build \"$SRC_ROOT/build\" --target yosys -j8"
