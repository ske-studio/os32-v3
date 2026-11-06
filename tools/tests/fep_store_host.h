/* Dependencies of the real ime_dict.c in legacy DB/IME host fixtures.
 * Keep connection lifetime and optional user DB behavior identical to FEP.
 * The core probe lives in sqlite_engine.c with module loading/startup, which
 * these direct-SQLite fixtures cannot link. Their DB diagnostics/v50 and
 * legacy IME cases do not call the new store open API; supply the validated
 * legacy identity at that import boundary, without replacing SQL/VFS/FDs.
 * Core metadata validation is exercised by test_sqlite_module_proto.py.
 */
#ifndef OS32_TESTS_FEP_STORE_HOST_H
#define OS32_TESTS_FEP_STORE_HOST_H

/* LP64 fixtures replace kstring.h's u32-sized libc declarations. Retain its
 * decimal buffer width for the real user-store creator as well. */
#ifndef U32_DEC_BUFSIZE
#define U32_DEC_BUFSIZE (10 + 1)
#endif

#include "../../kernel/ime/ime_conn.c"
#include "../../kernel/ime/ime_user_store.c"

static u32 fep_host_probe_reserve_kb = FEP_DICT_RESERVE_KB;

int fep_meta_probe(const char *path, FepMeta *out)
{
    if (!path || !out) return OS32_ERR_INVAL;
    *out = (FepMeta){ .min_reader = FEP_DICT_READER_VERSION,
        .pos_version = FEP_DICT_POS_VERSION,
        .reserve_kb = fep_host_probe_reserve_kb,
        .cache_pages = FEP_DICT_CACHE_PAGES };
    kstrncpy(out->dict_id, FEP_DICT_ID, sizeof(out->dict_id));
    return 0;
}

#endif
