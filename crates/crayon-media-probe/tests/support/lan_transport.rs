//! Private request-builder contract; builds requests but never opens sockets.
//! Included as a unit-test module so no test transport API enters production.
// The support subdirectory is not an independent integration target.
use super::*;

#[tokio::test]
async fn scope_pins_every_url_component_without_creating_a_connection() {
    let media = "http://192.168.0.10:8000/video.mp4?part=1";
    let target = SameOriginLanTarget::new("http://192.168.0.10:8000/page", media).unwrap();
    let scoped = ProbeHttpClient::new(ProbeHttpConfig::default()).for_selected_lan(target);
    let request = scoped
        .prepare(reqwest::Method::HEAD, media)
        .await
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(request.url().as_str(), media);
    assert_eq!(request.method(), reqwest::Method::HEAD);
    assert!(request.headers().get(reqwest::header::COOKIE).is_none());
    assert!(request
        .headers()
        .get(reqwest::header::AUTHORIZATION)
        .is_none());
    for other in [
        "http://192.168.0.11:8000/video.mp4?part=1",
        "http://192.168.0.10:8001/video.mp4?part=1",
        "https://192.168.0.10:8000/video.mp4?part=1",
        "http://192.168.0.10:8000/other.mp4?part=1",
        "http://192.168.0.10:8000/video.mp4?part=2",
        "http://192.168.0.10:8000/video.mp4?part=1#fragment",
        "http://localhost/video.mp4",
        "http://127.0.0.1/video.mp4",
    ] {
        assert!(matches!(
            scoped.prepare(reqwest::Method::HEAD, other).await,
            Err(ProbeHttpError::ScopeMismatch)
        ));
    }
}
