# GakuMotion WebView2 迁移范围纠偏审计（2026-11-15）

## 0. 执行摘要

本次审计确认：当前 `feat/webview2-manager-ui` 工作树的实现方向与用户纠偏目标不一致。

- 当前实现是 **GakuLoader Manager 整窗 WebView2/React 化**：选中 Manager 时，WebView2 覆盖整个控制窗口客户区，ImGui shell 被跳过；选中插件时才隐藏 WebView2，回到完整 ImGui shell。
- 用户要求是 **GakuLoader Manager 保持 ImGui**：Sidebar、插件管理、设置、菜单栏、状态栏常驻 ImGui；只在 **GakuMotion 插件内容区** 承载 WebView2 + React；GakuLocalify 等旧插件继续在 Manager 内容区使用 ImGui。
- 当前分支 **没有修改公共插件 ABI**：`include/GakuPlugin.h` 与 `dev` 零差异。
- 当前分支 **没有新增游戏窗口渲染回退**：Loader 仍是独立 Win32/D3D11 控制窗口，不 hook 游戏 Present/DXGI。发现的是 React 占位组件中的错误文案“in the game window”，需要纠正。
- 当前分支 **没有实现 GakuMotion React 前端**：现有 React 页面是 Loader Manager 页面；GakuMotion 只出现在 mock/test 中。
- 可复用资产包括：WebView2 动态加载/虚拟主机/origin 限制/生命周期基础、JSON Native Bridge 传输核心、React/Vite/Tailwind/vitest/离线构建工具、通用 shadcn/ui primitives。
- 推荐纠偏方案：**ImGui Manager 常驻 + Loader 拥有的子 HWND WebView2 插件内容区 + 可选 additive web-panel 服务 + GakuMotion 领域桥**。Composition/windowless 仅作为 airspace 不可接受时的后续预研。

本次只完成只读审计与纠偏方案，未编辑源码、未构建、未部署、未启动游戏。已有代码不删除，先分类保留/复用/隔离。

---

## 1. 基线与工作树核验

### 1.1 GakuLoader

| 项 | 值 |
|---|---|
| 审计工作树 | `D:\Database\Gakumas Develop\git-work\GakuLoader\worktrees\webview2-ui` |
| 分支 | `feat/webview2-manager-ui` |
| HEAD | `0d18501f3eb2b157176c07de47f6857bb97e32e8` |
| 基线 | `dev`，同为 `0d18501` |
| tracked 差异 | 18 个文件，约 `272 insertions / 35 deletions` |
| untracked | `deps/nlohmann/`、`deps/webview2/`、4 个 docs、整个 `frontend/`、`src/ManagerBackend.*`、`src/ManagerBridge.*`、`src/WebView2Ui.*`、bridge tests、`tools/package-release.ps1` |
| 公共 ABI | `include/GakuPlugin.h` 对 `dev` 零差异 |

结论：当前没有新提交，所有迁移改动都在工作树中。纠偏时不需要回滚提交，但需要重做宿主集成方向，并保留可复用基础设施。

### 1.2 GakuMotion

| 路径 | 分支 | HEAD | 状态 |
|---|---|---|---|
| `git-work\GakuMotion\dev` | `dev` | `594bf3b` | tracked 干净，仅未跟踪 `imgui.ini`；本次功能审计权威基线 |
| `git-work\GakuMotion\main` | `main` | `00ae016` | 干净，落后 dev |
| `git-work\GakuMotion\worktrees\fix-issue-5` | `fix/issue-5-import-modal` | `74e2ac8` | 有未提交 UI 改动，不混入 dev 清单 |
| `git-work\GakuMotion\worktrees\pr-6` | `pr-6` | `b0ccf31` | 已并入 dev 历史，tracked 干净 |

`fix-issue-5` 的未提交改动包含导入进度模态全局化、播放中编辑保护、`uiScale` 读取和回归测试。GakuMotion React 迁移应以这些语义为基线，否则会把已修复的 issue#5 退化回去。是否先合入该分支，需要单独确认。

---

## 2. 当前架构偏差

### 2.1 当前生产路径

关键证据：

- `src/DesktopUi.cpp:385` 注释说明 Manager 可由 WebView2/React 提供，WebView ready 时覆盖 client area，ImGui frame 被跳过。
- `src/DesktopUi.cpp:423` `ResolveUiMode()` 生产默认 `UiMode::WebView2`，只有 `[host] ui=imgui` 或 test build 走 ImGui。
- `src/DesktopUi.cpp:1321` `webManager` 条件为 WebView ready + document ready + 选中 Manager。
- `src/DesktopUi.cpp:1328` 使用 `GetClientRect(window)` 作为 WebView bounds，即整个客户区。
- `src/DesktopUi.cpp:1344` 非 Manager 选中时才隐藏 WebView 并绘制 ImGui。
- `frontend/src/App.tsx:49` 重建 DOM `MenuBar/Sidebar/StatusBar/ManagerPage`。
- `frontend/package.json:6` 自述为 “Semantic migration of the ImGui manager”。

这不符合“Loader Sidebar/插件管理/设置保持 ImGui”的要求。

### 2.2 旧插件实际路径

当前旧插件并没有被路由到游戏窗口：

- `src/DesktopUi.cpp:1243` 创建独立 top-level 控制窗口。
- `src/DesktopUi.cpp:1308` 日志明确 “no game presentation hooks”。
- `tests/DesktopUiSmoke.cpp:389-390` 断言 desktop 不 owned by game window，game window 未被 subclass。
- `src\PluginHost.cpp:1134` `DispatchDesktopPanel()` 仍在宿主 ImGui content panel 内调用插件 `onDesktopUi`。

