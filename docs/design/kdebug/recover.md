# kdebug/recover.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: BARLEN_DESIGN.md・GF6200_4A_DESIGN.md・GF6200_4A_S2_LAYOUT.md・GF6200_RECLOG_DESIGN.md・GF_STAGES_DESIGN.md・HWDEV_CORE_API.md・HWDEV_DESIGN.md・REALHW_RECOVER_ADDENDUM.md・REALHW_RECOVER_ADDENDUM_R2.md・REALHW_RECOVER_DESIGN.md・RECOVER_ROUTE_TEST.md・RECOVER_T4B_INJECT.md (D1 で移した)
ソース: `kdebug/recover.c`

## BARLEN_DESIGN

| 定数の置き場 | 内容 |
| --- | --- |
| `kdebug/recover.h:46` | 既存 RECOVER_RANGE_MAX を利用。終端は既存 RECOVER_ADDRESS_END を利用 |

- 他装置どうしの区間重複は本変更では判定しない。bridge窓とその子の重複・alias で誤拒否しない。

- GPU 対各除外区間は正確長または上限を使った半開区間 `[base,base+len)`。`a < b_end && b < a_end` のときだけ拒否、隣接は許可。

- 比較は `kdebug/recover.c:1997` に既存、全加算は64bit整数。GPU長はA1で測り、snapshotのBAR値から作らない。

## GF6200_4A_DESIGN

W 行は recover の register descriptor に収容し、4a 独自の許可表を持たない。read安全性・write/verify mask・head/port/index・依存順序も同 descriptor で検査する。

- KPEEK のブロック単位の読取行をそのまま W にしない。script の全分岐、macro、PLL内部手順、前処理を展開してレビュー済みの番地だけ W 行にする。

- ROM由来の番地リストは許可の根拠ではない。独立に固定した W 行との積集合を検査する。PFB全域、PBUS全域、PCRTC全域の一括 W 許可は禁止。

## GF6200_4A_S2_LAYOUT

## 1. layout の記法と番号（この規則から静的 C 配列を生成する）

| 記号 | 固定契約 |
| --- | --- |
|I(x), R(t), S(q)|それぞれ IMM（復号済み値）、READ（target ID）、SAVED（同sessionのslot）。列の要素数がargc。targetも先頭I(t)で渡す|
|L(t,v), X(t,i), D(t,v)|`[I(t),I(v)]/WRITE`、`[I(t),I(i)]/SELECT`、`[I(t),I(v)]/DATA`。Xのiはtarget.index|
|Q(t)|`[I(t)]/READ`。条件用読取り・一時採取用。read_safeとindexed選択成立が必要|
|M(t,m,v;dir)|`[I(t),R(t),I(m),I(v)]/dir`、登録EXPR `(old & m) OR v`。幅mask適用。dir=WRITEまたはDATA。kinds=(READ,IMM,IMM)の式 `(old & m) OR v` を1件登録して共有する|
|C番号(引数列)|方向NONE、rule_id=expr_id=NONE。表内の引数は明記したkindだけ。無引数は `[]`|

具体形：planと一緒にCRC/hash対象の固定capture表 `{slot,producer_node,consumer_node,target,read_ordinal}` 3件を渡す。REC_ARG_SAVEDの番号は開始snapshotを0≤slot<p->count、命令内captureをslot=p->count+i（i=0:RESET184C、1:CR57 head0、2:CR57 head1の3件）に固定し、bindの範囲検査と実行時の参照先をこの境界で分岐して後者をcapture値/validへ接続する。

recover所有値/validは当該producerの成功したREADでのみ設定、全経路の先行をbind検査。consumerは指定session/phase/main/pc/ctx/head/targetに完全一致、使い終わり・abort・session終了で無効化。普通のrequires成立bitをQ成功と偽装しない。

RESETはe14内READ ordinal0、CR57はQ0 ordinal0を指定。SAVEDのidentity式にはslotのみ渡す。未接続時は3行をDENYのままとし、RESTORE/LOCALへ偽装しない。捕捉値はcleanupから参照不可。

COPY2件も登録EXPR＋読取元分類＋既存mask/range検査が揃うまでDENY。mainの未知exprをhost専用合成式で許可しない。

