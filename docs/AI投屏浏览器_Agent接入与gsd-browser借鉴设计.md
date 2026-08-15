# get-video 项目：AI 投屏浏览器 Agent 接入与开源工具借鉴设计

> 目标读者：Codex / get-video 项目 AI / 工程实现负责人
> 文档用途：基于当前产品讨论，整理“AI Agent 如何高效、安全、可复用地操控蜡笔 AI 投屏浏览器”的技术设计方向，尤其借鉴 GitHub 开源工具 `open-gsd/gsd-browser` 的思路。
> 当前结论：不要直接 fork 一个现成浏览器；应自研 **Agent-Native Browser Runtime**，底层可用 CEF/WebView，上层提供 CDP 兼容层 + 蜡笔自有高层协议/MCP。

---

## 1. 背景

get-video / 蜡笔 AI 投屏浏览器未来不只是一个普通浏览器，而是面向三类能力：

1. **网页内容理解**：网页转 Markdown、正文提取、表格提取、视频信息识别。
2. **AI Agent 操作网页**：AI 能够打开网页、识别按钮、填写表单、保存草稿、沉淀站点工作流。
3. **投屏与大屏 Runtime**：识别网页公开视频，投到普通 DLNA 或蜡笔投屏接收端；蜡笔接收端可支持广告 + 正片 + 下一集 + 模板 + 回传。

普通浏览器和 Agent 之间通常依赖插件、Puppeteer、Playwright、CDP 端口等方式通信，但这些方式更偏底层自动化。get-video 应该做一层更高层、更安全、更产品化的能力抽象。

---

## 2. 核心判断

### 2.1 gsd-browser 是什么

`open-gsd/gsd-browser` 不是浏览器内核，也不是面向普通用户的独立浏览器，而是一个：

> Rust 编写的浏览器自动化 CLI / MCP Server，通过 Chrome DevTools Protocol 控制 Chrome/Chromium。

它不负责渲染网页，也不替代 CEF/WebView。它的位置更接近：

```text
AI Agent
  ↓
MCP / CLI
  ↓
gsd-browser
  ↓
CDP
  ↓
Chrome / Chromium
```

对 get-video 的价值不是“直接拿来当浏览器”，而是借鉴它的 **Agent 控制协议、页面快照、ref 操作、录制、断言、批处理、human takeover、MCP 化** 等设计。

### 2.2 我们应该怎么做

get-video 应形成两层对外能力：

```text
第一层：CDP 兼容层
- 兼容现有 Chrome/Chromium 自动化生态
- 适合开发者、测试工具、已有 Agent 框架
- 默认关闭，开发者/专业模式开启

第二层：Crayon Browser Agent Protocol / MCP
- 蜡笔自有高层协议
- 面向 AI Agent 的语义化能力
- 支持网页理解、Action Map、Workflow、视频检测、投屏、站点技能、权限控制
```

不要让 Agent 直接长期操作底层 DOM / JS / 坐标，而应该给它高层能力：

```text
browser.snapshot()
browser.click_ref(ref)
browser.fill_ref(ref, value)
browser.extract_markdown()
video.detect()
cast.play(device_id, video_id)
workflow.record_start()
workflow.save_skill()
```

---

## 3. 借鉴 gsd-browser 的关键能力

以下能力建议重点参考并产品化。

### 3.1 Persistent Daemon / Session 模型

gsd-browser 有常驻 daemon 和 session 概念。get-video 也应抽象出：

```text
Browser Runtime Daemon
├── App Shell Session
├── User Tab Session
├── Agent Task Session
├── Profile Session
└── Cast Session
```

建议数据模型：

```json
{
  "session_id": "sess_001",
  "type": "tab | task | cast | shell",
  "tab_id": "tab_001",
  "profile_id": "default",
  "browser_id": 1001,
  "state": "active | warm | discarded | running_task | waiting_user"
}
```

### 3.2 Page Snapshot + Versioned Refs

