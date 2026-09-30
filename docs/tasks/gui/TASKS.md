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
| **K** カーネル背骨 | C, `kernel/` `exec/` `sdk/kapi.json` | [TASK_K1_gui_call.md](../../archive/gui_v11/TASK_K1_gui_call.md) | `gui_call` / `gui_register` KAPI、SHM ブロック 12〜15 予約、共有ヘッダ、エラー番号 |
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
| SHM | `MEM_SHM_GUI_BASE = MEM_SHM_BASE + 192KB`、スロット 16KB × 4、オフセット: ヘッダ 0 / 要求 16 / 応答 528 / リング 1040 (128 × 16B) / 引数 3088 (8KB) |
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
5. **C89、`kstr*`、三層定数** ([C1] [C2] [C4])。ポート・ID は資料の出典をコメントに
   (`docs/hw/undocumented/io_*.md`。ミラーは git 管理外)。
6. **検証は [V1]〜[V4]**: deploy.yaml に載せる、NHD 配備で確かめる、失敗はそのまま書く。
7. **`get_tick` スピン禁止** (v2 M0)。待つのは `OP_WAIT` → `sys_halt` だけ。

## 7. 進捗 (PM が更新。ゲートは実機 = NP21/W の NHD 配備で判定)

| 日付 | 事項 |
|---|---|
| 2026-09-05 | K1 (KAPI v41 / 共有ヘッダ)、H1 (GfxBackend 表 / backend_pc98)、C1 (G 描画 API / gdi_test)、K4 (system.cfg / CUI⇄gshell 起動ループ / os32gui) を feat/gui に結合。**G1 通過** (gdi_test が描画し `gfx_stats` の present_bytes=512000)。レビュー 2 回 (11 点) 反映 |
| 2026-09-06 | **W1 結合** (gshell: handler / pump / cursor / desktop / wm / slot / ring / damage / visible / chrome / input / timer、`make gshell` で 0x300000 リンク、`/bin/gshell.bin`)。カーネル修正 §4-19 (CPL=3 の KAPI 中に IF=0 で hlt が永久停止 → `int80_stub` で sti / 出口 cli。G2 以降の `OP_WAIT` の前提)。**G2 の W 側 (単独)**: `os32gui` → デスクトップ (背景・手引き・カーソル) → ESC → CUI、3 往復で `ver` 応答。残りの G2 (gui_demo の窓 2 枚、ドラッグ、G4 の重なり、契約 P) は C2 結合後 |

| 2026-09-06 | **K2 結合** (syscall 境界ポンプ、CTRL+STOP で CPL=3 を畳む経路を新設、GUI 中は cooked に積まない)。**C2 結合** (libos32gui: client / app (U3) / window / widget / layout / timer、gui_demo 書き換え、lease_test、gui_bench)。**レビュー #3 (6 点) 反映**: ① 可視領域の打ち切りは部分集合のみ、② raw キーにイベント時点の修飾 (`keycode \| down<<8 \| mods<<9`)、③ raw は GUI 中のみ + 切替時に破棄、④ 切替要求は `gui_take_next_shell` の consume 方式、⑤ タイマ ABI を契約 U5 に (`u8 id / u8 repeat / u16 interval_ticks`)、⑥ フォーカス = 最前面の可視窓 (hide / create で Focus を流す)。実機検証: `make all` → NHD 配備 → kselftest 42/0 → gdi_test 復帰 → `os32gui` → **F1 で gui_demo の窓 2 枚 (Widgets / Help) が出て TAB でフォーカスが巡回、ESC でアプリの窓が全部消え (owner 回収)、ESC で CUI へ、ver 応答** = **G2 の主要項目通過** (screenshot: tools/emu_agent/logs/20260906-034431/shots/step12・16・19)。未確認: XOR ドラッグと重なりの再描画 (G4) は **NP21/W ai-debug にマウス注入 API が無い** (`/api/key` のみ) ため自動化できず、`/api/mouse` の追加 (np21w-src 側) が要る。契約 P の syscall 回数は gui_bench で次回 |

