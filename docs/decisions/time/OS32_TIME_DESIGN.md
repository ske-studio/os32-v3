# OS32_TIME_DESIGN — os32.time (ホストからの定期の時計合わせ・RTC への書き込み・手動の時刻合わせ)

> 出所: ~/os32-tmp/run/os32-time/DESIGN.final.md (astra 起草 r1→r2、Opus レビュー 2 回、N1・N2 は PM が反映)。
> 状態: **現行 (2026-11-05)** — 段の状態は票 TASK_NET_HOSTSVC が持つ。

## r1→r2 の変更
- R1: sys_time/clock_read_date のローカル互換・未設定時RTC直読・datetime_to_epoch再利用を明記。
- R2: NP21/WはRTC_FAILEDと壁時計設定を受入、I/O TIME SET対応はPM管理の独立段。
- R3: RTC読みは現行パラレルを維持、Ra266の年確認後に必要なら別段で修正。
- R4: 内部snapshot型をwall_snapshotに改名し既存time_snapshotとの衝突を解消。
- R5: RTC試行→gate解除→offset保存を契約としSFS上の設定保存を試験。
- R6: sys_time・newlibの互換維持理由とtime_mathのpending桁上がり対応を変更一覧に追記。
- R7: snapshotの単調時計欄をmedia_/raw_付きに改名。
- R8: media補正上限を固定500ppmとし壁時計の設定上限から分離。
- R9: 立上り・20µsの根拠の未照合を明示し実機波形確認へ移動。
- PM-SIZE: 8〜13KiBはvmkernelの圧縮前増分、起動FD上の実増分を統合時に実測。
- 作成: Codex（GPT-6 ベース、詳細モデル ID は環境から確認不能）。状態: 実装提案。
- 対象: PC-9821、シングルタスク・協調切替、整数のみ。NET_HOSTSVC_DESIGN の確定契約上に追加する。
- この票だけを作成。リポジトリ変更・git操作・preflight・ビルド・実行試験は行っていない（読み取り専用指定）。

## 1. 実物の確認
| 項目 | file:line と確認内容 |
|---|---|
| RTC | `drivers/char/rtc.c:47,110,168`: パラレルコマンド、48bit読出し、ClockOpsは読みだけ。書く口なし |
| 年 | `drivers/char/rtc.h:20`: pivot=80、1980〜2079。`rtc.c:145`: 不正日付の一部を丸めて正常に見せる |
| tick/µs | `arch/x86/kernel/idt.c:19` はu32、`kernel/time/ktime.c:4,10,14,56` は10ms＋PIT補間、IRQ損失と約497日周回は未対応 |
| KAPI | `sdk/kapi.json:125,195,1648`: get_tick、rtc_read、sys_time_now。後者はUTCでなく起動からのµs（`include/sys.h:47`） |
| sys_time | `sdk/kapi.json:687`、`kernel/sys.c:168–173`: ローカル暦をepochとするu32。newlib (`sdk/crt/syscalls.c:130–135`)・SQLite (`lib/sqlite3/os32_sqlite_vfs.c:655–661`)・FAT (`fatfs/diskio.c:393`) の時刻源 |
| date/hdate | `userland/shell/cmd_base.c:126,134`: 引数無視、年を20xx固定表示。`userland/cmds/hdate.c:4,43`: ホスト表示のみ、RTC未更新 |
| TIME | `tools/host_agent.py:791`: ローカル時刻の秒精度文字列、UTC offset・世代・送信時刻なし |
| 設定 | `init/sysconfig.c:117` はsystem.cfg整数読出し。`kernel/sqlite_engine.c:214` はsettings.dbのsystem/ival。`userland/lib/cfg/libos32cfg.h:82,109` にDBの共通口 |
| 設定設計票 | `docs/decisions/settings/DEVICE_RESERVATION_DESIGN.md:66,94` は資源予約・PCI configであり、時刻設定の永続化規約はない |
| ホスト | `docs/decisions/network/NET_HOSTSVC_DESIGN.md:120` 以降: 内部ALIVE非消費、IRQで同期しない、8要求、HOSTDOWN、LISTとhost_call |

