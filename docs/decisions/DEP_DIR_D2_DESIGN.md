# DEP_DIR_D2 — 入力・出力・時計を固定の操作表の後ろへ — 設計

> 状態: **現行 (2026-10-26)** — 確定 (astra 起草・Opus 2 往復・PM 決定 F5・F6・F10)。段の状態は票 TASK_DEP_DIR が持つ。
> 出所: ~/os32-tmp/run/depdir-d2/DESIGN.final.md を 2026-10-26 に写した。冒頭の『r1→r2 の変更』は往復の記録として残す。


## r1→r2 の変更
- F1: driverヘッダの明示パスと例外規則、ime・kselftest・起動・sysの宣言追従を追加。
- F2: KAPI audit fixtureにサービスとpc98表を接続し、変異の呼出名を更新。
- F3: 既存fixtureの追従一覧・担当段・fake表/関数名の扱いを追加。
- F4: 入力段をA1とA2に分割し、A1→A2→B→Cの直列に変更。
- F5: get_fattimeをclock_read_date経由にし、1980起点の結果を維持。
- F6: ver_collectのserial_get_statusをport_get_statusへ変更。
- F7: bind位置をkstack_hw_init()の直後へ訂正。
- F8: #45 PCI側の「後」への追記はPM担当と明記。
- F9: check_p2vのROOTSにplatformが無い事実とA1での確認・記録を追加。
- F10: D1→D2の直列取り込み、D1取り込み後のmainをD2の実装基点と明記。

## 1. 実物の確認
- 対象: `/home/hight/os32-v3` main `4256d6c2`。SURVEY は作業ディレクトリの `SURVEY.md`、基点 `d0f5b0e8`。以下のパスは main 基準。
- 単一利用者・単一タスク・協調切替。権限・ABI・待ち方・IRQ制御・装置の設定値を変更しない。整数のみ。archive は参照しない。
- **食い違いは3群**（行ずれを群単位で数える）。その他の対象は呼出し・所在とも一致。設計上の範囲限定は食い違いに数えない。

|群|SURVEY → 現行 main の実物|判定|
|---|---|---|
|18|`kernel/console/console.c:262,301,377` の rshell 複写→serial_putchar|一致|
|19|`lib/serial_diag.h:6–7` は extern 2本。`arch/x86/kernel/isr_handlers.c:66`、`arch/x86/kernel/double_fault.c:78`、`kernel/kaudit.c:32`、`kernel/gui.c:100`、`exec/exec.c:976`|一致。協調ログと例外出力を混同しない|
|20–24|`include/kbd.h:6`、`exec/exec.c:1085`、`kernel/gui.c:118,146`、`kernel/ime/fep_module.c:22,45,59`、`fs/vfs_fd.c:468`|一致|
|25|`kernel/sys.c:164–171` の RTC_Time/read/年補正|一致|
|33–34|`kapi/kapi_generated.c:651,831,2119`、`sdk/kapi.json:96,311,1584`|一致。後続のkbd診断も移す|
|35|SURVEY `kapi/kapi_generated.c:735` → 現行 wrapper先頭730、実呼出738|食い違い1（行ずれ）|
|38|SURVEY `kapi/kapi_generated.c:1538` → 現行 wrapper先頭1533、実呼出1541|食い違い2（行ずれ）|
|45|SURVEY `kapi/kapi_sys.c:318,334` → 現行PCI呼出337、serial呼出353（関数350）|食い違い3（行ずれ）。D2ではserial側のみ|

- `kernel/ver_collect.c:24` の状態収集もBで `port_get_status` へ置換し、includeをport_service.hへ変更する。
- #45 の PCI はバス管理であり本段から除外。票の「シリアル診断」を満たし、PCI側の既存検査・呼出しは後段へ残す。
- 生成器は `kapi/` にはなく **`sdk/gen_kapi.py:564–585`**。`build/kernel.mk:147–154` が生成物一式を更新する。
- 既存の手本は `drivers/base/dev.h:38–65` の操作関数表、`fs/vfs.h:92` のread、`kernel/ime/fep_module.c:65–73` の部品API。devレジストリ自体は拡張しない。

