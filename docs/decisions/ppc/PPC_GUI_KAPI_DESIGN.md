> 出所: ~/os32-tmp/run/docs-1122c/src/PPC_GUI_KAPI_DESIGN.md。
> 状態: **確定 (2026-11-22)** — gui_register・gui_call を共通の提供者に接続し、C-GUI で両 G4 のシェル切り替えを確認してから製品の GUI を有効にする。末尾の確定の節が本文に優先する。

# r2→r3 の変更

- P1　C-GUI 専用構成で二つの常駐像を別パスに置き、/bin/gshell.bin を選択・許可する。両 G4 で GUI 像欠落と連続 3 fault を is_gui が真の経路から試し、通常 CUI の応答まで確認する。

# r1→r2 の変更

- P1　KAPI 結線後に USB in2 段 C の GUI 受入を置き、両 G4 で入力から CUI 復帰まで通ってから製品 GUI を有効にする。
- P2（呼出条件）　OWNER_EXIT と TRIM_DONE の分岐順、シェル切替のパス境界、登録済み WM の焦点変更を x86・PPC の共通受入に加える。
- P2（描画主体）　同期合成の要否を本線の 7d 統合時に確定し、必要な描画権限の変更と x86 回帰試験を PowerPC 段に先行させる。

# PowerMac の常駐 gshell と GUI KAPI（設計 r3）

## 1. 実物と統合の判断

基準は origin/main の 69d9cef40。ここでいう同じ gshell.bin は同じ Rust/C ソース・同じ公開 ABI・同じ配置名から作る各 ISA の実行像を指す。

1. 同じ役目は既にある。x86 は sys/kern/exec/exec_kapi_init.inc の関数表から sys/kapi/kapi_generated.c:wrap_gui_register / wrap_gui_call / wrap_sys_switch_shell を経て sys/kern/gui.c の同名の本体へ入る。gui_register は shell owner 1・特権呼出し・非 NULL handler を要求し、pump を保存して入力を raw にする。gui_call は呼出元 owner を WM に渡し、WAIT と WM の文脈を囲う。gui_owner_exit は子の窓を回収し、WM 終了なら登録・pump・入力モードを戻す。build/sources/common.mk の COMMON_EXEC と hooks.mk の HOOK_gui は **既に PPC にも有効**。欠けるのは sdk/kapi_target_ppc.py の absent 指定を通る利用者側の入口。
2. 実装は一つにする。sys/hal/gfx/gfx_present.c の v1、gfx/v2、kernel/console の font source、exec と WM は共通。sys/platform/powermac/display_provider.c:display_provider_select が sys/dev/video/of_fb/backend_of_fb.c:gfx_backend_of_fb を返す。5b/5c の INDEX8 RAM shadow・同期・世代・幅・保護行を使う。機種に残すのは OF 表示の取得・色設定と FB 転送。USB は in2 に入り、WM が入力を読む。
3. AC-DISPLAY-1～3 と AC-INPUT-1 に沿う。ARCH_DRIFT_AUDIT の B02 は owner の履歴、B03 は段 E より前の記録。現在の sys/platform/powermac/shell_loop_ops.c:gui_default は常に 0、prepare(1) は常に 2。sys/kern/init/shell_loop.c:shell_loop_run と userland/shell/cmd_sys.c の os32gui は既存。USB 設計の C-GUI は KAPI 結線後、製品 GUI 有効化前に両 G4 で行う。独占能力の有効化は D で行う。

sdk/kapi_target_ppc.py の gfx 群は 7 口。gshell/src/wm.rs は set/get_palette、lease.rs は lease_palette、userland/lib/gfx/draw/gfx_cmd.c は hw_submit/get を使う。hardware_scroll・osri_show と gfx2 群唯一の gfx2_check_op は直接の GUI 起動要件から外す。最終 ELF の到達可能な呼出しで照合する。既存の gfx_init・screen_info・dirty/present は再実装不要。

## 2. 境目と容量

以下は既存名を使った結線図・擬似コード。公開スロット番号・引数・構造体を維持する。

    PPC 表 → 生成 wrapper → gui_register / gui_call / sys_switch_shell
    set_palette → gfx_set_palette_hal    get_palette → palette_get
    lease_palette → gfx_lease_palette    hw_submit/get → gfx_hw_submit/get

生成元で対象だけ absent から除き、同じ群に残る absent が必要な実提供者ヘッダまで抑制しないよう include 集合も分割する。sdk/kapi.json の out/in 長検査を保ち、生成器で再生成。sys/hal/gfx/gfx_hw_submit.c と gfx_hw_mono_cpu.c を pending から EXEC へ移す。対応 op は既存のソフト処理と HAL の能力判定を使い、未対応 op の完了件数・戻り値を維持する。

    gfx::init → gfx_init → display_provider_public_init_ok()
    gfx_present_dirty → DISPLAY_V1_PRESENT_OK() → gfx_v1_present_rect
    gfx_backend_of_fb.present_rect → of_fb_present → of_fb_copy

