> 出所: ~/os32-tmp/run/ppc-align-b07/DESIGN.final.md (設計ラリー 3 回、astra 起草・sol レビュー、2026-11-22 確定)。
> 状態: **確定 (2026-11-22)** — 票 TASK_PPC_ALIGN_X86 の B07 を x86 に寄せる。末尾の「確定」の節は本文に優先する。

# B07　製品と受入で同じ起動媒体を使う（設計 r3）

## r2→r3 の変更

- P1 受入 API　各ケースの最終 ELF を梱包し、起動前に ISO から抽出して照合する。
- P1 CI　旧 selector 試験を維持し、新製品の KB_ELF をレイアウト検査へ明示する。
- P2 層検査　新旧の依存出力を走査し、新製品側だけの違反で検出を確認する。
- P2 互換選択　tx0 と link-sh の出力期待値と変異を選択器と同時に移す。
- P2 ゲスト受入　通常起動で READY・USB 列挙を待ち、再入力はシェル再ロード後とする。
- P2 現行 CI　1,0 と 1,1 の共通のリンク・監査と旧旗の正規化を明記する。

## r1→r2 の変更

レビューの掲載順を ID とする。

- 01 P2 製品の選択／試験の選択　CI の四呼出しを三つの実効構成に対応付けた。
- 02 P1 製品選択　名前だけの指定、部分指定、環境値と衝突の解決順を定めた。
- 03 P1 出力とリンク　機能表からの旧マクロ導出、ユーザー出力、再リンク依存を定めた。
- 04 P1 受入 API　ELF 専用利用者を保ち、媒体利用者と Session 派生を同時移行する。
- 05 P1 配布契約　all・iso・配布の各入口から監査成功を要求する。
- 06 P1 lz4／ATA　両方とも main に統合済み。組で発行された ELF を使い IDE=1 を継承する。
- 07 P2 B04／B06／段 E　基点を更新し、本線担当との受渡しを特定した。
- 08 P2 既存検査　fixture、変異の移設、二本に分割されたゲスト検査と CI の依存を記載した。
- 09 P2 x86 同一性　比較識別子、両 FD、私有 HostDrv と NHD の比較手順を固定した。
- 10 P2 無改変のゲスト受入　配布媒体で入力、結果行、再起動後の再入力を画面から判定する。
- 11 P2 完了条件　B07 の着地と全体最終段の撤去に個別の合否を設けた。

## 1　基点と責務

比較元は origin/main の 58a8015f5。ユーザー決定（11-20）に従い、動作実績のある x86 の「選択した製品をビルド側が媒体にして渡す」順に揃える。変更境界は Make とホスト側の受入である。

|責務|PC-98|PowerMac の現物|
|---|---|---|
|構成|build/select.mk → sys/conf/images/pc98.mk → 所有者別集合と legacy 順|同じ選択器が先に sys/conf/images/ppc-flags.mk を読み、最後の override IMAGE の結果で機能表と kboot 順を読む。無指定は link-sh。|
|リンクと内側の root|build/targets/x86-pc98.mk の all と build/sources/order.mk|build/targets/ppc-powermac.mk の KB_LINK_COMMAND、build/ppc-root-pair.mk の grouped target。exec 像は root-input.iso から最終 PPCKERN.ELF と root.iso を組で発行。kboot_audit.py が最終 ELF と PPCKERN.map を検査。|
|媒体と配布|build/image.mk の FDD_IMAGE_ARGS と fd144、build/deploy.mk の deploy-fd と deploy-nhd|kb-user がユーザー像を組み、ROOT_MANIFEST が /sys/shell.bin を配置。外側の CD は tools/ppc/link_sh_run.py の Session.start と ata_accept.py (旧版 52972d5d2d51) の guest が個別生成。PPC の deploy は未対応。|
|通常入力|製品 FD/HDD でシェルを起動|sys/platform/powermac/shell_loop_ops.c の共通ループ用操作と fb_run.py:observe の USB キー・画面観測。link_sh_run.py:main の normal は settings(observe_flow=True) の計測用。|