## 2. 共通の境界・配線
- 共通契約を include/、薄い転送を kernel/、PC-9821固有の表・変換を drivers/ または platform/pc98/ に置く。
- 動的探索・名前解決・新しいioctl体系・所有者レジストリを作らない。各種1表、寿命は常駐全期間、解放・稼働中の差し替えなし。
- `platform/pc98/d2_bind.c` の `pc98_d2_bind()` が各const表を登録。`kernel_main` の `kstack_hw_init()`（現行 `init/kernel.c:199`）の直後で呼ぶ。I/Oは実行せず、装置初期化順は従来のまま。
- 起動の具体配線はD3までこの1呼出しを許容。D3で同じ関数をplatform起動一覧へ移す。drivers→kernelの参照は残す。

```c
/* include/input_service.h 等: 名前は設計名。表は必須項目を全て埋める。 */
void input_register(const InputOps *ops);     /* bootのみ、IRQ公開前 */
void pointer_register(const PointerOps *ops);
void port_register(const PortOps *ops);
void clock_register(const ClockOps *ops);
/* kernel側: 静的null実装を初期値にし、登録ではポインタだけを交換。 */
static const InputOps *input = &empty_input;
void input_register(const InputOps *p) { input = p; }
int input_getchar_kapi(void) { return input->getchar_kapi(); }
/* platform側: 既存driver関数をそのまま表に入れる（型の違う所だけadapter）。 */
void pc98_d2_bind(void) {
    input_register(&pc98_input); pointer_register(&pc98_pointer);
    port_register(&pc98_port); clock_register(&pc98_clock);
    con_output_register(&pc98_console_output);
}
```
- null表は無入力=-1、状態/計数=0、void操作=no-op、エラーを返せる制御=既存のNOSYS。時計のnullは0時刻。通常起動では必ず実表を使うことをhostで検査する。
- 登録の失敗・未登録を正常起動と見なさない。登録は静的な必須配線なので割当失敗経路は作らず、未登録試験は「固まらない」の確認に限る。
- 緊急出力は後述のリンク時固定表。登録以前の例外でも従来のUART既定ポートで出力できる。

## 3. 入力（kbd・mouse・stdin・exec・GUI・FEP）
```c
/* include/kbd.h: 共通キー値・SHIFT_*・RAW_*のみ。driverをincludeしない。 */
typedef struct InputOps {
    int (*getchar)(void), (*getkey)(void), (*trygetchar)(void);
    int (*trygetkey)(void), (*peekkey)(void), (*trygetchar_local)(void);
    int (*getchar_kapi)(void), (*getkey_kapi)(void), (*getkey_wait)(int);
    int (*has_key)(void), (*is_pressed)(int), (*trygetrawkey)(void);
    u32 (*modifiers)(void), (*dropped_count)(void);
    u8 (*shift_state)(void);
    void (*gui_mode)(int), (*discard_stop)(void);
    int (*diag)(KbdDiag *), (*diag_log)(u32, KbdDiagLogEnt *, int);
} InputOps;
/* SHIFTビットの値を変えない。modifierとの同一性を仮定しない。 */
static u8 pc98_shift_state(void) { return kbd_shift_state; }
/* kernel/input_service.c: execの責務は終了理由の判定まで。 */
void input_owner_exit(int aborted) { if (aborted) input->discard_stop(); }
/* execの既存1085行の位置で input_owner_exit(kind == EXEC_KIND_ABORTED); */
typedef struct PointerOps {
    void (*poll)(MouseState *);
    int (*available)(void);
    void (*bounds)(i16, i16, i16, i16);
    void (*cursor_mode)(int), (*cursor_show)(void), (*cursor_hide)(void);
} PointerOps;
```
- 19項目+6項目。公開転送は `input_*` / `pointer_*`。既存 `kbd_*` / `mouse_*` の実装・名前はdriversに残し、二重リングを作らない。
- `drivers/input/kbd.h` は `#include "../include/kbd.h"` で共通定義を読む側へ反転（"kbd.h"では自己参照になる）。I/O、8251定義、リング容量・タイムアウト、init/IRQ・kbd_shift_state宣言はdriver内。guard名を別々にする。
- `MouseState`（i16×4+u8×2=10B）、ボタン・モード・カーソルの公開値は `include/pointer_service.h` へ。ドライバ内部のバスマウス/NP21操作は移さない。
- `KbdDiag`/`KbdDiagLogEnt` は既存SDK共有型を利用。kbd注入APIはカーネルの既存機構であり、表へ迂回させない。
- 状態の持ち主: cooked/rawリング・STOP集約・押下/修飾・GUIモードはkbd driver、座標/範囲/前回poll/カーソルはmouse driver、注入リングとownerは既存kernel。
- 文脈: 転送は協調文脈/KAPI内のみ。IRQは既存driver handlerが同じ状態を更新。getkey_waitのinterruptible・park・ring3_abort_checkの位置は移さない。新たにcli/stiを足さない。
- GUIの118/146行だけ `input_set_gui_mode(1/0)`。cooked抑止開始/解除の順序、登録失敗時・終了時の復帰位置を保つ。
- driverヘッダの直読は装置部品（kernel/ime/ime.c・kdebug/kselftest.c）に許容し、kbdは `"../drivers/input/kbd.h"` を明示する。D3c2 で起動配線の例外（init/kernel.c の kbd・mouse）は解消し、platform/pc98/boot.c が読む。ime.cのkbd_getkey_wait/trygetkey/trygetcharは保持し、fep.mod importを改名しない。
- kernel/ime/fep_module.cのshift getterとfallback/関数表をinputへ。`getkey_wait(1)`→負値→abort_check、con_sink時の生バイト、拡張キー除外を維持。fep.mod import名・世代は変更しない。
- execではSTOP掃除の位置だけ転送。`kbd_inject_owner_exit(id)`（1091）とdiscard（1112）の順序・対象ownerを変更しない。
- stdinは既存canonical処理をVFSに残す。新しいTTY・Deviceインスタンスは不要。
```c
/* fs/vfs_fd.c: リダイレクトの先行判定、size==0、部分読出しはそのまま。 */
int c = input_getchar_kapi(); /* 従来kbd_getchar_kapiと同じprovider */
if (c < 0) return i ? (int)i : c;
if (c == '\r') c = '\n';
p[i++] = (u8)c;
if (c == '\n') break;
```

