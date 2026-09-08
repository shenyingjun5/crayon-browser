# 第一期代码与界面审查

- 日期：2026-09-08。
- 基线：`02a4a20d115c61436497312eb015fad27e37c7e7`，初始工作区干净。
- 范围：第一期 REL/SHELL/BUX 当前契约、最近提交、Alloy 图标/标签/导航/地址栏/Windows 产品装配、新标签页实现及对应测试。尚未完成全仓安全或发布总审。
- 初审结论：REQUEST_CHANGES。已修复项按下方分项记录；新增发现仍有待处理项，不能把局部APPROVE视为全仓或发行通过。

## 发现

1. **P2：标签栏没有网页标题。** `alloy_tab_strip.cc` 的 `Sync` 总是生成本地化“标签页 + 序号”。Windows Host 的 `OnBuiltinTitleChange` 只更新历史/书签用标题缓存，没有传给标签栏。两个不同网页无法凭标签标题区分。归 `PLT-SHELL-24W2b3b5b`，必须覆盖普通/后台标签、标题更新、关闭及跨窗口行为。
2. **P2：图标激活无条件清除键盘焦点。** 各 `OnButtonPressed` 调用 `ReleaseAlloyIconFocus`，后者先关闭再恢复 focusability；它没有区分鼠标和键盘激活。恢复“可聚焦”不等于恢复当前键盘焦点，需要真实 CEF 输入回归确认连续键盘操作。归 `PLT-SHELL-24W2b3b5b`，不能通过删除键盘可达性解决点击后蓝框。
3. **P2：新标签页与要求的 Chrome 风格不符。** 生产 `RenderNewTabStylesheet` 使用左对齐大标题、紫色品牌装饰和带边框的横向卡片；共享设计契约要求一致视觉语言。归 `PLT-SHELL-24W2b3b5a`，验证生产生成内容、无痕和三语言布局。
4. **P2：当前索引状态与实现不一致。** `docs/current/README.md` 仍宣称默认产品是 Chrome-style、Windows content/media host 尚未装配，SHELL 最新记录和 `AlloyProductHostWin` 已显示默认 Alloy 接线。`docs/plans/README.md` 的 b3b4 焦点说明也落后于已提交的“清除后恢复 focusability”实现。收口时按真实代码更新，不把旧叙述作为完成证据。
5. **P2：Windows 普通辅助窗口缺少完整浏览器工具栏。** `AlloyProductHostWin::OnWindowCreated` 对无痕窗口装配 navigation/omnibox，对普通 popup/restored window 仅装配 transfer surface 与内容容器。共享页面的 Chrome 风格不能补齐这一窗口级缺口。归 `PLT-SHELL-24W2b3b5d`，需让每个普通浏览窗口消费各自 navigation/omnibox/tab owner 的可见投影，再做 Windows 效果验证；当前尚未修复，不能声称全部 Windows 界面完成。
6. **P2：书签/历史领域允许带用户信息的URL进入持久化。** 两个 `IsValidUrl` 仅检查HTTP(S)前缀、长度和控制字符，因此 `https://user:password@example.test/` 可被新增及codec导入，违反所属SHELL任务声明的credentials拒绝边界。Windows书签入口把当前tab URL交给该领域，不能依赖页面来源提供安全字符串。归 `PLT-SHELL-11M2`，在共同领域入口拒绝含userinfo及歧义authority的URL，保留合法URL/IPv6/路径与query中的`@`；共享代码同时覆盖两平台。
7. **P1：下载控制在外部回调后继续使用旧迭代器。** `AlloyDownloads` 的确认/丢弃/暂停/继续/取消操作先调用外部callback，再通过之前的map迭代器更新item；同步回调若触发Shutdown，entries被清空，旧迭代器失效。部分动作还会在领域已不允许转换时先触发外部副作用。归13M，需先预检领域转换，调用后重新校验owner/generation/state，并覆盖同步Shutdown/进度回调；不能以正常下载单次通过替代重入边界。
8. **P2：历史列表与搜索词会失去一致。** `AlloyHistory::RefreshAll` 在新导航、删除和替换数据后显示全量投影，但保留原query；清空搜索则因store的空查询语义显示空列表。归12M，统一按当前query刷新，并保留全列表的newest-first与容量边界。
9. **P1：Profile/settings callback可使保存中的record失效。** adapter保存/reset/cleanup持有ProfileRecord指针跨外部callback，同步Shutdown清空profiles后仍继续使用该指针；BeginCleanup也在callback之后才发布pending generation，无法接收同步完成。归14M1，按拷贝输入、重查owner、拒绝嵌套和先绑定generation后发起外部动作处理。
10. **P1：权限确认入口未直接复核提示期限。** `AlloySiteControls::ResolvePermission` 写入授权前未检查deadline，只有独立过期扫描执行拒绝；提示到期后先收到确认时仍可能授权。归15M1，先以确定时间输入复现，再在同一入口拒绝过期front、消费一次callback且保留后续队列；不得依赖定时器顺序。
11. **P1：持久文件关闭时失败仍可能替换旧数据。** bookmarks/history/preferences三类codec只在ofstream析构前检查good；小载荷缓冲直到析构才写出，flush/close失败未反馈给Save，之后仍rename。归11M3，以测试子进程文件尺寸限制复现延迟错误，提交前显式检查flush/close并保留旧target。

