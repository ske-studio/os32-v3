/* ======================================================================== */
/*  OS32_GUI_SHARED.H — GUI シェル v1.1 プロトコル共有定義 (SSoT)            */
/*                                                                          */
/*  カーネル (gui_call ディスパッチ) / WM (gshell, Rust) / クライアント      */
/*  (libos32gui, Rust) の三者が同じ数値を使うための唯一の情報源。            */
/*                                                                          */
/*  ■ 規則 (契約 T5 / TASKS §4):                                            */
/*    - op 番号・イベント種別・構造体は **末尾追記のみ**。既存の値を          */
/*      動かすと W / C レーンが写した Rust 側と黙って食い違う。              */
/*    - SHM とイベントに **ポインタを載せない** (契約 T1)。文字列は長さ       */
/*      前置で値渡し。                                                       */
/*    - 大きさ (16 / 512) と各フィールドのオフセットは STATIC_ASSERT で       */
/*      固定する。同じ数表を docs/tasks/gui/PROTO_LAYOUT.md に書き出し、      */
/*      PM の tools/check_gui_proto.py が C 側の正典として読む。             */
/*                                                                          */
/*  親: docs/tasks/gui/API_CONTRACTS.md (G6 / T2 / T4 / U2)、TASKS.md §4     */
/* ======================================================================== */

#ifndef OS32_GUI_SHARED_H
#define OS32_GUI_SHARED_H

/* 基本型 (u8/u16/u32/i16/i32) と OS32_ERR_* / GFX_Stats の SSoT。 */
#include "os32_kapi_shared.h"

/* types.h の STATIC_ASSERT と同一。カーネル/ユーザランドどちらの経路でも
 * 自己完結させるためのフォールバック定義。 */
#ifndef STATIC_ASSERT
#define STATIC_ASSERT(cond, name) \
    typedef char static_assert_##name[(cond) ? 1 : -1]
#endif

#ifndef GUI_OFFSETOF
#define GUI_OFFSETOF(t, m) __builtin_offsetof(t, m)
#endif

/* ======================================================================== */
/*  プロトコルバージョン                                                     */
/* ======================================================================== */
#define GUI_PROTO_VERSION   1

/* ======================================================================== */
/*  op 番号 (gui_call の第1引数) — TASKS §4                                  */
/*                                                                          */
/*  カーネルは op の意味を解釈しない (WM の仕事)。0 は予約。                  */
/*  1〜7 = コア、16〜 = ウィンドウ、32〜 = サーフェス、48〜 = タイマ、        */
/*  64〜 = モーダル、80〜 = 予備 (カーネル内部の owner 回収に 80 を使う)。    */
/* ======================================================================== */
#define GUI_OP_NONE            0
#define GUI_OP_INIT            1   /* スロット割当 (戻り値=スロット番号 0〜3) */
#define GUI_OP_POLL            2   /* イベント一括取得 (戻り値=件数) */
#define GUI_OP_WAIT            3   /* arg=timeout_ticks。sys_halt で待つ */
#define GUI_OP_COMMIT          4   /* issued 矩形を present */
#define GUI_OP_INVALIDATE      5   /* 損傷申告 */
#define GUI_OP_STATS           6   /* カウンタ取得 */
#define GUI_OP_LEASE_PALETTE   7   /* パレットリース (G8) */

/* ウィンドウ (16〜) — U1 の順: CREATE DESTROY MOVE RESIZE SHOW SET_TITLE
 * CLIENT_RECT RAISE SET_FOCUS SET_TEXT_CURSOR */
#define GUI_OP_WIN_CREATE          16
#define GUI_OP_WIN_DESTROY         17
#define GUI_OP_WIN_MOVE            18
#define GUI_OP_WIN_RESIZE          19
#define GUI_OP_WIN_SHOW            20
#define GUI_OP_WIN_SET_TITLE       21
#define GUI_OP_WIN_CLIENT_RECT     22
#define GUI_OP_WIN_RAISE           23
#define GUI_OP_WIN_SET_FOCUS       24
#define GUI_OP_WIN_SET_TEXT_CURSOR 25

/* サーフェス (32〜) — G3 */
#define GUI_OP_SURF_CREATE     32
#define GUI_OP_SURF_DESTROY    33

/* タイマ (48〜) — U5 */
#define GUI_OP_TIMER_SET       48
#define GUI_OP_TIMER_KILL      49