## 4. 通常コンソール・シリアル・障害時診断
```c
typedef struct ConOutputOps { int (*putchar)(char); } ConOutputOps;
void con_output_register(const ConOutputOps *); /* 1出力先、bootのみ */
/* console.cの3箇所: rshell_activeの判定・文字の順序は残す。 */
if (rshell_active) con_output_putchar(*str);
typedef struct PortOps {
    void (*init)(u32), (*puts_kapi)(const char *);
    int (*getchar_kapi)(void), (*putchar_kapi)(char);
    int (*trygetchar)(void), (*is_initialized)(void), (*init_vfast)(u32);
    int (*get_status)(u32 *, u32 *, u32 *);
    void (*diag)(u32 *, u32 *, u32 *, u32 *);
} PortOps;
/* unsigned long baudとの関数ポインタ型不一致をcastで隠さない。 */
static void pc98_port_init(u32 baud) { serial_init((unsigned long)baud); }
typedef struct EmergencyOps {
    void (*puts_polled)(const char *);
    int (*gate_active)(void);
} EmergencyOps;
extern const EmergencyOps emergency_output; /* platformがリンク時に供給 */
/* kernel/serial_diag.c: 既存名を共通の口として残す。 */
void serial_puts_polled(const char *s) { emergency_output.puts_polled(s); }
int serial_gate_active(void) { return emergency_output.gate_active(); }
/* platform/pc98/d2_bind.c: 登録前から有効、書換なし。 */
const EmergencyOps emergency_output = {
    pc98_serial_puts_polled, pc98_serial_gate_active
};
```
- `serial_putchar_kapi`等の正確なプロトタイプはdrivers/char/serial.hに合わせる。表のinit_vfastにもbaud変換adapterを置く。
- consoleのrshell_activeはconsoleが保持（KAPI setterも維持）。con_sinkへの複写・描画抑止と独立、改行変換は従来serial_putcharに任せる。複写を一括putsに置換しない。
- UART状態・gate・hold/RXリング・エラー計数・高速モードはserial driverが保持。ポート表への転送は協調/KAPI文脈。診断snapのIRQ扱いも既存serial_diag_getと同じ。
- `kapi_serial_diag`のNULL判定と0戻りを保ち `port_diag_get(&out->oe,...)` に変更。SerialDiagの並び・出力検査は既存どおり。
- drivers/char/serial.cのputs_polled/gate_active実体だけpc98_接頭辞へ変更。driver内部で共通口を再帰呼出ししない。共通宣言をlib/serial_diag.hから読ませる。
- lib/serial_diag.hの整形helperと既存呼出し名は維持。kdebug import・fixtureの大量改名を避ける。kernel/execに不要となるserial.h includeは外す。
- 協調ログは呼出側の `!serial_gate_active()` 判定を保持。例外/二重障害/teardown失敗は従来どおりgateを迂回してpolledへ。
- 緊急表はIRQ/例外/IF=0で呼べる。割当・ロック・通常console・hlt・校正依存待機を使わない。`drivers/char/serial.c:875–905` の有限spin、改行順、io_waitを維持。
- driver依存の全体撤去ではない。serialfs等drivers側からserialへの呼出し、D2外の装置専用APIは残す。

