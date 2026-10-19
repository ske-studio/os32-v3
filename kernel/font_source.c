/* ========================================================================
 *  font_source.c — font の read lease 表 (T7b-b1、設計 §2-1・§2-3・§2-4・§3-2)
 *
 *  app の入口 (public KAPI font_source_open / read_at / close、v77) と
 *  常駐 shell の入口 (exec/shell_service.c の wrapper が段 A/B の後に呼ぶ
 *  段 C/D) の実体。FD の読み書きは fs/vfs_fd.c の font lease API
 *  (vfs_open_font / vfs_validate_font / vfs_font_read_at / vfs_close_font)
 *  だけで行い、FD 番号は呼び手に見せない。
 *
 *  回収: app の通常終了 / fault は exec_reclaim_resources が
 *  font_lease_close_owned(id) を vfs_close_owned(id) の直前に呼ぶ (結線は
 *  S3b)。shell の lease は shell_service_retire → font_lease_close_shell。
 *
 *  ホスト試験は tools/tests/font_source_host.c (fs/vfs_fd.c と一緒に実物で)。
 * ======================================================================== */
#include "font_source.h"
#include "exec.h"           /* ring3_call_from_user / ring3_fault_kill / OS32_ERR_* */
#include "redir_access.h"   /* caller_access_get / copy_caller_cstr */
#include "fd_redirect.h"    /* res_owner_get */
#include "gui.h"            /* GUI_SHELL_OWNER */

static FontLease g_font_leases[FONT_LEASE_SLOTS];

volatile u32 font_lease_reject_count = 0;
volatile u32 font_lease_reject_last = 0;

static i32 font_lease_reject(u32 reason, i32 rc)
{
    font_lease_reject_count++;
    font_lease_reject_last = reason;
    return rc;
}

static u32 font_lease_id(u32 idx)
{
    return (g_font_leases[idx].id_gen << FONT_LEASE_IDX_BITS) | idx;
}

static int font_lease_count(u8 kind, int owner, int any_owner)
{
    u32 i;
    int n = 0;
    for (i = 0; i < FONT_LEASE_SLOTS; i++) {
        const FontLease *s = &g_font_leases[i];
        if (s->kind == kind && (any_owner || s->vfs.owner == owner)) n++;
    }
    return n;
}

/* 空き枠 (FREE かつ id_gen が満了していない)。無ければ -1。 */
static int font_lease_free_slot(void)
{
    u32 i;
    for (i = 0; i < FONT_LEASE_SLOTS; i++) {
        if (g_font_leases[i].kind == FONT_LEASE_FREE &&
            g_font_leases[i].id_gen < FONT_LEASE_GEN_MAX) return (int)i;
    }
    return -1;
}

/* VFS で開いて枠に載せる。成功で 0 と id / size。 */
static i32 font_lease_install(u8 kind, const char *path, int owner, u32 flags,
                              u32 owner_gen, u32 *out_id, u32 *out_size)
{
    VfsFontLease l;
    u32 size = 0;
    int idx = font_lease_free_slot();
    int rc;
    FontLease *s;
    if (idx < 0) return OS32_ERR_NOSPC;
    rc = vfs_open_font(path, owner, flags, &l, &size);
    if (rc != VFS_OK) return rc;
    s = &g_font_leases[idx];
    if (s->id_gen == 0) s->id_gen = 1;   /* id 0 を出さない */
    s->kind = kind;
    s->vfs = l;
    s->owner_gen = owner_gen;
    s->file_size = size;
    *out_id = font_lease_id((u32)idx);
    *out_size = size;
    return 0;
}

/* VFS の FD を閉じて枠を空け、id_gen を進める (旧 id は以後 3b で STALE)。
 * 戻り = vfs_close_font の値 (失効でも一致すれば OK)。INVAL (FD が lease の
 * 知らない経路で既に解放・再利用されている) でも他人の FD は閉じず、枠は
 * 空ける — 持ち主が二度と閉じられない枠を残さない。 */
static i32 font_lease_release(FontLease *s)
{
    i32 rc = vfs_close_font(&s->vfs);
    s->kind = FONT_LEASE_FREE;
    s->vfs.fd = -1;
    s->vfs.generation = 0;
    s->vfs.owner = 0;
    s->owner_gen = 0;
    s->file_size = 0;
    s->id_gen++;
    return rc;
}

/* ---- app (public KAPI) ----------------------------------------------- */

/* 門 1・2。通れば *c に caller を入れて 0。 */
static i32 font_lease_app_gate(struct caller_access *c)
{
    if (ring3_call_from_user() == 0)
        return font_lease_reject(FONT_LEASE_REJ_NOT_USER, OS32_ERR_INVAL);
    if (!caller_access_get(c) || c->origin != CALLER_USER ||
        (int)c->owner != res_owner_get())
        return font_lease_reject(FONT_LEASE_REJ_CALLER, OS32_ERR_INVAL);
    return 0;
}

