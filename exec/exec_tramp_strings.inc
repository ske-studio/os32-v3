/* User-visible string copies and trampoline address. Included by exec.c. */
/* ======================================================================== */
/*  vfs_cwd_user — sys_getcwd の実体 (票 T9 §12 R1、KAPI の追加はしない)     */
/*                                                                          */
/*  `sdk/kapi.json` の `sys_getcwd` の target を `vfs_cwd` からこれに差し    */
/*  替えてある。スロット番号も引数も戻り型も変わらないので [ABI] の範囲内で、*/
/*  KAPI の版も上げない (外から見える約束が 1 つも動かないため — 版を上げると */
/*  既存バイナリの min_api_ver が一斉に足りなくなる副作用の方が大きい)。     */
/*                                                                          */
/*  CPL=3 の呼び手には **トランポリンページ内の写し** を返す。カーネル帯の    */
/*  static `cwd` は USER ビットが無く、読んだ瞬間に #PF → fault kill になる  */
/*  (sh.bin の `cd` / `pwd`、apps/edit がこれを踏んでいた)。                 */
/*  写しは呼ばれるたびに上書きする — 呼び手は次の KAPI 呼び出しより前に      */
/*  読み切ること (docs/KAPI_SPEC.md の sys_getcwd の行に注記)。              */
/* ======================================================================== */
/* CPL=3 の呼び手 (ring3_in_syscall) にはトランポリンページの写しを、CPL=0 には
 * src をそのまま返す (vfs_cwd_user / vfs_devname_user / path_get_*_user 共通)。 */
static const char *tramp_copy(const char *src)
{
    char *scratch = 0;
    if (ring3_tramp_page != 0) {
        scratch = (char *)(ring3_tramp_page + RING3_USTR_OFF);
    }
    return ring3_user_str(ring3_guard_active(ring3_in_syscall, ring3_wm_depth),
                          scratch, RING3_USTR_CAP, src);
}

const char *vfs_cwd_user(void) { return tramp_copy(vfs_cwd()); }

/* ======================================================================== */
/*  vfs_devname_user — vfs_devname の実体 (票 TASK_HDD_INSTALL 段 2、        */
/*  KAPI の追加はしない)                                                     */
/*                                                                          */
/*  sys_getcwd と同じ理由・同じ手: fs/vfs.c の vfs_devname はマウント表      */
/*  (カーネル帯の static、USER ビット無し) の dev_name をそのまま返すので、   */
/*  CPL=3 の呼び手が読むと #PF → fault kill になる。2026-09-24 の NP21/W で   */
/*  cdinst (CPL=3) が hd0 の検査の `vfs_devname("/")` で落ちた (sh.bin の     */
/*  hdprep と filer も同じ経路)。`sdk/kapi.json` の target を差し替え、CPL=3 */
/*  にはトランポリンページ内の写し (sys_getcwd と共用の 1 本、次の KAPI 呼び */
/*  出しより前に読み切ること) を返す。スロット・引数・戻り型は不変で、版も  */
/*  上げない。CPL=0 の呼び手 (常駐シェル) には従来どおりの番地が返る。       */
/* ======================================================================== */
const char *vfs_devname_user(const char *prefix) { return tramp_copy(vfs_devname(prefix)); }

/* ======================================================================== */
/*  path_get_drive_user / path_get_cwd_user — lib/path.c の 2 本の実体      */
/*  (票 TASK_HDD_INSTALL 段 2 のレビュー往復 2、KAPI の追加はしない)         */
/*                                                                          */
/*  lib/path.c の cur_drive / cur_cwd もカーネル帯の static で、そのまま      */
/*  返すと CPL=3 の呼び手は読んだ瞬間に fault kill になる (vfs_devname と     */
/*  同じ種類)。target を差し替え、CPL=3 には同じ 1 本の写しを返す。スロット・*/
/*  引数・戻り型・版は不変。                                                 */
/* ======================================================================== */
const char *path_get_drive_user(void) { return tramp_copy(path_get_drive()); }
const char *path_get_cwd_user(void)   { return tramp_copy(path_get_cwd()); }

u32 exec_tramp_page_addr(void)
{
    return (V2P(ring3_tramp_raw) + PAGE_SIZE - 1) & ~(u32)(PAGE_SIZE - 1);
}
