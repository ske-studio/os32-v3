#include "v86_module.h"
#include "v86_imports.h"
#include "v86_mem.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "os32_kapi_shared.h"
#include "kstring.h"
#include "kprintf.h"

extern const u32 v86_kexports[];
extern const struct module_symbol_contract v86_import_contracts[];
extern const struct module_symbol_contract v86_export_contracts[];
static struct module_handle v86_handle;
static V86ModuleApi candidate;
static int session_ref;

const struct kdebug_ops *v86_debug_ops(void) { return &kdebug.ops; }

/* A quarantined body still serves its outstanding session until the core
 * trusted landing has finished restoring PTE/ESP0 and detached disk state. */
static int body_callable(void)
{
    return session_ref || module_exports(v86_handle) != 0;
}

int v86_bios_attach_disk(const char *path)
{
    if (body_callable()) return candidate.v86_bios_attach_disk(path);
    return -1;
}

int v86_bios_attach_second(const char *path)
{
    if (body_callable()) return candidate.v86_bios_attach_second(path);
    return -1;
}

void v86_bios_detach_disk(void)
{
    if (body_callable()) candidate.v86_bios_detach_disk();
}

int v86_bios_has_disk(void)
{
    if (body_callable()) return candidate.v86_bios_has_disk();
    return 0;
}

u32 v86_bios_boot_daua(void)
{
    if (body_callable()) return candidate.v86_bios_boot_daua();
    return 0;
}

void v86_bios_setup(void)
{
    if (body_callable()) candidate.v86_bios_setup();
}

int v86_bios_is_hle(u32 vector)
{
    if (body_callable()) return candidate.v86_bios_is_hle(vector);
    return 0;
}

void v86_bios_dispatch(u32 *frame, u32 vector)
{
    if (body_callable()) candidate.v86_bios_dispatch(frame, vector);
}

u32 v86_bios_call_count(void)
{
    if (body_callable()) return candidate.v86_bios_call_count();
    return 0;
}

u32 v86_bios_last_vector(void)
{
    if (body_callable()) return candidate.v86_bios_last_vector();
    return 0;
}

void v86_io_apply_policy(void)
{
    if (body_callable()) candidate.v86_io_apply_policy();
}

void v86_io_reset_policy(void)
{
    if (body_callable()) candidate.v86_io_reset_policy();
}

u32 v86_io_in(u16 port, int size)
{
    if (body_callable()) return candidate.v86_io_in(port, size);
    return 0;
}

void v86_io_out(u16 port, int size, u32 value)
{
    if (body_callable()) candidate.v86_io_out(port, size, value);
}

u32 v86_io_trap_count(void)
{
    if (body_callable()) return candidate.v86_io_trap_count();
    return 0;
}

u16 v86_io_last_port(void)
{
    if (body_callable()) return candidate.v86_io_last_port();
    return 0;
}

void v86_pic_set_imr(int slave, u8 imr)
{
    if (body_callable()) candidate.v86_pic_set_imr(slave, imr);
}

int v86_pic_should_inject(u32 irq, u32 *out_vector)
{
    if (body_callable()) return candidate.v86_pic_should_inject(irq, out_vector);
    return 0;
}

int v86_kbd_push(u8 scancode)
{
    if (body_callable()) return candidate.v86_kbd_push(scancode);
    return 0;
}

int v86_kbd_pending(void)
{
    if (body_callable()) return candidate.v86_kbd_pending();
    return 0;
}

int v86_module_session_acquire(void)
{
    struct module_record *r = module_record_get(v86_handle);
    if (session_ref || !module_exports(v86_handle) || !r) return 0;
    session_ref = module_ref_acquire(v86_handle, r->owner, MODULE_REF_ACTIVE, 0);
    return session_ref;
}

void v86_module_session_release(void)
{
    if (!session_ref) return;
    module_ref_release(v86_handle, MODULE_REF_ACTIVE);
    session_ref = 0;
}

static int v86_start(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int (*init)(V86ModuleApi *) = (int (*)(V86ModuleApi *))(uptr)entry;
    int rc = init(&candidate);
    u32 entries[V86_MODULE_API_SLOTS];
    STATIC_ASSERT(sizeof(candidate) == sizeof(entries), v86_facade_size);
    kmemcpy(entries, &candidate, sizeof(entries));
    for (u32 i = 0; i < V86_MODULE_API_SLOTS; i++)
        if (entries[i] < r->text || entries[i] - r->text >= r->text_size)
            return MODULE_INIT_FAILED;
    return rc;
}

static int v86_quiesce(struct module_record *r)
{
    (void)r;
    return !session_ref;
}

int v86_module_load(void)
{
    if (v86_handle.generation) return MODULE_BUSY;
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path = SYS_V86_MOD, .name = "v86",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, V86_MODULE_ABI_GEN,
            bi->img_crc, V86_MODULE_OPS_VERSION, bi->img_valid,
            V86_IMPORT_COUNT, v86_import_contracts, v86_export_contracts,
            MODULE_ID_V86
        },
        .imports = v86_kexports,
        .lifecycle = {.start = v86_start, .quiesce = v86_quiesce}
    };
    int rc = module_load(&request, &v86_handle);
    if (!rc) rc = module_start(v86_handle);
    if (!rc && !module_exports(v86_handle)) rc = MODULE_INIT_FAILED;
    kprintf(0x07, "[v86] module rc=%d pair=%08x\n", rc, bi->img_crc);
    return rc;
}

int v86_module_abort(void) { return module_abort(v86_handle); }
