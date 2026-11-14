# PowerPC P3 詳細設計 r1 — SH の共通 rshell を link に結ぶ

## 1. 実物の確認・統合判断
- 対象 HEAD は `4e0e8b1e`。作業木は読み取りのみ。以下の行番号はこの版。設計のみで製品変更・ビルド・実行受入はしていない。
- 指定箇所から生成器・SH profile・起動口への参照だけを grep で追跡した。生成済み KAPI の表の存在と provider の存在を区別する。
- 読み取り限定を優先し、ファイルをコピーする `preflight --fix-fonts` は実行しない。実装時は指定環境で preflight を行う。

|食い違い ID|前提・見込みと実物（根拠）|必要な修正|
|---|---|---|
|E1|確定設計 §5:147・§6:190–192 は resident CLI。`build/ppc-kboot.mk:101` の KBOOT_SHELL と SH は別物|P3 の対象を SH に改訂|
|E2|SH に全ソースは入る (`build/ppc-user.mk:39`) が `userland/ppc/sh/profile.h:4` は SHELL_AS_APP。`userland/shell/main.c:128` は rshell 登録を除外、`rshell.c:364,478,610` は LAN 実行・poll・コマンド入口を除外|単なる KAPI 結線では不可。共通 rshell のアプリ対応が必要|
|E3|`exec/exec_kapi_init.inc:508,559` は代入だけ。実体は PPC 生成器 (`sdk/kapi_target_ppc.py:156,251`、`sdk/gen_kapi.py:659`) が host 5 本・notice_read を NOSYS、notice_serial を 0 にする|生成元の provider 選択とリンク対象を変更|
|E4|rshell は serial 初期化を要求 (`rshell.c:445`)。PPC の port 群も不在、sys_halt は void 無処理 (`sdk/kapi_target_ppc.py:7,47,262`)|LAN 起動を serial 必須条件から分離。待機口と ESCC 退避口を実装|
|E5|SH+LINK は error (`build/ppc-kboot.mk:17`)。出力先は LINK 優先で `/link` になり診断像と衝突 (`:37`)|組合せを許可し `/link/sh` を分離|
|E6|SH は旧 HostFS も組む (`build/ppc-kboot.mk:137`)。`hostfs_peer.c:14` は共通 mailbox と同名 host_alive_snapshot、`lan_service.c:98,108` の PLN1 FS 口は link で消える|P3 link-SH は旧 HostFS を組まない。SFS on link は P4|
|E7|`shell_boot.c:33` は TERM_OPEN/USB 打鍵待ち。`input_boot.c:21,31` と `console_stream.c:11` は link 像から外される terminal TU に依存|起動待ち・poll・入力・出力を PLN1 から切り離す|
|E8|USER syscall は EE=0 (`PPC_NOTES.md:32`)。`link_platform.c:14` の時計は tick、`nic_gem.c:2` の RX は net_jiffy 専有。`pm_lan_poll` の link 分岐 (`lan_service.c:291`) だけでは RX/RTO は進まない|協調待機で EE を開く。TB だけを読んでも解決しない|
|E9|`arch/ppc/exec_reclaim.c:4` の host owner 回収は空|owner 終了・異常終了で handle 回収を結線|
- 食い違いは上表の **9 件**。capture/token は不足に数えない。両者は absent 群に含まれず、生成器の通常経路で実体を呼ぶ (`exec_kapi_init.inc:592`、`con_sink.c:151`、`sdk/kapi.json:2040`)。
- POLICY_DEV §1 の判断①：本線に同じ役目がある。確定設計 §2・§3 と `rshell_lan.c:291` の READY/通知/実行/結果/END を共用する。
- 判断②：**HAL で一実装にする**。wire・ID・世代・capture は共通、PPC は platform_host、協調待機、USB/ESCC だけを提供する。rshell_link.c は作らない。
- 判断③：link v2・共通 100Hz・一 RX 所有者・共通 exec に合う。ただし「resident CLI を維持する」確定範囲とは異なるため、§7 の判断後に正典を更新して実装する。
- 互換契約は AC-TIME-3、AC-SYNC-2、AC-NET-3、AC-INPUT-1。D03（進行）/D14（close 保留 ACK）の論点は AC-NET-3 に集約されている (`ARCH_COMPAT_CONTRACT.md:108,111`)。x86/PPC に同じ世代・ID・capture・owner 回収試験を当てる。

