# 未登録 PPC 試験の期待の出所案（ef701722、2026-11-14）

対象は FINDINGS の c 全46本。製品・試験・CHECK は変更しない。これは登録時の案で、合格報告ではない。
略記: P=`tools/ppc/`、T=`tools/tests/`。根拠の L/M/S/E/V/K/C は下記の現行文書（file:line の file 部）を表す。
L=`docs/decisions/ppc/PPC_LAN_DESIGN.md`、M=`docs/decisions/ppc/PPC_MMU_DESIGN.md`。
S=`docs/decisions/ppc/PPC_SHELL_DESIGN.md`、E=`docs/decisions/ppc/PPC_SHELL_EXEC_DESIGN.md`。
V=`docs/decisions/ppc/PPC_LAN_SERVICE_DESIGN.md`、K=`docs/decisions/ppc/PPC_LAN_MARKS_DESIGN.md`、C=`docs/decisions/ppc/PPC_SHELL_SC_DESIGN.md`。
`basis=L.P11` 等は **PPC_LAN_DESIGN.P11** 等への展開案（新lintのID構文未確定、実在段/節への参照）。§は同文書の節。
budget は秒。既存宣言を引継ぎ、未指定5本は FINDINGS:106 の仮値60秒（*）。実測保証ではない。
包み名は `tools/checks/<名前>.py` の CHECK.name 案。P1は退役候補のため登録しない。†=確定設計に反する期待の固定。