> [要確認] 現行は診断も hwdev/gf/runner.c を使い、main 境界の 5 node を生成する。原文の静的展開・ordinal・verify 規則を現行実装と同一とは扱えない。

## GF6200_RECLOG_DESIGN

```c
static struct rec_tx_diag txd;         /* recover.c。最初の失敗は上書きしない */
static u32 tx_pos, tx_sent, tx_where;  /* 現在の行送信の文脈、12 B */
/* fn: NONE, READY, PUT, WAIT_FRAME, PIT_WAIT, BEGIN, EMPTY, BUDGET, ROW */
/* where: NONE, BYTE, BUSY_WAIT, AFTER_PUT, FLUSH, SEPARATOR, BUILD */
/* tx_fail(): active && !valid のときだけsid/seq/pos/fn/生rc/where/sent/polls/bytesを保存しvalid=1。記録専用で既存失敗出口を維持。 */
```

`row_send()` が active=1、begin 前 pos=0、各 byte 前 pos=i+1（ASCII の R が1、LFも数える）を設定。

補償 LF は pos=0/where=SEPARATOR、flush は pos=行長+1/where=FLUSH。end 後 active=0。

ready busy 中の wait は where=BUSY_WAIT/sent=0。sent は当該 byte の put 成功を示し、到着証明ではない。

begin/empty 失敗と poll・byte・時間・safe-point の各出口も記録（BUDGET の rc=-31/-32/-33/-34）。

外部 rc は -1 を維持。対応: ready<0→READY、生put非0→PUT、wait非0→WAIT_FRAME/PIT_WAIT は記録後break→既存REC_LOG出口。

begin/empty非0→BEGIN/EMPTY は既存所有解除・lost_lines出口へ。poll=-31/byte=-32→BUDGET記録後break→REC_LOG。

時間=-33/safe-point=-34→BUDGET記録後、既存TIMEOUT等の失敗とcleanup出口を維持。ROW=-35→REC_LOG。生rcは潰す前に保存。

put直前sent=0、成功直後sent=1/where=AFTER_PUT。tx_failを直接returnして既存fail()/end/lost_linesを飛ばさない。

セッション開始時だけゼロ化し、PAUSED/cleanup/owner detach では保持。新 sid 開始まで RAM に残す。

## GF_STAGES_DESIGN

`recover_owner_configure`（`recover.c:222`）**各入口でEのときだけ failed/cold_latched の拒否を迂回**し、終端 `REC_FAILED` も許す。RUNNING/PAUSEDからは開始しない。

## HWDEV_CORE_API

```c
/* 式は既存 recover_expr の id/argc/kinds/eval。eval は検査済み整数のみ。 */
```

- FAILED/cold・PTE破損ラッチ・最後の結果・session 世代は核の boot 寿命で、部品の解放/再登録で初期化しない。核の S0 ops/profile に戻すまで部品作業 RAM を返さない。

## HWDEV_DESIGN

recover は状態・検査・保存・ログ

- profile CRC は正規化値と式 ID/型、plan hash は全表と ROM 識別。関数ポインタを hash に入れず、実装は部品 CRC・核 CRC・契約版で固定。未実装式・未知版は拒否。

- FAILED/cold・PTE 破損・最後の結果・session 世代は核の boot 寿命。部品の登録や free で消せない。通常終了でも初版の部品 body は常駐、stop=0 で hot unload なし。開始失敗は module の rollback/隔離に従う。

- `recover_owner_configure(NULL, ...)` は IDLE/終端でのみ、最初に登録した核 S0 ops/profile/opaque に戻し plan と派生参照を解除する。結果・session・FAILED/cold は初期化せず、解放順は共通終端 → 核の参照解除 → work free → lease 解除、A1 成功だけは保持する。

## REALHW_RECOVER_ADDENDUM

## §2 追加：plan の入力と寿命

- planは4a S2のdecodeが作る、main別の命令列・分岐辺・内部write列だけの静的表。許可表はr2のrecover profileだけであり、planは許可を増やさない。PREPARE・SIZE_BARS・正常終端はrecover側の固定列を同じ形式で添える。任意グラフ言語・callback式・ユーザーポインタ入力は導入しない。