## 2. 案の比較と推奨
|案|利点|不足・費用|判定|
|---|---|---|---|
|A：rshell_link.c + resident CLI|確定 P3a–c に沿う。小さい診断的 CLI|製品 SH の問題を解決せず、操作状態・結果 capture を二重実装|採らない|
|B：SH + link + 共通 rshell|製品側の解釈器、ID 管理、結果送信を一本化|アプリ対応、KAPI provider、待機・入力の整備が必要|**推奨**|
|C：SH の fd0 に通知文字列を注入|既存 readline へ入れられる|完全行境界・ID・rc・capture 寿命を別管理し結局 adapter が増える|採らない|
- PM の見込みは統合先として正しいが「そのまま動く」は否。特に SHELL_AS_APP を丸ごと解除して resident 用 SFS/GUI/実行方式を持ち込まない。
- KBOOT_SHELL は PLN1 の比較・退避像として保存し、KBOOT_SHELL+KBOOT_LINK は引き続き拒否。P3 では常駐 CLI の link 端末を作らない。
- PLN1 の既定・既存 SH は保持。P3 選択像だけ link-SH とし、/host は未提供、GUI は未提供。P4/P5 の完了を既定切替の条件として維持する。

## 3. 部品の境目（断片は提案。生成物は手で編集しない）
### 共通 shell と起動
```c
/* SHELL_AS_APP は維持。専用 profile の機能フラグで LAN 部分だけを解放。 */
#if !defined(SHELL_AS_APP) || defined(SHELL_REMOTE_APP)
/* rsh_lan_poll/execute/handoff と rshell コマンドを同じ実装から構成 */
#endif
/* APP の execute は通常 execute_command、GUI/SFS 特別分岐は resident 限定。 */
/* main: link-SH 専用起動引数 --rshell-lan で共通 rshell ループへ。 */
/* LAN 起動は serial_is_initialized を必須にしない。 */
```
- link-SH 専用 sh.bin/root を別出力先に作り、--rshell-lan を exec の既存引数渡し経路で渡す。通常 SH の登録表・既定起動を変えない。
- SH 起動は link 接続・TERM_OPEN・USB 初回打鍵を待たない。agent が後起動でも READY を 2 秒間隔で再試行。LAN exit の結果/END 後は既存 SH 再起動方針を使う。
- 共通 rshell の行頭調停・126B 制限・ID 重複拒否・世代更新・FREEZE→OUT→END を維持。失敗 rc は次の `echo $?`、通信 200 と rc=0 を混同しない。
- SHELL_REMOTE_APP のコマンド登録は rshell/lantrace に限定。UART 専用操作と GUI/SFS 特別実行は未提供と返す（機能範囲の区別）。rshell_lan.c の wire 処理はコピーしない。
### KAPI → platform → link/mailbox
```c
/* 新 platform/powermac/device_host.c。kapi/kapi_host.c の copy/owner 規約を再利用 */
i32 platform_host_status(i32 h, u32 *st, u32 *len, int owner) {
    pm_link_service_once();             /* IRQ 外、lock 外。有限の協調進行 */
    return link_host_status(h, st, len, owner); /* 非待機、未完なら AGAIN */
}
/* open/write/read_stage/close/owner_exit も同名 link_host_* へ委譲。 */
/* read_stage のポインタ取得後〜copy 完了まで進行処理を挟まない。 */
/* notice_read は kapi_host_notice_read → stage → copy → commit を共用。 */
```
- host 系を absent 群から外して共通 `kapi_host.c` / `kapi_host_notice.c` をリンク。PLN1 側は platform の NOSYS 実体で返す。notice もビルド分岐で不在を返すので未接続 mailbox を参照しない。
- 共通 notice 入口には platform の一回進行口を追加（x86 は既存進行を保つ no-op）。poll は stage より前だけ。owner は res_owner_get、LOG/診断 owner=0 と SH owner を混ぜない。
- `exec_host_owner_exit` は platform_host_owner_exit へ。capture 回収は既存 con_sink_owner_exit の正常/異常終了経路を試験で確認し、欠落があれば既存の共通終了口へ追加する。
- ABI slot/署名は増やさない。`sdk/kapi_target_ppc.py` と必要な生成器分岐を変更して再生成する。port 群全体を有効にせず必要な ESCC 入出力と rshell flag のみ提供する。
### EE=0・待機・RX 所有者
```c
/* 新 pm_link_service_once: syscall の private lock を持たない地点専用 */
if (kctx_irq_depth || busy) return;      /* IRQ/再入から待たない */
busy = 1;
saved = ppc_irq_save();
/* TB による期限を保存。link_platform_now は tick_count のまま。 */
link_platform_idle();                  /* EE=1 で DEC/net_jiffy/link_tick が進む */
/* USB と reset/PHY/LOG/診断の前景仕事を有限回だけ進める。二重実行禁止。 */
ppc_irq_restore(saved);                /* syscall 内の元の EE=0 を復元 */
busy = 0;
```
- EE=0 を保ったままの RX/RTO 進行は **ない**。host_status 自体を完了まで待たせず、上の短い協調窓を作り AGAIN で戻す。USER へ戻れば通常の DEC が動く。
- hostfs_io.c:37,46,79 の手本は「IRQ 拒否・TB の絶対期限・有限 poll」。PLN1 の RX poll 自体は流用しない。TB を別の link jiffy にせず、前景から DEC/link_tick/net_jiffy を呼ばない。
- 待機 KAPI/入力のループは同じ進行口を使い、TB の期限を再試行で延長しない。共通 rshell の wait_again は sys_yield (`rshell_lan.c:94`)、行頭は sys_halt (`rshell.c:488`) なので両方の PPC 経路を接続する。
- link_platform_idle は既に EE を許可する (`link_platform.c:18`)。呼出前後の EE 保存は外側の責任。IRQ 中・copy 中・handle lock 内から呼ばない。helper の外に長い EE=0 待ちを残さない。
- D14 の close 保留 ACK は既存 link の専用保持を維持。close/owner_exit 直後にも次の協調窓で送信を進め、owner の再利用で消さない。
### capture・fd1・入力
```c
/* console_stream_write の link 分岐。通常 fd1 は共通 console を通る。 */
#if KBOOT_LINK
if (!con_sink_capture_active()) pm_escc_write(&serial, bytes, len);
#else
pm_term_write(bytes, len); /* 旧像のみ */
#endif
/* fd1 → console → con_sink PRINT と FB。mirror は保存 flag=0 で停止。 */
/* input_boot: local=USB のみ。ESCC は serial_trygetchar、LAN は notice のみ。 */
```
- capture は共通 sink 8KiB、BEGIN/FREEZE/END の owner・両 flag 復元を利用。console_stream 側でも直接 clear の ESC 出力 (`console_stream.c:49`) を抑止し、PRINT を二重投入しない。
- 診断 LOG は RSH OUT に混入させない。`console_log.c:11` の一時 flag 保存だけでは sink push は止まらないため、capture 中の診断の画面再投入を止め LOG 経路だけにする。
- terminal_input.c を link 像から外す前に USB poll と LAN reset/PHY 前景処理を新 poll 口へ移す。input_boot の旧 LAN キーデコーダは link 分岐で使わない。
- 行途中は USB/ESCC の行を完成させてから LAN を受理。LAN 通知を文字キューへ feed しない。local 入力口に ESCC を混ぜず、serial byte の二重消費を防ぐ。
- LAN 実行中は ESCC 読み込みを止め、USB STOP を残す。PPC は x86 kbd.c を使わないため input_boot に capture 起点の TB 30 秒期限を実装し、文字待ちは Enter、キー待ちは ESC を返す。FREEZE 中も閉鎖を維持し END で解除。

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

