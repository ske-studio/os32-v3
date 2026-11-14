# アーキテクチャ間の互換層のための差の調査

基点: `282738d0`、2026-11-14。作業木は読み取りのみ。表の `file:line` はこの基点のリポジトリ相対位置。案は未承認・未実装。
重要: この版には `net/link_platform.h`・GEM の NetNicOps adapter がない。背景にある「GEM credit=1514」「link 要求が3回」は本版で再現・確認した不具合として扱わない。別枝の確認が必要。
DEC は既に1kHzに復帰済み。boot-token も共通化済み。古い graph は現状の証拠にせず、現行ソースを検索して該当部分を照合した。
実施: 静的な所在・値・呼出し経路の照合。未実施: preflight --fix-fonts、ビルド、試験、変異、lint、実機・エミュレータ（PACK の読み取り専用指定のため）。
参照: `docs/decisions/time/TICK_ARCH_DESIGN.md:45`、`docs/decisions/network/LAN_OPERATOR_DESIGN.md:118`、`docs/decisions/gui/HAL_V2_DESIGN.md:85`（入口 `docs/INDEX.md:398`）、`docs/decisions/ppc/PPC_IMPL_RULES.md:11`、`docs/decisions/ppc/PPC_NOTES.md:30`、`docs/decisions/ppc/ARCH_BRINGUP_ORDER.md:21`。

## 1. 領域ごとの口

「一致」は意味・値が共通、「条件付」は能力照会・利用文脈が必要、「差」は未統合または契約の食い違い。装置の性能を同一にする必要はない。