Agent 不应每一步都重新读取完整 DOM 或截图。浏览器应生成低 token、可操作的页面快照。

```json
{
  "snapshot_id": "snap_12",
  "url": "https://creator.example.com/publish",
  "title": "发布内容",
  "page_type": "creator_publish_editor",
  "state": {
    "logged_in": true,
    "editor_open": true,
    "has_unsaved_content": false
  },
  "elements": [
    {
      "ref": "@snap_12:e1",
      "role": "button",
      "name": "发布笔记",
      "visible": true,
      "risk": "medium",
      "bbox": [120, 80, 96, 40]
    },
    {
      "ref": "@snap_12:e2",
      "role": "textbox",
      "name": "标题",
      "visible": true,
      "risk": "low"
    }
  ]
}
```

Agent 后续用 ref 操作：

```text
browser.click_ref("@snap_12:e1")
browser.fill_ref("@snap_12:e2", "标题内容")
```

这样比让 Agent 猜 selector 或坐标更稳定。

### 3.3 Accessibility Tree 优先

页面快照应优先参考 Accessibility Tree，而不是只看 DOM class。

原因：

- role/name 更接近用户语义；
- button/textbox/link 等控件更容易抽象；
- 页面 class 经常混淆或变化；
- 对 Agent 来说 accessibility 信息更省 token。

建议快照构成：

```text
DOM 结构
+ Accessibility Tree
+ 可见性判断
+ bbox
+ 表单字段
+ 按钮语义
+ 风险标签
```

### 3.4 Ref Action

定义稳定操作：

```text
browser.click_ref(ref)
browser.fill_ref(ref, value)
browser.select_ref(ref, option)
browser.upload_ref(ref, files)
browser.hover_ref(ref)
browser.scroll_to_ref(ref)
```

底层执行时再转换为：

```text
CEF API / CDP / JS / input event
```

### 3.5 Batch Execution

高频、低风险、已确认路径不应每步都让大模型重新思考。应支持批量执行。

示例：

```json
{
  "batch_id": "batch_create_draft",
  "steps": [
    {"type": "click_ref", "ref": "@snap_12:e1"},
    {"type": "fill_ref", "ref": "@snap_13:title", "value": "{{title}}"},
    {"type": "fill_ref", "ref": "@snap_13:content", "value": "{{content}}"},
    {"type": "click_ref", "ref": "@snap_13:save_draft"}
  ],
  "stop_on_error": true
}
```

### 3.6 Assert / Wait-for

每个自动化步骤都需要验证，不应“点完就算成功”。

建议工具：

```text
browser.wait_for_text(text, timeout)
browser.wait_for_url(pattern, timeout)
browser.wait_for_ref(ref_name, timeout)
browser.assert_text_exists(text)
browser.assert_state(condition)
browser.assert_no_error()
```

示例：

```json
{
  "type": "verify",
  "condition": "draft_saved_successfully",
  "signals": [
    {"type": "text", "value": "草稿已保存"},
    {"type": "url_pattern", "value": "/draft"},
    {"type": "action_visible", "value": "view_draft"}
  ]
}
```

### 3.7 Recording Bundle / Workflow Trace

第一次 Agent 探索完成任务后，浏览器应记录轨迹：

```text
页面 URL pattern
页面标题
页面快照
点击/输入/上传
等待条件
成功条件
失败提示
风险动作
用户确认节点
验证码/人工接管节点
```

用于生成 Workflow Recipe。

### 3.8 Action Cache / Selector Healer

操作成功后，缓存多种定位方式：

```json
{
  "action_id": "save_as_draft",
  "label": "保存草稿",
  "selectors": [
    {"type": "aria", "value": "保存草稿", "confidence": 0.95},
    {"type": "text", "value": "保存草稿", "confidence": 0.90},
    {"type": "css", "value": "button[data-testid='save-draft']", "confidence": 0.80},
    {"type": "visual", "bbox_hint": [820, 680, 120, 40], "confidence": 0.60}
  ],
  "effects": ["draft_saved"],
  "risk": "medium"
}
```

