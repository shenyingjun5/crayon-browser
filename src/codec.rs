//! Legacy HTTP codec inspection built on the formal pure parser crate.

use crate::extract::Protocol;
use futures_util::StreamExt;
use std::collections::HashMap;

pub use crayon_media_probe::{
    codec_name, codecs_from_m3u8, hls_container, mp4_codecs, segment_container, ts_codecs,
    CodecInfo,
};

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
        let line = line.trim();
        if want_next && !line.is_empty() && !line.starts_with('#') {
            return crate::extract::resolve_url(base, line);
        }
        want_next = line.starts_with("#EXT-X-STREAM-INF");
    }
    None
}

/// 媒体播放列表 EXT-X-MAP 的 URI（fMP4 init 段地址）。
fn ext_x_map_uri(text: &str) -> Option<String> {
    for line in text.lines() {
        let line = line.trim();
        if let Some(rest) = line.strip_prefix("#EXT-X-MAP:") {
            if let Some(index) = rest.find("URI=\"") {
                let after = &rest[index + 5..];
                let end = after.find('"').unwrap_or(after.len());
                return Some(after[..end].to_string());
            }
        }
    }
    None
}

#[cfg(test)]
#[path = "codec_tests.rs"]
mod tests;
