/** @file
  LvglUefiPort 主接口：把 tick、GOP 显示、键盘/鼠标输入装配成
  LvglPortInit / LvglPortPoll / LvglPortDeinit 三步调用模型。

  初始化顺序：先标定 tick（LvglTickInit 只读 TSC + Stall，不触 LVGL
  状态），再 lv_init()，随后才挂接 tick 回调——lv_init 的
  lv_global_init() 对整个全局状态 memzero（含 tick_get_cb），回调若
  在 lv_init 之前挂接会被清零，lv_tick_get() 退回永不递增的
  sys_time。之后运行一次 lv_mem_test() 作为自定义内存钩子的运行时
  自测，最后创建 GOP display 与 1ms 事件泵。键盘/鼠标属外设，初始
  化失败仅告警不致命。
**/

#include <Library/LvglLib.h>
#include <Library/LvglUefiPort.h>

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

//
// 模块内其它编译单元提供的内部接口（DisplayGop.c / InputKeyboard.c /
// InputMouse.c）。LvglTickInit/LvglTickGetMs 已随 <Library/LvglUefiPort.h>
// 导出，此处不再重复声明。
//
EFI_STATUS  GopDisplayInit (VOID);
VOID        GopDisplayDeinit (VOID);
VOID        GopDisplayGetResolution (UINT32 *Hor, UINT32 *Ver);   /* 鼠标绝对坐标映射用 */
EFI_STATUS  KeyboardInit (VOID);      /* InputKeyboard.c：SimpleTextInEx 优先 */
EFI_STATUS  MouseInit (VOID);         /* InputMouse.c：AbsolutePointer 优先 */
VOID        KeyboardPoll (VOID);      /* 空实现：读由 indev 读定时器驱动 */
VOID        MousePoll (VOID);         /* 空实现：同上 */
VOID        KeyboardDeinit (VOID);
VOID        MouseDeinit (VOID);

/// 防重复初始化：第二次 LvglPortInit 直接返回 EFI_ALREADY_STARTED。
static BOOLEAN  mInitialized = FALSE;

///
/// 事件泵与节拍源：固件的输入驱动（Ps2KeyboardDxe、USB 鼠标等）靠
/// TPL_NOTIFY 定时器事件通知把按键填入队列；通知只在 TPL 经
/// CoreRestoreTPL 降过通知级别时派发。纯轮询型主循环（LvglPortPoll +
/// Stall）从不给固件这个机会，键队列恒空——表现为应用收不到任何按键
/// （M1 取证时 ESC 失效即此因）。WaitForEvent 本身会 Raise/RestoreTPL，
/// 每次调用顺带派发全部挂起通知；等一个 1ms 周期定时器事件既派发事件
/// 又充当主循环节拍（替代调用方的 gBS->Stall）。
///
static EFI_EVENT  mEventPump = NULL;

/// 事件泵定时器周期（100ns 单位）：1ms。
#define EVENT_PUMP_PERIOD_100NS  10000

