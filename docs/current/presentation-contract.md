# 本地 Presentation v1 契约（MRT-16）

状态：冻结 v1；实现为 `browser/shared-ui/mdv` 的 `MdvPresentationModel`。
范围：本地文档演示模式的状态机与分节规则。不含 TV/Cast 会话（MRT-18）、
speaker-note 与布局/动画（MRT-17）、渲染器或 parser 改写。

## 1. 分节规则（v1 冻结）

1. 分节边界是 level ≤ 2 的标题（h1、h2），事实来自 MRT-10 的
   `OutlineHeading`（同一 md4c 解析事实，渲染顺序 ordinal 不变）。
2. level ≥ 3 的标题是节内结构，不分节。
3. CommonMark thematic break（`---`/`***`）永不分节；parser 不改写。
4. 没有任何 level ≤ 2 标题的文档是单节文档（全部内容为一节）。
5. 节上限继承事实层 512 标题预算；节计数 ≤ 512。
6. 第 i 节覆盖 `headings[i] .. headings[i+1)` 的渲染范围；末节到文档尾。

## 2. 状态机

```
Normal --Enter()--> Presenting --Exit()--> Normal
  ^                                            |
  +-------------- (revision 变化) <------------+
```

- `Enter`：Normal → Presenting。重复 Enter 幂等（返回当前状态，不重置节索引）。
- `Exit`：Presenting → Normal。重复 Exit 幂等。Esc/关闭按钮映射到 Exit。
- 文档 revision 变化（MRT-03 渲染去抖 revision 前进）：无论当前相位，
  模型强制 Exit 并将节索引归零；编辑状态不跨 revision 携带。
- 模型不持有文档内容：分节只消费标题事实（level/ordinal），节的内容
  定位由 UI 层用同一 ordinal 事实完成。

## 3. 节导航

- `Move(First | Last | Next | Previous)`：索引钳制在 `[0, section_count)`；
  Next 在末节、Previous 在首节停留（不环绕——演示翻页不做循环）。
- `GoTo(index)`：越界索引钳制到最近合法边界。
- 索引变化只发生在 `Presenting` 相位；`Normal` 相位导航调用是空操作。

## 4. 焦点与读屏（模型层契约）

- 进入：焦点意图 = 当前节（stage）；退出：焦点意图 = 返回 Normal 的入口
  控件。模型暴露 `focus_intent()`，UI 层（MRT-17）负责执行。
- 每节的可访问名称 = 该节起始标题文本 + `(section i+1/n)`；无标题单节
  文档使用文档标题。

## 5. 明确不做（v1）

- TV/Cast 会话、远端遥控（MRT-18 gap analysis）。
- speaker-note、16:9 布局、翻页动画、图表重排（MRT-17）。
- 任何对渲染 HTML / 源码文本的改写；任何持久化（最近节索引等）。
