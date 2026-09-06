# 自定义桌面外壳与 Alloy 迁移 Roadmap

- 日期：2026-09-04；决策来源：用户明确选择“自定义外壳＋Alloy”，长期按此架构、一期开始迁移。
- 状态：共享 `00..02`、Windows `03W..22W` VERIFIED；Windows 默认宿主已切到 Alloy，正在完成产品 surface 汇合与总回归。
- 归属：`PLT-M05/PLT-W05` 的跨领域迁移切片，前缀 `PLT-SHELL-`。不另建一套 REL/BUX/Cast 业务计划，不增加 297 个顶层任务或 212 个唯一用例 ID。
- 一期总入口：[REL](release-v1-roadmap.md)；投屏协议与交互仍由 [PLT-CAST-R](cast-experience-redesign-roadmap.md) 拥有。
- 平台：按用户 2026-08-31 决策先完成 Windows 10/11 x64 三闭环与发布候选；macOS 共享实现与既有证据保留，签名、公证、Keychain、原生生命周期和打包等特有门禁后续验证，两个平台不能互相替代。

## 1. 冻结的目标与不做项

目标架构是自有标签栏、导航栏、常驻投屏入口、菜单/面板，加可替换的内容视图。第一期正式网页后端为固定版本 CEF Alloy（windowed Views），不是把 Chrome 原生窗口裁剪、覆盖或嵌到另一个窗口。初始控件实现使用现有 CEF Views 和平台窗口 adapter，共享模型不含 CEF/OS 类型；不是新增 React/Qt/Electron 依赖，也不使用 OSR 自己做页面合成。

保留 Chrome 熟悉的交互心智，不继续依赖它的原生标签栏、omnibox、`ExecuteChromeCommand(IDC_NEW_TAB)` 或 LOCATION 借用布局。现有 Chrome 默认入口仅作迁移期回归基线；新外壳达到对应平台切换门禁前不替换日用入口。旧分支不是长期支持的第二套产品，也不是新外壳功能失败后的静默回退。

- 一期仍完整交付浏览器基础、网页 Markdown、LAN Direct/Relay、本地 Markdown 编辑及三语言；不借宿主迁移删减 PRD §4.1 的桌面基线。
- 多标签由自有外壳编排；CEF 保留页面渲染、网络和进程隔离。标签、内容视图、渲染进程不是一对一概念。
- 预留其他 WebView/原生内容 adapter 的接入点；第一期只交付 CEF Alloy 后端及已有内置内容，不安装其他引擎或宣称已支持 WKWebView/WebView2/ArkWeb。
- 将来需要 Chrome-style 特定能力时，独立评估隔离容器/窗口及扩展兼容矩阵；不承诺任意混合嵌入，不按网页“复杂程度”自动切引擎，不迁移 Cookie/授权或降级安全策略。
- 分屏布局、任意扩展安装、跨引擎热切换、Google 服务、内核源码 fork、CEF/SDK 升级不是本次一期交付。
- Agent/CLI/MCP、Workflow/Hub、模型、HarmonyOS、代理专项/接收端代检仍不进入一期。投屏安全与 SDK 所有权不变。

## 2. 唯一 owner 与内容视图边界

| 层 | 唯一职责 | 不允许承担 |
|---|---|---|
| 共享 Shell/BUX 模型 | 命令、可见标签排序/激活、焦点、布局、脱敏呈现 | CEF 指针、平台句柄、播放授权、文件路径授权 |
| TabController/TabModel | 当前真实浏览器生命周期及 Browser ID→逻辑 Tab 映射；迁移后仍由它确认创建/关闭/导航事实 | Views 回调之外私建第二份可写标签身份库 |
| 内容视图 host/adapter | 将一个当前内容实例挂载到容器，布局、可见性、焦点与销毁回调 | 自行决定活动标签、业务路由、Profile 权限 |
| engine-api/CEF adapter | 复用既有导航/快照接口；CEF UI 线程对象只在 shell adapter | 把视图句柄/任意 JS/CDP 加到公共接口 |
| gateway/app-runtime | 验证来源与 generation，取消旧任务；投屏候选/草稿/授权唯一 owner | 因换了宿主就绕过播放证明或换协议 |
| 平台 adapter | AppKit/HWND、菜单/IME/辅助功能、Profile 存储和原生生命周期 | 用标题/窗口坐标推断安全上下文 |

“内容用途”与“后端类型”分开：普通网页、受控内置页、本地文档是用途，不是三种引擎。第一期均可由 Alloy 承载；`crayon://mdv` 仍复用原资源与文件 owner。后端可嵌入性、导航、快照、可信输入和媒体观察必须逐能力声明；不支持时返回明确 unsupported，不能用引擎名称推断能力或以模型能力替代真实装配。

`02` 冻结最小视图协议时必须解决：

1. 复用已有 opaque Profile/Tab/Navigation ID，补充挂载 epoch；事件绑定实例和 epoch，关闭/重建/跨窗口迁移后旧回调不能污染新视图。
2. 创建/关闭请求与完成分离；创建失败、关闭期间的迟到创建、beforeunload 取消、重复销毁、崩溃和部分初始化失败都有唯一收敛路径。命令接受不等于页面已加载或窗口已关闭。
3. 共享 API 不暴露 `void*`/CEF/OS handle；真实 view 句柄保留在对应 shell adapter，使用窄接口连接布局层。渲染后端与 UI 工具包分别替换，不能因使用 CEF Views 让 Core 依赖 CEF。
4. 单 UI 线程操作、异步回调 fencing、owner 先撤销后释放；不得用同步 `Stop()` 等待必须在同一 UI 线程执行的 CEF 关闭回调。既有 engine-api Stop 契约若不能直接满足，先通过异步关闭编排排空，再完成 Stop，不能偷偷改变语义。
5. 容量复用既有标签预算；挂载/隐藏不卸载用户网页，不因切标签重新导航；后台页按既有播放/资源策略处理。禁止无限缓存已关闭的视图和墓碑。
6. 页面不能选择后端、注册 adapter、伪造内容用途或获得 native bridge。内置页能力绑定受控资源、Profile/tab/navigation、用户手势及用途，不因 URL 字符串或“是我们的页面”自动授予权限。
7. 不扩张现有 `BrowserUrl` 的 HTTP(S) parser；内置 scheme/本地文档走既有受控入口，新增统一表达必须另有兼容向量。未来 WebView 默认不继承 CEF Cookie、文件引用、投屏证明或快照资格。

## 3. 代码现状与复用清单

| 已核对对象 | 当前事实 | 迁移处置 |
|---|---|---|
| `browser/cef-shell/src/browser/window/tab_controller.cc` | `CreateBrowserWindow` 使用 Chrome style，新标签/popup 调 Chrome command | `04/08` 替换宿主路径；保留观察、下载、权限、文件入口及身份 fencing |
| `browser/shared-ui/{shell,tabs,omnibox,navigation,windows}` | 已有状态机和测试；不是完整 Alloy 产品 UI | 复用并逐项接线；不把原 BUX DONE 当作新外壳通过 |
| `browser/engine-api` | 已有纯 C++17 导航/Profile/快照接口，没有通用视图挂载接口 | `02` 先审查最小增量；不创建另一套浏览器业务 API |
| `chrome_location_bar`、`CastEntrySurface` | LOCATION 借用和三入口独立组件有证据；默认产品未接入 | LOCATION 路线停止扩张；`20` 拆除投屏 surface 对 LOCATION 的依赖，保留共享选择模型与意图 |
| Cast gateway/runtime/SDK/MHV2 | 逐播放器证明、部分身份、握手 codec 已有增量；完整实例列表/草稿协议尚未接通 | R04/R07 继续拥有实现；外壳完成不能宣称多视频投屏完成 |
| CNT/MDV/MRT | 快照/确定性输出、受控文件保存、离线扩展已有业务与旧宿主证据 | `18/19` 重新接线并验证，算法与文件安全 owner 不重写 |
| Profile/隐私/本地化/打包 | 有各自平台证据与未闭合项 | 新宿主回归由 `10/14/23/26/27` 映射原门禁，不抹掉历史失败或平台差异 |

## 4. 一期原子队列

`P` 必须在领取时实例化为 `M` 或 `W`，是两个独立任务，不是 Mac 通过自动关闭 Windows。所有后续 TODO 在领取前需补具体文件、命名预算、实际 target/命令与输入证据；单项若超过两天、约十个生产文件或千行净新增，继续拆子项，不从目录通配直接大改。每位执行者一次仅一个原子任务 IN_PROGRESS；PLT 聚合状态不是另一个本人领取项。

验收缩写：D=文档/事实/链接/guard；U=无 CEF 的 Format/Lint/Unit/Contract；H=对应平台 Debug/Release build、真实 CEF 本地 Harness；P=完整适用 CTest＋真实产品 UI；R=对应平台原有设备/隐私/质量/发布门禁。具体入口在 §6，缩写不是通过证据。

| ID 后缀（均为 PLT-SHELL-） | 状态 | 依赖 | 单一目标 / 允许领域 | 验收 |
|---|---|---|---|---|
| 00 | VERIFIED | 用户决策、current 契约 | 本计划与一期依赖、架构决策同步；仅文档 | D、方案 Review |
| 01 | VERIFIED | 00 VERIFIED | `shared-ui/shell` 命令 owner 显式选择；自有快捷键无 Chrome passthrough | U；旧调用兼容、重复/非法/关闭/重入拒绝 |
| 02 | VERIFIED | 00 VERIFIED | engine-api 与 shell 内容视图挂载/能力契约，按 §2 最小增量 | U；公开头独立编译、旧接口兼容、生命周期/未知能力拒绝；无运行后端宣告 |
| 03M | TODO | 02 VERIFIED | macOS windowed Alloy 单窗口＋两个内容视图的生产宿主原语与独立 Harness | H；同窗口切换不重建网页、缩放/隐藏/关闭/资源回落；默认入口不变 |
| 03W | VERIFIED | 02 VERIFIED | Windows windowed Alloy 单窗口＋两个内容视图的生产宿主原语与独立 Harness | H；同窗口切换不重建网页、缩放/隐藏/关闭/资源回落；默认入口不变 |
| 04W | VERIFIED | 01、03W VERIFIED | Windows TabController/TabModel 的异步创建/关闭与视图映射 | H；beforeunload 取消、迟到创建、崩溃/退出、无双 owner |
| 04M | TODO | 01、03M VERIFIED | macOS TabController/TabModel 的异步创建/关闭与视图映射 | H；beforeunload 取消、迟到创建、崩溃/退出、无双 owner |
| 05W | VERIFIED | 04W VERIFIED | Windows 自绘基础标签栏和新标签入口，消费既有 tab 模型 | H；新建/激活/关闭/排序、焦点/容量，不调用 Chrome 新标签命令 |
| 05M | TODO | 04M VERIFIED | macOS 自绘基础标签栏和新标签入口，消费既有 tab 模型 | H；新建/激活/关闭/排序、焦点/容量，不调用 Chrome 新标签命令 |
| 06W | VERIFIED | 05W VERIFIED | Windows 自绘 omnibox 编辑、搜索/URL 判定和建议接线 | H；输入/取消/旧建议、IDN/URL 显示安全边界、无默认联网建议 |
| 06M | TODO | 05M VERIFIED | macOS 自绘 omnibox 编辑、搜索/URL 判定和建议接线 | H；输入/取消/旧建议、IDN/URL 显示安全边界、无默认联网建议 |
| 07W | VERIFIED | 06W VERIFIED | Windows 导航控制/加载状态/站点身份可见反馈 | H；前后退/刷新/停止、重定向、证书错误、页面不能伪造安全标识 |
| 07M | TODO | 06M VERIFIED | macOS 导航控制/加载状态/站点身份可见反馈 | H；前后退/刷新/停止、重定向、证书错误、页面不能伪造安全标识 |
| 08W | VERIFIED | 05W、07W VERIFIED | Windows popup 与多窗口生命周期迁移 | H；来源与用户手势、容量、关闭/恢复、窗口间隔离 |
| 08M | TODO | 05M、07M VERIFIED | macOS popup 与多窗口生命周期迁移 | H；来源与用户手势、容量、关闭/恢复、窗口间隔离 |
| 09W | VERIFIED | 08W VERIFIED | Windows 高级标签功能接线：固定/复制/静音/搜索/分组/跨窗口移动 | H；复用 advanced 模型；移动不得隐式重载表单或丢状态 |
| 10W | VERIFIED | 08W VERIFIED | Windows 会话恢复与崩溃恢复绑定新宿主 | H；无痕不持久、损坏数据、重复恢复、schema 前后兼容、不可静默丢用户标签 |
| 11W | VERIFIED | 07W VERIFIED | Windows 书签栏/管理入口接入已有 store/view | H；编辑/搜索/导入导出、跨 Profile、失败反馈 |
| 12W | VERIFIED | 10W VERIFIED | Windows 历史/最近关闭入口接入已有 owner | H；删除边界、无痕隔离、恢复正确目标 |
| 13W | VERIFIED | 07W VERIFIED | Windows 下载 UI 与原 CefDownloadHandler 接线 | H；取消/续传、危险状态、受控保存和打开位置 |
| 14W | VERIFIED | 08W、10W VERIFIED | Windows 设置/Profile/无痕选择 UI 接线 | H；独立 request context、设置 readback、清理失败显式反馈 |
| 15W | VERIFIED | 07W、14W VERIFIED | Windows 权限/证书/popup/外部协议可信确认面板 | H；origin/TTL/导航失效、默认拒绝、不调用网页伪造面板 |
| 16W | VERIFIED | 07W VERIFIED | Windows 查找/缩放/全屏/打印/PDF 页面工具接线 | H；逐能力验证，不能假定 Alloy 提供 Chrome UI；PiP 不支持时明确矩阵 |
| 17W | VERIFIED | 15W VERIFIED | Windows 主菜单/上下文菜单/拖放/剪贴板/本地文件入口迁移 | H；平台快捷键、About/许可、安全文件选择、取消与来源约束 |
| 18W | VERIFIED | 17W VERIFIED | Windows 内置新标签/MDV 内容接入新 host | P；源码/预览/编辑/原子保存/冲突，Mermaid/Highlight/KaTeX 离线；复用 MDV/MRT 门禁 |
| 19W | VERIFIED | 07W、17W VERIFIED | Windows 网页 Markdown 从新入口到原快照/导出链 | P；当前标签、导航取消、跨源/隐藏内容拒绝、复制/保存；CNT addendum |
| 20 | VERIFIED | 02、PLT-CAST-R08u1 VERIFIED | CastEntrySurface 去 LOCATION 耦合，按钮/面板挂到自绘栏 | U＋H；布局/灰态/事件/释放；无真实后端时仍不允许开始 |
| 21W | VERIFIED | 07W、15W、20、R03b/R04/R07 VERIFIED | 对应 PLT-CAST-R08W 的 Alloy 产品接线，不另建投屏 owner | P；多视频/设备明确选择、连接不播放、错误/播控、MHV2 兼容拒绝 |
| 21M | TODO | 07M、15M、20、R03b/R04/R07 VERIFIED | 对应 PLT-CAST-R08M 的 Alloy 产品接线，不另建投屏 owner | P；多视频/设备明确选择、连接不播放、错误/播控、MHV2 兼容拒绝 |
| 22W | VERIFIED | 21W、PLT-CAST-R09 VERIFIED | 对应 PLT-CAST-R10W 的 Browser-owned 覆盖层接线 | P；普通主 frame、裁剪/旧几何/焦点/伪造拒绝；不可靠 iframe/fullscreen/PiP 不绘制 |
| 22M | TODO | 21M、PLT-CAST-R09 VERIFIED | 对应 PLT-CAST-R10M 的 Browser-owned 覆盖层接线 | P；普通主 frame、裁剪/旧几何/焦点/伪造拒绝；不可靠 iframe/fullscreen/PiP 不绘制 |
| 23W | BLOCKED | 09W、11W..19W、21W、22W VERIFIED | Windows 全外壳本地化/IME/键盘/读屏/缩放/主题回归 | P；LOC Windows 矩阵、UX-001..018；不擅改系统设置 |
| 23M | TODO | 09M、11M..19M、21M、22M VERIFIED | macOS 全外壳本地化/IME/键盘/读屏/缩放/主题回归 | P；LOC macOS 矩阵、UX-001..018；不擅改系统设置 |
| 24W | IN_PROGRESS | Windows 01..22W VERIFIED；23W 系统语言/IME/Narrator/原生 DPI 矩阵经用户 2026-09-05 明确后置 | Windows 产品默认入口切至自定义 Shell＋Alloy | P；分 24W1..W3；三闭环和日用功能无回退、入口与 capability 真实性 Review |
| 24M | TODO | macOS 01..23 对应项 VERIFIED | macOS 产品默认入口切至自定义 Shell＋Alloy | P；后续平台验证，不被 Windows 证据替代 |
| 25P | TODO | 24P VERIFIED | 移除该平台旧 Chrome 宿主/LOCATION 生产接线及临时迁移开关 | P＋artifact scan；另一平台仍需要的共享代码保留隔离，不删除他人改动 |
| 26P | TODO | 25P VERIFIED | 在新默认宿主复验 Direct→Relay→拒绝/交接→稳定性 | R；映射 PLT-W05c..f / M05b4..b6/M05c 与 R11P；真实接收端、100 次/睡眠/退出 |
| 27P | TODO | 23P、25P、26P VERIFIED | 新宿主三闭环/隐私/性能/发布证据汇总 | R；PRV/CNT/MRT/PLT/LOC/QAR/REL 对应平台完整门禁；无签名/真机不标 DONE |

`21P/22P/26P/27P` 是原业务/发布任务的迁移检查点，不重复实现、重复领取或双计完成。表内已按 W/M 拆分的行以对应平台状态为准；`03W` 不依赖 `03M` 的真机结果，跨平台共享代码在提交前保证结构可编译，实机分别补证。`24W` 不依赖 `23M/24M`。

## 5. 原宿主方案与一期剩余工作的接续

- `PLT-CAST-R02b/b2` 原 LOCATION 多 Chrome view 路线由本计划取代，不再等待宿主方向批准，也不能标成已实现成功。原状态/失败证据保留为历史；后续仅领取本计划 `02..24P`。
- `R02b3M/W、R02cM/W` 的平台宿主义务由 `24P` 验收；R08 的入口接线可在 `07P/15P/20` 后进入候选 host 验证，不再循环依赖“先最终默认切换，再接投屏”。
- `R08u1/u2` 的共享模型/组件证据保留，LOCATION-specific 部分不算 Alloy 证据；`R04c1` 握手 codec 也不等于完成完整 MHV2。
- Cast 独立关键链继续 `R04a/b2 完整复验 → R04c 后续消息/兼容 → R04d 实例集合 → R07b 草稿/连接/prepare/commit → R03b 原因投影 → R08P → R09/R10P → R11P`；每个协议切片先补冻结字节和 reject vectors。R09 几何可按原独立依赖提前实现，不扩大可信输入。
- 特殊代理网络、外部 SDK 新能力不恢复为前置；普通 Direct/Relay 的真实设备门禁仍必须通过。
- 第一期未闭合的 LOC 审校/真实语言、CNT/MRT Mac addendum、MDV 辅助功能、PRV/PLT 总审、QAR CI/E2E/性能/30 分钟与 8 小时长稳/SBOM/安装升级回滚/GoNoGo，全部保留，由 REL §5 新矩阵统一聚合。
- 历史证据按宿主标记为 Chrome baseline；相同算法/协议 unit 可复用，但宿主相关 UI、输入证明、Profile/生命周期、性能和 artifact 必须补 Alloy 证据。不得把旧全绿搬到新默认包。

## 6. 验证入口与不打扰用户的测试规则

当前真实入口（下列未执行项不是证据；不得用不存在的脚本/target 填通过）：

```sh
git diff --check
cargo run --quiet -p repo-guard -- scan --root .
cmake --preset engine-api
cmake --build --preset engine-api --target crayon_browser_shared_shell_test
ctest --preset engine-api -R '^browser_shared_shell_contract$'
cmake --build .cache/build/macos-arm64-cef-debug --parallel 4
cmake --build .cache/build/macos-arm64-cef-release --parallel 4
ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure
ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure
cmake --build --preset windows-cef-debug --config Debug --parallel 4
cmake --build --preset windows-cef-debug --config Release --parallel 4
ctest --test-dir .cache/build/windows-cef-debug -C Debug --output-on-failure
ctest --test-dir .cache/build/windows-cef-debug -C Release --output-on-failure
```

