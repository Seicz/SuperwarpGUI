// superwarp GUI — Rust + egui
// External GUI for the superwarp FFXI addon fork (Akaden's superwarp; fork by
// Eric Strawser (Seicz@Bahamut)). Connects to superwarp.lua's TCP server on
// localhost:19519 and speaks its line-delimited JSON protocol.
//
// Aether/warp theme: deep indigo console with a luminous azure accent and an
// arcane-violet highlight -- visually distinct from the Fisher GUI.
//
// by Eric Strawser (Seicz@Bahamut) . ITIWH.com

#![windows_subsystem = "windows"]

use eframe::egui::{self, Color32, CornerRadius, FontId, Margin, RichText, Stroke, Vec2};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::io::{BufRead, BufReader, Write};
use std::net::TcpStream;
use std::sync::mpsc;
use std::time::{Duration, Instant};

// =========================================================================
// Aether theme palette
// =========================================================================

const BG: Color32 = Color32::from_rgb(14, 15, 26); // deep indigo-black
const PANEL: Color32 = Color32::from_rgb(23, 26, 43); // raised panel
const INPUT: Color32 = Color32::from_rgb(10, 11, 20); // sunken input
const HOVER: Color32 = Color32::from_rgb(32, 37, 60);

const AZURE: Color32 = Color32::from_rgb(63, 200, 224); // primary warp glow
const AZURE_DIM: Color32 = Color32::from_rgb(30, 100, 116);
const VIOLET: Color32 = Color32::from_rgb(155, 108, 232); // arcane highlight
const TEXT: Color32 = Color32::from_rgb(213, 216, 234); // soft lavender-white
const MUTED: Color32 = Color32::from_rgb(107, 113, 148);
const WHITE: Color32 = Color32::from_rgb(245, 247, 255);

const LOCKED: Color32 = Color32::from_rgb(208, 85, 107); // muted rose
const UNLOCKED: Color32 = Color32::from_rgb(79, 208, 138); // aether green
const GOLD: Color32 = Color32::from_rgb(220, 188, 110); // currency

const GUI_VERSION: &str = "0.3.2";

// =========================================================================
// Protocol types (match superwarp.lua's JSON exactly)
// =========================================================================

#[derive(Debug, Deserialize, Clone)]
struct Currency {
    name: String,
    #[serde(default)]
    value: i64,
}

// A destination from the `lists` catalogue. Only the fields the GUI uses are
// named; the rest of each record is ignored.
#[derive(Debug, Deserialize, Clone, Default)]
struct Dest {
    #[serde(default)]
    zone: String,
    #[serde(default)]
    sub: Option<String>,
    #[serde(default)]
    label: String,
    #[serde(default)]
    aliases: Vec<String>,
    #[serde(default)]
    cost: Option<i64>,
}

#[derive(Debug, Deserialize, Clone, Default)]
struct SystemList {
    #[serde(default)]
    key: String,
    #[serde(default)]
    short: String,
    #[serde(default)]
    long: String,
    #[serde(default)]
    plural: String,
    #[serde(default)]
    count: u32,
    #[serde(default)]
    destinations: Vec<Dest>,
}

#[derive(Debug, Deserialize, Clone)]
struct UnlockDest {
    #[serde(default)]
    zone: String,
    #[serde(default)]
    sub: Option<String>,
    #[serde(default)]
    unlocked: bool,
}

#[derive(Debug, Deserialize, Clone, Default)]
struct UnlockSystem {
    #[serde(default)]
    system: String,
    #[serde(default)]
    updated: i64,
    #[serde(default)]
    partial: bool,
    #[serde(default)]
    currency: Option<Currency>,
    #[serde(default)]
    destinations: Vec<UnlockDest>,
}

