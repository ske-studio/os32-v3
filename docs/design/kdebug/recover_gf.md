# kdebug/recover_gf.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md・GF6200_4A_S2_LAYOUT.md・GF6200_VPLL_PROBE.md・HWDEV_DESIGN.md・REALHW_RECOVER_ADDENDUM.md・REALHW_RECOVER_ADDENDUM_R2.md・REALHW_RECOVER_DESIGN.md・SLIM_HWDEV_DESIGN.md (D1 で移した)
ソース: `kdebug/recover_gf.c`

## GF6200_4A_DESIGN

| 順序 | 操作・検査 | 失敗時 |
| --- | --- | --- |
| A1 承認後 BAR測定 | 0:13.0 の Command16・BAR0〜2を保存。Commandの I/O/Mem/BusMaster を0にし生読み確認→BARを1本ずつ FFFFFFFFh、マスクを読む→直ちに元値を戻し確認 | cleanupで全BARを復元してから Command=0102h（BMを復元しない）。復元不一致なら decodeを止めたまま異常終了 |
| A1 終了 | memory32 の `size=~(mask & fffffff0h)+1`、非零・2冪・整列・加算溢れ無し。32bit型のみ許可、64bit/I/O型は別設計まで拒否。割当て区間を他装置・bridge窓と照合、Commandを0102hへ（MemとSERRのみ、BusMaster=0を生読み確認） | BARの再割当て・ROM BARのenableはしない。Mem無効中はPBUS鏡にアクセスしない |
| A2 G1前倒し | **PBUS BAR0+1854h←0**、PBUSとcfg54hを読み0、Command=0102h・BM=0を確認。98テキストが表示され更新できることをユーザーが確認 | 以後のscript実行を禁止。強制で54hを1に戻して試さない |
| A2 前処理 | 1850h=0・PMC割込み=0を確認。PMC.ENABLE bit0 を立て、head0を選択、必要な拡張CRを解錠。対象値と許可行を実行前manifestに含める | readback不一致なら止める |

- 前処理の根拠: `/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/devinit/nv04.c:405–414`（PMC bit0・owner）、`/home/hight/os32-tmp/refs/geforce6200/xf86-video-nv/src/nv_hw.c:33–48`（CR1F=57h、CR11 bit7クリア）。元の index/owner/lock を記録する。

- PM補足: script0はhead1/0のSR01 bit5を立て、RESETでPMC200h、NV_REGでstrap101000hを書き換える。script3はPRAMIN70FFF0hを書き、SUB8がhead0へ戻した後E81EでSR01 bit5を落とす（head0の画面オフ解除）。全てwrite-setに含める。N1のユーザー決定によりE81Eだけを許可し、cfg50h=0・54h=0（PBUS1850h・1854h）の読戻しと保持を前後で確認する。main3 の DRAM 初期化（MACRO0・REPEAT 10×MACRO2・SUB8・100210=80000000h・COMPUTE_MEM・1700への写し）を省略せずA4へ進む。他の表示有効化は静的拒否を維持する。

- **PM 決定（R10・別名不応答）**: 54h=0 で別名が応答しなければ A2 を停止し共通終端へ進み、4a を保留して設計し直す。54h=1／I/O decode 有効化で迂回しない。

- **PM 決定（R10・1854h 書込み）**: 実 ROM に該当書込みは無い。別 ROM/展開 write-set に現れた場合の条件付き記録とし、descriptor の phase・値を照合して非零または承認外なら静的拒否する。命令の黙示改変・許可拡張はしない。

- 根拠: nouveau (MIT、drivers/gpu/drm/nouveau/nvkm/subdev/devinit/nv04.c の setPLL_double_lowregs、NV44 は devinit=nv1a・pll_set=nv04) の MEM PLL の列は、4600h を書込みの前に読んで bit31 を立て、係数と制御の後に保存値へ戻す — OS32 の kdebug/gfinit_pll.c:219・237 と同じ。nouveau は 4600h を書いた後に読み戻さない。envytools の rnndb (MIT) に 4600h の定義は無い。bit31 の意味と読み戻せるかは未確認 (WO とも断定しない)。

