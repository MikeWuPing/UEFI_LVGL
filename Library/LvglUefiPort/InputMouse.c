/** @file
  鼠标输入驱动：Absolute Pointer（首选）/ Simple Pointer（兜底）→ LVGL
  LV_INDEV_TYPE_POINTER indev。

  协议选择
  --------
  优先 AbsolutePointer：绝对坐标可直接线性映射到屏幕像素，是图形界面
  最直接的输入路径（QEMU usb-mouse 经 OVMF 的 UsbMouseAbsolutePointerDxe
  绑定后走这条：boot 协议相对鼠标被该驱动适配成 0..1024 绝对窗口，初始
  居中；不要用 usb-tablet，OVMF 没有它的驱动——IsUsbMouse 只认
  subclass=1/protocol=2 的 boot mouse，平板是 0/0）。拿不到时退回
  SimplePointer 并打 DEBUG_WARN。相对位移按设备 Mode->ResolutionX/Y
  缩放为像素增量，从屏幕中心开始累加并钳制在 GOP 边界内；缩放比例沿用
  QurOKR 已验证的 Resolution/16 counts-per-pixel，并保留亚像素余数，避免
  高分辨率设备的连续小位移因逐次舍入而永久丢失。

  多实例加固（M4）：固件里同一指针协议可能有多个实例——ConSplitter 的
  虚拟聚合实例与真实 USB 鼠标的子句柄实例并存。虚拟实例表面"协议存在"
  却永远读不到物理输入（其 AbsolutePointer 的 MaxX=MaxY=0x10000 且
  GetState 恒 EFI_NOT_READY），而 LocateProtocol 只返回固件找到的第一个
  实例，可能误中虚拟实例。OpenBestPointerInstance 因此遍历全部句柄，
  优先打开带 DevicePath 的实例：真实设备子句柄按 UEFI 驱动模型必有
  设备路径，ConSplitter 的虚拟句柄不是设备、没有设备路径。MouseInit
  同时探测两种协议并按“真实 Absolute、真实 Simple、fallback Absolute、
  fallback Simple”选择，避免虚拟 Absolute 抢占真实 Simple；init 日志打印
  选中来源（device-path / fallback-handle0）供串口取证核对。

  坐标映射与按键语义
  ------------------
  绝对模式：x = CurrentX * 屏宽 / AbsoluteMaxX（MaxX==0 表示设备无该轴，
  除零保护取 0；CurrentX==MaxX 时结果越界，钳回 0..W-1）。按键位
  EFI_ABSP_TouchActive（bit0）：MdePkg AbsolutePointer.h 注释为 "This
  bit is set if the touch sensor is active"，即触屏语义下的"触点激活"；
  对绝对鼠标，UsbMouseAbsolutePointerDxe 直接把 USB HID 报告 byte0 的
  按键位（bit0=主键/左键）填入 ActiveButtons（驱动源码：
  State.ActiveButtons = Data[0] & (BIT0|BIT1|BIT2)），故该位在鼠标语义
  下就是左键按下。两种语义对本驱动等价：置位即 LV_INDEV_STATE_PRESSED。

  GetState 语义：两种协议的 GetState 在状态自上次调用起未变化时都返回
  EFI_NOT_READY（UEFI 规范），因此按键状态缓存于 mPressed，NOT_READY 与
  DEVICE_ERROR 时沿用缓存。绝对坐标由 LVGL 在进入回调前预填上次值；相对
  坐标必须由本驱动持久化，才能把后续 delta 积分为绝对屏幕坐标。
**/

#include <Library/LvglLib.h>
#include <Library/LvglUefiPort.h>

#include <Uefi.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimplePointer.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>

//
// DisplayGop.c 提供的内部接口（与 LvglUefiPort.c 顶部内部声明块一致）：
// 读取 GOP 当前分辨率，供绝对坐标 → 屏幕像素映射用。
//
VOID  GopDisplayGetResolution (UINT32 *Hor, UINT32 *Ver);

static EFI_ABSOLUTE_POINTER_PROTOCOL  *mAbs;      ///< 首选：绝对坐标
static EFI_SIMPLE_POINTER_PROTOCOL    *mSimple;   ///< 兜底：相对位移
static lv_indev_t                     *mIndev;
static BOOLEAN                        mPressed;   ///< 缓存的按键状态
static INT32                          mSimpleX;   ///< 相对模式累计后的屏幕坐标
static INT32                          mSimpleY;
static INT64                          mSimpleRemainderX; ///< 未满一个像素的设备 counts
static INT64                          mSimpleRemainderY;

