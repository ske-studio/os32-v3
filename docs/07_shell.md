## 第7部 シェル (外部プログラム)

OS32カーネルは内蔵シェルを持たず、起動時に外部プログラム `userland/shell.bin` を実行してシステム制御を引き渡します。各種コマンドや入力機能はすべてこのシェルプログラムがKernelAPIを介して提供します。

### §7-1 コマンド一覧

**基本コマンド** (cmd_base.c):

| コマンド | 書式 | 説明 |
|---------|------|------|
| `help` / `?` | `help [cmd]` | コマンド一覧表示 |
| `clear` / `cls` | `clear` | 画面クリア |
| `tick` | `tick` | タイマカウンタ表示 |
| `ver` / `uname` | `ver` | OS バージョン表示 |
| `date` | `date` | RTC日時表示 |
| `beep` | `beep` | 起動ジングル再生 |
| `uptime` | `uptime` | 稼働時間表示 |
| `np2` | `np2` | NP21/W エミュレータ検出 |
| `time` | `time CMD` | コマンドの実行時間を計測 |
| `exit` | `exit` | このシェルを終わる |

`exit` は **`sh.bin` (`SHELL_AS_APP` ビルド、端末の子) だけ**に登録される。常駐シェル
`shell.bin` には無い — 抜けてもカーネルの起動ループが同じものを載せ直すだけなので。

**システムコマンド** (cmd_sys.c):

| コマンド | 書式 | 説明 |
|---------|------|------|
| `mem` / `heap`| `mem` | メモリ情報とヒープ使用状況 |
| `reboot` | `reboot` | システム再起動 |
| `dev` / `df` | `dev` | ブロック・キャラクタデバイス一覧 |
| `ide` | `ide [0-3]` | IDEドライブのCH/S・LBA情報 |
| `format` | `format [0-3] [sects]` | 区画表 (LBA 1、PC-98 標準配置) の OS32 区画に ext2 を作る。区画が無ければ断る (KAPI v64 以降、以前は LBA 1088 を仮定した)。長さは区画で頭打ち |
| `hdprep` | `hdprep [MB]` | **空の** hd0 (= BIOS DA 80h) に OS32 の一時置き場 (ext2、8〜256MiB、既定 256) を作る。BIOS 幾何・BX=512・LBA か現在の CHS・LBA 1 に区画項目が無い・LBA 0 に 55AA が無い・hd0 がルートでない、を全部見てから表示と `yes` の入力 → 探りの書き込み → `ext2_format_at` → 区画表を書いて読み戻し → `/hd0` にマウントして確認。後半の失敗は `INCOMPLETE`。KAPI v64 ([TASK_HDD_INSTALL](archive/realhw_v21/TASK_HDD_INSTALL.md) 段 1) |
| `play` | `play MML` | MML文字列をFM音源で再生 |
| `os32gui` | `os32gui [on\|off]` | GUI シェル (/bin/gshell.bin) へ切り替え / 起動時 GUI の既定を `/etc/system.cfg` に書く (GUI v1.1 K4) |
| `gfxmode` | `gfxmode pc98\|pegc\|cirrus\|auto` | 次回起動のグラフィクスバックエンドを `/etc/system.cfg` の `GFX=` に書く (GUI v1.1 H2b) |
| `kbdstat` | `kbdstat [-w]` | キーボード 8251 の診断カウンタ (IRQ1 回数・空 IRQ・エラー・オーバーラン・起動時の 0043h・書いたコマンド語・直近のステータスとスキャンコード・いまの 0043h) を 1 行で。KAPI v62 `kbd_diag`。**`-w`** は受信 1 バイトごとの行 (`seq` / 生の `code` と make・break / 処理後の修飾) を ESC か 30 秒まで出し続ける (KAPI v67 `kbd_diag_log`、seq が飛べば `LOST` 行)。rshell から読める。読み方は [POLICY_DEBUG.md](POLICY_DEBUG.md) §4-57 |

**PCI コマンド** (cmd_pci.c):