/* モーダル (64〜) — U4。65 / 66 は v1.2 の追記 (v12/CONTRACTS.md §1 S4 / §3 M1)。
 * 67〜79 は予約。未実装 op を受けた WM は OS32_ERR_NOSYS を返すので、
 * 「新 shlib + 古い gshell」は GUI_PROTO_VERSION=1 のままでも安全に失敗する。 */
#define GUI_OP_MODAL_OPEN      64
#define GUI_OP_MODAL_RESULT    65  /* 完了したモーダルの結果取得 (v1.2 M1) */
#define GUI_OP_SESSION_REQUEST 66  /* gshell へのセッション要求 (v1.2 S4) */

/* 予備 (80〜)。GUI_OP_OWNER_EXIT はカーネルが exec_exit から WM ハンドラへ
 * 渡す内部 op (契約 T4 / U8)。アプリは送らない。 */
#define GUI_OP_OWNER_EXIT      80

/* ======================================================================== */
/*  イベント種別 (GuiEvent.kind) — U2 の並び順どおり 1 から                  */
/* ======================================================================== */
#define GUI_EV_NONE       0
#define GUI_EV_PAINT      1   /* 導出型 */
#define GUI_EV_CONFIGURE  2   /* 導出型 */
#define GUI_EV_CLOSE      3
#define GUI_EV_FOCUS      4   /* sub: in 0/1 */
#define GUI_EV_KEY        5   /* sub: down 0/1 */
#define GUI_EV_TEXT       6   /* sub: len 1〜8 | more<<7 */
#define GUI_EV_POINTER    7   /* sub: buttons。合成される */
#define GUI_EV_BUTTON     8   /* sub: down 0/1 */
#define GUI_EV_TIMER      9   /* sub: id。導出型 */
#define GUI_EV_WIDGET     10  /* sub: kind。クライアント側で合成 */
#define GUI_EV_MODAL      11
#define GUI_EV_QUIT       12  /* sub: 理由 */
#define GUI_EV_PALETTE    13  /* sub: active 0/1。G8 */

/* ======================================================================== */
/*  Style.flags (u8) — G6                                                    */
/* ======================================================================== */
#define GUI_STYLE_TRANSPARENT_BG  0x01  /* 背景を塗らず文字だけ */
#define GUI_STYLE_XOR             0x02  /* 枠ドラッグ */
#define GUI_STYLE_DOTTED          0x04  /* フォーカス矩形の点線 */
#define GUI_STYLE_DITHER50        0x08  /* 50% 市松塗り */

/* ======================================================================== */
/*  ウィンドウフラグ GUI_WF_* (u16) — 契約 U1。GuiWinSpec.flags で使う。      */
/*  既存 libos32gui (types.rs) と同値。ABI 確定 (レビュー ③)。末尾追記のみ。 */
/* ======================================================================== */
#define GUI_WF_VISIBLE    0x0001  /* 生成時から可視 */
#define GUI_WF_HAS_CLOSE  0x0002  /* 閉じるボタンを WM が描く */
#define GUI_WF_MOVABLE    0x0004  /* タイトルバーでドラッグ移動可 */
#define GUI_WF_BORDER     0x0008  /* 立体枠を WM が描く */
#define GUI_WF_DEFAULT    (GUI_WF_VISIBLE | GUI_WF_HAS_CLOSE | GUI_WF_MOVABLE | GUI_WF_BORDER)

