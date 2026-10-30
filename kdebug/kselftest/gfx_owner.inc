/* ------------------------------------------------------------------------ */
/*  画面の所有者 (票 T8 D1 / D1a)                                            */
/*                                                                          */
/*  全画面 GFX の持ち主は 1 つで、gfx_init で移り、回収で WM へ戻る。ここが  */
/*  緩むと「プログラムが抜けたのに GUI が戻らない」「宣言していないプログラム */
/*  が黙って画面を壊す」の両方が起きる。                                     */
/* ------------------------------------------------------------------------ */
static void test_gfx_owner(void)
{
    u32 bad = appslot_gfx_owner_selftest();
    check((bad & (1u << 0)) == 0, "gfx owner moves on claim, returns on exit");
    check((bad & (1u << 1)) == 0, "gfx claim without OS32X_FLAG_GFX is refused");
    check((bad & (1u << 2)) == 0, "OS32X_FLAG_CUI_ONLY refused only from GUI");
}
