# 受限 LAN 媒体预检 Roadmap

本计划承接 `PLT-M05b4` 的生产私网预检阻塞；切片属于既有 PLT 顶层任务，不增加 297 项统计。用户于 2026-09-03 授权按受限 LAN 方案推进，并允许以公开在线视频补充人工真机测试。公网测试不替代本地确定性回归。

2026-09-03 后续决定：用户明确不处理代理特殊环境或接收端代检。以下 b4 的 Fake-IP 失败作为历史现场证据保留，不再要求修复该环境后才能继续 UI/普通 Direct；不改写为通过。当前继续顺序以[投屏体验重设计计划](cast-experience-redesign-roadmap.md)为准，其 R05/R06 已撤出队列；普通 Direct/Relay 的设备与生命周期验收仍保留。

## 1. 冻结边界与方案 Review

- 默认 `ProbeHttpClient` 仍拒绝非公网目标；不得在产品路径启用 `allow_private_addresses`。
- 首版只接受当前主页面与媒体 URL **同源的 RFC1918 IPv4 literal**，精确绑定规范化后的 scheme/IP/port/path/query；拒绝 URL 用户名/密码、媒体 fragment、跨源、域名、IPv6、loopback、link-local/metadata、CGNAT。域名私网支持后续独立评审，不引入 DNS 授权例外。
- 网络类型只校验范围，不签发用户权限。唯一授权 owner 为 app-runtime：仅 Browser 验证真实播放后的当前候选、用户明确选择的已发现设备、当前 tab/navigation/generation 的 `StartCast` 可以进入此分支。后台 `Decide`、纯 Network fact、Agent 输入不能签发例外。
- 单次检查消费不可复制的目标对象；只对该 URL 发 HEAD 和最多一次有界 Range，不跟随重定向、不访问 manifest 子资源/key/license、不携带凭证、不使用环境代理；最多 64 KiB 解析、15 秒整批 deadline。全量 URL 不进入 Debug、日志或公开 DTO。
- 产品授权至多 15 秒，预检与 SDK 提交拆开。预检期间 Cancel、导航、关页、退出、替换/停止投屏必须先撤销，丢弃 future；提交 SDK 前重验上下文、设备与期限。不能把取消 `spawn_blocking` 等同于撤销已开始的 SDK 副作用。
- 预检例外不扩大 Relay upstream allow-set、Direct/DRM/广告/设备能力策略或 Cast-SDK API，不开放通用 LAN 请求。
- 方案 Review（实现前）：需求/边界、所有权、SSRF/代理/重放与生命周期逐项核对；P0/P1 无设计遗留，`APPROVE`。实现尚未验证，不代表生产接线或真机通过。

## 2. 原子任务

| ID | 状态 | 依赖 | 单一目标 |
|---|---|---|---|
| PLT-M05b4b1 | VERIFIED | 用户方案授权 | 精确同源 LAN 目标校验与不可复制、限时、无代理的有界预检原语 |
| PLT-M05b4b2 | VERIFIED | b1 VERIFIED | 当前候选/设备授权、可取消预检与提交前重验的产品接线 |
| PLT-M05b4b3a | VERIFIED | b2 VERIFIED | 明确重试动作重新验证当前播放与候选，不让 Rejected 状态锁死 picker |
| PLT-M05b4b3b | VERIFIED | b3a VERIFIED | macOS 显示拒绝原因、重试入口和三语言反馈 |
| PLT-M05b4b4 | BLOCKED | b3b VERIFIED | 本地 Direct 闭环 PASS；公开 VP9 浏览器播放 PASS，系统 DNS 返回被禁止的基准测试地址，公网投屏拒绝；等待网络环境确认 |
| PLT-M05b4b3c | TODO | b3b VERIFIED | 核对真实 CEF 标题栏拒绝提示的视觉可见性与 picker 选择交互；不以 AX 文本存在代替截图可见 |

### b1 领取范围

