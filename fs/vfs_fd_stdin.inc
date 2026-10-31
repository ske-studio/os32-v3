/* The progress belongs to AppSlot for restartable reads, otherwise this call. */
int vfs_stdin_continue(VfsStdinRead *state)
{
    int result;

    while (state->done < state->size) {
        int c = input_getchar_kapi();
        if (c < 0) {
            result = state->done ? (int)state->done : c;
            state->active = 0;
            return result;
        }
        if (c == '\r') c = '\n';
        state->buf[state->done] = (u8)c;
        state->done++;
        if (c == '\n') break;
    }
    result = (int)state->done;
    state->active = 0;
    return result;
}

static int vfs_stdin_read(void *buf, u32 size)
{
    VfsStdinRead local = {0};
    VfsStdinRead *state = exec_stdin_state();

    if (!state) state = &local;
    if (state->active) {
        if (state->buf != buf || state->size != size || state->done >= size) {
            state->active = 0;
            return VFS_ERR_INVAL;
        }
    } else {
        state->buf = (u8 *)buf;
        state->size = size;
        state->done = 0;
        state->active = 1;
    }
    return vfs_stdin_continue(state);
}