当 selector 失效时，进入自愈：

```text
selector 失效
↓
用文本 / ARIA / 视觉位置 / DOM 相似度重新匹配
↓
执行验证
↓
成功后更新 Action Cache
```

### 3.9 Human Takeover

Agent 遇到登录、验证码、风控、支付、账号安全检查时，不应自动绕过，而应暂停并让用户接管。

```text
检测到挑战/风控
↓
暂停 Agent
↓
保存断点
↓
用户手动处理
↓
用户点击继续或系统检测恢复
↓
Agent 从断点续跑
```

### 3.10 Structured Extraction

网页信息提取应标准化：

```text
browser.extract_markdown()
browser.extract_tables()
browser.extract_links()
browser.extract_images()
browser.extract_video_metadata()
browser.extract_product_cards()
browser.extract_episode_list()
```

输出可用于：

- AI 摘要；
- 文档生成；
- 站点技能；
- 投屏卡片；
- 视频播放列表；
- Partner Capability Hub。

---

## 4. CDP 兼容层设计

### 4.1 定位

CDP 兼容层用于兼容现有生态：

```text
Puppeteer
Playwright Chromium mode
CDP-based Agent tools
自动化测试工具
开发者调试工具
```

### 4.2 原则

CDP 能力强但风险高，应作为专业/开发者能力：

```text
默认关闭
仅监听 127.0.0.1
必须启用开发者模式
必须 token 授权
可按 domain 限制权限
可查看审计日志
不暴露用户隐私 profile 给未授权客户端
```

### 4.3 建议启动模式

```bash
crayon-browser --enable-cdp --cdp-port=9222 --cdp-token=xxxx
```

或 App 设置中打开：

```text
设置 → 开发者 → CDP 兼容模式 → 启用
```

### 4.4 CDP 与 CEF 的关系

如果底层是 CEF，应优先通过 CEF/Chromium 内部能力实现 CDP 兼容。若完整实现成本过高，第一阶段可以只开放：

```text
Target / Page / Runtime / DOM / Network / Input / Accessibility / Screenshot
```

注意：CDP 是兼容层，不是主产品协议。

---

## 5. 蜡笔自有 Agent 协议 / MCP 设计

### 5.1 定位

Crayon Browser Agent Protocol 是主协议，面向 AI Agent 的高层能力。它比 CDP 更安全、更稳定、更懂任务。

### 5.2 对外形式

建议同时支持：

```text
MCP Server
CLI
Local HTTP API
Native SDK，后续可选
```

### 5.3 MCP Tool 分组

#### Browser Tools

```text
browser.open(url, profile?)
browser.get_tabs()
browser.activate_tab(tab_id)
browser.snapshot(tab_id)
browser.click_ref(tab_id, ref)
browser.fill_ref(tab_id, ref, value)
browser.upload_ref(tab_id, ref, files)
browser.wait_for(tab_id, condition)
browser.screenshot(tab_id)
```

#### Content Tools

```text
content.extract_markdown(tab_id)
content.extract_tables(tab_id)
content.extract_links(tab_id)
content.extract_images(tab_id)
content.summarize_page(tab_id, model?)
```

#### Video Tools

```text
video.detect(tab_id, mode)
video.get_candidates(tab_id)
video.get_metadata(video_id)
video.get_playlist(tab_id)
video.get_subtitles(video_id)
```

#### Cast Tools

```text
cast.discover_devices()
cast.play(device_id, video_id)
cast.play_url(device_id, url)
cast.play_playlist(device_id, playlist_id)
cast.pause(device_id)
cast.seek(device_id, seconds)
cast.next_episode(device_id)
cast.stop(device_id)
```

#### Workflow Tools

```text
workflow.record_start(tab_id, intent)
workflow.record_stop(record_id)
workflow.generate_recipe(record_id)
workflow.save_skill(recipe)
workflow.run(skill_id, inputs)
workflow.list_skills(site?)
workflow.update_skill(skill_id, patch)
```

