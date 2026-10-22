## 第9部 外部プログラム実行 (exec)

OS32Xバイナリ形式の実行ファイルをext2から私有 VA 0x80100000 (`MEM_EXEC_LOAD_ADDR`) にロードし、
KernelAPIポインタを引数として実行する。

詳細は **[KAPI_SPEC.md](KAPI_SPEC.md)** を参照。

| 項目 | 値 |
|------|------|
| バイナリ形式 | OS32X v4 (60バイトヘッダ + フラットバイナリ)。`sdk/mkos32x.py` が付与 |
| ヘッダマジック | 0x4F533332 ('OS32') |
| ソースファイル | `exec/exec.c` / `exec/exec.h` / `exec/exec_heap.c` |
| KAPIラッパー | `kapi/kapi_*.c` (自動生成分 + 手動分)。版は [KAPI_SPEC.md](KAPI_SPEC.md) |
| KAPIテーブルアドレス | 動的算出 (KHEAP_BASE + KHEAP_SIZE) |
| ロードアドレス | USER は `MEM_EXEC_LOAD_ADDR` = 0x80100000。常駐 shell は低位配置。OS32X v4 のロード番地・4世代を完全一致検査し、不一致は `EXEC_ERR_INVALID` |
| 特権レベル | **既定で CPL=3** (v2 M1〜M3, 2026-09-03)。例外は常駐 shell (CPL=0)。受理するフラグは `exec/os32x_hdr.c:6` を参照 |
| アドレス空間 | プログラムごとに PD。APP_BAND より下 (カーネル帯 0x100000〜・シェル帯 0x400000〜0x4FFFFF を含む) は全 PD 共有・非 USER。USER にするのは下の「Ring3 の USER 写像」の範囲だけ |
| 共有ライブラリ | 0x80000000〜0x800FFFFF に `/sys/lib/libos32gui.shlib` が常駐 (`kernel/shlib.c`)。.text はアプリ間で共有 (RO+USER)、.data/.bss はアプリ PD ごとに複製 (`shlib_addrspace_attach`、失敗は `EXEC_ERR_NOMEM`)。アプリは stub (ジャンプ表への薄いスタブ) を静的リンクし、版は先頭 4KB の `OS32ShlibHeader` で照合 |
| ヒープ | USER の libc 初期末尾は `page_up(img_end)+PAGE_SIZE` (BSS 端の端数 + 1 page)、その直後に guard_a。exec_heap は別予約 `MEM_EXEC_HEAP_BASE` (0x88000000)、未指定64KiB・明示値はページ丸め/最低64KiB。実行中は EXACT 伸長・副 arena・大塊を `mem_map` で確保する。resident shell/gshell は固定 sbrk 上限0x4B5000、exec_heap 256KiB (小さい明示値のみ縮小) |
| ネスト実行 | 最大 4 段 (ID の池 `exec/appslot.h` の `APP_MAX_APPS`)。深さと資源所有者のアプリ ID は別に管理する。**子が終了すると親に戻る** (親の exec_heap は `exec_heap_restore_state()` で復元) |
| 資源の所有者 | FD / リダイレクト / パイプ / SHM は `res_owner_get()` のアプリ ID (カーネル 0、常駐シェル 1、アプリ 2〜5) でタグ付けし、終了する ID の分だけ回収。切替時に `appslot` が所有者を更新する (`exec/appslot.c:261`)。保護されたカーネル常駐 FD は回収しない |
| 不正ポインタ | ディスパッチャがアプリ帯 / SHM / VRAM の範囲で早期検証。検証しきれないものは「ring3 syscall 実行中フォールトガード」が捕捉し、**アプリだけ kill** (`fault_kill_count`)。設計: archive/kernel_v2/ (アーカイブ: `docs/archive/kernel_v2/PLAN.md`) |
| プログラム専用スタック | CPL=3: `MEM_APP_STACK_TOP` (0x90000000) から可変量 (既定256KiB) を全 map、その直下に guard_b 1 page。常駐 shell/gshell は固定配置 |
| 呼び出し規約 | カーネル側 GCC (System V) + `__cdecl` ラッパー、外部プログラム System V i386 ABI |

### 実行方式

