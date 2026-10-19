# v2.0.2 — Windows 置顶修复

中文版 `v2.0.2` 与完整英文版 `v2.0.2-en` 同步发布。

## 修复

修复已保存“始终置顶”为开启，透明浮窗及其设置窗仍会被普通窗口遮挡的问题。此前使用的 `floating` 层级在主任务栏本身不置顶时，可能让窗口丢失原生置顶标记，造成设置与实际状态不一致。

Windows 上创建和更新这两个窗口的四处调用现在统一使用 `pop-up-menu` 层级；其他平台保留原来的 `floating` 选择。开启置顶后，窗口使用正确的原生置顶状态；关闭时仍按用户选择取消置顶。

自动刷新、每周期常驻重置倒计时、暂停仅影响动画、隐藏账号后收紧布局及中英文支持均保留。此前改动见 [v2.0.1 更新记录](release-notes-v2.0.1.md)。

## 验证

Windows 原生置顶 A/B 对照、6 组回归和 119 项单元测试通过。本机已部署浮窗的原生置顶标记已确认为开启，账号与设置继续保留。其他多场景额度回归使用隔离 fixture；Claude Code 官方授权后读取实际用量的完整端到端验证仍未完成，`/usage` 读取继续属于实验性集成。

## 下载与升级

- [中文版便携包](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2/TigaEnergyMonitor-v2.0.2-windows-x64-portable.zip) · [中文发布页](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.2)
- [完整英文版便携包](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2-en/TigaEnergyMonitor-v2.0.2-en-windows-x64-portable.zip) · [英文发布页](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.2-en)
- [双语源码](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2/TigaEnergyMonitor-v2.0.2-source.zip) · [中英文包 SHA-256 校验表](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2/TigaEnergyMonitor-v2.0.2-SHA256SUMS.txt)

两版提供同一份源码和校验文件，便携包按界面语言区分。

升级前从托盘完全退出显示器，完整解压新版并替换程序文件，保留自己的 `%LOCALAPPDATA%\TigaEnergyMonitorV2` 用户资料目录。已有账号授权和设置继续使用，无需为本次修复重新建立资料；中英文版共用该目录，同一时间运行一个版本即可。发布包不含用户资料，不要复制其他人的资料目录。

[完整演示](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.0/TigaEnergyMonitor-v2.0.0-demo.mp4)仍为 v2.0.0 的示例账号录像，不是本次新录制。

[English release notes](release-notes-v2.0.2.en.md) · [完整使用说明](../README.md)
