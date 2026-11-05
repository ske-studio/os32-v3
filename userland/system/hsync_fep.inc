/* Optional v81 API: hsync must also run on older kernels during upgrades. */
#define HS_MIN_KAPI_FEP 81
static int g_fep_off;

static int hsync_fep_begin(void)
{
    int rc;
    if (!g_fep_off || g_dry_run) return 0;
    if (api->version < HS_MIN_KAPI_FEP) rc = OS32_ERR_NOSYS;
    else rc = api->ime_dict_suspend(1);
    if (rc) api->kprintf(ATTR_RED, "FAIL: FEP suspend (err=%d)\n", rc);
    return rc;
}

static void hsync_fep_end(void)
{
    int rc;
    if (!g_fep_off || g_dry_run) return;
    rc = api->ime_dict_suspend(0);
    if (rc) {
        api->kprintf(ATTR_RED, "FAIL: FEP reopen (err=%d)\n", rc);
        g_errors++;
    }
}