| 2026-09-06 | **W2 結合** (FEP を WM で: SHIFT+SPACE / 未確定行と候補窓の GFX 描画 / Text 配送、14 色リースと 2 色クローム、モーダルと標準ダイアログ、F1〜F5 起動)。K 依頼 2 本を PM が実装: **`ime_feed_key` / `ime_set_render` を KAPI v41 末尾に追記** (180 関数。v41 は main 未リリースのため同じ版、v42 のネットワーク予約は不変)。共有ヘッダに `GUI_MODAL_*` を追記。未定義: ファイル選択のパスをアプリへ返す `GUI_OP_MODAL_RESULT` (追記候補)。K3 / C3 (G4) を投下中 |

| 2026-09-06 | **G3 通過 (実機)**: 一斉再ビルド (`make clean` → `make all`) → NHD 配備 → kselftest 42/0。(1) FEP: gui_demo のテキストボックスで SHIFT+SPACE → `nihongo` → SPACE → RETURN で「日本語」が入る (`ime_feed_key` / GFX 版 `IME_Render` 経由)。(2) リース: lease_test (F4) で 14 色が独自パレット、クロームが白黒 2 色・デスクトップが市松、ESC で復元。(3) ポンプ: gui_busy (F3) の計算中に 20 打鍵 → `POLL=68 (key=40 text=20 ptr=5)`、`flags=0 dropped=0 kbd_dropped_delta=0` (偽 OVERFLOW 無し)。(4) モーダル: F5 のファイル選択が開閉。(5) `os32gui on` → `system.cfg GUI=1` → リセットで gshell が起動時に上がる → ESC で CUI → `os32gui off`。screenshot は scratchpad の `g3_*.png` (セッション内)。**残課題**: FEP の `[あ]`/候補窓が左下固定 (テキストカーソル位置に出ない — C2 の textbox が `SET_TEXT_CURSOR` を送っているか要確認)、`ime_toggle` が TVRAM に "Dict loaded" 等を出す (GUI 中は抑止したい)、gui_busy の窓は WM の X3 が回らないので枠が出ない (契約 T8 の限界として記録)、`--nokapi` + CTRL+STOP は起動手段 (引数付き起動) が無く未検証、gshell 常駐中は hotdeploy が効かない (K2 の注記) |

| 2026-09-06 | **K3 結合・K 側 G4 通過 (実機)**: `make clean` → `make all` → 配備 → kselftest 42/0。`heap_test` が `load = 0x500000`、`guard_a = 0x680000`。`ring3_guard` は従来どおり kill (addr=0x7BF000)、`ring3_guard shlib` は `addr=0x00400000 [shlib band, WRITE] -> kill app` で shlib .text の書き込みを捕捉。gdi_test / klibc_test 49/49 は 0x500000 でも無変更で通る。shlib 未配備時は `[shlib] not found` で CUI が通常起動。`make external` で apps / game を 0x500000 で焼き直して配備、`hello32` と `ui_demo` (microUI) が無変更で起動。ui_demo を **CTRL+STOP** で畳めた (`ring3_abort_count` / `fault_kill_count` が +1、rshell 生存 = K2 作業 4)。submodule ポインタは変更なし。C3 (libos32gui.shlib) 結合が残り |

| 2026-09-06 | **C3 結合・G4 通過 (実機)**: `libos32gui.shlib` (84,920 B、ジャンプ表 95 本、text 17 ページ + data 4 ページ) を `/sys/lib` に配備、K3 のローダが 0x400000 に載せる。**gui_demo.bin 67,208 → 13,132 B (−80%)**、lease_test 62,312 → 9,644、gui_bench 66,260 → 12,808 (gdi_test は単独 GFX なので 30,420 → 22,312)。shlib 経由の gui_demo が窓 2 枚・チェックボックス ON/OFF、lease_test (14 色) → gui_demo の連続起動で状態が混ざらない (アプリごと .data ページ)。`make check-shlib` で番号表を突き合わせ。**未実行**: 版不一致の拒否 (`GUI_PROTO_VERSION` と `shlib.rs` の `.long` を両方上げてライブラリだけ焼き直す手順。コードレビューで経路は確認、実機は次回) |

