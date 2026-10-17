# GUI シェル v1.1 — 作業分担票 (全体)

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> 親: [DESIGN.md](DESIGN.md) / 契約: [API_CONTRACTS.md](API_CONTRACTS.md) (2026-09-04 凍結)
> 書式は v2 の [PLAN.md](../../archive/kernel_v2/PLAN.md) / [CONTRACTS.md](../../archive/kernel_v2/CONTRACTS.md) に倣う。
> ロードマップ: `docs/ROADMAP.md` v1.1

体制は v2 と同じ 3 層 (設計 = PM、コーディング = コーダー、ビルド検証 = `os32-cycle` を回す
検証層)。レーンは 4 本で、**契約 (API_CONTRACTS.md) に対して独立に実装する**。契約を動かす
必要が出たら PM に戻す (勝手に変えると他レーンが黙って壊れる)。

---

## 1. レーンと票

| レーン | 言語 / 場所 | 票 | 内容 |
|---|---|---|---|
| **H** HAL | C, `gfx/` `drivers/` | [TASK_H1_hal_backend.md](../../archive/gui_v11/TASK_H1_hal_backend.md) | バックエンド表 + 9801 プレーン実装 + カウンタ |
| | | [TASK_H2_pegc.md](../../archive/gui_v11/TASK_H2_pegc.md) | PEGC 256 色バックエンド (v1.1 後半) |
| | | [TASK_H3_cirrus.md](../../archive/gui_v11/TASK_H3_cirrus.md) | Cirrus GD54xx チップドライバ + Xe10 グルー + ai-debug screenshot 拡張 (v1.2) |
| **K** カーネル背骨 | C, `kernel/` `exec/` `sdk/kapi.json` | [TASK_K1_gui_call.md](../../archive/gui_v11/TASK_K1_gui_call.md) | `gui_call` / `gui_register` KAPI、SHM 末尾の GUI 領域予約 (契約 T2)、共有ヘッダ、エラー番号 |
| | | [TASK_K2_pump_hook.md](../../archive/gui_v11/TASK_K2_pump_hook.md) | syscall 境界の入力ポンプ (T6) |
| | | [TASK_K3_shared_lib_band.md](../../archive/gui_v11/TASK_K3_shared_lib_band.md) | 共有ライブラリ帯域 0x400000〜0x4FFFFF とロードアドレス移動 (最後) |
| | | [TASK_K4_gui_boot.md](../../archive/gui_v11/TASK_K4_gui_boot.md) | `/etc/system.cfg`、テキスト GDC 制御、`os32gui` コマンド |
| **W** WM (gshell) | Rust, `userland/gshell/` (新規) | [TASK_W1_wm_core.md](../../archive/gui_v11/TASK_W1_wm_core.md) | gshell 本体: ウィンドウ管理、スロット、イベントリング、損傷/commit、クローム、カーソル、タイマ |
| | | [TASK_W2_fep_lease_modal.md](../../archive/gui_v11/TASK_W2_fep_lease_modal.md) | FEP を WM で持つ、14 色リースと 2 色クローム、モーダル、標準ダイアログ |
| **C** クライアント | Rust, `userland/rust/libos32gui/` | [TASK_C1_drawing.md](../../archive/gui_v11/TASK_C1_drawing.md) | G 描画 (Rect / Style / Surface / クリップ / 文字) を libos32gfx の上に |
| | | [TASK_C2_client_loop_widgets.md](../../archive/gui_v11/TASK_C2_client_loop_widgets.md) | `gui_call` スタブ、U3 ループ、ウィジェット木、箱レイアウト、gui_demo 書き換え |
| | | [TASK_C3_shared_lib.md](../../archive/gui_v11/TASK_C3_shared_lib.md) | 固定アドレス・ジャンプ表・バージョン照合の共有ライブラリ化 (K3 と同時) |
| **W** (追加) | Rust, `userland/gshell/` | [TASK_KBD_NAV.md](TASK_KBD_NAV.md) | キーボードだけの GUI 操作 (WM ショートカット + KANA ラッチのマウスキー)。設計票、実機でバスマウスが無いため (2026-09-25) |
| **PM** | Python, `tools/` | (票なし、本書 §5) | 共有定数の照合スクリプト、ゲート検証、`os32-cycle` の GUI 用サブコマンド |

