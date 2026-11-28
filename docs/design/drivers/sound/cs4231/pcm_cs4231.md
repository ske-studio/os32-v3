# sys/dev/sound/cs4231/pcm_cs4231.c — CS4231 (MATE-X PCM) の PCM 再生ドライバ

> 状態: **現行 (2026-11-18)** — 出所: TASK_PCM_CS4231 (要約して移した)
ソース: `sys/dev/sound/cs4231/pcm_cs4231.c` (I/O と状態機械)・`sys/dev/sound/cs4231/pcm_cs4231_math.c` (純粋部)・`sys/dev/sound/cs4231/pcm_cs4231.h` (数と列の型)・`sys/dev/sound/cs4231/pcm_refill.h` (tick の救済の閾値)・`sys/include/pcm_contract.h` (状態・エラーの写し・KAPI の実体の宣言)

16 ビット・ステレオ・44.1kHz / 22.05kHz の**再生だけ**を、ドライバ所有の DMA リングと KAPI v61 の 5 本で提供する。単位は
frame (左右 1 組 = 4 バイト)。録音・PIO・8 ビット・モノラル・ミキサの入力側・86 型 PCM の FIFO の模倣・`0C2Bh/0C2Dh` の再配置・
DMA #0/#3・XTAL1 系のレート (48k など) は持たない。本体は部品 `pcm.mod` (module id 7、`build/pcm-module.mk`) にあり、
常駐側の façade は [pcm_module](pcm_module.md)。数は `pcm_cs4231.h` に 1 か所だけ置く ([C4])。

## S3 のつなぎと S4 の受入（2026-11-27）

Ra266 の BIOS 初期値は 0x20。診断像 9a8486cb3 で 0F40h に **0x22（INT5 / IRQ12、DMA #1）** を書くと 30 秒の再生中に IRQ12 が 5,138 回届き、到着時に R2.INT と I24.PI が共に立った。0x1A の対照窓では PCM の IRQ10 到着は 0。初段では常駐側の起動時プローブが 0188h・0288h の双方で ABSENT を確認し、BIOS が INT5 を選んだ構成を IRQ12 に結ぶ。OPNA・26K 相当・別ベースの音源・UNKNOWN（既登録で検査を省いた場合を含む）での 0x1A（INT41 / IRQ10・DMA #1）は、実 FM の線と IRQ10 の分離が確認でき、S3 が資源を取得できる場合に限る。衝突・線不明では資源設定前に open を拒否する。起動ログに IRQ と FM 判定による選択理由を記録する。

機種側で WSS の実ベース（既定 0F40h）と装置の存在を確認する。起動中、最初の経路書込みより前に base+0 を一度採取し、実ベース・読出し成否・元のバイトを常駐側で保持する。再 open と部品の再 attach もこの記録を使う。読出し不能、0xff、予約 DMA 符号、両ベース ABSENT で INT5 以外、または DMA #1 を使用できない機種（PC-9821Np を含む）は経路を変えず、起動ログに理由を一行残す。資料位置は PC-98 の io_sound.md:116–143,682–715。

| WSS bit5–3 | PC-98 割込み名 |
|---|---|
| 001 / 010 / 011 / 100 | INT0 / INT2 / INT41 / INT5（順に対応） |

| PC-98 割込み名 | PIC IRQ | 初段 |
|---|---|---|
| INT0 / INT2 | 3 / 6 | 将来対応。LGY-98 や USB と配線を共有できるか確認してから選択・解除・診断を一貫させる |
| INT41 | 10 | FM 搭載・UNKNOWN のつなぎ |
| INT5 | 12 | 共有登録。LGY-98 は従来どおり INT5 を拒否 |
| INT1 | 5 | CanBe 別レジスタの選択肢。WSS の符号化には使わない |

PIC 対応の参照位置は iopcmcia.md:153–159、lgy98.h:30–35。通常起動ではマザーボードコンフィギュレータと CanBe リモコン制御にアクセスしない。BIOS 値と実測線が食い違う場合だけ、対象機種を照合し、他利用者と直列化して元 index を安全に保存・復元できるときに診断する。0411h の index 69h と CanBe の index 01h は選択・復元の両方で書込みを伴い、割込みを挟むと他利用者の data アクセス先を変える。選択中は data（前者は 0413h）の読取りだけを行う。資料位置は io_mbcfg.md:9–27,204–213、io_canbe.md:38–55。

