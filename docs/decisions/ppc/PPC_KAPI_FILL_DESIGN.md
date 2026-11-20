# PowerMac の KAPI の提供者を埋める計画 (確定)

> 2026-11-20 の見直しで x86 の形に改めた ([DESIGN_X86_AUDIT](DESIGN_X86_AUDIT.md))。

表示の復帰と受入は [表示フックの確定設計](DISPLAY_HOOKS_DESIGN.md) を参照。

> 出所: ~/os32-tmp/run/ppc-kapi-fill/DESIGN.r4.md (設計の往復の確定版)。
> 状態: **現行 (2026-11-18)** — 確定。astra 起草 r1〜r4、Opus 5.5 の独立レビュー 2 回、本線 PM の確認 (同日)。段の状態は票 TASK_PPC_KAPI_FILL が持つ。

ユーザー決定 (2026-11-18、原文): 「KAPI の提供者無し 190 本を埋める計画を、先に作ってよいか。先に。ただし、x86 と重複するような実装はだめです」。音について: 「そもそも FM 音源がない機種は HAL のソフトウェア FM 音源の部分でつなぐ計画では？」→ PowerMac は「新しい口だけ」(旧 FM 6 口は E、アプリは sndl_chip_* → AV HAL のソフトウェア FM 音源 → PCM)。PM 決定: kcg_load_font は両像とも NOSYS、buz は当面 E (AV HAL の後に判定し直す)。

ユーザー決定 (2026-11-20 夕): 段 6c の U1 を見直し、PowerMac の ext2 の作成 2 本 (`ext2_format`・`ext2_format_at`) を実装対象に戻す (G4 に ATA の HDD)。前提として段 8 の「ATA の block 契約」(本線) を PowerPC の ATA の装置の前に置く。受入は QEMU (mac99 の IDE) が先、G4 は後。

## 往復の変更の記録

r3→r4 の変更
- N1　PowerMac の旧 FM/SSG 6 口を E とし、SDK 互換口を削除。
- N2　PowerPC アプリはすべて再ビルドし、sndl_chip_* を使うと決定。未決 0 件。
- N3　両 arch の SOFT 経路を AV HAL の共通合成器から PCM へ接続。
- N4　§3 に音声段の依存順と PowerPC 担当の PCM 後端を記載。
- N5　合成器の照合を sound に移し、pcm は常駐窓口の定義元を照合。

r2→r3 の変更：PowerMac の FM/SSG を共通のソフト合成器から PCM 後端へ接続する。sound・pcm の分担と受入を更新し、FM/SSG の未決を解消。


## PowerMac の KAPI 提供者を結線する計画 r2

### r1→r2 の変更

- ID 1　固定名の転送を両像で選ぶ共通 TU に移す。
- ID 2　cursor・poll・bounds の共有を USB 入力より先に置く。
- ID 3　KCG 本体を共用し、load は両像で NOSYS とする。
- ID 4　PCI 走査・記録・bind 台帳を共有し、config 取引を後端にする。
- ID 5　KCG・FEP・SQLite の選択条件を個別に解く。
- ID 6　sys と gfx のその場の変更と純移動を別段にする。
- ID 7　公開表の試験、リンク順、covers の追従を完了条件にする。
- ID 8　各段で wrap の解決先のソースパスと新規定義名を照合する。
- ID 9　console のセル操作は公開表だけを変更する。
- ID 10　PCM 常駐窓口を特定し、分離を D に置く。
- ID 11　reset と RTC は確定設計の VIA を共用する。
- ID 12　buz は暫定 E とし、AV HAL 後に再判定する。
- 本線配置　先行段と待ち側の対応表を追加する。

基点 a76398a35。2026-11-18 の決定に従い、共通の機能は両像で同じソースを使う。以下は実装と受入の計画である。

### 1. 実物の確認

依頼の前提と基点との差は次の4件。数え方は独立した前提につき1件とした。

1. 組数は25。sdk/kapi_target_ppc.py のキーを数え、ABSENT_PROVIDER_SLOTS の和集合を sdk/kapi.json の api 名と照合すると、重複なしの190本、全298本中の残り108本となる。残りには予約・特殊経路を含み得る。
2. 空の wrap の戻り方は一様でない。sdk/gen_kapi.py と sdk/kapi_target_ppc.py により、void は無動作、ポインタは NULL、指定の10照会は 0、その他は戻り型へ変換した NOSYS。公開前からこれを利用するアプリがある前提で扱う。
3. 44ソースの定義位置は build/sources/common.mk。build/select.mk が選択をまとめ、build/targets/ppc-powermac.mk が未選択分もコンパイルする。コンパイル合格だけでは提供の証明にならない。
4. 指定の docs/decisions/ppc/PPC_SQLITE_STACK_DESIGN.md はこの基点の非アーカイブ木に無い。段 S は作業記録 ppc-sqlite-stack/DESIGN.final.md の旧スタック上の FP guard と別スタック呼出を参照。kapi/kapi_db.c のエンジン準備・所有者取得まで含めて別途受入する。

#### 統合判断（POLICY_DEV §1）

- 同じ役目は既にある。入力は TASK_INPUT_HAL（1p・1a・1b が main）、音は TASK_AV_HAL（実装待ち）、待ちは TASK_IO_TIER（A0・A1・A3・A4 済み）。票の状態は各ファイル3行目による。
- 分ける境界は装置イベント、レジスタ、OF 起動情報、CPU の保存復帰。文字列・権限・描画・SQL・資源回収を PowerMac へ持ち込まない。AC-KAPI-1 の型別欠如、AC-DISPLAY-1/2 の能力差を両像で同じ呼出試験に掛ける。
- 操作は各 arch の標準 OS に合わせ、Mac の入力は USB HID、表示は実際の画面寸法、起動元は OF とする。協調切替・整数演算を前提に AC-CTX-4/5、AC-IO-1 の予算・前景待機を守る。Host Services は既存 link/GMAC と mount を使う。
- TASK_PPC_SEP.md の #4 は提供者ごとの exec フック14本、#5 は契約の装置名、#6 は像への採用、#7 は公開名の互換性。#5 の内部名を整理しても #7 の既存 slot の意味と名前を保つ。
- build/platform/powermac.mk の CORE、LINK/NOLINK、EXEC/NOEXEC、USERSHELL、USB は別構成。root_boot/root_image の読取媒体、ramfs、hostfs の書込可否は実際に開いた mount に従う。

### 2. 棚卸し

A は共通本体を選択・初期化して通常 wrap に接続。B は既存契約に後端を登録。C は本線が機種非依存部分を移設。D は本線の契約を先に整える。E はその機種に意味を持たない能力として欠如を保持する。
各行の照合対象には §3 の段別完了条件（slot ごとの定義元パス一致・同名新規定義0）を適用する。表の「照合」は両像のリンク入力と定義元で突き合わせる対象。全行で tools/check_layers.py の両機種違反0と、arch/ppc・platform/powermac の差分に同じ処理が増えていないことを確認する。

