## r1→r2 の変更

> 出所: ~/os32-tmp/run/machcfg/DESIGN.final.md (gpt-6.2-astra 起草 r1〜r2、claude-opus-5-5 のレビュー 2 回、確定の直しは PM)。行番号つきの参照は往復の版にあり、ここではパスだけにした (POLICY_DEV §9)。
> 状態: **現行 (2026-11-18)** — 確定設計。段の状態は票 TASK_MACHCFG が持つ。ユーザー決定 (2026-11-18): §9 の未決は「当面は ELF の再生成でよい」(推奨どおり)。

- F1　sysconfig の解析を共用し GF 設定を分離。
- F2　host 試験の配置を訂正。
- F3　HID0 の CHECK 20・22 を実装と対で更新。
- F4　system.cfg の利用者と配布経路を補完。
- F5　パスを sys/include/config.h に集約。
- F6　PVR と platform 既定の二段に修正。
- F7　両機種の起動時の呼出位置を訂正。
- F8　一回読みで全体超過を検出。
- F9　FS なし・mount 失敗の確定とリンク群を指定。
- F10　RAM root の配布と ELF 再生成を明記。
- F11　DPM を残す実機根拠を追加。
- F12　AC-BOOT-3 を第一候補に変更。
- F13　実機操作の承認と延期時の記録を追加。
- F14　性能要求の暫定既定を 0 に変更。

# 機種の固有の設定ファイル (/etc/machine.cfg) — 確定設計

## 1. main の実物と移行先

|置き場と根拠（リポジトリ相対）|読む主体・時点・内容|扱いと理由|
|---|---|---|
|sys/arch/ppc/kernel_cpu.c|CPU 初期化で PVR 上位を調べ、7450 と 7455 に同じ HID0 操作|この設計に移す。7450 の休止選択は運用で替える必要がある。|
|sys/platform/pc98/kernel_gfx_config.inc|起動時の VRAM 予約前に system.cfg の GFX・GUI・CONSOLE_DUAL・GFXDIAG を消費|改修項目。バックエンド指定と表示・診断の利用者設定を分ける。旧キーを今回消さない。|
|sys/kern/init/sysconfig.h、sys/kern/init/sysconfig.c|system.cfg の KEY=VALUE を整数・文字列として取得|既存の読取・解析を共用する。|
|sys/platform/pc98/kernel.c、boot_serial.inc、sys/dev/net/e82557/e82557.c、sys/kern/kdebug/kdebug_module.c|GUI、BOOTSERIAL、E82557/E82557_PHYVENDOR、KDEBUG を各利用者が読む|表示・診断・装置設定として維持。|
|sys/fs/serialfs/serialfs_session.c、kernel_boot_memory.inc|SFS_LAN/SFS_LAN_PEER は system.cfg と netcd.cfg の双方で使用|接続設定の既存経路を保持。|
|sys/include/config.h、tools/gen_deploy_set.py、sys/kern/io_policy.c|設定パス、配布セット、I/O 方針（AC-IO-2）|パスと配布は既存経路を拡張し、I/O 方針は既存所有者に残す。|
|assets/gfx/sync.cfg|同期層の設定選択時に PCI ID・機種で照合し、転送単位、待ち ns などを選ぶ|改修項目。装置単位の表はすでに独立。機種値との所有境界だけ点検する（TASK_HAL_V2.md）。|
|sys/platform/pc98/kernel_boot_memory.inc、sys/platform/powermac/hostfs_boot.c|ホスト FS の起動準備で PC-98 は netcd.cfg、PowerMac は SFS の構造体を使用|改修項目。接続先と起動媒体の設定を保持し、機種による既定だけを抽出できるか調査。|
|sys/platform/pc98/boot_token_pc98.c、sys/platform/powermac/root_boot.c|起動識別トークンの作成・渡された root 像の起動|移さない。起動ごとの情報。指定範囲で共通 cmdline の機種設定読取口は見当たらない。|
|sys/kern/module/debug_probe.c、sys/include/remote_control.h|実行中の probe 登録世代、遠隔サービス識別子の上限|移さない。registry の検索で見える登録管理には設定ファイルを格納しない。|
|sys/dev/hwdev/generated/hwdev_layout.txt、sys/dev/hwdev/generated/hwdev_contract.h|生成時・コンパイル時に VRAM 管理構造の配置を照合|移さない。装置 ABI の定義。|
|sys/include/memmap.h、sys/platform/pc98/platform_boot_layout.h|ビルド時にカーネル・ローダの番地を決定|移さない。docs/02_memory.md 以下の配置表と番地の正典を維持。|
|build/select.mk、build/platform/pc98.mk、build/platform/powermac.mk|make が対象とソース群を選択|移さない。機種既定の実装をリンクする箇所として使う。|
|build/app.conf|ビルド時のアプリ別サイズ指定|移さない。配布像の生成条件。|

追跡対象の ini/cfg/conf は assets/gfx/sync.cfg、assets/netcd.cfg、build/app.conf の 3 本。配布先とソース名は区別する。NP21/W の ini はエミュレータ側が所有する。

POLICY_DEV §1 の統合判断（同）:

1. KEY=VALUE は sys/kern/init/sysconfig.c を再利用する。PC-98 の性能段と PowerMac の NAP はともに起動ごとの方針選択。
2. 構文・優先順位・確定処理を共用。PVR、HID0 書込みは各所有者が扱う。両機種で同じ欠損・不正値試験を回す。
3. root の既存経路と bind 時の段決定を維持。内部契約の第一候補 AC-BOOT-3（両 arch で同じ設定解決結果）。実装前に ARCH_COMPAT_CONTRACT と照合し、既出ならその ID を用いる。公開 KAPI と hwdev の ABI は変更不要。

確認した差は 2 件。TASK_PPC_REALHW.md の暫定像と異なり、main の 7450 は NAP 有効。PERF_PROFILE_PLAN.md の旧案は /etc/system.cfg に文字列を置くが、新しい独立ファイルの指示と異なる。

## 2. 像とファイルの選択

|案|利点|代償・FS のない時点|
|---|---|---|
|a 設定からヘッダ生成|初回 CPU 初期化から使える|変更のたびに像を再生成。配布済みの像を共用しにくい。|
|b root のファイルのみ|媒体の交換で選べる|root 成立前には読めず、休止を保留する制御が必要。|
|c ビルド時の既定表＋root のファイル（推奨）|単一の像で安全に起動し、root 開通後に上書き可能|初期値と確定値の二段階。読み直し時点を一か所に限定する。|

