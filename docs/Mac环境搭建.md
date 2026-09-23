# Mac 环境搭建清单

目标：在这台 Mac 上复现 Windows 侧的基线数据，并具备后续开发 C++ 插件的能力。

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

---

## 第 1 步：装编译工具链

```bash
xcode-select --install
```

如果还没有 Homebrew：

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

然后装构建依赖：

```bash
brew install bison flex make
```

**注意**：macOS 自带的 bison 是 2.3 版，而 Yosys 要求 3.8 以上，
必须用 Homebrew 装的覆盖掉，否则源码编译会失败。这是 Mac 上最容易踩的坑。

---

## 第 2 步：装 OSS CAD Suite（先用起来）

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

应当报告 0.68 开头。然后复现基线：

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

macOS 上应当不会遇到 Windows 那个缺 make 的问题。

---

## 第 5 步（后续）：为写 C++ 插件做准备

前面几步用的都是预编译的二进制，够做摸底实验，但**不够写插件**，
插件必须在 Yosys 源码树里编译。届时需要：

```bash
git clone --recurse-submodules https://github.com/YosysHQ/yosys.git
cd yosys
git checkout v0.68
make config-gcc
make -j$(sysctl -n hw.ncpu)
```

编译完把 pass 放进 `passes/opt/` 再重新编译即可。

**先别急着做这一步**：等摸底实验跑完、确认要开工再编，
因为编译一次要几十分钟，而且比赛要求锁定版本，中途换版本要全部重来。
