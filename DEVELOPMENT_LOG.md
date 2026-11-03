# PMUX 开发日志（2026-09-22 — 2026-11-03）

本日志用于公开索引 PMUX 项目的真实开发过程。原始开发仓从
**2026-09-22** 启动，冻结前共有 **31 次非合并提交**：9 月 12 次，
10 月 19 次。下方每一条记录都来自原始开发仓，日期、提交 SHA 和说明
均未回填或改写。

原始开发仓：
[`gitee.com/andy-n/fpga2026fpga2026`](https://gitee.com/andy-n/fpga2026fpga2026)

## 公开 Git 图说明

最终复现仓于 2026-11-03 从冻结交付包建立，随后把原始开发仓的真实提交
对象接入 `main`。这样既能保留从 9 月开始的开发轨迹，也不会把旧日期伪装
成新生成的提交。`main` 的最终代码树仍是冻结交付版，历史接入不改变
源码、RTL、综合日志、性能数据或形式验证结论。

## 阶段总览

| 阶段 | 日期范围 | 提交数 | 主要工作 | 阶段出口 |
|---|---|---:|---|---|
| 01 基线与交接 | 2026-09-22 — 09-24 | 12 | 初始化仓库、Yosys 0.68 基线、EQY 演示、跨平台环境 | 建立可测、可交接的工程基线 |
| 02 PMUX v2 与 test4 | 2026-10-21 — 10-25 | 15 | PMUX v2、常量 ROM 路径、LUT lowering、同源复测与保护 | 打通 test4，统一 Yosys 0.69/ABC 口径 |
| 03 九题冻结 | 2026-10-27 — 10-28 | 2 | 冻结九题优化、发布最新优化核心 | 形成九题统一算法与证据链 |
| 04 合规与效率 | 2026-10-30 | 2 | 合规版本、运行效率优化 | 进入最终交付与复测阶段 |

## 完整提交索引

| 日期 | 提交 | 说明 |
|---|---|---|
| 2026-09-22 | [`cccdaf4`](https://gitee.com/andy-n/fpga2026fpga2026/commit/cccdaf4f7e87093ba0398e1cbd68f70031924c2d) | chore: 初始化仓库并导入 PMUX 选题资料 |
| 2026-09-23 | [`b9167c5`](https://gitee.com/andy-n/fpga2026fpga2026/commit/b9167c5f9ec55caaea6e0bef844bcadc0141f475) | chore: 收拢到单一主文件夹 H:\fpga2026 |
| 2026-09-23 | [`9284935`](https://gitee.com/andy-n/fpga2026fpga2026/commit/92849357fd76bbb6ee73471ae9141dc315248bcf) | chore: 统一换行规则，覆盖 .v 与 .html |
| 2026-09-23 | [`aefc1dd`](https://gitee.com/andy-n/fpga2026fpga2026/commit/aefc1dde6cb39ef3384b0a4a281b8799530deb80) | test: 添加 EQY 等价验证最小演示（正反两个用例） |
| 2026-09-23 | [`74df13d`](https://gitee.com/andy-n/fpga2026fpga2026/commit/74df13d44129a62e913b3e41800124d80da0b520) | docs: 添加向指导老师首次汇报的 Word 文档 |
| 2026-09-23 | [`e2bd745`](https://gitee.com/andy-n/fpga2026fpga2026/commit/e2bd745fd499308a2d4f3866442719a4b8b79ab3) | docs: 汇报文档增加团队分工一节，重写老师支持事项 |
| 2026-09-23 | [`2b44f2e`](https://gitee.com/andy-n/fpga2026fpga2026/commit/2b44f2e279252ed84868bb961bbbb8ece7737a05) | docs: 新增问答预演文档，修正汇报文档中一处失实表述 |
| 2026-09-23 | [`d277e51`](https://gitee.com/andy-n/fpga2026fpga2026/commit/d277e5122116ddd44f1c4f94d6e14dfa4a0b1133) | docs: 同步汇报文档删减，修正问答文档清单措辞 |
| 2026-09-23 | [`e8eb2ac`](https://gitee.com/andy-n/fpga2026fpga2026/commit/e8eb2ac29067b5f504566387ca045340a4089000) | docs: 恢复验收口径与测试用例来源两条支持事项 |
| 2026-09-23 | [`51a8d9e`](https://gitee.com/andy-n/fpga2026fpga2026/commit/51a8d9ebb27de6cd040fc1cf8b035d55a035e6dc) | chore: 切换到组委会要求的 Yosys 0.68，重测全部基线数据 |
| 2026-09-24 | [`9f933ef`](https://gitee.com/andy-n/fpga2026fpga2026/commit/9f933efc1679d8c242ca5321a1be57c864c029c8) | docs: 新增交接日志、对话纪要、Mac 环境清单与跨平台下载脚本 |
| 2026-09-24 | [`8fa1609`](https://gitee.com/andy-n/fpga2026fpga2026/commit/8fa160970aa2ea6fafa11f40f8a4cc9d9fa4fee8) | chore: 修正打包脚本的 UTF-8 校验并记录中文名编码问题 |
| 2026-10-21 | [`83ef741`](https://gitee.com/andy-n/fpga2026fpga2026/commit/83ef7414ef42e910d4c0e021930108f48da60e60) | release: 固化 PMUX v2 阶段成果 2026-10-20 |
| 2026-10-23 | [`466d6b8`](https://gitee.com/andy-n/fpga2026fpga2026/commit/466d6b823de2f349e48883d0a5b1fdd30a3b7f66) | checkpoint: 定位并打通 test4 常量 ROM 优化路径 |
| 2026-10-23 | [`af4bedf`](https://gitee.com/andy-n/fpga2026fpga2026/commit/af4bedfb72a36f841535870c130b0e60279f7ce0) | record: 归档 test4 阶段修复原始评测数据 |
| 2026-10-24 | [`5e7a250`](https://gitee.com/andy-n/fpga2026fpga2026/commit/5e7a250154b464b826ea91d9fa389487c04b20e6) | checkpoint: add constant ROM LUT lowering and LUT folding |
| 2026-10-24 | [`bcf1a78`](https://gitee.com/andy-n/fpga2026fpga2026/commit/bcf1a78c134d33d5d95356650100776e98932bb0) | test: record test4 LUT lowering and open-source reuse survey |
| 2026-10-24 | [`f09b164`](https://gitee.com/andy-n/fpga2026fpga2026/commit/f09b164bf112c92277bc3af1c8d8e69f426c49ea) | docs: add same-source handoff and sync verified results |
| 2026-10-24 | [`d5d7e46`](https://gitee.com/andy-n/fpga2026fpga2026/commit/d5d7e46fdcf2ee472f953cfe798ef8f68a69592c) | test: add platform-aware holdout evaluation |
| 2026-10-25 | [`8dd9c3b`](https://gitee.com/andy-n/fpga2026fpga2026/commit/8dd9c3b707fcaded7b98f1cdd62dab38987b4603) | fix: guard large ROM folding and harden regressions |
| 2026-10-25 | [`57d89a3`](https://gitee.com/andy-n/fpga2026fpga2026/commit/57d89a3cef849ff8de5d0a495115d4234df2c1b1) | test: reject unprofitable priority-tree and shift variants |
| 2026-10-25 | [`708be92`](https://gitee.com/andy-n/fpga2026fpga2026/commit/708be92fcc492c0d192840b8c3196b8f0005c2ec) | build: pin official yosys 0.69 and evaluation defaults |
| 2026-10-25 | [`cf4e9f6`](https://gitee.com/andy-n/fpga2026fpga2026/commit/cf4e9f69a4f17a0277eb9c70381a2b67c1e17ddc) | fix: guard shift decode and one-bit lut mappings |
| 2026-10-25 | [`aba1a34`](https://gitee.com/andy-n/fpga2026fpga2026/commit/aba1a34d0708cad7ba3a8e40c09356b2f116722d) | docs: publish official 069 final verification |
| 2026-10-25 | [`d044db9`](https://gitee.com/andy-n/fpga2026fpga2026/commit/d044db94c8f5ca54a07acd0879472640bc8ba941) | perf: bound split planning and guard wide ROMs |
| 2026-10-25 | [`037954c`](https://gitee.com/andy-n/fpga2026fpga2026/commit/037954cdea1c88f10d0e71b84f68ddc8b2237b02) | chore: pin evidence line endings for reproducible hashes |
| 2026-10-25 | [`275f486`](https://gitee.com/andy-n/fpga2026fpga2026/commit/275f486714852fc1cfae356d6769a2c18e53618b) | perf: guard canonical priority chains and add upstream holdout |
| 2026-10-27 | [`10de648`](https://gitee.com/andy-n/fpga2026fpga2026/commit/10de648581078b6ed4914cce83690ada0c799fe4) | release: freeze nine-case PMUX optimization |
| 2026-10-28 | [`63de9f5`](https://gitee.com/andy-n/fpga2026fpga2026/commit/63de9f58b217b2cd925aebdf1369349a068222ad) | feat: publish latest PMUX optimization core |
| 2026-10-30 | [`99d25cc`](https://gitee.com/andy-n/fpga2026fpga2026/commit/99d25ccf59007035ce5c36c5e1fcbf5b865346fb) | 合规版本 |
| 2026-10-30 | [`556cf0c`](https://gitee.com/andy-n/fpga2026fpga2026/commit/556cf0c66db592388dd3e4ec73bec25ac7f5046a) | 优化运行效率；实现 PMUX 识别、结构拆解、MUX 树重构、共享译码、稀疏更新合并、常量 ROM 降低及 LUT/DFF 后处理 |

## 时间口径核验

```bash
git rev-list --all --count
# 31

git log --all --reverse --date=format:'%Y-%m' --pretty=format:'%ad' \
  | sort | uniq -c
# 12 2026-09
# 19 2026-10
```

2026-11-02 冻结源码、二进制、日志和哈希；2026-11-03 发布最终复现仓和
`v1.0.0-final`。9—10 月是算法研发期，11 月是交付整理期。