## 5. 時計とKAPI生成
```c
/* include/clock_service.h: legacy ABI型と内部の西暦型を区別。 */
typedef struct { u8 year, month, day, wday, hour, min, sec; } RTC_Time;
typedef struct { int year; u8 month, day, wday, hour, min, sec; } ClockDate;
typedef struct ClockOps {
    void (*read_date)(ClockDate *);
    void (*read_legacy)(RTC_Time *);
} ClockOps;
/* drivers/char/rtc.c側adapter: readは1回。80/2000/1900は既存定数を使用。 */
static void pc98_read_date(ClockDate *d) {
    RTC_Time t; rtc_read(&t);
    d->year = t.year + (t.year < RTC_YEAR_PIVOT ? RTC_YEAR_BASE_NEW : RTC_YEAR_BASE_OLD);
    d->month=t.month; d->day=t.day; d->wday=t.wday;
    d->hour=t.hour; d->min=t.min; d->sec=t.sec;
}
/* sys_time: ClockDate t; clock_read_date(&t); datetime_to_epoch(t.year, ...); */
```
- RTCのBCD・ビット送受信・2桁年補正はdriver内。epoch演算は既存整数datetime_to_epochをsys側で使う。read_legacy=rtc_readで7Bの旧ABIを厳密維持。
- `const ClockOps pc98_clock = { pc98_read_date, rtc_read };` はdrivers/char/rtc.cに置き、platformはexternの表を登録する（static adapterをファイル外から参照しない）。
- `fs/fatfs/diskio.c:386–397` のget_fattimeはclock_service.hを読み、ClockDateへ `clock_read_date(&t)`、年は `t.year` を使う。自前pivot補正を除き、DOS_YEAR_BASE=1980の減算・ビット詰め・秒/2・読出し1回を維持する。
- ClockOpsは協調文脈のみ。時刻キャッシュや周期IRQを追加しない。異常値の丸め・再試行も足さず、現在の読出し結果を保存する。
- sdk/kapi.jsonのincludesを共通入力/ポインタ/時計/ポートヘッダへ置換。全kbd entry（diag/log/local/rawを含む）はtarget=input_*、全mouse entryはtarget=pointer_*。
- serialの8操作はtarget=port_*。serial_diagのbodyはkapi_serial_diagのまま。kbd_inject/pending、rshell_set_activeはそのまま。
- rtc_read bodyを `clock_read_legacy((RTC_Time *)rtc_time);`、mouse_poll bodyを `pointer_poll((MouseState *)info);` とする。outのsizeof型は共通型へ移すだけ。
- `sdk/gen_kapi.py:569–580` は既にbody/targetを解決するので生成ロジック追加は不要。生成器の回帰試験に「公開nameは同じ、呼出先だけ変更」を足す。
- `python3 sdk/gen_kapi.py`で再生成。kapi_generated.c手修正禁止。slot/name/ret/args/in/outの意味・cdecl・容量300・version79・ABI世代1を維持（公開KAPI変更なし）。
- sizeof/offsetofのhost32検査でRTC=7B、MouseState=10Bを固定。SDKヘッダ/slots/exec初期化表の公開差分がゼロであることを照合する。

