/* The real atapi_srst must invalidate both HDDs before asserting bank1 SRST. */
static void t_multiple_reset(void)
{
    unsigned failed;
    for (failed = 0; failed < 2; failed++) {
        g_np2cfg.strict = 1;
        setup_np2();
        atapi_select_bank(1);
        multiple_notify_count = 0;
        multiple_notify_mask = 15;
        multiple_notify_armed = 1;
        if (failed) N.d[0].srst_busy_us = 2ul * ATAPI_SRST_TIMEOUT_US;
        CHECK(atapi_srst() == (failed ? ATAPI_ERR_TIMEOUT : ATAPI_OK));
        CHECK(multiple_notify_count == 1 && multiple_notify_bank == 1);
        CHECK(multiple_notify_mask == 3);
        multiple_notify_armed = 0;
    }
    /* ATAPI DEVICE RESET touches one packet device, not the HDD bank state. */
    setup_np2();
    atapi_select_bank(1);
    multiple_notify_count = 0;
    multiple_notify_mask = 15;
    CHECK(atapi_select_device() == ATAPI_OK);
    CHECK(atapi_recover() == ATAPI_OK);
    CHECK(N.n_devreset == 1 && N.n_srst == 0);
    CHECK(multiple_notify_count == 0 && multiple_notify_mask == 15);
}
