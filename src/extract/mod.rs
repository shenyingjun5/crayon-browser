//! 提取编排：L1 静态解析 + L3 规则包 + DRM 检测 + 结果归一。
//! （L2 webview 嗅探需 Tauri GUI 环境，本轮不实现，见 README。）

mod rules;
pub mod sites;
mod static_parse;

pub use rules::{RuleMatch, RulePack, SiteRule};
pub use sites::SiteResult;
pub use static_parse::{
    guess_quality, parse_html, resolve_url, Candidate, Protocol, StaticParseResult,
};

use serde::Serialize;
use std::collections::HashMap;
use std::time::Duration;

/// 单个视频流格式（对齐 docs/design.md §3 统一返回结构）。
#[derive(Debug, Clone, Serialize)]
pub struct Format {
    pub url: String,
    pub protocol: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub quality: Option<String>,
    pub drm: bool,
    /// 受限原因（WASM 私有加扰 / 全站 DRM）：命中即不可播，不产出 relay 地址。
    #[serde(skip_serializing_if = "Option::is_none")]
    pub restriction: Option<String>,
    pub headers: HashMap<String, String>,
    /// DRM 内容不产出 relay 地址（docs/test-cases.md D1）。
    #[serde(skip_serializing_if = "Option::is_none")]
    pub relay_url: Option<String>,
}

/// 提取结果。
#[derive(Debug, Clone, Serialize)]
pub struct VideoInfo {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    pub webpage: String,
    pub source: String,
    pub formats: Vec<Format>,
    /// 附加说明（如命中已知 DRM 站点名单）。
    #[serde(skip_serializing_if = "Option::is_none")]
    pub note: Option<String>,
}

pub struct Extractor {
    client: reqwest::Client,
    rules: RulePack,
    /// 生成 relay_url 用的基地址，如 `http://127.0.0.1:8321`。
    relay_base: String,
    /// DASH MPD 内存仓库（与 relay 共享）；独立使用时为空仓库。
    dash_store: crate::relay::DashStore,
}

impl Extractor {
    pub fn new(relay_base: &str, rules: RulePack) -> Self {
        let client = reqwest::Client::builder()
            .user_agent(crate::DEFAULT_UA)
            .timeout(Duration::from_secs(10))
            .redirect(reqwest::redirect::Policy::limited(5))
            .build()
            .expect("reqwest client build");
        Self {
            client,
            rules,
            relay_base: relay_base.trim_end_matches('/').to_string(),
            dash_store: Default::default(),
        }
    }

    /// 注入与 relay 共享的 MPD 仓库（应用启动时调用），
    /// 之后 DASH 双轨候选的 relay_url 指向 `/dashmpd/{id}`。
    pub fn set_dash_store(&mut self, store: crate::relay::DashStore) {
        self.dash_store = store;
    }

    /// 提取入口：网页 URL → 统一视频流列表。
    pub async fn extract(&self, page_url: &str) -> Result<VideoInfo, String> {
        self.extract_with_cookie(page_url, None).await
    }

    /// 带站点登录态的提取：`cookie` 为「name=value; ...」形式的 Cookie 头，
    /// 目前仅传给 B 站解析器（解锁更高清晰度）。
    pub async fn extract_with_cookie(
        &self,
        page_url: &str,
        cookie: Option<&str>,
    ) -> Result<VideoInfo, String> {
        // 已知 DRM 站点前置标记
        if crate::drm::is_known_drm_site(page_url) {
            return Ok(VideoInfo {
                title: None,
                webpage: page_url.to_string(),
                source: "static".into(),
                formats: vec![],
                note: Some("命中已知 DRM 站点名单，按 DRM 拒播处理".into()),
            });
        }

        let resp = self
            .client
            .get(page_url)
            .send()
            .await
            .map_err(|e| format!("拉取页面失败: {e}"))?;
        let final_url = resp.url().to_string();
        let html = resp
            .text()
            .await
            .map_err(|e| format!("读取页面失败: {e}"))?;

        let parsed = static_parse::parse_html(&html, &final_url);
        let page_origin = origin_of(&final_url);

        let mut formats: Vec<Format> = Vec::new();
        let mut seen: HashMap<String, ()> = HashMap::new();

        // L1 候选
        for cand in &parsed.candidates {
            if seen.insert(cand.url.clone(), ()).is_some() {
                continue;
            }
            let mut headers = HashMap::new();
            headers.insert("Referer".to_string(), page_origin.clone());
            headers.insert("User-Agent".to_string(), crate::DEFAULT_UA.to_string());
            formats.push(self.build_format(&final_url, cand, &headers).await);
        }

        // L3 规则包
        let mut rule_hit = false;
        for m in self.rules.apply(&final_url, &html) {
            rule_hit = true;
            if seen.contains_key(&m.url) {
                continue;
            }
            if static_parse::Protocol::from_url(&m.url) == Protocol::Other {
                continue;
            }
            seen.insert(m.url.clone(), ());
            let cand = Candidate::single(
                m.url.clone(),
                static_parse::Protocol::from_url(&m.url),
                static_parse::guess_quality(&m.url),
            );
            let mut headers = HashMap::new();
            headers.insert(
                "Referer".to_string(),
                m.referer.clone().unwrap_or_else(|| page_origin.clone()),
            );
            headers.insert(
                "User-Agent".to_string(),
                m.ua.clone()
                    .unwrap_or_else(|| crate::DEFAULT_UA.to_string()),
            );
            formats.push(self.build_format(&final_url, &cand, &headers).await);
        }

        // 站点专用解析器（首批：央视网 tv.cctv.com / cntv、B 站番剧，见 sites.rs）
        let mut site_hit = false;
        let mut site_title: Option<String> = None;
        let mut site_note: Option<String> = None;
        if let Some(site) = sites::extract(&self.client, &final_url, &html, cookie).await {
            site_hit = !site.candidates.is_empty();
            site_title = site.title;
            site_note = site.note;
            for cand in &site.candidates {
                if seen.contains_key(&cand.url) {
                    continue;
                }
                seen.insert(cand.url.clone(), ());
                let mut headers = HashMap::new();
                headers.insert(
                    "Referer".to_string(),
                    site.referer.clone().unwrap_or_else(|| page_origin.clone()),
                );
                headers.insert("User-Agent".to_string(), crate::DEFAULT_UA.to_string());
                formats.push(self.build_format(&final_url, cand, &headers).await);
            }
        }

        Ok(VideoInfo {
            title: parsed.title.or(site_title),
            webpage: page_url.to_string(),
            source: if rule_hit || site_hit {
                "site-api".into()
            } else {
                "static".into()
            },
            formats,
            note: site_note,
        })
    }

