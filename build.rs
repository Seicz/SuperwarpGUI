// Embeds the application icon and authorship/version metadata into the Windows
// executable as a Win32 resource. This gives Explorer the chevron icon (instead
// of the default) and fills in the file's Properties -> Details tab.
//
// No-op on non-Windows targets. FileVersion / ProductVersion / OriginalFilename
// are filled automatically by winresource from the CARGO_PKG_* environment.
// Strings are kept ASCII to avoid resource-compiler codepage issues.

fn main() {
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
        // Soft-fail: if no resource compiler is available, still build a working
        // exe (just without the icon/metadata) rather than aborting.
        if let Err(e) = res.compile() {
            println!("cargo:warning=winresource: could not embed icon/metadata: {e}");
        }
    }
}
