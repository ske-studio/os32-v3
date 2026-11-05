# GeForce 6200 (NV44A) 層 5 (その他) — 確定設計: コアとメモリのクロック・温度とファン・電圧・タイリング・状態の保存と復元

> 出所: ~/os32-tmp/run/gf-feat5/DESIGN.final.md (astra 起草 r1〜r3、Opus レビュー 2 回、r3 は PM が確認)。
> 状態: **現行 (2026-11-02)** — 段の状態は票 TASK_GF_FEATURES が持つ。層 1〜4 は GF_FEAT1〜4_DESIGN。

# GeForce 6200 / NV44A 層5 詳細設計 r3

## r2→r3 の変更
- G1: AUTO/MANUAL/NONE、温度別duty・段階制御・熱しきい値を前景tickに追加。
- G2: vmap優先とprev/min_idの選択順を訂正。Ra266の表有無は資料不足を§1に明記。
- G3: PM決定に従いsnapshot上限を撤廃し、確保失敗だけENOMEM。

## r1→r2 の変更
- F1: RAM→fan上げ→volt上げ→core/shader→volt下げ→fan下げへ統括順を訂正。
- F2: clockのpauseを除き、RAM切替の停止をram_prog区間だけに限定（PM決定）。
- F3: base 64KiB整列・sizeを64*pitchへ切上げ・枠の再利用を確定。
- F4: 追加MIT refsでNV4a温度・VID/GPIOのレジスタと順を確定。
- F5: perf fanspeed・ファンPWM・cstate上下操作を追加。
- F6: A段のCHECK/共通py、B別host.c、正常対照、終了値、試験台帳を追加。
- F7: Ra266の別shaderはEBIOS、無書込のhost試験を追加。
- F8: 既存像/import実測と層3確定予算・層4への残量を計上。
- F9: 退避extentの具体例・上限・確保失敗時の扱いを追加。
- F10: 電圧選択を要求以上の最近値へ訂正。
- PM: 実機はROM状態間とboot復帰まで、温度は手本式・外部温度計なし、未決0件。

