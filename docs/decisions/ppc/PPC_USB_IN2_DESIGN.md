> 出所: ~/os32-tmp/run/docs-1122c/src/PPC_USB_IN2_DESIGN.md。
> 状態: **確定 (2026-11-22)** — 段 A・B と段 C の CUI を先行し、段 C の GUI と段 D は GUI KAPI の結線後に進める。末尾の確定の節が本文に優先する。

# PowerMac USB HID を共通入力 HAL に接続する詳細設計 r3

## r2→r3 の変更

- P1-1　PPC 用の実 gshell の生成、root 収録、常駐シェルとしての起動確認を GUI 受入の前提に加えた。
- P1-2　CUI と GUI を別起動にし、ゲストは公開 dev、host は源の世代を照合する。
- P2-1　A の旧 sc は報告前後の集合差で通知し、左右 Shift の両方向の持ち替えを検査する。
- P2-2　反復候補に出力可能条件を加え、A と 0xff の同時押下を検査する。
- P2-3　実変換と旧 FIFO を結合し、Caps 初期値と期待列を指定した。
- P2-4　行取消を cooked の sc と ETX の組で判定し、ROLL DOWN を対照にした。
- P2-5　焦点移動を試験用 WM 操作で起こし、解放経路別に LOST の最初の reader を指定した。

## 1. 実物と統合判断

照合元は origin/main（f30b82927）。以下の「差」は依頼が想定する 7d 着地後の木と照合時点の木との差を数える。

|入力|PC-98 の基準|PowerMac の現状|
|---|---|---|
|鍵盤|drivers/input/kbd_hal.c:kbd_hal_capture が上位ビットを break、下位7ビットを物理キーとして世代付き KmKey を input_capture_key に渡す。keymap 登録失敗時のみ sc の input_capture|drivers/usb/hid_kbd.c:kbd_done は usage を先に互換 sc に畳み、修飾を先に差分通知。platform/powermac/usb_platform.c:usbp_input_key から input_usb.c:pm_input_usb_key が専用 held・Caps・cooked FIFO を更新。break は FIFO に出ない|
|反復|本体の typematic は再 make。input/hal/kbd_merge.c:input_capture_key が old と down を比較し、互換 sc がある遷移だけを in2 へ出す|hid_kbd.c:kbd_tick の期限で pm_input_usb_repeat が専用 FIFO へ再投入|
|旧 API|drivers/input/kbd.c:kbd_local_trykey は共通 cooked を読む|input_boot.c:try_key は LAN 先行、USB は専用 FIFO。pressed・raw・modifiers は定数。input_decode.c:pm_input_decode は pm_terminal_trygetkey() が選ぶ LAN または ESCC のバイト列を処理|
|マウス|drivers/input/mouse_hal.c:mouse_hal_capture が相対 MOVE と正論理 BUTTON を input_capture に渡す|hid_mouse.c:mouse_done の符号付き変位と下位3ボタンから usb_platform.c:usbp_input_mouse が mouse_ext_move と pointer_button を直接呼ぶ|
|寿命|input/hal/in2_dev.c:input_src_release は世代を照合しキー・ボタンを解放|usb_platform.c:usbp_input_src_alloc は種類別に 1..4 を再利用。hid_boot.h の hid_gone・hid_stop から usbp_input_release が私有状態を解放|

- 実物との差① 7d の PowerMac 用 platform_input_caps.h は照合時点にない。② ARCH_COMPAT_CONTRACT §9 の今回消す例外行も未着地。7d の取り込み後に実名を使って変更する。
- 統合判断1（同じ役目） [INPUT_HAL_DESIGN](../INPUT_HAL_DESIGN.md) の 4a/4b と [INPUT_KEYMAP_DESIGN](../INPUT_KEYMAP_DESIGN.md) の S1m/S2a/S2u/S2m/S3m/S3b がこの接続を担当する。同じ作業として実装する。
- 判断2（境界） USB は報告の解釈・反復時計・転送寿命、機種は backend 登録と端末、共通層は入力源の世代・押下・キー評価・旧 API と in2 への配送を持つ。
- 判断3（整合） AC-INPUT-1 の符号・空値・非待機・割込み可能待機を x86 と同じベクトルで検証。物理差は PC-98 sc と HID usage の backend で吸収。input_context.c:input_context_owner_select と in2_excl.c の所有者・焦点規則を両機種で使用する。

