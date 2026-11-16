# TASK_HSYNC_MANIFEST — hsync の往復を減らす

> 状態: **実装中 (2026-11-16)** — 設計は確定、A〜H は統合の枝。詳細設計は [HSYNC_MANIFEST_DESIGN](../../decisions/network/HSYNC_MANIFEST_DESIGN.md)。

## 0. ユーザー決定 (2026-11-16)
| 項目 | 決定 |
|---|---|
| 方針 | 「往復を減らす」: ホストに木全体の一覧 (パス・大きさ・日時・CRC) を返させ、ゲストは自分の CRC と比べて違うファイルだけ写す。LAN 一括転送 (pipelining) には触れない |
| 同じ日時 | 「今どおり飛ばす」: 大きさと日時が同じなら読まずに飛ばし、日時が違う・分からないときと --verify のときだけ CRC で比べる |
| 背景 | x86 の NP21/W (シリアル版 SFS) の計測: ふだんの量 (260 ファイル・16MB) で差分なし 34 秒、全部の日時が変わると 67 分。時間のほぼ 100% が 1 本ずつ待つ往復 |

## 1. 段の状態
| 段 (設計 §7) | 状態 |
|---|---|
| A+E (hsync_inventory.inc と試験)・B+F (host_tree_manifest.py と試験) | 済 (2026-11-16、wt/hsm-base に統合) |
| C (serialfs_host.py)・D (hsync.c) | 済 (2026-11-16、wt/hsm-base に統合) |
| G (三つの経路の統合試験)・H (NET_HOSTSVC_DESIGN の追記) | 済 (2026-11-16)。60 ファイルで差分なしの往復が 362/2/140〜260 → 6/0/3〜4 (STAT/LIST/READ) |
| NP21/W での実測 (計測の 34 秒・67 分との比較) | 未 |
