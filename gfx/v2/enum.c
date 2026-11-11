/* HAL v2 enumeration (HAL_V2_DESIGN §9-1). */
#include "gfx2_int.h"
#include "kstring.h"

int gfx2_adapter_count(void)
{
    int n = 0;
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++)
        if (gfx2_adapter_get(i)) n++;
    return n;
}

int gfx2_adapter_info(u32 index, GFX2_AdapterInfo *out)
{
    static const char *const names[] = { "", "pc98", "pegc", "cirrus", "geforce", "trident" };
    struct gfx2_adapter *a = gfx2_adapter_get(index);
    GFX2_AdapterInfo v = {0};
    if (!a || !out) return OS32_ERR_INVAL;
    if (a->v1->name) kstrncpy(v.name, a->v1->name, sizeof(v.name));
    if (a->kind <= GFX2_KIND_MAX) kstrncpy(v.driver, names[a->kind], sizeof(v.driver));
    v.vram_bytes = a->caps.vram_bytes; v.gart_bytes = a->caps.gart_bytes;
    v.heads = a->heads; v.connectors = a->caps.connectors;
    v.backend_kind = a->kind; v.flags = a->flags;
    *out = v;
    return 0;
}

int gfx2_head_info(u32 index, u32 head, GFX2_HeadInfo *out)
{
    struct gfx2_adapter *a = gfx2_adapter_get(index);
    GFX2_HeadInfo v;
    int rc;
    if (!a || !out || head >= a->heads) return OS32_ERR_INVAL;
    rc = a->be->enum_head(a->ctx, head, &v);
    if (rc) return rc;
    v.mode_id = a->head[head].mode_id; v.scanout_sid = a->head[head].scan_sid;
    if (a->head[head].conn_mask) v.connector_mask = a->head[head].conn_mask;
    *out = v;
    return 0;
}

int gfx2_connector_info(u32 index, u32 conn, GFX2_ConnectorInfo *out)
{
    struct gfx2_adapter *a = gfx2_adapter_get(index);
    GFX2_ConnectorInfo v;
    int rc;
    if (!a || !out || conn >= a->caps.connectors) return OS32_ERR_INVAL;
    rc = a->be->enum_conn(a->ctx, conn, &v);
    if (rc) return rc;
    *out = v;
    return 0;
}

int gfx2_connector_edid(u32 index, u32 conn, u32 block, u8 *out)
{
    struct gfx2_adapter *a = gfx2_adapter_get(index);
    GFX2_ConnectorInfo info;
    u8 data[128];
    int rc;
    if (!a || !out) return OS32_ERR_INVAL;
    rc = gfx2_connector_info(index, conn, &info);
    if (rc) return rc;
    if (block >= info.edid_blocks) return OS32_ERR_INVAL;
    rc = a->be->edid(a->ctx, conn, block, data);
    if (!rc) kmemcpy(out, data, sizeof(data));
    return rc;
}

int gfx2_mode_count(u32 index, u32 head, u32 fmt)
{
    struct gfx2_adapter *a = gfx2_adapter_get(index);
    GFX2_ModeInfo mode;
    int rc;
    u32 n = 0;
    if (!a || head >= a->heads || fmt < 1 || fmt > GFX2_FMT_UYVY) return OS32_ERR_INVAL;
    while ((rc = a->be->enum_mode(a->ctx, head, fmt, n, &mode)) == 0) n++;
    return rc == OS32_ERR_INVAL ? (int)n : rc;
}

int gfx2_mode_get(u32 index, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *out)
{
    struct gfx2_adapter *a = gfx2_adapter_get(index);
    GFX2_ModeInfo v;
    int count, rc;
    if (!a || !out) return OS32_ERR_INVAL;
    count = gfx2_mode_count(index, head, fmt);
    if (count < 0) return count;
    if (i >= (u32)count) return OS32_ERR_INVAL;
    rc = a->be->enum_mode(a->ctx, head, fmt, i, &v);
    if (!rc) *out = v;
    return rc;
}

int gfx2_v1_enum_head(const GfxBackend *v1, u32 head, GFX2_HeadInfo *out)
{
    const GfxMode *m;
    GFX2_HeadInfo v = {0};
    if (!v1 || !out || head) return OS32_ERR_INVAL;
    m = gfx_backend_mode(v1);
    if (m) { v.w = m->width; v.h = m->height; v.max_w = m->width; v.max_h = m->height;
        if (m->format >= 1 && m->format <= 3) v.fmt_scanout = 1UL << m->format; }
    v.attached = 1; v.connector_mask = 1;
    *out = v;
    return 0;
}

int gfx2_v1_enum_conn(const GfxBackend *v1, u32 conn, GFX2_ConnectorInfo *out)
{
    GFX2_ConnectorInfo v = {0};
    if (!v1 || !out || conn) return OS32_ERR_INVAL;
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++) {
        struct gfx2_adapter *a = gfx2_adapter_get(i);
        if (a && a->v1 == v1) {
            v.type = (a->kind == GFX2_KIND_PC98 || a->kind == GFX2_KIND_PEGC) ? GFX2_CONN_PC98_ANALOG : GFX2_CONN_VGA;
            break;
        }
    }
    v.possible_heads = v.clone_mask = 1; v.status = GFX2_CONN_ON;
    *out = v;
    return 0;
}

int gfx2_v1_enum_mode(const GfxBackend *v1, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *out)
{
    u32 n = 0;
    if (!v1 || !out || head || fmt < 1 || fmt > GFX2_FMT_UYVY) return OS32_ERR_INVAL;
    for (u32 k = 0; k < v1->mode_count; k++) {
        const GfxMode *m = &v1->modes[k];
        if (m->format != fmt) continue;
        if (n++ == i) {
            GFX2_ModeInfo v = {0};
            v.id = m->hw_mode ? m->hw_mode : k + 1;
            v.w = m->width; v.h = m->height; v.fmt = fmt;
            *out = v;
            return 0;
        }
    }
    return OS32_ERR_INVAL;
}
