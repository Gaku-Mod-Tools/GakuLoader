# GakuMotion Web 化实施与测试报告（2026-11-15）

## 1. 结果摘要

- GakuLoader Manager 恢复并常驻 ImGui：Sidebar、菜单栏、插件管理、外观、热键、设置、状态栏全部保留原生实现。
- WebView2 仅作为插件内容区宿主：选中提供 `web-panel` 服务的插件（当前为 GakuMotion）时，在 `##PluginContent` 内容区渲染 React；其他插件继续 `onDesktopUi` ImGui 路径。
- 失败回退一律发生在 Manager 内容区（ImGui 插件面板/占位），不回退游戏窗口；未修改任何游戏渲染 Hook。
- 公共插件 ABI 未修改：两仓库 `include/GakuPlugin.h` 对基线零差异；新增的是可选 provider-owned 服务 `gakumotion.web-panel/v1`（`GakuWebPanel.h`，additive，旧插件零影响）。
- GakuMotion React 前端已建立并接入受控 Native Bridge；播放/曲库/导入/镜头/舞台/设置/Ensemble/骨骼微调/场景节点/Developer 全部功能页语义已迁移，仅 legacy dormant tabs 待产品决策。

## 2. 工作树与保护

| 仓库 | 工作树 | 分支 | 状态 |
|---|---|---|---|
| GakuLoader | `git-work\GakuLoader\worktrees\webview2-ui` | `feat/webview2-manager-ui` | 改动未提交（含纠偏前原型，全部保留） |
| GakuMotion | `git-work\GakuMotion\worktrees\web-ui` | `feat/web-ui`（基于 dev `594bf3b`） | 提交 `f44fe9d` + 资产路径修正未提交 |

