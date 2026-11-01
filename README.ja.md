# GakuLoader

学園アイドルマスター PC DMM 版向け Windows x64 ネイティブプラグインホストのソースです。バージョンプロキシ、ライフサイクル、メインスレッドタスク、Hook 所有権、ホットキーと F1 管理画面を提供します。

独立した固定ソーススナップショットであり、ビルド済みインストールパッケージは含みません。既定の作業範囲はローカルビルドと合成されたヘッドレス検証のみで、ゲームの配置・起動・操作は行いません。

Visual Studio の C++ デスクトップ開発（MSVC v143/v145）と Windows SDK が必要です。必要なサードパーティソースを同梱し、サブモジュールや隣接リポジトリは不要です。

```powershell
.\tools\validate-source.ps1
```

Release/Debug と MSVC テストをビルドし、文書、フレームポンプ、C ABI、MinHook、最小プラグインとローカル DLL を検証します。出力とログは除外対象の `build/` に保存します。
MinGW、実際のデスクトップ画面とゲームの検収は既定では実行しません。

[ビルド](wiki/Building.md) · [プラグイン](docs/PLUGIN_DEVELOPMENT.md) · [選定記録](STAGING_REVIEW.md) · [ハッシュ](SOURCE_MANIFEST.json) · [ライセンス](THIRD_PARTY_NOTICES.md)

GakuMotion.dll、GakuLocalify.dll などの機能プラグインは含みません。第一者コードは提供元の MIT 表記を保持し、依存コードのライセンスと既存パッチは別途記録しています。

[简体中文](README.md) · [English](README.en.md)