compile_ci.py (旧版 3d07969b3073):run の現行呼出し（SH, LINK 順）は次の通り。最後に 1,1 で common-compile を呼ぶ。

|引数|実効像|検査するもの|
|---|---|---|
|0,0|kboot-core|最小核のリンク・監査、診断と負試験の土台|
|0,1|kboot-link|LAN link 診断のリンク・監査|
|1,0|kboot-link-sh|既定 all で link＋ユーザーシェルのリンク・監査。旧 SH 呼出しを 1,1 に正規化|
|1,1|kboot-link-sh|既定 all で link＋ユーザーシェルのリンク・監査|
|1,1 common-compile|同じ選択|製品に入らない common ソースの PPC コンパイル|

段 E で更新する設計前提は三点。①旧 CLI 注入で網羅性を測る前提を廃し、各ランナーが実際に組む像を計測する（2133bcd6c）。②link-sh の場合集合は run と flow に分割済み（fcab33d80）。③端末の基準画面は HELLO 後の TIME 完了を待ち、診断の画面漏れも検査する（4111773d5、509f3e813）。共通ループへの既存接続（547dda500、ef7ffef67）を保持する。lz4 と ATA の着地はこの三点とは別の本線差分である。

## 2　製品の選択、リンク、発行

build/select.mk の PPC 分岐で初期値を代入する前に origin を保存する。以下は互換解決の Make 骨格。resolve は下表と現行 ppc-flags.mk の検証順を表す。

```make
requested := $(IMAGE)
flags_given := $(strip $(foreach v,KBOOT_LINK KBOOT_SH KBOOT_SHELL,$(if $(filter-out undefined,$(origin $(v))),$(v))))
IMAGE ?= $(PLATFORM)
# resolve は入力値を変更せず、正規化した旧名を返す
effective := $(call resolve,$(requested),$(flags_given))
# requested があれば許可名・実効構成の一致をここで検査
override IMAGE := $(if $(filter powermac,$(requested)),powermac,$(if $(strip $(requested) $(flags_given)),$(effective),powermac))
include build/images/$(IMAGE).mk
```

|入力 origin／指定|解決|
|---|---|
|IMAGE・三旗が undefined|powermac。NEGATIVE の非ゼロは製品では拒否。|
|IMAGE のみ command line または environment|powermac は製品。旧名は LINK,SH,SHELL の順に core=000、link=100、sh=010、shell=001、link-sh=110 を与える。未知名・空の明示値は拒否。|
|旗あり（command line 優先、次に environment）|不足分は LINK=1、SHELL=0。SH は LINK と SHELL がともに未指定の時だけ 1、他は 0。各値は単一の 0/1。例 LINK=0 は core、SH=1 は link-sh、SHELL=1 単独は LINK の既定と衝突。|
|名前＋旗|旗ありの不足分は前行と同じ。LINK＋SHELL、SH＋SHELL、非 core の NEGATIVE を正規化前に拒否。SH または SHELL が 1 なら 110 に正規化。要求名の対応する実効構成と比較し不一致は失敗。powermac＋110 は製品出力、旧 sh/shell＋110 は旧 link-sh 出力。|

x86 の既定 pc98 と選択順は現状維持。powermac.mk に現行 kboot-link-sh.mk の全値を移し、旧ファイルは include 一行。IDE、LOOP、LINK、USB、EXEC、USERSHELL は 1、CLI は 0。製品のリンク順・provider は旧 link-sh と同じ。旧入力の解決後、target で次の値を使う。

