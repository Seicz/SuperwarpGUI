# superwarp (GUI-bridged fork)

A fork of Akaden's **superwarp** -- the text-based teleport-assistance addon for
Final Fantasy XI on Windower 4 -- extended with an external-GUI bridge. The addon
keeps all of its original `//sw` command-line behavior; the fork adds a
non-blocking TCP/JSON server so a standalone desktop application can browse
destinations, track unlock progress, and issue warps without interfering with the
game's input pipeline.

This is a two-part system, both shipped:

- **the addon** (this folder) -- all warp logic, plus the bridge, the static
  destination catalogue, and live unlock/currency capture; and
- **[superwarp-gui](../superwarp-gui)** -- a Rust + egui desktop window that
  connects to the bridge and gives you a searchable, click-to-warp launcher with a
  per-system unlock tracker.

## Lineage

| | |
|---|---|
| Upstream | superwarp **v1.1.1+** by **Akaden** (this fork re-based onto it at v1.4.0; originally forked from v1.0.3) |
| This fork | **v1.6.0** by **Eric Strawser (Seicz@Bahamut)** with AI assistance |

Upstream is authoritative for all warp mechanics. The fork adds bug fixes and the
GUI bridge; it changes no teleport packet logic beyond the two `missing()`
nil-concat crash fixes noted in the changelog.

## Why an external GUI?

Windower's mouse-event pipeline cannot reliably stop clicks from reaching the FFXI
client, so any interactive in-game overlay causes character movement when clicked --
a limitation of the DirectX hook architecture. An external OS window has its own
input pipeline: clicking it has zero effect on the game. The `//sw` CLI remains
fully functional for anyone who prefers it; the GUI is an alternative front end, not
a replacement.

## What the GUI does

1. **Launcher** -- browse the full destination space across all 17 warp systems as
   a searchable, reorderable list and click to warp. Search is the addon's own
   fuzzy matcher (exact -> subsequence -> closest), so `altcav` finds
   *Abyssea - Altepa - Cavernous Maw*. A single/party/all toggle can send the whole
   crew at once. Sub-commands (`enter`, `exit`, `set`, ...) are one-click chips.

2. **Unlock tracker** -- per system, what's unlocked versus still missing, plus the
   last-seen currency balance for affordability -- using the same bitfield logic the
   `missing` handlers already compute.

### The two-tier data problem

superwarp has two kinds of data:

- **Static** -- the `warpdata` tables (destination names, indices, zones). Always
  available; exported to the GUI on connect.
- **Live** -- unlock bitfields and currency (gil, cruor, silt, accolades). These
  exist **only in the NPC menu packet**, readable only while standing at the
  relevant NPC with its menu open.

So the unlock view refreshes when you visit an NPC. The design is **cache + persist
last-seen unlock state per system**: visit a Survival Guide once and the unlocked
set is remembered (dated) until your next visit. Most captures happen **passively**
-- any menu a normal warp opens is read -- so an explicit scan is rarely needed.

A warp button is **not** teleport-from-anywhere. To execute, the addon still pokes
the correct NPC and waits for its menu, exactly as the CLI does. The GUI is a
command composer and state viewer over the existing `handle_warp` path.

## Architecture

```
+--------------------------+        TCP/JSON         +--------------------------+
|   FFXI (Windower 4)      |   localhost:19519       |   superwarp-gui          |
|                          |<----------------------->|                          |
|   superwarp.lua  v1.6.0  |   state / lists /       |   Rust + egui            |
|     All warp logic       |   unlocks  -->          |   Own window / own mouse |
|     map/*.lua modules    |   <--  commands         |   Searchable launcher    |
|     comm.lua (LuaSocket) |                         |   Unlock tracker         |
+--------------------------+                         +--------------------------+
```

`comm.lua` is a self-contained transport module -- it knows nothing about warps.
The host addon supplies an `on_command` callback (invoked with each decoded inbound
table) and an `on_connect` callback (invoked with each new client so the host can
push an initial snapshot). The server binds to `127.0.0.1:19519`, is polled once per
frame from `prerender`, accepts up to 4 clients, and guards against oversized lines.

## Warp systems

17 systems, each reachable by short name from `//sw` or via the GUI. Sub-commands
in the right column:

