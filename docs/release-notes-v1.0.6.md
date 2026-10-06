# v1.0.6 / v1.0.6-en

## 中文

Codex 主程序刷新登录信息后，显示器的额度读取进程可能仍保留旧凭据，导致持续显示“未登录”。此版本让认证失败后的重试重新加载登录状态。

- 首次认证失败后约 1 秒重建 App Server；若确实未登录，按 60 秒至 10 分钟的退避间隔继续重连。
- 稍后完成登录后，下一次重试会读取新的登录状态；点击刷新可以立即重连。
- 成功读取完整额度后恢复正常刷新，并允许后续新的认证失效自动恢复。
- 同一次启动的账户检查和额度请求重复报错时，共用一次重试，避免重复推进退避间隔。
- 旧连接排队的错误或额度消息不会覆盖新连接的状态和数据。
- 保留已有显示设置；显示器通过官方 CLI 读取额度，不读取、复制或修改凭据。

新增认证恢复测试，覆盖凭据更新、启动时未登录、持续未登录、延迟登录后的定时恢复、手动刷新、再次认证失效及旧消息隔离。每个版本运行全部六项测试套件；中文版另验证了真实账户额度读取。测试窗口与正式配置隔离。

普通用户下载对应版本的 `portable.zip`，完整解压后运行。包内包含官方 Codex CLI 0.146.0、说明、快捷方式脚本和许可证。升级前从托盘退出显示器，再替换程序文件。`source.zip` 是开发者源码；`SHA256SUMS` 提供下载文件校验值。

## English

After the desktop app refreshes its login, the monitor's quota process can retain old credentials and keep showing "not logged in". This release makes authentication retries reload the official login state.

- Restart App Server about one second after the first authentication failure. Persistent sign-out uses a 60-second-to-10-minute backoff, reconnecting on each retry.
- A later successful login is picked up by the next scheduled retry; Refresh reconnects immediately.
- A successful full quota read restores normal polling and allows a later authentication expiry to recover again.
- Duplicate authentication errors from the startup account check and quota read share one retry instead of advancing the backoff twice.
- Queued errors and snapshots from older connections cannot overwrite the new connection's state or data.
- Preserve saved display settings. The HUD uses the official CLI and does not read, copy, or modify credentials.

New recovery tests cover token rotation, initial and persistent sign-out, a later login, scheduled recovery, manual refresh, repeated expiry, and stale-message isolation. Each edition runs all six automated test suites. The Chinese edition also passed a live account quota probe. Test windows are isolated from production settings.

Download the edition's `portable.zip`, extract the complete archive, and run the monitor. It includes official Codex CLI 0.146.0, instructions, shortcut scripts, and licenses. Exit the monitor from its tray before upgrading. The `source.zip` is for developers; `SHA256SUMS` lists download checksums.

The Chinese edition (`v1.0.6`) is the default Latest release. The English UI edition is published separately as `v1.0.6-en`.
