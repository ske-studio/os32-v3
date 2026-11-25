> 出所: ~/os32-tmp/run/ppc-align-b10/DESIGN.final.md (設計ラリー 3 回、astra 起草・sol レビュー、2026-11-22 確定)。
> 状態: **確定 (2026-11-22)** — 票 TASK_PPC_ALIGN_X86 の B10 を x86 に寄せる。末尾の「確定」の節は本文に優先する。

# B10　r2→r3 の変更

- P1-1　段2の ppc-reset は CUI READY と VIA 成功を待ち、QMP で OF 切離しを確認する。
- P1-2　段2の reporter 選択に既存の配置診断フラグを加え、配置検査と無指定製品の成功行不在をそれぞれ検証する。
- P2-1　段2で rate_test の抽出終端を時計報告の呼出しに移し、正常系とレート変異の終了値を照合する。

## 1　58a8015f5 の実装と x86 との対照

以下 PM は platform/powermac。照合元は作業木の HEAD ではなく origin/main。段 E で更新する設計前提は三つ。①無指定は製品 link-sh、②旧 shell/sh は同じ製品への互換指定、③製品の操作・出力の基準は本線の共通 shell_loop と外部 /sys/shell.bin。別枝の shell を合成する作業は不要。root と IDE の合流状況は別途 §3 に記す。

|役目|PC-98|PowerMac|
|---|---|---|
|書式|sys/kern/kprintf.c の console_printf と console_vprintf に kprintf と kvprintf を alias|同ファイルと PM/log_printf.c が sys/kern/kprintf_format.inc を使う。exec 像は PM/console_log.c の pm_diag_write から pm_log_write、core/link は別名から PM/kboot_console.c の console_write|
|保存・配送|sys/kern/console/console.c の console_write、sys/kern/init/bootlog.c の bootlog_push、sys/platform/pc98/kernel.c の platform_boot_services で bootlog_save|PM/log.c の pm_log_write が登録順に ESCC と PM/lan_log.c のリングへ配る。PM/lan_service.c の pm_lan_poll が非 link の PLN1、link の前景サービスが PM/link_log.c の pm_link_log_poll を駆動。送出済みスロットは再利用する|
|起動と端末|sys/platform/pc98/kernel.c の platform_boot_machine/services/run と pc98_shell_failure|PM/ppc_kernel.c の同じ三局面。root_ready 後 pm_exec_boot、shell_loop_run。PM/shell_loop_ops.c の loading/result は kprintf、failure は shell_print と idle_wait_step|

装置差は契約 §9 の範囲。診断では FB に描画しない。共通入力・端末の方針、x86 boot.log の寿命、kdebug は今回の変更対象外。

**非常時の実経路。** OF が生きている間は PM/kernel_boot.c の kb_stop/kb_reserve_stop が ofw_write 後 ppc_stop、OF 確立前の失敗は直接停止。pm_kernel_takeover 後の ppc_kernel_real_start、ページ表構築・検証の失敗は ppc_stop/ppc_boot_walk_stop。MMU 切替中に通常 sink や OF を呼ばない。platform_boot_machine は pm_rh_bind/stage 後に pm_log_escc_init、失敗は pm_rh_stage(PM_RH_ESCC_INIT) と arch_cpu_stop。未登録時の pm_log_write は slot 0 の dropped を加算する。登録後の ppc_trap_fatal_sink は通常の kprintf。pm_log_fatal_hex は DR=1 で有効な I/G alias を渡す独立の非常用関数で、本線の trap からの呼出しはない。

## 2　選択と起動順

段1では全 PPC 像に同じ診断書式を提供する。sys/kern/kprintf.c の末尾に固定名 platform_kprintf.h を include し、PC-98 版に現在の alias 宣言二つをそのまま置く。PPC 版は既存の公開宣言に従い別名を定義しない。両 platform_console.h の PLATFORM_KPRINTF_IS_CONSOLE と sys/include/kprintf.h の対応説明を整理する。端末の書式実体は共通のまま。

