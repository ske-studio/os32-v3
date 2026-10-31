/* ======================================================================== */
/*  V86_BIOS.C — ゲスト BIOS の HLE                                         */
/* ======================================================================== */

#include "memmap.h"
#include "v86_bios.h"
#include "v86.h"
#include "kstring.h"
#include "loop_dev.h"
#include "v86_mem.h"
#include "../../mm/paging.h"
#include "vfs.h"

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
u32 v86_disklog_w = 0;                          /* 積んだエントリ数 */
u32 v86_disklog[V86_DISKLOG_N * V86_DISKLOG_WORDS];
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
 * I/O トラップログ (kernel/v86/v86_io.c) も同じ理由で同じ作りにしてある。
 *
 * **「直前と同じなら畳む」では足りない。** ゲストは失敗したコマンドを
 * SEEK と READ の**組で**何万回もリトライするので、直前だけ見ていると
 * 2 種類が交互に積まれてリングが一瞬で埋まり、そこへ至る履歴が消える。
 * 周期 2〜4 のループを畳めるようにしておけば、最初の失敗もその後の
 * 定常ループも同じログの中に残る。 */
#define DISKLOG_LOOKBACK    4

/* 出力レジスタへ書き換える前に呼び出し引数を保存する。 */
static void disklog_begin(u32 *rec, const u32 *frame)
{
    rec[V86_DISKLOG_AX] = frame[V86F_EAX] & U16_MAX;
    rec[V86_DISKLOG_CX] = frame[V86F_ECX] & U16_MAX;
    rec[V86_DISKLOG_DX] = frame[V86F_EDX] & U16_MAX;
    rec[V86_DISKLOG_BX] = frame[V86F_EBX] & U16_MAX;
    rec[V86_DISKLOG_ADDRESS] = ((frame[V86F_ES] & U16_MAX) << X86_REAL_SEG_SHIFT) + (frame[V86F_EBP] & U16_MAX);
}

