# AI 投屏浏览器 PRD 更新稿：Agent-Native Browser

> 适用项目：`get-video`
> 文档用途：用于更新和完善现有《AI 投屏浏览器 PRD》，并作为 Codex / 项目 AI 后续设计与实施的统一上下文。
> 文档状态：讨论稿（产品定义与技术边界）
> 核心原则：Agent 第一次会探索，第二次会复用，第三次会更快，页面改版后能够安全自愈。

---

## 1. 执行摘要

本产品不应被定义为一个替代 Chrome、Edge 或 Safari 的通用浏览器，也不应被定义为“全网视频解析器”。产品应定位为：

> **面向 AI Agent、网页内容理解和大屏播放的 Agent-Native AI 投屏浏览器。**

它同时服务两类用户：

1. 普通用户：浏览网页、提取正文、生成 Markdown、总结资料、识别可合法投屏的视频，并投到普通 DLNA 设备或蜡笔投屏接收端。
2. AI Agent：通过 MCP、CLI 或内部 API，以结构化、低 Token、可审计的方式读取和操作网页；任务成功后将操作路径沉淀为可复用的站点技能。

产品的核心壁垒不是“AI 能点击网页”，而是：

- 浏览器主动把网页转化为 Agent 易理解的结构化状态；
- 将易变的 DOM 操作抽象为稳定的业务动作；
- 将一次成功的探索过程沉淀为 Workflow Recipe；
- 在页面变化时进行受控、自解释、可回滚的自愈；
- 把网页内容处理、网页自动化与投屏能力连成闭环。

---

## 2. 产品定位

### 2.1 一句话定义

**蜡笔 AI 投屏浏览器是一个为 AI Agent 设计的网页任务运行时：它让 Agent 高效理解和操作网页，把成功路径沉淀为可复用技能，并把网页文章、文档和合规视频投到大屏。**

### 2.2 产品边界

本产品要做：

- 投屏场景下的轻量浏览器；
- 网页正文、表格、图片、链接、媒体和表单的结构化提取；
- 网页转 Markdown、摘要、问答和资料整理；
- 面向 Agent 的 MCP、CLI 和内部 Browser API；
- Action Map、Form Map、Media Map、Risk Map；
- Agent 操作轨迹记录、流程学习、技能保存、版本管理与自愈；
- 普通 DLNA 投屏和蜡笔接收端高级播放；
- 可插拔的大模型配置与隐私模式。

本产品暂不做：

- 完整浏览器生态、默认浏览器竞争、浏览器插件商店；
- 密码管理器或跨设备账号密码同步；
- 全网视频下载、缓存、转存、去广告或 VIP 解析；
- DRM、付费墙、登录限制、区域限制或平台技术措施的绕过；
- 无确认的发布、付款、删除、发消息等高风险外部操作；
- 将用户 Cookie、Authorization、会员 Token 或 DRM 凭证转交电视端；
- 云端集中解析或建立第三方视频资源索引。

### 2.3 建议产品名称

- 中文工作名：蜡笔 AI 投屏浏览器
- 英文工作名：Crayon Agent Browser / AI Cast Browser
- 技术内核名：Agent Browser Runtime

---

## 3. 产品目标与衡量指标

### 3.1 产品目标

1. **让网页可被 Agent 高效理解**：优先输出压缩后的语义快照，而不是让模型反复读取整页 DOM 或截图。
2. **让网页操作更稳定**：Agent 调用稳定的 `action_id`，执行层负责处理 CSS、ARIA、文本和视觉定位的变化。
3. **让重复任务越来越快**：首次探索成功后，可保存为带输入参数、前置条件、成功条件和风险等级的标准流程。
4. **让自动化可控且可信**：所有操作具备风险分级、授权策略、操作日志、敏感信息保护和高风险确认。
5. **形成内容到大屏的闭环**：文章可转 Markdown 或阅读卡片，公开视频可投屏，播放列表可自动续播。

### 3.2 北极星指标

- 同类任务二次执行耗时相对首次探索下降比例；
- 同类任务二次执行的模型 Token 消耗下降比例；
- 已保存 Workflow 的无人工干预成功率；
- Action Map 动作定位成功率；
- 自愈成功率与误操作率；
- 网页转 Markdown 的正文完整率和噪声率；
- 投屏成功率、首帧时间、断流率和续播成功率；
- 高风险操作零越权率。

### 3.3 MVP 建议指标

以下为立项时可调整的初始目标：

- 通用资讯/文档网页 Markdown 正文完整率不低于 90%；
- 常见表单控件 Action Map 覆盖率不低于 90%；
- 已验证站点技能的二次执行成功率不低于 85%；
- 已验证技能相较首次探索，操作轮次或 Token 消耗下降 50% 以上；
- 所有发布、付款、删除、发送等高风险动作均必须经过策略检查；
- 普通公开 MP4/HLS 在受支持 DLNA 设备上的成功投屏率不低于 90%。

---

## 4. 典型用户场景

### 4.1 网页资料整理

用户打开一篇文章，要求：

> 提取正文，保留标题、层级、表格、代码和原始链接，生成 Markdown，并总结关键结论。

浏览器直接向 Agent 提供可读 Markdown 和引用信息，不要求 Agent自行清洗整个 DOM。

### 4.2 多页面研究

用户打开多个标签页，要求 Agent：

- 分别提取页面内容；
- 去重并保留来源；
- 生成一个带引用的资料包；
- 保存为 Markdown；
- 必要时把报告转成大屏阅读卡片。

### 4.3 小红书草稿沉淀

第一次执行：

1. Agent 打开创作者中心；
2. 用户自行完成登录或必要验证码；
3. Agent 探索发布入口和编辑表单；
4. 上传素材、填写标题和正文；
5. 保存草稿并验证成功；
6. 浏览器记录轨迹并建议保存为 `xiaohongshu.create_draft`；
7. 用户确认后，流程进入个人站点技能库。

第二次执行：

1. Agent 将输入内容整理成小红书格式；
2. 直接调用 `xiaohongshu.create_draft`；
3. 执行层完成状态检查、字段填写和草稿保存；
4. 返回执行结果、草稿位置和审计摘要。

保存草稿可以按用户策略授权；“正式发布”必须视为高风险动作，默认每次确认。

### 4.4 网页视频投屏

用户在浏览器中打开网页并播放视频。浏览器检测到候选媒体后：

1. 生成 Media Map；
2. 检查资源是否公开、是否含 DRM、是否依赖登录凭证或授权 Token；
3. 对符合条件的视频展示候选清晰度、字幕和播放列表；
4. 用户选择普通 DLNA 设备或蜡笔接收端；
5. 蜡笔接收端可进一步支持授权的广告/正片编排和自动下一集。

不符合边界的内容，引导用户使用平台官方投屏或系统镜像。

---

## 5. 总体产品架构

```text
Agent / LLM / CLI / MCP Client
              │
              ▼
      Browser Agent Gateway
              │
   ┌──────────┼───────────┐
   ▼          ▼           ▼
页面理解层   动作执行层    工作流学习层
   │          │           │
Markdown     Action Map   Trace Recorder
Snapshot     Form Map     Recipe Generator
Media Map    Risk Guard   Skill Store
Risk Map     Permission   Selector Healer
   └──────────┼───────────┘
              ▼
       Browser Runtime
  WebView / CDP / 平台适配层
              │
   ┌──────────┴───────────┐
   ▼                      ▼
模型与知识服务          Cast Runtime
摘要/问答/写作          DLNA / 蜡笔接收端
```

### 5.1 运行时原则

- Tauri 继续使用系统 WebView 作为第一阶段浏览器运行时；
- Windows 可利用 WebView2/Chromium 能力做增强；
- 平台差异应封装在 Adapter 层，不向 MCP 或 Workflow 暴露不稳定细节；
- 上层使用稳定的 Page、Action、Form、Media、Workflow 语义模型；
- 浏览器执行器负责将稳定语义动作翻译为 DOM、ARIA、脚本或视觉操作。

---

## 6. Partner Capability Hub：合作网站能力聚合中心

### 6.1 背景与目标

AI 投屏浏览器不仅要支持 Agent 操作网页，还应逐步成为各类网站能力的统一聚合入口。

当网站愿意与蜡笔合作时，可以开放 API、MCP Server、OAuth 授权、内容接口、草稿接口、视频播放接口和投屏 Manifest 等能力。AI Agent 调用蜡笔 AI 投屏浏览器时，不必每次都通过网页自动化完成任务，而应优先使用合作方正式开放、用户已经授权且当前健康可用的结构化能力。

核心目标：

> 有正式合作接口时，优先走 Partner API / MCP；
> 没有合作接口时，走已经验证的 Site Skill；
> 仍不可用时，走受控网页自动化；
> 遇到验证码、登录、风控时，交给用户接管；
> 任何路径都必须经过统一权限、风险、审计与合规策略。

这会让 AI 投屏浏览器从“Agent 可操控浏览器”升级为：

> **Agent 能力网关 + 合作网站接口聚合器 + 网页自动化兜底 + 大屏投屏 Runtime。**

### 6.2 产品定位升级

原始定位：

```text
AI Agent 可以操控浏览器，完成网页任务。
```

升级后定位：

```text
AI Agent 调用蜡笔 AI 投屏浏览器，由浏览器统一判断最佳执行路径：
1. 合作网站 MCP/API；
2. 已沉淀站点技能；
3. 网页自动化探索；
4. 用户人工接管；
5. 不符合安全、权限或合规要求时拒绝执行。
```

最终产品形态：

```text
各种 AI Agent
      ↓
蜡笔 AI 投屏浏览器 / get-video 能力网关
      ↓
Capability Router 能力路由器
      ↓
┌────────────────┬────────────────┬────────────────┬────────────────┐
│ Partner API/MCP │ Site Skill     │ Web Automation │ Human Handoff  │
└────────────────┴────────────────┴────────────────┴────────────────┘
      ↓
内容生成 / 草稿创建 / 视频投屏 / 自动续播 / 数据提取 / 大屏展示
```