実機 PC-9821Ra266 の内蔵 LAN (Intel 82557 = `8086:1229`) を見つけるための口
(票 [`tasks/realhw/TASK_LAN_82557.md`](tasks/realhw/TASK_LAN_82557.md) L-A)。
**NP21/W は PCI を実装していない**ので、エミュレータでは
`lspci: no PCI (mechanism #1 not present)` が正しい応答。

| コマンド | 書式 | 説明 |
|---------|------|------|
| `lspci` | `lspci` | PCI デバイス一覧 (vendor:device・クラス・Header Type・IRQ / Pin・BAR の生番地) を 1 行 1 デバイスで |
| `lspci -v` | `lspci -v [bus:dev.fn \| bus dev fn]` | 1 デバイスを複数行で: Revision・Class/Subclass/ProgIF・Header Type (multi-function 明示)・Command (I/O / Mem / BusMaster)・Status・Subsystem (Type 0) またはバス番号 (Type 1)・**BAR の生値を 0 も含めて全部** (io / mem32 / mem64-lo・hi / prefetch と番地)・Interrupt Line / Pin。**読み取り専用** (BAR の大きさを調べる書き込みもしない)。未知の PCI カードの識別用。行づくりは `userland/shell/pci_verbose.c` (試験 `check-pci-decode-host`) |
| `pcidump` | `pcidump bus dev fn` | そのファンクションのコンフィギュレーション空間 256 バイトを 16 進ダンプ (Command / Subsystem / Cap ポインタを実機から持ち帰る用) |

**ディレクトリコマンド** (cmd_dir.c):

| コマンド | 書式 | 説明 |
|---------|------|------|
| `ls` | `ls [-la] [path...]` | ディレクトリ一覧表示 (`-a` で `.`/`..` も表示) |
| `cd` | `cd [path]` | カレントディレクトリ変更 (引数なしで `$HOME` へ) |
| `pwd` | `pwd` | カレントディレクトリ表示 |
| `mkdir` | `mkdir dir...` | ディレクトリ作成 |
| `rmdir` | `rmdir dir...` | ディレクトリ削除 |

**ファイルコマンド** (cmd_file.c):

| コマンド | 書式 | 説明 |
|---------|------|------|
| `cp` | `cp [-r] SRC DST / SRC... DIR` | ファイルのコピー (`-r` で再帰) |
| `mv` | `mv SRC DST / SRC... DIR` | ファイルの移動 |
| `rm` | `rm FILE...` | ファイルの削除 |
| `cat` / `cat2` | `cat [-n] FILE...` | ファイル内容表示 (`-n` で行番号付き) |
| `echo` | `echo [args...] [> FILE]` | テキスト出力 (stdout経由) |

**マウント・実行コマンド** (cmd_mnt.c):

| コマンド | 書式 | 説明 |
|---------|------|------|
| `mount` | `mount PREFIX DEV FS` | マウント (例: `mount /hd0/ hd0 ext2`) |
| `umount`| `umount PREFIX` | アンマウント |
| `sync` | `sync` | メタデータ書き戻し |
| `exec` | `exec FILE.BIN` | OS32Xバイナリを明示的に実行 |

**環境変数コマンド** (cmd_env.c):

| コマンド | 書式 | 説明 |
|---------|------|------|
| `env` | `env` | 全環境変数を表示 |
| `set` / `export` | `set VAR=VALUE` | 環境変数を設定 |
| `unset` | `unset VAR...` | 環境変数を削除 |

**リモートシェル(rshell) コマンド** (rshell.c):

