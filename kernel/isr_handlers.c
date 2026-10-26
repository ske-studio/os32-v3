#include "sqlite_engine.h"
#include "font_guard.h"
#include "module_runtime.h"
#ifdef OS32_R1_FIXTURE
#include "r1_fixture.h"
#endif
/* ======================================================================== */
/*  ISR_HANDLERS.C — C言語割り込みハンドラ                                  */
/*                                                                          */
/*  例外: テキストVRAMにエラー情報を表示して停止                            */
/*  タイマ: フレームカウンタの管理                                          */
/* ======================================================================== */

#include "idt.h"
#include "io.h"
#include "paging.h"
#include "memmap.h"
#include "kprintf.h"
#include "kstring.h"

#define EXC_TRACE_MAX          8
#define EXC_STACK_WORDS        8
#define EXC_SCREEN_CLEAR_ROWS 15
#define EXC_SCREEN_TRACE_END  20
#define EXC_SCREEN_STATUS_ROW (TVRAM_ROWS - 1)
#define EXC_SCREEN_PF_DUMP_ROW 15
#define EXC_SCREEN_PF_DUMP_END 21
/* Direct TVRAM legacy visible/blink/reverse attribute; preserve the byte. */
#define EXC_ATTR_LEGACY        0x07
#define EXC_HEX_PREFIX_COLS    2

/* Runtime records remain resident even when a failed module is quarantined. */
static int sqlite_fault_address(u32 eip)
{
    for (u32 i = 0; i < MODULE_RECORD_CAP; i++) {
        const struct module_record *r = &module_records[i];
        if (r->module_id == MODULE_ID_SQLITE && r->state != MODULE_ABSENT &&
            eip >= r->text && eip - r->text < r->text_size)
            return 1;
    }
    return 0;
}

/* exec フォルト復帰用 (exec.c で定義) */
extern volatile int exec_nest_level;
extern void exec_fault_recover(void);
/* CPL=3 (リング3) 由来のフォールトでアプリを kill する (exec.c, v2 M1e)。
 * fault_kill_count をインクリメントし master CR3 復帰 → AS 破棄 → longjmp。
 * この関数は戻らない。 */
extern void ring3_fault_kill(void);
/* ring3 syscall (wrap) 実行中フラグ (exec.c, v2 M2e)。立っている間の CPL=0
 * フォールトは「ring3 アプリ由来 (KAPI ポインタ deref 等)」とみなし kill する。 */
extern volatile int ring3_in_syscall;
/* カーネルが WM (gshell) のコードへ入っている深さ (exec.c、2026-09-26)。
 * kill の行に "(in WM)" を足すためだけに読む — 帰属 (kill するか) は変えない。
 * WM の中で落ちるとアプリの kill として畳まれる (POLICY_DEBUG §4-61) ので、
 * ここと ring3_wm_fault_count (exec.c) が「WM の中だった」を残す唯一の印。 */
extern volatile int ring3_wm_depth;

#include "serial.h"
#include "tvram.h"

/* シリアルポートにもログを出力するためのヘルパー */
static void sputs(const char *str)
{
    serial_puts_polled(str);
}

/* 注: この下の 16 進変換 (sput_hex32/tvram_put_hex32) は kutoa_hex に
 * 統一しない。例外ハンドラはコンソール状態が壊れていても動く必要があり、
 * 固定 8 桁で TVRAM/シリアルへ直接書く自己完結実装を意図的に残している。 */
static void sput_hex32(u32 val)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[EXC_HEX_PREFIX_COLS + HEX32_DIGITS + 1];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = HEX32_DIGITS - 1; i >= 0; i--) {
        buf[i+EXC_HEX_PREFIX_COLS] = hex[val & ((1U << HEX_NIBBLE_BITS) - 1)];
        val >>= HEX_NIBBLE_BITS;
    }
    buf[EXC_HEX_PREFIX_COLS + HEX32_DIGITS] = '\0';
    serial_puts_polled(buf);
}

