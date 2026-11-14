# PowerPC P3 詳細設計 r2 — SH の共通 rshell を link に結ぶ

## r1→r2 の変更
- ユーザー決定: 案 B（共通 SH）を採用済みとし、KBOOT_SHELL は PLN1 の退避像に残す。
- R1: EE の窓・協調窓の TB 期限・busy を削除し、EE を変えない有限の前景処理にする。
- R2: 共通 kapi_host_notice.c は無変更、PLN1 の mailbox は platform の NOSYS/0 stub にする。
- R3: PPC の halt/yield を共通 endpoint 1 か所へ結び、notice/status/入出力から進行処理を除く。
- R4: P3 は LAN と USB local に限定、ESCC の KAPI と調停を外す。
- R5: rshell_lan.c に rsh_lan_app_loop を置き、USB 行・LAN poll・exit の契約を確定する。
- R6: link-SH は正常終了・fault のいずれも回収後に再起動する。
- R7: 組める単位の 6 段に統合し、既存 test_kapi_binding.py・test_remote_x9.py を第 2 段に含める。
- R8: B2/B3 で専用 sh.bin の出力先と ROOT_BINS を同時に切り替える。
- R9: ZERO_SLOTS の host_notice_serial と生成 include の除外解除を H8/H9 に補う。
- 本線 PM の条件 1: P3 は io_pump の口を作らず使わない。将来の EE=0 内待機は N4 の共通口を使う。
- 本線 PM の条件 3 (PM が反映): 前景の仕事は idle_work に登録し、endpoint は idle_work_run() を呼ぶだけ。EE=0 の syscall との約束の食い違いを §3 の未決に書いた。
- 本線 PM の条件 2: APP 処理を rshell_lan.c に集約し、rshell.c/main.c は呼出し 1〜2 個、x86 build_compare は MATCH を目標にする。
- PM 決定: ESCC と EE の窓を外し、実装段を 6 つとする。

## 1. 実物の確認・統合判断
- 対象 HEAD は `4e0e8b1e`。作業木は読み取りのみ。以下の行番号はこの版。設計のみで製品変更・ビルド・実行受入はしていない。
- 指定箇所から生成器・SH profile・起動口への参照だけを grep で追跡した。生成済み KAPI の表の存在と provider の存在を区別する。
- 読み取り限定を優先し、ファイルをコピーする `preflight --fix-fonts` は実行しない。実装時は指定環境で preflight を行う。