需要纠正的错误文案：

- `frontend/src/components/PluginHostPlaceholder.tsx:34`：`rendered by the native ImGui surface in the game window`。
- `frontend/scripts/visual-check.mjs:73`：`native ImGui hosts the real panel in-game`。

正确表述应为“独立 GakuLoader 控制窗口的 ImGui 插件内容区”。

### 2.3 GakuMotion 现有呈现链

GakuMotion 当前同一套 ImGui UI 支持两种呈现：

```text
GakuPlugin_Query
  → RenderGmlMotion (src/GakuMotionPlugin.cpp:3517)
    → host::desktop.bind 协商 ImGui context/allocator
    → Core::OnRenderGui(hostedPanel) (src/GakuMotion.hpp:3025)
      → GuiOverlay::Draw(ctx, hostedPanel) (src/GkGuiOverlay.hpp:359)
        → DrawShell (src/gui/GuiOverlayShell.hpp:85)
```

- `hostedPanel = true`：Manager 内容区，不创建 ImGui 窗口，直接画入宿主内容区。
- `hostedPanel = false`：游戏内 overlay，创建 `GakuMotion 控制面板` ImGui 窗口。
- `injectionAllowed = false`：两种呈现都降级为“已禁用说明 + 开发者选项”。

纠偏后，Manager 内 GakuMotion 失败回退必须回到 **Manager 内容区 ImGui**，不能回退到游戏窗口。现有游戏内 overlay 是原有独立呈现，本次不修改游戏渲染 Hook；是否保留/废弃需要产品决策，但不能作为 WebView2 失败回退。

---

## 3. 六类改动分类

以下路径均相对 GakuLoader 审计工作树。

### 3.1 可复用 WebView2 基础设施

建议保留并改造为“插件内容区子视图宿主”。

| 文件 | 可复用点 | 必须改造 |
|---|---|---|
| `src/WebView2Ui.hpp` | `Initialize/SetVisible/SetBounds/Focus/PostMessageJson/Shutdown` 接口 | 参数化 assets/entry/panel id；支持多 panel 或单 Motion panel registry |
| `src/WebView2Ui.cpp:59` | 动态加载 app-local `WebView2Loader.dll`，避免 import lib 硬依赖 | 保留 |
| `src/WebView2Ui.cpp:92` | 虚拟主机 `https://gakuloader.invalid/`、本地 folder mapping | 为 Motion 使用独立虚拟主机或路径隔离 |
| `src/WebView2Ui.cpp:123` | origin/navigation/WebMessage 限制、NewWindow/Permission 拒绝 | 保留并扩展到 Motion bridge |
| `src/WebView2Ui.cpp:254` | `CreateCoreWebView2Controller(parent, ...)` | bounds 改为 ImGui 插件内容区矩形 |
| `src/WebView2Ui.cpp:331-352` | visible/bounds/focus 控制 | 增加内容区裁剪、DPI、焦点离开处理 |
| `src/WebView2Ui.cpp:364` | shutdown 顺序 | 重初始化前先 Close 旧 controller；增加 `ProcessFailed` |
| `deps/webview2/` | vendored SDK `1.0.4314.35`，hash/provenance 已记录 | 保留 |
| `deps/nlohmann/` | JSON 单头文件，hash/provenance 已记录 | 保留 |

当前缺口：

- 单全局 controller，不支持多插件生命周期。
- 缺 `ProcessFailed` / renderer-gone 恢复。
- 缺 `GotFocus/LostFocus/MoveFocusRequested/AcceleratorKeyPressed`。
- 缺 `RasterizationScale/BoundsMode/ShouldDetectMonitorScaleChanges` 的明确策略。
- `AllowExternalDrop(FALSE)` 与宿主文件拖放路径需要重新验证。

### 3.2 可复用 React/Vite 构建工具

工具链可复用，Manager 页面不可直接当作 GakuMotion 前端。

| 文件/目录 | 处置 |
|---|---|
| `frontend/package.json`、`package-lock.json` | 保留依赖基线；后续裁剪未使用 Radix 包，补前端许可 |
| `frontend/vite.config.ts` | 保留 `base: './'`、离线构建策略；入口改为 Motion |
| `frontend/vitest.config.ts`、`src/test/setup.ts` | 保留测试基础设施 |
| `frontend/tsconfig*.json`、`tailwind.config.js`、`postcss.config.js` | 保留 |
| `frontend/build.ps1` | 保留离线资产检查；产物目录改为 Motion UI |
| `frontend/src/components/ui/` | 保留通用 primitives |
| `frontend/src/lib/utils.ts` | 保留 |
| `frontend/src/bridge/client.ts` | 保留 correlation/timeout/AbortSignal/sid 失效模式 |
| `frontend/index.html` CSP | 保留并适配 Motion |

当前问题：

- `frontend/package.json:6` 范围是 Loader Manager。
- CI `npm ci` 与本地 `build.ps1 --ignore-scripts` 策略不一致，需要统一。
- `frontend/dist`、`frontend/visual-check` 只是 mock/browser 证据，不是 native WebView2 证据。

### 3.3 可复用 Native Bridge

传输核心可复用；`ManagerBackend` 是 Loader 管理面，不是 Motion 领域桥。

