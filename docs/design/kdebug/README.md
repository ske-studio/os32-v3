# kdebug の実機試験

実機の初回は記録だけで流し、食い違いを洗い出してから照合を締める。必須の確認は設計で列挙したものだけ。
外す・変えるものは実行時に渡す。試験は段ごとの小さなバイナリと共通部分の include に分け、ソースも段ごとに分ける。
道具の判定の誤りで戻す操作を止めない。台本の判定は道具の側に持たせる。

## 共通の決まり

- PC-9821、単一利用者・単一タスク・協調切替、整数演算。既存の実行・復旧・試験の意味を変えない。

- 権限分離や攻撃対策は追加しない。公開ヘッダの新定数は `#define`。CLAUDE.md [C1]〜[D4] を適用。

| 置き場 | 責務 |
| --- | --- |
| `kdebug/recover.c`・profile 群 (新設) | KPEEK と並ぶ ops。固定長 snapshot、許可表、状態機械、期限、restore/verify。KDEBUG=0 は入口で拒否し書込み 0 件 |
| kernel/HAL の共通部 | 98 表示復帰・短い窓操作・tick・serial の必要最小限の import。汎用の任意番地 write API は作らない |

## 索引

- [kdebug/gf_stage_state.c](gf_stage_state.md)
- [kdebug/gfinit.c](gfinit.md)
- [kdebug/gfinit_bus.c](gfinit_bus.md)
- [kdebug/gfinit_decode.c](gfinit_decode.md)
- [kdebug/gfinit_exec.c](gfinit_exec.md)
- [kdebug/gfinit_mode.c](gfinit_mode.md)
- [kdebug/gfinit_plan.c](gfinit_plan.md)
- [kdebug/gfinit_plan_c.c](gfinit_plan_c.md)
- [kdebug/gfinit_pll.c](gfinit_pll.md)
- [kdebug/gfinit_resources.c](gfinit_resources.md)
- [kdebug/gfinit_wire.h](gfinit_wire.md)
- [kdebug/kdebug_main.c](kdebug_main.md)
- [kdebug/recover.c](recover.md)
- [kdebug/recover_cleanup.h](recover_cleanup.md)
- [kdebug/recover_gf.c](recover_gf.md)
- [kdebug/recover_profile.c](recover_profile.md)
- [kdebug/recover_virtual.c](recover_virtual.md)
- [kdebug/recover_wire.h](recover_wire.md)
- [kdebug/selftest_exec.c](selftest_exec.md)
- [kdebug/selftest_kbd_inject.c](selftest_kbd_inject.md)
- [kdebug/selftest_lease.c](selftest_lease.md)
- [kdebug/generated/](generated/README.md)
- [kdebug/gfinit_decode/](gfinit_decode/README.md)
- [kdebug/gfinit_op/](gfinit_op/README.md)
- [kdebug/gfinit_plan_c/](gfinit_plan_c/README.md)
- [kdebug/gfinit_pll/](gfinit_pll/README.md)
- [kdebug/gfinit_resources/](gfinit_resources/README.md)
- [kdebug/recover_plan/](recover_plan/README.md)
- [kdebug/gfinit/rom.inc](gfinit/rom.md)
- [kdebug/recover_common/clock.inc](recover_common/clock.md)
- [kdebug/recover_common/log.inc](recover_common/log.md)
- [kdebug/recover_common/s0.inc](recover_common/s0.md)
- [kdebug/recover_common/session.inc](recover_common/session.md)
- [kdebug/recover_gf_stage/c.inc](recover_gf_stage/c.md)

### selftest_internal.h

ソース: `kdebug/selftest_internal.h`

```c
/* kdebug/selftest_internal.h: 移植先の通常C関数、importマクロ対象外 */
u32 kst_kbd_inject(void);
int kst_lease(void);
u32 kst_exec_tramp_user(void); u32 kst_exec_kapi_layout(void);
```

