# ARCH_CTX_DESIGN — 実行文脈ごとの約束 (ARCH_COMPAT W4、AC-CTX)

> 状態: **現行 (2026-11-14)** — 確定 (astra 起草 r1、Opus 5.5 の独立レビュー Approve、PM 決定を反映)。段の状態は票 TASK_ARCH_CTX が持つ。
> 出所: 往復の版は `~/os32-tmp/run/arch-ctx-w3/` (PACK、PPC_FACTS、DESIGN.r1・final)。ユーザー指示 2026-11-14「b であとあと他のアーキテクチャの追加で揺れていきそうですが。トータルでの解決策は考えておくべきでは？」「powerpc を待たせることで解決できるのであれば待たせたほうが良いです」。


- 前提: 単一 CPU・単一タスク・協調切替・整数演算。セキュリティは対象外。Win32 互換にも同じ前景契約を使う。
- 結論: **通常のユーザー syscall 本体を FG1 に統一する。EE/IF の許可は arch の入口が所有し、I/O の口では操作しない。IRQ 出口で仕事は回さない。** P3 は下記受入まで停止。

## 1. 統合判断と実物

|POLICY_DEV §1 の判断|採る方針|
|---|---|
|同じ役目はあるか|x86 syscall の IF 許可、共通 idle_work、IO_TIER §2/N4、LAN_BULK §5/J が同じ進行問題を扱う|
|共通化の境界|入口・フレーム・復帰は arch、仕事と待ちの反復は共通。PPC 専用 EE 窓を追加せず、AC-CTX を先に正典へ足す|
|既存計画との整合|Tier 初版 FG のみ・LAN の単一所有・協調切替を維持。確定済み N4 の手段を本設計で置換し、下記の段と受入を経て実装する|

- **FG0/FG1 を呼出元の区別へ改名しない。** 正典 `docs/decisions/ARCH_COMPAT_CONTRACT.md:26-28` の表は FG0=前景で禁止、FG1=前景で許可。ユーザー syscall も排他中は FG0、通常カーネル前景も許可中は FG1。
- 以下の「IRQ 入れ子」はマスク可能 IRQ。同期例外・NMI 等を「禁止なら来ない」と扱わない。実コードの静的確認であり、動作合格ではない。

|文脈|x86 の現状（file:line）|PPC の現状（file:line）|
|---|---|---|
|IRQ 本体|`arch/x86/kernel/isr_stub.asm:455` の入口・出口に深さ加減。スタブで STI しない。通常 IF=0、IRQ→IRQ を許可する作りではない|`arch/ppc/kernel_trap.c:91` は EE=0・IR/DR=1 を検査、`:98` が深さ加算。PR=0 入口は `kernel_vectors.S:319` の固定 trap slot を取得、再取得は `:332` で停止。IRQ→IRQ 不可|
|IRQ 出口|`isr_stub.asm:467` / `:533` で深さを下げ IRETD_USER。前景仕事なし。保存 IF を復帰、深さ0でも復帰命令前は前景ではない|`kernel_trap.c:100` で深さを下げ、`kernel_vectors.S:461` から busy 解除・SRR 復元、`:520` / `:573` の rfi。前景仕事なし。保存 EE を復帰|
|FG0 と通常カーネル前景|FG0 は定義上 IF=0。通常の CPL0 前景で IF=1 なら IRQ は現スタックへ入り戻れる（`isr_stub.asm:459`）。呼出元だけで IF=1 と推定しない|FG0 は EE=0。trap slot 未使用の通常前景は IRQ を別 slot へ受けられる（`kernel_vectors.S:311`）。既存 `kernel_dec.c:64` の idle は EE を立て永久 spin。PR=0 全体が割込不能ではない|
|ユーザー syscall 本体|`arch/x86/kernel/ring3_entry.asm:35` の pushad で保存、`:53` の STI 後 dispatch、`:67` の CLI 後復帰。syscall→IRQ 可。V86 分岐は STI を迂回する|`kernel_vectors.S:340` の exec 用 stack に保存、`:397` に SRR1、`:447` から翻訳を許可するが EE は0。`kernel_trap.c:104` の PR=1/syscall 分岐も許可せず `:107` の共通 dispatch へ。現状は FG0|
|sys_halt / sys_yield|`kernel/sys.c:39`、`exec/exec_schedule.inc:142` は poll→idle_work→halt/park。通常 syscall なら IF=1、直接 CPL0 呼出は呼出時の IF 次第|同じ共通コードだが現在 syscall からは EE=0 のため idle_work は無処理。`kernel_dec.c:70` の halt は EE 保存の nop で、IRQ を待って眠る処理ではない|
|例外|`isr_stub.asm:153` / `:171` / `:209` の CLI、`:98` の例外深さ。IF=0 でも同期例外は起こり得る。前景仕事なし、復帰/中断経路別に深さを戻す|`kernel_vectors.S:324` の独立 spill slot で DSI/ISI を処理。解決できない PR=0 fault は `:421` 以降で trap slot に移し、使用中なら `:427` で停止。`kernel_trap.c:110` の user fault は kill。一般的な例外の自由な入れ子ではない|