```make
KB_EXEC := $(filter 1,$(FEATURE_EXEC))
KB_C_LEGACY := -DKBOOT_LINK=$(FEATURE_LINK) -DKBOOT_SH=$(FEATURE_USERSHELL) -DKBOOT_SHELL=$(FEATURE_CLI)
KB_SH_OUT ?= $(KB_OUT)/user
# kb-user の再帰 make に PPC_USER_OUT=$(KB_SH_OUT) を渡す
KB_SELECT_MK += sys/conf/images/ppc-flags.mk build/targets/ppc-powermac.mk
ifneq ($(filter powermac kboot-link-sh,$(IMAGE)),)
KB_SELECT_MK += sys/conf/images/powermac.mk
endif
```

KB_OUT の既定は製品なら build/out/ppc-powermac、旧入口なら既存の core（負試験は neg番号）・link・link-sh の場所。check_layers.py の PPC 依存走査には ppc-kboot と ppc-powermac の両方を登録する。既存旧入口の既定ユーザー出力も維持し、製品・私有出力では上式を使う。派生 manifest の /sys/shell.bin と ROOT_DEPS は同じ KB_SH_OUT/shell.bin を参照。ネット用 include は FEATURE_LINK、生成 KAPI・RAM root・PM_ROOT_FRAME は KB_EXEC。IDE/LOOP/USERSHELL の OS32_FEATURE 定義は既存通り。KB_SELECT_MK はオブジェクト、ELF、grouped target の依存宣言より前に完成させる。

```make
BOOT_ISO := $(KB_OUT)/boot.iso
ifeq ($(IMAGE),powermac)
all: iso
endif
iso: audit $(BOOT_ISO)
$(BOOT_ISO): $(KB_ELF) build/targets/ppc-powermac.mk | audit
	# mktemp -d の空ディレクトリに最終 ELF を PPCKERN.ELF としてコピー
	# genisoimage -quiet -hfs -r -part -no-desktop、固有一時ファイルから rename
deploy-iso: iso
	# 明示した ISO_DEST へ一時コピーし成功時に rename
```

iso と deploy-iso を phony にする。生成は make ARCH=ppc PLATFORM=powermac all、再監査は同じ変数で make audit、配布は make deploy-iso ISO_DEST=/私有先/boot.iso。配布が失敗したときは後続起動を止める。QEMU は Session の既存 mac99・ROM・USB・LAN 引数を使い、-cdrom にその ISO の絶対パス、-prom-env に 'boot-device=cd:,\PPCKERN.ELF' を渡す。配布物と入力の SHA-256 を記録する。

## 3　移行単位

|段|同じ変更で移す台本|段の受入|
|---|---|---|
|1 選択・出力|test_ppc_tx0.py:exercise の selection は PPC_FLAGS_MK の差替えを実効選択器まで届ける。test_ppc_link_sh.py:selection は旗なしと明示旧旗で KB_OUT・KB_SH_OUT を別々に取得。|旗なしは製品の核・ユーザー出力、旧旗は従来値を期待。旧 KBOOT_LINK ?= 1 や旧出力式への変異を新しい既定選択・核出力・ユーザー出力の各式に移し、誤った選択またはパスで RED。check_layers.py:measure は新出力だけの .d に禁止 include を入れて違反を確認し、除去後は通す。|
|2–3 媒体・受渡し（同時着地）|ppc_boot_info.py:main は unverified_image / negative_image の後、observe の前で最終 ELF をケース別に梱包。|共通の外側 ISO 梱包処理は入力 ELF と出力 ISO を引数に取り、CD 内名を PPCKERN.ELF に固定。各ケースで抽出したファイルと実際の最終 ELF をバイト比較し、同じ媒体で 7450・7455 の既存 observe の停止 PC・出力・終了値の各判定を保持。|
|4 CI|ppc-kboot-layout.py:run は旧 --product-selectors 後に IMAGE=powermac を明示して製品をビルドし、同じ選択の KB_ELF を kboot_layout.py --product に渡す。|旧名の同値性と新製品 ELF・隣接 map/root.iso のレイアウトを各々検査。|
|5 通常起動|fb_run.py:observe の計測ケースと並行して、配布 ISO 用の入力順を §4 の通り検査。|READY・USB 列挙、ローカルへの復帰、first、シェル終了・再ロード、second を順に確認。|

