/* ======================================================================== */
/*  OS32X_HDR.C — OS32X ヘッダ v3 の配置照合 (票 TASK_KAPI_DATA_FIELDS)     */
/*                                                                          */
/*  見るのは 4 つ (方針 v2 の 4):                                            */
/*    1. 実際に読めた長さが v3 のヘッダ全体を含む                            */
/*       (短いファイルの後ろの古いバッファを kapi_data_off と読まない)       */
/*    2. version >= 3 (v2 以前は配置を示さない = 一度だけ全部断る)            */
/*    3. header_size >= OS32X_HDR_V3_SIZE                                    */
/*    4. kapi_data_off == カーネルの KAPI_DATA_FIELDS_OFF                    */
/* ======================================================================== */

#include "os32x_hdr.h"

int os32x_layout_check(const OS32Header *hdr, u32 read_len, u32 kernel_off)
{
    if (!hdr || read_len < (u32)OS32X_HDR_V3_SIZE) {
        /* v2 以前の小さなファイルでも、読めていない欄は信じない */
        if (hdr && read_len >= (u32)OS32X_HDR_V1_SIZE && hdr->version < 3u)
            return OS32X_LAYOUT_OLD;
        return OS32X_LAYOUT_SHORT;
    }
    if (hdr->version < 3u) return OS32X_LAYOUT_OLD;
    if (hdr->header_size < (u32)OS32X_HDR_V3_SIZE) return OS32X_LAYOUT_SHORT;
    if (hdr->kapi_data_off != kernel_off) return OS32X_LAYOUT_MISMATCH;
    return OS32X_LAYOUT_OK;
}

const char *os32x_layout_reason(int rc)
{
    switch (rc) {
    case OS32X_LAYOUT_OK:       return "ok";
    case OS32X_LAYOUT_SHORT:    return "short header";
    case OS32X_LAYOUT_OLD:      return "old header (no KAPI layout)";
    case OS32X_LAYOUT_MISMATCH: return "KAPI data layout mismatch";
    default:                    return "unknown";
    }
}