- 文档链接另用本地文件存在性检查，记录实际命令。`01` 仅需共享 target 双配置和定向契约，不启动 CEF。CEF/平台实现任务必须执行完整适用回归，排除/超时/中断逐项报告，不能以定向结果冒充全绿。
- `03P` 在已有 `browser/cef-shell/tests` 与 CMake 内建立独立 Alloy Harness；在同一进程/同一专用窗口中串行导航、切换和关闭测试标签，默认后台运行，不反复创建/激活顶层窗口。测试 case 接口只在独立测试 target，Release 产品无调试/远控入口。
- 只操作本次测试拥有的窗口/标签，不接管用户日用窗口。普通网页/布局用例优先复用测试 tab；需要加载新原生二进制时才重启测试进程。冷启动/崩溃/多窗口/前台输入/IME/读屏是例外，在运行前说明必要性与影响。
- 真实播放证明依赖物理/可信输入的门禁不伪造；后台 Harness 只能验证布局/生命周期，不能冒充前台媒体授权。需要用户物理输入时明确暂停该门禁，继续其他独立任务。
- 使用本地 fixture、事件就绪条件、deadline 和资源回落检查，不固定长 sleep、不访问第三方影视站。清理仅关闭本次创建的对象；不改系统语言、代理/DNS、Keychain、安全设置或用户 Profile。
- 记录 commit/range、脏工作区范围、OS/架构/CEF/配置、命令、exit、数量/耗时及 PASS/FAIL/TIMEOUT/NOT_RUN。最终安装包分别做 Release surface、签名/公证及更新回滚；凭证、上传和发布另需授权。

## 7. PLT-SHELL-00 方案完成

- 状态：VERIFIED；单一目标：将批准的自定义 Shell＋Alloy 决策变为一期完整依赖与迁移验收计划。
- 输入：当前 PRD/架构/测试/Review、REL/CEF/BUX/PLT/Cast、固定 CEF 150 头文件、TabController/TabModel、engine-api 与 shared-ui/shell 的真实调用方。
- 允许：本计划、REL/总/计划索引、CEF/BUX/PLT/Cast 计划入口、current README/architecture/browser-ux/cast-interaction、PRD；禁止生产/测试/依赖/系统设置、历史完成证据重写。
- 验收：§6 的 diff/guard、本地链接及任务依赖/计数检查、独立方案自审 P0/P1=0。新宿主真机和产品切换 NOT_RUN。
- 方案评审必须逐项确认：同一状态无双 owner、无循环依赖、无省略旧功能、未来 WebView 不扩权、两平台证据独立、旧宿主有明确退出节点、发布不能绕过新宿主门禁。

完成记录：main/cfaab39 的本次文档增量（工作区有其他在途改动，全部保留），macOS arm64。`git diff --check` PASS/0；`cargo run --quiet -p repo-guard -- scan --root .` PASS/0，9 passed、2 既有 warning、artifact N/A；本地 Node 文件链接检查 13 文件/92 链接/0 缺失，顶层表求和 20 模块/297 项、SHELL 00..27 共 28 组连续且不重复，均 PASS/0（首轮耗时未保留，最终复验见 §9）。独立方案自审按 current Review 顺序检查：关闭 R08→默认宿主切换的循环依赖、REL→27→REL 聚合环路，以及未来 WebView 自动继承权限的歧义；最终 P0/P1/P2/P3=0/0/0/0，APPROVE，仅限文档目标最高 VERIFIED。无新生产能力、GUI/设备/发布证据，未运行项 NOT_RUN。

## 8. PLT-SHELL-01 原子范围

- 状态：VERIFIED；依赖 00 VERIFIED。单一目标：让共享 CommandRegistry 显式区分旧原生 Chrome owner 与自定义 Shell owner，为 Alloy 快捷键/按钮统一执行提供入口；不切产品默认宿主。
- 允许修改：`browser/shared-ui/shell/include/crayon/browser_shell/command_registry.h`、`src/command_registry.cc`、`tests/shell_contract_test.cc`；本计划及必要索引。禁止 CEF/平台 app、TabModel/engine-api、投屏/协议、locales、依赖。
- 输入/调用方：Windows `shell_command_adapter` 当前两参构造与 NativeChrome pass-through；ShellState focus observer；既有纯 C++ shell contract。
- 兼容：保留两参构造的旧行为；新显式 custom 模式禁止 NativeChrome pass-through，产品按钮/宿主 accelerator 经同一 CanExecute/Execute。未知 mode/origin/command 拒绝，不发 observer 成功；序列有界单调，Shutdown 与回调重入后不能继续访问已撤销指针。原枚举数值不改，新值只追加；无 IPC/persistence 变化。
- 验收：旧构造兼容、14 类命令自定义分发、非法值、旧序列、禁用/执行失败、Shutdown/重入；Debug/Release 共享 target build＋`browser_shared_shell_contract`，clang-format 改动行、warnings-as-errors、guard、diff check。对两种模式使用固定预期向量；Windows adapter 未改，真实 Windows/Alloy GUI NOT_RUN。
- 明确不做：不把 command accepted 宣称为业务完成；不增加页面可调用入口，不访问原生 UI、不创建浏览器、不改旧产品行为。

## 9. 00/01 完成证据与 Review（2026-09-04）

被审对象：main/cfaab39＋本轮 13 个计划/契约文件与 `shared-ui/shell` 三文件增量；保留其他所有未提交改动，未提交/推送。平台 macOS 26.6.2 arm64、AppleClang/clang-format 21；无 CEF 共享 target，Debug/Release，固定 CEF/SDK/lockfile 未变。

实现：`CommandRouting` 由宿主装配根显式指定。原两参构造保留 Chrome pass-through；custom 模式拒绝 NativeChrome 来源，产品按钮/宿主 accelerator 都经目标能力检查和执行。旧枚举数值保留，新值追加；无 wire/schema 变化。分发中再次 Dispatch 返回 Reentrant 且不消费序列；CanExecute/Execute 内 Shutdown 后不再访问撤销的指针。Execute 已接受后关闭不伪造失败重试，且不再通知已撤销 UI。registry 必须活到当前调用栈退出，不支持同步销毁自身。

| 实际命令 | 结果 |
|---|---|
| `cmake --build --preset engine-api --target crayon_browser_shared_shell_test` | 原基线 PASS/0；新测试先因缺 CommandRouting/HostAccelerator 编译 FAIL/1；补接口后 PASS/0；最终修复后 PASS/0、0.25s |
| `ctest --preset engine-api -R '^browser_shared_shell_contract$'` | 修改前基线 PASS/0、1/1、13.36s |
| `ctest --preset engine-api -R '^browser_shared_shell_contract$' --timeout 30` | 接口具备后、生命周期修复前稳定 SegFault，FAIL/8、5.87s；修复后首次沙箱运行 TIMEOUT/8、30.02s，沙箱外复验 TIMEOUT/8、30.04s；不将超时记为通过 |
| `cmake -S . -B .cache/build/shell-alloy-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` | PASS/0；CMake configure/generate 报告约 3.2s，无浏览器窗口 |
| `cmake --build .cache/build/shell-alloy-release --target crayon_browser_shared_shell_test` | PASS/0、0.43s；Debug/Release 都启用 warnings-as-errors |
| `ctest --test-dir .cache/build/shell-alloy-release --output-on-failure -R '^browser_shared_shell_contract$' --timeout 30` | 初次 TIMEOUT/8、30.02s，出现受限 `/bin/ps` 提示 |
| `ctest --preset engine-api -R '^browser_shared_shell_contract$' --timeout 60` | 沙箱外原样复验 PASS/0、1/1、14.96s |
| `ctest --test-dir .cache/build/shell-alloy-release --output-on-failure -R '^browser_shared_shell_contract$' --timeout 60` | 沙箱外原样复验 PASS/0、1/1、32.71s |
| `ctest --preset engine-api -R '^browser_shared_shell_contract$' --timeout 60 --repeat until-fail:3` | 同一最终代码连续 3 次 PASS/0，总 0.01s；不是三项不同 CTest |
| `xcrun clang-format --dry-run --Werror --style=Google browser/shared-ui/shell/include/crayon/browser_shell/command_registry.h browser/shared-ui/shell/src/command_registry.cc browser/shared-ui/shell/tests/shell_contract_test.cc` | 首轮新测试排版 FAIL/1；仅格式化新增区间后 PASS/0、<1s，无无关格式化 |
| `xcrun clang++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -Ibrowser/shared-ui/shell/include -Ibrowser/engine-api/include browser/shared-ui/shell/src/command_registry.cc browser/shared-ui/shell/src/shell_state.cc browser/engine-api/src/types.cc browser/shared-ui/shell/tests/shell_contract_test.cc -o .cache/build/engine-api/shell-command-sanitized` | PASS/0、0.86s；仅独立测试产物 |
| `.cache/build/engine-api/shell-command-sanitized` | 最终 PASS/0、无 ASan/UBSan 发现；首次加载等待耗时未保留 |
| `/usr/bin/time -p .cache/build/engine-api/shell-command-sanitized` | 再验 PASS/0、real 0.05s、无 sanitizer 输出 |
| `cargo run --quiet -p repo-guard -- scan --root .` | 方案阶段与代码阶段最终均 PASS/0，9 passed、RG-003/004 两类既有 warning、artifact N/A；代码阶段曾长时间停在 loader，耗时未作为性能指标 |
| `node -e 'const r=require("child_process").spawnSync("cargo",["run","--quiet","-p","repo-guard","--","scan","--root","."],{encoding:"utf8",timeout:60000,maxBuffer:4194304});if(r.stdout)process.stdout.write(r.stdout);if(r.stderr)process.stderr.write(r.stderr);if(r.error)console.error(r.error.message);process.exitCode=r.status===null?1:r.status;'` | 已有代码阶段 PASS 后的额外有时限复验 TIMEOUT/1，`spawnSync cargo ETIMEDOUT`，60s；不能把这一轮写成 PASS，也不覆盖此前同一生产代码的通过证据 |
| `git diff --check` | PASS/0、<1s |

行为覆盖：原 5 组契约＋4 组新增契约；14 类命令的旧 NativeChrome 向量和 28 次自有按钮/accelerator 分发、未知值、禁用/执行失败、序列零值/重复/最大值、两种模式在能力检查/执行期间关闭、三处回调重入与 observer 关闭。不是完整产品 CTest，也不是实际 Alloy 窗口能力。

环境诊断：`sample 13166 1 1`（本次 sanitizer）和 `sample 13216 1 1`（本次 repo-guard）均 PASS/0，采样全在 `_dyld_start`，约 96K footprint、无应用栈。这只证明被采样时未进入应用代码，根因未确认，不能归因为命令死锁或宣称系统问题已解决。两次尝试 `kill -TERM` 指定测试 PID 时都返回 no such process，随后读取原会话证实进程已自行 PASS/0 退出；没有终止其他程序。`pgrep -fl 'shell-command-sanitized|crayon_browser_shared_shell_test'` 最终 exit 1、零匹配。未创建/激活/重启任何浏览器窗口，未改代理、Keychain、签名或系统安全设置。

最终文档检查命令如下，PASS/0，13 文件/92 个本地文件链接/0 缺失、20 模块合计 297、SHELL 28 组连续；不校验网页或 Markdown 内部锚点。任务依赖另外人工复核，不能用编号检查代替 DAG Review。

```sh
node <<'NODE'
const fs = require('fs');
const path = require('path');
const files = [
  'docs/plans/desktop-shell-roadmap.md', 'docs/plans/release-v1-roadmap.md',
  'docs/crayon-private-cast-browser-roadmap.md', 'docs/plans/README.md',
  'docs/current/README.md', 'docs/current/architecture.md',
  'docs/current/browser-ux.md', 'docs/current/cast-interaction.md',
  'docs/crayon-private-cast-browser-prd.md',
  'docs/plans/cast-experience-redesign-roadmap.md',
  'docs/plans/desktop-cef-browser-roadmap.md',
  'docs/plans/browser-product-experience-roadmap.md',
  'docs/plans/desktop-platform-adapters-roadmap.md'
];
let links = 0;
const missing = [];
for (const file of files) {
  for (const m of fs.readFileSync(file, 'utf8').matchAll(/\]\(([^)]+)\)/g)) {
    const target = m[1].split('#')[0];
    if (!target || /^[a-z]+:/i.test(target)) continue;
    links++;
    if (!fs.existsSync(path.resolve(path.dirname(file), target))) {
      missing.push([file, target]);
    }
  }
}
const total = [...fs.readFileSync(files[2], 'utf8')
  .matchAll(/^\| ([A-Z]+) \| (\d+) \|/gm)]
  .reduce((sum, m) => sum + Number(m[2]), 0);
const groups = [...fs.readFileSync(files[0], 'utf8')
  .matchAll(/^\| (\d{2}(?:M \/ \d{2}W|P)?) \| (?:TODO|READY|IN_PROGRESS|IMPLEMENTED|VERIFIED|DONE) \|/gm)]
  .map(m => m[1].slice(0, 2));
const expected = Array.from({length: 28}, (_, i) => String(i).padStart(2, '0'));
const sequential = JSON.stringify(groups) === JSON.stringify(expected);
console.log(JSON.stringify({files: files.length, links, missing, total, groups: groups.length, sequential}));
process.exitCode = missing.length || total !== 297 || !sequential ? 1 : 0;
NODE
```

Code Review：按需求/边界→正确性→架构/API→并发/生命周期→安全/隐私→性能→测试→维护自审。发现并修复关闭回调空指针（有修复前崩溃）和嵌套命令交错；无锁/IO/额外队列、无 CEF/OS 类型或权限扩张，生产文件 171/69 行、测试 401 行，test double 只在独立测试文件。范围内 P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED；不等于平台 DONE。frontend-design 仅约束规划沿用已有双行布局、token、字体、焦点与三语言，没有新增视觉框架或依赖。

未覆盖：Windows 构建/真实 UI、Alloy 窗口与多视图、完整适用产品 CTest、设备、性能长稳、安装包/签名/公证均 NOT_RUN。旧工作区测试中的超时/失败仍保留；本次不宣称全仓全绿。`02` 的后续完成证据见 §10；平台宿主按 Windows-first 先领取 `03W`，Mac `03M` 独立保留。

## 10. PLT-SHELL-02 原子范围

- 状态：VERIFIED；依赖 00 VERIFIED。单一目标：冻结纯 C++17 内容视图挂载、能力和异步生命周期契约，并由共享 Shell 以有界状态机收敛创建、关闭、取消、崩溃和迟到回调；不创建真实窗口或切换默认产品入口。
- 允许修改：`browser/engine-api/{include/crayon/browser_engine/content_view.h,src/content_view.cc,tests/**,CMakeLists.txt,README.md}`、`browser/shared-ui/shell/{include/crayon/browser_shell/content_view_registry.h,src/content_view_registry.cc,tests/shell_contract_test.cc,CMakeLists.txt}` 与本计划。禁止 CEF/平台 adapter、TabController/TabModel、Cast/MDV/CNT、依赖和本地化资源。
- 类型与预算：复用 `ProfileId/TabId/NavigationId`；新增非零 `MountEpoch`、闭合 `ContentPurpose`、最多 8 位的 `ContentCapabilitySet`、挂载请求/异步结果与 `ContentViewRegistry`。每标签至多一个 live mount、registry 容量由构造参数设定且 `1..256`；关闭后不保留无界 tombstone，epoch 由调用方单调分配并绑定所有完成事件。
- 输入/调用方：current 架构 §3.1、本计划 §2、既有 `BrowserEngineAdapter`/`ShellState` 和下一项 `03M/03W` 的平台 host。共享层只接收值类型，不公布后端名称、可用后端列表、native handle、任意脚本/文件/网络能力。
- 验收命令：`cmake --preset engine-api`；`cmake --build --preset engine-api --target browser-engine-contract browser-engine-headers crayon_browser_shared_shell_test`；`ctest --preset engine-api -R '^(browser_engine_contract|browser_engine_headers_compile|browser_engine_forbidden_api_scan|browser_shared_shell_contract)$' --output-on-failure`；另跑 clang-format、`git diff --check` 与 repo-guard。覆盖旧接口/头文件独立编译、未知 purpose/result/capability bit、零 epoch、重复 mount/close、beforeunload 取消、创建失败、关闭期迟到创建、崩溃、重复销毁、容量和 shutdown fencing。
- 明确不做：不宣告 Alloy 或其他 runtime backend 可用；不定义布局/native view 操作，不导航或重开 URL，不替代 TabModel owner，不同步等待关闭回调，不提供页面注册入口，不运行 CEF/GUI/真机/打包门禁。

完成记录（2026-09-04，基线 `main/0fc8eed`，Windows 11 10.0.26200 x64）：新增 `content_view.h/.cc` 的非零 mount epoch、闭合用途/六项能力 bit set、挂载请求与异步结果校验；新增 Shell `ContentViewRegistry`，以最多 256 条按 tab 有界记录收敛 creating/mounted/closing/detached，关闭期间迟到 created 只登记事实且继续 closing，close-cancel 后恢复完整能力，旧 epoch/身份不一致/未知枚举和 bit fail closed。原 `BrowserEngineAdapter` vtable、既有 DTO/枚举数值和调用路径未修改；没有 backend 名称、可用后端清单、CEF/OS handle、页面注册入口或同步等待。

验证：旧 `engine-api` preset 因目录从 `D:/get-video` 迁移遗留 CMakeCache 拒绝复用，未删除该缓存，改用 `.cache/build/engine-api-alloy-msvc`。`cmake -S . -B .cache/build/engine-api-alloy-msvc -G 'Visual Studio 17 2022' -A x64 -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` PASS/0；MSVC 19.44、Windows SDK 10.0.26100，Debug/Release 分别构建 `browser-engine-contract browser-engine-content-view browser-engine-headers crayon_browser_shared_shell_test crayon_browser_content_view_registry_test` PASS/0，`/W4 /WX /permissive-`。两配置分别定向 CTest 6/6 PASS（Debug 0.90 s、Release 0.79 s）：旧 engine、content-view、独立公开头、forbidden API scan、旧 shell、registry lifecycle。MinGW Debug 同六项 6/6 PASS/0（0.30 s）。Visual Studio 附带 clang-format 对 7 个新增 C++ 文件 PASS/0；`git diff --check` PASS/0；`cargo run --quiet -p repo-guard -- scan --root .` PASS/0，RG-001..009 通过，RG-003/004 仅既有全仓 warning，artifact N/A。

未覆盖与原始影响：Debug 完整无 CEF CTest 在外层 184.1 s 超时且未返回可审计单项输出，记 `TIMEOUT`；Release 完整无 CEF CTest、CEF/Alloy GUI、平台宿主、真机、性能、长稳和 artifact 均 `NOT_RUN`，不宣称全仓全绿或 Alloy 已可用。这些不属于本 U 契约的最高状态门禁，03W 起必须补 H/P/R 证据。

Code Review：按 v0.9 的需求/边界→正确性→架构/API→并发/生命周期→安全/隐私→性能→测试/证据→维护/供应链独立检查。修复两项审查中发现的问题：关闭期迟到 created 的能力丢失/取消后错误恢复，以及只定义 `operator==` 导致 MSVC 严格 C++17 编译失败；均有契约测试与双编译器证据。无锁、线程、IO、外部回调、依赖或 runtime backend 广告；生产文件 119/59/69/206 行，测试 93/179 行。范围内 P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED；下一原子任务 `03W READY`（领取时实例化并补具体范围）。

## 11. PLT-SHELL-03W 原子范围

- 状态：VERIFIED；依赖 02 VERIFIED。单一目标：提供 Windows 可运行的 CEF Alloy windowed 内容容器原语，并在独立 integration 模式内用同一隐藏测试窗口承载两个真实 BrowserView；不切换产品默认 Chrome-style 入口。
- 允许修改：`browser/cef-shell/src/browser/window/alloy_content_view_host.{h,cc}`、`tests/alloy_content_view_host_probe.{h,cc}`、`tests/page_snapshot_cef_integration_win.cc`、`browser/cef-shell/CMakeLists.txt` 与本计划。禁止产品 `BrowserApp`/`TabController` 装配、Mac、Cast/MDV/CNT、依赖、资源和本地化。
- 命名与预算：复用 02 的 request/epoch/capability/registry；host 至多 256 条记录，CEF `CefPanel/CefBrowserView` 只存在 cef-shell adapter 内。一个 production host、一个 test-only probe，不新增 backend registry、feature flag、远控入口或第二套 tab owner；预计 2 个生产文件、3 个测试/装配文件，净新增生产代码低于 500 行。
- 输入/调用方：固定 CEF `150.0.10+g8042e43+chromium-150.0.7871.101` 的 `CefWindow/CefPanel/CefBrowserView` Views API；现有 Windows `crayon_page_snapshot_cef_integration_win` sandbox bootstrap 与专用 executable。Harness 只使用本地 `about:blank`/内存脚本，不启动 content/media helper、不访问公网、不产生可信播放证明。
- 验收：Windows x64 Debug/Release 构建 product host 与 integration target；运行 `alloy_content_view_host_windows`，证明 Browser/Window runtime style 均为 Alloy、同一窗口双 BrowserView、激活切换只改 visible/focus 且 browser id/页面状态不变、独立 zoom、宽窄布局、空 view/容量资源回落、旧 epoch 拒绝、关闭/重复关闭/销毁排空。另跑 02 契约、完整适用 CTest（超时逐项记录）、source/forbidden scan、clang-format、diff check、repo-guard。
- 明确不做：不显示/接管用户日用窗口，不模拟物理输入，不验证 beforeunload（归 04W）、popup/多窗口、导航 UI、内置页面、投屏、IME/读屏、性能长稳或打包；不把 Harness 通过写成产品已切 Alloy。

