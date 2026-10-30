# 常駐シェルの継承バグ台帳 (T9 の Codex 網羅レビューで顕在化、2026-09-13)

T9 (sh.bin = 常駐シェルの CPL=3 ビルド) の実装レビュー (Codex、往復 6〜9) で、**T9 以前から常駐シェルにもあった**欠陥が
まとめて挙がった。T9 の判定は「T9 由来 + T9 の修正が持ち込んだ回帰」で行い、ここに載るものは別作業で扱う。
到達可能な反例つきのものは T9 の往復の中で直した (S7 の例外として常駐 .o が変わる)。残りは未修正。

修正済み項目とゲスト受入の証拠の記録はアーカイブ `docs/archive/shell/INHERITED_BUGS_CLOSED.md`。

## 未修正 (non-blocker、別作業)

- 注入リング満杯後に次のキーも失う (`ui.c` の UTF-8 後続待ち)。
- 複合内蔵 (`source file > out`) のリダイレクトが内側の `execute_command` で解除される。
- 255B 超の起動要求が「GUI が必要」と誤表示 (`sh_launch.inc`)。
- `ask` が ASCII しか格納しない (日本語を捨てる)。
- 入れ子 glob (`source /tmp/s*.sh` の内側で glob) の解放一覧が共有され解放漏れ / 二重 free 診断。
- 引用付き builtin (`"echo" a | echo b`) をパイプ事前判定が誤拒否。引用内の `|` も分割。`exec` は空白入り引数の引用を復元しない。
- `cp -r` の収集表 64 件上限で切る (断りは出るが、途中まで写した子は残る)。
- symlink の型が DIR 以外 FILE (`ext2_vfs.c:59`)。
- glob の `mem_alloc` 失敗が行の失敗に伝わらない (`sh_args.inc:79`)。
- `?` は照合では 1 バイトに一致するが glob の開始条件は `*` だけ。
- `sys_ls` コールバック内の KAPI 再入 (CPL=0 で int 0x80) — **カーネル側**。`find.bin` が CPL=3 で `[Process crashed]`。sh.bin は写し取りで回避 (T9 B2)。
- カーネル帯ポインタを CPL=3 に返す KAPI 5 本 (`vfs_devname` / `path_get_drive` / `path_get_cwd` / `db_column_text` / `db_last_error`) — **カーネル側**。`sys_getcwd` は T9 R1 で `ring3_user_str` の写しに。
- `losetup` 後の退場で loop スロットが回収済み FD を保持 (`drivers/block/loop_dev.c:928`) — **カーネル側**。sh.bin では `losetup` / `dd loN` を cui only に。
- ファイル名補完が 126B で名前を切る (`ui.c:210`)。
- シェルの `ls` (`userland/shell/cmd_dir.c:43`) は `sys_ls` の戻り値を見ないので、列挙途中の I/O 失敗 (S3I2-K で FAT が負を返すようになった後も) を表示できない — **シェル側** (2026-09-14、TASK_S3I2 §4-7)。
