# Convertii

原生 Win32 视频、音频与静态图片处理工具。使用系统控件和紧凑参数页，支持资源管理器右键、批量处理和逐文件覆盖。**0.2.0 起主程序、处理核心、安装程序均为 C++，发行版不需要 .NET。**

## 使用

直接打开 Convertii.exe，选择或拖入文件。右键菜单的“压缩大小”“格式转换”也会打开同一窗口。

- **压缩 / 转换**：按每文件大小上限、质量预设或无重编码换封装处理。1 MB = 1,000,000 字节。静态图片自动使用图片格式。
- **剪辑 / 画面**：时间轴定位、起止点、精确/关键帧截取、裁剪、旋转、翻转、缩放、帧率、变速和常用滤镜。各页配置同一个任务，无需反复输出中间文件。
- **音频**：视频提取音频、独立音频转换、音轨选择、码率、采样率、声道和增益。输出 MP3、M4A/AAC、FLAC、WAV。音频独立输出一次选择一条音轨。
- **字幕**：内嵌文本字幕及外部 SRT/ASS 的提取、封装、烧录和偏移。截取/变速同步调整事件时间；格式转换可能丢样式时需要确认。PGS/VobSub/OCR 尚不支持。
- **拼接**：列表上下移动调整顺序，逐文件设置片段范围；快速模式检查兼容性，统一转码保持比例并补黑边，无音轨片段补静音。输出一路视频及至多一路音频，字幕不自动合并。统一转码结果可选择大小上限。
- **任务 / 设置**：语言、大小预设、硬件检测、右键注册/移除和处理详情。

默认输出到原文件旁的 Convertii 子目录，也可选择统一目录。重名自动编号，原文件保留。任务入队时冻结参数；在运行期间修改表单不会改变该任务。

“单独设置 / 确认”切换到所选文件的参数，完成后点击“保存单独设置”。目标过小、HDR、JPEG 透明度、字幕样式转换或拼接需要确认时，任务暂停等待用户修改并重新入队，其余任务继续。

严格大小的视频使用软件两遍编码，并按编辑后的时长计算音频/封装预算；最多执行三轮完整编码，最后检查实际字节数。无法满足上限不会标记成功，不会截断视频或擅自降低分辨率/帧率。PNG、FLAC、WAV 无损输出无法达到目标时，应修改格式、尺寸或目标。质量预设不保证固定大小。

## 播放与预览

剪辑页提供内嵌原片播放、暂停、拖动定位、标记范围和选区播放。播放器按需创建，离开该页或开始正式处理后释放。

原片无法由 Windows 解码时，生成当前位置最长 30 秒的 MP4 代理片段，时间轴仍使用源时间。“效果预览”复用正式处理链，生成最长 10 秒、最高 720p 样片；不代表最终大小或最终分辨率。拖动会取消过期请求，可用取消按钮停止生成。正式任务运行时不生成新预览。缓存上限 512 MiB，旧文件会清理。

播放使用 Windows Media Foundation 与 DirectComposition，不使用 WebView。Windows N 版本需要安装系统 Media Feature Pack。

## 构建与测试

需要 Windows 10 22H2 / Windows 11 x64、Visual Studio 2022“使用 C++ 的桌面开发”、Windows SDK 10.0.26100 和 CMake 3.24+。依赖源码已保存于 third_party，构建不从网络拉取库。

    ./scripts/Build.ps1
    $env:CONVERTII_ENGINE_DIR = 'E:\ffmpeg\bin'
    ./scripts/Test.ps1

主程序：artifacts/native-build/bin/Release/Convertii.exe。CTest 包含真实媒体测试、原生界面/播放/单实例测试、安装安全与回滚测试，以及两类 Shell 的 4,000 文件传递测试。截图和测试数据保存在 artifacts。详细人工验收见 docs/TESTING.md。

引擎依次从 CONVERTII_ENGINE_DIR、程序旁的 engine 目录、PATH 查找。设置位于 %LOCALAPPDATA%\Convertii\settings.json；旧版设置首次迁移时保存 settings.v1.bak。CONVERTII_DATA_DIR 可隔离设置，CONVERTII_CACHE_DIR 可隔离预览缓存。

    Convertii.exe --check-engine report.json

此命令检查引擎并输出报告，不创建窗口。运行时无遥测、自动更新或远程媒体输入。

## 分发

    ./scripts/Publish.ps1 -EngineDirectory 'E:\ffmpeg'

产物位于 artifacts/release：便携 ZIP、Online-Setup.exe、Offline-Setup.exe、release-native.json、SHA256SUMS-native.txt。在线安装器自带同版 ZIP 校验值；配置 -DownloadBaseUri 后通过 HTTPS 下载，未配置地址的本地版本从安装器旁读取同版 ZIP。完整安装器自带同一 ZIP。便携版包含 ConvertiiSetup.exe，供安装后的卸载使用；安装请使用正式的 Online/Offline 安装程序。

安装和卸载为当前用户操作，不调用 PowerShell 或 .NET。升级使用安装清单和目录回滚；混有个人文件的目录会拒绝覆盖。卸载保留用户设置，只删除清单拥有的文件。DLL 被 Explorer 占用时会在删除前提示关闭/重启 Explorer。

便携版可在“任务 / 设置”中注册、修复、移除右键；移动目录后重新注册。Windows 11 新菜单需要受信任签名的身份包；未签名开发包保留传统入口并显示原因，不会自动信任测试证书。发布时可传 -CertificateThumbprint 和匹配的 -Publisher；也可传 -IdentityPackage 使用预先生成的包。

## 许可与发布状态

Convertii 使用 GPL-3.0-only，见 LICENSE。FFmpeg 使用固定构建，JSON 与 ZIP 库版本及许可证见 docs/THIRD-PARTY.md。

公开发布还需要真实下载地址、正式签名、FFmpeg 及相关依赖的完整对应源码/构建材料，并完成 Windows 10/11 干净系统的真实安装和 Explorer 验收。本地自动化测试不会修改系统菜单注册，不能替代这些验收。

本轮已运行的检查、体积及内存观测见 [原生迁移验证记录](docs/NATIVE-STATUS.md)。

暂不包含多轨视频编辑、转场、动图处理、OCR、录屏和安卓端。

---

Convertii is a native C++/Win32 media tool with a standard system UI. It supports batch compression, conversion, audio extraction, trimming, picture filters, text subtitles, concatenation and embedded playback. No .NET runtime is required. Switch between English and Simplified Chinese in Tasks / settings. GPL-3.0-only.