```c
struct recover_plan_arg { u32 kind, value; }; /* IMM=value, READ=target ID, SAVED=snapshot slot */
/* ctxは呼出し・反復を区別する不透明なID。構造化ctx表は持たない。 */
struct recover_plan_node {
    u32 phase, main_id, pc, opcode, substep, ctx, head, skip;
    u32 rule_id, expr_id, arg_first, arg_count, next[2];
};
struct recover_plan_dep { u32 consumer, required_rule, producer; }; /* node IDs */
struct recover_plan_entry { u32 phase, main_id, first; };
struct recover_plan {
    u32 version, profile_crc, rom_crc, rom_size;
    const struct recover_plan_node *nodes; u32 node_count;
    const struct recover_plan_arg *args; u32 arg_count;
    u32 ctx_count; /* node.ctxは0以上ctx_count未満、依存照合にも使うためhash対象 */
    const struct recover_plan_dep *deps; u32 dep_count;
    const struct recover_plan_entry *entries; u32 entry_count;
};
struct recover_plan_buffers { u32 *flow_bits, *valid_bits; }; /* 4aが実node数×64 u32とvalid 64 u32を確保 */
int recover_bind_plan(struct recover *r, const struct recover_profile *p,
                      const struct recover_plan *plan, struct recover_plan_buffers *buf, u32 *plan_hash);
int recover_plan_enter(struct recover *r, u32 node);
int recover_plan_leave(struct recover *r, u32 edge); /* 0/1 */
```

- NONEはr2同様0xffffffff。添字は各配列の0始まり、参照しない欄は0またはNONEと型ごとに固定する。入口はphase/mainごと、RUNは0→4の正常終了順のみ。非scriptはmain/pc=NONE。nodeのrule_id=NONEは制御・読取等の非W、Wは必ず一つのruleに対応し内部substepも省略しない。 件数上限は次表とし、超過はbindで拒否する。

| nodes | args | ctx | deps | entries |
| --- | --- | --- | --- | --- |
| 2048 | 8192 | 2048 | 64 | 32 |

- 4aはplanと一緒にflow_bits（2048×64 u32 = 512 KiB、nodeごとの必ず成功済みproducer集合）とvalid_bits（64 u32、nodeごとの成立bit）を静的バッファでbindへ渡す。bindで初期化し、flow_bitsは静的検査中、valid_bitsはsession終了まで保持する。集合内producerのctx/ruleから通常requiresを判定する。

```c
int recover_access(struct recover *r, u32 node, u32 dir_substep, u32 target, u32 width, u32 *value);
/* dir_substep は READ/WRITE/SELECT/DATA。node≠NONE なら node の opcode/substep と一致を要求、node=NONE (cleanup 中) は dir_substep と RESTORE/QUIESCE phase の rule だけで照合する。 */
/* 共有read/write口。方向はnodeのopcode/substepで決め、readは*valueへ出力、writeは*valueを入力。sessionはplan参照・現在node・valid_bitsだけを保持する。 */
```

## §2 追加：EXPR 引数の契約

- 制御nodeもargsに復号オペランドを持ち、opcode/substepごとに引数の順・型を固定する。多肢選択は選択indexとの一致判定を二辺の固定列に展開し、全選択肢と範囲外を残す。条件判定のREADとindexed SELECTにもr2の安全性/許可検査を適用する。

- EXPRのW nodeのexpr_idはrule.value.expr_idと一致し、arg_first/countはargsの連続範囲。expr IDごとにrecover側で引数順・個数・kind・値域・整数評価手順を固定する。IMMはdecode済み即値/表選択値/PLL limits、READは同substepのread_safe target、SAVEDは同sessionのvalid slotだけ。任意アドレス・任意計算結果を引数として受理しない。 登録はカーネル内の静的表とし、型は次のとおり。

```c
struct recover_expr { u32 id, argc; const u32 *kinds; int (*eval)(const u32 *args, u32 argc, u32 *value); }; /* kindsはargc個、検査後の整数値だけをevalへ渡す */
/* 登録表は recover_profile に `const struct recover_expr *exprs; u32 expr_count;` を足して渡し、CRC の対象にする。host 試験は合成式を持つ試験用 profile を渡す (本番の登録表には入れない)。 */
```

## §2 追加：検証済み BAR 長