## 1. 実物の確認
- Ra266 vmap確認: 指定refs・作業資料・OのROM decoder/host試験に実ROMのBIT P値を確認できるデータは見つからず、有無は未確認。`N/nvkm/subdev/bios/vmap.c:29–53`のBIT P v2+20hのLE32ポインタと版10/20hを実装時のinfoで出す（v1/ポインタ0は不在）。合成ROM試験を実機の根拠にしない。
- 対象: OS32 / PC-9821 Ra266、PCI 0:13.0、10de:0221、chipset=4ah。実機を今回測定したという意味ではない。
- O=`/home/hight/os32-v3/`、N=`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/` (指定 c03c1a1)、E=`/home/hight/os32-tmp/refs/geforce6200/envytools/`。以下の file:line はこの取得物。
- 票 `O/docs/tasks/realhw/TASK_GF_FEATURES.md`、比較 `O/docs/decisions/realhw/GF_FEATURE_COMPARE.md:52–55`、確定 F1=GF_FEAT1_DESIGN §2・3、F2=GF_FEAT2_DESIGN を前提とする。層3の窓/面台帳と層4の保存型を境界とし、容量は[共通合算表](GF_FEAT3_DESIGN.md#合算予算)に従う。
- ユーザー決定: 「基本的にすべての機能が同じレベル（GUI以外）には用意されるべきです」「実際の動作確認は実装後のテストでもいいし実機でテストで動作チェックすればいい」。GUIから呼ばずカーネル試験入口まで作る。
- GNU11、整数演算、kstring、番地/サイズの定数3層管理、P2V等の変換境界、[D1]〜[D4]を維持。MITのN/Eのみを実装資料としGPLのPCI/DRM/TTM実装を写さない。メーカーの詳細資料は指定資料内に無く、メーカー確認済みとはしない。

| 実物との食い違い（4件、利用できると思い込みやすい境界） | 確認根拠と設計への反映 |
|---|---|
| D1 初期化PLLは動的変更関数ではない | `O/hwdev/gf/pll.c:6–15,27–51`、`kdebug/gfinit_pll/sequence.inc:25–51` はdevinit用。Nの動的列を§2・3に新設する。 |
| D2 ROM decoderは汎用perf表/任意script実行器ではない | `O/kdebug/gfinit_decode.c:10–13,76–112`、`gfinit_decode/tables.inc:1–13` は登録済みspan。表解読を新設し、BIT Mのscriptは検証済みspanへ追加する。 |
| D3 refs欠落は解消（r1から訂正） | 追加された同じc03c1a1・MITのtherm/volt/gpioで§4・5を確定。clk上位の列も`N/nvkm/subdev/clk/base.c:159–215,266–305`に存在する。 |
| D4 GFの終了/局所復元はOS suspendではない | 比較`:54`。Oのkernel・arch/x86/kernel・includeでsuspend/resume/ACPI/power_managementをgrepした結果はアプリ切替のみ。全ファイル名のACPI/APM/suspend/power/pm検索も該当なし。OS電源管理/サスペンド機構は確認範囲に無い。 |

## 2. クロック（core・shader）と共通境界
- 全操作は前景の協調切替地点で行う。IRQは記録だけ。`gf_production`が`gf5`を所有し、F1/F2・表示部品の寿命内に置く。割込み禁止で5ms/2秒を待たない。
- 誤りは層5内部の負値、video境界は既存0/-1へ変換し`last_rc`をシリアルに残す。F1/F2の元rcも保存する。

```c
/* 新規 hwdev/gf/feature5.h。内部ABI、全型は固定幅。 */
enum gf5_rc { GF5_OK=0, GF5_EINVAL=-501, GF5_ENOTSUP=-502,
  GF5_EBIOS=-503, GF5_ERANGE=-504, GF5_EBUSY=-505,
  GF5_ETIME=-506, GF5_EIO=-507, GF5_ENOMEM=-508, GF5_ESTATE=-509 };
struct gf5_clock { u32 core_khz, shader_khz, mem_khz; };
struct gf5_plan { u32 nc, nn, sc, mux, mc, mn; struct gf5_clock actual; };
struct gf5; struct gf5_image; struct gf5_tile; struct gf5_thermal;
struct gf5_links { /* F2/F4 adapterが提供、追加のmodule importではない */
  int (*drain_pause)(void *); /* 受付閉→dlでfence完了→F2独自2秒のFIFO pause→GR idle */
  int (*unpause)(void *);    /* resume後last_rc/stateを照合し復帰失敗を返す。A0で凍結 */
  int (*display_save)(void *, struct gf5_image *);
  int (*display_restore)(void *, const struct gf5_image *);
  int (*memory_script)(void *, u16 pc); /* decode済みBIT M scriptのみ */
  void *owner;
};
int gf5_clock_read(struct gf5 *, struct gf5_clock *);
int gf5_clock_calc(struct gf5 *, const struct gf5_clock *, struct gf5_plan *);
int gf5_clock_prog(struct gf5 *, const struct gf5_plan *); /* FIFO pauseなし */
int gf5_ram_prog(struct gf5 *, const struct gf5_plan *);   /* この呼出しだけdrain_pause/unpauseで囲む */
int gf5_perf_select(struct gf5 *, u8 index); /* 表・fan・電圧・両clockを統括 */
```

| 順/前提・手本 | OS32の列 | 誤り |
|---|---|---|
| calc: `N/nvkm/subdev/clk/nv40.c:124–142 nv40_clk_calc_pll` | ROM PLL limitsを読み、要求<第1VCO maxなら第2段無効化。既存`gfinit_pll_solve`の整数探索を再用。coreキー004000、shader004008。全計算は各progの書込前に完了。 | 表破損EBIOS、解なしERANGE |
| encode: 同`:146–182 nv40_clk_calc` | N2=M2ならnc=80000100\|(P<<16), nn=N1<<8\|M1。それ以外nc=c0000000\|(P<<16), nn=N2<<24\|M2<<16\|N1<<8\|M1。shaderが0/同値ならsc=0,mux=333h、異なるならsc=c0000000\|(P<<16)\|N1<<8\|M1,mux=223h。 | shaderは1段解、ERANGE。Ra266 v2x表は004008対応なしで別shaderはEBIOS（`bios/pll.c:46–52,245`の-ENOENT）、同値/0はcore共有 |
| prog: 同`:186–196 nv40_clk_prog` | RMW(c040,333h,0)→4004=nn→RMW(4000,c0070100,nc)→RMW(4008,c007ffff,sc)→5ms→RMW(c040,333h,mux)。RMW=(old&~mask)\|value。 | MMIO adapter失敗EIO |
| read: 同`:39–94 read_pll_1/read_pll_2/read_clk`,`:97–114` | c040のcore[1:0]/shader[5:4]で3=4000の2段、2=4008の1段。memは4020。ref=27000kHz、段ごと整数除算後>>P、enable/M=0は0、未知muxは0と状態値を返す。 | 0を正常な計測周波数として報告しないESTATE |

- PM決定: clockにはdrain_pauseを加えない。停止はram_prog区間だけ（tile/stateの固有の停止は別）。理由: OS32 の CPU と GPU の VRAM アクセスを self-refresh の区間から外すため。
- `O/kdebug/gfinit_pll/parse.inc:1–43,solve.inc:8–60` を再用するが、devinit用`sequence`を呼ばない。mem用bias_p/max_pは既存`O/kdebug/gfinit_pll.h:73`に有り再用する。type04から既存parserのレジスタキーへの対応をadapterで行う。
- 性能表: `N/nvkm/subdev/bios/perf.c:30–88 nvbios_perf_table/entry` のBIT P(v1/v2)+0、またはBMP>=25hの+94h。BIT系20h〜3fhはhdr=+1,cnt=+2,len=+3,snr=+4,ssz=+5、entry stride=len+snr*ssz。
- 解読は同`:91–145 nvbios_perfEp`相当：12/13/14h、21/23/24h、25h、30/35hを版別に処理。NV4aの21/23/24hはshader=LE16(+6)*1000、core=shader+s8(+8)*1000、memory=LE16(+b)*2000、voltage=+5、fanspeed=+4（`bios/perf.c:111–125`）。25hはcore(+6),shader(+a),memory(+c)*1000。版をまたいで倍率を共通化しない。
- APIのmem_khzはPLL実周波数。perf側の値とreadbackを同じ単位として照合し、DDR実効値を表示するなら別フィールドにする。表不在はENOTSUP、表長/範囲不正EBIOS、未知版ENOTSUP。境界検査はROM decoderと同じspan検査で行う。
- 性能状態の選択は明示要求。負荷追従の自動governorはこの手本列だけでは規則が決まらず、勝手に閾値を追加しない。全表の列挙と選択入口は提供する。

## 3. メモリのクロック
| 順/前提・手本 | OS32の列（番地は16進） | 誤り |
|---|---|---|
| 0 `N/nvkm/subdev/fb/ramnv44.c:27–41 nv44_ram_new` | 10020c&ff000000が容量、100474のbit2/1/0でGDDR3/DDR2/DDR1（後の判定が優先）。4ahはnv40 ram関数を共有。 | 不明RAMは診断にUNKNOWN |
| 1 同`ramnv40.c:34–63 nv40_ram_calc` | PLL type04を解読し解を求める。mc=80000000\|P<<16\|min(bias_p+P,max_p)<<20、単段bit100/2段bit40000000とmnはcore同様。 | EBIOS/ERANGE、まだ書込なし |
| 2 同`:78–113 nv40_ram_prog` | head=0,1の600808+2000*hの変化を1µs間隔・cnt++<32で判定。活動headのc03c4+2000*h=1→c03c5を保存。SR1 bit20が0のheadだけvblank bit10000の0→1を各2000ms待ち、SR1\|20を書く。 | 時間切れETIME、既にblankしたheadを戻す |
| 3 同`:115–120` | 1002d4=1(precharge)→1002d0=1→同=1(refresh×2)→RMW(100210,80000000,0)→1002dc=1(self refresh)。 | この区間にVRAMアクセス禁止 |
| 4 同`:122–147` | RMW(c040,c000,0)→**4ahはdefaultのみ** RMW(4020,c0771100,mc)→4024=mn→100µs→RMW(c040,c000,c000)。4038/402c/4044へ書かない。 | EIOは状態を記録 |
| 5 同`:149–156` | 1002dc=0→RMW(100210,80000000,80000000)→100µs→BIT Mが有ればLE16(M+0)のmemory reset script。 | script不適合は事前EBIOS、実行失敗EIO |
| 6 同`:161–175` | 対象headのvblank bit10000=1を2000ms待ち→index=1→保存SR1へ戻す。 | ETIMEでもSR1復旧を試み、投入は閉じる |

- 差分: 手本のvblank待ち結果は戻り値にしていない。OS32では試験の失敗を成功扱いしないためETIMEを返す。新しいPLL lock poll/独自の待ち時間は足さない。
- RAM区間: calc成功後にCPUのVRAM操作を閉じ、F2 drain_pause→ram_prog全体→成功時unpause/CPU受付再開。RAM区間はCPU窓/present/描画/flipの受付を閉じる。現行referenceはMMIO読出し (fence.c:57) でIRQにVRAM fence読出しは無く、架空の抑止hookは作らない。F1 MMIO ACKは継続する。clock/fan/voltまで停止を広げない（PM決定）。
- BIT M scriptは切替前に全decodeし、呼出先を含め既存runnerで実行可能か確認。固定main全体の再実行で代替しない。未登録spanの登録/試験がB2の必須作業（D2）。
- calc失敗は無変更。self-refresh以後のEIOでは旧4020/4024とc040を同じ切替順で戻す試行を1回行い、必ずself-refresh解除/auto-refresh復旧まで進む。再失敗はFAULT、F2停止へ渡す。これは手本に無い試験adapter失敗の後始末であり自動再試行ループは作らない。

## 4. 温度・ファン
| 手本/資料 | OS32の列 | 誤り |
|---|---|---|
| `N/nvkm/subdev/therm/nv40.c:29–69,73–102` | 4ahはOLD_STYLE。init:15b0=ff→20ms→15b4&ff。read:15b0=ff→15b4&ff。15b8には触らない。 | 非対応ENOTSUP、MMIO失敗EIO |
| 同`:89–100` | slope_mult/div・offset_num/denのどれかが0ならENOTSUP。t=raw*mult/div+num/den+offset_constant-8、各除算を整数で行い、負値は0へ。milli_c=t*1000。 | rawは取得可、換算不能を正式温度にしない |
| `N/nvkm/subdev/bios/therm.c:29–64,67–146` | BIT P v1+12/v2+16の表。hdr/len/cnt、core section・最初のsensor、offset/slope・閾値/ヒステリシスを解読。 | span不正EBIOS |
| `N/nvkm/subdev/therm/fan.c:223–277`、`bios/fan.c:29–93` | DCB FAN tag09を検索、PWM→toggle→制御不可の順。fan表はBIT P v2(+58h、長さ>=5ch)、v10のentry0: type+0、min/max+2/+3、freq=LE32(+b)&ffffff。無ければ`bios/therm.c:152–210`へfallback（type22のlow/high byte=min/max、type26=freq、3b/3c=上下周期、46=温度端点）。 | 制御不可ENOTSUP、破損EBIOS |
| `N/nvkm/subdev/bios/perf.c:201–215`、`therm/fanpwm.c:59–81` | perf v20〜3fかつhdr>6ならdivs=LE16(perf+6)。fan freq非0ならnv40にはpwm_clockが無いためdivs=1/freq。duty=(divs*percent+99)/100、NV40系はduty=divs-duty。pwm_set→pwm_ctrl(true)。 | 0 divisorを勝手に別値へ補正しない、readはGPIOへfallback |
| `N/nvkm/subdev/therm/nv40.c:105–165` | line2: RMW(10f0,7fff7fff,duty<<16\|divs)→RMW(10f0,80000000,80000000)。line9:15f8=divs→RMW(15f4,7fffffff,duty)→同bit31 set。disableは該当bit31 clear。 | line2/9以外ENOTSUP |
| 同`:120–145`、`therm/fanpwm.c:38–55` | enabledならline2はdivs=10f0&7fff,duty=(10f0>>16)&7fff、line9はdivs=15f8,duty=15f4&7fffffff。divs=max(divs,duty)、反転後duty*100/divs。無効/0ならGPIO論理値*100。 | GPIO不在ENOTSUP |
| `N/nvkm/subdev/therm/base.c:164–173`、`therm/fan.c:32–74,184–201` | cstateのdir>0は要求>保存値、dir<0は要求<保存値、dir=0は常時更新。min/maxへclamp（既定0/100、ROM優先）。 | 不正percent EINVAL |
| `N/nvkm/subdev/therm/base.c:106–158,185–215,361–370` | 起動AUTO（温度取得不能ならEINVAL）。MANUALは現在duty（読取失敗100）を即時設定し自動算出を止める、NONEは自動算出/設定を止める。AUTOだけ温度目標を1秒周期で再計算（OTHERの非0 cstate時は更新要求時のみ）。 | 不正mode EINVAL、制御不可ENOTSUP |
| 同`:37–103,128–145`、`bios/fan.c:29–93` | TRIPは到達した最後のtripと前回tripのhysteresisを使用。LINEARはROMの温度端点間でmin/max dutyを整数線形補間、外側は端点値。fan表解読時はLINEAR。OTHERのみ非0 cstateを使い、0なら30℃〜fanboost温度で線形補間。 | 表/span不正EBIOS |
| `N/nvkm/subdev/therm/fan.c:32–100` | AUTOは目標をROM min/maxへclamp後、readbackから最大3 percentage pointsずつ接近。残差があれば上昇bump_period/下降slow_down_period(ms)で再予約。readback不能時は目標へ直接。 | IO失敗EIO |
| `N/nvkm/subdev/therm/nv40.c:196`、`therm/temp.c:26–54,81–209` | pollingを初期化時と1秒ごとに実施。既定fanboost/downclock/critical/shutdown=90/95/105/135℃、hyst=3/3/5/5℃をROMで上書きしhyst最低2。温度>=閾値で上昇、<=閾値-hystで下降を一度ずつ通知。fanboost上昇は即時100%要求（共通clamp適用）→AUTO。downclock/criticalは登録済みemergency hookへactiveを渡す、未登録なら記録のみ。shutdown上昇は記録＋即時100%要求。 | 温度取得不能は記録し次のsensor pollを予約しない、IO失敗EIO |
| `N/nvkm/subdev/therm/fantog.c:39–62,81–115` | PWMをdisable→GPIO論理値を反転。period=100000µs、on時間=percent*period/100、off時間=period-onで次回設定。0/100到達後は再予約しない。GPIO列は§5を共有。 | 時刻予約/IO失敗EIO |

```c
struct gf5_thermal { u32 raw; i32 milli_c; bool calibrated; };
int gf5_therm_read(struct gf5 *, struct gf5_thermal *);
enum gf5_fan_mode { GF5_FAN_NONE, GF5_FAN_MANUAL, GF5_FAN_AUTO };
int gf5_fan_mode_set(struct gf5 *, enum gf5_fan_mode);
struct gf5_emergency_ops { void (*downclock)(void *, bool); void (*pause)(void *, bool); void *owner; }; /* 任意登録、temp.c:110–117 */
int gf5_fan_get(struct gf5 *, u32 *percent);
int gf5_fan_cstate(struct gf5 *, u8 percent, int dir);
void gf5_fan_tick(struct gf5 *, u64 now_us); /* AUTO算出/段階duty/sensor poll/toggle各期限を前景adapterから駆動、呼出し箇所はPMがA0レビューで凍結 */
```
- PM決定: 温度は手本の整数式、外部温度計は使わない。calibratedはROM係数で換算可能という意味に限定し、外部測定済みを意味しない。15b0、fanの制御mode/方式/目標/cstate/last_tripと閾値状態をsnapshotに含め、復帰時は各期限を現在時刻から再予約する。
- perf切替のfan上げ/下げはcstateの保存値更新とAUTO再評価を指す。TRIP/LINEARの温度目標をperf値で上書きしない。差分: 非同期workerが無いため各alarmを前景tickへ移す。前景の協調地点・診断待機から型付きpollを呼ぶ具体的箇所はPMがF5 A0宣言レビューで選定し、凍結までC2は出さない。IRQ/tickの既存F1 polling口とは分離し、IRQからfan/alloc/待ちは実行しない。u64 now_usはsys_time_nowのhi/loから構成し、下位u32を単純拡張しない。
- 差分: OS32に電源断機構が無いためshutdownのorderly_poweroffを記録＋ファン100%要求へ置換する。15a0/15a4の独自書込や熱IRQは追加しない。無ファン板は制御不可を記録する。

## 5. 電圧
| 手本/資料 | OS32の列 | 誤り |
|---|---|---|
| `N/nvkm/subdev/bios/volt.c:29–65 nvbios_volt_table`、`:69–100` | BIT P v1+10h/v2+0ch→表。12hはhdr=5,len=+1,cnt=+2,mask=+4。20hはhdr=+1,len=+3,cnt=+2,mask=+5。30hはhdr=+1,len=+2,cnt=+3,mask=+4。 | 不在ENOTSUP、破損EBIOS |
| 同`:137–150 nvbios_volt_entry_parse` | 12/20h: entry[0]*10000µV,VID=entry[1]。30h:同µV、VID=entry[1]>>2。GPIO初期化で確定したVID maskを適用し、要求以上で最も近いentryを選ぶ。 | ERANGE |
| `E/nvbios/power.c:73–87`、`E/nvbios/gpio.c:119–120,179–189,403–412` | 表位置・VID機能/極性の解読を補助。VID→指定電圧の対応を公開する。**実電圧ADCではない**。 | 対応なしENOTSUP |
| `N/nvkm/subdev/volt/nv40.c:26–44`、`volt/gpio.c:30–97` | VID0..7=DCB tag04/05/06/1a/73/74/75/76（`include/nvkm/subdev/bios/gpio.h:15–22`）。GPIO不在のbitはmaskから除く。get/setともbit0→7、setは各bitの論理値を順に出す。独自の整定待ちは加えない。 | 検索の非ENOENT/IO異常はEIO |
| `N/nvkm/subdev/gpio/base.c:80–108`、`gpio/nv10.c:29–79` | setはDCB log[state]からdir=(log>>1)&1,out=log&1。line0/1:reg600818,shift=16*line,mask11,data=dir<<4\|out。line2..9:60081c,shift=4*(line-2)、line10..13:600850,shift=4*(line-10)、後二者mask3,data=dir<<1\|out。RMW(reg,mask<<shift,data<<shift)。 | line>=14 EINVAL |
| 同上、`N/nvkm/subdev/volt/base.c:31–78,156–182` | senseは上記reg/shiftのbit8（line<2）/bit2（他）、log[1]&1との一致を論理値にしてVIDを合成、表からµVを返す。setはentry.uv-request>=0で差最小（同差は後勝ち、完全一致は最初で終了）。perf voltage IDは次行でmapし、prev=getの結果でcondition=0/prev取得失敗/指定方向への変化を判定後、min_idのmap成功時にmax(request,min)を取りsetする（prevとのmaxではない）。 | 候補なしERANGE、未知VID ESTATE、map不在かつid=0 ENOTSUP |
| `N/nvkm/subdev/volt/base.c:104–183,233–239`、`bios/vmap.c:29–53,93–153` | vmap entryがあれば版10/20hの手本の符号付き64bit式（speedo/温度、mode別係数）→min/max clamp→link先のmap加算。entry不在時だけid*10000。nv40はspeedo_readが無く手本同様vmap計算はENOTSUP、定数倍へ逃がさない。表破損はEBIOS。 | 未対応版/必要speedoなしENOTSUP |

```c
struct gf5_vid_ops {
  int (*get)(void *, u8 *vid); int (*set)(void *, u8 vid); void *owner;
}; /* 上表のDCB mappingとGPIO列をvolt.cが実装、fanも共用。 */
int gf5_volt_get(struct gf5 *, u32 *requested_uv);
int gf5_volt_set(struct gf5 *, u32 requested_uv); /* 要求以上で最も近い表値 */
```
- 性能状態統括: 表/span/対応確認→RAM calc→drain_pause→RAM prog→unpause→fan上げ→電圧上げ→core/shader calc/prog→電圧下げ→fan下げ。手本`N/nvkm/subdev/clk/base.c:159–215,266–305`の順。PCI版Ra266にPCIe link切替は不要。手本の-ENODEV相当はENOTSUPとして機能不在を記録し、利用可能な操作を継続する。
- 差分: 中途失敗は後続の降圧/ファン下げを行わずFAULTへ渡し、最後に成功した設定を記録する。手本はcalc/prog失敗後も下げ経路へ進むが、OS32の偽MMIO失敗で部分書込後のクロックを確定できないため。RAM失敗は§3の復旧列。
- 本体は上記MIT列で実装し、暫定ENOTSUPのstubではB3/B4を完了しない。実ROMに対応GPIO/表が無い場合のENOTSUPは正常な機能不在として別に記録する。

## 6. タイリング
| 順/前提・手本 | OS32の列 | 誤り |
|---|---|---|
| `N/nvkm/subdev/fb/nv44.c:30–36 nv44_fb_tile_init`,`:60` | 12枠。addr=base\|1、limit=max(1,base+size)-1、pitchは要求値。F1予約/表示面/他tileと重ならないVRAM割当を入力とする。 | slot/範囲/加算overflow EINVAL、空き無しEBUSY |
| 同`:40–46 nv44_fb_tile_prog` | 100604+16*i=limit→100608+16*i=pitch→100600+16*i=addr→同readでflush。 | EIO |
| `N/nvkm/engine/gr/nv44.c:31–46,73 nv44_gr_tile` | FIFO pause→GR idle→400908+16*i=pitch→400904+16*i=limit→400900+16*i=addr→FIFO start。番地定義`gr/regs.h:204–206`。4ahに他chip用複製列は不要。 | drain失敗ETIME、EIO |

```c
struct gf5_tile { u32 base, bytes, pitch; };
int gf5_tile_set(struct gf5 *, unsigned slot, const struct gf5_tile *);
int gf5_tile_clear(struct gf5 *, unsigned slot); /* disable descriptorを両側へ */
```
- OS32統括列: drain/pause→PFB列→PGRAPH列→unpause。差分: 手本の二つの関数を単一の停止区間で包む（PFB/GRの不一致中に仕事を投入させないため）。
- clearはaddr/limit/pitch=0の無効descriptorを同順で両側へ書く。使用中枠の変更/解放は最後のfence後。単なるレジスタ更新で内容のlinear↔tiled変換は起きない。
- 入力契約: `N/nouveau_bo.c:178–188`よりbaseは65536B整列、pitch>0、bytes=roundup(要求bytes,64*pitch)をu64で計算し、その全領域を予約する（越境/overflow/未整列はEINVAL）。`nouveau_bo.c:108–135`どおり空き枠を昇順検索して最初を採用、未使用の古いdescriptorはclear、使用中枠は最後のfence後に再利用。pitch=0はclear入口だけ。
- `gr/nv44.c:98`のclass309eはF2 command.hで既に許可済みのswizzled surface objectでありPFB tileとは別。PFB tileは既定転送で検証し、層3に無いswizzled転送APIをB5の依存として追加しない。

## 7. 状態の保存と復元
| 手本の列 | OS32の列 | 誤り |
|---|---|---|
| `N/nouveau_drm.c:944–991 nouveau_do_suspend`、`dispnv04/disp.c:57–95 nv04_display_fini` | 投入/flip受付閉→F2 fence drain→表示の論理状態保存/blank・vblank抑止→使用中VRAMをF3 V0のCPU窓で動的RAMへ退避→fence世代保存→F3無効化→F2/F1停止。 | 退避領域不足ENOMEM、待ちETIME、F1 rc保存 |
| `N/nouveau_drm.c:1007–1028 nouveau_do_resume`、`dispnv04/disp.c:99–159 nv04_display_init` | BM先行→必要な既存devinit→F1/F2開始→F3 objects再生成（受付閉）→§5の順でRAM/clock/volt/fan復旧・therm再設定→tile復旧→VRAM内容戻し→表示mode/palette/cursor/DPMS再適用→IRQ要求復旧→受付開。 | 失敗はFAULT、画像/資源保持 |
| `nouveau_drm.c:1047–1076` | PCI D3/D0・OS電源断は行わない。F1のreset→IRQ解除→BM解除の停止順、再開のBM先行は変更しない。 | OS suspend入口は作らない |

```c
int gf5_state_save(struct gf5 *, struct gf5_image **out);
int gf5_state_restore(struct gf5 *, const struct gf5_image *);
void gf5_state_free(struct gf5 *, struct gf5_image *); /* 復旧/破棄後 */
/* image: version, clock/VID/thermal設定, tile[12], 表示論理state,
 * VRAM extent[]{off,bytes,host_copy}, fence世代。生MMIO全域dumpではない。 */
```
- RUN→SAVING→SAVED→RESTORING→RUN。pause/save中の二重要求はEBUSY、未保存restoreはESTATE。stop失敗はFAULTで元snapshotを残す。save前のメモリ確保失敗ならRUNを維持。
- 所有extentを全て退避: 表示front/back、cursor、層3面、呼出元が内容維持を要求した領域。PRAMIN/RAMHT/RAMFC/GR context/pushはF2の再構成対象として画像コピーと区別する。未完了コマンドを再送しない。
- F2 stopでmap/可変instmemが解放されるため、GPU内ポインタをsnapshotに残さない。restoreは同一VRAM offsetを再予約してから転送する。予約不能ならENOMEMで止める。
- 差分: nouveauのdisplay→evict→idleはTTMの依存追跡が有る。OS32のCPUコピーは先にfence完了が必要。fenceは旧世代を完了扱いにして新世代へ進め、旧ticketを再利用しない。
- 差分: nouveauのobject resume→VBIOS→display列をOS32のdevinit→F2再生成へ置換。既存devinitがエンジンを初期化し直すので先に行う。display adapterには両head・palette・cursor・DPMS・未完了flip整理を要求（層4設計へ渡す契約）。
- 試験は電源維持save/restoreと、退避後に試験用extentを破壊して復元する内容消失模擬まで。OS全体のS3、PCI電源状態遷移、実電源断復帰の完成とは呼ばない。

## 8. 大きさの見積もり
| 追加部品 | text/rodata予算KiB | 状態/一時領域 |
|---|---:|---|
| perf表/状態選択、clock、ram | 3 / 2 / 3 | 必要ROM/spanとdecode済みBIT M programはproductionがdevinit解放前に別所有し、F5停止・snapshot利用終了まで動的RAMで保持。plan約64B、restore用一時programとは別寿命 |
| therm（fan含む）、volt、tile、state | 3.25 / 2.25 / 1.5 / 3 | 常駐state合計1KiB以下、snapshot metadata約2KiB＋extent数依存 |
| 結線/余裕 | 0 | 層5増分計18KiB＋BSS/alignment最大2KiB=20KiB予算 |
| 診断 | 4〜6 | kdebug側だけ、KDEBUG=0のmoduleに文字列を残さない |

- 容量の起点は2026-11-04の層2後の実測とし、image/memを分離した[共通合算表](GF_FEAT3_DESIGN.md#合算予算)で判定する。F1/F2は再加算しない。
- F5本体20KiBにC1共有decoder/tables.incの0〜4KiBを別加算する。常駐ROM/span/programの動的RAMはC2報告で別記し、20KiBに含めない。
- 統合後にlink map/生成import実数とKDEBUG=0/1を計測する。F4 C3が設定するimage/mem160KiB・load_span208KiBと共通合算予算で判定し、圧縮を発注条件にしない。必要importは本数制限なしで生成元へ追加する。
- F5は既存gf_ioと型付き資源opsを使う。新しいimportが要るなら生成元 (geforce_imports.def) に足し、汎用dispatcher化しない。生成元を編集し生成ヘッダは手修正しない。
- VRAM退避はmodule BSS外のゲスト動的RAM。重複extentは統合しsum(bytes)を事前計算、metadata約2KiB＋extent配列を加えて一括確保。例:640×480×4Bの表示2面=2,457,600B、層3の同寸法2面=同量、cursor 64×64×4=16,384B、計4,931,584B（4.70MiB）。各面を64行へ切上げるtile例なら5,259,264B（5.02MiB）。
- PM決定: snapshotの固定上限/対RAM比率制限は置かない。必要総量の確保失敗時だけ停止前にENOMEM、部分確保を解放してRUNを維持。一部extentだけの成功はしない。scratch/ROM/画像の所有者を分離する。

## 9. ファイルごとの変更一覧と段（以下は実装予定、リポジトリ未編集）
| 段 | ファイル（O/相対、新規は＋） | 内容・概算行数 |
|---|---|---|
| A0 | ＋hwdev/gf/feature5.h | 上記型/adapter/rc、120行。F2停止口・F4保存型の凍結後。int unpause契約と前景poll呼出し箇所をPMレビューで凍結、F4 C3の前方宣言struct gf5_test_opsと整合 |
| A1 | ＋tools/tests/gf5_fake.h | MMIO/時間/ROM/extent/traceの試験骨格、160行。F5 A0統合後に発注 |
| A2 | ＋kdebug/gf5_test.c | serial入口の骨格、info/clock/ram/therm/fan/volt/tile/save/restore dispatch、100行。F5 A0統合後に発注し、F4 C3のfeature5_out slotを使用 |
| A3 | ＋tools/tests/test_gf5.py | CHECK登録・部品別host駆動・正常対照と変異、100行。未着手Bは未登録、追加済みBの欠落は失敗 |
| B0 | ＋hwdev/gf/perf.c | 表列挙/状態選択、250行＋tools/tests/gf5_perf_host.c |
| B1 | ＋hwdev/gf/clock.c | calc/read/prog、180行＋gf5_clock_host.c |
| B2 | ＋hwdev/gf/ramclock.c | self-refresh/blank/script、220行＋gf5_ramclock_host.c |
| B3 | ＋hwdev/gf/therm.c | 表/raw/換算/fan PWM・toggle/AUTO・閾値poll/設定保存、430行＋gf5_therm_host.c |
| B4 | ＋hwdev/gf/volt.c | 表/vmap・ID選択/VID・共用GPIO列、280行＋gf5_volt_host.c |
| B5 | ＋hwdev/gf/tile.c | 12枠/FB・GR同期、140行＋gf5_tile_host.c、確定整列契約と309e境界 |
| B6 | ＋hwdev/gf/state.c | extent退避/再構成/状態遷移、250行＋gf5_state_host.c |
| C0 | hwdev/gf/clock.c | 既存PLL limits/solverとのadapter（B1へ統合）、レジスタキー対応と同値回帰 |
| C1 | kdebug/gfinit_decode/tables.inc | BIT Mの既存未登録spanが有れば追加、runner decode試験を伴う |
| C2 | hwdev/gfのproduction所有者・F2 command境界・F4表示所有者 | links/save adapter/受付閉を結線（実装時の確定ファイルに1変更段ずつ割当） |
| C2-ROM | ＋hwdev/gf/feature5_rom.c | 必要ROM/span/programの取得・所有・解放。devinit解放前の取得結線は共有ファイルごとにC2で直列 |
| C2-service | ＋hwdev/gf/feature5_service.c | 前景fan poll adapter。PMがA0で呼出し箇所を凍結後、前景呼出し元の結線は別段 |
| C3 | build/geforce-module.mk・tools/prototypes/geforce_imports.def | 新部品link、image/mem160KiB・load_span208KiB検査、F4 C3後に直列 |
| C4 | kdebug/gf5_test.c | feature5_out slot経由でB試験入口を結線 (F5で版上げなし)、全機能serial台本、200行追加 |

- B各段は対応するホスト試験・変異を同時に完成させる。A骨格だけで合格にしない。A→B（依存順）→C→実機受入。B3/B4/B5の温度/電圧/整列列は本書で確定済み。

## 10. 受入 → 試験と変異
| 受入条件 | ホスト偽MMIO/変異（各B段） | Ra266シリアル |
|---|---|---|
| ROM値を版別に扱う | 長さ切詰め/符号/倍率/strideを独立fixtureで検証。変異: mem*2000→*1000、VID>>2除去 | ROM表位置/版/全entry/採用係数を出す |
| core切替順と結果 | golden trace、単段/2段、Ra266 v2x別shader=EBIOSかつ無書込（004008を補う変異を殺す）。変異:5ms削除、mux先行、mask誤り | 要求/実現値/前後PLL/mux、F2転送の結果比較 |
| RAM内容と表示が戻る | 2head/非活動head、vblank時刻、refresh×2、4a単partition、script失敗。変異:refresh削除、4038書込、SR1復旧削除 | 変更前後extent CRC、表示パターン、fence、所要時間 |
| 温度のraw/換算とfan制御 | OLD_STYLE setup/read、整数丸め/負値clamp/係数0、PWM line2/9・反転・toggle・上下cstate、3 mode/TRIP/LINEAR/OTHER・1秒/3%/hyst・emergency通知。変異:AUTOでperf直書き、閾値下降省略、15b8書込、-8除去、duty反転除去 | raw/係数/温度、fan方式・目標/readbackを記録、外部温度計なし |
| 電圧を実測値と呼ばない | 実GPIO列をfake MMIOで検証、vmap有無/必要speedo不在/prev失敗/min_id最大選択、要求間の最近上位entry/最大超過/同値・欠落bit・極性、RAM→fan↑→volt↑→clock→volt↓→fan↓。変異:下位entry採用、clock前降圧 | VID前後/要求・採用µV、ROM perf状態間とboot復帰 |
| tile両側が同期する | 12枠/再利用/境界/overlap/解放。変異:FB flush削除、GR addr先行、11→12 slot | linear基準とtiled転送の内容比較、隣接面CRC不変 |
| 状態が再使用できる | 8MiB超の確保成功/確保失敗ENOMEMかつ停止なし、save失敗/途中restore失敗/extent破壊/再割当/fence世代。変異:cursor退避削除、BM先行解除、古いticket再利用 | 描画→save→restore→再描画を3回、mode/cursor/palette/CRC/IRQ継続 |
| 予算・非GUI | KDEBUG両像、image/mem<=160KiB・load_span<=208KiB、GUI参照追加0 | 0:13.0/0221を記録、stage/rc/元rc付きのPASS/FAIL |

- 全実装後にPMが生成→まとめビルド→検査を実行し、check-changed MUTATE=0・各追加試験の一意anchor変異RED・lintを確認する。実機未実施は未実施と記録し、NP21/WでGPU実証したとしない。今回は設計のみでbuild/実機試験なし。
- `CHECK=dict(name="gf5-host",tier=1,needs=[],budget=90.0,covers=["hwdev/gf/**","kdebug/gf5_test.c","tools/tests/gf5*","tools/tests/test_gf5.py"])`をA3で宣言。Bごとのhost.cは実部品をlink、依存はA1 stub、Aにも骨格の正常/変異を置く。
- 各変異は無変更対照GREEN→一意anchorを1箇所変更→同じ試験が実行時RED→復元GREEN。compile失敗は検出成功に数えない。ゲスト入口は成功0/失敗1/実機未実施または機能不在77を返し、stage/rcをserialへ出す。77をPASSとして集計しない。
- 実装のD段で`O/docs/tasks/DEFERRED_TESTS.md`へ次表を登録する（今回はリポジトリ未編集）。

| 台帳ID / 状態 | 対象・解除条件 |
|---|---|
| GF-L5-Ra266 / 未実施 | 0:13.0/0221、ROM perf間＋boot復帰、RAM CRC、温度/fan/VID、tile、save/restore3回。担当/日付/serialログを実施時記入 |
| GF-L5-ENOTSUP / 暫定 | 未実装stubのENOTSUPは受入保留。B3/B4/B5の実列host GREENと実機結果で解除。ROM/GPIO不在は根拠付き機能不在へ分類し、stub完了に流用しない |

## 11. 未決（ユーザー0件／PM1件）
- PM決定（7）: F5前景fanの具体的呼出し箇所はPMがF5 A0宣言レビューで選定し、凍結後にC2を発注する。
- PM決定: 実機のクロック/電圧試験はROM perf状態間の切替と起動値復帰まで。ROM外は範囲外。
- PM決定: 温度は手本の式を採用し、外部温度計は使わない。

反論: 0件。

- 反論 G2（実ROM確認のみ）: 指定資料内にRa266のBIT P/vmap実データが無いため有無は断定できない。変換規則は修正済み、有無の確認は実装後のinfo採取に残す。

## 確定 (2026-11-02、Opus 第 2 回「G1 を r3 で足せば PM の確認で確定可」、PM が r3 を確認)
- G1: ファンの AUTO/MANUAL/NONE・TRIP/LINEAR/OTHER・1 秒ごとの見直し・3% 刻み・温度しきい値の polling を手本の列で足したことを PM が確認 (§4 の 104〜122 行)。shutdown は OS32 に電源断が無いので「記録 + ファン 100%」。
- G2: vmap があれば手本の式、無ければ id*10000。Ra266 の ROM の vmap の有無は手元の資料で分からないので、実装の後に実機の info で採る (astra の反論を採用)。
- G3: snapshot の上限は置かない (PM 決定、マジックナンバーを作らない)。確保に失敗したら ENOMEM。
- 未決 2 件は PM が決めた: 実機のクロック/電圧の試験は ROM の perf 状態どうしの切り替えと起動時の値への復帰まで。温度は手本の式に従い、外部温度計は使わない。

## 追記 (2026-11-04、段に出す前の点検)

| 境目／持ち主 | 宣言段で凍結する追記 |
|---|---|
| ROM／A0・B0・B2・C1・C2 | `service.c:29`→`devinit.c:155–163`でROM/programは解放される。gf5の表viewをscratchへ残さない。production所有の必要ROM/spanとdecode済みBIT M programを動的確保し、devinit解放前に取得、F5停止・snapshot利用終了まで保持する。restoreのdevinit用一時programとは別寿命。新`hwdev/gf/feature5_rom.c`をC2から独立した段に割り当てる。 |
| 時計／A0・B3 | F2/F4はsys_time_nowの下位u32 µsで絶対期限、差はi32比較。F5 fan期限は同じsys_time_nowのhi/loからu64 µsを構成し、u32を単純拡張しない。5ms等は既存短waitを刻んで待つ。時計失敗は元rcを保存しEIO。 |
| 前景fan／C2を分割 | `geforce_tick.c:14–16`・`foundation.c:106`はIRQ/tickのF1 polling口で、fanの前景入口ではない。新`hwdev/gf/feature5_service.c`がgf5_fan_tickを所有し、既存前景の協調地点・診断待機から型付きpollを呼ぶ結線を別段にする。**PM 決定（7）**: PMがF5 A0の宣言レビューで具体的呼出し箇所を決め、凍結するまでC2は出さない。IRQからfan処理・alloc・待ちを実行しない。 |
| RAM/tile停止／A0・B2・B5 | drain_pauseは受付閉→dlでfence完了→F2独自2秒のFIFO pause→GR idle。unpauseは復帰失敗を返すint型としてF5 A0で凍結する。RAM区間だけCPU窓/present/描画/flipの受付を閉じる。clock単体にpauseを追加しない。現行referenceはMMIO読出し（fence.c:57）、IRQにはVRAM fence読出しが無いので存在しない処理への抑止hookは作らない。 |
| save/restore／B6・C2 | snapshot所有者は呼出元、gf5は処理中借用し失敗時もfreeしない。停止前に全extent・metadataを確保。saveは受付閉→drain→表示保存/blank→CPU窓で退避→F3無効化→F2/F1停止。restoreはBM先行→必要devinit→F1/F2→F3 objects→性能/thermal/tile→同一offへ画像→F4論理状態→IRQ→受付公開。途中失敗はFAULT、未完了ticketは再送しない。 |
| tileと309e／B5 | N/nvkm/subdev/fb/nv44.c:30–46とengine/gr/nv44.c:31–46のPFB→PGRAPH列を一停止区間で実施。309eは既にF2許可class（command.h）、PFB tileとは独立。層3設計に無いswizzled転送APIをB5の依存として増設しない。 |
| 配置・試験環境 | B0 perf/B1 clock/B2 ramclock/B3 therm/B4 volt/B5 tile/B6 state・ROM/前景adapterはゲストgeforce.mod＋各libcなしhost試験。C0はB1の直列追記、C1 decoderは共有ゲスト部品＋既存host試験。A2/C4はゲストkdebug、A1はhost土台、A3はhost Python登録。新F5部品をhwdev.modに混入させない。 |
| 予算 | mod text/rodata18＋BSS/alignment2＝20KiBに、C1共有decoder/tables.incの0〜4KiBを別加算（geforce-module.mk:18）。snapshot画像は動的RAM（例4.70MiB、tile例5.02MiB）でmod BSS外、固定上限は追加しない。常駐ROM/span・decode済みprogramの動的RAM量はC2報告で別記し、20KiBに含むと偽らない。 |

共通の[合算予算](GF_FEAT3_DESIGN.md#合算予算)・[共有ファイルの衝突表と所有段](GF_FEAT3_DESIGN.md#共有ファイルの衝突表所有段)・[依存と並列発注](GF_FEAT3_DESIGN.md#依存と並列発注)はGF_FEAT3を正とする。

F5 A0はF2停止口とF4保存型の凍結後、A1/A2はF5 A0統合後に発注する。B0〜B6は異なるファイルならF3/F4 B群と並列にコーディングできる。B3はB4所有のGPIO宣言を使う。B2/B5の停止結合、B6の全extent/再生成結合、C2はF3を待ち、state/display結線はF4 C2/C4も待つ。C0はB1の同一所有者による直列追記、C1はF4 A0後。試験コードは各Bで完成させ、実行は全実装後のPMの生成→まとめビルド→検査にまとめる。

## 追記 (2026-11-04、層 4・5 の先回りの点検)

- 層 3 への依存・層 5 の宣言の段の中身・前景の呼び出し箇所 (PM 決定) は [GF_FEAT4_DESIGN](GF_FEAT4_DESIGN.md) の末尾の「追記 (2026-11-04、層 4・5 の先回りの点検 …)」。

## 追記 (2026-11-04、層 4 の実物との照合 D1〜D4 と宣言の段 A0 の凍結)

本追記r2が競合する旧記述に優先する。ここでのD1〜D4は層4実物との再照合の番号で、本文§1の同名番号とは別。O=`/home/hight/os32-v3/`、D=`O/hwdev/gf/display/`、K=`O/drivers/video/geforce/`、F4=`O/docs/decisions/realhw/GF_FEAT4_DESIGN.md`。Nは本文§1の指定MIT取得物。以下の新署名・結線は実装済みという意味ではない。

### D1〜D4の訂正と従うカード
- **D1** 旧「`hwdev/gf/service.c:29`起点でdevinit解放前にROMを取得」→ 正：F4通常startは`D/service.c:191,212,216`の`svc_rom`でacquire→run→release、restartの一時取得は`D/recover.c:286`、解放実体は`O/hwdev/gf/devinit.c:164` → **A0、C2-ROM、C2-display-service、B0/B2/B6**。初回svc_romでgf_display_baseline成功後・release前にC2-display-serviceがBIT M専用programをprepareし、完成品とROM全体をF5へ複製する。元main programは流用しない。F5側失敗は部分確保を解放し、元scratchのreleaseも実施する。
- **D2** 旧「decode済みconst M programを既存単一script口に渡して実行」→ 正：`O/hwdev/gf/runner.c:202,214`はwork.count=0からBIT I参照で再decodeし即実行、`O/hwdev/gf/internal.h:117`のconst口はmain用run_oneのみ → **A0-init、C1-runner、C2-ROM、B2**。下記prepare/executeへ既存処理を共有分離し、RAM切替前の検証と切替後の実行を分ける。別interpreter・main全体の再実行は作らない。
- **D3** 旧「video境界は既存0/-1」→ 正：`O/hwdev/gf/service.c:376`と`D/quiesce.c:89`は0/下位負rcを返す → **A0、C2-links、C2-backend、B6、C4**。既存videoの負rcを一律-1に潰さない。F5公開操作の出口は共通gf5_rc_mapで分類しlast_rcに、変換前の負rcをsource_rcに保持する。stop/restart adapter自体は下位負rcを保持し、OFFでも負rcを成功に丸めない。
- **D4** 旧「owner初期化前のbindは必ずEINVAL」→ 正：`D/service.c:49`のgetter自身がNULL ownerを常駐ctxへ補い、`O/hwdev/gf/service.c:70`のbindは受入れ得る → **C2-body、C2-service、A1/C4結合試験**。`K/geforce_body.c:51`のgf3_service_bind→gf4_service_init→gf3_service_owner_bindを維持する理由は初期化済みctxへの結線であり、逆順EINVALを受入条件にしない。

### GF_FEAT4_DESIGNへの訂正文（F4:729の括弧内を置換、反映済み）
「owner表のgetterはowner未設定なら常駐ctxを補うため、逆順bindが必ずEINVALになるわけではない。gf3_service_bind→gf4_service_init→gf3_service_owner_bindの順は、初期化済みctx・links・execへの結線のため維持する。」linksは非const、execはconstの既存getterを使い、exec.owner→links.owner→ctxの所有を別コピーへ置換しない（`D/api.h:141`、`D/links.c:316`）。

### BIT Mのprepare/execute境界（A0-initで宣言、C1-runnerで実装）
```c
/* O/hwdev/gf/internal.h：既存gf_init_context/programとdispatchを共有。 */
i32 gf_init_script_prepare(struct gf_init_context *ctx,
    struct gf_init_program *out, u32 pc, u8 head, u8 or_mask, u32 dl);
i32 gf_init_script_execute(struct gf_init_context *ctx,
    const struct gf_init_program *program, u32 pc, u8 head, u8 or_mask, u32 dl);
/* 既存gf_init_run_script(ctx,work,pc,head,or_mask,dl)は上の合成にする。 */
/* O/hwdev/gf/feature5.h：memory_pcとcallbackをu16からu32へ置換。 */
struct gf5_rom_view { const u8 *bytes; u32 size;
    const struct gf_init_program *memory_program; u32 memory_pc; };
i32 gf5_rom_acquire(struct gf5 *, const u8 *, u32, const struct gf_init_program *); /* 完成M program、M不在ならNULL */
void gf5_rom_release(struct gf5 *);
i32 gf5_link_memory_script(void *owner, u32 pc);
```
- BIT MのLE16 pcは取得時にu32へ拡張。M実行はhead=0、raw OR mask=0（表示routeではない）。prepareはBIT I参照、SUB/SUB_DIRECT先、REPEAT対応、dispatch対応を切替前に確認し、GPU書込みなし。既存decode拒否条件を維持し、REPEAT=255を実行予算試験に使わない。outは構築中mutable、成功後だけconstとして公開、失敗時は未公開のまま破棄する。
- C2-display-serviceがl->initのio・started_usとc->deadlineを使い、gf_display_baseline成功後・owner->release前にprepareする。一時M programは同段で動的確保・破棄し、main programは渡さない。gf5_rom_acquireはROM全体（rom_size bytes）と完成M programの複製だけを行う。memory_pcは同じROMのBIT MのLE16をu32化し、オフセットを維持する。PLL/B0/B3/B4もこのbytes/sizeを参照し、span表を追加・詰め直ししない。
- ROM/M programはproduction所有の動的領域で、成功時だけviewを公開する。通常停止ではgf5_rom_releaseを呼ばない（svc_romは!initialized時だけ）。解放はF5停止・最後のsnapshot破棄とcleanup終了の後かproduction破棄時だけ。start→leave→startでも保持し、restoreのdevinit一時scratchやTMDS再借用とは別寿命にする。
- **PM 決定（8）**：acquire/prepare失敗ではF5を無効のまま、F4は続ける。rom viewはNULL（bytes/program=NULL、size/pc=0）、ROMが要るF5操作はGF5_ENOTSUP。失敗の分類値/元値をlast_rc/source_rcに残し、F4成功で消さない。既存F4自身の失敗は従来どおり返す。
- **PM 決定（12）**：restart（D/recover.c:286）で初めてdevinitが通った場合、F5は無効のまま。取得窓を増やさない。
- 実行用mutable `gf_init_context`は操作中だけ借用し、ROMはF5所有view、ioは既存legacyの型付きioを使う。ctxにprogramを書き戻さず、decode済みprogramも変更しない。RAM切替前に必要scratchを確保し、切替後にalloc/decodeしない。saved_count!=0を持ち込まず、pc/head/OR/ROMはprepare時と同じ組で実行する。
- prepare/executeとも0/-1/-5。prepareの-1はGF5_EBIOS、executeの-1はGF5_EIO、-5はGF5_ETIME、0は成功；元値をsource_rcに保存する。時計/I/O取得失敗はadapterでGF5_EIO、確保失敗はGF5_ENOMEMとして区別する。M不在はmemory_program=NULL・pc=0で実行を省き、機能追加や再試行にしない。
- prepare入口はsteps/wait_us/elapsed_us=0、last_rc=GF_SCRIPT_EIO、out->count=0に初期化し、検証用pc/head/or_mask/deadline/display_script/fail_pcも設定する。execute入口はpc/head/or_mask/deadline、calls/repeats=0、skip=false、display_script=true、fail_pcを毎回設定し、予算はリセットしない。後日の単独executeでは外側F5操作がsteps/wait_us/elapsed_us=0・last_rc=GF_SCRIPT_EIOを設定する。
- started_usは初回prepareではl->initの取得時値、後日のM実行では外側F5操作の開始時計。同一操作のprepare→executeでは開始時計・steps/wait/elapsed予算を継続し、外側dlは入れ子で更新しない。既存run_scriptの取得時started_us・0/-1/-5・raw head/OR・TMDS公開署名を維持する。
- prepare失敗／execute失敗は当該runnerがio.restoreを1回実施し元rcを保持する（入口引数不正で未開始なら既存どおりrestoreなし）。prepare成功は書込みなしでrestore不要、execute成功後はadapterがindex/lock等のcleanupを1回行う。合成wrapperは失敗restoreを重ねない。B2の既決self-refresh解除・旧PLL復旧1回はRAM列の責任で、scriptを再実行しない。cleanup失敗も閉鎖/FAULTへ伝え、元の失敗を消さない。

### F2/F4 adapterと保存・公開（A0宣言、C2-links実装）
```c
/* feature5.h：links.ownerは常駐struct gf5の借用。 */
i32 gf5_rc_map(i32 lower);
struct gf5_links *gf5_links_table(void);
i32 gf5_link_drain_pause(void *owner); /* gf5.deadlineを使用 */
i32 gf5_link_unpause(void *owner);
i32 gf5_link_display_save(void *owner, struct gf5_image *image);
i32 gf5_link_display_restore(void *owner, const struct gf5_image *image);
i32 gf5_link_stop(void *owner, u32 dl);
i32 gf5_link_restart(void *owner, u32 dl);
i32 gf5_link_admission(void *owner, bool open);
i32 gf5_link_drain(void *owner, u32 dl);
i32 gf5_link_copy_extent(void *owner, const struct gf_vram_reservation *key,
                        u8 *ram, bool to_vram);
i32 gf5_link_alloc(void *owner, u32 bytes, void **out);
void gf5_link_free(void *owner, void *allocation);
i32 gf5_link_clock64(void *owner, u64 *us);
```
- gf5_links実体とgf5_links_tableはC2-linksだけが所有する。C2-serviceはinitで返された表のowner=gを設定し、g->linksへ同じ型付き結線を設定する。
- gf5_linksの同名欄（gf5_link_を除く）は上記関数ポインタ型とmemory_scriptを採用。NULL ownerはGF5_EINVAL。stop/restartは`gf4_service_suspend/restart(g->display,dl)`、display_save/restore は `gf4_display_save/restore(g->display,&image->display,g->deadline)` へ委譲し下位 rc を保持する。起動・再開の期限境界は GF_FEAT4_DESIGN X4 に従い、links.restart 成功後の性能復元、links.display_restore 入口でそれぞれ now+GF2_TIMEOUT_US を作り g->deadline と c->deadline を揃える。段内では絶対期限を延長せず、期限生成の時計失敗は GF2_EIO。診断ops.display_saveを使わない（`D/save.c:131,338`）。
- B0〜B6の公開出口はC2-links所有のgf5_rc_mapへ集約する。GF1〜GF4とGF_SCRIPTの全定数を下表の1つのswitchで列挙し、範囲推測しない。同値のOKはcase 0を共有、GF5全列挙値はそのまま返す。source_rcは変換前の元値。prepareのGF_SCRIPT_EIOだけ呼出し段でGF5_EBIOSとし共通mapへ渡す（source_rc=-1を維持）。
| 下位rc（同じ行は同じcase結果） | GF5への変換 |
|---|---|
| GF1_OK / GF2_OK / GF3_OK / GF4_OK / GF_SCRIPT_OK | GF5_OK |
| GF1_EINVAL / GF2_EINVAL / GF3_EINVAL / GF4_EINVAL | GF5_EINVAL |
| GF1_ENOSPC / GF2_ENOSPC / GF4_ENOSPC | GF5_ENOMEM |
| GF3_ERANGE / GF4_ERANGE | GF5_ERANGE |
| GF2_ESTATE / GF3_ESTATE / GF4_ESTATE | GF5_ESTATE |
| GF3_EUNSUPPORTED / GF4_EUNSUPPORTED / GF4_ENODEV | GF5_ENOTSUP |
| GF4_EBUSY | GF5_EBUSY |
| GF2_ETIME / GF4_ETIME / GF_SCRIPT_ETIME | GF5_ETIME |
| GF1_EPCI / GF1_EMAP / GF1_EIRQ / GF1_ESTOP / GF1_EIO / GF2_EMAP / GF2_EIO / GF2_EFAULT / GF2_EEXIST / GF2_ECLASS / GF2_ESTOP / GF3_EFORMAT / GF4_EFORMAT / GF4_EPLL / GF4_EDDC / GF4_EEDID / GF_SCRIPT_EIO / 未知値 | GF5_EIO |
- drain_pauseはadmission(false)→gf2_fence_emit/wait（元dl）→gf2_fifo_pause（既存独自2秒）→GR idle。GR idleはC2-links内の非公開helperが既存ioで00400700を読み、NV40のSYNC_STALL=00004000を除いたbitsの消滅を待つ（`N/nvkm/engine/gr/nv04.c:1211`、`gr/regs.h:160`）。手本の2秒と外側dlで打切り、架空の公開idle APIや追加lock/監視を作らない。既存F2の内側待ちが外側dl内に必ず収まるとは主張しない。
- unpauseはold_rc退避→ch->last_rc=0→void gf2_fifo_resume→last_rc/state/fault照合。新失敗は保持して負rc、無ければold_rc復元・戻り0。失敗時はF5 FAULT・受付閉で、F2が必ずFAULT化するとは仮定しない（`O/hwdev/gf/fifo.c:207,232`）。clock/volt/fan単独には停止を広げない。
- B6は全生存予約をgf_vram_nextで列挙し1予約1extent、node/owner/ID/generation/offを保持、restoreはmatchして再reserveしない。全確保後・破壊前にcallerへimageを渡しgf5は借用、失敗でも勝手にfreeしない。tripは深いコピー、image.displayはgf4_saved v2。metadata/trip/extent/画像はsizeofとu64合計で計上、固定snapshot上限なし（`D/api.h:13,21,33`、F4:376）。
- saveは直接F4 saveの受付閉→fence→views_close→hide/blank/IRQ抑止後に退避窓を借りる。copy_extentはboot表示mapとV0の境界で分割し、書込み排出→unmap成功後にsuspendへ進む。失敗時は窓/予約/像を保持、F2 OFF確認前にinvalidateしない。OFF＋負rcもB6成功に読み替えない（`D/save.c:119,138,149`、`D/recover.c:204,211`）。
- restoreは予約保持restart→性能/thermal/tile→画像→F4 restore/IRQ→全成功後だけsaved_closed解除→admission(true)。低水準restoreは非公開でholdを整理し旧frontへ戻す（`D/save.c:257,321,335`）。F2 startのepoch更新を二重加算せず旧ticketを再送しない。scan hookはhead0・形式別、INDEX8 hookを16/32bitへ無条件bindしない。

### A0で凍結する型・関数とファイル別の段
表のパスはO相対。A0群は宣言のみ、実装カードは1段1ファイル（C1-runner-testは既存fixtureと登録台本の対を更新）。同じ機能の本体を複数ファイルへ重複実装せず、共有ファイルへの結線は別段として直列化する。

| 宣言／実装段 | ファイル | 凍結する型・関数・責任 |
|---|---|---|
| A0 | hwdev/gf/feature5.h | F4:356〜370のrc/clock/plan/perf_entry/rom_view/tile/fan_trip/fan_saved/thermal/fan_mode/emergency_ops/vid_ops/extent/image/phase/gf5/links/test_opsの全欄。本追記のu32 pcを優先、int→i32・slot→u32。全公開口・adapter・ROM/service・gf5_rc_map・gf5_links_tableの宣言を集約。 |
| A0-init | hwdev/gf/internal.h | 上記prepare/executeの宣言、既存context/program/dispatch共有。 |
| C1-runner | hwdev/gf/runner.c | prepare/executeの共有分離と既存run_script合成実装。 |
| A0-production | hwdev/gf/production.h | `struct gf5 feature5`をgf_productionへ埋込み、feature5.hをinclude。ch/accel/displayは既存実体を借りる。 |
| A0-module | drivers/video/geforce/geforce_module.h | `const struct gf5_test_ops *geforce_feature5_ops(void);`と`void geforce_backend_feature5_bind(struct gf5 *); void gf_hook_feature5_bind(struct gf5 *);`を宣言（gf5は前方宣言）。Context版5・予約済feature5_outを使い版上げなし。 |
| B0 | hwdev/gf/perf.c | gf5_perf_count/get/select、gf5_perf_entry、表列挙と性能統括。 |
| B1 → C0（同一所有者の直列追記） | hwdev/gf/clock.c | gf5_clock_read/calc/prog、既存PLL parser/solver adapter。 |
| B2 | hwdev/gf/ramclock.c | gf5_ram_prog、上記M契約・既決RAM復旧列。 |
| B3 | hwdev/gf/therm.c | gf5_therm_read、gf5_fan_mode_set/get/cstate/tick、fan_saved/trip。 |
| B4 | hwdev/gf/volt.c | gf5_volt_get/set/id、gf5_gpio_find/get/set（B3も借用）。 |
| B5 | hwdev/gf/tile.c | gf5_tile_find/set/clear、reservation_id/generation付きtile、12枠。 |
| B6 | hwdev/gf/state.c | gf5_state_save/restore/free、image/extent、phase遷移・最終公開判断。 |
| C2-ROM | hwdev/gf/feature5_rom.c | gf5_rom_acquire/release、ROM全体と完成M programの複製・動的所有、成功時公開。 |
| C2-links | hwdev/gf/feature5_links.c | 上記全gf5_link_*、gf5_rc_map、gf5_links_table/実体、非公開GR idle。 |
| C2-service | hwdev/gf/feature5_service.c | `i32 gf5_service_init(struct gf_production *,struct gf4_ctx *,const struct gf5_test_ops **slot);`、`i32 gf5_service_poll(struct gf5 *);`、第2引数はbind済みproduction.display.display4。test_ops実体/phase検査/slot管理、links表のowner設定。 |
| C2-display-service | hwdev/gf/display/service.c | baseline後・release前のM prepare→acquire、部分確保解放、F5失敗時もF4継続。通常停止はROM保持。 |
| C2-body | drivers/video/geforce/geforce_body.c | 既存F3→F4→owner順の後にgf5_service_init、初回video start前に所有/slot結線と両feature5_bind(&production.feature5)を呼ぶ。 |
| C2-module | drivers/video/geforce/geforce_module.c | geforce_feature5_opsはregistered時のfeature5_slotを毎回読む。unload前NULL。 |
| C2-backend | drivers/video/geforce/backend_geforce.c | geforce_backend_feature5_bindの借用先保持、present・set_mode/dpms/cursor_image/move/show/vsync/page_flip/gammaの前景poll。 |
| C2-hook | drivers/video/geforce/gf_hook.c | gf_hook_feature5_bindの借用先保持、geforce_fill_rect/geforce_blitの前景poll。 |
| A2 → C4（直列） | kdebug/gf5_test.c | feature5 accessor毎回再読、全公開操作dispatch・診断待ちpoll。 |
| A1 | tools/tests/gf5_fake.h | F4実saved/slot/予約と結合するfake型・event。UNIT=0..6はB、7=stubなしを維持、8=ROM（ALLOC/FREE）、9=links（F2/F4・map・clock依存）。F4共有はUNIT14、自己実装stubは禁止。 |
| A3 | tools/tests/test_gf5.py | CHECK・B各hostとC2-ROM/C2-links host登録、C1-runner-testの既存台本を検査対象へ登録。未着手Bを成功扱いしない。 |
| B0-test | tools/tests/gf5_perf_host.c | B0本体の正常対照・変異。 |
| B1-test | tools/tests/gf5_clock_host.c | B1本体の正常対照・変異。 |
| B2-test | tools/tests/gf5_ramclock_host.c | B2本体の正常対照・変異。 |
| B3-test | tools/tests/gf5_therm_host.c | B3本体の正常対照・変異。 |
| B4-test | tools/tests/gf5_volt_host.c | B4本体の正常対照・変異。 |
| B5-test | tools/tests/gf5_tile_host.c | B5本体の正常対照・変異。 |
| B6-test | tools/tests/gf5_state_host.c | B6本体の正常対照・変異。 |
| C1-runner-test | tools/tests/gf_runner_trace_fixture.h・tools/tests/test_gf_runner.py | 既存試験へprepare無書込み、const execute、予算継続、単独execute初期化、失敗restore一回と旧wrapper互換を追記。 |
| C2-ROM-test | tools/tests/gf5_rom_host.c | UNIT8、全ROMのoffset保持・完成M複製・元scratch解放後の寿命・部分確保失敗。UNIT7の実結合で通常停止後保持とPM決定8/12も確認。 |
| C2-links-test | tools/tests/gf5_links_host.c | UNIT9、全rc変換・混在GF2_ETIME/ESTOP・OFF負rc・owner/結線・pause/resume。UNIT7＋F4 UNIT14で実物結合。 |
| C3-link | build/geforce-module.mk | F5本体と共有decoderをgeforce.modへlink。 |
| C3-import | tools/prototypes/geforce_imports.def | 必要importの生成元だけ編集、生成header手編集なし。 |

- **PM 決定（10）**：include/types.hには足さない。feature5.hに`typedef unsigned long long u64; typedef long long i64;`を置く。既存modes.c・gf4_display_fake.hと同形で、gnu11での同一typedef再宣言を用いる。
- 前景pollは`gf5_service_poll(g)`がclock64→gf5_fan_tick、busy/非RUNなら無処理。clock64はsys_time_nowのhi/loを合成する。上表のHAL関数の操作前と診断待ちの協調点で呼ぶ。int入口は負rcを伝播、void presentは記録して既存geforce_fail経路、診断はstage/last_rc/source_rcを報告する。module内HALは両bindで渡されたproductionの同一gf5を借用（static productionを直接参照しない）、kernel診断はaccessor経由。IRQ/geforce_tick/idle hookには結線せず、呼出しが無い間AUTO停止を維持する（`K/backend_geforce.c:445,491`、`K/gf_hook.c:67,81`）。
- gf5_test_opsはF4:369の全公開関数＋perf_count/get・tile_find・service_pollを同名欄の`__typeof__(&gf5_関数名)`で宣言し`i32 (*last_rc)(struct gf5 *)`を足す。SAVING/RESTORINGはslot=NULL、SAVEDはrestore/free/last_rcのみ、FAULTでも資源保持中は管理口を保持。freeは借用を外して画像/trip/metadataを解放し、FAULTを理由とする新しい拒否条件を作らない。F3/F4描画slotは最終RUNまで閉鎖する。
- 予算：GEFORCEはimage/mem各160KiB・load_span208KiB、HWDEVは各256KiB・320KiB（`O/tools/module_schema.py:176,184`）。F5 20KiB＋共有decoder 0〜4KiB、ROM/program・snapshot動的RAMは別計上。実装後のKDEBUG両像/import数/sizeof、全extent/V0・BIT M結合、実ROM表と実機挙動は未検証。ownerless F3 fixtureや合成ROMをその証明にしない。
- 設計の未決：0件。本追記は静的照合に基づく本文案で、ビルド・試験・実機受入の成功を主張しない。

## r1→r2
1: 通常停止はROMを保持し、F5停止・最後のsnapshot破棄後またはproduction破棄時に解放。
2: baseline後にC2-display-serviceがMをprepare、acquireは全ROMと完成Mの複製に限定。
3: C1-tablesを削除、BIT I経由の既存script decoderで検証。
4: backend/hookの型付きbindをA0-moduleに宣言し、C2-bodyから結線。
5: 全下位rcとGF5保持をgf5_rc_mapの単一switchへ集約、ENOSPC→ENOMEMも明記。
6: links実体/tableはC2-links所有、C2-serviceがownerを設定。
7: prepareの予算/work初期化とexecuteの実行欄初期化、同一操作の予算継続を明記。
8: PM 決定：F5取得/prepare失敗でもF4継続、NULL view・ENOTSUP・失敗記録。
9: runner/ROM/linksの試験段、A1 UNIT8/9とA3登録を追加。
10: PM 決定：u64/i64はfeature5.hに置き、include/types.hには追加しない。
11: gf5_service_initの第2引数をproduction.display.display4と明記。
12: PM 決定：restartで初devinit成功の場合もF5無効を維持、取得窓追加なし。

### 確定 (2026-11-04、Opus 第 2 回「確定可」、層 5 の機能 PM が反映)
- 第 1 回の 12 件 (P1×5・P2×4・P3×3) は r2 で直った。gf5_rc_map の表の定数名 (foundation.h・command.h・accel.h・display/data.h・internal.h) と prepare/execute の欄名 (internal.h・runner.c) は実物と一致。
- P3: C1-runner-test の段は tools/tests/gf_runner_host.c も持つ (試験の本体)。A3 は test_gf_runner.py を編集しない (持ち主は C1-runner-test の段だけ)。
- PM 決定 (層 5 の機能 PM): (8) ROM の取得・prepare の失敗は F5 だけ無効で F4 は続ける。(10) u64/i64 は feature5.h に置き include/types.h に足さない。(12) restart で初めて devinit が通った場合も F5 は無効のまま。(13) 宣言の段 A0 は試験の土台 A1 (tools/tests/gf5_fake.h) を含めて 1 段で凍結する (PORT_FLOW §3「宣言の段に入れる範囲」(a))。(14) 共有の登録ファイル (build/geforce-module.mk・tools/prototypes/geforce_imports.def) と既存ファイルへの結線 (display/service.c・geforce_body.c・geforce_module.c・backend_geforce.c・gf_hook.c) は 1 本の結線の段 (wire) が持つ。

## 追記 (2026-11-04、層 5 第 1 波の欠け — 層 5 の機能 PM の決定)
- **PM 決定 16** (C2-service・kd): feature5_service.c と kdebug/gf5_test.c は層 5 では host 試験の段を持たない (§9 の段表と末尾の追記の段表にも試験の段が無い)。gf5_fake.h の UNIT は B0〜B6・ROM・links の試験のためのもの。結合はまとめビルドと統合の検査、ゲストの診断は実機・エミュレータの受入 (上の PM)。DEFERRED_TESTS に未確認として残す。
- **PM 決定 17** (B5): tile の範囲の検査は (1) match した予約の中に、64*pitch へ切り上げた全範囲が収まる、(2) ほかの生存 tile と重ならない、の 2 つ。予約どうしは台帳が重ならないことを保証するので、表示・cursor・ほかの F3 面が別の予約なら (1) で足りる。同じ予約の中の部分の配置は予約の持ち主 (tile を要求した呼出元) の責任で、B5 は照合しない (手本も tile 領域は bo 単位、N/nouveau_bo.c:178–188)。GF_FEAT4 の A0 完全共有境界・A1 の「他面侵入」は「予約の外への侵入」と読む。宣言は変えない。
- **PM 決定 18** (B6→B3): B3 (therm.c) が `i32 gf5_therm_resume(struct gf5 *, const struct gf5_fan_saved *saved);` を持つ (feature5.h に宣言、A0x)。手本 N/nvkm/subdev/therm/base.c:376–394 nvkm_therm_init の順: sensor の setup (§4 の OLD_STYLE の init) → 保存 mode が MANUAL/AUTO なら保存 duty で fan を設定 → 保存 mode を設定 → sensor の poll と fan の期限を clock64 の現在時刻から再予約。saved のスカラーの欄を g->fan へ写し、trip は g->fan が既に持つ配列へ値を写す (ROM は restore の前後で同じなので ntrip が違えば GF5_ESTATE、配列を差し替えない・確保しない)。戻りは gf5_rc、失敗は last_rc/source_rc。B6 は §7 の「性能/thermal/tile 復旧」の thermal でこれを呼ぶ。UNIT6 に stub。
- **PM 決定 19** (C1-runner-test): C1 の試験は既存の tools/tests/gf_runner_host.c・gf_runner_trace_fixture.h の流儀 (その fixture の中の io の point/restore の callback) を延ばす。gf5_fake.h は使わない (「局所の stub を作らない」は gf5_fake.h を使う新しい試験の規則)。
- **PM 決定 20** (C2-links-test): gf5_fake.h (A0x) が UNIT9 に `gf_init_script_execute` の stub (呼出しの記録・失敗注入) と、gf_init_context の io の point/restore の callback (回数・失敗注入・observe) を組む helper を足す。links は memory_script の adapter の execute と成功後の cleanup 1 回、失敗時に restore を重ねないことをこれで確かめる。
- **PM 決定 21** (B0・B2): RAM 区間の停止 (drain_pause → ram の列 → unpause → admission(true)) は B2 の `gf5_ram_prog` が自分の中で持つ (§2 の宣言の注記「この呼出しだけ drain_pause/unpause で囲む」、test_ops から直接呼んでも停止が外れない)。B0 の `gf5_perf_select` は RAM の calc の後に `gf5_ram_prog` を呼ぶだけで、自分では pause/unpause/admission を呼ばない (二重の停止を作らない)。
- **PM 決定 22** (全段): 待ちの口が 1000µs (GF_INIT_WAIT_CHUNK_US) ずつに刻んで待つ。呼び元は長さをそのまま渡す。1000µs は devinit の実行器が中止と時計を確認する間隔で、ハードウェアの制約でも要求長の上限でもない。下位の負 rc は source_rc に元の値のまま、公開の戻り値だけを gf5_rc_map で分類する。ROM view が NULL は GF5_ENOTSUP、表の破損は GF5_EBIOS。