完成记录（2026-09-04，基线 `main/0fc8eed`，Windows 11 10.0.26200 x64）：新增 `AlloyContentViewHost`，把共享 `ContentViewRegistry` 的 mount epoch/capability/closing 契约映射到 CEF `CefPanel/CefBrowserView`；激活只切换可见性与焦点，缩放使用已校验 `ZoomFactor`，关闭由异步 browser/window 生命周期完成。新增 test-only Alloy probe，在同一从不显示的 `CefWindow` 中挂载两个真实 data URL BrowserView，并从 Window、BrowserView 和 BrowserHost 回读 `CEF_RUNTIME_STYLE_ALLOY`；验证 browser id、URL、JavaScript 页面状态在切换和宽窄布局后不变，两个视图缩放互不污染，空视图/容量/旧 epoch fail closed，重复关闭后 browser/window/registry 均排空。产品默认 `BrowserApp`、`TabController` 和 Chrome-style 入口未切换，测试脚本未进入 production target。

验证：使用固定 CEF `D:/crayon-browser/.cache/cef/windows64-root/cef_binary_150.0.10+g8042e43+chromium-150.0.7871.101_windows64`；`cmake --build --preset windows-cef-debug --target crayon-browser crayon_page_snapshot_cef_integration_win` 与对应 Release target 均 PASS/0。Debug/Release 分别运行 `browser_engine_contract|browser_engine_content_view_contract|browser_engine_headers_compile|browser_engine_forbidden_api_scan|browser_shared_shell_contract|browser_content_view_registry_contract|alloy_content_view_host_windows|windows_cef_shell_source_contract`，均 8/8 PASS/0（1.91 s/1.22 s），其中真实 Alloy probe 1.64 s/0.98 s。Debug 完整 CTest 注册 102 项：首轮 99 PASS、3 项因 executable 未构建 NOT_RUN；补建后 `cast_selection`、`player_input_proof` PASS，累计 101/102 PASS。余下 `media_host_v2_codec` 因未改动的 `media_host_v2_codec_test.cc:105` 在 MSVC `/WX` 下 C4244（`int` 到 `uint16_t/uint8_t`）编译失败而 NOT_RUN；完整 Release CTest NOT_RUN。新增 C++ 文件 clang-format PASS；`git diff --check` PASS/0；`cargo run --quiet -p repo-guard -- scan --root .` PASS/0，RG-001..009 通过，RG-003/004 仅既有全仓 warning。

Code Review：按 v0.9 顺序独立检查。审查中先复现并修复了直接 `CloseBrowser` 与 `CanClose` gate 形成的关闭超时，改为 Window 发起关闭、`CanClose` 驱动 `TryCloseBrowser`；补充三层真实 runtime-style 回读，并把 raw zoom double 收紧为 engine-api 的受校验类型。未新增锁、同步等待、网络、依赖、backend registry、远控或第二个 tab owner；P0/P1/P2/P3=0/0/0/0，APPROVE。由于本项不要求产品默认入口、beforeunload、多窗口、IME/读屏、性能、长稳或打包，最高状态 VERIFIED；这些门禁分别由 04W 及后续任务承担。

## 12. PLT-SHELL-04W 原子范围

- 状态：VERIFIED；依赖 01、03W VERIFIED。单一目标：让 Windows Alloy 路径以一个 controller 组合既有 `TabModel` 与 `AlloyContentViewHost`，收敛 BrowserView 异步创建、激活、关闭取消、迟到创建、renderer crash 和退出排空；不切产品默认入口、不实现标签栏。
- 允许修改：`tab_model.{h,cc}` 及其既有 contract test，新增 `alloy_tab_controller.{h,cc}`、`tests/alloy_tab_controller_probe.{h,cc}`，Windows integration scenario、CEF CMake 与本计划。禁止修改 app 装配、Chrome-style 默认路径、WindowClient 的业务 handler、Mac、popup/导航 UI、Cast/CNT/MDV、依赖与本地化。
- owner 与预算：`AlloyTabController` 是候选 Alloy 窗口内唯一 tab lifecycle owner，内部持有一个 `TabModel` 和一个 host；Browser ID、BrowserView 与 mount epoch 映射只保留一份、容量复用 32 tabs。页面/Renderer 不可构造 tab、epoch 或 capability；旧 epoch/未知 browser/view fail closed。预计 2 个生产文件、3 个测试/装配文件，净新增生产代码低于 650 行。
- 输入与关闭语义：复用 02/03W 的 opaque ID、mount result 和真实 CEF 150 `CloseBrowser(false)`/`DoClose`/`OnBeforeClose`；beforeunload 决策由 adapter 显式回送 close-cancel，controller 恢复原 tab/view，不把命令接受当作已关闭。创建期关闭保留有界 pending record，迟到 browser 一创建即强制关闭且永不进入 ready/active。
- 验收：Windows x64 Debug/Release 构建 product 与 integration；真实 Alloy 双视图验证异步 bind、切换、beforeunload cancel 后保状态/可重试关闭、创建期关闭的迟到 browser、renderer crash 强制回收、重复/旧 callback 拒绝、关闭顺序和最终排空。另跑 TabModel/02/03W 契约、完整适用 CTest（阻塞逐项记录）、source/forbidden scan、clang-format、diff check、repo-guard。
- 明确不做：不提供用户可见确认面板（归 15W），不实现 05W 标签控件、08W popup/多窗口、导航/权限/内置页/投屏、默认入口、性能长稳和打包；测试可控制 beforeunload callback，但不得冒充人工真机点击。

完成记录（2026-09-04，基线 `main/0fc8eed`，Windows 11 10.0.26200 x64）：新增候选路径 `AlloyTabController`，单一持有既有 `TabModel`、03W host 与 tab→mount/view/browser 映射；新增 `TabModel::CancelClose`。创建、绑定、激活、begin-close、adapter close-cancel、DoClose 后延迟撤销视图、OnBeforeClose 终态、renderer-gone 和 CloseAll 均在 CEF UI 线程收敛；创建期关闭保留有界 pending，迟到 browser 只登记 created 事实后强制回收，永不进入 ready。host 的 detached tombstone 在终态立即 forget，避免长期耗尽 32-tab 容量；旧 epoch/未知 view/browser/非法 create-failed error 均拒绝。

真实 CEF Harness 使用本地 HTTP dirty 页面与 Windows `SendInput` 建立真实 DOM 用户激活，证明点击进入页面、adapter 取消后 tab 恢复 Ready、同一 browser/page 的 JavaScript 状态仍为 42、可再次激活；另验证创建期关闭的迟到 browser、真实 Alloy runtime、renderer-gone callback 注入后的异步强制回收、重复/乱序 callback 拒绝与最终 Browser/Window/model/host 排空。CEF Alloy 多 BrowserView 默认 `OnBeforeUnloadDialog` 不作为本项产品 owner；04W 只验证受控 adapter 决策回送，用户可见可信确认面板和人工选择归 15W，不冒充已完成。

验证：固定 CEF 150，Visual Studio 2022/MSVC 19.44、Windows SDK 10.0.26100；Debug/Release 分别构建 `crayon_browser crayon_page_snapshot_cef_integration_win crayon_window_state_model_test` PASS/0。两配置最终定向运行 engine-api、content-view、公开头、forbidden scan、shared shell、registry、03W host、04W controller、Windows source contract、TabModel 共 10/10 PASS（Debug 5.39 s、Release 5.05 s）；最终 Review 修正后核心三项再次 3/3 PASS（Debug 17.85 s、Release 14.28 s）。完整 Debug 103 项 CTest 外层 604.2 s `TIMEOUT` 且工具未返回单项汇总；结合 03W 已审计的 101/102 与本项新增测试独立通过，只能证明 102 个唯一入口已有通过证据，既有 `media_host_v2_codec_test.cc:105` MSVC `/WX` C4244 构建阻塞仍未关闭，不能宣称全仓全绿。完整 Release CTest、人工 beforeunload 面板、性能长稳和 artifact `NOT_RUN`。

Code Review：按 v0.9 顺序独立检查并修复审查/真机 probe 发现的问题：CEF Views 回调栈内同步 RemoveChildView 导致 `Check failed: !iterating_`，改为回调登记事实、下一 UI task 逆序释放；close-cancel DTO 错带 capability 导致恢复失败；host detached tombstone 未回收；host 激活失败前模型先切 active；create-failed 非原子删除；窗口已存在时 BrowserView created callback 可早到的 Harness 映射。新增代码无锁、无同步等待、无公网、无新依赖、无 Chrome command、无第二 tab owner，生产文件 332/70 行、probe 506/18 行；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。下一项 `05W READY`。

## 13. PLT-SHELL-05W 原子范围

- 状态：VERIFIED；依赖 04W VERIFIED。单一目标：新增 Windows/CEF Views 自绘基础标签栏，纯投影 `TabModel` 的顺序与 active 状态，并把新建、激活、关闭意图回送唯一 lifecycle owner；不切产品默认入口。
- 允许修改：新增 `alloy_tab_strip.{h,cc}` 与独立 probe，Windows integration scenario、CEF CMake、本计划；为满足模型重排验收，可在 `TabModel` 增加唯一 owner 内的有界 `MoveTab` 命令及行为测试。若行为测试证明既有 basic tab state machine 有缺陷，可先补其 contract 再最小修复。禁止修改 app/默认入口、TabModel 身份所有权、omnibox/导航/popup/高级标签、Cast/CNT/MDV、依赖与本地化资源。
- owner 与预算：标签栏只保留最多 32 个 button binding 的渲染缓存，不持有第二份可写 tab 顺序/active 身份；每次从 `TabModel` 同步。稳定 command id 有界且不从页面输入；回调只产生 `new/activate/close` 意图。预计 2 个生产文件、3 个测试/装配文件，净新增生产代码低于 500 行。
- 验收：Windows x64 Debug/Release product build 与真实 CEF Alloy window Harness；验证两个以上 tab 的顺序/active 可见反馈、新建/激活/关闭事件、模型重排后的 UI 顺序、关闭替代焦点、32 容量禁用 new、窄宽布局、重复 Sync/Shutdown、按钮焦点与无 `ExecuteChromeCommand(IDC_NEW_TAB)`。运行 basic tab/04W/forbidden/source contract、完整适用 CTest（阻塞逐项记录）、clang-format、diff check、repo-guard。
- 明确不做：不创建实际 BrowserView（由 04W owner）、不实现拖动/固定/分组/恢复（09W）、标题/favicon（后续产品投影）、三语言/读屏完整回归（23W）、omnibox、popup、多窗口、默认入口、性能长稳或打包。

完成记录（2026-09-04，基线 `main/0fc8eed`，Windows 11 10.0.26200 x64）：新增 `AlloyTabStrip`，使用 CEF Views 自绘可聚焦 tab/close/new 按钮；只缓存最多 32 条 `TabId`→按钮绑定，每次从唯一 owner `TabModel` 投影顺序、active 与 closing 状态，稳定 command id 只按受控 slot 分配，回调只发出新建/激活/关闭意图。为闭合排序要求，`TabModel` 新增有界 `MoveTab`，不改变 tab identity 或 active identity；字符串由构造方注入，没有新增硬编码产品本地化 owner、Chrome command、BrowserView owner 或依赖。

真实 CEF Alloy window Harness 在当前 Windows 桌面使用 `SendInput` 点击真实 activate/close/new 按钮；验证三标签顺序和 active 可见灰态、模型重排后的按钮顺序、关闭 active 后选择相邻 tab、新建后 active、32 容量时禁用 new 且点击不发事件、320/1000 DIP 宽度布局、重复 Sync/Shutdown、按钮焦点与 `CEF_RUNTIME_STYLE_ALLOY`。首轮复现 close 按钮有 bounds 但无法命中，定位为 tab row 首选宽度没有包含 close 按钮、被父 row 裁剪；生产修复由 `CefPanelDelegate::GetPreferredSize/GetMinimumSize` 返回完整宽度。Harness 同时按 CEF 150 坐标契约使用 `ConvertScreenPointToPixels`，不把当前 DPI 假定为 100%。

验证：固定 CEF 150、Visual Studio 2022/MSVC 19.44、Windows SDK 10.0.26100；最终格式化后 Debug/Release 分别构建 `crayon_browser crayon_page_snapshot_cef_integration_win crayon_window_state_model_test` PASS/0。两配置分别运行 engine/content-view/headers/forbidden/shared-shell/registry/basic-tabs、03W、04W、05W、Windows source contract、TabModel 共 12/12 PASS（Debug 8.72 s、Release 6.49 s）；其中最终 05W 真 CEF probe Debug 2.10 s、Release 1.79 s。Debug 完整 CTest 104 项：103 PASS、`media_host_v2_codec` 因 executable 未生成 NOT_RUN，498.44 s；单独构建该既有目标稳定 FAIL/1：`media_host_v2_codec_test.cc:105` 在 MSVC `/WX` 下 C4244（`int` 到 `uint16_t/uint8_t`）。Release 完整 CTest `NOT_RUN`。VS 附带 clang-format 对 05W C++ 文件 PASS/0；误格式化既有 integration/test 文件的无关行已逐项恢复，最终仅留行为增量；`git diff --check` PASS/0；repo-guard PASS/0，RG-001..009 通过，RG-003/004 仅既有全仓 warning，artifact N/A。

Code Review：按 v0.9 的需求/边界→正确性→架构/API→并发/生命周期→安全/隐私→性能→测试/证据→维护/供应链独立检查。修复 Review/真机中发现的 tab row 裁剪、CEF DIP/Windows pixel 点击偏移，以及同步重建 Views 可能进入按钮回调迭代的问题（生产 `Sync/Shutdown` 在 dispatch 期间拒绝，调用方在下一 UI task 同步）。无锁、IO、网络、页面输入、后台线程、新依赖或第二份可写顺序/active；生产 2 文件约 265 行、probe 约 390 行，均低于预算；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖为产品默认入口、真实 BrowserView 创建（04W 已独立验证）、标题/favicon、完整三语言/读屏、性能长稳、Release 全量 CTest 与 artifact；分别归后续任务，不能把 05W 写成产品已切 Alloy。

## 14. PLT-SHELL-06W 原子范围

- 状态：VERIFIED；依赖 05W VERIFIED。单一目标：新增 Windows/CEF Views 自绘 omnibox 候选控件，复用现有 `browser_omnibox` parser/state 与显式配置的 provider，把编辑、异步本地建议、取消和提交转换为有界意图；不导航、不切产品默认入口。
- 允许修改：新增 `alloy_omnibox.{h,cc}` 与独立 probe，Windows integration scenario、CEF CMake、本计划；若测试证明 shared omnibox 对选择/输入安全缺少必要窄行为，可补 core/provider contract 后最小修复。禁止修改默认 app/Chrome location、TabController 导航、Profile/历史/书签 store、Cast/CNT/MDV、依赖和本地化资源。
- owner 与预算：控件只持有一个 `OmniboxStateMachine`、最多 8 条建议按钮、一个非零且有界递增 generation；建议 producer 由上层显式注入且只回送 generation/text，旧 generation 拒绝。搜索 URL 只由显式 `SearchProviderSet` 构建；空 provider 返回明确 no-provider，不内置/调用公网建议。用户文案由构造方注入。预计 2 个生产文件、3 个测试/装配文件，净新增生产代码低于 650 行。
- 安全与显示：提交前使用有界 `OmniboxInput/ParseOmniboxInput`；dangerous scheme fail closed，schemeless URL 使用显式 privacy defaults，搜索 terms 只经 provider percent encoding。地址回显不解码 punycode，不把非 ASCII host 显示为可混淆 Unicode；控制字符、credentials 与超长值不得原样进入 UI。editing generation 绑定文本，程序性地址更新不能覆盖用户正在编辑的内容。
- 验收：Windows x64 Debug/Release product 与真实 Alloy window Harness；验证文本输入、Enter 提交 URL/搜索/无 provider/dangerous，Escape 取消，8 项容量、建议点击与 keyboard selection、旧/重复 generation、导航地址回显与编辑保护、ASCII/punycode/Unicode host/credentials/control chars、窄宽布局、焦点、重复 Shutdown。运行 omnibox core/provider、05W/forbidden/source contract、完整适用 CTest（阻塞逐项记录）、clang-format、diff check、repo-guard。
- 明确不做：不调用 `LoadURL`（归 07W）、不读取真实历史/书签或联网建议、不保存 provider、不做证书/站点身份、三语言/完整 IME/读屏（23W）、popup、多窗口、默认入口、性能长稳或打包。

完成记录（2026-09-04，基线 `main/0fc8eed`，Windows 11 10.0.26200 x64）：新增 `AlloyOmnibox`，以 CEF Views 文本框和最多 8 条建议按钮组合既有 parser/state/provider；编辑 generation 非零递增，首次接受当前 generation 后即关闭该请求，旧值、重复值、非法建议和超量建议均拒绝或裁剪。键盘上下选择、Enter 提交、Escape 取消和鼠标建议点击只发出有界意图；搜索只使用上层显式注入的 provider，空集合明确返回 `kNoSearchProvider`，无公网建议或默认搜索商。共享 parser 补齐 scheme ASCII 大小写不敏感拒绝，state 补齐循环选择契约；导航地址更新先做完整安全校验，再原子提交状态，编辑中不覆盖用户文本。CEF 150 的空字符串 `SetText` 会触发 fatal，改为受 `setting_text` fence 保护的 SelectAll/Delete，不引入测试专用生产 API。

真实 CEF Alloy window Harness 使用 Windows `SendInput` 输入 `example.test`，实际按 Down/Enter 提交建议、鼠标点击建议和 Escape 取消；覆盖本地 provider 中文查询百分号编码、无 provider、大小写混合 JavaScript scheme、credentials、ASCII/punycode/Unicode host/control 字符安全回显、旧/重复 generation、8 项容量、程序地址更新与编辑保护、320/1000 DIP、焦点及重复 Shutdown。Review 中修复首次响应后仍可重复应用同 generation、idle/committed 状态仍发 cancel，以及无效导航地址先改变 state 后失败的非原子行为。

验证：固定 CEF 150、Visual Studio 2022/MSVC 19.44、Windows SDK 10.0.26100；Debug/Release 分别构建 `crayon_browser_omnibox_parser_test crayon_browser_omnibox_state_test crayon_browser_omnibox_provider_test crayon_page_snapshot_cef_integration_win crayon_browser` PASS/0。最终两配置均运行 parser/state/provider、forbidden API、03W、04W、05W、06W、Windows source contract、TabModel 共 10/10 PASS（Debug 27.86 s，Release 9.08 s），其中 06W 真 CEF probe Debug 9.60 s、Release 1.88 s；`git diff --check` PASS/0。05W 阶段的 Debug 完整 CTest 仍为 103/104 PASS，唯一 NOT_RUN 是既有 `media_host_v2_codec` 未生成；其单独构建稳定失败于 `media_host_v2_codec_test.cc:105` 的 MSVC `/WX` C4244，不属于本项。

Code Review：按 v0.9 顺序独立检查；控件没有锁、文件 IO、网络、持久化、CEF/OS 类型泄漏到共享 core、任意脚本/CDP 或默认 provider，回调重入由 dispatch fence 拒绝，建议和文本均有界；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖为真实导航/加载/证书身份（07W）、真实历史/书签建议、完整三语言/IME/读屏（23W）、产品默认入口、性能长稳、Release 全量 CTest 与 artifact。

## 15. PLT-SHELL-07W 原子范围

- 状态：VERIFIED；依赖 06W VERIFIED。单一目标：新增 Windows/CEF Views 自绘导航按钮与站点身份反馈，并以一个窄 adapter 将 Alloy omnibox 的 URL 意图、CEF 主 frame 导航事实及 back/forward/reload/stop 命令接通；不切产品默认入口。
- 允许修改：新增 `alloy_navigation.{h,cc}` 与独立 probe，Windows integration scenario、CEF CMake、shared navigation 的站点身份窄契约/测试、本计划。禁止修改旧 `TabController` Chrome 路径、默认 app、Profile/历史/书签/Cast/CNT/MDV、本地化资源、依赖或证书决策策略。
- owner 与预算：共享 `NavigationController` 仍是 tab 导航状态 owner；adapter 只绑定一个当前 Browser/Tab，CEF 回调必须带当前 browser identity 和单调 navigation id，旧 browser/旧 navigation 回调拒绝。UI 只持有投影和按钮，回调仅发命令意图。预计 2 个生产文件、3 个测试/装配文件，共享契约只做必要修复，净新增生产代码低于 750 行。
- 安全与身份：只有 browser-process adapter 的主 frame URL、loading/history capability、load error 与 certificate error 信号可以更新身份；页面标题、DOM、favicon、正文、JS 和 accessibility 文本不能选择或伪造身份。HTTPS 在当前 navigation 成功完成前不显示 secure；证书/SSL 失败显式显示 error 且默认不继续，HTTP/local/unknown 分别呈现。地址仍经 06W 安全显示入口。
- 验收：Windows x64 Debug/Release product 与真实 Alloy window Harness；使用本地确定性 fixture 验证 omnibox URL `LoadURL`、主 frame 重定向最终地址、加载中 reload→stop 切换、back/forward/reload/stop 的实际 CEF 调用和 capability 灰态、旧 navigation/browser 回调、HTTP/local/HTTPS pending/success/certificate error 身份、页面内容不能改变身份、窄宽布局、焦点与重复 Shutdown。运行 navigation/omnibox、06W/forbidden/source contract、完整适用 CTest（阻塞逐项记录）、clang-format、diff check、repo-guard。
- 明确不做：不绕过证书错误、不持久化例外、不实现权限/popup/外部协议确认（15W）、不实现多窗口、书签/历史/下载/页面工具、三语言/完整 IME/读屏（23W）、默认入口、性能长稳或打包。

