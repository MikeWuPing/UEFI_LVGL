/** @file
  鼠标输入驱动：Absolute Pointer（首选）/ Simple Pointer（兜底）→ LVGL
  LV_INDEV_TYPE_POINTER indev。

  协议选择
  --------
  优先 AbsolutePointer：绝对坐标可直接线性映射到屏幕像素，是图形界面
  可用的唯一现实路径（QEMU usb-mouse 经 OVMF 的 UsbMouseAbsolutePointerDxe
  绑定后走这条：boot 协议相对鼠标被该驱动适配成 0..1024 绝对窗口，初始
  居中；不要用 usb-tablet，OVMF 没有它的驱动——IsUsbMouse 只认
  subclass=1/protocol=2 的 boot mouse，平板是 0/0）。拿不到时退回
  SimplePointer 并打 DEBUG_WARN——相对位移模式 v1 只上报按键状态，光标
  不移动（已知限制，见 MouseReadCb 相对分支注释），仅保证固件无绝对
  指针时按键事件不丢。

  多实例加固（M4）：固件里同一指针协议可能有多个实例——ConSplitter 的
  虚拟聚合实例与真实 USB 鼠标的子句柄实例并存。虚拟实例表面"协议存在"
  却永远读不到物理输入（其 AbsolutePointer 的 MaxX=MaxY=0x10000 且
  GetState 恒 EFI_NOT_READY），而 LocateProtocol 只返回固件找到的第一个
  实例，可能误中虚拟实例。OpenBestPointerInstance 因此遍历全部句柄，
  优先打开带 DevicePath 的实例：真实设备子句柄按 UEFI 驱动模型必有
  设备路径，ConSplitter 的虚拟句柄不是设备、没有设备路径。全无设备
  路径时退回句柄 0 并打 DEBUG_WARN；init 日志打印选中来源
  （device-path / fallback-handle0）供串口取证核对。

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
  DEVICE_ERROR 时沿用缓存只刷新 state；坐标则由 LVGL 在进入回调前预填
  上次值（lv_indev.c indev_read_core），无需本驱动重复缓存。
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
        DEBUG_VERBOSE,
        "[LvglPort] ptr x=%u y=%u btn=0x%x\n",
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

    Status = mSimple->GetState (mSimple, &State);
    if (!EFI_ERROR (Status)) {
      mPressed = State.LeftButton;
      //
      // 已知限制（v1）：RelativeMovementX/Y 不累加进光标坐标，光标停在
      // 原点，只上报按键状态。相对位移累加需要按 Mode->ResolutionX/Y
      // （counts/mm）换算像素增量并做屏幕边界钳制，留待后续版本。
      // QEMU usb-mouse 在 OVMF 下走绝对模式（见文件头），相对路径仅为
      // 兜底，v1 不值得投入。
      //
    }
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
  都无设备路径时退回句柄 0（保持单实例/全虚拟场景的原行为）并打
  DEBUG_WARN。

  GET_PROTOCOL 属性下 AgentHandle/ControllerHandle 按 UEFI 规范可传
  NULL：本驱动只读接口，不向驱动模型声明消费关系。

  @param[in]  ProtocolGuid  要定位的指针协议 GUID
  @param[in]  ProtocolName  协议名（fallback 告警日志用，ASCII）
  @param[out] Interface     打开的协议实例；失败时为 NULL
  @param[out] Source        选中来源："device-path" / "fallback-handle0"
  @retval EFI_SUCCESS    成功打开某实例
  @retval EFI_NOT_FOUND  没有任何句柄安装该协议，或全部打开失败
**/
static
EFI_STATUS
OpenBestPointerInstance (
  IN  EFI_GUID     *ProtocolGuid,
  IN  CONST CHAR8  *ProtocolName,
  OUT VOID         **Interface,
  OUT CONST CHAR8  **Source
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;

  *Interface = NULL;
  *Source    = NULL;

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
      *Source = "device-path";
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
      *Source = "fallback-handle0";
      DEBUG ((
        DEBUG_WARN,
        "[LvglPort] %a: no device-path instance among %u handles, "
        "fallback to handle0 (ConSplitter virtual instance possible)\n",
        ProtocolName,
        (UINT32)HandleCount
        ));
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
  EFI_STATUS   Status;
  CONST CHAR8  *AbsSource;
  CONST CHAR8  *SimpleSource;

  AbsSource    = NULL;
  SimpleSource = NULL;

  Status = OpenBestPointerInstance (
             &gEfiAbsolutePointerProtocolGuid,
             "AbsolutePointer",
             (VOID **)&mAbs,
             &AbsSource
             );
  if (!EFI_ERROR (Status) && (mAbs->Mode == NULL)) {
    //
    // 规范保证协议带 Mode 指针；异常固件下防御性判空，拿不到 Mode 的
    // "绝对指针"无法做坐标映射，按没有绝对指针处理，继续走兜底。
    //
    DEBUG ((DEBUG_WARN, "[LvglPort] AbsolutePointer with NULL Mode, ignored\n"));
    mAbs = NULL;
  }

  if (mAbs == NULL) {
    Status = OpenBestPointerInstance (
               &gEfiSimplePointerProtocolGuid,
               "SimplePointer",
               (VOID **)&mSimple,
               &SimpleSource
               );
    if (EFI_ERROR (Status)) {
      mSimple = NULL;
      DEBUG ((DEBUG_WARN, "[LvglPort] no pointer protocol found: %r\n", Status));
      return EFI_UNSUPPORTED;
    }

    DEBUG ((
      DEBUG_WARN,
      "[LvglPort] AbsolutePointer unavailable, fallback to SimplePointer: "
      "relative mode reports buttons only, cursor fixed (v1 limitation)\n"
      ));
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
      AbsSource,
      (UINT32)mAbs->Mode->AbsoluteMaxX,
      (UINT32)mAbs->Mode->AbsoluteMaxY
      ));
  } else {
    DEBUG ((
      DEBUG_INFO,
      "[LvglPort] pointer: SimplePointer fallback (%a)\n",
      SimpleSource
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
}
