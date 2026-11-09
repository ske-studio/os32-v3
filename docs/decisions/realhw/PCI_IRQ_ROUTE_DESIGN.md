> 出所: ~/os32-tmp/run/pci-irq/DESIGN.final.md (2026-11-09、Claude 起草・codex astra のレビュー)。
> 状態: **現行 (2026-11-09)** — PCI の IRQ を機器の種類で振り直す (共通の層と PC-98・PowerMac G4 の backend) の確定設計。段の状態は票 TASK_PCI_IRQ_ROUTE が持つ。

# PCI の割込みの振り直し — 調べと設計 final (2026-11-09)

## r3→r4 の変更 (REVIEW.r2.md の 6 件。ID はレビューの記載順に振った。PM の決定で全件を直し案どおりにした)
- R2-01 (P1): 移してよいのは「全機能が settle 済みで、どれもまだ attach していない」group だけにした。attach が 1 つでもあれば凍結して移さない。結び直しは削った。凍結と順序の試験を足した (§3-1・§3-3・§6)。
- R2-02 (P1): GeForce の IRQ。PM の決定で、前提の段 gf-bdf の枝に `hwdev/gf/irq.c:12・96・293` の直しを PM が足す (§5 の前提の行)。本書では、GeForce を機器単位の口へ移す段 9 と、受入「E′ で IRQ5 の GPU が 5 で登録される」を明記した。§8 の申し送りは閉じた。
- R2-03 (P1): 82557 のモジュールの包み (`e82557.c:25`、`lan_module.c:107・114・210`) を機器単位の口に統合する段 8b と、移した後の解除の試験を足した (§3-1・§5・§6)。
- R2-04 (P2): 移す前に新線へ登録できるか (隔離・排他・空き slot) を確かめる。移した後の最初の attach が失敗したら、BIOS の線へ戻す (§3-3・§6)。
- R2-05 (P2): 戻しにも失敗したとき、線はマスクしない。その group の割込み源を止め (settle 済み、加えて Command の bit10)、共有の線は残った登録に従う (§3-4・§6)。
- R2-06 (P2): probe は、単独でも増える線ごとの dispatch の計数 (段 10) と、ISR の中の SWI の計数 (段 8、tick と区別) で判定する (§5・§6)。

## r2→r3 の変更 (REVIEW.r1.md の 10 件。ID はレビューの記載順に振った。PM の決定で全件を直し案どおりにした)
- R1-01 (P1): 振り直しが効くことの証明として、動いている機器を BIOS と違う線へ移して新しい線の ISR で受ける probe 試験を足し、通常の有効化の関門にした。関門を通るまでの既定は「計画だけ表示」(§3-4・§6)。
- R1-02 (P1): GeForce の BDF の固定は別の設計 gf-bdf が受け持つ。本書では「前提の段: gf-bdf の着地」として依存に書き、段 0 を削った。IRQ の登録 (`hwdev/gf/irq.c:12・96`) は gf-bdf が覆っていないので、末尾の申し送りに書いた (§2・§5・§8)。
- R1-03 (P2): 配線が分からない機能・線が未割当の機能・pin の無い機能の書き方、自己照合の対象、関連する group を固定する条件を明記した (§3-1)。
- R1-04 (P1): 対応している機器かどうかと、静止の時機 (起動時から静か / ドライバが止めた後) を計画の入力にした。EHCI は停止を確かめる前は動かさない。group の移動は、その group の機能が全部「静止」になった時点で行う (§3-1・§3-2・§3-3)。
- R1-05 (P2): 「どの登録より前、動的線は全部マスク済み」という書き方をやめた。FDC が IRQ11 を先に登録し、割込みも許可済みだから。移す旧線・新線の登録とマスクの状態を保存してから書き、終わったら戻す手順に限った (§3-3)。
- R1-06 (P2): 「IRR に積むので 1 回は届く」という保証を削った。回復は、各ドライバが開始時に要因を回収することと tick で行う。旧線に残った割込みの扱いも書いた (§3-3)。
- R1-07 (P2): LGY-98 を「影響なし」から外した。候補の 3・5・6 と重なるので、要因の判定 (`ne2000_irq.inc:25`) と定期回収 (`:88`) を評価に含め、受入に同時通信を足した (§2・§6)。
- R1-08 (P2): 計画の値は成功したときだけ公開する。失敗したときは元の列挙表と owner を保つ。戻しの読み戻しも失敗したら、その group を未割当にする (§3-4)。
- R1-09 (P2): group を連番の添字に正規化し、元の番号 (`native`) は別に持つことにした。読むだけの backend も OF で解いた線を列挙表に写す (§3-1・§6)。
- R1-10 (P2): 本物の backend を偽の config I/O で動かす段を足した。書き込みの位置ごとの失敗・戻し・固定の group が変わらないことを変異で確かめる (§5・§6)。

## r1→r2 の変更
- ユーザー指摘「これは、PowerPCでも効いてくる部分だと思っているのですが」を受けて **2 層の構造にした** (§3-0)。上は CPU に依らない共通の層 (機器ごとの割込みの登録・一覧と診断・共有の扱い・種類ごとの割り振りの方針)、下は platform の backend (PC-98 = 0:6.0 の PIRQ の表と 8259、PowerMac G4 = OF のデバイスツリーと MPIC)。
- backend は os32powerpc の推奨の形に揃えた。(a) 起動時の読み取りで割込みの表を作る口と、(b) 割込みコントローラの操作の口 (既存の `include/platform_irq.h`) の 2 つ。共通の層から見えるのは (b) と、(a) が作った表の読み取りだけ。backend は「振り直せる (PC-98) / 読むだけ (G4)」の能力を申告する。
- G4 の今の実物 (main 3ebbd208) を §1-5 に、PowerPC の backend の口と手本の対応を §3-6 に足した。PowerPC 側の実装は os32powerpc が担当する。
- ファイルと段 (§5) に契約のヘッダ・番人・PowerPC の段を足した。受入 (§6) にモック backend 2 種のホスト試験を足した。未決は 1 件増えて 4 件 (§7-4)。
- PC-98 の調べ (§1-1〜1-4)・ドライバの耐性 (§2)・組み合わせの試験 (§6-1) の中身は r1 と同じ。行数を収めるため文を詰めた。

