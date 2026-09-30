"""PEGC 640x480 へ入る / 戻る OUT 列と GDC の FIFO・VSYNC 待ち (gfx/backend_pegc.c)。

受け入れ (A): 実機 PC-9821Ra266 の ROM の OUT 列 (v86 -g の記録) との突き合わせ
(pegc_mode_host.c の ROM_S480 / ROM_BACK、票 TASK_PEGC480_REALHW §3-3・§5)。

記録: tools/tests/pegc_mode_tdd.md
票:   docs/tasks/realhw/TASK_PEGC480_REALHW.md (§2 H2・H3・H5、§4)

tools/tests/pegc_mode_host.c が実物の gfx/backend_pegc.c を 1 行も写さずに
#include し、ポート I/O だけを偽物 (tools/tests/pegc_hostshim/io.h) にして
ILP32 で回す。送る OUT 列 (ポート・値・順序) を期待列と比べ、GDC への書き込みの
直前に FIFO のステータスを見ていること・待ちに上限があることを確かめる。

  python3 -B tools/tests/test_pegc_mode.py [--mutate]

--mutate は否定側。順序・値・FIFO 待ち・上限を 1 か所ずつ壊した版を写しの木で
組み、この試験が RED になることを見る。make・エミュレータ・配備には触れない。
"""
import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar                                                   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement"]
# build/config.mk の INC_GFX と同じ探索先。io.h だけを偽物に差し替える。
TARGET_INCLUDES = ["-I" + str(ROOT / p)
                   for p in ("include", "arch/x86", "platform/pc98",
                             "sdk/include/os32",
                             "gfx", "drivers", "fs", "lib", "kernel")]
SHIM = "-I" + str(ROOT / "tools/tests/pegc_hostshim")
SRC = ROOT / "tools/tests/pegc_mode_host.c"
MUT_TARGET = "gfx/backend_pegc.c"
HDR = "include/pegc.h"


def host_cmd(exe):
    return ["gcc", *FLAGS, "-Wno-unused-function", "-O0", "-nostdlib",
            "-static", "-no-pie", SHIM, *TARGET_INCLUDES, str(SRC),
            "-o", str(exe)]


def target_cmd(obj):
    return ["i386-elf-gcc", *FLAGS, "-O2", *TARGET_INCLUDES,
            "-c", str(ROOT / MUT_TARGET), "-o", str(obj)]