/* テキストVRAM直接アクセス (ベアメタル) — tvram.h のマクロを使用 */
#define TVRAM_CHAR  ((volatile u16 *)P2V_IO_CONST(TVRAM_BASE))
#define TVRAM_ATRP  ((volatile u16 *)P2V_IO_CONST(TVRAM_ATTR))

/* ------------------------------------------------------------------------ */
/*  tvram_puts_at — テキストVRAMに文字列を直接書き込み                     */
/* ------------------------------------------------------------------------ */
static void tvram_puts_at(int row, int col, const char *str, u8 attr)
{
    int pos = row * TVRAM_COLS + col;
    while (*str) {
        TVRAM_CHAR[pos] = (u16)(u8)*str;
        TVRAM_ATRP[pos] = (u16)attr;
        str++;
        pos++;
    }
}

/* ------------------------------------------------------------------------ */
/*  tvram_put_hex32 — 32ビット値を16進数で表示                              */
/* ------------------------------------------------------------------------ */
static void tvram_put_hex32(int row, int col, u32 val, u8 attr)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[HEX32_DIGITS + 1];
    int i;
    for (i = HEX32_DIGITS - 1; i >= 0; i--) {
        buf[i] = hex[val & ((1U << HEX_NIBBLE_BITS) - 1)];
        val >>= HEX_NIBBLE_BITS;
    }
    buf[HEX32_DIGITS] = '\0';
    tvram_puts_at(row, col, buf, attr);
}

/* SQLite faults cannot unwind or enter app resource cleanup. */
void sqlite_engine_fault(u32 vector, u32 eip, u32 cr2)
{
    enum { SQLITE_FAULT_COL_VECTOR = 20, SQLITE_FAULT_COL_EIP = 33, SQLITE_FAULT_COL_CR2 = 46 };
    _disable();
    sputs("\n[SQLite] fault vector="); sput_hex32(vector);
    sputs(" EIP="); sput_hex32(eip);
    sputs(" CR2="); sput_hex32(cr2); sputs("\n");
    tvram_puts_at(0, 0, "SQLite fault vector=         EIP=         CR2=", TATTR_RED);
    tvram_put_hex32(0, SQLITE_FAULT_COL_VECTOR, vector, TATTR_RED);
    tvram_put_hex32(0, SQLITE_FAULT_COL_EIP, eip, TATTR_RED);
    tvram_put_hex32(0, SQLITE_FAULT_COL_CR2, cr2, TATTR_RED);
    for (;;) _stop();
}

/* A font fault in WM is a kernel failure, even while an app owns the call. */
void font_guard_fault(u32 vector, u32 eip, u32 address)
{
    _disable();
    sputs("\n[font] fault vector="); sput_hex32(vector);
    sputs(" EIP="); sput_hex32(eip);
    sputs(" addr="); sput_hex32(address); sputs("\n");
    tvram_puts_at(0, 0, "Font fault - system halted", TATTR_RED);
    for (;;) _stop();
}

/* ====================================================================== */
/*  例外ダンプ共通ヘルパー (exception_handler / page_fault_handler 共用) */
/* ====================================================================== */

/* レジスタダンプ (PUSHAD保存順序: EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX) */
static void exc_dump_regs(int row, u32 *regs)
{
    tvram_puts_at(row, 0, " EAX=", TATTR_YELLOW);
    tvram_put_hex32(row, 5, regs[ISR_REG_EAX], TATTR_YELLOW);
    tvram_puts_at(row, 14, " EBX=", TATTR_YELLOW);
    tvram_put_hex32(row, 19, regs[ISR_REG_EBX], TATTR_YELLOW);
    tvram_puts_at(row, 28, " ECX=", TATTR_YELLOW);
    tvram_put_hex32(row, 33, regs[ISR_REG_ECX], TATTR_YELLOW);

    tvram_puts_at(row+1, 0, " EDX=", TATTR_YELLOW);
    tvram_put_hex32(row+1, 5, regs[ISR_REG_EDX], TATTR_YELLOW);
    tvram_puts_at(row+1, 14, " ESI=", TATTR_YELLOW);
    tvram_put_hex32(row+1, 19, regs[ISR_REG_ESI], TATTR_YELLOW);
    tvram_puts_at(row+1, 28, " EDI=", TATTR_YELLOW);
    tvram_put_hex32(row+1, 33, regs[ISR_REG_EDI], TATTR_YELLOW);

    tvram_puts_at(row+2, 0, " EBP=", TATTR_YELLOW);
    tvram_put_hex32(row+2, 5, regs[ISR_REG_EBP], TATTR_YELLOW);
    tvram_puts_at(row+2, 14, " ESP=", TATTR_YELLOW);
    tvram_put_hex32(row+2, 19, regs[ISR_REG_ESP], TATTR_YELLOW);
}