1. 選択器・出力の切替と同時に test_build_selection.py の kboot-query、fixtures/build_profile_ppc.json、私有 Make ツリーへのコピー一覧を更新する。旧 LOOP 変異のアンカーを powermac.mk に移す。旧名→旗と明示名照合の各アンカーは新しい対応式へ移し、旧 sh の選択結果が変わること、矛盾が誤って成功することをそれぞれ RED とする。順序・所有者抜け・link 無効の変異を保持。
2. lz4 → ATA の合流済み target を土台に外側 ISO 規則を追加。中間 ELF や root-input.iso を外側に置かない。ATA 受入の guest はこの段で共通 ISO を IDE index=2 に渡し、試験ディスクと absent ケースを保持。既存の root pair の片側欠落時の再生成を維持する。
3. 同じ変更で受入側を移す。kboot_accept.build(out,*settings) は ELF 利用者向けの既存契約を保つ。新しい build_media(out,*settings) は同じ make 呼出しに iso を渡し、成功コード、監査成功、三ファイルの存在を確認して下記を返す。配布済みの通常受入ではビルドせず、配布 ISO とその元の ELF/map を渡す。リンク後加工ケースでは build の ELF と root.iso を従来通り加工し、段表の共通梱包処理で Media の elf と iso をそのケースの最終成果物に揃える。map は元リンクのものを渡す。Session.start の CD 作成を削除し、ELF のシンボル読出し・ハッシュ記録は維持する。

   ```python
   Media(elf=out/'PPCKERN.ELF', map=out/'PPCKERN.map', iso=out/'boot.iso')
   Session(media, cpu, evidence, timeout)
   ```

   旧手動受入 の ppc_{boot_info,console_cells,db,display,display_lifecycle,font,fp,gfx2,kcg,loop,mouse,newlib,path,pci,port,rtc,sfs,shell_layout,shm,sqlite_static,surface,sysinfo,ver}、tools/ppc の link_sh_run（LineSession、ResetSession を含む）、fb_run、ppc_sh_output、sb_run、sc_run、sd_run、usb_run と checks/ppc-reset.py の受渡しを一括変更。レイアウト・実機 ELF・core の専用メーターなど ELF だけを読む入口は build を使用する。診断の build-only でも媒体を要求する経路には genisoimage が必要。
4. compile_ci.py は core、link、明示製品 all、製品 common-compile を要求。旧 sh/shell の同値性は選択検査で毎回確認する。ppc_ci.py:matrix の期待引数は実装の IMAGES を読まず固定し、一段ごとに異なる失敗コードを返して後段未実行とコードの伝播を検査。製品抜け・失敗無視の変異で RED。build-setup と PPC CI の製品生成ジョブに genisoimage を配置。変更する checks の covers に選択器、共有機能表、target、媒体 API、各 runner を、needs に対象のクロス環境・ISO 生成ツール、ゲスト検査には ROM と QEMU を反映する（各登録の既存形式に従う）。ppc-link-sh-run と ppc-link-sh-flow の両方で場合集合と変異を保つ。
5. B04（設計中）は platform/powermac の LAN poll・rshell 操作、link_sh_run.py の ready/close、B06（未着手）は shell_loop_ops.c と fb_run.py の端末・USB 観測が接点。これは予定の接点で、担当側の編集ファイルと着地 SHA は未確定。統合時は LAN 接続・閉鎖後の手元入力、TIME 完了後の画面判定を担当間で突合し、既存の LAN OFF／USB 再接続の負試験を維持。B07 から製品側へ観測フックを足さない。


