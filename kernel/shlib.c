/* ======================================================================== */
/*  SHLIB.C — 共有ライブラリ帯域 (MEM_SHLIB_BASE) のローダ (GUI v1.1 K3)     */
/*                                                                          */
/*  設計: docs/tasks/gui/DESIGN.md §9.3 (案 A) / 票 TASK_K3_shared_lib_band  */
/*  形式: OS32X (OS32X_FLAG_SHLIB) + 先頭ページの OS32ShlibHeader           */
/*        (sdk/include/os32/os32_kapi_shared.h、PM が凍結した正典)          */
/*                                                                          */
/*  帯域のレイアウト (すべてヘッダの値から決まる。ここに番地は書かない):      */
/*                                                                          */
/*    MEM_SHLIB_BASE                       ジャンプ表 (先頭 1 ページ)        */
/*    .. +text_pages*PAGE_SIZE             .text/.rodata  RO + USER (共有)   */
/*    data_vaddr .. +data_pages*PAGE_SIZE  .data/.bss     RW + USER (専用)   */
/*    MEM_SHLIB_END - data_pages*PAGE_SIZE .data/.bss の原本 (複製元)        */
/*                                                                          */
/*  原本を帯域の末尾に置くのは、pgalloc から取ると子プロセスの claim         */
/*  (exec_child_claim が [MEM_EXEC_LOAD_ADDR, ...) をまとめて mark/free する) */
/*  と衝突して、子の終了時に道連れで解放されてしまうため。帯域はロード成功時に */
/*  まるごと pgalloc_mark_used() で押さえ、以後解放しない。                   */
/* ======================================================================== */

#include "shlib.h"

#include "config.h"
#include "memmap.h"
#include "kprintf.h"
#include "kstring.h"
#include "paging.h"
#include "pgalloc.h"
#include "vfs.h"
#include "os32_kapi_shared.h"
#include "os32x_hdr.h"      /* ヘッダ v3 の配置照合 (票 TASK_KAPI_DATA_FIELDS) */

/* 帯域は PDE 1 (APP_BAND_PDE) の内側でなければならない。ここがずれると
 * 「アプリごとに差し替わる PT」の外へ出てしまい、.data の per-app 複製が
 * 全アプリで共有されてしまう (黙って壊れる)。 */
STATIC_ASSERT((MEM_SHLIB_BASE >> 22) == APP_BAND_PDE, shlib_in_app_band_pde);
STATIC_ASSERT(((MEM_SHLIB_END - 1) >> 22) == APP_BAND_PDE, shlib_end_in_app_band_pde);
STATIC_ASSERT(MEM_SHLIB_END == MEM_EXEC_LOAD_ADDR, shlib_band_below_exec_load);
STATIC_ASSERT((MEM_SHLIB_BASE & (PAGE_SIZE - 1)) == 0, shlib_base_page_aligned);

/* ======================================================================== */
/*  常駐状態                                                                */
/* ======================================================================== */
static int g_loaded = 0;
static u32 g_version = 0;      /* OS32ShlibHeader.version */
static u32 g_nfunc = 0;
static u32 g_text_end = MEM_SHLIB_BASE;   /* .text/.rodata の終端 (exclusive) */
static u32 g_data_vaddr = 0;
static u32 g_data_pages = 0;
static u32 g_data_master = 0;  /* .data/.bss の原本 (帯域末尾、複製元) */
/* 載せずに断った理由 (SHLIB_REJECT_*)。kernel.c が GUI を CUI へ落とすときの
 * 案内を分ける — 配置違いは「/sys を作り直す」、要求版はその逆で「カーネルを
 * 先に更新する」(票 TASK_KAPI_DATA_FIELDS、実装レビュー ラリー 2 の B1)。 */
static int g_reject = SHLIB_REJECT_NONE;
static u32 g_reject_min_api = 0;

/* attach したアドレス空間ごとの .data 複製ページ。
 * 現状 exec は CPL=3 アプリを 1 つしか同時に持たない (exec.c の g_ring3_as)
 * が、将来のネストに備えて数本持つ。 */