Partner Capability Hub 不取代 Browser Runtime、Workflow Learning、Challenge-Aware Agent 或 Cast Runtime，而是把这些能力组织成一个统一、可发现、可授权和可路由的能力平面。

### 6.3 四层能力路由

#### 6.3.1 第一层：Partner API / MCP

这是结构化能力的最高优先级。

当合作方提供正式接口时，Agent 不再需要打开网页、寻找按钮、填写表单或承受页面改版，而是直接调用结构化能力。

示例：

```text
xiaohongshu.create_draft({
  title,
  content,
  images,
  tags
})
```

适合开放的能力：

- 创建草稿；
- 上传图片或视频；
- 读取课程目录；
- 读取视频列表；
- 获取授权播放 Manifest；
- 获取字幕；
- 获取商品列表；
- 创建报告；
- 同步知识库；
- 触发投屏播放；
- 回传播放状态、曝光和点击。

优点：

- 稳定；
- 快；
- 合规边界更清楚；
- 不依赖网页 DOM；
- 不容易触发验证码；
- 合作方可以保留广告、会员、统计、风控和权限控制。

使用 Partner API/MCP 的前提：

- Provider 已通过接入审核并具有可信身份；
- 用户已完成必要授权；
- 请求 scopes 覆盖目标动作且不过度授权；
- Connector 健康可用，版本和 Schema 兼容；
- 动作符合用户策略、风险策略和地区合规要求；
- 返回内容和媒体具有明确授权边界；
- API/MCP 不得通过远程提示或工具描述绕过本地安全策略。

#### 6.3.2 第二层：Site Skill

当没有正式合作接口，但用户已经通过网页自动化成功完成过某个任务，可以将路径沉淀成站点技能。

示例：

```text
xiaohongshu.create_draft
wechat_mp.create_article_draft
bilibili.extract_video_info
course_site.play_next_lesson
```

Site Skill 基于 Workflow Recipe 执行：

```text
打开网页
↓
检查登录状态
↓
执行标准路径
↓
需要验证时用户接管
↓
完成任务并验证结果
↓
记录成功状态和技能健康度
```

优点：

- 比每次重新探索更稳定；
- 可以复用 Action Map；
- 支持 Selector Healer 自愈；
- 可沉淀用户高频任务；
- 适合未合作但高频使用的网站。

Site Skill 仍然属于网页自动化，不代表站点官方授权；产品界面和能力目录必须清楚标识其来源、可信级别和维护主体。

#### 6.3.3 第三层：Web Automation

当没有合作接口，也没有已沉淀技能时，Agent 进入探索模式。

流程：

```text
读取页面
↓
生成 Agent Snapshot
↓
识别 Action Map / Form Map / Risk Map
↓
尝试完成任务
↓
验证任务结果
↓
任务成功后提示是否沉淀为 Site Skill
```

优点：

- 覆盖长尾网站；
- 不依赖合作；
- 可以逐步沉淀技能库。

局限：

- 较慢；
- 稳定性低于正式接口；
- 容易受页面改版影响；
- 可能遇到验证码和风控；
- 高风险动作必须用户确认；
- 仍需遵守网站条款、用户权限和数据使用边界。

#### 6.3.4 第四层：Human Handoff

当遇到验证码、登录、风控、账号安全验证、支付验证或自动化无法安全判断的场景时，Agent 不绕过风控，而是暂停并交给用户接管。

流程：

```text
检测到验证 / 风控 / 敏感步骤
↓
暂停 Agent
↓
保存最近安全断点
↓
用户手动完成验证或敏感操作
↓
浏览器重新检查页面状态与风险
↓
Agent 从断点继续执行
```

具体要求遵循本 PRD 的“Challenge-Aware Agent：验证码与风控接管”章节。

### 6.4 Capability Router：能力路由器

Capability Router 是能力聚合中心的决策核心，负责根据任务选择最佳执行路径。

输入：

```text
用户 / Agent 任务
当前网站或目标 Provider
目标动作与预期结果
用户身份和授权状态
合作能力可用性与健康度
站点技能可用性与成功率
风险等级和确认策略
数据驻留与地区要求
设备状态和接收端能力
用户对执行路径的偏好
```

输出：

```text
Partner API / MCP
Site Skill
Web Automation
Human Handoff
拒绝执行
```

示例：用户说：

```text
把这篇文章改成小红书草稿，保存但不要发布。
```

系统解析：

```json
{
  "intent": "create_social_draft",
  "target": "xiaohongshu",
  "risk": "medium",
  "preferred_path": "partner_api",
  "fallbacks": ["site_skill", "web_automation"],
  "requires_user_confirm": false
}
```

是否确认最终由用户授权策略决定；例如首次创建第三方草稿、首次上传文件或 scope 新增时仍应确认。

如果用户说：

```text
直接发布。
```

风险升级：

```json
{
  "intent": "publish_social_content",
  "target": "xiaohongshu",
  "risk": "high",
  "requires_user_confirm": true,
  "confirmation_policy": "confirm_every_time"
}
```

#### 6.4.1 路由决策原则

- 选择满足目标语义且风险最低的可用路径；
- 正式合作接口通常优先，但用户可禁止某个 Provider 或指定只用本地浏览器；
- 不因接口更快而扩大授权 scopes；
- 不在不同语义之间静默降级，例如“正式发布失败”不能自动降级成网页点击发布；
- Fallback 前重新评估权限、确认策略和数据流；
- 如果 fallback 会改变内容、账号、费用、可见性或审计主体，必须通知用户；
- 高风险任务不能因 API 失败而自动转网页执行；
- 所有路由决策应产生可解释的 `route_reason` 和审计事件。

#### 6.4.2 路由结果示例

```json
{
  "route_id": "route_01",
  "capability": "social.create_draft",
  "provider": "xiaohongshu",
  "selected_path": "partner_api",
  "connector_version": "1.2.0",
  "risk": "medium",
  "required_scopes": ["draft.create", "media.upload"],
  "confirmation": "first_use",
  "fallbacks": [
    {
      "path": "site_skill",
      "allowed": true,
      "requires_reconfirm": true
    },
    {
      "path": "web_automation",
      "allowed": false,
      "reason": "user_disabled_exploration_for_this_site"
    }
  ],
  "route_reason": "official_connector_available_and_authorized"
}
```

### 6.5 Capability Registry：能力目录

系统需要维护一个能力目录，用于描述每个合作方、网站、连接器和技能的可用能力。

示例：

```json
{
  "provider": "xiaohongshu",
  "display_name": "小红书",
  "trust_level": "verified_partner",
  "auth": {
    "type": "oauth",
    "scopes": ["draft.create", "media.upload", "profile.read"]
  },
  "capabilities": [
    {
      "id": "create_draft",
      "name": "创建图文草稿",
      "level": "partner_api",
      "risk": "medium",
      "requires_auth": true,
      "requires_user_confirm": false,
      "fallbacks": ["site_skill", "web_automation"]
    },
    {
      "id": "publish_note",
      "name": "发布笔记",
      "level": "partner_api",
      "risk": "high",
      "requires_auth": true,
      "requires_user_confirm": true,
      "fallbacks": ["site_skill"],
      "policy": "confirm_every_time"
    }
  ]
}
```

能力目录至少记录：

- Provider ID 与显示名称；
- Provider 身份、信任级别和接入状态；
- API/MCP 地址和协议版本；
- Connector 版本、签名和发布者；
- 授权方式与 scopes；
- Capability 列表；
- 输入参数、输出结果和错误模型；
- 风险等级与外部影响；
- 是否需要用户确认；
- Fallback 策略与语义差异；
- Rate Limit、配额、费用和幂等性；
- 数据分类、数据驻留和保留策略；
- 审计要求和日志主体；
- 健康状态、最近验证时间和兼容版本；
- 是否支持投屏 Manifest；
- 是否支持广告、正片和下一集；
- 是否支持曝光、点击和播放回传。

#### 6.5.1 能力来源与信任等级

建议区分：

```text
built_in
verified_partner
enterprise_private
verified_site_skill
personal_site_skill
experimental_web_automation
```

外部 Agent 应能看到能力来源和风险，但不能获得第三方 Token 或内部敏感连接信息。

#### 6.5.2 Registry 生命周期

```text
draft → review → active → degraded → suspended → revoked → archived
```

- Schema 或权限变化必须生成新版本；
- Connector 被撤销后立即停止新调用并撤销相关授权；
- 降级时路由器应评估 fallback，而不是无限重试；
- 安全事件可触发全局 Kill Switch；
- 缓存的 Registry 必须有签名、版本和过期时间。

### 6.6 对外统一 MCP Server

蜡笔 AI 投屏浏览器可以对外暴露统一 MCP Server。外部 Agent 不需要分别对接内容平台、课程平台、DLNA 和蜡笔接收端，而是统一调用蜡笔能力。

对外工具示例：

```text
capability.search
capability.describe
capability.get_route_preview

content.extract_page
content.export_markdown
content.create_report

social.create_draft
social.upload_media
social.publish_with_confirm

video.detect
video.get_metadata
video.get_playlist
video.get_subtitles

cast.discover_devices
cast.play
cast.play_playlist
cast.next_episode
cast.stop

tv.show_product_panel
tv.show_reading_card
tv.play_manifest
```

内部由 Capability Router 决定具体走：

```text
合作 API / MCP
站点技能
网页自动化
人工接管
```

这种方式可以让蜡笔成为：

> **Agent 调用网站能力和大屏能力的统一入口。**

统一 MCP Server 仍须遵循本 PRD 的 MCP 设计原则：

