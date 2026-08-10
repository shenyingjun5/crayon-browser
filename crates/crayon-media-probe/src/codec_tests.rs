use super::*;

#[test]
fn codec_name_mapping() {
    assert_eq!(codec_name("avc1.640028"), "H.264");
    assert_eq!(codec_name("hvc1.1.6.L93.B0"), "HEVC");
    assert_eq!(codec_name("hev1"), "HEVC");
    assert_eq!(codec_name("av01.0.08M.08"), "AV1");
    assert_eq!(codec_name("vp09.00.10.08"), "VP9");
    assert_eq!(codec_name("mp4a.40.2"), "AAC");
    assert_eq!(codec_name("ec-3"), "E-AC-3");
    assert_eq!(codec_name("unknown-thing"), "unknown-thing");
}

#[test]
fn m3u8_master_codecs() {
    let text = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000,CODECS=\"avc1.640028,mp4a.40.2\"\nv1/index.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=2000,CODECS=\"hvc1.1.6.L93.B0,mp4a.40.2\"\nv2/index.m3u8\n";
    let info = codecs_from_m3u8(text);
    assert_eq!(info.video.as_deref(), Some("H.264"));
    assert_eq!(info.audio.as_deref(), Some("AAC"));
}

#[test]
fn m3u8_hevc_master() {
    let text = "#EXTM3U\n#EXT-X-STREAM-INF:CODECS=\"hvc1.1.6.L93.B0,mp4a.40.5\"\nv.m3u8\n";
    let info = codecs_from_m3u8(text);
    assert_eq!(info.video.as_deref(), Some("HEVC"));
    assert_eq!(info.audio.as_deref(), Some("AAC"));
}

#[test]
fn hls_container_detect() {
    assert_eq!(
        hls_container("#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:5,\ns1.m4s\n"),
        Some("fMP4")
    );
    assert_eq!(hls_container("#EXTM3U\n#EXTINF:5,\ns1.ts\n"), Some("TS"));
    assert_eq!(hls_container("#EXTM3U\n#EXTINF:5,\nsegment1\n"), Some("TS"));
    assert_eq!(hls_container("#EXTM3U\n#EXTINF:5,\n"), None);
}

/// 构造一个 TS 包。
fn ts_packet(pid: u16, pusi: bool, payload: &[u8]) -> Vec<u8> {
    let mut pkt = vec![0u8; 188];
    pkt[0] = 0x47;
    pkt[1] = if pusi { 0x40 } else { 0x00 } | ((pid >> 8) as u8 & 0x1f);
    pkt[2] = pid as u8;
    pkt[3] = 0x10; // payload only
    let n = payload.len().min(184);
    pkt[4..4 + n].copy_from_slice(&payload[..n]);
    for b in &mut pkt[4 + n..] {
        *b = 0xff;
    }
    pkt
}

#[test]
fn ts_pat_pmt_parse() {
    // PAT：program 1 → PMT pid 0x100
    let mut pat = vec![0x00]; // pointer
    pat.extend_from_slice(&[
        0x00, 0xb0, 0x0d, // table_id + section_length 13
        0x00, 0x01, 0xc1, 0x00, 0x00, // tsid, ver, sec, last
        0x00, 0x01, 0xe1, 0x00, // program 1 → pid 0x100
        0x00, 0x00, 0x00, 0x00, // CRC
    ]);
    // PMT：H.264 视频 + AAC 音频
    let mut pmt = vec![0x00];
    pmt.extend_from_slice(&[
        0x02, 0xb0, 0x17, // table_id + section_length 23
        0x00, 0x01, 0xc1, 0x00, 0x00, // program, ver, sec, last
        0xe1, 0x01, // pcr pid
        0xf0, 0x00, // program_info_length 0
        0x1b, 0xe1, 0x01, 0xf0, 0x00, // H.264, pid 0x101
        0x0f, 0xe1, 0x02, 0xf0, 0x00, // AAC, pid 0x102
        0x00, 0x00, 0x00, 0x00, // CRC
    ]);
    let mut buf = ts_packet(0, true, &pat);
    buf.extend_from_slice(&ts_packet(0x100, true, &pmt));
    buf.extend_from_slice(&ts_packet(0x101, false, &[0u8; 100]));
    let info = ts_codecs(&buf);
    assert_eq!(info.video.as_deref(), Some("H.264"));
    assert_eq!(info.audio.as_deref(), Some("AAC"));
    assert_eq!(info.container.as_deref(), Some("TS"));
}

#[test]
fn ts_hevc() {
    let pat = [
        0x00u8, 0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xe1, 0x00, 0, 0, 0, 0,
    ];
    let pmt = [
        0x00u8, 0x02, 0xb0, 0x12, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1, 0x01, 0xf0, 0x00, 0x24, 0xe1,
        0x01, 0xf0, 0x00, // HEVC
        0, 0, 0, 0,
    ];
    let mut buf = ts_packet(0, true, &pat);
    buf.extend_from_slice(&ts_packet(0x100, true, &pmt));
    buf.extend_from_slice(&ts_packet(1, false, &[0u8; 50]));
    let info = ts_codecs(&buf);
    assert_eq!(info.video.as_deref(), Some("HEVC"));
}

