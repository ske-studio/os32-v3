/* GeForce thermal stop state shared by the resident backend and feature5.
 * Live protection state: never serialized in gf5_image or rolled back by
 * restore. Kept apart so resident headers need no feature5 internals. */
#ifndef GF5_STOP_STATE_H
#define GF5_STOP_STATE_H
#include "../../include/types.h"
enum gf5_stop_phase { GF5_STOP_NORMAL, GF5_STOP_REQUESTED, GF5_STOPPED };
struct gf5_stop_state {
    enum gf5_stop_phase state;
    i32 reason, milli_c, source_rc;
};
#endif
