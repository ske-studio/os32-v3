# 設計②　PC-98 の KAPI を保存し、PowerMac の表と起動領域を独立させる

対象は main `3497386e7`。①の起動骨格を前提とし、本稿は設計までとする。
互換契約は 2026-11-24 決定の POLICY_DEV §10、AC-KAPI-1 の G「名前・意味・振る舞い（slot の番号・並びは機種ごと）」を用いる。

## 1. 実物と互換の境界

|対象|現物で確認できる依存|契約の扱い|
|---|---|---|
|呼出名・型|SDK の C 構造体、Rust 宣言、入力・出力検査が同じ api 要素から生成される|ソース。引数の評価、副作用、失敗値も保つ|
|番号|gen_kapi.py が api 配列を走査し、番号定数・呼出コード・初期化を生成|バイナリ。x86 の既存 340 本を固定|
|表の場所|x86 の arch_exec_kapi.h は KAPI_ADDR、PPC の exec_kapi.c はページ整列した静的実体を返す|既に機種別。分離対象は主に内部配置|
|刻印|非ロードの .os32_kapi_layout を梱包時に読む。OS32X の +44 にデータ欄 offset、+48 に ABI 世代|バイナリ。ローダによる取り違え拒否は共通|
|データ欄|先頭 8 B、関数容量 384、sbrk_heap_limit と shm_base が 0x608、0x60c|値の意味はソース、位置は機種別|
|不在|現行 PPC でも wrap と欄を生成。PROVIDER_GROUPS は常時不在と構成依存を含む|不在時の応答はソース、穴の有無は機種別|
|試験|test_arch_compat.py が os32_kapi_slots.h をバイト比較し、共通配列から TRY_SLOT を得る|観測値は共通、番号の一致要求を解く|
|基点|check_kapi_abi.py は順序付き署名・データ欄・容量を過去の JSON と比較|各機種内のバイナリ契約として継続|
|ページング|共通 mm の固定 PD/PT、PowerMac の chain limit、boot_walk_valid が同じ枠を参照|案 D の形式を保ち、物理予約は機種別|
|シェル|リンク・梱包・exec・試験が 0x400000 に収束|コマンドと再起動の意味は共通、VA は機種別|
|pair|root_pair.py が部品と ISO を再結合し、module_pair.c が ELF と root を検査|混在拒否は共通、識別子と媒体の形式は機種別|
|import|module_image.c の PPC 位置検査は module_positional_imports(id) で限定|名前・型・版の検証は共通、KDEBUG の位置は部品内 ABI|

POLICY_DEV §1 の三つの判断を適用する。

1. SDK 生成、exec 初期化、shell_loop、部品検証には本線に同じ役目の実装がある。
2. 生成器・検証器・起動手順は一つにし、機種差はビルド入力とリンクされる提供者に閉じる。HWDEV の target_gate が先例。
3. ①のメモリ・媒体・サービス開始順、案 D、H-7 の外部 SDK 利用と整合する。§9 の旧 FM/SSG は x86 欄と PPC の不在応答を別記する。

## 2. 意味定義、配置台帳、生成

- kapi.json を名前で引く唯一の意味定義にする。ret、args、out/in、実体の別名、ラッパー本体、能力名、不在応答、データ項目の型を持つ。
- 同じ能力名は同じ操作を指す。提供の有無は機種の能力集合で表現する。未実装を「成功した操作」として追加しない。
- 既存配列はレビューと旧ツールのため維持できる。新生成器は配列添字を呼出番号に使わない。
- 配置台帳が持つのは名前への参照、容量、欄の順、ターゲット固有世代と crt 識別子。型と実装名を複写しない。
- 以下は入力と解決の抜粋。実台帳の slots は省略せず、番号 0 から順に全名を収録する。

```python
# sdk/kapi.json の api 要素例（既存の型を維持）
{"name":"gfx_init", "ret":"void", "args":[], "target":"gfx_kapi_init"}
# sdk/kapi_layout_x86.json の抜粋
{"target":"x86-pc98", "slots":["gfx_init"], "capacity":384,
 "data":["sbrk_heap_limit","shm_base"], "kapi_abi":2, "crt":"os32_kapi_v89"}
# sdk/kapi_layout_ppc.json の抜粋（製品系列）
{"target":"ppc-powermac", "slots":["gfx_init"], "capacity":320,
 "data":["sbrk_heap_limit","shm_base"], "kapi_abi":3, "crt":"os32_ppc_kapi_g3"}
by_name = {a["name"]: a for a in spec["api"]}
ordered = [by_name[name] for name in layout["slots"]]
data_off = 8 + 4 * layout["capacity"]
```