|食い違い ID|前提・見込みと実物（根拠）|必要な修正|
|---|---|---|
|E1|確定設計 §5:147・§6:190–192 は resident CLI。`build/ppc-kboot.mk:101` の KBOOT_SHELL と SH は別物|P3 の対象を SH に改訂|
|E2|SH に全ソースは入る (`build/ppc-user.mk:39`) が `userland/ppc/sh/profile.h:4` は SHELL_AS_APP。`userland/shell/main.c:128` は rshell 登録を除外、`rshell.c:364,478,610` は LAN 実行・poll・コマンド入口を除外|単なる KAPI 結線では不可。共通 rshell のアプリ対応が必要|
|E3|`exec/exec_kapi_init.inc:508,559` は代入だけ。PPC の host provider は `sdk/kapi_target_ppc.py:156,251` の absent、notice_serial は `:266` の ZERO_SLOTS、include は `sdk/gen_kapi.py:520` の除外対象|slot・ZERO_SLOTS・include とリンク対象を一緒に直す|
|E4|`rshell.c:445,475,496` は serial 初期化・ser_wd・rsh_serial_follow に依存。sys_halt は `sdk/kapi_target_ppc.py:7` の void 無処理、sys_yield は `sdk/kapi.json:1513` から exec_sys_yield。`arch/ppc/exec_kapi.c:1–7` は表だけで待機 endpoint は無い|APP 入口を別に定義し、PPC halt/yield を同じ新 endpoint に結ぶ|
|E5|SH+LINK は error (`build/ppc-kboot.mk:17`)。出力先は LINK 優先で `/link` になり診断像と衝突 (`:37`)|組合せを許可し `/link/sh` を分離|
|E6|SH は旧 HostFS も組む (`build/ppc-kboot.mk:137`)。`hostfs_peer.c:14` は共通 mailbox と同名 host_alive_snapshot、`lan_service.c:98,108` の PLN1 FS 口は link で消える|P3 link-SH は旧 HostFS を組まない。SFS on link は P4|
|E7|`shell_boot.c:33` は TERM_OPEN/USB 打鍵待ち。`input_boot.c:21,31` と `console_stream.c:11` は link 像から外される terminal TU に依存|起動待ち・poll・入力・出力を PLN1 から切り離す|
|E8|`arch/ppc/exec_enter.S:25` は USER を EE=1 で起動し、USER 復帰ごとに DEC が進む。不足は `terminal_input.c:73–78` の USB poll と `lan_service.c:291–299` の bind/mailbox 初期化を含む前景処理|EE の窓は不要。前景処理だけを待機 endpoint から 1 回進める|
|E9|`arch/ppc/exec_reclaim.c:4` の host owner 回収は空|owner 終了・異常終了で handle 回収を結線|
- 食い違いは上表の **9 件**。capture/token は不足に数えない。両者は absent 群に含まれず、生成器の通常経路で実体を呼ぶ (`exec_kapi_init.inc:592`、`con_sink.c:151`、`sdk/kapi.json:2040`)。
- POLICY_DEV §1 の判断①：本線に同じ役目がある。確定設計 §2・§3 と `rshell_lan.c:291` の READY/通知/実行/結果/END を共用する。
- 判断②：**HAL で一実装にする**。wire・ID・世代・capture は共通、PPC は platform_host、有限の前景進行、USB だけを提供する。rshell_link.c は作らない。
- 判断③：link v2・共通 100Hz・一 RX 所有者・共通 exec に合う。ただし「resident CLI を維持する」確定範囲とは異なるため、§7 の判断後に正典を更新して実装する。
- 互換契約は AC-TIME-3、AC-SYNC-2、AC-NET-3、AC-INPUT-1。D03（進行）/D14（close 保留 ACK）の論点は AC-NET-3 に集約されている (`ARCH_COMPAT_CONTRACT.md:108,111`)。x86/PPC に同じ世代・ID・capture・owner 回収試験を当てる。

## 2. 案の比較と決定
|案|利点|不足・費用|判定|
|---|---|---|---|
|A：rshell_link.c + resident CLI|確定 P3a–c に沿う。小さい診断的 CLI|製品 SH の問題を解決せず、操作状態・結果 capture を二重実装|採らない|
|B：SH + link + 共通 rshell|製品側の解釈器、ID 管理、結果送信を一本化|アプリ対応、KAPI provider、待機・入力の整備が必要|**採用済み**|
|C：SH の fd0 に通知文字列を注入|既存 readline へ入れられる|完全行境界・ID・rc・capture 寿命を別管理し結局 adapter が増える|採らない|
- PM の見込みは統合先として正しいが「そのまま動く」は否。特に SHELL_AS_APP を丸ごと解除して resident 用 SFS/GUI/実行方式を持ち込まない。
- KBOOT_SHELL は PLN1 の比較・退避像として保存し、KBOOT_SHELL+KBOOT_LINK は引き続き拒否。P3 では常駐 CLI の link 端末を作らない。
- PLN1 の既定・既存 SH は保持。P3 選択像だけ link-SH とし、/host は未提供、GUI は未提供。P4/P5 の完了を既定切替の条件として維持する。

