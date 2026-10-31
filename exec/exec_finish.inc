/* Application teardown, resource reclaim and exit landing. Included by exec.c. */
/* ======================================================================== */
/*  exec_teardown_app — CPL=3 アプリの物理とアドレス空間を返す (D1/D4)       */
/*                                                                          */
/*  **master CR3 に戻してから**呼ぶこと (破棄する PD がアクティブだと         */
/*  paging_addrspace_destroy が何もせずに戻る)。                             */
/*  返すのはこのアプリ帯の 3 領域だけ:                                        */
/*    [load_addr, sbrk_heap_limit)        本体 + data + bss + sbrk (BSS 端の端数 + 1 page)   */
/*    [exec_heap_base, +exec_heap_size)   exec_heap                          */
/*    [band_top - stack, band_top)        ユーザスタック                     */
/*  ガードページ (guard_a / guard_b) は「張っていない = 非 present」なので     */
/*  返すものが無い。共有帯 (SHM / トランポリン) に掛かる解放要求は          */
/*  paging_addrspace_free_user_range が全範囲無変更で拒否する。              */
/*  band_top は私有領域の上端 (MEM_APP_STACK_TOP)。BB の旧恒等 alias は      */
/*  supervisor のままで私有 PT に重ねない。CLIENT の lease は     */
/*  専用の解放口で回収し、ここで backing の物理ページを返さない (e11b2)。    */
/* ======================================================================== */
/* 取り残し (3 領域の外に張られたまま返らなかったページ) の累計。R5 (a) の
 * 診断で、0 のままが正常 (カーネルシンボル。KAPI にはしない)。 */
u32 exec_as_leftover_pages;
u32 exec_entry_calls; /* D35: rejected images leave this counter unchanged. */

#include "owner_diag.h"

static void exec_teardown_app(AppSlot *a)
{
    u32 left;
    if (!a || !a->cpl3 || !a->as || !a->as->pd_phys) return;
    u32 owner_pages_before = ledger_owner_pages(a->as->owner);
    u32 before = exec_as_leftover_pages;
    /* 共有ライブラリの .data 複製ページを返す (PD 破棄の前, K3) */
    if (lease_revoke_all(a->as))
        kprintf(ATTR_RED, "lease revoke failed during teardown\n");
    /* Revoke foreign leases even when private pages are quarantined. */
    if (a->as->appmem_poisoned) goto poisoned;
    shlib_addrspace_detach(a->as);
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) {
        const struct appmem_extent *e = &a->as->appmem.e[i];
        if (e->kind == APPMEM_ANON || e->kind == APPMEM_EXEC_ARENA || e->kind == APPMEM_EXEC_LARGE) {
            u32 pages = (e->end - e->base) / PAGE_SIZE;
            if (paging_addrspace_free_user_range(a->as, e->base, e->end) != pages) {
                paging_addrspace_poison(a->as);
                goto poisoned;
            }
        }
    }
    if (a->sbrk_heap_limit > a->load_addr)
        paging_addrspace_free_user_range(a->as, a->load_addr,
                                         a->sbrk_heap_limit);
    if (a->exec_heap_size)
        paging_addrspace_free_user_range(a->as, a->exec_heap_base,
                                         a->exec_heap_base + a->exec_heap_size);
    paging_addrspace_free_user_range(a->as, a->stack_base, a->stack_top);
    paging_addrspace_destroy(a->as);
    if (a->as->appmem_poisoned) goto poisoned;
    /* 最後に AS owner の取り残しを台帳から掃除し (件数は診断へ)、番号を返す
     * (TASK_T1_LEDGER §3-2、R5 (a))。 */
    left = 0;
    if (ledger_reclaim_owner(a->as->owner, &left))
        exec_as_leftover_pages += left;
    (void)ledger_owner_retire(a->as->owner);
    goto done;
poisoned:
    serial_puts_polled("OS32: appmem AS poisoned; owner pages retained\r\n");
    exec_as_leftover_pages += ledger_owner_pages(a->as->owner);
done:
    exec_owner_diag(a, 1, owner_pages_before, exec_as_leftover_pages - before);
    kfree(a->as);
    a->as = 0;
    a->cpl3 = 0;
}

