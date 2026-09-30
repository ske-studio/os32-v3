# GUI プロトコル レイアウト正典 (PROTO_LAYOUT)

> 発行: K1 (2026-09-06) / 親: [TASKS.md](TASKS.md) §4 / 契約: [API_CONTRACTS.md](API_CONTRACTS.md) T2 / U2
> C 側正典: `sdk/include/os32/os32_gui_shared.h` (STATIC_ASSERT で固定)
> Rust 側: `sdk/rust/os32api/src/gui/proto.rs` (C レーンが写す)

この表は `sdk/include/os32/os32_gui_shared.h` の `STATIC_ASSERT` が固定している
サイズとオフセットの写しである。PM の `tools/check_gui_proto.py` はこの表と
C ヘッダ / Rust `proto.rs` の三者を突き合わせる。**数値を動かすときは 3 か所を同時に。
以後は末尾追記のみ** (契約 T5)。単位はバイト。

## バージョン

| 項目 | 値 |
|---|---|
| `GUI_PROTO_VERSION` | 1 |
| KAPI 版 | 41 |

## GuiSlotHeader — 16B (契約 T2)

| offset | field | type | 書き手 |
|---|---|---|---|
| 0  | `proto_version` | u16 | 初期化 |
| 2  | `flags`         | u16 | WM (bit0 = OVERFLOW) |
| 4  | `seq`           | u32 | WM |
| 8  | `ring_head`     | u16 | アプリ (消費) |
| 10 | `ring_tail`     | u16 | WM (生産) |
| 12 | `dropped`       | u16 | WM |
| 14 | `reserved`      | u16 | — |
| **size** | | | **16** |

## GuiEvent — 16B (契約 U2)

共通ヘッダ 8B + ペイロード union 8B。`window` は完全な WindowId = index:16 | generation:16。

| offset | field | type | 備考 |
|---|---|---|---|
| 0 | `kind`   | u8  | `GUI_EV_*` |
| 1 | `sub`    | u8  | 種別ごとの小さな値 |
| 2 | `serial` | u16 | 入力系のみ。他は 0 |
| 4 | `window` | u32 | index:16 \| generation:16 |
| 8 | `payload`| union 8B | 下表 |
| **size** | | | **16** |

### ペイロード (payload の絶対オフセット = 8 + 相対)

| 種別 | フィールド (絶対 offset : type) |
|---|---|
| `Paint` / `Configure` | `rect` = { x@8 i16, y@10 i16, w@12 i16, h@14 i16 } |
| `Key`     | `scan`@8 u8, `ch`@9 u8, `mods`@10 u8 |
| `Text`    | `utf8`@8 u8[8] |
| `Pointer` | `x`@8 i16, `y`@10 i16 |
| `Button`  | `x`@8 i16, `y`@10 i16, `button`@12 u8 |
| `Widget`  | `widget`@8 u16, `value`@10 i32 (**非整列**, packed) |
| `Modal`   | `dialog`@8 u16, `result`@10 i16 |

- `sizeof(GuiPayload)` = 8、`sizeof(GuiEvtWidget)` = 6 (packed)。

## スロット内レイアウト (1 スロット = GUI_SLOT_SIZE = 16384B、契約 T2)

| 領域 | offset | size |
|---|---|---|
| ヘッダ (GuiSlotHeader) | 0    | 16 |
| 要求 (GuiReq*)         | 16   | 512 |
| 応答 (GuiResp*)        | 528  | 512 |
| イベントリング (128 × 16B) | 1040 | 2048 |
| 引数バッファ           | 3088 | 8192 |
| 予備                   | 11280 | 5104 |
| **合計**               | | **16384** |

- リング容量 `GUI_RING_CAPACITY` = 128 件。

## SHM 予約 (memmap.h)

| 定数 | 値 |
|---|---|
| `MEM_SHM_GUI_BASE` | `MEM_SHM_BASE + 0x30000` (+192KB = ブロック 12) |
| `MEM_SHM_GUI_SIZE` | `0x10000` (64KB = 4 ブロック) |
| `GUI_SLOT_SIZE`    | `0x4000` (16KB) |
| `GUI_SLOT_MAX`     | 4 (スロット 0〜3) |