## 3. 部品の境目（断片は提案。生成物は手で編集しない）
### 共通 shell と起動
- SHELL_AS_APP を維持し、link-SH 専用 profile だけ SHELL_REMOTE_APP を選ぶ。専用 sh.bin/root と `--rshell-lan` を既存 exec 引数経路で渡す。
- `userland/shell/rshell_lan.c:291` の poll の後に `int rsh_lan_app_loop(void)`、`:66` の trace の側に `void rsh_lan_app_init(void)` を追加し、宣言は `shell.h:192–198` の側へ置く。後者は APP 専用 rshell/lantrace の登録、前者は待受全体を所有する。
- `main.c:128–144` の APP 分岐は init と `--rshell-lan` 時の `return rsh_lan_app_loop()` の 2 呼出しに集約。`rshell.c:438–603` の resident ループは変えず、APP 用コマンド本体・登録も rshell_lan.c に置く。関数途中へ条件分岐を散らさず、APP 実装は機能ガードでまとまりを囲む。
- `rshell_lan.c:331,356` の execute/handoff は APP 用の同名実体を同 TU に置く（resident の `rshell.c:397,432` は保持）。APP execute は UART・GUI/SFS 特別操作を未提供とし、通常 execute_command を呼び、handoff=0。APP の handoff は未提供を返す。wire・ID・世代・capture の処理は共用する。
- app_loop は rsh_lan_reset（`:224`）後に USB local のみを読む。`kbd_trygetchar_local`（既存例 `rshell.c:72`）と `rsh_line_begin/feed`（`serial_watchdog.h:119–122`、from_serial=0）で 126B 制限の行を組む。ser_wd・rsh_serial_follow・serial 初期化には触れない。
- 行頭は USB の文字を先に確認し、半行が無いときだけ rsh_lan_poll（`:291`）を 1 回呼ぶ。半行中は LAN を保留し、USB 完成行を execute_command へ渡す。無入力時は sys_halt、LAN 内の AGAIN 待ちは既存 wait_again（`:88–94`）の sys_yield を使い、毎回 USER へ戻る。
- LAN の RSH_LAN_EXIT（`:371`、結果送信試行と capture END 後）なら app_loop は 0 を返す。USB の `exit N` は sh_exit_flag/code（`main.c:25–26`）を execute_command 復帰直後に確認して N を返す。行編集の EXIT は 0。退出時は reset と APP が保存した flag の復元を行う。通信失敗は成功した OUT とみなさない。
- SH は link 接続・TERM_OPEN・USB 初回打鍵を待たず起動し、agent 後起動にも既存の 2 秒 READY 再試行で対応する。同ID/古ID拒否・世代更新・FREEZE→OUT→END・失敗後の `echo $?` を維持する。
- `shell_boot.c:33–41` は link-SH だけ開始待ちを外し、正常終了・fault の両方で owner/capture 回収済みの exec 復帰後に結果を報告して再起動する。異常時の break と pm_terminal_poll の永久ループには入らない。PLN1 の挙動は保持する。
### KAPI → platform → link/mailbox
- 新 `platform/powermac/device_host.c` で platform_host の open/write/read_stage/status/close/owner_exit を同名 link_host_* へ委譲する。status は非待機で AGAIN を返す。これらの口では前景処理を行わない。
- copy/owner は共通 kapi_host.c、notice の stage→copy→commit は共通 kapi_host_notice.c をそのまま使う。**kapi/kapi_host_notice.c は変更しない**。owner は res_owner_get、LOG/診断 owner=0 と SH owner を混ぜない。
- 同じ TARGET の PLN1 SH では platform_host_* を NOSYS（void 回収は無処理）、host_mailbox_stage/commit を NOSYS、host_mailbox_latest_serial を 0 とする platform stub をリンクする。共通層へ KBOOT_LINK 判定を入れない。
- exec_host_owner_exit は platform_host_owner_exit へ結ぶ。既存 con_sink_owner_exit の正常/異常終了で両 flag と reader を回収することを確認する。
- `sdk/kapi_target_ppc.py:156,251,266` から host 5 本・notice_read/serial の absent と ZERO_SLOTS を外し、`sdk/gen_kapi.py:520` の host include 除外も解除する。halt/yield は同生成器の PPC 分岐で下記 endpoint を選択し、生成物は再生成する。
- ABI slot/署名は増やさない。serial_* は不在のまま、serial_is_initialized=0。rshell_set_active が必要な flag provider だけを個別に実体化し、port 群を一括で有効にしない。H5/H6/H7 と ESCC provider は削除する。
### EE を変えない単一の前景 endpoint
- 新 `i32 ppc_kapi_wait_once(void)` を `arch/ppc/exec_kapi.c` に置く（宣言は同 .h）。PPC の sys_halt wrapper は返値を捨て、sys_yield wrapper は返値を返す。両 wrapper ともこの endpoint を 1 回呼ぶ。**endpoint は `idle_work_run()` を呼ぶだけ**で、前景の仕事を直接呼ばない (本線 PM 条件 3。x86 も sys_halt・syscall の入口で idle_work_run を呼ぶ。IO_TIER_DESIGN はこの idle_work を Tier の振り分けへ育てる)。
- link-SH の前景の仕事 `pm_link_service_once()` (新 helper、link_platform.c/.h) は、起動の時に `idle_work_register` で登録する。中身は pm_usb_poll と pm_lan_poll(now) を各 1 回。bind・reset・PHY・LOG・診断は既存 pm_lan_poll の有限な分岐に任せる。EE の変更、完了待ち、busy、協調窓の TB 期限は設けない。
- 登録の枠 (kernel/idle_work.h の 4 本): link-SH で使うのは、nic.c:58 の net_rx_idle と pm_link_service_once の 2 本。4 本で足りる。
- **未決 (PM・本線): idle_work_run の約束 (`kernel/idle_work.h:4` の「割り込み許可・IRQ 外」) と、PPC の USER の syscall が EE=0 で走ることが合わない。** 案は 2 つ。(a) idle_work の約束を「IRQ 外・再入しない協調の 1 回」に直す (登録する仕事は EE=0 でも安全な有限の物に限る)。(b) endpoint は IO_TIER の N4 の口 (io_pump.c) で EE を一時的に許可してから idle_work_run を呼ぶ (そうなると P3 が N4 の最初の実装になる)。PM の推奨は (a)。x86 の sys_halt が IF をどう扱っているかを照らして決める。
- 非 link の sys_halt は従来の無処理、sys_yield は従来の exec_sys_yield へ委譲する。共通 KAPI 本体・exec_schedule は変更しない。
- link-SH の notice/status/入力/出力/shell_putchar には進行口を置かない。入力はキューを非待機で読む。カーネル内の入力待ちが必要な既存箇所は USER へ戻る待機方式にし、EE=0 のまま前景 poll を重複実行しない。
- USER の EE=1 復帰で DEC/net_jiffy/link_tick が進む。前景から RX・jiffy を回さず、link_platform_idle も呼ばない。D14 の close 保留 ACK は owner 回収後も保持し、USER 復帰後に進める。
- **P3 は io_pump の口を作らず、使わない。** AC-NET-3 には「EE=0 の syscall の中で待つ物が出てきたら、IO_TIER_DESIGN N4 の ppc_syscall_fg_run（arch/ppc/io_pump.c）の口を使う」を 1 行追記する。
- N4（IO_TIER_DESIGN:235）の EE 一時許可はその口だけ。AC-TIME-4（:60）の単調 µs 時計、AC-IO-1（:64）の FG1 協調進行・再入無動作・欠如 NOSYS を P3 で再定義しない。将来の LAN 一括転送/FG 移行はその設計へ従う。
### capture・fd1・入力
- fd1 → 共通 console → con_sink PRINT と FB を維持し、PRINT を二重投入しない。link-SH の console_stream は旧 pm_term_write と直接 clear の ESC 出力（`:49`）を使わない。ESCC の KAPI・出力経路を P3 のために追加しない。
- capture は共通 sink 8KiB と BEGIN/FREEZE/END の owner・両 flag 復元を使う。診断 LOG は capture 中に画面へ再投入せず LOG 経路だけにする（console_log.c:11 の flag 保存だけでは sink push は止まらない）。
- terminal_input.c:73–78 の USB poll と LAN 前景仕事を上記 helper へ移し、link-SH の input_boot は旧 LAN キーデコーダを使わない。USB 半行中は LAN 保留、LAN 通知を文字キューへ feed しない。
- LAN 実行中も USB STOP を残す。PPC input_boot の capture 起点 TB 30 秒期限で文字待ちは Enter、キー待ちは ESC を返す。FREEZE 中も閉鎖を維持し END で解除する。この入力期限は削除した EE 協調窓の期限とは別物。

