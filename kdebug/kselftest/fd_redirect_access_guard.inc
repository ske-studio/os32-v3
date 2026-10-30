/* ------------------------------------------------------------------------ */
/*  RedirAccess の登録者なし拒否と TRUSTED の恒等写像を確認する。          */
/*  この作りものの USER 項目は生存確認で断られ、walk には達しない。         */
/*  RO 登録の KAPI guard は CPL=3 の d0a_test、登録者 walk はホストで確認。      */
/* ------------------------------------------------------------------------ */
static void test_fd_redirect_access_guard(void)
{
    u8 kbuf[16];
    FdRedirect r;
    int saved = ring3_in_syscall;
    int saved_depth = ring3_wm_depth;
    u32 base;

    kmemset(&r, 0, sizeof(r));
    r.target_type = FD_TARGET_BUFFER;
    r.file_fd = -1;
    r.buffer = kbuf;
    r.buf_capacity = sizeof(kbuf);
    r.owner = 2;                        /* アプリ (ID 2) が張った */
    r.access.origin = REDIR_USER;

    kdi_set_ring3_in_syscall(1);               /* ディスパッチ中を装う */
    kdi_set_ring3_wm_depth(0);
    ring3_wm_enter();                   /* WM の文脈 (gui_call のハンドラ) */
    base = ring3_range_reject_count;

    check(fd_redirect_buf_write_ok(&r, 1u) == 0,
          "access-guard: missing registrant refused at depth 1");
    check(ring3_range_reject_count == base,
          "access-guard: missing registrant refused before walk");

    /* 同じ番地でも WM (常駐側) が張ったものなら深さ 1 では素通し。 */
    r.access.origin = REDIR_TRUSTED;
    r.owner = 1;
    check(fd_redirect_buf_write_ok(&r, 1u) == 1 &&
          ring3_range_reject_count == base,
          "access-guard: trusted kernel buffer passes at depth 1");
    ring3_wm_leave();

    /* boot の CPL=0 slot は USER 登録者にできない。walk 前に断り、表は不変。 */
    check(ring3_wm_depth == 0 &&
          fd_redirect_to_buffer(2, kbuf, sizeof(kbuf), 0) == -1,
          "access-guard: CPL0 slot cannot capture USER registration");
    check(!fd_is_redirected(2),
          "access-guard: refused registration leaves the table");

    kdi_set_ring3_wm_depth(saved_depth);
    kdi_set_ring3_in_syscall(saved);
}
