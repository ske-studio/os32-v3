# GeForce 6200 段 4a 詳細設計 r3 — init script の試行

> 状態: **現行 (2026-10-21)** — 段 4a (初期化 script の試行) の確定設計。r3 + Opus 第 3 回の PM 反映。実装の段は S1〜 (段の状態は票 TASK_GEFORCE6200 §5 が持つ)。
> 出所: `~/os32-tmp/run/gf6200_4a/DESIGN.final.md` の確定版を 2026-10-21 に写した。往復の版 r1〜rN は ~/os32-tmp に残る。中身は変えず、リポジトリ内の文書への参照だけリンクに直した。


## r2→r3 の変更

- N1: E81E の条件付き許可を §4・§8・§9 に反映。
- N2: 容量不明でも最小 VRAM 試験を行い、容量照合だけを制限。
- N3: A2 は PSTRAPS、main0 COPY 後は CR3C の組3を確認。
- N4: 独立 SIZE_BARS 終端にも Command override を適用。
- N5: 表範囲を半開区間とし 0x5DF2 の境界試験を追加。
- N6: A0 の Command は記録のみ、BM=0 は A1 後に要求。
- B1: quiesce 前に生 config の Mem/BAR0 を確認。
- B2: 保存 Command を再生せず0102hを verify。
- B3: 承認待ちを PAUSED とし期限の積算を停止。
- U1: 書込みは立会い、無応答時はユーザーの物理復帰。
- U2: A1→A2→mainごと→A4 の段別承認を確定。
- U3: 4a はモニタ無し、4b 前にモニタを決定。

## r1→r2 の変更

- R1: PRMVIO の head 別 2000h オフセットと A2 の head1 検証を追加。
- R2: 10020Ch の書込み無しを確定し、容量不明時の停止と条件付き分岐を追加。
- R3: 実物193命令・27種を確定し、全種を init.c と照合して表を置換。
- R4: 4a の Command を0102h、BusMaster=0に固定。
- R5: recover r2 §5 の trial・descriptor・REC・cold-required 契約へ統一。
- R6: manifest からログ送信時間を算定し、実行時間と別の有限予算に分離。
- R7: 選択値を静的検査し、実行時の範囲外は書かず記録して続行。
- R8: REPEAT count=0 は静的拒否へ変更。
- R9: PLL の中間演算を上限検査付き u32 と static assert に変更。
- R10: 54h=0で別名不応答／1854hへの禁止書込みの次の分岐を追加。
- PM: SR01 の画面オフ解除、PMC RESET・strap・PRAMIN の副作用を明記し、表示有効化のゲートを具体化。

- 対象: Ra266 の PCI 0:13.0、10de:0221、NV44A。設計のみ。実装・配備・実機書き込みは行っていない。
- 到達点: OS32 の解釈器で初期化表を実行し、PFB・PLL を設定して VRAM の読み書きを確認する。最初に BAR の大きさを測る。
- 映像出力の有効化は 4b、画面モード設定は 4c。ROM x86 コードの逆アセンブル、独自ドライバ解析、汎用 GPU ドライバ化は行わない。
- GNU11、整数演算、協調切替、カーネルは libc・ユーザーランド数学ライブラリに依存しない。共有ヘッダは C89 互換。
- 本文の `O/` = `/home/hight/os32-v3/`、`R/` = `/home/hight/os32-tmp/refs/geforce6200/`、`P/` = `/home/hight/os32-tmp/run/`。
- `N/` = `R/nouveau/drivers/gpu/drm/nouveau/nvkm/`、`I:行` = `N/subdev/bios/init.c:行`。参照版は `R/INDEX.md:6–10`。
- nouveau の引用元は MIT 本文のあるファイル。派生実装に著作権・許諾を残す。Haiku は計画のライセンス確認待ちを守り、挙動の照合だけに使う。

## 1. 実物の確認 — 食い違い 6 件

| ID | 前提・古い記述と実物 | 本設計での扱い |
|---|---|---|
| D1 | `nvbios/init_scripts.txt` の全9本を集計すると193命令・27種（圧縮されたNV_REGは各4命令に展開） | §4で全27種をinit.cと照合済み。S1はROM実バイトとmanifestの再検証を行う |
| D2 | 同票:56 の「COMPUTE_MEM が量を測る」に対し、NV44A は `N/engine/device/base.c:677–683` で nv1a_devinit。`N/subdev/devinit/nv1a.c:29–35` に meminit がなく、同 `base.c:44–47` は何もしない | NV44A の 0x63 は明示の no-op。容量は PFB・BAR1・独立の VRAM 試験で照合。nv20 の容量探索を移植しない |
| D3 | `P/gf6200/PLAN.final.md:34` の gfobs.c ではなく、実物は `O/kdebug/kdebug_main.c:34` の dbg_call→kpeek_call | 既存 KAPI を使う専用 op を追加。新しい KAPI スロットは不要 |
| D4 | 計画:35 の「4MB 窓 A/B」の実装は、実際には `O/kdebug/kpeek.c:112–146` で必要ページだけ保存・付け替え・復元、:49 は RW 無し | 同じ静的 PT を使い、書くページにだけ RW。4MB 全体を毎回張らない |
| D5 | 依頼の coldstart_card_516_up の所在は nv_general.c ではなく `R/haiku/src/add-ons/accelerants/nvidia/engine/nv_info.c:313`。:401 は ROMSHADOW を有効化 | coldstart 全体の移植はしない。末尾の映像有効化を 4a へ持ち込まない |
| D6 | 「命令前にシリアルへ1行」は既存 `O/drivers/serial.c:822–835` だけでは保証できない。送信待ち切れでも void で戻り、文字が落ちる | 成否と送信完了を返す専用出口を足し、失敗時は次の GPU 操作をしない |

