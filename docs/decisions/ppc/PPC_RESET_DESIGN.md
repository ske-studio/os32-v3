> 「リブートは先に入れないと先に進みにくいです。アーキテクチャをきちんと分離して実装してくださいね。」（2026-11-18）

# PowerMac の platform_reset 設計 (確定)

> 出所: ~/os32-tmp/run/ppc-reset/DESIGN.final.md (設計の往復の確定版)。
> 状態: **現行 (2026-11-18)** — 確定。 astra 起草 r1・r2、Opus 5.5 の独立レビュー 2 回、PM が第 2 回の P3-1〜P3-4 を反映。往復の版は ~/os32-tmp/run/ppc-reset/。HAL 中心期 (POLICY_DEV §1) の「新装置に着手しない」の例外 (冒頭のユーザーの直接の指示)。

## r1→r2 の変更

- A1 機種ヘッダに VIA の VA 窓と取得関数を定義。
- A2 送受信を via_command に集約し、restart は命令列を渡す。
- A3 OF は name の完全一致で選び、compatible は診断用に記録。
- A4 PMU の固定長命令の送信列を明記。
- A5 起動情報の種類を u8 にし、既存 host fixture・変異の追従先を列挙。
- A6 クロス環境欠落時の任意・必須モードを受入に追加。
- A7 PM 決定に従い、手動電源再投入後も resume で旧 ID を退避できるようにする。
- A8 timebase 欠落時の既存の起動停止を明記。
- A9 時計の口を GEM と同じ 64 bit に統一。
- A10 Cuda ポーリングで使用する IFR の判定と消去を限定。
- A11 PM 決定に従い、QEMU の PMU 命令未実装時の台帳と受入先を指定。
- A12 失敗 LOG の後に LAN を有界にポーリング。

POLICY_DEV §1 の HAL 中心期における「新装置に着手しない」の例外。根拠は上記のユーザーの直接の指示。
対象は QuickSilver と mac99。シングルタスク・協調切替、整数演算。実機での完了証拠は次のネットブート。

## 1. 実物の確認（main a76398a35）

| 入口から順に | 現状と統合先 |
|---|---|
| shell | userland/shell/cmd_sys.c の既存コマンドが g_api->sys_reboot() を呼ぶ。PPC も build/ppc-user.mk でこれを組む。 |
| PPC ラッパ | sdk/kapi_target_ppc.py の提供者無し集合を sdk/gen_kapi.py が読み、void の空関数を生成する。 |
| 共通 | kernel/sys.c は描画側の返却コールバック、platform_reset、永久 halt の順。コールバック未登録も処理済み。 |
| 機種の口 | include/platform_sys.h は復帰可。PC-98 は platform/pc98/system.c の I/O 列。PPC は build/platform/powermac.mk に相当する提供者がない。 |

依頼の略記と実物の食い違いは次の 4 件。
1. 「link の RSH」には像の指定が要る。build/images/kboot-link.mk は EXEC/USERSHELL 無効。受入には build/images/kboot-link-sh.mk の link-sh を使う。
2. docs/ppc/qemu-mac99.md は VIA-CUDA の文書記載まで。既定と via=pmu の選択は同資料では未確認。両起動の識別 LOG で確定する。
3. OpenBSD の写しで Cuda の restart は指定された cuda.c でなく sys/arch/macppc/dev/adb.c。FreeBSD の入口名も cuda.c と pmu.c の shutdown 系。
4. host_agent.py はホスト子プロセスの pending。RSH の実体は host_rshell.py。socket 側だけを直しても別 boot の要求が残る。

統合の判断（POLICY_DEV §1）：
- 役目は PC-98 と共通。現在の機械をリセットする一操作であり、終了ポリシーは共通 sys_reboot が持つ。
- 口は platform_reset 一つ。shell、RSH、KAPI に機種分岐を足さない。
- PPC の提供者無し 190 本（tools/ppc/test_kapi_binding.py）のうち装置の組の第一段。次の rtc_read は同じ識別・転送を使える。今回は時計の入口を足さない。

