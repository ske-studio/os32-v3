//! T7b-b3: guest paths of the system font set (single definition, [C4]).
//! `_C` variants are NUL-terminated for the open KAPI/service.
macro_rules! font_dir {
    () => {
        "/usr/share/fonts"
    };
}
pub const FONT_DIR: &str = font_dir!();
pub const FONT_JA: &str = concat!(font_dir!(), "/JF-Dot-Shinonome16.ttf");
pub const FONT_LATIN: &str = concat!(font_dir!(), "/Inconsolata-Regular.ttf");
pub const FONT_JA_C: &[u8] = concat!(font_dir!(), "/JF-Dot-Shinonome16.ttf\0").as_bytes();
pub const FONT_LATIN_C: &[u8] = concat!(font_dir!(), "/Inconsolata-Regular.ttf\0").as_bytes();
