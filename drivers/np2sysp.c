/* ======================================================================== */
/*  NP2SYSP.C — NP21/Wエミュレータ通信ドライバ                               */
/*                                                                          */
/*  プロトコル概要:                                                          */
/*    OUT 0x7EF: 文字列コマンドを1バイトずつ送信                              */
/*              エミュレータ側がコマンドをバッファに蓄積し、                   */
/*              登録コマンドと一致した時点でハンドラを呼び出す                 */
/*    IN  0x7EF: レスポンス文字列を1バイトずつ受信 (\0=終端)                  */
/*    OUT 0x7ED: 32bit値を4バイト送信 (MSBファースト、シフトレジスタ)          */
/*    IN  0x7ED: 32bit値を4バイト受信 (LSBファースト、シフトレジスタ)          */
/* ======================================================================== */

#include "np2sysp.h"
#include "io.h"   /* inp/outp (pragma aux) */

/* ======================================================================== */
/*  基本I/O                                                                 */
/* ======================================================================== */

/* コマンド文字列をポート0x7EFに送信 */
void np2_send_cmd(const char *cmd)
{
    const char *p = cmd;
    while (*p) {
        outp(NP2PORT_STR, (unsigned char)*p);
        p++;
    }
}

/* レスポンス文字列をポート0x7EFから受信 */
int np2_recv_str(char *buf, int maxlen)
{
    int i = 0;
    u8 ch;

    /* maxlen <= 0 では 1 バイトも書かない (終端の 1 バイトが出力保護の
     * 「長さ 0 は見ない」規則の外に出ていた。実装レビュー 非 blocker)。 */
    if (!buf || maxlen <= 0) return 0;

    while (i < maxlen - 1) {
        ch = (u8)inp(NP2PORT_STR);
        if (ch == 0) break;  /* \0終端 */
        buf[i] = (char)ch;
        i++;
    }
    buf[i] = '\0';
    return i;
}

/* 32bit値をポート0x7EDに送信 (4バイト、MSBファースト) */
void np2_send_val(u32 val)
{
    /* np2sysp_o7ed: outval = (dat << 24) | (outval >> 8) */
    /* 4バイト送ると outval に val が設定される */
    outp(NP2PORT_VAL, (u8)(val & 0xFF));
    outp(NP2PORT_VAL, (u8)((val >> 8) & 0xFF));
    outp(NP2PORT_VAL, (u8)((val >> 16) & 0xFF));
    outp(NP2PORT_VAL, (u8)((val >> 24) & 0xFF));
}

/* 32bit値をポート0x7EDから受信 */
u32 np2_recv_val(void)
{
    u32 val = 0;
    /* np2sysp_i7ed: ret = inpval & 0xff; inpval = (ret<<24) | (inpval>>8) */
    val |= (u32)inp(NP2PORT_VAL);
    val |= (u32)inp(NP2PORT_VAL) << 8;
    val |= (u32)inp(NP2PORT_VAL) << 16;
    val |= (u32)inp(NP2PORT_VAL) << 24;
    return val;
}

/* ======================================================================== */
/*  NP21/W検出                                                              */
/*                                                                          */
/*  "NP2"をポート0x7EFに出力すると、エミュレータ側が"NP2"を返す              */
/*  (np2spcmdテーブルの先頭エントリ)                                         */
/* ======================================================================== */
int np2_detect(void)
{
    char buf[8];

    np2_send_cmd("NP2");
    np2_recv_str(buf, sizeof(buf));

    /* "NP2"が返ってきたらNP21/W上で動作中 */
    return (buf[0] == 'N' && buf[1] == 'P' && buf[2] == '2');
}

/* ======================================================================== */
/*  情報取得ヘルパー                                                        */
/* ======================================================================== */

/* バージョン取得 */
int np2_get_version(char *buf, int maxlen)
{
    np2_send_cmd("ver");
    return np2_recv_str(buf, maxlen);
}

/* CPU種類取得 */
int np2_get_cpu(char *buf, int maxlen)
{
    np2_send_cmd("cpu");
    return np2_recv_str(buf, maxlen);
}

/* クロック取得 */
int np2_get_clock(char *buf, int maxlen)
{
    np2_send_cmd("clock");
    return np2_recv_str(buf, maxlen);
}

/* hostdrv対応確認 */
int np2_check_hostdrv(char *buf, int maxlen)
{
    np2_send_cmd("check_hostdrv");
    return np2_recv_str(buf, maxlen);
}