## 6. 大きさの見積もり
|部分|常駐text概算|rodata/data概算|bss増分|
|---|---:|---:|---:|
|入力25口・転送・null表|+500〜1,200B|+200B前後（実表+null表）|+8B|
|port9口・console1口・緊急2口|+250〜700B|+88B前後|+8B|
|clock2口・変換移動|+80〜250B|+16B前後|+4B|
|配線・不要直結コードの削減|−150〜+100B|0〜32B|0B|
|合計（整列込み予算）|**常駐 +1,000〜2,700B、上振れ枠4,096B**|左記合算|約20B（初期値付きpointerはdata扱い）|
- 未ビルドの設計見積もり。driverコードはリンクに残るので大きな削減は見込まない。表はi386関数pointer=4B。圧縮増分は同率と仮定しない。
- 参考の既存build/out（HEAD一致未検証）: kernel.bin=366,987B、vmkernel.lz4=223,926B、kdebug.bin=121,092B。これをmainの測定値とは扱わない。
- vmkernelは `boot/boot_defs.h:116` の508KiB=520,192B以内。常駐は `include/memmap.h:230` のMEM_KERNEL_IMAGE_MAXとlinker検査で別途確認。
- 起動FDは2HD/1.44MBの両方。MINIMALの品目を増やさず、`check-packages-host`でBOOT+MINIMAL一致と空き64KiB以上（tools/tests/test_packages.py:76）を確認。空きの実値は実装時に記録。
- kdebugは既存import名を保つのでコード増分0B見込み、pair再生成は必要。image/mem上限各262,144B、load_span524,288B（tools/module_schema.py:55–56）を据置。
- 4KiB枠/FD空き/常駐/モジュールのいずれかを超えれば転送・重複表を削って再計測。上限緩和やMINIMAL品目の除外をこの段で行わない。