#[test]
fn ts_invalid_sync() {
    let buf = vec![0u8; 188 * 4];
    assert_eq!(ts_codecs(&buf), CodecInfo::default());
}

/// 构造 box：size + type + content。
fn bx(typ: &[u8; 4], content: &[u8]) -> Vec<u8> {
    let mut v = ((content.len() + 8) as u32).to_be_bytes().to_vec();
    v.extend_from_slice(typ);
    v.extend_from_slice(content);
    v
}

#[test]
fn mp4_box_parse() {
    let ftyp = bx(b"ftyp", b"isom\x00\x00\x00\x01isom");
    // stsd：version/flags + count=1 + entry(size+fourcc+28 字节占位)
    let mut avc_entry = Vec::new();
    avc_entry.extend_from_slice(&36u32.to_be_bytes());
    avc_entry.extend_from_slice(b"avc1");
    avc_entry.extend_from_slice(&[0u8; 28]);
    let mut stsd_c = vec![0, 0, 0, 0];
    stsd_c.extend_from_slice(&1u32.to_be_bytes());
    stsd_c.extend_from_slice(&avc_entry);
    let stsd = bx(b"stsd", &stsd_c);
    let stbl = bx(b"stbl", &stsd);
    let minf = bx(b"minf", &stbl);
    let hdlr = bx(b"hdlr", &[0, 0, 0, 0, 0, 0, 0, 0, b'v', b'i', b'd', b'e']);
    let mut mdia_c = hdlr;
    mdia_c.extend_from_slice(&minf);
    let mdia = bx(b"mdia", &mdia_c);
    let trak = bx(b"trak", &mdia);
    // 音频轨
    let mut mp4a_entry = Vec::new();
    mp4a_entry.extend_from_slice(&36u32.to_be_bytes());
    mp4a_entry.extend_from_slice(b"mp4a");
    mp4a_entry.extend_from_slice(&[0u8; 28]);
    let mut stsd_a = vec![0, 0, 0, 0];
    stsd_a.extend_from_slice(&1u32.to_be_bytes());
    stsd_a.extend_from_slice(&mp4a_entry);
    let minf_a = bx(b"minf", &bx(b"stbl", &bx(b"stsd", &stsd_a)));
    let hdlr_a = bx(b"hdlr", &[0, 0, 0, 0, 0, 0, 0, 0, b's', b'o', b'u', b'n']);
    let mut mdia_ac = hdlr_a;
    mdia_ac.extend_from_slice(&minf_a);
    let trak_a = bx(b"trak", &bx(b"mdia", &mdia_ac));
    let mut moov_c = trak;
    moov_c.extend_from_slice(&trak_a);
    let moov = bx(b"moov", &moov_c);
    let mut buf = ftyp;
    buf.extend_from_slice(&moov);
    let info = mp4_codecs(&buf);
    assert_eq!(info.video.as_deref(), Some("H.264"));
    assert_eq!(info.audio.as_deref(), Some("AAC"));
    assert_eq!(info.container.as_deref(), Some("MP4"));
    assert_eq!(info.label().as_deref(), Some("H.264+AAC · MP4"));
}

#[test]
fn mp4_truncated_moov_still_parses_stsd() {
    // moov size 声明超buffer（截断）时仍能读出已到的 stsd
    let ftyp = bx(b"ftyp", b"isom\x00\x00\x00\x01isom");
    let mut avc_entry = Vec::new();
    avc_entry.extend_from_slice(&36u32.to_be_bytes());
    avc_entry.extend_from_slice(b"hvc1");
    avc_entry.extend_from_slice(&[0u8; 28]);
    let mut stsd_c = vec![0, 0, 0, 0];
    stsd_c.extend_from_slice(&1u32.to_be_bytes());
    stsd_c.extend_from_slice(&avc_entry);
    let stsd = bx(b"stsd", &stsd_c);
    let stbl = bx(b"stbl", &stsd);
    let minf = bx(b"minf", &stbl);
    let hdlr = bx(b"hdlr", &[0, 0, 0, 0, 0, 0, 0, 0, b'v', b'i', b'd', b'e']);
    let mut mdia_c = hdlr;
    mdia_c.extend_from_slice(&minf);
    let mdia = bx(b"mdia", &mdia_c);
    let trak = bx(b"trak", &mdia);
    // moov 头声明一个超大 size，内容只有 trak
    let mut moov = (trak.len() as u32 + 8 + 999_999).to_be_bytes().to_vec();
    moov.extend_from_slice(b"moov");
    moov.extend_from_slice(&trak);
    let mut buf = ftyp;
    buf.extend_from_slice(&moov);
    let info = mp4_codecs(&buf);
    assert_eq!(info.video.as_deref(), Some("HEVC"));
}

#[test]
fn label_format() {
    let info = CodecInfo {
        video: Some("HEVC".into()),
        audio: Some("AAC".into()),
        container: Some("TS".into()),
    };
    assert_eq!(info.label().as_deref(), Some("HEVC+AAC · TS"));
    let only_container = CodecInfo {
        container: Some("MP4".into()),
        ..Default::default()
    };
    assert_eq!(only_container.label().as_deref(), Some("· MP4"));
    assert_eq!(CodecInfo::default().label(), None);
}
