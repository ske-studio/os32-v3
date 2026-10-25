# TASK_MAGIC_NUM2 — fork 前からある行のマジックナンバーに名前を付ける ([C4]、OS 側)

> 上位: [ROADMAP](../../ROADMAP.md)・規則 [C4] ([POLICY_DEV](../../POLICY_DEV.md) §2「定数定義の3層管理」)・前の票 [TASK_MAGIC_NUM](TASK_MAGIC_NUM.md) (v3 で足された行)

> 状態: **受入完了 (2026-10-25)** — N1〜N14 が main。どの段も sol 実装・astra 取り込み可で、機械語の差は 0 (例外は ABI 世代の値だけ: N1 の FEP、N14 の CD・FM・PCM)。洗い出しの表は `~/os32-tmp/run/magic2/survey_{k,d,f,b}.md`。

## 0. ユーザー指示 (2026-10-25)

「マジックナンバーの洗い出し。修正箇所を洗い出してから修正票を書き出し、修正してください。」

PM の解釈: 前の票 TASK_MAGIC_NUM が外した **fork (a238ab1) 前からある行**を対象にする。範囲は OS 側 (kernel・exec・kapi・drivers・platform・fs・gfx・net・lib・include・arch・boot・kdebug・hwdev)。ユーザーランドは肥大化の調査のときの指示 (「ユーザーランドはまだ大丈夫」) に合わせて今回は入れない。

## 1. 洗い出しの結果

除いたもの: 取り込んだ外部コード (lib/sqlite3・lib/zlib・FatFs の本体)、生成物、自己試験の失敗ビット、配置を固定する STATIC_ASSERT の期待値、コメントの中の数。

| 調べ | 範囲 | A | B | C | 目立つもの |
|---|---|---|---|---|---|
| k | kernel・exec・kapi | 40 | 68 | 32 | GDT/IDT/PIC の値、isr の保存フレームの添字、v86 のレジスタ語・BIOS ワーク、TVRAM の属性、IME の容量 |
| d | drivers・platform | 33 | 76 | 36 | NIC の既存名と重複 (ne2000・lgy98)、IDE の IDENTIFY の欄、ATAPI の CDB、FDC の結果、ループデバイスの形式の欄 |
| f | fs・gfx・net・lib・include・arch | 34 | 70 | 26 | Ethernet の値を net/link が別に持つ、ext2 の値の二重定義 (ext2.h と ext2_layout.h)、tvram.h と pc98.h の重複、GDC のポートの別名、HostDrv の 260 |
| b | boot・kdebug・hwdev | 27 | 67 | 17 | ローダの番地・probe の上限、VK32/LZ4/FAT の欄の位置、BIOS の呼び出し (kdebug・hwdev は前の票で直したので 0) |

区分は前の票と同じ (A = 名前が既にある、B = 名前が無く重複か意味が読めない、C = 局所の値)。

## 2. 方針 (PM 決定)

- 前の票 §2 と同じ: **直すのは A と B。C は調べの表で挙げたものだけ**。機械語は変えない (変える前と後の像の `objdump -d` を比べ、差は 0 か、説明の付くものだけ)。1 つの意味に名前は 1 つにし、正典の置き場は POLICY_DEV §2 に従う。
- 直さないもの: 調べの各表の末尾の「直さない方がよいもの」(ハードの資料をそのまま写した表、GDT の表、BPB、FM の音色と F-number の表、scancode の変換表、CRC の表、媒体の geometry の組など)。
- 名前を置き換えると、試験の中の変異の目印 (元の式の文字列) が壊れる。各段の最後に `~/os32-tmp/bin/stale_anchor.py <基点>` で確かめる (前の票 §4)。
- 実装は sol、レビューは astra (ROLES §0。ユーザーの指定なし)。取り込みは段ごとに check-changed。

## 3. 段

N1 が共有ヘッダの名前を作り、N2〜N13 は N1 の取り込みの後に、触るファイルが重ならないように並べて出す。各段は自分のファイルに要る局所の名前を、そのファイルか同じディレクトリのヘッダに置く。共有ヘッダ (下の N1 の列) に足す名前が足りなければ、その段では足さずに報告し、PM が N1 の追補として入れる。

