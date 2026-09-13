//! Model adapter (CNT-12).
//!
//! Provider registry and a passthrough adapter that satisfies the
//! `ModelProviderPort` trait. No real HTTP client — the network layer is
//! product assembly (CNT-13/15).

use crayon_model_contract::{ModelContractError, ModelProviderConfig};

/// Provider registry: maps provider names to configs.
pub struct ProviderRegistry {
    providers: BTreeMap<String, ModelProviderConfig>,
}

use std::collections::BTreeMap;

impl ProviderRegistry {
    #[must_use]
    pub fn new() -> Self {
        Self {
            providers: BTreeMap::new(),
        }
    }

    pub fn register(&mut self, config: ModelProviderConfig) -> Result<(), ModelContractError> {
        config.validate()?;
        self.providers.insert(config.provider_name.clone(), config);
        Ok(())
    }

    pub fn get(&self, name: &str) -> Option<&ModelProviderConfig> {
        self.providers.get(name)
    }

    pub fn remove(&mut self, name: &str) -> Option<ModelProviderConfig> {
        self.providers.remove(name)
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.providers.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.providers.is_empty()
    }
}

impl Default for ProviderRegistry {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod model_adapter_tests;
