# CD の読み — 複数セクタの READ(10)・覚えたパス・先読みの窓 (ホスト試験の記録)

- 症状: 実機 **PC-9821Ra266** で CD からの `cdinst` (Full) が 10 分を超えても NORMAL.PKG の途中。
  パッケージは合計およそ 12MB (BOOT 6KB・MINIMAL 1.2MB・GUI 0.4MB・NORMAL 9.7MB・DEBUG 1.3MB、
  **全部 `lzss: false`** = `pkg_extract` の無圧縮の経路) なので、実効は 20KB/s を切っている。
  NP21/W は CD のシークも回転も模擬しないので、エミュレータでは再現しない
- 見立て (机上、**実機では測っていない**):
  1. `drivers/atapi.c` の `atapi_read_sectors` は count がいくつでも READ(10) を 1 セクタずつ出していた
  2. `fs/iso9660.c` の `read_stream` は呼ばれるたびに `iso_resolve_path` で根からたどり直し
     (根のディレクトリ = LBA 23 を 1 回読む)、それから 1 セクタずつ `iso_read_sector` を呼んでいた
  3. `userland/lib/rt/pkg.c` の無圧縮の展開は 4KB ずつ `sys_read` し、データ部の先頭 (表の後ろ) が
     セクタに揃っていないので 4KB の読みは 3 セクタに掛かる
  → 4KB ごとに「LBA 23 → データ (LBA 1440〜) の 3 セクタ」の **1 セクタの READ(10) が 4 回、うち 2 回は
  離れた位置へのシーク**。12MB で約 3000 区切り × 4 = 約 12000 コマンド、シーク約 6000 回。
  `pkg_parse` (事前検査と展開で 2 回) は 1B・パス長・5B の小さな `sys_read` を項目ごとに 3 回出すので、
  1 本の PKG で数百回ぶん「根を読む + データを 1 セクタ読む」を繰り返していた
- 対象: [`drivers/atapi.c`](../../drivers/atapi.c) / [`atapi.h`](../../drivers/atapi.h)、
  [`fs/iso9660.c`](../../fs/iso9660.c) / [`iso9660.h`](../../fs/iso9660.h)、
  [`userland/lib/rt/pkg.c`](../../userland/lib/rt/pkg.c)。KAPI は動かしていない
- 実行: `python3 -B tools/tests/test_cd_read.py [--target] [--mutate] [case ...]`
  (`make check-cd-read-host` が `--target --mutate` 付きで回す)

## 1. 直したもの

### atapi.c
- `atapi_read_sectors` は連続する count セクタを **`ATAPI_READ_MAX_SECTORS` (既定 16 = 32KB) ずつの
  READ(10)** で読む。CDB の転送長 (bytes 7-8) に n を入れる
- 複数セクタの READ(10) が失敗したら (ERR、または**受け取ったバイト数がちょうど n × 2048 でない**)、
  その範囲だけ 1 セクタずつ読み直す。1 セクタでも落ちたらそこで失敗を返す
- PIO: byte count limit には `min(バッファ, ATAPI_PIO_BCL_MAX = 0xF800)` を書く (偶数・セクタの倍数、
  64KB を書くと 16 ビットに入らず 0 になる)。データは DRQ ごとに Cylinder Low/High のバイト数を読む
  (以前から複数 DRQ のループはあった)。**各 DRQ のブロックの後に ALT_STATUS の空読みで 400ns** 置く
  — 置かないと前のブロックの DRQ=1 を読んで次のブロックと取り違える
- エラーのときエラーレジスタの bit7-4 (センスキー) を覚える。UNIT ATTENTION (6) / NOT READY (2) で
  **媒体の世代** (`atapi_media_gen`) を進め、UNIT ATTENTION の READ(10) は `ATAPI_UA_RETRIES` (3) 回まで出し直す
  (入れ替え直後の mount の PVD 読みが落ちないように)
