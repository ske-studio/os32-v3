/* Image launch, context handoff and entry. Included by exec.c. */
/* ======================================================================== */
/*  exec_launch — 外部プログラムのロードと実行 (exec_run / exec_start の実体) */
/*                                                                          */
/*  gui = 0: 従来の exec_run。子が終わるまで呼び出し元を塞ぐ。               */
/*  gui = 1: K5b の exec_start。子が最初の OP_WAIT で park した時点でも戻る。 */
/*                                                                          */
/*  違いは **どこで longjmp を受けるか** の 1 点だけで、ロードもレイアウトも  */
/*  共通。仮想レイアウト (app.ld / memmap.h の RING3_*) は 1 バイトも         */
/*  動かない (I12/I13) — 動いたのは「物理をどこから取るか」だけ。            */
/* ======================================================================== */
static int exec_launch(const char *cmdline, int gui_arg)
{
    if (font_guard_active) return OS32_ERR_BUSY;
    if (paging_v86_session_open()) return EXEC_ERR_INVALID;
    if (v86_call_pinned()) return EXEC_ERR_INVALID;
    /* longjmp 復帰側で読む gui。caller_context は setjmp 前の値から不変。
     * volatile でフレーム上に固定する
     * — レジスタに置かれると longjmp で失われる (他は全部グローバルで判断)。 */
    volatile int gui = gui_arg;
    u32 load_base;
    u32 max_size;
    u32 stack_top;
    u32 guard_a, guard_b;
    u32 exec_heap_base, exec_heap_size;
    u32 sbrk_end;            /* 実際に物理を張る sbrk の上端 (= sbrk 上限) */
    u32 stack_size = MEM_EXEC_STACK_SIZE;
    int is_shell;
    int launcher_id;
    int id;
    u32 need_pages;

    u8 *load_addr;
    OS32Header *hdr;
    int sz;
    u32 text_sz, bss_sz, heap_sz, entry_off;
    ExecEntry entry;
    AppSlot *ctx;
    int want_ring3 = 0;      /* CPL=3 で走らせるか (OS32X_FLAG_RING3, v2 M1) */

    char path[VFS_MAX_PATH];
    /* 解決済みのパス。ヘッダを先に 1 ページ読むので、本体の読み込みでは
     * 同じ探索をやり直さずこちらを使う (探索でヒットした綴りを保つ)。 */
    static char resolved[VFS_MAX_PATH];
    /* ヘッダだけを先に読むカーネル側バッファ。本体を読む先の物理は、
     * ヘッダの text_size / bss_size / heap_size を見るまで決まらない
     * (D1 の「起動時の順序」手順 2)。 */
    static u8 hdrbuf[OS32X_HDR_SIZE];
    u32 launcher_pd = paging_current_cr3();
    int launch_cmd_len = 0, launch_read_failed = 0;
    while (launch_read_byte(launcher_pd, cmdline + launch_cmd_len, &launch_read_failed)) {
        if ((u32)launch_cmd_len == S32_MAX) return EXEC_ERR_INVALID;
        launch_cmd_len++;
    }
    if (launch_read_failed) {
        shell_print("Error: unreadable launch command\n", ATTR_RED);
        return EXEC_ERR_INVALID;
    }
    const char *p = cmdline;
    int i = 0;

    launcher_id = appslot_cur();
    is_shell = (exec_nest_level == 0);
    if (is_shell) g_layout_reject = 0;   /* 0 段の起動ごとに消す (子では触らない) */

    /* ---- ID の池 (D3)。物理の勘定はヘッダを読んでから ---- */
    if (is_shell) {
        id = APP_ID_SHELL;
    } else {
        id = appslot_start_admit(gui, 0, 0);
        if (id < 0) {
            if (id == OS32_ERR_FULL)
                shell_print("Error: too many programs running\n", ATTR_RED);
            return id;
        }
    }

    /* コマンドラインからパスを抽出。**切り詰めない** (票 TASK_VFS_FD_PATH
     * v3 の追記): 以前は 255 バイトで切って、切った名前の別のプログラムを
     * 起動し得た。収まらなければ「見つからない」と同じ戻り方で断る。 */
    while (*p == ' ') p++;
    while (*p && *p != ' ' && i < (int)sizeof(path) - 1) {
        path[i++] = *p++;
    }
    path[i] = '\0';
    if (*p && *p != ' ') {
        shell_print("Error: command name too long\n", ATTR_RED);
        return EXEC_ERR_NOT_FOUND;
    }

    /* ====== Level に応じたメモリレイアウト決定 ====== */
    if (is_shell) {
        /* 常駐シェル: 専用のシェル帯。 */
        load_base = MEM_SHELL_LOAD_ADDR;
        max_size  = MEM_SHELL_MAX_SIZE;
        stack_top = MEM_SHELL_STACK_TOP;
        guard_a   = 0; /* 固定分割の両 heap 間には guard を置かない */
        guard_b   = MEM_SHELL_GUARD;
        sbrk_end  = 0;
        exec_heap_base = 0;
        exec_heap_size = 0;
    } else {
        load_base = MEM_EXEC_LOAD_ADDR;
        stack_top = MEM_APP_STACK_TOP;
        guard_a = guard_b = sbrk_end = exec_heap_base = exec_heap_size = 0;
        max_size = MEM_EXEC_HEAP_BASE - load_base;

    }

    load_addr = (u8 *)load_base;

    /* ====== ヘッダだけ先読み (master CR3、カーネルバッファ) ======
     * 現行のように全部読んでから枚数を決めることはできない — 読む先の
     * 物理がまだ無いため (D1 の手順 2)。 */
    kstrncpy(resolved, path, VFS_MAX_PATH);
    sz = vfs_read(resolved, hdrbuf, (int)sizeof(hdrbuf));

    /* フォールバック: パスにスラッシュがない場合、標準ディレクトリを順に検索 */
    /* 注意: SYS_DEFAULT_PATH (config.h) と整合させること */
    if (sz <= 0) {
        int has_slash = 0;
        int pi;
        for (pi = 0; path[pi]; pi++) {
            if (path[pi] == '/') { has_slash = 1; break; }
        }
        if (!has_slash) {
            static const char *search_dirs[] = {
                "/bin/", "/sbin/", "/usr/bin/", (const char *)0
            };
            int di;
            for (di = 0; search_dirs[di]; di++) {
                /* 連結が溢れる候補は切り詰めて試さない (別の名前になる) */
                if (kstrlen(search_dirs[di]) + kstrlen(path) + 1 > VFS_MAX_PATH)
                    continue;
                kstrncpy(resolved, search_dirs[di], VFS_MAX_PATH);
                kstrncat(resolved, path, VFS_MAX_PATH);
                sz = vfs_read(resolved, hdrbuf, (int)sizeof(hdrbuf));
                if (sz > 0) break;
            }
        }
    }

    if (sz <= 0) {
        return EXEC_ERR_NOT_FOUND;
    }

    hdr = (OS32Header *)hdrbuf;

    /* ---- KAPI データ欄の配置の照合 (票 TASK_KAPI_DATA_FIELDS、ヘッダ v3) ----
     * v62 以前のバイナリはデータ欄 (sbrk_heap_limit / shm_base) を別の
     * オフセットで読む。走らせると malloc が全部 ENOMEM になったり共有メモリの
     * 番地を取り違えたりするので、**常駐シェルも含めて**ここで断る
     * (シェルの停止と案内は kernel.c が exec_layout_rejected() を見て出す)。 */
    {
        int lrc = os32x_layout_check(hdr, (u32)sz, (u32)KAPI_DATA_FIELDS_OFF);
        if (lrc != OS32X_LAYOUT_OK) {
            if (is_shell) g_layout_reject = 1;
            kprintf(ATTR_YELLOW, "[exec] %s: %s (bin=%x kernel=%x)\n", path,
                    os32x_layout_reason(lrc),
                    (lrc == OS32X_LAYOUT_MISMATCH) ? hdr->kapi_data_off : 0u,
                    (u32)KAPI_DATA_FIELDS_OFF);
            const char *reason = os32x_layout_reason(lrc);
            if (lrc == OS32X_LAYOUT_MISMATCH) {
                if (hdr->kapi_data_off != KAPI_DATA_FIELDS_OFF) reason = "KAPI data layout mismatch";
                else if (hdr->kapi_abi_generation != OS32_KAPI_ABI_GENERATION) reason = "KAPI ABI generation mismatch";
                else if (hdr->memory_layout_generation != OS32_MEMORY_LAYOUT_GENERATION) reason = "memory layout generation mismatch";
                else if (hdr->min_api_ver > KAPI_VERSION) reason = "required KAPI version is newer than kernel";
                else reason = "shared library protocol mismatch";
            }
            shell_print(reason, ATTR_RED);
            shell_print("; rebuild required\n", ATTR_RED);
            return EXEC_ERR_INVALID;
        }
    }

    {
        const char *reason = exec_image_reject_reason(hdr, is_shell, load_base, max_size);
        if (reason) {
            if (is_shell) g_layout_reject = 1;
            shell_print(reason, ATTR_RED);
            return EXEC_ERR_INVALID;
        }
    }
    text_sz   = hdr->text_size;
    bss_sz    = hdr->bss_size;
    heap_sz   = hdr->heap_size;
    entry_off = hdr->entry_offset;

    /* ---- CUI 専用の宣言 (票 T8-2、受入 F5 の不合格を受けて) ----
     * `--cpl0` の砦 (下) は CPL=0 のプログラムしか捕まえない。v86.bin は
     * flags 0x0 の **CPL=3** プログラムで、V86 へは KAPI (v86_*) を通して
     * カーネル側から入るので素通りしていた。OS32X_FLAG_CUI_ONLY
     * (mkos32x --cui-only / app.conf の 4 列目 `cui`) を見て、GUI からの
     * 起動 (exec_start) だけをここで断つ。CUI の exec_run は無変更。
     * 帯も池もまだ 1 つも動かしていない位置に置くこと。 */
    if (!is_shell && appslot_cui_only_admit(gui, hdr->flags) < 0) {
        shell_print("Error: cui only - run this from CUI mode\n", ATTR_RED);
        return OS32_ERR_INVAL;
    }

    want_ring3 = !is_shell;
    if (want_ring3) {
        if (exec_stack_bytes(hdr->stack_size, (u32)launch_cmd_len, &stack_size))
            return EXEC_ERR_INVALID;
        guard_b = stack_top - stack_size - PAGE_SIZE;
    }

    if (!is_shell) {
        /* USER 初期量: libc は BSS 端の端数 + 1 page、exec_heap は最低 64KiB。 */
        exec_heap_base = MEM_EXEC_HEAP_BASE;
        exec_heap_size = MEM_EXEC_HEAP_MIN;
        if (heap_sz) {
            if (heap_sz > guard_b - exec_heap_base) return EXEC_ERR_NOMEM;
            exec_heap_size = PAGE_ALIGN_UP(heap_sz);
            if (exec_heap_size < MEM_EXEC_HEAP_MIN) exec_heap_size = MEM_EXEC_HEAP_MIN;
        }
        sbrk_end = PAGE_ALIGN_UP(load_base + text_sz + bss_sz) + PAGE_SIZE;
        guard_a = sbrk_end;
        /* 加算と必要ページの算出より先に VA を検査する。 */
        if (sbrk_end + PAGE_SIZE > exec_heap_base || exec_heap_size > guard_b - exec_heap_base)
            return EXEC_ERR_NOMEM;
    } else if (exec_shell_layout(text_sz, bss_sz, heap_sz,
                                 &sbrk_end, &exec_heap_base, &exec_heap_size)) {
        return EXEC_ERR_NOMEM;
    }

    /* ======== 物理の勘定 (D5)。入らなければ拒否、切り詰めない ======== */
    need_pages = 0;
    if (want_ring3) {
        need_pages = exec_ring3_pages(load_base, sbrk_end, exec_heap_size, stack_size);
        if (appslot_start_admit(gui, need_pages, pgalloc_free_pages()) < 0) {
            shell_print("[DBG] NOMEM: need pages=", ATTR_WHITE);
            shell_print_dec(need_pages, ATTR_WHITE);
            shell_print(" free=", ATTR_WHITE);
            shell_print_dec(pgalloc_free_pages(), ATTR_WHITE);
            shell_print("\n", ATTR_WHITE);
            exec_restore_band(launcher_id);
            return EXEC_ERR_NOMEM;
        }
    }

    /* ======== 起動元のヒープ使用量を控える (I7) ========
     * ここから先の失敗 (exec_launch_abort) は起動元の状態を戻すので、
     * **失敗しうる操作より前に**控えておく。後ろに置くと、巻き戻しが
     * 古い exec_heap_used で起動元のヒープ管理変数を上書きする。 */
    if (!is_shell) {
        exec_heap_save_state(&appslot_at(launcher_id)->exec_heap_used);
    }

    /* ======== スロットに諸元を書く (まだ commit しない) ======== */
    ctx = appslot_at(id);
    ctx->load_addr = load_base;
    ctx->stack_top = stack_top;
    ctx->stack_size = stack_size;
    ctx->stack_base = stack_top - stack_size;
    ctx->guard_a = guard_a;
    ctx->guard_b = guard_b;
    ctx->exec_heap_base = exec_heap_base;
    ctx->exec_heap_size = exec_heap_size;
    ctx->exec_heap_used = 0;
    ctx->sbrk_heap_limit = sbrk_end;
    ctx->cpl3 = 0;
    ctx->disk_write_authorized = exec_disk_write_path_allowed(resolved);
    /* 票 T8 D1a: 全画面 GFX の宣言 (OS32X_FLAG_GFX) を見るのは gfx_init を
     * 呼ばれた瞬間なので、起動時にヘッダの flags を控えておく。 */
    ctx->hdr_flags = hdr->flags;
    ctx->band_top = g_ring3_band_top;
    ctx->band_pdes = g_ring3_band_pdes;
    ctx->pages = need_pages;

    /* ======== CPL=3: アドレス空間と per-app 物理 (D1) ======== */
    if (want_ring3) {
        u32 as_owner;

        /* AS owner は AS 作成の直前に取り、struct addrspace に持つ
         * (TASK_T1_LEDGER §4-8)。返却は exec_teardown_app。 */
        if (!ledger_owner_new(LEDGER_KIND_AS, (u32)id, "app", &as_owner)) {
            shell_print("Error: ring3 addrspace create failed\n", ATTR_RED);
            exec_restore_band(launcher_id);
            return EXEC_ERR_NOMEM;
        }
        paging_load_cr3(paging_kernel_pd_phys());
        ctx->as = kmalloc(sizeof(*ctx->as));
        if (!ctx->as || paging_addrspace_create_lease(ctx->as, as_owner) != 0) {
            if (ctx->as) kfree(ctx->as);
            ctx->as = 0;
            (void)ledger_owner_retire(as_owner);
            exec_restore_context(launcher_id);
            return EXEC_ERR_NOMEM;
        }
        ctx->cpl3 = 1;
        appmem_init(ctx->as, load_base + text_sz + bss_sz, sbrk_end,
                    exec_heap_base + exec_heap_size, ctx->guard_b);

        /* **I6**: アプリ PT は master の identity PTE で初期化されている。
         * 落とし忘れると物理 0x5xxxxx が素通しで見え、他アプリのページや
         * pgalloc の作業域が CPL=3 から読める。ここが本設計で最も静かに
         * 壊れる箇所なので、per-app 物理を張る前に必ず全部 0 にする。 */
        paging_addrspace_clear_app_band(ctx->as);

        /* 3 領域を per-app 物理で張る。連続が取れなければページ単位へ倒す
         * (D10 の断片化の申し送り)。ガードは張らない = 非 present のまま。 */
        if (app_map_region(ctx->as, load_base, sbrk_end) != 0 ||
            app_map_region(ctx->as, exec_heap_base,
                           exec_heap_base + exec_heap_size) != 0 ||
            app_map_region(ctx->as, ctx->stack_base, ctx->stack_top) != 0) {
            shell_print("Error: out of physical memory for app\n", ATTR_RED);
            return exec_launch_abort(launcher_id, id, EXEC_ERR_NOMEM);
        }

        /* --- K3: 共有ライブラリ帯域 (0x80000000、常駐シェル帯とは別) ---
         * .text/.rodata は RO+USER、.data/.bss は同じ仮想番地にこのアプリ
         * 専用の物理ページ (原本から複製)。**master CR3 のまま**行う —
         * 原本 g_data_master は共有ライブラリ帯の末尾 = アプリ固有 PDE の
         * 中にあり、アプリ CR3 の下では別物を指す (I11)。 */
        if (shlib_addrspace_attach(ctx->as) < 0) {
            shell_print("Error: shlib data attach failed (out of memory)\n", ATTR_RED);
            return exec_launch_abort(launcher_id, id, EXEC_ERR_NOMEM);
        }
    }

    /* ======== setjmp — この ID の呼び出し元へ帰る点 ======== */
    volatile Ring3CallContext caller_context;
    ring3_context_save(&caller_context);
    if (exec_setjmp(ctx->jmpbuf) != 0) {
        ring3_context_clear();
        /* ======== longjmp復帰ポイント ========
         * ローカル変数は当てにできない (setjmp 後に書き換わったものが
         * 復帰側では読めない)。caller_context は setjmp 前から不変。
         *
         * フォルト経由の復帰では例外ゲートが IF をクリアしたまま longjmp
         * してくる (exec_longjmp は EFLAGS を復元しない)。呼び出し元は常に
         * 割り込み有効で動いているので、ここで無条件に開けてよい。 */
        exec_pending_finish();
        ring3_context_restore(&caller_context);
        _enable();
        /* 畳み (終了 / fault / CTRL+STOP) も park も、戻す作業は
         * exec_exit / exec_pending_finish / exec_park で済んでいる。 */
        if (g_longjmp_reason == EXEC_LJ_PARK) {
            return g_longjmp_id;      /* app_id (2〜5) — まだ生きている */
        }
        return gui ? 0 : exec_exit_status;
    }

    entry = (ExecEntry)(load_addr + entry_off);

    int image_rc = exec_load_image(ctx, resolved, hdr);
    if (image_rc) return exec_launch_abort(launcher_id, id, image_rc);

    /* ヒープ・ガードページ設定 */
    if (is_shell) {
        /* exec_shell_layout の分割が常駐 CRT の sbrk 上限も決める。 */
        ctx->exec_heap_base = exec_heap_base;
        ctx->exec_heap_size = exec_heap_size;
        kapi->sbrk_heap_limit = sbrk_end;
    }


    if (!is_shell) {
        kapi->sbrk_heap_limit = sbrk_end;

    }

    {
        u32 argv_addr;
        u32 frame[EXEC_ENTRY_ARGS + 1];
        u32 new_esp;
        u32 u_esp;   /* ring3: iret に渡すユーザ ESP (ダミー retaddr 込み) */
        /* 呼び出し元 ESP の退避先。ローカルにしないのは、子のスタックへ
         * 切り替えた後の復帰ムーブが %esp/%ebp 相対では読めないため。
         * ID 別に持つ (単一 static だとネスト exec で上書きされる)。 */
        static u32 saved_esp_stack[APP_SLOT_COUNT];

        int argc = exec_build_argv(ctx, launcher_pd, cmdline, launch_cmd_len, &argv_addr);
        if (argc < 0) return exec_launch_abort(launcher_id, id, argc);
        stack_top = argv_addr;

        /* ---- 呼び出しフレームを子スタック上に自分で組む ----
         * ExecEntry は __cdecl (int argc, char **argv, KernelAPI *api) で、
         * CRT0 `_start_c` は 4 語目 aux (常駐 shell には内部入口の表の番地、
         * app には 0 — T7b-b2 §3-1) も受ける。低位から argc, argv, kapi, aux
         * の順に並べる。call 時点で ESP を 16 バイト境界に揃えるのは
         * SysV i386 ABI の要求。 */
        new_esp = (stack_top - EXEC_ENTRY_ARGS * sizeof(u32)) & ~(u32)(ABI_STACK_ALIGN - 1);
        frame[1] = (u32)argc;
        frame[2] = argv_addr;
        frame[3] = (u32)kapi;
        frame[4] = is_shell ? (u32)(uptr)&g_shell_service : 0;

        if (want_ring3) {
            /* --- M2c: CPL=3 アプリには本物の表でなくトランポリン表を渡す ---
             * CPL=3 の初期 sbrk 上限は guard_a = sbrk_end = page_up(img_end) + PAGE_SIZE。 */
            ((u32 *)ring3_tramp_page)[KAPI_DATA_IDX_SBRK_HEAP_LIMIT] = sbrk_end;
            ((u32 *)ring3_tramp_page)[KAPI_DATA_IDX_SHM_BASE] = kapi->shm_base;
            frame[3] = ring3_tramp_page;   /* api = トランポリン */

            /* --- crt0 スタック規約合わせ (retaddr ズレ修正) ---
             * ring3 は iret でエントリへ飛ぶため call が無く retaddr が
             * 積まれない。iret に渡す ESP を argc の 1 スロット下にし、
             * そこにダミー retaddr を置いて call 経路と同一に揃える。 */
            u_esp = new_esp - sizeof(u32);
            frame[0] = 0;   /* ダミー retaddr */

            g_cur_app = ctx;
        }

        if (app_store(ctx, want_ring3 ? u_esp : new_esp,
                      want_ring3 ? frame : frame + 1,
                      (want_ring3 ? EXEC_ENTRY_ARGS + 1 : EXEC_ENTRY_ARGS) * sizeof(u32)))
            return exec_launch_abort(launcher_id, id, EXEC_ERR_INVALID);
        /* The heap header needs its user VA; switch only after image and argv. */
        if (want_ring3) paging_load_cr3(ctx->as->pd_phys);
        if (want_ring3) exec_heap_user_init(ctx->as);
        else if (exec_heap_size) exec_heap_init_at(exec_heap_base, exec_heap_size);

        if (want_ring3) kselftest_run_audit("AS-launch");

        /* ======== ここで初めて「走っているのはこの ID」になる ======== */
        if (is_shell) {
            appslot_shell_commit();
            /* T7b-b2 §3-1: 確定した shell の text 範囲で内部入口を登録する。
             * 順は retire → publish に固定 — 前の shell の終了 (exec_finish)
             * で retire 済みでも、ここで重ねて retire してから publish する
             * (retire 抜きの publish は旧 shell の lease を開いたまま残す)。 */
            shell_service_retire();
            shell_service_publish(load_base, text_sz);
        } else {
            appslot_start_commit(id, gui, need_pages);
            if (want_ring3) exec_owner_diag(ctx, 0, 0, 0);
            /* 暴走判定の起点 (票 T9 §12 S6)。 */
            appslot_mark_scheduled(id, tick_count);
        }
        exec_nest_level = ctx->depth;

        if (want_ring3) {
            /* --- M1d: CPL=3 に降りる ---
             * cli → TSS.ESP0 に現在のカーネル ESP → CR3 をアプリ PD へ →
             * セグメント → iret (IF=1 はユーザモードに入ると同時)、を 1 つの
             * 原始命令で行う。途中に割り込みが入ると TSS.ESP0 と CR3 が
             * 食い違うので分けない (契約は include/cpu.h、x86 の命令列は
             * arch/x86/arch_cpu.h)。CPL=3 実行中の割り込み / int 0x80 の
             * フレームはこの直下に積まれ、setjmp フレームを踏まない。
             * ここから通常 return しない — 終了は int 0x80 → longjmp。 */
            exec_entry_calls++;
            arch_enter_user(ctx->as->pd_phys, u_esp, (u32)entry);
            /* iret 後はここへ戻らない */
        } else {
            exec_entry_calls++;
            arch_call_on_stack(saved_esp_stack[id], new_esp, entry);
        }
        /* CPL=0 プログラムから普通に戻ってきた = 正常終了 (§1 事実 15)。 */
        exec_exit(EXEC_SUCCESS, EXEC_KIND_EXITED);
    }

    return EXEC_SUCCESS;
}