- 確定観測: 同票:44–56 の Command=0106h、BAR0=24000000h、BAR1=30000008h、BAR2=25000000h、cfg50=0・54=1、PMC=04A100A1、PFB 全零、27MHz、RAM 組3を起点とする。
- 起動時の非零レジスタを「POST 済み」の証拠にしない。段0〜3の結果は変更せず、D2は参照実装との食い違いとして残す。

## 2. 置き場・入口・権限

- 推奨: **kdebug の部品に専用の GF4A op 群**。KDEBUG=1 のときだけ実装を読み込む。別の観測バイナリより KAPI・窓・出力・既存の読み取りを共有できる。
- shell `gfinit` → `kdebug_call(op,&req,sizeof req)` → dbg_call の振り分け → gfinit。`peek` の op・構造・読み取り専用契約は維持する。
- op: INSPECT（PROM取得・静的検査のみ）、SIZE_BARS、PREPARE、RUN_ONE、VERIFY、ABORT。任意番地へ値を書く op は設けない。
- 固定長要求 `version,size,flags,session,script_id,max_steps,status,fail_pc,fail_reg,steps`（u32 のみ、予約語は0）。ユーザーポインタや任意 opcode 列は受けない。要求は部品内へコピーして検査する。
- op対応: INSPECTはtrial外、SIZE_BARSは測定専用の有界trial_begin→trial_step→trial_end、PREPAREはtrial_begin(profile,plan_hash,budget)、RUN_ONEは1 opcodeごとにtrial_step、VERIFYはtrial_end(normal)、ABORT・エラー・期限超過はtrial_end(abort)。session・snapshot・arm・終端cleanupはrecoverが所有する。
- Command は descriptor の override 項目とし、snapshot の0106hは記録だけで書き戻さず、目標0102h（BM=0）を生 config の16bit読取りで verify する。SIZE_BARS の独立 trial 終端にも適用する。BAR復元失敗時は decode を止めたまま FAILED とし、目標値のために Mem を再有効化しない。
- PREPARE と各 RUN_ONE の正常帰還時は kdebug_call の外で PAUSED とし、承認待ちの期限を止める。PAUSED 中の復旧要求は `recover now <session>` だけを受け、次段の承認済み kdebug_call で RUNNING に戻す。trial の期限は kdebug_call 内の時間だけを積算し、承認待ちを数えない。
- INSPECTでROMバッファを上限128KiBまで部品所有で確保し、不足なら書く前に終了。実行中は不変とし、CRC・長さ・BIT表位置・BDF・起動世代をsessionに結び付ける。終了/ABORTで解放し、古いsessionやmainの飛越し/二重実行は拒否する。
- 書き op でも独立の `GF_ALLOW_WRITE` が無ければ DENY。shell の `--allow-write` は毎コマンド必要。rshell の同名旗だけではカーネル許可にしない。
- 許可表・ログ形式・復旧状態は [realhw_recover/DESIGN.final.md](REALHW_RECOVER_DESIGN.md) §5 を唯一の契約とする。W 行は recover の register descriptor に収容し、4a 独自の許可表を持たない。read安全性・write/verify mask・head/port/index・依存順序も同 descriptor で検査する。
- KPEEK のブロック単位の読取行をそのまま W にしない。script の全分岐、macro、PLL内部手順、前処理を展開してレビュー済みの番地だけ W 行にする。
- ROM由来の番地リストは許可の根拠ではない。独立に固定した W 行との積集合を検査する。PFB全域、PBUS全域、PCRTC全域の一括 W 許可は禁止。
- MMIO/VRAM は volatile、supervisor・PCD、書きだけ RW。PAGEの保存→写像→操作→復元を irq_save/restore 内で行う。index/data と RMW は一組の間だけ割込み禁止。
- TIME、ログ、ROM全体の検査を割込み禁止区間に入れない。窓の排他フラグを KPEEK と共有し、busy 中の別 op は BUSY。PTE復元失敗は致命エラーとして後続を止める。
- `pci_get` は列挙時の写し（`O/drivers/pci.c:190–207`）。書きの直前には生 config を読み BDF・BAR・Command を再検証する。

## 3. BAR測定と G1、実行準備

