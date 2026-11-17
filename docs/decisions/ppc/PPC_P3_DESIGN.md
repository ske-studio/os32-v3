# PPC_P3_DESIGN — PowerPC P3: 共通の SH を link の LAN に載せる (FG1・文字端末の bridge)

> 状態: **現行 (2026-11-15)** — 確定設計の改訂版 v2 (astra 起草 r1〜r4、Opus 5.5 のレビュー 2 回ずつ、PowerPC PM が反映) と、その元の初版 (付録 A) の写し。出所は ~/os32-tmp/run/ppc-p3-design/ の DESIGN.final.md と、使い捨ての枝 wt/ppc-p3-draft の drafts/ppc-p3/DESIGN.final.v2.md。段の状態は票 TASK_PPC_LAN が持つ。関係する確定設計: [ARCH_CTX_DESIGN](../ARCH_CTX_DESIGN.md) (AC-CTX-1)、[INPUT_HAL_DESIGN](../INPUT_HAL_DESIGN.md)、[LAN_OPERATOR_DESIGN](../network/LAN_OPERATOR_DESIGN.md)。
> 本文中の「final」は付録 A を指す。付録 A の §3「EE を変えない単一の前景 endpoint」の io_pump・EE の窓の記述は、v2 §2 で全部置き換わっている (W4 の AC-CTX-1 で USER の syscall は EE=1)。

## r3→r4 の変更
- N1: link-SH と PLN1 の像別に USB/LAN 前景 work の登録関数を明記。
- N2: peek の有限採取を許し、完成 key の消費と交互選択の更新だけを禁止。
- N3: provider に GUI 状態の照会口を追加し、bridge の参照元を明記。
- N4: HAL 1p・1a・1b・4c を P3③ の厳密な前提とし、P3 先行案を削除。
- N5: ③を a〜d に分割し、各小段を 1〜3 ファイル・400 行以下に制限。
- N6: capture 期限を wait 開始起点に統一し、通知・BEGIN 起点 profile を削除。
- N7: 入口と endpoint の 2 周の根拠、poll 間引き、周別の計数を明記。
- N8: syscall 入口の idle_work_run の参照を exec/exec_syscall.inc:95 に訂正。
- N9: usb_boot.c の古い EE 注釈の修正と新規ソース一覧の追従を③の完了条件に追加。

## 1. 範囲と実物の確認
- 基準は main `4adf1a4a`（HEAD 一致を確認）。作業木は読み取りのみ、出力は本票だけ。
- 材料は `in/P3_DESIGN.final.md` と `in/INPUT_HAL_DESIGN.r2.md`、同レビュー r1・r2 依頼。以下「final」「HAL r2」はこれらを指す。
- 案 B、専用 link-SH、共通 rshell_lan.c/rshell.c、PLN1 退避像、HostFS 未提供、既定切替の条件、公開 ABI 不変更は final のとおり。
- final §1 の E1〜E9、§2 の採用理由は維持。ただし EE=0 の syscall という根拠を本節で置換する。
- `docs/decisions/ARCH_CTX_DESIGN.md:44`（AC-CTX-1）どおり、`arch/ppc/kernel_trap.c:104` の USER syscall 分岐は `:107` で許可、`:108` で dispatch、`:109` で出口処理のため禁止する。
- IRQ の深さは同 `:94–101` の外部/DEC handler だけで増減する。通常 syscall は IRQ 外、途中の IRQ は深さを戻して復帰する。
- `kernel/idle_work.h:4` の「許可状態・IRQ 外」を満たす。実装 `kernel/idle_work.c:30–36` は許可・深さ・再入を確認して各登録を 1 回呼ぶ。既存のガードは変更しない。
- `exec/exec_syscall.inc:95` の入口にも idle_work_run が既にある。W4 後は PPC でも有効であり、前景進行が halt/yield のみに限定されるとは扱わない。

## 2. 前景 endpoint（final §3 の EE/N4 記述を全置換）
```c
/* arch/ppc/exec_kapi.c、同 .h に宣言。生成器が halt/yield をここへ結ぶ。 */
i32 ppc_kapi_wait_once(void)
{
    idle_work_run();
    return 0;
}
```
- sys_halt は返値を捨て、sys_yield は返す。endpoint は EE を読書きせず、条件付き enable・独自 pump・TB 窓・io_pump.c を作らない。
- 呼出しは通常 syscall FG1、lock/共有 busy を保持しない。誤って FG0/IRQ/再入で idle_work を呼んでも仕事なし、そこで許可して救済しない。
- 各像は初期化完了後、下表の関数を idle_work_register する。USB と pm_lan_poll(now) を各 1 回、有限処理・自前の再入防止・許可状態復元は final のとおり。
| 像 | 登録する関数 | 前景の仕事 |
|---|---|---|
| link-SH | pm_link_service_once | USB と LAN/mailbox の進行 |
| PLN1 sh.bin | pm_terminal_poll 相当の idle_work 用 wrapper | USB と pm_lan_poll の進行。文字端末 provider の読出しとは独立 |
- endpoint 1 回につき登録処理 1 周。halt/yield syscall 全体には入口の 1 周もあり、通常は計 2 周になり得る。x86 も kernel/sys.c:41–42 の sys_halt と exec/exec_syscall.inc:95 の入口で 2 周回り、pm_usb_poll は 10ms、pm_lan_poll は 1ms で間引くため、受入では入口/endpoint の周ごとに呼出し数と実処理数を分ける。
- host/notice/status/入力 provider 自体から pump は呼ばない。それらの syscall に共通入口の進行があることとは区別する。
- net_rx_idle と helper の 2 枠は final のとおり。HAL の入力 work が登録済みなら総枠数と登録結果を再確認し、USB poll の実行所有者を一本化する。
- HAL が USB 進行を既存 work に移した場合、helper はその登録を重複せず LAN のみ担当する。単なる入力読出しから USB を再 poll しない。
- DEC/net_jiffy/link_tick は USER 復帰後だけでなく FG1 syscall 中も進む。前景 helper で RX/jiffy を直呼びせず、net_rx_idle の既存 rx_service と協調する。
- close 保留 ACK、link_platform_idle を呼ばない方針は final のとおり。IO_TIER と AC-NET-3 の文書は AC-CTX を参照し、P3 に N4 窓を作らせる記述を残さない。

