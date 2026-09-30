/* ======================================================================== */
/*  gui_busy — syscall 境界ポンプ (K2 / 契約 T6) の完了条件アプリ            */
/*                                                                          */
/*  GUI_OP_INIT でスロットを取り、窓を 1 枚作ってから **30 秒間ひたすら計算   */
/*  する**。その間、1 tick (10ms) ごとに KAPI を 1 回だけ叩く (= 毎秒 100 回)。 */
/*  カーネルはその syscall 境界で WM のポンプを回すので:                     */
/*    - マウスカーソルは追従する (screenshot で確認)                         */
/*    - その間の打鍵とクリックはリングに溜まり、計算が終わってからの          */
/*      OP_POLL でまとめて届く (契約 T3 の保持件数の範囲)                    */
/*    - 溢れた分は OVERFLOW + dropped で通知される                           */
/*    - **メニューは開かない** (契約 T8 の限界。開くのは OP_WAIT 中だけ)      */
/*                                                                          */
/*  `gui_busy --nokapi` は計算ループの中で KAPI を **1 回も呼ばない**版。     */
/*  ポンプが回らないのでカーソルも止まり、CTRL+STOP で kill してシェルに      */
/*  戻るしかない。これが仕様どおりの限界 (契約 T6 / T8 の帰結)。             */
/*                                                                          */
/*  親: docs/archive/gui_v11/TASK_K2_pump_hook.md (完了条件)、API_CONTRACTS T3/T6/T8 */
/* ======================================================================== */

#include "os32api.h"
#include "os32_gui_shared.h"

/* GUI 予約 SHM の位置は共有ヘッダの GUI_SHM_OFFSET / GUI_SLOT_SIZE (正典は
 * memmap.h の MEM_SHM_GUI_BASE / GUI_SLOT_SIZE)。 */

/* 走行時間 (tick, 100Hz) と、KAPI を呼ばない版の計算量。 */
#define RUN_TICKS           3000UL      /* 30 秒 */
#define NOKAPI_ROUNDS       3000UL      /* 校正した 1 tick 分 × 3000 ≒ 30 秒 */
#define FALLBACK_CHUNK      200000UL    /* 校正できないときの 1 tick 相当 */

/* 計算結果の捨て場。volatile にして最適化で消えないようにする。 */
static volatile u32 g_sink = 0;

/* ヘルパは main() の後ろ (SDK 規約)。 */
static int  arg_is_nokapi(int argc, char **argv);
static u32  calibrate_chunk(KernelAPI *api);
static u32  crunch(u32 seed, u32 iters);
static i32  make_window(KernelAPI *api, u32 slot_base);
static void report(KernelAPI *api, u32 slot_base, int polled, u32 kdrop_delta);

