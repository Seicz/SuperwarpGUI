# Changelog

All notable changes to superwarp companion are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/); versioning is
[SemVer](https://semver.org/).

## [0.1.0] - 2026-05-27

Initial demo.

### Added
- Win32 + Direct3D11 + Dear ImGui (v1.92.8, vendored) host application.
- Data-driven panel mirroring the full superwarp command surface: Homepoint,
  Waypoint, Proto-Waypoint, Survival Guide, Escha/Reis, Unity, Abyssea, Runic
  Portal, Voidwatch, Sortie, Odyssey, Limbus, Campaign, Incursion, Walk of
  Echoes, plus the smart command and global cancel/reset/missing/debug.
- Target selector (Me / Party / All) applied to every dispatched command.
- Per-system destination input with fuzzy-name hints; sub-command and
  list-missing buttons generated from the system table.
- On-screen log of every outgoing command line.
- `net` Winsock loopback client: one worker thread, queued fire-and-forget
  sends, timeout-guarded non-blocking connect, auto-reconnect.
- `superwarp_hub.lua`: generic loopback relay addon (`windower.send_command`),
  `//swhub status|on|off`, single-companion accept with line buffering.
- Size-tuned Release config (`/MT`, `/O1` favor-size, LTCG, REF+ICF, RTTI off).

### Notes
- Generic relay by design — forward-compatible with a combined
  superwarp/fisher/crucible companion.
- Binary size unverified in the authoring environment (no Win32 D3D11 compiler);
  README size figures are estimates pending a real build.