`kernel/shm.c` がブロック 12〜15 を `SHM_RESERVED` に固定し、`shm_alloc` は配らず、
`shm_free` / `shm_cleanup_all` は触らない。

## op 番号 (gui_call 第1引数)

| 範囲 | op |
|---|---|
| 0 | 予約 (`GUI_OP_NONE`) |
| 1〜7 | `INIT` 1, `POLL` 2, `WAIT` 3, `COMMIT` 4, `INVALIDATE` 5, `STATS` 6, `LEASE_PALETTE` 7 |
| 16〜25 | ウィンドウ: `CREATE` 16, `DESTROY` 17, `MOVE` 18, `RESIZE` 19, `SHOW` 20, `SET_TITLE` 21, `CLIENT_RECT` 22, `RAISE` 23, `SET_FOCUS` 24, `SET_TEXT_CURSOR` 25 |
| 32〜33 | サーフェス: `CREATE` 32, `DESTROY` 33 |
| 48〜49 | タイマ: `SET` 48, `KILL` 49 |
| 64 | モーダル: `OPEN` 64 |
| 80 | `OWNER_EXIT` (カーネル内部。exec_exit → WM。アプリは送らない) |

## イベント種別 (GuiEvent.kind)

`PAINT` 1, `CONFIGURE` 2, `CLOSE` 3, `FOCUS` 4, `KEY` 5, `TEXT` 6, `POINTER` 7,
`BUTTON` 8, `TIMER` 9, `WIDGET` 10, `MODAL` 11, `QUIT` 12, `PALETTE` 13。

## エラー番号 (os32_kapi_shared.h)

| 名前 | 値 |
|---|---|
| `OS32_ERR_INVAL` (= ERR_ARG) | -9 |
| `OS32_ERR_NOSYS` | -10 |
| `OS32_ERR_STALE` | -11 |
| `OS32_ERR_VERSION` | -12 |
| `OS32_ERR_FULL` | -13 |

## 上限 (P 性能規約 / U6)

`GUI_MAX_WINDOWS` 16, `GUI_MAX_SURFACES` 16, `GUI_MAX_WIDGETS` 64,
`GUI_MAX_LIST_ITEMS` 128, `GUI_MAX_TIMERS` 8, `GUI_MAX_CLIP_DEPTH` 8,
`GUI_MAX_DAMAGE` 8, `GUI_MAX_STRING` 256。

## Style.flags (u8)

`TRANSPARENT_BG` 0x01, `XOR` 0x02, `DOTTED` 0x04, `DITHER50` 0x08。

## システム色 (GUI_SYSTEM_PALETTE[16]、RGB 各 0〜15、G6)

| idx | 役割 | R | G | B |
|---|---|---|---|---|
| 0 | TEXT | 0 | 0 | 0 |
| 1 | TITLE_ACTIVE / SEL_BG | 0 | 0 | 8 |
| 2 | SHADOW | 6 | 6 | 6 |
| 3 | DISABLED | 9 | 9 | 9 |
| 4 | OK | 0 | 10 | 0 |
| 5 | WARN | 14 | 12 | 0 |
| 6 | FACE / TITLE_INACTIVE | 12 | 12 | 12 |
| 7 | WINDOW / TITLE_TEXT | 15 | 15 | 15 |
| 8 | CLOSE / ALERT | 12 | 0 | 0 |
| 9 | LINK | 0 | 10 | 14 |
| 10 | ACCENT | 12 | 0 | 12 |
| 11 | LIGHT | 14 | 14 | 14 |
| 12 | DESKTOP | 0 | 8 | 10 |
| 13 | HIGHLIGHT | 0 | 0 | 15 |
| 14 | SEL_TEXT | 15 | 15 | 15 |
| 15 | EDIT_BG | 15 | 15 | 14 |
