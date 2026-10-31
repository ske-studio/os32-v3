/* PEGC state diagnostics, window alias checks and test stripes. */
/* 状態の記録 (シリアル越しでも読める 1 行にして出す — rshell は kprintf を写す)。
 * 09A8h の生の読みと、09A0h で読める 6Ah / 68h の状態 ([U] io_disp.md
 * I/O 09A0h: 02h = 奇数ラスタのマスク (68h 09h で 1)、03h = 表示 (68h 0Fh で 1)、
 * 05h = LCD モード (6Ah 41h で 1)、09h = GDC クロック (bit0 CLOCK-1 / bit1
 * CLOCK-2)、0Ah = 256 色、0Dh = 800 ライン構成)、FIFO と VSYNC の打ち切り数。
 * 実機で画面を見ずに確かめる材料 (`pegcchk`、票 TASK_PEGC480_REALHW §5)。
 * 480 ラインへ入ったときは**読むだけ**で、出すのは戻った後 (CUI で 480 ラインの
 * 間に出すとテキスト面がテスト画に重なる — テキストはグラフィックより優先)。 */
typedef struct {
    u8  hs;
    u8  clk;
    u8  msk, dsp, lcd, ext, pgc;
    u32 fifo_to, vs_to;
} PegcDiag;

static PegcDiag s_diag_enter;

static void pegc_diag_take(PegcDiag *d)
{
    d->hs  = (u8)_in(PEGC_HSYNC_PORT);
    d->msk = (u8)pegc_stat(PEGC_STAT_SEL_ODDMASK);
    d->dsp = (u8)pegc_stat(PEGC_STAT_SEL_DISP);
    d->lcd = (u8)pegc_stat(PEGC_STAT_SEL_LCD);
    _out(PEGC_STAT_PORT, PEGC_STAT_SEL_GDCCLK1);
    d->clk = (u8)(_in(PEGC_STAT_PORT) & (PEGC_STAT_BIT | PEGC_STAT_RD_GDCCLK2));
    d->ext = (u8)pegc_stat(PEGC_STAT_SEL_GFXMODE);
    d->pgc = (u8)pegc_stat(PEGC_STAT_SEL_PAGECONT);
    d->fifo_to = pegc_gdc_fifo_timeouts;
    d->vs_to = pegc_vsync_timeouts;
    gfx_counters.io_accesses += 3;
}

static void pegc_diag_print(const char *tag, const PegcDiag *d)
{
    kprintf(KLOG_ATTR, "[pegc] %s 09a8=%02x msk=%d dsp=%d lcd=%d clk=%x ext=%d "
            "800l=%d fifo_to=%u vs_to=%u\n", tag, (unsigned int)d->hs,
            (int)d->msk, (int)d->dsp, (int)d->lcd, (unsigned int)d->clk,
            (int)d->ext, (int)d->pgc, (unsigned int)d->fifo_to,
            (unsigned int)d->vs_to);
}

static void __attribute__((cold)) pegc_diag_serial(const PegcDiag *d)
{
    if (serial_gate_active()) return;
    serial_puts_polled("[pegc] enter 09a8="); serial_diag_hex(d->hs, 2);
    const char *labels[] = {" msk=", " dsp=", " lcd=", " clk=", " ext=", " 800l=", " fifo_to=", " vs_to="};
    u32 values[] = {d->msk, d->dsp, d->lcd, d->clk, d->ext, d->pgc, d->fifo_to, d->vs_to};
    for (u32 i = 0; i < 8; i++) {
        serial_puts_polled(labels[i]);
        if (i == 3) serial_diag_hex(values[i], 1);
        else serial_diag_u32(values[i]);
    }
    serial_puts_polled("\r\n");
}

/* R7FIX2: normalize bank 0, then save both windows before either write.
 * The result establishes aliasing only, not the GDC scanout source. */
#define PEGC_DIAG_ALIAS_OFFSET 16
#define PEGC_DIAG_STRIPE_WIDTH 16
#define PEGC_DIAG_SECONDS 3
static int __attribute__((cold, noinline)) pegc_alias_test(u32 base, u8 v[4])
{
    volatile u8 *a = P2V_IO(PEGC_BANK_WIN0 + PEGC_DIAG_ALIAS_OFFSET);
    volatile u8 *f = P2V_IO(base + PEGC_DIAG_ALIAS_OFFSET);
    u8 saved[2];
    mmio_w16(PEGC_MMIO_BANK0, 0);
    saved[0] = *a; saved[1] = *f;
    *a = 0x5a; v[0] = *f;
    *a = 0xa5; v[1] = *f;
    *f = 0x5a; v[2] = *a;
    *f = 0xa5; v[3] = *a;
    *a = saved[0]; *f = saved[1];
    return v[0] == 0x5a && v[1] == 0xa5 &&
                       v[2] == 0x5a && v[3] == 0xa5;
}

static void __attribute__((cold, noinline)) pegc_diag_alias(void)
{
    u8 v[4];
    int ok = pegc_alias_test(s_window_base, v);
    if (serial_gate_active()) return;
    serial_puts_polled("[pegc] alias=");
    serial_puts_polled(ok ? "ok" : "ng");
    for (u32 i = 0; i < 4; i++) {
        serial_puts_polled(i == 0 ? " ab=" : i == 2 ? " ba=" : ",");
        serial_diag_hex(v[i], 2);
    }
    serial_puts_polled(" sys16m="); serial_diag_u32(s_sys16m_ram);
    serial_puts_polled("\r\n");
}

static void __attribute__((cold, noinline)) pegc_diag_stripes(void)
{
    if (!gfx_diag_stripes) return;
    const GfxMode *mode = gfx_backend_mode(&gfx_backend_pegc);
    volatile u8 *vram = P2V_IO(s_window_base);
    for (u32 y = 0; y < mode->height; y++)
        for (u32 x = 0; x < mode->pitch; x++)
            *vram++ = (x / PEGC_DIAG_STRIPE_WIDTH) & 15;
    u32 start = tick_count;
    for (u32 i = 0; i < 2; i++) {
        if (!serial_gate_active()) {
            serial_puts_polled("[pegc] stripes t"); serial_diag_u32(i);
            serial_puts_polled("="); serial_diag_u32(tick_count);
            serial_puts_polled("\r\n");
        }
        if (!i) while ((u32)(tick_count - start) < PEGC_DIAG_SECONDS * PIT_HZ)
            cpu_delay_us(1000);
    }
}

