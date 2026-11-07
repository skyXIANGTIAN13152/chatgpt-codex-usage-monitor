# v2.0.3 — Restore Codex quota refresh after sign-in expiry

When a Codex session expired, the bundled official service wrapped the usage endpoint's HTTP 401 `token_expired` response in JSON-RPC error `-32603`. The widget treated it as a generic connection error and could keep showing the previous quota and reset countdown.

The monitor now recognizes this expiry response, asks the official component to renew the session once, and retries the quota read. If the session is still expired, it clears the old readings and countdown and prompts you to sign in again. This fix ships in the Chinese edition `v2.0.3` and the English edition `v2.0.3-en`.

All 121 unit tests passed, including successful renewal and continued expiry. The existing local Codex account was verified to show both five-hour and weekly quotas again. Claude Code's `/usage` integration remains experimental pending a live sign-in and usage check.

## Downloads and upgrade

- [English portable package](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.3-en/TigaEnergyMonitor-v2.0.3-en-windows-x64-portable.zip) · [English release](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.3-en)
- [Chinese portable package](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.3/TigaEnergyMonitor-v2.0.3-windows-x64-portable.zip) · [Chinese release](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.3)
- [Bilingual source](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.3-en/TigaEnergyMonitor-v2.0.3-source.zip) · [SHA-256 checksums for both packages](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.3-en/TigaEnergyMonitor-v2.0.3-SHA256SUMS.txt)

Both release pages provide the same source and checksum files. To upgrade, exit through the tray, extract the entire new package and replace the program files. Keep your own `%LOCALAPPDATA%\TigaEnergyMonitorV2` directory to retain settings and account data. The two editions share that directory, so run one at a time.

The [demo](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.0/TigaEnergyMonitor-v2.0.0-demo.mp4) is still the v2.0.0 sample-account recording. The [v2.0.2 topmost fix](release-notes-v2.0.2.en.md) and [v2.0.1 refresh and countdown fixes](release-notes-v2.0.1.en.md) remain included.

[中文更新记录](release-notes-v2.0.3.md) · [Usage instructions](../README.en.md)
