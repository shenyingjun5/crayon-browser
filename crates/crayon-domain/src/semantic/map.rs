//! Page map envelope (ACT-01).
//!
//! `PageMap` is the frozen top-level semantic map: verified element nodes,
//! action offers, form and media maps, the deterministic risk entries and
//! the truncation report, all fenced to one navigation generation. It
//! carries no engine types, no DOM objects and no raw HTML.

use crate::ids::{SessionGeneration, TabId};
use crate::semantic::action::{validate_offers, ActionOffer};
use crate::semantic::form::{validate_forms, FormMap};
use crate::semantic::media::{validate_media, MediaElement};
use crate::semantic::node::SemanticNode;
use crate::semantic::risk::{validate_risk, RiskEntry};
use crate::semantic::{
    SemanticSchemaError, MAX_NODE_NAME_BYTES, MAX_ORIGIN_BYTES, MAX_SEMANTIC_NODES,
    SEMANTIC_MAP_SCHEMA_VERSION,
};
use serde::{Deserialize, Serialize};

/// Reported truncation of a bounded map or change batch. Zero counts mean
/// nothing was omitted.
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SemanticTruncation {
    #[serde(default)]
    pub nodes_omitted: u32,
    #[serde(default)]
    pub actions_omitted: u32,
    #[serde(default)]
    pub forms_omitted: u32,
    #[serde(default)]
    pub media_omitted: u32,
    #[serde(default)]
    pub risk_omitted: u32,
}

impl SemanticTruncation {
    /// Whether anything was omitted.
    #[must_use]
    pub fn any(self) -> bool {
        self.nodes_omitted > 0
            || self.actions_omitted > 0
            || self.forms_omitted > 0
            || self.media_omitted > 0
            || self.risk_omitted > 0
    }
}

/// The frozen top-level semantic map of one verified page state.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PageMap {
    pub schema_version: u32,
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    pub revision: u64,
    /// Validated `http(s)` origin of the mapped document (no path/query).
    pub origin: String,
    /// Bounded document title (untrusted page content).
    pub title: String,
    pub nodes: Vec<SemanticNode>,
    pub actions: Vec<ActionOffer>,
    pub forms: Vec<FormMap>,
    pub media: Vec<MediaElement>,
    pub risk: Vec<RiskEntry>,
    pub truncation: SemanticTruncation,
}

/// Validates a bounded `http(s)` origin: scheme + host [+ port], no path,
/// query or fragment, no credentials.
#[must_use]
pub fn is_valid_origin(origin: &str) -> bool {
    if origin.len() > MAX_ORIGIN_BYTES {
        return false;
    }
    let Some((scheme, rest)) = origin.split_once("://") else {
        return false;
    };
    if !matches!(scheme, "http" | "https") || rest.is_empty() {
        return false;
    }
    if rest.contains(['/', '?', '#', '@', '\\']) {
        return false;
    }
    match rest.rsplit_once(':') {
        Some((host, port)) => {
            host.len() > 1 && !port.is_empty() && port.bytes().all(|b| b.is_ascii_digit())
        }
        None => true,
    }
}

impl PageMap {
    /// Validates version, bounds and cross-references; wraps a page map.
    ///
    /// Action offers, form fields, media elements and risk entries must
    /// reference node ids that exist in `nodes`.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        tab_id: TabId,
        generation: SessionGeneration,
        revision: u64,
        origin: String,
        title: String,
        nodes: Vec<SemanticNode>,
        actions: Vec<ActionOffer>,
        forms: Vec<FormMap>,
        media: Vec<MediaElement>,
        risk: Vec<RiskEntry>,
        truncation: SemanticTruncation,
    ) -> Result<Self, SemanticSchemaError> {
        if !is_valid_origin(&origin) {
            return Err(SemanticSchemaError::OriginInvalid);
        }
        if title.len() > MAX_NODE_NAME_BYTES * 2 {
            return Err(SemanticSchemaError::BoundExceeded("title"));
        }
        if nodes.len() > MAX_SEMANTIC_NODES {
            return Err(SemanticSchemaError::BudgetExceeded("nodes"));
        }
        validate_offers(&actions)?;
        validate_forms(&forms)?;
        validate_media(&media)?;
        validate_risk(&risk)?;
        let known: std::collections::BTreeSet<&str> =
            nodes.iter().map(|node| node.id.as_str()).collect();
        if known.len() != nodes.len() {
            return Err(SemanticSchemaError::DuplicateEntry("node id"));
        }
        let referenced = |id: &crate::semantic::node::SemanticNodeId| known.contains(id.as_str());
        if actions.iter().any(|offer| !referenced(&offer.node))
            || forms.iter().any(|form| {
                !referenced(&form.node) || form.fields.iter().any(|field| !referenced(&field.node))
            })
            || media.iter().any(|media| !referenced(&media.node))
            || risk.iter().any(|entry| !referenced(&entry.node))
        {
            return Err(SemanticSchemaError::UnknownNode);
        }
        Ok(Self {
            schema_version: SEMANTIC_MAP_SCHEMA_VERSION,
            tab_id,
            generation,
            revision,
            origin,
            title,
            nodes,
            actions,
            forms,
            media,
            risk,
            truncation,
        })
    }
}
