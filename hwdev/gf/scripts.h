/* Private S1 script inventory shared by decoder and runner, not module ABI. */
#ifndef HWDEV_GF_SCRIPTS_H
#define HWDEV_GF_SCRIPTS_H
struct gf_script_span { u32 id, pc, length, crc; };
/* Callers borrow this immutable inventory. */
const struct gf_script_span *gfinit_script_spans(u32 *count);
#endif
