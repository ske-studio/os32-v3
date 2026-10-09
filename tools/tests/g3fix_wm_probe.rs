// PM decision A: input priority is part of the contract, including WAIT_POLL.
// App allocation/DONE/resource effects are covered by trim_flow input-exception.
fn g3fix_order_probe(input: bool) -> i32 {
    use crate::{input::Ctx, mocks, multiapp, wm};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = g2w_state(&shm);
    // All apps have used their turn (reachable after the arm/focus operations).
    mocks::set_app_state(2, multiapp::APP_STATE_WAIT_POLL);
    if input {
        // Release of the target key: the app consumed make before malloc.
        mocks::push_rawkeys(&[0x14]);
    }
    g2w_pending((1 << 3) | (1 << 4));
    mocks::set_memstat_app(3, 8, 15);
    mocks::set_memstat_app(4, 8, 15);
    wm::wm_cycle(&mut st, Ctx::Standalone);
    g2w_check_trim(&st, 3, 15);
    g2w_check_trim(&st, 4, 15);
    assert_eq!(g2w_counts(), (2, 0));
    assert_eq!(multiapp::input_ready(&st, 2), input);
    assert!(multiapp::resume_one(&mut st));
    let picked = mocks::resume_calls()[0].0;
    println!("X3 delivered B/C; A WAIT_POLL input={input}; first resume={picked}");
    // The mock resume does not execute an app: consume its input explicitly.
    // C flow separately executes the real allocator, hooks and kernel DONE.
    set_input_ready(&mut st, picked, false);
    if input {
        assert_eq!(g2w_counts(), (2, 0));
        assert_eq!(mocks::MEMSTAT_GLOBAL.lock().unwrap().1, (1 << 3) | (1 << 4));
        g2w_check_trim(&st, 3, 15);
        g2w_check_trim(&st, 4, 15);
        for id in [3, 4] {
            assert!(multiapp::resume_one(&mut st));
            assert_eq!(mocks::resume_calls().last().unwrap().0, id);
            set_input_ready(&mut st, id, false);
        }
        assert_eq!(g2w_counts(), (2, 0));
    } else {
        for id in [4, 2] {
            assert!(multiapp::resume_one(&mut st));
            assert_eq!(mocks::resume_calls().last().unwrap().0, id);
            set_input_ready(&mut st, id, false);
        }
    }
    picked
}
#[test]
fn g3fix_gate_foreground_input_before_backs() {
    assert_eq!(g3fix_order_probe(true), 2, "foreground unread input has priority");
}
#[test]
fn g3fix_control_without_input() {
    assert_eq!(g3fix_order_probe(false), 3);
}
#[test]
fn g3fix_stop_click_during_hook_pump() {
    use crate::{input::Ctx, mocks, multiapp, wm};
    for confirmed_focus in [false, true] {
        mocks::init();
        let shm = mocks::Shm::new();
        *wm::g() = g2w_state(&shm);
        let st = wm::g();
        st.inited = true;
        g2w_pending(1 << 3);
        mocks::set_memstat_app(3, 8, 15);
        wm::wm_cycle(st, Ctx::Standalone);
        g2w_check_trim(st, 3, 15);
        set_input_ready(st, 3, false); // B consumes TRIM, then enters hook.
        mocks::set_app_state(3, 1); // B is in get_tick hook loop.
        // Complete a click on B's title within X4-only hook execution.
        *mocks::MOUSE.lock().unwrap() = (160, 15, 1);
        wm::wm_cycle(st, Ctx::Pump);
        *mocks::MOUSE.lock().unwrap() = (160, 15, 0);
        wm::wm_cycle(st, Ctx::Pump);
        assert_eq!(st.front_owner(), 2, "X4 does not apply title-bar focus");
        if confirmed_focus {
            let window = st.windows[1].id(1);
            assert_eq!(crate::handler::gshell_gui_handler(
                os32api::gui::proto::GUI_OP_WIN_SET_FOCUS, window, 3), 0);
            assert_eq!(wm::g().front_owner(), 3, "fixture focus ok before slow loop");
        }
        // Fifth park completed; resume_one captures keyboard, not mouse.
        mocks::set_app_state(3, multiapp::APP_STATE_WAIT_POLL);
        mocks::push_rawkeys(&[0x60 | 0x100 | (0x10 << 9)]);
        assert!(multiapp::resume_one(wm::g()));
        let expected = if confirmed_focus { 3 } else { 2 };
        assert_eq!(mocks::kill_calls(), vec![expected]);
        println!("hook B, confirmed focus B={confirmed_focus}: STOP kills {expected}");
    }
}

// Corresponds to case_poll_gui_ready() in multiapp_model_host.c. Both check
// state x unread input x derived readiness, not only empty WAIT_POLL.
#[test]
fn g3fix_model_product_ready_matrix() {
    use crate::{mocks, multiapp};
    for state in [multiapp::APP_STATE_PARKED, multiapp::APP_STATE_WAIT_POLL] {
        for input in [false, true] {
            for derived in [false, true] {
                mocks::init();
                let shm = mocks::Shm::new();
                let mut st = g2w_state(&shm);
                mocks::set_app_state(2, state);
                set_input_ready(&mut st, 2, input);
                set_derived_ready(&mut st, 2, derived);
                set_input_ready(&mut st, 3, true);
                assert_eq!(multiapp::input_ready(&st, 2), input);
                assert_eq!(multiapp::derived_ready(&st, 2), !input && derived);
                assert_eq!(multiapp::pick(&st), if input { 2 } else { 3 });
                set_input_ready(&mut st, 3, false);
                assert_eq!(multiapp::pick(&st), if input || derived || state == multiapp::APP_STATE_WAIT_POLL { 2 } else { 0 });
            }
        }
    }
}
