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
use std::net::{TcpStream, ToSocketAddrs};
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
const PARTY_COL: Color32 = Color32::from_rgb(63, 200, 224); // party = azure
const ALL_COL: Color32 = Color32::from_rgb(232, 138, 96); // all = warn coral

// Where a warp / sub-command is sent: just this character, the party, or every
// connected character. Mirrors superwarp's all/party tokens.
#[derive(Debug, Clone, Copy, PartialEq)]
enum Dispatch {
    Off,
    Party,
    All,
}

impl Dispatch {
    fn next(self) -> Self {
        match self {
            Dispatch::Off => Dispatch::Party,
            Dispatch::Party => Dispatch::All,
            Dispatch::All => Dispatch::Off,
        }
    }
    // the addon's dispatch token, or None for single-character.
    fn token(self) -> Option<&'static str> {
        match self {
            Dispatch::Off => None,
            Dispatch::Party => Some("party"),
            Dispatch::All => Some("all"),
        }
    }
    fn label(self) -> &'static str {
        match self {
            Dispatch::Off => "Send: Off",
            Dispatch::Party => "Send: Party",
            Dispatch::All => "Send: ALL",
        }
    }
    fn color(self) -> Color32 {
        match self {
            Dispatch::Off => MUTED,
            Dispatch::Party => PARTY_COL,
            Dispatch::All => ALL_COL,
        }
    }
}

const GUI_VERSION: &str = "0.9.2";

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
    #[serde(default)]
    subcommands: Vec<String>,
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
        #[serde(default)]
        debug: bool,
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

#[derive(Clone, Serialize, Deserialize)]
struct Config {
    window_x: Option<f32>,
    window_y: Option<f32>,
    window_w: Option<f32>,
    window_h: Option<f32>,
    #[serde(default)]
    always_on_top: bool,
    #[serde(default)]
    last_system: Option<String>,
    #[serde(default)]
    system_order: Vec<String>,
    #[serde(default = "default_host")]
    host: String,
    #[serde(default = "default_port")]
    port: u16,
    #[serde(skip)]
    dirty: bool,
}

fn default_host() -> String {
    "127.0.0.1".to_string()
}
fn default_port() -> u16 {
    19519
}

impl Default for Config {
    fn default() -> Self {
        Self {
            window_x: None,
            window_y: None,
            window_w: None,
            window_h: None,
            always_on_top: false,
            last_system: None,
            system_order: Vec::new(),
            host: default_host(),
            port: default_port(),
            dirty: false,
        }
    }
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

#[derive(Clone, Copy, PartialEq)]
enum View {
    Warp,
    Advanced,
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
    addon_debug: bool,

    // catalogue + unlocks
    systems: Vec<SystemList>,
    unlocks: HashMap<String, SystemUnlocks>,

    // ui
    selected_system: Option<String>,
    search: String,
    dispatch: Dispatch,
    drag_system: Option<String>,
    drag_grab_offset: f32,
    clear_search_pending: bool,
    log_lines: Vec<(String, String)>,
    view: View,
    host_draft: String,
    port_draft: String,
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
            addon_debug: false,
            systems: Vec::new(),
            unlocks: HashMap::new(),
            selected_system: None,
            search: String::new(),
            dispatch: Dispatch::Off,
            drag_system: None,
            drag_grab_offset: 0.0,
            clear_search_pending: false,
            log_lines: Vec::new(),
            view: View::Warp,
            host_draft: String::new(),
            port_draft: String::new(),
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
        let addr = format!("{}:{}", self.config.host, self.config.port);
        let sockaddr = match addr.to_socket_addrs().ok().and_then(|mut it| it.next()) {
            Some(a) => a,
            None => {
                self.connect_error = format!("Invalid address: {}", addr);
                self.pending_conn = None;
                return;
            }
        };
        let (tx, rx) = mpsc::channel();
        self.pending_conn = Some(rx);
        std::thread::spawn(move || {
            let result = TcpStream::connect_timeout(&sockaddr, Duration::from_secs(1));
            let _ = tx.send(result.map_err(|e| format!("{}", e)));
        });
    }

    // Tear down the current link so poll_network reconnects (to the current
    // host:port) on the next frame. Used after changing the address.
    fn reconnect(&mut self) {
        self.conn = None;
        self.pending_conn = None;
        self.connect_error.clear();
        self.last_connect_attempt = None;
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
        let mut cmd = match sub {
            Some(s) => serde_json::json!({"cmd":"warp_to","system":system,"zone":zone,"sub":s}),
            None => serde_json::json!({"cmd":"warp_to","system":system,"zone":zone}),
        };
        if let Some(tok) = self.dispatch.token() {
            cmd["dispatch"] = serde_json::Value::String(tok.to_string());
        }
        self.send(&cmd.to_string());
    }

