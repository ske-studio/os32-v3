# 追跡ファイルの所属と像への依存

> 状態: P1-2 の設計案（A1 で確認、入口の実装は P2）。根拠は SRC_QA_RESTRUCTURE_SPEC §5・§15 と P0_FINDINGS の P0-1・P0-2。

## 所属を決める単位

[担当表](../../tasks/restructure/data/scope.tsv)の「担当」「種類」は配置作業を割り振る P1-0 の判断。「所属種類」は次の規則で生表をパス結合した初版で、配置が決まる前から使える。

| 入力 | 所属種類 |
|---|---|
| 製品の共通コード・内部ヘッダ・共通ユーザーランド | 共通実装 |
| P0-1 の層が arch（名前付きヘッダを含む）、CPU の命令・レジスタ・専用呼出規約に依存する実装 | CPU固有実装 |
| platform 層（名前付きヘッダを含む）、PC-98 の boot | 機種固有実装 |
| device 層の製品（drivers、hwdev、kdebug） | デバイス固有実装 |
| SDK の公開物（配布するリンク・変換の道具も含む） | 公開インターフェース |
| 検査本体・支援・実行基盤・運用の道具 | 開発・検査用実装 |
| 説明・仕様・ライセンス | 文書 |
| ビルド規則・リンク配置などの定義 | ビルドの定義 |
| 同梱データ・見本 | 資産 |
| 担当表の第三者の物、外部リポジトリの gitlink | 第三者の物 |

P1-0 の個別の用途判断を先に適用し、製品にだけ P0-1 の層を使う。例えば試験が vendor を入力にしても vendor の所有は変わらない。末尾の棚卸し用ファイルも開発・検査用実装とし、P1 の配置作業の母数と分けて照合する。

像の対象は、その構成が何をコンパイル・リンクするかで決める。所属は、実装が提供する機能と、その実装に必要な命令・機種仕様・装置で決める。例えば userland/ppc/hello.c は PPC 像に入るが SDK の呼出しだけなので共通実装。同じ場所の dbprob.c は FPSCR を操作するため CPU 固有。lib/kprintf.c は C_COMMON_CORE で書式処理を提供し、固定名 platform_kprintf.h を通じて機種に出力を委ねるため共通実装。

P1 の照合は check_layers.owner の名前つきヘッダと配置、担当表の用途、基点の Make 所属集合に直接記されたソースを使う。すべての所属候補と記入値を照合し、食い違いをパスごとに表示する。NAMED のヘッダと固定名の契約ヘッダは、その宣言・配置が定める所属と異なれば根拠欄の記述にかかわらず失敗する。その他の食い違いでは採用した種類と理由を「所属判断=種類：理由」と根拠欄に記す。例えば drivers/pci/pci.c は C_COMMON_EXEC に入るが、PCI 装置の処理を提供する。条件式・wildcard を含む像の依存全体の展開は P2 の問い合わせが扱う。

## P2 の正本と問い合わせ

ビルドの既存の選択式を維持し、所属の宣言は build/sources/ の Make 集合に集約する。既存の所属集合を再利用し、そこに現れないヘッダ・文書・資産等には同じ場所でディレクトリ既定と個別指定を宣言する。個別指定を優先し、同じ優先度の競合と未宣言は P2-2 のエラー。複数の機能集合・リンク順への登場は所有の重複に数えない。scope.tsv は初期値の引き渡しで、切り替え後に再編集する正本にはしない。[現行の選択式](select.md)

P2 が作る入口は `python3 tools/ownership.py [--revision REV] -- PATH...`。指定版（省略時は作業木）の追跡パスを対象にし、パス省略時は全件をパス順、指定時は引数順に UTF-8 の JSON Lines で返す。所有は kind と対象名 target（共通なら null）、公開は SDK の配布宣言から得る真偽、像は構成と出力の組の集合（重複なし、辞書順）。例えば sdk/kapi.json の P0 観測を表すレコードは次の形になる。

```json
{"path":"sdk/kapi.json","kind":"公開インターフェース","target":null,"header_role":null,"public":true,"images":[],"images_complete":false}
```

P2-3 は同じ所属宣言の header_role を使う。P2 への初期移行で NAMED の各ヘッダを named、fixed_name が認める実装ヘッダを fixed_contract、それ以外のヘッダを ordinary、ヘッダ以外を null とする。例えば include/pc98.h は named・機種 pc98 なので共通から読むと R4、platform_kprintf.h の各機種版は fixed_contract なので対応機種の共通から読める。異なる CPU・機種の target を読む辺にはどちらも先に R6 を適用する。移動後も宣言を引き継ぎ、検査側に旧パスの一覧を残さない。

sdk/include の 19 ヘッダでは public が true のまま images に依存先が入る。像ごとに展開した Make の入力・リンク順、コンパイラの include 依存、Cargo の crate・依存表、リンカスクリプト、生成・変換・媒体収録の前提を出力まで順方向に辿る。P0-2 の link_evidence と link_order はこの照合に使う。例えば loader.ld から通常・デバッグの ELF、boot_main.c から ELF を経た loader_hdd.bin への辺も拾う。未調査構成・解決できない条件があれば既知の辺を返し images_complete を false にする。空配列で true の場合だけ依存先が無いと言える。

