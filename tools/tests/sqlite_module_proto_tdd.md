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
所有者付き `SQLITE_ENGINE_STACK_SIZE` stack 上の init と callback、全 API の本文内番地、prefix 破棄後の
`:memory:` CREATE/INSERT/UPDATE/SELECT/DELETE と prepare/step/close、診断・shutdown を確認する。
owner pool の先頭/末尾 word を壊すと canary は -1/-2、復元後は 0 に戻る。
missing・pair 混在・pool/stack 割当失敗・prefix 返却失敗では stub を保持し全 owner ページを回収。

`MUTATE=1` は payload境界緩和/base overflow削除/import交差削除/重複検査削除に加え、
SQLITE の生成 policy 行から required PAIR を外す変異と pair 照合削除を検出する。
後二者は実 runtime が不正像を受理したことを assertion で観測する。
コンパイル失敗・signal・timeoutは検出成功に数えない。T4c-1 の依頼に従い実装時も
この検査の変異を実行する。ホストの VFS は部品の読込とメモリ DB 用の限定置換で、
ディスク上の DB・NP21/W 起動は PM のゲスト受入に残す。

## T4c-2 一括公開と rollback

同じ実像/adapter の試験で `services_boot` を実行する。実際の FEP buffer 確保・
abort・辞書 close と Unicode loader を使い、SQLite の group・MEMSYS5・shutdown は
再配置した製品本文で実行する。画面出力とディスクを置換し、settings は種を入れるため
RW の一時 RAM DB、辞書 open は RAM DB の実 stmt で代える。ディスク上の実 IME SQL は
既存 `check-vfs-fd-path-host` / `check-fep-budget-host`、ゲスト全体は PM の確認範囲。

正常対照は公開を最後まで閉じ、設定 224 KiB と既定 192 KiB、APP の割当拒否後も
FEP stmt の実行を確認する。module 欠落/対不一致/画像・pool・stack の確保失敗/
SQLite init/FEP の二つの buffer/部分 stmt/CRC 注入/辞書欠落/予約不足は占有 0 の MINIMAL。
settings close・FEP close・shutdown 失敗は MINIMAL_QUARANTINED、owner/guard を保持。
Unicode の欠落/旧形式/I/O/確保失敗でも SERVICES_READY、かなの算術変換は生存する。
ロード済み 7 頁は全 rollback で保持する。再呼出しは副作用 0。

追加変異は早期公開、隔離 owner の free、FEP close より先の module abort、Unicode の誤回収。
いずれもコンパイル後の assertion failure だけを RED と数える。通常対照を先に実行する。
`check-db-errstr-host` は内部 SQLite を ready にしたまま public を閉じ、全 DB 口の stub と
副作用 0 を確認する。`check-x11-shared-host` は未公開 IME 管理口と CUI/GUI 生入力を確認する。
ページ監査はこの fixture の合成 count を合格根拠にせず、既存の memory/memmap 監査検査と
NP21/W の diff=0 を使う。SA_ONSTACK の既存 overflow 検査は実 IDT の再現ではない。

## T5-0b stack 採否と fault の試験境界

製品 `sqlite.mod` を各ケース別の新しいプロセスで実行する。初期化/共通CRUD込みの
塗り/high-water は式深度100 (101はprepare拒否)、複合SELECT5項、1024行sort、
1024行index作成、OMIT_TRIGGERのprepare拒否を観測する。深度100の実測24,388Bに
基づき必要容量32KiBをruntimeで照合する (ユーザー決定2026-10-18)。
`check-sqlite-stack-host MUTATE=1` は正常対照の後、16KiBへ縮める変異だけを実行する。
深度100のSQL負荷がNPガードへ到達するhost signal seamを観測してruntime REDとする。
これは実IDT/#DF配送の代用ではない。
rollback/隔離の所有頁期待値は本体の定数から計算する。NPガードは追加1頁のまま。

既存SA_ONSTACKケースはhostの保護違反と診断関数単体だけを試す。IDTの#PFは
あふれたstackへフレームを積むので、このhost経路を#DF受入の代わりにしない。
`check-double-fault-host` は製品GDT/IDT/TSSの初期化、CR3の控え、診断出力を
privileged命令のseam付きで実行する。task gateを割込みgateへ戻す/誤CR3変異は
runtimeの記述子照合でRED。CPUのtask switch自体はNP21/Wの実guard注入で受ける。
`make kernel DF_GUARD_TEST=1` はbootのkernel guard、`=2` はservices起動後の
engine guardへESPを寄せて実pushする。通常 `=0` は呼出し無し。各試験像はcoreと
対のsqlite/kdebugをまとめて配備し、診断後は停止したままかを見る (詳細はT5-0b報告)。