## 3. bridge の境界と状態所有
- **bridge 先行を必須とする**。HAL の 1p・1a・1b・4c の完了を P3③全体の厳密な前提にする。in2 公開 ABI・mouse・pad の完了は待たない。
- HAL r2 の InputLegacyProvider は terminal/注入/park の口であり、USB local の口はまだ宣言されていない。PPC の文字キューを移す前には下記の一時的な local provider が必要。
- 下の追加宣言と mode は内部契約案。HAL 側で同名相当を確定し、P3 と別々の型を作らない。InputOps と SDK の署名は不変。
```c
/* kernel/input_legacy.c が使用する共通内部口。local は cooked u16 または -1。 */
struct InputLegacyLocal {
    int (*try_key)(void);
    int (*peek_key)(void); /* 有限採取可、完成 key の消費・交互選択の更新は禁止 */
};
enum InputLegacyMode {
    IL_CHAR, IL_KEY, IL_LOCAL_CHAR
};
int input_legacy_try(int mode);
int input_legacy_peek(int mode); /* HAL r2 §3 に補う */
int input_legacy_wait(int mode, int interruptible);
/* InputLegacyProvider に int (*gui_active)(void) を追加する。 */
/* HAL 3c 前の x86 は kbd_gui_mode を返す adapter、GUI のない像は偽。 */
/* GUI wait 用の park_kbd(void) を InputLegacyProvider に追加する。 */
/* 成立時は復帰せず、再開値は byte。通常 return は park 不成立だけ。 */
```
- PPC の移行中は local.try_key を pm_input_usb_take に結び、非消費 peek を USB キューに追加する。独自 lookahead で take を peek に偽装しない。
- HAL 4b 後は共通 cooked ring を local に結び、PPC USB は usbp_input_key/release → input_capture の既存 HAL 設計に従う。PPC に文字キュー/押下表/文字化の複製を残さない。
- provider 登録は起動時の常駐関数ポインタ。NULL は空/不在であり、共通部から PPC・serial・exec の装置固有 symbol を参照しない。
- **link-SH では terminal_try/peek と inject_try/peek は NULL、terminal_enabled は常に偽**。LAN notice は host_mailbox → rsh_lan_poll へ渡す。PPC LAN だから terminal_try に結ぶ、という一律登録はしない。
- PLN1 の旧文字端末を残す像だけ terminal provider に LAN byte stream を登録する。GUI 注入は別の宛先付きキューであり、LAN notice を inject_try へ載せない。
- ~~旧 PLN1 に必要な交互選択・lookahead・escape decoder は共通 bridge の互換 profile に移す。~~ (2026-11-15 に改めた。末尾の「HAL 4c の順の入れ替えで決めたこと」と「③b-2 で決めたこと」) PLN1 は terminal=NULL とし、local を「LAN (escape decoder と完成 key 1 個の非消費の先読み、platform 側) が先、空なら USB」の 1 つの口にする。LAN と USB の交互選択はやめる。共通の wait(IL_KEY) は local だけを読むので (kernel/input_wait.c、x86 の kbd.c:789 と同じ)、LAN を terminal にすると PLN1 の LAN のキーの待ちが止まるため。link-SH には decoder を有効化しない。x86 の serial 優先は変えない。
- 現行の状態は `platform/powermac/input_boot.c:8–12`、実際の交互選択は `:28–43`。terminal_input.c は byte ring と poll を持つ。移動元を取り違えない。
- 共通 bridge が選択状態・decoder・peek の整合・capture 入力期限・GUI policy を所有、HAL が物理状態/ring/keymap を所有する。provider は装置の byte/cooked 読出し・GUI 状態照会・park の口だけ、SH が半行/ID/実行状態を所有する。
- PLN1 の peek/has_key は有限の採取（byte→decoder→完成 key の lookahead）を進めてよい。完成 key の消費と交互選択の順の更新は禁止し、try 成功時だけ消費・選択更新する。採取は既存キューから行い、装置 pump は前景 work が担当する。
- input_boot.c は HAL 4c の共通 bridge へ provider を登録し、InputOps は薄く委譲する。PPC 専用 bridge や HAL 未着時の最小実装は作らない。

## 4. 口ごとの振る舞いと LAN 行頭の調停
- capture の期限は待ちの開始を起点にするので、一つのコマンドが capture の中で何度も待つと、それぞれが 30 秒まで待てる (全体の決まった上限は無い。x86 の rshell と TASK_LAN_OPERATOR:19 の現行と同じ)。全体の上限は host の側の待ちに任せる。⑥ の QEMU の台本には、試験の時間の上限を書く。
| 口 | bridge 契約と P3 の使い方 |
|---|---|
| try(IL_LOCAL_CHAR) | CUI の USB/local のみを非待機で消費。GUI は -1、注入/terminal/park を見ない。kbd_trygetchar_local をここへ結ぶ |
| try(IL_CHAR/IL_KEY) | GUI は注入だけ、空なら park_poll を 1 回試して通常復帰時 -1。CUI は profile に従う。capture 中は terminal を閉じ、local STOP は残す |
| peek | try と同じ選択対象/順番を非消費で見る。park・pump・deadline 合成文字の消費なし。有限採取可、完成 key の消費・交互選択更新なし。完成済み先頭は反復 peek で維持 |
| wait(IL_CHAR) | CUI は local 優先。x86 の rshell serial 条件と非 capture の空白 timeout は維持。link-SH は USB と中断/期限を検査し FG1 で有限 idle_work→再検査 |
| wait(IL_KEY) | x86 の現行どおり serial を読まない（kbd.c:789）。link-SH も local のみ。interruptible の中断は -1、捕捉期限は ESC |
| GUI wait | 注入を即返す、空なら park_kbd。成立時の再開値は注入 byte、try の park_poll は byte または -1。peek/has_key/local は park しない |
- HAL r2 の mode は文字/キー/ローカルという口の区別。GUI 状態は provider.gui_active()、capture 状態は con_sink_capture_active() を共通側で読み、呼び手の任意指定にしない。wait を try の一律ループにしない。
- rsh_lan_app_loop は **try(IL_LOCAL_CHAR)**（既存 KAPI 名 kbd_trygetchar_local）を 1 回先に呼ぶ。文字があれば rsh_line_feed、半行中は rsh_lan_poll を呼ばない。
- 行頭で local が空のときだけ rsh_lan_poll を 1 回呼ぶ。半行の完了/取消後に行頭へ戻る。LAN を保留しても kernel の通信・ACK・mailbox 採取は止めない。
- 調停に wait を使うと LAN 到着時に USB 待ちから戻れず、peek→別経路で consume すると由来が曖昧になるため、app_loop は local try に統一する。待受無入力は sys_halt、LAN AGAIN は sys_yield。
- 半行・126B 制限・exit N・LAN EXIT・reset/flag 復元・後起動 READY は final のとおり。
- capture 期限は x86 と同じ各 wait の開始起点（kbd.c:754,792）で 30 秒とする。bridge が待ちごとの開始時刻を持ち、ループ内で更新しない。BEGIN 起点の profile と input_legacy_capture_changed 通知は削除し、con_sink.c は変更しない。
- capture の有効状態は con_sink_capture_active() だけを正とする。FREEZE 中も terminal を閉じたまま、END/owner 回収後はその照会結果に従って解除する。
- 時計は共通単調時間で 30 秒を比較し、PPC は platform_clock_read_us の TB 由来値を使える。100Hz tick を ms と取り違えず、wrap 差を試験する。期限後の文字 wait は CR、キー wait は ESC、try/peek は非待機の既存結果を維持。
- STOP/中断を期限より先に確認し、capture 終了・fault 回収後に次の session へ期限を持ち越さない。sink 本体の 8KiB/reader/flag/PRINT と出力の契約は final のとおり。