/* V86 バッキングの owner (TASK_T1_LEDGER §4-8): KAPI 経由 (CPL=3 アプリの
 * syscall の中) なら呼び手の AS、カーネルの中からなら kernel。 */
u32 exec_ledger_owner(void)
{
    return (g_cur_app && g_cur_app->cpl3) ? g_cur_app->as->owner
                                         : LEDGER_OWNER_KERNEL;
}

/* ======================================================================== */
/*  exec_restore_context — 「現在のプログラム」を id のものに切り替える       */
/*                                                                          */
/*  票 D1 の I7 / I9 / I10 — 仮想レイアウトは動かさず、カーネル側の           */
/*  「いま走っているのは誰か」を表す値だけを差し替える:                       */
/*    CR3 / exec_heap の管理変数 / sbrk 上限 (本物の表と                      */
/*    トランポリンの両方) / exec ネスト段。                                   */
/* ======================================================================== */
static void exec_restore_context(int id)
{
    AppSlot *a = appslot_at(id);
    if (!a) return;

    if (a->cpl3 && a->as->pd_phys) {
        g_cur_app = a;
        paging_load_cr3(a->as->pd_phys);
    } else {
        g_cur_app = 0;
        paging_load_cr3(paging_kernel_pd_phys());
    }

    if (!a->cpl3 && a->exec_heap_base != 0) {
        /* exec_heap_init_at ではなく restore_state。init_at はヒープ先頭に
         * 空きブロックヘッダを書き直してしまい、親が子の起動前に確保して
         * いたブロックのヘッダを壊す ("bad magic feeefeee" の正体)。 */
        exec_heap_restore_state(a->exec_heap_base, a->exec_heap_size,
                                a->exec_heap_used);
    }
    kapi->sbrk_heap_limit = a->sbrk_heap_limit;
    if (a->cpl3) {
        ((u32 *)ring3_tramp_page)[KAPI_DATA_IDX_SBRK_HEAP_LIMIT] = a->sbrk_heap_limit;
    }
    exec_nest_level = a->depth;
}

/* ======================================================================== */
/*  exec_reclaim_owned — この ID が持っている資源だけを回収する (D3)         */
/*                                                                          */
/*  かつては「exec のネスト段」で回していた並びを、そのまま **アプリ ID** で  */
/*  回すようにしたもの。7 種のうち shm と db は所有者を見ていなかったので     */
/*  shm_free_owned / db_cleanup_owned へ差し替えてある (D3 の表、P3/P5)。     */
/*  ここを段のまま残すと、アプリ A の終了がアプリ B の SHM や DB 接続を       */
/*  巻き上げる (4 本同時では実際に起きる)。                                   */
/* ======================================================================== */
static void exec_reclaim_resources(int id)
{
    kapi_strings_cleanup_owned(id);
    /* (1) SQLite DB リソース (P5: cleanup_all → cleanup_owned)。
     * **FD 回収より先** (票 S0-K §1c / F2 の順序修正)。close は未 commit の
     * rollback で journal を読み書きするので、そのときまだ main / journal の
     * FD が生きていなければならない。ここを (6) に置いていた間は、
     * vfs_close_owned が先に FD を閉じ、SQLite が閉じた FD 番号へ遅延 close /
     * rollback を投げる形になっていた。他の相対順は 1 つも動かさない。 */
    db_cleanup_owned(id);
    /* (2) 標準FDのリダイレクト解除 (ファイルFDも自動クローズ)。
     * 表は FD 0/1/2 の 3 本しかないので、2 本のアプリが同時に stdout を
     * リダイレクトすることはできない (D3 の限界。GUI アプリは使わない)。 */
    fd_redirect_reset_owned(id);
    /* (3) FD自動クローズ (この ID が open した FD 3 以上)。
     * カーネル常駐FD (vfs_fd_set_protect で保護) は除外される。
     * (3a) その直前に app の font lease 表を畳む (T7b-b1 §2-4)。vfs_close_owned
     * も FONT の FD を閉じるが lease 表は知らないので、表の枠を先に返す
     * (shell の lease は kind が違い、protect なのでどちらにも掛からない)。 */
    font_lease_close_owned(id);
    vfs_close_owned(id);
    /* (4) パイプバッファ自動解放 */
    pipe_free_owned(id);
    /* (5) 共有メモリ (P3: 所有者付きになった) */
    shm_free_owned(id);
    /* (6) サウンド: この ID の退避済み音だけを捨てる (D9-4)。
     * 鳴っているのがこの ID なら止める。他のアプリの音は無事。 */
    snd_owner_exit(id);
    /* (6b) PCM: この ID が鳴らしていれば待たずに止めて資源を返す
     * (票 TASK_PCM_CS4231 §2-1「所有と回収」)。保存した owner と id だけを
     * 照合する — 現在 owner に依存せず、私有ページ返却の前に止める。 */
    pcm_reclaim(id);
}

