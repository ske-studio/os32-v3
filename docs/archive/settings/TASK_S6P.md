# 票 S6-P — ext2 の小さな書き込みが極端に遅い (診断と処置)

> 発行: PM (2026-09-14) / 状態: **受入完了 (2026-09-14)**

対象の観測: [`TASK_S6.md`](TASK_S6.md) 「PM 受入記録」 (2026-09-14)
実測の記録: [`tools/tests/s6p_tdd.md`](../../../tools/tests/s6p_tdd.md)
基点: `4fd5fb4` (feat/gui)

`tar c /tmp/e8.tar /etc/system.cfg` (15B のファイル 1 本 → 書庫 2048B) が
ext2 (hd0) で 15 秒を超え、HostDrv では 0.3 秒。

## 1. 原因は 2 段重ね

**掛け算の形になっている。**

| 段 | 何 | 倍率 |
|---|---|---|
| A (呼び出し側) | `lib/microtar/microtar.c` の `write_null_bytes()` が **1 バイトずつ** `sys_write` する。2KB の書庫に **1523 回** | 512B 単位で書けば 4〜6 回 → **約 300 倍** |
| B (ext2 側) | `sys_write` 1 回の固定費が **26 セクタ** (サイズに関係なく) | 妥当な下限は 10 セクタ → **2.6 倍** |

1523 × 26 = **39602 セクタ**。NP21/W の PIO は実測 ≈0.38ms/セクタなので約 15 秒 —
観測とぴったり合う。`/api/status` の EIP が `ide_*_sector_chs` に集中していたのも、
「遅い処理」ではなく「I/O の回数が異常」の像。

### A の現物

`lib/microtar/microtar.c:111`

```c
static int write_null_bytes(mtar_t *tar, int n) {
  int i, err;
  char nul = '\0';
  for (i = 0; i < n; i++) {
    err = twrite(tar, &nul, 1);      /* ← 1 バイトずつ tar->write を呼ぶ */
    if (err) return err;
  }
  return MTAR_ESUCCESS;
}
```

呼び出し元は 2 か所:
- `mtar_write_data()` — データのあとを 512B 境界まで埋める (15B のファイルなら **497 回**)
- `mtar_finalize()` — 終端の 2 レコード = **1024 回**

`userland/cmds/tar.c` の `mt_write` → `io_write` → `g_api->sys_write` なので、
この 1521 回がそのまま KAPI 呼び出しになる。
HostDrv で速いのは 1 回の書き込みが安いから (ホスト側の 1 write システムコール) で、
回数の異常は同じ。

### B の内訳 (`sys_write` 1 回 = 26 セクタ = 13 ブロック I/O)

`fs/vfs_fd.c` の `vfs_write_fd()` は FD ではなく **パス文字列**を FS に渡す
(`ops->write_stream(fs_ctx, f->path, buf, size, f->offset)`)。だから毎回ここから始まる:

| 経路 | ブロック読 | ブロック書 |
|---|---|---|
| `ext2_vfs_write_stream` → `ext2_resolve_path` → `ext2_lookup("/tmp/e8.tar")`<br>`find_entry(root,"tmp")` = inode 1 + dir ブロック 1、`find_entry(tmp,"e8.tar")` = 同 | 4 | 0 |
| `ext2_write_stream` → `ext2_read_inode` | 1 | 0 |
| 部分ブロックの read-modify-write | 1 | 1 |
| `ext2_write_inode` (read-modify-write) | 1 | 1 |
| `ext2_sync` → `ext2_write_super_raw` (ブロック 1) | 1 | 1 |
| `ext2_sync` → `ext2_write_gd_raw` (ブロック 2) | 1 | 1 |
| **合計** | **9** | **4** |

1 ext2 ブロック (1KB) = 512B セクタ 2 本 (`fs/ext2_super.c` の
`ext2_read_block` / `ext2_write_block` が 2 回に割る) → 13 × 2 = 26 セクタ。

後方 `lseek` は無実 — `fs/vfs_fd.c` の `vfs_seek()` は `VfsFile.offset` を
書き換えるだけで FS を一切触らない (0 セクタ、試験 P4 で表明)。
create 経路には backward seek 自体が出ない (`mtar_seek(last_header)` は read 側)。

## 2. B は直した (この票の範囲)

`fs/ext2_ctx.h` / `ext2_super.c` / `ext2_inode.c` / `ext2_dir.c` / `ext2_vfs.c` /
`ext2_priv.h`。**write-through の契約は変えていない** (書き込みが戻った時点で
ディスクに反映されている)。