r1で挙げた食い違い・未確認点の扱い（R3/R9を反映、未確認を修正根拠にしない）。
1. `rtc.c:40,115,123` はパラレル48bit。Bible 2-4も48bit、io_cal.mdに40bitの記載なし、NP21/Wも48bit。外部メーカー資料だけでは年無効と断定せず現行読みを維持。PMがRa266の現行dateの年をシリアル出力で確認し、不正時だけ別段で読みをシリアル方式へ直す。
2. `rtc.c:84` の立下りと外部メーカー資料の立上りの対応は未照合。読みの変更根拠にせず§9の実機波形で確認。
3. `rtc.c:115` 後の待ちと外部メーカー資料のTIME READ後20µsの適用条件は未照合。§9で確認し、読みの待ち時間変更は別段で判断。
4. `rtc.c:158` の1980〜2079と`cmd_base.c:134` の20xx固定が相違。新dateはフル年を表示。
5. `rtc.c:2` のμPD4990A一律表記とUNDOCUMENTEDの9821搭載品表が相違。4993(A)の互換モードも実機対象。

## 2. 時刻の持ち方
```c
/* 内部型。u64/i64は整数のみ。公開境界はu32の上下に分ける。 */
struct time_state {                 /* kernel/time/wallclock.c が単独所有 */
    i64 utc_us; u64 raw_anchor_us;  /* POSIX UTC、1970起点、うるう秒は数えない */
    i64 slew_left_us, last_error_us, last_sync_utc_us;
    u64 last_sync_raw_us; i32 offset_min; /* local = UTC + offset_min*60秒 */
    u32 flags, source, wall_seq, host_generation, rtc_result;
};
int time_snapshot(struct wall_snapshot *out); /* UTC/単調/状態を同じ採時で */
int time_apply_host(i64 target_us, i32 offset_min, u32 generation, int force);
int time_set_local(const struct time_local *in); /* full year/month/day/hour/min */
void time_poll(void);              /* IF=1の協調実行点、非待機、再入なら何もしない */
```
- UTCは符号付き64bit µs、通常の算術は足算・比較・32bit係数との乗算。公開は秒(lo/hi)＋usecへ変換。暦変換は既存`lib/os_time.c:26`の`datetime_to_epoch`を再利用し、旧u32契約を維持。内部64bit/負値の拡張が要る範囲は同ファイルに集約し、負値の除算は床丸め。
- 64bit除算は既存ライセンス適合helperを確認して使用、なければ時刻算術ファイルに定数除算を実装。[L1]に従いlibgcc依存を発生させない。
- `sys_time_now`の意味・既存slotは維持。IRQ0のtick更新点で上位tickを加算し、PIT snapshotのpending補正で桁上がりする場合も上位へ伝播させる。読出し間隔に依存する周回検出にはしない。
- 64bit snapshotと時計状態commitだけIF保存/cli/復元。暦変換・通信・設定I/O中はIF=1。長いCLIによるtick欠落は保証外のまま記録する。
- 起動時RTCを一度読んでtickに接続。切断中もその基準＋tickで継続し、RTCを毎秒読み直して壁時計を揺らさない。RTC自体も自走する。
- RTCの生BCD・実日数を検証。不正ならUTC_VALID=0、LOCAL_VALID=0で起動し、年月日を捏造しない。形が正常でも電池切れの古い日時は検出不能なのでRTC_UNVERIFIED。
- RTCはホストのローカル暦を格納。最後のoffsetが保存済みならUTCへ戻す。初回でoffset不明ならローカル表示だけ有効、手動設定も可能、UTC_VALIDはホスト取得まで0。
- 既存`clock_read_date`はローカル暦、`sys_time`はその暦を`datetime_to_epoch`でu32 epoch化する互換契約を維持。内部UTCを直接返さず、newlib localtime・SQLite・FATを9時間ずらさない。UTC/offsetの管理はos32.time内に閉じる。
- 一度も合わせていない未設定時、またはLOCAL_VALID=0では`clock_read_date`は従来のRTC providerをそのまま呼び、sys_timeも従来どおり変換する。FATもこの従来値を使う。新APIの無効flagsやdateの「時刻未設定」と旧APIの互換値は区別する。
- offsetはホストの現在値のみ（夏時間込み）。切断中は最後の値を固定し、復帰時に更新。OS32に地域名・夏時間表を持たない。

