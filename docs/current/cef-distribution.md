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

## 本地自建专有 codec 构建（C20g，2026-09-27，内部测试用）

- 状态：**内部测试通过**。用户/公司完成 H.264/AAC 专利许可（如 Via LA 池）并过发布门禁**之前**，本构建产物不得对外分发；仅限本机与内部开发验证。
- 源：CEF `7871` 分支 @ `8042e43d20cca43f182c3fc72e762b000f6ee22f`（与官方 `150.0.10+g8042e43+chromium-150.0.7871.101` standard 包完全同源）。与官方包的全部差异 = 下方 GN 参数与 1 个本地源码补丁。
- 构建：`automate-git.py --branch=7871 --checkout=8042e43… --arm64-build --no-debug-build`，`GN_DEFINES="proprietary_codecs=true ffmpeg_branding=Chrome symbol_level=0 use_libcxx_modules=false use_clang_modules=false mac_sdk_path=/Users/shenyingjun/Work/cef-build/MacOSX27-patched.sdk"`。构建脚本：`~/Work/cef-build/run-automate-release.sh`。
- **Xcode 27（macOS 27 SDK）四个适配点**（均为构建机环境适配，不改变产品语义）：
  1. `use_libcxx_modules=false` + `use_clang_modules=false`：SDK 27 的 DarwinFoundation modulemap 与 Chromium 150 自带 clang 23 的 C++ modules 不兼容。
  2. `mac_sdk_path` 指向修补副本 `~/Work/cef-build/MacOSX27-patched.sdk`：SDK 27 的 `.tbd` 引入 `arm64e.x1` 架构，lld(llvm-23) 无法解析；副本中 10791 个 `.tbd` 已剔除该架构标记。回到生产正确的 lld 链接（`use_apple_linker` 会导致 allocator shim 双注册，产品不出窗，已弃用）。
  3. 本地源码补丁 `~/Work/cef-build/local-patches/xcode27-seatbelt-const.patch`：SDK 27 从 `<sandbox.h>` 移除 `kSBXProfile*` 声明（libSystem 仍导出符号），在 `sandbox/mac/seatbelt.cc` 补 extern 声明。
  4. `xcodebuild -downloadComponent MetalToolchain`：Xcode 27 将 Metal 编译器拆为独立组件（ANGLE 着色器编译需要）。
- 产物：`cef_binary_150.0.10+g8042e43+chromium-150.0.7871.101_macosarm64.zip`，symbol_level=0 无 dSYM。SHA-256：
  - 双 flavor 完整版（Debug+Release，2026-09-27 交付版）：`4a120088de1efb697ac68022b07b65819b541c6a21db53d198dfca941951a85c`
  - 中间版（Release-only，lld）：`b398baf1b2a3ae9045f9872ef1ab703f889d711c001a93ff6ce6b75aba066093`
  - 中间版（use_apple_linker，已弃用）：`27ab82d2…aa007`（全文见 `~/Work/cef-build/local-patches/artifact-sha256.txt`）。
- Debug flavor 已单独构建并接入日常 Debug 壳（渲染进程树与产品功能实机核验通过，2026-09-27）；Doxygen 未装导致 docs 发行失败，无影响。
- 消费方式：解包后经 `scripts/build_macos_local.py --cef-root <解包目录>` 使用（`CRAYON_CEF_LOCAL_ROOT` 走结构性校验，不比对官方 SHA-1）。
- 端到端验证（2026-09-27，隔离实例 `CRAYON_CEF_ROOT_CACHE_PATH` + 首窗加载本地探针页）：`canPlayType` 结果 **H264=probably、AAC=probably、VP9=probably**；主进程/渲染进程/网络全链路正常。上一次 use_apple_linker 构建中曾出现 `sessions::CommandStorageBackend::AppendCommands` CHECK 崩溃一次，lld 重建后未复现（观察项）。
- 未决：Debug CEF 构建补齐（供日常 Debug 壳）；对外分发前的专利许可与门禁；Widevine CDM 另行决策。

## 上游资料

- [CEF Automated Builds](https://cef-builds.spotifycdn.com/index.html)
- [CEF 源码与 LICENSE](https://github.com/chromiumembedded/cef)
- [CEF 官方 CMake 示例工程](https://github.com/chromiumembedded/cef-project)