#define SHLIB_MAX_ATTACH  4
static struct {
    struct addrspace *as;
    u32 phys;
    u32 pages;
} g_attach[SHLIB_MAX_ATTACH];

/* ======================================================================== */
/*  shlib_init — /sys/lib/libos32gui.shlib を MEM_SHLIB_BASE に読む          */
/*                                                                          */
/*  呼ぶ位置: kernel.c のシェル起動ループの直前 (VFS / paging / pgalloc の    */
/*  初期化後)。ここより後に pgalloc が帯域のページを配ってしまうと            */
/*  ライブラリが黙って上書きされるので、遅らせないこと。                      */
/* ======================================================================== */
int shlib_init(void)
{
    const int band_pages = (int)(MEM_SHLIB_SIZE / PAGE_SIZE);
    u8 *buf = (u8 *)MEM_SHLIB_BASE;
    OS32Header *oh;
    OS32ShlibHeader *sh;
    u32 image_size;
    u32 text_end;
    u32 data_end;
    u32 master;
    int sz;
    int i;

    for (i = 0; i < SHLIB_MAX_ATTACH; i++) {
        g_attach[i].as = 0;
        g_attach[i].phys = 0;
        g_attach[i].pages = 0;
    }

    /* 先に帯域を押さえてから読む。読み込み中に他所が pgalloc から
     * ここを取ることは無いが、失敗経路で必ず戻すので順序を固定する。 */
    pgalloc_mark_used(MEM_SHLIB_BASE, band_pages);

    sz = vfs_read(SYS_SHLIB_GUI, buf, MEM_SHLIB_SIZE);
    if (sz <= 0) {
        /* 未ロード。GUI を使わないプログラムには影響しないので静かに戻る。 */
        kprintf(0x07, "[shlib] %s not found (GUI shlib disabled)\n", SYS_SHLIB_GUI);
        pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
        return -1;
    }

    /* ---- OS32X ヘッダの検証 ---- */
    oh = (OS32Header *)buf;
    if (oh->magic != OS32X_MAGIC ||
        oh->header_size < OS32X_HDR_V1_SIZE ||
        oh->header_size > (u32)sz) {
        kprintf(0xC1, "[shlib] bad OS32X header (magic=%x hsize=%u)\n",
                oh->magic, oh->header_size);
        pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
        return -1;
    }
    /* ---- 要求する KAPI 版 (exec のヘッダ検査と同じ条件) ----
     * このカーネルより新しい KAPI を要求するライブラリは、まだ無い関数
     * (予約スロット = OS32_ERR_NOSYS / CPL=3 は kill) を呼び得るので載せない
     * (実装レビュー R1、Codex 非 blocker)。 */
    if (oh->min_api_ver > KAPI_VERSION) {
        g_reject = SHLIB_REJECT_MIN_API;
        g_reject_min_api = oh->min_api_ver;
        kprintf(0xC1, "[shlib] %s: needs KAPI v%u > kernel v%u - "
                "update the kernel first (GUI shlib disabled)\n",
                SYS_SHLIB_GUI, oh->min_api_ver, (u32)KAPI_VERSION);
        pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
        return -1;
    }
    /* ---- KAPI データ欄の配置 (票 TASK_KAPI_DATA_FIELDS、ヘッダ v3) ----
     * ライブラリの .data にある os32api が KernelAPI のデータ欄 (shm_base =
     * GUI スロットの番地) を読む。配置が違うライブラリを載せると GUI アプリが
     * 別の番地を共有メモリと思い込むので、載せずに無効にする。 */
    {
        int lrc = os32x_layout_check(oh, (u32)sz, (u32)KAPI_DATA_FIELDS_OFF);
        if (lrc != OS32X_LAYOUT_OK) {
            g_reject = SHLIB_REJECT_LAYOUT;
            kprintf(0xC1, "[shlib] %s: %s - rebuild required (GUI shlib disabled)\n",
                    SYS_SHLIB_GUI, os32x_layout_reason(lrc));
            pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
            return -1;
        }
    }
    if ((oh->flags & OS32X_FLAG_SHLIB) == 0) {
        kprintf(0xC1, "[shlib] not a shared library (flags=%x)\n", oh->flags);
        pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
        return -1;
    }
    image_size = oh->text_size + oh->bss_size;
    if (oh->text_size + oh->header_size > (u32)sz || image_size > MEM_SHLIB_SIZE) {
        kprintf(0xC1, "[shlib] image too large (text=%u bss=%u)\n",
                oh->text_size, oh->bss_size);
        pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
        return -1;
    }

    /* ヘッダ分だけ前方へ詰める (exec_run と同じ。オーバーラップするので
     * kmemcpy ではなく memmove)。続けて .bss をゼロクリア。
     *   **`oh` は `buf` を指しているので、memmove の後にヘッダを読んではいけない**
     * — 詰めた時点でヘッダは上書きされ、`oh->text_size` / `oh->bss_size` は
     * ジャンプ表の別フィールド (text_pages / 予約 0) を読んでしまう。結果
     * `kmemset(..., 0, 0)` になり **.bss が一度もゼロクリアされず**、未初期化の
     * ままアプリへ複製されていた (2026-09-05 から存在、2026-09-14 に発見)。
     * 必ず**コピー前にローカルへ退避**した値を使う。 */
    {
        u32 hdr_size  = oh->header_size;
        u32 text_size = oh->text_size;
        u32 bss_size  = oh->bss_size;

        memmove(buf, buf + hdr_size, text_size);
        kmemset(buf + text_size, 0, bss_size);
    }

    /* ---- ジャンプ表 (OS32ShlibHeader) の検証 ---- */
    sh = (OS32ShlibHeader *)buf;
    if (sh->magic != OS32_SHLIB_MAGIC) {
        kprintf(0xC1, "[shlib] bad jump table magic %x\n", sh->magic);
        pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
        return -1;
    }
    if (sh->nfunc > (u32)OS32_SHLIB_MAX_FUNC) {
        kprintf(0xC1, "[shlib] nfunc %u > %u\n",
                sh->nfunc, (u32)OS32_SHLIB_MAX_FUNC);
        pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
        return -1;
    }
    if (sh->text_pages == 0 ||
        sh->text_pages > MEM_SHLIB_SIZE / PAGE_SIZE) {
        kprintf(0xC1, "[shlib] bad text_pages %u\n", sh->text_pages);
        pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
        return -1;
    }
    text_end = MEM_SHLIB_BASE + sh->text_pages * PAGE_SIZE;

    /* .data/.bss は .text の後ろのページ境界から。原本を帯域末尾に置くので
     * data_pages の 2 倍が帯域に収まらなければならない。 */
    data_end = sh->data_vaddr + sh->data_pages * PAGE_SIZE;
    if ((sh->data_vaddr & (PAGE_SIZE - 1)) != 0 ||
        sh->data_vaddr < text_end ||
        sh->data_pages > MEM_SHLIB_SIZE / PAGE_SIZE ||
        data_end > MEM_SHLIB_END ||
        data_end + sh->data_pages * PAGE_SIZE > MEM_SHLIB_END) {
        kprintf(0xC1, "[shlib] bad data range (vaddr=%x pages=%u)\n",
                sh->data_vaddr, sh->data_pages);
        pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
        return -1;
    }

    /* ---- .data/.bss の原本を帯域末尾に退避 (アプリごとの複製元) ---- */
    master = MEM_SHLIB_END - sh->data_pages * PAGE_SIZE;
    if (sh->data_pages > 0) {
        kmemcpy((void *)master, (const void *)sh->data_vaddr,
                sh->data_pages * PAGE_SIZE);
    }

    /* ---- .text/.rodata を read-only + USER で master PD に張る ----
     * アプリ PD は paging_addrspace_create が master の同帯 PT を写して
     * 作るので、この 1 回で全 PD に行き渡る。i386 に NX は無いので RO でも
     * 実行できる。CR0.WP=0 なのでカーネルからは引き続き書ける。 */
    if (paging_map_range(MEM_SHLIB_BASE, text_end, MEM_SHLIB_BASE,
                         PAGE_RO | PTE_USER) != 0) {
        kprintf(0xC1, "[shlib] text mapping failed\n");
        pgalloc_free_n(MEM_SHLIB_BASE, band_pages);
        return -1;
    }
    /* .data/.bss と原本は master では identity RW / USER なし のまま。
     * CPL=3 アプリには attach が per-PD で USER ページを張り直す。
     * CPL=0 プログラム (--cpl0) は master をそのまま共有する。 */

    g_version    = sh->version;
    g_nfunc      = sh->nfunc;
    g_text_end   = text_end;
    g_data_vaddr = sh->data_vaddr;
    g_data_pages = sh->data_pages;
    g_data_master = master;
    g_loaded     = 1;

    kprintf(0x0B, "[shlib] %s v%u loaded: %u funcs, text %u pg, data %u pg @%x\n",
            SYS_SHLIB_GUI, g_version, g_nfunc,
            sh->text_pages, g_data_pages, g_data_vaddr);
    return 0;
}