- 相性で困ったら `ATAPI_READ_MAX_SECTORS` を 1 にすれば旧来の読み方 (`#ifndef` なので -D でも変えられる)

### iso9660.c
- **覚えたパス 1 本** (`pc_path` → LBA・サイズ・フラグ)。同じパスなら根からたどらない。
  見つからなかったパスは覚えない
- **ディレクトリのセクタの LRU** (4 本 = 8KB、ctx の中)。`list_dir` は cb を呼ぶ前に手元へ写す
- **先読みの窓** (16 セクタ = 32KB、mount で kmalloc、取れなければ窓なし): ファイルのデータは窓を通す。
  窓より大きいセクタに揃った範囲は呼び手のバッファへ直接 (atapi が 16 セクタずつに分ける)。
  窓はファイルの外へ広げない (媒体の最後のファイルで読めない先を読まない)。窓は読む前に捨てる
- 捨てる合図: umount (ctx ごと)、媒体の世代が進んだとき、**最後に媒体を読んでから 2 秒
  (`ISO_IDLE_TICKS` = 200 tick) を超えたとき** (キャッシュの当たりは時刻を進めない)。
  読みの途中で世代が進んだら捨てて 1 回だけ読み直す。約束は docs/06_filesystem.md §6-6
  (「CD を入れ替えたら umount / mount」)

### pkg.c (cdinst の無圧縮の展開)
- 区切りを **32KB** (`PKG_STREAM_CHUNK`、`mem_alloc`。取れなければスタックの 4KB) にし、
  **区切りの終わりをファイルの中のセクタ境界 (2048 の倍数の位置) に揃える**。最初の読みだけ半端で、
  以後は 32KB の揃った読み = iso9660 が直接 1 回 (16 セクタ) の READ(10) で読む
- 失敗の枝でも取ったバッファを返す (以前は `return` が 3 か所に散っていた)
- `cdinst.c` は触っていない (展開は `pkg_extract` が全部持つ)。LZSS の経路は元から全体を 1 回で読む

## 2. 試験

ハーネスは 2 本。どちらも実物を `#include` し、ASan/UBSan を付けて組む。

- [`cd_read_host.c`](cd_read_host.c): 実物の `atapi.c` + `iso9660.c`。ポートは
  [`atapi_hostshim/io.h`](atapi_hostshim/io.h) が **ATAPI デバイスの模型**へ回す。模型は PACKET → CDB 6 ワード →
  DRQ ごとのデータをレジスタの粒度で写し、READ(10) の数・複数セクタの数・転送セクタ数・(LBA, 数) の列を持つ。
  DRQ の大きさ (2048 = NP21/W / bcl どおり / 6144 / 1000 / 512)、複数セクタだけ失敗、ある LBA が失敗、
  複数セクタで 1 セクタ足りない転送、次のコマンドで UNIT ATTENTION + 媒体の入れ替え、を選べる。
  ブロックの境目の後は **3 回ぶん古い状態 (DRQ=1) を見せ、データレジスタは屑**を返す (400ns の整定の再現)。
  媒体は試験が組む ISO 9660 (PVD・根・2 セクタの SUB・BIG.PKG = 601 セクタで末尾は半端・媒体の最後)
- [`cd_pkg_host.c`](cd_pkg_host.c): 実物の `pkg.c` の `pkg_extract`。KAPI の贋物が `sys_read` の
  (位置, 長さ) を記録し、書き出しの中身を突き合わせる。記録を cd_read_host の `replay` で再生して、
  pkg.c の読み方のまま実物の iso9660 + atapi で READ(10) を数える
- `ATAPI_READ_MAX_SECTORS=32` の版 (1 回 64KB) でも `stream_4k` など 5 ケースを回す

