/* ======================================================================== */
/*  BOOT_DEBUG.C — HDDブートデバッグ用 (最小限ステップ確認版)                 */
/*                                                                          */
/*  各ステップの成功/失敗をTVRAMに表示して停止する。                          */
/*  ext2やLZ4は使わず、IDEセクタ読み出しの基本動作のみ確認。                  */
/*                                                                          */
/*  表示内容:                                                                */
/*    行4: ジオメトリ (DA, Heads, SPT)                                       */
/*    行5: LBA 0 読み出しテスト (先頭8バイトダンプ)                           */
/*    行6: LBA 1 読み出しテスト (パーティションテーブル先頭ダンプ)            */
/*    行7: パーティション解析結果 (bootable, sys_type)                       */
/*    行8: パーティション開始LBA                                             */
/*    行9: ext2 SB読み出し (LBA = part_lba+2, マジック位置ダンプ)             */
/*    行10: ext2 マジック判定結果                                             */
/* ======================================================================== */

#include "boot_defs.h"
#include "pc98pt.h"   /* 区画表の共有部 (標準配置) */

/* ================================================================ */
/*  16進表示ユーティリティ                                           */
/* ================================================================ */

static void hex8(u8 val, char *buf)
{
    static const char h[] = "0123456789ABCDEF";
    buf[0] = h[(val >> 4) & 0x0F];
    buf[1] = h[val & 0x0F];
    buf[2] = '\0';
}

static void hex32(u32 val, char *buf)
{
    int i;
    static const char h[] = "0123456789ABCDEF";
    for (i = 7; i >= 0; i--) {
        buf[i] = h[val & 0x0F];
        val >>= 4;
    }
    buf[8] = '\0';
}

/* prefix + 8桁hex を1行に表示 */
static void show_val(u32 tvram, const char *prefix, u32 val)
{
    char msg[64];
    char hx[9];
    int i = 0;
    const char *p = prefix;

    while (*p && i < 50) msg[i++] = *p++;
    hex32(val, hx);
    msg[i++] = hx[0]; msg[i++] = hx[1]; msg[i++] = hx[2]; msg[i++] = hx[3];
    msg[i++] = hx[4]; msg[i++] = hx[5]; msg[i++] = hx[6]; msg[i++] = hx[7];
    msg[i] = '\0';
    boot_print_asm(tvram, msg);
}

/* バッファの先頭 n バイトを hex ダンプ表示 */
static void show_dump(u32 tvram, const char *prefix, u8 *data, int n)
{
    char msg[80];
    char hx[3];
    int i = 0, j;
    const char *p = prefix;

    while (*p && i < 30) msg[i++] = *p++;

    for (j = 0; j < n && i < 76; j++) {
        hex8(data[j], hx);
        msg[i++] = hx[0];
        msg[i++] = hx[1];
        msg[i++] = ' ';
    }
    msg[i] = '\0';
    boot_print_asm(tvram, msg);
}