| 文件 | 可复用点 | 处置 |
|---|---|---|
| `src/ManagerBridge.hpp/.cpp` | JSON request/response/event、`v/id/type/method/params`、`ok/result/error`、`seq/sid`、allow-list、异常封装、Reset/Invalidate | 抽为通用 transport 或保留核心 |
| `frontend/src/bridge/client.ts` | id correlation、timeout、AbortSignal、stale sid drop、bridge.closed invalidation | 复用到 Motion bridge |
| `frontend/src/bridge/protocol.ts` | 类型化 method/event 模式 | 重建 Motion 协议 |
| `src/ManagerBackend.hpp/.cpp` | Loader host snapshot/hotkeys/settings/appearance/language/shell | 隔离为 Loader 原型，不作为 Motion 后端 |
| `tests/ManagerBridgeTests.cpp` | headless bridge 测试模式 | 保留核心测试；新增 Motion bridge 测试 |

当前 bridge surface：

- 核心：`bridge.hello`、`bridge.ping`。
- Backend：23 个注册调用，总计 25 methods、14 events。
- 全部是 Loader Manager 语义；没有 `motion.*` / `gakumotion.*` 领域方法。

已知风险：

- panel 切换不结束 backend capture/editor suppression，可能残留热键抑制。
- `EmitAppearance()` 不比较 `dpiScale`，DPI-only 变化不推事件。
- 插件发现列表变化不推 `settings.changed`。
- 同步 handler 在 UI 线程执行 INI/Hotkeys；Motion 长任务必须走既有命令队列/主线程机制，不能阻塞 UI。

### 3.4 不必要的 GakuLoader UI Web 化

当前实现主体，应隔离/降级，不应作为默认 Manager。

| 文件 | 内容 | 建议 |
|---|---|---|
| `src/DesktopUi.cpp:385-433` | Manager 整窗 WebView 模式 | 移除生产默认整窗替换 |
| `src/DesktopUi.cpp:1157` | `StartWebView()` 构造 Manager backend | 改为 Motion web panel 初始化 |
| `src/DesktopUi.cpp:1321-1354` | Manager 选中时跳过 ImGui frame | 改为始终渲染 ImGui shell，仅内容区嵌 WebView |
| `frontend/src/App.tsx` | DOM MenuBar/Sidebar/StatusBar/ManagerPage | 不作为 Loader 生产 UI |
| `frontend/src/components/Sidebar.tsx` | Web Sidebar | 隔离 |
| `frontend/src/components/MenuBar.tsx` | Web MenuBar | 隔离 |
| `frontend/src/components/StatusBar.tsx` | Web StatusBar | 隔离 |
| `frontend/src/pages/ManagerPage.tsx` | 四 tab Manager | 隔离 |
| `frontend/src/pages/PluginListPage.tsx` | 插件列表 | 隔离 |
| `frontend/src/pages/AppearancePage.tsx` | 外观设置 | 隔离 |
| `frontend/src/pages/HotkeysPage.tsx` | 热键设置 | 隔离 |
| `frontend/src/pages/SettingsPage.tsx` | Loader 设置 | 隔离 |
| `frontend/src/state/store.ts` | Loader shell/store | 隔离或重建 Motion store |

必须保留的 ImGui Manager：

- `src/DesktopUi.cpp:978` `DrawHostShell`
- `src/DesktopUi.cpp:967` `DrawManagerPage`
- `src/DesktopUi.cpp:737` `DrawPluginList`
- `src/DesktopUi.cpp:766` `DrawAppearancePage`
- `src/DesktopUi.cpp:885` `DrawHotkeysPage`
- `src/DesktopUi.cpp:919` `DrawSettingsPage`

### 3.5 错误的 ImGui 游戏窗口回退

未发现当前分支新增实际游戏窗口回退代码。发现错误文案和潜在误导：

- `frontend/src/components/PluginHostPlaceholder.tsx:34`
- `frontend/scripts/visual-check.mjs:73`

GakuMotion 自身存在原有游戏内 overlay 路径：

- `src/GkGuiOverlay.hpp:394-424`：`hostedPanel=false` 时创建游戏内 ImGui 窗口。

纠偏要求：

- Manager 内 WebView2 失败 → 回退到 Manager 内容区 ImGui/占位面板。
- 不新增游戏窗口回退。
- 不修改游戏渲染 Hook。
- 现有游戏内 overlay 是否保留作为独立功能，需单独决策；不能替代 Manager 内回退。

### 3.6 尚未实现的 GakuMotion 前端功能

确认缺失：

- 无 Motion 页面、组件、领域 store。
- 无 Motion bridge methods/events。
- 无 Motion 资产目录/入口。
- `frontend/src/bridge/mock.ts:61-85` 中的 `gakumotion` 只是 mock id，不能当作真实插件集成证据。
- `shell.selectPanel` 只切换 native selection，不会加载 Motion React 内容。

GakuMotion 真实 descriptor：

- `src/GakuMotionPlugin.cpp:3569`：`id = "gakumotion"`，`version = "0.1.1"`。

---

## 4. GakuMotion 现有 UI 功能清单

以下路径均相对 `D:\Database\Gakumas Develop\git-work\GakuMotion\dev`。

### 4.1 外壳与全局