|選択|初版の配置|その後|
|---|---|---|
|x86-pc98|現行 JSON 順を機械転記。容量 384、44 空き、固定番地と全世代を保存|末尾追加。予約域を超える変更は別設計|
|ppc-powermac|link・ide・fep をすべて提供可能とした集合から常時不在 24 名を除いた 316 名。初回のみ旧順で抽出|台帳に末尾追加。既存の名前・順序・署名を固定|
|任意構成|serialfs 2、ide 9、ime 16 は提供可能なので系列に各一度だけ登録|off 構成は既存の不在応答をその番号に結線。on/off で後続番号を詰めない|
|PPC データ|容量 320、先頭 0x508、次が 0x50c。余り 4 は未割当で呼出拒否|容量内の追加はデータ位置を動かさない。満杯なら新世代|
|将来提供|常時不在から実装に移る名は既存 API として検索する|新 slot を末尾へ。能力・最小版・試験を同時更新|

- 「提供する口」は機種系列で一度でも提供する口とする。構成ごとの ABI を多数作る必要がなく、異なる構成のアプリでも不在応答を受けられる。
- 常時不在は PPC の wire 表、syscall 番号、カーネル初期化、dispatcher、カウンタのすべてから除く。
- SDK では旧名を残す。既存 C の `kapi->name(args)` は関数ポインタのメンバ呼出しなので、同名の自由関数を追加しただけでは互換にならない。
- PPC SDK の公開 KernelAPI とカーネル側の物理表型を分ける。crt がアプリ所有の公開ビューを入口で初期化し、既存名の型付きメンバを提供する。
- 提供メンバにはユーザー用 trampoline の該当入口、不在メンバには SDK 内の型付き関数を代入する。カーネルの関数アドレスを公開しない。
- ビューの関数ポインタはアプリ内の C 表現。メンバの offsetof を syscall 番号へ変換する経路は禁止する。
- sbrk_heap_limit と shm_base は exec が確定したユーザー表からアプリ開始ごとに転記。親復帰は親のビュー、子は子のビューを用いる。
- exec_launch / exec_finish が同一アプリの再開時にデータを変える経路では、制御返却時に当該ビューを再同期する。親子で可変状態を共有しない。
- crt を使わない入口にも同じ初期化関数を公開し、常駐シェル・共有ライブラリには呼出元の公開ビューを渡す。Rust の公開メンバと FFI は同じビューから生成。
- 非対応応答は pointer=NULL、ZERO_SLOTS の指定照会=0、void=副作用なし、他=OS32_ERR_NOSYS。呼出引数は通常の関数呼出しとして一度評価。
- absent_providers の slots と includes を分離し、オプション有効時の extern と実 provider を解決。ABSENT_PROVIDER_SLOTS 全体を初回削除リストに使わない。
- 台帳検証は未知名・重複・容量超過・提供口の未登録・常時不在の誤登録・データ型不一致で出力前に失敗する。
- ターゲットの選択はビルド側で一回行い、生成物を専用 out-dir に揃える。共通 C に機種マクロや機種ヘッダを追加しない。
- 物理表の型・番号定数・wrap・初期化 inc・PPC invoke・trampoline の添字・Rust の wire 参照・計測名を同じ ordered から出す。
- exec_kapi_init は既存の取得→初期化→データ設定。各機種の実体に対して offsetof と sizeof、予約域上限をコンパイル時照合する。

**刻印と更新手順（KAPI_SPEC §3-1 の改訂）**

