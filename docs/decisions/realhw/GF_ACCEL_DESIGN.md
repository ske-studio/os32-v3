# GeForce 6200 (NV44A) の転送高速化 — 確定設計 (段 0: dword present + MTRR WC、未達時だけ M2MF)

> 出所: ~/os32-tmp/run/gf-accel/DESIGN.final.md (astra 起草 r1〜r3、Opus レビュー 2 回)。
> 状態: **現行 (2026-11-01)** — 段の状態は票 TASK_GF_ACCEL が持つ。

起草: Codex（実行モデル ID は確認できず）。2026-11-01。設計案、実装・実機受入は未実施。
対象: Ra266 / PCI 0:13.0 / 10de:0221、640×480×8、pitch=640、表示先頭=0。整数のみ、単一 CPU・協調切替。
## r2→r3 の変更
- P2-1: PTE・台帳型・native 検査を不変とし、MTRR の取得/復元と注記に縮小。§7 の6段を削除し、§8 の試験・変異も追従。
- P3-2: CPUID.80000000 の最大拡張 leaf を確認してから 80000008 を読み、非対応時は36 bit。
- P3-3: dword 転送の整列判定を OS32 の uptr に修正。
- P3-4: push DMA は H:1051–1053 の flags0=3002h・flags2=base|2 を維持し、RO 化を撤回。

## r1→r2 の変更（履歴、r3 の修正を優先）
- U1: ユーザー決定「段0を先に、足りれば終了」を採用。CPU の dword/WC で目標を満たせば GPU 加速は作らない。
- P1-転送: byte UC → dword UC → dword WC を比較する段 0 を本票の中心とし、行端・写像属性・WC 排出を具体化。
- P1-閾値: CPU の最良実測を基準に変更。レビューの帯域・所要時間は予測であり受入値にしない。
- P2-Command: 後続の状態比較に bus/devinit だけでなく kdebug/gf_stage_state.c と recover_profile.c を含める。
- P2-BM: firmware の Command=0106h・latency=60h・PCI cache line size=0 を生データで確認し、計測欄に追加。
- P2-RO: 後続の PCI 入力 DMA と push DMA を GPU 視点 RO に変更、flags0 の RO と flags2 の非 RW を対で設定。
- P2-ring: 後続の ring/notify/試験面を BAR1+80000h 以降へ移し、表示用 512 KiB の WC 域と分離。
- P3-引用: NV44A→nv44_gr_new と nv04_mmu を引用し直す。GART 不要の根拠は非 clone の物理 PCI 経路とする。
- P3-規模: fill/copy は後続の gf_accept.c に集約。現行 FD の FAT12 空き実数と段 0 の増分見積りを記載。

