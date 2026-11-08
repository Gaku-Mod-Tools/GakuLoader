# 插件数据路径

Loader 提供可选 `gakuloader.paths` v1 服务，声明在 `GakuPaths.h`。原有 GakuPlugin ABI v1 的结构与函数签名不变。当前宿主的 runtime `localRoot` 为 `<GameRoot>/GakuLoader`，插件从其 `plugins/` 发现；宿主配置与日志分别位于 `Configs/gakuloader.ini` 和 `Logs/gakuloader.log`。宿主不会自动搬迁旧文件；旧数据迁移由用户与插件按各自指南显式处理。

插件查询服务后获得绑定自身 owner 的根目录，不传任意 pluginId：

```cpp
#include "GakuPaths.hpp"

GakuPathsV1 paths{};
const auto status = GakuPaths::Query(host, paths);
if (status != GAKU_OK) return status; // optional-service policy belongs to plugin
std::filesystem::path configRoot;
const auto lookup = GakuPaths::GetRoot(paths, GAKU_PATH_CONFIGS, configRoot);
if (lookup != GAKU_OK) return lookup;
// Lookup is read-only. Create only when there is actually something to write.
const auto created = paths.ensureRoot(paths.context, GAKU_PATH_CONFIGS);
if (created != GAKU_OK) return created;
std::filesystem::path settings;
const auto joined = GakuPaths::JoinRelative(configRoot, L"settings.json", settings);
if (joined != GAKU_OK) return joined;
// Use the UTF-16 path with Win32 W APIs or std::filesystem; save transactionally.
```

默认根为 `<GameRoot>/GakuLoader/<类别>/<descriptor.id>`。只查询不会创建目录；`ensureRoot` 只按需创建被请求的类别/owner 根。无需每插件建立六套空目录。

| kind | 目录 | 用途与清理边界 |
|---|---|---|
| `GAKU_PATH_USER_DATA` | `UserData/<id>` | 用户创作/导入/手改内容和恢复副本，普通升级与卸载保留 |
| `GAKU_PATH_CONFIGS` | `Configs/<id>` | 用户设置、校准，保留；安装包只带 example，不能覆盖活动配置 |
| `GAKU_PATH_PLUGIN_DATA` | `PluginData/<id>` | 持久状态、受管理资源、索引与迁移记录，默认保留 |
| `GAKU_PATH_CACHE` | `Cache/<id>` | 可证明无损重建的缓存，不放用户修改 |
| `GAKU_PATH_LOGS` | `Logs/<id>` | 日志/自动诊断，可建 Crash 会话子目录；用户导出成果另存 UserData |
| `GAKU_PATH_TEMP` | `Temp/<id>` | 无恢复价值的中间文件，按会话/任务分目录；未提交事务不能当普通 Temp 清理 |

程序 DLL 和随包只读资源不属于 PluginData。`GakuLoader/plugins/` 是插件发现目录；`GakuBurstBridge.dll` 和 `GakuMotion.VmdImporter.Native.dll` 是伴生库，由 GakuMotion 按需加载，不由 Loader 当作插件激活。路径服务本身不更改依赖搜索或加载顺序。

## C ABI 与错误合同

服务精确名称为 `GAKU_SERVICE_PATHS`、版本为 `GAKU_PATHS_VERSION`。所有根都是启动时固定的绝对 UTF-16 Windows 路径，不依赖之后的 CWD。`getRoot` 容量和 required 以 wchar_t 为单位并包含 NUL：

- `output=NULL, capacity=0` 测长返回 `GAKU_E_SIZE` 并仅更新 required；非空 output 容量不足也只更新 required，不写半条路径。
- 缓冲足够返回 OK、完整 NUL 结尾路径及 required。required 必填；NULL output 配非零 capacity 或无效 kind 返回 INVALID_ARGUMENT；此类错误、无效 namespace 与其他失败都不改变输出。
- 路径服务的查询/get/ensure 在 owner 退役后返回 SHUTTING_DOWN。目录创建与退役按 owner 串行协调；已开始创建会在退役返回前结束，退役后不再开始。
- ensureRoot 幂等；权限不足、同名普通文件或其他文件系统错误返回 INTERNAL，并在 Loader 私有日志记录 owner、类别和 OS error。不会悄悄改写到旧根或 AppData。
- 服务表自身仍遵守现有 CopyServiceTable 合同：错版本/小 buffer 不写任何表输出；大 buffer 只写表自身大小。没有 STL、异常或内存所有权穿过 C ABI。

pluginId 必须是稳定 ASCII 小写、1–64 字符，首字符为 a-z/0-9，随后只用 a-z/0-9/点/下划线/连字符；点分段不得为空，禁止 Windows 设备名 stem（`con`、`con.txt`、`com1` 等）。不做自动小写或字符替换：`foo` 与旧 ID `Foo` 不会映射同一个可写目录，后者仅路径服务返回 INVALID_ARGUMENT，仍可用原 ABI 装载并查询其他服务。开发者应显式选定稳定 ID；已有用户数据的 ID 更名要提供迁移。

`GakuPaths.hpp` 提供 Query/GetRoot，以及严格 `FromUtf8/ToUtf8` 和 `JoinRelative`。根和 Win32 文件 I/O 用 UTF-16；文本/跨语言边界用 UTF-8，禁止经 CP_ACP 往返。JoinRelative 只帮助合作插件排除相对跳转、设备名与明显非法文件名；不处理外部链接/ZIP 的信任边界，也不是文件系统沙箱。Loader 不拦截插件自行 fopen/Win32 I/O。

## 旧布局与迁移

查不到服务时返回 UNAVAILABLE。尚未迁移的安装可按插件既有合同继续旧布局；已完成新布局迁移的安装不能因换了旧 Loader 而静默读写旧残留。插件需用固定可发现状态或最低宿主版本要求阻止这种回退，并提示升级宿主。新根的权限/I/O 失败也不能触发旧根 fallback。

新服务只分配路径，不证明数据已经迁移。插件应区分全新安装和已有数据：全新安装可按自身合同初始化空布局；迁移已有数据须显式执行并保留原件，明确冲突与唯一 active-layout 切换点，不得仅凭目录存在宣布迁移完成。迁移锁必须覆盖普通配置保存/更新器，或让首次迁移前所有 writer 保持静止。已完成后不因 converter/插件版本变化重新导入上游；每次普通启动只写当前权威布局。

本阶段不提供自动 DataRoot 搬迁、安装 ID、统一 receipt、清理器或全局文件 Hook。不要把使用本 API 视为真实迁移已经完成。

## 本地验收

```powershell
.\tests\build-tests.ps1 -SkipFixtures
.\tests\run-all.ps1 -SkipBuild -SkipFixtures -Only paths,docs,frame-pump
```

paths 使用 MSVC 编译的合成旧 ABI 插件和独立测试安装树，不需要 MinGW、游戏或真实用户数据。具体通过范围及未运行矩阵见本阶段交接。