| コマンド | 書式 | 説明 |
|---------|------|------|
| `serial` | `serial [baud]` | 引数なし = 現在の設定 (mode / 実効速度 / FIFO の有無) を表示。`serial 9600` = 互換モードで初期化 (SerialFS は自動でマウントしない — `sfs run` の中だけ)、受信の誤り (overrun / framing / parity / リング溢れ) の数も出す、`serial 115200` = V･FAST (FIFO 搭載機のみ)。**出せない速度は適用せず拒否する**。rshell 中の切替は 5 秒無音で元の速度へ自動で戻る (票 TASK_SERIAL_VFAST) |
| `terminal` | `terminal` | ターミナルモード (ESCで終了) |
| `rshell` | `rshell` | リモートシェルモード開始 (ESCで終了) |
| `send` | `send TEXT...` | RS-232C文字列送信 |
| `hotdeploy` | `hotdeploy [PATH LEN CRC32]` | ステージング領域の内容をファイル化 (引数なしで領域の番地を報告)。ホスト側は `tools/hotdeploy.py` から使う |
| `sfs` | `sfs run COMMAND...` | **常駐シェルだけ**。ホストが rshell へ送った 1 行が丸ごと `sfs run ...` のときだけ動き、シリアル越しの `/host` (SerialFS) を開いて COMMAND を走らせる。ホストは `tools/rshell_serial.py --serve-host <dir>`。セッション中の出力は溜めて終わりに長さ付きのフレームで送る (`sfs: exit=N` を含む)。rshell のシリアルの ESC は行頭の単独のときだけ閉じる (票 TASK_SERIAL_HOSTFS 部品 B、`man sfs`) |
| `recv` | `recv [host:PATH [LOCAL]]` | ファイル受信 (SerialFS または旧プロトコル) |
| `push` | `push LOCAL host:PATH` | SerialFS 経由でホストへファイル送信 |
| `tvdump` | `tvdump` | テキストVRAMダンプをシリアル送信 |

※ 起動時に自動的にrshellモードに入るため対話コマンドもホストと連携可能です。

**ファイラ・ファイル管理コマンド** (cmd_filer.c):

| コマンド | 書式 | 説明 |
|---------|------|------|
| `filer` | `filer [dir]` | CUIファイラ (カーソル移動・Enter実行・拡張子関連付け) |

**その他の組み込みコマンド**:

| コマンド | 書式 | 説明 |
|---------|------|------|
| `losetup` | `losetup <path> <slot> \| -d <slot> \| -l` | ループデバイス管理 (ディスクイメージをブロックデバイスとして接続) |
| `dd` | `dd <dev> lba=N count=M [file=PATH]` | ブロックデバイスのセクタ読み出し |
| `export` | `export VAR=VALUE` | 環境変数の設定 |

**エイリアス**: `cls`→`clear`、`uname`→`ver`、`.`→`source`、`cat2`→`cat`、
`heap`→`mem`、`df`→`dev`。

### §7-2 プログラム実行とPATH探索

| 実行方法 | 例 | 説明 |
|---------|------|------|
| 直接実行 | `./test2.bin` | カレントディレクトリのバイナリを実行 |
| コマンド名実行 | `test2` | 未知コマンド → `.bin` を補完して探索 |
| PATH探索 | `grep hello` | `$PATH` 内のディレクトリを順に探索 |

- 環境変数 `PATH` にコロン区切りでディレクトリを設定可能 (デフォルト: `/bin:/sbin:/usr/bin` — `config.h` の `SYS_DEFAULT_PATH`)
- 起動時に `/etc/profile` → `$HOME/.profile` の順で自動読み込み (環境変数・PATHの初期設定用)
- `*` や `?` などの簡単なワイルドカードもサポート

