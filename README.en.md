# Tiga Energy Monitor 2.0.2

[中文](README.md) · English · [Release notes](docs/release-notes-v2.0.2.en.md)

An independent, transparent Windows desktop widget for Codex allowance from a ChatGPT account and Claude Code subscription usage. The character and account information sit directly on the desktop, with click-through empty space. Codex, ChatGPT, Steam and Wallpaper Engine do not need to stay open.

**[Download the English portable package](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2-en/TigaEnergyMonitor-v2.0.2-en-windows-x64-portable.zip)** · [Bilingual source](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2-en/TigaEnergyMonitor-v2.0.2-source.zip) · [SHA-256 checksums](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2-en/TigaEnergyMonitor-v2.0.2-SHA256SUMS.txt) · [English release](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.2-en) · [Chinese release](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.2)

The English edition uses `v2.0.2-en`; the Chinese edition uses `v2.0.2`. Both releases include the same bilingual source archive and checksum table covering both portable packages.

The [complete demo](https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.0/TigaEnergyMonitor-v2.0.0-demo.mp4) remains the approximately 1-minute-45-second v2.0.0 recording. It uses the Chinese interface, records actual application windows, and labels all readings as sample data. It is not a new v2.0.2 recording.

v2.0.2 fixes the widget and settings window being covered by ordinary windows despite Always on top being enabled. Automatic refresh, visible reset countdowns and the compact layout remain available. See the [release notes](docs/release-notes-v2.0.2.en.md); the earlier refresh fixes remain documented in the [v2.0.1 notes](docs/release-notes-v2.0.1.en.md).

## Getting started

1. Extract the entire `TigaEnergyMonitor-v2.0.2-en-windows-x64-portable.zip` and run `TigaEnergyMonitor.exe`. The Windows x64 package includes its runtime components.
2. Select the ChatGPT or Claude Code login link. The bundled official component starts its authorization flow, using a separate account directory.
3. Open Settings to choose visible accounts and either bars or rings. You can show one account, both or neither. The account that controls the character is selected separately.
4. Drag the character to move the widget. The always-visible toolbar provides refresh, pause, pin, settings and hide-to-tray. Use the tray menu to exit completely.

**Validation boundary:** existing authorized Codex readings on the local machine have recovered and were visually confirmed. Multi-scenario automatic-refresh, countdown, settings and quota regressions use isolated fixtures, rather than live accounts. End-to-end official Claude Code authorization followed by actual usage reading has not been completed; that integration remains experimental. Missing readings are never replaced with demo values.

## Features

- Independent Codex and Claude Code login, refresh and logout. Account visibility is separate from the character's target and aggregate membership.
- Choose bars or rings, with bars as the default. Each account has one concentric ring group: weekly outside, five-hour inside. A single available period uses one ring. The center prefers the five-hour percentage. Each visible period has a persistent reset countdown; hover for the full date and freshness details.
- Independent period colors: Codex uses blue/cyan, changing below 50% toward rose/red; Claude uses white/gold, changing below 50% toward dark yellow/orange-red. A real 0% uses a stone texture. Previews and aggregate selection do not recolor unrelated periods.
- Three transparent character states: full energy, warning and stone. Body texture and silhouette remain fixed; local moving light is derived from the original wallpapers. Transitions use a 1.8-second crossfade by default. Previewing a state never changes account data.
- A compact 420×246 logical-pixel widget with 80%, 100%, 125% and 150% scales, saved position, and a separate settings window. Hiding an account moves the remaining information and toolbar upward.
- Reliable topmost behavior on Windows for both the widget and settings window, fixing their being covered by ordinary windows despite Always on top being enabled. The saved topmost preference still controls both windows.
- Pause stops animation while quota refresh continues. Reduced-motion mode uses still images and disables transitions.
- Automatic synchronization at the selected interval, quota reset and computer wake. Unrelated settings do not postpone polling. Failed reads retry after at least 30 seconds; overlapping requests are combined, and unfinished work cannot restart background activity after exit.
- Optional launch watching for Codex and/or Claude Code, disabled by default. When enabled, a local watcher starts at Windows sign-in and opens the monitor when the selected application launches. Closing that application does not close the monitor. Turning both switches off removes the watcher's startup entry.

## How the character state is calculated

Choose Codex, Claude Code or an aggregate in Settings. **The five-hour window takes priority. Weekly is used only when the target has no five-hour limit.** A declared five-hour window that is missing a value, stale or past reset remains pending; it does not fall back to weekly.

