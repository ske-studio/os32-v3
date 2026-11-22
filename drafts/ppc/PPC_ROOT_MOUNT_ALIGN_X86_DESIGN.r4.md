# PPC_ROOT_MOUNT_ALIGN_X86_DESIGN — B05　導入媒体から HDD root へ

## 11-21 の本線のレビューとの突き合わせ

- P1 管理 ABI（§5・§6）。本線が段3前に begin/rescan/end と既存 I/O の排他を実装する。PowerPC は承認済み計画を渡し、検読・同期後の終了と途中中断の両方で解放を確認する。
- P2 導入経路（§4–6）。本線が inst_hdd／inst_disk と hdprep の機種境界を整理し、PC-98 の空ディスク検査、確認、区画末尾の試し書きから表更新までの順を固定する。
- P2 ext2（§3・§6）。本線が段2前に区画解決を分離し、短い区画と宣言容量超過の直接戻り値、診断、キャッシュ初期化順を回帰で固定する。

照合基点は origin/main の 56d7be02d。7a に関する補足の私有 ops は kernel/ime/fep_module.h で現在は版 6（24 スロット）。B05 の受入は §2 のカーネルと部品の組で行う。

## 確定版 (11-21) → r4 の変更

1. 段 E は main 入り。root 成功後は owner 1・CPL0 の常駐シェルから既存の shell_loop に入る。
2. lz4 root は main 入り。導入媒体の展開・登録・頁回収を現行の入口に任せる。
3. ATA P1・P2 は main 入り。IDE を含む像だけで公開済みの ATA 7 口を使い、raw と論理 hd0 の対応を B05 が作る。
4. MACHCFG は main 入り。媒体と HDD の双方で seed の後、選んだ root から設定を一度読む。
5. KAPI v89 の公開頁と v90 の末尾 39 口に追従し、ディスク管理を追加する場合の基点を v90 に更新する。
6. 入力 M5〜S4・7c は main 入り。既存の入力・前景・回収経路で導入確認を読み、公開独占は要求しない。
7. C04 共通の probe snapshot と KDEBUG 依存検査は main 入り。root の診断は既存の出力口を使う。
8. PPC の L2 と kdebug 窓口は wip/ppc-c04-diag、wt/int-ppc-1122b で CI 中。対応するカーネルと部品の組を配備する。
9. B10 と B07 段1は wip/ppc-align-b07。通常製品の成功ログを前提にせず、旗なし製品の出力先から像を取る。
10. USB in2 段 A・B は wip/ppc-usb-in2。HID の差分・反復と共通 HAL の配送に導入時の打鍵を通す。
11. HDD の最終配置は APM＋起動用 HFS＋ext2 に決定。TFTP＋HDD root の中間段を先に置き、実機書込みと NVRAM 変更を別承認にする。

照合基点は origin/main の 69d9cef40。11月21日の両稿とリポジトリ版末尾の確定事項を本文に統合した。本線 sol レビュー用。枝の成果は取り込み時に PM が SHA と CI 結果を記録する。

## 1. x86 と起動順を揃える

PC-98 は platform/pc98/boot.c の pc98_boot_bind で起動元から root_dev/root_fs を選び、kernel.c の platform_boot_machine が一度 mount する。FD は fd0/fat、HDD は hd0/ext2。HostDrv の自動 mount、時計、kernel_boot_memory の netcd・CD・KCG、kernel_mount_children、services の順を保つ。子の列挙本体だけを共通ヘッダに移し、同じ呼出位置で root・非 block・容量ゼロを飛ばす。CD は ISO、それ以外は ext2→ISO→FAT を試す。子単体の失敗は次の子へ進む。

PowerMac の platform/powermac/ppc_kernel.c は現在 root_frame_bytes がゼロの経路を持ち、pm_ide_boot は RAM root 処理後に呼ばれる。起動指定を媒体・HDD に分け、HDD の raw 探索を root より前に移す。旧診断像の frame 無しの契約はその像に残す。製品起動での不一致・不在・破損は装置と失敗段、rc を診断し停止する。

