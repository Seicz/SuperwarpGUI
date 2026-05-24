# superwarp GUI — development transcript

Session-by-session record of the GUI build, companion to the superwarp addon
fork. Begins at Stage 4 of that effort, once the addon's bridge (Stages 1–3:
transport, static catalogue, live unlock/currency capture; addon v1.3.1) was live-
confirmed.

---

## Session — Stage 4a: foundation (GUI v0.1.0)

**Context.** Addon v1.3.1 confirmed in-game: `lists` catalogue (675 destinations,
13 systems), `unlocks_all` with per-destination state, and `currency` (cruor
616,214 read from the Konschtat conflux). With the protocol proven end to end, the
GUI build began.

**Reference.** The user's Fisher GUI (`main.rs`, `Cargo.toml`) was the authoritative
pattern: eframe 0.31, `serde`/`serde_json`, size-optimized release profile, a
background connect thread handing a `TcpStream` back over mpsc, non-blocking
line-delimited JSON read in the update loop, 3-second auto-reconnect, JSON config
persisted next to the exe, `#[serde(tag="t")]` message enum, ViewportBuilder
geometry restore. The GUI mirrors these idioms closely to stay on proven 0.31 API.

**Built.**
- `Cargo.toml` — `superwarp-gui` 0.1.0, eframe 0.31 + serde + serde_json, same
  size-optimized release profile as Fisher.
- `src/main.rs` — bridge (try_connect / check_pending_connect / send / poll), the
  protocol types (`state`, `lists`, `unlocks`, `unlocks_all`, `msg`; commands
  `warp_to`/`scan`/`cancel`/`get_*`), an unlock cache keyed by `destkey(zone,sub)`
  matching the addon's record separator, and the UI: system browser with
  `unlocked/known` counts, destination list with state dots + click-to-warp +
  affordability-tinted cost, Scan button, per-system currency, label/zone/alias
  search, header (character/zone/connection/BUSY+cancel/pin), footer log.
- Outgoing commands built via `serde_json::json!` so apostrophe zone names escape.
- **Aether theme** — deep indigo / azure / violet, distinct from Fisher's Mercedes
  palette.
- Persistence of window geometry, always-on-top, and last-selected system.
- `README.md`, `CHANGELOG.md`.

**Not compiled here** (no Rust toolchain in the work environment) — written against
the Fisher GUI's exact 0.31 API usage; brace/paren/bracket balance verified. User
builds locally with `cargo build --release`.