1. 意味追加時は JSON の末尾へ追記し公開 version を上げ、KAPI_VERSION と同期する。今回の配置分離のみでは意味版 91 を保つ。
2. 提供するターゲットの台帳だけに追記する。互換破壊には対象 ABI 世代と crt symbol の更新、コミット本文の KAPI-Break 理由を要求する。
3. 移行初回は PPC ABI 2→3。x86 は ABI 2、memory 4、OS32X 5、shlib 2 のまま。PPC のメモリ移動時だけ memory 4→5。
4. 機種ごとの SDK・crt の非ロード刻印を ELF のバイト順で読み、全翻訳単位の世代・リンク参照の一致も検査する。
5. mkos32x は ELF machine で台帳を選び、欠落・異値混在・別機種の offset を拒否。OS32X メタデータは従来どおり LE。
6. ローダは CPU フラグ、offset、世代、min_api_ver を実行前に照合。同じ offset になり得るため番号順の破壊は世代で区別する。
7. 生成後 clean ビルドで SDK・crt・カーネル・シェル・受入像を同一世代にする。PPC 旧像の互換変換は不要。

## 3. 試験と基点の関門

|比較する範囲|比較入力と判定|
|---|---|
|ターゲット間|意味定義を名前で join。引数順・型・戻り型・能力の意味と提供時の結果を照合。能力の有無は明示した機種行と照合|
|test_arch_compat.py|ヘッダの全バイト比較を外し、TRY_SLOT は個別出力から名前で解決。従来の入力動作を両機種で実行|
|不在応答|型の一致に加え SDK 呼出しの観測値、出力バッファの非変更、syscall 回数 0 を検査。構成 off の場合はゲート経由の応答も検査|
|同じ機種内|台帳を期待値、実コンパイルの offsetof・シンボル・初期化先・dispatcher と梱包済み OS32X を観測値とする|
|test_kapi_table.py|PPC 台帳順で初期化先と invoke を逆引き。OSRI/mtime の同型入替、欠けた wrap、予約番号の誤受理を検出|
|test_kapi_binding.py|常時不在には wire 欄も外部参照も無いこと、構成依存には正しい実体／不在応答があることを全構成で検査|
|コンパイル|C のメンバ呼出し・ポインタ取得、Rust、独自入口を含める。SDK ビュー経由と直接ゲートの提供時の結果を照合|
|check-kapi-abi|merge-base（本線自身なら親、明示 --base 可）から対象ごとの台帳と当時の意味定義を読み、解決済みの署名列を比較|
|移行前基点|旧 JSON の順・容量・データ署名を復元して x86 と比較。PPC 旧系列→3 は明示された一度の破壊として記録|
|移行後基点|同世代は旧列の prefix、既存データ offset、crt、世代を照合。異なる機種の破壊理由で x86 の差を承認しない|

- 各試験で期待ファイルを現行生成物から再作成しない。基点の設定不備・台帳欠落は初回移行を除きエラー。
- x86 旧 SDK でビルド済みの像を新カーネルで起動する。最新 SDK 同士のテストだけで H-7 の互換を判定しない。

## 4. PowerMac の起動領域と常駐シェル

|現行の拘束|機種が供給する値|利用者|
|---|---|---|
|0x3e0000 の guard、次頁 PD、26 頁の固定枠|boot root PA、連続する初期 PT の先頭・数、窓の PT、予約終端、guard 区間|予約、mm 初期化、walk、root 切替|
|常駐末尾から固定 guard まで|image 上限と chain 上限を別々に持つ。stack・ledger・heap・KAPI・guard・SHM を含む|kernel.ld の ASSERT、予約表、map 検査|
|固定帯と HPT の混同を防ぐ|HPT は非零 PA、128 KiB、同境界整列、RAM 内かつ予約外から選んで専有|kboot_reserve、memory、SDR1 初期化|
|0x400000 のシェル|固定リンクする機種内 VA、像上限、余白、heap、guard、stack の区間|app_sys.ld、mkos32x、exec とページ保護|