- 決定: 4600h の書込みの列 (bit31 を立てる・保存値へ戻す) は変えない。**4600h の 2 規則 (substep 12020h と 1202Bh、表の tools/tests/recover_tables_oracle.c:557・tools/tests/recover_tables_oracle.c:568) だけ verify mask を 0 にする** (照合をしない、生成元から直し、生成物・承認表 tools/accept/gf_main.md の 4600h の行を同期)。7FFFFFFFh にはしない (下位 31bit の読み戻しの根拠も無い)。verify=0 の成功は PLL の lock や VRAM の成功を意味しない

> [要確認] 現行は診断も hwdev/gf/runner.c を使い、main 境界の 5 node を生成する。原文の静的展開・ordinal・verify 規則を現行実装と同一とは扱えない。

- 決定 (Opus のレビューの案 B): **SUB7 (MEM、12020h〜1202Eh) と SUB6 (CORE、12000h〜12005h) の書込み 21 本の verify mask をすべて 0 にする**。nouveau (MIT、nv04.c の setPLL_double_lowregs) はこれらを書いた後に読み戻さず、rnndb にも読み戻せるビットの定義が無い。実機の 1 回の観測で一部の照合を残す案は、値の組が変わると次回また止まりうるので採らない。書込みの列・値・順・保存値の復元は変えない。

- 根拠の集め方: 書込み先はどれも read_safe=1 (表の tools/tests/recover_tables_oracle.c:612-616・tools/tests/recover_tables_oracle.c:734-738) なので、verify が 0 でも kdebug/recover.c:1854-1855 が書く前に old を読んで gf_bus の行に残す。次の同じ番地への書込みの old で、前の書込みが効いたかを止めずに後から見る (例: 12028h の old で 12023h〜12027h の後の 4020h、1202Bh の old で 12020h の bit31)。

- 失う安全: CORE (SUB6) の 6 本と MEM の全部で、書込みの直後の照合は無くなる (係数・制御・mirror・gate・4600h の復元の不成立をその場で止めない)。もとの全ビット照合でも PLL の lock・実周波数・DRAM の正常は保証していない。書込みの失敗・許可・値・順の検査・期限・終端の cleanup と、98 側 (S0) の復帰の検証 (§163-171・§195) は変えない。

- 決定: main 0〜3 で verify が非零のまま残る 10 規則のうち **9 本を 0 にする** — VIDEO PLL の 5 本 (tools/tests/recover_tables_oracle.c:572-576、C040h・680580h・680578h・680508h・C040h、SUB6/7 と同じ PLL の列で nouveau の nv04.c は書いた後に読まない)、RESET の 184Ch の戻し (tools/tests/recover_tables_oracle.c:580、PCI config の写しで 98 側の安全に効かない)、h0/h1 CR57 の戻し (tools/tests/recover_tables_oracle.c:582-583、間接 index を戻すだけで後続の選択にも 98 側にも効かない)、1700h の容量の写し (tools/tests/recover_tables_oracle.c:584、rnndb の pbus.xml で BAR1_VRAM_SIZE は align=0x100000、容量は A4 の BAR1 の試験で間接に見る)。

- **残す: h0 CR3C (tools/tests/recover_tables_oracle.c:581、COPY の写し) の verify FFh**。nouveau も CR3C を書いた後に IO_RESTRICT_* の選択の添字として読み直し、VBIOS は値が保たれる前提。COPY の後は index の選択だけで data の書込みが無く、選択の読取りも REC に値を残さないので、old による後からの観測ができない — ここを外すと組の誤りを止めずに別の組の PFB を書き、原因を辿れない。

## GF6200_4A_S2_LAYOUT

