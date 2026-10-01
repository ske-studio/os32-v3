/* T2d d0a: real redirect code; Linux mappings stand in for CR3 switching.
 * Two distinct memfd backings, one VA, plus stable aliases for observation.
 * Permission/exec/VFS boundaries are stubs; no redirect logic is duplicated.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../../fs/fd_redirect.c"

#define PROBE_BYTES 64
#define PARENT_FILL 0xA5
#define CHILD_FILL 0x5A
#define PARENT_OWNER 2
#define CHILD_OWNER 3

static u8 *current_va;
static int checks;
static const char payload[] = "d0a child stdout\n";

int ring3_ptr_ok(u32 p) { return p == (u32)current_va; }
int ring3_call_from_user(void) { return 1; }
int ring3_user_ranges_writable_always(u32 pa, u32 la, u32 pb, u32 lb)
{
    checks++;
    assert(pb == 0 && lb == 0);
    return pa >= (u32)current_va && pa - (u32)current_va <= PROBE_BYTES &&
           la <= PROBE_BYTES - (pa - (u32)current_va);
}
int ring3_user_ranges_writable(u32 pa, u32 la, u32 pb, u32 lb)
{ return ring3_user_ranges_writable_always(pa, la, pb, lb); }
void ring3_fault_kill(void) { abort(); }
int vfs_open(const char *p, int m) { (void)p; (void)m; abort(); }
void vfs_close(int fd) { (void)fd; abort(); }
int vfs_seek(int fd, int o, int w) { (void)fd; (void)o; (void)w; abort(); }
int vfs_read_fd(int fd, void *b, u32 n) { (void)fd; (void)b; (void)n; abort(); }
int vfs_write_fd(int fd, const void *b, u32 n) { (void)fd; (void)b; (void)n; abort(); }

static u8 *map_backing(int fd, size_t bytes, void *va)
{
    void *p = mmap(va, bytes, PROT_READ | PROT_WRITE,
                   MAP_SHARED | (va ? MAP_FIXED : 0), fd, 0);
    assert(p != MAP_FAILED);
    if (va) assert(p == va);
    return p;
}

int main(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    int parent_fd = memfd_create("d0a-parent", 0);
    int child_fd = memfd_create("d0a-child", 0);
    u8 *parent, *child;
    int parent_ok, child_ok, exact_child_write;
    assert(parent_fd >= 0 && child_fd >= 0 && page >= PROBE_BYTES);
    assert(ftruncate(parent_fd, page) == 0 && ftruncate(child_fd, page) == 0);
    parent = map_backing(parent_fd, page, NULL);
    child = map_backing(child_fd, page, NULL);
    current_va = map_backing(parent_fd, page, NULL);

    /* Normal control: the registered process writes in its own AS. */
    fd_redirect_init();
    res_owner_set(PARENT_OWNER);
    memset(current_va, PARENT_FILL, PROBE_BYTES);
    assert(fd_redirect_to_buffer(1, current_va, PROBE_BYTES, 0) == 0);
    assert(fd_redirect_write(1, payload, sizeof(payload) - 1) == sizeof(payload) - 1);
    assert(memcmp(parent, payload, sizeof(payload) - 1) == 0);
    for (size_t i = sizeof(payload) - 1; i < PROBE_BYTES; i++) assert(parent[i] == PARENT_FILL);
    assert(fd_redirect_get_buf_len(1) == sizeof(payload) - 1);
    fd_redirect_reset(1);
    puts("d0a: same_as=OK");

    /* Register in parent -> switch backing -> child writes/self-checks -> exit
     * only child-owned resources -> restore parent backing -> parent checks. */
    memset(current_va, PARENT_FILL, PROBE_BYTES);
    memset(child, CHILD_FILL, PROBE_BYTES);
    assert(fd_redirect_to_buffer(1, current_va, PROBE_BYTES, 0) == 0);
    map_backing(child_fd, page, current_va);
    assert(current_va[0] == CHILD_FILL && parent[0] == PARENT_FILL);
    res_owner_set(CHILD_OWNER);
    assert(fd_redirect_write(1, payload, sizeof(payload) - 1) == sizeof(payload) - 1);
    child_ok = 1;
    exact_child_write = memcmp(child, payload, sizeof(payload) - 1) == 0;
    for (size_t i = 0; i < PROBE_BYTES; i++) {
        if (current_va[i] != CHILD_FILL) child_ok = 0;
        if (i >= sizeof(payload) - 1 && child[i] != CHILD_FILL) exact_child_write = 0;
    }
    fd_redirect_reset_owned(CHILD_OWNER);
    map_backing(parent_fd, page, current_va);
    res_owner_set(PARENT_OWNER);
    assert(fd_is_redirected(1) && fd_redirect_get_buf_len(1) == sizeof(payload) - 1);
    parent_ok = memcmp(current_va, payload, sizeof(payload) - 1) == 0;
    int parent_untouched = 1;
    for (size_t i = 0; i < PROBE_BYTES; i++) {
        if (current_va[i] != PARENT_FILL) parent_untouched = 0;
        if (i >= sizeof(payload) - 1 && current_va[i] != PARENT_FILL) parent_ok = 0;
    }
    assert(checks == 4); /* registration + write, for each case */
    fd_redirect_reset(1);
    printf("d0a: parent_buffer=%s\n", parent_ok ? "OK" : "MISSING");
    printf("d0a: child_value=%s\n", child_ok ? "OK" : "CHANGED");
    munmap(current_va, page); munmap(parent, page); munmap(child, page);
    close(parent_fd); close(child_fd);
    if (parent_ok && child_ok) return 0;
    /* Only this observed failure is expected; arbitrary failures are errors. */
    if (parent_untouched && !child_ok && exact_child_write) return 1;
    return 2;
}
