# DEP_DIR_B — 割込み・時刻・reset (PIC・PIT・sysclk・reset/beep) を platform へ — 設計

> 状態: **現行 (2026-10-27)** — 確定 (astra 起草・Opus 2 往復、ユーザー決定: 群 B を先に)。段の状態は票 TASK_DEP_DIR が持つ。
> 出所: ~/os32-tmp/run/depdir-b/DESIGN.final.md を 2026-10-27 に写した。冒頭の『r1→r2 の変更』は往復の記録として残す。


## r1→r2 の変更

- B1: serial-portc の buzzer 切り出し・変異先を system.c へ追従。
- B2: fdc-track の pic_eoi 切り出し元を pic.c へ追従。
- B3: v86-low に clock.c を組み、原本低位メモリの CHECK を維持。
- B4: 選択規則を check_map.d の対応表に訂正し、build/kernel.mk は broad。
- B5: 変異用コピーから新ヘッダ・platform 実装が見えることを完了条件化。
- B6: 基点列は一度採取して tools/tests/ に固定し、実行時 git 不要。
- B7: s_pit の所有者を kernel/pit.c に統一し pit_init の断片を追加。
- B8: kernel/sys.c を参照する既存試験全件を MUTATE=0 で確認。
- B9: L12 H は irq_finish 経路に限定し、IRQ0/7 ASM は N/R で維持確認。
- B10: 残存する直の PIC/PIT I/O を §1 の表に明記。
- B11: R の PIC 稼働診断のシリアル記録名と合否条件を明記。
- PM: Ra266 立会い枠を PM 決定に移し、ユーザー判断を0件とする。

- 基点: `/home/hight/os32-v3` main `12b7e2fe`。2026-10-27、設計のみ。実装・host 実行・ゲスト・実機試験は未実施。
- 正典: `docs/tasks/v3/TASK_DEP_DIR.md` §2、同梱 `PLAN.final.md` §2 L11–L13・§3、`SURVEY.md` #49–51。
- PC-9821、単一利用者・単一タスク・協調切替。目的は固まらない・壊さない・戻せること。整数演算、既存 I/O と待ちを維持。
- KAPI 番号・署名・呼出規約、部品 ABI、配置世代を変更しない。CLAUDE.md [C1]–[D4] を適用。新規ハード制御方式は導入しない。

## 1. 実物の確認

表の file:line は上記 main。SURVEY の記述と実物との食い違いは **0 件**。以下は境界を決めるための補足。

|調べ|現在の実物|確認結果／設計への反映|
|---|---|---|
|#50 reset/beep|`kernel/sys.c:19,20,21,38,43`|記載と一致。reset は SHUT0→SHUT1→RESET、その後 `:23` の無限 halt。beep は BSR 1 書込みずつ|
|#49 PIC 初期化・EOI|`kernel/idt.c:189,192,301`|記載と一致。初期化12書込み、通常 slave EOI は slave→master|
|#49 IRQ 状態|`kernel/irq.c:103–116`|`:106` は slave **ISR** 読出し、`:108` は IRR モード復帰。IRQ15 のスプリアス判定に使用|
|#49 mask|`kernel/idt.c:261,279`|mask の実装本体はこちら。`irq.c` は登録・隔離に応じて呼ぶ側。IMR の RMW を irq_save/restore で保護|
|#49 PIT・ktime|`kernel/idt.c:226,233,249–253`、`kernel/ktime.c:84–98`|PIT 設定状態を公開してから mode→LSB→MSB。時刻は IRR→latch/lo/hi→IRR→tick|
|#51 sysclk|`kernel/sysclk.c:16–18,27–30`|記載と一致。BIOS work bit7、1回だけ検出。未判定既定は 1996800Hz、is_8mhz=0|
|残存する直の PIC/PIT I/O|`kernel/isr_stub.asm:507–508,607–632` の EOI/ISR、`kdebug/recover_profile.c:108–109` の PIT latch/read、`kdebug/kselftest.c:1804` の IMR 読み|今回移さず維持。#49 全域の解消ではない|
|既設 platform|`platform/pc98/boot.c:256`、`d2_bind.c:61–65`|boot/boot_disk/boot_names/pci_resources/d2_bind と platform_io.h/tick_order.h。reset/PIC/PIT/基準 clock 実装は未移動|

