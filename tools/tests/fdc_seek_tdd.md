# FDC のシーク判定 — 実機のタイムアウトと割り込みの回収 (ホスト試験の記録)

- 症状: 実機 **PC-9821Ra266** で FD (1.2MB 2HD / 1.44MB とも) から起動すると
  カーネルの `MOUNT...` で **`root panic`** になる。IPL とローダは BIOS の
  INT 1Bh で読めているので、カーネル自前の FDC ドライバの側で失敗している
- 対象: [`drivers/fdc.c`](../../drivers/fdc.c) / [`drivers/fdc.h`](../../drivers/fdc.h) と、
  そこから切り出した純粋な判定 [`drivers/fdc_decide.c`](../../drivers/fdc_decide.c)
- 実行: `python3 -B tools/tests/test_fdc_seek.py [--target] [--mutate] [case ...]`
  (`make check-fdc-seek-host` が `--target --mutate` 付きで回す)
- 試験: [`fdc_seek_host.c`](fdc_seek_host.c) — 実物の `drivers/fdc_decide.c` を
  1 行も写さずに `#include` する。判定は I/O も `tick_count` も触らないので
  **模型が 1 つも要らない**
- KAPI は動かしていない。既存 API (`fdc_read_sector` / `_geom`、
  `fdc_write_sector` / `_geom`、`fdc_init`、`fdc_set_media*`、`fdc_get_geom`、
  `fdc_irq_fired`) のシグネチャもそのまま

## 1. なぜエミュレータで見つからなかったか

`FDC_IRQ_TIMEOUT_TICKS = 20` (200ms) は **NP21/W に合わせた値**だった。
エミュレータはシークを即時完了する — `np21w-src/src/io/fdc.c` の `fdc_intwait`
は 512 サイクル後に割り込みを上げる — ので 200ms で足りていた。

実機の機構はそうではない。

| 動作 | 実機の所要 | 旧 200ms |
|---|---|---|
| RECALIBRATE / SEEK | SRT 8ms × 最大 80 トラック = 640ms + セトリング | **必ず足りない** |
| READ / WRITE DATA | 最大 2 回転 (300rpm で 400ms) + ヘッドロード + 転送 | ぎりぎり |

ローダが `VMKRNL.LZ4` を読んだ直後のヘッドはシリンダ 20〜40 付近に居るので、
`fdc_init()` の RECALIBRATE は **毎回** 200ms で諦めていた。

そして **タイムアウト後の回収が無かった**。遅れて来た seek-end 割り込みを
SENSE INTERRUPT STATUS で読み出さないまま次の SEEK を出すと、µPD765A の INT 線
が上がりっぱなしになり、**エッジトリガの PIC に次のエッジが来ない**。以後
`fdc_wait_irq` が全部タイムアウトする → 3 回リトライしても読めない →
`RES_ERROR` → `f_mount` 失敗 → `root panic`。これが連鎖の全体。

## 2. ホストで試験できる形に切り出した 2 つの判断

副作用のある部分 (ポート I/O・tick 待ち) は実機でしか動かないが、
**「ST0 をどう読むか」だけは純粋**なので `drivers/fdc_decide.c` に分けた。
そこが実機でしか踏まない分岐そのものだから。

| ID | 見るもの | どう書いたか |
|---|---|---|
| F1 | pending 無しの SIS は **ST0 だけの 1 バイト応答** | `fdc_sis_result_bytes(st0)` が 80h に 1、それ以外に 2 を返す |
| F2 | RECALIBRATE の **EC は失敗ではなく再試行の合図** | `fdc_classify_seek_end()` が `FDC_SEEK_RETRY_EC` を返す |
| F3 | **未完了 (ST0=80h) と失敗を区別する** | 同じく `FDC_SEEK_PENDING` を別の値で返す |
| F4 | 成功に倒さない ([V4]) | Ready 変化 / 理由不明の異常終了は `FDC_SEEK_FAIL` |
| F5 | **NR は他の失敗と分ける** | `FDC_SEEK_NOT_READY`。媒体もドライブも無いのは回復で直らないので、呼び出し側がリトライも `fdc_recover` も飛ばす |

ケースは 6 本。

