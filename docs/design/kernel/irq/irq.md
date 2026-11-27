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
4. エッジが重なった場合、一巡後に立った別装置の要求には次のエッジが保証されない。PCM は tick で回収する。V86 はセッション中の既存タイマ復帰点でも音源状態を一回確認する。非 VM フレームでは pending を保存し、VM に戻れる時に再試行する。仮想 IMR で保留した要求は IRR に残し、許可後に配送する。音源要求が持続中は同じ要求を入れ子にしない。具体的には仮想 ISR の IR4 が立つ間は再注入を抑止し、ゲスト EOI 後に状態を再確認する。仮想 PIC は要求記録（IRR）、候補選択、注入確定を分ける。既存の v86_pic_should_inject の一括更新を分割し、ゲスト IF、両 PIC の IMR、優先順位と既存 ISR を確認する。IRQ0 と slave IR4 が同時なら既定の固定優先順で IRQ0 を先に配送し、後者を保持する。スタック範囲とベクタを検査し、ゲストスタック・復帰フレームの更新に成功した時だけ IRR を消費して ISR（slave IR4 と master IR7 の cascade）を立てる。検査失敗で配送済みにしない。slave/master の仮想 EOI、マスク解除の I/O 復帰点、タイマ復帰点で再評価し、一復帰につき一ベクタを配送する。
5. enter 前に所有権を取得し、仮想 PIC と pending を初期化して登録する。通常の v86_run_limit 復帰、timeout、v86_mem.c の v86_session_end → v86_runtime_end を通ってアプリへ戻る fault の双方で終了処理を一度だけ行う。ゲスト実行を止め inactive・closing とし再入を禁じる。所有中のタイマ出力を禁止してフラグを解除し、実 86 なら FIFO 割込みを禁止して残留要求を解除する。装置の非アサートを確認後、非 IRQ 文脈で自セッションの登録を外し、IRR/ISR・pending・shadow を破棄して所有を返す。IRQ・例外スタックでは release_pending を立て、closing が落ちても再入を拒否する。kill landing から再度入る v86_session_end の前景 release 節で、v86_module_session_release と exec_finish より前に登録を回収する。停止確認失敗は所有を隔離して再入を拒否する。登録失敗も取得した所有を同じ停止手順で返す。PCM の登録と IRQ2 VSYNC の寿命は独立する。

### 実音源とネイティブ FM

起動時の既存 opn_probe の OPNA / OPN / ABSENT / UNKNOWN を常駐側に保持する。OPNA だけでは 86 型 FIFO の証明にならない。86 型を示す機能 ID・ポート配置（io_sound.md の A460h、018xh/028xh）を照合し、確認済みのボードに限って FIFO 制御 A468h の許可 bit5 と要求 bit4 を確認する。26K は OPN タイマのみ。開放バス 0xff を要求として扱わない。FM 状態は検出したベースでタイマ A/B を読む。OPNA のタイマフラグは IRQ 出力許可と独立なのでゲストが書く 27h・29h の許可状態との一致を確認する。shadow が要る書込みは V86 I/O 経路で実チップへ同じ値を渡し記録する。ゲストが直接 I/O する構成でもこの観測を保証できることを S1 の条件にする。現行 v86_io.c の 0188h–018Fh は TSS の I/O 許可範囲なので、単に #GP に記録を足しても観測できない。制御レジスタ選択／データのトラップが必要な構成では実ポートへの転送順、BUSY ポーリングを含むゲストの演奏速度を現行像と比較し、偽反射ゼロと素通しの受入を両方満たしてから S2 に進む。

S1 では検出した基本バンクの index/data（0188h/018Ah または 0288h/028Ah）、OPNA の拡張バンク（base+4/+6）を TSS bitmap で捕捉する。バンク別に選択 index を保持し、実ポートへの byte IN/OUT を一度だけ同順で転送する。基本バンク 27h の bit2/3（A/B フラグ許可）と OPNA 29h の bit0/1（対応出力許可）は成功した data 書込みで更新する。拡張バンクの同番号では更新しない。word/dword や文字列 I/O が捕捉範囲に重なる場合は等価な転送を検証できた幅・命令だけ許可し、未知の操作では実音源共有を中止する。開始時は所有取得後に既知の出力停止値を実チップと shadow に設定し、reset・BIOS の音源書込みにも同じ同期を適用する。未観測の経路が残る機種は理由を記録して共有を開始しない。

実音源ありならゲストが実ポートを操作し、native FM とは既存の legacy 所有権で排他にする。ABSENT は物理音源 handler の登録ゼロ。UNKNOWN は診断に残し、プローブが発生源を止められなければ使用開始を失敗にする。将来のソフト 86 は仮想要求を同じ pending → 仮想 PIC に渡せる。物理 IRQ12 の到着だけで仮想音源を発火させない。

