A 3 件 / B 4 件 / C 4 件

対象は `arch-audit-single`、main `3497386e7`。最も削りやすいのは、前景 I/O の互換コールバック経路、OSRI ビューアの二重実装、全 SHM を一括解放する旧経路です。**IRQ と前景、停止中の親と実行中の子、GUI と CUI の切替による競合は実際に起きるため、それぞれの境界は必要です。** 以下の行数は製品コードの純減見込みで、試験・文書は含めていません。

### A　構造を整理する価値が大きい

| 件 | 確認した経路と競合 | 整理案・概算・維持する契約 |
|---|---|---|
| **A1　前景 I/O の二系統** | `kernel/io_work.c:io_dispatch()` は Tier 別の pending、予算、繰上げを処理した後、`kernel/idle_work.c:idle_work_compat_run()` ですべての互換コールバックを呼ぶ。後者は予算や時計の準備と独立して進む。PC-98 は `usb_resident.c:usb_idle()`、PowerMac は `hostfs_boot.c` の `pm_hfs_link_poll` や `terminal_input.c` の `terminal_service_once` をここに登録する。`net/nic.c:net_tick()` → `io_request()` のような IRQ 側からの通知と、前景コールバックの再入防止は**必要**。 | 前景の登録・実行口を一つにし、時計が使えないときも進める仕事を明示する。サービス ID に対応した pending を一枚にすれば、Tier 変更時の pending 移送も消せる。**25～60 行減の見込み**。`io_pump` の戻り値・再入時の扱い、時計不成立時の USB・端末・HostFS の進行を維持。`test_idle_work`、`test_io_work`、`test_pc98_usb_resident` が関係。 |
| **A2　表示面の所有権と寿命** | `gfx/gfx_core_surface.inc:gfx_client_to_gshell()` は PC-98 の `platform/pc98/kernel.c` と PowerMac の `platform/powermac/shell_loop_ops.c` から呼ばれる共有実装。初回は CLIENT を boot → gshell に移し、以後も GUI へ入るたび `gfx_reinit_tvram()` で古いマップを失効させる。停止中の CUI 親が持つ TVRAM のリースや、V86 復帰時の DISPLAY の失効は**必要**。一方、CLIENT の初回移譲と、通常文脈で切替やコールバックを挟まない `gfx_reinit_surface_roles()` の二相間に、別のアプリが取得しに来る経路は確認できなかった。 | 初回移譲が台帳の所有者に要求する処理を棚卸しし、固定所有にできる面を絞る。先に、公開停止 → マップ失効 → 世代更新 → 再公開を共通化する。**15～35 行減の見込み**。`surface_lease`／`gfx_surface_lease` の失効、古い SID、再アタッチ、失敗時の公開停止を維持。`test_gfx_transfer`、`test_gfx_reinit`、`test_gfx_reattach` が関係。 |
| **A3　OSRI ビューアの二重実装** | PC-98 の `gfx/gfx_osri_view.c:gfx_osri_view()` と PowerMac の `platform/powermac/osri_view.c:gfx_osri_view()` が、受付、`osri_active`、時刻・呼出元の保存、画像確保、デコード、表示呼出し、後始末をそれぞれ担当する。PC-98 は planar4 のバックバッファを借り、PowerMac は別領域にデコードする。PowerMac の `drivers/video/of_fb/of_fb_osri.inc:of_osri_show()` は待機後の復元で `BUSY` を再試行する。待機中に別のリースを取得する具体的な製品経路は未確認。 | 共通の `gfx_osri_view()` に実行順序を置き、作業面の取得・返却と実際の表示・復元を各バックエンドに渡す。PowerMac で一時バッファを公開するグローバル `published` も、同期呼出しの引数にできる。**20～40 行減の見込み**。`gfx_osri_show` の `BUSY`・中断・復元、デコード失敗後の PC-98 バッファ清掃を維持。`test_osri_view` と PowerMac 側の `powermac_osri_host.c` が関係。 |

### B　範囲を絞ると整理できる

