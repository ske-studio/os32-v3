# TASK_REDUNDANCY — ユーザーランド外の冗長コードの整理

> 状態: **実装中 (2026-10-30)** — **A01・A06 は main** (sol と terra に同じ依頼を並行で出して比べた: 差分は 2 段とも同一、所要は A01 sol 5:01 / terra 3:42、A06 sol 5:36 / terra 3:54、完了条件は両方とも満たした — terra の版を取り込み、astra レビュー Approve。記録 ~/os32-tmp/run/terra-eval.md)。それまで: **計画 (2026-10-29)** — 調査・裏取りを段へ分解した票。実装・受入は未着手。
> 上位: [ROADMAP](../../ROADMAP.md)・開発規約 [POLICY_DEV](../../POLICY_DEV.md) §2「ソースファイルの大きさ」。先行: [TASK_GEFORCE6200](../realhw/TASK_GEFORCE6200.md)・[TASK_HWDEV](../realhw/TASK_HWDEV.md)。調査の原文 (`REPORT.md`・`VERIFY.md`・`REPORT2.md`・`VERIFY2.md` の引用先) は `~/os32-tmp/run/redundancy/`。
>
> 作成: sol (gpt-6.1-sol) が起草、Opus のレビュー 1 回を反映した r2 を元に、票に載っていないファイルの再調査 (REPORT2・REPORT3、ユーザー指示 2026-10-29) と Opus の裏取り (VERIFY2) の訂正を取り込んだ **r3**。実装は GeForce の後、§2 の 2 段 (A01・A06) を sol と terra に並行で出して比べる (ユーザー 2026-10-29)。

OS32 は単一利用者・単一タスク・協調切替で攻撃者を想定しない。目的は、使わない定義と重複を減らし、既存の振る舞いを保って大きいソースを分けること。新しい防御・権限分離は足さない。
対象は BRIEF の kernel/・kdebug/・hwdev/・drivers/・fs/・exec/・lib/・gfx/・kapi/・platform/・net/・arch/・boot/ の手書きコード。ユーザーランド、生成物、第三者コード、フォント表は除く。ヘッダ・ビルド定義・試験は対象変更に必要な追従だけ含める。
**着手は GeForce の実装（4c の実機の回と本番の画面ドライバ）の後**（`docs/POLICY_DEV.md:131`）。この票の作成ではリポジトリを読み、ビルド・試験・NP21/W・実機操作・コード修正を行わない。
基点は main **`7dc9bcbd`**（作業木差分なし）。REPORT は `a839233d`、REPORT2 は `b836a3f1`、r2 票は `e7302178`。以下の現行行数は今回再計数した物理行。
**r3 要約:** 既存の段・ID と分割対象 14 本を保持し、REPORT2 の 28 件中 27 件を F/G/H 群の 24 段へ追加（G02a は試験補強）。U2-02 は F11 (pack32/cmp32 だけ除去、char_at は残す)。既存段の純減 366〜517 行＋追加 333〜435 行＝**計 699〜952 行 [推]**（F11 の約 30 行・REPORT3・試験追従・移動は除外）。

## 1. 根拠と読み方

- 件 ID と元の根拠は `REPORT.md:24`〜`:110` および `REPORT2.md` の各件。採否・順・危険度は `VERIFY.md:3`〜`:9` と `VERIFY2.md:2`〜`:14` を優先する。本文のリポジトリ内パスはリポジトリの根が起点、REPORT/VERIFY は `~/os32-tmp/run/redundancy/` にある。
- 規則は `CLAUDE.md:53`〜`:75` の [C1]〜[D4]、割当て・完了条件は `docs/tasks/agents/ROLES.md:12`・`:17`〜`:24`、分割は `docs/POLICY_DEV.md:129`〜`:131`。状態行は同 `:347`〜`:362`。
- GNU11・既存 kstring・既存 KAPI の番号/ABI・P2V/V2P・DMA の境界と寿命・HAL の制約を保つ。SDK の公開互換と生成物の扱いも従来どおり。分割のために契約の enum を増やさない（`docs/POLICY_DEV.md:143`）。
- 表の `path:入口 (N)` は根拠の入口行と**ファイル全体の現行 N 行**。`0` は新規候補。削減は追加ヘルパを引いた純減の**推測**。試験の追従分と移動は削減に数えない。数値は着手時に再確認する。
- 実装者欄の「terra 可」は、表の試験追従を含め 1,000 行超に触らない場合だけ。実際の編集対象に 1 本でも超過があれば **sol か astra** に替える。失敗時は terra → sol → astra、sol の実装の誤り 3 回で astra。同じ作業木・依頼パックを引き継ぐ（ROLES `:12`）。
- 直接試験欄の名前は `tools/tests/` 起点。`T` は §4 の追従候補群、`M` は既存の変異の目印の一意性の確認（§6）。試験を回すだけなら「触るファイル」には数えない。
- ゲスト欄の「束」は次の指定関門へまとめることを示し、ホスト合格で受入完了にしない。「要」はその段の受入前に必要。共通の最小回帰と段固有の項目は §6。

## 2. 最初の 2 段の sol / terra 比較（ユーザー方針 2026-10-29）

**A01 と A06 はそれぞれ同じ依頼を sol と terra に並行で出す**。同じ基点・完了条件・検査台本を使い、別々の作業木で比較する（計 4 成果、取り込むのは各段 1 成果だけ）。A01 の比較を終えて A06 の比較へ進む。試験の同時実行は全体で 2 本まで。
選定理由: A01 は IDE の 487 行と既存試験の 232 行だけ、A06 は宣言・実体・shim と試験の 5 ファイル（76・174・37・218・217 行）で閉じる。両方とも優先 1 の未使用除去で、1,000 行超への編集も振る舞いの設計変更も要らない。A01 は警告抑止の追従、A06 は宣言・実体・shim を一度に直す判断も比べられる。A02 は 13 行の削除だけで差が出にくいため比較対象から外す。
比較は依頼から完了までの所要、最後の変更後の列挙検査と報告の充足、必要な差分の行数・ファイル数、同一の観点で astra が挙げた P1/P2/P3 と直しの往復を記録する。速さだけで選ばない。記録先は将来の段の証拠 `RESULT.md` と既存の terra 評価記録（ROLES `:12`）。

## 3. 未使用・共通化の段（上から実施）

各行は 1 回の依頼。§2 の A01・A06 を先行し、残り A → B → F01〜09 → G01 → G02a/b → F10 → G03〜11 → C → D → E（抽出）→ S（分割）→ L/H（後回し）の順。同じソースを触る段は直列。各段の状態はすべて計画。旧 A03/B06 は A03、旧 B03〜B05 は B03 に統合し、他の ID は保つ。

