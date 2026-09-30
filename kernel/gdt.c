#include "types.h"
#include "kstring.h"
#include "gdt.h"
#include "x86_desc.h"   /* x86_load_gdt (arch/x86/、契約なし) */

/* GDTエントリ構造体 */
struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_middle;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed));

/* GDTポインタ構造体 */
struct gdt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

/* GDT は6エントリ (NULL, カーネルコード, カーネルデータ, TSS,
 * ユーザコード, ユーザデータ)。
 *
 * V86 ゲストはリアルモード形式のセグメントを使うので ring3 用の
 * コード/データディスクリプタを必要としないが、v2 の CPL=3
 * ネイティブプログラム (リング3) は USER_CS/USER_DS を使う (M1a)。
 * TSS は #GP/割り込み/フォールトで CPL=3→0 に戻るとき CPU が SS0:ESP0 から
 * カーネルスタックを取るために引き続き必要。
 *
 * セレクタ値・access バイト・エントリ数は CONTRACTS.md C1 で凍結。
 * gdt.h で定義しており、ここでは勝手に変えない。TSS は idx3 のまま固定し、
 * user code/data は末尾 (idx4/5) に足して TSS 番地を動かさない。 */

struct gdt_entry gdt[GDT_ENTRIES];
struct gdt_ptr gp;

/* GDTエントリ設定 */
static void gdt_set_gate(int num, u32 base, u32 limit, u8 access, u8 gran)
{
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access = access;
}

/* TSS ディスクリプタの設定 (tss.c の tss_init から呼ばれる)
 *
 * access = 0x89: P=1, DPL=0, S=0 (システム), type=9 (使用中でない 32bit TSS)
 * gran   = 0x00: バイト粒度。TSS は I/O ビットマップ込みで 8KB 強あるが、
 *                4KB 粒度にするとリミットが切り上がってビットマップの
 *                終端が曖昧になるので、バイト粒度で正確に指定する。 */
void gdt_set_tss(u32 base, u32 limit)
{
    gdt_set_gate(GDT_TSS_IDX, base, limit, 0x89, 0x00);
}

/* カーネルGDTの初期化 */
void gdt_init(void)
{
    gp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gp.base = (u32)&gdt;

    /* NULLディスクリプタ */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* カーネルコードセグメント (idx1, DPL=0): ベース=0, リミット=4GB,
     * 実行/読み込み可能, 32ビット, 4KB グラニュラリティ */
    gdt_set_gate(GDT_KERNEL_CS_IDX, 0, 0xFFFFFFFF,
                 GDT_ACCESS_KCODE, GDT_GRAN_FLAT);

    /* カーネルデータセグメント (idx2, DPL=0): ベース=0, リミット=4GB,
     * 読み書き可能, 32ビット, 4KB グラニュラリティ */
    gdt_set_gate(GDT_KERNEL_DS_IDX, 0, 0xFFFFFFFF,
                 GDT_ACCESS_KDATA, GDT_GRAN_FLAT);

    /* TSS (idx3) は tss_init() が gdt_set_tss() で埋める。
     * ltr する前に present=0 のままだと不正 TSS 例外になるので、
     * ここではゼロのままにしておき、tss_init() まで ltr しない。 */

    /* ユーザコードセグメント (idx4, DPL=3, USER_CS=0x23): フラット 4GB。
     * 保護はページ単位 (PTE_USER) で行うのでセグメントはフラットのまま。
     * CPL=3 プログラムの CS になる (M1d の iret フレーム)。 */
    gdt_set_gate(GDT_USER_CS_IDX, 0, 0xFFFFFFFF,
                 GDT_ACCESS_UCODE, GDT_GRAN_FLAT);

    /* ユーザデータセグメント (idx5, DPL=3, USER_DS=0x2B): フラット 4GB。
     * CPL=3 プログラムの SS/DS/ES になる。 */
    gdt_set_gate(GDT_USER_DS_IDX, 0, 0xFFFFFFFF,
                 GDT_ACCESS_UDATA, GDT_GRAN_FLAT);

    /* GDTのロードとセグメントレジスタの再設定 */
    x86_load_gdt((u32)&gp);
}