- 输入：正式配置默认拒绝 RFC1918 的现有测试与现场证据；现有 HEAD/Range inspector。
- 允许：`crates/crayon-media-probe/src/{http,inspect,lan}.rs`、`lib.rs` re-export、该 crate 独立测试，本计划及 current 安全/架构和 PLT 索引。兼容调用方仅允许 `crayon-relay/src/network_guard.rs` 的新增封闭错误映射（首次上层编译已暴露 exhaustive match），不改变 Relay 网络策略。
- 禁止：生产启用 test hook、runtime/IPC/SDK/Relay 策略接线、GUI/品牌/本地化无关修改、外部仓库、依赖版本变更。
- 验收：先增加失败回归；`cargo test -p crayon-media-probe`、`cargo clippy -p crayon-media-probe --all-targets -- -D warnings`、`cargo fmt --all -- --check`、`git diff --check`；确定性测试覆盖同源/端口/地址类别/凭证/fragment/默认拒绝/有界 HTTP，真 LAN 流量归 b4。
- 明确不做：LAN 域名/IPv6、跨源媒体、manifest 子资源授权、自动播放、公开网站自动化依赖；本切片不解除 b4 真机门禁。

### b1 验证与 Review（2026-09-03）

- 对象：main/cfaab39 上 b1 工作区 diff，保留之前 CEF/LOC 未提交修改；macOS 26.6.2 arm64。
- 失败先证：空 Range 溢出、URL userinfo 未被拒绝，两项定向测试各 FAIL/101；新 LAN API 测试首次编译 FAIL/E0432。修复共用入口后全部通过。
- `cargo test -p crayon-media-probe`：PASS/0，56/56（27 unit、7 assess、12 http、6 inspect、4 LAN），6.91s；一轮沙箱监听失败为 PermissionDenied/101，申请本机 fixture 网络权限后复跑通过。
- `cargo clippy -p crayon-media-probe --all-targets -- -D warnings`：PASS/0，11.62s；`cargo fmt --all -- --check`、`git diff --check`：PASS/0。
- `cargo check -p crayon-app-runtime -p crayon-media-host`：PASS/0，3.02s。首次上层编译暴露新增错误的 exhaustive match，补封闭拒绝映射，不改 Relay 策略。
- `bash scripts/check.sh security`：PASS/0，17.48s，relay unit 3/3、security 7/7；`cargo run -p repo-guard -- scan --root .`：PASS/0，既有 RG-003/004 warning。
- Review：P0/P1/P2/P3=0/0/0/0，APPROVE；无新依赖/外部协议，精确 URL/no_proxy/无凭证与请求数量边界已核对，私有请求构造测试不连接网络。最高 VERIFIED：真实 LAN 成功路径与批次取消的产品证据归 b2/b4，未声称手机可播放。Release artifact N/A（未装配）。

### b2 领取范围

- 单一目标：把用户 StartCast 拆成可取消预检与不可重复的 SDK 提交，只有此预检可消费同源 LAN 目标。
- 输入/依赖：b1 VERIFIED；MHV1 当前候选/设备查找、现有有界 pending queue、既有 CastUsecase。
- 允许：app-runtime 媒体规划/host 及独立测试、media-host 调度及独立测试、本计划；tokio 已有 dev dependency 启用 test-util 仅用于确定性 deadline 测试。集成 Review 追加 probe 共用 IPv4-mapped IPv6 分类修正与回归：LAN 构造拒绝后回到默认 probe 时，不能由 mapped IPv6 绕过同一私网约束。
- 禁止：IPC wire/schema、CEF 播放证明、SDK、Relay allow-set、UI（归 b3）、Keychain、自动播放。
- 验收：`cargo test -p crayon-app-runtime`、`cargo test -p crayon-media-host`、相关 clippy、format、security、repo-guard；当前绑定、失效/导航/关闭/取消/保护升级/设备切换无后续 SDK 调用、队列有界、默认 Decide 不获权。装配变更之后执行 Mac Debug/Release build + 完整 CTest；平台未补证前最高 IMPLEMENTED。
- 不做：SDK 已提交后的抢占式取消、LAN 域名/跨源授权、真机通过宣称；SDK session stop 继续走既有 owner。

