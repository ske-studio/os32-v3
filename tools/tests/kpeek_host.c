/* ======================================================================== */
/*  kpeek_host.c — kdebug の peek 本体 (kdebug/kpeek.c・kpeek_allow.c) の    */
/*  ホスト試験 (TASK_KPEEK §4)                                              */
/*                                                                          */
/*  実物の kpeek.c と kpeek_allow.c を 1 行も写さずに #include し、import   */
/*  (paging_* / pci_* / irq_save / irq_restore) と読み (kpeek_load) だけを  */
/*  贋物にする (KPEEK_HOST)。贋の PT は「番地 → PTE の生値」の表で、窓の     */
/*  張り替えと戻しをそのまま記録する。呼ばれた順は ev[] に 1 文字ずつ積む:  */
/*  S = irq_save、r = pte_raw、M = map_phys、L = 読み、R = pte_restore、     */
/*  U = irq_restore。                                                       */
/*                                                                          */
/*  python3 -B tools/tests/test_kpeek.py [--mutate]                          */
/* ======================================================================== */
#define KPEEK_HOST 1
#include "types.h"
#include "os32_kapi_shared.h"
#include "memmap.h"
#include "pc98.h"
#include "paging.h"
#include "drivers/pci.h"

/* ---- 出力と終了 (libc なし、ILP32 の int 0x80 — cirrus_win_host.c と同じ) ---- */
static void output(const char *s)
{
    unsigned int len = 0;
    while (s[len]) len++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(len) : "memory");
}
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
static void out_dec(u32 v)
{
    char b[12];
    int i = 11;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10U); v /= 10U; } while (v);
    output(b + i);
}
static int fails;
#define CHECK(c) do { if (!(c)) { output("FAIL line "); out_dec(__LINE__); \
    output(": " #c "\n"); fails++; } } while (0)

void *memset(void *d, int v, unsigned int n);
void *memcpy(void *d, const void *s, unsigned int n);
void *memset(void *d, int v, unsigned int n)
{ u8 *p = d; while (n--) *p++ = (u8)v; return d; }
void *memcpy(void *d, const void *s, unsigned int n)
{ u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static int streq(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int has(const char *a, char c) { while (*a) if (*a++ == c) return 1; return 0; }

/* ---- 呼ばれた順 ---------------------------------------------------- */
static char ev[64];
static u32 nev;
static void note(char c) { if (nev < sizeof(ev) - 1) { ev[nev++] = c; ev[nev] = 0; } }

/* ---- 贋の PT ------------------------------------------------------- */
#define FAKE_PTES 256
static struct { u32 va; u32 raw; } pt[FAKE_PTES];
static u32 npt;
static int pg_on = 1;

static int pt_find(u32 va)
{
    u32 i;
    for (i = 0; i < npt; i++) if (pt[i].va == (va & ~0xFFFU)) return (int)i;
    return -1;
}
static void pt_set(u32 va, u32 raw)
{
    int i = pt_find(va);
    if (i < 0) { if (npt >= FAKE_PTES) { CHECK(!"fake PT full"); return; } i = (int)npt++; pt[i].va = va & ~0xFFFU; }
    pt[i].raw = raw;
}
static u32 pt_get(u32 va) { int i = pt_find(va); return i < 0 ? 0 : pt[i].raw; }

int paging_is_present(u32 va) { return pg_on ? (pt_get(va) & PTE_PRESENT) != 0 : 1; }
u32 paging_pte_flags(u32 va) { return pg_on ? (pt_get(va) & 0xFFFU) : 0; }
u32 paging_pte_raw(u32 va) { note('r'); return pt_get(va); }

static int map_fail;
static u32 map_flags, map_calls, restore_calls;
int paging_map_phys(u32 va, u32 pa, u32 n, u32 flags)
{
    u32 i;
    note('M');
    map_calls++;
    map_flags = flags;
    if (map_fail) return -1;
    for (i = 0; i < n; i++) pt_set(va + i * PAGE_SIZE, (pa + i * PAGE_SIZE) | flags);
    return 0;
}
int paging_pte_restore(u32 va, u32 raw) { note('R'); restore_calls++; pt_set(va, raw); return 0; }

static int irq_depth;
static unsigned int irq_save(void) { note('S'); irq_depth++; return 0x5A5AU; }
static void irq_restore(unsigned int f) { note('U'); CHECK(f == 0x5A5AU); irq_depth--; }

/* ---- 読み: 番地から決まる値。窓なら「その VA が今どの物理を指すか」を
 *      贋の PT で引き、張ってから読んでいることを確かめる ---- */
static u32 load_calls, load_addr, load_width, load_len, load_irq_depth;
static u32 load_phys;            /* 窓の読みで、読んだ VA が指していた物理 */
static u32 load_pte;
static u8 val_at(u32 a) { return (u8)((a * 7U) ^ (a >> 8) ^ 0x5AU); }
static void kpeek_load(u32 addr, u32 width, u32 len, u8 *out)
{
    u32 i, raw = pt_get(addr);
    note('L');
    load_calls++;
    load_addr = addr; load_width = width; load_len = len;
    load_irq_depth = (u32)irq_depth;
    load_pte = raw;
    load_phys = (raw & ~0xFFFU) | (addr & 0xFFFU);
    for (i = 0; i < len; i++) {
        u32 pa = addr + i;
        if (addr >= MEM_DEVICE_APERTURE_BASE)
            pa = (pt_get(addr + i) & ~0xFFFU) | ((addr + i) & 0xFFFU);
        out[i] = val_at(pa);
    }
}

/* ---- 贋の PCI ------------------------------------------------------ */
static struct pci_dev nv;
static int nv_present;
static u32 pci_get_calls;
int pci_find(u16 vendor, u16 device)
{
    return (nv_present && vendor == nv.vendor && device == nv.device) ? 3 : -1;
}
int pci_get(u32 idx, void *out)
{
    pci_get_calls++;
    if (idx != 3 || !nv_present) return -1;
    memcpy(out, &nv, sizeof(nv));
    return 0;
}

#include "../../kdebug/kpeek_allow.c"
#include "../../kdebug/kpeek.c"

/* ---- 道具 ---------------------------------------------------------- */
#define NV_BAR0 0x24000000U
static KdebugPeek rq;

static void reset(void)
{
    memset(pt, 0, sizeof(pt)); npt = 0; pg_on = 1;
    nev = 0; ev[0] = 0;
    map_fail = 0; map_flags = 0; map_calls = 0; restore_calls = 0;
    irq_depth = 0; load_calls = 0; load_phys = 0; load_pte = 0;
    memset(&nv, 0, sizeof(nv));
    nv.vendor = KPEEK_NV_VENDOR; nv.device = KPEEK_NV44A_DEVICE;
    nv.bar[0] = NV_BAR0;                  /* メモリ BAR、32bit、非 prefetch */
    nv.command = PCI_CMD_MEM_ENABLE;
    nv_present = 1;
}

static void ram(u32 lo, u32 hi, u32 flags)    /* [lo, hi) を恒等で present */
{
    u32 a;
    for (a = lo; a < hi; a += PAGE_SIZE) pt_set(a, a | PTE_PRESENT | flags);
}

static u32 call(u32 space, u32 addr, u32 width, u32 len)
{
    i32 rc;
    memset(&rq, 0, sizeof(rq));
    memset(rq.data, 0xEE, sizeof(rq.data));
    rq.space = space; rq.addr = addr; rq.width = width; rq.len = len;
    rq.status = 0xFFFFFFFFU; rq.fail_addr = 0xFFFFFFFFU;
    rc = kpeek_call(KDEBUG_OP_PEEK, &rq, sizeof(rq));
    CHECK(rc == 0);
    return rq.status;
}

static int data_untouched(void)
{
    u32 i;
    for (i = 0; i < sizeof(rq.data); i++) if (rq.data[i] != 0xEE) return 0;
    return 1;
}

static int data_is(u32 pa, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++) if (rq.data[i] != val_at(pa + i)) return 0;
    return rq.data[len] == 0xEE;
}

/* ---- 場合 ---------------------------------------------------------- */
static void t_entry(void)
{
    KdebugPeek q;
    reset();
    memset(&q, 0x11, sizeof(q));
    CHECK(kpeek_call(KDEBUG_OP_PEEK, &q, sizeof(q) - 1) == OS32_ERR_INVAL);
    CHECK(q.status == 0x11111111U);                 /* 1 バイトも書かない */
    CHECK(kpeek_call(KDEBUG_OP_PEEK, 0, sizeof(q)) == OS32_ERR_INVAL);
    CHECK(kpeek_call(KDEBUG_OP_PEEK_LAN_PROBE, &q, sizeof(q)) == OS32_ERR_NOSYS);
    CHECK(kpeek_call(99, &q, sizeof(q)) == OS32_ERR_NOSYS);
    CHECK(q.status == 0x11111111U);
}

static void t_args(void)
{
    reset();
    ram(0x100000, 0x110000, 0);
    CHECK(call(0, 0x100000, 3, 16) == KDEBUG_PEEK_WIDTH);
    CHECK(rq.fail_addr == 0x100000 && data_untouched());
    CHECK(call(0, 0x100000, 0, 16) == KDEBUG_PEEK_WIDTH);
    CHECK(call(0, 0x100000, 8, 16) == KDEBUG_PEEK_WIDTH);
    CHECK(call(0, 0x100000, 1, 0) == KDEBUG_PEEK_ARG);
    CHECK(call(0, 0x100000, 1, KDEBUG_PEEK_CHUNK + 1) == KDEBUG_PEEK_ARG);
    CHECK(call(0, 0x100000, 1, KDEBUG_PEEK_CHUNK) == KDEBUG_PEEK_OK);   /* 上限ちょうど */
    CHECK(rq.data[0] == val_at(0x100000) &&
          rq.data[KDEBUG_PEEK_CHUNK - 1] == val_at(0x100000 + KDEBUG_PEEK_CHUNK - 1));
    CHECK(call(0, 0x100002, 4, 16) == KDEBUG_PEEK_ARG);             /* 番地が幅の倍数でない */
    CHECK(call(0, 0x100000, 4, 6) == KDEBUG_PEEK_ARG);              /* 長さが幅の倍数でない */
    CHECK(call(0, 0x100001, 2, 2) == KDEBUG_PEEK_ARG);
    CHECK(call(0, 0x100002, 2, 2) == KDEBUG_PEEK_OK);
    CHECK(call(0, 0xFFFFFF00U, 1, 0x200) == KDEBUG_PEEK_ARG);       /* 4GB を溢れる */
    CHECK(call(2, 0x100000, 1, 16) == KDEBUG_PEEK_ARG);             /* 知らない空間 */
    CHECK(data_untouched());
    memset(&rq, 0, sizeof(rq));
    rq.space = 0; rq.addr = 0x100000; rq.width = 1; rq.len = 16; rq.flags = KDEBUG_PEEK_F_LAN;
    CHECK(kpeek_call(KDEBUG_OP_PEEK, &rq, sizeof(rq)) == 0 && rq.status == KDEBUG_PEEK_ARG);
    CHECK(load_calls == 2);                          /* OK の 2 回だけ読んだ */
}

static void t_ram(void)
{
    reset();
    ram(0x100000, 0x103000, 0);
    CHECK(call(0, 0x100F00, 1, 0x200) == KDEBUG_PEEK_OK);           /* 2 ページにまたがる */
    CHECK(rq.fail_addr == 0 && data_is(0x100F00, 0x200));
    CHECK(load_addr == 0x100F00 && load_width == 1 && load_len == 0x200);
    CHECK(!has(ev, 'S') && !has(ev, 'M'));   /* RAM は窓を触らない */
    CHECK(call(0, 0x101000, 4, 0x400) == KDEBUG_PEEK_OK && load_width == 4);
    CHECK(call(0, 0x101000, 2, 0x10) == KDEBUG_PEEK_OK && load_width == 2);

    /* NOMAP: 2 ページ目が無い → fail_addr はそのページの先頭、1 バイトも読まない */
    reset();
    ram(0x100000, 0x101000, 0);
    CHECK(call(0, 0x100F00, 1, 0x200) == KDEBUG_PEEK_NOMAP);
    CHECK(rq.fail_addr == 0x101000 && data_untouched() && load_calls == 0);
    /* 先頭ページが無い → fail_addr は要求の番地より前のページ先頭 */
    CHECK(call(0, 0x200010, 1, 0x10) == KDEBUG_PEEK_NOMAP && rq.fail_addr == 0x200000);
    CHECK(call(0, 0xF0000000U, 1, 0x40) == KDEBUG_PEEK_NOMAP);     /* lease 帯 */

    /* PCD / PWT のページは DENY (K1)。末尾のページだけでも断る */
    reset();
    ram(0x100000, 0x101000, 0);
    ram(0x101000, 0x102000, PTE_PCD);
    CHECK(call(0, 0x100F00, 1, 0x200) == KDEBUG_PEEK_DENY);
    CHECK(rq.fail_addr == 0x101000 && data_untouched() && load_calls == 0);
    reset();
    ram(0x100000, 0x101000, PTE_PWT);
    CHECK(call(0, 0x100000, 4, 0x10) == KDEBUG_PEEK_DENY && rq.fail_addr == 0x100000);
    reset();
    ram(MEM_DEVICE_APERTURE_BASE, MEM_DEVICE_APERTURE_BASE + 0x1000, PTE_PCD);
    CHECK(call(0, MEM_DEVICE_APERTURE_BASE, 1, 0x10) == KDEBUG_PEEK_DENY);   /* Xe10 窓 */

    /* [A0000h, E8000h) は PCD でなくても DENY (R2)。境界の前後は読める */
    reset();
    ram(0x9F000, 0xF0000, 0);
    CHECK(call(0, 0x9FFF0, 1, 0x10) == KDEBUG_PEEK_OK);
    CHECK(call(0, 0x9FFF0, 1, 0x20) == KDEBUG_PEEK_DENY && rq.fail_addr == 0xA0000);
    CHECK(call(0, 0xA8000, 1, 0x10) == KDEBUG_PEEK_DENY && rq.fail_addr == 0xA8000);
    CHECK(call(0, 0xE7FF0, 1, 0x10) == KDEBUG_PEEK_DENY && rq.fail_addr == 0xE7000);
    CHECK(call(0, 0xE8000, 1, 0x10) == KDEBUG_PEEK_OK && data_is(0xE8000, 0x10));
    CHECK(call(0, 0xE7FF0, 1, 0x20) == KDEBUG_PEEK_DENY && rq.fail_addr == 0xE7000);
}

/* MMIO: 許可リストと BAR の確かめ */
static void t_allow(void)
{
    reset();
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PMC_OFF, 4, 0x10) == KDEBUG_PEEK_OK);
    CHECK(data_is(NV_BAR0, 0x10));
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PMC_OFF + 0x3F0, 4, 0x10) == KDEBUG_PEEK_OK);  /* 行の末尾ちょうど */
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PMC_OFF + 0x3F0, 4, 0x20) == KDEBUG_PEEK_DENY); /* 行をはみ出す */
    CHECK(rq.fail_addr == NV_BAR0 + 0x3F0 && data_untouched());
    CHECK(call(1, NV_BAR0 + 0x000400, 4, 4) == KDEBUG_PEEK_DENY);  /* 行の間 */
    CHECK(call(1, NV_BAR0 - 4, 4, 4) == KDEBUG_PEEK_DENY);         /* BAR の前 */
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PMC_OFF, 1, 4) == KDEBUG_PEEK_DENY);   /* 幅外 */
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PMC_OFF, 2, 4) == KDEBUG_PEEK_DENY);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PCRTC1_OFF + 0xFFC, 4, 4) == KDEBUG_PEEK_OK);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PRAMDAC1_OFF, 4, 0x400) == KDEBUG_PEEK_OK);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PSTRAPS_OFF + 0xC, 4, 4) == KDEBUG_PEEK_OK);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PSTRAPS_OFF + 0x10, 4, 4) == KDEBUG_PEEK_DENY);
    /* VGA 別名は 1 バイトずつ。3C9 と隣り合わせの 2 バイトは断る */
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PRMVIO0_OFF + KPEEK_VGA_SRX, 1, 1) == KDEBUG_PEEK_OK);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PRMVIO0_OFF + KPEEK_VGA_SR, 1, 1) == KDEBUG_PEEK_OK);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PRMVIO0_OFF + KPEEK_VGA_SRX, 1, 2) == KDEBUG_PEEK_DENY);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PRMVIO0_OFF + 0x3C9, 1, 1) == KDEBUG_PEEK_DENY);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PRMCIO0_OFF + 0x3DA, 1, 1) == KDEBUG_PEEK_DENY);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PRMVIO0_OFF + 0x3DA, 1, 1) == KDEBUG_PEEK_DENY);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PRMCIO0_OFF + KPEEK_VGA_CR, 1, 1) == KDEBUG_PEEK_OK);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PRMCIO0_OFF + KPEEK_VGA_CRX, 4, 4) == KDEBUG_PEEK_DENY);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PROM_OFF + 0x1FC00, 1, 0x400) == KDEBUG_PEEK_OK);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PROM_OFF, 4, 4) == KDEBUG_PEEK_DENY);

    /* BAR の 3 条件 (K2): I/O BAR・未割当・Memory 無効 */
    reset(); nv.bar[0] = NV_BAR0 | 1U;
    CHECK(call(1, NV_BAR0, 4, 4) == KDEBUG_PEEK_DENY && map_calls == 0);
    reset(); nv.bar[0] = 0x8U;                      /* 番地 0 (prefetch bit だけ) */
    CHECK(call(1, 0x0, 4, 4) == KDEBUG_PEEK_DENY && map_calls == 0);
    CHECK(call(1, KPEEK_NV_PMC_OFF, 4, 4) == KDEBUG_PEEK_DENY);
    reset(); nv.command = 0;
    CHECK(call(1, NV_BAR0, 4, 4) == KDEBUG_PEEK_DENY && map_calls == 0);
    reset(); nv.command = PCI_CMD_IO_ENABLE;
    CHECK(call(1, NV_BAR0, 4, 4) == KDEBUG_PEEK_DENY);
    reset(); nv_present = 0;                        /* NP21/W: PCI が無い */
    CHECK(call(1, NV_BAR0, 4, 4) == KDEBUG_PEEK_DENY && data_untouched());
    reset(); nv.device = 0x0222;
    CHECK(call(1, NV_BAR0, 4, 4) == KDEBUG_PEEK_DENY);
    reset(); nv.bar[0] = NV_BAR0 | 0x8U;            /* prefetch の印は基準から外す */
    CHECK(call(1, NV_BAR0, 4, 4) == KDEBUG_PEEK_OK);
    CHECK(sizeof(kpeek_allow_rows) / sizeof(kpeek_allow_rows[0]) == kpeek_allow_count);
}

