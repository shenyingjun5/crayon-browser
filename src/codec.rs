//! 流编码/封装识别：为每条可播候选补充「视频编码 + 音频编码 · 封装」标签，
//! 让投屏接收端一眼判断自身兼容性（典型场景：HEVC 流需要接收端支持 H.265，
//! WebKit webview 播不出画面但 ExoPlayer/VLC 正常）。
//!
//! 纯 Rust 解析，无 ffmpeg 依赖：
//! - m3u8 master：直接读 EXT-X-STREAM-INF 的 CODECS 属性；
//! - HLS 分片：TS 解析 PAT/PMT 的 stream_type；fMP4 走 box 树；
//! - MP4/M4S：box 树找到 moov/trak/mdia/stbl/stsd 的 codec fourcc。
//!
//! 全部解析函数为纯函数（可单测）；网络拉取只取文件头部小片段（Range）。

use crate::extract::Protocol;
use futures_util::StreamExt;
use std::collections::HashMap;

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

/// 拉取 URL 头部最多 `max` 字节（Range 请求；上游忽略 Range 时读到 max 即断流）。
async fn fetch_prefix(
    client: &reqwest::Client,
    url: &str,
    headers: &HashMap<String, String>,
    max: usize,
) -> Option<Vec<u8>> {
    let mut req = client
        .get(url)
        .header(reqwest::header::RANGE, format!("bytes=0-{}", max - 1));
    for (k, v) in headers {
        req = req.header(k, v);
    }
    let resp = req.send().await.ok()?;
    if !resp.status().is_success() && resp.status().as_u16() != 206 {
        return None;
    }
    let mut stream = resp.bytes_stream();
    let mut buf: Vec<u8> = Vec::new();
    while let Some(chunk) = stream.next().await {
        let c = chunk.ok()?;
        buf.extend_from_slice(&c);
        if buf.len() >= max {
            break;
        }
    }
    buf.truncate(max);
    if buf.is_empty() {
        None
    } else {
        Some(buf)
    }
}

/// 单条流的编码识别入口：按协议分发，返回 UI 标签。
/// 失败/无法识别一律 None（宁缺毋滥，不阻塞结果展示）。
pub async fn inspect(
    client: &reqwest::Client,
    url: &str,
    protocol: Protocol,
    headers: &HashMap<String, String>,
) -> Option<String> {
    match protocol {
        Protocol::Hls => inspect_hls(client, url, headers).await,
        Protocol::Mp4 => {
            let buf = fetch_prefix(client, url, headers, 512 * 1024).await?;
            mp4_codecs(&buf).label()
        }
        _ => None,
    }
}

/// DASH 音画双轨（B 站）：视频轨取视频编码、音频轨取音频编码。
pub async fn inspect_dash_dual(
    client: &reqwest::Client,
    video_url: &str,
    audio_url: &str,
    headers: &HashMap<String, String>,
) -> Option<String> {
    let vbuf = fetch_prefix(client, video_url, headers, 256 * 1024).await?;
    let abuf = fetch_prefix(client, audio_url, headers, 256 * 1024).await;
    let vinfo = mp4_codecs(&vbuf);
    let ainfo = abuf.as_ref().map(|b| mp4_codecs(b)).unwrap_or_default();
    let info = CodecInfo {
        video: vinfo.video,
        audio: ainfo.audio.or(vinfo.audio),
        container: Some("DASH(fMP4)".into()),
    };
    info.label()
}

