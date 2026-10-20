# -*- coding: utf-8 -*-
"""按日期下载 OSS CAD Suite（跨平台）。

用途：项目需要固定的 Yosys 版本时（例如组委会要求 0.68），
换成对应日期的构建即可。OSS CAD Suite 每天构建，内含当天主干的 Yosys。

版本与日期的对应关系（官方 tag 日期）：
    v0.68  2026-08-05   →  用 2026-08-06 的构建，实测报告 0.68+27
    v0.69  2026-09-09   →  用 2026-09-22 的构建，实测报告 0.69+117

    ⚠️ 0.69 请固定用 2026-09-22 这一版：团队此前在 Windows 上测的 0.69 数据
       就是这一版，换别的日期会得到 0.69+不同提交数，数字不可比。

用法：
    python3 download_suite.py --date 2026-09-22 --platform darwin-arm64 --dest ./tools
    python3 download_suite.py --date 2026-09-22 --platform windows-x64 --dest H:\\tools

    # 国内网络强烈建议走镜像，实测从 44 KB/s 提升到 4.5 MB/s
    python3 download_suite.py --date 2026-09-22 --platform darwin-arm64 \
        --dest ./tools --mirror https://gh-proxy.com

说明：
    - 多线程分块下载，大陆网络环境下实测比单连接快七倍以上。
    - --mirror 会把 GitHub 地址前拼上镜像前缀；不传则直连 GitHub。
    - 默认不走代理；需要时用 --proxy 指定，例如 http://127.0.0.1:7890
    - 支持断点续传：分片文件存在时会从已有长度继续。
    - 下载完成后校验字节数，不一致会报错退出。
"""

import argparse
import concurrent.futures
import os
import sys
import time
import socket
import urllib.request

BASE = "https://github.com/YosysHQ/oss-cad-suite-build/releases/download"


def build_url(day, platform):
    stamp = day.replace("-", "")
    name = "oss-cad-suite-%s-%s.tgz" % (platform, stamp)
    return "%s/%s/%s" % (BASE, day, name), name


def make_opener(proxy):
    handlers = []
    if proxy:
        handlers.append(urllib.request.ProxyHandler({"http": proxy, "https": proxy}))
    return urllib.request.build_opener(*handlers)


def remote_size(url, opener):
    req = urllib.request.Request(url, method="HEAD",
                                 headers={"User-Agent": "curl/8.9.0"})
    with opener.open(req, timeout=60) as resp:
        return int(resp.headers["Content-Length"])


def fetch_part(url, path, start, end, opener):
    """下载 [start, end] 这一段。

    ⚠️ 必须能自愈：国内镜像会把长连接「放死」——连接还在、但不再吐数据，
    实测 8 个分片跑十几分钟后集体掉到 0 字节/秒。
    所以这里每次只读到超时为止，出错就从断点重连，最多重试 max_tries 次。
    """
    max_tries = 200
    stall_timeout = 30

    for attempt in range(max_tries):
        have = os.path.getsize(path) if os.path.exists(path) else 0
        if have >= end - start + 1:
            return
        head = {"Range": "bytes=%d-%d" % (start + have, end),
                "Accept-Encoding": "identity",
                "User-Agent": "curl/8.9.0"}
        req = urllib.request.Request(url, headers=head)
        mode = "ab" if have else "wb"
        try:
            with opener.open(req, timeout=stall_timeout) as resp, open(path, mode) as fh:
                # 已经下过一部分时，服务端必须回 206；回 200 说明它忽略了 Range，
                # 这时候追加会写坏数据，直接重来。
                if have > 0 and getattr(resp, "status", 206) != 206:
                    os.remove(path)
                    continue
                while True:
                    try:
                        block = resp.read(262144)
                    except (TimeoutError, socket.timeout, OSError):
                        break          # 卡住就断开，外层重连续传
                    if not block:
                        break
                    fh.write(block)
        except Exception:
            time.sleep(2)
            continue
        # 一轮结束：要么下完，要么被截断，交给下一轮判断
        if os.path.getsize(path) >= end - start + 1:
            return

    raise SystemExit("分片下载失败（%s），已重试 %d 次" % (path, max_tries))


def download(url, dest, total, workers, opener, label):
    chunk = -(-total // workers)
    part_dir = dest + ".parts"
    os.makedirs(part_dir, exist_ok=True)
    paths = [os.path.join(part_dir, "part_%02d.bin" % i) for i in range(workers)]

    def part_path(i):
        return paths[i]

    def fetched():
        return sum(os.path.getsize(p) for p in paths if os.path.exists(p))

    started = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = []
        for i in range(workers):
            start = i * chunk
            end = min(start + chunk - 1, total - 1)
            futures.append(pool.submit(fetch_part, url, part_path(i), start, end, opener))
        while any(not f.done() for f in futures):
            time.sleep(10)
            got = fetched()
            speed = got / 1024 / max(time.time() - started, 1e-9)
            print("  %s  %5.1f%%  %7.1f MB  %6.0f KB/s" %
                  (label, got / total * 100, got / 1e6, speed), flush=True)
        for f in futures:
            f.result()

    with open(dest, "wb") as out:
        for path in paths:
            with open(path, "rb") as fh:
                while True:
                    block = fh.read(1 << 22)
                    if not block:
                        break
                    out.write(block)

    size = os.path.getsize(dest)
    if size != total:
        raise SystemExit("size mismatch: got %d, expected %d" % (size, total))
    for path in paths:
        print("  分片保留在 %s，确认无误后可自行删除" % path)


def main():
    parser = argparse.ArgumentParser(description="下载指定日期的 OSS CAD Suite")
    parser.add_argument("--date", required=True, help="发布日期，例如 2026-08-06")
    parser.add_argument("--platform", default="darwin-arm64",
                        choices=["windows-x64", "linux-x64", "linux-arm64",
                                 "darwin-arm64", "darwin-x64"])
    parser.add_argument("--dest", default="./tools", help="保存目录")
    parser.add_argument("--workers", type=int, default=8, help="并发分片数")
    parser.add_argument("--proxy", default="",
                        help="本地代理，例如 http://127.0.0.1:7890；不用代理则留空")
    parser.add_argument("--mirror", default="",
                        help="GitHub 镜像前缀，例如 https://gh-proxy.com；直连则留空")
    args = parser.parse_args()

    url, name = build_url(args.date, args.platform)
    if args.mirror:
        url = args.mirror.rstrip("/") + "/" + url
        print("mirror: %s" % args.mirror)
    opener = make_opener(args.proxy)
    os.makedirs(args.dest, exist_ok=True)
    dest = os.path.join(args.dest, name)

    total = remote_size(url, opener)
    print("asset : %s" % name)
    print("bytes : %d (%.1f MB)" % (total, total / 1e6))
    print("dest  : %s" % dest)
    download(url, dest, total, args.workers, opener, name)
    print("done  : %s" % dest)
    print("解压  : tar -xzf %s -C %s" % (dest, args.dest))


if __name__ == "__main__":
    sys.exit(main())
