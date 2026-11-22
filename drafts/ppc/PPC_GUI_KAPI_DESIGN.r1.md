# PowerMac の常駐 gshell と GUI KAPI（設計 r1）

## 1. 実物と統合の判断

基準は origin/main の 69d9cef40。ここでいう同じ gshell.bin は同じ Rust/C ソース・同じ公開 ABI・同じ配置名から作る各 ISA の実行像を指す。

1. 同じ役目は既にある。x86 は exec/exec_kapi_init.inc の関数表から kapi/kapi_generated.c:wrap_gui_register / wrap_gui_call / wrap_sys_switch_shell を経て kernel/gui.c の同名の本体へ入る。gui_register は shell owner 1・特権呼出し・非 NULL handler を要求し、pump を保存して入力を raw にする。gui_call は呼出元 owner を WM に渡し、WAIT と WM の文脈を囲う。gui_owner_exit は子の窓を回収し、WM 終了なら登録・pump・入力モードを戻す。build/sources/common.mk の COMMON_EXEC と hooks.mk の HOOK_gui は **既に PPC にも有効**。欠けるのは sdk/kapi_target_ppc.py の absent 指定を通る利用者側の入口。
2. 実装は一つにする。gfx/gfx_present.c の v1、gfx/v2、kernel/console の font source、exec と WM は共通。platform/powermac/display_provider.c:display_provider_select が drivers/video/of_fb/backend_of_fb.c:gfx_backend_of_fb を返す。5b/5c の INDEX8 RAM shadow・同期・世代・幅・保護行を使う。機種に残すのは OF 表示の取得・色設定と FB 転送。USB は in2 に入り、WM が入力を読む。
3. AC-DISPLAY-1～3 と AC-INPUT-1 に沿う。ARCH_DRIFT_AUDIT の B02 は owner の履歴、B03 は段 E より前の記録。現在の platform/powermac/shell_loop_ops.c:gui_default は常に 0、prepare(1) は常に 2。init/shell_loop.c:shell_loop_run と userland/shell/cmd_sys.c の os32gui は既存。USB 設計の C-GUI/D は本票の両 CPU 受入後。独占能力の有効化は D で行う。

sdk/kapi_target_ppc.py の gfx 群は 7 口。gshell/src/wm.rs は set/get_palette、lease.rs は lease_palette、userland/lib/gfx/draw/gfx_cmd.c は hw_submit/get を使う。hardware_scroll・osri_show と gfx2 群唯一の gfx2_check_op は直接の GUI 起動要件から外す。最終 ELF の到達可能な呼出しで照合する。既存の gfx_init・screen_info・dirty/present は再実装不要。

## 2. 境目と容量

以下は既存名を使った結線図・擬似コード。公開スロット番号・引数・構造体を維持する。

    PPC 表 → 生成 wrapper → gui_register / gui_call / sys_switch_shell
    set_palette → gfx_set_palette_hal    get_palette → palette_get
    lease_palette → gfx_lease_palette    hw_submit/get → gfx_hw_submit/get

生成元で対象だけ absent から除き、同じ群に残る absent が必要な実提供者ヘッダまで抑制しないよう include 集合も分割する。sdk/kapi.json の out/in 長検査を保ち、生成器で再生成。gfx/gfx_hw_submit.c と gfx_hw_mono_cpu.c を pending から EXEC へ移す。対応 op は既存のソフト処理と HAL の能力判定を使い、未対応 op の完了件数・戻り値を維持する。

    gfx::init → gfx_init → display_provider_public_init_ok()
    gfx_present_dirty → DISPLAY_V1_PRESENT_OK() → gfx_v1_present_rect
    gfx_backend_of_fb.present_rect → of_fb_present → of_fb_copy

of_fb_copy は左右をワード境界へ広げ、隣画素も RAM から取得し、pitch と protected_rows を守って native store する。gshell に VRAM を渡さない。query の実寸で合成し、字体は既存の ShellService / KCG と kernel/console の供給口で取得する。
backend_of_fb.c:palette は空、query の貸出範囲も空。set の成功を shadow だけで判定すると窓の色が一致しない。機種側で boot 中に既知の固定 CLUT を設定・保存し（rh_observe.c の color! の引数順を参照）、backend にその RGB 表を渡す案とする。論理 index→固定 CLUT の最近色を 256 byte に保持、初期は恒等。palette(first,count,rgb) で対応を更新し、変更時は既存 shadow の表示領域を再転送。of_fb_copy は転送時だけ各 byte を変換し、恒等時は既存高速経路を使う。set/get の論理値を共通 palette と一致させる。query は実装済みの貸出範囲だけ報告。固定 CLUT 設定失敗時は prepare で GUI 不可とし CUI を保持する。

    present 可 = 有効な adapter && 初期化時の世代 == 現世代
                 && 初期化時の owner == 描画を実行する owner

