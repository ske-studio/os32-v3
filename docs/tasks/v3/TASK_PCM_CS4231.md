# TASK_PCM_CS4231 — CS4231 (MATE-X PCM) の PCM 再生ドライバ (§5-5 の P1)

> 状態: **受入待ち** — 着地済み・v2.1 に同梱。NP21/W で E0〜E3 合格、実装レビュー往復 2 で Approve。残: E4・E5 (NP21/W) と E6 (実機)。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-fable-5-1`、2026-09-23) / それまでの状態: **実装済み・NP21/W で E0〜E3 合格 (2026-09-23、§3-1)。実装レビュー (Codex) 往復 1 = Request changes 5 件 (reclaim と restart の競合、close 入口の非原子性、close 期限の公開順、時計の巻き戻り判定、FAULTED 後の音量書き) → 修正済み (e301a61: 交互試験 7 ケース RED→GREEN、変異 30 本)。合流後に NP21/W で E3 を再確認 (5 秒 × 2 + 短いストリーム、close 0、fault site 0、kselftest 197/197)。**実装レビュー往復 2 = Approve** (P1〜P5 閉、新規 blocker 無し。非 blocker: claim のコメント、wrapper 経由の write 契約試験、交互試験の補強 3 件 — §3-1 の残件に)。E4/E5 は残件、E6 は実機**。
> 正典の関係: [`PLAN.md`](../../archive/v3/PLAN.md) §5-5、土台は [`TASK_HAL_WIRING.md`](TASK_HAL_WIRING.md) (1-1 割り込み、1-2 8237、1-3 プール、1-5 時計)、
> 出力保護は [`../../archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md`](../../archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md)。
> 典拠: Crystal **CS4231A データシート DS139PP2** (`docs/hw/crystal/cs4231a.pdf`、gitignore のミラー、`pdftotext` 済み)、
> `docs/hw/undocumented/io_sound.md`「PC-9821X･N内蔵型」、NP21/W `src/sound/cs4231c.c` / `src/cbus/cs4231io.c`。
> **NP21/W 上の実装と一次受入 (E0〜E5) は実機不要**。実機でしか検証できない項目は E6 に残る。

## 0. 範囲

**P1 だけ**: 16 ビット・ステレオ・44.1kHz (と 22.05kHz) の**再生**を、カーネル所有の DMA リングと KAPI で提供する。
単位は **frame** (左右 1 組 = 4 バイト)。録音・ミキサ (出力減衰以外)・PIO・V86 への提供 (P4)・ソフト合成器 (P2) は含まない。

## 1. ハードウェアの事実 (典拠つき)

| 項目 | 事実 |
|---|---|
| 検出 | **一続きの手順** (往復 2 R1: MODE1 のまま I25 を選ぶと I9 に化けて正常な装置を NOSYS にする): (a) `0F43h` の WSS ID (下位 6 ビット = 000100b) → (b) R0 が 0x80 でなくなるまで待つ (INIT) → (c) I12 の ID3-0 = 1010 を確認し、**MODE2 を書いて読み戻す** → (d) I25 の V2-0 = 100 (CS4231) / 101 (CS4231A) を確認 (DS139PP2 p.36/38)。`A460h` のサウンド ID は**参考情報**: NP21/W の `SNDboard=0x64` (86 + MATE-X PCM) では PCM の ID ポートが **B460h** に移る (`cs4231io.c:261`) ので、A460h を必須にすると NOSYS になる (往復 1 B1)。0C2Bh/0C2Dh の再配置はしない (既定 0F40h〜) |
| ポート | `0F44h` R0 Index (INIT / MCE / TRD / IA4-0)、`0F45h` R1 Data、`0F46h` R2 Status (INT bit0、**どんな値でも書けば INT が消える**)、`0F47h` R3 PIO (使わない) |
| 割り込みと DMA | **`0F40h`**: bit5-3 = INT (001 INT0 = IRQ3 / 010 INT2 = IRQ6 / 011 INT41 = IRQ10 / 100 INT5 = IRQ12)、bit2-0 = DMA (001 #0 / 010 #1 / 011 #3、111 = 無し。NP21/W の表では 111 も #3)。NP21/W: `cs4231irq[] = {ff,03,06,0a,0c,…}`、`cs4231dma[] = {ff,00,01,03,…}`。**書くとその場で DMA の attach が変わり、NP21/W はアドレスを先頭に戻す** (`cs4231io.c:379`) |
| 選ぶ値 | **INT41 (IRQ10) + DMA #1 → `0F40h = 0x1A`**。IRQ10 は 1-1 の動的な線、LGY-98 の既定 (3/5) と FDC (#2) を避ける。IRQ12 は固定スタブ (V86)。登録に失敗したら**利用不可** (1-1 の決定) |
| **I8 (Fs & Playback Data Format)** | D7-D5 = FMT1:FMT0:C/L、D4 = S/M、D3-D1 = CFS、D0 = C2SL。**16 ビット LE 2 の補数 = 010、ステレオ = 1** → 上位 nibble **0x5**。44.1kHz = XTAL2 (16.9344MHz、C2SL=1) ÷384 = CFS 5 → **I8 = 0x5B**。22.05kHz = XTAL2 ÷768 = CFS 3 → **0x57** (DS139PP2 p.33)。往復 1 B2 の 0x9B は予約フォーマット。**XTAL2 が無い機械では 44.1k/22.05k は出ない** (XTAL1 だけなら 48/32/16k) — 実機で I17 の XTALE と実際の周波数を E6 で確かめる |
| I9 (Interface Configuration) | D7 CPIO / D6 PPIO / **D4:D3 = CAL1:CAL0** / D2 SDC / D1 CEN / **D0 PEN**。CEN/PEN 以外は **MCE 中しか書けない** (p.34)。使う値: MCE 中に **`0x08` = CAL 1 (Converter calibration、136 サンプル周期 ≒ 3.1ms @44.1k)** — CS4231 (V=100) は CAL1 が予約なので両版に共通な値を採る (往復 2 R2。DAC だけの校正 `0x10` は CS4231A 専用)。PPIO=0、SDC=0、PEN=0。稼働は `PEN=1` を MCE 無しで on-the-fly |
| I10 (Pin Control) | **D1 = IEN** (割り込みピン許可)。他は 0 |
| I11 (Error Status、読み取り専用) | **D5 = ACI** (校正中)、D6 = PUR (再生アンダーラン、R2 読みで消える)。往復 1 B4 の「bit7」は誤り |
| **I12 (MODE and ID)** | **D6 = MODE2**。I16〜I31 (I24 を含む) は MODE2 でだけ見える。MODE1 のまま Index 24 を選ぶと IA4 が無視されて **I8 に化ける** (往復 1 B3)。初期化の最初に MODE2 を立て、読み戻して確認する |
| **I14 / I15 (Playback Base)** | 16 ビット = **NS − 1** (NS = 割り込み間の frame 数。16 ビットステレオでも「サンプル」= frame、p.24)。**I15 (下位) → I14 (上位) の順に書く。上位の書き込みが Current Count をロードする** (往復 1 B5) |
| I16 (Alternate Feature Enable I) | D0 DACZ (アンダーラン時に 0 を出す。**1 にする**)、D4 PMCE。I17 D1 XTALE |
| **I24 (Alternate Feature Status)** | D4 = **PI** (再生 DMA カウントの割り込み)、D0 PU。**PI は「そのビットに 0 を書く」か「R2 に何か書く」で消える** (p.39、往復 1 非 blocker)。NP21/W は IEN=0 のあいだ PI 自体を立てない (`cs4231c.c:474`) ので IEN=0 での PI 観測は実機と同じにならない |
| INIT / MCE / 校正 | INIT 中と MCE 後のクロック再同期中は**全読みが 0x80、書きは無視** (p.19-21)。**MCE を落とした後にも 0x80 が現れる** (XTALE=0 で水晶を切り替えたとき、p.21) ので、I11 の読み値 0x80 を「ACI=0」と読んではいけない (往復 2 R3)。手順: MCE を立てる (R0 = 0x40 \| 8) → I8 を 1 回の書きで変える → **R0 が 0x80 でなくなるまで poll** → I9 を書く (**MCE を保ったまま**: `cs_write_mce(9, v)` は R0 に 0x40 \| 9 を書く。素の `cs_write` は R0 に idx だけを書くので MCE が落ちる) → MCE を落とす (R0 = 9) → **もう 1 度 R0 が 0x80 でなくなるまで poll** → **ACI (I11 D5) が落ちるまで poll** (136 周期 ≒ 3.1ms)。待ちは全部 **IF=1 で tick の期限つき** (5 tick、IF=0 の一括ロックの中に置かない) |
| PC-98 の 8237 | 1-2 の `dma_chan_setup(1, ring, 16384, DMA_DIR_FROM_MEM, DMA_MODE_CYCLIC)` (auto-init)。リングは 1-3 のプールが 64KB 跨ぎを保証。NP21/W の折り返しは専用関数が無条件にアドレスを戻すので **auto-init ビットが正しい証明にはならない** (E6 で実機) |

## 2. 設計

### 2-1. カーネル `drivers/pcm_cs4231.c` (+ `pcm_cs4231_math.c` 純粋部)

**方式の変更 (往復 3 B2/B3)**: アプリのデータを foreground から DMA リングへ直接コピーする方式は、装置の半分の切り替えや
RESYNC との競合を `gen` の検査では消せない (既に鳴ったデータは取り消せない) ので**やめる**。**foreground が書くのは
ステージング (DMA しない 16KB) だけ**、**DMA リングを書くのは `pcm_advance` (IRQ / tick、IF=0) だけ**にする。
`pcm_advance` は「装置が別の半分に移った」ことを観測したときに、**消費し終えた半分**へステージングから写す。ただし観測は遅れ得る
(IRQ 消失 + tick 遅れ) ので「観測 = 切り替え直後」とは扱わず、**写すのは装置の現在位置から次の境界まで `REFILL_MARGIN` = 512 frame
(11.6ms @44.1k、コピー 8KB + I/O の最悪より十分長い) 以上あるときだけ** (往復 4 R2)。それより近ければ写さず「連続性の喪失」として
扱う (下の 2')。書き手は advance (と停止中の `pcm_start` / RS_RESTART) だけ、IF=0。8KB の memcpy の実時間は E3 で測る
(「386 で 1ms」は見込みであって根拠ではない)。

**メモリ**: **リング 16KB (DMA、4096 frame = 半バッファ 2048 × 2)** は 1-3 の `dma_pool_alloc` から (プール 64KB のうち。82557 の 16KB と共存できる)、
**ステージング 16KB (4096 frame、DMA しない)** は `kmalloc` (KHEAP) から (PM 指示 2026-09-23、`drivers/pcm_cs4231.c`。初稿は「カーネル予算が
6KB しか無いので暫定でプールを使う」だったが解消 — §2-1 末尾「票からの逸脱」1。カーネル予算の現在値は [`02_memory.md`](../../02_memory.md) §2-1 から案内する `build/out/MEMMAP.md`)。open 時に両方を 0 で埋める。

**レジスタアクセス**: `cs_read(idx)` / `cs_write(idx, val)` / `cs_write_mce(idx, val)` (R0 に `0x40 | idx` を書いて MCE を保つ) は
**Index と Data の組を 1 つの `irq_save` の中**で行う (往復 1 B9)。MCE の列の途中 (`s_mce_busy = 1`) は IRQ / tick は装置に触らない。

**状態機械** (装置を触る遷移は全部 **tick 駆動の分割状態**にし、IRQ / tick の中で待たない。foreground の待ちは IF=1・期限つき):

| 状態 | advance の動作 | write | 遷移 |
|---|---|---|---|
| CLOSED | 何もしない | 拒否 | open → OPENING |
| OPENING | 何もしない (IEN=0) | 拒否 | 検出 + 初期化列 (foreground、期限つき) → OPEN / 失敗 → 巻き戻し → CLOSED |
| OPEN | 何もしない (PEN=0) | ステージングへ | **ステージングが 4096 frame (両半分ぶん) 以上**、または close で未転送あり → `pcm_start` → RUNNING (close なら続けて DRAINING)。半分 1 つで始めると次の write が間に合っても最初の切り替えで 2048 frame の無音が入る (往復 5 B3) ので両半分がそろうまで待つ (開始遅延 93ms @44.1k)。短いストリームは close で開始する。**close で未転送なし、または reclaim → 再生せず `irq_unregister` → `dma_pool_free` × 2 → CLOSED** (往復 4 R7) |
| RUNNING | 観測・連続性の判定・補充・番犬 (`advance_run`) | ステージングへ | close → DRAINING / 番犬・連続性喪失 → RS_STOP / reclaim → 即時 abort (下) |
| DRAINING | 同上 (`advance_run`、新規の write は拒否) | 拒否 | **drain の 3 段階** (`last_data_half` に入るのを待つ → 読んでいる → 無音の半分を読んでいる) を切り替えの観測で進め、無音の半分から出る切り替えで完了 → STOP_REQ (往復 5 B2)。番犬・連続性喪失 → **drain 失敗を記録して STOP_REQ** (往復 3 B4) / reclaim → 即時 abort |
| RS_STOP | 入口 (1 回): IEN=0 → PEN=0 → ack、`deadline` = 今 + 3 tick を**入口でだけ**設定。以後の tick (`advance_stop`): R0 != 0x80 を確かめてから **I11 の DRS (D4、DRQ 動作中) = 0** を待つ → RS_RESTART。期限切れ → STOP_REQ (失敗)。**通常の補充はしない** (PI は ack だけ) | ステージングへ | close → `close_pending` を立てて RS_RESTART の後に DRAINING / reclaim → 即時 abort |
| RS_RESTART | `dma_chan_mask` → リング 0 化 → `filled[] = 0` → `dma_chan_setup` → I15/I14 → ステージングから半分 0 (と 1) へ写す → half = 0、gen +1、番犬と連続性の基準時刻を今に → unmask → PEN=1 → IEN=1 → RUNNING (`close_pending` なら DRAINING)、`resyncs` +1。setup が失敗したら STOP_REQ (失敗) | ステージングへ | |
| STOP_REQ | 入口 (1 回): IEN=0 → PEN=0 → R2 に書いて INT を消し、I24 に 0、`deadline` = 今 + 3 tick。以後の tick (`advance_stop`): R0 != 0x80 を確かめてから DRS=0 を待つ → `dma_chan_mask` → **証拠**: I9 の PEN=0、I24 の PI=0、R0 != 0x80 → STOP_DONE。期限内に証拠が読めない → FAULTED | 拒否 | reclaim → 残りを引き継ぐ (下) |
| STOP_DONE | PI だけ ack | 拒否 | foreground (close / reclaim) が **`irq_unregister` → `dma_pool_free` (リングとステージング)** → CLOSED |
| FAULTED | 何もしない (装置に触らない、Index 書きも 0 回) | 拒否 | foreground が `irq_unregister` → `dma_pool_mark_leaked` (両方)。再 open は `OS32_ERR_IO` (再起動まで) |

`irq_unregister` と `dma_pool_free` は **foreground だけ** (ISR 文脈からの解除は 1-1 で `IRQ_ERR_CTX`)。**advance の構造** (往復 4 R1、往復 9): **まず CLOSED / OPENING / FAULTED / `s_mce_busy` 中なら装置に触らず `IRQ_NONE`** (Index 書きを
含めてアクセス 0 回。E1 で数える) → 共通の PI の ack → 状態で分岐: RUNNING / DRAINING → `advance_run` (観測・連続性・補充・番犬)、RS_STOP / STOP_REQ → `advance_stop`
(入口の 1 回の処理と、後続 tick の確認を分ける。期限は入口でだけ設定)、RS_RESTART → 再構成、それ以外 (OPEN / OPENING /
STOP_DONE / FAULTED / CLOSED) → 何もしない。**新規の write を受けるのは OPEN と RUNNING と RS_* だけ**。

**open の列と巻き戻し** (往復 1 B6/B12、往復 2 R4): (1) 状態 CLOSED を確認、OPENING に → (2) `dma_chan_mask(1)` → (3) **`0F40h = 0x1A`**
(先に新しい経路を結ぶ。DMA #1 はマスク済み、IRQ10 は未登録だが装置は IEN=0。NP21/W は `0F40h = 0` で detach して
`dmach = 0xff` になり、その状態で I8 を書くと `cs4231_control` が配列外 (`dmac.dmach[255]`) を触るので、**detach 状態で装置
レジスタを書かない**。E0 で NP21/W 側にも未 attach の防御を足す) → (4) R2 に書いて INT を消し、I9 の PEN=0 と I10 の IEN=0 を書く
(BIOS の旧設定の要因を止める) → (5) 検出 (1 の表、MODE2 まで) → (6) `dma_pool_alloc` × 2 → (7) `irq_register(10, pcm_irq, 0,
IRQ_F_SHARED)` → (8) 初期化列 (下) → (9) `dma_chan_setup(1, …, CYCLIC)` (マスクのまま) → (10) I15 = 0xFF、I14 = 0x07 → (11) owner を
公開、状態 OPEN。**失敗は逆順**: (8)〜(10) の失敗は `irq_unregister` → `dma_pool_free` × 2 (DMA 未開始・マスク中なので安全) →
PEN=0/IEN=0 のまま `0F40h` は **0x1A のまま残す** (detach しない) → CLOSED。戻り値: `OS32_ERR_NOSYS` 検出なし / `OS32_ERR_NOMEM`
プール / `OS32_ERR_BUSY` IRQ 登録 / `OS32_ERR_IO` 初期化の期限。

**初期化列** (§1 の典拠どおり。各 poll は IF=1・期限 5 tick、超えたら `OS32_ERR_IO` で巻き戻し。往復 3 B1: §1 と一致させた):
R0 が 0x80 でなくなるまで poll (INIT) → I12 に MODE2=1 を書き、読み戻して MODE2 を確認 → **`cs_write_mce(8, 0x5B)`** (44.1k。
22.05k は 0x57) → R0 が 0x80 でなくなるまで poll (クロックの再同期) → **`cs_write_mce(9, 0x08)`** (CAL 1、PPIO=0、SDC=0、PEN=0。
MCE を保つ) → **`cs_write_mce(16, 0x01)`** (DACZ=1、**SPE=0** — SPE は MCE 中でしか変えられず、BIOS の旧設定で 1 なら DAC が
シリアル入力を使って DMA のデータが出ない。往復 5 B6) → R0 = 9 (MCE を落とす) → **もう 1 度 R0 が 0x80 でなくなるまで poll** → I11 の ACI (D5) が落ちるまで poll (136 周期
≒ 3.1ms) → I6/I7 = 0 (0dB、LDM/RDM = 0) → I24 に 0 (PI を消す) → I10 の IEN=1。

**`pcm_start`** (OPEN → RUNNING、foreground、装置は停止中。リングを書く例外の 1 つ): ステージングから半分 0 へ min(2048, staged)
frame を写し (`filled[0]`)、残りがあれば半分 1 へも (`filled[1]`)、残りは 0 のまま。**最後に正の frame を置いた半分を `last_data_half`
に、drain の段階は `last_data_half == 0` (再生は半分 0 から始まる) なら「読んでいる」、1 なら「入るのを待つ」に** (往復 5 B2、
往復 6 R3: 半分 0 だけの短いストリームを「待つ」から始めると 1 周遅れて close の期限を超える)。→ `irq_save` の中で half = 0、pos = 0、gen = 1、**観測・番犬の基準時刻
= 今、TC/PI の証拠を消し (`dma_chan_ack_tc`、I24 に 0)**、状態 RUNNING を公開 (往復 5 B5) → `dma_chan_unmask(1)` → I9 の PEN=1
(MCE 無しで書ける)。RS_RESTART も同じ初期化を行う。

**`pcm_advance()`** (IRQ handler と tick フックの両方がこれだけを呼ぶ。手順は**状態表と同じ規則**で、表が正 — 往復 5 B4):

0. **共通**: CLOSED / OPENING / FAULTED / `s_mce_busy` 中は装置に触らずに終わる (`IRQ_NONE`)。それ以外は I24 を読み、PI があれば
   I24 に 0 を書いて消す (handled = 1)。**PI は割り込みの受理 (この線は自分の要因だった) にだけ使い、連続性の判定には使わない**
   (往復 8: PI は回数を持たないレベルのフラグで、古い PI が残ったまま次の境界が来ても同じ 1 にしか見えない = 合流する。位置と
   組み合わせても 0 回と 2 回、1 回と 3 回は区別できない)。状態で分岐:
   RUNNING / DRAINING → 1〜5、RS_STOP / STOP_REQ → `advance_stop` (表の後続 tick の確認だけ。**補充も観測の更新もしない**)、
   RS_RESTART → 再構成、それ以外 → 終わり。
1. `dma_chan_remaining(1, &left, &tc)` (1-2)。**成功なら** `pos_bytes = (16384 − left) % 16384`、`h1 = pos_bytes / 8192`、
   `p1 = pos_bytes / 4`。`tc` は**使わない** (往復 6 R1/R2: NP21/W の CS4231 専用 DMA 経路は 8237 の TC を立てないし、HAL の
   `(left, tc)` は同時点の証拠ではない。`dma_chan_ack_tc(1)` は診断のために呼ぶだけ)。`now = sys_time_now` (負なら
   `tick_count × 10000`。単調性は前回値との max)。**`-EAGAIN` なら pos / half / filled は更新せず** `now` だけ取って 5 へ。
2. **連続性の判定 (位置だけ)** (往復 8 で確定)。前回 `(h0, p0)` (start / restart 直後は `(0, 0)`、有効な基準)、今回 `(h1, p1)`:
   - `h0 == h1` かつ `p1 >= p0`: 境界無し → **連続 (0 回)**。
   - `h0 == h1` かつ `p1 < p0`: 同じ半分で戻った = 1 周回った (2 境界) → **喪失**。
   - `h0 != h1`: → **連続 (切り替え 1 回)**。
   **保証の範囲**: この判定が正しいのは**成功した位置取得どうしの間隔が 1 半周期 (46.4ms @44.1k、92.9ms @22.05k) 未満**のとき
   (`p` はリング全体の frame 番号 0..4095。`dma_chan_remaining` は `-EAGAIN` を返し得るので、呼び出しの間隔 10ms だけでは足りず、
   連続した `-EAGAIN` による空白も保証条件に含める — 往復 9)。tick (10ms) がそれを与えるのは HAL の契約 (IF=0 の区間 < 10ms、
TASK_HAL_WIRING 1-5) の中だけで、契約を大きく破る空白 (≥ 2 半周期) では
   **0 回と 2 回、1 回と 3 回は区別できない** (未補充の半分を再読しても `p1 >= p0` なら見逃す)。これは**保証外**として記し、
   救済は 5 の番犬 (進行が止まった場合) と `p1 < p0` の場合だけ (W6 の残件。HAL の合格でも排除されない)。
   喪失は `repeats` +1 → RUNNING なら **RS_STOP**、DRAINING なら **drain 失敗を記録して STOP_REQ**。連続なら **`changed = (h1 != h0)`、
   `old = h0`、`progressed = (changed || p1 != p0)` を先に確定してから** `(h0, p0) = (h1, p1)` に更新 (往復 6 R6、往復 7 B2)。
   `progressed` なら `last_progress = now`。start / restart は `(h0, p0) = (0, 0)`、`last_progress = now` で初期化する。
3. **切り替え** (`changed`): gen +1。**判定は消す前に**: RUNNING で `filled[h1] < 2048` なら `underruns` +1。DRAINING は末尾の 0 埋めを
   数えない。
4. **補充** (切り替えを観測したときだけ): 装置の現在位置から `h1` の末尾まで **`REFILL_MARGIN` (512 frame) 以上**あることを確かめて
   から (往復 4 R2)、消費し終えた `old` へステージングから min(2048, staged) frame を写し (IF=0、≤ 8KB、ステージングの物理末尾を
   跨ぐときは 2 回に分ける — 往復 4 R6)、残りを 0 で埋め、`filled[old]` = 写した数、`stg_r` を進める。**正の frame 数を写したときだけ
   `last_data_half = old` とし、drain の段階を「入るのを待つ」に戻す** (0 frame の補充は更新しない — 往復 4 R4)。余裕が無ければ
   写さずに `repeats` +1 → 2 の喪失と同じ扱い。
4'. **drain の段階** (RUNNING でも追跡し、完了の遷移は DRAINING だけ — 往復 6 R4。**1 回の観測で 1 段階だけ**進める): 補充の後に `h1` で進める: 「入るのを待つ」で
   `h1 == last_data_half` → 「読んでいる」。「読んでいる」で `h1 != last_data_half` → 「無音の半分を読んでいる」。「無音の半分」で
   さらに切り替え → 「出た」。**DRAINING で「出た」かつ `staged == 0`** → drain 完了 → STOP_REQ (DMA 完了と DAC 出力は別なので
   無音の半分を丸ごと通す。ステージングにデータが残っていれば完了せず、次の切り替えで補充されて段階が戻る)。
5. **番犬** (往復 6 R5): `now − last_progress` (位置が進んだ最後の時刻。`-EAGAIN` や同じ位置の読みでは更新しない) が rate から
   計算した 2 半周期を超えたのに PEN=1 なら → RUNNING では **RS_STOP**、DRAINING では **drain 失敗を記録して STOP_REQ**。
   時計は長い IF=0 で過小評価する側なので、番犬は遅れて発火することはあっても早く発火することはない (安全側)。

**`pcm_write`** (foreground、往復 3 B2/B3 で DMA メモリに触らない形に): 受け付けるのは **frame の倍数** (`bytes & 3` は切り捨て、
0 なら 0)。入力の検査は `ring3_user_range_ok(buf, bytes)` (+ 加算あふれ) を **wrapper が先に**行う。`irq_save` の中で
`stg_w` とステージングの空きを取り、空きまでの長さを予約 → **IF=1** でユーザのバッファからステージングの `[stg_w, stg_w + n)`
へコピー (**物理末尾 (16KB) を跨ぐなら 2 回の memcpy に分ける**。読み側 (補充 / start) も同じ — 往復 4 R6) (この区間は公開前なので advance は読まない。途中の #PF は既存のフォールトガードから `exec_exit` → 回収へ。
公開していないので状態は無傷) → `irq_save` の中で `stg_w` を進めて公開 (RS_* の途中でも安全: リングの再構成はステージング
と独立)。**公開の直後、状態が OPEN で staged ≥ 4096 なら foreground の続きで `pcm_start` を呼ぶ** (往復 6 R7: 満杯を作った
write が開始しないと次の write は 0 のまま OPEN に留まる)。空きが無ければ 0 を返し、アプリは `sys_yield` して再試行 (yield は GUI では park、CUI/CPL=0 では hlt 1 回。driver の
中では yield しない)。**アプリのバッファを IRQ から読むことはしない**。

**`pcm_close` = drain、期限つき** (往復 1 B10/B11、往復 2 R9、往復 3 B4): RUNNING で DRAINING に (OPEN でステージングに
データがあれば `pcm_start` してから)。advance がステージングを空にし、最後のデータの半分を通過し、さらに 1 半分 (0) を
通過したら STOP_REQ に入る。foreground は **STOP_DONE か FAULTED になるまで IF=1 で待つ**。期限は**半分単位**で導く (往復 4 R5): 残りのステージングを
写し切る半分の数 `ceil(staged / 2048)` + 現在の半分の残り 1 + データの半分の通過 1 + 0 の半分の通過 1 + 停止確認の 3 tick →
`(ceil(staged / 2048) + 3) × H + 3 tick` を tick に切り上げ (H = 2048 × 1000 / rate ms)。staged = 0 で 44.1k は 17 tick、22.05k は 31 tick。期限切れは番犬と同じく drain 失敗 → STOP_REQ
を待つ。STOP_DONE なら `irq_unregister` → `dma_pool_free` × 2 → CLOSED、戻り 0 (drain 失敗が記録されていれば `OS32_ERR_IO`)。
FAULTED なら leaked、`OS32_ERR_IO`。**「PI が来ない」は証拠にしない**。末尾の 0 埋めは underrun に数えない。

**RESYNC と停止のサンプル境界** (往復 3 B6): PEN=0 は発行済みの DMA 要求の最後のサンプル転送が終わってから効く (DS139PP2
p.16/19)。だから **PEN=0 の後、I11 の DRS (D4: PDRQ/CDRQ が動作中) が 0 になるのを tick で待ってから** `dma_chan_mask` する
(RS_STOP / STOP_REQ の表)。CS4231 (V=100) と CS4231A の I11 の並びは同じ (Appendix A に差分無し — **実装時に再確認**)。再始動
(RS_RESTART) は mask 状態でリングを組み直してから unmask → PEN=1 なので、サンプル内のバイト位置は先頭からそろう。

**所有と回収** (往復 1 B13、往復 2 R10、往復 3 非 blocker): owner は **既存の資源 owner の規則と同じアプリ ID**
(`appslot_cur()` / `snd_focus` が使う id。CPL=0 のシェルは APP_ID_SHELL = 1)。`pcm_write / status / close / set_volume` は owner
一致を要求 (不一致は `OS32_ERR_INVAL`)。回収は `exec_reclaim_owned(id)` に `pcm_reclaim(id)` を足す (呼ばれる時点では `g_cur_app`
は親に戻っているので、**保存した owner と引数 id だけを照合**): **待たない abort** — 状態が RUNNING / DRAINING / RS_* なら
IEN=0 → PEN=0 → ack → `dma_chan_mask` (DRS は待たない。捨てるストリームなので境界は問わない) → 証拠の読み戻し 1 回 →
`irq_unregister` → 証拠があれば `dma_pool_free` × 2、無ければ leaked/FAULTED。STOP_REQ / STOP_DONE の途中なら**残りの手順を
引き継いで 1 度だけ**解放する (`finalized` フラグ。close 側の待ちは longjmp で戻らないので二重解放は起きないが、契約として
1 度に固定する)。**reclaim は IF=0 (fault の回収) からも来る**ので tick 待ちを再開せず、その場で待たない abort と 1 度だけの後始末を行う。**全状態の表** (往復 4 R7): OPENING (foreground の途中なので reclaim は来ない) / OPEN → 再生せず解放 /
RUNNING・DRAINING・RS_STOP・RS_RESTART → 即時 abort / STOP_REQ → 残りを引き継ぐ / STOP_DONE → 解放 / FAULTED → unregister + leaked /
CLOSED → 何もしない。正常終了・fault・CTRL+STOP・park 中の kill で同じ経路。

**レート**: 44100 / 22050 だけ。**音量**: I6/I7 の LDA/RDA (6 ビット、1.5dB 刻み、減衰値に線形) を `percent` (1〜100、100 = 0dB)
から写し、**0 は D7 の LDM/RDM (ミュート) を立てる** (1〜100 で落とす)。101 以上は `OS32_ERR_INVAL`。

#### 進捗 (2026-09-23、worktree `wt/pcm`、基点 17e3f46)

記録は [TASK_PCM_CS4231_RESULTS](../../archive/v3/TASK_PCM_CS4231_RESULTS.md) へ移した。

**票からの逸脱 (PM の判断が要る)**:

1. **ステージングは `kmalloc` (KHEAP) から取る** — プールは 64KB しかなく
   82557 の 16KB と分け合うため (PM 指示 2026-09-23)。§2-1「メモリ」の
   「暫定でプールを使う」は解消。リングだけが `dma_pool_alloc(16384, 4096)`。
   解放はリングの leaked と独立で、ステージングは**必ず `kfree`** してよい
   (装置が触らないメモリなので)。
2. `OS32_ERR_*` に **BUSY と NOMEM が無い**ので、BUSY → `OS32_ERR_FULL` (-13)、
   NOMEM → `OS32_ERR_NOSPC` (-4) に写した (`PCM_ERR_BUSY` / `PCM_ERR_NOMEM`)。
   新しい番号は足していない。
3. `drivers/pcm_cs4231{,_math}.c` だけ **`-Os`** で積んでいる。基点 17e3f46 の
   予算 (残り 6.2KB) では `-O2` だと ASSERT にちょうど触れて 1 バイトも余らない。
   `MEM_KERNEL_IMAGE_MAX` が 596KB になった枝へ着地したら `build/kernel.mk` の
   2 行を消して既定 (`-O2`) に戻してよい (→ 596KB への合流で外した、2026-09-23、`build/kernel.mk` の注記。残り予算の現在値は 02_memory §2-1)。**他所のコードは 1 行も削っていない。**
4. 完了条件の `staged == 0` は `frames > 0` の段階戻しと重なっていて
   `pcm_obs` の中からは到達できない (残りがあれば必ず補充されて段階が戻る)。
   **票どおり両方残した**が、変異試験にはできないので `pcm_cs4231_tdd.md` に
   その旨を書いた。
5. `advance` の中で `dma_chan_ack_tc` を呼ぶのは **`tc` が立っていたときだけ**に
   した (判定には一切使わない。診断の通知を落とすためだけ)。

**PM が NP21/W で見るもの (E0 / E2 が整ってから)**:

- E2: 起動行 `[pcm] CS4231 v=100 irq 10 dma 1 fmt 0x5B`
  (装置が無いいまの trial ini では `[pcm] none` が正しい姿)。
- E3: `pcm_test` の 5 秒。`/api/sound?pcm=1` の生 frame で
  右チャネル = frame 番号が連続していること、左が 1kHz (44.1 frame 周期)、
  `pcm_status` の free が書き込みで減り**半分の補充で 8KB 戻る**こと、
  200ms 止めて underrun が増え、再開後に右の番号が飛ばないこと。
- E4: 共有 IRQ の実証。E5: `pcm_test` を CTRL+STOP で殺して `pcm_reclaim`。
- E6: 実機 (XTAL2 の有無、INIT 中の書き無視、auto-init ビット、耳)。

### 2-2. KAPI (v61、末尾追記。出力引数は出力保護つき、入力は `ring3_user_range_ok`)

```c
int  pcm_open(u32 rate);                          /* 0 / NOSYS / BUSY (他の owner が open 中) / INVAL (rate) / NOMEM / IO (初期化の期限) */
int  pcm_write(const void *buf, u32 bytes);       /* 受け取ったバイト数 (frame の倍数、0 = 満杯) / 負 = 未 open・非 owner・範囲外 */
int  pcm_status(u32 *free_bytes, u32 *counters);  /* free_bytes = ステージングの空き。counters = (underruns<<24) | (repeats<<16) | resyncs (8/8/16 ビット、255/255/65535 で飽和。drain 失敗は close の戻り値)。出力 2 本、1-5 と同じ保護 */
int  pcm_close(void);                             /* drain、期限つき。0 / 負 (abort に落ちた = OS32_ERR_IO) */
int  pcm_set_volume(u32 percent);
```

### 2-3. 純粋関数 (ホスト試験)

`pcm_advance` の判定 (left → pos → half、**連続性の判定表 (h0 × h1 × p の全組)**、**保証内の正常列 (1 半周期未満の間隔): 0 境界 (同じ半分で p 進む)・1 境界 (半分が変わる。1 → 0 の折り返しは p が戻って見えるが別の半分なので
正常) が正しく出ること、喪失列 (同じ半分で p が戻る = 理想的な単調転送では 1 半周期未満に到達しないので、遅れた観測として与える) で repeats
+ RS_STOP、2 半周期以上の空白では 0/2 回・1/3 回が区別できないことを「保証外」として試験の期待に書く。座標は票と同じリング全体の frame 番号**
(往復 8/9)、**補充の余裕 (REFILL_MARGIN)**、切り替えの検出と **消す前の**
underrun 判定、両半分満杯の正常切り替えで underrun 0、**drain の 3 段階** (start / restart / 補充で `last_data_half` を置いた各場合、0 frame の
補充で動かない、`write(1 frame) → close`)、末尾後の無音、`-EAGAIN` でも番犬が動く、番犬の期間の計算、**遅れた観測 (同じ半分で `p1 < p0`) で repeats +
RS_STOP、DRAINING では STOP_REQ**、**進行の番犬 (同じ位置が続く)**、**満杯の write からの自動開始**、**「出た」+ staged > 0 は完了しない**)、ステージングの予約 / 公開 / 消費 (frame 倍数、空き、**物理末尾の
2 分割**、2047 + 2 + 消費後の 4096 の反例)、`pcm_start` の配り方 (close 起動の 2048 未満・自動開始の 4096) と基準時刻・証拠の初期化、レート → I8 の値と半周期、close の期限式
(staged と rate)、percent → 減衰とミュート、初期化列と停止列と RS_* の列を**ポート書きの列**として返して照合 (順序の変異: I14 → I15、
MCE 無しの I9、MODE2 無しの I24、DRS を待たない mask、期限を毎 tick 初期化する)、状態機械の遷移表 (上の表の全部と不正遷移の拒否、
close / reclaim が各状態から 1 度だけ解放すること)。

## 3. 受入

| ID | 見るもの | 手段 |
|---|---|---|
| E0 | **前提**: NP21/W の `/api/sound` に CS4231 を足す: PEN / IEN / MODE2 / I8 / Base / `bufdatas` / **PI の累積回数** (NP21/W の `totalsample` は周期内残量なので新設) / PU 回数 / DMA ch と IRQ / 0F40h の値、および **直近 1 秒の出力 PCM を base64 で返す `?pcm=1`**: 採取点は `cs4231g.c` の DMA 読み取りバッファ → ミックス前 (コーデックの rate、16 ビットステレオの生 frame、音量とホスト rate 変換の**前**、他音源が混ざる前)。**未 attach (`dmach = 0xff`) での `cs4231_control` の配列外アクセスの防御**も E0 で入れる。NP21/W の CS4231 専用 DMA 経路は 8237 の TC を立てない (`cs4231c.c` の `DMAEXT_END` は `dmac.stat` を触らない) — 設計は TC に依存しないのでそのまま (診断の `tc` は NP21/W では常に 0 と記す)。NP21/W は自前フォーク (`make build && make deploy` は NP21/W 停止が要る) | np21w-src |
| E1 | ホスト試験 (変異つき): 2-3 の全部 | `check-par` |
| E2 | NP21/W (**別 trial ini で `SNDboard=0x64` = [D2]**): 検出 (0F43h + I12 + I25) → 起動行 `[pcm] CS4231 v=100 irq 10 dma 1 fmt 0x5B` (NP21/W のこの構成は I25 = 0x80 → V = 100。CS4231A の V = 101 は実機 E6 で) | NP21/W |
| E3 | CPL=3 の `pcm_test`: **左 = 1kHz 正弦、右 = frame 番号の下位 16 ビット** (左右取り違え・旧半分の反復・ミュートを見分ける) を 5 秒 → E0 の PCM (生 frame) を取って左右の内容を照合 (右の frame 番号は連続、左は 1kHz の周期 44.1 frame ± 1、許容: 欠落 0 frame)、PI 累積 ≒ 5 × 44100 / 2048、underruns 0、PCM の採取は**開始直後・途中・close 直前・close 後**の 4 区間 (close 中の末尾と無音区間は close 後に残る採取で照合)。`pcm_status` の free (ステージングの空き) が書き込みで 4 バイト単位に減り**半分の補充で 8KB 戻る**。書き込みを 200ms 止めて無音 (underruns +、意図した停止区間は別に集計) → 再開後に右チャネルの番号が飛ばずに続く。ミュート (percent 0) は I6/I7 の D7 と音量適用後の出力で別に確認 | NP21/W |
| E4 | 共有 IRQ の実証 (証明範囲を明記): PCM だけ / 偽装置だけ / **同時 = 試験用 hook を実 PI の IRQ10 dispatch の入口 (PCM が I24 を ack する前) に置き、PI=1 を確認してから偽 pending を立て、同じ dispatch で両方の handler の結果を記録** / 2 巡目 = 偽 handler の初回走査の後に PCM handler が偽要因を立てる順序 / 偽装置の tick 回収 = IRQ を注入せず pending だけ立てる。偽装置は 2 つ目の物理要因ではない (電気的共有は実機の 82557 で) | NP21/W |
| E5 | CPL=3・CUI の `pcm_test` を CTRL+STOP で殺す → `pcm_reclaim` で PEN=0・DMA マスク・次の open が通る。GUI の park 中の kill と CPL=0 は対象外と明記 | NP21/W |
| E6 | 実機 (Ra266) — 独立した受入: MODE1 の初期状態、INIT 中の書き無視、MCE / 校正の待ち、Base 上位書きのロード、DAC の初期ミュート、**XTAL2 の有無** (44.1k が出るか)、BIOS の 0F40h 旧値、1MB 超 DMA、DRQ/FIFO の停止タイミング、auto-init ビット、実際の音 (耳) | 実機 |

### 3-1. 受入の記録 (PM、2026-09-23、NP21/W。ユーザー承認: trial ini の複製に `SNDboard=64`、フォーク exe の配備)

| ID | 結果 |
|---|---|
| E0 | 合格: NP21/W フォーク `wt/pcm-api` → main (2dca70f)、配備。`/api/sound` に `cs4231` (PEN/IEN/MODE2/I8/Base/経路/PI 累積/TC) と `?pcm=1` (DMA 直後の生 frame 1 秒、base64)。未 attach の防御も入った。`pu_count` は NP21/W に PUR/PU を立てる箇所が無いので無し |
| E1 | 合格: ホスト試験 21 ケース、変異 24 本 RED、`make check` 通過 |
| E2 | 合格: 新 trial ini (`np21w-trial-pcm-sndboard64.ini`、原本は無変更) で起動 → `[pcm] CS4231 v=100 irq 10 dma 1 fmt 0x5B`、kselftest 197/197。現行の trial (CS4231 無し) では `[pcm] none` と `pcm_test nodev` → NOSYS |
| E3 | 合格: `pcm_test` 5 秒 (左 1kHz / 右 frame 番号) → underruns 0、repeats 0、resyncs 0、close → 0、PI 累積 109 (= 5 × 44100 / 2048)、生 frame の照合: 右は 41,368 frame で欠落 0、左は 1,000Hz、末尾の 0 埋め 2,732 frame。`pcm_test short` (1 frame → close) → 0。停止後 PEN=0/IEN=0。**切り分けた欠陥 2 件**: (1) close の待ちループが ISR の書く `state` を素の読みで回していて、コンパイラが読みをループの外へ持ち上げ、期限まで回って毎回 IO/FAULTED になった → `volatile` 経由に (POLICY_DEBUG §4-56)。(2) 試験側の位相刻み `(1024 × 1000) << 16` が 32 ビットを溢れて左が 40Hz になっていた → 12 ビット先に上げて割る。**未実施**: 書き込みを 200ms 止めて underrun を数える経路 (試験に無い)、開始直後 / close 直前の採取区間、ミュートの別確認、8KB コピーの実時間 |
| E4 | 未 (共有 IRQ の偽装置 hook は未実装) |
| E5 | 未 (`/api/cmd` が返るまで `/api/key` を送れないので CTRL+STOP を注入できない。kselftest から `pcm_reclaim` を直接呼ぶ形に置き換える案) |
| E6 | **部分 (2026-09-23)**: 実機 Ra266 の起動画面に `[pcm] CS4231 v=101 irq 10 dma 1 fmt 0x5B` — 検出・ルート設定・I25 の版 (0x65 = 101、NP21/W の 0x80 と違う実チップ値)・初期化まで通った。再生 (音・XTAL2・DMA) は未 — FD に `pcmtest` (8.3 名) を足したので次の実機回で `pcmtest short` |

残件 (実装レビュー往復 2 の非 blocker): 生成 wrapper 経由の `pcm_write` 契約試験 (範囲・あふれ・端数の順序)、交互試験の補強 (2 段目の期限境界で STOP_DONE、release → 再 open の claim 引き継ぎ、親 owner に戻った状態の reclaim)。

診断: `pcm_diag_fault_site` / `pcm_diag_evidence` / `pcm_diag_df_site` / `pcm_diag_stop_calls` などを kernel.map から読める (E3 の切り分けで使った。残す)。

## 4. しないこと

録音、PIO、8 ビット、モノラル (アプリ側で複製)、ミキサの入力側、86 型 PCM の FIFO の模倣 (P4)、`0C2Bh/0C2Dh` の再配置、DMA #0/#3、I8 の XTAL1 系レート (48k 等。XTAL2 が無い機械への対応は E6 の結果で決める)。

## 5. 決裁が要る点

1. NP21/W の trial ini の複製に `SNDboard=0x64` (PC-9801-86 + MATE-X PCM、既存の OPN も残る) を入れる ([D2]。原本は触らない)。
2. E0 の NP21/W フォーク拡張は NP21/W の停止 → 配備 → 起動を伴う ([D1] と同じ型)。
