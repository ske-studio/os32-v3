# kcg boot 専用化のホスト検証 (T2e e8b)

- 票: [TASK_T2D_T2H.md](../../docs/tasks/v3/TASK_T2D_T2H.md) §2-3 / e8b 実装結果。
- 実行: `python3 -B tools/tests/test_kcg_boot.py --runner qemu --mutate`。
- `make check-kcg-boot-host` は HOST32_RUNNERS の全runnerで正常対照、先頭で変異。
- 実物の drivers/kcg.c / lib/utf8.c 全文を取り込む ILP32 足場
  `kcg_boot_host.c`。変更はテスト内の物理RAM変換、I/O stub と検証の観測口だけ。
  VFS は実際のヘッダと payload を返す stub。LZ4 は実物 lib/lz4.c を使用。
  カーネル本番の Rust LZ4 の再検証や KAPI syscall dispatch の試験ではない。

正常12シナリオ: 非圧縮/圧縮boot読込、正常表で閉鎖、閉鎖後のpath/NULL呼出し、
二重閉鎖、kcg_initでも再開しない、4点をそれぞれ壊した表、読込失敗ready=0の維持。
閉鎖時はscratch失効→4点照合→BB全128KiB→mailboxの868Bを観測する。
cache/Unicode/余り領域は閉鎖時に不変。閉鎖後の3呼出しでは全640KiBを
バイト比較し、VFS open/read/close/get_size と logging のカウンタ不変を確認。
boot_font_load→Unicode完全読込と検証→shlib_init→bootlog_save→閉鎖→通常execの
結線順序と閉鎖1回もkernel.cで照合する。

| 変異 | 期待FAIL (実行時rc=1) |
|---|---|
| boot終了ガード除去 | closed VFS untouched |
| 閉鎖後にVFS open | closed VFS untouched |
| BB消去除去 | mailbox follows BB |
| mailbox初期化除去 | close completes order |
| 4点照合除去 | invalid table ready zero |

正常はrc=0、5/5は上のFAIL文言とrc=1で検出。コンパイル失敗・signalは合格にしない。
最初の足場は未使用変数警告とVFS stubの署名不一致でコンパイル失敗、修正後は
I/O stub不足によりsignal 11。足場を修正し、12シナリオと5変異が全て成功。
本番ソースの変異は行わない。ゲスト/NP21/Wは依頼の範囲外で未実施。
