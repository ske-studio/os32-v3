# drivers/input/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D2_DESIGN (D1 で移した)
ソース: `drivers/input/README`

### mouse

- 座標/範囲/前回poll/カーソルはmouse driver。

- 状態の持ち主: cooked/rawリング・STOP集約・押下/修飾・GUIモードはkbd driver、座標/範囲/前回poll/カーソルはmouse driver、注入リングとownerは既存kernel。

### kbd.h

PC-98 の 7bit コード (`drivers/input/kbd.h` の `KEY_*`: F1〜F10 = 62h〜6Bh、VF1〜5、ROLL UP/DOWN、
INS/DEL、HELP、XFER/NFER、COPY、STOP、テンキー)。
修飾は `kbd_get_modifiers` の 5bit (SHIFT 01h / CAPS 02h / KANA 04h / GRPH 08h / CTRL 10h)。