### b3a / b3b 原子范围（领取前冻结）

- b3 涉及共享状态恢复与平台/资源展示，分为两个独立可审查切片；没有增加顶层任务。
- b3a 输入：现场 Rejected + button Eligible 导致 OpenPicker 永远失败。允许 CastFeatureViewModel/Coordinator 的头和实现及共享/壳 controller 独立测试；不改变 Browser proof、MHV1、网络或 SDK。显式 ack 只能回 Browsing，只有 coordinator 当前 verified button 才允许重新进入 picker，不自动重试或直接复用旧接收端。
- b3a 验收：先复现 reject 后 OpenPicker 失败，撤回 eligibility/页面关闭后不能重试；macOS Debug/Release 构建 `crayon_browser_cast_view_test`、`crayon_cast_ui_coordinator_test`、`crayon_cast_shell_controller_mac_test`，`ctest --test-dir <build> --output-on-failure -R '^(cast_feature_view|cast_ui_coordinator|cast_shell_controller_mac)$'`；changed-line clang-format、repo guard、diff check。正式 UI 门禁由 b3b 补证。
- b3b 输入：b3a VERIFIED；已存在的 macOS playback status surface 与统一 product-strings owner。允许 macOS cast chrome/header/app、product-strings、三语言 catalog 与确定性生成物及对应测试；共享 feature 仅同步拒绝状态的 message key 映射，不变更其状态机。不改 Windows adapter、SDK/probe、主题或无关布局。明确区分一般拒绝、无路由、DRM；错误持续显示到显式重试或页面失效，不伪装外部客户端已可用。
- b3b 验收：AppKit 独立测试断言拒绝反馈可见/可访问、重复 Render 不重复弹框、重试/撤销/跨窗口清理；catalog parity/generator 与 product-strings 测试、Debug/Release 完整 build/CTest、format、repo/release scan、Review。实际可信播放归 b4。
- b4 才进行真实可信播放。公开视频选择无需账号、DRM 或签名凭证的测试素材，分别记录浏览器可播、资源可探测、设备可解码和真实首帧，失败不伪造 capability 或更换安全策略。

### b4 领取范围（依赖通过后启动）

- 输入：b3b VERIFIED 的 macOS bundle、用户授权使用的 ADB 在线正式接收端、既有 600 秒 VP9 MP4 LAN fixture、下表 W3C 公共媒体。
- 单一目标：补真实产品 Direct 首帧/播控/停止证据，并记录公开媒体兼容性，不以网络可达替代成功投屏。
- 允许：忽略目录内固定路由的本地人工 fixture、Mac 产品原生 UI、ADB 接收端状态/截图、本计划与 PLT 索引证据。页面播放由用户亲自触发；不得自动点击播放、注入 proof、修改 SDK 或生产网络安全策略。
- 验收：先本地同源 LAN 页面真实播放→用户选定接收端→HEAD/有界 Range→SDK Direct→接收端首帧，随后暂停/恢复/seek/停止；公开 VP9/AVC 分别记录 HTTP、浏览器播放、预检/策略、接收端结果。人工命令/截图仅保留去敏摘要，设备标识与本机地址不进入版本控制。
- 不做：下载产品功能、公共网站自动化依赖、HLS/DRM 绕过、账户登录、Keychain、Windows 替代认证、签名公证、上传/发布。任何新代码缺陷另拆原子任务并 Review，失败如实记 BLOCKED。

### b2 验证与 Review（2026-09-03）

