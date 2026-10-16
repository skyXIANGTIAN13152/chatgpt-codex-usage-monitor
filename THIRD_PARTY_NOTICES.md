# Third-party notices

## Official Codex CLI

The full Windows portable Release includes the official Codex CLI executable
used by the local App Server integration. Its Apache-2.0 license and NOTICE
file are included beside the executable in the Release package. The large CLI
binary is intentionally not committed to this source repository.

## ChatGPT for Windows

The monitor can follow a Microsoft Store/MSIX ChatGPT desktop installation. It
does not include, modify, or redistribute the ChatGPT desktop application.

## v2.0.0 runtime additions

The sections above are retained for the legacy monitor. v2 is an independent
application; a ChatGPT desktop installation is no longer required. The release
includes third-party components under their own terms, separate from the
project's MIT license:

| Component | Role | License / notice |
| --- | --- | --- |
| Electron 44.7.2 and its bundled Chromium dependencies | Desktop host and transparent video rendering | Electron's MIT license and bundled third-party license notices |
| Official OpenAI Codex CLI | Local App Server login and usage requests | Apache-2.0; original Codex LICENSE and NOTICE are retained in the portable package |
| Official Claude Code 2.1.292, Windows x64 | Official login and experimental `/usage` reading | Unmodified official component, subject to [Anthropic's applicable terms](https://code.claude.com/docs/en/legal-and-compliance); not relicensed under MIT |
| Node.js 24.19.0 | Separate Claude worker runtime | Node.js license and bundled third-party notices, retained as `licenses/Node-LICENSE.txt` |
| node-pty 1.1.0 | Windows pseudoterminal integration | MIT, with its package license and included component notices |
| @xterm/headless 6.0.0 | Parsing the official terminal's rendered text | MIT; [xterm.js upstream](https://github.com/xtermjs/xterm.js) |

The Claude binary's source URL, version, SHA-256 and checked signature are
recorded in [its runtime manifest](app/native/claude/runtime-manifest.json).
It has not been patched. The integration does not constitute a separate
third-party Claude OAuth authorization. Its implementation and verification
limits are described in [integration-notes.txt](app/integration-notes.txt).
Large third-party executable binaries are distributed with release packages,
not committed to the source tree.

The C++ quota parser and energy-state logic reused from v1 retain their
original MIT notice. Their source and reuse record are in
`app/native/core-helper/`; the Windows startup watcher also reuses the legacy
process enumeration and executable-path lookup logic.

Playwright and rcedit are development/build dependencies, not account-data
providers. Their versions are fixed by the application lockfile; their own
package licenses apply.

## Wallpaper-derived media and generated artwork

The v2 character media derives from three Wallpaper Engine scenes by **柊明**:
Steam Workshop [3778523054](https://steamcommunity.com/sharedfiles/filedetails/?id=3778523054),
[3778488689](https://steamcommunity.com/sharedfiles/filedetails/?id=3778488689)
and [3778530158](https://steamcommunity.com/sharedfiles/filedetails/?id=3778530158).
Image editing reconstructed transparent cutouts; the displayed bodies remain
fixed while selected source-derived lighting changes. Wallpaper Engine and
Steam are not included or required to play the rendered media.

Wallpaper, character and icon rights are separate from the software license.
Neither rendering nor AI editing converts them into MIT-licensed artwork.
See [ASSET-LICENSE.md](ASSET-LICENSE.md) for attribution, asset scope and
the absence of an additional third-party reuse grant.
