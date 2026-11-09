> 出所: ~/os32-tmp/run/console-dual/DESIGN.final.md (2026-11-09、Claude 起草・codex astra の 3 回のレビュー)。
> 状態: **現行 (2026-11-09)** — CONSOLE_DUAL の確定設計。段の状態は票 TASK_CONSOLE_DUAL が持つ。

# CONSOLE_DUAL — 98 の TVRAM と GF の head に同時に出す 詳細設計 final (2026-11-09、r3 + 第 3 回レビューの PM 決定)

前提: シングルタスク・協調切替、9801 互換は見ない、整数演算、[C1]〜[D3]・[HW1]、1 機能 1 ファイル、内蔵の表示は外せない。
決定: 「AとC」(hal-v2/DECISIONS.md:15-18)。ユーザー (console-dual/DECISIONS.md 末尾)「この時しか使わないです。2画面。もっと言うとGeforceのDVIも足して3画面でも大丈夫なように。結線の件は気にしないで」。
対象の構成は 1 つだけ: **GF の D-SUB を別のモニターへ直結 + 0FACh は 98 のまま**。GF の DVI を足した 3 画面でも破綻しない形。Trident・Cirrus は対象外 (確定)。基点 main 13a4ffdd。r1→r2 の変更は DESIGN.r2.md の冒頭。

## r2→r3 の変更
- R2-P1 (再起動で 412 が 0 行目を上書き): 起動の表示は `con_dual_was_live()` でも追記を選ぶ (描けるかとは別)。shutdown→再起動の通し試験を A3・A6 に足した (§3-4・§8)。
- R2-P2-1 (GFX2_MAX_ADAPTERS が未定義): 当面の内部定数 `CON_DUAL_MAX_ADAPTERS 4`・`CON_DUAL_MAX_HEADS 2` を console_dual.h に置き、HAL v2 の C1 で `GFX2_MAX_ADAPTERS`・`gfx2_adapter.head[2]` に置き換える (§2-1)。
- R2-P2-2 (head 1 の有無の取得元が無い): GF の暫定の能力を 1 つの内部の口 `gf_outputs_query()` (新 drivers/video/geforce/gf_outputs.c、カーネル側) にまとめた。取得元は kernel が持つ `geforce_context->display_state` (geforce_module.h:86-88) の `head[GF4_HEADS]` (hwdev/gf/display/api.h:8・21-27)。能力・接続・稼働を別の欄にした (§2-1)。
- R2-P2-3 (head ↔ D-SUB/DVI の固定対応): 固定をやめ、各 head の `route.connector`・`route.type` (hwdev/gf/display/data.h:20-24) と connector の表から求める。同じ backend・開始状態・世代での接続・head 構成の変化は口が返す `sig` で表を更新する (§2-2)。

## 1. 実物の確認 (前提・§9-1 と実コードの食い違い)
| # | 前提・§9-1 の記述 | 実物 (file:line) | 設計への帰結 |
|---|---|---|---|
| F1 | 内蔵と増設は別の画面 | 98・Trident・RGB-IN (GF) は 98 の切替回路で 1 端子 (pc98_route.h:4-7、TASK_GEFORCE6200.md「映像の経路」)。2 画面は GF の D-SUB/DVI を別のモニターへ直結したときだけ | 対象は GF 直結の構成だけ。0FACh は 98 のまま保つ (§3-3) |
| F2 | Trident も相手 | Trident は 0FACh=2 を無条件に書く (trident/lifecycle.c:6-8) | 対象外 (確定)。断る |
| F3 | 停止は con_sink の門だけ | GF の開始で `pc98_console_set_safe(0)` (backend_geforce.c:618)、route は :627・:728。safe=0 は TVRAM の口を全部止める (console.c:55・67・102・117・124・134) | pc98_route.c で直結のとき safe を戻す |
| F4 | gfx_core_init.inc:9-14 を直す | 早い戻りは TVRAM に触らない。`_gfx_common_init` の消去 (gfx_core.c:257-269) は planar だけ | 変更なし (PPC B3 と衝突しない) |
| F5 | console.c:53-56・195-204 | 門は 53-56 の他に tvram_clear 106・console_set_cursor 388 | 門は 4 か所 |
| F6 | kernel.c:473 は消去 | GUI の後は sink が有効なので TVRAM は消さないが、`con_sink_push_clear()` (103) は積む | TVRAM と sink のクリアを分ける |
| F7 | (パニック未記述) | `platform_fault_cell` は門なしで TVRAM に直書き、論理カーソルを進めない (emergency.c:19-23) | §4 |
| F8 | backend の切替で世代が進む | `gfx_shutdown` は世代を進めず GF→PC98 に替える (gfx_core.c:477-490)、gshell の異常終了で呼ばれる (kernel/gui.c:154) | キャッシュの鍵に g_backend と開始状態を足す |
| F9 | HAL v2 の列挙がある | gfx2_* はまだコードに無い (grep 0 件)。設計は GFX2_MAX_ADAPTERS 4・head[2] (HAL_V2_DESIGN.md:532・554) | 今は g_backend から表を作り、HAL v2 の C1 後に列挙へ差し替え |
| F10 | GF の head 数・connector を外から聞ける | `GfxBackend` に head 数の口は無い。GF の状態は kernel 所有の貸し出し `geforce_context->display_state` (geforce_module.h:86-88) に `head[2]` (enabled・route.connector/type/head_mask・dpms) と `edid_connector` (検出像の connector、無ければ GF4_CONNECTORS) がある (api.h:21-27・37-41) | §2-1 の暫定の口はここを読むだけ (geforce.mod 不変) |