- 对象：main/cfaab39 上 b2 工作区 diff；macOS 26.6.2 arm64。StartCast 使用 15 秒、候选/tab/navigation/generation/device/revision 绑定；预检无 SDK 副作用，ready 对象不可复制，提交前重验；导航/关闭/Cancel/退出/换设备/Stop/EME/凭证收紧撤销，排队普通事实提交前应用并重验广告连续性。输入优先且单批最多 64 条，不让消息洪泛饿死取消。
- 失败先证：预检撤销 API 首次测试 E0599/101；集成 Review 的 mapped IPv6 分类测试 FAIL/101（`::ffff:192.168.0.1` 被误判公网）。修正共用分类后 literal 和 DNS pin 共用同一 IPv4 策略，未改变 LAN 例外范围。
- `cargo test -p crayon-app-runtime -p crayon-media-host -p crayon-media-probe`：PASS/0，149/149（runtime 86、host 7、probe 56），首轮 62.56s；最终 mapped 修正后完整复跑仍 149/149、exit 0。10 类提交前撤销（含 credentials/ad-policy/expiry）定向复跑 PASS。所有自动化仅本机 fixture，未联网公共站点。
- `cargo clippy -p crayon-app-runtime -p crayon-media-host -p crayon-media-probe --all-targets -- -D warnings`：PASS/0（首轮 22.63s，最终 12.69s）；`cargo fmt --all -- --check`、`git diff --check` PASS/0。
- `cmake --build .cache/build/macos-arm64-cef-debug -j4`：PASS/0，初轮 9.53s；最终 mapped 修正后重建 PASS（耗时输出未保留）。`ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure`：初轮 91/91、202.20s，最终 91/91、167.23s；最终真实 CEF integration 124.47s。
- `cmake --build .cache/build/macos-arm64-cef-release -j4`：PASS/0、97.86s（既有 Ninja recovering warning）；`ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure`：PASS/0、91/91、254.83s，真实 CEF integration 135.42s；包含 mapped 修正。
- `bash scripts/check.sh security`：PASS/0、20.95s（guard、legacy relay 3/3、security 7/7）；最终 `cargo test -p crayon-relay --test network_guard`：PASS/0、7/7、12.70s，重定向/固定地址/allow-set/敏感头边界未放宽。
- `codesign --verify --deep .cache/build/macos-arm64-cef-release/browser/cef-shell/Release/CrayonBrowser.app`：PASS/0。首次 artifact-path 错指 app 本身，RG-006 PASS、RG-009 缺 app 同级 sidecar，exit 1；按既有 MDV-20 发布目录契约在临时 staging 复制 app + NOTICE/SPDX/manifest 后，`cargo run --quiet -p repo-guard -- scan --root . --artifact-path <staging>`：PASS/0，RG-006/009 全过；没有修改/上传正式发布物。
- Review：按需求→正确性→API→生命周期→安全→性能→证据→维护核对，P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。新增 opaque Rust 内部阶段类型不变更 MHV1/Agent/SDK wire；无测试代码进入产品，无新第三方依赖。长函数为既有 command dispatch/矩阵测试，未跨领域拆 owner。
- 未覆盖：真实 LAN/手机首帧与播控归 b4；拒绝 UI P1 归 b3a/b3b，当前整体投屏尚不能宣布 DONE。Windows、长稳、Developer ID/公证、Keychain 未运行；既有 QAR-10 strict 签名风险不在本切片扩大处理。

### b3a 验证与 Review（2026-09-03）

- main/cfaab39 工作区，macOS arm64；先运行 `cmake --build .cache/build/macos-arm64-cef-debug --target crayon_cast_ui_coordinator_test crayon_cast_shell_controller_mac_test -j4` 成功，随后两项 CTest 均 FAIL/8（1.91s）：`rejected.OpenPicker()` / `controller.ActivateCastButton()` 稳定复现锁死。
- 新增 `AcknowledgeRejection()` 只回 Browsing；coordinator 仅在当前 button Eligible 且显式 OpenPicker 时确认拒绝并重新验证。没有自动重投，没有伪造 Browser proof 或沿用旧接收端。
- 按领取范围中的双配置三个 target build + 三项 CTest：PASS/0，Debug 3/3、1.51s；Release 3/3、1.12s。覆盖 ack 本身不能 OpenPicker、proof 撤回/页面关闭拒绝重试、恢复后重新发现、零假 session。
- changed-line clang-format、`git diff --check`、`cargo run --quiet -p repo-guard -- scan --root .`：PASS/0（既有规模/硬编码 warnings）。Review P0/P1/P2/P3=0/0/0/0，APPROVE；最高 VERIFIED。未改网络/协议/SDK，正式 AppKit 展示及整包复验证据归 b3b；真机归 b4。