| 順序 | 操作・検査 | 失敗時 |
|---|---|---|
| A0 読取 | Commandを生読みして記録のみ（起動直後の0106hを許容）。BM=0は承認済みA1で0102hへ移した後に要求し、それまではRUN禁止。ID・PMC・straps・cfg・他PCI装置とブリッジ窓を段0と比較。PROMを2回読み一致・55AA/PCIR・長さ・和を検査し、部品内の上限128KiBバッファに保存。下記A3の静的検査もここで先に完了する | 書く前に止める |
| A1 承認後 BAR測定 | 0:13.0 の Command16・BAR0〜2を保存。Commandの I/O/Mem/BusMaster を0にし生読み確認→BARを1本ずつ FFFFFFFFh、マスクを読む→直ちに元値を戻し確認 | cleanupで全BARを復元してから Command=0102h（BMを復元しない）。復元不一致なら decodeを止めたまま異常終了 |
| A1 終了 | memory32 の `size=~(mask & fffffff0h)+1`、非零・2冪・整列・加算溢れ無し。32bit型のみ許可、64bit/I/O型は別設計まで拒否。割当て区間を他装置・bridge窓と照合、Commandを0102hへ（MemとSERRのみ、BusMaster=0を生読み確認） | BARの再割当て・ROM BARのenableはしない。Mem無効中はPBUS鏡にアクセスしない |
| A2 G1前倒し | **PBUS BAR0+1854h←0**、PBUSとcfg54hを読み0、Command=0102h・BM=0を確認。98テキストが表示され更新できることをユーザーが確認 | 以後のscript実行を禁止。強制で54hを1に戻して試さない |
| A2 前処理 | 1850h=0・PMC割込み=0を確認。PMC.ENABLE bit0 を立て、head0を選択、必要な拡張CRを解錠。対象値と許可行を実行前manifestに含める | readback不一致なら止める |
| A3 静的確認 | 全 main 5本と SUB/CALL先を検査。193命令・27種・9本、表位置と全副作用を照合。承認対象のmanifestを確定 | 未知命令・N1のE81E以外の表示有効化・W行不足なら実機RUNを拒否 |

- Command は `pci_cfg_write16`（`O/drivers/pci.h:84`）を使う。Status の W1C ビットを含む DWORD RMW はしない。BAR probe は専用 phase に閉じ込める。
- G1（`P/gf6200/PLAN.final.md:88`）は「4bの前」の下限。本設計では初期化中の legacy decode も防ぐため4aの先頭で満たす。`R/envytools/docs/hw/bus/pci.rst:50–60,78` が根拠。
- [A0000h,C0000h) を GF の試験に写像・アクセスしない。GPU の I/O decode は常時0。1854h を非零、1850h bit0 を1にする script は事前拒否し、黙って書換えない。
- 各script終了でも cfg54/PBUS1854=0、cfg50/PBUS1850=0を確認。RESETの1850 bit0クリアは許可する。終了・ABORT時も54h=0を保持し、4bも同じ禁止条件を守るが、4a終了のGPU状態を残さず、承認されたinitを含む単一sessionを別途設計する。
- 前処理の根拠: `N/subdev/devinit/nv04.c:405–414`（PMC bit0・owner）、`R/xf86-video-nv/src/nv_hw.c:33–48`（CR1F=57h、CR11 bit7クリア）。元の index/owner/lock を記録する。
- MISCW=CBh・VSE2=1 は Haiku `nv_info.c:331–334` の照合対象。無条件な追加書きはせず、manifest上の必要性を確認して専用前処理に採否を記録する。

## 4. 解釈器 — 実物27種の照合済み仕様

