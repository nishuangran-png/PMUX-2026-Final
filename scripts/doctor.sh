#!/usr/bin/env bash
# 本机环境体检：一条命令确认「能不能开工」。
#
# 用法：
#   cd <仓库根目录>
#   bash scripts/doctor.sh
#
# 检查项全部通过 = 这台机器具备开发 Yosys 插件的完整条件。

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PASS=0
FAIL=0
ok()   { printf '  \033[32m✓\033[0m %s\n' "$1"; PASS=$((PASS+1)); }
bad()  { printf '  \033[31m✗\033[0m %s\n' "$1"; FAIL=$((FAIL+1)); }
head_() { printf '\n\033[1m%s\033[0m\n' "$1"; }

head_ "1. 工具链"

if [ -f tools/oss-cad-suite/environment ]; then
  ok "OSS CAD Suite 已解压"
  source tools/oss-cad-suite/environment >/dev/null 2>&1
  VER="$(yosys -V 2>/dev/null | head -1 | awk '{print $2}')"
  WANT="$(grep -v '^#' YOSYS_VERSION | tr -d '[:space:]')"
  case "$VER" in
    "$WANT"*) ok "Yosys 版本符合项目锁定值 $WANT（实测 $VER）" ;;
    *)        bad "Yosys 版本是 $VER，但项目锁定的是 $WANT（见 YOSYS_VERSION）" ;;
  esac
else
  bad "tools/oss-cad-suite 不存在，先跑 scripts/download_suite.py"
fi

for t in eqy sby yosys-abc iverilog vvp verilator; do
  if command -v "$t" >/dev/null 2>&1; then ok "$t 可用"; else bad "$t 缺失"; fi
done

head_ "2. 插件开发链路"

if command -v clang++ >/dev/null 2>&1; then
  ok "clang++ 可用（$(clang++ --version | head -1 | sed 's/.*version //;s/ .*//')）"
else
  bad "clang++ 缺失，需要 xcode-select --install"
fi

for h in kernel/yosys.h kernel/register.h kernel/sigtools.h kernel/celltypes.h; do
  if [ -f "tools/oss-cad-suite/share/yosys/include/$h" ]; then ok "头文件 $h"; else bad "头文件 $h 缺失"; fi
done

if [ -f tools/oss-cad-suite/bin/yosys-config ]; then ok "yosys-config 存在（注意 --build 用不了，见 plugin-smoke/README）"; else bad "yosys-config 缺失"; fi

SO="experiments/plugin-smoke/pmux_hello.so"
if clang++ $(yosys-config --cxxflags) $(yosys-config --ldflags) -o "$SO" -shared experiments/plugin-smoke/pmux_hello.cc >/tmp/doctor_build.log 2>&1; then
  if yosys -Q -m "$SO" -p "read_verilog rtl/single/pmux_basic.v; proc; opt; pmux_hello" 2>/dev/null | grep -q "PMUX_HELLO"; then
    ok "插件能编译、能加载、能执行"
  else
    bad "插件编译成功但加载执行失败，见 /tmp/doctor_build.log"
  fi
else
  bad "插件编译失败，见 /tmp/doctor_build.log"
fi

head_ "3. 基线数据"

check_cells() {
  local file="$1" mod="$2" want="$3"
  local got
  got="$(yosys -Q -p "read_verilog $file; synth -top $mod; stat" 2>/dev/null | awk '$2=="cells" && $1 ~ /^[0-9]+$/ {n=$1} END{print n}')"
  if [ "$got" = "$want" ]; then ok "$mod = $got cells"; else bad "$mod = $got cells，应为 $want"; fi
}
check_cells rtl/single/pmux_basic.v   pmux_basic   132
check_cells rtl/single/mux_tree_8to1.v mux_tree_8to1 56

head_ "4. 形式等价验证（EQY）"

EQYDIR="$(mktemp -d)"
cp experiments/eqy-demo/gold.v experiments/eqy-demo/gate_ok.v experiments/eqy-demo/gate_bad.v \
   experiments/eqy-demo/eq_ok.eqy experiments/eqy-demo/eq_bad.eqy "$EQYDIR/" 2>/dev/null
if (cd "$EQYDIR" && eqy eq_ok.eqy >/dev/null 2>&1); then ok "正例 eq_ok：PASS"; else bad "正例 eq_ok 没有通过"; fi
if (cd "$EQYDIR" && eqy eq_bad.eqy >/dev/null 2>&1); then bad "反例 eq_bad 竟然通过了，EQY 不可信"; else ok "反例 eq_bad：正确识别出不等价"; fi
rm -rf "$EQYDIR"

head_ "5. 参考资料"

if [ -d tools/yosys-src ]; then ok "Yosys 源码树在 tools/yosys-src（仅供阅读）"; else bad "tools/yosys-src 缺失"; fi
for d in tools/oss-cad-suite*; do
  case "$d" in *.tgz*|*.parts) continue ;; esac
  [ -d "$d" ] || continue
  ok "已安装：$d"
done

printf '\n\033[1m结果：%d 项通过，%d 项失败\033[0m\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] && printf '这台机器可以开工。\n' || printf '有失败项，先解决再开工。\n'
exit $([ "$FAIL" -eq 0 ] && echo 0 || echo 1)
