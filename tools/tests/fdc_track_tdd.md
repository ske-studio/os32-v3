# FD のトラック単位の読み出し — シークの省略・まとめ読み・先読み (ホスト試験の記録)

- 症状: 実機 **PC-9821Ra266** で 2HD 1232KB の FD から起動すると、とても遅い。
  既定フォント (`/sys/font/default.kcg`、LZ4 で 188KB) の読み込みで 1 分以上止まる。
  NP21/W は回転待ちもシーク時間も模擬しないので、エミュレータでは再現しない
- 見立て (机上、**実機では測っていない**): 旧 `disk_read` (DRV_FDD) は 1 セクタずつ
  `SEEK` + 20ms 待ち + `READ DATA` (R = EOT) を出していた。次のセクタはヘッドの下を
  過ぎているので、1 セクタごとにほぼ 1 回転 (360rpm で約 167ms) 待つ
- **要求を束ねるだけでは足りない**: FatFs は `FF_FS_TINY=1` で、セクタに揃わない読みを
  1 セクタの窓 (count=1) で回す。フォントは 16B のヘッダの後に 1024B ずつ読む
  (`kernel/boot_font.c` の `kcg_read_chunked`) ので、**要求は全部 count=1**。そこで
  要求したセクタからトラックの終わり (EOT = spt) までを 1 回で読み、残りを持っておく
- 対象: [`drivers/fdc_track.c`](../../drivers/fdc_track.c) (区切り・先読み・読み直しの純粋な層)、
  [`drivers/fdc.c`](../../drivers/fdc.c) (`fdc_read_sectors`、覚えたシリンダ)、
  [`drivers/fdc_decide.c`](../../drivers/fdc_decide.c) (`fdc_rw_timeout_ticks`)、
  [`fs/fatfs/diskio.c`](../../fs/fatfs/diskio.c) (結線と、書き込み・再初期化での破棄)
- 実行: `python3 -B tools/tests/test_fdc_track.py [--target] [--mutate] [case ...]`
  (`make check-fdc-track-host` が `--target --mutate` 付きで回す)
- 試験: [`fdc_track_host.c`](fdc_track_host.c) — 実物を `#include` する。
  (1) 偽の fdc (`struct fdc_track_ops`) で区切り方と読み直しを見る。
  (2) **本物の `fdc.c`** を µPD765A の模型の上で回す。ポートは
  [`fdc_hostshim/io.h`](fdc_hostshim/io.h) が模型へ回し、`tick_count` は
  `-Dtick_count=(*fdc_fake_tick_ptr())` で「読むたびに 1 進む」時計にする
- KAPI は動かしていない。既存 API のシグネチャもそのまま。足したのは
  `fdc_read_sectors` / `fdc_get_stats` / `fdc_print_stats` / `fdc_media_gen` /
  `fdc_track_slot` / `fdc_get_known_cyl` (カーネル内だけ)

## 0. 9ed7c80 を NP21/W で起動して分かったこと (2026-09-24 夕) と直したもの

PM の実測: フォントの読み込みは 4a8fad4 とほぼ同じ約 3 分。EIP は全部
`fdc_wait_seek_end` の IRQ 待ち (0x118140〜0x118150 = 内側に展開された `fdc_wait_irq`)。

- **主因 (机上で確定、実測は PM)**: FatFs は `FF_FS_TINY=1` で FAT とデータが 1 つの窓
  (`fs->win`) を取り合う。`f_read` (ff.c 3914〜) はクラスタを越えるたびに `get_fat` →
  `move_window(FAT のセクタ)` で窓を FAT に替え、次のデータのセクタで窓を戻す。フォントは
  16B のヘッダの後に 1024B ずつ読むので**毎回セクタ (= 2HD の 1 クラスタ) を越える**。
  FAT はシリンダ 0、データは奥なので、**1KB ごとにシークが 2 回**。先読みを 1 本しか
  持たないと FAT のトラックとデータのトラックが追い出し合い、まったく当たらない。
  → 先読みを **2 本 (LRU)** にした。`fat_data_interleave` で、同じ読み方が
  READ 6 回・SEEK 4 回・期限切れ 0 になることを本物の `fdc.c` で見る。
