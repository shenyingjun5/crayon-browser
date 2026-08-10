//! 流编码/封装识别：为每条可播候选补充「视频编码 + 音频编码 · 封装」标签，
//! 让调用方依据明确的 codec/container 证据评估接收端兼容性。
//!
//! 纯 Rust 解析，无 ffmpeg 依赖：
//! - m3u8 master：直接读 EXT-X-STREAM-INF 的 CODECS 属性；
//! - HLS 分片：TS 解析 PAT/PMT 的 stream_type；fMP4 走 box 树；
//! - MP4/M4S：box 树找到 moov/trak/mdia/stbl/stsd 的 codec fourcc。
//!
//! 全部解析函数为纯函数；网络获取由调用方通过有界适配层完成。

/// 识别结果：视频编码 / 音频编码 / 封装容器。
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CodecInfo {
    pub video: Option<String>,
    pub audio: Option<String>,
    /// TS / fMP4 / MP4 / DASH(fMP4) 等。
    pub container: Option<String>,
}

impl CodecInfo {
    /// UI 标签，如 `H.264+AAC · TS`、`HEVC+AAC · fMP4`；三无返回 None。
    pub fn label(&self) -> Option<String> {
        let mut parts: Vec<&str> = Vec::new();
        if let Some(v) = &self.video {
            parts.push(v);
        }
        if let Some(a) = &self.audio {
            parts.push(a);
        }
        let mut s = parts.join("+");
        if let Some(c) = &self.container {
            if !s.is_empty() {
                s.push(' ');
            }
            s.push_str(&format!("· {c}"));
        }
        if s.is_empty() {
            None
        } else {
            Some(s)
        }
    }
}

/// codec token → 通用名（avc1.640028 → H.264，hvc1 → HEVC …）。
pub fn codec_name(token: &str) -> String {
    let t = token.trim();
    let lower = t.to_ascii_lowercase();
    let base = lower.split('.').next().unwrap_or("");
    match base {
        "avc1" | "avc2" | "avc3" | "avc4" => "H.264".into(),
        "hvc1" | "hev1" => "HEVC".into(),
        "av01" => "AV1".into(),
        "vp09" | "vp9" => "VP9".into(),
        "vp08" | "vp8" => "VP8".into(),
        "mp4v" => "MPEG-4".into(),
        "mp4a" => "AAC".into(),
        "ac-3" | "ac3" => "AC-3".into(),
        "ec-3" | "ec3" => "E-AC-3".into(),
        "opus" => "Opus".into(),
        "flac" => "FLAC".into(),
        "mp3" => "MP3".into(),
        s if s.starts_with("dts") => "DTS".into(),
        _ => t.to_string(),
    }
}

/// token 是视频编码还是音频编码（m3u8 CODECS 分类用）。
fn is_video_token(token: &str) -> bool {
    let lower = token.trim().to_ascii_lowercase();
    let base = lower.split('.').next().unwrap_or("");
    matches!(
        base,
        "avc1"
            | "avc2"
            | "avc3"
            | "avc4"
            | "hvc1"
            | "hev1"
            | "av01"
            | "vp09"
            | "vp9"
            | "vp08"
            | "vp8"
            | "mp4v"
    )
}

/// 从 m3u8 文本提取全部 CODECS 属性，归并为 CodecInfo（容器另判）。
pub fn codecs_from_m3u8(text: &str) -> CodecInfo {
    let mut info = CodecInfo::default();
    let mut rest = text;
    while let Some(idx) = rest.find("CODECS=\"") {
        let after = &rest[idx + 8..];
        let end = after.find('"').unwrap_or(after.len());
        for token in after[..end].split(',') {
            let name = codec_name(token);
            if is_video_token(token) {
                if info.video.is_none() {
                    info.video = Some(name);
                }
            } else if info.audio.is_none() {
                info.audio = Some(name);
            }
        }
        rest = &after[end..];
    }
    info
}

/// m3u8 媒体播放列表的分片封装：EXT-X-MAP 或分片扩展名判断。
pub fn hls_container(text: &str) -> Option<&'static str> {
    if text.contains("#EXT-X-MAP:") {
        return Some("fMP4");
    }
    for line in text.lines() {
        let l = line.trim();
        if l.is_empty() || l.starts_with('#') {
            continue;
        }
        let path = l
            .split(['?', '#'])
            .next()
            .unwrap_or("")
            .to_ascii_lowercase();
        return Some(
            if path.ends_with(".m4s")
                || path.ends_with(".mp4")
                || path.ends_with(".cmfv")
                || path.ends_with(".cmfa")
            {
                "fMP4"
            } else {
                // .ts / 无扩展名 / 其它一律按 TS（HLS 默认）
                "TS"
            },
        );
    }
    None
}

