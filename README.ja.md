[简体中文](README.md) | [English](README.en.md) | **日本語**

# GakuLoader

ドキュメント：[Wiki ホーム](wiki/Home.md) · [クイックスタート](docs/QUICKSTART.md) · [全章ナビゲーション](wiki/_Sidebar.md)。

『学園アイドルマスター』（学マス / Gakumas）PC DMM版（Unity 6000.0.x IL2CPP x64）向けの軽量ネイティブDLLローダーです。ゲームルートディレクトリの `version.dll` をプロキシ（偽装）してプロセスに介入し、プラグインのロード、ホットキーのディスパッチ、Unityメインスレッドタスクのスケジューリング、排他Detour Hookリース、および独立したF1デバッグウィンドウを提供します。

ホストはプラグイン間の共有リソース調停のみに専念し、具体的なMod機能はプラグイン側で自律的に実装されます。

> **ドキュメントに関する注意**：現在、詳細なWikiは**簡体字中国語**で作成・管理されています。仕様やAPIが安定した段階で日本語・英語の翻訳版を提供する予定です。

## インストール方法

1. ゲームを完全に終了します。
2. ビルドした `build/version.dll` をゲームルートディレクトリ（`gakumas.exe` と同じ場所）に配置します。
3. プラグインDLLを `GakuLoader/plugins/` フォルダに配置します。

```text
<ゲームルートディレクトリ>/
├── gakumas.exe
├── version.dll                    GakuLoader 本体
└── GakuLoader/
    ├── Configs/gakuloader.ini     # 宿主配置（可选）
    ├── Logs/gakuloader.log        # 宿主日志
    └── plugins/                   プラグインフォルダ
        ├── GakuMotion.dll
        └── GakuLocalify.dll
```

ゲーム起動後に **F1** キーを押すとGakuLoaderコントロールウィンドウの表示・非表示を切り替えられます。右上の「×」ボタンを押すとタスクトレイに最小化され、トレイアイコンの右クリックメニューから再表示できます。

## 設定方法

`GakuLoader/Configs/gakuloader.ini` でプラグインの有効化/無効化、キー割り当ての変更、コンソールの起動などを設定できます。ファイルが存在しない場合はすべてデフォルトで動作します：

```ini
[plugins]
; 特定のプラグインを無効化（大文字小文字を区別しません。省略時は有効）
SomePlugin.dll=false

[hotkeys]
; 旧形式：<pluginId>.<actionId>=<キー>（一意に一致する場合のみ適用）
gakumotion.show-gui=Ctrl+Shift+M
; 空欄にすると割り当てを解除します
someplugin.action=

[host]
; true に設定すると、起動時に独立したコンソール黒ウィンドウを表示しログを出力します
console=false

[diagnostics]
; true に設定すると、クラッシュ時に crash.log を記録します
trace=false
```

キーの変更には **Manager → Hotkeys** を使用してください。保存される項目は ID 内のドットと大文字小文字を区別し、旧形式より優先されます。「デフォルトに戻す」はプラグインが現在指定する既定値に従います。旧項目は保持され、複数の操作に一致する場合は無視されてログに記録されます。詳しくは [Hotkeys](wiki/Hotkeys.md) を参照してください。

ログはデフォルトで `GakuLoader/Logs/gakuloader.log` に出力されます。設定ファイルの手動編集やプラグインの追加・削除後はゲームを再起動してください。Manager で即時反映と明示された設定はその場で適用されます。プラグインのホットリロードには非対応です。

## 現在対応しているプラグイン

- **GakuMotion**：対応済み（モーション・カメラアニメーション駆動）
- **GakuLocalify**：対応済み（日本語/多言語ローカライズ・テキスト置換）
- **自作プラグイン**：現在の `include/GakuPlugin.h` に準拠し、`GakuPlugin_Query` をエクスポートする任意のx64 DLL。

旧インターフェース（`GmlPlugin_Query` / `GakumasModPlugin.h`）でビルドされた古いDLLは読み込めません。新しいヘッダーファイルを使用して再ビルドしてください。

## プラグイン開発の始め方

完全な [最小プラグインのソース](examples/minimal-plugin/MinimalPlugin.cpp) を使用してください。MSVC と現在のヘッダーだけでビルドでき、MinGW のテスト用 DLL は不要です：

```powershell
.\examples\minimal-plugin\build.ps1
.\examples\minimal-plugin\verify.ps1
```

検証は合成ホストでクエリ、アクティベーション、ライフサイクルのログを確認します。ゲームは起動せず、実機検証の代わりにはなりません。

詳細なライフサイクル、スレッドモデル、API仕様はWikiの [Writing a Plugin](wiki/Writing-a-Plugin.md) を参照してください。

## ビルド方法

「C++ によるデスクトップ開発」と Windows SDK が必要です。VS 2022 / MSVC v143 と VS 2026 / MSVC v145 をサポートします。スクリプトはインストール済みツールセットを自動選択し、`-PlatformToolset v143` で指定もできます：

```powershell
# Release ビルド（build/version.dll に出力）
.\build.ps1

# ドキュメント、ABI、最小プラグインの検証
.\tools\test-docs.ps1
```

> ホストは `/MT`（静的CRT）が必須です。また、Control Flow Guard（`/guard:cf`）を有効にするとMinHookの動的トランポリンがブロックされてクラッシュするため、無効にする必要があります。MinGWによるホストのビルドには非対応です。

ビルドはローカルの成果物を生成するだけです。`-OutputDll` は明示した候補パスへコピーし、ゲームディレクトリの自動検出や自動配置は行いません。全テストと任意の MinGW 依存は [Building](wiki/Building.md) を参照してください。

ビルドに必要な ImGui と MinHook のソースを同梱しており、サブモジュール初期化は不要です。Loader と機能プラグインは対応する配布パッケージを組み合わせてください。

## 詳細ドキュメント (Wiki)

実装の詳細、API仕様、トラブルシューティングはWikiに記載されています（※現在は簡体字中国語のみ）：

- [Installation](wiki/Installation.md) - インストール、バックアップ、アンインストール、デプロイの責務
- [Configuration](wiki/Configuration.md) - 設定項目の詳細とサポートするキー構文
- [Writing a Plugin](wiki/Writing-a-Plugin.md) - 最小プラグイン実装とコンパイル上の注意点
- [Plugin API](wiki/Plugin-API.md) - ライフサイクルコールバックと提供サービス
- [Hooks](wiki/Hooks.md) - 排他MinHookリースと共有LateUpdate参加者チェーン
- [UI](wiki/UI.md) - 独立D3D11ウィンドウとImGui描画
- [Hotkeys](wiki/Hotkeys.md) - 物理キー判定、フォーカス制御、競合解決
- [Building](wiki/Building.md) - コンパイラオプション、成果物の分離、テストスイート
- [Troubleshooting](wiki/Troubleshooting.md) - ログ診断とゲーム終了時のウォッチドッグ挙動