## 2. 境界の処理

- B では pm_usb_boot() の探索前（pm_input_boot() より前）に km_backend_register(&hid_jp_backend) を一度実行。失敗は診断し鍵盤 attach を拒否する。マウス探索は継続。USBP の kind 0/1 は明示的に INPUT_KIND_KBD/MOUSE へ写す。インタフェースごとに input_src_register(kind, INPUT_ORIGIN_USB, &gen)、鍵盤だけ km_source_bind(src, gen, KM_MAP_HID)。bind 失敗時は直ちに release、attach 失敗として返す。
- H-usb の u8 の src は USB のハンドル。種類別4枠は共通 adapter に移し、各枠が HAL の {src,generation} を所有。登録・bind 完了後に枠を公開する。USB の完了・tick・gone は同じ割込み排他下。停止済み旧インタフェースから再使用枠を引かせない。
- kbd_done は8バイト以上の正常報告から usage 256個を表せるビット集合を作る。E0..E7 は修飾バイトから復元し、0x00 は空きとして除外し、配列中の重複は一度にする。usage 1..3 を含む報告・短縮報告では前回値を保持。生成表の order で修飾、lock、残りを各群 usage 昇順で処理し、左右を独立した usage として両方向の差分を送る。
- repeat の候補・期限は鍵盤ごとの hid_kbd に残す。候補は u16、無効値 0xffff（0xff と区別）。設定時・tick 発火前とも norepeat が 0 で、生成表にキー出力の割当がある usage のみ許可（全レベル未割当の 0xff は除外）。反復可能な新 make で候補を替え、500 ms 後から30 ms 間隔、符号付き差で周回判定し tick 一回一発。norepeat の make では候補を替えず、候補の break・停止で解除。共通部は両機種とも現在の修飾で再評価、in2 に反復 KEY を積まない。

境界の疑似コード（s は生存する USB 枠から得た HAL の組、usage は未変換）。

~~~c
/* usbp_input_usage と usbp_input_usage_repeat の共通出口 */
KmKey k = { .src = s.src, .generation = s.gen, .keycode = usage,
            .down = repeat ? 1 : down, .repeat = repeat };
input_capture_key(&k);
/* この capture と同じ排他内で、新規 Pause make かつ物理 CTRL 押下中、
 * かつ !(gui_active && stop_suppressed) なら ring3_abort_request()。
 * repeat・break は対象外。STOP の cooked 抑止は共通 capture に従う。 */

/* usbp_input_mouse。boot report は左=1、右=2、中=4。 */
InputEvent e = { .src = s.src, .generation = s.gen, .time_ms = in2_time_ms(),
                 .type = INPUT_EVENT_MOVE, .x = dx, .y = dy };
if (dx || dy) input_capture(&e);
e.type = INPUT_EVENT_BUTTON;
e.x = e.y = 0;
e.buttons = buttons & 7;
input_capture(&e);

/* gone/attach 巻戻し。stopped で新規 callback を遮断済み。 */
input_src_release(s.src, s.gen);
clear_usb_slot(kind, handle);
~~~

- HAL は usage ごとの held と互換 sc ごとの参照数を管理。別鍵盤の同じキーや左右 Shift は最後の解放だけ legacy raw break。bind した源へ sc 版 input_repeat や input_capture(KEY) を混ぜない。
- 切断では km_release_keys が binding の生存中に解放、input_state_release が源別ボタンを解放。押下のない源に偽 break を作らない。最終マウス切断時には既存どおり未読 legacy 変位を捨てる。他源の押下・in2 の未読イベントを消さない。

## 3. 旧 API の読み口と規模

