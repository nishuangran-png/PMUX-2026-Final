#!/usr/bin/env bash
# 编译并加载本项目的最小 Yosys 插件。
#
# ⚠️ 不能用 `yosys-config --build`：OSS CAD Suite 里那个脚本把编译器路径
#    写死成打包机的 /usr/local/osxcross/.../clang++，本机没有这个路径。
#    必须像下面这样，用本机 clang++ 手动组装参数。
#
# 用法：
#   cd <仓库根目录>
#   source tools/oss-cad-suite/environment
#   bash experiments/plugin-smoke/build.sh

set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

clang++ $(yosys-config --cxxflags) $(yosys-config --ldflags) -o experiments/plugin-smoke/pmux_hello.so -shared experiments/plugin-smoke/pmux_hello.cc
echo "built: experiments/plugin-smoke/pmux_hello.so"

clang++ $(yosys-config --cxxflags) $(yosys-config --ldflags) -o experiments/plugin-smoke/api_probe.so -shared experiments/plugin-smoke/api_probe.cc
echo "built: experiments/plugin-smoke/api_probe.so"

echo
echo "--- 加载测试 1/2：pmux_hello ---"
yosys -Q -m experiments/plugin-smoke/pmux_hello.so \
  -p "read_verilog rtl/single/pmux_basic.v; proc; opt; pmux_hello; synth -top pmux_basic; pmux_hello"

echo
echo "--- 加载测试 2/2：api_probe ---"
yosys -Q -m experiments/plugin-smoke/api_probe.so \
  -p "read_verilog rtl/single/pmux_basic.v; proc; opt_clean; pmux_api_test"
