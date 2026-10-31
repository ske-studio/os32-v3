# kdebug/recover_virtual.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: RECOVER_T4B_INJECT.md (D1 で移した)
ソース: `kdebug/recover_virtual.c`

## RECOVER_T4B_INJECT

- `kdebug/recover_virtual.c` に装置非依存の仮想試行を置く。核である kdebug に属し、hwdev.mod・GF・PCI 探索を必要としない。KDEBUG=1 のみリンクする。

- shell は `recover virtual begin <case>` / `recover virtual step <session>` / `recover virtual end <session>` を追加。case は固定列挙、一命令を承認として扱う。callback の shell 再入なし、RUNNING 中止は既存 abort フレーム。

- begin は IDLE/VERIFIED かつ既存 latch が許すときだけ。step/end は同じ仮想 owner/session のみ、二重 begin・別 session を拒否する。GF 側の dispatch・所有者照合は現状を維持する。

- profile は expected_bdf/device=0、regs/quiesce/rules=空、cold_required=false。① snapshot/restore/verify は空集合として完了し、GPU 状態の証明とは呼ばない。

- profile_ok が要求する未参照の合成 target を1件（PCI_CFG の論理 offset 0、read_safe）持つ。read/write は常に拒否し、実 PCI/MMIO/I/O へ接続しない。

- identity は boot/image/kdebug の固定識別と仮想装置欄0、raw_config は RAM 上の Command=RECOVER_COMMAND_MEM、BAR全0を返す（raw_matches の Mem 条件を満たす合成値）。start_ready は既存 CUI・route 読取り確認を委譲する。

- 最小 plan は PREPARE の無アクセス node 1個、argc=0、dir_substep=NONE、rule/expr=NONE、両 next=NONE。bind 検証を通し、step は enter→leave する。

- ② route/screen_safe/screen_restore_verify は既存 S0 ops へ委譲する。0FACh 書込みは許可しない。表示復帰と予約セル往復は実装置に触れるが、CUI・98 route 条件下で GPU/DMA は変更しない。

- 終端と開始失敗で transport/context を元へ戻し、owner_configure(NULL,...) で借用を解く。結果/session/seq/FAILED は保持する。FAILED 後の次 case は新起動で行う。

- 全 case の仮想 transport は UART 累積上限4096 polls、実 frame_us・begin/put/end を使う。静的最大の開始〜step ログが4096未満、復帰全列が4096B以内を開始前に検算する。

- trial-expire は実 tick のまま、operation 内で既存の有限1ms待ちと safe_point を反復して session の8秒枠を使い切る。時計・spent・reason は細工しない。opcode期限単独は既存 T1 host 試験に任せる。

- poll-stop/txbusy-stop の operation 内だけ ticks を凍結：通常 `virt=real−offset`、凍結中は開始値を返し、解除時 `offset += real_now−real_at_freeze`（u32差分）。OS tick/IRQ/PIT は止めず、local_cleanup 冒頭で解除し restore_tick との連続性を保ち、以後の復帰は実経過で数える。

- `phase=inject event=BEGIN step=<case_id> addr=0` は既存 recover_record で故障を有効化する直前に送る。case_id は normal=0、fault=1、trial-expire=2、poll-stop=3、txbusy-stop=4。

- inject END と終端フックは追加しない。最終 REC は既存 final END。凍結区間の tick差0、反復回数、解除後の時計連続性は host harness で証明する。

- Tx 注入は inject BEGIN 成功後の専用1行だけ、ready のみ差替える。put にダミー成功を返さず、実 UART の設定や TxRDY bit は変えない。

- transport.end は失敗でも checked 所有を解放し、その行の注入を解除する。local_cleanup も解除を保証する。cleanup が UART 累積値を0へ戻す既存動作で②のログを送れる。

- 仮想 identity の boot/image/kdebug は保存しない RAM 試行の固定値1とし、装置欄は0、開始条件は借用元の start_ready と route_read の両方へ委譲する。

- kdebug/recover.c:1103-1105: 仮想の identity は実装置と違うので、virtual begin のたびに GF の SIZE_BARS の BAR cache が 0 に戻る (後の GF PREPARE は再測定 — 安全側)。仕様として受け入れる。

- fault は begin で case_id を保存し (step 要求の case_id では上書きしない)、step の plan enter 後に既存 recover_record で inject BEGIN を送ってから callback が -1 を返す。

- reason・FAILED・S0 復帰は既存 trial_step/cleanup に任せる。

- 試行ログの静的上限に inject BEGIN 1 行と flush 1 回を加算する。

- C3再確認：recover_budget_apply の wait_ms/needed は呼出側の復帰作業量だけを検算し、試行待ちは数えない。trial-expire は bind 前に trial_limit=8秒×実HAL Hz (先に通った opcode 60秒の換算より小さく溢れない) にして CRC を再確定し、opcode60秒・復帰10秒等は既存 S0 予算を保つ。

- C1の待ち関数は virtual_wait_tick：owner の ticks が1 tick進むまで待ち、その後に recover_safe_point を呼ぶ。時計・spent・reason は変更しない。

- poll-stop は bind 前に max_polls=64 を設定して CRC を確定し、inject BEGIN の直後に virtual_freeze で recover の時計だけを凍結する。operation は safe_point を反復し、既存の poll 上限で cleanup へ進む。

- virtual_local_cleanup の冒頭で offset += real_now−freeze_real として解除し、借用元の local_cleanup へ委譲する。凍結中に採った restore_tick と解除直後の virt は一致し、以後は real−offset で復帰の実経過を数える。

- txbusy-stop は inject BEGIN の成功後に時計を凍結して plan を正常に leave し、続く既存の state PAUSED 行1行だけ ready=0 にする。新しい REC 行は足さず、put・実UART設定は借用元のままとする。

- virtual_tx_end は失敗時も借用元 end を呼んで所有を解放した後、virtual_tx_clear で注入を解除する。local_cleanup の冒頭でも解除し、既存のUART累積値リセットで復帰ログを送る。