#[derive(Debug, Deserialize, Clone)]
#[serde(tag = "t")]
enum ServerMsg {
    #[serde(rename = "state")]
    State {
        #[serde(default)]
        logged_in: bool,
        #[serde(default)]
        character: String,
        #[serde(default)]
        version: String,
        #[serde(default)]
        zone: String,
        #[serde(default)]
        zone_id: u32,
        #[serde(default)]
        busy: bool,
    },
    #[serde(rename = "lists")]
    Lists {
        #[serde(default)]
        systems: Vec<SystemList>,
    },
    // One system's unlock snapshot (same shape as a UnlockSystem, plus `t`).
    #[serde(rename = "unlocks")]
    Unlocks {
        #[serde(default)]
        system: String,
        #[serde(default)]
        updated: i64,
        #[serde(default)]
        partial: bool,
        #[serde(default)]
        currency: Option<Currency>,
        #[serde(default)]
        destinations: Vec<UnlockDest>,
    },
    #[serde(rename = "unlocks_all")]
    UnlocksAll {
        #[serde(default)]
        systems: Vec<UnlockSystem>,
    },
    #[serde(rename = "msg")]
    Message {
        #[serde(default)]
        text: String,
        #[serde(default)]
        level: String,
    },
}

// =========================================================================
// Persisted config (next to the exe, like Fisher)
// =========================================================================

#[derive(Clone, Serialize, Deserialize, Default)]
struct Config {
    window_x: Option<f32>,
    window_y: Option<f32>,
    window_w: Option<f32>,
    window_h: Option<f32>,
    #[serde(default)]
    always_on_top: bool,
    #[serde(default)]
    last_system: Option<String>,
    #[serde(skip)]
    dirty: bool,
}

fn config_path() -> std::path::PathBuf {
    std::env::current_exe()
        .unwrap_or_default()
        .parent()
        .unwrap_or(std::path::Path::new("."))
        .join("superwarp_gui.json")
}

impl Config {
    fn load() -> Self {
        match fs::read_to_string(config_path()) {
            Ok(data) => serde_json::from_str(&data).unwrap_or_default(),
            Err(_) => Self::default(),
        }
    }
    fn save(&mut self) {
        self.dirty = false;
        if let Ok(json) = serde_json::to_string_pretty(self) {
            let _ = fs::write(config_path(), json);
        }
    }
    fn mark_dirty(&mut self) {
        self.dirty = true;
    }
}

// =========================================================================
// Cached unlock state per system
// =========================================================================

#[derive(Default, Clone)]
struct SystemUnlocks {
    partial: bool,
    updated: i64,
    currency: Option<Currency>,
    dests: HashMap<String, bool>,
}

// Destination key shared by the catalogue and the unlock cache (mirrors the
// addon: zone, or zone + RS + sub).
fn destkey(zone: &str, sub: &Option<String>) -> String {
    match sub {
        Some(s) => format!("{}\u{1e}{}", zone, s),
        None => zone.to_string(),
    }
}

impl SystemUnlocks {
    fn from_msg(partial: bool, updated: i64, currency: Option<Currency>, dests: &[UnlockDest]) -> Self {
        let mut map = HashMap::new();
        for d in dests {
            map.insert(destkey(&d.zone, &d.sub), d.unlocked);
        }
        SystemUnlocks {
            partial,
            updated,
            currency,
            dests: map,
        }
    }
}

// =========================================================================
// App + connection
// =========================================================================

struct Connection {
    reader: BufReader<TcpStream>,
    writer: TcpStream,
}

struct WarpApp {
    // network
    conn: Option<Connection>,
    connect_error: String,
    last_connect_attempt: Option<Instant>,
    pending_conn: Option<mpsc::Receiver<Result<TcpStream, String>>>,

    // state from addon
    logged_in: bool,
    character: String,
    addon_version: String,
    zone: String,
    zone_id: u32,
    busy: bool,

    // catalogue + unlocks
    systems: Vec<SystemList>,
    unlocks: HashMap<String, SystemUnlocks>,

    // ui
    selected_system: Option<String>,
    search: String,
    log_lines: Vec<(String, String)>,
    config: Config,
    always_on_top: bool,
    last_save: Instant,
}

impl Default for WarpApp {
    fn default() -> Self {
        Self {
            conn: None,
            connect_error: String::new(),
            last_connect_attempt: None,
            pending_conn: None,
            logged_in: false,
            character: String::new(),
            addon_version: String::new(),
            zone: String::new(),
            zone_id: 0,
            busy: false,
            systems: Vec::new(),
            unlocks: HashMap::new(),
            selected_system: None,
            search: String::new(),
            log_lines: Vec::new(),
            config: Config::default(),
            always_on_top: false,
            last_save: Instant::now(),
        }
    }
}

impl WarpApp {
    fn log(&mut self, level: &str, text: &str) {
        self.log_lines.push((level.to_string(), text.to_string()));
        if self.log_lines.len() > 200 {
            let excess = self.log_lines.len() - 200;
            self.log_lines.drain(0..excess);
        }
    }

