/* ======================================================================== */
/*  KAPI_HOST.H — Host Services KAPI (v51) の実体                            */
/*                                                                          */
/*  ABI の正典: docs/archive/network/TASK_N0.md §1a。生成される __cdecl ラッパ  */
/*  (`wrap_host_*`) がここの `kapi_host_*` を呼ぶ ([C3])。                    */
/* ======================================================================== */

#ifndef OS32_KAPI_HOST_H
#define OS32_KAPI_HOST_H

#include "types.h"

i32 kapi_host_open(const char *req, u32 len);
i32 kapi_host_status(i32 h, u32 *status, u32 *length);
i32 kapi_host_read(i32 h, void *buf, u32 cap);
i32 kapi_host_write(i32 h, const void *buf, u32 len);
i32 kapi_host_close(i32 h);

/* owner 回収 (exec/exec.c の exec_reclaim_owned から、
 * launch_owner_exit / con_sink_owner_exit と同じ位置)。公開 API を通さず
 * 指定 ID のハンドルを内部解放する (RELEASE も送る)。 */
void host_owner_exit(int id);

#endif /* OS32_KAPI_HOST_H */
