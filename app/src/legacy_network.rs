//! legacy 局域网地址选择（投屏地址用）。
//!
//! 本文件是 FND-07C 从 `main.rs` 逐字移出的网络地址 helper；选择策略与
//! 迁移前完全一致（Unix 优先枚举网卡取 RFC1918 私网 IPv4，规避 VPN utun
//! 的 198.18.x.x 假地址；否则 UDP 路由探测）。

/// 本机局域网 IP（投屏地址用）。
/// UDP 路由探测（不产生实际流量）；VPN 接管默认路由时会拿到 utun 的
/// 198.18.x.x 这类假地址，因此 Unix 上优先枚举网卡取 RFC1918 私网地址。
pub(crate) fn lan_ip() -> Option<std::net::IpAddr> {
    #[cfg(unix)]
    if let Some(ip) = lan_ip_ifaddrs() {
        return Some(ip);
    }
    let s = std::net::UdpSocket::bind("0.0.0.0:0").ok()?;
    s.connect("8.8.8.8:80").ok()?;
    Some(s.local_addr().ok()?.ip())
}

/// 枚举网卡，取第一个「启用、非回环、RFC1918 私网」的 IPv4（跳过 VPN 虚拟网卡）。
#[cfg(unix)]
pub(crate) fn lan_ip_ifaddrs() -> Option<std::net::IpAddr> {
    use std::net::Ipv4Addr;
    unsafe {
        let mut ifaddrs: *mut libc::ifaddrs = std::ptr::null_mut();
        if libc::getifaddrs(&mut ifaddrs) != 0 {
            return None;
        }
        let mut cur = ifaddrs;
        let mut found = None;
        while !cur.is_null() {
            let ifa = &*cur;
            let flags = ifa.ifa_flags as libc::c_int;
            let up = flags & libc::IFF_UP != 0;
            let loopback = flags & libc::IFF_LOOPBACK != 0;
            let is_v4 = !ifa.ifa_addr.is_null()
                && (*ifa.ifa_addr).sa_family as libc::c_int == libc::AF_INET;
            if up && !loopback && is_v4 {
                let sin = &*(ifa.ifa_addr as *const libc::sockaddr_in);
                let ip = Ipv4Addr::from(u32::from_be(sin.sin_addr.s_addr));
                if ip.is_private() {
                    found = Some(std::net::IpAddr::V4(ip));
                    break;
                }
            }
            cur = ifa.ifa_next;
        }
        libc::freeifaddrs(ifaddrs);
        found
    }
}

#[cfg(test)]
#[path = "legacy_network_tests.rs"]
mod tests;
