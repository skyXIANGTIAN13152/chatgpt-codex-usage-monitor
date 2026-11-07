# v2.0.3 — 修复 Codex 授权过期后无法刷新额度

Codex 授权过期时，随附官方服务会把用量接口的 HTTP 401 `token_expired` 封装为 JSON-RPC `-32603`。旧版把它当作普通连接异常，浮窗可能继续显示上次的额度和重置倒计时。

新版识别到这类过期错误后，会请求官方组件续期一次，再读取额度。若仍提示过期，浮窗会清除旧读数和倒计时，并提示重新登录。此修复同时包含在中文版 `v2.0.3` 和英文版 `v2.0.3-en` 中。

121 项单元测试通过，包括续期后成功读数和持续过期后提示登录。已在本机用既有授权确认五小时与周额度均恢复。Claude Code 的 `/usage` 读取仍属实验性功能，真实授权及用量的端到端验证尚未完成。

## 下载与升级

- [中文版便携包](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.3/TigaEnergyMonitor-v2.0.3-windows-x64-portable.zip) · [中文发布页](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.3)
- [英文版便携包](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.3-en/TigaEnergyMonitor-v2.0.3-en-windows-x64-portable.zip) · [英文发布页](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.3-en)
- [双语源码](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.3/TigaEnergyMonitor-v2.0.3-source.zip) · [中英文包 SHA-256 校验表](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.3/TigaEnergyMonitor-v2.0.3-SHA256SUMS.txt)

两个发布页提供相同的源码和校验文件。升级时先从托盘完全退出，再完整解压新版并替换程序文件。保留自己的 `%LOCALAPPDATA%\TigaEnergyMonitorV2` 目录，继续使用已有设置和账号资料。中英文版共用该目录，请一次只运行一个版本。

[演示视频](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.0/TigaEnergyMonitor-v2.0.0-demo.mp4)仍是 v2.0.0 录制的示例账号演示。[v2.0.2 置顶修复](release-notes-v2.0.2.md)和 [v2.0.1 刷新与倒计时修复](release-notes-v2.0.1.md)继续保留。

[English release notes](release-notes-v2.0.3.en.md) · [完整使用说明](../README.md)