| System | Short | Sub-commands |
|---|---|---|
| Homepoint | `hp` / `ho` | `set` |
| Waypoint | `wp` / `wa` | (sub-zone shortcuts: AH, mog, frontier) |
| Proto-Waypoint | `pwp` / `pw` | |
| Survival Guide | `sg` / `sur` | |
| Escha | `ew` / `ea` | `enter`, `exit`, `domain` |
| Unity | `un` / `uy` | |
| Voidwatch | `vw` / `vo` | |
| Runic Portal | `po` / `ps` | `return`, `assault` |
| Abyssea | `ab` / `aa` | `enter`, `exit` |
| Spatial Displacement | `spd` | `enter` |
| Sortie | `so` / `se` | `port`, `normal`, `hard`, `repop` |
| Odyssey | `od` / `ody` | `port`, `exit` |
| Limbus | `li` / `te` / `ap` | `port`, `back`, `next`, `random`, `enter`, `exit` |
| Campaign | `ca` / `cn` | `return`, `port` |
| Mog Garden | `mg` | `zone` |
| Incursion | `in` / `inc` | |
| Walk of Echoes | `we` / `woe` | `enter`, `exit`, `medal`, `zone` |

`//sw` with no arguments detects a nearby warp NPC and does the obvious thing.
Control commands: `cancel` / `reset` (optionally `all` / `party`), `missing [max]`,
`chest` / `sync` (Limbus), `hpdefaults`, `display`, `understood`, `help`. Fork
additions: `//sw <system> scan` and the GUI bridge. See the in-game `//sw help` for
the authoritative list.

## Protocol

Newline-delimited JSON over TCP. Each message is one JSON object followed by a `\n`.
The inbound decoder is flat (top-level object only); arrays of objects (the
destination catalogue) flow addon-to-GUI only, where the GUI's JSON library handles
nesting natively.

### GUI to addon (commands)

`dispatch` is `"all"` | `"party"` | absent (just you).

| Command | JSON |
|---|---|
| Warp (structured) | `{"cmd":"warp_to","system":"homepoints","zone":"Southern San d'Oria","sub":"1"}` |
| Warp (party/all) | `{"cmd":"warp_to","system":"homepoints","zone":"Jeuno","dispatch":"party"}` |
| Warp (CLI string) | `{"cmd":"warp","line":"hp Bastok Markets 2"}` |
| Sub-command | `{"cmd":"sub_cmd","system":"homepoints","sub":"set"}` |
| Scan unlocks | `{"cmd":"scan","system":"survivalguides"}` |
| Cancel / reset | `{"cmd":"cancel","dispatch":"all"}` |
| Toggle addon debug | `{"cmd":"debug","on":true}` (omit `on` to toggle) |
| Refresh | `{"cmd":"get_state"}` / `{"cmd":"get_lists"}` / `{"cmd":"get_unlocks"}` |

