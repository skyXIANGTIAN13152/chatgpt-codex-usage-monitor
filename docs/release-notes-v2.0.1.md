# v2.0.1 — 自动刷新修复与常驻重置倒计时

本次同步更新中文版 `v2.0.1` 和完整英文版 `v2.0.1-en`。

## 修复

- **刷新间隔与有效期一致：**修复设置为 5 分钟刷新、读数却在 3 分钟后提前变成“待同步”的问题。有效期改为 `max(3 分钟, 刷新间隔 + 2 分钟)`；5 分钟刷新对应 7 分钟有效期，默认 1 分钟刷新仍保留 3 分钟。实际重置时间到期仍立即失效，不会因此延长。
- **主动同步与有序重试：**无关的暂停、显示等设置不再把下一次刷新往后推。按周期、额度重置到期和电脑唤醒主动同步；失败至少间隔 30 秒重试，合并手动、定时与唤醒请求，避免重叠。修复退出时尚未完成的初始化或刷新重新启动后台工作的竞态。
- **常驻重置倒计时：**光能条和光能环中，每个显示的五小时、总周周期都恢复直接可见的短倒计时；悬停仍可查看完整本地重置时间。暂停动画不影响倒计时和额度刷新。未知时间与已到期数据明确标注，不填造新额度。
- **保持小巧与双语：**420×246 浮窗继续紧凑排列，隐藏账号后工具栏随内容上移；中英文的条形、环形与时间文字同步适配。

## 下载与升级

- [中文版便携包](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.1/TigaEnergyMonitor-v2.0.1-windows-x64-portable.zip) · [中文发布页](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.1)
- [完整英文版便携包](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.1-en/TigaEnergyMonitor-v2.0.1-en-windows-x64-portable.zip) · [英文发布页](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.1-en)
- [双语源码](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.1/TigaEnergyMonitor-v2.0.1-source.zip) · [中英文包 SHA-256 校验表](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.1/TigaEnergyMonitor-v2.0.1-SHA256SUMS.txt)

两个发布页提供相同的源码与校验文件，只有便携包按界面语言区分。

从 v2 升级时，先从托盘完全退出显示器，完整解压新版并替换程序文件。保留自己的 `%LOCALAPPDATA%\TigaEnergyMonitorV2` 用户资料目录，已有设置和官方授权资料继续使用。中英文版共用该目录，同一时间运行一个版本即可。发布包不含用户资料；不要复制其他人的资料目录。

[完整演示](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.0/TigaEnergyMonitor-v2.0.0-demo.mp4)仍是 v2.0.0 的约 105 秒中文录像，额度全程标注为示例数据；本次没有重新录制视频。

## 验证范围

本机既有授权的 Codex 实际读数已恢复，并已目视确认。自动刷新、到期、唤醒、失败退避、退出、倒计时以及双语条环布局的多场景自动回归使用隔离 fixture 和测试资料，不代表所有真实账号与网络情况均已验证。

Claude Code 官方授权后读取实际用量的完整端到端验证尚未完成，官方 `/usage` 界面的读取仍为实验性功能。缺少有效数据时显示待同步，不替换成演示数值。

[English release notes](release-notes-v2.0.1.en.md) · [完整使用与构建说明](../README.md)
