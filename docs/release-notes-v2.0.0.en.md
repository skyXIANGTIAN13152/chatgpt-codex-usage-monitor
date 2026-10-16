# v2.0.0 — Independent dual-account desktop widget

The monitor now runs independently with a transparent character and separate Codex / Claude Code account displays. It no longer requires the ChatGPT desktop application, Steam or Wallpaper Engine to stay open.

## Downloads

- [English edition, v2.0.0-en](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.0-en): `TigaEnergyMonitor-v2.0.0-en-windows-x64-portable.zip`.
- [Chinese edition, v2.0.0](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.0): `TigaEnergyMonitor-v2.0.0-windows-x64-portable.zip`.
- Demo: `TigaEnergyMonitor-v2.0.0-demo.mp4`, approximately 105 seconds at 1080p/30fps. It records the actual Chinese application UI and clearly labels the readings as sample data.

Exit the old monitor, extract the entire package and launch `TigaEnergyMonitor.exe`. v2 uses `%LOCALAPPDATA%\TigaEnergyMonitorV2`; only the old refresh interval, warning threshold and always-on-top preference migrate. Credentials are not copied. Sign in through the selected official component. New source lives in `app/`; the legacy C++ source and [v1 documentation](README-v1.en.md) remain available.

## Changes

- Transparent full-energy, warning and stone characters with fixed body geometry and original-source local light animation; default 1.8-second crossfades.
- Independent account visibility, compacting layout, an always-visible toolbar, separate settings, saved position, four scales, pause and reduced-motion controls.
- Mutually exclusive bars or rings. Each account has an outer weekly and inner five-hour ring; a single returned period uses a single indicator.
- Independent per-period colors below 50%, with a stone treatment for a real 0%. The experimental branching decorations were removed.
- Choose Codex, Claude Code or an equal-weight aggregate to control the character, independently of visible account details.
- Five-hour allowance takes priority; weekly is used only when no five-hour limit exists. Missing or expired controlling data stays pending, without omitting members or inventing percentages. Model-specific limits do not replace overall weekly allowance.
- Optional launch watching for Codex and/or Claude Code, off by default. Closing those applications does not close the monitor.
- Legacy C++ quota/state logic reused behind a new Electron interface and independent authentication integrations.

## Validation and limitations

Isolated standalone startup, signed-out official components, real windows, transparency, calculations, settings persistence, bars/rings, animation, packaged launch and the complete demo have been checked. Tests and the demo use separate sample profiles.

Release checks passed: 76 automated tests, both native C++ builds and test suites, 14 Chinese display-mode checks, and 38 English text/layout audits. Both packaged editions also passed standalone launch, signed-out official components, ConPTY and video playback checks.

**Real authorized quota readings have not completed end-to-end verification.** Claude usage reading is experimental and parses the unmodified official CLI's `/usage` interface. First-run setup, permissions or upstream changes may prevent a reading. The application then reports pending data and offers official usage access; it never substitutes demo values. The application is unsigned.

Code uses MIT; character images, wallpaper-derived clips and icons do not inherit that license. See [asset terms](../ASSET-LICENSE.md), [third-party notices](../THIRD_PARTY_NOTICES.md), and the [README](../README.en.md) for usage and build instructions.
