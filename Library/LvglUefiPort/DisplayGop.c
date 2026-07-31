/** @file
  LVGL 的 GOP 显示驱动：负责创建 LVGL display、全屏后备缓冲与 flush 回调。

  色彩格式（color format）的选择依据
  ------------------------------------
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL 的内存字节序恒为 B,G,R,X（结构体字段依次是
  Blue/Green/Red/Reserved）。LVGL v9.2.2 可渲染的 32bit 格式只有
  ARGB8888/XRGB8888 两种（lv_color.h 里并不存在 XBGR8888）；其 lv_color32_t
  按内存地址从低到高排列为 {blue, green, red, alpha}，XRGB8888 表示忽略
  alpha 字节——内存字节序同样是 B,G,R,X，与 BLT_PIXEL 逐字节吻合。
  因此后备缓冲按 LV_COLOR_FORMAT_XRGB8888 渲染后可以直接交给 Gop->Blt，
  无需任何像素转换。LV_COLOR_DEPTH=32 时该格式也正是 LVGL 的 NATIVE 格式，
  软件渲染器原生输出，无格式转换开销。

  渲染模式（render mode）的选择依据
  -----------------------------------
  采用 LV_DISPLAY_RENDER_MODE_DIRECT + 单块全屏后备缓冲。该模式要求缓冲
  为整屏大小，LVGL 只把脏区域渲染到缓冲中对应的绝对坐标处，flush 回调
  也只按脏区域提交——文本编辑器场景（光标闪烁、单行改动）下渲染量与 Blt
  流量都远小于 FULL 模式（FULL 模式下任何失效都会整屏重绘并整屏提交）。
  代价与 FULL 相同：一块整屏后备缓冲（1080p 约 8MB）。

  flush 回调中 px_map 恒指向后备缓冲起点，area 为绝对屏幕坐标，因此 Blt
  源起点必须偏移 (area->y1 * 屏宽 + area->x1) 个像素，且行距 Delta 为
  整屏行跨距（屏宽 * 4），而不是脏区宽度 * 4。
**/

#include <Library/LvglLib.h>
#include <Library/LvglUefiPort.h>

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>

static EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop;
static lv_display_t                  *mDisp;
static UINT32                        *mDrawBuf;   ///< 全屏 BGRX 后备缓冲
static UINT32                        mHor;        ///< 水平分辨率（像素）
static UINT32                        mVer;        ///< 垂直分辨率（像素）

/**
  LVGL flush 回调：把脏区域经 Gop->Blt(BufferToVideo) 提交到屏幕。
  DIRECT 模式下 px_map 指向全屏后备缓冲起点，源起点与行距按整屏几何计算。
  Blt 为同步调用，返回后立即 lv_display_flush_ready() 允许 LVGL 复用缓冲。
**/
static
VOID
GopFlushCb (
  lv_display_t    *disp,
  const lv_area_t *area,
  uint8_t         *px_map
  )
{
  EFI_STATUS  Status;
  UINTN       Width;
  UINTN       Height;
  UINT8       *Src;

  Width  = (UINTN)(area->x2 - area->x1 + 1);
  Height = (UINTN)(area->y2 - area->y1 + 1);

  //
  // px_map 是整屏后备缓冲的起点，脏区域首像素在
  // (area->y1 * mHor + area->x1) 处；Delta 是相邻两行行首的字节距离
  // （整屏行跨距），不是脏区宽 * 4。Delta == mHor*4 成立的前提是
  // LV_DRAW_BUF_STRIDE_ALIGN == 1（当前 lv_conf 默认值），即 LVGL 不为
  // 行跨距插入对齐填充。
  //
  Src = px_map + ((UINTN)area->y1 * mHor + (UINTN)area->x1) * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL);

  Status = mGop->Blt (
                   mGop,
                   (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Src,
                   EfiBltBufferToVideo,
                   0,
                   0,
                   (UINTN)area->x1,
                   (UINTN)area->y1,
                   Width,
                   Height,
                   (UINTN)mHor * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                   );
  if (EFI_ERROR (Status)) {
    // 不阻塞 LVGL：即使本次提交失败也必须 flush_ready，否则刷新状态机停摆。
    DEBUG ((DEBUG_WARN, "[LvglPort] GOP Blt failed: %r\n", Status));
  }

  lv_display_flush_ready (disp);
}

