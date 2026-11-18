# REVIEW r1 (Opus 5.5、2026-11-18) と PM の決定
判定: 要修正 (P1 3 件で道の振り分けと §3 の並びが変わる)。
| ID | 重さ | 節 | 指摘 | 直し |
|---|---|---|---|---|
| 1 | P1 | §2 ext2・serialfs・platform(loop)・kcg・pci・ide | KAPI の wrap が呼ぶ先は platform_* の固定名 (sdk/kapi.json)。PC-98 の定義は共通/装置の関数へ渡すだけの薄い関数 (platform/pc98/device_fs.c・device_loop.c・device_kcg.c・device_pci.c・device_ide.c)。kapi.json を変えないので、「A」にすると platform/powermac に同じ転送関数を書き写す。前例 platform/powermac/device_host.c は既に link 用に同じ形の転送を持つ | 呼ぶ先が共通の転送 (ext2・serialfs・loop) は、両像で選ぶ共通の TU に移す C 段を足す。PowerPC が持つのは装置の後端だけ |
| 2 | P1 | §2 pointer・§3 3c | x86 の PointerOps (platform/pc98/d2_bind.c) の中身は drivers/input/mouse.c。bounds と cursor の層 (clamp、tvram_reverse_cell を使う set_mode・show・hide) は機種に依らない。PC-98 固有は mouse_init と snapshot だけ | 先に C 段 (cursor 層と poll・bounds のつなぎを input/hal へ移す)。PowerPC は platform/powermac/usb_platform.c から pointer_move・ボタンへ渡す後端だけ |
| 3 | P1 | §2 kcg・§5 kcg | drivers/video/kcg/kcg.c は機種に依らない (scale の範囲、kcg_rom_read_* への転送、load_font は NOSYS)。「init/scale は B、load は B」は x86 と同じ処理の書き直しで、load の意味がずれる | kcg.c を PowerPC でも選ぶ「A」。B は KCG1 (drivers/video/kcg/kcg_file.c) からの kcg_rom_read_ank・kanji だけ。**PM 決定: kcg_load_font は両像とも NOSYS** |
| 4 | P2 | §2 pci・§3 6d | drivers/pci/pci.c に機種固有の config 方式 (mech1) と機種に依らない走査 (pci_record・pci_scan_bus・count・get) が同居 | C (走査・記録・bind 台帳を共通へ) → B (config の読み書きだけを platform/powermac/pci_config.c の pm_pci_read32 で後端) と明記 |
| 5 | P2 | §3 7a・7b、kcg | build/select.mk で FEATURE_KCG・FEP・SQLITE が FEATURE_PC98_DEVICES を必須にしている。解く段が無い | 機能ごとに本線の select.mk の段を足し、PowerPC の段より前に |
| 6 | P2 | §3 5a・2a | 5a は「抽出」と移動が混ざる (drivers/video/pc98/gfx_vram.c の dirty の列と flip・VSYNC 待ち)。2a (kapi_sys.c の分割) も中身の変更。本線の「移す段と中身を変える段を混ぜない」に反する | 各々 2 行に: (i) その場での中身の変更 (NP21/W)、(ii) 移すだけ (build_compare の関数差 0) |
| 7 | P2 | §4 | 段ごとに壊れる既存の試験が追従の対象に無い (tools/ppc/test_kapi_binding.py の len(entries)==190、ALLOWED_UNDEFINED、変異 group-empty の対象 'gfx')。ROLES §4-3 の行 (order.mk・covers) も無い | §4 に追従の物として足し、各段の完了条件に写す |
| 8 | P2 | §2「照合」列・§5 | 重複しないことの確かめ方が名前の列挙だけ。check_layers は転送関数の重複を検出できない | 機械的に: 各 slot の wrap が呼ぶ先について、x86 と PowerPC のリンク map (nm -l) で定義元のソースのパスが同じ。差分の arch/ppc・platform/powermac に x86 と同じ名前の関数の定義が 0 |
| 9 | P3 | §3 4a | platform/powermac/console.c に con_backend_scroll・readchar・reverse が fbcon で既にある | 4a を「公開表だけ」に |
| 10 | P3 | 関数別 pcm | x86 の wrap の呼ぶ先は常駐の窓口 drivers/sound/cs4231/pcm_module.c (module への振り分け、PC98 tick の assert)、pcm_cs4231.c ではない | 定義元を直し、窓口を分ける作業を D に |
| 11 | P3 | §2 sys・clock、§3 6a | 再起動は草案 r1 でなく確定設計 docs/decisions/ppc/PPC_RESET_DESIGN.md (枝 wt/ppc-reset の 955665762)。VIA は drivers/macintosh/、via_command に後で rtc_read も載る | 確定版を引き直し置き場を合わせる |
| 12 | P3 | §2 sys buz | Mac の標準 OS にはシステムビープがある。E と言い切るのは「操作は各 arch の標準 OS」と合わない | **PM 決定**: 「当面は E、AV HAL の後に判定し直す」 |
- 本線の列に差し込む位置の一覧が無い (P3 相当): §3 に「本線の段 → それを待つ PowerPC の段」の対応表を足す。
- 未決 (FM/SSG の互換の範囲) はユーザーに聞く (推奨: 対象外、音は AV HAL の PCM・BGM・効果音で)。回答が来たら r2 に反映する。
