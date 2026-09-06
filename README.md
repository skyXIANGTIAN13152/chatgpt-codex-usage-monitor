# ChatGPTCodexUsageMonitor

中文说明 · [English README](README.en.md)

下载 **v1.0.5**：[中文版](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v1.0.5) · [English edition](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v1.0.5-en)。选择 `portable.zip`，完整解压后运行；无需自行编译。升级时先从托盘退出显示器，再替换解压目录中的程序文件；用户配置保存在独立目录，会继续保留。

本次更新统一了胸甲与光能背板，圆环右移并与身体留出间距，百分比、倒计时及底栏按钮排布更清晰。单圈、双圈使用相同的间距规则；整体缩放仍包含所有图形和文字。主额度按实际周期自动适配，不会混入 Spark 独立额度。完整说明见 [v1.0.5 更新记录](docs/release-notes-v1.0.5.md)。

这是一个 C++20 / 原生 Win32 的轻量 Windows HUD。它通过新的桌面快捷方式启动 Microsoft Store / MSIX 版 ChatGPT，同时从本机已登录的官方 Codex CLI App Server 读取 ChatGPT 账户中的 Codex 额度窗口。默认使用 80% 整体缩放的紧凑模式，约为 240×72；右键托盘菜单可按 5% 在 60%–140% 之间自由调整，且会同步缩放胸甲、能量灯、进度条/光能圆环、字体和粒子效果。额度形式可在“光能条”和“光能圆环”之间切换：双圆环以深色外圈表示周剩余额度、亮色内圈表示 5 小时剩余额度；双进度条则以深色上条和亮色下条表达同一对应关系。也可以切换为“仅周额度”。用户选择的尺寸、额度形式和窗口模式会写入配置并作为下次启动的默认值。ChatGPT 的有效顶层窗口全部关闭并持续 5 秒后，监视器自动退出；它不会提前常驻等待 ChatGPT，也不是开机常驻程序，不会安装服务。

## 它显示什么

- 主额度中实际存在的周 / 5 小时周期的剩余百分比、重置倒计时和本地绝对时间；
- 默认“自动适配额度（单 / 双）”：只有周额度时使用单圈 / 单条，同时返回两个周期时使用双圈 / 双条；也可固定仅周额度；
- 只显示 `codex` 主额度，不会把 Spark 等模型的独立额度拼入或当作主额度；
- credits balance、plan type、最后一次成功更新时间/过期状态；
- 状态：LIVE、WARNING、CRITICAL、STONE、STALE、OFFLINE、未登录、CLI 未安装等。

计算严格使用 `remainingPercent = clamp(100 - usedPercent, 0, 100)`。这些数字是 Codex 订阅额度窗口，不是当前对话 context window、聊天 token 统计或 OpenAI API Key billing。官方接口仅返回百分比时，本程序只显示百分比；除非接口明确同时返回 token 上限和已用 token，否则不会显示或估算“剩余 token”。

套餐升级后无需手动改显示模式：程序按主额度实际返回的周期适配，而不是把所有 Pro 账号硬编码成“无 5 小时限制”。主额度只有周周期时，自动移除第二圈 / 第二条和对应文字，保持原来的窗口尺寸、位置、整体缩放、主题及用户选择。缺失的周期不显示假 100% 或“无限”；主额度完全不可用时显示 `--` / “暂不可用”，不会触发 0% 提醒或石化。断网保留低亮的上次有效记录并明确标注状态；Spark 的更新也不会把主额度的旧记录刷新成“刚更新”。

## 迪迦能量指示器主题

默认同时显示精确额度图形和能量指示器。双圆环中周额度使用较深的蓝紫外圈，5 小时额度使用较亮的青蓝内圈；双进度条保持相同的深色上条 / 亮色下条层级。每个窗口会依据自己的百分比独立转为深红或亮红，但圆环和进度条始终稳定不闪烁，只有胸灯参与闪烁。能量指示灯在双额度模式下跟随两者中更紧张的窗口；切换为仅周额度后只跟随周窗口。低于默认 50% 后，红灯亮相位直接显示用户指定的完整胸甲原图，不对灯、灯座或胸甲重新绘制，并随额度降低加速闪烁；暗相位仍保留同一镜片的酒红纹理。正常状态使用同一张胸甲，仅将中央灯区换成同源蓝色玻璃；任一当前显示的限制窗口达到 0% 时，胸甲、金属、灯体一起进入低饱和、低对比、哑光颗粒的石像状态。各状态切换时构图、比例和裁切保持一致。

主题左侧直接内嵌并显示用户提供、且由用户明确确认拥有使用权的胸甲原图。红灯亮相位与该文件逐像素一致；蓝灯只把内置图像编辑生成的蓝色灯区柔边合回原图，暗相位只压暗原镜片，二者在中央灯区之外均保持主图像素不变；石像版本则统一处理整幅胸甲。运行时只进行等比缩放、定位、透明度和边缘淡化，能量条与文字状态动效由 Direct2D 实时绘制。