- まず機種別入力から C・ld・梱包用の数値を生成し、旧値で経路だけ置換する。次段で PPC の値を移す。
- 共通 mm には boot paging geometry を渡す。PC-98 の提供者は既存定数を返し、従来の静的検査は PC-98 設定の検査として残す。
- mm/paging_boot.inc の構築・恒等写像・検証、mm/paging.c の保護属性判定も同じ geometry を参照する。
- 案 D の 4 KiB PD が 1 枚、現行の事前登録する低位 PT が 8 枚、aperture と最高位 MMIO の PT が各 1 枚。初期ソフト表は 44 KiB。
- 低位 PT は未使用ページにも PDE を事前登録する。現在 present な PTE 数から算出すると後の map が壊れる。
- 26 枚との差の 15 枚（60 KiB）は自動継承しない。予約台帳・割当・root 切替までの利用者を列挙し、使う分だけ名前付きで予約する。
- 新配置は kernel image の許容上限から ledger・stack の実配置を含む resident_end を検算し、その後の chain の最大末尾を頁切上げする。
- その上に未写像 guard 1 頁、PD、初期 PT 群を置く。リンク時の最大予算から決め、ELF の成長で固定 SDK 番地が暗黙に変わるのを防ぐ。
- shell VA は当該予約・他の固定 VA と交差しない頁境界から選ぶ。像＋BSS、余白、256 KiB heap、guard 1 頁、40 KiB stack の順を初期予算とする。
- 新 VA をビルド入力で一つ選び固定する。実 RAM と VA を別の半開区間表で検証し、恒等写像する区間だけ PA=VA を要求する。
- 32 bit 終端は広い整数で加算し、wrap、空区間、整列違反、RAM 外を拒否。OF の予約、staging、FB/MMIO の alias、system 領域も重ねない。
- 初回 root の引数と復帰先は同じ boot root PA を使う。boot_walk_valid は実表全頁の supervisor/RW/WB、IO 属性、guard と HPT の非写像を walk で確かめる。
- app_sys.ld が出す __os32_shell_load_addr、ELF の LOAD、梱包器の resident 判定、exec の宛先と stack/heap の境界を照合する。
- PPC 常駐像はリンク記号欠落を拒否。一般アプリの梱包には常駐用必須記号を課さず、PC-98 の既存 fallback は保持する。
- fixed_layout / report_product は生成定数の丸写しを期待値にせず、ELF/map、OS32X の実サイズ、起動後の予約表・walk から独立に検査する。
- chain と shell の間の空きは許す。像末尾＋余白 ≤ heap 先頭、heap 末尾 ≤ guard、stack が guard を越えず、各実効権限が用途に合うことを要求する。
- 対照実験は合法な別番地で再リンクして起動し、重なり、guard 写像、ユーザーからの PD 書込み許可の各変異だけを失敗させる。

## 5. 部品と root の照合

- 要件は「当該製品ビルドと異なるビルドで生成した部品を、ABI と内容が偶然一致しても、開始・公開前に拒む」。
- 現行は正規化した ELF の CRC32 を pair にし、ISO 内の部品の pair と自身の CRC を書き戻し、再圧縮し、枠不足なら再リンクする。
- platform_module_pair は entry 検証済みであることと boot の image CRC・サイズを検査してから値を公開する。
- 代案は製品ビルドにつき 128 bit の識別子を一度払い出し、kernel と全部品の PPC 新形式に埋める。commit hash 単独は同一ソースの別ビルドを区別できない。
- 同一製品の並列ジョブと失敗再試行は同じ識別子を入力にする。独立した製品ビルドには新 ID を発行し、旧成果物の流用は ID 一致時だけ許す。
- PPC の新部品版は LE の 16 B 欄を明示し、ターゲット・部品形式・module ABI・ID 全長を比較。現行 u32 pair に切り詰めない。PC-98 は旧形式のまま。
- mkmod が最初から ID 付き部品と部品 CRC を出す。完成 ISO を一度圧縮し、その長さから frame を頁切上げして ELF を一度リンクする。
- 結果は展開長・root CRC・枠上限・部品 CRC を再検査。ELF 自体の整合検査は正規化 CRC と記録の最終確定を残してよい。部品へ逆伝播させない。
- root の取り違えも検出するため期待 root CRC と長さは kernel 記録に保持し、HDD から取得した部品にもロード時に ID 照合を課す。
- 起動媒体が検証できない場合は pair 相当の資格を発行しない。① S7 の FEP 公開条件、静的 SQL 成功後の失敗処理を保つ。
- ID 一致は媒体破損の検査を代替しない。逆に、CRC の再計算で別 ID の部品が通ってはならない。