- ゲストの /etc/machine.cfg を選ぶ。PC-98 は起動 root（FD を含む）、PowerMac は RAM root の像を作る際に任意で収録。/host やネットワークの成立を待たない。
- [C4] パスの仕様は本節、文字列の正典は sys/include/config.h の SYS_MACHINE_CFG、読取側はその名前を参照。バイト数は sysconfig の SYSCONFIG_MAX (1024、sys/kern/init/sysconfig.c。シェルの CFG_READ_MAX と等しいことを test_mem_stat.py (旧版 213f1e2d3820) が [C4] として見ているので値を変えない) に従い、契約ヘッダの MACHCFG_FILE_MAX は `SYSCONFIG_MAX - 1` (1023) から導く。MACHCFG_LINE_MAX (改行を除き 95) は契約ヘッダ。物理番地の追加なし。
- PowerMac は ROOT_MANIFEST を build/ram-root.mk の mkroot_image.py --manifest に渡し、root_image.c 経由で RAM root を ELF に埋め込む。収録内容を替えるには ELF を再生成する。
- ファイルは当該 root を使う機体への差分。共用媒体でも効く範囲をキーで限定する。例えば次の 2 行。

```ini
ppc7450.nap=0
perf.profile=2
```

- PowerMac は PVR version 一致 → platform 既定の順。7450 の既定は 0。pm_kboot_info は UNKNOWN/KEYLARGO の区別までで OF model は保持しない。PC-98 は機種 ID を保持せず既定一行を使う。
- 現行 sysconfig は空白・タブ、# と ; のコメントを扱い、最初の一致を返す。sc_atoi は先頭の + と - を受け、十進数字が途切れるまで変換し、数字なしは 0、あふれ検査はない。現行の読取は先頭 1023 B までを解析し、欠損・空・読取失敗は既定を返す。
- machine.cfg 用の検査は sysconfig の既存処理に集約する。長行は飛ばし、数字なし・末尾の空白以外の文字・整数あふれは指定の既定を返す。重複は最初の一致。**この厳しい検査は一時読取のあいだ (machine.cfg を読むとき) だけ効く。system.cfg の解釈は今のまま変えない** (例: `GUI=1 ;x` は今どおり 1)。machcfg は対象キーの取得値が NAP なら 0/1、性能段なら 0–2 かを検査し、範囲外なら当該キーの既定を使う。

## 3. 契約、所有者、確定点

```c
/* sys/include/machcfg_contract.h、カーネル内部のみ */
struct machcfg { u8 perf_request, nap7450; };
struct machcfg_seed { struct machcfg defaults; u8 accepts_nap7450; };
void machcfg_begin(struct machcfg_seed seed);
void machcfg_finish(const struct machcfg *values); /* NULL は既定で確定 */
const struct machcfg *machcfg_get(void);
```

- platform が機種情報を自分の表で解決して begin を一度呼ぶ。sys/kern/machcfg.c の静的領域が既定・能力・現在値・確定フラグを所有する。begin 後は早期利用者も get 可。協調起動で割込みから更新しない。
- machcfg_load_root() は sysconfig の一時読取口を開く。sysconfig 内で vfs_read(SYS_MACHINE_CFG, sc_buf, SYSCONFIG_MAX) を一度実行し、0 以下、または SYSCONFIG_MAX (1024) を返した (1023 B を超えた) なら全体を捨てて finish(NULL)。それ以外は終端を付けて保持し、一時読取中の sysconfig_get_int は同じパスならこのバッファを使う。既定のコピーに各キーの検査結果を入れ、一時読取を終了して finish(&values) を呼ぶ。
- finish は結果を一括公開。確定後の再呼出しは値を保持。CPU レジスタへの反映は arch の責任。root 読取側は共通の設定パスと sysconfig の口を使う。機種表に差替えヘッダが必要な場合の固定名は platform_machcfg.h（check_layers.md 以下）。
- kprintf は選択元、機種・CPU、ファイル結果、採用値を起動時に一度出す。未知キー・行番号の診断は行わない。PowerPC は適用後の HID0.NAP を別に記録する。

## 4. 最初の利用者

- 7450 は早期から NAP を明示クリア（Firmware が立てていた場合も含む）。確定後に 1 の場合だけセットする。7455 は現行どおり、他の PVR は書込みなし。既存の SLEEP 解除・DPM・同期列と HIGH_BAT_EN を保存し、idle (`arch_cpu_idle`・`ppc_cpu_nap` の MSR[POW]) は変えない。7450 で NAP=0 の時は POW を立てても休止に入らない想定 (追記 P-R1)。早期からファイル読取まで DEC 割込みが進むことを確認する。
- NetBSD は 7450 で NAP と DPM の両方を立てない。見分けの像 4c0f7ef4 は NAP だけを外した形で、**実機では未確認** (票 TASK_PPC_REALHW §2。追記 P-R4)。DPM を残すのはその像と同じ形にするため。
- 両 arch に perf_request を提供。PERF_PROFILE_PLAN §5-2 の回答までは既定 0（互換）。PERF_PROFILE_PLAN の実装者は bind 時に 0/1 を直接選択し、2 は DMA NIC、CPU の段、RAM 64 MiB 以上の全条件で 1、不明・未達は 0 とする。決定は bind 中固定。今回は PC-98 でも要求値の取得まで。枠、予算、タイマー、通信路は同票の P0–P5。

## 5. 像の予算（未計測の差分見積もり）

|部品|常駐コード・データ増|境界|
|---|---|---|
|契約・解決・機種表・適用|通常カーネル約 1–2 KiB、状態 32 B 以内|各像に自機種の表だけをリンク|
|root 読取・診断|約 0.5–1 KiB、BSS 約 1 KiB|FS なし MINIMAL は読取部・バッファを省く。既定経路は必要。|
|設定ファイル|例は 64 B 未満、媒体上は FS の割当単位|起動 FD は圧縮カーネルと空きクラスタを実測。RAM root は像の増分も計上。|
|kdebug.bin、hwdev|0 KiB を期待|状態・公開スロットを置かない。リンク map で混入なしを確認。|

通常・FD・MINIMAL の前後で text/data/bss、圧縮像長、配置末尾と既存上限との差を記録。余白不足なら読取診断のサイズを削って再測定する。

## 6. ファイル単位の実装段

各行を 1 ファイル・1 実装者・半日以内の作業単位とし、依存列の終了後は並列可。パスは追加案、既存への接着は入口呼出しに限定。