前提 (議論しない): シングルタスク・協調切替、9801 互換なし、整数。土台は「ドライバが IRQ の共有に耐える」こと (§2)。振り分けはその上に乗せる性能の改善。

## 1. 調べの結果

### 1-1. PC-98 の回路の所在 — C バスブリッジ 0:6.0 (1033:002c) の 0x60〜0x63
- PIRQ の振り分け器は **C バスブリッジの中** にある。82441FX (0:0.0) はホストとメモリの橋で、振り分け器を持たない (実機の 0x50〜0x72 は PAM・DRB の値)。Ra266 に PIIX は無い。
  - 出典: `/mnt/c/WATCOM/docs/undocumented/io_pci.md:282-321` (PIRQ0〜3 の配線。振り分け器は C バスブリッジの中にあり、通常は PnP BIOS が起動時に設定する)。
  - `pci_cbus.md:306-335`: **0x60〜0x63 = PIRQ0〜3 の経路**。bit7 = 1 で切り離し、bit6〜0 = IRQ。PIIX の PIRQRC と同じ形。
  - `pci_cbus.md:232-303`: 0x50〜0x5F = 板上の機器ごとの IRQ (bit7 = 1 で使わない)。資料は「通常は変えない」と書く。
  - 資料の対象は Xa の 1033:0001 で、資料自身が「機種ごとに実装が違い、互換は保証しない」と書く (`pci_cbus.md:18-24`)。
- 実機の値 (`~/os32-tmp/evidence/2026-10-15/ra266_snapshot/ra266_snapshot_2026-10-15.txt:118-136` と、ノートで観測した構成 A・E):
  - 0x60〜0x63: 構成 E `05 06 80 03`、構成 A `05 06 0a 03`。違うのは 0x62 だけ。0x50〜0x5F は両方で `00 01 02 0f 04 85 86 07 88 09 8a 0b 8c 0d 0e 8f`。
  - 0x3C と照合すると、82557 (0:11.0) の pin A → IRQ3 = 0x63、0:12.0 の pin A → IRQ5 = 0x60、0:13.0 の pin A → IRQ6 = 0x61。資料の形どおりに読める。
  - ※ 構成 A・E の生ダンプ (`usb-swap/pci-save/`・`hpt-back/pci-save/ALL_raw.txt`・`usbcard/`・`usb-bar/`) は WSL に無い (ノート側にある)。上の A の値は PM の伝言で、確定前に写して照合する。
- 配線の推定: 0:11.0 の INTA → PIRQ3。0:12.0 の INTA/B/C/D → PIRQ0/1/2/3。0:13.0 の INTA/B/C/D → PIRQ1/2/3/0。io_pci.md の表 3 (:288-295) と合う。pci_cbus.md:323-330 の表とは向きが違う。
- **共有には 2 種類ある**。(a) 同じ PIRQ に INTx が複数繋がる共有は基板の配線で決まり、OS は解けない。(b) 別々の PIRQ が同じ IRQ に落ちる共有 (構成 A の PIRQ2 → IRQ10、CS4231 と同じ線) は解ける。**振り直しで減らせるのは (b) だけ。**

### 1-2. 書けるか — **はい。ただし Ra266 で書いた値が効くかは実機で確かめる (R-2)**
- BIOS が構成ごとに 0x6x を書き分けているので、書けば効くレジスタだと強く示唆される。BIOS は BAR の番地も起動ごとに配り直している。
- 0x50〜0x5F の bit7 は 2 通りに読める (R-3)。読み 1 は資料どおりの「機器ごと」(53 = `0f` は IRQ3 の板上の機器を 15 へ逃がした意)。読み 2 は「bit7 の立った IRQ が PCI の候補」。どちらの読みでも、BIOS が PIRQ に使った IRQ (3・5・6・10) に PCI の割込みが届くことは実測で言える。**0x50〜0x5F は書かない。**

### 1-3. BSD の扱い
- FreeBSD の pc98 は i386 の `pci_pir.c`・`bios.c` をそのまま使う (`sys/conf/files.pc98`)。PC98 固有なのは `PCI_IRQ_OVERRIDE_MASK` 0x3e68 だけ。$PIR は E0000〜FFFFF から探し、経路付けは PCI BIOS に頼む。**BIOS が付けた経路は触らない** (`pci_pir_initial_irqs`)。NEC の振り分け器を直接叩くコードは無い。
  - https://github.com/freebsd/freebsd-src/blob/stable/11/sys/i386/pci/pci_pir.c ・ …/sys/i386/i386/bios.c ・ …/sys/conf/files.pc98
- FreeBSD の `sys/x86/isa/atpic.c`: pc98 の ICW1 はエッジ、ELCR は pc98 で使わない。IRQ 0・1・7・8 以外を「レベル」と表で扱うだけ。
- NetBSD の `sys/arch/i386/pci/pci_intr_fixup.c`: 振り分け器の表に **NEC (0x1033) は無い**。BIOS が付けた経路は変えず、未割当のリンクだけを付ける。 https://ftp.riken.jp/NetBSD/NetBSD-current/src/sys/arch/i386/pci/pci_intr_fixup.c
- Ra266 の BIOS に $PIR・`_32_`・$PCI があるかは不明 (吸い出しが無い)。OS32 は経路付けに BIOS を使わないので設計は左右されない。照合には使える (R-1)。
- OS32 の 8259 は `platform/pc98/pic.c:15,21` でエッジ。PC-98 に ELCR は無い。

