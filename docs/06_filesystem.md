## 第6部 ファイルシステム

### §6-1 VFS (仮想ファイルシステム) レイヤー

vfs.h/vfs.cが提供する統一的なファイル操作API。  
各FSドライバ(ext2, fat (FatFs), iso9660, hostdrv等)をVfsOps関数ポインタテーブルで抽象化し、
Linuxライクなコマンド体系を実現する。動的マウントに対応し、最大同時マウント数は `VFS_MAX_FS` (8) となる。
各FSドライバはマルチインスタンス方式 (mount時にctxをkmallocし、umount時にkfree) で複数デバイスの同時マウントをサポートする。

**VFS API**:

| 関数 | 説明 |
|------|------|
| `sys_mount(prefix, dev, fs)` | マウント ("/", "hd0", "ext2") |
| `sys_umount(prefix)` | アンマウント |
| `sys_is_mounted(prefix)`| マウント状態確認 |
| `sys_ls(path, cb, ctx)` | ディレクトリ一覧 (コールバック) |
| `sys_open(path, mode)` | ファイル/デバイスオープン (FD取得) |
| `sys_read(fd, buf, sz)` | FDから読込 |
| `sys_write(fd, buf, sz)` | FDへ書込 |
| `sys_close(fd)` | FDクローズ |
| `sys_lseek(fd, off, w)` | FDシーク |
| `sys_unlink(path)` | ファイル削除 |
| `sys_rename(old, new)` | ファイル名変更 |
| `sys_mkdir(path)` | ディレクトリ作成 |
| `sys_rmdir(path)` | ディレクトリ削除 |
| `sys_chdir(path)` | カレントディレクトリ変更 |
| `sys_getcwd()` | カレントディレクトリ取得 |
| `vfs_sync()` | メタデータ書き戻し |

パス解決: `vfs_resolve_path()` が相対パスをcwd基準で絶対パスに変換する。

**エラー規約**: VFS 層が返すのは `OS32_ERR_*` (`sdk/include/os32/os32_kapi_shared.h` が正典)。
FS ドライバは自前のエラーを**境界で翻訳**する (ext2 は `ext2_to_vfs_err`)。生の errno や
ドライバ固有の負値を VFS の外へ漏らさない。
**存在確認の入口 (`stat` / `get_size`) は「そのボリュームに存在しえない名前」も NOTFOUND に写す**:
FatFs は `FF_USE_LFN 0` なので 8.3 に収まらない名前 (`settings.db-journal` など) に
`FR_INVALID_NAME` を返すが、`fatfs_vfs.c` の `ff_stat_to_vfs` がこれを `OS32_ERR_NOTFOUND` にする
(`open` / `read` / `write` 系は `OS32_ERR_INVAL` のまま — 呼び手の誤りの診断を残す)。
KAPI v50 の hot journal 検査が NOTFOUND 以外を `SQLITE_IOERR` と断じるため、
FDD ブート (root = FAT) で `db_open_existing` が落ちていた。

**列挙の途中エラーは負で返す**: `list_dir` は `sys_ls` のコールバックへ渡した項目を取り消さないが、
途中で読めなくなったら**打ち切って負**を返す (`fatfs_vfs_list` は `f_readdir` の失敗を `ff_to_vfs` で写す)。
`vfs_ls` はその戻り値をそのまま上へ返すので、呼び手 (`install` の `copy_directory` など) は
戻り値を検査しないと「欠けたまま成功」になる。

**型の検査は VFS 側で行う**: `vfs_open` はディレクトリを開くことを拒否し、
`vfs_chdir` はディレクトリ以外を拒否する。これは FS ドライバ内部の型検査を禁止するものではない。

#### FD の同一性と失効 (票 TASK_VFS_FD_PATH)

- **ext2 の FD は open 時の inode で読み書きする** (`VfsOps.ino`、任意実装で ext2 だけ)。
  read / write / fstat / `O_TRUNC` はパスを引き直さないので、rename・親の rename の後も同じ実体を指す。
  inode が取れなければ open は失敗する (`O_CREAT` で作った後なら作ったものを消す)。
  FAT / HostDrv / ISO の FD は従来どおりパスで動く (ここの保証は ext2 だけ)。