## 1. 現行経路・性能目標（r1 の観測は維持）
参照略号: `O/`=/home/hight/os32-v3/、`R/`=/home/hight/os32-tmp/refs/geforce6200/、`N/`=R/nouveau/drivers/gpu/drm/nouveau/。
`H`=R/xf86-video-nv/src/nv_hw.c、`X`=同 nv_xaa.c、`E`=R/xf86-video-nouveau/src/nv04_exa.c、`F`=R/nouveau-v6.1/drivers/gpu/drm/nouveau/nv04_fbcon.c。
資料の版・許諾は R/INDEX.md・LICENSES.md。以下の nouveau .c は MIT、頭書きなしのヘッダは写さない。
| 経路・契約 | 実物と意味 |
|---|---|
| アプリ/gshell → RAM CLIENT | O/userland/rust/libos32gui/src/draw.rs:134,141,184、gshell/src/chrome.rs:64–69、O/gfx/gfx_core_framebuffer.inc:77 |
| dirty → present → VRAM | O/userland/gshell/src/wm.rs:879–901 → O/gfx/gfx_vram.c:43–62,255 → backend_geforce.c:108–112 → gf_present.c:29–44 |
| 現行の転送 | 1 byte ごとの volatile store、行ごとの point、最後に BAR0 read。既存 gf_present.o は text=277 B、内側は `mov %bl,(%ecx)` |
| fill/blit | gfx_core_api.inc:65–76 は backend 委譲。調べた GUI 呼出側には HW 口の利用なし。RAM 描画・合成は段 0 後も CPU |
| 他表示との共存 | O/docs/decisions/realhw/RA266.md §1-1。98/Trident/Cirrus を保持、GF の BAR の再配置や legacy decode の変更を足さない |
- 既存票の「加速なし」と実コードの食い違いは r1 同様 0 件。RAM CLIENT と VRAM CLIENT の契約差を隠して HW fill/blit を公開しない。
- 全面は 307,200 B。段 0 は CPU→VRAM を改善するが、RAM 描画・窓合成・dirty 管理の時間は消えない。
- 受入目標: 固定台本の窓移動/スクロール/カーソルの更新処理 p95≤33 ms、全面 present p95≤16.7 ms、表示一致・既存回帰なし。
- `Tbest` は byte UC/dword UC/dword WC のうち、その実機で成立した最速の CPU 経路の全面 present p95。小矩形の退行も別に判定する。
- 目標を満たせば段 0 で終了。未達なら内訳を調べ、転送が主因の場合だけ M2MF の試作へ進む。RAM 描画が主因なら GPU 転送を作る根拠にしない。
- 後続の本番採用は M2MF 全面 p95≤0.8×Tbest、典型操作の改善、小矩形は CPU 選択で退行なしが条件。交点が無ければ M2MF を製品に載せない。
- P100 の参考値は `Tcpu266×266/100 + Tpci + Tgpu`。P100 には MTRR が無いので Ra266 の WC 値を P100 の保証にしない。実機未測定の帯域を合否に使わない。

## 2. 段 0a: dword present
- 変更点は O/drivers/video/geforce/gf_present.c:36–37。クリップ、各行の point、pitch、計数、正常終了の BAR0 read の契約は維持。
- 手本: R/xf86-video-nouveau/src/nouveau_exa.c:29–39 の NVAccelMemcpyRect、:375–386 の CPU fallback（MIT）。src/dst pitch を独立に進める線形コピーを採る。
- 同手本は memcpy に委譲し、dword/byte の具体列は規定しない。以下の幅固定 store と端処理は OS32 独自。全行連結の最適化は行ごとの point を失うので採らない。
- 8bpp は palette index の byte 列、変換しない。各行 `s=client+y*client_pitch+x`、`d=display+y*display_pitch+x`、`n=w` とする。
```c
/* クリップ済みの一行。d の実アドレスで整列、s の整列は要求しない。 */
while (n && ((uptr)d & 3)) { *d++ = *s++; --n; }
while (n >= 4) {
    u32 v; __builtin_memcpy(&v, s, sizeof v); /* RAM の4 byteだけ、alias/alignment違反なし */
    *(volatile u32 *)d = v; s += 4; d += 4; n -= 4;
}
while (n) { *d++ = *s++; --n; }
```
- 行頭 byte は最大 3、末尾 byte は最大 3。幅 1–3、x=639、奇数 src pitch、クリップ後の幅でも矩形外を読まず書かない。VRAM の read-modify-write はしない。
- 行が終われば元の行頭からそれぞれの pitch だけ進める。dst pitch=640、src pitch は現行引数を尊重。w/h≤0・完全画面外は既存の no-op。
- `__builtin_memcpy` は定数 4 B の load 用。生成コードで libc 呼出し無し・整列した 32bit store・SSE/MMX 無しを確認し、通常経路に診断分岐を置かない。
- point 失敗は以後 BAR0/BAR1 に触らず、書けた行だけ present_bytes に足して失敗を返す。WC 使用時の失敗後の排出は §4 の CPU 側停止列が担当する。