|本|出所|根拠 (file:line)|包みの名前の案|budget|
|---|---|---|---|---:|
|P/mmu_run.py|basis=M§7|M:256（takeover・HPT・DEC）|ppc-mmu|600|
|P/test_app_link.py|basis=E§8|E:254（base/配置/guard/libgcc拒否）|ppc-app-link|60*|
|P/test_cli_boot.py|basis=S§7|S:199（echo/cat/redirect）|ppc-cli-boot|60|
|P/test_exec_cache.py|basis=E§6|E:209（PA alias・cache公開順）|ppc-exec-cache|60|
|P/test_exec_enter.py|basis=E§5|E:141（MSR・SRR・LR）|ppc-exec-enter|3|
|P/test_exec_entry.py|basis=E§3|E:68（整列・4引数・store前commit禁止）|ppc-exec-entry|3|
|P/test_exec_publish.py|basis=E§6|E:209（load後の各PA/長さ）|ppc-exec-publish|1|
|P/test_exec_return.py|basis=E§5|E:161（SC復帰・親root/owner/SP）|ppc-exec-return|3|
|P/test_exec_target.py|basis=E§6|E:185（PPC flag必須）|ppc-exec-target|60*|
|P/test_hello_build.py|basis=E§8|E:254（SDK包装・guard・hello）|ppc-hello-build|60*|
|P/test_kapi_call.py|basis=E§4|E:103（r0/r3–r10・型付きadapter）|ppc-kapi-call|3|
|P/test_kapi_table.py|basis=E§7/F15|E:224、E:318（kernel可書込・全slot/metadata）|ppc-kapi-table|10|
|P/test_lan_p1.py|kind=characterization（配置部分）、wireはbasis=L§4|tools/ppc/test_lan_p1.py:63（内部配置）；L:101（wireは別契約）|退役（登録なし）|0（旧60）|
|P/test_lan_p11.py|basis=L.P11|L:178（P11/H11）；L:231（受入と変異）|ppc-lan-p11|60|
|P/test_lan_p12.py|basis=L.P12|L:180（P12/H12）；L:231（受入と変異）|ppc-lan-p12|60|
|P/test_lan_p13.py|basis=L.P13|L:182（P13/H13）；L:231（受入と変異）|ppc-lan-p13|60|
|P/test_lan_p14.py|basis=L.P14|L:184（P14/H14）；L:231（受入と変異）|ppc-lan-p14|60|
|P/test_lan_p15.py|basis=L.P15|L:186（P15/H15）；L:231（受入と変異）|ppc-lan-p15|60|
|P/test_lan_p16.py|basis=L.P16|L:188（P16/H16）；L:231（受入と変異）|ppc-lan-p16|60|
|P/test_lan_p17.py|basis=L.P17|L:190（P17/H17）；L:231（受入と変異）|ppc-lan-p17|60|
|P/test_lan_p18.py|basis=L.P18|L:192（P18/H18）；L:231（受入と変異）|ppc-lan-p18|60|
|P/test_lan_p19.py|basis=L.P19|L:194（P19/H19）；L:231（受入と変異）|ppc-lan-p19|60|
|P/test_lan_p2.py|basis=L.P2|L:164（P2/H2）；L:231（受入と変異）|ppc-lan-p2|60|
|P/test_lan_p20.py|basis=L.P20|L:196（P20/H20）；L:231（受入と変異）|ppc-lan-p20|60|
|P/test_lan_p22a.py|basis=L.P22a|L:200（P22a/H22a）；L:231（受入と変異）|lan-p22a|60|
|P/test_lan_p22b.py|basis=L.P22b|L:202（P22b/H22b）；L:231（受入と変異）|lan-p22b|60|
|P/test_lan_p22c.py|basis=L.P22c|L:204（P22c/H22c）；L:231（受入と変異）|lan-p22c|60|
|P/test_lan_p22d.py|basis=L.P22d|L:206（P22d/H22d）；L:231（受入と変異）|lan-p22d|120|
|P/test_lan_p24.py|basis=V§2/§5|V:85、V:129（状態/応答枠/予算/再接続）|ppc-lan-p24|60|
|P/test_lan_p24c.py|basis=V§2|V:54、V:62（MMIO endian/TB/master readback）|ppc-lan-p24c|60|
|P/test_lan_p24d.py|basis=V§2|V:82、V:86（STATUS/MEMORY/READ_DIAG・RX除外）|ppc-lan-p24d|60|
|P/test_lan_p29.py|basis=K.P29/H29|K:71（保存/診断/無効像の誤受理拒否）|ppc-lan-p29|60|
|P/test_lan_p3.py|basis=L.P3|L:166（P3/H3）；L:231（受入と変異）|ppc-lan-p3|60|
|P/test_lan_p8.py|basis=L.P8|L:172（P8/H8）；L:231（受入と変異）|ppc-lan-p8|60|
|P/test_lan_terminal_service.py|basis=S§3|S:103、S:104（OUT/STATEと診断の共存）；S:339|ppc-lan-terminal-service|60|
|P/test_os32x_endian.py|basis=E§6|E:185、E:204（wire LE/ELF BE・payload不変）|ppc-os32x-endian|60*|
|P/test_ppc_terminal.py|basis=S§3|S:64（nonce/seq/drop・FIFO・codec）|ppc-terminal|60|
|P/test_ppc_terminal_input.py|basis=S§3/§4|S:95、S:121（穴/満杯後LFまで破棄）|ppc-terminal-input|60|
|P/test_setjmp.py|basis=E§5|E:180（25語・深さ保存/復元）|ppc-setjmp|10|
|P/test_sh_build.py|basis=C.B01|C:227（guard付きsh像）；E:254|ppc-sh-build|60*|
|P/test_shell_putchar.py|basis=E§4|E:133（1byte出力・LAN協調進行）|ppc-shell-putchar|60|
|P/test_syscall_code.py|basis=E§4|E:113（8B stub/共通SC/LS NOSYS/scratch）|ppc-syscall-code|60|
|P/test_user_crt.py|basis=E§3/§8|E:97、E:237（linkage frame・main・入口）|ppc-user-crt|60|
|P/test_user_trap.py|basis=E§5|E:161（SC/USER fault/kernel fatal/IRQ）|ppc-user-trap|120|
|P/test_user_vectors.py|basis=E§5|E:154（USER stack/非busy・SRR後load禁止）|ppc-user-vectors|10|
|T/ppc_sh_launch.py|basis=C.S02/T05|C:125、C:130、C:233（直後結果/139/拒否共有）|ppc-sh-launch-host|10|