## 2. 層と置き場

| 所有者 | 持つもの・読めるヘッダ |
|---|---|
| 共通 | kernel/sys.c と include/platform_sys.h は現行通り。共通に VIA 型を出さず、追加 contract は不要。 |
| PowerMac | OF の発見、物理窓から VA への対応、初期化結果、LOG、IRQ 制御、MMIO と時計の結線。装置ヘッダと共通・CPU の既存の口を読む。 |
| 装置 | drivers/macintosh/ に VIA の byte 転送と機種に依らないプロトコルを置く。装置内ヘッダと共通型を読み、OF・PVR・仮想番地を知らない。 |
| CPU | arch/ppc の既存 MSR、timebase、ページングを利用。VIA の識別・レジスタ・reset 命令列は置かない。 |

装置ヘッダの境目（型名は実装案）。操作の引数は byte offset。
```c
enum via_kind { VIA_NONE, VIA_PMU, VIA_CUDA };
struct via_io {
    void *cookie;
    u8 (*read8)(void *, u32);
    void (*write8)(void *, u32, u8);
    unsigned long long (*tb)(void *);
    u32 tb_hz;
};
struct via_controller { enum via_kind kind; struct via_io io; };
int via_prepare(struct via_controller *v);
int via_restart(struct via_controller *v);
```
機種側の取り込み結果は値で所有する。OF のプロパティ作業バッファ・phandle を実行時に持ち越さない。
```c
struct pm_via_boot {
    u8 kind; /* 0=none, 1=pmu, 2=cuda */
    u32 pa, bytes;
    int capture_rc;
};
```
- kboot_capture.c と同じ OF 有効期間に mac-io 配下を走査。NetBSD pmu.c / cuda.c と同じく name の via-pmu / via-cuda との完全一致で選ぶ。OS32 では compatible も有界に LOG に記録するが選択には使わない。
- 同一ノードの重複一致は一つ。異なる候補が二つ、照合・番地変換に必須の属性の欠落や切断は理由付き無効。先着採用や推測番地を避ける。
- escc_tree.c の BAR + 子 reg は直結の一例。mpic_tree.c にならい各親のセル幅・ranges と PCI 空間を検査し、mac-io のメモリ BAR 内の一意な窓を CPU PA に変換。
- reg の全長、0x1c00 への byte アクセス、終端加算、ページ丸めの桁あふれを検査。親境界をまたぐ窓は無効。資料は docs/ppc/open-firmware.md §「PCI のセルと割込み」。
- 機種の PVR 表や /etc/machine.cfg は設定解決用（MACHCFG_DESIGN §3）。VIA の種類・番地は装置の識別結果であり、設定キーは設けない。
- kernel_boot.c の ppc_boot_alias_plan と同じ方式で、使用範囲 0x1c01 byte の専用窓を計画。VA は PM_VIA_VA、ページ内 offset を保持。
- 同 :164–175,187–210 の install と walk 検査に入れる。RAM、既存 MMIO、USB、FB の全構成と PA/VA の重なりをページ単位で検査し、PPC_BOOT_IO_ATTR で写す。
- 機種内の静的状態に VA と結果を保持し、takeover 後のルートで検証した時だけ有効化。arch/ppc/kernel_boot.h の引渡しに機種の型を追加しない。
- platform/powermac/via_window.h の定義は次の通り。kernel_boot.c の slots[] に { PM_VIA_VA, PM_VIA_BYTES } を追加。最大ページ内 offset 0xfff と使用長 0x1c01 の合計を 3 ページで収める。

```c
#define PM_VIA_VA    0xfe3c4000U
#define PM_VIA_BYTES 0x3000U
struct pm_via_window { u32 va; int rc; };
const struct pm_via_window *pm_via_window(void);
```

  kernel_boot.c が static な窓を所有し、取得関数はその const ポインタを返す。初期値は未準備エラーと VA 0。capture/plan/install/walk の失敗理由を保持し、検証成功時だけ alias.va（offset 込み）と rc=0 を公開。system.c が取得し rc=0 の時だけ MMIO cookie に結線する。