| ケース | 見るもの |
|---|---|
| `stream_4k` | BIG.PKG を 4KB ずつ read_stream: 中身一致、**パスの解決 1 回**、**READ(10) ≤ ⌈601/16⌉ + 3** (実測 39)、根のセクタは 1 回、同じセクタを読み直さない |
| `stream_odd` | 1000B ずつ・13B ずれた位置から: 同じ上限 (実測 39) |
| `stream_32k` | pkg.c の形 (揃えて 32KB): 上限、窓を通さない直接の読みがある (実測 39) |
| `read_file` | 全体・切り詰め・ディレクトリ・無いファイル (実測 40) |
| `multi_fallback` | 複数セクタが全部落ちる: 1 セクタずつで中身一致 |
| `short_transfer` | 複数セクタで 1 セクタ足りない: 失敗として読み直す |
| `bad_outside` | 窓の先読みが**要求の外**の不良で落ちても、要求の範囲は読める (半端な位置からも)。窓は無効のまま、捨てた窓を当てない |
| `bad_inside` | **要求の中**の不良は VFS_ERR_IO (窓経由も、窓を通さない 64KB の読みも)。読み直しは不良で止まる |
| `stat_swap` | 解決の途中で交換: キャッシュの A の根が SUB = 21 と答え、SUB の読みが UNIT ATTENTION → B の 21 (囮) を読む。世代を見て B の根から引き直し、B の本物 (24) の DEEP.BIN の大きさを返す (stat / get_file_size) |
| `list_swap` | list_dir: 解決の途中の交換は引き直す / 2 セクタ目の読みで交換 (囮の 2 セクタ目は空 = cb が呼ばれない) → VFS_ERR_IO / cb の中の読みが交換を踏む (1 セクタの根) → VFS_ERR_IO、次の操作は新しい媒体 |
| `lru_order` | LRU の追い出しの順 |
| `ua_mount` | UNIT ATTENTION 直後の mount (PVD) が通り、世代が 1 進む |
| `multi_drq` | DRQ の区切り 5 通りで中身一致、bcl は偶数・0 でない・0xF800 以下 |
| `path_cache` | 違うパスは引き直す、同じなら引かない、見つからないパスは覚えない、mount し直しで引く |
| `dir_lru` | 2 本のパスを交互に小さく読む: パスは毎回引くがディレクトリのセクタは媒体から 1 回ずつ |
| `list_reentrant` | 窓なしで list_dir の cb が同じ FS を読み、根のスロットが追い出されても一覧が崩れない |
| `unit_attention` | 読みの途中で UNIT ATTENTION + 入れ替え: 新しい媒体の中身を返す (パスを引き直す) |
| `idle_rule` | 2 秒ちょうどは捨てない、超えたら捨てる。UNIT ATTENTION の無い入れ替えでも古い窓を当てない |
| `no_window` | 窓が取れなくても中身一致 |
| `pkg:aligned_chunks` | 読みは 32KB 以下、終わりはセクタ境界かファイルの終わり、取ったバッファを 1 回返す |
| `pkg:nomem` / `pkg:open_fail_frees` | 4KB に落ちる / 失敗の枝でもバッファを返す |
| `np2_read` / `np2_async` | NP21/W の写し (同期 / CD_ASYNC の BSY) の上で、容量・LBA 16・16 セクタ・37 セクタ・mount と読み |
| `np2_empty` | 空のドライブ: 容量 1、READ(10) 失敗と `[atapi] ... sense=5` の行、媒体が入れば読める |
| `np2_slave` | マスター CD / マスター空 CD + スレーブ CD / スレーブだけ / マスター CD + スレーブ空: 媒体のある方を選んで読む |
| `cap_nodata` | READ CAPACITY がデータ無しで終われば失敗 |
| `cap_len` | READ CAPACITY の応答が 7 / 9 / 6 バイトなら失敗、8 だけ通る (ワードで数えない) |
| `np2_slave_strict` | `np2_slave` を実機寄り (BSY の装置は書き込みを無視) で |
| `np2_sel_lag` | 選んだ直後に 7 回 BSY を見せる装置 × 4 構成: 選ぶ装置・読み・mount、規定違反 0、リセット 0 |
| `np2_absent` | 居ない装置が 0x00 / 0x80 (BSY 付き) を返す × マスターだけ / スレーブだけ: 見つけて読む、待ち・リセット 0 |
| `np2_sel_busy` | (白箱) バスが別の居る装置を BSY のまま選んでいる: 待ってから選ぶ / 固まったままなら DEVICE RESET (その装置だけ、UA が立つ) / DEVICE RESET を受けなければ SRST / 使う装置が選ばれた瞬間に固まり DEVICE RESET も受けない: SRST の後に使う装置を選び直してから PACKET |
| `np2_stuck` | 使っている装置が LBA 17 を渡した後に固まる: 複数セクタが期限切れ → 1 セクタずつ (DEVICE RESET で戻す) → 17 で失敗、行は `lba=17 n=1 ret=-1 st=d0 ... got=2048 req=16+4`、その後 16〜17 は読める |
| `np2_ua_init` | 空のマスター + 媒体のあるスレーブ、両方が UNIT ATTENTION (報告で消える / REQUEST SENSE でだけ消える): スレーブを選ぶ |
| `np2_becoming_ready` | スレーブが 3 回 NOT READY / 04h: 3 回待って (250ms × 3 = 750ms) スレーブを選ぶ |
| `np2_ready_total` | スレーブが 18 回 (4.5 秒) 準備中: 待ちきってスレーブを選ぶ (待ちは 100ms 以下の塊)。ずっと準備中: 待ちの合計 4〜5 秒で諦めてマスター |
| `np2_srst` | 使う装置が選ばれた瞬間に固まり DEVICE RESET も受けない × 3 構成 (空のマスター / マスター無し / スレーブが空): SRST → 2ms → マスターの BSY=0 → 選び直し、規定違反 0 |
| `ua_multi` | READ(10) の UNIT ATTENTION が 3 回続いても読める (世代 +3、READ(10) 4 回)、4 回続けば落ちる |
| `np2_no_medium` | マスターが NOT READY / 3Ah: READ CAPACITY 1 回で確定 (待たない)、スレーブを選ぶ。公開の `atapi_read_capacity` も `ATAPI_ERR_NO_MEDIA` を 1 回で |
| `replay` | pkg.c の読み方 (28 回、559KB) を再生: **READ(10) 19 回** (274 セクタ ÷ 16 = 17.2 + 根 + 端)、パスの解決 1 回 |