/* レジスタダンプ (シリアル出力付き) */
static void exc_dump_regs_serial(int row, u32 *regs)
{
    exc_dump_regs(row, regs);
    sputs("EAX="); sput_hex32(regs[ISR_REG_EAX]);
    sputs(" EBX="); sput_hex32(regs[ISR_REG_EBX]);
    sputs(" ECX="); sput_hex32(regs[ISR_REG_ECX]); sputs("\n");
    sputs("EDX="); sput_hex32(regs[ISR_REG_EDX]);
    sputs(" ESI="); sput_hex32(regs[ISR_REG_ESI]);
    sputs(" EDI="); sput_hex32(regs[ISR_REG_EDI]); sputs("\n");
    sputs("EBP="); sput_hex32(regs[ISR_REG_EBP]);
    sputs(" ESP="); sput_hex32(regs[ISR_REG_ESP]); sputs("\n");
}

/* EBPチェーンによるスタックトレース (最大8フレーム) */
static void exc_dump_stack_trace(int row, u32 *regs, int max_row, int serial)
{
    u32 ebp = regs[ISR_REG_EBP];
    int frame;
    int tr = row + 1;

    tvram_puts_at(row, 0, "---- Stack Trace ----", TATTR_WHITE);
    if (serial) sputs("---- Stack Trace ----\n");

    for (frame = 0; frame < EXC_TRACE_MAX && tr < max_row; frame++) {
        u32 ret_addr;
        u32 prev_ebp;
        /* 妥当なスタックの範囲か。**下限は 1MB** — 低位 1MB は V86 ゲストに
         * 明け渡す領域なので、そこを指す EBP はフレームチェーンとして
         * 信用しない。というのが元々の判定理由で、値には MEM_KSTACK_BASE を
         * 使っていた。2026-09-17 (決裁 D1) にカーネルスタックが 0x2FC000 へ
         * 移ったので、そのまま使うと **SQLite の代替スタック
         * (0x2BD000-0x2DCFFF) の上のフレームが辿れなくなる**。
         * 理由どおりの定数 (MEM_1MB) に直した。 */
        if (ebp < MEM_1MB || ebp >= MEM_TRACE_STACK_LIMIT) break;
        if (!paging_is_present(ebp) ||
            !paging_is_present(ebp + sizeof(u32))) break;
        prev_ebp = *(u32 *)ebp;
        ret_addr = *(u32 *)(ebp + sizeof(u32));
        tvram_puts_at(tr, 0, " #", TATTR_CYAN);
        tvram_put_hex32(tr, 2, (u32)frame, TATTR_CYAN);
        tvram_puts_at(tr, 10, " ret=", TATTR_CYAN);
        tvram_put_hex32(tr, 15, ret_addr, TATTR_CYAN);
        tvram_puts_at(tr, 26, " ebp=", TATTR_CYAN);
        tvram_put_hex32(tr, 31, prev_ebp, TATTR_CYAN);
        if (serial) {
            sputs("#"); sput_hex32((u32)frame);
            sputs(" ret="); sput_hex32(ret_addr);
            sputs(" ebp="); sput_hex32(prev_ebp); sputs("\n");
        }
        tr++;
        ebp = prev_ebp;
    }
}