P14は後の半reset期限切れ継続（L:318）、P17はANLPAR selector不問（L:320）、P12はI/G descriptor同期（L:321）も根拠に含める。古い本文より取り込み後の決定を優先する。

## P1 の判定：退役案
`pm_gmac_boot` のネイティブ配置を外部と共有する経路は見つからない。OFから受け取るのはプロパティのbyte列で、`platform/powermac/gmac_tree.c:126` が解釈して構築する。
`platform/powermac/kboot_info.h:37` に埋め込み、`platform/powermac/kboot_capture.c:199` で採取し、`arch/ppc/kernel_boot.c:344` からtakeoverへポインタで渡す経路はある。
ただし `arch/ppc/kernel_takeover.S:17` の分岐先も同一ELF内。`build/ppc-kboot.mk:63`・`:71` の入口/採取/利用者を、`:216` の一つのリンク規則で組む。別ローダ/別像との構造体ABIではない。
利用者は `arch/ppc/kernel_boot.c:229`、`init/ppc_kernel.c:73`、DMA/PCI/clock/service/diag、`platform/powermac/boot_token_powermac.c:10` の同じヘッダを使うCコード。OFへ構造体自体を渡す呼出しや、ホストがwin/sizeofをoffsetで読む処理は検索範囲（arch/platform/init/build/tools）で見つからない。
PLN1は `platform/powermac/lan_boot.h:1` の通りbyteで符号化し、V:90もstruct castを禁止する。外部wire契約と内部配置を混同しない。
従って76/224→88/236の追従で内部配置を契約化せず、P1を退役する。既存の退役予定とも整合（`docs/tasks/ppc/TASK_PPC_LAN.md:59`）。
代替は `test_lan_p3.py`（OF解析・boot値）、`kboot_capture.py`（gmac snapshot/rc）、`test_lan_p8.py`（PCI）、`test_lan_p12.py`（DMA）、`test_lan_p13.py`（clock）、`test_lan_p24c.py`・`test_lan_p24.py`（adapter/service）。
P1に混在するwire値は `test_lan_p21.py`（codec）、`test_lan_p23.py`（host）、`test_lan_p24.py`（診断往復）へ任せる。P21等は今回のc集合外で、登録経路・定数/異常値の網羅は退役実施時に照合する。

## 欠陥固定・登録時の留保
46本の判定式に「EE=0 syscall中はtick停止が正しい」を要求するものは見つからず、†該当は0本。`test_user_trap_host.c:7` のMSR=0x30は入口のfakeで、dispatch中EE/tickの期待を検査していない。
ただし新契約 **AC-CTX-1/6/7**（`docs/decisions/ARCH_CTX_DESIGN.md:44`・`:49`・`:50`）への適合証明でもない。`test_user_trap.py`・`test_user_vectors.py` はW4f（同`:114`）で入口許可/出口禁止/IRQ一段の試験追従が必要。MMU試験の起動/例外処理中EE=0は通常USER syscallとは区別する。
P1の76/224は期待値の陳腐化であり、製品の欠陥を固定する†とは別。内部の命令列・配置だけの追加条件は設計の主たる期待と区別し、契約IDを新造しない。
総予算は元の2,633＋5×60=2,933秒（48.9分）、P1退役後2,873秒（47.9分）・45包み。未指定5本はapp_link/exec_target/hello_build/os32x_endian/sh_build。
FINDINGSの既知失敗（P22b/P24cのcross指定、sh_buildのnetコピー欠如）は別途解消して登録する。新lintへの正式ID割当と任意toolchainのSKIP/必須時FAILも登録段の仕事。
検証は読取りと46行/予算/参照行の照合のみ。依頼の書込み限定・読取り専用調査に従い、preflight --fix-fonts、build、試験、変異、lintは未実行。.gitは環境で読取り専用のためcommitはPMへ委ねる。
