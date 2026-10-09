# T2g 変異対応表 — g5

2026-10-09 時点の実装・試験対応。基点 `b43e814`。
契約の正典: [T2d〜T2h §4-2 / §4-3](TASK_T2D_T2H.md)。
書式の前例: [T2e 変異対応表](T2E_MUTANTS.md)。
シングルタスク・協調切替を前提とし、製品の挙動は変更しない。

試験名は `tools/tests/test_<名前>.py`、`gshell` は
`userland/gshell/host/integration.py` を指す。全変異で正常対照を先に実行し、
製品入力への置換は一時コピーだけに適用する。
以下の値は変異定義・fixture の検査式と今回のログを照合したもの。
ログの置場は `/home/hight/os32-tmp/run/g/g5/`、最終実行は
`g5_final_mut1_<ターゲット名>.log`。直しで変更したtrim_kernel/kapi_layoutは
`g5fix_mut1.log` (その他は前回の最終実行)。RED 行は指定した実行時失敗を検査した後に出る。
C の rc=1、Rust test の rc=101 を区別し、ビルド不能を runtime RED に含めない。

## 必須の契約上の種類

設計票の18種類という表記は、票の13 + レビュー9から重なり6を合成した16種類 (票の13を正とする)。補助wrapを含めて下表17行。
「重複」の行には双方の呼び方を書く。変異名は実コード内の名前をそのまま用いる。

| 契約上の種類 (重複する呼び方) | 捕まえる試験 | 変異名 | runtime RED の FAIL 行・検査 |
| --- | --- | --- | --- |
| map 途中 pump | trim_flow | `map-mid-pump` | `FAIL: no WM inside map transaction` (rc=1) |
| VA 不足通知 | trim_kernel | `enova-marks` | `FAIL: prepare errors do not mark` (rc=1) |
| CUI 自身への配送 | trim_flow | `cui-self-delivery` | `FAIL: requester not marked` (rc=1) |
| 前景への配送 / レビュー「前景へ配送」(重複) | gshell | `deliver-front` | 試験 `g2w_front_is_deferred_until_back` が FAIL (rc=101) |
| busy/入口拒否での再試行 / レビュー「入口拒否で yield」(重複) | nano_adapter | `retry-while-busy` / `retry-on-enter-refused` | `yields <= 1` / `malloc(8) == NULL && yields == 0` (rc=1、`check:` 行) |
| slot 再利用で古い bit 維持 | trim_kernel / gshell | `start-keeps-bit` / `owner-exit-keeps-sent` | `FAIL: start clears trim` / 試験 `g2w_owner_exit_and_forget_clear_sent_on_id_reuse` が FAIL |
| 無制限 retry | nano_adapter / rust_alloc | `retry-unbounded` | `yields <= 1` (rc=1) / `retry-unbounded` (rc=101)。有限回を超えた時点で assert、timeout を代用しない |
| queue 満杯で bit 消失 / レビュー「後送欠落」(重複) | gshell | `full-marks-sent` | 試験 `g2w_full_preserves_events_and_retries_without_marking_sent` が FAIL (rc=101)。満杯で sent を立てると後送不能。kernel の pending は WM から書けないので、配送権を失う変異で検査する |
| 保留中の再要求で epoch 変化 / レビュー「保留中の再 mark で epoch 変化」(重複) | trim_kernel | `remark-changes-epoch` | `FAIL: pending coalesces` (rc=1) |
| 未応答への再配送 / レビュー「未応答再配送」(重複) | gshell | `resend-while-pending` | 試験 `g2w_unanswered_has_one_trim_resume_and_focus_is_separate` が FAIL (rc=101) |
| hook の二重呼出し | gui_trim (check-gui-host 内) | `hook-twice-per-batch` | `hook-twice-per-batch` (rc=101) |
| realloc 再試行の copy 欠落 / レビュー「realloc copy 欠落」(重複) | nano_adapter | `realloc-retry-no-copy` | `((const unsigned char *)p)[i] == byte` (rc=1、`check:` 行) |
| 世代据置 | kapi_layout | `generation-held` | `FAIL: OS32_SHLIB_PROTOCOL == 2`、rc=1。生成定数の確認 (loader 述語の行も同時に FAIL)。一時コピーで JSON を変えて生成し直す |
| fallback 退行 | trim_kernel | `fallback-removed` (g5追加) | `FAIL: TOPDOWN 16 pages` (rc=1)。EXACT 17枚の正常対照後、障害物で TOPDOWN 16枚を検査 |
| 飽和で停止 | trim_kernel | `saturation-stops-mark` (g5追加) | `FAIL: epoch saturation` (rc=1)。`epoch-wrap` は wrap 検出であり停止検出と分ける |
| WAIT_POLL 記録落ち | trim_kernel | `mark-excludes-wait-poll` (g5追加) | `FAIL: back states marked` (rc=1)。PARKED/WAIT_KEY/WAIT_POLL を順に検査 |
| 飽和値の wrap (停止と別の補助変異) | trim_kernel | `epoch-wrap` | `FAIL: epoch saturation` (rc=1) |

必須種類の生き残りは **0**。queue の行はカーネルの bit を消す口を新設せず、
既存の WM 配送状態で「満杯の後にも保留が届く」という契約を検査する。
上表の補助 wrap 行を除く必須種類は16行 (13+9から重複6行を合成)。

## g3fix / g4 と STOP focus の追加対応

