/* All image, argv and arch preparation has succeeded. Failure here still
 * belongs to the pre-commit rollback path in exec_launch. */
static int exec_enter_prepared(AppSlot *ctx, int id, int launcher_id, int gui,
                               u32 context, u32 text_sz, volatile u32 *io_parent_depth)
{
    int is_shell = id == APP_ID_SHELL;
    u32 parent = is_shell ? 0 : (u32)launcher_id;
    u32 parent_gen = parent ? appslot_at(launcher_id)->exec_gen : 0;
    int gui_session = con_sink_is_enabled();
    if (input_context_owner_reserve(id, ctx->exec_gen, parent, parent_gen, context))
        return EXEC_ERR_INVALID;

    arch_fp_save(&fp_parent[exec_nest_level]);
    arch_fp_init();

    /* ======== ここで初めて「走っているのはこの ID」になる ======== */
    unsigned irq;
    if (is_shell) {
        appslot_shell_commit();
        /* T7b-b2 §3-1: 確定した shell の text 範囲で内部入口を登録する。
         * 順は retire → publish に固定 — 前の shell の終了 (exec_finish)
         * で retire 済みでも、ここで重ねて retire してから publish する
         * (retire 抜きの publish は旧 shell の lease を開いたまま残す)。 */
        shell_service_retire();
        irq = irq_save();
        shell_service_publish(ctx->load_addr, text_sz);
    } else {
        *io_parent_depth = io_syscall_suspend(launcher_id); /* IO_SYSCALL_CHILD */
        appslot_start_commit(id, gui, ctx->pages);
        if (ctx->cpl3) exec_owner_diag(ctx, 0, 0, 0);
        /* 暴走判定の起点 (票 T9 §12 S6)。 */
        appslot_mark_scheduled(id, tick_count);
        irq = irq_save();
    }
    av_focus_exec_enter(id);
    exec_nest_level = ctx->depth;

    input_context_owner_activate(id, is_shell || (!gui_session && !gui));
    irq_restore(irq);
    exec_entry_calls++;
    arch_entry_enter(&ctx->entry, ctx->cpl3 ? ctx->as->pd_phys : 0);
    return 0;
}