**外部コマンド** (`/bin` に配置、`userland/cmds/`):
`cal` `cfg` `diff` `du` `find` `grep` `hclip` `hdate` `head` `hexdump` `ime` `less` `lpr` `man` `more` `sleep` `sort` `stat` `tail` `tar` `tee` `touch` `v86` `wget` `wc`
(`ime` は FEP の有効化/辞書操作、`v86` は V86 モードでのゲスト起動 (`-g` は実機の ROM の INT 18h AH=31h/30h の I/O 記録 — [05 §5-5](05_drivers.md))、`cfg` は設定レジストリ `/etc/settings.db` の get / set / list / status / init / export — [archive/settings/TASK_S2.md](archive/settings/TASK_S2.md) §2)
(Host Services (KAPI v51 の host_*、WSL2 の `host_agent.py` が要る、[archive/network/TASK_N3.md](archive/network/TASK_N3.md)): `wget <url> [file]` は URL 取得 (http_status 確定後にファイル作成、非 200 は捨てて終了 1)、`lpr <file>|-` は本文をホストのプリンタへ (basename の空白・制御文字を `_` に、`-` は stdin)、`hclip get|put <file>` はホストのクリップボード読み書き (1〜4096B)、`hdate` はホスト時刻を 1 行表示 (RTC は設定しない)。終了コード: 0 成功 / 1 業務失敗 / 2 リンク / 3 usage / 4 ローカル I/O)
(`stat PATH...` は `sys_stat` の結果を 1 パス 1 行で出す — 種別 / サイズ / `st_dev` (生値と `(dev_type << 8 | unit) + 1` の復号、[06_filesystem.md](06_filesystem.md) §6-1) / `st_ino` / mode / 時刻。存在しないパスは `stat: <path>: <理由>` で終了 1)
(`tar c|x|t` は ustar サブセットの束ね道具 — 通常ファイルとディレクトリだけ、100B 名、圧縮なし。圧縮は `lz4` を外で掛ける (`etc.tar.lz4`)。ホストの Python `tarfile` で読める — [archive/settings/TASK_S6.md](archive/settings/TASK_S6.md))
(その他 `/sbin` に `install` `cdinst`、`/usr/bin` にアプリ群。詳細は [09_exec.md](09_exec.md) 参照)

### §7-3 パイプ・リダイレクト

シェルは標準入出力のリダイレクトとパイプラインをサポートします。

| 演算子 | 書式 | 説明 |
|--------|------|------|
| `>` | `cmd > file` | stdoutをファイルに書き出し (上書き) |
| `>>` | `cmd >> file` | stdoutをファイルに追記 |
| `<` | `cmd < file` | ファイルをstdinとして入力 |
| `\|` | `cmd1 \| cmd2` | cmd1のstdoutをcmd2のstdinに接続 |

- パイプはカーネルのパイプバッファAPI (`sys_pipe_alloc` 等) を使用し、逐次実行方式で動作
- リダイレクトはカーネルのFDリダイレクトAPI (`sys_redirect_fd` 等) を使用
- フィルタコマンド (grep, wc, head, tail, tee, sort, more, less 等の外部コマンド) はstdin/ファイル両対応で、パイプラインと連携可能
- `ls` / `pwd` は `isatty(1)` で出力先を判定し、非TTY時は1行1エントリ形式で出力

### §7-4 入力機能

| 機能 | キー | 説明 |
|------|------|------|
| コマンド履歴 | ↑/↓ | 16件リングバッファ (`.history` に永続化、起動時に復元) |
| Tab補完 | Tab | 共通プレフィクス補完 (ディレクトリ探索含む) |
| Tab候補表示 | Tab×2 | 候補一覧表示 |
| カーソル移動 | ←/→ | 行内移動 |
| 行頭移動 | Home | カーソルを行頭に |
| 文字削除 | Del | カーソル位置の文字を削除 |
| 行クリア | ESC | 入力中の行をクリア |
| バックスペース | BS | カーソル左の文字を削除 |

**日本語入力**: 入力は `ime_getkey()` (KernelAPI) 経由で取得され、IME/FEP有効時は
UTF-8マルチバイト文字の入力・表示 (`shell_print_utf8`) に対応する。

### §7-5 スクリプトエンジン

シェルはバッチスクリプト実行機能を内蔵しています。起動時に `/etc/profile` が自動実行され、環境変数やPATHの初期設定が行われます。

| コマンド | 書式 | 説明 |
|---------|------|------|
| `source` / `.` | `source FILE` | 指定ファイルをスクリプトとして実行 |
| `if` | `if VAL1 == VAL2 CMD...` | 条件一致時のみ CMD を実行 (ELSE 分岐なし) |
| `goto` | `goto LABEL` | ラベルへのジャンプ |
| `return` | `return` | スクリプト実行を終了してシェルに復帰 |
| `ask` | `ask "prompt" VAR` | ユーザー入力を受け取り環境変数 VAR に格納 |

**ラベル定義**: 行頭に `:LABEL` 形式で記述。`goto LABEL` でジャンプ先を指定。

---