static void disklog(u32 *rec, u32 phys, u32 done, u32 result)
{
    u32 *e;
    u32 back;
    int i;

    rec[V86_DISKLOG_PHYS] = phys;
    rec[V86_DISKLOG_DONE] = done;

    for (back = 1; back <= DISKLOG_LOOKBACK && back <= v86_disklog_w; back++) {
        e = &v86_disklog[((v86_disklog_w - back) % V86_DISKLOG_N) * V86_DISKLOG_WORDS];
        if ((e[V86_DISKLOG_RESULT] & V86_DISKLOG_RESULT_MASK) != (result & V86_DISKLOG_RESULT_MASK)) {
            continue;
        }
        for (i = 0; i < V86_DISKLOG_RESULT; i++) {
            if (e[i] != rec[i]) {
                break;
            }
        }
        if (i == V86_DISKLOG_RESULT) {
            e[V86_DISKLOG_RESULT] += V86_DISKLOG_REPEAT_ONE; /* 回数だけ増やす */
            return;
        }
    }

    e = &v86_disklog[(v86_disklog_w % V86_DISKLOG_N) * V86_DISKLOG_WORDS];
    for (i = 0; i < V86_DISKLOG_RESULT; i++) {
        e[i] = rec[i];
    }
    e[V86_DISKLOG_RESULT] = V86_DISKLOG_REPEAT_ONE | (result & V86_DISKLOG_RESULT_MASK);
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
static u8 cur_cyl[V86_DRIVE_MAX];
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
        if (v86_drives[i].in_use && (v86_drives[i].daua & BIOS_DA_TYPE_MASK) == type) {
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
    BIOS_INT_DISK        /* ディスク BIOS */
};

void v86_bios_setup(void)
{
    u8 *guest = (u8 *)0;        /* リマップ済みのゲスト低位メモリ */

    /* 実機の IVT / BDA をゲストへ引き継ぐ。
     * ROM を指す割り込みベクタもここから受け継ぐので、HLE しないコールは
     * そのまま実 BIOS ROM が処理する。 */
    kmemcpy(guest, v86_bios_real_lowmem(), PAGE_SIZE);

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
     * → docs/archive/v21/v86v2/08_dos5.md §2, §3-1, §3-2
     *
     * アドレスの正本は np21w-src/src/bios/biosmem.h。
     * **0x0413 は IBM PC の流儀で PC-98 には無い。0x05AE は 1.44MB
     * ドライブ対応ビットマップ (MEMB_F144_SUP) であってメモリ量ではない。**
     * 以前はここに 572/4 を書いていて、全ドライブが 1.44MB 対応だと
     * 嘘をついていた。 */

    /* コンベンショナルメモリ量 (0x0501 = MEMB_BIOS_FLAG1 の下位 3bit) */
    guest[BIOS_WORK_SYSCLK] = (u8)((guest[BIOS_WORK_SYSCLK] & ~BIOS_MEM_CODE_MASK) | V86_GUEST_MEM_CODE);

    /* 拡張メモリは無い。
     *   0x0401  MEMB_EXPMMSZ  128KB 単位 << 3 (16MB まで)
     *   0x0594                16MB を超える分 (word) */
    guest[BIOS_WORK_MEM_128KB] = 0x00;
    *(u16 *)(guest + BIOS_WORK_MEM_HIGH_MB) = 0;

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
            if ((v86_drives[i].daua & BIOS_DA_TYPE_MASK) == V86_DAUA_HDD) {
                equip |= (u16)(BIOS_DISK_SASI_BIT0 << (v86_drives[i].daua & BIOS_DA_UNIT_MASK));
            } else {
                equip |= (u16)(0x0001U << (v86_drives[i].daua & BIOS_DA_UNIT_MASK));
            }
        }
        if (equip == 0) {
            equip = 0x0001;     /* ディスク無しセッションは従来どおり */
        }
        guest[BIOS_WORK_BOOT_DAUA] = v86_boot_daua_v;
        *(u16 *)(guest + BIOS_WORK_DISK_EQUIP) = equip;
    }
    guest[BIOS_WORK_SCSI_EQUIP] = 0x00;
    guest[BIOS_WORK_SYS_TYPE] = (u8)(guest[BIOS_WORK_SYS_TYPE] & ~BIOS_SYS_IDE_PRESENT);
    guest[BIOS_WORK_F144] = 0x00;

    bios_calls = 0;
    bios_last_vec = 0;

    /* 電源投入直後のドライブはトラック 0 に居る */
    for (u32 i = 0; i < V86_DRIVE_MAX; i++) {
        cur_cyl[i] = 0;
    }
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
    frame[V86F_EAX] = V86_REG_SET_AH(frame[V86F_EAX], ah);
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
 * READ を投げてきた (docs/archive/v21/v86v2/08_dos5.md §2)。
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
    u8 t = (u8)(al & BIOS_DA_TYPE_MASK);
    u8 unit = (u8)(al & BIOS_DA_UNIT_MASK);
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
    u32 ax = frame[V86F_EAX] & U16_MAX;
    u32 ah = 0x00;

    if (ax & BIOS_DISK_CMD_2HD) {
        ah |= 0x01U;                    /* AL bit7 = 2HD */
    }
    if ((ax & BIOS_DISK_SENSE_AX_MASK) == BIOS_DISK_SENSE_AX_DUAL) {
        ah |= BIOS_DISK_STATUS_DUAL;    /* 1MB/640KB 両用ドライブ */
        /* 1.44MB (0x04) は用意していないので立てない */
    }

    bios_set_ah(frame, ah);
    bios_set_cf(frame, 0);
}

#include "v86_bios_fdd_read.inc"

#include "v86_bios_fdd_command.inc"

#include "v86_bios_sasi.inc"

/* devtype (AL の上位ニブル) で FDD 規約と SASI 規約に振り分ける。
 * bios1b.c `bios0x1b()` の switch(devtype) と同じ構造。 */
static void bios_int1b(u32 *frame)
{
    u32 al = frame[V86F_EAX] & U8_MAX;
    u32 devtype = al & BIOS_DA_TYPE_MASK;

    if (devtype == V86_DAUA_HDD || devtype == 0x00U) {
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
    case BIOS_INT_DISK:
        bios_int1b(frame);
        break;

    default:
        /* 未対応の BIOS コールは CF=1 / AH=0x86 (機能なし) を返す。
         * 落とさずに返せば、ゲストは自前のフォールバックに進める。 */
        bios_set_ah(frame, BIOS_DISK_STATUS_UNSUPPORTED);
        bios_set_cf(frame, 1);
        break;
    }
}