|出口|切替後の責務|規模の目安|
|---|---|---|
|cooked / FEP|pm_input_keymap と私有 FIFO を撤去。USB は legacy_cooked_peek/take。既存のキー取得時の local と生成時 shift を保つため、共通 FIFO の同じスロットに修飾を記録し dequeue と同じ排他下で読む。満杯時・flush・peek で対応を崩さない。input_emit_keymap の physical_mods を push に渡す。pm_input_usb_take は成功時だけ local と生成時 shift を更新。peek/has_key/dropped は共通リングの薄い照会。空読み・peek・flush は最終消費情報を保存し、端末消費で local/shift をクリア|既存2ファイル撤去・縮小、メタデータ 1 byte/entry|
|LAN と待機|input_boot の LAN 先行、CRLF・ESC 期限、KBOOT_LINK の USB 単独、busy peek の idle_work_run を維持。LAN 文字から物理押下を合成しない|gui_active を実モードに結び、GUI 時は inject_peek/inject_try で注入リングを peek/take。通常時は既存の端末優先順。provider 配線 40–70行|
|raw / state|InputOps に共通 raw take、input_key_pressed、input_key_mods、legacy dropped、GUI 切替・STOP 廃棄を x86 と同条件で結線。空は -1。押下中の状態照会で FIFO を消費しない|20–40行|
|ポインタ|PointerOps の available は登録中マウス数、poll は mouse_legacy_snapshot、座標・bounds・cursor は既存サービス。変位・ボタンの書き手を input_capture に一本化|adapter 全体 100–150行、8枠の対応表は数十 byte|
|常駐像|HID 表は既存生成物を一度だけリンク。旧表を除いたリンク map と BSS を採取し MEM_KERNEL_IMAGE_MAX 0x275000 内を確認|表・補助は約3 KiB、差分用ビット集合は鍵盤あたり32 byte|

## 4. ファイルごとの変更

|ファイル|担当する変更|
|---|---|
|include/usb_platform.h|S2 の usage/usage_repeat 契約。全呼出元切替後に旧 sc 通知を削除|
|drivers/usb/hid_kbd_map.c|互換 sc（診断用）、order、norepeat を生成表参照に統一|
|drivers/usb/hid_kbd.c|usage 差分と反復。hid_mouse.c、hid_boot.h の報告長検査・停止順は流用|
|platform/powermac/input_keymap_backend.c（新）|S1m の HID backend 起動登録|
|input/hal/usb_input.c（新）|S2a の種類別ハンドルと HAL 源の寿命、鍵盤・ポインタの capture adapter|
|platform/powermac/usb_platform.c|usbp を adapter に委譲。利用可否と診断はここで接続。既存 USB KEY/REPEAT の sc 表示を維持し usage を追記|
|input/hal/legacy_ring.c、in2_int.h|cooked スロットの修飾と取得口を追加。従来の take の呼出契約を維持|
|platform/powermac/input_usb.c、input_usb.h|FEP の最終消費情報だけ残し、B で私有 producer・キューを削除し共通リングへ委譲|
|platform/powermac/input_keymap.c|削除。input_decode.c は LAN/ESCC 用に維持|
|platform/powermac/input_boot.c|上記 provider・InputOps を登録。待機、IME・独占中の旧配送を共通ポリシーに従わせる|
|PPC のビルド入力|下記の像別選択に従う。旧表をリンク対象から除去|
|platform/powermac/platform_input_caps.h|7d の独占能力を最終段で 0 から 1 へ|
|[ARCH_COMPAT_CONTRACT](../ARCH_COMPAT_CONTRACT.md)|AC-INPUT-1 の実装欄と試験を更新し、§9 の PowerMac 独占例外行を削除|

A の実装では hid_kbd.c の新 usage 通知を既存の H-usbp で受ける。鍵盤ごとに一報告を単位として変換する。報告境界を H-usbp に渡し、新旧 usage 集合を別々に互換 sc 集合に射影する。群順を保ち、新だけにある sc は make、旧だけにある sc は break、両方にある sc は無通知とする。repeat は保持中の対応 sc の旧 repeat に変換する。gone/stop で枠を初期化する。既存 cooked FIFO をここから駆動し、usb_stack_host.c と test_usb_hid.py も新通知に追従させる。B の 4b・S2m・S3m・S3b で暫定変換を除去し、HAL producer・backend・旧 API consumer を同時に有効化する。test_arch_compat.py の PPC 期待値 14→0、対応 XFAIL・pin・変異を同段で更新する。

