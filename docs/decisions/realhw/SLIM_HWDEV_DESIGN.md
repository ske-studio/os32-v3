# SLIM_HWDEV — recover_gf の表を詰める (hwdev.mod の縮小) — 設計

> 状態: **現行 (2026-10-25)** — 確定 (astra 起草・Opus 2 往復・PM が条件 B1〜B4 を反映)。段の状態は票 TASK_SLIM が持つ。
> 出所: ~/os32-tmp/run/slim-hwdev/DESIGN.final.md を 2026-10-25 に写した (往復の版と REVIEW はそこに残る)。本文の r2 の見出しと『r1→r2 の変更』は往復の記録として残す。

# SLIM_HWDEV r2 — S2 詳細設計（2026-10-25、実装 astra）

## r1→r2 の変更
- A1：bind後のnode_layout/node_writeと規則間ポインタ差を保つため、安定した従来型配列へ全表を先に展開する。
- A2：PM決定のhwdev内展開を採用し、compiledのinspect先行確保・160 KiBのページ消費・解放手順を明記する。
- A3：製品の直接参照は維持し、templateを読むhost試験6ファイルとownerスタブを展開済みfixtureへ移す。
- A4：accessorと重複検査中の展開を撤回し、従来の直接参照・検査コストを保つ。
- A5：hostは意味比較、i386 -Os機械語はゲストT4BのRUN経路で確認する。
- A6：decodeはtrial_begin前なので、decodeの期限内完走を受入から外す。
- A7：通常版/R1でgfinit_decode.oを共有するため、-Os指定は1か所だけにする。
- A8：oracle比較は意味の比較とし、hostのポインタ幅は問わず、製品のsizeofだけi386で検査する。

## 1. 実物の確認
- 対象は TASK_SLIM §1 #1・#6、§2 S2。単一利用者・単一タスク・協調切替、整数演算。
- recover の許可・拒否・復帰順・期限・ログを保存する。権限分離や入力防御は追加しない。
- パスは `/home/hight/os32-v3/` 相対。本設計作業は同リポジトリを変更していない。
- GCC 13.2.0、i386-elf-size / gcc-nm で build/out を測定。以下の KB は曖昧さを避け B / KiB で記す。
- 表の分布は recover_gf.o の .rodata を Python で構造体の欄ごとに集計した。
- 現ソースを同じ ABI・-O2・unit stamp 付きで一時領域へ再コンパイルし、.rodata のバイト一致も確認した。

| 対象／根拠 | 実測 | 票との関係 |
|---|---:|---|
| build/out/hwdev.mod | 134,432 B = 131.28 KiB | 131KB と整合 |
| build/out/kdebug.bin | 111,172 B = 108.57 KiB | 109KB と整合 |
| recover_gf.o / recover_gf.c:13,272,932 | 82,427 B | 82KB と整合（十進表記） |
| rules / recover.h:199 | 578 × 84 = 48,552 B | 件数・サイズ一致 |
| fixed_layouts / recover.h:214 | 1,039 × 24 = 24,936 B | 件数・サイズ一致 |
| targets / recover.h:191 | 154 × 36 = 5,544 B | 5.5KB と一致 |
| gfinit_decode_rom / gfinit_decode.c:632 | 11,839 B、.o 全体 20,739 B | 11.8KB と一致 |

- 食い違い **2 件**（単位の丸めは数えない）。
  - ① TASK_SLIM.md:18 の「欄が全部 u32」：recover.h:196,202–203 の allowed / requires / guards はポインタ。i386 では同じ 4 B。
  - ② TASK_SLIM.md:24 の kdebug が起動 FD の空きに効くとの記述：現在は build/packages.yaml:116–119 で両部品を FD から除外。
- 以下は `範囲（0 件数 / NONE 件数）`。分母は rules=578、targets=154、layouts=1039。件数/分母を割合の正確な表現とする。
- マスク・bits・max の 0xffffffff は通常値なので、以下の F は「値が全ビット1の件数」、NONE と区別する。