|組|本数|x86 の実体・所有者|道・必要な後端・先行段|重複防止の照合対象|
|---|---:|---|---|---|
|sys|3|共通 kernel/sys.c → 機種 platform_reset / platform_buzzer_*|reboot B は 確定設計 PPC_RESET_DESIGN.md の PMU/Cuda 受入後。buz 2本は暫定 E、Mac のシステムビープは AV HAL 後に再判定|sys_reboot の呼出元・本体|
|surface（単独）|3|共通 exec/surface_query.c|A。OF FB の CLIENT を表示 ledger に登録し、surface_query・surface_lease・surface_unlease を公開|surface_api_query・surface_api_lease・surface_api_unlease|
|surface（束）|1|共通 exec/surface_query.c|D。本線の #18 の確定設計を待って gfx_surface_lease を公開|surface_api_bundle|
|gfx|22|装置 drivers/video/pc98/gfx_vram.c、共通 gfx/gfx_core.c と各 inc、gfx_hw_submit.c、kapi/kapi_osri.c|C は present 系と dirty の共通方針を先に移設。A は情報・所有者・共通描画。B は可視面・palette・raster・HW 能力。OF FB の実能力を検査。OSRI は VFS 後|gfx_core と OSRI デコーダ|
|clock|1|共通 kernel/time/clock_service.c → 装置 drivers/char/rtc.c|B。drivers/macintosh/ の reset と同じ via_command を使う rtc_readを暦時計に登録、単調時計とは別|clock_read_legacy と VIA インスタンス|
|console_font|9|共通 kernel/console/{console,font_source,unicode_file}.c|セル5本 A は既存 fbcon、字形 lease 3本と文字変換 A は KCG1・変換表の配備後|con_backend 経由・lease の所有者|
|port|9|共通 kernel/port_service.c、kapi/kapi_sys.c|B。PortOps に ESCC の送受信・状態、diag は共通の出力検証。vfast は当該速度・方式を提供できる範囲のみ|port_register と ESCC の単一所有|
|gui|3|共通 kernel/gui.c|A。GUI 実行主体・exec #4・表示所有権・shell 復帰を先行|gui_register/call と exec 終了フック|
|sound|16|装置 drivers/sound/fm/{fm,snd_engine,fm_module}.c|本線の AV HAL が旧 MML・snd・起動音の整理を AV0/R0a→R0b・R0c で担当。PowerMac の旧 FM/SSG 6 口は無し（E）。PC-98 は OPN 単体を含め従来の HW。PowerPC アプリはすべて再ビルドし、sndl_chip_* を使う。追記 2 の SOFT 経路は両 arch 共通で、L6 → L2 合成器 → L1 ミキサ → L3 → PCM 後端で鳴らす。|本線が AV_HAL_DESIGN.md §8 の照らし直し → lib/os32_opna（整数 fmgen）の段 A〜D → userland/lib/snd/fmvoice.c（L2）を担当。IS・L1/L2 後に L3、C3・L3 後に L6、IL で統合。PPC 専用の合成器・adapter は作らない。 両 arch のビルド入力を正規化し、コアと fmvoice.c のソースパス集合の一致、リンクマップ上の各定義元が共通ライブラリに一つだけあること、機種別コピーが無いことを照合。|
|platform|8|機種 platform/pc98/device_{host,loop}.c、装置 drivers/block/loop_dev.c|np2_* 5本 E。loop 3本は固定名転送の C→媒体契約 D→A、VFS と同じ仮想装置を選択、PC98 取次を共有口へ|loop_dev_attach とイメージ形式解析|
|ide|8|機種 platform/pc98/{device_ide,bootinfo_geom}.c → 装置 drivers/ata/ide.c|生 ATA 7本は固定名窓口を共有する D→B、実 ATA 媒体・権限が条件。BIOS 由来の hdd_geom_info は E。RAM/ISO を ATA として報告しない|共通 sector 範囲・書込権限|
|ext2|2|機種 kernel/dev/device_fs.c → 共通 fs/ext2/ext2_fmt.c|固定名転送の C→D→A。書込可能な block の契約から両 format を呼ぶ。loop 媒体を先行|ext2_fmt と ext2_layout|
|kcg|5|機種 platform/pc98/device_kcg.c、装置 drivers/video/kcg/{kcg,kcg_file}.c|C で固定名転送を共有し、A で kcg.c を両像に選ぶ。B は KCG1 の kcg_rom_read_ank・kcg_rom_read_kanji だけ。load は両像とも NOSYS|kcg_file_load と glyph 読取|
|pci|4|機種 platform/pc98/device_pci.c、共通 kapi/kapi_sys.c → drivers/pci/pci_bind.c|C で drivers/pci/pci.c の走査・記録と bind 台帳・固定名窓口を共有し、B は config 読み書きだけ。OF と pci_config の BDF、実際の bind 台帳を対応付ける|config 取引と bind 台帳|
|serialfs|2|機種 kernel/dev/device_fs.c → 共通 fs/serialfs/serialfs_session.c|固定名転送の C→A/B。既存 hostfs と同じ transport 所有・deadline 契約を満たしてから begin/end|serialfs_session と sfs_proto|
|path|5|共通 lib/path.c、exec/exec_tramp_strings.inc|A。#6 から選択、起動 mount の validator と cwd を初期化|path_parse と tramp_copy|
|db|17|共通 kapi/kapi_db.c と SQLite エンジン、CPU 別スタック入口|A+B。段 S → エンジン/VFS・共有結果・所有者回収 → 17本。SQL の PPC 再実装は不要|SQLite TU と同じ handle 表|
|system_info|5|共通 kapi/kapi_sys.c、kernel/ver_collect.c、kernel/time/ktime.c|build・mtime は A、time は B（TB）、mem/ver は A+B（共通集計と実測可能な機種欄）|kapi_sys_* と mem_stat の構造体|
|ime|17|共通 kernel/ime/、kernel/gui.c、機種描画部|入力・辞書は A、表示は B、ime_set_render は GUI 後。FEP のロード・辞書 VFS・共通入力を先行|同じ FEP facade、辞書形式|
|v86|5|機種 platform/pc98/legacy/{v86,v86_gcap}.c、arch/x86 の仮想86入口|E。VM86 と GDC の対象環境が無い|PPC 像に V86 定義を追加しない|
|pcm|5|装置 drivers/sound/cs4231/{pcm_module,pcm_cs4231}.c|D→B。本線が常駐 dispatch と PC98 tick の条件を分離し、TICK 段 B → AV HAL 音声段 → IS でストリーム契約を確定。PowerPC 担当は PowerMac 音声装置の PCM 後端だけを実装し、共通ミキサの出力を形式と空き量に従って転送する。|両 arch のリンクマップで pcm_open・pcm_write・pcm_status・pcm_close・pcm_set_volume の各定義元が同じ常駐窓口 pcm_module.c に一致することを照合。|
|boot_info|1|共通 init/bootinfo.c → 機種の boot capture|A+B。検証済み root の情報を共通 boot_common に発行後|boot_image_info の唯一の変換|
|debug|1|共通 kernel/module/kdebug_call.c → 装置 kdebug/|A+B。module が使える像で実コマンドだけ公開し、CPU 固有採取は後端へ|同じ opcode dispatch|
|gfx2|29|共通 gfx/v2/ の列挙・面・submit・fence・display|A は列挙と資源管理、B は of_fb_v2 ops。各機能は caps・check_op 後に使用|共通 gfx2_* と adapter 登録|
|shm|3|共通 mm/shm.c|A。ppc_kernel.c の初期化とアプリの map/owner 回収を確認|shm_alloc/lock/free のリンク元|
|pointer|6|共通 kernel/input_service.c、input/hal/pointer_state.c、機種の登録|C→B。PC-98 の mouse.c から cursor・poll・bounds を input/hal に移し、PC-98 は init・snapshot、PowerMac は USB イベント供給だけを担当|座標・ボタン・範囲の計算元|