    // ---- network -------------------------------------------------------

    fn try_connect(&mut self) {
        if self.pending_conn.is_some() {
            return;
        }
        self.last_connect_attempt = Some(Instant::now());
        let (tx, rx) = mpsc::channel();
        self.pending_conn = Some(rx);
        std::thread::spawn(move || {
            let result = TcpStream::connect_timeout(
                &"127.0.0.1:19519".parse().unwrap(),
                Duration::from_secs(1),
            );
            let _ = tx.send(result.map_err(|e| format!("{}", e)));
        });
    }

    fn check_pending_connect(&mut self) {
        let rx = match &self.pending_conn {
            Some(rx) => rx,
            None => return,
        };
        match rx.try_recv() {
            Ok(Ok(stream)) => {
                stream.set_nonblocking(true).ok();
                let reader = BufReader::new(stream.try_clone().unwrap());
                self.conn = Some(Connection {
                    reader,
                    writer: stream,
                });
                self.connect_error.clear();
                self.pending_conn = None;
                self.log("info", "Connected to superwarp addon");
                // The addon sends state, lists, and unlocks_all on connect, but
                // ask explicitly too so a reconnect refreshes everything.
                self.send(r#"{"cmd":"get_state"}"#);
                self.send(r#"{"cmd":"get_lists"}"#);
                self.send(r#"{"cmd":"get_unlocks"}"#);
            }
            Ok(Err(e)) => {
                self.connect_error = format!("Connection failed: {}", e);
                self.pending_conn = None;
            }
            Err(mpsc::TryRecvError::Empty) => {}
            Err(mpsc::TryRecvError::Disconnected) => {
                self.connect_error = "Connect thread died".into();
                self.pending_conn = None;
            }
        }
    }

    fn send(&mut self, json_line: &str) {
        if let Some(conn) = &mut self.conn {
            let msg = format!("{}\n", json_line);
            if conn.writer.write_all(msg.as_bytes()).is_err() {
                self.conn = None;
                self.connect_error = "Disconnected".into();
            }
        }
    }

    fn warp_to(&mut self, system: &str, zone: &str, sub: &Option<String>) {
        let cmd = match sub {
            Some(s) => serde_json::json!({"cmd":"warp_to","system":system,"zone":zone,"sub":s}),
            None => serde_json::json!({"cmd":"warp_to","system":system,"zone":zone}),
        };
        self.send(&cmd.to_string());
    }

    fn scan(&mut self, system: &str) {
        let cmd = serde_json::json!({"cmd":"scan","system":system});
        self.send(&cmd.to_string());
    }

    fn poll_network(&mut self) {
        self.check_pending_connect();

        if self.conn.is_none() && self.pending_conn.is_none() {
            match self.last_connect_attempt {
                Some(last) if last.elapsed() > Duration::from_secs(3) => self.try_connect(),
                None => self.try_connect(),
                _ => {}
            }
        }

        let mut messages = Vec::new();
        if let Some(conn) = &mut self.conn {
            let mut line = String::new();
            loop {
                line.clear();
                match conn.reader.read_line(&mut line) {
                    Ok(0) => {
                        self.conn = None;
                        self.connect_error = "Disconnected".into();
                        break;
                    }
                    Ok(_) => {
                        if line.len() > 65536 {
                            continue;
                        }
                        let trimmed = line.trim();
                        if !trimmed.is_empty() {
                            if let Ok(msg) = serde_json::from_str::<ServerMsg>(trimmed) {
                                messages.push(msg);
                            }
                        }
                    }
                    Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                    Err(_) => {
                        self.conn = None;
                        self.connect_error = "Read error".into();
                        break;
                    }
                }
            }
        }

        for msg in messages {
            self.handle_msg(msg);
        }
    }

    fn handle_msg(&mut self, msg: ServerMsg) {
        match msg {
            ServerMsg::State {
                logged_in,
                character,
                version,
                zone,
                zone_id,
                busy,
            } => {
                self.logged_in = logged_in;
                self.character = character;
                self.addon_version = version;
                self.zone = zone;
                self.zone_id = zone_id;
                self.busy = busy;
            }
            ServerMsg::Lists { systems } => {
                self.systems = systems;
                // Default selection: restore last, else first system.
                if self.selected_system.is_none() {
                    self.selected_system = self
                        .config
                        .last_system
                        .clone()
                        .filter(|s| self.systems.iter().any(|sys| &sys.key == s))
                        .or_else(|| self.systems.first().map(|s| s.key.clone()));
                }
            }
            ServerMsg::Unlocks {
                system,
                updated,
                partial,
                currency,
                destinations,
            } => {
                // A per-system delta merges into the existing cache so zone-gated
                // systems accumulate across captures.
                let entry = self.unlocks.entry(system).or_default();
                entry.partial = partial;
                entry.updated = updated;
                if currency.is_some() {
                    entry.currency = currency;
                }
                for d in &destinations {
                    entry.dests.insert(destkey(&d.zone, &d.sub), d.unlocked);
                }
            }
            ServerMsg::UnlocksAll { systems } => {
                for s in systems {
                    let su = SystemUnlocks::from_msg(s.partial, s.updated, s.currency, &s.destinations);
                    self.unlocks.insert(s.system, su);
                }
            }
            ServerMsg::Message { text, level } => {
                self.log(&level, &text);
            }
        }
    }

    // ---- unlock helpers ------------------------------------------------

    fn dest_unlocked(&self, system: &str, dest: &Dest) -> Option<bool> {
        self.unlocks
            .get(system)
            .and_then(|su| su.dests.get(&destkey(&dest.zone, &dest.sub)).copied())
    }

    fn system_currency(&self, system: &str) -> Option<&Currency> {
        self.unlocks.get(system).and_then(|su| su.currency.as_ref())
    }

    fn system_partial(&self, system: &str) -> bool {
        self.unlocks.get(system).map(|su| su.partial).unwrap_or(false)
    }

    fn system_updated(&self, system: &str) -> Option<i64> {
        self.unlocks
            .get(system)
            .map(|su| su.updated)
            .filter(|u| *u > 0)
    }

    // unlocked / total counted only over destinations we have state for.
    fn unlock_counts(&self, sys: &SystemList) -> Option<(usize, usize)> {
        let su = self.unlocks.get(&sys.key)?;
        if su.dests.is_empty() {
            return None;
        }
        let mut known = 0usize;
        let mut open = 0usize;
        for d in &sys.destinations {
            if let Some(u) = su.dests.get(&destkey(&d.zone, &d.sub)) {
                known += 1;
                if *u {
                    open += 1;
                }
            }
        }
        if known == 0 {
            None
        } else {
            Some((open, known))
        }
    }
}

// =========================================================================
// Theme
// =========================================================================

fn setup_aether_theme(ctx: &egui::Context) {
    let mut style = (*ctx.style()).clone();
    style.visuals.dark_mode = true;
    style.visuals.panel_fill = BG;
    style.visuals.window_fill = BG;
    style.visuals.extreme_bg_color = INPUT;
    style.visuals.faint_bg_color = PANEL;

    style.visuals.widgets.noninteractive.bg_fill = PANEL;
    style.visuals.widgets.noninteractive.fg_stroke = Stroke::new(1.0, TEXT);
    style.visuals.widgets.noninteractive.corner_radius = CornerRadius::same(3);

    style.visuals.widgets.inactive.bg_fill = PANEL;
    style.visuals.widgets.inactive.fg_stroke = Stroke::new(1.0, TEXT);
    style.visuals.widgets.inactive.corner_radius = CornerRadius::same(3);

    style.visuals.widgets.hovered.bg_fill = HOVER;
    style.visuals.widgets.hovered.fg_stroke = Stroke::new(1.0, WHITE);
    style.visuals.widgets.hovered.corner_radius = CornerRadius::same(3);

    style.visuals.widgets.active.bg_fill = AZURE_DIM;
    style.visuals.widgets.active.fg_stroke = Stroke::new(1.0, WHITE);

    style.visuals.selection.bg_fill = AZURE_DIM;
    style.visuals.selection.stroke = Stroke::new(1.0, AZURE);
    style.visuals.window_stroke = Stroke::new(1.0, AZURE_DIM);

    style.spacing.item_spacing = Vec2::new(8.0, 5.0);
    ctx.set_style(style);
}

// =========================================================================
// UI
// =========================================================================

impl WarpApp {
    fn render_header(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.label(RichText::new("»").font(FontId::monospace(16.0)).color(AZURE));
            ui.label(
                RichText::new("superwarp")
                    .font(FontId::monospace(16.0))
                    .color(WHITE)
                    .strong(),
            );
            let ver = if self.addon_version.is_empty() {
                "...".to_string()
            } else {
                self.addon_version.clone()
            };
            ui.label(
                RichText::new(format!("v{}", ver))
                    .font(FontId::monospace(10.0))
                    .color(MUTED),
            );
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let mut aot = self.always_on_top;
                if ui
                    .checkbox(
                        &mut aot,
                        RichText::new("pin").color(MUTED).font(FontId::monospace(9.0)),
                    )
                    .changed()
                {
                    self.always_on_top = aot;
                    self.config.always_on_top = aot;
                    self.config.mark_dirty();
                    ui.ctx().send_viewport_cmd(egui::ViewportCommand::WindowLevel(
                        if aot {
                            egui::WindowLevel::AlwaysOnTop
                        } else {
                            egui::WindowLevel::Normal
                        },
                    ));
                }
                if self.conn.is_some() {
                    ui.label(
                        RichText::new("● connected")
                            .color(AZURE)
                            .font(FontId::monospace(10.0)),
                    );
                } else {
                    ui.label(
                        RichText::new("● offline")
                            .color(LOCKED)
                            .font(FontId::monospace(10.0)),
                    );
                }
            });
        });

