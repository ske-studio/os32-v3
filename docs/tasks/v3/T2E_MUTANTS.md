# T2e 変異対応表 — e12

正典: [T2d〜T2h §2-5 / §6-2](TASK_T2D_T2H.md)。2026-10-07、基点 `83f2db7`。
試験は `tools/tests/test_<名前>.py` の実ソース / fixture 変異。
ホストの MMU / I/O 足場では実デバイスを証明できないため、ゲスト構成別台本は
`tools/tests/guest_acceptance.yaml`、未確認は [持越し台帳](../DEFERRED_TESTS.md) に残す。

## 必須 12 種類

| 契約上の種類 | 捕まえる試験 | 変異名 (コード内の名前) | runtime RED の観測 |
| --- | --- | --- | --- |
| 旧 bb pointer | gfx_reattach | `old-bb` | `old_bb_not_reused`、両実体の plane[0] が新 lease base と一致しない |
| plane stride 丸め | gfx_kernel_fb | `rounded-stride` | `bb_r-bb_b == GFX_PLANE_SZ` (32,000B、VRAM 窓の丸めではない) |
| 片実体だけ再 attach (fixture の呼出し側を変える変異、製品ソースは不変) | gfx_reattach | `one-return-check` / `static-return-check` | `both_new_generation`、shlib / static の一方の lease が更新されない |
| GUI DISPLAY 許可 | surface_bundle | `ordinary GUI DISPLAY` | 非 owner GUI の DISPLAY query/lease 拒否 |
| UC 落ち | surface_bundle | `initial native UC` / `lease loses UC` / `V86 setup loses UC` | native / lease / V86 setup alias の PCD 一致 |
| 共有 PT 書込み許可 | lease | `shared PT write` | master/共有 PT と private lease PTE の比較 |
| revoke 前 free | lease | `backing free before active TLB synchronization` / `free before active TLB synchronization` | `ORDER: free before TLB synchronization`、backing / PT の区別 |
| V86 後 PDE USER 復元欠落 | shm_user | `v86-restore` | `V86 restores PDE0 USER` |
| teardown の PCD 欠落 | surface_bundle | `V86 teardown table loses UC` / `V86 teardown loses UC` | teardown 表と saved PTE の UC、監査 / 復元不一致 |
| SHM lock/free/回収後 USER 欠落 | shm_user | `lock-user` / `free-user` / `owned-user` / `cleanup-user` | `lock USER` / `free USER` / `owned USER` / `cleanup USER`。RW 専用変更を旧 map に戻すと USER を失う |
| 束 generation 照合削除 | surface_bundle | `bundle generation checks removed` | query/lease/paging の三段を同時に外して旧束 STALE 拒否を観測 |
| font の boot 終了ガード除去 | kcg_boot | `guard` / `init-clears-font` | `closed VFS untouched` / `closed init preserves font`、boot 後 NOSYS と表不変 |

「旧 bb pointer」「片実体だけ再 attach」「SHM 後 USER 欠落」は語の一致では見つからなかったが、
上記の既存変異の内容で捕まえている。重複変異を足さず、既存の検出条件を保持した。

## 既存 lease 10 種類

すべて `test_lease.py` / `lease_host.c` の正常対照後に実行する。

| 変異名 | 捕まえる観測 / 意図 |
| --- | --- |
| `surface registration master guard removed` | 非 master 登録が無変更拒否される |
| `free before active TLB synchronization` | PT を active TLB 同期より前に返さない (`ORDER kind: PT`) |
| `backing free before active TLB synchronization` | 最終参照 backing を PTE/PT 撤去・active TLB 同期より前に返さない (`ORDER kind: backing`) |
| `shared PT write` | lease は private PT だけを書き、master / 他 AS を変えない |
| `PCD lost` | UC SURFACE の lease PTE が PCD を維持する |
| `rollback leaked PT` | 途中失敗の pending PT / owner 会計を全返却する |
| `generation ignored` | stale ref が lease/paging の二段で拒否される |
| `plane end unchecked` | 各 plane の終端が backing bytes に収まる |
| `permission ignored` | RO SURFACE の RW lease を lease/paging の二段で拒否する |
| `free before PDE invalidation` | PT の free は PDE 無効化より後 |

## E11-BUD の追加変異

`test_app_bb_overlap.py` / `app_bb_overlap_host.c` の `peak-common-update` は
実 `kmalloc.c` の共通 kheap_alloc 更新に戻す。exec 用 KHeap を 2,048B 確保したとき
`p && kmalloc_peak_bytes == peak` が崩れて runtime RED (rc=1)。正常対照は kmalloc の
成功時だけピーク更新、free と失敗で不変も確認する。KHeap 型と kselftest 件数は変えない。
`test_memmap_boot.py` は同じ公開口で実 kmalloc.c / exec_heap.c をリンクするので変更不要。

## 分類別集計

§6-2 の規約に従い、rc 非ゼロだけで RED としない。下記は正常対照が通り、
指定した runtime 失敗理由を確認した変異数。リンク ASSERT / 生成拒否は別列で、
この対応表の試験には無い。証拠ログは `~/os32-tmp/run/e12/`、全候補の rc と時刻は report.md。

| 試験 | 変異数 | 意図した runtime RED | 生き残り | compile/link error | timeout | ASSERT / 生成拒否 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| lease | 10 | 10 | 0 | 0 | 0 | 0 |
| gfx_kernel_fb | 16 (通常リスト14 + 別TU2) | 16 | 0 | 0 | 0 | 0 |
| gfx_reattach | 6 | 6 | 0 | 0 | 0 | 0 |
| surface_bundle | 23 | 23 | 0 | 0 | 0 | 0 |
| shm_user | 35 | 35 | 0 | 0 | 0 | 0 |
| kcg_boot | 7 | 7 | 0 | 0 | 0 | 0 |
| app_bb_overlap | 15 (既存14 + ピーク1) | 15 | 0 | 0 | 0 | 0 |

X-4 の別試験の弱い変異をこの表の合格へ混ぜない。native / 実 CR3 / TLB / 描画の
ゲスト実測はこの qemu ホスト集計の外で、PM の統合・H-4 等の関門で補う。

合計 **112/112 runtime RED**、生き残り / compile・link error / timeout / ASSERT・生成拒否は各 **0**。
app_bb_overlap の既存 14 変異も `app_bb_classification.log` で rc=1 / `FAIL:` 理由を
別確認し、異常終了を runtime RED に混ぜていない。e12 本体のコードの最終変更は
2026-10-07 22:14:53 +09:00。fix1 の blit_test.c の最終変更は 2026-10-07T23:08:44+09:00。
文書の最終変更は 2026-10-07T23:08:44+09:00 (fix1)。
その後の文書検査は `fix1_docs.log`、fix1 の対象検査と時刻は `fix1_report.md` に記録する。
