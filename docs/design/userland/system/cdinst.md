# userland/system/cdinst.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_HDD_INSTALL 段 2 (11、§1-v3 N8) の契約を今のソースと照らして要約
ソース: `userland/system/cdinst.c` (`preflight`・`load_boot_pkg`・`pkg_data_ok`・`pkg_paths_ok`・`measure_packages`・`install_packages`・`required_on_hd0`)

CD の `.PKG` から hd0 へ入れる。hd0 の扱い (順序・ERASE・鍵) は `userland/system/inst_hdd.c` と共通で、ここは
パッケージの事前検査と展開だけ。FD の install (`userland/system/install.c`) は同じ規則を FD の列挙で行う。

## 事前検査 (書く前に全部)

- 選んだ型のパッケージを全部 `pkg_parse` し、前置 `/hd0` で溢れる項目・パス (`inst_check_path`、事前検査と展開の直前の 2 か所)・
  型 (ファイルとディレクトリ以外は断る — `pkg_extract` は黙って飛ばすため) を見る。
- **データ部**を表と突き合わせる: 項目の大きさの和 = orig_size、無圧縮なら comp_size = orig_size、PKG の長さ = データ部の先頭 +
  comp_size。BOOT.PKG の `boot_hdd.bin`・`loader_hdd.bin` は無圧縮であること。
- 必須 (`/boot/vmkernel.lz4`・`/sys/shell.bin`) は**展開順にたどった最終の大きさ**で判定する。展開は MINIMAL → GUI → NORMAL → DEBUG、
  PKG の中は項目の順で、後の同じパスの項目が置き換える。最終が 0 か、vmkernel が 508KiB を超えれば断る。Minimal を選んだら
  NORMAL は数えない。
- 大きさ (`inst_check_boot_files`) と容量 (`inst_check_space`) を hd0 の検査と合わせて見る。

## 展開の後

展開の後に `/hd0` の必須の実物を最終の大きさと突き合わせ (`required_on_hd0`)、違えば `INCOMPLETE`。`vfs_sync` の失敗も完了扱いに
しない。

## KAPI の返す文字列

インストーラ (CPL=3) が呼ぶ `const char *` を返す KAPI は、CPL=3 に写しを返す実体につながっていること (`vfs_devname` →
`vfs_devname_user` など、`sys/kern/exec/exec.c` の `*_user`)。カーネル帯の文字列をそのまま返すと fault kill になる。
ホストの贋物では見えないので、試験は `sdk/kapi.json` の target と生成物を見る (str-return guard)。

## 確かめ

`make check-hdd-stage2-host` (`tools/tests/test_hdd_stage2.py`、`tools/tests/cdinst_host.c`) のケース `ok817`・`ok1663`・`modes`・
`preflight`・`paths`・`final`・`incomplete` と real PKG paths (実物の `packages/*.PKG` の全項目が `inst_check_path` を通る)。
