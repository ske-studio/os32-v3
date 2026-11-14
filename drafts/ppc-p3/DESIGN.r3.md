# PowerPC P3 確定設計の改訂 r3 — 共通 SH・FG1・文字端末 bridge

## final→r3 の変更
- C1: W4c 済みの syscall FG1 契約を採用し、endpoint の EE 窓・arch/ppc/io_pump.c・N4 先行実装を削除。
- C2: 入力の選択・lookahead・decoder・待機 policy を共通 bridge に寄せ、PPC は provider と登録に縮小。
- C3: link-SH の LAN は mailbox コマンドであり文字注入ではないと明記し、USB local の try で行頭を調停。
- C4: HAL r2 の mode・peek・park・capture 期限の不足を具体化し、bridge 先行を推奨。
- C5: 6 段と受入表を更新し、syscall 入口と endpoint の各 idle_work 呼出しを区別。

## 1. 範囲と実物の確認
- 基準は main `4adf1a4a`（HEAD 一致を確認）。作業木は読み取りのみ、出力は本票だけ。
- 材料は `in/P3_DESIGN.final.md` と `in/INPUT_HAL_DESIGN.r2.md`、同レビュー r1・r2 依頼。以下「final」「HAL r2」はこれらを指す。
- 案 B、専用 link-SH、共通 rshell_lan.c/rshell.c、PLN1 退避像、HostFS 未提供、既定切替の条件、公開 ABI 不変更は final のとおり。
- final §1 の E1〜E9、§2 の採用理由は維持。ただし EE=0 の syscall という根拠を本節で置換する。
- `docs/decisions/ARCH_CTX_DESIGN.md:44`（AC-CTX-1）どおり、`arch/ppc/kernel_trap.c:104` の USER syscall 分岐は `:107` で許可、`:108` で dispatch、`:109` で出口処理のため禁止する。
- IRQ の深さは同 `:94–101` の外部/DEC handler だけで増減する。通常 syscall は IRQ 外、途中の IRQ は深さを戻して復帰する。
- `kernel/idle_work.h:4` の「許可状態・IRQ 外」を満たす。実装 `kernel/idle_work.c:30–36` は許可・深さ・再入を確認して各登録を 1 回呼ぶ。既存のガードは変更しない。
- `exec/exec_syscall.inc:94` の入口にも idle_work_run が既にある。W4 後は PPC でも有効であり、前景進行が halt/yield のみに限定されるとは扱わない。

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
- pm_link_service_once を初期化完了後に idle_work_register する。USB と pm_lan_poll(now) を各 1 回、有限処理・自前の再入防止・許可状態復元は final のとおり。
- endpoint 1 回につき登録処理 1 周。halt/yield syscall 全体には入口の 1 周もあり、通常は計 2 周になり得る。受入で「syscall 全体で各 1 回」と誤判定しない。
- host/notice/status/入力 provider 自体から pump は呼ばない。それらの syscall に共通入口の進行があることとは区別する。
- net_rx_idle と helper の 2 枠は final のとおり。HAL の入力 work が登録済みなら総枠数と登録結果を再確認し、USB poll の実行所有者を一本化する。
- HAL が USB 進行を既存 work に移した場合、helper はその登録を重複せず LAN のみ担当する。単なる入力読出しから USB を再 poll しない。
- DEC/net_jiffy/link_tick は USER 復帰後だけでなく FG1 syscall 中も進む。前景 helper で RX/jiffy を直呼びせず、net_rx_idle の既存 rx_service と協調する。
- close 保留 ACK、link_platform_idle を呼ばない方針は final のとおり。IO_TIER と AC-NET-3 の文書は AC-CTX を参照し、P3 に N4 窓を作らせる記述を残さない。

