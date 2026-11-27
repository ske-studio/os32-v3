# sys/kern/irq/irq.c — 設計
> 状態: **現行 (2026-11-07)** — 出所: DEP_DIR_B_DESIGN・DEP_DIR_D1_DESIGN (D1 で移した)
ソース: `sys/kern/irq/irq.c`

- IRQ 表、登録数、共有配送、storm/quarantine は kernel 所有。線一覧・予約・EOI は platform 所有。
- `irq_dispatch` は claim → deliver 時だけ共有配送 → needs_finish 時だけ complete → 未処理報告。claim 失敗時は配送も完了もしない。
- 互換 `irq_finish` は claim → complete → 報告で、dispatch と併用しない。PC98 は固定線を claim せず、IRQ0/7 等の既存 ASM だけが完了する。
- PC98 IRQ15 は配送後に ISR を読み、IRR 選択へ戻し、実 IRQ なら slave → master、spurious なら master のみ。handled から EOI を決めない。
- `irq_topology_check` を PIC 初期化直後・unmask 前に実行。不一致は診断後停止。32線以上の状態は irq_line が正、u32 診断は下位32線だけ。

- irq.h の「全登録者が tick 回収」は **FDC の排他完了通知だけ例外**と明記。FDC は既存 timeout/retry で戻す。NIC 等の tick 保証は緩めず、FDC の新規 poll 処理は作らない。

常駐で解除しない FDC を例外

## PC-98 IRQ12 共有の移行案

2026-11-27 起草。IRQ12 は slave IR4。通常の共有登録を使い、PCM・V86・既存 PCI 利用者の順序に依存せず各登録者を毎回一巡する。線の割当ては PC-98、装置の判定は sound、ゲストへの注入は V86 が所有する。KAPI と共通の handler 型は現行のまま使う。

### 物理線とゲストへの配送

1. PC-98 の動的線表に 12 を加え、要素数を 9 から 10 にする。既存の IRQ12 スタブはフレームを現行どおり保存し、IF=0 で一度だけ `irq_dispatch(12)`。物理 EOI はその complete が slave → master に各一回送る。スタブ・装置・仮想 PIC は物理 EOI を追加しない。
2. PCM と実音源の V86 用 handler は各々 `IRQ_F_SHARED` で登録する。PCM は R2.INT と I24.PI に従って ack。V86 側はセッションが所有する実音源の要求を非破壊で読み、該当すればセッション内の pending を立て IRQ_HANDLED。どちらも自分の要求がなければ IRQ_NONE。同時要求では両方が処理され、全員 NONE なら既存の storm 判定が働く。
3. スタブの dispatch 後、保存 EFLAGS.VM=1 の復帰点でのみ pending をゲストフレームに反映する。仮想 PIC の IMR・ベクタ設定と既存のスタック範囲検査を通す（既定 INT 14h）。物理 EOI 後、ゲスト ISR が実音源を読んでタイマ・FIFO の要求を解除し、ゲストの EOI は仮想 PIC に渡す。ホストはゲスト所有のラッチを先に消さない。
4. エッジが重なった場合、一巡後に立った別装置の要求には次のエッジが保証されない。PCM は tick で回収する。V86 はセッション中の既存タイマ復帰点でも音源状態を一回確認する。非 VM フレームでは pending を保存し、VM に戻れる時に再試行する。仮想 IMR で保留した要求は IRR に残し、許可後に配送する。音源要求が持続中は同じ要求を入れ子にしない。具体的には仮想 ISR の IR4 が立つ間は再注入を抑止し、ゲスト EOI 後に状態を再確認する。タイマ反射と音源の同時準備では一復帰につき一ベクタとし、後続を保留する。
5. 登録・解除は非 IRQ 文脈で行い、PIC のマスクは登録数と既存の隔離・storm 規則が所有する。V86 は実音源を確保してから enter 前に登録し、登録失敗なら入場を巻き戻す。通常終了・タイムアウト・fault/longjmp の全経路で当該セッションの登録だけを一度外し pending を破棄する。終了が IRQ 文脈なら先に active を落とし、解除を前景 unwind に寄せ、完了前の再入を防ぐ。PCM が残れば IRQ12 は開いたまま。IRQ2 の VSYNC は既存の寿命に従う。

### 実音源とネイティブ FM