build/platform/powermac.mk では EXEC リストから console_log.c と log_printf.c を除き、CORE 定義の直後で次の一行を加える。

    C_PLATFORM_CORE += sys/platform/powermac/log_printf.c

PM/log_printf.c は kprintf.h、stdarg.h、kboot_log.h、kstring.h を include。static void pm_diag_write(const char *bytes,u32 n,u8 color) は色を捨て pm_log_write(bytes,n)。static void pm_diag_print(const char *s,u8 color) は kstrlen(s) を渡す。KPRINTF_PRINT と KPRINTF_PRINT_UTF8 は共に後者、KPRINTF_WRITE は前者。既存 VNAME/NAME で "sys/kern/kprintf_format.inc" を一回展開し console_log.[ch] を除く。試験の include path にリポジトリルートも加える。

段2のビルド表。各行の命令は `make ARCH=ppc PLATFORM=powermac` に右欄を付ける。sys/conf/images/ppc-flags.mk が決めた最終 IMAGE と既存の KBOOT_SHELL_LAYOUT_DIAG から powermac.mk で reporter を一つだけ選ぶ。

|呼び方|実体・観測|追加引数|
|---|---|---|
|製品／旧 link-sh|link-sh・製品|なし／IMAGE=kboot-link-sh|
|配置診断|link-sh・旧診断|IMAGE=kboot-link-sh KBOOT_SHELL_LAYOUT_DIAG=1|
|旧 shell|link-sh・製品|IMAGE=kboot-shell|
|旧 sh|link-sh・製品|IMAGE=kboot-sh|
|旧 core|core・旧診断|IMAGE=kboot-core|
|旧 link|link・旧診断|IMAGE=kboot-link|
|負例各一像|core の neg1 / neg2・旧診断|IMAGE=kboot-core NEGATIVE=1 または 2|

|受入台本|段1|段2（reporter 移動と同時）|
|---|---|---|
|link_sh_run.py:accept_reset|現行の CUI・VIA・OF-OFF 待ち|製品で CUI・VIA を待ち、再起動前に QMP で切離しを読む|
|ppc_shell_layout.py:observe / verify_handoff|現行の配置診断像で順序と領域を検査|同じフラグ付き像に旧診断をリンクし、両検査を継続|
|kboot_clock.py:rate_test|現行の CLOCK 書式呼出し直前まで抽出|同じ検証ブロックを pm_report_clock 呼出し直前まで抽出|

互換名のための診断専用製品コピーは作らない。段1は上表全経路で既存出力を保つ。段2では PM/kboot_report.h に局面別関数を宣言し、旧診断の行（core/link、負例、配置診断）で成功行を整形する。製品実装は成功時に無出力。失敗と副作用は呼出し側が所有する。

    machine: ESCC → stream（exec）→ LAN sink → OF/MMU 観測 → GMAC quiesce
             → memory_boot_init → shm/trampoline/heap → MEMORY 往復検査
             → 負例入口 → DSI/ISI/guard/flush → DEC/VIA/MPIC 初期化
    services（両像）: 自己 IPI 2 回 → MPIC 結果 → tick/DEC/時計検証 → CLOCK 観測
             → PCI 列挙 → boot token → LAN start または LAN OFF
             → start=tb_now(), tick0=tick_count → ready(start)
             → do { now=tb_now(); pm_lan_poll(now); } while (now-start < 2*tb_hz)
             → alive(now,start,tick0) → exec 像の root 処理
    run: root_ready なら exec と共通 shell_loop、それ以外は LAN poll を継続

旧 ready は kboot_ready_tb=start を公開してから READY、旧 alive は tick 差、最終 now の TB 差の µs、OF 呼出し差を ALIVE に出す。製品 ready/alive は空で、同じ位置から同じ時間だけ初回 LAN 配送を駆動する。待ち時間の短縮は B10 の効果に含めない。MEMORY の online/heap/shm、TRAP の四結果、MPIC の delivery は両像で実行し、不成功時は現行の値入り行（0/bad）を一回出して続行。旧像は成功時も同形式。memory_boot_init、trampoline、clock_init、dec_start、mpic_init、no tick、dec_rate、clock_read/rate 等の既存 kboot_stop は維持する。