完成记录（2026-09-04，基线 `main/0fc8eed`，Windows 11 10.0.26200 x64）：新增 `AlloyNavigation`，CEF Views back/forward/reload-stop/身份控件只投影共享 `NavigationController`，所有命令在 capability 通过后才调用当前绑定 Browser。omnibox 提交再次校验长度、scheme、userinfo 与安全显示后才 `LoadURL`；主 frame address/loading/load-end/load-error 以 browser identity、单调 navigation id、当前安全地址和失败状态 fencing。共享身份契约新增 HTTPS pending/certificate-error，大小写 scheme 统一；HTTPS 只有当前主 frame 成功完成后显示 secure，SSL 失败默认不继续。

真实 CEF loopback Harness 使用本地 HTTP fixture：实际 `LoadURL`，302 到最终地址，页面把 title 写成 `Secure` 仍显示 `Not secure`；Windows `SendInput` 点击 back/forward/reload/stop，验证真实 history capability、reload generation、可取消流式慢加载与按钮灰态；第二个真实 BrowserView 的回调被拒绝，错误 URL 的迟到 load-end 被拒绝。对同一 loopback HTTP 端口发起 HTTPS，真实 CEF 返回 `ERR_SSL_PROTOCOL_ERROR(-107)`，显示 `Certificate error`；审计仅排除该明确记录的预期错误行，其他 CSP/resource/CORS/net error 仍失败。首轮测试先复现并修复：测试清理比较已释放 BrowserView 的 fatal、OnLoadEnd 早于最终 loading=false 导致 history 灰态竞态，以及 SSL 失败后的错误页 `OnLoadEnd`/AddressChange 覆盖证书错误。

验证：固定 CEF 150、VS 2022/MSVC 19.44、Windows SDK 10.0.26100；Debug/Release 构建 `crayon_browser_navigation_test crayon_page_snapshot_cef_integration_win crayon_browser` PASS/0。最终两配置运行 navigation/omnibox、forbidden API、03W..07W、Windows source contract、TabModel 共 12/12 PASS（Debug 13.63 s，Release 10.53 s）；07W 真 CEF probe Debug 3.93 s、Release 3.61 s。clang-format、`git diff --check`、repo-guard PASS/0；RG-003/004 仅既有全仓 warning，artifact N/A。

Code Review：按 v0.9 独立检查；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖为受信任有效公网 HTTPS 证书真站（自动化保持离线，成功 HTTPS 状态由同一 browser-process adapter 契约验证）、证书例外 UI（明确不做）、多窗口、默认入口、完整 CTest/性能长稳与 artifact。

## 16. PLT-SHELL-08W 完成记录

- 状态：VERIFIED；依赖 05W、07W VERIFIED。单一目标：以 Windows Alloy window coordinator 复用既有 popup policy、`WindowStateMachine` 与每窗口 `AlloyTabController`，把可信用户手势 HTTP(S) popup 收敛为自有窗口/标签生命周期；不切产品默认入口。
- 允许修改：新增 `alloy_window_coordinator.{h,cc}` 与独立 probe、Windows integration scenario/fixture、CEF CMake、本计划；共享 windows/popup 窄契约只有稳定复现证明缺口时最小修复。禁止修改旧 Chrome `TabController` popup 路径、默认 app、Profile store、会话持久化（10W）、高级标签（09W）、权限/Cast/CNT/MDV、本地化资源和依赖。
- owner 与预算：`WindowStateMachine` 唯一拥有窗口 identity、opener、focus recency 与容量；每窗口 controller 唯一拥有其 tab/browser。coordinator 最多 8 窗口、每 opener 最多 4 popup；所有 pending request 有界，browser/window 关闭、创建失败和重复回调最终只释放一次。预计 2 个生产文件、3 个测试/装配文件，净新增生产代码低于 850 行。
- 安全：只接收 browser-process `OnBeforePopup` 的 source browser、主 frame URL、CEF user_gesture 与 disposition；URL 继续使用现有 bounded HTTP(S)-only policy，程序 popup、未知 opener、超容量、credentials/control 字符和关闭中 source fail closed。页面不能指定 native window id、Profile、能力或绕过 popup policy。
- 验收：Windows x64 Debug/Release product 与真实 CEF Harness；本地 fixture 验证真实用户点击 popup、程序 popup 拒绝、来源/opener、每 opener/全局容量、窗口/标签关闭、opener 关闭后 popup 独立、focus 恢复、创建失败/重复/迟到回调、窗口间 tab/BrowserView 隔离、窄宽和重复 Shutdown。运行 windows/popup/TabModel、07W/forbidden/source contract、完整适用 CTest、clang-format、diff check、repo-guard。
- 明确不做：不持久化/恢复会话（10W），不做跨窗口 tab 移动（09W），不实现权限确认、下载、页面工具、三语言/读屏、默认入口、性能长稳或打包。

完成记录（2026-09-04，工作区基线 `main/0fc8eed`，Windows 11 10.0.26200 x64）：新增 `AlloyWindowCoordinator`，由共享 `WindowStateMachine` 唯一管理 8 窗口/每 opener 4 popup 容量、opener 与 focus recency，每窗口独占 `AlloyTabController`。程序 popup、未知/关闭中 source、非 owner browser、非 HTTP(S)、credentials/control 字符均 fail closed；创建回调失败回滚状态。实际 CEF 证明 `CreateTopLevelWindow` 可在创建回调返回前同步进入 `OnWindowCreated`，因此仅允许已登记 window 的预期 attach 重入，其他 request/focus/close 重入仍拒绝。真实 Harness 还发现并修复首次 BrowserView 挂载后未激活、测试 owner 未释放 BrowserView 导致关闭不收敛两项生命周期缺陷。

验证：固定 CEF 150、VS 2022/MSVC 19.44、Windows SDK 10.0.26100。Debug/Release 构建 `crayon_page_snapshot_cef_integration_win`、`crayon_window_state_model_test`、`crayon_browser` PASS/0；最终 Debug/Release 的 windows contract、03W..08W Alloy Harness、Windows source contract、window state 共 9/9 PASS（Debug 15.96s，Release 12.16s），08W 真 CEF 分别 3.51s/1.53s。fixture 通过 BrowserHost 鼠标事件产生 `OnBeforePopup(user_gesture=true)`，验证真实第二个 Alloy Browser/Window identity、opener/Profile 继承、关闭 opener 后 popup 独立存活、focus 恢复与最终双窗口释放；无手势/credentials 拒绝及创建失败回滚由同一 browser-process coordinator 直接固定向量验证。`git diff --check`、repo-guard PASS/0；RG-003/004 为既有全仓 warning，artifact N/A。

Code Review：按 v0.9 顺序独立检查需求边界、CEF 同步/异步回调、唯一 owner、popup URL/手势、容量、关闭顺序、测试证据与可维护性；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖：默认产品入口、会话持久化、高级标签、权限/下载/Cast/CNT/MDV、本地化、性能长稳与 artifact，分别保留给 09W..27W；macOS 对称任务保持 TODO。

## 17. PLT-SHELL-09W 完成记录

- 状态：VERIFIED；依赖 08W VERIFIED。单一目标：让 Windows Alloy tab/window owner 消费既有 advanced tabs 状态机，接通固定、复制、静音、搜索、分组和保持同一 BrowserView 的跨窗口移动；不切默认入口。
- 允许修改：`browser/shared-ui/tabs/advanced/**` 的窄兼容 API/契约、`alloy_tab_controller`、`alloy_window_coordinator`、新增 `alloy_advanced_tabs` 与独立 probe、CEF CMake/fixture、本计划。禁止会话持久化、书签/历史/下载/权限、Cast/CNT/MDV、本地化资源、默认 app、依赖。
- owner 与边界：`TabModel` 继续唯一拥有真实 CEF tab/browser lifecycle；advanced model 只拥有可见顺序及 pin/mute/group/search 投影，真实创建/关闭/移动成功后才同步。复制使用当前可信 browser URL 创建独立 BrowserView；静音只调用该 tab 的 `CefBrowserHost::SetAudioMuted`；跨窗口移动必须 reparent 同一 BrowserView/browser、更新 mount epoch 与 callback owner，不导航、不复制 Profile/授权、失败时回滚或 fail closed。
- 验收：Windows x64 Debug/Release product 与真实 CEF Harness；固定排序、复制 identity/URL、静音 readback、搜索、分组容量/清理、同 Profile 双窗口移动后 browser identifier/表单 DOM 状态不变、旧窗口回调拒绝、新窗口继续导航/关闭；非法/关闭中/跨 Profile/容量/重复 move 拒绝。运行 advanced/basic tabs、03W..08W、source/forbidden contract、完整适用 CTest、format/diff/guard。
- 明确不做：不跨 Profile 搬 Cookie/授权，不以关闭重建冒充移动，不持久化 advanced 状态（10W），不实现拖放手势/菜单入口（17W）、默认入口、性能长稳或打包；若底层 CEF view 不能安全 reparent，先拆 09W2 并保持任务未完成。

完成记录（2026-09-04，工作区基线 `main/0fc8eed`，Windows 11 10.0.26200 x64）：`AlloyTabController` 消费既有 `AdvancedTabStripStateMachine`，真实 TabModel 仍唯一拥有 CEF identity/lifecycle，advanced 层只保存 pin/mute/group/search/可见排序投影。共享 advanced API 新增“复制到已由真实后端分配的 target identity”，避免模型私造 copy id 成为第二 owner。静音调用目标 `CefBrowserHost` 并 readback；复制先创建独立 BrowserView，再复制 metadata。跨窗口 move 通过 shell 内部 transfer capsule 撤挂/重挂同一 BrowserView/browser，目标 Profile/容量/closing 状态先校验，mount epoch 更新；失败恢复源 identity，不关闭重建、不导航。

真实 CEF Harness 首先稳定复现并修复一个同步回调缺陷：向已显示容器添加第二个 BrowserView 时，`OnBrowserCreated` 可在 `BeginCreate` 返回前发生，旧实现因 mount 后才登记 owner 而拒绝合法回调；现改为 owner 先登记、挂载失败原子回滚。Harness 验证第二 browser identity/URL 独立，pin/mute/group 复制及 audio muted readback；同一 browser 在 popup/primary 两个 host 间往返 reparent 后 browser identifier、DOM dataset/title 和 advanced 状态均保持，旧 owner 不再承认 browser，回到新 owner 后继续正常关闭。

验证：Debug 增量构建产品/集成/advanced/TabModel target PASS；`advanced_tab_strip_contract`、`window_state_model_test`、受影响 Alloy controller/coordinator 4/4 PASS（6.37s），最终真实 controller/coordinator 2/2 PASS（5.98s）。Release 构建 `crayon_browser`、CEF integration、advanced、TabModel target PASS/0；advanced、03W..09W Alloy、source contract、TabModel 共 9/9 PASS（13.92s），其中 09W 真实 coordinator 3.11s。一次包含 CMake 重生成和全量链接的 120s 构建命令 TIMEOUT/124，但日志显示相关 targets 随后完成；不能把该轮记为通过，后续增量 Debug 与完整 Release 均 PASS。

Code Review：按 v0.9 独立检查唯一 owner、同步 callback reentrancy、transfer 回滚、Profile/容量、browser identity、静音副作用、DOM 状态与关闭顺序；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖：advanced 状态持久化（10W）、最终菜单/拖放手势（17W）、默认产品入口、长稳与 artifact；macOS 09M 保持 TODO。

## 18. PLT-SHELL-10W 原子范围

- 状态：VERIFIED；依赖 08W VERIFIED。单一目标：将既有 `browser/session` 恢复策略绑定 Windows Alloy window/tab snapshot 与恢复执行，覆盖正常退出和崩溃 checkpoint；不切默认入口。
- 允许修改：`browser/session/**` 的向后兼容完整 tab/window snapshot 与 codec、`alloy_session_restore.{h,cc}`、Alloy controller/coordinator 的只读 snapshot/受控 restore API、独立 contract/CEF fixture、CEF CMake、本计划。禁止 bookmarks/history/download/permission/Profile UI、Cast/CNT/MDV、默认 app、依赖。
- owner 与安全：session 模块唯一拥有持久化 schema/epoch/损坏拒绝；Alloy adapter 只把当前 regular Profile 的 bounded HTTP(S)/受控内部 URL、active index 与 advanced metadata 投影为 snapshot。无痕、credentials/control/超长 URL、跨 Profile、重复 window/tab、旧 epoch 和超容量 fail closed；恢复创建成功回调前不宣称完成，部分失败显式返回且保留原文件供重试。
- schema：新增 v2 完整 tab snapshot，保留只含 window/tab-count 的 v1 读取并明确降级为新标签占位，不静默丢数量；写入只产 v2，采用长度/数量上限与确定性编码。平台文件 adapter 使用同目录临时文件＋原子 replace，损坏主文件不覆盖。
- 验收：Windows x64 Debug/Release product、session contract 与真实 CEF Harness；正常/崩溃恢复、checkpoint tail 丢弃计数、无痕零持久化、v1/v2、损坏/重复/超量、epoch fencing、两个窗口多标签 URL/active/pin/mute/group 恢复及重复恢复幂等。运行 03W..09W、session/source/forbidden、完整适用 CTest、format/diff/guard。
- 明确不做：不恢复表单/页面 JS 内存，不持久化 Cookie/授权/文件 grant/投屏证明，不实现历史最近关闭 UI（12W）、Profile 选择（14W）、默认入口、性能长稳或打包。

## 19. PLT-SHELL-10W 完成记录

- 状态：VERIFIED。`browser/session` 新增有界 v2 profile/window/tab snapshot 与确定性 codec，写入只产 v2，兼容读取 CRLF/LF 的 v1 window/tab-count 并按原数量恢复受控新标签占位；拒绝未知版本、损坏字段、重复窗口、credentials/control/超长/不允许 URL、超窗口/标签/分组容量。既有 policy contract 继续拥有正常/崩溃 checkpoint、tail dropped 计数、无痕零记录、跨 Profile 与 epoch fencing。
- Windows Alloy 接线：coordinator 只读投影 ready regular Profile 的窗口、ordered tabs、active index、URL 与 pin/mute/group；受控 restore 预检 Profile、窗口容量/ID 后一次性建立 controller owner，重复恢复 fail closed。TabController 的 `BeginRestore` 恢复高级状态；真实 CEF 首次复现页面 load 会重置过早设置的 audio mute，现由 `SynchronizeRuntimeState` 在 load 完成后重放并校验真实 host readback。
- 文件 adapter：`AlloySessionRestore` 使用同目录唯一临时文件、flush、Windows atomic replace/move；2 MiB 上限、损坏不解码且不改主文件、Profile mismatch 拒绝、无痕返回 skipped 且不创建文件。部分 host restore 失败显式返回，checkpoint 文件不删除，允许下次重试。
- 验证：Windows x64 Debug/Release 全 target build PASS。session/file/真实 CEF 定向 Debug 3/3 PASS（4.99s）；Release 产品与 03W..10W 8/8 PASS（13.24s）；真实 Harness 完成两窗口、多标签 URL、active index、pin/mute/group/audio readback、v2 编解码后关闭、重建与重复恢复拒绝。第一次 Debug 全量 CTest 为 107/108，唯一 `media_host_v2_codec` 因测试初始化的 MSVC C4244 `/WX` 未生成 executable；补显式 `size_t/uint8_t` 测试值后该 target Debug/Release 编译通过，随后 Debug/Release 全 target build PASS，完整 CTest 两配置各 108/108 PASS（合计 566.7s）。`git diff --check` PASS；`cargo run --quiet -p repo-guard -- scan --root .` PASS（仅既有阈值 warnings）。
- Code Review：按 v0.9 复核 schema 前后兼容、容量/URL/Profile 边界、原子替换失败路径、同步 CEF callback、restore partial failure、generation/epoch owner、静音副作用与关闭逆序；P0/P1/P2/P3=0/0/0/0，APPROVE。最高 VERIFIED。未覆盖：默认产品启动时机/偏好 UI（14W/24W）、历史最近关闭（12W）、长稳/断电恢复与安装升级（27W）；macOS 10M 保持 TODO。

## 20. PLT-SHELL-11W 原子范围

- 状态：IN_PROGRESS；依赖 07W VERIFIED。单一目标：把既有 per-Profile `browser/bookmarks` store/codec 与 `shared-ui/bookmarks` bar view 接到 Windows Alloy 候选 host，提供书签星标、书签栏和受控管理操作；不切默认入口。
- 允许修改：书签 store/codec 为满足导入导出原子语义所需的向后兼容最小增量、`alloy_bookmarks.{h,cc}`、Alloy controller/独立 CEF probe 的只读 URL/导航接线、CEF CMake、本计划。禁止历史/下载/权限/Profile UI、Cast/CNT/MDV、默认 app、依赖。
- owner 与边界：每个 adapter 实例只绑定一个合法 ProfileId；domain store 唯一拥有树、ID、URL 校验与容量，bar 只持 bounded id/title/kind projection。网页、标题与导入文本不可信，不能跨 Profile、注入 credentials/control URL 或改变导航授权；导入先完整 decode/validate 到临时 store，成功才替换，失败保留当前树并返回稳定错误。
- 验收：Windows x64 Debug/Release product、bookmarks/bar contract 与真实 CEF Harness；当前页 add/edit/remove/star readback，bookmark/folder 打开到正确 tab，搜索上限，bar 显隐，v1 codec 导入导出 roundtrip、损坏/重复/超量原子拒绝，两个 Profile 零污染，失败可见且 shutdown/迟到动作拒绝。回归 03W..10W、source/forbidden、完整适用 CTest、diff/guard。
- 明确不做：不同步云端/账号，不抓 favicon，不增加联网建议，不实现历史/最近关闭、拖放跨进程、默认入口或 macOS UI。

## 21. PLT-SHELL-11W 完成记录

- 状态：VERIFIED。新增 per-Profile `AlloyBookmarks` adapter，复用 domain store/codec 与 bar state machine，提供当前页新增/星标、编辑、删除、有界搜索、栏显隐、文件夹子项投影、当前/新标签打开回调、确定性导入导出和幂等 shutdown；adapter 不保存 CEF/OS 类型，两个实例按强类型 Profile 隔离。导入先完整解码到候选 store，投影成功才提交，任何失败回滚原树。
- Windows 文件修复：真实验证发现既有 `std::rename` 在 Windows 无法原子覆盖已存在书签文件；改为 Windows `MoveFileExA(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`，同进程唯一 staging 名，保留非 Windows 原语，并补连续两次保存后读取新内容的回归。搜索 contract 增补超过 64 项时精确截断。
- 验证：Windows x64 Debug product/adapter/CEF build PASS，bookmarks domain、bar、adapter、真实 Alloy navigation 4/4 PASS（4.78s）；Release 对应产品与 4/4 PASS（3.10s）。真实 CEF 从当前页建书签，经 adapter 回调导航打开并核对 URL、starred/search readback；首次手写 codec fixture URL 长度 19/实际 18 被正确拒绝，修正 fixture 后通过，未放宽生产解析。最终 Debug/Release 全 target build PASS，完整 CTest 两配置各 109/109 PASS（命令总耗时 599.9s）。
- Code Review：按 v0.9 复核 per-Profile owner、URL/domain 校验、import 原子性、Windows replace 失败路径、UI 投影上限、callback 拒绝、shutdown 迟到动作与真实导航；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖：文件选择与 OS 拖放（17W）、历史（12W）、默认入口与安装升级；macOS 11M 保持 TODO。

## 22. PLT-SHELL-12W 原子范围

