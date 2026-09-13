use super::*;
use crayon_model_contract::{ModelProviderConfig, ModelRequest, ModelResponse};

fn config(name: &str) -> ModelProviderConfig {
    ModelProviderConfig {
        provider_name: name.to_owned(),
        endpoint_url: "https://api.example.com/v1".to_owned(),
        model_name: "test-model".to_owned(),
        api_key_handle: "key-handle-1".to_owned(),
        timeout_ms: 30_000,
        max_tokens: 4096,
    }
}

#[test]
fn registry_register_and_get() {
    let mut registry = ProviderRegistry::default();
    registry.register(config("openai-compat")).unwrap();
    assert!(registry.get("openai-compat").is_some());
    assert!(registry.get("nonexistent").is_none());
    assert_eq!(registry.len(), 1);
}

#[test]
fn registry_remove() {
    let mut registry = ProviderRegistry::default();
    registry.register(config("test")).unwrap();
    assert!(registry.remove("test").is_some());
    assert!(registry.get("test").is_none());
}

#[test]
fn fake_provider_echoes_prompt() {
    use crayon_model_contract::ModelProviderPort;
    struct FakeProvider;
    impl ModelProviderPort for FakeProvider {
        fn execute(
            &mut self,
            _config: &ModelProviderConfig,
            request: &ModelRequest,
        ) -> Result<ModelResponse, ModelContractError> {
            Ok(ModelResponse {
                text: format!("[fake] {}", request.prompt),
                prompt_tokens: 1,
                completion_tokens: 1,
            })
        }
    }
    let mut provider = FakeProvider;
    let cfg = config("fake");
    let request = ModelRequest {
        prompt: "summarize this".to_owned(),
        markdown: "# Document".to_owned(),
        max_tokens: 128,
    };
    let response = provider.execute(&cfg, &request).unwrap();
    assert!(response.text.contains("[fake]"));
}
