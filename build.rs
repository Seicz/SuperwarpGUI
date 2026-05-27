// Build script. Two jobs:
//
// 1. Decode assets/icon.png to a raw RGBA blob in OUT_DIR. main.rs then
//    `include_bytes!`s the blob and constructs an egui::IconData directly,
//    so the runtime binary never links image/png/fdeflate/flate2/miniz_oxide
//    just to load one icon at startup. This is cross-platform.
//
// 2. (Windows only) Embed the .ico + VERSIONINFO via winresource, so Explorer
//    gets the chevron icon and the Properties -> Details tab is populated.
//    Soft-fails to a cargo warning if no resource compiler is on PATH.

use std::{env, fs, path::PathBuf};

fn main() {
    // ---- 1. PNG -> raw RGBA --------------------------------------------
    let icon_src = "assets/icon.png";
    println!("cargo:rerun-if-changed={icon_src}");

    let out_dir = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR not set"));
    let png_bytes = fs::read(icon_src).expect("read assets/icon.png");

    let decoder = png::Decoder::new(png_bytes.as_slice());
    let mut reader = decoder.read_info().expect("decode icon png header");
    let mut buf = vec![0u8; reader.output_buffer_size()];
    let info = reader.next_frame(&mut buf).expect("decode icon png frame");

    // Normalize to 8-bit RGBA regardless of source channel layout.
    let rgba: Vec<u8> = match info.color_type {
        png::ColorType::Rgba => buf[..info.buffer_size()].to_vec(),
        png::ColorType::Rgb => buf[..info.buffer_size()]
            .chunks_exact(3)
            .flat_map(|p| [p[0], p[1], p[2], 0xFF])
            .collect(),
        other => panic!("unsupported icon png color type {other:?}; export as RGB or RGBA"),
    };

    fs::write(out_dir.join("icon_rgba.bin"), &rgba).expect("write icon_rgba.bin");
    fs::write(
        out_dir.join("icon_dims.rs"),
        format!(
            "pub const ICON_WIDTH: u32 = {};\npub const ICON_HEIGHT: u32 = {};\n",
            info.width, info.height
        ),
    )
    .expect("write icon_dims.rs");

    // ---- 2. Windows resource embedding ---------------------------------
    #[cfg(windows)]
    {
        let mut res = winresource::WindowsResource::new();
        res.set_icon("assets/icon.ico");
        res.set("ProductName", "superwarp GUI");
        res.set(
            "FileDescription",
            "superwarp GUI - external warp launcher for FFXI (Windower)",
        );
        res.set("CompanyName", "Eric Strawser (Seicz@Bahamut) - ITIWH.com");
        res.set(
            "LegalCopyright",
            "(c) Eric Strawser (Seicz@Bahamut). Built on Akaden's superwarp.",
        );
        if let Err(e) = res.compile() {
            println!("cargo:warning=winresource: could not embed icon/metadata: {e}");
        }
    }
}
