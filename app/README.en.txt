Tiga Energy Monitor v2.0.2 — English Edition

THIS UPDATE
Fixes the widget and settings window being covered by ordinary windows
despite Always on top being enabled. Both windows now use the correct native
topmost level, avoiding loss of the native flag when the primary taskbar is
not topmost. Turning Always on top off still follows your preference.
Native A/B checks, six regression groups and 119 unit tests passed. The installed
widget's native topmost flag was confirmed, with account data and settings kept.
Both language editions include the fix. Existing automatic refresh, reset
countdowns, pause controls and compact layout remain available.

START
Extract the entire portable ZIP, then run TigaEnergyMonitor.exe.
Windows x64 is required. Codex, ChatGPT Desktop, Steam and Wallpaper Engine
do not need to be running. The package includes its application runtimes.

The character and quota information float directly over the desktop.
Drag the character to move it. Right-click it to open settings. The toolbar
stays visible: Refresh, Pause animation, Pin, Settings, Hide to tray.
Click the tray icon to restore the widget; use its Exit command to quit.
Closing the settings window does not close the floating widget.

ACCOUNTS
Use “Sign in with ChatGPT” for Codex and “Sign in with Claude Code” for Claude.
Authorization uses the bundled, unmodified official components and their
official login flow. Existing external CLI credentials are not copied.
Each provider can be shown or hidden independently. Hiding an account moves
the remaining information and toolbar up without leaving an empty slot.

Claude usage reading is experimental. It parses the official Claude Code
/usage screen. First-run setup, account permissions or upstream UI changes
may prevent a reading. Follow the official setup link when requested, or use
the official usage page. Missing readings are not replaced with sample data.
Existing authorized Codex readings on the local machine have recovered and
were visually confirmed. Multi-scenario automated regressions use isolated
fixture providers, rather than live accounts. End-to-end official Claude Code
authorization followed by actual usage reading has not been completed.

DISPLAY AND ENERGY
Choose either Energy bars or Energy rings. The two modes are mutually exclusive.
Each account has its own meters: weekly is the left bar / outer ring;
five-hour is the right bar / inner ring. A missing period becomes a single meter.
The center of a ring group prioritizes the five-hour remaining percentage.

Codex uses blue/cyan above or at 50%, and rose/blood red below 50%.
Claude Code uses white/bright yellow above or at 50%, and deep yellow/orange red
below 50%. Each period is colored independently. Real zero uses a limestone
finish; low meters retain metallic reflections. Vessel decorations are removed.

The energy target can be Codex, Claude Code or an aggregate of selected accounts.
Aggregation is an equal-weight average of remaining percentages, calculated
separately for each period; it is not a transferable pool of provider credits.
Character state prioritizes five-hour allowance whenever that period is present.
Weekly allowance is used only when a five-hour period is absent. If the required
period is missing, stale or reset, the character waits for a valid update.
Showing or hiding an account does not change the selected energy target.
The default red-light threshold is 20%; it can be changed independently of
the meters' 50% color threshold. A controlling value of zero activates stone.

Three transparent scenes retain animated lighting with a fixed character
silhouette. Scene changes crossfade over 1.8 seconds. Pause stops only the
visuals; quota updates continue. Reduced motion switches to static scenes.
Pressing Play can override the system or application static preference.
Each period shows its own live reset countdown beside its bar or ring, even when
animation is paused. Unknown reset times and elapsed deadlines are labeled;
the app waits for official data instead of guessing a renewed allowance.
Hover a quota for its full reset date and freshness. Full details are in settings.
Quotas refresh automatically at the selected interval. Display and pause changes
do not postpone updates. Resets and waking the computer trigger a new sync;
failed reads retry after at least 30 seconds. Manual, timed and wake refreshes
do not overlap. Cached readings remain usable for the
longer of three minutes or the refresh interval plus two minutes, but never
beyond their reset deadline. Missing or expired readings are not guessed.
Widget scale options are 80%, 100%, 125% and 150%.

STARTUP AND DATA
“Start with Codex” and “Start with Claude Code” are separate opt-in settings,
both off by default. Enabling either registers this application's own small
current-user startup watcher. Turning both off removes that registration.
Closing Codex or Claude Code leaves the monitor running independently.

Settings and isolated account profiles: %LOCALAPPDATA%\TigaEnergyMonitorV2
Upgrading portable program files preserves this separate data directory.
Exit the app completely from its tray before replacing the full program folder.
Keep your own v2 profile; do not copy another person's profile directory.
The Chinese and English editions use the same application profile; run one
edition at a time. Only three safe display/refresh preferences migrate from v1.
The application is unsigned. No administrator account is required.

SOURCE, LICENSES AND MEDIA
Source and release notes:
https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor
English release:
https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.2-en
English portable package:
https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2-en/TigaEnergyMonitor-v2.0.2-en-windows-x64-portable.zip
Shared bilingual source:
https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2-en/TigaEnergyMonitor-v2.0.2-source.zip
Checksums for both language packages:
https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.2-en/TigaEnergyMonitor-v2.0.2-SHA256SUMS.txt
The demo remains the v2.0.0 sample-account recording; it is not a new v2.0.2 video:
https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/download/v2.0.0/TigaEnergyMonitor-v2.0.0-demo.mp4

Project code continues under MIT. Third-party programs and wallpaper-derived
media retain their own terms; they are not relicensed by the project's MIT file.
See LICENSE, ASSET-LICENSE.md, THIRD_PARTY_NOTICES.md and licenses/.
The three wallpaper sources are by 柊明, Workshop IDs 3778523054,
3778488689 and 3778530158. Generated cutouts preserve an unobstructed silhouette
but reconstruct some details; the animation uses source lighting rather than
claiming to preserve all original character motion. See ASSET-NOTES.txt and
resources/app/ui/assets/characters/manifest.json for production records.

BUILD
The repository's app/ directory contains the v2 application source.
Install the pinned pnpm dependencies in app/ and app/backend/claude-runtime/.
Build both native helpers from an MSVC x64 development terminal using their
native/core-helper/build.cmd and native/launch-watcher/build.cmd entry points.
Obtain the official runtime binaries from this release's resources/app/ tree.
Run node --test tests/*.test.cjs, then:
node scripts/package.cjs <output-directory> --locale=en
The output must be inside the parent workspace and outside the source/staging trees.
