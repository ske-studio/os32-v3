/* Non-equipped service. Never inspect guest frames or caller arguments. */
#include "legacy_guest.h"
#include "os32_kapi_slots.h"
#include "os32_kapi_shared.h"

int legacy_init(void) { return OS32_ERR_NOSYS; }
int legacy_active(void) { return 0; }
int legacy_pinned(void) { return 0; }
int legacy_session_open(void) { return 0; }
int legacy_closing(void) { return 0; }
int legacy_lowmap_busy(void) { return 0; }
void legacy_abort_begin(void) {}
void legacy_end(void) {}
void legacy_cleanup(void) {}
void legacy_request_exit(void) {}
int legacy_trap(void *frame) { (void)frame; return 0; }
int legacy_kbd_push(u8 scancode) { (void)scancode; return 0; }
int legacy_call_dispatch(u32 slot, void *fn, const void *args, u32 bytes,
                         u32 *result)
{
    (void)fn; (void)args; (void)bytes;
    switch (slot) {
    case KAPI_SLOT_V86_SELFTEST:
    case KAPI_SLOT_V86_DISKTEST:
    case KAPI_SLOT_V86_BOOT:
    case KAPI_SLOT_V86_BOOT2:
    case KAPI_SLOT_V86_GDC_CAPTURE:
        *result = OS32_ERR_NOSYS;
        return 1;
    default: return 0;
    }
}
int v86_smoke_test(void) { return OS32_ERR_NOSYS; }
int v86_disk_test(const char *path) { (void)path; return OS32_ERR_NOSYS; }
int v86_boot(const char *path) { (void)path; return OS32_ERR_NOSYS; }
int v86_boot2(const char *path, const char *second)
{ (void)path; (void)second; return OS32_ERR_NOSYS; }
int v86_gdc_capture(int mode, V86Gcap *out)
{ (void)mode; (void)out; return OS32_ERR_NOSYS; }
