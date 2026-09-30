/* os32_math — OS32 整数数学ライブラリ (Rust版)
 *
 * C版 libos32math の完全な Rust 移植。
 * FPUを使わず、固定小数点演算・LUT・CORDIC・ニュートン法で
 * ゲーム開発に必要な数学関数を提供する。
 *
 * モジュール構成:
 *   fix16  — Q16.16 固定小数点型と四則演算
 *   trig   — 整数 sin/cos LUT (512分割)
 *   sqrt   — 整数平方根・距離計算
 *   atan2  — CORDIC atan2
 *   recip  — 逆数LUTによる高速除算
 *   random — xorshift32 擬似乱数
 *   vec2   — 2Dベクトル演算
 *   lerp   — 線形補間・イージング関数
 */

#![no_std]

pub mod fix16;
pub mod trig;
pub mod sqrt;
pub mod atan2;
pub mod recip;
pub mod random;
pub mod vec2;
pub mod lerp;

/* 主要な型と定数を re-export */
pub use fix16::Fix16;
pub use vec2::Vec2;
