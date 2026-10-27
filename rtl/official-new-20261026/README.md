# 官方新增 PMUX 用例（2026-10-26）

本目录保存组委会于 2026-10-26 提供的 5 个参数化 RTL 用例，原样来自桌面
`pmux_cases.zip`，保留原始 CRLF 换行，不修改源文件。

ZIP SHA-256：`03a2aaf3c3aeccb50f2633df949fd5d88e8c4bcc0090f81099e5cb21d231fe9e`

默认实例参数：

| 顶层 | LANES | WIDTH | 结构特征 |
|---|---:|---:|---|
| `pmux_case01_balanced` | 1 | 12 | 多路平衡组合选择、同步反馈 |
| `pmux_case02_pipeline` | 4 | 3 | 多 lane 两级寄存、跨 lane 组合反馈 |
| `pmux_case03_crossbar` | 2 | 5 | 普通选择、异或、加减与混合控制 |
| `pmux_case04_feedback` | 2 | 8 | 多级组合反馈、同步复位使能 |
| `pmux_case05_nested` | 1 | 12 | 过程式嵌套 `case`，最多三级 |

评测清单：`experiments/official-new-5-20261026/manifest.json`。