起動時の既存 opn_probe の OPNA / OPN / ABSENT / UNKNOWN を常駐側に保持する。OPNA だけでは 86 型 FIFO の証明にならない。86 型を示す機能 ID・ポート配置（io_sound.md の A460h、018xh/028xh）を照合し、確認済みのボードに限って FIFO 制御 A468h の許可 bit5 と要求 bit4 を確認する。26K は OPN タイマのみ。開放バス 0xff を要求として扱わない。FM 状態は検出したベースでタイマ A/B を読む。OPNA のタイマフラグは IRQ 出力許可と独立なのでゲストが書く 27h・29h の許可状態との一致を確認する。shadow が要る書込みは V86 I/O 経路で実チップへ同じ値を渡し記録する。ゲストが直接 I/O する構成でもこの観測を保証できることを S1 の条件にする。現行 v86_io.c の 0188h–018Fh は TSS の I/O 許可範囲なので、単に #GP に記録を足しても観測できない。制御レジスタ選択／データのトラップが必要な構成では実ポートへの転送順、BUSY ポーリングを含むゲストの演奏速度を現行像と比較し、偽反射ゼロと素通しの受入を両方満たしてから S2 に進む。

実音源ありならゲストが実ポートを操作し、native FM とは既存の legacy 所有権で排他にする。ABSENT は物理音源 handler の登録ゼロ。UNKNOWN は診断に残し、プローブが発生源を止められなければ使用開始を失敗にする。将来のソフト 86 は仮想要求を同じ pending → 仮想 PIC に渡せる。物理 IRQ12 の到着だけで仮想音源を発火させない。

FM の能動プローブは PCM・PCI・V86 の初回 IRQ12 登録前、線が閉じた起動段階で一度だけ行う。後日の attach はその結果を再利用し、稼働中の線をマスクしない。ネイティブ側は machine の共有線方針を HAL 内部から受け、PCM のビルド定数への依存を外す。既存 opn_chip の OPNA 29h の IRQ 抑止、共有 OPN の 27h bit2–3 抑止と能力表示を適用する。LGY-98 の INT5 拒否を維持し、他の対応線の登録と IRQ13 マウスを退行検査に含める。

### 段と受入

| 段 | 対象と生成物 | 先に失敗を示す検査と完了条件 |
|---|---|---|
| S1 実音源の識別・所有 | fm_body.c、opn_probe.c、opn_chip.c、FM 常駐 façade・内部部品口、V86 の I/O 経路。kernel・fm.mod・v86.mod を一組にする | FM の模型で 86 / 26K / 無 / 不明、IRQ 出力禁止のタイマ、FIFO 単独、再 attach を区別。再 attach が PCM 稼働中に IMR を書く変異、OPNA を一律 86 とする変異を検出。実音源でゲストのポート操作がそのまま届く |
| S2 共通配送と V86 | irq_policy.c、platform_irq_limits.h、isr_stub.asm、legacy/v86.c・v86.h・v86_pic.c。PC-98 kernel・v86.mod / 診断像 | 実 dispatch・PIC I/O 模型で PCM の代役と V86 を両登録順で試す。各単独／同時／なし、同時の両 callback と EOI 各一回、片方解除と最後の解除を検証。VM=0、IMR 保留→許可、連続した同一要求、故障終了・次セッションを試す。先着で打切り・無条件反射・終了時の直接 mask は赤になる |
| S3 PCM 経路切替 | pcm_cs4231.h・pcm_cs4231.c、pcm_hw.c、pcm_module.c と E4 診断・計測の IRQ10 表記。pcm.mod と対応する kernel / 媒体 | 実 open・失敗巻戻しの I/O 模型で 0F40h=0x22、DMA1、IRQ12 登録を同時に確認。0x1A、IRQ10 登録、PCM だけでゲスト反射、PI の二重 advance、tick ack 削除の変異を検出。HAL と旧 advance の双方で IRQ 喪失後にも波形位置と drain を回復 |
| S4 統合受入 | S3 と同一 SHA・CRC の媒体、対応する部品、診断計数と音声記録 | PM が下記の NP21/W・Ra266 の窓を記録する。実 86 での素通しは 86 搭載構成の結果で判定 |

NP21/W は既存の SNDboard=0x64 trial ini を起点にする。PM は CS4231 と 86 の双方が検出できる構成か記録し、必要なら各音源用の ini を分ける。0F40h 読戻し、IRQ12 到着増分、IRQ 側 PI 受理、wave の played 増加、/api/sound の非無音 frame を同じ窓で採る。86 のタイマを使うゲストで演奏・反射・終了後の PCM 再生を確認する。PCM 単独では音源ベクタの反射増分は 0。

Ra266 は BIOS 値・設定後値、媒体 CRC、30 秒の wave で IRQ12 到着増分と IRQ 側 pi>0、tick 側 ack 数、played・epoch・underrun と聴取（無音・途切れの有無）を対にする。5,138 は比較実績で固定の合格値ではない。IRQ0 の負荷は同一レート・period・lead・窓長で変更前後の IRQ0／音声 callback の回数・最大時間、IRQ12 側の負荷を比較し、強制 epoch の最大値を通常 wave と別記する。USB の IRQ10 配送も確認する。
