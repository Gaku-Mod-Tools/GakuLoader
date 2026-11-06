# GakuLoader data root and explicit migration

The proxy `version.dll` stays beside `gakumas.exe`. On a fresh installation the host discovers plugins only in `GakuLoader/plugins/`, reads its optional settings from `GakuLoader/Configs/gakuloader.ini`, and writes its log to `GakuLoader/Logs/gakuloader.log`. `gakuloader.runtime.localRoot` is `GakuLoader`; the paths service derives plugin-owned `Configs`, `PluginData`, `UserData`, `Logs`, `Cache`, and `Temp` categories from this root. `tools` is also under this root. The C ABI itself is unchanged.

The loader does not automatically scan, copy, delete, or migrate old `gakumas-local` data or the old root INI. To migrate an existing setup, close the game, make an independent backup, and explicitly copy a reviewed `gakuloader.ini` into the new `Configs` directory and intended plugin DLLs into the new `plugins` directory. Do not merge or overwrite existing files without review. Plugin data and legacy Localify assets must follow each plugin's own documented migration procedure; a host root change alone does not transfer or select a plugin's data. Retain the original data until the migrated installation has been verified.

`GakuBurstBridge.dll` and `GakuMotion.VmdImporter.Native.dll` are Motion auxiliary libraries, not host plugins. They may reside in `plugins` but are excluded from plugin discovery and loaded only by their consumer.