MUTATIONS = [
    # (名前, 対象ファイル, 元, 変異後)
    # --- FIFO 待ち (H3) ---
    ("no_wait_cmd", MUT_TARGET,
     "    (void)gdc_wait_status(g->stat, GDC_STAT_FEMP, GDC_STAT_FEMP);\n", ""),
    ("no_wait_prm", MUT_TARGET,
     "        (void)gdc_wait_status(g->stat, GDC_STAT_FFUL, 0);\n", ""),
    ("prm_waits_full", MUT_TARGET,
     "gdc_wait_status(g->stat, GDC_STAT_FFUL, 0);",
     "gdc_wait_status(g->stat, GDC_STAT_FFUL, GDC_STAT_FFUL);"),
    ("cmd_waits_not_full", MUT_TARGET,
     "gdc_wait_status(g->stat, GDC_STAT_FEMP, GDC_STAT_FEMP);",
     "gdc_wait_status(g->stat, GDC_STAT_FFUL, 0);"),
    ("wait_wrong_port", MUT_TARGET,
     "static const PegcGdcPorts s_gdc_text = { GDC_TEXT_CMD, GDC_TEXT_PARAM, GDC_TEXT_STAT };",
     "static const PegcGdcPorts s_gdc_text = { GDC_TEXT_CMD, GDC_TEXT_PARAM, GDC_GFX_STAT };"),
    ("poll_bound_off_by_one", MUT_TARGET,
     "for (i = 0; i < PEGC_GDC_FIFO_POLLS; i++) {",
     "for (i = 0; i <= PEGC_GDC_FIFO_POLLS; i++) {"),
    ("no_timeout_count", MUT_TARGET,
     "    pegc_gdc_fifo_timeouts++;\n", ""),
    ("no_delay_between_polls", MUT_TARGET,
     "        cpu_delay_us(PEGC_GDC_FIFO_POLL_US);\n", ""),
    ("cmd_bypasses_fifo", MUT_TARGET,
     "    gdc_send(g, cmd, (const u8 *)0, 0);\n",
     "    _out(g->cmd, cmd);\n"),
    # --- VSYNC 待ち (実機の ROM の群の間の待ち) ---
    ("no_vsync_wait", MUT_TARGET,
     "    pegc_wait_vsync_edge(GDC_STAT_VSYNC);\n    pegc_wait_vsync_edge(0);\n", ""),
    ("vsync_rise_only", MUT_TARGET,
     "    pegc_wait_vsync_edge(GDC_STAT_VSYNC);\n    pegc_wait_vsync_edge(0);\n",
     "    pegc_wait_vsync_edge(GDC_STAT_VSYNC);\n"),
    ("vsync_bound_off_by_one", MUT_TARGET,
     "for (i = 0; i < PEGC_VSYNC_POLLS; i++) {",
     "for (i = 0; i <= PEGC_VSYNC_POLLS; i++) {"),
    ("vsync_no_timeout_count", MUT_TARGET,
     "    pegc_vsync_timeouts++;\n", ""),
    ("vsync_no_delay", MUT_TARGET,
     "        cpu_delay_us(PEGC_VSYNC_POLL_US);\n", ""),
    ("no_wait_before_gfx_reset", MUT_TARGET,
     "    pegc_wait_vsync();\n    gdc_cmd(&s_gdc_gfx, GDC_CMD_RESET);\n",
     "    gdc_cmd(&s_gdc_gfx, GDC_CMD_RESET);\n"),
    ("one_wait_after_resets", MUT_TARGET,
     "    pegc_wait_vsync();\n    pegc_wait_vsync();\n", "    pegc_wait_vsync();\n"),
    # --- 順序 (H2) と ROM の列 ---
    ("clock_after_sync", MUT_TARGET,
     "    if (t->set_clock) {\n"
     "        _out(MODE_FF2_PORT, t->clk1);\n"
     "        _out(MODE_FF2_PORT, t->clk2);\n"
     "        gfx_counters.io_accesses += 2;\n"
     "    }\n\n"
     "    pegc_wait_vsync();\n",
     "    pegc_wait_vsync();\n"
     "    if (t->set_clock) {\n"
     "        _out(MODE_FF2_PORT, t->clk1);\n"
     "        _out(MODE_FF2_PORT, t->clk2);\n"
     "        gfx_counters.io_accesses += 2;\n"
     "    }\n"),
    ("no_ext_mode_first", MUT_TARGET,
     "        ff2_locked_write(t->ext);\n", ""),
    ("no_lcd_mode", MUT_TARGET,
     "        _out(MODE_FF2_PORT, PEGC_FF2_LCD_MODE);\n", ""),
    ("display_not_stopped", MUT_TARGET,
     "    _out(MODE_FF1_PORT, MFF1_DISP_OFF);\n", ""),
    ("hsync_always_written", MUT_TARGET,
     "    if (((u8)_in(PEGC_HSYNC_PORT) & PEGC_HSYNC_READ_HF) !=\n"
     "        (u8)(t->hsync & PEGC_HSYNC_READ_HF)) {",
     "    if (1) {"),
    ("hsync_never_written", MUT_TARGET,
     "    if (((u8)_in(PEGC_HSYNC_PORT) & PEGC_HSYNC_READ_HF) !=\n"
     "        (u8)(t->hsync & PEGC_HSYNC_READ_HF)) {",
     "    if (0) {"),
    ("no_gfx_reset", MUT_TARGET,
     "    gdc_cmd(&s_gdc_gfx, GDC_CMD_RESET);\n    gdc_cmd(&s_gdc_gfx, GDC_CMD_SLAVE);\n",
     "    gdc_cmd(&s_gdc_gfx, GDC_CMD_SLAVE);\n"),
    ("no_master", MUT_TARGET,
     "    gdc_cmd(&s_gdc_text, GDC_CMD_MASTER);\n", ""),
    ("no_second_resets", MUT_TARGET,
     "    gdc_cmd(&s_gdc_gfx,  GDC_CMD_RESET);\n    gdc_cmd(&s_gdc_text, GDC_CMD_RESET);\n", ""),
    ("no_text_csrform", MUT_TARGET,
     "    gdc_send(&s_gdc_text, GDC_CMD_CSRFORM, s_tcsrform, PEGC_GDC_CSRFORM_LEN);\n", ""),
    ("no_gfx_csrform", MUT_TARGET,
     "    gdc_send(&s_gdc_gfx,  GDC_CMD_CSRFORM, s_gcsrform, PEGC_GDC_CSRFORM_LEN);\n", ""),
    ("no_text_pitch", MUT_TARGET,
     "    gdc_cmd1(&s_gdc_text, GDC_CMD_PITCH, PEGC_GDC_TPITCH);\n", ""),
    ("no_gfx_zoom", MUT_TARGET,
     "    gdc_cmd1(&s_gdc_gfx, GDC_CMD_ZOOM, PEGC_GDC_ZOOM);\n", ""),
    ("no_text_scroll", MUT_TARGET,
     "    gdc_send(&s_gdc_text, GDC_CMD_SCROLL, s_tscroll, PEGC_GDC_SCROLL_LEN);\n", ""),
    ("no_start_stop_dance", MUT_TARGET,
     "    gdc_cmd(&s_gdc_gfx,  PEGC_GDC_CMD_START2);\n"
     "    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_START2);\n"
     "    gdc_cmd(&s_gdc_gfx,  PEGC_GDC_CMD_STOP2);\n"
     "    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_STOP2);\n\n", "\n"),
    ("grp_200line", MUT_TARGET,
     "    _out(MODE_FF1_PORT, MFF1_HIRES);\n", "    _out(MODE_FF1_PORT, MFF1_200LINE);\n"),
    ("no_crtc", MUT_TARGET,
     "    for (i = 0; i < PEGC_CRTC_COUNT; i++) {", "    for (i = 0; i < 0; i++) {"),
    ("no_xattr", MUT_TARGET,
     "        xattr_write(PEGC_XATTR_UNLOCK);\n        xattr_write(t->xattr);\n"
     "        xattr_write(PEGC_XATTR_LOCK);\n", ""),
    ("xattr_no_io_wait", MUT_TARGET,
     "    _out(PEGC_XATTR_PORT, val);\n    io_wait();\n", "    _out(PEGC_XATTR_PORT, val);\n"),
    ("no_rom_tail", MUT_TARGET,
     "    _out(MODE_FF1_PORT, MFF1_ANK_7x13);\n", ""),
    ("enter_no_start", MUT_TARGET,
     "    pegc_apply_timing(&s_timing_480);\n"
     "    gdc_cmd(&s_gdc_gfx,  PEGC_GDC_CMD_START2);\n",
     "    pegc_apply_timing(&s_timing_480);\n"),
    ("back_starts_gfx", MUT_TARGET,
     "    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_START2);\n    if (!con_sink_is_enabled())",
     "    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_START2);\n"
     "    gdc_cmd(&s_gdc_gfx, PEGC_GDC_CMD_START2);\n    if (!con_sink_is_enabled())"),
    ("back_no_cursor", MUT_TARGET,
     "    if (!con_sink_is_enabled()) console_hw_cursor_enable();\n", ""),
    ("cursor_before_start", MUT_TARGET,
     "    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_START2);\n    if (!con_sink_is_enabled()) console_hw_cursor_enable();\n",
     "    if (!con_sink_is_enabled()) console_hw_cursor_enable();\n    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_START2);\n"),
    ("console_cursor_no_dc", "kernel/console.c",
     "    outp(GDC_TEXT_PARAM, (u8)(GDC_CSRFORM_DC | (GDC_TEXT_LINES_PER_ROW - 1)));",
     "    outp(GDC_TEXT_PARAM, (u8)(GDC_TEXT_LINES_PER_ROW - 1));"),
    ("cursor_in_gui", MUT_TARGET,
     "    if (!con_sink_is_enabled()) console_hw_cursor_enable();\n",
     "    console_hw_cursor_enable();\n"),
    ("unrecorded_touches_6a", MUT_TARGET,
     "    t.pegc = s_probe_ok;\n", "    t.pegc = 1;\n"),
    ("xattr_always_31k", MUT_TARGET,
     "    t.xattr = (hs == PEGC_HSYNC_31KHZ) ? PEGC_XATTR_31KHZ : PEGC_XATTR_24KHZ;",
     "    t.xattr = PEGC_XATTR_31KHZ;"),
    # --- 値 (H5) ---
    ("no_clk2", MUT_TARGET,
     "        _out(MODE_FF2_PORT, t->clk2);\n", ""),
    ("no_pitch_480", MUT_TARGET,
     "    s_msync_480, s_ssync_480, PEGC_GDC_PITCH_480, s_scroll_480\n",
     "    s_msync_480, s_ssync_480, 40, s_scroll_480\n"),
    ("pitch_skipped", MUT_TARGET,
     "    if (t->set_clock) gdc_cmd1(&s_gdc_gfx, GDC_CMD_PITCH, t->pitch);",
     "    if (0 && t->set_clock) gdc_cmd1(&s_gdc_gfx, GDC_CMD_PITCH, t->pitch);"),
    ("clock_not_set_on_enter", MUT_TARGET,
     "    PEGC_HSYNC_31KHZ, 1, PEGC_GDC_CLK1_480, PEGC_GDC_CLK2_480,",
     "    PEGC_HSYNC_31KHZ, 0, PEGC_GDC_CLK1_480, PEGC_GDC_CLK2_480,"),
    ("clk_2m5_values_swapped", HDR,
     "#define PEGC_FF2_GDC_CLK1_2M5  0x82", "#define PEGC_FF2_GDC_CLK1_2M5  0x84"),
    ("pitch5m_either_clock", MUT_TARGET,
     "if (s_boot_clk1 == 1 && s_boot_clk2 == 1) return PEGC_GDC_PITCH_400_5M;",
     "if (s_boot_clk1 == 1 || s_boot_clk2 == 1) return PEGC_GDC_PITCH_400_5M;"),
    ("restore_clock_ignores_boot", MUT_TARGET,
     "    t.clk1 = (s_boot_clk1 == 1) ? PEGC_FF2_GDC_CLK1_5M : PEGC_FF2_GDC_CLK1_2M5;",
     "    t.clk1 = PEGC_FF2_GDC_CLK1_2M5;"),
    ("restore_touches_clock_unrecorded", MUT_TARGET,
     "    t.set_clock = known;",
     "    t.set_clock = 1;"),
    ("clk2_read_from_bit0", MUT_TARGET,
     "    s_boot_clk2 = (clk & PEGC_STAT_RD_GDCCLK2) ? 1 : 0;",
     "    s_boot_clk2 = (clk & PEGC_STAT_BIT) ? 1 : 0;"),
    ("clk_read_without_select", MUT_TARGET,
     "    _out(PEGC_STAT_PORT, PEGC_STAT_SEL_GDCCLK1);\n    clk =",
     "    clk ="),
    ("hsync_upper_bits", MUT_TARGET,
     "        _out(PEGC_HSYNC_PORT, t->hsync & PEGC_HSYNC_MASK);",
     "        _out(PEGC_HSYNC_PORT, t->hsync | (u8)(s_boot_hsync_raw & 0x80));"),
    # --- 戻りの SYNC / SCROLL をクロックとの組で選ぶ (Codex レビュー P2) ---
    ("restore_ssync_always_5m", MUT_TARGET,
     "        t.ssync = ss5m ? s_ssync_400_5m : s_ssync_400_2m5;",
     "        t.ssync = s_ssync_400_5m;"),
    ("restore_ssync31k_always_5m", MUT_TARGET,
     "        t.ssync = ss5m ? s_ssync_400_31k_5m : s_ssync_400_31k_2m5;",
     "        t.ssync = s_ssync_400_31k_5m;"),
    ("restore_ssync_always_2m5", MUT_TARGET,
     "        t.ssync = ss5m ? s_ssync_400_5m : s_ssync_400_2m5;",
     "        t.ssync = s_ssync_400_2m5;"),
    ("restore_scroll_always_im0", MUT_TARGET,
     "    t.scroll = (known && is5m) ? s_scroll_400_5m : s_scroll_400_2m5;",
     "    t.scroll = s_scroll_400_2m5;"),
    ("restore_scroll_always_im1", MUT_TARGET,
     "    t.scroll = (known && is5m) ? s_scroll_400_5m : s_scroll_400_2m5;",
     "    t.scroll = s_scroll_400_5m;"),
    ("restore_31k_uses_24k_sync", MUT_TARGET,
     "        t.msync = s_msync_400_31k;", "        t.msync = s_msync_400;"),
    # --- ヘッダ §10・§13 の値 (実機の記録と照らす) ---
    ("hdr_ssync_2m5_cr_4e", HDR,
     "#define PEGC_GDC_SSYNC_400_2M5     { 0x02, 0x26,",
     "#define PEGC_GDC_SSYNC_400_2M5     { 0x02, 0x4E,"),
    ("hdr_ssync_2m5_hbp", HDR,
     "{ 0x02, 0x26, 0x03, 0x11, 0x83, 0x07, 0x90, 0x65 }",
     "{ 0x02, 0x26, 0x03, 0x11, 0x87, 0x07, 0x90, 0x65 }"),
    ("hdr_ssync_480_hbp", HDR,
     "#define PEGC_GDC_SSYNC_480     { 0x02, 0x4E, 0x4B, 0x0C, 0x83,",
     "#define PEGC_GDC_SSYNC_480     { 0x02, 0x4E, 0x4B, 0x0C, 0x87,"),
    ("hdr_scroll_480_no_im", HDR,
     "#define PEGC_GDC_SCROLL_480        { 0x00, 0x00, 0xF0, 0x7F }",
     "#define PEGC_GDC_SCROLL_480        { 0x00, 0x00, 0xF0, 0x3F }"),
    ("hdr_scroll_480_len0", HDR,
     "#define PEGC_GDC_SCROLL_480        { 0x00, 0x00, 0xF0, 0x7F }",
     "#define PEGC_GDC_SCROLL_480        { 0x00, 0x00, 0x00, 0x40 }"),
    ("hdr_scroll_400_5m_no_im", HDR,
     "#define PEGC_GDC_SCROLL_400_5M     { 0x00, 0x00, 0xF0, 0x7F }",
     "#define PEGC_GDC_SCROLL_400_5M     { 0x00, 0x00, 0xF0, 0x3F }"),
    ("hdr_scroll_400_len0", HDR,
     "#define PEGC_GDC_SCROLL_400_2M5    { 0x00, 0x00, 0xF0, 0x3F }",
     "#define PEGC_GDC_SCROLL_400_2M5    { 0x00, 0x00, 0x00, 0x00 }"),
    ("hdr_tscroll_len0", HDR,
     "#define PEGC_GDC_TSCROLL           { 0x00, 0x00, 0xF0, 0x3F }",
     "#define PEGC_GDC_TSCROLL           { 0x00, 0x00, 0x00, 0x00 }"),
    ("hdr_tcsrform_cursor_on", HDR,
     "#define PEGC_GDC_TCSRFORM          { 0x0F,",
     "#define PEGC_GDC_TCSRFORM          { 0x8F,"),
    ("hdr_gcsrform_lr1", HDR,
     "#define PEGC_GDC_GCSRFORM          { 0x00,",
     "#define PEGC_GDC_GCSRFORM          { 0x01,"),
    ("hdr_crtc_cl15", HDR,
     "#define PEGC_CRTC_VALUES           { 0x00, 0x0F, 0x10,",
     "#define PEGC_CRTC_VALUES           { 0x00, 0x0F, 0x0F,"),
    ("hdr_lcd_is_crt", HDR,
     "#define PEGC_FF2_LCD_MODE      0x41", "#define PEGC_FF2_LCD_MODE      0x40"),
    ("hdr_xattr31_is_24", HDR,
     "#define PEGC_XATTR_31KHZ       0x21", "#define PEGC_XATTR_31KHZ       0x20"),
    ("hdr_start_is_0d", HDR,
     "#define PEGC_GDC_CMD_START2    0x6B", "#define PEGC_GDC_CMD_START2    0x0D"),
    ("hdr_stop2_is_0c", HDR,
     "#define PEGC_GDC_CMD_STOP2     0x05", "#define PEGC_GDC_CMD_STOP2     0x0C"),
]