| 段 | 中身・件 ID | 触るファイルと現行行数（入口が根拠） | 実装者の候補 | 削減の推測 | 退行の危険 | 直接の試験 / 変異の目印への影響 | ゲスト受入 |
|---|---|---|---|---|---|---|---|
| A01 | U01、ide_wait_ready 除去と警告抑止追従 | `drivers/ata/ide.c:88 (487)`、`tools/tests/test_bootinfo.py:171 (232)` | terra 可、sol と比較 | 10 | 低 | `test_bootinfo.py --target`。当該 -Wno-unused-function と説明を除く。他の M は維持 | 束: A/B 終端で IDE 起動・読出し |
| A02 | U04、未使用 inline 2 本を除去 | `gfx/gfx_internal.h:79 (93)` | terra 可 | 13 | 低 | `test_dep_dir_d_display.py`・`test_dep_dir_d_splash.py`・`test_gfx_kernel_fb.py`。`gfx_kernel_fb_host.c:25` も当該ヘッダを include（`tools/check_map.d/check-gfx-host.yaml:31`）。既存 M は別ソースで維持 | 束: A/B 終端で表示 |
| A03 | U02/U09、path_slen・古い説明と path_basename・宣言を除去 | `lib/path.c:21・:105 (114)`、`lib/path.h:54 (59)` | terra 可 | 14 | 中 | 直接 host 試験は未確認。`make kernel` で実ソースを組み、参照を再検索。既存 M なし、追加不要 | 束: B03 でパス操作 |
| A04 | U03、fdc_motor_off 除去と試験の抑止追従 | `drivers/block/fdc.c:421 (1345)`、`drivers/block/fdc.h:77 (465)`、`tools/tests/test_fdc_seek.py:23 (137)`・`test_fdc_track.py:245 (603)` | sol か astra | 4 | 低 | `test_fdc_seek.py`・`test_fdc_track.py`。後者の `:245` の説明、`:250` の FLAGS 全体の -Wno-unused-function、`:513` の target ビルドで fdc.c だけの抑止を整理。`:250` は他ソースにもかかるので削除可否は確認して決める。NR/NW の M は維持 | 束: A/B 終端で FDD 読出し |
| A05 | U10、未参照の別名 2 定義だけ除去 | `exec/exec.c:256 (2534)`、`drivers/video/cirrus/backend_cirrus.c:81 (606)` | sol か astra | 2 | 低 | `test_exec_r1.py`・`test_cirrus_module.py`・`test_h3_park_resume.py`（check-h3-park-resume-host）。M 維持 | 束: A/B 終端で起動・アプリ・描画 |
| A06 | U05、insw_rep の宣言・実体・shim を同時除去 | `platform/pc98/platform_io.h:58 (76)`、`include/io.h:66 (174)`、`tools/tests/hostdrv_hostshim/io.h:30 (37)`、`tools/tests/test_serial_portc.py:79 (218)`・`test_gfx_kernel_fb.py:102 (217)` | terra 可、sol と比較 | 本体 6、追従分別 | 低、警告は推測 | `test_serial_portc.py`・`test_gfx_kernel_fb.py`・`test_b8_hostdrv.py`。既存 M 維持 | 束: A/B 終端でシリアル・描画・HostDrv |
| B01 | U06、未使用の外向き SCSI スタブと宣言 | `drivers/base/dev.c:171 (354)`、`drivers/base/dev.h:92 (115)` | terra 可 | 6 | 中 | `test_module_runtime.py`。登録/解除の M 維持 | 束: B03 で装置登録 |
| B02 | U07、未使用の polled 16 進出力と宣言 | `drivers/char/serial.c:910 (951)`、`drivers/char/serial.h:280 (330)` | terra 可 | 15 | 中 | `test_serial_portc.py`・`test_serial_vfast.py`。polled 出力の既存 M 維持 | 束: B03 でシリアル |
| B03 | U08、VFS・HostDrv・ext2 のアクセサと pipe_get_capacity を除去 | `fs/vfs.c:750 (939)`、`fs/vfs.h:359 (409)`、`fs/hostdrvfs.c:984 (992)`、`fs/hostdrvfs.h:20 (22)`、`fs/ext2_super.c:510 (547)`、`fs/ext2.h:253 (273)`、`fs/pipe_buffer.c:84 (116)`、`fs/pipe_buffer.h:43 (52)` | terra 可 | 約 33 | 中（VERIFY 修正） | `test_vfs_mount_dev.py`・`test_vfs_fd_path.py`・`test_hostdrv_list.py`・`test_ext2_write_io.py`・`test_owner_reclaim.py`・`test_h3b_fixtures.py`。callback フィールドを残し、所有者/解放を含む M 維持 | 要: A/B 一括の起動・装置登録・シリアル・パス・FS・pipe |
| C01 | D05、dev_streq を kstrcmp == 0 へ | `drivers/base/dev.c:24 (354)` | terra 可 | 約 6〜8 | 中 | `test_module_runtime.py`・`test_cd_module.py`。登録先の文字列一致、既存 M 維持 | 束: C02 で装置名の解決 |
| C02 | D05、str_len / str_cpy を kstrlen / kstrlcpy へ | `fs/vfs_fd.c:20 (876)`。T-FD の追従候補 2 本（§4） | sol か astra（試験が超過） | 約 6〜10 | 中 | `test_vfs_fd_path.py`・`test_sqlite_vfs_identity.py`・`test_kstring_c.py`。容量・型・NUL と M の追従 | 要: FS の open/read/write、C01 の装置名 |
| C03 | D11 の手本: text 内判定 1 個を共用し PCM/FM と runtime を置換 | `kernel/module/module_runtime.h (80)`・`module_runtime.c:221 (236)`、`drivers/sound/cs4231/pcm_module.c:132 (194)`・`fm_module.c:153 (212)`。T-M1 | terra 可、手本は sol | 合計は C05 に記す | 高（参照と初期化順） | `test_module_runtime.py`・`test_pcm_module.py`・`test_fm_module.py`。範囲の M を helper/入口へ追従 | 要: 部品の起動・PCM/FM の従来動作 |
| C04 | D11、同じ helper を KCG/FEP/Cirrus へ適用 | `drivers/video/kcg/kcg_module.c:51 (85)`・`fep_module.c:117 (172)`・`cirrus_module.c:37 (84)`、`module_runtime.h (80)`。T-M2 | terra 可 | C05 と共通 | 高 | `test_kcg_module.py`・`test_fep_boundary.py`・`test_cirrus_module.py`。各入口の M 維持/移設 | 要: glyph・IME・描画、MINIMAL |
| C05 | D11、V86/LAN/SQLite に適用、API 表 8 か所を閉じる | `kernel/v86/v86_module.c:167 (201)`・`lan_module.c:176 (234)`・`sqlite_engine.c:104 (358)`、`module_runtime.h (80)`。T-M3 | terra 可 | C03〜05 計 4〜10、再見積もり | 高 | `test_v86_module.py`・`test_lan_module.py`・`test_sqlite_module_proto.py`・`test_module_runtime.py`。境界/失敗の M 維持/移設 | 要: V86・LAN・DB と部品の欠落起動 |
| C06 | D02、KAPI 内部の同一範囲判定 3 本を共用 | `kapi/kapi_host.c:31 (98)`、`kapi/kapi_sys.c:201 (356)`、`kapi/range_internal.h (0)`。T-K | terra 可 | 8〜12 | 中（VERIFY 修正） | `test_kapi_ranges.py`・`test_kapi_bounds.py`・`test_net_link.py`。NULL/overflow/範囲の M を一意に追従 | 要: KAPI/FS/ネットの既存操作 |
| C07 | D03、lease の文脈述語を paging 側へ一本化 | `mm/paging.c:1435 (1696)`、`mm/paging.h (399)`、`exec/lease.c:23 (404)`。T-LEASE | sol か astra | 3〜6 | 高 | `test_lease.py`・`test_surface_lease.py`・`test_gfx_attach.py`。追加 PT 検査と M を残す | 要: lease・描画・アプリ終了、次を積む前 |
| D01 | D04、固定幅 LE の kernel 3 本を内部ヘッダへ | `kernel/module/module_runtime.c:10 (236)`・`module_image.c:17 (249)`・`bootinfo_check.c:21 (280)`、`lib/le_internal.h (0)`。T-LE | terra 可 | D03 と合算 | 中（純粋な読出し） | `test_module_runtime.py`・`test_module_image.py`・`test_bootinfo.py`。rd32 を名指す M を追従 | 束: D03 で起動/部品 |
| D02 | D04、同じ固定幅 LE を残る 4 本へ | `drivers/block/loop_dev.c:111 (1177)`、`fs/sfs_proto.c:31 (190)`、`net/link.c:66 (1329)`、`boot/vk32_boot.c:26 (139)`、`lib/le_internal.h (0)`。T-LE | sol か astra | D03 と合算 | 中（boot の受入要） | `test_h3b_fixtures.py`・`test_net_link.py`・`test_vk32_crc.py`。非整列/符号・既存 M 追従 | 要: boot・loop・転送 |
| D03 | D04、可変幅 LE の重複だけ共用 | `kdebug/gfinit_decode.c:221 (756)`・`gfinit_pll.c:60 (246)`、`lib/le_internal.h (0)`。T-LE | terra 可 | D01〜03 計 15〜25 | 中（VERIFY の過大評価を修正） | `test_gfinit.py`・`test_gfinit_pll.py`。幅と境界・既存 M を維持/追従 | 要: 部品起動、既存 gfinit inspect |
| D04 | D08、NE2000 Remote DMA の反復だけ共用 | `drivers/net/ne2000/ne2000.c:253 (1057)`、`tools/tests/test_ne2000_dma.py (0)`・`ne2000_dma_host.c (0)`、同名 check 対応表/登録 mk（各 0） | sol か astra | 8〜14 | 高（PIO/再試行） | 新規候補 `test_ne2000_dma.py` で実ソースの両方向・奇数長・timeout。既存 `test_tick.py` と ring 試験だけでは DMA を証明しない。新 M は一意 | 要: NE2000 構成で送受信 |
| D05 | D09、HostDrv 削除を属性付き内部関数へ | `fs/hostdrvfs.c:822 (992)`、T-FD と必要なら `tools/tests/test_b8_hostdrv.py (72)` | sol か astra（T-FD 超過） | 8〜14 | 高（削除/close 順） | `test_b8_hostdrv.py`・`test_vfs_fd_path.py`。rmdir/unlink の属性・失敗の M 追従 | 要: HostDrv の rmdir/unlink と失敗時 |