パス未知・所属競合・指定版の読出し失敗は診断を標準エラーに出し非ゼロで終了。P2-2 は全追跡集合・配置・コンパイル登録を照合する。P2-3 は include 辺の両端の所属とヘッダの役割を照合する。P1-7 と run.py --select は改名前後・削除前の版も問い合わせ、欠損・不完全な依存では全件へ広げる。

## 自前の一覧を読む箇所

| 道具 | 現在の一覧 | 切替後 |
|---|---|---|
| tools/check_layers.py | DEVICE_ROOTS・NAMED と像の軸 | kind・target・header_role を include の両端で参照 |
| tools/ci/ppc_scope.py | COMMON・PPC・X86・EXACT・LAYOUT_PATTERNS | 対象名・像・依存の完全性で選択 |
| tools/source_roots.py | 検査別の SCAN_ROOTS | 所属・対象・検査の適用条件から走査パスを生成 |
| tools/checks/run.py | FULL・SCAN_ROOTS | 所属と依存で選び、登録固有の covers は検査入力として結合 |
| tools/check_arm_compile.py | KNOWN_NON_KERNEL・IMPL_DIRS と走査根 | カーネル像の所属・対象で絞る |
| tools/check_c_dialect.py | VENDOR_DIRS、SDK_HDR_DIR、公開ヘッダ・見本の glob | 第三者の所属、SDK 配布宣言の public、見本のビルド入力から対象を得る。C 方言の規則は検査に残す |
| tools/check_av_portable.py | sound/hal・input/hal の C とヘッダ | 所属宣言に HAL の役割とサブシステムを付けて選ぶ |
| tools/check_usb_portable.py | drivers/usb の走査とファイル名の接頭辞 | USB の所属宣言と実装の役割から対象を選ぶ。許される include は検査の契約 |
| tools/check_manifests.py、tools/g16/scan_ui_chars.py、tools/gen_kdebug_imports.py、tools/gen_hwdev_contract.py | 順にプログラムの入力、GUI のソース、後二者は GF のソース・ヘッダ | 各対象を共通の所属・依存の宣言から列挙する |
| tools/check_privileged.py | SCAN_DIRS の userland 以下の object | 像のユーザーランド入力から対応する object を取得する |
| tools/restructure/owners.py | KERNEL_ROOTS と調査対象の規則 | P2 の全件照合後 P7 で撤去 |
| tools/restructure/hal_inventory.py | COMMON・OWNERS・SCOPES | 役割の判断を HAL の表に渡し P7 で撤去 |
| tools/restructure/path_refs.py | ROOTS | 移動表へ参照を渡し P7 で撤去 |

tools/ の走査根の定数、glob・rglob・os.walk、git ls-files の利用箇所を照合した。check_arch_asm・check_p2v・check_kprintf_window の走査根は source_roots の行に含む。配布済み SDK を再帰してハッシュを記録する gen_sdk_manifest は生成後の媒体を読む。

全件を列挙する入口は同じ Make 宣言を展開する。個々の検査の入力・試験 fixture・像のリンク順は、それぞれの契約として残す。移動後の照合では宣言から出した全件と Git の追跡パスの差、所属が一意に決まらないパスをそれぞれ報告する。

## 構造の関門（仕様 §15）

| 項 | 受け皿 | 壊して確かめる例 |
|---|---|---|
| ソース所属 | P2-2 | 同じ優先度で異なる所属を二重宣言する |
| 不正なディレクトリ配置 | P2-2 | 共通の配置先に CPU 固有宣言のファイルを置く |
| include 依存方向 | layers-x86・layers-ppc | 共通ソースから機種専用ヘッダを読む |
| リンク依存関係 | P2-7 | PowerMac のリンク前提に x86 専用 object を加える |
| 機種独立性 | ppc-compile の common-compile | 共通 C に x86 専用の命令を入れる |
| HAL 契約 | P1-3 が書く | |
| ソース登録漏れ | P2-2 | 製品 C のコンパイル登録を外し、追跡と所属だけ残す |
| 検査登録漏れ | P2-4 | 配置表の検査本体から登録だけを取り除く |
| 生成物の再現性 | P2-7 | 同じ版・構成の二つの清浄ビルドに作業パスを埋め、バイト差を検出する |
| 配置変更時の生成物同一性 | P2-1 | 移動の際にリンク順を逆転させ、基点の生成物と照合する |
| 重複実装の候補（上の10項とは別） | P2-7 | 空白とローカル名だけを変えた同一関数を別の所有者に置き、両方の位置が候補に出ることを確かめる |

重複候補の抽出結果は人が読む。候補の出力漏れを試験で検出し、共通化・削除は個別の判断に残す。