### たたき台・参照と実物との食い違い（5件、変更予定そのものは除外）

|番号|食い違いと訂正|
|---|---|
|D1|PM 案の「FG1=ユーザー、FG0=カーネル」は正典の割り込み状態による定義と異なる。由来は別軸にする|
|D2|指定の `arch/x86/ring3_entry.asm:53` は所在違い。実体は `arch/x86/kernel/ring3_entry.asm:53`|
|D3|指定の `arch/ppc/kernel_mmu.S:38` は EE を立てる箇所ではない。EE/IR/DR の消去 mask を作り `:39` の andc で落とす。`:75` は元 MSR の復元|
|D4|「PPC は入口で EE を立てるだけ」は不足。`kernel_trap.c:91` が EE=0 を要求し、`kernel_vectors.S:465` の出口 mask に EE がない。許可位置・復帰の両方を整合させる必要がある|
|D5|PPC の IRQ 深さは現状、DEC 1回でも dispatch `kernel_trap.c:98` と handler `kernel_dec.c:58` で2になる。「正なら IRQ」は成立するが、値を入れ子段数として読むと誤る|

## 2. 契約（正典 §3 に領域 CTX、§4 に本表を追加）

|ID|G: 保証|C: 能力|A: 不在・禁止|
|---|---|---|---|
|AC-CTX-1|通常ユーザー syscall は arch が完全なフレーム・安全な kernel stack・翻訳を用意してから IF/EE を許可し、共通 dispatch に FG1 で渡す。短い排他は保存/復元し、正常本体終了時も FG1|入口形式・保存方法は arch。V86/起動/異常例外は別入口|入口未適合の像を FG1 必須の共通サービスへ接続しない。個別 I/O で不足を隠さない|
|AC-CTX-2|FG1 を IRQ が中断し、レジスタ・PC・stack・翻訳・許可状態を保って戻れる。IRQ 本体では待機/前景 dispatcher を呼ばない|IRQ→IRQ の再許可は別能力。初版 x86/PPC とも提供なし|深さ0だけで IRQ 出口/例外を前景扱いしない。IRQ の再許可を共通コードが要求しない|
|AC-CTX-3|通常 kernel 前景は各入口に許可可能条件を宣言。FG0 の排他区間を勝手に FG1 へ変えず、所有者が区間終了時に復元する|起動完了・stack/root 有効・trap slot 非保持で IRQ を受けられる kernel 前景を arch が申告。PPC はこの限定範囲だけ、PR=0 trap 内は含めない|能力なしの kernel 経路では idle_work は何もせず戻り、次の FG1 協調点で回る。待ちしか進行手段がないサービスは接続しない|
|AC-CTX-4|idle_work/Tier は FG1・IRQ外・通常前景の協調点のみ。再入は仕事なし、予算/単一所有は既存設計どおり。callback は許可状態と所有を復元して戻る|登録サービスと予算は IO_TIER の能力|IRQ出口/例外/排他中に実行しない。idle_work の既存 void 無処理を保持。結果付き口は各既存エラー/再試行規則に従う|
|AC-CTX-5|sys_halt/yield・I/O待ちは FG1 で条件再検査を繰返す。IRQ・共有busy/lock保持・再入状態で待たない。終了/park/kill もフレーム・深さ・所有状態を整合|halt の睡眠は任意、PPC の nop も可。FG0 poll 待ちは AC-SFS-1 のような時計＋transport の個別保証がある口のみ|FG0 の一般的待ちを新たに保証しない。I/O の口で FG1 不足を自動 enable して救済しない。AC-SYNC-2 の原始操作（_idle、link_platform_idle の _enable、`platform/powermac/link_platform.c:23`）の許可は別枠で残す。trap busy を持つ文脈（PR=0 の trap dispatch）と IRQ からは呼ばない。無能力は型別エラーで要求前に返す|
|AC-CTX-6|起動完了後の正常 FG1 syscall/待ち/ユーザー計算中、timer IRQ を抑止し続けない。短い FG0・IRQ処理から有限時間で許可文脈へ戻す。PPC は DEC 再arm・経過分 step を維持|物理源/停止中に読む時計は AC-TIME-1/2/4。マスク中も µs が進むことと tick 配達を区別|故障・stop・無限排他・IRQ占有下で時刻進行は保証しない。長時間禁止中の x86 tick 全回収を保証しない|
|AC-CTX-7|入口/出口・同期例外の所有は arch。共有 scratch/SRR/stack の再利用前に保存し、復元途中は IRQ 禁止。irq_depth は一つの IRQ entry に一度加減する。onfault の arm から clear までは irq_save で囲み終了後に復元する（`platform/powermac/lan_monitor.c:71-78` の pm_lan_copy8 の型）。probe は1個の大域（`arch/ppc/kernel_trap.c:15`・`:57`）なので IRQ handler の onfault と衝突させない|同期 fault の回復範囲・専用 slot 数は arch。PPC は独立 spill 1段|spill 失敗＋trap busy 等の非対応入れ子は既存 fatal。例外を処理するために EE を許可しない。trap busy 保持中に EE が閉じる根拠は、例外入口で CPU が EE=0 にすること（`kernel_vectors.S:319-336`・`:419-437`）と、許可点が PR=1 の 0xc00 の枝だけであること|

