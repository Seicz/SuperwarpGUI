# superwarp GUI

An external desktop GUI for the [superwarp](../superwarp) FFXI addon fork — built
in Rust with [egui](https://github.com/emilk/egui)/eframe. It connects to the
addon's TCP bridge on `127.0.0.1:19519` and lets you browse every warp
destination, see what you've unlocked, check currency, and warp with a click —
without typing `//sw` commands.

Built on the same bridge pattern as the Fisher GUI (background connect thread,
mpsc hand-off, non-blocking line-delimited JSON, 3-second auto-reconnect).

> **Status: v0.1.0 — Stage 4a foundation.** Bridge, protocol, system browser,
> destination list, search, click-to-warp, scan, currency, and unlock indicators
> are in. Visual polish and a few conveniences are still to come.

## Build

Requires a Rust toolchain (stable). From this folder:

```
cargo build --release
```

The binary lands in `target/release/superwarp-gui.exe`. The release profile is
size-optimized (`opt-level="z"`, LTO, stripped). Run it any time the game is up
and the addon is loaded — it auto-connects and auto-reconnects, so launch order
doesn't matter.

## What it shows

- **Header** — addon version, logged-in character, current zone (and id),
  connection status, a BUSY indicator with a cancel button, and an always-on-top
  pin.
- **Systems** (left) — every warp system the addon exposes, each with an
  `unlocked/known` count once unlock data has been captured.
- **Destinations** (centre) — the selected system's destinations, each with:
  - a state dot — ● unlocked (green), ○ locked (rose), · unknown (not yet
    captured);
  - the destination label (click it to warp);
  - its cost, tinted rose if the system's known currency can't cover it.
- **Scan** — pokes the system's NPC purely to read its menu and refresh unlock
  state (and currency), without warping. Equivalent to `//sw <sys> scan`.
- **Search** — filters the current system's destinations by label, zone, or alias.
- **Currency** — the last-seen balance the menu revealed (cruor, silt, gil,
  accolades), shown next to Scan.

Unlock and currency data populate as you open menus (passively) or hit Scan. The
addon caches and persists them, so they survive reloads.

## How it talks to the addon

Commands sent (line-delimited JSON):

| Action | Message |
| --- | --- |
| Warp | `{"cmd":"warp_to","system":"survivalguides","zone":"Bastok Mines"}` |
| Warp (nested) | `{"cmd":"warp_to","system":"abyssea","zone":"Abyssea - Konschtat","sub":"3"}` |
| Scan unlocks | `{"cmd":"scan","system":"abyssea"}` |
| Cancel | `{"cmd":"cancel"}` |
| Refresh | `{"cmd":"get_state"}` / `{"cmd":"get_lists"}` / `{"cmd":"get_unlocks"}` |

Events consumed: `state`, `lists`, `unlocks`, `unlocks_all`, `msg`. See the
addon's README for the full protocol.

## Persistence

Window position/size, the always-on-top pin, and the last-selected system are
saved to `superwarp_gui.json` next to the executable.

## Credits

GUI by Eric Strawser (Seicz@Bahamut), with Claude. Bridge pattern adapted from the
Fisher GUI. The superwarp addon is a fork of Akaden's superwarp.