U08 は全 8 ファイルが 1,000 行未満で削減も約 33 行（`REPORT.md:63`）のため、terra の 1 段 B03 にまとめる。A03 の U09 と B 群は定義/宣言だけでなく、KAPI・生成表・ASM・試験も参照検索し直し、外向きの削除が既存の契約を変えないことを確認してから着手する。
D05 は `lib/kstring.h:30`・`:33`・`:50` の型・容量契約に従う。kstrlcpy の戻り値を新しい意味に使わず、呼出しの有効容量と NUL 終端を保つ（`fs/vfs_fd.c:252`・`:788`、`drivers/base/dev.c:310`）。
D11 は型付き init・API 表のコピー・candidate_valid・rc 判定・IRQ/tick/DMA 参照を各入口に残し、既存の text 判定だけを共用する。PCM/FM は init の rc より範囲検証が先、KCG は rc が先（`pcm_module.c:127`・`:134`、`kcg_module.c:45`）。順をそろえない。
**8 か所は API 表の検証入口**（PCM/FM/KCG/FEP/Cirrus/V86/LAN/SQLite）。さらに `kernel/module/module_runtime.c:221` の handler 用同式にも同じ述語を使う。`VERIFY.md:4` の拡張範囲を全て含め、参照取得・DMA ラッパそのものの共通化は本段に混ぜない。

### 3.1 REPORT2 の追加段（VERIFY2 の推奨順）

F は未使用・形、G は重複。F01 → F02 → F03 → 未使用アクセサ F04〜08 → F09 → G01 → G02a/b を優先する。F10 は G02b 後、同じ snd_engine.c の G08 より前。新規段もすべて計画。字面依存と実ソース include の追従は §4 の T-R3 群を同段に含める。

| 段 | 中身・件 ID | 触るファイルと現行行数（入口が根拠） | 実装者の候補 | 削減の推測 | 退行の危険 | 直接の試験 / 変異の目印への影響 | ゲスト受入 |
|---|---|---|---|---|---|---|---|
| F01 | R2-03、無効な pc98_count_present と呼出し・旧説明を除去 | `drivers/video/pc98/backend_pc98.c:101・:127 (180)`。T-R3-P | terra 可 | 19〜27 | 低 | `test_gfx_kernel_fb.py`・`test_boot_splash_native.py`・`test_gfx_bounds.py`。削除名の直接参照なし。backend を組む fixture と実転送点の計数 M を維持 | 束: F09 で PC-98 表示・present_bytes |
| F02 | U2-03/U2-12、空の disk.c と未展開 ISR_NOERR を除去 | `drivers/disk.c:14 (14)`、`build/kernel.mk:39 (366)`、`arch/x86/kernel/isr_stub.asm:150 (759)`。T-R3-ASM | terra 可 | 22〜23 | 低 | `test_dep_dir_d3_disk.py`・`test_ledger.py`・`test_fdc_track.py`。disk.h (26) は維持。ASM 字面解析の IRQ/例外入口と M は不変、古い .d の削除ソース参照も扱う | 束: F09 で起動・disk・IRQ/FDD |
| F03 | U2-01/U2-08、GUI weak 既定 2 本と旧 peek の gui_next_shell を除去 | `kernel/gui.c:221・:256 (270)`、`kernel/gui.h:56 (59)`。T-R3-GUI | terra 可 | 22〜29 | 低（VERIFY2） | `test_ring3_guard.py`・`test_font_source.py`・`test_d2_services.py`。gui.c の実 include とヘッダの字面抽出を確認。強い gfx 本体と gui_take_next_shell を残す、M 維持 | 束: F09 で GUI/CUI・同一シェル再要求 |
| F04 | U2-04/U2-05、mouse_buttons/set_rate/get_mode と np2_recv_val の実体・宣言を除去 | `drivers/input/mouse.c:137・:160・:178 (250)`・`mouse.h:29 (69)`、`drivers/char/np2sysp.c:62 (120)`・`np2sysp.h:28 (36)`。T-R3-MOUSE | sol か astra（PEGC fixture 超過） | 28 | 中 | `test_kbd_dlog.py`・`test_d2_services.py`・`test_pegc_mode.py`。mouse/np2 実 include・pointer fixture を追従、mouse_read_buttons・送信・NP2PORT_VAL と M は維持 | 束: F09 で通常/seamless mouse |
| F05 | U2-07/U2-09/U2-11、rtc_uptime_sec・shlib_version/text_end・TSS range 2 本を除去 | `drivers/char/rtc.c:157 (179)`・`rtc.h:77 (79)`、`mm/shlib.c:14・:65・:84 (132)`・`shlib.h:32 (56)`、`arch/x86/kernel/tss.c:79・:87 (104)`・`tss.h:67 (74)`。T-R3-STATE | terra 可 | 25〜26 | 高（shlib/TSS） | `test_d2_services.py`・`test_shlib_high.py`・`test_double_fault.py`・`test_exec_r1.py`。g_version と専用代入も除去、g_text_pages・port 単位/deny_all/is_allowed を残す。字面/include と M 維持 | 要: 起動・時刻・shlib アプリ・ring3 I/O、次を積む前 |
| F06 | U2-06、pci_present/seen・pci_bind_reason_get のアクセサだけ除去 | `drivers/pci/pci.c:298・:308 (340)`・`pci.h:36・:63 (80)`、`drivers/pci/pci_bind_match.c:29 (164)`・`pci_bind.h:75 (92)`。T-R3-PCI | terra 可 | 15〜18 | 中 | `test_pci_decode.py`・`test_pci_bind.py`・`test_device_reservation.py`・`test_e82557_drv.py`・`test_lan_module.py`。PCI の字面抽出/include・M を維持。g_pci_present/seen・s_reason は残し pci.h の説明を直す | 束: F09 で PCI 列挙・NIC 部品 |
| F07 | U2-10、crc32_stream_init/update/final と専用説明・宣言を除去 | `lib/crc32.c:20 (33)`・`crc32.h:26 (30)`。T-R3-CRC | sol か astra（CRC fixture 超過） | 15〜21 | 中 | `test_hsync_h1.py`・`test_lanfs.py`・`test_serialfs.py`。関数名の試験参照なし、実 core include と calc の M を維持、CRC32_INIT/core は残す | 束: F09 で CRC を伴う読込・同期 |
| F08 | U2-13、atapi_present だけを除去 | `drivers/ata/atapi.c:678 (825)`・`atapi.h:163 (218)`。T-R3-CD | sol か astra（CD fixture 超過） | 約 5 | 中 | `test_cd_read.py`。`cd_read_host.c:76` は atapi.c を include、`:2132` の atapi_test_unit_ready 呼出しは残す。媒体確認/再試行・既存 M 維持 | 束: F09 で CD 起動/読出し・媒体無し |
| F09 | R2-02、clip 成功後の重複検査と再 clip だけ除去 | `gfx/gfx_vram.c:88・:98 (506)`。T-R3-P | terra 可 | 12 | 中 | `test_gfx_bounds.py`・`test_kapi_bounds.py`・`test_gfx_kernel_fb.py`。入口 gfx_clip_screen を残し、幅 640・32px 整列の事後条件を確認。実ソース fixture/M を維持 | 要: F01〜08 の束＋400/200 行・負座標/端の矩形 |
| G01 | D2-01、shell_print_utf8/console_write の復号後の描画を共用 | `kernel/console/console.c:260・:353 (466)`。T-R3-CON | terra 可 | 35〜50 | 高 | `test_con_sink.py`・`test_dep_dir_d_console.py`・`test_bootlog.py`・`test_ime_display.py`。実 console include の fixture と M を維持、NUL/長さ入力・不完全 UTF-8・sink/bootlog/RSHELL・cursor 同期は各入口に残す | 要: ANK/漢字/代替・折返し/scroll・入力表示 |
| G02a | D2-03 の前提、read_file/read_stream の違いを試験で固定 | `tools/tests/test_ext2_read_bound.py (51)`・`ext2_read_bound_host.c (240)` | terra 可 | 0 | 低（試験のみ） | 同試験の実 ext2_file.c include を拡充。端数・全ブロック・範囲外を書かない境界・hole 打切り・エラー・stream だけの通常ファイル判定を固定。新 M は一意 | 不要: G02b の受入へ |
| G02b | D2-03、inode 内の範囲読込を共用（G02a の後） | `fs/ext2_file.c:7・:60 (538)`、同試験 2 本、T-R3-EXT | sol か astra（試験追従が超過） | 25〜35 | 高 | `test_ext2_read_bound.py`・`test_ext2_write_io.py`・`test_b8_open.py`・`test_vfs_fd_path.py`。実 include の複数 fixture と M を追従、read_file に通常ファイル検査を足さず、hole/戻り値/累積を維持 | 要: ext2 一括/stream・exec のヘッダ/本体読込 |
| F10 | R2-01、SE 定数表へ整理し snd_init 直前の二重 duration ゼロ化を除去 | `drivers/sound/fm/snd_engine.c:501・:655 (912)`、`tools/tests/fm_playback_host.c (84)`・`test_fm_module.py (111)` | terra 可 | 15〜25 | 中 | `test_fm_module.py` が実 snd_engine.c を include する上記 fixture を組む（`:91`）。6 定義・未定義 ID の duration=0・可変表を保持するケースを補う。対象名の字面解析/既存 M は未検出、FM 側の M は維持 | 要: FM/SSG の既存 SE と未定義 ID |
| G03 | D2-02、present_dirty/nosync の共通部 | `gfx/gfx_vram.c:246・:279 (506)`。T-R3-P | terra 可 | 12〜18 | 高 | `test_gfx_kernel_fb.py`・`test_gfx_attach.py`・`test_boot_splash_native.py`。実ソース fixture の M を追従。非 flip の VSYNC、dirty だけの gfx_diag_px、snapshot/merge/flush/flip・commits 順は維持 | 要: 同期/非同期・flip 有無・dirty 診断 |
| G04 | D2-04 前半、FAT 一括/stream の読込だけ共用 | `fs/fatfs_vfs.c:211・:348 (807)`、`tools/tests/test_fdc_track.py:195 (603)`、T-R3-FAT | sol か astra（T-FD 超過） | G05 と合算 | 高 | `test_fatfs_stat.py`・`test_fdc_track.py`・`test_vfs_fd_path.py`。字面の open→seek→read→close 検査を helper/入口の両方へ追従。full 側は seek せず、失敗/close 順と M 維持 | 要: FAT 一括/stream・FDD 読込 |
| G05 | D2-04 後半、FAT 一括/stream の書込だけ共用 | `fs/fatfs_vfs.c:235・:375 (807)`、`tools/tests/test_bootlog.py:216 (291)`、T-R3-FAT | sol か astra（T-FD 超過） | G04〜05 計 20〜30 | 高 | `test_fatfs_stat.py`・`test_bootlog.py`・`test_fdc_track.py`・`test_vfs_fd_path.py`。字面の return (int)bw と close エラー検査を追従。CREATE_ALWAYS/OPEN_ALWAYS・full 側 seek 無し・remount・M 維持 | 要: FAT 上書き/offset 書込・bootlog・close 失敗 |
| G06 | D2-05、inode の位置計算と既存ブロック読込だけ共用 | `fs/ext2_inode.c:12・:55 (584)`。T-R3-EXT | sol か astra（試験追従が超過） | 8〜12 | 高 | `test_ext2_write_io.py`・`test_b8_open.py`・`test_ext2_empty_name.py`・`test_vfs_fd_path.py`。実 inode include と M を追従。decode/encode/write-back と同一ブロックの他 inode を保持 | 要: ext2 inode 更新・同一ブロックの他ファイル |
| G07 | D2-06、free/mark_leaked の span 探索を共用 | `mm/dma_pool_math.c:154・:185 (203)`、`tools/tests/test_dma_pool.py:82 (234)`・`dma_pool_host.c:16 (460)` | terra 可 | 6〜10 | 高 | `test_dma_pool.py`・`test_h4_backing.py`。test_dma_pool.py の span/state の字面 M と実 include の free/leaked ケースを同時追従（VERIFY2 の dma_pool_host.c 指摘を含む）。bad_free・USED 限定・LEAKED 非再配布、呼び側 IRQ 保護を維持 | 要: DMA 確保/解放・音源/NIC、次を積む前 |
| G08 | D2-07、FM/SSG SE の BGM 借用保存だけ共用 | `drivers/sound/fm/snd_engine.c:572・:610 (912)`、`tools/tests/fm_playback_host.c (84)`・`test_fm_module.py (111)` | terra 可 | 10〜16 | 高 | `test_fm_module.py`。実 snd_engine include の fixture を FM/SSG の逆走査・再借用・復元で補強。関数名の字面依存なし、音源側の M 維持。発音停止/volume/period/decay と IRQ 区間は各入口に残す | 要: SE 割込み前後の BGM 復元 |
| G09 | D2-08、IRETD_USER を内部 .inc に置き ring3 でも展開 | `arch/x86/kernel/isr_stub.asm:89 (759)`・`ring3_entry.asm:75 (153)`、`kernel/iret_user.inc (0)`、`build/kernel.mk:283・:355 (366)`、T-R3-ASM | terra 可 | 約 12（VERIFY2） | 高 | `test_ledger.py`・`test_fdc_track.py`・`test_e10c_lifecycle.py`。字面解析は .inc と入口を分けて読む形へ追従し M を一意に保つ。マクロは元名、cli/popad の位置・VM/RPL・DS 復元を保ち、展開後命令を照合 | 要: IRQ/例外・ring3/V86 復帰、次を積む前 |
| G10 | D2-09、kutoa_dec/hex の基数変換と詰め直しを共用 | `lib/kprintf.c:33・:63 (296)`、`tools/tests/test_kprintf_attr.py (164)`・`test_kapi_print.py (91)`・`kapi_print_host.c (215)` | terra 可 | 15〜22 | 中 | `test_kprintf_attr.py`・`test_kapi_print.py`・`test_gfinit_owner.py`。実 kprintf.c を組む kapi_print_host.c に kutoa の直接ケースを足し、0・有効な小容量（2 以上）・切詰め・大小文字・hex shift を固定。kprintf 入口の字面カウント (`:122`) と attr M は別関数で維持。公開入口と戻り値を保つ | 束: G11 で数値/ログ表示 |
| G11 | D2-10、seamless mode=0 の OUT 2 組を np2_send_val(0) へ | `drivers/input/mouse_seamless.c:33・:91 (121)`、`drivers/char/np2sysp.c:51 (120)`、T-R3-MOUSE | sol か astra（PEGC fixture 超過） | 6 | 中 | `test_pegc_mode.py`・`test_kbd_dlog.py`。np2 実 include に加え seamless 経路の mode=0/4 OUT/コマンド前順を同 fixture で固定。変更関数の直接字面依存は未検出、既存 M 維持 | 要: seamless 有効/無効・G10 の数値表示 |

