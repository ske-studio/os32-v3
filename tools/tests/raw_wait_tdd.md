# T4b-7 / E11-A2b raw KAPI 待機

契約: [TASK_T4_T6_MODULES §7-1 T4b-7](../../docs/tasks/v3/TASK_T4_T6_MODULES.md#7-1-半日1日の受入単位)、同 §3-1 / §3-3。
ゲストの実行結果は PM が採取する。ホストの合格だけでは E11-A2b を閉じない。

## ホストの対照と変異

`make check-raw-wait-host MUTATE=0` は、実ドライバと生成済み公開 wrapper、
SQLite guard と実代替 stack、実 exec の launch/resume 着地を ILP32/qemu で実行する。
足場は `kapi_audit_host.c` / `sqlite_engine_host.c` / `test_exec_r1.py` と共用する。
物理 I/O・入力 IRQ・CR3/iret・辞書 storage の代替部分はそれぞれの足場冒頭を参照。
通常の GUI park と、park 不成立時の STOP 解除は別々に検査する。

`MUTATE=1` は次の 24 件だけを写しに適用し、コンパイル成功と指定アサートでの RED を要求する。
コンパイル失敗・タイムアウト・別アサートの失敗は変異検出の成功に数えない。

| 変異 | 期待する実行時失敗 |
|---|---|
| fixture の SQL を上限超過に戻す | `sizeof(RAW_SLOW_SQL) <= DB_SQL_MAX_BYTES` |
| fixture の SQL に CTE を再導入 | OS32 設定の SQLite prepare 失敗 |
| fixture の SQL を短縮 | 集計結果の対照 |
| fixture の読みを「あ」に戻す | 配布 M 辞書の候補数 0 |
| fixture の log 事前切替を省略 | log 親の正常対照 |
| fixture の log 事前切替エラーを無視 | 切替失敗時に子を起動しない対照 |
| fixture の FEP 事前検索を省略 | IME 親の正常対照 |
| fixture の CREATE/INSERT を結合 | 単一文 SQL 代替による正常対照の拒否 |
| fixture の INSERT エラーを無視 | INSERT 失敗対照 |
| kbd getchar / getkey の取消削除 (2) | `input CUI abort` |
| GUI fallback の取消削除 | `input GUI fallback STOP` |
| IME getchar / getkey の候補取消削除 (2) | `IME char/key STOP cancels composition` |
| serial 受信の取消削除 | `serial abort` |
| FM 待機の取消削除 | `mml wait abort` |
| FM 最後の Key-OFF 削除 | `mml sustained stop key off` |
| 内部 TX / puts に取消混入 (2) | `USER internal logs and frames never abort` |
| SQLite leave 前に STOP を消費 | `!sqlite_engine_active && !kills ...` |
| 終了処理中の要求を回収済み slot に残す | `reclaimed slot request cleared` |
| 終了処理中の要求を親に渡す | `parent request stays clear` |
| 延長ログの途中に中断を追加 / 要求をクリア (2) | `extended internal log retains STOP until end` |

ゲスト fixture 自身も記録用 KernelAPI で実行し、8 口の選択、予期しない入力・STOP 後の
return の拒否、親の二重回収・pool/USER/SQLite 漏れ・sentinel/result 不一致の検出を確認する。
実際の `RAW_SLOW_SQL` を同梱 SQLite と `os32_sqlite_config.h` で prepare/step し、
512KiB MEMSYS5 内で 4^12 行の集計結果と VM step 数を照合する (VFS のみメモリ用代替)。
IME の固定読みは IPADIC 全件を配布生成器で正規化した後、M のコスト上限・重複排除を
同じ生成器で適用し、候補があることを検査する。変更済み・学習済みのゲスト DB は根拠にしない。

正常対照は各入力口の空待ち→遅延入力→1 回消費、GUI park、serial 通常受信、
FM 1 音の全時間完了＋Key-OFF、内部ログの IF=0/1 両方、SQLite active 中の kill=0 と
leave 後の kill=1、親の exact esp0/caller 復元→次の実 syscall→親 USER heap 不変。
`raw_wait_late_stop_host.c` は実 `exec_finish` と実 AppSlot を使い、資源回収中・私有ページ回収中の
要求を注入する (資源/CR3 操作と IRQ の時刻は代替)。通常終了の結果・回収1回・親の次の安全点・
同じ slot の再利用・新しい STOP の1回消費まで照合する。

## PM のゲスト手順 (64MB、未実施)

[V1] に従い、この木で生成したカーネル・対 kdebug と `/usr/bin/raw_wait_test.bin` を揃える。
配備は PM が停止→配備→起動で行い、ファイル hash と起動像を照合する。
fixture は通常 CPL3、`userland/deploy.yaml` に登録済み。公開 KAPI・配置世代の変更はない。

先行入力を空にし、CUI はローカル shell を使う。rshell の kbd_getchar は既存のタイムアウトで
空白を返すので、無入力 STOP の対照にはしない。serial は SerialFS のセッションを閉じて
`s_gate=0`、接続相手から自動送信しない状態にする。ゲスト serial への入力は物理打鍵と区別する。
IME の通常完了は OFF にして大文字 `Q` を送る。変換取消の読みは「か」に固定する。
CUI の `parent imechar stop` / `parent imekey stop` は、基準採取前に `WARMUP` を表示する。
表示のたびに、ひらがな ON → `ka` → SPACE (候補表示) → ESC 2 回 (変換取消・読み消去)
→ OFF → 大文字 `Q` を入力する。候補リストを Down で展開しない。
同じ検索・取消後の SQLite 使用量が連続一致するまで最低 2 回、最大 4 回繰り返す。
子の `READY` が出てから再びひらがな ON → `ka` → SPACE で同じ「か」の候補を表示し、CTRL+STOP。
GUI でも読みは「か」に固定する。辞書切替で学習内容を削除しない。

| 口 (`entry`) | CUI 正常・STOP | GUI 正常・STOP |
|---|---|---|
| `kbdchar`, `kbdkey` | `raw_wait_test parent <entry> done` / `stop` | GUI 端末内で `raw_wait_test <entry> done` / `stop` |
| `imechar`, `imekey` | 同上。候補表示中の STOP も両口で行う | 同上。WM の FEP 配送と raw cooked 待ちを区別 |
| `serial` | 同上。done は READY 後に受信線へ `Q` | 同上。done は受信線へ `Q` |
| `fm` | 同上。done は 1 音、stop は約 28 秒の連音列中 | 同上。Key-ON 中に STOP |
| `db` | 同上。4^12 行の直積集計 SELECT 中に STOP | 同上 |
| `log` | 同上。既定辞書への reopen 成功ログ中に STOP | 同上 |

`done` は入力口では READY 後に `Q`、FM/DB/log は入力なしで完了を待つ。
`stop` は READY の後に CTRL+STOP。CUI では親に `PASS parent` が出て、さらに shell に戻る。
GUI は端末内で起動し `parent` を使わない (GUI CPL3 の nested exec_run は禁止)。
窓のない fixture を Run から直接起動すると入力先が成立しないため、この手順には使わない。
WAIT_KEY のアプリは
すでに WM に譲っているので、CTRL+STOP 後に WM の選択・終了操作で当該 app を kill する。
serial/FM の実行中 STOP では安全点で WM に戻ることを確認してから同じ終了操作を行う。
GUI の STOP park を採る回は syscall 入口から `APP_RUNAWAY_TICKS` 未満で送る。
それ以後は既存の暴走経路で abort/回収され得るので、その回は park の証拠と混ぜず、
すでに FREE の app を再度 kill しない。
通常完了では `PASS normal`、STOP では `FAIL returned` / `PASS normal` が出ないこと。
通常完了と STOP を交互に 2 周し、次の待機が新しい `Q` を 1 回だけ受け取れることも確認する。

fixture の READY は app ID・USER owner pages/heap・SQLite bytes を別々に表示する。
USER 96KiB と live `:memory:` DB は意図的に app に残す。CUI 親は子の正常/fault/STOP すべてで
reclaim 差=1、phys_free_pages/親 owner_pages/heap 不変、leftover/bad_free 増加=0、SQLite 使用量の
事前検索後の基準復帰、親 sentinel と次の mem_stat/DB 呼出しを照合する。
`raw_wait_test parent fault done` も実行し、#UD 後の親復帰を対照にする。
`log` の CUI 親は既定 M 辞書への切替を一度成功させてから SQLite と MemStat の基準を採る。
切替失敗なら子を起動せず FAIL (終了値 1)。初回常駐増分を子リークとして数えず、子の後の増分は許容しない。
GUI は親がないので、`raw_wait_test log done` を事前に一度完了させてから基準を採る。

### カーネル側の観測 (各 entry の前後)

現ビルドの ELF/map でシンボルを解決する。アドレスや構造体の offset を別ビルドから転用しない。
通常の GUI park と STOP park/kill の差を AppSlot の state・parked_from_kbd・parked_from_stop で区別する。

- 子の app ID/AS owner を READY で控える。`appslot_reclaim_count`、ledger owner pages、
  free pages と `lib/sqlite3/os32_sqlite_vfs.c` の `groups` の接続勘定を前後で採取する。
  CUI の自動照合に加え、GUI kill 後は子 owner=0、reclaim 増分=1、slot FREE、
  SQLite pool 基準復帰、親/RESIDENT group が生存していることを確認する。
  `db_mem_used()` は全 SQLite の量なので、それだけで子の group の消滅を判定しない。
  IME の事前検索後を基準とし、子 group の回収と常駐 FEP の接続・保持 statement/cache の
  生存を別々に採取する。
- FM は STOP 直前に `fm_note_on` 到達を確認し、最後の `fm_note_off(0)` の実 OPN
  KEY_ONOFF 書込を I/O trace で確認する。無音だけを Key-OFF の証拠にしない。
- IME は `g_ime` の kana/result/commit/rk と候補表示を確認する。CUI 読み手の終了で取消、
  mode/辞書は保持。GUI の raw 待ちは WM 所有の composition を取り消さない契約なので、
  別の WM textbox の候補を誤って消していないことも確認する ([X-11](../../docs/KAPI_SPEC.md#共有サービス12口の契約-x-11--t4b-5))。
- `parent` の exec_run に入る直前、子から戻った親の次の mem_stat wrapper 入口で
  `kernel_tss.esp0` と `exec/redir_access.c` の `caller_frame` (origin/app_id/as/valid)、
  `g_cur_frame` / `ring3_in_syscall` を採取。同じ親の値に戻り、mem_stat がその app を返すこと。
  fault/STOP/通常で比較する。GUI では終了後の WM の次 KAPI で元の WM esp0/caller を照合する。

### 内部ログ / SQLite の中断禁止を観測する方法

READY と処理入口の間に STOP を送ってもこの受入の証拠にはならない。
`db stop` は `sqlite_engine_active=1` の SELECT 内への IRQ 到達を採る。
`log stop` は以下の試験ビルドを使い、**ブレークせず**に注入する。通常ビルドの短いログと
停止点でのキー注入の組合せは使わない (停止中はエミュレータの時刻が進まない)。

1. `make -W kernel/ime_dict.c all KERNEL_CFLAGS_EXTRA=-DOS32_RAW_WAIT_LOG_TICKS=1000 MUTATE=0`。
   試験フラグは USER 呼出しからの辞書成功ログだけを1000 tick (100Hzで約10秒) 延長する。
   KAPI の引数・版・slot は変えない。フラグ切替は make の依存に含まれないため `-W` は必須。
   PM がこのカーネルと対の kdebug/モジュール・fixture を停止→配備→起動し、hash/CRC を照合する。
2. SerialFS/rshell を閉じ、`s_gate=0`、ローカル CUI、FEP OFF とする。
   シリアル出力を非停止で採取し、全 BP を外して `trap_pause=false` を確認する。
3. `raw_wait_test parent log done` を実行する。親の事前切替と子の本試行に各1組の
   `raw_wait LOG BEGIN` → ドット列 → `raw_wait LOG END` が出て、正常 PASS となること。
4. `raw_wait_test parent log stop` を実行する。最初の BEGIN/END (親の事前切替) は見送る。
   子の READY/app ID を控え、**2回目の BEGIN の後、ドットが増えている間**に
   キー API の `seq=CTRL+STOP` を1回だけ送る。BEGIN から END まで pause/BP/resume を使わない。
   自動注入はシリアル受信側で2回目の BEGIN を検出して即送る。ログの採取遅延が10秒以上なら
   tick 引数を増やして再ビルドする。hold の長さは配送区間の証拠にしない。
5. CPUを止めないメモリ読取りで、END より前の子の `abort_req=1`、abort/reclaim 増分0を
   採る (現 ELF の `g_cur` / `g_slot` と型の offset を使う)。時刻入りのシリアル記録と併せ、
   区間中の受理を示す。必要なら非停止 trace の IRQ1 EIP を `ime_dict_open` 内へインライン化されたログ区間とその呼出し先に照合する。
6. END 完送後にだけ abort が+1、回収が+1、`PASS parent log kind=3 code=-2` となり、
   子の `FAIL returned` / `PASS normal` が出ず、親の次 KAPI が通ること。続けて done を実行し、
   再利用 slot に STOP が残らないこと。GUI は END 後の STOP park と WM kill を区別する。

BEGIN/END の間の受理を示せなかった回、終了処理中にだけ届いた回はこの項目の成功に数えない。
後者は [09_exec.md](../../docs/09_exec.md#起動失敗の巻き戻しと強制脱出) の契約どおり子 slot とともに破棄する。
SQLite は leave 後、ログは caller の syscall 出口だけ CUI abort または GUI park を許す。
試験後に通常像へ戻す場合は同じ `-W` を使い、試験フラグ無しで `make all` し直して照合する。

`g_ime`/`caller_frame` 等の static は kernel ELF のローカルシンボルから読む。
観測だけのために KAPI・常駐の診断カウンタや固定アドレスは追加しない。