8237 の常駐側（dma8237.c/.h）がチャネル別に空き・所有者・隔離を保持する。取得は空きだけに成功し、返却・隔離は取得時の所有者だけが行える。検査と更新を同じ割込み禁止区間で行う。PCM 部品は常駐 façade を通す。FDC の転送・reset、kdebug の 8237 自己試験を含め、同一チャネルの setup・mask・unmask・TC 消費は取得済み所有者に限る。ハードウェアのマスク状態や DMA バッファの確保を所有の証拠に使わない。

open はバッファ確保後、選択した IRQ の共有登録 → DMA #1 の取得 → WSS 経路の書込み・読戻し → codec 初期化の順に進む。登録・取得に失敗した時点では経路・codec（index、PEN/IEN、PI を含む）・所有していない DMA への書込みはゼロ。成功済みの登録とメモリだけを回収する。所有した #1 をマスクしてからIRQ10 なら 0x1A、IRQ12 なら設定値 `(bios_route & 0xf8) | 0x02` を書き、IRQ12 では bit6 を含む上位ビットを保つ。書込みを試みた時点を記録する。読戻し一致まで codec の index/data へ書かず、IRQ/tick も OPENING 中は装置に触れない。起動時の存在確認で MODE2 を書く現行検出列も分割し、書込みを伴う codec 確認はこの着手点以降に置く。ログでは WSS 符号・PIC IRQ・V86 ベクタを区別する。

close・失敗・reclaim は経路書込みの試行と codec 着手を別々に追跡する。codec 着手後は補充を止め、IEN/PEN を落とし、DRS の停止を待ち、所有する #1 をマスクして残留 PI を解除する。reclaim は IF=0 で引き取り、再開・補充を禁止して IEN/PEN を落とす。所有権を保持したまま IF=1 の前景で、停止要求時に一度だけ定めた tick 期限まで DRS を確認する。期限までに DRS が落ちなければ元経路を書き戻さず、下記の隔離へ進む。元経路の DMA が #0/#3 でもそのチャネルをマスクしない。codec が停止している証拠（有効な読出しで PEN・IEN・DRS が 0）を確かめてから元の経路を復元する。経路読戻し不一致で codec 未着手なら初期化・PI 解除を飛ばし、停止を安全に確認できない場合は復元を試みず隔離する。元バイトの復元と読戻し一致後に #1、IRQ 登録、バッファを回収する。停止または復元が確認できなければ常駐側に故障を記録し、#1 をマスクして隔離し、DMA が参照し得るリングを保持する。以後の PCM 再 open（部品の再 attach を含む）と別所有者の #1 取得は再起動まで拒否する。

S4 は Ra266 の IRQ12 再生と、118・86+WSS の IRQ10 再生、BIOS 値の復元、ゲスト FM の IRQ12 配送を確認する。次段では FM タイマと FIFO の実配線をそれぞれ照合した上で、118 の PCM も BIOS の INT に揃えて IRQ12 を共有する。入口条件と受入窓は [IRQ 設計](../../../kernel/irq/irq.md) に従う。

