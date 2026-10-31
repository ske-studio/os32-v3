# drivers/input/kbd.c — PC-98 キーボード受信

> 状態: **現行 (2026-10-31)**

8251 の IRQ1 受信を分類し、キー状態と入力配送を管理する。

## 落とし穴

- **8251 の状態と反射**: コマンド 0x16 は RxE と RTY# HIGH を保つ。0x14 は RTY# を LOW にする (`drivers/input/kbd.c:314`)。
  IRQ では先に状態を読み、空ならデータを読まない。PE/FE は捨て、OE のみなら受信バイトを使ってエラーを解除する (`drivers/input/kbd.c:285`)。
  実データのない IRQ を V86 に反射すると偽のキーになるため、分類結果に従って反射する。
  `make check-kbd-status-host` と起動時のキーボード自己試験が守る。信号線の効果は実機でも確認する。

## 設計の補足

- `drivers/input/kbd.h` は `#include "../../include/kbd.h"` で共通定義を読む側へ反転（"kbd.h"では自己参照になる）。I/O、8251定義、リング容量・タイムアウト、init/IRQ・kbd_shift_state宣言はdriver内。guard名を別々にする。

- 状態の持ち主: cooked/rawリング・STOP集約・押下/修飾・GUIモードはkbd driver。

その先で溢れた打鍵は**カーネルが捨てて数える** (`kbd.c` の満杯分岐に `kbd_dropped` を足す。KAPI `kbd_dropped_count()` で読む)。

raw 満杯時の CTRL+STOP make は末尾を置換し、捨てた1件を数える。

### U2a. キー入力と文字入力の分離 — 確定 2026-09-04 (Key.code の体系)

事実: カーネルのキー待ち行列は `(scancode << 8) | ascii` の 16bit (drivers/input/kbd.c)。scancode は
PC-98 の 7bit コード (`drivers/input/kbd.h` の `KEY_*`: F1〜F10 = 62h〜6Bh、VF1〜5、ROLL UP/DOWN、
INS/DEL、HELP、XFER/NFER、COPY、STOP、テンキー) で、ascii は変換後の文字 (無ければ 0)。
修飾は `kbd_get_modifiers` の 5bit (SHIFT 01h / CAPS 02h / KANA 04h / GRPH 08h / CTRL 10h)。
