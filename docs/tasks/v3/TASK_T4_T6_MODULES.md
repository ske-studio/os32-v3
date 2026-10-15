# TASK_T4_T6_MODULES — SQLite・FEP・起動モジュールの詳細設計

> 状態: **設計中 (2026-10-15、再照合 r2、ユーザー確認待ち)** — 本票の実装・受入は未実施。元設計の独立レビュー往復3 Approve は §8 の履歴であり、この再照合版の承認ではない。
> **レビューの経過 (PM、2026-10-16)**: astra が起草 (r2・r3) → Opus 5.5 が独立レビュー 2 往復 (r1「直してから」、r2「確定してよい (未決の争点はユーザーへ)」)。往復の記録は `~/os32-tmp/run/t3design/T4_T6_MODULES/` (`opus_r1.md`・`astra_r2_response.md`・`opus_r2.md`)。**ユーザーに決めていただく争点**: SQL の 3 入口の拒否時の扱い、/persist の媒体 (T5b-6 の前) — 末尾の「未決の争点」。実装の前にユーザーが確認する。
> 再照合: Codex、main `c81b27bed7c1b0d05e5e7d9de9763c4eded844e5` (2026-10-15)。元設計: Codex gpt-6-astra、調査基点 `3c4171a784cc47720392ab55b3c4a24aee17e6f3`、レビュー基点 `59c4285`。D1〜D36・契約・順序は維持し、実装済みの部分と今回の実装案を分ける。
> 改訂: **r3 (2026-10-16)**。Opus 5.5 の再照合版レビュー r1 (P1なし、P2 1件・P3 5件) を反映。状態行は共通依頼の指定を維持し、上記mainが現在の先頭であることを再確認。採否は `astra_r2_response.md`、未決は§9。実装・受入済みとはしない。
> 上位契約: [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §3-5・§4・§6・§7。前提: [T2](TASK_T2_APPBAND.md) 完了 → [T3](TASK_T3_LAYOUT.md)。この票は形式、状態、移行と失敗処理の実装契約を補う。D番号の決定を変更しない。

## 0. 再照合

根拠の `file:line` は上記基点の `/home/hight/os32-v3/` からの相対位置。[実測] は現物の値、[推] は設計見積もり、未測定は受入前の関門。単一利用者・単一タスク・協調切替を前提とし、バグ・装置の誤動作・資源不足から戻れる範囲を明記する。認証・暗号・権限分離の追加は行わない。性能判定は P100/32MB の前景1本 (上位票:31)、装置の新しい扱いは [HW3] の資料＋BSD/MIT実装を根拠にする (`docs/CONSTRAINTS.md:97`)。

| 元票の前提・着手条件 | 判定 | 今の根拠と本票での扱い |
|---|---|---|
| T2c〜h の map/allocator・trim・owner/STOP 待ち | 吸収済み (T2) | `docs/tasks/v3/TASK_T2D_T2H.md:304,337,349`、`docs/tasks/agents/HANDOVER_2026-10-09.md:11`。受入済み機構を使い、台帳の未採取を完了扱いにしない |
| T2 → T3 → T4 → T5a/b/c → T6b | 維持 | `docs/tasks/v3/TASK_MEMMAP_V3.md:488`。T3 配置の受入と再計算後に本票の実装へ。LAN L-B・DBGREF S3 等の前段ゲートは `docs/tasks/agents/HANDOVER_2026-10-09.md:20,22` |
| e11 公開版・4世代を後で照合 | 吸収済み (T2/DBGREF) | `sdk/kapi.json:2`、`:3`: format=4、KAPI ABI=1、memory=3、shlib=2、機能版75。T3後は生成した値を再取得し、75を未来版として固定しない |
| 専用module核・生成器・リンカを新設 | 要改訂 (既存を拡張) | DBGREF S0〜S2、`kernel/module_image.h:7`、`kernel/module.c:37`、`tools/mkmod.py:15`、`build/module.ld:1`。kdebug専用policyを一般化する (§2) |
| SQLite固定配置・rootがPG/台帳より先 | 維持 | `build/kernel.mk:212`、`kernel/kernel.c:453,524,575,721`。EARLY化とSQLite分離は未実装 |
| B1 checked copy と B3/B4・F2/F3 は別 | 維持 | `docs/tasks/v3/TASK_T2D_T2H.md:94,587`。`db_exec` の切捨て・finalize先行は `kapi/kapi_db.c:610`。旧SQL入口も含む拒否時のstmt契約は末尾の争点へ |
| 512KiB単一MEMSYS5・FEP優先取り分 | 維持 | D27、現行は384KiB (`lib/sqlite3/os32_sqlite_vfs.c:34`)。512KiB化・代替stack全面化・取り分保証はT4 |
| FEP予約量・S/M/L最大負荷・断片化保証 | 未測定 | 元票 §3-3 を維持しT4a-3/4で数値化。過去の常駐38KBだけを予約量にしない |
| LAN/82557は将来の未実装driver | 要改訂 (移設対象に) | `docs/tasks/realhw/TASK_LAN_82557.md:3` はLB-2c送信まで、LB-3受信/IRQは未受入。T5cで完成済み範囲を確認して移す。L-B完了を推定しない |
| gfx資源識別・予約をcoreに残す | 維持 | R7FIX2後はPEGCのFFF00000h窓 (`include/memmap.h:376,395`)。旧F00000h固定期待へ戻さず、永久予約とmodule回収を分ける |
| HostDrv rootの管理ext2 DB配置 | 維持 (配置は判断待ち) | 元票 §3-5、`docs/tasks/v3/DOCS_REORG_T3.md:104`。LAN版/hostの段1 (`fs/sfs_lan.c:30,100`) はroot起動やDB整合性を提供しない |
| 8MB/17MB/実機の構成別受入 | 要改訂 (実施時期のみ) | `docs/tasks/DEFERRED_TESTS.md:19` の10-14決定で最終段へ。途中は64MB、D9/D21の最低条件・必須サービスは維持 |
| 像の余裕はT2終了後に再計算 | 吸収済み (今回の既存像測定) | `build/out/kernel.map:4452` は `__bss_end=0x1940C0`。下記計算。T3後/本票各段の量は未測定 |

**既存像の実測 (2026-10-15)**: `KERNEL_LOAD_ADDR=0x100000`、`MEM_KERNEL_IMAGE_MAX=0x99000=626,688 B` (`include/memmap.h:34,259`; 1MiB−KHEAP176KiB−KAPI4KiB−guard8KiB−SHM224KiB)。本体占有 `0x1940C0−0x100000=606,400 B`、残り **20,288 B**。KHEAPのページ切上げ後の余裕は `0x199000−0x195000=16,384 B`。DBGREF直後の30,176 B (`docs/tasks/v3/TASK_T2D_T2H.md:547`) から **9,888 B増**であり、全量をLANだけの増分とは呼ばない。
`build/out/kernel.bin` は389,724 B、`sqlite.bin` は374,968 B、`vmkernel.lz4` は500,430 B (現508KiB上限まで19,762 B)。map:3831の `os32_build_commit` の位置をkernel.binで読み `c81b27b` と照合した。これは既存成果物の測定であり再ビルド/ゲスト検証ではない。T3で帯が変わるため、この20,288 BをT4以降の使用承認枠にせず、§7の各段で同じ式を新しい定数・mapに適用する。

## 1. 現在との差と共通境界

基点では `build/os32.ld` と `build/kernel.mk` が SQLite を別セクション・固定宛先へ出力し、`kernel/kernel.c` が直接初期化する。`kernel/shlib.c` はモジュール再配置器ではない。`boot/vk32_boot.c` は複数エントリを検査するが、占有は raw_size、集積との非重複は低位集積を前提にする。`kernel_main` のルートマウントは `paging_init` / `memory_boot_init` より先。これらを変更済みとみなさない。

### 1-1. 基点で照合した接続先

| 実物 (file:line / シンボル) | 現在と後続の区別 |
|---|---|
| `build/os32.ld:85` (`__sqlite_start`)、`build/kernel.mk:212` (`--sqlite-addr`) | 固定 SQLite の出力を継続。module 形式には未移行 |
| `kernel/kernel.c:453` (`vfs_mount`)、`:524` (`paging_init`)、`:575` (`memory_boot_init`)、`:721` (`os32_sqlite_init`) | root → PG/台帳 → SQLite の現行順。§5 の並べ替えは後続設計 |
| `boot/vk32_boot.c:43` (`vk32_boot`) | 既存 raw_size の検証。load_span/BSS 込み形式は本票で実装する |
| `exec/redir_access.c:269` (`copy_caller_cstr`)、`kapi/kapi_db.c:205` (`db_copy_input`) | T2d の B1 は着地。全 DB facade の結線完了ではない |
| `kapi/kapi_db.c:598` (`kapi_db_exec`) | 旧 stmt の finalize 後に SQL をコピーする経路が残る。§3-1 B3 の修正対象 |
| `kernel/ime_dict.c:63` (`ime_dict_open`)、`fs/vfs_fd.c:292` (`vfs_validate_sqlite`) | 現行 FEP open と世代検査の入口。RO FEP・group/identity の追加契約と区別する |

T2d の B1 と本票への引渡しは `docs/tasks/v3/TASK_T2D_T2H.md:94,582` が正。T2f/g/hの機構は使えるが、GUI KAPI-loopのSTOP修正をB4のengine_active/代替stack保証と読み替えない。実装は既存の `kernel/module.c/.h`、`kernel/module_image.c/.h`、`tools/mkmod.py`、`build/module.ld` を拡張し、importの生成入力をmoduleごとに分ける。内部ABIは公開KAPI表に混ぜない。

### 1-2. 台帳から受けるIDと関門

`docs/tasks/v3/DOCS_REORG_T3.md:24,51,74,89` から **X-11・E11-A2b・X-1の一部**を受ける。未実施/閉鎖の正典は `docs/tasks/DEFERRED_TESTS.md:61,62,64`。ここでは担当と判定先だけを固定し、証拠なしに閉じない。

| ID | 本票の受け口・完了を判定する段 | 境界 |
|---|---|---|
| X-11 | T4b-5で12口の共有状態・呼出し主体・失敗時副作用を列挙し、正常な親子/park-resumeの対照を実行 | IME 9口 (`trygetchar/toggle/set_mode/switch_dict/user_delete/user_export/user_clear/trygetkey/feed_key`)、`exec_last_result/gui_call/con_sink_read`。pipeは閉鎖済みで含めない |
| E11-A2b | T4b-7でraw KAPI待ち利用者の明示ゲスト試験 | kbd/IME/serial/FMの待ち、CUI/GUI STOP、通常完了、内部ログ中は中断しない対照。B4のengine中断禁止と一緒に確認 |
| X-1 (本票分) | T4b-1でDB文字列、T4b-8で残るNUL入力とkprintf可変引数、T5a-1でFEP文字列 | ページ0のNP化はT7。残る全公開文字列口の一覧を作りB1済み/今回/他票を対応させ、DB/FEPだけでX-1全体を閉じない |

X-11の出発点は `~/os32-tmp/evidence/2026-10-07/audit-classification.md:33,43,57,60`。現在は全体の直前exec結果 (`exec/exec.c:2190`)、先着単一reader (`kernel/con_sink.c:251`)、WMへ実ownerを渡す入口 (`kernel/gui.c:66`)、共有FEP (`kernel/ime.c:603,641,715,767,850,891,912`) である。今回の方針案は、単一利用者の共有サービスという意味を保ち、正常な利用順・無効値・取消・出力寿命を明文化すること。本人別の結果保存や新しい権限表を自動追加しない。共有入力を二重消費する反例が出た場合は既存のWM/前景配送との接続で解消可能か確認し、契約選択が残るものは「未決の争点」へ送る。T4b-5の判定前にX-11を吸収済みとはしない。

## 2. モジュール形式と呼出し契約

### 2-1. バイト形式案

kdebug 固有の現行契約は [TASK_DBGREF §3-2](TASK_DBGREF.md#3-2-部品の形式--t4-と同じ形式として-1-つのスキーマに確定-q3)。LB-1 で ops は**版 2、18 口**となり、旧版は ABI_MISMATCH で拒否する。

little-endianの明示的decodeを使い、C構造体のcast/sizeofでファイルを読まない。**KMOD v1・144Bヘッダの項目順は既に確定** (`kernel/module_image.h:7,23`、`docs/tasks/v3/TASK_DBGREF.md:131`)。下表は意味の表であり再配置案ではない。C/ASM/Pythonはこの順序から生成・照合し、別スキーマを作らない。

ヘッダword 0〜35は `magic,format,header_size,image_size,kapi,layout,module_abi,id,phase,flags,mem_size,payload_off,payload_size,text_off/size,ro_off/size,data_off/size,bss_off/size,reloc_off/count,import_off/count,export_off/count,names_off/size,init,stop,pair_crc,image_crc,ops_version,reserved0,reserved1`。表offsetはcontainer基点、節・入口・reloc書込先・slotはbody基点。relocは4B、importは `{u32 hash; u16 type_ver,kind; u32 slot_off}`、exportは `{u32 hash; u16 slot,ver; u32 entry_off}` の12B。名前は順序付きNUL列、63B上限、hashと実名を照合し衝突は生成失敗。CRCはimage_crc欄 (byte128〜131) を0にした容器全体 (`docs/tasks/v3/TASK_DBGREF.md:132,158,164`)。
現KDEBUG policyはID=1/POST_ROOT/PAIR必須/stop=0/ops=2・18口/画像とbody各256KiB/reloc≤16,384/import≤1,024/export≤64 (`kernel/module_image.c:49,103`)。T4a-1でID・phase・stop・ops・サイズ上限を生成policyへ移し、KDEBUGの現契約とOFF/RUNNING/拒否診断を保持する。SQLITEの初期画像/body上限案は各512KiB [推]、load_span上限・表件数はT4a-2の実画像で確定し、超過を握り潰さない。**374,968Bの現SQLiteファイルは現256KiB上限に入らない**。
wire名 `kapi_abi_gen` は現KMODではKAPI機能版75を入れる欄であり、D35のKAPI ABI世代1とは別 (`docs/tasks/v3/TASK_DBGREF.md:141`、`sdk/kapi.json:2`)。意味を黙って入れ替えず、4世代は生成物の印と配備一式で照合する。`module_abi_gen` はmodule別import定義のFNV-1a、`ops_version` はexport契約。KDEBUGのpair CRCは維持。EARLYには自己を含むVK32 CRCを埋める循環を作らずPAIR無しをID別policyで許し、VK32全体/entry CRC・世代・配布一式で照合する (T5b-1の生成試験)。

| 項目 | 意味と検査 |
|---|---|
| magic, format_version, header_size, image_size | 完全長と既知の形式の完全一致。未来版・末尾欠落・不明必須フラグを拒否 |
| kapi_abi_gen, layout_gen, module_abi_gen | T2/P7 の世代検査 + 内部 import/export 契約世代。不一致を init 前に拒否 |
| module_id, phase, flags | 許可リストのID、EARLY/POST_ROOT、必須区分。ファイル自身の宣言だけで必須性や実行権限を決めない |
| mem_size, payload_offset, payload_size | payload はベース0画像 (text/rodata/dataと整列穴)。mem_size はBSS込みページ切上げ。payload_size≤mem_size、上限・overflowを検査 |
| text_offset/size, ro_offset/size, data_offset/size, bss_offset/size | 全範囲がmem_size内、互いに非重複。BSSはpayloadの外。実行入口はtext内、書込slotはdata内 |
| reloc_offset/count | image内のu32書込位置列。4B全体がpayload内、重複・重なる位置を拒否。BSSの再配置は生成側で禁止 |
| import_offset/count, export_offset/count | 名前/版/slotまたは入口offset。名前長に上限、未知名/重複名/slot重複を拒否 |
| init_offset, stop_offset | text内の入口。stop不在なら「停止不能時に回収できない」区分。無条件freeに進まない |
| image_crc | 転送破損検出。信頼/権限の代用ではない |

全範囲は `off <= limit && len <= limit - off`、件数は `count <= remaining / entry_size` で検査する。reloc が import slot と重なる場合も拒否する。各 `R_386_32` の元の値が内部画像の有効offset (許可したone-pastシンボルだけmem_size) であること、ロードbase加算がoverflowしないことを検査する。実行時にもreloc書込offset `r` を `r <= payload_size && 4 <= payload_size-r` で検査し、BSS/末尾整列/prefixへの書込みを拒否する (生成時の禁止だけに頼らない)。内部PC32は生成時に対象/位置を検証して表から除外。外部データ、未解決シンボル、未対応relocを生成失敗にする。意図的な MMIO 定数をrelocへ偽装しない [C5]。

import は名前だけでなく関数型の契約版を照合し、全て生成したC宣言とi386 cdeclスタブを使う。`memcpy`/`memset` はモジュール内のfreestanding実装をリンクする案とし、i64除算等に必要なlibgcc部品はモジュールへ明示的に静的リンクし、使用archive memberを生成台帳に残す。拒否するのは未解決の暗黙依存であり `__divdi3` 等の使用自体ではない。newlib全体は持ち込まない。SQLite の `tick_count` は `ktime_ticks()` へ。IRQ中に呼べるimport、通常文脈専用importをメタデータで区別し、所有者の登録口を直呼びで迂回させない。

**in-place形式は `header → reloc/import/export/名前の表 → ページ整列 → payload → BSS/末尾整列` に固定する。** 表はすべて `[header_size,payload_offset)` 内で互いに非重複、payload_offsetはページ整列。module headerのmem_sizeはbodyの占有、`image_size = payload_offset + payload_size`、container_phys自体もページ整列でなければ拒否し、全ロード占有 `load_span = payload_offset + mem_size` はoverflow検査しページ整列する。BSSは `[body_base+bss_offset, ...+bss_size)`、body_baseはcontainer_base+payload_offset。したがってBSSとprefixの表は交差しない。VK32 entryのmem_sizeはこの **load_span全体** を保持し、module headerのbody mem_sizeと混同しない。

prefixを処理後に返す場合、exportの解決済み関数表・dependency/owner/generation等の継続利用情報を先に固定容量のmodule recordへコピーする。保存容量不足はinit前に拒否。prefixを指す文字列/表ポインタをRUNNING状態へ持ち越さない。BSSゼロ→reloc→importをbodyだけへ適用した後、prefixページを返せる。成功/隔離したbodyページは保持する。現DBGREFの定常bodyは36,864B、prefixは12,288B、load_spanは49,152B、ファイルは44,840B (既存 `build/out/kdebug.bin` のword10〜12をdecodeした実測)。

**POST_ROOTの画像読込も本体へ直接行い、別の全画像stagingを取らない。** 小さな固定header bufferへ読み、既知形式・完全長上限・load_spanの算術・許可module IDを検査 → owner付きload_span連続割当 → prefix+payloadをその割当へ短読検査付きで読む → 全画像CRC/範囲/表を検証する。検証中にファイルを再読してrelocを書き換えず、確保した写しだけを参照する。全体検証失敗はinitを一度も呼ばず割当を返す。この領域はR4で許すmodule本体 (prefixを含む) であり、別の400KiB連続scratchではない。EARLYは同じ形式の同梱域を直接検証する。ピークにはprefix+bodyを全量計上し、prefix解放後を定常量と区別する。

### 2-2. 状態と公開

`ABSENT → HEADER_CHECKED → OWNED_IMAGE → VALIDATED → RELOCATED → STARTING → RUNNING`。
エラーは公開前なら `ROLLING_BACK → ABSENT`、参照を断てなければ `QUARANTINED` または起動停止。SERVICES_READY後の unload/reload は v3 のこの票に入れない。DBGREF票:28の「実行時に取り外し」はこの票へ機能を追加する根拠にせず、上位R7 (`docs/tasks/v3/TASK_MEMMAP_V3.md:252`) を優先する。boot transaction内のRUNNING依存先は、利用者未公開である限り後述の逆順rollback対象に含む。

内部 API 案 (既存 `module_image_header/validate` は検証核として残し、以下は共通化後の責務名):

- `module_validate(bytes, size, policy, plan_out)` は書込・割当なし。検証済み offset/サイズだけを返す。
- `module_load(plan, owner, backing)` は上記の直接読込/検証済みbodyへBSSゼロ/再配置/importを行う。連続RAMはSLACK優先、EARLYは同梱ownerからload_spanを移譲して同じ核を呼ぶ。
- `module_start(id)` は init を通常文脈で実行。公開表の初期値はstub。init成功後だけgenerationつき内部依存export表へ切替える。一般KAPIの利用可否は別のservices_publicフラグで閉じ、SQLite/FEPはSERVICES_READYで同時に開く。
- `module_abort(id)` は逆順cleanupと参照検査を行う。戻せた失敗だけownerをreclaim/retireする。

登録状態はmodule recordの固定容量配列に記録する `{owner, generation, state, backing, dependencies, irq_refs, tick_refs, callback_refs, dma_refs}`。配列満杯は副作用前に失敗。STARTING中のIRQ handlerは登録時点で有効なデータを持つ。brokerはRUNNING以外を全部無視する設計にはしない (initでIRQ完了を待つ装置がある)。IRQ・tick登録はownerとhandlerのtext所属を検証し、外部callbackもownerに結び付ける。

失敗手順は新規呼出しをstubへ戻す → 装置の新規転送を止める → callback/IRQ/tickを解除・進行中呼出しゼロ確認 → DMA停止確認 → RAM回収。IRQ unregisterだけで停止成功とはしない。永久DEVICE予約と必要な復帰コード/写像は回収対象から除く。停止不能なら関連コード・データ・DMAを全て隔離し、省略stubへ進んだだけで「回収済み」と記録しない。boot-only依存を逆順に畳み、RUNNINGの依存先を先に解放しない。

## 3. T4 SQLite と FEP 案 A

### 3-1. エンジン境界・資源

`kapi_db.c` / `ime_dict.c` / boot selftest / VFS初期化の全outer SQLite呼出しを型つきexport facadeへ移す。stubは `db_open` 等の失敗可能APIではNOSYS、bool/使用量等は各既存APIの無効値を返し、last_errorに未搭載理由を保持する。成功ハンドルを作らない。関数ごとのstub表をコード生成入力に置き、NULL関数ポインタへ飛ばさない。

SQLite module initは module code → owner付きMEMSYS5 512KiB → 代替stack128KiB → SQLite/VFS登録 → 内部export公開、で終える。settings読込/FEP予約評価とFEP initはその後のboot transactionで行う。stack pointerはCPL=0内部の可変値。**boot selftest・kapi_db・ime_dict・WM/gshellのFEP呼出しを含む全outer SQLite facadeを代替stackへ切替える**。引数はkernel staging/固定call frameへ揃え、元ESPとcallee-saved registerを保存・復帰する。engine_enterを先に行い再入はstack切替前に拒否、正規VFS callbackはそのstackを継続使用する。I/Oを伴うfacadeは入口IF=0なら副作用前に失敗し、勝手にstiしない。IRQ handlerは呼べない。例外判定はstack番地に依存せずcoreのengine_activeを見るので代替stack上でもfail-stop、元stackへ通常return/cleanupを行わない。終了時は進入深さゼロを要求する。init失敗では逆順に戻す。エンジン状態を破壊した例外からはcleanupを試みない。

**B4:** core側の `engine_enter/leave` を全outer呼出しへ置く。active owner/groupと呼出し深さはFD所有権とは別。正規VFS callbackはactiveを維持し二重enterしない。再入は失敗、例外時は `isr_handlers.c` のapp-kill判定より先にactiveを検査しfail-stopする。STOPは要求だけ保存し、leave後の安全点で処理する。診断API (`db_mem_used`/last_error) も例外ではない。

**B3:** T2 bounded copyでpath/SQLをkernelへ完了してから旧stmt finalize。SQLは既存1024Bバッファ (payload1023B) にNULまで入らなければ失敗し、切捨てない。不正入力時はSQLite進入0、既存stmt・transaction・slot・SHM結果を維持する基準契約 (`docs/tasks/settings/FEP_BOUNDARY.md:140,141,213`) を変えない。copyoutはengineを出てから行う。対象は旧 `db_exec/db_prepare` を含む全outer入力。ただし旧stmt維持には「拒否のrcを見落としてstepすると旧SQLを実行する」反例がある (`kapi/kapi_db.c:651,704`)。`db_prepare_only` の入口finalize契約 (`docs/KAPI_SPEC.md:1396`) との調整も含め、3入口の公開接続は§9の判断前に行わない。T4b-1はcopy核・反例fixtureだけ先行でき、旧入口なら争点を回避できるとはしない。

### 3-2. F2/F3 と重複接続の移行規則

[F2_OWNERSHIP](../settings/F2_OWNERSHIP.md) のgroup/leaseを唯一のFD所有モデルにする。`os32_sqlite_group_open` を全openへ適用し、default VFSの所属無しopenはfail-closed。RESIDENT groupは **2slot、1group=1接続=1回open**。bootではsettings/selftestが1slotを順次acquire→open→close→finishして返し、両slotがFREEになってからFEPを開始する。DICT_META前はFEPが1slot、後は辞書RO+学習RWの2slotを同時に使う。settingsはFEPのslotを借りたまま保持しない。settings close失敗ならそのslotをQUARANTINEDにし、残slotで標準起動を強行せずboot transactionを失敗させる。後発journalも同group。close/finalize失敗はquarantineへ残し、同じownerを正常retireしない。`exec_cleanup_owned_resources` はアプリgroupだけを処理しFEP/親groupに触れない。

F3a/c: xTruncate/xSyncのI/O rcを `SQLITE_IOERR_TRUNCATE` / `SQLITE_IOERR_FSYNC` へ翻訳、xDeleteは未存在と実I/O失敗を区別し失敗を成功にしない。xAccessは有無/読取/書込可否とI/O失敗を分ける。xOpenのpOutFlagsを実open結果から返し、RO接続のwriteはVFS側でも拒否する。SQLite lock表は作らない (D29、`docs/tasks/v3/TASK_MEMMAP_V3.md:45`)。

**F3b前の暫定重複禁止案:** 新しいlock表は持たず、既存live DB/groupレコードを走査して同じファイルの2本目の接続をmode/ownerによらず拒否する。parked・親子・RESIDENT・close失敗quarantineを含む。更新可能なFEP DBへのアプリ直openも拒否し、学習はFEP facadeからだけ行う。不変CDのREADONLY例外ではFEPとアプリの同じDBへのread-only重複openを許す (pathによる特例拒否は設けない)。`:memory:` は別接続ごとの実体なので対象外。判定は文字列pathでなく **MAIN_DBのxOpenで取得したFD** のidentity `(mount_generation, fs_file_id)`。CREATEで初めて作るファイルも、破壊的truncateを伴わないopen後にinodeを取得→既存group走査→groupへ登録、の順にする。重複なら新FDだけcloseしてSQLiteへopen失敗を返し、既存接続とファイル内容を変えない。相対/絶対/別名も同一とみなす。identity取得→走査→group登録は通常文脈の非再入区間、失敗/close完了でのみ解除する。

現行で安定inodeが取れるのはext2なので、**更新可能なfile-backed SQLiteはext2に限定**する。別に **不変の配布CD/iso9660上のDBをREADONLYだけ許す**。後者はguest側のRO mountだけで判定せず、boot policyが指定した不変配布媒体であること、媒体generationがopenからcloseまで不変、配布DBがclean close済みでjournal/WAL無し、を条件とする。媒体交換/読込不能はIOERRで失敗し、旧接続のまま新媒体を読まない。イメージを使う検証でも実行中にホストから媒体ファイルを書換えない。不変ROでは重複readerを許してよいが、CREATE/RW/journal/write/truncateはVFSでも拒否する。この接続のSHARED lock/unlockは不変媒体generationが有効なら成功するno-opとし、書込み用lock昇格は拒否する。xAccessは要求modeに応じて実際の有無/読取可否を書き、I/O失敗はエラーを返す (全て存在/成功にしない)。hot journal/WALが存在すれば不変配布DBの条件違反としてopenを拒否し、回復書込みを試みない。

**HostDrvはguest側ROでも外部writerがいるのでこの例外に含めない。** FAT/HostDrvの直接MAIN_DB openはFS能力判定でファイル作成前に非対応を返す。`:memory:` は維持する。root媒体とDB置場の接続、CDのFEPとHostDrv標準構成は§3-5に定める。影響を受ける `db_*`、libos32cfgの任意path、db_test、installerはこの可否をエラーとして扱う。unsupported backendへpath比較の弱い代替を設けない。LAN版/hostも同じ外部更新可能なFSとして扱い、パス名やtransportだけでext2/不変CDの能力を与えない。

これは排他openの汎用サービスの前倒しではなく、T4のSQLite入口で危険な重複を断る暫定制約。必要なVFS identityが現行で不足する場合はT4bで内部APIとして足す。既存ユーザーの同一DB複数open依存を調査し、必要なら接続共有へ移す。F2 T01は親/子/同ownerに**別のDBファイル**を使って所有回収を試験し、同じファイルのケースは2本目拒否の別試験へ。T12はFEP初回openをbootで済ませ、子実行中に初めて学習journalを開くケースへ読み替える。意味変更を隠してpath比較だけで合格にしない。

### 3-3. FEP予算と設定の循環

D27の「512KiBの単一MEMSYS5」は維持する。`cache_size`やsoft limitだけを予約保証と呼ばない。起動はSQLite既定値 → settingsを短命RESIDENT接続で読みclose → 検査済み `mem_reserve_kb` を決定 → FEP辞書/stmtの優先初期化 → アプリopen許可。DICT_METAまでは辞書metaを読まない。settingsが読めなければ既定値で続行し、FEPのinitが成立しなければD26のMINIMALへ。

実装案は既存MEMSYS5の外側に**割当許可の計数ラッパ**を置くこと。`SQLITE_CONFIG_HEAP`でMEMSYS5を設定 → `SQLITE_CONFIG_GETMALLOC`でそのallocatorを保存 → `SQLITE_CONFIG_MALLOC`でラッパ登録 → initialize、の順にする (`sqlite3.c` のHEAP処理はallocator表自体を置換する)。別ヒープは作らない。engineの呼出し文脈からFEP/APP/SYSTEMを識別し、app割当は将来FEPに残す量を割り込むとNULL。free/reallocはallocation側のタグ/実サイズで戻す (free実行時ownerを使わない)。共有engineメモリはSYSTEM勘定とし、取り分計算へ含める。ラッパのメタデータ分も512KiBの予算内で数える。

ただし総空きだけではMEMSYS5の断片化を保証できない。T4aの最小prototypeで、使用SQLite版のallocator hooks、丸め・realloc失敗時旧ブロック維持、FEP検索/学習の最大一時割当、appのsort/prepare連打後の1語変換を検証する。保証が成立しないなら予約量/アプリ上限を調整して再検証し、別ヒープや512KiB増量は本票で承認済みにしない。タグはpayload前置きでMEMSYS5の2冪丸めを増やす案だけで決めず、固定容量の外部タグ表+元allocatorのxSize利用と比較する。外部表のbyte数も512KiB予算に含める (heapへ渡す部分と表の和が512KiB)、表満杯なら割当を副作用前に拒否。S/M/Lの **最大負荷辞書** と全切替経路で同じ予約値を検証する。DICT_META前は個別metaが無いためS/M/L全部に通る共通予約量を既定とし、未検証の任意辞書へのlive切替は拒否する。settingsで量を変えるときは再起動し再評価する。既定予約量の数値はこの測定ゲートで決める。

辞書切替が起動時予約を超える場合は設定だけ保存し再起動要求、現在の辞書を保つ。アプリopen上限、割当拒否、FEP使用量を診断し、FEPを任意省略してメモリ試験を通さない。

### 3-4. 必須サービスを一括公開するboot transaction

`SQLITE_INTERNAL_READY → SETTINGS_CLOSED → FEP_INTERNAL_READY → SERVICES_READY`。途中は一般のdb/ime KAPIがstubのまま、信頼するboot coordinatorとFEPだけが内部SQLite exportを使う。settingsはengine_enter経由、予約評価もこの段で行う。FEP正常起動とgroup/予約確認が揃って初めてservices_publicを一度に開く。最初のユーザーASはこの後なので、半公開をアプリが見ることはない。T4の案AではFEP固定コードはcoreに残り、回収量0の対象はその動的資源とSQLite module。T5aの案B以後はFEP bodyも同じrollback対象に加わる (上位票 `docs/tasks/v3/TASK_MEMMAP_V3.md:442`)。

FEP画像CRC不一致・辞書なし・予約不足等は利用者へ未公開の失敗として、FEPの部分stmt/groupをclose/finalize → settingsを含むRESIDENT groupゼロを確認 → sqlite3_shutdown → 内部exportをstubへ → RUNNING/STARTINGからROLLING_BACK → FEP/SQLite ownerの回収、の順。init自身で失敗した場合も同じ段階印で一度だけ処理する。正常rollback後はSQLite/FEPの占有ゼロ、全dbはNOSYSのMINIMALへ入る。close/shutdown/装置停止の証明が失敗したらcode/heap/stack/groupを依存閉包ごとQUARANTINEDに残す。一般KAPIはstubだが **MINIMAL_QUARANTINED** と別状態で診断し、隔離byte数を空きから差し引く。これを標準MINIMALや8MB標準合格に数えない。安全な入力/回復継続を証明できなければ起動停止する。

### 3-5. CD/HostDrvルートでもFEPを必須に保つ

これは新しい実装契約案で、現行 `ime_dict_open` がREADWRITE固定であることを変更する。D21のFEP必須とD26の実失敗rollbackは維持し、rootのFS種別だけでMINIMALを選ばない。

- **CD標準構成:** 不変配布CDの既存combined `fep.db` をROで開いて変換を提供する。`ime_dict_open(mode)` にROを加え、CREATE TABLE/INDEXと学習stmtのprepareを行わず、`dict`と存在する場合の`dict_user`を読む検索stmtだけ作る。`dict_user`欠落なら辞書本体だけを検索する。学習は無操作、明示的な削除/clear等の更新APIはROFS、list/exportは読取として維持する。状態診断で「変換可・学習不可」を示す。これはFEP省略ではなく、DICT_METAの学習別file化も前倒ししない。settingsも不変CDをROで読み、更新はROFS。ext2へ学習を保存したい利用者は下の管理DB構成を選ぶ。
- **HostDrvルートの標準構成:** /sys等をHostDrvから読む一方、**管理下のext2データvolumeを別mountし、FEP/設定の既定DBはそちらを使うことを必須**にする。mount先案は `/persist`、辞書はその `db/fep.db`、設定は `etc/settings.db`。実際の名前はパス定数の正典へ登録し、HostDrv用配布profileでkernel FEP・boot settings・libos32cfg/installerの既定pathを一組に生成する。複数alias mountを作らず、同じvolumeは一つのmount identityで管理する。kernel boot coordinatorはroot成立後・SQLite内部openより前にext2 mountと期待volume/DBの存在を確認する。FEPはこのext2上のcombined DBをRWで開き、既存学習を保持する。
- **配備/不在時:** HostDrv上のlive DBをそのまま読むfallbackはない。初回のext2へのDB準備はユーザーデータを保った配布手順で行い、通常bootが既存学習を上書きしない。初回コピーでは元DBをclean closeし、コピー完了までホスト側の更新を止める。必要なら一時ファイルへコピーし、完了後に元DBの確定時に採った長さ・CRCと照合してから採用する。不一致なら採用せず再コピーする。通常bootで継続的なdigest検証は追加しない。管理volume/辞書が無い場合は設定不備という実失敗としてD26のrollbackを行い、その起動をHostDrv標準受入の合格に数えない。

CDのRO FEPはT4c-3、HostDrv root+ext2管理DBはT5b-6の受入に分ける。現行rootはFD/FATまたはHDD/ext2 (`kernel/kernel.c:444,449`) なので、T4c-3では不変CDをrootとするhost fixtureで1語変換・学習journal生成ゼロ・更新ROFSを確認し、CD実起動はT5b-5で受ける。T5b-6では変換→学習→再起動後保持、HostDrv源の外部変更が開いた管理DBへ影響しないこと、管理volume不在の明示的失敗を確認する。HostDrvの配布profile/mount接続・媒体配置の判断は **T5b-6実装前** のゲートで、T4cやT5b全体の入口へ前倒ししない (`docs/tasks/v3/DOCS_REORG_T3.md:104`)。実装済みともユーザー承認済みの新データ配置とも記録せず、実際の媒体を変更する際は既存配備の [D1]〜[D3] に従う。

## 4. T5a FEP 案 B

T4でfacade化したFEPを `fep.mod` へ移す。SQLite RUNNINGの依存を必須とし、未公開init中に辞書group/stmt/入力状態を全て準備してからexportを一括公開する。core側stubの意味は上位票 §5-6 (`docs/tasks/v3/TASK_MEMMAP_V3.md:444`) のAPI別表をそのままテストoracleとする。is_active/get_modeは0、set_mode/toggle/set_render/switch_dictは無操作、feed_key(k)はk≥0でk・負なら0、getkey/getcharは生のblocking入力、trygetkeyはkbd_trygetkeyの生キー (無入力だけ−1)、trygetcharはその文字、ime_user_*はNOSYS。MINIMALの英数編集を非ブロッキング/ブロッキング両経路で確認する。

B2 facadeは `ime_user_list/delete/export` の全入力を先にbounded copy → kernel staging → bind/step → stmt finalize/resetとbinding寿命終了 → engine_leave → copyout、の順。listは上限件数/文字列長で打ち切り状態を返す。deleteの不正kanjiをNULL条件に変換しない。exportはpath検証完了前にcreate/truncateしない。上限は [FEP_BOUNDARY §3](../settings/FEP_BOUNDARY.md) を引継ぎ、失敗時に返却バッファを部分公開しない。具体値はprefix/yomi/kanji/path各256B (NUL込み)、list max=1〜64、staging=64×68=4,352Bを専用領域に置く。NULL prefixは全件、NULL/正常な空kanjiは同一yomi全候補削除、不正kanjiは失敗。上限±1とページ末尾NUL、失敗後busy解除を試す (`docs/tasks/settings/FEP_BOUNDARY.md:45`)。

`dict_fd_protect` を廃止しRESIDENT groupへ集約する。close失敗は記録してreopenを拒否する。SQLite→FEPの依存順を守り、FEP init失敗で標準起動を成功扱いにしない。MINIMALへ落とす際も停止確認済みのgroupだけ解放する。

## 5. T5b 早期ロードと同梱

### 5-1. ブート段階

`BOOTINFO_SAVED → MEMORY_READY → EARLY_MODULES_READY → ROOT_READY → SERVICES_READY → USER_READY`。

1. bootinfo/BDAを保存、RAM検出、必要なcoreコンソール/例外/heapを準備。
2. PG/台帳をルートマウント前へ移し、集積域・同梱域を**最初の物理割当より前に**予約。DMA初期化もroot driverより前へ。
3. 同梱EARLYモジュールを検証・in-place再配置・起動しFS/装置を登録。importはcoreと既にRUNNINGのEARLY依存だけ。VFSファイル読込やSQLite/settingsを使わない。
4. `/` マウント。ここから一覧を読める。短いsystem.cfgの読込、gfx識別→永久予約→写像→BB確保をlive AS=0で済ませる。GUI probe/enableは後。
5. exec/shlibの準備 → SQLite→FEP → その他の通常モジュール。最初のAS以前に共通PDEの追加を終える (上位票 `docs/tasks/v3/TASK_MEMMAP_V3.md:337` の順を明記)。kdebugはroot/台帳/DMAの成立後、呼ぶ自己試験より前に読む。OFFでもkauditは残す。
6. スプラッシュアプリを実行・回収してからrshell/SerialFS対話とshellへ。gfxを使う場合は通常のGUI境界を経由する。

U19の移動可否はT5b入口でcore初期化の全依存を確認するゲート。`kernel/kernel.c:375,453,524,575,589,590` のFS登録・root I/O・PG/台帳/DMA・kdebugを依存表にし、PG前のBDA読取、console/IRQ/heap、root driverのDMA要求と初回物理割当を追う。移動不能と判明した場合のみ、上位票の代替「同梱固定予約」を具体化して再レビューし、未完成のpgallocへ逃げない。

### 5-2. 媒体別閉包と所有権

| 媒体 | 同梱依存の閉包 | 失敗 |
|---|---|---|
| HDD/ext2 | coreのIDE/ext2でroot、core1エントリ・同梱不要 | root未成立なら回復媒体へ。通常モジュール探索で循環させない |
| FD | core FDC + FAT EARLY、合計2エントリ | FAT失敗なら起動停止。SQLite/FEPなしMINIMALはroot成立後の回復構成 |
| CD | core + ATAPI EARLY + iso9660 EARLY =3、HostDrv併用でも追加1で計4。iso9660側のdiskio等を依存画像へ静的に含める | ATAPI未起動でiso9660登録しない |
| HostDrv | core + HostDrv EARLY =2。必要なnp2sys transportは同画像に含める。ext2管理DBの接続は§3-5 | 実機で専用I/Oへ触れない。rootとして使う構成ではEARLY依存を同梱 |

同梱域は1MiBの予約をまずboot ownerで保持し、各エントリを **VK32 entry.mem_size = load_span (prefix+body)** で配置する。BSS・整列を含め重複なし、同じページを2ownerに分けない。検証→再配置→initの順にmodule ownerへ占有ページを移譲し、**全エントリの処理と参照終了後に、余白ページだけ**返す。EARLYでも§2-1の継続情報コピーが済んだprefixページだけは余白として返せる。成功module/隔離moduleのbodyページは返さない。圧縮集積域は全展開・CRCとbootinfoへの結果コピーが終わった後に返す。

VK32生成器の実物は `tools/mkvmkernel.py` (`build/kernel.mk:210`)。上位票でいう `mkvk32` と同じ担当として、複数エントリと `raw_size/mem_size` を区別する形式へ更新する。C側HDDとASM側FDの検査は同じ壊れた画像fixtureで一致させる。許可destination種別をcore/bundleに限り、画像が宣言した任意住所へ書かない。旧形式は入口前に拒否する。

### 5-3. bootinfoとMINIMAL回復の境界

`include/bootinfo.h` の次形式に固定上限VK32_MAX_ENTRIESのentry配列を追加する。各wire entryは `{kind, module_id, container_phys, raw_size, load_span, payload_offset, body_mem_size, decoded_crc}` (各u32)、headerに `{wire_size, entry_count, layout_gen, image_crc, table_crc}` を持つ。既存drive/image情報64Bを保持し、追加header20B+entry32B×4で **212B**。MEM_BOOTINFO_SIZEは256Bなので `VK32_MAX_ENTRIES=4` を維持し、形式上の上限も `VK32_MAX_ENTRIES<=5` とASSERTする。形式versionは更新する。予約領域 `MEM_BOOTINFO_SIZE` 内へ収まるASSERT、C/ASM/Pythonのoffset照合を必須とし、収まらなければ黙って低位の隣へ伸ばさず新形式を再設計する。

loaderは全展開/CRC完了後にこの表を書き、最後に有効印をcommit。kernel最初のbootinfo captureが固定core配列へコピーし、count/CRC/世代・許可kind・containerのページ整列・範囲・非重複・`load_span=payload_offset+body_mem_size`を再検査する。bundle entryはmodule自身のヘッダ値とも照合してからEARLYを開始する。core entryはmoduleヘッダを持たないためmodule_id=0・payload_offset=0とし、body_mem_size=ページ切上げしたcore占有 (BSS込み) をリンク情報から生成してload_spanと照合する。未知/欠損情報は推測で復元せず停止する。台帳予約はコピーした値だけを使い、集積域を返した後にloader内ポインタを参照しない。

MINIMALでSQLiteを使う `install --recover-settings` は動作しない (D21)。この制限をエラー文言と回復手順へ明示し、NOSYSを「DB破損」や「復旧済み」に変換しない。MINIMALは媒体/ファイルの退避・正しいシステム一式の復元を担い、DB修復はSQLite/FEPが正常な標準構成またはホスト側の既存手順で行う。MINIMALでSQL修復まで必要という要求はD21の変更判断へ送り、本票でSQLiteを密かに読み込まない。

## 6. T5c 外へ出す順序と T6b

| 順 | モジュール候補 | coreに残す接続 / 特別な受入 |
|---|---|---|
| 1 | KCG ROM | コンソールからのtyped facade、未搭載診断。T7a前は旧font経路との排他 |
| 2 | FM+snd、PCM | IRQ/tick/DMA broker。登録直後失敗・停止不能を必ず注入 |
| 3 | V86 | entry/exit facade。低位PTE切替と例外の復帰コードの寿命を証明、稼働中回収なし |
| 4 | LAN/82557 | core PCI識別/永久resource、driver bindのSTARTING/RUNNING/QUARANTINEとmodule状態を対応。LB-3/4受入後の実装を移す (現在は送信まで)。LAN版/hostのNIC参照・session寿命も確認 |
| 5 | HostDrv / np2sysp+シームレスmouse / Cirrus+WAB | 検出前の専用I/Oを禁止。HostDrvはroot用EARLYなら同IDをPOST_ROOTで再ロードしない。Cirrusの識別glueだけcoreに残す |
| 6 | スプラッシュ | CPL=3アプリ、BB lease借用→終了解除。表示失敗で常駐shellを止めない |

gfxの副作用のない識別・resource記録はcoreに残す案とする。R7FIX2のPEGC別名窓FFF00000hの予約/写像 (`include/memmap.h:395`、`gfx/gfx_core.c:381`) はmodule失敗でも消さない。LANの装置手順は `docs/tasks/realhw/TASK_LAN_82557.md:51` のIntel/National資料とFreeBSD BSD実装を引継ぎ、新手順は[HW3]の根拠を添えてから実装する。同票:40の旧Linux参照を写し元にはしない。Trident enable/probe、合成器、追加NICはP4/P5の票でこのloaderへ載せる。USBをここで追加しない。起動一覧はmodule ID/phase/必須性/依存/許可パスを持つ配布manifestと照合し、任意ユーザーパスからのCPL=0ロード口を公開しない。必須集合と許可ID/phaseはcoreの生成policyで固定し、配布manifestはそのpolicyの任意モジュールの選択/期待hashを指定するだけ。root FSの閉包はboot媒体種別から決まり、manifestで省略不可。SQLite/FEPはMINIMALを除き必須で、manifestからの削除で省略扱いにしない。manifest欠落/破損/循環/重複/未知IDはユーザー未公開のboot失敗とし、root成立後は§3-4のrollbackを経てMINIMALへ、root閉包自体の失敗は停止する。

T6bは最後に集積を上位票の `[0x500000,0x600000)` へ移し上限1MiBにする。loader自身、stack、圧縮源、core展開先、同梱のmem_size占有、bootinfoを区間表へ載せ、**全宛先を検証するまで1バイトも展開しない**。source/destinationの重複も検査する。FDのreal-mode読込からPMコピーへの境界でsegment wrap/64KiB分割を監査する。単なるMAX_IMAGE_SIZE置換では閉じない。

8MBのピークは各段の同時生存区間の和で証明する。圧縮画像1MiBと同梱1MiBを持つ間にBB/SQLiteを先取りしない。展開完了後に集積を返し、同梱owner移譲・余白返却後にPOST_ROOTを開始する。終了時の空きだけを測ってピーク合格にしない。

## 7. 実装単位と試験台帳

### 7-1. 半日〜1日の受入単位

全35行は独立に差分を検査・受入できる0.5〜1日 [推] の単位で、丸数字が観測する条件 (最大8項、10項以下)。T4b-4a/4bはT4b-4へ、T4b-5/6はT4b-5へ統合し、残る番号は引継ぎ参照のため維持する。同一段内でも先行行の成果物を使う。未接続の核はhost試験で単独受入し、公開切替はT4c/T5aの統合ゲートを通す。工数超過/10項超なら依頼前にさらに割る。末尾の争点は該当行の実装前ゲートであり、他行の準備を止める理由にしない。

| 小段 (順) | 変更するもの・独立成果 | 観測できる受入条件 / 対応する変異 |
|---|---|---|
| T4a-1 (0.5日) | module_imageとmkmodの共通schema/ID別policy | ①既存kdebug正常像が通る ②旧ops/未知版/ID混同/表重複をinit前拒否 ③検証前後で入力・out不変 (成功時outだけ更新)。版/範囲検査削除変異は失敗 |
| T4a-2 (1日) | VA0 SQLite prototype・import生成・reloc量測定 | ①未解決/外部data/不支持relocを生成拒否 ②非整列正常relocは成功、末尾−2/BSS/重複/import交差は書込0 ③base加算overflow拒否 ④実画像/body/prefix/件数と上限を確定。境界緩和変異を検出 |
| T4a-3 (1日) | MEMSYS5 hook/タグ方式のhost prototype | ①HEAP→GETMALLOC→MALLOCの実allocatorを通る ②realloc失敗で旧block内容/勘定不変 ③別文脈freeと表満杯で漏れ0 ④heap+タグ総量≤512KiB。タグ/丸め勘定削除変異を検出 |
| T4a-4 (1日) | 最大負荷S/M/Lの取り分測定fixture | ①app sort/prepareで上限到達後も各辞書1語変換/学習が通る ②全辞書切替で最大一時量を記録 ③予約超えは旧辞書保持＋再起動要求 ④共通予約・APP上限・最大連続要求を数値化。APP割当門削除で対照が崩れる |
| T4a-5 (1日) | module record・load/start/abort共通核 | ①全確保点失敗でowner/page/FDが開始前へ ②init完了前はexport未公開 ③prefix返却後もexport/dependencyが有効 ④満杯/二重abortで二重freeなし ⑤未停止参照は隔離。先行公開/無条件free変異を検出 |
| T4b-1 (0.5日) | B3のcopy核・3入口の反例fixture (公開接続は§9判断後) | ①1023B+NUL成功、1024B+NUL/未終端/guard跨ぎ失敗 ②copy拒否でengine進入0・ファイル不変 ③出力はleave後。finalize先行/切捨て変異を検出。stmt/slot/SHMの期待値は§9で選んだ契約に固定し、3入口の拒否→stepと資源回収を対照試験する。核だけの合格を公開接続の受入にしない |
| T4b-2 (1日) | engine guard・sqlite_stack.asm・例外/STOP接続 | ①全outerのESPが128KiB stack内で戻りregister/IF一致 ②再入/IRQ/IF=0 I/Oは副作用前拒否 ③engine中faultはfail-stop、app-kill/cleanup0 ④STOPはleave後に1回 ⑤通常VFS callbackのactive連続。guard迂回/早期STOP変異を検出 |
| T4b-3 (1日) | F2 group/lease・KAPI/IME/boot利用者 | ①RESIDENT2slotをsettings返却→FEPで再利用 ②所属無しopen拒否 ③遅延journalは同group ④別DB親子/同ownerのT01/T02/T07/T12で子だけ回収 ⑤close失敗は隔離・再利用なし。現ownerへの誤帰属変異を検出 |
| T4b-4 (1日) | VFS FD identity・MAIN_DB重複門・F3a/c | ①相対/絶対/別名の同一ext2 inodeを拒否 ②parked/親子/同owner/隔離済みも拒否 ③CREATEは新FDのみcloseし既存内容不変 ④別DB/:memory:成功 ⑤truncate/sync/delete/access失敗を各SQLite rcへ伝播 ⑥pOutFlagsは実open結果 ⑦RO write/truncate拒否 ⑧FAT/HostDrv/LAN版host MAIN_DBは作成前拒否。path比較置換/登録早期解除/成功固定/RO門削除変異を検出 |
| T4b-4c (1日) | 不変CDのRO VFS経路 | ①FEP＋appのRO重複とSHARED lock成功 ②RW/CREATE/lock昇格を拒否 ③xAccess実有無/失敗が一致 ④hot journal/WALでopen拒否・write0 ⑤媒体generation変化はIOERR。ROだけで不変扱いする変異を検出 |
| T4b-5 (1日) | X-11全12口の契約表・共有サービス回帰 | ①12口の主体・入力・返値・状態変化・出力寿命・取消を表に明記 (IME9口は入力/モード/辞書管理に分類) ②CUI/WM通常配送で入力1回消費 ③parent/child/park-resumeで候補・辞書・出力の寿命が表と一致 ④無効mode/variantで状態不変 ⑤exec結果の「全体の直前1件」を親子切替で観測 ⑥gui_callの実owner/内部op拒否・正規WM配送を観測 ⑦con_sinkの先着/別reader EXIST/終了時返却を観測。二重配送/無効値書込/owner差替/reader早期解放変異を検出 |
| T4b-7 (1日) | E11-A2b raw KAPIゲストfixture | ①kbd/IME/serial無入力待ち・FM長音の各CUI/GUIでSTOP→shell/WM復帰 ②通常完了も成功 ③内部ログ/SQLite中は中断0 ④owner回収1回・Key-OFF・候補取消・池復帰。取消削除/内部ログに取消混入の変異を検出 |
| T4b-8 (1日) | X-1残る文字列・kprintfの棚卸し/接続 | ①全公開NUL入力/可変引数の担当が確定 ②format/%s/引数窓の端をB1で処理 ③ページ末尾NUL成功、次ページ欠落/未終端は副作用前失敗 ④通常表示同一。生ポインタ読取へ戻す変異を検出。量が多ければAPI群別に分割 |
| T4c-1 (1日) | SQLite module build・typed facade・全stub | ①旧固定sqlite section/addr参照が消える ②全db API/診断がstubかtyped exportへ ③通常db_test/db_mem_used成功 ④旧新混在はinit0で拒否。直呼び残存/NULL stub変異を検出 |
| T4c-2 (1日) | services_public・settings→FEP transaction | ①公開前db/imeはstub、全成功で同時公開 ②各init確保/設定close/FEP CRC/辞書欠落/予約不足を注入 ③戻せた失敗はSQLite owner占有0・MINIMAL ④close/shutdown失敗は隔離byteとMINIMAL_QUARANTINEDを診断。早期公開/隔離free変異を検出 |
| T4c-3 (1日) | CD RO FEP・標準構成の回帰 | ①CD rootのhost fixtureで1語変換 (実起動はT5b-5) ②dict_user有無とも読取成功 ③学習journal0/更新ROFS/list・export読取維持 ④app圧迫下FEP成功 ⑤ext2標準RW学習も保持。ROでCREATE/学習prepareする変異を検出 |
| T5a-1 (1日) | FEP B2・staging/出力境界 | ①各256B/64件境界・NULL契約一致 ②不正入力engine進入0/output不変 ③finalize/reset/binding終了→leave→copyout順 ④late出力拒否後もbusy解除 ⑤export不正pathでcreate/truncate0。寿命逆転/失敗kanjiをNULL化する変異を検出 |
| T5a-2 (1日) | fep.modへの分離・依存・stub | ①FEPのcore直リンクなし ②SQLite→FEP init、逆順rollback ③RESIDENTと学習journalを子終了で失わない ④close失敗でreopen拒否 ⑤MINIMALのGUI textbox/CUI editで英数編集 (生キーあり/無入力の両方)。stub常時−1/依存先先free変異を検出 |
| T5b-1 (1日) | VK32/bootinfo生成・C/ASM共通wire | ①4entry/212Bを同じoffsetでdecode ②entry_count5/旧形式/不一致CRC拒否、最大定数6はASSERT失敗 ③同梱mem_size=load_spanでBSS/prefix不重複 ④EARLY生成にpair CRC自己参照なし。rawだけの占有検査へ戻す変異を検出 |
| T5b-2 (1日) | U19初期化依存・PG/台帳/DMAを前へ | ①初回物理割当前にbundle全域予約 ②root driver前にDMA成立 ③HDD root成功 ④kdebug ON/OFFとkaudit回数維持 ⑤GUI=0で識別/予約まで、probe0。予約遅延/起動順逆転変異を検出 |
| T5b-3 (1日) | EARLY所有移譲・余白返却 | ①container非整列/BSS reloc/隣接破損をinit0で拒否 ②未マウントVFS/POST_ROOT import禁止 ③bootinfo capture後だけ旧領域返却 ④同一ページ二重owner/二重移譲0 ⑤全entry参照終了後に余白だけ返す。早期返却変異を検出 |
| T5b-4 (1日) | FAT切出し・FD起動 | ①core+FATの2entryでroot/shell成功 ②FAT失敗は停止 ③MINIMAL全db NOSYS・英数編集成功 ④install --recover-settingsは復旧済みと誤表示しない。閉包欠落/偽成功変異を検出 |
| T5b-5 (1日) | ATAPI/iso9660切出し・CD起動 | ①3entry (HostDrv併用4) でroot成立 ②ATAPI失敗でiso登録0 ③BSS込み占有とDMA停止/返却一致 ④CDのRO FEP回帰。依存順逆転/停止未確認free変異を検出 |
| T5b-6 (1日) | HostDrv EARLY・管理ext2配布profile | ①配置判断後にroot+管理DBで変換→学習→再起動保持 ②外部HostDrv源変更は開いたDBへ影響0 ③管理volume不在は明示rollback ④実機専用I/O0・同ID二重load0 ⑤初回コピーの長さ/CRC不一致で不採用。管理DBfallback/照合省略変異を検出 |
| T5c-0 (1日) | IRQ/tick/callback/DMA owner broker | ①登録直後失敗でも参照0を証明して回収 ②STARTINGの必要なIRQは有効 ③tick残存/DMA停止不能は依存閉包隔離 ④DEVICE予約不変。unregisterだけでfreeする変異を検出 |
| T5c-1 (0.5日) | KCG ROM移設 | ①typed facadeで同じ文字 ②欠落stub診断 ③旧font経路と排他 ④キャッシュ/owner予算一致。誤export/二重初期化変異を検出 |
| T5c-2a (1日) | FM+snd移設 | ①通常再生/STOP成功 ②tick登録後失敗で呼出先残存0 ③Key-OFF確認後回収、不能時隔離。tick解除欠落変異を検出 |
| T5c-2b (1日) | PCM移設 | ①再生/停止成功 ②IRQ登録後失敗とDMA停止不能を区別 ③64KiB境界内・停止不能リング保持 ④永久予約不変。DMA証拠省略変異を検出 |
| T5c-3 (1日) | V86移設 | ①CUIのentry/exit往復 ②低位PTE復帰 ③例外復帰コードが回収対象にならない ④セッション中free0。復帰前owner retire変異を検出 |
| T5c-4 (1日) | LAN/82557移設・SFS接続 | ①LB受入済みTX/RX/IRQ回帰 ②PCI DECLINEなら参照0、QUARANTINEなら再bind/free0 ③LAN版/host通信中の取消/断線で有限復帰 ④session/NIC callbackを断つまで保持。IRQ/tick片方のみ解除変異を検出 |
| T5c-5a (0.5日) | HostDrv POST_ROOT・np2sysp/mouse | ①検出前専用I/O0 ②EARLY済み同ID再load0 ③通常mouse/host利用成功 ④検出失敗はstub。重複load/検出門削除変異を検出 |
| T5c-5b (1日) | Cirrus+WAB移設・core識別glue | ①GUI=0識別/予約、enable0 ②GUI境界でだけprobe/enable ③失敗でもPEGC等の永久予約維持 ④全共通PDEが最初のAS前に準備済み。識別/enable混合変異を検出 |
| T5c-6 (1日) | 起動policy/manifest・splashアプリ | ①欠落/循環/重複/未知IDは公開前失敗 ②必須削除でもSQLite/FEPを省略扱いにしない ③splash lease返却→rshell/SerialFS→shellの順 ④表示失敗でもshellへ。必須判定/lease返却欠落変異を検出 |
| T6b-1 (1日) | HDD/FD集積移動・全区間検査 | ①508KiB超〜1MiB成功/1MiB+1拒否 ②loader/stack/source/core/bundle/bootinfo重複で展開write0 ③FD64KiB分割/segment wrap境界が一致 ④C/ASM同fixture同理由。最終entry検査前展開変異を検出 |
| T6b-2 (0.5日) | 生存区間/ピーク計測・最終構成台本 | ①展開・root・SQLite/FEP公開・splash各時点でowner/PFN和一致 ②集積返却後にBB/SQLite、別staging0 ③64MB通常/故障対照 ④8MB planar/PEGC・17MB・Ra266は最終段の未実施欄へ引継ぐ。返却漏れは計数不一致になる |

### 7-2. 像・実行時RAMの見積もりと止める条件

| 範囲 | core像の増減見積もり (未実装、[推]) | core外・測定ゲート |
|---|---:|---|
| T4a 共通核/生成policy | +4〜8KiB (DBGREF分を二重計上しない) | prototypeでprefix/body/record容量を確定。kdebug ONは既存body36KiB・peak48KiB、OFFは0 |
| T4b facade/F2/F3/X群 | +4〜10KiB | group/identity/出力stagingを含むmap差分。X-1の棚卸しで上限が変わるなら着手前に再見積もり |
| T4c SQLite分離 | +1〜3KiBのcore接続、固定SQLite帯は別計上 | 現固定SQLite全占有770,560B (`kernel.map:4456,8010`)、うちstatic MEMSYS5 393,216Bを除く377,344Bがbody基準。ページ切上げ380,928B＋512KiB＋128KiB＝**1,036,288B定常 [推]**、peakはこれに実測prefixを加える |
| T5a FEP分離 | 旧FEP約18.5KiBを外へ、B2 staging等+5〜7KiB [推] | module本体/状態・ページ丸めを新mapで計上。旧群値は `docs/tasks/v3/TASK_MEMMAP_V3.md:322`、現実測の節約量ではない |
| T5b EARLY/bootinfo | 接続/検査+4〜8KiB、FS群を外へ移した差分を別記 | 同梱域1MiB内で全load_spanの和≤1MiB。212B wire＋保存配列もcore像へ。旧FS群38.1KiB (`docs/tasks/v3/TASK_MEMMAP_V3.md:324`) は参考 |
| T5c 各driver/manifest | broker/一覧+3〜6KiBから実際に除いた群を差引く | NIC・PCMのDMAは固定DMA pool、module RAMと二重計上しない。群別の新mapとprefix/常駐/隔離を各段で採る |
| T6b | kernelの追加は計数/glue≤1KiB [推]、loader増分は別枠 | 圧縮源≤1MiB、同梱≤1MiB、定常から消える時点を採取。8MBの上位概算余裕を現在の合格値に転用しない |

T4開始時にT3受入成果物の `__bss_end`、実定数、core text/data/bss、圧縮長、SLACK、free/max連続pagesを基準化する。各段は増減を同じ構成・同じフラグで比較し、既存像の残りを越す/SLACKの返却量を侵す/必要連続量を確保できない場合は後続へ進めず段を縮めて再計測する。coreの増分は§7-2各行の上端も暫定見積ゲートとし、超過時は理由・新mapを提示して枠を見直す。KHEAP/SHMを削る、FEPを省略する、MEMSYS5を増減することで帳尻を合わせない。上の見積もりは承認済み予算ではなく、正負の未実測値を相殺して合格にも使わない。
SQLiteのpool/stackはbodyから外してowner付き連続割当とし、BSSの旧384KiBを残して512KiBを追加する二重確保をmapで検出する。FEPの4,352B stagingとタグ表はそれぞれ置いた領域で1回だけ計上する。KDEBUG=1受入ではそのowner/bodyもピーク・定常へ加え、隔離は空きに含めない。共通ログはbuild ID (SHA)・構成・小段・成否/未実施を持ち、詳細はその段の判定に必要なものだけ採る。形式/ロード段は世代・画像CRC・phase/状態、メモリ段はowner/PFN・prefix/body/heap/stack・最大連続空き、engine段はIF/深さ・例外分類、driver段はcallback/DMA参照を記録する。失敗時は初回理由・残存/隔離量を該当範囲で残す。

> 2026-10-14 ユーザー決定: 搭載メモリ量ごとの確認は v3 の最終段にまとめる (DEFERRED_TESTS の関門「v3 の最終段」)。それまでのゲスト受入は 64MB の 1 構成。以下の構成一覧は最終段用として残す。

各段は `make check-changed` と必要なbuildを通し、ABI段は [ABI1]〜[ABI3] に従う。形式/loaderは既存 `check-module-image-host`・`check-kdebug-init-host`、wire/展開は `check-bootinfo-host`・`check-vk32-crc-host`、LANは `check-lanfs-host` を拡張する (各 `build/checks.d/` の同名mk)。正常対照がPASSしてから対応変異が観測でFAILすることを記録し、意図した生成拒否/ASSERT拒否の試験を除き、コンパイル失敗/timeoutを検出成功に数えない。DBGREF自己試験の受入はKDEBUG=1・pair CRC一致・RUNNINGが前提、OFFはopen0/owner0とkaudit継続を別に確認 (`docs/tasks/v3/TASK_DBGREF.md:104,110,112`)。外部apps/gameは現行の対象外決定を維持し、再開時の全再ビルドを引継ぎゲートへ残す。故障注入は別の使い捨て媒体で行い、本番辞書へ書かない。通常ゲスト8MB planar/PEGC・17MB・Ra266とFDの受入を上位票§7へ紐付け、MINIMAL落ちは標準成功とは別結果で記録する。ログは上記の共通項目と該当段の観測値に絞り、失敗・未実施構成を省略しない [V4]。r2の実測は§0の既存成果物の値を引き継ぎ、今回のr3ではmain基点・レビュー根拠・段参照・行数を文書照合した。製品build/host試験/ゲスト受入は実行していない。

## 8. 独立レビュー対応 (往復1)

レビュアー `claude-opus-5-5` (Opus 5.5)、2026-10-01、判定Request changes。実装検証ではなくコード/設計の照合。当時の修正後確認は下記の往復2・3で実施。この履歴はr2/r3の再照合版のレビュー結果ではない。

| 指摘 | 到達性の確認と対応 |
|---|---|
| P1-1 表/BSS/占有 | prefix後置の素直な画像はBSSで表を壊す。§2-1で表を前置き・ページ整列body・load_spanを規定、§5-2/5-3/7に同梱占有/bootinfo/fixture |
| P1-2 FEP失敗 | SQLite RUNNING後のFEP欠落に到達する。§3-4で内部/利用者の二段公開とshutdown rollback、隔離時は別予算/状態 |
| P2-1/2 DB identity/試験 | MAIN_DB xOpenのFD inodeでCREATE対応、当面ext2のみを明示。T01は別DB、T12は遅延journalへ |
| P2-3/4 group/設定順 | RESIDENT2slotをsettings短命→全FREE→FEPへ。close隔離時はtransaction失敗、内部export後に設定評価 |
| P2-5 stack/IF | 全outerを代替stackへ、再入/IF=0拒否、activeによる例外停止 |
| P2-6/7 画像/libgcc | prefix+body本体へ直接読込、別全画像scratchなし。必要libgccは明示static link |
| P2-8 予算 | タグ方式の丸めコスト比較、S/M/L最大負荷と共通予約、任意辞書は検証なしlive切替禁止 |
| P2-9/10/11 policy/回復/bootinfo | 必須集合はcore policy、manifest失敗経路、MINIMAL SQL修復不可の手順、wire entryを具体化 |

### 往復2の確認と最終追記

同じレビュアー `claude-opus-5-5`、判定Request changes。往復1のP1-1/P1-2と非blocker1〜11は全てclosed。新規N1はext2限定がCD/HostDrv rootのFEPを必ず失敗させる反例として採用し、guest側ROだけでHostDrvを許す修正案は外部writerを止めないため採らなかった。

| 指摘 | 最終追記 |
|---|---|
| N1 rootとDB媒体 | §3-2/3-5で不変CDはRO変換/学習なし、HostDrv標準は別ext2管理DB必須。RO facade・配布profile・mount/学習試験を明記。D21/D26を変更しない |
| N2 container整列 | module形式・kernel bootinfo再検査・fixtureに絶対baseのページ整列 |
| N3 wire容量 | 現4本を維持、64+20+32×4=212B、最大5本以内。媒体別閉包を2〜4本へ固定 |
| N4 reloc/BSS | runtimeでも4B全体をpayload内へ限定、EARLY prefixのみの返却を明示 |

往復3: 同じ `claude-opus-5-5` が **Approve**。N1〜N4は全てclosed、blockerなし。最終非blockerはCDのFEP/アプリRO重複許可、SHARED lockの条件付きno-op、xAccessの実有無/エラーとhot journal拒否を§3-2とT4b試験へ追記して対応。PM方針に従い文言追記の追加レビューは行わない。/persist配布profileの承認・測定ゲート・実装/ゲスト検証は未了のまま。

再照合版への独立レビュー r1 (2026-10-16、Opus 5.5) は「直してから確定」、P1なし。F1/F2は反例と第三案を受けて§9の契約判断へ、F3〜F6は受入段・観測条件・粒度・照合/ログ範囲へ反映した。古いApproveを今回の承認に流用せず、採否の詳細は `astra_r2_response.md` に記録する。

## 9. 未決の争点

| 論点 | 両方の言い分・根拠 | 決着が必要な段 |
|---|---|---|
| 3 SQL入口の拒否時のstmt/slotとfinalize時期 (F1/F2) | レビュー側: `db_prepare(h,"SELECT * FROM a")` の残行あり→過長/未終端SQLを拒否→rcを見落とした `db_step(h)` が旧行を返す (`kapi/kapi_db.c:651,704`)。3入口とも旧stmtを実行対象から切り離し後でfinalizeすれば、拒否時engine進入0と次step=DONEを両立できる。本票側: D31が引き継ぐ `docs/tasks/settings/FEP_BOUNDARY.md:140,141,213` はstmtだけでなくslot・SHM結果の不変も明記し、`docs/KAPI_SPEC.md:1396` と `docs/tasks/v3/TASK_T2D_T2H.md:94` はprepare_onlyの**入口finalize**を明記する。第三案は誤実行防止の目的を満たすが、前者のslot更新と後者の資源解放/transaction終了時期まで同じとは言えない。既存契約維持案と下記の第三案のどちらへ揃えるかを判断し、正典を黙って読み替えない | T4b-1の3入口の公開接続前。copy核/反例fixtureだけ先行可。上位の契約は本票本文で変更しない |
| X-11の共有サービスの呼出し主体 | 監査分類:43〜60はフォーカス/端末/本人別の整理を要求し、先着readerが端末の読取りを妨げる等の通常利用順の問題も含む。一方レビューF4と本依頼は単一利用者で新しい授権・本人別保存を求めず、§1-2の共有契約と既存配送で足りるという立場。新たな権限分離は足さず、通常の親子/park-resumeで二重消費・別ownerへの副作用・寿命が守れるかをT4b-5で観測する。攻撃者を置かないことだけを理由に未観測の不具合を閉じない | T4b-5の契約表と通常順の反例を照合。共有意味の変更が必要な部分だけ判断し、受入自体は§7の観測値で判定 |
| HostDrv rootの管理DB配置 | §3-5の/persist ext2案は不変でないHostDrv上のDBを避けつつD21のFEP必須を満たす。一方既存配備のvolume/path/移行手順は未承認 (`docs/tasks/v3/DOCS_REORG_T3.md:104`)。root種別だけでFEPを省略する変更はしない | T5b-6の配布profile実装前。媒体操作は既存[D1]〜[D3]に従う |

F1/F2の第三案 (契約判断前の実装案、未採用): 有効handle/通常文脈を確認後、SQL copy前にactive_stmtを同じslot/owner/groupの固定1本分pendingへ移し、active=NULL・bind不可・次step=DONEとする。拒否時はengine/finalize/VFS呼出し0、固定診断だけを返し、ファイル/transactionを変えない。column/bindからもpendingへ触れず、反復拒否でpendingを増やさない。次のcopy成功済みexec/prepare/prepare_onlyではB4/代替stack上でpendingを一度finalizeしてから新stmtを作る。db_finalize/close・owner終了もpendingを回収対象とし、正常回収前にslot/groupを再利用しない。finalizeの非OKでも同stmtを二度呼ばず、診断とclose失敗時の隔離は `docs/tasks/settings/F2_OWNERSHIP.md:188,194,199` に従う。pendingの資源は生存分として勘定し、最大1本/接続とメタデータ増分をT4bの予算へ含める。
この案を選ぶ場合の追加観測: 3入口それぞれで旧SELECT残行/未実行DML→拒否→step=DONE・旧SQL実行0、拒否反復でpending本数/使用量一定、次の正常入力・finalize・close・子終了で回収1回、cleanup失敗の診断/隔離を確認する。拒否時のslot/SHM期待値と解放時期の正典整合を先に判断し、無効化欠落・先行finalize・pending漏れ/二重finalizeの変異を検出する。現状のB3とprepare_onlyの双方を満たしたものとして、この案を無断で公開接続しない。

FEP予約量・module別サイズ上限・U19可否・8MBピークは、対立した決定ではなく§7の測定ゲート。未測定の数字を埋めて承認済みにしない。今回、上位D番号の追加・上位本文の変更は行わない。契約変更を要する案はこの節の未決として残す。