def one_mutation(item):
    name, target, old, new = item
    original = (ROOT / target).read_text(encoding="utf-8")
    if name == "hsync_upper_bits":
        # 起動時の生の読み (81h など) を書き戻す誤り。値を持つ変数ごと足す。
        original_new = original.replace(
            "static int s_boot_hsync    = -1;",
            "static u8  s_boot_hsync_raw = 0;\nstatic int s_boot_hsync    = -1;", 1)
        original_new = original_new.replace(
            "    raw    = (u8)_in(PEGC_HSYNC_PORT);\n",
            "    raw    = (u8)_in(PEGC_HSYNC_PORT);\n    s_boot_hsync_raw = raw;\n", 1)
        if original_new == original:
            return "MUTATE %-34s SKIP (目印が見つからない)" % name, 1
        base = original_new
    else:
        base = original
    if old not in base:
        return "MUTATE %-34s SKIP (目印が見つからない)" % name, 1
    with tempfile.TemporaryDirectory(prefix="os32-pegc-mode-mut-") as td:
        exe = pathlib.Path(td) / ("mut-" + name)
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {target: base.replace(old, new, 1)},
                [host_cmd(exe)], capture_output=True)
        except subprocess.CalledProcessError:
            return "MUTATE %-34s RED (コンパイルが通らない)" % name, 0
        try:
            out = subprocess.run([str(exe)], cwd=str(tree), timeout=60,
                                 capture_output=True)
        except subprocess.TimeoutExpired:
            return "MUTATE %-34s RED (時間切れ)" % name, 0
    if out.returncode == 0:
        return ("MUTATE %-34s **GREEN のまま = 試験が規則を見ていない**"
                % name, 1)
    last = out.stdout.decode("utf-8", "replace").strip().splitlines()[-1:]
    return "MUTATE %-34s RED (期待どおり落ちた: %s)" % (
        name, last[0] if last else "rc=%d" % out.returncode), 0


if __name__ == "__main__":
    failed = 0
    with tempfile.TemporaryDirectory(prefix="os32-pegc-mode-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "pegc-mode"
        subprocess.run(host_cmd(exe), cwd=ROOT, check=True)
        print("HOST ILP32 GNU89 COMPILE PASS (real gfx/backend_pegc.c)",
              flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print("EXIT pegc_mode_host=%d" % rc, flush=True)
        failed += rc != 0
        subprocess.run(target_cmd(tmp / "backend_pegc.o"), cwd=ROOT,
                       check=True)
        print("TARGET i386-elf GNU89 -Werror COMPILE PASS (backend_pegc.c)",
              flush=True)
        if "--mutate" in sys.argv:
            failed += mutpar.run_with_control(one_mutation, MUTATIONS,
                                              ("control", MUT_TARGET, "", ""))
    sys.exit(1 if failed else 0)
