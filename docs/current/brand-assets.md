# 蜡笔 AI Agent 投屏浏览器品牌图标契约

- 版本：`app-icon-v1`
- 状态：`app-icon-v1` 工程母版、平台产物与自动门禁已完成；`BRD-01..04 DONE`
- 产品名：蜡笔 AI Agent 投屏浏览器

## 1. 参考源

| 属性 | 固定值 |
|---|---|
| 仓库路径 | `assets/brand/source/reference-v1.png` |
| SHA-256 | `aa807f170a73b5d8130b03f45ad36228cf45c97037dcf73d1363400b668db870` |
| 尺寸 | `1254×1254` |
| 像素格式 | 24-bit RGB，无 alpha |
| 来源 | 用户在当前产品任务中提供并确认作为产品图标 |
| 用途 | 视觉参考与溯源；不直接进入 Release 包 |

原图四角黑色像素已经烘焙，且不存在透明通道。正式资产必须从仓库 SVG 母版生成，禁止直接缩放该 PNG。

## 2. 品牌语义

- 浏览器窗口：表达产品载体。
- 蜡笔：表达“蜡笔”品牌与可理解、可操作的页面工作台。
- 蓝色底板、米白主体、绿色笔尖构成主色关系。
- App 图标不额外加入 AI 星光、机器人、投屏波纹、文字或徽标角标。

## 3. 变体

| 变体 | 用途 | 规则 |
|---|---|---|
| `master` | 64～1024px、商店、安装器、About | 保留完整窗口、三个圆点、页签与渐变 |
| `micro` | 16～48px、标题栏、任务栏小尺寸 | 移除不可辨细节，加粗窗口与蜡笔轮廓 |
| `monochrome` | 品牌水印或明确要求的单色环境 | 只表示品牌；不得表示投屏/Agent/权限状态 |

## 4. 平台组合

- Windows：透明画布上的圆角蓝色品牌底板；ICO 必须包含小尺寸专用渲染。
- macOS：同一图层结构使用完整方形底板，让系统应用当前圆角遮罩；不得保留参考图黑角。
- HarmonyOS 电脑：从相同母版按目标 DevEco/HarmonyOS 应用图标模板组合，并在 `HM-02` 真机/模拟器验证。

## 5. 禁止事项

- 不从 `app/icons` 或 `demo/icons` 的 legacy 占位图标派生正式资产。
- 不手工修改 `generated/`；任何变更从 SVG/manifest 重新生成。
- 不把 App 图标用作投屏按钮、连接状态、Agent grant、MCP、Challenge 或错误状态图标。
- 不在源或生成脚本中引用本机绝对路径、临时目录或网络资源。

## 6. 生成与验证

- 唯一配置入口：`assets/brand/manifest.json`。
- 生成命令：`node tools/brand-assets/generate.mjs`。
- 验证命令：`scripts/check.ps1 brand-assets` 或 `scripts/check.sh brand-assets`；先执行受管路径安全单测，再执行 `node tools/brand-assets/verify.mjs`，并已加入两套脚本的 `fast/all`。
- 正式输出：`assets/brand/generated/windows`、`assets/brand/generated/macos`、`assets/brand/generated/harmony`；`manifest-lock.json` 记录 renderer、文件清单与 SHA-256。
- `.gitattributes` 固定 manifest/SVG/生成工具为 LF，并把 PNG/ICO/ICNS 标记为 binary，避免 Windows checkout 改写受 hash 约束的输入。
- 平台壳和安装器只能复制这些受管输出；若平台工具要求重新封装，必须验证封装前后像素/容器来源，并保留对应平台证据。