| 段 | 触る所 | 調べ | 状態 |
|---|---|---|---|
| N1 | 共有の名前: include/{types,cpu,pc98,memmap,kprintf,tvram,bootinfo,config,net_eth}.h、sdk/include/os32/os32_kapi_shared.h、arch/x86/*.h、lib/utf8.h、lib/kstring.h (4 本の調べの「新しい名前」のうち、これらに置くもの) | k・d・f・b | **main** |
| N2 | kernel の CPU とメモリ: gdt・tss・idt・irq・irq_math・isr_handlers・paging・physmem・kmalloc・ktime・time_math・cpu_calibrate とそれらのヘッダ | k | **main** |
| N3 | kernel の asm: isr_stub.asm・ring3_entry.asm・v86_entry.asm と新しい .inc (C との値の一致を host 試験で固定) | k | **main** |
| N4 | kernel の v86: v86*.c・v86*.h | k | **main** |
| N5 | kernel の残り: bootinfo*・bootlog*・con_sink・console・ime*・kernel.c・sys.c・boot_splash・snd_engine とそれらのヘッダ | k | **main** |
| N6 | exec・kapi (生成物を除く) | k | **main** |
| N7 | drivers の NIC・PCI・ディスク: lgy98・ne2000*・pci*・dev・ide*・atapi | d | **main** |
| N8 | drivers の残り: fdc*・dma8237*・loop_dev・kbd・serial・mouse*・rtc・fm・pcm_cs4231*・wab_*・pc98pt・kcg、platform/ | d | **main** |
| N9 | fs (FatFs の本体を除く) | f | **main** |
| N10 | gfx・net・lib (外部由来を除く) | f | **main** |
| N11 | boot の C とヘッダ: boot_defs.h・ext2_mini.c・vk32_boot.c・lz4_mini.c・boot_debug.c・boot_main.c | b | **main** |
| N12 | boot の asm: boot_fat.asm・boot_hdd.asm・loader_hdd.asm・loader.ld・bootinfo_rm.inc と新しい .inc (C との値の一致を host 試験で固定) | b | **main** |
| N13 | boot の FAT ローダ: loader_fat_new.asm (最も密なので単独) | b | **main** |
| N14 | 追補: 各段が持ち越したもの (ABI の世代の値が変わる vfs.h の世代の上限・dma_pool.h の DMA_POOL_PAGE_SIZE・fm.h の SSG_PERIOD と NOTE、道具が数字しか読めない boot_defs.h の MAX_IMAGE_SIZE) と、各段が挙げた「共有に足したい名前」の重複の整理。ABI の世代の値は変わってよい (前の票の M2・K3 と同じ扱い、PM 決定) | 各段の報告 | **main** (sol 実装・astra 取り込み可・PM の直し: Cirrus のパレット数を戻す) — 機械語の差は CD・FM・PCM の ABI 世代の値 3 つだけ |

各段の完了条件は前の票 §3 と同じ: `make all` rc=0、機械語の比べ、`check_select.py --select --files <差分>` の候補を全部 `MUTATE=0` で rc=0、`check_select.py --lint` rc=0、`stale_anchor.py`。2 時間に収まらなければ A を先にし、残りを報告の「未了」に書く (PM が追補の段に回す)。

## 4. 取り込みで分かったこと

- 名前を寄せるとヘッダが別のヘッダを読むようになり、変異の試験の写しの木 (MIRROR などの一覧) に足りないヘッダが出る (N9 の ext2_layout.h → ext2.h で test_hdd_stage2 の変異 133 本が組めなかった)。MUTATE=0 の候補検査では見つからず、取り込みの変異込みの検査で初めて落ちる。新しく include を足した段は、変異の写しの一覧を grep で確かめる。
- 値が同じでも別の装置の仕様は名前を分ける (N14 で Cirrus の VGA DAC の数を PEGC の名前に寄せかけた)。
