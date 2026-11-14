# PPC 未登録試験の照合（ef701722、2026-11-14）
対象は `CHECK = dict(` / `CHECK=dict(` の **79本**。P=`tools/ppc`、T=`tools/tests`、W=`tools/checks`。
a=既存CHECK経由、a*=その経路で補助関数だけ使用（独立main・変異は未実行）、b=手動台本/他の試験だけ、c=実行呼出しなし。内訳 a=15（うち補助のみ4）、b=18、c=46。
宣言のcovers/inputsやコメントを呼出しに数えず、subprocess・import・`run.py --run`を照合。生データは `/home/hight/os32-tmp/run/ppc-unreg/{inventory.json,callers.txt,imports.txt,runs.json}`。
表のrc欄は `通常rc:最終非空行 / --mutate rc:最終非空行`。空出力は∅。一時領域内の基点の写しで直列実行し、製品・試験は変更していない。

|本の名前|CHECK名|分類|呼び手（file:line）|cのrc・末行（通常 / 変異）|
|---|---|---|---|---|
|P/kboot_accept.py|ppc-kboot-accept|a*|W/ppc-fb.py:30 → P/fb_run.py:20,281（buildのみ）|—|
|P/kboot_boot.py|ppc-kboot-boot|a*|W/ppc-rh-fb.py:23 → P/rh_fb.py:13,96（buildのみ）|—|
|P/kboot_capture.py|ppc-kboot-capture|b|P/kboot_all.py:32,62|—|
|P/kboot_clock.py|ppc-kboot-clock|b|P/kboot_all.py:32,62|—|
|P/kboot_console.py|ppc-kboot-console|b|P/kboot_all.py:32,62|—|
|P/kboot_cpu.py|kboot-cpu|b|P/kboot_all.py:32,62|—|
|P/kboot_dec.py|kboot-dec|b|P/kboot_all.py:32,62|—|
|P/kboot_entry.py|ppc-kboot-entry|b|P/kboot_all.py:32,62|—|
|P/kboot_hpt.py|kboot-hpt|b|P/kboot_all.py:32,62|—|
|P/kboot_layout.py|ppc-kboot-layout|b|P/kboot_all.py:32,62|—|
|P/kboot_log.py|ppc-kboot-log|b|P/kboot_all.py:32,62|—|
|P/kboot_memory.py|ppc-kboot-memory|b|P/kboot_all.py:32,62|—|
|P/kboot_mmu.py|kboot-mmu|b|P/kboot_all.py:32,62|—|
|P/kboot_mpic.py|kboot-mpic|b|P/kboot_all.py:32,62|—|
|P/kboot_mpic_tree.py|ppc-kboot-mpic-tree|b|P/kboot_all.py:32,62|—|
|P/kboot_reserve.py|ppc-kboot-reserve|b|P/kboot_all.py:32,62|—|
|P/kboot_root_image.py|ppc-kboot-root-image|b|P/kboot_all.py:32,62|—|
|P/kboot_run_selftest.py|ppc-kboot-run-selftest|b|P/kboot_all.py:32,62|—|
|P/kboot_trap.py|kboot-trap|b|P/kboot_all.py:32,62|—|
|P/kboot_vectors.py|kboot-vectors|a*|W/ppc-rh-fb.py:23 → P/rh_fb.py:14,20（symbolsのみ）|—|
|P/mmu_run.py|ppc-mmu|c|—|0:QEMU stage C PASS / 0:mutation boot-marker: GREEN/RED/GREEN (build rc=0)|
|P/ppc_sh_output.py|ppc-sh-output|a|W/ppc-sh-output.py:39|—|
|P/sb_run.py|ppc-sb|a*|W/ppc-sc.py:39 → P/sc_run.py:16,146（serial_acceptのみ）|—|
|P/sc_run.py|ppc-sc|a|W/ppc-sc.py:39|—|
|P/sd_run.py|ppc-sd|a|W/ppc-sd.py:69|—|
|P/test_app_link.py|未指定|c|—|0:PPC app link: OK / 0:PPC app link: OK|
|P/test_cli_boot.py|ppc-cli-boot|c|—|0:normal: OK / 0:cat-length: RED|
|P/test_exec_cache.py|ppc-exec-cache|c|—|0:PPC compile, symbols, cache instruction sequence and range loops OK / 0:RED icbi|
|P/test_exec_enter.py|ppc-exec-enter|c|—|0:∅ / 0:BE bit: RED (1)|
|P/test_exec_entry.py|ppc-exec-entry|c|—|0:reached=entry / 0:alignment: RED (1)|
|P/test_exec_publish.py|ppc-exec-publish|c|—|0:PASS: each physical alias and length published after its read / 0:publish omitted: runtime RED|
|P/test_exec_return.py|ppc-exec-return|c|—|0:∅ / 0:resume r3: RED (1)|
|P/test_exec_target.py|未指定|c|—|0:PPC target PASS / 0:mutant RED|
|P/test_hello_build.py|未指定|c|—|0:PPC hello build: OK / 0:PPC hello build: OK|
|P/test_kapi_binding.py|ppc-kapi-binding|b|P/test_kapi_table.py:14,44（generateのみ）|—|
|P/test_kapi_call.py|ppc-kapi-call|c|—|0:SSOT: Successfully generated kapi files. / 0:register offset: RED (1)|
|P/test_kapi_table.py|ppc-kapi-table|c|—|0:slots: 294 wrappers resolved: 293 / 0:const table: RED|
|P/test_lan_p1.py|ppc-lan-p1|c|—|1:      \| ^~~~~~~~~~~~~~ / 1:      \| ^~~~~~~~~~~~~~|
|P/test_lan_p11.py|ppc-lan-p11|c|—|0:PASS ppc-lan-p11 / 0:PASS ppc-lan-p11|
|P/test_lan_p12.py|ppc-lan-p12|c|—|0:PASS ppc-lan-p12 / 0:PASS ppc-lan-p12|
|P/test_lan_p13.py|ppc-lan-p13|c|—|0:PASS ppc-lan-p13 / 0:PASS ppc-lan-p13|
|P/test_lan_p14.py|ppc-lan-p14|c|—|0:PASS ppc-lan-p14 / 0:PASS ppc-lan-p14|
|P/test_lan_p15.py|ppc-lan-p15|c|—|0:PASS ppc-lan-p15 / 0:PASS ppc-lan-p15|
|P/test_lan_p16.py|ppc-lan-p16|c|—|0:PASS baseline / 0:RED crc|
|P/test_lan_p17.py|ppc-lan-p17|c|—|0:PASS ppc-lan-p17 / 0:PASS ppc-lan-p17|
|P/test_lan_p18.py|ppc-lan-p18|c|—|0:PASS ppc-lan-p18 / 0:PASS ppc-lan-p18|
|P/test_lan_p19.py|ppc-lan-p19|c|—|0:PASS ppc-lan-p19 / 0:PASS ppc-lan-p19|
|P/test_lan_p2.py|ppc-lan-p2|c|—|0:BSD definitions matched: 161; constants and errors matched / 0:RED span: reached=GEM_MMIO_SPAN mismatch=GEM_MMIO_SPAN|
|P/test_lan_p20.py|ppc-lan-p20|c|—|0:baseline OK / 0:RED again: reached=full; H:again|
|P/test_lan_p22a.py|lan-p22a|c|—|0:p22a PASS / 0:p22a PASS|
|P/test_lan_p22b.py|lan-p22b|c|—|1:FileNotFoundError: [Errno 2] No such file or directory: '/home/hight/opt/cross/bin/powerpc-elf-gcc' / 1:FileNotFoundError: [Errno 2] No such file or directory: '/home/hight/opt/cross/bin/powerpc-elf-gcc'|
|P/test_lan_p22c.py|lan-p22c|c|—|0:p22c PASS / 0:p22c PASS|
|P/test_lan_p22d.py|lan-p22d|c|—|0:p22d PASS / 0:p22d PASS|
|P/test_lan_p23.py|ppc-lan-p23|a|W/ppc-lan-p23.py:8|—|
|P/test_lan_p24.py|ppc-lan-p24|c|—|0:PASS ppc-lan-p24 / 0:PASS ppc-lan-p24|
|P/test_lan_p24c.py|ppc-lan-p24c|c|—|1:FileNotFoundError: [Errno 2] No such file or directory: '/home/hight/opt/cross/bin/powerpc-elf-gcc' / 1:FileNotFoundError: [Errno 2] No such file or directory: '/home/hight/opt/cross/bin/powerpc-elf-gcc'|
|P/test_lan_p24d.py|ppc-lan-p24d|c|—|0:baseline OK / 0:RED irq: reached=irq H:irq|
|P/test_lan_p29.py|ppc-lan-p29|c|—|0:PASS H29 / 0:PASS H29|
|P/test_lan_p3.py|ppc-lan-p3|c|—|0:GREEN: 26 firmware-tree cases / 0:RED: peer: H:A1 peer, rc=1, reached=A1|
|P/test_lan_p8.py|ppc-lan-p8|c|—|0:control OK / 0:RED id: assertion 16|
|P/test_lan_terminal_host.py|ppc-lan-terminal-host|a|W/ppc-lan-terminal-host.py:8|—|
|P/test_lan_terminal_service.py|ppc-lan-terminal-service|c|—|0:PASS ppc-lan-p24 / 0:PASS ppc-lan-p24|
|P/test_os32x_endian.py|未指定|c|—|0:PPC ELF packaging: OK / 0:PPC ELF packaging: OK|
|P/test_ppc_terminal.py|ppc-terminal|c|—|0:OK normal / 0:RED drop|
|P/test_ppc_terminal_input.py|ppc-terminal-input|c|—|0:OK normal / 0:RED discard|
|P/test_setjmp.py|ppc-setjmp|c|—|0:PASS: PPC jump context / 0:PASS: PPC jump context|
|P/test_sh_build.py|未指定|c|—|1:make: *** [build/ppc-user.mk:51: build/out/ppc-user/sh/shell/rshell_lan.o] Error 1 / 1:make: *** [build/ppc-user.mk:51: build/out/ppc-user/sh/shell/rshell_lan.o] Error 1|
|P/test_shell_putchar.py|ppc-shell-putchar|c|—|0:normal: OK / 0:byte-length: RED|
|P/test_syscall_code.py|ppc-syscall-code|c|—|0:PPC syscall page disassembly OK / 0:RED branch displacement|
|P/test_user_crt.py|ppc-user-crt|c|—|0:PPC USER CRT compile and entry OK / 0:RED argument register clobber (1)|
|P/test_user_trap.py|ppc-user-trap|c|—|0:USER trap PASS / 0:USER trap PASS|
|P/test_user_vectors.py|ppc-user-vectors|c|—|0:∅ / 0:USER late load: RED (1)|
|P/usb_run.py|ppc-usb|a|W/ppc-usb.py:40|—|
|T/ppc_hostfs_io.py|ppc-hostfs-io|a|W/ppc-hostfs-io.py:17|—|
|T/ppc_hostfs_server.py|ppc-hostfs-server|a|W/ppc-hostfs-server.py:9|—|
|T/ppc_sh_boot.py|ppc-sh-boot|a|W/ppc-sh-boot.py:11|—|
|T/ppc_sh_input.py|ppc-sh-input|a|W/ppc-sh-input.py:7|—|
|T/ppc_sh_launch.py|ppc-sh-launch-host|c|—|0:ALL PASS / 0:RED: interposed KAPI detected|
|T/ppc_sh_runtime.py|ppc-sh-runtime|a|W/ppc-sh-runtime.py:9|—|

