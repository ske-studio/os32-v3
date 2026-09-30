/* 2026-09-10 の ext2 スーパーブロック巻き戻しの回帰試験。
 * 実物の fs/vfs.c と fs/ext2_vfs.c を取り込み、境界 (ext2 本体・kmalloc) だけを
 * 差し替える。ext2_mount() が受け取ったデバイス番号を記録して、fd0 の
 * マウント要求が hd0 に化けていないことを確かめる。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfs.h"
#include "ext2_priv.h"
#include "../../fs/vfs.c"
/* fs/vfs_fd.c の FD 表側 (票 TASK_VFS_FD_PATH)。この試験は FD を開かない */
void vfs_fd_invalidate_ino(void *c, u32 i) { (void)c; (void)i; }
void vfs_fd_invalidate_mount(void *c) { (void)c; }
int vfs_fd_rename_busy(void *c, const char *a, const char *b)
{ (void)c; (void)a; (void)b; return 0; }
int vfs_fd_pinned_busy(void *c, int h, u32 i, const char *r)
{ (void)c; (void)h; (void)i; (void)r; return 0; }

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)

/* ---- ext2 本体の境界 (mount/unmount/sync のみ意味を持つ) ---- */
static int ext2_mount_calls;
static int ext2_mount_drive[8];   /* ext2_mount() が受け取った ide_drive */
static int ext2_mount_rc = EXT2_OK;
static int ext2_sync_calls;

int ext2_mount(Ext2Ctx *ctx, int ide_drive)
{
    if (ext2_mount_calls < 8) ext2_mount_drive[ext2_mount_calls] = ide_drive;
    ext2_mount_calls++;
    if (ext2_mount_rc != EXT2_OK) return ext2_mount_rc;
    ctx->mounted = 1;
    ctx->drive_num = ide_drive;
    return EXT2_OK;
}
void ext2_unmount(Ext2Ctx *ctx) { ctx->mounted = 0; }
int ext2_is_mounted_ctx(Ext2Ctx *ctx) { return ctx->mounted; }
int ext2_sync(Ext2Ctx *ctx) { (void)ctx; ext2_sync_calls++; return EXT2_OK; }

/* stat 経路の境界。stub_ino != 0 のとき「どのデバイスでも、どのパスでも
 * 同じ inode 番号」を返す — 別ディスクで inode が衝突した状況の再現。
 * stub_inode_rc != 0 なら read_inode を落として stat の合成経路へ回す。 */
static u32 stub_ino;
static int stub_inode_rc;

int ext2_create(Ext2Ctx *c, u32 d, const char *n, const void *b, u32 s)
{ (void)c; (void)d; (void)n; (void)b; (void)s; return -1; }
int ext2_find_entry(Ext2Ctx *c, u32 d, const char *n, u32 *o, u8 *t)
{ (void)c; (void)d; (void)n; (void)o; (void)t; return -1; }
int ext2_get_size_ino(Ext2Ctx *c, u32 i, u32 *s)
{ (void)c; (void)i; (void)s; return -1; }
int ext2_list_dir(Ext2Ctx *c, u32 d, ext2_dir_callback cb, void *u)
{ (void)c; (void)d; (void)cb; (void)u; return -1; }
int ext2_lookup(Ext2Ctx *c, const char *p, u32 *i)
{
    (void)c; (void)p;
    if (!stub_ino) return -1;
    *i = stub_ino;
    return 0;
}
int ext2_mkdir(Ext2Ctx *c, u32 d, const char *n)
{ (void)c; (void)d; (void)n; return -1; }
/* 解決済み経路の記憶 (票 S6-P)。この試験は毎回 ext2_lookup を通したいので、
 * 記憶は常に外し、書き込みも捨てる。 */
int ext2_path_memo_get(Ext2Ctx *c, const char *p, u32 *i)
{ (void)c; (void)p; (void)i; return EXT2_ERR_NOTFOUND; }
void ext2_path_memo_put(Ext2Ctx *c, const char *p, u32 i)
{ (void)c; (void)p; (void)i; }
int ext2_read_file(Ext2Ctx *c, u32 i, void *b, u32 m)
{ (void)c; (void)i; (void)b; (void)m; return -1; }
int ext2_read_inode(Ext2Ctx *c, u32 i, Ext2Inode *o)
{
    (void)c;
    if (!stub_ino || stub_inode_rc != 0) return -1;
    memset(o, 0, sizeof(*o));
    o->mode = 0100644;   /* 通常ファイル */
    o->links_count = 1;
    o->size = 4;
    (void)i;
    return 0;
}
int ext2_read_stream(Ext2Ctx *c, u32 i, void *b, u32 s, u32 o)
{ (void)c; (void)i; (void)b; (void)s; (void)o; return -1; }
int ext2_rmdir(Ext2Ctx *c, u32 d, const char *n)
{ (void)c; (void)d; (void)n; return -1; }
int ext2_unlink(Ext2Ctx *c, u32 d, const char *n)
{ (void)c; (void)d; (void)n; return -1; }
int ext2_write(Ext2Ctx *c, u32 i, const void *d, u32 s)
{ (void)c; (void)i; (void)d; (void)s; return -1; }
int ext2_write_stream(Ext2Ctx *c, u32 i, const void *b, u32 s, u32 o)
{ (void)c; (void)i; (void)b; (void)s; (void)o; return -1; }

