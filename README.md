# LvglPkg

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![LVGL](https://img.shields.io/badge/LVGL-9.2.2-green.svg)](https://github.com/lvgl/lvgl)
[![EDK2](https://img.shields.io/badge/Platform-UEFI%20/%20EDK2-orange.svg)](https://github.com/tianocore/edk2)
[![Arch](https://img.shields.io/badge/Arch-X64-blueviolet.svg)](LvglPkg.dsc)

**LVGL v9 for UEFI** —— 一个把 [LVGL](https://github.com/lvgl/lvgl) 图形库搬进 UEFI 固件环境的 EDK2 软件包，自带完整的 UEFI 移植层（GOP 显示、键盘、鼠标、节拍时钟与内存管理）。

**[中文](README.md) | [English](#english)**

- [概述](#概述) · [功能特性](#功能特性) · [目录结构](#目录结构) · [获取源码](#获取源码) · [编译](#编译) · [接入你自己的包](#接入你自己的包) · [使用方法](#使用方法) · [移植层设计要点](#移植层设计要点) · [配置说明](#配置说明) · [环境要求与已知限制](#环境要求与已知限制) · [许可证](#许可证) · [English](#english)

---

## 概述

`LvglPkg` 是一个标准的 EDK2 软件包，它把 LVGL v9 的原版上游源码编译成 EDK2 的 `BASE` 库（`LvglLib`），再配上一个完整的 UEFI 适配层（`LvglUefiPort`），把 LVGL 的 HAL 接口——显示、输入、节拍、内存——全部落到 DXE 应用触手可及的标准 UEFI 协议上。整个移植不修改 edk2 源码树的任何文件，包本身是 `MdePkg` 的平级邻居，放进 `PACKAGES_PATH` 即可使用。

这套移植背后是几个在固件环境里性命攸关的工程决策：显示路径做到**零像素转换**（LVGL 原生 `XRGB8888` 与 `EFI_GRAPHICS_OUTPUT_BLT_PIXEL` 的内存字节序逐字节一致）；节拍源用**TSC 实现**，绕开固件里各种不可用的定时器驱动；内存钩子基于 **`AllocatePool`** 实现，并给不存在的 UEFI pool `realloc` 语义补上完整实现；主循环靠一个 **1ms 事件泵**驱动——纯轮询循环会让固件输入驱动的 `TPL_NOTIFY` 键队列永远空着，表现为界面收不到任何按键。这些细节下文逐一展开。

`LvglPkg` 是 [Guedit](https://github.com/)（一个 UEFI 图形文本编辑器，计划另行开源）的 GUI 底层。你也可以这样用它：在平台 DSC 里加两条库映射，调用 `LvglPortInit()` / `LvglPortPoll()` / `LvglPortDeinit()` 三步接口，然后用原生 LVGL API 搭你的界面。

## 功能特性

- **LVGL v9.2.2** 以 EDK2 `BASE` 库形式编译，源码是原版上游镜像（git 子模块方式引入），无 fork、无补丁文件。
- **GOP 显示驱动**：整屏后备缓冲 + `LV_DISPLAY_RENDER_MODE_DIRECT` 直渲模式，按脏区域调用 `Gop->Blt` 提交；像素格式原生 `XRGB8888`，**零转换**；`PixelBitMask` 模式（可变位宽，如 30bit HDR）直接拒绝。
- **键盘 indev**：优先 `SimpleTextInEx`（可通过 `LvglKbdGetModifiers()` 读取 Ctrl/Shift 状态），`SimpleTextIn` 兜底；自动合成 `PRESSED → RELEASED` 沿——UEFI 只上报按下、LVGL 只在上升沿派发按键，两者之间必须补一条释放事件。
- **鼠标 indev**：优先 `AbsolutePointer`（绝对坐标线性映射到屏幕像素），`SimplePointer` 兜底；对 ConSplitter 虚拟聚合实例做了加固（真实设备必带设备路径，虚拟句柄没有）；通过 `lv_indev_set_cursor()` 挂上可见光标。
- **TSC 节拍源**：用一次 100ms `gBS->Stall()` 标定频率；64 位单调计数不回绕，定时器驱动缺失或损坏时照常工作。
- **自定义内存钩子**：完整的 `lv_mem_*_core()` 函数族基于 `AllocatePool`/`FreePool`，块头 8 字节记录请求尺寸以模拟 `realloc`；每次初始化都跑 `lv_mem_test()` 运行时自检。
- **1ms 事件泵**（`WaitForEvent` 周期定时器）：派发固件输入驱动赖以填充键队列的 TPL 通知，同时充当主循环节拍。
- **定制 `lv_conf.h`**：32bpp、`LV_STDLIB_CUSTOM` malloc + 内置 string/sprintf（不碰 libc）、`LV_OS_NONE`、33ms 刷新周期（约 30fps）、深色主题、Montserrat 12/14/16 + `unscii_16` 等宽字体、关闭 demo 与日志。
- **工具链**：MSVC（`/utf-8`，只对上游代码触发的告警做压制）与 GCC 编译选项齐备；仅支持 X64。

## 目录结构

```
LvglPkg/
├── LvglPkg.dec                     # 包声明（公开头文件、库类）
├── LvglPkg.dsc                     # 包构建描述（编译两个库）
├── Include/Library/
│   ├── LvglLib.h                   # LVGL 唯一入口头文件 → include 镜像里的 lvgl.h
│   └── LvglUefiPort.h              # 移植层 API：Init / Poll / Deinit、tick、修饰键、光标
└── Library/
    ├── LvglLib/
    │   ├── LvglLib.inf             # 由 GenLvglSources.py 生成——切勿手改
    │   ├── lv_conf.h               # LVGL 配置，按 UEFI 环境裁剪（UTF-8，含中文注释）
    │   ├── lvgl/                   # git 子模块 → 上游 lvgl v9.2.2（原版镜像）
    │   └── Fonts/                  # simsun 中文字库（本包重生成版，补简体字形；镜像内原版不编译）
    └── LvglUefiPort/
        ├── LvglUefiPort.inf        # UEFI_DRIVER 类库；[Protocols] 声明保证消费方干净链接
        ├── LvglUefiPort.c          # 生命周期装配：Init → Poll 循环 → Deinit
        ├── DisplayGop.c            # GOP 显示，DIRECT 直渲，脏区域 Blt
        ├── InputKeyboard.c         # SimpleTextInEx/SimpleTextIn → LVGL keypad indev
        ├── InputMouse.c            # AbsolutePointer/SimplePointer → LVGL pointer indev
        ├── TickTimer.c             # TSC 节拍，Stall 标定一次频率
        └── MemAlloc.c              # lv_mem_*_core()：AllocatePool/FreePool 实现
```

## 获取源码

LVGL 镜像以 git 子模块形式指向上游 v9.2.2：

```bash
git clone <你的仓库地址>
cd LvglPkg
git submodule update --init
```

`LvglLib.inf`（源文件清单）由 `GenLvglSources.py` 扫描 `lvgl/src` 后重新生成。升级子模块到新版 LVGL 标签后，重跑该脚本再生成 INF 即可——镜像本身保持原版，不做任何修改。

## 编译

`LvglPkg` 只依赖原版 edk2 的 `MdePkg`。克隆 edk2 后，把本仓库放到与 `MdePkg` 平级的目录（或把其父目录加入 `PACKAGES_PATH`），然后：

```bash
# Windows / MSVC
edksetup.bat
build -p LvglPkg/LvglPkg.dsc -a X64 -t VS2022

# Linux / GCC
source edksetup.sh
build -p LvglPkg/LvglPkg.dsc -a X64 -t GCC5
```

包的 DSC 会把两个库都编译并链接通过（产物在 `Build/LvglPkg`）。需要说明的是，`LvglPkg.dsc` 是**库级**构建——它验证移植层能编译、能链接，要跑起来还需要一个消费它的应用（见下节）。`DEBUG`/`RELEASE`/`NOOPT` 三种目标均支持。

## 接入你自己的包

在平台 DSC 的 `[LibraryClasses]` 里加两条库映射，外加 `CompilerIntrinsicsLib`——MSVC 在无宿主环境下会把结构体拷贝和填充循环合成对 `memcpy`/`memset` 的调用：

```ini
[LibraryClasses]
  LvglLib|LvglPkg/Library/LvglLib/LvglLib.inf
  LvglUefiPort|LvglPkg/Library/LvglUefiPort/LvglUefiPort.inf
  CompilerIntrinsicsLib|MdePkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf
```

在应用 INF 里声明库类：

```ini
[LibraryClasses]
  LvglLib
  LvglUefiPort
```

注意 `LvglUefiPort` 是 `UEFI_DRIVER` 类库（它消费 `gBS` 和 DXE 阶段的库实例），所以要由 DXE 阶段的应用/驱动来链接，`BASE` 模块不行。

## 使用方法

移植层只暴露三步生命周期，`LvglPortInit()` → 循环 `LvglPortPoll()` → `LvglPortDeinit()`，没有别的花活：

```c
#include <Library/LvglLib.h>        /* 拿全部 LVGL API 只需这一个头文件   */
#include <Library/LvglUefiPort.h>   /* Init / Poll / Deinit / 修饰键      */

EFI_STATUS
GuiMain (VOID)
{
  EFI_STATUS  Status;

  Status = LvglPortInit ();         /* tick → lv_init → 内存自检 → GOP → 输入 */
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /* 用原生 LVGL v9 API 搭建界面。 */
  lv_obj_t *Screen = lv_screen_active ();
  lv_obj_set_style_bg_color (Screen, lv_color_hex (0x2B2B2B), 0);
  lv_obj_t *Btn    = lv_button_create (Screen);

  while (!QuitRequested) {
    LvglPortPoll ();                /* 约 1ms：泵固件事件 + 输入 + lv_timer_handler() */
  }

  return LvglPortDeinit ();
}
```

辅助接口：`LvglKbdGetModifiers()` 返回 Ctrl/Shift 状态（`LVGL_KBD_MOD_CTRL`/`LVGL_KBD_MOD_SHIFT` 位），界面据此识别 `Ctrl+S` 这类组合键；`LVGL_KEY_PAGE_UP`/`LVGL_KEY_PAGE_DOWN` 自定义键值让翻页键绕过 LVGL 组的焦点导航直接到达焦点控件；`LvglPortGetMouseIndev()` 暴露指针 indev（失败返回 `NULL`）。

移植层对外设缺失很宽容：键盘/鼠标初始化失败只告警，GUI 照常运行。`LvglPortPoll()` 必须在 `TPL_APPLICATION` 下调用。

## 移植层设计要点

值得说的工程细节大多是在真机取证时用血泪换来的：

- **零转换像素路径。** `EFI_GRAPHICS_OUTPUT_BLT_PIXEL` 的内存布局恒为 `B,G,R,X`；LVGL v9 的 32bit 格式只有 `ARGB8888`/`XRGB8888`，内存字节序同样是 `B,G,R,X`。后备缓冲按 `LV_COLOR_FORMAT_XRGB8888` 原生渲染后直接交给 `Gop->Blt`，逐字节吻合，1080p 下不做任何逐像素转换。
- **DIRECT 直渲模式。** 配合整屏后备缓冲，LVGL 只渲染脏区域，flush 回调也只按脏区域提交——文本编辑器每敲一个键只失效几行，渲染量和 Blt 流量都远小于 FULL 模式。`LV_DRAW_BUF_STRIDE_ALIGN` 保持默认值，flush 源行距恰为整屏行跨距 `屏宽 × 4`。
- **事件泵是承重墙。** 固件输入驱动（PS/2、USB 鼠标）靠 `TPL_NOTIFY` 定时器通知把按键填进队列，而通知只在 TPL 降级时派发——纯 `Stall()` 轮询循环永远不给固件这个机会，键队列恒空，表现为收不到任何按键。`LvglPortPoll()` 等一个 1ms 周期定时器事件，既派发了全部挂起通知，又当了循环节拍。
- **tick 回调必须在 `lv_init()` 之后挂接。** `lv_global_init()` 会对整个 LVGL 全局状态 memzero（含 tick 回调），提前挂接会被悄悄清零，tick 冻结在 0。节拍源选 TSC 是因为 DXE 应用可用的 `TimerLib` 实例各有硬伤：空模板直接 ASSERT、APIC 定时器固件从不启动、AcpiTimerLib 是 24bit PM 定时器约 4.7 秒回绕一次。
- **按下/抬起沿合成。** `ReadKeyStroke(Ex)` 只上报按下；LVGL 的 keypad indev 只在上升沿（`PRESSED` after `RELEASED`）派发键值。连续两个 PRESSED 中间没有 RELEASED，第二个键会被当成"正在按下"长按重复——快速打字必然丢键。驱动在消费下一个键前先补发一条同键值的 `RELEASED`。
- **`SimpleTextInEx` 才有修饰键。** 扩展协议的 `KeyShiftState` 是 UEFI 里读 Ctrl/Shift 的唯一途径；兜底路径按键可用但无修饰键信息（初始化时打 `DEBUG_WARN` 提示组合键不可用）。
- **真实绝对指针优先。** 绝对设备按 `CurrentX × 屏宽 / MaxX` 线性映射；`SimplePointer` 从屏幕中心累计相对位移，按 `max(1, Resolution / 16)` counts-per-pixel 缩放并保留亚像素余数，最终钳制到屏幕边界。协议选择顺序是“真实 Absolute → 真实 Simple → fallback Absolute → fallback Simple”，避免 ConSplitter 虚拟 Absolute 抢占真实相对设备（真实设备带设备路径，虚拟聚合句柄没有）。
- **给 pool 补 `realloc`。** UEFI 内存池没有 realloc 语义，每个块前多开 8 字节记录请求尺寸，`alloc → copy → free` 模拟之。初始化时自检覆盖 `NULL`-realloc、扩容、收缩三条路径，失败即按致命错误中止初始化。

## 配置说明

`lv_conf.h` 位于 `Library/LvglLib/`，经 `LV_CONF_INCLUDE_SIMPLE` 编译。与 UEFI 环境相关的关键项：

| 配置项 | 取值 | 缘由 |
|---|---|---|
| `LV_COLOR_DEPTH` | `32` | `XRGB8888`——与 `BLT_PIXEL` 逐字节一致，零转换 |
| `LV_USE_STDLIB_MALLOC` | `LV_STDLIB_CUSTOM` | 挂到 `LvglUefiPort` 的 `AllocatePool` 分配器上 |
| `LV_USE_STDLIB_STRING` / `SPRINTF` | `LV_STDLIB_BUILTIN` | 固件里没有 libc |
| `LV_USE_OS` | `LV_OS_NONE` | DXE 阶段单线程 |
| `LV_DEF_REFR_PERIOD` | `33` ms | 约 30fps 刷新 |
| `LV_THEME_DEFAULT_DARK` | `1` | 默认主题的浅色配色在深色界面上不可读 |
| `LV_FONT_*` | Montserrat 12/14/16 + `UNSCII_16` | `unscii_16` 是等宽位图字体，专供文本编辑区 |
| `LV_USE_LOG` / 各类 demo | `0` | 固件无日志出口；demo 编译时剔除 |

该文件刻意使用 UTF-8 并保留中文注释，`LvglLib.inf` 的编译选项带 `/utf-8`（及 `/wd4819`）供 MSVC 正确解码——请勿转码或加 BOM。

## 环境要求与已知限制

**环境要求：** EDK2（仅 `MdePkg`）、X64 架构、带 GOP 控制台的 DXE 阶段、MSVC（VS2019/2022）或 GCC 工具链。

**已知限制：**
- 仅 X64、单线程（`LV_OS_NONE`）、仅 DXE 阶段——移植层消费 `gBS`/`AllocatePool`。
- `PixelBitMask` GOP 模式（变位宽通道，如 30bit HDR）被拒绝——渲染契约固定为 `BGRX8888`。
- `SimpleTextIn` 兜底路径：基本按键可用，但无 Ctrl/Shift 状态（组合键不可用）。
- `SimplePointer` 兜底路径支持相对移动和左键；右键、滚轮及鼠标加速度尚未映射。
- 输入设备均为可选外设：初始化失败只告警，不致命。

## 许可证

`LvglPkg` 以 **MIT License** 发布——见 [LICENSE](LICENSE)。随包携带的 LVGL 镜像保留上游项目自身的 MIT 许可（子模块内原样保留）。

---

# English

> [中文](README.md) · **English**

- [Overview](#overview) · [Features](#features) · [Repository layout](#repository-layout) · [Getting the sources](#getting-the-sources) · [Build](#build) · [Integrate into your package](#integrate-into-your-package) · [Usage](#usage) · [Port design notes](#port-design-notes) · [Configuration](#configuration) · [Requirements & limitations](#requirements--limitations) · [License](#license)

## Overview

`LvglPkg` is a standard EDK2 package that makes LVGL v9 buildable and usable inside UEFI firmware. It compiles the pristine upstream LVGL sources into an EDK2 `BASE` library (`LvglLib`), and pairs it with `LvglUefiPort` — a library that adapts the LVGL HAL (display, input, tick, memory) to the standard UEFI protocols a DXE application can reach. Nothing in the edk2 tree is modified; the package is a drop-in sibling of `MdePkg`.

The port is built around a few deliberate engineering decisions that matter in firmware: a **zero pixel-conversion** display path (LVGL's native `XRGB8888` is byte-identical to `EFI_GRAPHICS_OUTPUT_BLT_PIXEL`), a **TSC-based tick** that survives the absence of a working timer driver, a **custom allocator over `AllocatePool`** with `realloc` semantics (UEFI pools have none), and a **1 ms event pump** that keeps firmware input drivers alive — pure polling loops starve their `TPL_NOTIFY` key queues. Each of these is documented in detail below.

`LvglPkg` is the GUI foundation of [Guedit](https://github.com/) (a UEFI text editor, to be open-sourced separately). You can use it the same way: add two library mappings to your platform DSC, call `LvglPortInit()` / `LvglPortPoll()` / `LvglPortDeinit()`, and build your UI with the stock LVGL API.

## Features

- **LVGL v9.2.2** built as an EDK2 `BASE` library from the pristine upstream sources (vendored as a git submodule — no forks, no patched files).
- **GOP display driver**: full-screen back buffer in `LV_DISPLAY_RENDER_MODE_DIRECT` mode, dirty-region `Gop->Blt` submission, native `XRGB8888` pixels with **zero conversion**; `PixelBitMask` modes rejected up front.
- **Keyboard indev**: `SimpleTextInEx` preferred (Ctrl/Shift modifier state exposed via `LvglKbdGetModifiers()`), `SimpleTextIn` fallback; synthesized `PRESSED → RELEASED` edges so LVGL dispatches every key (UEFI only reports presses, LVGL only dispatches on the rising edge).
- **Mouse indev**: `AbsolutePointer` preferred with linear absolute→pixel mapping, `SimplePointer` fallback; ConSplitter virtual-instance hardening (real devices carry a device path, the virtual aggregator does not); visible cursor via `lv_indev_set_cursor()`.
- **TSC-based tick**: frequency calibrated once with a 100 ms `gBS->Stall()`; 64-bit monotonic source, no wraparound, immune to missing/broken timer drivers.
- **Custom memory hooks**: full `lv_mem_*_core()` family over `AllocatePool`/`FreePool` with an 8-byte size header emulating `realloc`; runtime self-test (`lv_mem_test()`) runs on every init.
- **1 ms event pump** (`WaitForEvent` on a periodic timer): pumps the TPL notification queue that firmware input drivers depend on, and doubles as the main-loop heartbeat.
- **Tuned `lv_conf.h`**: 32 bpp, `LV_STDLIB_CUSTOM` malloc + built-in string/sprintf (no libc), `LV_OS_NONE`, 33 ms refresh (~30 fps), dark theme, Montserrat 12/14/16 + `unscii_16` monospace fonts, demos/logs disabled.
- **Toolchains**: MSVC (`/utf-8`, warnings silenced only where upstream code trips them) and GCC flags provided; X64 only.

## Repository layout

```
LvglPkg/
├── LvglPkg.dec                     # package declaration (public headers, library classes)
├── LvglPkg.dsc                     # package build description (builds the two libraries)
├── Include/Library/
│   ├── LvglLib.h                   # single entry header → includes the LVGL mirror's lvgl.h
│   └── LvglUefiPort.h              # port API: Init / Poll / Deinit, tick, modifiers, cursor
└── Library/
    ├── LvglLib/
    │   ├── LvglLib.inf             # generated by GenLvglSources.py — never edit by hand
    │   ├── lv_conf.h               # LVGL configuration, tuned for UEFI (UTF-8, CJK comments)
    │   ├── lvgl/                   # git submodule → upstream lvgl v9.2.2 (pristine mirror)
    │   └── Fonts/                  # SimSun CJK fonts (regenerated here with extra simplified glyphs; the mirror's originals are not compiled)
    └── LvglUefiPort/
        ├── LvglUefiPort.inf        # UEFI_DRIVER-class library; [Protocols] declared for clean linking
        ├── LvglUefiPort.c          # lifecycle assembly: Init → Poll loop → Deinit
        ├── DisplayGop.c            # GOP display, DIRECT render mode, dirty-region Blt
        ├── InputKeyboard.c         # SimpleTextInEx/SimpleTextIn → LVGL keypad indev
        ├── InputMouse.c            # AbsolutePointer/SimplePointer → LVGL pointer indev
        ├── TickTimer.c             # TSC tick, calibrated once via gBS->Stall()
        └── MemAlloc.c              # lv_mem_*_core() over AllocatePool/FreePool
```

## Getting the sources

The LVGL mirror is a git submodule pointing at upstream v9.2.2:

```bash
git clone <your-lvglpkg-repo-url>
cd LvglPkg
git submodule update --init
```

`LvglLib.inf` (the generated source list) is rewritten by `GenLvglSources.py`, which scans `lvgl/src`. When you bump the submodule to a newer LVGL tag, rerun the script and regenerate the INF — the mirror itself is never edited.

## Build

`LvglPkg` builds against a stock edk2 tree (`MdePkg` only). Clone edk2, place this repo as a sibling of `MdePkg` (or add its parent to `PACKAGES_PATH`), then:

```bash
# Windows / MSVC
edksetup.bat
build -p LvglPkg/LvglPkg.dsc -a X64 -t VS2022

# Linux / GCC
source edksetup.sh
build -p LvglPkg/LvglPkg.dsc -a X64 -t GCC5
```

The package DSC compiles and links both libraries (output under `Build/LvglPkg`). Note that `LvglPkg.dsc` is a *library* build — it validates that the port compiles and links; a runnable image needs a consuming application (see below). Both `DEBUG`/`RELEASE`/`NOOPT` targets are supported.

## Integrate into your package

Add two library mappings to your platform DSC's `[LibraryClasses]` — plus `CompilerIntrinsicsLib`, because MSVC synthesizes `memcpy`/`memset` calls in freestanding builds:

```ini
[LibraryClasses]
  LvglLib|LvglPkg/Library/LvglLib/LvglLib.inf
  LvglUefiPort|LvglPkg/Library/LvglUefiPort/LvglUefiPort.inf
  CompilerIntrinsicsLib|MdePkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf
```

Declare the library classes in your application's INF:

```ini
[LibraryClasses]
  LvglLib
  LvglUefiPort
```

`LvglUefiPort` is a `UEFI_DRIVER`-class library (it consumes `gBS` and the DXE-phase library instances), so consume it from DXE applications/drivers, not `BASE` modules.

## Usage

The port exposes a three-step lifecycle, `LvglPortInit()` → loop `LvglPortPoll()` → `LvglPortDeinit()`, and nothing else:

```c
#include <Library/LvglLib.h>        /* the only header you need for LVGL APIs   */
#include <Library/LvglUefiPort.h>   /* Init / Poll / Deinit / modifiers         */

EFI_STATUS
GuiMain (VOID)
{
  EFI_STATUS  Status;

  Status = LvglPortInit ();         /* tick → lv_init → mem self-test → GOP → input */
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /* Build your UI with the stock LVGL v9 API. */
  lv_obj_t *Screen = lv_screen_active ();
  lv_obj_set_style_bg_color (Screen, lv_color_hex (0x2B2B2B), 0);
  lv_obj_t *Btn    = lv_button_create (Screen);

  while (!QuitRequested) {
    LvglPortPoll ();                /* ~1 ms: pump firmware events + input + lv_timer_handler() */
  }

  return LvglPortDeinit ();
}
```

Additional helpers: `LvglKbdGetModifiers()` returns `Ctrl`/`Shift` state (`LVGL_KBD_MOD_CTRL`/`LVGL_KBD_MOD_SHIFT`) so your UI can recognize combos like `Ctrl+S`; `LVGL_KEY_PAGE_UP`/`LVGL_KEY_PAGE_DOWN` carry the paging keys past LVGL's group navigation; `LvglPortGetMouseIndev()` exposes the pointer indev (or `NULL`).

The port tolerates missing peripherals: keyboard/mouse init failures only warn, and the GUI still runs. `LvglPortPoll()` must run at `TPL_APPLICATION`.

## Port design notes

The interesting engineering is in the details, most of which took real debugging to get right:

- **Zero-conversion pixels.** `EFI_GRAPHICS_OUTPUT_BLT_PIXEL` is always `B,G,R,X` in memory; LVGL v9's only 32-bit formats are `ARGB8888`/`XRGB8888`, whose memory layout is also `B,G,R,X`. The back buffer is rendered natively in `LV_COLOR_FORMAT_XRGB8888` and handed to `Gop->Blt` byte-for-byte — no per-pixel conversion, which matters at 1080p.
- **DIRECT render mode.** With a full-screen back buffer, LVGL renders only dirty regions and the flush callback submits only dirty regions — ideal for text editors where each keystroke invalidates a few lines. `LV_DRAW_BUF_STRIDE_ALIGN` stays at its default so the flush source stride is exactly `pitch × 4`.
- **The event pump is load-bearing.** Firmware input drivers (PS/2, USB mouse) enqueue keystrokes via `TPL_NOTIFY` timer notifications, which are only dispatched when TPL is lowered — a pure `Stall()` polling loop never gives the firmware that chance and the key queue stays empty forever. `LvglPortPoll()` waits on a 1 ms periodic timer event, which both drains those notifications and paces the loop.
- **Tick callback must be registered *after* `lv_init()`.** `lv_global_init()` memzeros all LVGL global state including the tick callback; registering earlier gets silently wiped and the tick freezes at 0. A TSC-based source is used because every `TimerLib` instance available to DXE applications is broken in this scenario (null template, never-started APIC timer, or a 24-bit PM timer that wraps every ~4.7 s).
- **Press/release edge synthesis.** `ReadKeyStroke(Ex)` only reports presses; LVGL's keypad indev only dispatches a key on the *rising* edge (`PRESSED` after `RELEASED`). Two consecutive presses without a release would be treated as one long keypress — fast typing would silently drop keys. The driver synthesizes a matching `RELEASED` before consuming the next key.
- **`SimpleTextInEx` for modifiers.** The extended protocol's `KeyShiftState` is the only way to read Ctrl/Shift in UEFI; the fallback path works but reports no modifiers (a `DEBUG_WARN` tells you when).
- **Real AbsolutePointer first.** Absolute devices use a linear `CurrentX × width / MaxX` mapping; `SimplePointer` starts at screen center, accumulates relative motion using `max(1, Resolution / 16)` counts per pixel with subpixel remainders, and clamps the result to the display. Selection follows real Absolute → real Simple → fallback Absolute → fallback Simple, so a ConSplitter virtual Absolute handle cannot mask a real relative device (real devices carry a device path; virtual aggregators do not).
- **`realloc` over a pool.** UEFI pools have no `realloc`; an 8-byte size header in front of every block emulates it (`alloc → copy → free`). A self-test covering the `NULL`-realloc, grow and shrink paths runs at every init and aborts init if the allocator is broken.

## Configuration

`lv_conf.h` lives in `Library/LvglLib/` and is compiled via `LV_CONF_INCLUDE_SIMPLE`. The UEFI-relevant settings:

| Setting | Value | Why |
|---|---|---|
| `LV_COLOR_DEPTH` | `32` | `XRGB8888` — byte-identical to `BLT_PIXEL`, zero conversion |
| `LV_USE_STDLIB_MALLOC` | `LV_STDLIB_CUSTOM` | hooks into `LvglUefiPort`'s `AllocatePool`-based allocator |
| `LV_USE_STDLIB_STRING` / `SPRINTF` | `LV_STDLIB_BUILTIN` | no libc exists in firmware |
| `LV_USE_OS` | `LV_OS_NONE` | DXE is single-threaded |
| `LV_DEF_REFR_PERIOD` | `33` ms | ~30 fps refresh |
| `LV_THEME_DEFAULT_DARK` | `1` | default theme's light palette is unreadable on the dark UI |
| `LV_FONT_*` | Montserrat 12/14/16 + `UNSCII_16` | `unscii_16` is a monospace bitmap font for text editors |
| `LV_USE_LOG` / demos | `0` | firmware has no log sink; demos are compiled out |

The file carries UTF-8 CJK comments on purpose; the `LvglLib.inf` build options set `/utf-8` (and `/wd4819`) so MSVC decodes it correctly — do not transcode it or add a BOM.

## Requirements & limitations

**Requirements:** EDK2 (`MdePkg` only), X64 architecture, DXE phase with a GOP console, MSVC (VS2019/2022) or GCC toolchain.

**Known limitations:**
- X64 only, single-threaded (`LV_OS_NONE`), DXE phase only — the port consumes `gBS`/`AllocatePool`.
- `PixelBitMask` GOP modes (variable-width channels, e.g. 30-bit HDR) are rejected — the render contract is fixed `BGRX8888`.
- `SimpleTextIn` fallback: basic keys work, but no Ctrl/Shift state (combos unavailable).
- `SimplePointer` fallback supports relative motion and the left button; right-click, wheel, and pointer acceleration are not mapped yet.
- Input devices are optional: their init failures are warnings, not fatal errors.

## License

`LvglPkg` is released under the **MIT License** — see [LICENSE](LICENSE). The bundled LVGL mirror retains its own MIT license from the upstream project (kept intact in the submodule).