- 各口の表記例: syscall `呼出=ユーザー; 進行=FG1:IRQ＋協調仕事; 復帰=保存ユーザー状態`、wait `呼出=FG1; 進行=IRQ＋有限poll; 復帰=FG1・条件再検査`。
- C は CPU 名から推測しない。初版は arch ごとの入口条件を適合表へ記載し、共通実行器は FG1 契約だけを使う。能力を動的に選ぶ利用者ができた段で内部照会口を追加し、公開 KAPI の能力と混同しない。
- IF/EE=1 は tick を進めるための条件であり、Tier の自動実行保証ではない。syscall/yield を呼ばない計算中でも tick は進むが、Tier は止まり得る。LAN の限定 IRQ 救済は従来の別契約。

## 3. PPC 移行（PR=0 trap の多段入れ子は作らない）

|変更対象|内容・目安差分|安全条件と危険|
|---|---|---|
|`ppc_trap_dispatch`（`arch/ppc/kernel_trap.c:87`）|20〜40行。入口の EE=0 検査は維持。PR=1 かつ vector=0xc00 の枝内、arch_syscall 準備後・共通 dispatch 直前だけ enable、正常 return 前 disable|全フレーム保存と翻訳許可が済んでいる。先に assembly 全入口で enable すると入口検査 fatal、IRQ/例外内まで許可して trap busy に衝突する|
|`ppc_kernel_vector_return`（`arch/ppc/kernel_vectors.S:461`）|10〜25行。出口先頭で EE も落としてから翻訳停止・busy解放・SRR復元。冒頭コメントも更新|C の disable に加え、全復帰経路の不変条件を assembly で固定。EE を残すと復元中 IRQ が SRR/SPRG を上書きし停止/誤復帰し得る|
|`ppc_decrementer_interrupt`（`arch/ppc/kernel_dec.c:42`）|5〜15行。深さの所有を trap dispatch に統一、handler の重複加減を撤去|handler の直呼び試験は dispatch 経由へ合わせる。DEC の負値へ周期を加える処理、正値 pending の無 tick、再arm順は保持|
|`exec/exec_syscall.inc:90`|5〜15行。PPC EE=0 で無処理という注記を共通 FG1 契約へ置換|入口の idle_work が PPC でも実行されるので、SH の登録者・初期化順・IRQ共有状態を受入で確かめる|