## 4. 大きさの見積もりと測定関門
|項目|静的見積もり（KiB=1024B）|採否を決める測定|
|---|---|---|
|link ring / 8 handle payload / TX+RX|8 + 8×3×1400/1024 + 約3 = 約44KiB、管理欄別 (`net/link.c:61,80,117,124`)|SH PLN1 と link-SH の map の text/data/bss 差|
|mailbox heap|64×sizeof(HostNotice)、約16KiB (`host_mailbox.c:9,24`)|実 sizeof と allocator overhead、失敗時 READY 不可の確認|
|sink / shell 結果|既存 sink 8KiB 再利用。これまで GC された rshell 結果などが残るので約8KiB以上を追加計上|sh.elf の bss/静的 buf と root 埋込みサイズを分ける|
|text/rodata・削除分|新 HAL/rshell 活性化は未測定。旧端末/PLN1/HostFS 削除分を差し引く|未測定の圧縮増分を保証しない|
- 計画上の追加 RAM 枠は **64～96KiB 程度＋text 差**。44+16+8KiB を基点に管理領域を見込み、PLN1 削除益は実測前には使わない。正味増分の保証値ではない。
- heap 1MiB の高水位は未測定。字体キャッシュ 295,940B（PPC_NOTES §5）に mailbox、VFS、exec の同時生存分を加える。kmalloc_peak_bytes (`mm/kmalloc.c:267`) を boot/READY/最大結果/再接続/終了反復で記録し、1MiB 未満・割当失敗なしを必須とする。bss と USER heap を kernel heap に二重計上しない。
- map とリンク入力リストで `lan_wire.o/lan_terminal.o/terminal_input.o/hostfs_io.o/hostfs_peer.o` が link-SH に存在しないことを検査。旧 service の PLN1 tx/rx 配列も不在、link_txbuf/link_rxbuf は存在すること。単に未使用 section になっただけを「TU 不在」としない。