**F11 (U2-02、ユーザー決定 2026-10-29)** — utf8_pack32・utf8_cmp32 と、その専用の定義 utf8_bswap32・utf8_eq32・UTF8_PACKED2_MASK・UTF8_PACKED3_MASK を除去。**utf8_char_at は残す** (アプリ開発者向けの UTF-8 文字列の補助として SDK に置く、ユーザーと PM の判断)。触るファイル: `lib/utf8.c:64・:93 (271)`・`lib/utf8.h:44-45・:88-104 (132)`。実装者 terra 可。削減 約 30 行 [推]。危険 中 (SDK 公開ヘッダ・libos32gfx.a、apps/・game/ に利用なし — Opus 確認、H-7 の再開時に再ビルド)。試験: lib/utf8.[ch] を組む・字面で読む `test_gfx_attach.py`・`test_dep_dir_d_console.py`・`test_unicode_surface.py`・`test_unicode_lookup.py`・`test_x11_shared.py`・`con_sink_host.c`・`kapi_audit_host.c`・`gui_text_host.rs` を回す (関数名の直接参照は無い — PM が grep で確認)。SDK のヘッダの差分は sdk/README.md の記載と合わせる。束: F09 で文字表示。
U2-08 の `init/kernel.c:533` の呼出しは **gui_take_next_shell**（消費する入口）で、削除対象 gui_next_shell ではない。kernel.c は F03 の編集対象に加えない。U2-13 の atapi_test_unit_ready は試験が使うので削除対象から外す。

## 4. 試験・ビルド追従の範囲

次は表から参照する編集候補。必要な追従を 1 段のソース変更と同じ依頼に入れる。未変更の試験を無用に書き換えない。名前の省略部分は同じディレクトリを指す。