- 状态：IN_PROGRESS；依赖 10W VERIFIED。单一目标：将既有 per-Profile `browser/history` store/codec 与 history view 接到 Windows Alloy 候选 host，并把 TabController 的真实关闭事件投影到最近关闭恢复；不切默认入口。
- 允许修改：history store/codec 的 Windows 原子持久化与有界测试最小增量、`alloy_history.{h,cc}`、Alloy controller/coordinator 的关闭 snapshot/恢复回调、独立 contract/CEF probe、CEF CMake、本计划。禁止书签之外的前序重构、下载/权限/Profile UI、Cast/CNT/MDV、默认 app、依赖。
- owner 与边界：每个 adapter 只绑定一个 Profile 和 regular/ephemeral 属性；history store 唯一拥有 visits、recently closed、删除与搜索。只有已提交主 frame 导航记录 visit；关闭只保存受控 HTTP(S)/内置 URL 与标题，不保存表单、Cookie、授权或页面数据。无痕拒绝记录/序列化；跨 Profile、credentials/control URL、倒置时间范围、迟到 generation fail closed。
- 验收：Windows x64 Debug/Release product、history/view/adapter contract 与真实 CEF Harness；导航记录/搜索 newest-first、单 URL/闭区间/clear 删除、最近关闭 LIFO/容量、关闭后恢复到正确 Profile/新 Tab、无痕零记录、损坏/超量导入原子拒绝、重复恢复仅消费一次、失败反馈与 shutdown。回归 03W..11W、source/forbidden、完整适用 CTest、diff/guard。
- 明确不做：不同步云端，不存下载/搜索建议，不恢复页面 JS/表单，不实现 Profile UI、默认入口或 macOS UI。

## 23. PLT-SHELL-12W 完成记录

- 状态：VERIFIED。新增 per-Profile `AlloyHistory` adapter，复用 history store/codec/view，使用单调 navigation generation 只接受当前未提交主 frame 导航一次；提供 newest-first 有界搜索投影、URL/闭区间/clear 删除、确定性导入导出、幂等 shutdown。最近关闭 LIFO 由 store 唯一拥有；打开新标签回调失败时按原 timestamp/title/url 放回栈顶，成功仅消费一次。
- 隐私与持久化：ephemeral 实例对 visit、closed tab、import/export 均明确拒绝，独立 regular Profile 不受污染。history import 先解码候选，view 投影失败回滚原 store。修复既有 history `std::rename` 在 Windows 无法覆盖现有文件的问题，使用 `MoveFileExA(REPLACE_EXISTING|WRITE_THROUGH)` 并补连续覆盖保存回读。
- 验证：Windows x64 Debug product/domain/adapter/CEF build PASS，history domain、view、adapter、真实 Alloy navigation 4/4 PASS（5.44s）；Release 对应构建与 4/4 PASS（3.13s）。真实 CEF 将已关闭 URL 通过 history owner 单次消费到独立 BrowserView，验证最终 URL、visit/search 投影和 recent count=0。最终 Debug/Release 全 target build PASS，完整 CTest 两配置各 110/110 PASS（命令总耗时 592.4s）。
- Code Review：按 v0.9 复核 generation fencing、无痕零落盘、Profile owner、最近关闭失败回滚、删除边界、导入原子性、Windows replace 和 callback/shutdown 生命周期；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖：下载（13W）、Profile 选择（14W）、默认入口、安装升级与长期磁盘故障；macOS 12M 保持 TODO。

## 24. PLT-SHELL-13W 原子范围

- 状态：VERIFIED；依赖 07W VERIFIED。单一目标：将既有 `CefDownloadHandler`、download domain 与 shelf view 接到 Windows Alloy 候选 host，提供下载可见状态与受控操作；不切默认入口。
- 允许修改：download domain/view 为真实 CEF 状态映射所需的最小兼容增量、`alloy_downloads.{h,cc}`、既有 download handler 的 observer adapter、Windows 受控保存/打开位置接口、独立 contract/CEF fixture、CEF CMake、本计划。禁止 history/bookmarks 重构、权限通用面板（15W）、Cast/CNT/MDV、默认 app、依赖。
- owner 与安全：CEF handler 继续唯一拥有下载 callback；domain 唯一拥有状态转换，shelf 只持 bounded projection。文件名/Content-Disposition/页面输入不可信，保存路径只来自用户选择或已验证产品目录；危险/中断状态不能伪装完成，打开位置只对 completed 且存在的受控路径启用。取消/暂停/续传命令绑定 download id/generation，迟到 callback fail closed。
- 验收：Windows x64 Debug/Release product、download domain/shelf/adapter 与真实 CEF 本地下载 Harness；开始/进度/完成/中断/危险、取消、pause/resume、重名/路径穿越拒绝、用户取消保存、打开位置 gate、容量/旧 generation/shutdown。回归 03W..12W、source/forbidden、完整适用 CTest、diff/guard。
- 明确不做：不绕过 Safe Browsing/系统保护，不自动打开下载，不赋予任意路径/文件系统能力，不实现默认入口或 macOS UI。

## 25. PLT-SHELL-13W 完成记录

- 状态：VERIFIED。新增纯 C++17 `AlloyDownloads` observer adapter，CEF handler 继续唯一拥有 callback，domain 唯一拥有状态转换，shelf 只保存 64 项有界脱敏投影。下载开始、进度、暂停/恢复、取消、危险确认/丢弃、完成/中断、打开位置和 shutdown 均绑定 ID 与单调 generation；终态清理 callback/generation，暂停后的在途 progress 不误报失败，旧 generation 不污染当前项。
- 安全：保存目标只由已验证目录和净化文件名组成，重名有界解析；补齐 Windows ADS/非法字符及 `CON/PRN/AUX/NUL/COM1..9/LPT1..9` 设备名拒绝，路径穿越不能逃离受控目录。危险扩展必须显式确认，未完成/失败/取消不能打开位置，页面 URL/文件名不进入权限或路径授权。
- 验证：Windows x64 Debug/Release product、download domain/shelf/adapter 与真实 CEF integration build PASS。两配置 `download_domain_contract|download_shelf_contract|alloy_downloads_contract|alloy_navigation_windows` 各 4/4 PASS（3.93s/3.89s）；真实 loopback attachment 在受控临时目录落盘并投影 completed 后才允许打开位置。Review 前完整 Debug/Release CTest 各 111/111 PASS，组合命令 PASS/0、607.1s；Review 修复后受影响矩阵两配置再次 4/4 PASS。
- Code Review：按 v0.9 复核 owner、callback/generation 生命周期、危险状态、Windows 路径/设备名、容量、旧事件和 shutdown；发现并关闭 interrupted callback 泄漏、Windows 特殊文件名、暂停在途事件与无效完成进度四项，P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖：系统 Safe Browsing UI/真实恶意样本（明确不绕过）、用户任意目录选择面板（17W）、默认入口与 macOS UI。

## 26. PLT-SHELL-14W 原子范围

- 状态：VERIFIED；依赖 08W、10W VERIFIED。单一目标：将既有 Profile picker、typed preference store/settings view 与 `ProfileContextFactory` 接到 Windows Alloy 候选 host，使 regular Profile、guest/incognito 和设置 readback 具有真实隔离与显式失败反馈；不切默认入口。
- 允许修改：preferences codec 的 Windows 原子覆盖最小修复、profiles/settings view 的必要兼容增量、`ProfileContextFactory` 的按 Profile/临时会话生命周期、`alloy_profile_settings.{h,cc}`、独立 contract/真实 CEF probe、CEF CMake、本计划。禁止权限通用面板（15W）、页面工具、文件入口、Cast/CNT/MDV、默认 app、依赖。
- owner 与隐私：每个 regular Profile 只复用自己的 persistent `CefRequestContext` 和 preferences；每个 guest/incognito window 使用新建 memory-only context，不跨 Profile/窗口缓存，不进入 session/bookmark/history/preferences 持久化。Profile ID 仅以既有 hash 进入 cache path；设置只接受 closed typed keys，写失败保留原值并显示稳定 failure token。清理完成回调绑定 request/generation，超时/失败必须显式报告，不能宣称已清理。
- 验收：Windows x64 Debug/Release product、preferences/settings/profiles/adapter contract 与真实 CEF Harness；两个 regular Profile cookie/cache 隔离、同 Profile context 复用、两个临时 context 独立且无 cache path、切换/未知/忙、设置 apply/restart readback/reset、连续覆盖、损坏/超量拒绝、写失败回滚、清理成功/失败/旧 generation/shutdown。回归 03W..13W、source/forbidden、完整适用 CTest、diff/guard。
- 明确不做：不实现账号同步/登录、跨 Profile 数据迁移、密码/支付/任意设置键，不静默清理失败，不切默认入口，不把 Windows 证据代替 macOS。

## 27. PLT-SHELL-14W 完成记录

- 状态：VERIFIED。新增 `AlloyProfileSettings`，以既有 picker、typed preference store 和 settings 状态机为唯一模型，绑定 regular/guest Profile 切换、独立无痕窗口、设置保存/reset 与清理 generation。设置只接受闭集 typed key；保存失败同时回滚 domain 值和 dirty 投影并显示稳定 token；清理启动失败、完成失败、旧 generation、未确认失败和 shutdown 均 fail closed。Windows preference 文件改用唯一 staging 加 `MoveFileExA(REPLACE_EXISTING|WRITE_THROUGH)`，连续覆盖后 restart readback 读取最新值。
- CEF Profile 隔离：`ProfileContextFactory` 对 regular Profile 复用以 opaque SHA-256 子目录为 cache path 的持久 context，每次 guest/incognito 创建全新 memory-only context，shutdown 后拒绝创建。真实 CEF 首先复现同步使用未初始化 context 的 DCHECK，再复现同一 Views Window 混挂不同 Chromium Profile 的 observer 重复注册；最终按产品边界等待四个 context 初始化，并以四个独立 Alloy 顶层窗口装配两个 regular Profile 和两个临时 context。Harness 验证同 Profile context 复用、异 Profile cache path 不同、临时 context 无 cache path，以及 `a`/`b`/`private` Cookie 跨 Profile/隐私窗口零串流。
- 验证：Windows x64 Debug/Release product build PASS；两配置 `preferences_contract|settings_page_contract|profile_picker_contract_test.cc|alloy_profile_settings_contract|alloy_profile_context_windows|profile_id_validator_test|profile_context_contract` 各 7/7 PASS（3.87s/3.07s）。Review 修复后 adapter/真实 CEF/profile-id 两配置各 3/3 PASS（3.98s/3.27s）。最终 Debug/Release 完整 CTest 各 113/113 PASS，串行总耗时 587.7s；`git diff --check` PASS；repo-guard PASS，RG-003/004 仅既有全仓 warning。
- Code Review：按 v0.9 复核 Profile/cache 唯一 owner、CEF 异步初始化与一窗口一 Profile、隐私数据流、原子替换、设置回滚、清理 generation、失败退出和逆序释放；审查中关闭保存失败 dirty 假状态、locale 相关 Profile ID 字符集与 probe 早期失败挂起三项。P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖：账号同步/登录、跨 Profile 数据迁移、Windows 长期磁盘故障、默认入口；macOS 14M 保持 TODO。

## 28. PLT-SHELL-15W 原子范围

- 状态：IN_PROGRESS；依赖 07W、14W VERIFIED。单一目标：将既有 permission store、site-controls/prompt queue、证书错误模型和 popup policy 接到 Windows Alloy 候选 host，并补外部协议的可信用户确认，使所有敏感请求都由 Browser process 的原生 surface 决策；不切默认入口。
- 允许修改：`browser/cef-shell/src/browser/permission/**` 的 callback observer/fencing 最小增量、`shared-ui/site-controls` 与 `shared-ui/windows` 的必要兼容增量、`alloy_site_controls.{h,cc}`、Alloy client/coordinator 的权限/证书/popup/外部协议窄接线、独立 contract/真实 CEF fixture、CEF CMake、本计划。禁止文件/拖放/剪贴板入口（17W）、页面工具、Cast/CNT/MDV、默认 app、依赖。
- owner 与安全：CEF callback 继续唯一拥有一次性回调；Alloy adapter 只保存有界 prompt projection 与 request/generation。请求必须绑定可信 Browser/主 frame、当前 navigation、canonical HTTP(S) origin、权限 kind 与 deadline；导航、关闭、超时、重复决策和旧 generation 默认拒绝。证书错误只来自 CEF Browser process 且默认拒绝，不持久化绕过；programmatic popup 默认拒绝，可信手势 popup 仍走 08W policy；外部协议只允许闭集 scheme、显示脱敏目标并逐次确认，不接受网页自绘结果。
- 预算与输入：复用 `PermissionStore`、`PermissionPromptQueue`、`SiteControlsStateMachine`、`PopupPolicy` 和固定 CEF 150 permission/request/display/lifespan callback；预计新增一个 production adapter、一个 callback bridge 与独立 tests/probe，总净新增 production 低于 750 行，不新增持久化 schema、OS shell 通用执行器或第二权限 owner。
- 验收：Windows x64 Debug/Release product、permission/site-controls/windows/adapter contract 与真实 CEF Harness；camera/mic/geolocation/notification/clipboard 的 origin 显示、allow once/deny/TTL、队列容量、导航/关闭/timeout/旧 generation，证书默认拒绝与不可伪造，programmatic/trusted popup，外部协议 allowlist/用户取消/重复回调。回归 03W..14W、source/forbidden、完整适用 CTest、format/diff/guard。
- 明确不做：不绕过证书/SmartScreen/OS 安全提示，不保存永久证书例外，不启动任意 scheme/命令，不把网页 DOM/标题/按钮当可信确认，不实现文件选择或默认产品切换，不把 Windows 证据代替 macOS。

## 29. PLT-SHELL-15W 完成记录

- 状态：VERIFIED。新增 per-view `AlloySiteControls`，复用既有 permission store、prompt queue、site-controls 和 popup policy；权限、证书与外部协议请求绑定 canonical origin/navigation generation/deadline，容量固定为 4，导航、超时、关闭、重复/乱序决策和 request-id 耗尽均一次性 fail closed。权限只提供 session/有界 TTL，证书只允许本次继续且不持久化，外部协议限制为 `mailto`/`tel`/`sms` 并始终先阻断 CEF 的 OS execution；programmatic/trusted popup 继续由 08W policy 唯一决策。
- 真实 CEF：Windows Alloy Harness 使用 CEF 官方离线 TLS TestServer 和过期证书，先验证默认拒绝，再切换新 generation 单次继续；本地 fixture 真实触发 Notification permission；`mailto:` 真实进入 IO-thread `OnProtocolExecution` 并观察 `allow_os_execution=false`，随后由 UI-thread adapter 逐次拒绝。测试资源按官方 `ceftests_files/net/data/ssl/certificates` 布局只复制到 integration target，并在初始化前调用 `CefSetDataDirectoryForTests`，不进入产品 artifact。
- 验证：Windows x64 Debug/Release `crayon_browser`、adapter、CEF Harness build PASS；两配置 `windows_contract|site_controls_contract|alloy_site_controls_contract|alloy_security_windows|alloy_window_coordinator_windows|permission_store_test|permission_contract` 各 7/7 PASS（8.79s/8.39s），真实安全探针 4.06s/3.63s；最终 Debug/Release 完整 CTest 各 115/115 PASS（约 385.0s/281.66s，组合命令 666.7s）；`git diff --check` PASS；repo-guard PASS，RG-003/004 仅既有全仓 warning。
- Code Review：按 v0.9 检查可信来源、origin/generation/deadline、FIFO/容量、callback 重入、导航/关闭逆序释放、证书不可持久绕过、外部协议 OS 执行阻断与 test-only 资源。审查中关闭无效 permission/protocol 决策提前消费、导航取消 callback 在旧 authority 下重入、跨 IO/UI 非原子读写及缺失 CEF test data root 四项；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖：真实 camera/mic/geolocation 硬件授权与用户可见视觉审校、Windows 系统外部应用实际启动（安全边界明确不由自动测试启动）、默认入口；macOS 15M 保持 TODO。

## 30. PLT-SHELL-16W 原子范围

- 状态：IN_PROGRESS；依赖 07W VERIFIED。单一目标：把既有 find/zoom/fullscreen/page-output 状态机接到 Windows Alloy 当前内容视图，并以固定 CEF 150 能力逐项验证；不切默认入口。
- 允许修改：`shared-ui/page-tools` 的最小兼容增量、`alloy_page_tools.{h,cc}`、Alloy client/window delegate 的 find/fullscreen/PDF 窄接线、独立 contract/真实 CEF fixture、CEF CMake、本计划。禁止文件选择/拖放/剪贴板（17W）、网页 Markdown（19W）、Cast/CNT/MDV、默认 app、依赖。
- owner 与边界：adapter 仅操作当前 Browser/Window 并绑定 navigation generation/Profile；查找 callback、PDF completion、全屏 transition 在导航/关闭后拒绝旧结果。缩放只接受共享 closed set 并映射 CEF zoom level；打印只调用系统 Print，不自动确认；PDF 路径必须来自上层已授权的受控输出，adapter 不创建通用任意路径入口。PiP 因固定 CEF 无壳层通用触发命令，能力矩阵明确 unsupported。
- 预算与验收：预计新增一个 CEF adapter 和一个真实 probe，production 净新增低于 600 行。Windows x64 Debug/Release build；真实页面查找/前后匹配/清除、25%..500% 缩放/reset、window/content fullscreen 进入退出、PrintToPDF success/failure/navigation fencing、系统 Print 命令存在且不静默代用户确认；page-tools/adapter/source/full CTest、format/diff/guard。明确不做：不自动操作系统打印对话框、不启用 kiosk printing、不把 PDF 等同物理打印、不通过 JS/CDP 触发 PiP。

## 31. PLT-SHELL-16W 完成记录

- 状态：VERIFIED。新增 `AlloyPageTools`，以既有 find/zoom/fullscreen/page-output 状态机为唯一模型，按 Browser/Profile/navigation generation 绑定当前内容视图；导航先切换 authority 再取消旧 find/PDF，拒绝 generation 回退，关闭时逆序撤销 callback。缩放按 CEF 固定步长映射；PDF 只接受上层授权的 Windows 绝对 `.pdf` 路径并限制平台最大路径长度；系统打印只暴露用户命令，不由自动化确认原生对话框。能力矩阵如实声明固定 CEF 150 不提供壳层通用 PiP 触发能力。
- 真实 CEF：离线 fixture 含三个匹配项，验证 start/next/previous/end、200% 与 reset、window fullscreen 进入退出、非空 PDF 输出、导航取消 completion、generation 回退拒绝，以及相对路径/非 PDF 路径 fail closed。Windows x64 Debug/Release `crayon_browser` 与 integration target build PASS；最终两配置 `page_tools_contract|alloy_site_controls_contract|alloy_navigation_windows|alloy_security_windows|alloy_page_tools_windows|windows_cef_shell_source_contract` 各 6/6 PASS（12.25s/9.66s）。此前在本工作树 Debug/Release 完整 CTest 各 115/115 PASS；本节末次路径边界增量由上述关联矩阵覆盖，最终发布前仍由 27W 重跑全量。
- Code Review：按 v0.9 复核内容视图 owner、generation/Profile fencing、callback 重入、窗口生命周期、任意路径边界和用户确认。审查中关闭 CefRefPtr wrapper 身份误判、导航取消 callback 在旧 authority 下重入、generation 回退和任意 PDF 路径四项；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖：不自动打开 Windows 系统打印对话框，未验证真实纸张打印；PiP 明确 unsupported；默认产品入口未切换；macOS 对应项保持 TODO。

## 32. PLT-SHELL-20 原子范围

- 状态：IN_PROGRESS；依赖 02、PLT-CAST-R08u1 VERIFIED。单一目标：移除 `CastEntrySurface` 对 Chrome LOCATION toolbar 与 Chrome runtime 的依赖，使入口由候选 Alloy 自绘栏显式拥有，同时保持既有选择 DTO、intent、灰态和安全边界；不接真实 Cast 后端，不切默认入口。
- 允许修改：`cast_entry_surface.{h,cc}` 的 host 接口、对应真实 CEF Probe、CEF CMake/source contract 与本计划。禁止修改 Cast 选择/会话 owner、MHV2/SDK/Relay、网页媒体资格、覆盖层协议和默认 app；不得为了测试复制投屏业务状态机。
- owner 与生命周期：调用方创建并持有自绘 toolbar panel，surface 只添加/移除自己的入口与 picker/overlay views；重复 attach/detach 幂等，Window/BrowserView/toolbar 任一失效即 fail closed。不得再调用 `GetChromeToolbar`、LOCATION suspend/restore 或 Chrome command；无 compatible media/device/backend 快照时按钮保持灰态且不产生 commit。
- 预算与验收：生产变更限定一个窄 host seam，不新增依赖。无 CEF contract 加 Windows x64 Debug/Release Alloy runtime Probe，覆盖宽/窄布局、初始灰态、选择 intent、重复提交拒绝、导航清空、关闭释放和无 Chrome LOCATION 源码契约；关联 CTest、build、diff/guard。明确不做：不实现 21W 的真实多视频/设备选择后端，不证明 Direct/Relay 或接收端播放；页面上方 overlay 的 Alloy 可交互性由 22W 独立拥有，不能用本任务入口面板证据替代。

## 33. PLT-SHELL-20 完成记录