|段|新規／変更ファイル|行数目安・責務|依存|
|---|---|---|---|
|A|変更 docs/decisions/ARCH_COMPAT_CONTRACT.md|5、AC-BOOT-3 を第一候補として契約照合・追記|なし|
|B|新規 sys/include/machcfg_contract.h|30、型・定数・関数|A|
|B1|変更 sys/include/config.h|1、SYS_MACHINE_CFG|A|
|B2|新規 sys/kern/init/gf_production_cfg.c|15、gf_production_configure・gf_production_selected と静的状態を移す|A|
|B2t|変更 gf_production_host.c (旧版 9030065fac80)・test_gf_production.py (旧版 2dbef53dc1b3)|5、gf_production_host.c の `#include "sys/kern/init/sysconfig.c"` に sys/kern/init/gf_production_cfg.c の include を足す (sysconfig.c の include は残す。試験が初期化に戻す static 変数 gf_configured の初期化が新しいファイルの側で見える形)。covers に新しいファイルを足す|B2|
|B3|変更 sys/kern/init/sysconfig.h|5、一時読取の開始・終了の宣言|B|
|B4|変更 sys/kern/init/sysconfig.c|45、gf_production.h 依存を除去。SYSCONFIG_MAX の行は変えず、配列だけ `sc_buf[SYSCONFIG_MAX + 1]` にする。§2–3 のサイズ・整数検査 (一時読取の間だけ) と一時読取を実装|B1,B2,B3|
|C|新規 sys/kern/machcfg.c|100、状態・既定・確定|B|
|D|新規 sys/kern/machcfg_root.c|45、一時読取・キー別 sysconfig_get_int・値域検査|B4,C|
|E|新規 sys/platform/pc98/machcfg.c|25、識別と既定表|B|
|F|新規 sys/platform/powermac/machcfg.c|35、識別と既定表|B|
|G|変更 sys/arch/ppc/kernel_cpu.c|20、早期と確定後の HID0 適用 (G-a と G-b に分ける、追記 P-G)|B|
|H|変更 sys/platform/pc98/kernel.c|8、早期 begin と root 後の load|C,D,E|
|G1|変更 cpu_nap_host.c (旧版 ea8e22515fed)・test_ppc_cpu_nap.py (旧版 0a6dbf7254ef)|30、CHECK 20・22 の 7450 早期期待値を NAP 解除に変更し、確定後の 0/1 と 7455 を検証。試験の目印の付け直し・明示クリアを削る変異・covers (追記 P-R2)|G|
|I|変更 sys/platform/powermac/ppc_kernel.c|12、platform_boot_services で成功・失敗を確定後に ppc_cpu_power_apply()|C,D,F,G|
|J|変更 build/sources/common.mk|6、sysconfig.c を C_COMMON_PC98_PENDING から C_COMMON_EXEC に移し (vfs_read を呼ぶので、sys/fs/vfs.c の無い像に入れない。CORE に入れると PowerMac の KBOOT_LINK でリンクが落ちる)、gf_production_cfg.c は旧区分。sys/kern/machcfg.c は C_COMMON_CORE、machcfg_root.c は C_COMMON_EXEC|B2,B4,C,D|
|K|変更 build/platform/pc98.mk|1、E の選択|E|
|L|変更 build/platform/powermac.mk|1、F の選択|F|
|M|新規 machcfg_host.c (旧版 dc8f05c16cda)|100、表駆動入力・凍結・誤り|C,D,E,F|
|M1|新規 machcfg-host.py (旧版 5a8c9a8f2988)|25、両表の host 試験をビルド・実行|M|
|N|変更 tools/gen_deploy_set.py|5、machine.cfg は optional とし指定時のみ etc/machine.cfg を配布|B1|
|N1|変更 build/targets/ppc-powermac.mk|5、`ROOT_MANIFEST`・`ROOT_DEPS` に、選んだ設定を書くスタンプ (中身が変わった時だけ更新) を足して ROOT_ISO の依存にする (追記 P-R5)|N|

PC-98 は kernel_boot_cpu（kernel.c）直後に begin。局面 1 の BOOT_AUTO_MOUNT直後、kernel_gfx_config前に成功なら load、失敗・FS なしなら finish(NULL)。PowerMac の ppc_kernel_real_start（kernel_boot.c）の power_init は設定に依存せず 7450 の NAP を下ろす。PVR 取得後に begin し、局面 2 の platform_boot_services（ppc_kernel.c）で mount 成功なら load、else は finish(NULL)。FS なしでも同局面で finish(NULL) とし、確定直後に ppc_cpu_power_apply() を呼ぶ。FS なし像で読取部・未解決 vfs_read が残らないことをリンク検査する。
## 7. 受入と変異（変異ごとに該当検査が失敗すること）

|項目|試験・観測|変異|
|---|---|---|
|既定選択|PowerMac の PVR 一致・unknown、PC-98 の既定一行を照合|PVR 一致より既定を優先|
|文法・値域|CRLF、重複の先頭、未知キー、巨大整数、長行直後の有効行を照合|不正行で解析を打切る|
|全体の確定|host で不在、読取失敗、全キーで vfs_read 計 1 回、1024/1025 B、二度目の finish|読めた先頭だけ公開|
|7450・7455|host の HID0 初期値を全 0／全 1 とし、読戻しと無関係ビットを照合。QEMU は対応 PVR で起動・tick・root 後の値を記録|7450 の明示クリアを削る|
|PC-98 の設定経路|NP21/W の同一 FD で欠損、0、1、2 を順に起動。シェルと要求値を確認|root を読む前に確定|
|PowerMac のファイル経路|設定あり・なしの ROOT_MANIFEST で ELF を再生成し QEMU で上書き・既定を照合。mount 失敗・FS なしでも既定を確認|読取先を /host に替える|
|層・容量|common-compile、include_layers、両 arch ビルドと §5 のサイズ比較|共通 C に機種ヘッダを追加|
|実機固有の停止|物理操作の承認後、7450 Quicksilver で既定 0 の読戻し、RSH 後の /host 連続読取完走。明示 1 の停止有無は別記|旧像の無条件 NAP とログを比較|


延期時の DEFERRED_TESTS 追記案「7450 Quicksilver／物理操作承認待ち／承認後に既定・明示 0・1 の HID0 と RSH 後の /host 連続読取を記録」。
A–B の契約確認を半日、C–G/J/M を並列で半日〜1 日、H/I/K/L と像検査を半日、エミュレータと実機試験を並列で半日〜1 日。QEMU に対象 PVR が無ければ、その PVR の読戻しは実機を受入証跡にする。

## 8. リファクタリング計画に追記する行案

|票・挿入位置|追記する 1 行|
|---|---|
|PERF_PROFILE_PLAN.md の置換|「段の要求は /etc/machine.cfg の perf.profile から受け、P1 で bind 時の判定と接続する。」|
|同票 P0 の前、機種設定の改修項目|「7450 NAP の暫定分岐を機種既定表へ移し、実機の HID0 と /host 完走を TASK_PPC_REALHW §2 に戻す。」|
|同じ改修項目の次行|「netcd.cfg、PowerMac の SFS 既定は接続先と機種差を切り分け、機種依存の既定のみ移行候補にする。」|
|TASK_HAL_V2.md の直後|「sync.cfg の PCI 別待ち時間は装置所有。system.cfg の GFX 等を棚卸しし、機種による既定と利用者指定の優先順・旧キー互換を別段で決める。」|