| 2026-09-06 | **H2 結合** (`5bcf7a0` + kernel.mk 登録): PEGC 256 色バックエンド、⑤ boot 順反転 (probe → init)、`paging_map_phys`、300KB バックバッファは物理末尾に予約。**実測**: NP21/W (SUPPORT_PC9821 / SUPPORT_PEGC 込み) は現行 ini (`ExMemory=16`) のままで probe が通り、`hal_test` = 640×480 / bpp 8 / PACKED8 / lease 16〜240、`gdi_test` は無変更で `present_bytes=307200 commits=1`、kselftest 42/0。**ini 変更 ([D2]) は不要**。**gshell は起動直後に #PF** (`asm_gfx_hline` の `rep stos` が NULL へ: libos32gfx が 4 プレーン決め打ちで `planes[1..3]=NULL` を踏む) → K4 の保険で CUI に戻る。対策 = H2b (libos32gfx の PACKED8 対応 + `GFX=pc98|pegc|auto` の強制指定。NP21/W が常に PEGC 相当になったので 9801 経路の回帰にも `GFX=pc98` が要る)。TEXT_OVERLAY / 480 ライン / 24kHz 復帰は gshell 復旧後に確認 |

| 2026-09-06 | **レビュー #4 (6 点) 反映**: ① gshell 終了時に `ime_set_render(NULL)` (gfx_shutdown より前) + カーネル `gui_owner_exit(1)` でも防御的に NULL、② X4 (ポンプ) で FEP オン中の raw 打鍵は WM の退避キュー (32) に積み、変換 (辞書検索) は次の X3 でだけ、③ `kbd_set_gui_mode(1)` で `kbd_dropped` / `kbd_raw_dropped` を 0 に (GUI セッションの起点 = gshell の baseline 0 と一致)、④ モーダル中の X4 は `prev_buttons` を進めない (エッジは X3 が拾う)、⑤ 可視領域 16 超の窓は OP_POLL ごとに `vis_rot` で計算順を回し捨てる断片を入れ替える (`visible::page_vis`)、⑥ `shlib_addrspace_attach` 失敗は `EXEC_ERR_NOMEM` で起動前に戻す。**KAPI v41 への追記は main マージ前に再確認** (レビュー指摘) |

| 2026-09-06 | **KAPI を v42 として確定** (`aec21de`、ユーザー承諾。ネットワークは v43)。**H2b 結合** (`324d15b`): libos32gfx の PACKED8 対応、`GFX=pc98|pegc|auto` / `gfxmode`。一斉再ビルド → 配備: `ver` が `API: v42`、kselftest 42/0、klibc 49/49、hal_test `backend pegc (packed 8bpp) 640x480 bpp=8`、gdi_test 307200。**PEGC で gshell が #PF なしに動き gui_demo の窓 2 枚が描ける**。**未解決 (H2c)**: (1) パレットがほぼ黒 (全色が暗い — PEGC 256 色モードのパレット書き込み経路が NP21/W の実装と合っていない疑い)、(2) 表示が 640×400 のまま (`/api/status` scrn_ymax=400、H2 が予告した 480 ライン SYNC 未設定)、(3) `grph_disp=0`。NP21/W のソース (`~/np21w-src`) が正典なので、それに合わせて直す |

| 2026-09-06 | **9801 プレーン経路の回帰 (`GFX=pc98`) 通過**: `gfxmode pc98` → **reset** (deploy は `/etc/system.cfg` を巻き戻すので設定切替の検証は reset で) → hal_test `backend pc98 (planar 4bpp) 640x400 bpp=4`、gdi_test 512000、gshell → gui_demo → FEP で「日本語」入力 → ESC → CUI → `gfxmode auto` に戻した。レビュー #4 ② の退避で **SHIFT+SPACE 直後の打鍵が FEP を素通りする順序問題**が出た (変換されず "nihongo") → 退避条件を「FEP オン / toggle 保留中 / 退避済みあり」に広げて解消 (`41e2de8`、hotdeploy で再確認: `[あ]▼日本語(01/02)` の候補窓がキャレット位置に出て「日本語」が入る)。gshell の hotdeploy は CUI で rshell が生きているときだけ効く |