typedef enum {
  PointerSourceNone,
  PointerSourceDevicePath,
  PointerSourceFallbackHandle0
} POINTER_SOURCE;

/** 返回协议实例来源的日志字符串。 */
static
CONST CHAR8 *
PointerSourceName (
  IN POINTER_SOURCE  Source
  )
{
  switch (Source) {
    case PointerSourceDevicePath:
      return "device-path";
    case PointerSourceFallbackHandle0:
      return "fallback-handle0";
    default:
      return "none";
  }
}

/**
  把 SimplePointer 的设备 counts 换算成像素增量，并跨读回调保留余数。

  QurOKR 在 QEMU/OVMF 上验证的比例为 Resolution/16 counts-per-pixel；EDK2
  UsbMouseDxe/Ps2MouseDxe 的 Resolution 分别为 8/4，故两者均退化为原始
  count 一像素。Resolution==0 按 UEFI 规范表示该轴不存在。
**/
static
INT32
ScaleSimplePointerDelta (
  IN     INT32   Delta,
  IN     UINT64  Resolution,
  IN OUT INT64   *Remainder
  )
{
  INT64  CountsPerPixel;
  INT64  Total;

  if (Resolution == 0) {
    *Remainder = 0;
    return 0;
  }

  CountsPerPixel = (INT64)(Resolution / 16U);
  if (CountsPerPixel <= 0) {
    CountsPerPixel = 1;
  }

  Total      = *Remainder + (INT64)Delta;
  *Remainder = Total % CountsPerPixel;
  return (INT32)(Total / CountsPerPixel);
}

/** 把相对模式的累计坐标钳制到单轴有效像素范围。 */
static
INT32
ClampSimplePointerCoordinate (
  IN INT64   Value,
  IN UINT32  Size
  )
{
  INT64  Maximum;

  if ((Size == 0) || (Value <= 0)) {
    return 0;
  }

  Maximum = (INT64)Size - 1;
  if (Value >= Maximum) {
    return (INT32)Maximum;
  }

  return (INT32)Value;
}

