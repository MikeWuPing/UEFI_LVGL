/** @file
  LVGL 毫秒 tick 源：基于 X64 TSC（时间戳计数器）实现。

  为什么不走 MdePkg 的 TimerLib：对 UEFI 应用可用的几个实例在本场景
  下都不可用——
    * BaseTimerLibNullTemplate：GetPerformanceCounter 直接 ASSERT(FALSE)
      并返回 0，是占位空模板；
    * SecPeiDxeTimerLibCpu：读本地 APIC 定时器的当前计数，而 DXE 阶段
      固件从不启动该定时器（TMICT 恒为 0），读数恒 0；
    * OvmfPkg/PcAtChipsetPkg 的 AcpiTimerLib：暴露的是原始 24bit PM
      定时器（3.579545MHz），约 4.7 秒回绕一次，编辑器会话必然踩中。
  TSC 是 64bit 单调计数器（现代 CPU/QEMU 均为 invariant TSC），不会
  回绕；LvglPkg 只支持 X64，AsmReadTsc() 恒可用。频率在 LvglTickInit()
  里用 gBS->Stall() 标定一次即可——OVMF 的 Stall 基于 PM 定时器，精度
  足够 UI tick 使用。
**/

#include <Library/LvglUefiPort.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>

/// 标定窗口：Stall 100ms。窗口越长频率误差越小，100ms 对启动速度无明显影响。
#define LV_TICK_CALIBRATION_US  100000

static UINT64  mTscStart;   ///< LvglTickInit 结束时刻的 TSC 读数
static UINT64  mTscFreq;    ///< TSC 频率（Hz），由标定得出

/**
  初始化 tick：用一次 100ms 的 Stall 标定 TSC 频率，并记录起始读数。
  必须在 lv_init() 及任何 LvglTickGetMs() 调用之前执行。
**/
VOID
LvglTickInit (
  VOID
  )
{
  UINT64  Tsc0;
  UINT64  Tsc1;

  Tsc0 = AsmReadTsc ();
  gBS->Stall (LV_TICK_CALIBRATION_US);
  Tsc1 = AsmReadTsc ();

  //
  // 频率 = 差值 * 1e6 / 窗口微秒数。先乘后除，不要求窗口整除 1e6；
  // 差值在 100ms 窗口下约 3e8（3GHz TSC），乘 1e6 后约 3e14，
  // 距 2^64 有五个数量级余量，不会溢出。
  //
  mTscFreq = MultU64x32 (Tsc1 - Tsc0, 1000000) / LV_TICK_CALIBRATION_US;
  if (mTscFreq < 1000) {
    //
    // Stall 失效（极端异常的固件）时兜底一个非零值，避免后续除零异常；
    // 此时 tick 速率不可靠，但系统不会崩。
    //
    mTscFreq = 1000000;
  }

  mTscStart = AsmReadTsc ();
}

/**
  返回自 LvglTickInit() 起经过的毫秒数。
  签名与 LVGL 的 lv_tick_get_cb_t（uint32_t (*)(void)）兼容，
  由 LvglPortInit() 通过 lv_tick_set_cb() 挂接。
**/
UINT32
LvglTickGetMs (
  VOID
  )
{
  UINT64  Elapsed;

  if (mTscFreq == 0) {
    // 未初始化先被调用：返回 0 而非除零崩溃。
    return 0;
  }

  Elapsed = AsmReadTsc () - mTscStart;
  //
  // mTscFreq/1000 对任何现实的 TSC 频率（< 4.3GHz）都远小于 2^32，
  // 且整除截断带来的相对误差在 30ppm 量级（一小时误差 < 0.5ms），
  // 对 UI tick 完全无感；2^64 的计数空间使回绕不构成问题。
  //
  return (UINT32)DivU64x32 (Elapsed, (UINT32)(mTscFreq / 1000));
}
