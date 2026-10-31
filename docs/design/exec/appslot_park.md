# exec/appslot_park.inc — park の共通更新

> 状態: **現行 (2026-11-01)** — L01b、既存の更新順を共用。

`appslot_transition.inc` が同じ翻訳単位に取り込む。
OP_WAIT / kbd / poll / yield の各入口が現在のスロットを取得し、
存在する場合だけ `appslot_park_commit_origin` に由来の印と遷移先を渡す。
リダイレクト退避 → 由来の印 → アプリとシェルの STOP 要求クリア →
`appslot_park_finish` の順を保つ。計数は各入口で最後に行う。
STOP の退避は計数と印の順が異なるため、従来の更新列を使う。

資格判定は `appslot_park_kbd_check` に共用する。
現在 ID の範囲 → 生存かつ RUNNING → GUI 起動の順に調べ、拒否を 1 回数える。
OP_WAIT はその後に OP_WAIT 内かを調べる。
poll は資格判定より先に同 tick を間引き、拒否される試みも tick を更新する。
yield は kbd と同じ判定を使い、tick を間引かない。
