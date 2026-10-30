# GF6200_RECLOG — 復元の段の REC の送信の上限と診断 — 設計

> 状態: **現行 (2026-10-27)** — 確定 (astra 起草・Opus 2 往復・PM が N1〜N4 を反映)。段の状態は票 TASK_HWDEV が持つ。
> 出所: ~/os32-tmp/run/gf-a4-followup/RECDIAG.final.md を 2026-10-27 に写した。前の調べは同じディレクトリの RECLOG.md・REC4.md、レビューの要旨は REVIEW_RECDIAG.r1.md。末尾の「確定」が本文の該当の行より優先する。

r1→r2 の変更 F1: GF cleanup の最悪量から復元の byte・時間・poll 枠を算出し、S0 予約を維持。
r1→r2 の変更 F2: 入れ子許可表を廃し、LIFO の phase・識別欄一致で検査。
r1→r2 の変更 F3: recover status に RAM 診断を別の1行で表示。
r1→r2 の変更 F4: tx_fail 後は既存出口へ進み、rc と出口を対応付ける。
r1→r2 の変更 F5: fn/where の綴りを統一し NONE を定義。
r1→r2 の変更 F6: diag 後に final を構築し txd 1欄を追加、全行の len/eor は撤回。
r1→r2 の変更 F7: REC 完全文字列の host 試験・差し替えを追従。
r1→r2 の変更 F8: 9600bps の所要時間と RAM・成果物サイズを受入に含める。
対象: RECLOG.md §3。読取木 `/home/hight/os32-v3` main `44b62681`（2026-10-27）。
RECLOG 基準 056db064 から recover.c / recover_profile.c / serial.c / rshell_recover.py の差分なし。
単一利用者・単一タスク・協調切替、攻撃者なし。実装・ビルド・実機操作は本票では未実施。
KAPI・配置世代・共有型/HWDEV契約は不変。ACK/NACK、送信再開、GPU再実行、cleanup全体CLIは範囲外。
## 1. ゲスト: 最初の失敗を RAM に保持し final 直前に一度だけ出す
`kdebug/recover_diag.h` を内部専用に新設。recover.c の static 1 枠（リング・動的確保なし）。
```c
struct rec_tx_diag {                  /* 全て32bit、48 B。共有 recover に追加しない */
    u32 sid, seq, pos, fn; int rc;
    u32 where, sent, polls, bytes, valid, attempted, active;
};
static struct rec_tx_diag txd;         /* recover.c。最初の失敗は上書きしない */
static u32 tx_pos, tx_sent, tx_where;  /* 現在の行送信の文脈、12 B */
/* fn: NONE, READY, PUT, WAIT_FRAME, PIT_WAIT, BEGIN, EMPTY, BUDGET, ROW */
/* where: NONE, BYTE, BUSY_WAIT, AFTER_PUT, FLUSH, SEPARATOR, BUILD */
/* tx_fail(): active && !valid のときだけsid/seq/pos/fn/生rc/where/sent/polls/bytesを保存しvalid=1。記録専用で既存失敗出口を維持。 */
```
`row_send()` が active=1、begin 前 pos=0、各 byte 前 pos=i+1（ASCII の R が1、LFも数える）を設定。
補償 LF は pos=0/where=SEPARATOR、flush は pos=行長+1/where=FLUSH。end 後 active=0。
ready busy 中の wait は where=BUSY_WAIT/sent=0。sent は当該 byte の put 成功を示し、到着証明ではない。
begin/empty 失敗と poll・byte・時間・safe-point の各出口も記録（BUDGET の rc=-31/-32/-33/-34）。
外部 rc は -1 を維持。対応: ready<0→READY、生put非0→PUT、wait非0→WAIT_FRAME/PIT_WAIT は記録後break→既存REC_LOG出口。
begin/empty非0→BEGIN/EMPTY は既存所有解除・lost_lines出口へ。poll=-31/byte=-32→BUDGET記録後break→REC_LOG。
時間=-33/safe-point=-34→BUDGET記録後、既存TIMEOUT等の失敗とcleanup出口を維持。ROW=-35→REC_LOG。生rcは潰す前に保存。
put直前sent=0、成功直後sent=1/where=AFTER_PUT。tx_failを直接returnして既存fail()/end/lost_linesを飛ばさない。
```c
/* drivers/char/serial.c: checked API の署名・import は不変。失敗は outp より前 */
int serial_checked_put(u32 byte) {
    unsigned int f=irq_save(); int ready=serial_checked_ready();
    if (ready!=1) { irq_restore(f); return ready<0 ? -21 : -22; }
    outp(s_port_data,(u8)byte); irq_restore(f); return 0;
}
/* kdebug_main.c: recovery_tx_ready/put は生 rc をそのまま返す（現行通り）。
 * recovery_transport.uart.wait_frame = recover_hal_wait_frame の接続も維持。
 * recovery_tx_end は flush の記録後にも必ず serial_checked_end() を呼ぶ。 */
```
`recover_profile.c:recover_hal_wait_frame()` は -11=設定/引数不正、-12=PIT count範囲外、-13=sample上限。
定数は内部ヘッダで名前を付ける。PIT のラッチ・IRQ区間・待ちアルゴリズムは変えず、rcだけ区別。
これで上位 wait_frame の一般失敗と、その内側の PIT 待ち失敗を特定できる。非UARTの待ちは txd に混ぜない。
セッション開始時だけゼロ化し、PAUSED/cleanup/owner detach では保持。新 sid 開始まで RAM に残す。
`transport_state()` の terminal 分岐で DISCARD 後・final 前に `transport_diag(r)` を必ず一回呼ぶ。
例: `REC sid=7 seq=16000 phase=diag event=TX fail_seq=15980 pos=72 fn=PUT rc=-22 where=BYTE sent=0 polls=93 bytes=72`
diag は成功時も必須（fail_seq/pos/rc/sent/polls/bytes=0、fn=NONE、where=NONE）。sid は失敗と同一。
attempted を送信前に立てる。diag 失敗でも再帰出力・再試行せず、その後に最新のFAIL/lost_linesでfinalを構築、txd=0/1（最初の送信失敗なし/あり）を付す。
fn/where は表記を全て大文字、正常はNONE。recover status は既存表示とは別の1行 `txd sid=... valid=... seq=... pos=... fn=... rc=... where=... sent=... polls=... bytes=...` をRAMから表示。
診断が出ない final、final 自体の欠落はノートで非成功。final 自身の送信失敗は RAM には残るが同runでの回収保証はない。
## 2. ノート: 行の完全性を検証してから seq/対応表を更新する
各RECの書式は維持し、finalのみtxdを追加。全行へのlen/eorは付けない（数値の途中欠けが合法値になる破損は単独では検出不能）。
row_text/row_number の黙った切詰めを廃止: rec_row にoverflowを設け、既存384 B枠とLF/CR分の境界を検査。
overflow は送信せず ROW/-35、lost_lines++、REC_LOG。最大長計算・改行込みの境界試験を必須にする。
`Records.line()` は ASCII、重複/未知key、裸token、未知phase/event、数値範囲、必須欄を検証する。
共通必須: sid/seq/phase/event。u32 は0..4294967295、rcはs32、boolは0/1、enumは既知値のみ。CRLFは末尾CR一個のみ除外。
| phase / event | 共通以外の必須欄 |
|---|---|
| state / IDLE,SNAPSHOTTING,ARMED,RUNNING,PAUSED,RESTORING | version,profile_crc,plan_hash,snapshot_crc |
| snapshot,access,restore,decode,screen,route / BEGIN | step,addr |
| 同 / END | rc,elapsed |
| gf_pre / BEGIN | main,pc,opcode,exec,substep |
| gf_pre / END | 上記5欄,rc |
| gf_bus / BEGIN | pc,bar,off,width,old,new |
| gf_bus / END | pc,bar,off,width,readback,rc |
| gf_verify / END（単独結果） | saved_words,minimal,capacity_known,full_capacity,capacity_bytes,tested_bytes |
| receive / DISCARD（単独） | discarded_bytes |
| diag / TX（単独、一回） | fail_seq,pos,fn,rc,where,sent,polls,bytes |
| final / END（単独、一回、最後のREC） | result,target,verified,unknown,override,saved,actual,trial,lost_lines,discarded_bytes,txd |
virtual 専用 inject/BEGIN は step,addr の単独通知として明示登録（実機A4では出現をエラー）。
通常の BEGIN/END は LIFO スタック。親子の固定許可表は使わず、gf_bus⊃access、screen⊃screenも同じ規則で扱う。
END は phase 一致、gf_pre は5識別欄、gf_bus はpc/bar/off/width一致。通常ENDは識別欄が無いのでphaseと順序で照合。
PAUSED/final で未閉鎖、孤立END、取り違え、final後のREC、複数finalはエラー。単独結果をpopしてはいけない。
sid/seq連続性・metadata freeze は現行条件を維持。検証済み行だけ状態へ反映し、stack と sticky invalid も保存する。
初回エラーは保存状態にも固定し、後続正常行や次の起動で消さない。旧保存状態は拒否。mainの状態読込/検証もtry内でexit=1へ統一。
`exchange()` は EOT/期限時の非空行バッファを検査し `recover: truncated line before EOT/deadline`、exit=1。
不正行は `recover: malformed REC sid=S seq=Q field=F`。
対応不整合は `recover: unmatched BEGIN/END sid=S seq=Q phase=P`、seq差は現行missing RECメッセージ、全てexit=1。
行過長も sticky error にしてLFまで捨てつつ raw は保存。最初のエラーを後続エラーで上書きせず、既存期限内でEOTまで排出。
OKには全検査通過、空stack、必須diag、現行success条件に加えtrial=0、final.txd=0とdiag.fn=NONEを要求。FAIL/PARTIAL/欠落はexit=1。
PAUSEDは完全な非終端応答として従来通りexit=0/表示PAUSEDを維持するが、A4合格やresult=OKとは扱わない。
## 3. 有限再試行の判断
追加しない。recover_uart_byte はready==0を既にframe待ち＋累積poll/時間上限付きで再検査する。
put=-22 は無送信と分かるが、今回の失敗がここだった証拠も再試行回数の根拠もない。先に観測を取る。
ready負値・PIT失敗・put後wait失敗は即中断。失敗後の補償LFと後続cleanupは現行通りで、同byteを再送しない。
## 4. host受入・変異（実装時に行うもの）
recover_host / serial checked / PIT 偽I/Oで、ready<0・put=-22・wait一般失敗・PIT各rc・begin/flush・各予算失敗を注入。
行の先頭/event末尾/LF、補償LFで注入し、diagのfail_seq/pos/fn/rc/where/sent、最初の失敗保持、lost=1を照合。
put後wait失敗でput一回、endによる所有解除、S0継続、FAIL保持を確認。diag/final失敗、RAM保持、次sidリセットも試す。
test_rshell_recover.py は実形式fixtureへ更新。正常入れ子・PAUSED継続・CRLF・1byte/256byte境界で成功。
必須欄の削除、数値範囲外、REC4型event途中切断、prefix/LF/行全体の削除、重複/逆順/孤立END、EOT残片をexit=1にする。
後続finalがOKでも不正を覆さず、rawはEOTまで一致、破損状態の再読込でも非成功。正常FAIL/PARTIALもexit=1を確認。
変異: tx_fail無効化/posずらし/rc潰し/初回上書き/put再送、必須欄免除/pop削除/EOT残片無視を各試験が殺す。
A4のS0前65行約4283 B・全89行約5.8 KBとS8 abort約3.6 KBを再現し枠内で欠けなし。最悪経路・最大桁・CR/LF込みも検証。
枠+1 BならREC_LOG・lost>0・非成功、S0継続。変異: 旧4096枠復帰、予約侵食、REC_LOG/lost無効化、diag前final構築、識別欄照合削除を検出。
完全文字列追従: recover_descriptor_host.inc/gfinit_exec_host.c/test_h3_park_resume.py/recover_gf_host.c/gfinit_pll_host.c/gfinit_bus_host.c/test_np21w_ini_live.py。
差し替えrecover_virtual_host.c:71-72・kdebug_init_host.c:89も更新し、status・final・正常NONEとdiag失敗を照合。
実装者は関連hostをMUTATE=0、check_select.py --lint、対応ビルド。PMがレビュー後に上記変異を実行。今回のPASS主張はない。
## 5. 大きさ・予算
既存成果物（鮮度未保証）のimage/mem: kdebug=121092/135168 B、hwdev=93664/90112 B。
tools/module_schema.py上限は各image/memがkdebug 262144 B、hwdev 196608 B、load spanは524288/262144 B。
見積: kdebug code/文字列/reloc +2～4 KiB、static RAM 60 B、rowスタック +4 B。serial常駐増分 <256 B、hwdev本体増分0想定。
共有型を変えないためHWDEV layoutは不変だが、kernel/core pair CRC更新に伴いhwdevも再生成し、image/mem/load spanを実測する。
F1根拠: PM集計のS0前65行4218 B（CR込み約4283 B）は現行8192−4096を超える。全89行5676 B（CR込み約5765 B）、S8 abort 3615 B。
決定: 一律定数増ではなくGF profileのcleanup経路から算出。各経路の行数Nと最大行長L（数値最大桁・CR/LF込み）でP=maxΣ(N×L)をS0前に割当。
対象はRESTORING通知、quiesce、保存復元、zero guards、失敗時の通知/補償LFを含む全分岐。観測65行は下限資料であって最悪証明ではない。
S0以後もscreen・DISCARD・diag・final(txd込み)・補償LFからQを計算し、S=max(4096,Q)、restore_log_bytes=P+S、s0_reserve_bytes=Sとする。
recover_profile.c:62,89の固定枠と適合検査をGF用算出値に対応させる。gf_budgetの実行ログ用rows/log_limitだけを増やしても復元枠は直らない。
cleanup開始でuart_bytes=0、S0移行でも累積を維持し、S0前P/以後P+Sという既存比較を使用。独立カウンタ追加は不要、S0予約は消費させない。
時間: frame=ceil(10×10^6/9600)=1042µs。4283 Bで約4.46秒、5765 Bで約6.01秒、8192 Bで約8.54秒、16384 Bなら約17.08秒（実行・busy待ちは別）。
段ごとT(B)=ceil(B×frame/10^6)+ceil(実行ms/1000)+max(3,ceil(送信秒/2))を計算、restore_limit=T(P)+T(S)、s0_reserve_ticks=T(S)をtick化。
固定RUN 18秒のままbyteだけ増やさない。recover.c:802-808相当のput/wait前後とrestore_timeの時間検査を維持し、GF実行ログの除外時間と混同しない。
poll枠も各段のbyte・wait・safe point・有限busy余裕から算出して合算/予約し、transport.max_pollsとの整合を検査。u32積/和・half ticks超過は開始前拒否。
追加通信はdiag最大256 Bとfinalのtxd 1欄のみ、statusは要求時だけ。上限自体はbuffer確保なし、既存profile欄を使いRAM増なし（診断60 B・overflow4 Bは上記）。
算出コードのサイズ増分はビルドで実測。上限を超える量/実装漏れは従来通りREC_LOG・lost_lines・FAIL、時間切れも非成功とし、成功へ丸めない。
## 6. 次の実機A4での判定 / 7. 未決
承認済みcold-required手順で対の版・CRCを確認しraw採取。欠けが再発したらdiagで関数/rc/seq/文字位置を特定して非成功に固定。
diagも失われた場合は新sidを開始せずrecover statusのRAM診断を採取し、回収不能なら原因未確定・再調査とする。PIT失敗だけからIRQ原因とは断定しない。
4a判断はGF6200_4A_DESIGN.md §7のPFB/PLL一致、VRAM最小試験、容量既知/不明、BAR復元、50h/54h=0、98テキスト維持を別記。
全mainの完全な対応ログ、予期しないDENY/timeoutなし、終端S0 verified=1が揃えば試験と復帰の合否を判定できる。
ログ欠け・未閉鎖・lost>0・FAIL/PARTIALは総合非成功。cold-required等のPARTIALをresult=OKへ緩和せず、部分成果と分ける。
今回の欠けたENDを後続kpeekで補完せず、新runの証拠だけで判断する。4bへ自動進行しない。
未決（ユーザー判断が必要な設計事項）: 0件。実機の実行承認は従来の段別手順で行う。
反論 F1: r1の「上限を増やして通さない」は撤回。実測が予約差引枠を超えており、最悪量に基づく増枠が必要。
反論 F6: len/eor撤回で任意の通信破損の完全検出は保証しない。予算欠けはゲストのsticky失敗とfinal.txd、ノートの構造検査で非成功にする。