## 7. ファイルごとの変更一覧（生成物を除く概算）
|段|新規/変更ファイル|行数・内容|
|---|---|---|
|A1/A2|新 include/input_service.h、pointer_service.h；kernel/input_service.c|各60/35/110、A1で入力、A2でpointerの契約・null表・転送|
|A1/A2|変更 include/kbd.h、drivers/input/kbd.h、drivers/input/mouse.h|計120、A1でkbd明示パス、A2でmouse共通定義移動|
|A1/A2|新 platform/pc98/d2_bind.c、変更 init/kernel.c・build/kernel.mk|100+各3〜5、入力表・配線入口、kbd/mouseのdriverヘッダ明示|
|A1|変更 kernel/ime/ime.c、kdebug/kselftest.c|各1、"../drivers/input/kbd.h"で既存宣言を保持|
|A1|変更 fs/vfs_fd.c、exec/exec.c、kernel/gui.c、kernel/ime/fep_module.c|各2/3/4/12、呼出先のみ|
|A1/A2|変更 sdk/kapi.json→生成物一式|約35、A1は入力、A2はポインタtargetとinclude|
|B|新 include/port_service.h、console_output.h；kernel/port_service.c、serial_diag.c|計180、通常/緊急の口|
|B|変更 platform/pc98/d2_bind.c、drivers/char/serial.c・serial.h、lib/serial_diag.h|計90、表・実体改名・共通宣言|
|B|変更 kernel/console/console.c、kapi/kapi_sys.c、exec/exec.c、arch/x86/kernel/isr_handlers.c・double_fault.c|計20、転送・include整理|
|B|変更 kernel/ver_collect.c|呼出し1行とinclude、port_get_statusへ|
|B|変更 sdk/kapi.json→生成物、build/kernel.mk|計20、serial targetとソース登録|
|C|新 include/clock_service.h、kernel/time/clock_service.c；変更 drivers/char/rtc.h・rtc.c|計80、共通型・転送・adapter|
|C|変更 kernel/sys.c、platform/pc98/d2_bind.c、sdk/kapi.json→生成物、build/kernel.mk|計35、時計接続、sys.cの"rtc.h"をclock_service.hへ|
|C|変更 fs/fatfs/diskio.c|約10、clock_read_date経由・重複年補正除去|
|A1/A2/B/C|新 tools/tests/d2_services_host.c・test_d2_services.py；変更関連host fixture・check_map.d/*.yaml・build/checks.d/*.mk|新250〜350、既存100〜200、段別assertと変異・依存地図|

## 8. 受入→試験・変異、既存利用者、実装段
|受入|host / ゲストNP21/W試験|検出する変異（各1つ）|
|---|---|---|
|依存方向・初期配線|host: 対象TUのincludeと未解決symbol検査、fake表で呼出先記録、起動配線検査|input登録1行を除去|
|stdinの従来契約|host: 0長/CR/LF/途中負値/64KB要求/redirect優先；guest: head、パイプ、リダイレクト|CR→LF変換を除去|
|exec/GUI/STOP|host: ABORTEDのみdiscard、注入owner清掃順；guest: CTRL+STOP→shell復帰、GUI起動終了・再起動|abort条件を反転|
|FEP/入力|host: fallbackの拡張キー/負値/con_sink、shift専用getter、kbd注入/dlog；guest: IME on/off、矢印、修飾、kbdstat -w|shift getterを0固定|
|pointer|host: pollの座標・差分・範囲・show/hide各転送；guest: seamless移動/クリック/カーソル表示|boundsのx/yを交換|
|console/serial|host: rshell on/off、出力順/改行、全port操作；guest: rshell、gserial、高速設定、serialfs往復|putcharを常時複写|
|診断/障害|host: gate中の協調抑止・例外迂回、IF=0/Tx未readyでも有限完了、4計数；guest:診断読出しと既存障害試験|polled先を通常serial_putcharへ変更|
|時計|host: 年79/80/00/99、閏日、legacy7BとFAT時刻32bitの旧結果比較；guest: 日時表示と既存rtc KAPI|年pivotの<を<=へ|
|KAPI不変|host: 全対象slotのfake呼出し・引数・戻り・sizeof/offsetof、生成を2回行い差分ゼロ|targetを旧driver名へ戻す|
|容量/全体|host: make all、packages・layout・module上限；guest: 両FD/HDD起動、LAN・シリアル・入力の既存smoke|試験用コピーの容量上限を生成物未満にする|

- F9: `tools/check_p2v.py:11` のROOTSはplatform/を含まない。A1で静的検査の対象設定を確認し、表だけのd2_bind.cが対象外であることを検証記録に残す。実装者が静的検査一式を追加実行する意味ではない。
- F2: test_kapi_audit.pyのwrapper抽出fixtureにA1でinput_service.cとpc98入力表、Bでport_service.cとpc98ポート表を接続・登録し、未定義input_*/port_*を解消する。生成wrapper名は保持する。
- F2: 変異は公開名からの `fn+'_kapi('` 合成をやめ、呼出先の明示対にする。入力はinput_getchar_kapi→input_getchar・input_getkey_kapi→input_getkey、Bのserialはport_putchar/puts/getcharの各_kapi→通常口、ime/fmは既存対を維持し、置換元の一致をassertする。
- F3（以下tools/tests/）: A1でring3_guard_host、trim_{flow,stop,kernel}_hostはinput_set_gui_modeをfake InputOpsへ接続。fep_standard_host、b8_open_host、ext2_empty_name_host、font_source_hostはinput_getchar_kapiをfake表へ接続する。
- F3: A1でtest_exec_r1.py:147のstub/fixtureをinput_discard_stopへ追従し、:506の完全一致変異を `if (kind == EXEC_KIND_ABORTED) input_discard_stop();` に更新。owner清掃順・ABORTED限定のassertを維持する。
- F3: A2でpointer wrapperのfixtureをfake PointerOpsに接続し、A1で更新した入力fixtureを含めて追従漏れを確認する。multiapp_model_hostは実ソースをincludeしない模型（:8）なので関数名置換・表の注入は不要、trim各fixtureとの回帰をA2で確認する。
- F3: Bでcon_sink_host:68、pegc_mode_host、gfinit_output_host.hの出力記録をcon_output_putchar経由のfake ConOutputOpsへ接続し、従来の出力assertを保持する。
- F3: Bでcpu_calibrate_host・serial_portc_host・serialfs_session_hostの試験ハーネスを確認し、serial.c取込み部分にはpc98表と共通診断口を接続、実体参照をpc98_serial_puts_polled/pc98_serial_gate_activeへ追従する。共通serial_puts_polledが未定義にならず二重定義も無いことを確認する。
- F3: Cでfdc_track_hostのrtc_read差し替えをfake ClockOpsへ接続し、get_fattimeの1980起点・秒/2を含む結果を照合する。
- 既存利用者の追随は段の完了条件（入力はA1、pointerと追従総点検はA2、serialはB、rtcはC）。`tools/tests/vfs_fd_path_host.c:233,1375`、vfs_kind/vfs_excl/vfs_fd_sqlite/fstat_redir等のkbd/rtc差し替えはfake共通表へ接続し、driverをfixtureに丸ごとリンクしない。
- FEPのsqlite_module_host/x11_shared_hostと文字列変異test_x11_shared.pyを新しいinput呼出しへ更新。kbd-status/dlog/kstopはdriver内部の既存試験を維持し、include分離による不足定義だけ追随。
- serial_diag_host.h、test_e10c_lifecycle.py、test_memmap_audit.py、test_double_fault.py、test_kdebug_imports.pyは公開名維持で期待を保存。driverを直接含むserial_gate_host/serial-vfastはpc98実体名と共通口の二重定義を解消する。
- check-kapi-{version,layout,out,ranges,bounds,audit}、check-vfs-fd-path-host、check-fep-{boundary,standard}-host、check-rshell-serial-host等の既存assertを弱めない。check_selectの地図に新ヘッダ/表/fixture/生成元を登録。
- 各段で make all → 足した/触った試験MUTATE=0 → check_select.py --lint。上表の変異は該当段で検出確認し、取り込みはcheck-changed。設計作業ではビルド/ゲスト試験未実施。

