# 提交包、仓库与 Release 对应关系

本页定义 `v1.0.0-final` 的唯一权威交付物，避免把运行包、审阅仓库和
Release 附件误认为三个不同版本。

## 唯一权威运行包

- 文件名：`PMUX-2026-Final-v1.0.0-final-repro.zip`
- 包内根目录：`PMUX_final_repro/`
- 文件大小：`67,465,315` 字节
- SHA-256：`74e622dfacf4fafdc1c371a24ef5b2c89ee372a7028066d7ce067a95eccf960c`
- 来源：以最终 `PMUX_final_repro(4)` 为冻结标准；仅优化包内 README，
  并重新生成 `MANIFEST.sha256`。候选源码、RTL、二进制、综合日志、性能
  采样、EQY 结果和测量值均未修改。
- 完整性：ZIP 结构校验通过；包内 `MANIFEST.sha256` 共 771 项，771/771
  全部匹配。

`FINAL_VERSION.json` 中的内部包名仍为 `PMUX_final_repro.zip`。这只是冻结
包内部的版本标识；对外分发的文件名改为上述名称，不改变包内容或成绩。

## GitHub 仓库的定位

GitHub 仓库是**源码与证据审阅副本**，不是第二个可执行运行包：

- `baseline/`、`candidate/` 中的大体积 Windows 可执行文件和运行库不进入
  Git 对象，由 Release 完整提供。
- 日志中的 Windows 本机目录统一替换为 `%LOCAL_HOME%`；只改变路径文本，
  不改变面积、性能、缩减率或等价性结论。
- 仓库额外包含公开开发日志、时间线和发布说明，便于审阅开发过程。
- 9 个用例、最终源码覆盖层、补丁、结果表和 EQY 结论与运行包同源。

因此仓库目录与 ZIP 不应逐字节比较；仓库与 ZIP 的差异仅限“可执行文件由
Release 提供”和“日志路径脱敏”。若需要核对最终运行产物，以本页 ZIP
SHA-256 为准。

## GitHub Release

- 发布页：`https://github.com/nishuangran-png/PMUX-2026-Final/releases/tag/v1.0.0-final`
- 唯一附件：`PMUX-2026-Final-v1.0.0-final-repro.zip`
- 附件必须与上述权威运行包字节一致，大小和 SHA-256 相同。

Release 不另行构建、重新打包或发布第二个候选版本。组委会提交包与 Release
附件应是同一个文件。

## 审计顺序

1. 核对 ZIP 的 SHA-256 和内部 `MANIFEST.sha256`。
2. 核对 Release 附件名称、大小和 SHA-256。
3. 在本仓库查看源码、补丁、综合日志、性能采样和 EQY 结论。
4. 以 `FINAL_VERSION.json` 冻结的二进制与 ABC 哈希为准，不以历史文档中的
   中间版本数据为准。
