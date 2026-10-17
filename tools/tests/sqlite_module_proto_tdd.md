# T4a-2 SQLite VA0 prototype

契約: [TASK_T4_T6_MODULES §2-1・§3-1・§7-1](../../docs/tasks/v3/TASK_T4_T6_MODULES.md)。

`make check-sqlite-module-proto-host MUTATE=0` は実 SQLite 全 amalgamation の
VA0 ELF → KMOD → 実 GNU11 ILP32 validator/relocator を qemu-i386 で通す。
生成器は専用 23 import の型・版・通常文脈/IRQ不可の情報を生成し、provider の
型をコンパイラで照合する。製品 SQLite/FEP/割当器・KAPI・配置定数は変更しない。
VFS のコピーだけ tick accessor と外部 pool 引数へ変換する。静的 pool 除去、
local memcpy/memset、libgcc member 台帳、世代・CRC・prefix/body を照合する。

① 実リンク ELF の未解決 symbol・外部 data (SHN_ABS)・不支持 reloc を拒否。
② 非整列/隣接/最後の4Bは成功。末尾−2/BSS/整列余白/重複/部分重複/import交差は
先頭の正常 reloc を含め全 buffer (prefix・payload・BSS・guard) 不変。
③ base+body が32bit overflowする場合も out と全 buffer 不変。
④ `build/out/sqlite-proto/measurements.json` に像/body/prefix/load_span/表件数・
節・libgcc member・構成を保存。生成 policy の上限超過を拒否する。
正常 KDEBUG S4 の対照は既存 `check-kdebug-init-host` と `test_mkmod.py`。

PM の `MUTATE=1` は payload境界緩和/base overflow削除/import交差削除/重複検査削除を
実行時の戻り値・buffer観測で検出する。コンパイル失敗・signal・timeoutは検出成功に
数えない。生成器側の変異は既存 mkmod 試験と併用する。実装者は変異を実行しない。