- **完了待ちの取りこぼし (実機の形)**: `fdc_wait_seek_end` は SIS が別ドライブの通知や
  Ready 変化を返すと、1 件読んだだけで次の IRQ を待っていた。µPD765A の INT 線は pending が
  尽きるまで上がったままで、エッジの PIC には次のエッジが来ない → 自分の完了が FIFO に
  残ったまま **1.5 秒の期限まで空待ちし、救済の SIS で拾う**。1 本のエッジで 80h が出るまで
  読むようにした (`seek_edge_foreign`)。NP21/W は事象ごとに IRQ を出す
  (`fdc_intdelay` → `fdc_interrupt`) ので、エミュレータでこれが主因かは起動時の
  `[fdc] font: ... tmo=` の行で確かめる
- **IF=0 に見えたもの**: 0x244 は IF (bit9 = 0x200) が立っている (0x200 + ZF 0x40 + PF 0x04)。
  0x214 と同じく IF=1 で、割り込み禁止の区間ではない
- 模型は **INT 線をレベルで持ち、立ち上がりだけ `fdc_irq_fired` を立てる**形にした
  (pending の SIS 結果か、読み終わっていない READ/WRITE のリザルトがあるあいだ上がったまま)。
  これで「省略を排水より先に置く」変異が RED になる (`drain_before_skip`)
- レビューの major:
  - 先読みのスロットに**中身の世代** (`fdc_media_gen`) を持たせた。`fdc_write_sector_geom` を
    通る書き込み全部 (FatFs / dev.c の fd0・fd1 / KAPI の `dev_blk_write`)、SIS で見た Ready
    変化、`fdc_set_media` で進む。diskio.c の `disk_write` での破棄はこれに置き換えた
  - `.bss` の詰め物: 受け皿を **1 本の静的な領域 (27KB = 窓 1 + スロット 2)** にし、DMA の窓の
    位置を `fdc_buf_layout()` が実行時に決める (領域は 64KB より短いので境界は高々 1 本、窓は
    先頭か末尾に必ず取れる)。揃え指定もリンカスクリプトも使わない。`__bss_end` は
    0x184800 → 0x180780
  - diskio.c の破棄の試験 (`diskio_rw`): 読む → `disk_write` → 読む、読む → `fdc_write_sector`
    直 → 読む、Ready 変化の無い入れ替え → `disk_initialize` → 読む。
    `diskio_set_fdd_drive` の破棄は外した — `STA_NOINIT` にするので次の読みの前に必ず
    `disk_initialize` を通り、そこで捨てる (残すと等価な変異になる)
- minor: NR はまとめ読みの失敗に数えず行も出さない (`multi_nr_quiet`、戻り値 -3)。
  Ready 変化は SIS のどこで見ても世代を進め、覚えたシリンダを捨てる
  (`readychange_invalidates`)
- 起動時に `[fdc] font: seek= skip= recal= tmo= foreign= rdy= multi=ok/fail nr= single= retry= write=`
  を 1 行出す (`kernel/kernel.c`、FD に触っていなければ出ない)

## 0-2. 6541ef1 の実測 (2026-09-24 夜) — まだ当たっていなかった理由と直したもの

PM の実測 (NP21/W、FD 起動): フォントの読み込み約 92 秒 (187 秒から半分)、
`[fdc] font: seek=751 skip=16 recal=2 tmo=0 foreign=0 rdy=0 multi=767/0 ...`。
tmo=0 で IRQ の取りこぼしは消えたが、**まとめ読み 767 回・シーク 751 回**で先読みが
ほぼ当たっていない。

