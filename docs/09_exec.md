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
| 不正ポインタ | ディスパッチャがアプリ帯 / SHM / VRAM の範囲で早期検証。検証しきれないものは「ring3 syscall 実行中フォールトガード」が捕捉し、**アプリだけ kill** (`fault_kill_count`)。設計: [archive/kernel_v2/](archive/kernel_v2/PLAN.md) |
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