| 功能 | 源文件 | 行为/状态 owner |
|---|---|---|
| Sidebar 页面导航 | `src/gui/GuiOverlayShell.hpp:113-145` | 曲库/动作/镜头/舞台/设置；开发者模式追加 [开发] |
| 顶部状态条 | `src/gui/GuiOverlayShell.hpp:21-83` | 驱动角色/绑定/曲目/播放态/允许控制 |
| 权限降级页 | `src/GkGuiOverlay.hpp:362-393` | `injectionAllowed=false` 时禁用说明 + 开发者选项 |
| 主题/缩放 | `src/gui/GkGuiThemeColors.hpp`、`src/GkGmlClient.hpp:95-103` | 宿主 appearance 服务 |
| 全局 TransportBar | `src/gui/GuiTransportBar.hpp` | 所有页面常驻 |
| 删除确认模态 | `src/GkGuiOverlay.hpp:444-485` | 永久删除 + libraryRevision 守卫 |
| 文件浏览/拖放轮询 | `src/GkGuiOverlay.hpp:516-544`、`src/gui/GuiOverlayCommon.hpp:353-485` | 宿主异步文件对话框/file-drop/file-drag |

### 4.2 TransportBar

| 控件 | 源 | 行为 |
|---|---|---|
| 曲目名/状态色/时间 | `GuiTransportBar.hpp:186-208` | 只读 player 状态 |
| 进度滑条 | `:229-254` | ScrubBegin/Update/End 三段式，拖动冻结时钟 |
| 上一曲/下一曲 | `:260-309` | `fnCycleMotion(±1)`；ensemble 激活时禁用 |
| 播放/暂停/继续/停止 | `:271-298` | 停止归零并恢复游戏动作 |
| 播放模式 | `:314-325` | 普通 4 态；ensemble 2 态 |
| 倍速 | `:327-346` | 0.1-3x；音频无法拉伸时自动静音 |
| 音乐音量 | `:348-356` | 0-100%；松开自动保存 |
| 镜头快捷模式 | `:358-383` | 游戏/VMD/跟随/注视，pending 确认 |
| 上一帧/下一帧 | `:395-401` | 暂停并帧步 |
| 时间输入定位 | `:403-430` | Enter 提交/Esc 取消，超时长报错 |
| 动作帧输入定位 | `:432-465` | 首帧 0，末帧 total-1 |
| 告警行 | `:469-505` | 帧位置/时长不一致/源文件变更/音量不支持/导航错误 |

### 4.3 曲库 Library

源：`src/gui/GuiLibraryPage.hpp`，约 1479 行。

| 功能 | 行号 | 行为 |
|---|---|---|
| 刷新曲库 | `:1214-1220` | `fnRefreshMotions` |
| 打开曲库文件夹 | `:1222-1227` | `ShellExecuteW` |
| 重新加载当前曲目 | `:1228-1235` | 保留时间并暂停 |
| 添加到曲库 | `:1251-1258` | 异步多选文件对话框 |
| 管理选中 | `:1265-1273` | 编辑模态 |
| 删除选中 | `:1275-1280` | 永久删除确认 |
| 搜索/清空 | `:1285-1292` | UI 本地过滤 |
| 加载预览/播放选中 | `:1293-1302` | `fnSelectMotion(autoplay)` |
| 曲库表格 | `:1308-1469` | 状态/曲目/内容标签/时长；单击选中、双击播放；ListClipper 虚拟化 |
| 整窗拖放 | `:411-642`、`:1170-1175` | Armed/Hover/Invalid 三态；文件类型过滤；提权解释 |
| 添加/管理模态 | `:684-1061` | 四槽位：动作/表情/镜头/音频；VMD 二进制倾向探测；速度/初始偏移；校验 |
| 导入进度模态 | `:1064-1164` | 进度/成功自关/错误详情 |
| 智能分类 | `GuiOverlayCommon.hpp:543-749` | 二进制倾向 + 文件名关键词推荐槽位/标题 |

### 4.4 动作 Motion / Ensemble / Bone

源：`src/gui/GuiMotionPage.hpp`、`src/gui/GkGuiTabBone.hpp`。

| 功能 | 源 | 行为 |
|---|---|---|
| 多人舞台 ensemble | `GuiMotionPage.hpp:16-171` | 最多 3 槽；角色/动作/站位/朝向；主槽/镜头槽；预设保存/载入；revision+actorEpoch |
| 驱动角色选择 | `GuiMotionPage.hpp:188-205` | sceneActors token+epoch |
| 表情幅度 | `GuiMotionPage.hpp:208-218` | 整体/眉毛/眼部/嘴部 0-2 倍；当前直写 `controller.facialWeights`；保存 |
| 初始位置偏移 | `GuiMotionPage.hpp:227-298` | XYZ ±10m；即时生效；保存默认 |
| 高级骨骼微调 | `GkGuiTabBone.hpp:113-479` | 55 骨树；对称联动；局部/世界；Pitch/Yaw/Roll；保存至曲目/全局；重置 |

### 4.5 镜头 Camera

源：`src/gui/GuiCameraPage.hpp`、`src/gui/GuiCameraMode.hpp`。

| 功能 | 行为 |
|---|---|
| 模式 Combo | 游戏/VMD/跟随/注视 |
| 状态行 | 接管/轨道/接口未就绪/错误/pending |
| VMD 运镜范围 | 0.3-1.0 |
| 分模式 Transform | 位置/旋转；legacy 偏移自动升级 |
| 跟随 | 部位/速率/位移比例 |
| 高级 | 随角色平移、位移比例、注视偏移、FOV、场景范围限制 |
| 提交/保存 | 草稿-expected-pending 确认；`camera.json` |

legacy 不可达功能：`src/gui/GkGuiTabCamera.hpp` 含镜头 VMD 导入（fps/scale/切镜检测）、角色阴影实验 Combo、重新载入镜头。当前产品页不可达，需决策去留。

### 4.6 舞台 Stage

