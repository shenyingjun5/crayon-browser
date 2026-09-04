# 活跃 Roadmap 索引

本目录只保存当前可执行的模块 Roadmap。领取任务前必须先读仓库根 `AGENTS.md`、`docs/current/README.md`、总 Roadmap 和所属模块 Roadmap。一次只领取一个满足依赖的原子任务。

## 1. 当前产品范围

- 产品是面向 AI Agent 定制的浏览器；Windows/macOS CEF 浏览器与局域网 Direct/Relay 投屏优先。
- 无视频推送路由时只交接给独立蜡笔投屏客户端；浏览器不做 WebRTC、采集或编码。
- 浏览器和投屏主链路完成后建设当前页数据面与确定性 Markdown；CAAP 协议/权限内核可在浏览器后半段先行。
- HarmonyOS 只规划鸿蒙电脑 PC 形态技术预览。
- 第一期只发布网页 Markdown、LAN Direct/Relay 投屏、本地 Markdown 编辑三大闭环，并要求同一桌面候选包支持 `en-US/zh-CN/zh-TW` 跟随系统；本地化是三闭环的横切发布质量，不是第四条业务闭环。CAAP、CLI/入站 MCP、高性能读页、语义动作、Workflow/Challenge、Capability Hub、Partner 与模型仍是产品方向，但统一进入第二期且默认关闭。Linux 没有当前活跃 Roadmap。
- 出站 Partner API/MCP 与入站 MCP 是不同安全边界；Partner/TV Cast Manifest 属于 Cast-SDK/接收端协议，不在浏览器内复制实现。

## 2. 权威入口

- [总 Roadmap](../crayon-private-cast-browser-roadmap.md)：阶段、依赖、总数与当前领取顺序。
- [当前契约索引](../current/README.md)：PRD、架构、测试和 Code Review 契约。

## 3. 模块索引