    async fn build_format(
        &self,
        page_url: &str,
        cand: &Candidate,
        headers: &HashMap<String, String>,
    ) -> Format {
        // DASH 音画分轨合成候选：上游（B 站）已过滤 DRM，跳过检测，
        // 生成 MPD 写入共享仓库，relay_url 指向 /dashmpd/{id}
        if let Some(audio) = &cand.audio_url {
            let v_proxy = self.relay_url(&cand.url, headers);
            let a_proxy = self.relay_url(audio, headers);
            let id = dash_doc_id(&cand.url, audio);
            let xml = build_mpd(cand, &v_proxy, &a_proxy);
            self.dash_store.lock().unwrap().insert(id.clone(), xml);
            return Format {
                url: cand.url.clone(),
                protocol: cand.protocol.as_str().to_string(),
                quality: cand.quality.map(|q| format!("{q}p")),
                drm: false,
                restriction: None,
                headers: headers.clone(),
                relay_url: Some(format!("{}/dashmpd/{id}", self.relay_base)),
            };
        }
        // 受限站点（WASM 私有加扰 / 全站 DRM）：直接打标，不再拉流检测
        let restriction = crate::drm::restricted_reason(page_url, &cand.url).map(str::to_string);
        let drm = if restriction.is_some() {
            false
        } else {
            self.detect_drm(cand, headers).await
        };
        let relay_url = if drm || restriction.is_some() {
            None
        } else {
            Some(self.relay_url(&cand.url, headers))
        };
        Format {
            url: cand.url.clone(),
            protocol: cand.protocol.as_str().to_string(),
            quality: cand.quality.map(|q| format!("{q}p")),
            drm,
            restriction,
            headers: headers.clone(),
            relay_url,
        }
    }

    /// DRM 检测：HLS 拉取播放列表（master 时再下一层），DASH 拉取 mpd。
    /// 拉取失败时不阻塞提取，按非 DRM 处理。
    /// （pub：供 Tauri demo 的 L2 webview 嗅探流程复用）
    pub async fn detect_drm(&self, cand: &Candidate, headers: &HashMap<String, String>) -> bool {
        match cand.protocol {
            Protocol::Hls => {
                let Some(text) = self.fetch_text(&cand.url, headers).await else {
                    return false;
                };
                if crate::drm::hls_is_drm(&text) {
                    return true;
                }
                // master 列表：取第一个子列表再检测一层
                if text.contains("#EXT-X-STREAM-INF") {
                    if let Some(variant) = first_variant_url(&text, &cand.url) {
                        if let Some(sub) = self.fetch_text(&variant, headers).await {
                            return crate::drm::hls_is_drm(&sub);
                        }
                    }
                }
                false
            }
            Protocol::Dash => self
                .fetch_text(&cand.url, headers)
                .await
                .map(|t| crate::drm::mpd_is_drm(&t))
                .unwrap_or(false),
            _ => false,
        }
    }

    async fn fetch_text(&self, url: &str, headers: &HashMap<String, String>) -> Option<String> {
        let mut req = self.client.get(url);
        for (k, v) in headers {
            req = req.header(k, v);
        }
        let resp = req.send().await.ok()?;
        resp.text().await.ok()
    }

