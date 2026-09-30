/* ======================================================================== */
/*  HDATE.C — ホスト (WSL2) の時刻を 1 行表示する (票 N3 §2)                 */
/*                                                                          */
/*  libos32host の host_time を叩くだけ。RTC は設定しない (表示のみ — KAPI に */
/*  rtc_write が無い、内蔵 `date -sync` は将来のシェル票)。                   */
/*  終了コード: 0 成功 / 2 リンク (host link down / timeout / NIC 無し)。     */
/*  ホスト TDD は -DHOST_TEST (tools/tests/host_cmd_host.c)。                 */
/* ======================================================================== */

#ifdef HOST_TEST
#include <stdio.h>
#include <string.h>
#else
#include "os32api.h"
#include <stdio.h>
#include <string.h>
#endif
#include "libos32host.h"

/* 終了コード ([C4]) */
#define HDATE_OK    0
#define HDATE_LINK  2

int main(int argc, char **argv
#ifndef HOST_TEST
         , KernelAPI *api
#endif
         )
{
    char t[HOST_TIME_BUF];
    int rc;

#ifndef HOST_TEST
    (void)api;   /* 局所 I/O は無い。host_time は大域 kapi を使う (crt0 設定) */
#endif
    (void)argc;
    (void)argv;

    rc = host_time(t);
    if (rc == 0) {
        printf("%s\n", t);
        return HDATE_OK;
    }
    if (rc == HOST_ENODEV) printf("hdate: no host link (no NIC)\n");
    else if (rc == HOST_ETIMEOUT) printf("hdate: host timeout\n");
    else printf("hdate: host link down\n");
    return HDATE_LINK;
}