- 工具声明只读性、幂等性、风险和外部影响；
- 高风险调用由本地权限内核强制确认；
- Partner 返回的数据被视为不可信输入，不可提升为系统指令；
- 第三方 MCP 的工具描述、资源和提示必须经过命名空间隔离与安全过滤；
- Agent 只得到任务所需的最小结果，Token 和连接器密钥永不暴露；
- 长任务返回任务 ID，支持查询、取消和审计；
- 对外工具保持意图级稳定，Provider 差异封装在 Connector Adapter 内。

### 6.7 合作网站可开放的能力类型

#### 6.7.1 内容、短视频与图文平台

可开放：

- 创建图文草稿；
- 创建视频草稿；
- 上传图片；
- 上传视频；
- 读取草稿列表；
- 发布前预览；
- 发布内容；
- 读取评论；
- 读取数据分析；
- 获取授权视频播放信息；
- 生成投屏播放列表。

建议策略：

- 创建草稿可在用户授权策略内自动执行；
- 正式发布必须每次确认；
- 删除、账号设置、私信等高风险动作默认不自动化。

#### 6.7.2 视频、课程与知识平台

可开放：

- 课程目录；
- 视频列表；
- 视频播放 Manifest；
- 字幕；
- 章节；
- 学习进度；
- 下一集；
- 播放状态回传；
- 字幕摘要；
- 课程笔记；
- 大屏播放授权。

适合场景：

```text
AI 帮用户打开课程
→ 提取课程大纲
→ 生成学习笔记
→ 投到电视
→ 自动下一集
→ 同步学习进度
```

#### 6.7.3 电商与直播平台

可开放：

- 商品信息；
- 直播间信息；
- 商品列表；
- 商品卡片；
- 优惠信息；
- 商品讲解片段；
- 直播投屏 Manifest；
- 曝光回传；
- 点击回传；
- 扫码购买链接。

安全要求：

- 下单、支付、退款、地址修改必须用户确认；
- 不允许 Agent 自动付款；
- 不允许绕过平台交易链路；
- 电视端只展示明确授权的数据和跳转入口；
- 购买行为回到平台控制的安全终端完成。

#### 6.7.4 企业系统

可开放：

- 文档读取；
- 报告生成；
- 工单创建；
- 会议纪要；
- 内部知识库；
- 培训视频；
- 企业大屏展示；
- 权限审计；
- 私有模型接口。

适合企业版：

```text
企业内网 MCP/API 聚合
企业知识网页转 Markdown
企业会议室投屏
企业 Agent 工作流
```

企业 Provider 还需支持租户隔离、管理员策略、数据驻留、私有网络和组织级审计。

#### 6.7.5 自媒体后台

可开放：

- 创建公众号草稿；
- 创建小红书草稿；
- 创建抖音草稿；
- 创建 B 站稿件草稿；
- 上传素材；
- 生成封面；
- 发布前预览；
- 数据读取；
- 评论摘要。

原则：

```text
保存草稿：可授权自动执行
正式发布：每次用户确认
删除 / 修改账号信息：默认不自动化
```

### 6.8 投屏 Manifest 合作能力

合作方如果只提供公开视频 URL，普通 DLNA 也可以播放，但能力较弱。蜡笔接收端可以支持更高级、正式授权的 `TV Cast Manifest`。

示例：

```json
{
  "manifest_version": "1.0",
  "title": "课程第 1 讲",
  "provider": "example_course",
  "session_id": "session_001",
  "expires_at": "2026-01-01T12:00:00Z",
  "playlist": [
    {
      "type": "ad",
      "url": "https://cdn.example.com/ad.m3u8",
      "duration": 15,
      "skippable": false,
      "tracking": {
        "impression": "https://api.example.com/ad/impression",
        "complete": "https://api.example.com/ad/complete"
      }
    },
    {
      "type": "main",
      "url": "https://cdn.example.com/lesson1.m3u8",
      "title": "第 1 讲：课程介绍",
      "subtitles": "https://cdn.example.com/lesson1.vtt",
      "tracking": {
        "start": "https://api.example.com/video/start",
        "progress": "https://api.example.com/video/progress",
        "complete": "https://api.example.com/video/complete"
      }
    }
  ],
  "next": {
    "title": "第 2 讲：基础概念",
    "action": "provider.course.next"
  },
  "tv_template": {
    "type": "course_player",
    "show_outline": true,
    "show_notes": true
  }
}
```

普通 DLNA：

```text
播放当前设备支持的 main URL，并提供基础控制。
```

蜡笔接收端：

```text
授权广告 → 正片 → 字幕 → 下一集
→ 合规播放回传 → 大屏模板 → 课程笔记
```

这是蜡笔接收端相比普通 DLNA 的核心差异化。

#### 6.8.1 Manifest 安全要求

- Manifest 必须版本化、具有 Provider 身份和短期有效期；
- 生产环境建议由合作方签名，接收端校验签名、受众和会话；
- Tracking URL 必须使用 HTTPS，并通过 Provider 域名或显式允许列表校验；
- 不允许 Manifest 指示接收端访问任意局域网地址、文件地址或管理端点；
- 不把 OAuth Token、Cookie、会员凭证或 DRM License 明文写入 Manifest；
- 回传遵循用户同意、数据最小化、频率限制和隐私政策；
- 下一集 Action 由发送端或可信 Provider Connector 解析，不让接收端任意调用外部工具；
- Manifest 过期、签名失败或能力不兼容时安全停止或降级，不静默绕过保护；
- 普通 DLNA 降级仅在内容授权和媒体 URL 允许直接播放时进行。

### 6.9 合作方价值

对合作方不能只讲“开放接口给我们”，而要强调增量价值。

#### 6.9.1 对内容平台

价值：

- 增加大屏播放入口；
- 保留广告和正片完整链路；
- 保留会员控制；
- 保留播放统计；
- 支持自动下一集；
- 支持 AI 搜索、摘要和推荐；
- 减少用户转向非官方网页解析方式的动机。

对外表达：

```text
我们不是绕过你的平台，而是把你的授权内容能力 AI 化、大屏化。
```

#### 6.9.2 对教育与课程平台

价值：

- 网页课程扩展为电视学习场景；
- 支持课程目录；
- 支持字幕摘要；
- 支持学习笔记；
- 支持家庭或会议室大屏播放；
- 支持学习进度同步；
- 支持 AI 学习助手。

#### 6.9.3 对自媒体平台

价值：

- AI 帮用户创建草稿，提高创作效率；
- 平台仍控制发布、审核和风控；
- 降低网页自动化产生的异常操作；
- 通过官方接口提供更稳定的 Agent 创作体验。

#### 6.9.4 对电商与直播平台

价值：

- 直播扩展到电视大屏；
- 商品区可以在电视端展示；
- 用户通过手机扫码进入平台交易链路；
- 曝光、点击和购买链路仍回到平台；
- 支持内容方授权的广告和商品回传。

### 6.10 授权与安全策略

能力聚合中心必须从第一版开始使用统一权限体系，不能让 Partner Connector 绕过 Browser Agent Gateway、Risk Guard 或审计层。

#### 6.10.1 用户授权

每个 Provider 单独授权，每项授权采用最小 scope。

示例：

```text
小红书：允许创建草稿，不允许自动发布
B 站：允许读取视频信息，不允许删除稿件
公众号：允许创建草稿，发布需每次确认
电商：允许读取商品，不允许自动下单
课程平台：允许读取课程目录和同步播放进度
```

用户应能：

- 查看每个 Provider 获得的 scopes；
- 查看最近使用时间和调用记录；
- 单独撤销某个 scope 或整个 Provider；
- 禁止某个任务使用网页自动化 fallback；
- 选择是否允许数据发送给云端模型；
- 指定允许控制的投屏设备。

#### 6.10.2 动作风险分级

| 动作 | 风险 | 策略 |
|---|---:|---|
| 读取网页或公开信息 | Low | 可自动 |
| 生成 Markdown | Low | 可自动 |
| 读取课程目录 | Low | 可自动 |
| 检测视频 | Low | 可自动 |
| 创建草稿 | Medium | 首次授权或按策略确认 |
| 上传文件 | Medium | 首次授权或每次确认 |
| 投屏播放 | Medium | 首次授权或设备选择确认 |
| 正式发布内容 | High | 每次确认 |
| 删除内容 | High | 每次确认 |
| 修改账号设置 | Critical | 不自动化 |
| 支付或下单 | Critical | 用户手动确认并完成关键步骤 |
| 账号安全验证 | Critical | 用户手动处理 |

风险等级不能由合作方自行降低。Provider 可以提出风险提示，但最终等级由蜡笔本地策略取双方中更严格者。

#### 6.10.3 Token 与凭证管理

要求：

- 不把第三方 Token 暴露给 Agent；
- 不把 Cookie 或 Token 传给电视端；
- Token 使用系统安全存储或企业密钥设施加密保存；
- 支持撤销授权；
- 支持按 Scope 授权；
- 支持到期刷新和轮换；
- 刷新失败时停止，不回退到抓取凭证；
- 日志中不记录 Token、Authorization Header 或刷新凭证；
- 高风险动作每次用户确认；
- 企业版支持管理员控制、租户隔离和密钥审计；
- Connector 只能获得执行单项能力所需的最小短期凭证。

#### 6.10.4 Connector 安全

- Provider Adapter 运行在受限边界中；
- 使用域名允许列表、超时、重试上限、响应大小上限和速率限制；
- 防止 SSRF、任意重定向、恶意下载和本地网络探测；
- 对输入输出进行 Schema 校验和内容类型校验；
- MCP 工具、资源和提示全部视为不可信数据；
- Connector 更新必须签名、版本化并支持撤回；
- 第三方故障不得阻塞整个 Browser Runtime；
- 安全事件触发 Provider 级或全局 Kill Switch。

### 6.11 Web 自动化与合作接口的关系

网页自动化不是低级方案，而是覆盖长尾网站的兜底能力。

完整能力栈：

```text
合作接口：稳定、高效、合规边界清楚、可商业化
站点技能：适合高频网站，具备复用价值
网页自动化：覆盖长尾网站
人工接管：处理登录、验证码、风控和敏感步骤
拒绝执行：处理越权、绕过、非法或无法安全完成的任务
```