/* ESPからのスタックダンプ */
static void exc_dump_stack(int row, u32 *regs, int max_row, int serial)
{
    u32 esp = regs[ISR_REG_ESP];
    int wi;
    int dr = row;

    tvram_puts_at(dr, 0, "---- Stack Dump (ESP) ----", TATTR_WHITE);
    if (serial) sputs("---- Stack Dump (ESP) ----\n");
    dr++;

    for (wi = 0; wi < EXC_STACK_WORDS && dr < max_row; wi += 2) {
        u32 addr0 = esp + (u32)wi * sizeof(u32);
        u32 addr1 = esp + (u32)(wi + 1) * sizeof(u32);
        if (paging_is_present(addr0)) {
            tvram_put_hex32(dr, 0, addr0, EXC_ATTR_LEGACY);
            tvram_puts_at(dr, 9, ":", EXC_ATTR_LEGACY);
            tvram_put_hex32(dr, 10, *(u32 *)addr0, TATTR_WHITE);
            if (serial) { sput_hex32(addr0); sputs(":"); sput_hex32(*(u32 *)addr0); }
        }
        if (paging_is_present(addr1)) {
            tvram_puts_at(dr, 19, " ", EXC_ATTR_LEGACY);
            tvram_put_hex32(dr, 20, addr1, EXC_ATTR_LEGACY);
            tvram_puts_at(dr, 29, ":", EXC_ATTR_LEGACY);
            tvram_put_hex32(dr, 30, *(u32 *)addr1, TATTR_WHITE);
            if (serial) { sputs("  "); sput_hex32(addr1); sputs(":"); sput_hex32(*(u32 *)addr1); }
        }
        if (serial) sputs("\n");
        dr++;
    }
}

/* ======================================================================== */
/*  exception_handler — CPU例外ハンドラ                                    */
/*  テキストVRAMにエラー情報を表示して停止する                              */
/* ======================================================================== */

/* 例外名テーブル */
static const char *exception_names[] = {
    "#DE Divide Error",        /* 0 */
    "#DB Debug",               /* 1 */
    "NMI",                     /* 2 */
    "#BP Breakpoint",          /* 3 */
    "#OF Overflow",            /* 4 */
    "#BR Bound Range",         /* 5 */
    "#UD Invalid Opcode",      /* 6 */
    "#NM No Math",             /* 7 */
    "#DF Double Fault",        /* 8 */
    "Coprocessor Overrun",     /* 9 */
    "#TS Invalid TSS",         /* 10 */
    "#NP Segment Not Present", /* 11 */
    "#SS Stack Fault",         /* 12 */
    "#GP General Protection",  /* 13 */
    "#PF Page Fault",          /* 14 */
    "Reserved (15)",           /* 15 */
    "#MF x87 FP Error",        /* 16 */
    "#AC Alignment Check",     /* 17 */
    "#MC Machine Check",       /* 18 */
    "#XM SIMD FP Exception",   /* 19 */
};

#define EXCEPTION_NAME_COUNT \
    ((u32)(sizeof(exception_names) / sizeof(exception_names[0])))