源：`src/gui/GuiStagePage.hpp`。

| 功能 | 行为 |
|---|---|
| 光影效果 Checkbox | 联动 `cameraSettings.actorShadowExperimentMode`，跨域副作用必须保留 |
| 场景背景 Checkbox | 关→hideBackground/greenScreen/overrideCameraClear；开→恢复 skybox |
| 背景颜色 | ColorEdit3 + 5 预置 |
| 高级场景编辑 | 自定义节点开关、扫描、候选树显隐、碰撞锁定 |
| 持久化 | `stage.json` |

### 4.7 设置 Settings

源：`src/gui/GuiSettingsPage.hpp`。

| 功能 | 行为 |
|---|---|
| 物理引擎 | 关闭/内建/原生；本次运行生效；重启读 `control.json` |
| 快捷键一览 | 静态表 |
| 开发者模式 | 会话态，不持久化 |

### 4.8 开发 Developer

源：`src/gui/GuiDeveloperPage.hpp`，6 个子 tab。

| 子 tab | 源 | 功能 |
|---|---|---|
| 动作 | `GuiMotionExperiments.hpp` | Miku 嘴形/眼眉拟合三态；腰部支撑；视线 |
| 遥测与诊断 | `GuiDeveloperPage.hpp:38-95` | player/clip/actor/scrub 探针；BeforeQuartz |
| 鞋底测量 v2 | `GkGuiTabSole.hpp:122-238` | 代理点/平面/手工三点/应用/保存/载入；自绘 2D 骨架图 |
| Pitch Demo | `GkGuiTabExperimentalSolePitch.hpp` | 实验开关；最大 pitch 0-35° |
| 摄影舞台替换 | `GuiStageLoadReplacement.hpp` | 下次摄影背景；权限禁用时也可编辑 |
| 布光与视频 | `GuiStageStudio.hpp` | 自定义布光/人物风格光/主补轮廓灯/背景装饰灯；本地视频幕布播放/暂停/循环/位置 |

注意：GakuMotion 没有视频录制 UI；“视频”指本地视频播放到摄影幕布。CaptureGuard/CaptureSafety 是截图保护/注入门控，不是录屏。

### 4.9 视线 Gaze

源：`src/gui/GkGuiTabGaze.hpp:9-99`。

- Auto/ForceOff/DesktopOn Radio + 循环切换。
- 桌面追焦 Checkbox。
- 眼/头/身体权重 0-1。
- 当前直写 `LookAtManager` 原子量 + config 字段；迁移建议命令化。

### 4.10 legacy dormant tabs

当前 Shell 无调用点，但代码存在：

- `GkGuiTabMotion.hpp`：旧曲库列表/导入表单/播放列表 JSON 保存载入/预览控制。
- `GkGuiTabConfig.hpp`：control.json 状态面板/物理/BeforeQuartz/腰撑。
- `GkGuiTabBone.hpp:11-111`：旧 Actor 状态/useRestRotations/多角色 legacy 按钮/录制高度校准。
- `GkGuiTabCamera.hpp`：镜头 VMD 导入/阴影实验。
- `GkGuiTabStage.hpp`：旧舞台即时挂载残留。

迁移时不能默认丢弃；需逐项决策“复活/合并/废弃”。

---

## 5. ImGui → React 迁移矩阵草案

原则：语义和功能等价优先，不大规模重设计。Native 后端保持权威；React 不拥有播放/录制/实时注入逻辑。

### 5.1 外壳与全局

| 功能 | 建议组件 | Bridge | 自动验证 | 人工验收 |
|---|---|---|---|---|
| 页面导航 | Sidebar + Button | UI 本地 store | 路由/高亮 | Manager 嵌入布局 |
| 顶部状态条 | StatusBar + Badge | 状态快照上行 | 快照渲染 | 游戏内角色名 |
| 权限降级页 | Alert + Collapsible | injectionAllowed/notice | 权限翻转 | 摄影/更衣室进出 |
| 主题/缩放 | Tailwind token | appearance 上行 | 双主题快照 | 宿主主题实时跟随 |
| 删除确认 | AlertDialog | library.delete(key,revision) | revision 失效 | 真实删除/播放保护 |

### 5.2 TransportBar

| 功能 | 建议组件 | Bridge | 自动验证 | 人工验收 |
|---|---|---|---|---|
| scrub | Slider pointer 三段 | scrub.begin/update/end | 拖动回归 | 不回弹/定位准确 |
| 播放控制 | Button + Tooltip | play/pause/resume/stop/cycle | 命令序列 | 音画同步/停止恢复 |
| 播放模式 | Select | playlist.setMode | 模式切换 | 循环/随机 |
| 倍速 | Select | speed.set | 速度下发 | 听感/同步 |
| 音量 | Slider onValueCommit | volume.set/volume.save | commit 才 save | 实际音量 |
| 镜头快捷模式 | Select | camera.set + ack | pending→ack | 镜头接管 |
| 帧步/时间/帧定位 | Input + Button | stepFrame/seek/seekFrame | 解析/边界 | 帧级画面 |

### 5.3 Library

| 功能 | 建议组件 | Bridge | 自动验证 | 人工验收 |
|---|---|---|---|---|
| 曲库表格 | Table + Virtual + Badge | library 快照(revision)/select | 大列表/双击 | 状态标记 |
| 搜索 | Input | UI 本地 | 过滤 | — |
| 刷新/重载/打开文件夹 | Button | library.refresh/motion.reload/shell.openFolder | 命令 | 文件夹打开 |
| 添加/管理模态 | Dialog + Table + Select + Input | files.probe/import.submit | 校验规则 | 真实 VMD 导入 |
| 文件选择 | Button | dialog.begin/poll | mock poll | 原生对话框 |
| 整窗拖放 | DropOverlay | drop.setTarget/poll/drag.peek | 状态机 | Explorer 拖入/提权 |
| 导入进度 | Dialog + Progress | import.progress | 进度状态机 | 大文件观感 |