void main(int argc, char **argv, KernelAPI *api)
{
    i32 slot;
    i32 win;
    u32 slot_base;
    u32 chunk;
    u32 seed;
    u32 start;
    u32 now;
    u32 calls;
    u32 kdrop0;
    int nokapi;
    int polled;

    nokapi = arg_is_nokapi(argc, argv);

    api->kprintf(0x0E, "gui_busy: %s, %d ticks\n",
                 nokapi ? "no KAPI in loop" : "KAPI 100/s",
                 (int)RUN_TICKS);

    /* --- スロット割当 (契約 T2)。WM 未登録なら CUI なので何もできない --- */
    slot = api->gui_call(GUI_OP_INIT, (u32)GUI_PROTO_VERSION);
    if (slot < 0) {
        api->kprintf(0x4F, "gui_busy: OP_INIT -> %d (WM not running?)\n",
                     (int)slot);
        return;
    }
    slot_base = api->shm_base + (u32)GUI_SHM_OFFSET +
                (u32)slot * (u32)GUI_SLOT_SIZE;

    /* --- 窓を 1 枚。打鍵とクリックの宛先 (フォーカス) を作るため --- */
    win = make_window(api, slot_base);
    if (win < 0) {
        api->kprintf(0x4F, "gui_busy: WIN_CREATE -> %d\n", (int)win);
        return;
    }
    api->kprintf(0x0A, "gui_busy: slot=%d win=%x — now type / click / move\n",
                 (int)slot, (unsigned int)win);

    /* --- 1 tick 分の計算量を測る (ここまでは KAPI を使ってよい) --- */
    chunk = nokapi ? (u32)FALLBACK_CHUNK : calibrate_chunk(api);
    if (chunk == 0) chunk = (u32)FALLBACK_CHUNK;

    seed   = 12345u;
    calls  = 0;
    kdrop0 = api->kbd_dropped_count();

    if (nokapi) {
        /* KAPI を 1 回も呼ばない計算ループ。ポンプは回らない。
         * CTRL+STOP で kill されるのが正しい終わり方 (契約 T6)。 */
        u32 i;
        for (i = 0; i < (u32)NOKAPI_ROUNDS; i++) {
            seed = crunch(seed, chunk);
        }
        g_sink = seed;
        api->kprintf(0x0E, "gui_busy: --nokapi loop finished (not killed)\n");
    } else {
        /* 1 tick 分計算しては KAPI を 1 回叩く = 毎秒約 100 回。
         * この 1 回の syscall のたびにカーネルがポンプを回す。 */
        start = api->get_tick();
        for (;;) {
            seed = crunch(seed, chunk);
            now = api->get_tick();      /* ← 境界ポンプが回る唯一の呼び出し */
            calls++;
            if (now - start >= (u32)RUN_TICKS) break;
        }
        g_sink = seed;
        api->kprintf(0x0E, "gui_busy: %u ticks, %u KAPI calls\n",
                     (unsigned int)(now - start), (unsigned int)calls);
    }

    /* --- 溜まったイベントを一括で受け取る (契約 T3 の OP_POLL) --- */
    polled = (int)api->gui_call(GUI_OP_POLL, 0);
    report(api, slot_base, polled, api->kbd_dropped_count() - kdrop0);

    /* 結果を 5 秒残す。gshell へ戻ると全画面合成でテキスト面が消え、
     * /api/tvram で読めなくなるため (検証用)。 */
    {
        u32 hold = api->get_tick();
        while (api->get_tick() - hold < 500u) api->sys_halt();
    }
}

/* ======================================================================== */
/*  arg_is_nokapi — "--nokapi" が渡されたか                                  */
/* ======================================================================== */
static int arg_is_nokapi(int argc, char **argv)
{
    int i;
    const char *s;

    for (i = 1; i < argc; i++) {
        s = argv[i];
        if (s == 0) continue;
        if (s[0] == '-' && s[1] == '-' && s[2] == 'n' && s[3] == 'o' &&
            s[4] == 'k' && s[5] == 'a' && s[6] == 'p' && s[7] == 'i' &&
            s[8] == '\0') {
            return 1;
        }
    }
    return 0;
}

/* ======================================================================== */
/*  calibrate_chunk — 1 tick (10ms) で回せる crunch の回数を測る             */
/*                                                                          */
/*  回数を倍にしながら 4 tick 以上かかるところまで伸ばし、1 tick 分に割る。  */
/*  これで本走行の「1 chunk = 約 1 tick」= 毎秒約 100 回の KAPI になる。      */
/* ======================================================================== */
static u32 calibrate_chunk(KernelAPI *api)
{
    u32 iters = 4096u;
    u32 seed  = 1u;
    u32 t0, dt;
    int guard;

    for (guard = 0; guard < 24; guard++) {
        t0 = api->get_tick();
        seed = crunch(seed, iters);
        dt = api->get_tick() - t0;
        if (dt >= 4u) {
            g_sink = seed;
            return iters / dt;
        }
        iters <<= 1;
        if (iters == 0) break;      /* 桁溢れの保険 */
    }
    g_sink = seed;
    return (u32)FALLBACK_CHUNK;
}

