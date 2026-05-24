# superwarp GUI

An external desktop GUI for the [superwarp](../superwarp) FFXI addon fork, built
in Rust with [egui](https://github.com/emilk/egui)/eframe. It connects to the
addon's TCP bridge on `127.0.0.1:19519` and lets you browse every warp system,
see what you've unlocked, fuzzy-search across all destinations, and warp (or send
your party/alliance) with a click -- no `//sw` typing required.

The window auto-connects and auto-reconnects (background thread, mpsc hand-off,
non-blocking line-delimited JSON, ~3s retry), so launch order relative to the
game doesn't matter.

## Build

Requires a stable Rust toolchain. From this folder:

```
cargo build --release
```

The binary lands at `target/release/superwarp-gui.exe` (size-optimized: `opt-level="z"`,
LTO, stripped, `panic="abort"`).

On Windows the build also embeds the app icon and file metadata (Properties ->
Details) via `build.rs` + the `winresource` crate, reading `assets/icon.ico`. This
needs a resource compiler on PATH (`rc.exe` from the MSVC toolchain, or `llvm-rc` /
`windres`); if none is found the build still succeeds and just skips the resource
with a cargo warning. The step is a no-op on non-Windows targets.

## Layout

**Header** -- the superwarp banner, addon version, logged-in character and current
zone (with id), connection status, a BUSY indicator with a cancel button, and an
always-on-top `pin` toggle.

**Systems** (left) -- every warp system the addon exposes. Tracked systems show an
`unlocked/known` count once unlock data is captured (green when complete); others
show their destination count.

- **Drag to reorder.** Grab any system and drag it; the list animates apart to open
  a slot, the dragged tile lifts and follows the cursor, and it settles into place
  on release. The order persists (see Persistence). Systems the addon adds later --
  or ones you scan into for the first time -- append at the bottom rather than
  disappearing.
- **Click to select** and browse that system's destinations on the right.

**Destinations** (centre) -- for the selected system (or, while searching, across
every system), each row has:

- a state dot -- unlocked (green), locked (rose), or unknown / not yet captured;
- the destination label (click to warp), with its zone and any sub-index;
- its cost, tinted rose if the system's known currency can't cover it;
- a system tag, shown only while searching.

**Actions** -- a row of chips under the search bar for the selected system's
sub-commands (`enter` / `exit` / `port` / `set` / ...) plus `cancel / reset`. These
respect the dispatch toggle, so e.g. `enter` can be fired for the whole party.

**Scan** -- pokes the system's NPC purely to read its menu and refresh unlock state
and currency, without warping. Equivalent to `//sw <sys> scan`.

## Fuzzy search

The search box matches across every system using the same matcher as the addon's
in-game `fuzzyfind` (ported to Rust and verified identical to the Lua scorer):

- **Exact -> subsequence -> approx.** Type an abbreviation and it finds the target:
  `altcav` -> *Abyssea - Altepa - Cavernous Maw*, `atto00` -> *Attohwa - 00*.
  Matching is a gap-aware, fzf-style score (word-boundary, camelCase and consecutive
  bonuses, gap penalties) over each destination's label, zone, and aliases.
- **Ranked best-first** while searching. (Browsing a system with an empty search
  keeps the natural zone / numeric-sub ordering.)
- **Never a dead end.** If your query isn't a subsequence of anything (a typo), the
  closest matches by edit distance are shown under a `no direct match -- closest:`
  note instead of an empty result.

## Dispatch toggle (single / party / all)

A toggle in the destination-panel header cycles **Off -> Party -> All**:

- **Off** -- warps just you.
- **Party / All** -- routes the warp (or sub-command, or cancel) through the addon's
  `send_all` so your party or full alliance goes too.

It defaults to **Off** on every launch, so a click never unexpectedly drags the crew
along.

## Protocol

Commands sent to the addon (line-delimited JSON). `dispatch` is `"all"` | `"party"`
| absent (just you):

| Action | Message |
| --- | --- |
| Warp | `{"cmd":"warp_to","system":"survivalguides","zone":"Bastok Mines"}` |
| Warp (nested) | `{"cmd":"warp_to","system":"abyssea","zone":"Abyssea - Konschtat","sub":"3"}` |
| Warp (party/all) | `{"cmd":"warp_to","system":"homepoints","zone":"Jeuno","dispatch":"party"}` |
| Sub-command | `{"cmd":"sub_cmd","system":"homepoints","sub":"set"}` |
| Scan unlocks | `{"cmd":"scan","system":"abyssea"}` |
| Cancel | `{"cmd":"cancel","dispatch":"all"}` |
| Refresh | `{"cmd":"get_state"}` / `{"cmd":"get_lists"}` / `{"cmd":"get_unlocks"}` |

Events consumed: `state`, `lists` (systems + their destinations and sub-commands),
`unlocks`, `unlocks_all`, `msg`. On connect the addon sends `state`, `lists`, and
`unlocks_all`. See the addon's README for the full protocol.

Unlock and currency data populate as menus open (passively) or when you Scan; the
addon caches and persists them, so they survive reloads.

## Advanced

An `advanced` toggle in the header switches the main view to a settings page:

- **Connection** -- change the host and port the GUI connects to (default
  `127.0.0.1:19519`) and `apply + reconnect`, or restore `defaults`. Useful if you
  run the addon's bridge on a non-default port. Selecting a system (or the `warp`
  toggle) returns to the launcher.
- **Diagnostics** -- an `addon debug` switch that toggles the addon's own debug mode
  (`//sw debug`) over the bridge; the addon's verbose output streams into an in-app
  log console (with a `clear` button). Handy for diagnosing warps or the connection.
- **About** -- GUI and connected addon versions.

## Persistence

Saved to `superwarp_gui.json` next to the executable: window position and size, the
always-on-top pin, the last-selected system, your custom system order, the connection
host/port, and the debug toggle.

## Credits

GUI by Eric Strawser (Seicz@Bahamut) - ITIWH.com, with AI assistance. The superwarp
addon is a fork of Akaden's superwarp. The fuzzy matcher descends from fuzzyfind
(research and assembly by Lili; gap-aware scorer and lean build by Eric Strawser).