        // Accent line
        let rect = ui.available_rect_before_wrap();
        let line = egui::Rect::from_min_size(
            egui::pos2(rect.min.x, ui.min_rect().max.y),
            Vec2::new(rect.width(), 2.0),
        );
        ui.painter().rect_filled(line, 0.0, AZURE);
        ui.add_space(5.0);

        // Character / zone / busy
        ui.horizontal(|ui| {
            if self.character.is_empty() {
                ui.label(
                    RichText::new("not logged in")
                        .color(MUTED)
                        .font(FontId::monospace(11.0)),
                );
            } else {
                ui.label(
                    RichText::new(&self.character)
                        .color(VIOLET)
                        .strong()
                        .font(FontId::monospace(12.0)),
                );
                ui.label(
                    RichText::new(format!("@ {} ({})", self.zone, self.zone_id))
                        .color(TEXT)
                        .font(FontId::monospace(11.0)),
                );
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if self.busy {
                    if ui
                        .small_button(RichText::new("cancel").color(LOCKED))
                        .clicked()
                    {
                        self.send(r#"{"cmd":"cancel"}"#);
                    }
                    ui.label(
                        RichText::new("BUSY")
                            .color(GOLD)
                            .strong()
                            .font(FontId::monospace(11.0)),
                    );
                }
            });
        });
    }

    fn render_systems(&mut self, ui: &mut egui::Ui) {
        ui.label(
            RichText::new("SYSTEMS")
                .color(MUTED)
                .font(FontId::monospace(10.0)),
        );
        ui.add_space(2.0);
        egui::ScrollArea::vertical()
            .auto_shrink([false; 2])
            .show(ui, |ui| {
                // clone keys/labels we need to avoid borrow conflicts on click
                let rows: Vec<(String, String, Option<(usize, usize)>, bool, u32)> = self
                    .systems
                    .iter()
                    .map(|s| {
                        let label = if s.long.is_empty() {
                            s.key.clone()
                        } else {
                            s.long.clone()
                        };
                        (
                            s.key.clone(),
                            label,
                            self.unlock_counts(s),
                            self.system_partial(&s.key),
                            s.count,
                        )
                    })
                    .collect();

                for (key, label, counts, partial, total) in rows {
                    let selected = self.selected_system.as_deref() == Some(key.as_str());
                    let resp = ui.selectable_label(
                        selected,
                        RichText::new(&label)
                            .color(if selected { WHITE } else { TEXT })
                            .font(FontId::monospace(12.0)),
                    );
                    match counts {
                        Some((open, known)) => {
                            let col = if open == known { UNLOCKED } else { MUTED };
                            let suffix = if partial { " ~partial" } else { "" };
                            ui.label(
                                RichText::new(format!("   {}/{} unlocked{}", open, known, suffix))
                                    .color(col)
                                    .font(FontId::monospace(9.0)),
                            );
                        }
                        None => {
                            if total > 0 {
                                ui.label(
                                    RichText::new(format!("   {} destinations", total))
                                        .color(MUTED)
                                        .font(FontId::monospace(9.0)),
                                );
                            }
                        }
                    }
                    if resp.clicked() {
                        self.selected_system = Some(key.clone());
                        self.config.last_system = Some(key.clone());
                        self.config.mark_dirty();
                    }
                }
            });
    }

    fn render_destinations(&mut self, ui: &mut egui::Ui) {
        let system = match self.selected_system.clone() {
            Some(s) => s,
            None => {
                ui.label(
                    RichText::new("Select a system on the left.")
                        .color(MUTED)
                        .font(FontId::monospace(12.0)),
                );
                return;
            }
        };

        // header row: system name, scan, currency
        let (long, short, plural) = self
            .systems
            .iter()
            .find(|s| s.key == system)
            .map(|s| (s.long.clone(), s.short.clone(), s.plural.clone()))
            .unwrap_or((system.clone(), String::new(), String::new()));

        ui.horizontal(|ui| {
            ui.label(
                RichText::new(&long)
                    .color(WHITE)
                    .strong()
                    .font(FontId::monospace(14.0)),
            );
            if !short.is_empty() {
                ui.label(
                    RichText::new(format!("[{}]", short))
                        .color(VIOLET)
                        .font(FontId::monospace(11.0)),
                );
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let scan_btn = egui::Button::new(
                    RichText::new("scan").color(WHITE).font(FontId::monospace(11.0)),
                )
                .fill(AZURE_DIM)
                .corner_radius(CornerRadius::same(3));
                if ui.add_enabled(self.conn.is_some() && !self.busy, scan_btn).clicked() {
                    self.scan(&system);
                }
            });
        });

        // metadata line: currency (left) and capture freshness (right). Kept off
        // the title row so a wide balance (e.g. nine-figure gil) can't collide with
        // the freshness text.
        let updated = self.system_updated(&system);
        let currency_txt = self
            .system_currency(&system)
            .map(|c| format!("{}: {}", c.name, fmt_thousands(c.value)));
        if currency_txt.is_some() || updated.is_some() {
            ui.horizontal(|ui| {
                if let Some(txt) = &currency_txt {
                    ui.label(RichText::new(txt).color(GOLD).font(FontId::monospace(11.0)));
                }
                if let Some(u) = updated {
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        ui.label(
                            RichText::new(format!("scanned {}", time_ago(u)))
                                .color(MUTED)
                                .font(FontId::monospace(10.0)),
                        );
                    });
                }
            });
        }

        // search (global across all systems)
        let search = egui::TextEdit::singleline(&mut self.search)
            .hint_text(RichText::new("search all systems...").color(MUTED))
            .desired_width(f32::INFINITY)
            .font(FontId::monospace(12.0));
        ui.add(search);
        let _ = plural;
        ui.add_space(4.0);

        // accent separator
        let rect = ui.available_rect_before_wrap();
        ui.painter().rect_filled(
            egui::Rect::from_min_size(rect.min, Vec2::new(rect.width(), 1.0)),
            0.0,
            PANEL,
        );
        ui.add_space(4.0);

        // Build the rows to show. With an empty search we browse the selected
        // system (no system tag). With a search we match across every system and
        // tag each row with the system it belongs to.
        let needle = self.search.to_lowercase();
        let global = !needle.is_empty();

        // rows: (system_key, system_long, Dest)
        let mut rows: Vec<(String, String, Dest)> = Vec::new();
        if global {
            for s in &self.systems {
                let long = if s.long.is_empty() { s.key.clone() } else { s.long.clone() };
                for d in &s.destinations {
                    if dest_matches(d, &needle) {
                        rows.push((s.key.clone(), long.clone(), d.clone()));
                    }
                }
            }
        } else if let Some(s) = self.systems.iter().find(|s| s.key == system) {
            let long = if s.long.is_empty() { s.key.clone() } else { s.long.clone() };
            for d in &s.destinations {
                rows.push((s.key.clone(), long.clone(), d.clone()));
            }
        }

        // Sort by zone, then sub (numeric-aware), then system -- so a zone's
        // points cluster and number in order, and same-named zones from different
        // systems sit together under a search.
        rows.sort_by(|(ak, _, a), (bk, _, b)| {
            a.zone
                .to_lowercase()
                .cmp(&b.zone.to_lowercase())
                .then_with(|| sub_order(&a.sub).cmp(&sub_order(&b.sub)))
                .then_with(|| sub_text(&a.sub).cmp(&sub_text(&b.sub)))
                .then_with(|| ak.cmp(bk))
        });

        let can_warp = self.conn.is_some() && !self.busy;

        if rows.is_empty() {
            ui.label(
                RichText::new(if global {
                    "No matches."
                } else {
                    "No destinations."
                })
                .color(MUTED)
                .font(FontId::monospace(12.0)),
            );
            return;
        }

        egui::ScrollArea::vertical()
            .auto_shrink([false; 2])
            .show(ui, |ui| {
                for (sys, sys_long, d) in &rows {
                    let unlocked = self.dest_unlocked(sys, d);
                    let afford = match (d.cost, self.system_currency(sys)) {
                        (Some(cost), Some(c)) => Some(c.value >= cost),
                        _ => None,
                    };

                    ui.horizontal(|ui| {
                        // lock glyph
                        let (glyph, gcol) = match unlocked {
                            Some(true) => ("●", UNLOCKED),
                            Some(false) => ("○", LOCKED),
                            None => ("·", MUTED),
                        };
                        ui.label(RichText::new(glyph).color(gcol).font(FontId::monospace(12.0)));

                        // label (click to warp): "Zone - point" for nested, plain
                        // zone label for flat entries.
                        let display = match &d.sub {
                            Some(_) => format!("{} - {}", d.zone, d.label),
                            None => d.label.clone(),
                        };
                        let label_col = match unlocked {
                            Some(false) => MUTED,
                            _ => TEXT,
                        };
                        let resp = ui.add_enabled(
                            can_warp,
                            egui::Button::new(
                                RichText::new(&display)
                                    .color(label_col)
                                    .font(FontId::monospace(12.0)),
                            )
                            .fill(Color32::TRANSPARENT)
                            .corner_radius(CornerRadius::same(3)),
                        );
                        if resp.clicked() {
                            self.warp_to(sys, &d.zone, &d.sub);
                            self.log("info", &format!("-> warp {} / {}", sys, display));
                        }

                        // right side: system tag (only while searching) then cost
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if global {
                                ui.label(
                                    RichText::new(format!("[{}]", sys_long))
                                        .color(VIOLET)
                                        .font(FontId::monospace(10.0)),
                                );
                            }
                            if let Some(cost) = d.cost {
                                let col = match afford {
                                    Some(false) => LOCKED,
                                    _ => MUTED,
                                };
                                ui.label(
                                    RichText::new(fmt_thousands(cost))
                                        .color(col)
                                        .font(FontId::monospace(10.0)),
                                );
                            }
                        });
                    });
                }
            });
    }

    fn render_footer(&mut self, ui: &mut egui::Ui) {
        // last few log lines
        let recent: Vec<(String, String)> = self
            .log_lines
            .iter()
            .rev()
            .take(3)
            .cloned()
            .collect::<Vec<_>>()
            .into_iter()
            .rev()
            .collect();
        for (level, text) in recent {
            let col = match level.as_str() {
                "error" => LOCKED,
                "warning" => GOLD,
                _ => MUTED,
            };
            ui.label(RichText::new(text).color(col).font(FontId::monospace(10.0)));
        }
        if !self.connect_error.is_empty() && self.conn.is_none() {
            ui.label(
                RichText::new(&self.connect_error)
                    .color(MUTED)
                    .font(FontId::monospace(9.0)),
            );
        }
    }
}

