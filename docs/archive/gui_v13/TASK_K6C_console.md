# K6C — console の差し込み口 (GUI モード中のカーネル出力を端末モデルへ)

> 発行: PM (2026-09-12) / 状態: **受入完了 (2026-09-12)**

受入済み (2026-09-12、`d381000`、C1〜C4 合格)。決裁 B の順序 (K5 → **K6 console** → K7 入力統合 → 端末アプリ) の 2 段目。
親: [PLAN.md](../../tasks/gui/v13/PLAN.md) §1。前提: K5b (GUI アプリ 4 本、`759d5bb` で main)。
ユーザー決裁: **端末は外部アプリ** (2026-09-10)。gshell は端末を持たない。

## 0. 目的

GUI モード中、カーネルと CUI コマンドが `kernel/console.c` の入口 (`shell_print*` / `console_write` /
カーソル系) に書く出力は、いまテキスト VRAM に描かれて (テキスト面は非表示) 消える。これを
**カーネル内のリング (シンク) に溜め、端末アプリが KAPI で吸って Paint する**ための差し込み口を作る。
この票は **K 側 (カーネル + KAPI v46) だけ**。端末アプリ側 (`userland/rust/t5a_display` が吸って描く) は
K6C-A として K 着地後に別票。K7 (入力統合: `kbd_getchar` ← GUI イベント、FEP) は含めない。

## 1. 接点 (確認済み)

- 入口は `kernel/console.c` の 6 本に集約されている: `shell_putchar` / `shell_print` (214) / `shell_print_dec` /
  `shell_print_hex32` / `shell_print_utf8` (250) / `console_write` (322)。`kprintf` もここへ落ちる。
  呼び出し元は 23 ファイル 128 か所あるが、差し込みは入口だけでよい。
- 既に `rshell_active` ならシリアルへ複写し、`v86_is_active()` なら描画を抑止している (= 「描画先を分ける」前例)。
- GUI モードの印: `console_text_gdc_stop()` / `console_text_gdc_start()` (437 行付近、gshell が GUI 入退場で呼ぶ)。
- 状態を変える入口: カーソル (`console_set_cursor`)、クリア (`cls` 相当の経路を洗い出すこと)、スクロールは
  リング側では「行の追加」に畳む。
- KAPI 版番号: **v46** (KAPI_SPEC §3-2 に「K6C console シンク」で予約してから実装。v43 はネットワーク予約)。

## 2. 設計 (PM 案、ここで決めた 3 点は変えない)

1. **リング容量 8KB、カーネル帯の静的配列** (kmalloc しない)。`MEMORY_BUDGET.md` に 8KB を計上する。
   あふれは **古い方を捨てる** (レコード単位)。捨てた回数をカウンタ `con_sink_drop_count` (カーネルシンボル、KAPI にしない) に積む。
2. **割込み文脈からの書き込みも通す**。push/pop は `cli`/`sti` (既存の IF 退避の作法) で守る。
   単一プロデューサを仮定しない。`v86_is_active()` 中も溜める (シリアル同様「出力は失わない」)。
3. **CUI へ戻るとき (`console_text_gdc_start`) はリングを捨てる** (テキスト VRAM が再び正になる)。
   GUI へ入るとき (`console_text_gdc_stop`) に空で始める。有効化のために別 KAPI は足さない — 既存の入退場に結び付ける。

レコード形式 (端末アプリが復元できる最小):

| type | payload | 発生源 |
|---|---|---|
| `PRINT` | `color u8`, `len u8`, UTF-8 バイト列 (最長 200、超えたら分割) | `shell_print*` / `console_write` / `kprintf` |
| `CLEAR` | なし | 画面クリアの経路 |
| `CURSOR` | `x u8`, `y u8` | `console_set_cursor` |
| `EXIT` | `id u8` | `exec_reclaim_owned` (票 T7 E1、2026-09-12 追加) — gshell 配下の子が畳まれた。正常終了 / `exec_kill` / fault の 3 経路すべてが通る |

改行 / CR は `PRINT` のバイトとして流す (端末モデルが解釈する)。スクロールはレコードにしない。

`EXIT` を積まないのは 3 つ: シェル自身 (`APP_ID_SHELL`) の終了、CUI モード中 (シンク無効)、
**読み手本人の退場** (受け取る相手がもう居ない)。読み手の照合に `con_sink_reader_get()` を使うので、
`con_sink_owner_exit()` で所有を返す**前**に判定する。

KAPI v46 (末尾追記、owner 制限は下記):

| 名前 | 引数 | 戻り | 備考 |
|---|---|---|---|
| `con_sink_read` | `void *buf, u32 cap` | 書いたバイト数 (0 = 空)、負 = エラー | レコード境界で切る (途中で切らない)。**読み手は 1 本だけ** — 最初に読んだアプリ ID が所有し、`exec_exit` / `exec_kill` の owner 回収で解放 |
| `con_sink_stat` | `u32 *pending, u32 *dropped` | 0 | 溜まっているバイト数と捨てた回数 |