static void exec_notify_owned(int id, int kind)
{
    /* (7) GUI リソース回収 (契約 T4 / U8)。WM がこの owner のウィンドウ・
     * サーフェス・タイマ・スロットを回収する。畳む 3 経路すべてが
     * ここを通るので、WM は 1 か所で回収できる。 */
    if (kind == EXEC_KIND_ABORTED) input_discard_stop();
    ime_owner_exit(id);
    gui_owner_exit(id, kind);
    /* (8) 打鍵の注入リング (票 K7 D5)。注ぎ手は con_sink の読み手 1 本なので、
     * 畳んだのがその 1 本なら溜まっている打鍵を捨てる。**con_sink の所有を
     * 返す前**に呼ぶこと — 照合に g_reader を使うため。 */
    kbd_inject_owner_exit(id);
    /* (9) 子の終了を端末へ知らせる EXIT レコード (票 T7 E1)。gshell 配下で
     * 走った CUI プログラムが畳まれたことは、con_sink を読んでいる端末が
     * 他に知る手がない (出力が止まるだけ) ので、ここで 1 本積む。
     * 積まない 3 つ: シェル自身の終了 (端末の親を「子の終了」と言わない)、
     * CUI モード中 (シンク無効 — 端末は居ない)、そして **読み手本人の退場**
     * (受け取る相手がもう居ない)。読み手の照合は g_reader を使うので、
     * 所有を返す (10) の **前** でなければ端末自身の退場でも積んでしまう。 */
    if (id != APP_ID_SHELL && con_sink_is_enabled() &&
        con_sink_reader_get() != id) {
        con_sink_push_exit(id);
        /* (9a) 端末**配下の子**が畳まれたら、その子の stdin に宛てて端末が
         * 注入リングへ積んだ打鍵 (票 N4 の貼り付け) の残りを捨てる。読み手
         * (端末) の退場は (8) が扱うが、子の退場では (8) の照合が外れて残る
         * ため、放っておくと次の子が食う (打鍵でも起きる既存挙動)。注ぎ手
         * (端末) は生きているので con_sink の所有規則には触れず内容だけ捨てる。
         *   捨てるのは **退場したのが読み手 (端末) の子のときだけ** — 無関係な
         * GUI アプリが畳まれても発火させない (N4a 実装レビュー B1 の退行修正)。
         * launch_child は不正 ID / 読み手不在 (CON_SINK_NO_READER) で 0 を返す
         * ので、読み手が居なくても id と一致せず安全。 */
        if (launch_child(con_sink_reader_get()) == id) {
            kbd_inject_discard();
        }
    }
    /* (9b) 起動要求表 (票 T9 D3)。**ID だけを使う** — 正常終了は AppSlot を
     * 解放した後にここへ来る (exec_kill も同じ)。空のスロットの欄を
     * 回収通知に使ってはいけない。
     *   child == id の表  : その要求は終わった (DONE + child = 0)
     *   requester == id の表: 要求者が退場した。子が残っていれば「孤児回収」
     *     (KILL の PENDING) として WM の top-level に渡す — カーネルはここから
     *     kill しない (回収文脈では CR3 も段も動かせない)。 */
    launch_owner_exit(id);
    /* (9c) Host Services のハンドル (票 N1 / TASK_N0 §1a)。owner が握ったまま
     * 畳まれたハンドルを内部解放する — RELEASE も送るので、Agent 側の受付枠
     * (ACTIVE は rid ごと 2 件) が埋まったままにならない。公開 API を通さず
     * ID を指定して解放する。 */
    host_owner_exit(id);
    /* (10) console シンクの読み手 (票 K6C)。読み手は 1 本だけなので、畳んだ
     * のがその 1 本なら所有を返す — 返さないと次の端末アプリが永久に
     * OS32_ERR_EXIST を食う。リングの中身は捨てない (GUI は続いており、
     * 次の読み手が拾えばよい)。 */
    con_sink_owner_exit(id);
    /* (11) 画面の所有者 (票 T8 D1)。全画面 GFX を握ったまま畳まれた
     * (正常終了 / kill / fault / CTRL+STOP のどれでも) ら WM へ返す。
     * WM は exec_start / exec_resume から戻った直後に gfx_screen_owner()
     * を見て、1 に戻っていれば復帰の描き直しに入る。 */
    appslot_gfx_owner_exit(id);
}