- PR=1 syscall は `ppc_exec_kernel_sp` 由来のフレームを使い、`.Ltrap_busy` を取得しない（`kernel_vectors.S:340`）。本体実行時は CPU は PR=0、そこで来た IRQ は `:319` の trap slot を取得する。IRQ の frame は `.Ltrap_top`（16KB、`arch/ppc/ppc_frame.h:14`）に載り、exec_kernel_sp を使わない。この「ユーザーフレーム＋trap slot」の分離が一段の syscall→IRQ を可能にする。
- IRQ は EE=0 のまま終わり、rfi で中断した syscall の EE=1 を復元する。IRQ 内で別 IRQ は受けない。IRQ 内の DSI/ISI は spill slot で解決できる範囲だけ許す。失敗時は使用中 trap へ移れず fatal のまま。
- `.Lscratch` と SPRG は再入前に各 frame へ退避済みであること、IRQ のために `ppc_exec_kernel_sp` を更新しないこと、ユーザーフレームの非重複をゲストの sentinel で確認する。
- PR=0 の通常 kernel 前景は trap slot を保持しない場合だけ許可できる。PR=0 trap dispatcher からの enable は能力として申告しない。起動中・root切替中も許可しない。`kernel_mmu.S` は現状の MSR 保存/復元を維持する。
- **非局所出口を接続ゲートにする:** sys_exit・kill・park は通常 return の disable を通らない。実装発注時にその呼出先と exec stack/root 復帰を限定追加調査し、保存 IF/EE・depth・busy・再開 frame を復元する既存経路を確認する。指定範囲外なので本稿は適合済みとしない。
- 同じく、新たに IRQ が入り得る syscall 内で IRQ と共有する状態・root/exec_kernel_sp の変更箇所を棚卸しする。必要な短い irq_save/restore は個別補助段へ。全 syscall を再び EE=0 で包まない。

## 4. 既存設計の書き換え

|正典/箇所|差し替える文・依存|
|---|---|
|ARCH_COMPAT §2/3/4・`:74`（AC-SYNC-2 PPC列）・`:136`（既存W3）|FG0/FG1 の意味を保持し、由来と状態を分離する注記、CTX 領域と上表を追加。AC-SYNC-1/2 は保存復元/idle復帰を保持し、AC-CTX-3/5 を参照。PPC列は原始操作の許可を別枠とし、syscallのEE窓を解決策にしない|
|`docs/decisions/ppc/PPC_NOTES.md:32`|「USERのsyscallはEE=0でtick_countが止まる」を移行前の観測とし、W4適合後はsyscall本体FG1・tick進行へ更新|
|AC-TIME / AC-NET / AC-KAPI|TIME は AC-CTX-6 への参照を追加、物理/論理周期を変更しない。NET-2 の単一所有・NET-3 の RTO/ACK は維持、EE許可だけで FGサービス進行を保証しない。KAPI-1 の不在型・slot・ABI は変更なし|
|IO_TIER `:9`（R1）、§2 `PPCの既存syscall本体はEE=0を既定` 以下3段落、`:145`（set/get/reset）、`:188`（P1）、`:210`（試験）、確定 N4（`:235`）|「AC-CTX-1 適合入口から共通 FG1 実行器へ。ppc_syscall_fg_run による許可/復元は不要」に置換。専用 EE 窓の段は削除。C4/P1 は必要な ABI adapter が残る場合だけ扱い、予算・明示/自動pump二重実行防止は共通側へ。R1とset/get/resetの「PR=1由来の専用境界」・EE方針を共通FG1契約へ、試験のEE許可/復元はarch入口/出口の検証へ変更|
|IO_TIER §2 X5・§4 の口|X5 は移行前観測と明記。io_dispatch は引き続き FG1、入れ子無処理。IRQ_TAIL は追加しない|
|LAN_BULK `:14`（F4のEE限定復元）、§5 `:154` / `:162` / `:173`、§8 J、§9、確定 Tier側N4|「archの安全なFG待ち口」「元IF/EE復元」を「共通 FG1 待ち、終端/負値まで有限roundを反復」に変更。F4の「EEを限定して戻す」を入口FG1契約へ、J の PPC pump依存を AC-CTX 受入依存へ。EE復元変異は入口/出口試験へ移す|
|LAN の J の本体|NIC fg_service→RX→link→bulk、入力queue、期限検査、busy、IO_WAIT時もHLTしない方針を維持。syscall の入口1回の idle_work だけでは長い read/write の進行は足りない|
|P3（PPC LAN SH）|独自 window の実装は再開しない。CTX受入後に既存の共通LAN/Tier・SH像へ接続。Tier未導入でも idle_work が FG1 で動くことを先に確認する|
|idle_work `.h:4` / `.c:30`|「通常前景FG1の協調点限定、例外/IRQ出口は禁止」を追記。許可状態・深さ・running の既存検査は保持。アプリ callback・無制限待ちを追加しない|

