# HDD インストーラ (段 2) — RED→GREEN の記録

- 票: [docs/archive/realhw_v21/TASK_HDD_INSTALL.md](../../docs/archive/realhw_v21/TASK_HDD_INSTALL.md) 段 2 (9〜11) / §1-v3 (N4・N6・N8・R3-1)
- 実行: `python3 -B tools/tests/test_hdd_stage2.py --target --mutate` (`make check-hdd-stage2-host`)
- ハーネス: `tools/tests/hdd_stage2_host.c` (純粋関数、ホスト 64 ビット + libc)、
  `tools/tests/cdinst_host.c` (実物の cdinst.c を main から、ILP32 -nostdlib)、
  `tools/tests/install_fresh_host.c` (実物の install.c を main から、`test_install_fresh.py` と共有)、
  `tools/tests/ext2_mini_host.c` (ローダの ext2 読み手、ILP32 -nostdlib)

## 1. 対象と見るもの

| 対象 | ケース | 見るもの |
|---|---|---|
| `userland/system/inst_disk.c` | `classify817` / `classify1663` | 空・空で 55AA・再作成 (標準配置、項目が表のどこでも、名前の後ろが NUL でも)・開始が 1 シリンダ前後・名前違い (OS32X / os32)・sid 違い・2 項目 (mid だけの項目も数える)・ディスクの外・壊れ、旧配置 8/17 (シリンダ 12 = LBA 1632) は再作成、旧配置 16/63 と 8/17 の標準配置の表を 16/63 で読むと開始 12,096 → 断る |
| 同 | `bootfiles` / `blocks` / `space` | IPL 1〜512・ローダ 1〜8192・vmkernel 1〜508KiB の境界、間接ブロック (13 ブロック目・二重間接の境目)、ブロック / inode の不足と「ちょうど収まる」、配置が成り立たない大きさ |
| 同 + `hdprep_plan.c` | `iplpt` | 8/17 (1632, 407,864)・16/63 (2016, 524,160)・8/17 で 256MiB を 136 で切り下げ (524,280) の計画から作った区画表を共有部で読み戻すと同じ範囲、残りの項目は空、IPL の [8]/[9] = 区画表の幾何 |
| 容量の見積もり | room | 16,652 / 20,160 / 36,000 / 62,496 セクタの空き (ブロック・inode) が **実物の `ext2_format_at` の像の `dumpe2fs`** と一致 (変異のたびにも見る) |
| 定数 | consts | `INST_KERNEL_MAX` = `boot/boot_defs.h` MAX_IMAGE_SIZE、`INST_LOADER_MAX` = `nhd_deploy.py` LOADER_MAX_SECTORS × 512、`INST_EXT2_MAX_GROUPS` = `fs/ext2_ctx.h`、IPL の [8]/[9]/[10] に `boot_hdd.bin` の既定 8 / 17 / 80h |
| `userland/system/cdinst.c` | `ok817` / `ok1663` / `modes` / `preflight` / `incomplete` | 8/17 (ドライブは 16/63 を申告) と 16/63 で区画表・IPL の幾何・IPL とローダの中身 (BOOT.PKG の中身、端数は 0)・format の範囲・順序 `[U] F W1 M W2.. W0`、旧配置 8/17 の再作成 (マウント中は外してから)・未知・2 項目、事前検査 (ローダ 8193・IPL 513・IPL 無し・LZSS の BOOT.PKG・vmkernel 508KiB ちょうどは通り +1 は断る・vmkernel 無し・shell 無し・NORMAL が 300MB を名乗る (Minimal なら数えない)・前置で溢れる項目・BIOS 幾何なし・承認しない) で 1 セクタも書かない、format / 区画表の読み戻し / マウント / IPL / NORMAL の展開 / sync の失敗は INCOMPLETE で「完了」と出さない |
| `userland/system/install.c` | `geom817` / `geom1663` / `modes` / `preflight` / `incomplete` / `rerun` (+ 既存 12 本) | cdinst と同じ規則を FD 側で。加えてマウントの 4 通り (ルートの hd0・別の場所・umount_checked の失敗・外しても残る)、FD の列挙の失敗は書く前に分かる、途中で止まった hd0 (format 失敗 → 空 / マウント失敗 → 再作成) と完了した hd0 を入れ直せる |
| `boot/ext2_mini.c` | mini | 1,000 B と 508KiB ちょうどは読んで後ろに書かない、508KiB + 1 と 600KiB は `EXT2M_ERR_TOO_BIG` で**読み先に 1 バイトも書かない** |

## 2. RED

