# sh_status TDD — 終了コードの配線と `$?`

票: [`docs/archive/shell/TASK_EXIT_STATUS.md`](../../docs/archive/shell/TASK_EXIT_STATUS.md)
基点: `feat/gui` = `a11ee98` (設計レビュー 5 往復で Approve、設計凍結)
試験: `make check-sh-status-host` (= `python3 -B tools/tests/test_sh_status.py --mutate`)
    肯定側だけ: `python3 -B tools/tests/test_sh_status.py`

## 0. この試験の形

`tools/tests/sh_status_host.c` が `userland/shell/main.c` を丸ごと `#include` し、
**登録表も `execute_command` も実物のまま**回す (受入 R2)。`exit` を文字列で直接見る
ようなスタブは 1 つも置いていない — `tools/tests/sh_shell_host.c` は `execute_command`
をスタブにしているので、この票の配線はそこでは試験できない (往復 2 所見 6)。

同じ 1 本を **2 通り**にコンパイルして両方走らせる:

| 版 | 定義 | 起動口 (§2-2) | 終了コード |
|---|---|---|---|
| `resident` | なし | `exec_run` → 直後に `exec_last_result` (KAPI v55) | 子の値がそのまま届く |
| `sh_app` | `-DSHELL_AS_APP` | 要求表 (`launch_req` / `launch_poll`) の写像 | **運べない** (§2-6)。`DONE` = `(EXITED, 0)` |

種別ごとの写像はビルドで実装が違うので、片方だけでは配線を見たことにならない。

### 「その経路が実際に走ったか」の窓

偽の緑を 4 回踏んでいるので、値の一致だけでは合格にしない。

| 窓 | 見えるもの |
|---|---|
| `g_exec_calls` | 子を起こそうとした回数 (常駐 = `exec_run` / `sh.bin` = `launch_req`) |
| `exec_count(path)` | **その綴りを何回**起こしにいったか (S2b「1 コピーで 1 回」) |
| `exec_tried(path)` | その候補を実際に試したか (S2 / S2c) |
| `g_out` | `kprintf` と `sys_write(1)` の写し。`echo` の出力で「その行が走ったか」を見る |
| `redir_opened(path)` | リダイレクト先を実際に開いたか (S13) |
| `sh_exit_flag` / `sh_exit_code` | 終了要求が立ったか / その値 (S6 / S6b) |
| `script_errexit_get()` | `set -e` の旗 (S5b の save / restore) |

### 贋の「子」

`g_progs[]` が `(綴り, 種別, 終了コード)` の台本。常駐の `h_exec_run` は
**カーネルと同じ規則**で記録を全 return 点に書き、`h_exec_last_result` がそれを返す
(記録が無ければ `OS32_ERR_INVAL`)。`sh.bin` 側は同じ台本を `LAUNCH_ST_*` に写す。

`g_rec_suppress` は「記録が無い」を演じる窓 (S15)。

## 1. RED → GREEN

実装前 (`a11ee98`) に新しい試験を当てると、`execute_command` が `void` で
`$?` が存在しないため **1 本もコンパイルが通らない** (= RED)。したがって
「行ごとの RED」は変異 (§3) で示している — 実装後のソースに欠陥を 1 つずつ
差し戻して、対応する検査が落ちることを確かめる形。

実装後: **314 検査 0 失敗** (常駐 186 / `sh.bin` 128)、変異 **23 本すべて RED**。
`8ac609c` (行ごとの譲り) と合流した後も同じ数字。`test_sh_truncation.py` は
**347 検査 0 失敗 / 変異 61 本すべて RED / CUI YIELD PROBE gui=1 cui=0 PASS**。

## 2. 受入との対応