- `nvbios/init_scripts.txt` の静的総数は193命令・27種・9本（main0〜4、SUB6/7/8、CALL D336）。REPEATの動的反復は件数に含めない。5DFA/5E34/5E6Eの各行はNV_REGを4命令として数え、main4の同一行DONEも含めた。
- 表の全行を `N/subdev/bios/init.c` の各handlerと照合した。0x58はE299/E2AB/D33Fの3件。資料冒頭のCOPY_NV_REG位置「E80B」は本文のバイト列・前後pcからE7FB（22B、次E811）と扱う。ROM実バイトの一致はINSPECTで確認する。
- 全読取りはLEの checked u8/u16/u32。`pc+length`、可変件数×要素幅、表index、ROM内参照先、shift、MMIOの整列を**実行可否によらず**検査する。マスク式の `m` は保持マスク。

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
|---|---:|---|---|
| 32 IO_RESTRICT_PROG | 32 | s=(VGA[index]&m)>>shift、count個のu32から選びR32へ。11+4count B。範囲外は無書込みで記録・続行 | I:621–651 |
| 33 REPEAT | 1 | 2B、実物count=10。count回反復し終端直後へ。0は静的拒否（nouveauでは本体へ落ち1回走る） | I:658–677 |
| 36 END_REPEAT | 1 | 1B、終端直後を保存し反復フレームへ復帰 | I:724–733 |
| 37 COPY | 1 | 11B、VGA←(旧&m) OR (shift(R32)&smask)。shift<80hは右、他は100h−shift左 | I:740–760,531–536 |
| 38 NOT | 2 | 1B、条件skip反転 | I:767–772 |
| 39 IO_FLAG_CONDITION | 4 | 2B、9B条件表参照、偽ならskip | I:512–528,779–790 |
| 4A IO_RESTRICT_PLL2 | 2 | 11+4count B、VGA選択のu32 kHzでPLL設定。範囲外は無書込みで記録・続行 | I:950–978 |
| 4B PLL2 | 1 | 9B、regへu32 kHzをsolverとチップ別手順で設定 | I:985–996 |
| 51 CR_INDEX_ADDRESS_LATCHED | 2 | 5+count B、CR addr0を保存、base++をaddr0へ、dataをaddr1へ書き、addr0復元 | I:1146–1168 |
| 52 CR | 2 | 4B、CR←(旧&m) OR 値 | I:1176–1189 |
| 53 ZM_CR | 9 | 3B、選択headのCRへ直書き | I:1196–1207 |
| 54 ZM_CR_GROUP | 3 | 2+2count B、{index,data}のCR直書き | I:1213–1230 |
| 58 ZM_REG_SEQUENCE | 3 | 6+4count B、baseから4ずつ増やしてu32直書き。実物count=3/4/4、11レジスタ | I:1281–1300 |
| 5B CALL（SUB_DIRECT） | 1 | 3B、ROM内u16絶対先へcall、DONEでpc+3へ。skip中は呼ばない | I:1344–1363 |
| 5F COPY_NV_REG | 1 | 22B、dst←(旧&dm) OR ((shift(src)&sm)^xor)。10020Cは読取り元、1700が宛先 | I:1415–1434 |
| 62 ZM_INDEX_IO | 2 | 5B、indexed VGA直書き | I:1441–1453 |
| 63 COMPUTE_MEM | 1 | 1B、force。NV44AはNOOP_NV4A（D2）、容量を設定しない | I:1459–1469 |
| 65 RESET | 1 | 13B、force。184Cの0F00を退避/クリア→regへdata1→10µs→data2→184C復元→1850 bit0クリア | I:1477–1497 |
| 6B SUB | 3 | 2B、script表のu8番号へcall、DONEでpc+2へ。skip中は呼ばない | I:1663–1683 |
| 6E NV_REG | 36 | 13B、R32←(旧&m) OR 値 | I:1709–1720 |
| 6F MACRO | 5 | 2B、macro表+index×8の{reg,value}を1組書く。4件の範囲検査 | I:1727–1743 |
| 71 DONE | 9 | 1B、現在フレーム終了、callなら呼出元、mainなら要求終了 | I:610–614 |
| 72 RESUME | 6 | 1B、条件skipクリア | I:1751–1756 |
| 74 TIME | 13 | 3B、u16 µs。実行許可時だけ待つ。§5の時計で最低指定時間を保証 | I:1781–1795 |
| 75 CONDITION | 2 | 2B、12B条件表の(R32&m)==v、偽ならskip | I:479–494,1802–1813 |
| 78 INDEX_IO | 8 | 6B、VGA←(旧&m) OR 値。indexは1Bとして復号 | I:1853–1868 |
| 7A ZM_REG | 42 | 9B、R32直書き。宛先200hだけbit0をOR | I:1892–1906 |

- 合計193。実物に無い34/56/69/76/79等の候補は許可集合から外す。4C〜50・5E・8E・5C・77・73・6D・47・48も無い。I2C・GPIOを含め表外opcodeは静的拒否する。
- 32/4Aは全てCR3C、mask=70h、shift=4、count=8でs=0..7を収容する。全到達値とcountを静的照合し、変異で範囲外が可能なら実行前に拒否。実行時の防御でも範囲外はnouveauどおり書かずRECへ記録し続行する。
- PM補足: script0はhead1/0のSR01 bit5を立て、RESETでPMC200h、NV_REGでstrap101000hを書き換える。script3はPRAMIN70FFF0hを書き、SUB8がhead0へ戻した後E81EでSR01 bit5を落とす（head0の画面オフ解除）。全てwrite-setに含める。N1のユーザー決定によりE81Eだけを許可し、cfg50h=0・54h=0（PBUS1850h・1854h）の読戻しと保持を前後で確認する。main3 の DRAM 初期化（MACRO0・REPEAT 10×MACRO2・SUB8・100210=80000000h・COMPUTE_MEM・1700への写し）を省略せずA4へ進む。他の表示有効化は静的拒否を維持する。
- 制御状態は `{pc,execute,head,call_frames,repeat_frames}`。executeのbit0=試行許可、bit1=skip、bit2=force（`I:59–81`）。forceは**書込許可・許可行・phaseを迂回しない**。
- skip中のHW読取は0、HW書込・TIMEは無し。NOT/RESUME/構造制御は進め、RESET/COMPUTE_MEMのforceを再現。条件は真でもskipを解除しない。SUB後の条件状態・headは共有する。
- 条件: `(R32&m)==v`。IO flag: `s=(VGA[index]&m)>>shift; (ROM[data+s]&dmask)==v`。欠けた表を偽として続行せず異常終了する。
- mainごとにexecute=1・head既定0で開始（`I:2323–2339`）。main番号順0→4を1コマンドずつ。SUB/CALLは同コマンド内、途中再開や任意pc開始は提供しない。
- 上限はmain1本につき動的4096命令、call深さ16、repeat深さ8、待ち累計5秒、送信を除く実行経過60秒。各内部MMIO・macro要素・repeatも予算を減らす。0回repeatは静的拒否、再帰・終端無しはhostで拒否を検証。
- 表領域は半開区間 `[start, start+n×size)` とする。IO flag 表 `[0x5DAA, 0x5DAA+8×9)` の終端0x5DF2は表に含めず、SUB8先頭として許可する。
- 9本の境界とopcode位置を先に作り、CALLが命令途中・表領域を指す、REPEAT不整合、未知命令、整数溢れは書く前に拒否。実行中のHW異常も即時停止し成功扱いにしない。
- BIT Iの追加unknown script（`I:430,2344`）が非零なら5本だけで完了とせず、manifestへ取り込みレビューするまで拒否。encoded MMIO番地のhead/OR補正（`I:144–179`）も展開後に許可検査し、出力依存で解決不能なら停止する。