static void exec_reclaim_owned(int id, int kind)
{
    exec_reclaim_resources(id);
    exec_notify_owned(id, kind);
}

/* ======================================================================== */
/*  exec_launch_abort — 起動途中で失敗したときの唯一の巻き戻し口             */
/*                                                                          */
/*  K5b で構成が単純になった: 起動は **最後まで失敗しうる操作を済ませてから** */
/*  appslot_start_commit する (= owner / 段 / ヒープの切り替えは iret の      */
/*  直前 1 か所) ので、ここで戻すのは 3 つだけ:                              */
/*    (1) アプリの物理ページとアドレス空間 (取れていれば)                     */
/*    (2) CR3 と「現在のプログラム」を起動元へ                               */
/*    (3) スロットを空へ                                                     */
/*  起動失敗では所有者回収を回さない — 子のコードは 1 命令も走っておらず、    */
/*  この ID のタグを持つ資源は存在し得ない (模型ケース 10)。                  */
/* ======================================================================== */
static int exec_launch_abort(int launcher_id, int id, int status)
{
    AppSlot *a = appslot_at(id);
    if (a) {
        /* スロットはまだ commit していない (state は FREE のまま) ので、
         * 返すのはアプリの物理とアドレス空間だけ。回収カウンタも動かさない。 */
        paging_load_cr3(paging_kernel_pd_phys());
        exec_teardown_app(a);
        a->pages = 0;
    }
    exec_restore_context(launcher_id);
    return status;
}

/* longjmp する側がスロットを空にするので、jmpbuf は先に控える。 */
static u32 g_exit_jmpbuf[KSETJMP_BUF_LEN];
volatile u32 exec_stop_count;
/* Polled output works with IF=0, including a recursive teardown fault. */
static void exec_stop_mark(void)
{
    exec_stop_count++;
    serial_puts_polled("OS32: exec teardown stopped\r\n");
}

/* ======================================================================== */
/*  exec_exit — 現在のプログラムを畳み、その ID の呼び出し元へ戻る            */
/*                                                                          */
/*  畳むのは常に「いま走っている 1 本」だけ (D4)。正常終了・fault・          */
/*  CTRL+STOP の 3 経路が全部ここを通る。 */
/* ======================================================================== */
static void exec_finish(int id, int status, int kind)
{
    AppSlot *a = appslot_get(id);
    int parent = APP_ID_SHELL;
    u32 k;

    if (!a) return;
    exec_exit_status = status;

    /* 票 TASK_EXIT_STATUS §2-1: 種別は **呼び手** が決める。ここで status から
     * 推測してはいけない (exit(-2) と fault が同じ値になる)。
     * GUI 経路の子 (exec_start / exec_resume = a->gui) は記録しない — 同期
     * 起動の結果と混ざる (受入 S15)。 */
    if (!a->gui) {
        g_last_kind = kind;
        g_last_code = status;
    }

    /* 後始末とシェル復帰は master PD 上で行う。 */
    if (g_cur_app) {
        paging_load_cr3(paging_kernel_pd_phys());
    }

    for (k = 0; k < KSETJMP_BUF_LEN; k++) g_exit_jmpbuf[k] = a->jmpbuf[k];

    /* WM 通知より先に親の文脈へ戻す。gui_owner_exit() は
     * WM (gshell) のコードで、そこで KAPI の mem_alloc を踏むと exec_heap が
     * 「畳んだアプリの仮想ヒープ」を指したままになる — その物理はもう
     * pgalloc へ返しているので、master CR3 の下で他人のページを書きに行く。
     * 回収は全部 ID を明示して呼ぶので、owner を先に戻しても取りこぼさない。 */
    if (id == APP_ID_SHELL) {
        /* シェル自身の終了 (K4 のシェル起動ループへ戻る)。従来どおり
         * 段 0 / owner 0 に落として exec_run(shell) の setjmp 点へ帰る。 */
        g_cur_app = 0;
        exec_nest_level = 0;
        res_owner_set(0);
        exec_reclaim_owned(id, kind);
        /* T7b-b2 §3-1: shell の内部入口を外し shell lease を全部閉じる。
         * 次の shell の publish はこの後 (exec_run の appslot_shell_commit 直後)。 */
        shell_service_retire();
    } else {
#ifdef OS32_R1_FIXTURE
        r1_fixture_parent(id);
#endif
        parent = appslot_return_target(id);
        /* 装置・FD の利用終了を私有ページの返却より先に済ませる (R1)。 */
        exec_reclaim_resources(id);
        exec_teardown_app(a);
        appslot_reclaim(id);
        /* 終了に伴う master 復帰は「生存アプリの集合が変わる瞬間」= G7 の
         * 切替ではない。appslot_switch_to が transition_count で別勘定する。 */
        appslot_switch_to(parent);
        exec_restore_context(parent);
        exec_notify_owned(id, kind);
    }
}