- 装置アクセスは順序保証付き 8 bit MMIO。起動後に OF を呼ばない。初期化完了後に例「VIA kind=pmu pa=80016000 va=fe3c4000 rc=ok」を一行、失敗時も種類・理由を一行。
- 時間源は kboot_capture.c の timebase-frequency。tb_hz=0 なら既存の capture が起動を止める。準備は OS 所有の MMIO と時計が有効になってから一度行う。
- 発見失敗時も他の機能は起動できる。失敗した reset は理由を既存のポーリング LOG に残し、共通側の停止へ戻る。

```c
void platform_reset(void)
{
    _disable();
    int rc = via_ready ? via_restart(&controller) : via_boot_error;
    if (rc) {
        pm_log_reset_error(rc);
        /* ppc_kernel.c の既存ループと同じく TB の期限まで
           pm_lan_poll(now) を反復し、送信キューを進める。 */
    }
}                                    /* kernel/sys.c が最後の待機を所有 */
```

## 3. 送信の手順

via.c 内の転送の口は次の形とし、送信状態機械は out[0..n) を順に消費する。

```c
static int via_command(struct via_controller *v, const u8 *out, u32 n,
                       u8 *in, u32 cap, u32 *got);
int via_restart(struct via_controller *v)
{
    static const u8 pmu[] = { 0xd0 }, cuda[] = { 0x01, 0x11 };
    u32 got;
    if (v->kind == VIA_PMU)
        return via_command(v, pmu, sizeof(pmu), NULL, 0, &got);
    if (v->kind == VIA_CUDA)
        return via_command(v, cuda, sizeof(cuda), NULL, 0, &got);
    return -1;
}
```

got は保存した byte 数で入口で 0。受信付き命令では Cuda の応答 frame を末尾まで読み、cap までは in に保存し、超過分は捨てる。事前受信・競合 frame は応答に数えない。restart は送信完了で戻る。PMU は命令の送受長の規則に従う。

出典略号 N/O は ~/os32-tmp/ref/{netbsd,openbsd}/sys/arch/macppc/dev、F は ~/os32-tmp/ref/freebsd/sys/powerpc/powermac。
Apple の Power Mac G4 (September 2002) Developer Note、vendor/text/apple-pmg4-sept02.txt:318 は KeyLargo と PMU99 を説明する。
これは MDD の資料。QuickSilver 個体の種類とレジスタ順の根拠はそれぞれ対象の OF tree と下記の BSD 実装。Apple の同資料に reset の byte 列はない。

| VIA レジスタ（byte offset） | 使うビットと用途 | 定義 |
|---|---|---|
| B 0x0000 / DDRB 0x0400 | PB3=0x08 は相手側 active-low、PB4=0x10、Cuda の PB5=0x20 はホスト側。出力ラッチ・方向の他ビットは保存。 | F/viareg.h |
| SR 0x1400 / ACR 0x1600 | SR は 1 byte、ACR の 0x0c がシフトモード、0x10 が出力。 | N/viareg.h、N/pmu.c |
| IFR 0x1a00 / IER 0x1c00 | Cuda は IFR の SR 完了 0x04 をポーリング。IER は bit7=0 で指定ビットを無効化。 | N/viareg.h、N/cuda.c |

