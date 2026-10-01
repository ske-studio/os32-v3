# TASK_T4_T6_MODULES — SQLite・FEP・起動モジュールの詳細設計

> 状態: **設計中 (2026-10-01)** — 実装用の設計。独立レビュー Opus 5.5 (`claude-opus-5-5`) 往復3でApprove。実装・受入は未実施。
> 設計: Codex gpt-6-astra。調査基点 `59c4285bbacf36e829e7480d741bbabac95be191`。
> 上位契約: [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §3-5・§4・§6・§7。前提: [T2](TASK_T2_APPBAND.md) 完了 → [T3](TASK_T3_LAYOUT.md)。この票は形式、状態、移行と失敗処理の実装契約を補う。D番号の決定を変更しない。

## 1. 現在との差と共通境界

基点では `build/os32.ld` と `build/kernel.mk` が SQLite を別セクション・固定宛先へ出力し、`kernel/kernel.c` が直接初期化する。`kernel/shlib.c` はモジュール再配置器ではない。`boot/vk32_boot.c` は複数エントリを検査するが、占有は raw_size、集積との非重複は低位集積を前提にする。`kernel_main` のルートマウントは `paging_init` / `memory_boot_init` より先。これらを変更済みとみなさない。

新規ファイル名は実装案: `kernel/module.c/.h` (状態と公開)、`kernel/module_image.c/.h` (副作用のない検証)、`tools/mkmod.py`、`build/module.ld`、`kernel/module_imports.def` (import の生成元)。モジュールの ABI は **内部 ABI** で、公開 KAPI と同じ表へ混ぜない。世代識別の値は T2/P7 の正典から取得する。

## 2. モジュール形式と呼出し契約

### 2-1. バイト形式案

little-endian u32 の明示的 decode を使い、C 構造体の cast/sizeof でファイルを読まない。ヘッダの項目順は実装開始時に一つのスキーマへ固定し、C/ASM/Python を生成・照合する。

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

prefixを処理後に返す場合、exportの解決済み関数表・dependency/owner/generation等の継続利用情報を先に固定容量のmodule recordへコピーする。保存容量不足はinit前に拒否。prefixを指す文字列/表ポインタをRUNNING状態へ持ち越さない。BSSゼロ→reloc→importをbodyだけへ適用した後、prefixページを返せる。成功/隔離したbodyページは保持する。

**POST_ROOTの画像読込も本体へ直接行い、別の全画像stagingを取らない。** 小さな固定header bufferへ読み、既知形式・完全長上限・load_spanの算術・許可module IDを検査 → owner付きload_span連続割当 → prefix+payloadをその割当へ短読検査付きで読む → 全画像CRC/範囲/表を検証する。検証中にファイルを再読してrelocを書き換えず、確保した写しだけを参照する。全体検証失敗はinitを一度も呼ばず割当を返す。この領域はR4で許すmodule本体 (prefixを含む) であり、別の400KiB連続scratchではない。EARLYは同じ形式の同梱域を直接検証する。ピークにはprefix+bodyを全量計上し、prefix解放後を定常量と区別する。

### 2-2. 状態と公開

`ABSENT → HEADER_CHECKED → OWNED_IMAGE → VALIDATED → RELOCATED → STARTING → RUNNING`。
エラーは公開前なら `ROLLING_BACK → ABSENT`、参照を断てなければ `QUARANTINED` または起動停止。SERVICES_READY後の unload/reload は v3 のこの票に入れない。boot transaction内のRUNNING依存先は、利用者未公開である限り後述の逆順rollback対象に含む。

内部 API 案:

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

**B3:** T2 bounded copyでpath/SQLをkernelへ完了してから旧stmt finalize。SQLは既存1024Bバッファ (payload1023B) にNULまで入らなければ失敗し、切捨てない。不正入力時はSQLite進入0、既存stmt維持。copyoutはengineを出てから行う。

### 3-2. F2/F3 と重複接続の移行規則

[F2_OWNERSHIP](../settings/F2_OWNERSHIP.md) のgroup/leaseを唯一のFD所有モデルにする。`os32_sqlite_group_open` を全openへ適用し、default VFSの所属無しopenはfail-closed。RESIDENT groupは **2slot、1group=1接続=1回open**。bootではsettings/selftestが1slotを順次acquire→open→close→finishして返し、両slotがFREEになってからFEPを開始する。DICT_META前はFEPが1slot、後は辞書RO+学習RWの2slotを同時に使う。settingsはFEPのslotを借りたまま保持しない。settings close失敗ならそのslotをQUARANTINEDにし、残slotで標準起動を強行せずboot transactionを失敗させる。後発journalも同group。close/finalize失敗はquarantineへ残し、同じownerを正常retireしない。`exec_cleanup_owned_resources` はアプリgroupだけを処理しFEP/親groupに触れない。

F3a/c: xTruncate/xSyncのI/O rcを `SQLITE_IOERR_TRUNCATE` / `SQLITE_IOERR_FSYNC` へ翻訳、xOpenのpOutFlagsを実open結果から返し、RO接続のwriteはVFS側でも拒否する。SQLite lock表は作らない (D29)。

**F3b前の暫定重複禁止案:** 新しいlock表は持たず、既存live DB/groupレコードを走査して同じファイルの2本目の接続をmode/ownerによらず拒否する。parked・親子・RESIDENT・close失敗quarantineを含む。更新可能なFEP DBへのアプリ直openも拒否し、学習はFEP facadeからだけ行う。不変CDのREADONLY例外ではFEPとアプリの同じDBへのread-only重複openを許す (pathによる特例拒否は設けない)。`:memory:` は別接続ごとの実体なので対象外。判定は文字列pathでなく **MAIN_DBのxOpenで取得したFD** のidentity `(mount_generation, fs_file_id)`。CREATEで初めて作るファイルも、破壊的truncateを伴わないopen後にinodeを取得→既存group走査→groupへ登録、の順にする。重複なら新FDだけcloseしてSQLiteへopen失敗を返し、既存接続とファイル内容を変えない。相対/絶対/別名も同一とみなす。identity取得→走査→group登録は通常文脈の非再入区間、失敗/close完了でのみ解除する。

現行で安定inodeが取れるのはext2なので、**更新可能なfile-backed SQLiteはext2に限定**する。別に **不変の配布CD/iso9660上のDBをREADONLYだけ許す**。後者はguest側のRO mountだけで判定せず、boot policyが指定した不変配布媒体であること、媒体generationがopenからcloseまで不変、配布DBがclean close済みでjournal/WAL無し、を条件とする。媒体交換/読込不能はIOERRで失敗し、旧接続のまま新媒体を読まない。イメージを使う検証でも実行中にホストから媒体ファイルを書換えない。不変ROでは重複readerを許してよいが、CREATE/RW/journal/write/truncateはVFSでも拒否する。この接続のSHARED lock/unlockは不変媒体generationが有効なら成功するno-opとし、書込み用lock昇格は拒否する。xAccessは要求modeに応じて実際の有無/読取可否を書き、I/O失敗はエラーを返す (全て存在/成功にしない)。hot journal/WALが存在すれば不変配布DBの条件違反としてopenを拒否し、回復書込みを試みない。

**HostDrvはguest側ROでも外部writerがいるのでこの例外に含めない。** FAT/HostDrvの直接MAIN_DB openはFS能力判定でファイル作成前に非対応を返す。`:memory:` は維持する。root媒体とDB置場の接続、CDのFEPとHostDrv標準構成は§3-5に定める。影響を受ける `db_*`、libos32cfgの任意path、db_test、installerはこの可否をエラーとして扱う。unsupported backendへpath比較の弱い代替を設けない。

これは排他openの汎用サービスの前倒しではなく、T4のSQLite入口で危険な重複を断る暫定制約。必要なVFS identityが現行で不足する場合はT4bで内部APIとして足す。既存ユーザーの同一DB複数open依存を調査し、必要なら接続共有へ移す。F2 T01は親/子/同ownerに**別のDBファイル**を使って所有回収を試験し、同じファイルのケースは2本目拒否の別試験へ。T12はFEP初回openをbootで済ませ、子実行中に初めて学習journalを開くケースへ読み替える。意味変更を隠してpath比較だけで合格にしない。

### 3-3. FEP予算と設定の循環

D27の「512KiBの単一MEMSYS5」は維持する。`cache_size`やsoft limitだけを予約保証と呼ばない。起動はSQLite既定値 → settingsを短命RESIDENT接続で読みclose → 検査済み `mem_reserve_kb` を決定 → FEP辞書/stmtの優先初期化 → アプリopen許可。DICT_METAまでは辞書metaを読まない。settingsが読めなければ既定値で続行し、FEPのinitが成立しなければD26のMINIMALへ。

実装案は既存MEMSYS5の外側に**割当許可の計数ラッパ**を置くこと。`SQLITE_CONFIG_HEAP`でMEMSYS5を設定 → `SQLITE_CONFIG_GETMALLOC`でそのallocatorを保存 → `SQLITE_CONFIG_MALLOC`でラッパ登録 → initialize、の順にする (`sqlite3.c` のHEAP処理はallocator表自体を置換する)。別ヒープは作らない。engineの呼出し文脈からFEP/APP/SYSTEMを識別し、app割当は将来FEPに残す量を割り込むとNULL。free/reallocはallocation側のタグ/実サイズで戻す (free実行時ownerを使わない)。共有engineメモリはSYSTEM勘定とし、取り分計算へ含める。ラッパのメタデータ分も512KiBの予算内で数える。

ただし総空きだけではMEMSYS5の断片化を保証できない。T4aの最小prototypeで、使用SQLite版のallocator hooks、丸め・realloc失敗時旧ブロック維持、FEP検索/学習の最大一時割当、appのsort/prepare連打後の1語変換を検証する。保証が成立しないなら予約量/アプリ上限を調整して再検証し、別ヒープや512KiB増量は本票で承認済みにしない。タグはpayload前置きでMEMSYS5の2冪丸めを増やす案だけで決めず、固定容量の外部タグ表+元allocatorのxSize利用と比較する。外部表のbyte数も512KiB予算に含める (heapへ渡す部分と表の和が512KiB)、表満杯なら割当を副作用前に拒否。S/M/Lの **最大負荷辞書** と全切替経路で同じ予約値を検証する。DICT_META前は個別metaが無いためS/M/L全部に通る共通予約量を既定とし、未検証の任意辞書へのlive切替は拒否する。settingsで量を変えるときは再起動し再評価する。既定予約量の数値はこの測定ゲートで決める。

辞書切替が起動時予約を超える場合は設定だけ保存し再起動要求、現在の辞書を保つ。アプリopen上限、割当拒否、FEP使用量を診断し、FEPを任意省略してメモリ試験を通さない。

### 3-4. 必須サービスを一括公開するboot transaction

`SQLITE_INTERNAL_READY → SETTINGS_CLOSED → FEP_INTERNAL_READY → SERVICES_READY`。途中は一般のdb/ime KAPIがstubのまま、信頼するboot coordinatorとFEPだけが内部SQLite exportを使う。settingsはengine_enter経由、予約評価もこの段で行う。FEP正常起動とgroup/予約確認が揃って初めてservices_publicを一度に開く。最初のユーザーASはこの後なので、半公開をアプリが見ることはない。

FEP画像CRC不一致・辞書なし・予約不足等は利用者へ未公開の失敗として、FEPの部分stmt/groupをclose/finalize → settingsを含むRESIDENT groupゼロを確認 → sqlite3_shutdown → 内部exportをstubへ → RUNNING/STARTINGからROLLING_BACK → FEP/SQLite ownerの回収、の順。init自身で失敗した場合も同じ段階印で一度だけ処理する。正常rollback後はSQLite/FEPの占有ゼロ、全dbはNOSYSのMINIMALへ入る。close/shutdown/装置停止の証明が失敗したらcode/heap/stack/groupを依存閉包ごとQUARANTINEDに残す。一般KAPIはstubだが **MINIMAL_QUARANTINED** と別状態で診断し、隔離byte数を空きから差し引く。これを標準MINIMALや8MB標準合格に数えない。安全な入力/回復継続を証明できなければ起動停止する。

### 3-5. CD/HostDrvルートでもFEPを必須に保つ

これは新しい実装契約案で、現行 `ime_dict_open` がREADWRITE固定であることを変更する。D21のFEP必須とD26の実失敗rollbackは維持し、rootのFS種別だけでMINIMALを選ばない。

- **CD標準構成:** 不変配布CDの既存combined `fep.db` をROで開いて変換を提供する。`ime_dict_open(mode)` にROを加え、CREATE TABLE/INDEXと学習stmtのprepareを行わず、`dict`と存在する場合の`dict_user`を読む検索stmtだけ作る。`dict_user`欠落なら辞書本体だけを検索する。学習は無操作、明示的な削除/clear等の更新APIはROFS、list/exportは読取として維持する。状態診断で「変換可・学習不可」を示す。これはFEP省略ではなく、DICT_METAの学習別file化も前倒ししない。settingsも不変CDをROで読み、更新はROFS。ext2へ学習を保存したい利用者は下の管理DB構成を選ぶ。
- **HostDrvルートの標準構成:** /sys等をHostDrvから読む一方、**管理下のext2データvolumeを別mountし、FEP/設定の既定DBはそちらを使うことを必須**にする。mount先案は `/persist`、辞書はその `db/fep.db`、設定は `etc/settings.db`。実際の名前はパス定数の正典へ登録し、HostDrv用配布profileでkernel FEP・boot settings・libos32cfg/installerの既定pathを一組に生成する。複数alias mountを作らず、同じvolumeは一つのmount identityで管理する。kernel boot coordinatorはroot成立後・SQLite内部openより前にext2 mountと期待volume/DBの存在を確認する。FEPはこのext2上のcombined DBをRWで開き、既存学習を保持する。
- **配備/不在時:** HostDrv上のlive DBをそのまま読むfallbackはない。初回のext2へのDB準備はユーザーデータを保った配布手順で行い、通常bootが既存学習を上書きしない。必要なら一時ファイルへコピーして固定した配布manifestのdigestを検証してから採用し、同時に変更されるHostDrv源を正しいsnapshotとみなさない。管理volume/辞書が無い場合は設定不備という実失敗としてD26のrollbackを行い、その起動をHostDrv標準受入の合格に数えない。

T4c/T5bの受入は、CD rootでFEP1語変換・学習journal生成ゼロ・更新ROFS、HostDrv root+ext2管理DBで1語変換→学習→再起動後保持、HostDrv源の外部変更が開いたDBへ影響しないこと、管理volume不在の明示的失敗を含む。HostDrv rootの配布profile/mount接続はT5b入口の依存ゲートで、実装済みともユーザー承認済みの新データ配置とも記録しない。実際の媒体を変更する際は既存配備の [D1]〜[D3] に従う。

## 4. T5a FEP 案 B

T4でfacade化したFEPを `fep.mod` へ移す。SQLite RUNNINGの依存を必須とし、未公開init中に辞書group/stmt/入力状態を全て準備してからexportを一括公開する。core側stubの意味は上位票 §5-6 のAPI別表をそのままテストoracleとする (trygetkeyは無入力−1、feed_keyは素通し)。MINIMALの英数編集を非ブロッキング/ブロッキング両経路で確認する。

B2 facadeは `ime_user_list/delete/export` の全入力を先にbounded copy → kernel staging → bind/step → stmt finalize/resetとbinding寿命終了 → engine_leave → copyout、の順。listは上限件数/文字列長で打ち切り状態を返す。deleteの不正kanjiをNULL条件に変換しない。exportはpath検証完了前にcreate/truncateしない。上限は [FEP_BOUNDARY §3](../settings/FEP_BOUNDARY.md) を引継ぎ、失敗時に返却バッファを部分公開しない。

`dict_fd_protect` を廃止しRESIDENT groupへ集約する。close失敗は記録してreopenを拒否する。SQLite→FEPの依存順を守り、FEP init失敗で標準起動を成功扱いにしない。MINIMALへ落とす際も停止確認済みのgroupだけ解放する。

## 5. T5b 早期ロードと同梱

### 5-1. ブート段階

`BOOTINFO_SAVED → MEMORY_READY → EARLY_MODULES_READY → ROOT_READY → SERVICES_READY → USER_READY`。

1. bootinfo/BDAを保存、RAM検出、必要なcoreコンソール/例外/heapを準備。
2. PG/台帳をルートマウント前へ移し、集積域・同梱域を**最初の物理割当より前に**予約。DMA初期化もroot driverより前へ。
3. 同梱EARLYモジュールを検証・in-place再配置・起動しFS/装置を登録。importはcoreと既にRUNNINGのEARLY依存だけ。VFSファイル読込やSQLite/settingsを使わない。
4. `/` マウント。ここから一覧を読める。短いsystem.cfgの読込、gfx識別→永久予約→写像→BB確保をlive AS=0で済ませる。GUI probe/enableは後。
5. SQLite→FEP、通常モジュール、exec/shlib準備。最初のAS以前に共通PDEの追加を終える。
6. スプラッシュアプリを実行・回収してからrshell/SerialFS対話とshellへ。gfxを使う場合は通常のGUI境界を経由する。

U19の移動可否はT5b入口でcore初期化の全依存を確認するゲート。移動不能と判明した場合のみ、上位票の代替「同梱固定予約」を具体化して再レビューし、未完成のpgallocへ逃げない。

### 5-2. 媒体別閉包と所有権

| 媒体 | 同梱依存の閉包 | 失敗 |
|---|---|---|
| HDD/ext2 | coreのIDE/ext2でroot、core1エントリ・同梱不要 | root未成立なら回復媒体へ。通常モジュール探索で循環させない |
| FD | core FDC + FAT EARLY、合計2エントリ | FAT失敗なら起動停止。SQLite/FEPなしMINIMALはroot成立後の回復構成 |
| CD | core + ATAPI EARLY + iso9660 EARLY =3、HostDrv併用でも追加1で計4。iso9660側のdiskio等を依存画像へ静的に含める | ATAPI未起動でiso9660登録しない |
| HostDrv | core + HostDrv EARLY =2。必要なnp2sys transportは同画像に含める。ext2管理DBの接続は§3-5 | 実機で専用I/Oへ触れない。rootとして使う構成ではEARLY依存を同梱 |

同梱域は1MiBの予約をまずboot ownerで保持し、各エントリを **VK32 entry.mem_size = load_span (prefix+body)** で配置する。BSS・整列を含め重複なし、同じページを2ownerに分けない。検証→再配置→initの順にmodule ownerへ占有ページを移譲し、**全エントリの処理と参照終了後に、余白ページだけ**返す。EARLYでも§2-1の継続情報コピーが済んだprefixページだけは余白として返せる。成功module/隔離moduleのbodyページは返さない。圧縮集積域は全展開・CRCとbootinfoへの結果コピーが終わった後に返す。

`mkvk32` は複数エントリと `raw_size/mem_size` を区別する形式へ更新する。C側HDDとASM側FDの検査は同じ壊れた画像fixtureで一致させる。許可destination種別をcore/bundleに限り、画像が宣言した任意住所へ書かない。旧形式は入口前に拒否する。

### 5-3. bootinfoとMINIMAL回復の境界

`include/bootinfo.h` の次形式に固定上限VK32_MAX_ENTRIESのentry配列を追加する。各wire entryは `{kind, module_id, container_phys, raw_size, load_span, payload_offset, body_mem_size, decoded_crc}` (各u32)、headerに `{wire_size, entry_count, layout_gen, image_crc, table_crc}` を持つ。既存drive/image情報64Bを保持し、追加header20B+entry32B×4で **212B**。MEM_BOOTINFO_SIZEは256Bなので `VK32_MAX_ENTRIES=4` を維持し、形式上の上限も `VK32_MAX_ENTRIES<=5` とASSERTする。形式versionは更新する。予約領域 `MEM_BOOTINFO_SIZE` 内へ収まるASSERT、C/ASM/Pythonのoffset照合を必須とし、収まらなければ黙って低位の隣へ伸ばさず新形式を再設計する。

loaderは全展開/CRC完了後にこの表を書き、最後に有効印をcommit。kernel最初のbootinfo captureが固定core配列へコピーし、count/CRC/世代・許可kind・containerのページ整列・範囲・非重複・`load_span=payload_offset+body_mem_size`を再検査する。module自身のヘッダ値とも照合してからEARLYを開始する。未知/欠損情報は推測で復元せず停止する。台帳予約はコピーした値だけを使い、集積域を返した後にloader内ポインタを参照しない。

MINIMALでSQLiteを使う `install --recover-settings` は動作しない (D21)。この制限をエラー文言と回復手順へ明示し、NOSYSを「DB破損」や「復旧済み」に変換しない。MINIMALは媒体/ファイルの退避・正しいシステム一式の復元を担い、DB修復はSQLite/FEPが正常な標準構成またはホスト側の既存手順で行う。MINIMALでSQL修復まで必要という要求はD21の変更判断へ送り、本票でSQLiteを密かに読み込まない。

## 6. T5c 外へ出す順序と T6b

| 順 | モジュール候補 | coreに残す接続 / 特別な受入 |
|---|---|---|
| 1 | KCG ROM | コンソールからのtyped facade、未搭載診断。T7a前は旧font経路との排他 |
| 2 | FM+snd、PCM | IRQ/tick/DMA broker。登録直後失敗・停止不能を必ず注入 |
| 3 | V86 | entry/exit facade。低位PTE切替と例外の復帰コードの寿命を証明、稼働中回収なし |
| 4 | LAN/82557 | core PCI識別/永久resource、driver bindのSTARTING/RUNNING/QUARANTINEとmodule状態を対応 |
| 5 | HostDrv / np2sysp+シームレスmouse / Cirrus+WAB | 検出前の専用I/Oを禁止。HostDrvはroot用EARLYなら同IDをPOST_ROOTで再ロードしない。Cirrusの識別glueだけcoreに残す |
| 6 | スプラッシュ | CPL=3アプリ、BB lease借用→終了解除。表示失敗で常駐shellを止めない |

gfxの副作用のない識別・resource記録はcoreに残す案とする。Trident enable/probe、合成器、追加NICはP4/P5の票でこのloaderへ載せる。USBをここで追加しない。起動一覧はmodule ID/phase/必須性/依存/許可パスを持つ配布manifestと照合し、任意ユーザーパスからのCPL=0ロード口を公開しない。必須集合と許可ID/phaseはcoreの生成policyで固定し、配布manifestはそのpolicyの任意モジュールの選択/期待hashを指定するだけ。root FSの閉包はboot媒体種別から決まり、manifestで省略不可。SQLite/FEPはMINIMALを除き必須で、manifestからの削除で省略扱いにしない。manifest欠落/破損/循環/重複/未知IDはユーザー未公開のboot失敗とし、root成立後は§3-4のrollbackを経てMINIMALへ、root閉包自体の失敗は停止する。

T6bは最後に集積を上位票の `[0x500000,0x600000)` へ移し上限1MiBにする。loader自身、stack、圧縮源、core展開先、同梱のmem_size占有、bootinfoを区間表へ載せ、**全宛先を検証するまで1バイトも展開しない**。source/destinationの重複も検査する。FDのreal-mode読込からPMコピーへの境界でsegment wrap/64KiB分割を監査する。単なるMAX_IMAGE_SIZE置換では閉じない。

8MBのピークは各段の同時生存区間の和で証明する。圧縮画像1MiBと同梱1MiBを持つ間にBB/SQLiteを先取りしない。展開完了後に集積を返し、同梱owner移譲・余白返却後にPOST_ROOTを開始する。終了時の空きだけを測ってピーク合格にしない。

## 7. 実装単位と試験台帳

| 段 | ファイル/成果物 | 必須の試験 |
|---|---|---|
| T4a 形式/allocator prototype | 新module核/生成器、`lib/sqlite3/os32_sqlite_vfs.c`, `sqlite_stack.asm` | reloc末尾−2/重複/slot重なり/未知版、各割当失敗、allocator hookとFEP headroom |
| T4b DB facade/F2/F3 | `kapi/kapi_db.c`, `kernel/ime_dict.c`, `kernel/isr_handlers.c`, VFS identity/group | copy-before-finalize、1024B境界、engine faultはkill/cleanupゼロ、parked/親子/別名重複、CDのFEP/アプリRO重複・SHARED lock・xAccess実有無/失敗・hot journal拒否、T01/T02/T07/T12 |
| T4c 分離/標準起動 | `build/kernel.mk`, `build/module.ld`, loader、boot初期化、deploy manifest | NOSYS、全db KAPI、db_mem_used、FEP1語、アプリ圧迫下FEP、init各段失敗、settings close/FEP CRC/辞書欠落→正常rollbackで占有ゼロMINIMAL、close失敗→隔離量診断 |
| T5a FEP | `kernel/ime*.c`, facade/exports、module build | 不正入力はengine進入ゼロ、学習journal生存、close失敗reopen拒否、MINIMALのGUI/CUI英数 |
| T5b early/bundle | `kernel/kernel.c`, `boot/vk32_boot.c`, FD loader ASM, `boot/boot_defs.h`, VK32生成器 | FD FAT2エントリ/CD/HDD、prefix表/BSS隣接の破損fixture、BSS内reloc・非整列container拒否、load_span境界、entry4本の212B wire・entry_count5の拒否・最大定数6のASSERT拒否、bootinfo不整合、余白返却・二重移譲、CRC/範囲不一致 |
| T5c driver lifecycle | `kernel/irq.c`, tick broker、各driver、module manifest、splash | IRQ登録後失敗、tick残存、DMA停止不能、隔離保持と永久予約不変、GUI=0識別のみ |
| T6b staging | HDD/FD loader、VK32生成器、区間検査 | 508KiB超〜1MiBの成功、1MiB超拒否、source重複、8MB各段ピーク、CRC同一fixture |

各段は `make check-changed` と必要なbuildを通し、ABI段は [ABI1]〜[ABI3] に従う。外部apps/gameは現行の対象外決定を維持し、再開時の全再ビルドを引継ぎゲートへ残す。故障注入は別の使い捨て媒体で行い、本番辞書へ書かない。通常ゲスト8MB planar/PEGC・17MB・Ra266とFDの受入を上位票§7へ紐付け、MINIMAL落ちは標準成功とは別結果で記録する。ログにはimageのbuild ID/世代、module状態、owner別ページ、callback/DMA参照、例外分類、未実施構成を残す [V4]。

## 8. 独立レビュー対応 (往復1)

レビュアー `claude-opus-5-5` (Opus 5.5)、2026-10-01、判定Request changes。実装検証ではなくコード/設計の照合。修正後の確認は未了。

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
