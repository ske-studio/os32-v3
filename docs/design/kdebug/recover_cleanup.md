# kdebug/recover_cleanup.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4C_DESIGN.md・GF6200_RECLOG_DESIGN.md (D1 で移した)
ソース: `kdebug/recover_cleanup.h`

## GF6200_4C_DESIGN

cleanup予算は既存のlocal→quiesce→screenを保ち、Cで増えるlocal保存対象を含む実対象数でP/Qを再計数、S=max(4096,Q)・byte/time/poll予約とCRC不変を維持。hostは算定Tlog/cleanup_secondsで待ちを設定し、9600bpsの既定60秒を流用しない。

## GF6200_RECLOG_DESIGN

決定: 一律定数増ではなくGF profileのcleanup経路から算出。各経路の行数Nと最大行長L（数値最大桁・CR/LF込み）でP=maxΣ(N×L)をS0前に割当。

対象はRESTORING通知、quiesce、保存復元、zero guards、失敗時の通知/補償LFを含む全分岐。観測65行は下限資料であって最悪証明ではない。

S0以後もscreen・DISCARD・diag・final(txd込み)・補償LFからQを計算し、S=max(4096,Q)、restore_log_bytes=P+S、s0_reserve_bytes=Sとする。

cleanup開始でuart_bytes=0、S0移行でも累積を維持し、S0前P/以後P+Sという既存比較を使用。独立カウンタ追加は不要、S0予約は消費させない。

段ごとT(B)=ceil(B×frame/10^6)+ceil(実行ms/1000)+max(3,ceil(送信秒/2))を計算、restore_limit=T(P)+T(S)、s0_reserve_ticks=T(S)をtick化。

固定RUN 18秒のままbyteだけ増やさない。recover.c:802-808相当のput/wait前後とrestore_timeの時間検査を維持し、GF実行ログの除外時間と混同しない。

poll枠も各段のbyte・wait・safe point・有限busy余裕から算出して合算/予約し、transport.max_pollsとの整合を検査。u32積/和・half ticks超過は開始前拒否。

上限を超える量/実装漏れは従来通りREC_LOG・lost_lines・FAIL、時間切れも非成功とし、成功へ丸めない。

cleanup に入るときに、実行時の `local_saved_count` (と quiesce・index の保存の数) から P (S0 の前のバイト数)・時間・poll の上限をその回ごとに出す。式は本文 §5 と確定 N1 の形のまま、容量 32 の代わりに実際の数を入れる。S0 の取り置きは今のまま。

決定: profile の復元上限6欄は **既定値**。GF のその回の算出値を制限する天井ではなく、非GF/S0単独の従来経路にも使う。

profile・profile_crc・token・metadata freeze は一切書き換えない。CRC の対象から既存欄を除外する変更もしない。

`recover_budget_apply()` は従来どおりセッション開始前の既定値設定のみ。cleanup から呼ばない。N4 は既定値そのものを変更した版だけに適用する。

内部予算は recover.c の単一所有者用 static 枠。共有 `struct recover` / profile / ops の配置・署名は変えない。

```c
struct rec_cleanup_budget {             /* recover.c 内部専用、単位は名前どおり */
    u32 ticks, polls, bytes;
    u32 s0_ticks, s0_polls, s0_bytes;
    u32 seconds;                       /* ceil(ticks / tick_hz)、REC用 */
};
static struct rec_cleanup_budget cb;    /* 28 B追加。動的確保なし */
static int cleanup_budget_prepare(struct recover *r); /* 下記の算出・検査 */
/* cleanup(): 既存の再入拒否の直後、local_cleanup/最初のsafe pointより前 */
/* prepareはまずprofileの6欄をcbへコピーし、GF時だけ検査済み候補で置換する。 */
if (cleanup_budget_prepare(r)) {
    fail(r, REC_CONTRACT, REC_PHASE_RESTORE, 0); /* 既定値で有限cleanupを続行 */
}
/* 続いてRESTORING設定、restore_tick取得、既存3カウンタを0化。 */
/* state(r, REC_RESTORING)をlocal_cleanupより前へ移し、従来位置から削除。 */
/* recover_uart_byte(): recover.c:785-808相当 */
u32 bytes = cb.bytes - (r->in_s0 ? 0 : cb.s0_bytes);
if (r->uart_bytes >= bytes) break;       /* REC_RESTORING時のみ */
/* restore_time(): put/wait前後・restore_pointから従来どおり呼ぶ */
u32 limit = cb.ticks - (r->in_s0 ? 0 : cb.s0_ticks);
u32 polls = cb.polls - (r->in_s0 ? 0 : cb.s0_polls);
/* tick差 < limit && r->restore_polls < polls の既存比較へ渡す。 */
```

`cleanup_budget_prepare()` はGFのRUN/SIZE_BARSで、入口のlocal_saved_count・saved_count・実際のquiesce/index保存対象を読んで一度だけ算出する（容量32を代入しない）。

その対象集合から出しうる行数を数える。送信済み行数による後追いではない。CR1F二訪問・失敗分岐・補償LF・移動後RESTORING行を含める。

§5のP/Q/S・T・N3の式を内部候補へ適用し、cb.bytes=P+S、cb.s0_bytes=S、時間/pollも前段とS0の和・予約で保持する。

S0予約の規則S=max(4096,Q)と時間/poll予約を維持。local_cleanupが保存数を減らしても再計算しない。S0移行でもカウンタを戻さない。

u32積/和、予約≤合計、時間<HALF_TICKS、有限pollを候補公開前に検査。初期化失敗時は候補を部分適用せず、既定値のS0予約で復帰を試み、成功扱いにしない。

全許容入力での算術成立とtransport容量は開始前に検査する（容量最大の計算は検査専用で、その回の実効上限に採用しない）。

`transport.max_polls` は開始前に最大候補を収容できる値に設定・検証しcleanup中は不変。UARTの引数max_pollsとの既存比較も残す。

実効poll制限はbyte/flushともrestore_point→restore_timeのcb比較が担う。時間/byte/pollの失敗出口、lost_lines、S0予約は既存契約を維持する。

cbは全cleanup入口（S0単独・部分snapshot・abort含む）で初期化し、終端diag/final送信まで保持。再入時は上書きせず、別runでは必ず作り直す。