## 5. PC-98 の VGA 別名、待ち、PLL

| 対象 | 実装・根拠 |
|---|---|
| VGA I/O | 3xxhへin/outしない。3C2/3CC・3C3・3C4/5・3CE/FはBAR0+0C0000h+head×2000h+port、3D4/5はBAR0+601000h+head×2000h+port。`R/envytools/rnndb/display/nv3_prmvio.xml:11–15`、`nv3_pcrtc.xml:23–30`、`nv_vga.xml:336` |
| 代替可否 | 上記別名の存在は資料根拠あり。legacy decode=0でのindexed読書き成功は実機A2で検証が必要。A2はPSTRAPSから組を導いて3を確認し、冷間のCR3C=0では止めない。main0の0xdfa9でCR3C←0、0xdfafのCOPYで(CR3C&0Fh) OR 30hを書いた直後にCR3C=3xhを確認する。I/O BAR有効化による代替はしない |
| indexed処理 | indexへの書きもW権限が必要。条件評価・選択読取も書きop内だけ。CR44はhead0に書き、値3なら以後head1（`I:240–260`）。PRMVIOにもhead×2000hを加える（`R/envytools/docs/hw/mmio.rst:95`）。A2でhead0/1両方のSR/GR/MISC別名が応答するか確認し、CR44切替後のhead1アクセスがhead0へ落ちないことを記録する |
| 除外ポート | 3C9・3DA/3BA等の副作用読取、未知port・indexは拒否。IO命令でread側が未定義のportは勝手に0を返さず、資料に基づく別名/意味が確定するまで止める |
| TIME | GPU PTIMERは初期化中なので時計にしない。OSのsys_time_now＋cpu_delay_usで少なくとも指定µsを待つ。校正済みを確認、分割待ちで予算を検査。長い待ちは切上げ可、切下げ不可 |

- PLL limits: 表0x5BC8、版0x21、4件。**エントリの選び方 (2026-10-23、S7b の実 ROM で PM 決定)**: 対象レジスタのキー (NV40 の対応 CORE 0x004000・MEMORY 0x004020・VPLL0 0x680508) が一致するエントリ、無ければ表の最初のエントリ (`N/subdev/bios/pll.c:162–175`)。キーの重複は断る。Ra266 の ROM はヘッダ 5・エントリ 35 バイト、キーは 0・0x4020・0x4008・0x402C で、CORE と VIDEO は最初のエントリを使う。`N/subdev/bios/pll.c:301–333` のVCO1/2上下限・入力周波数・N/M範囲・max_p/bias_pをchecked読取。ref_clk=0は27,000kHz（同:386–398）。
- 周波数はkHz整数、`f≈27000×N1×N2/(M1×M2×2^P)`。単段はN2=M2=1。中間積はu32、nouveauの段別演算順を維持し、一括積の式へ置換しない。N/M<=255、ref=27000を検査し、`_Static_assert(27000U <= UINT32_MAX / 255U / 255U, "PLL product")`で二段積上限1,755,675,000を確認する。clkP×M+丸め項も乗算前に `(UINT32_MAX-add)/M` と比較し、shift・加算・分母を検査して超過は拒否。pllnv04.c:99/107/171/184/195の演算をhostで比較し、32bitリンクで__udivdi3等の64bit除算支援が不要なことを確認する。
- solverは `N/subdev/clk/pllnv04.c:28–120,127–247` の探索順・丸め・単段/二段選択・NV4xの段間倍率制約を再現。解無しなら書かない。返した係数・実周波数・誤差をログ化する。
- 4004h/4024hは低位PLLのNMNM。limits検索はreg−4（`N/subdev/devinit/nv04.c:366`）、4000h/4020hの制御と対で扱う。680508hは高位PLL。
- 低位書込は同:277–353を基準に、4000/4004、4020/4024、4038/403C、4600、C040の順序・保存/復元・MPLL biasを再現。裸のM/N/P書込で済ませない。
- 高位は同:200–273、680508/680578・680580・1584・C040を含めチップ分岐で使用する補助レジスタを許可。402Ch=C1C11002hは生レジスタ値として扱い周波数と混同しない。
- 段3の要求200MHz@4004、400MHz@4024、36MHz@680508を入力として試験。perfのMemory200MHzとは別の符号化値なので「誤記」として半分にしない。
- 機能上のPLL lock検出を捏造しない。係数readbackと算出周波数、後続VRAM成功を別々に記録。合成limitsに対する誤差と限界外拒否をhostで確認する。

## 6. 停止・復旧・観測

