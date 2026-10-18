# v2.0.1 — Automatic refresh and visible reset countdowns

This update ships as the Chinese edition `v2.0.1` and the fully English edition `v2.0.1-en`.

## Fixes

- **Polling and data lifetime agree.** Five-minute polling no longer loses its readings after a fixed three minutes. Cached data is valid for `max(3 minutes, refresh interval + 2 minutes)`: seven minutes for five-minute polling, with the existing three-minute minimum for default one-minute polling. The actual reset deadline still invalidates a reading immediately.
- **Active synchronization and bounded retries.** Unrelated pause and display settings no longer postpone the next refresh. Polling, quota resets and computer wake trigger synchronization. Failed reads retry after at least 30 seconds; manual, timed and wake requests are combined rather than overlapping. Unfinished startup or refresh work can no longer restart background activity after exit.
- **Persistent reset countdowns.** Every visible five-hour and overall weekly period has a short countdown in both bars and rings. Hover still shows the full local reset date. Pausing animation does not pause countdowns or quota updates. Unknown times and expired readings are labeled without inventing a renewed allowance.
- **Compact in both languages.** The 420×246 widget keeps its compact flow, moving the toolbar upward when an account is hidden. Both language editions include the bar, ring and time-text changes.

## Downloads and upgrade

- [English portable package](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.1-en/TigaEnergyMonitor-v2.0.1-en-windows-x64-portable.zip) · [English release](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.1-en)
- [Chinese portable package](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.1/TigaEnergyMonitor-v2.0.1-windows-x64-portable.zip) · [Chinese release](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.1)
- [Bilingual source](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.1-en/TigaEnergyMonitor-v2.0.1-source.zip) · [SHA-256 checksums for both packages](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.1-en/TigaEnergyMonitor-v2.0.1-SHA256SUMS.txt)

Both release pages include identical source and checksum files; only the portable packages differ by interface language.

To upgrade from v2, exit the monitor completely through its tray, extract the entire new package and replace the program files. Keep your own `%LOCALAPPDATA%\TigaEnergyMonitorV2` profile directory; existing settings and official authorization data remain available. Both language editions share that profile, so run one edition at a time. Release packages contain no user profiles. Do not copy another person's profile directory.

The [complete demo](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.0/TigaEnergyMonitor-v2.0.0-demo.mp4) is still the approximately 105-second v2.0.0 Chinese recording, with readings labeled as sample data. No new video was recorded for this update.

## Validation scope

Existing authorized Codex readings on the local machine have recovered and were visually confirmed. Multi-scenario regressions for automatic polling, expiry, wake, retry backoff, shutdown, countdowns and bilingual bar/ring layout use isolated fixtures and test profiles. They do not establish coverage of every real account or network condition.

End-to-end official Claude Code authorization followed by actual usage reading has not been completed. Reading its official `/usage` screen remains experimental. Unavailable data stays pending and is never replaced with demo readings.

[中文更新记录](release-notes-v2.0.1.md) · [Usage and build instructions](../README.en.md)
