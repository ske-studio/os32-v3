/* ======================================================================== */
/*  kapi_sys.c — KernelAPI システム情報関数                                  */
/* ======================================================================== */

#include "os32_kapi_shared.h"
#include "exec.h"             /* CPL=3 ポインタ検証 */
#include "ver_info.h"
#include "kapi_sysinfo.h"

/* The generated __cdecl wrapper supplies the existing output-range check. */
int __cdecl kapi_sys_ver_info(void *out, u32 size)
{
    VerSnapshot s;

    if (!out || size != sizeof(OS32_VerInfo)) return OS32_ERR_INVAL;
    ver_collect(&s);
    kapi_sys_build_time_copy(s.build, sizeof(s.build));
    ver_format(&s, (OS32_VerInfo *)out);
    return 0;
}
