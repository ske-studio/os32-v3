static void check(int ok, const char *message)
{
    if (ok) return;
    unsigned n = 0;
    while (message[n]) ++n;
    __asm__ volatile("int $0x80" :: "a"(4), "b"(2), "c"(message), "d"(n) : "memory");
    __asm__ volatile("int $0x80" :: "a"(1), "b"(1));
    __builtin_unreachable();
}

static void passed(void)
{
    __asm__ volatile("int $0x80" :: "a"(1), "b"(0));
    __builtin_unreachable();
}