of_fb_copy は左右をワード境界へ広げ、隣画素も RAM から取得し、pitch と protected_rows を守って native store する。gshell に VRAM を渡さない。query の実寸で合成し、字体は既存の ShellService / KCG と kernel/console の供給口で取得する。
backend_of_fb.c:palette は空、query の貸出範囲も空。set の成功を shadow だけで判定すると窓の色が一致しない。機種側で boot 中に既知の固定 CLUT を設定・保存し（rh_observe.c の color! の引数順を参照）、backend にその RGB 表を渡す案とする。論理 index→固定 CLUT の最近色を 256 byte に保持、初期は恒等。palette(first,count,rgb) で対応を更新し、変更時は既存 shadow の表示領域を再転送。of_fb_copy は転送時だけ各 byte を変換し、恒等時は既存高速経路を使う。set/get の論理値を共通 palette と一致させる。query は実装済みの貸出範囲だけ報告。固定 CLUT 設定失敗時は prepare で GUI 不可とし CUI を保持する。

    present 可 = 有効な adapter && 初期化時の世代 == 現世代
                 && 初期化時の owner == 描画を実行する owner

現行 display_provider_present_ok は appslot_cur() 比較。子から GUI handler を呼んでも appslot は子のまま。子の要求を処理する登録済み WM の合成と、子自身の直接 present を区別する。shutdown は世代を進め unbind、再 init まで旧い操作を拒否する。

本線担当は 7d を統合し、段 1・2 の共有ビルド、PPC 向け KAPI 生成器と生成物を揃える。子からの同期 gui_call で再描画する操作を x86 で実行し、応答前に present が必要な経路を特定する。既存 pump で全操作が成立すれば、その操作列を回帰試験に残す。同期 present が必要なら、同じ本線段で共通 exec に次の条件を実装してから引き渡す。

- 現在登録された handler をカーネルが呼ぶ区間で、登録時のシェル世代が現世代と一致するときだけ、描画の owner をシェルとして判定する。adapter と表示の世代検査も通す。appslot と資源 owner は子のままとし、窓生成と終了時回収が同じ子を指すことを調べる。
- 通常 return、WAIT の park、fault による abort で区間を閉じる。子への復帰直後、別の子の実行時、シェル終了・再登録後の旧文脈で直接 present を試し、FB が変化しないことを確認する。x86 で同期呼出しの窓・重なり・再描画が正しく、同じ子の直接 present だけが拒否されることを本線段の完了条件とする。

    os32gui → sys_switch_shell(SYS_GSHELL_BIN) → shell 終了
    shell_loop_run → gui_take_next_shell（1 回消費）→ prepare → exec_run
    gshell 終了 → gui_owner_exit → 次の常駐像へ

PPC の gui_default は x86 同様 SYSTEM.CFG の GUI を読む。prepare は利用可能な FB・字形・リンク済み描画機能で判断する。CUI 復帰と GUI 読込失敗、連続 fault 3 回のフォールバックは共通 loop に任せる。root に /sys/shell.bin と /bin/gshell.bin を別々に収録する。KB_ROOT_SHELL は通常の CUI 像を指す。共通 loop は選択パスと SYS_GSHELL_BIN の一致で is_gui を決める。

容量は未実測。常駐余り約 376 KiB を目安とし、追加 kernel text/rodata を概算 8～32 KiB、色表と変換表を約 1 KiB、追加の画面大バッファを 0 と見込む。PPC ELF の map で text/data/bss とスタック予約を常駐帯端まで照合し、root 内の圧縮サイズ、展開 RAM、kernel 増分を別記する。gshell の静的 GuiState を含む ELF の占有量は shell と同じ帯の再利用として各像の最大値で評価する。

## 3. 実装段（各行の対象 1 ファイルを 1 段とする）