/**
  为鼠标 indev 挂可见光标（MouseInit 成功后调用；失败则无光标，不致命）。

  LVGL 指针 indev 默认没有任何可视光标，M2/M3 下鼠标只能盲点。这里创建
  一个 10x14 白底黑边小矩形交给 lv_indev_set_cursor()，此后 LVGL 在每次
  读到指针坐标时自动把它移到当前位置（lv_indev.c 读定时器里的
  lv_obj_set_pos(cursor, point.x, point.y)），端口层无需再参与。

  样式要点：remove_style_all 清掉主题默认样式后必须显式把 bg_opa 设为
  LV_OPA_COVER——LV_STYLE_BG_OPA 的属性默认值是 TRANSP，只设 bg_color
  会得到一个全透明的"隐形光标"。lv_indev_set_cursor() 自身会把光标对象
  重挂到 display 的 sys layer、去掉 CLICKABLE、加上 FLOATING|
  IGNORE_LAYOUT（不受父对象滚动与布局影响、不参与命中测试），故这里
  只负责尺寸与配色。
**/
static
VOID
MouseCursorCreate (
  VOID
  )
{
  lv_indev_t  *Indev;
  lv_obj_t    *Cursor;

  Indev = (lv_indev_t *)LvglPortGetMouseIndev ();
  if (Indev == NULL) {
    return;
  }

  Cursor = lv_obj_create (lv_screen_active ());
  if (Cursor == NULL) {
    DEBUG ((DEBUG_WARN, "[LvglPort] mouse cursor obj create failed\n"));
    return;
  }

  lv_obj_remove_style_all (Cursor);
  lv_obj_set_size (Cursor, 10, 14);
  lv_obj_set_style_bg_color (Cursor, lv_color_white (), 0);
  lv_obj_set_style_bg_opa (Cursor, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (Cursor, 1, 0);
  lv_obj_set_style_border_color (Cursor, lv_color_black (), 0);
  lv_indev_set_cursor (Indev, Cursor);
}

/**
  初始化 LVGL 与全部适配层子系统。
  @retval EFI_SUCCESS  成功（键盘/鼠标缺失不影响）
  @retval 其他         tick 无关，仅 GOP/display 致命失败时返回错误码
**/
EFI_STATUS
LvglPortInit (
  VOID
  )
{
  EFI_STATUS  Status;
  lv_result_t MemTest;

  if (mInitialized) {
    return EFI_ALREADY_STARTED;
  }

  //
  // tick 回调必须在 lv_init() 之后挂接：lv_init() 的 lv_global_init()
  // 对整个 LVGL 全局状态 memzero（含 tick_state.tick_get_cb），在
  // lv_init 之前 set_cb 会被清零——lv_tick_get() 退回永不递增的
  // sys_time，tick 冻结在 0，indev 读定时器永不触发（M1 取证时 ESC
  // 失效、画面只经 lv_timer_ready 初始渲染一帧即此根因）。定时器只在
  // lv_timer_handler() 里运行，lv_init 后挂接对一切读取都及时。
  //
  LvglTickInit ();
  lv_init ();
  lv_tick_set_cb (LvglTickGetMs);

  //
  // 自定义内存钩子（MemAlloc.c）的运行时自测：覆盖 malloc/realloc 扩容/
  // 收缩三条路径并回读校验。失败说明钩子有实现缺陷，后续一切分配都不可信，
  // 必须按致命错误处理。
  //
  MemTest = lv_mem_test ();
  if (MemTest != LV_RESULT_OK) {
    DEBUG ((DEBUG_ERROR, "[LvglPort] lv_mem_test failed: %d\n", (INT32)MemTest));
    lv_deinit ();
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((DEBUG_INFO, "[LvglPort] lv_mem_test OK\n"));

  Status = GopDisplayInit ();
  if (EFI_ERROR (Status)) {
    lv_deinit ();
    return Status;
  }

  //
  // 输入设备为可选外设：失败仅告警，不影响 GUI 启动。
  //
  Status = KeyboardInit ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "[LvglPort] keyboard init skipped: %r\n", Status));
  }

  Status = MouseInit ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "[LvglPort] mouse init skipped: %r\n", Status));
  } else {
    MouseCursorCreate ();
  }

  mInitialized = TRUE;

  //
  // 事件泵：1ms 周期定时器事件，LvglPortPoll 每次 WaitForEvent 它。
  // 失败不致命——退回无泵轮询（输入失效但显示仍可用），仅告警。
  //
  Status = gBS->CreateEvent (EVT_TIMER, TPL_APPLICATION, NULL, NULL, &mEventPump);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "[LvglPort] event pump create failed: %r\n", Status));
    mEventPump = NULL;
  } else {
    Status = gBS->SetTimer (mEventPump, TimerPeriodic, EVENT_PUMP_PERIOD_100NS);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "[LvglPort] event pump arm failed: %r\n", Status));
      gBS->CloseEvent (mEventPump);
      mEventPump = NULL;
    }
  }

  return EFI_SUCCESS;
}

/**
  主循环节拍：泵固件事件（输入驱动的 TPL_NOTIFY 队列填充依赖它，同时
  充当 ~1ms 的循环节拍），泵输入事件到 LVGL，然后执行 LVGL 定时器
  处理（含重绘）。非忙等：有事件泵时每次至多阻塞一个泵周期；泵缺失
  时退化为 1ms Stall 轮询（输入失效但显示/定时器仍正常）。

  调用前提：TPL_APPLICATION——WaitForEvent 只允许在 TPL_APPLICATION
  调用（DxeCore 在更高 TPL 下直接返回 EFI_UNSUPPORTED）。
**/
VOID
LvglPortPoll (
  VOID
  )
{
  if (mEventPump != NULL) {
    UINTN  Index;

    gBS->WaitForEvent (1, &mEventPump, &Index);
  } else {
    gBS->Stall (1000);  /* 泵缺失时的退化节拍，避免零延迟空转 */
  }

  KeyboardPoll ();
  MousePoll ();
  lv_timer_handler ();
}

/**
  反初始化：与初始化相反的顺序——先摘输入设备（indev 挂在 display 上，
  其读定时器由 lv_timer_handler 驱动），再释放 display（其析构走 LVGL
  堆），最后 lv_deinit()。
**/
EFI_STATUS
LvglPortDeinit (
  VOID
  )
{
  if (mEventPump != NULL) {
    gBS->SetTimer (mEventPump, TimerCancel, 0);
    gBS->CloseEvent (mEventPump);
    mEventPump = NULL;
  }

  KeyboardDeinit ();
  MouseDeinit ();
  GopDisplayDeinit ();
  lv_deinit ();
  mInitialized = FALSE;
  return EFI_SUCCESS;
}
