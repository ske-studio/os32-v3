# kernel/gui.c — 常駐 WM の登録と呼出し

> 状態: **現行 (2026-10-31)**

GUI 操作を登録された WM ハンドラへ転送する。
所有者終了の通知と IME 描画関数の登録も仲介する。

## 落とし穴

- アプリが後で CPL=0 から呼ばれる関数表を登録できると、終了後の解放済みコードへ
  飛ぶこともある。`gui_register` と `gui_ime_set_render` は owner 1 かつ
  ユーザーからの呼出しでないことを要求する (`kernel/gui.c:103`, `kernel/gui.c:155`)。
  関数ポインタの番地だけで呼出し元の信頼性を判断しない。
  `make check-ring3-guard-host` と `kdebug/kselftest/ime_render_gate.inc` が検査する。