void exception_handler(u32 error_code, u32 vector, u32 fault_eip,
                       u32 *regs)
{
    const char *name;
    int row = 0;

    _disable();
    if (font_guard_active) font_guard_fault(vector, fault_eip, 0);
    if (sqlite_engine_active) {
        u32 cr2 = 0;
        if (vector == EXC_PAGE_FAULT)
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        sqlite_engine_fault(vector, fault_eip, cr2);
    }
#ifdef OS32_R1_FIXTURE
    r1_fixture_exception(vector);
#endif

    /* ---- CPL=3 (リング3) 由来のフォールトはアプリだけ kill (v2 M1e/V4) ----
     * フォールトフレームの CS は PUSHAD 配列の上に CPU が積んだもの。
     * isr_common のフレーム: regs[8]=vector, regs[9]=error_code,
     * regs[10]=EIP(=fault_eip), regs[11]=CS, regs[12]=EFLAGS。
     * CS.RPL==3 なら CPL=3 由来 = リング3 アプリのフォールト。カーネル
     * (CPL=0) 自身のフォールトは CS.RPL==0 なので従来どおり停止させる
     * (ここを取り違えるとカーネルのバグを握り潰す)。ring3_fault_kill は
     * 資源を触らず longjmp し、着地点で master CR3 / IF=1 に戻して回収。 */
    if ((regs[ISR_COMMON_CS] & X86_SEL_RPL_MASK) == X86_RPL_USER || ring3_in_syscall) {
        sputs("\n[ring3] exception (CPL=3 / syscall) vec=");
        sput_hex32(vector);
        sputs(" EIP="); sput_hex32(fault_eip);
        if (ring3_wm_depth > 0) sputs(" (in WM)");
        sputs(" -> kill app\n");
        ring3_fault_kill();     /* 戻らない */
    }

    /* 画面上部クリア */
    {
        int r;
        for (r = 0; r < EXC_SCREEN_CLEAR_ROWS; r++) {
            tvram_puts_at(r, 0,
                "                                        "
                "                                        ", EXC_ATTR_LEGACY);
        }
    }

    if (vector < EXCEPTION_NAME_COUNT)
        name = exception_names[vector];
    else
        name = "Unknown Exception";

    /* 例外情報 */
    tvram_puts_at(row,   0, "==== EXCEPTION ====", TATTR_RED);
    tvram_puts_at(row+1, 0, " Type: ", TATTR_RED);
    tvram_puts_at(row+1, 7, name, TATTR_RED);
    tvram_puts_at(row+2, 0, " Vec: 0x", TATTR_WHITE);
    tvram_put_hex32(row+2, 8, vector, TATTR_WHITE);
    tvram_puts_at(row+2, 17, " ErrC: 0x", TATTR_WHITE);
    tvram_put_hex32(row+2, 26, error_code, TATTR_WHITE);
    tvram_puts_at(row+3, 0, " EIP: 0x", TATTR_WHITE);
    tvram_put_hex32(row+3, 8, fault_eip, TATTR_WHITE);

    /* EIPの領域判定 */
    {
        if (sqlite_fault_address(fault_eip)) {
            tvram_puts_at(row+3, 17, "[sqlite]", TATTR_CYAN);
        } else if (fault_eip >= KERNEL_LOAD_ADDR &&
            fault_eip < (u32)&__kernel_resident_end) {
            tvram_puts_at(row+3, 17, "[.text]", TATTR_CYAN);
        } else {
            tvram_puts_at(row+3, 17, "[OUT OF CODE!]", TATTR_YELLOW);
        }
    }

    /* レジスタダンプ */
    exc_dump_regs(row+4, regs);

    /* スタックトレース */
    exc_dump_stack_trace(row+7, regs, EXC_SCREEN_TRACE_END, 0);

    /* スタックダンプ */
    exc_dump_stack(EXC_SCREEN_TRACE_END, regs, TVRAM_ROWS, 0);

    /* exec実行中なら復帰、それ以外はシステム停止 */
    if (exec_nest_level > 0) {
        tvram_puts_at(EXC_SCREEN_STATUS_ROW, 0, " >> Returning to shell...               ", TATTR_CYAN);
        /* ここで _enable() してはいけない。IRQ ハンドラ内のフォルトだと
         * EOI 前に割り込みを開けて longjmp することになり、その IRQ が
         * 恒久ブロックされる。IF は exec_run の setjmp 復帰側で戻す。 */
        exec_fault_recover();
    }

    tvram_puts_at(EXC_SCREEN_STATUS_ROW, 0, " System halted.                         ", TATTR_RED);
    for (;;) { /* hlt */ }
}

