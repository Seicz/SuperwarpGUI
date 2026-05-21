# Changelog — superwarp GUI

Semantic versioning. This GUI versions independently of the addon (which is at
v1.3.1). Development began at Stage 4 of the superwarp fork.

---

## [0.1.0] — Stage 4a: foundation

First compiling cut: the bridge, the protocol, and a usable browse/warp UI.

- **Bridge.** Background connect thread with a 1s `connect_timeout`, result handed
  back over an mpsc channel, socket set non-blocking, line-delimited JSON read in
  the egui update loop, 3-second auto-reconnect, commands written with a trailing
  newline. Mirrors the Fisher GUI's proven pattern.
- **Protocol.** `serde` types tagged on `t`: `state`, `lists`, `unlocks`,
  `unlocks_all`, `msg`. Commands: `warp_to` (flat and nested), `scan`, `cancel`,
  `get_state` / `get_lists` / `get_unlocks` (re-requested on every connect).
  Outgoing commands are built with `serde_json` so zone names with apostrophes
  (Escha Zi'tah, Behemoth's Dominion) escape correctly.
- **UI.** System browser (left) with per-system `unlocked/known` counts; a
  destination list (centre) with a state dot (unlocked/locked/unknown), click-to-
  warp, and cost tinted by affordability; a Scan button and last-seen currency per
  system; a search box filtering by label/zone/alias; a header with character,
  zone, connection status, a BUSY+cancel control, and an always-on-top pin; a
  footer log strip.
- **Aether theme.** Deep indigo console, luminous azure accent, arcane-violet
  highlight — deliberately distinct from the Fisher GUI's Mercedes palette.
- **Persistence.** Window geometry, always-on-top, and last-selected system saved
  to `superwarp_gui.json` next to the exe.

### Known / next

- Built against eframe 0.31 mirroring the Fisher GUI's API usage; compile locally
  with `cargo build --release`.
- Planned: richer unlock checklist view, cross-system search, keyboard nav,
  favourites/shortcuts surfacing, and visual polish.