|領域 / 口（宣言）|x86 の実体|PowerPC の実体|単位・最小保証・上限 / 判定|
|---|---|---|---|
|論理 tick / `tick_clock_step` (`kernel/time/tick_clock.h:16`)|`arch/x86/kernel/isr_stub.asm:493`|`arch/ppc/kernel_dec.c:60`|100Hz=10ms、step ごとに最大1ジフィ、u32公開値は周回。共通 `kernel/time/tick.c:24`。一致|
|物理周期 / `tick_clock_config` (`kernel/time/tick_clock.h:13`)|`platform/pc98/clock_source.c:21`、`init/kernel_boot_cpu.inc:55`|`arch/ppc/kernel_dec.c:25`|双方 nominal 1000Hz。x86 は reload/clk 秒、PPC は1/1000秒＋DEC端数配分。整数「毎10割込」を共通保証にしない。一致|
|raw/ms/jiffy64 (`kernel/time/tick_clock.h:18`)|`kernel/time/tick.c:46`|同じ実体|raw/jiffyは64bit、msはu32・1ms。64bit読取りはIF/EE=0。割込停止中は進まない。PPCは復帰後catch-up、PITは複数周期喪失を一般には回復できない。条件付|
|µs / `platform_clock_read_us` (`include/clock_source.h:8`)|`platform/pc98/clock_source.c:78`（TSC→PIT退避）|`platform/powermac/clock.c:75`（TB）|64bit µs、未準備NOSYS、PIT読取りはAGAINあり。PPCのTBは1MHz〜1GHz受入 (`:57`)。µs単位はµs精度の保証ではない。条件付|
|源の情報 / `platform_clock_info` (`include/clock_source.h:4`)|`platform/pc98/clock_source.c:27`|`platform/powermac/clock.c:67`|tick_hzは物理Hz、counter_hzはPIT/TB Hz（x86でTSC利用中もPIT値）。source種別・精度・割込停止耐性の欄なし。差|
|PIT操作 / `platform_timer_program/sample` (`include/platform_timer.h:19`)|`platform/pc98/timer.c:8`|同名実装なし、DECは `arch/ppc/kernel_dec.c:25`|reload/count・IRRのp1/p2、PC98専用の契約。汎用タイマーとして流用不可。差|
|予算 / `arch_tick_budget_*` (`arch/x86/arch_tick_budget.h:4`、`arch/ppc/arch_tick_budget.h:4`)|`platform/pc98/tick_budget.c:9`|`platform/powermac/tick_budget.c:4`|base/elapsed/cntは同じ内部単位で比較。x86 µs、PPC TB count。slot 120µs、RX tail23µs/TX17µs (`kernel/irq/tick_budget.h:4`)。換算経由なら一致|
|排他 / irq_save,restore (`include/io.h:87`)|`arch/x86/arch_io.h:42`|`arch/ppc/arch_io.h:6`|不透明u32状態、入れ子で元のIF/EE復元。CPU0ローカルでありSMP lockではない。一致|
|待ち / _idle,_halt,_stop (`include/io.h:108`)|`arch/x86/arch_io.h:70`|`arch/ppc/kernel_dec.c:64`|x86 idleはsti;hlt後に復帰、PPC idleは許可後永久spin、haltはnop、stopは禁止後永久spin。差|
|IRQ / claim,complete (`include/platform_irq.h:13`)|`platform/pc98/irq_complete.c:9`|`platform/powermac/mpic.c:93`|共通は成功1/不正負、line/native_token/deliver/needs_finish。PPCは失敗0、native_tokenだけ設定。PPC専用dispatchでは成立するが共通irq.cへは接続不可。差|
|IRQ topology/mask (`include/platform_irq.h:8`)|`platform/pc98/irq_policy.c:12`、`platform/pc98/irq_complete.c:6`|`platform/powermac/mpic.c:92` のsource_countのみ|本数は列挙値、操作時IF/EE=0。PPCに共通topology/reserved/mask/unmaskの実体なし。差|
|NIC frame / NetNicOps (`net/nic.h:11`)|`net/nic_e82557.c:30`、`net/nic_lgy98.c:46`|同口なし、`drivers/net/gem/gem_tx.c:15`・`drivers/net/gem/gem_rx.c:15`|txはヘッダ込/FCSなしの全frameを受理、成功は配送完了ではない。GEMは14〜1514B、net共通RXも1514B (`net/nic_rx.c:12`)。adapter未統合|
|NIC credit (`net/nic.h:16`)|`net/nic_e82557.c:25`、`net/nic_lgy98.c:38`|未実装、GEM ringは `drivers/net/gem/gem.h:176`|単位Bのみ規定。linkはfloor(min(NIC,stream)/256)−12を0で飽和 (`net/link.c:323`)。32 descriptor中28公開は空き保証とは異なる。差|
|受信列・予算 (`net/nic.h:28`・`:32`)|`net/nic_rx.c:11`・`:36`|`drivers/net/gem/gem_rx.c:26`、`platform/powermac/lan_service.c:251`|x86 SFS/link各4×1514B、scan最大8＋時間予算。GEMはrx呼出しごと最大8 descriptor、TX回収最大8 (`drivers/net/gem/gem_tx.c:48`)。外側で8回呼ぶと最大64走査になるので同じ「8」ではない。差|
|link / host protocol (`net/link.h:26`・`:59`)|`net/link.c:335`・`:517`|`platform/powermac/lan_service.c:202`、`platform/powermac/lan_boot.h:51`|link payload1400B、RTO20×10ms、8 handles/stream8192B。PLN1 EtherType0x88b6、通常payload256B/SFS1044B、TBでpoll。`link_platform`は設計のみ。差|
|文字 / con_backend (`kernel/console/console_backend.h:8`)|`platform/pc98/console.c:26`|`platform/powermac/console.c:11`|x86 80×25、PPC cols=width/8・rows=(height−top)/16 (`kernel/console/fbcon.c:64`)、全角2cell。safe照会が必要。形状は条件付一致|
|色 / con_backend_color・printf_attr (`include/console_attr.h:7`、`kernel/console/console_backend.h:11`)|`platform/pc98/console.c:37`、`platform/pc98/console_attr.c:61`|`platform/powermac/console.c:13`|raw attrは同じ意味でない。PPCは既定属性へ正規化、FB全体のfg/bgを使う。差|
|表示 / display_provider (`include/display_provider.h:23`)|`platform/pc98/gfx_selector.c:69`|`platform/powermac/display_provider.c:38`|非NULL backend（不在用含む）、能力別に選択。PPC OF FB初段INDEX8、PC98 PLANAR4等。全機種共通解像度を現状保証しない。条件付|
|HAL v2 / 能力と操作 (`docs/decisions/gui/HAL_V2_DESIGN.md:85`)|`gfx/v2/enum.c:5`・`gfx/v2/caps.c:36`（共通）|内部は `build/ppc-kboot.mk:106`、公開KAPIは `sdk/kapi_target_ppc.py:204` の不在群|設計の必須形式INDEX8/RGB565/XRGB8888/ARGB8888と、実機のscanout能力は別。内部利用可能とUSER利用可能も別。差|
|字形 / KCG (`include/kcg_contract.h:25`)|`drivers/video/kcg/kcg.c:45`|`drivers/video/kcg/kcg_file.c:85`・`:104`|ANK8×16/漢字16×16、同じKcgModuleApi。ROM対resident file、ロード失敗時FB不在。公開KCGはPPC不在 (`sdk/kapi_target_ppc.py:106`)。条件付|
|入力 / InputOps (`include/input_service.h:11`)|`platform/pc98/d2_bind.c:13`|`platform/powermac/input_boot.c:58`|tryは空−1、getkeyはscan/文字。PPC USBは4source・128key列 (`platform/powermac/input_usb.h:6`)、pressed/modifiers/raw/interruptible待ち等は部分実装。差|
|起動 / boot_common (`include/bootinfo.h:8`)|`platform/pc98/boot_capture.c:4`|`platform/powermac/boot_capture.c:5`|image_valid/CRC/size。PPC CRC未検証なのでvalid=0・source=NONE。OF/RAM情報は別の `platform/powermac/kboot_info.h:31`。条件付|
|boot-token (`include/boot_token.h:5`)|`platform/pc98/boot_token_pc98.c:9`|`platform/powermac/boot_token_powermac.c:7`|16B、共通 `kernel/boot_token.c:4` が一度生成・保持。材料TSC/PIT/RTC/MAC対TB/PVR/MACは違ってよい。暗号強度・絶対一意性は保証なし。一致|
|メモリ地図 (`include/platform_memory.h:7`)|`platform/pc98/platform_memory_desc.h:8`、`platform/pc98/memory.c:42`|`platform/powermac/platform_memory_desc.h:6`、`platform/powermac/memory.c:26`|半開区間B、高RAM probeはend PFN/未確認0。x86上限2GiB、PPC admitted16MiB・高RAM0、[15,16)MiB除外。同じ物理RAM量の保証ではない。条件付|
|Host Services (`include/platform_host.h:11`)|`platform/pc98/device_host.c:31`|なし (`sdk/kapi_target_ppc.py:157` 不在群)|owner単位handle、部分write・borrowed read buffer・HOSTDOWN/STALE。PPCのPLN1操作口は代替ABIではない。差|
|HostFS/SFS (`fs/serialfs/sfs_client.h:24`)|`fs/serialfs/sfs_lan.c:150`、`fs/serialfs/serialfs_session.c:79`|`platform/powermac/hostfs_io.c:99`|共通SFS codec/client、nowは10ms、絶対期限100ticks/再送10ticks (`fs/serialfs/sfs_client.h:98`)。x86 tick_count、PPC TB由来。can_waitの契約は差|
|ホスト世代 (`net/host_mailbox.h:15`)|`net/host_mailbox.c:166`|`platform/powermac/hostfs_peer.c:14`・`:21`|PPCはPLN1 host_nonceを世代として別のhost_alive_snapshot実体を持つ。統合時は同名二重リンクを避ける。差|
|KAPI不在 (`sdk/kapi_target_ppc.py:250`)|`sdk/gen_kapi.py:644`（予約NOSYS）・通常provider|`sdk/gen_kapi.py:659`、`sdk/kapi_target_ppc.py:269`|pointer=NULL、明示能力query=0、他の値=NOSYS、voidは無動作。未実装の成功値を捏造しないがvoid単体では検出不能。条件付|
|ビルド選択 (`build/config.mk:142`)|`build/sources/common.mk`＋`order.mk`、arch/platform選択|`build/ppc-kboot.mk:54`・`:71`・`:128`|GNU11、ILP32。PPC BE/soft-float/signed-char、SH/SHELL排他。共通ソース一覧は二重保守、KBOOT_*は像の能力でありCPU番号ではない。差|

