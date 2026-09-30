# TASK_FDC_REALHW — 実機で FD から起動できない (root panic) を直す

> 状態: **受入完了 (2026-09-22)** — 実機 Ra266 で R6 合格 (bee42cc)。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-fable-5-1`、2026-09-22) / それまでの状態: **実機で合格 (R6、2026-09-22、bee42cc)** — FD 起動 → シェル、シリアル経由で `ver` / `ls` が返る

基点: `feat/gui` `ec48c6b`。実機計画は [`PLAN.md`](../../tasks/realhw/PLAN.md)、1.44MB の経緯は [`TASK_FD144.md`](../../tasks/realhw/TASK_FD144.md)、
FDC ドライバの仕様表は [`../../05_drivers.md`](../../05_drivers.md) §5-2。

## 0. 症状 (ユーザー報告、2026-09-22)

実機 PC-9821Ra266 で FD から起動すると、カーネルの `MOUNT...` の右に赤い **`root panic`**。
**1.2MB 2HD (`os32_boot.d88` 由来) と 1.44MB (`os32_boot144.img`) の両方**で同じ。
HDD は接続されているが未フォーマット (または他 OS の内容)。

ユーザーの当初の見立ては「新規インストール時は ext2 が無いので当たり前」だったが、
**FD 起動の経路に ext2 の依存は無い** (§1)。`root panic` は **fd0 の FAT マウント失敗**である。

## 1. 机上で潰したこと

| 疑い | 結論 | 根拠 |
|---|---|---|
| ext2 が無いと起動できない | **否** | `kernel/kernel.c` は `boot_drive` が FDD なら `fd0`+`fat` をルートにし、`/hd0` は ext2 → iso9660 → fat と試して**全部失敗しても続行**する。ext2 は superblock の magic 不一致で `EXT2_ERR_MAGIC` を返すだけ |
| 空 / 他 OS の HDD でマウント試行がハングする | **否** | `ide_wait_*` は全部ループ上限つき。`ext2_find_partition` は読めなければ既定 LBA に落ちる。`iso9660_mount` は CD 以外を断る。`pc98_find_fat_partition` は空エントリで抜ける |
| HostDrv が無いと止まる | **否** | `hostdrvfs_detect()` が 0 を返せばマウントしない (TASK_FD144 F11 で実測) |
| IPL / ローダ | **否** | どちらも BIOS INT 1Bh で読む。カーネルまで到達している (`MOUNT...` が出る) |
| **カーネル自前の FDC ドライバ** | **ここ** | fd0 の FAT マウントは `fs/fatfs/diskio.c` → `fdc_read_sector()` (`drivers/fdc.c`) で、BIOS を使わない。**実機で初めて走った** |

## 2. 原因

`drivers/fdc.h` の **`FDC_IRQ_TIMEOUT_TICKS = 20` (200ms)** が NP21/W に合わせた値で、実機の機構に足りない。

| 動作 | 実機の所要時間 | 200ms との関係 |
|---|---|---|
| RECALIBRATE / SEEK | SPECIFY が SRT=8ms なので **最大 77〜80 トラック × 8ms ≒ 620〜640ms**。ローダが `VMKRNL.LZ4` を読んだ直後のヘッドはシリンダ 20〜40 付近 → 160〜320ms | **`fdc_init()` の recalibrate がタイムアウト** |
| READ DATA | 1 回転 (360rpm=167ms / 300rpm=200ms) + ヘッドロード 10ms + 転送 16ms | ぎりぎり。運次第 |

**NP21/W はシーク時間を模擬していない**: `np21w-src/src/io/fdc.c` の `fdc_intwait` は
`nevent_set(NEVENT_FDCINT, 512, ...)` — 512 サイクル後に割り込みが来る。だからエミュレータでは
200ms で一度も困らなかった (POLICY_DEBUG §4-49 のシリアルと同じ型: **模擬していない量は「確認済み」にならない**)。

**タイムアウトの後に回収が無いので事故が連鎖する**:
遅れて来た seek-end 割り込みを SENSE INTERRUPT STATUS で読み出さないまま次の SEEK を出すと、
µPD765 の INT 線が上がりっぱなしになり、PIC (エッジトリガ) に次のエッジが来ない。以後の
`fdc_wait_irq` が**全部**タイムアウトし、3 回リトライしても読めず `RES_ERROR` → `f_mount` 失敗 → `root panic`。
1.2MB / 1.44MB の両方で同じになるのは、この経路が媒体に依らないから。

## 3. 直し方 (コーダーへの依頼、§4 に結果)

1. タイムアウトを機構に合わせる: シーク/リキャリブレート 1.5s、R/W 1s、リセット 0.5s (根拠は `fdc.h` のコメント)。
2. シーク完了待ちを堅牢に: IRQ を待ち、タイムアウトしても SIS で ST0 を読み、SE が立っていれば「エッジ取りこぼし」として完了扱い。
3. SEEK / RECALIBRATE の前に SIS で未回収割り込みを**排水** (上限 4 回、0x80 で止める)。
4. リトライの間に `fdc_recover()` (リセット → Specify → 排水 → recalibrate)。
5. RECALIBRATE の EC (77 ステップで届かない) はもう 1 回。
6. DMA バッファを `aligned(1024)` にして [HW2] を**リンク順に頼らず**保証。起動時に検査。
7. 最終失敗のときだけ `[fdc] ... st0/st1/st2` を 1 行出す ([V4]、実機の画面で読める)。

## 4. 結果 (コーダー Opus 5、`fix/fdc-realhw`)

| 項目 | 実装 |
|---|---|
| 時間上限 | `FDC_SEEK_TIMEOUT_TICKS` 150 / `FDC_RW_TIMEOUT_TICKS` 100 / `FDC_RESET_TIMEOUT_TICKS` 50。旧 `FDC_IRQ_TIMEOUT_TICKS` は R/W の別名として残置 |
| 判定の切り出し | `drivers/fdc_decide.[ch]` — `fdc_sis_result_bytes()` (SIS の 1 バイト応答) と `fdc_classify_seek_end()` (OK / RETRY_EC / PENDING / FAIL)。I/O も tick も触らない |
| 排水 | `fdc_drain_interrupts()` を SEEK / RECALIBRATE / リセット後に。ST0=80h で打ち切り、上限 4 |
| 完了待ち | `fdc_wait_seek_end()` — IRQ を待ち、タイムアウトしても SIS。別ドライブの通知は読み捨て |
| 回復 | `fdc_recover()` を R/W リトライの**間**にだけ |
| EC | `FDC_RECAL_ATTEMPTS` = 2 |
| DMA | `aligned(1024)`、番地は 0x1555e0 → 0x156000。起動時検査 |
| 診断 | `[fdc] read/write fail ... phase= st0= st1= st2=`、`[fdc] recalibrate drv= rc= st0=` |
| ホスト TDD | `tools/tests/test_fdc_seek.py --target --mutate` (6 ケース、変異 4 本が全部 RED)。記録 `tools/tests/fdc_seek_tdd.md`。`make check-par` に `check-fdc-seek-host` |

**コーダーが見つけて設計に入れたこと**: pending 無しの SIS は ST0=80h の **1 バイトだけ**返る (NP21/W `FDC_SenceintStatus` も同じ)。
旧コードは無条件に 2 バイト読んでいたので、排水のたびに来ない 2 バイト目を 10000 回空転して待つところだった。

**失敗経路の所要時間**: 全部タイムアウトする最悪ケースで 1 セクタ約 11.5 秒 (旧 1.2 秒)。媒体無しのドライブは
NR 付きの割り込みが即座に来る (実機の µPD765A も NP21/W の `FDC_Seek`/`FDC_Recalibrate` も) ので、普段は踏まない。

**変えていないこと**: SRT 8ms、モーター制御、`fdc_motor_off()` (未使用のまま)、既存 API のシグネチャ、`kernel/kernel.c`。

## 5. 検証の段取り

| ID | 見るもの | 手段 |
|---|---|---|
| R1 | ホスト TDD (判定関数) | コーダー |
| R2 | `make kernel` / `make all` / `make check` | テスター |
| R3 | **2HD FD 起動の回帰** (NP21/W): `root OK`、シェル、`/bin/cfg.bin` md5 一致 | テスター/PM |
| R4 | **1.44MB FD 起動の回帰** (NP21/W): 同上 | テスター/PM |
| R5 | HDD 起動の回帰 (fd0 は /fd0 にサブマウント) | テスター |
| **R6** | **実機で FD 起動 → `root OK` → シェル** | **ユーザー**。失敗時は `[fdc]` の行を写真で |

**エミュレータでは原因そのものは再現できない** (§2)。R3〜R5 は退行が無いことしか言わない。

### 5-1. 結果 (2026-09-22)

| ID | 結果 |
|---|---|
| R1 | **合格**。6 ケース、変異 4 本が全部 RED (`make check-fdc-seek-host`) |
| R2 | `make all` / `make fd144` / **`make check` (exit 0) 合格** (b299ea9、本体で実行。worktree は `.env` が無く `/usr/local/cross` 既定になるので全体ビルドには使えない) |
| R3 | **合格**。NP21/W を trial ini + `os32_boot.d88` 引数で起動 → `[fatfs] mounted: type=1 drv=0 pdrv=0 FAT12`、kselftest 85/85、`OS32 v1.0 (FDD Boot)`、`ver` の Build が新ビルド。`/bin/cfg.bin` (38,272B) がホスト原本と md5 一致、`/VMKRNL.LZ4` (484,794B、474 クラスタ) が**イメージ内のファイルと md5 一致** (原本との 2 バイト差はビルド時刻の秒。イメージ生成後に再リンクされたため)。`[fdc]` の失敗行は出ない。**往復 2 (b299ea9) でも再確認**: リセット後の画面に `[fdc] dma>1MB: 0439h ff -> ff` (新コードの印。NP21/W は 0439h を読めず FFh) → `[fatfs] mounted` → 85/85 → FDD Boot、`/VMKRNL.LZ4` (485,330B) がイメージ内容と md5 一致 |
| R4 | **未実施**。trial ツールが `.d88` しか引数に取れず、1.44MB は生 `.img`。手で挿入するか、ツールの拡張が要る |
| R5 | 未実施 (往復 2 の後、NHD 配備が要る → [D1]) |
| R6 | **未実施** (ユーザー) |

## 6. 独立レビュー (Codex `codex exec -s read-only`)

**往復 1 (b95479d)**: Request changes。PM が反例の到達可能性を確かめて全部採用:

| # | 所見 | 判断 |
|---|---|---|
| B1 (P1) | 最終 READ がタイムアウトしたまま戻ると DMA ch2 が動いたままで、次の WRITE が `dma_buffer` へ写した内容を遅れた旧転送が上書きし得る | 到達可能。最終失敗時に DMA マスク + FDC リセット (`fdc_abort_transfer`) |
| B2 (P2) | `fdc_wait_seek_end` の IRQ 待ちが 1 回だけなので、別ドライブの通知 (drv1 の Ready 変化) で自ドライブの正常なシークを PENDING で打ち切る | 稀だが到達可能。期限までループ |
| B3 (P2) | 媒体無しの `/fd0` 試行で NR 即失敗のたびに reset (NP21/W はリセット IRQ を出さない → 0.5s × 2) | 到達可能。NR は再試行しない + リセット待ち 100ms |
| 非 blocker | `fdc_read_results` の `i--` が無限になり得る / 診断の ST0 が SEEK のものでない / NP21/W の時間説明 (`FDC_INT_DELAY`=6 の後に 512 サイクル) | 全部直す |
| **非 blocker → 実機の第 2 原因** | **`I/O 0439h bit2` = 1MB 以上への DMA 禁止、ノーマルモードの起動時設定は 1** (`io_dma.md` 428 行〜)。`dma_buffer` は 0x156000 で 1MB 超。NP21/W は `necio_o0439` が値を保存するだけで DMA が見ない (`src/io/necio.c`) ので**エミュレータでは再現しない** | **blocker 4 として採用**: `fdc_init` で RMW して bit2 を落とす (bit7 はプリンタ I/F 選択なので他ビットを保つ)。**FFh でも書く** — 実機は bit7=1・bit2=1・未使用ビット 1 で FFh を返し得るので避けると直らない。読み戻しを `[fdc] dma>1MB: 0439h xx -> xx` で報告 (NP21/W は in ハンドラが無く `ff -> ff`) |

**往復 2 (b299ea9)**: **Approve**。往復 1 の blocker 3 件 + 非 blocker 4 件を 1 件ずつ「閉じた」と確認。
到達可能な反例を伴う blocker なし。非 blocker と PM の扱い:

| # | 所見 | 扱い |
|---|---|---|
| 1 | SIS 失敗時に `s_last_seek_st0` が更新されず、診断が前回の値 (例: 古い NR) を出し得る。結果が 1〜2 バイトのときの ST1/ST2 は 0 で埋まる | **残件** (診断の精度。動作には影響しない) |
| 2 | 「NR は再試行しない」が `fdc_init` の 100ms 待ち再 recalibrate と、`fdc_recover` 後の 1 回の SEEK には及んでいない | **残件** (空ドライブで数十 ms の余分。往復 1 の reset ×2 は復活しない) |
| 3 | コメント: 「Specify はリセットで消える」は誤り (SRT/HUT/HLT は RESET で保持)。「20〜40 シリンダなら 200ms で必ず」は強すぎる (20 × 8ms = 160ms) | **直した** (コメントのみ) |
| 4 | ホスト試験は判定関数だけで、待機ループ・abort の I/O 順序・NR 時の reset 回数は検査していない (記録は限界を明記済み) | **残件** (I/O を伴うので実機/エミュレータの領分) |

## 7. 実機での確認 (R6) の手引き

1. `make all` → `images/os32_boot.d88` (1.2MB、`images/os32_boot.img` が生イメージ) / `make fd144` → `images/os32_boot144.img` (1.44MB、生イメージ)。
2. FD から起動し、**画面の次の行を写す**:
   - `[fdc] dma>1MB: 0439h xx -> yy` — xx の bit2 が立っていて yy で落ちていれば 1MB 制限は解けた。`ff -> ff` のように落ちない機種なら別の手 (DMA バッファを 1MB 未満へ) が要る。
   - `MOUNT... root OK` と `[fatfs] mounted: type=1 drv=0 pdrv=0 FAT12`。
   - 失敗時: `[fdc] recalibrate drv=0 rc=.. st0=..` / `[fdc] read fail drv=0 chs=c/h/s phase=<seek|cmd|irq|result> st0=.. st1=.. st2=..` — phase と ST0 で原因が切り分けられる (seek + NR = Ready 不成立、irq = 転送が終わらない、result の ST1/ST2 = CRC/欠落など)。

## 8. 実機 R6 (2026-09-22、b299ea9 相当、1.2MB) — 不合格

写真: `DEV...OK` の右に赤い **`ER`** (= `fdc_init()` の失敗)、`MOUNT... root panic`。**`kprintf` の行が 1 行も無い**。

| 発見 | 内容 |
|---|---|
| ext2 は無関係 (ユーザーの再確認) | `root panic` はルート (fd0) の失敗でだけ出る。`/hd0` の ext2 を試すのは `root OK` の後 |
| **kprintf が実機で見えない** | `kprintf(0x07, …)` などの属性は PC/AT 流で、`tvram_putchar_at` が PC-98 の属性 VRAM へ直書き。PC-98 は bit0 = 表示なので 0x0A/0x0C/0x0E は非表示、0x07 は黒の反転。呼び出しは 0x07 が 73 か所。**エミュレータでは `/api/tvram` が文字コードを返すので誰も気づかなかった**。`/api/screenshot` で裏取り: 同じ瞬間の tvram に `[fdc]`/`[ide]`/`[fatfs]` の行があるのに、画面には 0xC1 (黄) の `[selftest]` と状態行しか映らない — 実機の写真と同じ姿 |
| **FRY (0x94 bit6) を立てていない** | `io_fdd.md` 191 行: RDY はドライブの RDY と FRY の OR。READY 線を出さないドライブでは FRY 無しだと µPD765A が全コマンドを NR で即終了。旧コードは `SE` だけ見て NR の recalibrate を成功扱い → READ で落ちて root panic。新コードは NR を即失敗 → `ER`。**両方の写真と整合**。NP21/W は `ctrlreg & 0x40` を見るが媒体入りは `fdd_diskready` で Ready → 露見しない |

往復 3: kprintf の属性変換 (+ kselftest)、`CTRL_FRY`、状態行 5 に `FDC rc= st0= 0439h=` を `tvram_print` で出す (kprintf に依らない保険)。

### 8-1. 往復 3 の結果 (6ee6fc0 + 状態行の移動)

| 項目 | 内容 |
|---|---|
| kprintf 属性 | `lib/kprintf_attr.c` `kprintf_attr_to_pc98()` (0x07→0xE1、0x0A→0x81、0x0C→0x41、0x0E→0xC1、PC-98 流は素通し)。kselftest 10 件 (85→95)、`check-kprintf-attr-host` (256 通りの不変条件、変異 6 本 RED)。**エミュレータの `/api/screenshot` で `[fdc]` `[ide]` `[fatfs]` が白で見えることを確認** |
| FRY | `CTRL_FRY` (0x40) を 0x94 の動作時の書き込み 3 か所に。NP21/W の副作用: 未搭載ドライブの RECALIBRATE が NR→EC (2 回出して失敗、`fdc_init` は無視)、搭載済み・媒体無しは NR→成功 (`FDC...ER` が `OK` に見える)。SEEK/READ の媒体検査は FRY を見ないので読み書きは従来どおり NR 即断 |
| 状態行 | `FDC rc= st0= 0439h=xx->yy` を**最下行 (行 24)** に `tvram_print` + 同じ内容を `[fdc] …` で kprintf。行 5 は IDE のログに root panic の前に上書きされた (Codex 往復 3 の blocker、エミュレータでも `ff->ff` の尻尾だけ残った) |
| 検証 | `make all` / `make fd144` / `make check` exit 0。NP21/W 2HD FD 起動: 行 24 に `FDC rc=0 st0=20 0439h=ff->ff`、`[fatfs] mounted FAT12`、シェル到達 |

**Codex 往復 3 (6ee6fc0)**: Request changes — blocker は状態行の上書き 1 件 (上のとおり直した)。非 blocker: FRY で実機の空ドライブは NR 早期終了が使えず READ の IRQ タイムアウト (1s × 3) まで待ち得る (有限、DMA マスク/リセットは残る) / `tv_cat` は [C2] の kstring 方針からずれる (境界違反なし) / 属性変換は CGA の背景色・点滅を一般には識別しない (現在の呼び出しに反例なし)。3 往復で Approve に至っていないので、争点 (状態行の置き場) の決着はユーザー判断に委ねる (ROLES §5)。

### 8-2. 実機で見てほしいもの (更新)

- 最下行の `FDC rc=<n> st0=<xx> 0439h=<xx>-><yy>`: rc=0 なら FDC 初期化は通った。st0 の bit3 (0x08) が立っていれば Not Ready (FRY でも直らない = ドライブ選択/モーターの問題)。0439h の yy で bit2 (0x04) が落ちていれば DMA の 1MB 制限は解けた。
- `[fdc] …` の白い行、`MOUNT... root OK`、`[fatfs] mounted`。

## 9. 実機 R6 — 合格 (2026-09-22、bee42cc、1.2MB)

- ユーザー報告: **FD からブートした**。シェルは起動直後に rshell (シリアル待ち) に入るので、
  シリアル未接続だと「ハング」に見える (ESC でローカルへ)。
- USB シリアル (FTDI、Windows の COM3) + クロスケーブルで、`tools/rshell_serial.py` (新規、
  Windows 側 Python + pyserial) から `ver` / `ls /` / `ls /bin` が返った。応答 296 バイト + EOT が 0.8 秒
  (約 490 B/s、9600bps)。Build は `Sep 22 2026 16:19:26` (bee42cc のビルド)。
- 原因は 3 つ重なっていた: §2 (シーク時間のタイムアウト) + §6 (`0439h` bit2 の DMA 1MB 制限) + §8 (FRY)。
  どれもエミュレータでは再現しない。**`0439h` の読み戻しと `FDC rc=` の行 (最下行) の実機の値は未記録** —
  次回の起動で写真を取る。
- 残件: R4 (エミュレータの 1.44MB 回帰)、R5 (HDD 起動の回帰、NHD 配備)、実機の 1.44MB 起動、
  Codex 往復 3 の非 blocker (§8-1)、`tools/rshell_serial.py` をテスターの「叩く側」に組み込む (PLAN §4 段 3)。

### 9-1. 実機で続けて分かったこと (2026-09-22)

| 件 | 内容 |
|---|---|
| シリアル速度 | Ra266 は `[ser] 9600bps (clk 2457600Hz, count 16)` = **2.4576MHz 系**。`serial 38400` (count 4) で切り替えて `ver` が返った。**ただし実効は 9600 で 490 B/s、38400 で 233 B/s** — 回線ではなくゲストの `serial_putchar` (TxRDY を 100 回スピン → `hlt` で次の 10ms tick まで待つ) が上限。速度を活かすには TxRDY のスピンを 1 文字時間ぶん (38400 なら 0.3ms) 取るか、送信割り込みで回す (残件)。19200 (count 8) も整数。57600 / 115200 は分周が割り切れないので `0434h` の 4 分周 (極性未決着、§4-50) が要る |
| **画面の桁ズレ** | 起動完了後の画面が行ごとに 1 文字 (8 ドット) ずつ右へずれる (滑らかな傾き)。root panic で止まった画面 (BIOS の同期のまま) にはずれが無い。OS32 の SYNC パラメータは資料の標準値 (`C/R=4Eh, HS=07, HFP=09, HBP=07, VFP=07, VBP=19h, L/F=190h`) と一致し、NP21/W も同じ値でテキスト幅を出す。**有力な原因**: `gfx/backend_pegc.c` の `pegc_shutdown` → `pegc_gdc_set_timing(400)` が 24kHz を強制 (`PEGC_HSYNC_24KHZ`) してテキスト GDC の SYNC を送り直し、液晶 (LCD172VXM) の自動調整が新しいタイミングに再ロックしていない。CRT では出ない型。**確認はモニタの自動調整を 1 回押す**。直すなら「480 ラインへ入らない起動では BIOS の同期に触らない」 |

## v3 の予定: トラック / シリンダ単位の読み出し (ユーザー決定 2026-09-23)

現行のカーネル FDC ドライバは Read Data の R = EOT = 同じセクタ番号で **1 コマンド 1 セクタ** (2HD で 1KB)。次のコマンドが間に合わないと 1 回転 (360rpm で約 167ms) を待つので、最悪 6KB/s 前後になる (推定、未測定)。
**v3 で 1 トラック (以上) を 1 コマンドで読む**: EOT = トラックの最終セクタ、MT=1 で同じシリンダのヘッド 1 まで続ける (2HD で最大 16KB)。根拠は Bible 2-9 の INT 1Bh データ読み出し (06h): BX = 転送バイト数、読み出しは指定シリンダ内、MT 指定でヘッド 0 → 1。**書き込みは MT 不可** (μPD765 が正しく動かない、同資料) なので 1 トラックずつ。DMA の受け皿は 16KB に広げ、64KB 境界 ([HW2]) と `0439h` (1MB 超の DMA、§4-51) を満たす場所に置く (DMA プール 0x2E8000 の span が候補、TASK_HAL_WIRING)。MS-DOS が BIOS を何バイト単位で呼んでいたかは資料に記述が無く未確認。
着手前に実機で FD 起動の所要時間を測って効果を見積もる。

### v3 の前倒し: トラック単位の読み出し (2026-09-24、ブランチ wt/fdc-multi)

実機で既定フォント (`/sys/font/default.kcg`、LZ4 で 188KB) の読み込みが 1 分以上止まったので、上の予定のうち
**トラック単位 (MT なし)** を先に入れた。**実機の速度はまだ測っていない** (机上の見積もり: 1 セクタ 1 回転なら
1KB あたり約 190ms、188KB で約 36 秒)。

> **2026-09-25 追記**: 既定フォントは MINIMAL から NORMAL へ移り、**起動 FD には載っていない**
> (`docs/08_build.md` の「起動 FD と MINIMAL」)。FD 起動の `[fdc] font: …` の行はフォントを
> 探して見つからなかった分しか数えないので、速度の目安にはならない。ホスト試験 `font_replay`
> は同じ読み方で FD に残る `/sys/unicode.bin` (128KB) を読む (`tools/tests/fdc_track_tdd.md`)。

| 件 | 内容 |
|---|---|
| シークの省略 | `drivers/fdc.c` がドライブごとに現在のシリンダを覚え、同じなら SEEK も 20ms の整定待ちも出さない (SIS の排水だけは出す)。**捨てる**: 読み書きの最終失敗・シークの失敗・まとめ読みの失敗・FDC リセット (回復を含む)・RECALIBRATE の失敗・`fdc_set_media` / `fdc_set_3mode`・前回と違うドライブを触ったとき。書き込みも同じ `fdc_seek` を通るので覚えた値を更新する。ずれていても READ / WRITE DATA が ID 部の C を照合するので別のシリンダは読み書きしない (WC で落ちて捨て、シークし直す) |
| まとめ読み | `fdc_read_sectors(drv, cyl, head, sect, count, geom, buf)`: 1 回の READ DATA で EOT = sect+count-1、MT=0、DMA 長 = count×bps (TC と EOT が最後のバイトで揃う — 単発で実機が通っている形を伸ばしたもの)。**リトライしない 1 回きり**で、失敗したら DMA を閉じ、FDC をリセットして RECALIBRATE まで済ませる (単発のリトライの間の `fdc_recover` と同じ形。リセット後の PCN を信じて別のシリンダへシークしないため) |
| 区切りと先読み | `drivers/fdc_track.c` (純粋な層) を `disk_read` (DRV_FDD) が呼ぶ。要求をトラックの境目で区切り、**要求したセクタから EOT までを読んで 1 トラック分持つ**。FatFs (`FF_FS_TINY=1`) の窓とフォントの 1024B ずつの読み (`kcg_read_chunked`、16B のヘッダの後なのでセクタに揃わない) は **count=1 で来る**ので、束ねるだけでは 1 セクタ 1 コマンドのまま変わらない。持っている中身は書き込みの前・`disk_initialize`・`diskio_set_fdd_drive` で捨て、ジオメトリが変われば当てない |
| 失敗の扱い | まとめ読みが失敗したら、**要求した区間だけ**を従来の `fdc_read_sector_geom` で 1 セクタずつ読み直す (リトライ・`fdc_recover`・NR の早期終了・0439h・FRY はそのまま)。先読みの分は読み直さない。先読みが落ちたトラックは 1 本だけ覚え、以後そこでは要求の範囲だけを束ねて読む (要求の外の傷で毎回失敗を踏まない)。失敗行 `[fdc] multi-read n=… falls back to single` は最初の 4 回だけ出す (実機で毎回落ちて速度が戻っていないかを見るため)。回数は `fdc_get_stats()` (当初の名前 `fdc_get_multi_stats` は廃止) |
| 時間上限 | `fdc_rw_timeout_ticks()` (`drivers/fdc_decide.c`): 2 × (2 回転 + ceil(count/spt) 回転 + HLT 10ms)、回転は 300rpm の 200ms。1 トラック全部で 1.22 秒、単発の 1 秒を下限。シークは別に 1.5 秒 |
| DMA の受け皿 | **9ed7c80 の当時の形。後に変更 (下表の「受け皿」: 1 本の静的な領域、DMA の窓は `fdc_buf_layout()` が実行時に決める。16KB 揃えは廃止)**。`dma_buffer` を 1 セクタ (1KB) から **1 トラック分 9216B** (1.44MB の 18×512 が最大) に広げ、**16KB 境界に揃えた** (2 の冪の揃え ≧ 大きさ なので 64KB 境界をまたがない、`fdc.h` の STATIC_ASSERT)。DMA プール (0x2E8000) は使わない — `fdc_init()` が `dma_pool_init()` より前に走り、`kselftest_run()` がプールを作り直すため。カーネルの `__bss_end` は 0x179400 → 0x184800 (+45KB、揃えの詰め物を含む)、リンク時の上限 (`MEM_KERNEL_IMAGE_MAX`) の中 |
| MT | 使わない (Bible 2-9 は読みの MT を許すが書き込みの MT を禁じる、読みの MT を実機で確かめた記録が無い、得はシリンダごとに最悪 1 回転)。上の v3 の予定として残す |
| ローダ | `boot/` の FAT 版ローダは触っていない。`boot/loader_fat.asm` / `loader_fat_new.asm` の `read_sect16` は **INT 1Bh (AH=76h = SEEK あり・MT なし) を 1 セクタ (BX = 1 セクタ長) ずつ**呼んでいるので、カーネルの読み込みも 1 セクタ 1 回転に近い形の可能性がある (未測定、見直しは別件) |
| 試験 | `make check-fdc-track-host` (`tools/tests/test_fdc_track.py --target --mutate`、記録 `tools/tests/fdc_track_tdd.md`)。本物の `fdc.c` を µPD765A の模型の上で回す。変異 21 本: RED 20 / ERROR 0 / SURVIVED 1 (対照) |
| 未確認 | 実機の速度、NP21/W での FD 起動、実機で MT なしのまとめ読みが正常終了すること |

**9ed7c80 の NP21/W での結果と直し (2026-09-24 夕)**: フォントの読み込みは約 3 分のまま (4a8fad4 と同じ)、EIP は全部 `fdc_wait_seek_end` の IRQ 待ち。

| 件 | 内容 |
|---|---|
| 主因 | FatFs (`FF_FS_TINY=1`) は FAT とデータで 1 つの窓を取り合い、クラスタ (2HD は 1 セクタ) を越えるたびに FAT のセクタ (シリンダ 0) を読み直す。1KB ずつ 16B ずれて読むフォントは毎回越えるので **1KB ごとにシーク 2 回**。先読み 1 本では FAT とデータのトラックが追い出し合って当たらなかった → **2 本 (LRU)** に |
| 完了待ち | SIS が別ドライブの通知 / Ready 変化を返したとき 1 件で次のエッジを待っていた。INT 線は pending が尽きるまで上がったままなので、実機では自分の完了を 1.5 秒の期限まで待って救済で拾う形になる → 1 本のエッジで 80h まで読む。NP21/W は事象ごとに IRQ を出すので、エミュレータでの寄与は `[fdc] font:` の `tmo=` で見る |
| IF=0 | 0x244 は IF=1 (0x200 + ZF + PF)。割り込み禁止の区間ではない |
| 世代 | 先読みは `fdc_media_gen()` の世代で外す。書き込み全部 (FatFs / dev.c / KAPI の `dev_blk_write`)・Ready 変化・`fdc_set_media` で進む |
| 受け皿 | 1 本の静的な領域 27KB (DMA の窓 1 + スロット 2)、窓の位置は `fdc_buf_layout()` が実行時に決めて 64KB をまたがない。揃えの詰め物は無くなり `__bss_end` 0x184800 → 0x180780 (4a8fad4 比 +29KB) |
| NR | まとめ読みの失敗に数えず行も出さない (-3) |
| 診断 | 起動時に `[fdc] font: seek= skip= recal= tmo= foreign= rdy= multi=ok/fail nr= single= retry= write=` を 1 行。`time:1ms` の FAIL には測った値を出す行を足した (原因は未特定、下) |
| time:1ms | `cpu_delay_us(1000)` を `sys_time_now` で挟む試験。校正 (`cpu_calibrate`) は FDC より前に 1 回だけで、FD の新しいコードは割り込みを禁止しない。NP21/W の CPU 速度はホストの負荷で揺れるので校正と実測がずれた可能性があるが**未確認**。次の起動で `[selftest] time: 1ms delay measured` の値を見る |
| 試験 | 21 ケース、変異 33 本: RED 32 / ERROR 0 / SURVIVED 1 (対照) |

**6541ef1 の NP21/W での結果と直し (2026-09-24 夜)**: フォントの読み込み約 92 秒 (半分に)、kselftest 212/0、`[fdc] font: seek=751 skip=16 recal=2 tmo=0 foreign=0 rdy=0 multi=767/0`。

| 件 | 内容 |
|---|---|
| 原因 | **VFS が 1 回の読みごとにファイルを開き直す** (`fatfs_vfs_read_stream` が毎回 `f_open` → `f_lseek` → `f_read` → `f_close`)。1KB ごとにルート (C0H0)・`/SYS` (C0H1)・`/SYS/FONT` (C62H0)・FAT (C0H0)・データの 4 本のトラックを巡回し、2 本の LRU は 1 回も当たらない。世代・ポインタ・範囲は無実 |
| 直し | count=1 の読み (FatFs の窓) を覚える 8 セクタの LRU を足し、トラックのスロットは 1 本に。受け皿は 26KB (窓 9KB + トラック 9KB + セクタ 8KB)。VFS の開き直し自体は触っていない (別件にするなら、チャンクごとに FAT の鎖を先頭からたどる CPU の無駄もある) |
| 再現 | `font_replay`: `images/os32_boot.d88` と実物の `ff.c` で、`kcg_load_font` と VFS の読み方のまま回す。6541ef1 の形では seek=752 / multi=769 (実測 751 / 767 と一致)、直した後は **seek=14 / multi=27 / single=0 / tmo=0**。試験で multi ≦ 27、seek ≦ 14 に固定 |
| 見込み | NP21/W のシーク 1 回約 100ms なら、フォントのシーク待ちは 75 秒 → 1.4 秒程度 (机上。実測は PM) |
| 表示 | `[fdc] font:` を 3 行に (シーク系 / 読み系 / キャッシュ)。どれも 80 桁以内 |
| 試験 | 22 ケース、変異 35 本: RED 34 / ERROR 0 / SURVIVED 1 (対照) |

**6fa2ec7 の結果とラリー 2 (2026-09-24 夜)**: NP21/W の FD 起動でフォントの読み込み 3 秒、seek=15、multi=28、kselftest 212/0。ラリー 2 は Codex・Fable とも Request changes (指摘は一致)。

| 件 | 直し |
|---|---|
| RECALIBRATE の後の整定 [両者] | RECALIBRATE が通ったら `fdc_head_settle()` (SEEK と同じ 2 tick) を待ってから 0 を覚える。回復の直後の C=0 の READ が整定前に出ていた |
| 同じ形式の媒体の差し替え [両者] | **2 秒規則**: `fdc_track_read` の入口で、最後の読みから 200 tick を超えていたらトラックとセクタの両方を捨てる (時計は `ops->now`、カーネルは `tick_count`)。pending の Ready 通知を SIS で読む前に当たる件 (Codex) もここで上限が付く。契約「媒体を替えたら umount / mount」を docs/06_filesystem.md §6-8 に書いた |
| NR [Codex] | 単発の READ のリザルトに NR が立っていたら回復・リトライより前に打ち切る (DMA を閉じるだけ)。まとめ読みのリザルトの NR も回復せずに -3。`fdc_track` は -3 なら 1 セクタずつへ落とさない |
| SIS の件数 [Fable] | 1 本のエッジで 4 件読んでも 80h が出ていなければ、エッジを待たずに読み続ける (時間で縛る) |
| まとめ読みのシーク失敗 [Fable] | -2 / -4 は `fdc_recover` (リセット + RECALIBRATE) を通してから単発へ |
| リザルトまで読めた失敗 | コマンドは終わっているので DMA を閉じるだけでリセットしない (覚えたシリンダは捨てる) |
| `[fdc] font:` [Fable] | FD 起動 (boot_drive 0x90 / 0x30) のときだけ出す。キャッシュの行に `idle=` (2 秒規則で捨てた回数) |
| extern [Fable] | `fs/fatfs/diskio_os32.h` を新設 (diskio.h は ff.h の型が要る FatFs の配布物のため)。kernel.c と fatfs_vfs.c の extern を移した |
| 試験の依存 [Fable] | `check-fdc-track-host` を `images/os32_boot.d88` に依存させ、`--require-image` で無ければ FAIL (SKIP にしない) |
| 試験 | 27 ケース、変異 41 本: RED 40 / ERROR 0 / SURVIVED 1 (対照) |
| WRITE の NR (後追い、Fable minor) | 単発の WRITE もリザルトの NR で回復・リトライより前に打ち切る (READ と同じ、DMA を閉じるだけ)。ケース `write_nr_no_recover` と変異 1 本を足して 28 ケース、変異 42 本: RED 41 / ERROR 0 / SURVIVED 1 (対照) |
