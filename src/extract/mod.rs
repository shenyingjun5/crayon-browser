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
        }
    }

    /// 提取入口：网页 URL → 统一视频流列表。
    pub async fn extract(&self, page_url: &str) -> Result<VideoInfo, String> {
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
            formats.push(self.build_format(cand, &headers).await);
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
            let cand = Candidate {
                url: m.url.clone(),
                protocol: static_parse::Protocol::from_url(&m.url),
                quality: static_parse::guess_quality(&m.url),
            };
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
            formats.push(self.build_format(&cand, &headers).await);
        }

        // 站点专用解析器（首批：央视网 tv.cctv.com / cntv、B 站番剧，见 sites.rs）
        let mut site_hit = false;
        let mut site_title: Option<String> = None;
        let mut site_note: Option<String> = None;
        if let Some(site) = sites::extract(&self.client, &final_url, &html).await {
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
                formats.push(self.build_format(cand, &headers).await);
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

    async fn build_format(&self, cand: &Candidate, headers: &HashMap<String, String>) -> Format {
        let drm = self.detect_drm(cand, headers).await;
        let relay_url = if drm {
            None
        } else {
            Some(self.relay_url(&cand.url, headers))
        };
        Format {
            url: cand.url.clone(),
            protocol: cand.protocol.as_str().to_string(),
            quality: cand.quality.map(|q| format!("{q}p")),
            drm,
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