| 模块 | Roadmap | 当前目标 | 关键起点 |
|---|---|---|---|
| REL | [release-v1-roadmap.md](release-v1-roadmap.md) | 第一期网页 Markdown、LAN 投屏、本地 Markdown 编辑三大闭环与发布范围 | `REL-01/02/05 DONE`；Windows x64 首发候选顺序已冻结 |
| BRD | [brand-assets-roadmap.md](brand-assets-roadmap.md) | 品牌图标母版、跨平台确定性资产与接入门禁 | `BRD-01..04 DONE` |
| FND | [foundation-migration-roadmap.md](foundation-migration-roadmap.md) | Workspace、契约、质量入口与仓库基线 | 20 个原子任务 `DONE` |
| MED | [media-policy-relay-roadmap.md](media-policy-relay-roadmap.md) | 媒体观察、策略、LAN Relay、外部客户端交接迁移 | `MED-01..19 DONE` |
| CEF | [desktop-cef-browser-roadmap.md](desktop-cef-browser-roadmap.md) | Windows/macOS CEF 壳、共享 UI、媒体观察和 IPC | `CEF-01..15 全部完成`（`CEF-06..14` 模型层 VERIFIED，实机接线归后续装配/切片任务）；Windows 总 Review 证据已补齐 |
| BUX | [browser-product-experience-roadmap.md](browser-product-experience-roadmap.md) | Chrome-inspired 蜡笔桌面浏览器 UI 与日用基础功能 | `BUX-01..18 DONE`（BUX-17/18 2026-08-26） |
| LOC | [localization-roadmap.md](localization-roadmap.md) | `en-US/zh-CN/zh-TW` 跟随系统、统一资源/解析器、CEF/平台装配与发布验证 | `LOC-01/03/04/05W/06W DONE`、`LOC-02 VERIFIED`、`LOC-07W BLOCKED`、`LOC-08M VERIFIED`；macOS arm64 双配置各 91/91 与本地 artifact 已补证，真实三语言交互归 LOC-09M |
| MDV | [markdown-viewer-roadmap.md](markdown-viewer-roadmap.md) | 本地 Markdown Runtime：查看/编辑/保存、图标工具栏、图片与 Mermaid Full 离线扩展 | `MDV-20/20W/25 DONE`；`MDV-24 VERIFIED`，24W 已记录支持矩阵，Narrator/IME/原生 DPI 等未覆盖项仍保留 |
| MRT | [markdown-runtime-roadmap.md](markdown-runtime-roadmap.md) | Markdown Runtime Extension Framework：闭合扩展 API、Highlight/KaTeX 与后续图表/演示门禁 | `MRT-01..09 DONE`（09 为 Windows 首发口径，macOS addendum 待补）；`MRT-10..19` 属第二期 |
| SDK | [cast-sdk-integration-roadmap.md](cast-sdk-integration-roadmap.md) | 固定源码 Cast-SDK facade、发现、连接和控制；后续 Partner Cast facade | `SDK-01..14 DONE`；`SDK-15/16` 等 HUB/外部已批准 API |
| PLT | [desktop-platform-adapters-roadmap.md](desktop-platform-adapters-roadmap.md) | Windows/macOS 存储、网络、生命周期、更新和客户端交接 | `PLT-01/02/W04/M04 DONE`；`PLT-M05 IN_PROGRESS`（macOS 后续切片暂缓），`PLT-W05a/W05b/W05c0 DONE`、`W05c BLOCKED` |
| PLT 内部切片 | [desktop-shell-roadmap.md](desktop-shell-roadmap.md) | 自定义外壳＋Alloy；一期全功能迁移、可替换内容视图与双平台门禁 | `PLT-SHELL-00/01 VERIFIED、02 READY`；默认产品未切换，不重复计入顶层总数 |
| PRV | [privacy-security-roadmap.md](privacy-security-roadmap.md) | Profile、隐私、安全、日志和删除语义 | `PRV-01..12` 已完成或 VERIFIED；一期核心 `PRV-13A`、第二期扩展 `PRV-13B` |
| CNT | [content-intelligence-roadmap.md](content-intelligence-roadmap.md) | 页面数据/Markdown 与第二阶段模型总结 | C1 数据面 `CNT-01..10 DONE/VERIFIED`；一期产品装配 `CNT-17..20 DONE`，`CNT-21W` 等 `PRV-13AW` 后总 Review；`CNT-11..16` 第二期 |
| AGT | [agent-access-roadmap.md](agent-access-roadmap.md) | CAAP、tool registry、CLI/MCP、高性能读页和授权操作 | A0 完成；`AGT-07/15 VERIFIED`，`AGT-12C/13/14` 按装配依赖后续推进 |
| ACT | [semantic-action-roadmap.md](semantic-action-roadmap.md) | Page/Action/Form/Media/Risk Map、action_id、前置条件和效果验证 | `ACT-01..12 全部完成`（2026-08-30，ACT-12 总 Review GO）；实机接线归后续装配切片 |
| WFL | [workflow-learning-roadmap.md](workflow-learning-roadmap.md) | Challenge 接管、Workflow Learning、个人 Site Skill、健康与受控修复 | `WFL-01/02/03/04/06/07 VERIFIED` |
| HUB | [capability-hub-roadmap.md](capability-hub-roadmap.md) | Capability Registry/Router、入站发现与出站 Partner connector | `HUB-01..06 DONE`；`HUB-07+ 待依赖` |
| HM | [harmony-browser-roadmap.md](harmony-browser-roadmap.md) | 鸿蒙电脑 PC 形态 ArkUI/ArkWeb 技术预览 | `HM-01`，后续启动 |
| QAR | [quality-release-roadmap.md](quality-release-roadmap.md) | Windows/macOS 构建、真实设备、性能、长稳和发布门禁 | Windows 核心 `QAR-02AW/05AW/08AW`；第二期 feature `02B/05B/08B`；macOS 特有门禁后置 |
| RNM | [naming-migration-roadmap.md](naming-migration-roadmap.md) | `get-video` → `crayon-browser` 仓库、包、README、GitHub 与本地路径迁移 | `RNM-01..08 DONE` |

当前共 297 个活跃任务，212 个唯一当前测试用例。新增任务来自 `REL-01..05`、`CNT-17..21`、`PRV-13A/B` 对原 PRV-13 的分拆、`QAR-02/05/08` 的 A/B 分拆、REL-02 发现的 `MDV-25` 生产 fixture 清理、规范治理 `FND-13`，以及三语言本地化 `LOC-01..10`；PLT-M05/PLT-W05 和 Windows-first W slices 为既有顶层任务的内部原子切片，不重复计数。LOC 复用现有 UX/RG 用例，不新增唯一测试 ID；MDV 的 `MDV-14..20` 专注 Mermaid Full，`MDV-21..24` 专注编辑器工具栏，`MDV-25` 负责 Release 生产隔离；MRT-09 P0 Runtime 总 Review 属于一期，MRT-10..19 属第二期。Linux、浏览器 WebRTC/采集/编码仍不计入活跃范围。