## 3. S1からの式と一時値（5件）

| 対象／W substep | 参照式・採取位置 | 実装条件 |
| --- | --- | --- |
|DFAF COPY / e37|`((old_CR3C & 0x0f) OR ((src101000 << 2) & 0xf0)) & 0xff`、e36 SELECT後、e37内R(dst),R(src)|一般式は表のsを右shift（s<80h）／左(100h−s)。shift量<32。Cの符号付きshift禁止、u32ビット演算|
|E7FB COPY_NV_REG / e492|`(old1700 & dm) OR ((shift(src10020C,s) & sm) XOR xor)`、e492内で採取。実物dm=0,s=0,sm=FFFFFFFF,xor=0|10020Cを読取元としてread_safe分類追加、書込先扱い禁止。0x1700はR(dst)用にRW・read_safeのtargetを追加する。 |

RMW を REC_EXPR に替えるのは **65 の RMW のうち e18 (zero_target 1850、出力停止の 0 書き) を除く**。e18 は REC_LITERAL (bits 0) のまま残す (出力停止は 0 を書くだけで、決まった値で縛るのがいちばん簡単で安全 — PM 決定)。

## GF6200_VPLL_PROBE

- **一条件** = 68050Ch bit9 (VPLL_PROG)。bit28 は変えない (nouveau は head A に bit9 と bit28 を一緒に立てる、dispnv04/crtc.c:91-93,153-162 — 無変化なら複合条件の説は残る)。510h は書かない。

- **列** (main 2 の pc=E623h、すべて BAR0 相対 32bit、GF_VPLL_PROBE のときだけ): ① 今の 5 本 (C040h←3C08C003h・680580h←01F0001Fh・680578h←80001003h・680508h←00040802h・C040h←3C0BC003h、段 1 の記録だけの読み戻しつき = 対照) → ② 68050Ch←00000204h (verify FFFFFFFFh) → ③ **元の 5 本を同じ値でもう一度** (C040 の切り離しを含む、nouveau nvkm/subdev/devinit/nv04.c:257-272 の書き方、記録だけの読み戻しつき) → ④ 68050Ch←00000004h (verify FFFFFFFFh)。

- **実装の範囲**: recover_gf の展開で診断のときだけ target 1 (68050Ch、read_safe=1、根拠 dispnv04/hw.c:402・481) と書込みの規則 (② 1・③ 5・④ 1) を足し、計画に ②③④ の節を足す。③ の 5 本に段 1 と同じ記録だけの読み戻し。版は 56504C04h (段 1 の 56504C03h も読めるまま)、rshell_recover.py は新しい substep を解釈。予算は書込みの本数分の行を段 1 と同じ式で足す。既定のビルドは変えない。510h の read_safe の切替・単独の読取り・access END の欄の追加はしない。

## HWDEV_DESIGN

許可表と登録式

## REALHW_RECOVER_ADDENDUM

- GF の許可表は `kdebug/recover_gf.c`に集約し、fixture の DENY_W は証拠側の既定拒否を維持する；host の ordinal→rule 対応は許可表ではない。

- CONSTANT/SELECT の保持式 `(old & keep) | value` は `write_mask=(~keep|value)&width_mask` と同じ value.mask/value.bits で表し、literal 以外は RANGE と旧値差分検査を併用する。

- RUN の substep は fixture gf_effects の0始まり ordinal とし、SELECT の8選択肢も別 substep に固定する

- read_safe は元命令が読む RMW と VGA indexed、および既定の config/PBUS/PMC 制御に限定し、未分類の literal/trigger の verify_mask は0；PLL/PFB/strap/PRAMIN は COLD とし保存値を終端再生しない。CR44/CR1F/CR11 だけ局所 RW とする。

