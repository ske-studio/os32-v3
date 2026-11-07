# PCI デバイス開発の注意点

> 状態: **現行 (2026-11-07)** — 手本のある PCI ドライバの設計・実装で守る装置の扱いの正典。

対象は Trident・GeForce・HPT370・82557 など、BSD/MIT の手本を OS32 に移す実装者と設計者。
装置固有の値・呼出列は各設計書に置き、ここでは共通の判断を定める。

## §1. 進め方

| すること / しないこと | 実例・根拠 |
|---|---|
| 手本は他の OS で動いた実績のある正解として、手順・順序・状態の持ち方をそのまま移す。BSD/MIT の著作権表示を残し、メーカー資料と手本の file:line を設計に挙げる。 | [CONSTRAINTS の HW3](CONSTRAINTS.md#hw3-新しいハードウェアの実装はライセンス上参照しやすい既存の実装資料を参照してから設計する)。GPL のコード・構造は写さない。 |
| 手本に無い状態・手順・検査を足さない。不具合は調べの版より先に手本と 1 手順ずつ比べ、外れた所を直す。 | Trident の全域 VERIFY は実物 2MiB の後ろを壊し、2 回目の初期化で白い矩形になった。 |
| 手本の暗黙の前提も移す。整列・上限・format/class の組合せ・bind/idle の順・失敗後の復帰を、作る側と受け取る境目で守る。手本より厳しい条件は独自に足さない。 | GeForce の源の無い時計の 0 を誤りにすると save が -509。読戻しと PLL 計算値の厳密一致も手本に無い。 |

設計と依頼の流れは PORT_FLOW、[os32-design-rally](../.claude/skills/os32-design-rally/SKILL.md)・[os32-delegate](../.claude/skills/os32-delegate/SKILL.md) に従う。

## §2. 部品の不在と誤り

| すること / しないこと | 実例 |
|---|---|
| 手本が不在・空表・未知版を無い扱いで続ける所は同じく続ける。空表を一律 ENODEV にせず、要求の解釈→必要な get/set の順と rc を保つ。 | GeForce の vmap 不在・id=0 は ENODEV で性能選択を継続するが、空の電圧表の直接 get は「不明」の負 rc を返す。壊れた表を握り潰さない。 |
| 任意の部品の失敗で必須の経路を落とさない。保存には取得できた任意状態の可用性を残し、取れなかった状態の復元を要求しない。 | GeForce の制御不能 fan は fannil 相当で継続する。温度・fan・時計の worker の失敗を present に返すと -502 で表示まで止まった。 |
| prepare が未実行・失敗なら部品の ops を公開しない。呼び手も NULL の口を呼ばず、誤りを返す。 | GeForce の gf1 status は準備されていない口を呼んで #PF になった。 |
| 段の失敗を `||` で一括せず、失敗した段と rc を記録する。黙って別の経路へ替えない。 | GeForce の立上げが黙って pc98-planar に戻り、失敗箇所が見えなかった。 |

## §3. 待ちと期限

| すること / しないこと | 実例 |
|---|---|
| 公開操作の入口で期限を作る。起動・前の操作の期限を状態に残して後で使わない。入れ子の呼出しはその操作の期限を借りる。 | GeForce の tile_set・perf_select・復旧後の再初期化は古い期限で最初の poll から時間切れになった。 |
| 待ちの開始点と予算の単位は手本どおりにする。長い devinit の前の期限で後段の待ちを削らず、再試行のたびに無制限に延長もしない。 | GeForce の objects_init は devinit が使った約 2 秒のため、GET=PUT でも -205 になった。RAM の head ごとの待ちにも各予算がある。 |
| 複数の対象に 1 つの絶対期限を配らない。connector・EDID block ごとに手本の待ち単位を保つ。 | GeForce では無応答ポートが予算を消費すると正常な後続ポートまで fallback する穴があった。今回のモード不足の根因とは未確定。 |

## §4. 初期化の順と状態

| すること / しないこと | 実例 |
|---|---|
| カードを立ち上げる devinit などの前に、後でしか意味を持たない値を読まない。 | GeForce の起動時予約で PFB 0x10020c を読み、VRAM=0 としてページ表の用意を飛ばした。 |
| 手本の初期化関数と機種分岐を全部たどり、移した対応を設計の表にする。`xf86IsPc98()` の枝を機械の前提だけで省かない。 | Trident の PC98TRIDENT96xxInit の省略で BAR0 が書けなかった。 |
| 判別表は default の行まで写す。途中で変わる状態は 1 つの欄で持ち、手本と同じ時点で切り替える。 | Trident の SPR 容量表の抜けは実機の値に当たらない。IOBase は Save・VGA Restore・TRIDENTRestore の途中で切り替わる。 |

## §5. レジスタと番地

| すること / しないこと | 実例・参照 |
|---|---|
| レジスタのアクセス幅を手本に合わせる。fake も幅別の回数と番地を記録する。 | GeForce foundation で幅が捨てられた。CR/SR の index/data・AR・パレットなどの 1 バイトの口を u32 にしない。 |
| LFB を CPU が書く前に手本の Sync 相当を完了する。HAL の出口から直接書き込みに戻る時点も守る。 | [Trident 全経路設計 §2](decisions/realhw/TRIDENT_FULL_DESIGN.md#2-接続契約面共通手順) は GE 操作後、CPU に戻る前の同期を定める。 |
| 手本の WC と OS32 の PCD 写像を同じ属性と思わない。PTE と MTRR/PAT の実効属性・排出・alias を確かめる。 | [GeForce 転送設計 §3](decisions/realhw/GF_ACCEL_DESIGN.md#3-段-0b-表示域-512-kib-の-mtrr-wc) は byte UC・dword UC・dword WC を区別する。PCD だけで WC 可否を決めない。 |
| DMA バッファは 64KB 境界をまたがせない。描画は HAL を通す。制約の本文は [CONSTRAINTS の HW1・HW2](CONSTRAINTS.md#ハードウェア)。 | FDC の DMA は境界を越えると回り込む。Trident・GeForce の加速も HAL バックエンドで接続する。 |
| HAL が今渡す値の幅を既存バックエンドで確かめ、幅を定数で受けて換算する。今の下限を将来の固定値にしない。 | Trident のパレットを 8 ビット入力と宣言すると、現行の 4 ビット入力では白がほぼ黒になる。 |

## §6. PC-98 の事情

- 内蔵 Trident・Cirrus は取り外せない。増設カードは内蔵装置との共存を前提にする。機種構成と配線の正典は [Ra266](decisions/realhw/RA266.md)。例: GeForce の BAR の調べでも内蔵 Trident の範囲を壊さない。
- primary / 非 primary の分岐を手本で確かめる。例: Ra266 の Trident は非 primary で、primary の font/text 保存をそのまま持ち込まない ([Trident 表示設計](decisions/realhw/TRIDENT_S3_DESIGN.md))。
- 映像の経路はモード設定と別に切り替える。例: Ra266 は本体側の 0FACh で共通の出力を選ぶ。Trident の Enable/Disable の出力順を共有 route の条件付き書込みへ置き換えない。
- 8 ビット転送の例外は確認した経路に限定する。例: Trident の RAM→VRAM present の格子・色帯には byte 転送を使うが、全経路設計の直接 VRAM 描画や cursor 転送へ一律に広げない。

## §7. 試験

| すること / しないこと | 実例 |
|---|---|
| 偽物は本番と同じ初期状態・上限で始める。起動前の NULL を埋めず、確認間隔と要求長の上限を混同しない。 | GeForce の ch->f の先埋めと 1000µs の扱いの差が、実機の -401・mode_load -1 を隠した。 |
| 本番の組み立てを通す試験を 1 本置く。fake が時計・待ち・ops を独自に渡すだけで済ませない。 | GeForce prepare の時計・待ちの未結線は部品試験で見えず、実機の gf2 status が -201 になった。 |
| 新しい呼出しを足したら兄弟の stub を持つ土台を揃える。include だけでなく Python の切出しも洗い、共有定義を外せる形にする。 | Host Services の vfs_fd_stale_mount の不足と、GeForce・Trident の共有 fake の二重定義が CI を順に落とした。 |

## §8. 実機での確かめ

- 手本の前提が PC-98 でも成り立つかは HW-BASIC で先に調べる。例: Trident の LFB の幅・映像経路は、小さい書込みと表示で確かめて設計へ戻す。
- 止まる危険を手本の処理を省く理由にしない。例: Trident の PC98 Init は省略せず、手本の列と実機で動いた列を照合する。
- 実機の依頼は [os32-realhw-update](../.claude/skills/os32-realhw-update/SKILL.md)・[ROLES §0](tasks/agents/ROLES.md#0-現行の体制) に従う。例: GeForce の診断版も、配備・再起動・シリアル試験はノートのセッションへ依頼する。
