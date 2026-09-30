/* ======================================================================== */
/*  CFG_BACKEND.C — ゲスト側の KAPI 境界                                     */
/*                                                                          */
/*  libos32cfg が呼ぶ KAPI をここだけに集める。ホスト TDD はこの翻訳単位を    */
/*  リンクせず、自前の cfg_backend_platform() を与えて実 kapi_db.c へ向ける。*/
/* ======================================================================== */

#include "cfg_internal.h"

/* crt0_c.c が設定する KernelAPI テーブル。 */
extern KernelAPI *kapi;

static int be_db_open_existing(const char *p, int w) { return kapi->db_open_existing(p, w); }
static int be_db_prepare_only(int h, const char *s)   { return kapi->db_prepare_only(h, s); }
static int be_db_bind_int(int h, int i, int v)        { return kapi->db_bind_int(h, i, v); }
static int be_db_bind_text(int h, int i, const char *s, int n)
{ return kapi->db_bind_text(h, i, s, n); }
static int be_db_bind_blob(int h, int i, const void *p, int n)
{ return kapi->db_bind_blob(h, i, p, n); }
static int be_db_bind_null(int h, int i)              { return kapi->db_bind_null(h, i); }
static int be_db_error_code(int h)                    { return kapi->db_error_code(h); }
static int be_db_open(const char *p)                  { return kapi->db_open(p); }
static int be_db_close(int h)                         { return kapi->db_close(h); }
static int be_db_exec(int h, const char *s)           { return kapi->db_exec(h, s); }
static int be_db_step(int h)                          { return kapi->db_step(h); }
static int be_db_finalize(int h)                      { return kapi->db_finalize(h); }
static unsigned char *be_shm(void)  { return (unsigned char *)kapi->shm_base; }
static int be_stat(const char *p, OS32_Stat *st)      { return kapi->sys_stat(p, st); }
static int be_rename(const char *a, const char *b)    { return kapi->sys_rename(a, b); }
static int be_unlink(const char *p)                   { return kapi->sys_unlink(p); }
static int be_open(const char *p, int m)              { return kapi->sys_open(p, m); }
static int be_read(int fd, void *b, u32 n)            { return kapi->sys_read(fd, b, n); }
static int be_lseek(int fd, int off, int wh)          { return kapi->sys_lseek(fd, off, wh); }
static void be_close_fd(int fd)                       { kapi->sys_close(fd); }
static u32 be_tick(void)                              { return kapi->get_tick(); }

static const CfgBackend cfg_kapi_backend = {
    be_db_open_existing,
    be_db_prepare_only,
    be_db_bind_int,
    be_db_bind_text,
    be_db_bind_blob,
    be_db_bind_null,
    be_db_error_code,
    be_db_open,
    be_db_close,
    be_db_exec,
    be_db_step,
    be_db_finalize,
    be_shm,
    be_stat,
    be_rename,
    be_unlink,
    be_open,
    be_read,
    be_lseek,
    be_close_fd,
    be_tick
};

const CfgBackend *cfg_backend_platform(void)
{
    return &cfg_kapi_backend;
}