- **失効**: `vfs_rm` と置き換え `vfs_rename` の宛先は、対象の inode を**操作の前に**取り
  (NOTFOUND 以外で取れなければ操作しない)、FS の操作に入ったら**成否を問わず**同じ inode の FD に
  印を付ける。`rename(f,f)` と同じ inode のハードリンク間は付けない。`vfs_umount` はそのマウントの
  全 FD に印を付けてから fs_ctx を解放する。印の付いた FD の read / write / fstat / seek は
  **`OS32_ERR_STALE`**、close (`vfs_close_sqlite` も) は通る。
- **使用中** (`OS32_ERR_BUSY`): 開いている SQLite の DB (`vfs_open_sqlite` の FD と、旧来の
  vfs_open 経路で `vfs_fd_set_sqlite_db` を付けた FD) とそのジャーナル `<名前>-journal`、それらの
  祖先ディレクトリは rename できない (元でも宛先でも)。loop デバイスが使用中のイメージ
  (`vfs_fd_set_pinned`) は unlink・置き換えできない。どちらも印を付ける前に断る。
- 最後の砦: `ext2_read_stream` / `ext2_write_stream` / 切り詰めは通常ファイル以外を `ISDIR` で断る。

#### パスの長さと最終要素 (票 TASK_VFS_FD_PATH)

- `vfs_resolve_path` は **切り詰めずに断る** (int を返す)。入力が NUL 抜き 255 バイトを超える、
  正規化の途中で要素が 32 個を超えて積まれる (33 個目で断る — 正規化後の数ではない)、要素 1 つが
  255 バイトを超える、結果が器に収まらない — どれも **`OS32_ERR_NAMETOOLONG`**。相対パスは
  cwd + "/" + 入力を大きい作業領域で正規化してから結果で判定する。
  切り詰めていた入口 (exec のコマンド名と `/usr/bin/` 等の連結、`kapi_db_open`、`vfs_mount` の
  prefix、`sys_switch_shell`) も断るようにした。
- rmdir / rename (両引数) / unlink / mkdir は、末尾の `/` を落とした後の**最終要素が `.` / `..`**
  なら正規化の前に `OS32_ERR_INVAL` (`rmdir a/.` が a を消さない)。
- マウント点への `O_CREAT|O_EXCL` は (NOSYS の判定の後で) `OS32_ERR_EXIST`。
- **FAT は `0:` を前に付けた後の長さも見る** (`ff_make_path`)。FD 起動では FAT が `/` に
  マウントされるので FS に届く相対パスは 255 バイトまで伸び、`"0:"` + パス + NUL が 256 に収まら
  ない (相対パス 254 / 255 バイト) ものは **`OS32_ERR_NAMETOOLONG`** (253 バイトまでは通る)。
  以前は末尾を黙って落とし、255 バイトの `…/disk1.imgXY` が 253 バイトの `…/disk1.img` を消せた。
- **FAT の rmdir はディレクトリだけ**を消す。FatFs の `f_unlink` はファイルも消すので、先に種別を
  見てファイルなら **`OS32_ERR_NOTDIR`**。加えて `vfs_rmdir` は inode を持たない FS (FAT / HostDrv)
  で名前か祖先が pinned の FD に当たれば `OS32_ERR_BUSY` (二重の防御)。

#### 名前の規則 — FAT / HostDrv (票 TASK_VFS_FD_PATH、実装レビュー ラリー 2・3)

BUSY / pinned (使用中の SQLite DB・loop イメージ) は、FD を開いた名前と操作の名前を 1 バイトずつ
(大文字小文字は `VfsOps.name_fold` で畳んで) 比べる。下の FS が「別の綴りを同じ実体」と読む名前は、
この比較をすり抜けて使用中の実体を消せるので、**入口で `OS32_ERR_INVAL` で断る**
(`fs/vfs_name_rules.inc`、FAT は `ff_make_path`、HostDrv は `hostdrv_create` と rename の宛先)。