### 1-4. 実機で確かめる点
- R-1: F0000〜FFFFF に $PIR・`_32_`・$PCI があるか (読むだけ)。
- R-2: 0x6x を書いて読み戻し、その IRQ に割込みが届くか (82557 の SWI)。
- R-3: 0x50〜0x5F の読み方。IRQ 8・15 に PCI の割込みが届くか。
- R-4: uPD720101 の各機能の 0x3D と、0:12.0・0:13.0 での PIRQ の検算。

### 1-5. PowerMac G4 QuickSilver の今の実物 (main 3ebbd208、os32powerpc の報告)
- 外部割込み (0x500) は未対応。登録の口も無く、今あるのは DEC だけ: `arch/ppc/exception.c:35-37`・`arch/ppc/decrementer.c:42`・`arch/ppc/ppc_frame.h:22`。IRQ の禁止・許可は `arch/ppc/ppc_oea.h:58-60` と `arch/ppc/arch_io.h:6-8`。
- 線の数は仮の値 (`platform/powermac/platform_irq_limits.h` が 1/1)。共通の irq の配列の大きさを platform から取る形は分離の第 2 段で済んでいる (`docs/decisions/ppc/PPC_SEP2_DESIGN.md` の「IRQ（C段）」)。
- OF の読み取りは `platform/powermac/ofw.h:9-27`・`ofw.c:73-81`。**client interface は OF から切り離す前の、起動の早い段でしか呼べない**。だから配線は早い段で木から表に写し、後はその表を使う。
- 事実の正典: `docs/ppc/powermac.md:27・40-56・82`。MPIC は KeyLargo の中にある。macio の子は `AAPL,interrupts` か `interrupts` を持つ。PCI は `interrupt-map` を引いて親の PIC を決める。`docs/ppc/open-firmware.md:97-99`: PCI の子の `interrupts` は INTA=1〜INTD=4 の pin で、PIC の番号ではない。未確定: interrupt-map-mask、複数段の nexus、極性と edge/level。
- G4 では **線の付け替えは OF (と基板) が決め、OS は読むだけ** の見込み。FreeBSD の OpenPIC (`sys/powerpc/powerpc/openpic.c`) も、sense と極性を枠組みから受けて `OPENPIC_SRC_VECTOR` に書くだけで、経路は選ばない。G4 にも同じ uPD720101 の USB カードが刺さっている。

## 2. 今のドライバの共有への耐性
登録の口は `kernel/irq/irq.c:72`。契約は `kernel/irq/irq.h:9-22` (共有可、全登録者が tick で回収)。動的な線は `platform/pc98/irq_policy.c:6-8` の 3・5・6・8・9・10・11・14・15。

| 機器 | 登録 | 自分の要因を確かめるか | 判定 |
|---|---|---|---|
| 82557 | `drivers/net/e82557/e82557.c:697` (dev->irq_line、SHARED) | `:416-440` SCB の STAT を読み、0 と 0xFF は NONE | 耐える |
| CS4231 | `drivers/sound/cs4231/pcm_cs4231.c:548` (PCM_IRQ=10 固定 `pcm_cs4231.h:38`) | `:385-415` PI を見て ack したときだけ HANDLED。tick は `:423` | 耐える |
| GeForce | `hwdev/gf/irq.c:96` (SHARED) | `:240-` PMC を読み、要因のある所だけ排出。tick は `:277` | 要因の確認は耐える。**ただし線は `GF_IRQ_LINE 6U` の固定 (`:12`) で、0x3C は `:94` で記録するだけ。BDF も固定** (`resources.c:18`・`bus.c:10`・`master.c:16`) → 前提の段 gf-bdf (IRQ の線は PM が足す) と段 9 |
| USB | 未実装。設計の `docs/decisions/usb/USB_DESIGN.md:215` は 0x3C の線を使い、`:234` の intr_collect が要因を返す | 設計上は耐える |
| LGY-98 (C バス) | `drivers/net/ne2000/lgy98.c:69・145` (SHARED、IRQ はジャンパの 3・5・6 など) | `ne2000_irq.inc:25` で ISR の要因を確かめ、残件は `:88` の `ne2k_timer_tick` で定期回収 | 耐える。ただし **PCI の候補 3・5・6 と重なる**ので、振り直すと新しい共有の相手になり得る (§3-2 の reserved、§6) |
| FDC | `drivers/block/fdc.c:278` (排他、IRQ11)。PCI の列挙より前に登録し、割込みも許可済み (`platform/pc98/boot.c:65`、`init/kernel.c:149・157`) | — | PCI の候補ではない。ただし §3-3 の「全部マスク済み」という前提を崩す |

## 3. 設計

**基準 (ユーザー方針の原文)**: 「振り直す動作の基準はあくまで現代の人間が迷わない割り振りで、このような実装の曖昧さを現代基準で抽象化します」。
- PC-98 の PIRQ・0x3C・エッジ/レベルや、G4 の interrupt-map・MPIC の source・sense は **backend の中に閉じる**。
- ドライバは「自分の機器の割込み」を登録するだけで、線・共有・PIRQ を意識しない。
- 利用者には機器ごとの一覧 (種類・BDF・IRQ・共有の相手) を見せる。
- 方針は現代の慣習どおり: **音・表示・USB・LAN を別の線に。どうしても共有するなら、遅延に厳しくない組で。**

### 3-0. 2 層の構造
| 層 | 置き場 | 持ち物 |
|---|---|---|
| 共通 (CPU・機種に依らない) | `drivers/pci/pci_irq_plan.c` (純粋)・`drivers/pci/pci_irq.c` (つなぎ) | 機器ごとの登録の口、割り振りの方針 (§3-2)、一覧と診断、`pci_dev.irq` の更新 |
| 契約 | `include/pci_irq_platform.h` | 表の型・能力の申告・2 つの口 (下の 3-1)。実装は置かない ([arch/README.md] の約束) |
| backend PC-98 | `platform/pc98/pci_irq_pc98.c` | 起動時に 0:6.0 の 0x60〜0x63・各機能の 0x3D/0x3C・機種表を読んで表を作る。振り直しの書き込み・読み戻し・戻し。操作の口は既存の 8259 (`platform/pc98/pic.c`・`irq_complete.c`) |
| backend G4 | `platform/powermac/pci_irq_ofw.c` (os32powerpc) | 早い段の OF の読み取りで表を作る (読むだけ)。操作の口は MPIC (`include/platform_irq.h` の mask・unmask・claim=IACK・complete=EOI、加えて優先度と宛先) |

