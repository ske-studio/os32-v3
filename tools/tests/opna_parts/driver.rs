#![allow(dead_code)]
mod adpcm;
mod rhythm;
mod state;
mod tables;
mod timer;
use std::io::{self, BufRead};
fn main() {
    let mut s: state::AudioState = unsafe { core::mem::zeroed() };
    let mut t: state::TimerState = unsafe { core::mem::zeroed() };
    let mut ram = [0; 0x40000];
    let mut waves = [[0u8; 194]; 6];
    adpcm::init(&mut s);
    rhythm::reset(&mut s);
    t.reset();
    s.reg29 = 31;
    for line in io::stdin().lock().lines() {
        let line = line.unwrap();
        let mut args = line.split_whitespace();
        let op = args.next().unwrap();
        let reg: u32 = args.next().unwrap().parse().unwrap();
        let val: u32 = args.next().unwrap().parse().unwrap();
        match op {
            "W" => {
                adpcm::write(&mut s, &mut ram, reg as u16, val as u8);
                rhythm::write(&mut s, reg as u16, val as u8);
                t.write(reg as u16, val as u8);
                if reg == 0x29 {
                    s.reg29 = val as u8;
                }
            }
            "X" => {
                adpcm::reset(&mut s);
                rhythm::reset(&mut s);
                t.reset();
                s.reg29 = 31;
            }
            "Q" => println!("{}", u8::from(t.irq_level(&s))),
            "D" => println!("{}", adpcm::read(&mut s, &mut ram)),
            "S" => println!("{}", adpcm::status_ex(&mut s, t.status)),
            "B" => println!("{}", ram[reg as usize]),
            "T" => {
                let next = t.next_event();
                let events = t.count(next.min(val) as u16, s.status_mask as u8);
                println!(
                    "{} {} {} {}",
                    next,
                    u8::from(events != 0),
                    u8::from(t.csm_due(events)),
                    t.status
                );
            }
            "I" => {
                let v = &mut waves[reg as usize];
                for (j, bytes) in v.chunks_exact_mut(2).enumerate() {
                    bytes.copy_from_slice(&((j * 9001 + reg as usize * 601) as i16).to_le_bytes());
                }
                assert!(rhythm::install(&mut s, reg as usize, v.as_ptr(), 97, val));
            }
            "V" => s.rhythm[reg as usize].volume = rhythm::volume(val as i32 - 100),
            "G" => s.rhythm_volume = rhythm::volume(val as i32 - 100),
            "M" => {
                s.adpcm.muted = u8::from(val & (1 << 9) != 0);
                s.rhythm_mask = (val >> 10) as i32;
            }
            "P" => {
                let mut dest = [0; 512];
                let slice = &mut dest[..val as usize * 2];
                adpcm::mix(&mut s, &ram, slice);
                unsafe { rhythm::mix(&mut s, slice) };
                for v in slice {
                    println!("{v}");
                }
            }
            _ => panic!(),
        }
    }
}