|IMAGE 指定|実効像と USB/EXEC/USERSHELL|B のリンクと受入|
|---|---|---|
|kboot-link-sh、kboot-sh、kboot-shell|現行 ppc-flags.mk ではすべて kboot-link-sh、1/1/1|C_COMMON_EXEC の共通 capture・legacy_ring と C_PLATFORM_USERSHELL の hid_jp.c を一度ずつ使う。USB 選択時に usb_input.c と input_keymap_backend.c を追加し、C_PLATFORM_USB の input_keymap.c を除く。初回 attach 後にシェルで USB 入力を消費する|
|kboot-core、kboot-link|各診断像、0/0/0|新 USB ソースの追加なし。両方の PPCKERN.ELF をリンクし、USB・backend への未解決参照がないことを確認|

kboot-sh の定義値は 1/1/1、kboot-shell は 0/1/0。正規化前の定義を独立した実行結果として数えない。非 LINK の端末先行分岐は provider 試験で検査する。

C/D の GUI 受入にはビルド経路の追加が必要。現行の build/programs.mk の gshell リンクは elf_i386 固定で、build/ppc-user.mk の常駐物は shell.bin。PPC の同ファイルに gshell.bin 規則を追加する。userland/gshell を PPC SysV・big endian の OS32 Rust target と PPC 用 KAPI bindings で staticlib 化し、PPC 版 gfx・cfg、resident CRT と app_sys.ld で shell.elf と同じ常駐帯へリンクする。PPC 用 Rust target・依存ライブラリ・GUI サービスで不足するものはこの前提作業で実装する。ELF32 PowerPC と入口・配置を確認してから resident shell と同じ objcopy と mkos32x.py で包む。

tools/accept/ppc_mouse.py の build / Session を使う受入台本を拡張し、CUI 用と GUI 用で kernel 出力・root manifest を分ける。ともに IMAGE=kboot-link-sh、KB_SH_OUT に PPC 生成物を置き、KB_ROOT_BINS に前景試験用 in2prob.bin を加えて kb-user と kernel をビルドする。GUI 試行は gshell.bin も /bin に収録し、受入専用の KB_ROOT_SHELL 指定で同じ gshell.bin を /sys/shell.bin に配置する（build/targets/ppc-powermac.mk の常駐入口）。通常の起動ループから WM として直接ロードする。RSH は同期・制御用に残す。各 PPCKERN.ELF を 7450 と 7455 で起動し、GUI は gshell の gui_register 成功、画面に描いた二窓、前景窓での probe 応答までを起動条件とする。この条件を満たすまで GUI 行は未検証と記録する。

## 5. 段別の受入と対応する一行変異

各行の変異は独立に適用し、対応する観測値が実行時に不一致になることを確認する。