- **原因**: VFS が 1 回の読みごとにファイルを開き直す。`fs/vfs_fd.c` の `vfs_read_fd` →
  `fs/fatfs_vfs.c` の `fatfs_vfs_read_stream` が毎回 `f_open` → `f_lseek` → `f_read` →
  `f_close` を回すので、1KB のチャンクごとにパスをたどり直す。実物のイメージでは
  ルートディレクトリ (LBA 5 = C0H0)、`/SYS` (LBA 15 = C0H1)、`/SYS/FONT` (LBA 997 = C62H0)、
  FAT (LBA 2 = C0H0)、データ (LBA 998〜1181 = C62〜C73 の 24 トラック) の
  **4 本のトラックを毎チャンク巡回**する。巡回の長さがスロットの数 (2) を超えると LRU は
  1 回も当たらない。候補に挙がった世代 / ポインタの比較 / スロットの範囲はどれも無実
  (世代は rdy=0・write 無しで進まず、font_replay で ptr と範囲も通る)
- **直し**: count=1 の読み (FatFs の窓の出し入れ) だけを覚える **8 セクタの LRU
  (セクタキャッシュ)** を足した。毎チャンク使うディレクトリと FAT のセクタが居続け、
  データはトラックの先読みから出る。トラックのスロットは 1 本に戻した (受け皿の合計は
  9KB + 9KB + 8KB = 26KB、6541ef1 の 27KB より小さい)。VFS の開き直しそのもの
  (チャンクごとに FAT の鎖を先頭からたどる CPU の無駄) は触っていない
- **再現 (`font_replay`)**: `images/os32_boot.d88` を LBA 順に直し、Python が FAT12 を
  たどって `/SYS/FONT/DEFAULT.KCG` を取り出す。ハーネスは**実物の `ff.c`** を通し、
  `kcg_load_font` と VFS の読み方 (`f_stat` × 2 → 16B → `f_stat` → 1024B ずつ、各読みは
  `f_open` → `f_lseek` → `f_read` → `f_close`) をそのまま回す。読み方の形が変わったら
  気づくよう、Python が `fatfs_vfs_read_stream` の呼び出し順を静的に見る
  - 6541ef1 の形 (トラック 2 本・セクタキャッシュ無し) で回すと **seek=752 / multi=769**
    — 実測の 751 / 767 とほぼ一致 (模型が実物の読み方を写している証拠)
  - 直した後: **seek=14 / multi=27 / single=0 / tmo=0** (sec_hit=1110、trk_hit=161)。
    当時の試験は multi ≦ 27、seek ≦ 14 の固定値だった
  - **上限は配置から出す (2026-09-24、PM 判断)**。FD ローダが 1207B → 2648B (A-4) で
    1 クラスタ増えると後ろのファイルが 1 セクタずれ、/SYS が C0 → C1 に移ってフォントが
    13 シリンダに跨り、seek=16 で固定値に落ちた (読みの回数は同じ 27)。いまは
    `test_fdc_track.py` の `font_bounds` が d88 の FAT をたどって
    seek ≦ データのシリンダ数 + データの外のメタデータのシリンダ数 (C0 を除く) + FAT への往復 2、
    multi ≦ データのトラック数 + データの外のメタデータのトラック数 + 往復の読み直し 1
    を出し、ハーネスへ `FDC_TRACK_SEEK_MAX` / `FDC_TRACK_MULTI_MAX` で渡す (無ければ FAIL)。
    基点の配置で 14 / 27 (従来の値と同じ、実測 14 / 27)、ローダ 2648B の配置で 16 / 28 (実測 16 / 27)。
    往復の 2 は実測の経路 (`SEEK 64 -> 0 -> 64` が 1 回) から
  - **2026-09-25: 読むファイルを `/SYS/UNICODE.BIN` (128KB) に替えた。** 既定フォントを
    MINIMAL から NORMAL へ移して FD に載らなくなったため (build/packages.yaml)。読み方
    (16B → 1KB ずつ、`FDC_TRACK_PATH` で渡す) と上限の出し方は同じ。そのときの値は
    上限 seek ≦ 12 / multi ≦ 21 (データ 17 トラック / 9 シリンダ)、実測 seek=12 / multi=19
  - `images/os32_boot.d88` が無ければ `--require-image` で FAIL (make check)。
    check-fdc-track-host はイメージを前提にしない — 作り直すと kernel まで組み直され、
    check-par の他の試験と食い違うため。make all が先
