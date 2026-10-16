# v2.0.0 — 独立双账号透明悬浮窗

v2 将原先跟随 ChatGPT 的 C++ 小窗口改为独立桌面应用。无需启动 Codex、ChatGPT、Steam 或 Wallpaper Engine；可分别通过官方组件登录 ChatGPT 和 Claude Code，选择显示一家或两家的额度。

## 下载与升级

- 中文版：[v2.0.0](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.0)，附件 `TigaEnergyMonitor-v2.0.0-windows-x64-portable.zip`。
- 完整英文版：[v2.0.0-en](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.0-en)，附件 `TigaEnergyMonitor-v2.0.0-en-windows-x64-portable.zip`。
- 演示附件：`TigaEnergyMonitor-v2.0.0-demo.mp4`，约 105 秒，1080p、30fps。录制实际应用窗口；画面中的额度明确标注为示例数据，界面与字幕为中文。

退出旧显示器后，完整解压新版并运行 `TigaEnergyMonitor.exe`。v2 数据使用独立目录 `%LOCALAPPDATA%\TigaEnergyMonitorV2`；只迁移旧版刷新间隔、红灯阈值和置顶偏好，不复制旧凭据。请在新版选择需要的账号完成官方授权。根目录旧 C++ 源码和 [旧说明](README-v1.zh-CN.md) 仍保留；新版源码在 `app/`。

## 本次变化

- 人物透明悬浮，移除壁纸矩形底板；身体与轮廓固定，保留来自原壁纸的局部动态光效。满能量、红灯、石化默认以 1.8 秒交叉淡化过渡。
- Codex / Claude Code 独立显示，隐藏账号后布局自动收紧；工具栏始终显示。提供独立设置窗、四档缩放、记住位置、置顶、暂停与减少动态效果。
- 光能条与光能环二选一。每账号一个同心环组：总周在外、五小时在内；只有一个周期时自动显示单条或单环。
- 各周期颜色独立计算：低于 50% 切换低额度色，真实 0% 石化材质；不再保留血管装饰。
- 状态目标可选 Codex、Claude Code 或自选成员聚合，与账号显示开关独立。聚合按账号等权、按周期分别平均。
- 人物状态优先看五小时；仅在没有五小时限制时使用总周。控制周期缺失或过期等待同步，不跳过缺失账号，也不伪造 0% / 100%。模型专属限制不混算为应用总周。
- 可选随 Codex / Claude Code 启动，默认关闭。启用后使用本地启动监视器；关闭目标应用不会退出显示器。
- 复用原版 C++ 额度解析与能量核心，新增 Electron 界面、独立登录流程及 Claude 适配。

## 验证范围与限制

已验证隔离环境下的独立启动、两套官方组件未登录状态、真实窗口与透明区域、计算与设置持久化、条环互选、三态播放与过渡、打包运行及完整演示视频。测试和演示中的示例账号不会写入用户的真实账号目录。

发版检查：76 项自动化测试通过；两套 C++ 原生组件构建与测试通过；中文版 14 项显示模式回归、英文界面 38 次文字及布局检查通过；中英文成品均通过独立启动、官方组件未登录、ConPTY 与视频播放检查。

**真实账号授权后的完整额度读取尚未完成端到端验证。**Claude 读取是实验性功能：使用未修改官方 CLI 的 `/usage` 界面，可能受官方首次设置、账号权限或界面更新影响。缺少准确读数时显示等待同步，并提供官方用量入口，不填入演示数据。本程序未做代码签名。

代码遵循 MIT；壁纸衍生人物、视频与图标不因此获得 MIT 授权。素材作者与处理方式见 [ASSET-LICENSE.md](../ASSET-LICENSE.md)，第三方运行组件见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。

[English release notes](release-notes-v2.0.0.en.md) · [完整使用与构建说明](../README.md)