参考 (机上): 直す前の形 (4KB ずつ、根から引き直し、1 セクタずつ) では、同じ 559KB で
4KB の区切り 144 回 × (根 1 + データ 2〜3) ≒ 550 回の READ(10)、うち約 290 回は離れた位置へのシーク。

## 3. 否定側 (変異)

`--mutate` は一時の木の写しに 1 か所ずつ当てる (ソースは書き換えない)。RED = どれかのケースが落ちた、
ERROR = コンパイルできなかった (**RED に数えない**)、SURVIVED = 見逃し。最後の 1 本は何も変えない対照で、
SURVIVED でなければ試験が不安定。変異の一覧と結果は `test_cd_read.py` の `MUTATIONS` と実行時の出力
(`MUTATION n/n RED`) が正。

- 「直す前の姿」は変異 1 (READ(10) を 1 セクタずつ) と変異 10 (覚えたパスを使わない) で、どちらも RED
  (READ(10) の上限と、パスの解決 1 回で落ちる)
- 組んでいて気づいた等価な変異は入れていない:
  - 窓の当たりの `lba >= ra_lba` — 符号無しの差で同じになるので条件から外した
  - `bcl &= ~1` — 呼び手のバッファは 8 か 2048 の倍数で、奇数にならない
  - UNIT ATTENTION の出し直しは、読みの経路では「1 セクタずつの読み直し」と iso9660 の読み直しが
    吸うので、落ちるのは mount (1 セクタの PVD 読み) だけ。`ua_mount` がそれを見る

## 3-2. Codex レビュー 1 (6e4bd83) の P2 × 2 の直し

