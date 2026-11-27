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

2026-11-27 第3稿。初段で CS4231 を IRQ12（slave IR4）に結ぶ条件は、0188h と 0288h の両方で実 FM 音源の不在を確認し、BIOS の WSS 経路が INT5 で、DMA #1 を使用できること。OPNA、26K 相当、別ベースの音源、UNKNOWN では PCM を開始せず、起動ログにベースと理由を一行記録する。実音源を使う V86 ゲストは現行のポート素通しと IRQ12 反射を使う。

### S1 の音源判定

PC-98 常駐側は初期値 UNKNOWN とし、FM 部品からベース別の検査結果、確認済みのチップ ID・機能 ID とそのポート、確認できた FIFO 制御ポートを受け取る。未確認の欄は無効とする。0188h が ABSENT または UNKNOWN なら 0288h も同じ手順で調べ、先の UNKNOWN を後の ABSENT で消さない。両方で連続した開放バス応答を得て、機能 ID が示す装置・マスク設定とも矛盾しない場合だけ両ベース ABSENT とする。機能 ID が 1111b の場合は 26 相当の可能性がある（PC-98 資料 io_sound.md:146–198,224–229）。

現行 opn_probe は 0188h 固定だが OPN の肯定経路も持つ。S1 では status・index・data を候補ベースから求める。安定したチップ ID が 1 なら OPNA。26K 相当の肯定には、0188h で OPNA ID を除外し、機能 ID と矛盾せず、Timer B を停止・クリアした状態から開始して新しいフラグの立上りを観測し、停止・クリアと出力設定の復元が成功することを要求する。別ベースの応答は所在と種別を別々に記録する。応答不定、停止・復元失敗、検査省略は UNKNOWN。OPNA の確認だけで 86 型 FIFO を認定しない。

初回は既存の LAN・PCI bind 後、USB 起動前の FM 検出順を使う。能動操作は候補装置が駆動し得る線すべてについて登録と直列化し、他の所有者がいないと確認できたときだけ行う。配送線を絞れず排他を保証できない場合も省略する。既登録なら IMR・タイマを書かず UNKNOWN とする。初回判定を常駐側で起動中固定し、PCM open と部品の再 attach は保存記録を参照する。再 attach で強制的にプローブして稼働中の線を動かさない。

### 初段の段と受入

各段で先に実入口を通す赤の検査を登録し、実装後に同じ検査の変異を確認する。

| 段 | 対象 | 先行検査と完了条件 |
|---|---|---|
| S1 識別 | opn_probe.c、fm_body.c、FM 部品口と PC-98 常駐側 | ポート I/O だけを模型にして実プローブを通す。0188h の 26K、0288h だけの OPNA、両方空、0188h 不明＋0288h 空、既登録で省略、再 attach を試す。ID だけで 26K とする、0288h を省く、UNKNOWN を空とする、既登録の IMR に書く変異を落とす |
| S2 IRQ12 の配送 | irq_policy.c、platform_irq_limits.h、isr_stub.asm、legacy/v86.c | 起動時の両ベース ABSENT の構成だけ動的線 12 を公開し、スタブから一度だけ irq_dispatch(12)。complete の物理 EOI は slave → master 各一回。VM 中でもゲスト反射ゼロ、V86 enter/exit・fault で IRQ12 の直接 enable/disable ゼロ。PCM open 前・close 後も同じ分岐。実音源あり・不明・判定前は現行スタブとセッションの線制御を使う。両構成で IRQ2・IRQ13 と既存 PCI 登録を保存し、共有登録順・片方解除・EOI 二重化・VM への無条件反射を実スタブと PIC 模型で検査 |
| S3 PCM と DMA | 8237 常駐側・競合利用者、PCM façade・pcm.mod、初期化表、E4 診断・ゲスト注入と計測 | 実 open で IRQ12 登録 → 所有者付き DMA #1 取得 → 0x20 から 0x22 への書込みと照合 → codec の順を検査。音源あり・不明、無効な BIOS 経路、Np、IRQ/DMA 競合で装置書込みゼロ。bit6・再 open・経路不一致・復元失敗・元 DMA #0/#3 を注入し、他者の mask、隔離後の再取得、IRQ10 固定、PI の二重 advance、tick ack 削除を落とす。IRQ 欠測後の位置回復と drain も確認 |
| S4 受入 | 同一 SHA・媒体 CRC の kernel・部品と診断像 | PM が下記の PCM 許可窓と実音源の素通し窓を記録する。拒否構成で PCM を開始する像、PCM だけでゲストベクタが増える像は不合格 |

NP21/W は SNDboard=0x64 と実音源用構成のそれぞれで両ベースの判定を記録する。両方 ABSENT の許可窓では WSS 読戻し、IRQ12 到着・IRQ 側 PI・played の増分、非無音 frame、V86 音源反射ゼロを同時に採る。86 搭載窓では PCM 拒否理由、PCM の登録・経路書込みゼロ、既存ゲストのタイマ割込みと演奏を確認する。併設できても初段の期待値は PCM 拒否。得られない構成は台帳に未確認と記す。

Ra266 も両ベースの結果を先に採る。許可時は BIOS 値・設定値、30 秒の wave で IRQ12 到着増分と IRQ 側 PI、tick ack、played・epoch・underrun、聴取結果を記録する。5,138 は過去の比較値。変更前後を同一レート・period・lead・窓長で比較し、IRQ0／音声 callback と IRQ12 の回数・最大時間、強制 epoch の最大値、USB の IRQ10 配送を記す。拒否時は理由と経路不変を採る。

