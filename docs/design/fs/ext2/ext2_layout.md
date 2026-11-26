# sys/fs/ext2/ext2_layout.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_HDD_INSTALL 段 1 (§1-v3 N7) の契約を今のソースと照らして要約
ソース: `sys/fs/ext2/ext2_layout.c`・`sys/fs/ext2/ext2_layout.h` (`ext2_layout_plan`・`ext2_layout_group_overhead`・`ext2_layout_is_sparse`・`EXT2L_MAX_SECTORS`)

ext2 を一括で作るときの配置 (ブロック数・グループ数・inode 数・GDT と inode 表の大きさ) を決める純粋関数。
`sys/fs/ext2/ext2_fmt.c` はここで決まった値だけを書き、インストーラの容量の見積もり (`userland/system/inst_disk.c`
`inst_check_space`) も同じ値を使う。

## 前提

1KiB ブロック、1 グループ最大 `EXT2_BLOCKS_PER_GROUP_MAX` (8192) ブロック、inode 128B、4 ブロックに 1 inode (最少 16)、
最少 64 ブロック。最初のデータブロックは 1。sparse superblock: グループ 0・1 と 3・5・7 の累乗のグループだけが
SB と GDT の写しを持つ。

## 最終グループの必要量

`ext2_layout_group_overhead` = bitmap 2 + inode 表 (+ sparse なら SB 1 + GDT)。最終グループがグループ 0 なら、さらに
ルートのデータ 1 ブロックを足す。lost+found は formatter が作らないので数えない。

## 固定点 (`ext2_layout_plan(sectors, max_groups, out)`)

1. ブロック数 = sectors / 2 から全部の値を計算する。
2. 最終グループの残りブロック数 ≥ 必要量なら確定。
3. 足りなければ最終グループを落とし (ブロック数 = 1 + (グループ数 - 1) × 8192)、グループ数・inode 数が変わるので
   1 から計算し直す (`EXT2L_MAX_ITER` 回まで)。
4. 64 ブロック未満になる・グループ 1 つでも足りない → `EXT2L_ERR_SMALL`。グループ数が `max_groups` を超える →
   `EXT2L_ERR_GROUPS`。

落とした大きさは素朴な配置では本当に足りず、落とさなかった大きさは足りる。例: 16,652 セクタは最終グループの残りが
133 < 必要 135 なので 1 グループ 8,193 ブロックになる。`EXT2L_MAX_SECTORS(g)` は g グループで作れる最大のセクタ数で、
`ext2_format` の頭打ちに使う。

## 確かめ

`make check-hdd-stage1-host` (`test_hdd_stage1.py (旧版 6201b26bccf0)`) のケース `layout` (32/33 グループの境界・sparse の境目を
1 ブロックずつ掃く・128〜524,288 セクタの全長) と、作った像の `e2fsck -fn` (16,652 / 20,160 / 36,000 / 62,496 セクタ)。