1. **`ext2_sync()` は SB / GD が動いていなければ何もしない** (`Ext2Ctx.meta_dirty`)。
   空き数・`used_dirs` を動かす 6 か所が `ext2_meta_touch()` を呼ぶ。
   書き戻すものが無いときに出していた同じ中身の read-modify-write
   4 ブロック (8 セクタ) が消える。
2. **解決済み経路の記憶** (`Ext2Ctx.memo[4]` + `ns_gen`)。
   `ext2_add_entry` / `ext2_delete_entry` が成功したら世代が進み、記憶は全部無効。
   パスが別の inode を指すようになる操作 (create / unlink / rename / mkdir / rmdir)
   は必ずそのどちらかを通る。`ext2_lookup` のディレクトリ辿り直し 4 ブロック
   (8 セクタ) が消える。
3. 付随: `sizeof(Ext2Ctx)` が 732 → 1788 バイトになるので、`fs/ext2_fmt.c` が
   スタックに積んでいた一時コンテキストを `static` にした
   (`ext2_format()` のフレーム 824 → 88 バイト — 変更前より小さい)。
   マウント中の `Ext2Ctx` は今までどおり kmalloc。

結果 (`python3 -B tools/tests/test_ext2_write_io.py`、10/10 PASS):

| | 前 | 後 |
|---|---|---|
| `sys_write(1B)` / `sys_write(512B)` (既存ブロック) | 26 セクタ | **10 セクタ** |
| `sys_write(512B)` (新ブロック確保あり) | 28 セクタ | 28 セクタ (変わらず — 確保があれば sync は要る) |
| `sys_read(512B)` | 12 セクタ | **4 セクタ** |
| `tar c` 全体 (microtar のまま 1523 回) | 39602 セクタ | **15258 セクタ** |
| `tar c` 全体 (512B 単位 4 回で書いた場合) | 108 セクタ | **68 セクタ** |

ext2 だけで **2.6 倍**。15 秒 → 約 5.8 秒の見込み。

## 3. A も直した (2 周目、PM が worktree に該当ファイルをコピーしたあと)

`lib/microtar/microtar.c` の `write_null_bytes()` を 512 バイト単位にした。
上流の外部仕様 (書く中身・`tar->pos` の勘定・戻り値) は一切変えていない。
`lib/microtar/README.OS32` の改変一覧に **4 点目**として記録済み。

```c
static int write_null_bytes(mtar_t *tar, int n) {
  static const char nul[512];       /* const = .rodata、全部 0 */
  int err;
  while (n > 0) {
    int chunk = (n > (int)sizeof(nul)) ? (int)sizeof(nul) : n;
    err = twrite(tar, nul, (unsigned)chunk);
    if (err) {
      return err;
    }
    n -= chunk;
  }
  return MTAR_ESUCCESS;
}
```

- `nul[]` は `static` — 外部プログラムのスタックは細いので自動変数にしない。
- C89 ([C1])。`i386-elf-gcc -std=gnu89 -Wall -Wextra -Werror
  -Wdeclaration-after-statement` で通ることを `test_tar_cmd.py` の
  `build_target` が毎回見る。

### 実測 (`tools/tests/ext2_write_io_host.c` の P5、実物の microtar を #include)

`tar c /tmp/e8.tar /etc/system.cfg`:

| | RED (両方とも直す前) | 段 B だけ (ext2) | 段 A + B |
|---|---|---|---|
| `sys_write` 回数 | 1523 | 1523 | **5** |
| いちばん小さい書き込み | 1B | 1B | **15B** |
| セクタ I/O 合計 | 39602 | 15258 | **78** |
| 0.38ms/セクタ での見込み | 15.0 秒 | 5.8 秒 | **0.03 秒** |

5 回の内訳は `512 / 15 / 497 / 512 / 512` (ヘッダ / 本体 / padding / 終端 2 本)。
書庫は `mtar_read_header()` で読み直して名前・サイズ・型・本体・終端のゼロまで
確認している。P5 は `writes <= 8` / `minsz >= 15` を表明するので、1 バイト書きに
戻ればこの試験が落ちる。

### 併せて直したもの

`userland/cmds/tar.c` の `collect_cb()` が `(void)ctx;` のあとに `int len;` を
置いていて [C1] (宣言はブロック先頭) に反していた。実機のビルドフラグには
`-Wdeclaration-after-statement` が無いので通っていたが、`test_tar_cmd.py` に
足したクロスコンパイル段で落ちたので直した。