## 5. 実装の 6 段（各段で組める単位、共通 shell は直列）
|段|対象（r1 の ID）|変更と段の検証|
|---|---|---|
|① 文書|D1 LAN_OPERATOR_DESIGN、D2 ARCH_COMPAT_CONTRACT|案 B 採用、AC-NET-3 の有限前景・USER 復帰・将来 N4 使用を反映。既定切替条件は保持|
|② provider|H3 device_host.c、H4 exec_reclaim.c、H8 kapi_target_ppc.py、H9 gen_kapi.py、PPC exec_kapi.c/.h、PLN1 mailbox stub、必要な build リンクリスト、T1|halt/yield は当段では従来動作を包む共通 endpoint とし、③で link-SH の helper 呼出しを接続。host/notice・flag provider と stub を同段でリンク。test_kapi_binding.py・test_remote_x9.py を更新し、新 test_ppc_rshell_link.py の provider/owner 試験とともに --mutate。両 PPC 像が組めることを確認|
|③ 前景と入出力|H1/H2 link_platform.c/.h、I1 input_boot.c、I2 console_stream.c、I3 console_log.c、I4 shell_putchar.c、S4 shell_boot.c、endpoint、T1|EE 不変で USB/LAN 各 1 回。旧 terminal 依存を除き、capture/STOP/入力期限と fault 後再起動。T1 の前景/入力/capture 変異と既存像ビルド|
|④ 共通 shell（直列）|S1 rshell.c・rshell_lan.c・shell.h、S2 main.c、S3 profile.h|APP init/loop と execute/handoff の専用実体、USB 行と exit 契約。共通の受入済み経路は保持。共通 rshell/capture/owner 試験、APP の行頭/半行/exit 試験、x86 build_compare MATCH を目標|
|⑤ 像|B1 ppc_kernel.c、B2 ppc-user.mk、B3 ppc-kboot.mk|SH+LINK 許可、KBOOT_SHELL+LINK 拒否。kernel は /link/sh、remote SH は別出力。ROOT_BINS（ppc-kboot.mk:47）とその依存・再帰 make の出力先も専用 sh.bin へ切替。専用 root の中身を照合し、旧 HostFS/TU を除外。PLN1 SH・link 診断像もビルド|
|⑥ QEMU|T2 sc_run.py、T3 ppc_sh_output.py、T4 link_run.py|7450/7455 の既存機構で §6 を実行。T3 は USB/LAN のみ。新 agent/プロトコルを作らず、変更試験の --mutate と lint を実行|
- 一段を 1 ファイルへ分断しない。②で必要なリンクリストは先に整え、⑤は像選択と専用出力を完成させる。生成器の変更と再生成は②で一体化し、PLN1 SH と link-SH の未定義参照を残さない。
- net/link・kernel/con_sink・exec 共通本体は原則無変更。既存終了口に回収不足が見つかった場合は②に補修と試験を含める。