|段・試験|合格条件|一行変異|
|---|---|---|
|A host・usb_hid_host|同一報告の Shift+A、Caps+A、左右 Shift、重複 usage、rollover→正常復帰で所定の usage 列|差分通知の usage を互換 sc に置換|
|A host・反復|時刻 499/500/529/530 ms で反復数 0/1/1/2。A 保持中 Caps でも候補維持、break 後は停止|期限比較の >= を > に変更|
|B host・実 adapter と HAL|二鍵盤、同番号のマウス、枠満杯、bind 失敗と再接続。抜去後に別源の Shift は残り、同じ枠の再登録で世代が更新され、旧世代の make を渡しても pressed とリング件数が変化しない|bind 失敗の release を削除|
|B host・AC-INPUT-1 共通ベクトル|対応する x86 sc と HID usage で同じ pressed/mods/raw/cooked、反復で in2 件数不変。FEP は投入後に Shift を離しても元の値、LAN は非 local|dequeue の修飾を現在値に置換|
|B host・ポインタ|負の X、正の Y、左右中の同時・個別解除、2匹の OR、抜去を検査。legacy と in2 の変位は各1回|MOVE の x を符号なし8bitに変換|
|C QEMU・両 CPU|tools/ppc/usb_run.py の mac99/OHCI に usb-kbd、usb-mouse を明示。7450・7455 各々で QMP の make/hold/break、移動・クリック・押下中 device_del、再追加を実行。ESCC 同期後 guest から dev 列挙・in2 列・旧 API の値を照合|BUTTON の capture を削除|
|C QEMU・旧 UI|両 CPU のシェルで USB 入力、非 LINK の provider 試験で LAN 先行、GUI で raw とドラッグ解除。KBOOT_LINK でも USB だけでキー消費。診断ログだけで合格にしない|USB の local try を常に -1 に変更|
|D 7d 取り込み・両 CPU と x86 共通試験|前景 owner の KBD/MOUSE acquire 成功、他 owner は BUSY。独占下で所有者の in2 と STOP 等の予約入力を維持、WM への通常配送停止。焦点変更・終了・unacquire で LOST と配送再開、ドラッグ残留なし|PowerMac の能力値を 0 に戻す|

追加の受入手順は次のとおり。

- A は hid_kbd、実 H-usbp 暫定変換、input_usb.c と旧 input_keymap.c を一緒にリンクする。初期状態は全解放・Caps OFF・FIFO 空。左 Shift→同一報告で右のみ→同一報告で左のみ→両方→右のみ→右+A→全解放で旧 sc 0x70 は最初の make と最後の break だけ、take は 0x1d41、次は -1。初期化後、{Caps,A}→空→{Caps,A}→空で旧 Caps は make/break/make/break、take は 0x1d41、0x1d61、-1。各手順は反復期限前に完了する。{A,0xff} の同時 make を保持し、500 ms と 530 ms の両方で A が反復し、0xff は出力しない。空きと同群の複数 make では出力可能で反復可能な最大 usage を選ぶ。時刻を u32 上限直前から進め、周回後の期限の直前・一致で反復数 0・1 を確認する。
- B では冷起動で最初から接続した鍵盤が登録・bind・列挙され、再接続なしで文字を読めることを確認する。登録失敗を注入すると鍵盤源が残らずマウスだけ使える。GUI 中の物理 A は raw に出し、注入文字は peek 2 回・take 1 回で同値、続く try は -1。Shift+A を積んで Shift を離し、peek・take・空読み後の FEP 情報と flush 後の次スロットの修飾を照合する。
- B/C では待機開始を通知したゲストに Ctrl make→Pause make を送り、割込み可能待機が中断値で復帰し cooked は空であることを確認する。GUI の STOP 抑止中は同じ操作で中断せず別の注入文字で復帰する。中断要求呼出しを削除する変異は前者の期限超過で RED。
- C は上記二像で同じ QMP 入力を別試行する。CUI は常駐シェルから probe を起動し、GUI は実 gshell の前景窓から起動する。probe は in2_dev_count と in2_dev_info で KBD/MOUSE の dev を控え ready を通知する。usb_run.py の keyboard_actions と ppc_mouse.py の input-send-event で A make、反復期限まで保持、break、相対 (+12,-7)、左押下、解除を各読取りの応答を待って送る。前景 probe の in2_read(out,max) の戻り件数分について KEY の dev/sc=鍵盤dev/0x1d と down=1,0（保持中の反復 KEY は 0 件）、MOVE のマウス dev と dx/dy、BUTTON の down=1,0 と changed=1 を照合する。CUI は pressed の 1→0 と cooked の a、保持中の再入力を読み、GUI は WM が読む raw の make/break と legacy pointer snapshot を記録する。押下中 device_del 後は再追加して再列挙し、その時点の dev に照合する。公開 dev の再利用を許す。鍵盤 capture 呼出しを飛ばす変異は KEY 欠落、BUTTON capture を削る別変異はボタン遷移欠落で個別に RED。tools/accept/ppc_mouse.py の guest probe・QMP 同期と抜去・再追加手順を流用する。
- Ctrl+C の cooked は旧 PPC の ESC から x86 と同じ ETX (0x03) に変わる。probe で値を確認し、usb_run.py のシェル受入は入力途中の Ctrl+C で行を捨て、次の Enter で取消行が実行されないことを確認する。userland/shell/ui.c は cooked の全値 0x2b03（C の sc と ETX）で取消を判定する。別試行で echo keep を編集し ROLL DOWN（0x3703）と Enter を送り keep の出力を確認し、既存の矢印・履歴・Backspace 等の編集試験も通す。
- D は origin/wt/int-1122g の in2_excl.c と gshell の excl.rs:settle / input.rs:cancel_capture に合わせる。7450・7455 の GUI 受入像と x86 の gshell で前景 probe と別窓を起動し、前景 P と別窓 Q を作り、P が in2_acquire(3) で 0、背景 Q が同じ要求で OS32_ERR_BUSY を得る。P は acquire 時の LOST を先に drain する。取得中の通常 KEY・MOVE・BUTTON は owner の in2 で観測し、WM の通常配送件数は増えないことを確認する。予約の STOP・CTRL・GRPH・TAB は各 make/break を WM 側でも観測する。タイトルバーのドラッグ中に独占境界を越え、焦点変更・終了・明示解放を別試行にする。各試行で取得時の LOST を読み切り、他の reader を待機させる。明示解放は P の in2_unacquire() が 0 を返した直後に P が最初に in2_read する。焦点変更は試験用 WM 制御で通常の焦点設定処理を Q に実行し、終了は P を終了して Q を次の前景とする。両試行とも前景が Q になった時点で Q が最初に in2_read する。各 reader は返却件数内に type=IN2_EV_LOST、u.lost.reason=IN2_LOST_EXCL を確認する。WM は in2_wm_state で excl=0 を確認し、、settle 後に左右 capture が空、synth_buttons が 0、ドラッグ枠が消え、ボタンを押し直さず動かしても窓位置が変わらないことを確認する。新規押下で通常配送とドラッグが再開する。