## 3. 段 0b: 表示域 512 KiB の MTRR WC
メーカー根拠は [Intel SDM Vol.3A](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf) §11.3、§11.5.2、§11.11.1–4/7–8、§11.12（この版の節番号）。CPU 命令のみの MTRR 実装は [HW3](../../CONSTRAINTS.md) の既存実装参照任意に従い、SDM から起こす。
### 3.1 適用条件と資源
- CPUID の存在を EFLAGS.ID の反転・復元で確認、leaf 0/1 を確認して GenuineIntel、EDX.MSR(bit5)・MTRR(bit12) を検査。非対応なら MSR に触らず dword UC。
- IA32_MTRRCAP(0xfe) の WC(bit10) と VCNT[7:0]、IA32_MTRR_DEF_TYPE(0x2ff) の E(bit11) を確認。E=0、WC 無し、空き無しは BIOS 方針を変えず UC に残す。
- MAXPHYADDR は CPUID.80000000:EAX ≥ 80000008h を確認した場合だけ CPUID.80000008:EAX[7:0] を読む。当該 P6 で対応 leaf が無ければ 36。MTRR の上位物理 bit も計算し、予約 bit は 0。
- 対象は `[BAR1.base, BAR1.base+0x80000)`。32bit memory BAR、raw bit3=prefetchable、512 KiB 整列、decode 長≧512 KiB、表示 bytes=0x4b000 を検査。BAR1 は config 0x14（BAR0 は 0x10）。
- 実測資料 E15=~/os32-tmp/evidence/2026-10-15/ra266_snapshot/ra266_snapshot_2026-10-15.txt:228–232 は BAR1 raw=30000008h、Command=0106h、cache-line byte=00h、latency=60h。運転中は既存 devinit により Command=0102h。段 0 は Bus Master を有効にしない。
- MTRR は `[30000000h,30080000h)`。BAR0=24000000h、BAR2=25000000h、98 の低位 VRAM、Trident の 20000000h/20400000h/20800000h と離れる（RA266.md:51）。
- 数字の一致だけに頼らず、既存資源台帳の BAR1 所有区間内であることと、RAM・他装置・内蔵表示の予約との非交差を使う。サイズ probe はしない。
- 全可変 MTRR の V=1 を読み、対象と重なる非 WC があれば適用しない。既存 WC が全域を覆う場合は借用（可変組は無変更）。部分 WC のみなら今回は UC 経路に残す。
- 重なりが無い場合は最小番号の V=0 の組を 1 個借りる。元 PHYSBASE/PHYSMASK を無効 bit 以外も丸ごと保存、他の組・固定 MTRR・DEF_TYPE の元値を保持する。
- `addr_mask=((1ULL<<MAXPHYADDR)-1)&~0xfffULL`、BASE=`(base&addr_mask)|1`、MASK=`(~(0x80000ULL-1)&addr_mask)|0x800`。P6/36bit の例: BASE=00000000:30000001、MASK=0000000f:fff80800。
### 3.2 PTE は不変、表示域の実効 WC を注記
- PM 確認済み（REVIEW.r2 P2-1、SDM 表11-6）: Ra266 の P6 は PCD=1/PWT=0 と MTRR=WC の組合せで実効 WC。現行の表示75頁・supervisor/PRESENT/RW・PCD=1/PWT=0/PAT=0 を保持する。PWT=1（PCD|PWT は UC を強制）の PTE は作らない。
- PAT 対応 CPU は既存 PTE が選ぶ PAT2=UC- を読み取り確認し、不一致なら WC 不適用。BAR0 の実効 UC、BAR1 の対象512 KiBの所有・非交差・他 CPU alias 不在も開始前に確認する。PAT/PTE は書き換えず、表示外の未写像域を128頁へ広げない。
- 台帳には「BAR1 の表示域は MTRR で WC」と注記するだけ。cache 値・区間・owner/res_mask/永久予約・DISPLAY surface は不変。WC 型、部分域分割 API、surface 同期、保存 PTE、native の状態別検査は作らない。既存の platform/native の写像検査を保つ。
- BIOS WC 借用時は PTE 不変のため byte/dword の実効 UC 比較を作れない。その比較を理由付き SKIP とし、WC の値を UC として記録しない。終了時も BIOS の WC を保持する。
### 3.3 設定・復元の順（単一 CPU、SDM §11.11.7–8）
| 順 | 操作と前提 |
|---|---|
| 0 | 描画を止める。元 MSR と EFLAGS/CR0/CR4 を保存、全検査・資源準備を終了。以後の区間で割当・ログ・point・協調切替をしない |
| 1 | IF 保存→CLI、CR0.CD=1/NW=0→WBINVD、CR4.PGE が立っていれば落とす、CR3 reload で全 TLB を破棄 |
| 2 | DEF_TYPE の E だけ clear（元の type/FE は保持）。選んだ PHYSMASK の V を clear→PHYSBASE→新 PHYSMASK(V=1) を書く |
| 3 | MSR readback→DEF_TYPE を元値に戻す→WBINVD→全 TLB flush→CR0 元値→CR4 元値→EFLAGS 元値。成功状態を公開する |
| 戻し | 同じ 0–3 の枠で借りた組の保存 BASE/MASK を復元。BIOS WC 借用時は MSR を書かず、CPU 側排出後に借用状態だけ解除する。PTE/台帳型は常に不変 |
- PTE を変更せず、MTRR 更新だけを上記 cache/TLB の枠内で行う。PGE=0/1 の両方を模型で試す。BIOS WC 借用の開始は設定列を省き、停止時の CPU 側排出は identity 失敗時も実行する。
- readback 不一致は同じ枠内で保存状態へ戻し、復元確認後のみ dword UC に戻る。復元できなければ GF 起動失敗、WC 成功や安全な fallback として継続しない。
- CPUID/MTRR 判定失敗は通常の能力無し。MTRR 非対応 CPU で CR4/MSR/WBINVD の設定列に入らない。SMP・汎用 PAT allocator は今回作らない。