### 5.4 Motion / Ensemble / Bone

| 功能 | 建议组件 | Bridge | 自动验证 | 人工验收 |
|---|---|---|---|---|
| 驱动角色 | Select | actors 快照/selectTarget(token,epoch) | actor epoch | 多角色驱动 |
| 表情幅度 | Slider×4 + Button | facial.setWeights/save | 数值 | 脸部视觉 |
| 初始位置 | DragNumber×3 | position.set/save | 命令 | 站位 |
| ensemble | Collapsible + Select + Slider | ensemble.set/preset/probe | 现有 ensemble 回归 | 3 人同台 |
| 骨骼微调 | Tree + RadioGroup + Slider | bone.get/set/reset/save/reload/toggle | 覆盖读写 | 姿态/穿模 |

### 5.5 Camera / Stage / Studio

| 功能 | 建议组件 | Bridge | 自动验证 | 人工验收 |
|---|---|---|---|---|
| 镜头参数 | Select + Slider + Input | camera.set/save + ack | 现有相机回归 | 运镜/跟随/注视 |
| 光影/背景 | Switch + ColorPicker | stage.set（native 保留联动） | 联动命令 | 绿幕/阴影 |
| 场景节点树 | Tree + Checkbox + Badge | stage.scan/toggleNode | 树/禁用 | 物件显隐 |
| 舞台替换 | Select | stage.setLoadReplacement | 命令 | 重进摄影 |
| 布光 | Tabs + Switch + Slider + ColorPicker | studio.set/reset | 现有 studio 回归 | 灯光效果 |
| 视频幕布 | Input + Switch + Slider | studio.set/dialog.begin(3)/video.notice | 校验 | 视频播放/位置 |

### 5.6 Settings / Developer / Gaze / Sole

| 功能 | 建议组件 | Bridge | 自动验证 | 人工验收 |
|---|---|---|---|---|
| 物理后端 | Select + Alert | physics.set | 命令 | 物理效果/稳定性 |
| 快捷键表 | Table | 无需 | 快照 | 键位 |
| 开发者模式 | Switch | UI 本地 | — | — |
| Miku/腰撑/BeforeQuartz | Switch | experiments.set | 现有回归 | 拟合效果 |
| 视线 | RadioGroup + Slider | gaze.set | 数值 | 追焦 |
| 鞋底测量 | Slider + Input + Canvas/SVG | sole.set/save/load | 投影纯函数 | 测量对照 |
| Pitch Demo | Switch + Slider | solePitch.set | 命令 | 穿地修正 |
| 遥测 | 描述列表 | telemetry 上行 | 快照 | — |
| legacy 功能 | 待决策 | camera.import/reload 等 | interop | 若复活需实机 |

---

## 6. 混合 UI 宿主技术方案

### 6.1 方案 A：子 HWND WebView2 Controller（推荐）

形态：

- Loader 继续每帧绘制完整 ImGui shell。
- 仅选中 GakuMotion web panel 且 WebView ready 时，显示 WebView2 子 HWND。
- WebView bounds 同步到 `##PluginContent` 的宿主客户区矩形。
- Manager/旧插件选中时隐藏 WebView，ImGui 全量绘制。

依据：

- WebView2 windowed hosting 基于 HWND，存在 airspace 限制。
- 当前 Loader 已有独立 STA UI thread + 消息泵，满足 WebView2 线程模型。
- vendored SDK 提供 `CreateCoreWebView2Controller`、`put_Bounds/put_IsVisible/MoveFocus/Close`、`ICoreWebView2Controller3/4`。

优点：

- 最小 ABI 影响。
- 复用现有 WebView2Ui 基础。
- 不修改游戏渲染 Hook。
- 旧插件路径不变。

限制：

- WebView 子 HWND 不能被 ImGui 绘制覆盖/裁剪/混合。
- ImGui popup/modal/tooltip 若叠到内容区会被遮。
- ImGui child clipping 不会裁剪 HWND，必须显式换算 bounds。
- 子 HWND 焦点可能触发宿主 `WM_KILLFOCUS`，必须区分内部焦点转移与离开宿主窗口。
- 文件拖放可能被子 HWND/WebView 吞掉。

必须处理：

- bounds/DPI/uiScale/resize/minimize/hide/tray。
- Sidebar 切换 visibility。
- Tab/Shift+Tab 离开 WebView，焦点交回 ImGui。
- editable focus 上报，热键抑制。
- `ProcessFailed`/renderer-gone fallback。
- controller generation、Close、environment 释放。
- 多插件 registry/owner retirement/资源上限。

### 6.2 方案 B：Composition/windowless Controller（后续预研）

优点：可解决 airspace，可在 visual tree 中排序/裁剪。

代价：

- 需要 DirectComposition/Windows.UI.Composition visual tree。
- 当前 Loader 是普通 windowed swapchain + Present，不是 composition tree。
- 宿主必须路由 mouse/pointer/keyboard/IME/focus/cursor/drag/accessibility。
- DPI/缩放/裁剪/hit-testing/Tab/屏幕阅读器都要宿主负责。
- 仍然不是 ImGui widget，不能进入 ImGui draw list。

