# Changelog

All notable changes to superwarp companion are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/); versioning is
[SemVer](https://semver.org/).

## [0.2.9] - 2026-05-28

### Fixed
- **Advanced > Diagnostics checkbox label was clipping at the right
  edge** ("...verbose addon loggir"). ImGui's `Checkbox` is a single-
  line label that has no wrap behavior and just clips at the parent's
  right boundary. Shortened the label to `addon debug (//sw debug)` and
  moved the verbose explanation into an on-hover tooltip describing the
  toggle's behavior + the fact that the checkbox reflects the addon's
  reported state.

## [0.2.8] - 2026-05-28

### Added
- **Embedded application icon.** `resources/icon.ico` is now compiled
  into the exe via `superwarp-companion.rc` (built by the project's
  `ResourceCompile` item). The icon shows up in Explorer, the title
  bar, the taskbar, and alt-tab. `main.cpp` loads `IDI_APPICON` and
  passes it to both `hIcon` and `hIconSm` on the window class.

### Changed
- **Variable row heights in the SYSTEMS sidebar.** Rows that show a
  count (`121/121 unlocked`, `48 destinations`) keep the 34-unit tall
  height; rows with no count -- Mog Garden, Spatial displacement, Walk
  Of Echoes, etc. -- shrink to 22 units, eliminating the ~40% dead-band
  below each label-only system. Slot positions now derive from a prefix
  sum of per-row heights rather than `slot * ROW_H`. The drag-reorder
  insertion logic now walks the cumulative heights to find the slot
  whose midpoint the ghost is closest to (so a tall row's gap opens by
  34 units, a short row's gap opens by 22 units).

## [0.2.7] - 2026-05-28

### Changed
- **Default sidebar width is now 140 (was 190)** so the destinations
  panel gets the majority of the horizontal real estate. The full long-
  form destination labels (`Yorcia Weald - Enigmatic Device`,
  `Western Adoulin - Couriers / Platea / Triumphus`) now fit without
  truncation at typical window widths.

### Added
- **Sidebar width persists to `superwarp_gui.json`.** Drag the resize
  handle and the new width is saved (on release, throttled save fires
  after 3s or on shutdown) so the layout sticks across sessions. Stored
  as `sidebar_width` (DPI-independent / unscaled), so the same JSON
  loads correctly on a 100% display and a 250% one.

## [0.2.6] - 2026-05-27

### Fixed
- **Sidebar scrollbar was always visible** even when every system fit
  on-screen. The `##sysscroll` child had `ImGuiWindowFlags_AlwaysVerticalScrollbar`
  set; now uses the default (scrollbar appears only when content overflows).
- **"scan" button clipped on the right** of the Destinations header. The
  right-align math estimated each `SmallButton`'s width as
  `text_width + 14` -- a 1.0x-scale guess. The real button width is
  `text_width + 2 * FramePadding.x`, which at 250% DPI is ~30px of
  padding per button, so the cluster ended up ~30-40px wider than the
  alignment math thought and the rightmost button overflowed past the
  panel edge. Now reads `FramePadding.x` and `ItemSpacing.x` from the
  live style so the alignment is correct at any DPI scale.

## [0.2.5] - 2026-05-27

### Fixed
- **Footer was a fixed-height "chat-log slab" that grew with the window.**
  The bottom strip was hard-coded at `56 * dpi` pixels regardless of how
  much it actually had to show, so once the window was stretched tall the
  footer turned into a big empty INPUT-colored panel beneath one line of
  text. Now the footer auto-sizes from its content (up to 3 most-recent
  log lines + optional connect-error line), the way Rust's
  `TopBottomPanel::bottom` does. An empty footer collapses to a thin
  padding strip.
- **Body height wasn't accounting for `ItemSpacing.y` between successive
  child windows.** `body_h = total - header - footer` would push the
  footer 2 * spacing pixels below the visible region (footer's bottom
  was clipped, its top showed the dead INPUT band described above).
  Now subtracts `2 * ItemSpacing.y` from `body_h` so the three children
  stack exactly within the available height.

## [0.2.4] - 2026-05-27

### Changed (visual parity with the Rust GUI)

- **Destination state dots are now Unicode** (●○·) instead of ASCII
  (o/x/.). Matches the Rust GUI exactly. The FFXI-chat-Unicode gotcha
  doesn't apply here -- the companion is its own desktop window, the
  Segoe UI atlas easily covers these glyphs, and ImGui renders them at
  the same crispness as everything else.
- **Em-dash in the window title** (`Seicz \u2014 superwarp v1.6.0 ...`)
  to match the Rust GUI's title formatting.
- **Header "superwarp" title bumped to 1.4x font size** via a custom
  `AddText` render. Mirrors Rust's `monospace(16)` vs body `monospace(11)`
  ratio so the banner looks like a banner rather than just another label.
- **Sidebar layout density.** Pushed `WindowPadding(0,0)` on the inner
  scroll child so the first system row sits tight under "SYSTEMS" rather
  than 20+ pixels below it at high-DPI. The `HorizontalScrollbar` flag on
  the same child was dropped (long labels just clip at the right edge
  now, matching the Rust ScrollArea behavior).
- **Base font reduced to 12pt (was 14pt)** to bring overall text density
  in line with the Rust GUI's Hack 11pt. At 250%-scale that's 30px text
  instead of 35px; rows feel tighter, more system entries fit visibly,
  and the proportions of label-to-count-to-row-height match Rust.
- **Glyph ranges expanded** in `BuildFontAtScale`: now loads Basic Latin +
  Latin-1 (includes `·`), General Punctuation 0x2010-0x2030 (includes
  `\u2014` em-dash + ellipsis + bullet), and Geometric Shapes 0x25A0-0x25FF
  (includes `\u25CF` and `\u25CB`). Without these the atlas would render
  the new glyphs as little boxes.

## [0.2.3] - 2026-05-27

### Fixed
- **Dark title bar.** The native window chrome was rendering with the
  Windows light theme regardless of the system setting, so a white slab
  sat above the aether panel. Now opts into DWM dark mode via
  `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE, TRUE)` immediately
  after window creation. Tries value 20 (Windows 10 20H1+) first, falls
  back to 19 (Windows 10 1809 - 19H2). No-ops cleanly on older Windows.
  `dwmapi.lib` is auto-linked via `#pragma comment`, no vcxproj change.
- **Sidebar row overlap at high-DPI.** v0.2.2 scaled the font + ImGui
  Style, but every hand-painted layout pixel (sidebar `ROW_H = 34.0f`,
  `PAD_X = 5.0f`, paint offsets, chevron geometry, accent line, header /
  footer child heights, default sidebar width, log console height,
  drop-shadow + corner rounding) was still raw and 96-DPI-sized. On a
  250% scale display, a 35px font was painting into a 34px row -> labels
  spilled into the next row. Now every hand-painted constant multiplies
  through a new `ui::dpi()` helper that derives the scale from the
  rendered font size (`ImGui::GetFontSize() / BASE_FONT_PX`). Everything
  paints at proportional size at any display scale.
- Sidebar resize handle delta is now divided by `dpi()` so dragging at
  high-DPI moves the handle by the same logical amount as at 100%, and
  the stored width is scale-independent.

## [0.2.2] - 2026-05-27

### Fixed
- **High-DPI rendering.** `ImGui_ImplWin32_EnableDpiAwareness()` was being
  called (so Windows wasn't bitmap-scaling the window), but ImGui itself
  was still painting at 96-DPI logical pixels - which meant a 250%-scale
  display showed the entire panel at ~40% of intended size. Now:
  - At startup, query `ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd)` and build
    the font at `14.0f * scale` pixels (prefers Segoe UI from
    `C:\Windows\Fonts`, falls back to ProggyClean default).
  - `ImGui::GetStyle().ScaleAllSizes(scale)` so paddings / spacings /
    rounding / framing all scale proportionally.
  - First-launch default window size scaled by `GetDpiForSystem()` so a
    fresh install doesn't open as a tiny rectangle on a high-DPI display.
    Persisted geometry is honored as-is (already in physical pixels).
  - `WM_DPICHANGED` is now handled: window snaps to the OS-suggested rect
    and the font atlas + style are rebuilt at the new scale on the next
    frame (via a pending-scale flag, `ImGui_ImplDX11_InvalidateDeviceObjects`
    / `_CreateDeviceObjects`, and a re-applied `theme::setup`). Lets you
    drag the window between monitors of different DPI without restarting.

## [0.2.1] - 2026-05-27

### Changed
- `PlatformToolset` bumped from `v143` to `v145` in both Release and Debug
  configs so the project retargets cleanly on VS installs that ship only
  the latest MSVC Build Tools (14.50+). No code changes; same source
  compiles under either toolset.

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
