//! Scope classification only; real LAN traffic belongs to the device harness.
use crayon_media_probe::{ProbeHttpClient, ProbeHttpConfig, ProbeHttpError, SameOriginLanTarget};

#[test]
fn only_same_origin_rfc1918_literals_are_accepted() {
    for ip in ["10.0.0.1", "172.16.0.1", "172.31.255.254", "192.168.0.1"] {
        let page = format!("http://{ip}:8000/page#section");
        let media = format!("http://{ip}:8000/video.mp4?part=1");
        assert!(SameOriginLanTarget::new(&page, &media).is_ok());
        assert!(SameOriginLanTarget::new(&page, &media.replace(":8000", ":8001")).is_err());
        assert!(SameOriginLanTarget::new(&page, &media.replace("http:", "https:")).is_err());
    }
    assert!(SameOriginLanTarget::new("https://10.0.0.1/", "https://10.0.0.1:443/v.mp4").is_ok());
    assert!(SameOriginLanTarget::new("http://192.168.0.1/", "http://192.168.0.2/v.mp4").is_err());
}

#[test]
fn lan_exception_never_admits_other_address_classes_or_hostnames() {
    for host in [
        "127.0.0.1",
        "127.1",
        "2130706433",
        "0.0.0.0",
        "169.254.169.254",
        "100.64.0.1",
        "198.18.0.1",
        "192.0.2.1",
        "172.15.255.255",
        "172.32.0.1",
        "224.0.0.1",
        "255.255.255.255",
        "8.8.8.8",
        "[::1]",
        "[fc00::1]",
        "[fe80::1]",
        "[::ffff:192.168.0.1]",
        "localhost",
        "media.example.test",
    ] {
        let page = format!("http://{host}/");
        assert!(
            SameOriginLanTarget::new(&page, &format!("{page}v.mp4")).is_err(),
            "{host}"
        );
    }
}

#[test]
fn credentials_fragments_and_non_http_are_not_lan_targets() {
    let page = "http://10.0.0.1/";
    for media in [
        "http://user@example.test/v.mp4",
        "http://user@10.0.0.1/v.mp4",
        "http://:example@10.0.0.1/v.mp4",
        "http://10.0.0.1/v.mp4#segment",
        "file:///v.mp4",
        "ftp://10.0.0.1/v.mp4",
        "blob:http://10.0.0.1/id",
        "not a url",
        "http://10.0.0.1:0/v.mp4",
    ] {
        assert!(SameOriginLanTarget::new(page, media).is_err());
    }
    assert!(SameOriginLanTarget::new("http://user@10.0.0.1/", "http://10.0.0.1/v.mp4").is_err());
}

#[tokio::test]
async fn constructing_a_lan_target_does_not_grant_the_default_client() {
    let media = "http://10.0.0.1/v.mp4";
    let _target = SameOriginLanTarget::new("http://10.0.0.1/", media).unwrap();
    assert_eq!(
        ProbeHttpClient::new(ProbeHttpConfig::default())
            .head(media)
            .await,
        Err(ProbeHttpError::NonPublicAddress)
    );
}