|起動元|root まで|root 成功後|
|---|---|---|
|導入媒体|現行の入口照合→圧縮枠予約→root_image.c の pm_root_image→root_boot.c の pm_root_boot→boot_root_start_registered。rd0/iso9660 を /、rd1/ramfs を /tmp に mount|登録後は後段失敗時も rd0 の裏頁を保持。pm_root_release に registered を渡し、圧縮枠・未登録の展開頁を所有権どおり回収|
|HDD|引渡しを検査→ATA raw 同定→APM 照合→選んだ view を hd0 に登録→ext2 を / に一度 mount|/etc・/tmp・/db は ext2 のディレクトリ。RAM 像の予約・展開は不要|

名前表には rd・既存の COM1 と hd を登録する。字句の登録と装置の実在を分け、root 成功後に path_init、path_set_device_validator(dev_name_present)、path_set_drive(vfs_devname("/")) を通す。path 失敗も shell 前で停止。MACHCFG は pm_machcfg_seed の値を基に kernel/machcfg_root.c の machcfg_load_root で実際の / の設定を反映する。ファイル欠落時は同関数の既定処理。追加装置登録後に子を列挙し、時計・poll 準備後に pm_hostfs_boot で persistent SerialFS を公開する。peer 不在でも常駐シェルへ進む。

段 E の shell_loop_run(&pm_shell_loop_ops) と /sys/shell.bin のロード・復帰を使う。installer はこのシェルが起動する通常プログラム。USB 枝統合後も確認入力は通常の CUI を通し、専用 FIFO を追加しない。B10 の ESCC・LAN に失敗を送り、成功はシェルから / の装置・ファイルを読むことで観測する。B07 の旗なし IMAGE=powermac は build/out/ppc-powermac、シェルはその user/。旧名の検査では旧出力先を使う。

## 2. 共通 FEP と配備

**そのまま使う所。** kernel/sqlite_engine.c:520–558 の辞書欠落時 SQLITE_ONLY、fep_meta_probe、reserve 検査、kernel/ime/fep_module.c:181 の fep_module_load(sqlite_handle)、公開・失敗時の巻戻しを両機種で使う。kernel/ime/ime_store.h の既定辞書 /db/fep.db、大小の辞書、学習先 /etc/fep_user.db と設定による変更がパスの正典。部品の形式・辞書の生成・期待語は FEP 担当、B05 は配備と永続性を担当する。

**直す所。** PPC の root_boot.c の OS32_HOOK_IME 用 /db・/dbu RAM mount は従来の診断像に限る。製品では SQLite・FEP の対応する部品、辞書と設定を同じ配備表から ISO と導入先に入れ、L2 が照合するカーネルと部品の組を保つ。媒体で辞書を読む場合は学習を無効、製品では /etc に学習して再起動後に候補を読む。C04 は診断用 KDEBUG=1 の構成で照合し、通常製品の起動条件にしない。共通 FEP 本体と x86 の配備表に変更予定はなく、x86 の kernel・部品・辞書はバイト一致見込み。

## 3. 共通 ext2 と区画の境目

**そのまま使う所。** fs/ext2/ext2_vfs.c:425 が ext2_mount に渡す論理番号、fs/ext2/ext2_super.c:274 以降の superblock・容量・グループ検査、既存の file/dir/inode I/O を共用する。fs/ext2/ext2_fmt.c:260,279 の ext2_format と ext2_format_at は登録済み ext2_medium_ops で範囲を確認して同じ整形本体を呼ぶ。PC-98 の ext2_medium_ide.c の ide_find_part は BIOS 幾何を要求し、min_lba は IPL を保護する。loop の別 ops も維持する。

