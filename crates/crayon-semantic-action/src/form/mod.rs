//! Form projection with exclusions (ACT-09, AC-009).
//!
//! Projects the frozen [`FormMap`] vocabulary into per-field views that
//! carry structure, constraints, filled state and bounded error text —
//! never values. Sensitive and file fields appear only as excluded rows;
//! the projection cannot express a value surface because none exists in
//! the input types.

use crayon_domain::{ElementState, FormMap, SemanticNodeKind};
use serde::{Deserialize, Serialize};

/// Why one field is excluded from action suggestions.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FieldExclusion {
    /// The field is a credential entry surface.
    SensitiveCredential,
    /// The field is a file upload surface.
    FileUpload,
    /// The field is not visible.
    Hidden,
    /// The field is disabled.
    Disabled,
    /// The field is read-only.
    ReadOnly,
}

impl FieldExclusion {
    /// All exclusions; the closed set locked by golden tests.
    pub const ALL: [Self; 5] = [
        Self::SensitiveCredential,
        Self::FileUpload,
        Self::Hidden,
        Self::Disabled,
        Self::ReadOnly,
    ];
}

/// One field as exposed to Agent readers: identity, constraint facts and
/// observed state. There is no value field in this type by construction.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FormFieldView {
    pub node: crayon_domain::SemanticNodeId,
    pub kind: SemanticNodeKind,
    pub label: String,
    pub required: bool,
    pub filled: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max_length: Option<u32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub error_text: Option<String>,
    /// Present when the field is excluded from action suggestions.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub excluded: Option<FieldExclusion>,
}

/// One form as exposed to Agent readers.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FormView {
    pub node: crayon_domain::SemanticNodeId,
    pub fields: Vec<FormFieldView>,
}

impl FormView {
    /// Fields that are not excluded and may back action suggestions.
    pub fn actionable_fields(&self) -> impl Iterator<Item = &FormFieldView> {
        self.fields.iter().filter(|field| field.excluded.is_none())
    }
}

/// Projects one form using the verified node states of its fields. A field
/// without a verified state entry is treated as hidden (fail closed).
#[must_use]
pub fn project_form(
    form: &FormMap,
    states: &dyn Fn(&crayon_domain::SemanticNodeId) -> Option<ElementState>,
) -> FormView {
    let fields = form
        .fields
        .iter()
        .map(|field| {
            let excluded = if field.kind == SemanticNodeKind::PasswordInput {
                Some(FieldExclusion::SensitiveCredential)
            } else if field.kind == SemanticNodeKind::FileInput {
                Some(FieldExclusion::FileUpload)
            } else if field.read_only {
                Some(FieldExclusion::ReadOnly)
            } else {
                match states(&field.node) {
                    None => Some(FieldExclusion::Hidden),
                    Some(state) if !state.visible => Some(FieldExclusion::Hidden),
                    Some(state) if !state.enabled => Some(FieldExclusion::Disabled),
                    Some(_) => None,
                }
            };
            FormFieldView {
                node: field.node.clone(),
                kind: field.kind,
                label: field.label.clone(),
                required: field.required,
                filled: field.filled,
                max_length: field.max_length,
                error_text: field.error_text.clone(),
                excluded,
            }
        })
        .collect();
    FormView {
        node: form.node.clone(),
        fields,
    }
}

/// Projects every form of a verified page map.
#[must_use]
pub fn project_forms(map: &crayon_domain::PageMap) -> Vec<FormView> {
    map.forms
        .iter()
        .map(|form| {
            project_form(form, &|node_id: &crayon_domain::SemanticNodeId| {
                map.nodes
                    .iter()
                    .find(|node| node.id == *node_id)
                    .map(|node| node.state.clone())
            })
        })
        .collect()
}
