# sdk/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN・DEP_DIR_D2_DESIGN・MEMMAP_V3・VER_INFO_DESIGN・API_CONTRACTS (D1 で移した)
ソース: `sdk/`

**trim は常時ではなく、池が足りないときだけ裏のアプリへ通知して返してもらう**。処理は SDK (malloc とイベントループ) の中で行い、**アプリは意識しなくてよい** (大きなキャッシュを抱えるアプリは任意で捨ててよい)。CUI は裏にいないので対象外

**(ii) 大きな塊** (メディアファイル・大きな文書・画像面) — **閾値は 64KB 固定** (D23): malloc は 64KB 以上をヒープではなく `mem_map` の別領域に回す。

> [要確認] 現行 sdk/allocator/nano_adapter.c は既存 arena で確保できる場合を先に扱う。64KB以上を常に別mapへ回すという記述とは異なる。_sbrk の伸長は sdk/crt/syscalls.c から同adapterへ転送する。

SDK (malloc とイベントループ) がヒープ末尾の空きページと保持中の大きな塊のうち返せるものを `mem_unmap` で返す。**アプリは意識しなくてよい** (大きなキャッシュを抱えるアプリは通知を受けて任意で捨ててよい — SDK が任意のフックを 1 つ出す)。CUI は裏にいないので対象外。

**アプリの中の断片化** (ヒープの内部断片化) は**アプリのアロケータの領分** (newlib malloc / kheap)。
大きな塊をヒープの外の別領域 (`mem_map`) にすることで、ヒープが大きな穴を抱えて縮められなくなる問題は軽くなる。

<a id="trim-contract"></a>

### T5a. TRIM と allocator の再試行

戻り page hint は `HOOK_PAGES_TOTAL` と shlib の `hook_pages` に記録するだけで、DONE の引数にはしない。

シングルタスク・協調切替のもと、GUI flag が有効な確保失敗だけ
`sys_yield` 1回 → 確保操作全体1回を行う。2回目失敗は NULL。
hook 中・flag 無し・allocator busy の再入・入口拒否・size 0 は yield 0。

C resident CRT はこの欄を除外し、gshell は retry を有効化しない。

### gen_kapi

`sdk/gen_kapi.py:569–581` は body が target より優先、target省略時は name。

`sdk/gen_kapi.py:569–580` は既にbody/targetを解決するので生成ロジック追加は不要。

### os32x_hdr

C / Rust の生成物と**各コンパイル単位**に ABI 識別を結び付けて最終 ELF の印・旧シンボル・配置を照合する

- sdk/os32x_hdr.py:264–271 の effective_min_api が生成済み OS32X_MIN_API まで要求版を上げる。新ユーザーランドは新カーネル専用で、旧カーネルは exec 時に拒否する。app.conf の変更と到達しない表示ガードは不要。

### kapi_rust_gen

**Rust 生成器 (`sdk/kapi_rust_gen.py:45`) は未知の非ポインタ型を `u32` に倒さず生成失敗に**する

## 索引

- [sdk/allocator/README](allocator/README.md)

- [sdk/crt/README](crt/README.md)

- [sdk/include/os32/README](include/os32/README.md)

- [sdk/include/os32/os32_gui_shared](include/os32/os32_gui_shared.md)

- [sdk/include/os32/os32_kapi_shared](include/os32/os32_kapi_shared.md)

- [sdk/kapi](kapi.md)

- [sdk/rust/os32api/src/README](rust/os32api/src/README.md)

- [sdk/rust/os32api/src/gui/README](rust/os32api/src/gui/README.md)

- [sdk/rust/os32api/src/gui/types](rust/os32api/src/gui/types.md)