fn dest_matches(d: &Dest, needle: &str) -> bool {
    if d.label.to_lowercase().contains(needle) {
        return true;
    }
    if d.zone.to_lowercase().contains(needle) {
        return true;
    }
    d.aliases.iter().any(|a| a.to_lowercase().contains(needle))
}

// numeric / text sort keys for a destination's sub, so "1,2,...,10" order
// naturally and named subs fall back to text. Numeric subs sort before named.
fn sub_order(sub: &Option<String>) -> (u8, u64) {
    match sub {
        None => (0, 0),
        Some(s) => match s.parse::<u64>() {
            Ok(n) => (1, n),
            Err(_) => (2, 0),
        },
    }
}

fn sub_text(sub: &Option<String>) -> String {
    sub.clone().unwrap_or_default().to_lowercase()
}

fn now_unix() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

fn time_ago(updated: i64) -> String {
    let secs = (now_unix() - updated).max(0);
    if secs < 10 {
        "just now".to_string()
    } else if secs < 60 {
        format!("{}s ago", secs)
    } else if secs < 3600 {
        format!("{}m ago", secs / 60)
    } else if secs < 86400 {
        format!("{}h ago", secs / 3600)
    } else {
        format!("{}d ago", secs / 86400)
    }
}

fn fmt_thousands(n: i64) -> String {
    let neg = n < 0;
    let mut s = n.abs().to_string();
    let mut out = String::new();
    while s.len() > 3 {
        let split = s.len() - 3;
        out = format!(",{}{}", &s[split..], out);
        s.truncate(split);
    }
    out = format!("{}{}", s, out);
    if neg {
        format!("-{}", out)
    } else {
        out
    }
}