| 断るもの | FAT | HostDrv (Win32) |
|---|---|---|
| `\`、制御文字 (0x01〜0x1F) | ○ | ○ |
| 要素の末尾の空白・末尾の `.` (`.` / `..` そのものは除く) | ○ | ○ |
| 要素の途中の空白 (FatFs はそこで名前を打ち切る) | ○ | 通す (`my file.txt` は正当) |
| `: * ? " < > \|` (`:` は代替データストリーム `disk.img::$DATA`) | FatFs 自身が断る | ○ |
| 最短形の正しい UTF-8 で BMP (U+0000〜U+FFFF、サロゲートを除く) の文字でないもの — 途中で切れた列 (`disk.img\xC2`)、単独の継続バイト、冗長な符号化 (`\xC1\xA4isk.img` = `disk.img`)、4 バイト列 (絵文字など BMP 外) | 見ない (0x80 以上は CP437 / SJIS のバイト) | ○ |

HostDrv の名前は `lib/kutf16.c` の `kutf8_to_utf16le` で UTF-16 にしてホストへ渡る。変換も同じ
規則で、不正な列・BMP 外・収まらない入力は置換も切り詰めもせずに失敗し (-1)、呼び手はホストへ
何も送らずに `INVAL` / `NAMETOOLONG` を返す。以前は途中終了・冗長形の受理・U+FFFD への置換で、
検査した名前と別の名前がホストに届いていた。**ホスト上の BMP 外の名前 (絵文字など) のファイルは
HostDrv から開けない** (一覧には CESU-8 風の名前で出るが、開く・消す・改名は `INVAL`)。

**既知の制限 (ユーザー決裁 2026-09-24)** — HostDrv 上のファイルを次の別名で指したときの BUSY /
pinned の保護は**保証しない**:

- Windows の **8.3 短名** (`disk-image.img` に対する `DISK-I~1.IMG`)
- **予約名** (`CON`、`NUL`、`COM1` など)
- **ASCII 以外の大文字小文字** (HostDrv の `name_fold` は ASCII の英字だけを畳む)

影響は**開発者のホスト上のファイルと、それを載せた loop デバイス**に限られる (実機に HostDrv は
無い)。HostDrv 上のイメージを loop に載せている間は、これらの別名で消したり改名したりしないこと。
将来 HostDrv を Host Services (ネットワーク越し) の経路でも使えるようにするときに、ホスト側の
実体識別で保護する案を検討する (v3 PLAN の候補)。

#### 排他的作成 `O_EXCL` (KAPI v53、票 H2)

`sys_open` の `KAPI_O_EXCL` (`0x0400`) は「**無いことを確かめて作る**」を
1 回の呼び出しの中で行う。

| 条件 | 戻り値 |
|---|---|
| `O_CREAT` と組で、名前が無い | 作って FD を返す (長さ 0) |
| 名前が既に在る (**種別を問わない**) | `OS32_ERR_EXIST` — ディレクトリでも `ISDIR` ではない |
| 在るかどうか**判定できない** | その負値をそのまま返す (「読めなかった」を「無い」と読み替えない) |
| `O_EXCL` 単独 (`O_CREAT` なし) | `OS32_ERR_INVAL` |
| `VfsOps.create_excl` を持たない FS | `OS32_ERR_NOSYS` |

**非対応 FS の判定は種別検査 (`vfs_path_kind`) と作成処理より先**に行う。後ろに
置くと、持っていない FS でも `EXIST` / `ISDIR` / I/O エラーが `NOSYS` より先に
返り、呼び手が「この FS では一時ファイル方式が使えない」と気づけない。

排他性の根拠は **VFS が非再入で、ゲストが協調型 (GetMessage 方式)** であること —
1 回の `sys_open` の中の「無いことの確認 → 作成」に他のゲスト処理は割り込まない。
**ホスト側が同時に書ける FS では成り立たない**ので、`create_excl` を実装して
いるのは ext2 だけ。HostDrv / FAT / ISO9660 は持たない (= `NOSYS`)。

