# test_cd_read.py — CD 読取りの試験登録と依存

> 状態: **現行 (2026-10-31)**

`python3 -B tools/tests/test_cd_read.py --target` は ASan/UBSan のホスト試験と
製品ソースの i386-elf ビルドを行う。`--mutate` は各契約を壊す変異と対照を確認する。
実行はリポジトリ共通の `check_slot.sh` を通す。

`TREE_FILES` は一時ツリーへ製品と試験の inc もコピーする。
`CHECK.covers` に入力を宣言し、コンパイラの依存走査に inc が現れることと
`REBUILD` の再構築対象を照合する。移動した LRU・PIO の変異も inc を対象にする。
ケースと fixture の責務は [cd_read_host](cd_read_host.md)。