| 種類 | 捕まえる試験 | 変異名 | runtime RED の FAIL 行・検査 |
| --- | --- | --- | --- |
| 模型 WAIT_POLL + 未読の入力群除外 | trim_flow | `poll-excludes-input` | `FAIL: input exception preserves pending before retry` (rc=1) |
| 模型 WAIT_POLL + 未読の ready 除外 | trim_flow | `poll-excludes-ready` | `FAIL: input exception ready includes requester` (rc=1) |
| blocked-primary の secondary mark 削除 | trim_flow | `blocked-primary-drops-secondary-mark` | `FAIL: blocked primary marks before first yield` (rc=1) |
| STOP 後の bit 維持 | trim_back_rs | `stop-leaves-bit` | `FAIL: STOP slot trim cleared` (rc=1) |
| WM forget 欠落 | trim_back_rs | `wm-forget-missing` | `FAIL: WM sent cleared` (rc=1) |
| Rust cache が LARGE に化ける | trim_back_rs | `rust-large-cache` | `FAIL: Rust PREP ARENA=2 LARGE=0` (rc=1)。実4000B Boxの分類を検査 |
| Rust raw-map retry | trim_back_rs | `rust-raw-map-retry` | `FAIL: raw map no retry` (rc=1)。fixture `cache::raw_map` 自身の自己防護。製品 wrapper の変異ではない。SDKに mem_map 関数定義がないことは別の静的assert |
| C stop-focus 呼出し欠落 | trim_flow → trim_fixtures | `c-stop-focus-missing` (g5追加) | `FAIL: !stop_focus || (focus_calls == 1 && focus_printed == 1)` (rc=1) |
| C focus error 無視 | trim_flow → trim_fixtures | `c-stop-focus-error-ignored` (g5追加) | `FAIL: !focus_rc` (rc=1)。失敗時に成功印を出さない |
| Rust stop-focus 呼出し欠落 | trim_back_rs → trim_focus_host | `rust-stop-focus-missing` (g5追加) | `stop-focus confirmed before hook` (rc=101) |
| Rust focus error 無視 | trim_back_rs → trim_focus_host | `rust-stop-focus-error-ignored` (g5追加) | `stop-focus error stops before ticks/release` (rc=101) |
| Rust stop-focus CLI 欠落 | trim_back_rs → trim_focus_host | `rust-stop-focus-cli-missing` (g5追加) | `stop-focus CLI accepted` (rc=101)。重複flag拒否も正常対照で検査 |

C/Rust focus 試験は実 fixture の hook (Rust は arguments も) を使い、KAPI/window/tick
だけを置き換える。focus 成功の確認が slow hook より先、失敗なら tick/cache 解放無し、
既定なら focus 無しを検査する。実UIによるSTOP宛先の決定は別の受入である。
`g3fix_wm_probe` の正常対照は「focus未確認ならfront、確認済みならback」を実 WM で確かめる。

## 分類別集計

対応表の検査を `MUTATE=1` で実行した総集計。表のg以外の既存変異も、
同じターゲットで実行した分は含める。Rust host は x86_64、ILP32 は qemu。
`nano_adapter` の正常リンク拒否53件は変異数に含めず、link gate の実行時変異3件は含める。

| ターゲット | 変異数 | runtime RED | compile/link error | timeout | survived |
| --- | ---: | ---: | ---: | ---: | ---: |
| check-trim-kernel-host | 19 | 19 | 0 | 0 | 0 |
| check-trim-flow-host (C fixture 2件を含む) | 11 | 11 | 0 | 0 | 0 |
| check-trim-back-rs-host (Rust focus 3件を含む) | 7 | 7 | 0 | 0 | 0 |
| check-gshell-host (既存GUI変異を含む) | 71 | 71 | 0 | 0 | 0 |
| check-nano-adapter-host (adapter47 + gate3) | 50 | 50 | 0 | 0 | 0 |
| check-rust-alloc-host | 10 | 10 | 0 | 0 | 0 |
| check-gui-host (gui_reattach15 + gui_trim5) | 20 | 20 | 0 | 0 | 0 |
| check-kapi-layout-host | 17 | 16 | 1 | 0 | 0 |
| 合計 | 205 | 204 | 1 | 0 | 0 |

`short_read_trusted` は既存 KAPI layout の変異で、ホスト GNU11 -Werror の
コンパイルに失敗するため **runtime RED ではない**。必須の世代据置は別の
`generation-held` の実行時 FAIL で捕まえる。その他の layout 変異は実行された
C predicate / Python の意味検査による失敗を指し、負入力への生成・リンク拒否を
検査する Python assert もここでは runtime RED に含む。

## kselftest と境界

g5 の kselftest 追加は **0本**、固定値は **293 pass / 0 fail** のまま。
既存の `trim:mark parked GUI`、`trim:pending coalesces`、`trim:done and stale`、
`trim:exclude/reclaim` が、起動で1回確かめる小さな不変条件を既に担う。
状態を戻したことは trim_kernel の `boot borrowed state restored` で確認する。
MemStat 132Bは既存の製品 STATIC_ASSERT、offset120/124/128は今回追加した3本で固定する。
trim_kernel の静的照合は実ヘッダのILP32 offsetofとgshellの手写し定数、Rust fixtureの
手写し欄位置・サイズを比較する。`memstat-pressure-pending-swapped` はヘッダの2欄を
一時コピーで交換し、`FAIL: MemStat wire offsets` (rc=1) にする。
C fixtureは欄名で参照し、gui.pyにMemStatの手写しoffsetは無い。
この静的照合と既存項目で確認するため、同じ確認をpassに追加しない。起動時のカウンタはGK-1で実測する対象であり、
今回のホスト合格を新しいゲストbootの観測とは扱わない。

実MMU/CR3切替、17MBでのGK-3再受入・GK-4、GUI操作でのSTOP宛先はホスト表の外。
ゲスト台本は `tools/tests/guest_acceptance.yaml` のg entry (17MBだけ)、
未受入は [持越し台帳](../DEFERRED_TESTS.md) のPM更新対象とする。
