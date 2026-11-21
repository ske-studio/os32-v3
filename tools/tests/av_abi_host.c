/* AV0 v90 public table and untouched output contract (ILP32). */
#include "av_contract.h"
#include "os32_kapi_slots.h"

volatile int ring3_wm_depth;
static int registered = 1, caller = 1, user_call;
static int notifications, last_old, last_new;
int exec_gui_registered(void) { return registered; }
int res_owner_get(void) { return caller; }
int ring3_call_from_user(void) { return user_call; }
void in2_on_focus(int old, int owner)
{
    notifications++;
    last_old = old;
    last_new = owner;
}

static unsigned char sentinel[8192] __attribute__((aligned(4)));
#define ABI_SIZE(type, size) typedef char size_of_##type[(sizeof(type) == (size)) ? 1 : -1]
ABI_SIZE(SND2_DevInfo, 64);
ABI_SIZE(SND2_Caps, 256);
ABI_SIZE(SND2_DspUnit, 32);
ABI_SIZE(SND2_DspParam, 32);
ABI_SIZE(SND2_StreamDesc, 32);
ABI_SIZE(SND2_Pos, 32);
ABI_SIZE(SND2_FmEv, 12);
ABI_SIZE(SND2_ChipCaps, 64);
ABI_SIZE(SND2_RegW, 4);
ABI_SIZE(IN2_PadState, 20);
ABI_SIZE(IN2_PadCaps, 64);
ABI_SIZE(IN2_Vibration, 4);
ABI_SIZE(IN2_DevInfo, 32);
ABI_SIZE(IN2_Event, 16);
ABI_SIZE(IN2_Status, 32);
ABI_SIZE(IN2_MouseState, 16);
ABI_SIZE(IN2_WmState, 16);
ABI_SIZE(V86_Begin, 136);
ABI_SIZE(V86_RunIn, 32);
ABI_SIZE(V86_Exit, 48);
static KernelAPI api;
extern int av_rust_probe(const KernelAPI *api);
#include "av_wrappers.inc"

static int probe_dsp(u32 dev, u32 unit, const i32 *params, u32 n,
                     const void *in, void *out, u32 frames, u32 fmt)
{
    return dev == 1 && unit == 2 && params == (i32 *)3 && n == 4 &&
           in == (void *)5 && out == (void *)6 && frames == 7 && fmt == 8;
}