    fn sub_cmd(&mut self, system: &str, sub: &str) {
        let mut cmd = serde_json::json!({"cmd":"sub_cmd","system":system,"sub":sub});
        if let Some(tok) = self.dispatch.token() {
            cmd["dispatch"] = serde_json::Value::String(tok.to_string());
        }
        self.send(&cmd.to_string());
    }

    // cancel/reset, honoring the dispatch toggle (cancel all / cancel party).
    fn cancel(&mut self) {
        let mut cmd = serde_json::json!({"cmd":"cancel"});
        if let Some(tok) = self.dispatch.token() {
            cmd["dispatch"] = serde_json::Value::String(tok.to_string());
        }
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
                debug,
            } => {
                self.logged_in = logged_in;
                self.character = character;
                self.addon_version = version;
                self.zone = zone;
                self.zone_id = zone_id;
                self.busy = busy;
                self.addon_debug = debug;
                // A queued search-result warp has completed once the addon reports
                // not-busy (it broadcasts state when current_activity clears).
                if self.clear_search_pending && !busy {
                    self.search.clear();
                    self.clear_search_pending = false;
                }
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
            // painted multicolor double-chevron (matches the app icon): violet
            // back + azure front, sharp ~60-degree angle. A text "»" can't be
            // two-toned, so we paint the geometry directly.
            {
                let h = 18.0_f32;
                let (rect, _) =
                    ui.allocate_exact_size(Vec2::new(h * 1.15, h), egui::Sense::hover());
                let p = ui.painter();
                let cy = rect.center().y;
                let half = 0.30 * h; // vertical half-span
                let dx = 0.50 * h; // arm reach; larger relative to half = sharper
                let sw = (0.17 * h).max(2.0);
                let gap = 0.32 * h; // horizontal offset between the two chevrons
                let lx0 = rect.left() + sw * 0.5;
                let chev = |lx: f32, color: Color32| {
                    let top = egui::pos2(lx, cy - half);
                    let apex = egui::pos2(lx + dx, cy);
                    let bot = egui::pos2(lx, cy + half);
                    p.line_segment([top, apex], Stroke::new(sw, color));
                    p.line_segment([apex, bot], Stroke::new(sw, color));
                    let cap = sw * 0.5;
                    p.circle_filled(top, cap, color);
                    p.circle_filled(apex, cap, color);
                    p.circle_filled(bot, cap, color);
                };
                chev(lx0, VIOLET);
                chev(lx0 + gap, AZURE);
            }
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
                let adv = self.view == View::Advanced;
                let toggle = egui::Button::new(
                    RichText::new(if adv { "warp" } else { "advanced" })
                        .color(if adv { AZURE } else { MUTED })
                        .font(FontId::monospace(10.0)),
                )
                .fill(PANEL)
                .corner_radius(CornerRadius::same(3));
                if ui.add(toggle).clicked() {
                    self.view = if adv { View::Warp } else { View::Advanced };
                    if self.view == View::Advanced {
                        self.host_draft = self.config.host.clone();
                        self.port_draft = self.config.port.to_string();
                    }
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
                        self.cancel();
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

    // The sidebar order: the persisted custom order, minus any systems no longer
    // present, with any not-yet-ordered systems (new catalogue entries, freshly
    // scanned systems) appended at the bottom so nothing ever silently vanishes.
    fn effective_order(&self) -> Vec<String> {
        let mut order: Vec<String> = self
            .config
            .system_order
            .iter()
            .filter(|k| self.systems.iter().any(|s| &s.key == *k))
            .cloned()
            .collect();
        for s in &self.systems {
            if !order.iter().any(|k| k == &s.key) {
                order.push(s.key.clone());
            }
        }
        order
    }

    fn render_systems(&mut self, ui: &mut egui::Ui) {
        ui.label(
            RichText::new("SYSTEMS")
                .color(MUTED)
                .font(FontId::monospace(10.0)),
        );
        ui.add_space(2.0);

        // Build the ordered, owned row data up front. This ends the immutable
        // borrow of self.systems before we take &mut self for clicks/drags.
        let order = self.effective_order();
        let rows: Vec<(String, String, Option<(usize, usize)>, bool, u32)> = order
            .iter()
            .filter_map(|key| {
                self.systems.iter().find(|s| &s.key == key).map(|s| {
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
            })
            .collect();

        const ROW_H: f32 = 34.0;
        const PAD_X: f32 = 5.0;
        const ANIM: f32 = 0.13; // slide/settle easing time (seconds) -- tune to taste
        let n = rows.len();
        if n == 0 {
            ui.label(
                RichText::new("waiting for catalogue...")
                    .color(MUTED)
                    .font(FontId::monospace(11.0)),
            );
            return;
        }

        egui::ScrollArea::vertical()
            .auto_shrink([false; 2])
            .show(ui, |ui| {
                let ctx = ui.ctx().clone();
                let dragging = self.drag_system.clone();
                let avail_w = ui.available_width();
                // One fixed-height slot per system, so the list has stable
                // geometry to animate within and the ScrollArea sizes correctly.
                let (area, _) = ui.allocate_exact_size(
                    Vec2::new(avail_w, n as f32 * ROW_H),
                    egui::Sense::hover(),
                );
                let top = area.top();
                let left = area.left();
                let width = area.width();

                let pointer = ctx.input(|i| i.pointer.hover_pos());
                let released = ctx.input(|i| i.pointer.any_released());

                // Top of the floating tab (cursor minus where it was grabbed),
                // and the insertion slot that position implies.
                let ghost_y = match (&dragging, pointer) {
                    (Some(_), Some(p)) => Some(p.y - self.drag_grab_offset),
                    _ => None,
                };
                let ins = match ghost_y {
                    Some(gy) => (((gy - top) / ROW_H).round() as i64)
                        .clamp(0, (n as i64 - 1).max(0)) as usize,
                    None => 0,
                };

                // The order with the dragged key removed -- used to map every
                // other row to a slot, opening a gap at `ins`.
                let without_k: Vec<&String> = rows
                    .iter()
                    .map(|(k, ..)| k)
                    .filter(|k| dragging.as_deref() != Some((*k).as_str()))
                    .collect();

                let mut clicked: Option<String> = None;

                for (key, label, counts, partial, total) in &rows {
                    let is_drag = dragging.as_deref() == Some(key.as_str());

                    // Target slot -> y relative to the list top (relative, so
                    // scrolling the panel doesn't retrigger the animation). Each
                    // row eases toward its slot; the dragged row pins to cursor.
                    let target_rel = if is_drag {
                        ghost_y.map(|gy| gy - top).unwrap_or(0.0)
                    } else {
                        let j = without_k.iter().position(|k| *k == key).unwrap_or(0);
                        let slot = if dragging.is_some() {
                            if j < ins { j } else { j + 1 }
                        } else {
                            j
                        };
                        slot as f32 * ROW_H
                    };
                    let dur = if is_drag { 0.0 } else { ANIM };
                    let rel =
                        ctx.animate_value_with_time(egui::Id::new(("sysrow", key)), target_rel, dur);
                    let row_y = top + rel;
                    let row_rect =
                        egui::Rect::from_min_size(egui::pos2(left, row_y), Vec2::new(width, ROW_H));

                    let is_sel = self.selected_system.as_deref() == Some(key.as_str());
                    let (count_txt, count_col) = match counts {
                        Some((open, known)) => {
                            let col = if open == known { UNLOCKED } else { MUTED };
                            let suffix = if *partial { " ~partial" } else { "" };
                            (Some(format!("{}/{} unlocked{}", open, known, suffix)), col)
                        }
                        None => {
                            if *total > 0 {
                                (Some(format!("{} destinations", total)), MUTED)
                            } else {
                                (None, MUTED)
                            }
                        }
                    };

                    if is_drag {
                        // The lifted tab: foreground layer, drop shadow, follows
                        // the cursor. animate_value above is pinned to ghost_y so
                        // that on release it eases from here into its new slot.
                        let fp = ctx.layer_painter(egui::LayerId::new(
                            egui::Order::Foreground,
                            egui::Id::new("sw_sys_ghost"),
                        ));
                        let g = row_rect.shrink2(Vec2::new(1.0, 1.0));
                        fp.rect_filled(
                            g.translate(Vec2::new(3.0, 4.0)),
                            5.0,
                            Color32::from_black_alpha(110),
                        );
                        fp.rect_filled(g, 5.0, HOVER);
                        fp.text(
                            g.min + Vec2::new(PAD_X, 3.0),
                            egui::Align2::LEFT_TOP,
                            label,
                            FontId::monospace(12.0),
                            WHITE,
                        );
                        if let Some(ct) = &count_txt {
                            fp.text(
                                g.min + Vec2::new(PAD_X + 6.0, 19.0),
                                egui::Align2::LEFT_TOP,
                                ct,
                                FontId::monospace(9.0),
                                count_col,
                            );
                        }
                        continue;
                    }

                    if is_sel {
                        ui.painter()
                            .rect_filled(row_rect.shrink2(Vec2::new(0.0, 1.0)), 3.0, HOVER);
                    }
                    let name_col = if is_sel { WHITE } else { TEXT };
                    ui.painter().text(
                        egui::pos2(left + PAD_X, row_y + 3.0),
                        egui::Align2::LEFT_TOP,
                        label,
                        FontId::monospace(12.0),
                        name_col,
                    );
                    if let Some(ct) = &count_txt {
                        ui.painter().text(
                            egui::pos2(left + PAD_X + 6.0, row_y + 19.0),
                            egui::Align2::LEFT_TOP,
                            ct,
                            FontId::monospace(9.0),
                            count_col,
                        );
                    }

                    let resp = ui.interact(
                        row_rect,
                        egui::Id::new(("sysdnd", key)),
                        egui::Sense::click_and_drag(),
                    );
                    if dragging.is_none() && resp.hovered() {
                        ctx.set_cursor_icon(egui::CursorIcon::Grab);
                    }
                    if resp.drag_started() {
                        self.drag_system = Some(key.clone());
                        self.drag_grab_offset = pointer.map(|p| p.y - row_y).unwrap_or(ROW_H / 2.0);
                    }
                    if resp.clicked() {
                        clicked = Some(key.clone());
                    }
                }

                // The open gap the tab will drop into (and keep the grab cursor).
                if dragging.is_some() {
                    ctx.set_cursor_icon(egui::CursorIcon::Grabbing);
                    let gap_y = top + ins as f32 * ROW_H;
                    let gap = egui::Rect::from_min_size(
                        egui::pos2(left, gap_y),
                        Vec2::new(width, ROW_H),
                    )
                    .shrink2(Vec2::new(1.0, 2.0));
                    ui.painter().rect_filled(
                        gap,
                        4.0,
                        Color32::from_rgba_unmultiplied(AZURE.r(), AZURE.g(), AZURE.b(), 26),
                    );
                }

                if let Some(sel) = clicked {
                    self.selected_system = Some(sel.clone());
                    self.config.last_system = Some(sel);
                    self.config.mark_dirty();
                    self.view = View::Warp;
                }

                // Commit on release: splice the dragged key into slot `ins` and
                // persist. Next frame it eases from the ghost position into place.
                if let Some(drag_key) = dragging {
                    if released {
                        let mut new_order: Vec<String> =
                            rows.iter().map(|(k, ..)| k.clone()).collect();
                        if let Some(from) = new_order.iter().position(|k| k == &drag_key) {
                            let item = new_order.remove(from);
                            let to = ins.min(new_order.len());
                            new_order.insert(to, item);
                            self.config.system_order = new_order;
                            self.config.mark_dirty();
                            self.config.save();
                        }
                        self.drag_system = None;
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

                // dispatch toggle: cycles Off -> Party -> All. Drives whether
                // warps / sub-commands go to just this character, the party, or
                // every connected character.
                let dcol = self.dispatch.color();
                let dfill = if self.dispatch == Dispatch::Off {
                    PANEL
                } else {
                    Color32::from_rgba_unmultiplied(dcol.r(), dcol.g(), dcol.b(), 40)
                };
                let toggle = egui::Button::new(
                    RichText::new(self.dispatch.label())
                        .color(dcol)
                        .font(FontId::monospace(11.0)),
                )
                .fill(dfill)
                .stroke(Stroke::new(1.0, dcol))
                .corner_radius(CornerRadius::same(3));
                if ui
                    .add(toggle)
                    .on_hover_text("Send warps to: just you / your party / all your characters")
                    .clicked()
                {
                    self.dispatch = self.dispatch.next();
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
        // If the user edits the box, drop any queued auto-clear -- they're starting
        // a fresh search and we shouldn't wipe it when an earlier warp lands.
        if ui.add(search).changed() {
            self.clear_search_pending = false;
        }
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
        let needle_clean = fuzzy_clean(&self.search);
        let global = !needle_clean.is_empty();

        // ACTIONS submenu: the selected system's sub-commands (enter/exit/port/
        // set/...) plus cancel/reset. Hidden during a global search since the
        // actions target the selected system. Honors the dispatch toggle, so a
        // sub-command like "enter" can be fired for the whole party at once.
        if !global {
            let subs: Vec<String> = self
                .systems
                .iter()
                .find(|s| s.key == system)
                .map(|s| s.subcommands.clone())
                .unwrap_or_default();
            let can_act = self.conn.is_some() && !self.busy;
            let mut clicked_sub: Option<String> = None;
            let mut clicked_cancel = false;
            ui.horizontal_wrapped(|ui| {
                ui.label(
                    RichText::new("actions")
                        .color(MUTED)
                        .font(FontId::monospace(10.0)),
                );
                let cancel_chip = egui::Button::new(
                    RichText::new("cancel / reset")
                        .color(LOCKED)
                        .font(FontId::monospace(11.0)),
                )
                .fill(PANEL)
                .corner_radius(CornerRadius::same(3));
                if ui.add(cancel_chip).clicked() {
                    clicked_cancel = true;
                }
                for sc in &subs {
                    let chip = egui::Button::new(
                        RichText::new(sc).color(AZURE).font(FontId::monospace(11.0)),
                    )
                    .fill(PANEL)
                    .corner_radius(CornerRadius::same(3));
                    if ui.add_enabled(can_act, chip).clicked() {
                        clicked_sub = Some(sc.clone());
                    }
                }
            });
            if clicked_cancel {
                self.cancel();
            }
            if let Some(sc) = clicked_sub {
                self.sub_cmd(&system, &sc);
                self.log("info", &format!("-> {} {}", system, sc));
            }
            ui.add_space(4.0);
        }

        // rows: (system_key, system_long, Dest)
        let mut rows: Vec<(String, String, Dest)> = Vec::new();
        let mut approx_note = false;
        if global {
            // Fuzzy match across every system, same tiers as the addon's
            // fuzzyfind: exact -> gap-aware subsequence -> closest by edit
            // distance. Ranked best-first rather than alphabetically.
            let mut scored: Vec<(i64, String, String, Dest)> = Vec::new();
            for s in &self.systems {
                let long = if s.long.is_empty() { s.key.clone() } else { s.long.clone() };
                for d in &s.destinations {
                    if let Some(sc) = dest_fuzzy(d, &needle_clean) {
                        scored.push((sc, s.key.clone(), long.clone(), d.clone()));
                    }
                }
            }
            if scored.is_empty() {
                // approx tier: nothing was a subsequence -> show the closest by
                // normalized edit distance (graceful "did you mean", never empty).
                let mut appr: Vec<(i64, String, String, Dest)> = Vec::new();
                for s in &self.systems {
                    let long = if s.long.is_empty() { s.key.clone() } else { s.long.clone() };
                    for d in &s.destinations {
                        appr.push((
                            dest_approx(d, &needle_clean),
                            s.key.clone(),
                            long.clone(),
                            d.clone(),
                        ));
                    }
                }
                appr.sort_by(|a, b| {
                    b.0.cmp(&a.0)
                        .then_with(|| a.3.label.len().cmp(&b.3.label.len()))
                });
                appr.truncate(12);
                rows = appr.into_iter().map(|(_, k, l, d)| (k, l, d)).collect();
                approx_note = !rows.is_empty();
            } else {
                // higher score first; ties -> shorter label, then zone name
                scored.sort_by(|a, b| {
                    b.0.cmp(&a.0)
                        .then_with(|| a.3.label.len().cmp(&b.3.label.len()))
                        .then_with(|| a.3.zone.to_lowercase().cmp(&b.3.zone.to_lowercase()))
                });
                rows = scored.into_iter().map(|(_, k, l, d)| (k, l, d)).collect();
            }
        } else if let Some(s) = self.systems.iter().find(|s| s.key == system) {
            let long = if s.long.is_empty() { s.key.clone() } else { s.long.clone() };
            for d in &s.destinations {
                rows.push((s.key.clone(), long.clone(), d.clone()));
            }
            // Browse mode keeps the natural ordering: zone, then sub (numeric-
            // aware), then system -- so a zone's points cluster and number in order.
            rows.sort_by(|(ak, _, a), (bk, _, b)| {
                a.zone
                    .to_lowercase()
                    .cmp(&b.zone.to_lowercase())
                    .then_with(|| sub_order(&a.sub).cmp(&sub_order(&b.sub)))
                    .then_with(|| sub_text(&a.sub).cmp(&sub_text(&b.sub)))
                    .then_with(|| ak.cmp(bk))
            });
        }

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

        if approx_note {
            ui.label(
                RichText::new("no direct match -- closest:")
                    .color(MUTED)
                    .font(FontId::monospace(10.0)),
            );
            ui.add_space(2.0);
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
                            // Warping from a search result: clear the box once the
                            // warp completes (next not-busy state from the addon).
                            if global {
                                self.clear_search_pending = true;
                            }
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

    fn render_advanced(&mut self, ui: &mut egui::Ui) {
        ui.label(
            RichText::new("ADVANCED")
                .color(MUTED)
                .font(FontId::monospace(10.0)),
        );
        ui.add_space(6.0);

        // ---- Connection ----
        ui.label(
            RichText::new("Connection")
                .color(WHITE)
                .strong()
                .font(FontId::monospace(13.0)),
        );
        ui.add_space(4.0);
        ui.horizontal(|ui| {
            ui.label(RichText::new("host").color(MUTED).font(FontId::monospace(11.0)));
            ui.add(
                egui::TextEdit::singleline(&mut self.host_draft)
                    .desired_width(150.0)
                    .font(FontId::monospace(12.0)),
            );
            ui.add_space(8.0);
            ui.label(RichText::new("port").color(MUTED).font(FontId::monospace(11.0)));
            ui.add(
                egui::TextEdit::singleline(&mut self.port_draft)
                    .desired_width(70.0)
                    .font(FontId::monospace(12.0)),
            );
        });
        ui.add_space(4.0);
        let mut do_apply = false;
        ui.horizontal(|ui| {
            let apply = egui::Button::new(
                RichText::new("apply + reconnect")
                    .color(WHITE)
                    .font(FontId::monospace(11.0)),
            )
            .fill(PANEL)
            .corner_radius(CornerRadius::same(3));
            if ui.add(apply).clicked() {
                do_apply = true;
            }
            let reset = egui::Button::new(
                RichText::new("defaults")
                    .color(MUTED)
                    .font(FontId::monospace(11.0)),
            )
            .fill(PANEL)
            .corner_radius(CornerRadius::same(3));
            if ui.add(reset).clicked() {
                self.host_draft = default_host();
                self.port_draft = default_port().to_string();
                do_apply = true;
            }
        });
        let status = if self.conn.is_some() {
            format!("connected to {}:{}", self.config.host, self.config.port)
        } else if !self.connect_error.is_empty() {
            format!("offline -- {}", self.connect_error)
        } else {
            "offline".to_string()
        };
        let scol = if self.conn.is_some() { UNLOCKED } else { MUTED };
        ui.label(RichText::new(status).color(scol).font(FontId::monospace(10.0)));
        if do_apply {
            let host = self.host_draft.trim().to_string();
            match self.port_draft.trim().parse::<u16>() {
                Ok(p) if p != 0 && !host.is_empty() => {
                    self.config.host = host;
                    self.config.port = p;
                    self.config.mark_dirty();
                    self.config.save();
                    self.log(
                        "info",
                        &format!("reconnecting to {}:{}", self.config.host, self.config.port),
                    );
                    self.reconnect();
                }
                _ => self.log("error", "invalid host/port"),
            }
        }

        ui.add_space(10.0);

        // ---- Diagnostics ----
        ui.label(
            RichText::new("Diagnostics")
                .color(WHITE)
                .strong()
                .font(FontId::monospace(13.0)),
        );
        ui.add_space(4.0);
        // The addon's own debug mode (//sw debug), driven over the bridge. The
        // checkbox reflects the addon's reported state; toggling sends the command
        // and the addon re-broadcasts state. Its verbose output arrives as msg
        // events and appears in the log below. Disabled until connected.
        let mut dbg = self.addon_debug;
        let resp = ui.add_enabled(
            self.conn.is_some(),
            egui::Checkbox::new(
                &mut dbg,
                RichText::new("addon debug (//sw debug) -- verbose addon logging")
                    .color(if self.conn.is_some() { TEXT } else { MUTED })
                    .font(FontId::monospace(11.0)),
            ),
        );
        if resp.changed() {
            self.send(&format!("{{\"cmd\":\"debug\",\"on\":{}}}", dbg));
        }
        ui.horizontal(|ui| {
            ui.label(
                RichText::new(format!("{} log lines", self.log_lines.len()))
                    .color(MUTED)
                    .font(FontId::monospace(10.0)),
            );
            let clear = egui::Button::new(
                RichText::new("clear").color(MUTED).font(FontId::monospace(10.0)),
            )
            .fill(PANEL)
            .corner_radius(CornerRadius::same(3));
            if ui.add(clear).clicked() {
                self.log_lines.clear();
            }
        });
        ui.add_space(4.0);
        egui::Frame::NONE
            .fill(INPUT)
            .corner_radius(CornerRadius::same(4))
            .inner_margin(Margin::symmetric(8, 6))
            .show(ui, |ui| {
                egui::ScrollArea::vertical()
                    .auto_shrink([false; 2])
                    .max_height(220.0)
                    .stick_to_bottom(true)
                    .show(ui, |ui| {
                        if self.log_lines.is_empty() {
                            ui.label(
                                RichText::new("(no log yet)")
                                    .color(MUTED)
                                    .font(FontId::monospace(10.0)),
                            );
                        }
                        for (level, text) in &self.log_lines {
                            let col = match level.as_str() {
                                "error" => LOCKED,
                                "warning" => GOLD,
                                "debug" => AZURE_DIM,
                                _ => MUTED,
                            };
                            ui.label(
                                RichText::new(text).color(col).font(FontId::monospace(10.0)),
                            );
                        }
                    });
            });

        ui.add_space(10.0);

        // ---- About ----
        ui.label(
            RichText::new("About")
                .color(WHITE)
                .strong()
                .font(FontId::monospace(13.0)),
        );
        let av = if self.addon_version.is_empty() {
            "(not connected)"
        } else {
            &self.addon_version
        };
        ui.label(
            RichText::new(format!("GUI v{}   addon v{}", GUI_VERSION, av))
                .color(MUTED)
                .font(FontId::monospace(10.0)),
        );
        ui.label(
            RichText::new("by Eric Strawser (Seicz@Bahamut) - ITIWH.com")
                .color(MUTED)
                .font(FontId::monospace(9.0)),
        );
    }
}

// ---------------------------------------------------------------------------
// Fuzzy matching -- a faithful port of the in-game addon's fuzzyfind (lean).
// Two algorithms, three tiers via dest_fuzzy/dest_approx below:
//   * exact: cleaned candidate == cleaned needle.
//   * fuzzy: needle is a subsequence of a candidate -> gap-aware fzf-style score
//     (boundary / camelCase / consecutive bonuses, gap penalties) on the RAW
//     candidate so case + separators carry signal.
//   * approx: subsequence of nothing -> closest by normalized edit distance.
// Lineage: research+assembly by Lili; gap-aware scorer + lean build by Eric
// Strawser (Seicz@Bahamut). Ported to Rust for the GUI search.
// ---------------------------------------------------------------------------

const F_MATCH: i64 = 16;
const F_GAP_START: i64 = -3;
const F_GAP_EXT: i64 = -1;
const F_BON_BOUNDARY: i64 = 8;
const F_BON_CAMEL: i64 = 7;
const F_BON_CONSEC: i64 = 8;
const F_FIRST_MULT: i64 = 2;
const F_NEG: i64 = -1_000_000_000;
const FUZZY_EXACT: i64 = 1_000_000;

#[derive(Clone, Copy, PartialEq)]
enum Cls {
    White,
    NonWord,
    Lower,
    Upper,
    Digit,
}

fn fclass(b: u8) -> Cls {
    if b == b' ' || b == b'\t' {
        Cls::White
    } else if b.is_ascii_digit() {
        Cls::Digit
    } else if b.is_ascii_uppercase() {
        Cls::Upper
    } else if b.is_ascii_lowercase() {
        Cls::Lower
    } else {
        Cls::NonWord
    }
}

fn fis_word(c: Cls) -> bool {
    matches!(c, Cls::Lower | Cls::Upper | Cls::Digit)
}

fn fbonus(prev: Cls, cur: Cls) -> i64 {
    if !fis_word(cur) {
        return 0;
    }
    if prev == Cls::White || prev == Cls::NonWord {
        return F_BON_BOUNDARY;
    }
    if prev == Cls::Lower && cur == Cls::Upper {
        return F_BON_CAMEL;
    }
    if prev != Cls::Digit && cur == Cls::Digit {
        return F_BON_CAMEL;
    }
    0
}

// needle: cleaned (lowercase, alnum-only). text: RAW. Returns the gap-aware
// score, or None if needle is not a subsequence of text's alnum. O(m*n) with
// 1-indexed scratch arrays to mirror the Lua exactly.
fn fuzzy_score(needle: &str, text: &str) -> Option<i64> {
    let nb = needle.as_bytes();
    let tb = text.as_bytes();
    let m = nb.len();
    let n = tb.len();
    if m == 0 || n == 0 {
        return None;
    }

    let mut b = vec![0i64; n + 1];
    let mut tl = vec![0u8; n + 1];
    let mut prev = Cls::White;
    for j in 1..=n {
        let c = tb[j - 1];
        let cl = fclass(c);
        b[j] = fbonus(prev, cl);
        tl[j] = c.to_ascii_lowercase();
        prev = cl;
    }

    let mut hprev = vec![F_NEG; n + 1];
    let mut hcur = vec![F_NEG; n + 1];
    let p1 = nb[0].to_ascii_lowercase();
    for j in 1..=n {
        if tl[j] == p1 {
            let lead = if j == 1 {
                0
            } else {
                F_GAP_START + F_GAP_EXT * (j as i64 - 2)
            };
            hprev[j] = F_MATCH + b[j] * F_FIRST_MULT + lead;
        } else {
            hprev[j] = F_NEG;
        }
    }

    for i in 2..=m {
        let pi = nb[i - 1].to_ascii_lowercase();
        let mut gbest = F_NEG; // max over k<=j-2 of Hprev[k] - GAP_EXT*k
        for j in 1..=n {
            let k = j as i64 - 2;
            if k >= 1 {
                let v = hprev[k as usize];
                if v > F_NEG {
                    let c = v - F_GAP_EXT * k;
                    if c > gbest {
                        gbest = c;
                    }
                }
            }
            if tl[j] == pi {
                let mut best = F_NEG;
                if j >= 2 && hprev[j - 1] > F_NEG {
                    // consecutive
                    let bb = if b[j] > F_BON_CONSEC { b[j] } else { F_BON_CONSEC };
                    let a = hprev[j - 1] + bb;
                    if a > best {
                        best = a;
                    }
                }
                if gbest > F_NEG {
                    // gapped
                    let bg = b[j] + F_GAP_START + F_GAP_EXT * (j as i64 - 2) + gbest;
                    if bg > best {
                        best = bg;
                    }
                }
                hcur[j] = if best > F_NEG / 2 { F_MATCH + best } else { F_NEG };
            } else {
                hcur[j] = F_NEG;
            }
        }
        hprev[..=n].copy_from_slice(&hcur[..=n]);
    }

    let mut best = F_NEG;
    for j in m..=n {
        if hprev[j] > best {
            best = hprev[j];
        }
    }
    if best <= F_NEG / 2 {
        None
    } else {
        Some(best)
    }
}

// Wagner-Fischer Levenshtein (two-row), approx fallback only.
fn fuzzy_lev(a: &str, b: &str) -> usize {
    let a = a.as_bytes();
    let b = b.as_bytes();
    let col = b.len() + 1;
    let mut prev: Vec<usize> = (0..col).collect();
    let mut cur = vec![0usize; col];
    for i in 1..=a.len() {
        cur[0] = i;
        for j in 1..col {
            let cost = if a[i - 1] == b[j - 1] { 0 } else { 1 };
            cur[j] = (prev[j] + 1).min(cur[j - 1] + 1).min(prev[j - 1] + cost);
        }
        std::mem::swap(&mut prev, &mut cur);
    }
    prev[col - 1]
}

fn fuzzy_clean(s: &str) -> String {
    s.chars()
        .filter(|c| c.is_ascii_alphanumeric())
        .map(|c| c.to_ascii_lowercase())
        .collect()
}

// the searchable raw fields of a destination
fn dest_fields(d: &Dest) -> Vec<&str> {
    let mut v: Vec<&str> = vec![d.label.as_str(), d.zone.as_str()];
    for a in &d.aliases {
        v.push(a.as_str());
    }
    v
}

// exact/subsequence score for a destination, or None if no field matches.
fn dest_fuzzy(d: &Dest, needle_clean: &str) -> Option<i64> {
    let mut best: Option<i64> = None;
    for raw in dest_fields(d) {
        let v = if fuzzy_clean(raw) == needle_clean {
            Some(FUZZY_EXACT)
        } else {
            fuzzy_score(needle_clean, raw)
        };
        if let Some(s) = v {
            best = Some(match best {
                Some(b) => b.max(s),
                None => s,
            });
        }
    }
    best
}

// approx closeness in 0..=10000 (higher = closer), for the no-subsequence tier.
fn dest_approx(d: &Dest, needle_clean: &str) -> i64 {
    let mut best = 0i64;
    let nlen = needle_clean.chars().count();
    for raw in dest_fields(d) {
        let cl = fuzzy_clean(raw);
        let denom = nlen.max(cl.chars().count()).max(1);
        let lev = fuzzy_lev(needle_clean, &cl);
        let sim = 10000 - (lev * 10000 / denom) as i64;
        if sim > best {
            best = sim;
        }
    }
    best
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
            .width_range(120.0..=360.0)
            .frame(egui::Frame::NONE.fill(BG).inner_margin(Margin::symmetric(10, 8)))
            .show(ctx, |ui| self.render_systems(ui));

        egui::CentralPanel::default()
            .frame(egui::Frame::NONE.fill(BG).inner_margin(Margin::symmetric(12, 8)))
            .show(ctx, |ui| match self.view {
                View::Advanced => self.render_advanced(ui),
                View::Warp => self.render_destinations(ui),
            });
    }
}

// =========================================================================
// Embedded assets (built into the binary)
// =========================================================================

// Hack Regular - one TTF covering both Proportional and Monospace, replacing
// egui's `default_fonts` bundle (Hack + Ubuntu-Light + NotoEmoji + emoji-icon-
// font; ~1 MB embedded, of which we only ever rendered Hack). Sheds roughly
// that megabyte from the binary. License: SIL OFL 1.1 (assets/Hack-LICENSE.md).
const UI_FONT: &[u8] = include_bytes!("../assets/Hack-Regular.ttf");

// App icon. build.rs decodes assets/icon.png to a raw RGBA blob in OUT_DIR so
// the runtime binary doesn't drag in image / png / fdeflate / flate2 /
// miniz_oxide (~120 KiB of .text) purely to decode one icon at startup.
include!(concat!(env!("OUT_DIR"), "/icon_dims.rs"));
const ICON_RGBA: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/icon_rgba.bin"));

fn install_fonts(ctx: &egui::Context) {
    let mut fonts = egui::FontDefinitions::empty();
    fonts.font_data.insert(
        "ui".to_owned(),
        std::sync::Arc::new(egui::FontData::from_static(UI_FONT)),
    );
    fonts
        .families
        .entry(egui::FontFamily::Proportional)
        .or_default()
        .insert(0, "ui".to_owned());
    fonts
        .families
        .entry(egui::FontFamily::Monospace)
        .or_default()
        .insert(0, "ui".to_owned());
    ctx.set_fonts(fonts);
}

fn app_icon() -> egui::IconData {
    egui::IconData {
        rgba: ICON_RGBA.to_vec(),
        width: ICON_WIDTH,
        height: ICON_HEIGHT,
    }
}

// =========================================================================
// main
// =========================================================================

fn main() -> eframe::Result<()> {
    let cfg = Config::load();

    let mut viewport = egui::ViewportBuilder::default()
        .with_min_inner_size([300.0, 360.0])
        .with_title("superwarp — FFXI");

    // title-bar / taskbar / alt-tab icon (decoded at build time, not runtime)
    viewport = viewport.with_icon(std::sync::Arc::new(app_icon()));

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
            install_fonts(&cc.egui_ctx);
            setup_aether_theme(&cc.egui_ctx);
            let mut app = WarpApp::default();
            app.always_on_top = cfg.always_on_top;
            app.port_draft = cfg.port.to_string();
            app.host_draft = cfg.host.clone();
            app.config = cfg;
            Ok(Box::new(app))
        }),
    )
}