- ログ形式は recover r3 §5 のRECのみ。各命令前の`phase=gf_pre event=BEGIN main=... pc=... opcode=... exec=...`、各書込前の`phase=gf_bus event=BEGIN pc=... bar=... off=... width=... old=... new=...`を送信完了してから処理し、ENDにreadback/rcを残す。sid/seqはrecover一元採番。最終result・試験結果・復帰結果も同契約に従う。
- RUN_ONE中も各opcode境界のtrial_stepでシリアルabortを取り込み、poll/delayも有界分割した安全点で確認する。要求があれば後続opcodeを実行せず局所PTE/IRQ/indexの後始末→trial_end(abort)へ進む。ABORT opがRUN中に届くことには依存しない。
- 既定9600bps（`O/drivers/serial.c:263`）では90文字×4096でも8N1で384秒。実際のRECはBEGIN/END・内部MMIO・cleanupも含め、manifestの全分岐・反復展開と各フィールド最大桁数から最大送信バイト数Bを計算する。UARTの実設定bits/frameで`T_wire=ceil(B×bits/frame/9600)`を求め、有限の送信待ち余裕を加えたT_logをtrial_beginの予算に渡す。kdebug_call内だけを数える総時間上限=60秒+T_log（PAUSEDの承認待ちは除外）、実測送信時間だけを60秒の実行予算から除外し、T_log超過はLOG_FAIL。待ち5秒は実行予算内。事前に算定不能・桁溢れ・recoverが総予算を受理しない場合は書く前に拒否する。
- serialの専用checked送信は既存経路と排他、TxRDY待ち＋最後の送信完了確認を期限付きにし、失敗はLOG_FAILで停止。LANは結果複製の出口、命令前の証拠の代わりにしない。
- 最後のREC gf_pre/gf_bus BEGIN行で命令・内部手順を特定。バスアクセスそのものがCPUを止めた場合、ソフトの期限では救えない。自動再試行・自動リセットを行わず、ユーザーの電源断・再投入で回復する。
- DIRTYはrecoverのFAILED/cold-requiredの派生表示で、別の復旧状態機械は持たない。正常なmain終了だけ次の番号へ進む。重複要求は既存結果を返す。再実行には電源再投入記録とCOLD再採取が必要で、新sessionだけでは解除しない。
- **4a は 0FACh を読まない・書かない**。0FACh は終端の②で recover だけが扱う (recover final §3)。
- 局所cleanupはPTE・IRQ状態・窓占有・安全に分類済みのindex/lockを戻す。BAR測定は測定前BARを復元しCommandは0102h。終端cleanup/verifyはrecoverが一度だけ実行し、その後4aからGPUを再設定しない。PLL/PFB初期化はcold-requiredで保存値書戻し0件（一時PLL補助値も中断後cleanupでは再生しない）。recoverの①GF quiesce（1850=0・1854=0・PMC割込み=0）には次の前提を適用する。quiesce 前に生の PCI config で Command.Mem=1 と BAR0 が snapshot と一致することを確認する。不一致・確認不能なら MMIO quiesce を省き FAILED に固定し、独立に安全を確認できる②（98画面）だけを行う。decode 解放を確認できなければ②でも VRAM に触れず停止する。前提成立時は①と②を呼び、S1成功を返さない。アクセス不能ならFAILEDとして停止する。
- 戻せないもの: DRAM訓練/初期化・失われたVRAM内容・PLL切替途中・副作用/書込専用レジスタ・バス停止。スナップショットを逆順に全部書く「rollback」は提供しない。
- 54h=0は安全側に保持する例外。中断時に1へ戻してlegacy窓を再取得させない。4bの承認前に1850h=1は書かない。
- 冷却・異常発熱・ケーブル・電源操作はユーザー。今回の設計作業は過去の立会い無し読取許可を新規書込許可と解釈しない。

## 7. 受入と実機手順

| 場所・段 | 合格条件 |
|---|---|
| Host デコード | ROMを含めず、BIT I・condition 12B・IO flag 9B・macro8B・PLL0x21・main5本/共有呼出先を模した合成fixture。確定した27種それぞれに長さ・作用・pc・skip/force・ログの期待列を手書きしmock busと比較 |
| Host 制御・境界 | IO flag表の最終バイト0x5DF1への分岐は拒否、終端0x5DF2はSUB8先頭として許可する境界例、NOT/RESUME、条件の真偽、SUB/CALL復帰、head44、repeat0の静的拒否・1/255・入れ子・深さ上限、選択s=3・範囲外の静的拒否と実行時の無書込み継続、PLL無解/丸め/27MHz、COMPUTE_MEM no-opを確認 |
| Host 変異 | 全位置で切詰め、count/offset/shift/port/index/opcodeを変異、循環CALL・DONE欠落・表への分岐、maskで禁止bitを変更、許可旗無し。事前検査失敗ならGPU書込0、mock例外後は後続0・cleanup実施 |
| Host 窓・PCI | PAGE跨ぎ、写像失敗/復元失敗、シリアル失敗、Command16のStatus保全、BAR probe各段失敗を注入。Mem=0/BAR0不一致時はMMIO quiesce 0件・FAILED、独立SIZE_BARS終端でもCommand=0106hを再生せず0102hをverifyすること、PAUSEDの承認待ちで期限が減らず承認済み次段で積算が再開することを確認。旧peekのR専用とKDEBUG無しNOSYSが維持される |
| NP21/W | PCI無しなのでGPU/BAR/MMIOの実機代替試験に数えない。解釈器はホスト試験のみ。既存OS32のbuild/ABI整合は確認、GPU成功を主張しない |
| 実機 A1 | ユーザー承認→BAR測定。BAR0/1/2のmask・size・復元値を記録。BAR1窓サイズとPFB容量は別欄。全資源の非重複をサイズ付きで再確認 |
| 実機 A2 | ユーザー承認→G1と前処理。cfg54/PBUS1854=0、cfg50/PBUS1850=0、98テキスト更新、BM=0、CRとhead0/1のSR/GR/MISC別名・PSTRAPSから導くRAM選択3を確認（CR3C=3xhはmain0 COPY直後） |
| 実機 A3-0〜4 | **main 0・1・2・3ごとに**書込一覧と期待readbackを提示し承認→RUN_ONE→差分保存→判断。SUB/CALL/REPEATはそのmainの承認範囲。main4はDONEのみと静的確認し書込み承認を追加せず完了確認する。N1のE81E以外の意図しない表示系書込があれば事前停止 |
| 実機 A4 | 全main成功後にユーザー承認→VERIFY内の予約VRAM試験→trial_end(normal)。結果持帰り後に4a合否。4bへ自動進行しない |

