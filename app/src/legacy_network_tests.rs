//! LAN 地址选择 helper 单测：只断言不变量（私网 IPv4 / 非回环），
//! 不断言具体地址——运行环境的网卡配置不属于契约。

use super::*;

#[cfg(unix)]
#[test]
fn lan_ip_ifaddrs_returns_private_v4_when_available() {
    if let Some(ip) = lan_ip_ifaddrs() {
        match ip {
            std::net::IpAddr::V4(v4) => {
                assert!(v4.is_private(), "应跳过非私网/ VPN 虚拟网卡地址: {v4}")
            }
            other => panic!("ifaddrs helper 只应返回 IPv4: {other}"),
        }
    }
}

#[test]
fn lan_ip_is_never_loopback() {
    if let Some(ip) = lan_ip() {
        assert!(!ip.is_loopback(), "投屏地址不得为回环地址: {ip}");
    }
}