対象外補足: T/ppc_hostfs_wire.py:13 は `dict(` ではなく旧 `{id,...}` 宣言だが W/ppc-hostfs-wire.py:14 から実行される。T/ppc_sh_string.py はCHECK宣言なし。
P/kboot_all.py:32 は kboot_*.py を動的収集する。bの kboot_hpt は P/kboot_mmu.py:196 からも通常だけ実行。P/compile_ci.py:19 は像を組むだけでこの79本を呼ばない。
CIの直接指定は .github/workflows/check.yml:199–200（ppc-compile/sh-string/sh-runtime）、full-check.yml:92（ppc-compile）。coversに載るだけの試験は実行されない。

**P1判定: 値の更新の漏れ。** P/test_lan_p1.py:61–67 の win=76/sizeof=224 は RC 前の値。`git log -p` とPPC gccの定数出力を照合（証拠: run/ppc-unreg/layout、lan_boot-history.txt、p1-history.txt）。

|履歴|win offset / sizeof|理由|
|---|---|---|
|bc76a404 → f56cfa7d|76/220 → 76/224|override_flagsを末尾追加。試験も220→224へ追従|
|b77a94ad^ → b77a94ad → HEAD|76/224 → 88/236 → 88/236|先頭へ pm_clock_boot（u32×3）を意図的追加|

