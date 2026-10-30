# OS32 デバッグポリシー

バイナリ反映の確認、検証の制限、領域別の落とし穴への入口。
開発全般は [POLICY_DEV.md](POLICY_DEV.md)、制約は [CONSTRAINTS.md](CONSTRAINTS.md)、
ハードウェアの技術ノートは [10_notes.md](10_notes.md) を参照。

## §1. デバッグの基本原則

現象・期待結果・原因候補を整理し、最小の観測で仮説を検証する。
具体的な進め方は [run-os32「観測してから変更する」](../.claude/skills/run-os32/SKILL.md#観測してから変更する)。

## §2. バイナリ反映の確認 (最優先)

ロジックの調査に進む前に、実行中のバイナリと手元の成果物を照合する。
配備経路と実行手順は [08_build §8-4](08_build.md#配備3経路)、操作は [run-os32](../.claude/skills/run-os32/SKILL.md)。

| 対象 | 照合するもの |
|---|---|
| ビルド | `ver` の Build・Commit・Image CRC と対象の成果物 |
| ロード先 | `config.h` の `SYS_SHELL_BIN` 等と `deploy.yaml` の `guest:` |
| ゲストの実ファイル | `ls -l /boot/vmkernel.lz4` 等のサイズ・ハッシュと手元のファイル |
| ABI 変更 | [CONSTRAINTS [ABI]](CONSTRAINTS.md) の clean ビルド |
| デバッグ部品 | `ver` の `kdebug: on (pair <CRC>, …)` と同じ `ver` の Image CRC |
| 自己試験 | 対象ビルドの `kernel.map` から引いた `kselftest_pass` / `kselftest_fail` |
| 起動ログ | `/var/log/boot.log` ヘッダの Commit・Image CRC と `ver` |

`make deploy` の成功や Build 日時だけでは反映の確認にならない ([V1]/[V4])。
NHD の作業イメージは `build/nhd/os32.nhd`。停止 → 配備 → 起動を守る ([D1])。
ゲストで書き換えたファイルは次の NHD 配備で消えるため、ホストの配備元にも反映する。

カーネルの到達確認は `kdebug/kselftest.c` の起動時自己試験で行う。
`KDEBUG=1` では試験が走り、`KDEBUG=0` は skipped・計数 0/0 (監査は走る)。
古い map の番地や初期化前のゼロ値を合格と判定しない。
`kstring` / `kmalloc` / `kprintf` を触ったら kselftest に項目を足す。
newlib をリンクする `userland/tests/klibc_test` はカーネル側の検証にはならない。

受入と採取は `kdebug on` → `reboot` 後、pair が Image CRC と一致した状態で行う。
`rejected (<code> …)` は OFF と同じに扱わず、理由コードを記録する。
本体と `/boot/kdebug.bin` は CRC の対。NHD は `deploy-kernel`、実機は `hsync boot` で対ごと入れる。
部品の形式は [08_build §8-2](08_build.md#起動時に読む部品の形式-kmod-v1)、理由コードは [KAPI_SPEC](KAPI_SPEC.md) の MemStat v75 欄。
OFF の起動は `ver` の Image CRC と `boot.log` で到達を確認する。

実機で流れた起動出力は `cat /var/log/boot.log` で読む。
末尾の `dropped` が 0 でなければ後ろが切れている。保存失敗後は前回分が残りうる。
`boot.new` の残存は保存途中で止まった印であり、完全なログの証拠ではない。
保存・世代交代の詳細は [bootlog の設計](design/init/bootlog.md#落とし穴)、配備の詳細は [nhd_deploy の設計](design/tools/nhd_deploy.md#落とし穴)。

## §3. 仮説の提示と検証プロセス

観測・仮説・修正後の報告は [run-os32「観測と調査」](../.claude/skills/run-os32/SKILL.md#観測と調査)。
承認範囲と判断の分担は [ROLES §0](tasks/agents/ROLES.md#0-現行の体制) に従う。

## §4. よくある落とし穴と教訓集

### 領域別の早見 (触る前にここを見る)

検証の制限は下の短文、技術詳細は設計書の「落とし穴」を参照。
設計書全体の入口は [design/README.md](design/README.md)。

**検証・配備**

- 反映は実行中の版・CRC・ファイル・map で照合する。→ [§2](#2-バイナリ反映の確認-最優先)
- API timeout をハングと決めつけない。→ [4-7](#4-7-api-timeout-とハング)
- NHD 配備は停止 → 配備 → 起動。→ [4-8](#4-8-nhd-の配備とロック)
- `hsync` の前に HostDrv を更新する。→ [4-33](#4-33-hsync-のコピー元)
- `/sys` は `hsync sys` + 再起動。→ [4-42](#4-42-古い共有ライブラリ)
- 媒体配置と配備元の扱い。→ [nhd_deploy](design/tools/nhd_deploy.md#落とし穴)

**実機と NP21/W の差**

- NP21/W で通信できても実機の baud は保証されない。→ [4-49](#4-49-シリアルの実効速度)
- FDC のシーク・回転待ち・DMA 制限はエミュレータだけで確認できない。→ [4-51](#4-51-fdc-の時間と-dma)
- FRY 無しで通るエミュレータと実機の Ready は違う。→ [4-53](#4-53-fdc-の-ready)
- 両クロックのホスト試験と実機の実時間は別に照合する。→ [4-54](#4-54-pit-の実時間)
- `io_wait()` 連打後の FD 読取異常は原因未特定・実機未確認。→ [4-55](#4-55-io_wait-連打後の-fd-読取)
- NP21/W はキーボード 8251 の信号線を見ない。→ [4-57](#4-57-キーボード-8251-の信号線)
- 8255 ポート C の全体書込みの副作用は実機で現れる。→ [4-59](#4-59-8255-ポート-c-の副作用)
- PEGC の実機モードは実機 ROM の OUT 列で照合する。→ [4-62](#4-62-pegc-の実機モード)
- 書くだけのレジスタは読み返せるとは限らない。→ [4-63](#4-63-レジスタの読戻し)
- シリアルの分周・クロック判定。
  → [serial](design/drivers/char/serial.md#落とし穴)、[sysclk](design/kernel/time/sysclk.md#落とし穴)
- FD の IPL・媒体ジオメトリ・D88。
  → [boot_fat](design/boot/boot_fat.md#落とし穴)、[loader_fat_new](design/boot/loader_fat_new.md#落とし穴)、[diskio](design/fs/fat/fatfs/diskio.md#落とし穴)、[mkd88](design/tools/mkd88.md#落とし穴)
- FDC の予算・再試行・キャッシュ。
  → [fdc](design/drivers/block/fdc.md#落とし穴)、[fdc_track](design/drivers/block/fdc_track.md#落とし穴)
- ISR と foreground 間の状態観測。→ [pcm_cs4231](design/drivers/sound/cs4231/pcm_cs4231.md#落とし穴)

**カーネル・FS**

- 起動時の BSS 初期化。→ [kentry](design/arch/x86/kernel/kentry.md#落とし穴)
- 呼出しをまたぐ状態と所有者単位の資源回収。
  → [exec](design/exec/exec.md#落とし穴)、[exec_heap](design/exec/exec_heap.md#落とし穴)
- CPL=3 の KAPI、IF と WM の信頼境界。
  → [ring3_entry](design/arch/x86/kernel/ring3_entry.md#落とし穴)、[ring3_str](design/exec/ring3_str.md#落とし穴)、[gui](design/kernel/gui.md#落とし穴)
- KAPI の座標・添字・長さの検査。→ [kapi](design/kapi/README.md#落とし穴)
- ext2 の scratch 所有・解放順・I/O 失敗。
  → [ext2_inode](design/fs/ext2/ext2_inode.md#落とし穴)、[ext2_super](design/fs/ext2/ext2_super.md#落とし穴)
- ext2 の端数読取、callback 再入、名前と像の検査。
  → [ext2_file](design/fs/ext2/ext2_file.md#落とし穴)、[ext2_dir](design/fs/ext2/ext2_dir.md#落とし穴)
- FAT 名のフィールド、HostDrv の共有状態。
  → [fatfs_vfs](design/fs/fat/fatfs_vfs.md#落とし穴)、[hostdrvfs](design/fs/hostdrv/hostdrvfs.md#落とし穴)
- デバイス窓は RAM 上端でなく物理地図で判定する。
  → [pgalloc](design/mm/pgalloc.md#落とし穴)、[backend_cirrus](design/drivers/video/cirrus/backend_cirrus.md#落とし穴)
- SQLite の共有プールと接続寿命・エラー取得。
  → [sqlite_engine](design/kernel/sqlite_engine.md#落とし穴)、[kapi_db](design/kapi/kapi_db.md#落とし穴)
- UTF-8 のバイト・表示桁・切詰め、フォント生成。
  → [utf8](design/lib/utf8.md#落とし穴)、[gen_font16](design/tools/gen_font16.md#落とし穴)
- 外部プログラムの漢字は KAPI `unicode_to_jis` で引く。→ [KAPI_SPEC §4-13](KAPI_SPEC.md)

**GUI・入力・道具**

- cfg の巻戻りと hotdeploy が届く文脈に注意する。→ [4-23](#4-23-gui-検証の実行環境)
- `key(text=)` の英大文字は検証の制限として扱う。→ [4-43](#4-43-keytext-の英大文字)
- Host Services の `-100` は常駐と実状態を確認する。→ [4-45](#4-45-host-services-の常駐)
- 入力の消費と WM への配送順。
  → [libos32ui_core](design/userland/lib/ui/libos32ui_core.md#落とし穴)、[input](design/userland/gshell/src/input.md#落とし穴)
- 描画形式・attach・Cirrus の窓・カーソル。
  → [libos32gfx_core](design/userland/lib/gfx/libos32gfx_core.md#落とし穴)、[backend_cirrus](design/drivers/video/cirrus/backend_cirrus.md#落とし穴)、[console](design/kernel/console/console.md#落とし穴)
- GUI の描画と計測。→ [draw](design/userland/rust/libos32gui/src/draw.md#落とし穴)

### 4-7. API timeout とハング

HTTP の timeout はコマンドの未実行・停止を意味しない。
画面・実行状態・完了印を照合し、実行中かシリアル不整合かを切り分ける。
待ち時間は [V3]、再送・再起動の判断は [run-os32](../.claude/skills/run-os32/SKILL.md#実行制御と通信) を参照。

### 4-8. NHD の配備とロック

NP21/W が NHD を開いたままでは配備が反映されない ([D1])。
停止 → 配備 → 起動の順と終了値を確認し、§2 でゲストへの反映を照合する。
詳細は [nhd_deploy](design/tools/nhd_deploy.md#落とし穴)、停止・起動は [run-os32](../.claude/skills/run-os32/SKILL.md)。

### 4-23. GUI 検証の実行環境

`make deploy*` は `/etc/system.cfg` を巻き戻す。設定切替だけの検証は reset で行う。
`hotdeploy` は CUI で rshell が生きているときだけ届く。GUI・FEP 中は先に CUI へ戻す。
エミュレータの操作者は同時に 1 人 ([ROLES §0](tasks/agents/ROLES.md#0-現行の体制))。
入力・GUI の出入りは [run-os32「GUI 操作」](../.claude/skills/run-os32/SKILL.md#gui-操作)。

### 4-33. hsync のコピー元

NHD 配備は HostDrv を更新しない。古い HostDrv から `hsync` すると NHD が巻き戻る。
`hsync` / `hsync sys` の直前に `make deploy` で HostDrv を同期する。
内容比較はコピー元の新旧を保証しないため、実行後も §2 の成果物照合を行う。

### 4-42. 古い共有ライブラリ

窓が出ず、例外も中断も 0 件なら、ゲストの `/sys/lib/libos32gui.shlib` と手元のサイズを照合する。
`hsync` は既定で `/sys` を除外する。`hsync sys` 後も、起動時に読む共有ライブラリの切替には再起動が要る。
`fault_kill_count` が増える場合の WM 文脈は [exec の設計](design/exec/exec.md#落とし穴) を参照。

### 4-43. key(text=) の英大文字

`key(text="ABC")` がゲストで `abc` になった記録があり、英大文字の注入は検証の制限として残る。
この経路だけで英大文字の入力を確認済みにしない。

### 4-45. Host Services の常駐

`-100` (`HOST_ELINK`) は STALE / リンク未確立。`tools/host_agent.py` の常駐を確認する。
設定ファイルだけで判断せず、`/api/net` の enabled・backend・送受信状態を照合する。
起動と応答待ちの詳細は [host_agent](design/tools/host_agent.md#落とし穴)。

### 4-49. シリアルの実効速度

NP21/W は baud の実効値を保証しない。エミュレータで通信できても実機の速度照合にはならない。
分周・警告・対応速度は [serial](design/drivers/char/serial.md#落とし穴)、クロックは [sysclk](design/kernel/time/sysclk.md#落とし穴)。

### 4-51. FDC の時間と DMA

NP21/W は実機のシーク時間・回転待ち・1MB 超への DMA 制限を保証しない。
機構の最悪値からの時間予算と DMA 設定は [fdc](design/drivers/block/fdc.md#落とし穴)、読み順とキャッシュは [fdc_track](design/drivers/block/fdc_track.md#落とし穴)。

### 4-53. FDC の Ready

FRY 無しでも NP21/W では通るが、実機では Not Ready になりうる。
ビットと Ready 判定の詳細は [fdc](design/drivers/block/fdc.md#落とし穴)。

### 4-54. PIT の実時間

両クロックのホスト試験が通っても、実機での tick の実時間は別に照合する。
判定順は [sysclk](design/kernel/time/sysclk.md#落とし穴)、reload と周期は [pit](design/kernel/time/pit.md#落とし穴)。

### 4-55. io_wait() 連打後の FD 読取

NP21/W で `io_wait()` を万単位で連打した後、FD が古いデータを返した。原因未特定・実機未確認。
時間待ちのループで連打しない。I/O wait と時間待ちの違いは [io](design/include/io.md#落とし穴)。

### 4-57. キーボード 8251 の信号線

NP21/W は DTR (RTY#)・RTS (RDY#)・RxE を見ないため、打鍵が通っても実機の信号線は保証されない。
コマンド・IRQ 分類は [kbd](design/drivers/input/kbd.md#落とし穴)、受信記録の欠落判定は [kbd_dlog](design/drivers/input/kbd_dlog.md#落とし穴)。

### 4-59. 8255 ポート C の副作用

エミュレータで副作用が見えなくても、0035h への全体書込みを正当化しない。
BSR と BUZ の極性は [serial](design/drivers/char/serial.md#落とし穴)。

### 4-62. PEGC の実機モード

NP21/W の BIOS 値だけでは実機の画面モードを検証できない。実機 ROM の OUT 列で照合する。
順序・除外表・未解明の組は [backend_pegc](design/drivers/video/pc98/backend_pegc.md#落とし穴)、採取は [v86](design/kernel/v86/v86.md#落とし穴)。

### 4-63. レジスタの読戻し

手本が書くだけのレジスタは、実機で書いた値と読戻しが違うことがある。
照合は必須 CHECK に絞り、ほかは記録だけにし、外すものは実行時に渡す。
mask と判定範囲は [GeForce の照合](design/hwdev/gf/display/checks.md#落とし穴)、[C 計画](design/kdebug/gfinit_plan_c.md#落とし穴)。

## §5. デバッグ道具箱

`driver.py` は `.claude/skills/run-os32/driver.py`。
コマンド・撮影・配備・起動停止・入力の手順は [run-os32](../.claude/skills/run-os32/SKILL.md)。
ツールの引数は `tools/np21w_mcp/server.py`、記号の番地は対象ビルドの `kernel.map` で確認する。

| 道具・口 | 用途・参照先 |
|---|---|
| `driver.py cmd` / `cmd --wait` | 短いコマンド / 長い試験と完了印。run-os32「短いコマンド」「長いコマンド」 |
| `driver.py smoke` | 生存確認。run-os32「生きていることを示す」 |
| `driver.py pull` | ゲストのファイル採取。run-os32「ゲストのファイルをホストへ」 |
| `driver.py shot` | 画面採取。run-os32「画面だけ」 |
| `tools/np21w_ctl.py` | 停止・起動・媒体・実体・ダイアログ。run-os32「配備」 |
| `emu_status` / `/api/status` | 実行状態・EIP |
| `emu_fault` / `emu_regs` | 例外と CPU 状態 |
| `emu_disasm` | 逆アセンブル。線形アドレスと 16/32bit モードに注意 |
| `emu_pic` | IRQ と EFLAGS.IF・IDT/ISR の照合 |
| `emu_tvram` / `/api/tvram` | テキスト画面 |
| `emu_screenshot` / `/api/screenshot` | 実際の描画・属性の見た目 |
| `emu_gdc` | 画面モード |
| `emu_cmd` / `emu_key` | 行単位のコマンド / 生キー入力 |
| `/api/mouse` | GUI の絶対座標入力 |
| `emu_trace` | 経路・性能の短い採取 |
| `emu_read_mem` / `/api/mem` | 対象 map のカウンタ・印。既定は物理アドレス |
| `emu_sound` | 音源の出力レベル・レジスタ |
| `/api/net` | Host Services の実際の通信状態 |
| `tools/rshell_serial.py peek` | 実機のメモリ採取。pair と ELF の版・CRC を照合 ([rshell_peek.py](../tools/rshell_peek.py) 冒頭) |
| `ver` / `cat /var/log/boot.log` | 起動した版と起動出力 (§2) |
| `mem` / `ls -l` / `cat /etc/profile` | メモリ・実ファイル・起動設定 |
| `kprintf()` | コンソールへのデバッグ出力 |
| `serial_printf()` | 画面を汚さないシリアル出力 |

## §6. AI との協調デバッグ

役割・承認・レビュー・失敗と未確認の報告は [ROLES](tasks/agents/ROLES.md#0-現行の体制)。
修正後の報告は [run-os32「修正後と成果」](../.claude/skills/run-os32/SKILL.md#修正後と成果)。
