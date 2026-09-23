# -*- coding: utf-8 -*-
"""把项目打包成跨平台可用的 ZIP。

为什么要用这个脚本而不是 tar 或资源管理器：
    Windows 上常见的打包工具会把中文文件名按 GBK 写进 ZIP 且不置 UTF-8 标志位，
    拿到 macOS 或 Linux 上打开就是乱码（本项目第一天就修过一次这种包）。
    Python 的 zipfile 会自动用 UTF-8 编码非 ASCII 文件名并置好标志位。

用法：
    python make_package.py                       # 打包当前目录到上一级
    python make_package.py --src H:\\fpga2026 --out D:\\pkg.zip
    python make_package.py --exclude tools --exclude _待删除
"""

import argparse
import os
import sys
import zipfile

DEFAULT_EXCLUDE = ["tools", "_待删除", "__pycache__", ".DS_Store"]


def collect(src, excludes):
    """返回 (绝对路径, 归档内相对路径) 列表，已按排除规则过滤。"""
    items = []
    for root, dirs, files in os.walk(src):
        rel_root = os.path.relpath(root, src).replace("\\", "/")
        dirs[:] = [d for d in dirs if d not in excludes]
        for name in files:
            if name in excludes:
                continue
            full = os.path.join(root, name)
            rel = name if rel_root == "." else rel_root + "/" + name
            items.append((full, rel))
    return items


def main():
    parser = argparse.ArgumentParser(description="跨平台安全打包（UTF-8 文件名）")
    parser.add_argument("--src", default=".", help="要打包的目录")
    parser.add_argument("--out", default=None, help="输出的 zip 路径")
    parser.add_argument("--exclude", action="append", default=None,
                        help="要排除的目录或文件名，可重复")
    parser.add_argument("--prefix", default=None,
                        help="归档内的顶层目录名，默认用 src 的目录名")
    args = parser.parse_args()

    src = os.path.abspath(args.src)
    if not os.path.isdir(src):
        raise SystemExit("not a directory: %s" % src)
    prefix = args.prefix or os.path.basename(src)
    excludes = set(args.exclude if args.exclude is not None else DEFAULT_EXCLUDE)
    out = args.out or os.path.join(os.path.dirname(src),
                                   "%s-package.zip" % os.path.basename(src))

    items = collect(src, excludes)
    total_bytes = sum(os.path.getsize(p) for p, _ in items)
    print("source : %s" % src)
    print("files  : %d (%.1f MB)" % (len(items), total_bytes / 1e6))
    print("exclude: %s" % ", ".join(sorted(excludes)))
    print("output : %s" % out)

    written = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for full, rel in items:
            info = zipfile.ZipInfo(prefix + "/" + rel, date_time=(1980, 1, 1, 0, 0, 0))
            info.flag_bits = 0x800          # 显式声明文件名是 UTF-8
            info.external_attr = 0o644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(full, "rb") as fh:
                zf.writestr(info, fh.read())
            written += 1

    size = os.path.getsize(out)
    print("done   : %d files, %.2f MB" % (written, size / 1e6))

    with zipfile.ZipFile(out) as zf:
        bad = zf.testzip()
        if bad:
            raise SystemExit("corrupt entry: %s" % bad)
        need = [i for i in zf.infolist() if any(ord(c) > 127 for c in i.filename)]
        miss = [i for i in need if not (i.flag_bits & 0x800)]
        print("utf8   : %d 个非 ASCII 文件名，缺标志位 %d 个" % (len(need), len(miss)))
        if miss:
            raise SystemExit("这些条目在 macOS 上会乱码：" + ", ".join(i.filename for i in miss[:5]))


if __name__ == "__main__":
    sys.exit(main())