```c
struct recover_bar_extent { u32 raw, base, length, valid; };
struct recover_bar_measurement {
    struct recover_identity identity;
    u32 measurement_session;
    struct recover_bar_extent bar[3];
};
/* recover所有：A1検証結果cacheと、各sessionにコピーしたbar[3]。snapshotとは別。 */
```

- 取得元は承認済みA1のSIZE_BARSだけ。recoverが測定mask・元BARを受け、4a §3のmemory32/非零/2冪/整列/溢れ/他装置・bridge窓照合を検証し、BAR復元とCommand overrideおよびA1終端成功後にcacheをvalidにする。途中結果は同A1 sessionだけの候補とし、失敗時に破棄する。ROMや固定想定サイズで補わない。 受渡し口は`int recover_bar_probe(struct recover *r, u32 bar, u32 raw, u32 mask);`とし、barは0〜2、rawは元BAR、maskは測定値。

- A1は独立trialなのでcacheをrecover所有者の同一起動・同装置の測定記録として保持する。後続trial_beginでidentityと生BAR0〜2を再照合してsessionへコピーする。再測定開始・起動/装置/BAR不一致・測定失敗で無効化。実アクセス直前の生Command/BAR確認も維持する。 ops.raw_configはcommandとbar[3]（BAR0〜2）を返す形へ広げる。

> [要確認] recover_bar_probe の最終シグネチャは exclude/upstream も受け取る。下記の確定シグネチャを参照。

- C1: recover_access に dir_substep (READ/WRITE/SELECT/DATA) を足した。node≠NONE なら一致を要求、cleanup 中 (node=NONE) は dir_substep と RESTORE/QUIESCE の rule で照合。

- C2: 式の登録表は recover_profile の exprs/expr_count で渡し CRC の対象。host の合成式は試験用 profile。

- C3: `int (*raw_config)(void *ctx, u32 *command, u32 bar[3])`。

```c
#define RECOVER_RANGE_MAX 256
struct recover_range { u32 base, len; };
int recover_bar_probe(struct recover *r, u32 bar, u32 raw, u32 mask,
    const struct recover_range *exclude, u32 exclude_count,
    const struct recover_range *upstream, u32 upstream_count);
```

- 上記を79行の受渡し口の確定シグネチャとする。両表は4a所有の各256要素の固定長配列で、countは使用要素数（0〜256）。bar/raw/maskは既定どおり。upstreamは当該bar用とし、直結ならcount=0。count=0のポインタはNULL可。

- 範囲は半開区間[base, base+len)。recoverはlen非零・端点の溢れを検査し、候補BARがexcludeのどれとも重ならず、upstreamの全窓に包含されることを検査する。端点計算はu64で行い、2^32を超えれば拒否する。

- 戻り値0が当該BARの測定・照合成功、負値が失敗。recoverは成功時のみ候補へ記録し、失敗時は候補/cacheを無効化、4aは既存の失敗/復元経路へ進む。表は呼出し中だけ参照し保持しない。cacheのvalid化は既定のBAR復元・Command override・A1終端成功後だけ。

```c
struct recover_opcode_layout {
    u32 opcode, substep, argc;
    const u32 *kinds; /* argc個の順序付きkind列: 既存IMM/READ/SAVED */
    u32 dir_substep; /* 既存READ/WRITE/SELECT/DATA、アクセスなしはNONE */
};
/* recover_profileへ追加し、内容をprofile CRCの対象とする。 */
/* const struct recover_opcode_layout *layouts; u32 layout_count; */
```

- r5: ops.read/write は `(opaque, const struct recover_bus_access *, value)` とし、共有 target ID、dir_substep、space/bar、検査済み実効 offset、width を渡す。SELECT は index port、DATA/READ は data port、同じ target ID を維持し、callback が前回アクセスから方向を推測する契約にはしない。

- 内部 profile の版は RECOVER_PROFILE_VERSION=2。旧形式は拒否する。式の eval ポインタは CRC に含めず、式 ID・argc・kinds と profile 版／image identity で静的実装を識別する。

- layout のアクセス形は先頭 IMM を target ID とし、READ はこれだけ、非 EXPR の W は続く IMM 値または SAVED slot、EXPR は続く登録式の引数列とする。非アクセス形の全引数も layout で照合する。式の引数上限は32、layoutは先頭targetを加え33。