番人: `tools/check_pci_irq_portable.py` (`tools/check_usb_portable.py` と同じ型)。対象は共通の `drivers/pci/pci_irq*.c`。`pc98.h`・`platform_io.h`・`ofw.h`・`arch_*.h` の include、`inp`/`outp`、config の書き込みを禁じる。`#ifdef` で層を分けることも禁じる (`arch/README.md`)。

### 3-1. 契約 (`include/pci_irq_platform.h`)
```c
struct pci_irq_fn {            /* 1 機能 1 行、起動時に backend が埋める */
    u8  bus, dev, fn, pin;     /* pin 1..4、0 = 使わない */
    u8  cls, sub, progif;      /* 種類の判定は共通の層 */
    u8  sense;                 /* PCI_IRQ_EDGE / PCI_IRQ_LEVEL_LOW (表示用、設定は backend) */
    u8  group;                 /* 0..ngroups-1 の連番 (R1-09)。PCI_IRQ_GROUP_UNKNOWN = 配線不明、_NONE = pin 0 */
    u16 line;                  /* 共通の irq 層の線番号。PCI_IRQ_LINE_NONE = 未割当 (0x3C=0xFF 等) */
    u8  owner;                 /* PCI_IRQ_BY_FIRMWARE (BIOS/OF のまま) / _BY_OS */
};
struct pci_irq_group { u16 native; u16 line; };  /* native: PC-98 は PIRQ 0..3、G4 は MPIC の source (疎でよい) */
struct pci_irq_caps {
    u8  can_reroute;           /* PC-98 の Ra266 = 1、G4・不明な機種・PCI 無し = 0 */
    u8  ngroups;
    u32 cand_mask;             /* group に渡せる線 (PC-98: {3,5,6})、32 本超は将来に広げる */
    u32 reserved_mask;         /* PCI を落とさない線 (PC-98: 10 = 音、ほかに §3-2) */
};
int platform_pci_irq_table(struct pci_irq_fn *out, int cap,
                           struct pci_irq_group *grp, int gcap, struct pci_irq_caps *caps); /* 表の写し、IF 不問 */
int platform_pci_irq_route(u8 group, u16 line); /* 1 group ずつ。can_reroute=0 は NOTSUP。失敗は §3-4 */
```
- **機能の分類** (R1-03、backend が埋める):
  - pin = 0 → `group = NONE`。計画にも自己照合にも入れない。
  - 配線表に無い dev (Ra266 の内蔵 Trident 0:8.0 は pin A・0x3C=0xFF) → `group = UNKNOWN`。line が NONE なら、BIOS も経路を付けていないので計画に入れない (振り直しの前後で変わらない)。line に値があれば、**BIOS の 0x6x がその line と同じ group を全部固定**にする (その機能が繋がっている可能性があるため)。
  - 自己照合の対象は「配線表にあり、pin ≠ 0、line ≠ NONE」の機能だけ。1 つでも合わなければ配線表が違うとみなし、何もしない。
- **対応と静止の判定** (R1-04、共通の層の `pci_irq_kinds.c`。計画の入力にする): 機能ごとに、OS32 のドライバの対応表 (vendor:device かクラス) と照らして {重み, 静止の時機} を付ける。静止の時機は 3 種類。
  - `QUIET_AT_BOOT`: 起動時に割込みを出さないと分かっている機器。
  - `QUIET_AFTER_DRIVER`: ドライバが自分で止めた後に静かになる機器。EHCI と OHCI (BIOS の引き継ぎ・SMI があり得る)、82557、GeForce。
  - `NEVER`: 対応表に無い機器。
  初版は `QUIET_AT_BOOT` を空にする (全部ドライバに止めさせる)。
- 共通の層の流れ: `table` → plan (§3-2) で各 group の行き先の line を決める → group ごとに「移す・保留・固定」。**移すのは、その group の全機能が静止になった時点** (§3-3)。`pci_dev.irq` を書き換えるのは `route` が成功した後だけ (§3-4)。
- ドライバの口: `pci_irq_settle(const struct pci_dev *d)` で「自分の機器を止めた」と告げ、続けて `pci_irq_attach(d, fn, arg)` / `pci_irq_detach`。attach は、引数 `d` の中の irq (呼び手のコピー) を**使わない**。`d` の BDF で共通の層の管理表を引き、その時点の線 (移した後の線) で `irq_register(line, fn, arg, IRQ_F_SHARED)` を呼ぶ。実際に登録した線は、機能ごとの登録の記録に保持し、`pci_irq_detach` はその記録の線で外す。理由: `pci_bind_all` は列挙表のローカルコピーをドライバへ渡す (`drivers/pci/pci_bind.c:51・62`)。そのコピーは settle の後も旧線のまま残る。保留中の group は、最後の機能の settle で移る。ただし、その group のどれかの機能が既に attach していれば移さない (凍結、§3-3)。結び直しはしない。
- モジュールの登録管理との統合 (R2-03): 82557 の登録は、モジュールの包み `lan_module_irq_register` を通っている (`e82557.c:25`)。この包みが保持する線と解除先 (`lan_module.c:107・114・210`) は、モジュールの参照の勘定 (`MODULE_REF_IRQ`) と一緒に管理されている。そこで、包みを機器単位の口に合わせて `lan_module_pci_irq_attach/detach` に替える。包みは `pci_dev` と fn を保持し、`pci_irq_attach` が実際に結んだ線 (移した後の線) を記録する。解除・アンロード (`lan_detach`) はその記録した線で外す。82557 は段 8 と段 8b でこの口へ移す。GeForce は段 9 で移す。対応表にあってもドライバが読み込まれなければ、その group は settle しないので BIOS のまま残る。
- 読むだけの backend (G4) も、OF で解いた line を `pci_irq_fn.line` に入れ、共通の層はそれを `pci_dev.irq` に写す (R1-09)。
- `pci_dev.irq` は u8 (0xFF = 未割当)。MPIC の source (≤ 254) も収まる。