- 既存 W3（`ARCH_COMPAT_CONTRACT.md:136`、PPC_NOTES・ARCH_BRINGUP_ORDER の結線）はそのまま残し、W4 の受入後に結線の対象へ AC-CTX を含める。

## 5. 新 arch の適合チェック

|項目|受入証拠|
|---|---|
|入口の完全保存|共通 dispatch の初命令で許可状態、frame/stack/root が有効|
|syscall→IRQ|長い syscall 中の timer と装置 IRQ、復帰後の全保存状態一致|
|IRQ→IRQ の能力|不在なら handler が再許可しない。深さは entry ごとに一増減|
|kernel 前景の許可条件|起動/排他/通常前景/trap を区別し、無能力経路で待たない|
|同期例外と scratch|解決可能 fault の復帰、非対応入れ子の明示停止、frame 非破壊|
|出口と協調切替|通常復帰・終了・kill・park/resume で IF/EE・深さ・所有状態一致|
|時計と待ち|FG1長待ち中に tick 前進、短い禁止後に復旧、独立時計の失敗を処理|
|前景サービス|FG1のみ実行、IRQ/出口/FG0/再入では無処理、両像の登録とclosure一致|
|I/Oと待ちの許可点|共通・ドライバ・platform の I/O と待ちの口が IF/EE を立てていないことを grep（rg）で確認する（_idle と AC-SYNC-2 の原始操作を除く）|

## 6. 段と受入・変異

- 全段は **1〜3ファイル・差分400行以下（試験込み）**。下表の試験名で「新」は予定名。既存試験の所在/anchor は発注時に照合し、行数超過は接続前に分割する。下準備の試験追加後、契約切替と退行更新を同じ統合受入で評価する。

