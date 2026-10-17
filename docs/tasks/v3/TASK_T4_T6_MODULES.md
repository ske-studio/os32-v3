# TASK_T4_T6_MODULES — SQLite・FEP・起動モジュールの詳細設計

> 状態: **設計中 (2026-10-18、再照合 r4)** — main `952f16b`、T3受入完了 (2026-10-18)、KAPI v76・memory_layout 4。圧縮余裕18,165 B。本票の実装・受入は未実施。
> **ユーザー決定 (2026-10-17)**: 「T4 から T7 までは実機でテストを行わず、エミュレーター上だけで確認を取れれば大丈夫」。真意は「ソフトウェアのコード変更はエミュレータでの確認で十分。ハードウェアに触るカーネルの変更は実機のテストをまとめて行う」— 各段の合否は NP21/W (64MB) で決める。本文で実機の観測を求めている項目 (T5c-4 の 82557 の回帰 — NP21/W は 82557 を再現しないのでホスト試験と LGY-98 の経路で代える、T6b-2 の Ra266 の欄) は、ハードウェアに触る変更がある程度たまった所でまとめて行う実機の回 (台帳 T4T7-RH) へ送り、段の受入を止めない。本文の「実機未確認を NP21/W だけで閉じない」系の記述は、この行が上書きする。
> 改訂: **r4 (2026-10-18)** — RECHECK_r4とOpusレビュー・PM判断F1〜F6を反映。T4a-1着手可、T4全体の受入を意味しない。r3までのレビュー経過は§8。
> 上位契約: [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §3-5・§4・§6・§7。前提: [T2](TASK_T2_APPBAND.md)・[T3](TASK_T3_LAYOUT.md) 受入完了。この票は形式、状態、移行と失敗処理の実装契約を補う。D番号の決定を変更しない。

## 0. 再照合

根拠の `file:line` はmain `952f16b`の `/home/hight/os32-v3/` からの相対位置。[実測] は現物の値、[推] は設計見積もり、未測定は受入前の関門。単一利用者・単一タスク・協調切替を前提とし、バグ・装置の誤動作・資源不足から戻れる範囲を明記する。認証・暗号・権限分離の追加は行わない。性能判定は P100/32MB の前景1本 (上位票:31)、装置の新しい扱いは [HW3] の資料＋BSD/MIT実装を根拠にする (`docs/CONSTRAINTS.md:97`)。

| 元票の前提・着手条件 | 判定 | 今の根拠と本票での扱い |
|---|---|---|
| T2c〜h の map/allocator・trim・owner/STOP 待ち | 吸収済み (T2) | `docs/tasks/v3/TASK_T2D_T2H.md:304,337,349`、`docs/tasks/agents/HANDOVER_2026-10-09.md:11`。受入済み機構を使い、台帳の未採取を完了扱いにしない |
| T2 → T3 → T4 → T5a/b/c → T6b | 維持 | `docs/tasks/v3/TASK_MEMMAP_V3.md:488`。T3配置は受入済み。本票各段で差分を再計測する。既存S4とLAN受入範囲を後続の正常対照にする (§2・§6) |
| e11 公開版・4世代を後で照合 | 吸収済み (T2/DBGREF) | `sdk/kapi.json:2`、`:3`: format=4、KAPI ABI=1、memory=4、shlib=2、機能版76。更新時は生成値で照合する |
| 専用module核・生成器・リンカを新設 | 要改訂 (既存を拡張) | DBGREF S4 (私的import235本)、`kernel/module_image.h:7`、`kernel/module.c:37`、`tools/mkmod.py:15`、`build/module.ld:1`。kdebug専用policyを一般化する (§2) |
| SQLiteリンク時連結配置・rootがPG/台帳より先 | 維持 | `build/os32.ld:60`、`build/kernel.mk:200`、`kernel/kernel.c:453,524,575,700`。EARLY化とSQLite分離は未実装 |
| B1 checked copy と B3/B4・F2/F3 は別 | 維持 | `docs/tasks/v3/TASK_T2D_T2H.md:94,587`。`db_exec` の切捨て・finalize先行は `kapi/kapi_db.c:610`。旧SQL入口も含む拒否時不変契約は§3-1・§9 |
| 512KiB単一MEMSYS5・FEP優先取り分 | 維持 | D27、現行は384KiB (`lib/sqlite3/os32_sqlite_vfs.c:34`)。512KiB化・代替stack全面化・取り分保証はT4 |
| FEP予約量・S/M/L最大負荷・断片化保証 | 未測定 | §3-3に従いT4a-3/4で数値化。過去の常駐38KBだけを予約量にしない |
| LAN/82557の移設範囲 | 更新 | TX/RX/IRQ・BOUNDとLB-3・LAN版/host実機受入済み (`drivers/e82557.c:377,700,729,793`、`docs/tasks/realhw/TASK_LAN_82557.md:3`)。T5c-4で移設回帰。LB-4まで完了とはしない |
| gfx資源識別・予約をcoreに残す | 維持 | R7FIX2後はPEGCのFFF00000h窓 (`include/memmap.h:337,357`)。旧F00000h固定期待へ戻さず、永久予約とmodule回収を分ける |
| HostDrv rootの管理ext2 DB配置 | 不採用 (2026-10-16 決定) | §3-5・§9。HostDrv は補助マウント、管理 DB は通常 NHD ext2 root に置く |
| 8MB/17MB/実機の構成別受入 | 要改訂 (実施時期のみ) | `docs/tasks/DEFERRED_TESTS.md:19` の10-14決定で最終段へ。途中は64MB、D9/D21の最低条件・必須サービスは維持 |
| 像・RAMの基準 | T3受入済み | 現行常駐鎖とSLACKは下記。各段の増減・連続量は未測定 |

**T3引渡し値** (`docs/tasks/v3/TASK_T3_LAYOUT.md:192`)：既存成果物のbuild IDは `ab020da` (`build/out/kernel.map:3833`)。main `952f16b` との差は文書3本と `tools/tests/test_nano_adapter.py` のみ。再ビルド値ではない。
`__bss_end=__sqlite_start=0x198000`、`__sqlite_end=0x254200`、`__kernel_resident_end=0x276000` (`build/out/kernel.map:4473,4477,8033,8038`)。core BSS→SQLite→guard→128KiB stackのリンク時連結配置で、KHEAP192KiBはresident終端から算出する (`build/os32.ld:60`、`include/memmap.h:42,228`)。常駐像上限0x275000 B、resident占有0x176000 B。鎖末尾0x2E1000からSLACK終端0x3E0000まで0xFF000=1,044,480 B (255頁) だが、連続空きの保証ではない。
案5の配置はDMA=0x7F0000/64KiB、PT26頁 (使用11)、guard=0x3FB000、kstack=0x3FC000から16KiB、shell=0x400000 (`include/memmap.h:64,247,271,297,307`)。後続の配置期待はこの基準から導出する。
`kernel.bin`=390,340 B、`sqlite.bin`=374,968 B、`vmkernel.lz4`=502,027 B、508KiB上限まで **520,192−502,027=18,165 B**。T3受入時のSLACK free=236/busy=19、KHEAP peak=3,784 B、kstack peak=4,348 Bは観測範囲の値。各段で同構成のmap・圧縮長・最大連続空きを測り直す (§7-2)。

## 1. 現在との差と共通境界

基点では `build/os32.ld` と `build/kernel.mk` が SQLite をcore BSS直後の連結セクションへ出力し、`kernel/kernel.c` が直接初期化する。`kernel/shlib.c` はモジュール再配置器ではない。`boot/vk32_boot.c` は複数エントリを検査するが、占有は raw_size、集積との非重複は低位集積を前提にする。`kernel_main` のルートマウントは `paging_init` / `memory_boot_init` より先。これらを変更済みとみなさない。

### 1-1. 基点で照合した接続先

| 実物 (file:line / シンボル) | 現在と後続の区別 |
|---|---|
| `build/os32.ld:60` (`__sqlite_start`)、`build/kernel.mk:200` (ELFから宛先導出) | リンク時連結SQLiteの出力を継続。module 形式には未移行 |
| `kernel/kernel.c:453` (`vfs_mount`)、`:524` (`paging_init`)、`:575` (`memory_boot_init`)、`:700` (`os32_sqlite_init`) | root → PG/台帳/DMA → Unicode (:598) → 監査 (:661) → exec (:665) → IME (:677) → SQLiteの現行順。§5 の並べ替えは後続設計 |
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

X-11の出発点は `~/os32-tmp/evidence/2026-10-07/audit-classification.md:33,43,57,60`。現在は全体の直前exec結果 (`exec/exec.c:2190`)、先着単一reader (`kernel/con_sink.c:251`)、WMへ実ownerを渡す入口 (`kernel/gui.c:66`)、共有FEP (`kernel/ime.c:603,641,715,767,850,891,912`) である。今回の方針案は、単一利用者の共有サービスという意味を保ち、正常な利用順・無効値・取消・出力寿命を明文化すること。本人別の結果保存や新しい権限表を自動追加しない。共有入力を二重消費する反例が出た場合は既存のWM/前景配送との接続で解消可能か確認し、契約選択が残るものは個別の契約判断へ送る。T4b-5の判定前にX-11を吸収済みとはしない。

## 2. モジュール形式と呼出し契約

### 2-1. バイト形式案

kdebug 固有の現行契約は [TASK_DBGREF §3-2](TASK_DBGREF.md#3-2-部品の形式--t4-と同じ形式として-1-つのスキーマに確定-q3)。LB-1 で ops は**版 2、18 口**となり、旧版は ABI_MISMATCH で拒否する。

little-endianの明示的decodeを使い、C構造体のcast/sizeofでファイルを読まない。**KMOD v1・144Bヘッダの項目順は既に確定** (`kernel/module_image.h:7,23`、`docs/tasks/v3/TASK_DBGREF.md:131`)。下表は意味の表であり再配置案ではない。C/ASM/Pythonはこの順序から生成・照合し、別スキーマを作らない。

ヘッダword 0〜35は `magic,format,header_size,image_size,kapi,layout,module_abi,id,phase,flags,mem_size,payload_off,payload_size,text_off/size,ro_off/size,data_off/size,bss_off/size,reloc_off/count,import_off/count,export_off/count,names_off/size,init,stop,pair_crc,image_crc,ops_version,reserved0,reserved1`。表offsetはcontainer基点、節・入口・reloc書込先・slotはbody基点。relocは4B、importは `{u32 hash; u16 type_ver,kind; u32 slot_off}`、exportは `{u32 hash; u16 slot,ver; u32 entry_off}` の12B。名前は順序付きNUL列、63B上限、hashと実名を照合し衝突は生成失敗。CRCはimage_crc欄 (byte128〜131) を0にした容器全体 (`docs/tasks/v3/TASK_DBGREF.md:132,158,164`)。
現KDEBUG policyはID=1/POST_ROOT/PAIR必須/stop=0/ops=2・18口/画像とbody各256KiB/reloc≤16,384/import≤1,024/export≤64 (`kernel/module_image.c:49,103`)。T4a-1でID・phase・stop・ops・サイズ上限を生成policyへ移し、KDEBUGの現契約とOFF/RUNNING/拒否診断を保持する。SQLITEの初期画像/body上限案は各512KiB [推]、load_span上限・表件数はT4a-2の実画像で確定し、超過を握り潰さない。**374,968Bの現SQLiteファイルは現256KiB上限に入らない**。
wire名 `kapi_abi_gen` は現KMODではKAPI機能版76を入れる欄であり、D35のKAPI ABI世代1とは別 (`docs/tasks/v3/TASK_DBGREF.md:141`、`sdk/kapi.json:2`)。意味を黙って入れ替えず、4世代は生成物の印と配備一式で照合する。`module_abi_gen` はmodule別import定義のFNV-1a、`ops_version` はexport契約。KDEBUGのpair CRCは維持。EARLYには自己を含むVK32 CRCを埋める循環を作らずPAIR無しをID別policyで許し、VK32全体/entry CRC・世代・配布一式で照合する (T5b-1の生成試験)。

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

prefixを処理後に返す場合、exportの解決済み関数表・dependency/owner/generation等の継続利用情報を先に固定容量のmodule recordへコピーする。保存容量不足はinit前に拒否。prefixを指す文字列/表ポインタをRUNNING状態へ持ち越さない。BSSゼロ→reloc→importをbodyだけへ適用した後、prefixページを返せる。成功/隔離したbodyページは保持する。現DBGREFの定常bodyは49,152B (48KiB)、prefixは12,288B、load_spanは61,440B (60KiB)、ファイルは57,528B (既存 `build/out/kdebug.bin` のword10〜12をdecodeした実測)。

**POST_ROOTの画像読込も本体へ直接行い、別の全画像stagingを取らない。** 小さな固定header bufferへ読み、既知形式・完全長上限・load_spanの算術・許可module IDを検査 → owner付きload_span連続割当 → prefix+payloadをその割当へ短読検査付きで読む → 全画像CRC/範囲/表を検証する。検証中にファイルを再読してrelocを書き換えず、確保した写しだけを参照する。全体検証失敗はinitを一度も呼ばず割当を返す。この領域はR4で許すmodule本体 (prefixを含む) であり、別の400KiB連続scratchではない。EARLYは同じ形式の同梱域を直接検証する。ピークにはprefix+bodyを全量計上し、prefix解放後を定常量と区別する。

S4の型・slot・生成ABI・pair CRC・ops2/18口を正常対照として維持する (`kdebug/generated/kdebug_imports.h:119`、`tools/gen_kdebug_imports.py:25,75,170`)。SQLite用import定義は別に作り、現235本へ一律追加しない。生成済みファイルは手編集しない。`tools/mkmod.py:211,297` の旧既定値75/3はT4a-1で必須引数化または `sdk/kapi.json` 読込へ変更する。

`make kernel` は「カーネルの像と、それと対になって起動時に読む部品の一式」を組む (部品は像の CRC を埋めるので像と一緒に組み直す。Tx の段ごとの `make kernel` で部品の誤りを拾うため、ユーザー決定 2026-10-18)。起動時に読むSQLite・FEP・後続部品はkdebugと同じ **`/boot`** に置き、`hsync boot` で対として運ぶ (PM決定)。機能版完全一致 (`kernel/module_image.c:103`) を維持し、同じ公開ABI世代でも旧部品を残さない。配布の接続は§5-4。

T5c-2b/T5c-3の依頼前に、kdebugのPCM/V86 importをcore facade経由で残すか、該当自己試験を部品側へ移すかを決める。対象はpcm_open・pcm_state・v86_* 13本・paging_v86_session_open (`kernel/kdebug_imports.def:895,903,911,1615`)、S4の本数・ABI印も再生成する。現形式に部品間importを追加したものとは扱わない。

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

**B3:** T2 bounded copyでpath/SQLをkernelへ完了してから旧stmt finalize。SQLは既存1024Bバッファ (payload1023B) にNULまで入らなければ失敗し、切捨てない。不正入力時はSQLite進入0、既存stmt・transaction・slot・SHM結果を維持する基準契約 (`docs/tasks/settings/FEP_BOUNDARY.md:140,141,213`) を変えない。copyoutはengineを出てから行う。対象は旧 `db_exec/db_prepare` を含む全outer入力。§9 の決定に従い、3入口とも拒否時は何も変えない。拒否の rc を見落として step すると旧文の続きが返るのはアプリ側の問題として扱う。prepare_only も入力を受け付けた場合だけ finalize する設計に揃える。KAPI_SPEC の契約説明の同期は T4b-1 の前に行う (本票の決定を実装済みとはしない)。

EXECNESTのcaller/frameとTSS.ESP0の保存・IRQ保護下復元 (`exec/exec.c:338`) はSQLiteのESP切替と別契約。通常CPL0呼出しではTSSを変更しない。子の正常/fault/STOP→親復帰→次KAPIとsplash終了で元のesp0/callerを確認する。現128KiB stackはboot testだけが使用 (`kernel/kernel.c:704`)。全outerへの適用・owner化・engine中fault/STOP処理は未実装である。

### 3-2. F2/F3 と重複接続の移行規則

[F2_OWNERSHIP](../settings/F2_OWNERSHIP.md) のgroup/leaseを唯一のFD所有モデルにする。`os32_sqlite_group_open` を全openへ適用し、default VFSの所属無しopenはfail-closed。RESIDENT groupは **2slot、1group=1接続=1回open**。bootではsettings/selftestが1slotを順次acquire→open→close→finishして返し、両slotがFREEになってからFEPを開始する。DICT_META前はFEPが1slot、後は辞書RO+学習RWの2slotを同時に使う。settingsはFEPのslotを借りたまま保持しない。settings close失敗ならそのslotをQUARANTINEDにし、残slotで標準起動を強行せずboot transactionを失敗させる。後発journalも同group。close/finalize失敗はquarantineへ残し、同じownerを正常retireしない。`exec_cleanup_owned_resources` はアプリgroupだけを処理しFEP/親groupに触れない。

F3a/c: xTruncate/xSyncのI/O rcを `SQLITE_IOERR_TRUNCATE` / `SQLITE_IOERR_FSYNC` へ翻訳、xDeleteは未存在と実I/O失敗を区別し失敗を成功にしない。xAccessは有無/読取/書込可否とI/O失敗を分ける。xOpenのpOutFlagsを実open結果から返し、RO接続のwriteはVFS側でも拒否する。SQLite lock表は作らない (D29、`docs/tasks/v3/TASK_MEMMAP_V3.md:45`)。

**F3b前の暫定重複禁止案:** 新しいlock表は持たず、既存live DB/groupレコードを走査して同じファイルの2本目の接続をmode/ownerによらず拒否する。parked・親子・RESIDENT・close失敗quarantineを含む。更新可能なFEP DBへのアプリ直openも拒否し、学習はFEP facadeからだけ行う。不変CDのREADONLY例外ではFEPとアプリの同じDBへのread-only重複openを許す (pathによる特例拒否は設けない)。`:memory:` は別接続ごとの実体なので対象外。判定は文字列pathでなく **MAIN_DBのxOpenで取得したFD** のidentity `(mount_generation, fs_file_id)`。CREATEで初めて作るファイルも、破壊的truncateを伴わないopen後にinodeを取得→既存group走査→groupへ登録、の順にする。重複なら新FDだけcloseしてSQLiteへopen失敗を返し、既存接続とファイル内容を変えない。相対/絶対/別名も同一とみなす。identity取得→走査→group登録は通常文脈の非再入区間、失敗/close完了でのみ解除する。

現行で安定inodeが取れるのはext2なので、**更新可能なfile-backed SQLiteはext2に限定**する。別に **不変の配布CD/iso9660上のDBをREADONLYだけ許す**。後者はguest側のRO mountだけで判定せず、boot policyが指定した不変配布媒体であること、媒体generationがopenからcloseまで不変、配布DBがclean close済みでjournal/WAL無し、を条件とする。媒体交換/読込不能はIOERRで失敗し、旧接続のまま新媒体を読まない。イメージを使う検証でも実行中にホストから媒体ファイルを書換えない。不変ROでは重複readerを許してよいが、CREATE/RW/journal/write/truncateはVFSでも拒否する。この接続のSHARED lock/unlockは不変媒体generationが有効なら成功するno-opとし、書込み用lock昇格は拒否する。xAccessは要求modeに応じて実際の有無/読取可否を書き、I/O失敗はエラーを返す (全て存在/成功にしない)。hot journal/WALが存在すれば不変配布DBの条件違反としてopenを拒否し、回復書込みを試みない。

**HostDrvはguest側ROでも外部writerがいるのでこの例外に含めない。** FAT/HostDrvの直接MAIN_DB openはFS能力判定でファイル作成前に非対応を返す。`:memory:` は維持する。root媒体とDB置場の接続、CDのFEPとHostDrv標準構成は§3-5に定める。影響を受ける `db_*`、libos32cfgの任意path、db_test、installerはこの可否をエラーとして扱う。unsupported backendへpath比較の弱い代替を設けない。LAN版/hostも同じ外部更新可能なFSとして扱い、パス名やtransportだけでext2/不変CDの能力を与えない。

これは排他openの汎用サービスの前倒しではなく、T4のSQLite入口で危険な重複を断る暫定制約。必要なVFS identityが現行で不足する場合はT4bで内部APIとして足す。既存ユーザーの同一DB複数open依存を調査し、必要なら接続共有へ移す。F2 T01は親/子/同ownerに**別のDBファイル**を使って所有回収を試験し、同じファイルのケースは2本目拒否の別試験へ。T12はFEP初回openをbootで済ませ、子実行中に初めて学習journalを開くケースへ読み替える。意味変更を隠してpath比較だけで合格にしない。

FDWP-1のFatFs修正は読込起動の回帰基準であり、SQLite RO契約の受入ではない。dirty時だけ書込み、flush成功後だけ窓を更新する (`fs/fatfs/ff.c:1062,1085`、`fs/fatfs_vfs.c:207`)。FAT切出し後も書込み禁止FDからroot/Unicode/shellを読む。F3のrc伝播とCD不変媒体条件は本節で実装する。

### 3-3. FEP予算と設定の循環

D27の「512KiBの単一MEMSYS5」は維持する。`cache_size`やsoft limitだけを予約保証と呼ばない。起動はSQLite既定値 → settingsを短命RESIDENT接続で読みclose → 検査済み `mem_reserve_kb` を決定 → FEP辞書/stmtの優先初期化 → アプリopen許可。DICT_METAまでは辞書metaを読まない。settingsが読めなければ既定値で続行し、FEPのinitが成立しなければD26のMINIMALへ。

実装案は既存MEMSYS5の外側に**割当許可の計数ラッパ**を置くこと。`SQLITE_CONFIG_HEAP`でMEMSYS5を設定 → `SQLITE_CONFIG_GETMALLOC`でそのallocatorを保存 → `SQLITE_CONFIG_MALLOC`でラッパ登録 → initialize、の順にする (`sqlite3.c` のHEAP処理はallocator表自体を置換する)。別ヒープは作らない。engineの呼出し文脈からFEP/APP/SYSTEMを識別し、app割当は将来FEPに残す量を割り込むとNULL。free/reallocはallocation側のタグ/実サイズで戻す (free実行時ownerを使わない)。共有engineメモリはSYSTEM勘定とし、取り分計算へ含める。ラッパのメタデータ分も512KiBの予算内で数える。

ただし総空きだけではMEMSYS5の断片化を保証できない。T4aの最小prototypeで、使用SQLite版のallocator hooks、丸め・realloc失敗時旧ブロック維持、FEP検索/学習の最大一時割当、appのsort/prepare連打後の1語変換を検証する。保証が成立しないなら予約量/アプリ上限を調整して再検証し、別ヒープや512KiB増量は本票で承認済みにしない。タグはpayload前置きでMEMSYS5の2冪丸めを増やす案だけで決めず、固定容量の外部タグ表+元allocatorのxSize利用と比較する。外部表のbyte数も512KiB予算に含める (heapへ渡す部分と表の和が512KiB)、表満杯なら割当を副作用前に拒否。S/M/Lの **最大負荷辞書** と全切替経路で同じ予約値を検証する。DICT_META前は個別metaが無いためS/M/L全部に通る共通予約量を既定とし、未検証の任意辞書へのlive切替は拒否する。settingsで量を変えるときは再起動し再評価する。既定予約量の数値はこの測定ゲートで決める。

辞書切替が起動時予約を超える場合は設定だけ保存し再起動要求、現在の辞書を保つ。アプリopen上限、割当拒否、FEP使用量を診断し、FEPを任意省略してメモリ試験を通さない。

負荷fixtureのUSER mallocはSQLite MEMSYS5と別計上する。shellのexec_heap256KiB固定分割 (`exec/exec.c:1638`) とSDK副arena最小64KiB (管理頁込み、失敗時は旧丸め量で再試行、`sdk/allocator/nano_adapter.c:260`) を使い、既存sbrk/exec_heap採取を再利用する。F-9のO(N)・extent上限・reallocはT4で再設計しない。

### 3-4. 必須サービスを一括公開するboot transaction

`ROOT_READY → Unicode読込 → SQLITE_INTERNAL_READY → SETTINGS_CLOSED → FEP_INTERNAL_READY → SERVICES_READY`。Unicode表はkernel ownerの7頁でSLACK/poolの内数 (`kernel/unicode_file.c:15`)。SQLite/FEP失敗で回収しない。欠落・旧128KiB形式・I/O失敗はかなのみで継続し、FEP省略・起動停止の条件にしない。途中は一般のdb/ime KAPIがstubのまま、信頼するboot coordinatorとFEPだけが内部SQLite exportを使う。settingsはengine_enter経由、予約評価もこの段で行う。FEP正常起動とgroup/予約確認が揃って初めてservices_publicを一度に開く。最初のユーザーASはこの後なので、半公開をアプリが見ることはない。T4の案AではFEP固定コードはcoreに残り、回収量0の対象はその動的資源とSQLite module。T5aの案B以後はFEP bodyも同じrollback対象に加わる (上位票 `docs/tasks/v3/TASK_MEMMAP_V3.md:442`)。

FEP画像CRC不一致・辞書なし・予約不足等は利用者へ未公開の失敗として、FEPの部分stmt/groupをclose/finalize → settingsを含むRESIDENT groupゼロを確認 → sqlite3_shutdown → 内部exportをstubへ → RUNNING/STARTINGからROLLING_BACK → FEP/SQLite ownerの回収、の順。init自身で失敗した場合も同じ段階印で一度だけ処理する。正常rollback後はSQLite/FEPの占有ゼロ、全dbはNOSYSのMINIMALへ入る。close/shutdown/装置停止の証明が失敗したらcode/heap/stack/groupを依存閉包ごとQUARANTINEDに残す。一般KAPIはstubだが **MINIMAL_QUARANTINED** と別状態で診断し、隔離byte数を空きから差し引く。これを標準MINIMALや8MB標準合格に数えない。安全な入力/回復継続を証明できなければ起動停止する。

### 3-5. CD と通常 root の FEP

これは新しい実装契約案で、現行 `ime_dict_open` がREADWRITE固定であることを変更する。D21のFEP必須とD26の実失敗rollbackは維持し、rootのFS種別だけでMINIMALを選ばない。

- **CD標準構成:** 不変配布CDの既存combined `fep.db` をROで開いて変換を提供する。`ime_dict_open(mode)` にROを加え、CREATE TABLE/INDEXと学習stmtのprepareを行わず、`dict`と存在する場合の`dict_user`を読む検索stmtだけ作る。`dict_user`欠落なら辞書本体だけを検索する。学習は無操作、明示的な削除/clear等の更新APIはROFS、list/exportは読取として維持する。状態診断で「変換可・学習不可」を示す。これはFEP省略ではなく、DICT_METAの学習別file化も前倒ししない。settingsも不変CDをROで読み、更新はROFS。学習を保存する通常構成は NHD ext2 root を使う。
- **通常構成:** 管理 DB は通常 root (NHD ext2) の既定配置を使う。HostDrv は root にしない (§9 の決定)。

CD の RO FEP は T4c-3 の host fixture で1語変換・学習journal生成ゼロ・更新ROFSを確認し、CD 実起動は T5b-5 で受ける。T5b-6 は HostDrv の補助マウントと通常 root の管理 DB が共存することを確認する。媒体変更は [D1]〜[D3] に従う。

## 4. T5a FEP 案 B

T4でfacade化したFEPを `/boot/fep.mod` へ移す。SQLite RUNNINGの依存を必須とし、未公開init中に辞書group/stmt/入力状態を全て準備してからexportを一括公開する。core側stubの意味は上位票 §5-6 (`docs/tasks/v3/TASK_MEMMAP_V3.md:444`) のAPI別表をそのままテストoracleとする。is_active/get_modeは0、set_mode/toggle/set_render/switch_dictは無操作、feed_key(k)はk≥0でk・負なら0、getkey/getcharは生のblocking入力、trygetkeyはkbd_trygetkeyの生キー (無入力だけ−1)、trygetcharはその文字、ime_user_*はNOSYS。MINIMALの英数編集を非ブロッキング/ブロッキング両経路で確認する。

B2 facadeは `ime_user_list/delete/export` の全入力を先にbounded copy → kernel staging → bind/step → stmt finalize/resetとbinding寿命終了 → engine_leave → copyout、の順。listは上限件数/文字列長で打ち切り状態を返す。deleteの不正kanjiをNULL条件に変換しない。exportはpath検証完了前にcreate/truncateしない。上限は [FEP_BOUNDARY §3](../settings/FEP_BOUNDARY.md) を引継ぎ、失敗時に返却バッファを部分公開しない。具体値はprefix/yomi/kanji/path各256B (NUL込み)、list max=1〜64、staging=64×68=4,352Bを専用領域に置く。NULL prefixは全件、NULL/正常な空kanjiは同一yomi全候補削除、不正kanjiは失敗。上限±1とページ末尾NUL、失敗後busy解除を試す (`docs/tasks/settings/FEP_BOUNDARY.md:45`)。

`dict_fd_protect` を廃止しRESIDENT groupへ集約する。close失敗は記録してreopenを拒否する。SQLite→FEPの依存順を守り、FEP init失敗で標準起動を成功扱いにしない。MINIMALへ落とす際も停止確認済みのgroupだけ解放する。

## 5. T5b 早期ロードと同梱

### 5-1. ブート段階

`BOOTINFO_SAVED → MEMORY_READY → EARLY_MODULES_READY → ROOT_READY → Unicode読込 → SERVICES_READY → USER_READY`。

1. bootinfo/BDAを保存、RAM検出、必要なcoreコンソール/例外/heapを準備。
2. PG/台帳をルートマウント前へ移し、集積域・同梱域を**最初の物理割当より前に**予約。DMA初期化もroot driverより前へ。
3. 同梱EARLYモジュールを検証・in-place再配置・起動しFS/装置を登録。importはcoreと既にRUNNINGのEARLY依存だけ。VFSファイル読込やSQLite/settingsを使わない。
4. `/` マウント後にUnicode表を読む (§3-4)。ここから `/boot` の部品と一覧を読める。短いsystem.cfgの読込、gfx識別→永久予約→写像→BB確保をlive AS=0で済ませる。GUI probe/enableは後。
5. exec/shlibの準備 → SQLite→FEP → その他の通常モジュール。最初のAS以前に共通PDEの追加を終える (上位票 `docs/tasks/v3/TASK_MEMMAP_V3.md:337` の順を明記)。kdebugはroot/台帳/DMAの成立後、呼ぶ自己試験より前に読む。OFFでもkauditは残す。
6. スプラッシュアプリを実行・回収してからrshell/SerialFS対話とshellへ。gfxを使う場合は通常のGUI境界を経由する。

U19の移動可否はT5b入口でcore初期化の全依存を確認するゲート。`kernel/kernel.c:375,453,524,575,590,594,598,661,665,677,700` のFS登録・root I/O・PG/台帳/DMA・kdebug・Unicode・監査・exec・IME・SQLiteを依存表にし、PG前のBDA読取、console/IRQ/heap、root driverのDMA要求と初回物理割当を追う。移動不能と判明した場合のみ、上位票の代替「同梱固定予約」を具体化して再レビューし、未完成のpgallocへ逃げない。

### 5-2. 媒体別閉包と所有権

| 媒体 | 同梱依存の閉包 | 失敗 |
|---|---|---|
| HDD/ext2 | coreのIDE/ext2でroot、core1エントリ・同梱不要 | root未成立なら回復媒体へ。通常モジュール探索で循環させない |
| FD | core FDC + FAT EARLY、合計2エントリ | FAT失敗なら起動停止。SQLite/FEPなしMINIMALはroot成立後の回復構成 |
| CD | core + ATAPI EARLY + iso9660 EARLY =3、HostDrv併用でも追加1で計4。iso9660側のdiskio等を依存画像へ静的に含める | ATAPI未起動でiso9660登録しない |
| HostDrv (補助マウント) | CD併用時は HostDrv EARLY を追加して計4エントリ。np2sys transport は同画像に含める | root には使わない。実機で専用I/Oへ触れない |

同梱域は1MiBの予約をまずboot ownerで保持し、各エントリを **VK32 entry.mem_size = load_span (prefix+body)** で配置する。BSS・整列を含め重複なし、同じページを2ownerに分けない。検証→再配置→initの順にmodule ownerへ占有ページを移譲し、**全エントリの処理と参照終了後に、余白ページだけ**返す。EARLYでも§2-1の継続情報コピーが済んだprefixページだけは余白として返せる。成功module/隔離moduleのbodyページは返さない。圧縮集積域は全展開・CRCとbootinfoへの結果コピーが終わった後に返す。

VK32生成器の実物は `tools/mkvmkernel.py` (`build/kernel.mk:210`)。上位票でいう `mkvk32` と同じ担当として、複数エントリと `raw_size/mem_size` を区別する形式へ更新する。C側HDDとASM側FDの検査は同じ壊れた画像fixtureで一致させる。許可destination種別をcore/bundleに限り、画像が宣言した任意住所へ書かない。旧形式は入口前に拒否する。

### 5-3. bootinfoとMINIMAL回復の境界

`include/bootinfo.h` の次形式に固定上限VK32_MAX_ENTRIESのentry配列を追加する。各wire entryは `{kind, module_id, container_phys, raw_size, load_span, payload_offset, body_mem_size, decoded_crc}` (各u32)、headerに `{wire_size, entry_count, layout_gen, image_crc, table_crc}` を持つ。既存drive/image情報64Bを保持し、追加header20B+entry32B×4で **212B**。MEM_BOOTINFO_SIZEは256Bなので `VK32_MAX_ENTRIES=4` を維持し、形式上の上限も `VK32_MAX_ENTRIES<=5` とASSERTする。形式versionは更新する。予約領域 `MEM_BOOTINFO_SIZE` 内へ収まるASSERT、C/ASM/Pythonのoffset照合を必須とし、収まらなければ黙って低位の隣へ伸ばさず新形式を再設計する。

loaderは全展開/CRC完了後にこの表を書き、最後に有効印をcommit。kernel最初のbootinfo captureが固定core配列へコピーし、count/CRC/世代・許可kind・containerのページ整列・範囲・非重複・`load_span=payload_offset+body_mem_size`を再検査する。bundle entryはmodule自身のヘッダ値とも照合してからEARLYを開始する。core entryはmoduleヘッダを持たないためmodule_id=0・payload_offset=0とし、body_mem_size=ページ切上げしたcore占有 (BSS込み) をリンク情報から生成してload_spanと照合する。未知/欠損情報は推測で復元せず停止する。台帳予約はコピーした値だけを使い、集積域を返した後にloader内ポインタを参照しない。

MINIMALでSQLiteを使う `install --recover-settings` は動作しない (D21)。この制限をエラー文言と回復手順へ明示し、NOSYSを「DB破損」や「復旧済み」に変換しない。MINIMALは媒体/ファイルの退避・正しいシステム一式の復元を担い、DB修復はSQLite/FEPが正常な標準構成またはホスト側の既存手順で行う。MINIMALでSQL修復まで必要という要求はD21の変更判断へ送り、本票でSQLiteを密かに読み込まない。

### 5-4. 配布・更新と予算不足時の順序

SQLite分離のT4c-1でVK32生成・依存・配布をkernel/sqliteの2本からcore1本へ同時変更する (`build/kernel.mk:209`、`tools/mkvmkernel.py:228`)。EARLY追加後は§5-2の2〜4本へ。最大4本は既存 (`boot/boot_defs.h:25`)、今回の拡張はraw_size占有からload_span/BSSとbootinfo形式への移行である。

起動部品は全て `/boot` に置く (§2-1)。上位票の `/sys/fep.mod` (`docs/tasks/v3/TASK_MEMMAP_V3.md:446,483`) もT5a-2で同期する。`hsync boot` の同世代カーネル先行更新 (`userland/system/hsync.c:869`) で部品も対で運ぶ。FDRECOV-1の稼働root世代門・名札format=2 (`userland/system/hsync.c:412,681,2444,2853`) を維持し、部品・対kdebug・28,252B Unicode表を配布一式に含める。CD/FDにも表を含め、HostDrvから自動代替読込しない。部品ABIは公開KAPI ABIとは別であり、既存CRC/policy照合を併用する。新しい一般世代管理器は作らない。

新VK32はloaderに加え、v2のentry幅/CRC位置を写す `userland/system/hsync_bootold.inc:25`、`userland/system/hsync.c:1709` とboot_image_info利用者も更新する。旧像の復旧用読取と新像の受理/拒否を区別し、正常更新をboot_image_unreadableにしない。同世代更新・異世代0件書込・別root復旧を回帰する。

T4a/bの圧縮予算不足には、案A＝不要増分を落として段を縮め508KiBを守る。案B＝**T4c-1をT4bより前へ並べ替える (承認済みの範囲でPMが決める)**。SQLiteはT4c-1でVK32から外れるため、T6b前倒しやユーザーへの再判断は要しない。実不足量を測って選び、圧縮上限そのものはT6bまで維持する (`boot/boot_defs.h:91`)。

## 6. T5c 外へ出す順序と T6b

| 順 | モジュール候補 | coreに残す接続 / 特別な受入 |
|---|---|---|
| 1 | KCG ROM | コンソールからのtyped facade、未搭載診断。T7a前は旧font経路との排他 |
| 2 | FM+snd、PCM | IRQ/tick/DMA broker。登録直後失敗・停止不能を必ず注入 |
| 3 | V86 | entry/exit facade。低位PTE切替と例外の復帰コードの寿命を証明、稼働中回収なし |
| 4 | LAN/82557 | core PCI識別/永久resource、driver bindのSTARTING/RUNNING/QUARANTINEとmodule状態を対応。受入済みTX/RX/IRQ・LAN同期を移す。LB-4の完了は推定しない。LAN版/hostのNIC参照・session寿命も確認 |
| 5 | HostDrv / np2sysp+シームレスmouse / Cirrus+WAB | 検出前の専用I/Oを禁止。HostDrvを補助マウント用EARLYで同梱した場合は同IDをPOST_ROOTで再ロードしない。Cirrusの識別glueだけcoreに残す |
| 6 | スプラッシュ | CPL=3アプリ、BB lease借用→終了解除。表示失敗で常駐shellを止めない |

gfxの副作用のない識別・resource記録はcoreに残す案とする。R7FIX2のPEGC別名窓FFF00000hの予約/写像 (`include/memmap.h:357`、`gfx/gfx_core.c:381`) はmodule失敗でも消さない。LANの装置手順は `docs/tasks/realhw/TASK_LAN_82557.md:51` のIntel/National資料とFreeBSD BSD実装を引継ぎ、新手順は[HW3]の根拠を添えてから実装する。同票:40の旧Linux参照を写し元にはしない。Trident enable/probe、合成器、追加NICはP4/P5の票でこのloaderへ載せる。USBをここで追加しない。起動一覧はmodule ID/phase/必須性/依存/許可パスを持つ配布manifestと照合し、任意ユーザーパスからのCPL=0ロード口を公開しない。必須集合と許可ID/phaseはcoreの生成policyで固定し、配布manifestはそのpolicyの任意モジュールの選択/期待hashを指定するだけ。root FSの閉包はboot媒体種別から決まり、manifestで省略不可。SQLite/FEPはMINIMALを除き必須で、manifestからの削除で省略扱いにしない。manifest欠落/破損/循環/重複/未知IDはユーザー未公開のboot失敗とし、root成立後は§3-4のrollbackを経てMINIMALへ、root閉包自体の失敗は停止する。

KCG移設でもUnicode探索はcoreに残す。撤去済みleaseや表ポインタimportを復活させない。低位 `MEM_KCG_LZ4_TEMP_BASE/END` はT7aまでFIXEDのまま (`include/memmap.h:118`、T3補遺)。font close→BB/mailbox初期化→USER公開を守り、splash/V86回収で一般池へ返さない。

h4は既存の低位/物理32MiB超USER bufferでext2/HostDrv往復とowner回収を行う (`kdebug/kselftest.c:1012,1182`)。移設後もS4 VFS/DMA importはcore facade経由で保ち (:1717,1845)、依存が使える時点で同fixtureを回す。POST_ROOT HostDrvをh4より後に置いてSKIPさせない。DMA境界検査のtransfer=none (:1175) はPCM/FDC実転送の証拠ではない。

T6bは最後に集積を上位票の `[0x500000,0x600000)` へ移し上限1MiBにする。loader自身、stack、圧縮源、core展開先、同梱のmem_size占有、bootinfoを区間表へ載せ、**全宛先を検証するまで1バイトも展開しない**。source/destinationの重複も検査する。FDのreal-mode読込からPMコピーへの境界でsegment wrap/64KiB分割を監査する。単なるMAX_IMAGE_SIZE置換では閉じない。

8MBのピークは各段の同時生存区間の和で証明する。圧縮画像1MiBと同梱1MiBを持つ間にBB/SQLiteを先取りしない。展開完了後に集積を返し、同梱owner移譲・余白返却後にPOST_ROOTを開始する。終了時の空きだけを測ってピーク合格にしない。

## 7. 実装単位と試験台帳

### 7-1. 半日〜1日の受入単位

全35行は独立に差分を検査・受入できる0.5〜1日 [推] の単位で、丸数字が観測する条件 (最大8項、10項以下)。T4b-4a/4bはT4b-4へ、T4b-5/6はT4b-5へ統合し、残る番号は引継ぎ参照のため維持する。同一段内でも先行行の成果物を使う。未接続の核はhost試験で単独受入し、公開切替はT4c/T5aの統合ゲートを通す。工数超過/10項超なら依頼前にさらに割る。§9は決定済み。測定ゲートは各担当行で確認する。

| 小段 (順) | 変更するもの・独立成果 | 観測できる受入条件 / 対応する変異 |
|---|---|---|
| T4a-1 (0.5日) | module_imageとmkmodの共通schema/ID別policy | ①既存kdebug正常像が通る ②旧ops/未知版/ID混同/表重複をinit前拒否 ③検証前後で入力・out不変 (成功時outだけ更新)。版/範囲検査削除変異は失敗 S4の型/slot/ABI・pair・ops2/18口を保持 (§2-1)。mkmod世代は必須引数またはsdk/kapi.jsonから取得。監査/配布変更はT4c以降へ引渡す。 |
| T4a-2 (1日) | VA0 SQLite prototype・import生成・reloc量測定 | ①未解決/外部data/不支持relocを生成拒否 ②非整列正常relocは成功、末尾−2/BSS/重複/import交差は書込0 ③base加算overflow拒否 ④実画像/body/prefix/件数と上限を確定。境界緩和変異を検出。SQLite専用importを別生成し、KDEBUG S4正常像を対照にする (§2-1)。 |
| T4a-3 (1日) | MEMSYS5 hook/タグ方式のhost prototype | ①HEAP→GETMALLOC→MALLOCの実allocatorを通る ②realloc失敗で旧block内容/勘定不変 ③別文脈freeと表満杯で漏れ0 ④heap+タグ総量≤512KiB。タグ/丸め勘定削除変異を検出 |
| T4a-4 (1日) | 最大負荷S/M/Lの取り分測定fixture | ①app sort/prepareで上限到達後も各辞書1語変換/学習が通る ②全辞書切替で最大一時量を記録 ③予約超えは旧辞書保持＋再起動要求 ④共通予約・APP上限・最大連続要求を数値化。APP割当門削除で対照が崩れる。USER mallocとMEMSYS5を分け、既存sbrk/exec_heap採取を使う (§3-3)。 |
| T4a-5 (1日) | module record・load/start/abort共通核 | ①全確保点失敗でowner/page/FDが開始前へ ②init完了前はexport未公開 ③prefix返却後もexport/dependencyが有効 ④満杯/二重abortで二重freeなし ⑤未停止参照は隔離。先行公開/無条件free変異を検出。KDEBUG OFFはload/owner0、ONはRUNNING・自己試験実行を確認 (`kernel/kernel.c:594,669,672`)。 |
| T4b-1 (0.5日) | B3のcopy核・3入口の拒否時不変契約 (§9 決定済み) | ①1023B+NUL成功、1024B+NUL/未終端/guard跨ぎ失敗 ②copy拒否でengine進入0・ファイル不変 ③出力はleave後。finalize先行/切捨て変異を検出。stmt/slot/SHMは拒否時不変に固定し、3入口の拒否→stepと資源回収を対照試験する。核だけの合格を公開接続の受入にしない |
| T4b-2 (1日) | engine guard・sqlite_stack.asm・例外/STOP接続 | ①全outerのESPが128KiB stack内で戻りregister/IF一致 ②再入/IRQ/IF=0 I/Oは副作用前拒否 ③engine中faultはfail-stop、app-kill/cleanup0 ④STOPはleave後に1回 ⑤通常VFS callbackのactive連続。guard迂回/早期STOP変異を検出。EXECNESTのcaller/TSS.ESP0保持とCPL0のESP切替を区別 (§3-1)。 |
| T4b-3 (1日) | F2 group/lease・KAPI/IME/boot利用者 | ①RESIDENT2slotをsettings返却→FEPで再利用 ②所属無しopen拒否 ③遅延journalは同group ④別DB親子/同ownerのT01/T02/T07/T12で子だけ回収 ⑤close失敗は隔離・再利用なし。現ownerへの誤帰属変異を検出 |
| T4b-4 (1日) | VFS FD identity・MAIN_DB重複門・F3a/c | ①相対/絶対/別名の同一ext2 inodeを拒否 ②parked/親子/同owner/隔離済みも拒否 ③CREATEは新FDのみcloseし既存内容不変 ④別DB/:memory:成功 ⑤truncate/sync/delete/access失敗を各SQLite rcへ伝播 ⑥pOutFlagsは実open結果 ⑦RO write/truncate拒否 ⑧FAT/HostDrv/LAN版host MAIN_DBは作成前拒否。path比較置換/登録早期解除/成功固定/RO門削除変異を検出。FDWP-1をRO全体の受入とせず、F3 rc伝播を確認 (§3-2)。 |
| T4b-4c (1日) | 不変CDのRO VFS経路 | ①FEP＋appのRO重複とSHARED lock成功 ②RW/CREATE/lock昇格を拒否 ③xAccess実有無/失敗が一致 ④hot journal/WALでopen拒否・write0 ⑤媒体generation変化はIOERR。ROだけで不変扱いする変異を検出。FDWP-1とは別に不変CD条件を確認 (§3-2)。 |
| T4b-5 (1日) | X-11全12口の契約表・共有サービス回帰 | ①12口の主体・入力・返値・状態変化・出力寿命・取消を表に明記 (IME9口は入力/モード/辞書管理に分類) ②CUI/WM通常配送で入力1回消費 ③parent/child/park-resumeで候補・辞書・出力の寿命が表と一致 ④無効mode/variantで状態不変 ⑤exec結果の「全体の直前1件」を親子切替で観測 ⑥gui_callの実owner/内部op拒否・正規WM配送を観測 ⑦con_sinkの先着/別reader EXIST/終了時返却を観測。二重配送/無効値書込/owner差替/reader早期解放変異を検出。子正常/fault/STOP→親→次KAPIのesp0/callerとUSER別勘定も確認 (§3-1/3-3)。 |
| T4b-7 (1日) | E11-A2b raw KAPIゲストfixture | ①kbd/IME/serial無入力待ち・FM長音の各CUI/GUIでSTOP→shell/WM復帰 ②通常完了も成功 ③内部ログ/SQLite中は中断0 ④owner回収1回・Key-OFF・候補取消・池復帰。取消削除/内部ログに取消混入の変異を検出。親復帰後のesp0/caller・次KAPIとUSER別勘定を確認 (§3-1/3-3)。 |
| T4b-8 (1日) | X-1残る文字列・kprintfの棚卸し/接続 | ①全公開NUL入力/可変引数の担当が確定 ②format/%s/引数窓の端をB1で処理 ③ページ末尾NUL成功、次ページ欠落/未終端は副作用前失敗 ④通常表示同一。生ポインタ読取へ戻す変異を検出。量が多ければAPI群別に分割 |
| T4c-1 (1日) | SQLite module build・typed facade・全stub | ①旧連結sqlite section/addr参照と固定stack依存が消える ②全db API/診断がstubかtyped exportへ ③通常db_test/db_mem_used成功 ④旧新混在はinit0で拒否。直呼び残存/NULL stub変異を検出。/bootへ配布しhsync bootで対更新 (§5-4)。resident終端・stack・KHEAP算出と監査を切替 (§7-2)。依頼に `rg -l "__sqlite_\|SQLITE_STACK\|__kernel_resident_end"` の全参照一覧 (paging/kentry/isr・監査・tools/testsも含む) を添える。VK32をcore1本へ変更、Unicode順序・配布世代門も同期 (§3-4/5-4)。 加えて `make kernel` がカーネルの像と対の部品 (kdebug.bin・SQLite の部品、T5 以降は起動時に読む部品) を全部組むこと (部品の一覧は `build/kernel.mk` の 1 つの変数、ユーザー決定 2026-10-18)。 |
| T4c-2 (1日) | services_public・settings→FEP transaction | ①公開前db/imeはstub、全成功で同時公開 ②各init確保/設定close/FEP CRC/辞書欠落/予約不足を注入 ③戻せた失敗はSQLite owner占有0・MINIMAL ④close/shutdown失敗は隔離byteとMINIMAL_QUARANTINEDを診断。早期公開/隔離free変異を検出。Unicode7頁を保持し欠落等はかなのみ継続 (§3-4)、配布世代門を回帰 (§5-4)。監査diff0 (§7-2)。起動tickを前後比較 (NP21/W参考値、現591〜595 / 544〜558、合否に使わない)。 |
| T4c-3 (1日) | CD RO FEP・標準構成の回帰 | ①CD rootのhost fixtureで1語変換 (実起動はT5b-5) ②dict_user有無とも読取成功 ③学習journal0/更新ROFS/list・export読取維持 ④app圧迫下FEP成功 ⑤ext2標準RW学習も保持。ROでCREATE/学習prepareする変異を検出 |
| T5a-1 (1日) | FEP B2・staging/出力境界 | ①各256B/64件境界・NULL契約一致 ②不正入力engine進入0/output不変 ③finalize/reset/binding終了→leave→copyout順 ④late出力拒否後もbusy解除 ⑤export不正pathでcreate/truncate0。寿命逆転/失敗kanjiをNULL化する変異を検出 |
| T5a-2 (1日) | fep.modへの分離・依存・stub | ①FEPのcore直リンクなし ②SQLite→FEP init、逆順rollback ③RESIDENTと学習journalを子終了で失わない ④close失敗でreopen拒否 ⑤MINIMALのGUI textbox/CUI editで英数編集 (生キーあり/無入力の両方)。stub常時−1/依存先先free変異を検出。/boot/fep.modをhsync bootの対更新へ含め、上位票の/sys記述も同期 (§5-4)。Unicode順序/7頁保持・配布世代門を回帰。 |
| T5b-1 (1日) | VK32/bootinfo生成・C/ASM共通wire | ①4entry/212Bを同じoffsetでdecode ②entry_count5/旧形式/不一致CRC拒否、最大定数6はASSERT失敗 ③同梱mem_size=load_spanでBSS/prefix不重複 ④EARLY生成にpair CRC自己参照なし。rawだけの占有検査へ戻す変異を検出。core1本から媒体閉包2〜4本へ。hsyncの旧/新像読取・boot_image_info利用者と配布世代門を同期 (§5-4)。 |
| T5b-2 (1日) | U19初期化依存・PG/台帳/DMAを前へ | ①初回物理割当前にbundle全域予約 ②root driver前にDMA成立 ③HDD root成功 ④kdebug ON/OFFとkaudit回数維持 ⑤GUI=0で識別/予約まで、probe0。予約遅延/起動順逆転変異を検出。案5配置・監査diff0 (§0/7-2)、Unicode順序 (§3-4)、h4の依存と実行時期 (§6)、配布世代門 (§5-4) を依存表に含める。 |
| T5b-3 (1日) | EARLY所有移譲・余白返却 | ①container非整列/BSS reloc/隣接破損をinit0で拒否 ②未マウントVFS/POST_ROOT import禁止 ③bootinfo capture後だけ旧領域返却 ④同一ページ二重owner/二重移譲0 ⑤全entry参照終了後に余白だけ返す。早期返却変異を検出。bundleのother勘定・prefix返却・隔離まで監査diff0 (§7-2)。 |
| T5b-4 (1日) | FAT切出し・FD起動 | ①core+FATの2entryでroot/shell成功 ②FAT失敗は停止 ③MINIMAL全db NOSYS・英数編集成功 ④install --recover-settingsは復旧済みと誤表示しない。閉包欠落/偽成功変異を検出。書込み禁止FDのroot/Unicode/shellを回帰 (§3-2)。h4は依存成立後 (§6)、hsync新旧像・配布世代門も回帰 (§5-4)。 |
| T5b-5 (1日) | ATAPI/iso9660切出し・CD起動 | ①3entry (HostDrv併用4) でroot成立 ②ATAPI失敗でiso登録0 ③BSS込み占有とDMA停止/返却一致 ④CDのRO FEP回帰。依存順逆転/停止未確認free変異を検出。部品・対kdebug・Unicode配布と同/異世代更新・別root復旧を確認 (§5-4)。 |
| T5b-6 (1日) | HostDrv EARLY・補助マウント | ①通常 NHD ext2 root の DB で変換→学習→再起動保持 ②HostDrv を root にせず既定 DB 配置を維持 ③実機専用I/O0・同ID二重load0。既存 DB を変更する移行処理は追加しない。h4往復/owner回収 (§6) と配布世代門 (§5-4) を回帰。 |
| T5c-0 (1日) | IRQ/tick/callback/DMA owner broker | ①登録直後失敗でも参照0を証明して回収 ②STARTINGの必要なIRQは有効 ③tick残存/DMA停止不能は依存閉包隔離 ④DEVICE予約不変。unregisterだけでfreeする変異を検出 |
| T5c-1 (0.5日) | KCG ROM移設 | ①typed facadeで同じ文字 ②欠落stub診断 ③旧font経路と排他 ④キャッシュ/owner予算一致。誤export/二重初期化変異を検出。Unicode探索はcore、低位scratchはT7aまでFIXED。font close→BB/mailbox→USER順 (§6)。 |
| T5c-2a (1日) | FM+snd移設 | ①通常再生/STOP成功 ②tick登録後失敗で呼出先残存0 ③Key-OFF確認後回収、不能時隔離。tick解除欠落変異を検出 |
| T5c-2b (1日) | PCM移設 | ①再生/停止成功 ②IRQ登録後失敗とDMA停止不能を区別 ③64KiB境界内・停止不能リング保持 ④永久予約不変。DMA証拠省略変異を検出。依頼前にPCM importのcore facade維持か自己試験移設を決め、S4本数/ABI印を更新 (§2-1)。案5 DMA期待 (§0) とh4接続を確認し、transfer=noneを実転送証拠にしない (§6)。 |
| T5c-3 (1日) | V86移設 | ①CUIのentry/exit往復 ②低位PTE復帰 ③例外復帰コードが回収対象にならない ④セッション中free0。復帰前owner retire変異を検出。依頼前にV86 importの接続とS4本数/ABI印を決定 (§2-1)。caller/esp0保持 (§3-1)、低位FIXEDの非回収 (§6) を確認。 |
| T5c-4 (1日) | LAN/82557移設・SFS接続 | ①LB受入済みTX/RX/IRQ回帰 ②PCI DECLINEなら参照0、QUARANTINEなら再bind/free0 ③LAN版/host通信中の取消/断線で有限復帰 ④session/NIC callbackを断つまで保持。IRQ/tick片方のみ解除変異を検出。現TX/RX/IRQ・LAN同期の移設回帰。h4/core facadeを維持 (§6)。82557はhost試験＋LGY-98経路、実機分はT4T7-RHへ送り受入を止めない。 |
| T5c-5a (0.5日) | HostDrv POST_ROOT・np2sysp/mouse | ①検出前専用I/O0 ②EARLY済み同ID再load0 ③通常mouse/host利用成功 ④検出失敗はstub。重複load/検出門削除変異を検出。HostDrv成立後にh4を実行しSKIPを防ぐ (§6)。 |
| T5c-5b (1日) | Cirrus+WAB移設・core識別glue | ①GUI=0識別/予約、enable0 ②GUI境界でだけprobe/enable ③失敗でもPEGC等の永久予約維持 ④全共通PDEが最初のAS前に準備済み。識別/enable混合変異を検出。案5のPT枠 (§0) とPEGC最上位窓予約を維持。 |
| T5c-6 (1日) | 起動policy/manifest・splashアプリ | ①欠落/循環/重複/未知IDは公開前失敗 ②必須削除でもSQLite/FEPを省略扱いにしない ③splash lease返却→rshell/SerialFS→shellの順 ④表示失敗でもshellへ。必須判定/lease返却欠落変異を検出。splash後のcaller/esp0保持 (§3-1)、font close→BB/mailbox→USER順と低位FIXED非回収 (§6) を確認。 |
| T6b-1 (1日) | HDD/FD集積移動・全区間検査 | ①508KiB超〜1MiB成功/1MiB+1拒否 ②loader/stack/source/core/bundle/bootinfo重複で展開write0 ③FD64KiB分割/segment wrap境界が一致 ④C/ASM同fixture同理由。最終entry検査前展開変異を検出。hsync読取・boot_image_info利用者も新像に追随 (§5-4)。 |
| T6b-2 (0.5日) | 生存区間/ピーク計測・最終構成台本 | ①展開・root・SQLite/FEP公開・splash各時点でowner/PFN和一致 ②集積返却後にBB/SQLite、別staging0 ③64MB通常/故障対照 ④8MB planar/PEGC・17MB・Ra266は最終段の未実施欄へ引継ぐ。返却漏れは計数不一致になる。案5配置・USER別勘定 (§0/3-3)、bundle/prefix/隔離を含む監査diff0 (§7-2)。実機分はT4T7-RHへ送り受入を止めない。 |

### 7-2. 像・実行時RAMの見積もりと止める条件

現 `kaudit_boot_pages` はkdebugだけを特別控除する (`kernel/kaudit_boot.c:14`、`kernel/memory_boot.c:391`)。SQLiteをFIXED鎖からowner割当へ移すT4cで、固定鎖減少とpool/SLACK供給増を更新する。module使用頁はその内数として観測し、独立欄に出すなら元分類から控除する。bundleのother勘定・prefix返却・隔離残頁まで重複なし、総和diff0を要求する。既存起動監査の回数を変えず、公開/rollback/集積返却は別観測点とする。

| 範囲 | core像の増減見積もり (未実装、[推]) | core外・測定ゲート |
|---|---:|---|
| T4a 共通核/生成policy | +4〜8KiB (DBGREF分を二重計上しない) | prototypeでprefix/body/record容量を確定。kdebug ONは既存body48KiB・peak60KiB、OFFは0 |
| T4b facade/F2/F3/X群 | +4〜10KiB | group/identity/出力stagingを含むmap差分。X-1の棚卸しで上限が変わるなら着手前に再見積もり |
| T4c SQLite分離 | +1〜3KiBのcore接続、連結SQLite占有は別計上 | 現連結SQLite全占有770,560B (`build/out/kernel.map:4477,8033`)、うちstatic MEMSYS5 393,216Bを除く377,344Bがbody基準。ページ切上げ380,928B＋512KiB＋128KiB＝**1,036,288B定常 [推]**、peakはこれに実測prefixを加える |
| T5a FEP分離 | 旧FEP約18.5KiBを外へ、B2 staging等+5〜7KiB [推] | module本体/状態・ページ丸めを新mapで計上。旧群値は `docs/tasks/v3/TASK_MEMMAP_V3.md:322`、現実測の節約量ではない |
| T5b EARLY/bootinfo | 接続/検査+4〜8KiB、FS群を外へ移した差分を別記 | 同梱域1MiB内で全load_spanの和≤1MiB。212B wire＋保存配列もcore像へ。旧FS群38.1KiB (`docs/tasks/v3/TASK_MEMMAP_V3.md:324`) は参考 |
| T5c 各driver/manifest | broker/一覧+3〜6KiBから実際に除いた群を差引く | NIC・PCMのDMAは固定DMA pool、module RAMと二重計上しない。群別の新mapとprefix/常駐/隔離を各段で採る |
| T6b | kernelの追加は計数/glue≤1KiB [推]、loader増分は別枠 | 圧縮源≤1MiB、同梱≤1MiB、定常から消える時点を採取。8MBの上位概算余裕を現在の合格値に転用しない |

§0のT3受入値・案5配置を基準に、T4開始時に成果物の `__bss_end`、実定数、core text/data/bss、圧縮長、SLACK、free/max連続pagesを基準化する。各段は増減を同じ構成・同じフラグで比較し、既存像の残りを越す/SLACKの返却量を侵す/必要連続量を確保できない場合は後続へ進めず段を縮めて再計測する。coreの増分は§7-2各行の上端も暫定見積ゲートとし、超過時は理由・新mapを提示して枠を見直す。KHEAP/SHMを削る、FEPを省略する、MEMSYS5を増減することで帳尻を合わせない。上の見積もりは承認済み予算ではなく、正負の未実測値を相殺して合格にも使わない。
SQLiteのpool/stackはbodyから外してowner付き連続割当とし、BSSの旧384KiBを残して512KiBを追加する二重確保をmapで検出する。FEPの4,352B stagingとタグ表はそれぞれ置いた領域で1回だけ計上する。KDEBUG=1受入ではそのowner/bodyもピーク・定常へ加え、隔離は空きに含めない。共通ログはbuild ID (SHA)・構成・小段・成否/未実施を持ち、詳細はその段の判定に必要なものだけ採る。形式/ロード段は世代・画像CRC・phase/状態、メモリ段はowner/PFN・prefix/body/heap/stack・最大連続空き、engine段はIF/深さ・例外分類、driver段はcallback/DMA参照を記録する。失敗時は初回理由・残存/隔離量を該当範囲で残す。

> 2026-10-14 ユーザー決定: 搭載メモリ量ごとの確認は v3 の最終段にまとめる (DEFERRED_TESTS の関門「v3 の最終段」)。それまでのゲスト受入は 64MB の 1 構成。以下の構成一覧は最終段用として残す。

各段はkernel＋直接試験＋lint、Tx最後はall＋全差分候補 (MUTATE=0)＋静的5本、PM節目は `make -k check` とする (`docs/tasks/agents/ROLES.md:17`)。ABI段は [ABI1]〜[ABI3] に従う。形式/loaderは既存 `check-module-image-host`・`check-kdebug-init-host`、wire/展開は `check-bootinfo-host`・`check-vk32-crc-host`、LANは `check-lanfs-host` を拡張する (各 `build/checks.d/` の同名mk)。正常対照がPASSしてから対応変異が観測でFAILすることを記録し、意図した生成拒否/ASSERT拒否の試験を除き、コンパイル失敗/timeoutを検出成功に数えない。DBGREF自己試験の受入はKDEBUG=1・pair CRC一致・RUNNINGが前提、OFFはopen0/owner0とkaudit継続を別に確認 (`docs/tasks/v3/TASK_DBGREF.md:104,110,112`)。外部apps/gameは現行の対象外決定を維持し、再開時の全再ビルドを引継ぎゲートへ残す。故障注入は別の使い捨て媒体で行い、本番辞書へ書かない。通常受入は64MB NP21/W。8MB planar/PEGC・17MBは最終段へ、Ra266等の実機分は状態行どおりT4T7-RHへ送り段の受入を止めない。FDの受入は上位票§7へ紐付け、MINIMAL落ちは標準成功とは別結果で記録する。ログは上記の共通項目と該当段の観測値に絞り、失敗・未実施構成を省略しない [V4]。本改訂は文書照合のみで、ビルド・試験・配備は未実施。

## 8. レビュー経過

元設計はOpus 5.5の独立レビュー3往復でApprove。r2/r3は2026-10-16に再照合し、独立レビュー2往復後に§9の方針を決定した。過去のApproveは本票の実装・受入を意味しない。
r4材料は `RECHECK_r4.md` と `opus_r1.md` (Approve、PMはF1〜F6採用)。設計本文・担当・受入条件は本票に保持する。

## 9. 決定済みの争点

| 論点 | 決定・根拠 | 反映・確認する段 |
|---|---|---|
| **決定 (ユーザー 2026-10-16)**: 3 SQL入口の拒否時のstmt/slotとfinalize時期 (F1/F2) | 古い行が返るのは、戻り値を見ずに `db_step` を呼んだアプリ自身の文だけで、シングルタスクでは他のアプリや OS のデータに及ばない — **アプリ側の書き方の問題**として扱う (ユーザー)。第三案 (保留 stmt) は採らない。拒否時は「何も変えない」(`docs/tasks/settings/FEP_BOUNDARY.md:140,141,213`) にそろえ、`docs/KAPI_SPEC.md:1396` の prepare_only の入口 finalize は「受け付けた入力のときだけ」と書き直す。KAPI_SPEC に「拒否の戻り値を見ずに step すると前の文の続きが返る」と注記 | 本票は反映済み。KAPI_SPEC の説明同期は T4b-1 の前に行う |
| **決定 (ユーザー 2026-10-16)**: X-11の共有サービスの呼出し主体 | 端末ごと・本人ごとの**新しい権限の分離は足さない** (単一利用者)。T4b-5 で通常の親子・park/resume の順を観測し、二重消費・別 owner への副作用・寿命の不具合が出たら、そこだけ直す。攻撃者を置かないことを理由に未観測の不具合を閉じない | T4b-5 の契約表と通常順の反例で照合 |
| **決定 (ユーザー 2026-10-16)**: HostDrv rootの管理DB配置 | **HostDrv を root にしない**。管理 DB は通常の root (NHD の ext2) に置く。§3-5 の `/persist` ext2 案と、HostDrv root のための移行手順は不要 | §3-5 と T5b-6 に反映済み。実装受入は未実施 |

FEP予約量・module別サイズ上限・U19可否・8MBピークは、対立した決定ではなく§7の測定ゲート。未測定の数字を埋めて承認済みにしない。今回、上位D番号の追加・上位本文の変更は行わない。契約変更が必要になった場合だけ個別に判断する。
