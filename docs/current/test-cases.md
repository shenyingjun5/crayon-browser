# 蜡笔隐私投屏浏览器测试用例目录

状态值：`AUTO` 自动化、`HARNESS` 专项设施、`DEVICE` 真机、`RELEASE` 发布产物。每个用例实现后必须在测试代码注释或测试名称中保留 ID。

## 1. Repo、模块与构建

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| RG-001 | AUTO | 扫描生产依赖图 | 无 production -> test-support/test framework 依赖 |
| RG-002 | AUTO | 扫描生产源中的内联测试正文、Mock/Fake/testOnly | 零命中；Rust 只允许独立测试模块声明 |
| RG-003 | AUTO | 统计函数/文件行数 | 触发 100/200、2000/3000 提醒；测试文件 >=3000 阻断 |
| RG-004 | AUTO | 扫描端口、UA、超时、token、绝对路径和协议字符串 | 业务可变值只来自配置/常量/能力模型，无凭证 |
| RG-005 | AUTO | 解析 workspace 依赖图 | 无循环依赖；仅 cast-adapter 依赖 Cast-SDK |
| RG-006 | AUTO | 构建 Release 并扫描符号/资源 | 不包含 test、fixture、debug remote control 和测试框架 |
| RG-007 | AUTO | 修改 IPC/schema 后运行兼容检查 | 当前与前一协议版本 golden vectors 均通过 |
| RG-008 | AUTO | 检查 Cast-SDK revision 与允许清单 | 固定 commit；无隐式本机 path dependency |

## 2. 浏览器、播放门禁与媒体观察

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| BR-001 | AUTO | 导航本地 HTTPS fixture，前进/后退/刷新 | URL、历史和 load state 正确 |
| BR-002 | AUTO | 两标签分别登录不同 fixture origin | Cookie/存储按 Profile 正常工作且不跨 origin |
| BR-003 | AUTO | 页面脚本伪造 `playing`，无可信输入 | 投屏按钮保持禁用 |
| BR-004 | AUTO | 用户点击播放且 `currentTime` 推进 | 当前标签变为 `PlaybackEligible` |
| BR-005 | AUTO | 点击页面非播放区域后页面自动播放 | 不满足播放门禁 |
| BR-006 | AUTO | 两个视频同时播放，一个可见且最近操作 | 选择最近操作且可见面积最大的媒体 |
| BR-007 | AUTO | 导航后旧 frame/worker 迟到上报 | navigation ID 不匹配，事件丢弃 |
| BR-008 | AUTO | 页面含 iframe/Worker/MSE 网络候选 | observation 带正确 frame/navigation/source，不泄露正文 |
| BR-009 | AUTO | 页面包含广告与正片顺序播放 | 不点击跳过、不 seek、不改速率、不按广告域过滤 |
| BR-010 | AUTO | fixture 监听 click/play/currentTime setter | 产品注入不产生自动点击和广告快进 |
| BR-011 | AUTO | EME `encrypted` signal 与 clear URL 同时出现 | 关联候选 protection 升级，直投被拒绝 |
| BR-012 | AUTO | `blob:`/MediaStream 无底层 URL | 只允许标签页模式或明确不支持，不伪造直投 URL |
| BR-013 | AUTO | 关闭标签/窗口时仍在探测 | observer 取消，旧事件不能重建候选 |
| BR-014 | DEVICE | 中文/英文输入法、缩放、全屏、下载、无障碍 | 支持平台行为符合 PRD，无崩溃/焦点丢失 |