/* ======================================================================== */
/*  page_fault_handler — ページフォルト (#PF) 専用ハンドラ                  */
/*                                                                          */
/*  error_code ビット:                                                      */
/*    bit 0: P   — 0=Not-Present, 1=Protection violation                   */
/*    bit 1: W/R — 0=Read, 1=Write                                         */
/*    bit 2: U/S — 0=Supervisor, 1=User                                    */
/*  fault_addr: CR2 (障害が発生した仮想アドレス)                            */
/* ======================================================================== */
void page_fault_handler(u32 error_code, u32 fault_addr, u32 fault_eip, u32 *regs)
{
    int row = 0;  /* 画面最上部から表示 (最大限の情報量) */

    _disable();
    if (font_guard_active) font_guard_fault(EXC_PAGE_FAULT, fault_eip, fault_addr);
    if (sqlite_engine_active) sqlite_engine_fault(EXC_PAGE_FAULT, fault_eip, fault_addr);

    /* ---- CPL=3 (リング3) 由来の #PF はアプリだけ kill (v2 M1e/V4) ----
     * #PF フレーム (isr_stub_14 .not_v86): regs[8]=error_code,
     * regs[9]=EIP(=fault_eip), regs[10]=CS, regs[11]=EFLAGS。
     * CS.RPL==3 なら CPL=3 由来。カーネル帯域 (PTE supervisor) への書き込みや
     * 未マップ読みで #PF したリング3 アプリを、カーネルを巻き込まず畳む。
     * カーネル自身の #PF は CS.RPL==0 なので従来どおり停止 (赤画面)。 */
    if ((regs[ISR_PF_CS] & X86_SEL_RPL_MASK) == X86_RPL_USER || ring3_in_syscall) {
        sputs("\n[ring3] #PF (CPL=3 / syscall) addr=");
        sput_hex32(fault_addr);
        sputs(" err="); sput_hex32(error_code);
        sputs(" EIP="); sput_hex32(fault_eip);
        /* 共有ライブラリ帯域 (K3) の切り分け。.text/.rodata は read-only +
         * USER なので、ここへの書き込みは保護違反 (error_code bit0=1,
         * bit1=1) になる。「番地がライブラリ帯」と出れば、アプリのポインタ
         * 計算ミスかライブラリ自身が定数域へ書いたかがすぐ分かる。 */
        if (fault_addr >= MEM_SHLIB_BASE && fault_addr < MEM_SHLIB_END) {
            sputs((error_code & PF_ERR_WRITE) ? " [shlib band, WRITE]" : " [shlib band, READ]");
        }
        if (ring3_wm_depth > 0) sputs(" (in WM)");
        sputs(" -> kill app\n");
        ring3_fault_kill();     /* 戻らない */
    }

    /* 画面上部をクリア (15行のみ — row 15以降のテスト出力を保持) */
    {
        int r;
        for (r = 0; r < EXC_SCREEN_CLEAR_ROWS; r++) {
            tvram_puts_at(r, 0,
                "                                        "
                "                                        ", EXC_ATTR_LEGACY);
        }
    }

    tvram_puts_at(row,   0, "==== PAGE FAULT (#PF) ====", TATTR_RED);
    sputs("\n\n==== PAGE FAULT (#PF) ====\n");
    
    tvram_puts_at(row+1, 0, " Addr: 0x", TATTR_WHITE);
    tvram_put_hex32(row+1, 9, fault_addr, TATTR_WHITE);
    sputs("Addr: "); sput_hex32(fault_addr);
    
    tvram_puts_at(row+1, 18, " ErrC: 0x", TATTR_WHITE);
    tvram_put_hex32(row+1, 27, error_code, TATTR_WHITE);
    sputs(" ErrC: "); sput_hex32(error_code); sputs("\n");
    
    tvram_puts_at(row+2, 0, " EIP:  0x", TATTR_WHITE);
    tvram_put_hex32(row+2, 9, fault_eip, TATTR_WHITE);
    sputs("EIP:  "); sput_hex32(fault_eip);

    /* EIPがコードセクション内かチェック */
    {
        if (sqlite_fault_address(fault_eip)) {
            tvram_puts_at(row+2, 18, "[sqlite]", TATTR_CYAN);
            sputs(" [sqlite]\n");
        } else if (fault_eip >= KERNEL_LOAD_ADDR &&
            fault_eip < (u32)&__kernel_resident_end) {
            tvram_puts_at(row+2, 18, "[.text]", TATTR_CYAN);
            sputs(" [.text]\n");
        } else {
            tvram_puts_at(row+2, 18, "[OUT OF CODE!]", TATTR_YELLOW);
            sputs(" [OUT OF CODE!]\n");
        }
    }

    /* 原因 */
    tvram_puts_at(row+3, 0, " Cause: ", TATTR_WHITE);
    sputs("Cause: ");
    if (error_code & PF_ERR_WRITE) {
        tvram_puts_at(row+3, 8, "WRITE ", TATTR_YELLOW);
        sputs("WRITE ");
    } else {
        tvram_puts_at(row+3, 8, "READ  ", TATTR_YELLOW);
        sputs("READ  ");
    }
    if (error_code & 0x01) {
        tvram_puts_at(row+3, 14, "R/O page", TATTR_YELLOW);
        sputs("R/O page\n");
    } else {
        tvram_puts_at(row+3, 14, "Not-Present", TATTR_YELLOW);
        sputs("Not-Present\n");
    }

    /* レジスタダンプ */
    exc_dump_regs_serial(row+4, regs);

    /* スタックトレース */
    exc_dump_stack_trace(row+7, regs, EXC_SCREEN_STATUS_ROW, 1);

    /* スタックダンプ */
    exc_dump_stack(row+EXC_SCREEN_PF_DUMP_ROW, regs, EXC_SCREEN_PF_DUMP_END, 1);

    /* exec実行中なら復帰、それ以外はシステム停止 */
    if (exec_nest_level > 0) {
        tvram_puts_at(EXC_SCREEN_STATUS_ROW, 0, " >> Returning to shell...               ", TATTR_CYAN);
        /* ここで _enable() してはいけない。IRQ ハンドラ内のフォルトだと
         * EOI 前に割り込みを開けて longjmp することになり、その IRQ が
         * 恒久ブロックされる。IF は exec_run の setjmp 復帰側で戻す。 */
        exec_fault_recover();
    }

    tvram_puts_at(EXC_SCREEN_STATUS_ROW, 0, " System halted.                         ", TATTR_RED);
    for (;;) { /* hlt */ }
}