Aggregation gives each selected account equal weight and averages each period separately:

| Period | Codex | Claude Code | Aggregate |
| --- | ---: | ---: | ---: |
| Five-hour | 20% | 80% | 50% |
| Overall weekly | 40% | 80% | 60% |

The example uses **50%** to control the character. A valid controlling period can be used even if another period is incomplete. If any selected account lacks valid data for the controlling period, the result stays pending; missing accounts are not silently excluded.

The default warning threshold is 20% or less; a real 0% produces the stone state. Signed-out, unavailable, stale or reset-expired data is not guessed to be 0% or 100%. Readings remain valid for the longer of three minutes or the refresh interval plus two minutes: five-minute polling permits seven minutes, while default one-minute polling retains the three-minute minimum. The actual reset deadline still invalidates a reading immediately and triggers synchronization. Failed reads retry after at least 30 seconds. Pause and display changes do not postpone polling. Model-specific limits remain in the detailed account view and are not treated as overall weekly allowance. An average across providers is a comparison of percentages, not a transferable shared allowance pool, token count, context-window statistic or API bill.

## Authentication, data and limitations

Codex uses the bundled official CLI App Server for login and usage requests. Claude Code uses its unmodified official CLI for login and attempts to read its official `/usage` screen. Opening a usage webpage does not mark an account as connected. If official first-run setup is required, the application offers a user-initiated setup entry and does not accept prompts on the user's behalf.

Settings and independent account data live under `%LOCALAPPDATA%\TigaEnergyMonitorV2`. Only the old refresh interval, warning threshold and always-on-top preference are migrated; old credentials are not copied. The official components manage their own separate authorization data. See the [integration notes](app/integration-notes.txt).

To upgrade from v2, exit completely through the tray, extract and replace the full program files, and keep your own profile directory above. Existing settings and official authorization data remain available. Both language editions share that directory; run one edition at a time. Release packages contain no user profiles. Do not copy another person's profile directory.

The application is unsigned. Claude's experimental parser may need updates when the official interface changes; end-to-end official authorization and actual usage reading remain unverified. Only recognizable, returned usage windows are shown; subscription plans need not expose identical periods.

## Source and build

The v2 application is in [app/](app/), reusing the legacy C++ quota parser and energy-state core. Root-level `src/`, `include/`, `resources/`, `tests/` and old build scripts remain available for v1. They are not the v2 installation entry point.

Use Windows x64, Node.js 24, pnpm, MSVC with C++20, and CMake 3.25+. From an MSVC x64 developer terminal at the repository root:

```powershell
cd app
pnpm install --frozen-lockfile
pnpm --dir backend/claude-runtime install --frozen-lockfile
```

Large third-party runtimes are not committed. Extract the portable package from the **same release**, then copy these directories while retaining its accompanying licenses and notices:

| Portable package path | Source checkout path |
| --- | --- |
| `resources/app/runtime/` | `app/runtime/` |
| `resources/app/native/claude/` | `app/native/claude/` |

Continue inside `app/`:

```powershell
cmd /c native\core-helper\build.cmd
cmd /c native\launch-watcher\build.cmd
node --test tests/*.test.cjs
node tests/ui-e2e.cjs
node scripts/package.cjs ../../outputs/portable --locale=en
```

The native scripts rebuild the project's helpers in `runtime/` and run their C++ checks. GUI tests use isolated data and do not require real login. Packaging writes a complete English portable directory to `outputs/portable/` alongside the repository; distribute that directory as a whole. Omit `--locale=en` to build the Chinese edition. Both editions use the same source and the same per-user v2 profile.

Archived documentation: [v1 Chinese](docs/README-v1.zh-CN.md) · [v1 English](docs/README-v1.en.md).

## Assets and licensing

Source code and build scripts use the [MIT License](LICENSE). Character images, wallpaper-derived clips and icons **do not inherit that grant**. See [ASSET-LICENSE.md](ASSET-LICENSE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The three states originate from user-selected Wallpaper Engine scenes by **柊明**. Image editing generated transparent cutouts with some reconstruction, then local light from the source videos was composited onto those fixed bodies. This is not a claim of pixel-identical preservation of the original character animation. Runtime independence from Wallpaper Engine does not change the assets' separate rights and redistribution conditions. Provenance and processing details are in [ASSET-NOTES.txt](app/ASSET-NOTES.txt) and the asset manifests.

This is a personal, unofficial utility, unaffiliated with OpenAI, Anthropic, Microsoft or the character/art rights holders.
