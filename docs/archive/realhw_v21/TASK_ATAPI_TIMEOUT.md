# TASK_ATAPI_TIMEOUT — ATAPI の待ち上限を秒単位にする

> 状態: **受入完了 (2026-09-26)** — be184faa で合流、P3 の後始末 b8f76e04。T1・T2 合格。**T3 (CD を 5 分放置した後の `ls /cd0`) は個別の記録が無い** — 実機の v2.1 確認 (CHECKLIST_2026-09-26 手順 6・7) で CD の経路は合格。(2026-09-29 の棚卸しで更新)
>
> それまでの状態: **実装済み・レビュー待ち** (wt/atapi-timeout、コーダー `claude-opus-5-5`、2026-09-26。T1・T2 済、T3 は実機)。発行: PM (Claude Code `claude-opus-5-5`)、2026-09-26。
> 関係: `drivers/atapi.c` / `drivers/ide.h` (`IDE_TIMEOUT_LOOP`)、[`../../05_drivers.md`](../../05_drivers.md) §5-6、
> ATAPI 装置選びの修正 (wt/cd-fix、2026-09-26 着地) の代行レビュー (Fable 5.1) の P3。

## 0. 問題

ATAPI の待ち (BSY / DRQ の解除) の上限が **`IDE_TIMEOUT_LOOP` = 1,000,000 回の `inp`** で、時間ではなくバスの速さで決まる。
Ra266 では 0.5〜1 秒の桁。実機の CD ドライブは次の場面でこれより長く BSY のままになりうる:

| 場面 | 規定・実機での長さ | 今の扱い |
|---|---|---|
| 回転が止まった後の最初の READ(10) (スピンアップ) | 2〜4 秒 | 期限切れ → 1 セクタずつ → `atapi_recover` が**健全な装置のコマンドを DEVICE RESET で捨てる** → UA → 出し直し。読みが 1 回失敗して上の層へ返る |
| SRST の後 (`atapi_srst`) | ATA は最大 **31 秒**を許す | 2ms + 約 1 秒で諦め、戻り値も捨てている。遅い装置では BSY 中に DRV_HEAD を書く (2 回目のレビューの P3 が後ろへずれただけ) |

## 1. やること

1. 待ちの上限を **`tick_count` の秒単位**にする。割り込みが閉じている場面 (起動時の `atapi_init`、KAPI 経由の読み) では tick が進まないので、
   `cpu_delay_us` (校正済みの空回し、1 回 100ms まで) を塊で回す時間に切り替える (`atapi_delay_us` の流儀)。どちらを使うかを場面ごとに決める。
2. 上限: 通常の PACKET は 5〜10 秒、**`atapi_srst` の BSY 待ちは 31 秒** (他より長く、規定の最大)。`atapi_srst` の戻り値を呼び手で見る。
3. 「遅いだけ」と「固まった」の境目を秒で持ち、DEVICE RESET はその境目を越えたときだけにする。
4. ホスト試験の模型にスピンアップ (READ の後しばらく BSY) と、SRST 後の長い BSY を足す。

## 2. 同じ機会に見ること (実機で踏んだら)

- 媒体なしを **ASC 3Ah 以外**で返す装置 (例: 04h/02h「initializing command required」= START UNIT が要る) は、待っても変わらないのに
  `atapi_read_capacity` (mount / cdinst) のたびに 5 秒待って媒体なしになる。ASC/ASCQ を診断行に出し、04h/02h には START STOP UNIT を出す。
- 装置が 2 台あると `atapi_init` の待ちは最大 10 秒 (1 台 5 秒 × 2)。

## 3. 受け入れ

- T1: ホスト試験で、スピンアップ 3 秒の READ が 1 回目で読める (DEVICE RESET 0 回)。SRST 後 10 秒 BSY の装置を待ちきる。
- T2: 起動の最悪時間 (装置 2 台とも応答なし) を計算して票と docs/05_drivers.md に書く。
- T3: 実機 Ra266 で、CD を 5 分放置した後の `ls /cd0` が 1 回で通る (`atapi_get_stats` の dev_resets が増えない)。

## 4. 実装 (2026-09-26)

### 待ち方