- (確定の追記、sol 第 3 回 P1) CI の `--product` には新製品の `KB_ELF` と一緒に、同じ Make の選択で得た新製品の `KB_SH_OUT/shell.bin` を渡し、`kboot_layout.py (旧版 ca41853c713a)` の `report_product()` は ISO の中のシェルをその新しいパスと照合する (旧 `build/out/ppc-user/link-sh/shell.bin` の固定参照をやめる)。
- (確定の追記、sol 第 3 回 P2) 層の検査の走査の根を足す段で、`test_layers.py (旧版 dbb7eed86d0b)` の `test_generated_kapi_and_build_headers` は新旧の両方の根に正常な `.d` を置き (`empty == []` を保つ)、違反の場合は新製品の側の `.d` だけに違反を入れて検出を確かめる。

## 4　合否

- PPC の host 検査は製品と旧 link-sh の順序付き入力・機能を比較し、配布 ISO を抽出して PPCKERN.ELF とバイト比較。共有表の更新による再リンク、ISO 規則更新による再発行、audit 失敗時に既存配布先が更新されないことも確認する。古い ELF を梱包する変異は抽出比較で RED。
- 配布用無改変 ISO の絶対パス・SHA-256、ELF・map の組を証跡に残す。fb_run.py:observe の local、finished、shot 相当を使い、通常起動では before_shell_input() による保留を使わず、RSH READY と serial-a の protocol=01 を両方待つ。close_rshell() が ESC を一度送り、rshell_active が 0 で画面に [Remote shell closed] が出た後、 USB で echo first を打つ。入力行の下の独立した first 行と次の OS32:/$ プロンプトを待ち、画素でも本文を照合。exit 入力直前のシリアル末尾から /sys/shell.bin returned 0、続いて次の Loading /sys/shell.bin を順に待ち、echo second でも second 行と新プロンプトを確認する。計測用 normal とこの受入は別々に実行する。私有出力で CD 内の名前だけを変更する変異では OF が指定パスを開けずカーネルに到達しない記録を要求し、一般的なタイムアウトだけでは RED としない。
- x86 は 58a8015f5 と変更後で同じツール、tools/build_compare.py の OS32_BUILD_COMMIT=layout-compare、SOURCE_DATE_EPOCH=946684800、TZ=UTC、LC_ALL=C、MUTATE=0 を使う。双方で make all と make images/os32_boot.d88 fd144。kernel・vmkernel.lz4・shell.bin、2HD と 1.44MB の先頭ブートセクタ、全ファイルの媒体内パスと SHA-256 を比較する。D88 はセクタを取り出して比較。双方で同一原本から私有 NHD と HostDrv を複製し、NP21W_DIR、HOSTDRV_DIR、OS32_NHD_LOCAL を各側の私有先に固定、profile 使用時は REMOTE・MOUNT・STAMP も私有先へ。各側で deploy-nhd（prune の both を含む）を実行し、NHD のブート領域と ext2 の配備パス・内容を比較する。32 bit host 試験は qemu-i386、製品 FD/HDD の起動は NP21/W で確認し、未実施は明記する。
- B07 の着地は選択・リンク・監査・配布・無改変 ISO 起動の一致、旧診断の継続、x86 同一性で判定する。変更 check の --mutate は各アンカー一意かつ期待する実行時 RED、登録 lint と ppc-ci-image を通す。最終撤去は別段。build・platform・tools・CI・配布手順に残る旧 5 名、旧旗、CLI シンボルの全利用者を移し、各旧名・旗を単独指定して明示的に拒否し、通常製品のビルド・配布・受入が通った時に完了する。

本稿は設計でありビルド・ゲスト試験は未実施。反論は 0 件。


## 確定 (2026-11-22、sol 第 3 回「条件付きで可」、PM が反映)
- P1・§3: 製品のシェルのパスの受け渡し (KB_SH_OUT/shell.bin を --product と一緒に) を §3 に追記。
- P2・§2–3: 層の検査の新旧の根の fixture を §3 に追記。