**直す所。** mount が直接呼ぶ platform_ide_drive_present、ext2_dev_for、ext2_find_partition_ex を機種別の範囲解決へ移す。新設案の境目は次の形とする。

```c
int platform_ext2_resolve(int logical_drive, Device **dev, u32 *base, u32 *len);
```

ext2_mount は read cache の reset → mounted なら unmount → resolver の順を維持する。PC-98 の resolver は成功時の幾何の出所と旧配置拒否時の移行案内を従来どおり出す。PC-98 は既存探索が返す raw Device・CHS 起点・長さ・旧配置の案内を維持。PPC は view Device・0・区画長を返す。共通 mount は ctx の dev/base_lba/part_len を初期化し現行の検査へ進む。区画欠落・短すぎる区画は NOPART、magic 不一致は MAGIC、宣言ブロック数が区画容量を超えた場合は従来の容量診断を出して EXT2_ERR_NOPART、装置読取失敗は IO。詳細 rc は VFS アダプタで記録し、mount の NULL と vfs_mount の OS32_ERR_IO は従来どおり。x86 の意味・媒体形式は同じ。関数分離による kernel の命令配置・map 差を見込む。

PPC の view は各 I/O の前に offset ≤ len、count ≤ len−offset を調べ、物理開始を一度だけ加える。APM は disk 容量内の表全体を検査し、空・重複・重なり・終端越えを拒否する。整形は raw 座標。PPC の HD 用 ops が承認済み計画と unit・start・len の一致、書込権を確認してから現行 ext2_format_at に渡す。raw は ide_boot.c の pm_ide_boot で発見する。現行の dev_register_hdd(i) をこのループから分離し、選択後に論理 hd0 を raw unit 0 または 1 の区画に結ぶ。空ディスクは導入時に raw だけ見える。

## 4. 共通 hdprep

**そのまま使う所。** userland/shell/cmd_hdprep.c:172 の cmd_hdprep の引数解釈、計画表示・hp_ask_yes・利用確認・整形・検読・hp_verify_mount の手順、hdprep_plan.c の hdprep_parse_mb と hdprep_check_mounts を共用する。PC-98 では hdprep_check_geom、hdprep_check_disk、hdprep_plan（同:72）、probe の書戻し照合、pc98pt_put の区画表更新を現行順で行う。

**直す所。** cmd_hdprep 内の固定 drive 0・BIOS DA・LBA 1・幾何計算・表の書込みを機種別の計画と実行に切り出す。同じ機種別実装を inst_hdd 側からも使う。PPC は選択した raw と元の APM、HFS/ext2 の各番号・型・開始・長さ、変更範囲を提示する。データを保存すると表示できるのは全書込範囲と保存区画が交差しない時だけ。hdprep の完了は整形・表の検読・再認識・mount 検査まで。起動ファイルは install が配備する。x86 shell.bin と同じ計画コードをリンクする installer は呼出し配置に差が出うる。PC-98 の受入では空ディスク検査失敗と確認拒否で全 sector が一致し、承認時は区画末尾の試し書き・読戻し→整形→区画表更新の順となり、試し書きの照合失敗で整形前に止まることを確認する。

## 5. 共通 install と排他

**そのまま使う所。** userland/system/install.c の precheck_media・measure_need・copy_directory はディレクトリ媒体、cdinst.c の report_missing_packages・measure_packages(choice)・install_packages(choice) は選択したパッケージを扱う。inst_hdd.c の inst_hdd_check/check_media・describe・ask_erase・release・prepare・write_boot の PC-98 手順を維持。install.c:605 と cdinst.c:218 の prepare 後、PC-98 は IPL/loader を書いてからコピーし、shell を検読・同期する。

