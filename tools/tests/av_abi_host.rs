// Accompany the C fixture, calling through the SDK's actual Rust table type.
use crate::av::*;
use crate::kapi_generated::KernelAPI;
use core::mem::{offset_of, size_of};

#[no_mangle]
pub unsafe extern "C" fn av_rust_probe(api: *const KernelAPI) -> i32 {
    const {
        assert!(size_of::<SND2_DevInfo>() == 64);
        assert!(size_of::<SND2_Caps>() == 256);
        assert!(size_of::<SND2_DspUnit>() == 32);
        assert!(size_of::<SND2_DspParam>() == 32);
        assert!(size_of::<SND2_StreamDesc>() == 32);
        assert!(size_of::<SND2_Pos>() == 32);
        assert!(size_of::<SND2_FmEv>() == 12);
        assert!(size_of::<SND2_ChipCaps>() == 64);
        assert!(size_of::<SND2_RegW>() == 4);
        assert!(size_of::<IN2_PadState>() == 20);
        assert!(size_of::<IN2_PadCaps>() == 64);
        assert!(size_of::<IN2_Vibration>() == 4);
        assert!(size_of::<IN2_DevInfo>() == 32);
        assert!(size_of::<IN2_Event>() == 16);
        assert!(size_of::<IN2_Status>() == 32);
        assert!(size_of::<IN2_MouseState>() == 16);
        assert!(size_of::<IN2_WmState>() == 16);
        assert!(size_of::<V86_Begin>() == 136);
        assert!(size_of::<V86_RunIn>() == 32);
        assert!(size_of::<V86_Exit>() == 48);
        assert!(offset_of!(IN2_Event, seq) == 4);
        assert!(offset_of!(IN2_Event, flags) == 12);
        assert!(offset_of!(KernelAPI, snd2_dsp_process) == 1224);
    }
    static mut KEYS: [u8; 128] = [0xa5; 128];
    let key_state: unsafe extern "C" fn(*mut u8) -> i32 = (*api).in2_key_state;
    if key_state(core::ptr::addr_of_mut!(KEYS).cast()) != -17 || KEYS[0] != 0xa5 || KEYS[127] != 0xa5 { return 42; }
    if ((*api).snd2_dsp_process)(1, 2, 3 as *const i32, 4, 5 as *const u8, 6 as *mut u8, 7, 8) != 1 {
        return 43;
    }
    0
}
