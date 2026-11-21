/* KAPI trampoline layout checks and initialization. Included by exec.c. */
#include "syscall_code.h"
/* 写し場は第 1 頁、スタブと ls シムは第 2 頁に収める。 */
STATIC_ASSERT(MEM_PHYS_RAM_CEILING <= MEM_APP_BAND_BASE, ram_below_app_band);
STATIC_ASSERT((MEM_EXEC_HEAP_BASE >> PDE_SHIFT) >= APP_BAND_PDE &&
              (MEM_EXEC_HEAP_BASE >> PDE_SHIFT) < (MEM_APP_BAND_MAX_TOP >> PDE_SHIFT), heap_pde_in_app_band);
STATIC_ASSERT(((MEM_APP_STACK_TOP - 1) >> PDE_SHIFT) >= APP_BAND_PDE &&
              ((MEM_APP_STACK_TOP - 1) >> PDE_SHIFT) < (MEM_APP_BAND_MAX_TOP >> PDE_SHIFT), stack_pde_in_app_band);
STATIC_ASSERT(RING3_USTR_OFF + RING3_USTR_CAP <= (u32)PAGE_SIZE,
              ring3_ustr_fits_in_trampoline_page);
STATIC_ASSERT(RING3_LS_SHIM_OFF + RING3_LS_SHIM_CAP <= RING3_TRAMP_BYTES,
              ring3_ls_fits_in_trampoline_page);

/* 生成したデータ変位と C のメンバー配置を照合する。 */
STATIC_ASSERT(__builtin_offsetof(KernelAPI, sbrk_heap_limit) ==
              (u32)KAPI_DATA_FIELDS_OFF, kapi_data_fields_fixed);
STATIC_ASSERT(__builtin_offsetof(KernelAPI, shm_base) ==
              (u32)KAPI_DATA_FIELDS_OFF + 4u, kapi_shm_base_fixed);
STATIC_ASSERT((u32)KAPI_DATA_IDX_SBRK_HEAP_LIMIT * 4u == (u32)KAPI_DATA_FIELDS_OFF,
              kapi_data_idx_sbrk);
STATIC_ASSERT((u32)KAPI_DATA_IDX_SHM_BASE * 4u == (u32)KAPI_DATA_FIELDS_OFF + 4u,
              kapi_data_idx_shm);
STATIC_ASSERT((u32)KAPI_FUNC_COUNT <= (u32)KAPI_FUNC_CAPACITY, kapi_func_capacity);
/* 本物の表は KAPI_ADDR からの 4KB (MEM_KAPI_SIZE) に置く (include/memmap.h)。 */
STATIC_ASSERT(sizeof(KernelAPI) <= (u32)MEM_KAPI_SIZE, kapi_table_fits_reserve);

/* ======================================================================== */
/*  ring3_trampoline_init — トランポリンページの構築 (v2 M2b)               */
/* ======================================================================== */
int ring3_trampoline_init(void)
{
    u32 page = exec_tramp_page_addr();
    u32 *tbl = (u32 *)P2V(page);
    struct syscall_layout layout;
    u32 i;

    if (!arch_syscall_layout(KAPI_FUNC_CAPACITY, &layout)) return -1;
    kmemset(tbl, 0, RING3_TRAMP_BYTES);
    if (!arch_syscall_emit(P2V(page), page, RING3_TRAMP_BYTES, KAPI_FUNC_CAPACITY)) return -1;

    /* magic / version は本物と同じ値 */
    tbl[0] = kapi->magic;
    tbl[1] = kapi->version;

    /* 予約スロット (KAPI_FUNC_COUNT..KAPI_FUNC_CAPACITY-1) にもスタブを置く
     * (票 TASK_KAPI_DATA_FIELDS)。NULL にすると旧 SDK が新しい関数を呼んだ
     * とき 0 番地へ飛ぶ。スタブならディスパッチャが slot >= KAPI_FUNC_COUNT
     * でアプリだけ kill する。 */
    for (i = 0; i < KAPI_FUNC_CAPACITY; i++) {
        tbl[KAPI_FN_TABLE_FIRST + i] = page + layout.stub_off + i * layout.stub_stride;
    }
    tbl[KAPI_FN_TABLE_FIRST + KAPI_SLOT_SYS_LS] = page + layout.ls_off;

    /* データフィールド (値): KernelAPI 表と同一オフセット。v63 から
     * KAPI_DATA_FIELDS_OFF に固定 (関数の数ではなく容量の後ろ)。
     * sbrk_heap_limit は exec_run が launch 時に上書きする。 */
    tbl[KAPI_DATA_IDX_SBRK_HEAP_LIMIT] = kapi->sbrk_heap_limit;
    tbl[KAPI_DATA_IDX_SHM_BASE] = kapi->shm_base;
    arch_syscall_publish(P2V(page), RING3_TRAMP_BYTES);
    ring3_tramp_page = page;

    /* boot 口が確定した全 PD 共有 RO+USER (kernel band PDE0)。i386 は NX なしなので
     * RO でも実行可能 (スタブ実行 OK)。ユーザは書けない = スタブ改竄不可。
     * CR0.WP=0 により、この初期化と per-launch のデータ更新も RO に書ける。 */
    return 0;
}
