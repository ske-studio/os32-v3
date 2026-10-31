/* Dictionary selection and caller-safe user dictionary APIs. */

/* 辞書バリアント切り替え */
int ime_switch_dict(int variant)
{
    if (!services_public) return OS32_ERR_NOSYS;
    const char *path;
    if (variant == 0) {
        path = "/db/fep_s.db";
    } else if (variant == 1) {
        path = "/db/fep.db";
    } else if (variant == IME_DICT_LARGE) {
        path = "/db/fep_l.db";
    } else {
        return -1;
    }
    int rc = ime_dict_reopen(&g_ime.dict, path);
    g_ime.dict_loaded = g_ime.dict.db != NULL;
    return rc;
}

/* B2: retain no caller or SQLite pointers in the shared result staging. */
static IME_UserEntry ime_user_staging[IME_USER_LIST_MAX];
static int ime_user_staging_busy;
STATIC_ASSERT(IME_USER_STRING_CAP == VFS_MAX_PATH, ime_user_path_capacity);
STATIC_ASSERT(sizeof(IME_UserEntry) == 68, ime_user_entry_size);
STATIC_ASSERT(__builtin_offsetof(IME_UserEntry, kanji) == 32, ime_user_kanji_offset);
STATIC_ASSERT(__builtin_offsetof(IME_UserEntry, freq) == 64, ime_user_freq_offset);

int ime_user_list_facade(const char *yomi_prefix, void *out, int max)
{
    struct caller_access caller;
    char prefix[IME_USER_STRING_CAP];
    int n;
    if (!services_public) return OS32_ERR_NOSYS;
    if (!out || max <= 0 || max > IME_USER_LIST_MAX ||
        !caller_access_get(&caller)) return -1;
    if (yomi_prefix) {
        if (!copy_caller_cstr(&caller, yomi_prefix, prefix, sizeof(prefix))) return -1;
        yomi_prefix = prefix;
    }
    if (!check_caller_write_range(&caller, out, (u32)max * sizeof(IME_UserEntry))) {
        ring3_fault_kill();
        return -1;
    }
    if (!sqlite_engine_available(1)) return -1;
    u32 flags = irq_save();
    if (ime_user_staging_busy) { irq_restore(flags); return -1; }
    ime_user_staging_busy = 1;
    irq_restore(flags);
    kmemset(ime_user_staging, 0, sizeof(ime_user_staging));
    n = ime_user_list(&g_ime.dict, yomi_prefix, ime_user_staging, max);
    /* The last outer finalize has left the engine before the fresh B1 walk.
     * B1 preflights all actual bytes before writing any of them. */
    if (n >= 0 && !copy_to_caller(&caller, out, ime_user_staging,
                                 (u32)n * sizeof(IME_UserEntry))) n = -1;
    flags = irq_save();
    ime_user_staging_busy = 0;
    irq_restore(flags);
    return n;
}

int ime_user_delete_facade(const char *yomi, const char *kanji)
{
    struct caller_access caller;
    char y[IME_USER_STRING_CAP], k[IME_USER_STRING_CAP];
    if (!services_public) return OS32_ERR_NOSYS;
    if (!yomi || !caller_access_get(&caller) ||
        !copy_caller_cstr(&caller, yomi, y, sizeof(y)) || !y[0]) return -1;
    if (kanji) {
        if (!copy_caller_cstr(&caller, kanji, k, sizeof(k))) return -1;
        kanji = k;
    }
    if (!sqlite_engine_available(1)) return -1;
    return ime_user_delete(&g_ime.dict, y, kanji);
}

int ime_user_export_facade(const char *path)
{
    struct caller_access caller;
    char p[IME_USER_STRING_CAP];
    if (!services_public) return OS32_ERR_NOSYS;
    if (!path || !caller_access_get(&caller) ||
        !copy_caller_cstr(&caller, path, p, sizeof(p)) || !p[0] ||
        !sqlite_engine_available(1)) return -1;
    return ime_user_export(&g_ime.dict, p);
}

int ime_user_clear_facade(void)
{
    if (!services_public) return OS32_ERR_NOSYS;
    return ime_user_clear(&g_ime.dict);
}

