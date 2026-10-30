# SLIM_KERNEL 付表 — 移す自己試験ごとの kdebug の import

> 状態: **現行 (2026-10-25)** — 確定設計 SLIM_KERNEL_DESIGN の付表。出所: ~/os32-tmp/run/slim-kernel/IMPORTS.final.md。

# IMPORTS.r2 — S3移植5試験のimport契約

- DESIGN.r2.md §2の付表。確定時は本文と一緒に写す。indexは詰め直すため記載せず、symbol＋kindで同定する。
- 「既存」は現物generated JSONの同symbol/kindを再利用、「追加」は新stage S6へ追加。同一行は試験間で共有する。
- FUNCは公開関数をカーネルで実行。元TUの私的関数やインライン化された公開関数の本体をkdebugへコピーしない。
- GETTER/SETTERは値、PTRは番地。データの記号欄は元symbol、名前欄は生成する呼出し名。
- 生成SETTER/GETTER/PTRは既存公開データの接続であり、新しい状態操作API「3口」とは別勘定（容量には算入）。
- 本表はソース呼出し＋元.o再配置を照合。再配置だけではconのpush群やkbdのtake群が漏れる。

## con_sink_selftest → kst_con_sink

根拠: `kernel/console/con_sink.c:314–431`。

| 記号 | 種類 | 呼出し名 | 表への変更 |
|---|---|---|---|
| `res_owner_get` | FUNC | `res_owner_get` | 既存 |
| `res_owner_set` | FUNC | `res_owner_set` | 既存 |
| `con_sink_enable` | FUNC | `con_sink_enable` | 既存 |
| `con_sink_disable` | FUNC | `con_sink_disable` | 既存 |
| `con_sink_push_print` | FUNC | `con_sink_push_print` | 追加 |
| `con_sink_push_clear` | FUNC | `con_sink_push_clear` | 追加 |
| `con_sink_push_cursor` | FUNC | `con_sink_push_cursor` | 追加 |
| `con_sink_push_exit` | FUNC | `con_sink_push_exit` | 追加 |
| `con_sink_stat` | FUNC | `con_sink_stat` | 追加 |
| `con_sink_read` | FUNC | `con_sink_read` | 追加 |
| `con_sink_owner_exit` | FUNC | `con_sink_owner_exit` | 追加 |
| `con_sink_drop_count` | SETTER | `kdi_set_con_sink_drop_count` | 追加 |

## kbd_inject_selftest → kst_kbd_inject

根拠: `kernel/kbd_inject.c:320–397`。

| 記号 | 種類 | 呼出し名 | 表への変更 |
|---|---|---|---|
| `appslot_cur` | FUNC | `appslot_cur` | 追加 |
| `appslot_gfx_owner` | FUNC | `appslot_gfx_owner` | 追加 |
| `con_sink_reader_get` | FUNC | `con_sink_reader_get` | 追加 |
| `kbd_inject` | FUNC | `kbd_inject` | 追加 |
| `kbd_inject_pending` | FUNC | `kbd_inject_pending` | 追加 |
| `kbd_inject_take` | FUNC | `kbd_inject_take` | 追加 |
| `kbd_inject_peek` | FUNC | `kbd_inject_peek` | 追加 |
| `kbd_inject_discard` | FUNC | `kbd_inject_discard` | 追加 |
| `kdi_selftest_inj_push` | FUNC | `kdi_selftest_inj_push` | 追加 |
| `kbd_inject_drop_count` | GETTER | `kdi_get_kbd_inject_drop_count` | 追加 |
| `kbd_inject_drop_count` | SETTER | `kdi_set_kbd_inject_drop_count` | 追加 |

## lease_selftest → kst_lease

根拠: `exec/lease.c:361–470`。

| 記号 | 種類 | 呼出し名 | 表への変更 |
|---|---|---|---|
| `paging_kernel_pd_phys` | FUNC | `paging_kernel_pd_phys` | 既存 |
| `paging_current_cr3` | FUNC | `paging_current_cr3` | 既存 |
| `ledger_owner_new` | FUNC | `ledger_owner_new` | 既存 |
| `ledger_owner_retire` | FUNC | `ledger_owner_retire` | 既存 |
| `pgalloc_alloc_phys` | FUNC | `pgalloc_alloc_phys` | 既存 |
| `pgalloc_free_n_owner` | FUNC | `pgalloc_free_n_owner` | 既存 |
| `ledger_surface_create` | FUNC | `ledger_surface_create` | 追加 |
| `ledger_surface_release` | FUNC | `ledger_surface_release` | 追加 |
| `paging_addrspace_create_lease` | FUNC | `paging_addrspace_create_lease` | 既存 |
| `paging_addrspace_destroy` | FUNC | `paging_addrspace_destroy` | 既存 |
| `lease_acquire` | FUNC | `lease_acquire` | 追加 |
| `lease_check` | FUNC | `lease_check` | 追加 |
| `lease_release` | FUNC | `lease_release` | 追加 |
| `lease_revoke_all` | FUNC | `lease_revoke_all` | 追加 |
| `paging_as_audit` | FUNC | `paging_as_audit` | 追加 |
| `paging_lease_pte` | FUNC | `paging_lease_pte` | 追加 |
| `paging_load_cr3` | FUNC | `paging_load_cr3` | 追加 |
| `shlib_read_page` | FUNC | `shlib_read_page` | 追加 |
| `kdi_selftest_lease_result` | FUNC | `kdi_selftest_lease_result` | 追加 |
| `ledger_surfaces` | PTR | `kdi_ptr_ledger_surfaces` | 既存 |