### ついでに見ておく値 (未着手、PM 判断)

`userland/cmds/tar.c` の `TAR_CHUNK` (本体データの読み書き単位、現在 8192B)。
512B の倍数なのでこのままで問題ないが、大きくすると展開側も同じ倍率で効く。

## 4. 手を出さなかった案 (規模の見積り付き)

| 案 | 効き | 規模 | 見送りの理由 |
|---|---|---|---|
| `ext2_write_stream` の `ext2_write_inode` が inode テーブルブロックを読み直すのを省く (直前の `ext2_read_inode` が同じブロックを `ext2_g_blk` に置いている) | 10 → 8 セクタ (-20%) | 小 (`ext2_g_blk` の保持ブロックを追う 20 行 + 無効化点の洗い出し) | 共有バッファの取り違えは過去に 2 度ファイルを壊している (gotcha §4-24 / §4-32)。効きの割に危ない。やるなら `ext2_read_block` / `ext2_write_block` の中だけで (ctx, block) を記録し、`ext2_mount` の生 memcpy で必ず無効化する |
| inode が 1 バイトも変わっていなければ `ext2_write_inode` ごと省く | 上書き専用で 10 → 6 セクタ | 小 (10 行) | `ext2_current_time()` が定数なので「サイズが伸びない上書き」には効く。tar の create はサイズが毎回伸びるので今回は効かない。`cp` の上書きや SQLite のページ書きには効く |
| ブロックキャッシュ層 (write-back) | 大 | 大 | write-through の契約が変わる。電源断・リセットの扱いを決め直す必要があり、この票の範囲外 |
| `ext2_read_block` / `ext2_write_block` が 2 セクタを 1 回の `dev_blk_*_lba(.., 2, ..)` で出す | 0 | 小 | `dev_blk_read_lba` は中でセクタごとに `blk_read_chs` を回すので、IDE の呼び出し回数は同じ。効果なし |
| `fs/vfs_fd.c` が FD にパスではなく inode を持つ | (2) の記憶と同じ | 中 (VfsOps の契約変更、全 FS ドライバ) | 記憶で同じ効きが取れたので不要。将来 VfsOps を触るときに検討 |

## 5. 残り: ゲストでの確認 (PM)

1. `make clean` は不要 (KAPI は触っていない)。`make kernel` → `make deploy-kernel`
   ([D1] NP21/W 停止) — 段 B (ext2) はカーネル側。
2. `make programs` (または `make all`) → `make deploy` → ゲストで `hsync` —
   段 A (microtar / tar) はユーザーランド側。`/bin/tar.bin` が新しくなったことを
   `ls -l` のサイズで確かめる ([V1] / [V4]、文言で判断しない)。
3. 期待値: `tar c /tmp/e8.tar /etc/system.cfg` が 15 秒超 → **1 秒未満**
   (ホスト実測で 78 セクタ ≈ 0.03 秒 + シェルの往復)。
   `/api/cmd` のタイムアウトは縮めないこと ([V3])。
4. `tar t` / `tar x` で書庫の中身が今までどおりであること。少し大きいもの
   (`tar c /tmp/bin.tar /bin` のような数百 KB) でも往復すること。
5. 回帰の目: `cp` / `cat >` / SQLite (`cfg` コマンド) が壊れていないこと。
   ext2 の空き数がずれると `df` が合わなくなるので、書き込みのあと `df` と
   再起動後の `df` を突き合わせるのが早い。

## PM 受入 (2026-09-14)

- 段 A (microtar、`9f16549`) + 段 B (ext2、`f6e99ef`) を着地。`make check` (ext2_write_io 10/10、tar_cmd 8/8 を含む) exit 0。
- ゲスト (HostDrv 経由で `hsync`、ユーザーランドのみ。**カーネル側の ext2 修正 (段 B) はまだ NHD に配備していない** = 次のカーネル配備で N1 の修正と束ねる): `tar c /tmp/e9.tar /etc/system.cfg` = **0.38 秒** (従来 15 秒超)、`/etc` 全体 (9 エントリ) = **1.2 秒**、`tar t` 一致。**段 A だけで実用速度に達した** (書き込み回数の削減が支配的)。段 B は配備後にさらに縮む見込み。
- S6-P は実質解決。段 B のカーネル配備とゲスト再測は N1 修正配備と同時に行う。
