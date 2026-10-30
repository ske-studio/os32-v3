/* ------------------------------------------------------------------------ */
/*  WM の文脈では KAPI の出力検査を効かせない (2026-09-26、filer が窓も出さず  */
/*  に消えた件。POLICY_DEBUG §4-61)。WM (gshell) はアプリの syscall の中で     */
/*  走るので ring3_in_syscall = 1 のまま。その間に WM が自分のスタックの        */
/*  MouseInfo を mouse_poll に渡すと、シェル帯には USER が無いので拒否 →        */
/*  アプリが kill された。ここでは実物の門 3 つを、ディスパッチ中を装って     */
/*  カーネル帯のローカル変数で叩く: 深さ 0 なら拒否 (= 従来どおりアプリの      */
/*  不正なポインタは kill)、深さ 1 以上なら素通し。純関数の表はホスト         */
/*  (tools/tests/ring3_guard_host.c) が持つ。                                  */
/* ------------------------------------------------------------------------ */
static void test_ring3_wm_guard(void)
{
    u32 local = 0;
    int saved = ring3_in_syscall;
    int saved_depth = ring3_wm_depth;
    u32 base;

    kdi_set_ring3_wm_depth(0);
    kdi_set_ring3_in_syscall(1);               /* ディスパッチ中を装う */
    base = ring3_range_reject_count;

    /* 深さ 0 = アプリ由来: カーネル帯 (USER 無し) は書けないと言う。
     * これが「アプリの不正なポインタは今までどおり kill」の側。 */
    check(ring3_user_range_writable((u32)&local, sizeof(u32)) == 0,
          "wm-guard: app-origin kernel ptr refused");
    check(ring3_range_reject_count == base + 1u &&
          ring3_range_reject_last != 0 &&
          ring3_range_reject_addr == (u32)&local,
          "wm-guard: write-side refusal is counted");
    /* 起動中は保存callerが無効。writeはWR_TABLE、readはBANDで断る。 */
    check(ring3_user_range_ok((u32)&local, sizeof(u32)) == 0 &&
          ring3_range_reject_count == base + 2u,
          "wm-guard: app-origin read range refused");

    /* 深さ 1 = WM の文脈: 同じポインタが素通しになる (mouse_poll の経路)。 */
    ring3_wm_enter();
    check(ring3_wm_depth == 1, "wm-guard: enter -> depth 1");
    check(ring3_user_range_writable((u32)&local, sizeof(u32)) == 1,
          "wm-guard: WM ptr passes while in WM");
    check(ring3_user_ranges_writable((u32)&local, sizeof(u32),
                                     (u32)&base, sizeof(u32)) == 1,
          "wm-guard: WM 2-range passes while in WM");
    check(ring3_user_range_ok((u32)&local, sizeof(u32)) == 1,
          "wm-guard: WM read range passes while in WM");
    check(ring3_range_reject_count == base + 2u,
          "wm-guard: pass-through is not counted");

    /* 入れ子 (WM の中の gui_call → ハンドラ) でも深さが 0 に戻るまで素通し。 */
    ring3_wm_enter();
    ring3_wm_leave();
    check(ring3_wm_depth == 1 &&
          ring3_user_range_writable((u32)&local, sizeof(u32)) == 1,
          "wm-guard: nested leave keeps WM context");

    /* 出口で深さ 0 に戻れば再び拒否する (WM を抜けた後のアプリのポインタ)。 */
    ring3_wm_leave();
    check(ring3_wm_depth == 0, "wm-guard: leave -> depth 0");
    check(ring3_user_range_writable((u32)&local, sizeof(u32)) == 0,
          "wm-guard: refused again after leaving WM");
    {
        u32 u0 = ring3_wm_depth_underflow;
        ring3_wm_leave();               /* 余分な leave は負にしない */
        check(ring3_wm_depth == 0, "wm-guard: leave at 0 stays 0");
        /* 黙って止めずに数える (代行レビュー P3、exec.h の注記)。 */
        check(ring3_wm_depth_underflow == u0 + 1u,
              "wm-guard: leave at 0 is counted (underflow)");
    }

    /* 負の深さ (壊れた状態) は安全側 = ガードを効かせる。 */
    kdi_set_ring3_wm_depth(-1);
    check(ring3_user_range_writable((u32)&local, sizeof(u32)) == 0,
          "wm-guard: negative depth still guards");

    kdi_set_ring3_wm_depth(saved_depth);
    kdi_set_ring3_in_syscall(saved);
}
