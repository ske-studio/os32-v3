# kdebug/recover_common/session.inc — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md・GF_STAGES_DESIGN.md・REALHW_RECOVER_ADDENDUM.md・REALHW_RECOVER_DESIGN.md (D1 で移した)
ソース: `kdebug/recover_common/session.inc`

## GF6200_4A_DESIGN

- RUN_ONE中も各opcode境界のtrial_stepでシリアルabortを取り込み、poll/delayも有界分割した安全点で確認する。要求があれば後続opcodeを実行せず局所PTE/IRQ/indexの後始末→trial_end(abort)へ進む。ABORT opがRUN中に届くことには依存しない。

## GF_STAGES_DESIGN

`trial_begin`（`:1238`）**各入口でEのときだけ failed/cold_latched の拒否を迂回**し、終端 `REC_FAILED` も許す。RUNNING/PAUSEDからは開始しない。

それ以外のplan有無・結合・profile/CRC・identity/範囲の検査は維持する。**failedを消すのは `trial_begin` の既存の新session初期化（`:1316`）だけ**。cold_latchedは消さず、`:1327`でもC profileのcold_required=trueを維持する。

**c_node に入った所でセッションの期限を C の定数に替え、S0 の予約は保つ**

## REALHW_RECOVER_ADDENDUM

cacheの成立は全3本の測定・復元・override・正常終端成功時だけ。GPU W 0件で候補未成立なら①省略だけではFAILEDにしない。

BARは同装置のcacheと生Command/BARを照合し、未測定なら範囲検査待ちとして拒否する。

- 非scriptのmain/pcと非EXPRのexpr_idはNONE、非indexedのindexはNONE、他の未使用欄は0。正常trial_endはplan終端到達を要求し、abort/失敗はcursorを破棄してRESTORE/QUIESCEへ移る。開始失敗・終端でplan参照と成立bitを解放する。

## REALHW_RECOVER_DESIGN

| 段 | 処理 | 成功条件・次の段 |
| --- | --- | --- |
| ④ deadman | 試験命令境界・poll・delay の各刻みで deadline/abort を確認し、超過時に①②を自動実行 | 復帰側にも別の期限・反復上限。ホストの監視は補助で、ゲストの復帰能力を作らない |

- 状態機械: IDLE→SNAPSHOTTING→ARMED→RUNNING、段間はRUNNING→PAUSED→RUNNING、終端はRUNNING/PAUSED→RESTORING→VERIFIED または FAILED。失敗を IDLE に戻して次の試験を許すことはしない。

- 単一セッション、入れ子禁止。snapshot 完成前に試験書込み禁止。profile CRC・起動世代・装置 ID/BAR 不一致、再起動後の再利用は拒否。

- 初版は既に停止した装置を試験開始条件とする。quiesce が設定を変更する profile では、その変更対象の安全な元値を先に保存し、準備途中の失敗も cleanup 対象にする。稼働中 LAN/PCM の透過的な中断・再開は保証しない。

- `trial_begin(profile, plan_hash, budget)` が snapshot+arm、`trial_step` が有界な 1 操作、`trial_end(reason)` が全出口共通 restore。成功・失敗・中止のすべてが同じ cleanup を通る。

- PREPARE と各 RUN_ONE の正常帰還時は kdebug_call の外で PAUSED とし、承認待ちの期限を止める。PAUSED 中の復旧要求は `recover now <session>` だけを受け、次段の承認済み kdebug_call で RUNNING に戻す。trial の期限は kdebug_call 内の時間だけを積算し、承認待ちを数えない。 recover の status 等の通常要求は PAUSED 中には受けない。復帰処理にも独立の有限予算を適用する。
