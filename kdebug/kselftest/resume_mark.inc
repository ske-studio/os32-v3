static void test_resume_mark(void)
{
    u32 bad = appslot_resume_mark_selftest();
    check((bad & (1u << 0)) == 0, "resume refuses a free slot");
    check((bad & (1u << 1)) == 0, "resume needs the OP_WAIT mark (PARKED)");
    check((bad & (1u << 2)) == 0, "resume needs the kbd mark (WAIT_KEY)");
    check((bad & (1u << 3)) == 0, "kill folds WAIT_KEY but not a running app");
    check((bad & (1u << 4)) == 0, "exec_app_state adds 3/4 without moving 0/1/2");
    check((bad & (1u << 5)) == 0, "refused resume never counts as a switch");
    /* 票 T8 §7 D8 (第 3 の park 点 = ポーリング型の 1 周だけの譲り) */
    check((bad & (1u << 6)) == 0, "resume needs the poll mark (WAIT_POLL)");
    check((bad & (1u << 7)) == 0, "poll yield is throttled to one PIT tick");
    /* 票 T9 D5 (第 4 の park 点 = 明示的な譲り)。印から「EAX に何を入れるか」
     * が導けないと、sh が譲っている間に子宛の打鍵を吸って捨てる。 */
    check((bad & (1u << 8)) == 0, "resume needs the yield mark, and reads no key");
}