- 状态：VERIFIED。`CastEntrySurface::Attach` 改为接收调用方持有的 Alloy toolbar panel，入口只作为该 panel 的 child 添加/移除；移除 `ChromeLocationBar`、`GetChromeToolbar`、LOCATION suspend/restore 及 Chrome runtime 前提。Attach 校验 Window/BrowserView/toolbar 均有效且属于同一 Window；重复 attach 拒绝、重复 detach 幂等，关闭先撤销 picker/accelerator/overlay/sink 再移除入口。
- 真实 CEF：Windows Alloy Probe 在自绘横向 toolbar 中验证右侧布局、初始灰态、两视频/一设备选择、prepare/commit 单次语义、cancel、窄宽重排、导航清空和 child 释放。调试期间真实复现 windowed Alloy 页面 child HWND 覆盖 Views overlay 的输入命中失败，因此在 22W Browser-owned 覆盖层完成前 Windows `SetVideoAnchors` 明确 fail closed，不绘制不可交互按钮；该证据不冒充 22W 通过。Windows x64 Debug/Release `crayon_browser` 与 integration build PASS；两配置 `cast_selection|alloy_cast_entry_surface_windows|windows_cef_shell_source_contract` 各 3/3 PASS（1.99s/1.60s）；`git diff --check` PASS；repo-guard PASS，RG-003/004 仅既有全仓 warning。
- Code Review：按 v0.9 检查 toolbar/window 同源、选择 owner、异步 intent 重入、灰态、导航/关闭逆序释放和缺失后端 fail closed。审查中关闭 Windows integration 缺失 localization 显式依赖、入口 detach 未直接移除 toolbar child、以及不可点击 Alloy overlay 仍展示三项；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖：真实 Cast 后端和 Direct/Relay（21W/26W）、Browser-owned 页面覆盖层（22W）、默认产品入口；macOS 旧 Chrome Probe 行为未由本轮 Windows 证据替代。

## 34. PLT-SHELL-17W 原子范围

- 状态：IN_PROGRESS；依赖 15W VERIFIED。单一目标：将 Windows Alloy 候选 host 的主菜单、CEF 上下文菜单、单文件拖放、受控剪贴板命令和本地 Markdown 文件入口接到既有共享 guard/MDV owner，消除 `IDC_OPEN_FILE` 作为自有入口的必要条件；不实现 MDV 内容 host（18W）或网页 Markdown（19W）。
- 允许修改：`cef_mdv_entries` 的 runtime-neutral 打开命令 seam、一个 `alloy_interactions` adapter、对应 contract/真实 CEF fixture、CEF CMake/source contract、本计划；共享 context-menu 只允许必要闭集兼容增量。禁止任意文件系统/目录选择、页面触发本地打开、任意 JS/CDP、绕过 CEF permission、默认产品切换和依赖升级。
- owner 与边界：主菜单只发闭集 native command；Open 必须由真实用户命令启动受控 `.md` 单选对话框，取消无副作用；拖放只接受单一 `.md` 并继续走 `MdvEntryController` gate；上下文菜单根据 CEF 可信参数和闭集 scheme 最小化；复制写入有界且来源为 user command，粘贴仍服从 CEF 页面权限。导航/关闭撤销临时菜单目标与 callback，不把原始路径写入日志/Recipe。
- 预算与验收：新增一个小型 client/surface adapter 和一个真实 Alloy Probe，production 净新增低于 700 行。Windows x64 Debug/Release build；真实菜单按钮/快捷键/上下文命令、单/多文件与非 Markdown drag 拒绝、剪贴板边界、open dialog 启动/取消生命周期、About/许可可达；context-menu/MDV/adapter/source/full CTest、diff/guard。原生文件对话框不得由自动化替用户选任意文件；可用独立 dialog callback/harness 验证取消与返回路径 fencing。

## 35. PLT-SHELL-17W 完成记录

- 状态：VERIFIED。新增 `AlloyInteractions`，由候选 Alloy window 的自绘 toolbar 持有真实 CEF `MenuButton/MenuModel`，只分发打开 Markdown、复制、粘贴、About 和许可闭集命令；Ctrl+O/C/V 在非法组合键时拒绝。打开文件复用 `MdvEntryController::HandleOpenFileCommand` 的受控单文件 `.md` 对话框，不再要求 Chrome `IDC_OPEN_FILE`；导航与 Shutdown 调用 `CancelTransientEntries`，撤销 context-menu 临时路径及所有 callback。拖放只接受单个 `.md`，上下文菜单由真实 CEF 参数增补并路由，About/许可只到编译期品牌 URL 与 `chrome://credits/`。
- 真实 CEF 与自动化：Windows x64 Debug/Release `crayon_browser`、integration target 均 PASS。两配置运行 `localization_generated_check|browser_localization_contract|context_menu_contract|mdv_entry_guard|mdv_handler_contract|alloy_interactions_windows|windows_cef_shell_source_contract` 各 7/7 PASS（2.44 s/2.45 s）；真实 Alloy window 打开原生菜单并关闭、验证三语言注入中的简体中文按钮、快捷键、真实页面右键回调、单/多文件与非 Markdown 拖放、命令目标、导航撤销、重复 Shutdown 和 child 释放。`node tools/locales/generate.mjs` 与 `--check` PASS，185 keys/9 files；`git diff --check` PASS。
- Code Review：按 v0.9 检查可信输入、闭集命令、路径权限、callback 重入、导航 generation、CEF UI 线程、关闭逆序和本地化完整性；修复回调重入可重新置活 context target 及 BrowserView 关闭时序。P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未覆盖：自动化没有替用户在 Windows 原生文件选择器中选择真实文件；只验证对话框启动/取消 seam 与返回路径 fencing。About 未访问公网内容，只验证受控目的地址；默认产品入口、内置页 host、网页 Markdown 分别归 24W、18W、19W。

## 36. PLT-SHELL-18W 原子范围

- 状态：IN_PROGRESS；依赖 17W VERIFIED。单一目标：把既有 `crayon://newtab` 和 `crayon://mdv` handler、runtime state 与编辑 owner 装入 Windows 候选 Alloy 内容 host，证明宿主迁移不改变内置页协议、文件 owner 或离线资产边界；不切产品默认入口，不重写 newtab/MDV/MRT 算法。
- 允许修改：一个窄 `alloy_builtin_content` 装配 adapter、Windows 真实 CEF Probe、CEF CMake/source contract 与本计划；仅在现有 handler 缺少 runtime-neutral seam 时做最小增量。禁止复制页面 HTML/JS、改变 `crayon` scheme 安全标志、引入公网资源、自动替用户选择本地文件、放宽原子保存/冲突 gate 或新增第二份文档状态。
- owner 与边界：scheme 仍由现有 `cef_new_tab_handler`/`cef_mdv_handler` 注册，MDV snapshot/path/generation 仍由 `MdvRuntimeState`、entry/editing controller 唯一持有；Alloy adapter 只负责候选 host 的创建、导航、视图挂载与生命周期接线。renderer/browser 进程必须使用同一自定义 scheme 契约；导航或关闭后旧编辑消息不得污染新 generation。
- 验收：Windows x64 Debug/Release product 与 integration build；真实 Alloy BrowserView 离线加载 newtab 和 MDV，验证 Source/Preview/Split、编辑/保存/外部修改冲突、Highlight/Mermaid/KaTeX 资产无公网与无 CSP/resource error、亮暗主题和关闭排空。复用适用 newtab/MDV/MRT/localization/source CTest，并执行 diff/guard；原生文件选择与接收端不在本项。

## 37. PLT-SHELL-18W 完成记录

- 状态：VERIFIED。新增窄 `AlloyBuiltinContent` CEF client seam，复用原 `crayon://newtab`/`crayon://mdv` scheme factories、`MdvRuntimeState`、entry/edit owner 与 browser-side `mdvQuery` router；导航、renderer 终止、close 和幂等 Shutdown 均先撤销旧 transient/query callback，未复制页面资源或文档状态。`MdvEditController::PushState` 补主 frame 必须为 `crayon://mdv/` 的来源门禁，避免导航前向 newtab 执行 `window.mdvPush`。
- 真实 CEF：Windows 11 x64 Debug/Release 专用 Alloy window 串行加载简体中文 newtab 与真实临时 `.md`，验证 Source/Preview/Split、恶意 `<script>` 保持文本、Highlight/KaTeX/Mermaid 完成标记、主题 CSS、页面编辑、原子回写、外部修改冲突确认及关闭排空；最终两配置 `alloy_builtin_content_windows` 分别 3.23s/2.42s PASS，复验为 2.93s/2.74s PASS，observer 未见 CSP/resource/公网请求错误。CEF 首次 profile 的 History/Favicons/QuotaManager mmap 诊断不参与通过判定。
- Mermaid 根因修复：严格 CSP 下拦截 render-id scoped `<style>` 在插入 DOM 前的写入并以 128KiB 上限捕获，通过既有闭合 CSS/SVG gate 后以 scheduler 字符串/字节预算缓存；首次与合并消费者只获得短生命周期 prepared DOM clone，缓存不持有 DOM；后续命中重新 gate，ID/fragment 只在已验证 DOM 上重绑定。SVG 与捕获 CSS 共用 4MiB candidate 上限，generation、并发、队列和 16MiB cache fencing 保持不变，不增加 `unsafe-inline` 或动态 hash。
- 自动验证：Debug/Release product 与 integration target 均构建成功；两配置适用 localization/newtab/MDV/MRT/source 矩阵各 17/17 PASS（Debug 52.28s，Release 7.90s）。最后增量复验 Node adapter 7/7、两配置 `markdown_runtime_mermaid|alloy_builtin_content_windows|windows_cef_shell_source_contract` 各 3/3 PASS；`node tools/mermaid/vendor.mjs --check` 为 104 files、3,522,090 bytes；`git diff --check` PASS。`clang-format` NOT_RUN：本机命令不可用。
- Code Review：按 v0.9 检查唯一 owner、CEF UI 线程、router/observer 重入、导航与 generation、原子保存、CSP/不可信 SVG/CSS、全局 style 捕获恢复、队列/缓存/DOM 生命周期、Release 测试隔离和 vendor 锁；修复对象缓存违反 scheduler 字符串契约、动态 style CSP、缓存 render-id、捕获 CSS 前置无界增长和 probe deadline 小于产品 deadline。P0/P1/P2/P3=`0/0/0/0`，APPROVE，最高 VERIFIED。
- 未覆盖与风险：原生文件选择仍归 17W 已记录人工缺口；本任务没有切换默认产品入口，也不代表网页 Markdown、投屏、Narrator/IME/原生 DPI 或发布包已通过，分别归 19W、21W+、23W/27W。

## 38. PLT-SHELL-19W 原子范围

- 状态：IN_PROGRESS；依赖 07W、17W、18W VERIFIED，且 CNT-17..20/W2 已 DONE。单一目标：把既有 Browser-issued PageSnapshot → Windows content-host → 确定性 Markdown → MDV Preview/Copy/Save As 链接入 Windows 候选 Alloy 内容 client；不重写 collector/gateway/Core/Markdown/MDV 算法，不切默认入口。
- 允许修改：一个窄 Alloy page-Markdown adapter、既有 preview controller 的 runtime-neutral 最小 seam、`AlloyBuiltinContent` 的受控 snapshot/lifecycle 转发、Windows 真实 CEF Probe、CEF CMake/source contract、本计划及必要 CNT addendum。禁止改变 PageSnapshot/CHV1 schema、内容预算、隐藏/跨源过滤、文件/剪贴板授权、页面写入能力、public network 或旧 Chrome 产品链。
- owner 与边界：Browser process 只对可信用户菜单命令和当前 Alloy browser/tab/navigation 签发 snapshot；`CefPageSnapshotBridge` 继续唯一拥有 request/source/sequence/terminal 校验，Windows content-host/Core 继续唯一拥有正文与 Markdown。导航、取消、renderer crash、tab close、host unhealthy 和 Shutdown 必须使旧 request/preview/export session 失效；页面消息不能触发复制、保存或扩大文件权限。
- 验收：Windows x64 Debug/Release product 与 integration build；真实 Alloy HTTP loopback 页面从 context menu 生成 MDV，覆盖当前标签、中英文结构、隐藏/敏感/child-frame 拒绝、导航取消、重复命令、Core 失败恢复、Preview/Source/Split、当前编辑缓冲区复制与受控 Save As seam、关闭零旧回调。复用 CNT/page snapshot/page Markdown/MDV/security/source CTest，执行 diff/guard，并按 v0.9 独立 Review；真实原生 Save As 用户点击只在已有自动化 seam 不足时列人工门禁。

## 39. PLT-SHELL-19W 完成记录

- 状态：VERIFIED。`CefPageMarkdownPreviewController` 新增只读取 tab facts、开始/取消 Browser-owned snapshot 的 runtime-neutral host seam，旧 Chrome `TabController` 构造继续兼容；新增 `AlloyPageMarkdown` 将当前 Alloy tab/navigation、`CefPageSnapshotBridge`、Windows content-host、既有 Markdown assembler 与 MDV edit/export owner 串成单一链路。Browser address/loading callback 只更新唯一 `TabModel`，新 generation 先通知 bridge；导航、renderer 终止、browser close、host unhealthy 与 Shutdown 均撤销旧 request/export session，Shutdown 在移除 observer 前发送 `OnSnapshotShutdown`。
- 真实 CEF：Windows 11 x64 本地 HTTP fixture 在真实 Alloy BrowserView 中两次发起 preview 命令，验证重复命令有界、首个 request 被新导航取消、恢复后经 renderer collector → Browser gateway → Rust content-host/Core 生成 Markdown并导航至 `crayon://mdv/app.html`；DOM 断言覆盖隐藏文本拒绝、GFM 表格、Preview/Source/Split、编辑缓冲区更新与复制，关闭后 content-host/TabController 排空。特定 probe 在真实 CEF 页面就绪后调用同一 `OnContextMenuCommand` 语义入口；17W 已独立证明真实右键事件进入 Alloy context-menu handler，本项未用自动化替用户点击原生 Save As 对话框。
- 验证：Windows x64 Debug/Release `crayon_browser` 与 `crayon_page_snapshot_cef_integration_win` build PASS。Debug `alloy_page_markdown_windows` 1/1 PASS（3.16s）；最终 Debug `windows_cef_shell_source_contract` 1/1 PASS（0.08s），同轮其余 19W 矩阵 3/4 PASS 后仅该旧令牌断言失败并已修正。Release `page_snapshot_cef_integration_windows|alloy_page_markdown_windows|windows_cef_shell_source_contract|page_markdown_preview` 4/4 PASS（47.45s，其中真实 Alloy 2.80s、完整 content fixture 44.53s）；Debug 同一 content fixture 64.47s、Alloy 3.22s、preview 0.03s 均 PASS。源码 guard 要求 `StartSnapshot/AdvanceNavigation/RendererGone/CloseBrowser/ShutDown` 且禁止 Chrome command、IDC、页面源码/Text 抽取。
- Code Review：按 v0.9 检查可信菜单入口、当前 tab/navigation、main-frame/HTTP(S) 与 URL 同一性、bridge sequence/terminal、回调重入、取消/关闭逆序、剪贴板/文件 owner、无任意 JS/CDP。审查中修复 Shutdown 先移除 observer 导致 content-host 收不到 shutdown、downstream 看不到已增补菜单、以及源码契约错误令牌三项；P0/P1/P2/P3=`0/0/0/0`，APPROVE，最高 VERIFIED。
- 未覆盖与风险：自动化未替用户在 Windows 原生 Save As 对话框内确认路径，沿用 CNT-20W1 已有 7,604-byte Save As/取消/覆盖 UI smoke 与受控保存 seam；本项不等于 `CNT-21W` 总 Review。真实网站公网、macOS、默认产品入口与投屏链未覆盖；下一 Alloy 任务 `21W` 依赖仍为 TODO 的 `PLT-CAST-R03b/R04/R07b/R08W`，不能绕过协议 owner 标成已开始。

## 40. PLT-SHELL-21W / PLT-CAST-R08W 原子范围

- 状态：VERIFIED；依赖 Windows `07W/15W/20`、`PLT-CAST-R03b/R04/R07` VERIFIED。单一目标：在 Windows 候选 Alloy window 中以一个窄 UI-thread controller 将既有 `MediaHostAdapter` 的 MHV2 播放器/草稿与 MHV1 设备/会话事件投影到 `CastEntrySurface`，完成多视频、设备、独立连接、prepare/显式 commit、原因和播控接线；不复制 Cast-SDK/runtime owner，不切默认产品入口。
- 允许修改：共享选择模型的最小闭集状态增量、一个 `alloy_cast_controller` adapter、Windows 候选宿主/真实 CEF Probe、MHV2 兼容字段、对应 unit/contract、CEF CMake/source contract、本计划与 Cast/current 协议文档。禁止 URL/Authorization/Cookie/SDK handle 进入 UI DTO，禁止自动选择或连接后自动播放，禁止网页消息触发 intent，禁止实现几何/overlay（22W/R09/R10W）、默认入口（24W）或 Cast-SDK 协议。
- owner 与预算：runtime 继续唯一拥有候选资格、草稿、路由与 session generation；`MediaHostAdapter` 继续拥有进程健康、请求关联、分页/事件 fencing；controller 只缓存至多 256 个脱敏播放器和设备投影并按当前 `CastViewContext` 重验证 intent。导航、Profile/tab/generation 变化、host unhealthy 与 Shutdown 必须撤销缓存及旧 draft；草稿 commit 必须从 MHV2 返回非零新 session generation 后才开放 MHV1 stop/pause/resume。预计新增 2 个生产文件、2 个测试/装配文件，净新增生产代码低于 800 行。
- 验收：Windows x64 Debug/Release product 与真实 CEF integration build；codec/host/adapter/controller/selection/surface/source CTest，真实 Alloy 入口覆盖两个同 URL 视频、不可选 EME、16+ 分页、设备刷新/投屏码、连接零 start、prepare/错误原因/commit 一次、session stop/pause/resume、旧 navigation/revision/session 和不兼容握手拒绝；720 DIP、100%/200% DPI、键盘与 Narrator 能完成则记录，不能自动化的原始人工门禁如实保留。执行完整适用 CTest、Rust fmt/clippy/test、diff/guard，并按 v0.9 独立 Review。明确不做 Direct/Relay 接收端播放（26W）、页面视频 overlay（22W）、默认产品切换和 macOS 真机。

## 41. PLT-SHELL-21W / PLT-CAST-R08W 完成记录（2026-09-05）

- 改动：新增 UI-thread-only `AlloyCastController`，只消费 `MediaHostAdapter` 的有界 MHV2 player/draft 与 MHV1 device/session 投影；同 URL 视频以 `(instance_id, source_revision)` 区分，HTTP 可见普通视频才可选，EME/无视频/不可见项 fail closed。设备 Connect 只发送独立连接，不调用旧 `StartCast`；Prepare/Commit 分离，只有 host 返回 `Committed + session_generation` 后才开放 pause/resume/stop。导航、关闭、旧 request/revision/session 与不兼容 capability 均撤销状态。`CastEntrySurface` 暴露稳定、互不冲突的原生 view ID 供 host 键盘/辅助功能路由；active session 的本地面板开关不发送新草稿或停止会话。
- 真实 CEF：Windows 11 x64 专用 Alloy window、720×720 DIP；Debug/Release `alloy_cast_bridge_windows` 均 PASS（2.65s/2.03s）。探针通过真实原生按钮的 focus + Space 完成入口→第二个同 URL 视频→设备→Connect→首次 Prepare timeout→简中“媒体检查超时”→重试→Commit→Pause→Stop；投影同时含第三个 EME 视频并验证 eligible=2/3，Connect 前后 `StartCast` 为 0，播控携带真实 committed generation 47。Release/Debug surface 专项提供 100% 基线，bridge 强制 `device-scale-factor=2` 提供 200% DPI；未用 DOM 自绘按钮替代原生 surface。
- 验证：Windows x64 Debug/Release 构建 `crayon_browser`、integration、codec、process、adapter、controller 目标 PASS。两配置适用矩阵 `localization_*|browser_localization_*|cast_selection|media_host_v2_codec|media_host_process_win|media_host_adapter_win|alloy_cast_controller_win|alloy_cast_entry_surface_windows|alloy_cast_bridge_windows|windows_cef_shell_source_contract` 各 14/14 PASS（7.55s/6.79s）。Release 首轮 13/14 暴露旧 `LocaleCatalog::Size()==180`，按生成 manifest 的 197 keys 修正并增加 timeout 三语言精确断言后双配置通过。Rust `fmt --check`、MHV2 contract 15/15、app-runtime 79/79、media-host 7/7、三 crate Clippy `-D warnings` 均 PASS；`git diff --check`、repo-guard PASS（RG-003/004 为既有全仓 warning）。
- Code Review：按 v0.9 逐项检查需求/边界、状态 owner、异步重入、导航与 shutdown、敏感数据、队列预算和证据。审查中关闭“Committed 后 picker 自动关闭导致播控不可达”、重复 Open 可能重启已准备草稿、缺少 committed session generation 无法安全播控、原生 ID 冲突和本地化数量漂移五项 P1；最终 P0/P1/P2/P3=`0/0/0/0`，APPROVE，最高 VERIFIED。
- 未覆盖与风险：自动化验证了原生 accessible name/focus 路由和纯键盘闭环，但未启动 Windows Narrator 朗读，记为 NOT_RUN；未使用真实手机/接收端，Direct/Relay 首帧与 100 次/睡眠长稳属于 26W；页面视频悬浮入口属于 22W，默认产品切换属于 24W。候选宿主使用真实 MediaHost codec/process/adapter 单测与真实 CEF surface 的 Fake transport 组合证据，不冒充 Cast-SDK 真机播放。

