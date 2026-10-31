/* KAPI trampoline layout checks and initialization. Included by exec.c. */
/* 写し場 (票 T9 §12 R1) はスタブの後ろに置く。KAPI が増えて 1 ページに
 * 収まらなくなったら **ここでビルドが落ちる** — 実機では「cd の直後に
 * pwd が化ける」としか見えないので、静的に止める。 */
STATIC_ASSERT(MEM_PHYS_RAM_CEILING <= MEM_APP_BAND_BASE, ram_below_app_band);
STATIC_ASSERT((MEM_EXEC_HEAP_BASE >> PDE_SHIFT) >= APP_BAND_PDE &&
              (MEM_EXEC_HEAP_BASE >> PDE_SHIFT) < (MEM_APP_BAND_MAX_TOP >> PDE_SHIFT), heap_pde_in_app_band);
STATIC_ASSERT(((MEM_APP_STACK_TOP - 1) >> PDE_SHIFT) >= APP_BAND_PDE &&
              ((MEM_APP_STACK_TOP - 1) >> PDE_SHIFT) < (MEM_APP_BAND_MAX_TOP >> PDE_SHIFT), stack_pde_in_app_band);
STATIC_ASSERT(RING3_USTR_OFF + RING3_USTR_CAP <= (u32)PAGE_SIZE,
              ring3_ustr_fits_in_trampoline_page);
STATIC_ASSERT(RING3_LS_SHIM_OFF + RING3_LS_SHIM_CAP <= PAGE_SIZE,
              ring3_ls_fits_in_trampoline_page);

/* データ欄の固定配置 (票 TASK_KAPI_DATA_FIELDS、KAPI v63)。生成器
 * (sdk/gen_kapi.py) の KAPI_DATA_FIELDS_OFF と構造体の実際の並びが一致し、
 * 1 ページの容量 (表 + スタブ 8B × 容量 + 写し場) を越えないことを静的に見る。
 * ここが落ちたら関数表の容量 (kapi.json の func_capacity) を見直す票を起こす。 */
STATIC_ASSERT(__builtin_offsetof(KernelAPI, sbrk_heap_limit) ==
              (u32)KAPI_DATA_FIELDS_OFF, kapi_data_fields_fixed);
STATIC_ASSERT(__builtin_offsetof(KernelAPI, shm_base) ==
              (u32)KAPI_DATA_FIELDS_OFF + 4u, kapi_shm_base_fixed);
STATIC_ASSERT((u32)KAPI_DATA_IDX_SBRK_HEAP_LIMIT * 4u == (u32)KAPI_DATA_FIELDS_OFF,
              kapi_data_idx_sbrk);
STATIC_ASSERT((u32)KAPI_DATA_IDX_SHM_BASE * 4u == (u32)KAPI_DATA_FIELDS_OFF + 4u,
              kapi_data_idx_shm);
STATIC_ASSERT((u32)KAPI_FUNC_COUNT <= (u32)KAPI_FUNC_CAPACITY, kapi_func_capacity);
STATIC_ASSERT(sizeof(KernelAPI) + (u32)KAPI_FUNC_CAPACITY * KAPI_STUB_SIZE + RING3_USTR_CAP
              <= (u32)PAGE_SIZE, kapi_trampoline_one_page);
/* 本物の表は KAPI_ADDR からの 4KB (MEM_KAPI_SIZE) に置く (include/memmap.h)。 */
STATIC_ASSERT(sizeof(KernelAPI) <= (u32)MEM_KAPI_SIZE, kapi_table_fits_reserve);

/* ======================================================================== */
/*  ring3_trampoline_init — トランポリンページの構築 (v2 M2b)               */
/* ======================================================================== */
static void ring3_trampoline_init(void)
{
    u32 page = exec_tramp_page_addr();
    u32 *tbl = (u32 *)P2V(page);
    u32 stub_base = page + RING3_USTR_STUB_OFF;   /* 全 struct の後ろ */
    u32 i;

    ring3_tramp_page = page;

    /* magic / version は本物と同じ値 */
    tbl[0] = kapi->magic;
    tbl[1] = kapi->version;

    /* 予約スロット (KAPI_FUNC_COUNT..KAPI_FUNC_CAPACITY-1) にもスタブを置く
     * (票 TASK_KAPI_DATA_FIELDS)。NULL にすると旧 SDK が新しい関数を呼んだ
     * とき 0 番地へ飛ぶ。スタブならディスパッチャが slot >= KAPI_FUNC_COUNT
     * でアプリだけ kill する。 */
    for (i = 0; i < KAPI_FUNC_CAPACITY; i++) {
        u8 *st = (u8 *)P2V(stub_base + i * KAPI_STUB_SIZE);
        /* ユーザ可視表: entry[i] = スタブ i の番地 (KernelAPI fn[i] と同一 offset) */
        tbl[KAPI_FN_TABLE_FIRST + i] = stub_base + i * KAPI_STUB_SIZE;
        /* スタブ: B8 <slot:imm32> CD 80 C3  (mov eax,slot; int 0x80; ret) */
        st[0] = 0xB8;
        st[1] = (u8)(i & 0xFF);
        st[2] = (u8)((i >> 8) & 0xFF);
        st[3] = (u8)((i >> 16) & 0xFF);
        st[4] = (u8)((i >> 24) & 0xFF);
        st[5] = 0xCD;   /* int */
        st[6] = 0x80;   /* 0x80 */
        st[7] = 0xC3;   /* ret */
    }

    kmemcpy(P2V(page + RING3_LS_SHIM_OFF), ring3_ls_start,
            (u32)(ring3_ls_end - ring3_ls_start));
    tbl[KAPI_FN_TABLE_FIRST + KAPI_SLOT_SYS_LS] = page + RING3_LS_SHIM_OFF;

    /* データフィールド (値): KernelAPI 表と同一オフセット。v63 から
     * KAPI_DATA_FIELDS_OFF に固定 (関数の数ではなく容量の後ろ)。
     * sbrk_heap_limit は exec_run が launch 時に上書きする。 */
    tbl[KAPI_DATA_IDX_SBRK_HEAP_LIMIT] = 0;
    tbl[KAPI_DATA_IDX_SHM_BASE] = (u32)MEM_SHM_BASE;

    /* boot 口が確定した全 PD 共有 RO+USER (kernel band PDE0)。i386 は NX なしなので
     * RO でも実行可能 (スタブ実行 OK)。ユーザは書けない = スタブ改竄不可。
     * CR0.WP=0 により、この初期化と per-launch のデータ更新も RO に書ける。 */
}
