# kdebug/recover_plan/bind.inc — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_S2_LAYOUT.md・GF_STAGES_DESIGN.md・REALHW_RECOVER_ADDENDUM.md・REALHW_RECOVER_ADDENDUM_R2.md・SLIM_HWDEV_DESIGN.md (D1 で移した)
ソース: `kdebug/recover_plan/bind.inc`

## GF6200_4A_S2_LAYOUT

bindとの同値性を優先し、SEQ→GFX→SEQ DATAを受理

| 項目 | S2での決定／出口 |
| --- | --- |
|計算量の対処|bind時にnode→layout/ruleを一度解決して固定長cache、indexed port IDも事前解決（profile/plan不変期間のみ有効）。依存検査内の線形探索を除きO(N²＋N·R)へ。前向き1回・合流交差・512KiB契約を維持|

## GF_STAGES_DESIGN

`recover_bind_plan`（`:1666`）**各入口でEのときだけ failed/cold_latched の拒否を迂回**し、終端 `REC_FAILED` も許す。RUNNING/PAUSEDからは開始しない。

builder・bind・契約生成を追従させる。

## REALHW_RECOVER_ADDENDUM

- `recover_bind_plan`をtrial_begin直前に呼ぶ。検査成功時のみrecoverが参照と計算したhashを保持し、既存trial_beginのplan_hashとprofileを照合してsessionへ束縛する。hash単独の新規armは禁止。S0等も小さな固定planをbindする。trial_begin/trial_step/tokenの既存引数・承認契約は維持する。

- 表とROMは部品所有のkernelメモリでbindからcleanup完了まで不変・生存。recoverはコピー不要、二重bind不可、開始失敗/終端で参照を解放する。全count・範囲・checked加算/乗算・確保失敗を書込み前に検査する。hashは全表の正規化u32値・参照先列・profile CRC・ROM識別から算出し、ポインタ/paddingを含めない。 hashは既存crc_wordの連鎖で計算する。

- 静的検査は各入口から到達可能な両辺を走査し、合流で「必ず成功済みの(ctx,rule)」集合を交差する。Wのrequiresを入力集合で検査し、成功と必要readback後だけ当該ruleを加える。skip/無書込み辺は加えない。失敗はplan走査を終了してcleanupへ移り、以後のtrialへ合流しない。全substepにも同じ規則を適用する。

- call/反復の新ctxに親/前回の成立集合を暗黙継承しない。ctxを戻したときは親自身の記録だけを使う。動的命令数・深さ・待ちの既存上限を経路ごとに検査し、静的node総数を実行stepsに数えない。展開表を確保できなければ開始拒否とし、上限や予算を拡大しない。

> [要確認] 現行は診断も hwdev/gf/runner.c を使い、main 境界の 5 node を生成する。原文の静的展開・ordinal・verify 規則を現行実装と同一とは扱えない。

- abort・失敗の時点でplanのcursorを無効にする。local_cleanup・①②はplanの辺を通らず、RESTORE/QUIESCE phaseのrule（SAVED・override）と既存の前提検査だけで動く。未成立のtrial依存を成功と仮定せず、独立に成立する②の経路とr2の予約/FAILED契約を維持する。

## §2 追加：世代跨ぎ依存とruntime照合

- 通常requiresは同session・同phase・同ctxの成功を指す。世代を跨ぐ場合だけdepsに`{consumer node, requiresのrule ID, producer node}`を一件追加する。producerは当該ruleの成功点そのもの、consumerの全到達経路に先行する必要がある。別phase/main/呼出し/反復の参照はこの宣言以外不可。存在しないID・重複・循環・requiresに無い追加依存を拒否する。

- 例：PREPAREの停止確認rule Rのnode PをRUNのnode Cが使うなら`{C,R,P}`。Pの書込みと必要readback完了を全経路で証明する。単に「PREPARE済み」や同じruleの前反復成功では代用しない。複数候補から最新成功を拾う形式は設けない。

- D1: 上限を nodes 2048・args 8192・ctx 2048 に下げ、flow_bits は 2048×64 u32 = 512 KiB、valid_bits は 64 u32 (目標機 32MB で 8 MiB の静的バッファは不可)。上限を超える plan は bind で拒否。

- D2: node の添字は位相順 — next[i] は NONE か自分より大きい添字。bind で検査し、循環の拒否も兼ねる。全経路検査は 1 回の前向きの走査。

- 表はprofile所有の静的不変表で、寿命は既存profileと同じ。(opcode, substep)を一意キーとし、bindで未登録/重複キー、argc/kinds不一致を拒否、accessで現在nodeの方向と照合する。SELECTとDATA、条件readと非アクセスの制御は別substepに分ける。

- skipの非実行形はアクセスなしのsubstep、forceの実行形は許可付きWのsubstepとして登録する。既定のhead/skip/辺検査とrule検査は維持し、この表だけで許可を増やさない。T1bの合成表はhost試験用profileだけに置き、実opcode・正常終端を含む固定列の未提供データを創作しない。

- indexed SELECT の成立は同 phase/main/ctx の成功記録を要求し、同一 index port の別 SELECT で古い選択記録を無効化する。静的合流ではこの記録も交差し、

- indexed EXPR READ にも既存の同 target・phase/main/ctx の SELECT 成立bitを全経路で要求し、同port再選択による失効と合流交差を適用する

unresolved_count 非0の inventory は bind を拒否し、PLL 入口の NONE を実装済み扱いしない。

- node→layout/rule の固定長cacheとtarget→実効index port IDをrecoverがbind時に所有し、依存走査内では線形表探索を行わない；既存512KiB flow_bitsは拡大しない。

- CR44のtargetはhead0で解決するがnode.headは命令入口の制御headを保持するため、共有node検証のtarget/head一致条件からhead0 CR44だけを除外する。

- CR44の実行形RMW（SUB8の78）はruleのvalue.maskが幅全体を覆いbitsが既定head値のときだけbindでheadを確定する；可変結果は拒否し、skip中の53/54以外のCR44書込みもdecode拒否を維持する。

## REALHW_RECOVER_ADDENDUM_R2

- EXPRは任意callbackでなく登録済み整数式ID（RMW/COPY/PLL係数/VRAM pattern等）と検証済みplanの引数を使う。規則側でも結果・範囲を検査し、未解決式は拒否する。旧値の読取りが安全でないRMWは禁止。

- SAVEDは同sessionのvalid snapshotに結び付いた安全なRWのRESTORE、または分類済み局所復元だけ。COLD/trigger/W1Cは許可しない。

- requiresは先行ruleの成功・必要readback完了を要求する。自己参照・循環・存在しないIDをprofile_okで拒否。展開plan上の全経路で先行成立を静的検査し、runtimeでも成立記録を照合する。

- 記録はsession/phase/呼出し・反復世代に束縛する。世代を跨ぐ依存はplanで先行実行を明記する（例:PREPAREの停止確認）。skipされた書込みや前反復の成功で依存を満たさない。

## SLIM_HWDEV_DESIGN

- node_layout/node_write（recover.h:336–337）の保持ポインタ、recover.c:694のq-p->rulesは連続した従来配列を指すので意味が変わらない。
