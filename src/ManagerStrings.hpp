#pragma once
#include "LanguageSettings.hpp"
#include <string>

namespace GakuLoader::ManagerStrings {
inline std::string FormatVersion(const std::string& version) {
    return !version.empty() && version[0] >= '0' && version[0] <= '9' ? "v" + version : version;
}
inline const wchar_t* OpenControls() {
    static const wchar_t* const labels[] = {L"Open GakuLoader", L"打开 GakuLoader", L"開啟 GakuLoader", L"GakuLoader を開く"};
    return labels[LanguageSettings::Current()];
}
enum class Key {
    Manager, PluginList, Appearance, Hotkeys, Settings, Plugin, Version, State, Panel,
    Ready, Activated, Inactive, Failed, Yes, No, Waiting, Running, Stopping,
    Theme, Scale, Languages, Dark, Sapphire, Paper, LanguageHelp, NoHotkeys,
    Action, Name, Combo, Active, Conflict, Unbound, Edit, Reset, Unbind, Cancel, Apply,
    Capture, CaptureHelp, CaptureButton, InvalidCombo, SaveFailed, HotkeyHelp,
    Console, Trace, NextStart, PluginEnable, PluginEnableHelp, File, Hide, View, Plugins,
    SelectPanel, FailedHelp, NoPanel, Count, Runtime, SummaryHelp, ConfigFile, Saved,
    DefaultBinding, CustomBinding, ScaleLimit, AppleLight, AppleDark, CountKeys
};
inline const char* Text(Key key) {
    static const char* const strings[][4] = {
        {"Manager", "管理器", "管理員", "マネージャー"},
        {"Plugin List", "插件列表", "外掛程式清單", "プラグイン一覧"},
        {"Appearance", "外观", "外觀", "外観"},
        {"Hotkeys", "快捷键", "快捷鍵", "ホットキー"},
        {"Settings", "设置", "設定", "設定"},
        {"Plugin", "插件", "外掛程式", "プラグイン"},
        {"Version", "版本", "版本", "バージョン"},
        {"State", "状态", "狀態", "状態"},
        {"Panel", "面板", "面板", "パネル"},
        {"runtime ready", "已就绪", "執行階段就緒", "ランタイム準備完了"},
        {"activated", "正在激活", "已啟用", "有効化済み"},
        {"not activated", "未激活", "未啟用", "未有効化"},
        {"failed", "失败", "失敗", "失敗"},
        {"yes", "有", "有", "あり"}, {"no", "无", "無", "なし"},
        {"waiting for IL2CPP", "等待 IL2CPP", "等待 IL2CPP", "IL2CPP を待機中"},
        {"running", "运行中", "執行中", "実行中"},
        {"shutting down", "正在退出", "正在結束", "終了処理中"},
        {"Theme", "主题", "主題", "テーマ"},
        {"UI Scale", "界面缩放", "介面縮放", "UI スケール"},
        {"Languages", "语言", "語言", "言語"},
        {"Dark", "深色", "深色", "ダーク"},
        {"Sapphire", "蓝宝石", "藍寶石", "サファイア"},
        {"Paper & Ink", "纸与墨", "紙與墨", "紙とインク"},
        {"Applies immediately to the loader and plugins supporting the language service.", "立即应用到 Loader 和支持语言服务的插件。", "立即套用至 Loader 與支援語言服務的外掛程式。", "Loader と言語サービス対応プラグインにすぐ反映されます。"},
        {"No plugin hotkeys registered.", "尚无插件注册快捷键。", "尚無外掛程式註冊快捷鍵。", "登録されたホットキーはありません。"},
        {"Action", "操作", "動作", "アクション"}, {"Name", "名称", "名稱", "名前"},
        {"Combo", "组合键", "組合鍵", "キーの組み合わせ"},
        {"active", "生效", "生效", "有効"}, {"conflict", "冲突", "衝突", "競合"},
        {"unbound", "未绑定", "未綁定", "未割り当て"},
        {"Edit", "修改", "修改", "変更"}, {"Reset", "恢复默认", "還原預設", "既定に戻す"},
        {"Unbind", "解除绑定", "解除綁定", "割り当て解除"},
        {"Cancel", "取消", "取消", "キャンセル"}, {"Apply", "应用", "套用", "適用"},
        {"Rebind hotkey", "重新设置快捷键", "重新設定快捷鍵", "ホットキーの変更"},
        {"Type a combo (Ctrl+M), or capture keys. F1 is reserved. Esc cancels capture.", "输入组合键（Ctrl+M）或录入按键。F1 已保留；Esc 取消录入。", "輸入組合鍵（Ctrl+M）或擷取按鍵。F1 已保留；Esc 取消擷取。", "Ctrl+M などを入力するか、キーを記録します。F1 は予約済み。Esc で記録を中止します。"},
        {"Capture keys", "录入按键", "擷取按鍵", "キーを記録"},
        {"Invalid combo or reserved F1.", "组合键无效或使用了保留的 F1。", "組合鍵無效或使用了保留的 F1。", "無効な組み合わせ、または予約済みの F1 です。"},
        {"Could not save the configuration.", "无法保存配置。", "無法儲存設定。", "設定を保存できませんでした。"},
        {"Changes apply immediately and are saved. Conflicting bindings stop dispatching until reassigned. F1 opens or hides the manager.", "修改立即生效并保存。冲突的绑定会暂停触发，直到重新设置。F1 打开或隐藏管理器。", "修改立即生效並儲存。衝突的綁定會暫停觸發，直到重新設定。F1 開啟或隱藏管理員。", "変更はすぐ反映・保存されます。競合したキーは再設定まで無効になります。F1 で管理画面を表示・非表示にします。"},
        {"Show console", "显示控制台", "顯示主控台", "コンソールを表示"},
        {"Diagnostic trace", "诊断跟踪", "診斷追蹤", "診断トレース"},
        {"Takes effect on the next game start.", "下次启动游戏时生效。", "下次啟動遊戲時生效。", "次回のゲーム起動時に反映されます。"},
        {"Enabled plugins", "启用的插件", "啟用的外掛程式", "有効なプラグイン"},
        {"Enable or disable discovered DLLs for the next game start.", "设置已发现 DLL 在下次游戏启动时是否启用。", "設定已找到的 DLL 在下次遊戲啟動時是否啟用。", "検出された DLL の次回起動時の有効・無効を設定します。"},
        {"File", "文件", "檔案", "ファイル"}, {"Hide controls", "隐藏窗口", "隱藏視窗", "ウィンドウを隠す"},
        {"View", "视图", "檢視", "表示"}, {"Plugins", "插件", "外掛程式", "プラグイン"},
        {"Select the manager or a plugin from the navigation.", "从左侧选择管理器或插件。", "從左側選擇管理員或外掛程式。", "左側から管理画面またはプラグインを選択してください。"},
        {"This plugin failed. Consult the loader log.", "插件运行失败，请查看 Loader 日志。", "外掛程式執行失敗，請查看 Loader 記錄。", "プラグインが失敗しました。Loader のログを確認してください。"},
        {"This plugin has no desktop control panel.", "此插件没有桌面控制面板。", "此外掛程式沒有桌面控制面板。", "このプラグインには操作パネルがありません。"},
        {"Plugins", "插件数", "外掛程式數", "プラグイン数"},
        {"Runtime", "运行时", "執行階段", "ランタイム"},
        {"Registered plugins and their current lifecycle state.", "已注册插件及其当前生命周期状态。", "已註冊外掛程式及其目前生命週期狀態。", "登録プラグインと現在のライフサイクル状態です。"},
        {"Config file", "配置文件", "設定檔", "設定ファイル"},
        {"Saved", "已保存", "已儲存", "保存済み"},
        {"default", "默认", "預設", "既定"}, {"custom", "自定义", "自訂", "カスタム"},
        {"This UI scale and DPI combination exceeds the font atlas limit. The previous scale is retained.", "此界面缩放与 DPI 组合超出字体贴图上限，已保留之前的缩放。", "此介面縮放與 DPI 組合超出字型貼圖上限，已保留先前的縮放。", "UI スケールと DPI の組み合わせがフォント上限を超えたため、以前の倍率を維持します。"},
        {"Apple Light", "Apple 浅色", "Apple 淺色", "Apple ライト"},
        {"Apple Dark", "Apple 深色", "Apple 深色", "Apple ダーク"}
    };
    static_assert(sizeof(strings) / sizeof(strings[0]) == static_cast<size_t>(Key::CountKeys));
    return strings[static_cast<size_t>(key)][LanguageSettings::Current()];
}
}
