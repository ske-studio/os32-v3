# DEP_DIR_D1 — 周期処理の登録表と FDC の IRQ の所有 — 設計

> 状態: **現行 (2026-10-26)** — 確定 (astra 起草・Opus 1 往復・PM が F1〜F8 と契約の例外を反映)。段の状態は票 TASK_DEP_DIR が持つ。
> 出所: ~/os32-tmp/run/depdir-d1/DESIGN.final.md を 2026-10-26 に写した。末尾の「確定」が本文の該当の行より優先する。


## 1. 実物の確認
- 基準: `/home/hight/os32-v3` の main `4256d6c2`。SURVEY.md は `d0f5b0e8`。実装・ビルド・ゲスト試験は未実施。
- **実物との食い違い: 0 件**。#16 の `arch/x86/kernel/isr_handlers.c:547,553,554` と #17 の `:561` は行番号・内容とも一致。下表は設計に必要な補足で、食い違いには数えない。
| 確認箇所（リポジトリ相対 file:line） | 現状と設計への影響 |
|---|---|
| `arch/x86/kernel/isr_handlers.c:540–554` | fixture → snd → pcm → ne2k → e82557 → link → lgy98。6 本すべてを移す |
| `arch/x86/kernel/isr_stub.asm:639–658`、`arch/x86/kernel/idt.c:144` | IRQ11 専用入口。EOI は slave → master、V86 反射なし |
| `kernel/irq/irq.h:7–20`、`kernel/irq/irq_math.h:29`、`kernel/irq/irq_math.c:18` | IRQ11 は登録対象外。対象表・IDT も変更必須。全登録者に tick 回収を要求する現契約は FDC にそのまま適用できない |
| `kernel/irq/irq_math.c:103–131` | 登録ハンドラは最大2巡。FDC の同じフラグへの再代入は冪等 |
| `drivers/block/fdc.c:27,264–273,1133–1189`、`init/kernel.c:313–316` | フラグはドライバ所有、待ちは期限付き。現在は kernel が IRQ11 を開けてから割込み許可・初期化 |
| `kernel/{fm,pcm,lan}_module.c:23,20,44` | 3 façade は常駐。tick_target とモジュール参照の取得・解除を既に所有 |
| `kernel/v86/v86_bios.c:178,649,811` | V86 の FD は loop イメージ経由。物理 FDC IRQ をゲストへ反射する経路ではない |

