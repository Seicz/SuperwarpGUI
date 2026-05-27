# superwarp-gui v0.9.4 - patch contents

Tiny patch on top of v0.9.3. Drop in over your repo:

```
Cargo.toml             (replace - version bump only)
src/main.rs            (replace - one-line warning fix)
CHANGELOG.md           (replace)
TRANSCRIPT.md          (replace)
```

`build.rs`, `assets/`, and everything else from v0.9.3 are unchanged.

## What changed

- **`src/main.rs`** - line 741: `let mut chev = ...` -> `let chev = ...`
  inside `setup_aether_theme`. The closure paints two line segments and
  doesn't mutate its captures, so it's `Fn`, not `FnMut`.
- **`Cargo.toml`** - version 0.9.3 -> 0.9.4.

No behavioral change. Build should be warning-free now:

```
cargo clean
cargo build --release
```
