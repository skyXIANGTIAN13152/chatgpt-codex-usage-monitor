# 迪迦额度显示器 · Tiga Energy Monitor 2.0.0

中文说明 · [English](README.en.md) · [更新记录](docs/release-notes-v2.0.0.md)

一个独立运行的 Windows 桌面透明悬浮窗，分别显示 ChatGPT 账号的 Codex 额度与 Claude Code 订阅额度。人物和信息直接浮在桌面上，透明空白区域可穿透；运行时无需打开 Codex、ChatGPT、Steam 或 Wallpaper Engine。

**[下载 v2.0.0 便携包](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.0/TigaEnergyMonitor-v2.0.0-windows-x64-portable.zip)** · **[观看完整演示](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.0/TigaEnergyMonitor-v2.0.0-demo.mp4)** · [发布页](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.0)

演示长约 1 分 45 秒，录制实际应用窗口，全程标注“演示额度 · 非真实账号”。中文版使用 `v2.0.0`；完整英文版使用 [v2.0.0-en](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.0-en)，下载 `TigaEnergyMonitor-v2.0.0-en-windows-x64-portable.zip`。

## 开始使用

1. 完整解压 `TigaEnergyMonitor-v2.0.0-windows-x64-portable.zip`，双击 `TigaEnergyMonitor.exe`。发布包面向 Windows x64，已包含运行组件。
2. 在浮窗中点击“使用 ChatGPT 账号登录”或“使用 Claude Code 账号登录”，由随附的官方组件打开授权流程。首次启动使用独立账号目录，不自动沿用旧版登录。
3. 打开设置，选择要显示的账号，以及“光能条”或“光能环”。两个账号可分别显示、同时显示或全部隐藏；控制迪迦状态的目标另行选择。
4. 拖动人物移动窗口；工具栏依次提供刷新、暂停、置顶、设置与收进托盘。工具栏始终显示。完全退出请使用托盘菜单。

**验证边界：**独立启动、未登录状态、设置持久化、实际窗口、动态素材及示例数据下的计算已有自动测试；本轮没有替用户完成真实账号授权，授权后完整额度读取尚未完成端到端验证。Claude Code 读取属于实验性集成，可能因首次设置、账号权限或官方界面变化而等待同步。未读到数据时不会填入演示额度。

## 主要功能

- **独立双账号：**分别登录、刷新和退出；账号显示开关与能量目标独立，隐藏明细不会改变聚合选择。
- **条与环二选一：**默认光能条。每个账号一个同心环组，外环总周、内环五小时；仅有一个周期时显示单环。中央优先显示五小时百分比，周期数值、重置与刷新提示仍保留。
- **各周期独立颜色：**Codex 常态蓝青，低于 50% 时玫瑰到血红；Claude Code 常态白金，低于 50% 时深黄到橙红；真实 0% 使用石灰质感。颜色规则不受预览或聚合目标影响。
- **透明人物三态：**满能量、红灯、石化。人物纹理和轮廓固定，只保留并增强原壁纸的局部动态光效；状态默认用 1.8 秒交叉淡化切换。预览只改变画面，不改账号数据。
- **紧凑布局：**420×246 逻辑像素，支持 80%、100%、125%、150% 缩放；记住位置，隐藏账号后信息和工具栏自然上移。
- **可控动画：**暂停仅停止画面，额度仍会刷新；减少动态效果使用静态画面并关闭过渡。
- **可选随应用启动：**可分别选择随 Codex 或 Claude Code 启动，默认关闭。开启后，本地监视器在 Windows 登录时运行，检测对应软件启动并打开显示器；关闭对应软件不会退出显示器。关闭两个开关会取消此监视器的启动项。

## 额度与状态如何计算

设置中的“迪迦状态的目标”可选 Codex、Claude Code 或聚合模式。**五小时优先；仅在目标没有五小时限制时才使用总周。**已声明的五小时窗口缺值、过期或重置到期时等待同步，不回退总周。

聚合成员至少保留一个账号，各账号等权，分别平均同一周期的剩余百分比。例如：