- 起動時の行は 80 桁で切れないよう 2 行に分け、キャッシュの行を足した:
  `[fdc] font: seek= skip= recal= tmo= foreign= rdy=` /
  `[fdc] font: multi=ok/fail nr= single= retry= write=` /
  `[fdc] font: cache sec_hit= trk_hit= fill=`

## 0-3. ラリー 2 (Codex / Fable、2026-09-24 夜) で直したもの

6fa2ec7 の実測: フォント 3 秒、seek=15、multi=28。指摘と直しは票
(TASK_FDC_REALHW.md の「6fa2ec7 の結果とラリー 2」) の表のとおり。試験で見るもの:

- `recal_settle`: C=0 の読みを 1 回落とし、回復 (リセット + RECALIBRATE) の直後の C=0 の
  READ が整定前に出ないこと。模型は SE 付きの SIS を CPU が読んだ時刻から 2 tick 経たない
  うちの READ / WRITE を数える
- `idle_rule`: 最後の読みからちょうど 200 tick なら当て、201 tick なら両方捨てる。捨てた後は
  差し替えた媒体の中身を返す
- `track_nr_stop` / `single_nr_no_recover`: NR は単発へ落ちず、回復もリトライもしない
- `sis_edge_limit`: 1 本のエッジの前に件数の上限 (4) ちょうどの別の通知が積まれていても
  期限切れにならない
- `fdc_forget_rules` の (a) (e) (e2) (f) を新しい振る舞いに合わせた (リザルトまで読めた失敗は
  リセットしない、期限切れは回復まで、シークの失敗も回復を通す)

変異 41 本: **RED 40 / ERROR 0 / SURVIVED 1 (対照)**。途中の ERROR 1 本 (2 秒規則を消す変異で
`now` が未使用になった) は閾値を上げる形に書き直して RED。

後追い (Fable minor): 単発の WRITE もリザルトの NR で回復・リトライより前に打ち切るよう
READ に揃えた。`write_nr_no_recover` (リセット・RECALIBRATE が増えず、WRITE は 1 回きり、
DMA ch2 は閉じている) と変異「単発の WRITE の NR で回復とリトライを踏む」を足して
28 ケース、変異 42 本: **RED 41 / ERROR 0 / SURVIVED 1 (対照)**。

## 1. ケース

