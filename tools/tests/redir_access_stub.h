extern int ring3_call_from_user(void);
extern int ring3_user_ranges_writable_always(u32, u32, u32, u32);
/* Exec boundary for unrelated redirect consumers. The d0b test includes the
 * real exec/redir_access.c; this shim preserves the older permission fixture. */
int redir_access_capture(RedirAccess *a)
{
    *a = (RedirAccess){0};
    a->origin = ring3_call_from_user();
    return 1;
}
int redir_access_check(const RedirAccess *a, u32 va, u32 len, int write)
{
    (void)write;
    return !a->origin || !len || ring3_user_ranges_writable_always(va, len, 0, 0);
}
int redir_access_copy(const RedirAccess *a, u8 *base, u32 *pos,
                      void *peer, u32 len, int write)
{
    u32 i;
    u8 *bytes = peer;
    if (!redir_access_check(a, (u32)(uptr)(base + *pos), len, write)) return -1;
    for (i = 0; i < len; i++) {
        if (write) base[*pos + i] = bytes[i];
        else bytes[i] = base[*pos + i];
    }
    *pos += len;
    return (int)len;
}
