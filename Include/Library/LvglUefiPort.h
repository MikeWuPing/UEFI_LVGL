/** @file
  LVGL UEFI 适配层接口。
  调用顺序：LvglPortInit() -> 循环 LvglPortPoll() -> LvglPortDeinit()。
**/
#ifndef LVGL_UEFI_PORT_H_
#define LVGL_UEFI_PORT_H_

#include <Uefi.h>

/// 初始化毫秒 tick 源（用 gBS->Stall 标定一次 TSC 频率，约耗时 100ms）。
/// 由 LvglPortInit() 在 lv_init() 与 lv_tick_set_cb() 之前调用。
VOID
LvglTickInit (
  VOID
  );

/// 返回自 LvglTickInit() 起经过的毫秒数。
/// 签名兼容 LVGL 的 lv_tick_get_cb_t（uint32_t (*)(void)），
/// 由 LvglPortInit() 通过 lv_tick_set_cb() 挂接给 LVGL。
UINT32
LvglTickGetMs (
  VOID
  );

/// 初始化：定位 GOP、创建 LVGL display、注册键盘/鼠标 indev、挂 tick。
EFI_STATUS
LvglPortInit (
  VOID
  );

/// 主循环节拍：泵输入事件并执行 lv_timer_handler()。非阻塞。
VOID
LvglPortPoll (
  VOID
  );

/// 释放 display/indev/缓冲区。
EFI_STATUS
LvglPortDeinit (
  VOID
  );

/// Modifier bits returned by LvglKbdGetModifiers().
#define LVGL_KBD_MOD_CTRL   0x1u  ///< Either Ctrl key is currently held
#define LVGL_KBD_MOD_SHIFT  0x2u  ///< Either Shift key is currently held

/// Custom key values: the LVGL group permanently intercepts LV_KEY_PREV/NEXT
/// for focus navigation, so PgUp/PgDn would never reach the focused widget.
/// The port maps SCAN_PAGE_UP/DOWN to these custom values instead; the group
/// does not intercept them and the focused widget handles paging itself.
#define LVGL_KEY_PAGE_UP    0x10000001U
#define LVGL_KEY_PAGE_DOWN  0x10000002U
/// Task 7: F 键无原生 LV_KEY_* 常量，SCAN_F2 映射为本自定义值，由 Ui 层
/// ScrKeyCb 识别为"重命名"（Task 4 起预留的 F2=重命名入口）。
/// Task 9 键盘全表：F1=关于 / F5=刷新 同模式（端口 SCAN_F1/SCAN_F5 →
/// 本自定义值，group 不拦截，直达屏幕 ScrKeyCb）。取值自 0x10000004 顺延
/// 于 LVGL_KEY_F2=0x10000003（Task 9 任务书误写 F1/F5 为 0x10000003/4，
/// 与 F2 冲突，按序顺延——自定义值仅要求组内唯一）。
#define LVGL_KEY_F2         0x10000003U
#define LVGL_KEY_F1         0x10000004U
#define LVGL_KEY_F5         0x10000005U
/// Task 10: Tab 从 LV_KEY_NEXT 改为自定义值——区域切换语义（列表↔树↔
/// 工具栏）改由 Ui 层 ScrKeyCb 分发，LVGL group 不再拦截 Tab 做组内
/// 焦点导航。取值 0x10000006 顺延于 F5（自定义值仅要求组内唯一）。
/// Task 24 (gsetupmod): Shift+Tab 映射为自定义 LVGL_KEY_TAB_PREV——
/// 与 LV_KEY_NEXT 同为 '\t' 上游拦截键的镜像问题（LVGL 无原生
/// Shift+Tab），端口在 '\t' 分支按 KeyShiftState 的 Shift 修饰位区分
/// 后映射两个自定义值，Ui 层行焦点循环据此前进/回退。取值 0x10000010
/// 顺延（自定义值仅要求组内唯一）。upstream 待推。
#define LVGL_KEY_TAB        0x10000006U
#define LVGL_KEY_TAB_PREV   0x10000010U

/// Current keyboard modifier state as LVGL_KBD_MOD_* bits, refreshed on
/// every key event read via SimpleTextInEx. The Ui layer uses this to
/// recognize combos such as Ctrl+S. Always 0 when the keyboard fell back
/// to plain SimpleTextIn (that protocol reports no modifier state).
UINT32
LvglKbdGetModifiers (
  VOID
  );

/// Mouse indev handle (lv_indev_t *) created by MouseInit, exposed as
/// VOID * so this header stays free of LVGL types. NULL when the mouse
/// failed to initialize. LvglPortInit uses it to attach the visible
/// pointer cursor via lv_indev_set_cursor().
VOID *
LvglPortGetMouseIndev (
  VOID
  );

#endif // LVGL_UEFI_PORT_H_