## 確定 (2026-10-27、Opus 5.5 の第 2 回 (軽い要修正) を PM が反映 — 本文の該当の行より優先する)

- **N1**: §5 の P (S0 の前の上限) の算出に、**local cleanup の行数の上限**を明記する — 行数は実行時の `local_saved_count` で決まり (kdebug/gfinit_bus.c:96-125、kdebug/recover.h:318、上限 RECOVER_MAX_REGS=32)、CR1F は k=0 と k=3 で 2 回通り、1 回の訪問で SELECT と DATA の 2 アクセス、それぞれが書込みと読み戻しの BEGIN/END を出しうる → local の最悪 = 32 × 2 回 × 2 アクセス × 2 組 × 2 行。profile の数 (regs・quiesce_count・index_save_count) だけで作らない。CR1F の 2 回訪問を含む host 試験を 1 本足す。今回の A4 で S0 の前の 65 行のうち 48 行 (seq 15916〜15963) がこの local cleanup。
- **N2**: 算出した restore_limit の秒数を、実装の段の報告と票に書く。§6 の手順で、ノートの `--cleanup-timeout` (既定 40 秒、tools/rshell_recover.py:199) を `restore_limit + 余裕` 以上に、`--timeout` も計算し直すことを必須にする。最悪の秒数が 60 秒を超えるなら、実装の前に PM が値をユーザーに示す。
- **N3**: poll の枠の式: `restore_max_polls ≥ P + S (byte の数) + busy の余裕 + safe point の数` (recover.c:796-797 で 1 byte ごとに 1 増える)。
- **N4**: 上限を変えると profile_crc が変わる (recover.c:51-54)。次の A4 は新しい profile_crc を基準にし、「CRC 群は A2 から不変」はその新しい値で見る。
- 裏付け: 第 2 回のレビューは、cleanup の始めに uart_bytes が 0 に戻ること (PAUSED の行を数えない) から、15916〜15979 の行と切れた行の 39 B の和が約 4093〜4096 B で、上限 4096 B にちょうど当たったことを確かめた。ゲストの送信は LF だけ。

