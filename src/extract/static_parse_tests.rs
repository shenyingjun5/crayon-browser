use super::*;

#[test]
fn protocol_classification() {
    assert_eq!(
        Protocol::from_url("https://a.com/x/index.m3u8?token=1"),
        Protocol::Hls
    );
    assert_eq!(Protocol::from_url("https://a.com/x.mp4"), Protocol::Mp4);
    assert_eq!(
        Protocol::from_url("https://a.com/x_da2-1-30032.m4s?upsig=a"),
        Protocol::Mp4
    );
    assert_eq!(Protocol::from_url("https://a.com/x.flv"), Protocol::Flv);
    assert_eq!(Protocol::from_url("https://a.com/x.mpd"), Protocol::Dash);
    assert_eq!(Protocol::from_url("https://a.com/x.html"), Protocol::Other);
}

#[test]
fn quality_guess() {
    assert_eq!(guess_quality("https://a.com/1080p/index.m3u8"), Some(1080));
    assert_eq!(guess_quality("4K 超清"), Some(2160));
    assert_eq!(guess_quality("nothing here"), None);
}
