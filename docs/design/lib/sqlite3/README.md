# lib/sqlite3/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3 (D1 で移した)
ソース: `lib/sqlite3/`

### os32_sqlite_vfs

| 段 | 内容 |
|---|---|
| 外部データ | **禁止**。`tick_count` は `ktime_ticks()` のアクセサに置き換える (`os32_sqlite_vfs.c` の 3 か所)。`mkmod.py` は外部データへの `R_386_32` を見つけたら**生成を拒否** |

| 段 | 内容 |
|---|---|
| エクスポート表 | カーネルが呼ぶ入口 (`sqlite3_open_v2` … `db_mem_used` が使う `sqlite3_memory_used`)。`kapi_db.c` / `ime_dict.c` は `os32_sqlite_vfs.h` のマクロ層で表を引く |

### os32_sqlite_module

池 (第一候補 KERNEL_SLACK)、CPL=0、supervisor。
**MEMSYS5 512KB**
engine stack 32KiB + NP 1頁
**モジュール init が池から取り** `SQLITE_CONFIG_HEAP` へ。stack 上端は `sqlite_engine_stack_top` 変数。

module body + MEMSYS5 512KiB + engine stack 32KiB + NP 1頁、ロード時はprefixを加算。

| 項目 | 決定 |
|---|---|
| MEMSYS5 の量 | **512KB、全機種同じ** (§4-2、§4-3)。モジュール init が池から取る (R7、owner=sqlite) |
