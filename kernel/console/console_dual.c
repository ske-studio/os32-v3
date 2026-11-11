#include "console_dual.h"
#include "con_sink.h"
#include "legacy_guest.h"

static int cfg;
static int was_live;
static struct con_out outs[CON_OUT_MAX];
static int n_out;
static u32 key;
static int have_key;

void con_dual_configure(int on)
{
    cfg = !!on;
}

int con_dual_cfg(void)
{
    return cfg;
}

int con_dual_active(void)
{
    if (!cfg || !con_sink_is_enabled() || legacy_active()) return 0;
    u32 k = display_dual_key ? display_dual_key() : 0;
    if (!have_key || key != k) {
        n_out = display_dual_outputs ? display_dual_outputs(outs, CON_OUT_MAX) : 0;
        key = k;
        have_key = 1;
    }
    int live = 0;
    for (int i = 0; i < n_out; i++)
        live |= outs[i].role == CON_OUT_TEXT && outs[i].live;
    if (live) was_live = 1;
    return live;
}

int con_dual_was_live(void)
{
    return was_live;
}

void con_dual_session_end(void)
{
    was_live = 0;
}

const struct con_out *con_dual_outputs(int *n)
{
    *n = n_out;
    return outs;
}