| 群 | 追従候補と現行行数 |
|---|---|
| T-FD | `tools/tests/test_vfs_fd_path.py (1086)`、`tools/tests/vfs_fd_path_host.c (2228)`。文字列ヘルパの shim/宣言、実ソース include、変異の置換先を確認 |
| T-M1 | `tools/tests/test_module_runtime.py (133)`・`test_pcm_module.py (156)`・`test_fm_module.py (111)` |
| T-M2 | `tools/tests/test_kcg_module.py (270)`・`test_fep_boundary.py (106)`・`test_cirrus_module.py (195)` |
| T-M3 | `tools/tests/test_v86_module.py (114)`・`test_lan_module.py (156)`・`test_sqlite_module_proto.py (672)` |
| T-K | `tools/tests/test_kapi_ranges.py (99)`・`test_kapi_bounds.py (67)`・`test_net_link.py (162)`・`test_dep_dir_a_l09.py (110)` |
| T-LEASE | `tools/tests/test_lease.py (103)`・`test_surface_lease.py (181)`・`test_gfx_attach.py (154)` |
| T-LE | `tools/tests/test_module_image.py (291)`・`test_bootinfo.py (232)`・`test_module_runtime.py (133)`・`test_vk32_crc.py (917)`・`test_net_link.py (162)`・`test_gfinit.py (198)`・`test_gfinit_pll.py (76)`・`test_h3b_fixtures.py (195)` |
| T-BUILD | `build/kernel.mk (366)`、`build/kdebug.mk (88)`、`build/v86-module.mk (33)`。各 S 行で該当する 1 系統のソース/オブジェクト登録だけ追従 |
| T-P2V | `tools/check_p2v_allow.txt (73)`。`file:function:reason`（`:1`）を移動先に対応付け、理由と例外の意味を維持 |

対応する `tools/check_map.d/<check名>.yaml` と `build/checks.d/<check名>.mk` も、新しい入力を登録する必要がある段だけ編集対象へ含める。既存登録の行数と実際の変更要否は依頼パックで確定する（現時点で全件編集は予定しない）。新規 D04 は `tools/check_map.d/check-ne2000-dma-host.yaml (0)`・`build/checks.d/check-ne2000-dma-host.mk (0)` を指す。
**1,000 行の判定は試験にも適用する**。例: T-FD、`tools/tests/test_h3_park_resume.py (1724)`、`test_serialfs.py (1775)`、`test_hdd_stage1.py (1044)` に触る依頼は sol か astra。terra 可の段で追従が発生したら割当てを直してから出す。
パス依存の調査規模は `VERIFY.md:7` の exec.c 97 本・paging.c 61・gfx_core.c 35・appslot.c 28・ime.c 13・fdc.c 9 を出発点にする。これらは裏取り時の探索数で、今回の検索では綴り/対象拡張子によって数が異なり、編集本数とは断定しない。
各 S 段の依頼前に `rg -n` でパス・basename・関数名を `tools/tests/`、対応表、ビルド定義から列挙し、実際に直す**各ファイルの行数**を付けてパックへ固定する。列挙が終わるまでは実装依頼を出さない。追従はソース移動と同段で完結させる。
`tools/tests/test_vfs_fd_path.py:492`・`:911` のように文字列一致数を確認する試験がある。include 先だけ替えても変異が元のパスを指せば不十分。変更後の実ソースと同じ試験経路で当たることを確認する。
T-P2V の「6 行」は現基点では **exec/exec.c の 6 エントリ**（`:32`・`:33`・`:36`・`:37`・`:40`・`:41`）。VERIFY の exec/paging/recover という説明を固定の配分とせず、paging/recover も移動関数名で再検索する。V86・kselftest・kernel_main の既存エントリも移動時に追従する（同 `:2`〜`:12`・`:25`）。


### r3 の検索と追従候補（T-R3）

既存表の各行と REPORT2 の全件で、`rg -n/-l -F/-w` により **関数名とファイル名の両方**を `tools/tests/` から検索した。以下は追加段の実ソース include・字面依存を具体化したもの。試験名だけの一致や同名 stub は本番の呼出しと数えない。追従候補は各行の編集許可範囲で、不要な変更はしない。ここにない利用者も着手時に再列挙する。

| 群 | 追従候補と現行行数・検索で確認した依存 |
|---|---|
| T-R3-P | `tools/tests/test_gfx_kernel_fb.py (217)`・`gfx_kernel_fb_host.c (897)`、`test_gfx_attach.py (154)`・`test_unicode_surface.py (135)`、`test_boot_splash_native.py (45)`・`boot_splash_native_host.c (293)`、`test_gfx_bounds.py (55)`・`test_kapi_bounds.py (67)`。backend/vram の実ソースを組む。gfx_bounds_cases.inc の矩形ケース、gfx_add_dirty_rect の字面 M (`test_kapi_bounds.py`) の意味と一意性を維持 |
| T-R3-ASM | `tools/tests/test_ledger.py:464・:484・:495 (713)`・`test_fdc_track.py:211・:214 (603)`・`test_e10c_lifecycle.py (85)`。isr_stub.asm の字面の復帰点/マクロ/入口順を読む。G09 の新 .inc を入力/依存に加え、入口の検査と M を弱めない。F02 は未使用マクロだけで入口名を変えない |
| T-R3-GUI | `tools/tests/test_ring3_guard.py (250)`・`ring3_guard_host.c:142 (602)` は gui.c を実 include。`test_font_source.py:164 (261)` が gui.h の宣言を抽出、`test_d2_services.py (275)` も参照。weak の代替が要る単体リンクと強い製品実装を確認。`test_kapi_bounds.py` の gfx_* stub は別物で削らない |
| T-R3-MOUSE | `tools/tests/test_kbd_dlog.py (224)`・`kbd_dlog_host.c:38 (568)` が mouse.c、`test_pegc_mode.py (354)`・`pegc_mode_host.c:47 (1549)` が np2sysp.c を実 include。`test_d2_services.py (275)` の mouse.h 配線抽出と pointer_fixture_host.h も参照。同小ヘッダは `tools/tests/pointer_fixture_host.h (25)`。G11 は fixture に実 mouse_seamless.c の include と送受信モデルを足す。kbd の stub_mouse_buttons は mouse_read_buttons 用で消さない |
| T-R3-STATE | `tools/tests/test_d2_services.py (275)` の RTC ヘッダ、`test_shlib_high.py (74)` の shlib 実ソース、`test_double_fault.py (173)`・`double_fault_host.c:4 (86)` の実 tss.c include と `test_exec_r1.py (577)` の TSS ヘッダ構成を確認。削除名を直接読む試験は未検出、shlib/TSS の検証・M を残す。別 fixture の同名 g_version は対象外 |
| T-R3-PCI | `tools/tests/test_pci_bind.py (153)`・`pci_bind_host.c:14 (477)`、`test_device_reservation.py (804)`（`:582` の pci.c 抽出）、`test_pci_decode.py (208)`、`test_e82557_drv.py (460)`・`test_lan_module.py (156)`。pci_bind_match.c の実 include と M を追従。lspci_cmd_host.c の g_pci_present は別の試験状態で残す |
| T-R3-CRC | `tools/tests/test_hsync_h1.py (138)`・`hsync_h1_host.c (1853)`、`test_lanfs.py (567)`・`test_serialfs.py (1775)`。実 crc32_core.inc と calc 経路の include/字面 M を確認。stream 3 関数の試験参照は未検出。core を動かさず、これらの期待 CRC と既存 M は維持 |
| T-R3-CD | `tools/tests/test_cd_read.py (705)`・`cd_read_host.c (2440)`。`:76・:77` で atapi.c/iso9660.c を実 include。ソース置換の M と媒体世代・callback 前後を保持。atapi_test_unit_ready の `:2132` を追従削除しない |
| T-R3-CON | `tools/tests/test_con_sink.py (61)`・`con_sink_host.c (913)` と `test_dep_dir_d_console.py (227)`・`dep_dir_d_console_grid_host.c (119)` が実 console.c、`test_bootlog.py (291)`・`test_ime_display.py (53)` も結線/実ソースを組む。sink の再入・順序と M を維持。文字列出力の stub 群は共用対象外 |
| T-R3-EXT | T-FD に加え `tools/tests/test_ext2_write_io.py (59)`・`ext2_write_io_host.c (818)`、`test_b8_open.py (286)`・`b8_open_host.c (5411)`、`test_ext2_empty_name.py (259)`・`test_hdd_stage1.py (1044)`。ext2_file/inode/vfs を実 include する各 fixture とソース置換を確認。G02a/b・G06・H02 の変更経路に当たる M のみ同時追従 |
| T-R3-FAT | T-FD と `tools/tests/test_fatfs_stat.py (57)`・`fatfs_stat_host.c:200 (602)` の実 include。`test_fdc_track.py:198 (603)` の read_stream 本体抽出、`test_bootlog.py:221 (291)` の write 本体抽出・戻り値/close 検査が必須追従。fatfold の表抽出 (`test_vfs_fd_path.py:548`) は別部で維持 |

