# Changelog

All notable changes to superwarp companion are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/); versioning is
[SemVer](https://semver.org/).

## [0.2.0] - 2026-05-27

Full-feature-parity port of the Rust GUI (`superwarp-gui` v0.9.4) to
C++/Dear ImGui. The earlier v0.1.x was a thin command-relay demo; this drops
the relay model entirely and speaks the typed JSON protocol directly to
Eric's superwarp fork over loopback (default `127.0.0.1:19519`).

### Added (new functionality, mostly mirrored from the Rust GUI)

- **Typed JSON protocol layer** (`proto.{h,cpp}`): full ServerMsg parsing
  (`state` / `lists` / `unlocks` / `unlocks_all` / `msg`) and command
  builders (`warp_to` / `sub_cmd` / `cancel` / `scan` / `debug` / `get_*`).
  Backed by vendored nlohmann/json v3.11.3.
- **Real network layer** (`net.{h,cpp}`): worker-thread Winsock client with
  800ms connect timeout, 3s reconnect backoff, non-blocking I/O, line-
  buffered framing, configurable endpoint, ConsumeConnectedEvent so the app
  can re-request state/lists/unlocks on every reconnect.
- **Fuzzy matcher** (`fuzzy.{h,cpp}`): direct port of the Rust scorer (which
  itself is byte-identical to the in-game Lua scorer). Three tiers via
  `dest_fuzzy` (exact / gap-aware subsequence) and `dest_approx`
  (Levenshtein fallback). Constants: F_MATCH=16, F_GAP_START=-3,
  F_GAP_EXT=-1, F_BON_BOUNDARY=8, F_BON_CAMEL=7, F_BON_CONSEC=8,
  F_FIRST_MULT=2, FUZZY_EXACT=1_000_000.
- **Persisted config** (`config.{h,cpp}`): `superwarp_gui.json` next to the
  exe. Fields: `window_x/y/w/h`, `always_on_top`, `last_system`,
  `system_order`, `host`, `port`. File format matches the Rust GUI's exactly
  (interchangeable between the two implementations).
- **Aether theme** (`theme.{h,cpp}`): the Rust GUI's palette translated to
  ImGui style colors. BG/PANEL/INPUT/HOVER, AZURE/AZURE_DIM/VIOLET,
  TEXT/MUTED/WHITE, LOCKED/UNLOCKED/GOLD/PARTY_COL/ALL_COL.
- **Header bar** (`ui_header.cpp`): painted multicolor double-chevron
  (violet back + azure front, sharp 60deg) — geometry painted directly via
  draw list since a two-tone glyph isn't expressible as a single character.
  `superwarp` + version + right-aligned `pin` (always-on-top) + advanced
  toggle + connection status. Accent line. Second row: character + zone(id)
  in violet/white + BUSY badge with cancel.
- **Systems sidebar** (`ui_systems.cpp`): per-system unlock counts
  (`121/121 unlocked` in green when complete) or `N destinations` for
  untracked systems, `~partial` suffix for zone-gated catalogues.
  **Animated drag-to-reorder** with per-row exponential-decay easing
  toward target slots (`tau ≈ ANIM*0.45`), foreground floating tab
  following the cursor with drop shadow, gap indicator at the insertion
  slot. ImGui has no FLIP/animate_value, so the lerp is hand-rolled with
  `ImGui::GetIO().DeltaTime`. 6px drag-start threshold; click-vs-drag
  disambiguated via `IsItemDeactivated` + `IsMouseDragging`.
- **Destinations panel** (`ui_dests.cpp`): header (long name + `[short]`),
  scan chip + Send-mode toggle (`Off`/`Party`/`ALL`), metadata row
  (currency in gold + `scanned 2m ago` muted), global search box, action
  chips row (selected system's sub-commands + `cancel / reset`, hidden
  during search), row list with state dot (`o`/`x`/`.`), click-to-warp
  label, right-aligned `[system_long]` tag during search, cost column
  with affordability tinting. "no direct match -- closest:" header for
  the approx tier.
- **Advanced view** (`ui_advanced.cpp`): editable host + port with
  apply+reconnect and defaults reset, addon-debug checkbox driving
  `//sw debug`, log line count + clear button + 220px scrolling log
  console, GUI + addon version + byline.
- **Footer** (`ui_footer.cpp`): last 3 log lines + offline reason.
- **Window-state persistence** (`main.cpp`): restore position + size on
  startup, persist on change (throttled to 3s), restore always-on-top on
  startup, toggle HWND_TOPMOST/HWND_NOTOPMOST in response to the pin
  checkbox. Dynamic title bar: `{character} - superwarp v{addon} (GUI v{gui})`.
- **Search auto-clear**: warping from a search result queues a clear; the
  next not-busy state from the addon clears the search box. Drops the
  queue if the user edits the box again.

### Removed

- The v0.1.x **superwarp_hub.lua** relay addon. The fork's own
  `bridge.lua` exposes the JSON protocol directly; no relay is needed.
- The v0.1.x command-string UI. v0.2.0 replaces it entirely with the
  feature-parity port.

### Build

- `vcxproj` Release | Win32 stays size-tuned: `/O1` favor-size, LTCG,
  REF + ICF, `/MT`, RTTI off, `DebugInformationFormat=None`.
- Include path additions: `$(ProjectDir)third_party` for the vendored
  nlohmann/json single header at `third_party/nlohmann/json.hpp`.
- New source files compiled in: `app.cpp`, `proto.cpp`, `fuzzy.cpp`,
  `config.cpp`, `theme.cpp`, `util.cpp`, and the five `ui_*.cpp` modules.
- Realistic size after build: ~1.2 – 1.5 MiB (vs Rust's 2.2 MiB floor).

### Notes

- Same `superwarp_gui.json` shape as the Rust GUI — configs are
  cross-readable between the two.
- The on-wire protocol is unchanged, so this companion connects to any
  bridge-enabled superwarp build that worked with the Rust GUI.

## [0.1.1] - 2026-05-27

Build fixes after the first user build on VS 18 / MSVC 14.50 (toolset v145).

### Fixed
- `s_host` macro collision in `<inaddr.h>`: renamed `std::string s_host`
  to `s_remote_host` and added a defensive `#undef` block for the BSD-
  compat macros (`s_host` / `s_net` / `s_imp` / `s_impno` / `s_lh`).
  Cause of the C3927 / C2146 / inet_pton arg-count cascade.
- Compile-time PDB conflict (C1041): Release now sets
  `DebugInformationFormat=None`; Debug uses `OldStyle` (`/Z7`).

## [0.1.0] - 2026-05-27

Initial command-relay demo (superseded by 0.2.0).