结论：不作为当前最小 ABI 首选。只有子 HWND airspace 被验收判定不可接受时，再单独立项。

### 6.3 其他方案

| 方案 | 结论 |
|---|---|
| 独立弹出 WebView 窗口 | 不符合“同一 Manager 窗口内容区”；只可诊断 |
| CEF/Qt WebEngine/OSR 纹理 | 新依赖/许可/输入/IME/生命周期成本高，偏离已有 WebView2 投入 |
| 把 DOM 画进 ImGui draw list | 不可行 |

---

## 7. 推荐架构与 ABI 影响

### 7.1 目标结构

```text
GakuLoader Manager（独立 Win32 窗口）
  ImGui MenuBar
  ImGui Sidebar
    Manager → ImGui 插件列表/外观/热键/设置
    GakuMotion → WebView2 + React（内容区）
    GakuLocalify → ImGui onDesktopUi（内容区）
    其他旧插件 → ImGui onDesktopUi（内容区）
  ImGui StatusBar
```

### 7.2 Loader 侧

- 保留 `DrawHostShell/DrawManagerPage/DispatchDesktopPanel`。
- 将 `WebView2Ui` 改造为 `WebPanelHost`：
  - 计算 ImGui 内容区矩形。
  - 管理 controller/child HWND/bounds/visibility/focus/DPI。
  - 管理 bridge transport、origin、generation、shutdown。
  - 处理 `ProcessFailed` 和 fallback。
- 生产默认不再整窗替换 Manager。
- `[host] ui=imgui` 可保留为全局禁用 web panel 的诊断开关。

### 7.3 GakuMotion 侧

- 新建 Motion React 入口、领域 store、页面组件。
- 复用现有 `GuiContext` / `GuiAction` / `QueueGui` 语义。
- 领域 bridge allow-list 由 GakuMotion 拥有，Loader 只转发 JSON。
- 文件对话框、OS 拖放、热键、ShellExecute、VMD 探测仍走 native/宿主服务。
- 直写共享状态的 UI 点（facialWeights/gaze/legacy useRestRotations）建议命令化。

### 7.4 ABI 方案

现有 `include/GakuPlugin.h` 不修改字段偏移、枚举值、函数签名。

推荐新增 **可选 additive web-panel 服务契约**，通过现有 `GakuHostApi::registerService/queryService` 注册/查询：

- 服务名示例：`gakuloader.web-panel/v1` 或 provider-owned `gakumotion.web-panel/v1`。
- 旧插件不查询则零影响。
- 不 repurpose `GAKU_DESKTOP_PRESENTATION_PLUGIN_PANEL`。
- 不把 WebView2 COM/HWND 暴露给插件；宿主拥有窗口/controller/安全边界。

服务表建议能力：

| 能力 | 用途 |
|---|---|
| assets/entry 查询 | Loader 映射本地虚拟主机 |
| onShow/onHide | 面板切换生命周期 |
| onUiTick | UI thread 轮询异步文件对话框/拖放/导入进度，不绘制 ImGui |
| handleMessage | 受控 JSON bridge 请求 |
| setEventSink | native → React 事件 |
| onShutdown | 失效 pending 请求/释放资源 |

为什么需要 `onUiTick`：GakuMotion 当前在 `Draw()` 内轮询文件浏览/导入进度。若 React 替代 ImGui 绘制后完全跳过 `onDesktopUi`，这些异步状态会停摆。因此 web panel 激活时仍需一个 UI-thread tick 回调；fallback 时恢复现有 `onDesktopUi`。

现有 `gakumotion.control/v0` 可复用为播放/相机/场景命令范式，但覆盖不足：只有 Load/Play/Pause/Stop/Seek/Speed/Offset/Camera/Scene，不能覆盖 Library 导入、文件对话框、拖放、骨骼、布光、视频、ensemble 等完整 UI。不能把 control v0 当作完整 Motion bridge。

### 7.5 回退策略

| 失败 | 回退 |
|---|---|
| WebView2 Runtime/loader 缺失 | Manager 内容区 ImGui GakuMotion |
| assets 缺失/映射失败 | Manager 内容区 ImGui GakuMotion |
| controller/navigation 失败 | Manager 内容区 ImGui GakuMotion |
| renderer ProcessFailed | Manager 内容区 ImGui GakuMotion + 诊断/重试 |
| bridge 断开 | Manager 内容区 ImGui GakuMotion |
| 旧插件 | 始终 Manager 内容区 ImGui |

禁止：

- 回退到游戏窗口。
- 修改游戏渲染 Hook。
- 因 WebView2 不可用而改变旧插件 UI 路由。

---

## 8. 分阶段实施计划

### 阶段 0：确认与基线保护

- 确认本报告架构方案。
- 确认是否先合入/对齐 `fix-issue-5`。
- 确认 legacy dormant tabs 去留。
- 确认游戏内 overlay 是否保留为独立功能。
- 保留当前未提交代码，不删除；必要时将 Loader Web 原型隔离为 dev/mock。

### 阶段 1：架构复位

- 恢复 ImGui Manager 常驻。
- 移除“Manager 选中即整窗 WebView”的生产路径。
- 保留 WebView2/bridge/前端工具基础设施。
- 修正 game window 错误文案。
- 现有 ImGui Manager/UI/Windows 测试必须通过。

### 阶段 2：单插件内容区 WebView2 原型

