#include "av_contract.h"
#include "in2_int.h"

int input_key_pressed(u8 sc) { (void)sc; return 0; }
u32 input_key_release_gen(u8 sc) { (void)sc; return 0; }
void kbd_raw_purge_keep(void) {}
void in2_ring_lost(u8 reason) { (void)reason; }

/* Native C uses unsigned long for kernel words. Copy into the Rust SDK layout. */
int host_public_wm_state(unsigned int *words)
{
    IN2_WmState state;
    int rc = in2_wm_state(&state);
    if (rc == 0) {
        words[0] = 4 * sizeof(*words);
        words[1] = state.gen;
        words[2] = state.excl;
        words[3] = state.owner;
    }
    return rc;
}