void exec_exit(int status, int kind)
{
    if (font_guard_active) font_guard_fault(0, 0, 0);
    /* A fault during teardown must not jump over an unfinished unmap. */
    if (v86_session.closing) { exec_stop_mark(); for (;;) { _stop(); } }
    v86_session.aborting = 1;
    v86_session_end();
    ring3_context_clear();
    v86_call_cleanup();
    exec_finish(appslot_cur(), status, kind);
    g_longjmp_reason = EXEC_LJ_EXIT;
    g_longjmp_id = 0;
    exec_longjmp(g_exit_jmpbuf);
}

/* IRQ/例外では資源に触れず、対象と種別を控えて trusted stack へ移譲。 */
static void exec_pending_transfer(int kind)
{
    if (font_guard_active) font_guard_fault(0, 0, 0);
    int id = appslot_cur();
    AppSlot *a = appslot_get(id);
    if (!a || g_pending_id) { exec_stop_mark(); for (;;) { _stop(); } }
    /* A fault during teardown must not jump over an unfinished unmap. */
    if (v86_session.closing) { exec_stop_mark(); for (;;) { _stop(); } }
    v86_session.aborting = 1;
    v86_session_end();
    ring3_context_clear();
    g_pending_id = id;
    g_pending_kind = kind;
    a->state = kind == EXEC_KIND_ABORTED ? APP_STATE_ABORT_PENDING :
                                         APP_STATE_FAULT_PENDING;
    g_longjmp_reason = EXEC_LJ_PENDING;
    exec_longjmp(a->jmpbuf);
}

/* launch と resume の両 setjmp 着地からだけ呼ぶ。park は消費しない。 */
static void exec_pending_finish(void)
{
    int id, kind;
    if (g_longjmp_reason != EXEC_LJ_PENDING) return;
    if (kctx_irq_depth || kctx_exc_depth) { exec_stop_mark(); for (;;) { _stop(); } }
    id = g_pending_id;
    kind = g_pending_kind;
    g_pending_id = 0;             /* callback / 再 longjmp より先に一度だけ消費 */
    g_longjmp_reason = EXEC_LJ_EXIT;
    g_longjmp_id = 0;
    paging_load_cr3(paging_kernel_pd_phys());
    _enable();
    v86_session_end();
    v86_call_cleanup();
    exec_finish(id, EXEC_ERR_FAULT, kind);
}

void exec_fault_recover(void)
{
    exec_pending_transfer(EXEC_KIND_FAULT);
}

void __cdecl kapi_sys_exit(int status)
{
    /* CPL=3 (リング3) アプリからの正常終了 (トランポリン経由, v2 M2)。
     * master CR3 復帰・AS 破棄・per-app 物理の返却は exec_exit が ID 単位で
     * 行う。CPL=0 プログラム (シェル等) は g_cur_app が 0 なので従来どおり。 */
    exec_exit(status, EXEC_KIND_EXITED);
}