```bash
> ./program.bin     # 直接実行
> program           # 未知コマンド → PATH 探索で *.bin を補完
> exec program.bin  # 明示的 exec
```

`api->exec_run()` で別プログラムを起動すると子として走り、終了で呼び出し元に戻る
(`execve` 置換ではない。2026-09-05 訂正)。戻り値は下表のステータス。

GUI の Run から直に起動した、窓を持たず入力先にも接続していないアプリが
`kbd_getchar` / `kbd_getkey` (GUI 中の `ime_getchar` / `ime_getkey` を含む) で
キー待ち (`WAIT_KEY`) に入った場合、WM は `No keyboard input. Run from a terminal.`
と表示してそのアプリを終了し、既存の `exec_kill` 経路で資源を回収する。
表示枠が使用中なら理由の表示を枠が空くまで保持し、回収は先に済ませる。
端末配下の起動連鎖・窓を持つ GUI アプリ・全画面アプリ・CUI 起動は従来どおりで、
STOP の宛先も「前景の可視窓 (全画面中は画面 owner) → 起動連鎖の末尾」を維持する。
端末注入は読み手とその slot/launch 子孫だけが取得できる。旧 K7 の無関係な
WAIT_KEY との共有承認は v3 で改めた (GUIRUNKEY-1、ユーザー決定 2026-10-18)。

### Ring3 の USER 写像 (exec_run が AS を作るときに立てるもの)

