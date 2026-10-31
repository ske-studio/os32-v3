# kdebug/gfinit_op/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md・GF6200_4A_S2_LAYOUT.md (D1 で移した)
ソース: `kdebug/gfinit_op/`

### io_restrict_decode.inc

ソース: `kdebug/gfinit_op/io_restrict_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 32 IO_RESTRICT_PROG | 32 | s=(VGA[index]&m)>>shift、count個のu32から選びR32へ。11+4count B。範囲外は無書込みで記録・続行 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:621–651 |
| 4A IO_RESTRICT_PLL2 | 2 | 11+4count B、VGA選択のu32 kHzでPLL設定。範囲外は無書込みで記録・続行 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:950–978 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 32 IO_RESTRICT_PROG | 11+4n：p16@1,i8@3,m8@4,s8@5,n8@6,d32@7,値32[]@11 |

> [要確認] 現行は診断も hwdev/gf/runner.c を使い、main 境界の 5 node を生成する。原文の静的展開・ordinal・verify 規則を現行実装と同一とは扱えない。

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 4A IO_RESTRICT_PLL2 | 11+4n：32と同配置（値はkHz） |

### repeat_decode.inc

ソース: `kdebug/gfinit_op/repeat_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 33 REPEAT | 1 | 2B、実物count=10。count回反復し終端直後へ。0は静的拒否（nouveauでは本体へ落ち1回走る） | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:658–677 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 33 REPEAT | 2：n8@1 |

### copy_decode.inc

ソース: `kdebug/gfinit_op/copy_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 37 COPY | 1 | 11B、VGA←(旧&m) OR (shift(R32)&smask)。shift<80hは右、他は100h−shift左 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:740–760,531–536 |
| 5F COPY_NV_REG | 1 | 22B、dst←(旧&dm) OR ((shift(src)&sm)^xor)。10020Cは読取り元、1700が宛先 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1415–1434 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 37 COPY | 11：src32@1,s8@5,sm8@6,p16@7,i8@9,m8@10 |
| 5F COPY_NV_REG | 22：src32@1,s8@5,sm32@6,xor32@10,dst32@14,dm32@18 |

### condition_decode.inc

ソース: `kdebug/gfinit_op/condition_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 39 IO_FLAG_CONDITION | 4 | 2B、9B条件表参照、偽ならskip | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:512–528,779–790 |
| 6F MACRO | 5 | 2B、macro表+index×8の{reg,value}を1組書く。4件の範囲検査 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1727–1743 |
| 75 CONDITION | 2 | 2B、12B条件表の(R32&m)==v、偽ならskip | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:479–494,1802–1813 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 39 IO_FLAG_CONDITION | 2：条件番号8@1→9B表 |
| 6F MACRO | 2：index8@1→表8B |
| 75 CONDITION | 2：条件番号8@1→12B表 |

### pll2_zm_reg_decode.inc

ソース: `kdebug/gfinit_op/pll2_zm_reg_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 4B PLL2 | 1 | 9B、regへu32 kHzをsolverとチップ別手順で設定 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:985–996 |
| 7A ZM_REG | 42 | 9B、R32直書き。宛先200hだけbit0をOR | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1892–1906 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 4B PLL2 | 9：reg32@1,kHz32@5 |
| 7A ZM_REG | 9：reg32@1,v32@5 |

### cr_index_address_latched_decode.inc

ソース: `kdebug/gfinit_op/cr_index_address_latched_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 51 CR_INDEX_ADDRESS_LATCHED | 2 | 5+count B、CR addr0を保存、base++をaddr0へ、dataをaddr1へ書き、addr0復元 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1146–1168 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 51 CR_INDEX_ADDRESS_LATCHED | 5+n：a0@1,a1@2,base@3,n@4,byte[]@5 |

### cr_decode.inc

ソース: `kdebug/gfinit_op/cr_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 52 CR | 2 | 4B、CR←(旧&m) OR 値 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1176–1189 |
| 53 ZM_CR | 9 | 3B、選択headのCRへ直書き | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1196–1207 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 52 CR | 4：i8@1,m8@2,v8@3 |
| 53 ZM_CR | 3：i8@1,v8@2 |

### zm_reg_sequence_decode.inc

ソース: `kdebug/gfinit_op/zm_reg_sequence_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 58 ZM_REG_SEQUENCE | 3 | 6+4count B、baseから4ずつ増やしてu32直書き。実物count=3/4/4、11レジスタ | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1281–1300 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 58 ZM_REG_SEQUENCE | 6+4n：base32@1,n8@5,v32[]@6 |

### index_io_decode.inc

ソース: `kdebug/gfinit_op/index_io_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 62 ZM_INDEX_IO | 2 | 5B、indexed VGA直書き | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1441–1453 |
| 78 INDEX_IO | 8 | 6B、VGA←(旧&m) OR 値。indexは1Bとして復号 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1853–1868 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 62 ZM_INDEX_IO | 5：p16@1,i8@3,v8@4 |
| 78 INDEX_IO | 6：p16@1,i8@3,m8@4,v8@5 |

### reset_nv_reg_decode.inc

ソース: `kdebug/gfinit_op/reset_nv_reg_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 65 RESET | 1 | 13B、force。184Cの0F00を退避/クリア→regへdata1→10µs→data2→184C復元→1850 bit0クリア | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1477–1497 |
| 6E NV_REG | 36 | 13B、R32←(旧&m) OR 値 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1709–1720 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 65 RESET | 13：reg32@1,d1@5,d2@9 |
| 6E NV_REG | 13：reg32@1,m32@5,v32@9 |

### sub_decode.inc

ソース: `kdebug/gfinit_op/sub_decode.inc`

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 6B SUB | 3 | 2B、script表のu8番号へcall、DONEでpc+2へ。skip中は呼ばない | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1663–1683 |

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 6B SUB | 2：番号8@1 |
