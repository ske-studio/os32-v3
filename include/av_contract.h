#ifndef AV_CONTRACT_H
#define AV_CONTRACT_H

#include "os32_kapi_shared.h"

int snd2_dev_count(void);
int snd2_dev_info(u32 dev, SND2_DevInfo *o);
int snd2_caps(u32 dev, SND2_Caps *o, u32 size);
int snd2_dsp_unit(u32 dev, u32 unit, SND2_DspUnit *o);
int snd2_dsp_param(u32 dev, u32 unit, u32 i, SND2_DspParam *o);
int snd2_dsp_insert(u32 stream, u32 unit, const i32 *params, u32 n);
int snd2_dsp_process(u32 dev, u32 unit, const i32 *params, u32 n, const void *in, void *out, u32 frames, u32 fmt_index);
int snd2_stream_open(const SND2_StreamDesc *d, u32 *stream);
int snd2_stream_write(u32 stream, const void *buf, u32 bytes);
int snd2_stream_start(u32 stream);
int snd2_stream_pause(u32 stream);
int snd2_stream_pos(u32 stream, SND2_Pos *o);
int snd2_stream_wait(u32 stream, u32 frame, u32 timeout_ms);
int snd2_stream_volume(u32 stream, u32 left, u32 right);
int snd2_stream_close(u32 stream, u32 how);
int in2_excl_wm_state(IN2_WmState *out);
int av_focus_is_wm(void);
int av_focus_set(int owner);
int av_focus_get(void);
int av_focus_owner(void);
void in2_reclaim(int owner);
void av_focus_exec_enter(int id);
void av_focus_exec_leave(int id, int parent);
int in2_pad_state(u32 slot, IN2_PadState *o);
int in2_pad_caps(u32 slot, IN2_PadCaps *o);
int in2_pad_vibrate(u32 slot, const IN2_Vibration *v);
int in2_dev_count(void);
int in2_dev_info(u32 i, IN2_DevInfo *o);
int in2_read(IN2_Event *out, u32 max);
int in2_key_state(u8 *out);
int in2_mouse_state(IN2_MouseState *o);
int in2_acquire(u32 flags);
int in2_unacquire(void);
int in2_status(IN2_Status *o);
int in2_wm_state(IN2_WmState *o);
int v86s_begin(const V86_Begin *b, u32 *sid);
int v86s_run(u32 sid, const V86_RunIn *in, V86_Exit *ex);
int v86s_fm_drain(u32 sid, SND2_FmEv *out, u32 max, u32 *last_seq);
int v86s_end(u32 sid);
int snd2_chip_caps(u32 dev, SND2_ChipCaps *o, u32 size);
int snd2_chip_open(u32 dev, u32 *h);
int snd2_chip_write(u32 h, const SND2_RegW *w, u32 n);
int snd2_chip_read(u32 h, u32 bank, u32 what, u32 reg);
int snd2_chip_reset(u32 h);
int snd2_chip_close(u32 h);

int kapi_snd2_dev_count(void);
int kapi_snd2_dev_info(u32 dev, SND2_DevInfo *o);
int kapi_snd2_stream_write(u32 stream, const void *buf, u32 bytes);
int kapi_snd2_dsp_process(u32 dev, u32 unit, const i32 *params, u32 n, const void *in, void *out, u32 frames, u32 fmt_index);

int kapi_v86s_check(u32 slot, const u32 *words);

#endif