/* ======================================================================== */
/*  crunch — 中身に意味のない整数計算 (最適化で消えない形)                   */
/* ======================================================================== */
static u32 crunch(u32 seed, u32 iters)
{
    u32 i;
    u32 x = seed;

    for (i = 0; i < iters; i++) {
        x = x * 1103515245u + 12345u;
        x ^= x >> 13;
    }
    return x;
}

/* ======================================================================== */
/*  make_window — 要求ブロックに GuiWinSpec を書いて WIN_CREATE             */
/* ======================================================================== */
static i32 make_window(KernelAPI *api, u32 slot_base)
{
    volatile u8 *req = (volatile u8 *)(slot_base + GUI_SLOT_REQ_OFF);
    GuiWinSpec spec;
    u32 i;
    const char *title = "gui_busy";

    for (i = 0; i < sizeof(spec); i++) {
        ((u8 *)&spec)[i] = 0;
    }
    for (i = 0; i < 39u && title[i] != '\0'; i++) {
        spec.title[i] = (u8)title[i];
    }
    spec.rect.x = 40;
    spec.rect.y = 40;
    spec.rect.w = 240;
    spec.rect.h = 120;
    spec.flags  = GUI_WF_DEFAULT;
    spec.min_w  = 0;
    spec.min_h  = 0;

    for (i = 0; i < sizeof(spec); i++) {
        req[i] = ((const u8 *)&spec)[i];
    }
    return api->gui_call(GUI_OP_WIN_CREATE, 0);
}

/* ======================================================================== */
/*  report — リングに溜まったイベントの内訳と OVERFLOW / dropped を印字      */
/*                                                                          */
/*  読んだ分は head = tail に進める (契約 T3 の消費確認はこれだけ)。         */
/* ======================================================================== */
static void report(KernelAPI *api, u32 slot_base, int polled, u32 kdrop_delta)
{
    volatile GuiSlotHeader *hdr;
    volatile GuiEvent *ring;
    u16 head, tail;
    u16 idx;
    int n_key = 0, n_text = 0, n_btn = 0, n_ptr = 0, n_paint = 0, n_other = 0;
    int i, n;

    hdr  = (volatile GuiSlotHeader *)(slot_base + GUI_SLOT_HDR_OFF);
    ring = (volatile GuiEvent *)(slot_base + GUI_SLOT_RING_OFF);

    head = hdr->ring_head;
    tail = hdr->ring_tail;
    n = (int)(u16)(tail - head);
    if (n > GUI_RING_CAPACITY) n = GUI_RING_CAPACITY;

    for (i = 0; i < n; i++) {
        idx = (u16)((head + (u16)i) % GUI_RING_CAPACITY);
        switch (ring[idx].kind) {
        case GUI_EV_KEY:     n_key++;   break;
        case GUI_EV_TEXT:    n_text++;  break;
        case GUI_EV_BUTTON:  n_btn++;   break;
        case GUI_EV_POINTER: n_ptr++;   break;
        case GUI_EV_PAINT:   n_paint++; break;
        default:             n_other++; break;
        }
    }

    api->kprintf(0x0F, "gui_busy: POLL=%d ring=%d (key=%d text=%d btn=%d "
                       "ptr=%d paint=%d other=%d)\n",
                 polled, n, n_key, n_text, n_btn, n_ptr, n_paint, n_other);
    api->kprintf((hdr->flags & GUI_HDR_FLAG_OVERFLOW) ? 0x0C : 0x0A,
                 "gui_busy: flags=%x dropped=%u kbd_dropped_delta=%u\n",
                 (unsigned int)hdr->flags, (unsigned int)hdr->dropped,
                 (unsigned int)kdrop_delta);

    /* 消費確認: 読んだところまで head を進める (書き手はアプリだけ)。 */
    hdr->ring_head = tail;
}