- ホスト試験が旧selftest名を呼ぶ場合、ホストfixtureだけの転送関数でkst_*へ渡す。本番に旧名の成功stubを残さない。

### kdebug_window.c

ソース: `kdebug/kdebug_window.c`

必要ページだけ保存・付け替え・復元

| 本設計での扱い |
| --- |
| 同じ静的 PT を使い、書くページにだけ RW。4MB 全体を毎回張らない |

- MMIO/VRAM は volatile、supervisor・PCD、書きだけ RW。PAGEの保存→写像→操作→復元を irq_save/restore 内で行う。index/data と RMW は一組の間だけ割込み禁止。

- TIME、ログ、ROM全体の検査を割込み禁止区間に入れない。窓の排他フラグを KPEEK と共有し、busy 中の別 op は BUSY。PTE復元失敗は致命エラーとして後続を止める。

- **割込み禁止の区間** (§2 の「index/data と RMW は一組の間だけ割込み禁止」): 実装は `docs/design/kdebug/recover.md` の区間を分ける契約に従う。

- T2窓をkdebug_windowへ移し、peekと同じboot寿命の排他・PTE復元失敗ラッチを使う；最大2 PTEを保存し、READはsupervisor/PCD・RWなし、WRITEだけRW、map失敗でも全PTEを復元してIRQを戻す。

- GFのSELECTとDATAはそれぞれ短い独立したIRQ禁止・窓区間とする；単一タスクでGPUを扱う割込みが無い前提に合わせ、4a本文§2の「一組の間」をこの形で具体化し、ログ・待ちはIRQ禁止区間に入れない。

### kpeek.c

ソース: `kdebug/kpeek.c`

同じ静的 PT を使い、RW 無し

### gfinit_budget.inc

ソース: `kdebug/gfinit_budget.inc`

実際のRECはBEGIN/END・内部MMIO・cleanupも含め、manifestの全分岐・反復展開と各フィールド最大桁数から最大送信バイト数Bを計算する。UARTの実設定bits/frameで`T_wire=ceil(B×bits/frame/9600)`を求め、有限の送信待ち余裕を加えたT_logをtrial_beginの予算に渡す。kdebug_call内だけを数える総時間上限=60秒+T_log（PAUSEDの承認待ちは除外）、実測送信時間だけを60秒の実行予算から除外し、T_log超過はLOG_FAIL。待ち5秒は実行予算内。事前に算定不能・桁溢れ・recoverが総予算を受理しない場合は書く前に拒否する。

REC予算は転送/比較の計2組4行（480行ずつの処理は内部反復）・DAC768アクセス・全レジスタ操作/待ち/失敗分岐の最悪行数から開始前に算定し、既存gf_budgetのnode/arg式へ反復分を加える（`kdebug/gfinit.c:259–295`）。trial全体の送信除外60秒・PAUSED積算外は維持。

> [要確認] GF_STAGES の C 実行は定数期限へ移行している。この旧 C 送信予算式は現行の C-only には適用されない。

5 回の読取りに新しい時間の上限は作らない。既存の verify の読取り (kdebug/recover.c:2016-2020・943-956) と同じく、送信の時間は tx_ticks、読取りの時間は試行の spent に数え (同:312-338・808-818)、期限の検査 (同:1168-1183) で超えたら REC_TIMEOUT。trial_limit (60 秒、recover_profile.c:86-88) は変えない。

- 予算: 増える 10 行・最大 3850B は RUNNING だけ。B'=B+3850 で log_limit と transport.max_polls を算出し直す (gfinit.c:273-288)。cleanup の restore_limit・P/S の予約・diag/final は変えない。

- GFの送信枠は不変planの全node・READ引数と最大REC行長から両辺を含む上界をchecked算出し、実UARTの8N1速度でwire時間の2倍＋3秒を有限上限にする；送信中の実tickだけを60秒の実行枠から除外し、PAUSEDを双方から除外

### recover_diag.h

ソース: `kdebug/recover_diag.h`

recover.c の static 1 枠（リング・動的確保なし）。