async fn inspect_hls(
    client: &reqwest::Client,
    url: &str,
    headers: &HashMap<String, String>,
) -> Option<String> {
    let text_bytes = fetch_prefix(client, url, headers, 1024 * 1024).await?;
    let text = String::from_utf8_lossy(&text_bytes);
    if !text.trim_start().starts_with("#EXTM3U") {
        return None;
    }
    let mut info = codecs_from_m3u8(&text);
    // master：下钻第一个变体拿媒体播放列表；media：直接用
    let media_text;
    let media_url;
    if text.contains("#EXT-X-STREAM-INF") {
        let variant = first_media_line(&text, url)?;
        media_url = variant;
        let bytes = fetch_prefix(client, &media_url, headers, 1024 * 1024).await?;
        media_text = String::from_utf8_lossy(&bytes).into_owned();
    } else {
        media_url = url.to_string();
        media_text = text.into_owned();
    }
    // 容器：播放列表特征优先，不确定时抓首个分片看魔数
    info.container = hls_container(&media_text).map(str::to_string);
    let need_segment = info.video.is_none() && info.audio.is_none();
    if need_segment {
        // fMP4：moov（含 stsd）在 EXT-X-MAP 的 init 段里，媒体分片（m4s）没有，
        // 优先解析 init 段；没有 EXT-X-MAP 才回退抓首个媒体分片（TS 走 PMT）
        let init_url =
            ext_x_map_uri(&media_text).and_then(|u| crate::extract::resolve_url(&media_url, &u));
        let seg_url = match init_url {
            Some(u) => Some(u),
            None => media_text
                .lines()
                .map(str::trim)
                .find(|l| !l.is_empty() && !l.starts_with('#'))
                .and_then(|l| crate::extract::resolve_url(&media_url, l)),
        };
        let seg_url = seg_url?;
        let buf = fetch_prefix(client, &seg_url, headers, 256 * 1024).await?;
        let seg_info = match segment_container(&buf) {
            Some("TS") => ts_codecs(&buf),
            Some("fMP4") => mp4_codecs(&buf),
            _ => CodecInfo::default(),
        };
        if seg_info.video.is_some() || seg_info.audio.is_some() {
            info.video = seg_info.video;
            info.audio = seg_info.audio;
            // 容器以分片魔数为准（HLS 的 MP4 一律是 fMP4；init 段的
            // mp4_codecs 会报 MP4，这里纠正回 fMP4）
            if let Some(c) = segment_container(&buf) {
                info.container = Some(c.to_string());
            }
        }
    }
    info.label()
}

/// master 播放列表里第一个变体（EXT-X-STREAM-INF 下一行）的绝对地址。
fn first_media_line(text: &str, base: &str) -> Option<String> {
    let mut want_next = false;
    for line in text.lines() {
        let l = line.trim();
        if want_next && !l.is_empty() && !l.starts_with('#') {
            return crate::extract::resolve_url(base, l);
        }
        want_next = l.starts_with("#EXT-X-STREAM-INF");
    }
    None
}

/// 媒体播放列表 EXT-X-MAP 的 URI（fMP4 init 段地址）。
fn ext_x_map_uri(text: &str) -> Option<String> {
    for line in text.lines() {
        let l = line.trim();
        if let Some(rest) = l.strip_prefix("#EXT-X-MAP:") {
            if let Some(idx) = rest.find("URI=\"") {
                let after = &rest[idx + 5..];
                let end = after.find('"').unwrap_or(after.len());
                return Some(after[..end].to_string());
            }
        }
    }
    None
}

#[cfg(test)]
mod tests {
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
            0x00u8, 0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xe1, 0x00, 0, 0,
            0, 0,
        ];
        let pmt = [
            0x00u8, 0x02, 0xb0, 0x12, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1, 0x01, 0xf0, 0x00, 0x24,
            0xe1, 0x01, 0xf0, 0x00, // HEVC
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

    #[test]
    fn master_first_variant() {
        let text = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nsub/v1.m3u8\n";
        assert_eq!(
            first_media_line(text, "http://a.com/live/master.m3u8").as_deref(),
            Some("http://a.com/live/sub/v1.m3u8")
        );
    }

    #[test]
    fn ext_x_map_uri_parse() {
        let text = "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4?sign=abc\",BYTERANGE=\"720@0\"\n#EXTINF:5,\ns1.m4s\n";
        assert_eq!(ext_x_map_uri(text).as_deref(), Some("init.mp4?sign=abc"));
        assert_eq!(ext_x_map_uri("#EXTM3U\n#EXTINF:5,\ns1.ts\n"), None);
    }
}
