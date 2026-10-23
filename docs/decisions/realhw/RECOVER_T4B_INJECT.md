# T4b 注入口の設計追記 r2（2026-10-23 PM）
- r1差分：PAUSED 門は kdebug_main.c:110 に残し、仮想 step/end の例外だけ追加。GF 許可判定は移さない。
- 期限は短い固定 profile と実時間待ちだけ。case は normal/fault/trial-expire/poll-stop/txbusy-stop の5種に削減。
- inject END・終端フックを削除し、反復数は host harness で検査。凍結解除は virt=real−offset に統一。
- PAUSED --status の host rc≠0 を期待値へ追加。段は B1=3h、B2=5h の2段に統合（レビュー全7件を採用）。
確定設計 §3〜5・T4 と R2 §1 の追記（実装・受入は未実施）。単一利用者・単一タスク・協調切替、整数演算、9801互換なし。パスは特記以外 `/home/hight/os32-v3/` 起点。

## 1 実物の確認（file:line）
| 実物 | 確認と今回埋める境界 |
|---|---|
| `kdebug/recover.c:174,1087,1212,1240` | boot 寿命の owner、bind 済み plan 必須の begin、承認済み step、共通 end。FAILED を再開始で消せない |
| `kdebug/recover.c:576,1058,1191,764` | profile に target 必須。trial/opcode 期限、step poll 上限、独立 UART poll 上限がある |
| `kdebug/recover.c:953,978,1035,357` | cleanup は①→実 S0 検証、S0 成功と試行失敗は独立。final は `target=S0`、失敗時 `result=FAIL` |
| `kernel/kdebug_call.c:8`、`kdebug/kdebug_main.c:102,110`、`userland/shell/cmd_recover.c:9` | kernel→既存dbg_call。GF分岐後にPAUSED門、shellはstatus/now（op 3/4）のみ |
| `tools/rshell_recover.py:2,34,93,185` | --command は一命令、PAUSED は EOT で帰還。state/log を継続し、初回だけ ver。欠落・timeout は非成功 |
| `docs/decisions/realhw/REALHW_RECOVER_DESIGN.md:182`、`REALHW_RECOVER_ADDENDUM_R2.md:26`（同ディレクトリ） | 仮想失敗→S0 と NP21 用予算は既定だが選択・注入口が欠ける |
| `docs/tasks/DEFERRED_TESTS.md:54`、`docs/decisions/realhw/HWDEV_DESIGN.md:144` | 仮想試行・送信詰まり・終端不着を受入、PAUSED 門除去を変異とする |
- `~/os32-tmp/evidence/2026-10-23/recover_t4b/RESULT.md` 末尾「PM が回した分」を採用：(1) now 0/PARTIAL/98 復帰は合格済み。冒頭の旧「未確認」で上書きしない。

## 2 仮想 profile と選び方
- `kdebug/recover_virtual.c` に装置非依存の仮想試行を置く。核である kdebug に属し、hwdev.mod・GF・PCI 探索を必要としない。KDEBUG=1 のみリンクする。
- shell は `recover virtual begin <case>` / `recover virtual step <session>` / `recover virtual end <session>` を追加。case は固定列挙、一命令を承認として扱う。callback の shell 再入なし、RUNNING 中止は既存 abort フレーム。
- `recover_wire.h` に私有 op `KDEBUG_OP_RECOVER_VIRTUAL=11`（既存 GF 5〜10 の後）と版付き 40B 要求を追加。既存 KAPI・KdebugRecover・slot の変更なし。
```c
typedef struct KdebugRecoverVirtual {
    u32 version, action, session, case_id, reserved[6]; /* 40B、reservedは0 */
} KdebugRecoverVirtual;
/* begin: configure → bind_plan → trial_begin(p, hash, p->trial_limit)
 * step: trial_step(owner, 保存token, true, virtual_operation, context)
 * end: trial_end(owner, 保存token, REC_NORMAL); now は既存口を使う。 */
```
- begin は IDLE/VERIFIED かつ既存 latch が許すときだけ。step/end は同じ仮想 owner/session のみ、二重 begin・別 session を拒否する。GF 側の dispatch・所有者照合は現状を維持する。
- PAUSED 門は kdebug_main.c:110 の位置のまま。既存 now 例外に `op==11 && action∈{step,end} && 仮想owner` だけ追加（payload のサイズ・版を先に照合）。GF 5〜10 は従来どおり :105 で hwdev へ渡し、GF_OP_ABORT 等の許可表を kdebug に持ち込まない。
- profile は expected_bdf/device=0、regs/quiesce/rules=空、cold_required=false。① snapshot/restore/verify は空集合として完了し、GPU 状態の証明とは呼ばない。
- profile_ok が要求する未参照の合成 target を1件（PCI_CFG の論理 offset 0、read_safe）持つ。read/write は常に拒否し、実 PCI/MMIO/I/O へ接続しない。
- identity は boot/image/kdebug の固定識別と仮想装置欄0、raw_config は RAM 上の Command=RECOVER_COMMAND_MEM、BAR全0を返す（raw_matches の Mem 条件を満たす合成値）。start_ready は既存 CUI・route 読取り確認を委譲する。
- 最小 plan は PREPARE の無アクセス node 1個、argc=0、dir_substep=NONE、rule/expr=NONE、両 next=NONE。bind 検証を通し、step は enter→leave する。
- ② route/screen_safe/screen_restore_verify は既存 S0 ops へ委譲する。0FACh 書込みは許可しない。表示復帰と予約セル往復は実装置に触れるが、CUI・98 route 条件下で GPU/DMA は変更しない。
- 終端と開始失敗で transport/context を元へ戻し、owner_configure(NULL,...) で借用を解く。結果/session/seq/FAILED は保持する。FAILED 後の次 case は新起動で行う。
- [C4]：wire 定数は subsystem の recover_wire.h、共通上限は recover.h、仮想 case・秒数・poll 値は recover_virtual.c の名前付き local 定数。Shared Layer 追加なし。