## 4. 当前领取队列

### 自定义外壳＋Alloy（2026-09-04 最新决策）

- 用户批准长期自定义 Shell＋Alloy，一期同步调整；[PLT-SHELL](desktop-shell-roadmap.md) 是当前宿主迁移队列，REL §5 是一期总依赖。00 方案与 01 命令 owner VERIFIED：Debug/Release 无 GUI 契约各 1/1、Debug 连续 3 次、ASan/UBSan 通过；关闭重入修复与启动超时原始证据见 §9。下一步 02 内容视图契约 READY、03M 本地 Alloy 宿主与复用窗口 Harness，再逐项接线。默认产品未切换，Windows 独立验收与首发政策不变。
- 原 R02b/b2 LOCATION 多 Chrome view 方案及“等待选择宿主”由本决定取代；旧记录保留为历史，不标完成、不继续原路线。R08 候选宿主接线不等待最终默认切换，防止循环依赖。
- 浏览器日用基线、三闭环、三语言和原隐私/设备/发布门禁全部保留。新增宿主相关证据不能复用旧 Chrome UI 通过结论；其他 WebView/Chrome 特殊容器只预留边界，不在一期伪造多引擎支持。
- 测试先无 GUI，后续 Alloy Harness 同进程/专用窗口复用标签；不接管用户日用窗口，不默认抢焦点。需要前台输入/重启新原生二进制的例外须提前说明。

### 投屏体验重设计（2026-09-03）

- 2026-09-04 用户要求继续 Mac 一期：独立推进 R04c1 MHV2 Hello/Welcome 固定字节、Rust/C++ codec 与共用 golden，状态 VERIFIED。启动延迟后原样复核，新协议 Rust 3/3、新旧 C++ Debug/Release 各 2/2、Release 媒体 5/5；双配置 C++ build、Rust build/clippy、格式/guard 通过，未启用产品握手或投屏能力。此前超时保留在 §18，不再当作当前永久阻塞。剩余一期范围仍是下表的投屏产品/真机、本地化、总审与发布矩阵，不把第二期引入。
- 同轮无 GUI 回归：Debug 93/93 PASS（明确排除五项窗口测试）；Release 分段累计 33 项通过，另 page_markdown_export/chrome/cast_selection 无输出超时，余项未完成后停止。仍不能宣称整个工作区全绿或 R04a/b2/R08u2r 平台门禁已关闭，见 §18 的完整命令和中断证据。
- 2026-09-04 续测：R08u2r 测试就绪修正后 Debug/Release 原生入口专项各连续 8 次通过；本轮 Debug 无界面定向 8/8，Release 定向 4/8、4 项启动超时，采样的 observer 停在 `_dyld_start`。最终完整回归仍未闭合，R08u2r/R04a/R04b2 保留 IMPLEMENTED，详见重设计 Roadmap §18；R04b2 已接 renderer 换源/删除的私有 CEF v2，并非 MHV2/runtime 或默认三入口产品装配完成。
- 2026-09-04 用户明确要求入口代码先做：R08u1 共享多视频选择与 R08u2 原生三入口组件 VERIFIED；新旧原生 unit 已能执行，历史 `_dyld_start` 启动阻塞不再是本次结论。新两专项在 Debug/Release 各连续 3 次通过，两套完整回归各 95/96，剩余媒体导航播放资格失败；状态/Review/完整证据见重设计 Roadmap §17。默认产品 Views 宿主与 MHV2/runtime 接线仍未完成，不把组件通过当作产品上线。
- 用户新增要求由[投屏体验重设计 Roadmap](cast-experience-redesign-roadmap.md)统一承接：代理环境域名 Direct、网址框后常驻灰态入口、多视频/设备显式选择、播放器悬浮快捷入口。
- `PLT-CAST-R00/R01/R02a/R03a/R00b/R07a VERIFIED`（方案/契约、Mac CEF 独立宿主原语、内部预检原因保留、范围调整、投屏码解析不再自动播放）；R07a 的 Mac Debug/Release 各 92/92，查找设备后须明确开始。§18 新发现：固定 CEF 单窗口最多一个 Chrome BrowserView，R02b 多 view 设计回退 IMPLEMENTED/REQUEST_CHANGES，R02b2 BLOCKED 等待替代宿主范围决定；默认窗口未改变。R04a 已修复导航/Blob/MSE/重启 fixture 时序，R04b1 实例/source 身份已实现，完整双配置复验与后续 renderer/MHV2/runtime 接线分别记录。用户明确不处理代理专项，R05/R06 撤出队列，不等待接收端代检接口。
- 这些是既有 PLT 的内部切片，不增加顶层 297 项统计，不覆盖下列平台剩余门禁；原 LAN b4 公网阻塞仍保留，b3c 视觉/picker 核对由 R08M 承接。

