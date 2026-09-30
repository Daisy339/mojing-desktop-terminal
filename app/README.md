# O'Clock Windows / Android App

App 工程与构建输出都保存在本工程 `app/` 内。App 连接 ESP32 的 IP，不是电脑语音网关 IP。当前源码仍需实机联调。

## Windows 路径修复

Windows 插件需要符号链接权限。若出现 `Building with plugins requires symlink support`，运行 `start ms-settings:developers`，在设置中开启“开发者模式”。本项目脚本不会自动修改此系统设置。

Flutter Windows 构建不接受父目录名中的英文撇号。使用 `subst` 创建原目录的盘符别名，文件不会复制、搬家。PowerShell 中执行：

```powershell
subst O: "D:\all project\esp\o'clock project"
cd O:\app
flutter pub get
flutter analyze
flutter test
flutter run -d windows
```

O: 对应原工程根目录；O:\app 就是原来的 app。电脑重启后通常需要重新映射。使用 `subst` 查看当前映射；如果已经指向同一工程，不需要再次创建。若 O: 被其他磁盘占用，请选择其他空闲盘符，不能覆盖或删除不明映射。

也可在原 app 目录运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tooling\flutter-safe.ps1 run -d windows`。这个入口会先检查 O: 的目标，映射原工程，再从 O:\app 调用 Flutter。它不会改动 ESP32 固件或执行烧录。

Windows/Android 平台文件已生成。仅当平台文件缺失时使用 `tooling/bootstrap-platforms.ps1`；禁止使用 `flutter create --overwrite`，这会用示例替换 App 源码和依赖配置。

## 开发环境和产物

若 Windows 启动时报 `C1041`（无法打开 `vc143.pdb`），runner 的 CMake 配置已增加 MSVC `/FS` 协调 PDB 写入。关闭失败的启动窗口后，只启动一个 App 入口重试。若仍失败，关闭运行中的 App 和构建窗口，在 app 目录先通过 `tooling/flutter-safe.ps1 clean` 清理生成文件，再启动。配置变更需要下一次 Windows 构建验证，不代表已完成实机验证。

工具安装由用户操作。Windows 需要 Flutter SDK、Visual Studio 的 Desktop development with C++ 工作负载。Android 需要 Android Studio 及 SDK 工具链。用 `flutter doctor -v` 检查，用 `flutter doctor --android-licenses` 阅读并接受所需许可。

在映射后的路径运行 `flutter devices`；Android 手机开启 USB 调试后执行 `flutter run -d <设备ID>`。构建命令为 `flutter build windows` 或 `flutter build apk --release`。构建输出仍位于原工程的 `app/build/`。Windows 需保留整个 Release 目录，不能只复制 exe。

Android APK 位于 `app/build/app/outputs/flutter-apk/app-release.apk`。可复制到手机后直接安装；手机安装时按系统提示允许当前文件管理器安装应用。安装后，手机与 ESP32 需在同一网络，App 中填写 ESP32 的 IP。手机不需要安装 Flutter。语音对话仍需要电脑语音网关运行。

## 当前功能

### Android 构建兼容

`file_picker` 8.3.7 使用 API 34，其解析出的 lifecycle 依赖要求 API 36。
`android/file-picker-compat.gradle` 在插件 DSL 配置完成时将该插件的 compileSdk 提至至少 36；不修改全局 Pub 缓存，不改变 minSdk/targetSdk。
`android/gradle.properties` 关闭 Kotlin 增量编译，避免 C: 插件源文件与 O: 工程之间的 `different roots` 缓存错误。
首次应用此修复，请关闭正在运行的 App，然后在 app 目录依次运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tooling\flutter-safe.ps1 clean
powershell -NoProfile -ExecutionPolicy Bypass -File .\tooling\flutter-safe.ps1 pub get
powershell -NoProfile -ExecutionPolicy Bypass -File .\tooling\flutter-safe.ps1 build apk --release
```

以上构建由用户执行；配置修改后仍需要重新打包验证。

设备状态、Wi-Fi/AP 配网、天气城市、待办新增/编辑/完成恢复/删除、图片/GIF 转黑白帧上传（最多 8 帧）、帧间隔、相册清空。数据读取复用原网页的表单结构，固件 HTML 若改变，需要同步适配解析器。局域网直连，不支持外网远程控制；语音网关仍由 `gateway/server.py` 运行。