背板以“致以光辉的人”为主题，由胸灯周围的蓝白光晕自然融入深靛底色，胸甲下沿渐隐至柔和反光中。原图与背板共用同一个视觉中心，不叠加独立小徽标、硬折线或第二层装饰框。所有光效均为静态 Direct2D 绘制，不新增背景图片或持续动画；0% 时统一变为低亮中性灰。

UI 字体使用原创量子离子效果：正常态由冰蓝、白色高能核心、青色电离层和紫蓝边缘组成多段能量渐变，并以两层低透明辉光形成发光生命体的凝聚感；低额度时字体与离子同步切换为白热红、绯红和洋红；0% 时粒子完全熄灭，文字转为低亮冷灰休眠态。百分比周围只保留四个带核心、光晕与微型轨道/拖尾的离子，RESET 只保留两个，避免散点和标点错觉。LIVE/WARNING/STONE 状态文字继续保留绿、红、灰语义色，并使用对应状态的微弱离子辉光。特效复用原有重绘和红灯闪烁节奏，不增加动画计时器。

额度显示器使用独立的透明器物图标，不再使用 Windows 默认蓝色信息图标。图标内含 16、20、24、32、40、48、64、128 和 256 像素九种画面：托盘小尺寸突出上部 U 形结构，任务栏尺寸加入金色底座，大尺寸显示完整器物。桌面的 ChatGPT 组合启动快捷方式仍可独立指定标准 ChatGPT 图标，不受额度显示器图标变化影响。

## 运行前提与 Codex CLI

1. Windows 10 1903 或更新版本，推荐 Windows 11 x64。
2. 已从 Microsoft Store 安装 ChatGPT Windows 桌面版。
3. 将交付目录中的 `codex.exe` 与监视器 EXE 放在同一目录（成品已经这样打包）。

成品随附 OpenAI 官方 Codex CLI 0.146.0（Apache-2.0），来源固定到官方 GitHub Release，构建时同时校验压缩包和可执行文件 SHA-256，并附带原始 LICENSE/NOTICE。程序优先使用环境变量 `CODEX_EXECUTABLE`，其次使用同目录的 `codex.exe`，然后才检查 PATH、`%USERPROFILE%\.codex\bin\codex.exe` 和本地程序/WinGet 链接目录。它不会读取 Codex 身份验证文件内容。

当前机器已通过随附 CLI 的真实 App Server 成功读取 ChatGPT 账户额度。若换到尚未登录 Codex 的电脑，首次登录时在交付目录打开终端并运行 `codex.exe`，按官方界面选择 ChatGPT 登录并完成浏览器授权。程序不会代替你复制 OAuth token，也不会读取 Cookie。

## 使用

### 界面优化与示例预览

v1.0.5 优化了双额度排版、按钮悬停提示、分组设置菜单及数据新鲜度提示。窗口底栏会显示“正常 / 警戒 / 危急 / 石化”和“刚更新 / N分前”；绝对重置时间与套餐详情可悬停查看。它仍按配置的间隔刷新，不代表逐 token 实时检测。默认 80% 缩放、100% Windows 显示缩放下，圆环布局约 224×72，进度条布局约 240×72。

编译后，可单独运行示例数据预览：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\preview.ps1 -View ring -State normal -Scale 85
# 模拟主额度只有周周期、Spark 仍有双周期，验证自动收起内圈：
powershell -ExecutionPolicy Bypass -File .\scripts\preview.ps1 -View bar -State normal -Scale 85 -ProAccount
# 自定义构建目录：追加 -Executable .\build\ChatGPTCodexUsageMonitor.exe
```

预览窗口的设置菜单可直接切换正常、红灯、石化三种状态，以及圆环/能量条、双额度/仅周额度、尺寸和布局。预览有独立的窗口与单实例标识，不启动 ChatGPT、不连接额度服务、不写入正式版配置或日志；最小化后从任务栏恢复，关闭预览不影响正式版。预览使用虚构的固定百分比，不应当作真实账户余额。正式模式仍使用托盘。

切换 `-ProAccount` 或启动参数前请先关闭已有预览；已有预览再次启动只会恢复窗口。这个开关只生成测试数据，不代表对任何套餐的固定规则判断。

### 正式模式

直接运行：

```text
ChatGPTCodexUsageMonitor.exe --launcher
```

也支持高级模式：

```text
ChatGPTCodexUsageMonitor.exe --monitor <ChatGPTProcessId>
ChatGPTCodexUsageMonitor.exe --debug
```

HUD 点击空白处可立即刷新；拖动可保存位置；右下角齿轮或托盘右键可切换双额度/仅周额度、光能条/光能圆环、显示模式、HUD 尺寸、跟随 ChatGPT、能量主题、光晕、石化、闪烁阈值和闪烁风格。右下角横线或双击 HUD 会最小化到托盘，托盘左键恢复；这不会关闭 ChatGPT。手动“退出额度显示器”也不会关闭 ChatGPT。

## 安装快捷方式和固定到任务栏

在 `dist` 目录中右键 PowerShell 运行（或从 PowerShell 执行）：

```powershell
powershell -ExecutionPolicy Bypass -File .\install-shortcut.ps1
```

它只创建桌面上的 `ChatGPT with token.lnk`，不会删除原始 ChatGPT 快捷方式，也不会自动固定任务栏。若要固定：右键新快捷方式，选择“显示更多选项”→“固定到任务栏”。之后可把它作为日常 ChatGPT 启动入口。

## 卸载

```powershell
powershell -ExecutionPolicy Bypass -File .\uninstall-shortcut.ps1
```

默认只删除本程序创建的快捷方式。要同时删除本程序的非敏感配置和日志：

```powershell
powershell -ExecutionPolicy Bypass -File .\uninstall-shortcut.ps1 -RemoveConfig
```

脚本不会触碰 ChatGPT 的安装、文件、快捷方式或账户数据。绿色版 EXE 可直接手动删除。

## 从源码构建

需要 Visual Studio 2022 Build Tools（Desktop development with C++，MSVC x64，Windows 10/11 SDK）和 CMake 3.25+。执行：

```text
scripts\build-release.cmd
```

脚本配置 x64 Release，使用 `/O2`、`/GL`、`/LTCG`、静态多线程 CRT，运行全部 CTest，然后从 OpenAI 官方发布页下载并校验固定版本 Codex CLI，生成可直接运行的 `dist`。监视器 EXE 不要求用户安装 Visual C++ Runtime。

手动构建：

```text
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## 数据来源、刷新和官方限制