## 5. 実装の 6 段（final の表を置換）
| 段 | 対象 | 変更と検証 |
|---|---|---|
| ① 文書・境界 | LAN_OPERATOR、ARCH_COMPAT、IO_TIER、HAL bridge 契約 | 案 B は維持、N4/EE 窓を削除。HAL と local/peek/wait/capture の担当・先行範囲を確定。公開 ABI は変えない |
| ② host/KAPI provider | device_host、exec_reclaim、kapi_target_ppc.py、gen_kapi.py、exec_kapi.c/.h、PLN1 stub・リンクリスト | final の host/notice/flag/owner 結線と生成を一体化。wait endpoint は直接 idle_work_run。binding/remote_x9/T1 の provider/owner 変異と両 PPC 像ビルド |
| ③a 前景・endpoint | link_platform、PLN1 work 登録の TU、usb_boot.c（各小段 1〜3 ファイル） | ②の endpoint 結線を使い、像別 work 登録と入口/endpoint の周別計数を検証。usb_boot.c:105 の「USER syscalls keep EE clear」を W4 後の FG1 契約へ訂正 |
| ③b-1 USB の非消費 peek | input_usb.c・input_usb.h・tools/checks/ver-ppc-input-usb.py（3 ファイル） | HAL 4c 完了後。USB のキューに非消費の peek を足す (§3 の「非消費 peek を USB キューに追加」)。ver-ppc-input-usb.py に「peek が消費する」変異を足す。input_boot.c は触らない |
| ③b-2 provider の像別登録 | input_boot.c・tools/checks/ver-ppc-input-usb.py・(要れば) terminal_input.c（3 ファイル以下） | ③b-1 の後。input_boot.c の独自の状態 (lookahead・last_source・交互選択) を除き、PLN1 は terminal=NULL・local=「LAN (decoder 付き) が先、空なら USB (③b-1 の try/peek)」、link-SH は terminal=NULL・local=USB を登録する。ver-ppc-input-usb.py の anchor (peek-consumes・lan-first・no-usb) を新しい provider へ移す。lan-first は「terminal (LAN) が先、空なら local (USB)」の期待に書き換える (交互選択の期待は消す)。HAL 共同 fixture で有限採取と非消費を検証 |
| ③c capture 期限 | HAL 4c に含める (本線に H7 として依頼)。HAL が入れない場合は、③c を kernel/input_wait.c (共通) の実装の段に替える (1〜3 ファイル) | bridge の wait 開始起点 (x86 と同じ kbd.c:754・:792)、FREEZE 閉鎖、期限後の文字の待ちは CR・キーの待ちは ESC、STOP、END/owner 回収を検証。con_sink 通知の段は設けない |
| ③d 出力・shell 起動 | d1: console_stream・console_log、d2: shell_putchar・shell_boot（各 2 ファイル） | d1→d2 の順で出力・起動を結線。T1 変異、fault 再起動と既存像ビルド |
| ④ 共通 shell（直列） | rshell.c/rshell_lan.c/shell.h、main.c、profile.h | final の APP 専用実体。local try の行頭優先・半行 LAN 保留・exit。共通 rshell/capture/owner 回帰、x86 build_compare MATCH を目標 |
| ⑤ 像 | ppc_kernel.c、ppc-user.mk、build/targets/ppc-powermac.mk | final の専用 sh.bin/root・出力先・ROOT_BINS 依存、SH+LINK 許可、KBOOT_SHELL+LINK 拒否。旧 TU/HostFS 除外、bridge/provider のリンク閉包と map を確認 |
| ⑥ QEMU | sc_run.py、ppc_sh_output.py、link_run.py | 7450/7455 で下表。新 agent/プロトコルなし。変更試験の --mutate と lint、HAL 共同試験を重複作成しない |
- 段の順: ② → ③a → (HAL 4c、③c を含む) → ③b-1 → ③b-2 → (HAL 4b) → ④ → ⑤ (③d を含む) → ⑥。③d は link-SH の像にしか効かず、その像は ⑤ で作るので、⑤ にまとめた (2026-11-15、本線 PM 了承)。⑤ が大きければ ⑤a 像・⑤b 出力 (旧 ③d1)・⑤c 起動 (旧 ③d2) に分ける。HAL 4b は「③b の local を共通の cooked ring に結び替え、③b-1 の peek を消す」段。③d は ③a の後なら ③b・HAL 4b と並べてよい (4c が遅れたら ③d を先に進める)。③b は peek の pump (input_boot.c:31) を消すので、③a の像別の work の登録より先に入れない (PLN1 の LAN の入力が止まる)。③d は ③a の後なら ③b と並べてよい。
- ③d: PLN1 の像の出力の側の直接の poll (console_stream.c:12・shell_putchar.c:9 の pm_terminal_poll) は残す。idle_work の登録と重なっても、LAN は lan_service.c:248-249 の poll_at を先に進めるので入れ子の 2 回目は何もしない。USB は 10ms の間引きと irq_save で守られる。この 2 つを受入の「二重登録なし」の根拠にする。link-SH の像では、旧端末の TU が外れるので、この直接の poll も無くなる。
- ③a〜d（d は d1/d2）の各小段は試験・ビルド一覧の変更も含め 1〜3 ファイル・400 行以下とし、超える場合は同じ段内でさらに分割する。各小段で組める状態を保つ。共通 shell は final のとおり直列。
- ③の完了条件に、新規ソースの ROLES §4-3 追従（build/sources/order.mk、build/sources/common.mk または arch/platform 一覧、tools/tests/fixtures/build_profile_x86.json の C_KERNEL、PPC は build/targets/ppc-powermac.mk）と選択・リンク確認を含める。io_pump.c は追加しない。
- host_notice 共通本体無変更、serial_* 不在、必要 flag だけ実体化、正常/異常の owner 回収、生成物手編集禁止は final のとおり。