## 6. 受入と変異の対応
|受入|試験（H=host、Q=QEMU 7450/7455 両方）|一意 anchor の変異、実行時 RED|
|---|---|---|
|KAPI 実体・不在|T1 H + T2 Q：USER から host/notice/capture/token、PLN1 不在、owner 違い|host を NOSYS に戻す／owner を0固定|
|起動・後起動|T4 Q：打鍵なし起動、HELLO→cui READY、遅延 agent、2秒再READY|TERM_OPEN 待ち復活／READY 再試行停止|
|共通 shell|T2/T4 Q：echo、失敗→echo $?、exit、同ID/古ID/世代変更、過長/複数行拒否|重複判定削除／APP poll 除外|
|前景進行・close ACK|T1 H + T4 Q：halt/yield 各呼出しで idle_work_run 経由の USB/LAN 各 1 回・EE 不変、bind/READY、USER 待機中の loss/RTO・部分 write・ACK 後 close・全 handle 再利用|endpoint の idle_work_run 呼出し削除／登録の削除／USB poll 削除／close 保留 ACK 消去|
|fd1・画面・LOG|T3 Q：fd1/kprintf/UTF-8/clear、FB 表示、RSH OUT 1回、LOG混入なし|PRINT 二重投入／診断を sink に投入|
|容量・結果の完了|T1 H + T4 Q：3B header 込み境界/超過、EOT、長さ/drop/ID照合、部分結果を成功にしない|drop 比較削除／部分 write の全量送信扱い|
|USB/LAN 調停|T3 Q：半行中LAN保留、USB STOP、30秒でEnter/ESC、終了後復帰|半行中 LAN poll 許可／capture 期限条件削除|
|寿命|T1 H + T2/T4 Q：拒否/切断/exit/USER fault、両flag・reader・全handle回収、SH 自身の fault 後再起動→READY→後続コマンド|END/owner 回収の一方を削除／fault 時 break 復活|
|像・メモリ・既存経路|T2/T4：map/TU検査、heap高水位、診断link/PLN1 SH/常駐CLIの回帰|旧wire TU混入／mailbox割当失敗を注入|
- P5-sc/sh の役目を T2/T3 に寄せ、通信側は既存 link_run の機構を再利用。別プロトコル・別 agent を作らない。x86 は共通 rshell の既存試験と capture/owner 回帰を回す。
- 実装後は専用 SH/root と kernel 像、既存 PLN1 SH、link 診断像をビルドし、変更した試験だけ --mutate、lint。生成 ABI を変更する場合だけ規定の clean/all も行う。全 mutation/check-changed は回さない。
- PPC toolchain/QEMU がない環境は host 部分を必ず実行し guest 部分のみ理由付き SKIP。新試験は PPC_CROSS_DIR=/nonexistent も確認。socket を要する Q は PM が実行し、環境で別の oracle に差し替えない。
- 今回は指定差分の実コード照合・200行制限・変更 ID・差分確認のみ。読み取り専用の設計依頼のため preflight --fix-fonts と実装検査は行わない。ビルド、host/変異、Q、実機、heap/map 実測は未実施。推定を受入済み値にしない。

## 7. 決定済みの範囲
- 案 B はユーザー承認済み。KBOOT_SHELL は PLN1 退避像、link-SH の HostFS は P4 まで未提供、専用像だけ LAN 待受で起動する。既定切替、認証・番地制限・許可リストは議題に戻さない。
- 反論: 0 件。