RCのcommit説明はUniNorth clock gateの接続を明記し、現 platform/powermac/lan_boot.h:84–93 にclockが残る。予期せぬ配置変化の証拠はない。
P1コメント(:23)は「設計から独立コピー」だが、PPC_LAN_DESIGN.md:49–51 の構造体から導く内部配置で、76/224自体を固定する設計ID・外部ABI契約は見つからない。PLN1定数/offsetの§4契約とは区別する。
docs/tasks/ppc/TASK_PPC_LAN.md:59 はP1を未登録・退役予定と明記。値を追従させるか退役するかを登録前に決める（今回は変更しない）。

|案|CI時間の見積り（宣言budgetの和、実時間保証ではない）|toolchainなしcheck-fast / 改修量|
|---|---|---|
|ア:探索へ2パターン追加|全79本13,767秒=229.5分＋budget未指定5本。仮60秒/本なら234.5分。重複11本3,910秒を除く追加分は164.3分＋未指定分（仮169.3分）|53本inputs、5本name/budget欠如、4本非literal、76本run(ctx)なし、11本名前重複を解消。全探索へ加えるだけでは失敗|
|イ:包み登録|同じ試験集合ならアと同時間。cの46本を先行登録なら2,633秒＋未指定5本（仮計48.9分）。bやa*の独立全体は別枠で追加|新規46包みが目安。既存宣言の53本を移行せず、literal covers/予算/入口を包みに集約。重複の二重実行を避けやすい|