/* ======================================================================== */
/*  システム色 (16 色固定、役割名で参照) — G6                                */
/*                                                                          */
/*  アプリは index でなく役割名で参照する。gshell が起動時に                 */
/*  gfx_set_palette で GUI_SYSTEM_PALETTE を入れる。                         */
/* ======================================================================== */
#define GUI_COLOR_TEXT            0   /* 文字、枠 */
#define GUI_COLOR_TITLE_ACTIVE    1   /* アクティブタイトル */
#define GUI_COLOR_SEL_BG          1   /* 選択背景 (別名) */
#define GUI_COLOR_SHADOW          2   /* 立体枠の影 */
#define GUI_COLOR_DISABLED        3   /* 無効文字 */
#define GUI_COLOR_OK              4   /* 成功表示 */
#define GUI_COLOR_WARN            5   /* 警告表示 */
#define GUI_COLOR_FACE            6   /* ボタン面 */
#define GUI_COLOR_TITLE_INACTIVE  6   /* 非アクティブタイトル (別名) */
#define GUI_COLOR_WINDOW          7   /* クライアント面 */
#define GUI_COLOR_TITLE_TEXT      7   /* タイトル文字 (別名) */
#define GUI_COLOR_CLOSE           8   /* 閉じるボタン */
#define GUI_COLOR_ALERT           8   /* エラー (別名) */
#define GUI_COLOR_LINK            9   /* リンク、情報 */
#define GUI_COLOR_ACCENT          10  /* 強調 */
#define GUI_COLOR_LIGHT           11  /* 立体枠のハイライト */
#define GUI_COLOR_DESKTOP         12  /* デスクトップ */
#define GUI_COLOR_HIGHLIGHT       13  /* 押下、フォーカスリング */
#define GUI_COLOR_SEL_TEXT        14  /* 選択項目の文字 */
#define GUI_COLOR_EDIT_BG         15  /* テキストボックス背景 */

/* RGB 各成分 0〜15 (PC-98 16 色)。GUI_COLOR_* の並びと一致。 */
typedef struct { u8 r, g, b; } GuiRgb;

static const GuiRgb GUI_SYSTEM_PALETTE[16] __attribute__((unused)) = {
    {  0,  0,  0 },   /*  0 TEXT           */
    {  0,  0,  8 },   /*  1 TITLE_ACTIVE   */
    {  6,  6,  6 },   /*  2 SHADOW         */
    {  9,  9,  9 },   /*  3 DISABLED       */
    {  0, 10,  0 },   /*  4 OK             */
    { 14, 12,  0 },   /*  5 WARN           */
    { 12, 12, 12 },   /*  6 FACE           */
    { 15, 15, 15 },   /*  7 WINDOW         */
    { 12,  0,  0 },   /*  8 CLOSE          */
    {  0, 10, 14 },   /*  9 LINK           */
    { 12,  0, 12 },   /* 10 ACCENT         */
    { 14, 14, 14 },   /* 11 LIGHT          */
    {  0,  8, 10 },   /* 12 DESKTOP        */
    {  0,  0, 15 },   /* 13 HIGHLIGHT      */
    { 15, 15, 15 },   /* 14 SEL_TEXT       */
    { 15, 15, 14 }    /* 15 EDIT_BG        */
};

/* ======================================================================== */
/*  上限 (P 性能規約 / U6) — 決め打ち禁止のための共有定数                     */
/* ======================================================================== */
#define GUI_MAX_WINDOWS      16
#define GUI_MAX_SURFACES     16
#define GUI_MAX_WIDGETS      64
#define GUI_MAX_LIST_ITEMS   128
#define GUI_MAX_TIMERS       8    /* / アプリ */
#define GUI_MAX_CLIP_DEPTH   8
#define GUI_MAX_DAMAGE       8    /* / ウィンドウ */
#define GUI_MAX_STRING       256  /* バイト (長さ前置 u8 + 255) */

/* 複数行編集ウィジェット WK_TEXTAREA (票 TASK_EDIT_GUI §2、決裁 A1)。
 * **部品は本文を持たない**。アプリ (エディタ) が本文を持ち、いま見えている
 * 行だけをここへ写す。だから上限は「1 画面に入る行数」と「1 行の見える幅」で
 * 足り、ファイルの大きさとは無関係。 */
#define GUI_MAX_TEXTAREA_ROWS  32   /* 見える行のプール (全 textarea で共有) */
#define GUI_TEXTAREA_ROW_CAP   192  /* 見える 1 行のバイト数 (80 桁 = 全角 40 字 * 3B + 余白) */
#define GUI_TEXTAREA_INPUT_CAP 8    /* 溜めておける確定文字列 (GuiEvtText.utf8 と同じ) */

/* ======================================================================== */
/*  ヘッダフラグ (GuiSlotHeader.flags)                                        */
/* ======================================================================== */
#define GUI_HDR_FLAG_OVERFLOW  0x0001  /* bit0: 打鍵の取りこぼしあり (T3) */

/* ======================================================================== */
/*  基本図形 — 座標は描画先サーフェスのローカル (G1)                          */
/* ======================================================================== */
typedef struct {
    i16 x, y, w, h;   /* 空は w<=0 || h<=0 */
} GuiRect16;          /* 8B */

