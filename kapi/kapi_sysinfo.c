#include "os32_kapi_shared.h"
#include "vfs.h"
#include "range_internal.h"

/* ======================================================================== */
/*  sys_set_mtime — 更新日時の設定 (票 H3 / KAPI v52)                        */
/*                                                                          */
/*  生成される __cdecl ラッパ (`wrap_sys_set_mtime`) がここを呼ぶ ([ABI])。    */
/*                                                                          */
/*  ここの仕事は 2 つだけ:                                                   */
/*    (1) CPL=3 のポインタ検証 (kapi_host.c / kapi_db.c と同じ 2 段の 2 段目。 */
/*        **先頭番地が帯外ならここへ来る前にディスパッチャが kill する** ―    */
/*        `kapi_argptr` による早期検査。NUL がどこにあるかは分からないので     */
/*        1 バイトずつ確かめながらカーネル側へ写す)                          */
/*    (2) VFS への受け渡し                                                   */
/*                                                                          */
/*  時刻の意味づけ・FS ごとの可否は `fs/vfs.c` の `vfs_set_mtime` が持つ。    */
/*  非対応の FS は `OS32_ERR_NOSYS` (失敗ではなく「持っていない」)。          */
/* ======================================================================== */

/* 検証後の写し先。KAPI は再入しない (CPL=3 の呼び手は 1 本ずつ)。 */
static char set_mtime_path[OS32_MAX_PATH];


/* 上限 cap (NUL 込み) の中で NUL を探しながら dst へ写す。
 * 1 バイトずつ検証するので、途中のページが非 present でもそこで止まる。
 * 戻り値: 1 = 写した / 0 = NULL・帯外・cap 内に NUL が無い (切り詰めない)。 */
static int set_mtime_user_str_copy(const char *src, char *dst, u32 cap)
{
    u32 i;

    if (!src) return 0;
    for (i = 0; i < cap; i++) {
        if (!kapi_user_range_ok(src + i, 1)) return 0;
        dst[i] = src[i];
        if (dst[i] == '\0') return 1;
    }
    return 0;                               /* 切り詰めた別のパスを作らない */
}

int kapi_sys_set_mtime(const char *path, u32 mtime)
{
    if (!path) return OS32_ERR_INVAL;
    /* 0 は「不明」の印。書かせない (vfs_set_mtime でも断るが、
     * 引数不正はここで返して VFS を呼ばない) */
    if (mtime == 0) return OS32_ERR_INVAL;
    if (!set_mtime_user_str_copy(path, set_mtime_path,
                                 (u32)sizeof(set_mtime_path)))
        return OS32_ERR_INVAL;
    return vfs_set_mtime(set_mtime_path, (os_time_t)mtime);
}
