//! pump.rs — syscall 境界ポンプ (契約 T6 / T8 の X4)。
//!
//! K2 がカーネルの `int 0x80` 入口から**上限付きで**呼ぶ。ここでできるのは
//! 入力の取り込みとリングへの追記、それにマウスカーソルの移動だけ
//! (数百バイトの転送)。**WM の状態機械は進めない**: ドラッグも閉じるボタンも
//! フォーカス切替もここでは起きない (それは X3 = `OP_WAIT` の中)。
//!
//! アプリが KAPI を呼んでいる限り WM が打鍵を落とさないのがこの経路の目的で、
//! 純粋な計算ループには効かない (T6 の但し書き。CTRL+STOP で kill)。

use crate::input;
use crate::wm;

/// `gui_register` に渡すポンプ。**戻り値なし・引数なし** (K2 の契約)。
#[no_mangle]
pub extern "C" fn gshell_gui_pump() {
    let st = wm::g();
    if !st.inited {
        return;
    }
    /* 再入防止。ポンプの中から KAPI を経由して再びポンプが呼ばれても、
     * 入力の二重取り込み (serial の重複) にしない。 */
    if st.in_pump {
        return;
    }
    st.in_pump = true;
    input::capture(st, input::Ctx::Pump);
    st.in_pump = false;
}
