# T4b-7 / E11-A2b raw KAPI 待機

契約: [TASK_T4_T6_MODULES §7-1 T4b-7](../../docs/tasks/v3/TASK_T4_T6_MODULES.md#7-1-半日1日の受入単位)、同 §3-1 / §3-3。
ゲストの実行結果は PM が採取する。ホストの合格だけでは E11-A2b を閉じない。

## ホストの対照と変異

`make check-raw-wait-host MUTATE=0` は、実ドライバと生成済み公開 wrapper、
SQLite guard と実代替 stack、実 exec の launch/resume 着地を ILP32/qemu で実行する。
足場は `kapi_audit_host.c` / `sqlite_engine_host.c` / `test_exec_r1.py` と共用する。
物理 I/O・入力 IRQ・CR3/iret・辞書 storage の代替部分はそれぞれの足場冒頭を参照。
通常の GUI park と、park 不成立時の STOP 解除は別々に検査する。

`MUTATE=1` は次の 14 件だけを写しに適用し、コンパイル成功と指定アサートでの RED を要求する。
コンパイル失敗・タイムアウト・別アサートの失敗は変異検出の成功に数えない。

| 変異 | 期待する実行時失敗 |
|---|---|
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

ゲスト fixture 自身も記録用 KernelAPI で実行し、8 口の選択、予期しない入力・STOP 後の
return の拒否、親の二重回収・pool/USER/SQLite 漏れ・sentinel/result 不一致の検出を確認する。

正常対照は各入力口の空待ち→遅延入力→1 回消費、GUI park、serial 通常受信、
FM 1 音の全時間完了＋Key-OFF、内部ログの IF=0/1 両方、SQLite active 中の kill=0 と
leave 後の kill=1、親の exact esp0/caller 復元→次の実 syscall→親 USER heap 不変。

## PM のゲスト手順 (64MB、未実施)

[V1] に従い、この木で生成したカーネル・対 kdebug と `/usr/bin/raw_wait_test.bin` を揃える。
配備は PM が停止→配備→起動で行い、ファイル hash と起動像を照合する。
fixture は通常 CPL3、`userland/deploy.yaml` に登録済み。公開 KAPI・配置世代の変更はない。

先行入力を空にし、CUI はローカル shell を使う。rshell の kbd_getchar は既存のタイムアウトで
空白を返すので、無入力 STOP の対照にはしない。serial は SerialFS のセッションを閉じて
`s_gate=0`、接続相手から自動送信しない状態にする。ゲスト serial への入力は物理打鍵と区別する。
IME の通常完了は OFF にして大文字 `Q` を送る。変換取消の読みは「あ」に固定する。
CUI の `parent imechar stop` / `parent imekey stop` は、基準採取前に `WARMUP` を表示する。
表示のたびに、ひらがな ON → `a` → SPACE (候補表示) → ESC 2 回 (変換取消・読み消去)
→ OFF → 大文字 `Q` を入力する。候補リストを Down で展開しない。
同じ検索・取消後の SQLite 使用量が連続一致するまで最低 2 回、最大 4 回繰り返す。
子の `READY` が出てから再びひらがな ON → `a` → SPACE で同じ「あ」の候補を表示し、CTRL+STOP。
GUI でも読みは「あ」に固定する。辞書切替で学習内容を削除しない。

| 口 (`entry`) | CUI 正常・STOP | GUI 正常・STOP |
|---|---|---|
| `kbdchar`, `kbdkey` | `raw_wait_test parent <entry> done` / `stop` | WM の通常 launch で `raw_wait_test <entry> done` / `stop` |
| `imechar`, `imekey` | 同上。候補表示中の STOP も両口で行う | 同上。WM の FEP 配送と raw cooked 待ちを区別 |
| `serial` | 同上。done は READY 後に受信線へ `Q` | 同上。done は受信線へ `Q` |
| `fm` | 同上。done は 1 音、stop は約 28 秒の連音列中 | 同上。Key-ON 中に STOP |
| `db` | 同上。長い再帰 SELECT 中に STOP | 同上 |
| `log` | 同上。既定辞書への reopen 成功ログ中に STOP | 同上 |

`done` は入力口では READY 後に `Q`、FM/DB/log は入力なしで完了を待つ。
`stop` は READY の後に CTRL+STOP。CUI では親に `PASS parent` が出て、さらに shell に戻る。
GUI は `parent` を使わない (GUI CPL3 の nested exec_run は禁止)。WAIT_KEY のアプリは
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
`log` の初回辞書 load による常駐増分を子リークと混同しないため、既定辞書を事前に開き、
同じ辞書への切替を一度成功させてから採取する。

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
`db stop` は `sqlite_engine_active=1` の SELECT 内、`log stop` は
`ime_dict_reopen` の成功診断が呼ぶ **内部** `serial_puts` の入口で停止点を取り、
そこにいることを確認して解除・CTRL+STOP を送る。停止した CPU に IRQ は配送されないので、
解除後に IRQ1 が到達した位置も trace で確認する。STOP 要求が保持されている間は
`ring3_abort_count` / reclaim が増えず、engine は active を保ち、ログは最後まで送られること。
SQLite は leave 後、ログは caller の syscall 出口後だけ CUI abort または GUI park を許す。
入口や出口にしか IRQ が届かなかった回は「未観測」とし、成功に数えず再採取する。
`db done` / `log done` は同じ処理が通常完了する対照。

`g_ime`/`caller_frame` 等の static は kernel ELF のローカルシンボルから読む。
観測だけのために KAPI・常駐の診断カウンタや固定アドレスは追加しない。