### 第一期可直接领取

| 顺序 | 任务 | 状态 | 说明 |
|---:|---|---|---|
| 1 | `LOC-07W` | BLOCKED | 自动化已收口；等待 Windows 防火墙提示/可信物理点击及 en-US、zh-TW、不支持语言包切换与注销重启 |
| 2 | `PLT-W05c` | BLOCKED | 产品投屏码与播控 UI/接线、双配置自动化已验证；当前远程桌面点击带 `LLMHF_INJECTED`，须在可信物理输入控制台闭合 ADB 正式接收端 Direct 真机链路 |
| 3 | `PLT-M05b4` | BLOCKED | [受限 LAN 预检](lan-media-probe-roadmap.md) 与本地 Direct 首帧/播控/stop PASS；公开 VP9 浏览器播放 PASS、投屏被系统 DNS 的基准测试网段阻塞；自动发现补证 6 台，真实拒绝文案视觉核对归 b3c；strict 签名归 QAR-10 |
| 4 | `LOC-09M` | TODO | LOC-08M 已补双配置构建/91 项完整 CTest/三语言 bundle；下一步真实系统语言与 VoiceOver/IME/缩放，当前需用户解锁 Mac，系统设置变更另需确认 |

### 平台收口与待拆装配

| 任务 | 状态 | 说明 |
|---|---|---|
| `MDV-20` | DONE | Windows 回归由 2026-09-01 MDV-20W 完成记录闭合，保留 macOS 已有证据 |
| `MDV-24` | VERIFIED | 主矩阵已闭合；Narrator、中文 IME、原生 200% DPI、原生 macOS x64 仍待补 |
| `MRT-09` | DONE | 2026-09-01 Windows 首发 P0 Review 已闭合；macOS addendum 单独补证 |
| `PLT-W05a..f` | DONE/BLOCKED/TODO | W05a/b/c0 已装配；W05c 等可信物理输入闭合 ADB Direct，之后才能 Relay→拒绝/交接→100 次稳定性，严格串行 |
| `PLT-M05b4..b6/M05c` | BLOCKED/TODO | b4 导航修复、受限 LAN 原语/产品取消接线/重试状态恢复/拒绝 UI VERIFIED；真机仍待闭合，b5/b6/M05c 不越过依赖 |

### 第一期剩余门禁盘点（2026-09-03）

| 范围 | 尚未闭合的任务/证据 | Mac 执行顺序或边界 |
|---|---|---|
| 三语言 | `LOC-02` 独立语言审校、`LOC-07W BLOCKED`、`LOC-09M/10 TODO`；08M 本次达到 VERIFIED | 下一步 09M 真实系统语言、header/JS/html-lang、VoiceOver/IME/缩放/签名包；不擅改用户系统设置 |
| 投屏真机 | `PLT-M05b4..b6/M05c`；Windows `W05c BLOCKED`、`W05d..f TODO` | Mac 依次 Direct→MP4 Range/HLS Relay→拒绝/外部交接→100 次切换/睡眠唤醒/退出；使用 ADB 正式接收端，不能以 SDK standalone 或在线设备代替产品上屏 |
| Markdown 收口 | `CNT-21W` 等 `PRV-13AW`；Mac CNT/MRT addendum；`MDV-24` 剩余辅助/输入真机证据 | Mermaid/Highlight/KaTeX 与生产 fixture 清理已有完成证据，不重复算未开发；多语言变更后仍需 Mac 对称回归 |
| 平台与隐私总审 | `PLT-19W/19M`、`PRV-13AW` 与 Mac addendum | 等对应产品/真机证据；真实 SecureStore Keychain 依用户决策放最后，不作为构建/启动前置 |
| 发布质量 | `QAR-01W/02AW/03W/04W/05AW/06W/07W/08AW/09/11W/12W/14W/15W/16W`、Mac `QAR-10` 及对应 M 补证、`REL-03/04` | CI/E2E、性能、安全、30 分钟/8 小时长稳、SBOM、安装/升级/回滚与 Go/NoGo；Mac Developer ID/公证不是本地 ad-hoc 验签，凭证与上传另需授权 |

