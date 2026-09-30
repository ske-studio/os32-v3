/* atan2.rs — 整数 atan2 (CORDIC方式)
 *
 * C版 atan2.c の Rust 移植。
 * CORDICアルゴリズムで y, x から角度を計算。
 * 戻り値は 0~511 (512分割角度, isin/icos と互換)。
 */

/* CORDIC の atan テーブル (512分割角度系)
 * atan(2^-i) を 512分割角度で表現 (i = 0..14) */
static CORDIC_ATAN: [i16; 15] = [
    64,   /* atan(2^0)  = 45.000° */
    38,   /* atan(2^-1) = 26.565° */
    20,   /* atan(2^-2) = 14.036° */
    10,   /* atan(2^-3) = 7.125°  */
     5,   /* atan(2^-4) = 3.576°  */
     3,   /* atan(2^-5) = 1.790°  */
     1,   /* atan(2^-6) = 0.895°  */
     1,   /* atan(2^-7) = 0.448°  */
     0, 0, 0, 0, 0, 0, 0,
];

/* ====================================================================== */
/*  iatan2 — 整数 atan2 (CORDIC 方式)                                       */
/*                                                                          */
/*  引数: y, x (符号付き整数)                                               */
/*  戻り値: 0 ~ 511 (512分割角度)                                           */
/*          0=右(+X), 128=下(+Y), 256=左(-X), 384=上(-Y)                    */
/* ====================================================================== */

pub fn iatan2(y: i32, x: i32) -> i32 {
    if x == 0 && y == 0 { return 0; }

    /* 第1象限に写像してCORDIC実行、後で補正 */
    let mut cx = if x < 0 { -x } else { x };
    let mut cy = if y < 0 { -y } else { y };
    let mut angle: i32 = 0;

    /* CORDIC ベクトルモード: ベクトル(cx, cy)をX軸に回転 */
    for i in 0..8 {
        if cy > 0 {
            /* 時計回りに回転 */
            let nx = cx + (cy >> i);
            cy = cy - (cx >> i);
            cx = nx;
            angle += CORDIC_ATAN[i as usize] as i32;
        } else {
            /* 反時計回りに回転 */
            let nx = cx - (cy >> i);
            cy = cy + (cx >> i);
            cx = nx;
            angle -= CORDIC_ATAN[i as usize] as i32;
        }
    }

    /* 角度を正に正規化 */
    if angle < 0 { angle = -angle; }

    /* 象限の補正 */
    if x < 0 && y >= 0 {
        /* 第2象限: 180° - angle */
        angle = 256 - angle;
    } else if x < 0 && y < 0 {
        /* 第3象限: 180° + angle */
        angle = 256 + angle;
    } else if x >= 0 && y < 0 {
        /* 第4象限: 360° - angle */
        angle = 512 - angle;
    }
    /* x >= 0, y >= 0: 第1象限はそのまま */

    angle & 511
}

/* ====================================================================== */
/*  angle_between — 2点間の角度 (512分割角度)                                */
/* ====================================================================== */

pub fn angle_between(x0: i32, y0: i32, x1: i32, y1: i32) -> i32 {
    iatan2(y1 - y0, x1 - x0)
}

/* ====================================================================== */
/*  C互換FFI関数                                                            */
/* ====================================================================== */

#[no_mangle]
pub extern "C" fn rs_iatan2(y: i32, x: i32) -> i32 { iatan2(y, x) }

#[no_mangle]
pub extern "C" fn rs_angle_between(x0: i32, y0: i32, x1: i32, y1: i32) -> i32 {
    angle_between(x0, y0, x1, y1)
}

/* ====================================================================== */
/*  テスト                                                                  */
/* ====================================================================== */

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_iatan2_axes() {
        /* +X軸 (右) = 0 */
        assert_eq!(iatan2(0, 100), 0);
        /* +Y軸 (下) = 128 (= 90°) */
        let a = iatan2(100, 0);
        assert!((a - 128).abs() <= 2);
        /* -X軸 (左) = 256 (= 180°) */
        let a = iatan2(0, -100);
        assert!((a - 256).abs() <= 2);
        /* -Y軸 (上) = 384 (= 270°) */
        let a = iatan2(-100, 0);
        assert!((a - 384).abs() <= 2);
    }

    #[test]
    fn test_iatan2_diagonal() {
        /* 45° = 64 */
        let a = iatan2(100, 100);
        assert!((a - 64).abs() <= 3);
    }

    #[test]
    fn test_iatan2_zero() {
        assert_eq!(iatan2(0, 0), 0);
    }

    #[test]
    fn test_angle_between() {
        /* (0,0) -> (10,0) = 右 ≈ 0 (CORDIC精度誤差で数ステップずれる) */
        let a = angle_between(0, 0, 10, 0);
        assert!(a <= 5 || a >= 507, "angle_between should be ~0, got {}", a);
    }
}