/**
  LVGL indev 读回调。按 mAbs/mSimple 哪个非空走绝对/相对分支。
  Data 进入前已被 LVGL 清零并预填 point=上次坐标，故 NOT_READY 路径
  只需写 state。
**/
static
VOID
MouseReadCb (
  lv_indev_t      *Indev,
  lv_indev_data_t *Data
  )
{
  EFI_STATUS  Status;

  if (mAbs != NULL) {
    EFI_ABSOLUTE_POINTER_STATE  State;
    UINT32                      Width;
    UINT32                      Height;
    UINT64                      MaxX;
    UINT64                      MaxY;
    UINT64                      X;
    UINT64                      Y;

    Status = mAbs->GetState (mAbs, &State);
    if (!EFI_ERROR (Status)) {
      //
      // 取证日志（M2 起；M4 降为 VERBOSE）：GetState 仅在状态变化时返回
      // 成功，故日志天然稀疏（移动/按下/释放沿各一条）。FixedDebugPrint-
      // ErrorLevelLib 的固定掩码不含 VERBOSE，串口平时看不到；需要核对
      // 坐标映射与按键沿时改库掩码即可恢复。
      //
      DEBUG ((
        DEBUG_INFO,
        "[LvglPort] ptr raw=%u,%u btn=0x%x\n",
        (UINT32)State.CurrentX,
        (UINT32)State.CurrentY,
        (UINT32)State.ActiveButtons
        ));

      GopDisplayGetResolution (&Width, &Height);
      MaxX = mAbs->Mode->AbsoluteMaxX;
      MaxY = mAbs->Mode->AbsoluteMaxY;

      //
      // 设备坐标 → 屏幕像素线性映射。MaxX==0 表示设备无该轴（MdePkg
      // AbsolutePointer.h：Min 与 Max 同为 0 时该轴不存在），除零保护。
      // CurrentX 与屏宽都是小量，UINT64 乘法不会溢出。
      //
      X = (MaxX != 0) ? (State.CurrentX * Width) / MaxX : 0;
      Y = (MaxY != 0) ? (State.CurrentY * Height) / MaxY : 0;

      //
      // CurrentX==MaxX 时上式结果==Width，越出有效列范围 0..Width-1；
      // 个别驱动还可能上报略超 MaxX 的值，统一钳回屏内。
      //
      if ((Width != 0) && (X >= Width)) {
        X = Width - 1;
      }

      if ((Height != 0) && (Y >= Height)) {
        Y = Height - 1;
      }

      Data->point.x = (int32_t)X;
      Data->point.y = (int32_t)Y;
      mPressed      = ((State.ActiveButtons & EFI_ABSP_TouchActive) != 0);
    }

    //
    // EFI_NOT_READY（状态未变）/ EFI_DEVICE_ERROR：point 已由 LVGL 预填
    // 上次值，按键沿用 mPressed 缓存，本次只刷新下面的 state。
    //
  } else {
    EFI_SIMPLE_POINTER_STATE  State;
    UINT32                    Width;
    UINT32                    Height;
    INT32                     DeltaX;
    INT32                     DeltaY;

    Status = mSimple->GetState (mSimple, &State);
    if (!EFI_ERROR (Status)) {
      mPressed = State.LeftButton;
      GopDisplayGetResolution (&Width, &Height);

      DeltaX = ScaleSimplePointerDelta (
                 State.RelativeMovementX,
                 mSimple->Mode->ResolutionX,
                 &mSimpleRemainderX
                 );
      DeltaY = ScaleSimplePointerDelta (
                 State.RelativeMovementY,
                 mSimple->Mode->ResolutionY,
                 &mSimpleRemainderY
                 );

      mSimpleX = ClampSimplePointerCoordinate ((INT64)mSimpleX + DeltaX, Width);
      mSimpleY = ClampSimplePointerCoordinate ((INT64)mSimpleY + DeltaY, Height);

      DEBUG ((
        DEBUG_VERBOSE,
        "[LvglPort] rel dx=%d dy=%d point=%d,%d left=%u\n",
        State.RelativeMovementX,
        State.RelativeMovementY,
        mSimpleX,
        mSimpleY,
        State.LeftButton ? 1U : 0U
        ));
    }

    //
    // 首次读即从屏幕中心上报；NOT_READY/DEVICE_ERROR 沿用累计坐标与按键
    // 缓存，不依赖 LVGL 内部 last_raw_point 的初始值。
    //
    Data->point.x = mSimpleX;
    Data->point.y = mSimpleY;
  }

  Data->state             = mPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  Data->continue_reading  = FALSE;
}