**直す所。** 起動資材の事前検査・対象計画・書込みの機種依存部分を platform_install_boot.h の境界へ出す。inst_hdd が両入口の導入手順を共有し、inst_disk の容量見積り・パス検査は共通に残す。inst_disk の PC-98 媒体判定・IPL 補正・区画表生成と hdprep の CHS 計画は機種側に置く。両入口で入力を検査し必要容量を見積もってから消去範囲を表示する。PPC は確認→排他→整形→APM 書込み検読→再認識→/hd0 mount→製品ファイル配置検読→HFS 起動像書込み検読→同期の順。ディレクトリとパッケージは各入口のコピー関数を使う。x86 install.bin・cdinst.bin は分離した関数と呼出位置でバイト差の可能性がある。媒体上の IPL・loader・kernel、導入結果と失敗順を比較する。

PPC の installer を通常の SDK/startup と IDE 構成でリンクし、導入 ISO の /sys に収録する。P2 の公開済み raw 操作で IDENTIFY と表を読み、消去拒否時は全 sector を保持する。管理 ABI は本線 KAPI 担当が宣言・生成・dispatch、B05 が機種ハンドラと呼出元を所有する。公開口は int32_t begin(const InstallPlanV1 *plan, uint32_t *handle)、int32_t rescan(uint32_t handle)、int32_t end(uint32_t handle)。begin は元表の識別情報と対象 unit・変更範囲を含む計画をカーネルにコピーして保持し、成功時だけ owner 専用 handle を返す。rescan/end は他 owner・失効済み handle を拒否する。ABI は固定幅の値とサイズ付き計画をコピーして検証し、成功は 0、権限・不正入力・利用中・I/O の各失敗は既存 OS32 エラーで返す。既存 slot の意味を変更せず sdk/kapi.json 末尾追加・版上げ・再生成・clean build を同段に含める。x86 にも公開表・SDK・ABI 依存バイナリの差が生じるため、この差を関数分離と別に記録する。

begin の前に view 対応表から全論理 mount を調べ、root が対象なら拒否、既存の子だけ checked unmount する。begin は owner・raw 識別・元表・範囲・利用を再照合して確保し、他 owner の raw 書込みや view 更新を拒否する。rescan が同じ unit の表と計画の一致を確認して初回だけ登録、再実行は同じ view を更新する。古い view は表変更前に無効化する。排他は当該 owner の配置・検読用 mount を許し、HFS 全量の読戻し・ファイル検読・sync まで保持する。失敗・中断・owner 終了でも解放。再実行は現物を読み新しい確認から始める。

## 6. 実装順と受入

各行は一実装者が所有する。新規ソースのリンク登録は作成段に含める。共通コードは機種ヘッダを include せず、各機種の実装をビルド時に選ぶ。

