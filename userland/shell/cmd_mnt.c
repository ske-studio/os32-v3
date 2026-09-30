#include "cmd_fs_shared.h"
#include "shell.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>

static int cmd_mount(int argc, char **argv)
{
    int ret;
    /* 余分な引数も断る (票 TASK_SERIAL_HOSTFS §1-v3)。黙って捨てると
     * `mount /host COM1 serialfs extra` が意図と違う形で通る。 */
    if (argc != 4) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    /* SerialFS は `sfs run` のセッションの中だけで付く。単独のマウントは
     * FS の口が断るので、理由を先に言う。 */
    if (strcmp(argv[3], "serialfs") == 0) {
        g_api->kprintf(ATTR_RED, "%s",
                       "mount: serialfs is mounted only by 'sfs run' "
                       "(from rshell_serial.py --serve-host)\n");
        return SH_STATUS_USAGE;
    }
    ret = g_api->sys_mount(argv[1], argv[2], argv[3]);
    if (ret != 0) {
        g_api->kprintf(ATTR_RED, "mount: failed %d\n", ret);
        return SH_STATUS_ERROR;
    }
    return 0;
}

static int cmd_umount(int argc, char **argv)
{
    if (argc < 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    /* sys_umount は void なので、未マウントは先に弾いて知らせる */
    if (!g_api->sys_is_mounted(argv[1])) {
        g_api->kprintf(ATTR_RED, "umount: %s: not mounted\n", argv[1]);
        return SH_STATUS_ERROR;
    }
    g_api->sys_umount(argv[1]);
    return 0;
}

static int cmd_sync(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_api->vfs_sync();
    return 0;
}

static int cmd_exec(int argc, char **argv)
{
    char cmdline[EXEC_CMDLINE_MAX];
    int i, pos;
    /* 票 §2-3: 以前は **戻り値で印字を分けて** いたので、子の `exit(-3)` が
     * 「file not found」になっていた。種別で分岐する。 */
    int kind = EXEC_KIND_NONE;
    int code = 0;

    if (argc < 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }

    /* T3: 溢れた引数を落として起動すると、意図と違う引数でプログラムが走る。
     * 結合する前に長さを数えて断る (区切りの空白も数に入れる)。`exec` は
     * クォートを付け直さないので、数えるのは本体と空白だけ。 */
    {
        int need = 0;
        for (i = 1; i < argc; i++) {
            const char *s = argv[i];
            if (i > 1) need++;                 /* 区切りの空白 */
            while (*s++) need++;
        }
        if (need > EXEC_CMDLINE_MAX - 1) {
            sh_refuse("exec: command line", EXEC_CMDLINE_MAX - 1);
            return SH_STATUS_USAGE;
        }
    }

    /* argv[1]以降を結合してcmdline全体を構築 */
    pos = 0;
    for (i = 1; i < argc; i++) {
        const char *s = argv[i];
        if (i > 1 && pos < (int)sizeof(cmdline) - 1) cmdline[pos++] = ' ';
        while (*s && pos < (int)sizeof(cmdline) - 1) {
            cmdline[pos++] = *s++;
        }
    }
    cmdline[pos] = '\0';

    (void)sh_exec_result(cmdline, &kind, &code);
    /* 断った (パイプ / リダイレクト / 長さ) 場合は印が立っていて、種別は
     * NONE のまま — もう赤字は出ているので何も言い足さない。 */
    if (kind == EXEC_KIND_NONE) return SH_STATUS_USAGE;

    switch (kind) {
    case EXEC_KIND_EXITED:
        g_api->kprintf(ATTR_GREEN, "exec: exited with %d\n", code);
        break;
    case EXEC_KIND_FAULT:
        g_api->kprintf(ATTR_RED, "%s", "exec: crashed\n");
        break;
    case EXEC_KIND_ABORTED:
        g_api->kprintf(ATTR_RED, "%s", "exec: aborted\n");
        break;
    case EXEC_KIND_NOT_FOUND:
        g_api->kprintf(ATTR_RED, "%s", "exec: file not found\n");
        break;
    case EXEC_KIND_INVALID:
        g_api->kprintf(ATTR_RED, "%s", "exec: invalid executable\n");
        break;
    default:
        g_api->kprintf(ATTR_RED, "%s", "exec: general error\n");
        break;
    }
    return sh_status_from_kind(kind, code);
}

/* ======================================================================== */
/*  losetup — ループバックデバイス管理 (Linux losetup 準拠)                 */
/*                                                                          */
/*  losetup <path> <slot>    イメージをアタッチ (フォーマット自動判別)       */
/*  losetup -d <slot>        デタッチ                                       */
/*  losetup -l [slot]        ステータス表示                                 */
/* ======================================================================== */
static int cmd_losetup(int argc, char **argv)
{
    int slot, ret, in_use, bps;
    u32 total;

    if (argc < 2) {
        /* 引数なし: 全スロット表示 */
        for (slot = 0; slot < 4; slot++) {
            in_use = g_api->loop_status(slot, &total, &bps);
            if (in_use) {
                g_api->kprintf(ATTR_GREEN, "lo%d: total_lba=%u bps=%d\n",
                               slot, total, bps);
            } else {
                g_api->kprintf(ATTR_WHITE, "lo%d: (empty)\n", slot);
            }
        }
        return 0;
    }

    /* -d: デタッチ */
    if (argv[1][0] == '-' && argv[1][1] == 'd') {
        if (argc < 3) {
            g_api->kprintf(ATTR_WHITE, "Usage: losetup -d <slot(0-3)>\n");
            return SH_STATUS_USAGE;
        }
        slot = atoi(argv[2]);
        g_api->loop_detach(slot);
        g_api->kprintf(ATTR_GREEN, "losetup: lo%d detached\n", slot);
        return 0;
    }

    /* -l: ステータス表示 */
    if (argv[1][0] == '-' && argv[1][1] == 'l') {
        if (argc >= 3) {
            slot   = atoi(argv[2]);
            in_use = g_api->loop_status(slot, &total, &bps);
            if (in_use) {
                g_api->kprintf(ATTR_GREEN, "lo%d: total_lba=%u bps=%d\n",
                               slot, total, bps);
            } else {
                g_api->kprintf(ATTR_WHITE, "lo%d: (empty)\n", slot);
            }
        } else {
            for (slot = 0; slot < 4; slot++) {
                in_use = g_api->loop_status(slot, &total, &bps);
                if (in_use) {
                    g_api->kprintf(ATTR_GREEN, "lo%d: total_lba=%u bps=%d\n",
                                   slot, total, bps);
                } else {
                    g_api->kprintf(ATTR_WHITE, "lo%d: (empty)\n", slot);
                }
            }
        }
        return 0;
    }

    /* アタッチ: losetup <path> <slot> */
    if (argc < 3) {
        g_api->kprintf(ATTR_WHITE,
            "Usage: losetup <image_path> <slot(0-3)>\n"
            "       losetup -d <slot>          detach\n"
            "       losetup -l [slot]          status\n");
        return SH_STATUS_USAGE;
    }
    slot = atoi(argv[2]);
    ret  = g_api->loop_attach(argv[1], slot);
    switch (ret) {
    case 0:
        g_api->kprintf(ATTR_GREEN, "losetup: lo%d attached: %s\n",
                       slot, argv[1]);
        break;
    case -1:
        g_api->kprintf(ATTR_RED, "losetup: invalid path or slot\n");
        return SH_STATUS_ERROR;
    case -2:
        g_api->kprintf(ATTR_RED, "losetup: unsupported format\n");
        return SH_STATUS_ERROR;
    case -3:
        g_api->kprintf(ATTR_RED, "losetup: slot %d already in use\n", slot);
        return SH_STATUS_ERROR;
    case -4:
        g_api->kprintf(ATTR_RED, "losetup: I/O error\n");
        return SH_STATUS_ERROR;
    default:
        g_api->kprintf(ATTR_RED, "losetup: error %d\n", ret);
        return SH_STATUS_ERROR;
    }
    return 0;
}

/* ======================================================================== */
/*  dd — ブロックデバイスのセクタ読み出し                                  */
/*  使い方: dd <devname> lba=<N> count=<M> [file=<vfs_path>] [noerr]       */
/*           noerr: 読み取りエラーをゼロ埋めしてスキップ                    */
/* ======================================================================== */
static int cmd_dd(int argc, char **argv)
{
    const char *dev_name;
    int lba, count, i;
    char *out_path;
    void *buf;
    u32  bps;
    int  dummy_bps;
    u32  dummy_total;
    int  noerr, err_count;

    /* I3 (non-blocker): 下の「lba+count が総数を超えるか」の判定で読むので
     * ループ経路以外でも必ず初期化しておく (以前は未初期化のスタック値)。 */
    dummy_total = 0;
    dummy_bps = 0;

    if (argc < 4) {
        g_api->kprintf(ATTR_WHITE,
            "Usage: dd <dev> lba=<N> count=<M> [file=<path>] [noerr]\n"
            "  noerr: skip read errors (zero-fill)\n");
        return SH_STATUS_USAGE;
    }

    dev_name  = argv[1];
    lba       = 0;
    count     = 1;
    out_path  = (char *)0;
    noerr     = 0;
    err_count = 0;

    for (i = 2; i < argc; i++) {
        if (strncmp(argv[i], "lba=", 4) == 0) {
            lba = atoi(argv[i] + 4);
        } else if (strncmp(argv[i], "count=", 6) == 0) {
            count = atoi(argv[i] + 6);
        } else if (strncmp(argv[i], "file=", 5) == 0) {
            out_path = argv[i] + 5;
        } else if (strcmp(argv[i], "noerr") == 0) {
            noerr = 1;
        }
    }

    /* デバイスの bps 取得 */
    {
        int slot = -1;
        if (dev_name[0] == 'l' && dev_name[1] == 'o' &&
            dev_name[2] >= '0' && dev_name[2] <= '3' && dev_name[3] == '\0') {
            slot = dev_name[2] - '0';
        }
        if (slot >= 0) {
            if (!g_api->loop_status(slot, &dummy_total, &dummy_bps)) {
                g_api->kprintf(ATTR_RED, "dd: lo%d not attached\n", slot);
                return SH_STATUS_ERROR;
            }
            bps = (u32)dummy_bps;
        } else {
            /* I3 / I-5: dev_blk_read が 1 セクタで書く長さはデバイス種別ごとに
             * 違う (cd = ATAPI 2048B、hd = IDE の物理 512B、fd = FDC 1024B)。
             * `dev_get_info` はセクタ長を返さないので**名前の先頭で見分ける**
             * — KAPI がセクタ長を返すようになったらそちらへ寄せること。
             * 足りない確保のまま読むと dev_blk_read がヒープを踏む。 */
            if (dev_name[0] == 'c' && dev_name[1] == 'd')
                bps = SYS_CDROM_SECTOR_SIZE;
            else if (dev_name[0] == 'h' && dev_name[1] == 'd')
                bps = SYS_HDD_SECTOR_SIZE;
            else
                bps = SYS_BLOCK_SECTOR_SIZE;
        }
    }

    if (count <= 0) {
        g_api->kprintf(ATTR_RED, "dd: count must be >= 1\n");
        return SH_STATUS_USAGE;
    }
    if (dummy_total > 0 && (u32)(lba + count) > dummy_total) {
        g_api->kprintf(ATTR_RED, "dd: lba+count exceeds total (%u)\n",
                       dummy_total);
        return SH_STATUS_USAGE;
    }

    buf = g_api->mem_alloc(bps);
    if (!buf) {
        g_api->kprintf(ATTR_RED, "dd: alloc failed\n");
        return SH_STATUS_ERROR;
    }

    if (out_path) {
        u32 total_bytes = 0;
        int fd = g_api->sys_open(out_path, 0x0301);
        if (fd < 0) {
            g_api->kprintf(ATTR_RED, "dd: cannot create %s (err=%d)\n",
                           out_path, fd);
            g_api->mem_free(buf);
            return SH_STATUS_ERROR;
        }
        for (i = 0; i < count; i++) {
            if (g_api->dev_blk_read(dev_name, (u32)(lba + i), 1, buf) != 0) {
                if (!noerr) {
                    g_api->kprintf(ATTR_RED, "dd: read error at lba=%d\n",
                                   lba + i);
                    g_api->sys_close(fd);
                    g_api->mem_free(buf);
                    return SH_STATUS_ERROR;
                }
                { u8 *z = (u8 *)buf; u32 k; for (k = 0; k < bps; k++) z[k] = 0; }
                err_count++;
            }
            /* I-6: 短い書き込み / 失敗を見逃さない。以前は要求長を無条件に
             * 足していたので、書けていなくても `wrote N bytes` と出た。 */
            {
                int w = g_api->sys_write(fd, buf, bps);
                if (w < 0 || (u32)w != bps) {
                    g_api->kprintf(ATTR_RED,
                        "dd: write failed at sector %d (wrote %u bytes)\n",
                        lba + i, total_bytes);
                    g_api->sys_close(fd);
                    g_api->mem_free(buf);
                    return SH_STATUS_ERROR;
                }
                total_bytes += (u32)w;
            }
        }
        g_api->sys_close(fd);
        if (err_count > 0)
            g_api->kprintf(ATTR_YELLOW,
                "dd: wrote %u bytes -> %s (%d errors skipped)\n",
                total_bytes, out_path, err_count);
        else
            g_api->kprintf(ATTR_GREEN, "dd: wrote %u bytes -> %s\n",
                           total_bytes, out_path);
    } else {
        u32 global_off = 0;
        for (i = 0; i < count; i++) {
            u8 *p;
            u32 off;
            if (g_api->dev_blk_read(dev_name, (u32)(lba + i), 1, buf) != 0) {
                if (!noerr) {
                    g_api->kprintf(ATTR_RED, "dd: read error at lba=%d\n",
                                   lba + i);
                    g_api->mem_free(buf);
                    return SH_STATUS_ERROR;
                }
                { u8 *z = (u8 *)buf; u32 k; for (k = 0; k < bps; k++) z[k] = 0; }
                err_count++;
            }
            p = (u8 *)buf;
            for (off = 0; off < bps; off += 16) {
                u32 end = off + 16;
                u32 k;
                if (end > bps) end = bps;
                g_api->kprintf(ATTR_WHITE, "%05X: ", global_off + off);
                for (k = off; k < end; k++)
                    g_api->kprintf(ATTR_WHITE, "%02X ", (unsigned)p[k]);
                for (k = end; k < off + 16; k++)
                    g_api->kprintf(ATTR_WHITE, "   ");
                g_api->kprintf(ATTR_CYAN, " ");
                for (k = off; k < end; k++) {
                    u8 c = p[k];
                    g_api->kprintf(ATTR_CYAN, "%c",
                                   (c >= 0x20 && c < 0x7F) ? (char)c : '.');
                }
                g_api->kprintf(ATTR_WHITE, "\n");
            }
            global_off += bps;
        }
        if (err_count > 0)
            g_api->kprintf(ATTR_YELLOW,
                "dd: %u bytes from %s lba=%d count=%d (%d errors)\n",
                global_off, dev_name, lba, count, err_count);
        else
            g_api->kprintf(ATTR_GREEN,
                "dd: %u bytes from %s lba=%d count=%d\n",
                global_off, dev_name, lba, count);
    }

    g_api->mem_free(buf);
    return 0;
}


static const ShellCmd mnt_cmds[] = {
    { "mount",     cmd_mount,     "PREFIX DEV FS",                    "Mount a filesystem" },
    { "umount",    cmd_umount,    "PREFIX",                           "Unmount a filesystem" },
    { "sync",      cmd_sync,      "",                                 "Sync file buffers to disk" },
    { "exec",      cmd_exec,      "FILE.BIN",                         "Execute a binary program" },
    { "losetup",   cmd_losetup,   "<path> <slot> | -d <slot> | -l",   "Loop device management" },
    { "dd",        cmd_dd,        "<dev> lba=N count=M [file=PATH]",  "Block device sector read" },
    { (const char *)0, 0, 0, 0 }
};
void shell_cmd_mnt_init(void) { shell_register_cmds(mnt_cmds); }