CPL=3 からのポインタは既存のディスパッチャ検証 (アプリ帯 / SHM) に通す。

## 3. 受入 (ゲスト、PM / テスター)

| ID | 試験 | 合格条件 |
|---|---|---|
| C1 | CUI モード無影響 | regress 6 本、v86 -t、rshell が従来どおり。リングは触られない (`pending` 0) |
| C2 | GUI 中の出力が溜まる | gshell に入り、CUI コマンド (例: `ver`) を **将来の端末経路で**流す代わりに、K 側単体では **kselftest にケースを足して**リングの push/pop/あふれ/CUI 復帰時の破棄を確かめる (ゲストの kselftest_pass が +N) |
| C3 | KAPI | `v12_api_test` 相当の小さな CPL=3 試験で `con_sink_stat` が読めること、2 本目の読み手が拒否されること |
| C4 | 回収 | 読み手アプリを ESC / CTRL+STOP で畳んだ後、別アプリが読み手になれる |

## 4. 禁止 / 範囲外

- gshell に端末の描画を持たせない (T5b は撤去済み、契約 v13: WM はクライアント面を持たない)。
- `kbd` / 入力側は触らない (K7)。
- 配備・コミット・push・エミュレータ・ローカル AI・ini・.env・`make` は禁止 (コーダー)。ホスト TDD は必須。

## 5. 実装メモ (コーダー、2026-09-12)

- 差し込みは **4 入口**。`shell_print_dec` / `shell_print_hex32` は `shell_print` へ落ちるので
  そこに置くと二重に積む。クリアの経路は `tvram_clear()` 1 本 (`cls` も KAPI も全部ここ)。
- リングは「バイトの環」で、レコード長は先頭バイトの型から導く (固定長スロットだと
  8KB ÷ 203 = 40 本しか入らない)。ワイヤ形式は端末アプリと共有するので
  `os32_kapi_shared.h` の `CON_SINK_*` に置いた ([C4] Shared Layer)。
- 票に無い判断を 2 つ足した: **(a)** `cap < CON_SINK_REC_MAX` (203) は `OS32_ERR_INVAL`
  — 0 を返すと呼び側が「空」と区別できず読み手が止まる。**(b)** 200 超の PRINT の分割は
  **UTF-8 の切れ目**で戻す (200 = 3×66+2 なので素朴に切ると 67 文字目が割れる)。
- `console_text_gdc_stop/start` の差し込みは `v86_is_active()` のガードより**前**に置いた
  (GDC を触らなくても「テキスト面が見えない」ことは変わらない)。CUI 復帰は中身を捨てる
  だけで読み手の所有は返さない — 返すのは `exec_reclaim_owned` の (8) だけ。
- KAPI v46 に `sys_ram_kb` (K6-RAM 決裁 (2)) が相乗りした。`mem` が使うので
  `build/app.conf` の `userland/shell` を 7 → 46 に上げた (gshell は 45 のまま)。
- **未確認**: 実機・`make` は未実施。割込み文脈からの同時書き込みはホストでは
  再現していない (錠は空に差し替え)。受入 C1〜C4 は全部これから。

## 6. 実機受入の記録 (PM / テスター、2026-09-12、`d381000` 配備、vmkernel 457,062 B、API v46)

| 受入 | obs | 判定 |
|---|---|---|
| ゲート | `make clean/all/external/check` exit=0 ([ABI3]) | 合格 |
| **C1** | regress 6 本 obs 全通過、`v86 -t` result OK、rshell 従来どおり、`gui_demo` 起動 → Start → CUI mode 復帰、`con_sink_drop_count` 0 | **合格** |
| **C2** | kselftest **44 → 50** (fail 0) | **合格** |
| `mem` | `RAM : 16384 KB (16 MB) usable (15-16MB system space excluded)` が `Physical : 17408 KB` の下に出る (K6-RAM 決裁 (2)) | 合格 |
| **C3 / C4** | K6C-A の A4 で確認: 2 本目は `ERR_EXIST` (busy)、1 本目の ESC で読み手が移る (`g_reader` 2 → 3) | **合格** |

## K6C-2 — GUI 中の二重表示 (テキスト面の残像、2026-09-12)

- 症状: シンク有効中も `console.c` がテキスト VRAM に描き続け、gshell の GFX 画面に残像 (`text_disp`=1)。
- 修正: `console_render_allowed()` = `!v86_is_active() && !con_sink_is_enabled()` を `shell_print` /
  `shell_print_utf8` / `console_write` / `shell_putchar` に、`con_sink_is_enabled()` を `tvram_clear` /
  `console_set_cursor` に。抑止中は `cursor_x/y` も進めない。GUI 入場の追加処理は不要 (`gfx_init` が既にテキスト面を消す)。
- CUI 復帰は `console_text_gdc_start()` が GUI から戻ったときだけ `tvram_clear()` し、画面と論理位置 (0,0) を揃える。
- 検査: `tools/tests/test_con_sink.py` 9a〜9p (修正前は 8 件 FAIL)、kselftest 2 項追加。実機・`make` は未実施。