## 6. 受入と変異（H=host、Q=QEMU 7450/7455 両方）
| 受入 | 観測する合格条件 | 一意 anchor・実行時 RED の変異 |
|---|---|---|
| FG1/endpoint | H+Q：USER halt/yield 本体は EE=1・深さ0、endpoint 内は 1 周、入口分は別計数。IRQ 後も復帰、FG0/IRQ/再入は仕事なし | W4 入口 enable 除去は既存 CTX 試験へ、endpoint 呼出し削除は P3 試験へ。自動 enable 混入も拒否 |
| 前景・close ACK | H+Q：link-SH/PLN1 両像の各 work の USB/LAN 有限進行、二重登録なし、bind/READY、FG1 中 DEC、loss/RTO/部分 write、ACK 後 close/再利用 | work 登録・USB 進行除去、保留 ACK 消去 |
| bridge 非待機 | H：GUI空+terminal有でも -1/park1回、local は park/注入なし、peek/has_keyの有限採取で未完成byteからkeyが完成、完成keyは非消費・交互選択不変、次tryが同値 | GUI fallthrough、peek消費、localへのLAN混入 |
| bridge 待機 | H+Q：char/key の優先と serial 有無、中断=-1、park再開byte/-1、FG1進行。GUI実動作は可能な像で別受入 | waitをtryループ化、park再開値破壊、中断無視 |
| capture 入力 | H+Q：各wait開始から30秒、BEGINから遅れてwaitしても開始時点から計時、待機ループ内で延長なし、FREEZE閉鎖、CR/ESC、USB STOP、END/owner回収後復帰 | BEGIN起点化、待機ループで期限再設定、FREEZEで解除、active照会を固定 |
| USB/LAN 調停 | Q：同時到着はlocal先、半行中LAN実行なし/通信は進行、行完了/取消後実行。LAN notice は文字キューに一度も入らない | 半行中poll許可、noticeを文字feed |
| KAPI/起動/共通shell | final の T1/T2/T4：host/notice/capture/token/不在/owner、打鍵なし起動、遅延agent、2秒READY、echo/rc/ID/世代/exit/長行 | final の NOSYS化・owner=0・TERM_OPEN待ち復活・重複判定削除 |
| 出力/容量/寿命 | final の T1/T2/T3/T4：fd1/FB/UTF-8/clear、PRINT一回、LOG非混入、境界/drop/EOT/部分結果、fault後READY、両flag/reader/handle回収 | final の PRINT重複・drop比較削除・END/回収除去・fault break |
| 像/メモリ/退行 | final の map/TU除外・heap高水位、PLN1 SH/診断link/常駐CLI/x86回帰。bridge追加分と旧入力削減分を実測 | 旧wire TU混入、mailbox割当失敗。HAL profile切替によるPLN1入力退行も検出 |
- final §4 の RAM 64〜96KiB＋text は概算のまま。bridge/HAL の共有増分を map で区別し二重計上しない。heap 1MiB 未満・割当失敗なしの条件は維持。
- 実装時のビルド、変更試験 --mutate、CHECK 入力宣言、移動 anchor の移設、lint、PPC ツール欠如時の理由付き SKIP、socket Q は PM 実行、全変異/check-changed を回さない方針は final のとおり。
- 本回は N1〜N9 の関連箇所の照合・差分・行数・出力確認のみ。読み取り指定を優先し preflight --fix-fonts、build、lint、host/変異、QEMU、実機、map/heap 実測は未実施。実装受入済みとは扱わない。

## 7. 未決
- ユーザー判断が必要な未決は 0 件。HAL 1p・1a・1b・4c の完了後に P3③へ進む。HAL 側への契約補足は次節の技術調整とし、案 B 等の承認済み範囲を再審議しない。

## 入力の HAL への意見
- H1: local try/peek・IL_CHAR/KEY/LOCAL_CHAR・共通 peek に加え、provider.gui_active() を補う。HAL 3c 前の x86 は kbd_gui_mode の照会 adapter とする。
- H2: PLN1 の文字端末と link-SH の mailbox を分け、後者は terminal/注入不在・local try＋行頭調停とする。
- H3: peek/has_key の有限採取（byte→decoder→完成 key の先読み）を許す。完成 key の消費と交互選択の順の更新は禁止する。
- H4: GUI wait の park_kbd を補い、成立時非復帰・再開 byte、通常 return は不成立、try の park_poll は byte/-1 とする。
- H5: 取り下げ。capture は wait 開始起点に統一し、有効状態は con_sink_capture_active() のみで判断する。
- H6: GUI local=-1/park なし、getkey_wait は serial なしを口別 fixture で確かめる。

## 確定 (2026-11-15、Opus 5.5 第 1 回 Request changes・第 2 回、PM が反映)
- 第 1 回 N1〜N9: r4 で直った (第 2 回で確認)。
- 第 2 回 M1 (P2): PPC の capture の期限の実装者が無かった。HAL 4c に含めるよう本線に H7 として依頼し、入らない場合は ③c を共通の input_wait.c の実装の段に替える、とした。
- 第 2 回 M2 (P3): ③d で PLN1 の出力の側の直接の poll を残すと明記し、二重の呼び出しの守りを受入の根拠にした。
- 第 2 回 M3 (P3): 段の順を明記した (③a は ③b より先)。
- 第 2 回 §3: 待ちの開始を起点にすることによる、全体の上限の振る舞いを §4 に書いた。

