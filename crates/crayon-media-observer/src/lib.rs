//! Platform-neutral facts emitted by browser media observation.

/// Provenance of a playback observation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ObservationOrigin {
    /// A page, injected script, or renderer message reported the event.
    PageReported,
    /// The privileged browser process independently verified the event.
    BrowserVerified,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UserActivation {
    Missing,
    BrowserVerified,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PlaybackProgress {
    NotAdvanced,
    Advanced,
}

/// Minimal pre-v1 evidence needed to decide whether planning may start.
///
/// This type intentionally carries facts only. It does not select a cast mode,
/// receiver, URL, or protocol. A later versioned contract may replace it.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PlaybackObservation {
    origin: ObservationOrigin,
    user_activation: UserActivation,
    playback_progress: PlaybackProgress,
}

impl PlaybackObservation {
    #[must_use]
    pub const fn new(
        origin: ObservationOrigin,
        user_activation: UserActivation,
        playback_progress: PlaybackProgress,
    ) -> Self {
        Self {
            origin,
            user_activation,
            playback_progress,
        }
    }

    #[must_use]
    pub const fn origin(self) -> ObservationOrigin {
        self.origin
    }

    #[must_use]
    pub const fn user_activation(self) -> UserActivation {
        self.user_activation
    }

    #[must_use]
    pub const fn playback_progress(self) -> PlaybackProgress {
        self.playback_progress
    }
}
