# tools/check_le_access.py — 設計
> 状態: **現行 (2026-11-18)** — 出所: 移植準備 順序 4-a (SURVEY_N1)・TASK_CLANG_CHECKS の契約を今のソースと照らして要約
ソース: `tools/check_le_access.py` (`GUARDED`・`direct_access`・`rules`・`main`)・`tools/clang_ast/__init__.py` (`alignment_cast`)・`sys/include/endian_le.h`

媒体・書庫の上のバイト列 (ext2 の superblock / group descriptor / inode / ディレクトリエントリ / 間接表、ISO9660 のレコード、KCG フォント書庫のヘッダ) を `*(u32 *)&buf[off]` で読み書きすると、「CPU がリトルエンディアン」と「非整列アクセスが許される」の 2 つの前提が隠れる。x86 では動くが、整列を要求する CPU では落ち、ビッグエンディアンでは値が化ける。ARM のコンパイル計測には現れない (コンパイルは通り、壊れるのは実行時)。外部形式は `sys/include/endian_le.h` の `le16_rd`・`le16_wr`・`le32_rd`・`le32_wr` (バイト単位で組み立てる純 C) を通す (`make check-le-access`)。

## 検査の規則

`GUARDED` の 9 ファイル (`sys/fs/ext2/ext2_super.c`・`ext2_inode.c`・`ext2_dir.c`・`ext2_file.c`・`ext2_fmt.c`・`ext2_vfs.c`、`sys/fs/isofs/iso9660.c`、`sys/dev/video/kcg/kcg.c`、`lib/utf8.c`) を、実ビルドの旗で clang に解析させ、そのファイルの中の C 形式のキャストを見る。

- **直アクセス** (`direct_access`): ポインタからポインタへのキャストで、先の型 (正規化した型) が 2 バイト以上の整数。typedef の別名でも、キャストと参照を 2 行に分けても捕まえる。整列は関係なく、バイト順の前提として拒否する。
- **整列を上げるキャスト** (`alignment_cast`): 先の型の要求整列が元の型より大きいポインタのキャスト。
- 1 つでもあれば `<ファイル>:<行>: <規則>` を出して終了コード 1。`GUARDED` のどれかがコンパイル行に無い、または解析に失敗したら、それも違反として落とす (SKIP しない)。

## この検査が言わないこと

- **`GUARDED` に無いファイルは見ない。** 新しく外部形式を触るファイルを書いたら手で足す (足し忘れを検出する仕組みは無い)。1 バイトずつ触るコード (ext2 のビットマップなど) は元から移植性があるので足さなくてよい。
- x86 で振る舞いが変わらないこと (等価性) は見ない。等価性は ext2 の試験と実物の `e2fsck` が見る。
- ビッグエンディアンで実際に動くことの証明ではない。

木全体の整列キャストの棚卸しは別の道具 `tools/clang_ast/align.py` (件数を出すだけで落とさない)。

検査の道具の試験: `tools/tests/test_clang_ast.py` (typedef・複数の修飾子・文字列連結の asm・マクロの asm と対照を有効な C で確かめ、規則を壊す変異が実行時に落ちることを見る)。
