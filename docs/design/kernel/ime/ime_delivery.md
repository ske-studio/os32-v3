# IME の候補確定と透過キーの配送

> 状態: **現行 (2026-10-31)** — 設計: gpt-6-astra、確定設計 IMETAB

ソース: `kernel/ime/ime_delivery.inc`、接続: `kernel/ime/ime.c`。

単一利用者・単一タスク・協調切替の共有 IME。候補確定が透過を返すときも、
確定した UTF-8 全体を元のキーより先に届ける。候補選択・確定条件・学習時点は変えない。

## 所有状態と順序

確定文字は既存の `g_ime.commit_buf/pos/len` が所有する。CUI の配送層は
`pending_keydata` を1件だけ持ち、空を -1 とする。scancode を含む元の整数を
保存し、UTF-8 バッファへ混ぜない。owner 別の状態や動的確保は持たない。

6つの CUI 読み出し口は4つの本体を通して、同じ補助関数を使う。
各読出しは未読確定バイト、保留キー、次の cooked キーの順で進む。
`ime_process_key()` が透過 (-1) を返して未読確定列を作った場合だけ元キーを保留し、
まず確定列の先頭を返す。保留を返した時点で -1 に戻す。
排出中は cooked キーを取得せず、キー処理・候補学習を再実行しないため、1件で足りる。

`getkey/trygetkey` は元の scancode を保持し、`getchar/trygetchar` は下位 ASCII を返す。
ASCII=0 は blocking で飛ばし、poll はそのキーだけを消費して -1 を返す。
混用しても同じ配送状態を進める。con_sink の bypass と既存の CUI reader owner 記録を維持する。

## 取消と park/resume

`ime_init()` は保留を空にする。`ime_cancel_input()` は未読確定列と保留キーを
一緒に捨てる。STOP、待ち中断、読者 owner の終了、GUI/CUI 描画先の変更は
既存の取消接続を通る。配送済みのバイトは巻き戻さない。
読んでいない子の終了、モード変更、空 poll を新しい取消境界にしない。

park/resume は入力先の終了ではない。共有確定列と保留キーをそのまま保持し、
復帰後は残りの候補、元キーを各1回配送する。owner 別 snapshot を追加しない。
候補コピー後の学習 API が通常復帰すれば、学習の成否によらず配送を続ける。

## GUI と検証

GUI は元キーを WM の現在の打鍵が保持する。`ime_feed_key(-1)` は確定バイトだけを
返し、CUI の保留キーを返さない。WM は Pass の場合も次の入力前に確定列を回収する。
配送先・既存 flush の契約は [fep](../../userland/gshell/src/fep.md) を参照。
KAPI/ABI v79 と公開状態構造体は変更しない。

`tools/tests/x11_shared_host_imetab.inc` が6入口×2候補状態、Tab/F1/制御コード、
混用、次の `a → Enter`、学習1回、取消、GUI KAPI 境界を検証する。
`x11_park_host.c` は製品 `exec_park/exec_resume` を通す。
GUI の回収・Text→Key・次打鍵・入力ダイアログの順序は
`userland/gshell/host/kbdnav_tests_imetab.rs` が製品 `fep.rs/input.rs` で検証する。