## 確定の後に決めたこと (2026-11-15、PowerPC PM)
- 段の順と HAL 4c: ③a は HAL 4c より先に出した (ユーザー決定 11-15、本線 PM の休止を受けて)。§5 の段の順を正とし、§3 と §7 の「HAL の 1p・1a・1b・4c は P3 ③ の厳密な前提」は、入力に触る ③b 以降の前提と読む。
- ② (main 620c5237): PLN1 の像の host_mailbox_commit は、void の宣言のまま無処理にした (stage が NOSYS を返し、kapi_host_notice の本体が先に戻るので commit は呼ばれない)。
- ③a (main 0ab4a885): pm_link_service_once (link-SH の前景の仕事) は段 ⑤ に移した。今の link 像は shell の無い診断像で、USB を組まない (build/targets/ppc-powermac.mk の KBOOT_LINK と KBOOT_SH の排他)。⑤ で link-SH の像に USB を入れるときに、一緒に作って登録する。③a は、PLN1 の像 (KBOOT_SH=1) の terminal_service_once の登録 (platform/powermac/ppc_kernel.c、再入防止つき) と、usb_boot.c の注釈だけを入れた。各像の idle_work の登録数は、PLN1 の像が 1/4 (terminal_service_once)、link の診断像が 1/4 (net_rx_idle)。

- **sys_yield の訂正 (2026-11-15):** §2 の「sys_halt は返値を捨て、sys_yield は返す」(両方を ppc_kapi_wait_once に結ぶ) は誤り。② でそのとおり結んだところ、arch-ctx-guest の lifecycle (exec_start の子が sys_yield で park して state 4 になる) が壊れた (② の a12d0ec8 から、CI の段 2 の準備で発見)。sys_yield は x86 と同じ exec_sys_yield (park の点、idle_work_run を自分で呼ぶ) に戻し、ppc_kapi_wait_once は sys_halt だけに結ぶ。
- **HAL 4c の順の入れ替えで決めたこと (2026-11-15、本線 PM の依頼と Opus のレビュー F3・F4 を受けて PowerPC PM が反映):**
  - 本線は入力の HAL の段の順を 4c-pre → 4c → P3 ③b → 4b にした (Opus 条件付き可)。input_boot.c はこの順で直列に触る。HAL 4c では、PPC は既存の先読み付きの try_key/peek (LAN と USB の交互) を provider の local として登録し、terminal は NULL にする。
  - ③b を ③b-1 (USB の非消費 peek) と ③b-2 (像別登録) に分けた (各 3 ファイル以下)。
  - lan-first の扱い (PM 決定): PLN1 の LAN と USB の交互選択はやめ、bridge の既定 (terminal が先) に従う。理由は 3 つ。共通の bridge に PLN1 専用の互換 profile を足さずに済む。x86 の serial 優先と同じ規則になる。PLN1 は退避像で、新しい機能を足さない方針。LAN が流れ続けると USB が後回しになるが、PLN1 の LAN は人の操作の端末で、流し続ける使い方はしない。問題が出たら profile に戻す。

- **③b-2 で決めたこと (2026-11-15、sol の停止の指摘を受けた PM 決定、本線 PM 了承):** 上の lan-first の項の「bridge の既定 (terminal が先) に従う」は、PLN1 では成り立たない。共通の wait(IL_KEY) は local だけを読むので、LAN を terminal にすると LAN のキーの待ちが止まる。そこで PLN1 は terminal=NULL とし、LAN と USB を 1 つの local (LAN が先、空なら USB) にした。優先の向きは同じ。R4 で trygetchar_local を IL_LOCAL_CHAR にしたため、PLN1 では local の口も LAN を読む。PLN1 は rsh_lan_app_loop を使わないので問題ない (link-SH は ⑤ で local=USB だけにする)。

## 付録 A: 初版の確定設計 (2026-11-14)
### PowerPC P3 詳細設計 (確定) — SH の共通 rshell を link に結ぶ

### A-r1→r2 の変更
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
- 本線 PM の条件 1 (確定の時に改訂): P3 は IO_TIER の N4 の口 (arch/ppc/io_pump.c) の最初の実装を持ち、endpoint はその窓の中で idle_work_run を呼ぶ。
- 本線 PM の条件 3 (PM が反映): 前景の仕事は idle_work に登録し、endpoint は idle_work_run() を呼ぶだけ。EE=0 の syscall との約束の食い違いを §3 の未決に書いた。
- 本線 PM の条件 2: APP 処理を rshell_lan.c に集約し、rshell.c/main.c は呼出し 1〜2 個、x86 build_compare は MATCH を目標にする。
- PM 決定: ESCC と EE の窓を外し、実装段を 6 つとする。

### A-1. 実物の確認・統合判断
- 対象 HEAD は `4e0e8b1e`。作業木は読み取りのみ。以下の行番号はこの版。設計のみで製品変更・ビルド・実行受入はしていない。
- 指定箇所から生成器・SH profile・起動口への参照だけを grep で追跡した。生成済み KAPI の表の存在と provider の存在を区別する。
- 読み取り限定を優先し、ファイルをコピーする `preflight --fix-fonts` は実行しない。実装時は指定環境で preflight を行う。