/* ======================================================================== */
/*  スロットヘッダ (16B) — 契約 T2                                            */
/*                                                                          */
/*  ring_head はアプリ (消費者) だけが書き、ring_tail は WM (生産者) だけが   */
/*  書く (T3)。書き手が 1 つずつなのでロック不要。                            */
/* ======================================================================== */
typedef struct {
    u16 proto_version;   /* @0  GUI_PROTO_VERSION */
    u16 flags;           /* @2  GUI_HDR_FLAG_* */
    u32 seq;             /* @4 */
    u16 ring_head;       /* @8  アプリが書く (消費) */
    u16 ring_tail;       /* @10 WM が書く (生産) */
    u16 dropped;         /* @12 取りこぼし累計差分 (受領で WM が消す) */
    u16 reserved;        /* @14 */
} GuiSlotHeader;         /* 16B */

STATIC_ASSERT(sizeof(GuiSlotHeader) == 16, gui_slot_header_16);
STATIC_ASSERT(GUI_OFFSETOF(GuiSlotHeader, proto_version) == 0,  gui_hdr_proto_0);
STATIC_ASSERT(GUI_OFFSETOF(GuiSlotHeader, flags)         == 2,  gui_hdr_flags_2);
STATIC_ASSERT(GUI_OFFSETOF(GuiSlotHeader, seq)           == 4,  gui_hdr_seq_4);
STATIC_ASSERT(GUI_OFFSETOF(GuiSlotHeader, ring_head)     == 8,  gui_hdr_head_8);
STATIC_ASSERT(GUI_OFFSETOF(GuiSlotHeader, ring_tail)     == 10, gui_hdr_tail_10);
STATIC_ASSERT(GUI_OFFSETOF(GuiSlotHeader, dropped)       == 12, gui_hdr_dropped_12);

/* ======================================================================== */
/*  イベント (16B 固定) — 契約 U2                                             */
/*                                                                          */
/*  共通ヘッダ 8B (kind@0, sub@1, serial@2, window@4) + ペイロード union 8B。 */
/*  window は完全な WindowId = index:16 | generation:16。古いイベントは       */
/*  generation が合わないのでクライアントが捨てる。                          */
/*                                                                          */
/*  C89 なので union + 共通ヘッダで書く。value は @10 で非整列 (U2) のため     */
/*  Widget ペイロードだけ packed。                                           */
/* ======================================================================== */
typedef struct { u8 scan; u8 ch; u8 mods; u8 _pad[5]; } GuiEvtKey;      /* U2a */
typedef struct { u8 utf8[8]; }                         GuiEvtText;      /* 8B */
typedef struct { i16 x; i16 y; u8 _pad[4]; }           GuiEvtPointer;
typedef struct { i16 x; i16 y; u8 button; u8 _pad[3]; } GuiEvtButton;
typedef struct { u16 widget; i32 value; } __attribute__((packed)) GuiEvtWidget; /* 6B, value@2 */
typedef struct { u16 dialog; i16 result; u8 _pad[4]; } GuiEvtModal;

typedef union {
    GuiRect16     rect;      /* Paint / Configure */
    GuiEvtKey     key;
    GuiEvtText    text;
    GuiEvtPointer pointer;
    GuiEvtButton  button;
    GuiEvtWidget  widget;
    GuiEvtModal   modal;
    u8            raw[8];
} GuiPayload;                /* 8B */

typedef struct {
    u8  kind;                /* @0  GUI_EV_* */
    u8  sub;                 /* @1  種別ごとの小さな値 */
    u16 serial;              /* @2  入力系のみ。他は 0 */
    u32 window;              /* @4  index:16 | generation:16 */
    GuiPayload payload;      /* @8  8B */
} GuiEvent;                  /* 16B */