## 実装の前に決めたこと (2026-10-27、ユーザー決定)

- sol の算出: local cleanup の容量の最大 (RECOVER_MAX_REGS=32) で数えると 512 行 × 94 B = 48,128 B、9600bps で約 51 秒、余裕と S0 を足すと restore_limit は 87 秒以上 (部分集計、~/os32-tmp/run/reclog/budget-stop.json) で N2 の 60 秒を超えた。
- **ユーザー決定 (推奨どおり): 上限は容量の最大ではなく、その回の実際の行数から計算する** — cleanup に入るときに、実行時の `local_saved_count` (と quiesce・index の保存の数) から P (S0 の前のバイト数)・時間・poll の上限をその回ごとに出す。式は本文 §5 と確定 N1 の形のまま、容量 32 の代わりに実際の数を入れる。S0 の取り置きは今のまま。今回の A4 の量 (S0 の前 65 行・約 4.3 KB) なら 10 秒台で収まる見込み。
- 算出した値は REC の cleanup の始まり (RESTORING か final) の欄 1 つに出し、ノートはその値で `--cleanup-timeout` が足りるかを確かめる (足りなければ警告)。ノートの既定の待ちは、容量の最大のときの値 (約 90〜120 秒) を上回るように上げる (止まったときに諦めるまでが延びるだけで、普段の速さは変わらない)。