- `inst_disk.c` / `inst_hdd.c` と cdinst / install の新しい手順はこの票で作った。**実装と試験は同じ作業の
  中で書いた (試験先行ではない)**。旧動作 (IPL のヘッド 8 固定・IDENTIFY の幾何を IPL に書く・ディスク全体の
  format・区画表を読み戻さない・ext2_mini の切り詰め) は §3 の変異で再現して RED を確かめた。
- `boot/ext2_mini.c` の上限は段 0 で直っていた。この段で初めて試験を付けた (変異 2 本が RED)。

## 3. 変異 (`--mutate`)

41 本を写しの上で 1 本ずつ当て、4 本のハーネスを全部組み直して回す。**ビルドが通って試験が落ちた**ものだけを
RED、ビルドが通らないものは ERROR (何も確かめていない) と数える。変異させる 5 ファイルそれぞれに恒等変異
(末尾に注釈 1 行) の対照を当て、SURVIVED になることを確かめる。

結果 (2026-09-24): **41/41 RED (ERROR 0、SURVIVED 0)、対照 5/5 SURVIVED**。

- 最初の回は ERROR 7 (変数・関数が未使用になり `-Werror` で落ちる形) と SURVIVED 2 だった。ERROR は
  値を使ったまま判定だけ外す形に書き直した。SURVIVED の 1 本 (空きの見積もりがルートの 1 ブロックを数えない) は
  dumpe2fs との突き合わせを変異のたびにも回すようにして RED にした。もう 1 本 (`inst_hdd_check` の
  `hdprep_check_geom` を外す) は `hdprep_plan` が同じ検査を中でもう一度するので**等価変異**。外して、
  「検査でマウントを数えない」に置き換えた。

## 4. 実装レビュー往復 1 (2026-09-24、Codex P1-1〜3・P2-4〜6 / Fable minor) と NP21/W の落ち

足したケース:

- `cdinst_host.c` `preflight`: データ部が 10 バイト足りない NORMAL.PKG / 1 バイト足りない BOOT.PKG /
  orig_size = 0 の MINIMAL.PKG と BOOT.PKG / 空の shell.bin / 1 ファイルの上限 + 1 (67,383,297 B) /
  13MB の区画に 30MB (Minimal なら数えない) / ルートが hd0 / umount_checked の失敗 / 別の prefix にも
  マウント — どれも 1 セクタも書かない。`paths`: `/../hd1/evil.bin`・`/usr/../../hd1/x`・`/usr/./x`・
  `//x`・`/x/`・`x/y`・`/.`・`/..` と MINIMAL の中の `..` は断り、`/hd0` の外に 1 つも作らない。
  深さ 31 要素 (`/hd0` と合わせて 32) は入り、32 要素は断る。`incomplete`: REBOOT の案内。
- `install_fresh_host.c`: `/sys/shell.bin` が無い・空・ディレクトリ、ローダがディレクトリ、hdd_geom_info の
  失敗、別の prefix / 2 か所のマウント、INCOMPLETE は 1 回だけ、区画表が媒体の上で壊れた場合
  (INCOMPLETE + ホスト側の手当て → 次の実行は 1 セクタも書かずに断り、同じ案内) を足した。
- `hdd_stage2_host.c` `paths` / `space`: パスの規則の表、inode の余白の境界、失敗でも room が 0 で埋まる、
  上限ちょうど / +1。
- 実物の `packages/*.PKG` の全 209 項目のパスが `inst_check_path` を通る (real PKG paths)。
- `boot_hdd.asm` の `mov cx, 16` × 512 = `INST_LOADER_MAX` (consts)。
- **str-return guard**: インストーラ (CPL=3) が呼ぶ `const char *` の KAPI は、どれも CPL=3 に写しを返す実体に
  つながる (`vfs_devname` → `vfs_devname_user`)。628c61f は NP21/W で cdinst が `vfs_devname("/")` の返り値
  (カーネルのマウント表) を読んで fault kill された。ホスト試験の贋物は利用者の文字列を返し、ページの保護も
  無いので見えなかった — この種類はホストのハーネスでは再現できないので、kapi.json の target と生成物を見る。

変異は 21 本を足して **62/62 RED (ERROR 0、SURVIVED 0)、対照 5/5 SURVIVED**。足した直後は「BOOT.PKG の
データ部を見ない」が SURVIVED だった (切れた BOOT.PKG は comp_size 分の読みで別に断れるので同じ結果になる)。
orig_size が表と食い違う BOOT.PKG のケースを足して RED にした。展開の直前のパスの検査 (事前検査と同じ規則の
2 回目) だけを外す変異は、事前検査が先に断るので区別できない — 変異の表には入れていない。

## 5. 実装レビュー往復 2 (2026-09-24、Codex P1-1・P1-2 / Fable minor)

