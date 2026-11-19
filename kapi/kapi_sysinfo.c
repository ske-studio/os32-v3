#include "os32_kapi_shared.h"
#include "vfs.h"
#include "range_internal.h"
#include "lib/kstring.h"
#include "platform_build_info.h"
#include "sys.h"
#include "exec.h"
#include "kapi_sysinfo.h"

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

/* Copy this translation unit's kernel build time without diagnostic output. */
void kapi_sys_build_time_copy(char *buf, u32 size)
{
    kstrncpy(buf, __DATE__ " " __TIME__, size);
}

/* カーネルビルド時の日時文字列を返す */
void kapi_sys_get_build_info(char *buf, int size)
{
    /* int の size を無検証で u32 に渡すと負値が巨大長に化ける */
    if (!buf || size <= 0) return;
    kapi_sys_build_time_copy(buf, (u32)size);
    platform_build_info_report();
}

/* ======================================================================== */
/*  sys_time_now — µs 時計 (票 TASK_HAL_WIRING §1-5 / KAPI v59)             */
/*                                                                          */
/*  生成される __cdecl ラッパ (`wrap_sys_time_now`) がここを呼ぶ ([ABI])。    */
/*  本体 (スナップショットの採り方) は kernel/time/ktime.c、判定と算数は          */
/*  kernel/time/time_math.c。ここの仕事は CPL=3 のポインタ検証だけ。             */
/*                                                                          */
/*  **検証の範囲は限定** (往復 9 の中継 1)。帯の外の番地はディスパッチャの   */
/*  早期検査 (`kapi_argptr` + `ring3_ptr_ok`) が他の KAPI と同じく           */
/*  `ring3_fault_kill` する。非 present / 書けないページへの書き込みで出る    */
/*  #PF も既存のフォールトガードがアプリの死として扱う — この KAPI だけ      */
/*  特別扱いしない。ここが「負を返して出力を 1 バイトも書かない」と          */
/*  約束するのは、早期検査を通った後に**この関数自身が判定できる**3 つだけ:  */
/*    (1) lo / hi が NULL                                                   */
/*    (2) どちらかの 4 バイトが帯の境界を跨ぐ                                */
/*    (3) 2 本の 4 バイト範囲が**交差する** (lo == hi だけでなく差 1〜3 も。  */
/*        重なっていると「上下が同じスナップショット」が壊れる)             */
/* ======================================================================== */

int kapi_sys_time_now(u32 *lo, u32 *hi)
{
    u32 a, b, diff;
    u32 snap_lo = 0, snap_hi = 0;
    int rc;

    if (!lo || !hi) return OS32_ERR_INVAL;

    a = (u32)lo;
    b = (u32)hi;
    diff = (a < b) ? (b - a) : (a - b);
    if (diff < (u32)sizeof(u32)) return OS32_ERR_INVAL;   /* 交差 (lo == hi 込み) */

    /* **書く前に 2 本とも検証する。** 2 本目だけ不正なときに 1 本目を
     * 書いてしまうと、呼び手は「負が返ったのに片方だけ新しい」を見る。 */
    if (!kapi_user_range_ok(lo, (u32)sizeof(u32))) return OS32_ERR_INVAL;
    if (!kapi_user_range_ok(hi, (u32)sizeof(u32))) return OS32_ERR_INVAL;

    /* **読み取り専用の USER ページ (共有ライブラリの .text) は kill** —
     * 負ではない (往復 10)。OS32 は CR0.WP = 0 なので、ここで止めないと
     * 全アプリ共有のコードをカーネルが書き換える。出力引数にコード番地を
     * 渡すのはアプリのバグで、他の KAPI の帯違反と同じ扱いにする。
     * `ring3_fault_kill()` は戻らない (longjmp)。 */
    if (!ring3_user_ranges_writable((u32)lo, (u32)sizeof(u32),
                                    (u32)hi, (u32)sizeof(u32))) {
        ring3_fault_kill();
    }

    /* ローカルの 1 スナップショットへ受けてから 2 本へ写す
     * (途中で時計が進んでも上下が食い違わない)。 */
    rc = sys_time_now(&snap_lo, &snap_hi);
    if (rc != 0) return rc;                 /* 負のときは出力を書かない */
    *lo = snap_lo;
    *hi = snap_hi;
    return 0;
}
