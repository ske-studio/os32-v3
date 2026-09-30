/* ======================================================================== */
/*  V86.C — 仮想8086モード サブシステムの操作コマンド                       */
/*                                                                          */
/*  Usage: v86 -t     セルフテストを実行する                                */
/*         v86 -g     実機の ROM の INT 18h AH=31h/30h の I/O を記録する     */
/*         v86 -g -t  その記録器の自己試験                                  */
/*         v86        使い方を表示する                                      */
/*                                                                          */
/*  16bit ゲスト (PC-98 ネイティブゲーム / DOS) を OS32 上で走らせるための   */
/*  ランタイムの入口。現時点で提供しているのはセルフテストだけで、          */
/*  ディスクイメージからのブートは未実装。                                  */
/*                                                                          */
/*  設計と実測データ: docs/tasks/v86v2/                                     */
/* ======================================================================== */
#include "os32api.h"
#include <string.h>
#include <stdio.h>

static KernelAPI *api;

/* v86 -g の記録 (8460 バイト)。スタックには置かない。 */
static V86Gcap cap;

static void usage(void)
{
    printf("Usage: v86 -t\n");
    printf("       v86 -d <image>\n");
    printf("       v86 -b <image> [second]\n");
    printf("       v86 -g [-t]\n");
    printf("\n");
    printf("  -t   run the V86 self test\n");
    printf("  -b   boot <image>: load its IPL at 1FC0:0000 and run it.\n");
    printf("       .nhd/.hdi boot as SASI HDD (DA/UA 80h), others as\n");
    printf("       2HD FDD (90h). [second] attaches another image as\n");
    printf("       the next drive (e.g. HDD boot + FDD for file copy)\n");
    printf("  -d   read sector C0/H0/S1 from <image> through the guest's\n");
    printf("       INT 1Bh and compare it with a direct read\n");
    printf("  -g   (admin diagnostic) call the machine's own ROM INT 18h\n");
    printf("       AH=31h, then AH=30h into 640x480 and back, and print\n");
    printf("       every I/O the ROM did meanwhile. The screen mode changes\n");
    printf("       for a moment. -g -t tests the recorder with a fixed\n");
    printf("       guest instead (no real port is touched)\n");
    printf("\n");
    printf("The self test runs a small 16bit program under virtual 8086\n");
    printf("mode and checks that it can write memory, that allowed I/O\n");
    printf("ports pass through untrapped, that denied ports are trapped\n");
    printf("and emulated, and that a real hardware IRQ reaches the guest's\n");
    printf("own interrupt handler.\n");
}

/* ------------------------------------------------------------------------ */
/*  v86 -g — 票 docs/tasks/realhw/TASK_PEGC480_REALHW.md §3 段 1             */
/*  出力は rshell で取れる形 (1 件 1 行、行頭の文字で種類が分かる)。          */
/* ------------------------------------------------------------------------ */
static const char *gcap_status_name(u32 st)
{
    static const char *n[] = {
        "OK", "no answer to AH=31h", "AH=31h value not recognized",
        "AH=30h rejected (AH != 05h)", "overflow (> 512 OUTs)",
        "aborted (unsupported I/O instruction)", "guest did not finish",
        "V86 setup failed", "IVT[18h] does not point into ROM",
        "self test failed"
    };
    return (st < sizeof(n) / sizeof(n[0])) ? n[st] : "?";
}

static const char *gcap_layout_name(u32 l)
{
    if (l == V86G_LAYOUT_BIT2) return "bit2 (NP21/W bios18.c order)";
    if (l == V86G_LAYOUT_BIT3) return "bit3 (Bible 3-2 order)";
    return "-";
}

static const char *gcap_phase_name(u32 ph)
{
    static const char *n[] = { "test", "rd31", "s480", "back" };
    return (ph < 4) ? n[ph] : "?";
}