### b3b 验证记录（2026-09-03）

- 对象：main/cfaab39 工作区，macOS 26.6.2 arm64；保留先前投屏码/播控/CEF 改动。本切片仅加入封闭拒绝原因、原生状态显示与重试标签、统一三语言资源，不更改播放授权或网络策略。
- 失败先证：`cmake --build .cache/build/macos-arm64-cef-debug --target crayon_cast_chrome_mac_test -j4` 后 `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure -R '^cast_chrome_mac$'`：FAIL/8，2.21s，拒绝状态文案不可见。修复后定向 AppKit PASS；补 DRM/general/gate、重复 Render 无 sheet、重试清理、跨窗口和页面失效清理断言。
- `node tools/locales/generate.mjs` 与 `--check`：PASS/0，3 locales、159 keys、9 outputs。`node --test tools/locales/generate.test.mjs` 初次 5/6、旧数量 156 断言 FAIL/1；同步 generator 与 C++ catalog 固定预期 159 后 PASS/0、6/6（0.144s），未放松 parity 或确定性要求。
- `cmake --build .cache/build/macos-arm64-cef-debug -j4`、Release 同命令：PASS/0（build 耗时未保留）；Debug 完整 `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure`：PASS/0、91/91、192.73s，其中真实 CEF integration 148.18s。仅格式调整后 Debug 再 build PASS，共享状态/本地化六项复跑 PASS/0、1.19s。
- changed-line clang-format dry-run/Werror、`cargo fmt --all -- --check`、`git diff --check`：PASS/0。`cargo run --quiet -p repo-guard -- scan --root .` PASS/0，既有 RG-003/004 warnings；Release `codesign --verify --deep <app>` 与 app + 三个 sidecar 临时 staging 的 `cargo run --quiet -p repo-guard -- scan --root . --artifact-path <staging>`：PASS/0，RG-006/009 通过，无发布/上传。
- `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure`：PASS/0、91/91、217.39s，真实 CEF integration 117.60s，包含最终格式化代码。Debug/Release GUI 测试串行运行，无产品真机操作混入。
- Review：按需求/边界→正确性→架构/API→生命周期→安全/隐私→性能→测试→维护核对，P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。封闭原因枚举不含 URL/设备秘密，持续文案无 modal 重入，回调后重取 surface，失效/跨窗口隐藏、关闭后回调安全已覆盖。原生已有单函数场景测试规模仍低于 3000 行，不为此引入新测试框架。
- 未覆盖与风险：真机首帧/播控、实际长文案视觉与辅助功能操作归 b4；Windows、长稳、正式 Developer ID/公证/Keychain 未运行。Ad-hoc 完整性不等于正式签名发布门禁。现在仅领取 b4，父 PLT-M05b4 仍等待完整设备闭环。

### b4 当前现场记录（2026-09-03）

- 最终 Debug `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure -R '^cast_chrome_mac$'`：PASS/0、1/1、3.23s（仅格式化后重新确认）。随后停止所有测试 UI，再启动真实产品，不与 CEF Harness 混跑。
- `node <ignored-fixture>/server.mjs`：固定 LAN 页面/MP4 路由已就绪；新增两条仅人工使用的固定 W3C VP9/AVC 原生 video 页面，无 autoplay/脚本 proof、无账号和代理。`adb devices -l`、选定设备 `shell ip addr show wlan0`、设备描述 HTTP GET 均成功，descriptor HTTP 200；正式 receiver 通过 launcher 置前台，UI dump 确認接收端就绪。
- computer-use 已打开最新 Debug 产品并实际导航到本地 fixture。截图显示彩条首画面及 `0:00 / 10:00`、原生 Play，尚未产生可信播放；本机 GET/Range 加载成功不等于 runtime 预检或手机首帧。
- 已请求用户亲自点击页面 Play。当前 BLOCKED 仅为真实用户动作；没有代点/注入播放证明，也没有触碰 Keychain。fixture 和浏览器保留待继续，未上传/发布/提交。
- Direct 预检、手机首帧、暂停/恢复/seek/停止、公网样片实际播放：NOT_RUN。下一步收到用户播放确认后继续 b4；父任务及 b5/b6/M05c 不越过真实设备门禁。
- Review：准备范围内 P0/P1/P2/P3=0/0/0/0，APPROVE；本任务最高 BLOCKED，不把材料准备视为验收通过。