|段|対象|完了条件|
|---|---|---|
|1|build/sources/common.mk|HW コマンドと MONO の共通提供者を EXEC 像で解決|
|2|sdk/kapi_target_ppc.py|上記 8 口が実提供者へ解決。生成物は生成器の出力として機械更新|
|3|sys/dev/video/of_fb/backend_of_fb.h|固定 CLUT の受渡しを記述|
|4|sys/platform/powermac/display_provider.c|OF 初期化から CLUT と可用性を受け渡し、世代・owner の門を照合|
|5|sys/dev/video/of_fb/backend_of_fb.c|set/query/copy が同じ変換表を使用、保護行と padding を保存|
|6|sdk/rust/powerpc-os32-none.json（新規）|32 bit BE SysV。整数・ポインタ・浮動小数点 ABI を PPC_USER_ABI と照合|
|7|build/ppc-user.mk|同じ userland/gshell を明示 cargo target で staticlib 化。PPC 用 gfx・cfg、resident CRT、syscalls、既存 newlib nano/allocator と libgcc で app_sys.ld にリンク。shell と同じ guard・objcopy・mkos32x。未解決 extern、x86 asm、SHM の endian 固定を参照元単位で解消|
|8|build/targets/ppc-powermac.mk|kb-user と root の依存に gshell.bin を追加。像更新時に root 再生成|
|9|sys/platform/powermac/shell_loop_ops.c|設定値と実能力により GUI 起動、失敗時に CUI を再利用|
|10|tools/tests/test_of_fb.py|色・世代・部分転送を host fixture で照合|
|11|tools/accept/ppc_gui.py（新規）|既存 ppc_mouse の build / Session を使い製品経路の起動・復帰を照合|

追加の共通化が必要なら変更元ごとに段を分割する。生成した表の手編集や機種マクロによる gshell の分岐は行わない。

PowerPC 担当は OF FB と PPC 常駐像・受入用 root を用意し、本線の結線を使って次の順に進める。

1. **C-GUI（受入専用起動）**　受入専用 root に通常の CUI 像を /sys/shell.bin、実 gshell を /bin/gshell.bin として収録する。C-GUI 専用ビルドでのみ shell_loop_run に渡す ops の gui_default() を 1 とし、prepare(1) は段 9 と同じ実能力の検査に通れば 0 を返す。prepare(0) は通常どおり CUI を許可する。共通 loop が SYS_GSHELL_BIN を選び is_gui が真のまま実 gshell を起動する。この構成で 7450・7455 それぞれで register 成功、子が作った二窓、前景 probe の ready を確認する。7d を含む WM から in2_wm_state が 0 を返し、独占なしの状態が読めることを要求する。
2. **製品 GUI 有効化**　C-GUI 通過後に 段 8・9 の製品 root、設定値、prepare を製品経路で結線する。両 G4 で通常の /sys/shell.bin から os32gui で /bin/gshell.bin に切り替え、同じ入力操作列、CUI 復帰、再起動、失敗時フォールバックを確認する。
3. **D（PowerPC 担当の後続段）**　ここで初めて独占能力を有効にし、USB in2 設計の取得・焦点変更・終了・解放の受入を行う。それまでは能力 0、前景からの in2_acquire(3) は NOSYS とし、前後で焦点・独占状態・通常配送が変わらないことを確認する。ARCH_COMPAT_CONTRACT §9 の該当行は D の成立まで維持する。

## 4. 受入と変異

- host で PPC KAPI binding/table、kernel shell、gshell-host、OF FB を実行。未登録の通常 call は NOSYS、子の register は INVAL、WAIT 後の通常処理、owner_exit 後の再登録、世代違いと別 owner の直接 present の拒否を検証する。色は get の論理 RGB と転送先の index を別々に比較する。
- リンク後に readelf で ELF32・PowerPC・big endian・入口と常駐帯を、map で実提供者と CRT を確認する。両 CPU に同一の PPC 成果物を使う。
- C-GUI 通過後の製品経路では kboot-link-sh を 7450・7455 で各々冷起動。CUI から os32gui を実行し、二つの窓を開く PPC クライアントの gui_call が成功し応答すること、QMP screendump の両矩形の枠・表題字形・背景色を照合する。gshell は register の失敗でも単体動作するためデスクトップ画像だけでは合格にしない。重なり順と前景を変え、dirty 再描画を確認する。GUI から CUI へ戻り再起動し、文字入力と旧 callback の消滅を確認。x86 でも同じ操作列を回す。
- 7450・7455 の各々で C-GUI 専用構成から二つの失敗試験を別々に冷起動する。どちらも /sys/shell.bin は通常の CUI 像とし、prepare(1) 通過後の Loading /bin/gshell.bin を観測する。欠落試験では /bin/gshell.bin だけを root から除き、読込失敗の次の起動が /sys/shell.bin であることを照合する。fault 試験では /bin/gshell.bin に起動ごとに fault する試験像を置く。切替要求を挟まず同パスの起動と EXEC_ERR_FAULT が 3 回続き、最初の 2 回は再試行、3 回目の直後に /sys/shell.bin が起動することを照合する。各試行で物理キーから入力した CUI コマンドの応答まで確認する。D の HID 独占・焦点変更・解放は USB in2 設計に従う。
C-GUI は QMP の各入力をゲストの観測応答で区切る。前景 probe が列挙した dev で A make、保持、break を照合し、押下照会が 1→0、WM の raw が make→break、保持中の in2 KEY 反復が 0 件となることを確認する。相対移動 (+12,−7)、左押下、解除は別々に送り、probe の MOVE・BUTTON と WM のポインタ状態を照合する。別窓をクリックし、前景交代の通知後に次のキーが新しい前景へ届くこと、ドラッグ解除後は移動だけで窓が動かないことを確認する。WM から CUI への切り替えを要求して終了した後 (普通の終了だけではループが同じ GUI の像を起動し直す) は CUI で物理キーからコマンドを入力し、cooked 入力の復帰を応答で判定する。