現行 display_provider_present_ok は appslot_cur() 比較。子から GUI handler を呼んでも appslot は子のまま。子の要求を処理する登録済み WM の合成と、子自身の直接 present を区別する。まず合成を既存 pump/owner 1 に戻して実行できる箇所を確認し、同期合成が必要な箇所は共通 exec の信頼された WM 呼出し区間だけ描画主体を shell と解決する。gui_call が handler に渡す資源 owner は子のまま。park/abort で WM 文脈を抜ける際も解除。shutdown は世代を進め unbind、再 init まで旧い操作を拒否する。

    os32gui → sys_switch_shell(SYS_GSHELL_BIN) → shell 終了
    shell_loop_run → gui_take_next_shell（1 回消費）→ prepare → exec_run
    gshell 終了 → gui_owner_exit → 次の常駐像へ

PPC の gui_default は x86 同様 SYSTEM.CFG の GUI を読む。prepare は利用可能な FB・字形・リンク済み描画機能で判断する。CUI 復帰と GUI 読込失敗、連続 fault 3 回のフォールバックは共通 loop に任せる。root に /sys/shell.bin と /bin/gshell.bin を別々に収録する。USB 設計の KB_ROOT_SHELL 差替えだけでは is_gui が偽になり、prepare と fault 回復を検証できない。

容量は未実測。常駐余り約 376 KiB を目安とし、追加 kernel text/rodata を概算 8～32 KiB、色表と変換表を約 1 KiB、追加の画面大バッファを 0 と見込む。PPC ELF の map で text/data/bss とスタック予約を常駐帯端まで照合し、root 内の圧縮サイズ、展開 RAM、kernel 増分を別記する。gshell の静的 GuiState を含む ELF の占有量は shell と同じ帯の再利用として各像の最大値で評価する。

## 3. 実装段（各行の対象 1 ファイルを 1 段とする）

|段|対象|完了条件|
|---|---|---|
|1|build/sources/common.mk|HW コマンドと MONO の共通提供者を EXEC 像で解決|
|2|sdk/kapi_target_ppc.py|上記 8 口が実提供者へ解決。生成物は生成器の出力として機械更新|
|3|drivers/video/of_fb/backend_of_fb.h|固定 CLUT の受渡しを記述|
|4|platform/powermac/display_provider.c|OF 初期化から CLUT と可用性を受け渡し、世代・owner の門を照合|
|5|drivers/video/of_fb/backend_of_fb.c|set/query/copy が同じ変換表を使用、保護行と padding を保存|
|6|sdk/rust/powerpc-os32-none.json（新規）|32 bit BE SysV。整数・ポインタ・浮動小数点 ABI を PPC_USER_ABI と照合|
|7|build/ppc-user.mk|同じ userland/gshell を明示 cargo target で staticlib 化。PPC 用 gfx・cfg、resident CRT、syscalls、既存 newlib nano/allocator と libgcc で app_sys.ld にリンク。shell と同じ guard・objcopy・mkos32x。未解決 extern、x86 asm、SHM の endian 固定を参照元単位で解消|
|8|build/targets/ppc-powermac.mk|kb-user と root の依存に gshell.bin を追加。像更新時に root 再生成|
|9|platform/powermac/shell_loop_ops.c|設定値と実能力により GUI 起動、失敗時に CUI を再利用|
|10|tools/tests/test_of_fb.py|色・世代・部分転送を host fixture で照合|
|11|tools/accept/ppc_gui.py（新規）|既存 ppc_mouse の build / Session を使い製品経路の起動・復帰を照合|

追加の共通化が必要なら変更元ごとに段を分割する。生成した表の手編集や機種マクロによる gshell の分岐は行わない。

## 4. 受入と変異

- host で PPC KAPI binding/table、kernel shell、gshell-host、OF FB を実行。未登録 call は NOSYS、子の register は INVAL、WAIT 後の通常処理、owner_exit 後の再登録、世代違いと別 owner の直接 present の拒否を検証する。色は get の論理 RGB と転送先の index を別々に比較する。
- リンク後に readelf で ELF32・PowerPC・big endian・入口と常駐帯を、map で実提供者と CRT を確認する。両 CPU に同一の PPC 成果物を使う。
- kboot-link-sh を 7450・7455 で各々冷起動。CUI から os32gui を実行し、二つの窓を開く PPC クライアントの gui_call が成功し応答すること、QMP screendump の両矩形の枠・表題字形・背景色を照合する。gshell は register の失敗でも単体動作するためデスクトップ画像だけでは合格にしない。重なり順と前景を変え、dirty 再描画を確認する。GUI から CUI へ戻り再起動し、文字入力と旧 callback の消滅を確認。x86 でも同じ操作列を回す。
- 受入専用 root のみで GUI 像欠落と連続 fault を別試行し CUI に戻ることを確認する。C-GUI はその後に前景 probe と QMP make/break・相対移動・click を応答ごとに同期。D は HID 独占・焦点変更・解放を別票で扱う。
- 変異は一つずつ実行。register を absent に戻すと最初の子の call が失敗、consume を飛ばすと CUI のまま、CLUT の一要素を恒等に戻すと指定色の画素が不一致、世代比較を外すと旧世代拒否の host 試験が失敗、owner 比較を外すと別 owner の転送で FB が変化する。それぞれ実行時 RED を必須とする。

## 5. 未決

ユーザー判断を要する項目は 0 件。設計時点ではビルド・ゲスト実行・容量実測を行っていない。