STATIC_ASSERT(sizeof(GuiEvtWidget) == 6, gui_evt_widget_6);
STATIC_ASSERT(sizeof(GuiPayload)   == 8, gui_payload_8);
STATIC_ASSERT(sizeof(GuiEvent)     == 16, gui_event_16);
STATIC_ASSERT(GUI_OFFSETOF(GuiEvent, kind)    == 0, gui_ev_kind_0);
STATIC_ASSERT(GUI_OFFSETOF(GuiEvent, sub)     == 1, gui_ev_sub_1);
STATIC_ASSERT(GUI_OFFSETOF(GuiEvent, serial)  == 2, gui_ev_serial_2);
STATIC_ASSERT(GUI_OFFSETOF(GuiEvent, window)  == 4, gui_ev_window_4);
STATIC_ASSERT(GUI_OFFSETOF(GuiEvent, payload) == 8, gui_ev_payload_8);
/* ペイロードの各フィールドの絶対オフセット (U2 の表) */
STATIC_ASSERT(GUI_OFFSETOF(GuiEvent, payload) +
              GUI_OFFSETOF(GuiEvtWidget, value) == 10, gui_ev_widget_value_10);
STATIC_ASSERT(GUI_OFFSETOF(GuiEvent, payload) +
              GUI_OFFSETOF(GuiEvtButton, button) == 12, gui_ev_button_12);

/* ======================================================================== */
/*  スロット内レイアウト (16KB = 1 スロット = アプリ 1 本) — 契約 T2          */
/*                                                                          */
/*  オフセット: ヘッダ 0 / 要求 16 / 応答 528 / リング 1040 (128×16B) /       */
/*  引数 3088 (8KB) / 予備 〜16384。                                          */
/* ======================================================================== */
#define GUI_SLOT_HDR_OFF    0
#define GUI_SLOT_REQ_OFF    16
#define GUI_SLOT_REQ_SIZE   512
#define GUI_SLOT_RESP_OFF   528
#define GUI_SLOT_RESP_SIZE  512
#define GUI_SLOT_RING_OFF   1040
#define GUI_RING_CAPACITY   128
#define GUI_SLOT_RING_SIZE  (GUI_RING_CAPACITY * 16)   /* 2048 */
#define GUI_SLOT_ARGS_OFF   3088
#define GUI_SLOT_ARGS_SIZE  8192

/* スロット全体は GUI_SLOT_SIZE (memmap.h, 16KB)。ここでは相対オフセットの
 * 連鎖と収まりを固定する (実サイズは memmap.h の STATIC_ASSERT で担保)。 */
STATIC_ASSERT(GUI_SLOT_REQ_OFF  == GUI_SLOT_HDR_OFF + 16,                 gui_slot_req_after_hdr);
STATIC_ASSERT(GUI_SLOT_RESP_OFF == GUI_SLOT_REQ_OFF + GUI_SLOT_REQ_SIZE,  gui_slot_resp_after_req);
STATIC_ASSERT(GUI_SLOT_RING_OFF == GUI_SLOT_RESP_OFF + GUI_SLOT_RESP_SIZE, gui_slot_ring_after_resp);
STATIC_ASSERT(GUI_SLOT_ARGS_OFF == GUI_SLOT_RING_OFF + GUI_SLOT_RING_SIZE, gui_slot_args_after_ring);
STATIC_ASSERT(GUI_SLOT_ARGS_OFF + GUI_SLOT_ARGS_SIZE <= 0x4000,           gui_slot_fits_16k);

/* ======================================================================== */
/*  要求 / 応答の共有構造体 (GuiReq* / GuiResp*)                              */
/*                                                                          */
/*  すべて要求ブロック (512B) / 応答ブロック (512B) 以内。文字列は長さ前置    */
/*  (GuiString) で値渡し。**末尾追記のみ** — op を足すたびにここへ req/resp   */
/*  構造体を追記する (W / C は Rust 側へ写す)。                               */
/* ======================================================================== */

/* 長さ前置文字列 (最大 255 バイト + NUL 相当の長さ)。UTF-8 境界で切り詰め。 */
typedef struct {
    u8 len;        /* 有効バイト数 (0〜255) */
    u8 s[255];
} GuiString;       /* 256B */
STATIC_ASSERT(sizeof(GuiString) == 256, gui_string_256);
STATIC_ASSERT(sizeof(GuiString) <= GUI_SLOT_REQ_SIZE, gui_string_fits_req);