## 5. ファイル別の段（全段 1 ファイル・1 実装者・変更400行以下）
- 以下は上から依存順。各行が独立依頼。既存の長いファイルは差分上限。共通ファイルは直列。試験も別段で先に準備する。各段の実装者は PM が割当てる。
|段|ファイル|変更内容・上限行|
|---|---|---|
|D1|docs/decisions/network/LAN_OPERATOR_DESIGN.md|§7 判断後、P3/CLI/受入の改訂、120|
|D2|docs/decisions/ARCH_COMPAT_CONTRACT.md|FG0 協調窓・復帰/IRQ 拒否を AC-NET-3 に追記、60|
|H1|platform/powermac/link_platform.c|一回進行・EE 保存・TB 期限・再入、180|
|H2|platform/powermac/link_platform.h（新）|前景 helper 宣言、40|
|H3|platform/powermac/device_host.c（新）|platform_host 全口・非link NOSYS、200|
|H4|arch/ppc/exec_reclaim.c|host owner 回収、40|
|H5 共通|include/platform_host.h|notice 用一回進行口宣言、20|
|H6|platform/pc98/device_host.c|同口の既存動作保持実体、20|
|H7 共通|kapi/kapi_host_notice.c|stage 前の進行、非link不在分岐、60|
|H8|sdk/kapi_target_ppc.py|host と必要 port/sys slot の provider 化、100|
|H9|sdk/gen_kapi.py|PPC の halt/yield/ESCC 専用 endpoint 選択、150|
|H10|platform/powermac/remote_port.c（新）|halt/yield・ESCC・rshell flag endpoint、220|
|I1|platform/powermac/input_boot.c|USB 専用 local・協調待機・30秒期限/STOP、220|
|I2|platform/powermac/console_stream.c|旧端末除去・capture/clear 出力抑止、80|
|I3|platform/powermac/console_log.c|capture 中の LOG 画面再投入抑止、40|
|I4|platform/powermac/shell_putchar.c|terminal poll 依存を新 helper に置換、40|
|S1 共通|userland/shell/rshell.c|APP LAN の実行/登録/調停、resident SFS/GUI 分離、350|
|S2 共通|userland/shell/main.c|機能 flag・専用起動引数、100|
|S3|userland/ppc/sh/profile.h|SHELL_REMOTE_APP の選択を受ける、30|
|S4|platform/powermac/shell_boot.c|link 起動待ち除去・引数・再起動、100|
|B1 共通|init/ppc_kernel.c|link-SH で旧 HostFS boot を呼ばない、40|
|B2|build/ppc-user.mk|remote SH の別出力/flag、90|
|B3|build/ppc-kboot.mk|SH+LINK・別出力・provider・旧FS/TU除外、180|
|T1|tools/tests/test_ppc_rshell_link.py（新）|provider/owner/入力/capture の fake と変異・CHECK、350|
|T2|tools/ppc/sc_run.py|既存 USER/SH 台本に link 選択と CPU 両種、250|
|T3|tools/ppc/ppc_sh_output.py|fd1/capture/USB/ESCC の link 受入、250|
|T4|tools/ppc/link_run.py|既存 agent/socket 機構で READY/OUT/断線/容量、350|
- `rshell_lan.c`、net/link、kernel/con_sink、exec の共通本体は原則変更不要。回収経路に不足が見つかれば共通終了口 1 ファイルの補修段（100行）を追加して先に試験する。生成物更新は H9 後に生成手順で実施する。

