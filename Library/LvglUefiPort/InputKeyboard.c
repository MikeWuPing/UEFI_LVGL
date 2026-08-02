/** @file
  键盘输入驱动：SimpleTextInEx（首选）/ SimpleTextIn（兜底）→ LVGL
  LV_INDEV_TYPE_KEYPAD indev。

  协议选择
  --------
  优先经 gBS->HandleProtocol(gST->ConsoleInHandle, SimpleTextInEx) 拿扩展
  协议——它的 ReadKeyStrokeEx 额外返回 EFI_KEY_STATE.KeyShiftState，从中
  可解析 Ctrl/Shift 修饰键状态（Ui 层靠 LvglKbdGetModifiers() 识别
  Ctrl+S 这类组合键）。拿不到时退回 gST->ConIn 的 ReadKeyStroke：基本
  按键可用，但没有任何修饰键信息，LvglKbdGetModifiers() 恒为 0，
  初始化时打 DEBUG_WARN 提示组合键不可用。

  PRESSED/RELEASED 上报策略
  -------------------------
  UEFI 的 ReadKeyStroke(Ex) 只上报"按下"事件，没有"抬起"事件；而 LVGL
  的按键派发只在 PRESSED-after-RELEASED 的沿上把键值发给焦点对象（见
  lv_indev.c indev_keypad_proc："Button press happened" 分支要求
  last_state == RELEASED）。若连续上报两个不同键的 PRESSED 而中间没有
  RELEASED，第二个键会落入 "Pressing" 分支被当作长按重复处理，不会派
  发——快速打字必然丢键。因此本驱动每上报一次 PRESSED，就置
  mReleasePending，在下一次读回调里先补一次同键值的 RELEASED，再消费
  下一个键，保证每个键都形成完整的按下沿。补发的 RELEASED 与继续排空
  队列都靠 continue_reading=TRUE 让 LVGL 在同一读周期内再次回调完成，
  不引入额外延迟。
**/

#include <Library/LvglLib.h>
#include <Library/LvglUefiPort.h>

#include <Uefi.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>

static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *mTxtEx;        ///< 首选：带修饰键状态
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL     *mTxtIn;        ///< 兜底：无修饰键
static lv_indev_t                         *mIndev;
static lv_group_t                         *mGroup;        ///< 键盘焦点组（默认组）
static UINT32                             mModifiers;     ///< LVGL_KBD_MOD_* 位组合
static UINT32                             mLastKey;       ///< 上次上报 PRESSED 的 LVGL 键值
static BOOLEAN                            mReleasePending;///< 欠 LVGL 一次 mLastKey 的 RELEASED

/**
  返回当前修饰键状态（LVGL_KBD_MOD_CTRL / LVGL_KBD_MOD_SHIFT 位组合）。
  每次成功读到键事件时按 KeyShiftState 刷新；SimpleTextIn 兜底路径下
  恒为 0。
**/
UINT32
LvglKbdGetModifiers (
  VOID
  )
{
  return mModifiers;
}

/**
  从 EFI_KEY_STATE.KeyShiftState 刷新 mModifiers。
  KeyShiftState 仅在 EFI_SHIFT_STATE_VALID 高位有效时才可信（MdePkg
  SimpleTextInEx.h："valid only if the high order bit has been set"），
  无效时保持旧值不动。
**/
static
VOID
UpdateModifiers (
  IN UINT32  KeyShiftState
  )
{
  if ((KeyShiftState & EFI_SHIFT_STATE_VALID) == 0) {
    return;
  }

  mModifiers = 0;
  if ((KeyShiftState & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)) != 0) {
    mModifiers |= LVGL_KBD_MOD_CTRL;
  }

  if ((KeyShiftState & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) != 0) {
    mModifiers |= LVGL_KBD_MOD_SHIFT;
  }
}