## 3 注入の口と合格の REC 行
以下は追加する固定予算行（実測値ではない）。秒→tick は実 HAL Hz の checked 切上げ整数換算、CRC 確定後は変更しない。
| case / profile 行 | trial / opcode / max_steps / max_polls | 復帰・送信枠 |
|---|---|---|
| normal、fault、txbusy-stop | 60秒 / 60秒 / 4096 / 65536 | 復帰10秒・16384 polls・4096B、S0予約0（既存 S0 行） |
| trial-expire | 8秒 / 60秒 / 4096 / 65536 | 同上。短い trial 行だけ追加 |
| poll-stop | 60秒 / 60秒 / 4096 / 64 | 同上。仮想 step 専用の短い poll 行 |
- 全 case の仮想 transport は UART 累積上限4096 polls、実 frame_us・begin/put/end を使う。静的最大の開始〜step ログが4096未満、復帰全列が4096B以内を開始前に検算する。
- trial-expire は実 tick のまま、operation 内で既存の有限1ms待ちと safe_point を反復して session の8秒枠を使い切る。時計・spent・reason は細工しない。opcode期限単独は既存 T1 host 試験に任せる。
- poll-stop/txbusy-stop の operation 内だけ ticks を凍結：通常 `virt=real−offset`、凍結中は開始値を返し、解除時 `offset += real_now−real_at_freeze`（u32差分）。OS tick/IRQ/PIT は止めず、local_cleanup 冒頭で解除し restore_tick との連続性を保ち、以後の復帰は実経過で数える。
- `phase=inject event=BEGIN step=<case_id> addr=0` は既存 recover_record で故障を有効化する直前に送る。case_id は normal=0、fault=1、trial-expire=2、poll-stop=3、txbusy-stop=4。
- inject END と終端フックは追加しない。最終 REC は既存 final END。凍結区間の tick差0、反復回数、解除後の時計連続性は host harness で証明する。
| NP21/W での起こし方 | 合格となる trace（`REC sid=N seq=…` 共通） |
|---|---|
| normal begin→step→end | SNAPSHOTTING→ARMED→RUNNING→PAUSED、step で RUNNING→PAUSED、end で RESTORING→final `result=OK target=S0 verified=1 unknown=0 trial=0 lost_lines=0` |
| fault の step callback が -1 | `inject BEGIN step=1`→RESTORING→final `result=FAIL verified=1 unknown=1 trial=3 lost_lines=0`。FAULT/例外命令ではなく REC_OPERATION |
| trial-expire の step | `inject BEGIN step=2`→実時間待ち→RESTORING→final `result=FAIL verified=1 unknown=1 trial=2 lost_lines=0`。8秒の trial が60秒の opcode より先に尽きる |
| poll-stop の step が safe_point を反復 | `inject BEGIN step=3`→RESTORING→final `result=FAIL verified=1 unknown=1 trial=2 lost_lines=0`。入口1＋operation内64＝65 polls は host で検査 |
| normal PAUSED で status/peek/別begin | 新しい REC は出ない。status は `recover: rejected`＋EOT、本文が rshell_recover.py:174 の正規表現に合わず host rc≠0 が期待。他も拒否応答。次の step/now の seq・CRC は連続、待機時間は非課金 |
| normal PAUSED で now N | RESTORING→final `result=OK target=S0 verified=1 unknown=0 trial=1 lost_lines=0`。空集合 S1 の OK であり now 0 の PARTIAL と区別 |
| txbusy-stop の step で tick凍結＋専用1行の ready=0 | `inject BEGIN step=4`→行欠落→RESTORING→final `result=FAIL verified=1 unknown=1 trial=9 lost_lines=1`。seq欠番1、host rc=1。ready回数＝4096−詰まり直前uart_polls は host で検査 |
| host が final 行を読まない / EOT を読まない | guest の final は送信可能でも host は成功にしない。期限後の abort は最大1回、cleanup期限で rc=1。guest trace は別保存で照合 |
- Tx 注入は inject BEGIN 成功後の専用1行だけ、ready のみ差替える。put にダミー成功を返さず、実 UART の設定や TxRDY bit は変えない。
- transport.end は失敗でも checked 所有を解放し、その行の注入を解除する。local_cleanup も解除を保証する。cleanup が UART 累積値を0へ戻す既存動作で②のログを送れる。
- 全失敗 case は FAILED=7、S0 verified bit=1 と画面・echo alive を照合。復帰自体の失敗、終端欠落、想定外 reason を「注入なので合格」にしない。

