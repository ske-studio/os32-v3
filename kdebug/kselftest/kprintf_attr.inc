/* ------------------------------------------------------------------------ */
/*  kprintf の属性変換 (lib/kprintf_attr.c)                                 */
/*                                                                          */
/*  PC-98 のテキスト属性は PC/AT (CGA) とビットの意味が別物で、             */
/*  呼び出し側の大半が渡している 07h をそのまま属性 VRAM へ書くと           */
/*  **画面に 1 文字も出ない** (bit0 = 表示 / bit1 = ブリンク /              */
/*  bit2 = リバース / bit5,6,7 = 青,赤,緑)。実機 PC-9821Ra266 の FD 起動が  */
/*  失敗したとき、[fdc] / [ide] の診断行が 1 行も読めなかったのがこれ       */
/*  (2026-09-22)。kprintf は KAPI 公開関数でもあるので、ここが壊れると      */
/*  カーネルもアプリも同時に黙る — プリミティブとして毎回踏む。            */
/*  ホスト側の試験は tools/tests/test_kprintf_attr.py。                     */
/* ------------------------------------------------------------------------ */
static void test_kprintf_attr(void)
{
    /* PC/AT 流の値 → PC-98 の色 + 表示。反転も点滅も付けない。 */
    check(kprintf_attr_to_pc98(0x07) == 0xE1, "kprintf attr 07 -> E1 (white)");
    check(kprintf_attr_to_pc98(0x0A) == 0x81, "kprintf attr 0A -> 81 (green)");
    check(kprintf_attr_to_pc98(0x0C) == 0x41, "kprintf attr 0C -> 41 (red)");
    check(kprintf_attr_to_pc98(0x0E) == 0xC1, "kprintf attr 0E -> C1 (yellow)");
    check(kprintf_attr_to_pc98(0x02) == 0x81, "kprintf attr 02 -> 81 (green)");
    check(kprintf_attr_to_pc98(0x04) == 0x41, "kprintf attr 04 -> 41 (red)");
    check(kprintf_attr_to_pc98(0x0B) == 0xA1, "kprintf attr 0B -> A1 (cyan)");
    /* 色を持たない値は黒 = 不可視になるので白へ倒す。 */
    check(kprintf_attr_to_pc98(0x00) == 0xE1, "kprintf attr 00 -> E1 (black->white)");
    /* 既に PC-98 流の値はそのまま。bit0 が落ちていれば立てるだけ。 */
    check(kprintf_attr_to_pc98(0xC1) == 0xC1, "kprintf attr C1 kept");
    check(kprintf_attr_to_pc98(0xE0) == 0xE1, "kprintf attr E0 -> E1 (visible)");
}
