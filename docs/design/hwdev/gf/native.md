# hwdev/gf/native.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/native.c`

BAR 全 decode 範囲を台帳予約し、BAR0 の必要頁と BAR1 の表示面を既存 master PD の supervisor+PCD で写像する。予約凍結後に拡張せず、実使用前は ID/base/属性の一致を読取りで確認する。
不一致・未知 ID・範囲不成立では GF を使わず pc98 を維持する。BAR1 の窓長と実 VRAM 容量は別で、既存 PFB 容量確認・表示面範囲も維持する。変換は `P2V_IO` と既存写像 API（[C5]、`O/gfx/gfx_core.c:305–327`）。
本番全体のソフト上限は既存 `GF_INIT_EXEC_US` の 60 秒を採用（`O/kdebug/gfinit_exec.h:4`）。期限超過は既存失敗出口へ進むが、戻らない MMIO に壁時計上限は保証できない。

1000µs は devinit の実行器 (`gf_init_wait`) が中止と時計を確認する間隔で、ハードウェアの制約でも待ち要求の上限でもない。呼び元は要求長をそのまま渡し、`gf_production_wait` は要求を `GF_INIT_WAIT_CHUNK_US` (1000µs) 以下に分け、各片で開始時刻を取り直して要求時間以上を待つ。0µs は時計を読まず成功する。開始・poll の時計失敗は元の rc、時計停止は各片の有限 poll 上限で -1 を返す。1ms を超える要求も受理し、foundation/display の両 timing adapter に同じ口を使う。mode_load の外側の分割は、各片の前後の期限確認を保つため残す。手本の合計待ちは nouveau `dispnv04/crtc.c:229` の 10ms、`dfp.c:105` の 50ms、`dac.c:279,299,309` の 4/1/5ms。

本番の待ち口は `gf_production` を受け取り、CTRL+STOP を扱う recover セッションを持たない。`gf_production_point` (`hwdev/gf/bus.c`) も同一性・devinit の期限だけを確認する。中止確認は診断側の `runner_point` (`kdebug/gfinit_exec.c`) → `recover_safe_point` (`kdebug/recover_common/session.inc`) が所有し、`gf_init_wait` の各片の前後で呼ぶ。本番の待ち口からこの診断セッションの中止確認は呼べない。