/// 分片魔数判封装：0x47 同步字节 → TS；offset 4 为 ftyp → fMP4。
pub fn segment_container(bytes: &[u8]) -> Option<&'static str> {
    if bytes.first() == Some(&0x47) {
        return Some("TS");
    }
    if bytes.len() >= 8 && &bytes[4..8] == b"ftyp" {
        return Some("fMP4");
    }
    None
}

/// TS 流 stream_type → 编码名。
fn ts_stream_codec(stream_type: u8) -> Option<(&'static str, bool)> {
    // 返回 (编码名, 是否视频)
    Some(match stream_type {
        0x1b => ("H.264", true),
        0x24 | 0x42 => ("HEVC", true),
        0x10 => ("MPEG-4", true),
        0x01 | 0x02 => ("MPEG-2", true),
        0x0f | 0x11 => ("AAC", false),
        0x03 | 0x04 => ("MP3", false),
        0x81 | 0x84 => ("AC-3", false),
        0x87 => ("E-AC-3", false),
        0x06 => return None, // 私有数据（字幕/TELETEXT 等），不展示
        _ => return None,
    })
}

/// 解析 TS 缓冲的 PAT/PMT，提取编码信息（只需头部几十 KB）。
pub fn ts_codecs(buf: &[u8]) -> CodecInfo {
    let mut info = CodecInfo {
        container: Some("TS".into()),
        ..Default::default()
    };
    if buf.len() < 188 * 3 || buf[0] != 0x47 || buf[188] != 0x47 || buf[376] != 0x47 {
        return CodecInfo::default();
    }
    // 每个 PID 一个 section 重组器
    let mut pmt_pids: Vec<u16> = Vec::new();
    let mut pat_buf: Vec<u8> = Vec::new();
    let mut pat_need: Option<usize> = None;
    let mut pmt_buf: Vec<u8> = Vec::new();
    let mut pmt_need: Option<usize> = None;

    for pkt in buf.chunks_exact(188) {
        if pkt[0] != 0x47 {
            break;
        }
        let pusi = pkt[1] & 0x40 != 0;
        let pid = (((pkt[1] & 0x1f) as u16) << 8) | pkt[2] as u16;
        let afc = (pkt[3] >> 4) & 0x3;
        let mut off = 4;
        if afc == 0 || afc == 2 {
            continue; // 无 payload
        }
        if afc == 3 {
            off += 1 + pkt[4] as usize; // adaptation field
            if off >= 188 {
                continue;
            }
        }
        let mut payload = &pkt[off..];
        if pusi && !payload.is_empty() {
            let pointer = payload[0] as usize;
            payload = if 1 + pointer < payload.len() {
                &payload[1 + pointer..]
            } else {
                &[][..]
            };
        }
        let is_pat = pid == 0;
        let is_pmt = pmt_pids.contains(&pid);
        if !is_pat && !is_pmt {
            continue;
        }
        let (acc, need) = if is_pat {
            (&mut pat_buf, &mut pat_need)
        } else {
            (&mut pmt_buf, &mut pmt_need)
        };
        if pusi {
            acc.clear();
            *need = None;
        }
        acc.extend_from_slice(payload);
        if need.is_none() && acc.len() >= 3 {
            let sec_len = (((acc[1] & 0x0f) as usize) << 8) | acc[2] as usize;
            *need = Some(3 + sec_len);
        }
        if let Some(n) = *need {
            if acc.len() >= n {
                let section = acc[..n].to_vec();
                if is_pat {
                    // PAT：table_id 0x00，8 字节头后每个节目 4 字节
                    if section[0] == 0x00 && section.len() >= 12 {
                        let mut i = 8;
                        while i + 4 <= section.len().saturating_sub(4) {
                            let program = ((section[i] as u16) << 8) | section[i + 1] as u16;
                            let pmt_pid =
                                (((section[i + 2] & 0x1f) as u16) << 8) | section[i + 3] as u16;
                            if program != 0 && !pmt_pids.contains(&pmt_pid) {
                                pmt_pids.push(pmt_pid);
                            }
                            i += 4;
                        }
                    }
                } else {
                    // PMT：table_id 0x02，12 字节头 + program_info，然后流条目
                    if section[0] == 0x02 && section.len() >= 12 {
                        let pil = (((section[10] & 0x0f) as usize) << 8) | section[11] as usize;
                        let mut i = 12 + pil;
                        while i + 5 <= section.len().saturating_sub(4) {
                            let st = section[i];
                            let es_len =
                                (((section[i + 3] & 0x0f) as usize) << 8) | section[i + 4] as usize;
                            if let Some((name, is_video)) = ts_stream_codec(st) {
                                let slot = if is_video {
                                    &mut info.video
                                } else {
                                    &mut info.audio
                                };
                                if slot.is_none() {
                                    *slot = Some(name.to_string());
                                }
                            }
                            i += 5 + es_len;
                        }
                        if info.video.is_some() || info.audio.is_some() {
                            return info;
                        }
                    }
                }
                acc.clear();
                *need = None;
            }
        }
    }
    if info.video.is_none() && info.audio.is_none() {
        return CodecInfo {
            container: Some("TS".into()),
            ..Default::default()
        };
    }
    info
}

