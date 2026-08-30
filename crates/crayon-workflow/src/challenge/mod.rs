//! Deterministic challenge evidence classification (WFL-02).
//!
//! Inputs are closed booleans produced by the trusted Browser adapter. Raw
//! DOM, page text, selectors and challenge values cannot enter this API.

use crayon_domain::{is_valid_origin, ChallengeEvidence, ChallengeKind, SemanticSchemaError};

const CAPTCHA_EVIDENCE: &str = "browser_signal:captcha";
const RISK_EVIDENCE: &str = "browser_signal:risk_check";
const LOGIN_EVIDENCE: &str = "browser_signal:login_required";

/// Closed, data-free observations normalized by the Browser process.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ChallengeSignals {
    /// A recognized CAPTCHA frame or platform challenge role is present.
    pub captcha_surface: bool,
    /// A recognized slider/puzzle verification surface is present.
    pub interactive_verification: bool,
    /// A Browser-classified risk/interstitial confirmation is present.
    pub risk_interstitial: bool,
    /// The current authorized operation requires an interactive login.
    pub login_required: bool,
}

/// Pure classifier with no IO, callbacks or operation surface.
#[derive(Clone, Copy, Debug, Default)]
pub struct ChallengeDetector;

impl ChallengeDetector {
    /// Returns bounded evidence when at least one closed signal is present.
    ///
    /// Concurrent signals resolve conservatively: CAPTCHA (including slider),
    /// then risk check, then login. The origin is revalidated by the frozen
    /// domain constructor and invalid input fails closed.
    pub fn detect(
        &self,
        origin: &str,
        signals: ChallengeSignals,
    ) -> Result<Option<ChallengeEvidence>, SemanticSchemaError> {
        if !is_valid_origin(origin) {
            return Err(SemanticSchemaError::OriginInvalid);
        }
        let classified = if signals.captcha_surface || signals.interactive_verification {
            Some((ChallengeKind::Captcha, CAPTCHA_EVIDENCE))
        } else if signals.risk_interstitial {
            Some((ChallengeKind::RiskCheck, RISK_EVIDENCE))
        } else if signals.login_required {
            Some((ChallengeKind::LoginRequired, LOGIN_EVIDENCE))
        } else {
            None
        };

        classified
            .map(|(kind, note)| {
                ChallengeEvidence::new(kind, origin.to_owned(), Some(note.to_owned()))
            })
            .transpose()
    }
}
