# 012 · 本机 Yosys 0.69 源码构建完成（可改 synth_intel、可重编译）

日期：2026-10-15 凌晨
状态：✅ 已构建并通过官方基线验证

## 一、为什么必须自建

官方口径要求把优化 pass **编进 yosys 主程序**（改 `synth_intel.cc`），禁止 plugin `.so` 方案。
本机原先只有预编译套件二进制，无法改源码 → 必须自建。

## 二、构建结果

| 项 | 值 |
|---|---|
| 源码 | `tools/build/yosys-069-src/`，取自 commit `e8db64c6`（2026-09-21），即 OSS CAD Suite 0.69+117 的同一 commit |
| 可执行 | `tools/build/yosys-069-src/build/yosys` |
| 版本输出 | `Yosys 0.69 (git sha1 ad7ff16f3, Release, AppleClang 16.0.0)` |
| ABC | 外挂套件自带 `tools/oss-cad-suite/bin/yosys-abc`（与官方二进制同源） |

**基线验证（官方流程，官方 test1）**：
`286 cycloneiv_lcell_comb + 222 dffeas + 2 $not` —— 与套件二进制完全一致 ✓

## 三、补装的工具链（都在 tools/ 下，gitignored，不会进团队仓库）

| 组件 | 位置 | 备注 |
|---|---|---|
| bison 3.8.2 | `tools/build-tools/bison` | macOS 自带 2.3 太老 |
| flex 2.6.4 | `tools/build-tools/flex` | **必须官方 2.6.4**：Apple 自带 flex-35 生成的 `LexerInput` 用 `size_t`，与 yosys 自带 `FlexLexer.h`（`int`）不兼容，会编译报错 |
| cmake 4.4.4 + ninja | `tools/oss-cad-suite/bin`（pip 装的） | 0.69 已全面转 CMake 构建（没有 Makefile 了） |
| 5 个子模块 | 已填进源码树 | cxxopts / fmt / tomlplusplus / boost_regex / symfpu（GitHub 源码包中为空目录） |
| 源码快照与子模块 tarball | `tools/build/*.tar.gz`、`tools/build/subdl/` | 给 Windows/WSL 队友打包用 |

## 四、重建命令（改完代码后）

```bash
cd tools/build/yosys-069-src
source ../../oss-cad-suite/environment

# 首次配置（已做过；改动 CMakeLists 或插桩文件后重跑一次即可）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBISON_EXECUTABLE=/Users/selina/Documents/ChatGPT/fpga比赛/tools/build-tools/bison/bin/bison \
  -DFLEX_EXECUTABLE=/Users/selina/Documents/ChatGPT/fpga比赛/tools/build-tools/flex/bin/flex \
  -DYOSYS_ABC_EXECUTABLE=/Users/selina/Documents/ChatGPT/fpga比赛/tools/oss-cad-suite/bin/yosys-abc \
  -DYOSYS_WITHOUT_READLINE=ON -DYOSYS_WITHOUT_TCL=ON -DYOSYS_WITHOUT_SLANG=ON \
  -DYOSYS_ENABLE_UNIT_TESTS=OFF

# 日常增量编译
tools/oss-cad-suite/bin/ninja -C build -j8
```

## 五、给 Windows/WSL 队友（提交物必须能在 Windows 运行）

WSL Ubuntu / Linux 侧命令基本一致：

```bash
sudo apt install build-essential cmake ninja-build bison flex
# 用 tools/build/ 下的源码快照 + 5 个子模块 tarball 还原源码树
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DYOSYS_ABC_EXECUTABLE=<套件路径>/bin/yosys-abc \
  -DYOSYS_WITHOUT_READLINE=ON -DYOSYS_WITHOUT_TCL=ON -DYOSYS_WITHOUT_SLANG=ON
ninja -C build -j$(nproc)
```

- Windows 原生 .exe 可选 MSYS2(mingw64) 或 MSVC 路线；WSL 版官方明说「可以，只要运行在 Windows 下」。
- 队友侧下载 OSS CAD Suite **Windows** 版 0.69，先用 4 个官方用例验证原版 abc9 是否复现 `vecInt.h:446` 崩溃（群里有人报过，官方未回应）。

## 六、下一步

1. `synth_intel.cc` 里加调用（coarse 段 `run("opt")` 附近）+ `pmux_opt.cc` 进构建；
2. 官方口径评测脚本重做（lcell+$not、每用例单独算再平均、单次运行计时、≤5% 效率测量）；
3. EQY 改为验证 abc 之后 `$lut` ilang；
4. 算法转向实验（test1 选择网络共享手工已有 −3.8% 证据）。