/* MMIO: 窓の付け替え (K6・K7) */
static void t_window(void)
{
    u32 win, a;
    /* 4MB の境界をまたぐ要求は ARG。境界で終わる要求は通る */
    reset(); nv.bar[0] = 0x24000000U;
    a = NV_BAR0 + KPEEK_NV_PROM_OFF;               /* PROM は 0x300000-0x31FFFF、4MB の中 */
    CHECK(call(1, a, 1, 0x10) == KDEBUG_PEEK_OK);
    reset(); nv.bar[0] = 0x240E0000U;              /* PROM の行が 4MB 境界 (0x24400000) をまたぐ BAR */
    a = 0x24400000U - 0x10U;                       /* PROM の行の末尾 = 4MB 境界 */
    CHECK(call(1, a, 1, 0x20) == KDEBUG_PEEK_ARG && map_calls == 0 && rq.fail_addr == a);
    CHECK(call(1, a, 1, 0x10) == KDEBUG_PEEK_OK);  /* 4MB 境界ちょうどで終わる */

    /* 順序: irq_save → raw 退避 → map → 読み → restore → irq_restore */
    reset();
    win = MEM_DEVICE_APERTURE_BASE + ((NV_BAR0 + KPEEK_NV_PCRTC0_OFF) & 0x3FFFFFU);
    pt_set(win, 0xFE000000U | PTE_PRESENT | PTE_RW | PTE_PCD);    /* 元の Xe10 の窓 */
    pt_set(win + PAGE_SIZE, 0xFE001000U | PTE_PRESENT | PTE_RW | PTE_PCD);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PCRTC0_OFF + 0xF00, 4, 0x200) == KDEBUG_PEEK_DENY); /* 行外 (0x601000〜) */
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PCRTC0_OFF + 0xE00, 4, 0x200) == KDEBUG_PEEK_OK);
    CHECK(streq(ev, "SrMLRU"));
    CHECK(load_irq_depth == 1 && irq_depth == 0);
    CHECK(load_addr == win + 0xE00 && load_width == 4 && load_len == 0x200);
    CHECK(load_phys == NV_BAR0 + KPEEK_NV_PCRTC0_OFF + 0xE00);    /* 張ってから読んだ */
    CHECK(data_is(NV_BAR0 + KPEEK_NV_PCRTC0_OFF + 0xE00, 0x200));
    CHECK(map_flags == (PTE_PRESENT | PTE_PCD));                   /* RW なし・supervisor */
    CHECK(!(map_flags & PTE_RW) && !(map_flags & PTE_USER));
    CHECK(pt_get(win) == (0xFE000000U | PTE_PRESENT | PTE_RW | PTE_PCD));   /* 戻った */
    CHECK(pt_get(win + PAGE_SIZE) == (0xFE001000U | PTE_PRESENT | PTE_RW | PTE_PCD));

    /* 2 ページにまたがる読み: 退避と戻しは 2 つずつ */
    reset();
    win = MEM_DEVICE_APERTURE_BASE + ((NV_BAR0 + KPEEK_NV_PROM_OFF) & 0x3FFFFFU);
    pt_set(win, 0x11111000U | PTE_PRESENT);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PROM_OFF + 0xF00, 1, 0x200) == KDEBUG_PEEK_OK);
    CHECK(streq(ev, "SrrMLRRU") && restore_calls == 2);
    CHECK(data_is(NV_BAR0 + KPEEK_NV_PROM_OFF + 0xF00, 0x200));
    CHECK(pt_get(win) == (0x11111000U | PTE_PRESENT) && pt_get(win + PAGE_SIZE) == 0);

    /* map の失敗: WINDOW、読まない、戻しと irq_restore は必ず通る */
    reset(); map_fail = 1;
    win = MEM_DEVICE_APERTURE_BASE + ((NV_BAR0 + KPEEK_NV_PMC_OFF) & 0x3FFFFFU);
    pt_set(win, 0xFE000000U | PTE_PRESENT | PTE_RW | PTE_PCD);
    CHECK(call(1, NV_BAR0 + KPEEK_NV_PMC_OFF, 4, 0x10) == KDEBUG_PEEK_WINDOW);
    CHECK(streq(ev, "SrMRU") && irq_depth == 0 && load_calls == 0);
    CHECK(rq.fail_addr == NV_BAR0 && data_untouched());
    CHECK(pt_get(win) == (0xFE000000U | PTE_PRESENT | PTE_RW | PTE_PCD));
}

void _start(void)
{
    CHECK(sizeof(u32) == 4 && sizeof(KdebugPeek) == 40 + KDEBUG_PEEK_CHUNK);
    t_entry();
    t_args();
    t_ram();
    t_allow();
    t_window();
    if (fails) { output("kpeek_host: FAIL "); out_dec((u32)fails); output("\n"); finish(1); }
    output("kpeek_host: PASS\n");
    finish(0);
}
