//! Input-sensitive KAPI seam: Pass may also leave a committed candidate.
use super::*;
use std::sync::Mutex;

struct Kernel {
    candidate: bool,
    commit: Vec<u8>,
    preedit: Vec<u8>,
    calls: Vec<i32>,
}
static KERNEL: Mutex<Kernel> = Mutex::new(Kernel {
    candidate: false, commit: Vec::new(), preedit: Vec::new(), calls: Vec::new(),
});
const TAB: i32 = (SC_TAB << 8) | 9;
const A: i32 = (SC_A << 8) | b'a' as i32;
const HOME: i32 = (0x3e << 8) | 1;

unsafe extern "C" fn feed(key: i32) -> i32 {
    let mut k = KERNEL.lock().unwrap();
    k.calls.push(key);
    if key == 0x60 << 8 {
        k.candidate = false;
        k.commit.clear();
        k.preedit.clear();
        return -1;
    }
    // Match the real boundary: unread bytes consume the next call, even a key.
    if !k.commit.is_empty() {
        return k.commit.remove(0) as i32;
    }
    if key < 0 { return -1; }
    if k.candidate && (key == TAB || key == HOME) {
        k.candidate = false;
        k.commit.extend_from_slice("亜".as_bytes());
        return key;
    }
    if key == A {
        k.preedit.extend_from_slice("あ".as_bytes());
        return -1;
    }
    key
}

fn calls() -> Vec<i32> { KERNEL.lock().unwrap().calls.clone() }

fn setup() {
    mocks::fep_script(&[]);
    unsafe { (*os32api::api_ptr()).ime_feed_key = feed; }
    fep::install();
    fep::cancel_input();
    let mut k = KERNEL.lock().unwrap();
    k.candidate = true;
    k.calls.clear();
}

#[test]
fn imetab_pass_delivers_text_before_tab_key() {
    mocks::init(); fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 400, 300)]);
    setup();
    run(&mut st, &tap(SC_TAB, 0));
    let ev = events(&st, 0);
    assert_eq!(ev.iter().filter(|e| e.kind == GUI_EV_TEXT || e.kind == GUI_EV_KEY).map(|e| e.kind).collect::<Vec<_>>(),
               vec![GUI_EV_TEXT, GUI_EV_KEY, GUI_EV_KEY], "IMETAB Text before Key(Tab)");
    assert_eq!(text(&st, 0), "亜".as_bytes());
    assert_eq!(keys(&st, 0), vec![(true, SC_TAB as u8, 9, 0), (false, SC_TAB as u8, 9, 0)]);
    assert_eq!(calls(), vec![TAB, -1, -1, -1, -1]);
    fep_off();
}

#[test]
fn imetab_next_a_is_new_preedit() {
    for same_cycle in [false, true] {
        mocks::init(); fep_off();
        let shm = mocks::Shm::new();
        let mut st = windows(&shm, &[(20, 20, 400, 300)]);
        setup();
        if same_cycle {
            run(&mut st, &[SC_TAB | DOWN, SC_TAB, SC_A | DOWN, SC_A]);
        } else {
            run(&mut st, &tap(SC_TAB, 0));
            run(&mut st, &tap(SC_A, 0));
        }
        let (preedit, commit, calls) = {
            let k = KERNEL.lock().unwrap();
            (k.preedit.clone(), k.commit.clone(), k.calls.clone())
        };
        assert_eq!(preedit, "あ".as_bytes(), "IMETAB next a must not drain old candidate");
        assert!(commit.is_empty());
        assert_eq!(calls, vec![TAB, -1, -1, -1, -1, A]);
        assert_eq!(text(&st, 0), "亜".as_bytes());
        fep_off();
    }
}

#[test]
fn imetab_input_dialog_flushes_before_on_key() {
    mocks::init(); fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 400, 300)]);
    modal::open_wm_input(&mut st, b"Run\0", modal::WM_PURPOSE_RUN);
    setup();
    let start = modal::fep_caret().unwrap();
    // HOME, like TAB, commits then passes. Its caret move observes flush order:
    // inserting after on_key would leave the caret after the candidate.
    run(&mut st, &tap(0x3e, 0));
    assert_eq!(calls(), vec![HOME, -1, -1, -1, -1],
               "IMETAB dialog drains Pass candidate");
    assert_eq!(modal::fep_caret(), Some(start), "IMETAB Text must precede HOME on_key");
    run(&mut st, &tap(0x3f, 0)); // HELP = END: prove the candidate is in the field.
    assert!(modal::fep_caret().unwrap().0 > start.0, "IMETAB dialog contains candidate");
    assert!(text(&st, 0).is_empty(), "IMETAB modal text must not reach app");
    modal::reset(); fep_off();
}