| 範囲 | 属性 | 目的 |
|---|---|---|
| image/libc 0x80100000〜sbrk_end、exec_heap 0x88000000〜exec_heap_end | RW+USER (私有・疎PT) | 初期域と実行中の mem_map 伸長 |
| guard_a=sbrk_end、guard_b=stack_top-stack_size-PAGE_SIZE | 非 present | libc 初期末尾 / stack 境界 (`ring3_guard`) |
| stack_top-stack_size〜stack_top | RW+USER | 可変ユーザスタック全量 |
| 0xA0000〜0xBFFFF | supervisor (e11b1〜) | テキスト / グラフィック VRAM。CPL=3 は KAPI (`tvram_*`) か lease 経由だけ |
| 低位 V86_LOW | supervisor | V86 session 中だけゲストの RAM として使用 |
| SHM | RW+USER (共有 PT、lock 中は RO) | boot 専用口で初期化し、SHM 専用口で RW だけ切替 |
| Unicode の組表 (kernel 所有の 7 頁、T3d1〜) | supervisor | CPL=3 は KAPI `unicode_to_jis` だけ (表は公開しない、[KAPI_SPEC §4-13](KAPI_SPEC.md#4-13-unicode_to_jis-v76)) |
| pool から確保した 32 頁 (SURFACE、9801 BB)、バックエンド固有 BB | supervisor (e11b2〜) | CLIENT lease の私有 VA で描画。Cirrus DISPLAY は授権された lease だけ RW |
| KAPI トランポリン 1 ページ | RO+USER | `int 0x80` スタブ列 |
| 0x80000000〜 shlib .text | RO+USER (共有) / .data は per-app | 共有ライブラリ |

SHM の先頭 1 ブロック (16KB) は DB 結果・エラー文用、末尾 4 ブロックは GUI 用に固定予約する。
`shm_alloc` が配るのは残り 9 ブロックで、解放・所有者回収・全回収でも固定予約を維持する。

通常の AS map/unmap は私有 PT だけを操作し、共有 PT に掛かる要求は無変更で拒否する。
旧低位 USER を前提とした成果物は memory_layout 世代の完全一致検査で拒否する。現行世代・KAPI 版は [sdk/kapi.json](../sdk/kapi.json)、照合契約は [KAPI_SPEC.md §4](KAPI_SPEC.md#4-kernelapi-構造体レイアウト) を参照。

V86 session の全出口 (正常・失敗・STOP・kill) で、低位の PTE の属性を戻し (VRAM/E の setup と teardown の両方で PCD を台帳と一致させる)、
boot 原本の page 0 と開始時に退避した mailbox を書き戻す。低位 RAM は開始時にクリアしてそのまま使うので、開始前の内容は戻さない (`kernel/v86_mem.c`)。
SHM/トランポリン用の PDE USER も復元してから CR3 を読み直し、通常の地図を検査する。V86 中は通常 AS の実行・生成・検査をしない。
V86 から戻った後の DISPLAY/TVRAM は lease を取り直す。拡張グラフィック対応機では出口で 6Ah に標準グラフィック (20h) を書いて戻す
(`kernel/v86.c`、拡張モードのまま戻ると A6h が効かず E0000h が MMIO のまま残るため、[U] 00A6h)。実機では未観測。

### V86 セッションの入場と呼出元 (T7a、`exec/v86_call.c`)

V86 に入るのは CPL=3 の `v86` コマンドが呼ぶ KAPI (`V86_SELFTEST` / `V86_DISKTEST` / `V86_BOOT` / `V86_BOOT2` / `V86_GDC_CAPTURE`) で、呼出元の AS があること自体は断る理由にしない。流れは CUI → 準備 → V86 → 復元 → CUI。

- **入場の条件** (`v86_call_admit`): CUI (console シンク無効)・WM の入れ子なし・呼出元は GUI でない実行中のアプリで親がシェル・生きている AS は呼出元の 1 つだけ・ほかのアプリのスロットなし・セッションが開いていない。外れたら低位を触る前に `OS32_ERR_BUSY` で断る (シェルから直接起動するよう 1 行出す)。GUI 中は `v86_gui_refuse` が断って呼出元を畳む。
- **準備**: mailbox (起動時に KHEAP へ 1 回だけ確保し core が持つ) が無ければ `OS32_ERR_NOSPC`。引数は呼出元からカーネルの置き場へ写す — パスは `VFS_MAX_PATH` の領域、画面取得は `V86Gcap` を確保し、書き戻し先が書けることを先に確かめる。未終端・範囲外は `OS32_ERR_INVAL` で、どれも低位を触る前に返す。呼出元の CR3 を控えて固定 (pin) する。
- **実行**: 置き場と TSS.ESP0 が共有のカーネルスタック (`MEM_CONV_END` 以上、アプリ帯の手前) にあることを確かめ、master の CR3 へ切り替えて実行し、控えた CR3 へ戻す。master の間は呼出元の VA を参照しない。
- **戻り**: 呼出元の AS と世代を照らし、違えば `OS32_ERR_STALE`。画面取得の結果は置き場から呼出元へ写す。最後に置き場を解放して固定を外す (`v86_call_cleanup`、例外の着地からも通る)。低位の復元は上の段落 (全出口で同じ復元を通す)。

### 起動失敗の巻き戻しと強制脱出

- `exec_nest_level++` の後で起動に失敗した場合 (`paging_addrspace_create` / `shlib_addrspace_attach`)
  は **`exec_launch_abort()`** に集約して、longjmp 復帰ブロックと同じ順序で状態を戻す
  (AS 破棄 → nest/owner → 子ガード → 子 heap → pgalloc 予約 → 親の heap / sbrk 上限 / ガード)。
  片方だけ直すと必ず食い違うので、変更時は両方を見る (レビュー #5 ④、2026-09-06)。
- **CTRL+STOP** は CPL=3 アプリを畳む (`ring3_abort_request` を IRQ1 で立て、IRQ1 スタブ (割り込まれた
  文脈が CPL=3 のとき) と syscall 入口の `ring3_abort_check` が `ring3_fault_kill` で回収)。GUI 中も CUI 中も効く。
  CUI の子が既に戻り、終了処理中に届いた STOP はその子の slot とともに捨て、親には引き継がない。
  その `abort_req` は同じ slot を次に使うアプリにも残さない。
- **syscall 境界ポンプ** (`ring3_gui_pump`): アプリが KAPI を呼ぶたび (1 tick に 1 回まで) に WM の
  ポンプ (X4) を回し、計算ループ中でもカーソルとクリックを取りこぼさない (契約 T6)。WM 自身 (owner 1) は除外。
- CPL=3 の KAPI 呼び出しは **IF=1** で走る (`int80_stub` の `sti` / 出口 `cli`。[POLICY_DEBUG §4-19](POLICY_DEBUG.md))。

### GUI アプリのスロットと譲り合い

- **割当**: GUI アプリは起動直後に `gui_call(OP_INIT)` を呼び、戻り値でスロット番号 (0〜3、`GUI_SLOT_MAX` = 4) を受け取る。スロットの先頭は `shm_base + GUI_SHM_OFFSET + 番号 × GUI_SLOT_SIZE` (16KB)。WM はスロットを窓の所有者 (owner) に結び、同じ owner がもう一度 `OP_INIT` を呼べば同じ番号を返す。アプリの終了・kill で回収する。空きが無ければ `OS32_ERR_FULL` (5 本目は起動しない)。実体は `userland/gshell/src/handler.rs` の `op_init` と `wm.rs` の `alloc_slot`。
- **配送**: イベントは窓の所有者のスロットのリングへ書く。`OP_POLL` は呼んだアプリのスロットだけを読む。フォーカスの切替で特別な処理は無い (リングはアプリごとに独立)。
- **譲り合い** (協調切替、スレッドは無い): アプリが WM へ制御を返す (park する) のは `OP_WAIT`・kbd 待ち・polling・`sys_yield`・GUI STOP の WM 返却要求の 5 種だけ (`exec/appslot.c` の `parked_from_*`)。5 番目は次節の形で正常 syscall の完了後に退避する。起こす順は WM の `pick` / `should_park` (`userland/gshell/src/multiapp.rs`)。
- **見え方**: SHM は全 PD 共有 + USER なので、アプリは他のアプリのスロットを読める。GUI の要求・イベントに秘密は無いので、今はこれを許す。

### GUI の KAPI-loop の STOP

GUI アプリが KAPI を呼び続けるループ (get_tick loop など) に入ると、IRQ 側の受付は暴走判定に掛からず、
WM 側も STOP を消費する OP_WAIT / top-level に届かない。この場合の STOP は次の形で WM が畳む (ユーザー決定 A、2026-10-02)。

- IRQ の GUI 受付拒否時、RUNNING・gui・CPL3 の current に `stop_wm_req` だけを立てる。WM の入力読取り直後〜resume/start の隙間に
  IRQ が来たときは shell slot に未配送の要求として保持し、次の正常 syscall・既存 park・WM の abort_clear・回収で消費する (current を誤って kill しない)。
- 正常 syscall の完了後 (戻り値の書込み・callback 復帰の後)、第 5 の由来 `parked_from_stop` で WAIT_POLL に退避する。状態の公開値は増やさず、
  WM の既存 pick/should_park で起こす。resume は EAX を保存し、注入リングを読まない。WM は既存の宛先解決で `exec_kill` する。
- `resume_one` の前段でも keyboard capture (Standalone) → top_level_abort を行う。exec_start が同じ周の X3 より後に戻った場合も、
  未読 raw / deferred / abort_seen を処理してから再開する。マウス・時計はそこで取り込まず、既存の起床順位と tick 制御を保つ。
- ABORTED の OWNER_EXIT は raw の未読 CTRL+STOP make、WM の abort_seen、pending_raw 内の CTRL+STOP make を消費し、次の前景への二重 kill を防ぐ。
  ABORTED の後に遅れて届く CTRL+STOP make も、STOP・修飾キー以外の次の make、新しいマウスボタンの押下、ABORTED から 500 ms (PIT 50 tick) の
  いずれかまで合流して捨てる。修飾キー・break・ボタン解放・押しっぱなし・終了に伴う自動の前景切替では解除しない。CUI と GUI の切替で抑止を持ち越さない。
- 既知の制約: STOP の張本人 (current) と前景連鎖の末尾が違うとき、宛先を畳んだ後に張本人は保存 EAX で再開し、再び WM を塞ぎ得る。
  窓なし・連鎖外の張本人と、GUI アプリの `exec_run` の子 (gui=0) の KAPI-loop は畳めない。協調切替なので KAPI-loop 中は時計も止まる。

### ヒープの大塊と副 arena

USER の AS の割当て。閾値は 64KiB (`OS32_NANO_LARGE` / `MEM_EXEC_HEAP_MIN`) で、C (`sdk/allocator/nano_adapter.c`、newlib nano + SDK の adapter) は要求サイズ (calloc は積)、
Rust は prefix・整列余白込みで `mem_alloc` に渡る実 byte 数で分ける。

- **大塊 (64KiB 以上)**: C は prefix 付きで `mem_map(TOPDOWN)` に直接写し、失敗はそのまま ENOMEM (arena へ落とさない)。free は即 unmap。
  kernel 側の `mem_alloc` (`exec/exec_heap.c`) は TOPDOWN の LARGE extent を取り、取れなければ既存 arena の空きだけを使う (新しい arena は作らない)。
- **小塊**: C は主 arena を `_sbrk` の EXACT で伸ばし、続きが取れなければ flags=0 の副 arena (最小 64KiB、取れなければ必要量で 1 回) を作る。arena ごとに nano の状態を持ち、free list を跨いで繋がない。
  kernel 側は exec_heap の現在端へ EXACT、取れなければ TOPDOWN で arena (最小 64KiB) を足す。
- 大小を跨ぐ realloc は新確保→copy→旧解放で、失敗は旧内容を保つ。完全に空いた副 arena と末尾の空きページは下の trim で返す。

### GUI slot への trim 要求と配送 (池不足時)

シングルタスク・協調切替の前提 (2026-10-09 改訂、ユーザー承認) で、厳密な一巡保証と kernel 側の前景判定は持たない。

- **状態**: 各 AppSlot に `trim_pending` (bit) と `trim_epoch`、kernel に u32 `appslot_trim_epoch` (0=無し、巻戻しごとに +1、満了で飽和)。
  保留の識別は bit で、epoch は TRIM_DONE の照合と観測にだけ使う。bit が立っている slot への再要求は何もしない。
  slot の回収・再利用で両方をゼロにし、古いイベントを次の owner へ渡さない。動的な queue は作らない。
- **要求**: appmem は data/PT の物理池の不足で準備を全巻戻しした後だけ、要求者以外の生きた GUI slot (PARKED / WAIT_KEY / WAIT_POLL) に bit を立てて NULL を返す。
  VA の穴不足・extent 満杯・KHEAP 制御の不足・引数不正では要求しない。割当ての動作 (EXACT→TOPDOWN、arena の巡回) は変えない。
- **配送**: WM は既存の X3 周期 (`wm_cycle`) で `mem_stat(0)` の pending mask を 1 回読み、bit の立つ slot が PARKED・前景でない・未配送 (`sent` が偽) なら、
  そのリングに `GUI_EV_TRIM` (window 0、payload に epoch) を 1 件積んで `sent` を立てる。前景の判定は WM が自分の `front_owner` で行い、USER の申告は使わない。
  queue が満杯なら次周へ送り、既存イベントを捨てない。起こす順は既存の pick / should_park / ラウンド規則のまま。TRIM の未応答を理由とする再配送・追加の resume はしない (通常のイベントによる resume は既存の規則どおり)。
  CUI と終了中の AS へは配送しない。
- **完了**: SDK は handler と allocator の外の安全点で trim → 任意の cache hook を 1 回 → TRIM_DONE (受信バッチごとに 1 回)。kernel は `gui_call` で TRIM_DONE を横取りし、
  saved caller の slot に保留があり epoch が一致するときだけ bit を下ろし、同じ caller AS のまま kernel 側 exec_heap の末尾を trim する。保留なし・不一致は STALE で状態不変。
  `sent` は mask の bit が 0 になったときと owner の終了で下ろす。新しいメモリ KAPI は無い (`mem_stat` の末尾追記だけ)。
- **再試行**: 前景の SDK の malloc/map 失敗時は allocator の lock を解き、`gui_retry` があり `in_trim` でない文脈だけ `sys_yield` 1 回 → 公開入口の操作全体を 1 回再実行する
  (realloc は新確保→copy→旧解放まで含め、1 回目の失敗は旧内容を保つ)。raw mem_map と size 0 は自動で再試行しない。再試行も失敗なら ENOMEM。SDK 側の契約は
  [KAPI_SPEC の T2g g2](KAPI_SPEC.md#t2g-g2-shlib-trim-と-allocator-の再試行)。
- **端末配下の CUI**: 前景 CUI の失敗では裏の GUI に bit を記録して CUI 自身は ENOMEM。親端末の OP_WAIT (X3) で配送され、その後の別の要求が成功し得る。CUI の失敗を同期の pump で成功に見せない。
- `ring3_wm_depth` を更新の排他とみなさない。syscall 入口の `ring3_gui_pump` から trim hook や別 AS の実行を起こさない。pool 0 でも固定 slot/event で通知・閉じる・STOP が動く。

### `sys_ls` の CPL3 callback

公開 slot 12 の ABI と版は維持する。USER 表の slot 12 は、通常の8バイトの
スタブに代えて、文字列写し場の後ろに置く RO+USER の shim を指す。
shim は private int80 selector `RING3_LS_CALL` で14件ずつ値を受け取り、
**CPL3 に戻ってから**元の callback を元の `ctx` で呼ぶ。公開 slot 12 を
直接 int80 で呼ぶ USER は kill し、TRUSTED のラッパー直呼びは従来どおり。

内部入口は caller access で引数・パスを取り込み、非再入の内部入口が持つカーネルの static 領域へ
列挙し、FS から戻った後に検査済みの USER 出力へ写す。callback/ctx は
内部入口に渡さない。FD・確保領域・継続カーソルを残さないため、callback 中の
kill/park/ネストでも FS の後始末を持ち越さない。名前の NUL 以降と padding はゼロ。
FS の戻り値（途中エラーを含む）と、そこまでの列挙順を保持する。

shim はパスを最初に一度だけ自分のスタックへ写す。束の3708バイトとパスの256バイト、
整列を含む3968バイトをスタックに置く（アプリの最小スタック16KBに対して約4KB）。
各バッチで先頭から再走査し、100件なら内部入口は8回呼ばれる。ext2 は読み飛ばす項目と
束の後ろの項目の inode サイズ取得を省く。FAT と HostDrv は列挙結果にサイズが含まれるため、
別のサイズ取得は不要。
列挙の間にディレクトリが変わらなければ順序・内容は同じ。変わると飛ばし・重複が起き得る
（例: callback の中での作成・削除、ホスト側の変更）。
[POLICY_DEBUG §4-26](POLICY_DEBUG.md#4-26-sys_ls-のコールバックから-fs-を触ると一覧が崩れる)
の呼び手側規則を維持する。値を返す公開列挙 API への移行は後続段。

### `exec_run` の構造と分割壁

`exec_run()` は約 280 行だが**関数分割はしない**。内部で `exec_setjmp()` / `exec_longjmp()`
によるコルーチン的制御を持ち、setjmp 後のブロック (ローカル変数の寿命、`saved_esp` の
インライン asm、`exec_nest_level` の増減) は物理的に別関数へ出せない。

1. setjmp 後のブロック (argv 構築 + asm 実行) は分離禁止
2. 拡張は setjmp 前のブロック (パス解決・ファイル読込・ヘッダ検証) にだけ足す
3. 300 行を超えたら、setjmp 前を `exec_load_binary()` として切り出すことを検討する

(構造分析レポート `exec_run_analysis.md` (2026-04-16) は削除済み。必要なら git 履歴。)

### 実行ステータスコード (`exec_status_t`)

`exec_run` 関数は実行結果として `exec_status_t` (定義: `os32_kapi_shared.h`) を返す。

| ステータス | 値 | 説明 |
|---------|---------|------|
| `EXEC_SUCCESS` | `0` | 正常終了 |
| `EXEC_ERR_GENERAL` | `-1` | 一般的なエラー（ロード失敗等） |
| `EXEC_ERR_FAULT` | `-2` | 例外やフォールトによるプロセスの異常終了 |
| `EXEC_ERR_NOT_FOUND` | `-3` | 指定されたOS32Xバイナリが見つからない |
| `EXEC_ERR_NOMEM` | `-4` | メモリサイズ超過、ページング領域確保失敗等のメモリ不足 |
| `EXEC_ERR_INVALID` | `-5` | ヘッダマジック不一致やバージョン非互換等の不正なバイナリ |

### プログラムの一覧

表はここに持たない (2026-09-05 に撤去。ゲームライブラリの所在など古くなっていた)。
正典は各層の配備マニフェスト `userland/deploy.yaml` / `apps/deploy.yaml` / `game/deploy.yaml`
(`tools/deploy_manifests.py` がマージ) と、コマンドは [07_shell.md §7-1](07_shell.md)。
ライブラリの設計書は `docs/tasks/lib*/`、ゲームエンジン 11 ライブラリは
`ske-studio/os32-game` の `docs/`。