## 6. 受入と変異の対応
|受入|試験（H=host、Q=QEMU 7450/7455 両方）|一意 anchor の変異、実行時 RED|
|---|---|---|
|KAPI 実体・不在|T1 H + T2 Q：USER から host/notice/capture/token、PLN1 不在、owner 違い|host を NOSYS に戻す／owner を0固定|
|起動・後起動|T4 Q：打鍵なし起動、HELLO→cui READY、遅延 agent、2秒再READY|TERM_OPEN 待ち復活／READY 再試行停止|
|共通 shell|T2/T4 Q：echo、失敗→echo $?、exit、同ID/古ID/世代変更、過長/複数行拒否|重複判定削除／APP poll 除外|
|EE=0 進行・close ACK|T1 H + T4 Q：USER 待機中の loss/RTO・部分 write・ACK 後 close・全 handle 再利用、TB で上限監視|協調窓の EE 許可削除／close 保留 ACK 消去|
|fd1・画面・LOG|T3 Q：fd1/kprintf/UTF-8/clear、FB 表示、RSH OUT 1回、ESCCへ複写なし、LOG混入なし|capture 中 ESCC を許可／診断を sink に投入|
|容量・結果の完了|T1 H + T4 Q：3B header 込み境界/超過、EOT、長さ/drop/ID照合、部分結果を成功にしない|drop 比較削除／部分 write の全量送信扱い|
|USB/ESCC 調停|T3 Q：半行中LAN保留、USB STOP、LAN中ESCC停止、30秒でEnter/ESC、終了後復帰|local に ESCC を混ぜる／capture 期限条件削除|
|寿命|T1 H + T2/T4 Q：拒否/切断/exit/USER fault、両flag・reader・全handle回収、後続コマンド|END/owner 回収の一方を削除|
|像・メモリ・既存経路|T2/T4：map/TU検査、heap高水位、診断link/PLN1 SH/常駐CLIの回帰|旧wire TU混入／mailbox割当失敗を注入|
- P5-sc/sh の役目を T2/T3 に寄せ、通信側は既存 link_run の機構を再利用。別プロトコル・別 agent を作らない。x86 は共通 rshell の既存試験と capture/owner 回帰を回す。
- 実装後は専用 SH/root と kernel 像、既存 PLN1 SH、link 診断像をビルドし、変更した試験だけ --mutate、lint。生成 ABI を変更する場合だけ規定の clean/all も行う。全 mutation/check-changed は回さない。
- PPC toolchain/QEMU がない環境は host 部分を必ず実行し guest 部分のみ理由付き SKIP。新試験は PPC_CROSS_DIR=/nonexistent も確認。socket を要する Q は PM が実行し、環境で別の oracle に差し替えない。
- 今回は設計ファイルの200行制限・必要章・HEAD確認のみ。ビルド、host/変異、Q、実機、heap/map 実測は未実施。推定を受入済み値にしない。

## 7. 未決（ユーザー判断が要るもの：1件）
- **P3 対象を常駐 CLI から共通 SH に変更してよいか。推奨：B を承認。** 同時に KBOOT_SHELL は PLN1 退避像、link-SH の HostFS は P4 まで未提供、専用像だけ LAN 待受で起動する範囲を承認する。確定設計 §5/§6/§7 と相違するため POLICY_DEV §1 の判断が必要。既定切替、認証・番地制限・許可リストは議題に戻さない。
