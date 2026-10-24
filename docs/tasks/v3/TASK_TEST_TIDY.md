# TASK_TEST_TIDY — 検査の整理 (重い検査を軽く・重複を束ねる・古い試験を削る)

> 上位: [ROADMAP](../../ROADMAP.md)

> 状態: **実装中 (2026-10-24)** — ユーザー決定 2026-10-24。調査は `~/os32-tmp/run/test_survey/REPORT.md` (2026-10-22、225 検査)。

## 0. ユーザー決定 (2026-10-24)

- 判断 1「おすすめ通り進めて」: 時間を測る → 重複を runner の統合で束ねる (ケースと変異は減らさない) → 検査に載っていない試験を載せる。
- 判断 2: **a5 (LGY-98・Host Services の 4 本: host-agent・host-lib-host・net-link-host・lan-bridge-host)「残すが保守からは一時的に外す (現物が手に入らないため)」**。その他はおすすめを承認 — a1 gui-gate・key-inject は道具の利用を確かめてから削る、a2 db-v50・db-owned はケースを後続の試験へ移してから削る、a3 kstr-bench は削る、a4 は残す (privileged は保守の束 A で `--strict` に)、a7 は残す。
- a6 (vfs-fd-sqlite・sqlite-groups) と台帳 T4B-CD: ユーザー決定 2026-10-24「消してください」— 凍結した不変 CD 専用の RO SQLite のコード (`vfs_sqlite_allow_immutable` とそれだけが使う経路) と、その host 試験 (sqlite-cd-host など) と a6 の 2 本を消す (段 TT5)。

## 1. 時間 (2026-10-23 の全体検査 a5f3351 の記録、変異込み、`check_times_2026-10-23.tsv`)

合計 7,673 秒 (並列前の和)。上位 4 本で約 3,080 秒 (40%): check-c-dialect-host 1,344・check-p2v 941・check-memmap-host 450・check-check-select-host 346。次いで memory-host 289・c-dialect 280・e82557-drv-host 238・kstr-bench-host 177・arch-asm 164。重複の束ね (b) は時間にはほとんど効かない — 時間は上位 4 本を軽くする段で取る。

## 2. 段

| 段 | 中身 | 実装 | 状態 |
|---|---|---|---|
| TT1 | 上位 4 本 (c-dialect-host・p2v・memmap-host・check-select-host) を測って軽くする。ケース・変異の検出力は減らさない (変異ごとの写しの木の作り直し・全体の再走査の重複を疑う) | sol | 未 |
| TT2 | PRE-1 (test_preflight・test_jobs を検査に)、a5 を CHECK_PAR_ORDER から外してローカルだけに (ファイルは残す、台帳 LGY98-L12 に「保守外」)、a3 削除、a1・a2 | sol (保守の束 A の取り込みの後) | 未 |
| TT3 | b1〜b4 の束ね (gfx 4 本・font 5 本・access/caller 系・gfinit 7 本) | sol | 未 |
| TT5 | T4B-CD の凍結コードと試験・a6 の削除 (カーネル層の fs/vfs に触るので候補の検査を全部 MUTATE=0)、台帳 T4B-CD を閉じる | sol (A・TT1 の後、検査枠が空いてから) | 未 |
| TT4 | b5〜b7 の束ね (hsync-h1〜h3・fs-kind 系・sh-* 系・sqlite-module-proto/stack) | sol | 未 |
