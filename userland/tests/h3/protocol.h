/* T2h/h3 test-only SHM protocol v1. e9 shares this layout, not public KAPI.
 * All fields are little-endian u32. owner is the ledger AS owner, NOT GUI
 * slot or app ID. The fixture publishes owner then generation (commit word)
 * through caller_identity. Host verifies these values and writes only mode
 * and arm, in that order.
 */
#ifndef H3_PROTOCOL_H
#define H3_PROTOCOL_H
#define H3_MAGIC 0x48335031u
#define H3_VERSION 1u
#define H3_INIT 1u
#define H3_IDENTIFIED 2u
#define H3_WAIT 3u
#define H3_RESUMED 4u
#define H3_ARMED 5u
#define H3_FIRING 6u
#define H3_ERROR 7u
#define H3_PF 1u
#define H3_GP 2u
#define H3_DE 3u
#define H3_UD 4u
#define H3_USER_LOOP 5u
#define H3_KAPI_LOOP 6u
#define H3_GRACE_TICKS 500u

typedef struct {
    unsigned int magic, owner, generation, phase, mode, arm;
    unsigned int version, fixture, resumes, consumed, window, gui_slot;
} H3Block;
_Static_assert(sizeof(unsigned int) == 4 && sizeof(H3Block) == 48,
               "h3 wire words");

/* Defined after main by fixture.inc; also executed by the ILP32 host test. */
static int h3_identity(volatile H3Block *b);
static unsigned int h3_consume(volatile H3Block *b);
#endif
