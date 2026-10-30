# fs/ext2/ext2_inode.c — ext2 inode とブロック管理

> 状態: **現行 (2026-10-31)**

inode の読書きとブロック・inode の割当てを扱う。
間接ブロック表の操作と解放を行う。

## 落とし穴

- **解放中の scratch**: 解放がビットマップを `ext2_g_aux` に読むため、そこに間接表を残すとブロック番号が化け、他のファイルと交差リンクする。
  解放をまたぐ表は `ext2_g_blk` と `ext2_g_dat` に置く (`fs/ext2/ext2_inode.c:485`, `fs/ext2/ext2_inode.c:501`)。
  割当・解放を呼ぶ間は aux の内容を保持しない。関連する媒体整合検査は `tools/tests/test_b8_open.py`。

- **参照を外してから解放**: inode が指すブロックを先に再利用すると、失敗時に別ファイルと共有される。
  inode のポインタをゼロにして媒体へ書いてから解放し、失敗時は漏れを許して参照を守る (`fs/ext2/ext2_inode.c:552`, `fs/ext2/ext2_inode.c:568`)。
  I/O 失敗を空きや不存在に変換しない。回帰は `tools/tests/test_b8_open.py`。