`O_TRUNC` を併せて渡しても何も起きない (いま作った長さ 0 のファイルに切り詰める
ものが無い)。

#### 名前の置き換え (`rename`) の契約

宛先に**同じ名前の通常ファイル**があるとき、`vfs_rename` は POSIX と同じく
置き換える。ext2 の実装は **宛先の名前をどの段でも消さない** (票 H2 §2-2):

- 宛先ディレクトリエントリの **inode 番号をその場で書き換える** (「公開」)。
  書くのは**そのフィールドを含むセクタ 1 本だけ**。
- どの段で落ちても、**宛先の名前は旧 inode か新 inode のどちらかを指す**。
  名前が消えた瞬間は存在しない。
- 公開の前に落ちたら宛先は**旧内容のまま**。公開の後に落ちたら宛先は
  **新しい内容**で、後始末 (旧 inode の解放、移動元の名前の削除) の漏れが
  残ることがある (`e2fsck` が回収する)。
- 公開したかどうかが**判定できない**とき (書き込みも読み直しも失敗) は、
  マウントを書き込み禁止 (`OS32_ERR_ROFS`) にして**旧 inode を解放しない**。
  解放は戻せないが、漏れは回収できる。
- 戻り値は最後の `ext2_sync` の失敗も含む。`OS32_ERR_IO` は「公開したかも
  しれないし、していないかもしれない」を意味するので、呼び手は宛先の
  **`st_ino`** を読み直して確かめること (サイズや CRC は公開の証拠にならない)。

**ディレクトリ同士**の置き換えは行わない (`OS32_ERR_EXIST`)。通常ファイルと
ディレクトリが食い違う組み合わせも断る。通常ファイル以外 (特殊ファイル) 同士は
従来の unlink + add 経路のままで、上の保証は付かない。

**例外**: 上の経路に入る条件は dirent の `file_type` が**両側とも
`EXT2_FT_REG_FILE`** であること (`fs/ext2_dir.c`)。**FILETYPE 機能
(`s_feature_incompat` の 0x0002) を持たない ext2 では `file_type` が常に 0** な
ので条件が成立せず、**従来の経路 (宛先の名前を先に消す) に落ちる** — 途中で
落ちると宛先の名前が消えた瞬間が存在する。OS32 の `fs/ext2_fmt.c` は
FILETYPE を立てて作るので手元の NHD では起きないが、他所で作った媒体を
マウントするときは付かない保証だと思うこと。

`rename` は inode の `mtime` を変えない。一時ファイルへ `sys_set_mtime` して
から置き換えれば、本名に現れた時点で日時が揃う。

#### 予約された名前 — `.hs~`

**`.hs~` で始まる名前は `hsync` の予約**である。`hsync` は置き換えの一時
ファイルをこの接頭辞で作り、訪れたディレクトリで見つけた `.hs~*` の通常
ファイルを (`st_nlink` を見ずに) 消す。**利用者はこの接頭辞を自分のファイルに
使わないこと。** 詳細は `docs/manpages/hsync.1`。

コールバック方式の `sys_ls` では、共有スクラッチを使う FS 操作の再入に注意する。
ext2 は走査ブロックの私有バッファで対策済み。FatFs / HostDrv の監査状況と呼び出し側の
注意は [POLICY_DEBUG.md §4-26](POLICY_DEBUG.md) を参照。

### §6-2 ext2 ファイルシステム (ext2_super.c / ext2_inode.c / ext2_dir.c / ext2_file.c / ext2_fmt.c / ext2_vfs.c)

IDE HDD上のLinux ext2ファイルシステムを読み書きする。パーティション開始位置は PC-98 パーティションテーブル (LBA 1) を `ext2_find_partition()` で解釈して動的に決定する (現行 NHD イメージではシリンダ12 = LBA 1632 開始。詳細は [NHD_FORMAT.md](NHD_FORMAT.md))。マルチインスタンス方式 (Ext2Ctx) により、複数デバイスの同時ext2マウントが可能。ext2フォーマット (`ext2_format`) によるmkfs相当の機能も備える。