## 42. PLT-SHELL-22W / PLT-CAST-R10W 完成记录（2026-09-05）

- 状态：VERIFIED。Windows-only Browser-owned Win32 overlay 消费 R09 同事件绑定的 player ref/geometry 和 R08W controller snapshot；最多 16 个标准 BUTTON/tooltip，按 Alloy root/client 与 CSS viewport 比例适配 100%/200% DPI。页面只能影响位置；点击只传 opaque ref，controller 在当前 context/view revision 上重新校验并发起显式 Open→SelectMedia，不选择设备、不连接、不播放。
- 真实 CEF 与验证：离线 640×360 视频、200% scale 的 Alloy window 中，真实 renderer v4→proof/gateway supported geometry 显示原生简中按钮；失焦、501ms 过期、真实 DOM 遮挡、unsupported 与导航均隐藏，遮挡移除恢复；focus+Space 命中同一 player ref。Windows Debug/Release product/integration targets PASS；四项矩阵各 4/4 PASS（4.87s/4.45s）；`git diff --check`、repo-guard PASS（仅既有 RG003/004 warning），`clang-format` NOT_FOUND。
- Code Review：P0/P1/P2/P3=`0/0/0/0`，APPROVE。关闭 root==browser HWND 合法形态误拒绝、部分创建失败残留按钮、picker 期间误显示和销毁后计数残留。未启动 Narrator；完整三语言、IME、读屏、主题与 DPI 回归归 23W，默认产品仍未切换。

## 43. PLT-SHELL-23W 原子范围（2026-09-05）

- 状态：BLOCKED；依赖 Windows 09W、11W..19W、21W、22W VERIFIED。单一目标是在 Windows 11 x64 对候选 Alloy 全外壳执行 LOC/UX 综合回归并修复实际暴露的 Windows 缺陷；不切默认产品入口，不修改系统语言、DPI、安全/隐私设置，不用 process flag/mock tag 冒充真实系统状态。
- 输入：`docs/current/browser-ux.md`、`localization.md`，`LOC-07W` 的既有阻塞证据，Alloy 各功能 probe、三语言 catalog/product strings、design golden 与 Windows package。允许修改候选 Alloy surface 的本地化/布局/焦点/主题/IME/辅助功能缺陷、对应测试与计划；禁止增加第四语言、手动语言设置、改变快捷键/授权/route、放宽物理输入证明或偷跑 24W 默认切换。
- 验收：三种真实用户 UI 语言与一种不支持语言从 clean Profile 完整重启；Chromium/自有页面/原生控件、`html lang`、`navigator.language(s)`、Accept-Language 一致；每种语言覆盖 light/dark、720/960 DIP、100%/200% DPI、完整键盘焦点顺序、简繁中文/英文 IME 和 Narrator。Windows Debug/Release product build、完整 CTest、三语言 package/artifact scan、零公网/CSP/resource error、diff/guard，按 v0.9 Review。当前机器只有 `zh-Hans-CN` 用户 UI 语言和两项简中 IME、系统缩放 100%；其他真实系统矩阵不得由自动化替代，完成前最高 BLOCKED。
- 已执行证据：只读系统检查得到 Windows 11 x64 当前 `Get-WinUserLanguageList=zh-Hans-CN`，仅两项 `0804` 简中输入法，`Get-UICulture/Get-Culture=zh-CN`，`Win8DpiScaling=0` 且无独立 `LogPixels`（当前 100%）。通过 Windows 应用控制启动真实 Debug `CrayonBrowser.exe`，唯一窗口标题“蜡笔浏览器 - Chromium”；UI Automation 树读出简中最小化/最大化/关闭、返回/前进/重新加载、网站信息、地址和搜索栏、书签/Profile/菜单、新标签页及 `crayon://newtab` 的简中标题/说明/固定入口。Alt+F4 后同一 app target 窗口为 0。该证据只证明当前简中基线且仍为旧默认宿主，不冒充候选 Alloy 综合、IME composition、Narrator 朗读或其他系统矩阵。
- 自动化收口（2026-09-05，当前未提交 Alloy 工作树）：三语言 generator `--check` 为 3 locales/197 keys/9 files，正负向测试 6/6 PASS；Windows x64 Debug/Release `ALL_BUILD` 均退出 0。先排除故意拒绝 injected input 的 `cast_cef_integration_windows`，Debug/Release 连续完整自动集合各 124/124 PASS（339.43s/313.04s），其中各含 23 项 Alloy、17 项真实集成和 2 项受控 DPI probe；随后用户真实物理点击门禁 Debug/Release 各 1/1 PASS（6.61s/4.52s），故两配置累计各 125/125。首轮 Debug 123/124 暴露 R09 新增 `media-geometry-win` 后 Darwin 测试期望未排除 Windows-only 场景；补 `assertNotIn`/错误平台拒绝回归后直接测试 4/4、CTest 1/1 及双配置全量均通过。受控 `--force-device-scale-factor=2` 只能证明候选布局换算，不能替代原生系统 200% DPI。
- Release 候选：按 `cef-runtime-files.txt` 固定闭包刷新 `target/localization-release-staging`，共 35 files/372,923,339 bytes/12 locale pak；`CrayonBrowser.exe` SHA-256=`EAB5D939293A666B210B8F5FAEC191324A017D6105485CFC45150863607BD367`，`CrayonBrowser.dll` SHA-256=`DB7EAAD658A8976D111B170982D8BD15E6B7705348E59E9CDE9BFD798F61545A`。NOTICE/SPDX/manifest 重新生成，显式 artifact scan PASS，RG-006/RG-009 PASS；`scripts/check.ps1 security`、`git diff --check` PASS。`scripts/check.ps1 fast` 首轮在 `cnt_20w1_real_process_health_markdown_and_shutdown` 以 Windows pipe error 233 失败，单项首次复现后连续 30 次及完整 `fast` 复跑 PASS；因不能稳定复现，不以复跑掩盖首轮，也未在无确定性测试时修改生产 IPC。
- 原始阻塞：本机未安装/启用 `en-US`、`zh-TW` 和一种不支持语言的用户 UI 语言，不能执行真实完整重启；系统 DPI 只有 100%，不能执行原生 200%；Windows 应用控制能读 accessibility tree 但不能审计扬声器实际 Narrator 朗读，也不能把 literal SendInput 当成 IME composition。安装语言包、切换用户 UI 语言、注销/重启、修改系统 DPI 均是系统级变更，未获用户逐步执行/授权且可能中断当前会话。P0/P1/P2/P3=`0/1/0/0`，REQUEST_CHANGES；P1 即规定的真实平台矩阵未闭合。24W 依赖 23W VERIFIED，故不得提前切默认入口。

## 44. PLT-SHELL-24W Windows 默认 Alloy 产品入口（2026-09-05）

- 用户决策：语言/IME/Narrator/原生 DPI 真机矩阵先放到后续，23W 继续保持 `BLOCKED` 且不得写成通过；该后置项不再阻塞 Windows 默认宿主工程切换。24W 不改变支持语言、系统设置或 LOC-07W/REL 的最终发布门禁，也不能用候选 probe 冒充默认产品。
- `24W1 VERIFIED`：建立 Windows production-only Alloy root/client/lifecycle，将 `BrowserApp` 首窗从 `TabController::CreateBrowserWindow` 的 `CEF_RUNTIME_STYLE_CHROME` 切到真实 `CefWindow`＋`CefBrowserView` Alloy；首个 `crayon://newtab`、关闭排空、Browser/Renderer 回调、图标与唯一 message-loop owner 必须真实通过。允许修改 Windows app、一个窄 product host、CMake/source contract 与独立真实 CEF 产品 smoke；禁止删除旧宿主、修改 Mac、Cast/MDV/CNT 算法、协议、授权或依赖。
- `24W2a VERIFIED`：把媒体 observation/network resource bridge、可信输入、MHV2 Cast controller、toolbar entry 与 Browser-owned overlay 接入同一 production host；所有候选、草稿、route、session 与 generation 仍由既有 gateway/adapter/controller 唯一持有，不复制 probe fake transport。
- `24W2b1 IMPLEMENTED`：把已 VERIFIED 的 permission/download handler、site controls、证书与外部协议安全边界接入 production Host；请求绑定当前 Browser/tab/navigation，默认拒绝，用户确认只经 Windows 原生 surface，不访问公网、不自动启动外部应用。自动化与通知权限真实产品负向验证已闭合；外部协议正向可信物理点击仍待用户完成，故不写成 VERIFIED。
- `24W2b2a VERIFIED`：把已 VERIFIED 的 popup policy 与多窗口 coordinator 接入 production Host；保持同 Profile/request-context、真实手势、opener/容量和关闭/focus 语义。
- `24W2b2b VERIFIED`：把 Profile/request-context factory 与无痕隔离接入 production Host；保持一窗口一 Profile、regular context 复用、每个无痕窗口 memory-only 且零持久化。
- `24W2b2c VERIFIED`：把 session checkpoint/restore 接入 production Host；只恢复 regular Profile 的有界窗口/tab/advanced metadata，损坏、跨 Profile 与部分失败显式拒绝。
- `24W2b3 TODO`：把 advanced tabs、bookmarks、history/recently-closed、download shelf 等其余日用 adapter 接入同一产品状态流与可见入口。24W2 原范围因 Cast、安全、多窗口/Profile 和日用 surface 合计超过单提交审查预算而拆为 a/b1/b2/b3，不改变验收或一期范围。
- `24W3 TODO`：双配置完整 build/CTest、默认入口与 artifact forbidden scan、真实 Windows 产品 UI/关闭恢复、性能/安全回归和 v0.9 独立 Review；只有 Chrome-style 默认创建不可达且 Alloy 三闭环无回退后，24W 才能 `VERIFIED`。旧生产接线与共享 Mac 隔离代码只在 25W 移除。

### 24W1 完成记录（2026-09-06）

- 状态与实现：`VERIFIED`。新增 `AlloyProductHostWin` 作为 Windows 默认产品唯一 Alloy root/client/lifecycle owner，首窗直接创建 `CefWindow`＋`CefBrowserView` 并打开 `crayon://newtab`；现有 `AlloyWindowCoordinator/AlloyTabController` 继续拥有窗口/标签状态，tab strip、omnibox/navigation、内置页、page Markdown、interactions/page tools 接入同一 Host。关闭活动/后台/最后标签均按 CEF DoClose/BeforeClose 逆序释放；最后窗口关闭前先 Shutdown 持有 BrowserView 的 chrome surface，避免悬挂或 `CefShutdown` 崩溃。旧 Chrome `TabController` 生产接线暂保留供 24W2/25W 迁移，不宣称已删除。
- 缺陷回归：真实 UI 暴露关闭活动标签后后继内容虽恢复但站点身份退回“未知站点”。先扩展 `alloy_navigation_windows`，旧实现稳定在 `rebind-loaded-tab` FAIL；随后由 `AlloyNavigation` 唯一持有最多 64 个 Browser 的地址/loading/back/forward/身份快照，切到另一 Browser 再切回时恢复可信状态，Shutdown 清空。增强探针同时覆盖已完成 HTTP 页和证书错误页跨标签重绑，Debug 回归转为 PASS，避免只从 HTTPS URL 推导并误报安全。`AlloyTabController` 另增加关闭活动标签后显式激活 model 选出的后继 view；产品 TLS 分类补齐 CEF `ERR_SSL_PROTOCOL_ERROR (-107)` 与 `-200..-299`。本地化新增导航身份和标签文案后，generator 固定数量旧值 197 首轮 5/6 FAIL，按三语言 manifest 的 206 keys 更新 Node/C++ 精确断言后 6/6 PASS，未放松 parity/hash 契约。
- 自动化：Windows 11 x64 VS 2022 同一多配置目录，Debug/Release `crayon_browser` 与 `crayon_page_snapshot_cef_integration_win` 均构建成功。最终 Debug/Release 完整 24W1 定向矩阵各 9/9 PASS（40.00s/40.39s），增强导航探针另有 Debug 1/1 PASS（11.08s）。`node --test tools/locales/generate.test.mjs` 6/6 PASS，`node tools/locales/generate.mjs --check` PASS（3 locales/206 keys/9 files）；Debug/Release `localization_generated_check|localization_generator_contract|browser_localization_contract` 各 3/3 PASS（2.08s/2.57s）。一次 Release build＋CTest 组合调用在 184.2s 外层 TIMEOUT，遗留 CTest 随后自然结束；拆分后取得明确 build/CTest 结果，不以超时调用冒充通过。`git diff --check` 与 repo-guard PASS；RG-003/004 仅既有全仓 warning，RG-006 artifact scan 归 24W3。
- 真实产品：Windows 应用控制启动刚重编译的 Debug `CrayonBrowser.exe`，UI Automation 树确认标题“蜡笔 AI Agent 投屏浏览器”、真实简中 Alloy tab strip/导航/地址栏/menu 与 `crayon://newtab` 内容。点击新建得到两个标签，关闭活动第二标签后首标签内容、`crayon://newtab/`、“本地页面”和刷新状态均恢复；Alt+F4 后只读检查 `CrayonBrowser process count: 0`。此前开发中真实复现并修复后台标签无法关闭、最后窗口悬挂及 CEF shutdown crash；最终未再复现。
- Code Review 与边界：按 v0.9 检查默认入口真实性、唯一 owner、异步创建/关闭、后继激活、BrowserView 引用释放、TLS 身份、snapshot generation、敏感数据与 Release 隔离；范围内 P0/P1/P2/P3=`0/0/0/0`，APPROVE。24W2 尚需把 Cast controller/entry/overlay、窗口 popup、profile/security 等剩余已验证 surface 接入这个真实 product host；24W3 完整 125 项双配置 CTest、artifact/性能/安全/三闭环总回归尚未执行。23W 的真实系统语言/IME/Narrator/原生 200% DPI 仍按用户决策后置并保持 `BLOCKED`，不由本项替代。

### 24W2a 完成记录（2026-09-06）

- 状态与实现：`VERIFIED`。`AlloyProductHostWin` 直接拥有 `CefObservationBridge`，转发 renderer media IPC 与 CEF network resource facts，并把可信物理输入绑定当前 Alloy Browser；BrowserApp 的媒体消费已切离旧 Chrome `TabController`。Host 使用既有 `MediaHostAdapter`、`AlloyCastController`、`CastEntrySurface` 与 `AlloyCastOverlayWin`，按 active tab/navigation/generation 绑定 MHV2 投影，最多保存 16 个、500ms 到期的几何提示；导航、切换、关闭和 shutdown 均先撤销 surface/context。MediaHost 启动竞态使用固定 500ms monotonic retry，无忙等和高频日志。
- 缺陷回归：新增 source contract 首轮稳定失败于 product Host 缺少 `CefObservationBridge`。真实产品随后复现快速内置页在 Browser 创建完成前已结束 loading，首代 navigation 仍为 0，导致 Cast surface 原子绑定失败并连带回滚菜单；修为 Browser 创建完成时显式建立首代并同步实际 loading。再次复现 `CastEntrySurface` 收到空 clock 后按契约拒绝 Attach，改为共享 steady monotonic clock。另新增 `failed_context_bind_can_retry`：旧 `AlloyCastController` 在 transport 首次拒绝后错误保留 current context，第二次绑定假成功但 projection 仍 incompatible；修为未 admitted 时清除 context，允许有界重试。
- 自动化：Windows 11 x64 VS 2022，Debug `crayon_browser` build PASS；Release `crayon_browser`、`crayon_alloy_cast_controller_win_test`、`crayon_page_snapshot_cef_integration_win` build PASS。Debug/Release `alloy_cast_controller_win|alloy_cast_entry_surface_windows|alloy_cast_bridge_windows|alloy_cast_overlay_win|alloy_cast_overlay_windows|media_geometry_windows|windows_cef_shell_source_contract` 各 7/7 PASS（20.70s/15.90s）；source contract 修复前为 0/1、修复后 PASS。新增 controller 回归修复前 0/1、修复后 1/1 PASS。`git diff --check` PASS。
- 真实产品：启动刚重建 Debug `CrayonBrowser.exe`，UI Automation 确认真实 Alloy 简中 `crayon://newtab` 同时显示菜单与灰态“投屏”原生按钮；点击新建标签后第二标签仍显示同一 Cast entry，关闭活动标签后首标签、本地页面身份、菜单和 Cast entry 均恢复；Alt+F4 后 `CrayonBrowser process count: 0`。本次未用公网、未把灰态入口冒充真实接收端播放；真实 Direct/Relay 证据仍沿用 PLT-W05/REL 所有者并由 24W3 聚合。
- Code Review 与边界：按 v0.9 检查 generation/active-tab fencing、MediaHost 唯一 owner、失败重试、几何容量/TTL、surface detach、关闭逆序、URL/凭证数据流与旧 Chrome 误消费；P0/P1/P2/P3=`0/0/0/0`，APPROVE。24W2b 的 popup/profile/security/其余日用 surface 尚未完成，24W3 全量/性能/安全/artifact 总回归未运行；语言/IME/Narrator/原生 DPI 继续按用户决策后置。

### 24W2b1 完成记录（2026-09-06）

- 状态与实现：`IMPLEMENTED`。Windows 默认 `AlloyProductHostWin` 直接实现 CEF permission/download/request handler，把 camera、microphone、notification、geolocation、clipboard 与 download 统一绑定当前 tab/navigation/origin 的 `AlloySiteControls`；组合 camera+microphone 和多 bit 请求一次原生确认后逐项落账，未知 bit 默认拒绝。证书错误只允许本次继续或返回，下载允许后仍交给既有 `CefDownloadHandlerAdapter`。外部协议的 IO 回调始终设置 `allow_os_execution=false`，仅 `mailto/tel/sms` 可进入 UI；first-party/referrer origin 必须匹配 Browser 进程保存的来源，并要求 2 秒内同 tab、同 generation 的一次性可信物理输入，再由 Windows 原生确认决定是否调用系统 handler。
- 缺陷与 Review 修正：首轮 Debug 构建稳定暴露 `CefString/std::string` 条件表达式歧义，改为显式 `ToString()` 后通过。实现复核修正原先只接受单独 camera 或 microphone、会拒绝常见组合请求的问题。真实产品又复现外部协议进入 IO 回调时 main-frame 已显示 `mailto:`，若从当前 frame 反查 origin 会丢失可信来源；改为 IO 捕获 first-party/referrer、UI 对照 per-tab origin/URL，并在拒绝后恢复来源页。独立 Review 另关闭证书流程替换 SiteControls 前未 Shutdown、可能不决议旧 pending callback 的生命周期问题。
- 自动化：Windows 11 x64、固定 CEF 150、VS 2022；Debug `cmake --build .cache/build/windows-cef-debug --config Debug --target crayon_browser --parallel 1` PASS，Release 同命令 PASS。Release 两次完整链接外层 60s 超时均如实记为 TIMEOUT/124，随后增量复跑分别 22.6s/30.3s 明确退出 0。最终 Debug/Release 的 `download_domain_contract|download_shelf_contract|alloy_downloads_contract|alloy_site_controls_contract|alloy_security_windows|windows_cef_shell_source_contract|permission_store_test|permission_contract` 各 8/8 PASS（9.42s/3.62s）。三语言 generator `--check` PASS（3 locales/214 keys/9 files），Node 正负向 6/6 PASS；`git diff --check`、repo-guard PASS，RG-003/004 仍为既有全仓 warning，`clang-format` NOT_FOUND。
- 真实产品：localhost fixture 在默认 Debug Alloy 产品中真实触发简中“网站权限”原生 MessageBox，默认焦点为“否”，选择拒绝后页面精确返回 `denied`。自动化点击外部协议链接未被 trusted-input monitor 接纳，产品未弹确认、未调用邮件客户端，证明 injected input 负向 fail closed；正向可信物理点击＋“否”已把窗口准备给用户，但当前尚未收到完成回执，不冒充通过。证书错误与外部协议状态机由真实 CEF `alloy_security_windows` 覆盖，不能替代默认产品正向人工门禁。
- Code Review：按 v0.9 检查 origin/generation owner、CEF UI/IO 跨线程投递、组合权限、pending callback、trusted input TTL/单次消费、外部 scheme/target 上限、默认拒绝、Shutdown 和敏感数据；范围内代码 P0/P1/P2/P3=`0/0/0/0`，APPROVE。最高仍为 IMPLEMENTED；待真实物理点击出现“打开外部应用”并选择“否”后才可补为 VERIFIED。24W2b2/b3 与 24W3 尚未开始，23W 的系统语言/IME/Narrator/原生 DPI 仍按用户决策后置。

