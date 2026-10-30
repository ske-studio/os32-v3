/* ------------------------------------------------------------------------ */
/*  KAPI が CPL=3 へ返す文字列の置き場 (票 T9 §12 R1)                       */
/*                                                                          */
/*  sys_getcwd はカーネル帯の static cwd をそのまま返していた。カーネル帯は  */
/*  USER ビット無しで張られるので、CPL=3 の sh.bin が `cd` / `pwd` で戻り値  */
/*  を読んだ瞬間に #PF → fault kill になる (実機で見えるのは「cd したら      */
/*  シェルが落ちる」だけ)。写し先はトランポリンページ (RO+USER) の空き。     */
/*  ここで踏むのは「その番地がページに収まり、PTE に USER が立っていて、     */
/*  CPL=0 の呼び手には従来どおり static cwd が返る」の 3 つ。                */
/* ------------------------------------------------------------------------ */
static void test_tramp_user_str(void)
{
    u32 bad = kst_exec_tramp_user();
    check((bad & (1u << 0)) == 0, "getcwd scratch fits after the KAPI stubs");
    check((bad & (1u << 1)) == 0, "getcwd scratch page is present and USER");
    check((bad & (1u << 2)) == 0, "sys_getcwd copies only for CPL=3 callers");
    /* TASK_HDD_INSTALL 段 2: vfs_devname / path_get_* もカーネル帯を返していた */
    check((bad & (1u << 3)) == 0, "vfs_devname copies into the scratch for CPL=3");
    check((bad & (1u << 4)) == 0, "path_get_drive/cwd copy into the scratch for CPL=3");
}