## 4 試験と変異・ゲスト手順
- host 試験：実 recovery_call と shell parser を含む harness で全 case の拒否/復帰 trace、無装置アクセス、plan/token/CRC、PAUSED 待機非課金、終端後 latch・transport 復元を一表で検査する。
- 変異：HWDEV_DESIGN:144 の kdebug_main.c:110 の PAUSED 門除去を必ず kill、加えて step失敗無視・trial比較除去・UART/step poll比較除去・host終端必須条件除去を各1件、有限 watchdog 下で kill する。
- host 不着用に `tools/accept/recover_t4b_host.py` を追加：rshell_recover の open_port/exchange を使い、受信を底の raw へ記録してから final 行または EOT だけを監視器へ渡さない。二重 reader にしない。
- フィルタは分割受信に対応し、final行抑止とEOT抑止を分ける。上位log・下位raw・abort回数を保存する。ゲストの送信成功をホストの終端受領と同一視しない。
- NP21/W は KDEBUG=1・31kHz・CUI。下の P は aidebug URL、C は ver の実 Image CRC、D は証拠ディレクトリ。配備が必要な段は既定の停止→配備→起動。各 FAILED case は新起動・新 state/log。PAUSED/EOT 後に拒否命令を逐次送信し、peek等は単一所有helperで応答/EOTを記録する。
```sh
python3 tools/rshell_recover.py --port "$P" --status --log "$D/status.raw"
python3 tools/rshell_recover.py --port "$P" --session 0 --image-crc "$C" --state "$D/case.json" --log "$D/case.raw" --command 'recover virtual begin normal'
# N は case.json の sid。以後同じ state/log、--session N（0のままにしない）
python3 tools/rshell_recover.py --port "$P" --status --log "$D/paused-reject.raw"
python3 tools/rshell_recover.py --port "$P" --session "$N" --image-crc "$C" --state "$D/case.json" --log "$D/case.raw" --command "recover virtual step $N"
python3 tools/rshell_recover.py --port "$P" --session "$N" --image-crc "$C" --state "$D/case.json" --log "$D/case.raw" --command "recover virtual end $N"
```
- now 分岐は別 normal session の PAUSED で --command を省略し既存 now N を送る。fault/期限/poll/Tx は begin の case だけ差替え、step 後の失敗終端で終了する。
- host 不着は別 normal session の end に適用、state/log は begin/step から継続し ver を再送しない。対照のフィルタ無し end は rc=0、フィルタ有りは rc=1 を要求する。
- 失敗注入の rc=1 と製品不合格は区別し、表の REC・後続 --status・PNG 目視・echo を保存する。/api/cmd と COM1 reader は並行しない。
- 段は B1（仮想 profile・入口・normal・PAUSED、3h）、B2（故障・期限・停止時poll/送信の注入3群、host不着フィルタ・変異・台本、5h）の2段。各段で host と対応する NP21/W 証拠を揃える。

