> 出所: ~/os32-tmp/run/ppc-draft-b05/DESIGN.final.md (astra 起草、sol 3 回、PM 確定 2026-11-21)。
> 状態: **確定 (2026-11-21)** — B05 の root と /host・mount の手続きを x86 に。

# B05　インストール媒体から HDD root へ

## r2→r3 の変更

1. 機種別 resolver が PC-98 の CHS 探索と旧配置診断、PPC の view 解決を担当する。
2. ext2 の詳細診断と既存 VFS の mount 戻り値を観測表に分けた。
3. 空ディスクの計画照合から再認識、初回登録、/hd0 mount までの入口を定めた。
4. 起動区画の初期化・配備・検読・再実行を段3の発注条件に加えた。
5. install のディレクトリ入力と cdinst のパッケージ入力を呼出順に残した。
6. 媒体と HDD の入口 ABI、早期検査、メモリ予約、P1 後の照合担当を定めた。
7. 製品の hd0 登録を OF 照合後に限定し、別ディスクを先に検出する試験を加えた。
8. 段3の host 変異と guest 起動を分け、段4に root 区画番号の不一致入力を加えた。

## 1. 実物と統合の判断

基準は TASK_PPC_ALIGN_X86 の B05、DESIGN_X86_AUDIT の 7a・D3・ATA 段8、ARCH_COMPAT_CONTRACT §9「root と /host」。段 E の共通 shell_loop と /sys/shell.bin を利用する。

|役目|PC-98 の実物|PowerMac の実物と判断|
|---|---|---|
|起動元|platform/pc98/boot.c:pc98_boot_bind が FD→fd0/fat、HDD→hd0/ext2 を boot_context に設定。kernel.c:platform_boot_machine が / を一度 mount、失敗時は停止|platform/powermac/ppc_kernel.c は root_frame_bytes がゼロなら後続も飛ばす。媒体を明示して分岐し、製品 HDD は frame 不要とする|
|RAM の所有権|HDD の mount に画像登録なし|root_image.c:pm_root_image・pm_root_release と init/boot_root.c:boot_root_start_registered は rd0 登録後に mount が失敗しても裏のページを保持する。展開・回収は lz4 の段で扱う|
|名前|boot_names.c:pc98_boot_names_bind の字句表と boot.c:pc98_boot_disk の実在登録が別|root_boot.c:pm_root_boot は rd と診断用 COM1 の表、path 初期化、mount を兼務。hd の字句・実在・区画範囲を区別する|
|子 mount|kernel.c:kernel_mount_children は登録済みの容量のある block を列挙し root を除外。kernel_boot_memory.inc:kernel_boot_memory が 任意の netcd 登録・CD ロード・kcg_module_load の直後に呼ぶ|boot_root_start_registered が /tmp、pm_root_boot が OS32_HOOK_IME 時だけ IME 用 /db・/dbu を RAM 上に作る。媒体の /tmp は維持し、製品は ext2 のディレクトリを直接使う|
|ホスト|boot.c:pc98_boot_auto_mount は HostDrv 検出時に /host を公開。SerialFS は kernel_boot_lan 内の kernel_boot_hostsvc が transport 準備後に扱う|hostfs_boot.c:pm_hostfs_boot → fs/serialfs/serialfs_persistent.c:sfs_persistent_start。LAN peer 不在でも入口を公開する。root 成功後の任意サービスに位置付ける|
|配置|userland/system/install.c:main が対象を確認して領域を準備し /hd0 に配置。PC-98 の IPL・loader を書く|rd0 に製品用ファイルが存在しても HDD は起動可能にならない。共通の配置と OF 用の起動領域書込みを分ける|

POLICY_DEV §1 の判断（レビュー観点も同じ）：

1. 同じ役目は PC-98 の boot_context、共通 VFS・Device、既存 installer と永続 SerialFS にある。P1 は ATA 後端、P2 は既定の 7 本の公開を担当する。
2. 起動元の解釈・区画の特定・ホスト transport を機種側に残し、root の mount と子の探索は一実装にする。共通側に機種マクロを足さない。
3. §9 の製品媒体・辞書パス、AC-HOST-1 の世代と AC-SFS-1 の待機・再送に従う。LAN の CALL をディスク探索や root 成功の条件にしない。

