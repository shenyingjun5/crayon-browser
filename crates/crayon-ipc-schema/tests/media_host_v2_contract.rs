use crayon_ipc_schema::media_host_v2::{decode, encode, matches_hello, Handshake, Kind};
use crayon_ipc_schema::{decode_media_host_message, encode_media_host_message, MediaHostMessage};

fn hello() -> Handshake {
    Handshake {
        kind: Kind::Hello,
        session_id: 7,
        generation: 9,
        capabilities: 15,
        max_frame_bytes: 16_384,
        max_page_items: 16,
    }
}
fn welcome() -> Handshake {
    Handshake {
        kind: Kind::Welcome,
        capabilities: 1,
        max_frame_bytes: 8192,
        max_page_items: 8,
        ..hello()
    }
}
fn bytes(hex: &str) -> Vec<u8> {
    hex.as_bytes()
        .chunks_exact(2)
        .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
        .collect()
}

#[test]
fn shared_golden_and_previous_wire_are_distinct() {
    let golden = include_str!("../../../tests/contracts/media_host_v2_handshake.golden");
    let mut count = 0;
    for line in golden.lines() {
        let (name, hex) = line.split_once(' ').unwrap();
        let expected = match name {
            "hello" => hello(),
            "welcome" => welcome(),
            "hello-boundary" => Handshake {
                session_id: u64::MAX,
                generation: 0x0102_0304_0506_0708,
                capabilities: 0,
                max_frame_bytes: 34,
                max_page_items: 1,
                ..hello()
            },
            _ => panic!("unknown golden"),
        };
        let wire = bytes(hex);
        assert_eq!(encode(expected).unwrap(), wire);
        assert_eq!(decode(&wire).unwrap(), expected);
        assert!(decode_media_host_message(&wire).is_err());
        count += 1;
    }
    assert_eq!(count, 3);
    let v1 = encode_media_host_message(&MediaHostMessage::Shutdown).unwrap();
    assert!(decode(&v1).is_err());
}

#[test]
fn malformed_frames_fail_closed() {
    let wire = encode(hello()).unwrap();
    for length in 0..wire.len() {
        assert!(decode(&wire[..length]).is_err());
    }
    let mut trailing = wire.clone();
    trailing.push(0);
    assert!(decode(&trailing).is_err());
    assert!(decode(&vec![0; 16_385]).is_err());
    for (index, byte) in [(0, b'X'), (5, 1), (6, 0), (6, 3), (7, 1), (27, 16)] {
        let mut bad = wire.clone();
        bad[index] = byte;
        assert!(decode(&bad).is_err(), "offset {index}");
    }
    for (begin, end) in [(8, 16), (16, 24), (28, 32), (32, 34)] {
        let mut bad = wire.clone();
        bad[begin..end].fill(0);
        assert!(decode(&bad).is_err());
    }
    for (index, byte) in [(31, 1), (33, 17)] {
        let mut bad = wire.clone();
        bad[index] = byte;
        assert!(decode(&bad).is_err());
    }
}

#[test]
fn local_invalid_values_and_negotiation_expansion_are_rejected() {
    assert!(matches_hello(hello(), welcome()));
    assert!(!matches_hello(welcome(), hello()));
    for bad in [
        Handshake {
            session_id: 0,
            ..hello()
        },
        Handshake {
            generation: 0,
            ..hello()
        },
        Handshake {
            capabilities: 16,
            ..hello()
        },
        Handshake {
            max_frame_bytes: 33,
            ..hello()
        },
        Handshake {
            max_frame_bytes: 16_385,
            ..hello()
        },
        Handshake {
            max_page_items: 0,
            ..hello()
        },
        Handshake {
            max_page_items: 17,
            ..hello()
        },
    ] {
        assert!(encode(bad).is_err());
        assert!(!matches_hello(bad, welcome()));
    }
    for bad in [
        Handshake {
            session_id: 8,
            ..welcome()
        },
        Handshake {
            generation: 10,
            ..welcome()
        },
        Handshake {
            capabilities: 16,
            ..welcome()
        },
        Handshake {
            max_frame_bytes: 16_385,
            ..welcome()
        },
    ] {
        assert!(!matches_hello(hello(), bad));
    }
    let restricted = Handshake {
        capabilities: 1,
        max_frame_bytes: 1024,
        max_page_items: 1,
        ..hello()
    };
    assert!(!matches_hello(restricted, welcome()));
    assert!(!matches_hello(
        restricted,
        Handshake {
            kind: Kind::Welcome,
            max_frame_bytes: 1025,
            ..restricted
        }
    ));
    assert!(!matches_hello(
        restricted,
        Handshake {
            kind: Kind::Welcome,
            capabilities: 2,
            ..restricted
        }
    ));
    assert!(!matches_hello(
        restricted,
        Handshake {
            kind: Kind::Welcome,
            max_page_items: 2,
            ..restricted
        }
    ));
    let empty = Handshake {
        capabilities: 0,
        max_frame_bytes: 34,
        max_page_items: 1,
        ..hello()
    };
    assert_eq!(decode(&encode(empty).unwrap()).unwrap(), empty);
    assert!(matches_hello(
        empty,
        Handshake {
            kind: Kind::Welcome,
            ..empty
        }
    ));
    assert!(!matches_hello(empty, welcome()));
}