    /// 拼接 relay 地址：/proxy/<编码URL>/<文件名>?referer=...&ua=...
    /// （文件名后缀为装饰性路径，兼容对 URL 扩展名挑剔的播放器）
    pub fn relay_url(&self, target: &str, headers: &HashMap<String, String>) -> String {
        let mut u = format!(
            "{}/proxy/{}",
            self.relay_base,
            crate::encode_url_component(target)
        );
        if let Ok(p) = url::Url::parse(target) {
            if let Some(name) = p.path().rsplit('/').next() {
                if !name.is_empty()
                    && name
                        .chars()
                        .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-'))
                {
                    u.push('/');
                    u.push_str(name);
                }
            }
        }
        let mut qs = vec![];
        if let Some(r) = headers.get("Referer") {
            qs.push(format!("referer={}", crate::encode_url_component(r)));
        }
        if let Some(ua) = headers.get("User-Agent") {
            if ua != crate::DEFAULT_UA {
                qs.push(format!("ua={}", crate::encode_url_component(ua)));
            }
        }
        if !qs.is_empty() {
            u.push('?');
            u.push_str(&qs.join("&"));
        }
        u
    }
}

/// 取 URL 的 origin（scheme://host[:port]）。
pub fn origin_of(url: &str) -> String {
    url::Url::parse(url)
        .ok()
        .map(|u| u.origin().ascii_serialization())
        .unwrap_or_default()
}

/// 从 master 播放列表里取第一个子列表的绝对地址。
fn first_variant_url(master_text: &str, base: &str) -> Option<String> {
    let mut lines = master_text.lines().map(|l| l.trim());
    while let Some(line) = lines.next() {
        if line.starts_with("#EXT-X-STREAM-INF") {
            for next in lines.by_ref() {
                if next.is_empty() || next.starts_with('#') {
                    continue;
                }
                return static_parse::resolve_url(base, next);
            }
        }
    }
    None
}

/// DASH 文档 id：视频轨+音频轨地址的稳定散列（进程内有效即可）。
fn dash_doc_id(v_url: &str, a_url: &str) -> String {
    use std::hash::{Hash, Hasher};
    let mut h = std::collections::hash_map::DefaultHasher::new();
    v_url.hash(&mut h);
    a_url.hash(&mut h);
    format!("{:016x}", h.finish())
}

/// XML 转义（MPD 的 BaseURL 里 query 参数含 `&`）。
fn xml_escape(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}

/// 由 DASH 双轨候选生成 MPD 清单（on-demand 单文件 fMP4 profile，
/// B 站 m4s 自带 init 段与 sidx，播放器经 Range 自行定位分片）。
fn build_mpd(cand: &Candidate, v_proxy: &str, a_proxy: &str) -> String {
    let dur_attr = cand
        .duration_ms
        .map(|ms| format!("PT{}.{:03}S", ms / 1000, ms % 1000));
    let dur_mpd = dur_attr
        .as_deref()
        .map(|d| format!(" mediaPresentationDuration=\"{d}\""))
        .unwrap_or_default();
    let dur_period = dur_attr
        .as_deref()
        .map(|d| format!(" duration=\"{d}\""))
        .unwrap_or_default();
    let v_codecs = cand.codecs.clone().unwrap_or_else(|| "avc1.640028".into());
    let w = cand.width.unwrap_or(1920);
    let h = cand.height.unwrap_or(1080);
    let vbw = cand.bandwidth.unwrap_or(2_000_000);
    let (v_proxy, a_proxy) = (xml_escape(v_proxy), xml_escape(a_proxy));
    format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" minBufferTime="PT2S" profiles="urn:mpeg:dash:profile:isoff-on-demand:2011"{dur_mpd}>
  <Period{dur_period}>
    <AdaptationSet mimeType="video/mp4" segmentAlignment="true" startWithSAP="1">
      <Representation id="v" codecs="{v_codecs}" width="{w}" height="{h}" bandwidth="{vbw}">
        <BaseURL>{v_proxy}</BaseURL>
      </Representation>
    </AdaptationSet>
    <AdaptationSet mimeType="audio/mp4" segmentAlignment="true" startWithSAP="1">
      <Representation id="a" codecs="mp4a.40.2" bandwidth="192000">
        <BaseURL>{a_proxy}</BaseURL>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>"#
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn cctv_candidate_marked_restricted() {
        let ex = Extractor::new("http://127.0.0.1:8321", RulePack::empty());
        let cand = Candidate::single(
            "https://hls.cntv.lxdns.com/asp/hls/main.m3u8".into(),
            Protocol::Hls,
            None,
        );
        let f = ex
            .build_format(
                "https://tv.cctv.com/2026/07/30/VIDE.shtml",
                &cand,
                &HashMap::new(),
            )
            .await;
        assert!(f.restriction.is_some(), "央视页面应标记受限");
        assert!(f.relay_url.is_none(), "受限内容不产出 relay 地址");
        assert!(!f.drm);
    }

    #[tokio::test]
    async fn normal_candidate_not_restricted() {
        let ex = Extractor::new("http://127.0.0.1:8321", RulePack::empty());
        let cand = Candidate::single("https://example.com/v.mp4".into(), Protocol::Mp4, None);
        let f = ex
            .build_format("https://example.com/p", &cand, &HashMap::new())
            .await;
        assert!(f.restriction.is_none());
        assert!(f.relay_url.is_some());
    }
}