| 2026-09-06 | **H2c 結合 → G5 前半 (PEGC) 通過 (実機)**: `/api/status` が `scrn_ymax=480` / `grph_disp=1`、hal_test `backend pegc 640x480 bpp=8`、gdi_test 307200、gshell のデスクトップと gui_demo が **640×480 / 正しいシステム 16 色**で描ける、ESC で CUI へ戻ると 400 ライン / `grph_disp=0` に復帰しテキストが正常。**TEXT_OVERLAY**: gui_busy (F3) の kprintf が 256 色の絵の上に合成されて見えた → `GFX_CAP_TEXT_OVERLAY` を立てる (DESIGN §5 に書き戻し)。残る G5 後半は H3 (Cirrus Xe10、NP21/W の WAB CL-GD54xx を Enable にする [D2]) |

| 2026-09-06 | **H3 着手** (ユーザー承認: NP21/W の WAB CL-GD54xx を Enable / Xe10 built-in に切り替えるのは H3 のコード完成後に PM が行い、検証後 OFF に戻す)。並行して np21w-src 側で `/api/screenshot` の WAB 出力対応 (票 §0 の前提) と `/api/status` の `wab_relay` を実装中。どちらも Opus 5 |

| 2026-09-06 | **H3 結合** (`0d04a03` + kernel.mk 登録): `include/wab_xe10.h` / `drivers/wab_glue.h` (グルー契約) / `drivers/wab_glue_xe10.c` / `drivers/wab_cirrus.c` / `gfx/backend_cirrus.c`、probe 順 Cirrus → PEGC → 9801、`GFX=cirrus`。NP21/W の根拠は `wab/cirrus_vga.c`。**設計の前提が 1 つ崩れた**: Xe10 内蔵の CPU 窓は 32KB バンク窓のみで、2MB のリニア窓 (reg 02h) は最小 0x01000000 = OS32 の 16MB ページングの外。H3 は `bb_base=NULL` にしてあり、そのままでは CPU 描画 (gshell / gdi_test) が #PF する → **PM 判断: 案 (a) ページングを 32MB に広げてリニア窓を採る (H3b、投下中)**。DESIGN §8 の「BLT レジスタは MMIO」は Xe10 モードに MMIO 窓が無いため I/O 経由 (逸脱を記録)。ini のキーは `[NekoProject21]` の `USEGD5430=true` / `GD5430TYPE=91` (10 進 = 5Bh) — 票 §0 の記述を訂正。np21w-src の screenshot WAB 対応は 337e7ff (未 deploy) |