## 2. 境目と順序

以下は呼出し契約案。boot_context の既存 root_dev/root_fs を使う。

```c
/* 共通口（案）。戻り値は VFS の rc を保持する。 */
boot_mount_root(dev, fs) { return vfs_mount("/", dev, fs); }
/* PC-98 platform_boot_machine */
root 成功 → BOOT_AUTO_MOUNT (HostDrv) → BOOT_CLOCK → kernel_boot_memory
→ POST_ROOT (netcd 構成時は LAN/hostsvc → netcd、続いて CD → KCG)
→ kernel_mount_children(root_dev) → 既存 services 順
/* PowerMac pm_root_boot / ppc_kernel */
媒体: boot_root_fs_init → boot_root_start_registered → path
HDD: ext2 登録 → boot_mount_root("hd0","ext2") → path
両方: 追加装置登録 → kernel_mount_children(root_dev) → 時計・poll 準備済みで pm_hostfs_boot → shell_loop
```

boot_root_mount は rd0 登録→共通 mount まで。boot_root_start は start_registered のラッパーを維持し、後者だけが root 成功後に ramfs 登録→/tmp mount を行う。各呼出しは最初の非ゼロ rc と段名を報告し、path_set_drive 失敗を含め子の列挙前に停止。登録失敗なら展開ページを返し、登録成功なら後続失敗時も保持する。圧縮枠は展開と照合が終わった時点で回収し、失敗時も所有中の枠だけ解放する。

- root の一行は既存 VFS を共用する。rd0 登録と /tmp 作成の間にある同じ mount を二度呼ばない。既存 boot_root_* 呼出元には従来の一括手順と registered の意味を保つ。
- 媒体起動では rd0/iso9660 を /、rd1/ramfs を /tmp にする。製品起動では P1 の識別・登録成功と ext2 登録を確認し、hd0/ext2 を / にする。HDD 不在・不正な区画・壊れた ext2 は root_dev とエラーを診断し、shell の前で停止する。自動で rd0 に切り替えない。
- hd0 の sector 0 を ext2 領域先頭とする VFS 用 block view を提供する。物理 ATA の sector 0 は APM を含むディスク先頭。installer の区画操作だけが物理座標を使い、データ I/O は区画長で検査してから開始 LBA を加える。既存 provider が変換する場合は再加算しない。
- 共通口案 `int ext2_mount_range(Device *medium, u32 start, u32 sectors, Ext2Context *ctx)`（型名は実装時に現行 ctx に合わせる）。ext2_vfs の mount から解決した範囲を渡す。PC-98 は既存 find_partition_ex の CHS 結果で (raw,start,len)、PPC は (view,0,len)。後者は LBA 1 を再探索しない。読書き前に count ≤ len−offset を確認し、superblock の宣言容量超過・空範囲は INVAL、デバイス読取り失敗は IO を返す。
- 共通宣言案 `int platform_ext2_resolve(Device *dev, Ext2Range *out)`。out は medium/start/sectors。ext2_vfs_mount は resolver 成功時だけ ext2_mount_range を呼ぶ。PC-98 実装へ既存 ext2_find_partition_ex と CHS 依存を移し、legacy 検出時は既存の再作成案内を表示して NOPART を返す。PPC 実装は登録済み view を (view,0,len) に解決する。
  
|失敗入力|resolver／範囲 API の rc（ext2_vfs_mount が装置名・段名と共に診断）|ext2_vfs_mount|vfs_mount|
|---|---|---|---|
|旧配置のみ／該当項目なし|resolver の EXT2_ERR_NOPART|NULL|OS32_ERR_IO|
|空範囲／宣言容量超過|範囲 API の EXT2_ERR_INVAL|NULL|OS32_ERR_IO|
|有効範囲の読取り失敗|範囲 API の EXT2_ERR_IO|NULL|OS32_ERR_IO|

