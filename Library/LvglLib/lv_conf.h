/**
 * LVGL v9.2.2 配置文件（UEFI 环境）
 *
 * 编码说明：本文件刻意使用 UTF-8（含中文注释）；编译 LVGL 的翻译单元
 * 依赖 LvglLib.inf BuildOptions 中的 /wd4819 压制 MSVC 代码页告警，
 * 请勿转码或加入 BOM。
 *
 * 本文件必须与 lvgl/lv_conf_template.h 同版本核对后维护：
 * 未在此处定义的宏将取 lv_conf_internal.h 中的模板默认值。
 *
 * 位置约定：编译时定义 LV_CONF_INCLUDE_SIMPLE，lv_conf_internal.h 会以
 * #include "lv_conf.h" 方式查找本文件；LvglLib.inf 所在目录（即本目录）
 * 自动在 edk2 模块 include path 中，无需额外路径配置。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/

/*GOP  framebuffer 使用 32bit XRGB8888*/
#define LV_COLOR_DEPTH 32

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/

/* UEFI 固件环境无 libc：
 * - malloc 用 CUSTOM，核心函数 lv_malloc_core/lv_realloc_core/lv_free_core
 *   由 LvglUefiPort 基于 AllocatePool/FreePool 实现（见 Task 5）；
 * - string/sprintf 用 LVGL 内置实现，避免引入 libc 依赖。
 * 枚举值定义见 lv_conf_internal.h：LV_STDLIB_BUILTIN/CLIB/.../CUSTOM。*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CUSTOM
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

/*====================
   HAL SETTINGS
 *====================*/

/*默认刷新周期 33ms，约 30fps*/
#define LV_DEF_REFR_PERIOD  33      /*[ms]*/

/*=================
 * OPERATING SYSTEM
 *=================*/

/*DXE 阶段单线程，不用 OS 抽象层*/
#define LV_USE_OS   LV_OS_NONE

/*========================
 * LOG / DEMO
 *========================*/

/*固件内不接 LVGL 日志（如调试需要可打开并注册 print cb）*/
#define LV_USE_LOG 0

/*不编译官方 examples 与 demos。
 *注：GenLvglSources.py 只扫描 lvgl/src，examples/ 与 demos/ 本就不在
 *编译列表中，这里显式置 0 作为声明。*/
#define LV_BUILD_EXAMPLES 0

#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

/*==================
 *   THEME
 *==================*/

/*M1 起界面底色为深色(#2B2B2B)，默认主题必须用 Dark 变体，
 *否则文字/控件沿用 Light 配色的深灰(#212121)，深底上不可读。*/
#define LV_THEME_DEFAULT_DARK 1

/*==================
 *   FONT USAGE
 *==================*/

/*内置 Montserrat 12/14/16，默认 14*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1

/*编辑区等宽位图字体：实测 16px 等宽（unscii-8.ttf 按 16px 渲染，
 *adv_w=16px、line_height=17、字形盒 <=16x16），覆盖 U+0020-U+007F；
 *Montserrat 是比例字体，不可用于编辑区。菜单/状态栏仍用 Montserrat。*/
#define LV_FONT_UNSCII_16 1

/*simsun 简体中文字库（Fonts/ 目录，本包重生成版，见 Fonts/lv_font_simsun_16_cjk.c
 *头部注释）：ASCII + 常用汉字 + FontAwesome 图标子集。lv_conf_internal.h 默认
 *关闭（0），此处启用——lv_conf.h 先于 internal 默认值被包含，不会被覆盖。*/
#define LV_FONT_SIMSUN_14_CJK 1
#define LV_FONT_SIMSUN_16_CJK 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif /*LV_CONF_H*/