- 固定 SIZE_BARS/PREPARE は共有 plan_node/arg/entry の実データで持ち、固定専用 opcode 0x100〜0x109 を使う；SIZE_BARS の READ node 中に測定値を recover_bar_probe へ渡す。正常BAR再生だけは valid snapshot の identity EXPR、異常時は RESTORE/SAVED とする。

- profile/固定plan は不変 template とし、所有者が checked tick 予算と layout を添え、profile CRC と plan.profile_crc を設定する。予約は BAR1 先頭1024 byte のみで、VRAM W/容量試験の未実装式を一括許可しない。

- N1は main3/E81E/head0 の DATA rule に4対象の前後guardとSELECT・PREPARE停止2件の依存を置く。

- T2の予算設定関数 recover_gf_profile と衝突するS1の不変許可表は recover_gf_template に改名する

- COPY 2式は GF の登録EXPR、採取3件の identity は既存の登録identity式を共用する

- COPY2行と非PLL一時再生3行は既存EXPR/capture契約により本番ruleへ追加する；1700hのCOPY宛先はCOLDとし、実行中の一回消費と終端再生禁止を維持する。

## REALHW_RECOVER_ADDENDUM_R2

- N1専用ruleはRUN/main3/pc=E81E/SR01 bit5解除のみ。headは展開planで確定し、他bitは原命令のmaskと値制約を満たす。別pc/mainの表示有効化は拒否する。

- N1 guardは生PCI cfg50/54とBAR0 PBUS1850/1854を各32bit、mask=0xffffffff、expected=0として直前/直後に確認する。どれか不一致・読取失敗なら後続scriptを止めFAILED→共通cleanup。

- PREPAREで0を確立した後は全phaseで非零書込みを拒否し、各RUN_ONEの入口/出口・終端でもguardを確認する。N1直後確認を遅延しない。cfg/MMIO同値視だけで片側確認を省かない。

許可表の所有者はrecoverとし、4a側へ重複配置しない。

- 4aはdecode/整数計算・展開write-setを作りrecoverの照合APIを引くだけ。静的検査と実アクセス直前に同じ検査器を使う。ROM候補表・fixture・S5 busに別の許可表を置かない。

## REALHW_RECOVER_DESIGN

- N1 の descriptor は main3/E81E の SR01 bit5 解除だけを条件付き許可し、cfg50h/54h と PBUS1850h/1854h の0の読戻し・保持を検査する。

- BAR sizing は quiesce、PCI Command の該当 decode 停止、BAR 保存→測定→復元→照合→Command=0102h（override）の独立した有界区間。サイズ測定値を snapshot の元値にしてはいけない。

- **PM 決定（R10・別名不応答）**: 54h=0 で別名が応答しなければ A2 を停止し共通終端へ進み、4a を保留して設計し直す。54h=1／I/O decode 有効化で迂回しない。

- **PM 決定（R10・1854h 書込み）**: 実 ROM に該当書込みは無い。別 ROM/展開 write-set に現れた場合の条件付き記録とし、descriptor の phase・値を照合して非零または承認外なら静的拒否する。命令の黙示改変・許可拡張はしない。

## SLIM_HWDEV_DESIGN

## 2. 詰め方と部品の境目

- 従来の recover_target/value/rule/opcode_layout は **論理型としてそのまま残す**。仮想profile・動的planも従来型。

- GFの静的表だけ別の格納型にし、inspectで先行確保する既存g->compiledの配列へ全表を展開する。別の全表用BSS・確保領域は作らない。

- 格納型はrecover_gf.c内だけに置く。定数は#define、enum・bitfield・packedは追加しない。recover.c・recover.h・共有profile型は不変。

- 通常の4 B整列を使う。下記サイズは i386 の STATIC_ASSERT で固定し、予約欄は生成時0。

- 符号化前に非NONEのpc<0xffff、main/expr<0xffを検査し、将来の追加値との衝突を生成時エラーにする。