默认执行优先级：

```text
Partner API / MCP
↓
Site Skill
↓
Web Automation
↓
Human Handoff
↓
拒绝执行
```

但路由不是机械降级链。每次切换路径都要重新校验：

- 目标语义是否完全一致；
- 用户授权是否覆盖新路径；
- 风险和确认策略是否改变；
- 数据将流向哪些主体；
- 是否违反站点或合作方约束；
- 已执行步骤是否会导致重复副作用。

### 6.12 版本规划

#### V1：本地能力路由雏形

- 定义 Capability Router；
- 定义 Capability Registry JSON/Schema；
- 支持内部能力注册；
- 支持网页转 Markdown、视频检测、投屏播放等内部 Capability；
- Agent 可通过 CLI/MCP 调用统一能力；
- 路由结果包含风险、来源和解释。

#### V2：Site Skill 与 Capability Registry 打通

- Workflow Recipe 注册为 Capability；
- 创建草稿等技能进入能力目录；
- 支持 Fallback；
- 支持风险等级；
- 支持用户授权与确认；
- 技能来源、版本和健康度进入路由决策。

#### V3：Partner Connector 框架

- 定义合作方接入规范；
- 支持 OAuth/API Key；
- 支持 Provider Adapter；
- 支持 Provider Capability Schema；
- 支持审计日志；
- 支持 Token 安全存储；
- 支持 Connector 签名、健康检查、撤销和 Kill Switch。

#### V4：TV Cast Manifest

- 定义蜡笔高级投屏 Manifest；
- 支持授权广告与正片；
- 支持自动下一集；
- 支持字幕；
- 支持播放状态回传；
- 支持大屏模板；
- 支持普通 DLNA 安全降级；
- 支持签名、有效期、回传允许列表和版本协商。

#### V5：开放 Partner Capability Hub

- 对外提供合作方接入文档；
- 对外提供蜡笔统一 MCP Server；
- 允许第三方 Agent 调用蜡笔能力；
- 支持企业私有 Provider；
- 支持内容平台、课程平台、电商直播和自媒体后台接入；
- 建立合作方审核、灰度、监控、计量和退出机制。

### 6.13 商业与运营能力

Partner Hub 后续需要考虑：

- Provider 接入审核与技术认证；
- Sandbox、测试账号和验收用例；
- SLA、配额、计量、费用和结算；
- 版本兼容和弃用周期；
- 能力健康度与告警；
- 用户投诉、数据请求和授权撤销；
- 安全事件响应和紧急停用；
- 合作方数据报表与隐私边界；
- 按地区、行业和租户控制能力可用性。

P0 阶段不必实现完整商业后台，但数据模型应为 Provider、租户、计量和审计预留稳定标识。

### 6.14 成功指标

建议跟踪：

- 通过 Partner API/MCP 完成的任务比例；
- 通过 Site Skill 和 Web Automation 完成的任务比例；
- 各路径成功率、耗时和 Token 消耗；
- 路由 fallback 发生率和重新确认率；
- Provider 授权转化率与撤销率；
- Connector 错误率、健康度和平均恢复时间；
- 高风险动作确认覆盖率；
- 投屏 Manifest 首帧时间、续播率和回传成功率；
- 普通 DLNA 安全降级成功率；
- 因权限、安全或合规被正确拒绝的任务数量。

### 6.15 核心价值总结

Partner Capability Hub 的价值在于：

```text
有接口时，不走网页；
无接口时，走技能；
无技能时，走自动化；
遇到验证时，人接管；
需要大屏时，走蜡笔投屏 Runtime；
不满足权限或合规要求时，明确拒绝。
```

最终蜡笔 AI 投屏浏览器不是普通浏览器，而是：

> **AI Agent 调用网站能力、网页能力和大屏能力的统一入口。**

这会形成三层壁垒：

1. **能力聚合壁垒**：合作网站越多，Agent 越愿意接入；
2. **Workflow 数据壁垒**：在用户授权和隐私保护下，站点技能越丰富，操作越稳定；
3. **投屏 Runtime 壁垒**：普通 DLNA 负责兼容播放，蜡笔接收端支持授权广告、正片、下一集、模板、回传和交互。

---

## 7. 核心模块

### 7.1 Page Understanding：页面理解层

页面理解层负责把网页转成 Agent 友好的结构化信息。

标准输出包括：

- `Page Snapshot`：页面类型、标题、URL、登录状态、主要区域和当前业务状态；
- `Readable Markdown`：去除噪声后的正文；
- `Action Map`：当前可执行动作；
- `Form Map`：表单、字段、校验、必填状态；
- `Media Map`：视频、音频、字幕、剧集和播放状态；
- `Risk Map`：每个动作或数据对象的风险级别；
- `Change Set`：相较上一快照发生变化的部分。

### 7.2 Agent Snapshot：低 Token 页面快照

Agent Snapshot 应优先返回业务语义和差异，而不是默认返回完整 DOM。

示例：

```json
{
  "snapshot_id": "snap_01",
  "page_type": "creator_publish_editor",
  "title": "发布笔记",
  "url": "https://creator.example.com/publish",
  "state": {
    "logged_in": true,
    "editor_open": true,
    "has_unsaved_content": false
  },
  "forms": ["note_editor"],
  "actions": [
    "upload_media",
    "save_as_draft",
    "publish_now"
  ],
  "risk": {
    "save_as_draft": "medium",
    "publish_now": "high"
  },
  "content_summary": "当前页面为图文发布编辑器。"
}
```

应支持三种读取级别：

- `compact`：状态、主要动作和关键内容，默认给 Agent；
- `standard`：增加表单字段、页面区域和媒体信息；
- `full`：仅在调试、探索或自愈时返回详细 DOM/ARIA/视觉线索。

### 7.3 Action Runtime：动作执行层

执行层提供稳定动作，不让上层 Agent 依赖易变的 selector。

职责包括：

- 生成和维护 Action Map；
- 动作前置条件检查；
- 风险策略与用户确认；
- 多定位策略执行；
- 等待页面状态变化；
- 验证动作效果；
- 失败时生成可诊断结果；
- 将执行轨迹发送给 Workflow Learning。

### 7.4 Workflow Learning：流程学习层

流程学习层把一次成功任务从“操作轨迹”转化为“可参数化技能”。

完整生命周期：

```text
探索 → 记录 → 验证成功 → 归纳参数 → 生成 Recipe
→ 用户确认保存 → 沙盒复验 → 发布技能版本
→ 后续复用 → 监控漂移 → 受控自愈 → 新版本
```

### 7.5 Cast Runtime：投屏执行层

投屏执行层统一提供设备发现、能力协商、播放任务、状态同步和播放队列。

两类接收端：

- 普通 DLNA：最大化兼容，提供单 URL 播放及基础控制；
- 蜡笔接收端：支持会话清单、播放队列、广告/正片编排、字幕、剧集、下一集预加载和完整状态回传。

---

## 8. 网页转 Markdown

### 8.1 功能范围

- 提取标题、作者、发布时间和正文；
- 保留标题层级、段落、列表、引用和分隔线；
- 保留表格、代码块、公式和必要的脚注；
- 将相对链接和图片地址规范化为可追溯链接；
- 去除导航、广告位、浮层、评论、相关推荐和重复元素；
- 标注内容来源 URL、提取时间和页面标题；
- 支持选区提取、当前区域提取和全文提取；
- 支持多标签页合并、去重和来源引用；
- 支持从 Markdown 继续生成摘要、知识卡片或大屏阅读页面。

### 8.2 输出要求

Markdown 文档至少包含：

```yaml
---
title: 页面标题
source_url: https://example.com/page
captured_at: 2026-01-01T10:00:00+08:00
author: 可选
content_hash: 可选
---
```

正文后可选附加：

- 原始链接列表；
- 图片资源列表；
- 表格导出链接；
- 页面摘要；
- 提取警告，例如“页面内容可能未完全加载”。

### 8.3 质量策略

- 先使用确定性正文提取算法和 DOM 语义；
- 大模型用于纠错、结构优化和摘要，不应默认重写原文；
- “忠实提取”和“AI 改写”必须是两个明确模式；
- 提取结果保留来源，不将模型生成内容伪装为原网页内容；
- 对动态加载页面，允许等待稳定、滚动加载或由用户指定区域；
- 涉及版权内容时，默认面向用户个人处理，不建立公开内容库。

---

## 9. Action Map 与稳定动作模型

### 9.1 设计目标

Agent 不应调用“点击坐标 120,80”或长期依赖某个 CSS class，而应调用业务动作：

```text
browser.click_action("save_as_draft")
```

Action Runtime 再根据当前页面状态选择最可靠的定位方式。

### 9.2 Action 数据结构

```json
{
  "action_id": "save_as_draft",
  "label": "保存草稿",
  "role": "button",
  "description": "保存当前编辑内容但不公开发布",
  "risk": "medium",
  "idempotency": "conditionally_idempotent",
  "preconditions": [
    "user_logged_in",
    "editor_open",
    "editor_has_content"
  ],
  "effects": ["draft_saved"],
  "locators": [
    {"type": "test_id", "value": "save-draft", "weight": 1.0},
    {"type": "aria", "value": "保存草稿", "weight": 0.9},
    {"type": "text", "value": "保存草稿", "weight": 0.8},
    {"type": "css", "value": "button.save-draft", "weight": 0.6}
  ],
  "verify": {
    "type": "state",
    "condition": "draft_saved_successfully"
  },
  "confidence": 0.94
}
```

### 9.3 定位策略优先级

建议综合以下信号，而不是固定只用一种 selector：

1. 站点稳定标识，如 `data-testid`；
2. Accessibility role 与可访问名称；
3. 表单 label、placeholder 和字段关系；
4. 文本与语义相似度；
5. DOM 结构和邻近元素；
6. 页面区域和视觉位置；
7. 截图视觉匹配，仅作为兜底；
8. 历史成功记录和当前页面版本特征。

