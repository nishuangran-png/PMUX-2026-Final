#!/usr/bin/env bash
# 把下载好的 OSS CAD Suite 2026-09-22（Yosys 0.69+117）装到 tools/。
#
# 前置：scripts/download_suite.py 已把 8 个分片下到
#       tools/oss-cad-suite-darwin-arm64-20260922.tgz.parts/
#
# 做的事：
#   1. 校验分片总字节数（必须等于发布页公布的 521342093）
#   2. 合并分片 → tar 完整性检查（tar -tzf 全量解压流）
#   3. 现有 tools/oss-cad-suite → tools/oss-cad-suite-068（并存，便于对比）
#   4. 解出新的 tools/oss-cad-suite，跑 yosys -V 确认版本号
#
# 用法： bash scripts/install_069.sh
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/tools"

PARTS="oss-cad-suite-darwin-arm64-20260922.tgz.parts"
EXPECT=521342093

if [ ! -d "$PARTS" ]; then
  echo "找不到分片目录 $PARTS —— 下载还没开始？" >&2
  exit 1
fi

have=$(cat "$PARTS"/part_*.bin | wc -c | tr -d ' ')
echo "分片总字节: $have / 期望 $EXPECT"
if [ "$have" != "$EXPECT" ]; then
  echo "还没下完或分片损坏，先别装。缺 $((EXPECT - have)) 字节。" >&2
  exit 1
fi

echo "合并分片…"
cat "$PARTS"/part_*.bin > oss-cad-suite-darwin-arm64-20260922.tgz

echo "tar 完整性检查（全量解压流，约需十几秒）…"
if ! tar -tzf oss-cad-suite-darwin-arm64-20260922.tgz > /dev/null; then
  echo "tar 校验失败：归档损坏，需要重下。" >&2
  exit 1
fi
echo "tar 归档完好。"

if [ -d oss-cad-suite-069 ]; then
  echo "tools/oss-cad-suite-069 已存在，跳过解包（先手工处理）。"
elif [ -d oss-cad-suite ]; then
  echo "现有 tools/oss-cad-suite → tools/oss-cad-suite-068"
  mv oss-cad-suite oss-cad-suite-068
  tar -xzf oss-cad-suite-darwin-arm64-20260922.tgz
  echo "解出 → tools/oss-cad-suite-069"
  mv oss-cad-suite oss-cad-suite-069
else
  echo "没有现役 tools/oss-cad-suite，直接解包到 oss-cad-suite-069"
  tar -xzf oss-cad-suite-darwin-arm64-20260922.tgz
  mv oss-cad-suite oss-cad-suite-069
fi

echo "版本自检："
./oss-cad-suite-069/bin/yosys -V
./oss-cad-suite-069/bin/eqy --version 2>/dev/null || true

cat <<'TIP'

下一步（二选一）：
  A. 切到 0.69（推荐，团队在 Windows 上用的就是这版）：
       cd tools && mv oss-cad-suite-069 oss-cad-suite
     （此时 0.68 已并存为 tools/oss-cad-suite-068，随时可换回来）
  B. 暂时并行跑两版做对比：分别 source 两个 environment，再跑 scripts/measure.sh。
TIP
