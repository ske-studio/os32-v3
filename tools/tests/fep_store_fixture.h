/* Shared DICT_META unit-test substrate. Real SQLite/VFS integration stays in
 * ime_dict_host.c and sqlite_module_host.c. This script does not parse SQL.
 * Unit wrappers adapt the typed SQLite/outer APIs to these ordered calls.
 * Byte pointers are borrowed until the next step/reset/finalize; wrappers
 * must poison/invalidate their row storage there when testing lifetimes.
 */
#ifndef OS32_FEP_STORE_FIXTURE_H
#define OS32_FEP_STORE_FIXTURE_H
#include "kernel/ime/ime_store.h"
#include "lib/sqlite3/sqlite3.h"

enum FepTestOp {
    FEP_TEST_HEADER_READ, FEP_TEST_ACQUIRE, FEP_TEST_OPEN,
    FEP_TEST_PREPARE, FEP_TEST_BIND, FEP_TEST_STEP,
    FEP_TEST_COLUMN, FEP_TEST_RESET, FEP_TEST_FINALIZE,
    FEP_TEST_BEGIN_CLOSE, FEP_TEST_CLOSE, FEP_TEST_FINISH,
    FEP_TEST_STALE
};
typedef struct {
    enum FepTestOp op;
    int rc; /* SQLite APIs use SQLITE_*, VFS APIs use OS32_ERR_*. */
    const void *data; /* Caller-owned row/blob/handle, never auto-populated. */
    u32 bytes;
} FepTestReply;
typedef struct {
    FepStore store;
    /* Start exactly like BSS: no loaded meta, enabled user, prepared SQL,
     * saved boot result or live connection. Settings default to absent. */
    int reserve_present, reserve_kb, settings_rc;
    const char *user_path; /* NULL = absent; borrowed by fixture. */
    const FepTestReply *script;
    u32 count, next, resident_live;
    int mismatch, resident_isolated;
} FepTestFixture;

void fep_test_reset(FepTestFixture *f);
/* Read explicit setting/presence independently: zero is not "absent".
 * Returns settings_rc without modifying outputs when settings read fails. */
int fep_test_settings(const FepTestFixture *f, int *present, int *kb,
                      const char **user_path);
void fep_test_script(FepTestFixture *f, const FepTestReply *replies, u32 count);
const FepTestReply *fep_test_next(FepTestFixture *f, enum FepTestOp op);
int fep_test_done(const FepTestFixture *f);
/* Wrappers call these on successful acquire/finish, not open/close. Uses the
 * product's CURRENT limit (2 at stage A; N1's 3 belongs to C2/registration).
 * Quarantined groups are not released. This is not a SQL implementation. */
int fep_test_resident_take(FepTestFixture *f);
void fep_test_resident_finish(FepTestFixture *f, int isolated);
#endif
