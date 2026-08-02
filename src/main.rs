//! get-video 命令行入口：启动本地 relay 服务。

use clap::Parser;
use get_video::relay::{self, RelayConfig};

#[derive(Parser)]
#[command(name = "get-video", about = "视频地址解析 + 本地 relay 流服务")]
struct Cli {
    /// 监听地址（0.0.0.0 可供局域网设备拉流）
    #[arg(long, env = "GET_VIDEO_HOST", default_value = "127.0.0.1")]
    host: String,
    /// 监听端口
    #[arg(long, env = "GET_VIDEO_PORT", default_value_t = 8321)]
    port: u16,
    /// L3 站点规则包本地 JSON 路径
    #[arg(long, env = "GET_VIDEO_RULES")]
    rules: Option<std::path::PathBuf>,
    /// 测试钩子：允许代理内网/本机地址（关闭 SSRF 黑名单，仅本地调试用）
    #[arg(long, env = "GET_VIDEO_ALLOW_PRIVATE")]
    allow_private_hosts: bool,
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "get_video=info".into()),
        )
        .init();

    let cli = Cli::parse();
    let config = RelayConfig {
        host: cli.host.clone(),
        port: cli.port,
        allow_private_hosts: cli.allow_private_hosts,
        rules_path: cli.rules,
        dash_store: None,
    };
    if config.allow_private_hosts {
        tracing::warn!("SSRF 黑名单已关闭（--allow-private-hosts），仅限本地调试！");
    }
    let handle = relay::start(config).await.expect("启动 relay 服务失败");
    println!("relay 服务已启动: {}", handle.base_url());
    println!(
        "  提取接口: {}/api/extract?url=<页面URL>",
        handle.base_url()
    );
    println!("  播放测试页: {}/player?url=<relay地址>", handle.base_url());
    // 常驻运行直到被 kill
    tokio::signal::ctrl_c().await.ok();
    handle.shutdown().await;
}