### 後段の実音源共有と仮想 PIC

1. 共有が成立した構成にも S2 の動的配送を適用する。既存の IRQ12 スタブはフレームを現行どおり保存し、IF=0 で一度だけ `irq_dispatch(12)`。物理 EOI はその complete が slave → master に各一回送る。スタブ・装置・仮想 PIC は物理 EOI を追加しない。
2. PCM と実音源の V86 用 handler は各々 `IRQ_F_SHARED` で登録する。PCM は R2.INT と I24.PI に従って ack。V86 側はセッションが所有する実音源の要求を非破壊で読み、該当すればセッション内の pending を立て IRQ_HANDLED。どちらも自分の要求がなければ IRQ_NONE。同時要求では両方が処理され、全員 NONE なら既存の storm 判定が働く。
3. スタブの dispatch 後、保存 EFLAGS.VM=1 の復帰点でのみ pending をゲストフレームに反映する。仮想 PIC の IMR・ベクタ設定と既存のスタック範囲検査を通す（既定 INT 14h）。物理 EOI 後、ゲスト ISR が実音源を読んでタイマ・FIFO の要求を解除し、ゲストの EOI は仮想 PIC に渡す。ホストはゲスト所有のラッチを先に消さない。
4. エッジが重なった場合、一巡後に立った別装置の要求には次のエッジが保証されない。PCM は tick で回収する。V86 はセッション中の既存タイマ復帰点でも音源状態を一回確認する。非 VM フレームでは pending を保存し、VM に戻れる時に再試行する。仮想 IMR で保留した要求は IRR に残し、許可後に配送する。音源要求が持続中は同じ要求を入れ子にしない。具体的には仮想 ISR の IR4 が立つ間は再注入を抑止し、ゲスト EOI 後に状態を再確認する。仮想 PIC は要求記録（IRR）、候補選択、注入確定を分ける。既存の v86_pic_should_inject の一括更新を分割し、ゲスト IF、両 PIC の IMR、優先順位と既存 ISR を確認する。IRQ0 と slave IR4 が同時なら既定の固定優先順で IRQ0 を先に配送し、後者を保持する。スタック範囲とベクタを検査し、ゲストスタック・復帰フレームの更新に成功した時だけ IRR を消費して ISR（slave IR4 と master IR7 の cascade）を立てる。検査失敗で配送済みにしない。slave/master の仮想 EOI、マスク解除の I/O 復帰点、タイマ復帰点で再評価し、一復帰につき一ベクタを配送する。
5. enter 前に所有権を取得し、仮想 PIC と pending を初期化して登録する。通常の v86_run_limit 復帰、timeout、v86_mem.c の v86_session_end → v86_runtime_end を通ってアプリへ戻る fault の双方で終了処理を一度だけ行う。ゲスト実行を止め inactive・closing とし再入を禁じる。所有中のタイマ出力を禁止してフラグを解除し、実 86 なら FIFO 割込みを禁止して残留要求を解除する。装置の非アサートを確認後、非 IRQ 文脈で自セッションの登録を外し、IRR/ISR・pending・shadow を破棄して所有を返す。IRQ・例外スタックでは release_pending を立て、closing が落ちても再入を拒否する。kill landing から再度入る v86_session_end の前景 release 節で、v86_module_session_release と exec_finish より前に登録を回収する。停止確認失敗は所有を隔離して再入を拒否する。登録失敗も取得した所有を同じ停止手順で返す。PCM の登録と IRQ2 VSYNC の寿命は独立する。

後段の開始条件は、FM タイマと FIFO それぞれについて実ボードの設定と物理配送線を照合できること。FM ベースから IRQ を推定しない（io_sound.md:318–321、NP21/W board86.c:150–162、pcm86io.c:330–349）。別線の発生源は IRQ12 の判定から除外し、線が不明なら共有を開始しない。実際に IRQ12 に接続した発生源だけ、タイマのフラグと出力許可、または確認済み FIFO 制御ポートの許可 bit5 と要求 bit4 を照合する。先行検査では IRQ10 に接続したタイマ／FIFO の許可と要求を立てたまま PCM IRQ12 を配送し、どちらもゲスト IRQ12 の増分ゼロを要求する。同時要求、保留、fault 回収、ゲスト演奏速度は後段の受入窓で測る。

後段では検出した基本バンクの index/data（0188h/018Ah または 0288h/028Ah）、OPNA の拡張バンク（base+4/+6）を TSS bitmap で捕捉する。バンク別に選択 index を保持し、実ポートへの byte IN/OUT を一度だけ同順で転送する。基本バンク 27h の bit2/3（A/B フラグ許可）と OPNA 29h の bit0/1（対応出力許可）は成功した data 書込みで更新する。拡張バンクの同番号では更新しない。word/dword や文字列 I/O が捕捉範囲に重なる場合は等価な転送を検証できた幅・命令だけ許可し、未知の操作では実音源共有を中止する。開始時は所有取得後に既知の出力停止値を実チップと shadow に設定し、reset・BIOS の音源書込みにも同じ同期を適用する。未観測の経路が残る機種は理由を記録して共有を開始しない。

将来のソフト 86 互換は実 86 のない機械で、仮想タイマ・FIFO が仮想 IRQ12 の pending を立てる入口を使う。CS4231 の物理 IRQ12 到着はこの入口を呼ばない。