## 2. 周期表の境界・順序
- 新しいスケジューラは作らず、IRQ 登録と同様の小さい固定表を常駐に置く。動的確保・時刻判定・KAPI 追加なし。
- 6 枠を昇順に走査する。空枠は飛ばす。登録順によらず、従来の前後関係を保つ。
```c
/* kernel/irq/tick.h: 内部口。TICK_SLOT_COUNT=6、エラー値も名前付き定数。 */
typedef void (*tick_fn)(void);
struct tick_slot { tick_fn fn; };
int tick_register(unsigned int slot, tick_fn fn);
void tick_dispatch(void);                 /* IRQ0 内、IF=0 */
/* kernel/irq/tick.c が static struct tick_slot slots[TICK_SLOT_COUNT] を所有。 */
/* platform/pc98/tick_order.h: 配線の順序。kernel/irq/tick.c は読まない。 */
enum { PC98_TICK_SND, PC98_TICK_PCM, PC98_TICK_NE2K,
       PC98_TICK_E82557, PC98_TICK_LINK, PC98_TICK_LGY98 };
/* timer_handler: fixture は現在の場所を維持。 */
#ifdef OS32_R1_FIXTURE
    r1_fixture_timer();
#endif
    tick_dispatch();
```
- 登録は前景のみ。`kctx_irq_depth != 0`・範囲外・NULL・他関数の同枠登録をエラーにし、表を変えない。同一 `{slot,fn}` は成功扱い。
- `irq_save/restore` 内で検査と公開。IF の元の状態を保つ。dispatch 中に登録不可、最大6呼出し。解除口は不要（登録するのは常駐関数だけ）。
- callback は現状同様 IRQ0 内。待ち・メモリ確保・協調切替を足さない。tick_count 更新、EOI、V86 timeout/反射、IRQ 深さは ASM の現状を保持。
| 登録する所有者 | 登録箇所・有効になる条件 |
|---|---|
| `drivers/sound/fm/fm_module.c` | `fm_module_load()` の先頭で `tick_register(PC98_TICK_SND, snd_tick)`。失敗なら既存の負値経路で load を中止 |
| `drivers/sound/cs4231/pcm_module.c` | `pcm_module_load()` の先頭、同様に pcm façade を登録 |
| `drivers/net/e82557/lan_module.c` | `lan_module_load()` の先頭、同様に e82557 façade を登録 |
| `drivers/net/ne2000/ne2000.c` | `ne2k_init()` の引数検査後・bring_up 前。失敗なら既存エラーで返し、装置を起動しない |
| `net/link.c` | `link_init()` で状態を有効化する前。失敗なら診断して return（初回は link_ready=0、既存状態を変更しない） |
| `drivers/net/ne2000/lgy98.c` | 公開 `lgy98_attach()` の検査後・装置操作前。init 経由以外の attach も覆う |
- 初期化前の未登録 callback は、現在の未初期化時 no-op と同じ。ne2k の RUNNING/OVW_WAIT、link_ready、反射フラグの公開より必ず先に登録する。
- façade は登録を残し、既存の tick_target=0 と参照解放で停止する。再ロードは同一登録として成功。表に `.mod` 内のアドレスを置かず、既存 ABI・import 表・参照寿命を変えない。
- snd/pcm/e82557 のモジュール内部実装には登録コードを入れない。単体 host 試験も引き続きその実装を呼べる。

## 3. FDC IRQ の境界・起動・V86
```c
/* drivers/block/fdc.c: 状態と入口をドライバ内へ閉じる。 */
static volatile u32 fdc_irq_fired;
static int fdc_irq(unsigned int irq, void *arg) {
    (void)irq; (void)arg;
    fdc_irq_fired = 1;
    return IRQ_HANDLED;
}
/* fdc_init(): DMA 状態の写し後、motor/reset より前 */
rc = irq_register(FDC_IRQ, fdc_irq, 0, 0);  /* 排他、SHARED にしない */
if (rc != 0 && rc != IRQ_ERR_EXIST) {
    s_init_rc = -1; s_init_st0 = 0; return -1;
}
fdc_irq_fired = 0;                       /* 既存の初期化位置を維持 */
```
- `IRQ_ERR_EXIST` は同一 fn/arg の再初期化だけ。登録成功後の reset/recalibrate 失敗では登録を残す（従来も IRQ11 は開いたまま）。再試行・後続 I/O の寿命を変えない。
- IRQ 内ではフラグだけ更新。SIS 読出し・状態判定・装置待ちは既存の前景処理に残す。2巡でもフラグは1、間に前景は走らない。IRQ_NONE/DEFERRED を返す方式に変えない。
- `irq_dyn_lines` に11を10と14の間へ追加、`IRQ_DYN_COUNT=9`。`IRQ_COMMON 11` を生成し IDT を `irq_stub_common_11` へ。専用 stub と C ハンドラ、extern、FDC 用 include を不要箇所から除く。
- `init/kernel.c:313` の `irq_enable(FDC_IRQ)` だけ削除。`:314` の `_enable()` と IRQ0・cascade 有効化は保持。irq_register が表公開後に IRQ11 を開けるので初回 reset 完了を受け取れる。
- IRQ11 の EOI は `irq_finish` だけから slave → master 各1回。共通入口の cld・IRQ_ENTER/LEAVE・IRETD_USER は専用入口と同じ。V86_REFLECT は追加しない。
- irq.h の「全登録者が tick 回収」は **FDC の排他完了通知だけ例外**と明記。FDC は既存 timeout/retry で戻す。NIC 等の tick 保証は緩めず、FDC の新規 poll 処理は作らない。
- 起動 FD は kernel の fdc_init→媒体設定→mount を維持。V86 FD は INT 1Bh→loop→VFS を維持し、背後のファイルが物理 FD 上ならこの FDC 経路で読む。V86 中の IRQ11 でもホスト通知だけ行う。

