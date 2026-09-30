# ext2 ダブル間接ブロック (DIND) バグ — デバッグ計画

## 1. 概要

### 症状

`ext2_read_file` で 270KB 超のファイルを読み込むと、**ダブル間接ブロック領域のデータが破損** している。
SQLite カーネル統合時に `sqlite.bin` (372KB) のロードで発覚。

### 影響範囲

ext2 上の **全ての 270KB 超ファイル** に影響する可能性がある。

| 領域 | ブロック番号 | バイト範囲 | 状態 |
|------|------------|-----------|------|
| 直接ブロック | 0-11 | 0 ~ 12,287 | ✅ 正常 |
| 間接ブロック | 12-267 | 12,288 ~ 274,431 | ✅ 正常 |
| **ダブル間接ブロック** | **268+** | **274,432+** | **❌ 破損** |

### 発見経緯

1. `sqlite.bin` を `vfs_read` でロード → `os32_sqlite_test` (オフセット 336,999 bytes) のメモリが不正
2. HostDrv 経由で同じファイルをロード → 正常起動
3. → ext2 固有の読み取り/書き込みバグと断定

---

## 2. 根本原因分析 (静的解析)

### 2.1 主因: `ext2_write_stream` の ext2_g_aux バッファ競合

**ファイル**: `fs/ext2_file.c` L199-259

`hsync` によるファイルコピーは `sys_write` → `vfs_write_fd` → `ext2_write_stream` を経由する。
この関数はダブル間接ブロック領域で **致命的なバッファ競合** を起こす。

#### 問題箇所 (L221-245)

```c
for (; remaining > 0; bi++) {
    phys = ext2_bmap(ctx, &inode, bi);         /* [A] ext2_g_aux に間接テーブル読む */
    if (phys == 0) {
        int new_blk = ext2_alloc_block(ctx);   /* [B] ext2_g_aux をビットマップで上書き */
        ret = ext2_bmap_set(ctx, &inode, bi, (u32)new_blk);
                                                /* [C] ext2_g_aux を DINDテーブルで上書き */
        inode.blocks += 2;
        phys = (u32)new_blk;
        if (byte_in_blk > 0 || remaining < EXT2_BLOCK_SIZE) {
            ext2_mem_zero(ext2_g_aux, EXT2_BLOCK_SIZE);
                                                /* [D] 部分ブロックの場合のみゼロ初期化 */
        }
        /* ★ フルブロック書き込みの場合、[D] をスキップ!
         *    ext2_g_aux には [C] のDINDテーブル残骸が入ったまま */
    }
    ...
    ext2_mem_copy(&ext2_g_aux[byte_in_blk], &src[...], to_write);
                                                /* [E] データをコピー (DINDテーブル残骸に上書き) */
    ret = ext2_write_block(ctx, phys, ext2_g_aux);
                                                /* [F] ディスクに書き出し */
}
```

#### バグの流れ (bi >= 268, ダブル間接ブロック領域)

1. `ext2_bmap` → ダブル間接テーブル + 間接テーブルを `ext2_g_aux` に読む → `phys = 0` (新規ブロック)
2. `ext2_alloc_block` → ビットマップ操作で `ext2_g_aux` を**上書き**
3. `ext2_bmap_set` → ダブル間接テーブルを `ext2_g_aux` に読む → **上書き**
4. フルブロック書き込み (`remaining >= 1024`) の場合、`ext2_g_aux` のゼロ初期化が**スキップ**される
5. `ext2_mem_copy` → `ext2_g_aux[0]` から `to_write` バイトだけ上書き（1024の場合は全部上書きだが...）

**実は `to_write == 1024` ならデータが全部上書きされるので、この経路では問題ない** (データは正しく書かれる)。

### 2.2 もう一つの可能性: 書き込み側は正しく、読み取り側にバグ

`ext2_read_file` (L6-40) での問題の可能性:

```c
for (bi = 0; remaining > 0; bi++) {
    phys = ext2_bmap(ctx, &inode, bi);   /* ext2_g_aux にDINDテーブル読む */
    if (phys == 0) break;                /* ★ phys==0 で中断 */
    ...
    ret = ext2_read_block(ctx, phys, &dst[total_read]);
    ...
    total_read += to_copy;
    remaining -= to_copy;
}
```

`ext2_bmap` がダブル間接ブロックのエントリを正しく読めず `phys = 0` を返す場合、
`break` で途中終了し、以降のデータがロードされない。

### 2.3 第三の可能性: inode.size の書き込みバグ

`ext2_write_stream` L251-253:
```c
if (offset + size - remaining > inode.size) {
    inode.size = offset + size - remaining;
}
```

`hsync` は 64KB チャンクで書き込む。最後のチャンク書き込み後に `inode.size` が
正しく更新されないと、`ext2_read_file` が途中で止まる。

**根拠**: `vfs_read` が 372,600 を返す (期待値 372,632 との差: 32バイト)

---

## 3. デバッグ計画

### Phase 1: 問題の切り分け (読み取りか書き込みか)

#### Step 1.1: ディスク上の inode.size 検証

**目的**: `sqlite.bin` の inode に記録されたファイルサイズが正しいか確認

**方法**: カーネルにデバッグコードを追加し、`/sys/sqlite.bin` の inode.size を表示

```c
/* kernel.c の sqlite ロード前に追加 */
{
    u32 fsize = 0;
    vfs_get_size("/sys/sqlite.bin", &fsize);
    /* fsize を TVRAM に16進で表示 */
}
```