| 項目 | 値 |
|------|-----|
| ブロックサイズ | 1024バイト |
| inodeサイズ | 128バイト |
| inode数 | 動的 (ブロック数/4、最小 16。グループ数で均等割りし 8 の倍数へ切り上げ) |
| 対応ブロック | ダイレクト(12) + 間接(1) + 二重間接(1) |
| ルートinode | 2 (`EXT2_ROOT_INO`) |
| ブロックグループ | マルチグループ対応。1 グループ最大 8192 ブロック (`EXT2_BLOCKS_PER_GROUP_MAX`)、グループ数上限 32 (`EXT2_MAX_GROUPS`) |

**API**:

| 関数 | 説明 |
|------|------|
| `ext2_mount(drive)` | マウント |
| `ext2_format(drive, total_sectors)` | ext2フォーマット (mkfs相当) |
| `ext2_read_file(ino, buf, max)` | ファイル読込 |
| `ext2_write(ino, data, sz)` | ファイル上書き |
| `ext2_create(dir, name, data, sz)` | ファイル新規作成 |
| `ext2_unlink(dir, name)` | ファイル削除 |
| `ext2_mkdir(dir, name)` | ディレクトリ作成 |
| `ext2_rmdir(dir, name)` | ディレクトリ削除 |
| `ext2_list_dir(ino, cb, ctx)` | ディレクトリ一覧 |
| `ext2_lookup(path, out_ino)` | パス→inode解決 |
| `ext2_sync()` | メタデータ書き戻し |

### §6-3 IDE ドライバ (ide.c / ide.h)

ATA PIOモードによるIDE HDD制御。

| 項目 | 仕様 |
|------|------|
| ベースI/O | 0x640 (プライマリ) |
| 転送モード | PIO (セクタ単位) |
| セクタサイズ | 512バイト |
| アドレッシング | LBA28 |

**API**:

| 関数 | 説明 |
|------|------|
| `ide_init()` | IDE検出・初期化 |
| `ide_identify(drv, info)` | ドライブ情報取得 (IdeInfo構造体) |
| `ide_read_sector(drv, lba, buf)` | 1セクタ読込 |
| `ide_write_sector(drv, lba, buf)` | 1セクタ書込 |
| `ide_write_sectors(drv, lba, cnt, buf)` | 複数セクタ連続書込 |
| `ide_drive_present(drv)` | ドライブ存在チェック |

### §6-4 FDリダイレクト (fd_redirect.c / fd_redirect.h)

VFSのファイルディスクリプタ (FD 0=stdin, 1=stdout) に対して、出力先をファイルまたはメモリバッファに切り替える機構。シェルのリダイレクト (`>`, `>>`, `<`) およびパイプ (`|`) を実現する基盤。

| 関数 | 説明 |
|------|------|
| `fd_redirect_to_file(fd, path, mode)` | FDの入出力先をファイルにリダイレクト |
| `fd_redirect_to_buffer(fd, buf, size, len)` | FDの入出力先をメモリバッファにリダイレクト |
| `fd_redirect_reset(fd)` | リダイレクトを解除しコンソールに復帰 |
| `fd_is_redirected(fd)` | FDがリダイレクト中か判定 |
| `fd_redirect_get_buf_len(fd)` | バッファリダイレクト時の書き込み済みバイト数取得 |

### §6-5 パイプバッファ (pipe_buffer.c / pipe_buffer.h)

コマンド間のデータ受け渡しに使用するカーネル管理のメモリバッファプール。最大同時確保数は `PIPE_BUF_COUNT` (2)、各バッファサイズは `PIPE_BUF_SIZE` (64KB)。

| 関数 | 説明 |
|------|------|
| `pipe_alloc()` | パイプバッファを1個確保 (IDを返す) |
| `pipe_free(id)` | パイプバッファを解放 |
| `pipe_get_buf(id)` | パイプバッファのデータポインタ取得 |
| `pipe_get_len(id)` | 書き込み済みバイト数取得 |
| `pipe_clear(id)` | パイプバッファをクリア |