| ケース | 見るもの |
|---|---|
| `split_2hd` / `split_144` | spt=8/1024B と spt=18/512B で、count=1、トラックの終わりちょうど、ヘッドの境目、シリンダの境目、最後のシリンダ |
| `readahead_count1` | count=1 の連続が **1 トラック 1 コマンド**になる。途中のセクタから始めたらそこから EOT まで。手前は持っていないので読みに行く |
| `cross_boundary` | 境目をまたぐ要求がトラックごとに分かれ、どのコマンドもトラックを越えない。中身が正しく、受け皿の外 (前後 64B) を書かない |
| `fallback_single` | まとめ読みが失敗したら**要求した区間だけ** 1 セクタずつ読み直す (先読みの分は読み直さない)。失敗で壊れた受け皿を前のトラックとして当てない。先読みが落ちたトラックでは以後、要求の範囲だけを束ねて読む (印は破棄で忘れる) |
| `single_fail` | 要求の中に読めないセクタがあれば失敗を返す |
| `cache_rules` | 破棄 / 別ドライブ / 別ジオメトリ / 別ヘッド / 別シリンダでは当てない |
| `oversize_geom` | 受け皿に入らないジオメトリは束ねずに 1 セクタずつ |
| `timeout_math` | まとめ読みの時間上限 (下の §2) |
| `fdc_multi_cmd` | 本物の `fdc.c`: READ DATA が MT=0、R=sect、EOT=sect+count-1、DMA 長 = count×bps、受け皿が 64KB 境界をまたがない ([HW2])、トラックをまたぐ引数は I/O の前に断る |
| `fdc_seek_skip` | 同じシリンダならシークを省く (ヘッドが違っても、単発でも、書き込みでも)。違えばシークし、覚えた値を更新する |
| `fdc_forget_rules` | まとめ読みの失敗 (DMA を閉じ、リセット + RECALIBRATE でヘッドを 0 に戻して 0 を覚え直す)・その RECALIBRATE も落ちたとき・メディアの変更・ドライブの切り替え・単発の最終失敗・IRQ 無し・シークの失敗で覚えた値を捨てる |
| `fat_data_interleave` | FatFs の読み方 (FAT のセクタとデータのセクタが交互) で、FAT はセクタキャッシュから返り、FAT のトラックは 1 回だけ読む。本物の `fdc.c` で SEEK 4 回・READ 6 回・期限切れ 0。セクタキャッシュ無しの 1 本は毎回読む (9ed7c80 の姿) |
| `gen_and_lru` | 世代が進んだトラックもセクタも当てない。4 本のトラックを巡回するメタデータ (VFS の開き直しの形) を 40 巡しても、2 巡目からメタデータは全部セクタキャッシュで返る (LRU) |
| `recal_settle` | 回復の RECALIBRATE の直後の C=0 の READ が整定前に出ない |
| `idle_rule` | 2 秒規則の境目 (200 tick は当て、201 tick で両方捨てる) |
| `track_nr_stop` | まとめ読みの NR (-3) で 1 セクタずつへ落ちない |
| `single_nr_no_recover` | 単発の READ の NR で回復もリトライもしない |
| `write_nr_no_recover` | 単発の WRITE の NR でも回復もリトライもしない (READ と同じ) |
| `sis_edge_limit` | 上限ちょうどの別の通知の後ろの完了を期限切れにしない |
| `font_replay` | 実物の FD イメージと実物の `ff.c` で、フォントの読み込みを VFS の読み方のまま再現する (2026-09-25〜 フォントが FD から外れたので同じ読み方で `/sys/unicode.bin` を読む)。multi / seek は配置から出した上限以下 (`font_bounds`)、single 0、期限切れ 0、中身がファイルと一致 |
| `seek_edge_foreign` | SEEK の完了の前に別ドライブの通知 / 自ドライブの Ready 変化が積まれていても、1 本のエッジで全部読んで期限切れを待たない。Ready 変化で世代が進む |
| `drain_before_skip` | 取り残しの通知で INT 線が上がったままでも、省略の前の排水で下ろし、READ の完了のエッジが来る |
| `readychange_invalidates` | SIS で Ready 変化を見たら、持っている先読みを入れ替え後の媒体に当てない |
| `multi_nr_quiet` | SEEK の NR / READ の NR はまとめ読みの失敗に数えず、行も出さない |
| `diskio_rw` | diskio.c ごと: 初期化前は NOTRDY、同じトラックは 1 回だけ読む、`disk_write` と `fdc_write_sector` 直の後に古い中身を返さない、Ready 変化の無い入れ替えは `disk_initialize` で読み直す |
| `buf_layout` | 受け皿の割り付け: 境界がどこにあっても DMA の窓は 64KB をまたがず、先読みの領域と重ならない |
| `fdc_end_to_end` | `fdc_track_read` + 本物の `fdc.c`: count=1 × 48 セクタが READ 6 回・SEEK 3 回。裏でヘッドが動いていても (V86 の BIOS など) ID 部の照合 (WC) で落ちて読み直し、別のシリンダは読まない |

## 2. 資料の根拠

- **EOT と TC**: 単発の読み (R = EOT、DMA 長 = 1 セクタ) が実機で通っている。
  まとめ読みは同じ組み立てを count セクタに伸ばしたもので、DMA の TC と EOT が
  最後のセクタの最後のバイトで同時に来る。NP21/W は `src/io/fdc.c` の
  `FDC_ReadData` が `FDCEVENT_NEXTDATA` で `R++ == eot` まで `readsector()` を
  繰り返し、`fdc_dmafunc` の `DMAEXT_END` で `tc` を立てる
- **MT は使わない**: PC9800Bible 2-9 の INT 1Bh「データの読みだし (06h)」は
  「MT を指定し開始ヘッドが 0 なら同じシリンダのヘッド 1 も読める」とし、NP21/W も
  `fdc.mt` で `H ^= 1` して続ける。使えば 1 コマンド 1 シリンダ (2HD で 16KB) だが、
  (a) 転送長を 1 トラック以下に保つ (受け皿 9KB)、(b) 同資料は**書き込みの MT を禁じて
  いる** (µPD765 が正しく動かない)、読みの MT を実機で確かめた記録がまだ無い、
  (c) 得はシリンダごとにヘッド 1 の先頭の待ち (最悪 1 回転) を省くだけ、の 3 つで見送った