- P1 の pm_ide_boot が ATA raw を所有し、raw の列挙中は hd0 を登録しない。媒体では installer の対象計画、製品では下記の OF 照合により登録対象を決める。空ディスクは raw のみ P2 から操作可能。再認識は unmount・利用者解放後に APM を再読し同じ unit の範囲を更新、初回だけ hd0 登録。表が不正なら view を無効にする。ext2_format_at(drive,start,len) は物理 medium に対し、容量・書込権・計画した ext2 区画との一致を確認後に書く。PC-98 の IPL 保護は既存入口に残す。
- PowerMac の名前表は初期化時に一括登録し、rd と既存 COM1 の解釈を維持して hd の unit 範囲を既存 VFS 定義に合わせる。path_set_device_validator(dev_name_present) と path_set_drive(vfs_devname("/")) は root 成功後。hd の名前が解釈できても装置不在なら drive を変更できない。
- 子は /装置名 に mount。cd は iso9660 のみ、それ以外は ext2 → iso9660 → fat。失敗は次の子へ進む。導入時の整形前の hd0 は未 mount でよく、整形後に /hd0 を mount してコピーする。
|配置|導入媒体|製品 HDD|担当|
|---|---|---|---|
|/etc・/tmp|ISO 内の既定値・rd1 の一時領域|初期設定と書込可能なディレクトリを ext2 に作成|B05|
|/db/fep.db・/etc/fep_user.db|現行 ppc-ram-root.json は辞書なし。収録時だけ RO 読取、媒体での学習は無効|辞書を検読し、学習 DB は /etc に生成|FEP が入力・期待語を提供、B05 が像規則とコピーに接続|
|KCG 等の部品|担当が提供した PPC 用部品を配備表に登録|同じ配備表のパスにコピーして検読|KCG が形式・パスを確定、B05 が収録|

- /host は PC-98 の既存検出順を維持。PowerMac は期限時計と poll の準備後に既存 persistent provider を一度起動する。未接続、切断、再接続は既存 session に任せる。
- x86 生成物の差を避けるため kernel_mount_children 本体は同一の static inline として共通ヘッダへ移し、同じ呼出位置・リンク入力で比較する。差が出たら kernel_boot_memory とインライン先 platform_boot_machine、installer の main 等、実際に差が出た関数・差分を列挙して本線レビューに出す。

## 3. 実装段とファイルの分担

|段|変更先（新設名は案）|到達点と依存|
|---|---|---|
|1　装置名と root 選択|platform/powermac/boot_names.c は名前表、root_boot.c は root の組と VFS/path、ppc_kernel.c は起動元と初期化順。init/boot_root.c と include/boot_root.h は rd0 登録・mount の既存 API を保って手順を分離|媒体で回帰を先に取る。HDD 実行は P1 と区画 view 待ち。空 frame を媒体選択の根拠にしない|
|2　子と /host|include/boot_mount_children.h は既存の列挙本体、platform/pc98/kernel.c はその参照、PowerMac の root_boot.c は同じ列挙を呼ぶ。hostfs_boot.c は transport の起動を担当|段1に接続。PPC は追加 block 登録後に子、続いて /host を公開|
|3　rd0 から導入|userland/system/install.c は検査・整形・配置の既存手順、機種別の固定名 platform_install_boot.h は下記の対象検査から起動配備までの機種別操作、PowerMac 側の区画 provider は hd0 の範囲を担当|P2 と §5 決定後。対象・境界・書込み可否・媒体の必須ファイルを確認してから既存の消去確認。ext2 作成→/hd0 に製品 shell・設定・辞書を配置し検読→OF 用ローダとカーネルを配備。途中失敗は成功表示を出さない|
|4　HDD 単独起動|PowerMac の起動引渡しと ppc_kernel.c を段1の選択に接続。既存 guest 起動試験に媒体取外しと再起動を追加|段3のディスクを再利用し、OF→ローダ→hd0/ext2→共通 shell を通す。常駐 CLI・旧 kboot 5像の撤去は全体の最後|