## 4. 排出、部品境界、寿命
- 正常 present の最後は compiler memory barrier→既存の volatile BAR0+10020ch read→完了計数。SDM §11.3（上記リンク）の UC read/write は部分 WC buffer の排出契機であり、同一装置への read は既存の PCI posted-write 排出経路でもある。
- SSE の SFENCE を要求しない。BAR1 の読み戻しも各行の drain も足さない。末尾 byte の残った WC buffer も最後の BAR0 read までを present 時間に含める。
- point/identity 失敗後は BAR0 を追加で読まず、CPU 側の §3.3 の WBINVD を含む停止・復元を通す。送信済み書込みの取り消しは主張しない。
```c
/* arch/x86/kernel/mtrr_wc.c が命令列、gf_wc.c が GF の適用条件・寿命を所有。 */
struct x86_wc_lease { u32 slot, owned, active; u64 old_base, old_mask; };
/* PTE/台帳は不変。release は借用時も CPU 側排出を行う。 */
int x86_wc_acquire(u32 base, u32 bytes, struct x86_wc_lease *);
int x86_wc_release(struct x86_wc_lease *);
/* geforce_platform に加える kernel 所有 callback。0=UCで継続、1=WC、負=致命的。 */
int (*wc_start)(void); int (*wc_stop)(void);
/* backend ops: .present_rect=geforce_present, .fill_rect=NULL, .blit=NULL */
```
- lease・実効 WC の状態は kernel の gf_wc.c に 1 個、部品は callback だけを持つ。KAPI/SDK は不変、内部 OPS_VERSION を更新し核と geforce.mod を対でビルドする。
- 開始: 既存 video_start 成功→wc_start→最初の present。active の同一モード init は何もしない。start の UC fallback は表示可能として扱う。
- 終了/CUI/再設定/abort/部品回収: 新規描画停止→最後の正常 drain（有効時のみ）→wc_stop→gf_production_quiesce→98 route。geforce_body.c:8 の共通 stop に置き module.c:56 の出口も覆う。
- **今回は GUI 終了時に戻す**。永続 BAR 予約があっても MTRR 枠と実効 cache の所有期間を表示の寿命に合わせ、診断・回収時に取得前の状態へ戻すため（BIOS WC 借用時は WC のまま）。再 GUI は再取得、stop 二重呼出しは no-op。