int ext2_rename(Ext2Ctx *c, u32 od, const char *on, u32 nd, const char *nn)
{ (void)c; (void)od; (void)on; (void)nd; (void)nn; return -1; }

/* ---- 票 H3 で ext2_vfs.c が使うようになった 2 本 ----
 * ext2_vfs_set_mtime() が read_inode -> write_inode -> sync と進むので、
 * 書き戻し先と時刻の出どころが要る。この試験の対象は**デバイス番号の
 * エンコード**なので、中身は他の境界と同じく成功を返すだけにする。
 * set_mtime そのものの規則は tools/tests/vfs_set_mtime_host.c が見る。 */
int ext2_write_inode(Ext2Ctx *c, u32 i, const Ext2Inode *o)
{ (void)c; (void)i; (void)o; return EXT2_OK; }
/* 本物 (fs/ext2_super.c) と同じく定数を返す。ゲスト側の「いま」の代わり。 */
u32 ext2_current_time(void) { return 0x67E8E800UL; }
/* 票 B8 往復 5: 書き込み系の入口の拒否 (エラー状態、fs/ext2_super.c)。
 * この試験は mount 経路だけを見るので、エラー状態には入らない贋物 */
int ext2_check_writable(Ext2Ctx *c) { (void)c; return EXT2_OK; }

void *kzalloc(u32 size) { return calloc(1, size); }
void kfree(void *p) { free(p); }

/* lib/kstring_asm.asm の i386 実装はホストで組めないので同義の C で置く。
 * kstrncpy / kstrncat の n はバッファ全体サイズ (BSD strlcpy 相当)。 */
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

#include "../../fs/ext2_vfs.c"

static void reset(void)
{
    int i;
    for (i = 0; i < VFS_MAX_FS; i++) mounts[i].in_use = 0;
    num_fs = 0;
    ext2_mount_calls = 0;
    ext2_sync_calls = 0;
    ext2_mount_rc = EXT2_OK;
    stub_ino = 0;
    stub_inode_rc = 0;
    ext2_init();
    CHECK(num_fs == 1);
}

/* ops->mount() に渡る値が (dev_type << 8) | dev_id であること。 */
static void encode(void)
{
    reset();
    CHECK(vfs_mount("/", "hd0", "ext2") == VFS_OK);
    CHECK(ext2_mount_calls == 1);
    CHECK(ext2_mount_drive[0] == 0);
    CHECK(VFS_MOUNT_DEV_ENCODE(VFS_DEV_HD, 0) == 0x000);
    CHECK(VFS_MOUNT_DEV_ENCODE(VFS_DEV_FD, 0) == 0x100);
    CHECK(VFS_MOUNT_DEV_ENCODE(VFS_DEV_CD, 1) == 0x301);
    CHECK(VFS_MOUNT_DEV_TYPE(0x301) == VFS_DEV_CD);
    CHECK(VFS_MOUNT_DEV_ID(0x301) == 1);
}

/* 本丸: ext2 は IDE 専用。fd0 / cd0 / hostdrv を hd に化けさせない。 */
static void ext2_rejects_non_hd(void)
{
    reset();
    CHECK(ext2_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_FD, 0)) == (void *)0);
    CHECK(ext2_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_CD, 0)) == (void *)0);
    CHECK(ext2_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_SERIAL, 0)) == (void *)0);
    CHECK(ext2_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_HOSTDRV, 0)) == (void *)0);
    /* 拒否は ext2 本体に届く前に済ませる。届いていたら hd0 を開いている。 */
    CHECK(ext2_mount_calls == 0);
    {
        void *ec = ext2_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_HD, 1));
        CHECK(ec != (void *)0);
        CHECK(ext2_mount_calls == 1 && ext2_mount_drive[0] == 1);
        ext2_vfs_umount(ec);
    }
}

/* ブート時の自動マウント列を再現する。fd0 への ext2 マウントが通ると
 * hd0 の同じパーティションに 2 つ目の Ext2Ctx ができ、vfs_sync() が
 * マウント時点の空き数を書き戻して e2fsck が壊れる。 */
static void boot_sequence_has_one_ext2(void)
{
    int i, ext2_mounts = 0;
    reset();
    CHECK(vfs_mount("/", "hd0", "ext2") == VFS_OK);
    /* kernel.c の自動マウントループ相当 */
    CHECK(vfs_mount("/fd0", "fd0", "ext2") != VFS_OK);
    CHECK(vfs_mount("/cd0", "cd0", "ext2") != VFS_OK);
    for (i = 0; i < VFS_MAX_FS; i++)
        if (mounts[i].in_use && mounts[i].ops == &ext2_ops) ext2_mounts++;
    CHECK(ext2_mounts == 1);
    CHECK(ext2_mount_calls == 1 && ext2_mount_drive[0] == 0);
    CHECK(vfs_sync() == VFS_OK);
    CHECK(ext2_sync_calls == 1);
}