/* token の判定表 3a〜3e (設計 §2-3)。通れば *out に枠。 */
static i32 font_lease_app_token(u32 id, const struct caller_access *c,
                                FontLease **out)
{
    u32 idx = id & FONT_LEASE_IDX_MASK;
    u32 id_gen = id >> FONT_LEASE_IDX_BITS;
    FontLease *s;
    if (idx >= FONT_LEASE_SLOTS)
        return font_lease_reject(FONT_LEASE_REJ_IDX, OS32_ERR_INVAL);
    s = &g_font_leases[idx];
    if (s->id_gen != id_gen)
        return font_lease_reject(FONT_LEASE_REJ_ID_GEN, OS32_ERR_STALE);
    if (s->kind != FONT_LEASE_APP)
        return font_lease_reject(FONT_LEASE_REJ_KIND, OS32_ERR_INVAL);
    if (s->vfs.owner != (int)c->owner)
        return font_lease_reject(FONT_LEASE_REJ_OWNER, OS32_ERR_INVAL);
    if (s->owner_gen != c->generation)
        return font_lease_reject(FONT_LEASE_REJ_OWNER_GEN, OS32_ERR_STALE);
    *out = s;
    return 0;
}

i32 font_lease_app_open(const char *path, u32 *out_id, u32 *out_size)
{
    struct caller_access c;
    char kpath[VFS_MAX_PATH];
    i32 rc = font_lease_app_gate(&c);
    if (rc) return rc;
    if (!out_id || !out_size) return OS32_ERR_INVAL;
    if (!copy_caller_cstr(&c, path, kpath, sizeof(kpath))) {
        /* USER の不正な入力ポインタ (NUL が範囲内に無い・写せない) */
        ring3_fault_kill();
        return font_lease_reject(FONT_LEASE_REJ_PATH, OS32_ERR_INVAL);
    }
    if (font_lease_count(FONT_LEASE_APP, (int)c.owner, 0) >= VFS_FONT_FD_MAX_APP ||
        font_lease_count(FONT_LEASE_APP, 0, 1) >= VFS_FONT_FD_MAX_APP_TOTAL)
        return OS32_ERR_NOSPC;
    return font_lease_install(FONT_LEASE_APP, kpath, (int)c.owner,
                              VFS_FONT_OPEN_PIN, c.generation, out_id, out_size);
}

i32 font_lease_app_read_at(u32 id, u32 offset, void *dst, u32 len)
{
    struct caller_access c;
    FontLease *s = 0;
    i32 rc = font_lease_app_gate(&c);
    if (rc) return rc;
    rc = font_lease_app_token(id, &c, &s);
    if (rc) return rc;
    return vfs_font_read_at(&s->vfs, offset, dst, len);
}

i32 font_lease_app_close(u32 id)
{
    struct caller_access c;
    FontLease *s = 0;
    i32 rc = font_lease_app_gate(&c);
    if (rc) return rc;
    rc = font_lease_app_token(id, &c, &s);
    if (rc) return rc;
    return font_lease_release(s);
}

void font_lease_close_owned(int owner)
{
    u32 i;
    for (i = 0; i < FONT_LEASE_SLOTS; i++) {
        FontLease *s = &g_font_leases[i];
        if (s->kind == FONT_LEASE_APP && s->vfs.owner == owner)
            (void)font_lease_release(s);
    }
}

/* ---- 常駐 shell (段 C / 段 D) ----------------------------------------- */

/* 段 C: kind==SHELL・id_gen 一致・owner_gen が現 shell 登録世代。 */
static i32 font_lease_shell_token(u32 id, u32 shell_gen, FontLease **out)
{
    u32 idx = id & FONT_LEASE_IDX_MASK;
    FontLease *s;
    if (idx >= FONT_LEASE_SLOTS) return OS32_ERR_INVAL;
    s = &g_font_leases[idx];
    if (s->id_gen != (id >> FONT_LEASE_IDX_BITS)) return OS32_ERR_STALE;
    if (s->kind != FONT_LEASE_SHELL) return OS32_ERR_INVAL;
    if (s->owner_gen != shell_gen) return OS32_ERR_STALE;
    *out = s;
    return 0;
}

i32 font_lease_shell_open(const char *path, u32 shell_gen, u32 *out_id, u32 *out_size)
{
    if (!path || !out_id || !out_size || shell_gen == 0) return OS32_ERR_INVAL;
    if (font_lease_count(FONT_LEASE_SHELL, 0, 1) >= VFS_FONT_FD_MAX_SHELL)
        return OS32_ERR_NOSPC;
    return font_lease_install(FONT_LEASE_SHELL, path, GUI_SHELL_OWNER,
                              VFS_FONT_OPEN_PROTECT | VFS_FONT_OPEN_PIN,
                              shell_gen, out_id, out_size);
}

i32 font_lease_shell_read_at(u32 id, u32 shell_gen, u32 offset, void *dst, u32 len)
{
    FontLease *s = 0;
    i32 rc = font_lease_shell_token(id, shell_gen, &s);
    if (rc) return rc;
    return vfs_font_read_at(&s->vfs, offset, dst, len);
}

i32 font_lease_shell_validate(u32 id, u32 shell_gen)
{
    FontLease *s = 0;
    i32 rc = font_lease_shell_token(id, shell_gen, &s);
    if (rc) return rc;
    return vfs_validate_font(&s->vfs);
}

i32 font_lease_shell_close(u32 id, u32 shell_gen)
{
    FontLease *s = 0;
    i32 rc = font_lease_shell_token(id, shell_gen, &s);
    if (rc) return rc;
    return font_lease_release(s);
}

void font_lease_close_shell(void)
{
    u32 i;
    for (i = 0; i < FONT_LEASE_SLOTS; i++) {
        FontLease *s = &g_font_leases[i];
        if (s->kind == FONT_LEASE_SHELL) (void)font_lease_release(s);
    }
}