| rules の欄 | 値域・割合 |
|---|---|
| id / key | 0..577（1/578 が0）、2..153（0なし）；全行 id=添字 |
| phase / main_id | 0..5、5種類（8/578 が0）；0..3（141/578 が0、34/578 がNONE） |
| pc / substep | 0x5df2..0xe824、34/578 がNONE；0..0x12044、544種類、5/578 が0 |
| write_mask | 0..0xfc000000 と F、27種類、0=6/578、F=360/578 |
| verify_mask | 0..0xffff と F、6種類、0=516/578、F=40/578 |
| value.mode / mask | mode=0..4 の4種類、0=482/578；mask=0..0xfc000000 と F、0=52/578、F=326/578 |
| bits / min / max | bits=0..0xf0000101 と F、0=155/578、F=3/578；min は578/578 が0；max={255,65535,F}、F=398/578 |
| expr_id / flags | expr=1..8 またはNONE、NONE=497/578；flags=0..4 の4種類、0=563/578 |
| allowed / count | 全578行 NULL / 0 |
| requires / count | NULL=480/578；count={0,1,3}、参照延べ100件、参照ID=0..568 |
| guards / count | NULL=577/578；唯一の4件は key=0..3、mask=F、expected=0、when=3 |

| targets の欄 | 値域・割合 |
|---|---|
| space / bar | space=0..5 の3種類、0=6/154；bar は154/154 が0 |
| offset / head | offset=0..0x70fff0、0=39/154；head={0,1}、0=139/154 |
| port / index | port={0,0x3c4,0x3d4}、0=115/154；index=1..0x86 またはNONE、NONE=115/154 |
| width / access / read_safe | width={1,2,4}；access={0,1}、0=6/154；read_safe={0,1}、0=67/154 |

| layouts の欄 | 値域・割合 |
|---|---|
| opcode / substep | opcode=0x32..0x109 の37種類；substep=0..0x2e832、0=4/1039 |
| argc / kinds | argc=0..7 の7種類、0=5/1039；kinds=NULL は5/1039、非NULLは9種類の配列参照 |
| kinds の内容 | 延べ1797要素：IMM(0)=1716、READ(1)=54、SAVED(2)=27 |
| dir_substep / flags | dir=0..3 またはNONE、0=7/1039、NONE=479/1039；flags={0,1}、0=1027/1039 |

- 元順を保ち、opcode・argc・kinds内容・dir・flags が同じで substep が +1 の隣接行だけを併合すると **472行**。
- 長さ別：1×410、2×13、3×1、4×5、5×2、6×2、7×2、8×35、9×1、255×1（合計1039）。
- 最長は opcode=0x33、substep=0x11001..0x110ff。PLL 帯は種類の切替や穴があり、全帯を一括範囲にはしない。
- 任意strideなら400行になるが採らない。+1限定は順序・境界・CRCの対応を単純に保つ。
- 確定設計との対応：REALHW_RECOVER_ADDENDUM_R2.md:46–69 の target ID・規則・保存順を維持。
- REALHW_RECOVER_ADDENDUM.md:41,47 の NONE=0xffffffff、表の bind〜cleanup 間の不変性・生存期間も維持。

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
|---|---|
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
- 展開関数はrecover_gf.cで定義し、新規recover_gf_expand.hに宣言する。このヘッダはgfinit*.hからincludeしない。
- gen_hwdev_contract.py:97–101の傘はrecover.h/recover_gf.h/gfinit*.h。新ヘッダはgfinit.cとhost試験からのみ読む。

