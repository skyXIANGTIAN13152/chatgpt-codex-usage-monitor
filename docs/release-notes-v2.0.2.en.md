# v2.0.2 — Windows Always on top fix

This update ships as the Chinese edition `v2.0.2` and the fully English edition `v2.0.2-en`.

## Fix

The transparent widget and its settings window could be covered by ordinary windows despite Always on top being saved as enabled. The previous `floating` level could lose the window's native topmost flag when the primary taskbar itself was not topmost, leaving the saved preference out of sync with the actual window state.

All four creation and update calls for these windows now use the shared `pop-up-menu` level on Windows. Other platforms retain `floating`. Enabling Always on top applies the correct native state; disabling it still follows the user's choice.

Automatic refresh, persistent per-period reset countdowns, animation-only pause, compact layout after hiding an account and both interface languages remain available. Earlier changes are preserved in the [v2.0.1 notes](release-notes-v2.0.1.en.md).

## Validation

Windows native A/B checks, six regression groups and 119 unit tests passed. The installed widget's native topmost flag was confirmed as enabled, with account data and settings preserved. Other multi-scenario quota regressions use isolated fixtures. End-to-end official Claude Code authorization followed by actual usage reading remains unverified; its `/usage` integration is still experimental.

## Downloads and upgrade

- [English portable package](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2-en/TigaEnergyMonitor-v2.0.2-en-windows-x64-portable.zip) · [English release](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.2-en)
- [Chinese portable package](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2/TigaEnergyMonitor-v2.0.2-windows-x64-portable.zip) · [Chinese release](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.2)
- [Bilingual source](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2-en/TigaEnergyMonitor-v2.0.2-source.zip) · [SHA-256 checksums for both packages](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2-en/TigaEnergyMonitor-v2.0.2-SHA256SUMS.txt)

Both editions provide the same source and checksum files; only the portable packages differ by interface language.

Exit the monitor completely through its tray, extract the entire new package and replace the program files. Keep your own `%LOCALAPPDATA%\TigaEnergyMonitorV2` profile directory. Existing account authorization and settings remain available; this fix does not require creating a new profile. Both editions share that directory, so run one edition at a time. Release packages contain no user profiles. Do not copy another person's profile directory.

The [complete demo](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.0/TigaEnergyMonitor-v2.0.0-demo.mp4) remains the v2.0.0 sample-account recording, not a new video for this update.

[中文更新记录](release-notes-v2.0.2.md) · [Usage instructions](../README.en.md)