## 3. bridge の境界と状態所有
- **推奨は bridge 先行**。HAL の 1p/1a/1b と必要な wait/capture の契約・実装を P3③の前提にする。in2 公開 ABI・mouse・pad の完了は待たない。
- HAL r2 の InputLegacyProvider は terminal/注入/park の口であり、USB local の口はまだ宣言されていない。PPC の文字キューを移す前には下記の一時的な local provider が必要。
- 下の追加宣言と mode は内部契約案。HAL 側で同名相当を確定し、P3 と別々の型を作らない。InputOps と SDK の署名は不変。
```c
/* kernel/input_legacy.c が使用する共通内部口。local は cooked u16 または -1。 */
struct InputLegacyLocal {
    int (*try_key)(void);
    int (*peek_key)(void); /* ring/decoder/選択順を進めない */
};
enum InputLegacyMode {
    IL_CHAR, IL_KEY, IL_LOCAL_CHAR
};
int input_legacy_try(int mode);
int input_legacy_peek(int mode); /* HAL r2 §3 に補う */
int input_legacy_wait(int mode, int interruptible);
/* GUI wait 用の park_kbd(void) を InputLegacyProvider に追加する。 */
/* 成立時は復帰せず、再開値は byte。通常 return は park 不成立だけ。 */
```
- PPC の移行中は local.try_key を pm_input_usb_take に結び、非消費 peek を USB キューに追加する。独自 lookahead で take を peek に偽装しない。
- HAL 4b 後は共通 cooked ring を local に結び、PPC USB は usbp_input_key/release → input_capture の既存 HAL 設計に従う。PPC に文字キュー/押下表/文字化の複製を残さない。
- provider 登録は起動時の常駐関数ポインタ。NULL は空/不在であり、共通部から PPC・serial・exec の装置固有 symbol を参照しない。
- **link-SH では terminal_try/peek と inject_try/peek は NULL、terminal_enabled は常に偽**。LAN notice は host_mailbox → rsh_lan_poll へ渡す。PPC LAN だから terminal_try に結ぶ、という一律登録はしない。
- PLN1 の旧文字端末を残す像だけ terminal provider に LAN byte stream を登録する。GUI 注入は別の宛先付きキューであり、LAN notice を inject_try へ載せない。
- 旧 PLN1 に必要な交互選択・lookahead・escape decoder は共通 bridge の互換 profile に移す。link-SH にはその profile/decoder を有効化しない。x86 の serial 優先を全像共通の交互選択に変えない。
- 現行の状態は `platform/powermac/input_boot.c:8–12`、実際の交互選択は `:28–43`。terminal_input.c は byte ring と poll を持つ。移動元を取り違えない。
- 共通 bridge が選択状態・decoder・peek の整合・capture 入力期限・GUI policy を所有、HAL が物理状態/ring/keymap を所有する。provider は装置の byte/cooked 読出しだけ、SH が半行/ID/実行状態を所有する。
- PLN1 decoder は有限の採取段で cooked を用意し、peek は完成済みの先頭を照会するだけ。peek による byte 消費・decoder 更新・交互選択の更新は禁止し、try 成功時にのみ選択を進める。
- **P3 が先の場合**も上の共通内部口を先に置き、bridge の必要な最小部分を kernel/input_legacy.c に実装する。input_boot.c は provider 登録と InputOps の薄い委譲だけにする。
- 後着 HAL はそのファイルを引き継ぎ、local の供給元を共通 ring に交換する。別名の PPC bridge を増設しない。HAL 1p/1a/1b/4c の重複範囲と試験は取り込み時に統合する。

## 4. 口ごとの振る舞いと LAN 行頭の調停
| 口 | bridge 契約と P3 の使い方 |
|---|---|
| try(IL_LOCAL_CHAR) | CUI の USB/local のみを非待機で消費。GUI は -1、注入/terminal/park を見ない。kbd_trygetchar_local をここへ結ぶ |
| try(IL_CHAR/IL_KEY) | GUI は注入だけ、空なら park_poll を 1 回試して通常復帰時 -1。CUI は profile に従う。capture 中は terminal を閉じ、local STOP は残す |
| peek | try と同じ選択対象/順番を非消費で見る。park・pump・deadline 合成文字の消費なし。反復 peek で先頭/選択状態が変わらない |
| wait(IL_CHAR) | CUI は local 優先。x86 の rshell serial 条件と非 capture の空白 timeout は維持。link-SH は USB と中断/期限を検査し FG1 で有限 idle_work→再検査 |
| wait(IL_KEY) | x86 の現行どおり serial を読まない（kbd.c:789）。link-SH も local のみ。interruptible の中断は -1、捕捉期限は ESC |
| GUI wait | 注入を即返す、空なら park_kbd。成立時の再開値は注入 byte、try の park_poll は byte または -1。peek/has_key/local は park しない |
- HAL r2 の mode は文字/キー/ローカルという口の区別。GUI 状態や capture 状態を呼び手の任意指定にせず共通側で読む。wait を try の一律ループにしない。
- rsh_lan_app_loop は **try(IL_LOCAL_CHAR)**（既存 KAPI 名 kbd_trygetchar_local）を 1 回先に呼ぶ。文字があれば rsh_line_feed、半行中は rsh_lan_poll を呼ばない。
- 行頭で local が空のときだけ rsh_lan_poll を 1 回呼ぶ。半行の完了/取消後に行頭へ戻る。LAN を保留しても kernel の通信・ACK・mailbox 採取は止めない。
- 調停に wait を使うと LAN 到着時に USB 待ちから戻れず、peek→別経路で consume すると由来が曖昧になるため、app_loop は local try に統一する。待受無入力は sys_halt、LAN AGAIN は sys_yield。
- 半行・126B 制限・exit N・LAN EXIT・reset/flag 復元・後起動 READY は final のとおり。
- capture は BEGIN 起点の 30 秒という final の P3 契約を維持し、共通 bridge に期限を移す。各 wait 開始ごとに延長しない。FREEZE 中も閉鎖、END/owner 回収で解除する。
- HAL r2 の「旧期限維持」と混同しない。x86 現行は wait 開始 tick 起点（kbd.c:754,792）なので、P3 の BEGIN 起点は bridge の link-SH profile として明示する。
- BEGIN の正確な時刻を初回 read で推定しない。共通内部通知 `void input_legacy_capture_changed(int active)` を成功した BEGIN/END/owner 回収に結ぶ（FREEZE は維持）。bridge が開始時刻/有効状態を持つ。
- これは final の con_sink 原則無変更に対する最小例外。`kernel/console/con_sink.c:158,171,342` の遷移確定後、sink の錠を外して通知し、失敗した BEGIN/END では通知しない。通知は有限・待機なし。
- 時計は共通単調時間で 30 秒を比較し、PPC は platform_clock_read_us の TB 由来値を使える。100Hz tick を ms と取り違えず、wrap 差を試験する。期限後の文字 wait は CR、キー wait は ESC、try/peek は非待機の既存結果を維持。
- STOP/中断を期限より先に確認し、capture 終了・fault 回収後に次の session へ期限を持ち越さない。sink 本体の 8KiB/reader/flag/PRINT と出力の契約は final のとおり。