/* create_window の spec (U1)。title は 40B 固定 (U1 の [u8;40])。 */
typedef struct {
    u8        title[40];   /* @0  UTF-8, 短ければ 0 埋め */
    GuiRect16 rect;        /* @40 初期位置・大きさ */
    u16       flags;       /* @48 既存 GUI_WF_* を継承 */
    i16       min_w;       /* @50 */
    i16       min_h;       /* @52 */
} GuiWinSpec;              /* 54B */
STATIC_ASSERT(GUI_OFFSETOF(GuiWinSpec, rect)  == 40, gui_winspec_rect_40);
STATIC_ASSERT(GUI_OFFSETOF(GuiWinSpec, flags) == 48, gui_winspec_flags_48);
STATIC_ASSERT(sizeof(GuiWinSpec) <= GUI_SLOT_REQ_SIZE, gui_winspec_fits_req);

/* ======================================================================== */
/*  op ごとの要求 / 応答 (契約 U1 / U4 / U5 / G3)。ABI 確定 (レビュー ③)。    */
/*                                                                          */
/*  gui_call(op, arg) で arg が小さい値 (WindowId 等) だけの op は arg に直接  */
/*  載せる。複数フィールドが要る op は下の GuiReq* を要求ブロック (512B) に    */
/*  書いてから呼び、結果は GuiResp* を応答ブロック (512B) から読む。           */
/*  WindowId / SurfaceId は u32 (index:16 | generation:16)。result<0 は        */
/*  OS32_ERR_*。W (gshell) / C (libos32gui) はこの並びを Rust へ写す。         */
/*  **末尾追記のみ** (契約 T5)。                                              */
/* ======================================================================== */

/* 単一ハンドルを対象にする op (DESTROY / RAISE / SET_FOCUS / CLIENT_RECT) */
typedef struct { u32 window; } GuiReqWindow;                        /* 4B */

typedef struct { u32 window; i16 x; i16 y; } GuiReqWinMove;         /* MOVE   8B */
typedef struct { u32 window; i16 w; i16 h; } GuiReqWinResize;       /* RESIZE 8B */
typedef struct { u32 window; u8 show; u8 _pad[3]; } GuiReqWinShow;  /* SHOW   8B */
typedef struct { u32 window; i16 x; i16 y; u8 visible; u8 _pad; } GuiReqTextCursor; /* SET_TEXT_CURSOR */
typedef struct { u32 window; GuiString title; } GuiReqWinTitle;     /* SET_TITLE 260B */

typedef struct { i16 w; i16 h; } GuiReqSurfCreate;                  /* SURF_CREATE */
/* タイマ (契約 U5: set_timer(id: u8, interval_ticks: u16, repeat: bool))。
 * 2026-09-06 レビュー #3 ⑤で契約に合わせた: id は u8 (Timer イベントの sub と同幅)、
 * 間隔は tick (10ms)、repeat=0 は単発 (1 回発火して WM が消す)。 */
typedef struct { u32 window; u8 timer_id; u8 repeat; u16 interval_ticks; } GuiReqTimerSet; /* TIMER_SET 8B */
typedef struct { u32 window; u8 timer_id; u8 _pad[3]; } GuiReqTimerKill;                  /* TIMER_KILL 8B */
typedef struct { u16 buttons; u16 _pad; GuiString message; } GuiReqModal;     /* MODAL_OPEN 260B */

typedef struct { i32 result; u32 window;   } GuiRespWindow;         /* CREATE -> window */
typedef struct { i32 result; u32 surface;  } GuiRespSurface;       /* SURF_CREATE -> surface */
typedef struct { i32 result; GuiRect16 rect; } GuiRespRect;         /* CLIENT_RECT -> rect */
typedef struct { i32 result; i16 button; i16 _pad; } GuiRespModal;  /* MODAL_OPEN -> button */

/* INVALIDATE (op 5): 契約 G4 の invalidate(window, rect)。W1 の申し送りで追記
 * (2026-09-06)。rect は窓のクライアント座標。 */
typedef struct { u32 window; GuiRect16 rect; } GuiReqInvalidate;    /* INVALIDATE 12B */

/* SHM 内の GUI 領域: スロットの番地 = kapi->shm_base + GUI_SHM_OFFSET + slot × GUI_SLOT_SIZE。
 * 正典は include/memmap.h (MEM_SHM_GUI_BASE / GUI_SLOT_SIZE)。C のアプリが自前定義しなくて
 * 済むよう写しを置く (K2 の申し送り、2026-09-06)。カーネル側は memmap.h が先に定義する。 */