## 3. ホストとの時計合わせ
```text
要求: os32.time NOW 1\n                 （本文なし、host_callでも呼べる）
成功: 200、本文24B、全フィールドLE、構造体memcpy禁止
offset 0:u32 version=1; 4:u32 host_generation;
8:i64 unix_us; 16:i32 utc_offset_min; 20:u32 reserved=0
旧 TIME: 従来のYYYY-MM-DD HH:MM:SS文字列をそのまま維持
LIST: os32.time 1 <世代内id> builtin       （実装が有効になった時だけ登録）
```
- ホストは応答生成直前の`time.time_ns() // 1000`を採り、その同じinstantのローカルoffsetを整数分で求める。分未満offsetは503、形式違反は400。POSIX秒、秒60は送らない。
- カーネル同期本体はSDKをリンクせず、既存open/status/read/closeを使う非待機状態機械。同じwire契約をSDKの`host_call`で公開する。
- 最初のopen直前をm0、完全な応答を受けた直後をm1とし、受信時推定値=`unix_us+(m1-m0)/2`。HELLO待ちは採時に含めず、UPになってから開始。
- 応答組立遅延・非対称回線を含む単純推定。表示誤差目安はRTT/2＋採時/協調遅延、保証精度ではない。RTT・採用誤差を公開。1試行1標本、RTT>1000msは不採用。
- m0/m1は調整しないraw µsを使う。採用はversion/長さ/offset(-1440..1440)/UTCの表現範囲/要求時と現在の世代一致を検査してから。古い応答は破棄。
| 設定（system.cfg、起動時読込） | 既定・範囲・意味 |
|---|---|
| TIME_SYNC_SEC | 300、10〜86400秒。起動時/復帰時は周期と無関係に直ちに要求 |
| TIME_STEP_MS | 2000、0〜60000ms。通常同期の絶対誤差が超過ならstep、0なら常にstep |
| TIME_SLEW_PPM | 500、1〜10000。閾値以下の壁時計誤差をこの最大速度で寄せる（mediaは固定上限500ppm） |
| TIME_OFFSET_MIN | 未設定可、-1440..1440。最後にホストから得たoffsetのキャッシュ |
- 不正設定は該当キーのみ既定に戻して1回ログ。settings.dbと二重の正本にしない。後の設定UIは同じsystem.cfgのキーを更新する。
- offset変更はRAMに採用して保存待ちを記録し、RTC試行→最新世代のgate解除→設定保存の順に行う。保存中のSFS再HELLOはgateを通過でき、time_poll再入に依存しない。新世代発生時はその先行処理を優先し保存を繰り越す。
- offsetが変わった時だけtime_configが既存KEY=VALUE方式で他行を保って保存。読取専用/書込失敗でもRAMには適用、PERSIST_FAILEDを表示。毎回の同期時刻は永続化しない。
- 通常slewは経過raw µs×ppm/1000000の符号付き補正を残量まで適用、余りを保存。新標本は現在の補正済み壁時計との差で残量を置換する。
- 起動時初回・世代変化後・手動は壁時計をstep。通常同期は上の閾値。offset変更だけでもローカル表示とRTCを書き替える。
- **採用ごとにRTCへホスト推定の現在ローカル時刻を書く**。slew中の壁時計ではなく同期目標を使う。RTC失敗でも壁時計は採用しRTC_FAILEDを別表示。
```c
/* kernel/time/host_time.c所有、要求は最大1本、ownerは終了しないkernel owner */
struct time_job { u32 generation, barrier_generation, phase; int handle;
                 u64 m0, deadline, next_due; int busy; };
int time_rebind_gate(u32 generation); /* 0=先行試行済、AGAIN=先行処理中 */
/* IDLE -> OPEN -> STATUS/READ -> APPLY -> RTC -> CLOSE -> UNGATE -> PERSIST -> IDLE */
```
- 内部ALIVE snapshotでホスト世代変化を検出し、旧handleをclose、最新世代に統合。通知リングを消費しない。同世代ALIVEの観測通番だけでは周期をリセットしない。
- 新世代のサービス再OPEN・netcd BOPEN・SFS再HELLOの前にgateを置く。linkのHELLO/ACK/ALIVE、TIME自身は通す（接続確立の循環待ちを避ける）。既存資源の失効は即時。
- gateは最新世代でTIME完了→RTC書込み試行が終わるまでAGAIN。外部待機者はyield、カーネルはtime_pollを進める。世代が再変化したら旧結果で解除しない。
- 無応答1000ms/HOSTDOWN/未提供501時はその試行を失敗として終了、同期不能を公開してgateを解除し他サービスを進める。復帰順序を守りつつRTC故障だけで全サービスを永久停止させない。
- 失敗は5秒後に再試行、非UP時は要求を開かず既存1秒HELLO再同期を待つ。同世代の断線復帰でもDOWN→UPで即試行。新世代は5秒待ちを打ち切る。
- 要求8枠を増やさず最大1枠使用、BUSYなら次の協調点へ。既存handleを横取りしない。TIME無応答は当該handleだけHOSTDOWN、session全体の再同期条件は変更しない。
- time_pollは起動後・共通協調park点・CUI/GUIのidle経路に結線。IRQ0はtick延長とdueフラグだけ。協調点に戻らないアプリ中は同期が遅れる（割込み通信で補わない）。

