/* ======================================================================== */
/*  BOOTINFO_HOST.C — kernel/bootinfo_check.c をそのままホストで回す        */
/*                                                                          */
/*  実物の検証関数を 1 行も写さずに #include する (I/O も低位メモリも無い)。  */
/*  記録: tools/tests/bootinfo_tdd.md                                       */
/*  票  : docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 0                        */
/*                                                                          */
/*  域はローダが書くのと同じ**バイト列**で組む (構造体を重ねない — ホストの  */
/*  u32 は 64bit)。期待値は式ではなく数で書く。                             */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/bootinfo_check.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* 実機 Ra266 で想定される 8GB の答え (F1 の IDENTIFY と同じ 16/63) と、
 * NP21/W の NHD (8/17) の答え。 */
static void put16(u8 *p, unsigned v) { p[0] = (u8)(v & 0xFF); p[1] = (u8)(v >> 8); }

static void drive(u8 *raw, int slot, unsigned da, unsigned valid, unsigned cf,
                  unsigned ah, unsigned bx, unsigned cx, unsigned dh, unsigned dl,
                  unsigned queried)
{
    u8 *r = raw + BI_OFF_DRIVE0 + slot * BI_DRIVE_SIZE;
    r[BI_DRV_DA] = (u8)da;
    r[BI_DRV_VALID] = (u8)valid;
    r[BI_DRV_CF] = (u8)cf;
    r[BI_DRV_AH] = (u8)ah;
    put16(r + BI_DRV_BX, bx);
    put16(r + BI_DRV_CX, cx);
    r[BI_DRV_DH] = (u8)dh;
    r[BI_DRV_DL] = (u8)dl;
    r[BI_DRV_QUERIED] = (u8)queried;
}

/* FD ローダが実機で書くはずの域: 80h = 8GB (16/63)、81h = 無し (CF=1, AH=60h) */
static void make_fd(u8 *raw)
{
    memset(raw, 0, BOOTINFO_WIRE_SIZE);
    put16(raw + BI_OFF_VERSION, 2);
    raw[BI_OFF_SOURCE] = 1;
    raw[BI_OFF_NDRIVES] = 2;
    drive(raw, 0, 0x80, 1, 0, 0x00, 512, 16382, 16, 63, 1);
    drive(raw, 1, 0x81, 0, 1, 0x60, 0, 0, 0, 0, 1);
    bootinfo_seal(raw);
}

static void good(void)
{
    u8 raw[BOOTINFO_WIRE_SIZE];
    struct bootinfo bi;

    make_fd(raw);
    /* 封の値は数で見る (式で作ると両方同じだけずれても気づけない) */
    CHECK(raw[0] == 0x42 && raw[1] == 0x4F && raw[2] == 0x54 && raw[3] == 0x49);
    CHECK(raw[0x2C] == 0xBF && raw[0x2D] == 0xB0 && raw[0x2E] == 0xAB && raw[0x2F] == 0xB6);
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK);
    CHECK(bi.status == BOOTINFO_OK);
    CHECK(bi.source == 1 && bi.ndrives == 2 && bi.version == 2);
    CHECK(bi.drive[0].da == 0x80 && bi.drive[0].valid == 1);
    CHECK(bi.drive[0].seclen == 512 && bi.drive[0].cyl == 16382);
    CHECK(bi.drive[0].heads == 16 && bi.drive[0].spt == 63);
    CHECK(bi.drive[1].da == 0x81 && bi.drive[1].valid == 0);
    CHECK(bi.drive[1].cf == 1 && bi.drive[1].ah == 0x60);
    /* 長さ不足・NULL は断る */
    CHECK(bootinfo_parse(raw, BOOTINFO_WIRE_SIZE - 1, &bi) == BOOTINFO_ERR_ARG);
    CHECK(bootinfo_parse(0, BOOTINFO_WIRE_SIZE, &bi) == BOOTINFO_ERR_ARG);
}

/* 情報域の無効: magic 無し (ローダが書いていない / 起動のたびの初期化で消えた) */
static void magic(void)
{
    u8 raw[BOOTINFO_WIRE_SIZE];
    struct bootinfo bi;

    make_fd(raw);
    raw[BI_OFF_MAGIC] = 0; raw[1] = 0; raw[2] = 0; raw[3] = 0;
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_MAGIC);
    CHECK(bi.drive[0].valid == 0);
    /* 全部 0 (bi_clear 直後で止まった域) */
    memset(raw, 0, sizeof(raw));
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_MAGIC);
    CHECK(bi.magic == 0);
    /* フォントのゴミ (0x7E00 がフォントキャッシュの内側) */
    memset(raw, 0xFF, sizeof(raw));
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_MAGIC);
}

/* 書きかけ: magic まで書いて check の前に止まった / check が壊れた */
static void check_word(void)
{
    u8 raw[BOOTINFO_WIRE_SIZE];
    struct bootinfo bi;

    make_fd(raw);
    memset(raw + BI_OFF_CHECK, 0, 4);
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_CHECK);
    CHECK(bi.drive[0].valid == 0);
    make_fd(raw);
    raw[BI_OFF_CHECK + 3] ^= 0x80;
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_CHECK);
}