|段|変更先と到達点|観測・変異|
|---|---|---|
|1 媒体起動|root_boot.c は mount/path、boot_names.c（新設案）は字句、ppc_kernel.c は順序を担当。既存 boot_root API と MACHCFG を維持|boot-root-host・ppc-root-lz4-layout-host・ppc-link-sh-run/flow。登録前後の失敗を分け、path 失敗を成功にする変異で shell 到達を検出|
|本線の段（段2前）ext2|§3 の resolver と共通範囲検査を分離。完了後に PowerPC 段2を開始|直接 rc・診断・reset 順、VFS の失敗を PC-98 で比較|
|本線の段（段2b前）子 mount|PC-98 の子 mount を共通化。完了後に PowerPC 段2bを接続|PC-98 の root 除外と子 mount を確認|
|2 HDD root|ide_boot.c は raw 探索、区画 view と APM 解決は各一ファイル。§3 の resolver を接続。TFTP からの試行では明示した HDD 識別・区画を使う|別 raw を先着させ、選択した ext2 の固有ファイルを読む。先頭加算・最終 sector 判定を変異。範囲外と番兵を確認|
|2b 子と /host|kernel_mount_children を共用し、PPC は追加 block の後に呼ぶ。B04 の hostfs/poll を利用|root 除外を反転し二重 mount を検出。peer 不在で shell、再接続後に旧 handle 拒否|
|本線の段（段3前）導入経路|§4–5 の hdprep／install／cdinst の境界整理。PC-98 の受入完了を段3の前提とする|確認拒否時の無書込み、試し書き失敗、起動配備順を比較|
|本線の段（段3前）管理 ABI|§5 の公開口・排他・owner 終了処理を確定し生成・実装。完了後に PowerPC 段3を開始|他 owner の既存 I/O による書込み拒否と中断後の再取得を検査|
|3a 起動資材|製品入口・OF ローダ・製品 ELF・HFS 像を先に作る。B07 の出力先から組にして導入 ISO に収録|使い捨て像で OF の HFS 形式・区画長・両ファイルのパスと内容を検読。媒体用 LOAD の混入を像検査で拒否|
|3b 導入|本線の管理 ABI と導入経路に、PowerPC の機種別計画→hdprep→install/cdinst の順に各機能を一人に発注。§4–5 の順で段3a の資材を配備|host は拒否時の全 sector 一致、途中失敗後の排他解放・再導入、境界変異。mac99 は空 HDD と ISO から実 installer を実行し初回 mount と配置を確認|
|4 HDD 単独|段3b の同じディスクを電源再投入。媒体・TFTP・-kernel を外して OF から起動|/ が hd0、/hd0 二重 mount 無し、/etc の値と学習が再起動後も読める。起動ファイル欠落・破損 ext2 は shell に達しない|

製品入口は r5=OF CI、r3=固定引渡し領域、r4=長さ。領域を stack/BSS の外に置き、参照前に番地と長さを検査する。BSS 消去後に magic/version/size・起動種別・パス長・区画番号を照合し、最初の割当前に PT_LOAD と引渡し領域を予約する。媒体は r5 と既存の pm_verify_entry_pair を維持する。HDD 単独では /chosen/bootpath から起動ディスクと HFS を同定する。TFTP 段ではネットの bootpath と明示指定された HDD の機器パスを別々に保持する。

導入時に HFS 内に root の番号・型・開始・長さと起動区画番号を記録し検読する。製品入口で比較する期待値は配備した同計画の製品設定から独立に取得する（製品 ELF の予約済み設定欄へ配備時に記入して検読）。ローダの申告、期待値、実 APM が一意に一致した時だけ hd0 を登録する。同じ APM に別の正常な ext2 を置き、引渡しの root 番号だけをそちらに変える。番号照合の単独変異を観測できるよう、まず番号を比較し、不一致時は番号専用の診断を出す。変異時は後段の範囲照合が停止しても診断の違いで赤になる。

x86 は基点と同じツール・設定で kernel、shell、install、cdinst、起動媒体、SDK/部品を比較する。差を map・逆アセンブル・ABI 世代で関数または生成規則に帰属させる。hdd-stage2-host、ext2 の partition/medium/read-bound/write 系、hdprep の拒否と install の順序、FEP store、PPC の MACHCFG と build profile、各段の変異アンカーを追従する。NP21/W で FD/HDD 起動と install/cdinst の導入を観測する。製品に受入専用の分岐・成功行を足さない。

旧見積り 260–600 行は root と導入の接続部分のみ。ローダ・APM/HFS・管理 ABI は各段で別計上する。像のサイズ差は実装後に kernel/FD の空きと PPC の ELF・圧縮 ISO・HFS の使用量を実測する。完了証跡は mac99 と対象 G4 の HDD 単独起動、永続化、上記の負例と x86 回帰。今回は設計稿のためビルド・起動・実機操作は未実施。

## 7. 未決

設計上のユーザー判断は 0 件。G4 の HDD 書込みは内容・対象・変更範囲を確認した時点で個別承認を取る。NVRAM の boot-device 更新も別件として値と復元方法を提示し承認を取る。中間段は一回限りの起動指定で検証できる。