## 3. 候选、预检与策略

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| PL-001 | AUTO | 相同 URL 来自 DOM/network/currentSrc | 合并为一个 candidate，保留多份证据 |
| PL-002 | AUTO | URL 含短期签名 query | 不错误去 query；日志只保留脱敏 origin |
| PL-003 | AUTO | MP4 HEAD 405，Range 返回 `ftyp` | 有界 fallback 后识别 MP4，不下载主体 |
| PL-004 | AUTO | HLS master 含 variant/audio/subtitle | parser 识别完整资产关系 |
| PL-005 | AUTO | HLS 含 AES-128/SAMPLE-AES/SESSION-KEY | 按当前合规策略拒绝直投/relay，不请求 key |
| PL-006 | AUTO | DASH `ContentProtection` | DRM 拒绝；无可用直投 URL |
| PL-007 | AUTO | 接收端不支持候选 codec/protocol | 策略选择 Mirror 或稳定 unsupported |
| PL-008 | AUTO | 候选需要 Cookie/Authorization | 不把 secret 交给接收端；按授权策略选择 relay/mirror |
| PL-009 | AUTO | 广告连续性 unknown 且选择从头播放 | 选择 Mirror，不走 direct |
| PL-010 | AUTO | 未通过用户播放门禁但其他条件均满足 | 结果必须 Reject |
| PL-011 | AUTO | 平台不支持 system audio | capability 驱动降级并返回明确原因 |
| PL-012 | AUTO | 旧 candidate plan 超 TTL 或 receiver 变化 | 重新规划，不能复用旧 Direct |
| PL-013 | AUTO | 同一输入在 Win/mac/Linux/Harmony fake capability 下运行 | 安全/隐私结论一致，只有可用模式不同 |
| PL-014 | AUTO | Probe 网络错误、超时、取消 | 不升级权限，不从普通失败推导 relay |

## 4. Session Relay 安全与媒体行为

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| RL-001 | AUTO | 构建正式 LAN router | 不存在 `/api/extract`、任意 URL `/proxy`、player/probe 页面 |
| RL-002 | AUTO | 创建 session | token/resource ID 至少 128-bit CSPRNG，不含上游 URL |
| RL-003 | AUTO | 未配对设备、错误 receiver ID/IP 请求资源 | 401/403，不访问 upstream |
| RL-004 | AUTO | 停止 session 后重复访问 | 10 秒门禁内失效；vault/registry 清空 |
| RL-005 | AUTO | 导航、route lost、设备替换、Profile 销毁、App exit | 每个触发器均撤销 session 和 secret |
| RL-006 | AUTO | 上游重定向到 localhost/RFC1918/link-local/metadata | 每跳拒绝；不返回内部响应 |
| RL-007 | AUTO | DNS 校验公有 IP 后重绑定私网 | 实际连接固定已校验地址或拒绝 |
| RL-008 | AUTO | 猜测 token/resource ID、路径穿越、超长头、POST/CONNECT | 全部拒绝且有界处理 |
| RL-009 | AUTO | MP4 GET/HEAD/Range 0-, suffix、越界 | 正确返回 200/206/416，seek 可用 |
| RL-010 | AUTO | HLS master/media 相对、绝对、query URI | 全部改写为 opaque resource ID，字节语义正确 |
| RL-011 | AUTO | fMP4 init/segment 和 TS segment | 输出 hash 与 upstream 一致，不经文本转换 |
| RL-012 | HARNESS | 8 并发 segment + 慢接收端 + 上游卡死 | 无全局串行；并发/缓存/超时有界 |
| RL-013 | HARNESS | 30 分钟 VOD/live | 内存不随媒体时长线性增长；停止后回落 |
| RL-014 | AUTO | 扫描日志、DTO、诊断、磁盘 | 无完整 URL query、Cookie、Authorization、token |
| RL-015 | AUTO | 上游跨 origin redirect | header scope 逐跳校验，敏感头不泄露 |

## 5. Cast-SDK 适配与设备会话

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| CS-001 | AUTO | Fake facade start/refresh/stop discovery | 生命周期幂等，UI 只消费设备快照 |
| CS-002 | AUTO | 设备同名、UDN 冲突、多网卡候选 | UI 使用稳定 device ID，不缓存 IP |
| CS-003 | AUTO | 六位码成功、过期、错误、取消 | 映射稳定状态；浏览器不实现算法副本 |
| CS-004 | AUTO | receiver capability 变化 | policy 使用 SDK 最新 assessment，旧缓存失效 |
| CS-005 | AUTO | Direct plan 投送 | 只通过 Cast-SDK facade，App 不拼 SOAP/URL |
| CS-006 | AUTO | pause/seek/volume/stop 带旧 session handle | SDK/adapter 拒绝 stale generation |
| CS-007 | AUTO | receiver 自然结束、电视端 Stop、route lost | App 状态收敛并撤销 relay/mirror |
| CS-008 | AUTO | SDK 返回 unsupported/protocol/permission 错误 | 映射稳定产品错误码，不解析自然语言 |
| CS-009 | AUTO | Cast-SDK revision 升级 | API contract、行为 golden、依赖树和许可证检查通过 |
| CS-010 | DEVICE | 自动发现和投屏码分别连接蜡笔接收端 | 发现、确认、首帧、控制、停止闭环 |