G02a/G08/F10/G11 の補強も既存 fixture に不足する対象契約だけを足す。新しい変異の登録が必要なら同名 check の YAML/mk・check-ci-stab-host に追従し、各登録の実行時行数をパックへ固定する。G09 は新 .inc の NASM 依存も build/kernel.mk で保持する。1,000 行を超える追従候補がある F04/F07/F08/G02b/G04/G05/G06/G11/H01/H02 は sol か astra とした。

## 5. 長関数の抽出と、大きい 14 ファイルの分割

### 5.1 抽出（E 群、別々の依頼）

抽出は元ファイルの中で関数境界を作る段。S 群のファイル移動と同じ差分に混ぜない。機械語の比較基点は E 群受入後に取り直す。新しい処理・検査を足さず、既存の順と返値を保つ。

| 段 | 件 ID / 1 回で抽出するもの | 触るファイル・現行行数 | 実装者の候補 | 純減の推測 / 危険 | 直接試験と変異の目印への影響 | ゲスト |
|---|---|---|---|---|---|---|
| E01a → E01b | R01、a=image 転送、b=argv 構築 | `exec/exec.c:1573 (2534)`、T-P2V、関係する実ソース試験は §4 で確定 | sol か astra | 各 0 / 高 | 各段 `test_exec_r1.py`・`test_exec_selftest.py`・`test_h3_park_resume.py`（check-h3-park-resume-host）。setjmp フレーム/abort 順と M を維持 | 各段要、アプリ起動/終了・raw_wait |
| E02a → E02b | R02、a=起動段の補助、b=shell 復旧 | `init/kernel.c:110 (670)`、T-P2V、`tools/tests/test_kdebug_init.py (271)`・`test_memory_boot.py (363)`・`test_bootlog.py (291)`・`test_dep_dir_d3_boot.py (107)`・`test_cd_read.py (705)`・`test_dma_pool.py (234)`・`test_stack_entry.py (119)` | terra 可（超過試験への追従時は変更） | 各 0 / 高 | 同 7 試験。追加 3 本も kernel.c を文字で読む: `test_cd_read.py:543` は変異の正規表現、`:569・:574` は kernel_mount_children の呼出し/本体を index で探索。`test_dma_pool.py:136` は static inline void kernel_mount_children(...) を正規表現で抽出。`test_stack_entry.py:50・:106` は kernel.c 読取と kernel_main の書換え。文字位置による抽出を同段で追従、M の移設 | 各段要、起動順/MINIMAL・shell 復旧 |
| E03a | R03、format の領域初期化 | `fs/ext2_fmt.c:152 (428)`、`tools/tests/test_hdd_stage1.py (1044)` | sol か astra | 0 / 高 | `test_hdd_stage1.py`。配置/書込順の M を追従 | 要: ext2 format/mount |
| E03b | R03、add_entry の既存/新規ブロック処理 | `fs/ext2_dir.c:173 (1228)`、T-FD、`tools/tests/test_ext2_write_io.py (59)` | sol か astra | 0 / 高 | `test_ext2_write_io.py`・`test_ext2_empty_name.py`・`test_vfs_fd_path.py`。失敗/公開順の M 追従 | 要: ext2 作成/削除 |
| E04 | R04、serial_init_ex の適用と報告 | `drivers/char/serial.c:221 (951)`、`tools/tests/test_serial_portc.py (218)`・`test_serial_vfast.py (249)` | terra 可（test_serialfs.py の編集が必要なら sol に替える） | 0 / 高 | 同 2 試験と `test_serialfs.py (1775)` を回す。後者も serial.c を組み `:1519〜:1543` で変異を当てるが、目印は serial_init_ex（`drivers/char/serial.c:221〜:431`）の外なので編集不要の見込み（推測）。IF=0 区間と早期 return、既存 M 維持 | 要: シリアル再設定・通信 |
| E05a | R05、FDD 転送計画と実転送 | `kernel/v86/v86_bios.c:505 (1277)`、T-P2V、`tools/tests/test_v86_low.py (129)` | sol か astra | 0 / 高 | `test_v86_low.py`・`test_v86_module.py`。CF/AH・verify の無転送、M 追従 | 要: V86 の FDD/D88/raw |
| E05b | R05、GP の命令別処理 | `kernel/v86/v86.c:195 (963)`、T-P2V、`tools/tests/test_v86_gcap.py (304)`・`test_v86_gcap_abort.py (93)` | terra 可 | 0 / 高 | 同 2 試験。命令長/IP/戻り値と M 追従 | 要: V86 実行と復帰 |
| E06a → E06b | R06、a=PCI 準備、b=NIC 起動/既存回収への分岐 | `drivers/net/e82557/e82557.c:654 (880)`、`tools/tests/test_e82557_drv.py (460)` | terra 可 | 各 0 / 高 | `test_e82557_drv.py`。取得済み資源だけを逆順で戻す M を維持/移設 | 各段要: 82557 の初期化・送受信 |
| E07a → E07b | R07、a=idle read、b=計画アクセス | `kdebug/recover.c:1966 (2413)`、`tools/tests/test_recover.py (211)`・`test_recover_tables.py (171)`、T-P2V | sol か astra | 各 0 / 高 | 同 2 試験。既存の拒否/ログ/復元順と M を追従 | 各段要: 既存の仮想 recover 受入 |
| E08 | R08、時計試験を条件ごとの補助へ | `kdebug/kselftest.c:2093 (2495)`、`tools/tests/test_kdebug_init.py (271)`・`test_memory_boot.py (363)` | sol か astra | 0 / 中 | 同 2 試験。ケース数、fixture の初期化/復元と M を維持 | 要: 起動自己試験の全条件 |

### 5.2 機械的分割（S 群）

下表は 14 本を漏れなく扱う。**a/b/... はそれぞれ独立した段 ID・1 回の依頼**で、左から 1 群ずつ切り出す。最後の「残」は元の .c に残す群で独立の実装依頼ではない。各段は 1 つの元ファイルだけを分割し、登録・内部宣言・試験追従までで閉じる。
各群の移動先は同ディレクトリの `<元名>_<群名>.c`（すべて現行 0 行の新規候補、名称は推測）。追加する `<元名>_private.h` も現行 0 行。既存の同名ファイルがあれば転用せず、依頼前に名称を確定する。各出力は約 500 行を目安とし、見込みは REPORT `:97`〜`:110` に基づく推測。
全 S 段は **sol か astra**、純減 **0（移動）**。共通編集対象は表の元ファイル、当該新規 .c/private.h、T-BUILD の該当定義、移動関数に関係する T-P2V と §4 で確定した試験/対応表。表に挙げる試験は最低限で、参照ソースを組む利用者の追従も必要。