### 9.4 动作执行结果

每次执行返回：

- 是否成功；
- 实际使用的定位策略；
- 执行前后状态；
- 是否触发确认；
- 验证结果；
- 页面变化摘要；
- 可重试性；
- 失败原因和建议恢复路径；
- 审计事件 ID。

---

## 10. Workflow Learning 与流程沉淀

### 10.1 Trace Recorder

首次探索时记录：

- URL pattern、页面标题和页面类型；
- 页面关键状态及状态变化；
- 动作 ID、定位线索和实际目标；
- 表单字段语义及填充值的参数占位符；
- 用户确认点；
- 页面等待条件；
- 成功与失败信号；
- 异常分支及恢复动作；
- 敏感信息类别，但不记录密码、验证码、Cookie、Token 原值。

### 10.2 Workflow Recipe

示例：

```yaml
workflow_id: xiaohongshu.create_draft
name: 创建小红书图文草稿
site: xiaohongshu.com
version: 1.0.0
url_patterns:
  - "https://creator.xiaohongshu.com/**"
inputs:
  title:
    type: string
    required: true
  content:
    type: string
    required: true
  images:
    type: file[]
    required: false
  tags:
    type: string[]
    required: false
preconditions:
  - browser_session_available
  - user_logged_in
steps:
  - action: navigate
    url: "https://creator.xiaohongshu.com"
  - action: ensure_state
    state: creator_home_ready
  - action: click_action
    target: open_publish_editor
  - action: upload
    target: media_uploader
    value: "{{images}}"
    when: "images.length > 0"
  - action: fill
    target: title_input
    value: "{{title}}"
  - action: fill
    target: content_editor
    value: "{{content}}"
  - action: fill_tags
    target: tags_input
    value: "{{tags}}"
  - action: click_action
    target: save_as_draft
  - action: verify
    condition: draft_saved_successfully
risk:
  level: medium
  confirmation: first_run_or_policy
success_output:
  - draft_id
  - draft_url
  - saved_at
```

以上仅为产品数据模型示例，项目实现时应根据现有架构选择 JSON、YAML、数据库或 Rust 类型。

### 10.3 从轨迹生成技能的规则

- 只有任务被可靠验证为成功后，才能生成候选 Recipe；
- 默认由用户确认是否保存，不自动沉淀所有浏览行为；
- 自动识别变量输入与固定路径，避免把具体文案和文件写死；
- 密码、验证码、Cookie、Authorization、支付信息不得写入 Recipe；
- 发布、删除、付款等高风险确认点不得在学习时被自动移除；
- 保存后先标记为 `draft`，经过复验后再变为 `verified`；
- 每次修改生成新版本，保留回滚能力和变更说明。

### 10.4 自愈机制

当原定位失效时：

1. 停止当前动作，不盲目点击相似目标；
2. 判断页面是否处于预期页面类型和状态；
3. 根据 ARIA、文本、结构、视觉、上下文和历史记录计算候选；
4. 对候选执行置信度评分；
5. 低风险动作且高置信度时允许自动尝试；
6. 中风险动作需要更严格阈值，并在策略要求时确认；
7. 高风险动作禁止自动改写目标，必须用户确认；
8. 验证动作结果；
9. 成功后创建候选修订版本，不静默覆盖已验证版本。

### 10.5 漂移与降级

出现以下情况应降级到探索模式：

- 页面类型无法确认；
- 关键前置状态不满足；
- 多个候选动作置信度接近；
- 页面出现验证码、风控或异常登录；
- 高风险动作的目标或效果发生变化；
- 连续两次验证失败；
- 站点条款、权限或合规策略发生变化。

---

## 11. 站点技能库

### 11.1 技能层级

- **内置通用技能**：网页转 Markdown、提取表格、页面搜索、识别视频；
- **官方站点技能**：由项目团队维护和签名发布；
- **用户个人技能**：由用户自己的成功任务沉淀，仅在本机或用户私有同步空间使用；
- **企业技能**：由企业管理员维护，面向企业站点和权限体系；
- 第三方公共技能市场不进入早期范围，需单独评估供应链与合规风险。

### 11.2 技能元数据

每个技能至少记录：

- `skill_id`、名称、站点和用途；
- 输入、输出和前置条件；
- URL 匹配规则与页面指纹；
- 风险等级与确认策略；
- 是否需要登录、文件上传或用户在场；
- 技能版本、作者、来源和签名；
- 最近验证时间、成功率和失败原因分布；
- 支持的平台/WebView 版本；
- 自愈记录和回滚版本；
- 数据访问范围与保留策略。

### 11.3 技能状态

```text
draft → verified → active → degraded → disabled → archived
```

- `draft`：刚从轨迹生成，尚未复验；
- `verified`：已在明确环境中复验；
- `active`：允许正常匹配执行；
- `degraded`：成功率下降，优先进入辅助执行；
- `disabled`：因安全、合规或严重失效被停用；
- `archived`：保留历史但不再使用。

### 11.4 技能匹配

匹配时综合：

- 站点域名和 URL pattern；
- 页面类型与关键状态；
- 页面结构指纹；
- 用户意图和输入参数；
- 当前平台、WebView 版本和登录状态；
- 技能健康度与风险策略。

不可仅凭域名命中后直接执行。

---

## 12. MCP 接口规划

### 12.1 设计原则

- MCP 是对 Agent 暴露能力的标准入口，内部仍使用统一 Browser Agent API；
- 工具名称稳定，易变的 DOM 细节由浏览器运行时封装；
- 每个工具声明只读性、幂等性、风险、是否可能产生外部影响；
- 默认返回结构化摘要和资源引用，避免返回超大 DOM；
- 高风险调用不能仅依赖模型自觉，必须由执行层强制确认；
- MCP 客户端不得直接读取 Cookie、密码、验证码或授权令牌。

### 12.2 页面读取工具

```text
browser.get_context
browser.get_tabs
browser.get_snapshot
browser.extract_markdown
browser.extract_links
browser.extract_images
browser.extract_tables
browser.get_selected_text
browser.search_in_page
browser.screenshot
```

关键参数建议：

- `tab_id`；
- `detail: compact | standard | full`；
- `scope: selection | main | full_page`；
- `include_sources`；
- `max_chars` 或分页游标。

### 12.3 页面操作工具

```text
browser.navigate
browser.go_back
browser.reload
browser.open_tab
browser.close_tab
browser.list_actions
browser.click_action
browser.fill_form
browser.upload_file
browser.scroll
browser.wait_for_state
browser.request_user_intervention
```

`browser.request_user_intervention` 用于登录、验证码、设备授权或 Agent 不应代办的敏感步骤。

### 12.4 Workflow 与技能工具

```text
workflow.start_recording
workflow.stop_recording
workflow.create_candidate
workflow.validate
workflow.save
workflow.run
workflow.get_status
workflow.cancel
workflow.list
workflow.get
workflow.rollback
workflow.disable
workflow.explain_failure
```

高风险要求：

- `workflow.save` 应显示将保存的输入、步骤、权限和确认点；
- `workflow.run` 执行前返回匹配版本、风险和所需确认；
- `workflow.rollback` 与 `workflow.disable` 应记录审计日志。

### 12.5 视频和投屏工具

```text
video.detect
video.get_candidates
video.get_metadata
video.get_subtitles
video.get_episode_list
cast.discover_devices
cast.get_device_capabilities
cast.create_session
cast.play
cast.pause
cast.resume
cast.seek
cast.set_volume
cast.enqueue
cast.next
cast.stop
cast.get_status
```

### 12.6 文档与模型工具

```text
doc.export_markdown
doc.export_pdf
doc.save_to_library
doc.cast_as_reading_cards
ai.summarize_page
ai.ask_page
ai.transform_content
ai.compare_tabs
```

其中“忠实提取”应由 `browser.extract_markdown` 完成；`ai.transform_content` 是明确的生成式改写，两者不得混淆。

---

## 13. CLI 接口规划

CLI 与 MCP 应共享同一业务 API 和权限模型，避免出现两套行为。

示例命令仅用于定义体验，具体名称以项目现有 CLI 规范为准：

```text
get-video browser tabs
get-video browser snapshot --detail compact
get-video browser markdown --scope main --output page.md
get-video browser actions
get-video browser click save_as_draft
get-video workflow record start
get-video workflow record stop
get-video workflow save xiaohongshu.create_draft
get-video workflow run xiaohongshu.create_draft --input input.json
get-video skills list
get-video skills inspect xiaohongshu.create_draft
get-video cast devices
get-video cast play --candidate video_01 --device living_room
```

CLI 要求：

- 默认输出简洁、人类可读；
- 提供 `--json` 供 Agent 和脚本调用；
- 长任务返回 `task_id`，支持查询和取消；
- 不在日志中显示密码、Cookie、Authorization 或视频授权 Token；
- 高风险动作不能用一个全局 `--yes` 永久绕过确认；
- 自动化环境中应使用细粒度授权策略或一次性批准令牌；
- exit code 稳定，并给出机器可读错误类型。

---

## 14. 大模型能力与配置

### 14.1 模型使用场景

- 网页摘要、问答和多页对比；
- 正文结构修复与内容分类；
- 根据用户要求改写为草稿、报告或学习卡片；
- 首次探索时理解页面语义；
- 将成功轨迹归纳为候选 Workflow；
- selector 失效时对候选目标进行语义匹配；
- 视频标题、简介、剧集和字幕摘要；
- 用自然语言控制投屏会话。

### 14.2 模型不应独立决定的事项

- 是否绕过 DRM、登录、会员或付费限制；
- 是否发送消息、发布内容、删除数据或付款；
- 是否读取或导出账号凭证；
- 是否把一次探索静默保存为长期技能；
- 是否跳过权限和风险检查。

这些必须由确定性策略层约束。

