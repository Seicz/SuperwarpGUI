# superwarp-gui v0.9.3 - patch contents

Drop these files into your existing `superwarp-gui` repo, overwriting where
they collide:

```
Cargo.toml             (replace)
build.rs               (replace)
src/main.rs            (replace)
assets/Hack-Regular.ttf   (new)
assets/Hack-LICENSE.md    (new, SIL OFL 1.1 attribution)
CHANGELOG.md           (replace)
TRANSCRIPT.md          (replace)
```

Your existing `assets/icon.png` and `assets/icon.ico` are unchanged and still
required - `build.rs` reads `assets/icon.png` at build time and embeds
`assets/icon.ico` into the Windows resource.

## What changed in code

- **`Cargo.toml`** - `eframe` is now `default-features = false, features = ["glow"]`
  (was `default_fonts` + glow + the v0.31 defaults). `png = "0.17"` added under
  `[build-dependencies]`. Version bumped to 0.9.3.
- **`build.rs`** - new step decodes `assets/icon.png` to a raw RGBA blob in
  `OUT_DIR` and writes `icon_dims.rs` with `ICON_WIDTH` / `ICON_HEIGHT`. The
  Windows resource block is unchanged.
- **`src/main.rs`** - three localized edits:
  1. New "Embedded assets" block above `main()`: `UI_FONT`, `ICON_RGBA`,
     `include!(.../icon_dims.rs)`, `install_fonts(ctx)`, `app_icon()`.
  2. The `eframe::icon_data::from_png_bytes(...)` block in `main()` is
     replaced with `viewport.with_icon(Arc::new(app_icon()))`.
  3. `install_fonts(&cc.egui_ctx)` added at the top of the `run_native`
     closure, just before `setup_aether_theme`.

No other source touched. No protocol or behavioral change.

## Build & measure

```
cargo clean
cargo build --release
```

Then `dir target\release\superwarp-gui.exe` for the new size, and
`cargo bloat --release --crates` for the new `.text` breakdown (the `image` /
`png` / `fdeflate` / `flate2` / `miniz_oxide` entries should disappear).

Expected: roughly 3.0 MB -> ~1.7-2.0 MB.

## Font attribution

Hack-Regular.ttf is SIL OFL 1.1; the unmodified license text is at
`assets/Hack-LICENSE.md`. No code or font modification - just vendored as-is
from source-foundry/Hack so it ships with the build.
