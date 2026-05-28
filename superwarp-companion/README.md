# superwarp companion (v0.2.9)

A native **C++ / Dear ImGui** companion-window front end for Eric Strawser's
fork of [superwarp](https://github.com/AkadenTK/superwarp) (originally by
Akaden, with the JSON bridge + fuzzy search added in the fork). It connects
directly to the fork's TCP bridge and renders the full feature surface as a
clickable panel.

This is the **C++ port of the Rust/egui GUI** (`superwarp-gui`), built to
benchmark binary size and to ship a leaner option alongside the Rust version.
Feature parity is the goal; the protocol on the wire is unchanged.

> by Eric Strawser (Seicz@Bahamut) · ITIWH.com

---

## What you get

- One window, ambient-aether theme: deep indigo panels, azure accents,
  violet for character + system tags, rose for locked / coral for "ALL"
  dispatch. Same palette as the Rust GUI, just rebuilt with ImGui style colors.
- **Systems sidebar** with per-system unlock counts (`121/121 unlocked`,
  green when complete), `~partial` flag for zone-gated catalogues, and
  **animated drag-to-reorder** (per-row easing, foreground floating tab with
  drop shadow, gap indicator, persisted to `superwarp_gui.json`).
- **Destinations panel**: header (long name + `[short]`), scan button +
  Send-mode toggle (`Off` / `Party` / `ALL`), metadata line (currency in gold,
  `scanned 2m ago` in muted), global search box, **action chips** (the
  selected system's sub-commands + a `cancel / reset` chip), and the
  destination list with state dots (`o` unlocked, `x` locked, `.` unknown),
  click-to-warp, cost column with affordability tinting.
- **Global fuzzy search** across every system, ranked best-first; falls back
  to an approx (edit-distance) tier with a "no direct match -- closest:"
  note. The scorer is a one-for-one port of the Rust scorer (which is
  byte-identical to the in-game Lua scorer).
- **Advanced view**: editable host + port with apply-and-reconnect,
  defaults reset, addon-debug toggle (`//sw debug` over the bridge), log
  console with clear, GUI + addon version, byline.
- **Window state persistence** in `superwarp_gui.json` next to the exe:
  position, size, always-on-top (`pin` checkbox in the header), last
  selected system, system order, host/port. File format matches the Rust
  GUI's so configs can be moved either direction.

## Why the C++ port

The Rust GUI lands at ~2.2 MiB after the size-optimization passes (v0.9.3 +
v0.9.4: stripped accesskit/wayland/x11/web_screen_reader, default to glow,
vendored Hack font, raw-RGBA icon; the `arboard` clipboard transitively
pulls `image`/`png` into the .text and that's the binding floor for any
egui/winit app).

This port targets ~1.2 – 1.5 MiB. Same protocol, same theme, same scorer.

## Architecture

```
  +-----------------------------+      loopback TCP        +------------------------+
  |  superwarp-companion.exe    |  127.0.0.1:19519         |  superwarp.lua          |
  |  (this project)             | <----------------------> |  (Eric's fork)          |
  |                             |   newline-delimited      |                         |
  |  Win32 + D3D11 + ImGui      |   JSON commands          |  bridge.lua: TCP server |
  |  Winsock client (1 thread)  |   JSON state/lists/      |  state + catalogue +    |
  |                             |   unlocks/msg events     |  unlock cache pushers   |
  +-----------------------------+                          +------------------------+
                                                                       |
                                                                       v
                                                          superwarp (//sw ...) in game
```

No D3D hooking, no injection. The companion is an ordinary desktop window; the
game stays untouched. **No hub addon needed** — the fork's own bridge speaks
the typed JSON protocol directly.

## Layout

```
superwarp-companion/
  src/
    main.cpp           Win32 + D3D11 bootstrap, window lifecycle, title bar
    app.{h,cpp}        App state + frame loop + message handling + commands
    proto.{h,cpp}      ServerMsg / SystemList / Dest / UnlockSystem + JSON
    net.{h,cpp}        Winsock client (1 worker thread, auto-reconnect)
    fuzzy.{h,cpp}      fzf-style scorer + Levenshtein, three-tier search
    config.{h,cpp}     superwarp_gui.json (next to the exe)
    theme.{h,cpp}      aether palette + ImGui style colors
    util.{h,cpp}       destkey, time_ago, fmt_thousands, sub_order
    ui_header.cpp      title bar with painted chevron + status + pin
    ui_systems.cpp     sidebar with animated drag-reorder
    ui_dests.cpp       search / actions / row list
    ui_advanced.cpp    connection / diagnostics / about
    ui_footer.cpp      last log lines + offline reason
  imgui/               vendored Dear ImGui v1.92.8 (MIT) + Win32/DX11 backends
  third_party/
    nlohmann/json.hpp  vendored nlohmann/json v3.11.3 (MIT)
  superwarp-companion.sln / .vcxproj
```

## Build

1. Open `superwarp-companion.sln` in Visual Studio 2022 (toolset v143).
2. **Release | Win32**.
3. Build. Output lands in `build/Release/superwarp-companion.exe`.

Release config is size-tuned: `/O1` favor-size, LTCG, REF + ICF, `/MT`, RTTI
off, no PDB. Same flags as v0.1.1.

## Run

1. Have Eric's fork of superwarp loaded in Windower (`//lua load superwarp`).
   The bridge listens on `127.0.0.1:19519` by default.
2. Run `superwarp-companion.exe`. It connects automatically.

You can change the endpoint at runtime via **advanced -> Connection**. It's
saved to `superwarp_gui.json`.

## Compatibility

The on-wire protocol is the same one the Rust GUI uses, so this companion
talks to **any** version of superwarp's fork that exposes the JSON bridge.
The config file format (`superwarp_gui.json`) is interchangeable between
the two GUIs.

## Credits

- **superwarp** — original addon by Akaden; JSON-bridge + fuzzy-search fork
  by Eric Strawser.
- **fuzzyfind** scorer — research+assembly by Lili; gap-aware scorer + lean
  build by Eric Strawser. Lua → Rust port by ES; Rust → C++ port for this
  project.
- **Dear ImGui** v1.92.8 by Omar Cornut (MIT), vendored under `imgui/`.
- **nlohmann/json** v3.11.3 (MIT), vendored under `third_party/nlohmann/`.
- AI assistance: pair-programming with Claude.

## License

MIT — see `LICENSE`. Bundled libraries retain their own licenses (`imgui/LICENSE.txt`).
