# ChatGPT Codex Usage Monitor — English Edition

An unofficial, lightweight Windows HUD for showing Codex usage windows alongside the ChatGPT desktop app. It is written in C++20 with native Win32 and Direct2D. The monitor follows the ChatGPT desktop window, reads rate-limit data from the locally installed official Codex CLI App Server, and exits after all eligible ChatGPT windows have been closed for five seconds. The default is an 80% compact layout (about 240×72); the tray menu provides 5% steps from 60% to 140% and scales the chest, energy lens, bars/rings, typography, and ion particles together. In dual-quota mode, a deeper outer ring represents weekly remaining usage and a brighter inner ring represents the five-hour window; bar mode uses the same mapping as upper and lower bars. A weekly-only option is also available. The selected size, visual form, and quota mode are saved and reused as the next launch defaults.

## What it shows

- Remaining percentages for the weekly and five-hour usage windows;
- Separate reset countdowns and local reset times for both windows;
- Nested dual rings, paired bars, and a weekly-only mode;
- Model-specific buckets when supplied by the official interface;
- Credits balance, plan type, last successful update, and stale/offline state;
- LIVE, WARNING, CRITICAL, STONE, STALE, OFFLINE, not-logged-in, and CLI-not-installed states.

The monitor displays the percentages returned by the official interface. It does not pretend that a rate-limit percentage is a token count, context-window statistic, or API billing value.

## Theme and visual states

The optional tribute theme uses the owner-provided chest artwork and an original Direct2D background inspired by light, dawn, and energy. The chest geometry stays aligned across states. Weekly usage uses the deeper blue-violet outer/upper layer, while five-hour usage uses the brighter cyan inner/lower layer. Each quota changes to its own deep-red or bright-red warning palette without blinking; only the chest lens blinks. In dual mode, the lens follows whichever window has less remaining usage. The progress display can be rendered as paired luminous bars or nested circular rings:

- Above the warning threshold: blue energy lens and cyan quantum UI typography;
- Below the threshold: the original red warning lens with synchronized red/pink ion typography;
- At 0%: the entire chest enters a low-contrast stone state and the UI ion particles go dormant.

The monitor icon is a transparent multi-size device cutout. Small tray sizes emphasize the recognizable U-shaped top, while larger sizes show the complete device. The desktop ChatGPT launcher shortcut may continue to use a separate standard ChatGPT icon.

![Normal state](docs/images/demo-normal.png)
![Warning state](docs/images/demo-warning.png)
![Stone state](docs/images/demo-stone.png)

## Download

Download the latest Windows portable package from [Releases](../../releases/latest). Extract the complete ZIP and start `ChatGPTCodexUsageMonitor.exe`. The full package includes the official Codex CLI executable and its license/notice files.

The first run requires:

1. ChatGPT for Windows installed from Microsoft Store/MSIX;
2. A ChatGPT account signed in to the desktop app;
3. Codex CLI signed in through its official ChatGPT login flow.

The monitor does not copy OAuth tokens, read cookies, install a service, require administrator access, or add a startup task. It communicates with the local Codex App Server and only displays the returned usage fields.

Use the tray menu to switch between **Weekly + 5 hours** and **Weekly only**, and between **Light-energy bars** and **Light-energy rings**. These choices persist across restarts.

## Build from source

Requirements:

- Windows 10 version 1903 or later; Windows 11 x64 recommended;
- Visual Studio/MSVC with C++20 and CMake;
- PowerShell 5+;
- Python with Pillow only if regenerating the transparent application icon.

From a Developer PowerShell or a prompt with MSVC available:

```powershell
cmd /c scripts\build-release.cmd
```

The build regenerates the theme variants, compiles the Win32 application, runs the four automated tests, and packages the verified official Codex CLI. The CLI binary is intentionally ignored by Git and is distributed through Releases instead of the source repository.

## Project layout

```text
src/          C++ application and rendering code
include/      Public headers and state models
docs/         Architecture, protocol, security, performance, and visual notes
resources/    Theme images, transparent icon, Win32 resources
scripts/      Build, asset, shortcut, and icon-generation helpers
tests/        Unit, lifecycle, fake-server, and theme-alignment tests
```

## License and artwork

The source code and build scripts are released under the MIT License. The chest artwork and monitor icon are governed separately by [ASSET-LICENSE.md](ASSET-LICENSE.md); the MIT grant does not automatically grant reuse of those images. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the official Codex CLI notice.

This is a personal, unofficial utility and is not affiliated with OpenAI, Microsoft, or any character/art rights holder.