## 3　合流と変更位置

基点 58a8015f5 は PM/ppc_kernel.c:platform_boot_services に pm_root_image/boot/release と pm_loop_boot/pm_ide_boot がある。旧並行先 wt/ppc-root-lz4-3 (d72920cb0) と wt/ppc-ata-p1b (f5e187e6b) のファイルを再適用しない。順序は統合済み root・IDE → B10 段1の CORE と profile 更新 → 段2の reporter 選択。PCI 列挙後、LAN 窓終了後に既存の root 取得、取得成功時だけ登録、成功・失敗の両方で登録状態に応じて release、解放失敗は停止。root 成功時だけ loop → IDE → console → hostfs/USB/work/input → root_ready。root の rc・所有ページ・空きページの検証は root 担当のまま。

|担当|共有位置と取り込み時の確認|
|---|---|
|B10|machine/services の結果報告、powermac.mk の診断提供元だけ。fixture build_profile_ppc.json は正規化後の実際の順序、root/IDE の所属と一意性まで照合|
|B04（変更予定位置）|PM/lan_service.c の PLN1/link poll、services の起動、run/idle の配送と rshell。初回の poll と以後の前景実行を両方式で確認|
|B06（変更予定位置）|platform_console.h と console_stream/console_text の端末方針。B10 は診断選択だけを外し、UTF-8、鏡、FB/RSH の出力先を端末試験で確認|

## 4　段別の合格条件

**段1。** 全指定と負例を明示リンクする。ppc-ci-image の IMAGES 四組には製品へ正規化する重複があり、負例は含まれない。旧診断の行順・値・CRLF、製品の従来出力を対応する受入で比較する。ppc-kboot-console の UNITS に log_printf.c、covers に同ファイル・雛形・固定名ヘッダを加え、kboot_console.c/log.c/escc.c/lib/kprintf.c/lib/kstring_c.c を残す。cross_audit はリンク検査として残し、同 CHECK の書式用入口を qemu-i386 と qemu-ppc の静的 ILP32 ELF で実行する。PPC 用 _start は Linux/PPC exit syscall と記録 sink を持ち、実 MMIO をスタブ化。幅・符号・文字列ポインタ・UTF-8・LF・レジスタ引数を超える列を可変長入口と u32 配列入口で同じ期待バイトに照合する。

ppc-stream-host/test_ppc_stream.py と test_ppc_link_sh.py の合成 TU は移動先を使い、試験 shim に実ヘッダ・雛形と書式 helper を供給して二重定義を避ける。ppc-usb の CHECK と usb_run.py の covers も移す。fb_run.py の trace-screen は現在 console_text.c の「FB text ready」にあり維持する。kboot_console.py の旧端末 write-length/string-end/lf-expansion/screen-success は端末実体で実行し、診断 adapter の誤長・端末への誤配は別に記録バイトで RED とする。

pm_log_write の未登録 n byte は dropped +n、failures 不変。二 sink の片方が失敗すればその slot の failures +1、dropped +n（u32 飽和）、他方は全 n byte。先頭 sink 失敗後に return する変異は後方の記録欠落で RED。ppc-link-log の満杯試験では ESCC 全量、LAN の破棄区間で drop_records/drop_bytes 増、LAN adapter 自体は成功を返すため slot failures/dropped は不変。前景 complete(LAN_OK) で空きを作り次の記録が届くこと、再送待ち中に書込が送信を呼ばないことを確認する。

