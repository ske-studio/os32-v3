# kernel/module/module.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: VER_INFO_DESIGN・HWDEV_CORE_API・HWDEV_DESIGN (D1 で移した)
ソース: `kernel/module/module.c`

kdebug_state_name()
state<0 / 配列上限以上→UNKNOWN
13〜16 は RECORD_FULL/BAD_DEPENDENCY/BUSY/QUARANTINED になる。

既存 19 口の順序は維持。

- kdebug の既存 import は未使用になっても削除・並替え禁止、必要分だけ末尾追加。内部 ops 版 / 契約 CRC も更新。

KDEBUG=1 のみロード、PAIR は kernel 像 CRC。
任意の装置部品を自動発見・交換する仕組みはない

いまは版 4・20 口 (`tools/module_schema.py`、08_build §8-2)。