/**
  定位 GOP、按当前模式创建 LVGL display 与全屏后备缓冲。
  @retval EFI_SUCCESS           成功
  @retval EFI_UNSUPPORTED       PixelBitMask 像素格式（v1 不支持）
  @retval EFI_OUT_OF_RESOURCES  缓冲分配或 display 创建失败
  @retval 其他                  LocateProtocol 失败
**/
EFI_STATUS
GopDisplayInit (
  VOID
  )
{
  EFI_STATUS                          Status;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN                               BufSize;

  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)&mGop
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "[LvglPort] LocateProtocol GOP failed: %r\n", Status));
    return Status;
  }

  Info = mGop->Mode->Info;
  mHor = Info->HorizontalResolution;
  mVer = Info->VerticalResolution;
  DEBUG ((
    DEBUG_INFO,
    "[LvglPort] GOP %ux%u fmt=%u\n",
    mHor,
    mVer,
    Info->PixelFormat
    ));

  //
  // 本驱动只经 Gop->Blt 提交画面，Blt 源缓冲恒为 EFI_GRAPHICS_OUTPUT_BLT_PIXEL
  // 格式，与硬件像素排布无关——因此 PixelRedGreenBlue / PixelBlueGreenRed /
  // PixelBltOnly 三种模式都可接受（Blt 内部完成转换；PixelBltOnly 只是
  // 不允许 CPU 直访显存，而本驱动从不直访）。
  // PixelBitMask 的每通道位宽可变（如 30bit HDR），无法与固定的 BGRX8888
  // 渲染契约对应，v1 直接拒绝。
  //
  if (Info->PixelFormat == PixelBitMask) {
    DEBUG ((DEBUG_ERROR, "[LvglPort] PixelBitMask not supported\n"));
    return EFI_UNSUPPORTED;
  }

  //
  // 零分辨率模式（异常固件/未连接显示器）下分配 0 字节缓冲会让
  // lv_display_create(0,0) 之后的渲染语义崩塌，直接拒绝。
  //
  if ((mHor == 0) || (mVer == 0)) {
    DEBUG ((DEBUG_ERROR, "[LvglPort] zero GOP resolution\n"));
    return EFI_UNSUPPORTED;
  }

  BufSize  = (UINTN)mHor * mVer * sizeof (UINT32);
  mDrawBuf = AllocatePool (BufSize);
  if (mDrawBuf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mDisp = lv_display_create ((int32_t)mHor, (int32_t)mVer);
  if (mDisp == NULL) {
    FreePool (mDrawBuf);
    mDrawBuf = NULL;
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // BGRX（内存字节序 B,G,R,X）== LV_COLOR_FORMAT_XRGB8888，与
  // EFI_GRAPHICS_OUTPUT_BLT_PIXEL 逐字节吻合，Blt 无需像素转换。
  //
  lv_display_set_color_format (mDisp, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_flush_cb (mDisp, GopFlushCb);
  lv_display_set_buffers (
    mDisp,
    mDrawBuf,
    NULL,
    (uint32_t)BufSize,
    LV_DISPLAY_RENDER_MODE_DIRECT
    );
  return EFI_SUCCESS;
}

/**
  读取 GOP 当前分辨率。供 Task 7 的绝对指针坐标映射使用。
  未初始化时返回 0（静态变量零初始化）。
  @param[out] Hor  水平分辨率（像素），可为 NULL
  @param[out] Ver  垂直分辨率（像素），可为 NULL
**/
VOID
GopDisplayGetResolution (
  UINT32  *Hor,
  UINT32  *Ver
  )
{
  if (Hor != NULL) {
    *Hor = mHor;
  }

  if (Ver != NULL) {
    *Ver = mVer;
  }
}

/**
  释放 display 与后备缓冲。须在 lv_deinit() 之前调用
  （lv_display_delete 内部走 LVGL 堆释放）。
**/
VOID
GopDisplayDeinit (
  VOID
  )
{
  if (mDisp != NULL) {
    lv_display_delete (mDisp);
    mDisp = NULL;
  }

  if (mDrawBuf != NULL) {
    FreePool (mDrawBuf);
    mDrawBuf = NULL;
  }
}