- 時計は **`cpu_delay_us` の 1 本だけ**。`atapi_wait_clear(mask, limit_us)` が ALT_STATUS を 1 回読むごとに
  `cpu_delay_us(ATAPI_POLL_US = 100µs)` を挟み、挟んだ時間の合計で上限を数える (`atapi_wait_bsy` / `atapi_wait_idle` /
  `atapi_wait_drq` / SRST の待ち)。すぐ落ちていれば待たない。
- **tick_count と使い分けない理由**: 起動時の `atapi_init` と KAPI 経由の読みのどちらでも、PIT の割り込みが来ている
  (IF=1) と決められない。場面ごとに時計を替えると、ホスト試験で見えない経路ができる。`cpu_delay_us` の誤差 ±10% と
  読みの間の inp の時間 (上限を長い側へ約 1% ずらす) は、上限を規定・実測の 2 倍以上に取って吸う。
  `cpu_calibrate` より前に呼ばれると `cpu_delay_us` は待たない (上限は 10 万回の読み ≒ 0.1 秒に縮む) — `atapi_init` は
  `cpu_calibrate` の後 (`kernel/kernel.c`)。

### 上限と根拠 (`drivers/atapi.h`)

| 定数 | 値 | 使う場面 | 根拠 |
|---|---|---|---|
| `ATAPI_CMD_TIMEOUT_US` | 10 秒 | 通常の PACKET、装置の選択、DEVICE RESET の後 | スピンアップ 2〜4 秒の 2 倍 + 余裕。-10% に外れても 9 秒 |
| `ATAPI_INIT_TIMEOUT_US` | 5 秒 | `atapi_init` の間だけ (シグネチャ、2 台のときの容量確認) | スピンアップ 4 秒は待ちきり、起動の最悪時間を抑える。電源投入直後の長い BSY は SRST の 31 秒が受け持つ。init で期限切れになった遅い装置も以後は 10 秒で待つ |
| `ATAPI_SRST_TIMEOUT_US` | 31 秒 | SRST の後のマスターの BSY | ATA の規定の最大 |

### 「遅いだけ」と「固まった」の境目

コマンドの待ちが期限切れになっても DEVICE RESET はせず、期限切れを返す。次のコマンドの装置選択 (`atapi_select_device`) で
**さらに上限まで待っても** BSY / DRQ のときだけ DEVICE RESET (`atapi_recover`)。READ(10) の経路では、複数セクタの
READ(10) が 10 秒で期限切れ → 1 セクタずつの 1 本目の選択で 10 秒 → そこで初めてリセット、つまり **BSY が 20 秒続いた装置
だけ**がリセットされる。スピンアップ (2〜4 秒。20 秒未満なら 1 セクタずつの 1 本目で読める) ではリセットしない。
`atapi_srst` は int を返し、31 秒で BSY が落ちなければ `atapi_recover` は DRV_HEAD を書かずに期限切れ、`atapi_init` は
シグネチャの再確認をせず「CD なし」。

### §2 (04h/02h)

`atapi_capacity_ready` の NOT READY で ASC 04h / ASCQ 02h なら START STOP UNIT (開始、IMMED=0) を**1 回だけ**出し、
250ms 待たずに出し直す。準備中のまま諦めたら `[atapi] NOT READY, gave up drv= st= asc/ascq=04/01` の形で最後の
ASC/ASCQ を 1 行出す (START UNIT を出したときも 1 行)。DEVICE RESET と SRST の期限切れも 1 行ずつ。どれも
`ATAPI_DIAG_MAX` (8) 行まで。`AtapiStats` に `start_units` を足した (KAPI 外、カーネル内だけの構造体)。

### T2: 起動の最悪時間 (`atapi_init`)

`cpu_delay_us` が数える時間。ホスト試験 `np2_boot_worst` が同じ数に固定している (誤差 ±10% と inp の +1% は別)。

| 形 | 内訳 | 合計 |
|---|---|---|
| 装置なし (浮いたバス 0xFF) | SRST の 2ms | 0.002 秒 |
| 2 台とも電源投入から BSY のまま (シグネチャも出ない) | マスター 5 + スレーブ 5 + SRST 0.002 + SRST 後 31 | **41.002 秒** で「CD なし」 |
| 2 台ともシグネチャは出るが最初の PACKET で固まる | 容量確認 5 + スレーブを選ぶ前 5 + DEVICE RESET の後 5 + SRST 0.002 + 31 | **46.002 秒** で「マスター」(上限) |
| 2 台とも準備中 (04h/01h) が続く | 250ms × 20 × 2 台 | 10 秒 (以前どおり) |

