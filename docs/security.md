# 安全与隐私边界

- 只调用 Windows 公共 MSIX/AUMID、窗口事件、进程等待、网络变化和绘图 API。
- 只通过官方 Codex CLI App Server 的结构化本地接口读取额度。
- 不打开 Codex/ChatGPT 身份验证文件，不读取或导出 Cookie/token/API Key。
- 不抓取 ChatGPT 网页，不发送带用户 Cookie 的私有 HTTP 请求。
- 不注入、Hook、截流、修改 ChatGPT，不读取对话或数据库。
- 子进程 stdin/stdout/stderr 均重定向；日志只保存分类后的脱敏错误，不保存完整响应。
- 配置采用临时文件 + 原子替换，仅含非敏感 UI/刷新设置。
- `asInvoker` manifest、无服务、无计划任务、无开机启动、无防火墙或证书修改。

