# TRANSCRIPT

Working log of the superwarp GUI-bridge fork. User turns are verbatim; Claude
turns are reconstructed faithfully from session context with engineering actions
(file reads, edits, tests) annotated in **[brackets]**. Appended as work proceeds.

Participants: Eric Strawser (Seicz@Bahamut) and Claude.

---

## Session 1

### Turn 1 — Pulse check

**User:** Provided the Windower Lua wiki links (Events, Functions, Game-ID
Reference) and noted LuaCore.dll is Windower 4's internal Lua, no external libs
needed. Uploaded the superwarp addon (README, superwarp.lua, sendall.lua,
fuzzyfind.lua, and the map modules: spd, survivalguides, unity, voidwatch, maps,
portals, protowaypoints, plus homepoints/waypoints/escha/abyssea/sortie/odyssey/
limbus). "This is superwarp. a text based teleport assistance addon for ffxi.
I'd like to analyze it first and get a reading on it, and work from there. Just
check its pulse and make sure its breathing for me, please."

**Claude:** **[Read superwarp.lua end-to-end plus the in-context modules.]**
Verdict: alive and well-built (Akaden v1.0.3). Architecture is a clean
orchestrator + plugin pattern — `superwarp.lua` is the engine, each `map/*.lua`
returns a table conforming to an implicit contract (`short_name`, `npc_names`,
`zone_npc_list`, `validate`, `missing`, `build_warp_packets`, optional
`sub_commands`, `warpdata`); `maps.lua` loads them and the engine dispatches
generically. Core flow is a packet-driven state machine: command → `handle_warp`
→ `resolve_warp` (fuzzy match) → `find_npc` → `poke_npc` (0x01A) → catch menu
packet (0x034/0x032) → `validate` → build an action queue → `perform_next_action`
walks it injecting 0x05B packets with `wait_packet`/`delay`/`expecting_zone`,
with scheduled timeout/retry. Flagged from the first read: `spd.lua`
`build_warp_packets` references undefined locals (dead path); `superwarp.lua:439`
`last_activity = activity` typo; `protowaypoints` `{name}` vs `{name=name}`
inconsistency (harmless); the `find_npc` distance check mixes true sqrt distance
(gate 15) with squared distance (vs 6^2). Noted that escha/abyssea/waypoints/
homepoints/sortie/odyssey/limbus had not yet been read. Asked where to point next.

### Turn 2 — Bug hunt

**User:** "crack open everything, we are going bug hunting first."

**Claude:** **[Read all remaining modules: homepoints, waypoints, escha, abyssea,
sortie, odyssey, limbus — full files and targeted greps.]** Delivered a ranked
catalogue:

- **Confirmed crashes (conditional):** `escha.lua:74` and `abyssea.lua:82` —
  `missing` appends `z..'-'..d` with `z` undefined (stale ref from the
  multi-zone→single-zone refactor). Crashes on `//sw ew/ab missing` when any
  destination in the current zone is locked.
- **Confirmed functional (silent):** `odyssey.lua:497/515` pass `wiggle_value()`
  positionally instead of `delay = ...` (delay dropped); `homepoints.lua:95`
  concatenates `destination.expac` before its nil-guard (safe only by data luck).
- **Smells / latent:** `superwarp.lua:439` `last_activity = activity` typo;
  global leaks in `waypoints.lua` (`npc_names`, `temp_list`), `sortie.lua`
  (the whole device/gadget/bitzer block + menu-id tables), `odyssey.lua`
  (`zone_tag`, `Sheol_A/B/C`); `sortie.lua` `find_*_by_id` ignore their param
  (dead+broken); `spd.lua` dead broken `build_warp_packets`.
- **Data gaps:** escha Reisenjima 8/10 lack `offset`; limbus has no `missing`.

Noted only the two `missing` crashers actually throw in normal use. Offered to
start a fix pass on those first.

### Turn 3 — Crash fix pass

**User:** "Reisenjima portals 8 and 10 lack an `offset` field, they are
triggered by having a special key item. they are omited on purpose. lets work on
the crashes as a fix pass, the functional bugs might be intentional so hold off
for now."