## 2. 構造
### 2-1. 出力先の表 (N 個) と暫定の能力の口
```c
/* kernel/console/console_dual.h — 内部だけ (KAPI・kexports に出さない) */
#define CON_DUAL_MAX_ADAPTERS 4   /* HAL v2 C1 で GFX2_MAX_ADAPTERS に置き換え */
#define CON_DUAL_MAX_HEADS    2   /* HAL v2 C1 で gfx2_adapter.head[] の数に置き換え */
#define CON_OUT_MAX (CON_DUAL_MAX_ADAPTERS * CON_DUAL_MAX_HEADS)
enum { CON_OUT_NONE = 0, CON_OUT_TEXT = 1, CON_OUT_DESK = 2 };          /* 役割 */
enum { CON_CONN_UNKNOWN = 0, CON_CONN_ON = 1, CON_CONN_OFF = 2 };       /* 接続 */
struct con_out {          /* adapter・head・connector は HAL v2 の番号の意味 */
    u8 adapter, head, connector, conn_type;   /* conn_type は GFX2 の VGA・DVI_I/D・TV・PC98_ANALOG */
    u8 conn_status;       /* 接続 (CON_CONN_*) */
    u8 running;           /* 稼働 (head が走査中) */
    u8 role, live;        /* 役割と、今そこへ描けるか */
};
void con_dual_configure(int on);
int  con_dual_cfg(void);
int  con_dual_active(void);        /* 1 = GUI 中も TEXT の出力先へ描く */
int  con_dual_was_live(void);      /* この GUI の間に 1 度でも active (sticky) */
void con_dual_session_end(void);   /* sticky を下ろす (gdc_start の最後) */
const struct con_out *con_dual_outputs(int *n);   /* 診断・試験用 */

/* drivers/video/geforce/gf_outputs.h — GF の暫定の能力 (HAL v2 前だけ。C1 後は gfx2_head_info/connector_info に差し替え、この口ごと消す) */
struct gf_out_caps {
    u8 heads;                         /* 能力: 持っている head の数 (GF4_HEADS) */
    u8 connectors;                    /* 能力: connector の数 */
    struct { u8 running, connector, conn_type, conn_status; u32 connector_mask; } head[CON_DUAL_MAX_HEADS];
    u32 sig;                          /* 上の内容の畳み込み。接続・構成が変われば変わる */
};
int gf_outputs_query(struct gf_out_caps *o);   /* 0 / -1 (GF が無い・状態の貸し出しが無い)。I/O なし、読むだけ */

/* platform/pc98/display_dual.c — 弱いシンボル (PPC は実体なし = 0 個) */
int display_dual_outputs(struct con_out *o, int max) __attribute__((weak));
u32 display_dual_sig(void) __attribute__((weak));   /* I/O なし。gf_outputs_query の sig を返す */
```
- gf_outputs.c の求め方: `heads=GF4_HEADS`。head i ごとに `running = head[i].enabled && dpms==ON`、`connector = route.connector`、`conn_type` は `route.type` (GF4_ANALOG→VGA、GF4_TMDS→DVI_D (`dual_link` は問わない)、GF4_TV→TV、GF4_LVDS→対象外の値)、`connector_mask = 1u << route.connector` (head_mask は possible の側なので使わない)、`conn_status` は `edid_connector == connector && edid.base_valid` (hwdev/gf/display/data.h:29) のときだけ ON、それ以外→UNKNOWN (切断の確定は持たない)。EDID の失敗のフォールバック (hwdev/gf/display/modes.c:321-326 で edid を空にし、:442 で edid_connector は書く) は base_valid=0 なので ON にしない。**値はどれも起動・モード設定の時に検出して保存した情報**で、この口は保存された状態の列挙 (I/O なし)。抜き挿しのライブ検出はしない (シングルタスク・今の要件外)。sig は各欄を畳む (加算でなく `sig = sig*31 + 欄`)。
- 表 (display_dual_outputs が作る): 先頭に `{PC98, head 0, connector 0, PC98_ANALOG, ON, running 1, TEXT}`。続けて **能力として GF の全 head (GF4_HEADS = 2) を常に 1 行ずつ** (98 と合わせて計 3 行)。停止中の head は running=0・役割 NONE。`running` な head のうち **GF の机を出している head** (HAL v2 前は scanout の head = head 0 の設定、backend_geforce.c:226・279) を DESK、他は NONE。head と D-SUB/DVI の対応は conn_type から読むだけで固定しない。DVI の head に何を出すかは決めない (NONE)。
- HAL v2 の C1 後: display_dual.c の GF の行を `gfx2_adapter_count/info` と `gfx2_head_info` (`attached`・`connector_mask`)・`gfx2_connector_info` (`type`・`status`) の列挙に替え、DESK は ACTIVE の adapter の attached な head。console_dual.c・console.c は変えない。