// =========================================================================
// eframe::App
// =========================================================================

impl eframe::App for WarpApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.poll_network();

        // track geometry
        if let Some(outer) = ctx.input(|i| i.viewport().outer_rect) {
            if self.config.window_x != Some(outer.min.x) || self.config.window_y != Some(outer.min.y) {
                self.config.window_x = Some(outer.min.x);
                self.config.window_y = Some(outer.min.y);
                self.config.mark_dirty();
            }
        }
        if let Some(inner) = ctx.input(|i| i.viewport().inner_rect) {
            if self.config.window_w != Some(inner.width()) || self.config.window_h != Some(inner.height()) {
                self.config.window_w = Some(inner.width());
                self.config.window_h = Some(inner.height());
                self.config.mark_dirty();
            }
        }

        // dynamic title
        let title = if self.character.is_empty() {
            format!("superwarp — FFXI  (GUI v{})", GUI_VERSION)
        } else {
            let ver = if self.addon_version.is_empty() {
                "..."
            } else {
                &self.addon_version
            };
            format!("{} — superwarp v{}  (GUI v{})", self.character, ver, GUI_VERSION)
        };
        ctx.send_viewport_cmd(egui::ViewportCommand::Title(title));

        if self.config.dirty && self.last_save.elapsed() > Duration::from_secs(3) {
            self.config.save();
            self.last_save = Instant::now();
        }

        ctx.request_repaint_after(Duration::from_millis(120));

        egui::TopBottomPanel::top("header")
            .frame(egui::Frame::NONE.fill(BG).inner_margin(Margin::symmetric(12, 8)))
            .show(ctx, |ui| self.render_header(ui));

        egui::TopBottomPanel::bottom("footer")
            .frame(egui::Frame::NONE.fill(INPUT).inner_margin(Margin::symmetric(12, 6)))
            .show(ctx, |ui| self.render_footer(ui));

        egui::SidePanel::left("systems")
            .resizable(true)
            .default_width(190.0)
            .frame(egui::Frame::NONE.fill(BG).inner_margin(Margin::symmetric(10, 8)))
            .show(ctx, |ui| self.render_systems(ui));

        egui::CentralPanel::default()
            .frame(egui::Frame::NONE.fill(BG).inner_margin(Margin::symmetric(12, 8)))
            .show(ctx, |ui| self.render_destinations(ui));
    }
}

// =========================================================================
// main
// =========================================================================

fn main() -> eframe::Result<()> {
    let cfg = Config::load();

    let mut viewport = egui::ViewportBuilder::default()
        .with_min_inner_size([520.0, 420.0])
        .with_title("superwarp — FFXI");

    if let (Some(w), Some(h)) = (cfg.window_w, cfg.window_h) {
        viewport = viewport.with_inner_size([w, h]);
    } else {
        viewport = viewport.with_inner_size([720.0, 620.0]);
    }
    if let (Some(x), Some(y)) = (cfg.window_x, cfg.window_y) {
        viewport = viewport.with_position([x, y]);
    }
    if cfg.always_on_top {
        viewport = viewport.with_always_on_top();
    }

    let options = eframe::NativeOptions {
        viewport,
        ..Default::default()
    };

    eframe::run_native(
        "superwarp GUI",
        options,
        Box::new(move |cc| {
            setup_aether_theme(&cc.egui_ctx);
            let mut app = WarpApp::default();
            app.always_on_top = cfg.always_on_top;
            app.config = cfg;
            Ok(Box::new(app))
        }),
    )
}