## 9. 未決 (決着済み)

G4 の設定切替を埋め込み RAM root と ELF の再生成で運用してよいか。推奨は再生成。既存の ROOT_MANIFEST と起動経路を使い、設定入り ELF 単位で実機試験を再現できる。

**ユーザー決定 (2026-11-18)**: 「当面は ELF の再生成でよい」。/host から後で読む形は今回の外。

反論なし。

## 確定 (2026-11-18、claude-opus-5-5 の第 2 回、PM が反映)
- R1: sysconfig.c は C_COMMON_EXEC へ (§6 J)。
- R2: gf_production_host.c と test_gf_production.py の追従を B2t 段として足した (§6)。
- R3: SYSCONFIG_MAX は変えず、配列を SYSCONFIG_MAX+1 に。machine.cfg の上限は 1023 B、vfs_read が SYSCONFIG_MAX を返したら捨てる。MACHCFG_FILE_MAX = SYSCONFIG_MAX - 1 (§2・§3・§6 B4)。
- R4: 厳しい検査は machine.cfg の一時読取の間だけ。system.cfg の解釈は変えない (§2)。
- R5: gf_production_cfg.c は init/ のまま C_COMMON_PC98_PENDING に残す (PM 決定)。理由: 同じ組の gfx の保留ファイルと一緒に、分離の残件 (票 TASK_PPC_SEP) で機種の置き場へ移す方が 1 回で済む。hwdev の slot と kdebug の公開表は名前で結ばれるので、置き場に依らない。
- sc_atoi の符号 (+/-) を §2 に 1 語足した。

## 確定後の追記: PowerPC 側のレビュー (2026-11-18、claude-opus-5-5、os32powerpc が依頼、PM が反映)

| ID | 決めたこと |
|---|---|
| P-R1 | idle は変えない。`sys/arch/ppc/kernel_dec.c` の `ppc_cpu_nap` が立てる MSR[POW] はそのまま (AC-SYNC-2 の試験 `test_arch_compat.py (旧版 feb27053b188)` の anchor_nap と、`test_ppc_cpu_nap` の CHECK 10 を変えない)。POW も外すのは下の P-R4 の順の 2 手目で、その時は `kernel_dec.c` と試験 2 本の段を足す |
| P-R2 | 段 G1 に `test_ppc_cpu_nap.py (旧版 0a6dbf7254ef)` を含める: G の式の変更に合わせた目印の付け直し (count==1 の assert)、§7 の変異「7450 の明示クリアを削る」を MUTATIONS に、covers に `sys/include/machcfg_contract.h` |
| P-R3 | 口は `void ppc_cpu_power_apply(u32 pvr, u8 nap7450)`。`sys/arch/ppc/kernel_cpu.c` で `ppc_cpu_power_init` の直後に置き (試験が `ppc_cpu_power_init(` 〜 `ppc_cpu_load_segments(` で切り出すため)、中で `machcfg_get` を呼ばない (値は呼び手の platform が渡す) |
| P-R4 | 手本 NetBSD (`sys/arch/powerpc/oea/cpu_subr.c` の 7450 の分岐、「XXX my quicksilver hangs if nap is enabled」) は 7450 で NAP も DPM も立てず、powersave も 0 (POW も立てない)。見分けの像 4c0f7ef4 (NAP だけ外した) は未確認で、11-18 の G4 の観測では `LAN STATE state=5 link=1`・`LINK HEAP used=383408` でプロンプトが出ずに止まっていた (/host より前。NAP だけでは説明できない恐れ)。§7 の実機の行の順: (1) NAP=0 の像で /host が通るか、(2) 通らなければ DPM と POW も外す (NetBSD と同じ形)、(3) それでも止まれば NAP 以外の原因として調べ直す |
| P-R5 | 段 N1 の置き場は `build/targets/ppc-powermac.mk` の `ROOT_MANIFEST ?=` と `ROOT_DEPS`。`build/ram-root.mk` は mtime で依存を見るので、設定入りから無しに戻すと ISO が作り直されず `/etc/machine.cfg` が残る。選んだ設定のスタンプ (中身が変わった時だけ更新) を ROOT_ISO の依存にする |
| P-R6 | 段 A で、ARCH_COMPAT_CONTRACT の AC-SYNC-2 の PowerPC の列 (「7450/7455 で NAP、休ませる」) に、7450 の既定 NAP=0 の例外を 1 行足す |
| P-R7 | 識別は PVR だけで妥当 (7450 = 0x8000、7455 = 0x8001。QuickSilver はどちらも OF の model が PowerMac3,5)。キーは `ppc7450.nap` だけで、7455 の NAP を切る口は今回の外 |
| P-R8 | 持越し台帳の PPC-LINK-G4 の「NAP 復帰 (SRR1[POW] が残らない)」は、7450 では nap7450=1 の像だけの項目にする (台帳の直しは PowerPC の PM) |
| P-I | 段 I の差し込み位置: 確定は `sys/platform/powermac/ppc_kernel.c` の `platform_boot_services` で `pm_root_boot()` が 0 を返した直後 (`pm_hostfs_boot` より前)。失敗の時も同じ所で finish(NULL)。KBOOT_SHELL・SH の無い像は、その `#if` の塊の外で finish(NULL)。確定の前の局面 2 の link 待ち (`sys/platform/powermac/link_platform.c` の `pm_lan_start` が `_idle` を呼ぶ) は、power_init が先に 7450 の NAP を下ろしているので安全 |
| P-G | 段 G を 2 つに分ける: **G-a** (`ppc_cpu_power_init` で 7450 の NAP を下ろす。設定に依らない。B〜E に依存しない) と **G-b** (`ppc_cpu_power_apply`、B に依存)。G-a と G1 は PowerPC の PM が先に出してよい (PM 決定)。F・I・N1・G-b は共通の段 (B〜B4・C・D・J) の後 |

## 段 B・C の実装で埋めた設計の欠け (2026-11-19、gpt-6.2-astra の追記、PM が受け入れ)

### 1. machine.cfg のファイル長（段 B・M1）
段 B の sys/include/machcfg_contract.h に公開上限を置く。式は `#define MACHCFG_FILE_MAX (1024 - 1)` とし、読取幅 1024 の正典は sys/kern/init/sysconfig.c の `#define SYSCONFIG_MAX  1024` とする。
段 M1 の machcfg-host.py (旧版 5a8c9a8f2988) はその数値定義を読み、host コンパイルに -DTEST_SYSCONFIG_MAX=<読んだ数値> を渡す。段 M の machcfg_host.c (旧版 dc8f05c16cda) で `_Static_assert(MACHCFG_FILE_MAX == TEST_SYSCONFIG_MAX - 1, "machine.cfg limit");` を課す。
段 B3 で定数を移す必要はない。[C4] は test_mem_stat.py (旧版 213f1e2d3820) (CFG_READ_MAX と SYSCONFIG_MAX の照合) が userland/shell/cmd_sys.c の CFG_READ_MAX と sys/kern/init/sysconfig.c の SYSCONFIG_MAX を直接読むため、試験の追従も不要。
§7 受入・変異：上限の等式が成立し、1023 B は解析、1024 B は全体を破棄する。契約式の減数を 2 に変えると host ビルドが失敗する。

