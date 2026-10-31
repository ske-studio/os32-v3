/* Shared CUI delivery: unread commit bytes, one original key, then cooked input.
 * A reader drains this state before fetching another key, so one slot suffices.
 * GUI keeps its current key in the WM and drains only commit_buf via feed(-1). */
static int pending_keydata = -1;

static void ime_delivery_clear(void)
{
    pending_keydata = -1;
}

/* Return a committed byte (scan=0), a saved key including scan, or -1. */
static int ime_delivery_take(void)
{
    if (g_ime.commit_pos < g_ime.commit_len)
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    int keydata = pending_keydata;
    pending_keydata = -1;
    return keydata;
}

/* Process a cooked key once. A Pass result can also have committed text. */
static int ime_delivery_process(int keydata)
{
    int result = ime_process_key(keydata);
    if (result == -1 && g_ime.commit_pos < g_ime.commit_len)
        pending_keydata = keydata;
    if (g_ime.commit_pos < g_ime.commit_len)
        return ime_delivery_take();
    return result == -1 ? keydata : -1;
}