/* ======================================================================== */
/*  isr_unexpected_report — 誰も受けなかった IRQ の**診断だけ**             */
/*  (kernel/irq.c の irq_finish から呼ばれる)                               */
/*                                                                          */
/*  **EOI は送らない。** 動的経路で EOI を送るのは irq_finish() の 1 か所    */
/*  だけで、IRQ15 のスプリアス検査 (スレーブ ISR の IR7) もそちらに移した    */
/*  (票 TASK_HAL_WIRING §1-1 の 5)。2 か所から EOI が出ると、片方が          */
/*  スプリアスを見落としてスレーブの ISR ビットを消してしまう。             */
/*                                                                          */
/*  元は isr_unexpected_irq() で、IRQ_UNEXP スタブから EOI ごと呼ばれていた。 */
/* ======================================================================== */
void isr_unexpected_report(u32 irq)
{
    static u16 reported = 0;

    if (irq < IRQ_LINE_MAX && !(reported & (u16)(1u << irq))) {
        reported |= (u16)(1u << irq);
        kprintf(ATTR_YELLOW, "[isr] unclaimed IRQ%d (no registrant claimed it)\n", (int)irq);
    }
}

/* ======================================================================== */
/*  timer_handler — タイマ割り込みハンドラ (IRQ0)                           */
/*  tick_countのインクリメントはASMスタブで行う                             */
/* ======================================================================== */
#include "tick.h"

void timer_handler(void)
{
#ifdef OS32_R1_FIXTURE
    r1_fixture_timer();
#endif
    tick_dispatch();
}