### 14.3 模型配置

建议支持：

- 产品默认模型；
- 用户自定义兼容接口和 API Key；
- 本地模型隐私模式；
- 企业私有模型 Endpoint；
- 按任务类型选择模型，如提取、推理、视觉和生成；
- 明确显示哪些网页内容会发送给模型；
- 支持“仅本地提取，不上传正文”模式。

凭证必须使用系统安全存储，不写入普通配置、Workflow 或日志。

---

## 15. 投屏能力规划

### 15.1 普通 DLNA 接收端

目标是“广泛兼容、能力保守”。建议支持：

- 设备发现与能力探测；
- 播放单个公开媒体 URL；
- 播放、暂停、停止、进度和音量控制；
- 标题、封面、时长等基础元数据；
- 对队列和下一集采用发送端编排：上一条结束后再发送下一条；
- 明确处理设备不支持的编码、封装、字幕或 HTTPS 能力。

普通 DLNA 不应假设支持复杂会话清单、广告编排或可靠的双向状态。

### 15.2 蜡笔投屏接收端

蜡笔接收端支持高级 `CastSessionManifest`：

```json
{
  "session_id": "session_123",
  "title": "课程第一集",
  "items": [
    {
      "id": "ad_01",
      "type": "ad",
      "url": "https://authorized.example/ad.m3u8",
      "skippable": false
    },
    {
      "id": "main_01",
      "type": "main",
      "url": "https://authorized.example/ep1.m3u8",
      "title": "第一集"
    }
  ],
  "next": {
    "content_id": "ep2",
    "title": "第二集"
  },
  "policy": {
    "autoplay_next": true,
    "report_playback_state": true
  }
}
```

高级能力包括：

- 广告、正片、片尾的授权编排；
- 播放列表、自动下一集和下一集预加载；
- 字幕、章节、清晰度和多音轨；
- 播放状态、错误和完成事件回传；
- 大屏阅读卡片、课程目录或商品二维码等模板；
- Agent 对播放队列的自然语言控制。

### 15.3 广告与正片边界

- 仅支持内容方授权或用户自有内容的广告/正片编排；
- 不从第三方平台拆解、替换、跳过或重新拼装其广告；
- 不把绕过原网页播放器、统计、风控或广告机制作为产品能力；
- 曝光、播放完成等回传需有明确授权、目的说明与隐私策略；
- 对网页原生平台视频，优先使用官方投屏或系统镜像。

### 15.4 自动下一集来源

下一集可来自：

- 用户手动创建的播放列表；
- 用户自有 NAS、企业或教育内容目录；
- 合作内容方提供的正式接口或 Manifest；
- 网页中明确展示且符合合规策略的剧集列表，经用户确认后生成队列。

不得通过绕过平台保护或未授权接口获取会员剧集资源。

---

## 16. 合规、安全与隐私

> 本节是产品与技术控制要求，不替代针对目标市场和具体内容源的正式法律意见。

### 16.1 核心合规原则

- 产品定位为网页任务助手和投屏工具，不是全网解析、下载或破解工具；
- 只处理用户有权访问、可合法投屏且不受禁止性技术措施保护的内容；
- 不绕过 DRM、付费墙、会员、登录限制、地区限制或平台风控；
- 不下载、缓存、保存或转存第三方受保护媒体；
- 不将用户 Cookie、Authorization、会员 Token、DRM License 或密码交给接收端；
- 解析尽量在本机完成，不建立云端视频解析服务和资源索引；
- 对第三方站点操作遵守其服务条款、robots/自动化政策及适用法律；
- 对高风险内容平台默认采用官方投屏或系统镜像。

### 16.2 动作风险分级

| 风险等级 | 示例 | 默认策略 |
|---|---|---|
| 低 | 读取页面、提取 Markdown、页面搜索 | 可自动执行并记录 |
| 中 | 填写表单、上传文件、保存草稿 | 首次或按用户策略确认 |
| 高 | 正式发布、发消息、删除、下单、付款、修改权限 | 每次明确确认 |
| 禁止 | 读取密码/Cookie/Token、绕过验证码/DRM/付费墙 | 不提供能力 |

“保存草稿”虽然不公开发布，但会写入第三方系统，应视为中风险外部写操作。

### 16.3 登录与验证码

- 允许用户在内置 WebView 中自行登录；
- 密码、验证码和二次验证由用户直接完成；
- Agent 只能获知“已登录/未登录”等状态，不获得凭证值；
- 提供临时/无痕会话和一键清除站点数据；
- 登录态只保留在浏览器沙盒中，不导出给 MCP、CLI、模型或接收端；
- 遇到验证码、风控或账号异常时暂停自动化并请求用户接管。

### 16.4 Agent 权限模型

建议采用：

```text
用户/企业策略
  × Agent 身份
  × 站点范围
  × 工具与动作
  × 风险等级
  × 时间/次数
  × 数据范围
```

授权示例：

- 允许某 Agent 在指定站点读取页面，持续 30 天；
- 允许某工作流创建草稿，但每次正式发布都要确认；
- 允许访问指定下载目录中的图片，不允许任意读取磁盘；
- 允许控制客厅电视，但不允许控制办公室设备。

### 16.5 审计与可解释性

每次 Agent 任务至少记录：

- 谁发起、使用哪个 Agent 和模型；
- 访问了哪些站点和标签页；
- 调用了哪些工具、技能和版本；
- 执行了哪些外部写操作；
- 用户在哪些节点进行了确认；
- 自动修复了什么；
- 成功、失败或中止原因；
- 敏感字段必须脱敏或完全不记录。

用户应能查看、导出和清除个人执行历史与个人技能。

### 16.6 Prompt Injection 防护

网页内容是不可信输入。浏览器必须区分：

- 用户指令；
- Agent/系统策略；
- 网页正文；
- 网页中的诱导性指令；
- 工具返回结果。

防护要求：

- 网页中的文字不得自动升级为系统指令；
- 网页要求上传文件、泄露信息或改变任务目标时必须拦截；
- 外部写操作始终经过 Risk Guard；
- 技能只访问声明的数据范围；
- 对跨站导航、下载、弹窗和新权限请求进行明确控制；
- 不因网页声称“已获授权”就绕过权限策略。

---

## 17. Challenge-Aware Agent：验证码与风控接管

### 17.1 背景

很多网站会通过验证码、滑块、点选图案、短信验证、邮箱验证、Cloudflare Turnstile、reCAPTCHA、hCaptcha、极验等方式识别自动化操作。AI 浏览器在执行任务时，不能把“自动破解验证码”作为能力，否则会进入绕过网站安全机制、反机器人机制和平台风控的高风险区域。

因此，AI 投屏浏览器不做“自动过验证码”，而是建设 **Challenge-Aware Agent**：

> AI 能识别验证码或风控状态，暂停自动化，把控制权交给用户；用户完成验证后，AI 从安全断点继续执行，并把这个人工验证节点沉淀到站点 Workflow 中。

### 17.2 产品原则

#### 17.2.1 不做的能力

明确不支持以下能力：

- AI 自动识别验证码答案；
- AI 自动选图；
- AI 自动拖动滑块通过验证；
- AI 自动模拟人类鼠标轨迹绕过风控；
- AI 自动处理 reCAPTCHA、Turnstile、hCaptcha 或极验；
- AI 自动调用打码平台；
- AI 自动规避 WebDriver、Headless 或 Bot 检测；
- AI 自动读取短信验证码、邮箱验证码或账号安全码；
- AI 以重试、切换网络、伪造设备指纹等方式规避网站挑战。

这些能力容易构成对网站反机器人、访问控制或账号安全机制的绕过，不应成为正式产品能力。

#### 17.2.2 支持的能力

支持以下合规能力：

- 识别页面进入验证码或风控状态；
- 暂停 Agent 自动操作；
- 保存最近的安全任务断点；
- 提示用户人工接管；
- 用户完成验证后，在重新检查页面状态和权限后继续任务；
- 将验证节点沉淀到 Workflow Recipe；
- 下次执行同类任务时提前预判可能的验证节点；
- 验证失败多次后停止任务，避免触发更高级别风控；
- 为用户清楚解释暂停原因、已完成步骤和继续后的下一步。

### 17.3 标准流程

```text
Agent 执行任务
↓
检测到验证码 / 滑块 / 点选图 / 登录风控 / 二次验证
↓
暂停自动化，禁止继续点击或提交
↓
保存最近安全断点
↓
提示用户人工完成验证
↓
用户完成验证
↓
浏览器重新检测页面、登录态和风险状态
↓
Agent 从断点继续执行
↓
Workflow 记录该站点的人工验证节点
```

### 17.4 用户体验设计

当检测到验证码或风控时，页面展示：

```text
网站要求人工验证

AI 已暂停操作，请你在页面中完成验证。
完成后，AI 将从当前安全断点继续执行剩余任务。
```

操作入口：

- 我已完成验证，继续执行；
- 停止任务；
- 稍后继续。

如果系统能可靠检测验证码消失、登录成功或目标页面恢复，可以提示用户后自动进入恢复检查；但正式发布、付款、删除、发消息等高风险动作仍须按照原有策略获得用户确认，不能因为用户完成验证而自动放行。

界面还应显示：

- 当前任务名称；
- 已完成步骤；
- 正在等待的验证类型；
- 验证后准备执行的下一步；
- 断点有效期；
- 是否会保留已填写但尚未提交的内容。

### 17.5 Workflow Recipe 中的验证节点

Workflow 中加入 `challenge_handoff` 节点，只记录暂停、人工接管和恢复条件，不记录“如何破解验证码”。

示例：

