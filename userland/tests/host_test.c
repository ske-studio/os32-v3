/* ======================================================================== */
/*  HOST_TEST.C — KAPI v51 (Host Services) の CPL=3 受入 (票 N1 段 5)         */
/*                                                                          */
/*  ここで踏むのは「CPL=3 から実機で呼んだときだけ分かること」だけ。          */
/*  ワイヤの反例 (欠落・重複・墓標・再同期) はホスト試験                      */
/*  (tools/tests/test_net_link.py / test_host_agent.py) が見る。             */
/*                                                                          */
/*    1. `GET /pattern/65536` — AGAIN ループで受け切り、内容が一致する        */
/*    2. `GET /notfound` — 業務結果 404                                      */
/*    3. `TIME` — 19B の本文                                                 */
/*    4. `ECHO 5` + `host_write` — WDATA が折り返して戻る                    */
/*    5. 不正な引数 (0 長 / 1400B 超 / 二重 close) が負値で断られ、           */
/*       **この試験プログラムが落ちない**                                     */
/*    6. `host_test stale` — Agent を再起動した後に走らせると                 */
/*       STALE → close → open が通る (運用者が Agent を落として使う)         */
/*                                                                          */
/*  使い方: `host_test [stale]`。合否は最後の集計行と終了コードで読む         */
/*  (票 docs/archive/test/TASK_TEST_RESULT.md §2)。Agent が居ない・カーネルが   */
/*  古いのは「不合格」ではなく「実行しなかった」なので SKIP (終了コード 2)。  */
/*  WSL2 側で `python3 tools/host_agent.py` が動いていることが前提。          */
/* ======================================================================== */

#include "os32api.h"
#include "rt/testresult.h"

static int passed;
static int failed;
static KernelAPI *g;

static void ok(int cond, const char *name);
static unsigned int slen(const char *s);
static int svc(const char *req, u32 *status, u32 *len, u8 *body, u32 cap, u32 *got);
static int wait_up(int ticks);

