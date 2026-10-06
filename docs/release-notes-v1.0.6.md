# v1.0.6 - English Edition

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
