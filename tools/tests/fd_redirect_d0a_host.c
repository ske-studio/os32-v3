/* Real fd_redirect + saved-AS copy. MMU/slot/IRQ boundaries are instrumented;
 * mmap changes the active VA backing, while P2V aliases stay stable. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "appslot.h"
#define __KSTRING_H
void *kmemcpy(void *dst, const void *src, u32 n) { return memcpy(dst, src, n); }
#define IO_H
static unsigned int irq_enabled = 1, saves, restores;
static unsigned int irq_save(void) { unsigned int f = irq_enabled; irq_enabled = 0; saves++; return f; }
static void irq_restore(unsigned int f) { assert(!irq_enabled); irq_enabled = f; restores++; }
#include "../../fs/fd_redirect.c"
#include "../../exec/redir_access.c"

#define BYTES (PAGE_SIZE * 2)
static AppSlot slots[APP_SLOT_COUNT];
static struct addrspace spaces[APP_SLOT_COUNT];
static u8 *current_va, *aliases[APP_SLOT_COUNT];
static int current_id = 2, user_call = 1, denied_page = -1, read_denied;
static u32 walk_count;
static const char payload[] = "d0a child stdout\n";
AppSlot *appslot_get(int id) { assert(!irq_enabled); return id > 0 && id < APP_SLOT_COUNT && slots[id].state ? &slots[id] : NULL; }
int appslot_cur(void) { return current_id; }
int ring3_call_from_user(void) { return user_call; }
u32 paging_kernel_pd_phys(void) { return 0x1000; }
u32 paging_current_cr3(void) { return (u32)current_id * PAGE_SIZE; }
static int walk(u32 pd, u32 va, u32 *pa, int write)
{
    assert(!irq_enabled);
    walk_count++;
    if (va < (u32)(uptr)current_va || va - (u32)(uptr)current_va >= BYTES) return 1;
    u32 off = va - (u32)(uptr)current_va;
    if ((int)(off / PAGE_SIZE) == denied_page && (write || read_denied)) return 1;
    for (int id = 2; id <= 3; id++) if (pd == spaces[id].pd_phys) {
        *pa = (u32)(uptr)(aliases[id] + off);
        return 0;
    }
    return 1;
}
int as_va_to_pa(u32 pd, u32 va, u32 *pa) { return walk(pd, va, pa, 1); }
int as_va_to_pa_read(u32 pd, u32 va, u32 *pa) { return walk(pd, va, pa, 0); }
int vfs_open(const char *p, int m) { (void)p; (void)m; abort(); }
void vfs_close(int fd) { (void)fd; abort(); }
int vfs_seek(int fd, int o, int w) { (void)fd; (void)o; (void)w; abort(); }
int vfs_read_fd(int fd, void *b, u32 n) { (void)fd; (void)b; (void)n; abort(); }
int vfs_write_fd(int fd, const void *b, u32 n) { (void)fd; (void)b; (void)n; abort(); }

static u8 *map_backing(int fd, void *va)
{
    void *p = mmap(va, BYTES, PROT_READ | PROT_WRITE,
                   MAP_SHARED | (va ? MAP_FIXED : MAP_32BIT), fd, 0);
    assert(p != MAP_FAILED && (uptr)p <= 0xffffffffUL);
    return p;
}
static void unchanged(u8 *p, u32 n, u8 value)
{ for (u32 i = 0; i < n; i++) assert(p[i] == value); }
static void refuse_write(void)
{
    u32 old = redir_table[1].buf_len;
    u32 refused = redir_refuse_count;
    assert(fd_redirect_write(1, payload, 17) == -1);
    assert(redir_table[1].buf_len == old);
    assert(redir_refuse_count == refused + 1);
    unchanged(aliases[2], BYTES, 0xA5);
    unchanged(aliases[3], BYTES, 0x5A);
}
int main(void)
{
    int fds[4] = {0};
    for (int id = 2; id <= 3; id++) {
        fds[id] = memfd_create("d0b", 0);
        assert(fds[id] >= 0 && ftruncate(fds[id], BYTES) == 0);
        aliases[id] = map_backing(fds[id], NULL);
        spaces[id].owner = 10 + id; spaces[id].generation = 20 + id;
        spaces[id].pd_phys = (u32)id * PAGE_SIZE;
        slots[id].as = &spaces[id]; slots[id].cpl3 = 1;
        slots[id].state = APP_STATE_RUNNING;
    }
    current_va = map_backing(fds[2], NULL);
    fd_redirect_init(); res_owner_set(2);
    assert(fd_redirect_to_buffer(1, current_va, 64, 0) == 0);
    assert(fd_redirect_write(1, payload, 17) == 17);
    assert(!memcmp(aliases[2], payload, 17));
    puts("d0a: same_as=OK");
    memset(aliases[2], 0xA5, BYTES); memset(aliases[3], 0x5A, BYTES);
    assert(fd_redirect_to_buffer(1, current_va, BYTES, 0) == 0);
    /* Failed registrations preserve an already installed redirect. */
    res_owner_set(3);
    assert(fd_redirect_to_buffer(1, current_va, 16, 0) == -1);
    assert(redir_table[1].access.app_id == 2);
    res_owner_set(2);
    u32 saved_pd = spaces[2].pd_phys;
    spaces[2].pd_phys = spaces[3].pd_phys;
    /* Current CR3 is independently fixed, not derived from the changed AS. */
    assert(fd_redirect_to_buffer(1, current_va, 16, 0) == -1);
    assert(redir_table[1].access.pd_phys == saved_pd);
    spaces[2].pd_phys = saved_pd;
    current_id = 3; res_owner_set(3); map_backing(fds[3], current_va);
    assert(fd_redirect_write(1, payload, 17) == 17);
    assert(!memcmp(aliases[2], payload, 17)); unchanged(aliases[2] + 17, BYTES - 17, 0xA5);
    unchanged(aliases[3], BYTES, 0x5A);
    fd_redirect_reset_owned(3);
    assert(fd_redirect_get_buf_len(1) == 17);
    puts("d0a: parent_buffer=OK\nd0a: child_value=OK");
    u8 out[BYTES]; memset(out, 0xCC, sizeof(out));
    assert(fd_redirect_read(1, out, 17) == 17 && !memcmp(out, payload, 17));

    /* Value snapshots survive nested save/restore and park transfer, and the
     * inherited table still belongs to the parent on a child's exit. */
    FdRedirectState parent, nested, parked;
    fd_redirect_save(&parent);
    assert(!fd_is_redirected(1));
    assert(fd_redirect_to_buffer(1, current_va, BYTES, 0) == 0);
    fd_redirect_save(&nested); fd_redirect_restore(&parent);
    fd_redirect_clear_state(&parent);
    fd_redirect_save(&parked); fd_redirect_restore(&nested);
    fd_redirect_clear_state(&nested);
    assert(fd_redirect_write(1, "C", 1) == 1 && aliases[3][0] == 'C');
    fd_redirect_reset_owned(3);
    fd_redirect_restore(&parked); fd_redirect_clear_state(&parked);
    assert(fd_redirect_write(1, "P", 1) == 1 && aliases[2][17] == 'P');
    assert(redir_table[1].access.app_id == 2 && redir_table[1].access.generation == 22);

    redir_table[1].buf_len = redir_table[1].buf_pos = 0;
    memset(aliases[2], 0xA5, BYTES); memset(aliases[3], 0x5A, BYTES);
    /* Poison old pointer: lookup must fail before dereferencing a dead AS. */
    struct addrspace *saved = redir_table[1].access.as;
    slots[2].state = APP_STATE_FREE; redir_table[1].access.as = (void *)1;
    refuse_write();
    slots[2].state = APP_STATE_RUNNING;
    refuse_write(); redir_table[1].access.as = saved;
    spaces[2].generation++; refuse_write(); spaces[2].generation--;
    spaces[2].owner++; refuse_write(); spaces[2].owner--;
    redir_table[1].access.pd_phys = spaces[3].pd_phys;
    refuse_write(); redir_table[1].access.pd_phys = spaces[2].pd_phys;
    slots[2].state = APP_STATE_FAULT_PENDING; refuse_write(); slots[2].state = APP_STATE_RUNNING;
    slots[2].state = APP_STATE_ABORT_PENDING; refuse_write(); slots[2].state = APP_STATE_RUNNING;
    slots[2].cpl3 = 0; refuse_write(); slots[2].cpl3 = 1;
    denied_page = 0; refuse_write();
    /* RO still readable; absent read page rejects without touching output. */
    redir_table[1].buf_len = 17; read_denied = 1;
    memset(out, 0xCC, sizeof(out));
    assert(fd_redirect_read(1, out, 17) == -1 && redir_table[1].buf_pos == 0);
    unchanged(out, sizeof(out), 0xCC);
    read_denied = 0;
    assert(fd_redirect_read(1, out, 17) == 17); unchanged(out, 17, 0xA5);
    denied_page = -1;

    /* Full-range preflight before any byte; copy/position bounded per page.
     * IRQ restore occurs between each page, for both incoming IF values. */
    for (unsigned int f = 0; f <= 1; f++) {
        irq_enabled = f;
        redir_table[1].buf_len = redir_table[1].buf_pos = 0;
        denied_page = 1; memset(out, 0x42, sizeof(out));
        assert(fd_redirect_write(1, out, BYTES) == -1);
        unchanged(aliases[2], BYTES, 0xA5); assert(redir_table[1].buf_len == 0);
        denied_page = -1;
        unsigned int before = saves;
        assert(fd_redirect_write(1, out, BYTES) == BYTES);
        assert(saves == before + 4 && saves == restores && irq_enabled == f);
        unchanged(aliases[2], BYTES, 0x42); unchanged(aliases[3], BYTES, 0x5A);
        memset(aliases[2], 0xA5, BYTES);
    }
    irq_enabled = 1;
    /* Unaligned tail crosses into the next page in both directions. */
    redir_table[1].buf_len = PAGE_SIZE - 5;
    redir_table[1].buf_pos = PAGE_SIZE - 5;
    assert(fd_redirect_write(1, payload, 17) == 17);
    assert(fd_redirect_read(1, out, 17) == 17 && !memcmp(out, payload, 17));
    unchanged(aliases[3], BYTES, 0x5A);
    redir_table[1].buf_len = BYTES + 1;
    assert(fd_redirect_write(1, payload, 17) == -1);
    assert(fd_redirect_read(1, out, 1) == -1);
    assert(fd_redirect_to_buffer(1, current_va, 16, 17) == -1);
    assert(fd_redirect_to_buffer(1, (u8 *)(uptr)0xfffffff0U, 32, 0) == -1);
    /* TRUSTED uses permanent identity addresses and never an AS walk. */
    u8 *trusted = mmap((void *)(uptr)(MEM_APP_BAND_BASE - PAGE_SIZE), PAGE_SIZE,
                      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(trusted != MAP_FAILED);
    user_call = 0;
    u32 before_walk = walk_count;
    assert(fd_redirect_to_buffer(1, trusted, PAGE_SIZE, 0) == 0);
    assert(redir_table[1].access.origin == REDIR_TRUSTED);
    assert(fd_redirect_write(1, payload, 17) == 17 && !memcmp(trusted, payload, 17));
    assert(fd_redirect_read(1, out, 17) == 17 && !memcmp(out, payload, 17));
    assert(walk_count == before_walk);
    assert(fd_redirect_to_buffer(1, trusted + PAGE_SIZE - 8, 16, 0) == -1);
    assert(fd_redirect_to_buffer(1, (u8 *)(uptr)MEM_APP_BAND_BASE, 16, 0) == -1);
    RedirAccess access = redir_table[1].access;
    assert(!redir_access_check(&access, ~(u32)0 - 7, 16, 1));
    assert(!redir_access_check(&access, MEM_APP_BAND_BASE - 8, ~(u32)0, 1));
    redir_table[1].buffer = (u8 *)(uptr)MEM_APP_BAND_BASE;
    redir_table[1].buf_len = 0;
    u32 refused = redir_refuse_count;
    assert(fd_redirect_write(1, payload, 17) == -1);
    assert(redir_refuse_count == refused + 1);
    munmap(trusted, PAGE_SIZE);
    assert(saves == restores && walk_count);
    puts("d0b: lifetime/reuse/RO/read/nest/park/pages/IF/bounds=OK");
    for (int id = 2; id <= 3; id++) { munmap(aliases[id], BYTES); close(fds[id]); }
    munmap(current_va, BYTES);
    return 0;
}
