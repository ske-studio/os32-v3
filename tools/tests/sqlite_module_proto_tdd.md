# T4c-1 SQLite PAIR と VA0 像の検査

契約: [TASK_T4_T6_MODULES §2-1・§3-4・§7-2](../../docs/tasks/v3/TASK_T4_T6_MODULES.md)。

`make check-sqlite-module-proto-host MUTATE=0` は製品 SQLite の全 amalgamation と
型付き facade を含む VA0 ELF → 現行 VK32 と対の KMOD → 実 GNU11 ILP32
validator/relocator を qemu-i386 で通す。生成器の 29 import の型・版・通常文脈/
IRQ不可の情報と provider 型を照合し、静的 pool 除去 (pool は外部を指す 4 B)、
local memcpy/memset、libgcc member 台帳、世代・CRC・prefix/body を確認する。

① 実リンク ELF の未解決 symbol・外部 data (SHN_ABS)・不支持 reloc を拒否。
② 非整列/隣接/最後の4Bは成功。末尾−2/BSS/整列余白/重複/部分重複/import交差は
先頭の正常 reloc を含め全 buffer (prefix・payload・BSS・guard) 不変。
③ base+body が32bit overflowする場合も out と全 buffer 不変。
④ `build/out/sqlite-proto/measurements.json` に像/body/prefix/load_span/表件数・
節・libgcc member・構成を保存。生成 policy の上限超過を拒否する。
⑤ 実製品像で PAIR 必須、pair 不一致、旧 layout/module ABI/ops v1、boot CRC 不在を拒否。
⑥ 既存 `module_runtime_host.c` の独立 wire fixture を SQLITE の slot/name に合わせ、
実 runtime + validator で PAIR 外し・旧新混在・ops v1 を init 0 回、export 未公開で拒否。
正常 KDEBUG S4 の対照は既存 `check-kdebug-init-host` と `test_mkmod.py`。
⑦ 実 `sqlite_engine.c` (boot adapter を含む)・typed outer・runtime・validator と
NASM stack trampoline をリンクし、実製品像を relocate して init/CRUD を実行。
所有者付き 128 KiB stack 上の init と callback、全 API の本文内番地、prefix 破棄後の
`:memory:` CREATE/INSERT/UPDATE/SELECT/DELETE と prepare/step/close、診断・shutdown を確認する。
owner pool の先頭/末尾 word を壊すと canary は -1/-2、復元後は 0 に戻る。
missing・pair 混在・pool/stack 割当失敗・prefix 返却失敗では stub を保持し全 owner ページを回収。

`MUTATE=1` は payload境界緩和/base overflow削除/import交差削除/重複検査削除に加え、
SQLITE の生成 policy 行から required PAIR を外す変異と pair 照合削除を検出する。
後二者は実 runtime が不正像を受理したことを assertion で観測する。
コンパイル失敗・signal・timeoutは検出成功に数えない。T4c-1 の依頼に従い実装時も
この検査の変異を実行する。ホストの VFS は部品の読込とメモリ DB 用の限定置換で、
ディスク上の DB・NP21/W 起動は PM のゲスト受入に残す。
