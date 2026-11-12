# PPC_RAM_ROOT_W6_PLAN — 段 W6 (PC-98 の FD 起動を、PowerPC と共通の読み取り専用の RAM root へ) の計画

> 状態: **計画確定 (2026-11-12)・本線 PM のレビュー待ち** — 共通の部品の段 (C1〜C4、PowerPC 側) と PC-98 の FD 側の段 (P1〜P4・PB、本線が手すきの時に起こす) に分け、案 B (root を別置きの RAM へ運ぶ) を推奨。A か B か・pair の契約・CdModuleApi の版上げは本線 PM、FD の中身を削るかはユーザー (2HD の実測で分かれる条件付き)。段の状態は票 TASK_PPC_SHELL が持つ。

> 出所: ~/os32-tmp/run/ppc-w6/PLAN.final.md (gpt-6-astra 起草 r1→r2、Opus 5.5 の第 1 回 Request changes・第 2 回 条件付き Approve、PM が C3・C4 と N1〜N3 を反映)。前提は [PPC_SHELL_DESIGN](PPC_SHELL_DESIGN.md) の末尾の確定 (P1-1・P1-2・P2-1〜P2-3)。

r1→r2 の変更
- R1：A の .mod pair 循環を成立条件へ格上げし、現行 pair を維持できる B を第一推奨とする。
- R2：インストーラの kernel 読込み元と `fd_renames`／`fd_only` の追従を P3/P4 に加える。
- R3：2HD の既存使用量から製品 root の容量下限の目安を示し、内容削減を現在の判断事項とする。
- R4：`CdModuleApi.fs` の除去・版上げと、全起動元に共通する ISO 登録の1か所を明記する。
- R5：FD/HDD の共通 kernel と pair を比較軸に加え、B の kernel 二重格納も容量に算入する。
- R6：起動後の FD 読書き経路を表にし、自動アクセス抑止と明示利用を分けて受け入れる。
- R7：C4 は mkpkg の FD 名写像に揃え、ISO 作成道具が無い PC-98 build は失敗とする。
- R8：FD の外側 FAT と内側 ISO を検査する形へ package 試験・空き容量検査を改める。
- C3：PM 決定を反映し、共通 init が ISO を登録、`boot_root_start()` は root と `/tmp` の mount を担う。
- C4：PM 決定の名前写像を採用し、正規パスを読む利用側とインストーラの逆写像まで整合させる。

対象：`/home/hight/wt/os32-v3/ppc-w6`、提示された基準 `origin/main 3569d3df`。
r1 の変更不要部分は維持し、レビューに関係するコード・文書だけを追加確認した。
共通部品は PowerPC 側が設計し、本線 PM が確定版をレビューする。
PC-98 の起動・配備変更は本線が起こす。本書は読み取りだけの計画である。
シングルタスク・協調切替・整数中心を維持する。

**1. 実物の確認**

以下の「設計」は `docs/decisions/ppc/PPC_SHELL_DESIGN.md` を指す。
r1 で確認済みの食い違い5件は維持する。未実装の接続とは別に数える。

|番号|設計・前提|実物と計画への反映|
|---|---|---|
|差1|設計:164,168 は RAM root を cd0 とする|`drivers/block/ram_ro.c:32`、`init/boot_root.c:11` の rd0 を採用|
|差2|設計:168 は boot_root 内で ISO 登録|現行登録は `platform/powermac/root_boot.c:14`。共通 init へ移す|
|差3|設計:369 は rdN 対応を追加する段を記載|`fs/isofs/iso9660.c:377` は既に `VFS_DEV_RAM` を受ける|
|差4|設計:180 は S-B で hello.bin 追加までを記載|`build/ppc-kboot.mk:127` は hello.bin と fault.bin を同梱済み|
|差5|設計:371 の vmkernel.lz4 293KiB|過去値としてのみ使用。現在の容量合格を意味しない|

|維持する基礎|根拠・契約|
|---|---|
|RAM block と root|`ram_ro.c` の範囲検査・write 拒否、`boot_root_mount()` の rd0 mount を再利用|
|共通 ISO|既存 parser と `VFS_DEV_RAM=5` を使い、複製しない|
|書込み先|`fs/ramfs/ramfs.c` の既存固定上限で `/tmp` を分離|
|PC-98 初期化順|`init/kernel.c:187` の root mount が同:199 の `kernel_boot_memory()` より先|
|FD 搬送|`boot/loader_fat_new.asm` は ELF でなく VK32 の VMKRNL.LZ4 を読む|
|入力制限|同:446 の508KiB、物理 `0x10000〜0x8EFFF`、64KiB 境界でセグメント更新|
|別置き候補|`include/memmap.h:396` の `0x600000`、1MiB の boot bundle 帯|
|boot wire|`platform/pc98/bootinfo_wire.h:6,37` は v2・64B、image 欄の予約は4B|