/* 同じ (FS, 種別, unit) の二重マウントは、FS 本体を呼ぶ前に断る。 */
static void duplicate_device_refused(void)
{
    reset();
    CHECK(vfs_mount("/", "hd0", "ext2") == VFS_OK);
    CHECK(vfs_mount("/again", "hd0", "ext2") == VFS_ERR_EXIST);
    CHECK(ext2_mount_calls == 1);
    /* 別 unit は通る */
    CHECK(vfs_mount("/hd1", "hd1", "ext2") == VFS_OK);
    CHECK(ext2_mount_calls == 2 && ext2_mount_drive[1] == 1);
    CHECK(vfs_sync() == VFS_OK);
    CHECK(ext2_sync_calls == 2);
}

/* 別デバイスで inode 番号が衝突しても stat の (st_dev, st_ino) は衝突しない。
 * FS ドライバは st_dev を埋めない (ext2/iso9660 は 0 固定) ので、VFS が
 * マウント単位の値を上書きしないと、filer の同一ファイル判定が別ディスクの
 * 別ファイルを「同じファイル」と見て正当な上書きコピーを拒む。 */
static void stat_dev_identifies_mount(void)
{
    OS32_Stat a, b, c;

    reset();
    stub_ino = 12;
    CHECK(vfs_mount("/", "hd0", "ext2") == VFS_OK);
    CHECK(vfs_mount("/hd1", "hd1", "ext2") == VFS_OK);

    CHECK(vfs_stat("/a", &a) == VFS_OK);
    CHECK(vfs_stat("/hd1/a", &b) == VFS_OK);
    CHECK(vfs_stat("/b", &c) == VFS_OK);

    /* 前提: inode 番号は等しい (これが衝突の種) */
    CHECK(a.st_ino == 12 && b.st_ino == 12 && c.st_ino == 12);
    /* 修正前はどれも 0 で、a と b が「同じファイル」に見えた */
    CHECK(a.st_dev != 0 && b.st_dev != 0 && c.st_dev != 0);
    CHECK(a.st_dev != b.st_dev);   /* 別デバイス → 別ファイル */
    CHECK(a.st_dev == c.st_dev);   /* 同一マウント内の 2 パスは同値 */
    /* 値はマウントのデバイス指定 + 1 (0 は「不明」に予約) */
    CHECK(a.st_dev == (u32)VFS_MOUNT_DEV_ENCODE(VFS_DEV_HD, 0) + 1U);
    CHECK(b.st_dev == (u32)VFS_MOUNT_DEV_ENCODE(VFS_DEV_HD, 1) + 1U);
}

/* マウントルートの合成 stat も同じ規則で埋める (FS が stat を落とす経路)。 */
static void stat_dev_on_synth_root(void)
{
    OS32_Stat r0, r1;

    reset();
    stub_ino = 12;
    stub_inode_rc = -1;            /* ext2 の stat を失敗させる */
    CHECK(vfs_mount("/", "hd0", "ext2") == VFS_OK);
    CHECK(vfs_mount("/hd1", "hd1", "ext2") == VFS_OK);

    CHECK(vfs_stat("/", &r0) == VFS_OK);
    CHECK(vfs_stat("/hd1", &r1) == VFS_OK);
    CHECK(r0.st_dev == (u32)VFS_MOUNT_DEV_ENCODE(VFS_DEV_HD, 0) + 1U);
    CHECK(r1.st_dev == (u32)VFS_MOUNT_DEV_ENCODE(VFS_DEV_HD, 1) + 1U);
    CHECK(r0.st_dev != r1.st_dev);
    /* マウントの下のパスはそのマウントの値 ("/hd1/..." は hd1 側) */
    CHECK(vfs_path_dev("/hd1/deep/x") == r1.st_dev);
    CHECK(vfs_path_dev("/deep/x") == r0.st_dev);

    /* どこにもマウントが無ければ 0 (「不明」) */
    reset();
    CHECK(vfs_path_dev("/x") == 0);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "encode")) encode();
    else if (!strcmp(c, "ext2_rejects_non_hd")) ext2_rejects_non_hd();
    else if (!strcmp(c, "boot_sequence_has_one_ext2")) boot_sequence_has_one_ext2();
    else if (!strcmp(c, "duplicate_device_refused")) duplicate_device_refused();
    else if (!strcmp(c, "stat_dev_identifies_mount")) stat_dev_identifies_mount();
    else if (!strcmp(c, "stat_dev_on_synth_root")) stat_dev_on_synth_root();
    else { fprintf(stderr, "unknown case: %s\n", c); return 2; }
    return 0;
}