|KDEBUG の位置依存|設計上残す検査|
|---|---|
|module_diag.c の .data.kdebug_import_slots に struct pm_diag_slots を置き、snapshot、bar を読む|diag_slots.h と kdebug_imports.json の 8 要素の名前・型・offsetof を照合|
|platform_hwb.h は abort_take、phys_check、phys、block、platform_hwb_pci.h は cfg_read/write をメンバ参照|書込位置がずれると同じ型でも別操作へ飛ぶので、mkmod と loader の双方で検出|
|module_positional_imports は KDEBUG のみ。PPC module_image は data_off+4*i を要求|この部品版では維持。FEP 等は記述された位置の範囲・重複・型・名前・版を検証|

## 6. 各段に含める整理

- 生成時に一度配置を解決し、実行時の名前探索・動的な表再構築・互換用の二重 dispatcher を設けない。
- PPC は ABI を製品系列で一つにする。協調切替中は既存 exec の保存復帰を使い、能力照会やビューにロックを追加しない。
- 部品 ID の保持期間を製品ビルドに限定する。媒体別の登録サービスや再試行キューを設けない。

## 7. PowerPC 像の増減見積もり

|対象|差の根拠|計測方法|
|---|---|---|
|カーネル表|384→320 語で BSS −256 B。ページ整列により予約頁差は 0 の場合あり|ELF の symbol size、map、NOBITS|
|wrap / invoke|常時不在 24 本の wrap と case が消える。各 4〜32 B なら約 −0.2〜1.5 KiB（両者合計の概算）|同一最適化・同一 feature の text 差|
|ユーザー像|C ビューは 340 語＋先頭とデータで約 1.35 KiB のアプリ BSS。初期化・stub の text が増える|shell と最小 crt 像の filesz/memsz を別計測|
|paging|ソフト表 44 KiB と HPT 128 KiB は残る。未使用なら固定予約を最大 60 KiB 解放|RAM 予約台帳を比較。ELF ファイル削減量には算入しない|
|部品|ID を拡張欄に追加する案で各 +16 B＋整列、旧照合コードの削減は未計測|各 .mod の header/全長、root.iso と圧縮後の実長|
|製品|常駐像、スタック、heap、埋込 root 枠は別予算。小さな差でも圧縮枠は頁単位で変わる|PPCKERN.ELF、map、媒体、guard までの余裕を各段で記録|

## 8. 変更ファイル群と順序

① T8 の両機種の受入完了時点を基点とし、②を開始する。①で移動した関数は移動後の所有ファイルに適用する。
下表は実装時の手編集規模（行、概算）。各行を一段とし、関係する生成物・対応試験も同じ段で更新する。

|段・担当|新規／変更と目安|段の完了条件|
|---|---|---|
|1 本線|変更 tools/check_kapi_abi.py 70、tools/tests/test_kapi_abi.py 65|旧 JSON 基点の復元、x86 の負例を先に固定|
|2 本線|新 sdk/kapi_layout_x86.json 約350、変更 sdk/kapi.json 40、gen_kapi.py 100、kapi_rust_gen.py 25|x86 生成一式が同一。PPC 切替前は従来出力|
|3 本線|変更 tools/tests/test_arch_compat.py 65、docs/KAPI_SPEC.md 30、ARCH_COMPAT_CONTRACT.md §4.2・§9 約10|両ターゲットを名前で検査。文書は docs-1124g を基準に統合|
|4 PPC|新 sdk/kapi_layout_ppc.json 約325、変更 sdk/kapi_target_ppc.py 40、gen_kapi.py 120、kapi_rust_gen.py 45|compact 表・SDK ビュー・init/invoke を専用出力へ生成|
|5 PPC|変更 arch/ppc/{exec_kapi.c,kapi_table.h,syscall_code.c} 各10〜30、PPC crt 40、exec の trampoline/launch/finish 各15〜30|SDK と製品を一緒に切替、独自 crt と親子復帰も確認|
|6 PPC|変更 sdk/{os32x_hdr.py,mkos32x.py} 各25、exec/os32x_hdr.c 15、tools/ppc/test_kapi_{table,binding}.py 各60|機種別世代・刻印・実結線。4〜6 は製品切替まで同じ統合系列で検査|
|7 本線|変更 tools/gen_memmap.py 50、include/memmap.h 20、mm/{paging.c,paging_boot.inc} 各45、新 共通 geometry 宣言約35|既存 PC-98 定数で同じコード・配置を生成。PPC も旧値で起動|
|8 PPC|変更 platform/powermac/{platform_kboot_layout.h,kernel_boot.c,kboot_reserve.c,memory.c} 各20〜55、arch/ppc/kernel.ld 25|guard・PD/PT・HPT の予約と walk を新配置に切替|
|9 PPC|変更 sdk/link/ppc/app_sys.ld 15、exec/exec_launch.inc 20、tools/ppc/{kboot_accept.py,kboot_layout.py} 各60|機種の shell VA と新 memory 世代を梱包・受入へ反映。8 と同時に旧像を隔離|
|10 PPC|変更 tools/{mkmod.py,ppc/root_pair.py} 各50〜90、build/targets/ppc-powermac.mk 40、module_pair.c 45、kernel/module/{module_schema.h,module_image.c} 各25|§10 決定後に製品 ID・新形式・一回リンクを導入、KDEBUG 8 位置を維持|
|11 PPC|変更 tools/accept/ppc_module.py 50、対応 tools/checks/ 登録各5〜15、PPC の KAPI_FILL・SHELL_ALIGN・MODULE_ALIGN 設計に各10〜20|各変異が意図した箇所で落ち、製品シェルと部品が起動|

