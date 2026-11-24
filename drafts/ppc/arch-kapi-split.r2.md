# 設計②　PC-98 の KAPI を保存し、PowerMac の表と起動領域を独立させる

## r1→r2 の変更

- P1-1 常駐シェルと一般アプリの入口型・結線・寿命を分けた。
- P1-2 親ビューの記録先、復元順、起動失敗からの復帰を定めた。
- P1-3 旧固定帯の利用者、機種別の予約・写像・隣接条件を定めた。
- P1-4 HPT 全体の下限・上限を選定と再検証で統一した。
- P1-5 同じ ID を埋込像と HDD 専用像へ供給する順序を定めた。
- P1-6 部品・ロード入口ごとに ID と検証資格を必須にした。
- P2-1 全生成入力、既定 CLI、Rust 単独生成と cmp を定めた。
- P2-2 刻印の発行元を明記し、梱包とロードの負例を分けた。
- P2-3 データ署名列と機種別の破壊宣言を基点比較に加えた。
- P2-4 常時不在の観測点を SDK のメンバ呼出しへ移した。
- P2-5 常駐指定と機種定義を使い、旧番地で自己整合する像を拒む。
- P2-6 依存・試験登録・変異アンカーを各段に、通し受入を段11に割り当てた。
- P3-1 初版の集合・空き枠数を検査し、案 D のソフト表を明記した。
- F1 全世代・crt・容量・データ順を機種台帳に集約した。
- F2 各ターゲットの生成器が更新する ld を限定した。
- F3 破壊宣言を機種・世代遷移に照合する。
- F4 版検査と配置試験の x86 番号・終端を台帳から得る。
- F5 make の前提と checks の covers に台帳を登録する。
- F6 配布 SDK の梱包に生成済み Python 表を使い、x86 の生成先を維持する。
- F7 公開版を機種別に管理する。
- F8 比較する両ビルドで commit と epoch を固定する。
- F9 PC-98 の geometry をコンパイル時定数にする。

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
|ページング|案 D でも共通 mm が使うソフト表 PD/PT の固定配置、PowerMac の chain limit、boot_walk_valid が同じ枠を参照|案 D の形式を保ち、物理予約は機種別|
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
- 既存配列は意味定義のレビュー順として維持する。新生成器は配列添字を呼出番号に使わない。
- 機種台帳を slots、func_capacity、data の順、version、crt_kapi_symbol、四つの generations の唯一の入力とする。意味定義からこれらの旧値を除く。欠落は生成エラー。
- 以下は入力と解決の抜粋。実台帳の slots は省略せず、番号 0 から順に全名を収録する。

```python
# sdk/kapi.json の api 要素例（既存の型を維持）
{"name":"gfx_init", "ret":"void", "args":[], "target":"gfx_kapi_init"}
# sdk/kapi_layout_x86.json の抜粋
{"target":"x86-pc98", "slots":["gfx_init"], "func_capacity":384,
 "data":["sbrk_heap_limit","shm_base"], "version":91, "crt_kapi_symbol":"os32_kapi_v89",
 "generations":{"kapi_abi":2,"memory_layout":4,"os32x_format":5,"shlib_protocol":2}}
# sdk/kapi_layout_ppc.json の抜粋（製品系列）
{"target":"ppc-powermac", "slots":["gfx_init"], "func_capacity":320,
 "data":["sbrk_heap_limit","shm_base"], "version":91, "crt_kapi_symbol":"os32_ppc_kapi_g3",
 "generations":{"kapi_abi":3,"memory_layout":4,"os32x_format":5,"shlib_protocol":2}}
by_name = {a["name"]: a for a in spec["api"]}
ordered = [by_name[name] for name in layout["slots"]]
data_off = 8 + 4 * layout["func_capacity"]
```

|選択|初版の配置|その後|
|---|---|---|
|x86-pc98|現行 JSON 順を機械転記。容量 384、44 空き、固定番地と全世代を保存|末尾追加。予約域を超える変更は別設計|
|ppc-powermac|link・ide・fep をすべて提供可能とした集合から常時不在 24 名を除いた 316 名。初回のみ旧順で抽出|台帳に末尾追加。既存の名前・順序・署名を固定|
|任意構成|serialfs 2、ide 9、ime 16 は提供可能なので系列に各一度だけ登録|off 構成は既存の不在応答をその番号に結線。on/off で後続番号を詰めない|
|PPC データ|容量 320、先頭 0x508、次が 0x50c。余り 4 は未割当で呼出拒否|容量内の追加はデータ位置を動かさない。満杯なら新世代|
|将来提供|常時不在から実装に移る名は既存 API として検索する|新 slot を末尾へ。能力・最小版・試験を同時更新|

