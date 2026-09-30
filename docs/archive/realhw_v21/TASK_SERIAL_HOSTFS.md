# TASK_SERIAL_HOSTFS — 実機でシリアル越しに `/host` をマウントし、`hsync` でカーネルとユーザーランドを更新する

> 状態: **受入完了 (2026-09-29)** — 部品 A・B (KAPI v66)・`hsync --root` が着地、2026-09-29 に実機で HDD 起動のまま 135b6b5 → 44bd0fe を更新 ([RELEASE_v2.1](../../RELEASE_v2.1.md) §1、os32 リポジトリの `docs/archive/realhw_v21/CHECKLIST_2026-09-26.md` の実測節)。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-opus-5-5`、2026-09-23) / それまでの状態: **方針確定 (2026-09-24、ユーザー決裁)** — 3 ラリーで閉じなかった 5 点は下の「ユーザー決裁」で閉じた。実装は HDD インストール (TASK_HDD_INSTALL) の段 1/2 と合わせて順序を決める。
> **ユーザー決裁の変更 (2026-09-23)**: ラリー 1 で「SQLite はカーネルと同じ ELF にリンクされ、カーネル関数を 400 か所以上直接呼ぶ = 分離しても独立更新できない」と判明 → **SQLite の分離は取り下げ、1 ファイルのまま LZ4 高圧縮だけ採用** (選択肢 1)。
> ユーザー決裁 (2026-09-23): 「SQLite の分離と LZ4 高圧縮の両方採用」「昔のシリアル経由のホストドライブ (SerialFS) を復活させる方向で」。
> 前提の票: [TASK_HDD_INSTALL.md](TASK_HDD_INSTALL.md) (HDD に入れた後の更新手段)、[TASK_SERIAL_VFAST.md](TASK_SERIAL_VFAST.md) (115200)、CI の配備ツリー (docs/08_build.md §8-6)。

## 0. 事実 (PM が確認)

| # | 事実 | 根拠 |
|---|---|---|
| S1 | 実機でファイルを入れる手段は**無い**。`recv host:` / `push` は `/host` (= NP21/W の HostDrv) に読み替えるだけ、`hotdeploy` のバッファは NP21/W のデバッグ API がメモリへ書く、`hsync` は `/host` が前提 | userland/shell/rshell.c:525〜545, 467〜520、userland/system/hsync.c:7〜17 |
| S2 | **SerialFS** が 2026-04 まであった: VFS ドライバ `fs/serialfs.c` (534 行、`CONFIG_SERIALFS`)、RPC (ENQ + `SF` + cmd、CRC16、LZSS)、コマンド OPEN/READ/WRITE/CLOSE/LS/MKDIR/RMDIR/UNLINK/RENAME/GETSIZE/READ_STREAM/WRITE_STREAM、ホスト側は `tools/os32_server.py` の RPC ハンドラ。**8896d38 (2026-04-23) で「HostDrvFS が完全に代替」として削除**。`git show 8896d38^:fs/serialfs.c` で読める | git |
| S3 | 旧 SerialFS の時間切れは**ループ回数** (`timeout_ms * 10000`、「エミュレータの高速実行対策」)。実機で動いたことは無い。mtime を返す操作が無い (hsync はサイズと日時で差分を決める、stat の mtime が要る) | 8896d38^:fs/serialfs.c:23〜37、fs/vfs.h:116〜122 |
| S4 | 削除の後に、同じシリアル線へ **rshell** (1 行送信 → 応答 + EOT、`rshell_active` 中は console 出力をシリアルへ複写 kernel/console.c:247/283/358) と **V-FAST 115200 の切替 + 番犬** (`serial ack`) が入った。rshell の中から `hsync` を走らせると、RPC のフレームと console の複写が同じ線に混ざる | kernel/console.c、userland/shell/rshell.c |
| S5 | `vmkernel.lz4` = VK32 ヘッダ + 区間 (kernel.bin → 0x100000、sqlite.bin → **0x200000**) を**高速モードの LZ4** で詰めた 1 ファイル、507,949B。HDD ローダの上限 `MAX_IMAGE_SIZE` 508KiB (残り約 12KB)。FD ローダは FAT12 の `VMKRNL.LZ4` を 0x10000 から読む | tools/mkvmkernel.py、boot/boot_defs.h:45〜49、boot/boot_main.c:68〜90、boot/loader_fat_new.asm:100〜281 |
| S6 | 実測 (PM、2026-09-23): kernel.bin 281,856 → LZ4 高速 約 201KB / **高圧縮 171,894**、sqlite.bin 374,840 → 高速 約 306KB / **高圧縮 257,980**。展開側 (`lib/lz4.c`) はどちらのモードでも同じ | python lz4.block |
| S7 | 115200 の出力は 3.9 KB/s (ゲストの出力経路が上限)。ゲストの**受信**速度は未測定 | TASK_SERIAL_VFAST |


## 1. 方針 (レビュー対象)

### 部品 A — カーネル一式を小さくする (ユーザー採用 2 点)

1. `tools/mkvmkernel.py` を **LZ4 高圧縮** (`mode='high_compression'`, level は実測で決める) にする。展開側は無変更。
2. **SQLite を別ファイルに分ける**: `/boot/vmkernel.lz4` (kernel だけ) と `/boot/sqlite.lz4` (sqlite だけ、同じ VK32 形式)。HDD ローダ (`boot_main.c`) と FD ローダ (`loader_fat_new.asm`、FAT 名 `SQLITE.LZ4`) が 2 つを読んで展開する。**読み込み域** (0x10000〜) を 2 ファイルでどう使うか (順に読んで展開、または別の番地) を決め、`MAX_IMAGE_SIZE` の検査をファイルごとに持つ。片方が無い・壊れているときは画面に出して止まる。インストーラ (cdinst/install)・`nhd_deploy.py`・`deploy.yaml`・FD イメージ (`build/image.mk`)・CI の成果物に `sqlite.lz4` を足す。
3. 見込み: 普段のカーネル更新は約 170KB。

### 部品 B — シリアル越しの `/host` (SerialFS の復活)

4. **VFS ドライバ** `fs/serialfs.c` を旧版から起こし直す。`/host` の既定は従来どおり HostDrv (NP21/W で `hostdrvfs_detect()` が真)。**HostDrv が無いとき**だけ、`mount /host serial serialfs` (明示のコマンド) で SerialFS を `/host` に付ける。自動マウントはしない (シリアルの相手が居るとは限らない)。
5. **プロトコル**: 旧 RPC を土台に、(a) **時間切れは tick** (TASK_HAL_WIRING の時計、`sys_time_now` か tick)、(b) **stat (サイズ + mtime、UNIX 秒 UTC)** と **LS に mtime を載せる**、(c) CRC は **CRC32** (hsync と同じ) に上げる、(d) 1 要求 = 1 応答、要求ごとに通し番号、応答の番号違い・CRC 違いは再送 1 回 → 失敗で `OS32_ERR_IO`、(e) 圧縮 (LZSS) は**付けない** (vmkernel は圧縮済み、実測で要否を決める)、(f) 1 フレームの最大長を決める (ゲストのバッファと 16 バイト FIFO の取りこぼしを考える)。
6. **シリアル線の共有 (rshell との関係)**: SerialFS の要求中は、ゲストの console → シリアルの複写を**止める** (フレームの途中に文字が混ざらない)。ホスト側は 1 本のプログラム (`tools/rshell_serial.py` を拡張、または `tools/serialfs_host.py` を新設して rshell_serial から使う) が、rshell の応答 (EOT 区切りの文字) と SerialFS のフレーム (ENQ で始まり長さを持つ) を**同じポートで振り分ける**。rshell の EOT 約束と `serial ack` の番犬は変えない。
7. **ホスト側サーバ**: ノートで CI の artifact (`hostdrv.tar.gz`) を展開したディレクトリを `/host` として出す。書き込み (push、hsync のマニフェスト) は許すがルートの外は断る (`..` と絶対パス)。
8. **使い方**: ノートで `rshell_serial.py --fast 115200 --serve-host <dir> repl` → ゲストで `mount /host serial serialfs` → `hsync` (必要なら `hsync sys`、`hsync boot`) → 再起動。
9. **安全**: `/boot/vmkernel.lz4` と `/boot/sqlite.lz4` は hsync の一時名 → 検証 → rename (既存、§4-36) で置き換わる。さらに**置き換える前に旧版を `/boot/*.old` に残す** (hsync の `/boot` 専用の前処理)。起動しなくなったら FD で起動して `/hd0/boot/*.old` を戻す (手順を文書に)。ローダ・IPL の生セクタは hsync では**書かない**。

## 1-v2. ラリー 1 による改訂 — §1 の該当項をこの節で置き換える

**部品 A (1 ファイル + 高圧縮 + 壊れの検出)**
- A-1 はそのまま (mkvmkernel を高圧縮、level は 9 と 12 を実測)。**A-2 (SQLite の分離) は取り下げ**。見込み 約 430KB、上限 508KiB まで約 90KB。
  - **A-1 は先行して着地 (2026-09-24、wt/lz4-hc)**: HC level 12 を採用。`vmkernel.lz4` 516,731B → **437,169B** (level 9 は 438,072B)、上限 520,192B まで残り 83,023B。mkvmkernel は合計が `MAX_IMAGE_SIZE` (boot/boot_defs.h から読む) を超えると出力を消して失敗する (N8 の生成側)。展開側 3 実装 (lz4_mini.c・lib/lz4.c・FD ローダの ASM) の一致はホスト試験 `make check-vmkernel-lz4-host` ([記録](../../../tools/tests/vmkernel_lz4_tdd.md))。NP21/W・実機の起動確認は残件。
- **A-4 は実装済み (2026-09-24、wt/vk32-crc、手元ビルドとホスト試験のみ)**: VK32 v2 (エントリごとの展開後 CRC32・`image_size`・ファイル全体の `image_crc`、header_size = 16 + 20n + 8、形式の正典は `boot/boot_defs.h`)。HDD ローダ (`boot/vk32_boot.c`) と FD ローダ (`pm_vk32_boot`) が同じ順序・同じ `VK32_ERR_*` で検査し、外れたら画面に出して止まる。FD は **CRC32 も検査する** (nibble 表 64B)、LZ4 のリテラル / マッチに入出力の境界検査、全出口を `ebp` から戻す (旧 `.lz4_err` のスタック崩れを解消)、読む前に FAT チェーン全体を検査 (長さ・早期終端・範囲外・循環)。ローダは起動したイメージの CRC をブート情報域 v2 (0x30〜0x3F) に残し、KAPI v65 `boot_image_info` と `ver` / 起動画面の `Image CRC:` が見せる。`ver` と起動画面には `Commit:` (ビルドした git のコミット ID) も出す。**v1 のイメージはどちらのローダも断る** (ユーザー決裁 4: 旧ローダ互換なし)。試験は `make check-vk32-crc-host` / `check-build-id-host` ([記録](../../../tools/tests/vk32_crc_tdd.md))。NP21/W・実機の起動と壊したイメージで止まることの確認は残件。
- **A-4 壊れの検出** (両者): VK32 に **CRC32 表を追加**する。既存のエントリ (16B × n) の後ろに `entry_count` 個の CRC32 (展開後のデータに対して) と、ファイル全体の CRC32 を置き、`header_size` を増やす。**旧ローダとの互換**: 旧ローダがエントリを `16 + i*16` の固定位置で読み、`data_offset` を明示で使うなら、表の追加は旧ローダに見えない (実装者がコードで確認して報告。互換にならないなら止めて報告)。新ローダ (HDD `boot_main.c` + `lz4_mini.c`、FD `loader_fat_new.asm` の ASM デコーダ) は: 完全長 (ext2 の切り詰めは段 0 で止まる形に済)、`entry_count ≥ 1`、各エントリの `data_offset + comp_size ≤ ファイル長`、展開先が許可範囲 (0x100000〜の帯) に収まる、**`decoded == raw_size`**、CRC32 一致、を検査し、外れたら画面に出して止まる。FD の ASM デコーダが CRC32 を持つのが重ければ、FD は長さと範囲の検査だけにし CRC は HDD ローダだけ (FD は媒体ごと書き直す運用なので) — 実装者の判断を報告。

**部品 B (SerialFS)**
- **B-4' マウント**: `vfs_dev_parse` が受ける名前に合わせて **`mount /host COM1 serialfs`**。`/host` が既にマウントされていれば断る (vfs_mount が同じ prefix の二重登録を断るよう直す)。`rshell.c` の `serial N` に残っている `sys_mount("/host", "COM1", "serialfs")` (旧版の名残) は**削除**。自動マウントはしない。
- **B-6' 線の共有 = 遠隔から入れる排他モード** (Codex の「排他」と Opus の「遠隔操作が要る」の両立): SerialFS の要求は **SerialFS セッションの中でだけ**出る。セッションはマウント時に始まりアンマウント (または rshell の `sfs end`、時間切れの連続) で終わる。セッション中は、(a) ゲストの**送信の所有者ゲート** (`serial_putchar` の手前、console の複写・KAPI の serial_putchar/serial_puts・`ime_dict` の serial_puts・`isr_handlers` の polled 出力をすべて通す) が SerialFS 以外の送信を**捨てる**、(b) rshell・`kbd.c` の 4 か所 (rshell_active 中にシリアルを読む) はシリアルを読まない、(c) ホスト側は線を**占有**して SerialFS の要求に答え続け、利用者の入力は送らない。セッションの終わりに rshell は EOT を 1 つ返し、ホストは rshell の会話に戻る。**使い方**: ホストで `rshell_serial.py --fast 115200 --serve-host <dir> cmd "mount /host COM1 serialfs && hsync boot && umount /host"` のように 1 行で投げ、ホストはその 1 行の完了 (セッション終了後の EOT) まで線を占有する。ゲートは ring3_fault_kill / exec_exit / rshell のループの全経路で下ろす。IF=0 のまま待たない (tick が進まない)。
- **B-5' プロトコル** (両者):
  - フレーム = `ENQ 'S' 'F'` + 種別 + 通し番号 (u16) + 長さ (u16) + ペイロード + **CRC32 (種別〜ペイロード全体)**。ペイロード上限 **512B** (応答が大きい READ / LS は分割し「続き」フラグと offset/cookie で継続)。
  - 受信は 1 つの受信器がフレーム全体 (長さ 0・エラー応答も) を消費して検証する (旧版の残留バイト問題を持ち込まない)。長さは確保・コピーの前に上限と照合。
  - **番号違い・CRC 違いのフレームは数えずに捨て**、期限まで待ち続ける。時間切れ (tick: 最初の 1 バイト 2s、バイト間 100ms、全体 = 長さ × 87µs × 2 + 余裕) なら同じ番号で**再送** (最大 3 回)、それでも駄目なら `OS32_ERR_IO`。時間切れの後は「線が 200ms 静かになるまで読み捨て」てから戻る。**連続 N 回 (既定 3) 失敗したら線を死んだと見なし**、アンマウントまで即座に IO を返す。
  - **ホストは (セッション, 番号) ごとに最後の応答を保存**し、同じ番号の再送には**操作を再実行せず保存した応答を返す** (RENAME / UNLINK / MKDIR / WRITE の二重実行を防ぐ)。
  - 圧縮 (LZSS) は付けない。
- **B-3' VFS 契約** (Opus Q3): `stat` (st_mode の種別、st_size、0 でない st_mtime)、`get_file_size` (ディレクトリは ISDIR)、`list_dir` (ファイルには NOTDIR、エントリの境界検査)、`read_stream` (要求より多く返さない、EOF 前に 0 を返さない)、`write_stream` / `write_file` (push 用)。「無い」は NOTFOUND、通信失敗は IO に分ける。LS に mtime は載せない (hsync は別に stat する)。set_mtime / create_excl は NOSYS でよい。
- **B-7' ホスト側サーバ**: 受信を常時読むスレッド、フレームには常に答える、コマンドの完了はフレームの外の EOT、時間切れは「進捗が無い時間」で数える、配信中は reset_input_buffer を使わない。ルートの封じ込めは **realpath** (symlink 経由の脱出を断る)、wire 上のパスは `/` 始まりの「ルート相対」でホストの絶対パスと区別。書き込みは push のパスだけ許す (hsync はマニフェストを**読むだけ**)。
- **B-9' `.old`** (両者): `/boot/vmkernel.lz4` を置き換える前に、**それが今動いているビルドと同じとき (`ver` の Build と VK32 のビルド時刻が一致) だけ** `/boot/vmkernel.old` を作る (一度も起動していない版で上書きしない)。作り方は複製 → 検証 → rename (hsync の既存の一時名の経路)、本名が存在しない時間を作らない。**戻す範囲**: カーネルだけ。同じ実行で `hsync sys` によって KAPI が上がっていたら、shell.bin 等も戻す必要がある旨を文書に。ローダの自動切り替えはこの票ではしない (FD 起動 → `/hd0/boot/vmkernel.old` を戻す手順を文書に)。ext2 は非ジャーナルなので、電源断での媒体破損までは保証しない。
- **性能**: CI のビルドは毎回全ファイルの mtime を変えるので、`hsync` の既定 (全体) はシリアル越しでほぼ全ファイルを読み比べる (数分〜十数分)。**当面は `hsync boot` / `hsync bin` と範囲を絞って使う**旨を文書に。ホストが CRC を返す操作は別票。
- **ISR の計数**: `drivers/serial.c` の ISR は OE / FE を数えずにリセットしている → 数えて `serial` コマンドに出す (実機の切り分け用)。

## 1-v3. ラリー 2 による改訂 — §1-v2 の該当項をこの節で置き換える

- **部品 B は実装済み (2026-09-25、wt/serialfs、手元ビルドとホスト試験のみ、KAPI v66)**: `sfs_begin` / `sfs_end` / `serial_diag`。形式の正典は `fs/sfs_proto.h` (ホストの写しは `tools/serialfs_host.py`)、VFS は `fs/serialfs.c`、要求 / 応答は `fs/sfs_client.c`、セッションは `fs/serialfs_session.c`、ゲートは `drivers/serial.c`、`sfs run` は `userland/shell/rshell.c`、ホストは `tools/rshell_serial.py --serve-host` (NP21/W は `--port aidebug:http://…:8025`)。**票からの 1 点の入れ替え**: 溜めた出力の LOG / EXIT フレームは (9) ゲートを下ろす**前に**送る (下ろした後は console の複写が同じ線に出てフレームの間に文字が挟まりうる)。試験は `make check-serialfs-host` ([記録](../../../tools/tests/serialfs_tdd.md)) と `check-hsync-h2-host` の `case_boot_old`。NP21/W (T2)・実機 (T3/T4) は残件。
  **レビュー往復 1 の修正 (2026-09-25、wt/serialfs)**: ホストの受信は別スレッドで常に回し応答の期限は**到着時刻**から数える (送る直前に BYE / EOT / 期限を見る)、`read(in_waiting or 1)` で要求ごとの 200ms 待ちを無くす、`.old` は image_crc 欄と公開する一時ファイル自身も検査、Windows の根の判定は要素単位 (commonpath)、`sfs run` は rshell が**行全体を**引き受ける (組込みの `sfs` は常に断る)、POSIX のホストは dir_fd + `O_NOFOLLOW` で symlink を一切辿らない (Windows は競合が残ると man に明記)、ゲートの上げ下げで UART / FIFO の残りも読み捨てる、REPL は行ごとに結果を初期化、ESC を含む行の拒否は行末まで、**ホストへの書き込みは既定で禁止 (`--allow-write`)**、HELLO が通らなかった回も隔離してから下ろす、EXIT の後に保留へ入った文字も生で流す、`--serve-host` 無しの `sfs run` は送らない、セッション中の `serial_getchar` は -1 (KAPI_SPEC)。セッション層の試験 `tools/tests/serialfs_session_host.c` を追加。
  **レビュー往復 2 (2026-09-25)**: パスの作りの書き込み許可を realpath で解決した実体にも当てる (リンク越しに `--allow-write` の外へ書けない)、openat の起点は起動時に `O_NOFOLLOW` で開いた `root_fd` に固定、ESC を含む行の拒否は沈黙では解けず行末まで (本体の Enter / ESC が回復の口)、受信スレッドは `cancel_read` で止めて終わりを確かめてから次の行へ、`pwrite` の一部書き込みはくり返す。

**セッションは 1 つのコマンドの中で閉じる** (両者 N1/N2)
- 新しい組込み **`sfs run <コマンド行>`** (常駐シェル。`sh` には入れない)。これだけが SerialFS のセッションを開ける。流れ: (1) `/host` が空いていることを確かめる (HostDrv や既存のマウントがあれば断る) → (2) 送受信のゲートを上げる → (3) **HELLO** (ホストが 32 ビットの乱数のセッション ID を返す) → (4) `/host` に SerialFS をマウント → (5) 子のコマンドを exec で走らせる → (6) 子がどう終わっても (通常 / 非ゼロ / fault kill) `sfs run` に戻る → (7) **BYE** → アンマウント (線が死んでいてもローカルの ctx は解放する) → (8) 受信の隔離 (下) → (9) ゲートを下ろす → (10) 溜めた出力を流す → (11) `sfs: exit=N` を 1 行 → rshell の行末 EOT。**exec_exit / ring3_fault_kill はゲートに触らない** (持ち主は `sfs run` だけ)。`mount /host COM1 serialfs` の単独実行は断る。`cmd_mount` は余分な引数を断るよう直す。
- rshell の外 (手元の CUI) で `sfs run` を打った場合も同じ流れ (rshell の EOT が無いだけ)。
- 使い方 (ホスト): `rshell_serial.py --fast 115200 --serve-host <dir> cmd "sfs run hsync boot"`。ホストは `--serve-host` のときだけ、**`sfs run` の行を送ってから行末 EOT までの間だけ**フレームを解釈する。それ以外の時間はフレームを解釈しない (セッション外の `cat` の本文で要求が動かない、Codex N7)。

**セッション中の出力は溜めて後で流す** (Opus N3、[V4])
- ゲートで捨てずに、カーネルのリング (例 8KB、`kernel/con_sink.c` の流儀) に溜め、(10) で流す。溢れた分は数えて `sfs: log dropped N bytes` を出す。`hsync` の結果行と終了コードが遠隔に必ず届く。

**送受信のゲートは呼び口で分ける** (Opus 実装メモを方針に格上げ)
- 送信: SerialFS だけが使う専用の送信口。ほかの送信 (console の複写、KAPI の serial_putchar/serial_puts、ime_dict、ISR の kprintf) はセッション中は上のリングへ。`serial_puts_polled` (パニック・例外) は**通す** (パニックならセッションは死んでいる)。
- 受信: セッション中は KAPI の `serial_trygetchar` / `serial_getchar` と kbd.c の 4 か所は -1 (持ち主は SerialFS の受信器だけ)。セッション中の `serial_init` (速度変更) は断る。SerialFS の待ちループは RxRDY を直接見て FIFO に残った末尾も汲み出す (FIFO モードで ISR が取り残す数バイト、NP21/W では出ない)。

**セッション ID・番号・応答キャッシュ** (Codex N4)
- すべてのフレームにセッション ID (HELLO で決めた 32 ビット) を載せる。違う ID のフレームは捨てる。
- 番号は u16 で**周回させない**: 65,535 要求に達したらその要求は IO で失敗させ、セッションは BYE で閉じる (`hsync boot` は数百要求)。
- ホストの応答キャッシュは**直前の 1 件だけ**、一致条件は (セッション ID, 番号, 要求フレームの CRC32)。HELLO で消す。ホストが再起動したらセッション ID を知らないので、未知の ID の要求にはエラーで答える → ゲストは IO → `sfs run` が終える (再実行は新しいセッションで)。

**終わった後の遅延応答を rshell に入れない** (両者 N3/N4)
- ホスト: 要求を受けてからゲストの 1 試行の期限 (下の式) を過ぎた応答は**送らない**。BYE を受けたか、`sfs run` の行末 EOT を見たら、以後そのセッションの応答を一切送らない。
- ゲスト: (8) の隔離 = ゲートを上げたまま受信を捨て、**500ms 静かになるまで**待つ。上限 5 秒。上限に達したら (相手が送り続けている)、**ゲートを下ろさず** `sfs: line not quiet, serial input disabled until 'serial ack'` を**画面に**出し、rshell はシリアル入力を捨て続ける。ホスト側は `rshell_serial.py sync` 相当で線を静めてから `serial ack` を送って復帰する (既存の番犬と同じ合図)。
- rshell: シリアルから来た ESC で rshell を閉じるのは**行の先頭の単独の ESC** のときだけにする (フレームの中の 0x1B で閉じない)。

**時間切れの式は今の速度から** (Opus N6、Codex 実装メモ)
- 1 バイトの時間 = `serial_get_status` の実効速度から (8N1 = 10 ビット)。最初の 1 バイト 2s、バイト間 100ms、全体 = バイト数 × バイト時間 × 2 + 500ms。番号違い・CRC 違いで全体の期限を延ばさない。IF=0 で呼ばれたら待たずに IO。

**`.old` の識別はカーネルイメージの CRC** (両者 N5)
- VK32 の**ファイル全体の CRC32** (CRC 欄自身を 0 として計算) を A-4 で持つ。ローダが起動時に、読んだファイルの CRC をブート情報域 (0x7E00、段 0 の構造体に欄を足し version を上げる) に書く。カーネルは起動したイメージの CRC を保存し、KAPI で見せる (追記)。`ver` にも出す。
- `hsync` は `/boot/vmkernel.lz4` を置き換える前に、その CRC と**起動したイメージの CRC** を比べ、一致したときだけ `.old` を作る (複製 → 検証 → rename)。一致しない (FD 起動・前回の更新後に未起動) ときは**断る** (`--no-backup` を明示したときだけ進む)。`/sys` の復旧はこの票では扱わず、**KAPI が上がる更新ではカーネルだけの復旧は成立しない**と文書に書く。

**FD ローダ** (両者 N6/Codex 4)
- **FD も CRC32 を検査する** (T2 と整合)。ASM デコーダのリテラル / マッチのコピーに入力と出力の境界検査を入れ、全エラー出口のスタックを正す (延長読みで入力終端に達したときの `.lz4_err` のスタック不整合)。`decoded == raw_size`。ローダは 1024B を超えて 2 クラスタになってよい — IPL がチェーンで読むことを 2HD / 1.44MB の両方で確かめる。FAT チェーンの循環・範囲外・早期終端を検査。

**その他**
- `vfs_mount` に同じ prefix の二重登録の拒否を足す (今は (ops, dev) だけ)。`vfs_mount` が cwd を `/` に戻す件は `sfs run` の中で元へ戻す。
- T3 は `sfs run hsync boot`、更新の証拠は `ver` の**イメージ CRC** の変化 (Build 時刻ではない)。
- T5 から「本文の EOT」は外す: セッション外の rshell は従来どおり生の EOT で行を区切るので、本文に 0x04 を含む出力は途中で切れる (既知の制約として文書に書く)。

## 2. レビュアーに見てほしいこと

- Q1 (v2 で取り下げ、参考): 部品 A の 2 ファイル化で、FD ローダ (実モード、FAT12、0x10000 からの連続読み) と HDD ローダ (ext2_mini) の読み込み域・上限・順序に破綻は無いか。1 ファイルのまま区間ごとに読む案と比べてどちらが安全か。
- Q2: SerialFS を rshell と同じ線で多重化する案 (§1-6) に到達可能な混線は無いか (console の複写を止める範囲、割り込み文脈の kprintf、番犬、EOT、ホストの振り分け)。rshell を抜けてから SerialFS を使う (排他) 案と比べてどちらが安全か。
- Q3: VFS の契約 (fs/vfs.h の VfsOps: stat / set_mtime / read_stream / write_stream / list_dir / rename) のうち、hsync が実際に使うものを SerialFS がすべて満たすか。満たせないものは何か。
- Q4: 時間切れ・再送・フレーム長の決め方。実機 115200 の 16 バイト FIFO で取りこぼさない条件。
- Q5: `/boot/*.old` と FD での復旧で、更新失敗から確実に戻れるか。

## 3. 受入 (案)

| ID | 内容 | 場 |
|---|---|---|
| T1 | ホスト試験: フレームの組み立て・CRC32・番号・再送・フレーム長の境界、ホストの振り分け (EOT とフレームの混在)、パスの `..` 拒否 | ホスト |
| T2 | NP21/W: 高圧縮 + CRC 表の vmkernel で FD 起動・NHD 起動・kselftest、**旧ローダでも新 vmkernel が起動する** (互換)、CRC を壊した vmkernel で両ローダが止まる。HostDrv を切った状態 (ini は変えない — HostDrv の検出を無効にする試験フラグで) で SerialFS を NP21/W のシリアル (名前付きパイプ or TCP) 越しに mount → `ls /host` → `hsync --verify` | NP21/W |
| T3 | 実機: 115200 で `sfs run hsync boot` (と `sfs run hsync bin`) → 再起動 → `ver` の**イメージ CRC** が変わる、`.old` が起動したイメージと一致 | 実機 (HDD インストール後) |
| T4 | 実機: 壊れたカーネルを入れて起動失敗 → FD 起動 → `.old` で復旧 | 実機 |
| T5 | ホスト + NP21/W: 障害注入 — 応答の喪失・遅延 (番号ずれ)・CRC 破損・ホストの停止 (線が死ぬ判定)・再送で副作用が二重にならない・セッション中に rshell へフレームが漏れない・`cat` した本文に ENQ/EOT があってもセッション外で混線しない | ホスト / NP21/W |

## 4. しないこと

ローダ・IPL の更新 (生セクタ)、自動マウント、SerialFS の圧縮、複数クライアント。

## ユーザー決裁 (2026-09-24) — ラリー 3 の残り 5 点

1. **異常終了後の復帰 → B**: 線が静まらなければ 5 秒で打ち切り、警告を出して通常に戻す (保留状態は設けない)。残る危険は「ESC は行頭の単独だけ」と「ホストは期限切れの応答を送らない」で抑える。
2. **溜めたログの排出 → A**: ログと最終結果 (`sfs: exit=N`) を長さ付きのフレームで運ぶ (生バイトで流さない)。
3. **本体キーボードからの `sfs run` → A**: 取り下げ。ホストから送った 1 行だけで使う。
4. **初回導入と古いローダ → A**。ユーザー: 「まだ実機でインストールしていないので古いローダーの事は考えなくて良い」。→ 旧ローダ互換の扱いは不要。**条件: 部品 A (VK32 の CRC 表 + ローダの CRC 記録 + LZ4 高圧縮) を実機への最初の HDD インストールより前に入れる**。先にインストールした場合は CD から入れ直す。
5. **カーネルを戻したときの `/sys` → C**: KAPI をまたぐ更新は復旧保証の対象外と明記する。

## 5. FD 起動から HDD を更新する (`hsync --root`、2026-09-26、ユーザー提案)

**状態**: 実装 (wt/hsync-root、`userland/system/hsync.c` の `--root`、ホスト試験 `check-hsync-h2-host` の `case_root`)。NP21/W・実機は未実施。

**なぜ要るか**: HDD にある既存のカーネル (v65) は SerialFS (KAPI v66 の `sfs_begin`) を持たない。HDD から起動したままでは `sfs run` が使えず (`sfs: kernel KAPI v65 < 66`)、新しいカーネルを HDD へ運べない。そこで**新しいカーネルの FD で起動**し、FD 起動で自動マウントされる `/hd0` (HDD) を宛先にして `hsync` を回す。`/` は FD なので、`--root` が無いと `dest_on_fd` で断る。

**`--root <根>` の門** (詳細は `docs/manpages/hsync.1`「同期先の根 (--root)」): `<根>` はマウントの根そのもの (`vfs_devname` が空でない)・デバイスが `fd` でない・FS が ext2 (根の `stat` の inode が `EXT2_ROOT_INO` = 2) のときだけ受ける。`/host` とその下・`/cd0`・`/hd0/bin`・HDD 起動の `/hd0` (マウントされない) は 1 件も書かずに断る。保護 (`/etc/settings.db` の一族)・予約名の掃除・マウントをまたがない規則・名札の照合・`vmkernel.old` は `<根>` からの相対で効く。**KAPI の門 (v53・名札の `kapi=`) は走っているカーネル (= FD の版) と比べる** — 書き込みをするのはそのカーネルなので。

### 手順 (実機、ノートは `rshell_serial.py`)

前提: FD と `hostdrv.tar.gz` は**同じ CI 成果物** (この変更を取り込んだ feat/gui 以降)。FD の hsync が `--root` を知らない版だと `Error: unknown option: --root` で止まる。

```bash
# ノート: 成果物を取り、配備元のディレクトリを作る (名札 .deploy/manifest.txt 入り)
tools/ci_fetch.sh --sha <SHA>
tar -xzf ./os32-ci/os32-feat-gui-<SHA>/hostdrv.tar.gz -C ./os32-ci/os32-feat-gui-<SHA>/
H=./os32-ci/os32-feat-gui-<SHA>/hostdrv
S="python3 tools/rshell_serial.py --port /dev/ttyUSB0 --fast 115200 --timeout 60 --serve-host $H"
```

| # | 誰が | やること | 見るもの |
|---|---|---|---|
| 1 | ユーザー | 新しい FD で起動 (HDD は繋いだまま) | `Commit: <SHA>`、`API: v66` 以上。`/hd0` がマウントされている (`ls /hd0/boot`) |
| 2 | ノート | `$S cmd "sfs run hsync -n --root /hd0 --no-backup boot"` | `DEPLOY build=<SHA>…`、`hsync: /host/boot -> /hd0/boot`、`PLAN /hd0/boot/vmkernel.lz4`。`reason=root_*` / `dest_on_fd` が出たら止まる |
| 3 | ノート | `$S cmd "sfs run hsync --root /hd0 --no-backup boot"` | `UPDATE /hd0/boot/vmkernel.lz4`、`NOTE: /boot を更新した`、`sfs: exit=0` |
| 4 | ノート | `$S cmd "sfs run hsync --root /hd0 sys"` | `UPDATE /hd0/sys/…`、`sfs: exit=0` |
| 5 | ノート | `$S cmd "sfs run hsync --root /hd0"` | 全体 (ルート直下の `sys` は除く)。`PROTECTED /hd0/etc/settings.db` は正常。`sfs: exit=0` |
| 6 | ユーザー | FD を抜いて再起動 (HDD 起動) | `Image CRC … src=hdd` |
| 7 | ノート | `python3 tools/rshell_serial.py --port /dev/ttyUSB0 cmd ver` | `API: v66` 以上・`Commit: <SHA>` = HDD のカーネルが入れ替わった。以後は HDD 起動のまま `sfs run hsync …` (`--root` 無し) が使える |

- **`cmd` の行は必ず引用符で 1 つに括る** (`cmd "sfs run hsync --root /hd0 boot"`)。括らないと `--root` を `rshell_serial.py` 自身の argparse が拾って `unrecognized arguments` で止まる (`cmd -- sfs run …` でもよい)。
- 手順 3 の **`--no-backup`**: `vmkernel.old` は「起動した版と同じときだけ作る」規則で、FD 起動では起動した版 = FD の版なので HDD の版と一致せず、`--no-backup` 無しでは `not_booted_image` で断る (`--root` の案内が付く)。この形では HDD の旧カーネルは残らない — 戻す口は**この FD そのもの** (FD で起動すれば新しいカーネルで動く)。旧版を残す規則を `--root` で緩めるかは PM の判断待ち (§5 の末尾)。
- 3 → 4 → 5 は **1 回の FD 起動の中で続けて**打つ。KAPI の門は FD のカーネルと比べるので、途中でやめて HDD から起動すると HDD のカーネルと `/sys` の版が食い違い得る。
- 2 回目以降 (HDD のカーネルが v66 以上になった後) は HDD 起動のまま `sfs run hsync boot` → 再起動 → `sfs run hsync sys` / `sfs run hsync` で回せる (§1-v3、このときは `.old` が作られる)。
- **時間の見積もり (実測ではない)**: 115200bps (≒ 11KB/s、8N1) で、カーネル (`/boot`) は**約 40 秒**、初回の全体 (`sys` と合わせて約 10MB) は**15 分程度**。CI のビルドは毎回全ファイルの mtime を変えるので、サイズが同じものも内容比較で両側を読む — 2 回目以降も大きくは縮まない。`--timeout` は進捗の無い時間なので 60 で足りるが、[V3] に合わせて短くしない。

### NP21/W で先に試す (PM)

NP21/W では `/host` が HostDrv のまま使えるので、SerialFS 抜きで `--root` だけを確かめられる。HDD 起動では `/hd0` がマウントされない (= `root_not_mount` で断るのが正しい) ので、**FD 起動 + NHD** で行う。

1. `make all` → `make deploy` (HostDrv に配備元と名札)。FD イメージは `build/` の新しいもの。
2. `tools/np21w_ctl.py stop` → `tools/np21w_ctl.py start --ini <HDD 付きの ini> --fd <新しい FD イメージ> --wait-ready` (ini は変えない)。
3. `/api/cmd` で `ver` (FD の版)、`ls /hd0/boot`、`hsync -n` (→ `dest_on_fd`)、`hsync -n --root /hd0 --no-backup boot`、`hsync --root /hd0 --no-backup boot`、`hsync --root /hd0 sys`、`hsync --root /hd0`。断る側: `hsync -n --root /hd0/bin` (`root_not_mount`)、`hsync -n --root /host` (`root_is_source`)、`/cd0` があれば `hsync -n --root /cd0` (`root_not_ext2`)。
4. 停止 → FD 無しで起動 (NHD 起動) → `ver` の `Image CRC … src=hdd` と Commit が新しい版、kselftest。
5. 注意: NHD の中身を変える試験なので、終わったら `build/nhd/os32.nhd` を元へ戻すかは PM が決める ([D2] の対象になる操作はしない — hsync は NHD をゲストの中から書くだけ)。

### PM の判断待ち

- FD 起動からの `--root` 更新で `vmkernel.old` を作る規則 (今は「起動した版と一致したときだけ」のまま、`--no-backup` が要る)。緩めるなら「`<根>` の vmkernel の `image_crc` 欄が中身と一致する (壊れていない) なら `.old` にしてよい」などの案があるが、「一度も起動していない版を `.old` にしない」という §1-v3 の決め (Codex N5) と衝突するので、この票では変えていない。
- ext2 の判定を KAPI にするか: 今は根の `stat` の inode 番号 (KAPI 不要)。カーネル内の `vfs_fstype` を KAPI に出せば文字列で確かめられるが、KAPI の追加 ([ABI3] の clean ビルドと外部の再ビルド) を伴うので入れていない。
