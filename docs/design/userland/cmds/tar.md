# userland/cmds/tar.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S6・TASK_S6P の契約を今のソースと照らして要約
ソース: `userland/cmds/tar.c`・`lib/microtar/microtar.c` (改変点は `lib/microtar/README.OS32`)

設定のバックアップを束ねるための最小の ustar。圧縮は持たず、`lz4` を外で掛ける。

```text
tar c ARCHIVE PATH...      PATH... を束ねる (ディレクトリは再帰)
tar x ARCHIVE [-C DIR]     DIR (既定は .) へ展開する
tar t ARCHIVE              中身を一覧する
```

## 契約

| 項目 | 決まり |
|---|---|
| 扱う物 | 通常ファイルとディレクトリだけ。リンク・デバイス・所有者・時刻は持たない (ヘッダの mode・mtime は書くが展開では使わない) |
| ustar の読み書き | vendor した `lib/microtar/` (MIT)。I/O だけ KAPI (`sys_open`・`sys_read`・`sys_write`・`sys_lseek`・`sys_close`) に差し替える (`mt_read`・`mt_write`・`mt_seek`)。stdio 版は `MTAR_NO_STDIO` で外す |
| 名前 | 先頭の `/` を落として格納する。ustar の name 欄は 100 byte なので、NUL 終端のため 99 byte (`TAR_NAME_MAX`) を超える名前は作る側でも読む側でも拒否し非ゼロで終わる。100 byte を使い切った書庫は `t`・`x` とも落ちずに非ゼロで、壊れた名前で書き出さない |
| 展開の安全 | `..` の成分を含む名前と絶対名は `unsafe path` で断る |
| ディレクトリの判別 | `sys_stat` の `st_mode` ではなく `sys_open` が失敗するか (`io_stat`)。`vfs_open` はディレクトリを断る |
| 一覧 | `sys_ls` の callback では名前を集めるだけで、戻ってから開く・再帰する (callback の中で FS に触らない) |
| 上限 | 再帰の深さ `TAR_MAX_DEPTH` (8)、1 回の作成で覚える名前 `TAR_ENT_MAX` (512)。超えたらエラー |
| 誤り | `tar: <パス>: <理由>` を出す。入力が無いときは `No such file or directory`。1 本が欠けても残りは束ね、終了コードは非ゼロ。引数不足は usage と非ゼロ |
| メモリ | ヒープを使わず静的バッファだけ (`build/app.conf` の既定のまま) |

## 書き込みの形 (ext2 で遅くしない)

- microtar の `write_null_bytes` (データ後の 512 byte 境界までの詰め物と、終端の 2 レコード) は 512 byte 単位で書く (1 byte ずつ書かない)。詰め物の配列は `static const` (外部プログラムのスタックは細い)。
- 15 byte のファイル 1 本の `tar c` は `sys_write` が 5 回 (`512 / 15 / 497 / 512 / 512`) で、書庫は 2048 byte。試験はこの回数が 8 以下・最小の書き込みが 15 byte 以上であることを表明する。
- 後方の `lseek` は `vfs_seek` が offset を書き換えるだけで、ext2 の I/O を出さない。create の経路には後方の seek が出ない (`mtar_seek` は読み側)。

## ゲストでしか確かめられない事

ホスト試験 (`tools/tests/test_tar_cmd.py`) は I/O を POSIX に差し替える (`-DHOST_TEST`) ので、次はゲストで見る。

- 読み側の後方 `sys_lseek` (`mtar_read_header` は 512 byte 読んでヘッダの先頭へ戻る) が ext2・HostDrv・FDD (FatFs) で同じに効くか。
- `sys_open` の失敗でディレクトリを判別する方法が FS ごとに同じに効くか。
- 1KB 境界に乗らないサイズのファイルの往復、`lz4` との組み合わせ、ゲストで作った書庫をホストの Python `tarfile` で読めること。