- flags は N1、LOCAL（分類済み局所 SAVED 復元）、FORCE（skip 時の許可付き実行）に分ける。通常の skip node はアクセスなしの layout を用い、FORCE でも rule の照合を省かない。

- BAR_VRAM 用の予約区間を profile.vram_reserved（BAR相対base/len、CRC対象）に置く。実capacityの証明ではなく、検証済みBAR長と予約全長の双方を検査する。未使用は0/0。

- kdebug内のboot寿命singletonをrecover_owner()で共有し、試験所有者は同一個体へprofile/opsを接続する；差替えはIDLEまたは非coldのVERIFIEDだけ、sessionカウンタ・既存終端は保持し、FAILED/coldを初期化で解除しない。

- 未試験のnow 0は新sessionでS0のみを実行する；既存HALがCUI・31kHz・0FACh=98を確認できる場合だけ、装置書込みなしのS0予算/profile/opsを使い、0FACh書込み承認は常にfalse。TVRAM内容を保持しcursorを同期、S1はunknownのPARTIAL（装置保存・実測済みとの主張なし）。

- 内部 profile 版を3とし、plan の capture 表（slot/producer/consumer/target/read ordinal）と unresolved_count を hash 対象に追加する

- COLD data と独立した index_save_targets は検査済み index port の代表target IDの配列（CRC対象）とし、同portのSELECT保存を1件にまとめる；GFはCRTC/SEQの各headを分類し、data保存・終端再生の権限は増やさない。

- layout に REC_LAYOUT_SKIP_HEAD を追加し profile CRC に含め、内部版を4へ進める；GF の53/54のKだけが付与し、共有コアはopcode番号を解釈しない。

## REALHW_RECOVER_ADDENDUM_R2

## 2. 欠け 2 の決定

- GNU11のkdebug内部契約を版上げする。32件のsnapshot表とW規則表を分離するが、許可の所有者は共にrecover。同じ番地の複数phase/値規則を表現する。

- `key` は下記targets配列のID（0始まり、ポインタや詰込み番地でない）。全read/write accessorでこの共有表を解決する。snapshotのregsとquiesceも同じIDを参照する。

```c
struct recover_target { /* canonical address tuple */
    u32 space, bar, offset, head, port, index, width, access, read_safe;
};
struct recover_value {
    u32 mode, mask, bits, min, max, expr_id;
    const u32 *allowed; u32 allowed_count;
};
struct recover_guard { u32 key, mask, expected, when; };
struct recover_rule {
    u32 id, key, phase, main_id, pc, substep, write_mask, verify_mask;
    struct recover_value value;
    const u32 *requires; u32 requires_count; /* rule IDs */
    const struct recover_guard *guards; u32 guard_count;
    u32 flags; /* evidence (file:line) はhost側のrule ID対応表/コメントに置く */
};
/* recover_profile に追加。既存のregs/countはsnapshot/復元順専用。 */
/* const struct recover_target *targets; u32 target_count;
   const struct recover_rule *rules; u32 rule_count;
   u32 restore_max_polls, s0_reserve_ticks, s0_reserve_polls; */
/* recover_reg に u32 restore_rule_id を追加。
   recover_write に u32 rule_id を追加。key/width/valueは規則と照合。 */
```

- table上限はtargets=1024、rules=2048、各requires/guards=8。S1の619副作用候補は許可済み件数ではない。展開後不足なら開始拒否しレビューで上限を更新する。実行回数は既存の動的予算で制限する。

- spaceはPCI_CFG、BAR_MMIO、BAR_VRAM、PC98_IO、VGA_DIRECT、VGA_INDEXED。不要欄は0、非indexedのindexはNONE（0xffffffff）、非scriptのmain/pcはNONEとし曖昧なwildcardにしない。

- phaseはSIZE_BARS、PREPARE、RUN、VRAM_TEST、QUIESCE、RESTORE、ROUTE、SCREEN、VERIFY。既存ログphaseと区別した権限用enumを追加し、実行contextはrecoverが保持する。

- main_id/pc/substepを含め完全一致で引く。SUB/CALLは呼出元main＋実pc、PLL/macroは各内部substepで照合する。INSPECTはread_safeな読取だけで、index選択書込みを行わない。

