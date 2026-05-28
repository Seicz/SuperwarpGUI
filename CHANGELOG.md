# Changelog

All notable changes to this fork of Akaden's superwarp. Versioning is
semantic. The fork began from upstream v1.0.3; at v1.4.0 it was re-based onto
upstream v1.1.1+ (see that entry). Everything at or above v1.0.4 is fork work
by Eric Strawser (Seicz@Bahamut) with Claude.

The GUI application versions separately (like Fisher's addon/GUI split). It is
unchanged at v0.3.2 through this re-base (verified compatible — see [1.4.0]).

---

## [1.6.0] -- expose addon debug over the bridge

- `state` events now include the addon's `debug` flag, so a client can reflect it.
- New remote command `{"cmd":"debug","on":true|false}` (omit `on` to toggle) mirrors
  `//sw debug`: sets `settings.debug`, persists, and re-broadcasts `state`. This lets
  the GUI's Advanced view drive the addon's debug mode; its verbose output continues
  to flow to clients as `msg` events.

---

## [1.5.1] -- sub-command / dispatch routing fix

GUI sub-command buttons (Homepoint "set", Mog Garden "zone", etc.) and all/party
warps did nothing. `received_warp_command` matches a system by SHORT-NAME (via
`handle_map_short_name` / `warp_list`), but the new `sub_cmd` and dispatch paths
were passing the system KEY (`c.system`, e.g. "homepoints"/"garden") straight
through, so nothing matched and the command silently no-op'd. Fixed by resolving
the map's short-name (`map.short_name[1]`) before calling `received_warp_command`.
(Single-character destination warps were unaffected -- they use `do_warp`, which
takes the key directly.) GUI unchanged at v0.4.0.

---

## [1.5.0] -- GUI dispatch + sub-command protocol

Added two protocol capabilities so the GUI can drive everything the chat commands
can, not just single-character destination warps:

- **Dispatch (all / party / single).** `warp_to`, the new `sub_cmd`, and `cancel`
  now accept an optional `dispatch` field (`"all"` | `"party"`; absent = single
  character). When set, the command is routed through `received_warp_command`
  (the proven chat entry), so `handle_warp` detects the all/party token and fires
  `send_all_with_confirm` to broadcast to the crew -- identical to typing
  `//sw hp Jeuno all`. Single-character destination warps still take the
  structured `do_warp` path that preserves multi-word sub-zones.
- **`sub_cmd{system, sub, dispatch?}`.** Fires a system sub-command (enter / exit /
  port / return / next / set / ...) via `received_warp_command`, optionally to
  all/party. Lets the GUI expose each system's sub-commands as buttons.
- **`cancel{dispatch?}`.** Cancel/reset for just you, or `reset_all` across
  party / all characters.

No change to the single-character warp/scan paths shipped in 1.4.x. Pairs with
GUI v0.4.0.

---

## [1.4.2] -- GUI warp NPC-mismatch fix (same root cause as 1.4.1)

GUI click-to-warp aborted with the same "This is not the NPC the warp was meant
for" message, on every warp. Same root cause as the 1.4.1 scan fix, wider scope:
`received_warp_command` (the CLI warp entry) is the only place that clears
`intended_npc` and resets `state.warp_confirmed` / `state.current_sub_cmd` /
`prev_location` before a warp -- state the new orchestrator's `poke_npc` and warp
state-machine depend on. The GUI `warp_to` path calls `do_warp` directly
(deliberately, to keep multi-word/case-sensitive sub-zones reachable), so it
bypassed that setup and inherited a stale `intended_npc`. Fixed by mirroring
`received_warp_command`'s full pre-warp setup inside the `warp_to` handler.

(1.4.1 fixed the same omission in `do_scan`; this completes it for the warp path.)

---

## [1.4.1] -- scan NPC-mismatch fix

`//sw <sys> scan` and the GUI Scan button aborted with "This is not the NPC the
warp was meant for. Shutting it down." Root cause: `poke_npc` records the poked
target into `current_activity.poked_npc_id` via the file-scope `intended_npc`,
and only when `intended_npc.id` is unset; a stale id left over from a prior warp
was copied instead of the NPC being scanned, so the menu packet's real NPC failed
the mismatch guard. The warp path clears `intended_npc = {}` in
`received_warp_command` before poking; `do_scan` did not. Fixed by clearing
`intended_npc` in `do_scan` immediately before `poke_npc`, mirroring the warp path.

---

## [1.4.0] -- re-base onto upstream superwarp v1.1.1+

Akaden released a large update (the orchestrator nearly doubled, 1017 -> 1826
lines; `sendall` was fully rewritten, 68 -> 355). Rather than cherry-pick, the
fork was re-based: upstream v1.1.1+ is the new base, and every fork feature was
re-applied on top of it.

Adopted from upstream:
- Four new warp systems: **Campaign** ([S] zones via Campaign Arbiters, with
  `return`/`port` sub-commands), **Mog Garden**, **Incursion**, and
  **Walk of Echoes**.
- Orchestrator rewrite: per-character settings (`data/settings_<name>.xml`),
  first-run "understood" gate, `hp_legacy_defaults`, Limbus chest tracking with
  IPC sync, the `//sw` smart-command and `magic_map`/`the_superwarp` dispatch,
  and `all_warp_zones` multi-zone validation.
- The `send_all_with_confirm` system (scan-based participant discovery + an
  on-screen "waiting on player X" readout) replacing the old `sendall`.
- Per-module fixes: `sortie`'s malformed global tables wrapped correctly with
  corrected offsets; `waypoints` auto-subzone selection + `local` bugfixes;
  "you are already at that location" guards; jittered menu delays (`delay=2` ->
  `1 + wiggle`); homepoints `default_by_keyword` auto-selection.
- Cosmetic: capitalized `long_name`s (incl. "veridical conflux" -> **Abyssea**
  and "eschan portal" -> **Escha**), trimmed `short_name` aliases, reworded
  `help_text`. These flow into the GUI sidebar automatically via the catalogue.

Fork work re-applied on top of the new base:
- `libs/` layout preserved; the one `package.path` load line re-added ahead of
  the bundled `sendall`/`fuzzyfind`/`maps` requires.
- GUI bridge (`comm`/`destinations`/`unlocks`/`unlockcache`) re-woven into the
  new orchestrator: `prerender` poll, `comm.init`/`on_connect` in `load`,
  `comm.shutdown` in `unload`, state broadcasts on zone-change / reset / cancel,
  `log()` mirrored to clients, passive `capture_unlocks` at the menu catch, the
  `do_scan` path (CLI `//sw <sys> scan` + GUI `scan`), and `handle_remote_command`.
- Lean `fuzzyfind` v1.0.0 retained (upstream never touched fuzzyfind -> zero
  conflict). Its tier logic (act on exact/fuzzy; route `approx` to a "Did you
  mean...?" suggestion) re-applied to the new `resolve_warp`, the sub-zone match,
  and `resolve_waypoint_subzone` (whose old numeric score gate no longer applies).
- **Carried-forward crash fix:** upstream's v1.1.1 `abyssea`/`escha` `missing()`
  still append `z..'-'..d` with `z` undefined (nil-concat crash on find-missing).
  Re-fixed by capturing the zone name (`zname`) per branch.

New systems are not yet in the unlock-tracking SCHEME, so `capture_unlocks` /
scan skip them gracefully (they appear in the GUI catalogue without unlock dots).
`destinations.build` was unit-tested against all four new warpdata shapes
(flat-without-zone, empty/sub-command-only) — no crash.

---

## [1.3.3] -- libs/ reorganization

Project layout: every addon module now lives under `libs/`, leaving only
`superwarp.lua` (plus the docs) in the addon root.

- Moved `comm`, `destinations`, `unlocks`, `unlockcache`, `fuzzyfind`, `sendall`,
  and the whole `map/` folder into `libs/` (so `libs/map/`).
- Added one line at load: `package.path = package.path .. ';' ..
  windower.addon_path .. 'libs/?.lua'`. Every `require()` string is unchanged --
  `require('comm')`, `require('map/maps')`, and maps.lua's own `require('map/...')`
  all resolve via the new path. Windower's shared libs (tables, logger, files, ...)
  still resolve via their own path; the libs/ entry is appended, so it's only
  consulted for names they don't provide.
- No behavioral change. README file-structure tree updated.

---

## [1.3.2] -- fuzzyfind lean v1.0.0

Swapped in the lean `fuzzyfind` (v1.0.0: gap-aware subsequence scorer + Levenshtein
fallback, two algorithms; LCS and the confidence floor removed). `fmatch` now
returns `(winner, score, tier)` where tier is `exact` / `fuzzy` / `approx`.

- **`resolve_warp` retuned to the tier, not the score.** The old gate
  (`score >= 3 and score >= #zone`) depended on the old score scale (LCS length /
  substring length) and is meaningless on the new scale, so it's gone. The zone and
  sub-zone matches now act on `exact`/`fuzzy` and treat `approx` (the input was a
  subsequence of nothing -- a likely typo) as a suggestion: "No close match for
  X. Did you mean Y?" rather than silently warping to an edit-distance guess.
- Expect intended behavioral wins: abbreviations resolve via camelCase initials
  (`bms` -> Bastok Markets [S]), bare prefixes resolve more reliably, and `[S]`/
  Adoulin-style keys win tiebreaks they used to lose.
- Lineage preserved in the module header: research+assembly by Lili; gap-aware
  scorer + lean build by Eric Strawser (Seicz@Bahamut).

---

## [1.3.1] — currency capture

Surfaces the currency balance each warp menu already reveals, so the GUI can show
affordability alongside the unlock checklist. No extra pokes — it rides on the same
passive/active captures as unlock state.

- **`unlocks.lua`.** `decode()` now reads the system's currency from the open menu
  and returns it as `currency = { name, value }` (or `nil`). The offsets are taken
  verbatim from each module's `build_warp_packets`, which reads the same menu to gate
  its own warps — so these are the addon's own retail-verified offsets, not new
  guesses: gil (homepoints `i@21`, survivalguides `I@9`), silt (escha `i@21`), cruor
  (abyssea `I@29`, voidwatch `i@17`), accolades (unity `i@9`). Reads are guarded —
  a negative or out-of-range result is discarded rather than cached. Abyssea's two
  cruor offsets are unambiguous here: `decode()` only succeeds at the conflux menu
  (`I@29`), never the entry NPC (`i@5`), because `missing()` resolves only inside an
  abyssea zone.
- **`unlockcache.lua`.** `merge()` stores last-seen `currency` per system (kept if a
  later capture doesn't surface one); `snapshot_system()` includes it; `serialize()`/
  `restore()` round-trip it.
- **Protocol.** `unlocks` / `unlocks_all` system snapshots gain an optional
  `currency` object. Gil systems include it for completeness, but it matters most for
  the consumable currencies (cruor/silt/accolades) that can actually run dry.
- Verified offline: decode reads currency for global and zone-gated systems, the
  `out_of_range` path stays clean, and currency survives the serialize/restore
  round-trip with `partial` preserved.

---

## [1.3.0] — Stage 3: live unlock-state capture

Decodes per-destination unlock state from caught NPC menus and exposes it to the
GUI bridge, passively by default with an active on-demand refresh.

- **`unlocks.lua` (new).** `decode(maps, system, zone_id, p)` reads the open menu's
  `Menu Parameters` bitfield into `{ system, out_of_range, partial, entries={ {zone,
  sub?, unlocked} } }`. The per-system bit decode varies (offset, index, inverted
  `invoffset`, voidwatch's 4-byte mask), so decode **delegates all bit reading to
  each module's `missing()`** — the authoritative, retail-verified decoder — and
  adds only scope: `global` (one menu reveals the whole system) vs `zone` (only the
  current region's group; abyssea/escha). The zone→group mapping is derived from the
  module's own `warpdata`, not hardcoded, so it can't drift.
- **`unlockcache.lua` (new).** Per-system, per-destination cache. Merges by
  destination so zone-gated systems accumulate full coverage across visits, while
  global systems refresh wholesale. `partial` marks gated systems. Pure/testable;
  `serialize()`/`restore()` round-trip through plain `loadstring`.
- **Passive capture.** Any tracked-system menu a warp already opens is decoded,
  cached, broadcast, and persisted — no extra pokes. Wired into the shared menu
  handler ahead of the warp/teardown paths.
- **Active scan.** `//sw <system> scan` (and `{"cmd":"scan","system":...}`) pokes the
  system's NPC purely to read its menu, then closes it without warping. Runs in a
  yield-capable coroutine (same fix as v1.2.2). Rejected for systems with no scheme.
- **Persistence.** Cache is dumped to `data/unlocks.lua` (debounced) and restored on
  load, so the checklist survives reloads. Lua-source dump because comm's inbound
  JSON decoder is flat-only; the nested structure needs `loadstring`.
- **Protocol.** New commands `scan`, `get_unlocks`; new events `unlocks` (one-system
  delta) and `unlocks_all` (full snapshot, also sent on connect after `state`/`lists`).
- **Currency deferred.** No retail-verified packet offset is known; this fork does
  not guess. Unlocks ship now; currency can follow once an offset is confirmed.

Verified offline: decode + cache + serialize round-trip across global and
zone-gated systems (incl. abyssea "Cavernous Maw", which shares a group with no
zone id of its own), and a socket round-trip of the nested `unlocks_all` over the
real comm transport. Pending live in-game confirmation.

## [1.2.2] — Fix: run GUI-triggered warps in a yield-capable coroutine

The actual cause of GUI warps stalling after the NPC poke. `comm.poll` dispatches
inbound commands via `pcall(on_command, ...)`. The warp path reaches `poke_npc`,
which blocks on `coroutine.sleep` — and **Lua 5.1 cannot yield across a C-call
boundary (`pcall`)**. So the moment the menu opened and `poke_npc` tried to sleep,
it raised "attempt to yield across C-call boundary", which the dispatch `pcall`
swallowed: the poke had already gone out (guide selected, menu open), then the
flow died silently. The typed `//sw` command was unaffected because it runs in
the `addon command` event coroutine, which can yield.

- `warp` and `warp_to` now hand execution to `coroutine.schedule(fn, 0)`, which
  runs the warp as a fresh top-level Windower coroutine — the same yield-capable
  context the `//sw` handler uses. `poke_npc`'s `coroutine.sleep` and the menu
  state machine work identically to a typed command.
- Non-poking commands (`cancel`/`reset`/`get_state`/`get_lists`) stay inline; they
  never yield.

This is the fix that makes GUI-issued warps actually complete. The v1.2.1 state
setup remains required (the menu retry handlers read it once a warp is running).



`warp_to` called `do_warp` directly but skipped the state setup that
`handle_warp` performs ahead of every warp. The initial menu selection was
unaffected (it reads `current_activity`, which `do_warp`/`poke_npc` populate),
but the menu state machine's retry handlers (menu timeout, event-skip, aborted
teleport) read `state.loop_count` and `state.current_warp`/`current_args` — left
`nil` on the structured path, so a retry would compare `nil > 0` and throw
instead of retrying.

- `warp_to` now mirrors `received_warp_command`/`handle_warp`: sets
  `debug_stack`, `loop_count = settings.max_retries`, `fast_retry = false`, and
  `current_warp`/`current_args` to a CLI-equivalent form so a scheduled retry
  re-resolves the same destination (the sub is stored as a single list element,
  so multi-word subs such as `Frontier Station` re-peel correctly).
- Still calls `do_warp` directly rather than re-tokenizing through
  `handle_warp`, preserving reachability of case-sensitive/multi-word subs (e.g.
  abyssea `Cavernous Maw`) that the CLI peeler cannot express.

Note: this is unrelated to the survival-guide menu `validate` gate (menu IDs
8500/8501). A warp that opens a different menu — e.g. a home-point menu — is
canceled by that upstream check identically on the typed CLI and the GUI path.



Gives the bridge a payload: the complete, browsable destination catalogue, plus
a structured warp command that can address any destination. Backward compatible;
the Stage 1 transport and every `//sw` command are unchanged.

**New — `destinations.lua` (catalogue normalizer):**
- Pure function `build(maps)` walks every map module's `warpdata` into one
  schema. Depends only on the maps table (no windower/packets), so it is
  unit-testable outside the game.
- Normalizes the two `warpdata` shapes the engine already distinguishes: FLAT
  (`warpdata[zone]` has `.index` — the zone is the destination) and NESTED
  (`warpdata[zone]` is a group of sub-entries). Real entries (`.index` or
  `.menu_id`) become destinations; `.shortcut` entries are folded in as display
  `aliases`, never emitted separately.
- Per-destination `label` resolution: leaf `display_name`, else shortcut
  aliases, else the sub-key. Forwards `zone_id` and the unlock-relevant fields
  (`index`/`offset`/`expac`/`cost`/`unlocked`) when present, for Stage 3.
- Output: **675 destinations across 13 systems**, sorted deterministically.

**New — commands:**
- `warp_to {system, zone, sub}` — structured warp. Calls `do_warp` directly with
  the canonical map-table key, zone-name key, and sub-key. This sidesteps
  `handle_warp`'s single-token sub-zone limitation, so multi-word destinations
  (e.g. waypoints' `Frontier Station`, index 41) are addressable — the
  `warp`/`line` path provably cannot express these. Honours the busy lock;
  rejects unknown systems with a chat notice. This is the launcher's primary
  warp path.
- `get_lists` — re-sends the catalogue on demand.

**New — events / connect sequence:**
- `lists` event: `{"t":"lists","version":...,"systems":[...]}`. Built once and
  cached (warpdata is immutable at runtime).
- On connect the addon now sends `state` then `lists`.

**Design note — why structured over CLI-string round-tripping:**
The Stage 1 `warp` path serializes to a `//sw` line and re-parses it. Testing the
normalizer against the real modules surfaced that `handle_warp` only peels the
*last single token* as a sub-zone, so any multi-word sub-zone (waypoints
`Frontier Station`, `Enigmatic Device`) can never round-trip as a string. Rather
than special-case the serializer, the GUI now passes the structured triple it
already holds and the addon resolves it directly. Every exported `(key, zone,
sub)` is verified to resolve to a real `warpdata` leaf.

**Verification:**
- Normalizer run against all 13 real map modules: 675 destinations, every one
  resolving to a real leaf; spot-checks on flat (survival guide), nested
  (homepoints alias label, escha), `display_name` (limbus), multi-word
  (waypoints), and unlock-bitmask (voidwatch) destinations.
- Full socket round-trip over a live TCP connection: on-connect `state` + `lists`
  (13 systems / 675 dests), `warp_to` dispatch including the multi-word case,
  and `get_lists` re-request.



First step of the external-GUI effort. Adds a non-blocking TCP/JSON server to
the addon and wires it into the existing command/state flow. Backward
compatible: every `//sw` command behaves exactly as before, and sessions with
no GUI connected pay no measurable cost.

**New — `comm.lua` (transport module):**
- Self-contained LuaSocket TCP server bound to `127.0.0.1:19519`, knows nothing
  about warps. Host supplies `on_command` and `on_connect` callbacks.
- Newline-delimited JSON, both directions. JSON encoder/decoder ported from
  Fisher v7.x — the encoder emits `[]` for empty tables (a `{}` there silently
  breaks strict array parsers), the decoder is flat-object-only by design.
- Non-blocking poll: accept (capped at 4 clients), per-client single-line read,
  `pcall(decode)`, dispatch. Oversized-line guard (4 KB). Early-out broadcast
  when no client is connected.
- `comm.init / poll / send / broadcast / client_count / shutdown` API.

**New — addon wiring (`superwarp.lua`):**
- `comm` required alongside the map modules.
- `build_state()` produces the GUI state snapshot: version, character, zone
  name + id, busy flag (true while a warp is in progress), logged-in flag.
- `handle_remote_command()` dispatches inbound GUI commands. The `warp` command
  splits its space-joined `line` into the exact CLI token stream and hands it to
  `received_warp_command` — no new resolution logic; the full fuzzy-match /
  shortcut / favorites pipeline is reused untouched. Unknown systems are
  rejected. `cancel` / `reset` clear state; `get_state` re-broadcasts.
- `prerender` event polls the server every frame.
- `load` event binds the server and registers the callbacks; on connect the new
  client is sent an immediate state snapshot.
- `unload` event now also closes the server and all client sockets.
- State is re-broadcast on zone change and on reset/cancel.
- `log()` mirrors all output to connected clients as `msg` events, guarded by a
  client-count check so CLI-only users are unaffected.

**Meta:**
- `_addon.version` bumped to `1.1.0`.
- `_addon.author` updated to credit the fork lineage.

**Verification:**
- `comm.lua` and all edited modules parse under Lua 5.1 (`luac5.1 -p`). The full
  `superwarp.lua` is validated by neutralizing only the pre-existing
  string-literal-method idiom (`'ICHC':pack(...)`, which PUC `luac` rejects but
  Windower's runtime accepts) and parsing the remainder clean.
- Transport verified end-to-end over a live socket against a Python client: the
  on-connect snapshot, `get_state` round-trip, a `warp` command splitting
  `"hp Bastok Markets 2"` into `system=hp, args={Bastok, Markets, 2}`, silent
  rejection of an unknown system, and a `cancel` state re-broadcast all pass.

---

## [1.0.4] — Crash-Fix Pass

Two confirmed crashers in the `missing` handlers, fixed. No behavior change
otherwise.

**Bug fixes:**
- `map/escha.lua` and `map/abyssea.lua`: the `missing` handler appended
  `z .. '-' .. d`, but `z` was never defined in that function — a leftover from
  refactoring the multi-zone loop down to a single-zone lookup. The result was
  a nil-concatenation crash on `//sw ew missing` / `//sw ab missing` whenever
  any destination in the current zone was still locked (when everything was
  unlocked, the append never ran and the bug stayed hidden). Fixed by capturing
  the zone name into a `zname` local at the point `zd` is selected, and
  appending `zname` instead. Output format (`ZoneName-PortalNumber`) preserved.

**Verified intentional (left as-is):**
- Reisenjima portals 8 and 10 in `map/escha.lua` omit `offset` on purpose — they
  are gated by a key item, not a menu bitfield.

**Deferred (possibly intentional; awaiting confirmation):**
- `map/odyssey.lua` lines ~497/515 pass `wiggle_value(...)` positionally instead
  of as `delay = ...`, dropping the simulated delay on two actions.
- `map/homepoints.lua` concatenates `destination.expac` into a `debug()` string
  before its nil-guard (safe today only because every homepoint leaf carries
  `expac`).
- Global-namespace leaks in `waypoints.lua`, `sortie.lua`, `odyssey.lua`.
- `superwarp.lua` `reset()`: `last_activity = activity` references an undefined
  `activity` (diagnostic-only).

---

## [1.0.3] — Upstream baseline (Akaden)

Original superwarp by Akaden. Text-based teleport assistance for FFXI on
Windower 4. Orchestrator (`superwarp.lua`) plus per-system plugin modules under
`map/`, a packet-driven menu state machine, fuzzy zone-name matching, IPC
send-all, shortcuts/favorites, and `missing`-destination reporting. This is the
fork point; no entry here enumerates upstream history.