追加確認：`bootinfo_check.c:104` は単一版との一致判定で、現状のまま v3 は受理しない。
`tools/mkmod.py:196–207,296` は VK32 全体の検証済み CRC を pair に使用する。
`install.c:55` の kernel 読込み元の実名は `/VMKRNL.LZ4`、HDD 側は `/boot/vmkernel.lz4`。
GPL の資料・コードおよび `docs/archive/` は参照していない。

**2. 共通部品の段――PowerPC 側**

共用するのは ISO 形式・装置・mount 契約であり、PPC と x86 の実行ファイルは別に作る。
★は x86 生成物が変わる段。行数は変更・追加の概算で、試験は別計上する。

|不足|境目・対象|概算行|★|
|---|---|---:|---|
|C1 静的 ISO の build の口|新規 `build/ram-root.mk`、既存 ISO 非module経路|30〜60|有効化時★|
|C2 root image/blob の build の口|同 mk、`build/ppc-kboot.mk`。ISO と任意の blob object を生成|40〜70|有効化時★|
|C3 共通初期化と mount|`include/boot_root.h`、`init/boot_root.c`、PowerMac wrapper|40〜70|接続時★|
|C4 内容・容量・名前の契約|`mkroot_image.py`、mkpkg 連携、容量確認用 host check|50〜90|入力変更時★|

C1：共通側は kernel 用 flags の静的リンク対象を提示する。
対象は `ram_ro.c`、`boot_root.c`、`iso9660.c`、`iso9660_media.c`、`ramfs.c`。
module 用 object を kernel に流用せず、ISO parser は kernel 常駐に一本化する。
本線 P1 が cd.mod との所有関係・ABI・imports を同時に切り替える。

C2：ISO 作成と blob 化を分け、A/PPC は blob、B の PC-98 は別置き ISO を使う。
blob は `objcopy -I binary` に形式・CPU を渡し、`.rodata.boot_root` へ置く。
platform linker が KEEP と開始・終了シンボル、常駐寿命を保証する。
A の依存は契約解決後に ISO→object→kernel→VK32→FD 像とする。
B の依存は kernel→paired .mod→staging→ISO→FD 像とし、循環を作らない。

C3：ISO 登録は起動元によらない共通 init の1か所へ集約する。
提案する `boot_root_fs_init()` にだけ `vfs_register_fs(&iso9660_ops)` を置く。
PC-98 は `BOOT_DEV_FS` 完了後・root mount 前に、FD/HDD 共通でこれを呼ぶ。
PowerMac も VFS 初期化後に同じ関数を呼び、wrapper の直接登録を削除する。
`boot_root_start(image)` は ISO 登録済みを前提に、root mount→ramfs 登録→`/tmp` mount を行う。
rd 名前登録と image の予約・写像は platform 側に残し、低水準の `boot_root_mount()` は維持する。
失敗は呼出し元へ返す。PC-98 は root panic とし、起動 FD root へ自動退避しない。

C4：内容表は mkpkg の FD 選択・除外・名前写像を唯一の入力とする。
ISO level 1 の8.3名に揃え、別の手書き rename 表を生成側に増やさない。
既存の2048B整列検査を再利用し、必須ファイル・名前衝突・実際の読出しパスを検査する。
`mkroot_image.py:24,29–30` の道具不在 rc=3 を PC-98 build の成功や SKIP に変換しない。
PC-98 の `make all` と CI に genisoimage または xorriso を必須化し、不在なら明確に失敗させる。
PPC の hello/fault 用 root の容量を PC-98 製品 root の見積もりへ流用しない。

**3. A/B の再比較と推奨**