|食い違い ID|前提・見込みと実物（根拠）|必要な修正|
|---|---|---|
|E1|確定設計 §5:147・§6:190–192 は resident CLI。`build/targets/ppc-powermac.mk` の KBOOT_SHELL と SH は別物|P3 の対象を SH に改訂|
|E2|SH に全ソースは入る (`build/ppc-user.mk:39`) が `userland/ppc/sh/profile.h:4` は SHELL_AS_APP。`userland/shell/main.c:128` は rshell 登録を除外、`rshell.c:364,478,610` は LAN 実行・poll・コマンド入口を除外|単なる KAPI 結線では不可。共通 rshell のアプリ対応が必要|
|E3|`exec/exec_kapi_init.inc:508,559` は代入だけ。PPC の host provider は `sdk/kapi_target_ppc.py:156,251` の absent、notice_serial は `:266` の ZERO_SLOTS、include は `sdk/gen_kapi.py:520` の除外対象|slot・ZERO_SLOTS・include とリンク対象を一緒に直す|
|E4|`rshell.c:445,475,496` は serial 初期化・ser_wd・rsh_serial_follow に依存。sys_halt は `sdk/kapi_target_ppc.py:7` の void 無処理、sys_yield は `sdk/kapi.json:1513` から exec_sys_yield。`arch/ppc/exec_kapi.c:1–7` は表だけで待機 endpoint は無い|APP 入口を別に定義し、PPC halt/yield を同じ新 endpoint に結ぶ|
|E5|SH+LINK は error (`build/targets/ppc-powermac.mk`)。出力先は LINK 優先で `/link` になり診断像と衝突 (`:37`)|組合せを許可し `/link/sh` を分離|
|E6|SH は旧 HostFS も組む (`build/targets/ppc-powermac.mk`)。`hostfs_peer.c:14` は共通 mailbox と同名 host_alive_snapshot、`lan_service.c:98,108` の PLN1 FS 口は link で消える|P3 link-SH は旧 HostFS を組まない。SFS on link は P4|
|E7|`shell_boot.c:33` は TERM_OPEN/USB 打鍵待ち。`input_boot.c:21,31` と `console_stream.c:11` は link 像から外される terminal TU に依存|起動待ち・poll・入力・出力を PLN1 から切り離す|
|E8|`arch/ppc/exec_enter.S:25` は USER を EE=1 で起動し、USER 復帰ごとに DEC が進む。不足は `terminal_input.c:73–78` の USB poll と `lan_service.c:291–299` の bind/mailbox 初期化を含む前景処理|EE の窓は不要。前景処理だけを待機 endpoint から 1 回進める|
|E9|`arch/ppc/exec_reclaim.c:4` の host owner 回収は空|owner 終了・異常終了で handle 回収を結線|
- 食い違いは上表の **9 件**。capture/token は不足に数えない。両者は absent 群に含まれず、生成器の通常経路で実体を呼ぶ (`exec_kapi_init.inc:592`、`con_sink.c:151`、`sdk/kapi.json:2040`)。
- POLICY_DEV §1 の判断①：本線に同じ役目がある。確定設計 §2・§3 と `rshell_lan.c:291` の READY/通知/実行/結果/END を共用する。
- 判断②：**HAL で一実装にする**。wire・ID・世代・capture は共通、PPC は platform_host、有限の前景進行、USB だけを提供する。rshell_link.c は作らない。
- 判断③：link v2・共通 100Hz・一 RX 所有者・共通 exec に合う。ただし「resident CLI を維持する」確定範囲とは異なるため、§7 の判断後に正典を更新して実装する。
- 互換契約は AC-TIME-3、AC-SYNC-2、AC-NET-3、AC-INPUT-1。D03（進行）/D14（close 保留 ACK）の論点は AC-NET-3 に集約されている (`ARCH_COMPAT_CONTRACT.md:108,111`)。x86/PPC に同じ世代・ID・capture・owner 回収試験を当てる。

### A-2. 案の比較と決定
|案|利点|不足・費用|判定|
|---|---|---|---|
|A：rshell_link.c + resident CLI|確定 P3a–c に沿う。小さい診断的 CLI|製品 SH の問題を解決せず、操作状態・結果 capture を二重実装|採らない|
|B：SH + link + 共通 rshell|製品側の解釈器、ID 管理、結果送信を一本化|アプリ対応、KAPI provider、待機・入力の整備が必要|**採用済み**|
|C：SH の fd0 に通知文字列を注入|既存 readline へ入れられる|完全行境界・ID・rc・capture 寿命を別管理し結局 adapter が増える|採らない|
- PM の見込みは統合先として正しいが「そのまま動く」は否。特に SHELL_AS_APP を丸ごと解除して resident 用 SFS/GUI/実行方式を持ち込まない。
- KBOOT_SHELL は PLN1 の比較・退避像として保存し、KBOOT_SHELL+KBOOT_LINK は引き続き拒否。P3 では常駐 CLI の link 端末を作らない。
- PLN1 の既定・既存 SH は保持。P3 選択像だけ link-SH とし、/host は未提供、GUI は未提供。P4/P5 の完了を既定切替の条件として維持する。

### A-3. 部品の境目（断片は提案。生成物は手で編集しない）
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
- **endpoint の窓 (確定、本線 PM の推奨 (b))。** `kernel/idle_work.c:30` は割り込みが禁止なら何もせずに戻る。PPC の syscall は EE=0 で走るので、そのまま呼んでも前景の仕事は走らない (Opus 第 2 回 N1)。そこで ppc_kapi_wait_once は、IO_TIER_DESIGN の N4 の口 (arch/ppc/io_pump.c の「PR=1 から入った syscall の中で EE を一時的に許可して戻す」1 つの口) を通して idle_work_run を呼ぶ。**P3 がこの口の最初の実装になる。** IO_TIER の段の「PPC の pump」は、P3 が作った口を使う。
  - 窓を開ける条件: 外側のフレームが PR=1、kctx_irq_depth==0、trap busy を持っていない、再入していない。条件を満たさなければ、窓を開けずに何もしない。
  - 手順: EE を保存 → EE=1 → idle_work_run() → EE を戻す。idle_work_run と、その既存の呼び手 (x86・exec_syscall.inc:94) と、idle_work.h の約束は変えない。
  - 入れ子の IRQ の安全 (PR=1 から入った syscall はフレームが exec_kernel_sp に積まれ、.Ltrap_busy=0) は、IO_TIER の Opus のレビューで確かめ済み。
  - pm_link_service_once は、自前の再入防止を 1 個持つ。
  - 窓の中では net_rx_idle も本来どおり rx_service を回す (x86 と同じ)。RX を持つのは net_jiffy と rx_service の協調のままとする。
- USER の EE=1 復帰で DEC/net_jiffy/link_tick が進む。前景から RX・jiffy を回さず、link_platform_idle も呼ばない。D14 の close 保留 ACK は owner 回収後も保持し、USER 復帰後に進める。
- **P3 は io_pump の口 (N4) の最初の実装を持つ。** 置き場は arch/ppc/io_pump.c (新)。IO_TIER_DESIGN と ARCH_COMPAT_CONTRACT (AC-NET-3・AC-TIME-4・AC-IO-1) に「P3 が先に作り、IO_TIER の段はそれを使う」と書く (段 ①)。
- N4 の EE の一時許可は、この口だけにする。AC-TIME-4 の単調な µs の時計と、AC-IO-1 の FG1 の協調の進行・再入しない・欠如は NOSYS、を P3 で定義し直さない。