## 4. 大きさの見積もり（i386、リンク前の概算）
| 対象 | 増減 B・予算の扱い |
|---|---|
| 周期表 | BSS +24 B、登録/dispatch/呼出側 text +250～650 B、直接呼出し/include 除去 −30～60 B |
| FDC/IRQ | irq_line 1本 +68 B（4×12+4+4×4）、対象表 +1 B、登録/失敗処理 +60～160 B、専用 stub→共通で −10～30 B程度 |
| 合計 | 常駐約 +300～900 B（丸め別）。IRQ 共通配送・6間接callのCPU増もゲストで観測。4KiB境界越えならヒープ開始がさらに1ページ動く |
| 起動 FD | 1232KiB（1,261,568 B）/1440KiB（1,474,560 B）は総容量で、空きではない。圧縮差分・クラスタ丸め込みで再生成必須 |
| MINIMAL | 集合・除外設定は変更しない。BOOT+MINIMAL と FD の対応を packages 試験で維持。kernel と対になる各モジュールの CRC を全て再生成 |
| kdebug.bin | selftest の拒否対象11→12だけなら概ね0 B（文字列長も同じ）。tick import追加なし。mkmod.py:283–286 の image_max/mem_max/load_span_max を通常ビルドで再判定 |
- 上限値・パッケージを変更しない。`build/os32.ld:81` の常駐配置 ASSERT、mkfat12 の容量検査、mkmod の上限検査を通す。
- この確認では main の生成物が得られず余裕 B は未測定。「上限に触れない」とは未判定。実装段で同条件の前後を build し kernel text/data/BSS・常駐終端・圧縮サイズ・FD空き・kdebug サイズ/上限との差を記録。超過時は取り込まない。

## 5. ファイルごとの変更一覧（概数、生成物を除く）
| 新規/変更 | ファイル | 行数 |
|---|---|---:|
| 新規 | `kernel/irq/tick.h` / `kernel/irq/tick.c` / `platform/pc98/tick_order.h` | 25 / 50 / 15 |
| 変更 | `arch/x86/kernel/isr_handlers.c` / `arch/x86/kernel/isr_stub.asm` / `arch/x86/kernel/idt.c` | −30 / −25,+2 / 5 |
| 変更 | `kernel/irq/irq_math.h` / `kernel/irq/irq_math.c` / `kernel/irq/irq.h` | 5 / 4 / 12（irq.c は変更不要） |
| 変更 | `drivers/block/fdc.c` / `init/kernel.c` | 20 / −1 |
| 変更 | `drivers/sound/fm/fm_module.c` / `drivers/sound/cs4231/pcm_module.c` / `drivers/net/e82557/lan_module.c` | 各6 |
| 変更 | `drivers/net/ne2000/ne2000.c` / `drivers/net/ne2000/lgy98.c` / `net/link.c` | 各8～12 |
| 変更 | `build/kernel.mk` / `kdebug/kselftest.c` | 2 / 2 |
| 新規 | `tools/tests/tick_host.c` / `test_tick.py` / `fdc_irq_host.c` / `test_fdc_irq.py` | 100 / 70 / 90 / 60 |
| 新規 | `build/checks.d/check-{tick,fdc-irq}-host.mk`、`tools/check_map.d/check-{tick,fdc-irq}-host.yaml` | 各15～25 |
| 変更 | `tools/tests/irq_math_host.c` / `test_irq_math.py` / `test_e82557_drv.py` / `test_ledger.py` | 15 / 10 / 20 / 5 |
| 変更 | `tools/tests/r1_fixture_host.c` / `test_r1_fixture.py`、対応する既存 check_map.d の依存集合 | 10 / 5 / 各5～15 |
| 変更 | fdc/NE2K/link/モジュール試験の登録口の差し替え定義・コンパイル入力 | 各5～15、実装時に該当する host 構成へ限定 |