### 3-2. 割り振り (共通の層、純粋な関数)
| 種類 | 重み | 理由 |
|---|---|---|
| 音 (CS4231、板上の IRQ10) | — | PCI ではない。`reserved_mask` で守る |
| 表示 (03.xx) | 4 | VBLANK・FIFO は遅延に厳しい |
| USB OHCI (0c.03.10) | 3 | キーボード・マウス |
| LAN (02.00) | 2 | 量は多いが遅延には寛容 |
| USB EHCI (0c.03.20) | 0 | 段 1 は静止 (USBINTR=0)。ただし動かすのはドライバが止めて settle した後だけ |
| ドライバの無い機能 (`NEVER`) | 固定 | その group は firmware の値のまま触らない |
| C バスの機器が登録済みの線 (LGY-98 など) | — | 計画を立てる時点で登録されている非 PCI の線は `reserved_mask` に足す。後から結ぶ C バスの機器とは共有になり得るが、要因の判定と tick で耐える (§2) |

0. 固定の group (`NEVER` を含む group、§3-1 の UNKNOWN で固定にした group) の線は使用済みにする。
1. group ごとに重みを足す (基板の共有は足し算で受け入れる)。
2. 重みの大きい group から順に、空いている `cand_mask` の線を渡す。firmware の値が空いていれば優先する。
3. 足りなければ、重みの小さい 2 つの group を同じ線にまとめる。同点なら同じ種類どうしを先にまとめる。firmware と同じ結果なら何も書かない。
4. `can_reroute=0` (G4) のときは割り当てを変えず、同じ計算で **共有の有無と「遅延に厳しい組が共有している」の警告** だけを出す。
- 例 (構成 A): PIRQ0 = OHCI(3)、PIRQ1 = OHCI+GeForce(7)、PIRQ2 = EHCI(0)、PIRQ3 = LAN(2)。PIRQ1→6、PIRQ0→5、PIRQ3→3、PIRQ2→3 (LAN とまとめる)。**音の線から PCI が外れる**。ただし PIRQ2 が実際に移るのは、USB のドライバが EHCI を止めて settle したとき。USB が着地するまでは保留のまま IRQ10 に残る。

### 3-3. いつ書くか・書き方 (PC-98 の backend)
- 計画は `pci_init` (`drivers/pci/pci.c:273`) の列挙の直後に 1 回だけ立てる。書き込みは group ごとに、その group が「全機能が静止」になった時点で行う (多くはドライバの settle の中、§3-1)。
- **前提にしないこと** (R1-05): 「どの登録より前」「動的な線は全部マスク済み」とはしない。FDC は PCI の列挙より前に IRQ11 を排他で登録し、割込みを許可している (`init/kernel.c:149・157`、`platform/pc98/boot.c:65`、`drivers/block/fdc.c:278`)。
- **移してよい条件** (R2-01): その group の全機能が settle 済みで、**どの機能もまだ attach していない**こと。attach した機能は動き出している (割込みを出し得る) ので、その時点で group は「凍結」になり、以後は移さない (`[pci-irq] group <n> frozen: started`)。凍結した group は計画と違っても今の線のまま残る。結び直しはしない (r3 の手順 3・5 を削った)。
  - 例: A′ では LAN (82557) が先に attach すると、EHCI が後で settle しても PIRQ3 は凍結のまま。計画でも PIRQ3 は 3 なので、実害は無い。
- 1 group の移し方 (`irq_save` の中で行う):
  1. **新線に登録できるかを先に確かめる** (R2-04): 隔離されていない (`irq_math.c:44`)、排他の登録が無い、空きの slot が group の機能の数以上ある。どれかが欠ければ移さない (固定)。
  2. 旧線と新線について、今のマスクを保存し、両方をマスクする。
  3. 0x6x を 1 バイト書いて読み戻す。続けて、その PIRQ に繋がる機能の 0x3C を書いて読み戻す (配線は変わらないが、ドライバと lspci が読む値なので揃える)。
  4. 両方の線のマスクを、登録数から決まる状態に戻す (`kernel/irq/irq.c` の `irq_recount_locked` の規則)。移した group の登録は 0 なので、他の登録者の状態だけで決まる。
- 移した後の attach が失敗したとき (手順 1 の確認から attach までの間に隔離された、など): それが group で最初の attach なら、その group を BIOS の線へ戻し (§3-4 の「移動成功後の復元」)、旧線で attach をやり直す。2 つ目以降の attach なら、その機能は IRQ 無しとして tick で動かす (`irq.h` の契約)。どちらもログに残す。
- **届くことの保証はしない** (R1-06): 旧線の IRR に残った割込みは新線へは移らない。新線の共有相手が既に割込みを上げたままなら、新しいエッジも起きない (この喪失は `kernel/irq/irq.h:15` の契約が既に認めている)。回復は次の 2 つで行う。
  - 各ドライバは開始時 (attach の直後) に、自分の要因を 1 回回収する。
  - 以後は tick で定期回収する。
  旧線に残った IRR は、その線に登録があれば次の dispatch で全員 NONE を返し、`irq_unexpected` が 1 つ増える (1 回の移動につき高々 1)。登録が無ければマスクのまま残り、害は無い。
- エッジ/レベルの設定は変えない (ELCR は無い)。0x50〜0x5F は書かない。