- 只接 GakuMotion。
- Loader 计算 `##PluginContent` 矩形并同步 bounds。
- 处理 visibility/focus/Tab/DPI/resize/hide/minimize。
- 增加 `ProcessFailed`、navigation failure、bridge disconnect fallback。
- 验证文件拖放/对话框/热键不被破坏。
- 此阶段可先用宿主内部集成，暂不新增公共服务。

### 阶段 3：可选 web-panel 服务与 Motion bridge

- 新增 additive 服务契约。
- GakuMotion 注册 web-panel 服务。
- Loader panel registry 管理生命周期。
- 建立 Motion 领域 allow-list、状态快照、事件、revision/token。
- 复用 control v0 范式，但不局限于 v0。

### 阶段 4：GakuMotion React 语义迁移

优先顺序：

1. Shell/状态条/权限降级/TransportBar。
2. Library 表格/搜索/选择/刷新/删除。
3. 导入模态/文件对话框/拖放/进度。
4. Motion/驱动角色/表情/初始位置。
5. Camera/Stage/Settings。
6. Ensemble/Bone。
7. Developer/Studio/Gaze/Sole/Pitch/遥测。
8. legacy 功能按决策处理。

每页必须保留隐性行为：

- scrub 三段式防回弹。
- 草稿-expected-pending 确认。
- libraryRevision/actorEpoch/controlTicket 防过期。
- 音量松开即存。
- 双击播放。
- ensemble 时禁用上下曲。
- 光影开关联动相机阴影模式。
- 权限丢失丢弃 scrub。
- 原生文件对话框/OS 拖放/热键门控。

### 阶段 5：验证与打包

- Native Release/Debug。
- 既有 headless suites。
- 新增 web panel bounds/focus/DPI/lifecycle/fallback 测试。
- Motion bridge contract tests。
- Frontend typecheck/test/build。
- package-release + 前端许可 notices。
- 游戏内人工验收清单。

---

## 9. 验证门禁

### 9.1 离线门禁

- `include/GakuPlugin.h` 现有 ABI 不变。
- 旧服务表大小/枚举/签名不变。
- Manager UI / DesktopUi WARP smoke / WndProc chain / FileDrop / Dialogs / Fonts / Appearance / Language / Hotkeys 通过。
- Bridge core 测试通过。
- 新增内容区 bounds/visibility/focus/Tab/text-input/DPI/resize/minimize/ProcessFailed/retire/shutdown 测试。
- 静态确认无游戏 Present/DXGI/render hook 改动。
- Frontend typecheck/test/build 通过。
- 打包包含正确 Motion UI assets、WebView2Loader、许可。

### 9.2 人工验收门禁

- Manager/Sidebar/设置始终 ImGui。
- GakuLocalify 等旧插件始终 Manager 内 ImGui。
- GakuMotion React 只出现在插件内容区。
- ImGui 弹窗与 WebView 遮挡关系可接受，或有避让/隐藏策略。
- 窗口拖动/resize/多显示器 DPI/uiScale 无错位。
- 点击 WebView、Tab 离开、IME、热键抑制、F1、最小化/托盘恢复正常。
- 文件拖放/对话框/hotkey capture 正常。
- runtime/assets/renderer 失败时回退 Manager ImGui，不回退游戏窗口。
- 多插件切换/retire/shutdown 无泄漏/挂起/串扰。
- 动作播放/音画同步/停止恢复/物理/骨骼/表情/镜头/光影/视频/ensemble/舞台替换/场景节点实机验收。

---

## 10. 风险与待确认决策

| 风险/决策 | 说明 |
|---|---|
| 架构方向 | 当前 Manager 整窗 React 必须重做 |
| airspace | 子 HWND 不能被 ImGui 覆盖；弹窗/tooltip 需避让 |
| 焦点/热键 | 子 HWND 焦点触发 WM_KILLFOCUS；Tab/IME/editable focus 必须处理 |
| DPI | ImGui scale、WebView rasterization、CSS zoom 可能双重/三重缩放 |
| 生命周期 | 当前单 controller、重初始化未 Close、缺 ProcessFailed |
| 拖放 | AllowExternalDrop(FALSE) 和子 HWND 可能破坏 file-drop mailbox |
| ABI | 新增 web-panel 服务必须可选、additive，需确认 |
| fix-issue-5 | 未提交导入模态/播放中编辑保护/uiScale 需先对齐 |
| legacy tabs | 镜头 VMD 导入/阴影实验/旧列表/配置面板需决策 |
| 游戏内 overlay | 是否保留为独立功能需决策；不能作为 Manager 回退 |
| 前端许可 | React/Radix/lucide/Tailwind 等 notices 未覆盖 |
| 文档 | 现有 matrix/API docs 是 Loader Web 化口径，需纠偏 |

---

## 11. 本次验证边界

已执行：

- 只读 git status/diff/worktree/branch 核验。
- GakuLoader/GakuMotion 源码、脚本、文档读取与符号定位。
- vendored 依赖版本/provenance/hash 核对。
- 既有 build 产物时间戳/存在性检查。

未执行：

- MSBuild/native 构建。
- npm/vitest/tsc/vite。
- package-release。
- 游戏启动/部署/控制。
- WebView2 runtime 实机验收。
- 游戏内视觉/热键/拖放/播放验收。

现有 build/test 产物不能证明当前未提交源码通过：`src/ManagerBridge.cpp` 最后写入时间晚于部分测试二进制；`build/validation/receipt.json`、`build/tests/report.json`、`build/manager-bridge-tests/report.json` 缺失；`build/WebView2Loader.dll` 缺失。纠偏实施后必须重新跑完整门禁。