## 2. 食い違い（16件: P1=0、P2=12、P3=4）

P1はこの版で現在の利用経路に不具合を示せるもの。静的な潜在差・未提供機能だけでP1としない。P2は次の統合・利用拡張を妨げる差で、意図した段階的不在も含む。

|ID|重さ|根拠 file:line|相違・発火条件|
|---|---|---|---|
|D01|P2|`include/io.h:116` / `arch/ppc/kernel_dec.c:64`|共通_idleの復帰契約とPPC noreturnが不一致。共通の条件待ちを移すと再検査できない。haltのnopは安全性より性能・契約表現の差|
|D02|P2|`include/platform_irq.h:14` / `platform/powermac/mpic.c:93` / `kernel/irq/irq.c:145`|失敗0を共通dispatcherは成功として扱い得る。claimの未初期化line/deliver/needs_finishも問題。現PPCは専用dispatchであり、接続前に修正が必要|
|D03|P2|`net/link.c:335` / `platform/powermac/hostfs_io.c:37` / `docs/decisions/network/LAN_OPERATOR_DESIGN.md:134`|link設計は10ms tick_count、PPC syscall EE=0では止まる。SFSはTB時計で回避済み。独自TB時計を無断でlinkへ足すのでなく、設計で「進行保証」と再送駆動を揃える。3回再送の原因は未確定|
|D04|P2|`net/nic.h:10` / `net/link.c:325` / `net/link.h:127`|credit=Bだけでは不十分。1514ならfloor=5、余裕12を引いて0。正creditの必要条件はNICとstream双方3328B以上、6pageの1frameを許すには4608B以上。GEM adapter未存在なので本版のP1ではない|
|D05|P2|`net/link.h:59` / `platform/powermac/lan_boot.h:51` / `sdk/kapi_target_ppc.py:157`|link/Host ServicesとPLN1の操作口・payload・peer世代が別。既定の操作道具をそのままPPCへ使えない。LAN_OPERATOR_DESIGNの既存統合計画に従う|
|D06|P2|`net/nic_rx.c:43` / `drivers/net/gem/gem_rx.c:26`|「8」は外側のwork回数と内側のdescriptor走査数で別。GEMに共通demuxを載せる際、総走査量・時間予算・RX単一所有者を決めないと割込/待ち時間が増える|
|D07|P2|`include/input_service.h:20` / `platform/powermac/input_boot.c:47`|interruptibleを無視、pressed/modifiers/shiftは0、rawは−1。USB側が保持する状態との違いを能力として公開していない。高度な入力利用者の移植で未押下と不在を混同|
|D08|P2|`build/ppc-kboot.mk:106` / `sdk/kapi_target_ppc.py:204`|PPC内部にHAL v2を組んでもUSER gfx2は不在。FB consoleが動くことをアプリの描画API使用可の根拠にできない|
|D09|P2|`include/platform_memory.h:19` / `platform/powermac/platform_memory_desc.h:6`|high probe=0はRAMなしではなく未採用。実機RAM量からx86相当のarenaを仮定すると予約域/未写像を使う。PPCのcompat BIOS帯も実在BIOSではない|
|D10|P3|`include/platform_timer.h:11` / `platform/pc98/clock_source.c:27`|platform_timerはPIT形状、clock_info.counter_hzは実際のµs源とは限らない。機種専用口の名前・照会欄が汎用と紛らわしい|
|D11|P3|`include/bootinfo.h:5` / `platform/powermac/boot_capture.c:7`|起動元はFD/HDD/NONEだけ。OF起動を識別できず、CRCも無い。ただしvalid=0なので成功は偽らない|
|D12|P2|`sdk/kapi_target_ppc.py:269` / `sdk/gen_kapi.py:659`|型別不在はあるがvoid操作は何も返せない。能力queryと対で使わない移植コードは表示/音/機器操作が成功したと誤認する。ABI変更ではなく能力分類を先に確定|
|D13|P3|`platform/pc98/console.c:37` / `platform/powermac/console.c:13`|PC98 raw属性に対しPPCは既定属性へ正規化。色・強調の同一性は保証されない|
|D14|P2|`fs/serialfs/sfs_client.h:33` / `platform/powermac/hostfs_io.c:43`|can_waitの宣言はIF=1、PPC実体はIRQ外ならEE=0でも可。pollで進む実装をIF判定へ揃えると既存USER HostFSを壊す。契約を協調的進行可能性に直す必要|
|D15|P2|`build/sources/common.mk:1` / `build/ppc-kboot.mk:71`|共通一覧とPPC closureが別。共通関数を足してx86だけ組めてもPPCリンク漏れが残る。#ifが少ないことだけでは互換性を示さない|
|D16|P3|`drivers/video/kcg/kcg_file.c:104` / `sdk/kapi_target_ppc.py:106`|内部KCGは同口、PPC公開KCGは不在。ROM常在とfile読込み失敗の違いもあり、字体が必ずあるという前提は置けない|