- `PLAN.final.md` の基点は `033ec2b8`、SURVEY は `d0f5b0e8`。古い行番号を置換指示に使わず上表の関数単位で切る。
- D1 §2: `tick_register`、6枠、IRQ0/IF=0、SND→PCM→NE2K→E82557→LINK→LGY98 を維持。`kernel/isr_handlers.c:540` の dispatch は変更しない。
- D3 §3: `boot_platform_register(const struct boot_platform *)` と `step[BOOT_POINT_COUNT]` は既存のまま。
  `kernel/kernel.c:117` の bind、`:176` PIC→`:182` sysclk→BOOT_DMA→`:187` PIT の呼出位置、IF、ログを固定する。
- 指定範囲外の V86 用 PIC 定義や ASM の固定経路はこの段で再設計しない。#49 全域解消を過大に主張しない。

## 2. 共通境界 — リンク時固定の小さい口

- カーネル→PC-98 の具体 I/O だけを切る。platform→kernel の irq_save/restore、pit_compute 等は普通の依存として許す。
- 早期起動・IRQ 内でも必要な口なので登録待ちを作らない。下記ヘッダの外部関数を PC-98 実装1個にリンクする。
- 新しい ops 構造体・登録関数は **0**。既存公開名は残す。新規名は内部専用で KAPI に公開しない。
- 起動操作を BOOT_DMA 等に束ねない。既存 boot 登録一覧と呼出位置をそのまま使い、新しい初期化フェーズを足さない。
- IRQ 表 `irq_register/irq_unregister`、登録数、共有配送、storm/quarantine、EOI の判断は kernel 所有。
- ハード定数は既存 pc98.h/idt.h の名前を使う。互換定義の一括移動は行わず、今回追加する値に裸の数値を散らさない。

## 3. L11 — reset / beep

```c
/* 新規 kernel/platform_sys.h: 内部、リンク時固定、状態なし */
void platform_reset(void);       /* reset I/O の3書込みだけ、戻る場合あり */
void platform_buzzer_on(void);   /* BSR_BUZ_ON の1書込み */
void platform_buzzer_off(void);  /* BSR_BUZ_OFF の1書込み */
/* kernel/sys.c: 既存公開名・halt の位置を維持 */
void sys_reboot(void) {
    platform_reset();
    for (;;) { _halt(); }
}
void buz_on(void)  { platform_buzzer_on(); }
void buz_off(void) { platform_buzzer_off(); }
/* platform/pc98/system.c */
void platform_reset(void) {
    outp(SYSPORT_C_BSR, BSR_SHUT0_SET);
    outp(SYSPORT_C_BSR, BSR_SHUT1_SET);
    outp(CPU_RESET_PORT, 0x00); /* 現行値を定数化する場合も値は固定 */
}
```

- 呼出文脈・IF は呼出元のまま。reset/beep に CLI/STI、待ち、再試行、tick 依存、PIT 再設定を加えない。`sys_halt` は変更しない。
- ブザーの極性は現行の BSR_BUZ_ON/OFF に従う。時長・周波数をこの口に持たせない。beep の状態変数も増やさない。
- 危険: 再起動不能・実機の極性差。host では reset の3書込み後に halt shim で脱出し、製品の無限 halt は残す。

## 4. L12 — PIC の操作と IRQ の判断を分ける

```c
/* 既存 idt.h の宣言・公開名は維持。定義を platform/pc98/pic.c へ移す */
void pic_init(void);
void irq_enable(unsigned int irq);
void irq_disable(unsigned int irq);
void pic_eoi(unsigned int irq);
/* 新規 kernel/platform_irq.h: kernel が必要とする状態/操作だけ */
int platform_irq_slave_last_in_service(void); /* ISR bit7、読後は IRR に戻す */
void platform_irq_eoi_master(void);           /* master だけ EOI */
int platform_irq_timer_pending(void);         /* master IRR bit0、L13 が使用 */
/* kernel/irq.c: irq_finish の既存 plan と報告条件を維持 */
if (irq == PIC_SLAVE_LAST_IRQ)
    bit7 = platform_irq_slave_last_in_service();
plan = irq_eoi_plan(irq, bit7);
if (plan == IRQ_EOI_SLAVE_MASTER) pic_eoi(irq);
else if (plan == IRQ_EOI_MASTER) platform_irq_eoi_master();
/* IRQ 表・件数・隔離・試験状態は kernel/irq.c に残す。 */
```

