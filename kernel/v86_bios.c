/* ======================================================================== */
/*  V86_BIOS.C — ゲスト BIOS の HLE                                         */
/* ======================================================================== */

#include "v86_bios.h"
#include "v86.h"
#include "kstring.h"
#include "loop_dev.h"
#include "v86_mem.h"
#include "paging.h"
#include "vfs.h"

/* 実機の IVT (1KB) と BDA (0x400-0x5FF) の退避先。
 *
 * バッキング RAM を張るとゲストから見た低位メモリは真っさらになるので、
 * 実機の値をそのまま渡してやらないとゲストは何も判断できない。
 * ROM を指す割り込みベクタもここから引き継ぐ。 */
#define REAL_SNAPSHOT_SIZE  0x600
static u8 real_lowmem[REAL_SNAPSHOT_SIZE];
static int real_saved = 0;

static u32 bios_calls = 0;
static u32 bios_last_vec = 0;
static int disk_attached = 0;

/* ドライブ表 — DA/UA と loop_dev スロットの対応 (08_dos5.md §5-6 の宿題)。
 *
 * [0] = ブートイメージ (slot 0)、[1] = 2 台目 (slot 1)。
 * daua は種別ニブル + ユニット番号の完成形 (0x90/0x91/0x80/0x81)。
 * in_use=0 のエントリはどの DA/UA にも答えない。 */
struct v86_drive {
    int in_use;
    u8  daua;
    i8  slot;
};
static struct v86_drive v86_drives[V86_DRIVE_MAX];
static u8 v86_boot_daua_v = V86_DAUA_FDD;

/* 失敗した INT 1Bh の中身。推測でなく事実で切り分けるために残す。
 * static にすると kernel.map に出ずホストから読めないのでグローバル。 */
u32 v86_disk_fail_n   = 0;      /* 失敗回数 */
u32 v86_disk_fail_why = 0;      /* 1=範囲外 2=セクタ長 3=セクタ不在 4=装置なし */
u32 v86_disk_fail_chs = 0;      /* cyl<<16 | head<<8 | sect */
u32 v86_disk_fail_len = 0;      /* n<<16 | count */
u32 v86_disk_fail_dst = 0;      /* ES:BP のリニアアドレス */

/* SEEK と ID ずれの実測用。§「物理位置と論理 ID の分離」を参照。 */
u32 v86_disk_seek_n   = 0;      /* SEEK / RECALIBRATE の回数 */
u32 v86_disk_seek_cyl = 0;      /* 最後に SEEK した物理シリンダ */
u32 v86_disk_shift_n  = 0;      /* 物理≠論理のまま読めた回数 */
u32 v86_disk_ident_n  = 0;      /* 物理=論理 (フォールバック) で読めた回数 */

/* INT 1Bh 呼び出しログ。
 *
 * カウンタだけでは「何回失敗したか」は分かっても「どういう順で何を
 * 要求されたか」が分からず、推測が始まってしまう。1 コール 8 ワードの
 * リングにそのまま積んでおけば、ホストから emu_read_mem 一発で
 * 呼び出し列を復元できる。 */
#define V86_DISKLOG_N   32
u32 v86_disklog_w = 0;                          /* 積んだエントリ数 */
u32 v86_disklog[V86_DISKLOG_N * 8];
/*  [0] AX  [1] CX  [2] DX  [3] BX
 *  [4] ES:BP のリニアアドレス
 *  [5] 呼び出し時の物理位置 (head<<8 | cyl)
 *  [6] 転送できたバイト数
 *  [7] 連続回数<<8 | 結果 (0=成功 / v86_disk_fail_why と同じ値) */

/* 直近 4 エントリのどれかと一致したら積まずに回数だけ数える。
 *
 * **これが無いとログが役に立たない。** ゲストは失敗したコマンドを
 * 何万回でもリトライするので、32 エントリのリングが一瞬で埋まり、
 * 「そこへ至る直前に何をしていたか」が消えてしまう。
 * I/O トラップログ (kernel/v86_io.c) も同じ理由で同じ作りにしてある。
 *
 * **「直前と同じなら畳む」では足りない。** ゲストは失敗したコマンドを
 * SEEK と READ の**組で**何万回もリトライするので、直前だけ見ていると
 * 2 種類が交互に積まれてリングが一瞬で埋まり、そこへ至る履歴が消える。
 * 周期 2〜4 のループを畳めるようにしておけば、最初の失敗もその後の
 * 定常ループも同じログの中に残る。 */
#define DISKLOG_LOOKBACK    4

static void disklog(u32 *frame, u32 ax_in, u32 phys, u32 done, u32 result)
{
    u32 rec[7];
    u32 *e;
    u32 back;
    int i;

    /* AX は処理の中で AH (ステータス) を書き換えるので、
     * 呼び出し時点の値を別に受け取る。 */
    rec[0] = ax_in;
    rec[1] = frame[V86F_ECX] & 0xFFFFU;
    rec[2] = frame[V86F_EDX] & 0xFFFFU;
    rec[3] = frame[V86F_EBX] & 0xFFFFU;
    rec[4] = ((frame[V86F_ES] & 0xFFFFU) << 4) + (frame[V86F_EBP] & 0xFFFFU);
    rec[5] = phys;
    rec[6] = done;

    for (back = 1; back <= DISKLOG_LOOKBACK && back <= v86_disklog_w; back++) {
        e = &v86_disklog[((v86_disklog_w - back) % V86_DISKLOG_N) * 8];
        if ((e[7] & 0xFFU) != (result & 0xFFU)) {
            continue;
        }
        for (i = 0; i < 7; i++) {
            if (e[i] != rec[i]) {
                break;
            }
        }
        if (i == 7) {
            e[7] += 0x100U;             /* 回数だけ増やす */
            return;
        }
    }

    e = &v86_disklog[(v86_disklog_w % V86_DISKLOG_N) * 8];
    for (i = 0; i < 7; i++) {
        e[i] = rec[i];
    }
    e[7] = 0x100U | (result & 0xFFU);
    v86_disklog_w++;
}

/* disk_read() が実際に転送できたバイト数 (ログ用) */
static u32 xfer_done = 0;

/* ドライブの現在位置 (SEEK で決まる物理シリンダ)。
 *
 * µPD765A は「どの物理トラックに居るか」をドライブ側のトラックレジスタで
 * 持ち、READ DATA コマンドの C/H/R/N は**そのトラック上で照合する ID**
 * でしかない。両者が一致している必要はどこにもない。
 *
 * Ys.D88 は実際にずれている。物理 (C1,H0) のセクタ ID は C=0,H=1、
 * 物理 (C2,H0) は C=1,H=0 というように、ID が物理位置より 1 シリンダ
 * 遅れて振られている (トラック 0 だけは C=0,H=0 で一致)。
 * このため「CL を物理シリンダとみなす」実装ではトラック 0 以外
 * 一切読めない。実測で INT 1Bh が 1647 回失敗していたのはこれ。
 *
 * NP21/W も同じモデルで実装されている:
 *   src/diskimage/fd/fdd_d88.c  fdd_read_d88()
 *     trkseek(fdd, (fdc.treg[fdc.us] << 1) + fdc.hd)   ← 物理位置
 *     searchsector_d88()  … c/h/r/n を照合            ← 論理 ID
 *
 * 物理シリンダはドライブのトラックレジスタ (SEEK の CL、または
 * AH bit4 付き READ の CL)、物理ヘッドは V86_FDC_HEAD() を参照。
 *
 * 【教訓】ここは実測ログからモデルを起こそうとして 2 回外した。
 * 「SEEK の DH」→ 外れ、「AL bit2 だけ」→ 半分外れ (2 本が逆順)。
 * 正解は NP21/W の src/bios/bios1b.c にそのまま書いてあった。
 * **INT 1Bh の仕様は FDC の仕様ではない。BIOS 側のソースを読むこと。**
 */
