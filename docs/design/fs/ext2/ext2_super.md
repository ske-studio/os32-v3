# sys/fs/ext2/ext2_super.c — ext2 スーパーブロックと I/O

> 状態: **現行 (2026-10-31)**

ext2 の媒体アクセスとマウント状態を管理する。
メタデータの I/O 失敗時にはファイルシステムをエラー状態にする。

ブロック／セクタの生 I/O は [ext2_super_block_io](ext2_super_block_io.md)
を include し、範囲検査と Device 発行を集約する。
`ext2_priv.h` が範囲読み書きの内部宣言と最大 32 ブロック定数を持つ。
このファイルの追加常駐領域・ヒープは 0B、既存共有 3×1KiB バッファは増やさない。
[共通失効](ext2_priv.md) を fs_error 入口、mount の早期失敗より前、
unmount の sync 後かつ ctx 解放前に呼ぶ。sync の失敗でも失効する。

## 落とし穴

- **メタデータ I/O の失敗**: 読めなかったものを不存在や空きと扱うと、不正な上書きや交差リンクにつながる。
  エラーを伝播し、メタデータ障害後は `ext2_check_writable` で書込みを拒否する。
  `ext2_fs_error` のエラー記録用バッファは操作中の共有 scratch と分ける。
  実際の open を通す失敗経路と媒体整合の回帰は `test_b8_open.py (旧版 6455da9bbf55)`。

## 区画の探索とマウントの範囲

出所: TASK_HDD_INSTALL 段 1 (§1-v3 N3〜N6) の契約を今のソースと照らして要約 (2026-11-18)。

- `ext2_find_partition(drive, &start, &len)` は LBA 1 の区画表から OS32 の区画 (`pc98pt_find_os32`) を探して返す。
  LBA を仮定するフォールバックは無い。LBA 1 が読めない・ドライブが無い → `EXT2_ERR_IO`、幾何が取れない・項目が無い・
  範囲が壊れている (ディスクの外を含む) → `EXT2_ERR_NOPART` (-13)。
- 区画表の CHS は **BIOS 幾何** (`bootinfo_part_geom`、DA 80h/81h の AH=84h の値) で LBA にする。BIOS 幾何が無ければ
  IDENTIFY の既定に落ちる。IDENTIFY 8/17 のディスクで LBA 272 に化ける表が、BIOS 幾何 16/63 なら 2016 を指す。
  終わりは IDENTIFY の総数の内側でなければならない。
- マウントは `[EXT2] hdN: partition LBA s +n (CHS geometry from BIOS|IDENTIFY)` を出す。旧配置の OS32 項目を見つけたら
  `migrate it (host: make nhd-migrate-pt) or reinstall` を出してマウントしない。
- 区画が短すぎる・FS のブロック数が区画より大きい (`fs (N blocks) exceeds partition`) ならマウントしない。
- `part_len` を文脈に持ち、ブロック I/O (`ext2_super_block_io.inc`) は区画の外 (×2 の桁あふれを含む) をデバイスに出さない。
- 試験: `make check-hdd-stage1-host` のケース `find_bios`・`find_fail`・`mount_bounds`。RAM ディスクの ext2 試験は
  LBA 1 に本物の区画表を置く足場 `hdd_pt_fake.h (旧版 28f26153b2da)` に乗る。

## 小さな書き込みの I/O を減らす 2 つの仕掛け

出所: TASK_S6P の契約を今のソースと照らして要約 (2026-11-18)。どちらも **write-through の契約を変えない** (書き込みが戻った時点でディスクに反映されている。ext2 にダーティなブロックキャッシュは無い)。

### `ext2_sync` は SB・GD が動いていなければ書かない (`Ext2Ctx.meta_dirty`)

- 空きブロック数・空き inode 数・`used_dirs` を動かす所 (ブロックと inode の確保・解放、mkdir・rmdir) が `ext2_meta_touch` を呼ぶ。`ext2_mount` は読み込み直後に 0 に戻す。
- `ext2_sync` は `meta_dirty` が 0 なら何も書かない。立っていれば `ext2_write_super_raw`・`ext2_write_gd_raw` を書いて 0 に戻す。確保を伴う書き込みでは必ず書く。
- fs_error の後の `ext2_sync` は書かず、`meta_dirty` が立っていれば IO を返す。
- 明示の `sync` も同じ規則。ext2 の外から生セクタを書いた区画に、メモリ上の古い写しを上書きしない。

### 解決済み経路の記憶 (`Ext2Ctx.memo[EXT2_PATH_MEMO_N]`・`ns_gen`)

- VFS の FD 層は read・write のたびにパス文字列を渡すので、`ext2_vfs.c` の `ext2_resolve_path` がパス → inode 番号を 4 件まで覚える (`ext2_path_memo_get`・`ext2_path_memo_put`)。
- 名前空間を動かす所 (`ext2_dir.c` のエントリの追加・削除、rename の段 0) が `ext2_ns_touch` で世代を進め、記憶は全部まとめて無効になる。パスが別の inode を指すようになる操作 (create・unlink・rename・mkdir・rmdir) は必ずこれを通る。
- `OS32_MAX_PATH` に収まらないパスは覚えない (切り詰めで別のパスに化けて当たるのを避ける)。世代が一周したら記憶を全部捨てる。記憶は `Ext2Ctx` の中なので umount で消える。
- `Ext2Ctx` が大きくなったので、`ext2_format` の一時コンテキストは `static` (カーネルスタックに積まない)。

### 検査

`test_ext2_write_io.py (旧版 dd0b1062633f)` (本体 `ext2_write_io_host.c (旧版 a5c1ea237934)`。実物の ext2 と microtar を組み、512 byte セクタの I/O 回数を数える)。
数で表明するのは「確保を伴う書き込みは伴わない書き込みより I/O が多い (sync が出る)」・後方 lseek が 0 セクタ・`tar c` の `sys_write` 回数で、各書き込みのセクタ数は表示だけ。
ほかに、マウントし直した別インスタンスで中身・サイズ・空き数が一致すること (write-through)、記憶が unlink → 再作成・rename・rmdir → 同名ファイル・2 インスタンス同時に追従すること、毎操作のあとメタデータ (空き数・`used_dirs`・GD) がディスクと一致することを見る。