发现 3 已由 5a 修复：共享 C++ 行为契约 1/1 PASS、生产生成页面 16 组布局检查及键盘/200% CSS 缩放 PASS；已做截图检查。Mac App 验收另外执行，不能用预览代替。

## 修复与复审（2026-09-08）

- P1 快速编辑尾部丢失：真实 Mac 产品稳定复现，前端 leading-only 80ms 节流会永久丢掉末尾 input。移除丢弃路径，既有 Browser 编辑 owner/渲染调度保留；真实快速输入、Cmd+S、磁盘完整回读通过。共享生产脚本的同一任务连续 input 三次全部投递，Windows CEF probe 补连续输入 write-back 回归，Windows执行待补。
- P2 标签标题/焦点：共享标签栏读取现有 Host 标题投影，标题更新不重建按钮，超长标题回退；窗口标题随活动标签更新。图标完成状态不再清除键盘焦点。Mac CEF 专用窗口实测标题、焦点、点击、容量/布局 PASS；Windows Host 代码已接线，Windows编译/产品效果仍 NOT_RUN。
- P2 Mac 菜单/快捷键：补 AppKit 本地化菜单与 CEF 命令映射，复用 Markdown open/save owner，正确处理 Shift 字符与产品名占位符，退出时销毁 target 并恢复旧菜单。原生快捷键行为测试 PASS；真实 Cmd+T/O/L/S 与菜单退出通过。
- P2 Mac 构建/入口：会话可选分组显式空默认值；新标签配置接到产品 URL；CEF 框架重新装配避免增量 `ln -sf` 跟随旧目录链接产生坏链。重复本地构建、strict/deep ad-hoc 验签 PASS。
- 视觉：共享新标签页及 Markdown 统一中性色、蓝色交互、圆形图标、清晰留白、深色主题；Markdown 保存成功使用 status/成功色，下一次编辑清除旧状态，状态栏不再挤出页面。生产渲染三视图×两主题×宽/窄12组无溢出，视觉截图已检查。
- 验证：Mac 既有全套 CTest 101/101 PASS（244.32s），新增共享 CEF 标签栏与受影响 MDV/菜单/source/package 回归13/13 PASS（8.28s）；后补 Shift 前后标签原生回归1/1 PASS（1.74s）。源码 repo-guard `passed=true`；RG-003/004 为既有规模/仓库提醒，Windows Host 3275行，本次仅标题投影增量，未更改其领域所有权。
- 边界复审：未改变 grant/URL/file/DRM/Cast 协议与状态 owner；生产构建不引入测试 target；自有页面不新增外部资源请求。Mac Alloy 迁移新增测试结果按所属任务记录，不覆盖本地产品基线证据。剩余风险是 Windows 实机、Mac Alloy 完整迁移、真机投屏、输入/语言、长稳与发行门禁。
- 迁移复核发现：Luna 在冻结测试产物上复核时，06M 地址栏建议鼠标点击发生 stage 5 超时，输入/建议 generation/显示安全已通过，整项仍为 FAIL。原单次 PASS 不足以关闭本项；06M 重开，07M 下游验收暂停。主会话负责根因判断与方案/代码 Review，Terra 负责明确实现，Luna 负责固定验证；Agent 自报结论不替代主会话复审。
- 上述迁移失败后续：06M改以真实CEF键盘激活建议按钮，连续3次PASS；系统鼠标验证保留23M，未声称修复全局鼠标路由。07M导航与08M多窗口随后通过真实CEF验收；04M页面输入改为BrowserHost定向鼠标事件，beforeunload连续3次PASS，09M高级标签复核闭合。10M新增Mac会话原子文件adapter，经主会话Review修正长文件名、缺失父目录和测试清理后，Luna产品构建/验签与2项存储契约PASS。03M..10M最高VERIFIED；Mac默认产品仍是Chrome-style宿主，完整Alloy切换归24M。
- 当前书签Review：从文件成功替换树后未清理旧 `folder_items_`，可能继续展示已失效条目；11M先补稳定复现再修复，Windows共享代码同步。
- 后续出口：11M旧folder投影、11M2持久URL、12M历史筛选、13M下载callback、14M1配置callback、15M1权限期限、11M3延迟写入失败均已修复，经主会话Review APPROVE、Terra实现、Luna红绿验证，各4项定向回归PASS。14M2真实Profile/cookie隔离与15M2真实CEF安全探针各1/1 PASS，完整关闭必需；15M2另有测试证书复制与严格签名证据。完整命令/指纹见SHELL任务。16M真实页面工具探针最终1/1 PASS（5.76s），关闭PDF路径NUL漏检与完成/取消后回调未清空两项问题；Mac libc++小回调移动保留源是第二项的实际平台根因。修复保持共享API与输出owner，主会话Review APPROVE，16M最高VERIFIED。Windows辅助窗口UI缺口仍待办；不将候选组件证据当成Mac默认Alloy产品完成。
- 提交前一致性复审：既有Windows interactions probe仍要求书签按钮激活后丢失焦点，与本轮保留键盘焦点修复相反；只同步该断言与诊断，未启动17M或改变Windows交互实现。Windows执行仍NOT_RUN。
- 用户收尾指令：2026-09-08要求完成当前16M后暂停并提交推送；17M及后续不启动，第一期整体、默认Alloy切换及完整UI改造不宣称完成。本轮代码范围Review APPROVE不等于产品发布Go。