### 24W2b2a 完成记录（2026-09-06）

- 状态：`VERIFIED`；依赖 08W、14W、24W1 VERIFIED。Windows 默认产品 Host 现由 `OnBeforePopup` 把已拥有 Browser、主 frame target 与 CEF user gesture 交给 `AlloyWindowCoordinator`，只为获准请求创建真实顶层 `CefWindow`＋独立 `AlloyTabController/CefBrowserView`；主 Browser 创建后捕获其实际 request context，popup 复用同一引用，不复制 Cookie/权限或重建 Profile。所有 Browser 创建、地址/loading、DoClose/BeforeClose、renderer crash 与 view release 回调按所属 window/controller 路由；媒体、site-controls、下载与产品 chrome 仍只属于 primary。
- 输入与允许路径：固定 CEF 150 `OnBeforePopup`/Views lifecycle，`AlloyWindowCoordinator`、`AlloyTabController`、`ProfileContextFactory` 的只读/窄兼容 API；允许修改 `alloy_product_host_win.*`、Windows app 的 context 注入、相关 source contract/真实 CEF fixture、本计划。若真实复现证明 coordinator 缺口，只允许对其做向后兼容最小修复并先补回归。
- owner/边界：coordinator 继续唯一拥有 window id、opener、focus recency、全局 8 窗口/每 opener 4 popup 容量；每窗口 controller 唯一拥有自己的 BrowserView/browser。只接受 Browser-process 回调中的已拥有 source browser、主 frame、HTTP(S) bounded target 与 CEF `user_gesture=true`；程序 popup、未知/关闭中 opener、credentials/control/超长 URL、重复/迟到创建默认拒绝。popup 继承 opener 的 regular Profile/request-context，不复制 Cookie/权限或生成新 Profile。
- 缺陷回归：首次产品装配在 `BrowserApp` 构造期调用 `GetGlobalContext()`，真实启动稳定触发 CEF `DCHECK failed: context not valid`；移动到独立 context 又使全局注册的 `crayon://newtab` 变为 `ERR_UNKNOWN_URL_SCHEME`。最终不预取或重建 context，而在 primary Browser 创建后通过 `GetRequestContext()` 捕获 CEF 实际 global context，既保持内置页又为 popup 提供相同 identity。关闭矩阵随后暴露主窗口 X 被当成全应用退出、连带关闭 popup；拆分 `primary_closing_` 与全局 `closing_`，用户关闭 opener 只排空 primary，显式 Host `Close()` 才关闭全部窗口。
- 自动验证：Windows 11 x64、固定 CEF 150、VS 2022；Debug/Release `cmake --build .cache/build/windows-cef-debug --config <Debug|Release> --target crayon_browser --parallel 1` 均 PASS。最终两配置 `browser_engine_forbidden_api_scan|alloy_tab_controller_windows|alloy_profile_context_windows|alloy_window_coordinator_windows|windows_cef_shell_source_contract|window_state_model_test|profile_context_contract` 各 7/7 PASS（18.43s/23.60s）。`git diff --check` 与 repo-guard PASS；RG-003/004 为既有全仓 warning，artifact scan 归 24W3；`clang-format` 仍因本机命令不可用 NOT_RUN，新增 C++ 按仓库格式人工复核。
- 真实产品：启动刚构建 Debug 默认产品，先确认 `crayon://newtab` 正常。localhost fixture 页面 load 时请求 programmatic `window.open` 后仍只有一个窗口；Windows 应用控制点击 HTTP 链接后出现第二个 720×560 顶层 Alloy window，子页显示 `popup-ready context-shared`，证明同 origin Cookie 经同 request context 可见。关闭 primary 后 popup 独立存活并取得焦点；关闭最后 popup 后只读检查 `CrayonBrowser process count: 0`。每 opener/全局容量、未知 owner、非法 URL、创建回滚与重复 Shutdown 由同轮真实 CEF coordinator probe 覆盖，不把 source contract 冒充交互证据。
- Code Review：按 v0.9 顺序检查需求/边界、owner 路由、同步 BrowserView mount、关闭/崩溃逆序、popup URL/gesture fail-closed、context 数据流、容量、测试与可维护性；关闭 context 初始化时序与 opener 连带退出两项缺陷后，P0/P1/P2/P3=`0/0/0/0`，APPROVE，最高 `VERIFIED`。
- 明确不做：不实现 Profile picker/guest/incognito（b2b）、session checkpoint/restore（b2c）、高级 tabs/bookmarks/history/shelf UI（b3）、语言系统矩阵、性能长稳、artifact 或 macOS；不以新 tab 冒充 popup window，不以重建 Browser 冒充同 context 继承。

### 24W2b2b 原子范围（2026-09-06）

- 状态：`VERIFIED`；依赖 14W、24W1、24W2b2a VERIFIED。单一目标：让 Windows 默认 Alloy 产品由 `ProfileContextFactory` 唯一登记并复用 default regular context，并从用户可见命令创建独立 memory-only 无痕顶层窗口；每个无痕窗口只继承来源 regular Profile 的逻辑 scope，不继承 Cookie/cache/session 持久化。
- 输入与允许路径：固定 CEF 150 `CefSettings.root_cache_path/cache_path`、`CefRequestContextHandler::OnRequestContextInitialized`、per-context scheme factory；14W 的 `ProfileContextFactory`、`AlloyProfileSettings` 与无痕 new-tab 模型。允许修改 Windows bootstrap/app/product host、context factory、Alloy main menu 的窄 typed command、内置 scheme 注册的向后兼容 overload、相关测试/CMake/source contract、本计划。禁止 session restore（b2c）、bookmarks/history/download shelf（b3）、Cast/CNT/MDV 算法、协议、依赖和 macOS。
- owner 与边界：Windows bootstrap 只解析并创建 `%LOCALAPPDATA%\CrayonBrowser\CEF` 固定产品 cache root。CEF Chrome runtime 已唯一拥有 `root_cache_path/Default` global context，因此 factory 只在 UI 线程核对并登记这个 context，逻辑 `default` ID 仅驻内存；后续额外 regular Profile 才继续使用既有 opaque hash 子目录，不能为 default 重建第二个同路径 context。每次无痕请求新建 distinct empty-cache context，等待初始化成功并为该 context 注册私有 `crayon://newtab` factory 后才建窗；失败显式拒绝并释放，不回退 global context。共享 MDV runtime 会泄露 regular Profile 的内存文档，故本项不向无痕 context 注册 `crayon://mdv`，后续若支持必须先有 per-context 文档 owner。无痕窗口与其可信 popup 可共享该临时 context，但不同无痕请求不能共享；不进入 session、preferences、bookmarks、history 或权限持久化。
- 验收：Windows x64 Debug/Release product build；profile context/settings、new-tab/MDV scheme、window coordinator、source/forbidden 与完整适用 CTest。真实默认产品验证 regular 重启 Cookie 持久化、同 regular popup 共享、两个无痕窗口互相隔离且 `GetCachePath()` 为空、关闭重开无残留、`crayon://newtab` 显示无痕模型、主/无痕独立关闭与退出零残留；记录失败路径、diff/guard，并按 v0.9 独立 Review。
- 明确不做：本项不新增账号/同步、任意 Profile 创建或迁移 UI，不把 guest 冒充无痕，不实现 session checkpoint、历史/书签/下载持久化 surface，不执行后置的三系统语言/IME/Narrator/原生 200% DPI 或 macOS 门禁。

### 24W2b2b 完成记录（2026-09-06）

- 状态与实现：`VERIFIED`。Windows bootstrap 固定创建 `%LOCALAPPDATA%\CrayonBrowser\CEF` 并设置 `root_cache_path`/`persist_session_cookies`；`BrowserApp` 在 CEF context 初始化后的下一 UI task 获取 global context，`ProfileContextFactory::AdoptGlobalContext` 核对 global identity 与实际 `Default` cache path 后登记逻辑 default，内置 new-tab/MDV factory 改为可按 request context 注册。产品 Host 在 Start 前强制同一 persistent context，并由主菜单或 Ctrl+Shift+N 经既有 `AlloyProfileSettings` 创建每窗独立 empty-cache 临时 context；无痕 popup 继承 opener context，regular popup 继承 global context，关闭与 CEF shutdown 前按逆序释放全部引用。无痕仅注册私有 new-tab，不复用 regular MDV runtime，避免内存文档跨 profile 泄露。
- 缺陷与修复：最初为 default 创建 custom persistent context 后，真实产品约 8 秒稳定在 `CefRunMessageLoop` 崩溃；WER 为 `0x80000003`、`libcef` offset `0x9a17c8a`，cdb 捕获 `profile_attributes_storage.cc:1052 DCHECK failed: !profile_attributes_entries_.contains(path.value())`。共同根因是 Chrome runtime 已拥有 `root_cache_path/Default`，二次 context 重复登记同一 profile；改为 adoption 后启动、重启与退出稳定。regular 重启 Cookie 首轮因 session cookie 未持久化显示 `context-isolated`，按 CEF setting 补 `persist_session_cookies=1` 后显示 `context-shared`。Release 全量首轮 124/125（585.44s）及中间复验 124/125（604.32s）均只在 `alloy_interactions_windows` 超时；诊断证明 windowed CEF 的合成右键依赖前台桌面，改用测试 target 内合法 `CefContextMenuParams`/`CefMenuModelDelegate` 直接验证公开 handler 合同，不修改生产 API，最终 Debug/Release 连续各 20/20 PASS（66.80s/56.77s）。独立 Review 发现无痕 context 初始化 handler 捕获裸 `this`，若 Host 提前销毁会形成悬挂回调；改为 `CefRefPtr<AlloyProductHostWin>` 保活并补 source contract。Review 后 Debug 全量又在 `alloy_builtin_content_windows` 捕获后端 conflict state 已为 `dirty=1/confirm=1/save_ok=0`、但 DOM confirm 偶发未更新；根因是 MDV `PushState` 只有一次无确认脚本投递。修为 generation fencing、weak owner、50ms 间隔且最多三次补投，无高频日志和无界任务；test-only probe 增加总 watchdog 与有界 DOM 查询，修复后 Debug/Release 各连续 10/10 PASS（61.41s/43.49s）。
- 自动化：Windows 11 x64、固定 CEF 150、VS 2022。最终生产修复后 Debug/Release 构建 `crayon_browser crayon_page_snapshot_cef_integration_win` PASS（78.4s/127.4s）。修复前 Debug 完整 125/125 PASS（676.92s）；最终快照 Debug 完整命令实际 122/125 PASS（603.47s），仅三个依赖交互桌面前台的 `alloy_tab_controller_windows|alloy_tab_strip_windows|alloy_omnibox_windows` 在 `SetForegroundWindow`/真实输入阶段失败，同一构建产物、不改代码立即定向复跑 3/3 PASS（18.72s），按前台激活偶发干扰记录，不把首次全量冒充通过。最终快照 Release 完整同命令 125/125 PASS（623.49s），包含 23 项 Alloy、profile/source/forbidden/new-tab/MDV/window contracts 与真实 CEF context probe。最终 Debug/Release 核心 7 项矩阵严格串行各 7/7 PASS（19.99s/16.76s）；一次错误地并行运行双配置时 Debug `alloy_interactions_windows` 因两套 GUI 争抢唯一前台而超时，该编排结果无效且未用于判定。`git diff --check` 与 `cargo run --quiet -p repo-guard -- scan --root .` PASS；RG-003 包含本轮审查过的 2179 行 product Host 与其他既有阈值 warning，仍低于 3000 行强制门槛且保持单一窗口编排 owner，本项不为机械降行拆散生命周期；RG-004 为既有全仓 warning，RG-006 artifact N/A。`clang-format` NOT_FOUND，新增 C++ 按仓库格式人工复核。
- 真实产品：Debug localhost fixture 中 regular setter→正常退出→重启 child 显示 `popup-ready context-shared`；两个独立无痕窗口 child 均显示 `context-isolated`，无痕一的可信 popup 显示 `context-shared`，证明同 opener 共享、跨无痕隔离；私有 new-tab/无痕标题与 toolbar 可见，关闭后无新增无痕磁盘目录。Release 最终启动仅显示“蜡笔 AI Agent 投屏浏览器” Alloy 主窗与 `crayon://newtab`，Alt+F4 后窗口/进程归零。针对 Review 生命周期项，真实 Release 中 Ctrl+Shift+N 后立即关闭主窗，两个已完成创建的无痕窗口仍正常存活，逐一 Alt+F4 后窗口与 `CrayonBrowser` 进程均为 0，未出现悬挂回调崩溃。首次使用既有脏 Default profile 启动曾同时恢复一个旧 Chrome-style 窗口，正常关闭后二次启动未复现；不在本项越界删除用户 session，必须由 24W2b2c 明确迁移/拒绝 legacy Chrome session。
- Code Review：按 v0.9 检查 CEF UI-thread 时序、global context 唯一 owner、cache path/逻辑 ID、临时 context 容量与 generation、异步 handler 保活、popup 继承、scheme factory 数据隔离、MDV 状态投递的有界重试、关闭逆序、Cookie/权限隐私、测试确定性和 Release 边界；首轮发现并关闭上述裸 `this` P1，最终 P0/P1/P2/P3=`0/0/0/0`，APPROVE。剩余风险为 legacy Chrome session 一次性恢复，已明确归 24W2b2c；Debug 完整套件的三项前台激活偶发性留存原始失败与定向通过证据；无痕 MDV 在拥有独立 runtime 前 fail closed，不冒充已支持。24W2b2c、24W2b3、24W3 仍未完成，后置语言/IME/Narrator/原生 DPI 与 macOS 门禁不由本项替代。

### 24W2b2c 原子范围（2026-09-06）

- 状态：`IN_PROGRESS`；依赖 10W、24W1、24W2b2a、24W2b2b VERIFIED。单一目标：把 10W 已验证的 session v2 codec、Windows 原子文件 adapter 与 coordinator restore 接到默认 Alloy 产品启动/稳定状态/正常退出生命周期；只恢复 default regular Profile，不恢复无痕或页面内存。
- 输入与允许路径：`browser/session` 与 `alloy_session_restore` 的既有闭合契约；允许修改 Windows bootstrap/app/product Host、coordinator 的窄只读/回滚补强、真实 CEF product fixture/source contract、本计划与索引。session 文件固定在产品 cache root 下的独立有界文件，不复用 Chromium `Default/Sessions` 私有格式；旧 Chrome session 只隔离/拒绝，不猜测解析、不删除 Cookie/History/Preferences。
- owner 与失败语义：session 模块继续唯一拥有 schema/URL/Profile/容量校验，coordinator 唯一拥有窗口/controller 恢复，product Host 只编排真实 request context、BrowserView、窗口与 checkpoint 时机。启动只接受匹配当前 Profile 且含唯一 primary 的合法快照；not-found 进入新标签，corrupt/profile-mismatch/超量显式记录并保留原文件。恢复先全量预检；部分真实窗口/tab 创建失败必须关闭已创建对象、保留 checkpoint 供下次诊断，不以半恢复状态继续。无痕窗口及其 popup 永不进入 snapshot。
- 验收：Windows x64 Debug/Release product 与 integration build；session/file/source/forbidden、coordinator 真实 CEF 与产品启动恢复探针，覆盖正常重启、崩溃 checkpoint、v1 降级、两窗口多标签 active/pin/mute/group、损坏/跨 Profile/重复/超量/部分失败、无痕零持久化与重复恢复幂等；完整适用 CTest、diff/guard。真实产品验证 regular 多标签关闭重启恢复、无痕关闭重启不恢复、旧 Chrome session 不生成第二套默认窗口、退出零残留，按 v0.9 独立 Review。
- 明确不做：不恢复表单、页面 JS/滚动内存、Cookie/Authorization、权限 grant、下载任务、Cast session 或 trusted-input proof；不修改 schema/依赖/macOS，不实现 bookmarks/history/recently-closed/download shelf UI（b3），不执行后置语言/IME/Narrator/原生 DPI 门禁。

### 24W2b2c 完成记录（2026-09-06）

- 状态与实现：`VERIFIED`。Windows bootstrap 将默认 Alloy 数据根隔离到 `%LOCALAPPDATA%\CrayonBrowser\CEF-Alloy-v1`，session v2 checkpoint 固定为根下 `alloy-session-v2`；旧 `%LOCALAPPDATA%\CrayonBrowser\CEF` 及其中 Cookie/History/Preferences 原样保留，不读取 Chromium 私有 session，也不再触发 Chrome-style startup window。`BrowserApp` 把 session path 注入唯一 product Host；Host 只接受当前 default Profile、唯一 primary、regular-only 且通过既有 schema/URL/容量校验的快照，先由 coordinator 全量恢复 model，再延迟 250ms 创建 BrowserView，待所有 Browser ready 后一次性激活各窗 active tab。tab/url/pin/mute/group 继续由 `AlloyTabController`/advanced model 唯一持有；稳定状态用单个有界 debounce task 写原子 checkpoint，正常关闭前同步保存，无痕及页面内存不入盘。损坏、跨 Profile、超量或部分物化失败关闭已登记及迟到 Browser/Window、禁写并保留原 checkpoint，不带半恢复状态继续。
- 缺陷与修复：既有脏 CEF root 首次真实启动会额外恢复 Chrome-style 窗口，改为版本化 Alloy root 并加 `no-startup-window` 后隔离。首轮真实恢复在 `AlloyWindowCoordinator::RestoreSession` 回调内同步 `CefBrowserView::CreateBrowserView`，CEF 稳定触发 `observer_list.h:330 Check failed observers_.empty()`；改为事务返回后延迟物化。随后 active tab 在 Browser 尚未 ready 时激活失败，改为全部 `OnBrowserCreated` 完成后统一激活。双标签正常关闭又暴露同时 `TryCloseBrowser` 会令活动后继也处于 closing、窗口不收敛；非 force 路径改为逐个串行关闭，force 仍有界并行。独立 Review 最后关闭部分恢复失败在“无顶层窗口”“已有顶层窗口”“迟到 Browser/Window”三种时序下的悬挂/空壳风险：恢复中 window id 参与 owner 查找，失败 Browser 仍完成登记后立即 force-close，已有窗口经正常 `OnWindowDestroyed` 收敛，迟到窗口直接关闭。
- 自动化：Windows 11 x64、固定 CEF 150、VS 2022。同一多配置目录最终 Debug/Release 全目标 build 均退出 0（约 66s/68s）；最终 Review 前完整 CTest 各 125/125 PASS（618.60s/501.35s）。失败清理第一轮修正后，Debug/Release `session_restore_contract_test.cc|alloy_session_restore_windows|page_snapshot_cef_integration_windows|alloy_tab_controller_windows|alloy_window_coordinator_windows|windows_cef_shell_source_contract` 各 6/6 PASS（102.11s/76.84s）；最终空壳窗口修正后双配置 product build 再次 PASS（43.84s/49.33s），去除未受影响的长时 page-snapshot 项后相关 5/5 再次 PASS（15.67s/12.32s）。`git diff --check` 与 repo-guard PASS；RG-003/004 为已审查的规模/既有全仓 warning，RG-006 artifact 归 24W3；`clang-format` 在 PATH 与常用安装位置均 NOT_FOUND，新增 C++ 按邻近格式人工复核。
- 真实产品：同一份 136-byte `CRAYON_SESSION_V2` default/primary checkpoint 含两个 `crayon://newtab/`，active index=1。刚构建的 Debug 与 Release 均只出现标题“蜡笔 AI Agent 投屏浏览器”的可见 Alloy 产品窗口，标签栏显示“标签页 1/新建标签页”，无旧 Chrome-style 产品窗、无 Application Error；点击系统关闭按钮后窗口列表与 `CrayonBrowser` 进程计数均为 0。checkpoint 曾在窗口存活时由一标签更新为两标签，证明 crash 前稳定状态 checkpoint；正常退出后仍保持合法双标签内容。真实调试中曾用原 session 稳定复现上述 CEF FATAL 与无窗口残留，修复后不再复现；最终未通过生产故障注入强造资源耗尽型部分创建失败，该分支由 session/coordinator 回滚测试、source contract 和逐时序 Review 覆盖，不冒充真实故障注入通过。
- Code Review 与边界：按 v0.9 依次检查 session/schema owner、Profile/无痕隔离、CEF UI-thread/ObserverList 时序、异步 Browser/Window 生命周期、active generation、checkpoint debounce/原子写、正常/force 关闭、敏感数据与 legacy profile 保留；两轮关闭上述恢复失败生命周期 P1 后，P0/P1/P2/P3=`0/0/0/0`，APPROVE。保留风险为版本化 Alloy root 不自动迁移旧 root 的普通浏览数据，以及资源耗尽型部分创建失败未做生产故障注入；前者避免猜测迁移 Chromium 私有状态，后者不增加测试实现到生产图。24W2b3 日用 adapter 与 24W3 总门禁仍未完成；语言/IME/Narrator/原生 DPI 按用户决策继续后置。