| 段 ID / 元ファイル（現行行数） | 各依頼の移動群 → 残す群（各 .c の行数は推測） / 根拠の入口 | 危険 | 直接試験 / 変異の目印への影響 | ゲスト受入 |
|---|---|---|---|---|
| S01a/b/c/d/e/f、`exec/exec.c (2534)` | a=tramp 250、b=image 350、c=launch 450、d=finish 450、e=syscall 350、f=schedule 450 → 残 context 400。`:147`・`:828`・`:1573`・`:937`・`:1379`・`:2174`。E01a/b 後 | 高 | 毎段 `test_exec_r1.py`・`test_exec_selftest.py`・`test_h3_park_resume.py`（check-h3-park-resume-host）。逆アセンブル観測点と M のパスを追従 | 毎段要: 起動/終了・park/resume・raw_wait、次を積む前 |
| S02a/b/c/d/e/f/g、`kdebug/kselftest.c (2495)` | a=basic 350、b=memory 450、c=backing 350、d=dma 300、e=irq 350、f=clock_boot 400、g=guard 400 → 残 run 150。`:94`・`:351`・`:1064`・`:1612`・`:1778`・`:2093`・`:2291`・`:2443`。E08 後 | 中 | `test_kdebug_init.py`・`test_memory_boot.py`・該当 memory/DMA/IRQ host。runner の順・ケース数と M を維持 | 毎段要: 起動自己試験・部品起動 |
| S03a/b/c/d/e、`kdebug/recover.c (2413)` | a=profile 450、b=transport 500、c=trial 500、d=plan 420、e=access 380 → 残 mode 320。`:62`・`:241`・`:1112`・`:1434`・`:1808`・`:2146`。E07a/b 後 | 高 | `test_recover.py`・`test_recover_tables.py`・`test_recover_virtual.py`。M/名前検索/部品リンク先追従 | 毎段要: 仮想 recover・既存復帰 |
| S04a/b/c/d、`mm/paging.c (1696)` | a=boot 500、b=map 420、c=addrspace 320、d=lease 230 → 残 selftest_audit 400。`:225`・`:492`・`:875`・`:1435`・`:1150`。C07 後 | 高 | `test_paging_bounds.py`・`test_lease.py`・`test_surface_lease.py`・`test_memory_boot.py`。M と P2V の所属を追従 | 毎段要: paging/lease・mem、次を積む前 |
| S05a/b/c、`drivers/block/fdc.c (1345)` | a=io 250、b=seek_reset 350、c=transfer 450 → 残 drive 400。`:217`・`:459`・`:768`・`:1143`。DMA バッファ/統計の実体は 1 所有者 | 高 | `test_fdc_seek.py`・`test_fdc_track.py`。NR/NW の M は内容を変えず移動先へ | 毎段要: FDD 読み書き・回復 |
| S06a/b/c、`kernel/v86/v86_bios.c (1277)` | a=fdd_read 400、b=fdd_command 400、c=sasi 250 → 残 context 300。`:505`・`:719`・`:1046`・`:173`。E05a 後、既存 FDD 状態を共有 | 高 | `test_v86_low.py`・`test_v86_module.py`、`build/v86-module.mk (33)`。M/P2V/部品の組み方を追従 | 毎段要: V86 FDD/SASI と復帰 |
| S07a/b、`gfx/gfx_core.c (1251)` | a=surface 450、b=api_diag 450 → 残 lifecycle 450。`:341`・`:701`・`:1014`。backend/世代状態の実体は維持 | 高 | `test_gfx_attach.py`・`test_gfx_reinit.py`・`test_gfx_kernel_fb.py`。世代/200 行の M を移設 | 毎段要: CUI/GUI・400/200 行・再初期化 |
| S08a/b、`fs/ext2_dir.c (1228)` | a=directory 420、b=rename 440 → 残 entry 490。`:403`・`:809`・`:173`。E03b 後、`:911` publish は rename 側 | 高 | `test_ext2_write_io.py`・`test_ext2_empty_name.py`・`test_vfs_fd_path.py`。公開/回収順の M を移設 | 毎段要: ext2 mkdir/rmdir/rename |
| S09a/b/c、`drivers/video/pc98/backend_pegc.c (1215)` | a=timing 480、b=diag 200、c=present 300 → 残 lifecycle 400。`:199`・`:801`・`:656`・`:564`。shutdown/restore の既存順を保持 | 高 | `test_pegc_mode.py`・`test_gfx_kernel_fb.py`・`test_gfx_boot.py`。同期/復元の M を移設 | 毎段要: PEGC 表示・復元、実機の表示条件は該当関門へ |
| S10a/b、`drivers/block/loop_dev.c (1177)` | a=format 400、b=d88_io 400 → 残 core 450。`:313`・`:216`・`:825`。LoopSlot は private、実体は core | 高 | `test_h3b_fixtures.py`・`test_v86_low.py`。D88/LBA の M と実ソース経路を追従 | 毎段要: loop attach/read/write |
| S11a/b/c、`kernel/ime/ime.c (1159)` | a=render 280、b=convert 460、c=dict 190 → 残 input 350。`:57`・`:338`・`:643`・`:757`。owner/commit buffer を維持 | 高 | `test_ime_display.py`・`test_fep_boundary.py`・`test_vfs_fd_path.py` の ime。M/入力 shim 追従 | 毎段要: IMECAND/IMEIND/IMEDUP/GUIRUNKEY |
| S12a/b、`exec/appslot.c (1136)` | a=transition 420、b=selftest 300 → 残 core 500。`:312`・`:882`・`:115`。slot 表の実体は core | 高 | `test_h3_park_resume.py`・`test_exec_selftest.py`・`test_r1_fixture.py`。M と所有者/park の観測を追従 | 毎段要: park/resume・終了、次を積む前 |
| S13a/b/c、`mm/pgalloc.c (1098)` | a=owner 400、b=resource 200、c=surface 220 → 残 core 380。`:384`・`:739`・`:885`・`:74`。arena/freeze の所有者は維持 | 高 | `test_pgalloc_range.py`・`test_pgalloc_model.py`・`test_surface_lease.py`。台帳の M/fixture 追従 | 毎段要: mem/台帳・surface、次を積む前 |
| S14a/b、`drivers/net/ne2000/ne2000.c (1057)` | a=control 300、b=data 380 → 残 hw 480。`:477`・`:637`・`:232`。nic 状態/IMR の既存排他を維持、D04 後 | 高 | 新規候補 `test_ne2000_dma.py`・`test_tick.py`・`ne2000_ring_test.c`。M と実ソース/IRQ 経路を追従 | 毎段要: NE2000 の RX/TX・再初期化 |

各 S 段は切出し先と残部が共にビルド/試験でき、状態の実体が二重化しないところまでを受入単位とする。依頼の条件が 10 項を超えれば、関数群をさらに小さくし、この表の ID に枝番を付ける（ROLES `:19`）。全ファイルの分割完了まで受入を先送りしない。
`init/kernel.c:105` は補助本体を kernel_main の後に置く理由が試験の位置抽出であると明記する。E02 では前方宣言/本体位置・抽出境界・依存宣言を試験と同時に直す。固定した期待順やケースを減らして通さない。
**機械語の門:** 各 S 段の直前/直後を同じコンパイラ・フラグ・構成で組み、`objdump -dr` の関数別結果を対応表で比較する。配置アドレスと再配置先を対応付け、命令列・分岐・呼出し・データ参照の同一性を確認する（POLICY_DEV `:131`）。古い build/out は基点に使わない。
static を別の翻訳単位へ移すと、インライン化や定数伝播が変わり一致しなくなる見込みは**推測**（`VERIFY.md:7`）。不一致なら呼出し元と helper を同じ群に残すなど境界を修正し、同じ段で再確認する。
それでも一致できなければ分割の完了とはせず、差が出た関数・objdump・境界案を PM へ戻す。比較条件を緩めたり、命令の増減をアドレス差として隠したりしない。抽出/共通化による機械語の変化は E/L 群の別差分で判断する。

## 6. 段ごとの完了条件の型

以下は将来の実装依頼の型であり、今回実行する手順ではない（ROLES `:17`〜`:23`・`:62`〜`:69`）。

1. PM は基点 SHA・モデルと理由・許可するファイル/現行行数・関数と呼出し元・保つ挙動・直接試験・受入条件 10 項以内を依頼パックに固定する。環境は CROSS_DIR、ディスク上の TMPDIR、HOST32_RUNNERS=qemu を指定し、実装時に preflight を通す。
2. 実装者は変更前に報告の枠を先に書く。先頭は実基点とモデル ID、続けて変更範囲・各完了条件と証拠の置場・未確認/持越し。検査結果欄は実行するまで未実施とする。票への追記は 1 段 10 行以内、詳細は証拠の `RESULT.md`。
3. 各段の検査は列挙を固定した**台本 1 本**にまとめ、コマンド・runner・開始/終了・rc を記録する。既存の台本を使い、足りない受入観測の台本だけ `tools/accept/` に置く（ROLES `:21`）。試験は `check_slot.sh` 経由、同時に 2 本まで。検査を途中で止めて最初からやり直さない。
4. 本票の連続段は Tx として、各段 `make kernel`（対の部品も）＋足した/触った直接試験 `MUTATE=0`＋`python3 tools/check_select.py --lint` の rc=0。最後の変更後のログだけを合格証拠にする。独立に閉じる依頼は `make all`。Tx 最後の段の後、レビュー前に `make all`＋全差分の候補 `MUTATE=0`＋静的 5 本を 1 回。
5. **M の門:** 既存変異ごとに対象パス・関数・文字列/正規表現の目印を対応付け、変更後も 1 か所だけに当たることを確認する。0/複数一致、count=1 やファイル順による先頭だけの置換は不合格。意味が同じ新しい位置へ目印を移し、正常ケース/期待値/条件数は維持する。
6. 独立レビューは terra/sol の実装を astra、astra の実装を別モデルの Opus が読む。レビュー後、PM が選択された変異を 1 回確認する。変異はビルド成功後の実行時 RED、原本は GREEN を証拠にし、コンパイル失敗を検出成功としない。新規変異を持つ試験は check-ci-stab-host も列挙する。
7. PM の取り込みは `make all`＋`make -k check-changed BASE=<前回の全体検査>`、節目の全体は専用作業木で `make all`＋`make -k check` を 1 回。静的 5 本は check-p2v / check-c-dialect / check-constraints / check-arch-asm / check-privileged。失敗後は直した所と落ちた検査だけ再確認する。
8. カーネル層の受入は PM が実施する。最小回帰は ver の Commit/Image CRC/PAIR・起動自己試験/SERVICES_READY・mem の失敗/残留/監査・db_test/db_v50_test。表の段固有項目を加え、exec/STOP は raw_wait、入力/FEP は入力の約束、起動/部品は MINIMAL（ROLES `:86`）。heap/exec/paging の高危険段はゲストを通す前に次を積まない。
9. host だけ、SKIP、環境不足、未結線は完了にしない。延期は `docs/tasks/DEFERRED_TESTS.md` の 1 か所へ理由と必ず回収する関門を書く。実機の構成差（NE2000/82557、PCM/FM、PEGC/Cirrus 等）は該当する既存の関門にまとめる。受入結果は票に合否と証拠パスだけ残す。