両案ともPPC_CROSS_DIRへ統一し、host検査は実行、cross/QEMU/socket部分だけ理由付きSKIP rc=0、OS32_REQUIRE_PPC=1なら不足をFAILにする（PPC_NOTES.md:47–50）。
現ppc-setup(action.yml:2,10–11)はcrossと必須フラグだけでQEMU依存なし。PPC jobへ実行指定とQEMU/OpenBIOS/genisoimage・socket環境を追加し、QEMU/手動枠を分けて予算を実測する。
run.py:748–780は許可欄・必須欄・literal・run(ctx)・名前重複を検査する。needsのmakeはrun(ctx)より前なので、任意toolchainの判定前にビルドさせない設計も必要（run.py:1245–1249）。
**推奨はイ。** まずcを小分けで包みに登録し、P1退役判断・環境の不統一・既知失敗を解消。kboot_allとa*の独立試験は別のPPC実行枠へ。アの一括探索は既存包みとの重複と宣言移行の影響が大きい。

検証・留保: preflightの基点/toolchain/runner/fontsはOK、vmkernel.lz4/kernel.mapは欠如NG。調査専用・書込み範囲制限によりmake all/全体check類は未実行。コミットは.git読取り専用のためPMへ委ねる。
cの46本×通常/変異=92回を完走、84回rc=0、8回rc=1、計270.69秒。timeout/SKIPなし。lintは `python3 -B tools/checks/run.py --lint` rc=0（lint.logは空）。
失敗4本: P1は通常対照でassert失敗のため変異未到達。sh_buildは自身のcopy対象(:42–44)にnetがなく、remote_control.h:5の../net/host_mailbox.h欠如（通常/変異とも）。
P22b(:12)・P24c(:18)は指定CROSS_DIRをPPCとして解釈して失敗。補足実行のみCROSS_DIR=cross-ppcへ変更し、通常/変異とも0。末行はP22b=p22b PASS / p22b PASS、P24c=host PCI cases OK / RED status（supplement.json）。
AF_INET/AF_PACKETを要する未実行部分はPMが回す（例: aのP/test_lan_p23.py:192–195）。cには実socketが必須の試験なし。開始時loadは2.54、他のqemu-systemプロセスなし。各QEMU起動時のload再確認は未記録。
