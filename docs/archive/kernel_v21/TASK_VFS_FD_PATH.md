# TASK_VFS_FD_PATH — FD がパスを覚えて書き込みごとに引き直す / 長いパスを黙って切り詰める (VFS の既存欠陥)

> 状態: **受入完了 (2026-09-24)** — 実装レビュー ラリー 3 の修正 c7f1ba2 で閉じ、NP21/W で CD 新規インストール → e2fsck clean → HDD 起動 kselftest 210/210 (本文「実装レビュー ラリー 3」の末尾)。v2.1 に同梱。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-opus-5-5`、2026-09-24) / それまでの状態: **方針確定 (2026-09-24)** — ラリー 3: Opus 条件付き Approve、Codex は A-R3-1 のみ → ユーザー決裁 ①。実装中 — ユーザー指示「別票を着手」(2026-09-24)。**実装レビュー ラリー 3 の修正 (FAT の長いパス・FAT の rmdir・HostDrv の名前の厳密化・既知の制限の記載) を実装、レビュー待ち** (wt/vfs-fix4、記録 `tools/tests/vfs_fd_path_tdd.md` の「ラリー 3」)。カーネル層 (VFS/FS) の既知の欠陥なので POLICY_DEV §1 に沿って新機能より先に扱う。
> 出所: TASK_EXT2_EMPTY_NAME の修正 (fc5ce67) の実装レビュー (Codex / Opus とも Approve、どちらも「修正前からある別の欠陥」として挙げた)。

## 欠陥 1 (Codex、優先): 開いた FD の書き込みが、同じパスに作り直したディレクトリを上書きする

- 反例: `open("/hd0/f", O_CREAT|O_WRONLY)` → `unlink("/hd0/f")` → `mkdir("/hd0/f")` → 元の FD へ `write`。FD は inode ではなくパスを保持し、書き込みのたびに再解決する (`fs/vfs_fd.c:381` `vfs_write_fd`)。その先の `ext2_write_stream` (`fs/ext2_file.c:311`) に通常ファイルの型検査が無く、新しいディレクトリのブロックを任意データで上書きできる (空名項目も作れる)。
- 直し方の候補: (a) FD が inode 番号 (と世代) を持ち、書き込みは inode で行う。(b) 最低限、write_stream で通常ファイルでなければ ISDIR で断る + open 時の inode と再解決した inode が違えば IO/STALE で断る。

## 欠陥 2 (両者): 長いパス・深いパスを拒否せず切り詰める

- `vfs_resolve_path` (`fs/vfs.c:56`) は 256 バイトの一時領域へコピーし、超えた分を捨てる。`/` + `a`×254 + `X` と `…Y` が同じ名前に解決される → 書き込み・削除・rename が別の対象へ作用し得る。要素数が `VFS_MAX_PATH_DEPTH` (32) を超えると超えた分を捨てる (深い `mkdir -p` が EXIST で成功に見える)。
- `tools/mkpkg.py` はパスの形だけ検査し、消費側の `PKG_MAX_PATH` = 128 を見ない。`/hd0` 前置後の切り詰めで後のファイルが前を上書き、128 以上ではパーサの読み位置がずれる (`userland/lib/rt/pkg.c:164`、`userland/system/cdinst.c:284`)。
- 直し方: どれも**切り詰めずに断る** (NAMETOOLONG 相当)。mkpkg は UTF-8 バイト長で展開先の前置を含めて検査。

## そのほか (記録)

- マウント点への `open(O_CREAT|O_EXCL)` は EXIST でなく INVAL (契約 vfs.h:131 と不一致、到達する呼び手は無い)。
- `rmdir("/hd0/a/.")` は正規化で `a` 自体を消す (正規化前の検査が要るかは仕様判断)。

## 方針 (レビュー対象)

1. **FD の同一性**: `VfsOps` に任意の操作 `ident(ctx, path, u32 *id_hi, u32 *id_lo)` を追記する (ext2 は inode 番号 + inode の生成時刻 (ctime) か、世代が無ければ inode 番号と i_links/i_ctime の組)。`vfs_open` で FD に同一性を記録し、`vfs_read_fd` / `vfs_write_fd` / `vfs_fstat` / 切り詰めのたびに再解決した同一性と比べ、**違えば `OS32_ERR_STALE` (新設) で断る** (書かない)。`ident` を持たない FS (FAT / ISO / HostDrv) は従来どおり。
2. **型の検査を FS 側にも**: `ext2_write_stream` / `ext2_read_stream` / 切り詰めは、解決した inode が通常ファイルでなければ `ISDIR` で断る (1 が無い経路・将来の FS の最後の砦)。
3. **長いパスは切り詰めずに断る**: `vfs_resolve_path` は、正規化の前後どちらかで `OS32_MAX_PATH` (256、終端込み) を超える、または要素数が `VFS_MAX_PATH_DEPTH` (32) を超えるとき **`OS32_ERR_NAMETOOLONG` (新設)** を返し、呼び手 (VFS の全入口) はそれを返す。要素 1 つが 255 バイトを超えるのも同じ。
4. **パッケージ**: `tools/mkpkg.py` は UTF-8 **バイト長**で、展開先の前置 (`/hd0`) を含めて `PKG_MAX_PATH` (128) 未満を検査して断る。`userland/lib/rt/pkg.c` の `pkg_parse` はパスが上限以上なら**読み位置をずらさずに**エラーを返す (項目を読み飛ばす長さはヘッダの値で進める)。cdinst の前置は溢れたら断る。
5. **マウント点への `O_CREAT|O_EXCL`** は `EXIST` (vfs.h:131 の契約どおり)。
6. **末尾の `.` / `..`** (`rmdir("/a/.")` 等): POSIX と同じく、rmdir / rename / unlink の**最終要素が `.` または `..`** なら正規化の前に `INVAL` で断る。mkdir の `mkdir a/.` も INVAL (EXIST にしない)。
7. エラーコードの新設 (STALE / NAMETOOLONG) は `sdk/include/os32/os32_kapi_shared.h` の `OS32_ERR_*` に**追記** (KAPI のスロットは増えない)。シェル・hsync の表示を足す。
8. 受入: ホスト試験 (実物の vfs + ext2) で、欠陥 1 の反例 (open → unlink → mkdir → write が STALE で断られ、ディレクトリのブロックが変わらない、e2fsck -fn clean)、256/257 バイトと 32/33 要素の境界、mkpkg / pkg_parse の 127/128 バイト境界、O_EXCL の EXIST、`rmdir a/.` の INVAL。

## 方針 v2 (ラリー 1 を反映、上の方針 1〜8 を置き換える)

**同一性は ctime / links / generation に頼らない** (両者 A-1、Codex A-2: ext2 の時刻は定数 `0x67E8E800`、generation は常に 0、inode は最小番号から再利用、set_mtime / O_TRUNC で ctime が変わる)。代わりに **VFS 側の失効 (invalidation)**:
1. FD は open 時に (マウント, inode 番号) を記録する (ext2 に `ino_of(ctx, path, &ino)` を追記。FS が持たなければ記録しない)。
2. `vfs_rm` / `vfs_rmdir` / `vfs_rename` (上書きされる宛先と移動元の両方) は、操作の前に対象の inode を取り、**成功したら**同じ (マウント, inode) の開いた FD 全部に `stale` 印を付ける。`vfs_umount` は**そのマウントの FD 全部**に印を付ける (Codex A-3: 解放済みの fs_ctx を触らない — 印は fs_ctx を参照する前に見る)。
3. read / write / fstat / 切り詰め / seek は最初に印を見て、立っていれば `OS32_ERR_STALE` (**既存の -11** を使う) を返す。読み書きのたびの追加 lookup は無い (ext2 の ns_gen パスキャッシュもそのまま)。
4. `ext2_write_stream` / `read_stream` / 切り詰めは通常ファイル以外を `ISDIR` で断る (最後の砦)。
5. 恒久策 (inode を保持し、最後の FD が閉じるまで unlink した実体を解放しない = POSIX) は別票。今回の保証は「ext2 で、名前空間の変更後に古い FD が別の実体へ書かない」。FAT / HostDrv は従来どおり (ISO は書き込み対象外)。
6. **常駐 FD の持ち主** (Opus A-2): IME 辞書 (`kernel/ime_dict.c`) は STALE / IO を受けたら閉じて開き直す (1 回、失敗なら辞書無しで動く)。loop_dev と cfg_backend の長く開いた接続は同じ扱いを入れるか、置き換え時に閉じる — 実装で全持ち主を列挙して報告。
7. **newlib の境界** (Codex A-4): `sdk/crt/syscalls.c` の `_write` / `_read` / `_open` / `_close` / `_lseek` / `_fstat` は OS32 の負値を **-1 + errno** に変換する (STALE → ESTALE、NAMETOOLONG → ENAMETOOLONG、ISDIR → EISDIR、ほか EIO)。SDK の変更なので `make external` の対象。

**長いパス** (両者):
8. `vfs_resolve_path` は**コピーの前に**長さを検査し (NUL 抜き 255 バイトまで)、要素の積み上げで表 (`VFS_MAX_PATH_DEPTH` 32) が満杯になった**時点で**断る (正規化後の数ではない)。戻り値を int にし、全呼び手 (vfs.c 14 か所、vfs_fd.c、kapi_db.c、ホスト試験の写し) を直す。`exec/exec.c:1430` の `"/usr/bin/"+path` の切り詰めも範囲に入れる。`OS32_ERR_NAMETOOLONG` は **-16** に新設し、ネットワークの予約 (-16 以降) を 1 つ進めて `os32_kapi_shared.h` と KAPI_SPEC §3-2 を更新。
9. **パッケージ**: mkpkg は UTF-8 バイト長で `/hd0` 前置込み NUL 抜き 127 以下 (格納パス 123 以下)、項目数 ≤ `PKG_MAX_ENTRIES` (128) を検査し、255 での黙った切り詰めを廃止。`pkg_parse` は過長項目・項目数超過で**即エラー** (読み飛ばさない)。`pkg_extract` は sys_open / sys_write の失敗を返し、cdinst は失敗を表示して止める。
10. **末尾の `.` / `..`**: 末尾の `/` を落とした後の最終要素が `.` / `..` なら、rmdir / rename (両引数) / unlink / mkdir を正規化前に INVAL。
11. **マウント点の O_EXCL**: NOSYS の判定の後に EXIST。
12. 受入 (追加): 通常ファイルへの inode 再利用 (open → unlink → 同名 create → 旧 FD write が STALE、新ファイル不変)、ディレクトリへの作り直し、set_mtime / O_TRUNC 後も同じ FD は STALE にならない、umount 後の旧 FD、read / fstat、相対パス + cwd、255/256 と 32/33 の境界、mkpkg 123/124 と 128/129 項目、newlib の write が -1 + errno、IME 辞書の置き換え後に FEP が動く (NP21/W)。

## 方針 v3 (ラリー 2 を反映、v2 の 1〜7 を置き換える。8〜12 は下の追記つきで残す)

**ext2 の FD は記録した inode で読み書きする** (Opus 案 (i)、Codex A-R2-1 親ディレクトリの rename を塞ぐ):
1. ext2 に `open_ino(ctx, path, &ino)` / `read_ino` / `write_ino` / `stat_ino` / `truncate_ino` を追記 (内部はすでに inode で動く: `ext2_read_stream(ctx, ino, …)`)。`vfs_open` は ext2 なら inode を取って FD に記録し、以後の read / write / fstat / 切り詰めは**パスを引き直さず inode で行う** (rename・親の rename の後も FD は同じ実体を指す = POSIX と同じ)。**inode が取れなければ open を失敗させる** (Codex A-R2-3 / Opus A2-2)。ext2 以外 (FAT / HostDrv) は従来どおりパスで (今回の保証は ext2 だけ、と明記)。
2. **失効の印は inode が解放されうる操作だけ**: `vfs_rm` (unlink) と `vfs_rename` の**置き換えられる宛先**、`vfs_rmdir` (ディレクトリは開けないので実質なし)。対象の inode は操作の前に取り、**取得が NOTFOUND 以外で失敗したら操作をしない** (rename の宛先の NOTFOUND は正常)。**印は操作の成否を問わず付ける** (Codex A-R2-2: 公開後の失敗でも旧実体は解放されうる。余分に STALE にするほうへ倒す)。`vfs_umount` はそのマウントの全 FD (FAT / HostDrv 含む) に印を付け、fs_ctx の解放**より前**に済ませる。
3. 読み書きは最初に印を見て、立っていれば `OS32_ERR_STALE` (既存 -11)。close は印付きでも解放できる (`vfs_close_sqlite` も)。
4. `ext2_write_stream` / `read_stream` / 切り詰めは通常ファイル以外を `ISDIR` で断る (最後の砦)。
5. ハードリンク (ホストで作った媒体) では、1 つの名前の unlink で同じ inode の他の FD まで STALE になる (余分に断る側、許容)。
6. **常駐 FD**: IME 辞書は STALE / IO で**SQLite 接続ごと**開き直す (ステートメント 3 本の finalize → `sqlite3_close` → open → prepare → `dict_fd_protect`、1 回だけ、hot journal が残っていないことを確かめる、失敗なら辞書無し)。cfg の常駐接続も同じ。loop_dev は**使用中のイメージの置き換え・削除を断る** (幾何や D88 索引を持つので開き直しでは続けられない)。inode 方式では rename だけなら常駐 FD は生きたまま。
7. **newlib**: `_write` / `_read` / `_open` / `_lseek` / `_fstat` / **`_unlink` / `_stat`** を -1 + errno に (Codex A-R2-6 / Opus)。`_close` は void のまま (ABI を変えない)。既存の NOTFOUND / EXIST / NOSPC 等も errno に対応させる。

**長いパス (v2 の 8 に追記)** — Codex A-R2-4: resolver の前でコピーする入口も直す: exec のコマンド名抽出 (`exec/exec.c:1370`)、`kapi_db_open` の固定長コピー (`kapi/kapi_db.c:507`)、`vfs_mount` の prefix 登録 (`fs/vfs.c:299`)。相対パスは cwd + "/" + 入力の合計で判定。32 要素は受理し 33 個目で断る。
**パッケージ (v2 の 9 に追記)** — Codex A-R2-5: **cdinst は前置 (`/hd0`) 後に溢れる項目があれば展開を始める前に断る** (外部で作られた PKG への消費側の保護)。`pkg_parse` の項目数超過は 128 項目の後の終端を見て検出。
**範囲外 (明記)**: マウント中の装置への `ext2_format`、FAT / HostDrv の FD 同一性、unlink した実体を最後の close まで保持する完全な POSIX 意味論。
**受入 (v2 の 12 に追加)**: 親ディレクトリの rename 後も旧 FD が同じ実体を読み書きする (新しい同名ファイルは不変)、置き換え rename の公開後失敗 (注入) で旧宛先 FD が STALE、inode 取得の失敗注入で open / unlink が断る、ハードリンク、IME 辞書の置き換え後に FEP が動く、長い DB 名・長い mount prefix・長いコマンド名が断られる、cdinst が 124〜127 バイトの格納パスを断る。

## ラリー 3 とユーザー決裁 (2026-09-24)

- **A-R3-1 (Codex、SQLite のジャーナルは開いた時の名前を覚える) → 決裁 ①**: **開いている SQLite DB (`vfs_open_sqlite` の FD) と、そのジャーナル (`<名前>-journal`)、およびそれらの祖先ディレクトリの rename を `OS32_ERR_BUSY` 相当で断る** (既存のコードが無ければ新設、-16 の NAMETOOLONG の次)。
- 実装で守ること (Opus / Codex の実装メモ): 利用中による拒否・INVAL・NAMETOOLONG は**印を付ける前**に判定し、印は FS の操作に入った場合だけ / `rename(f,f)` と同じ inode のハードリンク間は印を付けない / 失効の判定は read・write・fstat・truncate・seek の全部、umount 後の fstat が解放済み ctx を触らない / 常駐 SQLite の開き直しで hot journal が見つかったら**開かない** (辞書無し / cfg 無効、画面に出す)、開き直しは失効 1 回につき 1 回 / O_CREAT で作った後に inode が取れなければ作ったファイルを消して失敗 / `_close` は int のまま (void なのは KAPI の sys_close) / `sys_switch_shell` (`kernel/gui.c:130`) の切り詰めも長さ検査の入口に入れる / cwd が長いときは大きい一時バッファで正規化してから結果を判定。

## 実装レビュー ラリー 1 の修正 (2026-09-24、Codex / Opus の Request changes)

- **cdinst**: MINIMAL 以外 (NORMAL / FULL / DEBUG / APPEND) の展開の失敗でも止め、`Installation Complete` を出さない。同期 (`vfs_sync`) の失敗も完了にしない (`install_packages`)。
- **SQLite のファイルメソッドの失効検査** (Codex B2): read / write / truncate / sync / サイズ / lock / unlock / CheckReservedLock / FileControl / SectorSize は失効した FD で SQLITE_IOERR 系 (`file_live`)。close だけは失効後も通り、FD を解放できる。失効は group の sticky エラーに数えない。
- **失効後も BUSY** (Codex B3): 開いている SQLite の FD は失効 (unlink) しても**接続が閉じるまで** DB・ジャーナル・祖先の rename を BUSY で断る (umount の失効は fs_ctx を外すので対象外)。
- **名前比較は FS の規則** (Codex B4): `VfsOps.name_fold` (任意、NULL = バイトで区別) を追記。FAT は FatFs と同じ規則 (英小文字 → 大文字、0x80 以上は CP437 の大文字化表 — ff.c の `TBL_CT437` の写しで、ホスト試験が本文と突き合わせる)、HostDrv / ISO9660 は ASCII の英字だけ、ext2 は区別する。BUSY と pinned の名前比較がこれに従う。
- **IME の hot journal の順序** (Codex B1): 旧接続に SQL を流す**前**に失効を確かめ (`dict_ready`)、失効していたら step をせずに閉じ、journal を stat してから開き直す (中身のある journal があれば開かずに辞書無し)。
- **IME の管理 API** (Codex B5): ユーザー辞書の一覧・削除・書き出し・全消去も検索・学習と同じ回復経路 (失効の事前確認 + I/O エラーで 1 回だけ開き直し)。書き出しは `sqlite3_step` の異常終了を EOF と区別して失敗 (-4)、書き込みの失敗も -5。一覧の途中の失敗は -4 (部分的な一覧を成功にしない)。
- **長すぎる DB 名** (Opus nb2): `<名前>-journal` が 255 バイトを超える名前は、解決前・解決後のどちらで超えても**開く時点で**断る (`kapi_db_open` は CANTOPEN + `path too long (NAMETOOLONG)`、SQLite VFS の xOpen も MAIN_DB で CANTOPEN)。
- **hsync と開いている辞書** (Opus nb1): FEP を有効にした後の `/db/fep.db` の置き換えは BUSY になる (決裁 ① どおり)。hsync は `reason=replace_failed err=-17 (BUSY)` に「使用中で置き換えられなかった: 旧内容のまま」と次の手 (再起動して FEP を有効にする前に hsync) を出す。
- **受入の読み替え**: 「IME 辞書の置き換え後に FEP が動く」は、**`rm /db/fep.db` → 作り直し (コピー) → FEP で変換**の手順で見る。hsync の差し替え (置き換え rename) は辞書を開いている間は BUSY で断られるので、失効 → 開き直しの経路には入らない。

## 実装レビュー ラリー 3 とユーザー決裁 (2026-09-24)

- **FAT は直す** (ユーザー): (1) FD 起動で FAT が `/` のとき `ff_make_path` が `0:` を前に付けて末尾を切り詰める (Codex / Opus 一致) → 収まらなければ NAMETOOLONG。(2) FAT の `rmdir` (実体は `f_unlink`) がファイルも消し pinned を通らない (Opus) → 種別を見て NOTDIR。
- **HostDrv** (ユーザー: 「ホストドライブはホストサービス実装後に使える経路にしたい」): 名前の厳密化を今入れる — `VFS_NAME_RULE_WIN32` で最短形の正しい BMP の UTF-8 以外と Win32 の禁止文字 `: * ? " < > |` を断る (`kutf8_to_utf16le` の途中終了・冗長符号化・U+FFFD 置換による別名を塞ぐ、`:` の代替データストリームもここで閉じる)。**将来: HostDrv を Host Services (ネットワーク越し) の経路でも使えるようにする** (v3 PLAN の候補へ)。
- **8.3 短名 (`DISK-I~1.IMG`)・予約名 (CON)・ASCII 以外の大文字小文字 → 既知の制限** (ユーザー決裁 = 推奨): HostDrv 上のファイルを別名で指したときの BUSY / pinned の保護は保証しない。影響は開発者のホスト上のファイルとそれを載せた loop に限られる。docs/06_filesystem.md にも書く。

