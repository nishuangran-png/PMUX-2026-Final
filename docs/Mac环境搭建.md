# Mac 环境搭建清单

目标：在这台 Mac 上复现 Windows 侧的基线数据，并具备开发 C++ 插件的能力。

> **当前状态：已全部装好并验证通过。**
> 随时用 `bash scripts/doctor.sh` 一条命令体检（20 项检查），全绿即具备开工条件。

---

## 第 0 步：先确认机器规格

不同芯片要下不同的包，别下错。

| 项目 | 需要确认 | 影响 |
| --- | --- | --- |
| 芯片 | Apple Silicon（M 系列）还是 Intel | 决定下 darwin-arm64 还是 darwin-x64 |
| 系统版本 | macOS 12 或以上 | 低了新版工具链跑不起来 |
| 空闲磁盘 | 建议 10 GB 以上 | 源码编译的构建产物很占地方 |
| 内存 | 8 GB 起步，16 GB 更稳 | 跑千级节点测试时吃得比较狠 |

查看方式：左上角苹果菜单，关于本机。

本机实测：Apple Silicon（arm64）· macOS 14.8.8 · 8 核 · 8 GB。

---

## 第 1 步：装编译工具链

只需要 Xcode 命令行工具（自带 `clang++`）：

```bash
xcode-select --install
```

**这就够了。** 本机实测 `Apple clang 16.0.0` 可以直接编译 Yosys 插件。

### 关于 Homebrew / bison / flex（本机未装，也不需要）

网上多数 Mac 版 Yosys 教程会让你装 Homebrew 再 `brew install bison flex make`。
**那个流程只在「从源码编译 Yosys 本体」时才需要，本项目用不到**——详见第 5 步。

本机没有 Homebrew，且标准安装需要管理员密码（本机 `sudo` 需要密码，无法非交互安装）。
**这不是问题，不影响任何开发工作。**

---

## 第 2 步：装 OSS CAD Suite

项目里带了下载脚本，支持按日期取任意版本：

```bash
cd <项目目录>
python3 scripts/download_suite.py \
    --date 2026-08-06 \
    --platform darwin-arm64 \
    --dest ./tools \
    --proxy ""
```

- `--platform`：Apple Silicon 用 `darwin-arm64`，Intel 用 `darwin-x64`
- `--proxy ""`：不需要代理就传空字符串，需要的话传 `http://127.0.0.1:7890` 之类

⚠️ 在国内网络下直连 GitHub 只有 44 KB/s，516 MB 要下三小时且中途掉速。
用 GitHub 镜像（实测 `gh-proxy.com` 单连接 4.5 MB/s）快得多。

下载完成后解压：

```bash
cd tools
tar -xzf oss-cad-suite-darwin-arm64-20260806.tgz
```

加载环境（macOS 用 shell 脚本，不是 `.ps1`）：

```bash
source tools/oss-cad-suite/environment
```

---

## 第 3 步：验证环境（关键一步）

```bash
yosys -V
```

应当报告 YOSYS_VERSION 里锁定的版本。然后复现基线：

```bash
cd <项目目录>
yosys -p "read_verilog rtl/single/pmux_basic.v; synth -top pmux_basic; stat"
```

输出末尾应当是 **132 cells**。再试对照：

```bash
yosys -p "read_verilog rtl/single/mux_tree_8to1.v; synth -top mux_tree_8to1; stat"
```

应当是 **56 cells**。

**这两个数字对不上就说明版本不对**，别急着往下做。

批量看全部 6 个用例、7 种流程的对照表：

```bash
bash scripts/measure.sh
```

---

## 第 4 步：验证 EQY

```bash
cd experiments/eqy-demo
eqy eq_ok.eqy      # 预期通过
eqy eq_bad.eqy     # 预期被找出反例
```

如果报编码错误，`.eqy` 文件里的中文注释是元凶，设一下即可：

```bash
export PYTHONUTF8=1
```

macOS 上不会遇到 Windows 那个缺 make 的问题。

⚠️ **等价验证一律走 EQY，不要手搓 `miter -equiv` + `sat`。**
实测：对一对**已知等价**的电路，手搓 miter 会报 FAIL 并给出假反例
（该反例已用 iverilog 仿真和 `yosys eval` 双重证伪），而 EQY 正确判 PASS。
详见 `reports/004-开工摸底与开发链路验证.md` 第四节。

---

## 第 5 步：写 C++ 插件的环境（已就绪）

**结论：写插件不需要 Yosys 源码树，也不需要 Homebrew 和 bison。**

OSS CAD Suite 里自带了 `yosys-config` 和插件开发所需的**全部头文件**
（`share/yosys/include/kernel/` 下有 `yosys.h`、`register.h`、`sigtools.h`、
`celltypes.h`、`satgen.h`、`cost.h` 等）。用本机 `clang++` 直接编译 `.so`，
再用 `yosys -m` 加载即可。

```bash
cd <项目目录>
source tools/oss-cad-suite/environment
bash experiments/plugin-smoke/build.sh
```

⚠️ **坑：`yosys-config --build` 用不了。** 它把编译器路径写死成打包机的
`/usr/local/osxcross/target/bin/aarch64-apple-darwin25.5-clang++`，本机没有这个路径，
直接调用会报 `No such file or directory`。必须手动组装参数：

```bash
clang++ $(yosys-config --cxxflags) $(yosys-config --ldflags) -o foo.so -shared foo.cc
yosys -m foo.so -p "...; foo; ..."
```

`--ldflags` 里的 `-undefined dynamic_lookup` 是 macOS 上加载插件的关键。

### 参考源码树（只读）

`tools/yosys-src/` 是 Yosys 的完整源码（26 MB，当前是 v0.68，**仅供阅读**，
用来查上游 pass 怎么写的、`$pmux` 这类 cell 的定义是什么。
已经在 `tests/` 里解压好，不需要自己再 clone。

### 可选：从源码编译 Yosys 本体

**本项目不需要。** 只有当你想把 pass 编进 Yosys 本体（而不是做成插件）时才需要，
届时才涉及 Homebrew + bison ≥ 3.8（macOS 自带 2.3，版本不够）：

```bash
cd tools/yosys-src
make config-gcc
make -j$(sysctl -n hw.ncpu)
```

编译一次几十分钟，而且项目要求锁定单一版本（见 `YOSYS_VERSION`），中途换版本要全部重来。
**先用插件路线，不要一上来就编本体。**

---

## 附录：eLinx 为什么装不了

官方指南写「本赛题全部通过 eLinx 软件进行编译及仿真，无需硬件平台」，
并给了网盘链接（提取码 `b4tn`）。但这台 Mac 上装不了：

- 官网 `www.ehiway.cn` 的资料下载**需要登录/提交试用申请**才能拿到，无法脚本化下载
- eLinx 是 Windows 平台的 FPGA IDE，没有 macOS 版本

**处理方式**：eLinx 相关验证放到 Windows 机器上做；Mac 这台负责 Yosys 插件开发与
形式等价验证。另外，指南正文写「开发环境：Yosys + EQY」、末尾又写「全部通过 eLinx」，
这两处本身就矛盾——**已列入要问组委会的四个问题之一**（见 `PMUX选题资料/00-先读我.md`）。