/* ================================================================ */
/*  boot_main — 最小デバッグ版                                       */
/* ================================================================ */
int boot_main(void)
{
    u8 buf[512];
    u32 row;
    u32 part_lba = 0;
    int i;

    /* 行0: タイトル (ASM側で表示済み)                          */
    /* 行1: [0]IDE OK (ASM側)                                   */
    /* 行2: [0.5]BSS CLR (ASM側)                                */
    /* 行3: [1]CALL MAIN (ASM側)                                */
    /* ここから行4 (offset 640) を使用                           */
    row = 0xA0000 + 640;

    /* ---- Step 0: ジオメトリ表示 ---- */
    {
        char geo[40];
        char h1[3], h2[3], h3[3];
        int p = 0;
        geo[p++] = '['; geo[p++] = 'G'; geo[p++] = ']';
        geo[p++] = 'D'; geo[p++] = 'A'; geo[p++] = '=';
        hex8(param_da, h1);
        geo[p++] = h1[0]; geo[p++] = h1[1]; geo[p++] = ' ';
        geo[p++] = 'H'; geo[p++] = '=';
        hex8(param_heads, h2);
        geo[p++] = h2[0]; geo[p++] = h2[1]; geo[p++] = ' ';
        geo[p++] = 'S'; geo[p++] = '=';
        hex8(param_spt, h3);
        geo[p++] = h3[0]; geo[p++] = h3[1];
        geo[p] = '\0';
        boot_print_asm(row, geo);
        row += 160;
    }

    /* ---- Step 1: LBA 0 読み出し (MBR/IPL) ---- */
    boot_print_asm(row, "[1]Read LBA0...");
    row += 160;

    boot_read_sector_asm(0, buf);
    show_dump(row, " LBA0:", buf, 16);
    row += 160;

    /* ---- Step 2: LBA 1 読み出し (パーティションテーブル) ---- */
    boot_print_asm(row, "[2]Read LBA1 (PT)...");
    row += 160;

    boot_read_sector_asm(1, buf);
    show_dump(row, " PT:", buf, 16);
    row += 160;

    /* ---- Step 3: パーティション解析 ---- */
    for (i = 0; i < 16; i++) {
        u8 *ent = &buf[i * 32];
        u8 bootable = ent[0];
        u8 sys_type = ent[1];

        if (sys_type == 0x00 && bootable == 0x00) continue;

        /* 最初の有効エントリを表示 */
        {
            char info[50];
            char h1[3], h2[3];
            int p = 0;
            info[p++] = '['; info[p++] = '3'; info[p++] = ']';
            info[p++] = '#';
            if (i >= 10) { info[p++] = '0' + (i / 10); }
            info[p++] = '0' + (i % 10);
            info[p++] = ' '; info[p++] = 'B'; info[p++] = '=';
            hex8(bootable, h1);
            info[p++] = h1[0]; info[p++] = h1[1];
            info[p++] = ' '; info[p++] = 'T'; info[p++] = '=';
            hex8(sys_type, h2);
            info[p++] = h2[0]; info[p++] = h2[1];
            info[p] = '\0';
            boot_print_asm(row, info);
            row += 160;
        }

        /* 先頭エントリのバイト列もダンプ */
        show_dump(row, " ent:", ent, 16);
        row += 160;

        if (sys_type == PC98PT_SID_OS32) {
            PC98PartEntry pe;
            unsigned long st, ln;

            /* 標準配置で読む (drivers/pc98pt.c、票 TASK_HDD_INSTALL 段 1-4) */
            (void)pc98pt_get(buf, i, &pe);
            if (pc98pt_entry_range(&pe, param_heads, param_spt, 0,
                                   &st, &ln) == PC98PT_OK)
                part_lba = st;

            show_val(row, " CHS->LBA=", part_lba);
            row += 160;
            break;
        }
    }

    if (part_lba == 0) {
        /* 以前は LBA 1088 を仮定した。本物のローダ (boot_main.c) と同じく止まる */
        boot_print_asm(row, "[3]No OS32 partition in LBA 1!");
        return -7;
    }

    /* ---- Step 4: ext2 スーパーブロック読み出し ---- */
    /* ext2 SB はパーティション先頭から 1024B (= 2セクタ目) */
    {
        u32 sb_lba = part_lba + 2;
        u16 magic;

        show_val(row, "[4]SB LBA=", sb_lba);
        row += 160;

        boot_read_sector_asm(sb_lba, buf);

        /* マジック: SBの先頭から offset 56 (0x38) */
        show_dump(row, " SB@0:", buf, 16);
        row += 160;

        show_dump(row, " SB@48:", &buf[48], 16);
        row += 160;

        magic = (u16)buf[56] | ((u16)buf[57] << 8);
        show_val(row, "[4]magic=", (u32)magic);
        row += 160;

        if (magic == 0xEF53) {
            boot_print_asm(row, "[4]ext2 OK!");
        } else {
            boot_print_asm(row, "[4]NOT ext2! Check partition LBA");
        }
        row += 160;
    }

    boot_print_asm(row, "=== DEBUG HALT ===");

    /* カーネルにジャンプしない */
    return 0;
}