int main(int argc, char **argv, KernelAPI *api)
{
    static u8 body[1400];
    u32 status = 0, len = 0, got = 0;
    i32 h;
    int rc, i;
    int stale_mode = 0;

    g = api;
    passed = 0;
    failed = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == 's') stale_mode = 1;
    }

    api->kprintf(0xE1, "host_test: KAPI v%d\n", (int)api->version);
    if (api->version < 51) {
        return os32_test_summary_skip(api, "host_test",
                                      "kernel is older than KAPI v51");
    }

    /* ---- (0) リンクが立つまで待つ (link_tick が HELLO を通す) ---------- */
    if (!wait_up(600)) {
        return os32_test_summary_skip(
            api, "host_test",
            "no host agent (host_open keeps returning STALE)");
    }

    /* ---- (1) GET /pattern/65536 — AGAIN ループと内容一致 --------------- */
    {
        u32 off = 0;
        u32 bad = 0;
        h = api->host_open("GET /pattern/65536", 18);
        ok(h >= 0, "host_open(GET /pattern/65536)");
        if (h >= 0) {
            for (i = 0; i < 20000; i++) {
                rc = api->host_status(h, &status, &len);
                if (rc != OS32_ERR_AGAIN) break;
                api->sys_yield();
            }
            ok(rc == 0, "host_status returns the business result");
            ok(status == 200, "GET /pattern -> 200");
            ok(len == 65536, "GET /pattern -> 65536 bytes");
            for (i = 0; i < 200000; i++) {
                i32 n = api->host_read(h, body, sizeof(body));
                if (n == 0) break;
                if (n == OS32_ERR_AGAIN) { api->sys_yield(); continue; }
                if (n < 0) break;
                {
                    u32 k;
                    for (k = 0; k < (u32)n; k++)
                        if (body[k] != (u8)(off + k)) bad++;
                }
                off += (u32)n;
            }
            ok(off == 65536, "host_read consumed the whole body");
            ok(bad == 0, "the body matches the pattern");
            ok(api->host_read(h, body, sizeof(body)) == 0, "read after the end is 0");
            ok(api->host_close(h) == 0, "host_close");
        }
    }

    /* ---- (2) GET /notfound = 404 (業務結果) ---------------------------- */
    status = 0; len = 0; got = 0;
    rc = svc("GET /notfound", &status, &len, body, sizeof(body), &got);
    ok(rc == 0, "GET /notfound completes");
    ok(status == 404, "GET /notfound -> 404 (business status)");

    /* ---- (3) TIME ------------------------------------------------------ */
    status = 0; len = 0; got = 0;
    rc = svc("TIME", &status, &len, body, sizeof(body), &got);
    ok(rc == 0 && status == 200, "TIME -> 200");
    ok(len == 19 && got == 19, "TIME body is 19 bytes");
    if (got == 19) {
        body[19] = '\0';
        api->kprintf(0x07, "  host time = %s\n", (const char *)body);
    }

    /* ---- (4) ECHO 5 + host_write --------------------------------------- */
    h = api->host_open("ECHO 5", 6);
    ok(h >= 0, "host_open(ECHO 5)");
    if (h >= 0) {
        i32 n = -1;
        for (i = 0; i < 20000; i++) {
            n = api->host_write(h, "hello", 5);
            if (n != OS32_ERR_AGAIN) break;
            api->sys_yield();
        }
        ok(n == 5, "host_write accepted 5 bytes");
        ok(api->host_write(h, "x", 1) == OS32_ERR_INVAL,
           "writing past the declared length is INVAL");
        for (i = 0; i < 20000; i++) {
            rc = api->host_status(h, &status, &len);
            if (rc != OS32_ERR_AGAIN) break;
            api->sys_yield();
        }
        ok(rc == 0 && status == 200 && len == 5, "ECHO -> 200, 5 bytes");
        got = 0;
        for (i = 0; i < 20000; i++) {
            i32 k = api->host_read(h, body + got, sizeof(body) - got);
            if (k == 0) break;
            if (k == OS32_ERR_AGAIN) { api->sys_yield(); continue; }
            if (k < 0) break;
            got += (u32)k;
        }
        ok(got == 5 && body[0] == 'h' && body[4] == 'o', "ECHO body came back");
        ok(api->host_close(h) == 0, "host_close after ECHO");
    }

    /* ---- (5) 引数の検査で落ちない -------------------------------------- */
    ok(api->host_open("", 0) == OS32_ERR_INVAL, "0-length request is INVAL");
    ok(api->host_open("PING", 1401) == OS32_ERR_INVAL, "over 1400B is INVAL");
    ok(api->host_read(-1, body, 16) == OS32_ERR_INVAL, "read on a bad handle is INVAL");
    ok(api->host_status(99, &status, &len) == OS32_ERR_INVAL,
       "status on a bad handle is INVAL");
    ok(api->host_close(99) == OS32_ERR_INVAL, "close on a bad handle is INVAL");
    h = api->host_open("PING", 4);
    if (h >= 0) {
        ok(api->host_close(h) == 0, "close a fresh handle");
        ok(api->host_close(h) == OS32_ERR_INVAL, "double close is INVAL");
    }
    ok(api->host_read(0, (void *)0, 16) == OS32_ERR_INVAL, "NULL buf is INVAL");

    /* ---- (6) Agent 再起動 → STALE → close → open ----------------------- */
    if (stale_mode) {
        h = api->host_open("GET /pattern/4096", 17);
        ok(h >= 0, "host_open before the agent restart");
        api->kprintf(0x0E, "  restart the host agent now (waiting for STALE)...\n");
        rc = OS32_ERR_AGAIN;
        for (i = 0; i < 200000; i++) {
            rc = api->host_status(h, &status, &len);
            if (rc == OS32_ERR_STALE) break;
            api->sys_yield();
        }
        ok(rc == OS32_ERR_STALE, "the handle goes STALE after the agent restarts");
        ok(api->host_close(h) == 0, "a STALE handle can be closed");
        ok(wait_up(2000), "a new session comes up after the restart");
        status = 0; len = 0; got = 0;
        ok(svc("TIME", &status, &len, body, sizeof(body), &got) == 0 && status == 200,
           "a new request works after the restart");
    } else {
        api->kprintf(0x06, "  (skipped: agent-restart case — run `host_test stale`)\n");
    }

    return os32_test_summary(api, "host_test", passed, passed + failed);
}

static void ok(int cond, const char *name)
{
    if (cond) {
        passed++;
    } else {
        failed++;
        g->kprintf(0x41, "  FAIL: %s\n", name);
    }
}

static unsigned int slen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

/* リンクが立つまで待つ。open が STALE を返す間はセッション未確立。 */
static int wait_up(int ticks)
{
    int i;
    for (i = 0; i < ticks * 20; i++) {
        i32 h = g->host_open("PING", 4);
        if (h >= 0) { g->host_close(h); return 1; }
        if (h != OS32_ERR_STALE && h != OS32_ERR_AGAIN && h != OS32_ERR_FULL) return 0;
        g->sys_yield();
    }
    return 0;
}

/* 1 本の要求を最後まで通す (status → 本文)。戻り値 0 = 完了。 */
static int svc(const char *req, u32 *status, u32 *len, u8 *body, u32 cap, u32 *got)
{
    i32 h = g->host_open(req, slen(req));
    int i, rc = -1;

    *got = 0;
    if (h < 0) return (int)h;
    for (i = 0; i < 20000; i++) {
        rc = g->host_status(h, status, len);
        if (rc != OS32_ERR_AGAIN) break;
        g->sys_yield();
    }
    if (rc != 0) { g->host_close(h); return rc; }
    for (i = 0; i < 200000; i++) {
        i32 n = g->host_read(h, body + *got, cap - *got);
        if (n == 0) break;
        if (n == OS32_ERR_AGAIN) { g->sys_yield(); continue; }
        if (n < 0) break;
        *got += (u32)n;
        if (*got >= cap) break;
    }
    g->host_close(h);
    return 0;
}