|操作|固定する I/O と文脈|
|---|---|
|pic_init|前景・起動時 IF=0。master ICW1→offset→ICW3→ICW4、slave 同4件、master mask FF→slave mask FF→master IRR→slave IRR。合計12書込み|
|irq_enable/disable|前景／IRQ 両方。port 選択→irq_save→対象 IMR read→bit clear/set→write→irq_restore。既存範囲・カスケードの扱いを変えない|
|pic_eoi|IRQ 内。irq<8 は master のみ、irq>=8 は slave→master。追加の読み・待ちなし|
|slave_last_in_service|IRQ15 の finish 内 IF=0。slave OCW3_ISR write→ISR read→OCW3_IRR write→bit7 を boolean 化|
|eoi_master|IRQ 内。master OCW2_EOI の1書込み。IRQ15 スプリアスで slave へ送らない|
|timer_pending|L13 の IF=0 区間で master OCW3_IRR write→read→bit0 を boolean 化。L12 は口を単体試験するだけ|

- PIC 実装はソフト mask の写しを作らない。現物 IMR を毎回読む。IRQ 番号からの plan、登録数と隔離の判定を platform に渡さない。
- スプリアス処理は現行 `irq_eoi_plan` のまま。IRQ7/15 の新たな判定、EOI 統合、I/O wait の追加はしない。
- 危険: マスク RMW の分断で固まる、EOI の順で割込み停止、初期化順で起動不能。R の確認を L13 取り込みの前提にする。

## 5. L13 — sysclk → PIT → ktime のハード読み出し

```c
/* 新規 kernel/platform_timer.h: 一時値、継続状態ではない */
struct platform_clock_value { unsigned long hz; u8 is_8mhz; };
struct platform_timer_sample { unsigned int count; int p1, p2; };
void platform_clock_read(struct platform_clock_value *out);
void platform_timer_program(unsigned int reload); /* mode2→LSB→MSB */
void platform_timer_sample(struct platform_timer_sample *out); /* 呼出側 IF=0 */
/* kernel/sysclk.c: s_hz/s_is_8mhz/s_detected は従来の所有者に残す */
void sysclk_detect(void) {
    struct platform_clock_value v;
    if (s_detected) return;
    platform_clock_read(&v);
    s_is_8mhz = v.is_8mhz; s_hz = v.hz; s_detected = 1;
}
/* kernel/pit.c: s_pit を所有し、状態公開後にハードへ渡す */
int pit_init(unsigned int hz) {
    struct pit_setup s;
    int rc = pit_compute(sysclk_hz(), hz, &s);
    if (rc != 0 && pit_compute(sysclk_hz(), (unsigned int)PIT_HZ, &s) != 0)
        return rc; /* 両失敗なら状態/I/O 不変 */
    s.mode = (u8)PIT_MODE_TIMER0;
    s_pit = s;
    platform_timer_program(s.reload);
    return rc; /* fallback 成功時も元の rc */
}
/* platform/pc98/timer.c: sample 内に tick や補間状態は置かない */
void platform_timer_sample(struct platform_timer_sample *out) {
    u8 lo, hi;
    out->p1 = platform_irq_timer_pending();
    outp(PIT_MODE, PIT_LATCH_TIMER0);
    lo = (u8)inp(PIT_CNTR0); hi = (u8)inp(PIT_CNTR0);
    out->count = ((unsigned int)hi << 8) | lo;
    out->p2 = platform_irq_timer_pending();
}
/* kernel/ktime.c: irq_save 済みの既存 retry loop 内 */
platform_timer_sample(&hw);
p1 = hw.p1; count = hw.count; p2 = hw.p2;
t = (unsigned int)tick_count;  /* 必ず sample 後、feed 注入前 */
/* 以後 feed→time_decide→time_us_from→clamp→irq_restore→出力をそのまま残す */
```