## Mac 构建恢复发现

- 首次失败是 Cargo 缓存目录的沙箱写权限，工具授权后依赖和 Rust hosts 已编译，不是产品代码错误。
- 初版本地 adapter 尝试 Unix Makefiles，在系统 Make 3.81 下，带 `(Alerts)` 等后缀的 CEF Helper `LINK_DEPENDS` 被识别为 archive member，尽管对应文件已生成，主 App 仍报 `No rule to make target`。该替代 generator 方案已撤回，按仓库 CMakePresets 使用 Ninja。
- 本地工具固定为 PyPI/scikit-build 的 Ninja 1.13.2 universal2，Apache-2.0，wheel 306611 bytes，SHA-256 `fd82e26c0706ad4ab88e5fdd26f3fab0a987a90f810160f6c322e752c6af298b`；只安装到忽略的本地 toolchain 目录，不进入产品包或更新产品依赖。

## 第一期剩余工作

**后续范围调整：** 用户 2026-09-08 要求 Mac 优先构建调通，Windows UI 代码同步改成 Chrome 风格、效果后验。当前推进共享页面、macOS Chrome-style 可运行基线、SHELL-03M 起的 Alloy 迁移和对应平台收口；旧 Windows 首发队列不作为本轮执行顺序。

Windows 待验证范围保留：SHELL-24 界面和三闭环总回归、25W 旧宿主退出、26W 真实 Direct/Relay/拒绝/交接与生命周期、27W 总汇。REL-03/04 仍 TODO。

必须保留的外部门禁：LOC-07W/23W 真实系统语言、IME、Narrator 与原生 DPI；PLT-W05c..f 正式接收端和可信物理输入；CNT-21W、MRT/MDV Windows 对称回归；PRV-13AW、PLT-19W；QAR 的安全、性能、30 分钟/8 小时、安装/升级/回滚、SBOM、候选 Go/NoGo。逐项状态以所属 Roadmap 为准；本文不重建平行状态台账。