- 初版検査は PROVIDER_GROUPS の serialfs・ide・ime の和集合が 27、ABSENT_PROVIDER_SLOTS との差集合が 24、意味定義 340 から後者を引いた台帳が 316、320−316=4 を要求する。
- 「提供する口」は機種系列で一度でも提供する口とする。構成ごとの ABI を多数作る必要がなく、異なる構成のアプリでも不在応答を受けられる。
- 常時不在は PPC の wire 表、syscall 番号、カーネル初期化、dispatcher、カウンタのすべてから除く。
- SDK では旧名を残す。既存 C の `kapi->name(args)` は関数ポインタのメンバ呼出しなので、同名の自由関数を追加しただけでは互換にならない。
- PPC SDK の公開 KernelAPI とカーネル側の物理表型を分ける。crt がアプリ所有の公開ビューを入口で初期化し、既存名の型付きメンバを提供する。
- 常駐シェルの特権入口は compact なカーネル表へのポインタと shell service を受け、crt が提供メンバをその wrap へ結線する。一般アプリの入口は同じ wire 配置のユーザー trampoline 表を受け、提供メンバを対応するゲート stub へ結線する。両者の不在メンバは SDK 内の型付き関数とする。
- crt・Rust・独自入口が共用する初期化関数が像ごとの BSS に公開 KernelAPI を構築する。shlib は呼出元のビューを借り、像の終了時に失効する。公開メンバの offsetof を番号に変換しない。
- exec は梱包時に ELF から記録したビューの BSS 相対位置とサイズをロード時に書込可能範囲・整列まで検査し、解決した VA を各 app context に保存する。開始時は入口表の sbrk_heap_limit と shm_base を転記する。
- 親の中断時に親 context に両値を保存する。exec_finish と exec_launch_abort は親 root が有効であることを確定してから親の wire 表と検査済みビューを親の両値で更新し、最後に親へ制御を戻す。子の終了時は子の記録を破棄する。
- 非対応応答は pointer=NULL、ZERO_SLOTS の指定照会=0、void=副作用なし、他=OS32_ERR_NOSYS。呼出引数は通常の関数呼出しとして一度評価。
- absent_providers の slots と includes を分離し、オプション有効時の extern と実 provider を解決。ABSENT_PROVIDER_SLOTS 全体を初回削除リストに使わない。
- 台帳検証は未知名・重複・容量超過・提供口の未登録・常時不在の誤登録・データ型不一致で出力前に失敗する。
- ターゲットの選択はビルド側で一回行い、無指定 CLI と Rust 単独の既定は x86、明示ターゲットは同じ台帳解決器を使う。x86 は従来の木、PPC は専用 out-dir とし、各々自機の app.ld・app_sys.ld だけを更新する。共通 C に機種マクロや機種ヘッダを追加しない。
- 物理表の型・番号定数・wrap・初期化 inc・PPC invoke・trampoline の添字・Rust の wire 参照・計測名を同じ ordered から出す。
- exec_kapi_init は既存の取得→初期化→データ設定。各機種の実体に対して offsetof と sizeof、予約域上限をコンパイル時照合する。

**刻印と更新手順（KAPI_SPEC §3-1 の改訂）**

1. 意味追加時は JSON の末尾へ追記し提供機種の台帳の version を上げ、KAPI_VERSION と同期する。今回の配置分離のみでは意味版 91 を保つ。
2. 提供するターゲットの台帳だけに追記する。互換破壊には対象 ABI 世代と crt symbol の更新、同じ遷移の機種付き破壊宣言を要求する。
3. 移行初回は PPC ABI 2→3。x86 は ABI 2、memory 4、OS32X 5、shlib 2 のまま。PPC のメモリ移動時だけ memory 4→5。
4. crt・Rust 起動コード・独自入口が非ロード刻印を発行し、リンクでは発行元の参照が対象 crt symbol に解決することを確認する。刻印を発行しない翻訳単位の世代一致までは保証しない。
5. 配布 SDK の mkos32x は ELF machine で os32_generations.py の生成表を引く。x86 の既存名・値を維持し、機種別 SDK に対応表を同梱する。JSON は配布時不要。ELF 順で刻印を読み、欠落・混在・機種別 offset 不一致を梱包時に拒む。OS32X メタデータは LE。
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
|移行後基点|同世代は関数署名の旧列 prefix、容量、既存データの名前・型・順序・offset、crt を照合。破壊には対象台帳の ABI 更新・新 crt と同じ変更での `KAPI-Break(x86-pc98): 旧世代→新世代 理由`（PPC は ppc-powermac）を要求し、機種なし・世代更新だけは不可。異なる機種の破壊理由で x86 の差を承認しない|