install は媒体ツリーの必須物検査・copy_directory の見積り → 対象検査・確認 → 準備・起動配備 → copy_directory のコピー。cdinst は report_missing_packages・measure_packages(choice) → 対象検査・確認 → 準備・起動配備 → install_packages(choice)。共有するのは対象計画・確認・検読・同期の手順で、各入力の検査・配置は既存入口が担う。固定名 platform_install_boot.h の機種別実装は check_media、probe_plan、describe_erase、release、prepare、write_boot を供給し、確認対象の drive・消去範囲を計画に保持する。確認拒否、対象変更、読戻し不一致は書込み開始前または当該段で停止。
PC-98 は既存媒体検査（IPL/loader を含む）→inst_hdd_check/check_media→describe・既存確認/ask_erase→release→prepare（必要時消去、整形、区画表更新・検読、再 mount）→write_boot→配置・検読・sync。PPC は OF 入力を検査し、承認済み計画で整形・APM 更新検読→再認識・mount→配置検読→起動配備検読・sync。段3の PPC installer は P2 の既存 disk KAPI と書込権判定を通し、PPC 用 SDK/startup と機種実装をリンクして導入 ISO の /sys に収録する。

段3の PPC installer からの呼出順（新設入口名は案）。
1. probe_plan は P2 の ide_init → ide_drive_present → ide_identify → ide_read_sector で raw unit・容量・現行 APM を採取し、媒体の必須物・予定区画・消去範囲を照合する。空の APM は新規計画として提示する。
2. 消去確認後に sys_umount_checked（既存 mount のみ）と dev_mount_count で利用を解き、対象を排他確保。新設の機種別管理要求 begin(plan) が raw 識別・現行表・権限を再検査し、同じ対象を prepare 完了まで保持する。既存 disk KAPI の公開とこの管理要求の installer から kernel への配線は別々に段3で検証する。
3. prepare は計画した ext2 範囲を ext2_format_at で整形し、ide_write_sector で APM を更新して ide_read_sector で検読。新設要求 rescan(plan) が kernel 内で同一 raw の APM を読み直し、型・開始・長さが計画に一致するときだけ view を初回 dev_register_hdd（再実行なら同じ view を更新）。sys_mount("/hd0","hd0","ext2") 成功後に配置する。失敗時は古い範囲を公開せず排他を解く。
4. §5 で HFS 案を採用した場合、B05 の像作成側が決定済み形式・長さで空 HFS を初期化し、所定パスにローダと製品 ELF を入れた起動区画像を導入 ISO に収録する。write_boot は承認済み起動区画との長さ一致を検査して像を書き、全量を読戻し比較し、HFS 内の両パスと内容を原本と照合する。中断後は対象と消去範囲を再確認して同じ像を先頭から配備する。形式・区画長・パス・像生成担当と mac99／G4 の OF 読出し証跡を PM が記録して段3を発注する。別案採用時も同じ入出力と再実行条件を確定する。

段4の入口 ABI 案。媒体 ELF と製品 ELF はビルド時に入口契約を固定する。
- 媒体は現行どおり r5=OF CI、r3/r4 は参照しない。kernel_entry.S が CI を退避し、stack 設定・BSS 消去後、pm_verify_entry_pair を ppc_kernel_start より先に呼ぶ。不正なら entry_stop。成功時は CI を復元し、媒体の record/frame 範囲を予約してから展開する。
- HDD ローダは r5=OF CI、r3=引渡しレコード実効アドレス、r4=バイト長で製品入口へ分岐する。製品 ELF に専用の固定長引渡し領域（BSS・stack 外）を設け、ローダがその領域に記入。入口で三値を保存し、読出し前にアドレスと長さがリンク済み領域に一致することを検査する。BSS 消去後、製品用早期検査で magic/version/size・HDD 種別・bootpath 長・起動/root 区画番号を検査し、不正なら entry_stop。媒体 record/frame LOAD の混入は像検査で拒否する。
- ppc_kernel_start は CI と検査済みレコードを受け、最初のページ割当前に kernel PT_LOAD と引渡し領域を予約する。製品では root frame を予約しない。kboot_capture で CI から /chosen/bootpath を採取し、ローダが渡した起動パスと照合する。ppc_kernel_start が pm_ide_boot の raw 列挙後、OF 装置パス・unit と同じディスクの APM 上の起動/root 区画番号・型・範囲を照合する。一意に一致した root の view だけを hd0 に一度登録し mount。不一致・曖昧なら hd0 登録前に停止する。