|所有者／文脈|移動・維持する中身|
|---|---|
|platform/pc98/clock.c、起動前景|`v86_bios_real_lowmem()[BIOS_WORK_SYSCLK]` を1回読む。bit7=1:1996800Hz/8MHz系、0:2457600Hz。既存低位メモリ変換経路を維持|
|kernel/sysclk.c|未判定既定1996800Hz/is_8mhz=0/detected=0、検出済み判定、3 getter を維持。RTC の clock 登録表とは無関係|
|kernel/pit.c（idt.c から論理部移動）|`s_pit`、pit_get_setup、pit_init を所有。pit_compute と既定 PIT_HZ 再計算、元の rc、両方失敗時は状態/I/O 不変|
|platform/pc98/timer.c、起動前景 IF=0|kernel/pit.c が状態公開後に呼ぶ program(reload) の実体。mode→LSB→MSB だけ、s_pit を持たず再計算・待ちを追加しない|
|kernel/pit_math.*|構造体の型・配置・mode 値・純粋整数演算をそのまま維持。100Hz: reload 19968/24576、period_us=10000|
|kernel/ktime.c、呼出元 IF を保存|ps の valid/mode/reload 検査、retry 上限 TIME_RETRY_MAX、feed、補間、s_last_us と clamp_count を所有。sample は IRQ 内でも待たない|

- sample の I/O は **W IRR/R pending → W latch/R lo/R hi → W IRR/R pending** の7操作。割込み禁止区間を短縮・分割しない。
- p1/p2 不一致の再試行、上限時 AGAIN、未設定時 NOSYS、失敗時出力不変、NULL 出力、feed 中 clamp 無効、成功時の単調性を維持。
- tick 読出しは sample 後。ASM の tick 更新・EOI・V86 timeout/反射・IRQ 深さ、R1 fixture→tick_dispatch の順は対象外として保存。
- 危険: clock の逆選択で実機 tick が約123Hz化、latch/IRR 順で時刻の飛び・停止、無限 retry で固まる。採用する上限や待ちを変更しない。

## 6. 大きさの見積もりと上限

|段|リンク後カーネルの増減見積もり（B）|根拠|
|---|---:|---|
|L11|0～+96|3薄い呼出し、I/O 本体移動。静的状態+0|
|L12|−64～+192|PIC 本体の移動、ISR/IRR と master EOI の関数境界。静的状態+0|
|L13|−64～+320|PIT 論理部の移動、clock/sample/program 呼出し。一時構造体は i386 で8/12B、既存静的状態の複製なし|
|計|−128～+608（評価予算+1024）|関数配置・インライン化差を含む予測。実測値ではなく、+1024超なら境界の重複を調べる|

- kernel/ 配下の減少と platform/ 配下の増加は同じ常駐像に入るため相殺。移動行数を容量削減と数えない。
- 既存 build/out は kernel.bin 約358.4KiB、vmkernel.lz4 約218.7KiB、kdebug.bin 約118.3KiB（来歴未検証の参考値）。基点の再ビルド値で余裕を確定する。
- 常駐の上限は `include/memmap.h:230` MEM_KERNEL_IMAGE_MAX、resident end の既存 link ASSERT で判定。bin サイズだけでは BSS/align の上限を判断しない。
- 起動 FD は両形式とも BOOT+MINIMAL の集合（build/image.mk:6–10）。増分は圧縮・クラスタ丸めに依存し、+608Bをそのまま空き容量から引かない。
- FD 両形式・MINIMAL を生成して使用量/空きを基点比で記録する。容量上限の引上げ、収録物削除、メモリ配分変更で通さない。
- kdebug は code/import を増やさないので本体の増分は原則0B。ペア情報は再生成する。image/memory 上限262144B、load span524288B（tools/module_schema.py:55–56）を既存検査で確認。
- kernel/vmkernel/kdebug の組を揃え、make all のリンク・pack 検査と check-packages-host を通す。上限非接触の見込みだが実装前の設計では合格を断言しない。

## 7. ファイルごとの変更一覧

行数は追加・移動・削除を含む概算。レビュー指摘の切り出し・変異・リンク箇所を確認し、既存試験の残りの本文改修量は実装時に該当 assertion を絞って確定する。

