# userland/system/inst_hdd.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_HDD_INSTALL 段 2 (9〜11・明示の消去 `ERASE`、§1-v3 N4〜N6・R3-1) の契約を今のソースと照らして要約
ソース: `userland/system/inst_hdd.c`・`userland/system/inst_hdd.h` (`inst_hdd_check`・`inst_hdd_check_media`・`inst_hdd_describe`・`inst_hdd_ask_erase`・`inst_hdd_release`・`inst_hdd_prepare`・`platform_install_write_boot`・`inst_hdd_incomplete`・`inst_hdd_getkey`・`inst_hdd_getkey_after_key`)・呼び手 `userland/system/cdinst.c`・`userland/system/install.c`

機種の口は `include/platform_install_boot.h`、計画の型は各機種の `platform_install_layout.h`。
PC-98 の `platform_install_target` は hdprep と同じ CHS 計画を作り、`inst_hdd_prepare` はその drive・start・len を整形に渡す。
`platform_install_partition` が表を書いて検読し、通常マウント後に `platform_install_write_boot` がローダと IPL を順に書く。
PC-98 の実装は `platform/pc98/install_target.c`。PowerMac は機種側の実装が揃う段で常駐シェルへ登録する。


cdinst (CD) と install (FD) が共有する、KAPI で hd0 (= DA 80h だけ、HDD の自動検出はしない) を検査して書く手順。
純粋な判定は `userland/system/inst_disk.c` と `userland/shell/hdprep_plan.c`。

## 順序 (両インストーラ共通)

1. **全検査** (1 セクタも書かない): 媒体 (呼び手) → `inst_hdd_check` (hd0 の幾何・モード・マウント) →
   `inst_hdd_check_media` (大きさ・容量)。
2. `inst_hdd_describe` (確認画面) → `y/N`。
3. 表が使えないディスクなら `inst_hdd_ask_erase` (`ERASE` の打鍵)。
4. `inst_hdd_release`: hd0 が `/hd0` にマウントされていれば `sys_umount_checked("/hd0")` で外し、`dev_mount_count(0)` が 0 でなければ
   断る。ここまで書かない。続いて消去が要るなら LBA 0 → LBA 1 を 0 で書いて読み戻す。
5. `inst_hdd_prepare`: `ext2_format_at` → 区画表 (項目 0 だけ) → 読み戻し比較 → `sys_mount("/hd0")` で確認。
6. `inst_hdd_write_boot`: ローダ (LBA 2〜、512B ずつ書いて読み戻し、端数は 0) → IPL (LBA 0、[8]/[9] に BIOS 幾何)。
7. 呼び手がディレクトリ・展開・`vfs_sync`。

ローダと IPL をマウントの確認の**後**に書くので、どこで止まっても次の実行は通る (format の失敗なら空のディスク、
区画表の後なら再作成)。書いた後の失敗は `INCOMPLETE: <理由>` を出し、「完了」は出さない。書き込みか読み戻しが
失敗したら後続を書かない。

## マウントの検査

ルートが hd0、または `/hd0` 以外にもマウントされていれば、確認画面より前に断る (`Nothing was written`)。
`/hd0` にだけマウントされていれば確認画面に `will be unmounted first` と出し、手順 4 で外す。断りの文言は umount を
呼んだかで分ける: 呼ぶ前 (`/hd0 no longer holds hd0`、マウントが増えた) は `Nothing was written`、呼んだ後 (umount の失敗・
外したのに残る) は `Nothing was erased or formatted` と umount が書き出し得る旨 (umount は外す前に FS を sync する)。

## 明示の消去 `ERASE` (N4 の例外)

- 聞くのは `inst_classify` が `INST_E_FOREIGN`・`INST_E_MULTI`・`HDPREP_E_MBR_SIG`・`INST_E_BROKEN`・`INST_E_START` を返したとき
  だけ (`InstTarget.erase_needed`)。空のディスク・再作成では聞かない。既定は断るまま。
- 検査の表示: 理由とコード、他の OS の区画なら「ERASE しか道が無い」、中途半端な OS32 の項目ならホスト側の手当て
  (NP21/W は `make nhd-init`) も。続けて `Current contents of hd0:` の要約 (LBA 0 の先頭 4 バイトと 55AA、項目の数、
  各項目の mid・sid・名前 (表示できない字は `.`)・開始と終了の C/H/S・BIOS 幾何で読んだ LBA 範囲か `not a valid range`)。
- 表が使えないディスクは**消した後の空のディスクとみなして**大きさ・容量・マウントの検査と確認画面まで進む。確認画面に
  「After y, type ERASE」を出す。
- `y` の後に `Erase hd0's partition table? Type ERASE:` へ 1 行。`ERASE` (大文字 5 文字の完全一致) + 行末 (CR・LF・CRLF) だけが通る。
  空行・小文字・前後の空白・制御文字 (NUL・BS) を含む行・ESC・15 文字を超える行は `Not erased. Nothing was written.`。
- 消すのは全検査・`y`・`ERASE`・マウントの検査の後。消した後に止まったとき: format の失敗は「空のディスクとして入れ直せる」、
  区画表を書いた後の失敗は再作成の案内、消す書き込み・読み戻しの失敗は「もう一度 y と ERASE」。

## 鍵の読み方

- `inst_hdd_getkey`: kbd → serial の順に読み、0 以上はすべて入力 (受けた NUL = 0 も入力、入力なしは -1)。CR の直後の LF は
  同じ行末として捨てる (もう届いていても後から届いても)。cdinst の `[0-3]`・`y/N`、install の `y/N`、ERASE の行が同じ読み手を使う。
- `y` の後の最初の行末 1 つは `y` の行末として読み捨てる (ERASE の行の `skip_eol`)。2 つめの行末は空行 = 取り消し。
  cdinst の `[0-3]` の直後の `y/N` も同じ規則 (`inst_hdd_getkey_after_key`)。行末以外 (字・NUL・ESC) が先なら何も捨てない。

## 確かめ

`make check-hdd-stage2-host` (`tools/tests/test_hdd_stage2.py`) の `cdinst_host.c` と `install_fresh_host.c` のケース
(`modes`・`preflight`・`incomplete`・`erase`・`erase_fail`・`erase_mount`・`keys` ほか)。贋の鍵は長さ付きのバイト列 (NUL も
1 バイト) で、鍵を渡すたびに write・format・umount が 0 回であることを見る。