int shlib_loaded(void) { return g_loaded; }
int shlib_reject_reason(void) { return g_reject; }
u32 shlib_reject_min_api(void) { return g_reject_min_api; }
u32 shlib_version(void) { return g_loaded ? g_version : 0; }
u32 shlib_text_end(void) { return g_text_end; }
u32 shlib_data_pages(void) { return g_loaded ? g_data_pages : 0; }

/* ======================================================================== */
/*  shlib_addrspace_attach — アプリ PD にライブラリを張る                    */
/* ======================================================================== */
int shlib_addrspace_attach(struct addrspace *as)
{
    u32 phys;
    u32 i;
    int slot = -1;

    if (!g_loaded || !as) return 0;

    /* .text/.rodata: master から写っているはずだが明示的に張り直す
     * (PDE への USER 伝播もここで確実にする)。 */
    paging_addrspace_map_user_range(as, MEM_SHLIB_BASE, g_text_end,
                                    PAGE_RO | PTE_USER);

    if (g_data_pages == 0) return 0;

    for (i = 0; i < SHLIB_MAX_ATTACH; i++) {
        if (g_attach[i].as == 0) { slot = (int)i; break; }
    }
    if (slot < 0) {
        kprintf(0xC1, "[shlib] attach table full\n");
        return -1;
    }

    /* アプリ専用の物理ページ。この時点で exec は子プロセス帯
     * [MEM_EXEC_LOAD_ADDR, ...) を mark_used 済みなので、pgalloc は
     * その上の動的確保リザーブ (EXEC_DYN_RESERVE の穴) から返す。 */
    phys = pgalloc_alloc_n((int)g_data_pages);
    if (phys == 0) {
        kprintf(0xC1, "[shlib] no memory for %u data pages\n", g_data_pages);
        return -1;
    }

    /* 原本を複製 (identity マッピングなので物理=仮想で書ける)。 */
    kmemcpy((void *)phys, (const void *)g_data_master,
            g_data_pages * PAGE_SIZE);

    for (i = 0; i < g_data_pages; i++) {
        paging_addrspace_map_user(as,
                                  g_data_vaddr + i * PAGE_SIZE,
                                  phys + i * PAGE_SIZE,
                                  PAGE_RW | PTE_USER);
    }

    g_attach[slot].as = as;
    g_attach[slot].phys = phys;
    g_attach[slot].pages = g_data_pages;
    return 0;
}

/* ======================================================================== */
/*  shlib_addrspace_detach — 複製した .data/.bss ページを返す                */
/* ======================================================================== */
void shlib_addrspace_detach(struct addrspace *as)
{
    int i;

    if (!as) return;
    for (i = 0; i < SHLIB_MAX_ATTACH; i++) {
        if (g_attach[i].as == as) {
            pgalloc_free_n(g_attach[i].phys, (int)g_attach[i].pages);
            g_attach[i].as = 0;
            g_attach[i].phys = 0;
            g_attach[i].pages = 0;
            return;
        }
    }
}
