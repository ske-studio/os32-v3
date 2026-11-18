# platform/pc98/display.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (表示不具合の修正) の契約を今のソースと照らして要約
ソース: `platform/pc98/display.c` (呼ぶ側は `platform/pc98/legacy/v86.c` の `v86_cui_display_restore`・`platform/pc98/legacy/v86_io.c`・`kdebug/v86_gcap.c`)

V86 のゲスト (DOS のプログラム) が表示を書き換えた後、OS32 の CUI へ戻す。

## CUI への復帰 (`display_restore_98`)

V86 の通常の出口は `v86_cui_display_restore` → `display_restore_98` を通る。順序と結果:

1. グラフィック GDC を STOP し、**止めたままにする** (CUI の復帰でグラフィックを START しない)。
2. 起動時に保存した BIOS の表示フラグ (`v86_bios_real_lowmem`、ゲストの BDA ではない) を見て、EGC があれば EGC をリセットして GRCG モードへ、GRCG があれば GRCG を OFF、拡張グラフィック (PEGC) があれば標準グラフィック・400 行 VRAM へ戻す。
3. 16 色モード・高解像度、400 行の GDC 設定、スクロールを 400 行・分割なしに戻す。
4. `display_text_start`: `gfx_current_height` を 400、flip を無効にし、グラフィック GDC の STOP・表示出力の ON・テキスト GDC の START。
5. 表示ページ・描画ページを両方 0 にする。
6. パレットを初期化し (16 色に戻した後でないとアナログパレットにならない)、テキストのカーソル形状を OS32 の形に戻す。

## 画面の取り込み (gcap) の後始末

`kdebug/v86_gcap.c` の `gcap_cui_rebuild` は、取り込みの経路が PRAM とタイミングを既に戻した後なので `display_text_start` だけを呼び (全体の復帰を二重に行わない)、保存したテキスト画面とカーソルを戻す。取り込みを使わない出口は上の全体の復帰を通る。

## 検査

`tools/tests/test_display_cleanup.py` が V86 の出口の実関数を抜き出して動かし、通常の CUI 復帰・取り込みを迂回する場合・取り込みから作り直す場合の 3 つで、各ポート出力の欠落・ページの取り違え・グラフィックの START を実行時に検出する。