## 5. 計測の口と進む条件
- 受入用 gf_accept.c が実際の gf_present_rect を呼び、その直前/正常 drain 後を既存 sys_time_now(u32 lo,hi) で測る（hwdev/gf/native.c:66、部品 import 済み）。失敗 sample を成功群へ混ぜない。
- 同一ソース・同一最適化の受入 3 ビルド: byte UC / dword UC / dword WC。WC 不成立および BIOS WC のため UC を作れない比較は理由付き SKIP、実効型を確認してから比較。MTRR 切替時間は転送時間から除外する。
- 固定 RAM パターン 0..255、全面640×480・320×200・640×16・32×32・16×16・(x=1,w=63)・1×480・端1px。ウォームアップ10回後100回×3、個々の経過 µs を RAM に保存する。
- p50/p95 は昇順の nearest-rank（100個なら50/95番、合算300個なら150/285番）。中央値・p95・最大・転送 byte・point 回数・失敗数を出す。小矩形は時計の空測定も記録する。
- gshell の既存 dirty/present 境界とイベント処理入口/更新完了に受入時だけ計時を入れ、RAM 描画/合成と present を分離する。同じ窓・移動幅・スクロール量・入力間隔で3方式を比べる。
- PCI config 0x0c の cache-line size は dword 単位、0 は未設定として記録（CPU cache line と混同しない）。Command/latency/BAR raw、CPUID family/model/stepping、MTRRCAP/全組/DEF_TYPE、PAT有無、元/現 PTE も採る。
- CPU cache line は対応 leaf4、なければ P6 の leaf2 descriptor を [SDM Vol.2 CPUID 表](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf)で解読（raw も保存）。CLFLUSH 非対応の P6 で leaf1 EBX の値を line size として使わない。不明なら不明と記す。
- 受入用 platform.report callback を kernel 側 gf_wc.c に持たせ、計測完了後の record を serial_puts（O/drivers/char/serial.h:278）へ出す。計時中のシリアル出力を止め、常用版に sample 配列を含めない。
- tools/accept/gf_accel.py は保存済みシリアルログから版/設定/個別時間を照合し集計。実機操作は PM→ノート側セッション、表示の判定は RA266.md §3-1 のカメラ、数字はシリアル。

### 5.1 受入版の作成と入替 (S14〜S16/T3)

PM判断(2026-11-01): `GF_PRESENT_BYTE` は受入対象だけが定義し、既定の dword の
機械語は不変とする。通常の gf_present.o の .text を追加前後で比較する。
受入backendだけが `gf_accept_transfer` を経由し、実物の gf_present_rect を呼ぶ。
受入用の配列/計時は常用のgeforce.mod/kdebug.binに含めない。

作業木の環境(CROSS_DIR/TMPDIR/HOST32_RUNNERS)とビルド済みkernelを用意する。
PMは同じソースで識別子(例 `f5468c6c-gfa-r2`)を固定し、各MODEについて次を実行する。
ビルド/試験は直接実行する。自分から make all は実行しない。

```sh
MODE=byte-uc  # 次の2回は dword-uc / dword-wc
GF_ACCEPT_ID=f5468c6c-gfa-r2 GF_ACCEPT_MODE="$MODE" \
  make -j4 geforce-accept gshell GEFORCE_ACCEPT="$MODE" GSHELL_FEATURES=gf_accel_accept
```

部品は `build/out/geforce-accept/$MODE/geforce.mod`、GUIは
`userland/gshell.bin` (BUILD_OUTを替えたときはその下のresident/gshell.bin)。
GUIの出力先は通常と共用なので方式ごとに別名で保管する。
`GF_ACCEPT_ID` を部品/gshellで一致させ、gshell環境の `GF_ACCEPT_MODE` も同じ方式にする。
受入部品のkernel pairは常用の `build/out/vmkernel.lz4` と同じ。kernelは通常版を使う。
取り違え防止に配布物のCRC/ハッシュも保管する。常用へ戻すGUIは featureなしで組み直す。
通常像を後で組み直した場合は kernel Image CRC が変わり得るため、受入3部品も
その kernel に対して再作成し、部品の PAIR_CRC と照合してから配布する。

PM→ノート側セッションで、kernel/通常部品の対が揃った状態から、CUIでhsyncを使って
選んだ `geforce.mod` を `/boot/geforce.mod`、対応GUIを `/bin/gshell.bin` に入れ再起動。
方式を替えるたび同じ経路で入替え、再起動してシリアルを方式別ファイルに保存する。
NP21/Wの設定変更・実機の操作はこの実装者は行わない。常用へ戻すときも通常の
geforce.mod/gshell.binを同じ経路で戻す。詳細の配備権限は ROLES §0 に従う。