| 周期 | Codex | Claude Code | 聚合 |
| --- | ---: | ---: | ---: |
| 五小时 | 20% | 80% | 50% |
| 总周 | 40% | 80% | 60% |

该例使用五小时的 **50%** 判断人物状态。只要控制周期完整有效，其他周期缺失不会阻挡它；控制周期缺少任何所选成员则等待同步，不跳过缺失账号。

默认剩余 20% 或以下为红灯，0% 为石化。没有登录、没有准确额度、数据超过 3 分钟或已经到重置时间，不会猜成 0% 或 100%。模型专属限制留在完整明细中，不混算为应用总周额度。不同平台的聚合百分比只是便于观察的平均值，不是可转移的共享额度池，也不是 token 数量、上下文大小或 API 账单。

## 登录、数据与已知限制

Codex 通过随附官方 CLI 的 App Server 发起登录和额度请求。Claude Code 使用未修改的官方 CLI 登录，并尝试从其官方 `/usage` 界面读取用量；没有把“打开用量网页”当成登录成功。若官方组件需要首次设置，应用提供用户主动打开的设置入口，不会替用户接受提示。

应用设置与独立账号资料位于 `%LOCALAPPDATA%\TigaEnergyMonitorV2`。旧版仅迁移刷新间隔、红灯阈值和置顶偏好，不复制旧登录凭据。官方组件管理自己的独立授权资料。详见 [集成说明](app/integration-notes.txt)。

本版本未做代码签名。真实账号授权后的读数仍需实际验证；Claude 实验性解析可能随上游界面变化失效。只展示官方返回且能识别的周期，不保证每个套餐都有相同的五小时与总周窗口。

## 源码与构建

新版源码位于 [app/](app/)，复用旧版 C++ 额度解析与能量判定核心。仓库根目录的 `src/`、`include/`、`resources/`、`tests/` 与旧构建脚本继续保留，供查阅或构建 v1；请勿把旧版启动方式当作新版安装步骤。

需要 Windows x64、Node.js 24、pnpm、MSVC C++20 与 CMake 3.25+。在 MSVC x64 开发终端中，从仓库根目录执行：

```powershell
cd app
pnpm install --frozen-lockfile
pnpm --dir backend/claude-runtime install --frozen-lockfile
```

源码仓库不提交大型第三方运行时。先完整解压**同版本**便携包，再按下表复制；保留随包许可证与通知，不混用其他版本：

| 便携包内路径 | 源码目录 |
| --- | --- |
| `resources/app/runtime/` | `app/runtime/` |
| `resources/app/native/claude/` | `app/native/claude/` |

继续在 `app/` 下执行：

```powershell
cmd /c native\core-helper\build.cmd
cmd /c native\launch-watcher\build.cmd
node --test tests/*.test.cjs
node tests/ui-e2e.cjs
node scripts/package.cjs ../../outputs/portable
node scripts/package.cjs ../../outputs/portable-en --locale=en
```

原生构建会更新 `runtime/` 中的本项目辅助程序并运行对应 C++ 检查。GUI 测试使用隔离数据目录，不需要真实登录。打包结果位于仓库上一级的 `outputs/portable/`，完整目录一起分发。

旧文档：[v1 中文](docs/README-v1.zh-CN.md) · [v1 English](docs/README-v1.en.md)。

## 素材与许可

代码和构建脚本遵循 [MIT License](LICENSE)；人物、壁纸衍生视频和图标**不因此获得 MIT 授权**，详见 [素材说明](ASSET-LICENSE.md) 与 [第三方通知](THIRD_PARTY_NOTICES.md)。

三态来自作者 **柊明** 的用户指定 Wallpaper Engine 壁纸。透明人物由图像编辑工具从原画面提取并有所重建，再结合原视频的局部光效；这里不宣称逐像素保留原角色动画。软件运行不需要壁纸引擎，但素材的版权与再分发条件仍独立适用。来源、处理与提示词保存在 [素材记录](app/ASSET-NOTES.txt) 及资产目录的 manifest 中。

本项目为个人制作的非官方工具，与 OpenAI、Anthropic、Microsoft 或角色、美术权利人无隶属关系。