|段|ファイル（リポジトリ相対）|種別／おおよその行数・内容|
|---|---|---|
|L11|kernel/platform_sys.h / platform/pc98/system.c|新規 各15 / 30、内部口と3操作|
|L11|kernel/sys.c|変更15、I/O を転送、halt 保存|
|L12|kernel/platform_irq.h / platform/pc98/pic.c|新規 各20 / 130、状態読出しと既存PIC操作|
|L12|kernel/idt.c / kernel/irq.c|変更 各100 / 15、PIC 本体移動、finish の判断は維持|
|L13|kernel/platform_timer.h|新規30、2一時構造体と3関数|
|L13|platform/pc98/clock.c / platform/pc98/timer.c|新規 各25 / 35、BIOS選択とPIT操作|
|L13|kernel/pit.c / kernel/idt.c|新規55 / 変更45、設定状態・fallbackを移動|
|L13|kernel/sysclk.c / kernel/ktime.c|変更 各15 / 25、ハード操作だけ置換|
|L13|kernel/idt.h / kernel/sysclk.h|変更 各5、実装所在地の注記。宣言・定数の互換維持|
|各段|build/kernel.mk|変更 計10、platform 新規Cとpit.cを常駐・fixture双方へ|
|各段|tools/tests/dep_dir_b_io_host.c / test_dep_dir_b_io.py|新規 各180 / 120、順序記録・基点比較・段別変異|
|各段|build/checks.d/check-dep-dir-b-host.mk / tools/check_map.d/*.yaml|新規20 / 変更各数行目安、下表の選択規則を登録。check_select.py 本体は変更不要|
|L11|tools/tests/test_serial_portc.py|変更20目安、platform_buzzer_on/off の切り出し元と変異先を system.c へ移し、呼出名も整合|
|L12|tools/tests/test_fdc_track.py|変更5目安、pic_eoi の切り出し元を platform/pc98/pic.c へ|
|L13|tools/tests/v86_low_host.c / test_v86_low.py|変更各10目安、clock.c の組込みと paths 追加、原本 bit7 の CHECK を維持|
|L12/L13|tools/tests/test_pit_clock.py / test_time_math.py / test_irq_math.py / test_dep_dir_d3_boot.py|変更 各0～20、実装パスを前提にする検査のみ追従、既存期待値は維持|

選択規則は `tools/check_map.d/` の以下の YAML に追加（既存入力は保持）。

|YAML|追加する入力|
|---|---|
|check-serial-portc-host.yaml|kernel/platform_sys.h、platform/pc98/system.c|
|check-fdc-track-host.yaml|kernel/platform_irq.h、platform/pc98/pic.c|
|check-pit-clock-host.yaml|kernel/pit.c、kernel/platform_timer.h、kernel/platform_irq.h、platform/pc98/{clock,timer,pic}.c|
|check-time-math-host.yaml|kernel/pit.c、kernel/platform_timer.h、kernel/platform_irq.h、platform/pc98/{clock,timer,pic}.c|
|check-irq-math-host.yaml|kernel/platform_irq.h、platform/pc98/pic.c|
|check-v86-low-host.yaml|kernel/platform_timer.h、platform/pc98/clock.c|
|check-dep-dir-b-host.yaml（新設）|kernel/{sys,idt,irq,pit,sysclk,ktime}.c、kernel/platform_{sys,irq,timer}.h、platform/pc98/{system,pic,clock,timer}.c、使用する既存ヘッダ・shim・runner・固定fixture・自身のYAML・mk|

- `build/kernel.mk` は broad として全関連試験を選択させる。各試験の runner/host と新規入力を lint で検査する。
- kernel/pit_math.*、boot.c、tick.c、ASM、sdk/kapi.json、生成 KAPI、kdebug_imports.def は変更不要。

## 8. 受入・変異・利用者の退行・段分け

基点12b7e2feを同じ shim へリンクして列を一度だけ採取し、入力 read 列・採取手順・基点SHAとともに tools/tests/ の差分レビュー可能な fixture に固定する。
実行時は変更後だけをリンクし、固定fixtureの (R/W,port,value)＋IF save/restore＋halt/tick読出し位置と比較する。git を引かず、作業木・浅いcloneでも動く。余った入力・過剰な読みも失敗とし、変更後から期待値を自動更新しない。
変異は1行ずつ適用し当該試験が落ちることを確認、戻して通常成功を確認する。ゲスト/実機に変異版を配備しない。

|受入項目|host（H）／NP21/W（N）／Ra266（R）試験|1行変異|
|---|---|---|
|L11 reset順・戻らない経路|H:3書込みとその後halt、N:再起動→起動完了、R:serial再接続/起動ログで判定、画面目視は次の立会いで補完|SHUT0/SHUT1 の2呼出しを1行で逆順にする|
|L11 beep極性・停止|H:on/offの各BSR値、N:鳴動/停止、R:目視では判定不可なので聴取でON/OFF確認|on の値を BSR_BUZ_OFF にする|
|L12 PIC初期化|H:12書込み列完全一致、N:FD/HDD起動、R:serial起動完了と下記 DEPDIR-B-PIC 記録|ICW3_MASTER を ICW3_SLAVE にする|
|L12 mask原子性|H:master/slave・IF=0/1でRMWと元IF復帰、N/R:FD/LAN/serial/入力IRQの進行|irq_restore(flags) を削除|
|L12 EOI/スプリアス|H:irq_finish 経路の IRQ8/15 等、slave ISR bit7=0/1、handled=0/1、IRR復帰とEOI列。IRQ0 EOI・IRQ7スプリアスのASMは維持し N/R で割込み継続を確認|IRR復帰書込みを削除|
|L12 隔離と登録数|H:既存irq_math/dispatchの登録・解除・隔離試験、N/R:通信と入力が継続|irq_finish の master-only を pic_eoi(irq) に変更|
|L13 clock|H:bit7両分岐・未検出値・再検出なし、N:設定値、R:serialで2457600Hz/非8MHz系を確認|bit7 の三項演算子の周波数を逆にする|
|L13 PIT計算/設定|H:両clock、正常100Hz・不正hz fallback・両失敗、状態公開とI/O列、N/R:tickと経過時間|PIT下位/上位の書込みを1行で逆順にする|
|L13 読出し順|H:IRR前後・latch lo/hi・tick・feed位置、N/R:時刻取得が戻る|p2 を latch 前で読む位置に移す|
|L13 補間/上限|H:pending遷移・count境界・retry上限・NOSYS/AGAIN出力不変・単調性・feed解除、N/R:連続時刻とtick|attempt < TIME_RETRY_MAX を <= にする|
|D1/D3維持|H:tick6枠/fixtureとbootの順、N/R:音・通信・入力と時刻の同時進行|sample後のtick取得をsample前へ移す|
|ABI/容量|H:公開symbol/署名と既存pack/link/FD検査、N:対応kdebugロードと既存時計selftest|試験専用上限を生成物サイズ−1にする|

- N は **各段** でFD両形式/HDD起動・LAN送受信・シリアル往復・キー/マウス入力・音開始/停止・時刻進行を基点比で記録。NP21/Wだけで実機合格にしない。
- R の serial 判定: 再起動前後ログ、PIC/PIT設定・tick増分・時計値、FD読出し/LAN/serialの進行。入力の物理操作は別枠。起動不能なら外部timeoutで止め、製品側に待ち変更を足さない。
- R の立会い枠は PM 決定: reset・L12・L13 のシリアル判定は立会い無しで実施可。beep聴取・画面目視は次の立会いのついでに実施し、未実施項目は保留として記録。必要な媒体/入力操作も PM が調整。既存 DEPDIR-D1-FD と今回の検証を区別する。
- R の PIC 稼働診断はシリアル採取記録 `DEPDIR-B-PIC` と名付け、同一起動の時点別 tick 値、FD読出し完了、LAN送受信結果、serial往復応答を保存する。tick 増加と各IRQ利用処理の完了を基点と比較して合格とする（新しい製品ログを追加する意味ではない）。
- I/O順固定の既存候補（名前のみ確認）: test_pit_clock.py、pit_clock_host.c、pit_clock_tdd.md、test_irq_math.py、irq_math_host.c、irq_math_tdd.md、test_time_math.py、time_math_tdd.md。純粋演算の試験だけでI/O一致を済ませず新shimで補う。
- 既存利用者: e82557_drv_host.c、fdc_track_host.c、kbd_dlog_host.c、lan_module_host.c、double_fault_host.c、v86_low_host.c の irq/pit 差し替え定義は旧名を維持して二重リンクを防ぐ。
- test_pit_clock.py 等の idt.c/sysclk.c 本文照合は移動先を追加し、弱い文字列検査への置換で合格させない。time_test_feed・irq_test_quiet・R1 fixture・kdebug時計selftestの口は維持。
- sysclk getter を使うserial/CPU校正、pit_get_setupの診断も値・寿命不変。host runner のソース一覧、変異先パス、tools/check_map.d/*.yaml の選択規則を移動と同じ段で更新する。
- B1/B2: serial-portc は system.c の実体を切り出し、BSR_BUZ_OFF→BSR_BUZ_ON の変異もそこへ移す。fdc-track は pic.c から pic_eoi を切り出す。旧ファイルの転送関数を切り出して合格にしない。
- B3: v86-low は sysclk.c と clock.c を組み、test_v86_low.py の paths に clock.c を追加する。`clock boot original` CHECK は残し、原本低位メモリの bit7 を読むことを引き続き検証する。
- B5: 新試験の通常・変異用コピー双方から kernel/platform_{sys,irq,timer}.h と必要な platform/pc98/*.c が参照・リンクできることを完了条件とする。コピー対象と include パスを更新し、依存欠落によるコンパイル/リンク失敗を変異検出に数えない。
- B8: `grep -rln 'kernel/sys.c' tools/tests/` の全件に対応する試験を MUTATE=0 で実行する。test_pgalloc_model.py・test_memory_boot.py・test_device_reservation.py を含め、転送先のリンクまたは目的に即した stub を整備し既存の検査を維持する。

|段（Codex実装＋関連host、各2時間以内）|完了点／並列可否|
|---|---|
|L11: 約1時間|sys転送＋shim reset/beep＋host。L12と独立に並行可、build/kernel.mk の統合だけ直列|
|L12: 最大2時間|PIC移動＋finish＋shim全PICケース＋既存irq回帰。NとRを確認後にL13取り込み可|
|L13: 最大2時間|前半sysclk移動→後半PIT/ktime移動→shim境界/既存time回帰。共通shimを再利用。L12 R待ち中は独立host準備まで|

- 各段 make all、追加/変更試験 MUTATE=0、check_select.py --lint、取り込み時 check-changed。節目Bで全体検査とR reset/clock/PIC/PITを記録。
- ゲスト/実機待ち・立会いは実装2時間に含めない。統合基点を1つにしてN受入。2時間を越える見込みならL13をsysclk完了点で分け、未検証PITを混ぜない。
- 各段を独立差分として戻せるよう保持。基点・画像・kernel/kdebugの組を保存。NP21/W停止→配備→起動、不可逆媒体変更は別承認、Ra266 HDD書込み試験は行わない。

## 9. PM 決定（ユーザー判断 0 件）

- **PM 決定: Ra266 の立会い枠。** reset・L12・L13 はシリアルで判定できる分を立会い無しで進める。beep聴取・画面目視は次の立会いのついでとし、物理操作の日程は PM が調整する。L13取り込み前のL12 Rゲートは省略しない。
- 関数境界・登録方式・整数式・世代維持は本設計で確定。測定値や未実施試験は実装者の確認事項であり、ユーザーへの設計判断に数えない。

反論: 0件（B1〜B11を採用）。

## 確定 (2026-10-27、Opus 5.5 の第 2 回で承認)

- r1 の B1〜B11 は r2 で直った (REVIEW.r1.md)。B11 の残り: DEPDIR-B-PIC の時点ごとの tick の値はシェルの `tick` (userland/shell/cmd_base.c:245) で取る。
- N1 (P3): check-time-math-host.yaml に platform/pc98/clock.c を足すのは選ぶ範囲が広いだけで、そのままでよい (PM)。
- 未決 0 件。Ra266 の確認は PM 決定: reset・L12・L13 はシリアルで判定 (立会い無しで可)、beep の聴取・目視は次の立会いのついで。L13 の取り込みの前に L12 の Ra266 の確認を置く。
