# kdebug/recover_common/clock.inc — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_RECLOG_DESIGN.md・GF_STAGES_DESIGN.md・REALHW_RECOVER_ADDENDUM.md・REALHW_RECOVER_ADDENDUM_R2.md・REALHW_RECOVER_DESIGN.md (D1 で移した)
ソース: `kdebug/recover_common/clock.inc`

## GF6200_RECLOG_DESIGN

recover_uart_byte はready==0を既にframe待ち＋累積poll/時間上限付きで再検査する。

## GF_STAGES_DESIGN

定数の期限を使うC実行種別を既存の開始・安全点・送信・終端に通す。`gf_budget`、`recover_budget_apply`、`cleanup_budget_compute/prepare` の予約計算はC-onlyから呼ばない。

`row_send`／`transport_state`／`restore_time`／`cleanup` までこの分岐を通し、旧予算をダミー値で満たす方式にしない。通常のinit/A4等の予算処理はそのまま残す。

ゲストCは送信・承認待ちを除く60秒、送信とS0にも定数締切を持つ。VRAMは行間・DACは色単位・delayは刻みごとの安全点を維持。byte/pollの予約枠や行数からの予算計算は追加しない。

## REALHW_RECOVER_ADDENDUM

- UARTは既存serial設定を参照してTxRDY確認・1byte出力・T2のPITフレーム待ちを使う；SerialFS gate中は開始を拒否。trial送信pollは65536、restoreは既定の独立poll/byte枠、tick停止・送信詰まりでも有限とする。

## REALHW_RECOVER_ADDENDUM_R2

- 枠超過・poll上限到達は最初の原因と段を保存してFAILEDに固定。危険アクセスの再試行なし。残予約内で独立に安全確認できる②だけ続行し、総枠を越えて継続しない。

- 時間制限は同期バス停止を解除しない。ログ不通時は有限待ちで送信を諦め欠落数を保持し、後始末を妨げない。FAILEDをS0成功だけで解除しない。

## REALHW_RECOVER_DESIGN

- 期限は整数 tick 差分 (wrap 安全、予算は半周未満)、全体と opcode ごとの上限・最大命令数・poll 回数を持つ。profile に実測根拠付き上限を固定し、無期限指定を認めない。

- IRQ 内は期限超過フラグを立てるだけ。復帰の MMIO/表示/ログを IRQ/NMI 内から行わない。irq_save は窓の最小操作だけで、script 全体を囲まない。