- **P2-1** 窓の先読み (要るセクタから 16 本) が、要求の外にある不良セクタで落ちると、要求の範囲が読めても
  VFS_ERR_IO を返していた。→ 先読みが落ちたら窓は無効のまま、その呼び出しの残りは**窓なしと同じ経路**
  (揃った範囲は直接、端は LRU) で要るセクタだけを読む。`bad_sector` (この失敗を正解として固定していた) を
  `bad_outside` / `bad_inside` に分けた
- **P2-2** stat / get_file_size / list_dir は入口でしか世代を見ていなかった。旧媒体のキャッシュで得た
  サブディレクトリの LBA を読む READ(10) が UNIT ATTENTION → 出し直しで新媒体の同じ LBA を読み、その答えを
  返していた。→ 解決を `iso_resolve_checked` に寄せ、解決の後に世代を比べて変わっていれば捨てて 1 回だけ
  引き直す (2 回とも変われば VFS_ERR_IO)。list_dir はセクタを読んだ後と cb の後にも開始時の世代と比べ、
  変わっていたら VFS_ERR_IO で中断する (cb に渡した分は取り消せないので読み直さない)
- 模型の媒体 B は SUB を 24 に移し、B の 21, 22 には 2 セクタ目が空の囮の SUB を置いた
- 変異 34〜37 (上の 4 つを 1 つずつ戻す) は全部 RED

## 3-3. NP21/W で 1 セクタも読めなかった件 (2026-09-26、fe8305d)

- 報告: `dd cd0 lba=16 count=1` → read error、`mount /cd0 cd0 iso9660` → -1、`cd0: block 1 sects`。ISO は NP21/W の ide3
- NP21/W の実装 (`np21w-src/src/cbus/ideio.c` / `atapicmd.c`) を読んで写した**2 台の模型** (`g_np2`、セカンダリの
  マスター / スレーブ) を足した。同期の経路 (`atapi_dataread`: セクタごとに DRQ 2048、最後のワードで次のセクタか
  完了、エラーレジスタを読むと CHK が落ちる、リセット後の CylLo/Hi = EB14h、READ CAPACITY は**総数**を返し空のドライブは 0) と、
  CD_ASYNC の「読みの後しばらく BSY、DRQ は次のフレーム」を写した
- 分かったこと (机上 + 模型): 新しい手順 (複数セクタ・400ns・受け取った長さの確認) は NP21/W の同期・CD_ASYNC の
  どちらの経路でも読める (`np2_read`、`np2_async`)。**報告の形 (容量 1・READ(10) が失敗) は「選んだ装置が空の CD」**
  のときにだけ出る: fe8305d の atapi.c をこの模型の「マスターに空の CD、スレーブ (ide3) に ISO」で回すと
  `cap total=1`・`read16 rc=-3`、「マスター無し・スレーブに ISO」では `atapi_init` が CD 無しを返す。
  **275df91 以前の atapi.c もマスターしか見ない**ので、同じ構成なら同じに落ちる (以前読めたときは ISO がマスター
  = ide2 だったと考えられる。**NP21/W の ini は読んでいない**ので構成は PM が確かめる)
- 直し: マスターとスレーブの両方を見て、2 台なら媒体の入っている方を選ぶ。セカンダリへ切り替えるたびに選んだ
  装置を選び直す (直す途中、スレーブを見た後の最初の BSY 待ちが居ないスレーブ = 0xFF を見て期限切れになるのを
  模型が見つけた)。READ CAPACITY の長さの確認と、読みの失敗の診断の行を足した
- 試験: `np2_read` / `np2_async` / `np2_empty` (空のドライブで容量 1・sense=5 の行・入れば読める) / `np2_slave`
  (4 通りの構成で選ぶ装置と読み) / `cap_nodata`。変異 38〜43 は全部 RED

## 3-4. Codex レビュー 2 (2dc3c7f) の P1 / P2 × 3 / P3