```c
#define REC_TABLE_NONE8  0xffU
#define REC_TABLE_NONE16 0xffffU
struct recover_rule_small {                 /* 56 B */
    u32 substep, write_mask, verify_mask, mask, bits, min, max;
    const u32 *allowed, *requires;
    const struct recover_guard *guards;
    u16 id, key, pc, allowed_count;
    u8 phase, main_id, mode, expr_id, flags, requires_count, guard_count, reserved;
};
struct recover_target_small {               /* 16 B */
    u32 offset, index;
    u16 port;
    u8 space, bar, head, width, access, read_safe;
};
struct recover_layout_range {               /* 20 B */
    u32 first, last;                        /* inclusive substep */
    const u32 *kinds;
    u16 opcode, ordinal;                    /* 展開後の先頭添字 */
    u8 argc, dir_substep, flags, reserved;
};
```

| 欄 | 符号化・変換 |
| --- | --- |
| rule id/key | u16、順序もIDも保存。id=添字への暗黙化はしない |
| rule pc/main_id/expr_id | pc は u16、main/expr は u8。NONE のみ専用値へ変換し、読む時 u32 NONEへ戻す |
| rule substep | u32 のまま。0x12044 を u16 へ切り詰めない |
| phase/mode/flags/count | 現分布の範囲でu8、allowed_countはu16。生成時に収容範囲を検査 |
| mask/bits/min/max/write/verify | 全てu32。0xffffffffも0もそのまま。min=0の省略もしない |
| target index | NONEを含むu32のまま。offsetもu32、portはu16、他はu8 |
| layout | opcode u16、substep端点u32、ordinal u16、dir NONEだけ0xffに符号化 |
| requires/guards/allowed/kinds | ポインタはそのまま、参照配列の要素型・順序・NULLを保存。プールID化しない |

- exprs の kinds と eval 関数ポインタ、guards 本体、requires 本体も変更しない。空欄を削って意味を補わない。

- 範囲化は元表の隣接行だけ。`ordinal[i+1] = ordinal[i] + last-first+1`、先頭0、総和1039。

```c
/* recover_gf.c：以下は処理骨格。全て成功0、容量・範囲不整合は-1。 */
int recover_gf_profile_expand(struct recover_profile *p, struct gfinit_plan *buf) {
    struct recover_profile next = recover_gf_template; /* 固定metadataのみ */
    if (!buf) { *p = next; return 0; } /* release用、3表pointer/countは0 */
    /* 各件数 <= GF_PLAN_* を確認。rules/targets全欄をbuf配列へ展開。
       rangeを元順で走査し、ordinal == 書出し添字を検査。
       first..lastを+1で展開、dirの0xffはRECOVER_NONEに戻す。
       容量・範囲・展開総数を確認してから次のpointer/countを公開する。 */
    next.rules = buf->rules; next.rule_count = REC_GF_RULE_COUNT;
    next.targets = buf->targets; next.target_count = REC_GF_TARGET_COUNT;
    next.layouts = buf->layouts; next.layout_count = REC_GF_LAYOUT_COUNT;
    *p = next;
    return 0;
}
/* 件数定数はgenerated include内の#define（578/154/1039）。
   recover.cのrule_by_id/layout_for、CRC、重複検査は従来のまま。
   例：const struct recover_rule *w = &p->rules[i]; */
```

- templateの既存extern宣言は残し、実体は3表pointer/countを0にしたmetadata雛型とする。直接bind/decodeに渡さない。

- 展開はbufの3配列だけへ書く。他のplan/deps/saved状態を一括memsetしない。NULL bufは解放後reset専用で、利用可能なprofileを返す意味ではない。

- 補助配列と式関数は recover_gf.c に残し、範囲の結合には kinds の参照名も一致させ、ポインタの参照先と順序を保存する。

- 範囲は書込み前に私有の検査関数で ordinal・端点・容量・展開総数を検査し、inclusive 長は差分で比較する；最後の substep を書いた直後に終了して u32 末尾の加算折返しを避ける。
