# GUI プロトコル レイアウト参考 (PROTO_LAYOUT)

> 発行: K1 (2026-09-06) / 親: [TASKS.md](TASKS.md) §4 / 契約: [API_CONTRACTS.md](API_CONTRACTS.md) T2 / U2
> C 側正典: `sdk/include/os32/os32_gui_shared.h` (STATIC_ASSERT で固定)
> Rust 側: `sdk/rust/os32api/src/gui/proto.rs` (C レーンが写す)

数値・配置の更新先は C 共有ヘッダだけであり、Rust の写しは
`tools/check_gui_proto.py` で照合する。本書は配置の読み方を説明する参考資料で、
独立した数値表を持たない。以後の追加は契約 T5 の末尾追記に従う。

## スロットの構造

スロットはヘッダ、要求、応答、イベントリング、引数バッファ、予備の順に並ぶ。
`GuiSlotHeader` の `ring_head` はアプリ (消費者)、`ring_tail` は WM (生産者) が書く。
`GuiEvent` は種別・sub・serial・WindowId の共通ヘッダと payload を持つ。
WindowId は index と generation を含み、Widget の value は packed の非整列フィールド。
各構造体の型・サイズ・オフセットは
[`os32_gui_shared.h:233–287`](../../../sdk/include/os32/os32_gui_shared.h) の構造体と `STATIC_ASSERT` を参照する。

## SHM 予約 (memmap.h)

GUI は SHM 末尾の予約範囲をスロットに分割する。
容量と範囲は [`memmap.h:167・192–194`](../../../include/memmap.h) の
`MEM_SHM_SIZE` / `MEM_SHM_GUI_SIZE` / `MEM_SHM_GUI_OFFSET`、
アプリからの計算は [`os32_gui_shared.h:383–385`](../../../sdk/include/os32/os32_gui_shared.h) の `GUI_SHM_OFFSET` / `GUI_SLOT_SIZE` / `GUI_SLOT_MAX` が正典。
[`kernel/shm.c:119–122`](../../../kernel/shm.c) はこの範囲を `SHM_RESERVED` に固定し、
`shm_alloc` は配らず、`shm_free` / `shm_cleanup_all` は触らない。

## 番号・上限・色

op・イベント種別・上限・Style.flags・システム色は
[`os32_gui_shared.h`](../../../sdk/include/os32/os32_gui_shared.h)、
エラー定義は [`os32_kapi_shared.h`](../../../sdk/include/os32/os32_kapi_shared.h) を参照する。
TRIM の配送・消費・DONE の順序は [API 契約 T5a](API_CONTRACTS.md#trim-contract) に従う。