- test_kapi_abi.py に PPC 宣言と同型 x86 2 slot 交換の組合せを入れ、x86 破壊として拒む。
- test_arch_compat.py の np2_detect 等は kernel wrap 抽出を SDK メンバ実呼出しに替え、独立した期待値で 0・NULL・NOSYS・void、引数ごとの一度評価、出力不変、ゲート回数 0 を検査する。
- 各試験で期待ファイルを現行生成物から再作成しない。基点の設定不備・台帳欠落は初回移行を除きエラー。
- x86 旧 SDK でビルド済みの像を新カーネルで起動する。最新 SDK 同士のテストだけで H-7 の互換を判定しない。

## 4. PowerMac の起動領域と常駐シェル

|現行の拘束|機種が供給する値|利用者|
|---|---|---|
|0x3e0000 の guard、次頁 PD、26 頁の固定枠|boot root PA、連続する初期 PT の先頭・数、窓の PT、予約終端、guard 区間|予約、mm 初期化、walk、root 切替|
|常駐末尾から固定 guard まで|image 上限と chain 上限を別々に持つ。stack・ledger・heap・KAPI・guard・SHM を含む|kernel.ld の ASSERT、予約表、map 検査|
|固定帯と HPT の混同を防ぐ|HPT は非零 PA、128 KiB、同境界整列、RAM 内かつ予約外から選んで専有。選定時と起動時とも admitted_end ≤ PA、PA+128 KiB ≤ 32 MiB を広い整数で検証|kboot_reserve、memory、SDR1 初期化|
|0x400000 のシェル|固定リンクする機種内 VA、像上限、余白、heap、guard、stack の区間|app_sys.ld、mkos32x、exec とページ保護|

- まず機種別入力から C・ld・梱包用の数値を生成し、旧値で経路だけ置換する。次段で PPC の値を移す。
- 共通 mm には boot paging geometry を渡す。PPC は関数で渡し、PC-98 はヘッダの既存コンパイル時定数を用い、従来の静的検査は PC-98 設定の検査として残す。
- paging.c の paging frame 末尾＝stack guard、kstack 末尾＝shell、shell 末尾＝pool の隣接検査は PC-98 に限定。PPC の memory_boot.c は旧固定境界から pool を一括予約せず、実 RAM の常駐像・chain・stack・ソフト表・shell の backing・HPT と保護用 guard を個別に allocator から除外する。空き RAM は他予約との照合後に返す。
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
- PPC のシェル梱包規則は resident を明示する。通常像は app VA のみ許可。resident は機種定義の shell VA を独立した期待値として記号・先頭 LOAD・OS32X 宛先の全一致と静的リンクを要求し、記号欠落や現世代で旧番地に自己整合する像を拒否する。一般アプリの梱包には常駐用必須記号を課さず、PC-98 の既存 fallback は保持する。
- fixed_layout / report_product は生成定数の丸写しを期待値にせず、ELF/map、OS32X の実サイズ、起動後の予約表・walk から独立に検査する。
- chain と shell の間の空きは許す。像末尾＋余白 ≤ heap 先頭、heap 末尾 ≤ guard、stack が guard を越えず、各実効権限が用途に合うことを要求する。
- 対照実験は合法な別番地で再リンクして起動し、重なり、guard 写像、ユーザーからの PD 書込み許可の各変異だけを失敗させる。

## 5. 部品と root の照合

- PM 決定。正規化した kernel ELF と製品に含む全部品 ELF が同じなら、独立再ビルド・並列・再試行も同じ製品とする。PC-98 は現行 CRC pair を維持する。
- PPC は媒体梱包前の共通 kernel ELF と部品 ELF を入力にする。kernel を先頭、部品を module ID 昇順とし、各入力の ID 欄のみをゼロ化、種類・ID・バイト長を固定幅 LE で前置して連結する。SHA-256 の先頭 16 B をその順で product.id に保存する。入力は埋込 root・媒体別の最終リンクより前に確定し、循環を作らない。
- 同じ product.id から生成した定長オブジェクトを PPCKERN.ELF と build/ppc-install-boot.mk の PRODUCT.ELF の双方にリンクする。mkmod は同じ 16 B を必須欄に書いて部品 CRC を確定。ISO は完成後一度圧縮し、実長を頁切上げして PPCKERN の frame を決める。HDD 専用 PRODUCT は frame を持たない。
- 配布前に両 ELF の ID 欄、ISO・HDD の各部品の ID と CRC を再読する。埋込起動は entry・展開長・root CRC・枠・image CRC の検証後に資格を発行。HDD 起動は handoff と期待 plan の検証後に PRODUCT の ID を公開し、異常時は既存の起動拒否、root partition 不一致時の診断を維持する。
- 製品の外部部品 KDEBUG・FEP を埋込 root、インストール済み HDD の両経路で共通 module_image 検証に通す。静的 SQL は ELF 部品のロード対象外。部品追加時も同じ必須ポリシーとし、任意 pair flag による照合省略を廃止する。
- target・新形式・module ABI・資格・16 B 全長の一致を開始／公開前に要求する。ID なし、旧形式、資格なし、先頭 32 bit のみ同じ別 ID、CRC 再計算済みの混在 HDD 部品を独立に拒む。部品不一致は既存のロード失敗へ返し、① S7 の FEP 非公開と静的 SQL 成功後の失敗処理を保つ。


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
|10 PPC|変更 tools/{mkmod.py,ppc/root_pair.py} 各50〜90、build/targets/ppc-powermac.mk 40、module_pair.c 45、kernel/module/{module_schema.h,module_image.c} 各25|§5 の製品 ID・新形式・媒体別最終リンクを導入、KDEBUG 8 位置を維持|
|11 PPC|変更 tools/accept/ppc_module.py 50、対応 tools/checks/ 登録各5〜15、PPC の KAPI_FILL・SHELL_ALIGN・MODULE_ALIGN 設計に各10〜20|各変異が意図した箇所で落ち、製品シェルと部品が起動|