**Claude:** **[Copied escha.lua + abyssea.lua to a working dir; installed
luac5.1; baseline-parsed both.]** Applied the fix: capture the zone name into a
`zname` local where `zd` is selected (escha: 3 zones; abyssea: 9 zones), and
append `zname` instead of the phantom `z`. **[Re-parsed clean; confirmed no bare
`z` remained; wrote a stubbed Lua harness and exercised the formerly-crashing
locked-destination path.]** Results: escha locked offset 3 → `Escha Zi'tah-4`,
abyssea locked offset 5 → `Abyssea - Konschtat-6`, out-of-zone → friendly error,
no crash. **[Delivered escha.lua + abyssea.lua to outputs.]** Left functional
bugs parked per instruction.

### Turn 4 — GUI proposal (Fisher reference)

**User:** Uploaded Fisher (Fisher.lua, data.lua, main.rs, Cargo.toml/lock,
README, CHANGELOG). "So I'm giving you the files for fisher. The reason is, I
want to build a GUI for superwarp KINDOF like the same way we made one for
fisher. I'm providing fisher and its GUI to show how we did it. I think it's the
best idea since sliced bread."

**Claude:** **[Read both ends of the Fisher bridge: the Rust GUI
connection/threading + protocol enum, and the Lua TCP server + prerender poll.]**
Assessment: the **transport layer transfers wholesale** — LuaSocket server on
`127.0.0.1:19519`, `settimeout(0)`, polled from `prerender` (accept cap 4,
per-client `receive('*l')`, `pcall(json.decode)`, dispatch), newline-delimited
JSON, hand-rolled encoder/decoder, broadcast with early-out; Rust side does a
background-thread connect via mpsc, `set_nonblocking(true)`, drain `read_line`,
typed `serde_json`, 3 s auto-reconnect, JSON persistence. But the GUI's
**purpose must change**: Fisher is a stateful loop with continuous telemetry;
superwarp is request/response with nothing to monitor between warps. Where a
superwarp GUI earns its keep: destination browsing/launching, unlock
visualization (the `missing` bitfields), and zone-context awareness. Flagged the
**two-tier data problem** (static `warpdata` vs live menu-gated unlock/currency)
and that click-to-warp still requires being at the NPC. Proposed a 4-stage build.
**[Asked three scoping questions via the input tool.]**

### Turn 5 — Decisions + Stage 1 build

**User:** Primary purpose: **Both, equal weight.** Unlock/currency: **Cache +
persist last-seen per system.** Theme: **You pick something fitting.**

**Claude:** **[Re-read superwarp.lua integration points via greps and targeted
views: requires, settings/state, the `received_warp_command` funnel + `addon
command`/`unhandled command` events, `handle_warp` arg handling, zone-change and
unload events; confirmed `warp_list` and `current_activity` are globals and
no existing `prerender`/`socket`.]** Built Stage 1:

- **[Staged the full addon working tree with the escha/abyssea fixes overlaid.]**
- **[Ported Fisher's JSON encoder/decoder.]**
- **[Wrote `comm.lua`]** — self-contained TCP/JSON transport (init/poll/send/
  broadcast/client_count/shutdown), `on_command`/`on_connect` callbacks.
- **[Wired `superwarp.lua`]** — required `comm`; added `build_state()`,
  `handle_remote_command()` (warp/cancel/reset/get_state), `prerender` poll,
  `load` init with on-connect snapshot, `unload` shutdown, state broadcasts on
  zone change/reset, and a `log()` mirror to clients. The warp command ships
  tokens as a single space-joined `line` and reuses the CLI funnel — no new
  resolution logic; the JSON decoder stays flat.
- **[Verified: `comm.lua` parses; my superwarp edits parse once the pre-existing
  `'ICHC':pack` idiom is neutralized; installed LuaSocket and ran the real server
  with a Python client.]** All assertions passed: on-connect snapshot,
  `get_state` round-trip, `"hp Bastok Markets 2"` → `system=hp, args={Bastok,
  Markets, 2}`, unknown-system rejection (silent), `cancel` re-broadcast.
- **[Delivered `superwarp-gui-stage1.zip`]** (complete addon folder; dev harness
  kept out per delivery preference). Left `_addon.version` at 1.0.3 pending the
  user's call on fork versioning. Offered to proceed to Stage 2.

### Turn 6 — Formalize the fork (docs + versioning)

**User:** "I want you to process the README/CHANGELOG/TRANSCRIPT files and to
process everything as if we are incrementing versions. we are making changes, we
are actively forking, we are modifying, we should be monitoring and making note
of everything we do."

**Claude:** Established the version lineage — upstream **1.0.3** (Akaden),
**1.0.4** (crash-fix pass), **1.1.0** (GUI bridge Stage 1). **[Bumped
`_addon.version` to 1.1.0 and updated `_addon.author` to credit the fork.]**
Authored README.md (fork-aware, protocol, roadmap, two-tier-data rationale,
credits), CHANGELOG.md (semver, 1.1.0 / 1.0.4 / 1.0.3 baseline), and this
TRANSCRIPT.md. **[Re-bundled the complete addon plus the three docs as
`superwarp-v1.1.0.zip`.]**

---

*End of Session 1.*

---

## Session 2 — Stage 2: Static Destination Export

### Turn 7 — Live load confirmation

**User:** *[Screenshot of the in-game chat log showing `[superwarp] GUI server
listening on 127.0.0.1:19519` and the normal `data/settings.xml` write.]*
"superwarp loaded 1.1.0 in game no problems."

**Claude:** Confirmed Stage 1 validated live — the banner means the `load` event
fired, `comm.init` bound the socket cleanly, and the `prerender` poll runs
without throwing (the part the offline harness cannot exercise). Noted the
`settings.xml` line as upstream's normal per-character config write. Proceeded to
Stage 2.

### Turn 8 — Build the static destination export

**User:** "Begin stage 2 building please."

**Claude:** Surveyed `warpdata` across all 13 modules and confirmed the engine's
two-shape convention (FLAT: leaf has `.index`; NESTED: group of sub-entries, with
`.shortcut` entries as aliases). Verified `sub_zone_targets` per system and read
`resolve_warp` / `handle_warp` to learn how a `(zone, sub)` pair is consumed.

- **[Wrote `destinations.lua`]** — a pure `build(maps)` normalizer producing one
  catalogue schema. **[Tested it against the real map modules under lua5.1]**
  (stubbing T/S/windower and neutralizing the `'STR':method()` idiom for PUC
  luac).
- **[Bug surfaced by the test]** the first design emitted a CLI `line` per
  destination, but the run flagged 8 dead lines in waypoints (`Frontier
  Station`, `Enigmatic Device`): `handle_warp` peels only the last *single*
  token as a sub-zone, so multi-word sub-zones can never round-trip as a string.
- **[Design decision]** dropped the fragile `line` round-trip. The GUI already
  holds the canonical `(system, zone, sub)`; it now ships that triple and the
  addon calls `do_warp` directly. **[Rewrote `destinations.lua`]** to emit
  structured destinations (no `line`, no addressability gating); re-ran the test
  — **675 destinations across 13 systems, every `(key, zone, sub)` verified to
  resolve to a real `warpdata` leaf**, plus spot-checks on flat / nested / alias
  / `display_name` / multi-word / unlock-bitmask cases.
- **[Wired `superwarp.lua`]** required `destinations`; added cached
  `build_lists()`; added the `warp_to` (structured, busy-guarded → `do_warp`)
  and `get_lists` commands; on-connect now sends `state` then `lists`. **[Bumped
  `_addon.version` to 1.2.0.]**
- **[Full live socket round-trip]** with a Python client: on-connect `state` +
  `lists` (13 systems / 675 dests), `warp_to` dispatch for both a numbered sub
  and the multi-word `Ceizak Battlegrounds / Frontier Station`, and a `get_lists`
  re-request. All assertions passed.
- **[Updated README / CHANGELOG / this TRANSCRIPT; re-bundled
  `superwarp-v1.2.0.zip`]** (complete addon + three docs; dev harness kept out of
  the delivery per preference).

### Turn 9 — Live bridge tests + warp_to retry-state fix (v1.2.1)

**User:** *[Connection test from home: `python -c "...recv(4096)"` printing the
state snapshot with a trailing `\n`.]* "Seems to be working but i noticed a
trailing `\n` breakline. Should that be there?"

**Claude:** Confirmed yes — the `\n` is the frame delimiter for newline-delimited
JSON, required so a client can split the stream into discrete messages (it
matters more in 1.2.0, where connect sends `state` then `lists` back to back).
Noted the snapshot still showed `1.1.0` (the loaded build) and that key order is
insignificant. Gave a two-line read helper to see both messages.

**User:** *[Second test: `messages: 2`, `version":"1.2.0"`, `state` + head of
`lists`.]* Asked for the Python command to push a `warp_to`.

**Claude:** Supplied a connect → drain → `warp_to survivalguides / Bastok Mines`
→ read-loop one-liner; flagged that the warp runs the real `do_warp` path (must
be in range of the NPC) and that a trailing timeout is just the loop running dry.

**User:** *[Screenshot: in Ru'Lude Gardens; the addon selected the guide and a
menu opened (home-point teleport text), but it went no further; Python timed
out.]*

**Claude:** **[Read `do_warp`, `handle_warp`, `received_warp_command`, the
incoming-chunk menu handler, `poke_npc`, and `survivalguides.validate`.]** Two
findings:
- **The stall is the survival-guide `validate` gate, not the bridge.**
  `survivalguides.validate` accepts only menu IDs 8500/8501; the menu that opened
  was a home-point menu, so it was canceled. `do_warp` is identical on the typed
  and structured paths, so `//sw sg Bastok Mines` from that spot behaves the
  same. The structured path drove the interaction correctly to the same gate.
- **A real, separate bug in `warp_to`:** it called `do_warp` directly and skipped
  the state `handle_warp` sets up. The initial selection was fine (it reads
  `current_activity`, populated by `do_warp`/`poke_npc`), but the menu retry
  handlers read `state.loop_count` / `current_warp` / `current_args` — `nil` on
  the structured path, so a retry would throw on `nil > 0`. **[Verified against
  real data that only 9 subs (`Cavernous Maw`, a casing quirk in abyssea's
  `sub_zone_targets`) fail the CLI peeler, confirming a direct-`do_warp` path is
  worth preserving over routing through `handle_warp`.]**

**[Fixed: `warp_to` now mirrors `received_warp_command`/`handle_warp` setup
(`debug_stack`, `loop_count`, `fast_retry`, `current_warp`, `current_args`) while
still calling `do_warp` directly. Bumped to v1.2.1; updated CHANGELOG and this
TRANSCRIPT; re-bundled `superwarp-v1.2.1.zip`.]**

---

### Turn 10 — Root cause: yielding across the poll pcall (v1.2.2)

**User:** "I was in front of the survival guide in Ru'Lude. I ran the command
next to it, it selected the guide, but nothing happened. I ran the same command
in game chat, and I teleported to Bastok Mines correctly... so it's not the
engine itself." *[Screenshot: warp_to from terminal, Python timed out.]*

**Claude:** The typed command working from the same spot ruled out the
validate-gate theory and pinned the fault to the GUI path. **[Read `poke_npc` and
`comm.poll`.]** Root cause: `poke_npc` blocks on `coroutine.sleep`, and
`comm.poll` dispatches commands inside `pcall(on_command, ...)`. **Lua 5.1 cannot
yield across a C-call boundary**, so when the menu opened and `poke_npc` tried to
sleep, it raised "attempt to yield across C-call boundary", which the dispatch
`pcall` swallowed — the poke had already fired (guide selected), then the flow
died silently. The typed `//sw` path runs in the `addon command` coroutine, which
can yield, so it was unaffected.

**[Fixed: `warp` and `warp_to` now hand execution to `coroutine.schedule(fn, 0)`,
running the warp as a fresh top-level Windower coroutine — the same yield-capable
context the `//sw` handler uses. Non-poking commands stay inline. Bumped to
v1.2.2; updated CHANGELOG and this TRANSCRIPT; re-bundled
`superwarp-v1.2.2.zip`.]** This is the fix that makes GUI-issued warps actually
complete; the v1.2.1 state setup remains needed for the in-flight menu retries.

---

*End of Session 2.*



## Session 3 — Stage 3: live unlock-state capture

**User:** I also lean passive-by-default with an explicit active refresh on
demand. Moving on to Stage 3.

**Claude:** Agreed on passive-by-default + active refresh. Before writing
anything, grounded the design in how each system actually exposes unlock state.
The addon already has a `missing` mechanism (`//sw X missing` pokes an NPC, catches
the menu, calls `map.missing(warpdata, zone, p)`) — that is the capture primitive
to reuse rather than reinvent.

Surveyed all `missing()` functions. Two scope patterns, and four distinct bit
decodes:
- **global** (one menu reveals everything): survivalguides (bit_start 96),
  homepoints (32, keyed on `index`), waypoints (32, `offset` + inverted
  `invoffset`), unity (192), voidwatch (4-byte mask via `unpack('I',21)`),
  protowaypoints (0), portals (32, but only readable at Whitegate).
- **zone-scoped** (one menu reveals only the current region's group):
  abyssea (128), escha (32) — both map a zone id to a `warpdata` group by name.
- sortie/odyssey/limbus have no `missing()`; spd's returns empty.

Because the bit decode varies four ways, re-implementing it would duplicate
authoritative logic and risk drift. **Decision: delegate all bit decoding to each
module's `missing()`** and add only the one thing it doesn't expose — scope.

**[Anomaly + recovery.]** Found a prior session had already wired Stage 3 into
`superwarp.lua` against an `unlocks.has_scheme` / `unlocks.decode` interface, with
an intact `unlockcache.lua` and `files`-based persistence to `data/unlocks.lua`.
Mid-work, `unlocks.lua` was accidentally overwritten with a different
(`capture`/`SCOPE`) draft. Recovered by reconstructing `unlocks.lua` to the exact
`has_scheme`/`decode` contract the wiring and `unlockcache.merge` expect, rather
than rewiring everything.

**`unlocks.lua` (reconstructed).** `decode(maps, map_name, zone_id, p)` calls the
module's `missing()` for the locked set, then enumerates the covered destinations
straight from `warpdata` (flat leaves with `index`; nested groups, scoped by a
sub-entry's numeric `zone` for zone systems — which also pulls in members with no
zone id of their own, e.g. abyssea "Cavernous Maw"). Returns `{ system,
out_of_range, partial, entries={ {zone, sub?, unlocked} } }`. Identifiers fed to
the locked-set match `missing()`'s own string convention (flat `<zone>`, nested
`<zone>-<sub>`).

**`unlockcache.lua` (existing).** Merges by destination so zone-gated systems
accumulate across visits; `partial` marks them; `serialize()`/`restore()`
round-trip through `loadstring`.

**Wiring (existing, verified coherent):** passive `capture_unlocks` in the shared
menu handler (ahead of warp/teardown), active `do_scan` + `//sw <sys> scan` and
`{"cmd":"scan"}` (yield-capable coroutine, no-warp teardown), `get_unlocks`,
debounced persistence, and `unlocks_all` sent on connect after `state`/`lists`.

**[Verified offline.]** decode + merge + snapshot + serialize/restore across global
(survivalguides: 96 unlocked / 2 locked, partial=false) and zone-gated systems
(abyssea La Theine + Tahrongi accumulate to 18, incl. Cavernous Maw, partial=true;
uncovered zone and portals-off-Whitegate both yield `out_of_range`). Socket
round-trip of the nested `unlocks_all` over the real comm transport (on-connect +
`get_unlocks`). All parse-checks pass.

**[Shipped: bumped to v1.3.0; updated README (protocol, unlock events, roadmap,
file structure), CHANGELOG, and this TRANSCRIPT; re-bundled
`superwarp-v1.3.0.zip`.]** Currency capture deferred — no retail-verified offset,
and this fork does not guess. Pending live in-game confirmation of passive capture,
`scan`, and persistence across reload.

---

*End of Session 3.*

---

## Session 4 — currency capture (v1.3.1)

**[v1.3.0 live-confirmed.]** A `get_unlocks` query over the real transport returned
the `unlocks_all` payload with survivalguides fully decoded (per-zone `unlocked`
true/false, `partial:false`). Passive capture and the on-connect/`get_unlocks`
path are confirmed working in-game.

**Directive: add currency capture (v1.3.1), then move to Stage 4.** Framed the
value honestly first: gil systems (homepoints/survivalguides) gain little since gil
is never scarce; the payoff is the consumable currencies — cruor (abyssea/voidwatch),
silt (escha), unity accolades — where a GUI affordability indicator is genuinely
useful. Scope kept minimal: surface the balance the menu already reveals, riding on
captures that already happen, no extra pokes.

**Abyssea offset ambiguity resolved.** Abyssea reads cruor at two offsets gated by
`if zone == destination.zone`: the conflux menu (standing in the abyssea zone) reads
`I@29` with `unlock_bit_start=128`; the entry NPC reads `i@5` with `unlock_bit_start=0`.
Since `missing()` only resolves inside an abyssea zone, `decode()` only ever succeeds
at the conflux — so `I@29` is unambiguous and the entry-NPC offset never applies.

**Implementation.** `unlocks.lua` gains a small `CURRENCY` spec (name/fmt/pos per
system, lifted verbatim from each module's `build_warp_packets`) and a guarded
`read_currency()` (discards negative / out-of-range reads). `decode()`'s success
path attaches `currency = { name, value }` or `nil`; the `out_of_range` path leaves
it nil. `unlockcache.lua`: `merge()` stores last-seen currency (preserved if a later
capture lacks one), `snapshot_system()` exposes it, `serialize()`/`restore()`
round-trip it. No `superwarp.lua` structural change — currency rides the existing
snapshot through `unlocks`/`unlocks_all`.

**[Verified offline.]** decode reads gil for a global system (survivalguides `I@9`)
and cruor for a zone-gated one (abyssea `I@29`); the `out_of_range` path carries no
currency; merge → snapshot → serialize → `loadstring` → restore preserves both the
currency and `partial`. All parse-checks pass.

**[Shipped: bumped to v1.3.1; CHANGELOG + README (unlock-event `currency` field)
updated; re-bundled `superwarp-v1.3.1.zip`.]** Pending live confirmation that real
balances populate (e.g. cruor at a conflux, silt in Escha). Stage 4 (Rust/egui GUI)
is next once 1.3.1 checks out.

---

## Session 4 (cont.) — Stage 4a: GUI foundation (GUI v0.1.0)

With v1.3.1 confirmed, the external GUI build began as a sibling project
(`superwarp-gui/`, versioned independently). Mirrors the Fisher GUI's eframe 0.31
bridge (bg connect thread, mpsc, non-blocking line JSON, 3s reconnect, JSON config
next to exe). First cut: bridge + protocol (`state`/`lists`/`unlocks`/`unlocks_all`/
`msg`; `warp_to`/`scan`/`cancel`/`get_*`), system browser, destination list with
state dots + click-to-warp + affordability, scan, per-system currency, search,
header/footer, and an "Aether" indigo/azure/violet theme distinct from Fisher.
Shipped `superwarp-gui-v0.1.0.zip` (README/CHANGELOG/TRANSCRIPT included). Not
compiled in-environment (no toolchain); user builds locally. Stage 4b: polish +
conveniences after live compile.

---

## Session 5 — fuzzyfind lean v1.0.0 swap (addon v1.3.2)

User reworked `fuzzyfind` in another chat into the lean v1.0.0 (gap-aware
subsequence scorer + Levenshtein fallback; LCS and confidence floor removed) and
asked for the two-minute call-site audit before trusting it live.

**Audit:** only two call sites, both in `resolve_warp` (zone match, sub-zone match).
Neither destructured a third value, so the new `tier` return is harmless. The real
dependency was line 254's gate `closest_zone_value >= 3 and closest_zone_value >=
#zone` -- built on the old score scale (LCS length / substring length), meaningless
on the lean's scale (fuzzy ~16xneedle; approx 0-100 similarity).

**Fix:** dropped the numeric gate; retuned both matches to the tier. Act on
exact/fuzzy; treat `approx` (input was a subsequence of nothing) as a suggestion --
'No close match for X. Did you mean Y?' -- rather than silently warping to an
edit-distance guess. The sub-zone match (previously unguarded, and the lean never
returns nil) got the same treatment so a typo can't silently warp to the closest
sub-zone. Confirmed no other old-scale comparisons remain (the surviving
`closest_zone_value` refs are debug logging only). README file-structure note
updated (no longer "upstream"; now lean v1.0.0, Lili + Eric). Parse-clean.

Shipped addon `superwarp-v1.3.2.zip`.

---

## Session 6 -- libs/ reorganization (addon v1.3.3)

Request: move all loose modules into a libs/ folder so only superwarp.lua lives in
root. Found no manual package.path setup -- Windower puts the addon root on the
path as `?.lua`, which is how both `require('comm')` and `require('map/maps')`
(literal '/') resolve. So the clean move: add `libs/` to package.path once, then
relocate files; every require string stays unchanged. Added
`package.path = package.path .. ';' .. windower.addon_path .. 'libs/?.lua'` after
the shared-lib requires; moved comm/destinations/unlocks/unlockcache/fuzzyfind/
sendall and the map/ folder into libs/ (libs/map/). Verified all requires resolve
to existing files; parse-clean. README tree updated. No behavior change.

Shipped `superwarp-v1.3.3.zip`.

---

## Session 7 -- re-base onto upstream superwarp v1.1.1+ (addon v1.4.0)

**Eric:** Akaden released a superwarp update (he tags it "1.1.1+", the "+" is not
a further number). We need to figure out what changed from our 1.0.3 fork point,
then apply his changes onto our 1.3.3. [Uploads delivered in two waves: 10 files,
then the remaining 12 -- all 1.1.1+ map modules, superwarp.lua, sendall.lua,
fuzzyfind.lua, maps.lua, README.]

**Claude:** Staged all 1.1.1+ files and ran a full diff against the 1.0.3 baseline.
Findings:
- `superwarp.lua` nearly doubled (1017 -> 1826 lines): per-character settings
  (`settings_<name>.xml`), first-run "understood" gate, `hp_legacy_defaults`,
  Limbus chest tracking + IPC sync, new dispatch (`the_superwarp`, `magic_map`,
  `smart_command`, `resolve_waypoint_subzone`), `all_warp_zones` validation, and
  `send_all_with_confirm` integration.
- `sendall.lua` fully rewritten (68 -> 355): scan-based participant discovery,
  on-screen "waiting on X" readout, `reset_all`, `warp_listener`.
- Four new modules: `campaign`, `garden`, `incursion`, `walkofechoes`.
- Every existing module: capitalized `long_name` (incl. "veridical conflux" ->
  "Abyssea", "eschan portal" -> "Escha"), trimmed `short_name`, reworded help;
  plus real fixes (sortie tables, waypoints auto-subzone, "already there" guards,
  jittered delays, homepoints auto-select).
- `fuzzyfind.lua` IDENTICAL to 1.0.3 -- Akaden never touched it, so our lean swap
  has zero conflict.
- **Critical catch:** the new modules are coupled to the new orchestrator
  (`validate` now takes `p`; a version check requires `maps.abyssea.target_ids`,
  `maps.campaign.target_ids`, `maps.odyssey.all_warp_zones`, `maps.incursion`,
  `maps.walkofechoes.all_warp_zones`). And upstream's 1.1.1 `abyssea`/`escha`
  `missing()` STILL have the nil-concat crash we fixed (`z` undefined). So this is
  a re-base, not a cherry-pick: take 1.1.1+ as the new base, re-apply our fork.

Presented four decisions (per-character settings; adopt `long_name` renames that
change GUI labels; adopt `send_all_with_confirm`; adopt Limbus tracking +
`hp_legacy_defaults`) -- all recommended "adopt upstream."

**Eric:** Go ahead and proceed on that basis. Your justifications are sound.

**Claude:** Executed the re-base in stages.
- *Stage 1:* laid down all 18 upstream map modules + `sendall` into `libs/`, kept
  fork modules (`comm`/`destinations`/`unlocks`/`unlockcache`/lean `fuzzyfind`),
  re-applied the `abyssea`/`escha` `missing()` crash fix (capture `zname` per zone
  branch, append `zname..'-'..d`). All 18 modules + sendall parse clean.
- *Stage 2:* re-wove the GUI bridge into the new orchestrator: `package.path` +
  fork requires; `log()` mirror; lean-fuzzyfind tier handling re-applied to
  `resolve_warp` (main + sub-zone) and `resolve_waypoint_subzone` (approx ->
  "Did you mean...?"; for `waypoints` approx falls through to the global
  sub-zone search); a self-contained fork block (`do_scan`, `build_state`,
  `build_lists`, unlock-cache funcs, `capture_unlocks`, `handle_remote_command`)
  inserted before the event registrations so the incoming-chunk closure captures
  `capture_unlocks`; `prerender` poll; `comm.init`/`on_connect` in `load`;
  `comm.shutdown` in `unload`; state broadcasts on zone-change/reset/cancel;
  passive capture + scan teardown at the `0x034/0x032` menu catch (after the
  NPC-mismatch guard); CLI `//sw <sys> scan` via a forward-declared `do_scan`.
  Woven `superwarp.lua` parses clean at 2106 lines; version check satisfied.
- *Verification:* unit-tested `destinations.build` against mock versions of all
  four new warpdata shapes (campaign/incursion flat-without-zone; garden/
  walkofechoes empty/sub-command-only) -- no crash; new systems correctly
  untracked by `unlocks` (has_scheme=false) so capture/scan skip them.
- *Stage 3:* GUI verified fully catalogue-driven (keys on `s.key`, displays
  `s.long`/`s.short`; no hardcoded names) -> renamed + new systems flow in with
  zero GUI code change. GUI stays at v0.3.2.
- *Stage 4:* `_addon.version` -> `1.4.0`, author credits the fork, welcome string
  updated; README (lineage + file tree), CHANGELOG (1.4.0 entry), TRANSCRIPT
  updated; re-bundled `superwarp-v1.4.0.zip`.

---

## Session 7 (cont.) -- scan NPC-mismatch fix (v1.4.1)

**Eric:** [screenshot] Running scan from the GUI OR the command line gives
"superwarp: This is not the NPC the warp was meant for. Shutting it down."

**Claude:** Root-caused it. `poke_npc` records the poked NPC into
`current_activity.poked_npc_id` through the file-scope `intended_npc`, but only
takes the "fresh" branch when `intended_npc.id` is nil. The warp path clears
`intended_npc = {}` in `received_warp_command` before poking; both scan paths
reach `poke_npc` without that reset, so a stale id from a prior warp gets copied
and the menu packet's real NPC fails the mismatch guard. Fixed by clearing
`intended_npc` in `do_scan` right before `poke_npc`. Bumped to v1.4.1.

---

## Session 7 (cont.) -- GUI warp NPC-mismatch fix (v1.4.2)

**Eric:** [screenshot] Now actual GUI warps fail the same way: "Warping via Home
Point #2 to Ru'Lude Gardens - 2." immediately followed by "This is not the NPC
the warp was meant for. Shutting it down." -- on every homepoint warp.

**Claude:** Same root cause as 1.4.1, wider scope. `received_warp_command` is the
only warp entry that clears `intended_npc` (and resets `state.warp_confirmed`,
`state.current_sub_cmd`, `prev_location`) before warping -- state the new
orchestrator's `poke_npc` and warp state-machine rely on. Our GUI `warp_to` calls
`do_warp` directly (to keep multi-word subs reachable) and so skipped all of it,
inheriting a stale `intended_npc`. Fixed by mirroring `received_warp_command`'s
full pre-warp setup inside the `warp_to` handler. Bumped to v1.4.2.

---

## Session 8 -- GUI dispatch toggle + sub-command submenu (addon v1.5.0 / GUI v0.4.0)

**Eric:** [mockup] I'd like the sub-commands in their own little submenu on the
warp side, plus a top toggle for sending the command to all my characters / party
/ just one (off). Use your creativity.

**Claude:** Traced the addon's dispatch model: all/party lives inside handle_warp
(detects an 'a'/'p' token at args[1], then send_all_with_confirm broadcasts the
command string to participants). The explicit-warp chat branch is just
`received_warp_command(cmd, args)` with args = [dispatcher?, ...dest/sub tokens].
So the GUI can drive the exact same entry.

Addon (v1.5.0): extended handle_remote_command -- `warp_to` gains an optional
`dispatch` (all/party routes through received_warp_command so send_all fires;
single keeps the structured do_warp path for multi-word sub-zones); added
`sub_cmd{system,sub,dispatch?}` (routes through received_warp_command ->
do_sub_cmd); `cancel{dispatch?}` does reset() or reset_all(participants).

GUI (v0.4.0): added a Dispatch enum (Off/Party/All) with a cycling toggle button
in the warp-panel header (Off=muted, Party=azure, All=coral), defaulting to Off
each launch for safety; an ACTIONS submenu under the search showing the selected
system's sub-commands as chips plus cancel/reset (hidden during a global search);
SystemList gained a `subcommands` field (already emitted by destinations.build).
warp_to/sub_cmd/cancel all attach the dispatch token when not Off.

---

## Session 8 (cont.) -- sub-command routing fix (v1.5.1)

**Eric:** Clicking GUI sub-command buttons does nothing -- Homepoint "set" should
set the closest homepoint, Mog Garden "zone" should zone me in (even standing next
to the NPC), but neither fires.

**Claude:** `received_warp_command` matches the system by SHORT-NAME (handle_map_
short_name / warp_list), but the new sub_cmd and dispatch paths passed the system
KEY (c.system = "homepoints"/"garden") through to it, so handle_warp matched no map
and silently did nothing. (Single warps were fine -- they use do_warp, which takes
the key directly.) Fixed by resolving map.short_name[1] before received_warp_command
in both the sub_cmd and warp_to-dispatch branches. v1.5.1.

---

## Session (addon v1.6.0) -- addon debug over the bridge

**Eric:** debug is an option in the superwarp addon, not the GUI.

**Claude:** Right -- moved it addon-side. build_state() now reports settings.debug;
added a `debug` remote command ({"cmd":"debug","on":bool}, or toggle if omitted) that
sets settings.debug, saves, and re-broadcasts state -- the bridge equivalent of
//sw debug. The GUI's Advanced checkbox drives this and reflects the reported state.