### §6-6 ISO 9660 ファイルシステム (iso9660.c / iso9660.h)

CD-ROM上のISO 9660 Level 1ファイルシステムを読み取り専用でVFS経由で提供する。ATAPIドライバ (§5-6) を介してセクタ読み出しを行う。

| 項目 | 値 |
|------|-----|
| セクタサイズ | 2048バイト |
| PVD位置 | LBA 16 |
| ファイル名 | Level 1 (8.3)、case-insensitive比較 |
| 拡張 | Rock Ridge / Joliet 非対応 |
| 書き込み | 非対応 (全write系操作はVFS_ERR_IO) |

**VFS操作**:

| 関数 | 説明 |
|------|------|
| `iso9660_mount(dev_id)` | PVD読み込み、Iso9660Ctx確保 |
| `iso9660_umount(ctx)` | コンテキスト解放 |
| `iso9660_list_dir(ctx, path, cb, user)` | ディレクトリ一覧 (コールバック) |
| `iso9660_read_file(ctx, path, buf, max)` | ファイル全体読み込み |
| `iso9660_read_stream(ctx, path, buf, sz, off)` | オフセット付き部分読み込み |
| `iso9660_get_file_size(ctx, path, size)` | ファイルサイズ取得 |
| `iso9660_stat(ctx, path, st)` | ファイル情報取得 |

**読みのキャッシュ** (実機の cdinst が 20KB/s を切った件、`tools/tests/cd_read_tdd.md`):

- **直前に解決したパス 1 本** (LBA・サイズ・フラグ) を覚える。VFS は読みのたびにパスを渡すので、以前は区切り
  (4KB) ごとに根からディレクトリを読み直し、データと離れたディレクトリのセクタへ毎回シークしていた
- **ディレクトリのセクタは 4 本の LRU** (`ISO_SCACHE_SLOTS`)。`list_dir` は cb を呼ぶ前にセクタを手元へ写す
  (cb がこの FS を読むとキャッシュが入れ替わる、§4-26)
- **ファイルのデータは先読みの窓** (`ISO_RA_SECTORS` = `ATAPI_READ_MAX_SECTORS` 本、mount で kmalloc した 32KB)
  を通す。窓より大きいセクタに揃った範囲は呼び手のバッファへ直接読む。窓はファイルの外へ広げない。
  窓が取れなければ窓なしで動く (遅いだけ)。窓の先読みが要求の外の不良セクタで落ちても、要求のセクタだけを
  読み直す (要求の中の不良だけが失敗)
- **CD を入れ替えたら umount / mount する (約束)**。捨てる合図は (1) umount (ctx ごと)、(2) ATAPI の媒体の世代
  (`atapi_media_gen`、UNIT ATTENTION / NOT READY で進む。読み・stat・get_file_size の途中で進んだら捨てて
  1 回だけやり直す。list_dir は一覧の途中で進んだら VFS_ERR_IO で中断)、
  (3) **最後に媒体を読んでから 2 秒 (`ISO_IDLE_TICKS` = 200 tick) を超えて空いたとき** (FD の §6-8 と同じ
  「2 秒規則」。キャッシュの当たりは時刻を進めない)。**NP21/W は READ(10) で UNIT ATTENTION を返さない**ので、
  エミュレータで効くのは (1) と (3) だけ。2 秒以内の入れ替えは保証しない

### §6-7 HostDrvFS (hostdrvfs.c / hostdrvfs.h)

NP21/WエミュレータのHostDrv機能を利用し、ホストPC (Windows) のファイルシステムにゲストOSから直接アクセスする仮想ファイルシステム。セッションベースのhypercall I/Oモデルで動作する。

