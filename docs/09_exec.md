## 第9部 外部プログラム実行 (exec)

OS32Xバイナリ形式の実行ファイルをext2から0x500000 (`MEM_EXEC_LOAD_ADDR`) にロードし、
KernelAPIポインタを引数として実行する。

詳細は **[KAPI_SPEC.md](KAPI_SPEC.md)** を参照。

| 項目 | 値 |
|------|------|
| バイナリ形式 | OS32X (40バイトヘッダ + フラットバイナリ)。`tools/mkos32x.py` が付与 |
| ヘッダマジック | 0x4F533332 ('OS32') |
| ソースファイル | `exec/exec.c` / `exec/exec.h` / `exec/exec_heap.c` |
| KAPIラッパー | `kapi/kapi_*.c` (自動生成分 + 手動分)。版は [KAPI_SPEC.md](KAPI_SPEC.md) |
| KAPIテーブルアドレス | 動的算出 (KHEAP_BASE + KHEAP_SIZE) |
| ロードアドレス | `MEM_EXEC_LOAD_ADDR` = 0x500000 (2026-09-06 K3。0x400000〜0x4FFFFF は共有ライブラリ帯域 `MEM_SHLIB_BASE`: [archive/gui_v11/TASK_K3](archive/gui_v11/TASK_K3_shared_lib_band.md)。OS32X ヘッダ v2 の `load_addr` が一致しないバイナリは `EXEC_ERR_INVALID`) |
| 特権レベル | **既定で CPL=3** (v2 M1〜M3, 2026-09-03)。例外は shell (CPL=0 常駐) と `OS32X_FLAG_FORCE_CPL0` (`mkos32x --cpl0`) |
| アドレス空間 | プログラムごとに PD。カーネル帯 0x100000〜0x3FFFFF は全 PD 共有・非 USER。USER にするのは下の「Ring3 の USER 写像」の範囲だけ |
| 共有ライブラリ | 0x400000〜0x4FFFFF に `/sys/lib/libos32gui.shlib` が常駐 (`kernel/shlib.c`)。.text はアプリ間で共有 (RO+USER)、.data/.bss はアプリ PD ごとに複製 (`shlib_addrspace_attach`、失敗は `EXEC_ERR_NOMEM`)。アプリは stub (ジャンプ表への薄いスタブ) を静的リンクし、版は先頭 4KB の `OS32ShlibHeader` で照合 |
| ヒープ | [本体][newlib sbrk (最低 256KB)][ガード][exec_heap] を**ロード時に動的に決める** (固定 1MB 上限は 2026-09-04 に撤廃)。exec_heap の大きさは OS32X ヘッダ `heap_size` (`mkos32x --heap`) があればそれ、0 なら空きを sbrk と折半。実行中の拡張は無い |
| ネスト実行 | 最大 4 段 (ID の池 `exec/appslot.h` の `APP_MAX_APPS`)。Level 0 = カーネル、1 = シェル、2+ = アプリ。**子が終了すると親に戻る** (親の exec_heap は `exec_heap_restore_state()` で復元) |
| 資源の所有者 | FD / リダイレクト / パイプは `res_owner_get()` (= ネスト段) でタグ付け、終了段の分だけ回収 ([10 §10-9](10_notes.md)) |
| 不正ポインタ | ディスパッチャがアプリ帯 / SHM / VRAM の範囲で早期検証。検証しきれないものは「ring3 syscall 実行中フォールトガード」が捕捉し、**アプリだけ kill** (`fault_kill_count`)。設計: [archive/kernel_v2/](archive/kernel_v2/PLAN.md) |
| プログラム専用スタック | CPL=3: アプリ帯の上端から 256KB + その直下にガード 1 ページ。帯 1 枚 (既定) なら 0x7C0000〜0x7FFFFF / ガード 0x7BF000、2 枚なら 0xBC0000〜0xBFFFFF / ガード 0xBBF000 ([tasks/memory/APP_BAND_PDE.md](tasks/memory/APP_BAND_PDE.md))。CPL=0: mem_end 付近 |
| 呼び出し規約 | カーネル側 GCC (System V) + `__cdecl` ラッパー、外部プログラム System V i386 ABI |

### 実行方式

```bash
> ./program.bin     # 直接実行
> program           # 未知コマンド → PATH 探索で *.bin を補完
> exec program.bin  # 明示的 exec
```

`api->exec_run()` で別プログラムを起動すると子として走り、終了で呼び出し元に戻る
(`execve` 置換ではない。2026-09-05 訂正)。戻り値は下表のステータス。

### Ring3 の USER 写像 (exec_run が AS を作るときに立てるもの)

| 範囲 | 属性 | 目的 |
|---|---|---|
| 0x500000〜スタック直下 (`RING3_HEAP_TOP`) | RW+USER (アプリ PD 固有 PT) | 本体 / sbrk / exec_heap |
| ガード 0x7BF000 | 非 present | ヒープ / スタック境界 (`ring3_guard`) |
| 0x7C0000〜0x7FFFFF | RW+USER | ユーザスタック 256KB (帯 1 枚のとき。2 枚なら 0xBC0000〜0xBFFFFF) |
| 0xA0000〜0xBFFFF | supervisor (e11b1〜) | テキスト / グラフィック VRAM。CPL=3 は KAPI (`tvram_*`) か lease 経由だけ |
| フォントキャッシュ 0x01000〜 | supervisor (e11b1〜) | `kcg_read_*` の KAPI 越し |
| SHM | RW+USER (共有 PT、lock 中は RO) | boot 専用口で初期化し、SHM 専用口で RW だけ切替 |
| Unicode 表 0x4A000〜 | supervisor (e11b2〜) | CPL=3 は RO の Unicode lease 経由 |
| 0x6A000〜0x89FFF (9801 BB)、バックエンド固有 BB | supervisor (e11b2〜) | CLIENT lease の私有 VA で描画。Cirrus DISPLAY は授権された lease だけ RW |
| KAPI トランポリン 1 ページ | RO+USER | `int 0x80` スタブ列 |
| 0x400000〜 shlib .text | RO+USER (共有) / .data は per-app | 共有ライブラリ |

SHM の先頭 1 ブロック (16KB) は DB 結果・エラー文用、末尾 4 ブロックは GUI 用に固定予約する。
`shm_alloc` が配るのは残り 9 ブロックで、解放・所有者回収・全回収でも固定予約を維持する。

通常の AS map/unmap は私有 PT だけを操作し、共有 PT に掛かる要求は無変更で拒否する。
旧低位 USER を前提とした成果物は memory_layout 世代 2 の完全一致検査で拒否する (KAPI v70 は不変)。

### 起動失敗の巻き戻しと強制脱出

- `exec_nest_level++` の後で起動に失敗した場合 (`paging_addrspace_create` / `shlib_addrspace_attach`)
  は **`exec_launch_abort()`** に集約して、longjmp 復帰ブロックと同じ順序で状態を戻す
  (AS 破棄 → nest/owner → 子ガード → 子 heap → pgalloc 予約 → 親の heap / sbrk 上限 / ガード)。
  片方だけ直すと必ず食い違うので、変更時は両方を見る (レビュー #5 ④、2026-09-06)。
- **CTRL+STOP** は CPL=3 アプリを畳む (`ring3_abort_request` を IRQ1 で立て、IRQ1 スタブ (割り込まれた
  文脈が CPL=3 のとき) と syscall 入口の `ring3_abort_check` が `ring3_fault_kill` で回収)。GUI 中も CUI 中も効く。
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