/**
  多实例加固的协议实例挑选（MouseInit 用，AbsolutePointer 与
  SimplePointer 路径共用）。

  遍历安装了 ProtocolGuid 的全部句柄，优先以 OpenProtocol(GET_PROTOCOL)
  打开带 DevicePath 的实例——真实 USB 鼠标经 UsbMouseAbsolutePointerDxe
  绑定到带设备路径的子句柄，而 ConSplitter 的虚拟聚合句柄不是设备、
  没有设备路径，据此绕开"协议存在但恒读不到输入"的虚拟实例。所有实例
  都无设备路径时退回句柄 0（保持单实例/全虚拟场景的原行为）。最终若
  选中 fallback 实例，MouseInit 统一打 DEBUG_WARN。

  GET_PROTOCOL 属性下 AgentHandle/ControllerHandle 按 UEFI 规范可传
  NULL：本驱动只读接口，不向驱动模型声明消费关系。

  @param[in]  ProtocolGuid  要定位的指针协议 GUID
  @param[out] Interface     打开的协议实例；失败时为 NULL
  @param[out] Source        选中来源枚举
  @retval EFI_SUCCESS    成功打开某实例
  @retval EFI_NOT_FOUND  没有任何句柄安装该协议，或全部打开失败
**/
static
EFI_STATUS
OpenBestPointerInstance (
  IN  EFI_GUID       *ProtocolGuid,
  OUT VOID           **Interface,
  OUT POINTER_SOURCE *Source
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;

  *Interface = NULL;
  *Source    = PointerSourceNone;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  ProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  //
  // 第一遍：找带 DevicePath 的真实设备实例。LocateHandleBuffer 成功即
  // 保证 HandleCount >= 1（无句柄时返回 EFI_NOT_FOUND）。
  //
  for (Index = 0; Index < HandleCount; Index++) {
    VOID  *DevicePath;

    if (EFI_ERROR (gBS->HandleProtocol (
                         Handles[Index],
                         &gEfiDevicePathProtocolGuid,
                         &DevicePath
                         )))
    {
      continue;
    }

    //
    // 规范对 OpenProtocol 失败时的 Interface 输出不做保证（可能残留
    // 垃圾值），每次尝试前显式清零，失败路径的 *Interface==NULL 判定
    // 才不依赖具体固件实现。
    //
    *Interface = NULL;
    Status = gBS->OpenProtocol (
                    Handles[Index],
                    ProtocolGuid,
                    Interface,
                    NULL,
                    NULL,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (!EFI_ERROR (Status)) {
      *Source = PointerSourceDevicePath;
      break;
    }
  }

  //
  // 第二遍（兜底）：全无设备路径或打开失败时退回句柄 0。可能命中
  // ConSplitter 虚拟实例——打 WARN 留取证痕迹，读回调侧的 NOT_READY
  // 缓存语义保证此情形只是"鼠标不动"，不会出错。
  //
  if (*Interface == NULL) {
    *Interface = NULL;   /* 同上的防御性清零（此刻恒为 NULL，明示语义） */
    Status = gBS->OpenProtocol (
                    Handles[0],
                    ProtocolGuid,
                    Interface,
                    NULL,
                    NULL,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (!EFI_ERROR (Status)) {
      *Source = PointerSourceFallbackHandle0;
    }
  }

  FreePool (Handles);
  return (*Interface != NULL) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

/**
  初始化鼠标 indev：定位指针协议、注册 POINTER indev。
  @retval EFI_SUCCESS           成功（绝对或相对模式之一）
  @retval EFI_UNSUPPORTED       两种指针协议都拿不到
  @retval EFI_OUT_OF_RESOURCES  indev 创建失败
**/
EFI_STATUS
MouseInit (
  VOID
  )
{
  EFI_ABSOLUTE_POINTER_PROTOCOL  *AbsCandidate;
  EFI_SIMPLE_POINTER_PROTOCOL    *SimpleCandidate;
  POINTER_SOURCE                 AbsSource;
  POINTER_SOURCE                 SimpleSource;
  POINTER_SOURCE                 SelectedSource;
  UINT32                         Width;
  UINT32                         Height;

  AbsCandidate    = NULL;
  SimpleCandidate = NULL;
  AbsSource       = PointerSourceNone;
  SimpleSource    = PointerSourceNone;
  SelectedSource  = PointerSourceNone;

  OpenBestPointerInstance (
    &gEfiAbsolutePointerProtocolGuid,
    (VOID **)&AbsCandidate,
    &AbsSource
    );
  if ((AbsCandidate != NULL) && (AbsCandidate->Mode == NULL)) {
    //
    // 规范保证协议带 Mode 指针；异常固件下防御性判空，拿不到 Mode 的
    // "绝对指针"无法做坐标映射，按没有绝对指针处理，继续走兜底。
    //
    DEBUG ((DEBUG_WARN, "[LvglPort] AbsolutePointer with NULL Mode, ignored\n"));
    AbsCandidate = NULL;
    AbsSource    = PointerSourceNone;
  }

  OpenBestPointerInstance (
    &gEfiSimplePointerProtocolGuid,
    (VOID **)&SimpleCandidate,
    &SimpleSource
    );
  if ((SimpleCandidate != NULL) && (SimpleCandidate->Mode == NULL)) {
    DEBUG ((DEBUG_WARN, "[LvglPort] SimplePointer with NULL Mode, ignored\n"));
    SimpleCandidate = NULL;
    SimpleSource    = PointerSourceNone;
  }

  //
  // 跨协议选择：真实绝对设备优先；若只有 ConSplitter 虚拟 Absolute，
  // 必须让带 DevicePath 的真实 SimplePointer 胜出，否则相对路径不可达。
  // 两类协议都只有虚拟聚合实例时保持原先的 Absolute 优先策略。
  //
  if ((AbsCandidate != NULL) && (AbsSource == PointerSourceDevicePath)) {
    mAbs           = AbsCandidate;
    SelectedSource = AbsSource;
  } else if ((SimpleCandidate != NULL) && (SimpleSource == PointerSourceDevicePath)) {
    mSimple        = SimpleCandidate;
    SelectedSource = SimpleSource;
  } else if (AbsCandidate != NULL) {
    mAbs           = AbsCandidate;
    SelectedSource = AbsSource;
  } else if (SimpleCandidate != NULL) {
    mSimple        = SimpleCandidate;
    SelectedSource = SimpleSource;
  } else {
    DEBUG ((DEBUG_WARN, "[LvglPort] no pointer protocol found\n"));
    return EFI_UNSUPPORTED;
  }

  if (SelectedSource == PointerSourceFallbackHandle0) {
    DEBUG ((
      DEBUG_WARN,
      "[LvglPort] selected pointer has no device path "
      "(ConSplitter virtual instance possible)\n"
      ));
  }

  if (mSimple != NULL) {
    GopDisplayGetResolution (&Width, &Height);
    mSimpleX          = (INT32)(Width / 2U);
    mSimpleY          = (INT32)(Height / 2U);
    mSimpleRemainderX = 0;
    mSimpleRemainderY = 0;

    DEBUG ((
      DEBUG_WARN,
      "[LvglPort] AbsolutePointer unavailable, fallback to SimplePointer: "
      "relative motion enabled\n"
      ));

    if ((mSimple->Mode->ResolutionX == 0) &&
        (mSimple->Mode->ResolutionY == 0))
    {
      DEBUG ((
        DEBUG_WARN,
        "[LvglPort] SimplePointer has no X/Y axis; buttons only\n"
        ));
    }
  }

  mIndev = lv_indev_create ();
  if (mIndev == NULL) {
    mAbs    = NULL;
    mSimple = NULL;
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // 记录选定的指针路径、实例来源与绝对坐标域：若选中来源是
  // fallback-handle0 且 max=0x10000 两端，基本可断定拿到的是
  // ConSplitter 虚拟 AbsolutePointer（无物理设备时 GetState 恒
  // NOT_READY）——这行日志是区分"真实指针设备缺失"与"驱动/映射缺陷"
  // 的判据（M2 取证曾据此定位 OVMF 缺 USB 鼠标驱动）。
  //
  if (mAbs != NULL) {
    DEBUG ((
      DEBUG_INFO,
      "[LvglPort] pointer: AbsolutePointer (%a) max=%ux%u\n",
      PointerSourceName (SelectedSource),
      (UINT32)mAbs->Mode->AbsoluteMaxX,
      (UINT32)mAbs->Mode->AbsoluteMaxY
      ));
  } else {
    DEBUG ((
      DEBUG_INFO,
      "[LvglPort] pointer: SimplePointer fallback (%a) res=%ux%u\n",
      PointerSourceName (SelectedSource),
      (UINT32)mSimple->Mode->ResolutionX,
      (UINT32)mSimple->Mode->ResolutionY
      ));
  }

  lv_indev_set_type (mIndev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb (mIndev, MouseReadCb);
  return EFI_SUCCESS;
}

/**
  返回鼠标 indev 句柄（LvglPortInit 挂可见光标用）。MouseInit 未调用
  或失败时为 NULL。
**/
VOID *
LvglPortGetMouseIndev (
  VOID
  )
{
  return (VOID *)mIndev;
}

/**
  泵鼠标事件到 LVGL。空实现：indev 自带读定时器，读取由主循环里的
  lv_timer_handler 按周期驱动（同 KeyboardPoll）。
**/
VOID
MousePoll (
  VOID
  )
{
}

/**
  释放鼠标 indev。协议侧无资源要释放：实例以 OpenProtocol(GET_PROTOCOL)
  且 AgentHandle=NULL 打开，UEFI 核心不为这种打开建立需归还的追踪
  记录，接口由固件拥有。须在 lv_deinit() 之前调用。
**/
VOID
MouseDeinit (
  VOID
  )
{
  if (mIndev != NULL) {
    lv_indev_delete (mIndev);
    mIndev = NULL;
  }

  mAbs     = NULL;
  mSimple  = NULL;
  mPressed = FALSE;
  mSimpleX = 0;
  mSimpleY = 0;
  mSimpleRemainderX = 0;
  mSimpleRemainderY = 0;
}