- **P1** 初期化中の容量確認が、対象装置を選ぶ前に旧装置の BSY を待ち Features / Byte Count を書き、
  選んだ直後に PACKET を出していた。→ PACKET の共通路 `atapi_send_cdb` が毎回 `atapi_select_device`
  (DRV_HEAD → 400ns → 選んだ装置の BSY/DRQ クリア待ち) を済ませてからレジスタを書く。
  `atapi_select_bank(1)` はバンクを切り替えるだけ
- **P2** BSY タイムアウトの後も無条件で DRV_HEAD を書いていた。→ 書く前に ALT_STATUS を見て、
  0xFF (浮いたバス) と**シグネチャの出なかった装置**の値は待たず、居る装置の BSY/DRQ だけを
  「コマンド未完了」として待つ。期限切れなら `atapi_recover`: **DEVICE RESET (08h)** — PACKET 装置が
  BSY でも受ける唯一のコマンドで、その装置だけを戻す (NP21/W `ideio_o64e` case 0x08 = drvreset) —
  それでも戻らなければ **SRST** (バンクで選んだバスの 2 台とも。UNDOCUMENTED io_ide 074Ch bit2、
  NP21/W `ideio_o74c` は `getidedev()` = そのバンクだけなので、プライマリの HDD には届かない)。
  選んだ後の待ちが期限切れでも同じ回復を 1 回だけ試す
- **P2** UNIT ATTENTION (6) を媒体なしと扱っていた。→ `atapi_capacity_ready`: UA は REQUEST SENSE で消して
  出し直す、NOT READY (2) は REQUEST SENSE の ASC が **3Ah (媒体なし) のときだけ確定**、それ以外 (04h 準備中など)
  は 250ms 置いて出し直す。`ATAPI_READY_RETRIES` (当時 16) まで。公開の `atapi_read_capacity` も同じ経路。
  (当時は `cpu_delay_us(250ms)` を 1 回で頼んでいて、実物は 100ms に丸めるので実際は 16 × 100ms ≒ 1.6 秒だった — §3-5)
- **P2** `got` が `total_read += 2` (ワード) 由来で、Byte Count=7 の応答を 8 と数えていた。→ ポートから読むワード数
  `(xfer+1)/2` と有効バイト数 `xfer_size` を分け、`total_read = end`
- **P3** 診断の行の lba / n が呼び手の範囲で、st / err / got は最後の 1 セクタの試行のものだった。→ `atapi_read10` が
  落ちたコマンドの (lba, n) を `s_diag_lba / s_diag_n` に残し、行はそれと `req=<範囲>` を出す。
  `atapi_status()` が待ちで読んだ ALT_STATUS を毎回 `s_diag_st` に残し、期限切れの早期 return でも `s_diag_got` を更新
- 模型 (NP21/W の写し) に **実機寄りの strict** を足した: BSY の装置はレジスタ書き込みを無視する (NP21/W は
  `atapi_dataread_asyncwait` で先に完了を待つ = 0 のときの振る舞い)、選んだ直後に `sel_lag` 回 BSY、居ない装置の値
  `absent_status` (0x00 / 0x80)、UA (報告で消える / REQUEST SENSE でだけ消える)、NOT READY / 04h・3Ah、固まる装置
  (DEVICE RESET / SRST で戻る、`ignore_devreset`)、REQUEST SENSE (`atapicmd.c` case 0x03 の写し)。
  規定違反 (BSY 中の DRV_HEAD / コマンド / レジスタ書き込み、Byte Count を書かない PACKET) を数えて 0 を見る
- 変異 44〜56 (直す前の順序、選んだ後に待たない、選ばれている装置の BSY を待たない、リセットしない、SRST の後に
  使う装置を選び直さない、居ない装置の BSY も待つ、UA を媒体なし、UA を REQUEST SENSE 無しで出し直す、NOT READY を
  待たない、3Ah も待つ、ワードで数える、行が呼び手の範囲、期限切れで got を残さない、st を残さない) は全部 RED。
  全 56 本 RED / 0 SURVIVED (対照は SURVIVED)
