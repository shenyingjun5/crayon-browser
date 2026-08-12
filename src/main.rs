//! `legacy-dev` only CLI entry point for the historical relay service.

use clap::Parser;
use crayon_browser_core::relay::{self, RelayConfig};

#[derive(Parser)]
#[command(
    name = "crayon-legacy-video-tool",
    about = "蜡笔浏览器历史视频解析与本地 relay 开发工具"
)]
struct Cli {
    /// 监听地址（0.0.0.0 可供局域网设备拉流）
    #[arg(long, env = "CRAYON_LEGACY_HOST", default_value = "127.0.0.1")]
    host: String,
    /// 监听端口
    #[arg(long, env = "CRAYON_LEGACY_PORT", default_value_t = 8321)]
    port: u16,
    /// L3 站点规则包本地 JSON 路径
    #[arg(long, env = "CRAYON_LEGACY_RULES")]
    rules: Option<std::path::PathBuf>,
    /// 测试钩子：允许代理内网/本机地址（关闭 SSRF 黑名单，仅本地调试用）
    #[arg(long, env = "CRAYON_LEGACY_ALLOW_PRIVATE")]
    allow_private_hosts: bool,
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "crayon_browser_core=info".into()),
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