### capture・fd1・入力
- fd1 → 共通 console → con_sink PRINT と FB を維持し、PRINT を二重投入しない。link-SH の console_stream は旧 pm_term_write と直接 clear の ESC 出力（`:49`）を使わない。ESCC の KAPI・出力経路を P3 のために追加しない。
- capture は共通 sink 8KiB と BEGIN/FREEZE/END の owner・両 flag 復元を使う。診断 LOG は capture 中に画面へ再投入せず LOG 経路だけにする（console_log.c:11 の flag 保存だけでは sink push は止まらない）。
- terminal_input.c:73–78 の USB poll と LAN 前景仕事を上記 helper へ移し、link-SH の input_boot は旧 LAN キーデコーダを使わない。USB 半行中は LAN 保留、LAN 通知を文字キューへ feed しない。
- LAN 実行中も USB STOP を残す。PPC input_boot の capture 起点 TB 30 秒期限で文字待ちは Enter、キー待ちは ESC を返す。FREEZE 中も閉鎖を維持し END で解除する。この入力期限は削除した EE 協調窓の期限とは別物。

### A-4. 大きさの見積もりと測定関門
|項目|静的見積もり（KiB=1024B）|採否を決める測定|
|---|---|---|
|link ring / 8 handle payload / TX+RX|8 + 8×3×1400/1024 + 約3 = 約44KiB、管理欄別 (`net/link.c:61,80,117,124`)|SH PLN1 と link-SH の map の text/data/bss 差|
|mailbox heap|64×sizeof(HostNotice)、約16KiB (`host_mailbox.c:9,24`)|実 sizeof と allocator overhead、失敗時 READY 不可の確認|
|sink / shell 結果|既存 sink 8KiB 再利用。これまで GC された rshell 結果などが残るので約8KiB以上を追加計上|sh.elf の bss/静的 buf と root 埋込みサイズを分ける|
|text/rodata・削除分|新 HAL/rshell 活性化は未測定。旧端末/PLN1/HostFS 削除分を差し引く|未測定の圧縮増分を保証しない|
- 計画上の追加 RAM 枠は **64～96KiB 程度＋text 差**。44+16+8KiB を基点に管理領域を見込み、PLN1 削除益は実測前には使わない。正味増分の保証値ではない。
- heap 1MiB の高水位は未測定。字体キャッシュ 295,940B（PPC_NOTES §5）に mailbox、VFS、exec の同時生存分を加える。kmalloc_peak_bytes (`mm/kmalloc.c:267`) を boot/READY/最大結果/再接続/終了反復で記録し、1MiB 未満・割当失敗なしを必須とする。bss と USER heap を kernel heap に二重計上しない。
- map とリンク入力リストで `lan_wire.o/lan_terminal.o/terminal_input.o/hostfs_io.o/hostfs_peer.o` が link-SH に存在しないことを検査。旧 service の PLN1 tx/rx 配列も不在、link_txbuf/link_rxbuf は存在すること。単に未使用 section になっただけを「TU 不在」としない。

### A-5. 実装の 6 段（各段で組める単位、共通 shell は直列）
|段|対象（r1 の ID）|変更と段の検証|
|---|---|---|
|① 文書|D1 LAN_OPERATOR_DESIGN、D2 ARCH_COMPAT_CONTRACT、D3 IO_TIER_DESIGN|案 B 採用。AC-NET-3 の前景の進行は N4 の口の窓で、P3 が N4 の最初の実装を持つ (IO_TIER の段の表を改める)。既定切替条件は保持|
|② provider|H3 device_host.c、H4 exec_reclaim.c、H8 kapi_target_ppc.py、H9 gen_kapi.py、PPC exec_kapi.c/.h、PLN1 mailbox stub、必要な build リンクリスト、T1|halt/yield は当段では従来動作を包む共通 endpoint とし、③で link-SH の helper 呼出しを接続。host/notice・flag provider と stub を同段でリンク。test_kapi_binding.py・test_remote_x9.py を更新し、新 test_ppc_rshell_link.py の provider/owner 試験とともに --mutate。両 PPC 像が組めることを確認|
|③ 前景と入出力|新 arch/ppc/io_pump.c (N4 の口)、H1/H2 link_platform.c/.h、I1 input_boot.c、I2 console_stream.c、I3 console_log.c、I4 shell_putchar.c、S4 shell_boot.c、endpoint、T1|EE 不変で USB/LAN 各 1 回。旧 terminal 依存を除き、capture/STOP/入力期限と fault 後再起動。T1 の前景/入力/capture 変異と既存像ビルド|
|④ 共通 shell（直列）|S1 rshell.c・rshell_lan.c・shell.h、S2 main.c、S3 profile.h|APP init/loop と execute/handoff の専用実体、USB 行と exit 契約。共通の受入済み経路は保持。共通 rshell/capture/owner 試験、APP の行頭/半行/exit 試験、x86 build_compare MATCH を目標|
|⑤ 像|B1 ppc_kernel.c、B2 ppc-user.mk、B3 build/targets/ppc-powermac.mk|SH+LINK 許可、KBOOT_SHELL+LINK 拒否。kernel は /link/sh、remote SH は別出力。ROOT_BINS（build/targets/ppc-powermac.mk）とその依存・再帰 make の出力先も専用 sh.bin へ切替。専用 root の中身を照合し、旧 HostFS/TU を除外。PLN1 SH・link 診断像もビルド|
|⑥ QEMU|T2 sc_run.py、T3 ppc_sh_output.py、T4 link_run.py|7450/7455 の既存機構で §6 を実行。T3 は USB/LAN のみ。新 agent/プロトコルを作らず、変更試験の --mutate と lint を実行|
- 一段を 1 ファイルへ分断しない。②で必要なリンクリストは先に整え、⑤は像選択と専用出力を完成させる。生成器の変更と再生成は②で一体化し、PLN1 SH と link-SH の未定義参照を残さない。
- net/link・kernel/con_sink・exec 共通本体は原則無変更。既存終了口に回収不足が見つかった場合は②に補修と試験を含める。