## 5. 実装の 6 段（final の表を置換）
| 段 | 対象 | 変更と検証 |
|---|---|---|
| ① 文書・境界 | LAN_OPERATOR、ARCH_COMPAT、IO_TIER、HAL bridge 契約 | 案 B は維持、N4/EE 窓を削除。HAL と local/peek/wait/capture の担当・先行範囲を確定。公開 ABI は変えない |
| ② host/KAPI provider | device_host、exec_reclaim、kapi_target_ppc.py、gen_kapi.py、exec_kapi.c/.h、PLN1 stub・リンクリスト | final の host/notice/flag/owner 結線と生成を一体化。wait endpoint は直接 idle_work_run。binding/remote_x9/T1 の provider/owner 変異と両 PPC 像ビルド |
| ③ bridge・前景・入出力 | 共通 input_legacy/input_wait、PPC input_boot、link_platform、console_stream/log、shell_putchar、shell_boot、capture 通知 | bridge 先行版を使用（未着なら最小共通部を先に実装）。独自入力状態を除去、有限 work 登録・期限/STOP・fault 再起動。HAL と共同 fixture、T1 変異、既存像ビルド。io_pump.c は追加しない |
| ④ 共通 shell（直列） | rshell.c/rshell_lan.c/shell.h、main.c、profile.h | final の APP 専用実体。local try の行頭優先・半行 LAN 保留・exit。共通 rshell/capture/owner 回帰、x86 build_compare MATCH を目標 |
| ⑤ 像 | ppc_kernel.c、ppc-user.mk、ppc-kboot.mk | final の専用 sh.bin/root・出力先・ROOT_BINS 依存、SH+LINK 許可、KBOOT_SHELL+LINK 拒否。旧 TU/HostFS 除外、bridge/provider のリンク閉包と map を確認 |
| ⑥ QEMU | sc_run.py、ppc_sh_output.py、link_run.py | 7450/7455 で下表。新 agent/プロトコルなし。変更試験の --mutate と lint、HAL 共同試験を重複作成しない |
- ③の入力抽出は HAL の小段を合流させる統合単位。巨大な一括差分にせず、各小段で組める状態を保つ。共通 shell は final のとおり直列。
- host_notice 共通本体無変更、serial_* 不在、必要 flag だけ実体化、正常/異常の owner 回収、生成物手編集禁止は final のとおり。