- 組んでいて見つけた自分の穴: 選んだ後の待ちが期限切れで SRST まで行くと、SRST はマスターを選び直すので、
  そのまま Byte Count / PACKET を書くと**マスターへ行く**。`atapi_recover` が SRST の後に使う装置を選び直す
  (`np2_sel_busy` (4) = 選ばれた瞬間に固まって DEVICE RESET も受けない装置、変異 47)
- 白箱の `np2_sel_busy` はバスの選択 (`s_cursel`) を試験が直接動かす — 「別の居る装置が BSY のまま選ばれている」は
  今の atapi.c の経路では作れない (装置を替えるのは atapi_init だけ) が、`atapi_select_device` の規則そのものを見る
- `atapi_read10` の UA の出し直しは REQUEST SENSE を挟まない (報告で UA が消える装置を前提。SPC の既定)。
  REQUEST SENSE でだけ消える装置では読みの UA は出し直しでは消えない — 未対応 (容量確認だけ直した)

## 3-5. 代行レビュー (Fable 5.1) 2 回目の P2 / P3 × 3

- **P2** `ATAPI_READY_WAIT_US` (250ms) を `cpu_delay_us` に 1 回で渡していたが、`cpu_delay_us` は 1 回
  100ms で丸める (`kernel/cpu_calibrate.c`)。実際の待ちは 16 × 100ms ≒ **1.6 秒** (文書は 4 秒) で、トレイを閉じた
  直後 (becoming ready 2〜5 秒) のスレーブを待ちきれず空のマスターに固定していた (選び直す経路は無い)。
  → `atapi_delay_us` が `CPU_DELAY_US_MAX` (100ms。`kernel/cpu_calibrate.h` を直接 include する — 値を写さない) 以下の塊に分けて回す。
  `ATAPI_READY_RETRIES` を 20 にして**待ちの合計は最大 20 × 250ms = 5.0 秒**。
  **tick_count で待たない理由**: tick は PIT の割り込み (IF=1) が前提で、呼ばれる文脈 (起動時の `atapi_init`、
  KAPI 経由の読み) で割り込みが開いていると決められない。`cpu_delay_us` は割り込み禁止でも待て、`atapi_init` は
  `cpu_calibrate` の後 (`kernel/kernel.c`)
- ホスト試験の贋物 `cpu_delay_us` を実物どおり `CPU_DELAY_US_MAX` で丸める形にした (以前は `g_delay_us += us`
  で丸めず、1 回 250ms の呼び方を見抜けなかった)
- **P3** SRST を解いた直後、BSY の解除を待たずに DRV_HEAD を書いていた。→ `atapi_srst`: 解いて 2ms
  (`ATAPI_SRST_SETTLE_US`) 置き、マスターの BSY=0 を待つ (マスターが居ない・浮いたバスなら待たない)。
  選び直し (DRV_HEAD → 400ns → BSY=0) は呼び手 (`atapi_recover`)。strict の模型は SRST を解いた後
  `NP2_SRST_BUSY_READS` 回 BSY を見せ、2ms 前のステータスの読み / DRV_HEAD を規定違反 (`srst_early`) と数える
- **P3** READ(10) の UNIT ATTENTION の出し直しを 1 回から `ATAPI_UA_RETRIES` (3) 回に
- **P3** SRST は同じバンク (セカンダリ) の ATA HDD (`ide.c` の drive 2 / 3) も戻す、と atapi.c と §5 に書いた
- 変異 57〜61 (250ms 1 回で頼む、16 回で諦める、SRST の後 2ms 置かない、SRST の後マスターの BSY を待たない、
  UA を 1 回しか出し直さない) は全部 RED。全 61 本 RED / 0 SURVIVED (対照は SURVIVED)
- ATAPI の待ち上限 (ループ回数) を tick の秒単位にする件は別票