|先着・基点|接点|後発の担当|
|---|---|---|
|段 E 1088dbda4・lz4 58770721e・ATA P1 f5e187e6b が main 入り|ppc_kernel.c、root 像規則、powermac.mk|PM が三者を含む main SHA を記録し段1と x86 比較の基点に固定。B05 が shell・展開・ATA を結線|
|段1→段2、P2→段3|raw KAPI・view・installer リンク|P2 の公開7本が着地後、B05 が導入像で呼出確認|
|B04→段2|hostfs/poll|B04 が再接続を所有し B05 が root 後に起動|
|B06→段1の shell 接続|terminal/input|B06 が端末を準備、B05 が root 成功後に共通 shell へ渡す|
|B10→段4|起動診断|B05 が媒体・停止段・rc を B10 の出力口へ接続|
|段4→B07|像・受入|B07 が導入済み像と OF 証跡を製品受入に統合|

260–600 行・7–13 ファイルは B05 の見積り。ATA P1/P2、lz4 の展開・像作成、段 E の端末・入力を再実装しない。区画・ローダの新規実装量は §5 の実機確認後に別計上する。

## 4. 受入と完了条件

|環境|観測する結果|期待の出所・赤にする変異|
|---|---|---|
|host、boot_root_host と mount の記録|rd0 登録失敗・登録後 mount 失敗を区別。root 失敗なら子・shell に進まない。root 自身、非 block、容量ゼロを飛ばし、残りの名前と試行順を照合|boot_root_start_registered と kernel_mount_children。registered を消す、root 除外を反転、ext2/ISO の順を交換し各々実行時に赤|
|host、区画付き使い捨て像|先頭・最終 sector の既知値を読み書きし、終端越えを拒否。外側の番兵、APM、起動区画を照合|区画表の開始・長さから独立に算出。開始加算の削除、終端の一つずれで赤|
|両 CPU の QEMU|i386 は共通 VFS/子列挙を QEMU runner で実行、PC-98 の FD/HDD 実起動回帰は NP21/W。mac99 は rd0 の書込拒否、/tmp 書込、導入時の /hd0 と /host の切断・再接続を観測|PC-98 boot_bind・mount 手順、AC-HOST-1/AC-SFS-1。peer 不在でも shell に到達し、再接続後に古い handle を拒否|
|mac99 の電源再投入|導入媒体を外し、同じ HDD のみで /sys/shell.bin を起動。/ の装置は hd0、/hd0 の二重 mount なし。/etc に識別値を書き再起動後に再読する。辞書と学習の公開試験は FEP 配備後に接続|§9 と ime_store.h。rd0 強制、/etc の RAM 化で赤。破損 ext2 の負例では製品 shell に到達しない|

段別 CHECK（追加名は案）。host 変異と段4は `make check-<CHECK> MUTATE=1`、段3の guest は通常の make check 入口で実行し、各変異は指定の分岐一か所だけに当て、実行時の観測で赤にする。

|段|CHECK|観測と変異アンカー|
|---|---|---|
|1|boot-root-host、ppc-root-select-host|test_boot_root.py の既存 API fixture を追従。/tmp 失敗後も registered、path 失敗後 shell 0 回。PPC の path エラー返却を成功に変える|
|2|boot-children-host|実列挙に root・空・CD・HDD を登録し試行列と host 開始を記録。root 除外条件を反転|
|3|ppc-install-disk-host|使い捨て像で新規導入、拒否時に全 sector 一致、書込失敗後の再実行で検読成功。view の最終 sector 判定を一つ狭める。判定は host の最終 sector 読書きで観測する|
|3|ppc-install-disk-run|mac99 に ISO と空 HDD を接続し /sys の実 installer を起動。初回 hd0 登録・mount・配置検読・完了を serial で確認し、同じ HDD を段4へ渡す|
|4|ppc-of-hdd-run|媒体と -kernel を外し、OF bootpath→HDD ローダ識別値→hd0→shell を記録。起動ファイルを除いた対照は shell 未到達。別の正常な ext2 ディスクを先に列挙させ、OF 指定側の固有ファイルを読む。負例ではローダが root 番号だけを同じ APM の HFS 番号に替え、区画不一致診断・hd0 登録0回・shell 未到達を要求。番号と型の一致を要求する判定を無効化すると停止段の観測で赤|