| 票の受入 | この試験の節 | 備考 |
|---|---|---|
| S1 | `case_s1_exit_codes` | `0/1/-1/-2/-3/-4/-5/2/255` → `0/1/255/254/253/252/251/2/255`。`sh_app` は `DONE` = 0 だけ |
| S1b | `case_s1b_fault_abort` | fault=139 / CTRL+STOP=130。表示は今までどおり `[Process crashed]`。`exit(-2)` が 254 であることも裏で見る |
| S2 / S2b / S2d | `case_s2_path_scan` | 「成功の直後に未知コマンド」で前の記録を読まないことを含む |
| S2c / S8 | `case_s2c_s8_invalid` | `INVALID` は次の候補へ / 尽きたら 126 (127 と言わない) |
| S3 | `case_s3_builtins` | `cd` / `cat` / `mkdir` の成功・失敗と、引数不足の 2 |
| S3b | `case_s3b_composites` | `source` / `time` / `if` (真偽) / パイプ / `goto` / ESC |
| S4 | `case_s4_unreached` | リダイレクト失敗・長すぎる行・引数過多・パイプ確保失敗・not found・空行 |
| S5 / S5b | `case_s5_errexit` | 外部 / 組み込み / リダイレクト失敗 / 未知コマンドの 4 通り + `set +e` + 入れ子 |
| S6 | `case_s6_exit` (常駐) | 対話は終わらず `$?`、スクリプト中は打ち切って `source` が N |
| S6b | `case_s6_exit` (`sh_app`) | `exit` / `exit 3` で端末を閉じる印が立つ |
| S7 | `case_s7_expand` | `$?` / `$?x` / `$` / `${?}` / `$VAR` 併用 / パイプ行 |
| S9 | `case_s9_exit_in_pipe` | `exit 3 \| echo tail` / `exit 0` / `time` / `if` / 入れ子 `source` |
| S10 / S11 / S12 | `case_s10_s12_refused` | 断った行の `$?` は 2。S11 は **子を起こしていない**ことを `g_exec_calls == 0` で見る |
| S13 | `case_s13_zero_rows` | リダイレクトだけ / help の短絡 / 実行行の無い `source` → 0 |
| S14 / S15 | `case_s14_s15_records` | 入れ子 `exec` の値、記録が無いときに前の値を返さない |
| R1 | (別ファイル) | `test_sh_shell.py` / `test_sh_launch.py` / `test_fs_kind_callers.py` / `test_sh_truncation.py` / `test_cat_linenum.py` |
| R1b | `tools/tests/sh_launch_host.c` `case_req_nogui` | 新しい契約 (GUI 外 = 126・走査停止) へ移した |
| R1c | `tools/tests/fs_kind_callers_host.c` | `run()` が `int (*)(int, char **)` を受ける |
| R2 | `case_r2_real_table` | 登録表が実物・`exit` / `set` / `source` が 1 本ずつ・二重登録なし |

### 票にあって、この試験で見ていないもの

- **S15 のカーネル側** (`AppSlot.gui` の子は記録を書かない)。`exec/exec.c` の
  `exec_exit` にある規則で、`exec.c` をホストで動かす足場が無いため**自動試験は無い**。
  シェル側の「記録が無いときに前の値を返さない」だけを見ている。
- park (票の事実 10) は `exec_run` では起こらないので、票のとおり**実行試験を作らない**。

## 3. 変異 (否定側)

`--mutate` で 23 本。どれも「その規則を試験が見ている」ことの証拠で、
**RED になること**が期待値。

| 変異 | 壊す規則 |
|---|---|
| `kind_from_value` | (1) 種別を値から作る → `exit(-2)` が fault に化ける |
| `scan_by_value` / `scan_by_value_path` | (2) PATH 走査を値で止める (実害 1 そのもの) |
| `forget_invalid` / `invalid_stops_scan` | (3) `INVALID` を覚えない / cwd の `INVALID` で止める |
| `exit_value_lost` / `pipe_ignores_exit` / `exit_flag_is_value` / `exit_flag_leaks` | (4) `exit` の値と要求の扱い |
| `set_e_not_caught` / `errexit_not_restored` / `errexit_no_abort` | (5) `set -e` |
| `status_expand_late` | (6) `$?` を名前の走査より後ろで見る |
| `no_last_result` / `stale_record_ok` | (7) 起動口の統一と記録なしの扱い |
| `builtin_result_dropped` / `unreached_is_zero` / `notfound_is_zero` | 組み込みと届かない行の値 |
| `source_always_zero` / `pipe_first_stage` / `if_false_keeps_status` | `source` / パイプ / `if` の値 |
| `exit_arg_unchecked` | `exit 1a` を通してしまう |
| `profile_leaks_status` | 起動時 profile の結果を `$?` に残す |