#define GUI_SHM_OFFSET  0x28000UL   /* MEM_SHM_BASE からのオフセット (末尾 4 ブロック = +160KB) */
#define GUI_SLOT_MAX     4        /* スロット 0〜3 (契約 T2: 4 本)。Rust 側 proto.rs と同値 */
#ifndef GUI_SLOT_SIZE
#define GUI_SLOT_SIZE   0x4000UL    /* 16KB = 1 スロット */
#endif
STATIC_ASSERT(GUI_SLOT_ARGS_OFF + GUI_SLOT_ARGS_SIZE <= GUI_SLOT_SIZE, gui_slot_fits_slot_size);

/* LEASE_PALETTE (op 7、契約 G8): first/count は要求ブロック、色は要求ブロック内に
 * 直接 (16 色 × 3B = 48B、引数バッファは使わない)。count=0 で返却。W2 が実装。
 * 追記 2026-09-06 (W1 の申し送り)。 */
typedef struct { u16 first; u16 count; GuiRgb rgb[16]; } GuiReqLease;    /* 52B */

STATIC_ASSERT(sizeof(GuiReqWinMove)   == 8,  gui_req_winmove_8);
STATIC_ASSERT(sizeof(GuiReqInvalidate) == 12, gui_req_invalidate_12);
STATIC_ASSERT(sizeof(GuiReqTimerSet)  == 8,  gui_req_timerset_8);
STATIC_ASSERT(sizeof(GuiReqLease)     == 52, gui_req_lease_52);

/* MODAL_OPEN (op 64、契約 U4) の GuiReqModal.buttons と GuiEvtModal.result の値
 * (W2 の申し送り、2026-09-06 追記)。result は 1 = OK / Yes / Open、0 = Cancel / No / ESC。
 * 選択したパスや入力文字列をアプリへ返す経路は GUI_OP_MODAL_RESULT (op 65、v1.2 M1)。 */
#define GUI_MODAL_OK          0   /* OK */
#define GUI_MODAL_OK_CANCEL   1   /* OK / Cancel */
#define GUI_MODAL_YES_NO      2   /* Yes / No */
#define GUI_MODAL_FILE_OPEN   3   /* ファイル選択 (Open / Cancel) */
#define GUI_MODAL_INPUT       4   /* 1 行入力 (OK / Cancel) — v1.2 M4。prompt は
                                   * 既存 GuiReqModal.message。0〜3 は不変。 */
#define GUI_MODAL_RESULT_CANCEL 0
#define GUI_MODAL_RESULT_OK     1
STATIC_ASSERT(sizeof(GuiReqWinTitle)  <= GUI_SLOT_REQ_SIZE,  gui_req_wintitle_fits);
STATIC_ASSERT(sizeof(GuiReqModal)     <= GUI_SLOT_REQ_SIZE,  gui_req_modal_fits);
STATIC_ASSERT(sizeof(GuiRespRect)     <= GUI_SLOT_RESP_SIZE, gui_resp_rect_fits);
STATIC_ASSERT(sizeof(GuiRespWindow)   <= GUI_SLOT_RESP_SIZE, gui_resp_window_fits);

/* ======================================================================== */
/*  v1.2 追記 (K5) — MODAL_RESULT / SESSION_REQUEST                          */
/*                                                                          */
/*  正典: docs/tasks/gui/v12/CONTRACTS.md §1 (S4 / S5) と §3 (M1 / M4)。      */
/*  KAPI は v42 のまま、GUI_PROTO_VERSION も 1 のまま。既存の op / イベント / */
/*  構造体は動かさず末尾へ足すだけ (契約 T5)。SHM にポインタは載せない (T1) —  */
/*  パスも入力文字列も長さ前置の GuiString で値渡しする。                     */
/*                                                                          */
/*  ■ GUI_OP_MODAL_RESULT (op 65) の consume 規則 (M2):                      */
/*    - WM は GUI スロットごとに完了済みの結果を 1 件だけ保持する。            */
/*    - 未 consume の結果がある間、そのスロットの MODAL_OPEN は                */
/*      OS32_ERR_FULL。                                                      */
/*    - 要求の dialog が保持中の ID と違えば OS32_ERR_STALE。                  */
/*    - 成功時は応答ブロックへ result / value を全部書いてから consume する。   */
/*    - consume 済みの ID をもう一度取りに行くと OS32_ERR_STALE。              */
/*    - owner 回収 (OWNER_EXIT) で未 consume の結果も破棄する。                */
/*    value は MessageBox なら空、File Open なら選択した絶対パス、Input なら    */
/*    入力した UTF-8 (いずれも最大 255B、切り詰めて別の値として返さない)。      */
/*                                                                          */
/*  ■ GUI_OP_SESSION_REQUEST (op 66) の意味 (S2 / S3):                       */
/*    外部アプリが自分で exec_run() せず、gshell のトップレベルへ起動 / CUI    */
/*    復帰 / 停止を依頼する経路。戻り値 0 は**受理**であって完了ではない —      */
/*    WM は値を私有メモリへ写して pending を立てるだけで、VFS / exec_run /     */
/*    system.cfg 更新は行わない (X4 の bounded-work 規約 S8)。                */
/*    同時に保持できる SessionAction は 1 件で、新しい要求で上書きしない        */
/*    (pending 中は OS32_ERR_FULL)。action / flags / パスが不正なら             */
/*    OS32_ERR_INVAL、op を知らない古い WM は OS32_ERR_NOSYS。                 */
/* ======================================================================== */

