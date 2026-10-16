Tiga Energy Monitor v2.0.0 — English Edition

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
Real-account authorization and subsequent live readings have not been
end-to-end verified for this release. No real login was performed in tests.

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
Hover a quota for reset time and freshness. Full account details are in settings.
Widget scale options are 80%, 100%, 125% and 150%.

STARTUP AND DATA
“Start with Codex” and “Start with Claude Code” are separate opt-in settings,
both off by default. Enabling either registers this application's own small
current-user startup watcher. Turning both off removes that registration.
Closing Codex or Claude Code leaves the monitor running independently.

Settings and isolated account profiles: %LOCALAPPDATA%\TigaEnergyMonitorV2
Upgrading portable program files preserves this separate data directory.
The Chinese and English editions use the same application profile; run one
edition at a time. Only three safe display/refresh preferences migrate from v1.
The application is unsigned. No administrator account is required.

SOURCE, LICENSES AND MEDIA
Source and release notes:
https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor
English release:
https://github.com/skyXIANGTIAN13152/chatgpt-codex-usage-monitor/releases/tag/v2.0.0-en

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
