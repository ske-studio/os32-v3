#include "os32_kapi_shared.h"
#include "port_service.h"

/* ======================================================================== */
/*  serial_diag (KAPI v66、票 TASK_SERIAL_HOSTFS §1-v2「ISR の計数」)        */
/*  受信の誤りの数を呼び手の SerialDiag へ写す。出力範囲の検査は生成ラッパ。 */
/* ======================================================================== */
int kapi_serial_diag(SerialDiag *out)
{
    if (!out) return OS32_ERR_INVAL;
    port_diag_get(&out->oe, &out->fe, &out->pe, &out->overflow);
    return 0;
}
