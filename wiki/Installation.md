# 安装与部署

本页说明 GakuLoader 的安装目录要求、文件放置、配置备份、升级与卸载步骤。

---

## 安装步骤

安装前请确认游戏已完全退出。

1. 确认游戏根目录（包含 `gakumas.exe` 的文件夹）。
2. 如果游戏根目录下已有其他工具的 `version.dll`，请先备份并移走该文件。
3. 将配套的 `version.dll` 放置在与 `gakumas.exe` 同级的根目录下。
4. 在游戏根目录下创建 `GakuLoader/plugins/` 目录，将所需插件的 DLL 文件放置在该目录中。

安装后的标准文件布局如下：

```text
<游戏根目录>/
├── gakumas.exe
├── version.dll                    GakuLoader 核心代理文件
└── GakuLoader/                 Mod 与插件工作目录
    ├── Configs/gakuloader.ini     # 宿主配置（可选）
    ├── Logs/gakuloader.log        # 宿主日志
    └── plugins/                   插件存放目录
        ├── GakuMotion.dll         示例插件：动作播放
        └── GakuLocalify.dll       示例插件：界面汉化
```

启动游戏后，按 **F1** 键可以呼出 GakuLoader 桌面控制窗口。同时可以查看 `GakuLoader/Logs/gakuloader.log`，确认插件是否正常出现 `Activated` 激活日志。

---

## 升级与配置保留

升级 GakuLoader 或插件时，请先完全退出游戏：

1. 保留现有的 `gakuloader.ini` 配置文件，避免自定义的外观、插件开关或修改后的快捷键被重置。
2. 保留 `GakuLoader/` 下的插件个人数据与配置文件。
3. 替换游戏根目录下的 `version.dll` 或 `GakuLoader/plugins/` 下的插件 DLL。
4. 重新启动游戏完成升级。若遇到异常，将备份的文件换回即可恢复。

---

## 卸载

卸载 GakuLoader 只需要退出游戏，并将游戏根目录下的 `version.dll` 删除或移走即可：

- 移走 `version.dll` 后，游戏恢复为原版启动状态，不再加载任何 Mod 插件。
- `GakuLoader/` 目录中存放的是日志、配置文件与插件文件，可以根据需要保留或直接删除。
- 如果安装前有备份其他工具的原版代理 `version.dll`，卸载后将其恢复至游戏根目录。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
