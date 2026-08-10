use super::parse_sniff_url;

#[test]
fn relative_and_empty_sniff_urls_are_rejected() {
    for invalid in ["", "not-a-url", "/relative/path"] {
        assert!(
            parse_sniff_url(invalid).is_err(),
            "relative input must fail before main-thread dispatch: {invalid:?}"
        );
    }
}

#[test]
fn absolute_sniff_url_is_preserved() {
    let input = "http://127.0.0.1:18765/video?quality=720";
    let parsed = parse_sniff_url(input).expect("absolute loopback URL should parse");
    assert_eq!(parsed.as_str(), input);
}