足したケース (`cdinst_host.c` `final`): 型 2 の `/sys/shell.bin`・型 7 の `/etc/odd` は断る、NORMAL の大きさ 0 の
shell (Normal は断り、Minimal は通って 1000 B のまま)、同じ MINIMAL の中の重複 (後の 0 B が勝つ → 断る)、NORMAL が
vmkernel を 508KiB + 1 で置き換える → 断る、NORMAL の 500 B の shell は最終の大きさで入る、展開の後に shell の
大きさが違う → INCOMPLETE。`install_fresh_host.c`: 写した shell の大きさが違う → INCOMPLETE、他の OS の区画は
「対象外」の文言で `nhd-init` を出さない、中途半端な OS32 の項目 (開始違い・壊れ) は `nhd-init` を出す。
偽の `vfs_devname` は実物と同じく未マウントで `""` を返す。

str-return guard は `const char *` を返す KAPI の全部 (6 本) の target が CPL=3 に読める実体であること、生成物の
wrap がそれを呼ぶこと、`exec/exec.c` の 4 本の `*_user` がトランポリンの写しを返すこと、userland/ の全 C ソース
(16 か所) がそれ以外を呼ばないことを見る。target を 1 本外した写しで NG になることを手で確かめた。
カーネルの写しそのもの (返り番地がトランポリンページの中) は kselftest の `test_tramp_user_str` で実機が見る
(ホストでは見られない)。

変異は 6 本を足して (別に 3 本を書き換え、62 + 6 = 68) **68/68 RED (ERROR 0、SURVIVED 0)、対照 5/5 SURVIVED**。

## 6. 明示の消去 ERASE (2026-09-25、wt/cdinst-wipe、N4 の例外)

実機 Ra266 の 8GB が FOREIGN (-40) で断られた (前の OS の区画表)。断る表のときだけ要約を出して `ERASE` を
求める道を `inst_hdd.c` に足した (票 TASK_HDD_INSTALL 段 2 の追補)。

足したケース (`cdinst_host.c` と `install_fresh_host.c` に同名の 3 本):
- `erase`: FOREIGN・MULTI・55AA だけ・BROKEN・START × 8/17・16/63 × ERASE でない入力 12 通り (空行・`erase`・
  `ERASE `・` ERASE`・`y`・`Erase`・`ERAS`・`ERASEE`・BS 入り・ESC・LF だけ・20 文字) で 1 セクタも書かず、
  ディスクの像が変わらず、行の終わりより先の鍵を読まない。`ERASE` (CR / LF) なら順序が `W0 W1` から始まり、
  空のディスクとして最後まで通る (区画表・IPL・ローダ・format の範囲)。要約の文言 (55AA、項目の数、mid・sid・
  名前・開始と終了のシリンダ、16/63 の LBA 範囲、読めない範囲)。空のディスクでは聞かない。
- `erase_fail`: 消した後の `N` (install は終了コード 1)、IPL 513 B での断り、format の失敗、消す書き込み・読み戻しの
  失敗 (format に進まない)、区画表を書いた後の失敗では「空のディスク」と言わない。`N` の後の再実行は聞かずに
  空のディスクとして通る。
- `erase_mount`: ルートの hd0・別の prefix では聞かない、umount の失敗・外したのに残るでは消さない、`/hd0` だけ
  なら `U W0 W1 …` の順で外してから消す (最初の書き込みの時点でマウント 0)。
- 既存の断るケースは ERASE の問いに空行で答える。install の贋の鍵は尽きた後に 100000 回読まれたら落とす
  (1 行読みが鍵を待ち続けて止まらないように)。

変異は ERASE の 30 本を足して (既存の 2 本を移動したコードに合わせて書き換え、68 + 30 = 98) **98/98 RED
(ERROR 0、SURVIVED 0、NOT_APPLIED 0)、対照 5/5 SURVIVED**。初回は 3 本が数えられなかった (置き換え元が
2 か所にあった 1 本、組めなかった 2 本) — 文脈を足して書き直した。長すぎる行を切り詰めても `ERASE` にはならない
ので「長すぎる行を許す」変異は区別できず、表に入れていない。

## 7. ERASE の実装レビュー往復 1 (2026-09-25、Codex Request changes → PM 決定で順序を変更)

指摘: [P1] `ih_getkey` が 0 を読み捨てていた (実物のドライバは「入力なし」= -1、受けた NUL = 0) のでシリアルの
`ERA<NUL>SE<CR>` で消えた。贋の鍵が NUL 終端の文字列で「入力なし」も 0 だったので見えなかった。[P2] CRLF の
Enter は CR で行を閉じた後の LF が次の `y/N` に届いて取り消しになった。順序: 消すのが y/N より前だったので、
大きさ・容量の不足が分かっているのに区画表だけを失えた。

