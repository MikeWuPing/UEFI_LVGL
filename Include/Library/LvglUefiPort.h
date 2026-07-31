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