## 3. 共通ディレクトリに残るアーキテクチャ条件分岐

検索範囲: kernel/net/fs/include/lib/userland/init/mm/exec/gfx/drivers/sdk/kapi/kdebug の `.c/.h/.inc/.S`、行継続を結合。ヘッダguard・通常の機能/試験flagは除外。`#if/#ifdef/#ifndef/#elif` の開始行を1件と数える（else/endifは数えない）。
**34箇所**: OS側16（うちPPC専用起動12、SDK1、kdebug1、狭義共通2）＋SQLite18。狭義共通だけなら20（vendor込）、自前だけなら2。共通kernel/net/fs/userland/shellには該当なし。PPC専用入口もinit/内なので除外せず全掲する。

|file:line / 条件|移せるか・扱い|
|---|---|
|`include/callconv.h:5` / __i386__|arch_abiへ移せるが呼出規約の正当な境界。OS定数化では解決しない|
|`exec/shell_service.h:62` / __i386__|32bit ABIのsize assertionを両archへ。host64bit除外をCPU名と切り離す|
|`sdk/include/os32/os32_ver_info.h:28` / __i386__|公開ILP32レイアウトの検査として両archへ。C89互換を維持|
|`kdebug/recover_gf.c:235` / __i386__|生成表のsize/alignment条件をABI contractへ。PPCで同じ値と決めつけず検査|
|`init/ppc_kernel.c:37` / KBOOT_SHELL・SH|像別include。boot profile選択へ移せる、CPU能力とは区別|
|`init/ppc_kernel.c:50` / KBOOT_SH|製品shのinclude。profile選択へ|
|`init/ppc_kernel.c:64` / KBOOT_NEGATIVE未定義|診断設定の既定値。buildの生成profile定数へ|
|`init/ppc_kernel.c:77` / SHELLでもSHでもない|診断用USER gateの有無。profile固有ソースへ|
|`init/ppc_kernel.c:183` / SHELL・SH|console logの初期化。boot providerへ|
|`init/ppc_kernel.c:213` / SHELL・SH|trampolineと診断gateの選択。exec boot providerへ|
|`init/ppc_kernel.c:220` / SHELL・SH|字体用heap確保。像の必要量をboot profileへ|
|`init/ppc_kernel.c:248` / NEGATIVE==1|負試験。診断像の専用入口へ（製品のplatform口に足さない）|
|`init/ppc_kernel.c:251` / NEGATIVE==2|負試験。上と同じ|
|`init/ppc_kernel.c:340` / SHELL・SH|root→font→FB→execの結線。platform boot providerへ段階的に移せる|
|`init/ppc_kernel.c:386` / SH|HostFS/USB/inputの初期化。製品profileへ|
|`init/ppc_kernel.c:392` / SH|sh/CLI開始の選択。profileのstart口へ|
|`lib/sqlite3/sqlite3.c:15915` / __SIZEOF_POINTER__|vendor pointer幅。OS platformへ移さず維持|
|`lib/sqlite3/sqlite3.c:15917` / i386・ARM・PPC等|vendor pointer幅fallback。同上|
|`lib/sqlite3/sqlite3.c:15993` / BYTE_ORDER big|vendor endian判定。OSの定数へ置換しない|
|`lib/sqlite3/sqlite3.c:15995` / BYTE_ORDER little|同上|
|`lib/sqlite3/sqlite3.c:15997` / BIG_ENDIAN|同上|
|`lib/sqlite3/sqlite3.c:15999` / x86・ARM等|vendor little-endian fallback。同上|
|`lib/sqlite3/sqlite3.c:36790` / x86_64・aarch64・riscv|vendor intrinsic選択。現32bit二機種に無理に共通化しない|
|`lib/sqlite3/sqlite3.c:36826` / x86_64・aarch64・riscv|同上|
|`lib/sqlite3/sqlite3.c:37330` / ARM32・PPC32|vendor u64除算回避。保持|
|`lib/sqlite3/sqlite3.c:85661` / GCC版＋i386|vendor compiler対策。保持|
|`lib/sqlite3/sqlite3.c:96509` / i386|vendor性能計測時計。製品の時刻契約と混ぜない|
|`lib/sqlite3/sqlite3.c:96518` / x86_64|同上|
|`lib/sqlite3/sqlite3.c:96526` / aarch64|同上|
|`lib/sqlite3/sqlite3.c:96534` / ppc|同上|
|`lib/sqlite3/sqlite3.c:217491` / BYTE_ORDER big|vendor別区画のendian判定。保持|
|`lib/sqlite3/sqlite3.c:217493` / BYTE_ORDER little|同上|
|`lib/sqlite3/sqlite3.c:217495` / BIG_ENDIAN|同上|
|`lib/sqlite3/sqlite3.c:217497` / x86・ARM等|同上|