/* SessionAction の種別 (GuiReqSession.action)。0 は「要求なし」で予約。 */
#define GUI_SESSION_LAUNCH      1   /* value = 1〜255B の絶対パス */
#define GUI_SESSION_SWITCH_CUI  2   /* value は空でなければならない */
#define GUI_SESSION_SHUTDOWN    3   /* value は空でなければならない */

/* GUI_EV_QUIT (kind 12) の GuiEvent.sub に入る終了理由。0 は予約 (S5)。
 * Quit は制御イベントなのでリング満杯でも捨てず、WM が sticky に再配送する。 */
#define GUI_QUIT_REASON_REPLACE_APP  1   /* 別アプリ起動のため退去 */
#define GUI_QUIT_REASON_SWITCH_CUI   2   /* CUI モードへ切替 */
#define GUI_QUIT_REASON_SHUTDOWN     3   /* system halt */

typedef struct {
    u16 dialog;    /* @0 結果を取りに行くダイアログ ID */
    u16 _pad;      /* @2 */
} GuiReqModalResult;                     /* MODAL_RESULT 要求 4B */

typedef struct {
    i16       result;   /* @0 GUI_MODAL_RESULT_* / <0 は OS32_ERR_* */
    u16       dialog;   /* @2 応答した ID (要求と一致) */
    GuiString value;    /* @4 パス / 入力文字列。MessageBox は len=0 */
} GuiRespModalResult;                    /* MODAL_RESULT 応答 260B */

typedef struct {
    u8        action;   /* @0 GUI_SESSION_* */
    u8        flags;    /* @1 v1.2 は 0 のみ */
    u16       _pad;     /* @2 */
    GuiString value;    /* @4 LAUNCH は絶対パス、他は len=0 */
} GuiReqSession;                         /* SESSION_REQUEST 要求 260B */

STATIC_ASSERT(sizeof(GuiReqModalResult)  == 4,   gui_req_modalresult_4);
STATIC_ASSERT(sizeof(GuiRespModalResult) == 260, gui_resp_modalresult_260);
STATIC_ASSERT(sizeof(GuiReqSession)      == 260, gui_req_session_260);
STATIC_ASSERT(GUI_OFFSETOF(GuiRespModalResult, dialog) == 2, gui_resp_modalresult_dialog_2);
STATIC_ASSERT(GUI_OFFSETOF(GuiRespModalResult, value)  == 4, gui_resp_modalresult_value_4);
STATIC_ASSERT(GUI_OFFSETOF(GuiReqSession, flags) == 1, gui_req_session_flags_1);
STATIC_ASSERT(GUI_OFFSETOF(GuiReqSession, value) == 4, gui_req_session_value_4);
STATIC_ASSERT(sizeof(GuiReqModalResult)  <= GUI_SLOT_REQ_SIZE,  gui_req_modalresult_fits);
STATIC_ASSERT(sizeof(GuiReqSession)      <= GUI_SLOT_REQ_SIZE,  gui_req_session_fits);
STATIC_ASSERT(sizeof(GuiRespModalResult) <= GUI_SLOT_RESP_SIZE, gui_resp_modalresult_fits);

#endif /* OS32_GUI_SHARED_H */