## exec_tramp_user_selftest → kst_exec_tramp_user

根拠: `exec/exec.c:584–651`。

| 記号 | 種類 | 呼出し名 | 表への変更 |
|---|---|---|---|
| `kdi_selftest_exec_view` | FUNC | `kdi_selftest_exec_view` | 追加 |
| `paging_pte_flags` | FUNC | `paging_pte_flags` | 既存 |
| `vfs_cwd_user` | FUNC | `vfs_cwd_user` | 追加 |
| `vfs_cwd` | FUNC | `vfs_cwd` | 追加 |
| `vfs_devname_user` | FUNC | `vfs_devname_user` | 追加 |
| `vfs_devname` | FUNC | `vfs_devname` | 追加 |
| `kstrcmp` | FUNC | `kstrcmp` | 既存 |
| `path_get_drive_user` | FUNC | `path_get_drive_user` | 追加 |
| `path_get_drive` | FUNC | `path_get_drive` | 追加 |
| `path_get_cwd_user` | FUNC | `path_get_cwd_user` | 追加 |
| `path_get_cwd` | FUNC | `path_get_cwd` | 追加 |
| `ring3_wm_enter` | FUNC | `ring3_wm_enter` | 既存 |
| `ring3_wm_leave` | FUNC | `ring3_wm_leave` | 既存 |
| `ring3_in_syscall` | GETTER | `kdi_get_ring3_in_syscall` | 既存 |
| `ring3_in_syscall` | SETTER | `kdi_set_ring3_in_syscall` | 既存 |

## exec_kapi_layout_selftest → kst_exec_kapi_layout

根拠: `exec/exec.c:522–579`。

| 記号 | 種類 | 呼出し名 | 表への変更 |
|---|---|---|---|
| `kdi_selftest_exec_view` | FUNC | `kdi_selftest_exec_view` | 追加 |
| `kapi_reserved_nosys` | FUNC | `kapi_reserved_nosys` | 追加 |
| `kmemset` | FUNC | `kmemset` | 既存 |
| `os32x_layout_check` | FUNC | `os32x_layout_check` | 追加 |
| `ring3_ls_start` | PTR | `kdi_ptr_ring3_ls_start` | 追加 |
| `ring3_ls_end` | PTR | `kdi_ptr_ring3_ls_end` | 追加 |
| `__kernel_resident_end` | PTR | `kdi_ptr___kernel_resident_end` | 追加 |

## 追加数と生成規則

- 5試験の和集合は64契約。追加は44契約（FUNC=38、GETTER=1、SETTER=2、PTR=3）。旧5 FUNC削除後の全体は308枠。
- S2のFUNCは134→129、S3〜S5の開始indexも−5。GF_ONLY_SLOTSは旧264..267→259..262（番号直書きよりsymbolから解決）。
- S6の順序は本表の追加FUNCを試験順・表順に重複除去し、追加GETTER、SETTER、PTRの順に並べる。
- `STAGE_KINDS['S6'] = ('FUNC',) * 38 + ('GETTER',) * 1 + ('SETTER',) * 2 + ('PTR',) * 3`。公開定数は#define。
- 宣言は既存ヘッダから抽出。ring3_ls_start/endはexec/ring3_ls.hのconst u8配列、__kernel_resident_endはinclude/memmap.hのu32単体としてPTR生成。
- __kernel_resident_endはリンカ記号の番地が必要。GETTERでメモリ内容を読まない。既存生成形 `#define __kernel_resident_end (*kdi_ptr___kernel_resident_end())` と `&` の組合せを保持する。
- kapi_reserved_nosysの宣言が元.cだけにある分は既存生成器のsource=.c経路を使う。KAPI_FUNC_RESERVED=0でも参照を条件付きで消せることを検査する。
- FUNCのstub番地と実関数番地は異なる（gen_kdebug_imports.py:254）。kapi_reserved_nosysとの等値比較には生のimport slot値を使う。
- 型付き `KDI_ADDR_name` を必要な2記号だけ生成: `((__typeof__(&name))(uptr)kdebug_import_slots[KDI_INDEX_name])`。KDI_INDEXは生成#define。
- kapi_reserved_nosysの比較とshlib_read_pageのcallback渡しにKDI_ADDRを使用。通常の関数呼出しは既存tail-jump stubのまま。
- check_nmはこの2件のアドレス利用もmanifestで照合する。stubを比較して常時FAILにしたり、未解決参照がないことを理由にimportを消したりしない。

## 移動しない間接依存（元.oの再配置から消える理由）