```json
{
  "workflow_id": "xiaohongshu.create_draft",
  "site": "xiaohongshu.com",
  "intent": "创建小红书草稿",
  "steps": [
    {
      "type": "navigate",
      "url": "https://creator.xiaohongshu.com"
    },
    {
      "type": "ensure_state",
      "state": "logged_in"
    },
    {
      "type": "challenge_handoff",
      "when": "captcha_or_risk_challenge_detected",
      "message": "请完成人机验证，完成后 AI 将继续保存草稿。",
      "resume_condition": "challenge_cleared_and_expected_page_restored",
      "max_wait_seconds": 1800
    },
    {
      "type": "click_action",
      "action_id": "open_publish_editor"
    },
    {
      "type": "fill",
      "target": "title_input",
      "value": "{{title}}"
    },
    {
      "type": "fill",
      "target": "content_editor",
      "value": "{{content}}"
    },
    {
      "type": "click_action",
      "action_id": "save_as_draft"
    },
    {
      "type": "verify",
      "condition": "draft_saved_successfully"
    }
  ]
}
```

`challenge_handoff` 至少应包含：

- 触发条件；
- 面向用户的说明；
- 恢复条件；
- 最大等待时间；
- 超时后的停止或保存策略；
- 是否需要用户主动点击继续；
- 恢复后需要重新确认的风险动作。

### 17.6 Checkpoint Resume：断点续跑

验证码出现时，可按最小必要原则保存：

- 当前 URL；
- 当前任务 ID；
- Workflow ID 与版本；
- 已完成步骤；
- 下一步待执行动作；
- 已填写表单的非敏感内容或可恢复引用；
- 已上传文件的状态和本地授权引用；
- 当前页面快照；
- Action Map；
- Form Map；
- Risk Map；
- 等待用户验证的原因；
- 断点创建时间和过期时间。

用户完成验证后，不重新执行整个任务，而是从最近安全断点继续。但恢复前必须重新检查：

- 当前域名、URL 和页面类型仍符合预期；
- 当前登录账号未发生意外切换；
- 页面未跨站跳转到未知来源；
- 待执行动作仍存在，且风险等级未提升；
- 已提交或不可逆步骤不会被重复执行；
- 页面内容和表单状态没有与断点发生冲突。

断点安全要求：

- 不保存密码、短信验证码、邮箱验证码、账号安全码、Cookie、Authorization 或会话 Token；
- 敏感表单内容尽量只留在 WebView 页面内存中，不写入持久化断点；
- 必须持久化的非敏感任务数据使用本机加密存储；
- 文件只保存受限引用，不复制到新的公共目录；
- 断点具有短期有效期，超时自动失效并清理；
- 退出登录、清除站点数据或账号发生变化时，相关断点立即失效；
- 恢复失败时停止并解释原因，不从更早步骤盲目重跑。

### 17.7 Challenge Detector：检测能力

需要识别以下类型：

- 普通图片验证码；
- 滑块验证；
- 点选图案验证；
- 文字验证码；
- 短信验证；
- 邮箱验证；
- 登录二次验证；
- Cloudflare Challenge；
- reCAPTCHA；
- Turnstile；
- hCaptcha；
- 极验；
- 平台风控拦截页；
- 账号安全确认页；
- 支付或交易二次确认页。

检测信号包括：

- DOM 关键字和组件特征；
- iframe 来源域名；
- Accessibility Tree；
- 页面文本；
- 表单结构；
- 特定脚本资源；
- 页面标题；
- URL pattern；
- 页面状态或网络跳转；
- 视觉区域识别。

检测输出不应只返回布尔值，建议包含：

```json
{
  "challenge_detected": true,
  "challenge_type": "captcha_or_risk_control",
  "confidence": 0.96,
  "risk": "high",
  "recommended_action": "human_handoff",
  "evidence": [
    "known_challenge_iframe",
    "page_text_match",
    "expected_action_missing"
  ]
}
```

检测能力仅用于暂停、解释和人工接管，不用于求解或绕过验证。

### 17.8 Risk Map 风险分级

验证码和风控事件需要进入 Risk Map：

| 场景 | 风险等级 | Agent 行为 |
|---|---:|---|
| 普通验证码 | High | 暂停，用户接管 |
| 登录验证码 | High | 暂停，用户接管 |
| 短信验证码 | Critical | 用户手动输入，Agent 不读取 |
| 邮箱验证码 | Critical | 用户手动输入，Agent 不读取 |
| 支付验证 | Critical | 暂停，每次由用户确认并操作 |
| 账号安全验证 | Critical | 不自动化，交由用户处理 |
| 验证失败多次 | Critical | 停止任务 |
| 无法确定是否为挑战页 | High | 暂停并请求用户确认页面状态 |

如果挑战出现前的请求涉及正式发布、支付、删除或发送，完成挑战后仍需重新确认该外部动作。

### 17.9 与站点技能库的结合

站点技能库可记录：

- 该站点是否经常出现验证；
- 验证通常出现在第几步；
- 用户是否需要提前登录；
- 是否需要人工接管；
- 验证后如何恢复；
- 验证完成后的成功判断条件；
- 最近触发频率和失败率；
- 建议用户在场的执行阶段。

示例：

```json
{
  "site": "xiaohongshu.com",
  "skill": "create_draft",
  "challenge_profile": {
    "may_trigger_challenge": true,
    "common_steps": [
      "login",
      "open_publish_editor",
      "save_as_draft"
    ],
    "agent_policy": "human_handoff_only",
    "resume_strategy": "continue_from_last_safe_checkpoint"
  }
}
```

技能库只记录挑战出现的概率、位置与恢复策略，不记录验证码内容、答案、验证令牌或规避方案。

### 17.10 MCP 与 CLI 接口补充

建议增加以下能力：

```text
challenge.get_status
challenge.request_handoff
challenge.resume
challenge.cancel
checkpoint.get
checkpoint.resume
checkpoint.discard
```

接口要求：

- `challenge.get_status` 只返回挑战类型、风险、状态和所需用户动作；
- `challenge.resume` 不接收验证码答案，只表示用户已完成页面内操作并请求恢复检查；
- `checkpoint.resume` 在执行任何下一步前重新验证页面、账号上下文和风险；
- MCP/CLI 不提供读取验证码控件内容、截图求解、发送到模型或打码平台的工具；
- 所有人工接管、恢复、超时和取消事件进入审计日志。

### 17.11 合规声明

产品条款、帮助中心和审核说明中应明确：

```text
本产品不提供验证码破解、滑块破解、反机器人绕过、打码平台接入或账号风控绕过能力。

当网站要求人工验证时，AI 会暂停任务并请求用户接管。用户完成验证后，AI 可在用户授权下，从最近的安全断点继续执行剩余任务。
```

### 17.12 MVP 实现建议

#### V1

- 检测常见验证码和风控页面；
- 暂停 Agent；
- 弹出用户接管提示；
- 用户点击继续后重新检查并恢复执行；
- 多次失败后停止任务。

#### V2

- 保存加密、短期、最小化的任务断点；
- 验证完成后自动定位安全恢复点；
- Workflow 中记录 `challenge_handoff` 节点；
- 增加挑战相关审计事件。

#### V3

- 站点技能库记录验证概率和常见位置；
- 执行任务前提示用户可能需要在场；
- 验证后自动定位下一步；
- 建立挑战检测的测试夹具和误报/漏报指标。

#### V4

- 企业自有站点可通过 `site_automation_policy.json` 声明测试策略；
- 仅在站点所有者明确授权的非生产测试环境中，允许使用测试专用的 challenge bypass 或测试密钥；
- 生产站点、第三方站点和真实用户账号不得使用 bypass；
- 企业策略必须包含环境标识、授权主体、有效期、审计和撤销机制。

### 17.13 核心价值

普通 Agent 遇到验证码通常会失败、反复尝试，甚至误入绕过风险。蜡笔 AI 投屏浏览器应做到：

```text
识别验证 → 暂停任务 → 用户接管 → 安全复检
→ 断点续跑 → 流程沉淀 → 下次提前提示
```

这不是绕过网站风控，而是让 AI 浏览器在真实网页任务中更稳定、更可控、更合规。

---

## 18. 版本路线

### V0：基础运行时与数据契约

目标：统一现有浏览器、解析和投屏代码的语义模型。

- 定义 Page Snapshot、Action、Form、Media、Workflow 和 CastSession 数据结构；
- 抽象 WebView 平台适配层；
- 建立 Risk Guard、权限检查和审计事件基础；
- 明确哪些现有模块可以复用，避免重复实现。

验收：数据结构、错误模型、权限边界和最小端到端链路可运行。

### V1：Agent 可读浏览器

目标：让 Agent 以低 Token 方式理解网页。

- 网页正文提取与 Markdown 导出；
- compact/standard/full Agent Snapshot；
- Action Map 与 Form Map；
- 视频候选检测和 Media Map；
- MCP/CLI 只读工具；
- 基础模型摘要与网页问答。

验收：Agent 可以读取页面、理解主要动作、导出 Markdown，而不依赖全量截图循环。

### V2：安全动作执行

目标：让 Agent 可控地操作网页。

- `list_actions`、`click_action`、`fill_form`、`upload_file`；
- 前置条件、效果验证和状态等待；
- 风险分级、用户确认和用户接管；
- 操作审计与敏感数据脱敏；
- 一项代表性站点任务端到端跑通。

验收：中高风险策略不能被 MCP、CLI 或模型绕过。

### V3：Workflow Learning 与技能库

目标：实现第一次探索、第二次复用。

- Trace Recorder；
- Recipe Generator；
- 用户确认保存；
- 技能版本、状态、成功率和回滚；
- 选择 1—2 个明确站点做个人技能验证，例如“保存草稿”，暂不自动正式发布。

验收：同一任务二次执行显著减少操作轮次和 Token 消耗。

### V4：受控自愈

目标：页面小幅改版后保持可用。

- 多信号候选定位；
- 页面指纹和漂移检测；
- 风险感知自愈；
- 新版本候选、复验与回滚；
- 技能健康度面板。

验收：低风险动作可在高置信度下自愈；高风险动作绝不静默更换目标。

### V5：完整投屏闭环