```c
/* recover_gf_expand.h：既存型の前方宣言だけ、契約の傘には入れない。 */
struct recover_profile;
struct gfinit_plan;
int recover_gf_profile_expand(struct recover_profile *p, struct gfinit_plan *buf);
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
- gfinit.c:228：inspectでclaim後にg->compiledをgf_alloc(sizeof(*g->compiled))、展開後にidentity設定・bus_init・probe・ROM/decodeへ進む。
- 確保後のprobe/ROM/decode/展開の失敗は全てgf_release_romへ集約し、追加した160 KiBとleaseを残さない。inspect済み表示は成功時だけ。
- gfinit.c:308：gf_beginではcompiled存在を確認して再展開し、identityを再設定。SIZE_BARSにも必ず展開済みprofileを渡す。
- RUNのgfinit.c:317の遅延確保は削除。gfinit_plan.c:109–113のコピーは同じ配列への自己代入となり、そのままでよい。
- gfinit_plan.c:83,102–103,167とVRAM用追記、decode/bus/exec内の表参照も従来型の直接参照のまま。
- gf_read（gfinit.c:94）はcompiledの存在だけで通知せず、`r->plan == &g->compiled->plan`も条件にする。先行確保しただけのSIZE_BARSではplan_readを呼ばない。
- gf_release_rom（gfinit.c:194–209）は既存のdetach成功を確認→全backing解放→compiled=NULL→expand(&g->profile,NULL)の順。失敗したdetachを飛ばして解放しない。
- 再展開はinspectまたは既存gf_beginのIDLE/VERIFIED門内だけ。bind後からcleanup完了まで配列の番地・内容・参照先の寿命を保つ。
- node_layout/node_write（recover.h:336–337）の保持ポインタ、recover.c:694のq-p->rulesは連続した従来配列を指すので意味が変わらない。
- kdebug.binのrecover.o（build/kdebug.mk:13）は無変更。表と展開コードはbuild/hwdev.mk:3–8のhwdev側だけ。
- gen_hwdev_contract.py:119–127のsize/align/offset/member_sizeと契約CRCを旧版と比較し不変を確認。inventory更新・seed・世代変更は不要。
- normal/R1は既存の組合せで再ビルドする。公開KAPI・wire形式・profileの意味の版・kdebug.binを変更しない。

## 3. 同値の確かめ方
- S2aで変更前の3表を固定oracle（host専用C fixture）へ保存。参照先配列も収録、元版・SHA256・抽出手順を記録する。
- oracleは圧縮生成器から生成し直さない。旧型の別translation unitとしてリンクし、製品には入れない。
- 新規 gen_recover_tables.py は oracle の機械抽出結果を入力として格納表を生成し、`--check` で再現性を検査。
- 抽出はCの初期化子を独自解釈せず、host harnessで評価して数値・参照配列を出力する。シンボル名を別に保持してC参照を生成。
- host試験はexpandで得た全578/154/1039行について、全スカラーとポインタのNULL性・個数・参照先全要素を欄別比較。
- 意味比較なのでhostのポインタ幅は問わない。製品格納型のsizeof/整列だけi386 cross compilerで検査する。
- ポインタ数値やstruct全体のmemcmpは使わない。allowed/requires/guards/kindsの意味を比較し、関数ポインタexprsは非変更も検査。
- 端点、穴、範囲間境界、先頭/末尾/展開先容量超過、NONEと0の区別も試す。全profile CRC・decode plan/hash・バスtraceも旧表と一致。
- 変異：圧縮行1個のverify_maskの1bitを反転→当該ruleの欄比較が必ず失敗すること。

## 4. #6 の結論
- build/config.mk:137 の -O2 が build/hwdev.mk:8 にそのまま使われている。-Os指定は無い。
- gfinit_decode.c:692 の expand_step 呼出が rom本体へ入り、nmに独立expand_stepが無い。expand_pllは別関数として残る。
- `-fopt-info-inline` は expand_step 内への control/write_node/le の多数の展開、rom内へのCRC展開を報告。
- 「104行だけで11.8KB」ではなく、展開先のswitchと補助処理を含んだサイズ。ソース行数の短縮を目的にしない。
- 同じGCC・ABI・include・unit stampで一時領域へ再コンパイル：-O2=20,739 B、-Os=13,589 B、**差7,150 B（6.98 KiB）**。
- -Os時のrom関数は6,258 B。関数単体の差5,581 Bより、.o全体の差で採否を判断する。
- `-O2 -fno-inline-functions-called-once` はstamp無しで20,774 B（同条件O2=20,719 B）と55 B増。単純な展開抑止は採らない。
- **直す価値あり**：gfinit_decode.oだけ末尾に-Osを付けるtarget-specific KERNEL_CFLAGSをbuild/hwdev.mkへ追加。
- R1は通常版と同じ.oを使う（build/hwdev.mk:27）ため、指定は$(HWDEV_OUT)/gfinit_decode.oの1か所で足りる。
- decodeはtrial_begin前（gfinit.c:318–320,339）。受入は結果の一致であり、decodeの期限内完走は要求しない。
- host gcc試験は意味だけを確認。i386 -Osの証拠はゲストT4Bの既存RUN経路で実decodeを通し、同一fixtureの結果/エラー/planを変更前と照合する。

## 5. 大きさの見積もり
| 項目 | 現在 → 設計値／差 |
|---|---:|
| rules | 48,552 → 32,368 B、−16,184 B |
| targets | 5,544 → 2,464 B、−3,080 B |
| layouts | 24,936 → 9,440 B、−15,496 B |
| 表3種合計 | −34,760 B（33.95 KiB） |
| 展開・初期化コード等 | hwdevだけ +1〜3 KiBを予算、nmで確認 |
| #6 | −7,150 B（表変更前の実測、最終リンクでは再測定） |
| hwdev.mod | 約91〜94 KiBを見込む（現在131.28 KiB）；relocation/align差も最終実測に含める |
| kdebug.bin | 変化0 B、同じ入力条件でバイト一致を確認 |
| 起動FD空き | **0 B**：両部品とも除外。通常/R1の部品配布・格納容量には効く |
- sizeof(struct gfinit_plan)はi386 GCCのsize probeで **163,044 B**、gf_allocのページ丸め後 **163,840 B（160 KiB、40ページ）**。
- 内訳：plan64＋entries384＋deps768＋targets27,648＋rules107,520＋layouts25,344＋exprs256＋saved_vram1,024＋末尾36 B（gfinit_plan.h:30–40）。
- ヒープ空きへの影響は0 B：gf_allocはpgalloc（gfinit.c:73–79）で既存RUNと同じ40連続ページを先取りするだけ；inspect/SIZE_BARSの保持量は160 KiB増、実機の空きは未測定なので不足時はinspectを失敗・全解放する。
- RUNのピーク確保量は増えず確保時期だけ早まる。静的表自体は34,760 B減る。profile型・plan型・呼出側スタックの配列は増やさない。
- 受入目安：hwdevで36 KiB以上減、kdebugは0 B差。未達なら展開コードと最終リンクの差を再見積もりする。

## 6. ファイルごとの変更一覧
| 区分／ファイル | おおよその変更行数・役割 |
|---|---|
| 新規 kdebug/recover_gf_expand.h | 約10：既存型の前方宣言とhwdev内展開API |
| 変更 kdebug/recover_gf.c | 表約1800行をgenerated includeへ移動、格納型約25・展開約90・metadata約10行 |
| 新規 kdebug/generated/recover_gf_tables.inc | 約1250行：578 rules、154 targets、472 rangesと補助表 |
| 変更 kdebug/gfinit.c | 約30〜50：inspect先行確保・再展開・失敗解放・plan_read条件 |
| 新規 tools/gen_recover_tables.py | 150〜220：抽出結果から圧縮・範囲化、--check |
| 新規 tools/tests/recover_tables_{oracle,host}.c 等 | oracle約1900行、比較harness約180行、抽出driver約80行 |
| 変更 tools/tests/{gfinit,gfinit_bus,gfinit_exec,gfinit_pll,gfinit_romfile,recover_gf}_host.c | 合計約60〜90：冒頭で展開済みfixtureを準備しmetadata直参照を置換 |
| 変更 tools/tests/gfinit_owner_host.c | 約20：展開APIスタブ、先行確保・失敗時解放・SIZE_BARSの回帰確認 |
| 新規 check-recover-tables-host.mk/yaml、変更既存map | 約35：host試験と全入力の登録 |
| 変更 build/hwdev.mk | 2〜4：decodeだけ-Os（生成表の追跡は既存-MMD） |
- recover.c/recover.h/recover_gf.h/gfinit*.h、契約inventory/生成物、kdebugのソースは変更しない。

## 7. 受入 → 試験・変異、段分け
| 受入 | host / ゲスト試験 | 変異1行 |
|---|---|---|
| 全欄・順序・NONE保存 | host旧新全行比較、CRC一致 | rule.main_idのNONEを0にして不一致検出 |
| 範囲の展開1039行が一致 | host全添字と端点・穴・重複の検査 | 255連番のlastを1減らし総数/欄比較が失敗 |
| 従来profile・保持ポインタ・動的追記を維持 | host SIZE_BARS bindとRUN、node_write/node_layout、VRAM追記を照合 | beginの再展開を省略し件数/traceの退行を検出 |
| inspect先行確保と解放 | host各確保/probe/decode失敗、再inspect、SIZE_BARS、detach拒否を注入 | compiledのfreeを省略しページ会計差で検出 |
| 復帰・失敗時の動作不変 | host既存失敗注入のtrace比較、ゲスト既存T4BとCUI復帰・後続echo | guard.expectedを1にし旧新trace不一致 |
| 契約・既存kdebugの口を維持 | host hwdev module/image/audit、ゲストnormal/R1でstatus・begin/end | fixtureのlayout CRCを1bit変え既存拒否を確認 |
| -Osのdecode結果を維持 | hostは意味比較、ゲストT4B RUNでi386 -Osを実行し結果一致 | decode結果の1nodeのsubstep反転を比較で検出 |
| サイズ予算・FD不変 | size/nm/stat、FD manifestと空きcluster数を前後比較 | テスト用集計へ旧rulesサイズを戻し予算超過を検出 |

- 各段はastraの1回、実装＋当該候補検査を2時間以内に収める単位。段内のソース変更後の結果だけを採用する。
| 段 | 作業と候補検査（MUTATE=0） | 時間目安／並列 |
|---|---|---|
| S2a | 旧表oracle、抽出・比較harness、map登録；新host、check_select --lint | 60〜90分／単独 |
| S2c | 旧論理表からの展開APIを導入、inspect先行確保・解放・全fixture移行；owner/gfinit/recover候補 | 90〜120分／S2a後、並列不可 |
| S2d | rules/targets圧縮とlayouts範囲化を展開API内へ接続；全欄同値・gfinit・hwdev候補 | 90〜120分／S2c後、並列不可 |
| S2e | decodeだけ-Os、総量計測・最終ビルド／全差分候補 | 60〜120分目標／最後に直列 |
- r1のS2b（accessor移行）は廃止し、段IDは欠番として残す。S2c時点では旧表を配列へ写すだけで寿命・失敗経路を先に検査する。
- 新includeの通常依存は既存-MMDで追跡し、build/hwdev.mkの編集は-OsのS2eにまとめる。
- S2a/c/dはmake kernelと変更部品、追加・変更host、lint。S2eでmake all・全S2差分候補（MUTATE=0）・既定の静的検査をまとめる。
- 主候補は check-recover-host / check-recover-gf / check-recover-t3 / check-recover-virtual / check-gfinit-host。
- 境界候補は check-kdebug-init-host / check-kpeek-host / check-hwdev-{audit,module,image}-host、新check-recover-tables-host。
- 実際の候補は段ごとの全変更ファイルを check_select.py --select --files に渡して確定。未登録の新規入力を放置しない。
- **実測の選択結果：build/hwdev.mk を含めると full → check の全候補**。限定候補だけで最終受入済みにしない。
- S2eの全候補が2時間枠に収まらない場合は、-Os実装と最終統合検査を別回S2fへ分け、結果を省略しない。
- 変異は上表を登録しPM側の最終検査で実行。今回の設計作業では製品試験・ゲスト試験は未実施。
- 6本の既存host試験は寿命を持つgfinit_planバッファへ1回展開し、以後そのprofileを使う。部分コピーするlayouts/targetsもその配列を借りる。
- `rg recover_gf_template`で試験・道具を再監査し、metadata雛型を直接decode/bindへ渡す箇所を残さない。製品の従来配列参照は維持する。
- guestのshell/wire形式は変更しない。実機操作は既存受入手順に従い、S2の同値確認のために新しい装置書込み試験を増やさない。

## 8. 未決
- ユーザー判断が必要な未決は **0件**。推奨は上記のGF限定格納圧縮＋decode単体-Os。
- コード量・最終サイズ・ゲストdecode結果は実装時の測定項目であり、未測定を合格扱いしない。

反論：0件（A1〜A8とPM決定を反映）。

## 確定 (2026-10-25、Opus 5.5 第 2 回の条件付き承認、PM が反映)

- B1 (P2): S2c で移す対象は「decode/bind へ渡す箇所」ではなく **`recover_gf_template` の全利用** (gfinit_host.c だけで約 20 か所、`rg recover_gf_template` で全部)。template の 3 表が 0 件になると、移し忘れた負の試験 (例 gfinit_host.c:90-91 の decode_rom(...)<0、:408 の expand_step(...)<0) が別の理由で通るため。host 試験に `recover_gf_template.rule_count == 0 && recover_gf_template.rules == 0` を確かめる 1 本を足す。
- B2 (P3): 試験の移し方は、各ファイルの冒頭で展開済みの profile を 1 つ作り、名前を置き換える機械的な手順。§6 の試験の行数は 60〜90 より多くてよい。
- B3 (P3): `generated/recover_gf_tables.inc` の再現は `gen_recover_tables.py --check` を check-recover-tables-host から呼んで検査する (生成物を手で直さない)。
- B4 (P3): gf_read の条件 (`r->plan == &g->compiled->plan`) の追加で、前の RUN の compiled が残ったまま SIZE_BARS に入る組み合わせを owner 試験で 1 件確かめる (gfinit_recover_end が VERIFIED/FAILED で解放するので到達しない見込み)。