|比較軸|A：kernel に root を同梱|B：root を別置き RAM へ搬送|
|---|---|---|
|R1：.mod pair|root 内 .mod→最終 VK32 CRC→.mod の循環。現行契約では通常の build 順序が成立しない|先に kernel を確定し、その CRC の .mod を ISO に入れる。現行 pair を維持可能|
|R2：インストーラ|自分を含む ISO に同じ最終 VK32 は格納できない。別の搬送・RAM 保持契約が必要|ISO に最終 VMKRNL.LZ4 と HDD boot 部品を格納でき、FD 抜去後に install 可能|
|R3：容量の下限の目安|約490KiB級の内容に ISO 丸めが加わる。半分に圧縮できても293+245≒538KiBで508KiB超過の見込み|kernel 単体の508KiB制限は残る。インストーラ用 kernel を含む ISO は約789KiB級＋ISO 差分が目安|
|R5：FD/HDD 共用|共通 kernel なら HDD にも root が常駐。FD 専用 kernel に分けると別 pair 集合と install 方針が必要|同一 VK32 と同一 .mod 集合を両方で使用可能。FD 外側と ISO 内に kernel を二重格納する|
|常駐・予約|blob を `MEM_KERNEL_IMAGE_MAX` 内へ入れる|候補1MiB帯を予約し、root mount 前に写像する|
|ローダ・wire|既存搬送を使えるが、R1/R2 の解決方式によって追加搬送が発生|高位への反復転送と boot 情報 v3 が必要|
|推奨|pair 変更と install 搬送の解決、容量実測が揃った場合の代案|第一推奨。現行 pair・インストーラ・共通 kernel を保てる根拠が強い|

B を推奨する。ただし容量・高位転送を確認済みという意味ではなく、本線 PM の選択前提とする。
A を選ぶ場合は「root に .mod を入れない」だけで済ませず、必要 module の RAM 供給方法を示す。
cd・kcg・cirrus・trident・lan・v86 のうち製品内容に含むものを FD の FAT に残して後読みしてはならない。
A の pair 契約変更は本線の module ABI 設計で扱い、CRC の無条件無視で解消しない。

**4. B の FD ローダ実現性と容量**

`loader_fat_new.asm:218–244` は ES:BP の低位宛先へ1セクタずつ読み、64KiB ごとに更新する。
同:890–913 の `read_sect16` は INT 1Bh を使う。この経路で6MiBへ直接読む設計にはできない。
同:291–329 に A20 有効化・GDT 設定・保護モード移行があり、高位アドレスを扱う土台はある。
確認範囲では BIOS 読込みへ戻る往復経路はなく、既存の一方向移行をそのまま反復には使えない。
B は低位 bounce→一時的な保護モードで高位へコピー→実モードへ復帰、の追加を候補とする。
BIOS 再呼出し時の segment・stack・割込み状態、32bit register 上位破壊を含めて保存契約を決める。
DMA の64KiB境界を維持し、kernel 圧縮入力・FAT 作業域・loader・boot 情報を上書きしない。
root を先に高位へ搬送して低位帯を再利用し、その後 kernel を既存経路で読む順序を第一候補とする。
追加コードが loader 配置・読込み枠に収まることも検査する。grep 確認だけでは実装量を確約しない。
新たな機種依存手順の確定時は HW3 に従いメーカー資料と BSD/MIT 実装を確認する。

boot 情報 v3 は root の物理 base・bytes・妥当性情報を明示幅・offset で定義する。
4B の予約欄へ base と bytes を詰めず、64B の現行 wire と256B の予約域の境界を守る。
C/NASM 定義、capture のコピー長、版別 check 語、parser を一緒に更新する。
HDD の v2 は受理し、FD の v3 は root 記述を必須にする。未知版・欠損・範囲外は拒否する。
B の root 記述と、既存の kernel CRC/size による pair 情報は別物として保持する。
pgalloc 初期化直後・一般割当て前に image 帯を予約し、root mount 前に P2V で写像する。
`kernel_boot_memory()` 全体の単純前倒しは避け、root 後の module 読込みとメモリ初期化を分離する。

容量の根拠は `docs/08_build.md:767` の sqlite.mod 除外後、2HD 空き約443KBとする。
媒体1232KiB−443KiB≒789KiBが管理領域を含む使用量の概算である。
789−過去の kernel 293−loader 等で、従来 root 内容は約490KiB級という下限側の目安になる。
これは個々のファイル長を集計した厳密な下限ではない。FAT 管理領域・割当丸めは分離して測る。
ISO のファイル単位2048B丸めと directory 分が増え、FAT 管理分との差引きも必要になる。
B で install 用 kernel を内包すると、外側全体は概ね789+293≒1082KiB級から評価する。
そこへ ISO/FAT の差分を足すため、2HD の空き64KiB条件も1MiBの RAM 帯も実測前に合格とはしない。

|対象|制限・合否|
|---|---|
|A の VK32／B の kernel VK32|header・整列込みで520,192B以下|
|A の常駐像|blob・BSS・整列込みで `MEM_KERNEL_IMAGE_MAX` 以下（現行0x275000）|
|B の ISO|候補の1MiB帯以下。install 用 kernel と必須 .mod を含める|
|FD 像|2HD 1,261,568B／1.44MB 1,474,560B内、外側 FAT の空き64KiB以上|
|`/tmp`|既存 ramfs 上限を維持し、ISO 容量と別に heap 消費を確認|