### 2. ppc7450.nap の採否（段 C・D・E・F）
段 F の sys/platform/powermac/machcfg.c は PVR version 0x8000（7450）の行だけ accepts_nap7450=1、7455 とその他は 0 とする。段 E の sys/platform/pc98/machcfg.c も 0 とする。
段 D の sys/kern/machcfg_root.c は既定のコピーに値域検査済みの候補を入れる。CPU 判定は段 C の sys/kern/machcfg.c に保存した seed の能力で行う。
machcfg_finish は確定済みなら直ちに戻る。初回は次の順で採用値を決め、一括公開する。
```c
struct machcfg next = values ? *values : seed.defaults;
if (!seed.accepts_nap7450)
    next.nap7450 = seed.defaults.nap7450;
```
7455・PC-98 で ppc7450.nap=1 を読んでも捨てるのはこのキーだけで、同じファイルの有効な性能段は採用する。7455 の HID0 は従来の機種処理に従う。
起動時の kprintf は選択元、機種・CPU、ファイル結果、確定した性能段と nap7450 を一度出す。対象外なら NAP の選択元は「機種既定（対象外）」とし、行別警告は出さない。PowerPC の適用後 HID0.NAP は別記する。
§7 受入・変異（段 M・M1）：同じ nap=1 の入力で 7450 は 1、7455・PC-98 は seed の既定を保持し、有効な性能段も残る。能力判定を削ると対象外で既定 0 のケースが失敗する。

### 確定本文と矛盾しない理由
1. §2・B4・R3 の読取幅と数値定義の位置を保ち、公開上限が正典より 1 小さいことを host で検証する。
2. §2 の対象 CPU 違いの行を捨てる規則を §3 の一括確定で実現し、§4・P-R7 の 7455 の電力制御を維持する。

## 段 D の追記 (2026-11-19、確定)

r1→r2 の変更 (REVIEW-D.r1 と PM 決定):

- D1: `machcfg_begin` で足した static 3 つ (結果 = NOFS・キー = 0・出した印 = 0) を戻す (§2・§3・§5)。
- D2: 「一時読取の終了」の観測を、load の後の `sysconfig_get_int(SYS_MACHINE_CFG, …)` で vfs_read の回数が増えること、に替えた (system.cfg の観測は併用) (§5)。
- D3: 一時読取中の get_int が数字なし・末尾が空白以外・あふれで def を返すことを口の注釈に書き、受入の入力に `perf.profile=x` と巨大整数を足した (§2・§5)。
- D4: PowerMac の seed の出所として段 F に `pm_machcfg_seed` を足した (§3・§5)。
- D5: host の kprintf の代役は `sys/kern/kprintf_format.inc` の取り込み (vsnprintf は使えない)、covers に足す (§5)。
- D6: PC-98 の report は load の直後で確定。未確認の項と代案を消した (§3)。
- D7: 属性は KLOG_ATTR と同じ物 (§3)。
- D8: seed の欄は残し、PowerMac は常に `cpu`・PC-98 は常に `plat` と注記 (§3・§5)。
- D9: report の cpu にも `0x` を付ける (§3)。
- ほか: D-i の依存に B2・B2t を足した (§5)。U1 は PM 決定「出す」で決着、未決は 0 件 (§6)。

> 2026-11-19、設計者の追記。確定設計 MACHCFG_DESIGN §3 (kprintf)・§5 (root 読取・診断と前後の計測)・§6 段 D・末尾の追記 2 を埋める。
> 実物は main 1f0deee6d を grep で確かめた。ビルド・計測・実行はしていない (大きさは見積もり、**未計測**)。

### 1. 実物の確認

| 物 | 場所 | 今の状態 |
|---|---|---|
| 契約 `struct machcfg`・`struct machcfg_seed`・begin/finish/get | `sys/include/machcfg_contract.h` の `struct machcfg・struct machcfg_seed` | 済。`machcfg_load_root` の宣言は**無い** |
| 上限 `MACHCFG_FILE_MAX` (1023)・`MACHCFG_LINE_MAX` (95) | `sys/include/machcfg_contract.h` の `MACHCFG_FILE_MAX・MACHCFG_LINE_MAX` | 済 |
| 状態 `machine_seed`・`current`・`finished` | `sys/kern/machcfg.c` の `machine_seed・current・finished` (static、外から読む口なし) | 済 |
| `machcfg_begin` / `machcfg_finish` (対象外の NAP を既定に戻す) / `machcfg_get` | `sys/kern/machcfg.c` の `machcfg_begin` / `machcfg_finish` / `machcfg_get` | 済。ビルドの一覧には未登録 (段 J) |
| パス `SYS_MACHINE_CFG` | `sys/include/config.h` の `SYS_MACHINE_CFG` | 済 |
| host 試験 | `machcfg_host.c (旧版 dc8f05c16cda)`・`machcfg-host.py (旧版 5a8c9a8f2988)` (machcfg.c だけを組む。変異は文字列一致 `begin-defaults`) | 済 |
| sysconfig の読取 | `sys/kern/init/sysconfig.c` の `SYSCONFIG_MAX` (1024)・`sc_buf[SYSCONFIG_MAX]`・`sc_load` (1023 B だけ読む)・`sysconfig_get_int` | **一時読取口は無い** (B3・B4 未着手)。今は 1023 B 超を見分けられない |
| 機種の表 | `sys/platform/pc98/machcfg.c`・`sys/platform/powermac/machcfg.c` | **無い** (E・F 未着手) |
| PC-98 の差し込み点 | `sys/platform/pc98/kernel.c` の `kernel_boot_cpu`・`BOOT_AUTO_MOUNT`・`kernel_boot_serial`・`kernel_gfx_config` | 呼び出しは無い (H 未着手) |
| PowerMac の差し込み点 | `sys/platform/powermac/kernel_boot.c` の `ppc_kernel_real_start` (PVR はローカル変数)・`sys/platform/powermac/ppc_kernel.c` の `platform_boot_services`・`pm_root_boot` (`pm_root_boot(&img) == 0`、else 節なし)・`pm_hostfs_boot` | 呼び出しは無い (I 未着手) |
| HID0 の読み | `sys/arch/ppc/ppc_oea.h` の `ppc_read_hid0`・`PPC_HID_NAP` (0x00400000)。`sys/arch/ppc/kernel_cpu.c` の `power_init` (7450 は `power_init` で NAP を下ろす) | `ppc_cpu_power_apply` は無い (G-b 未着手) |