| ケース | 中身 |
|---|---|
| `sis_len` | 80h は 1 バイト、20h / 25h / 60h / 70h / C0h / C1h / 00h は 2 バイト |
| `seek_ok` | SEEK の正常完了と PCN 照合、RECALIBRATE (`want_cyl < 0`) は PCN を見ない |
| `seek_ec` | 70h / 71h、および SEEK 側で EC が立った場合も再試行 |
| `seek_pending` | 80h は `want_cyl` の有無によらず `FDC_SEEK_PENDING` |
| `seek_fail` | SE なし / 理由不明の異常終了 (60h) / Ready 変化 (C0h・E0h) |
| `seek_not_ready` | NR を他の失敗と分ける。SE の有無・EC との同時発生・80h との優先順位 |
| `real_hw_story` | 実機の事故の筋書きを順に並べたもの (取りこぼし → 未完了 → EC → 完了、と排水ループの停止条件) |
| `other_drive` | 期限内に別ドライブの通知が割り込む筋書き。3 つの結果が別々の値であること |

### F1 は NP21/W のソースで裏を取った

`np21w-src/src/io/fdc.c` の `FDC_SenceintStatus()` (cmd 08) は、4 ドライブ分の
`fdc.stat[]` が全部 0 のとき

```c
	if (!fdc.bufcnt) {
		fdc.buf[0] = FDCRLT_IC1;
		fdc.bufcnt = 1;
	}
```

とする。`FDCRLT_IC1` は `np21w-src/src/io/fdc.h` 38 行で `0x000080`。
つまり **ST0 = 80h を 1 バイトだけ**返し、µPD765A のデータシートどおり。
直す前の `fdc_sense_interrupt()` は ST0 と PCN を無条件に 2 バイト読んでいたので、
**排水ループは pending が尽きる回に必ず 1 度、来ないバイトを
`FDC_TIMEOUT_LOOP` (10000) 回空転して待っていた**。実機・エミュレータの両方で。

`C0h` (IC=11b, Ready 線が変化) を 80h と一緒に 1 バイト扱いにしないことも
`sis_len` で見ている。これを IC の bit7 だけで判定すると PCN が FIFO に残り、
以後のリザルトが 1 バイトずつずれる。

## 3. RED → GREEN

### RED (判定を「今の `drivers/fdc.c` の判断」のまま関数にした状態)

```
$ python3 -B tools/tests/test_fdc_seek.py
HOST GNU89 -Werror compile PASS (real drivers/fdc_decide.c)
FAIL sis_len:30: fdc_sis_result_bytes(0x80) == FDC_SIS_LEN_INVALID
EXIT sis_len=1
PASS seek_ok
EXIT seek_ok=0
FAIL seek_ec:77: fdc_classify_seek_end(0x70, 0, -1) == FDC_SEEK_RETRY_EC
FAIL seek_ec:78: fdc_classify_seek_end(0x71, 0, -1) == FDC_SEEK_RETRY_EC
FAIL seek_ec:81: fdc_classify_seek_end(0x70, 0, 40) == FDC_SEEK_RETRY_EC
EXIT seek_ec=1
FAIL seek_pending:91: fdc_classify_seek_end(0x80, 0, 40) == FDC_SEEK_PENDING
FAIL seek_pending:92: fdc_classify_seek_end(0x80, 0, -1) == FDC_SEEK_PENDING
EXIT seek_pending=1
FAIL seek_fail:100: fdc_classify_seek_end(0x28, 40, 40) == FDC_SEEK_FAIL
FAIL seek_fail:101: fdc_classify_seek_end(0x68, 0, -1) == FDC_SEEK_FAIL
FAIL seek_fail:104: fdc_classify_seek_end(0x60, 0, -1) == FDC_SEEK_FAIL
FAIL seek_fail:105: fdc_classify_seek_end(0x60, 40, 40) == FDC_SEEK_FAIL
FAIL seek_fail:108: fdc_classify_seek_end(0xE0, 0, -1) == FDC_SEEK_FAIL
EXIT seek_fail=1
FAIL real_hw_story:124: fdc_classify_seek_end(0x80, 0, -1) == FDC_SEEK_PENDING
FAIL real_hw_story:126: fdc_classify_seek_end(0x70, 0, -1) == FDC_SEEK_RETRY_EC
FAIL real_hw_story:133: fdc_sis_result_bytes(0x80) == FDC_SIS_LEN_INVALID
FAIL real_hw_story:134: fdc_classify_seek_end(0x80, 0, -1) == FDC_SEEK_PENDING
SUMMARY 1/6 PASS
```