hdd-stage2-host の既存導入順、ppc-root-lz4-layout-host の媒体/製品別 LOAD、ppc-link-sh-run と PPC build fixture のリンク入力・ISO 内容を各変更段で追従。x86 は同じ toolchain・設定・入力で基点と候補の kernel/install/cdinst をバイト比較し、差があれば map・逆アセンブルで関数単位に帰属させる。許容差は取り込み前に本線 PM の合意を記録。NP21/W の FD/HDD 起動と既存 install/cdinst 導入回帰を必須とする。

完了は対象 G4 の OF が導入済み HDD を単独で読み shell に達したログと、両媒体の通常起動と失敗経路、導入後の永続化、/host 不在時の起動、x86 生成物比較を記録した時点。設計稿ではビルド・起動・変異は未実施。

## 5. 未決（ユーザー判断 1 件）

- **PowerMac の製品 HDD の起動区画を APM＋小さな HFS とし、別区画を ext2 root にするか。** この構成を推奨する。OF が ext2 を直接読める前提を置かず、OF が読むローダ・カーネルと OS32 が読む hd0 を分けられる。docs/ppc/ata.md §7 が示すのは mac99 の IDE 接続候補まで。採用前に mac99 と対象 G4 で OF の起動パス・読める HFS 形式・ローダ引渡しを確認し、区画のサイズと配置を確定する。

PM は OF 資料と mac99 で HFS 形式・ローダ位置・起動パス・root 選択を絞り、対象 G4 での読出し観測と、消去する範囲・既存区画を保存できる条件を示してからユーザー判断を求める。実ディスクへの書込みは対象と範囲の明示承認後に行う。

## 確定 (2026-11-21、sol 第 3 回、PM が反映)
以下は本文に優先する。sol 第 3 回 (/home/hight/os32-tmp/run/ppc-draft-b05/REVIEW.r3.out.md) の「上記を反映すれば可」の残りを、そのまま実装の取り決めとする:
  - P1・§2–3・P1 f5e187e6b の pm_ide_boot は raw 列挙中に dev_register_hdd(i) を呼ぶ。同関数は物理 unit i を hd<i> として登録するため、OF が unit 1 を選んでも既存の呼出しでは unit 0 が hd0 に残る。B05 の変更先に raw 登録の分離と、論理 hd0 を選択済み raw unit に結ぶ view 登録を明記する。
  - P1・§3 手順2–4・同じ番号の raw unit と論理 hd 番号を前提にできない。dev_mount_count は VFS の論理番号を数えるので、raw unit 1 が既に hd0 のとき unit 1 を数えても既存 mount が残る。view の対応表から利用を確認し、承認した raw と範囲を write_boot の検読・sync まで保持して、失敗・中断時も解除する。begin/rescan の公開 ABI と契約の担当も記す。
  - P1・§3・段3 ISO はローダと製品 ELF を含むが、製品入口の実装は段4に置かれ、段4は段3の導入済み HDD を要求する。段3が収録する起動成果物の生成を先行条件に置き、段4の受入で同じディスクを使える順序を明記する。
  - P2・§3・install/cdinst の共通順に「起動配備→コピー」と書く一方、PPC の契約は「配置検読→起動配備」。共通段の記述から write_boot の位置を外し、機種別の順序で固定する。
  - P2・§4・root 番号を HFS 番号へ置換する負例では、番号照合だけが抜けても型照合が停止させる。番号照合を変異対象にするなら、同じ APM に別の有効な ext2 を置き、その番号だけを渡す。期待する root 区画の根拠も契約に置く。
- 未決 (ユーザー): PowerMac の製品の HDD を APM + 起動用の小さな HFS + ext2 の root に分けるか (推奨のまま書いてある、ユーザーの判断を待つ — 段 3 の installer の段の前に決める)。
- ユーザー決定 (2026-11-21、PowerPC のセッションで直接「承認」): 未決の HDD の区画は推奨の案 A — APM の区画表で、Open Firmware が読む起動用の小さな HFS の区画と、ext2 の root の区画に分ける (HDD だけで起動できる形、x86 と同じく「HDD に入れたら HDD だけで起動」)。理由: G4 の Open Firmware は ext2 を読めず HFS/HFS+ (と CD の ISO9660・TFTP) だけを読む。G4 の実機の HDD に区画を作るのは、中身を確かめてから改めて承認 ([D2])。