static u8 cur_cyl  = 0;
static u8 cur_head = 0;      /* SEEK の DH。実際には使わないが観測用に残す */

/* READ ID がトラック内のどこまで返したか (実機の fdc.crcn)。
 * READ ID 以外のコマンドが来たら 0 に戻す — 実機も同じ
 * (bios1b.c: `if ((CPU_AH & 0x0f) != 0x0a) fdc.crcn = 0;`)。 */
static u8 cur_id_idx = 0;

/* 物理ヘッドの決まり方。**NP21/W の BIOS エミュレーションが正本。**
 *
 *   src/bios/bios1b.c:335
 *     hd = ((CPU_DH) ^ (CPU_AL >> 2)) & 1;
 *
 * DH と DA/UA の bit2 の **XOR**。DH 単独でも AL bit2 単独でもない。
 * 実測ログからモデルを起こしたときは「AL bit2 だけ」と読み違えていて、
 * 同じシリンダの 2 本のトラックが逆順にロードされていた。
 * (どちらも「別々のトラックを読む」ので、失敗が出ずに気づきにくい)
 *
 * セクタ ID 照合に使う H は DH そのまま (bios1b.c:68 `fdc.H = CPU_DH;`)。
 * トラック選択とセクタ ID で別の値を使うのが 8259A ならぬ µPD765A の作法。 */
#define V86_FDC_HEAD(dh, al)   ((((dh) ^ ((al) >> 2))) & 1U)

u32 v86_bios_call_count(void)  { return bios_calls; }
u32 v86_bios_last_vector(void) { return bios_last_vec; }
int v86_bios_has_disk(void)    { return disk_attached; }

u32 v86_bios_boot_daua(void) { return v86_boot_daua_v; }

/* イメージをスロットにアタッチし、ドライブ表に登録する。
 * DA/UA の種別ニブルは loop_dev の HDD 判定から、ユニット番号は
 * 「同種の既登録ドライブの数」から決める (2 台目の FDD は 0x91)。 */
static int drive_register(const char *path, int slot, int idx)
{
    u8 type, unit;
    int i;

    if (loop_dev_attach(path, slot) != 0) {
        return -1;
    }
    type = loop_dev_is_hdd(slot) ? V86_DAUA_HDD : V86_DAUA_FDD;
    unit = 0;
    for (i = 0; i < V86_DRIVE_MAX; i++) {
        if (v86_drives[i].in_use && (v86_drives[i].daua & 0xF0U) == type) {
            unit++;
        }
    }
    v86_drives[idx].in_use = 1;
    v86_drives[idx].daua   = (u8)(type | unit);
    v86_drives[idx].slot   = (i8)slot;
    return 0;
}

int v86_bios_attach_disk(const char *path)
{
    if (disk_attached) {
        v86_bios_detach_disk();
    }
    v86_boot_daua_v = V86_DAUA_FDD;
    if (path == (const char *)0) {
        return 0;               /* ディスク無しで動かす */
    }
    if (drive_register(path, V86_DISK_SLOT, 0) != 0) {
        return -1;
    }
    v86_boot_daua_v = v86_drives[0].daua;
    disk_attached = 1;
    return 0;
}

int v86_bios_attach_second(const char *path)
{
    if (!disk_attached || path == (const char *)0) {
        return -1;              /* ブートイメージが先 */
    }
    if (v86_drives[1].in_use) {
        return -1;
    }
    return drive_register(path, V86_DISK_SLOT_2ND, 1);
}

void v86_bios_detach_disk(void)
{
    int i;
    for (i = 0; i < V86_DRIVE_MAX; i++) {
        if (v86_drives[i].in_use) {
            loop_dev_detach(v86_drives[i].slot);
            v86_drives[i].in_use = 0;
            v86_drives[i].daua = 0;
            v86_drives[i].slot = -1;
        }
    }
    disk_attached = 0;
    v86_boot_daua_v = V86_DAUA_FDD;
}

/* HLE する割り込みベクタ。ここに無いものは実機の IVT の値
 * (ほとんどが BIOS ROM) がそのままゲストに渡る。 */
static const u8 hle_vectors[] = {
    0x1B        /* ディスク BIOS */
};

void v86_bios_save_real(void)
{
    /* ページ 0 の状態に依存しないこと。
     *
     * paging.c は NULL ガードを R/O にしている (Not-Present だと LZ4 展開中の
     * BDA 参照で落ちるため、と経緯が残っている) が、外部プログラムを一度でも
     * 実行すると Not-Present に戻ってしまう実測がある。原因は exec 経路の
     * どこかで未特定。IVT の退避をその状態に賭けるわけにはいかないので、
     * 読む直前に自分で読める状態を作る。
     *
     * この後 v86_mem_setup() がページ 0 をバッキング RAM に張り替え、
     * teardown が R/O に戻すので、ここで R/O にしておくのが一貫している。 */
    paging_set_page(0, 0, PAGE_RO);
    kmemcpy(real_lowmem, (const void *)0, REAL_SNAPSHOT_SIZE);
    real_saved = 1;
}

/* セッション終了時に実機の IVT/BDA を書き戻す。
 * ページ 0 は実物理のままゲストに渡している (v86_mem.c 参照) ので、
 * ゲストが書き換えた分をここで巻き戻さないと OS32 側に残ってしまう。 */
void v86_bios_restore_real(void)
{
    if (!real_saved) {
        return;
    }
    paging_set_page(0, 0, PAGE_RW);
    kmemcpy((void *)0, real_lowmem, REAL_SNAPSHOT_SIZE);
}