static const char *gcap_exit_name(u32 r)
{
    static const char *n[] = {
        "none", "HLT", "escape port", "unsupported instruction",
        "timeout", "hotkey", "fault", "#GP watchdog"
    };
    return (r < 8) ? n[r] : "?";
}

static int do_gcap(int selftest)
{
    int rc;
    u32 i;
    int mode = selftest ? V86G_MODE_SELFTEST : V86G_MODE_ROM;

    printf("V86 GDC capture%s\n", selftest ? " self test (fixed guest, no real port)"
                                            : " (ROM INT 18h AH=31h/30h)");
    rc = api->v86_gdc_capture(mode, &cap);
    if (rc < 0) {
        printf("  could not start (rc=%d)\n", rc);
        printf("    -9  = GUI mode / bad argument\n");
        printf("    -17 = V86 busy / 640x480 graphics active\n");
        printf("    -4  = no kernel memory for the record\n");
        return 1;
    }

    printf("R result   : %s (status=%lu)\n",
           gcap_status_name(cap.status), (unsigned long)cap.status);
    if (selftest) {
        printf("R failbits : %04lx\n", (unsigned long)cap.selftest_fail);
    } else {
        printf("R 31h      : AX=%04x BX=%04x  layout=%s\n",
               cap.r31_ax, cap.r31_bx, gcap_layout_name(cap.layout));
        if (cap.layout != V86G_LAYOUT_NONE) {
            printf("R 30h 480  : AX=%04x BX=%04x -> AX=%04x BX=%04x\n",
                   cap.set_ax, cap.set_bx, cap.set_ret_ax, cap.set_ret_bx);
            printf("R 30h back : AX=%04x BX=%04x -> AX=%04x BX=%04x (%s)\n",
                   cap.rst_ax, cap.rst_bx, cap.rst_ret_ax, cap.rst_ret_bx,
                   cap.restore == V86G_RST_ROM ? "ROM"
                   : cap.restore == V86G_RST_FALLBACK ? "FALLBACK: OS32 table"
                   : "-");
        }
    }
    printf("R exit     : %s", gcap_exit_name(cap.exit_reason));
    if (cap.abort_kind != V86G_ABORT_NONE) {
        printf("  abort=%s at %04x:%04x",
               cap.abort_kind == V86G_ABORT_INSOUTS ? "INS/OUTS" : "32-bit IN/OUT",
               cap.abort_cs, cap.abort_ip);
    }
    printf("\n");
    printf("R count    : out=%lu%s in=%lu (other ports %lu) io=%lu\n",
           (unsigned long)cap.n_out, cap.overflow ? " OVERFLOW" : "",
           (unsigned long)cap.in_total, (unsigned long)cap.in_other,
           (unsigned long)cap.seq_next);

    /* IN: ポートごと。pass = 実機から読んだ / emu = 仮想化した値 */
    printf("# I port width count first last pass\n");
    for (i = 0; i < cap.n_in && i < V86G_IN_MAX; i++) {
        const V86GcapIn *e = &cap.in[i];
        printf("I %04x %u %lu %04x %04x %s\n", e->port, e->widths,
               (unsigned long)e->count, e->first, e->last,
               (e->flags & V86G_F_PASSED) ? "hw" : "emu");
    }
    /* OUT: 畳まずに起きた順。seq の飛び = その間の IN の数 */
    printf("# O seq phase port width value cs:ip pass\n");
    for (i = 0; i < cap.n_out && i < V86G_OUT_MAX; i++) {
        const V86GcapOut *e = &cap.out[i];
        printf("O %05lu %s %04x %u %04x %04x:%04x %s\n",
               (unsigned long)e->seq,
               gcap_phase_name((u32)(e->flags >> V86G_F_PHASE_SHIFT)),
               e->port, e->width, e->value, e->cs, e->ip,
               (e->flags & V86G_F_PASSED) ? "hw" : "emu");
    }
    printf("E end\n");
    return cap.status == V86G_ST_OK ? 0 : 1;
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    int rc;

    api = kapi;

    if (argc >= 2 && strcmp(argv[1], "-g") == 0) {
        return do_gcap(argc >= 3 && strcmp(argv[2], "-t") == 0);
    }

    if (argc >= 3 && strcmp(argv[1], "-b") == 0) {
        static const char *reason[] = {
            "none", "guest executed HLT", "escape port",
            "unsupported instruction", "session timeout", "hotkey",
            "fault", "runaway guest (#GP watchdog)"
        };
        const char *second;
        second = (argc >= 4) ? argv[3] : (const char *)0;
        printf("V86 boot: %s\n", argv[2]);
        if (second) {
            printf("  second : %s\n", second);
        }
        rc = api->v86_boot2(argv[2], second);
        if (rc < 0) {
            printf("  could not start (rc=%d)\n", rc);
            printf("    -1 = image not found / attach failed\n");
            printf("    -2 = no memory for the guest\n");
            printf("    -3 = could not read the IPL\n");
            return 1;
        }
        printf("  exit   : %s\n",
               (rc >= 0 && rc <= 7) ? reason[rc] : "?");
        printf("\n");
        printf("  Read the counters from the host:\n");
        printf("    emu_read_mem addr=v86_boot_gp     len=4  (#GP count)\n");
        printf("    emu_read_mem addr=v86_boot_bios   len=4  (BIOS calls)\n");
        printf("    emu_read_mem addr=v86_boot_iotrap len=4  (I/O traps)\n");
        printf("    emu_read_mem addr=v86_boot_ioport len=4  (last port)\n");
        printf("    emu_read_mem addr=v86_boot_gpcs   len=4  (last #GP CS)\n");
        printf("    emu_read_mem addr=v86_boot_gpip   len=4  (last #GP IP)\n");
        printf("    emu_read_mem addr=v86_boot_gpop   len=4  (opcode there)\n");
        printf("    emu_read_mem addr=v86_flt_vec     len=24 (non-#GP fault)\n");
        printf("    emu_read_mem addr=v86_disk_ident_n len=32 (disk counters)\n");
        printf("    emu_read_mem addr=v86_kbd_n_push  len=12 (kbd push/read/drop)\n");
        printf("  Exit the guest with CTRL+STOP.\n");
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "-d") == 0) {
        printf("V86 disk test: %s\n", argv[2]);
        rc = api->v86_disktest(argv[2]);
        if (rc == 0) {
            printf("  result : OK\n");
            printf("\n");
            printf("  the guest issued INT 1Bh READ DATA and the bytes it\n");
            printf("  received match a direct read of the image.\n");
            return 0;
        }
        printf("  result : FAILED (rc=%d)\n", rc);
        printf("\n");
        printf("  Inspect the details from the host:\n");
        printf("    emu_read_mem addr=v86_disk_marker len=4  (guest marker)\n");
        printf("    emu_read_mem addr=v86_disk_cmp    len=4  (bytes matched)\n");
        printf("    emu_read_mem addr=v86_disk_reason len=4  (exit reason)\n");
        return 1;
    }

    if (argc < 2 || strcmp(argv[1], "-t") != 0) {
        usage();
        return 1;
    }

    printf("V86 self test...\n");
    rc = api->v86_selftest();

    if (rc == 0) {
        printf("  result : OK\n");
        printf("\n");
        printf("  guest ran under V86, wrote memory, passed through the\n");
        printf("  allowed ports, was trapped on the denied ones, and its\n");
        printf("  own ISR received the reflected timer IRQ.\n");
        return 0;
    }

    printf("  result : FAILED (rc=%d)\n", rc);
    printf("\n");
    printf("  Inspect the details from the host:\n");
    printf("    emu_read_mem addr=v86_smoke_reason len=4   (exit reason)\n");
    printf("    emu_read_mem addr=v86_smoke_iotrap len=4   (I/O traps)\n");
    printf("    emu_read_mem addr=v86_smoke_gcnt   len=4   (guest ISR count)\n");
    printf("    emu_fault                                  (if it crashed)\n");
    return 1;
}