kprintf の行き先 (実物):

| 像 | 道 | 着く所 |
|---|---|---|
| PC-98 | `PLATFORM_KPRINTF_IS_CONSOLE 1` (`sys/platform/pc98/platform_console.h` の `PLATFORM_KPRINTF_IS_CONSOLE`) → console_write → `bootlog_push` (`sys/kern/console/console.c` の `console_write`) | 画面、`/var/log/boot.log` (`sys/platform/pc98/kernel.c` の `bootlog_save` bootlog_save)、`BOOTSERIAL=1` ならシリアル (`sys/platform/pc98/boot_serial.inc` の `kernel_boot_serial`、 kernel_boot_serial の時点までの分をまとめて出し、以後は鏡) |
| PowerMac の exec を持つ像 (kboot-sh・kboot-shell・kboot-link-sh) | `sys/platform/powermac/log_printf.c` の `kprintf` → `pm_diag_write` (`platform/powermac/console_log.c` の `pm_diag_write`) | LAN のログ (`pm_log_write`)、続けてコンソール (stream_only・捕捉中は出さない) |
| PowerMac の exec を持たない像 (kboot-core・kboot-link) | `sys/platform/powermac/platform_console.h` の `PLATFORM_KPRINTF_IS_CONSOLE` が 1 | コンソール |

実物との食い違い (5 件):

| # | 設計の記述 | 実物 | 扱い |
|---|---|---|---|
| X1 | §3 は `machcfg_load_root()` を使う | 契約に宣言が無く、診断に要る seed・選択元を読む口も無い | 本追記 §2・§3 で契約の末尾に足す |
| X2 | 設計 §5「root 読取・診断」「FS なし MINIMAL」 | MINIMAL は CD・FD の配布集合の名前 (`build/image.mk` の `FDD_IMAGE_DEPS`)。FS の無い像は PowerMac の kboot-core・kboot-link (`FEATURE_EXEC = 0`) だけ。PC-98 の像は 1 種 (`sys/conf/images/pc98.mk`) | 「FS なし」は kboot-core・kboot-link と読む |
| X3 | PC-98 も「失敗・FS なしなら finish(NULL)」 | PC-98 は root の mount 失敗で停止する (`sys/platform/pc98/kernel.c` の `platform_boot_machine`)。その道は来ない | PC-98 の H は load だけ。finish(NULL) の分岐は書かない |
| X4 | 設計 §5「root 読取・診断」「BSS 約 1 KiB」 | `sc_buf` 1024 B は既に在る。増えるのは B4 の +1 B と本追記の 3 B | §4 |
| X5 | §4「他の PVR は書込みなし」 | 7450・7455 以外は `sys/platform/powermac/kernel_boot.c` の `ppc_kernel_real_start` で ppc_stop。届くのはこの 2 つだけ | HID0 の行はこの 2 つで足りる |

### 2. 段 D の中身

契約の末尾に足す (カーネル内部だけ、KAPI・hwdev は変えない)。

```c
/* sys/include/machcfg_contract.h の末尾 */
#define MACHCFG_FILE_NOFS   0   /* 読まなかった (FS なし・mount 失敗) */
#define MACHCFG_FILE_NONE   1   /* vfs_read が 0 以下 (無い・空・読取失敗) */
#define MACHCFG_FILE_BIG    2   /* 1023 B を超えた。全体を捨てた */
#define MACHCFG_FILE_OK     3   /* 読めた (有効なキーが 0 個でもよい) */
#define MACHCFG_KEY_PERF 0x01   /* ファイルの値を採ったキー */
#define MACHCFG_KEY_NAP  0x02

int  machcfg_load_root(void);                    /* sys/kern/machcfg_root.c。戻り値 MACHCFG_FILE_* */
void machcfg_note_file(u8 result, u8 keys);      /* sys/kern/machcfg.c。finish の前に 1 回 */
/* machcfg_begin は結果 = MACHCFG_FILE_NOFS・キー = 0・出した印 = 0 に戻す (既存の current = seed.defaults; はそのまま) */
void machcfg_report(const char *machine, u32 cpu_id, int seed_from_cpu); /* sys/kern/machcfg.c */
```

一時読取口 (B3・B4 の形をここで決める。名前は B3 の実装者が変えてよいが戻り値は守る):

```c
/* sys/kern/init/sysconfig.h */
int  sysconfig_temp_begin(const char *path); /* >0 保持した長さ、0 無い・空・失敗、-1 1023 B 超 */
/* 一時読取中の sysconfig_get_int は、数字なし・末尾が空白以外・整数あふれで def を返す (B4 の厳しい検査。今の sc_atoi は数字なしで 0) */
void sysconfig_temp_end(void);
```

```c
/* sys/kern/machcfg_root.c (C_COMMON_EXEC)。読むヘッダ: machcfg_contract.h・config.h・sys/kern/init/sysconfig.h */
int machcfg_load_root(void)
{
    struct machcfg v = *machcfg_get();           /* begin 後なので既定のコピー */
    int n = sysconfig_temp_begin(SYS_MACHINE_CFG), x;
    u8 keys = 0;
    if (n <= 0) {
        sysconfig_temp_end();
        machcfg_note_file(n < 0 ? MACHCFG_FILE_BIG : MACHCFG_FILE_NONE, 0);
        machcfg_finish(0);
        return n < 0 ? MACHCFG_FILE_BIG : MACHCFG_FILE_NONE;
    }
    x = sysconfig_get_int(SYS_MACHINE_CFG, "perf.profile", -1);
    if (x >= 0 && x <= 2) { v.perf_request = (u8)x; keys |= MACHCFG_KEY_PERF; }
    x = sysconfig_get_int(SYS_MACHINE_CFG, "ppc7450.nap", -1);
    if (x >= 0 && x <= 1) { v.nap7450 = (u8)x; keys |= MACHCFG_KEY_NAP; }
    sysconfig_temp_end();
    machcfg_note_file(MACHCFG_FILE_OK, keys);
    machcfg_finish(&v);
    return MACHCFG_FILE_OK;
}
```