void v86_bios_setup(void)
{
    u8 *guest = (u8 *)0;        /* リマップ済みのゲスト低位メモリ */

    /* 実機の IVT / BDA をゲストへ引き継ぐ。
     * ROM を指す割り込みベクタもここから受け継ぐので、HLE しないコールは
     * そのまま実 BIOS ROM が処理する。 */
    if (real_saved) {
        kmemcpy(guest, real_lowmem, REAL_SNAPSHOT_SIZE);
    }

    /* ゲストに見せるマシン構成を作る。
     *
     * **実機の BDA をそのまま渡してよいのは、BIOS ワークエリアを読まない
     * ゲストだけ。** Ys は読まないので実機の値で問題にならなかったが、
     * MS-DOS は 0000:0584 でブートデバイスを決め、0000:055C で装置を数え、
     * 0000:0501 でメモリ量を知る。実機 (= NHD から起動した OS32) の値を
     * 渡すと、フロッピーイメージから起動した DOS が「自分はハードディスク
     * 起動だ」と思い込み、途中から SASI 規約で INT 1Bh を呼び始める。
     *
     * **渡すべきは「実機の状態」ではなく「仮想マシンの構成」。**
     * ここで作る仮想マシンは「2HD FDD 1 台 / HDD 無し / 拡張メモリ無し /
     * コンベンショナル 640KB」。
     * → docs/tasks/v86v2/08_dos5.md §2, §3-1, §3-2
     *
     * アドレスの正本は np21w-src/src/bios/biosmem.h。
     * **0x0413 は IBM PC の流儀で PC-98 には無い。0x05AE は 1.44MB
     * ドライブ対応ビットマップ (MEMB_F144_SUP) であってメモリ量ではない。**
     * 以前はここに 572/4 を書いていて、全ドライブが 1.44MB 対応だと
     * 嘘をついていた。 */

    /* コンベンショナルメモリ量 (0x0501 = MEMB_BIOS_FLAG1 の下位 3bit) */
    guest[0x0501] = (u8)((guest[0x0501] & ~0x07U) | V86_GUEST_MEM_CODE);

    /* 拡張メモリは無い。
     *   0x0401  MEMB_EXPMMSZ  128KB 単位 << 3 (16MB まで)
     *   0x0594                16MB を超える分 (word) */
    guest[0x0401] = 0x00;
    *(u16 *)(guest + 0x0594) = 0;

    /* ブートデバイスと装置構成。
     *   0x0584  MEMB_DISK_BOOT    起動した装置の DA/UA
     *   0x055C  MEMW_DISK_EQUIP   bit0-3=2HD FDD / bit8-11=SASI / bit12-15=2DD
     *   0x0482  MEMB_DISK_EQUIPS  SCSI 用 (SASI/IDE ブートでは 0 のまま —
     *                             実測: 10_dos_hdd.md §2-4)
     *   0x0480  MEMB_SYS_TYPE     bit7 = IDE あり (IDE ポートは拒否している
     *                             ので落とす。DOS 5 標準ドライバは INT 1Bh
     *                             経由なので不要 — 実測で確認)
     *   0x05AE  MEMB_F144_SUP     1.44MB 対応ドライブのビットマップ
     *
     * DISK_EQUIP はドライブ表から組み立てる。FDD はユニットの bit0-3、
     * SASI/IDE HDD は bit8-11 (実測: HDD ブートの実機 BDA は 0x0103 だが
     * 存在しない FDD まで申告する必要はない)。 */
    {
        u16 equip = 0;
        int i;
        for (i = 0; i < V86_DRIVE_MAX; i++) {
            if (!v86_drives[i].in_use) {
                continue;
            }
            if ((v86_drives[i].daua & 0xF0U) == V86_DAUA_HDD) {
                equip |= (u16)(0x0100U << (v86_drives[i].daua & 0x03U));
            } else {
                equip |= (u16)(0x0001U << (v86_drives[i].daua & 0x03U));
            }
        }
        if (equip == 0) {
            equip = 0x0001;     /* ディスク無しセッションは従来どおり */
        }
        guest[0x0584] = v86_boot_daua_v;
        *(u16 *)(guest + 0x055C) = equip;
    }
    guest[0x0482] = 0x00;
    guest[0x0480] = (u8)(guest[0x0480] & ~0x80U);
    guest[0x05AE] = 0x00;

    bios_calls = 0;
    bios_last_vec = 0;

    /* 電源投入直後のドライブはトラック 0 に居る */
    cur_cyl  = 0;
    cur_head = 0;
    cur_id_idx = 0;
    v86_disk_seek_n = 0;
    v86_disk_seek_cyl = 0;
    v86_disk_shift_n = 0;
    v86_disk_ident_n = 0;
    v86_disk_fail_n = 0;
    v86_disklog_w = 0;
}

