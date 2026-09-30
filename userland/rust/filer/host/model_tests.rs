//! filer の CopyJob だけをホストで動かす。KAPI の open/close/read/write/unlink
//! を差し替えた**挙動試験**で、実 FS には触れない。
use crate::model::{CopyJob, PATH_CAP};
use std::sync::Mutex;

pub static UNLINKED: Mutex<Vec<Vec<u8>>> = Mutex::new(Vec::new());
pub static OPEN_FDS: Mutex<Vec<i32>> = Mutex::new(Vec::new());
pub static CLOSED: Mutex<Vec<i32>> = Mutex::new(Vec::new());

fn cstr(p: *const u8) -> Vec<u8> {
    let mut v = Vec::new();
    let mut i = 0;
    unsafe {
        while *p.add(i) != 0 {
            v.push(*p.add(i));
            i += 1;
        }
    }
    v
}

unsafe extern "C" fn m_open(_path: *const u8, _mode: i32) -> i32 {
    let mut f = OPEN_FDS.lock().unwrap_or_else(|e| e.into_inner());
    let fd = 10 + f.len() as i32;
    f.push(fd);
    fd
}
unsafe extern "C" fn m_close(fd: i32) {
    CLOSED.lock().unwrap_or_else(|e| e.into_inner()).push(fd);
}
unsafe extern "C" fn m_unlink(path: *const u8) -> i32 {
    UNLINKED.lock().unwrap_or_else(|e| e.into_inner()).push(cstr(path));
    0
}
unsafe extern "C" fn m_read(_fd: i32, _buf: *mut u8, _size: u32) -> i32 {
    0 /* EOF: finish() へ進む */
}
unsafe extern "C" fn m_write(_fd: i32, _buf: *const u8, size: u32) -> i32 {
    size as i32
}

fn install() {
    UNLINKED.lock().unwrap_or_else(|e| e.into_inner()).clear();
    OPEN_FDS.lock().unwrap_or_else(|e| e.into_inner()).clear();
    CLOSED.lock().unwrap_or_else(|e| e.into_inner()).clear();
    unsafe {
        let a = &mut *os32api::api_ptr();
        a.sys_open = m_open;
        a.sys_close = m_close;
        a.sys_unlink = m_unlink;
        a.sys_read = m_read;
        a.sys_write = m_write;
    }
}

fn setup() {
    static ONCE: std::sync::Once = std::sync::Once::new();
    ONCE.call_once(|| {
        os32api::os32_init(Box::into_raw(Box::new(os32api::mock_api())));
    });
    install();
}

fn path(s: &[u8]) -> [u8; PATH_CAP] {
    let mut p = [0u8; PATH_CAP];
    p[..s.len()].copy_from_slice(s);
    p
}

/// 中断 (ESC / 閉じる / Session Quit / タイマ失敗) でも、自分が作った
/// コピー先は消す。消さないと途中まで書いたファイルが正常な顔で残る。
#[test]
fn abort_deletes_the_partial_output_it_created() {
    setup();
    let mut job = CopyJob::NEW;
    assert_eq!(job.start(&path(b"/a.bin\0"), &path(b"/b.bin\0"), true), 0);
    assert!(job.active && job.created);
    job.abort();
    assert!(!job.active);
    let unlinked = UNLINKED.lock().unwrap_or_else(|e| e.into_inner()).clone();
    assert_eq!(
        &unlinked[..],
        &[b"/b.bin".to_vec()],
        "中断で途中のコピー先が残った"
    );
    assert_eq!(job.src_fd, -1);
    assert_eq!(job.dst_fd, -1);
}

/// 印刷 (票 N4b §2) はファイラの選択 (cwd + 選択名) から `os32gui_print_file`
/// に渡す **絶対パス** と **basename** を組む。どちらも `model` の純関数
/// (`path_join` / `basename_off`) なので KAPI 無しで確かめられる。
#[test]
fn print_target_path_and_basename() {
    use crate::model::{basename_off, path_join};
    let mut p = [0u8; PATH_CAP];
    let n = path_join(b"/usr/docs", b"readme.txt", &mut p).unwrap();
    assert_eq!(&p[..n], b"/usr/docs/readme.txt");
    assert_eq!(&p[basename_off(&p[..n])..n], b"readme.txt");

    /* ルート直下は区切りを重ねない。 */
    let n = path_join(b"/", b"a.txt", &mut p).unwrap();
    assert_eq!(&p[..n], b"/a.txt");
    assert_eq!(&p[basename_off(&p[..n])..n], b"a.txt");
}

/// 上書きコピー (created == false) は中断しても消さない。既存ファイルを
/// 消してしまうほうが害が大きい。
#[test]
fn abort_keeps_an_existing_destination_it_did_not_create() {
    setup();
    let mut job = CopyJob::NEW;
    assert_eq!(job.start(&path(b"/a.bin\0"), &path(b"/b.bin\0"), false), 0);
    job.abort();
    let unlinked = UNLINKED.lock().unwrap_or_else(|e| e.into_inner()).clone();
    assert!(unlinked.is_empty(), "自分が作っていない出力を消した: {unlinked:?}");
}

/// **成功したコピーは消さない**。`finish` の後や 2 回目の `abort` で
/// unlink が走ると、正常なコピーが消えてしまう。
#[test]
fn abort_after_a_completed_copy_never_deletes_the_result() {
    setup();
    let mut job = CopyJob::NEW;
    assert_eq!(job.start(&path(b"/a.bin\0"), &path(b"/b.bin\0"), true), 0);
    /* m_read が 0 = EOF を返すので 1 周で完了する。 */
    let _ = job.step();
    assert!(!job.active, "step で完了していない");
    let after_finish = UNLINKED.lock().unwrap_or_else(|e| e.into_inner()).clone();
    assert!(after_finish.is_empty(), "完了時に消した: {after_finish:?}");
    job.abort();
    job.abort();
    let after_abort = UNLINKED.lock().unwrap_or_else(|e| e.into_inner()).clone();
    assert!(
        after_abort.is_empty(),
        "完了後の abort が成功したコピーを消した: {after_abort:?}"
    );
}