### 2-2. 判定と表の更新条件 (console_dual.c、約 70 行)
```c
int con_dual_active(void)
{
    if (!cfg || !con_sink_is_enabled() || legacy_active()) return 0;   /* V86 は I/O の前に断る */
    u32 sig = display_dual_sig ? display_dual_sig() : 0;               /* I/O なし */
    if (key.be != g_backend || key.started != gfx_is_started() ||
        key.gen != gfx_init_gen || key.sig != sig) {                   /* 更新条件 4 つ */
        n_out = display_dual_outputs ? display_dual_outputs(outs, CON_OUT_MAX) : 0;
        key = (struct dual_key){ g_backend, gfx_is_started(), gfx_init_gen, sig };
    }
    int live = 0;
    for (int i = 0; i < n_out; i++) live |= outs[i].role == CON_OUT_TEXT && outs[i].live;
    if (live) was_live = 1;
    return live;
}
```
- 表の更新条件: (1) backend が替わる (gfx_shutdown の GF→PC98、gfx_core.c:477-490) (2) 開始状態が変わる (3) init の世代が進む (4) **同じ backend・開始状態・世代のまま** connector の接続・head の稼働・head と connector の対応が変わる (= sig が変わる)。(4) は 1 回の出力ごとに sig を読む (I/O なし、2 head 分の欄を畳むだけ)。
```c
/* display_dual.c (約 70 行) */
int display_dual_outputs(struct con_out *o, int max)
{
    struct gf_out_caps c;
    if (g_backend != &gfx_backend_geforce || !gfx_is_started() || gf_outputs_query(&c)) return 0;  /* 98・PEGC・Trident・Cirrus は対象外 */
    if (!pc98_route_safe() || pc98_route_cell_check()) return 0;  /* 0FACh=98 かつ GF が A0000 を decode しない */
    display_dual_text_on();   /* 98 のグラフィック GDC STOP・FF1 表示 ON・テキスト GDC START だけ。gfx_current_height・flip に触らない */
    /* o[0] = 98 の TEXT、o[1..] = c.head[0..c.heads-1] を §2-1 の規則で (max を超えない) */
}
```
- 断ったとき (cfg=1 で GF 以外) は 1 回だけ `kprintf("[con] CONSOLE_DUAL: <name> is not a target -> off")`。

## 3. 束ね方・起動時の選択・GUI の出入り
### 3-1. 束ね方
- 出力は 1 本のまま: `shell_print` → sink (gshell の端末) と TEXT の出力先 (今は TVRAM)。kprintf も同じ経路。論理カーソルは進める、98 のハードウェアカーソルは消したまま (打鍵は GUI へ)。

