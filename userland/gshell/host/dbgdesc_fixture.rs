//! 実物の型を Rust の欄アクセスで設定し、像と記述子を別々に書き出す。
#[test]
fn dbgdesc_fixture() {
    use crate::{dbgdesc::GSHELL_DBG_DESC, trim, wm};
    use core::mem::size_of_val;
    let out = std::path::PathBuf::from(std::env::var_os("GSHELL_FIXTURE_DIR").unwrap());
    let st = wm::g();
    for (i, owner, gen, rect, title, visible, minimized) in [
        (1, 2, 7, (-12, 30, 240, 160), &b"back"[..], true, false),
        (4, 3, 9, (80, -5, 320, 200), &b"front"[..], true, false),
        (6, 4, 2, (4, 5, 64, 48), &b"hidden"[..], false, false),
        (9, 5, 3, (9, 10, 120, 90), &b"mini"[..], false, true),
    ] {
        let w = &mut st.windows[i];
        w.used = true; w.owner = owner; w.gen = gen;
        (w.x, w.y, w.w, w.h) = rect;
        w.title[..title.len()].copy_from_slice(title);
        w.visible = visible; w.minimized = minimized;
    }
    st.zorder[..5].copy_from_slice(&[1, 4, 6, 0, 9]);
    st.z_count = 5; // 未使用窓・非表示窓が前にあっても前景は owner 3。
    st.slots[0].used = true; st.slots[0].owner = 3;
    st.slots[2].used = true; st.slots[2].owner = 2;
    st.slots[1].owner = 99; // 未使用 slot は出力に含めない。
    assert_eq!(st.front_owner(), 3);
    let sent = unsafe { &mut *trim::SENT.0.get() };
    *sent = [true, false, true, false];
    // Linux host の実メモリ像。Rust の未初期化 padding を &[u8] として読まず、
    // OS に bytes をコピーさせる。読み手は型の欄だけを解釈する。
    fn dump<T>(out: &std::path::Path, name: &str, value: &T) {
        use std::os::unix::fs::FileExt;
        let mut bytes = vec![0u8; size_of_val(value)];
        std::fs::File::open("/proc/self/mem").unwrap()
            .read_exact_at(&mut bytes, value as *const T as usize as u64).unwrap();
        std::fs::write(out.join(name), bytes).unwrap();
    }
    dump(&out, "gui.bin", &wm::GUI);
    dump(&out, "sent.bin", &trim::SENT);
    dump(&out, "desc.bin", &GSHELL_DBG_DESC);
}