### b4 用户真实播放后的 Direct 验证（2026-09-03）

- 对象：main/cfaab39 工作区最新 Debug bundle，macOS 26.6.2 arm64；ADB 正式接收端为 Android 5.1 金立手机。只记录设备类别，不提交序列号/IP/投屏码。用户亲自点击原生 Play，页面时码推进并出现“选择接收端”；未使用自动点击或注入证明。
- 原生 picker 首次发现列表为空；已存在且核验的正式投屏码经原生“连接”成功解析手机并启动投屏。本轮证明投屏码连接，不把空列表算自动发现通过（既有发现证据另存）。
- 本地 fixture 日志：选择设备后 HEAD 200、Range `0..4095` 206，随后接收端 IP 直接 GET MP4 200；无 Relay URL、无全局私网开关。桌面显示停止/暂停/秒/跳转控件；`adb -s <selected> exec-out screencap -p` 采集的手机画面确实显示彩条视频与递增时码，Direct 首帧 PASS。
- 原生“暂停”回执后按钮变“继续”；手机显示暂停提示，19:53:07 与 19:53:21 的两张 PNG SHA-256 完全一致（间隔 14 秒、画面时码 40.542s）。原生“继续”后手机画面变化、时码推进到 44.836s，暂停/恢复 PASS。
- 原生秒数栏输入并核对 `100` 后点击“跳转”，手机截图时码约 104.333s（命令执行至截图存在采集延迟），没有控制失败提示，seek PASS。原生“停止投屏”后桌面播控收起，手机退出视频返回正式接收端“已就绪，等待投屏”，stop PASS。
- 上述 ADB 截图命令均 exit 0；`shasum -a 256 <paused-a> <paused-b>` exit 0；证据保存在忽略目录下 `receiver-direct-first-frame/paused-a/paused-b/resumed/seek100/stopped.png`，不上传。Android `dumpsys media_session` 没有本接收端可用 playback state，本次仅以真实截图和原生回执取证，不冒充 MediaSession 证据。
- 本地无声 VP9/MP4 的首帧/播控闭环 PASS；音频、公开媒体、自动发现本轮空列表、Release 真机、Relay/DRM/长稳仍未闭合，不将此扩大成全部格式或一期发布通过。公开 VP9 页面已完成 metadata 加载（原生控件 `0:00 / 1:00`），已请求用户真实点击 Play，当前 BLOCKED 为该人工动作，不是 Keychain 或 LAN 预检。
- Review：验证路径遵守 b4 范围，无生产代码/SDK 改动，无 Keychain/凭证/上传；现有证据可接受，APPROVE。本原子任务尚未 DONE，公开视频矩阵仍需完成。

### b4 公开 VP9 实测与网络阻塞（2026-09-03）