### 歯が立たなかったので**実装の側を直した**もの

`pipe_ignores_exit` は当初、段ループの **入口と末尾の 2 か所**で `sh_exit_flag` を
見ていたため、片方を壊しても挙動が変わらず GREEN のままだった。見張りを末尾の
1 か所に寄せて (`userland/shell/main.c` のパイプ段ループ)、変異に歯が立つようにした。
入口の判定は「段が実行される前に必ず末尾の判定を通っている」ので冗長だった。

## 4. 既存試験への影響 (R1)

| 試験 | 変えたところ | 理由 |
|---|---|---|
| `test_sh_launch.py` / `sh_launch_host.c` | `case_req_nogui` を結果 API (`sh_exec_result`) へ移した | R1b。GUI 外は `EXEC_KIND_NOMEM` = 126 で**走査を止める**。旧 `sh_launch` の「NOT_FOUND = 次の候補へ」は残さない |
| `test_fs_kind_callers.py` / `fs_kind_callers_host.c` | `run()` の引数を `int (*)(int, char **)` に | R1c。決裁 E2 の型変更 |
| `test_sh_shell.py` / `sh_shell_host.c` | `execute_command` スタブを `int` に。`$?` / `sh_exit_code` / `sh_exit_arg` の実体を置いた | 決裁 E2 / `execute_command` の `int` 化 |
| `test_cat_linenum.py` | 変異 `e_read_tty` の目印を `return SH_STATUS_USAGE;` に直した | 目印が古いままだと SKIP になり、否定側の歯が 1 本抜ける |
| `test_fs_kind_callers.py` | 変異 3 本の目印 (`MKDIR_BLOCK` / `LS_BLOCK`) を `return SH_STATUS_ERROR;` に直した | 同上 |
| `test_sh_truncation.py` / `sh_truncation_host.c` | 贋 `launch_req` を「要求は受け付けて子が見つからない」に替えた。`5f` の期待を新しい `source` の契約 (最後に実行した行の値) に直した。変異の目印 13 本を新しいソースに合わせた | R1b で「GUI 外 = 走査停止」になったため、候補を全部試す反例 (`18e`) と `ran()` の窓が成り立たなくなる。**検査数 337 / 変異 59 は増減なし** |
| `test_sh_truncation.py` case 30 (`8ac609c` の「行ごとの譲り」) | 30a〜h を **内蔵コマンド (`echo`)** で組み直し、外部コマンドの形は 30i〜j の**差分**で見るようにした (+2 検査)。30i/j の手前に `tick_step()` を足した | `sh_launch` は要求表の待ちループで `sys_yield` を回す。旧い贋 `launch_req` は「GUI 外」を返して待ちループへ入らなかったので `yield_count()` に混ざらなかったが、新しい足場では混ざる。絶対値をやめ、同じ 1 行の対話 / スクリプトの差が 1 であることで「script_exec が足した 1 回」を取り出す (PATH 候補の数に依存しない)。`tick_step()` は 30h が tick を止めたまま残すため — 動いていない tick で測ると**正しく間引かれて** 0 回になる |

## 5. 走らせ方

```bash
python3 -B tools/tests/test_sh_status.py            # 肯定側だけ (2 版)
python3 -B tools/tests/test_sh_status.py --mutate   # 否定側つき
make check-sh-status-host                           # 上と同じ (--mutate つき)
```
