// Only GUI boundaries are mocked; the Filer state and methods are production code.
type WidgetId = u32;
type Timer = ();
struct Window;
impl Window { fn id(&self) -> u32 { 1 } }
#[derive(Default)]
struct Ui { quit: bool }
impl Ui { fn quit(&mut self) { self.quit = true; } }
impl Filer { fn repaint_path(&self) {} }

#[derive(Clone, Copy)]
struct GuiErr(i32);
impl GuiErr { fn code(self) -> i32 { self.0 } }
static LAUNCHES: std::sync::Mutex<Vec<Vec<u8>>> = std::sync::Mutex::new(Vec::new());
static LAUNCH_RC: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);
mod session {
    use super::*;
    pub fn session_launch(path: &[u8]) -> Result<(), GuiErr> {
        LAUNCHES.lock().unwrap_or_else(|e| e.into_inner()).push(path.to_vec());
        match LAUNCH_RC.load(std::sync::atomic::Ordering::Relaxed) {
            0 => Ok(()), rc => Err(GuiErr(rc)),
        }
    }
}
mod modal {
    use super::*;
    pub fn modal_open(_: u32, _: u16, _: &[u8]) -> Result<u16, GuiErr> { Ok(7) }
}
fn app() -> Filer {
    model_tests::setup();
    LAUNCHES.lock().unwrap_or_else(|e| e.into_inner()).clear();
    LAUNCH_RC.store(0, std::sync::atomic::Ordering::Relaxed);
    Filer::fixture()
}

#[test]
fn accepted_launch_allows_relaunch_without_quit() {
    let mut f = app();
    for _ in 0..3 { f.launch(b"/usr/bin/about.bin\0"); }
    assert_eq!(*LAUNCHES.lock().unwrap_or_else(|e| e.into_inner()), vec![b"/usr/bin/about.bin".to_vec(); 3]);
}

#[test]
fn accepted_launch_shows_basename_and_bounds_long_status() {
    let mut f = app();
    f.launch(b"/usr/bin/about.bin\0");
    assert_eq!(&f.status[..f.status_len], b"launched about.bin");
    let mut path = vec![b'/'];
    path.extend([b'x'; 200]);
    path.push(0);
    f.launch(&path);
    assert!(f.status_len <= f.status.len());
    assert!(f.status[..f.status_len].starts_with(b"launched "));
}

#[test]
fn accepted_launch_allows_new_folder_dialog() {
    let mut f = app();
    f.launch(b"/usr/bin/about.bin\0");
    f.op_mkdir();
    assert_eq!(f.pending, PEND_MKDIR);
    assert_eq!(f.dialog, 7);
}

#[test]
fn launch_errors_remain_visible_and_allow_retry() {
    let mut f = app();
    for rc in [OS32_ERR_FULL, OS32_ERR_NOSYS, OS32_ERR_INVAL] {
        LAUNCH_RC.store(rc, std::sync::atomic::Ordering::Relaxed);
        f.launch(b"/usr/bin/about.bin\0");
        let status = std::str::from_utf8(&f.status[..f.status_len]).unwrap();
        assert!(status.starts_with("Run failed: "), "{status}");
        assert!(status.contains(std::str::from_utf8(err_name(rc)).unwrap()), "{status}");
        assert!(status.contains(&format!("({rc})")), "{status}");
        f.pending = PEND_NONE; // User dismisses the error dialog.
    }
    LAUNCH_RC.store(0, std::sync::atomic::Ordering::Relaxed);
    f.launch(b"/usr/bin/about.bin\0");
    assert_eq!(LAUNCHES.lock().unwrap_or_else(|e| e.into_inner()).len(), 4);
}

#[test]
fn modal_and_copy_still_block_new_operations() {
    let mut f = app();
    f.pending = PEND_ERROR;
    f.op_mkdir();
    assert_eq!(f.pending, PEND_ERROR);
    f.pending = PEND_NONE;
    f.job.active = true;
    f.op_mkdir();
    assert_eq!(f.pending, PEND_NONE);
}

#[test]
fn session_quit_closes_copy_fds_and_returns() {
    for reason in [GUI_QUIT_REASON_SWITCH_CUI, GUI_QUIT_REASON_SHUTDOWN] {
        let mut f = app();
        f.launch(b"/usr/bin/about.bin\0");
        let mut src = [0; PATH_CAP]; src[..7].copy_from_slice(b"/a.bin\0");
        let mut dst = [0; PATH_CAP]; dst[..7].copy_from_slice(b"/b.bin\0");
        assert_eq!(f.job.start(&src, &dst, true), 0);
        f.timer = Some(());
        let mut ui = Ui::default();
        f.on_quit(&mut ui, reason);
        assert!(ui.quit);
        assert!(f.timer.is_none());
        assert!(!f.job.active);
        assert_eq!(f.job.src_fd, -1);
        assert_eq!(f.job.dst_fd, -1);
        assert_eq!(model_tests::CLOSED.lock().unwrap_or_else(|e| e.into_inner()).len(), 2);
        assert_eq!(*model_tests::UNLINKED.lock().unwrap_or_else(|e| e.into_inner()), vec![b"/b.bin".to_vec()]);
    }
}
