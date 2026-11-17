/* この像にはフックの提供者が無い (build/sources/hooks.mk)。宣言の側のヘッダを読む。 */
#define OS32_HOOK_KAPI_DB 0
#include "exec_kapi_db.h"

void exec_db_cleanup_owned(int owner) { (void)owner; }