/**
  EFI_INPUT_KEY → LVGL 键值映射。
  扫描码优先（方向/HOME/END/PAGE/DELETE/ESC 等功能键 UnicodeChar 为 0），
  其次控制字符 \r、\b，然后 0x20..0x7E 可打印 ASCII 原样透传（Shift 的
  大小写效果已体现在 UnicodeChar 里），最后 Ctrl+字母 控制码还原。
  F 键与 Tab 的映射：Tab 以 UnicodeChar=0x09 上报（UEFI 无 SCAN_TAB）
  → 自定义 LVGL_KEY_TAB（Task 10 区域切换语义，见下）；SCAN_F2/F1/F5
  → 自定义 LVGL_KEY_F1/F2/F5（无原生 LV_KEY_* 常量，自定义值 group
  不拦截，由屏幕 ScrKeyCb 分发为重命名/关于/刷新）。INS、F3/F4/F6-F12
  仍不映射。
  PgUp/PgDn 不映射为 LV_KEY_PREV/NEXT：LVGL group 会把这两个值永久拦截
  做焦点导航（lv_group_send_data），焦点控件永远收不到；映射为自定义
  值 LVGL_KEY_PAGE_UP/DOWN 后 group 不拦截，直达焦点控件自行翻页（M4）。

  Ctrl 组合键：OVMF 的 PS/2 键盘把 Ctrl+字母 上报为 ASCII 控制码
  （Ctrl+C=0x03、Ctrl+V=0x16、Ctrl+A=0x01、Ctrl+N=0x0E，ScanCode 为
  SCAN_NULL）。直接透传会撞上 LVGL 自定义键值（LV_KEY_HOME=2、
  LV_KEY_END=3），被 LVGL 当导航键拦截；丢弃则应用收不到 Ctrl 快捷键。
  这里在 KeyShiftState 的 Ctrl 修饰位有效时把控制码 +0x40 还原为字母
  （0x03→'C'…），走正常按键路径，让上层靠 LvglKbdGetModifiers() 区分。
  \r（回车）、\b（退格）、\t 保持各自映射，不做还原。
  @param  Key            固件按键（ScanCode + UnicodeChar）
  @param  KeyShiftState  EFI_KEY_STATE.KeyShiftState（SimpleTextIn 兜底
                         路径下为 0，Ctrl 还原不生效）
  @retval 0  不可映射（调用方应跳过该键继续排空队列）
**/
static
UINT32
MapEfiKeyToLv (
  IN CONST EFI_INPUT_KEY  *Key,
  IN UINT32               KeyShiftState
  )
{
  if (Key->ScanCode != SCAN_NULL) {
    switch (Key->ScanCode) {
      case SCAN_UP:        return LV_KEY_UP;
      case SCAN_DOWN:      return LV_KEY_DOWN;
      case SCAN_LEFT:      return LV_KEY_LEFT;
      case SCAN_RIGHT:     return LV_KEY_RIGHT;
      case SCAN_HOME:      return LV_KEY_HOME;
      case SCAN_END:       return LV_KEY_END;
      case SCAN_PAGE_UP:   return LVGL_KEY_PAGE_UP;
      case SCAN_PAGE_DOWN: return LVGL_KEY_PAGE_DOWN;
      case SCAN_DELETE:    return LV_KEY_DEL;
      case SCAN_ESC:       return LV_KEY_ESC;
      case SCAN_F2:        return LVGL_KEY_F2;   /* Task 7：重命名入口（自定义键值，group 不拦截） */
      case SCAN_F1:        return LVGL_KEY_F1;   /* Task 9：关于框（自定义键值，group 不拦截） */
      case SCAN_F5:        return LVGL_KEY_F5;   /* Task 9：刷新（自定义键值，group 不拦截） */
      default:             return 0;
    }
  }

  if (Key->UnicodeChar == L'\r') {
    return LV_KEY_ENTER;
  }

  if (Key->UnicodeChar == L'\b') {
    return LV_KEY_BACKSPACE;
  }

  /* Task 10：Tab = 自定义 LVGL_KEY_TAB（区域切换语义）。UEFI 无
     SCAN_TAB 扫描码——Tab 以 UnicodeChar=0x09（SCAN_NULL）上报，与
     \r/\b 同走控制字符路径。Task 9a 曾映射为 LV_KEY_NEXT（LVGL 惯例
     Tab=组焦点前移），但组内导航在分区制下无意义且会与对话框焦点篱笆
     冲突——现改映射为自定义值：lv_indev 不拦截（非 NEXT/PREV），以
     KEY 事件发给焦点对象并冒泡到屏幕 ScrKeyCb，由 Ui 层 GuTabNext
     做列表↔树↔工具栏的区域切换。 */
  if (Key->UnicodeChar == L'\t') {
    return LVGL_KEY_TAB;
  }

  if ((Key->UnicodeChar >= 0x20) && (Key->UnicodeChar <= 0x7E)) {
    return (UINT32)Key->UnicodeChar;
  }

  /* Ctrl+字母 控制码还原（0x00..0x1A，\r/\b/\t 已在上文映射/排除） */
  if ((KeyShiftState & EFI_SHIFT_STATE_VALID) != 0 &&
      (KeyShiftState & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)) != 0 &&
      (Key->UnicodeChar <= 0x1A) &&
      (Key->UnicodeChar != L'\t')) {
    return (UINT32)(Key->UnicodeChar + 0x40);
  }

  return 0;
}