- 段2で gen_kapi.py、gen_kdebug_imports.py の ABI_SYMBOL、gen_generation_manifest.py、ppc-powermac.mk、test_kapi_layout.py の変異入力を移す。check_kapi_version.py の check_spec_table と配置試験は x86 台帳順と使用端 8+4×slot数を使う。PPC の段4までは旧入力相当の値を明示して生成する。
- 段2で build/kernel.mk の KAPI_GENERATED、programs.mk の Rust、PPC 生成 stamp に意味定義・対象台帳・生成器を依存登録し、kapi-abi・kapi-abi-host・kapi-layout-host の covers に両台帳を登録する。台帳だけを touch して再生成を確認する。
- 段2の cmp は既定 CLI・Rust 単独を含む既存 x86 全出力に適用する。x86 配布は従来の os32_generations.py の定数を x86 行として読む。PPC 配布では同名モジュールに機種キー付き表を生成する。段6で build/sdk.mk の配布物だけを使い外部 apps/・game/ の梱包を検査する。
- 段4〜6で PPC 部品 mkmod の版・layout-gen の供給を PPC 台帳からの生成値へ切替える。各変更段で tools/checks の covers・host/accept 登録と旧 wrap・番地・pair CRC 比較に依存した変異アンカーを対応する SDK 呼出し・機種区間・全長比較に更新する。
- 段11は① T8 の両媒体から通し起動し、製品シェル再起動、一般アプリと親復帰、KDEBUG・FEP の公開、HDD の異常終了経路、旧 x86 SDK 像と再現 cmp までを受入とする。
- 段4〜6、8〜9は個別に生成・host 検査し、全経路が揃った切替点でのみ製品既定にする。旧受入像と新 SDK の混在を配布しない。
- 各段で同一 toolchain、設定、固定した OS32_BUILD_COMMIT・SOURCE_DATE_EPOCH、submodule revision の PC-98 基準ビルドと clean ビルドを別出力に生成して cmp する。
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
|版・刻印|新像を受理、混在刻印 ELF は梱包で拒否、旧 PPC 像を入口前に拒否|offset 一致のまま世代だけ旧値|
|ビュー復帰|シェルの直接 wrap と一般アプリの trampoline を呼び、子の正常終了・起動失敗の双方で親の両データと関数ポインタを検査|親に子の heap limit を残す|
|案 D の予約|切替前後に全 PD/PT と予約外 allocator を照合|最後の窓 PT を予約から除外|
|HPT と guard|起動後 walk と実 RAM 台帳を検査|HPT をユーザー VA に恒等写像|
|chain 予算|製品 ELF と map の上限検証|SHM 末尾を PD に重ねる|
|shell の番地|合法な別配置で起動・終了・再起動|旧番地 fallback で梱包|
|shell 権限|stack/heap/像の属性と guard の未写像を確認|guard を RW にする|
|別ビルド部品|同一 ABI の別 ID を公開前に拒否|ID 比較を先頭 32 bit のみにする|
|媒体整合|展開・CRC・長さ、混在 HDD 部品を独立に検査|root CRC 更新済みの別ビルド部品を混入|
|KDEBUG と FEP|8 import の位置検査と非位置依存部品の正例|KDEBUG の第2・第3位置を交換|
|製品サイズ|①の媒体別予算で clean 像と予約量を実測|root frame を実圧縮長未満にする|

## 10. PM 決定（未決 0 件）

同一内容の再ビルドは同一 ID とし、版と四世代は機種別とする。照合木は 3497386e7。指定の本線リポジトリに 66d28bfd1 がなく、当該差分は未確認。本線側はレビュー記載に基づく。

反論 1 件（sol の再ビルド判定）。独立ビルドごとの新 ID は採用しない。同一バイト列を拒んでも混在検出は強まらず、製品の再現 cmp を壊すため。