- PFB組3の必須比較: 100220=03080E0B、100224=05010405、100228=20220303、10022C=41088102、100234=00266622（票:56）。残りのPFB値はS1で全表から確定し、後の上書きがある場合は該当命令直後と最終値を分ける。
- 10020Chは上位容量フィールドを記録・解釈し、0/不合理値なら容量不明を記録し、容量照合（alias試験）だけを省く。予約済みBAR1先頭1KiBの最小試験は10020Chに依存せず実行し、容量未検証を別記してtrial_end(normal)へ進む（§9 N2）。全script・macro・sequenceに10020Chへの書込みは無く、COPY_NV_REGは読むだけなので、初期値0から容量が得られる保証は無い。BAR1サイズを実装VRAM量とは呼ばない。PMC.ENABLEも実行traceから導いた最終値と比較する。
- PLLは4000/4004・4020/4024・4038/403C・402C・680508/680578/680580、および変更した補助レジスタをreadback。レジスタ毎の読出可能マスクで比較し、WO/W1Cへ検証用の追加RMWをしない。
- VRAM最小試験: planで破棄可能と明記した予約範囲（BAR1先頭1KiBを予約できた場合）に交互55AA55AA/AA55AA55を**全て書いてから全て読む**、反転でも繰返す（Haiku `nv_general.c:1394–1422` の test_ram相当）。書く前の内容は退避するが初期化前の保全は保証しない。
- 容量照合: 10020Chの容量フィールドが非零かつ合理的な場合だけ、`min(BAR1窓,PFB申告量)` 内の2冪境界・先頭・最終DWORDへ異なる番地署名をまとめて書き、全点読戻しでaliasを検出。範囲外へは書かず、窓が小さければ全容量未検証と記す。
- PCD写像とvolatileを使い、GPUの同じ安全な読取でposted writeを排出。BAR1書込範囲と試験patternも承認資料に明記。疎な試験の成功を全VRAMセルの健全性とはしない。
- 合格: 必須PFB・PLL一致、VRAM最小試験成功（容量不明時は容量照合未実施・容量未検証を別記）、BAR復元済み、54h=0・50h=0、98テキスト維持、予期しないDENY/timeout無し、recover終端のS0検証成功（試験結果と復帰結果は別記）。失敗したscriptの残りを飛ばして合格にしない。

## 8. ファイル別変更と実装段（各0.5〜1日、sol実装→astraレビュー）

| 段 | 予定ファイル（O/以下） | 成果・出口 |
|---|---|---|
| S1 0.5〜1日 | `docs/tasks/realhw/TASK_GEFORCE6200.md`、新 `tools/tests/gfinit_fixture.h` | 本r3で確定した27種/193命令をfixture化し、ノート側ROM実バイト・全副作用・表長・main順と再検証。W行はrecoverの単一descriptorに収容。ROMは共有しない。N1のE81E条件付き許可をdescriptorとfixtureへ反映し、その他の表示有効化・W行不足等の静的拒否が解消するまでS2以降の実機許可無し |
| S2 1日 | 新 `kdebug/gfinit_decode.c/.h`、`tools/tests/gfinit_host.c` | checked decode・表検証・合成fixture、全命令の参照先/作用一覧 |
| S3 1日 | 新 `kdebug/gfinit_exec.c`、同host試験 | 制御・条件・macro・skip/force・実行予算、完全mockで27種の期待trace |
| S4 1日 | 新 `kdebug/gfinit_pll.c`、同host試験 | 0x21 parser・整数solver・低位/高位書込列、MIT表示、未解決リンク依存0 |
| S5 1日 | 新 `kdebug/gfinit_bus.c`、`kdebug/kpeek.c`、新 `kdebug/kdebug_window.c/.h` | 窓部品の共有、GF専用RW許可行・VGA別名・PCI probe、peek回帰試験 |
| S6 1日 | `drivers/serial.c/.h`、`kernel/kdebug_imports.def`、`kdebug/kdebug_main.c`、新 `kdebug/gfinit.c/.h` | checkedログ、session/phase・cleanup・RUN_ONE、imports末尾追加と生成物再生成 |
| S7 0.5〜1日 | `sdk/include/os32/os32_kapi_shared.h`、新 `userland/shell/cmd_gfinit.c`、shell登録/ビルド定義、kdebugビルド定義 | op/固定要求・旗、INSPECT表示、ABI方針に従う版更新（スロット追加無し）、KDEBUG=0/1ビルド |
| S8 0.5〜1日 | 同票、実機証拠ディレクトリ（ROMをコミットしない） | A1/A2受入。承認待ち・電源操作待ちは工数外 |
| S9以降 各0.5〜1日 | 同票・実機ログ | mainごとの受入を分割、最後にA4。固まったら停止して証拠から次の設計変更をレビュー |