## 4. 互換層の案

新しい巨大HALを挟むより、既存の口に「単位・能力・進行文脈・最小容量・不在」を足す案を推す。全実装を最低性能へ制限する案はHAL v2の能力表と衝突し、機種別複製は今回の差を再生産する。
以下は提案値であり現状の保証ではない。特にlink時計の変更はLAN_OPERATOR_DESIGNとの設計調整を先に行う。

|領域|最大公約数: 共通コードが前提にできる保証案|最小公倍数: 全archが提供する口（不在は明示）|
|---|---|---|
|tick/時計|公開100Hz、内部物理nominal1kHz、周期は有理数。u32期限差は2^31未満。64bit µsは成功時だけ有効。IRQ停止中の進行可否を能力で区別|既存tick_clock一式、platform_clock_*＋source/分解能/停止耐性。link nowは10msと決定済み、単位と駆動源を別々に規定|
|IRQ/排他/待ち|CPU0、保存値不透明、restoreで元状態。claim成功1/不正負、全欄初期化。idleは有限で戻り条件を再検査、低消費電力は保証しない|irq_save/restore/enabled、platform_irq topology/mask/claim/complete、wait_once（進行poll可能）とstopを分離。ないIRQはNOSYS|
|NIC/link|非VLAN MTU1500・frame14〜1514B/FCSなし、tx成功=全frame受理のみ。共通queue各4frame、総descriptor scan≤8/サービス単位。creditは二重計上なし|NetNicOps全項、frame上限/credit容量/消費単位照会、link lock/unlock/now/idle。NICなしNOSYS/切断HOSTDOWN/一時不足AGAIN|
|creditの追加条件|既存linkを使うproviderは空の正常時に6pageを広告できること（min(NIC,stream)≥4608B）。満たせないNICはlink利用不可を明示。小容量対応は別設計|byte数に加えframe単位の消費量・保証可能frame数を明記。1514Bを1frame容量として256B切捨てに渡すだけでは不可|
|console/表示/字体|stream文字出力を最低経路、画面なしを許す。画面時geometry照会、ANK8×16/漢字16×16・全角2cell。raw attrの共通意味は保証しない|con_backend、display_provider、gfx2能力照会＋操作、KcgModuleApi。描画/字体不在を明示、HAL v2の必須形式とscanoutを混同しない|
|入力|try空−1、キー値の共通符号化。有限queueでdrop計数。最低文字入力、pressed/raw/modifier/interruptibleは独立能力|InputOps＋caps、get/try/peek、pressed/modifiers/raw、cancel。能力なしを未押下0と同一視させない|
|起動/メモリ|token16Bは起動中不変・認証用途不可。image_valid=0を尊重。地図はB半開区間、page4096B、probeはPFN、未確認0|boot_common/token、platform_memory geometry/予約列挙、boot_root_image。CRC/起動元不明はUNKNOWN、物理総量と採用量を分離|
|Host/SFS|同期handle/owner、借用bufferの寿命、部分write、SFS frame≤1044B・絶対期限100×10msを維持。can_waitはIRQ外かつ期限/transportが進むこと|platform_host全項、SfsIo、HostAlive。両archのSFS wire/clientは共用、PPC SFS CALL移行は既存計画に接続|
|KAPI|スロット順とILP32 ABI不変。不在pointer=NULL、query=0、値=NOSYS、voidは事前能力照会必須|機能群capsと型別不在生成器。未実装slotを成功0で埋めない。公開API追加なら通常のABI承認・版上げ|
|ビルド|両archに同じ契約試験ID・期待値。差は能力表とmanifestで宣言、共通CにCPU判定を足さない|arch/platform include選択、像profile、共通closure検査。SH/SHELL診断profileはCPU種別と分離|

