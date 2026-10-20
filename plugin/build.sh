#!/usr/bin/env bash
# 编译 pmux_opt 插件。
#
# ⚠️ 不能用 yosys-config --build（它写死了打包机的编译器路径），
#    必须像下面这样手动组装参数。详见 experiments/plugin-smoke/README.md
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build
clang++ $(yosys-config --cxxflags) $(yosys-config --ldflags) -o build/pmux_opt.so -shared plugin/pmux_opt.cc
echo "built: build/pmux_opt.so"