GUIの最初の通常present後に、0..255の固定RAMパターンが表示される。
GFA1 pattern行から測定/記録の送出中に RA266 §3-1 のカメラで確認し、同じID/方式で
記録する。計時の外で各矩形をreadbackし、送出が終わればCLIENT全面を戻す。
1起動で合成転送の計測は1回だけ。固定台本の3操作(窓移動/スクロール/カーソル)は
それぞれ別起動で、同じ窓/移動幅/スクロール量/入力間隔を使い、10更新warmup後に
300更新のGFW1が出るまで記録する。GUI時間はwm_cycle内の更新に加え、
その外のCOMMIT/Pumpによる実転送も採る。周期外は転送だけを独立sampleにし、
total=present、event/composite=0とする。次周期にアプリが描く時間・待ち時間は
含めない。総合の操作応答は実機台本でも見る。

```sh
python3 -B tools/accept/gf_accel.py --build-id f5468c6c-gfa-r2 \
  --pair-crc 0x1234abcd --require-wm byte-uc.log dword-uc.log dword-wc.log > comparison.json
```

`--pair-crc` はその日のkernel Image CRCの実値に置換する。保存済みログだけを読む。
nearest-rankの100個/300個を別集計し、時刻失敗は数えて除外する。BIOS WC下のUCや
WC不成立は理由付きSKIPで、比較の合格と数えない。レコードの欄は
[gf_accept](../../design/drivers/video/geforce/gf_accept.md) と
[gf_accel](../../design/tools/accept/gf_accel.md)。実機はGUI終了後の元MSR/PTE復元、
再GUIと他表示への復帰、既存ゲスト回帰も別に確認する。host結果だけで受入としない。

## 6. 規模と現在の成果物（読み取り実測、再ビルドなし）
| 項目 | 2026-11-01 の値・段 0 の見積り |
|---|---|
| geforce.mod / kdebug.bin | 39,596 B / 127,748 B。gf_present.o text=277 B。段 0 の部品増分は 0.5–1.5 KiB、kernel の MTRR/寿命接続は 2–4 KiB の設計見積り（未ビルド） |
| RAM | 保存 PTE・台帳差分は不要、lease/状態等で1 KiB未満の見積り、DMA bounce 不要。受入版のみ300 sample×u32程度を追加 |
| 2HD images/os32_boot.img | 1,261,568 B、1024 B/cluster、全1221・空85 cluster = **87,040 B (85 KiB)**。64 KiB下限まで21 KiB |
| 1.44 MB images/os32_boot144.img | 1,474,560 B、512 B/cluster、全2849・空592 cluster = **303,104 B (296 KiB)** |
- FAT12 の BPB から data cluster 数を求め、cluster 2..末尾の FAT entry=0 を数えた。像の SHA256: 2HD=`332da004480afb4ca4b5a77cc714f6abe7e0ee4883611120719cb98b541daeb9`、144=`760a5e48259fde940488eb46ce78330e4f391ddf4c1fe1ac636b1ffcfbe14c89`。
- GF は MINIMAL/起動FDに含む。kernel圧縮差・cluster丸めがあるため上の増分を空きから単純減算して合格にしない。実装後 make all の2像で空き≧64 KiBを再確認。
- 常用 MTRR は kernel の専用 OBJS、gf_accept は受入構成だけ。hwdev/gf wildcard へ加速源を追加しない。kdebug の native は変更せず、image/mem/span（256/256/512 KiB）を実測する。