## 実装で埋めた設計の欠け (2026-10-27、設計者 astra — sol が動的な上限と profile CRC の衝突で止まった)

対象: `/home/hight/os32-v3` main、`docs/decisions/realhw/GF6200_RECLOG_DESIGN.md` §2・§5・確定N4・末尾決定。以下を該当記述より優先する。
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
REC: `transport_state()` の `phase=state event=RESTORING` 行だけに必須u32欄 `cleanup_seconds=cb.seconds` を1つ追加し、当該行の最大長もPに含める。
ノートはこの欄をmetadataの4欄から分離して検証・保存し、`--cleanup-timeout < cleanup_seconds + 余裕` を警告する。freeze条件は変更しない。
待ちの既定値は容量最大で求めた秒数＋余裕を上回る値にし、--timeoutも整合させる。実効秒数と既定待ち秒数は実装報告に記す。
host受入（ゲスト）: 同一profileでlocal件数0/実数/32・CR1F二訪問を比較し、cbだけ変化、CRC/token不変、境界+1のbyte/poll/tickで非成功・S0継続、再入不変・次run初期化・算術拒否を確認する。
host受入（ノート）: PAUSED保存→再読込→RESTORING→finalで同一CRCとcleanup_secondsを受理し、不足待ちを警告、欄欠落/不正値とCRC変更は拒否、既存diag/final・LIFO検査を維持する。
§5の「上限は既存profile欄を使いRAM増なし」を撤回し、上記28 Bをサイズ実測へ加える。本追記は設計のみ、リポジトリ変更・試験実行なし。
