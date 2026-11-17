// Diagnostic sample coordinates are owned by the C/Rust shared header [C4].
use std::{env, fs, path::PathBuf};
fn main() {
    let root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let header = root.join("../../sdk/include/os32/os32_gui_shared.h");
    println!("cargo:rerun-if-changed={}", header.display());
    let source = fs::read_to_string(header).unwrap();
    let mut output = String::new();
    for name in ["OS32_GUI_DIAG_P1_X", "OS32_GUI_DIAG_P1_Y",
                 "OS32_GUI_DIAG_P2_X", "OS32_GUI_DIAG_P2_Y"] {
        let value = source.lines().find_map(|line| {
            let mut words = line.split_whitespace();
            if words.next() == Some("#define") && words.next() == Some(name) {
                Some(words.next().unwrap().parse::<i32>().unwrap())
            } else { None }
        }).unwrap_or_else(|| panic!("missing shared constant {name}"));
        output += &format!("const {name}: i32 = {value};\n");
    }
    fs::write(PathBuf::from(env::var("OUT_DIR").unwrap()).join("diag_points.rs"), output).unwrap();

    // The kernel-internal shell service table (T7b-b2) is owned by exec/shell_service.h.
    let header = root.join("../../exec/shell_service.h");
    println!("cargo:rerun-if-changed={}", header.display());
    let source = fs::read_to_string(header).unwrap();
    let mut output = String::new();
    for name in ["SHELL_SERVICE_MAGIC", "SHELL_SERVICE_VERSION", "SHELL_SERVICE_SIZE"] {
        let value = source.lines().find_map(|line| {
            let mut words = line.split_whitespace();
            if words.next() == Some("#define") && words.next() == Some(name) {
                let text = words.next().unwrap().trim_end_matches(['u', 'U']);
                Some(match text.strip_prefix("0x") {
                    Some(hex) => u32::from_str_radix(hex, 16).unwrap(),
                    None => text.parse::<u32>().unwrap(),
                })
            } else { None }
        }).unwrap_or_else(|| panic!("missing shared constant {name}"));
        output += &format!("pub const {name}: u32 = {value:#x};\n");
    }
    fs::write(PathBuf::from(env::var("OUT_DIR").unwrap()).join("shell_service.rs"), output).unwrap();

    // T7b-b3 S3: the font service/guard return codes (src/font.rs) are owned by
    // sdk/include/os32/os32_kapi_shared.h [C4].
    let header = root.join("../../sdk/include/os32/os32_kapi_shared.h");
    println!("cargo:rerun-if-changed={}", header.display());
    let source = fs::read_to_string(header).unwrap();
    let mut output = String::new();
    for name in ["OS32_ERR_NOSPC", "OS32_ERR_NOSYS", "OS32_ERR_BUSY"] {
        let value = source.lines().find_map(|line| {
            let mut words = line.split_whitespace();
            if words.next() == Some("#define") && words.next() == Some(name) {
                Some(words.next().unwrap().parse::<i32>().unwrap())
            } else { None }
        }).unwrap_or_else(|| panic!("missing shared constant {name}"));
        output += &format!("pub const {name}: i32 = {value};\n");
    }
    fs::write(PathBuf::from(env::var("OUT_DIR").unwrap()).join("kapi_err.rs"), output).unwrap();
    // G1: read the LAN operator contract; never duplicate its numeric values.
    let mut output = String::new();
    for (file, names) in [
        ("../../include/remote_control.h", vec!["REMOTE_SVC_GSERIAL", "REMOTE_NOTIFY_EXECUTE", "REMOTE_NOTIFY_PROBE", "REMOTE_GSERIAL_LINE_MAX", "REMOTE_ID8_HEX_DIGITS"]),
        ("../../net/link.h", vec!["LINK_MAX_PAYLOAD", "LINK_NOTIFY_DATA_MAX"]),
        ("../../sdk/include/os32/os32_kapi_shared.h", vec!["OS32_ERR_AGAIN", "OS32_ERR_STALE", "OS32_ERR_FULL"]),
    ] {
        let header = root.join(file);
        println!("cargo:rerun-if-changed={}", header.display());
        let source = fs::read_to_string(header).unwrap();
        for name in names {
            let value = source.lines().find_map(|line| {
                let words: Vec<_> = line.split_whitespace().collect();
                if words.get(0) == Some(&"#define") && words.get(1) == Some(&name) {
                    Some(words[2].trim_end_matches(['u', 'U']).parse::<i32>().unwrap())
                } else { None }
            }).unwrap_or_else(|| panic!("missing shared constant {name}"));
            output += &format!("const {name}: i32 = {value};\n");
        }
    }
    fs::write(PathBuf::from(env::var("OUT_DIR").unwrap()).join("remote_control.rs"), output).unwrap();

}