正典案: `docs/decisions/ARCH_COMPAT_CONTRACT.md` 1本にID・口・単位・最低値・能力・不在・試験ID・担当設計への参照を置く。数値のC定義は既存ヘッダを正とし複製しない。時刻/ネット/表示の詳細設計も二重に書かない。
試験案: 同じCの契約ベクトルをx86/PPC providerのfakeと実体へ結ぶ。1000step→100jiffy、PIT有理周期、周回、EE/IF停止、claim全欄、credit境界1514/3072/3328/4608、RX総走査8、部分TX拒否、KAPI不在型を照合する。
host fakeだけではidle復帰・割込進行は証明できない。NP21/Wとmac99のguest probeで同じ契約IDを記録し、機種の時計を基準に進行を照合。TBと独立な時計の実機比較・NIC飽和は受入へ残す。
新試験は自分のCHECKにinputs/coversを宣言、変更行の変異anchorを追従、実行時REDを確認。共通変更はPPC SH像もリンクし、対象変異とlint、全体はGitHub CI/full-checkへ（PPC_IMPL_RULES・ROLES §0）。
POLICY_DEV §1への結線: 別archの設計§1の3判断に「ARCH_COMPATの該当ID・能力差・両arch共通試験」を添える1行を追加する。

## 5. terraへ渡す段の案（実装依頼ではなく分割候補）

