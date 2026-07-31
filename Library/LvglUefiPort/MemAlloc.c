/** @file
  LVGL 自定义 stdlib 内存钩子，基于 UEFI AllocatePool/FreePool 实现。

  lv_conf.h 将 LV_USE_STDLIB_MALLOC 设为 LV_STDLIB_CUSTOM，因此 LVGL 要求
  适配层提供完整的 lv_mem_*_core() 函数族（必需符号集可对照
  lvgl/src/stdlib/clib/lv_mem_core_clib.c 的 LV_STDLIB_CLIB 变体）：
    lv_malloc_core / lv_realloc_core / lv_free_core
    lv_mem_monitor_core / lv_mem_test_core
    lv_mem_init / lv_mem_deinit / lv_mem_add_pool / lv_mem_remove_pool

  UEFI 内存池没有 realloc 语义，FreePool 也不需要尺寸；为了在不改 LVGL
  语义的前提下实现 lv_realloc_core（保留旧内容），分配时在块前多开
  8 字节记录本次请求的尺寸，realloc 时按 min(old, new) 拷贝。
**/

#include <Library/LvglLib.h>

#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

/// 块头尺寸：记录用户请求的字节数（UINT64）。
#define LV_POOL_HDR_SIZE  8

/**
  初始化内存分配器。UEFI 内存池由固件维护，无需初始化。
**/
void
lv_mem_init (
  void
  )
{
  return;
}

/**
  反初始化内存分配器。无需操作（进程退出时固件统一回收）。
**/
void
lv_mem_deinit (
  void
  )
{
  return;
}

/**
  追加内存池。UEFI 池全局唯一且不可追加，恒返回 NULL（不支持）。
**/
lv_mem_pool_t
lv_mem_add_pool (
  void    *mem,
  size_t  bytes
  )
{
  LV_UNUSED (mem);
  LV_UNUSED (bytes);
  return NULL;
}

/**
  移除内存池。不支持，空操作。
**/
void
lv_mem_remove_pool (
  lv_mem_pool_t  pool
  )
{
  LV_UNUSED (pool);
  return;
}

/**
  分配 size 字节。块头多分配 LV_POOL_HDR_SIZE 字节记录请求尺寸。
  @param size  请求字节数
  @return 用户区指针；失败返回 NULL
**/
void *
lv_malloc_core (
  size_t  size
  )
{
  UINT8  *Raw;

  // 防止 size + 块头溢出回绕成小块分配。
  if (size > (size_t)(MAX_UINTN - LV_POOL_HDR_SIZE)) {
    return NULL;
  }

  Raw = AllocatePool ((UINTN)size + LV_POOL_HDR_SIZE);
  if (Raw == NULL) {
    return NULL;
  }

  *(UINT64 *)Raw = (UINT64)size;
  return Raw + LV_POOL_HDR_SIZE;
}

/**
  释放 lv_malloc_core/lv_realloc_core 返回的指针。
**/
void
lv_free_core (
  void  *p
  )
{
  if (p != NULL) {
    FreePool ((UINT8 *)p - LV_POOL_HDR_SIZE);
  }
}

/**
  重新分配为 new_size 字节，旧内容按 min(old, new) 保留。
  EDK2 无 ReallocatePool 语义，这里"分配-拷贝-释放"。
  @param p         原指针（可为 NULL，等价于 malloc）
  @param new_size  新尺寸
  @return 新指针；失败返回 NULL 且原块保持有效
**/
void *
lv_realloc_core (
  void    *p,
  size_t  new_size
  )
{
  UINT8   *Raw;
  UINT8   *NewRaw;
  UINT64  Old;

  if (p == NULL) {
    return lv_malloc_core (new_size);
  }

  // 防止 new_size + 块头溢出回绕成小块分配（原块保持有效）。
  if (new_size > (size_t)(MAX_UINTN - LV_POOL_HDR_SIZE)) {
    return NULL;
  }

  Raw    = (UINT8 *)p - LV_POOL_HDR_SIZE;
  Old    = *(UINT64 *)Raw;
  NewRaw = AllocatePool ((UINTN)new_size + LV_POOL_HDR_SIZE);
  if (NewRaw == NULL) {
    return NULL;
  }

  *(UINT64 *)NewRaw = (UINT64)new_size;
  CopyMem (
    NewRaw + LV_POOL_HDR_SIZE,
    Raw + LV_POOL_HDR_SIZE,
    (UINTN)(Old < (UINT64)new_size ? Old : (UINT64)new_size)
    );
  FreePool (Raw);
  return NewRaw + LV_POOL_HDR_SIZE;
}

/**
  汇总堆状态。UEFI 池无法按分配者统计，留零（调用方 lv_mem_monitor()
  已先行清零结构体），与 CLIB 变体行为一致。
**/
void
lv_mem_monitor_core (
  lv_mem_monitor_t  *mon_p
  )
{
  LV_UNUSED (mon_p);
  return;
}

/**
  分配器自检，覆盖三条关键路径并回读校验数据：
    1. lv_realloc_core(NULL, n) 等价 malloc 的路径；
    2. 扩容 realloc（64 -> 128）后前 64 字节内容必须原样保留；
    3. 收缩 realloc（128 -> 32）后前 32 字节内容必须原样保留。
  @return LV_RESULT_OK 表示分配器工作正常，否则 LV_RESULT_INVALID
**/
lv_result_t
lv_mem_test_core (
  void
  )
{
  void   *p;
  void   *q;
  UINTN  Index;

  //
  // NULL-realloc 路径：分配 64 字节并填充特征值。
  //
  p = lv_realloc_core (NULL, 64);
  if (p == NULL) {
    return LV_RESULT_INVALID;
  }

  SetMem (p, 64, 0xA5);

  //
  // 扩容到 128：旧内容（前 64 字节）必须完整保留。
  //
  q = lv_realloc_core (p, 128);
  if (q == NULL) {
    lv_free_core (p);
    return LV_RESULT_INVALID;
  }

  for (Index = 0; Index < 64; Index++) {
    if (((UINT8 *)q)[Index] != 0xA5) {
      lv_free_core (q);
      return LV_RESULT_INVALID;
    }
  }

  //
  // 收缩到 32：前 32 字节必须完整保留。
  //
  p = lv_realloc_core (q, 32);
  if (p == NULL) {
    lv_free_core (q);
    return LV_RESULT_INVALID;
  }

  for (Index = 0; Index < 32; Index++) {
    if (((UINT8 *)p)[Index] != 0xA5) {
      lv_free_core (p);
      return LV_RESULT_INVALID;
    }
  }

  lv_free_core (p);
  return LV_RESULT_OK;
}

/**
  内存拷贝。LVGL v9.2 的 string 实现走内置路径（LV_STDLIB_BUILTIN），
  并不引用本函数；保留此便捷封装供适配层自身使用。
**/
void *
lv_memcpy_core (
  void          *dst,
  const void    *src,
  size_t        len
  )
{
  return CopyMem (dst, src, (UINTN)len);
}

/**
  内存填充。说明同 lv_memcpy_core。
**/
void *
lv_memset_core (
  void    *dst,
  int     v,
  size_t  len
  )
{
  return SetMem (dst, (UINTN)len, (UINT8)v);
}