## 2. 依存と順序

```
        H1 ─────┐
        K1 ─────┼──► W1 ──► K2 ──► W2 ──┐
        C1 ─────┤     │                  ├──► K3 + C3 (一斉再ビルド)
        K4 ─────┘     └──► C2 ─┘         │
                                         │
        H1 ──► H2 ──► H3 (screenshot 拡張は H3 の前提)
```

- **並列で始められるもの**: H1、K1、C1、K4。互いに触るファイルが重ならない (§3)。
- **W1 は K1 と H1 と K4 の後**: `gui_register` (K1) が無いと WM がアプリから呼べず、カウンタ
  (H1) が無いと性能規約 P を測れず、`sys_switch_shell` (K4) が無いと CUI から gshell を
  起動できない (契約 T9。gshell は shell.bin と同じ 0x300000 に載るので exec では起動できない)。
- **C2 は K1 の後**: op 番号と SHM レイアウトの共有ヘッダ (K1 成果物) を写す。動作確認は
  W1 と結合してから。
- **K2 は W1 の後**: ポンプの呼び先 (`gui_pump`) は W1 が作る。
- **K3 + C3 は最後**: ロードアドレス移動は os32 / os32-apps / os32-game の全 OS32X を
  一斉再ビルドして NHD を入れ替える (stale の罠)。他レーンが全部終わってから 1 回で行う。
- **H2 は H1 の後、H3 は H2 と screenshot 拡張の後**。v1.1 の完了条件には含めない
  (ROADMAP の「v1.1 後半〜v1.2」)。

## 3. 衝突ゾーンの所有権 (v2 C7 と同じ流儀)

| 所有 | ファイル |
|---|---|
| H 排他 | `gfx/**`、`include/gfx_hal.h` (新規)、`drivers/wab_*.{c,h}` (新規、H3) |
| K 排他 | `kernel/**` `exec/**` `include/memmap.h` `sdk/kapi.json` と生成物 4 つ、`sdk/include/os32/os32_kapi_shared.h`、`sdk/include/os32/os32_gui_shared.h` (新規)、`sdk/link/app.ld`、`userland/shell/**` (K4 の `os32gui`) |
| W 排他 | `userland/gshell/**` (新規) |
| C 排他 | `userland/rust/libos32gui/**`、`userland/rust/gui_demo/**`、`sdk/rust/os32api/src/gui/**` (新規、共有定数の Rust 側) |
| PM | `tools/check_gui_proto.py` (新規)、`docs/tasks/gui/**`、`docs/ROADMAP.md` |
| 共有 (PM 経由で追加) | `build/programs.mk`、`userland/deploy.yaml`、`docs/KAPI_SPEC.md`、`CLAUDE.md` |

- KAPI に足すのは K だけ。H が KAPI を要るときは K1 の票に「H1 からの依頼」として載せる
  (本書発行時点で H1 が要る KAPI は §4 の `gfx_stats` / `gfx_lease_palette` の 2 本)。
- `sdk/rust/os32api/src/kapi_generated.rs` は K の再生成物。C は触らない。

## 4. 契約から先に固める共有定数 (K1 の成果物、他レーンはこれを写す)

`sdk/include/os32/os32_gui_shared.h` に C で置き、`sdk/rust/os32api/src/gui/proto.rs` に
C レーンが同じ値を写す。PM の `tools/check_gui_proto.py` が両者を突き合わせ、`make check`
に入る。**以後は末尾追記のみ** (契約 T5)。

