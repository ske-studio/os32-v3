# kdebug init / loader (S1a)

票: [TASK_DBGREF](../../docs/tasks/v3/TASK_DBGREF.md) §3-4・§4・S1a。
`make check-kdebug-init-host` は実物 init を ILP32 で呼び、候補表の完全一致と
import 呼出しゼロを検査する。`--mutate` は kprintf 呼出し・候補未設定を RED にする。

実物 module.c / module_image.c と実際に生成した部品を組み合わせ、VFS/ledger
だけを偽物にして実行する。OFF、ON、open/stat/header read/owner/alloc/body read
失敗、img_valid、header/pair/ABI/CRC/reloc/import/export の拒否、init 失敗を検査。
全拒否で公開表ゼロ、close、ページ全返却、owner retire、診断一行を確認する。
正常では prefix を破壊しても body の関数だけが公開され、body ページだけが残る。

ゲストの実 ledger・boot.log と起動継続の判定は PM の G1 受入で行う。