### 3-2. console.c (門 4 か所 + 新しい口 1 つ)
```c
/* 53-56 */ return safe && !legacy_active() && (!con_sink_is_enabled() || con_dual_active());
/* 106   */ if (con_sink_is_enabled() && !con_dual_active()) return;
/* 388   */ if (con_sink_is_enabled() && !con_dual_active()) return;
            cursor_x = x; cursor_y = y;
            if (con_sink_is_enabled()) console_hw_cursor_sync(); else console_hw_cursor_enable();
/* 434-443 */ if (was_gui && !con_dual_was_live()) tvram_clear(); ... con_dual_session_end();
/* 新 */ void console_session_clear(void)       /* シェルの再起動の区切り */
{ if (con_dual_was_live()) { con_sink_push_clear(); return; } tvram_clear(); }  /* sink は従来どおり CLEAR、TVRAM は残す */
```

### 3-3. 起動時の選択 (pc98_route.c)
```c
int pc98_route_to_gf(void)
{
    if (con_dual_cfg()) { pc98_console_set_safe(pc98_route_safe()); return 0; }  /* 0FACh を書かない */
    return route_set(PC98_ROUTE_GF);
}
```
- 呼び手 (backend_geforce.c:627・728) は変えない → geforce.mod 0 バイト、kexports 不変。戻り (483 の route_to_98) は 0FACh が 00 なので無書込み。

### 3-4. GUI に入る/出る
| 時点 | 今 | 2 画面 / 3 画面 |
|---|---|---|
| 入る (kernel.c:411-412) | sink 有効、TVRAM 凍結、0 行目に "Loading gshell..." | `con_dual_active() \|\| con_dual_was_live()` なら `shell_print` で 1 行を追記 (0 行目を上書きしない)。再起動の時は gfx_shutdown 後で started=0 なので active は偽だが was_live で追記を選ぶ (描けるかは門が決め、描けない間は sink にだけ積む) |
| gshell の異常終了 (gui.c:154 gfx_shutdown) | GF→PC98 | 鍵が変わり active=0 になるが was_live は残る |
| 再起動の区切り (kernel.c:473) | tvram_clear (TVRAM は残る・sink は CLEAR) | `console_session_clear()` (同じ結果を明示) |
| CUI へ戻る (gdc_start 443) | was_gui なら消去 | was_live なら消さず、最後に sticky を下ろす |
| 読み込み失敗 (kernel.c:435-436、kernel_gui_shlib.inc:13-14) | 消去 → 赤字 | 変えない |
| V86 | TVRAM はゲストのもの | 変えない (I/O 0 回) |

## 4. パニック・kdebug の出力
| 種類 | 経路 | 保持の対象・期間 |
|---|---|---|
| 止まるパニック (二重フォールト・kernel の例外) | platform_fault_cell が 0 行目から直書き、停止 | 最後の画面として残る (以後の出力が無い)。リセットまで |
| 復帰する例外 (アプリ、gshell の fault) | 直書き → 復帰 → 次の出力が論理カーソルの位置から続く | 直書きの行は次の出力で上書きされうる。**保持の対象は通常の出力の記録** (exec の fault の kprintf 行 = ログの 1 行) で、期間は CUI へ戻るまで (§3-4)。直書きの行の保持は約束しない |
| kdebug | kdebug_main.c:53 の display_restore_98 (safe=1 のまま 98 の文字画面) | 変更なし。kdebug.bin 0 バイト |

## 5. 設定
- `/etc/system.cfg` の `CONSOLE_DUAL=0|1` (無い = 0)。init/kernel_gfx_config.inc の GFX= と同じ所 (⑥ より前): `con_dual_configure(sysconfig_get_int(SYS_SYSTEM_CFG, "CONSOLE_DUAL", 0));`
- 1 は「GF を直結した構成」の宣言 (自動の検出はしない)。説明の 1 行を system.cfg の説明の正典 (INDEX の表で決まる所) へ。


## 6. 大きさの見積もり
| 物 | 増分 | 上限との関係 |
|---|---|---|
| kernel (FD・MINIMAL) | .text 約 1.0KB (console_dual 0.35、display_dual 0.3、gf_outputs 0.2、門・route・kernel.c 0.15) + .bss 約 100B (表 8 行 × 8B + 鍵) | 2HD の空き 75KB − 下限 64KB = 11KB に収まる (08_build.md:765) |
| geforce.mod・hwdev.mod・kdebug.bin | 0 | 変更なし (gf_outputs.c はカーネル側、貸し出しの状態を読むだけ) |
| MINIMAL・起動 FD の構成 | 0 ファイル | packages.yaml 不変 |