各行は変更する試験も含め1〜3ファイル・差分400行以下。判断が残る段は先に設計を確定してから依頼する。新規ファイル登録・fixture追従で3本を超える場合は登録段を別に切る。

|段|種別 / 対象ファイル（最大3）|差分目安 / 完了条件・依存|
|---|---|---|
|W1|文書: 新ARCH_COMPAT_CONTRACT.md、docs/INDEX.md、docs/POLICY_DEV.md|250行。値と能力と不在の正典、既存確定設計を変更しない部分を先に採用|
|W2|文書: LAN_OPERATOR_DESIGN.md、TICK_ARCH_DESIGN.md|120行。EE=0中のlink期限/再送駆動・戻るidle・credit admissionを確定。背景の後続枝を別SHAで照合してから|
|T1|試験土台: 新tools/tests/test_arch_compat.py、新tools/tests/arch_compat_vectors.c|250行。共通tick/credit/不在の値のoracle、CHECK宣言、両profileを実行。既存試験名/固定hash/dep-dir追従を調べる|
|F1|直し: fs/serialfs/sfs_client.h、platform/powermac/hostfs_io.c、T1試験|120行。can_waitを進行可能性として整合。EE=0前景は期限切れ、IRQ内は拒否、SFS書込み再実行なしを試験|
|F2|直し: arch/ppc/kernel_dec.c、arch/ppc/arch_io.h、arch/ppc/ppc_trap.h|100行。W2/T1後、idle復帰の口を整合。実体の共通接続前にcallsite・noreturn依存を確認し、低消費電力は追加しない|
|F3|直し: platform/powermac/mpic.c、include/platform_irq.h、T1試験|180行。claim全欄/返値と既存専用dispatchを同時整合。spurious/失敗/完了1回を確認。topology結線は別段|
|F4|試験・宣言: net/nic.h、T1試験、arch_compat_vectors.c|180行。creditの3328/4608境界、frame/scan消費量を固定。既存LGY/E82557を同時照合|
|F5|直し: 後続枝のGEM adapter、対応するGEM容量照会、T1試験|250行。**本版にadapterが無いので正確なパスは後続SHAで確定してから発注**。使用中descriptorを数えたcredit、単一RX所有者、総scan上限を受入|
|F6|宣言: include/input_service.h、kernel/input_service.c、T1試験|180行。能力の表現を確定、不在と0状態を区別。公開KAPIは別段|
|F7|直し: platform/powermac/input_boot.c、platform/powermac/input_usb.c、T1試験|220行。F6後、modifiers/pressedの提供範囲とinterruptible待ちを確定。既存文字入力を回帰|
|F8|試験: sdk/kapi_target_ppc.py、T1試験、arch_compat_vectors.c|160行。不在群/void事前照会/内部描画と公開不在を検査。ABIや生成物を手編集しない|
|B1|ビルド検査: build/ppc-kboot.mk、build/sources/common.mk、T1試験|200行。差分manifestで共通追加漏れを検出。直ちに両一覧を大移動せず、x86/PPC SH双方のclosureを確認|
|W3|文書: ARCH_COMPAT_CONTRACT.md、PPC_NOTES.md、ARCH_BRINGUP_ORDER.md|100行。通った契約ID/能力差/未実機を反映。詳細と進捗を混ぜず入口だけ結ぶ|

各直しの受入は対応像のビルド＋対象変異＋lint。統合時に両arch guestの同じ契約IDを比較し、既存LAN/console/HostFS/shの回帰を追加。独立レビューと採用判断はPMの次工程であり、この調査では未実施。