- 用户再次真实点击 Play；CEF 展示 W3C BBB 视频运动画面，最终控件 `1:00 / 1:00`，Browser 签发投屏入口：浏览器真实播放 PASS。原生发现列表本次出现 6 个接收端，含前述正式手机，补充了前一轮空列表之后的自动发现成功证据。
- picker 的 AX 点击/方向键选择后选中项仍是列表首项，未对错误接收端提交；改用已核验的手机投屏码，成功解析为目标接收端。随后产品进入“没有可用的投屏方式，可尝试其他接收端。”/“重新选择接收端并重试”，手机截图仍为接收端主页：公网投屏尝试 FAIL（不是手机解码 FAIL，未建立首帧证据）。
- `curl --noproxy '*' --head --max-time 15 --silent --show-error https://w3c.github.io/webcodecs/samples/data/bbb_video_vp9_frag.mp4` exit 0、HTTP 200；但 `dscacheutil -q host -a name w3c.github.io` 和 Node `dns.lookup("w3c.github.io", {all:true}, callback)` 均 exit 0，返回同一 `198.18.0.0/15` 基准测试网段地址。该地址属于 probe 明确禁止范围；普通 curl 可达不表示通过产品 DNS/IP 安全校验。
- 源码确认：`ProbeHttpClient::prepare` 对系统解析的全部地址先 `validate_resolved_inner`，命中基准网段即拒绝；runtime 将失败收敛为 Unknown，策略在外部交接不可用时返回 CapabilitiesUnavailable，与当前 UI NoRoute 一致。此为确定的网络前置阻塞；Fake-IP/代理 DNS 为环境原因推断，未读取或修改具体代理软件设置，也未放宽地址校验或绕过系统路由。
- 原生 AX 可读拒绝文案和重试标签，但完整窗口截图未显示对应长文案，不能把自动化/AX 通过等同于真实可视反馈通过；另记 `b3c TODO` 核对布局。picker 选择的 AX 操作差异也归 b3c 人工交互核对，尚未定性为产品鼠标选择缺陷，不夹带代码修改。
- Review：本轮证据采集范围无 P0/P1 变更风险，APPROVE；产品视觉未覆盖/疑点跟踪 b3c，公网路径仍 BLOCKED。下一步需用户确认如何暂时关闭 Fake-IP/TUN 或恢复真实 DNS 后复验；系统网络设置变更需动作时授权。H.264/AVC、音频、Release 真机及后续 Relay/长稳仍 NOT_RUN。

### b4 后续方向调整：整体投屏体验方案（2026-09-03）

- 用户随后要求处理代理环境、直接发送域名媒体地址、入口紧邻网址框、多视频明确选择，并评估播放器悬浮按钮；统一进入[投屏体验重设计 Roadmap](cast-experience-redesign-roadmap.md)。当前仅做方案，不放宽 probe/Relay 策略。
- 上一段“等待用户关闭 Fake-IP/TUN 或恢复 DNS”仅保留为历史诊断建议，不再作为产品默认解决方式。实际待解决的是本机预检与接收端可达性混淆，以及缺少受控接收端 URL 评估的正式能力；旧接收端的能力限制必须明示。
- b4 公网路径继续 BLOCKED，原本地 Direct 与 b1/b2/b3a/b3b 证据保留；代理原因分层/正式 SDK 路径/真机复验由 R03/R05/R06/R11M 承接。b3c 的错误布局和 picker 核对纳入 R08M，尚未完成，不重复计为已解决。
- 本次不修改系统网络设置、SDK 外部仓库、生产代码或用户已有改动；真实播放、签名、Keychain 与发布边界不变。

## 3. 公开在线视频候选（人工补充，不进入自动化依赖）

来源：[W3C WebCodecs 官方 samples/data](https://github.com/w3c/webcodecs/tree/main/samples/data)。选择原生 video 加载的媒体文件，不把 WebCodecs canvas 示例当作 currentSrc 页面证据。

| 素材 | 本次可达性检查 | 浏览器真实播放 / 手机首帧 |
|---|---|---|
| [BBB VP9 fragmented MP4](https://w3c.github.io/webcodecs/samples/data/bbb_video_vp9_frag.mp4) | curl HEAD 200；系统 DNS 却返回基准测试网段，被产品安全策略拒绝 | CEF 用户真实播放 PASS（1 分钟）；投屏尝试 FAIL/NoRoute，手机首帧未出现；需真实 DNS 环境复验 |
| [BBB AVC fragmented MP4](https://w3c.github.io/webcodecs/samples/data/bbb_video_avc_frag.mp4) | 同命令：exit 0，HTTP 200、video/mp4、Accept-Ranges: bytes | NOT_RUN；用于 H.264 浏览器/手机兼容性对照 |

用户无需登录或提供凭证。媒体从官方站点流式加载，不上传本地文件、不自动点击播放；public 可达性受网络影响，不替代 LAN fixture 与 DRM/凭证/重定向拒绝门禁。