## 6. Profile、隐私与安全存储

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| PV-001 | AUTO | 隐私 Profile 登录、写 Cookie/LocalStorage/IDB/Cache/SW | 会话内可用 |
| PV-002 | AUTO | 关闭最后一个隐私窗口并新建会话 | 前述存储全部不可读 |
| PV-003 | AUTO | 清理一项被锁定/失败 | UI 明确失败；不宣称清理完成 |
| PV-004 | AUTO | 两个常用空间登录同一 origin | Cookie、权限和存储互不可见 |
| PV-005 | AUTO | 销毁常用空间 | 先停止投屏/撤销 secret，再删除受控目录 |
| PV-006 | AUTO | 构造符号链接/目录联接逃逸 | 拒绝删除目标，外部文件不受影响 |
| PV-007 | DEVICE | DPAPI/Keychain/Secret Service/HUKS 写读删 | 密钥不以明文落盘，解除配对后删除 |
| PV-008 | AUTO | 默认启动并检查网络/日志 | 遥测关闭，无浏览 URL/标题上报 |
| PV-009 | AUTO | 标准/严格防追踪模式 | 统一策略，不为不同 Profile 生成唯一随机指纹 |
| PV-010 | AUTO | 诊断导出预览 | 用户可见内容与实际发送一致，无秘密 |

## 7. 标签页采集与平台生命周期

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| CP-001 | DEVICE | 1080p30 动态时间码 + 声音投屏 30 分钟 | P95 延迟/首帧达到 PRD，音画同步 |
| CP-002 | DEVICE | 窗口遮挡、最小化、全屏、多屏/缩放 | 行为符合平台定义且有明确降级 |
| CP-003 | DEVICE | 撤销录屏/系统音频权限 | 会话安全停止并提示，不循环申请 |
| CP-004 | DEVICE | 休眠/唤醒、锁屏、网络切换 | 旧 session 不误恢复；用户可重连 |
| CP-005 | DEVICE | 受保护画面/DRM fixture | 遵循系统黑屏/拒绝，不绕过 |
| CP-006 | HARNESS | encoder 不可用或过热/资源不足 | capability 降级；无静默无限软件编码 |
| CP-W01 | DEVICE | Windows WGC + WASAPI + MF/D3D11 | 画面、系统音频、硬编和释放通过 |
| CP-M01 | DEVICE | macOS ScreenCaptureKit + VideoToolbox | 权限、公证包、音频、硬编通过 |
| CP-L01 | DEVICE | Linux Wayland + PipeWire portal + VA-API | Portal、音频、GPU、退出回收通过 |
| CP-H01 | DEVICE | Harmony AVScreenCapture + AVCodec | ArkWeb 页面与采集、后台限制结果明确 |

## 8. 端到端、稳定性与发布

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| E2E-001 | DEVICE | 打开 fixture -> 点击播放 -> 自动发现 -> Mirror -> Stop | 首帧、控制、清理闭环 |
| E2E-002 | DEVICE | 登录 clear MP4/HLS -> Direct/Relay -> seek -> Stop | 不泄露凭证，Range/HLS 正常，token 失效 |
| E2E-003 | DEVICE | DRM fixture 播放后点击投屏 | 本机播放与投屏能力分离；不产生 direct URL |
| E2E-004 | DEVICE | 广告 + 正片 fixture 选择从头播放 | 使用 Mirror 并保留完整页面编排 |
| E2E-005 | DEVICE | 100 次开始/停止/设备切换 | 无线程/socket/token/临时目录持续增长 |
| E2E-006 | DEVICE | 8 小时标签页投屏 | 无崩溃、音画漂移在预算内、资源稳定 |
| E2E-007 | DEVICE | VPN/多网卡/IPv6/防火墙切换 | 不广告错误 LAN 地址，不连接未知设备 |
| UP-001 | RELEASE | 干净机器安装、首次启动、卸载 | 签名正确；用户数据边界符合说明 |
| UP-002 | RELEASE | Stable N -> N+1 覆盖升级 | Profile/配对按 schema 迁移；失败可恢复 |
| UP-003 | RELEASE | 尝试降级到已知高危内核 | 阻断或明确安全策略，不静默降级 |
| UP-004 | RELEASE | 扫描安装包 SBOM/NOTICE/源码映射 | 组件、许可、版本和产物一致 |
| UP-005 | RELEASE | 扫描 H.264/AAC/CDM 组件 | 只有书面放行组件进入产物 |
