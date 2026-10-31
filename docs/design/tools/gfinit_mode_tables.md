# tools/gfinit_mode_tables.py — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `tools/gfinit_mode_tables.py`

分割/複製無し。唯一の生成元 `tools/gfinit_mode_tables.py:29` を維持。blank/DPMS の modeset 順序もここに保持。

## D1 で移した設計 (GF6200_4C_DESIGN)

| Q | `nv04_dac_prepare`→DACCLK off→`nv04_dfp_disable`：FP_TG が動作中なら OFF と50ms、stateのLCD route解除（`V/dac.c:364–373,426–472`, `V/dfp.c:93–110`）。Q→P後、mode設定の冒頭でCR21 shadowを解錠（`V/crtc.c:659–660`, `V/hw.h:287–305`）。 |
| P | `nv_crtc_prepare`：owner=0、DPMS OFF、blank、PCRTC_CONFIG=NON_VGA、RAMDAC900 bit16解除（`V/crtc.c:708–728`）。DPMS の SR00 reset→SR01 bit5→10ms→CR17 bit7→reset解除→CR1A sync mask は同:175–234。 |
| S | `nv_crtc_mode_set_vga` は timing・SR/GR/AR・pitchをstate化（`V/crtc.c:237–449`）。Curie はSEL_CLKをPLL計算前にも書く（同:662–667）；`nv_crtc_save` の初期値は saved_SEL_CLK & ~00050000h（同:685–689）。 |
| S | `nv_crtc_mode_set_regs` は出力OFFを前提にCRTC拡張・cursor・RAMDACを生成（`V/crtc.c:452–606`）；CR45と634は計算段でも書く（同:63–88,534,600）。`nv_crtc_calc_state_ext` がPLL limits→単段判定→solver→50Ch stateを生成（同:117–162）。 |
| S | `nv04_cursor_set_offset` のCR30→31→2FとCurieの位置再書込み（`V/cursor.c:38–59`, `V/hw.h:341–349`）はcursor非表示でも実施する。 |
| D | `nv04_dac_mode_set`：選択DACCLK=head<<8\|1、他ANALOGを反対headへ、NV44AのTEST_CONTROL=00100000h（`V/dac.c:375–409`）。OR番号はhead番号ではない；OR0 offset=0、OR1 offset=2000h（同:41–51）。 |
| L1 | `nv_crtc_commit`→load（`V/crtc.c:731–749`）。`NVVgaProtect(true)` のSR00=1・SR01 blank・AR gate（`V/hw.h:194–219,238–255`）の後、`nv_load_state_ramdac` がCURSYNC→PLL→50Ch→SEL_CLK→GENERAL→630→634→TV→FP→8C0→A20/A24/A34→CTV（`V/hw.c:468–536`）。 |
| PLL | `nv40_clk_new` のpll_prog=`nv04_clk_pll_prog`（`N/nvkm/subdev/clk/nv40.c:229`, `nv04.c:49–62`）→`setPLL_double_highregs`：C040切離し→必要時580→578→508→C040復元（`N/nvkm/subdev/devinit/nv04.c:197–272`）。50Chはその後。510hの書込みはこのload経路に無い。 |
| L2 | `nv_load_state_ext`：ENGINE_CTRLはCR33より先、PVIDEO/PBUS→PCRTC→900→CONFIG→拡張CR→cursor位置再書込み→残りCR→START（`V/hw.c:665–772`）。NV44Aには旧世代用の垂直帰線待ち:746–757を入れない。 |
| L3 | palette mask/index/data を順に書く（`V/hw.c:794–807`）。`nv_load_state_vga`：MISC→SR0..4→CR11解錠→CR0..18→CR11施錠→GR0..8→AR0..14→AR gate（同:564–586）。 |
| C | unprotect（`V/hw.h:250–255`）→`nv04_crtc_do_mode_set_base` の形式/pitch/START/arbitration（`V/crtc.c:863–903`）→CRTC DPMS ON（同:749）→DAC commit/DPMS ON（`V/dac.c:412–419`）。 |
| Q | 68052C=r&~1；680848がONなら20000022と50ms；state.FP=20000022、CR33=s&~3B | §2 Q。head Bはmain0のblankを維持し、新たに表示しない。 |
| P | CR1F=57、CR44=0；SR00=1→SR01=r\|20→10ms→CR17=r&7F→SR00=3→CR1A=r\|C0；blank；600804=1、680900=r&~10000 | A2解錠を確認して再利用（`docs/design/kdebug/recover_gf.md`）、§2 P/`V/hw.c:85–124`。 |
| S | CR21=r\|FA→680524=s&~00050000；CR45=0；680634=0；CR30/31/2F=80/00/00→680300=r | §2 S。cursorの領域割当ては省き、offset=0・非表示（CR31 bit0=0）のみ。 |
| D | 68052C=1→68252C=(r&~100)\|100→680608=00100000 | §2 D。TEST_CONTROL上位senseは期待値に含めない(記録はアーカイブ)。 |
| L1a | protect→680404=02000000→C040=r&~00030000→680580=(r&~100)\|single*100→680578=(r&7FFF0000)\|80000000\|NM2\|(single?011F:0)→680508=(r&FFF80000)\|(P<<16)\|NM1→C040=退避値 | §2 PLL。既存4aの5書込みsequenceを再利用するので同値時も実施、580の条件省略も4aとの差を増やさない（`O/kdebug/gfinit_pll.c:196–210`）。 |
| L1b | 68050C=10000204→680524=s&~00050000→680600=20100130→680630=0→680634=0→680700=0→680848=20000022→6808C0=100→680A20=0→680A24=000FFFFF→680A34=1 | `V/hw.c:481–531`, `V/crtc.c:589–606`。head A単独の50Chはbit2/9/28。診断の204hを流用しない。 |
| L2a | 600860=10→8704=1→8140=0→1588=0→600810=04011100→600830=1DD→600834=1DF→60081C=s→600850=s→680900=r\|10000→600804=2 | `V/hw.c:680–710`, `V/crtc.c:496–568`。overlay停止、cursor非表示。 |
| L2b | CR19=00→1A=04→25=10→28=01→33=s&~3B→2D=00→1C=s&~20→1B=04→20=C0→47=00→9F=01→49=00→30/31/2F=80/00/00→680300=r→CR39=FF | `V/hw.c:712–734`, `V/crtc.c:388–413,494,555,578`, `V/arb.c:239–264`。FIFOはNV44A分岐のburst=4/lwm=C0をstateに先置き。 |
| L2c | CR3B=22→3C=s→41=00→45=00→4B=s\|80→52=(s+4)&FF→42=00→53=00→54=00→CR57/58[i=0..15]=i/00→59=00→5B=00→85=FF→86=01→600800=0 | `V/hw.c:736–772`, `V/crtc.c:519–575`。未計算CR49/CR58/5Bはzero state相当（`V/disp.c:223`）、POST代替の妥当性は未確認。 |
| L3a | 6813C6=FF→6813C8=00→6813C9にRGB×256。index0..7=黒/赤/緑/青/白/cyan/magenta/黄（成分00/FF）、8..255=黒 | `V/hw.c:794–807`。DAC dataは自動増分、old/readbackを付けない。paletteの値は本試験専用。 |
| L3b | C03C2=E3；SR00..04=00 21 0F 00 0E；CR11 bit7解除→CR00..18=`5F 4F 4F 83 53 9F 0B 3E 00 40 00 00 00 00 00 00 E9 2B DF 50 00 DF 0C 43 FF`→CR11 bit7設定 | `V/crtc.c:245–256,305–381`, `V/hw.c:570–578`。単なる標準VGA表の転記ではなくnouveauの+1/-1補正を適用。 |
| L3c | GR00..08=`00 00 00 00 00 40 05 0F FF`；AR00..0F=00..0F、AR10..14=`01 00 0F 00 00`、各index/data前にFF reset；AR gate復帰→unprotect | `V/crtc.c:418–449`, `V/hw.c:580–586`, `V/hw.h:194–255`。VGA実portには書かない。 |
| C | CR28=01→680600=20100130→CR19=00→13=50→42=00→600800=0→CR1B/20/47=04/C0/00；SR00=1→SR01=01→10ms→CR17=C3→SR00=3→CR1A=04→68052C=r\|1 | §2 C。CR11最終AB、CR21解錠維持、CR44=0。S0まで旧timingを再生しない。 |