**段2。** core/link の kboot_run は OF-CAPTURE → OF-OFF → MMU-D → MEMORY → TRAP → MPIC → CLOCK → READY → ALIVE、既存の値域、QMP の kboot_ready_tb、2 秒以上、OF 差 0 を照合。READY 欠落変異は順序違反で RED。負例は MEMORY 後それぞれ次の一行だけを FATAL として出し、ppc_trap_stop の PC を連続二回観測、READY に進まない。

    FATAL trap vector=0x300 reason=0x2 addr=0x80000
    FATAL trap vector=0xc00 reason=0x108 addr=0x0

ppc-reset の link_sh_run.py:accept_reset は製品のまま、最初の待ちを s.ready()、mode が cui、対象 VIA の rc=ok にする。OF-OFF の文字列待ちとその行の表示を外し、reboot 提出前に既存の QMP 読取り s.word('detached') で ofw.c の値 1 を要求する。Cuda・PMU とも guest RESET、新 boot ID の CUI READY、旧要求の退役、echo まで確認する。既存変異は reboot 実行後の判定まで進める。

ppc_shell_layout.py:observe はフラグ付き link-sh で SHELL-LAYOUT を待ち、その行より前の列を verify_handoff に渡す。MMU-D の HPT 範囲と先行する OF-OFF を従来どおり要求する。フラグ無指定の製品は次段落の ESCC・LOG 不在検査で対にする。

kboot_clock.py:rate_test は ppc_kernel.c の既存の u32 want 宣言を始点とし、同じ関数内の新しい pm_report_clock 呼出しの行頭を排他的終端とする。時計検証を呼出し側に残し、正常系の終了値 0 と --mutate で既存の上下限比較を無効化した場合の終了値 2 を要求する。

製品 ppc_sh_output.py:link_accept は RSH READY 後 /bin/outprobe.bin の完全な出力と echo alive の返答を照合。既存の「Loading /sys/shell.bin」を ESCC と再構成した LOG の双方に要求し、OUT/FB への混入を拒む。LOG 世代・連番・無損失は通常負荷で照合。圧力試験は drop を許し命令完了を要求する。製品の ESCC と LOG に上記九つの成功行が一つでも出れば RED。製品への READY 混入変異はコマンド完了後この判定で落とす。RSH 接続や link プロトコルの ALIVE は引き続き検査する。

kboot_run.py:validate_root は現在 validate の全 milestone を前置条件にする。製品用には ROOT_PATTERNS の rc/登録・ページ収支と既存障害検出を独立させ、ppc_sh_output/fb_run/sb_run/sc_run/sd_run/shell_run/usb_run の製品待機を更新する。旧 core/link の validate は保持。link_run.py:log_ready も対象 ELF で選び、旧 link は OF-CAPTURE から LINK HEAP、製品で使う場合は既存 LOG イベントとプロトコル完了を確認する。新しい受入用メッセージを製品に追加しない。

**x86 と着地。** 各段の共通基点のクリーンな二つの出力で、片方だけに当該 B10 差分を適用。tools/build_compare.py と同じ OS32_BUILD_COMMIT=layout-compare、SOURCE_DATE_EPOCH=946684800、同一ツールと設定で make kernel。kernel.elf、kernel.bin、vmkernel.lz4、同時生成の全 .mod（SQLite を含む）、kdebug.bin および生成対象の r1 対応物を相対パスで突き合わせ、集合差と cmp 差をともに不合格とする。nm で kprintf=console_printf、kvprintf=console_vprintf を両側で確認。差があれば対象物と差のあるシンボルを記録し着地前に解決する。画面・boot.log の比較は NP21/W。各段の最終編集後に対象 build、変更 CHECK の --mutate、checks lint。変異は所定の実行時観測のみ RED。今回は設計のみで、ビルド・実行・実機確認は未実施。

反論なし。


## 確定 (2026-11-22、sol 第 3 回「条件付きで可」、PM が反映)
- P2・§2: 旧 shell・旧 sh の行の「同上」を「link-sh・製品」と明記した (旗の無指定は製品の reporter を選ぶ規則と揃える)。