## 4. RTC
- メーカー一次資料: [NEC μPD4990A User's Manual IEU-1210](https://www.renesas.com/en/document/mat/serial-io-real-time-clock-ic-upd4990a-users-manual) §2.2.2、§3.2/Fig.3-4,3-6（印刷p13–14,21–22）。
- 機種資料: `/home/hight/os32-v3/docs/hw/undocumented/io_cal.md:19,26,56,103`（指定のio_rtcに相当する実ファイル）、`PC9800Bible/2-4_カレンダ時計.md:45,49,77`。前者優先。
- BSD手本候補: [FreeBSD sys/pc98/cbus/pcrtc.c](https://raw.githubusercontent.com/freebsd/freebsd-src/releng/10.4/sys/pc98/cbus/pcrtc.c)。取得失敗で本文・ライセンス・file:lineを未確認。**HW3未完了、RTC実装着手前にPMが取得し照合する技術ゲート**。推測の行番号を付けない。
```c
int rtc_read_checked(ClockDate *out);  /* 生BCDと暦の検証、丸めない */
int rtc_write_date(const ClockDate *local); /* 検証後1 transaction、0/IO/INVAL */
static void rtc_serial_cmd(u8 cmd);   /* C2..C0=111、cmdをLSBから4clock、STB */
static void rtc_shift_bit(u8 bit);    /* DI設定 -> CLK↑ -> CLK↓、各wait */
```
1. 入力の全項目をI/O前に検証。秒/分/時/日/年はBCD、月は1..12の二進4bit、曜日0..6は暦から計算。year%100を格納。
2. シリアルHOLD(0000)→SHIFT(0001)。DATA INから秒LSB順に48bit、続けてTIME SET(0010)の4bitを入れSTBを立てる（52bit鎖の末尾4bitを忘れない）。
3. TIME SETでカウンタを止めて転送、直ちにHOLD(0000)をserial送信・STBし計数再開。SET後の4clockはデータ領域をシフトしない。
4. 書込みのコマンド設定/DI setup/CLK high/low/STBは各2µs以上の暫定値。既存io_waitのPC-98待ちを使い定数化し、HW3照合と§9の実測で下限を確定。
5. 読みと書込み後の読戻しは現行パラレル48bitのまま。シリアル書込み終了時は現行読みのコマンド状態へ戻す。読みのシリアル化はRa266の年確認で不正が出た場合の別段とする。
- transaction内は協調切替なし、irq_save/restoreで元のIFを保存。変換/秒境界待ちをCLI内に置かず、I/O部分の連続IF=0を1ms未満（必須10ms未満）で測定。
- RTCは秒精度。目標のusecを切り捨て即時書込み、1秒未満の量子化誤差を明記。読戻しが書いた秒または次秒なら成功、それ以外はIO。再起動後の保持を成功条件にしない。現行NP21/WはI/O TIME SETを捨てるため通常RTC_FAILEDとなる（偶然読戻し一致なら成功を見かけるため試験は元RTCと異なる日時を設定）。
- 書込み可能年は既存pivotと往復一致する1980〜2079。範囲外ホスト値は64bit壁時計には採用するがRTC_RANGE、手動はINVAL。2100対応をRTCへ暗黙に折り返さない。
- 4993(A)の互換I/Oは実機で確認。0x22の不明ビットを書き換えない。年越し・閏年・電源断での欠点は機種資料と併記し、動作確認前に全9821保証とはしない。

## 5. 手動の時刻合わせ
- `date`=現在ローカル壁時計、`date -s YYYY-MM-DD HH:MM`=手動設定（秒0）、`date -sync`=即時ホスト同期、`date -status`=状態表示。既存hdateは表示専用を維持。
- 書式・実日数・年範囲はカーネルでも検査。不正なら壁時計/RTCとも変更なし。引数なしはoffset不明でもLOCAL_VALIDなら表示、未設定なら「時刻未設定」。
- `time_set_local`へdateも将来コントロールパネルも入る。offset既知ならUTCへ変換、不明ならローカル仮基準を設定しUTC_VALID=0。timezone指定UIは不要。
- 手動設定は進行中TIMEをcloseして世代とは別の操作通番を進め、古い応答を不採用にする。RTCへ書いてsource=MANUAL、次の定期同期は設定周期後。ホスト復帰イベントは即時優先。
- RTC失敗時も壁時計設定は有効、dateは「設定済・RTC書込み失敗」と非0を返す。同期設定を永久停止しない。

## 6. アプリの口
```c
/* 公開C89、全て32bit単位、ABIでsizeof/offsetを固定。 */
typedef struct Os32TimeSnapshot {
 u32 size, flags, source, wall_seq, host_generation;
 u32 utc_sec_lo, utc_sec_hi, usec, media_mono_us_lo, media_mono_us_hi;
 i32 utc_offset_min; u32 last_sync_sec_lo, last_sync_sec_hi;
 u32 error_us_lo, error_us_hi, last_sync_raw_us_lo, last_sync_raw_us_hi;
 u32 rtt_us; i32 last_error, rtc_result;
} Os32TimeSnapshot; /* 80B、sec/errorは上下を結合し符号付き解釈 */
int time_get(Os32TimeSnapshot *out, u32 size);
int time_set_local(const Os32LocalMinute *in); /* u32 year,month,day,hour,minute */
int time_sync(void); /* 非待機受付、既に要求中はAGAIN、結果はtime_get */
```
- flags=LOCAL_VALID/UTC_VALID/OFFSET_VALID/SYNC_VALID/SLEWING/HOLDOVER/RTC_FAILED/PERSIST_FAILED、source=UNSET/RTC/HOST/MANUAL。最後の同期は成功時だけ更新、失敗理由は別欄。未同期のlast_syncはSYNC_VALIDで判別。last_sync_raw_usはraw µs基準の経過測定用（mediaとは別）。
- KAPIポインタは一時kernelコピーを使い検証してから副作用、size=80を初版契約にする。[ABI]でkapi.json実装時末尾に3本追記・版上げ・C/Rustラッパー生成、旧slotは維持。
- **raw単調µsは締切専用、壁時計はUTC・表示用**。audio/midi/screenには別の共通media単調µsをsnapshotのmedia_mono_us欄で渡す。
- mediaは起動時0、raw経過＋最大min(TIME_SLEW_PPM, 500)ppmの補正で進む。media用残量・端数は壁時計と独立に保持し、壁時計が10000ppmでslewしてもmediaは500ppmを超えない。負補正でも速度>0。初回同期/手動/大幅stepでmediaを飛ばさず、wall_seqを進めて新しいUTC↔media対応を公開する。
- 再生開始時に同じsnapshotのUTC/media/seqを組で保存し、全streamをmedia差分で駆動。壁時計step後も開始済streamの基準は固定、新規開始は最新対応を使う。未同期開始も可能だがUTC対応は無効と表示。
- `sys_time`はローカルepochの秒精度を維持、newlib `_gettimeofday`のtv_usec=0も維持。UTC/µsが必要な新アプリはtime_getを使う。
- `sys_time_now`は補正を受けないまま。rtc_readの7byte ABIは維持し物理RTC読出しを続ける。通常dateと新アプリは新APIを使う。

## 7. 部品の大きさ
| 像 | 増分予算（未実測、圧縮前） | RAM/FD |
|---|---:|---|
| vmkernel（起動FDにはvmkernel.lz4として収録） | 時計算術/状態3〜5KiB、同期/設定3〜5KiB、RTC/結線/KAPI2〜3KiB、計8〜13KiB | 固定状態/作業領域2KiB以内、既存要求ring1本を借用 |
| シェル/SDK | dateと薄いwrapper 1〜2KiB | シェル収録分はFDに影響 |
| host_agent | Python 2〜4KiB | ホストのみ、FD影響0 |
- 8〜13KiBはvmkernel本体の圧縮前増分であり、FD消費量ではない。起動FDの残り約23KiBには触れる。新旧vmkernel.lz4＋実際に収録するshell/SDKのFATクラスタ割当差を2HD/1.44MB別に実測する。圧縮率を仮定して収まったと言わない。
- lan.mod/cd.modはgateの小さな結線のみ（各0.5KiB以内を目標）。新しい常駐DB/daemon/通知ringは足さない。超過時だけPMが除外候補と実測値を提示、勝手に既存機能を外さない。

## 8. ファイルごとの変更一覧
各行は1ファイル=1実装者=1段。主機能は500行目安、既存大ファイルには呼出しだけ。ヘッダは共有契約の例外。
| 段/ファイル | 変更 |
|---|---|
| A1 新include/time_service.h | 内部契約、型・状態・gate宣言 |
| A2 sdk/include/os32/os32_kapi_shared.h | 公開型/flags/版、生成器管理箇所は生成する |
| B1 新kernel/time/calendar_math.c | BCD境界・検証、暦変換はlib/os_time.cへ委譲 |
| B1a lib/os_time.c | datetime_to_epoch再利用、必要な64bit暦拡張・整数除算を集約、旧u32契約維持 |
| B2 kernel/time/ktime.c | 64bit tick基準とPIT pending桁上がり |
| B2a kernel/time/time_math.c | time_decideのp1=1時t+1がFFFFFFFF→0となる繰上りをktime側へ伝え、IRQ側上位tickと二重加算しない |
| B2b kernel/time/time_math.h | 既存time_snapshotにpending繰上りの内部契約を追加、wall_snapshotとは別型 |
| B3 kernel/irq/tick.c | tick_dispatch先頭でktime tick延長呼出し、dueフラグ（実更新はisr_stub.asm:453） |
| B4 新kernel/time/wallclock.c | UTC/media状態、slew、snapshot、手動設定 |
| C1 drivers/char/rtc.c | checked readは現行パラレル、writeはシリアル（HW3照合後） |
| C2 drivers/char/rtc.h | 新宣言・I/Oタイミング定数 |
| D1 新tools/host_time.py | NOW応答生成、整数採時、旧TIMEに影響なし |
| D2 tools/host_agent.py | サービス/LIST登録、D1へdispatch |
| D3 新kernel/time/host_time.c | 非待機同期、世代/操作通番、gate、期限 |
| D4 新kernel/time/time_config.c | system.cfg読込とoffset差分保存、既存KEY=VALUE規約 |
| E1 init/kernel.c | 初期化・起動初回poll、設定読込順序 |
| E2 exec/exec_schedule.inc | park_kbd/park_poll/sys_yieldのCUI分岐より前でpoll、再入回避 |
| E3 net/link_host_api.inc | 再OPENのgate、TIME自身は内部フラグで通す |
| E4 drivers/block/netcd.c（確定票の新設先） | BOPEN前gate |
| E5 fs/serialfs/sfs_client.c | 再HELLO前gate、既存期限内で待つ |
| E6 tools/prototypes/lan_imports.def | netcdからtime_rebind_gateを輸入、部品契約世代更新 |
| E7 kernel/time/clock_service.c | clock_read_dateは有効なローカル壁時計、未設定/無効時は従来RTC provider。legacyは物理読出しを維持 |
| E8 kernel/sys.c | sys_timeのローカルepoch/u32契約を明記、clock_read_date→datetime_to_epochの処理維持 |
| F0 sdk/crt/syscalls.c（変更不要） | _gettimeofdayはsys_timeとtv_usec=0を維持。UTC/µsは新APIで取得 |
| F1 新kapi/kapi_time.c | CPL3検証、3 APIの実体 |
| F2 sdk/kapi.json | 末尾追記、生成対象列挙、版同期 |
| F3 新userland/shell/cmd_date.c | 表示/設定/同期/status、本体分離 |
| F4 userland/shell/cmd_base.c | 旧cmd_date除去、新関数の宣言・登録 |
| G1 build/kernel.mk | 新カーネルソース/KAPI登録 |
| G2 build/programs.mk | 新dateソースのリンク |
- 生成ファイルはF2の生成成果物であり別実装者に手編集させない。app.conf要求版、確定設計・KAPI仕様の記録は各1ファイルずつ統合段に分ける。E4/E5は前提設計の着地時に実パスを照合する。

## 9. 受入 → 試験と変異
| 独立の試験ファイル（tools/tests/test_time_*.py）/段 | 観測による受入 → 1行変異 |
|---|---|
| calendar / B1 | host:1980/2000閏日・不正BCD・月12・2079境界が正しい → 月をBCD変換 |
| clock / B2–B4 | host:tick FFFFFFFF→0、p1=1のpending繰上りと次IRQで二重加算なし、負slew/stepでもmedia非減少、raw不変 → 上位tick桁上がり削除 |
| rtc / C1 | host:I/O traceで書込み48+4bit/SET→HOLD→パラレル復帰、読戻し、IF復元 → SETをHOLDに変更 |
| protocol / D1–D2 | host:24B LE、UTC/負offset/旧TIME文字列、同一instant → offset符号反転 |
| sync / D3 | host:100ms RTTで+50ms、1000ms無応答は1handle、最新世代のみ採用 → 古い世代検査削除 |
| order / E1–E8 | host:満杯通知ring・連続世代変更でもTIME→RTC試行→gate解除→offset保存、SFS上system.cfgの初回保存・再HELLO成功、BOPENはRTC後、同世代復帰も同期 → 保存をgate解除前へ移動 |
| config / D4 | host:周期/閾値/ppm、10000ppm設定でもmediaは500ppm以内、読取専用でも同期継続、他設定保持 → 不正周期を既定に戻さない |
| api / F1–F4 | host:bad pointerで無副作用、手動中旧応答破棄、1980表示、hdate表示のみ → 手動時の操作通番更新削除 |
- 互換API試験 / E7–E8・F0: UTC+540分でsys_time/newlib/SQLite/FATが従来のローカル値、LOCAL_VALID=0と未設定時は従来RTC値、tv_usec=0 → sys_timeをUTC秒に変更。
- NP21/W: 起動/5分周期/停止再起動、切断中に壁時計継続、date -sの秒0・RTC_FAILED（非0終了）と設定どおりの壁時計、CUI/GUI idle双方、既存hdate・TIME・sys_time_now回帰。RTC読戻し一致・再起動後保持は期待しない。各期待値と世代/順序をログ照合。
- 独立段 N（PM管理、OS32段とは独立）: `~/np21w-src`フォークにI/O TIME SETを実装し試験の幅を広げる。現状はupd4990.c:39–41で破棄、calendar_setはbios1c.c:24のINT 1Chのみ（REVIEW r1の確認）。OS32のRTC書込み受入は実機で行う。
- 実機: まずPMがRa266の現行dateの年をシリアル出力で確認、不正なら読み修正を別段化。4990/4993A搭載機で書込み読戻し、CLK/STB待ち・CLI時間、立上り/立下りとDATA OUTの対応・TIME READ後20µsの必要性・開始bitの重複/欠落を波形で確認（外部資料の適用条件も照合）、12月/年越し/閏日、電池なし電源再投入→RTC未検証→初回同期、ホスト停止・復帰、82557/LGYで順序確認。電源操作はPMが承認を得て実施。
- 段順: A契約→B算術/時計→C RTC→Dホスト→E結線→F API/date→G統合。各試験も1ファイルの段として対応実装の受入に含め、CHECK inputsに全依存を宣言。
- 実装時は各段の対象像ビルド・check-changed MUTATE=0・追加/変更試験--mutate・--lint。変異は一意anchorで実行時RED、既存anchor移動も照合。PM統合時[ABI] clean→allとFD実測、独立レビュー、NP21/W/実機。今回は全て未実行。

## 10. 未決
- ユーザー判断待ちは **0件**。既定値・失敗時の有限gate解除・1980〜2079手動範囲は上記を推奨案として具体化。
- 技術上の未確認: HW3のBSD本文/ライセンス/file:line照合、前提設計の着地先、像の実増分、実機波形。これらは実装着手/統合の検証項目で、ユーザーへの仕様質問とは数えない。
- 把握している危険: 確定ホスト設計と同じく平文・無認証でホストを信用する。受信元許可リストは追加しない。

反論: 0件。

## 確定 (2026-11-05、Opus 第 2 回 Approve、PM が反映)
- N1 (P3): §9 の NP21/W — 定期の同期での RTC_FAILED と成功の別は判定に使わない (NP21/W の calendar_get は Windows の時計で、秒が合えば成功する)。判定するのは `date -s` (元の RTC と違う日時) の RTC_FAILED だけ。
- N2 (P3): §4 の 5 — Ra266 で今の `date` の年を確かめる前は、書き込み後の読み戻しの照合を時・分・秒・日・月に限る。年の照合は確かめた後に入れる。
- PM の宿題: Ra266 で今の date の年を確かめる (実機ノート)、NP21/W のフォークの段 N (I/O の TIME SET) を立てる。
