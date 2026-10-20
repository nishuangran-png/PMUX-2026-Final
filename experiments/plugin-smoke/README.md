# 最小插件冒烟测试

目的：证明「**在这台 Mac 上能编译 Yosys 插件并加载运行**」。

这一步此前是项目里最大的未知：本机没有 Homebrew、bison 只有 2.3（编不动 Yosys 源码）、
也没有 Yosys 源码树。如果插件编不出来，整个选题无从下手。

## 结论

**不需要 Homebrew、不需要 bison、不需要重新编译 Yosys。**
OSS CAD Suite 里自带了 `yosys-config` 和插件开发所需的全部头文件，
用 Xcode 命令行工具自带的 `clang++` 就能直接编译出 `.so` 并加载。

## 用法

```bash
cd <仓库根目录>
source tools/oss-cad-suite/environment
bash experiments/plugin-smoke/build.sh
```

预期输出里能看到：

```
6. Executing PMUX_HELLO pass.
  module pmux_basic: 132 cells (0 x $_MUX_, 0 x $pmux)
```

## ⚠️ 踩坑：`yosys-config --build` 用不了

OSS CAD Suite 的 `yosys-config` 把编译器路径**写死**成打包机的
`/usr/local/osxcross/target/bin/aarch64-apple-darwin25.5-clang++`，本机不存在，
直接调用会报 `No such file or directory`。

绕开办法（`build.sh` 里就是这么写的）：

```bash
clang++ $(yosys-config --cxxflags) $(yosys-config --ldflags) -o foo.so -shared foo.cc
yosys -m foo.so -p "...; foo; ..."
```

`--ldflags` 里的 `-undefined dynamic_lookup` 是 macOS 上加载插件的关键，
少了它链接会失败；`--cxxflags` 会带上 `-std=c++20` 和 `share/yosys/include`。

## 两个 pass

`build.sh` 会把两个插件都编出来：

| 文件 | pass 名 | 用途 |
| --- | --- | --- |
| `pmux_hello.cc` | `pmux_hello` | 最小骨架：遍历模块、数单元。纯验链路 |
| `api_probe.cc` | `pmux_api_test` | 用真实 API：`SigMap` / `CellTypes` / 驱动关系分析 |

`api_probe` 是写 `pmux_opt` 时真正该抄的模板——它已经做了这两件后面必须做的事：

1. 建 `SigMap`，再扫一遍所有单元的输出端口，得到 **「每个信号位由哪个单元驱动」的索引表**
   （这是识别「`$eq` 常量比较 → `$pmux`」模式的基础）
2. 按 `A / B / S / Y` **四个端口**正确读出 `$pmux`，并统计每个选择位来自哪种单元

实测输出（`pmux_basic`）：

```
$procmux$18: A=8 bits, S=8 位选择, B=64 bits（每片 8 位，符合 |B| = |S| x W）
   选择信号来源: $logic_not × 1
   选择信号来源: $eq × 7
```

即：8 路选择 = **7 个 `$eq` 常量比较 + 1 个 `$logic_not`**，数据每片 8 位。
这就是本选题要优化掉的原始结构。