| Cuda の順 | OS32 の操作 | 手本 |
|---|---|---|
| 1. 準備 | DDRB |= 0x30、続いて &= ~0x08。ACR の出力を落とし、0x0c を立てる。B の PB4/5 を high にして idle。 | N/cuda.c cuda_init、同 :483–490 cuda_idle |
| 2. 同期 | SR 読み、IER=0x04、idle、150µs、TIP low、150µs、PB4 反転、150µs、TIP high、150µs、idle、SR 読み。 | N/cuda.c、TIP/clear_tip :493–510、toggle_ack :534–540 |
| 3. 送信前 | IRQ 配送を止めたまま相手の pending frame を有界に読み捨てて idle にする。 | N/cuda.c cuda_restart が send 前に poll、同 :454–468 |
| 4. 開始 | 150µs 後 ACR 出力、SR=0x01 (pseudo)、PB4 high、TIP low。線の変更より先にデータを書き込む。 | N/cuda.c。F/cudavar.h |
| 5. 次 byte | IFR & 4 を待ち、SR 読みで消去、PB3 確認。正常なら SR=0x11、PB4 を反転。 | N/cuda.c cuda_intr |
| 6. 完了 | 二度目の IFR & 4、SR 読み、PB3 確認。ACR を入力に戻し、PB4/5 high。 | N/cuda.c。F/cuda.c も同順 |
| 7. 競合 | PB3 low なら入力・idle、150µs 後に受信。最初の SR を捨て、PB3 low なら入力・TIP low。以後 IFR ごとに SR 読み、PB3 low 中は PB4 反転、high で idle。 | N/cuda.c |
| 8. 再開 | 競合 frame 終了後 1500µs 待ち、PB3 を再確認して先頭から再送。全体期限は引き継ぐ。完了済みのコマンドは再送しない。 | N/cuda.c。O/adb.c、F/cuda.c 以下も pseudo/reset |
 
- N の IER=0x84 は割込みハンドラ用。OS32 では SR の IRQ 出力を無効のままにし、IFR の SR bit だけを読む。cuda_intr の bit7 判定と IFR への 0x7f 書込みは写さず、SR 読みで完了を消す。ADB の登録・入力配信は行わない。
- N の受信内容別の処理を省く。各 frame は §3 冒頭の規則で消費する。異常長は 256 byte 上限で失敗にする。
- 競合または期限切れでフレーム途中なら入力・idle に戻し段階を記録する。単なる timeout は実行済みか不明のため再送しない。

| PMU の順 | OS32 の操作 | 手本 |
|---|---|---|
| 1. 準備 | B の PB4 high を先に設定、DDRB で PB4 出力・PB3 入力。Cuda の PB5 を操作しない。 | F/pmu.c |
| 2. 送信開始 | CPU IRQ 禁止。IER を読み 0x10 と AND して書き戻し、PB3 high（idle）を待つ。 | N/pmu.c pmu_send、同 :620–624 |
| 3. 命令 | ACR |= 0x1c、SR=0xd0、B の PB4 low。 | N/pmu.c、N/pmuvar.h |
| 4. 相手の応答 | PB3 low を待ち、PB4 high、PB3 high を待つ。最後の PB4 high も保つ。 | N/pmu.c、F/pmu.c 以下 |
| 5. 終了 | pm_send_cmd_type が 0 以上なら長さ byte を送らない。0xd0 は type 0 で payload・受信とも 0、送信は命令 1 byte のみ。 | N/pmu.c、F/pmu.c |
| 6. 失敗 | PB4 を解放して high。戻り値に idle/busy/free のどの待ちかを残す。 | O/pm_direct.c に有限待ちと応答解放 |

- N/pmu.c と F/pmu.c の ADB 等のイベント許可は省く。コマンド転送は PB3 のレベルだけで進む。
- BSD の通常転送終了時の割込み再許可は省く。停止を所有する呼出元へ戻る terminal 操作のため。
- N/pmu.c は timeout 未実装、N/cuda.c も無期限。OS32 は各辺 100ms、転送全体 1s を初期予算とする。BSD にこの時間値はない。
- timebase 差を unsigned で測り、整数の切上げで µs を ticks に換算、期限は wrap の半周期未満。IRQ 禁止で進まない tick や sleep は使わない。
- 実機で最後の辺より早く reset が掛かってよい。転送関数の復帰や ACK は再起動の合格証拠にしない。
- GEM/USB の停止・DMA バッファ解放は加えない。N の両 restart、F の shutdown コールバックの送信列に装置別停止はない。
- BSD の shutdown 全体と同じ停止保証があるとは仮定しない。送信後は共通 halt へ直行し、DMA 所有メモリを再利用しない。再起動後の LAN/USB 再初期化を実機で確認。

