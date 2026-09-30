/* =========================================================================
 *  VFS_SET_MTIME_HOST.C — `vfs_set_mtime()` の振り分け規則を
 *                          **実物の fs/vfs.c で** 確かめる
 *
 *  票:   H3 (docs/archive/shell/TASK_H3.md §4)
 *  実行: python3 -B tools/tests/test_vfs_set_mtime.py [--target]
 *  記録: tools/tests/h3_tdd.md
 *
 *  実物の fs/vfs.c をそのまま #include し、境界 (kstring / kmalloc) だけを
 *  同義の C で置く (tools/tests/vfs_kind_host.c と同じ作法)。FS ドライバは
 *  合成した VfsOps で、`set_mtime` を**持つ / 持たない**の両方を作れる。
 *
 *  押さえること:
 *    - フックを**持たない** FS は `OS32_ERR_NOSYS`。これは失敗ではなく
 *      「この FS には無い」— 呼び手 (hsync) は内容の同期を続ける。
 *      **VfsOps に足したフィールドはゼロ初期化されるので、既存の FS
 *      ドライバ (FAT / HostDrv / ISO9660) はこの経路に落ちる。**
 *    - `mtime == 0` (現行 ABI の「不明」の印) は INVAL で断り、
 *      ドライバまで届かない。不明を書けると次の同期で自分が困る。
 *    - マウントが無ければ NOMOUNT。パスは正規化してから振り分ける。
 *    - ドライバのエラーはそのまま伝える (握り潰して成功にしない)。
 *
 *  エミュレータ・実配備・make には一切触れない。
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfs.h"

/* ---- fs/vfs.c の境界 (lib/kstring_asm.asm / kmalloc はホストで組めない) ---- */
void *kzalloc(u32 size) { return calloc(1, size); }
void kfree(void *p) { free(p); }
void *kmemset(void *dst, int val, u32 n) { return memset(dst, val, n); }
int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
u32 kstrlen(const char *s) { return (u32)strlen(s); }
char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i;
    if (n == 0) return dst;
    for (i = 0; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return dst;
}
char *kstrncat(char *dst, const char *src, u32 n)
{
    u32 d = (u32)strlen(dst);
    if (d + 1 >= n) return dst;
    kstrncpy(dst + d, src, n - d);
    return dst;
}

#include "../../fs/vfs.c"
/* fs/vfs_fd.c の FD 表側 (票 TASK_VFS_FD_PATH)。この試験は FD を開かない */
void vfs_fd_invalidate_ino(void *c, u32 i) { (void)c; (void)i; }
void vfs_fd_invalidate_mount(void *c) { (void)c; }
int vfs_fd_rename_busy(void *c, const char *a, const char *b)
{ (void)c; (void)a; (void)b; return 0; }
int vfs_fd_pinned_busy(void *c, int h, u32 i, const char *r)
{ (void)c; (void)h; (void)i; (void)r; return 0; }

/* ------------------------------------------------------------------------ */

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* ---- 合成 FS ドライバ ---------------------------------------------------- */

static int   drv_set_calls;
static int   drv_set_rc;
static u32   drv_set_mtime;
static char  drv_set_path[VFS_MAX_PATH];

static int drv_set_mtime_fn(void *ctx, const char *path, os_time_t mtime)
{
    (void)ctx;
    drv_set_calls++;
    drv_set_mtime = (u32)mtime;
    kstrncpy(drv_set_path, path, VFS_MAX_PATH);
    return drv_set_rc;
}

static int drv_stat(void *ctx, const char *path, OS32_Stat *buf)
{
    (void)ctx; (void)path;
    kmemset(buf, 0, sizeof(OS32_Stat));
    buf->st_mode = (u16)(OS_S_IFREG | 0644);
    return VFS_OK;
}

static int drv_ctx_tag = 1;
static void *drv_mount(int dev) { (void)dev; return &drv_ctx_tag; }
static void drv_umount(void *ctx) { (void)ctx; }
static int drv_is_mounted(void *ctx) { (void)ctx; return 1; }

static VfsOps drv_ops;

/* **既存の FS ドライバと同じ形**で組む — 末尾の set_mtime を書かない
 * 位置指定の初期化子が、C89 の規則でゼロ (= 持っていない) になることを
 * ここで固定する。
 *
 * 実物の hostdrvfs / fatfs / iso9660 は `-Wextra` の
 * `-Wmissing-field-initializers` に引っかかるので明示的に 0 を置いてある。
 * ここは**書かなかったときにどうなるか**を確かめるのが目的なので、
 * その 1 か所だけ警告を外す。 */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