origin/main の 56d7be02d にある sys/kern/gui.c と sys/kern/av_focus.c を基準に、次を両機種の公開入口から試す。

|入口|操作と期待値|
|---|---|
|gui_call|登録の前後とも外部の OWNER_EXIT は INVAL。TRIM_DONE は登録検査より先に exec_trim_done(arg) に届き、その結果を返す。どちらも WM の受信件数は増えない。未登録の通常 op は NOSYS。登録後の通常 op では handler が呼出元の子 owner を受け取る。|
|sys_switch_shell|子 owner は有効パスでも INVAL。owner 1 で NULL と空文字は INVAL、OS32_MAX_PATH 以内に NUL がないパスは NAMETOOLONG。長さ OS32_MAX_PATH−1 の有効要求は成功し、その後の拒否を挟んでも元の要求を一度だけ取り出せる。|
|av_focus_set|未登録時はシェルも INVAL。登録後は特権の owner 1 と、子 owner を保持した正規の WM handler 区間で成功する。同じ子が handler 復帰後に直接呼ぶと INVAL となり、直前の前景が保たれる。|

- 変異は一つずつ実行。register を absent に戻すと最初の子の call が失敗、consume を飛ばすと CUI のまま、CLUT の一要素を恒等に戻すと指定色の画素が不一致、世代比較を外すと旧世代拒否の host 試験が失敗、owner 比較を外すと別 owner の転送で FB が変化する。それぞれ実行時 RED を必須とする。

### 4.1 C-GUI 入力の受入（7450・7455）

| 操作 | ゲストで確認する値 | 単独変異の失敗点 |
|---|---|---|
| P 前景で acquire(3) | NOSYS。前後の WAIT で WM の窓 P・子 owner・独占 0・取得 owner 0・世代が同じ | register 欠落は INIT NOSYS の後 READY 不達、CUI 固定は GUI 起動不達 |
| A make・保持・break | P の同一鍵盤で各 edge 1 件、pressed は 1→0、WM raw と USB REPEAT | KEY capture 欠落は USB make 後も P が空読み |
| 移動 (+12,−7)、左押下、解除 | P の MOVE と BUTTON、Q への前景交代、WM 物理/実効は 1/0 → 0/0 | BUTTON capture 欠落は物理押下後も P が空読み。前景更新欠落は Q の FOCUS 不達 |
| タイトル押下、移動 (+10,+3)、解除、移動 (+12,−7) | ドラッグ枠が動き、解除時だけ確定矩形が (+10,+3) 移る | ドラッグ終了欠落は解除後の矩形不一致 |
| 手順 13 の A make・break | A の GUI_EV_KEY の enqueue 成功差分は Q の make・break が各 +1、P は各 +0。GUI_EV_TEXT は集計外 | |
| 左保持で抜去し再追加 | RELEASE → 元マウスの DETACH → 公開列挙から消失、ボタン・ドラッグ解除。追加後は USB DEV（mouse）→ 公開再列挙 → 新マウスの MOVE と WM の (+12,−7) | 解放欠落は抜去後もボタン保持 |
| CUI 切替 | probe 終了後 CUI を起動し、物理鍵盤で cooked 応答 | consume 欠落は GUI 再起動 |

## 5. 未決

ユーザー判断を要する項目は 0 件。設計時点ではビルド・ゲスト実行・容量実測を行っていない。


## 確定 (2026-11-22、本線の sol 第 3 回「追記で確定可」、PM が反映)
- §4: 「WM の終了の後」を「WM から CUI への切り替えを要求して終了した後」と明記した (普通の終了だけでは、シェルのループは同じ GUI の像を起動し直す)。置き換え 1 か所 (§4 の C-GUI の段落)。
- USB の in2 の設計 (PPC_USB_IN2_DESIGN) の段 C の GUI の配置は、この設計の C-GUI (KAPI の結線と製品の GUI の有効化の間、/sys/shell.bin と /bin/gshell.bin を分け、C-GUI 専用の構成で /bin/gshell.bin を選ぶ) に揃える。
- 本線の先行段: 7d の統合、段 1–2 の共有ビルドと PPC の KAPI の生成、x86 での同期 present の要否の判定と要る時の共通の描画の権限・回帰。PowerPC の担当は C-GUI の起動の道を直して両 CPU (7450・7455) で通した後に製品の GUI を有効にする。