## 6. 受入 → 試験・変異、退行防止、段分け
| 受入項目 | host / ゲスト NP21/W | 検出する変異（各1例） |
|---|---|---|
| kernel の装置依存を除去 | host: timer 本体・IRQ11 stub・kernel.c の禁止参照を限定検査、KAPI生成物差分なし | ne2k 直接callを戻す |
| 全6本の旧順序・各tick各1回 | host: 実 tick.c に逆順登録し call log、空枠・再登録も確認 | LINK/NE2K の枠を交換 |
| 排他とエラー時の不変性 | host: IF保存、IRQ内拒否、NULL/範囲/競合、満枠後も既存6本維持 | 拒否前に表を書き換える |
| モジュール寿命 | host: FM/PCM/LAN load失敗→再load→abort、tick_target解除後のdispatchで呼ばれない | 表に candidate.tick を直接登録 |
| IRQ11 の配送とEOI | host: 実 irq_math/irq.c＋I/O shim、2巡・未登録・slave/master順・IRQ深さ | 共通stubに余分なEOIを追加 |
| 初回/reinit/登録失敗 | host: 実 fdc.c の初期化にIRQ注入、PIC公開順、重複・競合時にmotor/reset未発行 | 登録をreset後へ移動 |
| FDC 既存復旧 | host: fdc-seek/track継続、IRQを落として既存期限で戻る | waitの期限判定を削る |
| FD起動と媒体 | NP21/W: 1232/1440両方で起動→mount→ファイル読出し、HDD起動も比較 | kernel側enableを削り登録も省く |
| V86 FD | NP21/W: コピーしたFDイメージでINT 1Bh読出し・終了・再入、物理FD背後の読出しと終了後host FD再読出し | IRQ11にV86反射を追加 |
| LAN・音・通信・入力 | NP21/W: NE2K/link、反射モード、利用可能な82557、FM/PCM、serial・キー入力を前後比較 | façadeの登録1本を省く |
| fixture・容量 | host: R1/ledger/stack-entry、packages、make allと前後サイズ記録 | fixtureをdispatch後に移す |
- 既存利用者: irq_math_host.c:172,186 と kdebug/kselftest.c:1946 の「11=NOTSUP」は固定線12の拒否へ移し、11の成功は隔離したhost表で試す。稼働中FDCの線をselftestで解除・隔離しない。
- test_e82557_drv.py:398 の文字列順検査を登録枠＋実dispatch検査へ。test_ledger.py:432 の専用stub一覧を共通11へ。R1抽出fixtureはtick_dispatchの差し替えを用意し、fixture先行/longjmp後に配送しないことを維持。
- fdc_track_host.c:205,212 は同一翻訳単位に含めたfdc.cのstatic状態にアクセスできる構成を維持。extern依存があれば試験側を専用handler注入へ変更し、製品へ試験用公開フラグを残さない。
- 既存 fdc-seek/track、irq-math、e82557-drv、net-link、fm/pcm/lan-module、R1/ledger/stack-entry の対象と変異を落とさない。未対応のNP21/W装置は未実施と記録し、host代替だけでゲスト合格にしない。
| 段（各 Codex 2時間以内） | 内容・完了ゲート | 並列 |
|---|---|---|
| D1a | FDC/IRQ11と既存利用者修正、make all＋対象MUTATE=0＋check_select.py --lint、FD/V86受入 | D1bとはisr_handlers競合。順次推奨 |
| D1b | tick表・6登録・host/fixture修正、同3検査＋周期処理受入 | D1a後。host shim調整もここに含む |
| D1c | 結合ゲスト・容量前後比較・対象変異、失敗時修正を戻す | ゲストの並列操作不可、host変異は独立実行可 |
- 配備時はNP21/W停止→配備→開始を既存手順で行う。NHD/マスター上書き・ini変更は本設計の作業に含めず、必要時は[D2]の個別承認を得る。

