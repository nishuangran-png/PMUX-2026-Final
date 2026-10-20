#!/usr/bin/env bash
# 反复续传 OSS CAD Suite 2026-09-22（Yosys 0.69），直到字节数达标。
#
# 为什么要这样：国内镜像对长连接会限速/放死（实测从 300+ KB/s 掉到几 KB/s），
# 但**新连接**一开始都很快。所以策略是「卡住就重启」——脚本本身支持断点续传
# （按已下字节数发 Range 请求），重启只损失几秒。
#
# 用法：bash scripts/download_069_resume.sh
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

EXPECT=521342093
PARTS="tools/oss-cad-suite-darwin-arm64-20260922.tgz.parts"
MIRROR="${MIRROR:-https://gh-proxy.com}"

size() { stat -f%z $PARTS/part_*.bin 2>/dev/null | awk '{s+=$1} END {print (s==""?0:s)}'; }

for round in $(seq 1 60); do
  have=$(size)
  if [ "$have" = "$EXPECT" ]; then echo "✓ 分片齐了：$have 字节"; exit 0; fi
  printf '[第 %s 轮] 已有 %.1f / 521.3 MB\n' "$round" "$(python3 -c "print($have/1e6)")"

  python3 scripts/download_suite.py --date 2026-09-22 --platform darwin-arm64 \
    --dest ./tools --workers 8 --mirror "$MIRROR" >/dev/null 2>&1 &
  DL=$!

  last=$have; stall=0
  while kill -0 "$DL" 2>/dev/null; do
    sleep 15
    now=$(size)
    [ "$now" = "$last" ] && stall=$((stall+1)) || stall=0
    last=$now
    [ "$stall" -ge 2 ] && { echo "  停滞 30 秒，重启续传（已下 $now）"; kill "$DL" 2>/dev/null; sleep 2; kill -9 "$DL" 2>/dev/null; break; }
  done
  wait "$DL" 2>/dev/null
  [ "$(size)" = "$EXPECT" ] && { echo "✓ 下载完成"; exit 0; }
done
echo "六十轮仍未完成，需要人工看" >&2; exit 1
