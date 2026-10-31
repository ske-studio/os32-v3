# tools/tests/ext2_block_io_host.c — ext2 範囲 I/O 模型

> 状態: **現行 (2026-10-31)**

`test_ext2_block_io.py` は製品 `ext2_super.c` を ILP32 GNU11 で組み、
Device だけを RAM 媒体へ差し替える。未使用の mount 依存はセクション GC で除く。
`host32` の指定 runner で実行する。

blocks=1,2,32 の各範囲読書きが要求 1 回、count=2,4,64 となること、
LBA・全バイト・前後 canary・範囲外媒体が一致することを確認する。
blocks=0,33、区画外、開始／末尾 overflow は要求 0。
末尾 LBA がちょうど U32_MAX の有効範囲も確認する。

途中 READ と部分 WRITE 失敗は Device の負のエラーをそのまま返す。
data は fs_error を立てず、metadata は立てる。
既存の 2 セクタ書込み順序、sector 操作、反復読取りの再発行も検査する。
mounted な metadata 失敗時には、専用バッファで媒体の errors 印を書くことも通す。
D1 の保持枠と関数別失効・remount の統合試験は D1 の結線後に行う。

実行は `python3 -B tools/tests/test_ext2_block_io.py`。
`--mutate` は未変更コピーの成功後、範囲・overflow・count・失敗契約を壊した
製品 inc の実行時 assertion による RED を要求する。
CHECK は新 inc と入力を宣言する。既存の HDD stage1、read_bound、write_io は回帰対象。
HDD stage1 の format 模型は読取り count=2 をセクタ列に展開して照合する。
固定した書込み順序と全 2008 セクタ位置での失敗注入は保ち、書込み count=1 を要求する。
製品契約は `ext2_super_block_io`。