通った 1 本 (`seek_ok`) は正常系だけを見ているケース。**エミュレータで
確かめられるのはここまでだった**、というのがこの RED の意味でもある。

`seek_fail` の `0x28` (SE=1, NR=1, PCN 一致) が落ちているのが分かりやすい:
古い判定は「SE が立っていて PCN が合えば成功」なので、**ディスクが入って
いないドライブのシークを成功と答えていた**。

### GREEN

```
$ python3 -B tools/tests/test_fdc_seek.py --target
HOST GNU89 -Werror compile PASS (real drivers/fdc_decide.c)
TARGET i386-elf GNU89 -Werror PASS
PASS sis_len / seek_ok / seek_ec / seek_pending / seek_fail / real_hw_story
SUMMARY 6/6 PASS
```

### 往復 2 の RED → GREEN (独立レビューの指摘を受けて)

レビュー (Codex) が **NR を普通の失敗と混ぜている** ことを挙げた。
HDD 起動時の `/fd0` サブマウント試行は空のドライブを読みに行くので、
SEEK が NR で即失敗しても残り 2 試行のたびに `fdc_recover`
(リセット待ち + recalibrate) を踏む。NP21/W はこの経路でリセット IRQ を
出さない (`necio` / `fdc_o94` はモーター ON を同時に書いたときしか割り込みを
積まない) ため、**毎回まるまる空待ちする**。

`FDC_SEEK_NOT_READY` を足して `seek_not_ready` と `other_drive` を書いた。
往復 1 の実装 (`drivers/fdc_decide.c` @ b95479d) に当てた RED:

```
$ python3 -B tools/tests/test_fdc_seek.py
HOST GNU89 -Werror compile PASS (real drivers/fdc_decide.c)
PASS sis_len / seek_ok / seek_ec / seek_pending / seek_fail
FAIL seek_not_ready:101: fdc_classify_seek_end(0x28, 40, 40) == FDC_SEEK_NOT_READY
FAIL seek_not_ready:102: fdc_classify_seek_end(0x28, 0, -1) == FDC_SEEK_NOT_READY
FAIL seek_not_ready:105: fdc_classify_seek_end(0x68, 0, -1) == FDC_SEEK_NOT_READY
FAIL seek_not_ready:106: fdc_classify_seek_end(0x69, 0, -1) == FDC_SEEK_NOT_READY
FAIL seek_not_ready:108: fdc_classify_seek_end(0x68, 40, 40) == FDC_SEEK_NOT_READY
FAIL seek_not_ready:111: fdc_classify_seek_end(0x48, 0, -1) == FDC_SEEK_NOT_READY
FAIL seek_not_ready:112: fdc_classify_seek_end(0x08, 0, -1) == FDC_SEEK_NOT_READY
FAIL seek_not_ready:114: fdc_classify_seek_end(0x78, 0, -1) == FDC_SEEK_NOT_READY
PASS real_hw_story
FAIL other_drive:182: fdc_classify_seek_end(0x69, 0, -1) == FDC_SEEK_NOT_READY
SUMMARY 6/8 PASS
```

`other_drive` が 1 件だけ落ちているのは、**期限内に別ドライブの通知が
割り込む筋書きそのものは往復 1 の判定でも表現できていた**ため。
足りなかったのは「その通知が NR だったときに、自ドライブの待ちを
続けてよいと分かること」だけだった。

GREEN は `SUMMARY 8/8 PASS`、`TARGET i386-elf GNU89 -Werror PASS`。

## 4. 否定側 (`--mutate`) — 5 本、すべて RED

| # | 変異 | 落ち方 |
|---|---|---|
| 1 | pending 無しでも PCN を読みに行く (= 直す前の姿) | 2 件 |
| 2 | EC を失敗として扱う | 2 件 |
| 3 | 未完了 (ST0=80h) を失敗と区別しない | 2 件 |
| 4 | Not Ready を見ない (ディスク無しを完了にする) | 2 件 |
| 5 | NR を普通の失敗と混ぜる (空ドライブで回復を 3 回踏む) | 2 件 |

## 5. 純粋でない側で直したこと (ホストでは試験していない)

判定の外側は実機でしか確かめられない。何を変えたかだけ残す。

