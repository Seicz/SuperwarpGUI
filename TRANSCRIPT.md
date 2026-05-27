# Transcript

Session log for the superwarp companion demo. User turns are verbatim; Claude
turns are reconstructed from session context (summarized where the original was
long-form prose) and appended as work proceeded.

---

**User:** [Provided Windower Lua wiki links: Events, Functions, Game-ID-Reference;
noted LuaCore.dll is Windower4's internal Lua.] Looking to combine the GUIs for
addons superwarp, fisher, and crucible. Trying to decide on C++ or Rust for the
GUI. Research project at this point. No building.

**Claude:** Framed the real fork: the language choice is downstream of *where the
GUI renders*. Branch A (in-game overlay) favors C++/Dear ImGui built on the
existing FFXiTools D3D8 hook, gated by the d3d8->d3d9 device-chain work. Branch B
(separate companion window) favors Rust/egui, generalizing the existing Fisher
GUI, with a LuaSocket hub addon over localhost TCP for event-driven comms. Noted
the three addons are independent Lua states needing a hub aggregator, and asked
which experience was intended.

**User:** Can't do in-game D3D without the plugin SDK. Companion app is the only
way. But C++ with ImGui is new to me. Want a leaner file size — the Rust/egui
solution is over 3 MB after compile.

**Claude:** Confirmed the size ordering is real (ImGui minimal ~0.5-1.5 MB vs
egui's higher floor from winit+wgpu). Recommended first running the cheap
experiment — egui with the glow backend + a size profile (opt-level "z", LTO,
codegen-units 1, panic abort, strip) commonly drops to ~1.5-2 MB — before
committing to a rewrite. Noted egui was modeled on Dear ImGui so the immediate-
mode intuition transfers; only the Win32 + D3D backend boilerplate is new. Flagged
CRT linkage (/MT vs /MD) as the biggest size lever and raw Winsock over asio.

**User:** It's nothing, but that's not the point. 3 MB on unnecessary clutter is
still unnecessary. Want lean and clean. Would like to give it a demo. Rewrite the
superwarp GUI in C++ with ImGui.

**Claude:** Confirmed superwarp's source wasn't in the project; pulled Akaden's
README to get the exact command surface (all warp systems + sub-commands + the
smart command + target modifiers). Built the demo:

- Vendored Dear ImGui v1.92.8 (pinned tag) with Win32 + DX11 backends.
- `src/net.{h,cpp}`: Winsock loopback client, one worker thread, queued sends,
  timeout-guarded non-blocking connect, auto-reconnect.
- `src/ui.{h,cpp}`: data-driven panel; every superwarp system described as a row
  in a table and rendered generically. Target selector, destination input,
  sub-command buttons, list-missing, smart-warp, global cancel/reset, log pane.
- `src/main.cpp`: trimmed ImGui Win32+DX11 host wiring in the net lifecycle.
- `hub/superwarp_hub.lua`: generic loopback relay (send_command), ASCII-only
  chat output, single-companion accept with line buffering.
- Size-tuned Release vcxproj (/MT, /O1 favor-size, LTCG, REF+ICF, RTTI off).
- Verified ImGui API usage by syntax-checking ui.cpp against the pinned headers
  (caught and removed a 1.92 two-arg PushFont mismatch).

Packaged as superwarp-companion-v0.1.0.zip.
