use libos32term::utf8::Decoder;

#[test]
fn scalar_boundaries_roundtrip_including_max_unicode() {
    for scalar in [
        0, 0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xffff, 0x10000, 0x10ffff,
    ] {
        let ch = char::from_u32(scalar).unwrap();
        let mut bytes = [0; 4];
        let text: &str = ch.encode_utf8(&mut bytes);
        for cuts in 0..1 << (text.len() - 1) {
            assert_eq!(decode_chunks(text.as_bytes(), cuts), text);
        }
    }
}

#[test]
fn all_two_byte_inputs_match_independent_lossy_utf8_oracle() {
    for first in 0u8..=255 {
        for second in 0u8..=255 {
            let input = [first, second];
            assert_eq!(
                decode_chunks(&input, 0),
                String::from_utf8_lossy(&input),
                "{input:x?}"
            );
        }
    }
}

#[test]
fn sampled_four_byte_inputs_match_oracle_at_every_partition() {
    let mut seed = 0x1234_5678u32;
    for _ in 0..4096 {
        seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
        let input = seed.to_le_bytes();
        for cuts in 0..8 {
            assert_eq!(
                decode_chunks(&input, cuts),
                String::from_utf8_lossy(&input),
                "{input:x?}"
            );
        }
    }
}

#[test]
fn finalization_replaces_only_pending_prefix_once() {
    for input in [&b"\xc2"[..], &b"\xe2\x82"[..], &b"\xf0\x9f\xa6"[..]] {
        let mut decoder = Decoder::default();
        for byte in input {
            assert_eq!(decoder.push(*byte).0, [None, None]);
        }
        assert_eq!(decoder.finish().0, [Some('�'), None]);
        assert_eq!(decoder.finish().0, [None, None]);
        assert_eq!(decoder.push(b'A').0, [Some('A'), None]);
    }
}

fn decode_chunks(input: &[u8], cuts: usize) -> String {
    let mut decoder = Decoder::default();
    let mut output = String::new();
    let mut start = 0;
    for end in 1..=input.len() {
        if end == input.len() || cuts & (1 << (end - 1)) != 0 {
            for byte in &input[start..end] {
                output.extend(decoder.push(*byte).0.into_iter().flatten());
            }
            start = end;
        }
    }
    output.extend(decoder.finish().0.into_iter().flatten());
    output
}

#[test]
fn every_partition_has_identical_utf8_output() {
    for input in [
        &b"a\0\xc2\xa2\xe6\xbc\xa2\xf0\x9f\xa6\x80"[..],
        &b"\xe2\x82A\xed\xa0\x80\xf4\x90\x80\x80"[..],
        &b"\xf0\x9f\xa6"[..],
    ] {
        let expected = decode_chunks(input, 0);
        for cuts in 0..1 << (input.len() - 1) {
            assert_eq!(decode_chunks(input, cuts), expected);
        }
    }
}

#[test]
fn malformed_sequences_use_documented_replacement_units() {
    let cases: &[(&[u8], &str)] = &[
        (b"\xe2\x82A", "�A"),
        (b"\xe2A", "�A"),
        (b"\xc0\xaf", "��"),
        (b"\xe0\x80\x80", "���"),
        (b"\xed\xa0\x80", "���"),
        (b"\xf4\x90\x80\x80", "����"),
        (b"\xf5\x80\xff\x80", "����"),
        (b"\xf0\x80\x80\x80", "����"),
        (b"\xe2\xc2\xa2", "�¢"),
        (b"\xe2\0Z", "�\0Z"),
    ];
    for (input, expected) in cases {
        let mut decoder = Decoder::default();
        let output: String = input
            .iter()
            .flat_map(|b| decoder.push(*b).0.into_iter().flatten())
            .collect();
        assert_eq!(&output, expected, "{input:x?}");
    }
}

#[test]
fn valid_multibyte_waits_for_complete_scalar() {
    for word in ["é", "漢", "🦀"] {
        let mut decoder = Decoder::default();
        for (i, byte) in word.bytes().enumerate() {
            let decoded = decoder.push(byte);
            assert_eq!(
                decoded.0,
                [
                    if i + 1 == word.len() {
                        word.chars().next()
                    } else {
                        None
                    },
                    None
                ]
            );
        }
    }
}

#[test]
fn ascii_including_nul_is_length_delimited() {
    let mut decoder = Decoder::default();
    let output: String = b"a\0Z"
        .iter()
        .flat_map(|b| decoder.push(*b).0.into_iter().flatten())
        .collect();
    assert_eq!(output, "a\0Z");
}