**Shipped:** `superwarp-gui-v0.1.0.zip`. Next: live compile + react (theme/layout
are the user's call given the high visual bar), then Stage 4b polish — richer
unlock checklist, cross-system search, conveniences.

---

## Session — Stage 4b: readable rows (GUI v0.2.0)

**Live result of 0.1.0:** ran an hour, connected, sidebar populated, state dots
correct (lone locked Toraimarai Canal among unlocked homepoints), warps landing,
currency refreshing on scan/warp (gil 298,678,681). Compiled clean bar three
dead-code warnings (`short`, `count`, `updated` deserialized but unused).

**Issue spotted in the render:** nested entries showed as bare sub-keys ("1",
"3", ...) with zone context lost and scattered through hash order. User chose flat
rows labeled "Zone - point".

**Built (0.2.0):**
- Row label = `"{zone} - {point}"` for nested, plain label for flat.
- Sort by (zone, numeric-sub-then-text) so a zone's points cluster and number in
  order.
- Folded the three flagged fields into display (clearing the warnings): `count` ->
  "{n} destinations" in sidebar pre-capture; `short` -> violet "[hp]" header tag;
  `updated` -> "scanned Nm ago"; plus a "~partial" suffix on accumulating gated
  systems' counts.

Shipped `superwarp-gui-v0.2.0.zip`. Next: continued polish per user reaction.

---

## Session — Stage 4c: global search (GUI v0.3.0)

**Request:** search should span all systems, each result tagged with its system at
the end; no tag when browsing a single system (search empty).

**Built (0.3.0):** render_destinations now builds its row set two ways -- selected
system only when the search is empty (no tag), or every matching destination across
all systems when searching (each row tagged `[<system long>]` in violet on the
right). Per-row unlock dot, affordability, and warp target all key off the row's own
system. Rows sort by zone -> sub (numeric-aware) -> system so same-named zones
cluster across systems. Search hint changed to "search all systems...". Empty-result
state shows "No matches."

Shipped `superwarp-gui-v0.3.0.zip`.

---

## Session — Stage 4 fix: header collision (GUI v0.3.2)

After the addon libs/ reorg (v1.3.3) loaded clean, a screenshot showed the
destination header overlapping: right-aligned currency (nine-figure gil) overflowed
left into the left-flow "scanned ago" text. Root cause: too much on one row. Fixed
by splitting the header -- title + `[short]` + scan on row 1; currency (left) +
"scanned N ago" (right) on a metadata row 2. Robust to any balance width.
Shipped `superwarp-gui-v0.3.2.zip`.

---

## Session (GUI v0.4.0) -- dispatch toggle + sub-command submenu

Added a Dispatch enum (Off/Party/All) with a cycling toggle button in the warp
panel header, an ACTIONS submenu of sub-command chips + cancel/reset below the
search, and a `subcommands` field on SystemList. warp_to/sub_cmd/cancel attach
the dispatch token when not Off. Pairs with addon v1.5.0. Defaults to Off each
launch for safety.

---

## Session (GUI v0.5.0) -- drag-to-reorder systems sidebar

**Eric:** Considered a frequency-weighted auto-sort, then realized manual drag-and-
drop snap-to makes more sense; wants the full list freely reorderable.

**Claude:** Reinforced why manual beats auto (a launcher used by muscle memory must
not reorder under you; frequency != priority). Implemented full-list drag-to-reorder
in the sidebar: each row is a click_and_drag interact inside a Frame (selection bg
without paint-order tricks); click selects, drag shows a snap-to insertion line and
on release splices the row into the new slot. Order persists in superwarp_gui.json
as `system_order`; effective_order() drops stale keys and appends any not-yet-ordered
systems at the bottom so new/scanned systems never vanish. GUI-only; v0.5.0.

---

## Session (GUI v0.6.0) -- animated drag-to-reorder

**Eric:** Wants a physical indication of lifting the tab and moving it into place,
watching snap animations -- and chose the full version (rows physically slide apart)
over the lighter lift-and-follow-only option.

**Claude:** egui has no built-in FLIP/list-reorder transition, so hand-rolled it:
the sidebar now positions rows at animated y-offsets within a fixed-height slot grid,
painting rows directly (painter text + bg) for full positional control. Dragged row
lifts onto a foreground layer with a shadow and follows the cursor; other rows ease
toward their slots (gap opens at the insertion point) via animate_value_with_time;
on release the tab eases from cursor into its slot. Offsets are relative to the list
top so scrolling doesn't retrigger animation. Persists as before; new systems append.
Easing is one tunable constant. Flagged: passes brace/symbol checks but not a real
cargo build, and the motion will want local visual tuning. GUI-only; v0.6.0.

---

## Session (GUI v0.6.1) -- smaller minimum window width

**Eric:** [screenshot] egui won't let me shrink the window horizontally any further;
want to shrink it more to remove the free space on the right / reclaim screen space.

**Claude:** Root cause was `with_min_inner_size([520, 420])` -- eframe passes that to
the OS as a hard window-size floor. Lowered to [300, 360], and added a sidebar
width_range(120..=360) (default still 190) so a narrow window can give the freed
space to the destination list rather than the sidebar holding it. v0.6.1.

---

## Session (GUI v0.7.0) -- app icon + authorship metadata

**Eric:** Give the exe authorship context and an icon (the blue chevron) instead of
the default giant "E".

**Claude:** Generated a chevron icon matching the GUI palette (deep-indigo rounded
square, violet back + azure front double-chevron) via Pillow -> assets/icon.png
(256) and assets/icon.ico (multi-size 16-256). Two-part wiring: (1) build.rs using
the winresource crate embeds the .ico + VERSIONINFO (ProductName, FileDescription,
CompanyName "Eric Strawser (Seicz@Bahamut) - ITIWH.com", LegalCopyright; version
auto from CARGO_PKG_*) -> Explorer icon + Properties/Details; Windows-only, soft-fails
to a warning if no resource compiler. (2) runtime with_icon via
eframe::icon_data::from_png_bytes(include_bytes!) -> title-bar/taskbar icon, guarded.
v0.7.0. Flagged: can't cargo-build here; winresource + from_png_bytes are the items
to confirm on Eric's machine.

---

## Session (GUI v0.7.1) -- multicolor banner chevron + sharper icon

**Eric:** [screenshot] Prefers the banner chevron's angle over the icon's "weak"
(blunt) angle; likes the color scheme; wants the banner chevron multicolor like the icon.

**Claude:** The banner "»" was a single azure text glyph (can't be two-toned), so
replaced it with painted geometry: a violet-back + azure-front double-chevron drawn
via line_segment + round caps, sharp ~60deg. Also regenerated the icon with a matching
sharp angle (was ~120deg/blunt; dx/half ratio raised). Both now share the look. v0.7.1.

---

## Session (GUI v0.8.0) -- fuzzy search

**Eric:** Apply the same fuzzy logic to the GUI search that the in-game addon has.

**Claude:** Ported fuzzyfind (lean) to Rust: the gap-aware subsequence scorer
(fuzzy_score), Wagner-Fischer Levenshtein (fuzzy_lev), clean(), and the exact/
fuzzy/approx tiers as dest_fuzzy + dest_approx over each dest's label/zone/aliases.
Adapted from "pick one winner" to "filter + rank a list": search results now rank
best-first by score; if nothing is a subsequence, the closest-by-edit-distance are
shown under a "closest:" note rather than an empty result. Browse mode keeps the
alphabetical/numeric ordering. Verified the port byte-identical to the addon's Lua
scorer via a Python mirror diffed against lua5.1 across subsequence/nil/Levenshtein
cases. GUI-only; v0.8.0.

---

## Session (GUI v0.9.0) -- advanced settings view

**Eric:** GUI needs an advanced tab section for misc settings -- e.g. debug, and
changing the connection port.

**Claude:** Added a View enum (Warp/Advanced) toggled from the header; the central
panel renders render_advanced in Advanced mode, and selecting a system returns to
Warp. Config gained host/port/debug (manual Default so port defaults to 19519, not
0; serde defaults for old config files). try_connect now builds host:port and
resolves via ToSocketAddrs (localhost works); a reconnect() helper tears down the
link so poll_network redials immediately. Advanced view: Connection (host/port +
apply+reconnect + defaults + live status), Diagnostics (debug toggle -> raw protocol
traffic logged -> / <- to an in-app console + clear), About (versions). debug logging
wired into send() and poll_network() (collected raw, logged after the conn borrow).
GUI-only; v0.9.0.

---

## Session (GUI v0.9.1) -- debug is the addon's, corrected

**Eric:** debug is an option in the superwarp addon, not the GUI.

**Claude:** Corrected. Removed config.debug and the GUI raw-protocol logging.
Added addon_debug (from the state event's new debug field); the Advanced checkbox
reflects it and, on toggle, sends {"cmd":"debug","on":bool} to the addon (disabled
until connected). The addon's verbose output arrives as msg events and shows in the
log console. Pairs with addon v1.6.0.

---

## Session (GUI v0.9.2) -- clear search after a search-result warp

**Eric:** After queuing a warp from the search box, clear the search box after the
warp completes.

**Claude:** Warps are fire-and-forget, but the addon broadcasts state at completion
(line 648, current_activity cleared -> busy=false); no broadcast at warp start, so
the box naturally persists through the warp. Added clear_search_pending: set when a
result is clicked while in global search; cleared (clearing self.search) on the next
State with busy==false; and cancelled if the user edits the search before it lands
(so a fresh query isn't wiped). GUI-only; v0.9.2.