### 3-4. 失敗したとき — 公開するのは成功した値だけ (R1-08)
- 自己照合の不一致・候補の不足は、計画の段で「全部固定」にして何も書かない。
- 失敗の種類は 2 つあり、区別して扱う。
  - **初回の書き込みの失敗**: 移す途中で読み戻しが合わない場合。新しい値はまだ公開していない。その group の 0x6x と、書いた 0x3C を BIOS の値に戻して読み戻す。移す group には登録が無い (§3-3) ので、結び直しは要らない。列挙表 (`pci_dev.irq`) と owner は元のまま保つ。他の group には影響しない。ログは `[pci-irq] keep firmware: <理由>`。
  - **移動成功後の復元**: 移した後の最初の attach が失敗した場合 (§3-3)。新しい線と owner = OS は既に公開している。0x6x と、その group の全機能の 0x3C を BIOS の値に書き戻して読み戻す。成功したら、**その group の全機能の公開線 (管理表と列挙表の `pci_dev.irq`) と owner も firmware の値へ戻す**。その後で、旧線で attach をやり直す。ログは `[pci-irq] group <n> restored to firmware: <理由>`。
- 戻しの読み戻しも合わなければ、ハードの状態が分からないとみなす。その group の機能の `pci_dev.irq` を 0xFF (未割当) にする。**線はマスクしない** (R2-05)。旧線・新線にいる他の登録者 (IRQ10 の CS4231、IRQ3 の LAN など) は、残っている登録に従って今までどおり動く。その group の機能の割込み源は止める。機能は settle 済みで静かなはずなので、加えて、PCI の Command の bit10 (Interrupt Disable) が書けて読み戻せる機能ではそれを立てる。そのドライバは IRQ が無いものとして tick だけで動くか、使えないと報告する (`irq.h` の契約)。ログは `[pci-irq] group <n> lost: restore failed`。
- **既定の段階** (R1-01): 実機で probe 試験 (§6) を通るまでは `pci_irq_route=plan` (計画を表示するだけで書かない) を既定にする。関門を通った線だけを機種表の `cand_mask` に入れ、既定を `on` にする。止め口は `pci_irq_route=0` (置き場は §7-3)。

### 3-5. 見える形
- 起動ログ: `[pci-irq] display 0:13.0 irq6 | usb 0:12.0 irq5 | lan 0:11.0 irq3 (shared: usb-ehci 0:12.2)`。group の詳細は KDEBUG のときだけ出す。
- `lspci`: 既存の irq・pin の列に「shared with <種類> <BDF>」を足す。KAPI は変えない (共有は `pci_get` の irq から計算する)。

### 3-6. PowerPC の backend — 口と手本の対応 (実装は os32powerpc)
| 口・仕事 | OS32 の今 | 手本 |
|---|---|---|
| (前提) 外部割込みの入口 0x500 → `platform_irq_claim`=IACK → dispatch → complete=EOI | 未対応 (`arch/ppc/exception.c:35-37`、DEC だけ) | NetBSD `sys/arch/powerpc/pic/intr.c`、FreeBSD `openpic.c` の openpic_dispatch・openpic_eoi |
| (b) mask・unmask・優先度・宛先・spurious | `include/platform_irq.h` の契約あり、線の数は仮 (`platform_irq_limits.h` 1/1) | NetBSD `pic/openpic_common.c`・`pic_distopenpic.c`・`pic_openpic.c:99`、FreeBSD openpic_enable・openpic_mask・openpic_config |
| (a) 早い段の読み取り: PCI ノードの `interrupts` (pin) → `interrupt-map` (+ mask・セル数) → 親 PIC と source・sense | `platform/powermac/ofw.h:9-27` (getprop・child・peer・parent)、OF を切り離す前だけ | FreeBSD `sys/dev/ofw/ofw_bus_subr.c` (ofw_bus_lookup_imap・ofw_bus_search_intrmap)、NetBSD `sys/arch/powerpc/pci/pci_machdep_ofw.c:118・221` |
| MPIC の発見 (macio の子、assigned-addresses + reg) | — | NetBSD `sys/arch/macppc/macppc/interrupts.c:73・91・138・168` |
| macio の子 (`AAPL,interrupts` を先に) | — (PCI の振り分けの対象外) | NetBSD `sys/arch/macppc/dev/obio.c:296` |
| `platform_pci_irq_table` | 新規: (a) の表から、source ごとに連番の group を作る (`native = source`、`line = source`)。各機能の `line` には OF で解いた source を入れる。`can_reroute = 0` | — |
| `platform_pci_irq_route` | 常に NOTSUP | — |
- 未確定 (os32powerpc の調べに任せる): interrupt-map-mask・複数段の nexus・極性と edge/level (`docs/ppc/powermac.md` の [未])。uPD720101 の 3 機能が 1 本の source か別々の source か (G4 の実機の OF の木で見る)。

## 4. 大きさ
- 共通: plan ~160 行、対応表 (`pci_irq_kinds.c`) ~50 行、つなぎ・settle・attach・結び直し・ログ ~160 行、契約 ~60 行。PC-98 の backend: ~230 行 (機種表・group ごとの戻しを含む)。`pci.c` への呼び出しは 1 行。カーネルは +2.5KB 前後。
- 82557 を口へ移す ~10 行、lspci ~40 行、番人 ~70 行、ホスト試験 ~300 行 (plan、モック backend 2 種) と ~250 行 (PC-98 の backend を偽の config I/O で動かす)。KAPI は変えない。
- G4 の backend (os32powerpc): 表 ~120 行。0x500 と MPIC は別の段 (分離の計画の側)。

