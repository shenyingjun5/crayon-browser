# macOS 本地构建 adapter

## local-acceptance

- 产品：蜡笔浏览器，版本来自根 `CMakeLists.txt`；原生 macOS arm64。
- 唯一入口：`python3 scripts/build_macos_local.py --cef-root <已校验的固定 CEF 根目录> --flavor Debug`；可显式选择 `Release`。
- 使用仓库既有 CMake/Cargo 生产图、锁定 Cast-SDK 和 CEF，以及预设的 Ninja generator。macOS 系统 Make 3.81 会把 Helper 名称末尾的括号误作 archive member，不能用作该 App 图的替代 generator。先由 `DownloadCef.cmake` 校验官方 archive，再解压使用，不能使用任意来源的 CEF。
- 入口检查工具、磁盘、SDK revision/干净状态、CEF version；冻结工作区源码摘要；每阶段及结束核对摘要，首次失败停止，30 分钟预算。不修改版本或发布状态。
- 产物：对应 `.cache/build/macos-arm64-cef-<flavor>-ninja/browser/cef-shell` 下的 `CrayonBrowser.app`；CMake 既有流程进行本地 ad-hoc 签名；入口执行 strict/deep 验签并记录可执行文件 SHA-256。
- 每次在 build 目录的 `local-builds/<attempt>/receipt.json` 保存独立记录与分阶段日志，不覆盖失败证据，状态只表示 `BUILD_VERIFIED_NOT_ACCEPTED`。资格验收另外在同一实际 App 上执行启动/关闭、新标签页、网页 Markdown、本地 Markdown 编辑和 LAN 投屏；测试失败不能宣称 QUALIFIED。
- 不覆盖 `/Applications` 既有应用或用户 Profile；通过独立测试 Profile 启动生成的应用，保留用户数据。无正式签名凭证、Keychain 秘密读取、公证、上传、发布或外部 SDK 修改。

## formal-release

本 adapter 不提供正式构建或发布。正式签名、公证、发行包、SBOM、平台和设备矩阵按 QAR/REL 独立执行；本地 Debug/Release 产物不能晋升为正式候选或声称已发布。Debug 的纯逻辑证据仅能在源和依赖一致时复用；优化、正式签名、安装/升级/回滚及完整产物验收仍需最终字节独立证据。
