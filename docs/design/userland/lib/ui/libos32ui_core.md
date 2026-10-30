# userland/lib/ui/libos32ui_core.c — UI 入力アダプタ

> 状態: **現行 (2026-10-31)**

microui の入力をキーボードとポインタから取り込む。

## 落とし穴

- **入力の二重消費**: `mui_pump_input` は自分で `kbd_trygetchar` を呼ぶため、同じフレームでアプリが再度読むとキーが失われる。
  アプリもキーを使う場合は一度だけ読み、`mui_pump_input_ch` にその値を渡す (`userland/lib/ui/libos32ui_core.c:192`, `:220`)。
  呼出し順を変更するときはアプリのショートカットと UI 入力の両方を確認する。
