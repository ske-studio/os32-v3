/* Included by the exec_notify_owned fixture, for owner and non-owner exits. */
static int exit_step, exiting_id, owner_before_exit;
static void exit_begin(int id)
{
    CHECK(exit_step == 0);
    exit_step = 1;
    exiting_id = id;
    owner_before_exit = appslot_gfx_owner();
}
static void exec_gfx_owner_exit(int id)
{
    CHECK(exit_step == 1 && id == exiting_id);
    CHECK(appslot_gfx_owner() == owner_before_exit);
    exit_step = 2;
}