int v86_bios_is_hle(u32 vector)
{
    u32 i;
    for (i = 0; i < sizeof(hle_vectors); i++) {
        if (hle_vectors[i] == vector) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  戻り値の受け渡し                                                        */
/*                                                                          */
/*  INT は IDT ゲートの DPL チェックで配送前に落ちているので、ゲスト        */
/*  スタックには何も積まれていない。したがって BIOS の成否 (CF) は          */
/*  フレームの EFLAGS に直接書く。#GP から復帰する iretd がそれを            */
/*  ゲストに戻す。                                                          */
/* ------------------------------------------------------------------------ */
static void bios_set_cf(u32 *frame, int cf)
{
    if (cf) {
        frame[V86F_EFLAGS] |= 0x0001UL;
    } else {
        frame[V86F_EFLAGS] &= ~0x0001UL;
    }
}

static void bios_set_ah(u32 *frame, u32 ah)
{
    frame[V86F_EAX] = (frame[V86F_EAX] & 0xFFFF00FFUL) | ((ah & 0xFFU) << 8);
}

/* ------------------------------------------------------------------------ */
/*  INT 1Bh — ディスク BIOS                                                 */
/*                                                                          */
/*  PC-98 のレジスタ規約。前回プロジェクトが取り違えて詰まった箇所なので     */
/*  ここは特に慎重に:                                                       */
/*                                                                          */
/*    AH = コマンド (上位ビットはリトライ等の修飾。判定は下位ニブル)         */
/*    AL = DA/UA (装置番号)。0x90 = 2HD FDD#1                                */
/*    CL = シリンダ    CH = セクタ長コード (0..3 → 128<<N バイト)           */
/*    DH = ヘッド      DL = セクタ (1 起算)                                 */
/*    BX = 転送バイト数                                                     */
/*    ES:BP = 転送バッファ                                                  */
/*                                                                          */
/*  戻り: CF=0 成功 / CF=1 失敗、AH にステータス。                          */
/* ------------------------------------------------------------------------ */

/* ゲストのリニアアドレス検証は v86_mem.h の v86_guest_range_ok() に共通化。
 * 範囲外は必ず弾いて CF=1 で返す。 */
#define guest_range_ok(linear, len) v86_guest_range_ok((linear), (len))

/* この DA/UA はこちらが持っている装置か。
 *
 * **AL を無視してはいけない。** 以前は装置番号を一切見ずに、どの DA/UA に
 * 対しても「レディ」と答えていた。ゲストが 1 台しか無いはずのマシンで
 * 4 台のドライブとハードディスクを見つけてしまう。実測でも MS-DOS が
 * DA/UA 0x80 (SASI HDD) の SENSE に成功をもらい、その後 SASI 規約で
 * READ を投げてきた (docs/tasks/v86v2/08_dos5.md §2)。
 *
 * 上位ニブルが装置種別、下位 2bit がユニット番号
 * (np21w-src/src/bios/bios1b.c `bios0x1b()` の devtype = AL & 0xF0)。
 * ドライブ表を引いて loop_dev スロットを返す。-1 = うちの装置ではない。
 *
 * **DA=0x00 は 0x80 の旧 SASI 表記。** DOS 5 のブロックデバイスドライバは
 * AL=0x00 の LBA モードで叩いてくる (実測 10_dos_hdd.md §2-2、READ の
 * 126/130 がこれ)。ここを落とすと IO.SYS のロードで止まる。 */
static int daua_to_slot(u32 al)
{
    u8 t = (u8)(al & 0xF0U);
    u8 unit = (u8)(al & 0x03U);
    int i;

    if (!disk_attached) {
        return -1;
    }
    if (t == 0x00U) {
        t = V86_DAUA_HDD;
    }
    for (i = 0; i < V86_DRIVE_MAX; i++) {
        if (v86_drives[i].in_use && v86_drives[i].daua == (u8)(t | unit)) {
            return v86_drives[i].slot;
        }
    }
    return -1;
}

/* FDD 側 INT 1Bh が今回のコールで使うスロット。
 * bios_fdd_int1b() の冒頭で daua_to_slot() の結果を入れる。
 * (FDD が 2 台目 = slot 1 のこともあるので定数では持てない) */
static int cur_fdd_slot = V86_DISK_SLOT;

/* AH=04h SENSE — 装置の状態を返す。
 * ゲストは起動時にこれで「ドライブがあるか」を確かめる。
 *
 * **AH の戻り値はビットの集まりで、DOS のメディア判定はこれを見る。**
 * 正本は bios1b.c の case 0x04:
 *   0x10 書き込み禁止 / 0x01 2HD / 0x08 1MB・640KB 両用ドライブ /
 *   0x04 1.44MB 対応 (AH bit6 かつドライブが対応しているとき)
 * 常に 0x00 を返していた頃は「2DD の片用ドライブ」と判定されていた。 */
static void disk_sense(u32 *frame)
{
    u32 ax = frame[V86F_EAX] & 0xFFFFU;
    u32 ah = 0x00;

    if (ax & 0x0080U) {
        ah |= 0x01U;                    /* AL bit7 = 2HD */
    }
    if ((ax & 0x8F40U) == 0x8400U) {
        ah |= 0x08U;                    /* 1MB/640KB 両用ドライブ */
        /* 1.44MB (0x04) は用意していないので立てない */
    }

    bios_set_ah(frame, ah);
    bios_set_cf(frame, 0);
}

/* D88 の 1 セクタ読み。
 *
 * 物理トラックを (phys_cyl, head) で選び、その上で ID (id_c, id_h, id_r) を
 * 照合する。見つかったセクタの実データ長ぶんを buf へ読む。
 *
 * loop_dev_read_chs() は使えない。あれは「物理=論理」を仮定するうえ、
 * 読み出し長をイメージ全体の bps (= トラック 0 の値。Ys.D88 では 256) で
 * 頭打ちにするため、1024 バイトセクタが 256 バイトしか転送されない。
 *
 * セクタ長 (= コマンドの N) も照合する。**ここを緩めてはいけない。**
 * 緩めた実装で実測したところ、ゲストが N=3 (1024B) を要求した読みに
 * トラック 0 の 256B セクタを 3 個返して「成功」と答えてしまい、
 * ロード先の 3/4 が未初期化のまま残ってゲストがそこへ飛んで #UD になった。
 * 見つからないなら見つからないと答えるほうがゲストは正しく振る舞える。
 * NP21/W も searchsector_d88() で c/h/r に加えて n を比較している。
 *
 * 戻り値: 読めたバイト数、-1 = セクタ不在 / 長さ不一致 / I/O エラー */
static int d88_read_sector(u32 phys_cyl, u32 head,
                           u32 id_c, u32 id_h, u32 id_r, u32 seclen,
                           u8 *buf, u32 buflen, u16 *out_spt)
{
    u32 off, data_len = 0, to_read;
    u16 spt = 0;
    int fd;

    off = loop_dev_seek_d88(cur_fdd_slot,
                            (u8)phys_cyl, (u8)head,
                            (u8)id_c, (u8)id_h, (u8)id_r,
                            &data_len, &spt);
    if (off == 0 || data_len == 0 || data_len != seclen) {
        return -1;
    }
    fd = loop_dev_get_fd(cur_fdd_slot);
    if (fd < 0) {
        return -1;
    }
    to_read = data_len;
    if (to_read > buflen) {
        to_read = buflen;
    }
    vfs_seek(fd, (int)off, 0);
    if (vfs_read_fd(fd, buf, to_read) < (int)to_read) {
        return -1;
    }
    if (out_spt) {
        *out_spt = spt;
    }
    return (int)to_read;
}

/* AH=06h READ DATA / AH=01h VERIFY — CHS を進めながら BX バイト転送する。
 *
 * transfer=0 ならゲストのバッファへは書かずに読めるかどうかだけ見る
 * (ベリファイ)。実機も同じ作りで、bios1b.c の case 0x01 と case 0x06 は
 * `MEML_WRITES()` の有無しか違わない。 */
static void disk_read_common(u32 *frame, int transfer)
{
    u32 cyl   = (frame[V86F_ECX] >> 0) & 0xFFU;     /* CL */
    u32 n     = (frame[V86F_ECX] >> 8) & 0xFFU;     /* CH: セクタ長コード */
    u32 head  = (frame[V86F_EDX] >> 8) & 0xFFU;     /* DH */
    u32 sect  = (frame[V86F_EDX] >> 0) & 0xFFU;     /* DL (1 起算) */
    u32 count = frame[V86F_EBX] & 0xFFFFU;          /* BX: バイト数 */
    u32 dst   = ((frame[V86F_ES] & 0xFFFFU) << 4) +
                (frame[V86F_EBP] & 0xFFFFU);
    u16 cyls; u8 heads, spt; u16 bps; u32 total;
    u32 seclen;
    u32 ah    = (frame[V86F_EAX] >> 8) & 0xFFU;
    u32 al    = frame[V86F_EAX] & 0xFFU;            /* DA/UA */
    u32 phys_cyl;
    u32 phys_head = V86_FDC_HEAD(head, al);
    int is_d88 = (loop_dev_get_format(cur_fdd_slot) == LOOP_FMT_D88);
    /* PC-98 のセクタ長は最大 1024 バイト (N=3)。 */
    static u8 secbuf[1024];

    /* AH bit4 が立っていたら READ 自身がシークする (bios1b.c:375
     * `if (CPU_AH & 0x10) biosfd_seek(CPU_CL, ...)`)。
     * 立っていなければ直前の SEEK が決めた位置のまま読む。 */
    if (ah & 0x10) {
        cur_cyl = (u8)cyl;
    }
    phys_cyl = cur_cyl;

    v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
    v86_disk_fail_len = (n << 16) | count;
    v86_disk_fail_dst = dst;
    xfer_done = 0;

    if (!disk_attached) {
        v86_disk_fail_n++; v86_disk_fail_why = 4;
        bios_set_ah(frame, 0x60);
        bios_set_cf(frame, 1);
        return;
    }
    if (count == 0) {
        /* 実機は BX=0 を「何もせず成功」として返す (while(size) に入らない) */
        bios_set_ah(frame, 0x00);
        bios_set_cf(frame, 0);
        return;
    }
    /* 実機は 64KB 境界を跨ぐ転送をエラーにする (bios1b.c: AH=0x20)。
     * ES:BP のオフセットは 16bit なので、跨いだ瞬間にゲストが意図した
     * 場所と違うところへ落ちる。素通しにすると壊れ方が静かになる。 */
    if ((dst & 0xFFFFU) > ((dst + count - 1) & 0xFFFFU)) {
        v86_disk_fail_n++; v86_disk_fail_why = 1;
        bios_set_ah(frame, 0x20);
        bios_set_cf(frame, 1);
        return;
    }
    if (transfer && !guest_range_ok(dst, count)) {
        v86_disk_fail_n++; v86_disk_fail_why = 1;
        bios_set_ah(frame, 0x40);       /* パラメータ異常 */
        bios_set_cf(frame, 1);
        return;
    }
    if (loop_dev_get_geometry(cur_fdd_slot, &cyls, &heads, &spt,
                              &bps, &total) != 0 || spt == 0) {
        bios_set_ah(frame, 0x60);
        bios_set_cf(frame, 1);
        return;
    }

    /* 転送単位はゲストの CH (セクタ長コード) に従う。実機の FDC と同じで、
     * 何バイトのセクタを読むかは呼び出し側が指定する。
     *
     * loop_dev が報告する bps を使ってはいけない。D88 はトラックごとに
     * セクタ長が違い得るからで、実際 Ys.D88 はトラック 0 が 256B x 16、
     * 以降が 1024B x 5 になっている。bps (=先頭トラックの値) で弾くと、
     * IPL の次段が読めずに INT 1Bh を無限リトライする。
     *
     * ただし loop_dev はセクタの実長ぶんを書き込むので、ゲストのバッファへ
     * 直接読ませるとサイズが食い違ったときに溢れる。いったんカーネル側の
     * バウンスバッファへ受けてから、ゲストが要求した長さだけ渡す。 */
    seclen = 128UL << (n & 3);
    if (seclen > sizeof(secbuf)) {
        v86_disk_fail_n++; v86_disk_fail_why = 2;
        bios_set_ah(frame, 0x40);
        bios_set_cf(frame, 1);
        return;
    }
    while (count) {
        u32 i;
        u32 got;                    /* セクタから読めたバイト数 */
        u32 xfer;                   /* うちゲストへ渡す長さ */
        u16 trk_spt = 0;

        /* 実機は最後の 1 セクタだけ端数を転送する
         * (bios1b.c: `accesssize = min(size, secsize)`)。
         * **端数を捨てて成功を返してはいけない。** 以前は
         * `while (count >= seclen)` で回していたので、BX がセクタ長の
         * 倍数でないとき残りを黙って捨てて CF=0 を返していた。
         * ゲストは読めたつもりで未初期化領域を使うことになる。 */
        xfer = (count > seclen) ? seclen : count;

        if (is_d88) {
            /* まず SEEK で決まった物理トラックを見る (実機の FDC と同じ)。 */
            int r = d88_read_sector(phys_cyl, phys_head, cyl, head, sect,
                                    seclen, secbuf, seclen, &trk_spt);
            if (r < 0) {
                /* そこに無ければ「物理=論理」でもう一度探す。
                 * ゲストが SEEK を省いて BIOS の暗黙シークに頼る
                 * 場合があるため。どちらで読めたかは実測できるよう
                 * カウンタを分けてある。 */
                r = d88_read_sector(cyl, head, cyl, head, sect,
                                    seclen, secbuf, seclen, &trk_spt);
                if (r < 0) {
                    v86_disk_fail_n++; v86_disk_fail_why = 3;
                    v86_disk_fail_chs = (phys_cyl << 24) | (phys_head << 28) |
                                        (cyl << 16) | (head << 8) | sect;
                    bios_set_ah(frame, 0x40);   /* セクタ不在 */
                    bios_set_cf(frame, 1);
                    return;
                }
                phys_cyl  = cyl;
                phys_head = head;
                v86_disk_ident_n++;
            } else if (phys_cyl != cyl || phys_head != head) {
                v86_disk_shift_n++;
            }
            got = (u32)r;
            if (trk_spt != 0) {
                spt = (u8)trk_spt;      /* トラックごとに違い得る */
            }
        } else {
            /* RAW/FDI はイメージ全体で 1 つのセクタ長しか持たない。
             * ゲストが違う長さを要求してきたら、実機の FDC が ID 照合で
             * 空振りするのと同じく「セクタ不在」で返す。
             *
             * **ここを素通りさせてはいけない。** N を見ていなかった頃は、
             * MS-DOS が SASI 規約で投げた「128B / セクタ 0」の読みに
             * FAT 領域のゴミを返して成功と答えていた
             * (docs/tasks/v86v2/08_dos5.md §2)。
             * セクタ長を照合しておけば secbuf の溢れも同時に閉じる。 */
            if (seclen != (u32)bps) {
                v86_disk_fail_n++; v86_disk_fail_why = 2;
                v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
                bios_set_ah(frame, 0x40);
                bios_set_cf(frame, 1);
                return;
            }
            if (loop_dev_read_chs(cur_fdd_slot, (u16)cyl, (u8)head,
                                  (u8)sect, secbuf) != 0) {
                v86_disk_fail_n++; v86_disk_fail_why = 3;
                v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
                bios_set_ah(frame, 0x40);   /* セクタ不在 */
                bios_set_cf(frame, 1);
                return;
            }
            got = seclen;
        }

        if (got > xfer) {
            got = xfer;                 /* 端数セクタは要求ぶんだけ渡す */
        }
        if (transfer) {
            for (i = 0; i < got; i++) {
                ((volatile u8 *)dst)[i] = secbuf[i];
            }
        }
        xfer_done += got;
        dst   += xfer;
        count -= xfer;

        /* 次のセクタへ。**実機はトラックを跨がない。**
         *
         * bios1b.c は最終セクタ (R == para) を読んだ時点で
         *   - AH bit7 が立っていて物理ヘッドが 0 なら → 裏面へ続ける
         *   - そうでなければ → ループを抜ける (残っていればエラー)
         * となっていて、**シリンダは絶対に跨がない**。
         * 以前はここでヘッドもシリンダも自由に繰り上げていたので、
         * 実機ならエラーになる要求が「成功」として通ってしまい、
         * 隣のトラックの中身が返っていた。
         *
         * 物理位置も同じ歩幅で動かす。ID のずれは連続領域では一定なので、
         * 差を保ったまま動かせば整合が取れる。 */
        if (sect >= spt) {
            if ((ah & 0x80U) && phys_head == 0 && heads > 1) {
                head      = 1;
                phys_head = 1;
                sect      = 1;
            } else {
                break;
            }
        } else {
            sect++;
        }
    }

    if (count) {
        v86_disk_fail_n++; v86_disk_fail_why = 3;
        v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
        bios_set_ah(frame, 0xC0);       /* 転送しきれなかった */
        bios_set_cf(frame, 1);
        return;
    }

    bios_set_ah(frame, 0x00);
    bios_set_cf(frame, 0);
}

static void disk_read(u32 *frame)   { disk_read_common(frame, 1); }
static void disk_verify(u32 *frame) { disk_read_common(frame, 0); }

/* AH=05h WRITE — FDD 規約の書き込み。正本は bios1b.c case 0x05。
 *
 * READ と同じレジスタ規約・同じ「トラックを跨がない」歩き方で、
 * 方向だけが逆。FDI/RAW のみ対応 (D88 の ID ずらしイメージへ書く用途は
 * 無い — Ys 等は読み専用。来たら正直に AH=0x40 で返す)。
 * エラーコードは実機と同じ: 0x70 ライトプロテクト / 0x20 64KB 境界 /
 * 0xC0 転送しきれず。 */
static void disk_write(u32 *frame)
{
    u32 cyl   = (frame[V86F_ECX] >> 0) & 0xFFU;     /* CL */
    u32 n     = (frame[V86F_ECX] >> 8) & 0xFFU;     /* CH: セクタ長コード */
    u32 head  = (frame[V86F_EDX] >> 8) & 0xFFU;     /* DH */
    u32 sect  = (frame[V86F_EDX] >> 0) & 0xFFU;     /* DL (1 起算) */
    u32 count = frame[V86F_EBX] & 0xFFFFU;          /* BX: バイト数 */
    u32 src   = ((frame[V86F_ES] & 0xFFFFU) << 4) +
                (frame[V86F_EBP] & 0xFFFFU);
    u16 cyls; u8 heads, spt; u16 bps; u32 total;
    u32 seclen;
    u32 ah    = (frame[V86F_EAX] >> 8) & 0xFFU;
    u32 phys_head;
    static u8 secbuf[1024];

    if (ah & 0x10) {
        cur_cyl = (u8)cyl;
    }
    phys_head = V86_FDC_HEAD(head, frame[V86F_EAX] & 0xFFU);

    v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
    v86_disk_fail_len = (n << 16) | count;
    v86_disk_fail_dst = src;
    xfer_done = 0;

    if (!disk_attached) {
        v86_disk_fail_n++; v86_disk_fail_why = 4;
        bios_set_ah(frame, 0x60);
        bios_set_cf(frame, 1);
        return;
    }
    if (count == 0) {
        bios_set_ah(frame, 0x00);
        bios_set_cf(frame, 0);
        return;
    }
    if ((src & 0xFFFFU) > ((src + count - 1) & 0xFFFFU)) {
        v86_disk_fail_n++; v86_disk_fail_why = 1;
        bios_set_ah(frame, 0x20);
        bios_set_cf(frame, 1);
        return;
    }
    if (!guest_range_ok(src, count)) {
        v86_disk_fail_n++; v86_disk_fail_why = 1;
        bios_set_ah(frame, 0x40);
        bios_set_cf(frame, 1);
        return;
    }
    if (loop_dev_get_geometry(cur_fdd_slot, &cyls, &heads, &spt,
                              &bps, &total) != 0 || spt == 0) {
        bios_set_ah(frame, 0x60);
        bios_set_cf(frame, 1);
        return;
    }
    if (!loop_dev_is_writable(cur_fdd_slot)) {
        v86_disk_fail_n++; v86_disk_fail_why = 1;
        bios_set_ah(frame, 0x70);       /* ライトプロテクト */
        bios_set_cf(frame, 1);
        return;
    }
    if (loop_dev_get_format(cur_fdd_slot) == LOOP_FMT_D88) {
        v86_disk_fail_n++; v86_disk_fail_why = 2;
        bios_set_ah(frame, 0x40);
        bios_set_cf(frame, 1);
        return;
    }
    seclen = 128UL << (n & 3);
    if (seclen != (u32)bps || seclen > sizeof(secbuf)) {
        v86_disk_fail_n++; v86_disk_fail_why = 2;
        bios_set_ah(frame, 0x40);
        bios_set_cf(frame, 1);
        return;
    }

    while (count) {
        u32 i;
        u32 xfer = (count > seclen) ? seclen : count;

        /* 端数セクタは読んでから前半だけ差し替える (RMW)。
         * 残りを 0 で埋めて書くと隣のデータを消してしまう。 */
        if (xfer < seclen) {
            if (loop_dev_read_chs(cur_fdd_slot, (u16)cyl, (u8)head,
                                  (u8)sect, secbuf) != 0) {
                v86_disk_fail_n++; v86_disk_fail_why = 3;
                bios_set_ah(frame, 0x40);
                bios_set_cf(frame, 1);
                return;
            }
        }
        for (i = 0; i < xfer; i++) {
            secbuf[i] = ((volatile u8 *)src)[i];
        }
        if (loop_dev_write_chs(cur_fdd_slot, (u16)cyl, (u8)head,
                               (u8)sect, secbuf) != 0) {
            v86_disk_fail_n++; v86_disk_fail_why = 3;
            v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
            bios_set_ah(frame, 0x40);
            bios_set_cf(frame, 1);
            return;
        }
        xfer_done += xfer;
        src   += xfer;
        count -= xfer;

        /* READ と同じ: トラックは跨がない。AH bit7 + 表面なら裏面へ。 */
        if (sect >= spt) {
            if ((ah & 0x80U) && phys_head == 0 && heads > 1) {
                head      = 1;
                phys_head = 1;
                sect      = 1;
            } else {
                break;
            }
        } else {
            sect++;
        }
    }

    if (count) {
        v86_disk_fail_n++; v86_disk_fail_why = 3;
        bios_set_ah(frame, 0xC0);
        bios_set_cf(frame, 1);
        return;
    }

    bios_set_ah(frame, 0x00);
    bios_set_cf(frame, 0);
}

/* AH=0Ah READ ID — ヘッドの下を次に通るセクタの ID を返す。
 *
 * **ゲストはこれでディスクのフォーマットを知る。** MS-DOS 5 は
 * RECALIBRATE → SENSE → READ ID の順に呼んで、返ってきた N (セクタ長コード)
 * と R からメディアが 2HD (1024B×8) か 2DD (512B×8/9) かを判定する。
 * 未実装 (AH=0x86) で返していた頃は、ここで諦めて
 * 「MSDOS.SYS 読み込み時にエラー」になっていた。
 *
 * 実機は `fdc.crcn` にトラック内のセクタ番号を持ち、呼ばれるたびに
 * 1 つ進めて末尾で 0 に戻す (bios1b.c case 0x0a → fdd_readid_*)。
 * 返すのは物理位置の C/H と、そのセクタが実際に持っている ID の R/N。
 *
 * 実機との差: 実機は crcn が一周したときトラックも進める。
 * こちらは進めない — READ ID がシーク位置を動かす副作用は、
 * この後の READ の物理位置を狂わせるほうが害が大きい。
 * (フォーマット判定に使う限り一周する前に答えが出る) */
static void disk_readid(u32 *frame)
{
    u32 ah  = (frame[V86F_EAX] >> 8) & 0xFFU;
    u32 al  = frame[V86F_EAX] & 0xFFU;
    u32 cyl = frame[V86F_ECX] & 0xFFU;              /* CL */
    u32 head;
    u16 cyls; u8 heads, spt; u16 bps; u32 total;
    u8  id_c, id_h, id_r, id_n;
    u16 trk_spt = 0;

    if (ah & 0x10) {                                /* AH bit4 = シーク付き */
        cur_cyl = (u8)cyl;
    }
    head = V86_FDC_HEAD((frame[V86F_EDX] >> 8) & 0xFFU, al);

    if (loop_dev_get_geometry(cur_fdd_slot, &cyls, &heads, &spt,
                              &bps, &total) != 0 || spt == 0) {
        bios_set_ah(frame, 0x60);
        bios_set_cf(frame, 1);
        return;
    }

    if (loop_dev_get_format(V86_DISK_SLOT) == LOOP_FMT_D88) {
        if (loop_dev_track_id_d88(cur_fdd_slot, cur_cyl, (u8)head,
                                  (u16)cur_id_idx,
                                  &id_c, &id_h, &id_r, &id_n,
                                  &trk_spt) != 0) {
            cur_id_idx = 0;
            bios_set_ah(frame, 0xE0);               /* ID が見つからない */
            bios_set_cf(frame, 1);
            return;
        }
        spt = (u8)trk_spt;
    } else {
        /* RAW/FDI は全トラック同じ書式。ID は物理位置そのもの。 */
        if (cur_cyl >= (u8)cyls || head >= (u32)heads) {
            bios_set_ah(frame, 0xE0);
            bios_set_cf(frame, 1);
            return;
        }
        id_c = cur_cyl;
        id_h = (u8)head;
        id_r = (u8)(cur_id_idx + 1);
        id_n = loop_dev_get_sec_n(cur_fdd_slot);
    }

    if (++cur_id_idx >= spt) {
        cur_id_idx = 0;
    }

    /* CL=C / DH=H / DL=R / CH=N を返す (bios1b.c case 0x0a の末尾と同じ) */
    frame[V86F_ECX] = (frame[V86F_ECX] & 0xFFFF0000UL) |
                      ((u32)id_n << 8) | (u32)id_c;
    frame[V86F_EDX] = (frame[V86F_EDX] & 0xFFFF0000UL) |
                      ((u32)id_h << 8) | (u32)id_r;
    bios_set_ah(frame, 0x00);
    bios_set_cf(frame, 0);
}

static void bios_fdd_int1b(u32 *frame)
{
    u32 cmd = (frame[V86F_EAX] >> 8) & 0x0FU;   /* AH の下位ニブル */
    u32 ax_in = frame[V86F_EAX] & 0xFFFFU;
    u32 phys_in = ((u32)V86_FDC_HEAD((ax_in >> 8) & 0xFFU, ax_in & 0xFFU) << 8)
                  | cur_cyl;
    u32 fails_before = v86_disk_fail_n;
    int slot;

    xfer_done = 0;      /* SEEK 等でも「今回の転送量」が 0 になるように */

    /* READ ID 以外が来たらトラック内の走査位置を戻す (実機と同じ) */
    if (cmd != V86_DISK_READID) {
        cur_id_idx = 0;
    }

    /* 装置番号 (DA/UA) の判定。こちらが持っていない装置には
     * 「レディでない」で答える。
     *
     * 初期化 (AH=03h) だけは実機も装置の有無を見ずに答えるので通す
     * (bios1b.c: `if ((CPU_AH & 0x0f) != 0x03) { ...ready チェック... }`)。 */
    slot = daua_to_slot(ax_in & 0xFFU);
    if (cmd != V86_DISK_INIT && slot < 0) {
        v86_disk_fail_n++; v86_disk_fail_why = 4;
        bios_set_ah(frame, 0x60);
        bios_set_cf(frame, 1);
        disklog(frame, ax_in, phys_in, 0, v86_disk_fail_why);
        return;
    }
    cur_fdd_slot = (slot >= 0) ? slot : V86_DISK_SLOT;

    switch (cmd) {
    case V86_DISK_SENSE:
        disk_sense(frame);
        break;

    case V86_DISK_READ:
    case V86_DISK_READ_DIAG:
        disk_read(frame);
        break;

    case V86_DISK_WRITE:
        disk_write(frame);
        break;

    case V86_DISK_VERIFY:
        disk_verify(frame);
        break;

    case V86_DISK_READID:
        disk_readid(frame);
        break;

    case V86_DISK_SEEK:
        /* CL/DH の物理位置へヘッドを動かす。イメージ相手なので実体は
         * 無いが、**位置は必ず憶えておくこと**。この後の READ は
         * 「ここに居る物理トラック上で ID を照合する」ためにこれを使う。
         * ヘッドも憶える — シリンダだけだと Ys の 3 番目のロードが
         * 物理トラック 0 を見に行って空振りする。 */
        cur_cyl  = (u8)(frame[V86F_ECX] & 0xFFU);
        /* DH はそのまま憶える。**1 ビットにマスクしてはいけない。**
         * 実測で DH=3 の SEEK が来ることがあり、マスクするとヘッド 1 の
         * 別トラックが「それらしく」読めてしまう。ID は合っているので
         * 成功として返ってしまい、中身だけが違う — いちばん質の悪い壊れ方。
         * マスクしなければ範囲外トラックとして読みが失敗し、ゲストは
         * 自分で DH=0 に直して読み直す。その結果が正しい。 */
        cur_head = (u8)((frame[V86F_EDX] >> 8) & 0xFFU);
        v86_disk_seek_n++;
        v86_disk_seek_cyl = ((u32)cur_head << 8) | cur_cyl;
        bios_set_ah(frame, 0x00);
        bios_set_cf(frame, 0);
        break;

    case V86_DISK_RECAL:
        /* トラック 0 へ戻す */
        cur_cyl  = 0;
        cur_head = 0;
        v86_disk_seek_n++;
        v86_disk_seek_cyl = 0;
        bios_set_ah(frame, 0x00);
        bios_set_cf(frame, 0);
        break;

    case V86_DISK_INIT:
        bios_set_ah(frame, 0x00);
        bios_set_cf(frame, 0);
        break;

    default:
        /* WRITE (05) / FORMAT (0D) / 密度設定 (0E) は未実装。
         * CF=1 / AH=0x86 (機能なし) で返せばゲストは自前の代替手段に進める。 */
        bios_set_ah(frame, 0x86);
        bios_set_cf(frame, 1);
        break;
    }

    disklog(frame, ax_in, phys_in, xfer_done,
            (v86_disk_fail_n != fails_before) ? v86_disk_fail_why
          : ((frame[V86F_EFLAGS] & 1UL) ? 0x80U : 0U));
}

/* ======================================================================== */
/*  INT 1Bh — SASI/IDE HDD (DA=0x80 / 旧表記 0x00)                          */
/*                                                                          */
/*  **FDD と共用しない。** レジスタ規約 (CX/DL/BX)、エラーコード             */
/*  (0xD0/0x40 系)、転送モデル (LBA 線形 vs トラック内)、コマンド表          */
/*  (READ ID の成否が逆) のすべてが違う。if を差し込むと 08_dos5.md §2 の    */
/*  「偶然動く」型の事故が再発する。                                          */
/*                                                                          */
/*  正本: np21w-src/src/bios/sxsibios.c (sxsi_pos / sasifunc / sense)。      */
/*  実測: docs/tasks/v86v2/10_dos_hdd.md §2 (2026-08-11 の 154 呼び出し)。   */
/*                                                                          */
/*  レジスタ規約 (FDD と違う点だけ):                                         */
/*    CHS モード (AL bit7=1): CX=16bit シリンダ全体 / DH=ヘッド /            */
/*                            DL=セクタ (**0 起算**)                          */
/*    LBA モード (AL bit7=0): pos = (DL<<16) | CX                            */
/*    BX = バイト数。**0 は 0x10000 (64KB)** — FDD の「0 なら何もしない」と逆 */
/*    転送は pos++ の線形でトラックもシリンダも自由に跨ぐ                     */
/*  戻り: AH >= 0x20 のときだけ CF=1 (0x0F は CF=0 の成功扱い)               */
/* ======================================================================== */

/* 戻り AH と CF をまとめて書く。CF の閾値 0x20 は bios1b.c の
 * `if (ret_ah >= 0x20) flag += 1;` と np2sysp_sasi の両方に一致。 */
static void sasi_done(u32 *frame, u32 ret)
{
    bios_set_ah(frame, ret);
    bios_set_cf(frame, ret >= 0x20U);
}

/* アドレッシング。sxsi_pos() の移植。範囲外は 0xD0。 */
static u32 sasi_pos(u32 *frame, u16 cyls, u8 heads, u8 spt, u32 total_lba,
                    u32 *out_pos)
{
    u32 al = frame[V86F_EAX] & 0xFFU;
    u32 cx = frame[V86F_ECX] & 0xFFFFU;
    u32 dh = (frame[V86F_EDX] >> 8) & 0xFFU;
    u32 dl = frame[V86F_EDX] & 0xFFU;
    u32 pos;

    if (al & 0x80U) {
        /* CHS: DL は 0 起算。範囲照合してから線形位置へ落とす。 */
        if (dl >= (u32)spt || dh >= (u32)heads || cx >= (u32)cyls) {
            return 0xD0;
        }
        pos = (cx * (u32)heads + dh) * (u32)spt + dl;
    } else {
        /* LBA: 総セクタ数 <= 0xFFFFFF (u32 ファイルサイズの上限から
         * 4GB/512B = 8.4M セクタ < 16.7M) なので (DL<<16)|CX で足りる。 */
        pos = (dl << 16) | cx;
    }
    if (pos >= total_lba) {
        return 0xD0;
    }
    *out_pos = pos;
    return 0;
}

/* READ (06) / WRITE (05)。バウンスバッファ 1 セクタで pos++ の線形転送。
 * ゲスト側アドレスは ES:BP からリニアに進み、セグメントの繰り上がりは
 * 気にしない (実機の MEML_READS/WRITES も物理線形)。 */
static u8 sasi_bounce[4096];

static u32 sasi_rw(u32 *frame, int slot, int is_write,
                   u16 bps, u32 pos, u32 total_lba)
{
    u32 size = frame[V86F_EBX] & 0xFFFFU;
    u32 addr = ((frame[V86F_ES] & 0xFFFFU) << 4)
             + (frame[V86F_EBP] & 0xFFFFU);
    u32 r;

    if (size == 0) {
        size = 0x10000;         /* BX=0 は 64KB。FDD と逆 (sxsibios.c) */
    }

    if (is_write && !loop_dev_is_writable(slot)) {
        return 0x70;            /* ライトプロテクト。黙って捨てない */
    }

    while (size) {
        r = size;
        if (r > (u32)bps) {
            r = bps;
        }
        if (pos >= total_lba) {
            return 0xD0;        /* 転送中に末尾を越えた */
        }
        if (!guest_range_ok(addr, r)) {
            v86_disk_fail_n++; v86_disk_fail_why = 1;
            v86_disk_fail_dst = addr;
            return 0x40;        /* バッキング RAM の外。書くと カーネルが壊れる */
        }
        if (is_write) {
            /* 端数はセクタを読んでから前半だけ差し替える (RMW)。 */
            if (r < (u32)bps) {
                if (loop_dev_read_lba(slot, pos, 1, sasi_bounce) != 0) {
                    return 0xD0;
                }
            }
            kmemcpy(sasi_bounce, (const void *)addr, r);
            if (loop_dev_write_lba(slot, pos, 1, sasi_bounce) != 0) {
                return 0xD0;
            }
        } else {
            if (loop_dev_read_lba(slot, pos, 1, sasi_bounce) != 0) {
                return 0xD0;
            }
            kmemcpy((void *)addr, sasi_bounce, r);
        }
        addr += r;
        size -= r;
        pos++;
        xfer_done += r;
    }
    return 0;
}

/* SENSE (nibble=4)。AH=84h のときだけジオメトリをレジスタで返す。
 * 戻り AH は SASI 標準ジオメトリ (SPT=33) なら容量クラス、それ以外は
 * 0x0F (CF=0)。NHD/HDI (SPT=17 等) は常に 0x0F — 実測どおり。 */
static u32 sasi_sense(u32 *frame, u16 cyls, u8 heads, u8 spt, u16 bps)
{
    u32 ah = (frame[V86F_EAX] >> 8) & 0xFFU;

    if (ah == 0x84U) {
        frame[V86F_EBX] = (frame[V86F_EBX] & 0xFFFF0000UL) | bps;
        frame[V86F_ECX] = (frame[V86F_ECX] & 0xFFFF0000UL) | cyls;
        frame[V86F_EDX] = (frame[V86F_EDX] & 0xFFFF0000UL)
                        | ((u32)heads << 8) | spt;
    }
    if (spt == 33 && heads <= 8) {
        u32 bytes = (u32)cyls * heads * spt * bps;
        if (bytes < 10475520U)  return 0;   /* < 10MB */
        if (bytes < 15713280U)  return 1;   /* < 15MB */
        if (bytes < 20275200U)  return 2;   /* < 20MB */
        if (bytes < 41564160U)  return 3;   /* < 40MB */
        if (bytes == 41564160U) return 4;   /* = 40MB */
    }
    return 0x0F;
}

/* INIT (nibble=3)。実機は BDA 0x55C の SASI ビットを接続台数で立て直す
 * (sxsibios.c sasibios_init)。装置の有無は見ずに成功する。 */
static u32 sasi_init(void)
{
    u8 *guest = (u8 *)0;
    u16 equip = *(u16 *)(guest + 0x055C);
    int i;

    equip &= 0xF0FF;
    for (i = 0; i < V86_DRIVE_MAX; i++) {
        if (v86_drives[i].in_use &&
            (v86_drives[i].daua & 0xF0U) == V86_DAUA_HDD) {
            equip |= (u16)(0x0100U << (v86_drives[i].daua & 0x03U));
        }
    }
    *(u16 *)(guest + 0x055C) = equip;
    return 0;
}

static void bios_sasi_int1b(u32 *frame)
{
    u32 cmd = (frame[V86F_EAX] >> 8) & 0x0FU;
    u32 ax_in = frame[V86F_EAX] & 0xFFFFU;
    u32 fails_before = v86_disk_fail_n;
    u32 ret;
    u32 pos = 0;
    int slot;
    u16 cyls = 0, bps = 0;
    u8 heads = 0, spt = 0;
    u32 total_lba = 0;

    xfer_done = 0;

    /* INIT 以外は装置の有無を先に見る (sasibios_operate と同じ)。 */
    slot = daua_to_slot(ax_in & 0xFFU);
    if (cmd == V86_DISK_INIT) {
        ret = sasi_init();
        sasi_done(frame, ret);
        disklog(frame, ax_in, 0, 0, 0);
        return;
    }
    if (slot < 0 || !loop_dev_is_hdd(slot)) {
        v86_disk_fail_n++; v86_disk_fail_why = 4;
        sasi_done(frame, 0x60);
        disklog(frame, ax_in, 0, 0, v86_disk_fail_why);
        return;
    }
    loop_dev_get_geometry(slot, &cyls, &heads, &spt, &bps, &total_lba);

    switch (cmd) {
    case V86_DISK_VERIFY:               /* 実機も実読みしない */
    case V86_DISK_RECAL:
    case V86_SASI_RETRACT2:
        ret = 0;
        break;

    case V86_DISK_SENSE:
        ret = sasi_sense(frame, cyls, heads, spt, bps);
        break;

    case V86_DISK_READ:
    case V86_DISK_WRITE:
        ret = sasi_pos(frame, cyls, heads, spt, total_lba, &pos);
        if (ret == 0) {
            ret = sasi_rw(frame, slot, (cmd == V86_DISK_WRITE),
                          bps, pos, total_lba);
        }
        break;

    default:
        /* SEEK(0)/READ_DIAG(2)/READID(A)/FORMAT(D)/密度(E) その他。
         * SASI では失敗が正 — 実測でも AH=8E が飛んできて 0x40 で
         * 流している (10_dos_hdd.md §2-2)。 */
        ret = 0x40;
        break;
    }

    sasi_done(frame, ret);
    disklog(frame, ax_in, pos & 0xFFFFFFU, xfer_done,
            (v86_disk_fail_n != fails_before) ? v86_disk_fail_why
          : ((ret >= 0x20U) ? 0x80U : 0U));
}

/* devtype (AL の上位ニブル) で FDD 規約と SASI 規約に振り分ける。
 * bios1b.c `bios0x1b()` の switch(devtype) と同じ構造。 */
static void bios_int1b(u32 *frame)
{
    u32 al = frame[V86F_EAX] & 0xFFU;
    u32 devtype = al & 0xF0U;

    if (devtype == 0x80U || devtype == 0x00U) {
        bios_sasi_int1b(frame);
        return;
    }
    bios_fdd_int1b(frame);
}

void v86_bios_dispatch(u32 *frame, u32 vector)
{
    bios_calls++;
    bios_last_vec = vector;

    switch (vector) {
    case 0x1B:
        bios_int1b(frame);
        break;

    default:
        /* 未対応の BIOS コールは CF=1 / AH=0x86 (機能なし) を返す。
         * 落とさずに返せば、ゲストは自前のフォールバックに進める。 */
        bios_set_ah(frame, 0x86);
        bios_set_cf(frame, 1);
        break;
    }
}