macOS 后续独立平台证据不计入 Windows 首期候选通过。第二期 Agent/CLI/MCP、Workflow、Hub、Partner、模型与 HarmonyOS 继续关闭。

## 本轮验证边界

已完成：静态核对生产调用方与对应测试、最新 Git 提交和上述 Roadmap。

未运行：Windows CEF/产品、正式接收端、系统输入/语言矩阵、平台发布门禁。当前工作机为 macOS，已恢复固定 CEF/SDK 和 Debug 构建缓存。CodeGraph 只读状态/查询首次调用在10秒超时，随后使用定向源码读取；不把索引失败当产品失败。

## 16M完成后的提交检查与暂停

- 冻结输入：基线HEAD `02a4a20d115c61436497312eb015fad27e37c7e7` 加本轮工作树，源码指纹 `88a775fbd089405c787d9166e11726e9bef3ddc01e843998ced617e65f86300e`；包含tracked/untracked源码与SDK revision，排除文档和输出。Mac arm64 Debug，固定CEF150与SDK `44c3a99871aa1e68cbda71eacefbb41d23a747a8`。本轮不推送本地缓存、证书fixture副本、测试输出或应用包。
- `git diff --check` PASS/exit0；`cargo run --quiet -p repo-guard -- scan --root .` PASS/exit0/2.23s；`cargo fmt --all -- --check` PASS/exit0/0.79s；`node --test tools/brand-assets/tests/managed-paths.test.mjs` 3/3 PASS/exit0（测试报告59.84ms）；`node tools/brand-assets/verify.mjs` PASS/exit0，8 checks/27 generated files。日志 `.cache/evidence/luna-final-{diff-check,repo-guard,cargo-fmt,managed-paths,brand-assets-verify}.log`。
- 本机无pwsh，未直接执行 `scripts/check.ps1 fast`；按其顺序执行等价步骤时，`cargo test --workspace` FAIL/exit101/约99.8s。已报告的64个suite共690 passed/1 failed；失败包 `crayon-platform-macos` 为33 passed/1 failed，`secure_store::tests::object_safety_assertion` 在 `secure_store_tests.rs:115` 返回 `delete: Unavailable`。日志 `.cache/evidence/luna-final-cargo-test-workspace.log`。该模块本轮git diff为空；现有测试涉及真实Keychain，不把未经重现的环境/并发猜测当成根因。fast整体保持FAIL；后续legacy-unit、本次原定产品构建和117项CTest均在首错处NOT_RUN。
- 收尾计划调整：用户要求当前16M完成后暂停，不扩展修复既有Keychain测试。独立开启同源Mac产物/CEF检查批次，10分钟预算、首错停止；不会覆盖fast失败记录，不启动17M或后续Roadmap。
- 独立批次结果：前后源码指纹保持 `88a775fbd089405c787d9166e11726e9bef3ddc01e843998ced617e65f86300e`。`PATH="$PWD/.cache/toolchains/ninja/bin:$PATH" python3 scripts/build_macos_local.py --cef-root '.cache/cef/cef_binary_150.0.10+g8042e43+chromium-150.0.7871.101_macosarm64' --flavor Debug` PASS/exit0/4.81s（receipt内部4.68s），包括严格deep签名检查；receipt `.cache/build/macos-arm64-cef-debug-ninja/local-builds/1788846026784697000/receipt.json`，状态 `BUILD_VERIFIED_NOT_ACCEPTED`，产品SHA256 `21076b1a199c02338c241ddda9c10162355999ad9a9bda891211cecd4e4fc044`。日志 `.cache/evidence/luna-final-product-build.log`。
- `PATH="$PWD/.cache/toolchains/ninja/bin:$PATH" ctest --test-dir .cache/build/macos-arm64-cef-debug-ninja --output-on-failure --stop-on-failure` 117/117 PASS/exit0/195.21s；日志 `.cache/evidence/luna-final-ctest.log`。此结果覆盖登记的C++/CEF检查，不覆盖失败的Rust Keychain测试、未运行legacy-dev、Windows实机或默认Alloy产品切换。
- 最终Review：本轮已修改代码范围APPROVE；逐原子任务状态见SHELL，16M为VERIFIED。Keychain既有失败与发布/设备门禁保留，整体第一期不作完成或发布通过结论。用户已授权提交推送；后续开发暂停。