| 件 | 確認した経路と競合 | 整理案・概算・維持する契約 |
|---|---|---|
| **B1　I/O policy の所有者履歴** | `kernel/io_kapi.c:kapi_io_policy_set()` はシェルと USER アプリを受け付ける。`kernel/io_policy.c:commit()` は所有者ごとに旧表と世代を保存し、`io_policy_owner_exit()` は途中の所有者が終了した場合に履歴をつなぎ直す。親の設定中に子が設定し、終了後に親へ戻る経路は `exec/exec_finish.inc:exec_finish()` → `exec_reclaim_resources()` → `io_kapi_owner_exit()` で成立する。保存・復元は**必要**。製品内の設定利用例と、途中の所有者を除去する実使用は今回見つからなかった。 | 現行契約のままなら、二重の公開表と保存・復元処理の統合を検討する。前景 dispatch はその回の設定をスロットに取り込んでから callback を実行する。**10～25 行減の見込み**。所有者を一人に限定する案は KAPI の意味が変わる。`test_io_policy`、`test_io_kapi` の順不同終了・ID 再利用・dispatch 中の `BUSY` が判断基準。 |
| **B2　IN2 の排他・フォーカス・LOST** | `kernel/av_focus.c:select_owner()` → `input/hal/in2_reclaim.c:in2_on_focus()` が旧イベントを捨てて `LOST_FOCUS` を入れ、`in2_excl.c:transition()` が `LOST_EXCL` を入れる。押したまま切り替えると、旧利用者のキーが新しい入力先に混ざるため、押下状態と release 世代の記録は**必要**。`in2_ring_push()` と `in2_ring_pop()` は IRQ 生産者と前景消費者を保護する。gshell の `ring.rs:append()` は窓宛ての配送で、IN2 と購読先・消費時点が異なる。 | 共通 HAL のフォーカス変更と排他解除を一つの更新手順にまとめる。`in2_ring_lost()` の満杯時置換は、すべての呼出元で到達条件を確定してから簡略化する。**5～20 行減の見込み**。`in2_status`／`in2_wm_state` の世代、LOST の理由と順序、Ctrl+STOP、キー解放後の再入力を維持。`userland/ppc/guiprob*`、`test_gshell_excl` が関係。 |
| **B3　SHM の全回収経路** | `exec/exec_finish.inc:exec_reclaim_resources()` は `mm/shm.c:shm_free_owned()` を使う。親と子のブロックが同時に生きるので所有者表は**必要**。`shm_lock()` は書込み可否を変える公開機能で、CPU の同時実行とは別の役割を持つ。`shm_cleanup_all()` はリポジトリ内でホスト試験からの呼出しだけを確認した。 | `shm_cleanup_all()` と宣言を削り、試験の初期化を所有者別回収か fixture 初期化に置き換える。**約25～30 行減**。`sys_shm_alloc/lock/free`、予約領域、PTE の R/W 復帰、親の SHM 生存を維持。`test_shm_user`、`test_shm_lockwrite`、`shm_reuse_test` が関係。 |
| **B4　ATA 単一セクタ入口の重複** | `drivers/ata/ide_request.inc:ide_read_one()` と `ide_write_one()` は、存在確認・アドレス生成・`ide_pio_command()` 呼出しを方向別に複製している。`ide_pio.inc:ide_pio_command()` の BSY/DRQ 待ちは装置の状態待ち。通常要求経路に、複数アプリの取り合いを扱う永続キューやロックは見当たらなかった。 | 単一セクタの読書きを同じ内部入口にまとめる。複数セクタ入口へ寄せる場合は、SASI/CHS の分割と既存エラー順を照合する。**10～20 行減の見込み**。`test_ide_pio` の単一・複数転送、部分失敗、バイト順を維持。PowerMac の IDE は `platform/powermac/ide_boot.c` から入るため、同じ調停層を機種別に持つ構造は確認されなかった。 |

### C　機能上の必要性を確認した箇所