程序以隐藏子进程启动当前官方 CLI 的 `codex app-server`，通过 stdin/stdout JSONL 完成 `initialize`/`initialized` 握手，调用 `account/rateLimits/read`，并监听 `account/rateLimits/updated`。默认 60 秒主动刷新；通知、用户点击、系统睡眠恢复和网络接口变化会触发即时刷新。失败后指数退避，最长 10 分钟，同一时间最多一个额度请求。

解析字段包括 `rateLimits`、`rateLimitsByLimitId`、`limitId`、`limitName`、`primary`、`secondary`、`usedPercent`、`windowDurationMins`、`resetsAt`、`credits`、`hasCredits`、`unlimited`、`balance`、`planType`、`rateLimitReachedType` 和 `rateLimitResetCredits.availableCount`。程序不依赖 `primary`/`secondary` 的顺序，而以 300 分钟识别 5 小时窗口、10,080 分钟识别周窗口，并优先配对同一个 Codex bucket。稀疏的 `account/rateLimits/updated` 通知会合并进完整快照，因此只更新一个窗口时不会让另一个图形消失。字段缺失、新增、改序或部分 bucket 不可用不会导致崩溃。

如果当前官方接口不返回准确额度，程序显示“官方接口当前未提供此数据”/`DATA UNAVAILABLE`，并保留最后一次成功数据的时间；不会退回网页抓取、Cookie、私有 ChatGPT HTTP 接口、估算或伪造。

## 隐私和安全

程序不注入/Hook ChatGPT，不截获网络，不读取对话、Cookie、ChatGPT 数据库或 Codex 登录文件，不复制 token/API Key，不安装根证书/服务/开机启动，不修改防火墙，不需要管理员权限。配置只保存 HUD 位置和显示/刷新选项；日志默认仅记录 warning/error，单文件最多 1 MB，最多 3 个，不记录完整账户响应、授权头或聊天内容。

配置：`%LOCALAPPDATA%\ChatGPTCodexUsageMonitor\settings.json`

日志：`%LOCALAPPDATA%\ChatGPTCodexUsageMonitor\logs\`

加入 `--debug` 可启用详细但脱敏日志。

## 常见错误排查

- **CODEX CLI 未找到**：确认交付目录中的 `codex.exe` 与 `ChatGPTCodexUsageMonitor.exe` 位于同一目录且未被移动/删除；也可用 `CODEX_EXECUTABLE` 指定其他官方 CLI。
- **CODEX 尚未登录**：先直接运行 `codex` 并完成 ChatGPT 登录。
- **DATA UNAVAILABLE**：官方版本可能暂未给此账户返回额度；查看日志，等待下一次通知/刷新。程序不会改用 Cookie 抓取。
- **网络不可用 / 401 / 403 / 429 / 5xx**：检查登录和网络；程序会自动退避重试，网络恢复时立即刷新。
- **找不到 ChatGPT**：确认安装的是 Microsoft Store/MSIX 版且可正常启动；程序不硬编码某台机器的安装目录。
- **HUD 不见了**：托盘菜单选择“显示额度窗口”；多显示器变更时程序会把窗口夹回可用工作区。

确认程序已经完全退出：关闭 ChatGPT 所有有效顶层窗口并等待约 5 秒，然后在任务管理器“详细信息”中确认不存在 `ChatGPTCodexUsageMonitor.exe`。如果手动隐藏 HUD，它仍在托盘；必须选择托盘“退出额度显示器”才是手动完全退出。

## 测试与设计文档

- `docs/protocol.md`：App Server 协议和字段映射；
- `docs/architecture.md`：启动、生命周期、刷新和绘制结构；
- `docs/security.md`：威胁边界和隐私约束；
- `docs/visual-design.md`：主题素材来源与绘制边界；
- `docs/performance.md`：本机 Release 实测。
