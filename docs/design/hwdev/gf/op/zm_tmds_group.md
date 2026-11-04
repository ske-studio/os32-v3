# hwdev/gf/op/zm_tmds_group.c — TMDS literal group script instruction

> 状態: **現行 (2026-11-04)** — GF_FEAT4_DESIGN「TMDS の script の実行の口」C2-links-init。

ソース: `hwdev/gf/op/zm_tmds_group.c`。opcode 0x50、3+2*count bytes:
selector/count は +1/+2、各 address/value pair は +3 以降。

MIT nouveau c03c1a1 `nvkm/subdev/bios/init.c:1121-1143` に従い、各 pair を
DATA=value → CONTROL=address の順で書く。selector の解決は
`tmds_reg.c` の共通 helper (`init.c:539-572`)。論理 head stride は足さない。
skip では I/O なし、未知 selector は -1、bus failure 以降の pair は書かない。
ROM 範囲は共通 decoder、絶対期限と失敗時復元は runner が担当する。
実装ファイルに手本の著作権・MIT 許諾を保持する。