`warp_to` is the launcher's primary path. `system` is the map-table key (the `key`
field from each system in `lists`), `zone` is the exact zone-name key, and `sub` is
the exact sub-key (omit for flat systems). For a single character it calls `do_warp`
directly, bypassing the CLI's single-token sub-zone limitation -- so multi-word
destinations like waypoints' `Frontier Station` are reachable. For `party` / `all`,
and for `sub_cmd`, it routes through the proven `received_warp_command` ->
`handle_warp` path (resolving the system's short-name first), which fires the addon's
`send_all` broadcast so the crew warps too.

`scan` actively refreshes unlocks: it pokes the named system's NPC purely to read
its menu, captures the state, and closes the menu without warping. It honours the
busy lock and is rejected for systems with no unlock scheme. Most of the time no
scan is needed -- unlock state is captured passively from any menu a warp opens.

### Addon to GUI (events)

| Event | JSON |
|---|---|
| State snapshot | `{"t":"state","version":"1.6.0","character":"Seicz","zone":"Western Adoulin","zone_id":256,"busy":false,"logged_in":true,"debug":false}` |
| Destination catalogue | `{"t":"lists","version":"1.5.1","systems":[ ... ]}` |
| Log message | `{"t":"msg","text":"...","level":"info"}` |
| Unlock delta (one system) | `{"t":"unlocks","system":"abyssea","updated":1737000000,"partial":true,"destinations":[ ... ]}` |
| Unlock snapshot (all) | `{"t":"unlocks_all","systems":[ ... ]}` |

On connect the addon sends one `state`, one `lists`, and one `unlocks_all`. `state`
is re-broadcast on zone change and on reset/cancel. The catalogue is static (built
once from `warpdata`, then cached) and re-sent on `get_lists`. All `log()` output is
mirrored to clients as `msg` events, with an early-out when no client is connected
so CLI-only sessions pay no cost.

#### The `lists` catalogue

`systems` is sorted by key. Each system:

```
{ "key":"homepoints", "short":"hp", "aliases":["hp","ho","home"],
  "long":"homepoint", "plural":"homepoints",
  "subcommands":["set"], "count":121, "destinations":[ ... ] }
```

Each destination carries only the fields present on its `warpdata` leaf:

```
{ "zone":"Southern San d'Oria", "sub":"1", "label":"Entrance",
  "aliases":["Entrance"], "zone_id":230, "index":0, "expac":0 }
```

`label` is the best display name (a leaf `display_name`, else shortcut aliases, else
the sub-key); `sub` is present only for nested systems. Shortcut entries are folded
into `aliases`, never emitted as separate destinations. The export covers ~690
destinations across all 17 systems; every `(key, zone, sub)` triple resolves to a
real `warpdata` leaf.

#### Unlock + currency events

Unlock state is decoded from the NPC menu packet's `Menu Parameters` bitfield, only
readable while a menu is open. Each map module already decodes that field in its
`missing()` function (the decode differs per system -- `has_bit` on an offset, on an
index, an inverted `invoffset`, or voidwatch's 4-byte bitmask), so capture
**delegates all bit decoding to `missing()`** and only adds scope: whether one menu
reveals the whole system (`global`) or just the current zone's group (`zone`).
Zone-scoped systems (abyssea, escha) accumulate region-by-region across visits and
are flagged `"partial":true`.

```
{ "system":"abyssea", "updated":1737000000, "partial":true,
  "currency":{"name":"cruor","value":1240},
  "destinations":[ {"zone":"Abyssea - La Theine","sub":"3","unlocked":false}, ... ] }
```

`(zone, sub)` match the `lists` catalogue exactly, so the GUI joins them into a
per-destination locked/unlocked checklist. The optional `currency` object is the
last-seen balance the menu revealed, so the GUI can tint unaffordable destinations.
It's present only for systems that surface a balance -- gil (homepoints,
survivalguides), cruor (abyssea, voidwatch), silt (escha), accolades (unity) -- and
is sticky: a capture that doesn't re-read the balance keeps the prior value. The
cache persists to `data/unlocks.lua` (a Lua-source dump, so the nested structure
round-trips through `loadstring`) and is restored on load.

## Installation

1. Extract this folder into `Windower4/addons/superwarp/`.
2. Load or reload in-game: `//lua r superwarp`. You should see:
   `[superwarp] GUI server listening on 127.0.0.1:19519`
3. Launch **superwarp-gui** (see its README to build). It auto-connects and
   auto-reconnects, so launch order relative to the game doesn't matter. The `//sw`
   command line works with or without the GUI running.

## File structure

```
superwarp/
+- superwarp.lua        Orchestrator + GUI bridge wiring (only file in root)
+- libs/                All addon modules (added to package.path at load)
|  +- comm.lua             TCP/JSON transport module (fork)
|  +- destinations.lua     warpdata -> catalogue normalizer (fork)
|  +- unlocks.lua          menu-packet unlock + currency decoder (fork)
|  +- unlockcache.lua      per-system unlock/currency cache + serializer (fork)
|  +- sendall.lua          IPC send-all + confirm/readout (upstream 1.1.1+)
|  +- fuzzyfind.lua        Fuzzy zone-name matcher (lean; Lili + Eric Strawser)
|  +- map/                 Per-system warp modules (upstream 1.1.1+, 2 crash fixes)
|     +- maps.lua + 17 system modules (escha.lua + abyssea.lua re-fixed in this fork)
+- data/
|  +- unlocks.lua          Cached unlock/currency state (created at runtime)
+- README.md  CHANGELOG.md  TRANSCRIPT.md
```

## Credits

| Component | Author |
|---|---|
| superwarp (original) | Akaden |
| Fork: crash fixes + GUI bridge + scan/unlock capture | Eric Strawser (Seicz@Bahamut) with AI assistance |
| Fuzzy matcher (fuzzyfind) | research + assembly by Lili; gap-aware scorer + lean build by Eric Strawser |
| send-all (IPC) | upstream superwarp (Akaden) |

## License

Inherits the license of Akaden's upstream superwarp.
