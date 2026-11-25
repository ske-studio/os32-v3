# sys/kern/exec/appslot.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `sys/kern/exec/appslot.c`

### T2a. 複数アプリ時のスロット割当と譲り合い — 確定 2026-09-04

今の契約 (割当・配送・譲り合い・見え方) は [09_exec「GUI アプリのスロットと譲り合い」](../../09_exec.md#gui-アプリのスロットと譲り合い) へ移した。

<a id="park-origins"></a>

**park の由来 (5 種)**: OP_WAIT / kbd待ち / polling / sys_yield に加え、
GUI STOP の WM 返却要求だけが **正常 syscall 完了後**に退避する。
第5由来は WAIT_POLL の内部印 `parked_from_stop`、resume は保存 EAX を維持し、
注入リングを読まない。X4 自体は退避も宛先解決もしない。

OP_WAIT / kbd / poll / yield の共通更新と資格判定の順は
[appslot_park](appslot_park.md) を参照。

## stdin の KBD 再開

未完了の sys_read は AppSlot の進捗を保持し、KBD 再開時に文字を残して syscall を再実行する。
1文字を EAX に返す扱いは単独の getchar/getkey に適用する。
状態の寿命とフレーム復帰の契約は [exec_stdin](exec_stdin.md) を参照。

## 起動・畳み・CTRL+STOP・画面の所有者の判定

> 出所: TASK_K5_multiapp・TASK_K5B_kernel・TASK_K5B_gshell (決裁 A1)・TASK_T8_fullscreen_gfx (§2 D1・D1a・D3、§6 F5) の契約を今のソースと照らして要約 (2026-11-18)

判定は表だけを見る関数に分け、状態を動かす関数と別にしてある (ホストでそのまま試験するため)。park と resume の判定は `docs/design/exec/appslot_transition.md`。

### 起動 (`appslot_start_admit(gui, pages, free_pages)`)

- ID は 1 = シェル帯、2〜5 = アプリ (`APP_ID_MIN`..`APP_ID_MAX`、同時に 4 本)。GUI アプリと CUI の入れ子 `exec_run` の子は同じ池を使う。5 本目は `OS32_ERR_FULL`。
- GUI アプリ (`exec_start`) を立てられるのは WM top-level からだけ (`g_cur` がシェル帯)。それ以外は `OS32_ERR_INVAL`。CUI の入れ子は走っているアプリからも通る。
- 要るページ数 (`exec_ring3_pages`) が空きを超えれば `EXEC_ERR_NOMEM`。切り詰めない・スワップしない。拒否は表も空きページも動かさない。起動の失敗は CR3・owner・回収のどれも動かさない。
- `appslot_cui_only_admit(gui, hdr_flags)`: GUI からの起動で `OS32X_FLAG_CUI_ONLY` (`mkos32x --cui-only`) が立っていれば `OS32_ERR_INVAL`。CUI (`exec_run`) からは通す。宣言の無いプログラムは GUI からも通す。`exec_launch` はこれを帯も池も動かす前に見る。

### 畳み・CTRL+STOP

- `appslot_kill_check(id)`: WM top-level (cur と res owner がシェル帯) からだけ。止めてあるアプリ (PARKED・WAIT_KEY・WAIT_POLL) は畳める。走っている本人は `OS32_ERR_STALE`。
- `appslot_abort_request()`: IRQ1 の CTRL+STOP は「いま走っているアプリ」(RUNNING、シェル帯でない) にだけ `abort_req` を立てる。止めてあるアプリには届かない (そちらは WM が `exec_kill` で畳む)。
- `appslot_abort_clear()` (KAPI `exec_abort_clear`): 呼べるのは res owner がシェル帯のときだけ (それ以外は `OS32_ERR_INVAL` で何も降ろさない)。全アプリの `abort_req` と `stop_wm_req` を降ろす (隔離で付いた abort は残す)。降ろすだけで 1 本も畳まない。要求が無くても、アプリが居なくても 0。WM はこれで走っている本人の要求を降ろしてから、フォーカス窓の側を `exec_kill` で畳む。

### 画面の所有者 (`appslot_gfx_claim`・`appslot_gfx_owner_exit`)

- 所有者の初期値は WM (`GFX_OWNER_WM` = シェル帯)。アプリを立てただけでは移らない。
- `gfx_init` (KAPI の門 `sys/hal/gfx/gfx_core.c`) が `appslot_gfx_claim(con_sink_is_enabled())` を呼ぶ。判定は純関数 `appslot_gfx_claim_check(gui_mode, caller, cpl3, hdr_flags)`:
  - 呼び手がアプリの範囲外 (WM 自身の復帰の `gfx_init` など)、または CPL=0 → 素通し (所有者は動かない)。
  - GUI 中で `OS32X_FLAG_GFX` (`mkos32x --gfx`) の宣言が無い → `OS32_ERR_INVAL`。所有者は動かず `gfx_init_reject_count` が 1 増え、そのアプリに `abort_req` を立てる (syscall の出口で畳まれる)。何度拒否しても同じ。
  - それ以外 (GUI 中で宣言あり、または CUI 中) → 所有者は呼び手。CUI 中は宣言が無くても通し、拒否として数えない。同じアプリが二度呼んでも変わらない。
- `appslot_gfx_owner_exit(id)`: 回収された ID が所有者なら WM へ戻す。他人の回収では戻らない。park しただけでは戻らない (WM は上書きしない)。正常終了でも `exec_kill` でも戻る。

検査: `tools/tests/test_multiapp_impl.py` (`multiapp_impl_host.c`) と、ブート時の `appslot_gfx_owner_selftest` (`sys/kern/exec/appslot_selftest.inc`)。