## 7. 段 0 のファイル割当て（+は新規、1行=1ファイル=1段=1実装者）
| 段 / ファイル | 機能・概算行数、依存 |
|---|---|
| S0 +arch/x86/kernel/mtrr_wc.h | lease/戻り値契約、30–45、先行 |
| S1 +arch/x86/kernel/mtrr_wc.c | CPUID・選択・設定/復元・借用時排出、160–230、S0後 |
| S2 gf_present.c | dword/端処理、+20–35、S1と並列可 |
| S7 geforce_module.h | platform callback・内部版・512KiB定数、+10–20、S0後 |
| S8 geforce_platform.c | callbackの登録とBAR1表示域のMTRR WC注記、+5–15、S7後 |
| S10 +drivers/video/geforce/gf_wc.c | kernel所有のlease/適用条件/寿命と受入report、100–160、S1/S8後 |
| S11 backend_geforce.c | video_start後のwc_start接続、+5–15、S10後 |
| S12 geforce_body.c | 共通stopのwc_stop接続、+5–15、S10後 |
| S13 build/kernel.mk | S1/S10のkernel OBJS登録、+2–5、各leaf後 |
| S14 +drivers/video/geforce/gf_accept.c | 3方式・矩形・readback・計時、100–160、S2/S10後 |
| S15 build/geforce-module.mk | 受入構成/転送方式選択、+10–20、S14後 |
| S16 userland/gshell/src/wm.rs | 受入時だけイベント/合成/presentの計時、+20–40、S14後 |
| T0 +tools/tests/test_mtrr_wc.py | fake CPUID/MSR/CR・PTE不変とCHECK/変異、160–230、S0後に並列 |
| T1 tools/tests/test_geforce.py | 転送store trace・端・WC寿命と既存写像維持、+60–100、S2/S7/S10後 |
| T3 +tools/accept/gf_accel.py | シリアル記録の統計/版照合、80–130、S14後 |
- r2 の S3–S6（mm の型/分割/同期）、S9（native 状態別検査）、T2（台帳変更試験）の6段を削除。21段→15段、追跡のため残る段の ID は維持する。
- Cファイル名のみの行は drivers/video/geforce/（hwdev/arch/mm/build は表記どおり）。各段は半日以内〜1日、共有ヘッダを先に確定。既存fixtureが別ファイルならその1本を追加子段として指名する。
- Tx は各段 make kernel＋触った像/直接試験、最後に make all・全差分候補 MUTATE=0・触った変異・lint、静的5本と取り込みは ROLES §0。本依頼では実装・ビルド・機器操作をしない。

## 8. 受入 → 試験と変異
| 受入 | 観測・試験 → 1行変異 |
|---|---|
| byteとdwordの出力一致 | src/dst整列0..3、幅1..9/639/640、独立pitch、クリップ、sentinel、store幅/番地のtrace → n>=4をn>0にする |
| 行ごとの契約維持 | point失敗位置別、行外無書込み、正常時最後のBAR0 read、計数 → 行pointを先頭1回に移す |
| 能力無しはUC | T0: fake CPUIDなし/MSRなし/WCなし/E=0/空き無し/競合、MSR write無し → MTRR bit検査を削る |
| 拡張leafの範囲を守る | T0: 80000000hの返値が80000008h未満なら後者を読まず36bit、対応時だけ幅を採用 → 最大拡張leafの比較を常に真にする |
| BIOSを保存 | T0: V=0選択、36bit mask、既存WC借用時MSR無変更、他組/固定域不変、readback失敗rollback → mask上位を0にする |
| 設定順が正しい | T0: fake privileged traceでCLI/CD/WBINVD/TLB/E/BASE/MASK/戻しを検査、PGE両値、PTE write無し → 2回目TLB flushを削る |
| 既存写像のままWCを判定 | T0/T1: PTE/台帳型/区間/surface不変、表示75頁、BAR0/他表示不変、PAT2=UC-以外を拒否 → PAT2適合判定を常に真にする |
| 借用時も排出する | T0: BIOS WC借用の通常/identity失敗の停止でCPU側排出あり・MSR無変更 → 借用時の排出を省く |
| UC比較を偽らない | T3: BIOS WC下ではUC比較を理由付きSKIP、WC測定をUC群に混ぜない → UC群の実効型検査を省く |
| 全出口で元へ戻る | T1: 初回/再GUI/同一init/再設定/abort/初期失敗/二重stopの原状態比較 → 共通stopのwc_stopを省く |
- NP21/W の MTRR 実装は追加・改造しない。fake MSR は host だけ。ゲストは GF 不在の拒否、既存 PC98/PEGC/Cirrus GUI・CUI 復帰・媒体/部品のpairingを確認し、WC成立には数えない。
- Ra266 は3方式の画面/byte照合（計時外readback）、p50/p95、MTRR/PTE実値、GUI終了後の原状態一致、再GUI、他表示への復帰を確認。WC能力が足りなければ段0bは未受入と明記する。
- hostは実CPUのWC/PCI排出を証明しない。§1の目標判定は実機の正常sampleと同じ版のカメラ確認に限る。現在は全て未実行。