本次 LOC-08M 已达到 VERIFIED，不改变 Windows 首发策略；Mac 首期仅 Apple Silicon，原生 Intel/x64 长稳仍 `NOT_IN_RELEASE`。上述盘点按模块完成记录纠正索引旧状态，不把历史 `IN_PROGRESS` 段落当作当前任务。

### 第二期与依赖阻塞

- `AGT-13/14` 等 AGT-12 产品装配；`AGT-16` 再等 CLI/MCP 与 `AGT-15 VERIFIED`。
- `AGT-12C` 与 `AGT-13/14/16` 均为第二期；先拆 CEF accept loop、stop、session/grant/tool dispatch 原子切片，不能夹入一期 CNT/Cast 装配。
- `HUB-07` 等 `WFL-12`，`HUB-08` 等 `AGT-14`；Partner connector `HUB-09+` 仍按独立信任/OAuth/网络门禁推进。
- `CNT-11` 必须等 `CNT-21`、`AGT-16`、`PRV-13B` 与 provider ADR；第一期不得提前接真实模型。
- `SDK-15/16` 等 `HUB-16` 及外部 Cast-SDK/receiver 正式 API，不在浏览器内临时拼协议。

第一期任务完成前不直接领取 WFL/HUB/M2/MRT-10+；已具备依赖的第二期模型或状态机任务也保持排队，不与 CEF 产品装配争抢工作区和真机矩阵。

## 5. 当前代码事实

- 品牌资产 `BRD-01..04`、Foundation 20 个原子任务、`MED-01..19`、`CEF-01A..01D`、`CEF-02W`、`CEF-03`、`BUX-01..03`、`SDK-01..12` 与 `RNM-01..08` 已完成，共 72 项。
- `browser/engine-api` 已冻结为不含 CEF/ArkWeb/OS/Cast/Relay 类型的 C++17 契约，并通过 GCC/MSVC 双编译器、公开头独立编译、生命周期 contract 和 production boundary scan；它还不是可运行浏览器。
- Cast-SDK 固定源码 revision 为 `44c3a99871aa1e68cbda71eacefbb41d23a747a8`，由 `third_party/cast-sdk` gitlink 与 `config/cast-sdk-source.toml` 约束；后续以 `SDK-01` 最终 Review 记录为准。
- `CastPolicyDecision::Mirror` / `DeliveryPlan::Mirror` 已由 `MED-19` 迁移为 `ExternalClientHandoff`（纯建议 DTO + 稳定 reason + 用户确认要求）；旧 `mirror` wire 值仅作兼容读取，新代码不得再引用 Mirror 语义。
- `CastFacade` 的确定性 Fake 在 `test-support::cast_facade::FakeCastFacade`（dev/test target only）；SDK-05+ 的真实 service 与 SDK-12 编排测试都应以它为行为基准。
- Roadmap 表示目标和完成证据，不等于所有目标都已由代码实现；领取前必须读取真实代码、测试和 Git 状态。

## 6. 状态与完成规则

- 状态仅使用 `TODO`、`READY`、`IN_PROGRESS`、`BLOCKED`、`IMPLEMENTED`、`VERIFIED`、`DONE`。
- `IMPLEMENTED` 不等于完成；必须记录实际 Format、Lint、Unit、Integration、Build 与适用 Harness 结果。
- 平台/设备任务没有真实平台或指定 Harness 证据时不得标 `DONE`。
- 每个原子任务完成后按 `docs/current/code-review-standard.md` 独立 Review；P0/P1 未关闭不得合并。
- 外部发布、推送、Tag、部署、凭证使用和 Cast-SDK 外部仓库修改仍需明确授权。