## 5 ファイルごとの変更
| ファイル | 追記する実装・資料（本稿では変更しない） |
|---|---|
| `kdebug/recover_virtual.c`・`.h`（新規） | 固定profile/最小plan/合成ops、5 case、停止試験専用wrapper、BEGIN記録。buildはKDEBUG=1のみ、既存サイズ上限を検査 |
| `kdebug/kdebug_main.c` | S0委譲・仮想 dispatch、:110 の例外1つ、全出口 detach。GF分岐・kernel・hwdev.mod は変更不要 |
| `kdebug/recover_wire.h` | 私有 op・40B wire・版/action/case 定数。既存公開 ABI/KAPI 生成物は変更しない |
| `userland/shell/cmd_recover.c`・`cmd_sys.c` | virtual 下位命令の厳密な引数解析・help。KDEBUG=0 は既存 NOSYS、注入なし |
| `tools/tests/` の recover harness・test driver | 実 dispatcher/CLI、予算静的検算、表の trace、門除去を含む変異を登録 |
| `tools/accept/recover_t4b_host.py`（新規）・`recover_t4b.md` | 終端抑止 helper、case ごとのコマンド・期待値・証拠。rshell_recover の成功契約は緩めない |
| REALHW_RECOVER_ADDENDUM・R2、DEFERRED_TESTS | 本決定と固定短予算行、試験完了時だけ証拠リンクを追記。旧版巻戻しは独立残件のまま |

## 6 未決（ユーザー判断が要るものだけ）
- 0件。本追記は既存の無書込み仮想受入の具体化。T5 の0FACh初回書込み承認・GPU実機受入・隔離像巻戻しの判断を前借りしない。

## 確定 (2026-10-23、Opus r2 条件付き確定可、PM が条件を決めた)
- C1 (§3 trial-expire の待ち): 「既存の有限1ms待ち」は recover*.c・kdebug_main.c に無い。実装者は operation の中で owner の ticks (仮想 transport の時計、凍結しない) を見て 1 tick 進むまで待ってから safe_point を呼ぶ形にし、その関数名を本書の末尾の「設計者が決めたこと」に 1 行。safe_point の空回しで max_polls が先に尽きる形にしない (host 試験で reason=TIMEOUT が期限側で出たことを poll 数 < max_polls で確かめる)。
- C2 (§2 :110 の例外): op 11 の分岐は kdebug_main.c:111 の `op != STATUS && op != NOW → kpeek_call` より**前**に置く (PAUSED でないときの op 11 が kpeek へ流れない)。
- C3 (§3 予算の静的検算): recover_profile.c:83 の検算 (wait_ms ≤ E=2000ms) が試行中の待ちまで数えるかを実装の最初に確かめる。数えるなら trial-expire の profile だけ数え方を変えずに済む形 (試行の待ちは trial_limit が上限) を設計者として決めて末尾に 1 行、確定本文を覆すなら止めて報告。

## T4b-1 の実装で設計者が決めたこと
- 私有 wire の不正は `KDEBUG_RECOVER_VIRTUAL_INVALID=-1`、形式が正しい未実装要求は `KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED=-2` とし、KDEBUG=0 の既存 NOSYS=-10 は変えない。
- op 11 の受け口は既存 PAUSED 門の後・kpeek 分岐の前とし、この段では PAUSED の例外を設けない。

## T4b-2 の実装で設計者が決めたこと
- PAUSED 門の例外は T4b-3 に残し、host は実 recovery_call で begin→PAUSED を確認した後、内側の recover_virtual_call を直接呼んで step/end を確認する（外側の op 11 は -1 のまま）。
- 仮想 identity の boot/image/kdebug は保存しない RAM 試行の固定値1とし、装置欄は0、開始条件は借用元の start_ready と route_read の両方へ委譲する。
- normal だけを実装し、他 case の begin は UNSUPPORTED のままとする。終端の借用解除は既存 now 経路にも接続するが、now の受入と PAUSED 門の変更は次段に残す。
- recover_budget_apply の wait_ms は呼出側が渡す復帰の静的作業量で、trial 待機を加算しない。normal は既存 S0 の予算を使い、最大幅 REC 行（復帰11対＋state/final/discard、begin〜stepはstate6行＋discard）を開始前に4096B・UART4096 pollsと照合する。

## T4b-2 の取り込みで T4b-3 へ送る項目 (2026-10-23、Opus レビュー P3)
- kdebug/recover_virtual.c:84-89 と kdebug/gfinit.c:330-332: virtual begin で PAUSED の後に同じ session へ op 10 の GF_OP_ABORT を送ると、PAUSED の門を通らずに trial_end(ABORT) で VERIFIED になり、borrowed=1 で ops が仮想のまま残る (次の op 11 か NOW の detach で戻る、実害は見当たらない)。T4b-3 で門の例外を作るときに、recovery_call の GF 分岐の後にも recover_virtual_detach() を置くか、仮想 owner の PAUSED 中は GF 5〜10 を断るかを決める。
- kdebug/recover.c:1103-1105: 仮想の identity は実装置と違うので、virtual begin のたびに GF の SIZE_BARS の BAR cache が 0 に戻る (後の GF PREPARE は再測定 — 安全側)。仕様として受け入れる。
