# TASK_HAL_WIRING — 結線の土台 (割り込みの動的登録 / 8237 DMA の共通部 / DMA プール / PCI の結線表 / µs 時計)

> 状態: **受入完了・実機確認待ち (2026-09-23)** — NP21/W 受入、Codex 実装レビュー Approve、v2.1 に同梱。実機は W0 と W4 が合格 (W4: `timetest` TIME PASS・`clamped 0 of 10000`、archive の HANDOVER_2026-09-22 §次の実機の回)。残: W7 (キャッシュ整合、実機)。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-fable-5-1`、2026-09-23) / それまでの状態: **実装済み・NP21/W 受入済み (2026-09-23、§3-1)。Codex の実装レビュー往復 1 = Approve (非 blocker 5 件、うち 2 件はコード修正、3 件は本文に反映)。**実機 (Ra266、2026-09-23): W0 合格 (120 秒で uptime +120 秒、修正前は +148 秒相当)、82557 は Interrupt Line 3 / PIN A**。W4 (2.4576MHz の `ktime_clamp_count`、`time_test`) と W7 は次の実機回 — FD に `time_test` を **`/bin/timetest.bin`** として足した (`build/image.mk`。FAT12 は 8.3 のみ = `time_test` は NOTFOUND になる)、kselftest の行は画面で読む**。
> ユーザー指示 2026-09-23: 「結線の土台の票の設計を先に起こす」。
> 往復記録: v1 → Codex 往復 1 (B1〜B14、Request changes) → v2 → Codex 往復 2 (R1〜R9、Request changes) → v3 → Codex 往復 3 (B1〜B7、Request changes。R2/R3/R4/R6 は閉、R1/R5/R7/R8/R9 は部分) → v4 → Codex 往復 4 (R1〜R6、Request changes。B3/B6/B7 は閉、B1/B4/B5 は部分、B2 は未閉) → v5 → Codex 往復 5 (B1〜B4、Request changes。R1〜R6 は閉、R4 は残件移管) → v6 → Codex 往復 6 (R1〜R3、Request changes。B1/B3 は閉、B2/B4 は部分) → v7 → Codex 往復 7 (B1〜B3。R2 は閉、R1/R3 は部分。B1/B2 = 実装前に決める設計判断、B3 = 実装レビューで可) → v8 (PM の決定を固定) → Codex 往復 8 (R8-1〜R8-3、Request changes。B1〜B3 は閉、1-2/1-3 に新規 blocker 無し) → v9 → Codex 往復 9 (R9-1〜R9-3、Request changes。R8-1 は閉、R8-2/R8-3 は部分。1-1 の dispatch/EOI/storm と 1-2/1-3 に新規 blocker 無し) → v10 → Codex 往復 10 (R10-1 の 1 件のみ、Request changes。R9-1/R9-3 は閉。L-B 持ち越し無し) → v11 (PM の決定で固定) → Codex 往復 11 (R11-1 の 1 件。競合・shlib 属性・COW は新規無し) → v12 → **Codex 往復 12: Approve** (新規 blocker 無し、注意 7 点) → v13。

正典の関係: [`PLAN.md`](../../archive/v3/PLAN.md) §3-1 (HAL の棚卸し)、§3 (ドライバの動的読み込み — この票の「取り決め」を後で外部モジュールに開く)、
[`../realhw/TASK_LAN_82557.md`](../realhw/TASK_LAN_82557.md) (L-B 82557 が最初の顧客)、§5-5 (PCM リングとタイマが 2 番目の顧客)。

## 0. なぜ要るか

PCI の装置は **BIOS が IRQ を割り当て、複数の装置が 1 本の IRQ を共有し得る**。いまの OS32 は `kernel/isr_stub.asm` が
装置ごとに固定の C ハンドラを `extern` で結んでおり (`timer_handler` / `kbd_irq_handler` / `serial_irq_handler` /
`fdc_irq_handler` / `mouse_irq_handler` / `ne2k_irq` (`irq_stub_nic_3/5/6`)、それ以外は `IRQ_UNEXP` で EOI だけ)、
**起動時に読んだ IRQ 番号でハンドラを結ぶ口が無い**。DMA も `dma_setup()` が `drivers/fdc.c` の中で ch2 決め打ち、
DMA に使えるメモリは静的配列 + 整列で取っている。82557 (バスマスタ、IRQ は BIOS 次第) と CS4231 (8237 の #1/#3) を
載せるには、この 4 つを装置から切り離した層にする必要がある。

**v3 で変えたこと (Codex 往復 2)**: 共有 IRQ の**処理継続**は「タイマ tick の回収走査 + ストーム時のマスク」で契約化 (R1)、
EOI は `irq_finish()` の 1 経路で、IRQ15 のスプリアス検査を handled から独立 (R2)、クロック判定は **PG=0 の起動専用経路**で
BIOS ワークを読んで保存 (R3)、DMA プールは **SHM ではなく SQLite 帯の予約域の固定番地** (SHM・GUI オフセット・SDK・
アプリは無変更、R4/R5)、プールは span 表で `free` を識別 (R6)、`remaining` は **TC の保持 + 一致規則の明文化** (R7)、
時計は **IRR を挟み読みするスナップショット手順** (R8)、probe の失敗は「戻して次へ」と「隔離して打ち切り」を区別 (R9)。

## 1. 設計

### 1-0. 前提の修正: PIT の分周をクロック判定に合わせる (既存バグ)

`kernel/idt.c` の `pit_init()` は `PIT_CLOCK` = 1,996,800Hz 決め打ちで分周する。**2.4576MHz 系 (実機 Ra266 は
`0000:0501h` bit7 = 0)** では 100Hz を頼んでも **tick = 8.125ms (123Hz)** になっており、`tick_count` を使う待ち・番犬・
校正すべてが 23% 速い。

- **`sysclk_detect()` (新設、`kernel/sysclk.c`)**: `paging_init()` より**前** (PG=0、物理番地がそのまま見える) に
  `kernel.c` から 1 回だけ呼び、`0000:0501h` bit7 を読んで `g_sysclk_hz` (1996800 / 2457600) と `g_sysclk_8mhz` に保存する。
  以後は**保存値だけを読む**。既存の `serial_detect_clock()` は CR3 が master と一致しないと何もしない (PG=0 では
  CR3 が未設定なので現状は**検出をスキップして既定値のまま**) — これを保存値の参照に置き換え、CR3 検査を消す。
  シリアルの `serial_init` (KAPI 経由、CPL=3) も同じ保存値を見る。
- **`pit_init(hz)`**: `reload = g_sysclk_hz / hz`。**この票では hz = 100 に限定** (両クロックで reload = 19968 / 24576、
  整数、周期はちょうど 10ms)。100 以外を頼まれたら負を返して既定 100 にする (1kHz は §5-5 の別票で、
  そのときは実周期 = `reload × 1e6 / g_sysclk_hz` を µs で持つ汎用式に広げる)。`struct pit_setup` (実装は `kernel/pit_math.h`: `clk_hz, hz, reload, period_us, mode, valid`、i386 で 20B) を
  記録する (1-5 は `pit_get_setup()->valid` と `mode == 0x34` (mode 2) を検査してから使う)。
- 順序: `sysclk_detect → pit_init → irq_enable(0) / sti → cpu_calibrate` (kernel.c 184〜204 の並びに `sysclk_detect` を
  足すだけ。校正は PIT の後という現状を保つ)。
- 効果: NP21/W (1.9968MHz 設定) では reload = 19968 のまま**不変**。実機では tick が 10ms になり、
  `cpu_delay_us()` の実時間・FDC のタイムアウト (tick 単位)・シリアルの TX 予算 (tick 単位) が**全部変わる**ので、
  W0 に回帰を入れる。この票の**最初の着地単位** (決裁 2)。
- **進捗 (2026-09-23、worktree `wt/pit-clock`)**: 実装済み。`kernel/sysclk.{c,h}` / `kernel/pit_math.{c,h}` を新設、
  `pit_init` は `int` を返して `pit_get_setup()` で `struct pit_setup` を公開、`serial_detect_clock()` は廃止して
  シリアルも保存値を見る。ホスト試験 `make check-pit-clock-host` (5 ケース / 変異 5 本すべて RED) と
  kselftest の `test_pit_setup()` を追加、経緯は `docs/POLICY_DEBUG.md` §4-54。
  `make kernel` は警告の増加なしで通る。**W0 (実機での tick 実時間の回帰) は PM の検証待ち**。

- **W0 の NP21/W 側 (PM、2026-09-23)**: 着地 (7269639、`make check` 全通過)。`kselftest_fail` = 0 (`test_pit_setup` 込み)、
  ホストの単調時計で 30 秒測って **3003 tick = 99.3Hz (10.07ms、HTTP の往復ぶんの誤差)** — 1.9968MHz 設定では不変。
  **実機側は未検証** (ユーザーの次の実機回): FD を最新の `images/os32_boot.img` で書き直し → 起動画面 2 行目に
  `PIT 2.4576M` が出ること → `rshell_serial.py cmd uptime` をホストの時計で 120 秒あけて 2 回 (直す前なら
  ゲストは 148 秒進む、直っていれば 120±1 秒)。

### 1-1. 割り込みの動的登録 — `kernel/irq.c` / `kernel/irq.h`

```c
#define IRQ_NONE      0   /* 自分の要因ではない */
#define IRQ_HANDLED   1   /* 自分の要因を見つけて落とし、残件は無い */
#define IRQ_DEFERRED  2   /* 自分の要因だが、予算超過 / busy で残件がある (装置側の割り込みはマスクしてある) */
typedef int (*irq_handler_fn)(unsigned int irq, void *arg);
int  irq_register(unsigned int irq, irq_handler_fn fn, void *arg, unsigned int flags);
int  irq_unregister(unsigned int irq, irq_handler_fn fn, void *arg);
#define IRQ_F_SHARED  0x01
```

**対応 IRQ (契約)**: 動的登録を受けるのは**共通スタブ `irq_stub_common %1` に結ばれた IRQ だけ**。この票で共通スタブに
するのは、いま `IRQ_UNEXP` の 3/5/6/8/9/10/14/15 と、`irq_stub_nic_3/5/6` (LGY-98) の 3/5/6。
それ以外 (0 タイマ / 1 KBD / 2 VSYNC (V86) / 4 シリアル / 7 スプリアス / 11 FDC / 12 サウンド (V86) / 13 マウス) は
固定スタブのままで、`irq_register` は **`-ENOTSUP` (負) で拒否**する。範囲外 (≥16) も拒否。**登録を拒否された装置はこの票では利用不可**
(往復 7 B1 の決定: ポーリング稼働は持たない。probe は逆順に巻き戻して DECLINE。`-ENOTSUP` (固定 IRQ) も `-EBUSY`
(隔離済みの線) も同じ。**BIOS が 82557 に固定 IRQ (11 など) を割り当てる機械では、この票の範囲では LAN として使えない**。
PIRQ → 8259 の経路は BIOS が設定し、Interrupt Line レジスタを書き換えても配線は変わらない。BIOS に変更 UI があるか、
動的 IRQ (3/5/6/9/10/14/15) へ変えられるかは機種ごとの確認事項で、実機の R1〜R4 (TASK_LAN_82557 §5-2) に足す。`lspci` が `[irq N unsupported]` / `[irq N quarantined]` を出す。
**診断の取得口 (往復 8 R8-2)**: 結果は BDF ごとに `struct pci_bind_info { u8 bus, dev, fn, result; u8 irq; u8 reason;
u8 line_state; u8 pad; }` (8 バイト。`line_state` (LINE_OK = 0 / LINE_STORM_MASKED = 1 / LINE_QUARANTINED = 2、両方立っていれば QUARANTINED。pad は 0) は
**getter が読む時点で `irq_line_quarantined` / `irq_storm_masked` から合成する** (irq = 0xFF や ≥ 16 はシフト・配列参照より前に除外して LINE_OK) (往復 9 R9-1: 先に BOUND した装置 X の線が後から別装置 Y で隔離されても X の `result` は BOUND のまま、
`line_state` = QUARANTINED で「IRQ が来なくなった」を `lspci` が出す)。result = NONE / BOUND / DECLINED / QUARANTINED。reason の列挙は下の「上書き規則」の 1 つだけが正) に保存し、カーネル関数 `pci_bind_info_get(idx, *out)`
(idx は `pci_get` と同じ列挙順) で読む。driver は `pci_bind_set_reason(dev, reason)` で理由を書く。上書き規則: reason (列挙: OK / IRQ_UNSUPPORTED / IRQ_QUARANTINED / RESET_FAILED / START_FAILED / NOISY / NOISY_UNMASKABLE /
NO_DRIVER / DECLINED_UNSPECIFIED) は候補ごとに OK に**初期化**してから probe を呼び、理由未設定で DECLINE なら `DECLINED_UNSPECIFIED`、
一致候補ゼロは `NONE / NO_DRIVER`。DECLINED の後に別 driver が BOUND になれば BOUND/OK で上書き、全 driver が DECLINE なら
最後の DECLINED と理由、QUARANTINED は上書きされない。
`lspci` へは**新しい KAPI `pci_bind_info(idx, void *out)` (8 バイト写し) を v60 として末尾追記** (v59 は 1-5 の
`sys_time_now`。スロット順は PM が決める: A の着地後に PM が足す)。既存 `pci_get` の 40 バイトは広げない (旧呼び手のバッファ)。ポーリング稼働が要るなら L-B で
別に設計する)。LGY-98 は `lgy98_init()` で `ne2k_irq` の**アダプタ**を
`irq_register` する形に**この票で移す** (下記)。**LGY-98 だけは登録拒否でも利用不可にしない** (実装時の PM 判断、実装レビュー
往復 1 の非 blocker 1): 既存の `ne2k_timer_tick` が定期回収を持つのでポーリング稼働を続ける。「利用不可」の決定 (往復 7 B1) は
PCI probe 経路 (82557) のもの。

**共有の約束**: 2 つ目以降の登録は、**既存の全登録者と新規の両方が `IRQ_F_SHARED`** のときだけ受ける。1 IRQ あたり
最大 4 登録。ハンドラは自分の装置のステータスを読み、要因があれば**落としてから** `IRQ_HANDLED` を返す
(PC-98 の 8259 は `ICW1_INIT` = 0x11 の**エッジ**設定。要因を落とさないと次のエッジが来ない)。
ハンドラは**有界**でなければならない: 装置待ちのスピンは禁止、処理は自分の予算 (フレーム数など) で打ち切って
`IRQ_DEFERRED` を返す。debug ビルドは `sys_time_now` でハンドラ時間を測り、1ms 超を 1 回だけ報告する。

**ディスパッチ (`irq_dispatch(irq)`、割り込み文脈、IF=0、ネスト無し)**:
1. 表の全登録者を順に呼び、`any |= rc` を**2 巡ぶん累積**する (`handled_any` は 2 巡全体の OR。2 巡目の 0 で
   上書きしない)。最初の `IRQ_HANDLED` で打ち切らない (A と B が同時に要因を持つ場合に B を取りこぼすと
   共有線が上がったまま次のエッジが来ない)。
2. 1 巡目で誰かが受けたら**同じ走査をもう 1 回だけ**行う (一巡の途中で別の装置に要因が積まれた場合の回収)。
3. **処理継続の契約 (往復 4 で設計変更)**: v3/v4 の「IRQ0 が `irq_poll_pending` を tick ごとに回収する」汎用機構は、
   往復 3〜4 の指摘 (片側解除で pending が消える、IRQ0 の予算が PIT count では測れない、ラウンドロビンの飢餓、
   途中打ち切りの再開位置、DEFERRED と無進捗の混同、NE2000 の reset 経路) が**全部この機構に集中した**ので
   **取り下げる**。この層は「呼ぶ・集約する・EOI を 1 回送る」だけにし、残件の回収は**装置ごとの driver が自分の
   tick フックで行う** (既存の型: `ne2k_timer_tick` / `snd_tick` / `link_tick` / `lgy98_tick`。`timer_handler` から
   呼ばれる)。契約:
   - `IRQ_DEFERRED` を返す driver は、**自分で tick フック (`timer_handler` に登録済みの既存の口) を持ち、装置側の
     割り込みをマスクしたまま残件を自分の予算で回収し、回収し終えたら自分でマスクを外す**。この層は
     `irq_deferred_count[irq]` を数えるだけで、回収も再走査もしない。
   - **共有線のエッジ喪失は有限の走査では防げない (往復 5 B1)**: A と B が正常に HANDLED を返し続けても、走査の終了と
     新しい要因の到着が重なると線が一度も下がらず、8259 (エッジ) は次の IRQ を出さない。したがって
     **動的 IRQ に登録する driver は全員 (SHARED か排他かを問わず)、IRQ が来なくても進む定期回収 (自分の tick
     フック) を持つ**ことを登録の契約にする (DEFERRED の有無と無関係。往復 7 B2 の決定: 線の隔離 (noisy) で巻き添えに
     なった登録者も tick だけで動き続けるため、排他登録者にも要求する)。**この契約は `irq_register` が検査しない**
     (フックの登録を API で取っても回収能力の証明にはならない)。この票の driver は静的 (`timer_handler` の直接呼び出し)
     なので、レビューと受入 (W3) で保証する。§3 の動的読み込みでは別に決める。IRQ は加速器、tick が保証 — NE2000 の M4 (`ne2k_timer_tick` の
     受信 watchdog) と同じ原則で、82557 も CS4231 も同じ形を持つ。回収周期は driver が決める (受信は 1 tick、
     送信完了は数 tick など)。この層は `irq_deferred_count[irq]` と `irq_shared_dispatch[irq]` を数えるだけ。
     「要因を落とさず、マスクもしない」driver は契約違反で、W3 の偽装置で「線が上がったまま 2 巡で戻る」ことを
     確認するだけ (救済しない)。
   - 無進捗の検出はこの層では**しない** (戻り値からは「残件の有無」しか分からない。進捗は driver の処理カウンタと
     watchdog の責任。往復 4 R5)。
   - IRQ0 の所要時間はこの票では**増えない** (何も足さない)。既存の tick フックの長時間処理 (NE2000 の
     `wait_rdc` 4ms / `hw_reset` 20ms が `service()` の失敗分岐から `reinit_or_fail → bring_up → hw_reset` で
     ISR 文脈に到達する。往復 4 R4) は**既存の契約違反として残件に記す** (L-C で「再初期化の要求を記録する処理」と
     「foreground で実行する処理」に分ける)。この票の時間保証は「この層自体は 2 巡の走査 + EOI 以外に何も
     しない」だけ。
4. **ストーム**: 1 tick の間に同じ IRQ のディスパッチが `IRQ_STORM_LIMIT` (= 200) 回を超えて `handled_any == 0`
   (誰も受けない) なら、PIC でその IRQ をマスクして `irq_storm_masked |= 1 << irq`、1 回だけ報告する。
   復帰は次の `irq_register` / `irq_unregister` の**登録数の再計算時**に行う (driver が装置を直してから登録し直す)。
   レベルトリガは**扱わない** (現行 PIC はエッジ。「しないこと」に明記)。
5. **終了経路は 1 つ、`irq_finish(irq, handled_any)` (R2)**:
   - `irq == 15` なら、**handled に関係なく**スレーブの ISR (OCW3 = 0x0B) を読み、bit7 が立っていなければスプリアス
     として**マスタにだけ EOI** (`isr_unexpected_irq` の現行と同じ)。立っていれば `pic_eoi(15)`。
     IRQ7 のスプリアスは固定スタブのままなので対象外。読みのあと OCW3 = 0x0A (IRR) に戻す。
   - それ以外は `pic_eoi(irq)` (スレーブ → マスタ)。
   - `handled_any == 0` なら **`isr_unexpected_report(irq)`** を呼ぶ (診断だけ。現行 `isr_unexpected_irq` から
     EOI を抜いた関数。`IRQ_UNEXP` スタブは共通スタブに置き換えるので、EOI を送る経路は `irq_finish` だけになる)。
   - ここが `pic_eoi` を呼ぶ唯一の動的経路 (固定スタブは各自の EOI を保つ)。
6. 共通スタブは `RESTORE_KSEG` / `cld` / `IRETD_USER` を既存スタブと同じに持つ。**IRQ0 の固定スタブにも C 呼び出しの
   前に `cld` を足す** (往復 3 B3: 割り込み入口は DF を自動で消さない。V86 ゲストが `STD` の直後に IRQ0 を受けると、
   tick 回収から呼ばれた callback の `kstrcmp` (`lodsb`) が逆向きに走る)。他の固定スタブ (1/4/11/13) は C 呼び出し
   の前に `cld` があるかを実装時に確かめ、無ければ同じく足す。**V86 中**は動的登録の IRQ を
   ホスト所有として扱い、反射しない。

**LGY-98 のアダプタ (R1)**: `ne2k_irq_shared(irq, arg)`: (a) `nic.busy` なら `irq_pending = 1` を立てて
`IRQ_DEFERRED` (leave() が処理し、それまでの tick 回収は `busy` を見て何もしない)。(b) `ISR & IMR_MASK` が 0 で
`rx_backlog` も 0 なら `IRQ_NONE`。(c) それ以外は現行 `ne2k_irq()` の本体 (service + recheck) を走らせ、戻りに
`rx_backlog` か **IMR に最後に書いた値 (`nic.imr_written`、新設。`imr_mask` は復帰時の設定値なので判定に使わない)**
が 0 なら `IRQ_DEFERRED`、無ければ `IRQ_HANDLED`。`lgy98_init()` の `irq_is_free` (PIC のマスクで空きを見る) は
**廃止**し、`irq_register` の戻りで判定する (共有登録に合わせる)。`ne2k_irq_shared` からも `service()` の失敗分岐で
`hw_reset` に至る (既存)。この票では変えず、残件 (L-C) に記す。
**busy とマスクの境界 (往復 5 B2)**: 現行の `ne2k_enter()` は `busy = 1` → IF 復元 → IMR = 0 の順、`ne2k_leave()` は
IMR 復帰 → `busy = 0` の順で、どちらも**「busy だが装置は未マスク」の窓**があり、そこで IRQ が来るとアダプタは
DEFERRED を返すのに装置側は保持したままになる。移行時に **`irq_save` の中で `busy = 1` と IMR = 0 を一体で**行い、
`leave` も **`irq_save` の中で `irq_pending` の処理 → IMR 復帰 → `busy = 0` を一体で**行う (窓を消す)。busy 中の
callback は NIC レジスタに触らない (foreground のページ切替・remote DMA と衝突する) — DEFERRED を返すだけ。
**IMR を書く場所は `ne2k_imr_sync()` だけ** (往復 6 R1 → 1-6 に一本化): 現行の `program_ring()` (再初期化
`reinit_or_fail → bring_up` の中) は `busy == 1` のまま `IMR = imr_mask` を書き、DEFERRED 契約 (busy = 装置マスク済み) を
破る。移行時に `program_ring()` は IMR を書かず、通常 IRQ 出口・leave の `irq_save` 区間・tick の全最終出口が
`ne2k_imr_sync()` で実 IMR と `imr_written` を同時に書く。W3 に「再初期化直後の窓」への注入を足す。
残件 (W6): leave 全体が IF=0 になるので、`irq_pending` の処理から `service()` → RDC 待ち・再初期化が IF=0 区間に
入る (既存 ISR 経路に加えて、変更後はこの経路も)。L-C で foreground に分ける対象に含める。
W3 で**両方の窓に割り込みを注入** (NP21/W のブレークで enter/leave の途中で止めて `/api/pic` から IRQ を上げる) する。`ne2k_timer_tick` は残す (B1 の定期回収そのもの。
OVW 復旧と送信タイムアウトもそちらが持つ)。

**寿命と排他 (単一 CPU)**: 表の更新は `irq_save` で囲む。`{fn, arg, flags}` は**一括で有効化** (fn を最後に書く)。
ISR の中からの登録・解除・自己解除は禁止 (契約、debug ビルドで検査)。解除の順序は **装置の要因を止める →
`irq_unregister` → 状態を解放**。`irq_unregister` が戻った後に callback が走らないことを保証する (IF=0 で表から
外す。単一 CPU なので走行中の callback は無い)。PIC のマスクは**登録数で持つ**:
最初の登録で `irq_enable`、最後の解除で `irq_disable`。共有者が勝手に `irq_disable` を呼ばない (契約)。
この層は遅延処理を持たない (driver 側の tick フックはこの層の外で、解除の前に driver が自分で止める)。

- **進捗 (2026-09-23、worktree `wt/hal-a`、実装 A)**: 実装済み。`kernel/irq.{c,h}` (表・PIC のマスク・
  EOI) と `kernel/irq_math.{c,h}` (判断だけ、I/O 無し) を新設、`kernel/isr_stub.asm` の `IRQ_UNEXP` を
  共通スタブ `irq_stub_common_%1` (3/5/6/8/9/10/14/15) に置き換え、`IRQ_NIC` (LGY-98 専用) は廃止。
  固定スタブ 0/1/2/4/11/12/13 の C 呼び出し前に `cld` を追加 (往復 3 B3。2 と 12 も `V86_REFLECT` 経由で
  C を呼ぶので足した)。`isr_unexpected_irq` は **EOI を抜いた** `isr_unexpected_report` に分割し、
  IRQ15 のスプリアス検査ごと `irq_finish` へ移した (動的経路で `pic_eoi` を呼ぶのはそこだけ)。
  LGY-98 は `irq_is_free` を廃止して `irq_register(irq, ne2k_irq_shared, NULL, IRQ_F_SHARED)` に移行、
  NE2000 は `nic.imr_written` shadow と **IMR を書く唯一の口** `ne2k_imr_sync()` を入れ、`enter`/`leave` を
  それぞれ 1 つの `irq_save` にまとめ、`program_ring()` は IMR を書かなくした。
  ホスト試験 `make check-irq-math-host` (4 ケース / 変異 8 本すべて RED)、kselftest の `test_irq_dynamic()`
  (`int 0x23` で共通スタブを通し、拒否規則・2 巡・DEFERRED の計数・登録数での PIC マスク・解除後に
  呼ばれないこと・200 と 201 の撃ち分け・隔離の sticky を毎起動で見る)。記録は `tools/tests/irq_math_tdd.md`。
  `make kernel` は警告の増加なしで通る。
  **PM の検証待ち (W3、NP21/W)**: 実 IRQ のエッジ (master 3 / slave 9) を `/api/pic` で IRR・ISR を見ながら、
  共有 2 登録の集約と tick 回収、片方解除、CPL=3 アプリ実行中と V86 中、NE2000 の enter/leave と再初期化
  直後の窓への注入、noisy 隔離、IRQ0 の所要時間 (`/api/prof`)、**LGY-98 をアダプタに移した後の rshell/LAN の通信**。
  **票からの逸脱 1 件**: 登録を断られた LGY-98 は「利用不可」ではなく**ポーリング稼働のまま**にした
  (PM の指示。`ne2k_timer_tick` が既に定期回収を持つので NIC を落とす必要がない)。票 §1-1 の
  「登録を拒否された装置はこの票では利用不可」は PCI probe 経路の決定 (往復 7 B1) として読んだ。
  **未実装**: debug ビルドのハンドラ時間計測 (1ms 超の 1 回報告) と `irq_off_max_us`。W1/W3 の受入項目に
  無く、`sys_time_now` の再帰呼び出しを避ける設計 (1-6) が要るので別に起こす。
- **修正 (2026-09-23、worktree `wt/hal-fix`)**: NP21/W の起動自己診断で「200 本でマスクしない」と
  「200 本でストームビットが立たない」が FAIL した。**原因は試験の側** — `irq_storm_step` は tick ごとに
  数え直すので、直前の解除の試験で撃った「受け手のいない `int 0x23`」が同じ tick に入っていると
  その数だけ下駄を履き、200 本目で閾値を越えていた。`kernel/kselftest.c` に
  `ksel_storm_wait_tick()` (**IF=1 で** tick の変わり目まで待つ。IF=0 では IRQ0 が止まって
  `tick_count` が進まない) を入れ、200 の山と 201 の山の**両方**をその直後から撃つようにした。
  `kernel/irq.c` / `kernel/irq_math.c` は 1 バイトも変えていない (閾値の字義はホスト試験
  `make check-irq-math-host` の `storm` が「200 で 0、201 で 1、tick が変われば窓を作り直す」を
  そのまま持っている)。

### 1-2. 8237 DMA の共通部 — `drivers/dma8237.c` / `dma8237.h`

```c
#define DMA_DIR_TO_MEM   0   /* 装置 → メモリ (8237 の write transfer) */
#define DMA_DIR_FROM_MEM 1   /* メモリ → 装置 (read transfer) */
#define DMA_MODE_SINGLE  0   /* auto-init 無し (FDC)。8237 の転送モードは single transfer、番地は増分 */
#define DMA_MODE_CYCLIC  1   /* auto-init 有り (PCM のリング)。転送モードは同じく single transfer、番地は増分 */
int  dma_chan_setup(unsigned int ch, u32 phys, u32 bytes, int dir, int mode);  /* 0 / 負。**設定後はマスクしたまま返す** */
void dma_chan_unmask(unsigned int ch);
void dma_chan_mask(unsigned int ch);
int  dma_chan_remaining(unsigned int ch, u32 *bytes_left, int *tc_seen);   /* 0 / 負 (安定読み不能) */
void dma_chan_ack_tc(unsigned int ch);          /* 保持している TC を消す */
int  dma_above_1mb_state(void);   /* DMA_A20_VERIFIED / DMA_A20_UNREADABLE / DMA_A20_BLOCKED (診断) */
```

- **検査 (負を返して転送しない)**: `bytes` は 1〜65536 (`bytes-1` を書く。0 は禁止)、`phys + bytes - 1` が `phys` と
  同じ 64KB バンク、`ch` は 0〜3、`dir` / `mode` は上の値以外を拒否、`phys >= 16MB` は**機種によらず拒否**
  (拡張バンクはこの票で扱わない)。demand / block モード・番地減分は口を持たない。
- **0439h (診断であって、転送の可否に使わない)**: `dma8237_init()` (fdc_init より前、`sysclk_detect` の次) で
  書く前の値 `pre` と RMW 後の読み戻し `post` を保存し、`post & 0x04` が 0 → `VERIFIED`、`post == 0xFF` →
  `UNREADABLE`、それ以外 → `BLOCKED`。**プールは構造上 1MB 超 (0x2E8000) にあり、FDC の静的バッファもカーネル
  (0x100000〜) にある**ので、BLOCKED の機種では OS32 の FDC 自体が動かない (W2 の実機 FD 起動がそれを検出する)。
  よって `dma_chan_setup` は 1MB 超を状態で拒否しない (往復 1 の B7 から変更。理由は上)。3 値は起動行と
  `pcidump` 相当の診断に出す。`VERIFIED` は「ビットが落ちた」であって動作実証ではない (実証は W2)。
- **再設定の責任**: 呼び手は再設定の前に `dma_chan_mask` を呼ぶ (設定関数はマスク中を前提。マスクされて
  いなければ負)。設定は `irq_save` で囲む — フリップフロップ (`0019h`) はチャネル間で共有され、途中で別チャネルの
  ISR が触ると上位 / 下位が取り違わる。
- **TC の保持 (R7、往復 3 B4/B5)**: 8237 のステータス (`0011h`) は**読むと全チャネルの TC が消える**。共通部だけが
  これを読み、読んだ値の TC ビットを**2 つの状態**に写す: `done[ch]` (終了済み。**次の `dma_chan_setup` まで
  保持**、消費されない) と `tc_event[ch]` (通知。`dma_chan_ack_tc` で消す。CYCLIC では周回ごとに立つ)。
  `dma_chan_setup` は自分のチャネルの両方を消す (その前にステータスを 1 回読んで他チャネルの分を保存する)。
- **`dma_chan_remaining` の手順 (R7、往復 3 B4/B5)**: `irq_save` の中で
  1. ステータス読み → `done` / `tc_event` 更新。**SINGLE で `done` なら `bytes_left = 0` で即返す** (count の
     採用判定より**先**。TC 後の `FFFFh` は設定長超で不採用になるので、count は見ない)。
  2. FF クリア → 下位 → 上位 でカウント `c1`。
  3. **ステータス再読み** → 更新。SINGLE で `done` なら `bytes_left = 0` で返す (`c1` の読みの直後に終わった場合。
     `irq_save` は DMA を止めないので、count とステータスは**一体**として扱う)。
  4. 同じ手順で `c2`、そのあと**もう 1 度ステータス** → SINGLE で `done` なら `bytes_left = 0` で返す。
  5. **採用条件**: `c1 == c2`、または `c1 > c2` かつ `c1 - c2 <= 64` (2 回の読みの間に進み得る転送量の上限)。
     採用は**後の値 `c2`**。どちらかが設定長 (`bytes - 1`) を超えたら合成が壊れている (再ロードや桁借りを
     またいだ) として不採用。不採用なら 2〜4 をやり直し、**3 組**で揃わなければ `-EAGAIN`。
  6. 返す `bytes_left` = `c2 + 1`。
  **返却値の規則 (往復 4 R6、早期 return を含む全成功経路で同じ)**: `bytes_left` は `done` から (done なら 0)、
  `*tc_seen` は **`tc_event` から** (ack 済みなら 0)。遷移表: setup 成功 → (done 0, tc_event 0) → 「未完了量, 0」/
  TC 採取 → (1, 1) → 「0, 1」/ ack → (1, 0) / 再度 remaining、新しい TC 無し → 「**0, 0**」/ 次の setup → (0, 0)。
  同じ完了を 2 度通知しない。
  CYCLIC では `bytes_left` は**現在の周回の中の位置**でしかない: `tc_event` は「前回の ack 以降に 1 周以上完了」
  しか意味せず、周回数・停止・アンダーランは装置側の割り込み (CS4231 の half/full) で判定する (契約に明記。
  remaining を停止の証拠に使わない)。`done` は CYCLIC では立てない (auto-init は終わらない)。
- **ポート表 (PC-98、奇数番地。バンクは等差ではない)**: ch0 addr `0x01` / count `0x03` / bank `0x27`、ch1 `0x05` / `0x07` / `0x21`、
  ch2 `0x09` / `0x0B` / `0x23`、ch3 `0x0D` / `0x0F` / `0x25`。status `0x11` (読むと全 TC が消える)、mask `0x15`、mode `0x17`、
  FF クリア `0x19`。
- 純粋関数: `dma_split_addr(phys) → {addr16, bank8}`、`dma_crosses_64k(phys, bytes)`、`dma_count_to_bytes`、
  安定読みの採用判定 `dma_accept_pair(c1, c2, limit)`、ポート表の引き (上の表と一致することを試験)。ホスト試験。
- FDC: `dma_setup()` は **`dma_chan_mask(2)` → `dma_chan_setup(2, …, SINGLE)` → 成功したときだけ `dma_chan_unmask(2)`**
  の 3 段 (setup だけに置き換えるとマスクしたままコマンドを出してタイムアウトする)。**負が返ったら FDC コマンドを
  発行せず `fdc_report_fail` の phase に "dma" を足して出す**。`fdc_abort_transfer` と `fdc_recover` (fdc.c 480 行) の
  `DMA_MASK_CH2` 直書きも `dma_chan_mask(2)` に。

- **進捗 (2026-09-23、worktree `wt/hal-b`)**: 実装済み。`drivers/dma8237.{c,h}` と純粋部
  `drivers/dma8237_math.c` を新設し、`drivers/fdc.c` の `dma_setup()` は
  `dma_chan_mask(2) → dma_chan_setup(2,…) → dma_chan_unmask(2)` の 3 段に置き換え、負なら
  **FDC コマンドを出さず** `phase="dma"` で `fdc_report_fail` に出す。`fdc_abort_transfer` /
  `fdc_recover` の `DMA_MASK_CH2` 直書きも `dma_chan_mask(2)` に。**0439h の RMW と 3 値の診断は
  `dma8237_init()` へ移した** (`kernel.c` の `sysclk_detect` の次、`fdc_init` より前)。起動行は
  `[dma] 0439h xx -> yy state=VERIFIED|UNREADABLE|BLOCKED`、最下行の `FDC rc=… 0439h=xx->yy` は
  `dma_above_1mb_raw()` の写しで書式が変わっていない。ポート番号は `drivers/dma8237.h` に 1 か所
  ([C4]、`fdc.h` の `DMA_CH2_*` は撤去して `FDC_DMA_CHANNEL 2` だけ残した)。典拠は
  UNDOCUMENTED Vol.2「DMAコントローラ」(`/mnt/c/WATCOM/docs/undocumented/io_dma.md` 140〜380 行) を
  実読して照合 — **ch0 のバンクだけ 0027h で並びが飛ぶ**ことを表とホスト試験で固定した。
  エラーは独自の `DMA_ERR_*` を作らず `OS32_ERR_INVAL` / `OS32_ERR_AGAIN` / `OS32_ERR_NOSYS` の別名。
  ホスト試験 `make check-dma8237-host` (7 ケース / 変異 8 本すべて RED)、kselftest の `test_dma8237()`
  (悪い引数でハードウェアに触らないこと・マスク無しの setup を断ること・setup 後の done/tc_event)。
  **PM が NP21/W で見るもの**: W2 (FDC が dma8237 経由でも 2HD 起動と md5 一致、書き込み→読み戻し、
  `dma_chan_setup` が負のときコマンドを出さない、タイムアウト→`fdc_abort_transfer`→再試行) と
  実機の FD 起動 + 0439h の 3 値。`dma_chan_remaining` / `dma_chan_ack_tc` は**まだ呼び手が無い**
  (CS4231 は §5-5 の別票) ので、遷移表の実機確認は L-B / PCM の票に残る。

### 1-3. DMA プール — `kernel/dma_pool.c` (固定番地、SQLite 帯の予約域)

**理由 (往復 1 の B8/B9、往復 2 の R4/R5)**: 割り込みは**そのときの CR3 (アプリの PD)** で走る。全 PD で共有される
のは PDE 0 (0〜4MB) 全体 (`kernel/paging.h` 151 行)。v2 の「SHM 帯から切る」は (a) SHM の基点が `__bss_end` 由来の
4KB 整列で 64KB 整列にならない、(b) `MEM_SHM_SIZE` を減らすと `MEM_SHM_GUI_OFFSET` が動いて SDK の `GUI_SHM_OFFSET`
(C / Rust) と GUI アプリ全部の再ビルドが要る、の 2 点で取り下げる。

- **置き場**: SQLite 帯の「カーネル予約」(現在 0x2DD000〜0x2FAFFF、120KB、NOT PRESENT) の中の**固定番地**
  `MEM_DMA_POOL_BASE` = **0x2E8000**、`MEM_DMA_POOL_SIZE` = 0x10000 (0x2E8000〜0x2F7FFF)。純粋な定数式なので
  `STATIC_ASSERT(MEM_DMA_POOL_BASE + MEM_DMA_POOL_SIZE <= MEM_STACK_GUARD)` で固定できる。上下は予約域のまま
  NP (下: SQLite 代替スタックの後、上: 0x2F8000〜0x2FAFFF の 12KB) がガードになる。
  SQLite の成長は `build/os32.ld` で止める。リンカは C ヘッダを読まないので、`MEM_SQLITE_STACK_SIZE = 0x20000;` と
  `MEM_DMA_POOL_BASE = 0x2E8000;` を**絶対シンボルとして ld に持ち** (`MEM_KSTACK_TOP` と同じ作法、
  `tools/gen_memmap.py --check` が C 側と照合)、
  `ASSERT(__sqlite_end + MEM_SQLITE_STACK_SIZE + 0x1000 <= MEM_DMA_POOL_BASE, "SQLite band overruns DMA pool")`
  を書く (いまの余裕 44KB。SQLite は amalgamation で固定に近い)。
  `MEM_KERNEL_IMAGE_MAX` (カーネル帯の予算) は**触らない** (プールはカーネル帯の外)。SHM・GUI・SDK・アプリは無変更。
- **写像**: `paging_init` が `paging_set_not_present` の範囲からプールを外し、**present / supervisor / RW / キャッシュ
  有効**で張る。アプリ PD は master の PDE 0 を写すので同じ PT を見る。**USER ビットが立たないこと**を W5 で確かめる。
  現行の MM 検査 (`kernel/paging.c` 1149〜、`memmap_seen_at`) は RW を見た時点で `MM_RW` を返し **USER 混入を
  見分けない** (往復 3 B6) ので、`MM_RWU` (present + RW + USER) を足して `MM_RW` と区別し、期待値側 (`memmap_expect_at`)
  は**予約域を NP とする分岐より先に**プールを `MM_RW` と判定する (関数名は実コードでは `memmap_want_at`。この検査は
  **アプリ起動前専用**で、USER 変異は検査後に復元する)。変異試験: プールの PTE に USER を立てると W5 が落ちる。
- **64KB 境界**: プールは 0x2F0000 を跨ぐので、`dma_pool_alloc` は候補ごとに `dma_crosses_64k` を検査して跨ぐ候補を
  飛ばす。**受付範囲は 1〜32KB** (`bytes` が 32KB を超えたら即 NULL。境界の両側がそれぞれ 32KB なので 33〜64KB は空でも
  必ず失敗する)。≤ 32KB が必ず入るのは**空のプール**の話で、断片化後は失敗し得る (呼び手は NULL を扱う)。
- **割り当て (R6)**: 4KB 単位 16 ページのビットマップ + **span 表** `struct { u8 first, npages, state; }` × 16
  (state = FREE / USED / LEAKED)。`dma_pool_alloc(bytes, align, *phys_out)`: `bytes` は 1〜32KB、`align` は 2 の冪で
  ≤ 32KB、0 は 4KB。成功で virt (= phys、恒等) と span の登録、失敗は NULL + `*phys_out = 0`。
  `dma_pool_free(virt)`: **span の先頭と一致するときだけ**解放。途中ポインタ・二重解放・範囲外は負
  (`-EINVAL`) を返して `dma_pool_bad_free` を数える。`dma_pool_mark_leaked(virt)`: span を LEAKED にして
  `dma_pool_leaked` を数える (停止を確認できない失敗経路で driver が明示的に呼ぶ。LEAKED は再利用しない)。
  alloc / free / mark はいずれも `irq_save` の短い排他の中で表だけを触る (探索 + 表更新、動的確保なし) ので
  割り込み文脈から呼べる。`dma_pool_init` 前は全部負 / NULL。
- **解放の契約 (R6/R9、往復 3 B7)**: `dma_pool_free` は**装置がそのメモリへの DMA を止めた証拠を呼び手が持ってから**
  呼ぶ。証拠の定義は装置ごと: 82557 では **CUS/RUS の Idle だけは証拠にしない** (SDM §6.3.2.1: CU Start の直後は
  しばらく Idle が見える。SCB command byte = 0 も「受理」であって完了ではない)。**開始命令 (CU Start / RU Start) を
  1 度でも発行した後は、PORT selective reset (PORT = 0010、CU/RU を止め設定を保つ) を必須**とし、10µs 待って
  CUS = Idle かつ RUS = Idle を読む。これが証拠。reset 後も Idle にならなければもう 1 回 reset、それでも駄目なら
  **隔離** (下記)。開始命令を出す前 (probe の (5) まで) は DMA は起きていないので証拠は要らない。PCM は
  `dma_chan_mask(ch)` + CS4231 の再生許可ビットを落とした後の**装置側**の確認 (remaining の一致は証拠にしない)。
- 用途と大きさ: 82557 の CB (数 KB) + RFD 8 本 × 1.5KB ≒ 16KB、PCM リング 16KB + ステージング 16KB (TASK_PCM_CS4231、暫定)、余裕 16KB。
  FDC の 1KB は**静的配列のまま** (起動最初期に要る)。
- 純粋関数: 最初適合 + 整列 + 64KB 跨ぎの判定、span の登録 / 解放 / 不正解放をホスト試験に。

- **進捗 (2026-09-23、worktree `wt/hal-b`)**: 実装済み。`include/memmap.h` に
  `MEM_DMA_POOL_BASE` = 0x2E8000 / `MEM_DMA_POOL_SIZE` = 0x10000 / `MEM_DMA_POOL_END` を純粋な定数式で
  追加し、`build/os32.ld` に `MEM_SQLITE_STACK_SIZE` / `MEM_DMA_POOL_BASE` の絶対シンボルと
  `ASSERT(__sqlite_end + MEM_SQLITE_STACK_SIZE + 0x1000 <= MEM_DMA_POOL_BASE, …)` を置いた
  (いまの余裕は `__sqlite_end` = 0x2BC060 に対して **44KB**)。`tools/gen_memmap.py` は 2 つの写しを
  `MIRRORS` に足し、帯の表を「カーネル予約 (下) 44KB NP / **DMA プール 64KB RW** / カーネル予約 (上)
  12KB NP」に割った (`docs/02_memory.md` 再生成済み、`--check` 通過)。`kernel/paging.c` は
  `paging_set_not_present(予約域)` の**後**に `paging_map_range(…, PAGE_RW)` で張り直す (NP 化の範囲から
  外すだけにしない)。MM 検査に `MM_RWU` (present+RW+**USER**) を足して `MM_RW` と分け、期待値側は
  予約域を NP とする分岐より**先に**プールを `MM_RW` と判定する。`kernel/dma_pool.{c,h}` と純粋部
  `kernel/dma_pool_math.c` (最初適合 + 整列 + 候補ごとの 64KB またぎ、16 ページのビットマップ +
  16 本の span 表、FREE/USED/LEAKED)。受付は 1〜32KB で、**33KB 以上は `ERR_ARG`** (断片化の
  `ERR_NOSPC` と分ける)。ホスト試験 `make check-dma-pool-host` (7 ケース / 変異 9 本すべて RED)。
  kselftest は `test_dma_pool()` (16KB×3 → 真ん中を free → 8KB が入る / 途中ポインタ / mark_leaked /
  枯渇 / `phys_out == virt`。**最後に `dma_pool_init()` で池を作り直す**ので `pci_bind_all` は
  `kselftest_run` の後に置いた) と `test_memmap_pool_user()` (プールが `MM_RW`、PTE 1 本に USER を
  立てると検査が落ちる、戻すと通る)。USER の変異は `paging_poke_user_bit()` を新設して PTE だけを
  触る — `paging_set_page` は USER を **PDE にも伝播させる**ので、戻しても PDE に残ってしまう。
  **PM が見るもの**: W5 の `docs/02_memory.md` (再生成済み) と kselftest の結果、カーネル増分。

### 1-4. PCI デバイスの結線表 — `drivers/pci_bind.c`

```c
#define PCI_PROBE_OK          0
#define PCI_PROBE_DECLINE    -1   /* 装置に触っていない、または完全に戻した → 次の候補へ */
#define PCI_PROBE_QUARANTINE -2   /* 装置を未知の状態に残した → その BDF は候補探索を打ち切り、隔離 */
struct pci_driver {
    u32 size;                                 /* sizeof(struct pci_driver)。将来の外部モジュールが版を名乗る口 */
    u16 vendor, device;                       /* 0xFFFF = 任意 */
    u8  class, subclass;                      /* 0xFF = 任意 */
    const char *name;
    int (*probe)(const struct pci_dev *dev);
};
int pci_bind_all(const struct pci_driver *const *table, int n);   /* 1 件ずつのポインタ配列 */
```

- **一致規則**: 4 欄の AND。`0xFFFF` / `0xFF` は任意。表順に最初に一致した driver の `probe` を呼び、**DECLINE なら
  次の候補へ**、**QUARANTINE ならその BDF の探索を打ち切って `pci_quarantined` に記録** (`lspci` が `[quarantined]`
  を出す)。同じ driver が複数の装置 (別 BDF) に一致してよい — driver は 2 台目を DECLINE するか自分で複数を持つ。
  `probe` に渡す `struct pci_dev` は列挙表の**写し**で、probe から戻った後は無効。
- **BAR の検査は driver の責任** (binder は BAR で除外しない。I/O BAR の生値 `1` は「あるが未割り当て」)。
- **probe の段階 (82557 の例) と失敗時の戻し**: (0) driver の状態を `STARTING` にする (**この状態の間、IRQ callback と
  tick フックは装置に触らず `IRQ_NONE` / 何もしない**。往復 8 R8-1: 登録した瞬間から共有線の別装置の IRQ で callback が
  呼ばれる) → (1) Command の I/O Space Enable → (2) PORT selective reset + 10µs → **(2') SCB command の M ビットを立てる**
  (装置側マスク。M と CU/RU command は別バイト、SDM §6.3.2.2) → (3) `dma_pool_alloc` で CB/RFD、descriptor と SCB の初期化 →
  (4) `irq_register` → (5) Command の Bus Master Enable → (6) CU/RU 開始 → (7) **`irq_save` の短い区間で状態を `RUNNING` にし、
  M ビットを落とす** (ここから callback / tick が装置を扱う。M を落とした瞬間に pending が積まれていれば最初の IRQ で
  callback が通常どおり処理する)。(7) より前に開始命令を出せるのは (6) だけで、(6) は STARTING のままなので callback / tick は
  交錯しない。**(5) までの失敗は逆順に戻して DECLINE** (装置は触っていないか reset 直後。M は立てたまま巻き戻す) — ただし **(2) の reset が Idle を
  確立できなかった場合は例外で QUARANTINE** (状態不明の装置を次の driver に渡さない)。
  **(6) 以降の失敗**は selective reset を打って停止の証拠 (1-3) を取り、取れれば逆順に戻して DECLINE、取れなければ
  **QUARANTINE**。
  DECLINE の巻き戻しの列 (往復 5 B3): **まず `irq_save` の中で driver の状態を `STOPPING` にする** (IRQ callback と
  tick フックはこの状態を見て**装置に触らず** `IRQ_NONE` / 何もしない — reset 後の 10µs は SCB へのアクセス禁止
  (SDM §6.3.3.3) なので、共有線の別装置の IRQ で callback が呼ばれても SCB を読まないため) → selective reset →
  10µs → CUS/RUS Idle 確認 → Bus Master Enable を落とす → `irq_unregister` → `dma_pool_free` → I/O Space Enable を
  元に戻す。「tick は unregister の前に止める」では遅い。QUARANTINE では: 状態を `QUARANTINED` に → **SCB command の M ビット (割り込みマスク、SDM §8.4) を立てて
  装置の INTA# を止める** → Bus Master Enable を落とす (それ自体は書ける) → span を `dma_pool_mark_leaked` →
  `irq_register` した callback は**解除せず「排出モード」に切り替える** (M ビットを立てた後にだけ SCB status を
  読んで ack する。処理はしない。callback が参照する状態と **I/O Space Enable は保つ**)。
  **登録前の QUARANTINE (probe (2) の reset 失敗、往復 5 B4 / 往復 6 R2・R3)**: callback が無いので、SCB command
  word の **M ビット (bit 8、DWORD では bit 24) を立てて INTA# のアサートを止める**。M は status の pending を消す
  操作ではない (SDM Table 14 / §8.4) ので、**隔離の成立条件は M の読み戻しだけ**。ラッチ済みの status (FR/CNA など)
  は STAT/ACK に 1 回だけ W1C を書いて消し、消えなくても**そのまま終了**する (pending の消失を待って Bus Master
  無効化を保留する経路は持たない)。順序: M 確認 → STAT/ACK 1 回 → Bus Master 落とす → I/O Space 落とす → 記録。
  **M の読み戻しが変わらない装置 (noisy)**: 追加の IRQ に依存しない処置を取る — ストーム検出は「線が上がったまま
  でエッジが来ない」「同じ線の正常装置が HANDLED を返す」のどちらでも発火しないので当てにしない。
  `irq_quarantine_line(irq)` で **PIC のその線を明示的にマスクし、`irq_line_quarantined |= 1 << irq` を立てる**。
  この状態は登録数の再計算で解除されず (再起動まで)、以後その線への `irq_register` は `-EBUSY`、既に登録済みの
  登録者 (共有・排他とも) は定期回収 (tick フック) だけで進む (全登録者に要求している契約なので動き続ける。
  IRQ が来なくなったことは `irq_line_quarantined` で読め、`lspci` と起動後の報告に出る)。線が固定 IRQ (0/1/2/4/7/11/12/13) に
  割り当てられていた場合は PIC をマスクできない (カーネル装置が死ぬ) ので、`pci_quarantined_noisy_unmaskable` に
  記録して報告し、線は固定スタブのスプリアス処理に任せる (未解決の制限として明記)。`lspci` は
  `[quarantined noisy irq N masked]` / `[... unmaskable]` を出す。W1/W3 に「線の保持でエッジが来ない」「共有者が
  HANDLED」の両反例を足す。
- Command の更新は**下位 16 ビットだけ**書く (`pci_cfg_write16` を足す。上位の Status は W1C)。
- 表は静的 (`kernel/kernel.c`、最初は 82557 の 1 行)。`pci_bind_all` は **pgalloc と DMA プールの初期化の後**。
  §3 の動的読み込みが来たら `size` で版を確かめる。

- **進捗 (2026-09-23、worktree `wt/hal-b`)**: 実装済み。`drivers/pci_bind.{c,h}` と純粋部
  `drivers/pci_bind_match.c` (一致規則・候補探索・1 台ぶんの遷移)。`probe` は関数ポインタなので
  遷移はホストで全部踏める。`drivers/pci.c` に `pci_cfg_write16` を追加 — CF8 の DWORD 書きと
  **`CFC + (reg & 2)` の WORD 書き**を同じ `irq_save` の中に置き、奇数オフセットは
  `OS32_ERR_INVAL`、PCI 不在は `OS32_ERR_NOSYS`、32 ビットの RMW は使わない (上位の Status は W1C)。
  BDF ごとの記録 `struct pci_bind_info` (8 バイト: bus/dev/fn/result/irq/reason/line_state/pad) を
  カーネル配列 `g_bind[PCI_MAX_DEVS]` (32 台 = **256 バイト**) に保存し、`pci_bind_info_get(idx, out)`
  で読む。**`line_state` は保存せず読む時点で合成する** — `pci_bind_line_state_hook`
  (既定 NULL = `PCI_LINE_OK`) に実装 A の `irq_line_quarantined` / `irq_storm_masked` を写す 3 行の
  アダプタを PM が合流時に差す。hook は `PCI_LINE_BIT_STORM` / `PCI_LINE_BIT_QUARANTINED` の
  ビットを返し、**両方立てば QUARANTINED が勝つ**規則は `pci_bind_info_compose` が持つ
  (irq が 0xFF か 16 以上ならシフト・配列参照より前に `LINE_OK` で戻る)。理由は候補ごとに `OK` へ
  初期化してから probe を呼び、書かずに DECLINE したら `PCI_BIND_DECLINED_UNSPECIFIED`。
  結線表 `pci_drivers[]` は `kernel/kernel.c` にあり **いまは空** (82557 は L-B)。
  `pci_bind_all` は `pgalloc` と `dma_pool_init` の後 (実際には `kselftest_run` の後)。
  ホスト試験 `make check-pci-bind-host` (**8 ケース** / 変異 10 本すべて RED)。
  **KAPI は足していない** — `lspci` の注記に要る v60 は PM が末尾追記する契約で、いまは
  カーネルの起動行 `[pci] bb:dd.f vvvv:dddd bound (ok) irq=xx` だけが外から読める。
  `pci_bind_info_get` は **8 バイトちょうど**を書く (v60 の出力保護がこの大きさを通す)。
  **PM が実機で見るもの**: 表が空でないときの結線 (L-B)、隔離の 2 経路 (W1/W3 の反例)。

**進捗 (2026-09-23、worktree `wt/hal-c`)**: 診断の取得口を **KAPI v60 `pci_bind_info(u32 idx, void *out)`**
として末尾追記した (slot 223 = 0x384、`sdk/kapi.json` から再生成。[ABI2] の追記のみ)。wrapper は
`kapi/kapi_sys.c` の `kapi_pci_bind_info` で、実装 A の出力ポインタ保護を `sys_time_now` と同じ形で使う
— NULL と 8 バイトの帯境界跨ぎは `OS32_ERR_INVAL` (**1 バイトも書かない**)、`ring3_user_ranges_writable`
が落ちたら `ring3_fault_kill`、写しは**ローカルの 1 スナップショットから 8 バイトちょうど**。
`idx` が範囲外も `OS32_ERR_INVAL`。`userland/shell/cmd_pci.c` の `lspci` が 1 行の末尾に
`bound (ok) irq=N` / `declined (<reason>)` / `quarantined (<reason>)` と、線の様子の
`[irq N quarantined]` / `[irq N storm-masked]` を足す (reason は短い小文字の語の静的表。
`result` = NONE かつ `line_state` = OK のときは何も出さない)。`build/app.conf` は
`userland/shell` / `userland/tests/kstr_bench` / `userland/tests/time_test` を **60** へ。
ホスト試験は `check-pci-bind-host` に `info_get` を足して **8 ケース** (8 バイトちょうど / 範囲外と NULL は
出力を書かない / 取得口を通しても `line_state` は合成 / 列挙 0 件)。変異 10 本は全て RED のまま。
**未実施**: NP21/W と実機での実行 ([V1])、`make check` 全体、`make external` (PM)。

### 1-5. µs 時計 — `sys_time_now(u32 *lo, u32 *hi)`

- **時間源**: `tick_count` (1-0 の後は 10ms) + PIT ch0 のラッチ読み (mode 2、再ロード後は `reload` から 1 まで減る)。
  `us = tick × 10000 + ((reload − count) × 10000) / reload`。乗算は u64 (GNU89 の `unsigned long long`、カーネル内のみ、
  libgcc は既にリンク済み) で行う。
- **スナップショット手順 (R8)**、全体を `irq_save` (IF=0) の中で:
  1. `p1` = PIC1 の IRR bit0 (OCW3 = 0x0A を書いてから読む。既定選択も IRR だが明示する。`irq_finish` の ISR 読みは
     IF=0 の外では走らないので競合しない)
  2. ラッチ → `count` (下位 → 上位)
  3. `p2` = IRR bit0 を再読
  4. `t` = `tick_count` (IF=0 の中なので手順の間は不変)
  5. 判定: `p1 == 0 && p2 == 0` → `t`、`count` を採用。`p1 == 1` → 再ロードはラッチより前 → `t + 1`、`count` を採用
     (count は新周期)。`p1 == 0 && p2 == 1` → 再ロードがラッチの前後どちらか不明 → **やり直し** (最大 3 回。
     1 回は数 µs なので同じ周期の中でもう 1 度境界を踏むことは無い)。3 回失敗で `-EAGAIN`。
  これで往復 2 の反例 1 (古い count に +1) は起きない。
- **契約**: この時計が正しいのは **IRQ0 が失われない範囲**、すなわち**システム全体で IF=0 の区間が 1 周期 (10ms) 未満**
  のとき (`tick_count` 自体が同じ前提)。2 回以上の境界を IF=0 で跨いだ (反例 2) 場合は tick が 1 つ失われ、この時計も
  `tick_count` も 10ms 遅れる — 検出はしない (契約に明記、W4 で「IF=0 10.2ms」の挙動を記録)。debug ビルドは
  `irq_save` の最長区間を測る (`irq_off_max_us`)。
- **返却**: 0 = 成功、`-EAGAIN` = 3 回失敗、`-ENODEV` = `pit_setup` 未初期化または mode 2 でない。負のときは
  `*lo`/`*hi` を触らない。呼び手は `tick_count × 10000` に落とせるが、**その値は同じ tick の補間値より小さいので、
  fallback は単調性の保証に含めない** (呼び手が前回値と max を取る)。
- **KAPI**: 64 ビットの戻り値は使えない (往復 1 の B14) ので**出力引数 2 本**で末尾追記 (版数 58 → 59)。
  CPL=3 のポインタ検査 (往復 9 R9-2 で契約を限定、往復 10 R10-1 で出力保護を追加): **既存の kill 規則を採る** —
  生成される早期検査 (`kapi_argptr` / `ring3_ptr_ok`。`ring3_user_range_ok` は偽を返すだけの関数) が帯の外の番地で
  `ring3_fault_kill()` するのは他の KAPI と同じ。**ただし読み取り専用ページは fault しない**: OS32 は `CR0.WP = 0`
  (`arch/x86/arch_cpu.h` の MMU 有効化、`kernel/shlib.c` がカーネルからの書き込みに依存) なので、CPL=0 の wrapper は
  共有ライブラリの `.text` (RO、全アプリで共有) にも書けてしまう。**出力引数を持つ新設 KAPI (`sys_time_now`、
  `pci_bind_info`) は、書く前に `ring3_user_range_writable(ptr, len)` (新設、`exec/exec.c`) を通し、通らなければ
  `ring3_fault_kill()`**。**検査の手順 (往復 11 R11-1: どの写像で表を読むかを固定)**: 検査対象は KAPI 入口で保存した
  **アプリの CR3 の PD**。アプリ CR3 のまま PD/PT の物理番地を辿ると、アプリ帯が per-app 写像に置き換わっているので
  表ではなく別データを読む (`exec/exec.c` 517 行付近の既存記録)。`paging_pte_flags()` は master の PT を読むので、
  アプリでは RW+USER の shlib `.data/.bss` が master では USER 無しになり正常な出力を誤拒否する。したがって
  helper は **短い IF=0 区間で master CR3 に切り替え** (master では低位物理が恒等写像なので PD/PT を物理番地で読める)、
  保存したアプリ PD の PDE (present、PS 付きは拒否 — 現行に 4MB PDE の生成経路は無い) → PT の PTE (present + RW + USER、
  範囲の全ページ) を確かめ、**元の CR3 と IF を復元してから**判定を返す。master 滞在中はユーザー出力へ書かない。
  2 本とも検査が通ってから、元のアプリ写像で書く。検査と書き込みの間に GUI ポンプ・yield・park を挟まない。
  検査は KAPI wrapper に置く (カーネル内部からの時計呼び出しはローカル変数を渡すので USER を要求しない)。
  検査順: NULL・帯境界跨ぎ・交差の負返却 → 全出力の writable 検査 → 書き込み。
  (出力バッファにコード番地を渡すのはアプリのバグ。負を返して続行させない)。既存の出力 KAPI への適用は別票 (残件)。
  全体の WP 有効化は shlib の書き込み依存と対象 CPU の確認が要るので、この票では選ばない。
  wrapper が**負を返して出力不変**を保証するのは、早期検査を通った後に wrapper 自身が判定できる範囲だけ:
  `lo == NULL` / `hi == NULL`、各 4 バイトが帯境界を跨ぐ、**2 本の 4 バイト範囲が交差する** (`lo == hi` だけでなく差 1〜3 も)。
  この範囲内では 2 本とも検査してから、ローカルの 1 つのスナップショットを書く。受入 W4 は wrapper の直呼びではなく
  **CPL=3 のゲストから KAPI を呼ぶ試験** (`kselftest_run_post_exec` はカーネル関数なので、そこから CPL=3 の試験バイナリを
  起動する) で、負が返る 3 種と kill される 2 種 (帯の外、**共有ライブラリ `.text` の番地を出力に指定** → kill され、共有内容が
  不変 (2 本目だけ RO の場合も))、**正常系の対照** (非恒等写像のアプリ heap / stack と shlib `.data/.bss` への出力は成功)、
  不在 PTE、ページ跨ぎ、を確かめる。`pci_bind_info` (v60) も同じ試験。
- 単調性: u64 に組み立てるので 71 分の桁あふれは無い。`tick_count` の u32 周回 (497 日) は扱わない (契約)。
- **単調性のクランプ (2026-09-23 追記、NP21/W の実測で判明)**: 上の p1/p2 の挟み込みは
  **8254 の再ロードと 8259 の IRR bit0 が原子的に動くこと**を前提にしている。
  **NP21/W は 8254 の再ロードと 8259 の IRR を原子的に模擬しない** (`count` は経過サイクルから
  計算され、IRQ0 は別立てのタイマ事象で上がるので、**count が先/IRR が先の両方が出る**)。
  そのため (a) count は新周期・IRR はまだ → `p1=p2=0` で `t × 10000 + 小さい端数` = 直前の
  `t × 10000 + 9999` より小さい、(b) IRR が先で `p1=1` → `(t+2) × 10000` 相当まで跳び、ISR の後の
  読みで戻る、の 2 つが判定をすり抜ける (実機も数百 ns の窓で同じ形)。
  **単調性は `s_last_us` へのクランプで保証し、回数を `ktime_clamp_count` に数える**
  (`kernel/time_math.c` の `time_clamp` が純粋な判定、書き手は `kernel/ktime.c` の
  `sys_time_now` だけで、スナップショットと同じ `irq_save` の中。`us == last` は数えない。
  試験の注入中 (`time_test_feed_n > 0`) はクランプを通さない — 境界の count を撃ち分ける試験が
  わざと小さい値を作るため)。**実機での回数は W4 の記録項目**。

- **進捗 (2026-09-23、worktree `wt/hal-a`、実装 A)**: 実装済み。`kernel/ktime.c` (スナップショット) /
  `kernel/time_math.{c,h}` (判定と算数、I/O 無し) / `kernel/ktime.h` (試験専用の注入口) を新設、
  KAPI を **v58 → v59** に上げて `sys_time_now` を末尾追記 (slot 222 = 0x380)。CPL=3 の検証は
  `kapi/kapi_sys.c` の `kapi_sys_time_now`。`-EAGAIN` / `-ENODEV` は既存の ABI 空間に合わせて
  `OS32_ERR_AGAIN` (-14) / `OS32_ERR_NOSYS` (-10) を使った (新しい番号は増やしていない)。
  ホスト試験 `make check-time-math-host` (4 ケース / 変異 6 本すべて RED)、kselftest の `test_time_now()`
  (1 万回連続読みで逆行 0、`cpu_delay_us(1000)` を挟んだ実測、注入で p1/p2 の 3 分岐と 3 回失敗、
  境界の count、実 PIT の位相待ちは**踏めた分岐を記録して未検証を報告**、1 回あたりの µs を表示)。
  記録は `tools/tests/time_math_tdd.md`。
  **レビュー往復 10/11/12 の反映**: OS32 は CR0.WP = 0 なので、CPL=0 のラッパは読み取り専用の USER
  ページ (共有ライブラリの `.text`) にも #PF なしで書けてしまう。`exec/exec.c` に
  `ring3_user_ranges_writable()` を新設し、**master CR3 へ切り替えてからアプリ PD の物理を歩いて**
  present + RW + USER を PDE と PTE の両方で確かめる (2 本を 1 回の往復で、CR3 → IF の順で復元)。
  落ちたら `ring3_fault_kill`。ビットの判定表は `exec/ring3_str.c` の純粋関数にして
  `make check-ring3-str-host` のケース 5 で網羅した。
  **PM の検証待ち (W4)**: 両クロック (NP21/W 1.9968 / 実機 2.4576)、CPL=3 からの 64 ビット受け取りと
  ポインタ契約 (`userland/tests/time_test.bin`、`time_test ro` は**わざとアプリを殺す**)、
  非恒等写像の出力が物理側に反映されること、復帰後の CR3 が呼び出し時と一致すること、
  IF=0 を 10.2ms 続けた後の読み (契約外の挙動の記録)。
  **手元で通っていないもの**: `userland/tests/time_test.c` は**コンパイルは通るがリンクできない**
  (この環境の newlib が `/usr/local/cross` に無い。既存の `hal_test` も同じで、私の変更とは無関係)。
  `build/app.conf` と `userland/deploy.yaml` には登録済み ([V2])。
- **修正 (2026-09-23、worktree `wt/hal-fix`)**: NP21/W の起動自己診断で「1 万回連続読みで逆行しない」が
  FAIL した。原因は上の「単調性のクランプ」の項 (8254 の再ロードと 8259 の IRR が原子的でない)。
  `kernel/time_math.c` に純粋判定 `time_clamp()`、`kernel/ktime.c` に `s_last_us` と
  `ktime_clamp_count` を入れた (クランプはスナップショットと同じ `irq_save` の中、書き手は 1 か所)。
  ホスト試験にケース `clamp` と変異 2 本を追加 (`make check-time-math-host` = 5 ケース / 変異 8 本すべて RED)。
  kselftest は 1 万回読みの後に `ktime_clamp_count` を表示し、「全部がクランプではない」ことも見る。
  境界の count を撃ち分ける 2 本の読みは**同じ `irq_save` の中**に入れた (あいだに tick 境界が入ると
  補間の比較が偶発的に落ちる、10ms に数 µs の窓)。

### 1-6. 実装の注意 (往復 5・6・12 の非 blocker、実装レビュー往復 1)

- **ISR 文脈の登録拒否は共通 IRQ の中だけ**を検出する (`irq_in_irq` は `irq_dispatch` だけが増減)。固定 IRQ の ISR や tick フックから
  `irq_register` / `irq_unregister` を呼ぶのも契約違反だが検出されない (現行にその呼び出しは無い)。将来 tick フックから
  登録する driver を書くときは `timer_handler` でも `irq_in_irq` を立てる。
- W4 の「1000 回の所要時間」は平均であって IF=0 の最長時間ではない (IF=1 の区間と割り込みを含み、KAPI の CR3 往復も測って
  いない)。最長 IF=0 (`irq_off_max_us`) とハンドラの 1ms 超診断は未実装 (別に起こす)。

- **出力保護 (`ring3_user_range_writable`、往復 12 の注意)**: (1) 「KAPI 入口でアプリ CR3 を保存する」機構は**既存に無い**
  (`exec/exec.c` の dispatcher も `kernel/ring3_entry.asm` の int80 入口も CR3 を触らない。`g_cur_app->as.pd_phys` は入口で
  採った CR3 とは別物)。helper が呼び出し時の CR3 を自分で読んで保持し (共有グローバル 1 つを無条件に上書きしない)、
  復元は **CR3 → IF** の順。(2) CPL=0 の直呼び (常駐シェル / gshell のローカル出力) は既存の `ring3_user_range_ok` と同じく
  **適用対象外**にする。「CR3 が master と同じか」だけで代用しない。直呼びの正常系も試験に。(3) 2 本の出力は**1 つの検査区間**
  (master 往復 1 回) で扱う。出力ごとに往復すると時計 1 回で CR3 書き込みが 4 回になる。IF=0 の最長時間と呼び出しコストを
  W4 で実測する。(4) PDE の RW / USER も検査する (実効権限は両段の AND。現行の写像では PDE は RW で USER も伝播するので
  反例にはならないが、名前に合う実装にする)。(5) 例外 (#PF / NMI) は「復元して判定を返す」経路ではなく既存の例外処理から
  終了し得る。検査不合格の明示 kill は復元後に行う。(6) W4 の観測: 成功値だけでなく、非恒等写像の出力が正しい物理側に
  反映されること、2 本目の拒否時に 1 本目が不変であること、復帰後の CR3 が一致すること。V86 中の試験はホスト側の時計と
  DOS 側の独自 `INT 80h` を区別する (後者の ABI はこの票の範囲外)。

- **NE2000 の IMR と shadow の同期 (往復 7 B3、実装レビューの必須確認)**: 通常 IRQ の出口 (`ne2k_irq` は leave を
  通らず、OVW 検出で `ovw_begin()` が IMR = 0 にする) ・foreground の leave・tick の**全部の最終出口を 1 つの
  `ne2k_imr_sync()` に集約**し、実 IMR と `imr_written` を同時に書く。OVW_WAIT / FAILED / 再初期化成功のそれぞれで
  shadow が実 IMR と一致することを W3 で見る (一致しないとアダプタが「復旧待ちで装置マスク中」を HANDLED と誤判定する)。
- **noisy の全出口で Bus Master を落とす** (M 不成立、固定 IRQ の unmaskable も)。登録後の隔離にも同じ確認と失敗処理。
- `irq_quarantine_line()` は IRQ の範囲検査 (< 16) をシフト・PIC 操作より前に行う (PCI の未割り当て値 0xFF など)。
- 固定 IRQ の unmaskable は**システム継続を保証できない制限**。固定スタブは NIC の要因を解消しない。
- SCB の M 操作は command の上位バイトだけ、STAT/ACK は対応バイトだけに書く (CU/RU 命令を再発行しない)。
- **driver 共通の排他契約**: tick フックと IRQ callback の中で IF を有効化しない。busy 中の tick は装置に触らない。
  foreground は装置マスク (busy) を出口まで保持する。この 3 条件の下で、tick と callback は単一 CPU・IF=0 で直列、
  STOPPING の公開に「走行中の tick 待ち」は要らない。

- `irq_register`: 未知の flags・NULL の fn・同じ {fn, arg} の重複登録は負。登録操作が失敗したときはストームのマスクを
  解除しない (解除は成功した再計算のときだけ)。排他登録 (SHARED 無し) でも DEFERRED は返してよい。
- `dma_chan_setup` の検査順: `ch` → (配列・ポート表を引く前に) `dir` / `mode` → `bytes` / `phys` → 終端の計算と 64KB /
  16MB 判定 → **全部通ってから** ステータス採取と自チャネルの `done` / `tc_event` の初期化。失敗した setup は旧状態を
  消さない (W2 に試験)。
- `paging_init`: プールは NP 化から外すだけでなく**実際に写像する操作** (`paging_map_range` 相当、RW / supervisor) で張る。
  W5 の USER 変異の復元では TLB を無効化する。
- `pci_cfg_write16`: CF8 の DWORD 書きと `CFC + (reg & 2)` の WORD 書きを同じ `irq_save` の中に置く。offset は偶数に限る (奇数は負)。32 ビットの RMW で代用しない。
  PCI 不在時は既存関数と同じく何もしない。
- `sys_time_now`: `irq_save` の最長区間の計測 (debug) からこの関数を再帰的に呼ばない。
- W7 の `wbinvd` は解決策として確定していない。不一致が出たら対象 CPU と所有権移譲の手順を含めて再設計する。

## 2. 顧客と順序

| 順 | 顧客 | 使う層 |
|---|---|---|
| 0 | 全部 (実機の時間) | 1-0 (sysclk_detect + PIT のクロック修正) — **最初に単独で着地** |
| 1 | 82557 (L-B) | 1-1 (共有 IRQ)、1-3 (CB/RFD)、1-4 |
| 2 | CS4231 PCM (§5-5 P1) | 1-2 (#1/#3、CYCLIC、remaining + TC)、1-3 (16KB)、1-1 |
| 3 | LGY-98 | 1-1 (3/5/6 をこの票で移す、アダプタ) |
| 4 | FDC | 1-2 (薄い呼び出しに置き換え、負を報告) |

## 3. 受入

| ID | 見るもの | 手段 |
|---|---|---|
| W0 | 1-0: NP21/W (1.9968MHz 設定) で reload = 19968 のまま、`kselftest` に `pit_setup` の検査を足す。**実機**: `sys_time_now` はまだ無いので、ホストの単調時計で **`tick_count` を 60 秒間隔で 2 回読む** (シリアル越し、往復遅延 < 50ms を差し引き、誤差 ±0.5% 以内。修正前は 23% ずれる)。回帰: `cpu_calibrate` の `loops_per_tick`、`cpu_delay_us(1000)` **× 1000 回の累積時間** (単発は往復遅延に埋もれる)、FD 起動、シリアル 115200 の ack | NP21/W + 実機 |
| W1 | 純粋関数のホスト試験 (変異つき): 集約と 2 巡 (同時要因、2 巡目の回収、`handled_any` の保持、`irq_deferred_count`、ストーム閾値)、**TC → ack → remaining の遷移表**、失敗した setup が旧 `done` / `tc_event` を保つこと、`irq_finish` の **EOI 送信列** (master / slave / IRQ15 スプリアスの ISR 検査を handled と独立に)、登録の一括有効化と拒否規則 (非対応 IRQ、SHARED 不一致、5 件目)、`dma_split_addr` / `dma_crosses_64k` / `bytes` の範囲 / `dma_accept_pair` / **TC の read-clear の保存**、プールの最初適合と整列と 64KB 跨ぎ (**境界直前から跨ぐ候補を飛ばして後半に置く、32KB × 2 が空プールに入る、LEAKED を free しても空きに戻らない**)、**隣接 span の free と途中ポインタ・二重解放**、PCI の一致規則 (任意 / 完全 / DECLINE で次へ / QUARANTINE で打ち切り / 複数装置)、時計の整数式と **p1/p2 の判定表** (両クロック、周期境界) | `check-par` |
| W2 | FDC が `dma8237` 経由でも **NP21/W の 2HD 起動と md5 一致**、**書き込み → 読み戻しの一致**、`dma_chan_setup` が負のとき FDC コマンドを発行しない、読み失敗のタイムアウト → `fdc_abort_transfer` → 再試行。**実機の FD 起動** と 0439h の 3 値の表示 | NP21/W + 実機 |
| W3 | 共通スタブに `irq_register` した偽装置 (`kselftest` / 試験用 KAPI): NP21/W の `/api/pic` で IRR/ISR を見ながら、master (3) と slave (9) の両方、連続、共有 (2 登録の集約と**残件の tick 回収の完了**)、共有者の**片方解除**後にもう片方が動く、解除後に callback が走らない、CPL=3 のアプリ実行中と V86 中、**要因を落とさずマスクもしない偽装置で 2 巡で戻ること** (救済しない契約の確認) と、**誰も受けないエッジを 201 回/tick 打ってマスクが入る (200 回では入らない)** ことを別の試験に、**正常に HANDLED を返す 2 つの偽装置で走査終了と新要因を重ねてエッジを失わせ、双方の tick フックが回収する** (B1 の反例)、**NE2000 の enter/leave の両方の窓への割り込み注入** (B2)、**82557 の巻き戻し中 (STOPPING) に共有線の別装置の IRQ を上げても、tick からも SCB に触らない** (B3、偽装置で代替)、**STARTING の観測点: (4) 登録直後・(6) 開始中・(7) M 解除直後のそれぞれで共有線の IRQ を上げ、(7) より前は装置アクセス無し、(7) の後は通常処理** (往復 9)、**NE2000 の再初期化直後の窓への注入** (往復 6 R1)、**noisy 隔離: 線が上がったままエッジが来ない / 共有者が HANDLED を返し続ける、の両方で `irq_quarantine_line` が入り再計算で解除されない** (往復 6 R3)、**`DEFERRED` を返した偽装置が自分の tick フックで残件を回収してマスクを外す**こと、**残った側が `DEFERRED`・装置マスク中のまま片方を解除しても残った側の tick フックが動き続ける**こと、IRQ0 の所要時間がこの票の前後で変わらないこと (`/api/prof`)、**LGY-98 をアダプタに移した後の rshell/LAN の通信** | NP21/W |
| W4 | `sys_time_now`: 1 万回連続読みで逆行 0 (**クランプ込み**。`ktime_clamp_count` を kselftest が表示するので、NP21/W と実機のそれぞれで**押さえた回数を記録する** — 0 なら p1/p2 だけで足りていた機械)、両クロック (NP21/W 1.9968 / 実機 2.4576)、**境界注入**: IF=0 で PIT の残りが 1〜2 count の位相から読む試験 (`kselftest` が count を見て待ち合わせる) で p1/p2 の 3 分岐を全部踏む、**三分岐の網羅は hook だけが必須** (往復 8 R8-3: 既定の入力列 `{p1, p2, count}` を与える test hook と分岐カウンタで 0/0・1/x・0/1→再試行→成功・0/1×3→`-EAGAIN` を全部踏む。実 PIT では呼び出しから p1 読みまでに境界を越える機械があり、位相待ちだけでは 0/0 と 0/1 を踏めない)、**実 PIT の試験は期限付きの位相待ち** (**位相待ちは IF=1 で**行い、残り 1〜2 count を観測した瞬間だけ短く IF=0 にして採取する。IF=0 で待つと IRQ0 が止まって tick の期限が進まない (往復 9 R9-3)。期限は 100 tick、さらに IF=0 区間の中の観測ループにも独立した有限回数 (1000 回) の上限。実際に通った分岐を記録し、踏めなかった分岐は「未検証」と報告する。**三分岐の網羅は hook の方だけが必須**)、**PIT の残りが 0.1ms の位相から** IF=0 を 10.2ms 続けた後の読み (契約外の挙動を記録)、**CPL=3 の KAPI 経由の試験は post-exec 側** (`kselftest_run_post_exec`、`exec_init` の後)、CPL=3 から 64 ビットが揃って返る | NP21/W + 実機 |
| W5 | カーネル増分 ≤ 5KB、内訳: IRQ 表 8 × 4 × 12B = 384B、storm ビット + IRQ 別の tick 内発生数・deferred 回数 (8 × 2 × 4B = 64B)、bind 情報 32 件 × 8B = 256B + getter/KAPI/診断コード、pool ビットマップ + span 16 × 4B = 72B、DMA チャネル状態 4 × 16B、pit_setup 20B、コード (irq / dma8237 / dma_pool / pci_bind / sys_time ≒ 2.5KB、u64 の割り算は libgcc に既にある)、診断文字列 ≒ 0.5KB。`__bss_end` 差分と `docs/02_memory.md` の生成 (**新配置 0x2E8000〜0x2F7FFF が `MM_RW` で USER 無し、上下が NP**)、プールの枯渇 → 解放 → 再利用、**プールの PTE に USER を立てる変異で検査が落ちる** | `docs/02_memory.md` + kselftest |
| W6 | **土台完了**の受入はここまで。82557 での連続 TX/RX・共有・再開は **L-B の受入**。土台完了は**システム全体の IF=0 時間保証ではない** (NE2000 の残件: 既存 ISR 経路に加え、変更後は foreground の leave → `irq_pending` → `service()` の経路も IF=0)。土台の合格と実機条件の合格 (W0 実機、W7、82557 の実 IRQ での共有) は分けて記録する。HAL 単体で未検証の実機条件を残件に明記: 82557 の実 IRQ 番号での共有、PCI バスマスタのキャッシュ整合 (W7)、CS4231 の DMA (§5-5)、**NE2000 の `wait_rdc` / `hw_reset` が ISR 文脈で回る既存の契約違反** (`ne2k_irq` と `ne2k_timer_tick` の両方から `service()` の失敗分岐 → `reinit_or_fail → bring_up → hw_reset` で到達。L-C で「要求の記録」と「foreground の実行」に分ける) | — |
| W7 | **キャッシュ整合 (実機)**: CPU がパターンを書いて**フラッシュせずに** DMA で読ませる (FDC 書き込み → 読み戻し、82557 は L-B のループバック)、DMA で受けた直後に CPU が読む、を双方向で 100 回。所有権の移譲は「CPU 書き → 装置へ渡す → 装置完了の証拠 → CPU 読み」の順で、明示のフラッシュ命令は使わない。不一致が出たら対象 CPU と所有権移譲の手順を含めて再設計する (1-6。`wbinvd` は候補であって確定ではない) | 実機 |

### 3-1. 受入の記録 (PM、2026-09-23、NP21/W + 実機 Ra266 の W0)

記録は [TASK_HAL_WIRING_RESULTS](../../archive/v3/TASK_HAL_WIRING_RESULTS.md) へ移した。

未確認・残件 (記述時点は元の記録のまま):

| ID | 結果 |
|---|---|
| W0 | `cpu_delay_us × 1000` の実機値は未 (シェルから読む口が無い) |
| W2 | タイムアウト → abort → 再試行の経路は未観測 (NP21/W では起きない) |
| W3 | 実 IRQ の**共有 (2 装置)**、slave 側 (9)、V86 中は未 (`/api/pic` は読み取り専用で人工のエッジは作れない) |
| W4 | 実 PIT の位相待ちは p1=1 の分岐だけ踏めた (0/0 と再試行は未検証)。 **`[selftest] 186/186 passed`** (NP21/W は 187。 |
| W6 | 残件: 82557 の実 IRQ での**共有** (実機の `lspci`: 0:11.0 8086:1229 **irq 3 pin A**、bar1 io 0x6000、Command 0x0147 = IO/MEM/BM 有効 → 動的 IRQ 3 で `irq_register` できる番号。TASK_LAN_82557 §6)、W7 キャッシュ整合、NE2000 の ISR 内 reset (L-C)、既存出力 KAPI の RO 穴 (TASK_KAPI_OUTPUT_GUARD)、**io_wait() 連打で FD 読みが古くなる (POLICY_DEBUG §4-55、原因未特定)**、p1=1 で 1 周期先に出る値の扱い (クランプで単調だが 10ms 止まる。判定に count を併用する案) |
| W7 | 未 (実機。FDC の書き→読み戻しは W2 で NP21/W のみ) |

## 4. しないこと

- BAR の自前割り当て (BIOS 未割り当ての装置)。
- 既存の固定 IRQ (0/1/2/4/7/11/12/13) の移行。レベルトリガの PIC 設定。
- 境界モード `0029h` の変更、拡張バンク (16MB 超)、8237 の demand / block モード、番地減分。
- 1kHz tick と `pit_init` の汎用 hz (§5-5 の別票)。
- 外部モジュールの ABI (§3)。`struct pci_driver.size` はそのための口だけ。

## 5. ユーザー決裁が要る点

> **決裁 (ユーザー、2026-09-23)**: (1) DMA プールの 0x2E8000 固定は**暫定承諾** (v3 でメモリマップ自体を見直す予定。
> その時に再配置し得る)。(2) 1-0 の先行着地は**承認**、先に直す。レビューは追加 1 往復 (往復 4) を使う。
> 往復 4 も Request changes だったため、ユーザーは **v5 以降にさらに 3 往復まで** (往復 5〜7) を許可 (2026-09-23)。
> 往復 7 の後、ユーザーは **v8 の 2 つの設計判断 (登録拒否 = 利用不可、動的 IRQ の全登録者に定期回収) を了承し、実装着手 (2 本に分割: A = 1-1 + 1-5、B = 1-2 + 1-3 + 1-4) と、設計レビューさらに 3 往復 (往復 8〜10) を許可** (2026-09-23)。実装は B から (往復 5 以降に新規指摘が無い部分)。

1. **DMA プールを SQLite 帯の予約域 0x2E8000〜0x2F7FFF に固定** (メモリマップの数字。決裁 D1 の続き)。
   SHM・GUI オフセット・SDK・アプリは無変更。代償は SQLite の成長余地が 120KB → 44KB になること (リンク時 ASSERT)。
2. 1-0 (`sysclk_detect` + PIT のクロック修正) を**この票より先に単独で着地**させてよいか (実機の時間が 23% ずれている
   既存バグ。W0 を単独で閉じる)。

## 6. この票の外で見つけた不整合

- `tools/memmap_audit_live.py:25` は GUI 基点を `MEM_SHM_BASE + 0x30000` で決め打ちしている。正典 (`MEM_SHM_GUI_OFFSET`
  = 0x28000) とずれている (2026-09-17 の末尾相対化の取り残し)。別の小票で直す。