## 5. ファイルと段 (1 ファイル = 1 段)
| 段 | ファイル | 中身 | 実装者 |
|---|---|---|---|
| 前提 | gf-bdf (`~/os32-tmp/run/gf-bdf/DESIGN.r2.md`) の着地 | GeForce の BDF を `pci_locate` から資源・検証・設定まで渡す。本書では重ねて設計しない。**PM の決定: gf-bdf の枝に、`hwdev/gf/irq.c:12・96・293` の `GF_IRQ_LINE 6U` をやめて、見つけた BDF の 0x3C の線で登録・解除する直しを PM が足す** | (gf-bdf の段、PM) |
| 1 | `include/pci_irq_platform.h` | §3-1 の契約 | sol |
| 2 | `drivers/pci/pci_irq_plan.c` (+`.h`) | §3-2 の純粋な関数 | terra |
| 2b | `drivers/pci/pci_irq_kinds.c` | 対応表 (重み・静止の時機、§3-1) | terra |
| 3 | `tools/tests/pci_irq_plan_host.c` | libc なし、モック backend 2 種 (振り直せる / 読むだけ。疎な source 20・25 の group を含む)、`make check-pci-irq-plan`、変異 | terra |
| 4 | `drivers/pci/pci_irq.c` | つなぎ・`pci_irq_settle`・`pci_irq_attach`・結び直し・ログ・成功後だけの `pci_dev.irq` 更新 | sol |
| 5 | `tools/check_pci_irq_portable.py` | 番人 (§3-0) | terra |
| 6 | `platform/pc98/pci_irq_pc98.c` | PC-98 の backend (機種表・機能の分類・自己照合・group ごとの書き込み・戻し) | sol |
| 6b | `tools/tests/pci_irq_pc98_host.c` | 段 6 を偽の config I/O で動かす。書き込みの位置ごとの失敗 (0x6x・各 0x3C・戻し)、0x3C が全部戻ること、列挙表との整合、固定の group が変わらないこと。`make check-pci-irq-pc98`、変異 | terra |
| 7 | `userland/shell/pci_verbose.c` | lspci の共有の表示 | terra |
| 8 | `drivers/net/e82557/e82557.c` | `irq_register` を `pci_irq_settle` + `pci_irq_attach` に替え、開始時に要因を 1 回回収する。ISR から呼ばれた (irq ≠ 0) ときの SWI を、tick の回収 (irq = 0) とは別に数える (`E82557_ST_SWI_ISR`) | terra |
| 8b | `drivers/net/e82557/lan_module.c` | 包みを `lan_module_pci_irq_attach/detach` に替える (R2-03)。参照の勘定・記録する線・`lan_detach` の解除先を機器単位の口に揃える | terra |
| 9 | `hwdev/gf/irq.c` | GeForce を機器単位の口 (`pci_irq_settle`・`pci_irq_attach`・`pci_irq_detach`) に移す (R2-02)。前提の段 (gf-bdf と PM の直し) の後に行う | terra |
| 10 | `kernel/irq/irq_math.c` (+ `irq.h` の読み口) | 線ごとの dispatch の計数 `ln->dispatch` を、登録者の数に関わらず増やす (R2-06。今の `shared_dispatch` は 2 人以上のときだけ増える、`irq_math.c:103`)。読み口は `irq_line_dispatch(irq)` | terra |
| P | `platform/powermac/pci_irq_ofw.c` | G4 の backend (§3-6)。前提は 0x500 と MPIC | os32powerpc |

## 6. 受入 → 試験
| 受入 | 試験 |
|---|---|
| 割り振りの全分岐 (専用・基板の共有・まとめ・固定・不足・同じなら書かない・照合の不一致・UNKNOWN の固定・保留から settle で移る・読むだけで警告だけ) | 段 3 のホスト試験と変異 |
| 共通の層が backend に依らない | 番人 (段 5)。モックの G4 型 (疎な source) で `route` が 1 回も呼ばれず、`pci_dev.irq` に OF の line が写ること |
| backend の部分書き込みと戻し | 段 6b。書き込みの位置ごとに失敗を注入し、戻した後の config・列挙表・owner・登録が元と一致すること。戻しも失敗したら、その group が 0xFF になり、他の group は変わらないこと |
| NP21/W で何も変わらない | 起動ログに `[pci-irq]` が出ない、kselftest が全数通る |
| 実機の R-1・R-4 (読むだけ) | 計画だけ表示する版で、構成 A・E の自己照合が通り、計画が §3-2 の例と一致する |
| **関門: 振り直しが効くこと (R1-01・R-2)** | `pci_irq_route=probe:<線>` で起動する。82557 の group (PIRQ3) を、settle の時点で BIOS の値 3 と違う候補の線 (5・6・8・15 のどれか。線ごとに 1 回) へ移し、その後 attach して動かす。SWI を出し、**新しい線の `irq_line_dispatch` が増え (単独の線でも増える、段 10)、`E82557_ST_SWI_ISR` が増え (tick の回収とは区別、段 8)、旧線 3 の `irq_line_dispatch` は増えない**こと。ping も通ること。通った線だけを `cand_mask` に入れ、既定を `on` にする |
| 凍結と順序 (R2-01) | ホスト試験 (段 3・6b): A′ で LAN が attach した後に EHCI が settle しても、route が呼ばれないこと。全機能が settle し、どれも attach していない group だけが移ること |
| 登録できない線へは移さない (R2-04) | 段 6b: 新線を隔離・排他・slot 満杯にした 3 つの場合で、移さずに固定になること。移した後に最初の attach が失敗したら、group が BIOS の線へ戻り、旧線で登録されること |
| 戻しの失敗で他を止めない (R2-05) | 段 6b: 戻しの読み戻しの失敗を注入し、その group だけが 0xFF になること。旧線・新線のマスクが、残った登録者 (CS4231・LAN の役の模擬) に従って開いたままであること |
| attach が今の線で登録される (R3-1、結合試験) | 段 4・8b のホスト試験: `pci_bind_all` と同じく、列挙表の**コピー**を probe へ渡す (モックで表そのものを渡さない)。settle で group を 3→5 に移した後、そのコピー (irq=3 のまま) で attach する。登録が 5 だけに作用し、detach も 5 だけを外し、3 の登録数・マスクが一度も変わらないこと |
| 移動成功後の復元が全部を戻す (R3-2) | 段 6b: 移した後の最初の attach の失敗を注入し、復元の後に、config (0x6x と全機能の 0x3C)・列挙表・管理表の線・owner・登録した線が、すべて firmware の値と一致すること。初回の書き込みの失敗の場合 (公開前) と、別の case として分けて確かめる |
| モジュールの解除が移した線で外れる (R2-03) | 段 8b のホスト試験と実機: probe で線を移した後に LAN モジュールをアンロードし、新しい線の登録が 0 に戻ること、`MODULE_REF_IRQ` が解放されること、旧線には何も触らないこと |
| GeForce が見つけた線で登録される (R2-02) | 実機 E′ (GeForce 0:12.0、PIRQ0 = IRQ5): GPU が IRQ5 で登録され (起動ログ `[gf1] PCI IRQ=5 register(5)=0`)、VBLANK の計数が IRQ5 の `irq_line_dispatch` と一緒に増え、IRQ6 には登録されないこと |
| 実機の書き込み (構成 A、USB の着地の後) | EHCI の settle の後に 0x62 ≠ 0x0a、USB の 0x3C が新しい線になっていること。CS4231 を鳴らしても IRQ10 に PCI 由来が乗らないこと。`irq_unexpected` の増えが移動の回数以下であること |
| 実機で届くこと | 82557 の SWI、GeForce の VBLANK の計数、`irq_storm_masked` = 0 |
| LGY-98 との共有 (板があるとき) | LGY-98 を PCI と同じ線 (3・5・6) に置き、82557 と LGY-98 で同時に通信する。両方の要因の判定と tick の回収で取りこぼしが無いこと。Ra266 は今 LGY-98 を持たない (RA266 §5) ので、板が手に入ったときに採る |
| 失敗したら firmware に戻る | 試験の旗で読み戻しを不一致にし、`pcidump` で 0x60〜0x63 と 0x3C が BIOS の値に戻っているのを見る |
| G4 | os32powerpc の回 (段 P の後)。OF の木から作った表と lspci の一覧が一致し、`route` が呼ばれないこと |