## 7. ファイルごとの変更一覧 (1 ファイル = 1 段 = 1 実装者)
| 段 | ファイル | 量 | 前提 |
|---|---|---|---|
| D1 | kernel/console/console_dual.h・.c (新) + 結線 (build/sources/order.mk・tools/tests/fixtures/build_profile_x86.json の C_KERNEL) | +35 / +70 / 各 +1 | なし |
| D1g | drivers/video/geforce/gf_outputs.h・.c (新、カーネル側) + 結線 (geforce_module.c と同じ一覧・build_profile_x86.json) | +20 / +50 / 各 +1 | D1 の .h (CON_DUAL_MAX_HEADS) |
| D2 | platform/pc98/display_dual.c (新) + 結線 (build/platform/pc98.mk・build_profile_x86.json) | +70 / 各 +1 | D1・D1g の .h |
| D3 | kernel/console/console.c (§3-2) | ±18 | D1 |
| D4 | drivers/video/pc98/pc98_route.c (§3-3) | +4 | D1 |
| D5 | init/kernel.c (412・473) + init/kernel_gfx_config.inc (1 行) | ±8 | D1・D3 |
| D6 | tools/tests/console_dual_host.c (新) + tools/checks/console-dual-host.py (新、`make check-console-dual-host`) | +240 / +20 | D1〜D4 |
| D7 | 設定の説明 1 行 (文書) | +1 | PM |
- 実装者は全部 terra (どれも 1,000 行未満)。D1・D1g の .h を先に固定し D2〜D5 を並列、**D1〜D6 を 1 本の統合の枝 (wt/int-*) で CI 1 回**の取り込みの単位にする (途中段は単独で main に入れない)。
- HAL v2 との境目: gf_outputs.[ch] は HAL v2 C1 で消える暫定の口。C1 の票に「display_dual.c の GF の行を gfx2 の列挙に差し替え、gf_outputs を消す、CON_DUAL_MAX_* を GFX2 の定数に」の 1 行を足す (PM)。
- PPC 第 5 段との順: 本票が触るファイル (console.c・console_backend.h・platform/pc98/console.c・include/tvram.h・init/kernel.c ほか) は PowerPC 第 5 段 C1〜C3・B1〜B6 と同じ (os32powerpc の指摘)。**実装は第 5 段 Z の後に出し、移った先の構成で本書の前提 (§1 の file:line と §7 の置き場) を見直してから**にする。本票の論理は新ファイル、console.c は門の式と 1 関数だけ、console_dual.c は pc98.h・TVRAM の番地を include しない、の方針は変えない。

