//! L3 站点规则包（最小骨架）。
//!
//! 仅支持从**本地 JSON 文件**加载规则；远程热更新未实现（后续里程碑 M3 再做，
//! 见 README「待办」）。规则格式：
//!
//! ```json
//! [
//!   {
//!     "name": "示例站点",
//!     "domains": ["example.com"],
//!     "pattern": "videoUrl\\s*=\\s*\"(?<url>[^\"]+\\.m3u8)\"",
//!     "referer": "https://example.com/",
//!     "ua": "可选，覆盖默认 UA"
//!   }
//! ]
//! ```

use serde::Deserialize;

#[derive(Debug, Clone, Deserialize)]
pub struct SiteRule {
    pub name: String,
    /// 域名后缀匹配列表。
    pub domains: Vec<String>,
    /// 提取正则，需包含命名分组 `(?<url>...)`。
    pub pattern: String,
    #[serde(default)]
    pub referer: Option<String>,
    #[serde(default)]
    pub ua: Option<String>,
    #[serde(skip)]
    compiled: Option<regex::Regex>,
}

/// 规则包：从 JSON 文件加载，按页面域名匹配后应用正则。
#[derive(Debug, Default, Clone)]
pub struct RulePack {
    rules: Vec<SiteRule>,
}

/// 规则命中后的提取结果。
#[derive(Debug, Clone)]
pub struct RuleMatch {
    pub rule_name: String,
    pub url: String,
    pub referer: Option<String>,
    pub ua: Option<String>,
}

impl RulePack {
    pub fn empty() -> Self {
        Self { rules: vec![] }
    }

    /// 从本地 JSON 文件加载。文件不存在时返回空规则包（不视为错误）。
    pub fn load(path: &std::path::Path) -> Result<Self, String> {
        if !path.exists() {
            return Ok(Self::empty());
        }
        let text = std::fs::read_to_string(path).map_err(|e| format!("读取规则包失败: {e}"))?;
        Self::from_json(&text)
    }

    pub fn from_json(text: &str) -> Result<Self, String> {
        let mut rules: Vec<SiteRule> =
            serde_json::from_str(text).map_err(|e| format!("规则包 JSON 解析失败: {e}"))?;
        for rule in &mut rules {
            let re = regex::Regex::new(&rule.pattern)
                .map_err(|e| format!("规则 [{}] 正则无效: {e}", rule.name))?;
            if re.capture_names().flatten().all(|n| n != "url") {
                return Err(format!("规则 [{}] 缺少 (?<url>...) 命名分组", rule.name));
            }
            rule.compiled = Some(re);
        }
        Ok(Self { rules })
    }

    pub fn len(&self) -> usize {
        self.rules.len()
    }

    pub fn is_empty(&self) -> bool {
        self.rules.is_empty()
    }

    /// 对页面 HTML 应用匹配的规则，返回所有命中结果。
    pub fn apply(&self, page_url: &str, html: &str) -> Vec<RuleMatch> {
        let host = url::Url::parse(page_url)
            .ok()
            .and_then(|u| u.host_str().map(|h| h.to_ascii_lowercase()))
            .unwrap_or_default();
        let mut out = Vec::new();
        for rule in &self.rules {
            let domain_hit = rule
                .domains
                .iter()
                .any(|d| host == *d || host.ends_with(&format!(".{d}")));
            if !domain_hit {
                continue;
            }
            let Some(re) = &rule.compiled else { continue };
            for cap in re.captures_iter(html) {
                if let Some(m) = cap.name("url") {
                    out.push(RuleMatch {
                        rule_name: rule.name.clone(),
                        url: m.as_str().replace("\\/", "/"),
                        referer: rule.referer.clone(),
                        ua: rule.ua.clone(),
                    });
                }
            }
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rule_pack_match() {
        let json = r#"[{
            "name": "demo",
            "domains": ["example.com"],
            "pattern": "videoUrl\\s*=\\s*\"(?<url>[^\"]+\\.m3u8)\"",
            "referer": "https://example.com/"
        }]"#;
        let pack = RulePack::from_json(json).unwrap();
        let html = r#"<script>videoUrl = "https://cdn.example.com/a/b.m3u8";</script>"#;
        let hits = pack.apply("https://www.example.com/watch/1", html);
        assert_eq!(hits.len(), 1);
        assert_eq!(hits[0].url, "https://cdn.example.com/a/b.m3u8");
        // 域名不匹配则不命中
        assert!(pack.apply("https://other.com/", html).is_empty());
    }

    #[test]
    fn rule_pack_rejects_bad_regex() {
        let json = r#"[{"name":"bad","domains":["x.com"],"pattern":"(.*)"}]"#;
        assert!(RulePack::from_json(json).is_err());
    }
}
