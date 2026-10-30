/* ------------------------------------------------------------------------ */
/*  キーボード 8251 のコマンド語 (POLICY_DEBUG §4-57)                        */
/*                                                                          */
/*  kbd_init が 0043h に書いた語が BIOS の定常値 0x16 で、DTR (bit1) が      */
/*  立っていること — 0 だと RTY# が LOW に張り付いてキーボードに再送を       */
/*  要求し続け、実機で打鍵が一切届かない (2026-09-23)。NP21/W は bit1 を     */
/*  見ないのでエミュレータの打鍵では気づけない。ここが唯一の見張り。         */
/*  ついでに kbd_diag (KAPI v62) が NULL を断ること。                        */
/* ------------------------------------------------------------------------ */
static void test_kbd_cmd(void)
{
    KbdDiag d;

    check(kbd_diag((KbdDiag *)0) == OS32_ERR_INVAL, "kbd_diag refuses NULL");
    kmemset(&d, 0, sizeof(d));
    check(kbd_diag(&d) == 0, "kbd_diag returns 0");
    /* マクロではなくリテラルと比べる — kbd_init もマクロを書くので、マクロ
     * どうしの比較は恒真で、定数を誤って変えても落ちない。 */
    check(d.cmd == 0x16, "kbd cmd word is 0x16 (BIOS)");
    check((d.cmd & KBD_CMD_DTR) != 0, "kbd cmd keeps DTR=1 (RTY# HIGH)");
}