## 7. 最後の群（VERIFY / VERIFY2 が後回しとした件）

S 群が受け入れられた後に L 群 → H01 → H02 を実施する。H01/H02 は削減が小さく、VERIFY2 が優先を下げた件。ここでの元ファイル行数は現基点の値で、実装時には S 群の移動先と行数に置き換えて依頼する。**抽出/共通化と S 群を混ぜない**。割当ては元の状態共有の大きさも考慮し sol か astra を候補とする。

| 段 | 件 ID / 中身 | 触る現基点のファイルと行数（S 後は移動先） | 実装者 | 削減の推測 / 危険 | 直接試験・変異の目印への影響 | ゲスト |
|---|---|---|---|---|---|---|
| L01a → L01b | D01、a=kbd/yield の同一本体、b=park の共通資格/更新列 | `exec/appslot.c:312 (1136)`、`tools/tests/test_h3_park_resume.py (1724)`、関係する slot 試験は §4 で確定 | sol か astra | 合計 60〜90 / 高 | 各段 `test_h3_park_resume.py`・`test_exec_selftest.py`。OP_WAIT/poll 間引き/由来フラグ/カウンタの M を一意に維持 | 各段要: park/resume・raw_wait |
| L02a → L02b | D06、a=コマンド送出の同一部分、b=転送骨格の残り | `drivers/block/fdc.c:768 (1345)`、`tools/tests/test_fdc_track.py (603)`・`test_fdc_seek.py (137)` | sol か astra | 合計 50〜75 / 高 | 各段同 2 試験。NR/NW の文字列/正規表現と移動先を同時追従（test_fdc_track.py:400〜409） | 各段要: FDD 読/書・媒体無し/書込保護・回復 |
| L03 | D07、400/200 行初期化の共通部 | `gfx/gfx_core.c:1014 (1251)`、`tools/tests/test_gfx_reinit.py (79)` | sol か astra | 25〜40 / 高 | `test_gfx_reinit.py`・`test_gfx_boot.py`・`test_gfx_kernel_fb.py`。geometry/clear-range の M を維持/移設 | 要: 400/200 行・packed native・再初期化 |
| L04 | R09、候補確定後の同一更新列 | `kernel/ime/ime.c:338 (1159)`、T-FD、`tools/tests/test_ime_display.py (53)` | sol か astra | 15〜25 / 高 | `test_ime_display.py`・`test_vfs_fd_path.py` の ime。通常キーの追送/返値と M を維持 | 要: 候補確定・次の入力先 |
| L05a → L05b | D10、a=確定バイト取得、b=キー 1 件の変換だけ共用 | `kernel/ime/ime.c:757 (1159)`、T-FD、`tools/tests/test_ime_display.py (53)`・`test_fep_boundary.py (106)` | sol か astra | 合計 55〜85 / 高 | 各段同試験。待機/非待機・ASCII/scan code・services_public・入力 owner の差と M を維持 | 各段要: IMECAND/IMEIND/IMEDUP/GUIRUNKEY |
| H01 | D2-11、ISO レコード位置計算だけ共用 | `fs/iso9660.c:290・:543 (743)`、T-R3-CD | sol か astra（fixture 超過） | 4〜6 / 高 | `test_cd_read.py`・`test_hsync_h3.py`・`test_vfs_fd_path.py`。実 iso include とソース置換 M を追従。continue は各入口に残し、検索 cache/list のコピー・callback 前後の世代検査を保持 | 要: CD 検索/一覧・媒体交換 |
| H02 | D2-12、ext2 unlink/rmdir の親解決〜最終操作だけ共用 | `fs/ext2_vfs.c:173・:263 (528)`、T-R3-EXT | sol か astra（fixture 超過） | 数行（2〜4 [推]）/ 高 | `test_ext2_write_io.py`・`test_b8_open.py`・`test_ext2_empty_name.py`・`test_vfs_fd_path.py`。型付き最終操作・薄い VfsOps 入口、字面の callback 検査 (`tools/tests/test_kcallback.py (131)`) と実 include/M を維持。mkdir/create_excl/rename は巻き込まない | 要: ext2 unlink/rmdir・失敗時 |

D06 の差は契約として残す: read だけが single_reads/single_retries を数える（`drivers/block/fdc.c:780`・`:790`）、write は事前コピー/世代更新、read は成功時コピー。write の NW を NR より先に判定し（`:946`・`:956`）、DMA を閉じ、FDC_RC_WRITE_PROTECTED を返す（`:968`）。NR の回復前打切りも維持する。
この差を保ったまま共通化できる範囲だけ採る。既存の READ/WRITE 固有コメントを使う変異は、共通化後も方向ごとの振る舞いを壊して実行時 RED にできる位置へ移す（`tools/tests/test_fdc_track.py:397`〜`:411`）。重複が減るだけでは受入にしない。

## 8. 未確認と持越し

- **nm・kernel.map の主張は今回照合していない**。元調査の成果物は HEAD より古い（`REPORT.md:17`〜`:18`、`VERIFY.md:2`）。現ソースの参照検索を主根拠とし、U06〜U09 の削除時にはその段の新しい成果物で参照を確かめる。ツリー外の直接リンク利用者は未確認（REPORT `:117`）。
- `.inc` の追加調査 (REPORT3、手書き 19 本・未読 0) の新規 2 件は**見送り**: D3-01 (vfs_name_rules.inc と kutf16.c の UTF-8 の受理規則、5〜12 行で FS の名前の同一性に触れ危険が高い)・D3-02 (gfinit_budget.inc と recover.c の layout の探索、0〜4 行で recover 側は試験の計数を挟む)。PM が実物で同じ形を確かめたうえで、効果に対して危険が大きいと判断 (2026-10-29)。
- 削減行数はすべて見積もり。r3 表の既存 366〜517 行＋REPORT2 修正後 333〜435 行＝699〜952 行（重複 403〜610、未使用 235〜253、形 61〜89）。REPORT の約 370〜520、REPORT2 の 399〜526 は原見積もりで、r3 は U2-02 を F11 (合計に未算入)・U2-13/D2-08/11/12 の訂正を反映。分割/抽出は 0、D11 は C05 で再見積もり。
- U05 の宣言残存による GCC -Wunused-function は**推測**で未再現（VERIFY `:5`）。宣言・実体・shim 3 か所を一つの段で扱い、警告抑止を足して通す案にはしない。
- 試験のパス依存の全追従先、分割後の正確な行数、static 移動後の機械語一致、ビルド/ゲスト/速度の前後差は未確認。票は計画であり、実装パックと前後の証拠で確定する。
- `lib/path.c` の未使用除去に直接対応する既存 host 試験は今回の検索で見つからなかった。A03 のパス操作のゲスト受入を B03 で回収し、別の path 実装を試す試験を代用として合格に数えない。
- 分割対象は POLICY_DEV の一覧から userland/system/hsync.c を除く 14 本だけ。現時点で別に 1,000 行超の `kapi/kapi_db.c`・`net/link.c`・`boot/loader_fat_new.asm` を分割対象に追加しない（REPORT `:112`）。D04 の LE 共用は既出候補の範囲だけ。
- 未使用でもレジスタの説明のために残す定義、C/ASM の代替実装、既存 CRC 共用、返値/切詰め契約が異なる整形処理は本票の整理対象に追加しない（REPORT `:118`〜`:123`）。
- REPORT2 の古い成果物の不一致も未解消（build_id は 24b00c7）。U2 系の現ソース参照は再検索したが、現 HEAD の ELF/nm・SDK のツリー外利用者・全構成のリンクは未確認。U2-02 はユーザー決定 (2026-10-29、char_at は残す)。

この作成依頼の成果物はこの r3 票・別調査 `REPORT3.md`・修正一覧 `TASK_r3_response.md`。実装・ビルド・試験・ゲスト/実機受入は未実施。
