static void h2_plan(unsigned int stack, unsigned int *bytes, unsigned int *depth, unsigned long *guard)
{
    *bytes = stack > MEM_EXEC_STACK_SIZE ? H2_LARGE_BYTES : H2_SMALL_BYTES;
    *depth = stack > MEM_EXEC_STACK_SIZE ? 7U : 3U;
    *guard = MEM_APP_STACK_TOP - stack - MEM_GUARD_SIZE;
}

static int h2_entry_ok(int argc)
{
    if (argc != 3) return 0;
    return 1;
}

/* Actual automatic arrays; never sweep addresses containing return frames. */
static int h2_argv_ok(struct h2_probe *p)
{
    const char *s;
    if (p->argc != 3 || !p->argv || !p->argv[0] || !p->argv[1]) return 0;
    s = p->argv[2];
    return s && s[0] == 'h' && s[1] == '2' && s[2] == '-' &&
           s[3] == 'a' && s[4] == 'r' && s[5] == 'g' && s[6] == 0;
}

static void h2_fill(struct h2_probe *p, volatile unsigned char *a, unsigned int n)
{
    unsigned int i;
    unsigned long first = (unsigned long)a, last = first + n;
    if (first < p->low) p->low = first;
    if (last > p->high) p->high = last;
    for (i = 0; i < n; i += H2_PAGE_BYTES) {
        a[i] = (unsigned char)(H2_PATTERN ^ (i / H2_PAGE_BYTES));
        p->pages++;
    }
    a[n - 1] = H2_PATTERN;
}

static int h2_verify(volatile unsigned char *a, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i += H2_PAGE_BYTES)
        if (a[i] != (unsigned char)(H2_PATTERN ^ (i / H2_PAGE_BYTES))) return 0;
    return a[n - 1] == H2_PATTERN;
}

static __attribute__((noinline)) int h2_deep(struct h2_probe *p, unsigned int depth)
{
    volatile unsigned char frame[H2_DEEP_BYTES];
    int ok;
    h2_fill(p, frame, sizeof(frame));
    if (depth) {
        ok = h2_deep(p, depth - 1);
    } else {
        ok = h2_argv_ok(p);
        if (ok && p->wait) ok = p->wait(p, frame, sizeof(frame));
        if (!h2_argv_ok(p)) ok = 0;
    }
    return h2_verify(frame, sizeof(frame)) && ok;
}

static __attribute__((noinline)) int h2_large(struct h2_probe *p, unsigned int depth)
{
    volatile unsigned char array[H2_LARGE_BYTES];
    int ok;
    h2_fill(p, array, sizeof(array));
    ok = h2_deep(p, depth);
    return h2_verify(array, H2_LARGE_BYTES) && ok;
}

static __attribute__((noinline)) int h2_small(struct h2_probe *p, unsigned int depth)
{
    volatile unsigned char array[H2_SMALL_BYTES];
    int ok;
    h2_fill(p, array, sizeof(array));
    ok = h2_deep(p, depth);
    return h2_verify(array, H2_SMALL_BYTES) && ok;
}

static int h2_probe_run(struct h2_probe *p, unsigned int bytes, unsigned int depth)
{
    p->low = ~0UL;
    p->high = 0;
    p->pages = 0;
    return bytes == H2_LARGE_BYTES ? h2_large(p, depth) : h2_small(p, depth);
}
