# Changelog — superwarp GUI

Semantic versioning. This GUI versions independently of the addon (which is at
v1.3.1). Development began at Stage 4 of the superwarp fork.

---

## [0.5.0] -- drag-to-reorder systems sidebar

- The SYSTEMS list is now **freely reorderable by drag-and-drop**. Grab any row,
  drag it, and an insertion line snaps to the target slot; release to drop. Click
  still selects (a clean click vs a drag is disambiguated by movement).
- The order **persists** in `superwarp_gui.json` (`system_order`). New systems --
  ones Akaden adds upstream, or a system you scan into for the first time --
  append at the bottom rather than vanishing, and systems no longer present are
  dropped from the saved order automatically.
- The dragged row highlights (azure) and the cursor switches to grab/grabbing.

No protocol change; works with addon v1.5.0+.

## [0.4.0] -- dispatch toggle + sub-command submenu

- **Send toggle** in the warp-panel header, cycling **Off -> Party -> All**
  (muted / azure / coral). Controls whether destination warps, sub-commands, and
  cancel go to just this character, the party, or every connected character.
  Defaults to Off on every launch (safety).
- **ACTIONS submenu** under the search bar: the selected system's sub-commands
  (enter / exit / port / return / next / set / ...) as clickable chips, plus a
  cancel / reset chip. Hidden during a global search. Each chip honors the Send
  toggle (e.g. fire "enter" for the whole party).
- Destination clicks and the footer cancel now honor the Send toggle too.
- Protocol: `warp_to` / `cancel` gained an optional `dispatch` field; added
  `sub_cmd`. `SystemList` parses the catalogue's `subcommands`. Requires addon
  v1.5.0+.

## [0.3.2] — fix header collision

- The destination header crammed title + `[short]` + "scanned ago" + currency +
  scan onto one row; a wide balance (nine-figure gil) right-aligned and overflowed
  leftward into the freshness text, overlapping it. Split into two rows: title and
  the scan button on top, currency (left) and "scanned N ago" (right) on a metadata
  line below. No collision regardless of balance width.

---

## [0.3.1] — fix header glyph

- The header mark was `✦` (U+2726), which egui's default font doesn't include, so
  it rendered as a tofu box. Swapped for `»` (Latin-1, always present), still azure.

---

## [0.3.0] — global search

- **Search is now global.** Typing in the search box matches destinations across
  every system, not just the selected one. Each result is tagged at the end with
  the system it belongs to (e.g. a violet `[homepoint]`), so you can tell where a
  match lives. Clearing the search returns to browsing the selected system, with
  no system tag (you already know which system you're in).
- Clicking a search result warps via that result's own system, and its unlock dot
  and affordability reflect that system's state.
- Results sort by zone, then point, then system, so same-named zones from
  different systems sit together. Empty results show "No matches."

---

## [0.2.0] — Stage 4b: readable destination rows

- **"Zone - point" labels.** Nested destinations (homepoints, waypoints, abyssea
  confluxes, escha portals) previously rendered as bare sub-keys ("1", "3", ...)
  with the zone lost. Each nested row now reads "Zone - point"; flat entries are
  unchanged.
- **Sorted list.** Destinations sort by zone, then by sub read as a number when it
  is one (so "1, 2, ..., 10" order rather than lexical) and by text otherwise — so
  a zone's points cluster and number in order instead of scattering through the
  addon's hash order.
- **Folded in the three deserialized-but-unused fields** (clearing the dead-code
  warnings): system `count` shows as "{n} destinations" in the sidebar before any
  unlock data is captured; `short` shows as a violet "[hp]" tag in the header;
  `updated` drives a "scanned 4m ago" age next to the system name.
- **Partial signal.** Zone-gated systems whose coverage is still accumulating
  (abyssea/escha) show "~partial" after their sidebar unlock count.

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
