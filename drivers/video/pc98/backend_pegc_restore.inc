/* Restore text synchronization and shut down the PEGC mode. */
/* ------------------------------------------------------------------------ */
/*  shutdown — 標準グラフィックモード / 起動時の周波数のテキストへ戻す        */
/*                                                                          */
/*  リニア窓を閉じる → (pegc_apply_timing が) 拡張モードを抜ける → 起動時の   */
/*  周波数・クロックの 400 ラインの表示モード → 400 ライン VRAM 構成、の順。  */
/*  E0000h の意味が「制御レジスタ」から「プレーン 3」へ戻るのは拡張モードを   */
/*  抜けた瞬間なので、MMIO への最後の書き込みはその前に済ませておく。        */
/*  ※ 周波数は **起動時に BIOS が作っていた値** へ戻す (pegc_boot_sync_record */
/*  の記録。24kHz 決め打ちで CUI がずれた件 — TASK_FDC_REALHW §9-1)。         */
/*  s_active が 0 のとき (480 ラインへ入っていない) は何もしない。           */
/*  戻り方は実機の ROM の back の列と同じ (pegc_apply_timing)。ROM は両 GDC を  */
/*  止めたまま返すので、OS32 はテキスト GDC だけを START してコンソールを残す  */
/*  (グラフィック GDC は止めたまま — 9801 経路の gfx_init() が改めて START を  */
/*  出す、従来の挙動)。                                                      */
/* ------------------------------------------------------------------------ */
/* 起動時の周波数 hs とクロックの 400 ラインへ。pegc_shutdown と
 * pegc_restore_text_sync が共有する後半。
 * クロックと PITCH は**起動時に記録したクロック**へ戻す (480 ラインへ入る側と
 * 対称。pegc_boot_sync_record)。記録が無い (PEGC の probe が通っていない =
 * 09A0h を読んでいない) ときは触らない — 従来どおり。
 * グラフィック GDC の SYNC と SCROLL (IM) もクロックとの組で選ぶ (pegc.h §10、
 * Codex レビュー P2): 起動時が 5MHz (両方) なら 5MHz 用、2.5MHz なら 2.5MHz 用。
 * 記録が無いときは従来の組 (5MHz 用 SYNC + IM=0) で、クロックと PITCH は触らない。
 * 実機の ROM の列はテキスト GDC の CSRFORM を「カーソル非表示」(CS = 0) で
 * 入れるので、CUI (GUI のシンクが無効) ならコンソールのカーソルを戻す。GUI
 * から抜けるときは console_text_gdc_start() が同じことをする。 */
static void pegc_text_sync_400(u8 hs)
{
    PegcTiming t;
    int known = (s_boot_clk1 >= 0 && s_boot_clk2 >= 0) ? 1 : 0;
    int is5m = (s_boot_clk1 == 1 && s_boot_clk2 == 1) ? 1 : 0;
    int ss5m = known ? is5m : 1;     /* 記録なし = 従来の 5MHz 用 SYNC */

    t.pegc = s_hw_ok;
    t.ext = PEGC_FF2_STD_GFX;
    t.vram = PEGC_FF2_VRAM_400L;
    t.xattr = (hs == PEGC_HSYNC_31KHZ) ? PEGC_XATTR_31KHZ : PEGC_XATTR_24KHZ;
    t.hsync = hs;
    t.set_clock = known;
    t.clk1 = (s_boot_clk1 == 1) ? PEGC_FF2_GDC_CLK1_5M : PEGC_FF2_GDC_CLK1_2M5;
    t.clk2 = (s_boot_clk2 == 1) ? PEGC_FF2_GDC_CLK2_5M : PEGC_FF2_GDC_CLK2_2M5;
    if (hs == PEGC_HSYNC_31KHZ) {
        t.msync = s_msync_400_31k;
        t.ssync = ss5m ? s_ssync_400_31k_5m : s_ssync_400_31k_2m5;
    } else {
        t.msync = s_msync_400;
        t.ssync = ss5m ? s_ssync_400_5m : s_ssync_400_2m5;
    }
    t.pitch = pegc_restore_pitch();
    t.scroll = (known && is5m) ? s_scroll_400_5m : s_scroll_400_2m5;
    pegc_apply_timing(&t);

    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_START2);
    if (!con_sink_is_enabled()) console_hw_cursor_enable();
}

static void pegc_shutdown(void)
{
    u8 hs;

    if (!s_active) return;

    mmio_w16(PEGC_MMIO_LINEAR, PEGC_LINEAR_OFF);

    /* 480 ラインでだけ見えていた 26〜30 行目を消す。25 行までは
     * コンソールの内容なので触らない (init 側で一度消してある)。 */
    pegc_tvram_clear(TVRAM_ROWS, PEGC_TEXT_ROWS_480);

    /* 起動時に記録した周波数へ戻し、それに合う 400 ラインの表示モードを入れる
     * (24kHz 決め打ちをやめた。TASK_FDC_REALHW §9-1)。起動時の 09A8h が
     * 読めなかった機種は従来どおり 24kHz。 */
    hs = pegc_restore_hsync();
    pegc_text_sync_400(hs);

    gfx_current_height = GFX_HEIGHT;
    gfx_backend_pegc.selected_mode = gfx_mode_pick(&gfx_backend_pegc, 0);
    s_active = 0;
    {
        PegcDiag d;
        pegc_diag_take(&d);
        pegc_diag_print("enter", &s_diag_enter);
        pegc_diag_print("exit ", &d);
    }
}

/* ------------------------------------------------------------------------ */
/*  pegc_restore_text_sync — ROM で戻れなかったときの OS32 側の戻し          */
/*                                                                          */
/*  `v86 -g` (kdebug/v86_gcap.c、票 TASK_PEGC480_REALHW §3 段 1) は実機の    */
/*  ROM の INT 18h AH=30h で 480 ラインへ入り、同じ AH=30h で元のモードへ     */
/*  戻す。その戻しが失敗した (AH≠05h・打ち切り・暴走) ときだけここへ来る。    */
/*  やることは pegc_shutdown の後半と同じ考え方 — 拡張モードを抜け、        */
/*  **採取の前に AH=31h で読んだ周波数** (hsync31) の 400 ラインの SYNC を   */
/*  入れ、グラフィック GDC を止める。24kHz 固定にはしない (CUI の桁ずれが     */
/*  再発する、TASK_FDC_REALHW §9-1)。                                        */
/*  リニア窓は拡張モードのときだけ閉じる (標準モードの E0000h はプレーン 3 で、*/
/*  書くと画素を 1 つ壊すだけだが意味が無い)。09A0h は PEGC の probe が通った */
/*  機種にしか無いので、読むのもそのときだけ。                              */
/* ------------------------------------------------------------------------ */
void pegc_restore_text_sync(int hsync31)
{
    /* 09A0h と 6Ah・6Eh は PEGC の probe が通った機種の値。9801 で ROM の戻しに
     * 失敗してここへ来ても書かない (代行レビュー P3-2。6Ah・6Eh は
     * pegc_apply_timing が t.pegc = s_hw_ok で見る)。480 ラインの利用可否とは別。 */
    if (s_hw_ok && pegc_stat(PEGC_STAT_SEL_GFXMODE)) {
        mmio_w16(PEGC_MMIO_LINEAR, PEGC_LINEAR_OFF);
    }
    pegc_text_sync_400(hsync31 ? PEGC_HSYNC_31KHZ : PEGC_HSYNC_24KHZ);

    gfx_current_height = GFX_HEIGHT;
    s_active = 0;
}