- value.modeはLITERAL、SET、RANGE、EXPR、SAVED。共通条件は`(new & mask)==bits`、幅内、RMWでは`((old^new)&~write_mask)==0`。SETは列挙値一致、RANGEはmin..maxを追加で満たす。

## REALHW_RECOVER_DESIGN

| 状態 | 定義 |
| --- | --- |
| S0: 安全な 98 CUI | 31kHz、98 テキスト表示、graphics 非表示・アクセス/表示 page 0、外部 GPU の legacy 窓を解放、試験装置の DMA/IRQ を停止。PEGC を備える既定構成を維持するが GUI は起動しない |

| 状態 | 定義 | シリアルで確認する値 / 証明の限界 |
| --- | --- | --- |
| S1: 試験前の装置状態 | 同一起動・同一装置・同一 profile の安全に保存/復元できるレジスタだけが一致。除外した内部状態/VRAM/FIFO は別扱い | snapshot ID/CRC、BDF・vendor/device/revision・BAR、各許可レジスタの `(actual ^ saved) & verify_mask == 0`、未検証数。全体一致とは表示しない |
| ③ シリアル 1 命令 | `recover now <session>` が試験停止→①②→verify を一度の要求で行う。未試験時も S0 profile のみ実行可 | `REC ... result=OK state=S0`。試験中は§4 の専用 abort 受信に同じ要求を渡し、shell の再入実行はしない |

## 5. 書込み試験との契約・安全規則

| 項目 | 必須の約束 |
| --- | --- |
| register descriptor | 装置/版、BDF・device/subsys、BAR・相対 offset、幅、W、read 安全性、write/verify mask、許容値・phase、head/port/index、依存順序、停止条件、根拠 file:line を固定。4a のレビュー済み W 行もこの単一 descriptor に収容し、別の許可表は持たない。indexed 型は操作前に元の index と安全に読める lock/data を保存し、必要な解錠（CR1F・CR11 bit7）→data 復元→lock 復元→元の index 復元の順。未分類は拒否 |
| 保存禁止 | read-to-clear・FIFO pop・DAC index を進める読出し・属性 FF を変える読出しは採取しない。必要なら既存 driver の shadow を出典付きで使う |
| 書戻し禁止 | W1C/W0C status、IRQ ack、reset/trigger、doorbell、FIFO、予約 bit、EEPROM/flash、DMA 起動を生の保存値で再生しない |
| mask の扱い | RW だけ安全に保存/復元。W1C 混在は単純 RMW 禁止、許された幅と非作動値を profile に定義。例: PCI Command と Status をまとめた DWORD 書戻し禁止 |

- 実験 VRAM は予約範囲のみ。保存で戻すか破棄してよいかを plan に明記。通常 RAM の固定領域へ snapshot を置き、試験装置の VRAM や LAN 越し保存だけに依存しない。

安全点・accessor が非 0 を返した後、operation は資源に触れずすぐ戻る。local_cleanup は冪等で、解放はこれだけが持つ

## RECOVER_ROUTE_TEST

T5 の 1 回は、現在値が 00b でも 00h を書く経路・mask 読戻し・98 表示維持を確かめる；非98入力からの切替成功までは証明しない。

- T5の未試験S0専用：IDLE・token.session=0・実機S0 ops接続時だけ受理する。既試験なら新たな起動で実施し、状態を強制初期化しない。

- 通常の `recover now <sid>` と自動復帰は従来どおり承認false。T5成功を恒久的な許可に変換しない。

`route_test_once` と承認は recover.c のファイル内 static とし、入口関数 (例 `recover_route_test_session`) が立て、全出口で false に戻す。§2 の「route_write_approved=true を設定」は、この static の承認を screen() が route_write_approved と OR して読む形に読み替える (configure の永続値は false のまま)。

- `recover_route_test_session` (kdebug/recover.c) の IDLE・session の検査は kdebug_main.c の op 12 の門と重なる。害は無いので残す。

## RECOVER_T4B_INJECT

| 実物 | 確認と今回埋める境界 |
| --- | --- |
| `kdebug/recover.c:174,1087,1212,1240` | boot 寿命の owner、bind 済み plan 必須の begin、承認済み step、共通 end。FAILED を再開始で消せない |
| `kdebug/recover.c:576,1058,1191,764` | profile に target 必須。trial/opcode 期限、step poll 上限、独立 UART poll 上限がある |