## 4. KAPI と検査の追従

- 'sys' の absent 集合を buz_on/buz_off のみにする。既存 sys_reboot を通常生成へ戻す。rtc_read は sdk/kapi_target_ppc.py のまま。
- tools/ppc/test_kapi_binding.py の未定義許可に sys_reboot を加え、PPC KAPI .o に実際に U sys_reboot があること、最終 ELF で解決することを検査。
- absent stub は 189 本。単純な個数だけに頼らず reboot の実呼出し、buzzer 2 本の空実装、rtc の absent を確認。host のリンク用 stub は必要な fixture にだけ足す。
- sdk/kapi.json の既存スロット・型・版を使うので ABI 追記は 0。PPC 提供者の差だけが生成 C に出る。生成物を手で直さない。
- os32-kapi-add に従い、将来 ABI を増やす段では api 末尾追記、JSON version と os32_kapi_shared.h の KAPI_VERSION を同期して再生成する。
- 今回も clean → make all、PPC 像を新規生成。ROLES §4-3 の HWDEV layout CRC/inventory、FEP_MODULE_ABI_GEN、KAPI 版・ranges・audit・ABI を基点と照合。
- 値が変わらない ABI 指紋は更新しない。像一覧は build_profile 出力から更新し、変更した試験の covers、入力と変異の一意性を lint する。

## 5. 部品の大きさ（差分の概算）

- C_PLATFORM_CORE に置くため core/link/shell/sh/link-sh の各 kboot 像が識別と reset を共有。RSH から shell を使う受入は link-sh。
- 装置 C 約 200 行、機種 C 約 150 行、宣言・ビルド約 40 行。動的割当なし、受信は cap まで in に保存し超過分は捨てる (§3 via_command)、MMIO の窓は 3 ページ (§2 via_window.h)。
- PPC の増分はリンク後に各像の text/data/bss と map を基点と比較して報告。PC-98 に入る新規ソース数は 0。

## 6. ファイルごとの変更（行数は目安）

| ファイル | 担当する差分 | 行 |
|---|---|---:|
| drivers/macintosh/via.h | 種類、I/O・時間の口、準備と再起動 | 26 |
| drivers/macintosh/via.c | byte 操作、期限、PMU/Cuda の送受信とコマンド | 200 |
| platform/powermac/via_tree.c | OF 一意照合と親経路の番地解決 | 75 |
| platform/powermac/kboot_info.h | 種類は u8 で保持。via.h を include せず機種の capture 境界で種類値を対応づける | 8 |
| platform/powermac/kboot_capture.c | OF 有効中の capture | 2 |
| platform/powermac/via_window.h | PM_VIA_VA、窓の型と取得宣言 | 8 |
| platform/powermac/kernel_boot.c | VA 窓・重複・PTE 検査と引渡し | 35 |
| platform/powermac/system.c | 装置接続、準備、識別 LOG、platform_reset | 38 |
| platform/powermac/ppc_kernel.c | takeover 後の準備呼出し | 2 |
| build/platform/powermac.mk | 機種ソースとこの機種が使う装置ソースの明示列挙 | 1 |
| sdk/kapi_target_ppc.py | sys の absent を 2 本にする | 1 |
| tools/ppc/test_kapi_binding.py | stub・通常生成・nm、reboot を absent に戻す変異 | 12 |
| tools/tests/fixtures/build_profile_ppc.json | 各像の KB_C を生成値に同期 | 1 |
| tools/host_rshell.py | reboot 自動退避と、別 boot を観測した resume の明示退避 | 10 |
| tools/host_operator_socket.py | resume を既存の pending ID に通し、退避結果を一度取得・消費 | 4 |
| tools/tests/test_remote_x13.py | socket と operator の別 boot、遅着、次の submit | 30 |
| tools/tests/via_host.c | MMIO/timebase の模擬、正常・競合・期限 | 120 |
| tools/tests/test_powermac_reset.py | host 実行、OF fixture、変異と CHECK/covers | 90 |
| tools/ppc/link_sh_run.py | 実 shell を使う reset 専用ケースと QMP 観測 | 45 |
| tools/checks/ppc-reset.py | 両 mac99 ケースと入力の宣言 | 15 |

