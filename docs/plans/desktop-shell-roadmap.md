# 自定义桌面外壳与 Alloy 迁移 Roadmap

- 日期：2026-09-04；决策来源：用户明确选择“自定义外壳＋Alloy”，长期按此架构、一期开始迁移。
- 状态：`PLT-SHELL-00/01 VERIFIED`；下一步 `02 READY`；尚未切换产品默认宿主。
- 归属：`PLT-M05/PLT-W05` 的跨领域迁移切片，前缀 `PLT-SHELL-`。不另建一套 REL/BUX/Cast 业务计划，不增加 297 个顶层任务或 212 个唯一用例 ID。
- 一期总入口：[REL](release-v1-roadmap.md)；投屏协议与交互仍由 [PLT-CAST-R](cast-experience-redesign-roadmap.md) 拥有。
- 平台：当前机器先推进共享实现与 macOS arm64 验证；Windows 10/11 x64 对称落地、独立发布门禁。保留 Windows 首发政策，不以 Mac 结果替代 Windows，也不等待 Mac 签名/公证才开发 Windows。

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
| 02 | READY | 00 VERIFIED | engine-api 与 shell 内容视图挂载/能力契约，按 §2 最小增量 | U；公开头独立编译、旧接口兼容、生命周期/未知能力拒绝；无运行后端宣告 |
| 03M / 03W | TODO | 02 VERIFIED | 对应平台 windowed Alloy 单窗口＋两个内容视图的生产宿主原语与独立 Harness | H；同窗口切换不重建网页、缩放/隐藏/关闭/资源回落；默认入口不变 |
| 04P | TODO | 01、03P VERIFIED | TabController/TabModel 的异步创建/关闭与视图映射 | H；beforeunload 取消、迟到创建、崩溃/退出、无双 owner |
| 05P | TODO | 04P VERIFIED | 自绘基础标签栏和新标签入口，消费既有 tab 模型 | H；新建/激活/关闭/排序、焦点/容量，不调用 Chrome 新标签命令 |
| 06P | TODO | 05P VERIFIED | 自绘 omnibox 编辑、搜索/URL 判定和建议接线 | H；输入/取消/旧建议、IDN/URL 显示安全边界、无默认联网建议 |
| 07P | TODO | 06P VERIFIED | 导航控制/加载状态/站点身份可见反馈 | H；前后退/刷新/停止、重定向、证书错误、页面不能伪造安全标识 |
| 08P | TODO | 05P、07P VERIFIED | popup 与多窗口生命周期迁移 | H；来源与用户手势、容量、关闭/恢复、窗口间隔离 |
| 09P | TODO | 08P VERIFIED | 高级标签功能接线：固定/复制/静音/搜索/分组/跨窗口移动 | H；复用 advanced 模型；移动若需重建，不隐式重载表单或丢状态，拆子项解决 |
| 10P | TODO | 08P VERIFIED | 会话恢复与崩溃恢复绑定新宿主 | H；无痕不持久、损坏数据、重复恢复、schema 前后兼容、不可静默丢用户标签 |
| 11P | TODO | 07P VERIFIED | 书签栏/管理入口接入已有 store/view | H；编辑/搜索/导入导出、跨 Profile、失败反馈 |
| 12P | TODO | 10P VERIFIED | 历史/最近关闭入口接入已有 owner | H；删除边界、无痕隔离、恢复正确目标 |
| 13P | TODO | 07P VERIFIED | 下载 UI 与原 CefDownloadHandler 接线 | H；取消/续传、危险状态、受控保存和打开位置 |
| 14P | TODO | 08P、10P VERIFIED | 设置/Profile/无痕选择 UI 接线 | H；独立 request context、设置 readback、清理失败显式反馈 |
| 15P | TODO | 07P、14P VERIFIED | 权限/证书/popup/外部协议可信确认面板 | H；origin/TTL/导航失效、默认拒绝、不调用网页伪造面板 |
| 16P | TODO | 07P VERIFIED | 查找/缩放/全屏/打印/PDF 页面工具接线 | H；逐能力验证，不能假定 Alloy 提供 Chrome UI；PiP 不支持时明确矩阵 |
| 17P | TODO | 15P VERIFIED | 主菜单/上下文菜单/拖放/剪贴板/本地文件入口迁移 | H；平台快捷键、About/许可、安全文件选择、取消与来源约束 |
| 18P | TODO | 17P VERIFIED | 内置新标签/MDV 内容接入新 host | P；源码/预览/编辑/原子保存/冲突，Mermaid/Highlight/KaTeX 离线；复用 MDV/MRT 门禁 |
| 19P | TODO | 07P、17P VERIFIED | 网页 Markdown 从新入口到原快照/导出链 | P；当前标签、导航取消、跨源/隐藏内容拒绝、复制/保存；CNT addendum |
| 20 | TODO | 02、PLT-CAST-R08u1 VERIFIED | CastEntrySurface 去 LOCATION 耦合，按钮/面板挂到自绘栏 | U＋H；布局/灰态/事件/释放；无真实后端时仍不允许开始 |
| 21P | TODO | 07P、15P、20、R03b/R04/R07 VERIFIED | 对应 PLT-CAST-R08P 的 Alloy 产品接线，不另建投屏 owner | P；多视频/设备明确选择、连接不播放、错误/播控、MHV2 兼容拒绝 |
| 22P | TODO | 21P、PLT-CAST-R09 VERIFIED | 对应 PLT-CAST-R10P 的 Browser-owned 覆盖层接线 | P；普通主 frame、裁剪/旧几何/焦点/伪造拒绝；不可靠 iframe/fullscreen/PiP 不绘制 |
| 23P | TODO | 09P、11P..19P、21P、22P VERIFIED | 全外壳本地化/IME/键盘/读屏/缩放/主题回归 | P；LOC 对应平台矩阵、UX-001..018；不擅改系统设置 |
| 24P | TODO | 01..23 的对应平台项 VERIFIED | 对应平台产品默认入口切至自定义 Shell＋Alloy | P；三闭环和日用功能无回退、入口与 capability 真实性 Review；只切本平台 |
| 25P | TODO | 24P VERIFIED | 移除该平台旧 Chrome 宿主/LOCATION 生产接线及临时迁移开关 | P＋artifact scan；另一平台仍需要的共享代码保留隔离，不删除他人改动 |
| 26P | TODO | 25P VERIFIED | 在新默认宿主复验 Direct→Relay→拒绝/交接→稳定性 | R；映射 PLT-W05c..f / M05b4..b6/M05c 与 R11P；真实接收端、100 次/睡眠/退出 |
| 27P | TODO | 23P、25P、26P VERIFIED | 新宿主三闭环/隐私/性能/发布证据汇总 | R；PRV/CNT/MRT/PLT/LOC/QAR/REL 对应平台完整门禁；无签名/真机不标 DONE |

`21P/22P/26P/27P` 是原业务/发布任务的迁移检查点，不重复实现、重复领取或双计完成。`03W` 不依赖 `03M` 的真机结果；跨平台共享代码在提交前保证结构可编译，实机分别补证。`24W` 不依赖 `23M/24M`。

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

未覆盖：Windows 构建/真实 UI、Alloy 窗口与多视图、完整适用产品 CTest、设备、性能长稳、安装包/签名/公证均 NOT_RUN。旧工作区测试中的超时/失败仍保留；本次不宣称全仓全绿。后续 `02 READY`（领取前补具体类型/预算/命令），再 `03M` 建真实 Alloy 宿主和复用专用窗口 Harness；没有遗留本人 IN_PROGRESS 原子任务。