A は HID 差分、B は HAL と旧 API の切替をそれぞれビルド可能な単位にする。C まで能力 0 で実配送を確認し、D で能力と契約書を同時更新する。各段で対象 host check・変異、PPC 像、共通部を触る段は x86 像と既存入力試験も実行する。

## 6. 未決

ユーザー判断が必要な項目は 0 件。7d の未着地部分は本線取り込み時の照合事項。

## 確定 (2026-11-22、設計ラリー 3 回で決着せず、ユーザー決定「段を分ける」)
- 段 A (HID の差分と反復)・段 B (HAL と旧い API の切り替え、冷起動の bind、Ctrl+Pause の中断)・段 C の CUI の部分 (常駐シェルから probe、QMP の make/hold/break・移動・クリック・device_del と再追加、7450・7455、Ctrl+C は ETX) を確定して実装に出す。能力 (platform_input_caps.h) は 0 のまま。
- 段 C の GUI の部分と段 D (独占の有効化、能力を 1 に、ARCH_COMPAT_CONTRACT §9 の行を消す) は、PowerPC の GUI の口 (gui_register・gui_call を実の提供者に結線する段) の後に回す。sol 第 3 回の P1 2 件 (GUI の KAPI の結線・常駐の像の選択の条件) と P2 2 件 (QMP の make と break を分ける同期・前景の移りの同期) はその時の設計に入れる。§9 の行はそれまで残す。

- (2026-11-22、PM) 段 C の GUI の配置は [PPC_GUI_KAPI_DESIGN](PPC_GUI_KAPI_DESIGN.md) の確定 (C-GUI) に揃える: KB_ROOT_SHELL で gshell を /sys/shell.bin に置く形はやめ、/sys/shell.bin と /bin/gshell.bin を分けて C-GUI 専用の構成で /bin/gshell.bin を選ぶ。