## 8. 受入 → 試験と変異
| 受入 | 試験 | 場所 |
|---|---|---|
| A1 判定: cfg × sink × V86 × backend (pc98・pegc・trident・cirrus・geforce) × started × route_safe × セル × 更新条件 4 つ (gfx_shutdown の GF→PC98、同じ世代での sig の変化)。V86 中は inp/outp・セルの呼び出し 0 回 | console_dual.c と **display_dual.c・gf_outputs.c の実物**を組み、inp/outp・セル・g_backend・`display_state` (偽の gf4_state) だけ差し替え (libc なし) | host |
| A1g 列挙: gf4_state の head[0..1] の enabled・dpms・route.connector/type・edid_connector の組で、表は常に 3 行 (head 1 停止なら running=0・NONE、稼働なら running=1・NONE)、conn_type が route.type から決まり head 番号に依らない (head 0 = DVI・head 1 = VGA の入れ替えでも正しい)、保存された接続・稼働の変化で sig が変わり表が作り直される。edid_connector が一致しても base_valid=0 (modes.c:321・442 のフォールバック) なら UNKNOWN | 同上 | host |
| A2 display_dual_text_on の前後で gfx_current_height・gfx_flip_enabled・gfx_display_page が不変、出す I/O の列が GDC STOP・FF1・TEXT START だけ | 同上 | host |
| A3 console.c の門 4 か所、was_live で 443 が消さない、console_session_clear が sink に CLEAR を積み TVRAM を残す。**通し**: active → gfx_shutdown (started=0・PC98) → 473 → 411-412 の起動表示が追記で 0 行目が不変 → GF 再開で active に戻る | console.c と kernel.c の該当の関数を host で組む既存の型 (tools/tests/legacy_callers_host.c の流儀) | host |
| A4 route: cfg=1 で 0FACh 無書込み・safe=route_safe、cfg=0 で従来どおり | pc98_route.c を組み I/O 差し替え | host |
| A5 回帰: CONSOLE_DUAL=0 と未指定で今と同じ (GUI 中 TVRAM 凍結)。=1 + GFX=pegc/pc98/cirrus で断りの 1 行・机に文字が重ならない | NP21/W (GF が無いので 2 画面の経路はここでは通らない) | NP21/W |
| A6 GF の D-SUB 直結 + =1: 98 のモニターに kprintf・端末の出力が流れ、GF の D-SUB に机 (カメラ)。gshell の異常終了と再起動で 98 の記録が残り 0 行目が上書きされない。GUI→CUI で消えない。表が 3 行 (DVI の head は NONE、`con_dual_outputs` をシリアルの診断で出す) | 実機 (結線はユーザー、物理操作は立会い) | 実機 |
| A7 =1 + GFX=trident で断りの 1 行、Trident の画面は今と同じ | 実機 | 実機 |
- 変異 (`make check-console-dual-host` の --mutate 段): 53-56 の `|| con_dual_active()` を外す / 鍵から g_backend を外す / 鍵から sig を外す / legacy の判定を I/O の後へ / GF 以外の除外を外す / conn_type を head 番号から決める / conn_status で base_valid を見ない / 停止中の head を表から省く / text_on で gfx_current_height を書く / 443 の `!con_dual_was_live()` を外す / 412 の `|| con_dual_was_live()` を外す / session_clear の push_clear を外す / route の dual の分岐を外す → それぞれ A1〜A4 のどれかが落ちること。
- 段: D1・D1g → (D2・D3・D4・D5) → D6 → 統合の枝で CI → A5 → A6・A7 (ノートのセッション経由)。

## 9. 未決
- 無し (U1・U2 はユーザー決定で閉じた)。

## 反論
- 無し (第 2 回の 4 件は直し案どおり)。

## 確定 (2026-11-09、astra の第 3 回、PM 決定で反映)
- R3-1: 列挙は能力として GF の全 head を常に返す (98 と合わせて計 3 行)、停止中の head は running=0・NONE。A1g を揃えた。
- R3-2: 接続 ON は edid_connector の一致 + edid.base_valid。値は起動・モード設定の時に検出して保存した情報、EDID の失敗のフォールバック (modes.c:321・442) を ON にしない試験と変異を足した。
- R3-3: 抜き挿しのライブ検出はしない。暫定の口は保存された状態の列挙と明記、A6 から DVI の挿入で行が増える確認を外した。

## 確定の後の追記 (2026-11-10、今の main cfb9cec3 との前提の照らし直し、PM 決定)
- **実装の時機 (os32powerpc の指摘)**: 本票が触るファイルは PowerPC 第 5 段 C1〜C3・B1〜B6 と同じ。cfb9cec3 の時点では第 5 段で入ったのは A (`gfx/gfx_internal.h`) と E・F だけで、§7 の 4 ファイル (`kernel/console/console.c`・`console_backend.h`・`platform/pc98/console.c`・`include/tvram.h`) は変化なし。実装は第 5 段 Z の後に出し、そのとき移った先の構成で §1 の F1〜F10 の file:line と §7 の置き場を照らし直してから依頼する。§7 の「D3 と C1 は先に入った方に載せ直す (推奨: D3 が先)」はこの順に書き換えた。
- **D5 と PPC の選択集合**: `init/kernel.c` は `tools/ppc/selected_success.json` の PPC の初回成功集合に入っている (範囲の前から)。D5 は `console_dual.h` だけを include し、`drivers/`・`platform/pc98/` のヘッダを足さない (今の設計どおり)。足すなら `tools/ppc/known_header_dependencies.json` に組を足す。
- **その他の前提は変化なし**: `tools/tests/legacy_callers_host.c` は D2 で `v86_call_dispatch` のスタブの型 (`src, count`) だけ変わり、console の偽の置き換え (`:83`・`:140-143`) は同じ。GF は 1 枚の前提も保たれる (`drivers/video/geforce/geforce_platform.c` が 2 枚目を `second ... ignored` と記録、`kdebug/recover.h:54-57`)。
