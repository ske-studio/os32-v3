# TASK_TEST_TIDY — 検査の整理 (重い検査を軽く・重複を束ねる・古い試験を削る)

> 上位: [ROADMAP](../../ROADMAP.md)

> 状態: **実装中 (2026-10-24)** — ユーザー決定 2026-10-24。調査は `~/os32-tmp/run/test_survey/REPORT.md` (2026-10-22、225 検査)。

## 0. ユーザー決定 (2026-10-24)

- 判断 1「おすすめ通り進めて」: 時間を測る → 重複を runner の統合で束ねる (ケースと変異は減らさない) → 検査に載っていない試験を載せる。
- 判断 2: **a5 (LGY-98・Host Services の 4 本: host-agent・host-lib-host・net-link-host・lan-bridge-host)「残すが保守からは一時的に外す (現物が手に入らないため)」**。その他はおすすめを承認 — a1 gui-gate・key-inject は道具の利用を確かめてから削る、a2 db-v50・db-owned はケースを後続の試験へ移してから削る、a3 kstr-bench は削る、a4 は残す (privileged は保守の束 A で `--strict` に)、a7 は残す。
- a6 (vfs-fd-sqlite・sqlite-groups) と台帳 T4B-CD: ユーザー決定 2026-10-24「消してください」— 凍結した不変 CD 専用の RO SQLite のコード (`vfs_sqlite_allow_immutable` とそれだけが使う経路) と、その host 試験 (sqlite-cd-host など) と a6 の 2 本は2026-10-24 に削除 (票 TASK_TEST_TIDY の TT5)。共有C fixtureは残存検査用に保持。台帳T4B-CDの閉鎖はPMが行う。

## 1. 時間 (2026-10-23 の全体検査 a5f3351 の記録、変異込み、`check_times_2026-10-23.tsv`)

合計 7,673 秒 (並列前の和)。上位 4 本で約 3,080 秒 (40%): check-c-dialect-host 1,344・check-p2v 941・check-memmap-host 450・check-check-select-host 346。次いで memory-host 289・c-dialect 280・e82557-drv-host 238・kstr-bench-host 177・arch-asm 164。重複の束ね (b) は時間にはほとんど効かない — 時間は上位 4 本を軽くする段で取る。

## 2. 段

| 段 | 中身 | 実装 | 状態 |
|---|---|---|---|
| TT1 | 上位 4 本 (c-dialect-host・p2v・memmap-host・check-select-host) を測って軽くする。ケース・変異の検出力は減らさない (変異ごとの写しの木の作り直し・全体の再走査の重複を疑う) | sol | **main** (2026-10-24、astra 取り込み可) — 変異込みの前→後 (秒): c-dialect-host 971→1,037 (効果なし)、p2v 1,076→287、memmap-host 282→170、check-select-host 703→237。4 本の合計 3,032→1,731。変異の RED/GREEN の数は 4 本とも前と同じ。測定は `~/os32-tmp/run/tt1/{before,after}-times.tsv`。c-dialect-host は次の段の候補 |
| TT2 | (PRE-1 は 2026-10-23 に閉じ済み — test_preflight は check-preflight-host、test_jobs は運用補助で載せない)。a5 を CHECK_PAR_ORDER から外してローカルだけに (ファイルは残す、台帳 LGY98-L12 に「保守外」)、a3 削除、a1・a2 | sol (保守の束 A の取り込みの後) | **main** (d0d4dce、astra 取り込み可・PM の直し 2 回) — a5 の 4 本はローカルだけ (local_only は承認済みの 4 本に限る)、a3 削除 (kstr_bench 本体は計測で使うので残す)、a1 は道具が使われているので残す、a2 の 39 ケース・変異 18 本を db-errstr-host へ移して削除 (CI の静的の列はホストだけで test_db_errstr.py) |
| TT3 | b1〜b4 の束ね (gfx 4 本・font 5 本・access/caller 5 本・gfinit は現行 PAR の 6 本) | sol | **main** (a42bcf4、astra 取り込み可) — 20 検査を 4 登録へ (gfx・font・access・gfinit、tools/run_host_group.py が親の jobserver を共用)。ケース・変異・RED/GREEN の数は同じ。検査は 220→204 本 |
| TT5 | T4B-CD の凍結コードと試験・a6 の削除 (カーネル層の fs/vfs に触るので候補の検査を全部 MUTATE=0) | sol | **main** (fb1c7f3、astra 取り込み可、台帳 T4B-CD は閉鎖台帳へ) |
| TT4 | b5〜b7 の束ね (hsync・fs-kind・sh-input・sh-exec・sqlite-module-proto の 5 群) | sol | **実装中 (レビュー待ち)** — 12 登録を 5 登録へ。runner は TT3 の jobserver を共用し、既存の引数・ケース・変異を保持。SQLite は正常対照の重複を除き stack 容量変異を統合。検証証拠は `~/os32-tmp/run/tt4/` (前後の件数・時間、候補・静的・ビルドのログ) |
