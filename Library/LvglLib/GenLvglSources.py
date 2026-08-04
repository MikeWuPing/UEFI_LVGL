#!/usr/bin/env python3
"""扫描 lvgl/src 下全部 .c 与 Fonts/ 目录，生成 LvglLib.inf。

镜像（lvgl/ 目录）为官方 PRISTINE 源码，一律不手工在 INF 里列文件；
更换 LVGL 版本后重跑本脚本即可重新生成 [Sources]。

字体文件特殊处理：lvgl/src/font/ 下的 lv_font_simsun_*.c 是上游镜像自带的
原版字体，本项目在 Fonts/ 目录维护重生成版本（补简体字形，见
Fonts/lv_font_simsun_16_cjk.c 头部注释），编译只取 Fonts/ 版本——
脚本跳过镜像内的 simsun 字体，避免重复符号。
注意：src 下的 .cpp（ThorVG 等）不在收集范围——当前 lv_conf.h
未启用任何 C++ 组件，LV_USE_THORVG_INTERNAL=0。
"""
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "lvgl", "src")
FONTS = os.path.join(ROOT, "Fonts")

# LvglLib.inf 的 FILE_GUID，首次生成后固定，重跑脚本保持不变
FILE_GUID = "1533DFC6-4530-4585-BAF7-7B50B82AD567"

sources = []
for dirpath, _dirs, files in os.walk(SRC):
    _dirs.sort()  # 遍历顺序不依赖文件系统枚举顺序，保证 [Sources] 可重现
    for f in sorted(files):
        if not f.endswith(".c"):
            continue
        if f.startswith("lv_font_simsun"):
            continue   # 镜像内原版字体不编译，用 Fonts/ 重生成版
        rel = os.path.relpath(os.path.join(dirpath, f), ROOT).replace(os.sep, "/")
        sources.append(rel)
for f in sorted(os.listdir(FONTS)):
    if f.endswith(".c"):
        sources.append("Fonts/" + f)

if not sources:
    sys.exit("error: 未找到 lvgl/src 下的 .c")

tmpl = """## @file
# LVGL v9 graphics library (built from the pristine upstream source mirror).
#
# GENERATED FILE - every part of it, including the [Sources] list, is
# rewritten by GenLvglSources.py. Never edit this file by hand; make any
# change in GenLvglSources.py and rerun it to regenerate.
# NOTE: keep this file pure ASCII - BaseTools reads meta files with the
# OS locale encoding (cp936 here), non-ASCII bytes can break the parser.

[Defines]
  INF_VERSION                    = 0x00010005
  BASE_NAME                      = LvglLib
  FILE_GUID                      = %s
  MODULE_TYPE                    = BASE
  VERSION_STRING                 = 1.0
  LIBRARY_CLASS                  = LvglLib

[BuildOptions]
  # LV_CONF_INCLUDE_SIMPLE: lv_conf_internal.h locates the config via
  # #include "lv_conf.h"; this directory is on the module include path
  # automatically, so lv_conf.h lives next to this INF.
  # /utf-8 is MANDATORY, not cosmetic: lv_conf.h carries UTF-8 CJK
  # comments, and decoding them as cp936 (the ANSI default) mis-parses
  # "*/" after a 3-byte character, so the comment swallows the
  # LV_USE_STDLIB_MALLOC=LV_STDLIB_CUSTOM define. Without this flag the
  # builtin malloc backend stays active and collides with the port's
  # custom allocator at final link time (LNK2005).
  # The mirror is pristine upstream code: do not fix its warnings, just
  # turn off warnings-as-errors (/WX-) and silence the noisy ones (/wd*).
  MSFT:*_*_*_CC_FLAGS = /DLV_CONF_INCLUDE_SIMPLE /utf-8 /wd4189 /wd4244 /wd4267 /wd4100 /wd4204 /wd4819 /wd4018 /wd4221 /wd4245 /wd4389 /WX-
  GCC:*_*_*_CC_FLAGS = -DLV_CONF_INCLUDE_SIMPLE -Wno-unused -Wno-format

[Sources]
%s

[Packages]
  MdePkg/MdePkg.dec
  LvglPkg/LvglPkg.dec

# MSVC optimizes struct copies and fill loops into calls to the libc
# symbols memcpy/memset even in freestanding EFI builds. LVGL itself never
# calls them (LV_USE_STDLIB_STRING=LV_STDLIB_BUILTIN), so the definitions
# must come from CompilerIntrinsicsLib; every DSC that links LvglLib needs
# the matching instance mapping.
[LibraryClasses]
  CompilerIntrinsicsLib
""" % (FILE_GUID, "\n".join("  " + s for s in sources))

# Keep generated metadata LF-normalized so regeneration is stable across hosts.
with open(os.path.join(ROOT, "LvglLib.inf"), "w", newline="\n") as fp:
    fp.write(tmpl)
print("LvglLib.inf: %d sources" % len(sources))