- 既定の印は **-1** (どちらのキーの値域にも無い)。無い・書式誤り・あふれは B4 の厳しい検査で def の -1 が返り、値域外は値域の検査で落ちて、既定のまま (B4 が無いと `perf.profile=x` が 0 の明示になる。D-i が B4 に依存する理由)。これで「既定の 0」と「ファイルで明示した 0」を sysconfig を変えずに見分ける。
- 戻り値は誤りコードではなく結果の区分。どの値でも起動は止めない。呼び手は戻り値を使わなくてよい (診断は note が持つ)。
- 読む時機は **FS が使えた後**: PC-98 は `sys/platform/pc98/kernel.c` の `BOOT_AUTO_MOUNT` の直後 ( kernel_boot_serial の BOOTSERIAL と  kernel_gfx_config の gfx 設定より前)。PowerMac は `sys/platform/powermac/ppc_kernel.c` の `pm_root_boot` の成功の中の先頭 (`pm_hostfs_boot` の /host より前)。
- 既定への戻り方: FS の無い像・mount 失敗 (PowerMac だけ) は load を呼ばず `machcfg_finish(0)`。note を呼ばないので結果は begin が戻した NOFS。`pm_root_boot` に else 節を足す (P-I)。
- 対象外の NAP を既定に戻すのは今までどおり finish (`sys/kern/machcfg.c` の `machcfg_finish`)。load は能力を見ない。

### 3. 診断の kprintf

出す行 (共通の書式、`sys/kern/machcfg.c` の `machcfg_report`、行末は共通のソースの慣例の `\n`、79 桁以内):

```
MACHCFG <機種> cpu=0x<16進> seed=<cpu|plat> file=<nofs|none|big|ok> perf=<0-2>/<元> nap=<0|1>/<元>
```

| 欄 | 中身 |
|---|---|
| 機種 | 機種が渡す文字列。`pc98`・`powermac` |
| cpu | 機種が渡す u32 を `0x%x` (HID0 の行の pvr と同じ形)。PowerMac は PVR の生の値、PC-98 は 0 (機種 ID を持たない、設計 §2) |
| seed | 既定の行の出所。`cpu` = PVR が一致した行、`plat` = 機種の既定の 1 行。今届く組は PowerMac が常に `cpu` (7450・7455 だけ、X5)、PC-98 が常に `plat` |
| file | `machcfg_note_file` の結果。呼ばれていなければ `nofs` |
| 元 | `def` = 既定の表、`file` = ファイルで明示、`n-a` = この CPU は対象外 (nap だけ。`accepts_nap7450 == 0` ならファイルに在っても `n-a`) |

例:

```
MACHCFG pc98 cpu=0x0 seed=plat file=none perf=0/def nap=0/n-a
MACHCFG pc98 cpu=0x0 seed=plat file=ok perf=2/file nap=0/n-a
MACHCFG powermac cpu=0x80000201 seed=cpu file=ok perf=0/def nap=1/file
MACHCFG powermac cpu=0x80010201 seed=cpu file=ok perf=2/file nap=0/n-a
MACHCFG powermac cpu=0x80000201 seed=cpu file=nofs perf=0/def nap=0/def
```

- x86 と PowerPC で**同じ書式・同じ関数**。違うのは機種が渡す 3 つの引数だけ。採用値の並びは契約の構造体の順 (perf、nap) で固定。キーを足す時は行の末尾に足す。
- 出すのは 1 回。`machcfg_report` は static の印で 2 回目以降を黙って戻る。印は `machcfg_begin` が戻す (host 試験は 1 プロセスで seed ごとに begin を呼ぶ、`machcfg_host.c (旧版 dc8f05c16cda)` の `run`)。確定前 (finish の前) に呼ばれた時も出さずに戻る (印は立てない)。
- PowerMac の seed の出所は段 F の `int pm_machcfg_seed(u32 pvr, struct machcfg_seed *out)` (`sys/platform/powermac/machcfg.c`、戻り値 1 = PVR が一致、0 = 機種の既定)。begin の所と I の report の所でそれぞれ `ppc_read_pvr()` を読み直して引く (PVR は `sys/platform/powermac/kernel_boot.c` の `ppc_kernel_real_start` のローカルで残らない)。
- 呼ぶ所: PC-98 は load の直後 (H)。PowerMac は確定 → `ppc_cpu_power_apply` → `machcfg_report` → HID0 の行、の順 (I)。FS の無い像でも出す (既定の確認に使う)。
- PC-98 の report は **load の直後 (`sys/platform/pc98/kernel.c` の `BOOT_AUTO_MOUNT` の直後) で確定**。kprintf は console_printf の別名 (`sys/kern/kprintf.c` の `kprintf`)、console_write は毎回 bootlog_push (`sys/kern/console/console.c` の `console_write`)、bootlog は BSS から溜める (`sys/kern/init/bootlog.c` の `g_bl_stopped`)。 BOOT_AUTO_MOUNT より前の `bootinfo_report` も kprintf で出している。
- 属性は KLOG_ATTR と同じ物 (PM 決定)。ATTR_* の出所は `sys/include/kprintf.h` の os32_kapi_shared.h の取り込み → `sdk/include/os32/os32_kapi_shared.h` の ATTR_WHITE など。KLOG_ATTR (0x07) も同じヘッダの `KLOG_ATTR` に在るので、共通のソースからその名前で書ける。
- 未知のキー・行番号は出さない (設計 §3 のまま)。

PowerPC の HID0.NAP の記録 (別の行、機種の側):

```
MACHCFG HID0 pvr=0x80000201 hid0=0x8040c0a4 nap=0
```

- 置き場は `sys/platform/powermac/ppc_kernel.c` の `platform_boot_services` (段 I)。`sys/arch/ppc/kernel_cpu.c` には置かない (kprintf を持たず、試験が関数の範囲を文字列で切り出す、P-R3)。
- 書式 `kprintf(KBOOT_ATTR, "MACHCFG HID0 pvr=0x%x hid0=0x%x nap=%u\r\n", ...)`。行末はこのファイルの慣例の `\r\n`。値は適用後に `ppc_read_pvr()`・`ppc_read_hid0()` を読み直した物 (PVR は保持されていないので読み直す、`sys/platform/powermac/ppc_kernel.c` の `bat_valid` と同じ)。
- 7455 は `nap=0/n-a` と `HID0 ... nap=1` が並ぶ。前の行は設定の値、後の行はレジスタの実際。これが別の行にする理由。

### 4. 大きさ (見積もり、未計測)

| 物 | 入る像 | text+rodata | bss |
|---|---|---|---|
| `machcfg_note_file`・`machcfg_report` (書式 1 本と語 9 個) | 全部の像 (C_COMMON_CORE) | x86 約 0.3 KiB、PowerPC 約 0.4 KiB | 3 B (結果・キー・出した印) |
| `machcfg_load_root` | exec を持つ像だけ (PC-98、kboot-sh・kboot-shell・kboot-link-sh) | 約 0.2 KiB | 0 |
| HID0 の行 | PowerMac の全部の像 | 約 0.1 KiB | 0 |
| sysconfig の一時読取 (B4 の分、参考) | sysconfig.c の入る像 | 約 0.2 KiB | 1 B |

