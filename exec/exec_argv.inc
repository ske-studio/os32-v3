static int exec_build_argv(AppSlot *ctx, u32 launcher_pd, const char *cmdline,
                           int cmd_len, u32 *argv_addr)
{
    u32 stack_top = ctx->stack_top;
    u32 launch_argv[OS32_MAX_ARGS];
    u32 str_addr, d;
    int argc = 0, copy_failed = 0;
    const char *s = cmdline;

    stack_top -= (cmd_len + 1);
    stack_top &= ~((u32)(ABI_WORD_ALIGN - 1));
    str_addr = stack_top;

    stack_top -= sizeof(char *) * OS32_MAX_ARGS;
    *argv_addr = stack_top;

    s = cmdline;
    d = str_addr;
    while (launch_read_byte(launcher_pd, s, &copy_failed)) {
        char quote;

        /* 引数間の空白をスキップ */
        while (launch_read_byte(launcher_pd, s, &copy_failed) == ' ') s++;
        if (!launch_read_byte(launcher_pd, s, &copy_failed)) break;

        /* 新しい引数を開始 */
        if (argc < OS32_MAX_ARGS - 1) launch_argv[argc++] = d;

        /* クォート対応トークナイザ */
        while (launch_read_byte(launcher_pd, s, &copy_failed) && launch_read_byte(launcher_pd, s, &copy_failed) != ' ') {
            if (launch_read_byte(launcher_pd, s, &copy_failed) == '"' || launch_read_byte(launcher_pd, s, &copy_failed) == '\'') {
                quote = launch_read_byte(launcher_pd, s++, &copy_failed);
                while (launch_read_byte(launcher_pd, s, &copy_failed) && launch_read_byte(launcher_pd, s, &copy_failed) != quote) {
                    if (launch_read_byte(launcher_pd, s, &copy_failed) == '\\' && quote == '"' && launch_read_byte(launcher_pd, s + 1, &copy_failed)) {
                        s++;
                    }
                    { u8 ch = launch_read_byte(launcher_pd, s++, &copy_failed); copy_failed |= app_store(ctx, d++, &ch, 1); }
                }
                if (launch_read_byte(launcher_pd, s, &copy_failed) == quote) s++;  /* 閉じクォートをスキップ */
            } else if (launch_read_byte(launcher_pd, s, &copy_failed) == '\\' && launch_read_byte(launcher_pd, s + 1, &copy_failed)) {
                s++;
                { u8 ch = launch_read_byte(launcher_pd, s++, &copy_failed); copy_failed |= app_store(ctx, d++, &ch, 1); }
            } else {
                { u8 ch = launch_read_byte(launcher_pd, s++, &copy_failed); copy_failed |= app_store(ctx, d++, &ch, 1); }
            }
        }
        { u8 ch = 0; copy_failed |= app_store(ctx, d++, &ch, 1); }
    }
    launch_argv[argc] = 0;
    if (copy_failed || app_store(ctx, *argv_addr, launch_argv, (argc + 1) * sizeof(u32)))
        return EXEC_ERR_INVALID;
    return argc;
}