§2 の「装置が 2 台あると最大 10 秒」は準備中の待ちの数で、固まった装置の数は上の表。居ない装置の ALT_STATUS が BSY に
見える機械 (0x80 など、`np2_absent`) では、居る装置が 1 台でもあればシグネチャの確認が居ない装置の分だけ 5 秒延びる
(以前は 1 秒弱)。**1 台も居なければ 41.002 秒** — シグネチャが出ないので SRST し、`s_present_mask` が空のまま 0x80 を
「マスターの BSY」と見て 31 秒待つ (表の「2 台とも BSY」と同じ内訳。`np2_boot_worst` の (c) 5 秒 / (d) 41.002 秒で固定)。
スレーブの SRST 後の BSY は `atapi_srst` では待たず、選び直した後の待ち (`s_wait_limit_us`) までしか待たない。

### 着地後の直し (2026-09-26、代行レビューの P3)

- **バスが死んだ印**: SRST が 31 秒で期限切れになったら `s_bus_dead` を立て、以後の `atapi_read_sectors` /
  `atapi_read_capacity` / `atapi_test_unit_ready` はバスに触らず即 `ATAPI_ERR_TIMEOUT` (それまでは `ls /cd0` のたびに約 61 秒 =
  選択の前後 10 + 10、DEVICE RESET の後 10、SRST 31)。解くのは次の `atapi_init` だけ (呼び直しがバスの再試行。前回の
  `cdrom_present` も持ち越さない)。診断は SRST の行 `[atapi] SRST: BSY did not clear, bus marked dead ... limit=31s` と、
  最初に断ったときだけの `[atapi] bus dead since SRST timeout, failing at once until atapi_init ...` の 1 行。断った数は
  `AtapiStats.dead_fails`。SRST の行の `limit=` は PACKET の上限でなく `ATAPI_SRST_TIMEOUT_US` を出す (`atapi_note_ex`)
- **未実施 (計測待ち)**: データ相のブロック間の 100µs 待ち。実機 T3 で cdinst が遅く見えたら、最初の数百回は待たずに読む

### ホスト試験 (`tools/tests/test_cd_read.py`、記録 `tools/tests/cd_read_tdd.md` §3-6)

模型 (strict) に時計 `np2_now()` (贋 `cpu_delay_us` の合計 + ステータスの読み 1 回 1µs) を入れ、秒で見せる BSY を足した。

- `np2_spinup` — **T1**: スピンアップ 3 秒・9 秒の READ(10) が 1 回で読める、DEVICE RESET 0 回。25 秒はリセット 1 回、
  それは BSY が上限を越えた後。PACKET から CDB の DRQ までの 3 秒も待つ
- `np2_srst_long` — **T1**: SRST 後 10 秒・25 秒の BSY を待ちきって読む (BSY の装置へ DRV_HEAD を書かない)。40 秒は 31 秒で
  諦め、DRV_HEAD も PACKET も書かずに期限切れ
- `np2_boot_worst` — **T2**: 上の 41.002 / 46.002 秒
- `np2_start_unit` — §2: START UNIT 1 回で読める / 直らない装置には 1 回だけ出して媒体なし + ASC/ASCQ の行
- `np2_bus_dead` — 着地後: SRST の 31 秒切れの後、読み・容量確認・TEST UNIT READY は時計を進めず PACKET / DEVICE RESET /
  SRST を 1 つも出さずに期限切れ、即失敗の行は 1 回、`atapi_init` の後は読める。変異 6 本 (印を立てない、読み / 容量確認が
  印を見ない、init が解かない、行を毎回出す、SRST の行の limit= を PACKET の上限にする) が RED
- 変異 12 本 (上限を回数に戻す × 2、PACKET 上限 1 秒、SRST の戻り値無視、SRST を PACKET の上限で諦める、init の SRST の
  戻り値無視、init も 10 秒、init 後に上限を戻さない、START UNIT を出さない / 何度も出す、ASCQ を読まない、ASC/ASCQ の
  行を出さない) を足して全 73 本 RED

### T3 (実機で見ること)

Ra266 で CD を入れて 5 分放置 → `ls /cd0` が 1 回で通り、`atapi_get_stats` の `dev_resets` / `soft_resets` が増えない
こと、`[atapi] DEVICE RESET` の行が出ないこと。所要時間 (スピンアップの秒数) も見る。