主な先行制約は508KiBの搬送と媒体容量であり、常駐像上限だけを根拠に A を可としない。

**5. PC-98 接続と起動後の経路**

P1：`cd_body.c:27` の `out->fs` と `CdModuleApi.fs` を除去し、装置側 API にする。
`cd_module.c:29,33,58` の FS pointer 検査・登録・解除と対応する参照保持を削除する。
装置 pointer の body 内検査、登録・解除・rollback と使用中の unload 拒否は維持する。
`CD_MODULE_OPS_VERSION` と関連 module ABI 世代を更新し、旧組合せは明確に拒否する。
`build/cd-module.mk` から ISO object を外し、kernel 側へ移す。重複登録エラーを握り潰さない。
HDD 起動でも共通 init が ISO を登録し、cd.mod の有無や unload が RAM root の FS を消さない。

P2/P3：B の image accessor、予約・写像、FD 起動時だけ rd0/iso9660 を選ぶ接続を追加する。
rd0/rd1 を名前表へ加え、子 mount へ渡す root 選択情報も更新する。
`kernel_mount_children()` の起動 FD 自動 mount・探索を止め、保存媒体は明示 mount にする。
P3/P4：install の3入力を RAM root から読み、`fd_renames` と `fd_only` を新しい構成へ追従させる。
B の外側 FAT は loader・VMKRNL.LZ4・root image、内側 ISO は利用内容と install 入力を持つ。
外側だけの搬送ファイルを HDD コピー集合へ混ぜず、同じ kernel と paired .mod を HDD へ渡す。

|起動後の経路|扱いと必要な変更|
|---|---|
|root の shell・設定・module 読込み|rd0 に向ける。遅延読込みする必須ファイルも ISO に含める|
|子 mount・FD 自動探索|起動 FD を対象外にする。抜去済みでも probe/read を発生させない|
|hsync `hsync.c:194,2503`|同期元の root 読込みは rd0。rd0 への書込みは失敗のままにし、既存 FD 書込み拒否も維持|
|filer `cmd_filer.c:78–98`|短名 fallback を今回の rd0 root に対応。`/etc` と `/` が同じ装置という条件を維持|
|`drivers/base/dev.c:61,279` の fd0|装置登録は維持。登録と自動 mount を分け、明示利用だけを対象にする|
|kcg の ROM/cache|`kcg.mod` は rd0 から読む。`kcg_body.c:16,68` の RAM cache と ROM glyph 読出しは FD I/O 対象外|
|`pc98_boot_font()`|現行の KCG 初期化を維持し、cache を永続 FD storage として扱わない|
|install|3入力とコピー対象を rd0 から読む。HDD への書込みは従来の承認手順を維持|
|bootlog_save|ext2/FAT の保存先限定を維持し、ISO root は対象外。FD 自動 mount は復活させない|
|root 設定保存・一時ファイル|root 書込みは失敗のまま。一時ファイルは `/tmp`、永続保存は明示した書込み可能媒体|
|後から明示 mount した FD|利用者の明示 read/write は別扱い。通常起動・抜去受入ではアクセスゼロを測る|

名前写像は「rd0 なら何でも短名へ退避」とせず、FD 用 root の契約に限定する。
`/sys/shell.bin` 等の既存正規パスと ISO 内の実名が一致することを内容検査で保証する。

**6. 段と検証**

|担当・段|対象・概算行|依存・試験|
|---|---|---|
|共通 C1〜C4|§2 の対象、計160〜290行|静的link、両CPUの生成依存、名前・必須内容・道具不在|
|共通 CT|関連 host 試験100〜180行|rd0 read、root write拒否、`/tmp`、変更試験のmutation|
|本線 P1 ★|ISO所有・cd ABI、80〜140行|共通登録、HDD ISO利用、旧ABI拒否、CD unload/rollback|
|本線 P2/PB ★|loader・wire・image予約、詳細設計時に再見積もり|高位転送、v2/v3、境界・範囲・台帳衝突|
|本線 P3 ★|起動選択・利用側・install、100〜180行|root前予約、FD自動I/Oなし、短名、install入力|
|本線 P4 ★|FD像規則・staging、60〜110行|kernel→module→ISO の依存、両形式の実容量|
|本線 PT|関連試験180〜300行|loader境界、packages、抜去、HDD install後の起動|