| 2026-09-06 | **H3b 結合** (`55049c5`): 32MB ページング (実 RAM 管理は 16MB のまま)、Cirrus の 2MB リニア窓 01000000h を CPU 描画面に。回帰 (WAB 無効のまま): kselftest 42/0、heap_test `load=0x500000`、ring3_guard kill、PEGC の hal_test / gdi_test 307200 / gshell + gui_demo、klibc 49/49。**hello32 (apps submodule) が PEGC で #PF** → 原因は 04:13 に焼いた旧 libos32gfx (H2b 以前、4 プレーン決め打ち) を静的リンクしているため。SDK ライブラリを変えたら `make external` が必要 (CLAUDE.md に追記) — `make external` → 配備で `hello32: done` (PEGC で正常)。submodule ポインタは不変 |
| 2026-09-06 | **G5 後半 (H3 Cirrus Xe10) 通過 (実機)**。手順: NP21/W 停止 → np21w-src `337e7ff` (`/api/screenshot?src=` / `wab_relay`) を `make deploy` → ini `[NekoProject21]` `USEGD5430=true` / `GD5430TYPE=91` ([D2] 承認済み) → 起動。結果: hal_test `backend cirrus 640x480 bpp=8 flags=0x6`、**`hw_fill_rect=0 / hw_blit=0`、`hw_ops +2`** (32×32 のエンジン経路)、gdi_test **`present_bytes=0 commits=1 hw_ops=9`** (CPU は 1 バイトも運ばない)、gshell 中は `/api/status` `wab_relay=1` / `/api/screenshot` が `X-Screen-Source: wab` の 640×480、gui_demo の窓 2 枚・FEP で「日本語abc」入力、ESC で CUI へ戻ると `wab_relay=0` / 400 ライン / テキスト正常。`gfxmode pegc` / `pc98` / `auto` → reset で 3 バックエンドを行き来 (pc98: hal_test planar、gdi_test 512000)、klibc 49/49。**直した 2 点**: (1) hal_test が init 前に HW 塗りを呼んで NOSYS (-10) を見ていた → HW_FILL が立つ機種では `gfx_init` → 塗り/転送 → `gfx_shutdown` で測り hw_ops の増分も見る。(2) **gshell 配下のアプリで漢字が描けない** (PEGC / Cirrus 共通、9801 は正常): `attach_gfx` が framebuffer だけ取り直して `gfx_packed` を立てず、shlib 内の `gfx_draw_font` がプレーン経路に落ちていた (ANK は libos32gui が自前で置くので気付かなかった) → 判定を `libos32gfx_attach()` (C 側) に集約し init / attach の両方から呼ぶ。`make external` 済み。**未検証のまま**: commit 前の描画が表示面に出ないことの目視 (単一スレッドの API では途中のスクリーンショットが撮れない。gdi_test の present_bytes=0 が間接証拠)。検証後 `USEGD5430=false` に戻し PEGC/9801 で再確認 (次行) |
| 2026-09-06 | **WAB を OFF に戻して再確認** (`USEGD5430=false`、`GD5430TYPE=91` は残置、NP21/W は taskkill → ini → os32-cycle deploy): `GFX=auto` で hal_test `backend pegc 640x480 flags=0x1`、gdi_test 307200、gshell + gui_demo が PEGC で描け `wab_relay=0` / screenshot は pc98 側、ESC で CUI 復帰。**G1〜G5 すべて通過**。残りは feat/gui → main のマージ (ユーザー判断) と、未検証項目 (ドラッグ/重なり = NP21/W にマウス注入 API が無い、shlib 版不一致の拒否、commit 前描画の目視) |
| 2026-09-06 | **レビュー #5 (4 点) 反映**: ① X4 の退避列 `pending_raw` の空きを `kbd_trygetrawkey` の**前**に見る (`b1496d9`、満杯時に 1 件黙って失う) ② デバイス窓 (Cirrus 01000000h / PEGC F00000h) は master に **supervisor + PCD** で張り、exec が `gfx_bb_phys_range()` の**クライアント面だけ**をアプリ PD で USER に昇格 ③ 昇格は新設 `paging_addrspace_map_user_keep()` で既存 PTE の PCD/PWT を保持、kselftest に `paging_map_user_keep_selftest` (43 項目) ④ `exec_launch_abort()` に起動失敗の巻き戻しを集約 (pgalloc 予約 / ガード / 子 heap / 親の heap・sbrk 上限・ガード / owner)。②③ = Opus (`707d856`)、④ = Opus (`cdacb47`)、マージ `d7f23a5`。**②の追補 (PM、`6e11e1a`)**: 隔離を入れると単独 CPL3 アプリ (gdi_test) が 0104B000h で #PF — exec の昇格は gfx_init より前なのに shutdown が窓を畳んで bb_base=NULL にし、再 init の `paging_map_phys` が共有 PTE を supervisor で上書きしていた → 窓は最初の init で一度だけ張り shutdown で畳まない。さらに gshell がアプリ終了後に呼ぶ gfx_init で glue->init がリレーを倒すのに `s_relay_on=1` のまま enter() が書かず、CTRL+STOP 後にデスクトップが黒画面に隠れた (既存バグ) → init で `s_relay_on=0`。**実機**: Cirrus: kselftest 43/0、hal_test hw_ops +2、gdi_test present_bytes=0、**`ring3_guard cirrus` → #PF addr=01000000h kill / `ring3_guard pegc` → F00000h kill** (契約 G4 の否定試験、新設)、hello32 OK、gshell → gui_demo (FEP 日本語) → ESC → gui_busy → CTRL+STOP でも `wab_relay=1` 維持 → ESC で CUI (`wab_relay=0`)。WAB OFF (PEGC): 同じ並びで 480 ライン維持 → CUI で 400、klibc 49/49、`make check` 全項目 OK (07_shell.md の欠落も修正)。**未検証**: ④ の失敗経路の実走 (pgalloc 枯渇の誘発には ini の ExMemory 縮小 [D2] が要る)、`gfx_stats` のカウンタは起動からの累計なので gdi_test の値は 1 回目だけが基準 |
| 2026-09-06 | **レビュー #6 (1 点) 反映** (`8df4c2e`): Ring3 AS 作成時に 9801 の主記憶バックバッファ (6A000h、128KB) を**常に** USER にし、その上でバックエンド固有の面を `_keep` で足す。アプリの gfx_init でアクセラレータの setup が失敗して 9801 へ落ちると `gfx_get_framebuffer()` が 6A000h を返すが、そこがフォント〜69FFFh と VRAM A0000h〜の間の穴だった。正の試験 `ring3_guard bb` (書けて生き残る = "BB??"+"SURV") を新設 — その過程で ring3_guard の生存側の終了が `int 0x80` eax=0 (= スロット 0 の gfx_init) で無限ループしていた既存不具合を sys_exit (スロット 84) に修正。**実機**: PEGC / Cirrus の両方で `ring3_guard bb` 生存、pegc / cirrus / guard / shlib の 4 ケースは kill、kselftest 43/0、gdi_test (Cirrus present_bytes=0 hw_ops=9)、hello32、gshell + gui_demo → CUI。WAB は OFF に戻した。**未検証**: Cirrus setup 失敗 → 9801 フォールバックの実走 (NP21/W では setup が失敗しない。写像の存在は `ring3_guard bb` で代替) |
| 2026-09-06 | **APPROVE → main へマージ** (`8e184e2`)。残課題の消化: (1) KAPI スロット定数 `os32_kapi_slots.h` (`KAPI_SLOT_*`、`d1de374`)、(2) GitHub Actions の静的ゲート `.github/workflows/check.yml` (`5bda7f3`、初回 green)、(3) feat/lgy98 は既に main の祖先で追従不要、(4) **NP21/W に `/api/mouse` を追加** (np21w-src `73f416e` + `1e593db`: `dx/dy` = バスマウス計数、**`ax/ay` = np2sysp getmpos の絶対座標上書き** (OS32 はシームレスモードなのでこちら)、`btn`/`hold`、`abs=off`)。それで G2 の未検証項目を実機で通した: **ドラッグの XOR 枠 → drop → 重なりの再描画 → 背面窓クリックで前面化 → × で閉じる → チェックボックス / OK / デスクトップ**。**見つけて直した 2 件**: (a) X4 がボタンエッジを先に見ると `prev_buttons` を進めて X3 の状態機械に届かず、ドラッグの離しが失われ × が前面化にしかならなかった → `wm_owns_edge()` で WM の領分なら据え置く (レビュー #4 ④ の一般形)。(b) シームレス座標を 639/399 に決め打ちしていて 480 ラインで下 80 ラインに届かなかった → 移動範囲に比例配分。/api/mouse のピクセル換算は `ax = px*65535/639`、`ay = py*65535/(H-1)` |
| 2026-09-09 | **PEGC 経路の回帰 2 件を修正 (v1.3 作業中)** (`113901b`)。`08964e3` で boot_splash が PC98 を強制した結果、実バックエンドの probe と init がアプリ (CPL=3) の初回 `gfx_init` まで繰り延べられ、(a) PEGC probe が読む BIOS ワークエリア 0x045C はアプリ PD に写像が無く `#PF` でアプリが死ぬ、(b) バックバッファ予約 (`sys_reserve_top`) は物理末尾側なので走行中アプリのスタック/ヒープに阻まれ必ず失敗する。→ `gfx_prepare_backend()` を boot_splash 直後に 1 回だけ呼び、選択→init→shutdown をカーネル文脈で済ませる (splash は PC98 強制のまま)。`bios_flag()` は master AS 以外では読まない。**実測**: 起動直後に `s_probed=1/s_probe_ok=1`、`sys_top_reserved=307200`、`sys_frozen_exec` 0xEBE000→0xE73000、hal_test `backend pegc (packed 8bpp) 640x480 bpp=8`、gdi_test は `#PF` せず完走、回帰 6/6、`GFX=pc98` の 9801 経路も従来どおり。**基準値の訂正**: 上の 2026-09-06 行にある `gdi_test present_bytes=307200 commits=1` は、その後の `c236267` (OP_INVALIDATE で Paint を即配布) と `1a5636f` (fill_solid の行単位化) で present のまとめ方が変わったため**もはや比較対象にならない**。現在はクリーンブート 1 回で PEGC 691200 / 9801 512000、どちらも commits=119。planar 4bpp が 0.5 B/px、packed 8bpp が 1 B/px という計上差の範囲で、回帰の証拠は見つかっていない (断定はしていない)。**hal_test の読み方**: `gfx_init()` を呼ばず起動時に選ばれた値を報告するだけなので、`gfxmode` の後はリセットするまで変わらない |

### 記録しておく v1 の限界 (W1 の申し送り)

- **commit 前の描画が表示面に出うる**: ソフトウェアバックエンド (9801) は単一バックバッファを WM とアプリで共有するので、WM の X3 present (クローム / デスクトップ) がアプリの未 commit 描画を巻き込む。契約 G4 は「バックエンドの責任」としており、v1 では許容。PEGC / Cirrus (H2 / H3) でサーフェスを分けられれば解消。
- **K3 の副作用** (2026-09-06): 子プロセスの空間が 1MB 減る (8MB 機で本体上限 ≈1.2MB)。shlib ロード時に帯域 1MB を pgalloc から永久に取る。`EXEC_DYN_RESERVE` の穴 (1MB) を V86 バッキング 640KB と per-app data で分けるので、ライブラリ .data/.bss が 300KB を超えると V86 と競合。`--cpl0` バイナリは master PD なので .data を共有 (仕様)。gshell 常駐中は hotdeploy のポーリングが止まる (K2)。
- **cooked キュー**: WM は raw リングだけ読むので、GUI 中に cooked (`kbd_buf`) が溢れて `kbd_dropped` が偽 OVERFLOW になる。暫定は gshell の `drain_cooked_queue`、本筋は K2 で「GUI 中は cooked に積まない」。
- W1 の解釈 (契約に受け渡し場所の記述が無かったもの): `OP_INIT` の proto_version は arg、単一ハンドル op は arg≠0 ならハンドル / 0 なら `GuiReqWindow`、`OP_STATS` は応答ブロック先頭に `GFX_Stats` 生、P2 の取り込み tick はスロット +11280 に 64×4B (`u16 serial` + `u16 tick`)、`Configure` の rect は画面絶対座標 / `Paint` はクライアントローカル。タイマは契約 U5 のとおり (レビュー #3 ⑤で修正、反復固定は撤回)。C2 はこれに合わせてある。`Key` は down=false (break) も届く (契約 U2a の「当面届かない」より前進、クライアントは編集に使わない)。gshell 配下のアプリは `libos32gfx_init()` を呼ばず `client::init()` の attach を使う (gfx_init が VRAM とパレットを壊す)。