static VfsOps drv_ops_positional = {
    "synth_pos",
    drv_mount, drv_umount, drv_is_mounted,
    0, 0, 0,
    0, 0, 0,
    0,
    0, 0, 0,
    0,
    0, 0, 0,
    drv_stat
    /* set_mtime は書かない = 0 */
};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void reset(int with_hook)
{
    int i;

    drv_set_calls = 0;
    drv_set_rc = VFS_OK;
    drv_set_mtime = 0;
    drv_set_path[0] = '\0';

    kmemset(&drv_ops, 0, sizeof(drv_ops));
    drv_ops.name = "synth";
    drv_ops.mount = drv_mount;
    drv_ops.umount = drv_umount;
    drv_ops.is_mounted = drv_is_mounted;
    drv_ops.stat = drv_stat;
    if (with_hook) drv_ops.set_mtime = drv_set_mtime_fn;

    num_fs = 0;
    for (i = 0; i < VFS_MAX_FS; i++) kmemset(&mounts[i], 0, sizeof(mounts[i]));
    vfs_register_fs(&drv_ops);
    if (vfs_mount("/synth", "hostdrv", "synth") != VFS_OK) {
        printf("  (harness) vfs_mount failed\n");
        exit(2);
    }
}

int main(void)
{
    printf("=== vfs_set_mtime (票 H3 §4) ===\n");

    printf("== フックを持つ FS: そのまま渡す ==\n");
    reset(1);
    check(vfs_set_mtime("/synth/a.bin", 123456UL) == VFS_OK, "VFS_OK");
    check(drv_set_calls == 1, "ドライバを 1 度だけ呼んだ");
    check(drv_set_mtime == 123456UL, "mtime をそのまま渡した");
    check(kstrcmp(drv_set_path, "/a.bin") == 0,
          "マウント点を外した相対パスで呼んだ");

    printf("== ドライバのエラーはそのまま伝える ==\n");
    reset(1);
    drv_set_rc = VFS_ERR_NOTFOUND;
    check(vfs_set_mtime("/synth/x", 1UL) == VFS_ERR_NOTFOUND,
          "NOTFOUND をそのまま返す");
    reset(1);
    drv_set_rc = VFS_ERR_IO;
    check(vfs_set_mtime("/synth/x", 1UL) == VFS_ERR_IO,
          "IO をそのまま返す (握り潰して成功にしない)");

    printf("== フックを持たない FS: NOSYS (失敗ではない) ==\n");
    reset(0);
    check(vfs_set_mtime("/synth/a.bin", 123456UL) == VFS_ERR_NOSYS,
          "VFS_ERR_NOSYS");
    check(VFS_ERR_NOSYS == OS32_ERR_NOSYS, "値は OS32_ERR_NOSYS (-10)");
    check(drv_set_calls == 0, "ドライバを呼んでいない");

    printf("== 位置指定の初期化子で末尾を書かない既存ドライバも NOSYS ==\n");
    {
        int i;
        num_fs = 0;
        for (i = 0; i < VFS_MAX_FS; i++)
            kmemset(&mounts[i], 0, sizeof(mounts[i]));
        vfs_register_fs(&drv_ops_positional);
        if (vfs_mount("/synth", "hostdrv", "synth_pos") != VFS_OK) {
            printf("  (harness) vfs_mount failed\n");
            return 2;
        }
    }
    check(drv_ops_positional.set_mtime == 0,
          "書かなかったフィールドは 0 (C89 の集成体初期化)");
    check(vfs_set_mtime("/synth/a.bin", 123456UL) == VFS_ERR_NOSYS,
          "FAT / HostDrv / ISO9660 と同じ形のドライバは NOSYS");

    printf("== mtime == 0 は不明の印。ドライバまで届かせない ==\n");
    reset(1);
    check(vfs_set_mtime("/synth/a.bin", 0UL) == VFS_ERR_INVAL, "INVAL");
    check(drv_set_calls == 0, "ドライバを呼んでいない");

    printf("== path が NULL / 空 ==\n");
    reset(1);
    check(vfs_set_mtime(0, 1UL) == VFS_ERR_INVAL, "NULL は INVAL");
    check(vfs_set_mtime("", 1UL) == VFS_ERR_INVAL, "空文字列は INVAL");
    check(drv_set_calls == 0, "ドライバを呼んでいない");

    printf("== マウントが無ければ NOMOUNT ==\n");
    {
        int i;
        num_fs = 0;
        for (i = 0; i < VFS_MAX_FS; i++)
            kmemset(&mounts[i], 0, sizeof(mounts[i]));
    }
    check(vfs_set_mtime("/nowhere/a.bin", 1UL) == VFS_ERR_NOMOUNT, "NOMOUNT");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