#### 関数別の追跡

左は公開 slot（組名の接頭辞を省略しない）。矢印の右は x86 生成 wrap の実呼出。ファイルは上表と下記の定義元で特定する。
各行は同じ組の連続した最大4本。生成器のガードと所有者検証は通常経路に残す。

- sys ［buz_on → buz_on @ kernel/sys.c］ ［buz_off → buz_off @ kernel/sys.c］ ［sys_reboot → sys_reboot @ kernel/sys.c］
- surface ［surface_query → surface_api_query @ exec/surface_query.c］ ［surface_lease → surface_api_lease @ exec/surface_query.c］ ［gfx_surface_lease → surface_api_bundle @ exec/surface_query.c］ ［surface_unlease → surface_api_unlease @ exec/surface_query.c］
- gfx ［gfx_init → gfx_kapi_init @ gfx/gfx_core.c］ ［gfx_init_200 → gfx_kapi_init_200 @ gfx/gfx_core.c］ ［gfx_shutdown → gfx_shutdown @ gfx/gfx_core.c］ ［gfx_present → gfx_present @ drivers/video/pc98/gfx_vram.c］
- gfx ［gfx_hardware_scroll → gfx_hardware_scroll @ gfx/gfx_scroll.c］ ［gfx_present_rect → gfx_present_rect @ drivers/video/pc98/gfx_vram.c］ ［gfx_set_palette → gfx_set_palette_hal @ gfx/gfx_core_api.inc］ ［gfx_get_palette → palette_get @ gfx/palette.c］
- gfx ［gfx_get_framebuffer → gfx_get_framebuffer @ gfx/gfx_core_framebuffer.inc］ ［gfx_add_dirty_rect → gfx_add_dirty_rect @ drivers/video/pc98/gfx_vram.c］ ［gfx_present_dirty → gfx_present_dirty @ drivers/video/pc98/gfx_vram.c］ ［gfx_present_nosync → gfx_present_nosync @ drivers/video/pc98/gfx_vram.c］
- gfx ［gfx_present_raster → gfx_present_raster @ drivers/video/pc98/gfx_vram.c］ ［gfx_screen_info → gfx_screen_info @ gfx/gfx_core_api.inc］ ［gfx_hw_fill_rect → gfx_hw_fill_rect @ gfx/gfx_core_api.inc］ ［gfx_hw_blit → gfx_hw_blit @ gfx/gfx_core_api.inc］
- gfx ［gfx_stats → gfx_stats @ gfx/gfx_core_api.inc］ ［gfx_lease_palette → gfx_lease_palette @ gfx/gfx_core_api.inc］ ［gfx_screen_owner → gfx_screen_owner @ gfx/gfx_core.c］ ［gfx_osri_show → kapi_osri_show @ kapi/kapi_osri.c］
- gfx ［gfx_hw_submit → gfx_hw_submit @ gfx/gfx_hw_submit.c］ ［gfx_hw_get → gfx_hw_get @ gfx/gfx_hw_submit.c］
- clock ［rtc_read → clock_read_legacy @ kernel/time/clock_service.c］
- console_font ［tvram_putchar_at → tvram_putchar_at @ kernel/console/console.c］ ［tvram_putkanji_at → tvram_putkanji_at @ kernel/console/console.c］ ［tvram_scroll → tvram_scroll @ kernel/console/console.c］ ［tvram_readchar_at → tvram_readchar_at @ kernel/console/console.c］
- console_font ［tvram_reverse_cell → tvram_reverse_cell］ ［unicode_to_jis → unicode_file_lookup @ kernel/console/unicode_file.c］ ［font_source_open → font_lease_app_open @ kernel/console/font_source.c］ ［font_source_read_at → font_lease_app_read_at @ kernel/console/font_source.c］
- console_font ［font_source_close → font_lease_app_close @ kernel/console/font_source.c］
- port ［serial_init → port_init @ kernel/port_service.c］ ［serial_puts → port_puts_kapi @ kernel/port_service.c］ ［serial_getchar → port_getchar_kapi @ kernel/port_service.c］ ［serial_putchar → port_putchar_kapi @ kernel/port_service.c］
- port ［serial_trygetchar → port_trygetchar @ kernel/port_service.c］ ［serial_is_initialized → port_is_initialized @ kernel/port_service.c］ ［serial_init_vfast → port_init_vfast @ kernel/port_service.c］ ［serial_get_status → port_get_status @ kernel/port_service.c］
- port ［serial_diag → kapi_serial_diag @ kapi/kapi_port.c］
- gui ［gui_call → gui_call @ kernel/gui.c］ ［gui_register → gui_register @ kernel/gui.c］ ［sys_switch_shell → sys_switch_shell @ kernel/gui.c］
- sound ［fm_startup_sound → fm_startup_sound @ drivers/sound/fm/fm.c］ ［fm_play_mml → fm_play_mml_kapi @ drivers/sound/fm/fm.c］ ［snd_bgm_play → snd_bgm_play @ drivers/sound/fm/fm_module.c］ ［snd_bgm_stop → snd_bgm_stop @ drivers/sound/fm/fm_module.c］
- sound ［snd_bgm_is_playing → snd_bgm_is_playing @ drivers/sound/fm/fm_module.c］ ［snd_se_play → snd_se_play @ drivers/sound/fm/fm_module.c］ ［snd_se_play_raw → snd_se_play_raw @ drivers/sound/fm/fm_module.c］ ［snd_set_master → snd_set_master @ drivers/sound/fm/fm_module.c］
- sound ［snd_bgm_set_persist → snd_bgm_set_persist @ drivers/sound/fm/fm_module.c］ ［fm_note_on → fm_note_on @ drivers/sound/fm/fm.c］ ［fm_note_off → fm_note_off @ drivers/sound/fm/fm.c］ ［fm_set_tone_num → fm_set_tone_num @ drivers/sound/fm/fm.c］
- sound ［ssg_tone → ssg_tone @ drivers/sound/fm/fm.c］ ［ssg_volume → ssg_volume @ drivers/sound/fm/fm.c］ ［ssg_all_off → ssg_all_off @ drivers/sound/fm/fm.c］ ［snd_focus → snd_focus @ drivers/sound/fm/fm_module.c］
- platform ［np2_detect → platform_np2_detect @ platform/pc98/device_host.c］ ［np2_get_version → platform_np2_get_version @ platform/pc98/device_host.c］ ［np2_get_cpu → platform_np2_get_cpu @ platform/pc98/device_host.c］ ［np2_get_clock → platform_np2_get_clock @ platform/pc98/device_host.c］
- platform ［np2_check_hostdrv → platform_np2_check_hostdrv @ platform/pc98/device_host.c］ ［loop_attach → platform_loop_dev_attach @ platform/pc98/device_loop.c］ ［loop_detach → platform_loop_dev_detach @ platform/pc98/device_loop.c］ ［loop_status → platform_loop_dev_status @ platform/pc98/device_loop.c］
- ide ［ide_init → platform_ide_init @ kernel/dev/device_ide.c］ ［ide_drive_present → platform_ide_drive_present @ kernel/dev/device_ide.c］ ［ide_identify → platform_ide_identify @ kernel/dev/device_ide.c］ ［ide_read_sector → platform_ide_read_sector @ kernel/dev/device_ide.c］
- ide ［ide_write_sector → platform_ide_write_sector @ kernel/dev/device_ide.c］ ［ide_write_sectors → platform_ide_write_sectors @ kernel/dev/device_ide.c］ ［ide_get_info → platform_ide_get_info @ kernel/dev/device_ide.c］ ［hdd_geom_info → hdd_geom_info @ platform/pc98/bootinfo_geom.c］
- ext2 ［ext2_format → platform_ext2_format @ kernel/dev/device_fs.c］ ［ext2_format_at → platform_ext2_format_at @ kernel/dev/device_fs.c］
- kcg ［kcg_init → platform_kcg_init @ platform/pc98/device_kcg.c］ ［kcg_set_scale → platform_kcg_set_scale @ platform/pc98/device_kcg.c］ ［kcg_read_ank → kcg_read_ank @ drivers/video/kcg/kcg.c］ ［kcg_read_kanji → kcg_read_kanji @ drivers/video/kcg/kcg.c］
- kcg ［kcg_load_font → platform_kcg_load_font @ platform/pc98/device_kcg.c］
- pci ［pci_count → platform_pci_count @ platform/pc98/device_pci.c］ ［pci_get → platform_pci_get @ platform/pc98/device_pci.c］ ［pci_cfg_read32 → platform_pci_cfg_read32 @ platform/pc98/device_pci.c］ ［pci_bind_info → kapi_pci_bind_info @ kapi/kapi_sys.c］
- serialfs ［sfs_begin → platform_serialfs_session_begin @ kernel/dev/device_fs.c］ ［sfs_end → platform_serialfs_session_end @ kernel/dev/device_fs.c］
- path ［path_get_drive → path_get_drive_user @ exec/exec_tramp_strings.inc］ ［path_get_cwd → path_get_cwd_user @ exec/exec_tramp_strings.inc］ ［path_set_drive → path_set_drive @ lib/path.c］ ［path_set_cwd → path_set_cwd @ lib/path.c］
- path ［path_parse → path_parse @ lib/path.c］
- db ［db_open → kapi_db_open @ kapi/kapi_db.c］ ［db_close → kapi_db_close @ kapi/kapi_db.c］ ［db_exec → kapi_db_exec @ kapi/kapi_db.c］ ［db_prepare → kapi_db_prepare @ kapi/kapi_db.c］
- db ［db_step → kapi_db_step @ kapi/kapi_db.c］ ［db_column_int → kapi_db_column_int @ kapi/kapi_db.c］ ［db_column_text → kapi_db_column_text @ kapi/kapi_db.c］ ［db_finalize → kapi_db_finalize @ kapi/kapi_db.c］
- db ［db_last_error → kapi_db_last_error @ kapi/kapi_db.c］ ［db_mem_used → kapi_db_mem_used @ kapi/kapi_db.c］ ［db_open_existing → kapi_db_open_existing @ kapi/kapi_db_v50.inc］ ［db_prepare_only → kapi_db_prepare_only @ kapi/kapi_db_v50.inc］
- db ［db_bind_int → kapi_db_bind_int @ kapi/kapi_db_v50.inc］ ［db_bind_text → kapi_db_bind_text @ kapi/kapi_db_v50.inc］ ［db_bind_blob → kapi_db_bind_blob @ kapi/kapi_db_v50.inc］ ［db_bind_null → kapi_db_bind_null @ kapi/kapi_db_v50.inc］
- db ［db_error_code → kapi_db_error_code @ kapi/kapi_db_v50.inc］
- system_info ［sys_get_build_info → kapi_sys_get_build_info @ kapi/kapi_sys.c］ ［sys_set_mtime → kapi_sys_set_mtime @ kapi/kapi_sysinfo.c］ ［sys_time_now → kapi_sys_time_now @ kapi/kapi_sys.c］ ［mem_stat → kapi_mem_stat @ kapi/kapi_sysinfo.c］
- system_info ［sys_ver_info → kapi_sys_ver_info @ kapi/kapi_sys.c］
- ime ［ime_getchar → ime_getchar_kapi @ kernel/ime/fep_module.c］ ［ime_trygetchar → ime_trygetchar @ kernel/ime/fep_module.c］ ［ime_toggle → ime_toggle @ kernel/ime/fep_module.c］ ［ime_is_active → ime_is_active @ kernel/ime/fep_module.c］
- ime ［ime_set_mode → ime_set_mode @ kernel/ime/fep_module.c］ ［ime_get_mode → ime_get_mode @ kernel/ime/fep_module.c］ ［ime_getkey → ime_getkey_kapi @ kernel/ime/fep_module.c］ ［ime_switch_dict → ime_switch_dict @ kernel/ime/fep_module.c］
- ime ［ime_user_list → ime_user_list_facade @ kernel/ime/fep_module.c］ ［ime_user_delete → ime_user_delete_facade @ kernel/ime/fep_module.c］ ［ime_user_export → ime_user_export_facade @ kernel/ime/fep_module.c］ ［ime_user_clear → ime_user_clear_facade @ kernel/ime/fep_module.c］
- ime ［ime_trygetkey → ime_trygetkey @ kernel/ime/fep_module.c］ ［ime_feed_key → ime_feed_key @ kernel/ime/fep_module.c］ ［ime_set_render → gui_ime_set_render @ kernel/gui.c］ ［ime_dict_info → ime_dict_info_facade @ kernel/ime/fep_module.c］
- ime ［ime_dict_suspend → ime_dict_suspend @ kernel/ime/fep_module.c］
- v86 ［v86_selftest → v86_smoke_test @ platform/pc98/legacy/v86.c］ ［v86_disktest → v86_disk_test @ platform/pc98/legacy/v86.c］ ［v86_boot → v86_boot @ platform/pc98/legacy/v86.c］ ［v86_boot2 → v86_boot2 @ platform/pc98/legacy/v86.c］
- v86 ［v86_gdc_capture → v86_gdc_capture @ platform/pc98/legacy/v86_gcap.c］
- pcm ［pcm_open → pcm_open @ drivers/sound/cs4231/pcm_module.c］ ［pcm_write → pcm_write @ drivers/sound/cs4231/pcm_module.c］ ［pcm_status → pcm_status @ drivers/sound/cs4231/pcm_module.c］ ［pcm_close → pcm_close @ drivers/sound/cs4231/pcm_module.c］
- pcm ［pcm_set_volume → pcm_set_volume @ drivers/sound/cs4231/pcm_module.c］
- boot_info ［boot_image_info → boot_image_info @ init/bootinfo.c］
- debug ［kdebug_call → kdebug_call @ kernel/module/kdebug_call.c］
- gfx2 ［gfx2_adapter_count → gfx2_adapter_count @ gfx/v2/enum.c］ ［gfx2_adapter_info → gfx2_adapter_info @ gfx/v2/enum.c］ ［gfx2_adapter_gen → gfx2_adapter_gen @ gfx/v2/backend_reg.c］ ［gfx2_caps → gfx2_caps @ gfx/v2/caps.c］
- gfx2 ［gfx2_check_op → gfx2_check_op @ gfx/v2/check_op.c］ ［gfx2_surface_create → gfx2_surface_create @ gfx/v2/surface.c］ ［gfx2_surface_free → gfx2_surface_free @ gfx/v2/surface.c］ ［gfx2_surface_desc → gfx2_surface_desc @ gfx/v2/surface.c］
- gfx2 ［gfx2_screen_surface → gfx2_screen_surface @ gfx/v2/surface.c］ ［gfx2_lock → gfx2_lock @ gfx/v2/lock.c］ ［gfx2_unlock → gfx2_unlock @ gfx/v2/lock.c］ ［gfx2_submit → gfx2_submit @ gfx/v2/submit.c］
- gfx2 ［gfx2_fence_wait → gfx2_fence_wait @ gfx/v2/fence.c］ ［gfx2_fence_query → gfx2_fence_query @ gfx/v2/fence.c］ ［gfx2_head_info → gfx2_head_info @ gfx/v2/enum.c］ ［gfx2_connector_info → gfx2_connector_info @ gfx/v2/enum.c］
- gfx2 ［gfx2_connector_edid → gfx2_connector_edid @ gfx/v2/enum.c］ ［gfx2_mode_count → gfx2_mode_count @ gfx/v2/enum.c］ ［gfx2_mode_get → gfx2_mode_get @ gfx/v2/enum.c］ ［gfx2_mode_set → gfx2_mode_set @ gfx/v2/modeset.c］
- gfx2 ［gfx2_flip → gfx2_flip @ gfx/v2/display.c］ ［gfx2_vblank_wait → gfx2_vblank_wait @ gfx/v2/display.c］ ［gfx2_palette → gfx2_palette @ gfx/v2/display.c］ ［gfx2_gamma → gfx2_gamma @ gfx/v2/display.c］
- gfx2 ［gfx2_cursor_image → gfx2_cursor_image @ gfx/v2/display.c］ ［gfx2_cursor_move → gfx2_cursor_move @ gfx/v2/display.c］ ［gfx2_cursor_show → gfx2_cursor_show @ gfx/v2/display.c］ ［gfx2_dpms → gfx2_dpms @ gfx/v2/display.c］
- gfx2 ［gfx2_present → gfx2_present @ gfx/v2/display.c］
- shm ［sys_shm_alloc → shm_alloc @ mm/shm.c］ ［sys_shm_lock → shm_lock @ mm/shm.c］ ［sys_shm_free → shm_free @ mm/shm.c］
- pointer ［mouse_poll → pointer_poll @ kernel/input_service.c］ ［mouse_available → pointer_available @ kernel/input_service.c］ ［mouse_set_bounds → pointer_set_bounds @ kernel/input_service.c］ ［mouse_cursor_set_mode → pointer_cursor_set_mode @ kernel/input_service.c］
- pointer ［mouse_cursor_show → pointer_cursor_show @ kernel/input_service.c］ ［mouse_cursor_hide → pointer_cursor_hide @ kernel/input_service.c］
- pointer の公開の順 (PowerPC の PM 決定 2026-11-19、Opus のレビューの指摘): 段 3c は mouse_set_bounds・mouse_cursor_set_mode・mouse_cursor_hide の 3 本を先に出す。mouse_available・mouse_poll・mouse_cursor_show は、本線の後端の ops (#27) と既定の範囲 (#28) の後に一緒に出す (available だけを先に出すと、poll が無いまま 1 を返し、アプリが初期化していない MouseInfo を読むため)。

### 3. 実施順と担当

段の単位は半日〜1日・製品ファイル1個。下の表の各行は作業の順序であり、異なるファイル名を矢印で連ねたものは別々の段とする。
後端を未登録のままビルドできる順に進める。共通の口、PC-98 側の採用、PPC の採用、像の選択、テスト、最後の公開表はそれぞれ別ファイルの段。
各段で単独で受入可能な差分を残す。1ファイルに複数日分の機能がある場合は slot の部分集合で分割する。担当は本線＝共通・PC-98、PPC＝PowerMac の配線・PPC の公開表。
A の場合も、対象像が持つ弱い代役に解決されていないことを確認してから公開する。

|順|担当・対象ファイル（提案の新規ファイルは「新設」）|出口と次段の条件|
|---|---|---|
|1a|本線 build/sources/common.mk|lib/path.c を EXEC の持ち物へ。PC98_PENDING と二重選択しない。validator と初期 drive を対象像で確認|
|1b|PPC platform/powermac/root_boot.c|root の公開後に path の実在する device を選ぶ。USERSHELL 固有の hostfs は mount 後だけ利用|
|1c|PPC sdk/kapi_target_ppc.py|path 5本を公開。get の返す文字列は既存のアプリ領域へコピー|
|1d|PPC 同公開表|shm 3本。既存 map の書込・別所有者・終了回収が通れば path と並列可|
|2a-i|本線 kapi/kapi_sys.c|build と mtime の依存を分離。像で不足する診断変数は常に0の extern で補わない|
|2a-ii|本線 kapi/ の共通 TU|2a-i はその場の変更を NP21/W で受入。その後、分離済み関数だけを移動し x86 build_compare の関数差0|
|2b|本線 kernel/time/ktime.c → kernel/ver_collect.c|前者は clock source の実値、後者は boot と機能欄の提供元を確認。各採用は #6 の1ファイル分だけ|
|2c|PPC 公開表|system_info は build、mtime、time、mem、ver の順。未準備なスナップショットの slot は後続へ|
|3a|PPC platform/powermac/escc.c|既存送信の所有者・ログとの共存、受信と非待機読取を確認。PortOps 登録は別段|
|3b-i|本線 drivers/input/mouse.c|d2_bind.c の PointerOps が使う poll・bounds のつなぎ、clamp、tvram_reverse_cell の set_mode/show/hide をその場で分離。init と snapshot を PC-98 に残し NP21/W で検証|
|3b-ii|本線 input/hal/|分離済み cursor と poll・bounds を純移動し、PC-98 の登録で同じ動作を確認|
|3c|PPC platform/powermac/usb_platform.c|移動後の共有入口 pointer_move とボタン入力へ配送。抜去時の解放とキーボード同時接続を確認して公開|
|4a|PPC 公開表だけ|セル5本を既存 console.c の con_backend_scroll・readchar・reverse 等と fbcon に結線。tvram_clear の生成分岐も照合|
|4k-i|本線 build/select.mk|FEATURE_KCG の FEATURE_PC98_DEVICES 必須条件を解き、PC-98 の選択と無効像も確認|
|4k-ii|本線 kernel/dev/device_kcg.c|device_kcg.c の固定名転送を純移動し両像で同じ TU と kcg.c を選ぶ|
|4b|共通 kernel/console/font_source_kcg.c|PPC は既存 KCG1 から kcg_rom_read_ank・kanji を供給。load は両像で NOSYS。既存 KCG1 をフォント供給元に選ぶ。font lease の close と exec 終了の回収を先に受入|
|5a-i|本線 drivers/video/pc98/gfx_vram.c（その場の変更、NP21/W で確認）|present、rect、dirty、nosync、raster のクリップ・変更矩形管理を抽出。PC98 VRAM 書込と同期は装置に保持|
|5a-ii|本線 `gfx/gfx_dirty.c`|分離した dirty 列・矩形処理だけを純移動。flip・VSYNC 待ちは装置側。x86 関数差0 (変位と `gfx_add_dirty_rect` からの呼出前後のスタック整列、ret 後の実行されない整列命令のみ許容)|
|5b|本線 gfx/gfx_core.c → PPC platform/powermac/display_provider.c|共通 present が後端へ渡す。INDEX8 の変換・pitch は既存 OF FB を使う。200行指定の実現可能範囲を照会|
|5c|共通 HAL → OF FB 後端|所有・世代・転送範囲は共通 HAL、固定モード・pitch・画素形式は後端が返す。既存の表示の門は B02 で整理する。|
|5d|PPC_SHELL_ALIGN_X86_DESIGN → kernel/gui.c|段 E で製品シェルを owner 1 の起動ループに接続する。段 F で通常の wrapper から共通の sys_switch_shell を通して shell → GUI → shell と回収を確認し、子からの切替要求が拒否されることを確かめて公開する。|
|6a|PPC drivers/macintosh/ の VIA 装置 → platform/powermac/system.c|wt/ppc-reset の 955665762、docs/decisions/ppc/PPC_RESET_DESIGN.md の順。後続の rtc_read も同じ via_command を使用。再起動1本を先に公開、時計は同一装置を登録して別段で公開|
|6f|転送先 TU は `kernel/dev/device_fs.c`・`kernel/dev/device_host.c` (2026-11-19 に移した)。loop (`kernel/dev/device_loop.c` の予定) は保留 — 転送先の loop_dev_* が装置のヘッダ drivers/block/loop_dev.h にしか無く、共通から呼ぶ契約 (include/ の宣言) ができるまで platform/pc98/device_loop.c に残す|serialfs・loop・ext2 の順に一組ずつ両像共通の TU に純移動。device_host.c の既存 PPC 転送も選択を切り替え、各固定名の定義を一つにする。PowerPC の EXEC 像は device_fs.o を選ぶが、中の転送先 (ext2_format*・serialfs_session_*) は PowerPC に無く、-ffunction-sections と --gc-sections で捨てられてリンクが通っている (6b・6c で転送先が入るまでの前提)|
|6b|本線 fs/serialfs/serialfs_session.c → PPC hostfs_link_io.c|協調進行と session の所有を確定。既存のユーザー shell の transport を二重に初期化しない|
|6c|本線 drivers/block/loop_dev.c → fs/ext2/ext2_fmt.c|仮想媒体の attach と書込権限を通してから formatter を選択。loop と ext2 は独立公開|
|6d-i|本線 drivers/pci/pci.c|mech1 を config 後端に分離して NP21/W で照合。別の純移動段で pci_record・pci_scan_bus・count/get・bind 台帳と固定名転送を共有|
|6d-ii|PPC platform/powermac/pci_config.c（pm_pci_read32 等の config 読み書きだけ）|BDF と項目順を確定。列挙から外れる装置を既存 GMAC への固定応答で代用しない|
|6e|本線 init/bootinfo.c → PPC platform/powermac/root_image.c|捕捉した検証結果の発行順を決め、boot_info と ver の根拠を共有|
|7a-選択|本線 build/select.mk|FEATURE_FEP の FEATURE_PC98_DEVICES 依存を実際の前提機能に置換。両像の有効・無効構成を照合|
|7a|module の読み込み L1 → L2 → FEP → KCG|fep.mod の起動と回収を x86 に揃える。辞書・学習は HDD の後、x86 と同じ VFS パスで公開する。|
|7b-選択|本線 build/select.mk|FEATURE_SQLITE の FEATURE_PC98_DEVICES 依存を実際の前提機能に置換。両像の有効・無効構成を照合|
|7b|静的 SQLite → kapi/kapi_db.c|公開済みの db 17 本を維持する。製品経路で VFS 上の DB、段 S のスタック・FP 保存復帰・guard、SHM と bind の寿命を確認する。SQLite の module 化は任意の B08。|
|7c|本線 kernel/module/kdebug_call.c|module ローダの有効化後、許可された診断コマンドを選ぶ。CPU 固有の採取は独立段|
|8|本線 AV HAL の音声契約 → 装置後端、ATA の block 契約 → 装置後端|HAL 票の依存（音は A0・1kHz tick）を待つ。契約確定後に PowerPC 担当が各後端を接続する|
|8-音|本線 AV HAL の合成器の照らし直し → os32_opna A〜D → L2、IS → L1、L1・L2 → L3、C3・L3 → L6|その後、PowerPC 担当が PowerMac の PCM 後端 drivers/sound/powermac/pcm_powermac.c（新設）を接続する。装置候補は Screamer（NetBSD awacs.c）や snapper（同 snapper.c）。G4 QuickSilver の実機搭載品は未確認。|

本線の CI 手直し（TASK_CI_REWORK）と CRT 後始末を先行させる。小さい純移動だけは CI の段と並行できる。

|本線の挿入順・先行段|それを待つ PowerPC の段|
|---|---|
|1a、2a-i→ii・2b|1b→1c、2c。1d は独立|
|3b-i→ii|3c。3a は独立|
|4k-i→ii|4b。4a は独立|
|5a-i→ii、5b → 5c → 5d|段 E・F のシェル接続と共通の切替の門の受入後に切替を公開。既存の二重実装の整理は KAPI の機能を揃えてから行う。|
|6f の各転送、6b・6c の媒体・通信契約|serialfs、loop、ext2 の各公開。6a は reset 設計の装置を待つ|
|6d-i、6e の発行口|6d-ii、root_image|
|7a・7b の選択|L1 → L2 の後に FEP → KCG。辞書・学習には HDD、DB は静的結線を維持して製品 VFS で受け入れる。7c は debug module の後。|
|8 の PCM 常駐窓口分離・AV HAL、ATA の共通窓口|実音声・ATA 後端、buz 再判定|

**全段の完了条件**　表の各行とその分割段で §4 の追従を確認する。両像のリンク map とデバッグ情報付き ELF の nm -l から、対象 slot → wrap の直接呼出先 → 定義元ソースのリポジトリ相対パスを突き合わせる。通常 wrap は両像でパス一致、未公開段は共通にした定義を先に照合し公開段で wrap から再照合する。不在 slot は生成された不在経路を確認する。差分で arch/ppc・platform/powermac に加わる関数定義名と x86 の定義名の積集合は空であること。パス不明は合格に数えない。純移動は毎回 docs/SRC_MOVE.md と x86 build_compare 関数差0、共通の口は両機種の呼出経路と check_layers 違反0を受入記録に残す。

- 並列化は別ファイルに限る。1d と 1a、3a と 3b-i、6a と 5a-i、段 S と表示を並行できる。公開表と common.mk の採用変更は担当が直列に取り込む。
- C は本線が docs/SRC_MOVE.md §3 の対応表→dry-run→移動→参照更新を担当。移動と処理変更を分け、include 元、走査対象、試験 covers と変異の参照を追従させる。
- D は include/<名>_contract.h に共通の入出力と能力を定義し x86 呼出側を先に通す。機種別の定数・登録は同名 platform_<名>.h、CPU 保存規約は arch_<名>.h に閉じる。
- E は §5 の不在時試験が完了条件。実装済み本数とは別に記録し、残りスロット数だけを達成指標にしない。

### 4. 公開表、生成物、ABI

1. 段の開始時に slot 名と現 wrap、前提機能、ユーザー出力・資源所有者を対応付ける。AC-KAPI-1 の通常・欠如両方を試験する。
2. sdk/kapi_target_ppc.py の該当 frozenset から受入済み名だけを外す。空集合になった組を消す際は IME_SLOTS の直接参照とその利用者も点検する。
3. include 除外は全不在組の和集合である。一部だけ有効にした時点で必要になったヘッダを除外から外す。同じヘッダが他組に残っていないことを確認する。
4. ZERO_SLOTS は absent_result 用。部分公開後も残りの不在物の型別値を保ち、IME の不在照会は NOSYS のまま。通常 wrap は KAPI_HIT と生成済み引数検査を通る。
5. sdk/gen_kapi.py --target ppc-powermac --out-dir と --target x86-pc98 --out-dir を各々独立の出力先で基点と候補に対して実行する（起動は python3 -B）。
6. x86 の全生成物を基点とバイト比較。PPC は対象 include と通常 wrap への復帰を照合し、無関係な wrap・dispatch・公開構造体・slot 定数・Rust 宣言が基点と一致すること。
7. sdk/kapi.json の api 順・ret/args・data_fields、世代、version=87、capacity=300 を基点比較。KAPI_FUNC_COUNT=298 と引数幅・ptr マスク、HWDEV/FEP のレビュー済み指紋も比較する。
8. os32-kapi-add の生成差分・kapi-abi・生成再現性・呼出受入を適用。今回は既存 slot の提供だけなので版上げ・指紋更新・SDK 要求版変更は不要。
9. 公開 ABI 変更が不可避な案はこの段から外す。別の承認後、KAPI_SPEC §3-1 の末尾追加・版同期・再生成・clean→all を適用。外部 apps/game は H-7 に従う。

10. 各段で tools/ppc/test_kapi_binding.py の len(entries)==190 を段の不在 slot 集合との一致へ追従し、ALLOWED_UNDEFINED は残る正当な参照だけに更新。group-empty の gfx が空になる段では残存組を対象に替え、変異が実行時 RED になることを確認する。
11. ROLES §4-3 に従い TU の追加・移動は build/sources/order.mk、像別選択、build_profile_x86.json の C_KERNEL、ppc-powermac.mk を追従。check-build-selection-host と make all を確認する。試験の CHECK.covers・変異アンカーも実際の定義元へ更新し lint と対象変異を通す。

### 5. 受入と変異

実装した段ごとに mac99 の CPU 7450 と 7455 の両方で同じユーザーアプリを実行し、起動構成・slot・戻り値・実際の副作用を記録する。
下表の変異は製品の1箇所を変える例。各変異が該当する観測で実行時に RED になることを確かめる。装置の無い QEMU 構成は不在側だけの合格とする。
後端がある場合は既知の入力と独立した期待値で検証する。実機固有能力を host の模擬後端で検証した場合、実機での受入は別欄に残す。

|組|ゲストで観測する操作と結果|host 試験・変異例|
|---|---|---|
|path|shell から既知の mount に移動、相対パスを開き、drive/cwd を読む。不正 drive 後も元の状態|validator の拒否を成功に替える|
|system_info|版とビルドを取得、書込可のファイルに mtime を設定して再読、時刻は単調。mem_stat の要求サイズと所有者を変える|time の上位・下位を交換、既知の64bit標本と比較|
|shm|確保・lock・書込・解放・再確保、終了した子の領域を回収|解放を飛ばし容量復帰で検出|
|port|ESCC 対向と既知バイトを往復。入力なしの try、設定速度、diag と initialized を照合|送受信の1文字破棄、速度応答固定|
|pointer|USB 相対移動、端で clamp、ボタン押下中に抜き、再接続。カーソル show/hide と位置を観測|release 配送を落として押下残りを検出|
|console_font|指定セルの ANK/JIS、反転と scroll の画面・読戻し、既知 Unicode の JIS 変換、無圧縮の東雲16 KCG1 から字形を部分読取、EOF 境界・close 後の拒否、終了前に使い切った lease 枠の再取得|行送りを1行ずらす、lease 範囲端をずらす、提供者を外す|
|surface（単独）|OF FB の CLIENT を query→lease→書込と読戻し→unlease。解除済み token は INVAL、世代変更後の旧 ref は STALE。アプリ終了時に残した lease を回収|世代照合を無効にすると旧 ref の取得が成功。提供者を外すと query が NOSYS|
|surface（束、本線 #18 の確定設計後）|QEMU で書ける出力先で件数の不一致を与え、入力を読む前の INVAL と、その後の正しい CLIENT 1 面での再取得を確認|束の後半で失敗した場合の巻き戻しは本線の host 受入で確認 ([束の設計 §5](../display/SURFACE_LEASE_BUNDLE_DESIGN.md))|
|gfx|画面寸法と owner を取得、既知矩形だけ更新し周囲の pixel を保存。非対応 raster/HW は能力に一致|dirty 範囲の右端を欠落させる|
|gfx2|adapter/head/connector/mode と caps、面生成・lock・描画・present・解放。fence と非対応 EDID 等は実能力どおり|of_fb_v2 の固定モード比較を外し異寸法が通ることを検出|
|gfx/gfx2（OF FB）|`gfx_screen_owner` と `gfx2_vblank_wait`・`gfx2_palette`・`gfx2_gamma`・`gfx2_cursor_move`・`gfx2_cursor_show`・`gfx2_dpms` を結線。旧 link-sh の試験を製品起動へ移し、owner 1 のシェルで表示所有、無効 head の INVAL、OF の vblank 未対応（HAL v2 §11-7 の NOSYS）、非所有者の表示制御の BUSY、終了後の head と owner を確認| `make check-ppc-display-guest MUTATE=1` で owner・vblank・表示制御の3組の提供者を外す |
|gui|登録者に要求が一度届き、GUI 終了後に shell に入力が戻る|終了時の所有解放を省略|
|sys|再起動前後の boot ログを別起動として観測。確定設計の PMU/Cuda ごとに送信回数を照合。buz は無動作|reset の装置呼出を外し再起動を検出できなくする|
|clock|mac99 の Cuda と PMU を `-rtc base=2024-02-29T00:00:00` で起動し、RSH から 7 バイトの暦時刻と曜日を読む。起動から読出しまでの経過を加えた基準時刻との差は 2 秒以内|年を 1 年ずらす、Cuda と PMU の読出し命令を交換する、公開表で提供者を外す|
|platform|NP2 照会は型別不在。既知イメージを loop attach、容量を照合し detach 後の状態を読む|loop_status の sector 数を1増やす|
|ide|ATA 有効像では識別済み試験用媒体だけで sector 往復。不在像は present=0。拒否した書込後に内容が一致|先頭 LBA を1ずらし隣接 sector の番兵で検出|
|ext2|使い捨て loop 上で format と format_at、再 mount、範囲外の前後 sector を比較|start_lba を無視させる|
|kcg|KCG1 の既知 ANK/JIS を読み字形と照合、両像で kcg_load_font が NOSYS を返した後の有効フォントと倍率を確認|LE offset の解釈を反転|
|pci|列挙した index の vendor/device を BDF の config 値と照合し、bind 情報の同じ index を確認|cfg の byte swap を落とす|
|serialfs|既存 peer と begin、ファイル読取、link の結果 RSH OUT の `sfs: exit=N` で end の結果を取得。切断・再開は host の A7（test_serialfs）で検査する|exit の行を落とす|
|db|書込可 VFS で create/insert、int/text/blob/null の bind、step、column、finalize、close、再 open。失敗コード・使用量・終了回収を観測|別スタック復帰時の所有者復元、または bind 値の複製を欠落させる|
|ime|登録済み辞書で変換、切替・編集・export・suspend、キー未入力の try と未提供時 NOSYS を区別|辞書切替を無視し既知の異なる候補で検出|
|v86|5本それぞれ不在を確認、画面と起動状態は保持|不在の値付き応答を成功に替える|
|sound|host で既知の音色・音高・長さをsndl_chip_* の SOFT 経路に与え、FM と SSG の各発音、音量変更、消音後の PCM を基準波形と照合。PC-98 の同じ操作は HW 書込みで確認。mac99 はゲストから列挙できる音声装置と録音経路を確認し、利用可能なら録音で照合。観測不能なら host の波形試験と PowerMac 実機の録音を揃えて受入。|共通合成器の FM または SSG の消音処理 を無効にして消音区間の照合が落ちる|
|pcm|既知の左右別の整数波形を open/write し、status の消費量、音量、close を確認。mac99 の録音が可能ならサンプル順と振幅を照合し、観測不能なら host の転送試験と実機の対向録音で確認。|左右またはサンプル byte 順を交換|
|boot_info|検証済み媒体と未検証の起動で valid、容量・検証値を照合。OF に架空の PC98 起動元を付けない|未検証側で valid を立てる|
|debug|実装済み opcode を実モジュールで往復し未知番号と区別。診断の前後で shell 継続|dispatch 先を隣の opcode に替える|

PowerMac の暦時計は reset と同じ VIA インスタンスを使う。Apple Power Mac G4 (September 2002) Developer Note の KeyLargo / PMU99 の資料は [reset 設計 §4](PPC_RESET_DESIGN.md) に記載。通信の手本は NetBSD `sys/arch/macppc/dev/cuda.c:788–815`、`cudavar.h:45`、`pmu.c:492–558,727–729`、`pmuvar.h:41`。Cuda は `01 03` に続く応答の 3 バイトのヘッダを検証し、PMU は `38` の固定長応答を読む。どちらも時刻本体は 1904-01-01 からの秒数を表すビッグエンディアン 4 バイト。PowerMac の後端でグレゴリオ暦に変換し、公開スロットは既存の 2 桁年と日曜を 0 とする曜日を返す。

- 各組で通常 wrap が残留 stub に解決された場合に成功経路の観測が失敗すること。E は機種固有の後端呼出が0回であることも host 側で計数する。
- make ppc-ci-image の common-compile、両機種の layer 検査、各段で §3 のパス一致・同名新規定義0の照合記録、像ごとのリンク map と nm -l を保存。map に共通本体が一つあり、PPC 後端の新規定義は機種・装置操作だけか diff で検査する。
- 実装段では make all、該当する guest/host 試験と変異、python3 -B tools/checks/run.py --lint。変更した製品行に依存する既存の変異も追従させる。
- 取り込み候補の revision ができたら python3 -B tools/build_compare.py a76398a35 <候補revision> --all を実行し、同じ固定時刻・ビルド情報で x86 の生成物を比較する。
- 移動のみの段は x86 同一を合格条件とする。共通 HAL の変更で差が必要なら段を分離し、変更対象を申告して NP21/W で対になるアプリ操作を検証する。
- 今回は設計のみ。ソース変更・フォント配備・ビルド・QEMU・変異・build_compare は未実施。上表の成功例は実測結果ではなく各段の完了条件である。

### 6. ユーザー判断

- 0件。
- 段 S の採用 revision、各像の後端の有無、QEMU が提示する能力の確認は担当者が行う実装前の照合作業とする。

反論なし。


## 本線の取り決め (2026-11-18、本線 PM の確認で追加)
1. **移す先のファイル名**: 2a-ii「kapi/ の共通 TU」、3b-ii「input/hal/」、4k-ii「KCG 転送 TU」、5a-ii「gfx/ の共通 TU」、6f「両像共通の TU」の移す先は、**本線が各段の着手の時に docs/SRC_MOVE.md §3 の対応表で決める** (1 機能 1 ファイル)。決めたら票に書き戻す。
2. **共有の登録ファイルの持ち主**: 1a (build/sources/common.mk)、4k-i・7a-選択・7b-選択 (build/select.mk) は docs/tasks/agents/PARALLEL_BRANCHES.md §2 の共有の登録ファイル。本線がこれらの段を出す時は、その時の持ち主から受け取ってから出す。select.mk の 3 段は同じファイルなので本線の列では直列。
3. **NP21/W の受入は直列**: 「その場の変更」の段 (2a-i・3b-i・5a-i・6d-i) は NP21/W の受入が要るので、本線の列では 1 本ずつ並べる。並列にできるのは「移すだけ」の段どうしと、PowerPC の段との組だけ。
- 本線の最初の差し込みの予定: 1a・2a-i・2b・3b-i (CI の手直しの S0 の取り込み、CRT の後始末の後。1a は wt/ppc-sqlite-stack が common.mk を手放した後)。

- 追記 (2026-11-19): pci の組の本体を両機種で選べるようにする設計は [PCI_COMMON_DESIGN](PCI_COMMON_DESIGN.md) (本線の M1〜M3 と PowerPC の P1〜P5)。