|段（Codex実装枠）|範囲と完了点|順・並列可否|
|---|---|---|
|A1: 110分|入力核・共通配線35分、呼び手/明示include/KAPI35分、入力fixture/試験40分|D1取り込み後のmainから開始。pointerは既存経路を保持|
|A2: 100分|pointer25分、fixture追従・総点検45分、host/guest30分|A1後。入力の回帰も通し、Aを閉じる|
|B: 120分|console/port/緊急表40分、KAPI/ver_collect/fixture45分、host/guest35分|A2後。共通ファイルの編集・生成・取り込みは直列|
|C: 110分|時計/get_fattime35分、ABI/容量/既存利用者検査35分、全体ゲスト40分|B後。A1→A2→B→Cの順に完了|
- F10: 取り込みはD1→D2の直列。D2の実装基点はD1取り込み後のmainとし、init/kernel.c・build/kernel.mk・kdebug/kselftest.cのD1変更を保持して差分を載せる。r1の行番号は照合用であり基点SHAを意味しない。
- 各段は単独で旧機能が動く状態まで閉じる。共通ファイルの担当はその段に一本化。ゲスト用環境が利用可能な前提の作業枠で、試験不能を成功扱いにしない。
- ゲスト配備は既存手順。NHD/マスター上書き・ini変更はこの設計作業の承認に含まない。実装検証では停止済みの試験用コピーを用い、不可逆操作は[C1]〜[D4]に従う。

## 9. 未決（ユーザー判断が要るものだけ）
- **0件**。#45のPCIは後段、診断は固定の別口、公開ABI/上限は据置を推奨案として採用。サイズ実測とゲスト結果は実装時の検証事項であり設計選択の未決ではない。
- F8: 票の「後」へ#45（PCI側）を足すのはPM担当。D2ではシリアル診断側のみを完了対象とする。

反論: 0件。

## 確定 (2026-10-26、Opus 5.5 の第 2 回で条件なしの承認)

- r1 の指摘 F1〜F10 は r2 で直った (REVIEW.r1.md)。F8 (#45 の PCI 側を票の「後」の行へ) は PM が票に入れた。
- PM 決定: F5 (FatFs の get_fattime を clock_read_date 経由) と F6 (ver_collect を port_get_status) は入れる。D1 → D2 の直列、D2 の実装の基点は D1 を取り込んだ main。

## A1 の実装で埋めたこと (2026-10-26、PM が F1 の規則を当てはめた)

- `kernel/v86/v86_kbd.h:25` (v86.mod の部品、KBD_DATA・KBD_CMD を使う) は F1 の「部品は "../drivers/input/kbd.h" を明示して読む」に入れる (ime.c・kselftest.c と同じ扱い)。ポートの定数は PC-98 のドライバの内部定義のまま drivers/input/kbd.h に置き、include/kbd.h へ戻さない。host 試験 kbd-status・kbd-dlog の fixture も同じ明示のパスに追従する。

## D2 の取り込みで D3 へ送る項目 (2026-10-26、astra のレビュー P3 2 件)

- `kernel/input_service.c:68` の `input_owner_exit` は製品のコードから使われず試験だけが呼ぶ — 宣言・定義・専用の試験を消し、実際の exec の終了の経路の検査を残す (D3a で)。
- `kernel/ver_format.c:6` が `KBD_IRQ` のためにドライバのヘッダを直接読む (F1 の例外の一覧の外) — D3 で kernel.c の例外を解くときに、ver の装置の行の値を input の口か platform から取る形にする。
