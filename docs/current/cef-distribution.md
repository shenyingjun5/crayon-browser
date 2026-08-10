# CEF 发行包契约

状态：CEF-01A；更新日期：2026-08-10。

## 固定版本

- CEF：`150.0.10+g8042e43+chromium-150.0.7871.101`，CEF Automated Builds 在 2026-07-09 发布并标记为 stable。
- 发行类型：Standard。它提供后续 Debug/Release 壳构建所需的完整二进制、资源、头文件和 CMake 支持；archive 与解压目录只进入本地/CI 缓存，不进入 Git。
- 官方来源：`https://cef-builds.spotifycdn.com/`。唯一机器可读事实位于 `cmake/cef/CefDistribution.cmake`，其他构建文件不得复制版本、URL 或 hash。

| 平台键 | 官方 archive SHA-1 |
|---|---|
| `windows64` | `b5ae23cec83689ef9843951e182443cacbaff5af` |
| `macosx64` | `17e14fe00415e01a79e8b6d7ecaad8a861f1b388` |
| `macosarm64` | `2e77063444e3ca07aea2651b763d3c4248bf2543` |
| `linux64` | `8ef7861df621ac9ce370ff30161e4c5ba5d7e7de` |

CEF 官方只发布上述 SHA-1 sidecar；下载器强制 HTTPS、固定官方 origin 和精确 SHA-1。QAR-10 生成正式 SBOM/NOTICE 时还必须计算并记录各发布输入的 SHA-256 与产物映射。

Windows x64 实际下载证据：archive 大小 `346936917` bytes，SHA-256 `407c5a52e96a175a79331dcecefee0345feca85f98161619d79553632866eb8e`；该值用于本次供应链记录，下载器仍以四平台均由上游发布的 SHA-1 为统一自动校验契约。

## 缓存与离线输入

显式下载命令：

```powershell
cmake '-DCRAYON_CEF_PLATFORM=windows64' '-DCRAYON_CEF_CACHE_DIR=.cache/cef' -P cmake/cef/DownloadCef.cmake
```

- `CRAYON_CEF_CACHE_DIR` 必填；仓库内约定 `.cache/cef/` 且已被忽略，CI 可以传工作区外缓存。
- 同一缓存目录使用 60 秒有界文件锁；已存在的 archive 先校验再复用，hash 不匹配立即失败且不静默覆盖。下载使用 `.partial` 临时文件，网络或 hash 失败时删除 partial，成功校验后再原子改名。
- 无网络构建传入解压后的本地根；根至少包含 `include/cef_version.h`、`cmake/cef_variables.cmake`、`libcef_dll/CMakeLists.txt`，且版本头必须精确匹配固定 revision。01C 会把该验证接入 CMake configure，01A 不提前创建产品构建图。
- 自动化 contract 使用本地 fixture，不以公网或 CEF 服务可用性作为通过条件。

## 许可与发布门禁

- CEF 源码和 cef-project 使用 BSD 风格许可；再分发必须保留 CEF archive 内的 `LICENSE.txt`、版权声明、条件和免责声明。
- Chromium 及 archive 内第三方组件具有各自许可。QAR-09/QAR-10 必须在每个平台正式包中生成与实际文件一致的 SBOM、NOTICE 和 source mapping；完成前不得发布。
- CEF 默认未启用受专利约束的专有 codec。本项目不修改 Chromium/CEF 构建参数来启用 H.264/AAC，也不捆绑 Widevine/CDM；任何变更必须先经过独立法律结论、依赖 Roadmap 和发布门禁。
- 本任务只锁定依赖输入，不表示 CEF shell、sandbox、codec 兼容性或三平台构建已经完成。

## 上游资料

- [CEF Automated Builds](https://cef-builds.spotifycdn.com/index.html)
- [CEF 源码与 LICENSE](https://github.com/chromiumembedded/cef)
- [CEF 官方 CMake 示例工程](https://github.com/chromiumembedded/cef-project)
