/* Select the owner of a restartable stdin read; WM reads stay synchronous. */
VfsStdinRead *exec_stdin_state(void)
{
    if (!g_cur_app || !g_cur_app->gui || !g_cur_app->cpl3 ||
        !ring3_in_syscall || ring3_wm_depth || !g_cur_frame ||
        g_cur_frame[APP_FRAME_EAX] != KAPI_SLOT_SYS_READ ||
        appslot_get(appslot_cur()) != g_cur_app)
        return 0;
    return &g_cur_app->stdin_read;
}
