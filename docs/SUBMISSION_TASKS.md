# 提交任务

## 1. 源码仓库

1. 将 `source/final/overlay/` 覆盖到官方 Yosys 提交
   `d0e71cfb7bcafe2b437f3edc1789b69e99ecd55a`。
2. 也可以先解压仓库内的 `source/baseline_source_tree.tar.gz`，再覆盖
   `source/final/overlay/`。
3. 审核补丁位于 `source/patches/changes.patch`。
4. 不要把 ZIP 当作源码仓库内容提交。

最终源码还应保留：

- `source/final/overlay/passes/pmux/pmux_opt.cc`
- `source/final/overlay/techlibs/intel/synth_intel.cc`
- `source/final/overlay/passes/techmap/abc9_exe.cc`
- `source/final/overlay/kernel/io.cc`
- `source/final/overlay/kernel/yosys.cc`
- `source/final/overlay/techlibs/intel/common/ff_map_pn1.v`
- `source/final/overlay/techlibs/intel/cycloneiv/pmux_abc9.abc`

## 2. 二进制

Windows 可执行文件不进入 Git 历史，作为 GitHub Release
`v1.0.0-final` 的附件上传。运行包中的 `baseline/` 和 `candidate/`
目录应包含：

- `yosys.exe`
- `yosys-abc.exe`
- `share/`
- 运行所需 DLL

候选可以从同目录查找 ABC；基线必须通过包内脚本运行，因为其内置
回退路径是构建机路径，脚本会设置 `ABC`。

## 3. 测试材料

提交：

- `cases/` 和 `cases/INDEX.csv`
- `scripts/`
- `source/final/dffeas_formal.v`
- `logs/eqy/netlists/`
- `logs/eqy/result.json` 和 `logs/eqy/eqy-all.log`
- `logs/synthesis/` 的 18 份原始日志及 `stats.csv`
- `logs/performance/` 的原始采样和汇总
- `docs/` 和 Word 报告

## 4. 声明规则

- 官方四题：EQY 原生分区 PASS。
- 新增五题：EQY 分区流程因分区 `.sv` 相对路径错误返回 ERROR，
  随后整设计等价 PASS，必须标记 `PASS*`。
- 性能：引用队友独立审计的 10 轮结果 `1.023 / 1.031`，并注明最终
  成绩以官方环境为准。
- 旧七轮高频内存轮询结果只作为历史证据，不用于最终效率结论。

## 5. 提交前运行

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\smoke_runtime.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run_synthesis.ps1 `
  -PythonExe <python.exe>
powershell -ExecutionPolicy Bypass -File .\scripts\run_performance.ps1 `
  -PythonExe <python.exe> -Repeats 10
powershell -ExecutionPolicy Bypass -File .\scripts\run_eqy.ps1 `
  -PythonExe <python.exe> -EqyExe <eqy.exe>
```

最后校验 `hashes/cases.sha256`、`hashes/windows_binaries.sha256` 和
仓库根目录的 `SHA256SUMS`。任何源码或二进制变化都必须重新生成面积、
性能、EQY、哈希和最终运行包。
