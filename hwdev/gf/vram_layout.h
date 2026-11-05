/* Shared boot/runtime NV40 VRAM placement. MIT nouveau nv40.c:158-206
 * (instmem oneinit); push placement is GF_FEAT2_DESIGN section 3. */
#ifndef HWDEV_GF_VRAM_LAYOUT_H
#define HWDEV_GF_VRAM_LAYOUT_H
#include "../../include/types.h"
#include "../../include/memmap.h"
#define GF_VRAM_SIZE_REG 0x10020cU
#define GF_VS_REG 0x1540U
#define GF_VS_MAX 8U
#define GF_VS_CONTEXT_BYTES 0x4980U
#define GF_BASE_CONTEXT_BYTES 0x4000U
#define GF_CONTEXT_COUNT 32U
#define GF_RESERVE_BASE_BYTES 0x100000U
#define GF_INSTMEM_BYTES(vs) \
    (((GF_VS_CONTEXT_BYTES * (vs) + GF_BASE_CONTEXT_BYTES) * \
      GF_CONTEXT_COUNT + GF_RESERVE_BASE_BYTES + MEM_PAGE_SIZE - 1U) & \
     ~(MEM_PAGE_SIZE - 1U))
#define GF2_PUSH_BYTES 0x10000U
#define GF2_PUSH_ALIGN MEM_PAGE_SIZE
static inline u32 gf_vram_bytes(u32 pfb)
{
    return pfb & 0xff000000U;
}
static inline u32 gf_instmem_reserved(u32 mask)
{
    u32 vs = 0;
    mask = (mask >> 8) & 0xffU;
    while (mask) { vs += mask & 1U; mask >>= 1; }
    return GF_INSTMEM_BYTES(vs);
}
/* Caller checks available >= GF2_PUSH_BYTES before subtracting. */
static inline u32 gf_push_base(u32 available)
{
    return (available - GF2_PUSH_BYTES) & ~(GF2_PUSH_ALIGN - 1U);
}
#endif