static void version(void)
{
    u8 raw[BOOTINFO_WIRE_SIZE];
    struct bootinfo bi;

    make_fd(raw);
    put16(raw + BI_OFF_VERSION, 1);   /* 旧 v1 (イメージ欄なし) のローダ */
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_VERSION);
    make_fd(raw);
    put16(raw + BI_OFF_VERSION, 3);
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_VERSION);
    make_fd(raw);
    raw[BI_OFF_NDRIVES] = 3;
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_NDRIVES);
}

/* ドライブ記録が封の後で変わった (部分的な上書き) */
static void sum(void)
{
    u8 raw[BOOTINFO_WIRE_SIZE];
    struct bootinfo bi;

    make_fd(raw);
    raw[BI_OFF_DRIVE0 + BI_DRV_DH] = 8;   /* 16 → 8 (幾何が化ける) */
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_SUM);
    CHECK(bi.drive[0].valid == 0);
}

/* 1 ドライブの規則: queried / DA / CF / BX=512 / CX,DH,DL != 0 / ローダの valid */
static int one(unsigned da, unsigned valid, unsigned cf, unsigned bx,
               unsigned cx, unsigned dh, unsigned dl, unsigned queried)
{
    u8 raw[BOOTINFO_WIRE_SIZE];
    struct bootinfo bi;

    make_fd(raw);
    drive(raw, 0, da, valid, cf, 0, bx, cx, dh, dl, queried);
    bootinfo_seal(raw);
    if (bootinfo_parse(raw, sizeof(raw), &bi) != BOOTINFO_OK) return -1;
    return bi.drive[0].valid;
}

static void rules(void)
{
    CHECK(one(0x80, 1, 0, 512, 16382, 16, 63, 1) == 1);
    CHECK(one(0x81, 1, 0, 512, 272, 8, 17, 1) == 1);     /* NP21/W の NHD */
    CHECK(one(0x80, 1, 0, 256, 615, 8, 33, 1) == 0);     /* SASI 256B */
    CHECK(one(0x80, 1, 0, 2048, 100, 1, 1, 1) == 0);     /* CD */
    CHECK(one(0x80, 1, 0, 0, 0, 0, 0, 1) == 0);          /* NP21/W の空きスロット */
    CHECK(one(0x80, 1, 0, 512, 16382, 0, 63, 1) == 0);   /* DH = 0 */
    CHECK(one(0x80, 1, 0, 512, 16382, 16, 0, 1) == 0);   /* DL = 0 */
    CHECK(one(0x80, 1, 0, 512, 0, 16, 63, 1) == 0);      /* CX = 0 */
    CHECK(one(0x80, 1, 1, 512, 16382, 16, 63, 1) == 0);  /* CF = 1 */
    CHECK(one(0x80, 1, 0, 512, 16382, 16, 63, 0) == 0);  /* 問い合わせていない */
    CHECK(one(0x80, 0, 0, 512, 16382, 16, 63, 1) == 0);  /* ローダが 0 と書いた */
    CHECK(one(0x82, 1, 0, 512, 16382, 16, 63, 1) == 0);  /* 知らない DA */
    CHECK(one(0x00, 1, 0, 512, 16382, 16, 63, 1) == 0);
}

static void format(void)
{
    u8 raw[BOOTINFO_WIRE_SIZE];
    struct bootinfo bi;
    struct bootinfo_ata a;
    char line[BOOTINFO_LINE_MAX];

    make_fd(raw);
    bootinfo_parse(raw, sizeof(raw), &bi);
    bootinfo_format_bios(&bi, 0, line, sizeof(line));
    CHECK(strcmp(line, "[hdd] bios da=80 cf=0 ah=00 len=512 C/H/S=16382/16/63 src=fd") == 0);
    bootinfo_format_bios(&bi, 1, line, sizeof(line));
    CHECK(strcmp(line, "[hdd] bios da=81 cf=1 ah=60 len=0 C/H/S=0/0/0 src=fd (unusable)") == 0);

    raw[BI_OFF_SOURCE] = 2;                       /* sum の外なので封はそのまま */
    drive(raw, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0);     /* HDD ローダは 1 台だけ */
    bootinfo_seal(raw);
    bootinfo_parse(raw, sizeof(raw), &bi);
    bootinfo_format_bios(&bi, 0, line, sizeof(line));
    CHECK(strcmp(line, "[hdd] bios da=80 cf=0 ah=00 len=512 C/H/S=16382/16/63 src=hdd") == 0);
    bootinfo_format_bios(&bi, 1, line, sizeof(line));
    CHECK(strcmp(line, "[hdd] bios da=00 not queried") == 0);

    memset(raw, 0, sizeof(raw));
    bootinfo_parse(raw, sizeof(raw), &bi);
    bootinfo_format_bios(&bi, 0, line, sizeof(line));
    CHECK(strcmp(line, "[hdd] bios geom: none (magic 00000000 err=-2)") == 0);

    a.def_cyl = 16382; a.def_heads = 16; a.def_spt = 63;
    a.cur_cyl = 16382; a.cur_heads = 16; a.cur_spt = 63;
    a.cur_valid = 1; a.lba = 1; a.total = 16514063UL;
    bootinfo_format_ata(&a, 0, line, sizeof(line));
    CHECK(strcmp(line, "[hdd] ata0 def=16382/16/63 cur=16382/16/63(valid) lba=1 total=16514063") == 0);
    a.cur_valid = 0; a.lba = 0;
    bootinfo_format_ata(&a, 1, line, sizeof(line));
    CHECK(strcmp(line, "[hdd] ata1 def=16382/16/63 cur=16382/16/63(n/a) lba=0 total=16514063") == 0);

    /* 小さい buf でも溢れない (終端される) */
    memset(line, 'X', sizeof(line));
    bootinfo_format_ata(&a, 0, line, 8);
    CHECK(line[7] == '\0' && line[8] == 'X');
}

