# hwdev/gf/op/tmds_reg.c — TMDS register script instruction

> 状態: **現行 (2026-11-04)** — GF_FEAT4_DESIGN「TMDS の script の実行の口」C2-links-init。

ソース: `hwdev/gf/op/tmds_reg.c`。opcode 0x4f、5 bytes:
selector/address/mask/value は +1/+2/+3/+4。

MIT nouveau c03c1a1 `nvkm/subdev/bios/init.c:539-572,1093-1119` に従い、
selector 0..3 は 6808b0/6808b8/6828b0/6828b8。0x80/0x81 は raw DCB
OR mask を offset 表に添字として渡す。0x81 は offset を 8 と XOR。
論理 head の stride は重ねない。未知 selector は -1。

skip では I/O なし。それ以外は CONTROL=address|10000 → DATA read →
DATA=(old&mask)|value → CONTROL=address。各 bus failure で直ちに停止。
script 全体の期限・失敗時の一度の復元は runner が担当する。
実装ファイルに手本の著作権・MIT 許諾を保持する。
