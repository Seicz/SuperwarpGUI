# superwarp companion

A native **C++ / Dear ImGui** companion-window front end for the Windower 4
addon [superwarp](https://github.com/AkadenTK/superwarp) (by Akaden). It renders
the full superwarp command surface as a point-and-click panel and relays the
chosen commands into the game over a localhost socket.

This is a **demo** — a lean-binary counterpart to an egui/Rust companion, built
to see how small and clean the C++/ImGui path is in practice.

> by Eric Strawser (Seicz@Bahamut) · ITIWH.com

---

## Why this exists

We can't render an in-game D3D overlay without the Windower plugin SDK, so a
**companion window** is the route. The question was C++/ImGui vs Rust/egui, and
the deciding factor was binary size: the egui build lands north of 3 MB after
compile. This project is the C++/ImGui demo to weigh against that.

## Architecture

```
  +-----------------------------+        loopback TCP         +------------------------+
  |  superwarp-companion.exe     |  127.0.0.1:51337            |  superwarp_hub.lua      |
  |  (this project)              | --------------------------> |  (Windower addon)       |
  |                              |   newline-delimited         |                         |
  |  Win32 + D3D11 + Dear ImGui  |   command lines             |  windower.send_command  |
  |  Winsock client (1 thread)   |                             |          |              |
  +-----------------------------+                             +----------|-------------+
                                                                          v
                                                              superwarp (//sw ...) in game
```

- **companion.exe** builds command strings (`sw hp party Norg 2`, `sw cancel`, …)
  and queues them to a single background Winsock thread.
- **superwarp_hub.lua** is a *generic* relay: it runs whatever line it receives
  via `windower.send_command`. It is intentionally not superwarp-specific, so
  the same hub can later front fisher and crucible in a combined GUI.
- No D3D hooking, no injection. The companion is an ordinary desktop window; the
  game stays untouched.

## Layout

```
superwarp-companion/
  src/
    main.cpp        Win32 + D3D11 bootstrap and frame loop
    ui.{h,cpp}      the panel - data-driven from superwarp's command tables
    net.{h,cpp}     Winsock loopback client (worker thread + queue)
  imgui/            vendored Dear ImGui v1.92.8 (MIT) + Win32/DX11 backends
  hub/
    superwarp_hub.lua   the relay addon
  superwarp-companion.sln / .vcxproj
```

## Build

1. Open `superwarp-companion.sln` in Visual Studio 2022 (toolset v143).
2. Select **Release | Win32**.
3. Build. Output: `build\Release\superwarp-companion.exe`.

Toolchain matches FFXiTools: v143, Win32, C++17, `/MT`. The Release config is
tuned for size (`/O1` favor-size, whole-program opt, `/LTCG`, `/OPT:REF`,
`/OPT:ICF`, RTTI off).

> Requires `d3dcompiler_47.dll`, which ships with Windows 10/11 — nothing extra
> to distribute.

### Size

Estimated Release sizes (measure your own after first build):

| Config | Approx size | Notes |
|---|---|---|
| `/MT` (this default) | ~0.8–1.3 MB | self-contained, no redist needed |
| `/MD` | ~0.4–0.6 MB | smaller, but needs the VC++ runtime present |

Either way, comfortably under the ~3 MB egui build. (Numbers are estimates — a
Win32 D3D11 binary can't be compiled in the authoring environment.)

If you want to go even leaner, the DX9 backend (`imgui_impl_dx9`) drops the
runtime shader-compiler path entirely; the trade is legacy D3D9.

## Run

1. Drop the `hub/superwarp_hub.lua` folder into `Windower4/addons/superwarp_hub/`
   and `//lua load superwarp_hub` in game (superwarp itself must also be loaded).
2. Launch `superwarp-companion.exe`.
3. The status badge turns green once connected. Pick a target (Me / Party / All),
   choose a warp system, and click. The log pane shows every line sent.

The companion runs fine with no hub connected — it just logs the command it
*would* send, so you can demo the UI without FFXI open. `//swhub status` reports
the relay state; `//swhub off` logs lines without executing them (dry run).

## Protocol

Newline-delimited command lines over `127.0.0.1:51337`. Each line is a Windower
console command. The companion sends superwarp's documented forms:

```
sw <prefix> [target] <destination>     warp to a destination
sw <prefix> [target] <token>           sub-command (enter/exit/port/...)
sw <prefix> [target] missing           list locked destinations
sw [target]                            the smart command (//sw)
sw cancel [target] / sw reset [target] misc verbs
```

`[target]` is omitted (me), `party`, or `all`.

## Security note

The hub binds loopback only. A localhost listener that runs console commands is
a minor local-trust surface; a shared handshake token would harden it and is the
natural next step if this graduates past demo.

## Credits

| Component | Author / Source |
|---|---|
| superwarp (command surface) | Akaden — https://github.com/AkadenTK/superwarp |
| Dear ImGui (bundled, MIT) | Omar Cornut — https://github.com/ocornut/imgui |
| companion + hub | Eric Strawser (Seicz@Bahamut) · ITIWH.com |
| AI-assisted development | Claude Opus 4.7 (Anthropic) |

superwarp's command strings are referenced for compatibility; superwarp itself
is not bundled or modified.