### A-6. 受入と変異の対応
|受入|試験（H=host、Q=QEMU 7450/7455 両方）|一意 anchor の変異、実行時 RED|
|---|---|---|
|KAPI 実体・不在|T1 H + T2 Q：USER から host/notice/capture/token、PLN1 不在、owner 違い|host を NOSYS に戻す／owner を0固定|
|起動・後起動|T4 Q：打鍵なし起動、HELLO→cui READY、遅延 agent、2秒再READY|TERM_OPEN 待ち復活／READY 再試行停止|
|共通 shell|T2/T4 Q：echo、失敗→echo $?、exit、同ID/古ID/世代変更、過長/複数行拒否|重複判定削除／APP poll 除外|
|前景進行・close ACK|T1 H + T4 Q：EE=0 の syscall の中の halt/yield で、窓の中の idle_work_run 経由で USB/LAN が各 1 回走り、戻った後の EE が元の値・窓の条件を満たさないときは何もしない、bind/READY、USER 待機中の loss/RTO・部分 write・ACK 後 close・全 handle 再利用|endpoint の窓を開けない (EE=0 のまま idle_work_run を呼ぶ。今は何も走らない)／登録の削除／USB poll 削除／close 保留 ACK 消去|
|fd1・画面・LOG|T3 Q：fd1/kprintf/UTF-8/clear、FB 表示、RSH OUT 1回、LOG混入なし|PRINT 二重投入／診断を sink に投入|
|容量・結果の完了|T1 H + T4 Q：3B header 込み境界/超過、EOT、長さ/drop/ID照合、部分結果を成功にしない|drop 比較削除／部分 write の全量送信扱い|
|USB/LAN 調停|T3 Q：半行中LAN保留、USB STOP、30秒でEnter/ESC、終了後復帰|半行中 LAN poll 許可／capture 期限条件削除|
|寿命|T1 H + T2/T4 Q：拒否/切断/exit/USER fault、両flag・reader・全handle回収、SH 自身の fault 後再起動→READY→後続コマンド|END/owner 回収の一方を削除／fault 時 break 復活|
|像・メモリ・既存経路|T2/T4：map/TU検査、heap高水位、診断link/PLN1 SH/常駐CLIの回帰|旧wire TU混入／mailbox割当失敗を注入|
- P5-sc/sh の役目を T2/T3 に寄せ、通信側は既存 link_run の機構を再利用。別プロトコル・別 agent を作らない。x86 は共通 rshell の既存試験と capture/owner 回帰を回す。
- 実装後は専用 SH/root と kernel 像、既存 PLN1 SH、link 診断像をビルドし、変更した試験だけ --mutate、lint。生成 ABI を変更する場合だけ規定の clean/all も行う。全 mutation/check-changed は回さない。
- PPC toolchain/QEMU がない環境は host 部分を必ず実行し guest 部分のみ理由付き SKIP。新試験は PPC_CROSS_DIR=/nonexistent も確認。socket を要する Q は PM が実行し、環境で別の oracle に差し替えない。
- 今回は指定差分の実コード照合・200行制限・変更 ID・差分確認のみ。読み取り専用の設計依頼のため preflight --fix-fonts と実装検査は行わない。ビルド、host/変異、Q、実機、heap/map 実測は未実施。推定を受入済み値にしない。

### A-7. 決定済みの範囲
- 案 B はユーザー承認済み。KBOOT_SHELL は PLN1 退避像、link-SH の HostFS は P4 まで未提供、専用像だけ LAN 待受で起動する。既定切替、認証・番地制限・許可リストは議題に戻さない。
- 反論: 0 件。

### A-確定 (2026-11-14、Opus 5.5 第 1・2 回、本線 PM の条件 1〜3、PM が反映)
- 第 1 回 R1〜R9: r2 で直った (Opus 第 2 回で確認)。
- 第 2 回 N1 (P1): idle_work_run は EE=0 では何もしない (idle_work.c:30)。endpoint の窓を N4 の口で開ける形に直した。
- 未決 (idle_work の約束と EE=0): Opus は (a') (IF を問わない別の入口 idle_work_run_coop) を推奨、本線 PM は (b) (N4 の口で窓を開ける) を推奨した。PM は (b) に決めた。理由: idle_work の前提 (IF/EE=1・IRQ の外) を x86 と PPC でそろえたまま Tier へ育てられる。EE=0 のまま仕事を回すと DEC が止まり tick を落とす。Opus が (b) を退けた理由は本線 PM の条件 1 (io_pump を使わない) だけで、その条件は本線 PM が改めた。Opus の (a') のうち、idle_work_run と既存の呼び手を変えないこと、pm_link_service_once の再入防止、N1 の変異は取り入れた。


### ⑥ で決めたこと
- 台本は `tools/ppc/link_sh_run.py`。診断像の link_run の観測と 192KiB 閾値を保ち、HostAgent・OperatorSocket・RSHELL/EXECUTE・PumpQMP・USB の既存注入を再利用する。
- link-SH は heap 高水位 1MiB 未満・割り当て失敗 0。試験用コピーだけに FREEZE の戻り値・END の回数・割り当て失敗の観測変数と USER 自身の fault/UTF-8/容量の刺激を入れる。製品の像へ試験用コマンドを追加しない。
- link-SH では診断の PDIAG (link_diag、65532) を動かさない (lan_service.c の `#if !KBOOT_SH`)。RSH READY は APP の shell だけが mode=cui で出し、診断の命令は診断像 (KBOOT_LINK だけ) の役目にする。⑥ の受入で、PDIAG の READY (diag) と shell の READY (cui) が同じ boot から交互に出て operator の ready が入れ替わるのを見つけた (本線 PM 了承、2026-11-15)。
- rshell_lan.c の con_sink_stat の pending は、NULL ではなく受け皿を常に渡す。CPL=3 の APP の shell では KAPI の出力の検査が NULL を拒み、sh.bin が畳まれていた (⑥ の受入で発見、ユーザーと本線 PM の決定で x86 も同じ形にした)。
- 重い QEMU は `ppc-link-sh-run` の full_only。通常 CI の時間増分は 0、両 CPU の正常・遅延 agent の後で変異を実行する。socket 不可は未 (rc=3)、包みは非必須環境だけ rc=0 に写す。
- 受入: 未。証拠・PM の実行コマンド・host/像の検査は `/home/hight/os32-tmp/run/ppc-p3-6/RESULT.md`。QEMU と socket の合否は PM が記録する。R1 は不要 (製品の実行モデル・メモリ配置を変更しない)。