int av_abi_probe(void)
{
    unsigned i;
    for (i = 0; i < sizeof(sentinel); i++) sentinel[i] = 0xa5;
    av_bind(&api);
    if (KAPI_SLOT_IO_PUMP != 297 || __builtin_offsetof(IN2_Event, flags) != 12 ||
        __builtin_offsetof(IN2_Event, seq) != 4) return 44;
    if (__builtin_offsetof(KernelAPI, snd2_dev_count) != 1200 ||
        api.snd2_dev_count != wrap_snd2_dev_count ||
        api.snd2_dev_count() != OS32_ERR_NOSYS) return 1;
    if (__builtin_offsetof(KernelAPI, snd2_dev_info) != 1204 ||
        api.snd2_dev_info != wrap_snd2_dev_info ||
        api.snd2_dev_info(1, (SND2_DevInfo *)sentinel) != OS32_ERR_NOSYS) return 2;
    if (__builtin_offsetof(KernelAPI, snd2_caps) != 1208 ||
        api.snd2_caps != wrap_snd2_caps ||
        api.snd2_caps(1, (SND2_Caps *)sentinel, 4) != OS32_ERR_NOSYS) return 3;
    if (__builtin_offsetof(KernelAPI, snd2_dsp_unit) != 1212 ||
        api.snd2_dsp_unit != wrap_snd2_dsp_unit ||
        api.snd2_dsp_unit(1, 1, (SND2_DspUnit *)sentinel) != OS32_ERR_NOSYS) return 4;
    if (__builtin_offsetof(KernelAPI, snd2_dsp_param) != 1216 ||
        api.snd2_dsp_param != wrap_snd2_dsp_param ||
        api.snd2_dsp_param(1, 1, 1, (SND2_DspParam *)sentinel) != OS32_ERR_NOSYS) return 5;
    if (__builtin_offsetof(KernelAPI, snd2_dsp_insert) != 1220 ||
        api.snd2_dsp_insert != wrap_snd2_dsp_insert ||
        api.snd2_dsp_insert(1, 1, (const i32 *)sentinel, 1) != OS32_ERR_NOSYS) return 6;
    if (__builtin_offsetof(KernelAPI, snd2_dsp_process) != 1224 ||
        api.snd2_dsp_process != wrap_snd2_dsp_process ||
        api.snd2_dsp_process(1, 1, (const i32 *)sentinel, 1, (const void *)sentinel, (void *)sentinel, 1, 1) != OS32_ERR_NOSYS) return 7;
    if (__builtin_offsetof(KernelAPI, snd2_stream_open) != 1228 ||
        api.snd2_stream_open != wrap_snd2_stream_open ||
        api.snd2_stream_open((const SND2_StreamDesc *)sentinel, (u32 *)sentinel) != OS32_ERR_NOSYS) return 8;
    if (__builtin_offsetof(KernelAPI, snd2_stream_write) != 1232 ||
        api.snd2_stream_write != wrap_snd2_stream_write ||
        api.snd2_stream_write(1, (const void *)sentinel, 1) != OS32_ERR_NOSYS) return 9;
    if (__builtin_offsetof(KernelAPI, snd2_stream_start) != 1236 ||
        api.snd2_stream_start != wrap_snd2_stream_start ||
        api.snd2_stream_start(1) != OS32_ERR_NOSYS) return 10;
    if (__builtin_offsetof(KernelAPI, snd2_stream_pause) != 1240 ||
        api.snd2_stream_pause != wrap_snd2_stream_pause ||
        api.snd2_stream_pause(1) != OS32_ERR_NOSYS) return 11;
    if (__builtin_offsetof(KernelAPI, snd2_stream_pos) != 1244 ||
        api.snd2_stream_pos != wrap_snd2_stream_pos ||
        api.snd2_stream_pos(1, (SND2_Pos *)sentinel) != OS32_ERR_NOSYS) return 12;
    if (__builtin_offsetof(KernelAPI, snd2_stream_wait) != 1248 ||
        api.snd2_stream_wait != wrap_snd2_stream_wait ||
        api.snd2_stream_wait(1, 1, 1) != OS32_ERR_NOSYS) return 13;
    if (__builtin_offsetof(KernelAPI, snd2_stream_volume) != 1252 ||
        api.snd2_stream_volume != wrap_snd2_stream_volume ||
        api.snd2_stream_volume(1, 1, 1) != OS32_ERR_NOSYS) return 14;
    if (__builtin_offsetof(KernelAPI, snd2_stream_close) != 1256 ||
        api.snd2_stream_close != wrap_snd2_stream_close ||
        api.snd2_stream_close(1, 1) != OS32_ERR_NOSYS) return 15;
    if (__builtin_offsetof(KernelAPI, av_focus_set) != 1260 ||
        api.av_focus_set != wrap_av_focus_set ||
        api.av_focus_set(1) != 0) return 16;
    if (__builtin_offsetof(KernelAPI, av_focus_get) != 1264 ||
        api.av_focus_get != wrap_av_focus_get ||
        api.av_focus_get() != 1) return 17;
    if (notifications != 1 || last_old != 0 || last_new != 1) return 50;
    caller = 2;
    ring3_wm_depth = 1;
    if (api.av_focus_set(2) || api.av_focus_get() != 2) return 51;
    if (notifications != 2 || last_old != 1 || last_new != 2) return 52;
    if (api.av_focus_set(2) || notifications != 2) return 53;
    ring3_wm_depth = 0;
    if (api.av_focus_set(3) != OS32_ERR_INVAL) return 54;
    caller = 1;
    user_call = 1;
    if (api.av_focus_set(3) != OS32_ERR_INVAL) return 55;
    user_call = 0;
    registered = 0;
    if (api.av_focus_set(3) != OS32_ERR_INVAL || notifications != 2) return 56;
    av_focus_exec_enter(3);
    av_focus_exec_enter(4);
    if (av_focus_owner() != 4) return 57;
    av_focus_exec_leave(4, 3);
    if (av_focus_owner() != 3) return 58;
    registered = 1;
    av_focus_exec_enter(5);
    av_focus_exec_leave(5, 1);
    if (av_focus_owner() != 3) return 59;
    av_focus_exec_leave(3, 1);
    if (av_focus_owner() != 0) return 60;
    if (__builtin_offsetof(KernelAPI, in2_pad_state) != 1268 ||
        api.in2_pad_state != wrap_in2_pad_state ||
        api.in2_pad_state(1, (IN2_PadState *)sentinel) != OS32_ERR_NOSYS) return 18;
    if (__builtin_offsetof(KernelAPI, in2_pad_caps) != 1272 ||
        api.in2_pad_caps != wrap_in2_pad_caps ||
        api.in2_pad_caps(1, (IN2_PadCaps *)sentinel) != OS32_ERR_NOSYS) return 19;
    if (__builtin_offsetof(KernelAPI, in2_pad_vibrate) != 1276 ||
        api.in2_pad_vibrate != wrap_in2_pad_vibrate ||
        api.in2_pad_vibrate(1, (const IN2_Vibration *)sentinel) != OS32_ERR_NOSYS) return 20;
    if (__builtin_offsetof(KernelAPI, in2_dev_count) != 1280 ||
        api.in2_dev_count != wrap_in2_dev_count ||
        api.in2_dev_count() != OS32_ERR_NOSYS) return 21;
    if (__builtin_offsetof(KernelAPI, in2_dev_info) != 1284 ||
        api.in2_dev_info != wrap_in2_dev_info ||
        api.in2_dev_info(1, (IN2_DevInfo *)sentinel) != OS32_ERR_NOSYS) return 22;
    if (__builtin_offsetof(KernelAPI, in2_read) != 1288 ||
        api.in2_read != wrap_in2_read ||
        api.in2_read((IN2_Event *)sentinel, 1) != OS32_ERR_NOSYS) return 23;
    if (__builtin_offsetof(KernelAPI, in2_key_state) != 1292 ||
        api.in2_key_state != wrap_in2_key_state ||
        api.in2_key_state((u8 *)sentinel) != OS32_ERR_NOSYS) return 24;
    if (__builtin_offsetof(KernelAPI, in2_mouse_state) != 1296 ||
        api.in2_mouse_state != wrap_in2_mouse_state ||
        api.in2_mouse_state((IN2_MouseState *)sentinel) != OS32_ERR_NOSYS) return 25;
    if (__builtin_offsetof(KernelAPI, in2_acquire) != 1300 ||
        api.in2_acquire != wrap_in2_acquire ||
        api.in2_acquire(1) != OS32_ERR_NOSYS) return 26;
    if (__builtin_offsetof(KernelAPI, in2_unacquire) != 1304 ||
        api.in2_unacquire != wrap_in2_unacquire ||
        api.in2_unacquire() != OS32_ERR_NOSYS) return 27;
    if (__builtin_offsetof(KernelAPI, in2_status) != 1308 ||
        api.in2_status != wrap_in2_status ||
        api.in2_status((IN2_Status *)sentinel) != OS32_ERR_NOSYS) return 28;
    if (__builtin_offsetof(KernelAPI, in2_wm_state) != 1312 ||
        api.in2_wm_state != wrap_in2_wm_state ||
        api.in2_wm_state((IN2_WmState *)sentinel) != OS32_ERR_NOSYS) return 29;
    if (__builtin_offsetof(KernelAPI, v86s_begin) != 1316 ||
        api.v86s_begin != wrap_v86s_begin ||
        api.v86s_begin((const V86_Begin *)sentinel, (u32 *)sentinel) != OS32_ERR_NOSYS) return 30;
    if (__builtin_offsetof(KernelAPI, v86s_run) != 1320 ||
        api.v86s_run != wrap_v86s_run ||
        api.v86s_run(1, (const V86_RunIn *)sentinel, (V86_Exit *)sentinel) != OS32_ERR_NOSYS) return 31;
    if (__builtin_offsetof(KernelAPI, v86s_fm_drain) != 1324 ||
        api.v86s_fm_drain != wrap_v86s_fm_drain ||
        api.v86s_fm_drain(1, (SND2_FmEv *)sentinel, 1, (u32 *)sentinel) != OS32_ERR_NOSYS) return 32;
    if (__builtin_offsetof(KernelAPI, v86s_end) != 1328 ||
        api.v86s_end != wrap_v86s_end ||
        api.v86s_end(1) != OS32_ERR_NOSYS) return 33;
    if (__builtin_offsetof(KernelAPI, snd2_chip_caps) != 1332 ||
        api.snd2_chip_caps != wrap_snd2_chip_caps ||
        api.snd2_chip_caps(1, (SND2_ChipCaps *)sentinel, 4) != OS32_ERR_NOSYS) return 34;
    if (__builtin_offsetof(KernelAPI, snd2_chip_open) != 1336 ||
        api.snd2_chip_open != wrap_snd2_chip_open ||
        api.snd2_chip_open(1, (u32 *)sentinel) != OS32_ERR_NOSYS) return 35;
    if (__builtin_offsetof(KernelAPI, snd2_chip_write) != 1340 ||
        api.snd2_chip_write != wrap_snd2_chip_write ||
        api.snd2_chip_write(1, (const SND2_RegW *)sentinel, 1) != OS32_ERR_NOSYS) return 36;
    if (__builtin_offsetof(KernelAPI, snd2_chip_read) != 1344 ||
        api.snd2_chip_read != wrap_snd2_chip_read ||
        api.snd2_chip_read(1, 1, 1, 1) != OS32_ERR_NOSYS) return 37;
    if (__builtin_offsetof(KernelAPI, snd2_chip_reset) != 1348 ||
        api.snd2_chip_reset != wrap_snd2_chip_reset ||
        api.snd2_chip_reset(1) != OS32_ERR_NOSYS) return 38;
    if (__builtin_offsetof(KernelAPI, snd2_chip_close) != 1352 ||
        api.snd2_chip_close != wrap_snd2_chip_close ||
        api.snd2_chip_close(1) != OS32_ERR_NOSYS) return 39;
    for (i = 0; i < sizeof(sentinel); i++)
        if (sentinel[i] != 0xa5) return 40;
    api.snd2_dsp_process = probe_dsp;
    if (!api.snd2_dsp_process(1, 2, (i32 *)3, 4, (void *)5, (void *)6, 7, 8)) return 41;
    return av_rust_probe(&api);
}
