/* Drain one unsigned commit byte; leave an exhausted buffer untouched. */
static int ime_commit_take(void)
{
    if (g_ime.commit_pos < g_ime.commit_len)
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    return -1;
}
