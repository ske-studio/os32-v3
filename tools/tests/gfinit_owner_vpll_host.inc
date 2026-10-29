static void vpll_budget_test(void)
{
    static struct gfinit_plan budget_plan;
    struct gfinit_owner g = {.compiled=&budget_plan}; struct recover r = {0};
    struct recover_transport t = {0};
    struct recover_plan p = {.node_count=100, .arg_count=200};
    u32 rows=128+100*12+200*4;
#if GF_VPLL_PROBE
    CHECK(RECOVER_VPLL_ROWS==20U);
    rows += 20U;
#endif
    u32 bytes=rows*(RECOVER_ROW_MAX+1U);
    u32 seconds=(bytes*RECOVER_UART_FRAME_BITS+serial.actual-1)/serial.actual;
    CHECK(gf_budget(&g,&r,&t,REC_BUDGET_GF,&p)==0);
    CHECK(r.log_limit==(seconds*2+RECOVER_BUDGET_MARGIN_S)*PIT_HZ);
    CHECK(t.max_polls==bytes*2+1024);
    p.node_count=~0U; CHECK(gf_budget(&g,&r,&t,REC_BUDGET_GF,&p)!=0);
    output("VPLL budget rows and overflow PASS\n");
}