- 段4〜6、8〜9は個別に生成・host 検査し、全経路が揃った切替点でのみ製品既定にする。旧受入像と新 SDK の混在を配布しない。
- 各段で同一 toolchain、設定、時刻入力、submodule revision の PC-98 基準ビルドと clean ビルドを別出力に生成して cmp する。
- SDK の C/Rust/asm/ld、番号表、初期化 inc、crt と既存アプリ、kernel.bin・vmkernel.lz4・kdebug.bin・両 FD のハッシュ差を取る。
- PPC 単独段で PC-98 差分が出たら停止。本線段も x86 の生成コードと物理配置を同一に保ち、共通 geometry の定数畳込みを map・逆アセンブルで確認する。

## 9. 受入項目、試験、変異

|受入項目|通常試験|落とす変異|
|---|---|---|
|x86 既存バイナリ|基点 cmp と旧 SDK 像の起動|同型 2 slot の交換|
|共通の意味|両 SDK で同じ呼出しと出力を比較|型付き出力の値を一方だけ変更|
|PPC 常時不在|C/Rust の不在応答・ゲート未到達|NP2 照会を NOSYS に変更|
|構成依存|link/ide/fep の全組合せで slot 一定、実結線と off 応答を確認|IDE off で後続 slot を詰める|
|PPC 表と初期化|offsetof・実アドレス・invoke・データ末尾を比較|同型の初期化先だけ交換|
|版・刻印|新像を受理、旧 PPC 像を入口前に拒否|offset 一致のまま世代だけ旧値|
|ビュー復帰|親→子→親でデータと関数ポインタを検査|親に子の heap limit を残す|
|案 D の予約|切替前後に全 PD/PT と予約外 allocator を照合|最後の窓 PT を予約から除外|
|HPT と guard|起動後 walk と実 RAM 台帳を検査|HPT をユーザー VA に恒等写像|
|chain 予算|製品 ELF と map の上限検証|SHM 末尾を PD に重ねる|
|shell の番地|合法な別配置で起動・終了・再起動|旧番地 fallback で梱包|
|shell 権限|stack/heap/像の属性と guard の未写像を確認|guard を RW にする|
|別ビルド部品|同一 ABI の別 ID を公開前に拒否|ID 比較を先頭 32 bit のみにする|
|媒体整合|展開・CRC・長さ、混在 HDD 部品を独立に検査|root CRC 更新済みの別ビルド部品を混入|
|KDEBUG と FEP|8 import の位置検査と非位置依存部品の正例|KDEBUG の第2・第3位置を交換|
|製品サイズ|①の媒体別予算で clean 像と予約量を実測|root frame を実圧縮長未満にする|

## 10. 未決（ユーザー判断 1 件）

- 「別ビルド」を同一入力の独立再ビルドまで含めるか。推奨は含める（§5 のビルド単位 ID）。同一入力でも別 ID となり、製品バイト列の再現には発行済み ID の保存が要る。
- 入力が同じ再ビルドを同一扱いする場合は、全ソース・submodule・設定・ツール・生成入力の正規化 digest を製品 ID にする。この選択だけを段10の前に確定する。