## 9. 段 0 未達時だけの後続 M2MF（r1 §3–4 を縮約・修正）
- 1チャネルDMA push、静的PRAMIN/RAMHT/PGRAPH/PFIFOの列は r1 §3.2–3.4 / H:983–1053,1166–1419 / X:203–281 を出発点とし、再開時に実装票へ展開して再レビューする。未実証の列を通過済みとしない。
- NV44A は N/nvkm/engine/device/base.c:677–696 → gr/nv44.c:78–107（initはnv40、M2MF=0039）。mmuはbase.c:688のnv04。PCI物理入力は dma/usernv04.c:46–68,97–127 の非VM・非clone経路なのでGART不要。
- WC=[0,80000h)、ring=[80000h,88000h)、notify=[88000h,88020h)、試験面=[90000h,db000h)。BAR1先頭1MiB写像、ring等の実効型はUC、PRAMIN別予約。BIOSの広いWCがある場合もringの実効UCを確認する。
- 32KiB連続・整列bounce、GPU視点ROのPCI snoop入力={2703dh,7fffh,phys,phys}。入力のROはflags0|4000h、flags2のRW bit2hを付けない（usernv04.c:119–127）。push DMA は H:1051–1053 の3語を維持し、flags0=3002h、limit=7fffh、flags2=80000h|2h とする。RO 7002h は今回は採らず、手本から外すなら理由を示して後続票でレビューする。宛先/notifierは書込み可能。
- M2MFはE:399–427の0184/0188結線→030cから8word→notify/kick、GET==PUTかつPGRAPH idleまで待つ。bounce再利用・CPU fallbackの前にGPU停止を確認し、停止不能なら資源保持して既存失敗経路へ。
- BM遷移0102→0106→0102はbus.c:29,109/devinitの期待値に加え kdebug/gf_stage_state.c:63・recover_profile.c:221–231 を扱う。recoverへ渡す前にGPU停止と0102 readbackを完了させる境界を固定し、0106のまま旧profileを走らせない。
- fill/copyはgf_accept.c内の非表示面検査だけ（F:32–73）、HALの能力は公開しない。IFC/SIFM/カーソル/3D/Xv/複数モードは範囲外。GPU停止→WC復元の順に寿命を拡張する。
- 未決のユーザー判断は0件。性能・WC可否・容量は観測事項。段0合格なら本節のファイル・DMA buffer・objectは作らない。

## 反論
- 今回（r2→r3）は0件。以下は r2 の履歴で、REVIEW.r2 で妥当と確認済み。
- P3-引用（前回1件）: usernv04.c:52–60 の clone はページ表を参照し、:97–102 はVMをPCI/RWへ変換する経路であるため、それ自体をGART不要の根拠にはしない。物理PCI/非cloneの:46–68,104–127へ引用を訂正した。

## 確定 (2026-11-01)
- r3 を確定版とする。Opus の第 2 回 (条件付き Approve) の条件 P2-1・P3-2・P3-3・P3-4 は r3 で反映済み (PM が r2→r3 の変更行で確認)。P2-1 の根拠は PM が確認: LKML 2001-03 (https://cs.helsinki.fi/linux/linux-kernel/2001-03/0488.html) — SDM 表の「PCD=1・PWT=0・MTRR WC → WC」。
- ユーザー決定 (2026-11-01、原文の選択肢): 「段0を先に、足りれば終了 (Recommended)」。
- 目標の値 (§1 の 16.7ms / 33ms) は PM の案で、ユーザーの決定ではない。「足りる」かは段 0 の実測をユーザーに示して決める (2026-11-02 改め、ユーザー指摘)。