## 7. 未決（ユーザー判断が要るもののみ）
- **0件**。IRQ11の排他・既存timeout維持、6固定枠、常駐façadeの永続登録を推奨として採用。実測サイズとゲスト証拠は実装段の受入作業であり、仕様判断の保留ではない。

## 確定 (2026-10-26、Opus 5.5 の第 1 回 (P1 なし) を PM が反映 — 本文の該当の行より優先する)

- **F1**: §5 に `drivers/block/fdc.h` を足す (465 行の `extern volatile u32 fdc_irq_fired;` と isr_handlers の注記を削る。fdc.c で static にすると、そのままでは「static declaration follows non-static」で止まる)。
- **F2**: `tools/tests/test_net_link.py:92-99` の check_timer_calls_link_tick (timer_handler の本文に ne2k_timer_tick → link_tick の順の文字) は D1b で必ず落ちる。「PC98_TICK_NE2K < PC98_TICK_LINK、かつ link_tick を登録するのは net/link.c だけ」の検査に置き換える (D1b に含める)。
- **F3 (PM 決定)**: IRQ11 の入口の変更は実機の FD 起動で確かめる。DEFERRED_TESTS の T4T7-RH の回に「D1 の Ra266 FD 起動 (IRQ11 の共通入口)」を足す。NP21/W の FD 起動 (1232/1440) は D1c のゲストで PM が回す。
- **F4**: 完了条件は ROLES §0 の Tx の形: D1a・D1b は各 `make kernel` + その段の直接の試験 `MUTATE=0` + `check_select.py --lint`、D1b の後に 1 回だけ `make all` + `check_select.py --select --files <D1 全体の差分>` の候補を `MUTATE=0` + 静的 5 本。表の「FD/V86 受入」「対象変異」「結合ゲスト・容量」は **PM の行** (D1c = PM がレビューの後に回す)。
- **F5**: tick_register が失敗したときの load の戻り値は各部品の既存の初期化失敗の値 (FM/PCM/LAN の MODULE_INIT_FAILED 相当、今の load の失敗の経路と同じ)、ne2k_init は既存の NE2K_ERR_* のうち初期化失敗の値を返す。link_init の登録の置き場は関数の先頭 (kmemcpy より前、登録が失敗したら状態を変えずに戻る)。
- **F6**: platform の enum に `PC98_TICK_COUNT` を足し、両方が見える登録側で `_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT, ...)`。
- **F7**: fdc_track_host (test_fdc_track.py:236-240) は fdc.c の `#include "irq.h"` に shim か `-Ikernel` を足し、host 側の irq_register は成功と失敗を切り替えられる形にする (「登録失敗時に motor/reset 未発行」を試す)。test_h4_backing.py:64 が fdc.c の `s_fdbuf[`〜`fdc_dma_buf(void)` を切り出すので、fdc_irq はこの区間の外に置く。
- **F8**: §5 に文書と注記の追従を足す: docs/04_interrupts.md:42、tools/tests/irq_math_tdd.md:73、arch/x86/kernel/idt.c:149、kernel/irq/irq_math.c:13-16、kernel/irq/irq_math.h:27-28 (線の一覧 3/5/6/8/9/10/14/15 に 11 を足す)。
- **PM 決定 (契約)**: irq.h の「全登録者が tick 回収」の契約に、常駐で解除しない FDC を例外として書く (TASK_HAL_WIRING §1-1 の契約の追記、irq.h の注記に 1 行)。