| 件 | 確認した経路と競合 | 整理案・概算・維持する契約 |
|---|---|---|
| **C1　exec の子回収・SQLite スタック・kmalloc** | `exec/exec_finish.inc:exec_finish()` は子の資源を回収し、親の文脈を復元してから WM に通知する。SQLite の rollback は FD 回収より先に必要。`kernel/sqlite_engine.c:sqlite_engine_run()` の専用スタックと `active` は共有スタックの再入を防ぎ、IRQ・例外文脈を拒否する。`mm/kmalloc.c:kheap_alloc()` はカーネルヒープと `exec/exec_heap.c` のヒープで共有済み。 | 所有者別回収、復元順、スタック切替と再入ガードは**必要**。**確認できた純減は 0 行**。SQLite の CPU 別スタック入口は呼出規約に従う。`test_owner_reclaim`、`test_sqlite_engine`、PPC の SQLite スタック試験が関係。 |
| **C2　IRQ の tick budget** | `kernel/irq/tick.c:tick_dispatch()` が開始・終了を管理し、`net/nic_rx.c:net_rx_demux()` 側の受信処理や `net/link.c` の送信処理が予算を使う。PC-98 の `usb_resident.c:usb_tick()` は `tick_budget_peek()` を使う。前景の Tier とは実行文脈が異なる。予算ロジックは共有済みで、PC-98 と PowerMac で時計の読み方が異なる。 | 長い IRQ 処理を抑え、RX/TX の進行機会を確保する仕組みは**必要**。**確認できた純減は 0 行**。`test_tick_budget`、`test_pc98_usb_resident` が関係。前景 Tier の繰上げ規則をさらに削れるかは、実負荷の計測が要る。 |
| **C3　USB の転送・切断・コールバック** | `drivers/usb/usb_req.c:usb_xfer_sync()` は `usb_busy` 中の同期呼出しを拒否する。`usb_service.c:usb_service()` は完了処理を進め、`usb_detach.c:usb_free_device_step()` は転送を止めてから解放する。制御要求は `usb_ctrl_async()` にハブや HID からも入る。OS の実行が一つでも、コールバック、抜去、DMA 完了は別の時点に起こる。 | 制御要求の直列化、同期再入の拒否、DMA 停止後の解放は**必要**。USB core は共有済み。**確認できた純減は 0 行**。`test_usb_core`、`test_usb_stack`、`test_usb_ohci` が関係。 |
| **C4　LAN と描画の装置側所有権** | PC-98 の `drivers/net/ne2000/ne2000_irq.inc:ne2k_irq_shared()` は前景のページ切替中に割り込むとレジスタに触れず前景処理へ回す。PowerMac の `drivers/net/gem/gem_tx.c:gem_tx_reclaim()` は DMA 完了後に記述子を回収する。`gfx/v2/lock.c:gfx2_lock()` は CPU マップの状態と装置 fence を検査する。 | NIC の IRQ と前景の排他、DMA の所有権、描画 fence と `DONOTWAIT` は**必要**。装置ごとにレジスタ・記述子が異なる。**確認できた純減は 0 行**。`test_ne2k_bh`、PPC LAN 試験、`test_gfx2_lock`・`test_gfx2_fence` が関係。 |

gshell にも所有状態はあります。`userland/gshell/src/fullscreen.rs:observe()` は `gfx_screen_owner` を読んで、子から戻ったときのパレット復元を判断し、`lease.rs` はフォーカスされた窓の色を適用します。画面所有者と窓ごとの要求色には別々の役割があり、協調切替でも両方が必要です。この範囲で独立した時間制リースは確認できませんでした。

### 上位 5 件

1. **A1**　前景 I/O の登録・実行口を統合する。時計が使えない状態での装置・端末の進行を先に確認する。
2. **A3**　OSRI の実行順序を共通化する。作業面と復元は各バックエンドが担当する。
3. **A2**　表示面の失効・世代更新・再公開をまとめる。初回 CLIENT 移譲の削除は台帳の所有条件を確認してから判断する。
4. **B3**　SHM の旧全回収経路を削除する。終了時には現行の所有者別回収を使う。
5. **B1**　I/O policy の保存表を簡素化する。複数所有者の復元を縮める判断には KAPI の利用実態が要る。

### 調べきれなかった範囲

- PowerMac の OSRI 復元待ちで別のリースが実際に取得される経路、I/O policy を複数アプリが設定する製品上の利用例、Tier 繰上げを外した場合の最大待ち時間は未確認です。
- GeForce・Trident・Cirrus の backend 個別の復元状態、IN2 の満杯時置換の全到達経路、ATAPI の全エラー復帰、外部アプリの KAPI 利用までは追っていません。
- 読み取り専用の静的調査です。ファイル変更、実装用 preflight、ビルド、試験、エミュレータ・実機検証は行っていません。