直し: 共通の `inst_hdd_getkey` (0 以上はすべて入力、CR の直後の LF は捨てる、覗いた別の字は戻す) を cdinst の
`[0-3]`・`y/N` と install の `y/N` と 1 行読みが使う。順序は全検査 → 確認と `y/N` → `ERASE` の打鍵
(`inst_hdd_ask_erase`) → マウントを外す → 消す (`inst_hdd_release` の中) → format。表が使えないディスクは
`erase_needed` を立てて空のディスクとみなし、確認画面に「After y, type ERASE」を出す。`inst_hdd_stopped` は
不要になり削除 (確認の前に止まるのは必ず「何も書いていない」)。

贋物: 鍵は**長さ付きのバイト列** (`KEYS(lit)`、NUL も 1 バイト)、尽きたら -1 (100000 回で落とす)、`keys_serial`
で serial から、`keys_gap` で鍵の間に「入力なし」、`keys_hook` で特定の鍵を渡す直前にマウントを変える。鍵を渡す
たびに write・format・umount が 0 回であることを見る (どの鍵も書く前に読まれる)。読み戻しの違いは位置を選べる
(`inj_readback_off`: 先頭側と 511 = 末尾)、LBA ごとの write / read の失敗 (`inj_write_fail_lba` /
`inj_read_fail_lba`、読み戻しだけなら `inj_read_fail_after_write`)、`/hd0` に別のデバイス (`hd0_other_dev`)。

足したケース: cdinst `keys` (`inst_hdd_getkey` / `ih_read_line` を直に: CRLF・LF LF・NUL・後から届く LF・serial・
覗いた字の戻し・NUL 入りの行・ESC の後の字)。`erase`: ERASE でない 17 通り (NUL 入り 5 通りを含む) × kbd / serial
× 鍵の間の「入力なし」、通る行末 3 通り (CR・LF・CRLF、CRLF の LF も読み切る)、表示の順序 (要約 → 確認 → ERASE の
問い → 消去 → format)。`erase_fail`: `N`・IPL 513 B・容量不足は消す前 (確認も ERASE も無い)、format の失敗の後は
空のディスクとして入れ直せる、消す書き込み / 読み戻し (先頭側と 511 バイト目) の失敗。`erase_mount`: ERASE の
入力中に別の場所 / `/hd0` にマウントされる、`/hd0` の相手が hd1 になる / 外れる → 1 セクタも書かない。
`incomplete` (cdinst / install): LBA 1・2・5・17・0 の write の失敗と LBA 1・5・0 の read の失敗で後続の書き込みが
止まる (順序の記録で見る)、読み戻しの 511 バイト目だけが違う (区画表 / ローダ / IPL)。

変異は 98 → **117 本**: 削除 2 (無くなったコード)、書き換え 6 (順序の変更)、追加 21 — P1 (0 を読み捨てる、行の中の
NUL を読み捨てる、serial を読まない)、P2 (LF を捨てない、届いている LF を読まない、覗いた字を捨てる)、順序 (検査の
中で消す、マウントの検査より前に消す、ERASE を受けても消さない、打鍵なしで消す、確認画面に出さない)、比較の長さを
511 に縮める (2 か所)、write の失敗の後も書き続ける (ローダ / IPL / 区画表)、呼び手 (ERASE でなくても先へ進む・
聞かずに消す・N でも書く・旧の鍵読みに戻す) — 結果は下の「変異の結果」。

初回の `--mutate` は「serial からの鍵を読まない」で**止まった** (27 分): 台本を serial から出す試験で、変異体は kbd
だけを回し、贋物の「尽きた後 100000 回で落とす」は台本を出す側にしか無く、`run_cases` にも上限が無かった。
贋物は台本を出さない側の -1 も数えて落とし (`keys_none`)、`run_cases` は 1 ケース 120 秒 (`CASE_TIMEOUT`、mutate は
`TimeoutExpired` を RED と数える) にした。
同じ回で SURVIVED が 3 本: 「前の実行の消したを持ち越す」(完了した実行の後に消して format が失敗する順が無くなって
いた → 持ち越しの 2 通りを足した)、「元の断りのモードのまま」(`empty disk` が案内の文にも含まれていた → `Target:` の
行の全文で見る)、「cdinst の y/N が旧の鍵読み」(確認への NUL を読み捨てるかどうか → `1<NUL>y` で取り消しになる試験を
足した)。install の y/N の同じ変異は `confirm_install` が y/n/Enter/ESC 以外を読み飛ばすので区別できず、表から外した。

