//! Form map schema (ACT-01).
//!
//! A `FormMap` describes structure, constraints and observed state. Field
//! **values** are not expressible — the schema has no place to carry them —
//! and password/file fields appear only as excluded kinds (AC-009 owns the
//! refined exclusion policy).

use crate::semantic::node::SemanticNodeId;
use crate::semantic::{
    SemanticNodeKind, SemanticSchemaError, MAX_FIELDS_PER_FORM, MAX_FORMS, MAX_FORM_ERROR_BYTES,
};
use serde::{Deserialize, Serialize};

/// One form field: identity, bounded label, coarse constraints and the
/// observed filled/error state. No value, no pattern, no DOM attributes.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FormField {
    pub node: SemanticNodeId,
    /// The field's element kind; restricted to input-like kinds.
    pub kind: SemanticNodeKind,
    pub label: String,
    #[serde(default)]
    pub required: bool,
    #[serde(default)]
    pub read_only: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max_length: Option<u32>,
    #[serde(default)]
    pub filled: bool,
    /// Bounded, page-provided validation error text (untrusted content).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub error_text: Option<String>,
}

/// One form region and its fields.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FormMap {
    pub node: SemanticNodeId,
    pub fields: Vec<FormField>,
}

impl FormMap {
    /// Validates kind restrictions and bounds; wraps a form map.
    pub fn new(node: SemanticNodeId, fields: Vec<FormField>) -> Result<Self, SemanticSchemaError> {
        if fields.len() > MAX_FIELDS_PER_FORM {
            return Err(SemanticSchemaError::BudgetExceeded("form fields"));
        }
        for field in &fields {
            if !matches!(
                field.kind,
                SemanticNodeKind::TextInput
                    | SemanticNodeKind::PasswordInput
                    | SemanticNodeKind::FileInput
                    | SemanticNodeKind::Checkbox
                    | SemanticNodeKind::Radio
                    | SemanticNodeKind::Select
                    | SemanticNodeKind::Slider
                    | SemanticNodeKind::Textarea
            ) {
                return Err(SemanticSchemaError::KindMismatch("form field kind"));
            }
            if field.label.len() > MAX_FORM_ERROR_BYTES * 2 {
                return Err(SemanticSchemaError::BoundExceeded("field label"));
            }
            if let Some(error) = &field.error_text {
                if error.len() > MAX_FORM_ERROR_BYTES {
                    return Err(SemanticSchemaError::BoundExceeded("field error text"));
                }
            }
        }
        Ok(Self { node, fields })
    }
}

/// Bounds check helper for the map assembly.
pub(crate) fn validate_forms(forms: &[FormMap]) -> Result<(), SemanticSchemaError> {
    if forms.len() > MAX_FORMS {
        return Err(SemanticSchemaError::BudgetExceeded("forms"));
    }
    Ok(())
}