- **時間上限** (POLICY_DEBUG §4-51 の「機構の最悪値」):
  `2 × (2 回転 + ceil(count/spt) 回転 + HLT 10ms)`、回転は遅い 300rpm の 200ms。
  1 トラック全部で 2 × (400 + 200 + 10) = 1.22 秒。単発の 1 秒を下限にする。
  シークは `fdc_seek` が自分の上限 (1.5 秒) で別に待つ

## 3. 変異 (否定側)

`--mutate` は実装の写しを一時の木に置いて 1 か所ずつ壊す (ソースは書き換えない)。
判定: RED = どれかのケースが落ちた / ERROR = コンパイルできない (**RED に数えない**) /
SURVIVED = 見逃し。最後の 1 本は何も変えない対照で、SURVIVED でなければならない。

2026-09-24 の結果: **RED 20 / ERROR 0 / SURVIVED 1 (対照)** (変異 21 本)。

2026-09-24 夕 (上の §0 を直した後): **RED 32 / ERROR 0 / SURVIVED 1 (対照)** (変異 33 本)。

2026-09-24 夜 (§0-2 のセクタキャッシュの後): **RED 34 / ERROR 0 / SURVIVED 1 (対照)** (変異 35 本)。
トラックの LRU の変異はスロットが 1 本になって等価になったので外し、セクタキャッシュの
4 本 (使わない / LRU でない / 世代を見ない / LBA を見ない) を足した。
途中で SURVIVED が 2 本出た: (a) 覚えたシリンダの破棄はリセット・RECALIBRATE に加えて
Ready 変化の SIS でも行うので三重 (3 つとも消す形にして RED)、(b) `disk_initialize` の変異が
スロットの貸し出しごと消していて先読み自体が止まり、古い中身を返しようがなかった
(貸し出しと破棄を分け、試験に「同じトラックは 1 回だけ読む」を足して RED)。

途中で見つけた穴 (直した):
- 「トラックの境目で切らない」の最初の書き方は未使用変数でコンパイルが落ちた (ERROR)。
  境目を 1 つ越えて切る形に書き直して RED
- 「まとめ読みの前に捨てない」が SURVIVED — 偽の fdc が失敗のとき受け皿を壊して
  いなかった。実行フェーズの途中で落ちた DMA の姿 (受け皿を途中まで壊す) にして RED
- 「覚えた値を捨てる」は二重になっている所がある: DMA を積んだ後の失敗はリセット
  (`fdc_forget_all`) と RECALIBRATE の出す前の破棄、シークの失敗は `fdc_seek` の出す前の
  破棄とまとめ読みの失敗経路の破棄。片方だけ消す変異は振る舞いが変わらない (等価な変異)
  ので SURVIVED になった。両方消す形にして RED
- 自己レビューで 2 点足した: (1) まとめ読みの失敗の後にリセットだけでなく RECALIBRATE も
  出す (リセット後の PCN を信じて単発が別のシリンダへシークし、WC で 1 回無駄にしないため。
  単発のリトライの間の `fdc_recover` と同じ形)、(2) 先読みが落ちたトラックに印を付け、
  以後そこでは要求の範囲だけを読む (要求の外の傷で毎回「失敗 + 回復 + 1 セクタずつ」を
  踏まないため)。どちらも変異を足して RED を確かめた

## 4. ホストで見ていないもの

- diskio.c と FatFs 本体 (ff.c) はホストでも回す (`diskio_rw`、`font_replay`)。VFS (vfs_fd.c /
  fatfs_vfs.c) は通さず、読み方を写して回す (形は静的に照合)
- 実機の速度、エミュレータでの起動、実機での MT なしのまとめ読みの挙動 (TC と EOT が
  同時に来て正常終了すること) は未確認
