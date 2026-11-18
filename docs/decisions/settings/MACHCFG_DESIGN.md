## r1→r2 の変更

> 出所: ~/os32-tmp/run/machcfg/DESIGN.final.md (gpt-6.2-astra 起草 r1〜r2、claude-opus-5-5 のレビュー 2 回、確定の直しは PM)。行番号つきの参照は往復の版にあり、ここではパスだけにした (POLICY_DEV §9)。
> 状態: **現行 (2026-11-18)** — 確定設計。段の状態は票 TASK_MACHCFG が持つ。ユーザー決定 (2026-11-18): §9 の未決は「当面は ELF の再生成でよい」(推奨どおり)。

- F1　sysconfig の解析を共用し GF 設定を分離。
- F2　host 試験の配置を訂正。
- F3　HID0 の CHECK 20・22 を実装と対で更新。
- F4　system.cfg の利用者と配布経路を補完。
- F5　パスを include/config.h に集約。
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
|arch/ppc/kernel_cpu.c|CPU 初期化で PVR 上位を調べ、7450 と 7455 に同じ HID0 操作|この設計に移す。7450 の休止選択は運用で替える必要がある。|
|platform/pc98/kernel_gfx_config.inc|起動時の VRAM 予約前に system.cfg の GFX・GUI・CONSOLE_DUAL・GFXDIAG を消費|改修項目。バックエンド指定と表示・診断の利用者設定を分ける。旧キーを今回消さない。|
|init/sysconfig.h、init/sysconfig.c|system.cfg の KEY=VALUE を整数・文字列として取得|既存の読取・解析を共用する。|
|platform/pc98/kernel.c、boot_serial.inc、drivers/net/e82557/e82557.c、kdebug/kdebug_module.c|GUI、BOOTSERIAL、E82557/E82557_PHYVENDOR、KDEBUG を各利用者が読む|表示・診断・装置設定として維持。|
|fs/serialfs/serialfs_session.c、kernel_boot_memory.inc|SFS_LAN/SFS_LAN_PEER は system.cfg と netcd.cfg の双方で使用|接続設定の既存経路を保持。|
|include/config.h、tools/gen_deploy_set.py、kernel/io_policy.c|設定パス、配布セット、I/O 方針（AC-IO-2）|パスと配布は既存経路を拡張し、I/O 方針は既存所有者に残す。|
|assets/gfx/sync.cfg|同期層の設定選択時に PCI ID・機種で照合し、転送単位、待ち ns などを選ぶ|改修項目。装置単位の表はすでに独立。機種値との所有境界だけ点検する（TASK_HAL_V2.md）。|
|platform/pc98/kernel_boot_memory.inc、platform/powermac/hostfs_boot.c|ホスト FS の起動準備で PC-98 は netcd.cfg、PowerMac は SFS の構造体を使用|改修項目。接続先と起動媒体の設定を保持し、機種による既定だけを抽出できるか調査。|
|platform/pc98/boot_token_pc98.c、platform/powermac/root_boot.c|起動識別トークンの作成・渡された root 像の起動|移さない。起動ごとの情報。指定範囲で共通 cmdline の機種設定読取口は見当たらない。|
|kernel/module/debug_probe.c、include/remote_control.h|実行中の probe 登録世代、遠隔サービス識別子の上限|移さない。registry の検索で見える登録管理には設定ファイルを格納しない。|
|hwdev/generated/hwdev_layout.txt、hwdev/generated/hwdev_contract.h|生成時・コンパイル時に VRAM 管理構造の配置を照合|移さない。装置 ABI の定義。|
|include/memmap.h、platform/pc98/platform_boot_layout.h|ビルド時にカーネル・ローダの番地を決定|移さない。docs/02_memory.md 以下の配置表と番地の正典を維持。|
|build/select.mk、build/platform/pc98.mk、build/platform/powermac.mk|make が対象とソース群を選択|移さない。機種既定の実装をリンクする箇所として使う。|
|build/app.conf|ビルド時のアプリ別サイズ指定|移さない。配布像の生成条件。|

追跡対象の ini/cfg/conf は assets/gfx/sync.cfg、assets/netcd.cfg、build/app.conf の 3 本。配布先とソース名は区別する。NP21/W の ini はエミュレータ側が所有する。

POLICY_DEV §1 の統合判断（同）:

1. KEY=VALUE は init/sysconfig.c を再利用する。PC-98 の性能段と PowerMac の NAP はともに起動ごとの方針選択。
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
- [C4] パスの仕様は本節、文字列の正典は include/config.h の SYS_MACHINE_CFG、読取側はその名前を参照。バイト数は sysconfig の SYSCONFIG_MAX (1024、init/sysconfig.c。シェルの CFG_READ_MAX と等しいことを tools/tests/test_mem_stat.py が [C4] として見ているので値を変えない) に従い、契約ヘッダの MACHCFG_FILE_MAX は `SYSCONFIG_MAX - 1` (1023) から導く。MACHCFG_LINE_MAX (改行を除き 95) は契約ヘッダ。物理番地の追加なし。
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
/* include/machcfg_contract.h、カーネル内部のみ */
struct machcfg { u8 perf_request, nap7450; };
struct machcfg_seed { struct machcfg defaults; u8 accepts_nap7450; };
void machcfg_begin(struct machcfg_seed seed);
void machcfg_finish(const struct machcfg *values); /* NULL は既定で確定 */
const struct machcfg *machcfg_get(void);
```

- platform が機種情報を自分の表で解決して begin を一度呼ぶ。kernel/machcfg.c の静的領域が既定・能力・現在値・確定フラグを所有する。begin 後は早期利用者も get 可。協調起動で割込みから更新しない。
- machcfg_load_root() は sysconfig の一時読取口を開く。sysconfig 内で vfs_read(SYS_MACHINE_CFG, sc_buf, SYSCONFIG_MAX) を一度実行し、0 以下、または SYSCONFIG_MAX (1024) を返した (1023 B を超えた) なら全体を捨てて finish(NULL)。それ以外は終端を付けて保持し、一時読取中の sysconfig_get_int は同じパスならこのバッファを使う。既定のコピーに各キーの検査結果を入れ、一時読取を終了して finish(&values) を呼ぶ。
- finish は結果を一括公開。確定後の再呼出しは値を保持。CPU レジスタへの反映は arch の責任。root 読取側は共通の設定パスと sysconfig の口を使う。機種表に差替えヘッダが必要な場合の固定名は platform_machcfg.h（check_layers.md 以下）。
- kprintf は選択元、機種・CPU、ファイル結果、採用値を起動時に一度出す。未知キー・行番号の診断は行わない。PowerPC は適用後の HID0.NAP を別に記録する。

## 4. 最初の利用者

- 7450 は早期から NAP を明示クリア（Firmware が立てていた場合も含む）。確定後に 1 の場合だけセットする。7455 は現行どおり、他の PVR は書込みなし。既存の SLEEP 解除・DPM・同期列と HIGH_BAT_EN を保存し、NAP 無効中の idle は MSR[POW] に依存せず通常待機する。早期からファイル読取まで DEC 割込みが進むことを確認する。
- NetBSD は 7450 で NAP と DPM の両方を立てない。実機確認済みの像 4c0f7ef4 は NAP だけを外しており、DPM 維持はその実績に合う。
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
|B|新規 include/machcfg_contract.h|30、型・定数・関数|A|
|B1|変更 include/config.h|1、SYS_MACHINE_CFG|A|
|B2|新規 init/gf_production_cfg.c|15、gf_production_configure・gf_production_selected と静的状態を移す|A|
|B2t|変更 tools/tests/gf_production_host.c・tools/tests/test_gf_production.py|5、gf_production_host.c の `#include "init/sysconfig.c"` に init/gf_production_cfg.c の include を足す (sysconfig.c の include は残す。試験が初期化に戻す static 変数 gf_configured の初期化が新しいファイルの側で見える形)。covers に新しいファイルを足す|B2|
|B3|変更 init/sysconfig.h|5、一時読取の開始・終了の宣言|B|
|B4|変更 init/sysconfig.c|45、gf_production.h 依存を除去。SYSCONFIG_MAX の行は変えず、配列だけ `sc_buf[SYSCONFIG_MAX + 1]` にする。§2–3 のサイズ・整数検査 (一時読取の間だけ) と一時読取を実装|B1,B2,B3|
|C|新規 kernel/machcfg.c|100、状態・既定・確定|B|
|D|新規 kernel/machcfg_root.c|45、一時読取・キー別 sysconfig_get_int・値域検査|B4,C|
|E|新規 platform/pc98/machcfg.c|25、識別と既定表|B|
|F|新規 platform/powermac/machcfg.c|35、識別と既定表|B|
|G|変更 arch/ppc/kernel_cpu.c|20、早期と確定後の HID0 適用|B|
|H|変更 platform/pc98/kernel.c|8、早期 begin と root 後の load|C,D,E|
|G1|変更 tools/ppc/cpu_nap_host.c|20、CHECK 20・22 の 7450 早期期待値を NAP 解除に変更し、確定後の 0/1 と 7455 を検証|G|
|I|変更 platform/powermac/ppc_kernel.c|12、platform_boot_services で成功・失敗を確定後に ppc_cpu_power_apply()|C,D,F,G|
|J|変更 build/sources/common.mk|6、sysconfig.c を C_COMMON_PC98_PENDING から C_COMMON_EXEC に移し (vfs_read を呼ぶので、fs/vfs.c の無い像に入れない。CORE に入れると PowerMac の KBOOT_LINK でリンクが落ちる)、gf_production_cfg.c は旧区分。kernel/machcfg.c は C_COMMON_CORE、machcfg_root.c は C_COMMON_EXEC|B2,B4,C,D|
|K|変更 build/platform/pc98.mk|1、E の選択|E|
|L|変更 build/platform/powermac.mk|1、F の選択|F|
|M|新規 tools/tests/machcfg_host.c|100、表駆動入力・凍結・誤り|C,D,E,F|
|M1|新規 tools/checks/machcfg-host.py|25、両表の host 試験をビルド・実行|M|
|N|変更 tools/gen_deploy_set.py|5、machine.cfg は optional とし指定時のみ etc/machine.cfg を配布|B1|
|N1|変更 build/ram-root.mk|5、ROOT_MANIFEST と設定の追加・変更・除外で RAM root と ELF を再生成する依存関係を確認・補完|N|

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
|層・容量|common-compile、tools/check_layers.py、両 arch ビルドと §5 のサイズ比較|共通 C に機種ヘッダを追加|
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