既存 host 側の確認・変更箇所（tools/ppc/ 配下）

- kboot_boot_host.c で VIA 窓の offset・端・重複・無効値を検査。kboot_boot.py の入力・ビルド構成と kboot_boot_guest.c の walk 検査に窓を反映。
- kboot_capture_fake.c に VIA の発見・欠落 fixture を追加し、kboot_capture.py のリンク入力に via_tree.c を追加。kboot_mutations.py は既存の置換目印を照合し、変更行を指すものを一意な実行時 RED の目印に移す。
- kboot_info.h を直接読む fake は kboot_capture_fake.c、kboot_mpic_tree_fake.c、lan_p24_fake.c。共通の u8 表現で既存の単独コンパイルを維持する。

## 7. 受入から逆算した試験と変異

- host の accept_ready で generation/session/boot の既存検証を通過後、旧 pending が cui・単独行 reboot・running/UNKNOWN、boot が異なる場合だけ旧 ID を results に退避して pending を空ける。
- 旧 ID は「別 boot を観測」、complete=false。実行成功の status を捏造しない。socket の result で退避結果を消費する。現在の要求と旧 ID は混ぜない。
- 切断だけ・同じ boot の READY・不正 session は従来通り。通常コマンドでも明示 resume 時は検証済みの READY と pending の boot を比較し、異なれば旧 ID を同じ「別 boot を観測」で退避し pending を空ける。同一 boot は既存の resume を通す。手動電源再投入後の通常コマンドでも result と次の submit を socket 経由で検査。host_agent.py の子プロセス辞書には触れない。
- host は旧 boot の遅着出力、結果照会前の次要求、重複 READY、同じ boot の timeout で再送しないことを確認。boot 比較反転の変異で旧要求処理が RED。
- OF fixture は PMU/Cuda 個別、両方、欠落、壊れた reg、親 ranges 外、上位セルの桁あふれを入力。期待 PA とページ内 offset、無効時 MMIO 0 回を比較。
- host の模擬 VIA は方向・データ・制御線の各遷移を観測し、正しい順でのみ相手側の辺を進める。独立した期待列は Cuda [01,11]、PMU [d0]。
- Cuda の古い IFR、送信前受信と途中競合、PMU の PB3 固着 high/low、時間 wrap を含める。期限切れ後に余分な SR 書込みがないことを確認。
- 変異は個別にコマンド置換、TIP と SR の逆順、PB4 反転省略、PB3 極性反転、期限無効化。各変異を実行し trace/結果/外部 watchdog のいずれかで RED を示す。
- QEMU は link-sh を mac99 既定と mac99,via=pmu で別々に起動。通常の tools/ppc/link_sh_run.py は -no-reboot を付けるため、このケースだけ外す。
- それぞれ LOG の cuda/pmu、OF-OFF、CUI READY を確認。QMP 接続時・初回起動のイベントを排出し、serial の末尾位置を記録してから RSH で reboot を一度送る。
- 送信後の guest RESET event または新しい OF 起動行を期限内に得る。QMP system_reset は使わない。exit、切断、既存バナーの再読は失敗。空ラッパ変異で期限切れ RED。
- mac99,via=pmu の 0xd0 実装有無を使用版の QEMU ソースで確認する。未実装なら版・根拠・代替試験を docs/tasks/DEFERRED_TESTS.md に記録し、PMU は host 試験と PMU 搭載 G4 実機で受ける。実装済みの場合の timeout は試験失敗。
- 新規 PPC 試験は PPC_CROSS_DIR=/nonexistent で SKIP・rc=0、同条件に OS32_REQUIRE_PPC=1 を加えると FAIL・非ゼロを確認。
- QuickSilver は実機ノートの tftpd で初回 RRQ の送信元・ファイル・時刻を記録。VIA の一行、OF-OFF、link-sh の READY と boot ID を保存。
- 同じノートの RSH から reboot を一度送り、送信後に同じ実機から対象像の新規 RRQ、再ロード、別 boot の READY、次の ver 応答を確認。初回 RRQ の再送と区別する。
- USB 入力、GEM 経由の再接続も記録。結果欠落だけで合格にしない。実機の装置種別と PA はこのログで確定する。
- x86 は基点と同一条件の clean build_compare と x86 KAPI 全生成物の byte 比較。PPC の差だけを申告。layers、build-selection、ppc-kapi-binding、追加・変更試験 --mutate、run.py --lint を実施。
- この r2 は設計のみ。ビルド、変異実行、QEMU、実機 RRQ の実測は実装後の受入。