## 6. 受入と変異（H=host、Q=QEMU 7450/7455 両方）
| 受入 | 観測する合格条件 | 一意 anchor・実行時 RED の変異 |
|---|---|---|
| FG1/endpoint | H+Q：USER halt/yield 本体は EE=1・深さ0、endpoint 内は 1 周、入口分は別計数。IRQ 後も復帰、FG0/IRQ/再入は仕事なし | W4 入口 enable 除去は既存 CTX 試験へ、endpoint 呼出し削除は P3 試験へ。自動 enable 混入も拒否 |
| 前景・close ACK | H+Q：各 work の USB/LAN 有限進行、二重登録なし、bind/READY、FG1 中 DEC、loss/RTO/部分 write、ACK 後 close/再利用 | work 登録・USB 進行除去、保留 ACK 消去 |
| bridge 非待機 | H：GUI空+terminal有でも -1/park1回、local は park/注入なし、peek反復でキュー/選択/decoder不変、次tryが同値 | GUI fallthrough、peek消費、localへのLAN混入 |
| bridge 待機 | H+Q：char/key の優先と serial 有無、中断=-1、park再開byte/-1、FG1進行。GUI実動作は可能な像で別受入 | waitをtryループ化、park再開値破壊、中断無視 |
| capture 入力 | H+Q：BEGINから30秒、複数waitで延長なし、FREEZE閉鎖、CR/ESC、USB STOP、END/owner回収後復帰 | waitで期限再設定、FREEZEで解除、回収通知除去 |
| USB/LAN 調停 | Q：同時到着はlocal先、半行中LAN実行なし/通信は進行、行完了/取消後実行。LAN notice は文字キューに一度も入らない | 半行中poll許可、noticeを文字feed |
| KAPI/起動/共通shell | final の T1/T2/T4：host/notice/capture/token/不在/owner、打鍵なし起動、遅延agent、2秒READY、echo/rc/ID/世代/exit/長行 | final の NOSYS化・owner=0・TERM_OPEN待ち復活・重複判定削除 |
| 出力/容量/寿命 | final の T1/T2/T3/T4：fd1/FB/UTF-8/clear、PRINT一回、LOG非混入、境界/drop/EOT/部分結果、fault後READY、両flag/reader/handle回収 | final の PRINT重複・drop比較削除・END/回収除去・fault break |
| 像/メモリ/退行 | final の map/TU除外・heap高水位、PLN1 SH/診断link/常駐CLI/x86回帰。bridge追加分と旧入力削減分を実測 | 旧wire TU混入、mailbox割当失敗。HAL profile切替によるPLN1入力退行も検出 |
- final §4 の RAM 64〜96KiB＋text は概算のまま。bridge/HAL の共有増分を map で区別し二重計上しない。heap 1MiB 未満・割当失敗なしの条件は維持。
- 実装時のビルド、変更試験 --mutate、CHECK 入力宣言、移動 anchor の移設、lint、PPC ツール欠如時の理由付き SKIP、socket Q は PM 実行、全変異/check-changed を回さない方針は final のとおり。
- 本回は文書と実コードの照合・行数・出力確認のみ。読み取り指定を優先し preflight --fix-fonts、build、lint、host/変異、QEMU、実機、map/heap 実測は未実施。実装受入済みとは扱わない。

## 7. 未決
- ユーザー判断が必要な未決は 0 件。bridge 先行を推奨し、未着時の共通最小実装も上記で定める。HAL 側への契約補足は次節の技術調整とし、案 B 等の承認済み範囲を再審議しない。

## 入力の HAL への意見
- H1: `in/INPUT_HAL_DESIGN.r2.md:66–86` は local provider・mode の値・bridge peek の宣言が不足。USB キュー移行前の local try/peek と IL_CHAR/KEY/LOCAL_CHAR、共通 peek を本票 §3 の形で補ってほしい。
- H2: `in/INPUT_HAL_DESIGN.r2.md:140–142` の「PPCはLAN」と交互選択移植は像別に限定が必要。PLN1 の文字端末と link-SH の mailbox コマンドを分け、後者は terminal/注入不在・local try＋shell 行頭調停とする。
- H3: `in/INPUT_HAL_DESIGN.r2.md:69,139,142` の非消費 peek と旧lookahead移植を両立させる採取点が未定義。現行 `platform/powermac/input_boot.c:31–37` はpoll/take/選択更新をするため、そのまま移さず有限採取と純粋peekを分ける。
- H4: `in/INPUT_HAL_DESIGN.r2.md:71,141` はpark_pollのみ宣言しGUI待機のpark_kbdがない。成功時非復帰・try再開byte/-1・wait再開byte・通常return時のfallbackをprovider契約に追加する。
- H5: `in/INPUT_HAL_DESIGN.r2.md:141,174,185` はcapture期限の起点/所有者が曖昧。x86待機起点とP3 BEGIN起点30秒をprofileで区別し、FREEZE維持・END/owner回収通知・期限後CR/ESCを試験する。
- H6: `in/INPUT_HAL_DESIGN.r2.md:139–141` のGUI早期復帰はlocal-onlyの例外を明示し、GUI local=-1/parkなし、getkey_waitはserialなしを口別fixtureへ追加する（`drivers/input/kbd.c:705,789`）。