- **タイムアウトを用途別にした** — `FDC_SEEK_TIMEOUT_TICKS` 150 (1.5s)、
  `FDC_RW_TIMEOUT_TICKS` 100 (1s)、`FDC_RESET_TIMEOUT_TICKS` 50。
  根拠は `drivers/fdc.h` の各定数の隣に書いた ([C4])。
  旧 `FDC_IRQ_TIMEOUT_TICKS` は名前だけ残して R/W 側の別名にしてある。
- **SEEK / RECALIBRATE の前に SIS で排水する** (`fdc_drain_interrupts`)。
  上限は `FDC_SIS_DRAIN_MAX` = 4 (µPD765A が溜められるドライブ数)。
  停止条件は ST0 = 80h で、無限ループにならない。
- **シーク完了待ちを `fdc_wait_seek_end()` に統一した**。タイムアウトしても
  SIS を出し、SE が立っていれば「エッジを取りこぼしただけ」として完了扱いに
  する。80h なら未完了として区別する。別ドライブの遅れた完了通知は
  読み捨てて次を見る (上限 4 回)。
- **リトライの間に `fdc_recover()`** = リセット → Specify → 排水 →
  recalibrate。R/W の 3 回リトライで、失敗した試行の後・次の試行の前に呼ぶ
  (最後の失敗の後には呼ばない)。
- **RECALIBRATE の EC は 2 回まで出す** (`FDC_RECAL_ATTEMPTS`)。
  1 回で 77 ステップ踏めるので、80 シリンダ媒体のいちばん奥からでも 2 回で
  トラック 0 に届く。
- **DMA バッファを 1024B 境界に揃えた** ([HW2])。1024B 境界の 1024B は
  64KB 境界をまたぎようがない。`fdc_init()` の先頭で実際に確かめ、破れて
  いたら 1 行出す。番地は 0x1555e0 → 0x156000 に動いた (どちらも無事だが、
  以前は **リンク順のたまたま**に依存していた)。
- **最終失敗のときだけ 1 行出す** ([V4])。
  `[fdc] read fail drv=0 chs=0/0/1 phase=seek st0=28 st1=00 st2=00 (NR)` /
  `[fdc] recalibrate drv=0 rc=-2 st0=80`。リトライごとには出さない。
  リザルトフェーズまで届いていないときは `results[]` が空なので、
  **最後の SEEK / RECALIBRATE の ST0** を出す。
- **NR は即座に最終失敗にする**。リトライも `fdc_recover` もしない。
- **転送の後始末 `fdc_abort_transfer()`** = DMA ch2 マスク → FDC リセット →
  Specify → SIS 排水 (IRQ 待ちなし)。DMA を開いたまま (`dma_armed`) 抜ける
  経路すべてで呼ぶ。`fdc_recover()` の先頭でも ch2 をマスクする。
- **I/O 0439h bit2 (1MB 超への DMA 禁止) を落とす** — `fdc_init()` で 1 度、
  bit7 を壊さないよう RMW。**読みが FFh でも書く**: 実機は bit7=1 (内蔵
  プリンタ)・bit2=1 (起動時設定)・「未使用(?)」ビットが 1 で読めれば正当に
  FFh を返し得るので、そこを避けると直したい root panic が直らない。
  RMW なら FFh → FBh で bit7 は保たれる。
- **`fdc_read_results()` の空回りに上限**を掛けた (`FDC_MSR_SETTLE_LOOP`)。

## 6. この試験が見ていないこと

- **実機でもエミュレータでも 1 度も動かしていない。** タイムアウトの値が
  実際に足りるか、排水で INT 線が下りるか、`root panic` が消えるかは
  すべて未検証 — ホストで確かめられるのは ST0 の読み方までが限界。
- モーター制御・Specify の値 (名前を付けただけ)・DMA の設定手順・
  `fdc_set_media*` の経路は触っていない。
- **I/O 0439h bit2 はホストでは試験できない** (I/O だけなので判定が無い)。
  NP21/W は `necio_bind` が 0439h に out ハンドラしか繋がず、`iocore` の
  `definp8` が FFh を返すため、**このビットを模擬していない**。つまり
  「効いたかどうか」はエミュレータでは一切分からない。
- **期限までループする `fdc_wait_seek_end`** と **`fdc_abort_transfer`** も
  純粋でないので試験していない。判定関数が 3 つの結果を別々の値で返すこと
  (`other_drive`) までがホストで確かめられる範囲。