| 項目 | 提案値 (K1 が確定させる) |
|---|---|
| `GUI_PROTO_VERSION` | 1 |
| SHM | [契約 T2](API_CONTRACTS.md#t2-shm-レイアウト--確定-2026-09-04) と `include/memmap.h` / `os32_gui_shared.h` の定義を参照 |
| op | 0 予約、1 `INIT`、2 `POLL`、3 `WAIT`、4 `COMMIT`、5 `INVALIDATE`、6 `STATS`、7 `LEASE_PALETTE`、16〜 ウィンドウ (`CREATE` `DESTROY` `MOVE` `RESIZE` `SHOW` `SET_TITLE` `CLIENT_RECT` `RAISE` `SET_FOCUS` `SET_TEXT_CURSOR`)、32〜 サーフェス (`CREATE` `DESTROY`)、48〜 タイマ (`SET` `KILL`)、64〜 モーダル (`OPEN`)、80〜 予備 |
| イベント種別 | 契約 U2 の並び順どおり 1 から: `PAINT` `CONFIGURE` `CLOSE` `FOCUS` `KEY` `TEXT` `POINTER` `BUTTON` `TIMER` `WIDGET` `MODAL` `QUIT` `PALETTE` |
| イベント配置 | **16B = 共通ヘッダ 8B (`kind` u8 @0, `sub` u8 @1, `serial` u16 @2, `window` u32 @4 = index:16 \| generation:16) + ペイロード 8B** (契約 U2 の表)。C / Rust とも大きさとオフセットを static assert |
| エラー | `OS32_ERR_STALE -11` `OS32_ERR_VERSION -12` `OS32_ERR_FULL -13` `OS32_ERR_ARG` は既存 `OS32_ERR_INVAL` (-9) を使う |
| 色 | 契約 G6 の 16 色を `GUI_COLOR_*` の役割名で、RGB は `GUI_SYSTEM_PALETTE[16]` |
| `Style.flags` | `TRANSPARENT_BG 0x01` `XOR 0x02` `DOTTED 0x04` `DITHER50 0x08` |
| 上限 | ウィンドウ 16、サーフェス 16、ウィジェット 64、リスト項目 128、タイマ 8/アプリ、クリップ深さ 8、損傷 8/ウィンドウ、文字列 256B |
| 追加 KAPI (v41) | `gui_call(u32 op, u32 arg) -> i32`、`gui_register(handler, pump) -> i32` (shell 帯からのみ)、`gfx_stats(void *out)`、`gfx_lease_palette(first, count, const u8 *rgb)`、`sys_switch_shell(const char *path) -> i32` (shell 帯からのみ、K4)、`kbd_dropped_count(void) -> u32` (カーネルのキー待ち行列が捨てた打鍵の累計。`drivers/kbd.c` の満杯分岐で加算) |

> **版番号確定 (2026-09-06、同日改訂)**: GUI は **v42** (ime_feed_key / ime_set_render を含む 180 関数。開発中は v41 と呼んでいた)、ネットワークの Host Services ([../network/LINK_PLAN.md](../network/LINK_PLAN.md)) は **v43**。ユーザー承諾済み。版番号割り当ての正典は [KAPI_SPEC §3-2 の予約表](../../KAPI_SPEC.md)。

## 5. ゲート (検証層が `os32-cycle` で回す。各票の完了条件はゲートに対応させる)

| ゲート | 名前 | 通過条件 | 検証 |
|---|---|---|---|
| G0 | 契約凍結 | 済 (2026-09-04)。**G0b: 契約の実装可能性** (2026-09-05 追加) = 契約末尾の 5 筋書き (イベント配置 / 重なった 2 窓 / リング満杯 / 変換中の BS / CUI ⇄ gshell) を PM が机上で通し、`check_gui_proto.py` のオフセット照合が動く | 机上 + スクリプト |
| G1 | 描ける | C1 の `gdi_test` が G API だけで契約 G6 の 16 色見本・文字・クリップを描き、H1 の `gfx_stats` が present バイト数を返す。既存 GFX テスト (gfx_demo / blit_test / hello_gfx) に回帰なし | `os32-cycle demo gdi_test` の screenshot を `docs/tasks/gui/ref/g1.png` と目視比較、`stats` を tvram で確認 |
| G2 | 窓が出る | CUI シェルから `os32gui` で gshell に切り替え (T9)、その上で書き換えた `gui_demo` (別プロセス, CPL=3) が窓 2 枚・XOR ドラッグ・フォーカス・ボタン・テキストボックス・リストボックスを動かす。重なった 2 窓で背面の再描画が前面を壊さない (G4)。1 周の syscall が `POLL` + `COMMIT` + `WAIT` の 3 回、代表操作の転送量が P2 の予算内 | `/api/key` で操作、`/api/screenshot`、`gui_bench` の tvram 出力 |
| G3 | デスクトップで起動 | `GUI=1` の `system.cfg` で起動時から gshell が上がる (K4-1)、`os32gui` / 「CUI へ」の切替が再起動なしで往復する (T9)、FEP が WM で動き (SHIFT+SPACE → 変換 → `Text` 配送)、14 色リース中に 2 色クロームが出る、モーダルが入れ子ループ無しで閉じる、計算ループ中のアプリ (KAPI は呼ぶ) でもカーソルが追従しクリックが失われない (K2、T8 の限界どおりメニューは開かない) | `os32-cycle deploy` からの起動確認 + G2 と同じ操作 + FEP 手順 (メモリ os32-fep-testing) |
| G4 | 共有ライブラリ | ライブラリが 0x400000 に常駐、アプリが 0x500000 から動き、os32-apps / os32-game が再ビルドで無変更動作。`gui_demo` の .bin がライブラリ分だけ小さくなる | `heap_test` の guard_a 表示、`make external` + 全 deploy.yaml の起動確認 |
| G5 | 9821 (v1.1 後半〜) | PEGC 256 色で G1〜G3 と同じ絵、Cirrus Xe10 で HW 塗り / BLT の能力ビットが立ちカウンタの hw_ops が増える | np21x64w.ini の切替 ([D2] 承認要) + screenshot 拡張 |

- 「配備完了」の文言だけを信じない ([V1]、メモリ os32-nhd-deploy-lock)。ゲートは必ず NHD
  配備 + 再起動後の実機到達で判定する。
- **性能は NP21/W で測れない** (DESIGN §8)。ゲートの数値判定はカウンタ (回数) で行う。

## 6. 全レーン共通の鉄則

1. **契約を動かさない。** 足りないものは PM へ。op / イベント / 構造体は末尾追記のみ。
2. **ポインタを SHM とイベントに載せない** (契約 T1)。文字列は長さ前置で値渡し。
3. **決め打ち禁止**: 640×400 / 16 色 / プレーンを GUI と WM に書かない。`gfx_screen_info()`
   を信じる (契約 G5)。H 以外がポート番号や VRAM アドレスを書いたら差し戻し。
4. **Rust は no_std、外部クレート無し、描画は libos32gfx を FFI** (v2 C8 をそのまま継承)。
5. **内部 C は GNU11、公開 SDK ヘッダは C89/GNU89 互換、SQLite は GNU89。`kstr*`、三層定数** ([C1] [C2] [C4])。ポート・ID は資料の出典をコメントに
   (`docs/hw/undocumented/io_*.md`。ミラーは git 管理外)。
6. **検証は [V1]〜[V4]**: deploy.yaml に載せる、NHD 配備で確かめる、失敗はそのまま書く。
7. **`get_tick` スピン禁止** (v2 M0)。GUI イベント待ちは `OP_WAIT`。park の条件は [契約 T8](API_CONTRACTS.md#park-origins)。

## 7. 進捗 (v1.1 の関門記録)

条件は §5、操作・出力・調査経緯は [受入記録](../../archive/gui/GUI_V11_ACCEPTANCE_LOG.md)。
以下は NP21/W の NHD 配備での判定であり、物理実機の性能判定ではない。

| 関門 | 判定 | 証拠・持越し |
|---|---|---|
| G0 | 凍結済み | [契約の凍結記録](../../archive/gui/GUI_API_HISTORY.md#凍結の記録) |
| G1 | 通過 (2026-09-05) | 受入記録の K1/H1/C1/K4 結合行 |
| G2 | 通過 (2026-09-06) | 受入記録の C2 結合と main マージ行。ドラッグ・重なりは mouse API 追加後に確認 |
| G3 | 通過 (2026-09-06) | 受入記録の G3 行。FEP キャレット位置は同日の 9801 回帰行で確認 |
| G4 | 通過 (2026-09-06) | 受入記録の K3/C3 行。版不一致拒否の当時のゲスト未実行は下記に保持 |
| G5 | 通過 (2026-09-06) | 下記 G5 と受入記録の H2c / H3b / G5 後半行 |

<a id="gui-g5"></a>

### G5 の確認範囲

PEGC の 480 ライン・TVRAM 合成・CUI 復帰と、Cirrus Xe10 の HW 塗り/BLT・FEP・CUI 復帰を確認。
WAB screenshot は np21w-src `337e7ff` で対応し、出力元 `X-Screen-Source: wab` とリレー状態を確認した。
WAB OFF 後の PEGC/9801 回帰も通過。詳しい条件と出力は [受入記録](../../archive/gui/GUI_V11_ACCEPTANCE_LOG.md)。

### 未確認事項の扱い

当時の未実行を完了扱いにしない。shlib 版不一致拒否は現行 ABI の
[H-7](../DEFERRED_TESTS.md) の旧 shlib 拒否証拠を参照する (旧 GUI_PROTO_VERSION 手順とは別)。
commit 前描画の目視、起動失敗時の巻戻し全経路、Cirrus setup 失敗からの 9801 フォールバック、
`gui_busy --nokapi` + CTRL+STOP、P2 の syscall/転送量測定は、この票の旧記録だけでは完了を証明できない。
対象経路は現行コードにもあるため、後続受入との対応確認が済むまで未確認として保持する。
旧 raw/cooked 問題は `input.rs` / `kbd.c` の GUI モード専用 raw 配送で解消済み。

### 記録しておく v1 の限界 (W1 の申し送り)

- **commit 前の描画が表示面に出うる**: ソフトウェアバックエンド (9801) は単一バックバッファを WM とアプリで共有するので、WM の X3 present (クローム / デスクトップ) がアプリの未 commit 描画を巻き込む。契約 G4 は「バックエンドの責任」としており、v1 では許容。PEGC / Cirrus (H2 / H3) でサーフェスを分けられれば解消。
- **K3 の副作用** (2026-09-06): 子プロセスの空間が 1MB 減る (8MB 機で本体上限 ≈1.2MB)。shlib ロード時に帯域 1MB を pgalloc から永久に取る。`EXEC_DYN_RESERVE` の穴 (1MB) を V86 バッキング 640KB と per-app data で分けるので、ライブラリ .data/.bss が 300KB を超えると V86 と競合。`--cpl0` バイナリは master PD なので .data を共有 (仕様)。gshell 常駐中は hotdeploy のポーリングが止まる (K2)。
- **cooked キュー**: GUI 中は cooked に積まず raw リングだけを配送する (K2 実装済み)。
- W1 の解釈 (契約に受け渡し場所の記述が無かったもの): `OP_INIT` の proto_version は arg、単一ハンドル op は arg≠0 ならハンドル / 0 なら `GuiReqWindow`、`OP_STATS` は応答ブロック先頭に `GFX_Stats` 生、P2 の取り込み tick はスロット +11280 に 64×4B (`u16 serial` + `u16 tick`)、`Configure` の rect は画面絶対座標 / `Paint` はクライアントローカル。タイマは契約 U5 のとおり (レビュー #3 ⑤で修正、反復固定は撤回)。C2 はこれに合わせてある。`Key` は down=false (break) も届く (契約 U2a、クライアントは編集に使わない)。gshell 配下のアプリは `libos32gfx_init()` を呼ばず `client::init()` の attach を使う (gfx_init が VRAM とパレットを壊す)。