#### Permission / Safety Tools

```text
permission.request(action, risk, reason)
permission.get_policy()
challenge.detect(tab_id)
challenge.handoff(tab_id, message)
challenge.resume(tab_id)
```

---

## 6. BrowserEngine 内部抽象

无论底层用 CEF 还是 WebView，上层都应统一到 `BrowserEngine`。

```rust
trait BrowserEngine {
    fn open_url(&self, tab_id: &str, url: &str) -> Result<()>;
    fn eval_js(&self, tab_id: &str, script: &str) -> Result<JsonValue>;
    fn snapshot(&self, tab_id: &str) -> Result<PageSnapshot>;
    fn click_ref(&self, tab_id: &str, r#ref: &str) -> Result<ActionResult>;
    fn fill_ref(&self, tab_id: &str, r#ref: &str, value: &str) -> Result<ActionResult>;
    fn detect_video(&self, tab_id: &str, mode: DetectMode) -> Result<Vec<VideoCandidate>>;
    fn observe_network(&self, tab_id: &str, filter: NetworkFilter) -> Result<()>;
    fn screenshot(&self, tab_id: &str) -> Result<ImageRef>;
}
```

每个 engine 声明能力：

```json
{
  "engine": "cef",
  "capabilities": {
    "dom_snapshot": true,
    "accessibility_tree": true,
    "network_observe": true,
    "network_intercept": true,
    "cdp_compat": true,
    "video_deep_detect": true,
    "multi_profile": true
  }
}
```

---

## 7. 页面快照与 Action Map 数据模型

### 7.1 PageSnapshot

```json
{
  "snapshot_id": "snap_001",
  "tab_id": "tab_001",
  "url": "https://example.com",
  "title": "页面标题",
  "timestamp": 1797200000,
  "page_type": "unknown | article | video | form | creator_editor",
  "main_markdown_preview": "# 页面标题\n...",
  "state": {
    "logged_in": true,
    "loading": false,
    "challenge_detected": false
  },
  "actions": [],
  "forms": [],
  "media": [],
  "risks": []
}
```

### 7.2 ActionRef

```json
{
  "ref": "@snap_001:e12",
  "action_id": "save_as_draft",
  "role": "button",
  "name": "保存草稿",
  "visible": true,
  "enabled": true,
  "risk": "medium",
  "bbox": [820, 680, 120, 40],
  "selectors": [
    {"type": "aria", "value": "保存草稿"},
    {"type": "text", "value": "保存草稿"}
  ],
  "preconditions": ["editor_has_content"],
  "effects": ["draft_saved"]
}
```

### 7.3 FormMap

```json
{
  "form_id": "note_editor",
  "fields": [
    {
      "field_id": "title",
      "ref": "@snap_001:f1",
      "label": "标题",
      "type": "text",
      "required": true
    },
    {
      "field_id": "content",
      "ref": "@snap_001:f2",
      "label": "正文",
      "type": "rich_text",
      "required": true
    }
  ]
}
```

---

## 8. Workflow Learning 设计

### 8.1 首次探索

```text
Agent 执行任务
↓
生成页面快照
↓
识别 Action Map / Form Map
↓
执行点击、填写、上传
↓
记录步骤和页面变化
↓
成功后生成 Workflow Recipe
```

### 8.2 用户确认保存

任务完成后提示：

```text
已完成“小红书创建草稿”。
是否保存为标准流程？
下次 AI 可直接复用该路径。
```

### 8.3 Workflow Recipe