PI の意味とクリアは Crystal CS4231A DS139PP2 の R2・I24 に従う。V86 との共有は OS32 の設計である。[FreeBSD mss.c](https://github.com/freebsd/freebsd-src/blob/stable/11/sys/dev/sound/isa/mss.c)・[PC98 GENERIC.hints](https://github.com/freebsd/freebsd-src/blob/stable/11/sys/pc98/conf/GENERIC.hints)

IRQ 側は所有権・MODE2・状態を確認し、R2.INT があるときだけ I24 を読む。PI があれば既存の再生専用 ack を済ませてから DMA 位置で advance する。PI がなければ装置に書かず IRQ_NONE。RUNNING 中の tick の PI 確認と ack、停止・drain の期限、欠測時の位置観測を残す。IRQ と tick は IF=0 で直列化し、同じ PI を二度計上せず、tick の advance は位置と時刻で判定する。通常補充が IRQ12 に移ると IRQ0 の補充回数は減り得るが、tick の観測と再同期は残る。通常 wave と強制 epoch の窓を分けて実測する。

SNDboard は NP21/W が載せる音源構成を選ぶ。IRQ の配送は OS が書いた経路を観測して判定する。移行の段と両機種の受入は [IRQ12 共有の設計](../../../kernel/irq/irq.md#pc-98-irq12-共有の移行案) に置く。以下の IRQ10・0x1A と受入記録は移行前の実装を示す。

## ハードウェアの手順と根拠

典拠: Crystal CS4231A データシート DS139PP2 (`docs/hw/crystal/cs4231a.pdf`、gitignore のミラー)、`docs/hw/undocumented/io_sound.md`
「PC-9821X･N内蔵型」、NP21/W `src/sound/cs4231c.c` / `src/cbus/cs4231io.c`。

| 項目 | 事実と使い方 |
|---|---|
| ポート | `0F40h` 経路 (R/W)、`0F43h` WSS ID、`0F44h` R0 Index (INIT / MCE / TRD / IA4-0)、`0F45h` R1 Data、`0F46h` R2 Status (何を書いても INT が消える)、`0F47h` PIO (使わない) |
| 移行前の経路 `0F40h` | bit5-3 = INT (011 = INT41 = IRQ10)、bit2-0 = DMA (010 = #1)。**`0x1A` = IRQ10 + DMA #1** (`PCM_ROUTE_INT41_DMA1`)。IRQ10 は動的な線で LGY-98 の既定 (3/5) と FDC (#2) を避ける。書くとその場で DMA の attach が変わる |
| 検出 (`pcm_detect`) | 一続きで: `0F43h` の下位 6 ビット = 000100b → R0 が 0x80 でなくなるまで待つ → I12 の ID3-0 = 1010b → **MODE2 を書いて読み戻す** → I25 の V2-0 = 100 (CS4231) / 101 (CS4231A) (p.36/38)。`A460h` のサウンド ID は見ない (NP21/W の `SNDboard=0x64` では B460h に移る) |
| I8 | 16 ビット LE ステレオ、XTAL2 (16.9344MHz)。44.1k = ÷384 → `0x5B`、22.05k = ÷768 → `0x57` (p.33)。XTAL2 の無い機械では出ない |
| I9 | D4:D3 CAL、D0 PEN。PEN 以外は MCE 中しか書けない (p.34)。MCE 中に `0x08` (CAL 1 = 変換器の校正、136 サンプル周期 ≒ 3.1ms) — CS4231 では DAC だけの `0x10` が予約なので両版に共通な値。PEN は MCE 無しで書く |
| I10 | D1 IEN。I11 (R/O): D5 ACI (校正中)、D4 DRS (DRQ 動作中) |
| I12 | D6 MODE2。I16〜I31 は MODE2 でだけ見える。MODE1 のまま Index 24/25 を選ぶと IA4 が無視されて I8/I9 に化ける |
| I14 / I15 | Playback Base = NS − 1 (NS = 割り込み間の frame 数)。半分 2048 frame なので `0x07FF`。**I15 → I14 の順** (上位の書きが Current Count をロードする) |
| I16 | D0 DACZ = 1 (アンダーランで 0 を出す)、D5 SPE = 0 (MCE 中に書く。BIOS が 1 にしていると DAC がシリアル入力を使い DMA の音が出ない) |
| I24 | D4 PI (再生 DMA カウントの割り込み)。I24 に 0 を書くか R2 に書けば消える (p.39) |
| INIT / MCE | INIT 中と MCE を落とした後の再同期中は全読みが 0x80・書きは無視 (p.19-21)。**0x80 を「ACI = 0」と読まない** |
| 8237 | `dma_chan_setup(1, ring, 16384, DMA_DIR_FROM_MEM, DMA_MODE_CYCLIC)` (auto-init)。8237 のバンクは 8 ビットなので上限 16MB (`DMA_PHYS_LIMIT`) |

## データの形と不変条件

- **リング** 16KB (4096 frame = 半分 2048 × 2) は DMA プールから (`dma_alloc(16384, 4096, DMA_PHYS_LIMIT)`、64KB 跨ぎ無し)。
  **ステージング** 16KB (4096 frame) は DMA しないので `kmalloc` (KHEAP)。プール 64KB は 82557 と分け合う。open で両方を 0 で埋める。
- **リングを書くのは `pcm_advance` (IRQ / tick、IF=0) と、停止中の `pcm_start` / RS_RESTART だけ**。foreground (`pcm_write`) が書くのは
  ステージングだけで、**アプリのバッファを IRQ から読むことはしない**。
- ステージングは `struct pcm_stg` (w / r / staged、frame 単位)。空きは `staged` から出す (w == r を空と満杯の両方に使わない)。
  物理末尾を跨ぐ写しは `pcm_split` で 2 回に分ける (書き側も読み側も)。
- 観測の状態は `struct pcm_core g_pcm` (kernel.map から読める): 前回の半分 `half` と frame 番号 `pos` (リング全体 0..4095)、
  `filled[2]` (各半分の実データの frame 数)、`last_data_half`、drain の段階、`last_progress` / `now` (µs)、カウンタ 3 本、`finalized`。
- レジスタは `cs_read` / `cs_write` で **Index と Data の組を 1 つの `irq_save` の中**で触る。MCE の列の途中 (`s_mce_busy`) は
  IRQ / tick が装置に触らない。
- 装置への書きの列は `struct pcm_op` の表 (`pcm_seq_prologue` / `_init` / `_start` / `_stop_entry` / `_stop_tail` / `_restart` / `_abort`) で持ち、
  `pcm_run` が実行する。順序そのものが仕様なので表をホスト試験が配列として照合する。

## 状態機械 (`PCM_ST_*`)

装置を触る遷移は tick 駆動の分割状態で、IRQ / tick の中で待たない。foreground の待ちは IF=1・tick の期限つき。

| 状態 | advance | write | 遷移 |
|---|---|---|---|
| CLOSED / OPENING / FAULTED | 装置に触らない (Index 書きも 0 回) | 拒否 | open → OPENING → OPEN / 失敗は巻き戻して CLOSED |
| OPEN | PI の ack だけ | ステージングへ | staged が 4096 (両半分) に達した write、または未転送ありの close → `pcm_start` → RUNNING (close なら DRAINING)。未転送なしの close → 再生せず解放 |
| RUNNING | `pcm_advance_run` | ステージングへ | close → DRAINING / 番犬・連続性喪失 → RS_STOP |
| DRAINING | 同上 | 拒否 | drain 完了 → STOP_REQ / 番犬・喪失 → drain 失敗を記録して STOP_REQ |
| RS_STOP | 入口 1 回 (IEN=0 → PEN=0 → R2 → I24=0、期限 = 今 + 3 tick) の後、DRS = 0 を待って mask | ステージングへ | → RS_RESTART / 期限切れ → drain 失敗を記録して STOP_REQ |
| RS_RESTART | mask → リング 0 → setup → I15/I14 → ステージングから配る → 基準を今に → unmask → PEN=1 → IEN=1、`resyncs` +1 | ステージングへ | → RUNNING (`close_pending` なら DRAINING) / 失敗 → STOP_REQ |
| STOP_REQ | 入口は RS_STOP と同じ。DRS = 0 → mask → **証拠** (R0 != 0x80、I9 の PEN = 0、I24 の PI = 0) | 拒否 | → STOP_DONE / 期限内に証拠が無い → FAULTED |
| STOP_DONE | PI の ack だけ | 拒否 | foreground が解放 → CLOSED |

- 停止の期限は**入口でだけ**入れ、後続の tick で入れ直さない。PEN=0 は発行済みの DMA 要求の最後のサンプル転送の後に効く
  (p.16/19) ので、DRS が落ちてから `dma_chan_mask` する。
- FAULTED を一度踏むと再起動まで `pcm_open` は `OS32_ERR_IO` (`s_fault_sticky`)。

## 口の契約 (KAPI v61、実体は `sys/include/pcm_contract.h`)

| 関数 | 契約 |
|---|---|
| `pcm_open(rate)` | 44100 / 22050 だけ。0 / `OS32_ERR_NOSYS` (検出なし、部品なし) / `OS32_ERR_FULL` (= `PCM_ERR_BUSY`、他の owner が open 中・IRQ10 に結べない) / `OS32_ERR_INVAL` (rate) / `OS32_ERR_NOSPC` (= `PCM_ERR_NOMEM`) / `OS32_ERR_IO` (初期化の期限、FAULTED の後) |
| `pcm_write(buf, bytes)` | 受け取ったバイト数 (frame の倍数、端数は切り捨て、0 = 満杯 → アプリが `sys_yield` して再試行)。wrapper は `pcm_write_check` (owner と状態) を**入力の検査より先に**呼ぶので、未 open・非 owner は bytes = 0 でも負。次に NULL・加算あふれ・`ring3_user_range_ok` |
| `pcm_status(free, counters)` | free = ステージングの空き (バイト)。counters = `(underruns<<24) \| (repeats<<16) \| resyncs` (8/8/16 ビットで飽和)。出力 2 本は出力保護つき |
| `pcm_close()` | drain して止まるまで IF=1 で待つ。0 / `OS32_ERR_IO` (drain 失敗・FAULTED) |
| `pcm_set_volume(percent)` | 1〜100 を I6/I7 の 6 ビット減衰へ線形 (100 = 0dB)、0 = D7 のミュート、101 以上は `OS32_ERR_INVAL`。回収中・MCE 中・FAULTED は `OS32_ERR_AGAIN` (判定と 2 つの書きは 1 つの禁止区間) |

owner は既存の資源 owner と同じアプリ ID (`res_owner_get()`、CPL=0 のシェルは 1)。write / status / close / set_volume は owner 一致を要求する。
覗き口 `pcm_state` / `pcm_present` は KAPI ではない (`ver` の表示などが使う)。

### open の列と巻き戻し

`pcm_seq_prologue` (`dma_chan_mask(1)` → **`0F40h = 0x1A` を先に** → R2 → I9 の PEN=0 → I10 の IEN=0) → 検出 → リングとステージングの確保 →
`irq_register(10, pcm_irq, 0, IRQ_F_SHARED)` → `pcm_seq_init` (INIT 待ち → MODE2 を書いて読み戻す → MCE 付きで I8 → INIT 待ち →
MCE 付きで I9 = CAL 1 → MCE 付きで I16 = DACZ → R0 = 9 で MCE を落とす → INIT 待ち → ACI 待ち → I6/I7 = 0 → I24 = 0 → IEN=1 →
`dma_chan_setup` (マスクのまま) → I15 → I14) → 状態 OPEN、owner を公開。待ちは全部 IF=1・5 tick。
失敗は `pcm_release(0)` (unregister → ステージングを kfree → リングを dma_free) で、`0F40h` は 0x1A のまま残す (detach しない)。

### advance (IRQ10 と tick が呼ぶ唯一の入口)

0. 回収済み・MCE 中・CLOSED / OPENING / FAULTED なら装置に触らず `IRQ_NONE`。それ以外は I24 を読み、PI があれば 0 を書いて handled。
   **PI は割り込みの受理にだけ使い、連続性には使わない** (回数を持たないレベルのフラグで、古い PI と次の境界が合流する)。
1. `dma_chan_remaining(1, &left, &tc)` が成功なら `p1 = pcm_pos_frames(left)`、`h1 = p1 / 2048`。`tc` は判定に使わない (立っていれば診断のため ack だけ)。
   `-EAGAIN` なら位置を更新せず時刻だけ取る。時刻は `sys_time_now` (取れなければ `tick_clock_jiffy64` × 10000µs) を 64 ビットで前回値と max。
2. 連続性 (`pcm_cont`、位置だけ): 半分が変わった → 切り替え 1 回、同じ半分で `p1 >= p0` → 0 回、同じ半分で `p1 < p0` → 1 周回った = **喪失**
   (`repeats` +1 → RUNNING は RS_STOP、DRAINING は drain 失敗で STOP_REQ)。`changed` / `old` / `progressed` は更新の**前に**確定する。
3. 切り替えなら `gen` +1。RUNNING で `filled[h1] < 2048` なら `underruns` +1 (**消す前に**数える。DRAINING の末尾の 0 埋めは数えない)。
4. 補充: 現在位置から `h1` の末尾まで `PCM_REFILL_MARGIN` (512 frame) 以上あるときだけ、消費し終えた `old` へステージングから
   min(2048, staged) を写し残りを 0 で埋める。足りなければ写さずに喪失と同じ扱い (書いている間に装置が追い越すと古い音が鳴る)。
   正の frame を写したときだけ `last_data_half = old` とし drain を「入るのを待つ」に戻す。
5. drain の段階 (1 回の観測で 1 段): 待つ → (`h1 == last_data_half`) 読んでいる → (離れた) 無音の半分 → (さらに切り替え) 出た。
   **DRAINING で「出た」かつ staged = 0** で完了 → STOP_REQ (DMA の完了と DAC の出力は別なので無音の半分を丸ごと通す)。
6. 番犬: `now − last_progress` (位置が進んだ最後の時刻) が 2 半周期 (44.1k で 92879µs) を超えたら RS_STOP / drain 失敗で STOP_REQ。
   時計は長い IF=0 で過小評価する側なので、遅れて発火しても早くは発火しない。

**tick の救済** (`pcm_refill.h`): tick から来た観測で半分の切り替えが見えても、もう 1 度位置を読み直し、1 半周期未満の間隔・喪失でない・
次の境界までの距離が `pcm_refill_threshold(rate)` (= 512 + 10ms の tick 周期・揺れ・コピー・誤差の余裕を frame にした値) より遠い、
のすべてなら何も公開せずに戻り、補充は IRQ10 に任せる。近ければ tick が補充する。閾値が半分以上になる rate は open で `OS32_ERR_INVAL`。
この閾値の設計は `docs/decisions/time/TICK_T123_DESIGN.md` が持つ。

**保証の範囲**: 連続性の判定が正しいのは、成功した位置取得どうしの間隔が 1 半周期 (44.1k で 46.4ms、22.05k で 92.9ms) 未満のとき
(連続した `-EAGAIN` の空白も含む)。tick がそれを与えるのは IF=0 の区間 < 10ms という HAL の契約の中だけ。2 半周期以上の空白では
0 回と 2 回、1 回と 3 回を区別できない (保証外。救済は番犬と `p1 < p0` の場合だけ)。

### start / close / reclaim

- `pcm_start` (IF=0 で `pcm_seq_start` を流す): 半分 0 へ min(2048, staged)、残りを半分 1 へ。`last_data_half` は最後に正の frame を置いた半分、
  drain の初期段階は半分 0 だけなら「読んでいる」、半分 1 にもあれば「待つ」。TC と PI の証拠を消し、half = pos = 0・基準時刻 = 今で RUNNING を公開 →
  unmask → PEN=1。両半分がそろうまで始めない (開始遅延 93ms @44.1k) のは、半分 1 つで始めると最初の切り替えで 2048 frame の無音が入るため。
- `pcm_close`: 状態の判定と DRAINING / `close_pending` の公開を 1 つの禁止区間で。待ちの期限は半分単位
  `pcm_close_ticks` = ceil((ceil(staged/2048) + 3) × H / 10ms) + 3 tick (staged = 0 で 44.1k は 17、22.05k は 31)。期限切れは
  drain 失敗 → STOP_REQ と停止の入口を 1 つの禁止区間で公開し、さらに 3 + 2 tick 待つ。それでも STOP_DONE でなければ FAULTED。
  **「PI が来ない」は止まった証拠にしない**。STOP_DONE ならリングを返す、FAULTED ならステージングだけ返してリングは `dma_mark_leaked`。
- `pcm_reclaim(owner)` (`exec_reclaim_owned` から。保存した owner と引数だけを照合): **待たない abort**。IF=0 の中で `s_claimed` を立て、
  RUNNING / DRAINING / RS_* / STOP_REQ なら `pcm_seq_abort` (DRS を待たずに mask)、証拠 (R0 != 0x80 かつ PEN = 0) を 1 回読む。
  証拠があれば STOP_DONE として解放、無ければ FAULTED でリングだけ leaked。OPEN と STOP_DONE は止めずに解放、CLOSED / OPENING は何もしない。
  正常終了・fault・CTRL+STOP で同じ経路。部品の巻き戻し (`pcm_module_quiet`) も同じ abort を使う。
- 解放 (`pcm_release`) は `finalized` で**1 度だけ**。unregister → ステージングは**常に** kfree (装置が触らないので) → 証拠があればリングを
  dma_free、無ければ leaked。`irq_unregister` と kfree / dma_free は foreground だけ (ISR からの解除は `IRQ_ERR_CTX`)。

## 落とし穴

- **ISR と foreground の状態観測**: 停止待ちで通常の読みを使うと、最適化で読みがループ外へ移り、ISR が完了しても期限切れになる。
  `pcm_wait_stopped` は ISR が更新する状態を volatile 経由で繰り返し読む。別の I/O 命令による偶然の再読込みに頼らず、この観測契約を保つ。
  `pcm_cs4231_host.c (旧版 8d6596ae4052)` の `case_close()` は停止完了の状態遷移を検査する。
- **foreground の判定と公開は 1 つの禁止区間**: close の入口、close の期限切れ (STOP_REQ の公開と停止の入口)、reclaim の引き取り、
  set_volume の判定と書き。分けると tick が間に入り、止まった装置を DRAINING で上書きする・古い期限で FAULTED にする・
  RS_RESTART が unmask してから foreground がリングを free する・FAULTED の後に装置へ書く、が起きた。
- MODE1 のまま I24/I25 を選ばない (I8/I9 に化けて正常な装置を NOSYS にする)。MCE を保つ書きは R0 に `0x40 | idx` (素の idx は MCE が落ちる)。
- NP21/W: `0F40h = 0` で detach すると `dmach = 0xff` になり、その状態で I8 を書くと `cs4231_control` が配列外を触る — **detach 状態で装置レジスタを書かない**
  (経路を先に結ぶ、停止確認後に BIOS の元値へ戻す)。NP21/W は IEN=0 のあいだ PI を立てない、CS4231 専用の DMA 経路は 8237 の TC を立てない、
  折り返しで無条件にアドレスを戻す (auto-init の証明にならない)、再生が実時間より速い。
- 時計の巻き戻りの判定は 64 ビットで (下位 32 ビットの差で推すと起動 35 分 48 秒後からの初回再生で番犬が死んでいた)。`pcm_obs` は u32 の差なので 71 分まで正しい。
- `pcm_test` の右チャネルは frame 番号の下位 16 ビットなので、65536/44100 ≈ 1.49 秒周期でブツッと鳴る (ドライバの不具合ではない)。

## 他の部品との境目

- 土台: 割り込みの登録 (`kernel/irq`、`IRQ_F_SHARED`)、8237 (`sys/dev/dma/dma8237.h` の `dma_chan_setup` / `_remaining` / `_mask` / `_unmask` / `_ack_tc`)、
  DMA プール (`sys/kern/mm/dma_pool.h`)、µs 時計 (`sys_time_now`)。部品の中では `irq_register` / `dma_alloc` などは façade の `pcm_module_*` に置き換わる ([pcm_module](pcm_module.md))。
- `PCM_IRQ` の値を知るのは pcm_cs4231 側だけ。既存 imports の `pcm_module_irq_register(irq,fn,arg,flags)` を使い、表や export スロットは追加しない。
- 回収は `sys/kern/exec/exec_reclaim.h` の `exec_pcm_reclaim` → `pcm_reclaim`。起動時の検出は `pcm_init` (検出だけ、装置が無ければ `[pcm] none`)。
- 診断: `pcm_diag_fault_site` / `pcm_diag_evidence` / `pcm_diag_df_site` / `pcm_diag_stop_calls` などを kernel.map から読む (値の意味は `PCM_DIAG_*`)。
  `-DPCM_REFILL_DIAG` で観測と補充の時間 (`pcm_diag_observe` / `pcm_diag_refill`) を IRQ10 / tick 別に採れる。

## 確かめ

| 検査 | 見ているもの |
|---|---|
| `make check-pcm-cs4231-host` (`test_pcm_cs4231.py (旧版 8806077bde45)`、記録 `pcm_cs4231_tdd.md (旧版 b69f05d9bd5d)`) | 実物を `#include` し `io.h` だけ模型に差し替え: 連続性の全組・保証内の正常列と喪失列・補充の余裕・消す前の underrun・drain の段階・番犬・満杯からの自動開始・ステージングの 2 分割・close の期限式・音量・列の順序 (I14→I15、MCE 無しの I9、MODE2 無しの I24、DRS を待たない mask、期限の毎 tick 初期化を変異で)・状態の遷移と 1 度だけの解放・foreground と tick の交互試験 (`PCM_PREEMPT`) |
| `pcm-refill-host` (`test_pcm_refill.py (旧版 aadecf802127)`) | tick の救済の閾値・補充の起点・DMA の期限 |
| `pcm-e4-host` (`test_pcm_e4.py (旧版 36a81d2e03fb)`) | 共有 IRQ10 (実 I24 の handler と偽装置)。ゲストは `make kernel-pcm-e4-diag` の像で `pcm_test e4` |
| `pcm-module-host`・`pcm-contract-host` | 部品の読み込み・巻き戻しと再生、KAPI / façade / 本体を通した契約 |

NP21/W はフォークの `/api/sound` (CS4231 の PEN / IEN / MODE2 / I8 / Base / PI の累積 / 0F40h、`?pcm=1` で直近 1 秒の生 frame) を
`SNDboard=0x64` の trial ini で使う。起動行は `[pcm] CS4231 v=100 irq 10 dma 1 fmt 0x5B` (NP21/W は I25 = 0x80)。

## 実機で確かめた事 (Ra266)

検出と初期化が通り、`pcm_test` 3 回と `pcm_play` (44.1kHz・16bit・ステレオの WAV) 3 回で under / rep / resync 0・close 0・leftover 0。
ユーザーが聴いて合格。再生中の CTRL+STOP で止まり、次の open が通る。

## 未確認

- NP21/W で未実施: 書き込みを 200ms 止めた無音と再開後の連続、開始直後と close 直前の区間の採取、ミュートの出力での確認、
  8KB の写しと restart の 16KB クリア + 写しの IF=0 の実時間。CTRL+STOP の注入 (再生中に `/api/cmd` が HTTP を塞ぐ)。
- 実機で個別に見ていない: MODE1 の初期状態、INIT 中の書き無視、MCE / 校正の待ち、Base 上位書きのロード、DAC の初期ミュート、
  XTAL2 の有無 (I17 の XTALE)、BIOS の `0F40h` の旧値、1MB 超の DMA、DRQ / FIFO の停止の時機、auto-init ビット。
- 共有 IRQ の電気的な共有 (82557 と同じ線) は未実証 (E4 はソフトウェアの共有だけ)。GUI の park 中の kill と CPL=0 の回収は対象外。
- 64MB 構成の持越し (DEFERRED_TESTS の H-4): (b) GUI で鳴らしている間の park / resume (GUI で鳴らす fixture が無い)、
  (c) 停止後の I9 の PEN と 8237 の DMA マスクのレジスタ読み (読む API が無い。IMR の変化と STOP 後の解除は確認済み)。
- CS4231 (V=100) と CS4231A で I11 の並びが同じこと (Appendix A に差分無しと読んだが再確認していない)。
- ホスト試験に無いもの: 生成 wrapper を通した `pcm_write` の契約 (範囲・あふれ・端数の順序)、交互試験の補強 (2 段目の期限境界での STOP_DONE、
  解放 → 再 open の claim の引き継ぎ、親 owner に戻った状態の reclaim)。
- `PCM_REFILL_COPY_US` は仮の値で実機の受入が要る (`pcm_refill.h` の注記)。

## 次段で BIOS INT5 を保つ open（設計案）

常駐側の配線判定で IRQ12 の共有を許可した 118 と 86+WSS は、FM 搭載でも PCM を IRQ12 に登録する。118 の許可には型番・WSS 実ポート・BIOS 設定に一致する WSS 単独の PI／物理 IRQ12 到着／解除、FM 単独到着、両出力の同線駆動の確認記録を要する。初回確認は diag_tick_s1 の排他診断像で行い、製品では確認済み表と 0F40h・A460h・0Eh が一致するときだけ共有する。IRQ12 共有表に一致しない場合、実音源と IRQ10 の分離を確認できれば IRQ10・0x1A のつなぎと旧 IRQ12/V86 を保つ。FM が IRQ10 または線不明なら資源設定前に open を拒否する。診断像の一時配送でも上記 S3 の資源取得・読戻し・停止・隔離の順序を使い、0F40h の DMA 欄だけを #1 にする。同線を確認した場合の req83 の観測から導く open 中／close 後の期待値は、118 が 22h／23h、86+WSS が 22h／22h、Ra266 が 22h／20h。元値と設定値が等しい場合も IRQ と DMA の所有を取得して読戻しを確認する。118 の元 DMA #3 の mask を操作しない。PCM 側は R2.INT と I24.PI を満たす再生要求だけを処理し、FM／FIFO の ack とゲスト反射は音源所有側が担当する。PCM だけの機械で将来提供するソフト音源は、仮想タイマ／FIFO の周期で仮想 IRQ12 を要求できる。PCM の物理 period とゲストの演奏周期を独立に保つ。[配線確定・先行検査・受入](../../../kernel/irq/irq.md#次段の配線確定と有効化設計案基点-c34ee44f1)

codec の間接 index/data アクセスと MODE2 の登録配置は Crystal DS139PP2（R2、I24）および BSD 実装の ad_read、ad_write と CS4231 初期化表を照合した。PC-98 の線選択には上記の機種資料を用いる。[NetBSD ad1848.c](https://raw.githubusercontent.com/NetBSD/src/trunk/sys/dev/ic/ad1848.c)