R8：`test_packages.py` の外側 FAT 集合を「MINIMAL−exclude」とする前提を変更する。
外側は搬送集合、内側 ISO は mkpkg の FD 内容集合として、それぞれバイト列まで照合する。
case 7 の両形式、7b の D88/RAW 同一性、case 9 の install 逆写像・HDD 集合を追従させる。
`FD_MIN_FREE_KB=64` は外側 FAT の実空きで維持し、ISO の整列余白を空きへ数えない。
別に ISO 収容上限・kernel 二重格納の同一性・全 .mod の pair 一致を検査する。

実機受入は機種・FD形式・成果物識別値を記録し、root と `/tmp` の mount 後に FD を抜く。
未読ファイル・外部コマンド・必要 module・filer を使い、`/tmp` 書戻しと root 書込み拒否を確認する。
起動後の自動 FD read/write がゼロであることを計測し、明示した保存媒体への保存も確認する。
抜去したまま install の入力を読めること、HDD へ導入後に同じ kernel/pair で起動することを受け入れる。
NP21/W は先行確認とし、実機抜去受入の代替にしない。
実装時は指定環境・preflight、`make all`、変更試験の `--mutate`、`python3 -B tools/checks/run.py --lint`。
ABI 変更時は `make clean`→`make all`。mutation anchor を移し、新規試験は CHECK に入力を宣言する。
landing は GitHub CI、全 mutation は full-check。check-changed は実行しない。
今回は読み取り計画のため preflight・build・試験・配備は未実施、ファイル変更もない。

**7. 未決――現在決める事項**

|決定者・件|推奨と判断材料（各件3行以内）|
|---|---|
|本線 PM：A か B か|B を推奨。pair循環・install自己包含・FD/HDD共用を解消できる。高位転送/v3の追加範囲と実容量を判断材料にする|
|本線 PM：pair の契約|B では現行の最終 VK32 CRC pair を維持。A を選ぶ場合は module ABI と識別対象を再設計し、検証を弱めず循環を断つ|
|本線 PM：CdModuleApi の版上げ|`fs` 除去と版上げを推奨。新旧混在拒否・装置寿命・HDD ISO登録を同時に受け入れる|
|ユーザー：FD の中身を削るか|まず現行集合を保つ B を推奨。削減候補ごとの容量・失う機能と2HDの64KiB余裕を示して判断し、fd.exclude を独断で増やさない|

C3 の登録境界と C4 の名前写像・道具必須化は PM 決定済みで、未決へ戻さない。
未決は本線 PM 3件、ユーザー1件。実物との食い違いは r1 から5件を維持する。
反論：なし。
r1→r2 の変更の数 / 反論の数：10 / 0


## 確定 (2026-11-12、Opus 5.5 の第 2 回の条件付き Approve を PM が反映)
- N1 (P2): B の FD のローダの段 (PB) の詳細設計の最初の確認項目に、1MB より上への転送の候補として (a) PC-98 の BIOS の拡張メモリへのブロック転送 (INT 1Fh AH=90h とされる。[HW3] に沿ってメーカー資料で確かめる)、(b) unreal mode、(c) r2 §4 の PM への往復 (最大の見積もり) を並べ、資料で確かめられた手段で見積もり直す。
- N2 (P2): B は kernel を 2 回持つ (外の FAT と ISO の中)。2HD は 789KB + 293KiB + ISO の丸めで使える 1168KiB にほぼ届くか超える見込み (推定)、1.44MB は収まる見込み。2 回持つのを避ける案として「v2 の img_size (platform/pc98/bootinfo_wire.h:34) を使い、V86 より前に kernel が低位の VK32 を写して install に渡す (ISO に kernel を入れない)」を候補に足す。ユーザーへの推奨は「まず今の中身のまま B。2HD の実測で超えたらこの案か中身の削減を、容量と失う機能を示してユーザーが決める」の条件付きにする。
- N3 (P3): MEM_BOOT_BUNDLE (include/memmap.h:388-396) は「ブート必須モジュールの展開先」として T5b/T6b が使う予定の帯。W6 の root に使うなら、P2 で用途を「W6 の root」に書き換えるか STAGING (0x500000) と分けて使うかを決め、memmap.h の注釈と tools/tests/memory_boot_host.c:401-407 を追従させる。
- 未決の振り分け: 本線 PM 3 件 (A か B か、pair の契約、CdModuleApi の版上げ。推奨は B・pair は今のまま・fs を外して版上げ)。B はローダと boot 情報 v3 で範囲が広がるので、本線 PM が B を採る時点でユーザーに知らせる (スコープの拡大)。ユーザー 1 件 (FD の中身を削るか、N2 の条件付き)。
