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
}