## 3-6. 待ちの上限を秒で (2026-09-26、[TASK_ATAPI_TIMEOUT](../../docs/archive/realhw_v21/TASK_ATAPI_TIMEOUT.md))

- BSY / DRQ の待ちを `IDE_TIMEOUT_LOOP` (100 万回の inp) から**時間**へ: `atapi_wait_clear` が読み 1 回ごとに
  `cpu_delay_us(ATAPI_POLL_US = 100µs)` を挟んで合計を数える。上限は `ATAPI_CMD_TIMEOUT_US` 10 秒 (init の間は
  `ATAPI_INIT_TIMEOUT_US` 5 秒)、SRST の後は `ATAPI_SRST_TIMEOUT_US` 31 秒。tick は使わない (割り込みが開いている保証が無い)
- DEVICE RESET は、コマンドの待ちが期限切れになった後、次の選択でさらに上限まで待っても BSY / DRQ のときだけ
- `atapi_srst` は int を返し、31 秒で BSY が落ちなければ `atapi_recover` / `atapi_init` は DRV_HEAD を書かずに諦める
- NOT READY / 04h/02h には START STOP UNIT (開始) を 1 回出して出し直す。準備中のまま諦めたら ASC/ASCQ の行
- 模型 (strict): 時計 `np2_now()` = 贋の `cpu_delay_us` の合計 + ステータスの読み 1 回 1µs。秒で見せる BSY
  (`busy_until`)、スピンアップ (`spinup_us`)、CDB の前の BSY (`cdb_busy_us`)、SRST 後の BSY (`srst_busy_us`)、
  固まったまま戻らない装置 (`dead` / `die_on_packet`)、START UNIT が要る装置 (`need_start` / `start_ignored`)。
  `np2_sel_busy` (1) の「30 万回 BSY」は「5 秒 BSY」に書き換えた (回数の模型では 100µs 刻みの待ちが 30 秒になる)
- ケース 4 本: `np2_spinup` (3 秒・9 秒は READ(10) 1 回・リセット 0、25 秒はリセット 1 回で、それは BSY が上限を
  越えた後、CDB の前の 3 秒)、`np2_srst_long` (SRST 後 10 秒・25 秒を待ちきる、40 秒は 31 秒で諦め DRV_HEAD も PACKET も
  書かない)、`np2_boot_worst` (起動の最悪時間 41.002 秒 / 46.002 秒に固定)、`np2_start_unit`
- 変異 62〜73 (上限を回数に戻す × 2、PACKET 上限 1 秒、SRST の戻り値無視、SRST を PACKET の上限で諦める、init の SRST
  戻り値無視、init も 10 秒、init 後に上限を戻さない、START UNIT を出さない、何度も出す、ASCQ を読まない、ASC/ASCQ の行
  を出さない) は全部 RED。既存の変異のうち待ち・回復の形が変わった 7 本はパターンを新しい形に合わせた。
  全 73 本 RED / 0 SURVIVED (対照は SURVIVED)

## 4. 未検証

- **実機・NP21/W での速さは測っていない** (PM が測る)。NP21/W は CD のシーク・回転を模擬しないので
  速さの差は実機でしか出ない
- 実機の CD ドライブが 16 セクタの READ(10) と 0x8000 の byte count limit を受けるか。困ったら
  `ATAPI_READ_MAX_SECTORS` を下げる
- 実機のドライブが READ(10) で UNIT ATTENTION を返すか (返さなければ 2 秒規則だけが効く)
- DEVICE RESET (08h) / SRST の回復と、NOT READY / 04h の待ち (250ms × 20 = 最大 5 秒)、秒の上限
  (スピンアップ・SRST 後の BSY・START UNIT) は実機でしか踏まない
  (NP21/W は固まらず、READ CAPACITY で UA / NOT READY を返さない)。`atapi_get_stats` の
  `dev_resets` / `soft_resets` / `ready_retries` で踏んだかが分かる
- HDD (ext2) への書き込みの速さは見ていない。CD 側が速くなった後は、そちらが律速になりうる
