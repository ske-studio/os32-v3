/* Dictionary metadata display. Keep byte slices intact, including UTF-8 splits. */
static int ime_info_error(const char *key, int rc)
{
    const char *reason;
    switch (rc) {
    case OS32_ERR_NOSYS: reason = "FEP is not loaded or unavailable"; break;
    case OS32_ERR_AGAIN: reason = "no dictionary is loaded"; break;
    case OS32_ERR_NOTFOUND: reason = "metadata is not recorded"; break;
    case OS32_ERR_IO: reason = "metadata read or format error"; break;
    case OS32_ERR_STALE: reason = "dictionary connection is stale"; break;
    case OS32_ERR_BUSY: reason = "dictionary connection is isolated or busy"; break;
    case OS32_ERR_INVAL: reason = "invalid metadata query"; break;
    default: reason = "metadata query failed"; break;
    }
    printf("Error: ime info %s: %s (rc=%d).\n", key, reason, rc);
    return 1;
}

static int ime_info(KernelAPI *api)
{
    static const struct {
        int key;
        const char *name;
    } fields[] = {
        {IME_DICT_INFO_STATUS, "status"},
        {IME_DICT_INFO_FORMAT_VERSION, "format_version"},
        {IME_DICT_INFO_MIN_FEP_VERSION, "min_fep_version"},
        {IME_DICT_INFO_DICT_ID, "dict_id"},
        {IME_DICT_INFO_KIND, "kind"},
        {IME_DICT_INFO_LANGUAGE, "language"},
        {IME_DICT_INFO_CHARSET, "charset"},
        {IME_DICT_INFO_VARIANT, "variant"},
        {IME_DICT_INFO_SOURCE, "source"},
        {IME_DICT_INFO_SOURCE_VERSION, "source_version"},
        {IME_DICT_INFO_LICENSE, "license"},
        {IME_DICT_INFO_ATTRIBUTION, "attribution"},
        {IME_DICT_INFO_BUILD_TOOL, "build_tool"},
        {IME_DICT_INFO_BUILD_DATE, "build_date"},
        {IME_DICT_INFO_ENTRY_COUNT, "entry_count"},
        {IME_DICT_INFO_POS_TABLE_VERSION, "pos_table_version"},
        {IME_DICT_INFO_COST_SCALE, "cost_scale"},
        {IME_DICT_INFO_MEM_RESERVE_KB, "mem_reserve_kb"},
        {IME_DICT_INFO_CACHE_PAGES, "cache_pages"},
        {IME_DICT_INFO_CHECKSUM, "checksum"}
    };
    char chunk[IME_DICT_INFO_CAP];
    int legacy = 0;

    for (unsigned int i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        int offset = 0;
        int n = api->ime_dict_info(fields[i].key, offset, chunk, sizeof(chunk));
        if (n == OS32_ERR_NOTFOUND && legacy) {
            printf("%s: 旧形式に未収録\n", fields[i].name);
            continue;
        }
        if (n < 0) return ime_info_error(fields[i].name, n);
        if (fields[i].key == IME_DICT_INFO_STATUS) {
            legacy = strcmp(chunk, "legacy") == 0;
            if (strcmp(chunk, "unloaded") == 0)
                return ime_info_error(fields[i].name, OS32_ERR_AGAIN);
        }
        printf("%s: ", fields[i].name);
        while (n > 0) {
            /* Do not decode/re-encode or treat dictionary text as a format. */
            fwrite(chunk, 1, (size_t)n, stdout);
            offset += n;
            n = api->ime_dict_info(fields[i].key, offset, chunk, sizeof(chunk));
        }
        putchar('\n');
        if (n < 0) return ime_info_error(fields[i].name, n);
    }
    return 0;
}
