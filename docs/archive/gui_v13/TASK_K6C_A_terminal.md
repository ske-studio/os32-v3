# K6C-A — 端末アプリ: con_sink を吸って Paint する (外部アプリ)

> 発行: PM (2026-09-12) / 状態: **受入完了 (2026-09-12)**

受入済み (2026-09-12、`abef34f`)。前提: [K6C (K 側)](TASK_K6C_console.md) `d381000` 配備済み (KAPI v46、C1/C2 合格)。
親: [PLAN.md](../../tasks/gui/v13/PLAN.md) §1 (決裁 B: K5 → K6 console → **端末アプリ** → K7 入力統合 …)。
ユーザー決裁: **端末は外部アプリ**、gshell は端末を持たない。

## 0. 目的

GUI モード中にカーネル / CUI コマンドが出す出力は、K6C でリングに溜まる (`con_sink_read` / `con_sink_stat`、
`os32_kapi_shared.h` の `CON_SINK_REC_PRINT / CLEAR / CURSOR`)。それを **外部アプリ `t5a_display`
(`userland/rust/t5a_display`、T4 セルモデル + `libos32term_render`) が吸って端末窓に描く**。
この票は**表示だけ**: 入力 (K7) と「CUI コマンドを端末から流す」経路は含めない。

## 1. 接点 (確認済み)

- KAPI v46 Rust バインディング: `sdk/rust/os32api/src/kapi_generated.rs` idx 189 `con_sink_read(buf, cap) -> i32`、
  idx 190 `con_sink_stat(pending, dropped) -> i32`。`cap >= CON_SINK_REC_MAX` (203) でないと `OS32_ERR_INVAL`。
  戻りはレコード境界で切られる。読み手は最初に読んだアプリ 1 本 (終了で解放)。
- ワイヤ形式: `PRINT` = type(1) + color(1) + len(1) + UTF-8 (≤200、UTF-8 の切れ目で分割済み)、`CLEAR` = type(1)、
  `CURSOR` = type(1) + x(1) + y(1)。改行 / CR は PRINT のバイト。
- `t5a_display`: `guest.rs` が `Session::new(cells, Fixture::Normal)` で固定 fixture を流している。
  `state.rs` (セル・スクロール)、`paint.rs`、`view.rs`、`boundary.rs` は T4/T5 で受入済み (ホスト試験あり)。
- GetMessage 方式: 待ちは `OP_WAIT` の中だけ。**ポーリングはタイマイベントで行う** (libos32gui のタイマ機構、
  gui_bench / gui_demo の使い方に倣う。100ms 周期程度)。busy loop は禁止 (協調型、他アプリを飢えさせる)。
- 版: `build/app.conf` の `t5a_display` (無ければ追加) を KAPI 46 に。

## 2. 設計 (PM 案)

1. `guest.rs` の fixture 供給を **sink 供給**に置き換える: タイマごとに `con_sink_read` を空になるまで
   (1 周の上限 = 8KB 分) 呼び、レコードを T4 モデルへ流す。`PRINT` → セルに UTF-8 を書く (色は既存の
   属性に写せる範囲で、無ければ無視)、`CLEAR` → 画面クリア、`CURSOR` → カーソル位置。
   `\n` / `\r` は T4 モデルの既存の解釈に任せる。
2. `dropped` が増えたら状態行に「dropped N」を出す (黙って欠けさせない)。
3. パース (バイト列 → レコード列) は `no_std` の純関数として **ホスト試験可能に分離** (`sink.rs` など)。
   境界ケース: 空、PRINT の len 0、未知 type (以降を捨てて `dropped` 相当を数える)、UTF-8 の 3 バイト文字。
4. Fixture の経路は host 試験用に残してよいが、ゲストでは sink だけ。

## 3. 受入 (ゲスト、PM / テスター)

| ID | 試験 | 合格条件 |
|---|---|---|
| A1 | 起動 | gshell から `/usr/bin/t5a_display.bin` を起動して端末窓が出る (空)。他アプリと共存 (4 本の枠内) |
| A2 | 表示 | 端末窓を出したまま**別アプリ**を起動 / 終了すると、カーネルがその過程で出す行 (例: `[shlib]`、exec のメッセージ、無ければ `ring3_fault.bin` を Run して `#PF … kill app` の行) が端末窓に現れる |
| A3 | あふれ | `dropped` が 0 のまま通常操作が続く。故意に大量出力する手段があれば dropped が表示に出る |
| A4 = C3/C4 | 読み手 1 本 / 回収 | 端末を 2 本起動すると 2 本目は表示できない (読み手拒否をステータスに出す)。1 本目を ESC で畳むと 2 本目が読める |
| A5 | 回帰 | regress 6 本、CUI 復帰 (Start → CUI mode) が従来どおり |

## 4. 禁止 / 範囲外

- 入力の受け取り (K7)、CUI コマンドの起動 (次段)。gshell の変更。カーネル / KAPI の変更 (必要なら止まって報告)。
- 配備・コミット・push・エミュレータ・ローカル AI・ini・.env・`make` は禁止 (コーダー)。ホスト試験 (crate の host テスト、
  `cargo check`) は可。

## 5. 実機受入の記録 (PM / テスター、2026-09-12、`abef34f` を HostDrv → `hsync`、t5a_display.bin 28,648 B、15MB、API v46)

| 受入 | obs | 判定 |
|---|---|---|
| **A1** | gshell → Run `/usr/bin/t5a_display.bin` で端末窓 (`Terminal (con_sink)`、状態行 `LIVE reading in=0B rec=0 …`) が出る | **合格** |
| **A2** | Run `/etc/system.cfg` (OS32X でない) → exec の `Error: invalid OS32X binary` が端末窓に出る (`in=31B rec=1`、リングは `g_count` 0 まで吸われた)。`con_sink_drop_count` 0 | **合格** |
| A2 註 | `ring3_fault.bin` を Run しても `[ring3] #PF … kill app` は出ない: 例外ハンドラは `serial_puts_polled` / TVRAM 直書き (`kernel/isr_handlers.c`、コンソール状態が壊れていても動く自己完結経路) で console.c を通らない。設計どおりだが、GUI 中の障害表示としては欲しいので**追随の小票候補** (fault 文脈から `con_sink_push_print` を安全に呼べるか要検討) | — |
| A3 | 通常操作 (起動 / 拒否 / 2 本目 / 終了) を通して `dropped` 0、`con_sink_drop_count` 0。故意のあふれは生成手段が無く未実施 | 部分合格 (あふれの実機は未実施、ホスト試験 `test_con_sink` のあふれケースで担保) |
| **A4** (= C3 / C4) | 端末 2 本目を Run → 状態行 `LIVE busy rc=-5` (`OS32_ERR_EXIST`)。タスクバーで 1 本目にフォーカスして ESC → 2 本目が `LIVE reading` に変わり、カーネルの `g_reader` は 2 → 3、`appslot_reclaim_count` +1 | **合格** |
| **A5** | Start → CUI mode で端末が畳まれ `g_reader` -1 / `g_enabled` 0 / `g_count` 0、regress 6 本 obs 全通過 (kselftest 50 / 0) | **合格** |

**判定 (PM、2026-09-12)**: A1 / A2 / A4 / A5 合格、A3 は部分 (あふれの実機生成手段なし)。**K6C-A 受入済み**。
追随候補: (1) 例外ハンドラの障害表示をシンクにも流す、(2) `CURSOR` (80×25 座標) を 40×64 モデルへ写す、(3) 色属性。