- 纠偏前保护快照：`build\protection\20261115-scope-correction\`（HEAD、status、tracked.patch、75 个 untracked 源文件副本）。
- 未触碰 GakuMotion `dev`/`main`/`fix-issue-5` 工作树；`fix-issue-5` 的导入模态语义（550ms 最短显示/250ms stale-success/播放中编辑保护）已按 React 导入流程实现并经 vitest 验证。
- Loader Manager React 原型未删除：隔离为 `frontend/src/prototype/LoaderPrototypeApp.tsx`，仅 `?prototype=1` 可达。

## 3. 架构要点

- `src/WebView2Ui.*`：参数化 assetsDir/entryPath/virtualHost/userDataDir；内容区 bounds、DPI raw-pixel/rasterization、focus/Tab 离开、resize/minimize/hide、`ProcessFailed`、generation、Close/Shutdown；headless seams。
- `src/DesktopUi.cpp`：WorkerBody 始终渲染 ImGui shell；DrawHostShell 计算内容区物理矩形；web panel ready 时跳过该插件 ImGui 绘制但调用 `onUiTick`；ImGui popup 打开时隐藏 WebView 避让 airspace；`[host] ui=imgui` 全局禁用插件 web panel。
- `src/PluginHost.cpp` / `src/Host.hpp`：loader-internal `QueryWebPanelService(pluginId)`，按 `<id>.web-panel/v1` 查询并校验 owner/accepting/版本/大小/函数指针；热键 poller 将宿主子 HWND 视为宿主前景。
- `src/ManagerBridge.*`：受控 raw request 转发（校验 envelope/response id/type），事件 envelope 原样透传；Loader 不持有 Motion 领域语义。
- GakuMotion `src/GkWebPanelBridge.hpp` + `GakuMotionPlugin.cpp`：发布可选服务；复用 `Core::QueueGui`/`GuiAction`/revision/token 守卫；长任务入队 Unity 主线程；`getAssets` 优先 `runtime.gameRoot/GakuMotion.UI`，回退插件模块目录，双缺返回 UNAVAILABLE；`onShutdown` 在 UI 线程发 `bridge.closed`，Unity 线程 shutdown 仅静默失效。
- 前端 `frontend/src/motion/`：MotionApp/Sidebar/StatusBar/PermissionGate/TransportBar/页面；capability 协商（`motion.hello.supportedMethods`），未实现方法显示“待迁移/不可用”且不发请求；dev mock 有醒目横幅。

## 4. 功能迁移状态

### 已迁移（native + React，离线契约验证）

- Shell：页面导航、顶部状态条、权限降级页（含权限丢失丢弃 scrub）、主题/缩放跟随。
- TransportBar：scrub 三段式、播放/暂停/继续/停止/上下曲（ensemble 禁用）、播放模式、倍速、音量 commit 保存、镜头快捷模式 pending→ack、帧步、时间/帧定位、告警行。
- Library：表格三态标记、搜索、刷新、打开文件夹、重载当前曲目、单击选中/双击播放、删除确认（revision+key+directory 守卫）。
- Motion：驱动角色 token+epoch、表情幅度 4 滑条+保存、初始位置 ±10m+保存。
- Camera：模式/分模式 transform/跟随/高级参数、草稿-expected-pending、保存。
- Stage：光影/背景/预置色（光影→相机阴影联动保留在 native）。
- Settings：物理后端、快捷键表、开发者模式（会话态）。
- 导入管线：导入模态（四槽候选/选择、槽位速度与偏移、探测徽章、活动曲目告警、停止并保存）、追加文件/浏览（dialog.pickFile）、路径探测（probeFiles）、导入进度模态（550ms 最短显示/250ms stale-success/播放中编辑保护）、拖放三态视觉（drop_overlay）与管理选中入口。
- Ensemble：三槽编辑器（启用/演员 token/动作 key/偏移/朝向）、预设、草稿校验与 apply。
- 骨骼微调：51 骨 6 组树、Inspector（pitch/yaw/roll/世界坐标/对称联动）、归零/恢复默认/全部重置/保存至当前曲目或全局。
- 场景节点树：scanStageNodes/toggleStageNode、父级未激活禁用提示。
- Developer 六子 tab：实验与 Gaze（直写语义按桥命令）、遥测与诊断、鞋底测量 v2、Pitch Demo、摄影舞台替换、布光与视频。

### 待迁移（前端明确标记，native 返回 not_implemented）

- legacy dormant tabs（旧镜头 VMD 导入/阴影实验/旧列表/配置面板）去留仍待产品决策。

## 5. 测试结果（全部离线）

| 检查 | 结果 |
|---|---|
| Loader `build.ps1` Release / Debug | PASS |
| Loader `tests/build-tests.ps1 -SkipFixtures`（MSVC v145） | PASS |
| Loader `tests/run-all.ps1 -SkipBuild -SkipFixtures` | PASS_WITH_SKIPS：13 通过 / 4 跳过（MinGW 缺失） |
| manager-bridge | 1514 checks PASS |
| web-panel-host | 25 checks PASS |
| Manager UI / Desktop UI WARP、docs、paths、dialogs、file-drop、wndproc、fonts、appearance、frame-pump、language | PASS |
| c-abi-test / minhook-isolation | PASS |
| Motion `tests/run_web_panel_tests.ps1` | 285/285 PASS（23 组：import/probe/dialog/drop、ensemble、bone、gaze、stage nodes、experiments/telemetry、sole/solePitch/studio、drop_peek 等） |
| Motion `build.ps1 -Target Runtime` | PASS（`build/native/GakuMotion.dll`） |
| Motion `build.ps1 -Target Contracts` | 80/80 PASS |
| Frontend `tsc -b --noEmit` | PASS |
| Frontend `vitest run` | 10 文件 / 74 用例 PASS |
| Frontend `build.ps1 -Dependencies Skip` | PASS（离线资产门禁，dist 3 文件） |
| `tools/package-release.ps1` | PASS（18 payload，manifest SHA256 003e0cb8…） |
| `tools/test-docs.ps1 -StaticOnly` | PASS |
| ABI / GameHook 零差异 | 已核验 |

打包产物：`build\release\GakuLoader-WebView2-20261115-132117-b59559a3\`（version.dll + WebView2Loader.dll + GakuMotion.UI + 许可/manifest）。

### 验收环境部署（2026-11-15）

- 目标：`F:\Games\gakumas`（gakumas.exe 未运行时执行）。
- 备份：`F:\Games\gakumas\Backup\webview2-acceptance-20261115-20261115-132149\`（旧 `version.dll`、旧 `GakuLoader\plugins\GakuMotion.dll`）。
- 部署：新 `version.dll`、`WebView2Loader.dll`、`GakuMotion.UI\`（游戏根）、新 `GakuMotion.dll`（`GakuLoader\plugins\`）；全部 SHA256 与源一致。
- 未触碰并核验不变：GakuLocalify.dll、GakuVRify.dll、RinhaReplacement.dll、GakuBurstBridge.dll、GakuMotion.VmdImporter.Native.dll 及用户配置/曲库。
- 记录：`D:\Database\Gakumas Develop\build\webview2-acceptance-20261115\DEPLOYMENT.json`（`game_loaded_verified:false`）与 `ACCEPTANCE_CHECKLIST.md`。

## 6. 未验证 / 必须人工验收

- 真实 WebView2 Runtime 下的 Manager 联调：内容区定位、resize、多显示器 DPI、uiScale、最小化/托盘/F1。
- 焦点/IME/Tab 回 ImGui、热键抑制（当前按 WebView 获得焦点处理，非 DOM editable 精确上报）。
- 子 HWND airspace：ImGui popup 避让策略实机观感；OLE 拖放是否被子 HWND 吞掉（drop 未 advertise，ImGui 路径保留拖放）。
- GakuMotion 实机：播放/音画同步/停止恢复、物理、骨骼/表情视觉、镜头/光影/绿幕、ensemble、舞台替换、场景节点。
- GakuLocalify 等旧插件 Manager 内 ImGui 回归（离线 WARP 通过，实机未验）。
- renderer crash/缺 Runtime/缺 assets 的实机回退路径。

## 7. 已知缺口与后续

- MinGW 缺失导致 host/diagnostic-boundary/host-dialog-concurrency/desktop-ui-cross-dll 4 套件跳过；HostTests 新增断言已编译未执行。
- 前端依赖（React/Radix/lucide/Tailwind 等）许可 notices 尚未纳入发布包；发布前必须补齐。
- 旧 `docs/ui-migration-matrix.md` / `docs/native-bridge-api.md` 仍描述 Loader Web 原型口径，已被 `webview2-migration-notes.md` 指向纠偏审计；后续需重写为 Motion 口径。
- Loader 工作树改动未提交；Motion 资产路径修正未提交。是否提交/合并由用户决定。
- `fix-issue-5` 未合并；React 导入流程实现时必须对齐其全局进度模态/播放中编辑保护/uiScale 语义。
