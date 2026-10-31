# kdebug/recover_plan/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: REALHW_RECOVER_ADDENDUM.md (D1 で移した)
ソース: `kdebug/recover_plan/`

## 索引

- [kdebug/recover_plan/access.inc](access.md)
- [kdebug/recover_plan/bind.inc](bind.md)
- [kdebug/recover_plan/profile.inc](profile.md)

### step.inc

ソース: `kdebug/recover_plan/step.inc`

- 4aはopcode/substep前にenter、終了時にleaveを呼び、recoverが現在入口/辺・ctxを管理する。条件readと選択値は共有accessorから得て、leaveでは選択されたnext[0]/next[1]の辺であることだけを確認する（両辺は静的全経路検査済み）。実アクセスは現在nodeのphase/main/pc/substep/head・rule・引数と完全一致させる。非実行nodeではW不可（RESET等のforceはr2の許可付きW node）、辺の飛越し/再実行は拒否する。 cleanup中はcursor無効のためaccessのnode=NONEとし、前記RESTORE/QUIESCEのrule・既存前提検査を適用する。

- nodeの成立bitはrecover accessorが書込み成功＋必要readback完了時だけ作る。通常依存は同ctxの先行記録、明示依存は同sessionの指定producerの成立bitとplan上のphase/ctx/node/rule全一致を要求する。session終了で全破棄。PAUSEDは保持するが承認・入口順を飛ばせない。失敗/skipは記録しない。

- recover_plan_accessは既存recover_accessと同じ検査経路で登録式を一度評価して要求値に用いる；SAVED採取・read順・許可表・mask/guard検査の所有者はrecoverのまま、実行器から生busを呼ばない。

### access_idle_read.inc

ソース: `kdebug/recover_plan/access_idle_read.inc`

- INSPECTはbind済みprofileのread_safeな非indexed READのみ。SELECTによる読取り準備はINSPECTに含めない。
