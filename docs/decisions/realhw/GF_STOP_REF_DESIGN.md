# GF の表示の停止・suspend を手本 nouveau に寄せる — 設計 確定 (2026-11-12、main eebdafaa + S0 7eee4b40)

> 状態: **実装・実機の受入済み (2026-11-12、main d93f0279、R3・R5'・R6 は延期の台帳へ)**。設計は 2026-11-12 に確定 (独立レビュー 2 回、PM 決定)。第 1 回 (A1〜A9)・第 2 回 (B1〜B5) の指摘、S0 の実機の値、PM の決定を入れた。第 1 版・第 2 版はリポジトリの外 (~/os32-tmp/run/gf-stop-ref/)。
> 手本: nouveau c03c1a1 (MIT)。`N/` = `/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau`、`V/` = `N/dispnv04`、`DI/` = `N/nvkm/subdev/devinit`。refs に無い upstream の関数は名前で示す (`nouveau_display_fini`・`nouveau_display_suspend` は `nouveau_display.c`、`drm_helper_force_disable_all`・`__drm_helper_disable_unused_functions`・`drm_helper_resume_force_mode` は `drm_crtc_helper.c`)。
> OS32: `D/` = `hwdev/gf/display`。前提: シングルタスク・協調切替。GF は 1 枚。

## 0. ユーザー決定 (2026-11-12、原文)

問い:「GeForce の表示を止める道で、画面を DPMS on に戻す手順 (停止の service.c:651 と suspend の recover.c の 2 か所) が手本 nouveau に無いと分かりました。nouveau は止める時に ON を書かず、起動時に保存した値に戻すだけです。このせいで CONSOLE_DUAL の『CUI に戻る時に右のモニターを DPMS で切る』が効きません。どう直しますか?」
答え:「手本どおりに寄せる (推奨)」— 停止・suspend の道から DPMS on を外し、nouveau の nv_crtc_restore と同じく起動時に保存した値に戻す形にする。

## 0-1. PM の決定 (2026-11-12、第 2 版で入れた物)

- **Q1 は閉じた**: devinit の後の head 0 は CR17=0 (同期の出力が止まった状態) で、右のモニターは無信号。手本どおり restore すれば、CUI に戻ると右は無信号になる。CONSOLE_DUAL の `display_dual_dpms` は外す (§4、段 S5)。
- **保存は module を読み込んでから最初の 1 回だけ** (nv04_display_create の save、V/disp.c:297-302 に当たる)。2 回目以降の開始と restart では保存し直さない (§3-1)。
- **A1**: 保存値の置き場は module の中の static (D/hw_state.c)。gf4_state・hwdev の配置の契約 (HWDEV_LAYOUT_CRC) は変えない。
- **Q2**: S0 で AR10=0 (text) だったので、VGA フォントの保存・復元は手本どおり入れる。A5 の 2 点は §3-1・§3-2 #9 で決めた。
- **Q3**: 停止・suspend の cursor hide は今回は残す。手本に無い処理として §9 に残し、後でユーザーに尋ねる。
- **Q4**: quiesce の PCRTC_START の二度書きは残す。**Q6**: A6 のとおり書き直した (§3-2 #4)。
- A2・A3・A7・A8・A9 を本文に入れた。A3 の PLL は setPLL_double_highregs をそのまま写す。

## 1. 事実

| # | 事実 | 根拠 |
|---|---|---|
| F1 | 停止の DPMS ON は `svc_dpms_on` (D/service.c:139-172)、呼び出しは :651-655。suspend の DPMS ON は `rec_dpms_on` (D/recover.c:138-169)、呼び出しは :194 | ソース |
| F2 | DPMS ON の根拠は OS32 の設計だけ (GF_FEAT4_DESIGN.md:59・561・655・722) | grep |
| F3 | OS32 は起動時の CRTC の状態を保存していない。今あるのは 3 つだけで、SR01・CR17・CR1A・DACCLK は保存も復元もしていない。<br>・baseline (D/baseline.c:12-36): nv_crtc_save の「mode_reg の種」(V/crtc.c:684-689) に当たる<br>・ロックと index (hwdev/gf/bus.c:73-98)<br>・PCRTC_START (D/quiesce.c:59-67) | ソース |
| F4 | nouveau の unload の順:<br>1. `nouveau_display_fini(dev,false,false)` (N/nouveau_drm.c:597-598)<br>2. `drm_helper_force_disable_all` → 使われていないエンコーダーに dpms(OFF)。CRTC は `.disable = nv_crtc_disable` (V/crtc.c:801-813) で unpin だけ<br>3. `nv04_display_fini` (V/disp.c:56-96、INTR_EN=0)<br>4. `nv04_display_destroy` (V/disp.c:189-208): enc_restore (:196-197) → nv_crtc_restore (:199-200) → フォントの復元 (:202)<br>5. `nouveau_accel_fini` (N/nouveau_drm.c:601) | refs |
| F5 | `nv_crtc_restore` (V/crtc.c:692-706) = owner → `nouveau_hw_load_state` → CR21=保存値 → last_dpms=CLEARED。<br>`nouveau_hw_load_state` (V/hw.c:827-835) の順: VgaProtect(true) → ramdac (:468-536) → ext (:665-772) → palette (:795-811) → vga (:564-586) → VgaProtect(false)。<br>VgaProtect(false) は SR01 bit5 を必ず落とし (V/hw.h:243-256)、vga の CR の書き込みは CR11 bit7 を必ず立てて終わる (`nv_lock_vga_crtc_base(true)`、V/hw.h:272-284) | refs |
| F6 | nouveau の保存: create の中 (V/disp.c:245 フォント → :297-299 crtc → :301-302 enc) と、init の先頭 (V/disp.c:112-121、load と resume)。create は nvkm devinit (POST) の後。<br>フォントの保存・復元は最後に `NVBlankScreen(head, false)` を両 head に書く (V/hw.c:367-369) ので、フォントの後に読む crtc の保存値の SR01 bit5 は 0 になる | refs |
| F7 | nouveau の suspend (`nouveau_display_suspend` → `nouveau_display_fini(dev,true,…)` → V/disp.c:56-96) は DPMS も restore も書かない。resume は vbios init (N/nouveau_drm.c:1023) の後に保存し直してから `drm_helper_resume_force_mode` (V/disp.c:170) | refs |
| F8 | OS32 の restart は、初期化済みなら devinit を再実行しない (D/recover.c:287-295) | ソース |
| F9 | scan の最初の面は VRAM 0 から予約する (D/scan.c:202、`GF_MODE_VRAM_OFFSET`=0)。フォントの窓 (BAR1 の 0〜64KiB) と重なる | ソース |
| F10 | `gf_display_stop` は時計が読めないと `gf4_service_stop` を呼ばずに下の停止へ進む (D/quiesce.c:107-122) | ソース |

### 1-1. S0 の実機の値

S0 7eee4b40 で、owner を CR44 で切り替えて読んだ値。証拠はノートの ~/os32-tmp/evidence/2026-11-12/gf-s0/。

| | 1 回目の開始 (起動後に初めて GUI へ、devinit の後) | 2 回目の開始 (今のコードで 1 回目の停止の後) |
|---|---|---|
| head 0 | cr11=20 sr01=0 cr17=0 cr1a=3f fptg=11100011、cr21=fa ar10=0 config=0 | sr01=1 cr17=c3 cr1a=2c fptg=20000022、cr21=fe ar10=1 config=2 |
| head 1 | sr01=20 cr17=0 cr1a=2b fptg=11100011、cr21=fa ar10=0 config=0 | 1 回目と同じ |
| DACCLK | dacclk0=1 dacclk1=101 | 1 回目と同じ |
| PLL | vpll1=51104 vpll2=51104 b1=80000e02 b2=e02 ext=1f0001f pllsel=4 sel_clk=0 | vpll1=21a07 b1=8000011f ext=1f0011f pllsel=204 |
| 右のモニター | 起動の後・GUI の前は無信号 (黒、LED 橙) | GUI では机。CUI に戻すと机が止まったまま (svc_dpms_on のため) |

読み方:
- **devinit の後の head 0** は CR17 bit7=0 (同期の出力なし)、AR10 bit0=0 (text)、CONFIG=0 (VGA) だった。
- **保存値の VPLL は 0 でない** (51104) ので、restore は PLL を書く。
- **2 回目の開始の値**は 1 回目の停止の後の値、つまり OS32 の GUI の mode で、devinit の後の値ではない。OS32 は 2 回目以降に devinit を走らせない (F8) ので、2 回目以降に保存すると GUI の mode を保存してしまう。これが「保存は最初の 1 回だけ」の理由 (§3-1)。

## 2. 今の順と手本の順

### 2-1. 通常の停止 (`gf4_service_stop`、D/service.c:611-690) と nouveau の unload

| # | OS32 の今 | nouveau | 判定 |
|---|---|---|---|
| 1 | 受付を閉じる (:618-627) | fbcon 停止・flip event block (V/disp.c:63) | 一致 (役割) |
| 2 | pending の flip 収束 (:642-646) | 無し | OS32 の追加 (X1-X4)。触らない |
| 3 | cursor hide (:649) | 無し。cursor のレジスタは #8 で保存値に戻る | **手本に無い**。外した (末尾の追記) |
| 4 | **DPMS ON** (:651-655) | 無い | **外す** (ユーザー決定) |
| 5 | (無い) | エンコーダー dpms(OFF) (force_disable_all) | **足す** |
| 6 | (無い) | PCRTC_INTR_EN_0=0 (V/disp.c:66-69) | 順が違う。このレジスタは F1 の IRQ 層が持ち、F2/F1 の停止で 0 にする (hwdev/gf/irq.c:91・226-234)。F4 から書くと F1 の wanted_heads と食い違うので変えない |
| 7 | (無い) | enc_restore (V/disp.c:196-197) | **足す** |
| 8 | (無い) | nv_crtc_restore (V/disp.c:199-200) | **足す** |
| 9 | (無い) | フォントの復元 (V/disp.c:202、V/hw.c:309-371) | **足す** (Q2 で入れると決めた。置き場は §3-2 #9) |
| 10 | 窓 close・F3 fini (:657-665) | `nouveau_accel_fini` | 一致 |
| 11 | gf2_stop (:666-671) | channel の終了 | 一致 |
| 12 | quiesce (:672): foundation_stop → PCRTC_START=baseline_start → 局所 restore → quiesce rows | START は #8 の V/hw.c:772 で書く | 二度書き (同じ値)。残す (Q4) |
| 13 | tiles retire・予約の返却 (:674-686) | ttm_fini など | OS32 の資源 |

### 2-2. suspend (`rec_suspend`、D/recover.c:172-216)

| # | OS32 の今 | nouveau | 判定 |
|---|---|---|---|
| 1 | 受付を閉じる (:175-177) | `drm_client_dev_suspend`・flip block | 一致 |
| 2 | `rec_capture` (:179) | 非 atomic では drm core の状態が残る | 一致 (写し方が OS32 流) |
| 3 | flip 収束 (:182-185) | 無し | OS32 の追加 |
| 4 | fence (:186-189) | `nouveau_channel_idle` (N/nouveau_drm.c:966-976) | 一致 |
| 5 | cursor hide (:192) | 無し (unpin だけ、V/disp.c:85-92) | **手本に無い**。外した (末尾の追記) |
| 6 | **DPMS ON** (:194) | 無い (F7) | **外す** |
| 7 | close・fini・F2 stop・foundation_stop (:197-215) | fence suspend・`nvif_client_suspend` (:980-990) | 一致 |

### 2-3. 再開 (`gf4_service_restart` + `gf4_display_restore`)

| # | OS32 | nouveau | 判定 |
|---|---|---|---|
| 1 | BM → (未初期化なら) devinit / validate → F1 → F2 → objects → match → surfaces | nvif resume・`nouveau_run_vbios_init` (:1023) | 一致 (devinit を毎回は走らせない点は F8) |
| 2 | (無い) | 保存し直す (V/disp.c:112-121) | **外す** (理由は §3-1、S0 の 2 回目の値) |
| 3 | `gf4_display_restore(hold_saved)` | `drm_helper_resume_force_mode` (V/disp.c:170) | 一致 |

## 3. 寄せた後

### 3-1. 保存 (create の save に当たる所)

**置き場 (A1)**
- 新しいファイル `D/hw_state.c` の static `struct gf4_hw_saved hw_saved` を 1 つ置く。nouveau の `saved_reg`・`saved_vga_font` もドライバの私有 (V/disp.h:84-92)。gf4_state と HWDEV_LAYOUT_CRC は変えない。
- `struct gf4_hw_saved` (宣言は `D/hw_state.h`) の中身:
  - `crtc[2]`: `nv04_crtc_reg` (V/disp.h:23-69) のうち、NV44A (Curie・gf4_disp_arch・two heads・chipset>=0x30) で読む欄。MISC・CR[0xa0]・CR58[16]・SR[5]・GR[9]・AR[21]・DAC[768]・PCRTC 8 個・PRAMDAC 群・pllvals・ctv[38]
  - `pllsel`・`sel_clk`
  - エンコーダー (DCB の route ごと): `output` (DACCLK)・TMDS の bound head・TV の state
  - フォント `font[4][16384]` (u32、256KiB)
  - `valid` (crtc・enc の保存が揃ったか)・`fonts_valid` (フォントの保存が揃ったか)
- フォントの 256KiB は static に置かず、保存の時に `pgalloc_alloc_phys` で取る (hwdev/gf/devinit.c:183 の scratch と同じ型)。AR10 bit0=1 (graphics) なら、手本どおり保存しない (V/hw.c:322-326) のでページも取らない。

**時機: devinit を実際に走らせた直後の 1 回だけ**
- `gf4_hw_save(c,l)` を `svc_rom` の 1 か所 (D/service.c:314 の `gf_display_baseline` の後、:316-319 の feature5_deadline・`svc_feature5` の前、`l->owner->release` の前) で呼ぶ。module の寿命で最初の devinit の直後なので、nouveau の create (devinit の後) に当たる。
- `gf4_service_restart` の未初期化の枝 (D/recover.c:287-294) には置かない。到達しないため: restart は hold_saved を要し、hold_saved は suspend が取り、suspend は ready を要し、ready は initialized の後にしか立たない (B2)。
- エンコーダーの保存は `c->dcb.routes`・`c->dcb.nroutes` を使う (B3)。svc_rom の中では `c->routes`・`c->nroutes` はまだ入っていない (D/service.c:326 で release の後に入る)。
- 2 回目以降の開始と restart では保存しない。理由:
  - S0 の 2 回目の開始の値 (cr17=c3・config=2・vpll1=21a07) は、前の停止の後の GUI の mode で、devinit の後の値ではない。
  - OS32 は 2 回目以降に devinit を走らせない (F8)。nouveau の init・resume の保存し直し (V/disp.c:112-121) を写すと GUI の mode を保存し、次の停止で机をまた映す。
  - nouveau 自身も V/disp.c:104-111 で、保存は「load の前の状態」のためと書いている。
- 保存の順 (A5): **フォント → crtc (head 0・1) → エンコーダー** (V/disp.c:245 → 297-299 → 301-302)。F6 のとおり、フォントの後に読む head 1 の SR01 bit5 は 0 になる (S0 の 20 ではない)。手本どおりこのまま。
- 保存の前に拡張 CR を解錠する (CR1F=57)。nvkm devinit の `nvkm_lockvgac(false)` (DI/base.c:94) に当たり、今の `svc_detect_prepare` (D/service.c:472) と同じ書き込み。
- レジスタの読み書きは devinit と同じ `l->io` (production の io) を使う。svc_rom は F1/F2 の前で、まだ `c->ch->f` が無いため。BAR1 の 0〜64KiB は `raw` が許す VRAM の範囲 (hwdev/gf/bus.c:31-32、GF_MODE_VRAM_BYTES まで) に入る。

**valid と失敗の扱い (A9・B1)**
- **保存に失敗しても svc_rom は成功させる** (開始は続ける)。devinit の acquire は module の寿命で 1 回しか試せない (hwdev/gf/devinit.c:175-176 の `p->attempted`) ので、保存の失敗で開始を失敗にすると GF が二度と使えなくなる。手本も保存の失敗で止めない (V/hw.c:330-334、フォントの map に失敗しても記録して続ける)。
- フォントのページが取れない、またはフォントの読みが失敗したら、フォントだけ飛ばして `fonts_valid=false` とし、`[gf4] hw save fonts skipped rc=…` を記録する。crtc・enc の保存は続ける。
- crtc・enc の保存が失敗したら `valid=false` とし、`[gf4] hw save failed stage=… rc=…` を記録する。停止は restore しない (§3-2)。
- `valid`・`fonts_valid` が true になるのは、それぞれ全部読めたときだけ。どちらも false に戻すのは module の終了 (`gf4_hw_state_reset`。フォントのページもここで返す) だけ。保存は 1 回きりなので、失敗した module の寿命の間は restore しない。

### 3-2. 通常の停止 (寄せた後、`gf4_service_stop`)

1. 受付を閉じる (今と同じ)。
2. pending の flip 収束 (今と同じ、running の時だけ)。
3. cursor hide (今と同じ、§9)。
4. **エンコーダーの DPMS OFF** (force_disable_all)。enabled で `dpms != GF4_OFF` の head の route に、`gf4_analog_dpms`・`gf4_tmds_dpms`・`gf4_tv_dpms` (OFF) を呼ぶ。
   - 根拠 (A6): nouveau では、使われていないエンコーダーは fbcon の最初の modeset の `__drm_helper_disable_unused_functions` で既に OFF になっている。そのため unload の時には last_dpms の判定 (V/dac.c:465-467、dfp・tv も同じ) で何も書かず、書くのは使っているエンコーダーだけ。
   - 差の記録: OS32 の `svc_initial` は、使っていないエンコーダーを OFF にしない。S0 の dacclk1=101 (OR1 の DAC が devinit の後に on のまま) がその例。停止の #5 で保存値に戻るので、結果は同じ。開始の側の差は今回は直さない。
   - running を条件にしない。MMIO だけで足り、手本の destroy も無条件。revoked なら owner bus が拒む。
5. **enc_restore** を、保存した全 route に。
   - DAC: DACCLK=保存値 (V/dac.c:485-495)
   - TV: DACCLK と `nv17_tv_state_load(saved)` (V/tvnv17.c:628-639、V/tvmodesnv17.c:406-)
   - TMDS: 保存した bound head の保存 PLL の clock で `exec->tmds_script` (V/dfp.c:604-609、D/api.h:47)
   - LVDS はこのカードに無い。DCB に出たら EUNSUPPORTED を記録する。
6. **crtc_restore** を head 0・1 に (V/crtc.c:692-706): owner → `gf4_hw_load` → CR21=保存値。`gf4_hw_load` の順と中身は §3-5。
7. 失敗は今と同じく、最初の rc を残して先へ進む (手本の restore は void)。
8. 窓 close → F3 fini (fence)。
9. **フォントの復元** (V/disp.c:202、V/hw.c:309-371)。F3 fini が成功した後・gf2_stop の前に置く。条件と扱い (A5):
   - **置き場の理由**: 手本の順は crtc の restore の直後 (accel_fini の前)。OS32 では F3 fini の fence を待つまで、2D の仕事が VRAM 0 からの面 (F9) に書き込み得るので、fence の後に置く。crtc の restore の後である順は手本と同じ。
   - **`c->diagnostic_saved` があるときは復元しない**。理由:
     - 診断の保存は、予約と面の中身を保ったまま次の開始で戻す約束 (D/save.c:366-404、D/scan.c:92-95)。
     - 最初の面は VRAM 0 からなので、BAR1 の 0〜64KiB へのフォントの書き込みが保存した画を壊す。
     - 復元しなかったことを `[gf4] stop fonts skipped diagnostic_saved` と記録し、フォントは保存したまま残す。次に diagnostic_saved の無い停止が来たら復元する。PC-98 ではフォントの使い手がいないので、表示への影響は無い。
   - F3 fini を走らせない (close に失敗した) ときは復元しない。fence が無いため。
   - 復元は最後に両 head の SR01 bit5 を 0 にする (F6)。#6 の VgaProtect(false) と同じ値。
10. 以降は今と同じ: gf2_stop → quiesce → 予約の返却。

あわせて変えること:
- `svc_dpms_on` と DPMS 用の定数 (D/service.c:117-131・139-172) を削除する。
- restore (#5・#6・#9) も `l->io` (production の io、`c->legacy->io`) を使う (B4)。開始が失敗した停止では `c->ch->f` が入っていないことがある (D/service.c:540、:678-680)。#4 のエンコーダー OFF は既存の関数 (`c->ch->f->io`) なので、`c->ch->f` が無いときは飛ばす (#5 で上書きされる一時の値なので結果は同じ)。
- `hw_saved.valid == false` のときは #4〜#6 を飛ばし、`fonts_valid == false` のときは #9 を飛ばす。読んでいない値をハードに書かないため。手本は create の失敗でも 0 埋めの値を restore するが、そこは写さない。
- **diagnostic_saved のときの SR01 bit5 (A9)**: 診断の保存は SR01 bit5 を立てて画面を隠している (D/save.c:109-120)。#6 の restore は VgaProtect(false) で bit5 を落とすが、保存値の head 0 は CR17=0 (同期なし) なので画面は出ない。次の開始の `gf4_diagnostic_restore` が modeset からやり直す。手本どおり特別扱いしない。

停止の後の画面: SR01 bit5=0、CR11 bit7=1。CR17・CR1A・DACCLK・FP_TG・PLL・CONFIG は保存値。S0 の値なら head 0 は CR17=0 で**無信号**になる。

### 3-3. 時計が読めない停止 (A9、`gf_display_stop` の D/quiesce.c:107-122)

- 今はこの枝が `gf4_service_stop` を通らないので、#4〜#9 が走らない。この枝でも下の停止 (gf3_service_close) の前に `gf4_hw_restore_nodeadline(c->display4)` を呼ぶ。中身は #5 のうち DAC・TV の DACCLK と TV の state、および #6 の crtc_restore。
- 期限の要る物は飛ばす:
  - #4 のエンコーダー OFF と TMDS の script は期限が要る (D/tmds.c:141-145 の fp_deadline)。#4 は手本でも #5 で上書きされる一時の値なので、結果の画面は同じ。TMDS は飛ばしたことを記録する。
  - #9 のフォントは、F3 fini の fence が無いので飛ばす。
- load_state と setPLL_double_highregs には待ちが無いので、期限は要らない。V/hw.c:746-757 の CRE_53 の待ちは Kelvin 未満だけで、DI/nv04.c:198-273 には待ちが無い。
- FEAT4 の X4 (時計の失敗) の文に足す (§8)。

### 3-4. suspend (寄せた後、`rec_suspend`)

- `rec_dpms_on` (D/recover.c:138-169) と、D/recover.c:82-96 のうち DPMS 用の定数を削除する。:191-196 は cursor hide だけを残す。
- suspend は SR01・CR17・CR1A・DACCLK を書かない。saved_closed の物理 blank はそのまま残る。
- 再開は今と同じく、`gf4_display_restore(hold_saved)` が prepare・commit と image の dpms を戻す。restart で保存し直さない (§3-1)。

### 3-5. `gf4_hw_load` (nouveau_hw_load_state、V/hw.c:827-835) の中身 (A3・A7)

順 (括弧は V/hw.c の行):
1. `NVVgaProtect(true)` (V/hw.h:243-256): SR01 を読む → SR00=1 → SR01|=20 → AR gate。
2. `nv_load_state_ramdac` (:468-536) を次の順に書く:
   - CURSYNC → **PLL (下の「PLL」)** → PLL_COEFF_SELECT=pllsel → SEL_CLK → GENERAL → 630 → 634
   - TV 8 個 → FP vert/horiz 7 組 → FP_DITHER・850/85C 3 組
   - FP_TG → FP_DEBUG 0-2 → MARGIN → 8C0 → A20・A24・A34 → CTV 38 個
3. `nv_load_state_ext` (:665-772):
   - ENGINE_CTRL を CR33 (LCD) より先に書く (:671-678)。
   - **PVIDEO_STOP=1・PVIDEO_INTR_EN=0・OFFSET_BUFF(0/1)=0・LIMIT(0/1)・UVPLANE_LIMIT(0/1)=VRAM の量-1・PBUS_POWERCTRL_2=0** (:682-690)。VRAM の量は validate が読む PFB の量 (hwdev/gf/devinit.c:209-213) を使う。
   - CURSOR_CONFIG・830・834 (:692-694)・GPIO_EXT (:697)・850 (:700)。**PRAMDAC_900 の bit16 は crtc_cfg==HSYNC なら立て、でなければ落とす** (:702-706)。
   - 続く順:
     - CONFIG (:710)
     - CR の RPC0・RPC1 (CR1A)・LSR・PIXEL・LCD・HEB・ENH・FF・FFLWM (:712-720) → CR47 (:723) → CR9F (:726) → CR49 (:728)
     - HCUR_ADDR0/1/2 (:729-731) → **`nv_fix_nv40_hw_cursor`** (:732-733、V/hw.h:341-) → ILACE (:734)
     - SCRATCH3・SCRATCH4 (:736-737) → EBR・CSB・4B・TVOUT_LATENCY (:739-742)
     - CR42・53・54 (:759-761) → CR57/58 の 16 組 (:763-764) → 59・5B (:765-766) → 85・86 (:768-769)
     - PCRTC_START (:772)
   - 垂直帰線待ち (:746-757) は Kelvin 未満だけなので入れない。
4. palette (:795-811): PIXEL_MASK=FF → WRITE_INDEX=0 → 768 バイト → AR gate を戻す。
5. `nv_load_state_vga` (:564-586): MISC → SR0..4 → CR11 bit7 解除 → CR0..24 → CR11 bit7 設定 → GR0..8 → AR0..20 (index ごとに AR の FF をリセット、gate)。
6. `NVVgaProtect(false)`: SR01 = 読んだ値 & ~20 → SR00=3 → AR gate を戻す。

**PLL (A3)**: `clk->pll_prog` = `nv04_clk_pll_prog` → `setPLL_double_highregs` (DI/nv04.c:198-273) を**そのまま写す**。gfinit_pll_sequence には渡さない。
- reg1 は head 0 が 0x680508、head 1 が 0x680520。reg2 = reg1+0x70 (0x680578) / reg1+0x5c (0x68057c)。
- pll1 = (old1 & 0xfff80000) | log2P<<16 | NM1、pll2 = (old2 & 0x7fff0000) | 1<<31 | NM2。
- chip 0x44 > 0x40 で reg1 >= 0x680508 なので、0x680580 を `new_ramdac580` で計算し、今の値と違えば old1 を ~0 にして必ず書く。single_stage (NM2=0 か N2==M2) なら pll2 |= 0x011f。
- old1==pll1 かつ old2==pll2 なら何も書かない (already set)。
- `powerctrl_1_shift` (DI/nv04.c:113-140) が 0 以上なら、0x1584 を退避して書く。chip>=0x40 なら 0xc040 の該当の 2 bit を落とす (shift は reg1 で決まる)。
- NV44A (chip 0x44) での値 (B5): powerctrl_1_shift は head 0 (0x680508) が -4+4+4+4=8、head 1 (0x680520) が 12 で、どちらも「shift>4 かつ chip>=0x40」で -4 になる。**0x1584 はどちらの head でも書かない** (PM の指示では head 1 だけとあったが、DI/nv04.c:120-137 の計算では head 0 も -4)。c040 の shift は head 0 が 16、head 1 が 18 (DI/nv04.c:244-259)。
- 書く順: 0x680580 (違うときだけ) → reg2 → reg1 → 0x1584 を戻す → 0xc040 を戻す。
- 保存の側は `nouveau_hw_get_pllvals` (V/hw.c:163-202) と `nouveau_hw_decode_pll` (:131-161) を写す。
- S0 の保存値は vpll1=51104・b1=80000e02 で 0 でない。GUI の mode の 21a07/8000011f とは違うので、2 回目以降の停止では必ず書く。

## 4. CONSOLE_DUAL との関係 (Q1 は閉じた)

- 停止の restore が head 0 を起動時の値 (CR17=0、無信号) に戻すので、CUI に戻ると右のモニターは無信号になる。ユーザー決定の「DPMS で切る」はこれで満たされる。
- **`display_dual_dpms` を外す (段 S5)**。外す所:
  - 関数 `platform/pc98/display_dual.c:27-37`
  - 宣言 `kernel/console/console_dual.h:24`
  - 呼び出し `gfx/gfx_core.c:394` (停止の前の dpms(3)) と `gfx/gfx_core_init.inc:16` (開始の後の dpms(0))
  - 試験の弱いシンボル `tools/tests/gfx2_backend_reg_host.c:473`・`tools/tests/test_gfx_policy_triggers.py:70`・`tools/tests/sep5_gfx_init_host.c:124`
  - `tools/tests/console_dual_host.c:92-95・221-223` (A3 DPMS)、`tools/tests/test_console_dual.py:52` (変異 'DPMS call')
- 開始の後の dpms(0) が要らない理由: GUI への再入は毎回 `svc_initial` が `dpms=GF4_ON` の image (D/service.c:372) で modeset し、commit で DPMS ON にする。
- 外すのは S4 (restore が入る段) の実機の受入 R5 の後。S4 より前に外しても、svc_dpms_on のせいで右に机が止まって映る今の状態と同じなので害は無いが、受入の意味が無くなる。

## 5. 試験

### 5-1. 影響を受ける既存の試験

| 試験 | 直し |
|---|---|
| tools/tests/gf4_c2rec_host.c:327-330 | suspend の前後で SR01・CR17・CR1A・DACCLK への書き込みが 0 回、settle が無いことに替える |
| tools/tests/test_gf_display.py:1264-1270 (変異 `c2rec`/`saved-blank`) | 削除して §5-2 の変異に替える |
| tools/tests/gf4_c2svc_host.c:339-349 | 停止は常に settle が無く、最初の時計は F3 fini。saved_closed=false の場合も同じ結果になる行を足す |
| tools/tests/gf4_c2svc_host.c:357 の注記 | "suspend does not write DPMS" に替える |
| tools/tests/gf4_c2log_host.c (S0 で足した detect state の欄) | 2 回目の開始の期待値を「1 回目と同じ。ただし SR01 bit5・CR11 bit7・CR21 を除く」にする (fixture は停止の restore を通す) |
| tools/tests/console_dual_host.c・test_console_dual.py ほか 3 本 | S5 で §4 の行を削除する |
| gf4_c2stop_host.c・gf5_display_lifetime 系 | 書き込みの数や順を見る行が落ちたら、fixture の CR/SR/RAMDAC の初期値を S0 の 1 回目の値にして直す |

### 5-2. 足す試験と変異

**新しい `tools/tests/gf4_hwstate_host.c`** (test_gf_display.py に段 `hwstate` を足す)。偽のレジスタ表の初期値は S0 の 1 回目の値にする。
- 保存:
  - フォント → crtc 0・1 → エンコーダーの順。owner を head ごとに CR44 で選ぶ。
  - AR10 bit0=1 ならフォントを読まず、ページも取らない。
  - crtc の読みが失敗したら valid=false、フォントのページが取れなければ fonts_valid=false で crtc・enc は保存する。
  - **保存が失敗しても svc_rom (開始) は成功する** (B1)。
- load: §3-5 の 1〜6 の順。PVIDEO・PBUS_POWERCTRL_2・900 bit16・nv_fix_nv40_hw_cursor が §3-5 の位置にあること (A7)。
- PLL (B5、head ごとに期待値を分ける): S0 の値 (今の値 21a07/8000011f、保存値 51104/80000e02) で、
  - head 0 (reg1=0x680508・reg2=0x680578): c040 の bit 16-17 を落とす → (580 が違えば 580) → reg2 → reg1 → c040 を戻す。0x1584 は書かない。
  - head 1 (reg1=0x680520・reg2=0x68057c): c040 の bit 18-19 を落とす → (580) → reg2 → reg1 → c040 を戻す。0x1584 は書かない。
  - 同じ値なら 0 回。
- 終わりの値: SR01 bit5=0・CR11 bit7=1・CR17=0・CR1A=3f (head 0)・CR21=fa・DACCLK=1/101。
- 変異 (それぞれ RED になること):
  - VgaProtect(false) の `& ~0x20` を外す
  - ENGINE_CTRL を CR33 の後へ
  - CR11 の解錠を外す
  - PLL の already-set の判定を外す (同じ値でも書く)
  - reg2 と reg1 の順を入れ替える
  - c040 の退避を外す
  - c040 の shift を head で変えない (head 1 も 16)
  - 0x1584 を書く (powerctrl_1_shift の -4 の判定を外す)
  - 保存の失敗で svc_rom を失敗にする (B1)
  - エンコーダーの保存を `c->routes` で読む (svc_rom の中では 0 件、B3)
  - PVIDEO_STOP を外す
  - 900 bit16 の判定を逆にする
  - nv_fix_nv40_hw_cursor を外す
  - owner を head 0 に固定する
  - CR21 の復元を外す
  - フォントの保存を crtc の後へ
  - AR10 の判定を外す

**停止 (gf4_c2svc_host.c、S4a)**
- 確かめること:
  - (a) CR17 bit7 を立てる書き込みが無い。
  - (b) エンコーダー OFF → enc_restore → crtc_restore → close → F3 fini → フォント → gf2_stop の順。
  - (c) 停止の後の値が fixture の保存値と同じ。
  - (d) valid=false なら #4〜#6 を、fonts_valid=false なら #9 を書かない。
  - (g) 開始が失敗して `c->ch->f` が無い停止でも、restore が `l->io` で走る (B4)。
  - (e) diagnostic_saved ならフォントを書かず、記録の行が出る。
  - (f) 2 回目の開始で保存しない。
- 変異: restore の io を `c->ch->f->io` にする (開始の失敗の後の停止で落ちる、B4) / enc と crtc の restore の順を入れ替える / エンコーダー OFF を外す / フォントを F3 fini の前へ / diagnostic_saved の判定を外す / valid の判定を外す / 2 回目の開始で保存する。

**時計の失敗 (gf4_c2stop_host.c、S4a)**
- 時計が失敗した gf_display_stop で、crtc_restore と DACCLK の restore が走り、エンコーダー OFF・TMDS・フォントは走らないこと。
- 変異: この枝の restore を外す。

**suspend・restart (gf4_c2rec_host.c、S4b)**
- suspend で DPMS のレジスタを書かないこと。restart で保存しないこと。
- 変異: restart で保存する。

## 6. 段の表 (terra に出す形: 1〜3 ファイル・400 行以下)

| 段 | ファイル | 前提 | 見込み | 実装者 |
|---|---|---|---|---|
| S0 観測 (済、7eee4b40) | D/service.c の detect state の欄と owner の切り替え、tools/tests/gf4_c2log_host.c、test_gf_display.py | — | 実装済み | main に入れる段として統合の枝に載せる |
| S1 保存 | D/hw_state.h (新、宣言)、D/hw_state.c (新、static の置き場・`gf4_hw_save`・`gf4_hw_state_reset`・save_state の ramdac/vga/palette/ext・get_pllvals/decode_pll) | — | 300 | terra |
| S1b フォント | D/hw_fonts.c (新、V/hw.c:289-371 の save/restore、ページの取得と返却) | S1 の宣言 | 120 | terra |
| S2 復元 | D/hw_load.c (新、§3-5 の 1〜6 と `nv_crtc_restore`) | S1 の宣言 | 300 | terra |
| S2b PLL | D/hw_pll.c (新、setPLL_double_highregs・new_ramdac580・powerctrl_1_shift の写し) | S1 の宣言 | 120 | terra |
| S3 エンコーダー | D/enc_state.c (新、DAC/TMDS/TV の save/restore。bound head は V/dfp.c:55-、TV の state の save/load は V/tvmodesnv17.c:379-430)、D/tv.c (tv_state_load を公開する 1 行) | S1 の宣言 | 220 | terra |
| S4a 停止と開始の結線 | D/service.c (svc_dpms_on の削除・svc_rom の保存・停止の #4〜#9)、D/quiesce.c (時計の失敗の枝、§3-3)、tools/tests/gf4_c2svc_host.c・gf4_c2stop_host.c・新 gf4_hwstate_host.c | S1〜S3 | -90/+110、試験 +250 | sol |
| S4b suspend・restart の結線 | D/recover.c (rec_dpms_on の削除)、tools/tests/gf4_c2rec_host.c、tools/tests/test_gf_display.py (変異の差し替え) | S1〜S3 | -50/+10、試験 +60 | terra |
| S5 CONSOLE_DUAL | `display_dual_dpms` を外す (§4 の 9 か所) | S4a・S4b の実機の受入 R5 | -50 | terra |
| S6 文書 | §8 の 2 文書。docs/design/hwdev/gf/display/ に hw_state.md・hw_load.md・hw_pll.md・hw_fonts.md・enc_state.md | S4a・S4b | — | PM |

- S1 の宣言 (hw_state.h) を先に入れれば、S1b・S2・S2b・S3 は並列にできる。
- 手本がある移植なので PORT_FLOW に従い、宣言・コーディングの段ではビルドせず、S4a の後にまとめてコンパイルする。
- レジスタの読み書きは、各ファイルに static の index 補助を置く既存の型にする (D/dpms.c:21-30 など)。

## 7. 実機の受入 (Ra266、31kHz)

1 画面 (CONSOLE_DUAL=0):
- **R1 (A2)**: 起動 → GUI → CUI を 3 回。
  - 毎回 98 に戻り、毎回 GF の机が出る (カメラ)。
  - 2・3 回目の `[gf4] detect state` が 1 回目と同じこと。除くのは、restore が強制するビット (SR01 bit5、CR11 bit7)、CR21 (1 回目は fa、restore の後も fa の筈)、ENGINE_CTRL の I2C bit。
  - PLL も 1 回目の 51104/80000e02 に戻っていること。
  - 違う欄が出たら、その欄の restore の漏れとして記録する。
- **R2**: `gfx2_rh` を 2 回続けて回し、PASS と描画を確かめる (カメラ)。
- **R3**: GUI 中に F5 の suspend/restart (性能段の切り替え、feature5_links.c:152-165) と、診断の STOP→START (command_test.c:195-213) を各 1 回。机が戻ること。(STANDBY のままかの確かめは外した。GUI の間に STANDBY に入れる口が無い、ユーザー決定 2026-11-12「R3 から外す」。)
- **R4**: 停止の時、98 に替わる前に変な表示が一瞬出るかを見る (カメラの連写、記録だけ)。
- **R5'**: 診断の保存 (diagnostic_saved) を持ったまま停止 → 開始 → 診断の restore で画が戻ること。シリアルに `fonts skipped` が出ること。

2 画面 (CONSOLE_DUAL=1、右に GF の D-SUB):
- **R5**: CUI → GUI で右が点く → GUI → CUI で右が**無信号 (黒、LED 橙)** → もう一度 GUI で点く。これを 3 回。S4 の後 (S5 の前) と S5 の後の両方で行う。
- **R6**: gshell の異常終了の道 (`geforce_display_failed` → 停止) でも R5 と同じになること。
- **R7**: R3 を 2 画面でも 1 回。
- **R8**: CONSOLE_DUAL の A6 の残り (98 の記録・追記) が変わらないこと。

## 8. 文書の書き換え (新しい文)

### 8-1. docs/decisions/realhw/GF_FEAT4_DESIGN.md

- **:59** → 「- 停止順: 層4 (flip受付停止・進行中処理を完了/打切り→cursor hide→エンコーダーの DPMS OFF (`drm_helper_force_disable_all` 相当)→エンコーダーの restore (V/disp.c:196-197)→CRTC の restore (`nv_crtc_restore` V/crtc.c:692-706 = `nouveau_hw_load_state(saved)` V/hw.c:827-835))→層3 fini→VGA フォントの復元 (V/disp.c:202、F3 fini の fence の後、diagnostic_saved の間は保留)→gf2_stop (drain→channel停止)（内部でF1 stop）→冪等foundation_stop cleanup→quiesce rows (局所復元)→route_to_98。restore の値は module で最初の devinit の直後に 1 回だけ保存した値 (V/disp.c:245・297-302、D/hw_state.c の static)。停止の道で DPMS ON を書かない (手本に無い、ユーザー決定 2026-11-12)。F1 stop後はVBLANKを待たない。失敗時は既存の所有保持/復元規約に従う。」
- **:561** の該当部分 → 「startはF3 ready・epoch→台帳移管→D0/limits値取得 (module で最初の devinit の直後に表示の保存: フォント→crtc→enc)→detect→既定head0表示/IRQ→公開。2 回目以降の開始は保存しない。stopは受付閉→pending収束（不能なら表示停止まで保持）→cursor hide→エンコーダー DPMS OFF→enc_restore→crtc_restore (保存値に戻す、DPMS ON は書かない)→窓close→F3 fini→フォント復元→F2 stop→foundation cleanup→局所quiesce→予約解放。」
- **:655** → 「suspendは受付閉→F4前処理（pending収束/不能時hold保持、cursor hide）→窓close→gf3_service_fini(s,dl)→gf2_stop→OFF確認→invalidate→legacy->owner->foundation_stop(legacy)を直接呼ぶ冪等foundation cleanup（局所restore/quiesce rowsは呼ばない）。suspend は DPMS も表示の restore も書かない (nouveau_display_suspend → nouveau_display_fini(suspend=true)、V/disp.c:56-96)。予約・snapshot・state・相互借用を保持し、通常停止用の予約解放/初期表示への局所復元を行わない。F5は表示保存・画像退避後に呼ぶ。saved_closed時の物理blank/hideは suspend が書かないのでそのまま残る。」
- **:656** の末尾に足す → 「restart は表示の保存をし直さない (保存は svc_rom の 1 か所だけ。restart の未初期化の枝は、restart が hold_saved → suspend → ready → initialized を要するので到達しない)。理由: 実機 (2026-11-12、S0) で 2 回目の開始の値が前の停止の後の GUI の mode (CR17=c3・VPLL 21a07) だったため。OS32 は 2 回目以降に devinit を走らせないので、nouveau の init・resume の保存し直し (V/disp.c:112-121) を写すと GUI の mode を保存してしまう。」
- **:722 (N3)** → 「- N3 (P3) suspend の前の値: STOP→START で戻す論理 head・DPMS・hold・IRQ は、suspend の前処理 (pending の収束・cursor hide) より前に `hold_saved` へ取る。」
- **X4 (時計の失敗) の文**に足す → 「時計が読めない gf_display_stop でも、期限の要らない restore (DAC・TV の DACCLK と TV の state、crtc_restore) は下の停止の前に行う。エンコーダー OFF・TMDS の script・フォントの復元は期限か fence が要るので飛ばし、記録する。」
- **valid の文を新しく置く** → 「表示の保存値は module の寿命で 1 つで、svc_rom の devinit の直後に 1 回だけ取る。保存に失敗しても開始は続ける (devinit の acquire は 1 回しか試せない、hwdev/gf/devinit.c:175-176。手本も V/hw.c:330-334 で記録して続ける)。フォントが取れなければ fonts_valid=false でフォントだけ復元しない。crtc・enc が取れなければ valid=false で、停止は restore しない。valid・fonts_valid を落とすのは module の終了だけ。restore も production の io (l->io) を使う。」

### 8-2. docs/decisions/gui/CONSOLE_DUAL_DESIGN.md

- **:230** → 「- **CUI へ戻る時の GF の head**: 中核から DPMS を呼ばない。停止の restore が起動時の値 (devinit の後の CR17=0、無信号) に戻すので、右のモニターは無信号になる (実機 S0、2026-11-12)。GUI に戻る時は GF の開始の初期 mode (DPMS ON) が点けるので、開始の後の dpms(0) も置かない。`display_dual_dpms` は持たない。」
- **:232** の A6 の受入の項 → 「A6 の受入に『CUI へ戻ると右のモニターが無信号になり、GUI に戻ると点く』と『端末の sh の ver の結果が 98 にも出る』を足す。」
- §8 の変異の表から 'DPMS call' (test_console_dual.py:52) を削除し、A3 の通しから DPMS の確認を外す。

## 9. 残る点

**ユーザーに後で尋ねる**
- 停止・suspend の cursor hide (D/service.c:649・D/recover.c:192) は手本に無い。停止では restore が cursor のレジスタを保存値に戻すので、結果は同じ。suspend では手本は隠さない (V/disp.c:85-92 は unpin だけ)。今回は残す (PM の決定)。

**手本から外れる所** (理由は本文)
- 保存は最初の devinit の直後の 1 回だけで、init・resume の保存し直しは写さない (§3-1、S0 の値)。
- valid でないときに restore しない (§3-2)。
- フォントの復元は F3 fini の fence の後に置き、diagnostic_saved の間は保留する (§3-2 #9)。
- PCRTC_INTR_EN は F1 に任せる (§2-1 #6)。
- quiesce の PCRTC_START の二度書きを残す (Q4。quiesce は F4 の無い経路と共有)。
- 開始の側で、使っていないエンコーダーを OFF にしない差を残す (§3-2 #4)。

**確かめずに写す所**
- フォントの BAR1 の窓は 0〜64KiB で、plane ごとに SR02/GR04 を切り替えて 4 回読み書きする (V/hw.c:289-307・348-358)。NV44A の BAR1 の線形の窓で plane の選択がどう効くかは確かめず、手本どおりの処理として写す。

---

## PM メモ (2026-11-12、第 1 回レビューの後) — 第 2 版で反映済み

- A1 → §0-1・§3-1 (D/hw_state.c の static)。
- A2 → §1-1 の読み方・§3-1・§7 R1。
- A3 → §3-5 の PLL。
- A4 → S0 は 7eee4b40 で直し済み (§1-1)。
- A5 → §3-1 (保存の順)・§3-2 #9 (diagnostic_saved)。
- A6 → §3-2 #4。
- A7 → §3-5 の 3・§5-2。
- A8 → §6 の S4a・S4b。
- A9 → §3-1 の valid・§3-2 の SR01 bit5・§3-3・§8-1。

## PM メモ (2026-11-12、第 2 回レビューの後) — 第 3 版 (確定) で反映済み

- B1 → §3-1 の「valid と失敗の扱い」・§3-2・§5-2・§8-1 の valid の文 (保存の失敗で開始を失敗にしない、fonts_valid を分ける)。
- B2 → §3-1 (保存は svc_rom の 1 か所、restart の未初期化の枝は到達しない理由)・§5-2・§6 S4b・§8-1 :656。
- B3 → §3-1 (エンコーダーの保存は c->dcb.routes/nroutes、svc_feature5 の前)・§5-2 の変異。
- B4 → §3-1・§3-2 (restore も l->io)・§5-2 (g) と変異。
- B5 → §3-5 の NV44A の値・§5-2 の head ごとの PLL の期待値と変異。0x1584 は DI/nv04.c:120-137 の計算で head 0・1 とも書かない (head 0 も shift=8>4 で -4)。

## 実装の後の追記 (2026-11-12、S4a、PM 決定)
- module の終了での `gf4_hw_state_reset` の呼び出しは置かない。geforce.mod の口は initialize・get_probe_decls だけで、kernel が動いている間 module を外す道が無い (geforce_body.c・tools/module_schema.py)。valid・fonts_valid は kernel の寿命で 1 つになる。reset 自体 (ページの返却・valid の解除) は host 試験で確かめてある。

## 実機の受入の結果 (2026-11-12)

Ra266・31kHz・main d93f0279。証拠はノートの ~/os32-tmp/evidence/2026-11-12/gf-stopref/。S5 (`display_dual_dpms` を外す) は main への取り込み待ちなので、R5 は S4 の後・S5 の前の回だけ。

| 項目 | 結果 |
|---|---|
| R1 | 合。3 回の出入りで毎回 98 に戻り、右は CUI で黒、モニターに省電力の表示 |
| R2 | 合。gfx2_rh ×2・gfx2_i2・gfx2_i1 が PASS |
| R3 | 未実施 (起こすコマンドが分からない)。延期の台帳 GF-STOPREF-R3 |
| R4 | 記録のみ |
| R5' | 未実施 (同上)。延期の台帳 GF-STOPREF-R5D |
| R5 | 合。2 画面で、CUI では右が無信号 (LED 橙)、GUI では点く。3 回とも同じ |
| R6 | 未実施 (同上)。延期の台帳 GF-STOPREF-R6 |
| R8 | 合。CONSOLE_DUAL の 98 の記録と、端末の sh の ver が左に出る |

R1 の `[gf4] detect state` の比べ:
- 前の版で 2 回目に残っていた GUI の値 (cr17_0=c3・vpll1=21a07 など) は、1 回目の値に戻った。
- 2 回目以降も 1 回目と違って残った欄と、その説明:
  - **PLL の b2 (e02→80000e02)・ext (1f0001f→11f0001f)**: 手本どおり。`setPLL_double_highregs` が pll2 に 1<<31 を常に立て (nvkm devinit/nv04.c:209)、single stage の 0x680580 の bit を立てる (同 :184-195) ため。
  - **ec1 (I2C の bit)**: §7 の R1 で除くと決めた ENGINE_CTRL の I2C bit。
  - **cr44 (3→0)**・**cr11 (20/ab)**: 記録した。結果 (右が無信号・98 に戻る) には効いていない。
  - **cr1a_0 (同じ起動の 2 回目の開始で 2c)**: 起動後の最初の停止で、エンコーダーの復元の TMDS の ROM スクリプトの後始末 (hwdev/gf/bus.c の gf_production_restore) が起動時に記録した CR1F のロックを書き戻し、続く crtc_restore の拡張 CR (CR1A・CR21・CR44) が捨てられていた。停止で CR1F の解錠を crtc_restore の直前に取り直す形に直した (main 9a7e2739、手本 dispnv04/crtc.c:692-706・devinit/base.c:94,117 は解錠のまま restore する)。実機 (2026-11-12、Ra266) で同じ起動の中の 3 回とも cr1a_0=3f、2 画面も合。cr44 (3→0)・ec1 (I2C の bit)・cr11 は §7 の R1 で除く欄。

## 確定の後の追記 (2026-11-12、停止・suspend の cursor hide、ユーザー決定)
- 停止 (D/service.c の gf4_service_stop の cursor hide、:695) と suspend (D/recover.c の rec_suspend の cursor hide、:134) でカーソルを隠す処理は、手本に無い (nouveau の nv04_display_fini、V/disp.c:56-96 は割り込みを止め、suspend の時は FB とカーソルの BO の pin を外すだけで、カーソルを隠さない)。
- ユーザー決定 (2026-11-12、問い「止めるときにカーソルを消す処理をどうするか。手本どおり外す (起動時の値への復元でカーソルも元に戻る) / 今のまま残す」): 「手本どおり外す」。
- 決定: 停止と suspend の両方から cursor hide を外す。停止ではカーソルのレジスタも crtc_restore が保存値に戻す (§3-2)。suspend はレジスタを書かない (§3-4) ので、カーソルは suspend の前の状態のまま残る (手本と同じ)。
- 実機の受入: 1 画面と 2 画面 (CONSOLE_DUAL=1) で GUI→CUI を各 2 回。CUI に戻った後の右に、カーソルの残りが出ないこと (2 画面では右は無信号)。GUI に戻った時にカーソルが出て動くこと。