**判定**:
- `fsize == 372632 (0x5AF18)` → inode.size は正しい → 読み取りバグ (Phase 2a)
- `fsize != 372632` → 書き込みバグ (Phase 2b)

#### Step 1.2: ext2_bmap 単体テスト

**目的**: ダブル間接ブロック領域の物理ブロック番号が正しく取得できるか確認

**方法**: カーネルコードで `sqlite.bin` の inode を取得し、
ブロック 268 (ダブル間接の最初) の `ext2_bmap` 結果を表示

```c
/* ブロック 268 の phys を表示 */
u32 phys268 = ext2_bmap(ctx, &inode, 268);
/* phys268 を TVRAM に16進で表示。0なら DIND エントリ欠損 */
```

### Phase 2a: 読み取りバグの修正 (inode.size は正しいが読めない場合)

#### Step 2a.1: ext2_bmap トレース

`ext2_read_file` のループに、`phys == 0` で `break` した時のブロック番号を記録。
どのブロック番号で読み取りが中断されたかを特定する。

#### Step 2a.2: DIND テーブルの内容ダンプ

`inode.block[EXT2_DIND_BLOCK]` が指すブロックを読み、
エントリ 0 (= 最初の間接テーブルポインタ) が 0 かどうか確認。

### Phase 2b: 書き込みバグの修正 (inode.size が間違っている場合)

#### Step 2b.1: ext2_write_stream の inode.size 追跡

`hsync` の各 `sys_write` チャンク (64KB) ごとに、
`ext2_write_stream` が返す書き込みバイト数と `inode.size` の更新値をログ出力。

#### Step 2b.2: ext2_bmap_set のダブル間接ブロック割り当て確認

`ext2_bmap_set` のダブル間接パス (L288-330) で:
1. DIND ブロック割り当ての成否
2. IND ブロック割り当ての成否
3. `ext2_g_aux` / `ext2_g_blk` の再読み込みタイミング

をログで追跡。

### Phase 3: 修正と検証

#### Step 3.1: バグ修正

Phase 2 の結果に基づき修正を適用。想定される修正パターン:

**パターン A: ext2_write_stream の ext2_g_aux 競合修正**
```c
/* 新規ブロック確保後は必ず ext2_g_aux をデータ用にリセット */
if (phys == 0) {
    ...
    ext2_mem_zero(ext2_g_aux, EXT2_BLOCK_SIZE);  /* ← 無条件化 */
}
```

**パターン B: ext2_bmap の読み取りバッファ分離**
```c
/* ダブル間接テーブル読み取りにローカルバッファを使用 */
u8 dind_buf[EXT2_BLOCK_SIZE];  /* ← スタック確保 (要検討: 1KB) */
ret = ext2_read_block(ctx, inode->block[EXT2_DIND_BLOCK], dind_buf);
```

**パターン C: ext2_write_stream の直接書き込み対応**
```c
/* フルブロックの場合は ext2_g_aux を経由せず dst に直接読む */
if (byte_in_blk == 0 && to_write == EXT2_BLOCK_SIZE) {
    ret = ext2_write_block(ctx, phys, &src[size - remaining]);
} else {
    /* 部分ブロックのみ ext2_g_aux 使用 */
}
```

#### Step 3.2: 検証テスト

1. **基本テスト**: `hsync -f sys` で sqlite.bin を ext2 に再書き込み
2. **読み取り検証**: ext2 経由で `sqlite.bin` をロードし、先頭/末尾/中間バイトを検証
3. **サイズ境界テスト**: 268KB, 269KB, 512KB のダミーファイルで境界条件確認
4. **回帰テスト**: `hsync` 全体を実行し、他のファイルに影響がないことを確認

---

## 4. 関連ファイル一覧

| ファイル | 役割 |
|---------|------|
| `fs/ext2_inode.c` L228-260 | `ext2_bmap` — 論理→物理ブロックマッピング (読み取り) |
| `fs/ext2_inode.c` L262-332 | `ext2_bmap_set` — ブロックマッピング書き込み |
| `fs/ext2_file.c` L6-40 | `ext2_read_file` — ファイル全体読み取り |
| `fs/ext2_file.c` L199-259 | `ext2_write_stream` — ストリーム書き込み |
| `fs/ext2_super.c` L25-38 | `ext2_read_block` — ブロック読み取り基盤 |
| `fs/ext2_super.c` L18-19 | `ext2_g_blk` / `ext2_g_aux` — 共有バッファ宣言 |
| `kernel/kernel.c` L338 | sqlite.bin ロード呼び出し |

## 5. 作業順序

```
Phase 1.1 (inode.size 検証)
  ↓
  ├─ 正しい → Phase 2a (読み取りバグ)
  │            ↓
  │           Phase 2a.1 → 2a.2
  │            ↓
  │           Phase 3 (修正)
  │
  └─ 間違い → Phase 2b (書き込みバグ)
               ↓
              Phase 2b.1 → 2b.2
               ↓
              Phase 3 (修正)
```

## 6. 注意事項

- `ext2_g_blk` / `ext2_g_aux` はシングルタスク前提のグローバル共有バッファ。
  将来的にはバッファプールや関数ごとのスタックバッファに移行すべき。
- 修正後は必ず `make clean && make all` で完全再ビルドすること。
- デバッグコードは TVRAM 直書きを使用（`kprintf` は再入問題あり）。