| 記号 | 元の種類・操作 | r2での扱い |
|---|---|---|
| ring3_wm_depth | データ読書き（GETTER/SETTER相当） | kbdのpeek/take、execのwm_enter/leaveをFUNCとして呼ぶ。既存GETTER/SETTERは他のkselftest用に維持 |
| ring3_wm_depth_underflow / ring3_wm_shell_gen | データ読書き（GETTER/SETTER相当） | ring3_wm_leave内に残す。この5試験用の直接import追加は不要 |
| shell_service_wm_mark / ring3_guard_active / ring3_user_str | FUNC | wm_enterや*_userのカーネル実体から呼ぶ。移植先へ展開しない |
| lease_revoke_sid | FUNC | lease_revoke_allのカーネル実体から呼ぶ |
| lease_revoke_all_fail_count | データ読書き（GETTER/SETTER相当） | lease_revoke_all内の加算のまま。値をkdebugで読み書きしないので追加口不要 |
| lease_selftest_result | データ書込み（SETTER相当） | 新FUNC kdi_selftest_lease_result経由。従来の公開観測シンボルは残す |
| g_reader | static書込み | owner_exit(saved_owner)で既にNO_READER。末尾の重複代入だけ削除、新しい口なし |
| kapi / ring3_tramp_page | static読出し | 新FUNC kdi_selftest_exec_viewで取得 |
| tables_image / irq_save / irq_restore / P2V等 | 移植補助 / ヘッダinline・マクロ | tables_imageは同じ移植.oへ、IRQ/番地マクロは既存ヘッダ。別importは作らない |

## 代入・参照の置換許可リスト

- `con_sink_drop_count = 0`（413/431）→`kdi_set_con_sink_drop_count(0)`。
- kbd保存読出し→生成GETTERマクロ、`kbd_inject_drop_count = saved_drop`→`kdi_set_kbd_inject_drop_count(saved_drop)`。
- ring3_in_syscallの保存読出し→既存GETTERマクロ、0/1/savedの各代入→既存`kdi_set_ring3_in_syscall(...)`。
- ledger_surfaces、ring3_ls_start/end、&__kernel_resident_end→PTRマクロ。配列は内容でなく先頭番地、endは終端番地。
- lease結果代入、inj_push呼出し、exec状態読出し→新3口。g_readerの重複代入削除を許可。
- カーネル内inline依存→表記した公開FUNC呼出し、関数アドレス2件→KDI_ADDR。この対応表以外の呼先/副作用の変化は不許可。
- kapi_reserved_nosys比較、配列差分、callbackを実import tableで結ぶhost検査を加え、移植先の未解決記号とデータ左辺を検査する。

## S4a/bで回す既存19試験（init/kernel.c参照の現物一覧）

- 全てtools/tests/の下。各候補をMUTATE=0で実行し、文字位置/呼出し順序のassertを弱めない。補助定義はkernel_mainの後、宣言のみ前に置く。
- test_kcg_module.py、test_lan_module.py、test_memory_boot.py、test_kcg_boot.py、test_sh_truncation.py。
- test_e10c_lifecycle.py、test_cd_read.py、test_stack_entry.py、test_mem_stat.py、test_gfx_boot.py。
- test_clang_ast.py、test_dma_pool.py、test_module_policy.py、test_kdebug_init.py、test_shm_user.py。
- test_cirrus_module.py、test_c_dialect.py、test_device_reservation.py、test_bootlog.py。
- 文字位置の不一致が残れば、意味が等価な抽出先を追う最小修正だけを試験側へ行い、旧来の順序条件を保持する。

## S3a の基準値 (2026-10-25、25aeb119、S2/S4 統合後)

- `CROSS_DIR=/home/hight/opt/cross HOST32_RUNNERS=qemu TMPDIR=/home/hight/os32-tmp/run/slim-s3`、既定の `make kernel`。以下は移植前の測定値で、§1 の設計時点の値と区別する。
- kernel.elf: text=338,467 B、data=30,487 B、合計=368,954 B、BSS=210,616 B。vmkernel.lz4=224,278 B。
- kdebug.elf: text=93,650 B、data=1,136 B、BSS=31,369 B。kdebug.bin=111,172 B。kselftest.o: text=35,115 B、BSS=2,368 B。
- 移動予定の本体: con=1,689 B、kbd=1,661 B、lease=2,152 B、exec tramp=1,380 B、exec layout=465 B、計7,347 B。S3a の import は269枠、S6追加0、旧5枠は存続。
- 生の証拠: `~/os32-tmp/run/slim-s3/baseline/` の kernel.elf・kernel.map・kdebug.map・nm.txt・size.txt・vmkernel.lz4・kdebug.bin・kdebug_imports.json・本付表の変更前コピー。ビルドログは同親ディレクトリの `baseline-build.log`。
- `images/` が無いため両FDの空きクラスタ・同梱一覧は未測定。設計時点の値を今回の基準値に流用せず、S3fで候補像から採取する。ゲストの自己試験件数・結果もこの段では未確認。