```c
struct rec_tx_diag {                  /* 全て32bit、48 B。共有 recover に追加しない */
    u32 sid, seq, pos, fn; int rc;
    u32 where, sent, polls, bytes, valid, attempted, active;
};
```

### kpeek_allow.c

ソース: `kdebug/kpeek_allow.c`

| DWORD（BAR0相対） | 非破壊読取りの根拠と許可 |
| --- | --- |
| 680508h | /home/hight/os32-tmp/run/gf-main0-fail/ref/nouveau/nvkm/subdev/devinit/nv04.c:205,207の保存読取り。係数定義/home/hight/os32-tmp/run/gf-main0-fail/ref/envytools/rnndb/display/nv3_pramdac.xml:10–14,41。HWDEV read_safe=1（kdebug/generated/recover_gf_tables.inc:739）、kpeek可。 |
| 680578h | /home/hight/os32-tmp/run/gf-main0-fail/ref/nouveau/nvkm/subdev/devinit/nv04.c:206,208の保存読取り。/home/hight/os32-tmp/run/gf-main0-fail/ref/envytools/rnndb/display/nv3_pramdac.xml:26–29,88はNV31:NV34限定なのでNV44Aへの根拠はN。read_safe=1（同:740）、kpeek可。 |
| 680580h | /home/hight/os32-tmp/run/gf-main0-fail/ref/nouveau/nvkm/subdev/devinit/nv04.c:220–224のNV41以降の保存読取り。read_safe=1（同:615）、kpeek可。 |
| 00C040h | /home/hight/os32-tmp/run/gf-main0-fail/ref/nouveau/nvkm/subdev/devinit/nv04.c:257–259,272の保存→変更→復元。read_safe=1（同:612）、kpeek不可なので下記1行を追加。 |
| 68050Ch | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/dispnv04/hw.c:392–404の通常状態保存で読む（MIT）。/home/hight/os32-tmp/run/gf-main0-fail/ref/envytools/rnndb/display/nv3_pramdac.xml:59–80も源選択制御。既存kpeek PRAMDAC帯で可、HWDEV行は不要。 |

これらは通常制御の保存読取りを非破壊性の根拠とする。NV44Aマニュアルによる保証ではなく、追加MMIO readによるposted write排出・時間差は残る。

C040はkdebug/kpeek_allow.c:12–20に `NV_ROW(0x00c040U,4U,KPEEK_W4)` 相当を定数化して1行追加。PMC帯全体を広げない。

### recover_gf_expand.h

ソース: `kdebug/recover_gf_expand.h`

- 展開関数はrecover_gf.cで定義し、新規recover_gf_expand.hに宣言する。このヘッダはgfinit*.hからincludeしない。

- gen_hwdev_contract.py:97–101の傘はrecover.h/recover_gf.h/gfinit*.h。新ヘッダはgfinit.cとhost試験からのみ読む。

```c
/* recover_gf_expand.h：既存型の前方宣言だけ、契約の傘には入れない。 */
struct recover_profile;
struct gfinit_plan;
int recover_gf_profile_expand(struct recover_profile *p, struct gfinit_plan *buf);
```

### recover_gf_c.h

ソース: `kdebug/recover_gf_c.h`

許可表は `recover_gf` のC専用phaseに§3各先の幅・値式・順・verify maskを登録。PLL・indexed対象は既存targetを共有、mode dataはCOLD。

C出口は50C/510/508/578/580/524、CONFIG/START、CR00..18・19/1A/25/28/33/44/47、DACCLK/TEST、cfg50/54/Command/route、セル検査結果を保持する。

同じ target への別操作も固有 substep を保ち、旧 phase/rule/trace は変更しない。

> [要確認] row ID・件数は設計時の値。現行の生成表と C 操作の型付き規則の件数は再照合が必要。

### gfinit_verify.inc

ソース: `kdebug/gfinit_verify.inc`

記録属性と比較 mask

C 実行中は変更できない。