| 項目 | 値 |
|------|-----|
| 通信方式 | NP21/W HostDrv hypercall (共有メモリ + I/Oポート) |
| マウントポイント | `/host` (カーネル自動マウント) |
| 対応操作 | READ, WRITE, LIST (ディレクトリ一覧) |
| 書き込み | 対応 (`hdrv_write_file` / `hdrv_write_stream`) |
| セッション管理 | CREATE → READ/WRITE/LIST → CLOSE のIRPシーケンス |
| 同期要件 | 通信バッファは `volatile` 宣言必須 (GCC最適化対策) |

**VFS操作**:

| 関数 | 説明 |
|------|------|
公開エントリは `hostdrvfs_init` / `hostdrvfs_detect` の 2 つだけで、VfsOps に
登録される実体は `hdrv_*` 接頭辞を持つ (`fs/hostdrvfs.c`)。

| 関数 | 説明 |
|------|------|
| `hostdrvfs_init()` | HostDrv検出・自動マウント |
| `hostdrvfs_detect()` | HostDrv の有無を判定 |
| `hdrv_mount / hdrv_umount / hdrv_is_mounted` | マウント制御 |
| `hdrv_read_file / hdrv_read_stream` | ファイル読み込み (全体/オフセット付き部分) |
| `hdrv_write_file / hdrv_write_stream` | ファイル書き込み (全体/部分) |
| `hdrv_list_dir` | ディレクトリ一覧 (コールバック) |
| `hdrv_get_file_size` / `hdrv_stat` | サイズ・ファイル情報取得 |
| `hdrv_unlink / hdrv_rename / hdrv_mkdir / hdrv_rmdir` | 削除・改名・ディレクトリ操作 |
| `hdrv_sync` | メタデータ書き戻し |
| `hdrv_block_size / hdrv_free_blocks / hdrv_total_blocks` | 容量問い合わせ |

### §6-8 FAT (FatFs)

- **fatfs/ + fatfs_vfs.c** — ELM FatFs (elm-chan.org) の移植 + VfsOps 統合ラッパー。ext2_vfs.c と同じマルチインスタンスパターン (FatFsCtx を kmalloc/kfree)。FDD ブート時のルートFSでもある (`root_fs = "fat"`)
- FatFs のボリュームは pdrv 0/1 (fd0/fd1) の2つ。`fatfs_vfs_mount` は pdrv 単位の busy フラグで二重マウントを弾く (fd1 の自動マウント試行が fd0 のマウントを壊す経路があったため)
- **FD の媒体を差し替えたら umount / mount する (契約)**。FD の読みはトラックの先読みとセクタキャッシュを
  持ち (`drivers/fdc_track.c`、票 [`archive/realhw_v21/TASK_FDC_REALHW.md`](archive/realhw_v21/TASK_FDC_REALHW.md))、FatFs も
  マウント中は FAT とディレクトリを覚えている。捨てる合図は (1) `disk_initialize` (= マウント)、(2) 書き込みと
  SIS で見た Ready 変化による世代、(3) **最後の読みから 2 秒 (200 tick) 空いたとき** (MS-DOS と同じ考え方の
  「2 秒規則」)。FRY=1 では同じ形式の媒体の差し替えが Ready 変化にならず (io_fdd.md の 0094h)、NP21/W も
  交換で IRQ を出さないので、**2 秒以内の差し替えと、FatFs が覚えている FAT・ディレクトリは保証しない**

> **自作 FAT12 ドライバ (`fs/fat12.c`, 1,152行) は廃止済み。** FatFs と二重実装で
> あるうえ、FAT を8セクタに切り詰める (1.44MB で読みはサイレント切断・書きは
> ボリューム破壊)、`fat12_vfs_write/unlink` が basename しか見ずサブディレクトリに
> 書けない、といった不具合を抱えていた。FatFs は LFN・FAT16/32・サブディレクトリ
> 書き込み・rename・mkdir に対応した上位互換であるため一本化した。
> ホスト側の `tools/mkfat12.py` はイメージ生成ツールとして残っている。

**用途**: `hsync` コマンドによる `/host` → `/` へのファイル同期 (HostDrvデプロイワークフロー) の基盤。

---