### 6-1. 組み合わせの試験 (ユーザー方針「実装検証段階では色々なデバイスで組み合わせを試しましょう」)
内蔵の Trident (0:8.0)・CS4231 (IRQ10)・82557 (0:11.0) は常設。抜き差しはユーザー、採取は実機ノートが行う。PIRQ は §1-1 の推定 (R-4 で検算する)。

| 構成 | 0:12.0 | 0:13.0 | 基板の共有 (OS で解けない) | 振り直しで期待すること |
|---|---|---|---|---|
| E (今) | HighPoint | GeForce | なし | HighPoint の PIRQ0 は固定。他は BIOS と同じなので書かない |
| E' (逆) | GeForce | HighPoint | なし | HighPoint の PIRQ1 は固定 (GeForce を 0:12.0 に置くのは gf-bdf の着地の後) |
| A | USB | GeForce | PIRQ1 = OHCI#2 + GeForce | PIRQ2 (EHCI) を IRQ10 から外す |
| A' (逆) | GeForce | USB | PIRQ3 = EHCI + LAN | 残りを別々の線に。足りなければ EHCI と LAN をまとめる (gf-bdf の着地の後) |
| U+H | USB | HighPoint | PIRQ1 = OHCI#2 + HighPoint | PIRQ1 は固定、残りを振る |
| H+U | HighPoint | USB | PIRQ3 = EHCI + LAN | PIRQ0 は固定 |
| 片側 | 1 枚 | 空き | 構成による | 空きの PIRQ は切り離しのまま |

各構成で見る物:
1. 起動ログの `[pci]`・`[pci-irq]` (自己照合、書いた・書かない・戻した理由)。
2. `lspci` の一覧、`pcidump 0 6 0` の 0x60〜0x63、各機能の 0x3C。計画と一致すること。
3. 共有の有無: 音の線に PCI が乗らないこと。候補が足りた場合は、基板の共有の他に共有が無いこと。
4. 各機器の動作: LAN の往復、CS4231 の再生、GeForce の VBLANK、USB の入力 (段 1 以降)。`irq_unexpected`・`irq_storm_masked`・`irq_line_quarantined` が増えないこと。
5. `pci_irq_route=0` で同じ構成を起動し、動作に退行が無いこと。

## 7. 未決 (ユーザー判断)
1. **候補の線をどう広げるか** — 推奨: BIOS が実際に使った {3,5,6} で始め、IRQ 8・15 等は R-3 で届くのを確かめてから機種表に足す。
2. **カードの挿し場所** (物理の作業) — 推奨: USB を 0:13.0、GeForce を 0:12.0 に挿す。GeForce が基板で誰とも共有しなくなる。EHCI は LAN と基板で共有するが、段 1 では静止している。
3. **止め口の置き場** — 推奨: 既存の起動設定に `pci_irq_route=0` を 1 行足す。
4. **G4 で種類の重みを MPIC の優先度に使うか** — G4 は経路を OS が選べない代わりに、source ごとの優先度を OS が決められる。推奨: 共通の層は計画の中に「種類の重み」まで出し、それを MPIC の優先度に写すかどうかは G4 の受入 (段 P の後) で決める。初版はすべて同じ優先度にする。

## 8. gf-bdf への申し送り
閉じた (r4)。PM の決定で、GeForce の IRQ の線の直しは PM が gf-bdf の枝に足す (§5 の前提の行)。

## 反論
なし。

## 確定 (2026-11-09、astra の第 3 回、PM 決定で反映)
- P1: attach は、引数のコピーの IRQ ではなく、BDF で管理表の今の線を引いて登録する。解除用に、実際に登録した線を保持する (`pci_bind.c:51・62` のローカルコピーが旧線のまま残る件、§3-1)。
- P2: 初回の書き込みの失敗と、移動成功後の復元を区別した。復元に成功したら、全機能の公開線と owner も firmware の値へ戻す (§3-3・§3-4)。
- 試験: 実際と同じコピーを probe へ渡し、settle で 3→5 へ移して、登録・解除が 5 だけに作用することを確かめる結合試験を足した (§6)。
- 試験: 復元の後に、config・列挙表・owner・登録線が一致することまで確かめる復元試験を足した (§6)。