## 8. 未決

ユーザー判断を要する項目は 0 件。実機の装置種別、実際の応答時間、像サイズは上記の検査で記録する。

反論なし。

## 確定 (2026-11-18、Opus 第 2 回、PM が反映)
- P3-1: §6 の既存の host の土台と試験の一覧に足す — (a) kboot_info.h を読む tools/ppc/kboot_memory_host.c、(b) 本文の文字列を目印に読む tools/tests/test_ppc_terminal_work.py と tools/tests/test_arch_ctx.py (platform/powermac/ppc_kernel.c)、tools/tests/test_ppc_cpu_nap.py (platform/powermac/kernel_boot.c)、(c) tools/checks/ppc-rh-fb.py の covers (kernel_boot.c)。ppc_kernel.c は呼び出しを 1 行足すだけなので、各試験の目印が外れないかを実装者が grep と MUTATE=1 で確かめる。
- P3-2: §3 via_command — Cuda の応答 frame を待つのは `in != NULL かつ cap > 0` の時だけ。それ以外は送信の完了で戻る。
- P3-3: §2 platform_reset — 失敗の時に LAN の LOG を流し続ける期限は、ppc_kernel.c の既存の 2 秒の poll と同じ `2 × tb_hz` (TB の差) とする。
- P3-4: §5 の「受信は 1 byte ずつ廃棄」「MMIO は通常 2 ページ」を、cap まで保存・窓 3 ページに直した (上の §5)。

## 確定後の追記 (2026-11-18、R-b の設計の欠け: USB の物理の MMIO と VIA の窓、astra 起草、Opus 1 回)

## PowerMac reset 設計追記 r1（確定）— 遅れて判明する OHCI の物理窓

### §2　割当てと判定時点

実機では VIA の reg は mac-io の子オフセットで、親のメモリ BAR の割当て内に収まる。
OHCI の BAR0 は別の PCI function のメモリ資源である。正常なファームウェアの
割当てでは、両装置が同じ CPU 物理ページをデコードする配置は生じない。
OS32 が使う mac-io の割当てと PCI 親窓の区別は docs/ppc/open-firmware.md に従う。
QEMU mac99 でも OpenBIOS の macio の assigned-addresses が割当て済み基底を持つ
（docs/ppc/qemu-mac99.md の E1・E2）。OHCI も PCI BAR の割当てを受ける。
いずれも正常配置での結論であり、OF の木だけから後刻の BAR0 の値は証明できない。
VIA の最初の書き込みは局面 1 の pm_via_prepare() で、USB の BAR の照合 (局面 2 の pm_usb_boot) より前になる。通常のファームウェアの割り当てでは重ならないので、この順は変えない (R-b のレビューの P1 は到達しないことで受け入れる)。後の照合は USB の側の診断として、重なりを見たら LOG 1 行を出して USB だけを外し、VIA は残す。