```json
{
  "workflow_id": "xiaohongshu.create_draft",
  "site": "xiaohongshu.com",
  "intent": "创建小红书图文草稿",
  "inputs": {
    "title": "string",
    "content": "string",
    "images": "file[]",
    "tags": "string[]"
  },
  "steps": [
    {"type": "navigate", "url": "https://creator.xiaohongshu.com"},
    {"type": "ensure_state", "state": "logged_in"},
    {"type": "click_action", "action_id": "open_publish_editor"},
    {"type": "upload", "target": "media_uploader", "value": "{{images}}"},
    {"type": "fill", "target": "title_input", "value": "{{title}}"},
    {"type": "fill", "target": "content_editor", "value": "{{content}}"},
    {"type": "click_action", "action_id": "save_as_draft"},
    {"type": "verify", "condition": "draft_saved_successfully"}
  ],
  "risk_policy": {
    "save_as_draft": "first_confirm",
    "publish_now": "confirm_every_time"
  }
}
```

### 8.4 自愈机制

```text
执行 Workflow
↓
action_id 找不到
↓
Selector Healer 重新匹配
↓
匹配成功并验证通过
↓
更新 skill
```

---

## 9. Challenge-Aware Agent

不要做“自动破解验证码/滑块/选图”。应做：

> 识别验证 → 暂停任务 → 用户接管 → 断点续跑 → 流程沉淀。

### 9.1 不做的能力

```text
AI 自动识别验证码答案
AI 自动选图
AI 自动拖动滑块绕过验证
AI 自动模拟真人轨迹反风控
AI 自动接入打码平台
AI 自动读取短信/邮箱验证码
```

### 9.2 支持的能力

```text
检测验证码/风控/登录二次验证
暂停 Agent
保存任务断点
用户人工完成验证
验证后继续执行
把 challenge_handoff 节点写入 Workflow
```

### 9.3 Recipe 节点

```json
{
  "type": "challenge_handoff",
  "when": "captcha_or_risk_challenge_detected",
  "message": "请完成人工验证，完成后 AI 将继续执行。"
}
```

---

## 10. 视频检测与投屏工具

### 10.1 视频检测分级

```text
基础检测：DOM video/source/currentSrc
增强检测：网络请求 mp4/m3u8/mpd
深度检测：manifest 解析、字幕、剧集列表、清晰度
```

### 10.2 风险判断

拒绝：

```text
DRM / Widevine / FairPlay / PlayReady
license server
会员/付费/登录保护视频
需要转移 cookie/token 到电视端
明显绕过平台广告/风控/播放器
```

允许：

```text
公开视频
用户自有视频
企业/学校授权视频
合作方提供的 TV Cast Manifest
```

### 10.3 普通 DLNA 与蜡笔接收端

```text
普通 DLNA：播放单个 URL，控制能力基础
蜡笔接收端：支持 Cast Manifest、广告+正片、下一集、字幕、模板、回传、商品区
```

---

## 11. 权限与安全

### 11.1 Bridge 隔离

```text
app://index.html 可以调用 Native Bridge
https://普通网页 不允许直接调用 Native Bridge
```

所有网页操作应由 Native Core 主动执行，网页不能直接调用本地能力。

### 11.2 动作分级

| 动作 | 风险 | 策略 |
|---|---:|---|
| 读取页面 | Low | 可自动 |
| 生成 Markdown | Low | 可自动 |
| 视频检测 | Low/Medium | 可自动或用户触发 |
| 填写表单 | Medium | 可授权 |
| 保存草稿 | Medium | 首次确认 |
| 发布内容 | High | 每次确认 |
| 删除/支付/账号设置 | Critical | 默认不自动化 |
| 验证码/账号安全 | Critical | 用户接管 |

### 11.3 CDP 安全

```text
CDP 默认关闭
仅 localhost
必须 token
审计所有连接和命令
用户可随时关闭
高风险 profile 不暴露给 CDP 客户端
```

---

## 12. 与 CEF 架构结合

推荐结构：

```text
CEF App Shell Browser：加载本地 UI
CEF Web Content Browser：每个标签页一个 CefBrowser
Rust Core：业务、投屏、MCP、Workflow
C++ Adapter：CEF 生命周期和事件封装
```

多标签：