- 定数は既存の三層規則でHW定義・方針上限・導出値に分ける。P2V/P2V_IO経由、必要な例外だけ `tools/check_p2v_allow.txt` に理由付き登録。生成ファイルは手修正しない。
- 各段で該当host試験と既存チェックを行い、ゲスト統合後に通常ビルドとKDEBUGビルドを確認。実機前にtraceとW行の独立レビューを必須とする。

## 9. 決定事項 — ユーザー判断の未決 0 件

- **U1・ユーザー決定（2026-10-21）**: CPU/バス停止時は当面ユーザーが実機の前で電源・リセットを操作する。書込み段は立会い時に行う。遠隔電源制御は後で検討する。
- **U2・ユーザー決定（2026-10-21）**: A1（BAR測定）→A2（準備）→script ごと（main 0・1・2・3）→A4（VRAM確認）の順に、各結果を見て次の段を承認する。この方針決定は個々の書込み実施承認を代替しない。
- **U3・ユーザー決定（2026-10-21）**: 4a は GeForce 用モニタ無しで行い、映像出力用モニタは 4b の前に決める。
- **N1・ユーザー決定（2026-10-21）**: main3 の E81E に限り SR01 bit5 の解除を 4a で許す。cfg50h=0・54h=0（PBUS1850h・1854h）を読み戻し、実行前後を含め保持することが条件。安全の根拠は cfg50h=0 で GeForce の出力が止まること（envytools bus/pci.rst:50–53）と、4a の間は 0FACh が 98 表示のまま（RGB-IN を選ばない）であること。

- **PM 決定（N2）**: 容量不明でも BAR1 先頭 1KiB の最小試験を行う。その後の容量探索・別設計への技術的進路と Haiku のライセンス確認は PM が担当し、現行承認範囲のまま探索へ切り替えない。
- **PM 決定（R10・別名不応答）**: 54h=0 で別名が応答しなければ A2 を停止し共通終端へ進み、4a を保留して設計し直す。54h=1／I/O decode 有効化で迂回しない。
- **PM 決定（R10・1854h 書込み）**: 実 ROM に該当書込みは無い。別 ROM/展開 write-set に現れた場合の条件付き記録とし、descriptor の phase・値を照合して非零または承認外なら静的拒否する。命令の黙示改変・許可拡張はしない。

- D1の命令集合は解決済み。全W descriptor、PLL係数、ROM実バイト検証は実装者の成果物であり、ユーザー判断の未決には数えない。段2bの判断を再要求しない。

## 反論

なし（0件）。


## 確定 (2026-10-21、Opus 第 3 回、PM が反映)
- G1: N1 の根拠から「98 とは別コネクタ」を外し、cfg50h=0 と 0FACh が 98 のままに置き換え (4a と recover の同文)。
- G2: 4a は 0FACh を読み書きしないを §6 に 1 行。
- G3: 契約の参照先を [realhw_recover/DESIGN.final.md](REALHW_RECOVER_DESIGN.md) に。

## 実装の後の変化 (2026-10-23、PM 注記 — 上の確定本文は書き換えない)

- **置き場** (§2・§8 の「kdebug の部品」「kdebug ビルド定義」): S1〜S6 は kdebug に入ったが、kdebug.bin が上限の 92% になったため、GeForce の部分は HWDEV の分離 (M3c 92153ea) で `/boot/hwdev.mod` へ移った。入口 (kdebug_call の op 5〜10) は核から部品へ振り分ける (M3b c1986c3)。構造は HWDEV_DESIGN.md。
- **割込み禁止の区間** (§2 の「index/data と RMW は一組の間だけ割込み禁止」): 実装は REALHW_RECOVER_ADDENDUM.md の S5 の決定 (区間を分ける) に従う。S5 のレビューの P3 で送った文言の直しをこの注記で閉じる。
- **S7** (§8 の行の「KAPI 共有ヘッダと版更新」): 先にできた `recover` は部品の private な wire ヘッダを shell が直接読み KAPI を変えていない。S7 もそれに揃える — **2026-10-23 確定**: private な `kdebug/gfinit_wire.h` を shell (`cmd_gfinit.c`) が直接読み、既存の kdebug_call の op 5〜10 を使う。KAPI の版・slot・生成元は変えない。§8 の S7 行より [GF6200_S7_SHELL.md](GF6200_S7_SHELL.md) (段 S7a・S7b・S7c) を正とする。trial の外の INSPECT は例外として通常の 1 行と終わりの行を出す (同 §3)。
- **A1 の他装置の BAR**: Ra266 の基準構成 ([RA266.md](RA266.md) §1-1) では Trident 0:8.0 と 82557 0:11.0 が「長さ不明のメモリ BAR」で拒否に当たる。根拠の設計は票 TASK_HWDEV §3-1 の BARLEN。