- 設計 §5「root 読取・診断」の枠 (0.5〜1 KiB) の内。BSS は枠 (約 1 KiB) より小さい (X4)。
- kboot-core・kboot-link に machcfg_root.c と vfs_read が入らないことは設計 §6 末尾のリンク検査で見る。report は入る (読取部とは別のファイルなので切り離せる)。
- 起動 FD・MINIMAL は PC-98 の同じカーネルを圧縮して載せる。上限に触れるかは**計測しないと言えない**。設計 §5 末尾のとおり段 J の後に前後を測る。足りなければ先に語を数字に替え (`file=3 perf=2/1`、約 60 B 減)、次に report を exec の側へ移す。

### 5. 変更の一覧と段

段 D を 2 つに分ける。D-ii は C だけに依存するので今出せる。D-i は B2・B2t・B3・B4 を待つ。

| 段 | ファイル | 中身 | 依存 |
|---|---|---|---|
| D-ii | `sys/include/machcfg_contract.h` | 定数 6 個と `machcfg_note_file`・`machcfg_report` の宣言 | C |
| D-ii | `sys/kern/machcfg.c` | static 3 つ、begin での戻し 3 行、note・report (約 38 行)。既存の行は動かさない (変異の目印 `current = seed.defaults;` を保つ) | C |
| D-ii | `machcfg_host.c (旧版 dc8f05c16cda)`・`machcfg-host.py (旧版 5a8c9a8f2988)` | kprintf の代役は、先例 `test_kprintf_va.py (旧版 2d2f6c72ec59)` と同じく `sys/kern/kprintf.c` を展開して組み (kprintf_spec・kutoa_dec・kutoa_hex を含める)、`sys/kern/kprintf_format.inc` の `KPRINTF_WRITE` をバッファへ書く関数にする (host は `-m32 -ffreestanding -nostdlib` で vsnprintf が無い)。行の照合、変異、covers に `sys/kern/kprintf.c` と `sys/kern/kprintf_format.inc` | C |
| D-i | `sys/include/machcfg_contract.h` | `machcfg_load_root` の宣言 | D-ii |
| D-i | `sys/kern/machcfg_root.c` (新規) | §2 の断片 (約 30 行) | B2・B2t・B3・B4・D-ii (B4 は B1・B2・B3 に依存、設計 §6) |
| D-i | `machcfg_host.c (旧版 dc8f05c16cda)`・`machcfg-host.py` | vfs_read の代役 (回数を数える)、sysconfig.c と組む、covers に machcfg_root.c | B4 |
| F | `sys/platform/powermac/machcfg.c` | 設計の表に `pm_machcfg_seed` を足す | B |
| H | `sys/platform/pc98/kernel.c` | begin、load、report の 3 つの呼び出し | D-i・E |
| I | `sys/platform/powermac/ppc_kernel.c` | load か finish(0)、apply、report、HID0 の行 | D-i・F・G-b |

受入:

| 項目 | 見る物 | 変異 (1 行ずつ、該当の検査が落ちること) |
|---|---|---|
| host: 両機種一致 | 同じ入力の表 (無い・空・1023 B・1024 B・`perf.profile=2`・`ppc7450.nap=1`・値域外・重複・`perf.profile=x`・巨大整数) を pc98・7450・7455 の seed で回し、行が機種・cpu・seed・nap の欄を除いて一致 | 既定の印 -1 を 0 に (明示なしが `file` になる) |
| host: 値域 | `perf.profile=3`・`ppc7450.nap=2` は `def` | `x <= 2` を `x <= 3` に |
| host: 読みすぎ | 1024 B は `file=big` で全キー既定、vfs_read は全体で 1 回 | `n < 0` の区分を NONE に固定 |
| host: 一時読取の終了 | load の後に `sysconfig_get_int(SYS_MACHINE_CFG, "perf.profile", -1)` を呼ぶと vfs_read の回数が 1 増える (保持したバッファを使わない)。併せて system.cfg の `GUI=1 ;x` が 1 | 成功の道の `sysconfig_temp_end()` を削る |
| host: 1 回 | report を 2 回呼んで行は 1 本。finish の前の report は 0 本 | 出した印を立てる行を削る |
| host: 対象外 | 7455・PC-98 で `ppc7450.nap=1` を読んで `nap=<既定>/n-a` | `n-a` の判定を削る (`file` になる) |
| NP21/W | machine.cfg なし・`perf.profile=2` の 2 回、どちらもシェル到達。boot.log に `MACHCFG pc98` の行がちょうど 1 本で §3 の例の 1・2 本目と一致 | root を読む前に確定 (設計 §7 のまま) |
| QEMU PowerMac | 7450 で既定・明示 0・明示 1 の `MACHCFG HID0 ... nap=` が 0・0・1、7455 は 1。kboot-core は `file=nofs`。seed は `cpu` だけを期待する (`plat` は PowerMac では出ない) | 7450 の明示クリアを削る (設計 §7 のまま) |

NP21/W の受入は root の `/etc/machine.cfg` を置く配備 (段 N、[D1]・[D2] の範囲の確認) が要る。

### 6. 未決 (ユーザー判断が要るもの: 0 件)

- r1 の U1 (PC-98 の起動画面に `MACHCFG ...` の 1 行が増える) は **PM 決定で「出す」**。PC-98 は `bootinfo_report` などですでに起動画面に kprintf の行を出しているので、ユーザー判断は要らない。

PM が決めれば足りる物: 契約の末尾への追加を AC-BOOT-3 の照合 (段 A) に含めるか、一時読取口の名前、短い語 (`plat`・`def`・`n-a`) の綴り、report を FS の無い像にも入れるか (本追記は入れる)。

### 反論

なし (D1〜D9 と「ほか」をすべて受け入れた)。


確定 (2026-11-19、Opus 5.5 (reviewer) の第 2 回 Approve、N1・N2 を PM が反映): N1 kprintf の代役は sys/kern/kprintf.c を展開して組む、N2 D-i の依存を B2・B2t・B3・B4 に揃えた。
- **段 E・F の入口 (PM 決定 2026-11-21、段 E の astra の止まりを受けて)**: 機種の既定の表の入口は固定名 `struct machcfg_seed platform_machcfg_seed(void);` を sys/include/machcfg_contract.h に宣言し、段 E (sys/platform/pc98/machcfg.c) と段 F (sys/platform/powermac/machcfg.c) が同じ名前で実装する (機種名の付いた名前にしない — 層の決まりの固定名)。既定の 1 行を返すだけで共通のコードを呼ばない。段 H・I がこれを machcfg_begin に渡す。host 試験 machcfg-host は試験の中で seed を組み立てる代わりに各機種の実装を組んで、実の表の既定値 (PC-98 は NAP 非対応) を照合する。