現行の起動順を使う。kernel_boot_memory 内の LAN 読込みと PCI bind が fm_module_load より先、USB 起動は後である。初回も再 attach も machine 側で IRQ12 の登録と直列化し、登録者がいる場合は能動プローブ・IMR 書込みを行わず、保存した結果を使う。保存結果がなければ UNKNOWN とする。線を排他的に確保できた初回だけ操作し、途中失敗時も排他期間内に装置と線を戻す。ネイティブ側は machine の共有線方針を HAL 内部から受け、PCM のビルド定数への依存を外す。既存 opn_chip の OPNA 29h の IRQ 抑止、共有 OPN の 27h bit2–3 抑止と能力表示を適用する。LGY-98 の INT5 拒否を維持し、他の対応線の登録と IRQ13 マウスを退行検査に含める。

### 段と受入

| 段 | 対象と生成物 | 先に失敗を示す検査と完了条件 |
|---|---|---|
| S1 実音源の識別・所有 | fm_body.c、opn_probe.c、opn_chip.c、FM 常駐 façade・内部部品口、V86 の I/O 経路。kernel・fm.mod・v86.mod を一組にする | FM の模型で 86 / 26K / 無 / 不明、IRQ 出力禁止のタイマ、FIFO 単独、再 attach を区別。再 attach が PCM 稼働中に IMR を書く変異、OPNA を一律 86 とする変異を検出。実音源でゲストのポート操作がそのまま届く |
| S2 共通配送と V86 | irq_policy.c、platform_irq_limits.h、isr_stub.asm、legacy/v86.c・v86.h・v86_pic.c。PC-98 kernel・v86.mod / 診断像 | 実 dispatch・PIC I/O 模型で PCM の代役と V86 を両登録順で試す。各単独／同時／なし、同時の両 callback と EOI 各一回、片方解除と最後の解除を検証。VM=0、IMR 保留→許可、連続した同一要求、故障終了・次セッションを試す。先着で打切り・無条件反射・終了時の直接 mask は赤になる |
| S3 PCM 経路切替 | pcm_cs4231.h・pcm_cs4231.c、pcm_hw.c、pcm_module.c、pcm_cs4231_math.c の初期化表、pcm_e4_guest.c の注入経路と E4 診断・計測の IRQ10 表記。pcm.mod と対応する kernel / 媒体 | 実 open・失敗巻戻しの I/O 模型で Ra266 相当の 0x20→0x22、DMA1、IRQ12 登録を同時に確認。0x1A、IRQ10 登録、PCM だけでゲスト反射、PI の二重 advance、tick ack 削除の変異を検出。HAL と旧 advance の双方で IRQ 喪失後にも波形位置と drain を回復 |
| S4 統合受入 | S3 と同一 SHA・CRC の媒体、対応する部品、診断計数と音声記録 | PM が下記の NP21/W・Ra266 の窓を記録する。実 86 での素通しは 86 搭載構成の結果で判定 |

各段で実装前に失敗を確認する検査を登録し、実装後は次の変異でも失敗することを確かめる。

- S1 は両ベース・バンクで実ゲストの転送順と shadow を照合する。転送を省く変異、出力禁止でタイマフラグだけを立てた PCM IRQ を反射する変異、初回・再 attach・途中失敗で既登録の IMR に書く変異を落とす。
- S2 は実スタブの保存 → dispatch → 物理 EOI → VM 復帰を通す。VM=0 への注入、スタック検査前の IRR 消費、仮想 EOI による物理 EOI、cascade のマスク無視を落とす。両装置の要求が重なって線が下がらない模型で、追加エッジなしに tick が PCM を回収し、仮想マスク解除後に音源を一度配送する。未 ack のゲストを fault 終了させ、実音源の停止と登録解除、PCM 再生継続、次セッションの初期状態まで連続して検査する。
- S3 は INT5 以外・無効・予約・開放バス・読出し不能、Np・DMA 競合、IRQ 登録失敗で経路書込みゼロを確認する。bit6 を立てた元値、再 open、書込み後の readback 不一致、復元失敗を注入し、0x22 固定・上位ビット破壊・未復元での所有返却を落とす。

NP21/W は既存の SNDboard=0x64 trial ini を起点にする。PM は CS4231 と 86 の双方が検出できる構成か記録し、必要なら各音源用の ini を分ける。0F40h 読戻し、IRQ12 到着増分、IRQ 側 PI 受理、wave の played 増加、/api/sound の非無音 frame を同じ窓で採る。86 のタイマを使うゲストで演奏・反射・終了後の PCM 再生を確認する。PCM 単独では音源ベクタの反射増分は 0。併設できた場合は同時要求、長いゲスト ISR またはマスク保留、ゲスト終了直後の PCM 継続をそれぞれ別窓で記録する。併設できなければ共有模型の結果と各装置単独の実測を台帳に載せ、併設受入を「未確認」とする。

Ra266 は BIOS 値・設定後値、媒体 CRC、30 秒の wave で IRQ12 到着増分と IRQ 側 pi>0、tick 側 ack 数、played・epoch・underrun と聴取（無音・途切れの有無）を対にする。5,138 は比較実績で固定の合格値ではない。IRQ0 の負荷は同一レート・period・lead・窓長で変更前後の IRQ0／音声 callback の回数・最大時間、IRQ12 側の負荷を比較し、強制 epoch の最大値を通常 wave と別記する。USB の IRQ10 配送も確認する。