```text
Tab 1 → CefBrowser 1 → profile default
Tab 2 → CefBrowser 2 → profile xhs_account_a
Tab 3 → CefBrowser 3 → profile agent_temp
```

Tab 生命周期：

```text
Active：当前可见，完整运行
Warm：后台近期 tab，降低检测频率
Discarded：销毁 CefBrowser，仅保留元数据
```

---

## 13. Codex 实施路线

### Phase 0：调研与接口冻结

- 阅读 `open-gsd/gsd-browser` README 和命令设计；
- 阅读 CDP 基础 domain：Runtime、Page、DOM、Network、Input、Accessibility、Target；
- 阅读 MCP tools 规范；
- 输出内部接口草案：BrowserEngine、PageSnapshot、ActionRef、WorkflowRecipe。

### Phase 1：BrowserEngine + Snapshot POC

- 实现一个 CEF tab；
- 注入 JS 提取 DOM；
- 获取可见按钮/输入框；
- 生成 PageSnapshot；
- 支持 `click_ref` / `fill_ref`。

验收：

```text
打开任意网页
生成快照
列出按钮和输入框
通过 ref 点击/填写成功
```

### Phase 2：MCP / CLI POC

实现工具：

```text
browser.open
browser.snapshot
browser.click_ref
browser.fill_ref
content.extract_markdown
video.detect
```

验收：

```text
外部 Agent/Codex 可通过 MCP 或 CLI 控制浏览器完成基础任务。
```

### Phase 3：Workflow Recorder

- 记录操作步骤；
- 记录快照变化；
- 记录 wait/assert；
- 生成 WorkflowRecipe；
- 支持保存/复用。

验收：

```text
完成一次“打开网页 → 填表 → 保存”流程后，可保存为 recipe 并复用。
```

### Phase 4：Video + Cast

- DOM 视频检测；
- 网络候选检测；
- HLS/MP4/MPD 基础识别；
- 风险过滤；
- 发送到 DLNA/蜡笔接收端。

### Phase 5：Human Handoff + Skill Store

- 检测验证码/登录/风控；
- 暂停/恢复任务；
- 将 challenge 节点写入 Workflow；
- 站点技能库管理。

### Phase 6：CDP 兼容层

- 提供专业模式 CDP endpoint；
- token 授权；
- 暴露基础 domain；
- 做审计日志。

---

## 14. 不建议做的事情

```text
不要直接 fork gsd-browser 做产品浏览器；它不是浏览器 UI/内核。
不要让 Agent 直接长期用 Runtime.evaluate 操作 DOM。
不要默认开放 CDP 端口。
不要把普通网页暴露给 native bridge。
不要做自动破解验证码、反风控、指纹伪装浏览器。
不要为了视频解析绕过 DRM/会员/登录/广告技术措施。
```

---

## 15. 参考资料

1. open-gsd/gsd-browser：Rust browser automation CLI / MCP Server via CDP
   https://github.com/open-gsd/gsd-browser
2. Chrome DevTools Protocol 官方文档
   https://chromedevtools.github.io/devtools-protocol/
3. Chrome Extension debugger API：通过扩展发送 CDP 命令的安全限制参考
   https://developer.chrome.com/docs/extensions/reference/api/debugger
4. Model Context Protocol Tools 规范
   https://modelcontextprotocol.io/specification/2025-06-18/server/tools

---

## 16. 一句话总结

get-video / 蜡笔 AI 投屏浏览器应借鉴 gsd-browser 的 Agent 自动化思想，但不直接照搬其产品形态。最终架构应是：

```text
CEF/WebView 浏览器底座
+ CDP 兼容层
+ Crayon Browser Agent Protocol / MCP
+ Page Snapshot / Ref Action / Batch / Assert
+ Workflow Recorder / Skill Store / Selector Healer
+ Human Handoff
+ Video Detect / Cast Runtime
```

这样既能兼容现有 CDP 生态，又能形成蜡笔自己的高层 Agent 能力和投屏差异化。