/* イメージ欄 (v2): ローダが検査済みイメージの CRC を記録したときだけ有効 */
static void image(void)
{
    u8 raw[BOOTINFO_WIRE_SIZE];
    struct bootinfo bi;

    /* 記録なし (bi_clear の 0 のまま = FD/HDD ローダが展開前に止まった) */
    make_fd(raw);
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK);
    CHECK(bi.img_valid == 0 && bi.img_crc == 0);

    /* 記録あり。チェック語の値は数で見る: 0x12345678 ^ 446969 ^ 'IMGC' */
    make_fd(raw);
    bootinfo_seal_image(raw, 0x12345678UL, 446969UL);
    CHECK(raw[0x38] == 0xC8 && raw[0x39] == 0xCA && raw[0x3A] == 0x75 && raw[0x3B] == 0x51);
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK);
    CHECK(bi.img_valid == 1 && bi.img_crc == 0x12345678UL && bi.img_size == 446969UL);

    /* CRC の欄だけ化けた (チェック語が合わない) */
    raw[BI_OFF_IMG_CRC] ^= 1;
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK);
    CHECK(bi.img_valid == 0 && bi.img_crc == 0);

    /* チェック語を書く前に止まった */
    make_fd(raw);
    bootinfo_seal_image(raw, 0xCAFEF00DUL, 1000UL);
    memset(raw + BI_OFF_IMG_CHECK, 0, 4);
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK);
    CHECK(bi.img_valid == 0);

    /* 大きさ 0 は記録なし (crc = size = 0 のとき check = KEY になる偶然を拾わない) */
    make_fd(raw);
    bootinfo_seal_image(raw, 0UL, 0UL);
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK);
    CHECK(bi.img_valid == 0);

    /* 主部が無効ならイメージ欄も使わない */
    make_fd(raw);
    bootinfo_seal_image(raw, 0x12345678UL, 446969UL);
    raw[BI_OFF_CHECK] ^= 1;
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_CHECK);
    CHECK(bi.img_valid == 0);

    /* イメージ欄は主部の sum に入らない (後から書いても主部は有効のまま) */
    make_fd(raw);
    bootinfo_seal_image(raw, 0xFFFFFFFFUL, 520192UL);
    CHECK(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK);
    CHECK(bi.img_valid == 1 && bi.img_crc == 0xFFFFFFFFUL && bi.img_size == 520192UL);
}

/* NASM 側の写しと照合するための値を出す (test_bootinfo.py が読む) */
static void dump(void)
{
#define D(n) printf(#n "=%lu\n", (unsigned long)(n))
    D(BOOTINFO_MAGIC); D(BOOTINFO_VERSION); D(BOOTINFO_CHECK); D(BOOTINFO_NDRIVES);
    D(BOOTINFO_SRC_FD); D(BOOTINFO_SRC_HDD); D(BOOTINFO_SECLEN); D(BOOTINFO_WIRE_SIZE);
    D(BI_OFF_MAGIC); D(BI_OFF_VERSION); D(BI_OFF_SOURCE); D(BI_OFF_NDRIVES);
    D(BI_OFF_DRIVE0); D(BI_DRIVE_SIZE); D(BI_OFF_SUM); D(BI_OFF_CHECK);
    D(BI_DRV_DA); D(BI_DRV_VALID); D(BI_DRV_CF); D(BI_DRV_AH); D(BI_DRV_BX);
    D(BI_DRV_CX); D(BI_DRV_DH); D(BI_DRV_DL); D(BI_DRV_QUERIED);
    D(BI_OFF_IMG_CRC); D(BI_OFF_IMG_SIZE); D(BI_OFF_IMG_CHECK); D(BOOTINFO_IMG_KEY);
#undef D
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "good")) good();
    else if (!strcmp(c, "magic")) magic();
    else if (!strcmp(c, "check_word")) check_word();
    else if (!strcmp(c, "version")) version();
    else if (!strcmp(c, "sum")) sum();
    else if (!strcmp(c, "rules")) rules();
    else if (!strcmp(c, "format")) format();
    else if (!strcmp(c, "image")) image();
    else if (!strcmp(c, "dump")) { dump(); return 0; }
    else { fprintf(stderr, "unknown case %s\n", c); return 2; }
    return failed ? 1 : 0;
}