/**
  LVGL indev 读回调。Data 进入前已被 LVGL 清零并预填 key=last_key
  （lv_indev.c indev_read_core），故"无键"路径只需写 state。
  每次回调最多上报一个键（PRESSED 或补发的 RELEASED），余下靠
  continue_reading 驱动的后续回调消化。
**/
static
VOID
KbdReadCb (
  lv_indev_t      *Indev,
  lv_indev_data_t *Data
  )
{
  EFI_STATUS   Status;
  EFI_KEY_DATA KeyData;
  UINT32       KeyShiftState = 0;  // SimpleTextIn 兜底路径无此字段，恒 0
  UINT32       LvKey;

  //
  // 先补上一次 PRESSED 欠下的 RELEASED（理由见文件头"上报策略"）。
  // key 必须与按下时相同：释放分支（如 ENTER 的 CLICKED 事件）按
  // Data->key 判定。
  //
  if (mReleasePending) {
    Data->key               = mLastKey;
    Data->state             = LV_INDEV_STATE_RELEASED;
    Data->continue_reading  = TRUE;   // 可能还有排队键，继续读
    mReleasePending         = FALSE;
    /* 与按下日志对称：释放沿是 ENTER 的 CLICKED 等事件的判定依据。
       M4 降为 VERBOSE 级（见下）：FixedDebugPrintErrorLevelLib 掩码
       不含 VERBOSE，串口平时不可见。 */
    DEBUG ((DEBUG_VERBOSE, "[LvglPort] key lv=0x%x released\n", mLastKey));
    return;
  }

  //
  // 排空固件键队列：跳过不可映射键（F 键、纯修饰键按下等 ScanCode 与
  // UnicodeChar 全 0 的事件），直到拿到一个可上报的键或队列空。
  // 每次成功读都会消费队列项，循环必终止。
  //
  for (;;) {
    if (mTxtEx != NULL) {
      Status = mTxtEx->ReadKeyStrokeEx (mTxtEx, &KeyData);
      if (!EFI_ERROR (Status)) {
        UpdateModifiers (KeyData.KeyState.KeyShiftState);
        KeyShiftState = KeyData.KeyState.KeyShiftState;
      }
    } else {
      Status = mTxtIn->ReadKeyStroke (mTxtIn, &KeyData.Key);
    }

    if (EFI_ERROR (Status)) {
      // EFI_NOT_READY：无键。key 已由 LVGL 预填 last_key，无需设置。
      Data->state             = LV_INDEV_STATE_RELEASED;
      Data->continue_reading  = FALSE;
      return;
    }

    LvKey = MapEfiKeyToLv (&KeyData.Key, KeyShiftState);
    if (LvKey != 0) {
      break;
    }
  }

  DEBUG ((DEBUG_VERBOSE, "[LvglPort] key scan=0x%x chr=0x%x -> lv=0x%x\n",
          (UINT32)KeyData.Key.ScanCode, (UINT32)KeyData.Key.UnicodeChar, LvKey));
  Data->key               = LvKey;
  Data->state             = LV_INDEV_STATE_PRESSED;
  Data->continue_reading  = TRUE;     // 紧跟一次补 RELEASED / 继续排空
  mLastKey                = LvKey;
  mReleasePending         = TRUE;
}