/// MP4/fMP4 box 树解析：从 moov/trak/mdia/stbl/stsd 取编码 fourcc，
/// hdlr 判音/视频轨。buf 只需覆盖文件头部（moov 前置时通常 ≤512KB）。
pub fn mp4_codecs(buf: &[u8]) -> CodecInfo {
    let mut info = CodecInfo::default();
    // ftyp 判容器
    if buf.len() >= 8 && &buf[4..8] == b"ftyp" {
        info.container = Some("MP4".into());
    } else {
        return info;
    }
    // 顶层找 moov
    for (typ, content) in top_boxes(buf) {
        if &typ == b"moov" {
            parse_moov(content, &mut info);
            break;
        }
    }
    info
}

/// 顶层 box 遍历：(类型, 内容切片)。size==0 表示到缓冲末尾。
fn top_boxes(mut buf: &[u8]) -> Vec<([u8; 4], &[u8])> {
    let mut out = Vec::new();
    while buf.len() >= 8 {
        let size = u32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]) as usize;
        let typ = [buf[4], buf[5], buf[6], buf[7]];
        let (hdr, total) = if size == 1 {
            if buf.len() < 16 {
                break;
            }
            let large = u64::from_be_bytes([
                buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15],
            ]) as usize;
            (16, large)
        } else if size == 0 {
            (8, buf.len())
        } else {
            (8, size)
        };
        if total < hdr || total > buf.len() {
            // 缓冲截断：moov 未完整到达，内容部分仍尽量用
            if total > buf.len() && hdr < buf.len() {
                out.push((typ, &buf[hdr..]));
            }
            break;
        }
        out.push((typ, &buf[hdr..total]));
        buf = &buf[total..];
    }
    out
}

fn parse_moov(buf: &[u8], info: &mut CodecInfo) {
    for (typ, content) in top_boxes(buf) {
        if &typ == b"trak" {
            parse_trak(content, info);
        }
    }
}

fn parse_trak(buf: &[u8], info: &mut CodecInfo) {
    for (typ, content) in top_boxes(buf) {
        if &typ == b"mdia" {
            let mut is_video = None;
            let mut codecs: Vec<String> = Vec::new();
            for (t, c) in top_boxes(content) {
                if &t == b"hdlr" {
                    // full box：4 版本/标志 + 4 pre_defined + 4 handler_type
                    if c.len() >= 12 {
                        let ht = &c[8..12];
                        is_video = Some(if ht == b"vide" {
                            true
                        } else if ht == b"soun" {
                            false
                        } else {
                            return; // hint/meta 等轨道忽略
                        });
                    }
                } else if &t == b"minf" {
                    find_stsd(c, &mut codecs);
                }
            }
            for name in codecs {
                match is_video {
                    Some(true) if info.video.is_none() => info.video = Some(name),
                    Some(false) if info.audio.is_none() => info.audio = Some(name),
                    _ => {}
                }
            }
        }
    }
}

/// minf → stbl → stsd：收集所有 entry 的编码 fourcc 通用名。
fn find_stsd(buf: &[u8], out: &mut Vec<String>) {
    for (typ, content) in top_boxes(buf) {
        if &typ == b"stbl" {
            for (t, c) in top_boxes(content) {
                if &t == b"stsd" && c.len() >= 8 {
                    let count = u32::from_be_bytes([c[4], c[5], c[6], c[7]]) as usize;
                    let mut entries = &c[8..];
                    for _ in 0..count {
                        if entries.len() < 8 {
                            break;
                        }
                        let size =
                            u32::from_be_bytes([entries[0], entries[1], entries[2], entries[3]])
                                as usize;
                        if size < 8 || size > entries.len() {
                            break;
                        }
                        let fourcc = std::str::from_utf8(&entries[4..8]).unwrap_or("");
                        out.push(codec_name(fourcc));
                        entries = &entries[size..];
                    }
                }
            }
        }
    }
}

#[cfg(test)]
#[path = "codec_tests.rs"]
mod tests;