**2026-09-24 状態: 実装済み (c7f1ba2)。実装レビュー 3 ラリー + ユーザー決裁で閉じた。NP21/W で CD 新規インストール → e2fsck clean → HDD 起動 kselftest 210/210。FAT 媒体上の BUSY・IME 辞書の rm → 作り直し → 変換は NP21/W / 実機で未確認。**

## NP21/W での未確認項目の確認 (2026-09-24、ユーザー指示「未確認のチェック」)

- **IME 辞書の rm → 作り直し → 変換: 合格**。v63 の HDD 起動で `ime on` (辞書を開く) → `mv /db/fep.db /db/fep_moved.db` は **`Device or resource busy`** (開いている SQLite DB の rename は BUSY、決裁 ①) → `cp /db/fep.db /tmp/fep.bak` → `rm /db/fep.db` → `cp /tmp/fep.bak /db/fep.db` → `ime user list` がエラー無し → rshell を抜けてキー入力 `kanji` + 変換 → **`換字 (01/32)`** = 作り直した辞書から引けた (失効 → 接続ごと開き直し)。
- **FAT の名前の規則: 一部合格**。FD 起動 (FAT がルート) で `rm /bin/sleep.bin.` が **INVAL**。loop に載せて固定する試験は、`.bin` を losetup が `unsupported format` で断り、1.2MB の FD に loop 用のイメージを置く空きも無いので実地では未確認 (大文字違いで BUSY になることは、実物の FatFs + fatfs_vfs を RAM の FAT12 に載せたホスト試験 fatname 段で確認済み)。
- 観察: **NP21/W は FD を引数で渡すと FD から起動する** (HDD が起動可能でも)。以前の「FD が入っていても HDD から起動した」は、その回は FD が入っていなかったための誤認。