/**
  初始化键盘 indev：拿输入协议、建默认焦点组、注册 KEYPAD indev。
  @retval EFI_SUCCESS           成功（SimpleTextInEx 或兜底 SimpleTextIn）
  @retval EFI_UNSUPPORTED       两条协议路径都拿不到（无控制台输入）
  @retval EFI_OUT_OF_RESOURCES  LVGL 组或 indev 创建失败
**/

EFI_STATUS
KeyboardInit (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = gBS->HandleProtocol (
                  gST->ConsoleInHandle,
                  &gEfiSimpleTextInputExProtocolGuid,
                  (VOID **)&mTxtEx
                  );
  if (EFI_ERROR (Status)) {
    mTxtEx = NULL;
    if (gST->ConIn == NULL) {
      DEBUG ((DEBUG_WARN, "[LvglPort] no console input protocol: %r\n", Status));
      return EFI_UNSUPPORTED;
    }

    mTxtIn = gST->ConIn;
    DEBUG ((
      DEBUG_WARN,
      "[LvglPort] SimpleTextInEx unavailable (%r), fallback to SimpleTextIn: "
      "Ctrl/Shift modifier combos unusable\n",
      Status
      ));
  }

  //
  // 键盘焦点组：设为默认组后，后续创建的顶层对象（Ui 层）自动入组，
  // 方向键/Enter 等经 indev 派发给组内焦点对象。
  //
  mGroup = lv_group_create ();
  if (mGroup == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  lv_group_set_default (mGroup);

  mIndev = lv_indev_create ();
  if (mIndev == NULL) {
    lv_group_delete (mGroup);
    mGroup = NULL;
    return EFI_OUT_OF_RESOURCES;
  }

  lv_indev_set_type (mIndev, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb (mIndev, KbdReadCb);
  lv_indev_set_group (mIndev, mGroup);
  return EFI_SUCCESS;
}

/**
  泵键盘事件到 LVGL。空实现：indev 自带读定时器（lv_indev_create 内部
  lv_timer_create(lv_indev_read_timer_cb, ...)），读取由主循环里的
  lv_timer_handler 按周期驱动，无需在此手动泵。
**/
VOID
KeyboardPoll (
  VOID
  )
{
}

/**
  释放键盘 indev 与焦点组。协议侧无资源要释放：SimpleTextInEx/SimpleTextIn
  由固件拥有，HandleProtocol 拿到的只是接口指针，不需要 CloseProtocol。
  须在 lv_deinit() 之前调用。
**/
VOID
KeyboardDeinit (
  VOID
  )
{
  if (mIndev != NULL) {
    lv_indev_delete (mIndev);
    mIndev = NULL;
  }

  //
  // 组也是本模块创建的 LVGL 资源，一并删除（lv_group_delete 会自动把组
  // 从引用它的 indev 上摘除；漏删 lv_deinit 也会经 lv_group_deinit 兜底，
  // 但显式释放不依赖调用顺序）。
  //
  if (mGroup != NULL) {
    lv_group_delete (mGroup);
    mGroup = NULL;
  }

  mModifiers      = 0;
  mLastKey        = 0;
  mReleasePending = FALSE;
  mTxtEx          = NULL;
  mTxtIn          = NULL;
}