|段|対象（ファイル数）/上限行|完了条件・依存|
|---|---|---|
|W4a1|`docs/decisions/ARCH_COMPAT_CONTRACT.md`・`docs/decisions/ppc/PPC_NOTES.md`（2）/160|本設計を正典化。CTX、AC-SYNC-2 PPC列、既存W3との関係、USER syscallのEE/tick記述を更新。実装未適合を明記|
|W4a2|`docs/decisions/IO_TIER_DESIGN.md`・`docs/decisions/network/LAN_BULK_DESIGN.md`（2）/160|W4a1後、§4の全対象（IO_TIER R1・set/get/reset・P1・試験、LAN F4を含む）を更新。N4/Jと入口FG1契約を整合|
|W4b|新 `tools/tests/arch_ctx_host.c`・新 `tools/tests/test_arch_ctx.py`（2）/380|実 trap C を fake MSR/IRQ で組む共通契約vector。EE切替・入口拒否・復帰、CHECK inputs/covers/変異。新契約のREDを確認|
|W4c|`kernel_vectors.S`・`kernel_trap.c`・`kernel_dec.c`（3、全てarch/ppc）/120|arch-ctx-host `tools/tests/test_arch_ctx.py:20,68,88` の PRE_W4C を切り替え、EE 許可後の成功と control を検証。上記 enable/出口/深さを一括変更。W4bと既存trap/DEC試験を実行。W4f・W4gと同じ統合の枝に積む（単独では既存の深さの試験が赤になり得る）。下記host stub追従も完了条件とし、ゲスト受入まではP3へ接続しない|
|W4d|`exec/exec_syscall.inc`・`kernel/idle_work.h`・既存 `tools/tests/test_exec_syscall_fgpoll.py`（3）/100|共通注記/期待を整合。x86・PPCとも入口で仕事1巡、無効文脈と再入は仕事なし|
|W4e|新 `tools/accept/arch_ctx_probe.c`・新 `tools/accept/arch_ctx.py`（2）/380|ppc-usb `tools/ppc/usb_run.py:65,218,228` は syscall 中の IRQ 進行・USB 継続を期待し、`tools/checks/ppc-usb.py:1` の登録も追従。両arch共通の観測列。QEMU/NP21/Wで下表を判定。PPCの32KB stack（`arch/ppc/platform_kboot_layout.h:29`）は、syscall内でnet_rx_idle等が動くため最大使用量を測り、境界を越えないことを確認。probeのカーネル組込は下記4か所を補助段で追従|
|W4f|既存trap/DEC/idle試験とharness（各補助段1〜3）/各≤300|kboot-dec `tools/ppc/kboot_dec_host.c:38,55` は実 dispatcher → DEC の深さ 1 → 0 を検証し、`tools/ppc/kboot_dec.py:30` の変異も移す。EE=0固定やDEC二重深さを期待する試験を新契約へ。変えた行の mutation anchor を rg で探し移動、一意・runtime RED|
|W4g|非局所出口/共有状態の限定修正（棚卸し後に実ファイル指定、各1〜3）/各≤400|W4c接続前ゲート。確かめるだけで済む見込み（根拠は下記）だが段は残す。必要な修正なしの場合も根拠を記録。未確認のまま段を閉じない|
|W4h|P3/IO_TIER/LAN側の既存担当段（各1〜3）/各≤400|W4c〜gの両arch受入後に接続。重複EE窓を作らず、待ちと予算の元試験を保持|

- PPC の IRQ handler からは、非局所の出口 (longjmp・kill・exec_pending_transfer) を出さない。.Ltrap_busy が残り、次の PR=0 の IRQ が fatal になるため。
- W4gの見込みの根拠: longjmpの着地 `exec/exec_launch.inc:336`・`exec/exec_schedule.inc:246` は _enable、PPCのsetjmp.Sは深さを復元する。IRQ frameはtrap slotでexec_kernel_spを読まない。arch_entry_enter・arch_user_resumeは先にppc_irq_save（`arch/ppc/exec_enter.S:11`・`arch/ppc/exec_resume.S:11`）。x86もsys_exitでIF=1のまま同じ着地へ進む。これらは確認を省く根拠ではなく、追加修正が不要と見込む根拠。
- W4cの完了条件: kernel_trap.cを組むhost試験 `tools/ppc/test_user_trap_host.c`・`tools/ppc/kboot_dec_host.c`・`tools/tests/ppc_hostfs_io.c` に ppc_enable/ppc_irq_save のstubを用意する（ROLES §4-3）。追従はW4fの補助段に割り当て、W4cと同じ統合枝で検証する。
- W4eでprobeをカーネルへ組み込む場合は order.mk・common.mk・build_profile_x86.json・ppc-kboot.mk の4か所を追従する。補助段は前2ファイル/後2ファイルの各2ファイル・各≤80行とし、1段3ファイル上限を守る。