kernel_boot.c の slots[] と USB 固定 VA の静的検査は VA を扱う。
VIA は takeover 後の walk 成功で公開する。比較対象は via_alias.pa と via_alias.bytes
（ページ丸め済みの実際の写像）とし、レジスタ先頭や予約上限 PM_VIA_BYTES は使わない。
同ファイルに機種内照会を置き、via_window.h には関数宣言を足す。

```c
int pm_via_phys_overlap(u32 pa, u32 bytes)
{
    return via_window.rc == 0 &&
           overlap(pa, bytes, via_alias.pa, via_alias.bytes);
}
```

pm_usb_boot の BAR0 読出し・形式検査の次に、usb_platform.c の usbp_mmio_map を呼ぶ。
同関数で現 root のメモリ ranges を選び、BAR を CPU PA に変換し、ページ整列を確認する。
その直後、maps[] の既存 BDF 検索・スロット取得・paging_map_phys のいずれにも先立ち一度比較する。

```c
if (pm_via_phys_overlap(pa, PM_USB_PAGE)) {
    usbp_log("USB SKIP bdf=%02x:%02x.%u pa=%08x reason=via-overlap",
             f->bus, f->dev, f->fn, pa);
    return -1;
}
```

pm_usb_boot の usbp_cfg_cmd(..., 2, 0) は map 成功後、takeover 前に移す。
cfg_cmd 失敗時は作成した USB map を unmap して次へ進む。
衝突時は当該 OHCI のみを採用せず、上記一行を残して列挙を続ける。
PCI command の変更、OHCI MMIO、HC 登録は行わない。公開済み VIA での reset と起動を維持する。

### §7　試験と変異

host fixture で PCI 基底と CPU 基底を異ならせ、変換後の BAR ページを VIA 写像の
先頭・末尾ページに順に重ねる。各回とも SKIP 一行、map 発行・cfg_cmd・takeover は各 0 回、
VIA の va/rc は保存値のまま、続く非衝突 HC は登録まで進むことを確認する。
VIA の直前・直後の隣接ページ、VIA 未公開の場合は通常の USB 経路を通す。
変異は比較の pa を bus に置換して衝突例を RED、overlap の境界を含端にして隣接例を RED。
QEMU は既存 USB 起動と reset の正常経路で確認し、実機の BAR 再配置は試験条件にしない。

### §6　実装単位

R-b の直し一段。platform/powermac/{kernel_boot.c,via_window.h,usb_platform.c,usb_boot.c}
と tools/tests/test_powermac_reset.py 配下の試験登録・host fixture を対象とする。
system.c は公開済み窓の利用者のまま。共通層・装置層・KAPI の変更は不要。

### 追記の確定 (2026-11-18、Opus 第 1 回、PM が反映)
- R-b の astra のレビューの P2 3 件 — (a) platform/powermac/kernel_boot.c の install_via が pm_usb_roots の構成の窓 (usb_boot.c の root->pa+0x800000 と +0xc00000) と照合していない、(b) platform/powermac/via_tree.c の ranges が別 BDF・別 BAR でも数値が mac-io の BAR0 の内なら採用する (BAR トークン付きの親番地では BDF・BAR の一致を必須に)、(c) via_tree.c で ofw_child/ofw_peer の失敗 -1 を正常の終端 0 と同じに扱う (-1 は探索の失敗として伝播、候補の発見の後に失敗する fixture を足す) — は、この追記の範囲外で、R-b の実装の直しとして PM が出す ((a) はこの追記の照合と同じ段にまとめてよい)。
- 根拠の実物の値: QEMU mac99 で VIA は pa=80016000 (R-b の LOG)。OHCI の BAR0 の値は実装の段の QEMU で LOG に出して並べる。G4 の値は未確認。
- 試験の土台 (PM 決定): 新しく tools/ppc/usb_via_overlap_host.c を作り、差し替える関数は paging_map_phys・usbp_cfg_read32・usbp_cfg_write32 (または今の cfg の書き込みの関数)・pm_usb_roots の窓・OHCI の start。登録は tools/tests/test_powermac_reset.py の CHECK (powermac-reset) に足す。