目标：形成网页内容到普通电视和蜡笔接收端的差异化体验。

- 普通 DLNA 基础播放和发送端队列；
- 蜡笔 CastSessionManifest；
- 自动下一集、字幕和播放状态回传；
- 授权内容的广告/正片编排；
- Markdown 大屏阅读卡片；
- MCP/CLI 投屏工具。

验收：同一套 Agent API 可根据接收端能力自动降级。

### V6：企业与生态能力

- 企业私有模型；
- 企业站点技能与管理员策略；
- 技能签名、发布、灰度和撤回；
- 合作内容源接口；
- 组织级审计、权限和数据保留；
- 在安全评审完成后，再决定是否开放第三方技能分发。

---

## 19. 建议的第一批 P0 能力

为避免范围过大，建议先完成以下最小闭环：

1. 当前标签页正文提取并生成 Markdown；
2. `compact` Agent Snapshot；
3. Action Map：按钮、链接、输入框和上传控件；
4. MCP：`get_snapshot`、`extract_markdown`、`list_actions`；
5. CLI：对应的只读命令和 `--json` 输出；
6. 用户授权后的 `click_action` 与 `fill_form`；
7. Trace Recorder 记录一次“创建并保存草稿”路径；
8. 将路径保存为个人技能并完成二次复用；
9. 公开视频候选检测；
10. 普通 DLNA 投放单个公开媒体 URL；
11. 基础 Risk Guard 和审计日志；
12. Capability Registry Schema 与内置能力注册；
13. Capability Router 的只读路由预览和可解释结果；
14. MCP/CLI 通过统一能力入口调用 Markdown、视频检测和投屏；
15. Provider、Site Skill、Web Automation 的来源和风险标识。

暂缓：

- 自动正式发布；
- 付款、删除和私信；
- 公共技能市场；
- 云端技能共享；
- 登录态视频直投；
- 第三方平台广告拆解；
- 跨所有站点的“万能自愈”。

---

## 20. 关键技术与产品决策

项目团队需要在详细设计前确认：

1. `get-video` 当前 Rust/Tauri/WebView 版本和支持平台；
2. 现有页面注入、网络观察、DLNA、接收端协议和本地服务能力；
3. Browser Agent API 是进程内 Rust API、本地 RPC，还是独立服务；
4. MCP Server 是内置启动、独立二进制，还是 CLI 子命令；
5. Workflow Recipe 的存储格式、签名和迁移方式；
6. 用户个人技能是否同步；若同步，采用何种端到端加密和冲突策略；
7. Windows WebView2 与 macOS WKWebView 能力差异如何降级；
8. 哪些站点作为 MVP 验证对象，并确认其自动化和内容使用边界；
9. 蜡笔接收端现有协议能否扩展 CastSessionManifest；
10. 产品首发地区对应的法律、应用商店与第三方服务条款审查；
11. Capability Registry 的持久化、签名、缓存和版本迁移方式；
12. Partner Connector 的信任分级、审核、沙盒、健康检查和 Kill Switch；
13. OAuth scopes、Token 安全存储、授权撤销和多租户隔离方案；
14. 路由 fallback 的语义一致性、重新确认和幂等策略；
15. TV Cast Manifest 的签名、过期、回传允许列表和 DLNA 降级契约。

---

## 21. 给 Codex / get-video 项目 AI 的执行说明

收到本文件后，请将其视为“现有 PRD 的增量输入”，不要在不了解仓库现状时直接整体重写或立即实现全部功能。

### 21.1 第一步：仓库审计

请先只读检查并输出：

- 当前产品、平台、Tauri 和 Rust 结构；
- 浏览器/WebView 创建、导航、脚本注入和事件通信位置；
- 视频识别、网页解析、DLNA、设备发现和接收端协议实现；
- 现有 API Client、OAuth、MCP Client/Server、Provider Adapter 或能力注册机制；
- 现有 CLI、MCP、本地服务、配置、数据库和日志能力；
- 已有 PRD、架构文档、合规说明和测试；
- 可复用模块、缺失能力、技术债和平台差异；
- 与本文规划冲突或重复的现有设计。

不要修改同步参考文件，不要覆盖用户未提交的改动。

### 21.2 第二步：更新主 PRD

结合实际仓库，将本文件内容合并到主 PRD，并明确：

- 现状、目标态和非目标；
- 用户流程和异常流程；
- 模块边界和依赖关系；
- MCP/CLI 的最小接口与风险注解；
- Action、Snapshot、Workflow、Skill、CastSession 数据契约；
- P0/P1/P2 优先级；
- 平台差异与降级策略；
- 可量化验收标准；
- 待确认决策和外部依赖。

若仓库现状与本文假设不一致，以“保留现有可用架构、最小增量演进”为原则，并在 PRD 中记录差异及原因。

### 21.3 第三步：形成实施计划

实施计划应按依赖顺序拆分为可验证的小任务，建议顺序：

1. 统一数据模型和错误模型；
2. Capability Registry 与只读路由预览；
3. Page Snapshot 与 Markdown；
4. Action Map 与只读 MCP/CLI；
5. 安全动作执行与确认；
6. Trace Recorder；
7. Workflow Recipe、个人技能库及 Capability 注册；
8. 自愈和版本管理；
9. Partner Connector 安全框架；
10. DLNA 与蜡笔接收端能力协商；
11. 完整端到端 Agent 场景。

每个任务应说明：修改范围、接口、迁移、测试、风险、验收条件和回滚方法。

### 21.4 实现约束

- 不把网页 DOM 细节直接固化为长期对外接口；
- MCP、CLI 和 UI 共用同一业务与权限内核；
- 确定性安全策略优先于模型判断；
- 任何外部写操作都必须有结构化风险信息；
- 密码、验证码、Cookie、Token 不进入模型、日志或 Workflow；
- 高风险动作禁止静默自愈；
- 视频能力严格遵守本文合规边界；
- 先建立测试夹具和可控示例页，再连接真实复杂站点；
- 新增能力必须带最小单元测试、契约测试和端到端验证；
- 不为追求“全站点成功率”引入远程解析规则、绕过逻辑或不可审计脚本。

### 21.5 建议项目 AI 的交付物

项目 AI 完成 PRD 更新阶段后，应至少交付：

1. 更新后的主 PRD；
2. 现状与目标差距表；
3. 模块架构图；
4. MCP/CLI 接口草案；
5. 核心数据结构草案；
6. 安全与权限矩阵；
7. 分版本实施计划；
8. MVP 验收清单；
9. 待产品负责人确认的问题列表；
10. Capability Registry、路由决策与 Partner Connector 接入草案。

---

## 22. MVP 验收清单

### 网页理解

- [ ] 当前网页可导出结构化 Markdown；
- [ ] 提取结果包含来源信息；
- [ ] 可输出 compact Agent Snapshot；
- [ ] 能识别主要按钮、链接、输入框、表单和媒体；
- [ ] Agent 可请求差异快照而非反复读取全页。

### Agent 操作

- [ ] MCP/CLI 可列出稳定 `action_id`；
- [ ] Agent 可通过 `action_id` 点击和填表；
- [ ] 动作具备前置条件、效果验证和错误类型；
- [ ] 登录、验证码等步骤可安全交还用户；
- [ ] 中高风险动作按策略确认并进入审计日志。

### Workflow Learning

- [ ] 可记录一次完整成功轨迹；
- [ ] 可生成参数化候选 Recipe；
- [ ] 用户可预览并确认保存；
- [ ] 保存技能可二次复用；
- [ ] 技能有版本、健康状态和回滚；
- [ ] 失效时能够停止、解释并降级探索。

### Partner Capability Hub

- [ ] 内置能力、Partner Connector、Site Skill 和网页自动化具有统一能力描述；
- [ ] Capability Router 可返回路径、原因、风险、所需 scopes 和 fallback；
- [ ] 路由 fallback 会重新检查权限、风险、幂等性和语义一致性；
- [ ] 外部 Agent 无法读取 Provider Token 或 Connector 密钥；
- [ ] Partner MCP/API 返回内容被视为不可信输入；
- [ ] Connector 支持健康检查、超时、速率限制、撤销和 Kill Switch；
- [ ] 用户可查看并撤销每个 Provider 的授权范围；
- [ ] 高风险能力不能因 Partner API 失败而静默降级到网页执行。

### 投屏

- [ ] 可发现普通 DLNA 设备；
- [ ] 可投放符合条件的公开媒体；
- [ ] 可执行基础播放控制；
- [ ] 能识别接收端能力并安全降级；
- [ ] DRM、登录凭证依赖或高风险内容会被拦截；
- [ ] 蜡笔接收端高级 Manifest 有明确契约和版本字段。
- [ ] Partner Manifest 具备身份、有效期、签名或等效完整性保护；
- [ ] 回传地址受允许列表、隐私策略和频率限制约束。

### 安全与合规

- [ ] Cookie、密码、验证码、Authorization 不进入模型或日志；
- [ ] Prompt Injection 防护覆盖网页到工具调用链；
- [ ] 发布、付款、删除、发消息均需明确确认；
- [ ] 高风险动作不能静默自愈；
- [ ] 用户可查看并清除操作记录和个人技能；
- [ ] 产品文案不使用“全网解析、VIP 投屏、去广告、破解”等表述。

---

## 23. 最终产品判断

这个项目最有价值的方向不是给现有浏览器简单增加一个聊天侧栏，而是把浏览器本身建设成 Agent 的可靠运行时：

```text
网页 → 结构化理解 → 稳定动作 → 安全执行
     → 成功轨迹 → 标准流程 → 站点技能 → 受控自愈
     → Markdown / 草稿 / 文档 / 视频 → 大屏输出
```

最终应让用户清楚感受到：

> **Agent 第一次完成任务是在探索；第二次是在调用经验；此后是在运行一个可验证、可审计、可修复的技能。**

这才是蜡笔 AI 投屏浏览器相较普通浏览器加 Agent 的核心差异。