|試験/契約|受入の観測（ゲストはPM担当）|一意anchorの変異・RED条件|
|---|---|---|
|CTX-1/4、host＋両guest|共通dispatch時に許可、FG1でidle実行、FG0/IRQ/再入でcallback数0|syscall enable削除、idleの状態/深さ/running各判定除去→回数または状態不一致|
|CTX-2/7、QEMU 7450・7455 各々|PR=1 syscallを継続させDECと外部IRQを注入。IRQ frameの保存PR=0、保存EE=1、深さ1→0、syscall frame sentinel不変、戻り値一致|PR=1入口をtrap slotへ誤配分、保存SRR破壊、深さ加減削除→fatal/状態不一致|
|CTX-7、QEMU両CPU|出口の各境界でIRQ pendingを作り、復元中は未配達・rfi後に配達。解決可能spill後に本体継続、非対応再入は明示停止|出口EE消去欠落はC側disableと独立にEE=1で出口へ入るprobeで検出。busy無視/spill slot共有→sentinel破壊。hostの文字列検査では代用しない|
|CTX-5/6、両guest|sys_halt/yield、長いpoll待ち、通常復帰/終了/kill/park-resume。tickが試験開始値より進み、復帰状態一致。PPCはDEC再arm後にも繰返し割込|halt前disable、DEC再arm削除、非局所出口の状態復元欠落→進行停止/状態差。固まりは外部watchdogの期限超過をREDとする|
|CTX-2/4/6、NP21/W|既存CUI/GUI待ち・入力・yield・V86・IRQ復帰、syscallなし30秒でもtickと既存LAN救済が進行、Tierは協調点のみ|IRQ出口で仕事実行、x86 STI削除、共有busy無視→禁止callback/停止/単一所有違反|
|P3接続、QEMU両CPU＋NP21/W|SH起動・通常syscall・LAN待ちでRX/期限が進む。LAN既存内容照合/遅延試験は維持|waitのfg_service欠落、IO_WAITでHLT、再入step実行→期限/内容/予算違反|

- host は実 C の判定を確認し、assembly 入れ子・rfi/iret の合格を代用しない。QEMU は 7450 と 7455 の双方、x86 は NP21/W のログを独立に要求する。
- 実装者は対象像（x86 kernelとPPC SHを含む）の指定ビルド、変更試験 `--mutate`、`python3 -B tools/checks/run.py --lint`。取り込みの全checkはCI、全変異はfull-check。check-changedは使わない。
- 本稿は設計のみのため preflight --fix-fonts・ビルド・host・lint・変異・QEMU・NP21/W はすべて未実施。作業木への書込み禁止を優先し、適合合格は宣言しない。

## 7. 未決

- ユーザー判断が要る未決: **0件**。推奨は「用語維持＋syscall入口FG1＋PR=0 trap再許可なし」。P3を待たせて入口契約を先行する指示の範囲内。
- 未確認の非局所出口・IRQ共有状態、実測、試験fixtureの所在は実装ゲートであり、方針の未決とは分ける。多段IRQ、IRQ出口Tier、非協調アプリの仕事保証は今回追加しない。


## 確定 (2026-11-14、Opus 第 1 回 Approve、PM 決定を反映)

- N1（PM）: 本設計はW4（実行文脈CTX）。既存W3を保持し、W4受入後にAC-CTXを結線対象へ含める。
- F1: AC-CTX-7にonfaultのarm〜clearのirq_save保護と、trap busy保持中のEE=0の根拠を明記。
- F2: §4の更新対象を追加し、文書段をW4a1（ARCH_COMPAT/PPC_NOTES）・W4a2（IO_TIER/LAN_BULK）へ分割。
- F3: AC-CTX-5にAC-SYNC-2原始操作の許可を別枠で残し、PR=0 trap dispatchとIRQからの呼出しを禁止。
- F4: 共通・ドライバ・platformのI/O/待ちにIF/EE許可窓がないことを、新archの適合チェックへ追加。
- F5: W4gは根拠付きで確認だけで済む見込みとし、非局所出口/共有状態の接続ゲートを保持。
- F6: IRQ frameの16KB trap stackとexec_kernel_sp非使用を明記し、W4eでPPCの32KB stack最大使用量を観測。
- F7: host stub・probe組込4か所の追従を完了条件に追加し、W4c/f/gを同じ統合枝で検証。
- F8: AC-CTX-3の無能力経路はidle_work無処理、次のFG1協調点で実行と訂正。
- F9: x86保存の参照をring3_entry.asm:35、FG0/FG1定義の参照をARCH_COMPAT:26-28へ訂正。
